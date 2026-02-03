/*
 * voodoo_emu.c - Voodoo emulator core functions
 *
 * SPDX-License-Identifier: BSD-3-Clause AND GPL-2.0-or-later
 *
 * Derived from DOSBox-Staging voodoo.cpp
 */

#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdio.h>
#include "voodoo_state.h"
#include "voodoo_pipeline.h"
#include "glide3x.h"

/* Debug logging from glide3x_state.c */
extern void debug_log(const char *msg);

/* Debug counters - reset each session via reset_debug_counters() */
int g_scanline_log_count = 0;
int g_texel_log_count = 0;
int g_tri_debug_count = 0;

void reset_debug_counters(void) {
    g_scanline_log_count = 0;
    g_texel_log_count = 0;
    g_tri_debug_count = 0;
}

/*************************************
 * Reciprocal/log lookup table
 * (exported for use by voodoo_pipeline.h)
 *************************************/

uint32_t voodoo_reciplog[(2 << RECIPLOG_LOOKUP_BITS) + 2];
static int reciplog_initialized = 0;

static void init_reciplog_table(void)
{
    if (reciplog_initialized) return;

    /* Build the reciprocal/log table with paired entries */
    for (int i = 0; i <= (1 << RECIPLOG_LOOKUP_BITS) + 1; i++) {
        uint32_t input = (uint32_t)i << (RECIPLOG_INPUT_PREC - RECIPLOG_LOOKUP_BITS);

        /* reciprocal entry (even index) */
        if (input == 0) {
            voodoo_reciplog[i * 2] = 0xFFFFFFFF;
        } else {
            voodoo_reciplog[i * 2] = (uint32_t)((((uint64_t)1 << (RECIPLOG_LOOKUP_PREC + RECIPLOG_INPUT_PREC)) / input) >> (RECIPLOG_INPUT_PREC - RECIPLOG_LOOKUP_PREC + 10));
        }

        /* log entry (odd index) */
        if (input == 0) {
            voodoo_reciplog[i * 2 + 1] = 0;
        } else {
            /* compute log2(input) as RECIPLOG_LOOKUP_PREC.0 fixed point */
            double logval = log2((double)input / (double)(1ULL << RECIPLOG_INPUT_PREC));
            voodoo_reciplog[i * 2 + 1] = (uint32_t)((-logval) * (1 << RECIPLOG_LOOKUP_PREC));
        }
    }

    reciplog_initialized = 1;
}

/*************************************
 * Dither lookup table for RGB565
 *************************************/

static uint8_t dither4_lookup[4 * 2048];
static uint8_t dither2_lookup[4 * 2048];
static int dither_initialized = 0;

static void init_dither_tables(void)
{
    if (dither_initialized) return;

    /* Build dither lookup tables for 4x4 and 2x2 patterns */
    for (int y = 0; y < 4; y++) {
        for (int val = 0; val < 256; val++) {
            for (int x = 0; x < 4; x++) {
                int dith4 = dither_matrix_4x4[y * 4 + x];
                int dith2 = dither_matrix_2x2[y * 4 + x];

                /* For R and B (5 bits) */
                int rb4 = ((val << 3) + dith4) >> 6;
                int rb2 = ((val << 3) + dith2) >> 6;
                if (rb4 > 31) rb4 = 31;
                if (rb2 > 31) rb2 = 31;

                /* For G (6 bits) */
                int g4 = ((val << 2) + dith4) >> 5;
                int g2 = ((val << 2) + dith2) >> 5;
                if (g4 > 63) g4 = 63;
                if (g2 > 63) g2 = 63;

                dither4_lookup[(y << 11) + (val << 3) + (x << 1) + 0] = rb4;
                dither4_lookup[(y << 11) + (val << 3) + (x << 1) + 1] = g4;
                dither2_lookup[(y << 11) + (val << 3) + (x << 1) + 0] = rb2;
                dither2_lookup[(y << 11) + (val << 3) + (x << 1) + 1] = g2;
            }
        }
    }

    dither_initialized = 1;
}

/*************************************
 * State creation/destruction
 *************************************/

voodoo_state* voodoo_create(void)
{
    voodoo_state *v = (voodoo_state*)calloc(1, sizeof(voodoo_state));
    if (!v) return NULL;

    init_reciplog_table();
    init_dither_tables();

    /* Set default chip configuration */
    v->chipmask = 0x01;  /* FBI only initially */

    /* Set default register values */
    v->reg[fbzMode].u = (1 << 9);  /* RGB buffer write enabled */

    return v;
}

void voodoo_destroy(voodoo_state *v)
{
    if (!v) return;

    /* Free FBI RAM */
    if (v->fbi.ram) {
        free(v->fbi.ram);
        v->fbi.ram = NULL;
    }

    /* Free TMU RAM */
    for (int i = 0; i < MAX_TMU; i++) {
        if (v->tmu[i].ram) {
            free(v->tmu[i].ram);
            v->tmu[i].ram = NULL;
        }
    }

    free(v);
}

/*************************************
 * FBI (Frame Buffer Interface) init
 *************************************/

/* Counter for FBI INIT events, exported for triangle tracking */
int g_fbi_init_count = 0;

void voodoo_init_fbi(fbi_state *f, int fbmem)
{
    if (fbmem < 1) fbmem = 1;

    g_fbi_init_count++;

    /* Allocate frame buffer RAM (aligned to 8 bytes) */
    f->ram = (uint8_t*)calloc(1, fbmem + 8);
    if (!f->ram) return;

    /* Align to 8-byte boundary */
    while ((uintptr_t)f->ram & 7) {
        f->ram++;
    }

    f->mask = (uint32_t)(fbmem - 1);
    f->rgboffs[0] = f->rgboffs[1] = f->rgboffs[2] = 0;
    f->auxoffs = (uint32_t)(~0);

    /* Default framebuffer settings */
    f->frontbuf = 0;
    f->backbuf = 1;
    f->width = 640;
    f->height = 480;
    f->rowpixels = 640;

    f->vblank = 0;

    /* Initialize fog tables */
    memset(f->fogblend, 0, sizeof(f->fogblend));
    memset(f->fogdelta, 0, sizeof(f->fogdelta));
    f->fogdelta_mask = 0xff;  /* Voodoo 1 style */

    f->yorigin = 0;
    f->sverts = 0;

    memset(&f->lfb_stats, 0, sizeof(f->lfb_stats));
}

/*************************************
 * TMU (Texture Mapping Unit) init
 *************************************/

void voodoo_init_tmu(tmu_state *t, voodoo_reg *reg, int tmumem)
{
    if (tmumem < 1) tmumem = 1;

    /* Allocate texture RAM */
    t->ram = (uint8_t*)calloc(1, tmumem + 8);
    if (!t->ram) return;

    /* Align to 8-byte boundary */
    while ((uintptr_t)t->ram & 7) {
        t->ram++;
    }

    t->mask = (uint32_t)(tmumem - 1);
    t->reg = reg;
    t->regdirty = 1;

    /* Initialize LOD settings */
    t->lodmin = 0;
    t->lodmax = 8;  /* 256x256 max */
    t->lodbias = 0;
    t->lodmask = 0x1FF;

    /* Default texture size masks */
    t->wmask = 0xFF;  /* 256 pixels */
    t->hmask = 0xFF;

    t->bilinear_mask = 0xF0;  /* Voodoo 1 style */

    /* Initialize NCC tables */
    memset(&t->ncc[0], 0, sizeof(ncc_table));
    memset(&t->ncc[1], 0, sizeof(ncc_table));

    /* Initialize palettes to grayscale */
    for (int i = 0; i < 256; i++) {
        t->palette[i] = MAKE_ARGB(255, i, i, i);
        t->palettea[i] = MAKE_ARGB(i, i, i, i);
    }
}

/*************************************
 * TMU shared state init (lookup tables)
 *************************************/

void voodoo_init_tmu_shared(tmu_shared_state *s)
{
    /* Build RGB 3-3-2 lookup table */
    for (int val = 0; val < 256; val++) {
        int r = (val >> 5) & 7;
        int g = (val >> 2) & 7;
        int b = val & 3;
        r = (r << 5) | (r << 2) | (r >> 1);
        g = (g << 5) | (g << 2) | (g >> 1);
        b = (b << 6) | (b << 4) | (b << 2) | b;
        s->rgb332[val] = MAKE_ARGB(255, r, g, b);
    }

    /* Build alpha 8-bit lookup table */
    for (int val = 0; val < 256; val++) {
        s->alpha8[val] = MAKE_ARGB(val, val, val, val);
    }

    /* Build intensity 8-bit lookup table */
    for (int val = 0; val < 256; val++) {
        s->int8[val] = MAKE_ARGB(255, val, val, val);
    }

    /* Build alpha/intensity 4-4 lookup table */
    for (int val = 0; val < 256; val++) {
        int a = (val >> 4) & 0xF;
        int i = val & 0xF;
        a = (a << 4) | a;
        i = (i << 4) | i;
        s->ai44[val] = MAKE_ARGB(a, i, i, i);
    }

    /* Build RGB 5-6-5 lookup table */
    for (int val = 0; val < 65536; val++) {
        int r = (val >> 11) & 0x1F;
        int g = (val >> 5) & 0x3F;
        int b = val & 0x1F;
        r = (r << 3) | (r >> 2);
        g = (g << 2) | (g >> 4);
        b = (b << 3) | (b >> 2);
        s->rgb565[val] = MAKE_ARGB(255, r, g, b);
    }

    /* Build ARGB 1-5-5-5 lookup table */
    for (int val = 0; val < 65536; val++) {
        int a = (val >> 15) & 1;
        int r = (val >> 10) & 0x1F;
        int g = (val >> 5) & 0x1F;
        int b = val & 0x1F;
        a = a ? 255 : 0;
        r = (r << 3) | (r >> 2);
        g = (g << 3) | (g >> 2);
        b = (b << 3) | (b >> 2);
        s->argb1555[val] = MAKE_ARGB(a, r, g, b);
    }

    /* Build ARGB 4-4-4-4 lookup table */
    for (int val = 0; val < 65536; val++) {
        int a = (val >> 12) & 0xF;
        int r = (val >> 8) & 0xF;
        int g = (val >> 4) & 0xF;
        int b = val & 0xF;
        a = (a << 4) | a;
        r = (r << 4) | r;
        g = (g << 4) | g;
        b = (b << 4) | b;
        s->argb4444[val] = MAKE_ARGB(a, r, g, b);
    }
}

/*************************************
 * Helper: round coordinate
 *************************************/

static inline int32_t round_coordinate(float value)
{
    int32_t rounded = (int32_t)value;
    if (value - (float)rounded > 0.5f)
        rounded++;
    return rounded;
}

/*************************************
 * Rasterize a single scanline
 *************************************/

/* Diagnostic logging counter - log first few pixels per frame */
int diag_pixel_count = 0;

static void raster_scanline(voodoo_state *vs, uint16_t *dest, uint16_t *depth,
                            int32_t y, int32_t startx, int32_t stopx,
                            int64_t iterr, int64_t iterg, int64_t iterb, int64_t itera,
                            int32_t iterz, int64_t iterw,
                            int64_t iters0, int64_t itert0, int64_t iterw0,
                            int64_t iters1, int64_t itert1, int64_t iterw1,
                            stats_block *stats)
{
    const voodoo_reg *regs = vs->reg;
    const fbi_state *fbi = &vs->fbi;
    const tmu_state *tmu0 = &vs->tmu[0];
    const tmu_state *tmu1 = &vs->tmu[1];

    /*
     * Determine which TMU(s) are active based on lodmin.
     * TMU is active if lodmin < (8 << 8). Games disable a TMU by setting lodmin >= 8.
     * For single-texture rendering, only one TMU will be active.
     */
    int tmu0_active = (tmu0->lodmin < (8 << 8));
    int tmu1_active = (tmu1->lodmin < (8 << 8));

    /* Select the active TMU for texture fetches (prefer TMU1 if both active, as per hardware) */
    int active_tmu_index = tmu1_active ? 1 : 0;
    const tmu_state *active_tmu = &vs->tmu[active_tmu_index];

    const uint32_t r_fbzColorPath = regs[fbzColorPath].u;
    const uint32_t r_fbzMode = regs[fbzMode].u;
    const uint32_t r_alphaMode = regs[alphaMode].u;
    const uint32_t r_fogMode = regs[fogMode].u;
    const uint32_t r_zaColor = regs[zaColor].u;
    const uint32_t r_textureMode = active_tmu->reg ? active_tmu->reg->u : 0;
    uint32_t r_stipple = regs[stipple].u;

    /* Check if texture is enabled */
    int texture_enabled = FBZCP_TEXTURE_ENABLE(r_fbzColorPath);

    /* Compute dither pointers */
    const uint8_t *dither = NULL;
    const uint8_t *dither4 = NULL;
    const uint8_t *dither_lookup = NULL;

    if (FBZMODE_ENABLE_DITHERING(r_fbzMode)) {
        dither4 = &dither_matrix_4x4[(y & 3) * 4];
        if (FBZMODE_DITHER_TYPE(r_fbzMode) == 0) {
            dither = dither4;
            dither_lookup = &dither4_lookup[(y & 3) << 11];
        } else {
            dither = &dither_matrix_2x2[(y & 3) * 4];
            dither_lookup = &dither2_lookup[(y & 3) << 11];
        }
    }

    /* Loop over pixels */
    for (int32_t x = startx; x < stopx; x++) {
        rgb_union iterargb = { 0 };

        /* Pixel pipeline begin - depth testing and stippling */
        PIXEL_PIPELINE_BEGIN(vs, (*stats), x, y, r_fbzColorPath, r_fbzMode,
                             iterz, iterw, r_zaColor, r_stipple);

        /* Clamp iterated RGBA */
        CLAMPED_ARGB(iterr, iterg, iterb, itera, r_fbzColorPath, iterargb);

        /* Get base color from iterated values */
        r = iterargb.rgb.r;
        g = iterargb.rgb.g;
        b = iterargb.rgb.b;
        a = iterargb.rgb.a;

        /* Apply texture if enabled */
        if (texture_enabled) {
            /* Use texture coordinates for the active TMU */
            int64_t iters = (active_tmu_index == 1) ? iters1 : iters0;
            int64_t itert = (active_tmu_index == 1) ? itert1 : itert0;
            int64_t iterw_tex = (active_tmu_index == 1) ? iterw1 : iterw0;

            rgb_t texel;
            TEXTURE_PIPELINE(vs, active_tmu_index, x, dither4, r_textureMode,
                             iters, itert, iterw_tex, texel);

            /* Force compiler to not optimize away TMU access */
            {
                volatile rgb_t sink = texel;
                (void)sink;
            }

            /* Texture combine based on fbzColorPath settings */
            rgb_union c_local;
            c_local.rgb.r = r;
            c_local.rgb.g = g;
            c_local.rgb.b = b;
            c_local.rgb.a = a;

            rgb_union c_texel;
            c_texel.u = texel;

            /* Texture combine based on CC_RGBSELECT
             *
             * CC_RGBSELECT determines the "other" color source:
             *   0 = Iterated (vertex) color
             *   1 = Texture color
             *   2 = Color1 register
             *
             * However, many games enable texturing via grAlphaCombine but don't
             * call grColorCombine. In this case, CC_RGBSELECT remains 0 but the
             * game still expects texture to be visible.
             *
             * For compatibility: when texture is enabled and CC_RGBSELECT=0,
             * check if the combine equation bits suggest "pass through other".
             * If CC_ZERO_OTHER is not set and no add/sub operations are set,
             * use texture color as the output (decal mode).
             */
            switch (FBZCP_CC_RGBSELECT(r_fbzColorPath)) {
            case 0:  /* Iterated color as "other" source */
                /* Check if this looks like an unconfigured state where
                 * the game expects texture output. If CC_ADD_CLOCAL is not
                 * set (no local color contribution), use texture instead
                 * of the likely-black iterated color.
                 */
                if (!FBZCP_CC_ZERO_OTHER(r_fbzColorPath) &&
                    FBZCP_CC_ADD_ACLOCAL(r_fbzColorPath) == 0) {
                    /* Decal mode fallback: use texture color directly */
                    r = c_texel.rgb.r;
                    g = c_texel.rgb.g;
                    b = c_texel.rgb.b;
                }
                /* else: keep iterated color (for modulation or other effects) */
                break;
            case 1:  /* Texture color */
                r = c_texel.rgb.r;
                g = c_texel.rgb.g;
                b = c_texel.rgb.b;
                break;
            case 2:  /* Color 1 register */
                r = regs[color1].rgb.r;
                g = regs[color1].rgb.g;
                b = regs[color1].rgb.b;
                break;
            default:
                break;
            }

            /* Handle alpha select */
            switch (FBZCP_CC_ASELECT(r_fbzColorPath)) {
            case 0:  /* Iterated alpha */
                break;
            case 1:  /* Texture alpha */
                a = c_texel.rgb.a;
                break;
            case 2:  /* Color 1 alpha */
                a = regs[color1].rgb.a;
                break;
            default:
                break;
            }
        }

        /* Pixel pipeline modify - fogging and alpha blend */
        PIXEL_PIPELINE_MODIFY(vs, dither, dither4, x, r_fbzMode, r_fbzColorPath,
                              r_alphaMode, r_fogMode, iterz, iterw, iterargb);

        /* Pixel pipeline finish - write to framebuffer */
        PIXEL_PIPELINE_FINISH(vs, dither_lookup, x, dest, depth, r_fbzMode);

        PIXEL_PIPELINE_END((*stats));

        /* Update iterators */
        iterr += fbi->drdx;
        iterg += fbi->dgdx;
        iterb += fbi->dbdx;
        itera += fbi->dadx;
        iterz += fbi->dzdx;
        iterw += fbi->dwdx;

        /* Update texture iterators for both TMUs */
        iters0 += tmu0->dsdx;
        itert0 += tmu0->dtdx;
        iterw0 += tmu0->dwdx;
        iters1 += tmu1->dsdx;
        itert1 += tmu1->dtdx;
        iterw1 += tmu1->dwdx;
    }
}

/*************************************
 * Triangle rendering
 *************************************/

void voodoo_triangle(voodoo_state *vs)
{
    const voodoo_reg *regs = vs->reg;
    fbi_state *fbi = &vs->fbi;

    /* Get vertices from fixed-point coordinates (12.4 format) */
    float ax = (float)fbi->ax / 16.0f;
    float ay = (float)fbi->ay / 16.0f;
    float bx = (float)fbi->bx / 16.0f;
    float by = (float)fbi->by / 16.0f;
    float cx = (float)fbi->cx / 16.0f;
    float cy = (float)fbi->cy / 16.0f;

    /* Sort vertices by Y coordinate */
    float v1x = ax, v1y = ay;
    float v2x = bx, v2y = by;
    float v3x = cx, v3y = cy;

    if (v2y < v1y) {
        float t;
        t = v1x; v1x = v2x; v2x = t;
        t = v1y; v1y = v2y; v2y = t;
    }
    if (v3y < v2y) {
        float t;
        t = v2x; v2x = v3x; v3x = t;
        t = v2y; v2y = v3y; v3y = t;
        if (v2y < v1y) {
            t = v1x; v1x = v2x; v2x = t;
            t = v1y; v1y = v2y; v2y = t;
        }
    }

    /* Compute integral Y values */
    int32_t v1yi = round_coordinate(v1y);
    int32_t v3yi = round_coordinate(v3y);

    /* Degenerate triangle check */
    if (v3yi <= v1yi)
        return;

    /* Determine draw buffer */
    uint16_t *drawbuf;
    switch (FBZMODE_DRAW_BUFFER(regs[fbzMode].u)) {
    case 0:  /* front buffer */
        drawbuf = (uint16_t*)(fbi->ram + fbi->rgboffs[fbi->frontbuf]);
        break;
    case 1:  /* back buffer */
        drawbuf = (uint16_t*)(fbi->ram + fbi->rgboffs[fbi->backbuf]);
        break;
    default:
        return;  /* reserved */
    }

    /* Get depth buffer if enabled */
    uint16_t *depthbuf = NULL;
    if (fbi->auxoffs != (uint32_t)(~0)) {
        depthbuf = (uint16_t*)(fbi->ram + fbi->auxoffs);
    }

    /* Compute edge slopes */
    float dxdy_v1v2 = (v2y == v1y) ? 0.0f : (v2x - v1x) / (v2y - v1y);
    float dxdy_v1v3 = (v3y == v1y) ? 0.0f : (v3x - v1x) / (v3y - v1y);
    float dxdy_v2v3 = (v3y == v2y) ? 0.0f : (v3x - v2x) / (v3y - v2y);

    stats_block my_stats = {0};

    /* Rasterize scanlines */
    for (int32_t y = v1yi; y < v3yi; y++) {
        float fully = (float)y + 0.5f;

        /* Compute left edge (always v1->v3) */
        float startx = v1x + (fully - v1y) * dxdy_v1v3;

        /* Compute right edge (v1->v2 or v2->v3 depending on which half) */
        float stopx;
        if (fully < v2y)
            stopx = v1x + (fully - v1y) * dxdy_v1v2;
        else
            stopx = v2x + (fully - v2y) * dxdy_v2v3;

        /* Clamp to integers */
        int32_t istartx = round_coordinate(startx);
        int32_t istopx = round_coordinate(stopx);

        /* Ensure start < stop */
        if (istartx >= istopx) {
            if (istartx == istopx)
                continue;
            int32_t t = istartx;
            istartx = istopx;
            istopx = t;
        }

        /* Apply Y origin */
        int32_t scry = y;
        if (FBZMODE_Y_ORIGIN(regs[fbzMode].u))
            scry = (fbi->yorigin - y) & 0x3ff;

        /* Get pointers for this scanline */
        uint16_t *dest = drawbuf + scry * fbi->rowpixels;
        uint16_t *depth = depthbuf ? (depthbuf + scry * fbi->rowpixels) : NULL;

        /* Compute starting iterator values */
        int32_t dx = istartx - (fbi->ax >> 4);
        int32_t dy = y - (fbi->ay >> 4);

        int64_t iterr = fbi->startr + dy * fbi->drdy + dx * fbi->drdx;
        int64_t iterg = fbi->startg + dy * fbi->dgdy + dx * fbi->dgdx;
        int64_t iterb = fbi->startb + dy * fbi->dbdy + dx * fbi->dbdx;
        int64_t itera = fbi->starta + dy * fbi->dady + dx * fbi->dadx;
        int32_t iterz = fbi->startz + dy * fbi->dzdy + dx * fbi->dzdx;
        int64_t iterw = fbi->startw + dy * fbi->dwdy + dx * fbi->dwdx;

        /* Compute texture coordinates for both TMUs */
        tmu_state *tmu0 = &vs->tmu[0];
        tmu_state *tmu1 = &vs->tmu[1];

        int64_t iters0 = tmu0->starts + dy * tmu0->dsdy + dx * tmu0->dsdx;
        int64_t itert0 = tmu0->startt + dy * tmu0->dtdy + dx * tmu0->dtdx;
        int64_t iterw0 = tmu0->startw + dy * tmu0->dwdy + dx * tmu0->dwdx;

        int64_t iters1 = tmu1->starts + dy * tmu1->dsdy + dx * tmu1->dsdx;
        int64_t itert1 = tmu1->startt + dy * tmu1->dtdy + dx * tmu1->dtdx;
        int64_t iterw1 = tmu1->startw + dy * tmu1->dwdy + dx * tmu1->dwdx;

        /* Rasterize this scanline */
        raster_scanline(vs, dest, depth, y, istartx, istopx,
                        iterr, iterg, iterb, itera, iterz, iterw,
                        iters0, itert0, iterw0, iters1, itert1, iterw1, &my_stats);
    }

    /* Accumulate statistics */
    fbi->lfb_stats.pixels_in += my_stats.pixels_in;
    fbi->lfb_stats.pixels_out += my_stats.pixels_out;
    fbi->lfb_stats.chroma_fail += my_stats.chroma_fail;
    fbi->lfb_stats.zfunc_fail += my_stats.zfunc_fail;
    fbi->lfb_stats.afunc_fail += my_stats.afunc_fail;

    /* Update triangle count */
    vs->reg[fbiPixelsOut].u++;
}

/*************************************
 * Fast fill
 *************************************/

void voodoo_fastfill(voodoo_state *vs)
{
    const voodoo_reg *regs = vs->reg;
    fbi_state *fbi = &vs->fbi;

    /* Get clip rectangle */
    int32_t sx = (regs[clipLeftRight].u >> 16) & 0x3ff;
    int32_t ex = regs[clipLeftRight].u & 0x3ff;
    int32_t sy = (regs[clipLowYHighY].u >> 16) & 0x3ff;
    int32_t ey = regs[clipLowYHighY].u & 0x3ff;

    /* Determine draw buffer */
    uint16_t *drawbuf;
    switch (FBZMODE_DRAW_BUFFER(regs[fbzMode].u)) {
    case 0:  /* front buffer */
        drawbuf = (uint16_t*)(fbi->ram + fbi->rgboffs[fbi->frontbuf]);
        break;
    case 1:  /* back buffer */
        drawbuf = (uint16_t*)(fbi->ram + fbi->rgboffs[fbi->backbuf]);
        break;
    default:
        return;
    }

    /* Get fill color from color1 register */
    uint32_t color = regs[color1].u;
    uint8_t r = (color >> 16) & 0xff;
    uint8_t g = (color >> 8) & 0xff;
    uint8_t b = color & 0xff;

    /* Convert to RGB565 */
    uint16_t rgb565 = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);

    /* Get depth buffer and value if needed */
    uint16_t *depthbuf = NULL;
    uint16_t depthval = (uint16_t)(regs[zaColor].u & 0xffff);
    if (fbi->auxoffs != (uint32_t)(~0) && FBZMODE_AUX_BUFFER_MASK(regs[fbzMode].u)) {
        depthbuf = (uint16_t*)(fbi->ram + fbi->auxoffs);
    }

    /* Fill the rectangle */
    for (int32_t y = sy; y < ey; y++) {
        int32_t scry = y;
        if (FBZMODE_Y_ORIGIN(regs[fbzMode].u))
            scry = (fbi->yorigin - y) & 0x3ff;

        uint16_t *dest = drawbuf + scry * fbi->rowpixels;
        uint16_t *depth = depthbuf ? (depthbuf + scry * fbi->rowpixels) : NULL;

        for (int32_t x = sx; x < ex; x++) {
            if (FBZMODE_RGB_BUFFER_MASK(regs[fbzMode].u))
                dest[x] = rgb565;
            if (depth && FBZMODE_AUX_BUFFER_MASK(regs[fbzMode].u))
                depth[x] = depthval;
        }
    }
}

/*************************************
 * Buffer swap
 *************************************/

void voodoo_swapbuffer(voodoo_state *v)
{
    uint8_t temp = v->fbi.frontbuf;
    v->fbi.frontbuf = v->fbi.backbuf;
    v->fbi.backbuf = temp;
}
