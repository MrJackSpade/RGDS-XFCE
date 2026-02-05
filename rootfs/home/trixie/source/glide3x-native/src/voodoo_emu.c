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
#include <assert.h>
#include <limits.h>
#include <stdatomic.h>
#include <pthread.h>
#include "voodoo_state.h"
#include "voodoo_pipeline.h"
#include "glide3x.h"

/* Debug logging from glide3x_state.c */
#include "glide3x_state.h"

/* Forward declarations */
static void recompute_texture_params(tmu_state* t);
static void ncc_table_update(ncc_table* n);
static void triangle_worker_shutdown(triangle_worker *tworker);
static void triangle_worker_run(triangle_worker *tworker);

/* Global voodoo state for worker threads (set before triangle_worker_run) */
static voodoo_state *v = NULL;

/*************************************
 *
 *  Float-to-int conversions
 *
 *************************************/

static inline int32_t float_to_int32(uint32_t data, int fixedbits)
{
    /* Clamp the exponent to the type's shift limit (31 bits for int32_t) */
    const int max_shift = 31;
    int exponent = ((data >> 23) & 0xff) - 127 - 23 + fixedbits;
    if (exponent < -max_shift) exponent = -max_shift;
    if (exponent > max_shift) exponent = max_shift;

    int32_t result = (data & 0x7fffff) | 0x800000;

    if (exponent < 0) {
        if (exponent > -max_shift) {
            result >>= -exponent;
        }
        else {
            result = 0;
        }
    }
    else {
        int64_t temp = (int64_t)result << exponent;
        if (temp > INT32_MAX) result = INT32_MAX;
        else if (temp < INT32_MIN) result = INT32_MIN;
        else result = (int32_t)temp;
    }
    if ((data & 0x80000000) != 0u) {
        result = -result;
    }
    return result;
}

static inline int64_t float_to_int64(uint32_t data, int fixedbits)
{
    int exponent = ((data >> 23) & 0xff) - 127 - 23 + fixedbits;
    int64_t result = (data & 0x7fffff) | 0x800000;
    if (exponent < 0)
    {
        if (exponent > -64) {
            result >>= -exponent;
        }
        else {
            result = 0;
        }
    }
    else
    {
        if (exponent < 64) {
            result <<= exponent;
        }
        else {
            result = 0x7fffffffffffffffLL;
        }
    }
    if ((data & 0x80000000) != 0u) {
        result = -result;
    }
    return result;
}


/*************************************
 *
 *  NCC table management
 *
 *************************************/

static void ncc_table_write(ncc_table* n, uint32_t regnum, uint32_t data)
{
    /* I/Q entries reference the palette if the high bit is set */
    if (regnum >= 4 && ((data & 0x80000000) != 0u) && (n->palette != NULL))
    {
        const uint32_t index = ((data >> 23) & 0xfe) | (regnum & 1);

        const rgb_t palette_entry = 0xff000000 | data;

        if (n->palette[index] != palette_entry) {
            /* set the ARGB for this palette index */
            n->palette[index] = palette_entry;
#ifdef C_ENABLE_VOODOO_OPENGL
            v->ogl_palette_changed = true;
#endif
        }

        /* if we have an ARGB palette as well, compute its value */
        if (n->palettea != NULL)
        {
            const uint32_t a = ((data >> 16) & 0xfc) |
                ((data >> 22) & 0x03);

            const uint32_t r = ((data >> 10) & 0xfc) |
                ((data >> 16) & 0x03);

            const uint32_t g = ((data >> 4) & 0xfc) |
                ((data >> 10) & 0x03);

            const uint32_t b = ((data << 2) & 0xfc) |
                ((data >> 4) & 0x03);

            n->palettea[index] = MAKE_ARGB(a, r, g, b);
        }

        /* this doesn't dirty the table or go to the registers, so bail */
        return;
    }

    /* if the register matches, don't update */
    if (data == n->reg[regnum].u) {
        return;
    }
    n->reg[regnum].u = data;

    /* first four entries are packed Y values */
    if (regnum < 4)
    {
        regnum *= 4;
        n->y[regnum + 0] = (data >> 0) & 0xff;
        n->y[regnum + 1] = (data >> 8) & 0xff;
        n->y[regnum + 2] = (data >> 16) & 0xff;
        n->y[regnum + 3] = (data >> 24) & 0xff;
    }

    /* the second four entries are the I RGB values */
    else if (regnum < 8)
    {
        regnum &= 3;
        n->ir[regnum] = (int32_t)(data << 5) >> 23;
        n->ig[regnum] = (int32_t)(data << 14) >> 23;
        n->ib[regnum] = (int32_t)(data << 23) >> 23;
    }

    /* the final four entries are the Q RGB values */
    else
    {
        regnum &= 3;
        n->qr[regnum] = (int32_t)(data << 5) >> 23;
        n->qg[regnum] = (int32_t)(data << 14) >> 23;
        n->qb[regnum] = (int32_t)(data << 23) >> 23;
    }

    /* mark the table dirty */
    n->dirty = true;
}

static void ncc_table_update(ncc_table* n)
{
    int r;
    int g;
    int b;
    int i;

    /* generte all 256 possibilities */
    for (i = 0; i < 256; i++)
    {
        const int vi = (i >> 2) & 0x03;
        const int vq = (i >> 0) & 0x03;

        /* start with the intensity */
        r = g = b = n->y[(i >> 4) & 0x0f];

        /* add the coloring */
        r += n->ir[vi] + n->qr[vq];
        g += n->ig[vi] + n->qg[vq];
        b += n->ib[vi] + n->qb[vq];

        /* clamp */
        r = clamp_to_uint8(r);
        g = clamp_to_uint8(g);
        b = clamp_to_uint8(b);

        /* fill in the table */
        n->texel[i] = MAKE_ARGB(0xff, r, g, b);
    }

    /* no longer dirty */
    n->dirty = false;
}



/*************************************
 *
 *  Faux DAC implementation
 *
 *************************************/

static void dacdata_w(dac_state* d, uint8_t regnum, uint8_t data)
{
    d->reg[regnum] = data;
}

static void dacdata_r(dac_state* d, uint8_t regnum)
{
    uint8_t result = 0xff;

    /* switch off the DAC register requested */
    switch (regnum)
    {
    case 5:
        /* this is just to make startup happy */
        switch (d->reg[7])
        {
        case 0x01:	result = 0x55; break;
        case 0x07:	result = 0x71; break;
        case 0x0b:	result = 0x79; break;
        }
        break;

    default:
        result = d->reg[regnum];
        break;
    }

    /* remember the read result; it is fetched elsewhere */
    d->read_result = result;
}

/*************************************
 * Reciprocal/log lookup table
 * (exported for use by voodoo_pipeline.h)
 *************************************/

uint32_t voodoo_reciplog[(2 << RECIPLOG_LOOKUP_BITS) + 4];
static int reciplog_initialized = 0;

static void init_reciplog_table(void)
{
    if (reciplog_initialized) return;

    /* Build the reciprocal/log table with paired entries */
    for (int i = 0; i <= (1 << RECIPLOG_LOOKUP_BITS) + 1; i++) {
        /* Use 64-bit to avoid overflow when i >= 512 (512 << 23 = 2^32) */
        uint64_t input64 = (uint64_t)i << (RECIPLOG_INPUT_PREC - RECIPLOG_LOOKUP_BITS);

        /* Clamp to 32-bit max to avoid division issues */
        uint32_t input = (input64 > 0xFFFFFFFF) ? 0xFFFFFFFF : (uint32_t)input64;

        /* reciprocal entry (even index) */
        if (input == 0) {
            voodoo_reciplog[i * 2] = 0xFFFFFFFF;
        } else if (input == 0xFFFFFFFF) {
            /* Near-minimum reciprocal for saturated input */
            voodoo_reciplog[i * 2] = 1;
        } else {
            voodoo_reciplog[i * 2] = (uint32_t)((((uint64_t)1 << (RECIPLOG_LOOKUP_PREC + RECIPLOG_INPUT_PREC)) / input) >> (RECIPLOG_INPUT_PREC - RECIPLOG_LOOKUP_PREC + 10));
        }

        /* log entry (odd index) */
        if (input == 0) {
            voodoo_reciplog[i * 2 + 1] = 0;
        } else {
            /* compute log2(input) as RECIPLOG_LOOKUP_PREC.0 fixed point */
            double logval = log2((double)input64 / (double)(1ULL << RECIPLOG_INPUT_PREC));
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

    /* Build dither lookup tables for 4x4 and 2x2 patterns
     * Formula from DOSBox-Staging, confirmed hardware-accurate */
    for (int y = 0; y < 4; y++) {
        for (int val = 0; val < 256; val++) {
            for (int x = 0; x < 4; x++) {
                int dith4 = dither_matrix_4x4[y * 4 + x];
                int dith2 = dither_matrix_2x2[y * 4 + x];

                /* For R and B (5 bits): (colour << 1) - (colour >> 4) + (colour >> 7) + amount, then >> 4 */
                int rb4 = ((val << 1) - (val >> 4) + (val >> 7) + dith4) >> 4;
                int rb2 = ((val << 1) - (val >> 4) + (val >> 7) + dith2) >> 4;
                if (rb4 > 31) rb4 = 31;
                if (rb2 > 31) rb2 = 31;

                /* For G (6 bits): (colour << 2) - (colour >> 4) + (colour >> 6) + amount, then >> 4 */
                int g4 = ((val << 2) - (val >> 4) + (val >> 6) + dith4) >> 4;
                int g2 = ((val << 2) - (val >> 4) + (val >> 6) + dith2) >> 4;
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
    /* DOSBox: v = new voodoo_state(num_additional_threads)
       where num_additional_threads = get_num_total_threads() - 1
       We use 0 for single-threaded operation by default */
    const int num_threads = 0;

    voodoo_state *vs = (voodoo_state*)calloc(1, sizeof(voodoo_state));
    if (!vs) return NULL;

    init_reciplog_table();
    init_dither_tables();

    /* Initialize triangle_worker (matches DOSBox constructor):
       triangle_worker(const int num_threads_)
           : num_threads(num_threads_),
             num_work_units((num_threads + 1) * 4),
             threads(num_threads) */
    vs->tworker.num_threads = num_threads;
    vs->tworker.num_work_units = (num_threads + 1) * 4;
    vs->tworker.disable_bilinear_filter = false;
    atomic_init(&vs->tworker.threads_active, false);
    atomic_init(&vs->tworker.work_index, INT_MAX);
    atomic_init(&vs->tworker.done_count, 0);
    pthread_mutex_init(&vs->tworker.mutex, NULL);
    pthread_cond_init(&vs->tworker.cond, NULL);

    /* Set default chip configuration */
    vs->chipmask = 0x01;  /* FBI only initially */
    vs->vtype = VOODOO_2; /* Voodoo 2 for Glide 3.x */

    /* Enable hardware init by default */
    vs->pci.init_enable = 1;

    /* Set default register values */
    vs->reg[fbzMode].u = (1 << 9);  /* RGB buffer write enabled */

    /*
     * Default textureMode: "replace" mode - output c_local (the texture)
     *
     * RGB bits:
     *   TC_ZERO_OTHER (bit 12) = 1: zero the "other" input
     *   TC_ADD_CLOCAL (bit 18) = 1: add c_local to result
     *   Result: (0 * blend) + c_local = c_local
     *
     * Alpha bits:
     *   TCA_ZERO_OTHER (bit 21) = 1: zero the "other" alpha
     *   TCA_ADD_ALOCAL (bit 28) = 1: add a_local to result
     *   Result: 0 + a_local = a_local
     *
     * Without this default, textureMode=0 causes:
     * _blend=0, no reverse, no add_clocal -> result = other/256 ≈ black
     *
     * TMU accesses t->reg[textureMode], so absolute address = base + textureMode
     */
    #define DEFAULT_TEXMODE ((1 << 12) | (1 << 18) | (1 << 21) | (1 << 28))
    vs->reg[TMU0_REG_BASE + textureMode].u = DEFAULT_TEXMODE;
    vs->reg[TMU1_REG_BASE + textureMode].u = DEFAULT_TEXMODE;
    #undef DEFAULT_TEXMODE

    return vs;
}

void voodoo_destroy(voodoo_state *vs)
{
    if (!vs) return;

    /* Shutdown triangle worker threads (DOSBox: triangle_worker_shutdown(v->tworker)) */
    triangle_worker_shutdown(&vs->tworker);
    pthread_mutex_destroy(&vs->tworker.mutex);
    pthread_cond_destroy(&vs->tworker.cond);

    /* Free FBI RAM */
    if (vs->fbi.ram) {
        free(vs->fbi.ram);
        vs->fbi.ram = NULL;
    }

    /* Free TMU RAM */
    for (int i = 0; i < MAX_TMU; i++) {
        if (vs->tmu[i].ram) {
            free(vs->tmu[i].ram);
            vs->tmu[i].ram = NULL;
        }
    }

    free(vs);
}

/*************************************
 * FBI (Frame Buffer Interface) init
 *************************************/

void voodoo_init_fbi(fbi_state *f, int fbmem)
{
    if (fbmem < 1) fbmem = 1;

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

void voodoo_init_tmu(voodoo_state* vs, tmu_state* t, voodoo_reg* reg, int tmem)
{
    /* Sanity check inputs */
    assert(vs);
    assert(t);
    assert(reg);
    assert(tmem > 1);

    /* Allocate texture RAM */
    t->ram = (uint8_t*)calloc(1, tmem + 8);
    if (!t->ram) return;

    /* Align to 8-byte boundary */
    while ((uintptr_t)t->ram & 7) {
        t->ram++;
    }

    t->mask = (uint32_t)(tmem - 1);
    t->reg = reg;
    t->regdirty = true;
    t->bilinear_mask = 0xff;  /* Voodoo 2 style */

    /* mark the NCC tables dirty and configure their registers */
    t->ncc[0].dirty = t->ncc[1].dirty = true;
    t->ncc[0].reg = &t->reg[nccTable + 0];
    t->ncc[1].reg = &t->reg[nccTable + 12];

    /* create pointers to all the tables */
    t->texel[0] = vs->tmushare.rgb332;
    t->texel[1] = t->ncc[0].texel;
    t->texel[2] = vs->tmushare.alpha8;
    t->texel[3] = vs->tmushare.int8;
    t->texel[4] = vs->tmushare.ai44;
    t->texel[5] = t->palette;
    t->texel[6] = t->palettea;  /* Voodoo 2 */
    t->texel[7] = NULL;
    t->texel[8] = vs->tmushare.rgb332;
    t->texel[9] = t->ncc[0].texel;
    t->texel[10] = vs->tmushare.rgb565;
    t->texel[11] = vs->tmushare.argb1555;
    t->texel[12] = vs->tmushare.argb4444;
    t->texel[13] = vs->tmushare.int8;
    t->texel[14] = t->palette;
    t->texel[15] = NULL;
    t->lookup = t->texel[0];

    /* attach the palette to NCC table 0 */
    t->ncc[0].palette = t->palette;
    t->ncc[0].palettea = t->palettea;  /* Voodoo 2 */

    t->lodmin = 0;
    t->lodmax = 0;
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
/* VALIDATED */

static void raster_generic(voodoo_state* vs, uint32_t TMUS, uint32_t TEXMODE0,
    uint32_t TEXMODE1, void* destbase, int32_t y,
    const poly_extent* extent, stats_block *stats)
{
    const uint8_t* dither_lookup = NULL;
    const uint8_t* dither4 = NULL;
    const uint8_t* dither = NULL;

    int32_t scry = y;
    int32_t startx = extent->startx;
    int32_t stopx = extent->stopx;

    /* Quick references */
    const voodoo_reg *regs = vs->reg;
    const fbi_state *fbi = &vs->fbi;
    const tmu_state *tmu0 = &vs->tmu[0];
    const tmu_state *tmu1 = &vs->tmu[1];

    const uint32_t r_fbzColorPath = regs[fbzColorPath].u;
    const uint32_t r_fbzMode = regs[fbzMode].u;
    const uint32_t r_alphaMode = regs[alphaMode].u;
    const uint32_t r_fogMode = regs[fogMode].u;
    const uint32_t r_zaColor = regs[zaColor].u;

    uint32_t r_stipple = regs[stipple].u;

    /* determine the screen Y */
    if (FBZMODE_Y_ORIGIN(r_fbzMode)) {
        scry = (fbi->yorigin - y) & 0x3ff;
    }

    /* compute the dithering pointers */
    if (FBZMODE_ENABLE_DITHERING(r_fbzMode))
    {
        dither4 = &dither_matrix_4x4[(y & 3) * 4];
        if (FBZMODE_DITHER_TYPE(r_fbzMode) == 0)
        {
            dither = dither4;
            dither_lookup = &dither4_lookup[(y & 3) << 11];
        }
        else
        {
            dither = &dither_matrix_2x2[(y & 3) * 4];
            dither_lookup = &dither2_lookup[(y & 3) << 11];
        }
    }

    /* apply clipping */
    if (FBZMODE_ENABLE_CLIPPING(r_fbzMode))
    {
        /* Y clipping buys us the whole scanline */
        if (scry < (int32_t)((regs[clipLowYHighY].u >> 16) & 0x3ff) ||
            scry >= (int32_t)(regs[clipLowYHighY].u & 0x3ff)) {
            stats->pixels_in += stopx - startx;
            /*stats->clip_fail += stopx - startx;*/
            return;
        }

        /* X clipping */
        int32_t tempclip = (regs[clipLeftRight].u >> 16) & 0x3ff;
        if (startx < tempclip)
        {
            stats->pixels_in += tempclip - startx;
            startx = tempclip;
        }
        tempclip = regs[clipLeftRight].u & 0x3ff;
        if (stopx >= tempclip)
        {
            stats->pixels_in += stopx - tempclip;
            stopx = tempclip - 1;
        }
    }

    /* get pointers to the target buffer and depth buffer */
    uint16_t* dest = (uint16_t*)destbase + scry * fbi->rowpixels;
    uint16_t* depth = (fbi->auxoffs != (uint32_t)(~0))
        ? ((uint16_t*)(fbi->ram + fbi->auxoffs) +
            scry * fbi->rowpixels)
        : NULL;

    /* compute the starting parameters */
    const int32_t dx = startx - (fbi->ax >> 4);
    const int32_t dy = y - (fbi->ay >> 4);

    int64_t iterr = fbi->startr + dy * fbi->drdy + dx * fbi->drdx;
    int64_t iterg = fbi->startg + dy * fbi->dgdy + dx * fbi->dgdx;
    int64_t iterb = fbi->startb + dy * fbi->dbdy + dx * fbi->dbdx;
    int64_t itera = fbi->starta + dy * fbi->dady + dx * fbi->dadx;
    int32_t iterz = fbi->startz + dy * fbi->dzdy + dx * fbi->dzdx;
    int64_t iterw = fbi->startw + dy * fbi->dwdy + dx * fbi->dwdx;
    int64_t iterw0 = 0;
    int64_t iterw1 = 0;
    int64_t iters0 = 0;
    int64_t iters1 = 0;
    int64_t itert0 = 0;
    int64_t itert1 = 0;
    if (TMUS >= 1)
    {
        iterw0 = tmu0->startw + dy * tmu0->dwdy + dx * tmu0->dwdx;
        iters0 = tmu0->starts + dy * tmu0->dsdy + dx * tmu0->dsdx;
        itert0 = tmu0->startt + dy * tmu0->dtdy + dx * tmu0->dtdx;
    }
    if (TMUS >= 2)
    {
        iterw1 = tmu1->startw + dy * tmu1->dwdy + dx * tmu1->dwdx;
        iters1 = tmu1->starts + dy * tmu1->dsdy + dx * tmu1->dsdx;
        itert1 = tmu1->startt + dy * tmu1->dtdy + dx * tmu1->dtdx;
    }

    /* loop in X */
    for (int32_t x = startx; x < stopx; x++)
    {
        rgb_union iterargb = { 0 };
        rgb_union texel = { 0 };

        /* pixel pipeline part 1 handles depth testing and stippling */
        PIXEL_PIPELINE_BEGIN(vs, (*stats), x, y, r_fbzColorPath, r_fbzMode, iterz, iterw, r_zaColor, r_stipple);

        /* run the texture pipeline on TMU1 to produce a value in texel */
        /* note that they set LOD min to 8 to "disable" a TMU */
        if (TMUS >= 2 && tmu1->lodmin < (8 << 8)) {
            rgb_union c_zero = { .u = 0 };
            TEXTURE_PIPELINE(tmu1, x, dither4, TEXMODE1, c_zero,
                tmu1->lookup, tmu1->lodbasetemp, iters1, itert1, iterw1, texel);
        }

        /* run the texture pipeline on TMU0 to produce a final */
        /* result in texel */
        /* note that they set LOD min to 8 to "disable" a TMU */
        if (TMUS >= 1 && tmu0->lodmin < (8 << 8)) {
            if (!vs->send_config) {
                TEXTURE_PIPELINE(tmu0, x, dither4, TEXMODE0, texel,
                    tmu0->lookup, tmu0->lodbasetemp, iters0, itert0, iterw0, texel);
            }
            else {	/* send config data to the frame buffer */
                texel.u = vs->tmu_config;
            }
        }

        /* colorpath pipeline selects source colors and does blending */
        CLAMPED_ARGB(iterr, iterg, iterb, itera, r_fbzColorPath, iterargb);

        int32_t blendr;
        int32_t blendg;
        int32_t blendb;
        int32_t blenda;
        rgb_union c_other;
        rgb_union c_local;

        /* compute c_other */
        switch (FBZCP_CC_RGBSELECT(r_fbzColorPath))
        {
        case 0:		/* iterated RGB */
            c_other.u = iterargb.u;
            break;
        case 1:		/* texture RGB */
            c_other.u = texel.u;
            break;
        case 2:		/* color1 RGB */
            c_other.u = regs[color1].u;
            break;
        case 3:	/* reserved */
            c_other.u = 0;
            break;
        }

        /* handle chroma key */
        APPLY_CHROMAKEY(vs, (*stats), r_fbzMode, c_other);

        /* compute a_other */
        switch (FBZCP_CC_ASELECT(r_fbzColorPath))
        {
        case 0:		/* iterated alpha */
            c_other.rgb.a = iterargb.rgb.a;
            break;
        case 1:		/* texture alpha */
            c_other.rgb.a = texel.rgb.a;
            break;
        case 2:		/* color1 alpha */
            c_other.rgb.a = regs[color1].rgb.a;
            break;
        case 3:	/* reserved */
            c_other.rgb.a = 0;
            break;
        }

        /* handle alpha mask */
        APPLY_ALPHAMASK(vs, (*stats), r_fbzMode, c_other.rgb.a);

        /* handle alpha test */
        APPLY_ALPHATEST(vs, (*stats), r_alphaMode, c_other.rgb.a);

        /* compute c_local */
        if (FBZCP_CC_LOCALSELECT_OVERRIDE(r_fbzColorPath) == 0)
        {
            if (FBZCP_CC_LOCALSELECT(r_fbzColorPath) == 0) {
                /* iterated RGB */
                c_local.u = iterargb.u;
            }
            else {
                /* color0 RGB */
                c_local.u = regs[color0].u;
            }
        }
        else
        {
            if ((texel.rgb.a & 0x80) == 0) {
                /* iterated RGB */
                c_local.u = iterargb.u;
            }
            else {
                /* color0 RGB */
                c_local.u = regs[color0].u;
            }
        }

        /* compute a_local */
        switch (FBZCP_CCA_LOCALSELECT(r_fbzColorPath))
        {
        case 0:		/* iterated alpha */
            c_local.rgb.a = iterargb.rgb.a;
            break;
        case 1:		/* color0 alpha */
            c_local.rgb.a = regs[color0].rgb.a;
            break;
        case 2:		/* clamped iterated Z[27:20] */
        {
            int temp;
            CLAMPED_Z(iterz, r_fbzColorPath, temp);
            c_local.rgb.a = (uint8_t)temp;
            break;
        }
        case 3:		/* clamped iterated W[39:32] */
        {
            int temp;
            CLAMPED_W(iterw, r_fbzColorPath, temp);			/* Voodoo 2 only */
            c_local.rgb.a = (uint8_t)temp;
            break;
        }
        }

        /* select zero or c_other */
        if (FBZCP_CC_ZERO_OTHER(r_fbzColorPath) == 0)
        {
            r = c_other.rgb.r;
            g = c_other.rgb.g;
            b = c_other.rgb.b;
        }
        else {
            r = g = b = 0;
        }

        /* select zero or a_other */
        if (FBZCP_CCA_ZERO_OTHER(r_fbzColorPath) == 0) {
            a = c_other.rgb.a;
        }
        else {
            a = 0;
        }

        /* subtract c_local */
        if (FBZCP_CC_SUB_CLOCAL(r_fbzColorPath))
        {
            r -= c_local.rgb.r;
            g -= c_local.rgb.g;
            b -= c_local.rgb.b;
        }

        /* subtract a_local */
        if (FBZCP_CCA_SUB_CLOCAL(r_fbzColorPath)) {
            a -= c_local.rgb.a;
        }

        /* blend RGB */
        switch (FBZCP_CC_MSELECT(r_fbzColorPath))
        {
        default:	/* reserved */
        case 0:		/* 0 */
            blendr = blendg = blendb = 0;
            break;
        case 1:		/* c_local */
            blendr = c_local.rgb.r;
            blendg = c_local.rgb.g;
            blendb = c_local.rgb.b;
            break;
        case 2:		/* a_other */
            blendr = blendg = blendb = c_other.rgb.a;
            break;
        case 3:		/* a_local */
            blendr = blendg = blendb = c_local.rgb.a;
            break;
        case 4:		/* texture alpha */
            blendr = blendg = blendb = texel.rgb.a;
            break;
        case 5:		/* texture RGB (Voodoo 2 only) */
            blendr = texel.rgb.r;
            blendg = texel.rgb.g;
            blendb = texel.rgb.b;
            break;
        }

        /* blend alpha */
        switch (FBZCP_CCA_MSELECT(r_fbzColorPath))
        {
        default:	/* reserved */
        case 0:		/* 0 */
            blenda = 0;
            break;
        case 1:		/* a_local */
        case 3:
            blenda = c_local.rgb.a;
            break;
        case 2:		/* a_other */
            blenda = c_other.rgb.a;
            break;
        case 4:		/* texture alpha */
            blenda = texel.rgb.a;
            break;
        }

        /* reverse the RGB blend */
        if (!FBZCP_CC_REVERSE_BLEND(r_fbzColorPath))
        {
            blendr ^= 0xff;
            blendg ^= 0xff;
            blendb ^= 0xff;
        }

        /* reverse the alpha blend */
        if (!FBZCP_CCA_REVERSE_BLEND(r_fbzColorPath)) {
            blenda ^= 0xff;
        }

        /* do the blend */
        r = (r * (blendr + 1)) >> 8;
        g = (g * (blendg + 1)) >> 8;
        b = (b * (blendb + 1)) >> 8;
        a = (a * (blenda + 1)) >> 8;

        /* add clocal or alocal to RGB */
        switch (FBZCP_CC_ADD_ACLOCAL(r_fbzColorPath))
        {
        case 3:		/* reserved */
        case 0:		/* nothing */
            break;
        case 1:		/* add c_local */
            r += c_local.rgb.r;
            g += c_local.rgb.g;
            b += c_local.rgb.b;
            break;
        case 2:		/* add_alocal */
            r += c_local.rgb.a;
            g += c_local.rgb.a;
            b += c_local.rgb.a;
            break;
        }

        /* add clocal or alocal to alpha */
        if (FBZCP_CCA_ADD_ACLOCAL(r_fbzColorPath)) {
            a += c_local.rgb.a;
        }

        /* clamp */
        r = clamp_to_uint8(r);
        g = clamp_to_uint8(g);
        b = clamp_to_uint8(b);
        a = clamp_to_uint8(a);

        /* invert */
        if (FBZCP_CC_INVERT_OUTPUT(r_fbzColorPath))
        {
            r ^= 0xff;
            g ^= 0xff;
            b ^= 0xff;
        }
        if (FBZCP_CCA_INVERT_OUTPUT(r_fbzColorPath)) {
            a ^= 0xff;
        }

        /* pixel pipeline part 2 handles fog, alpha, and final output */
        PIXEL_PIPELINE_MODIFY(vs, dither, dither4, x,
            r_fbzMode, r_fbzColorPath, r_alphaMode, r_fogMode,
            iterz, iterw, iterargb);
        PIXEL_PIPELINE_FINISH(vs, dither_lookup, x, dest, depth, r_fbzMode);
        PIXEL_PIPELINE_END((*stats));

        /* update the iterated parameters */
        iterr += fbi->drdx;
        iterg += fbi->dgdx;
        iterb += fbi->dbdx;
        itera += fbi->dadx;
        iterz += fbi->dzdx;
        iterw += fbi->dwdx;
        if (TMUS >= 1)
        {
            iterw0 += tmu0->dwdx;
            iters0 += tmu0->dsdx;
            itert0 += tmu0->dtdx;
        }
        if (TMUS >= 2)
        {
            iterw1 += tmu1->dwdx;
            iters1 += tmu1->dsdx;
            itert1 += tmu1->dtdx;
        }
    }
}

static void prepare_tmu(tmu_state* t)
{
    int64_t texdx;
    int64_t texdy;
    int32_t lodbase;

    /* if the texture parameters are dirty, update them */
    if (t->regdirty)
    {
        recompute_texture_params(t);

        /* ensure that the NCC tables are up to date */
        if ((TEXMODE_FORMAT(t->reg[textureMode].u) & 7) == 1)
        {
            ncc_table* n = &t->ncc[TEXMODE_NCC_TABLE_SELECT(t->reg[textureMode].u)];
            t->texel[1] = t->texel[9] = n->texel;
            if (n->dirty) {
                ncc_table_update(n);
            }
        }
    }

    /* compute (ds^2 + dt^2) in both X and Y as 28.36 numbers */
    texdx = (t->dsdx >> 14) * (t->dsdx >> 14) + (t->dtdx >> 14) * (t->dtdx >> 14);
    texdy = (t->dsdy >> 14) * (t->dsdy >> 14) + (t->dtdy >> 14) * (t->dtdy >> 14);

    /* pick whichever is larger and shift off some high bits -> 28.20 */
    if (texdx < texdy) {
        texdx = texdy;
    }
    texdx >>= 16;

    /* use our fast reciprocal/log on this value; it expects input as a */
    /* 16.32 number, and returns the log of the reciprocal, so we have to */
    /* adjust the result: negative to get the log of the original value */
    /* plus 12 to account for the extra exponent, and divided by 2 to */
    /* get the log of the square root of texdx */
    (void)fast_reciplog(texdx, &lodbase);
    t->lodbasetemp = (-lodbase + (12 << 8)) / 2;
}

/*************************************
 *
 *  Texuture parameter computation
 *
 *************************************/

static void recompute_texture_params(tmu_state* t)
{
    int bppscale;
    uint32_t base;
    int lod;

    /* extract LOD parameters */
    t->lodmin = TEXLOD_LODMIN(t->reg[tLOD].u) << 6;
    t->lodmax = TEXLOD_LODMAX(t->reg[tLOD].u) << 6;
    t->lodbias = (int8_t)(TEXLOD_LODBIAS(t->reg[tLOD].u) << 2) << 4;

    /* determine which LODs are present */
    t->lodmask = 0x1ff;
    if (TEXLOD_LOD_TSPLIT(t->reg[tLOD].u))
    {
        if (!TEXLOD_LOD_ODD(t->reg[tLOD].u)) {
            t->lodmask = 0x155;
        }
        else {
            t->lodmask = 0x0aa;
        }
    }

    /* determine base texture width/height */
    t->wmask = t->hmask = 0xff;
    if (TEXLOD_LOD_S_IS_WIDER(t->reg[tLOD].u)) {
        t->hmask >>= TEXLOD_LOD_ASPECT(t->reg[tLOD].u);
    }
    else {
        t->wmask >>= TEXLOD_LOD_ASPECT(t->reg[tLOD].u);
    }

    /* determine the bpp of the texture */
    bppscale = TEXMODE_FORMAT(t->reg[textureMode].u) >> 3;

    /* start with the base of LOD 0 */
    if (TEXADDR_SHIFT == 0 && ((t->reg[texBaseAddr].u & 1) != 0u)) {
        /* LOG: Tiled texture */
    }
    base = (t->reg[texBaseAddr].u & TEXADDR_MASK) << TEXADDR_SHIFT;
    t->lodoffset[0] = base & t->mask;

    /* LODs 1-3 are different depending on whether we are in multitex mode */
    /* Several Voodoo 2 games leave the upper bits of TLOD == 0xff, meaning we think */
    /* they want multitex mode when they really don't -- disable for now */
    if (0) /* TEXLOD_TMULTIBASEADDR(t->reg[tLOD].u) */
    {
        base = (t->reg[texBaseAddr_1].u & TEXADDR_MASK) << TEXADDR_SHIFT;
        t->lodoffset[1] = base & t->mask;
        base = (t->reg[texBaseAddr_2].u & TEXADDR_MASK) << TEXADDR_SHIFT;
        t->lodoffset[2] = base & t->mask;
        base = (t->reg[texBaseAddr_3_8].u & TEXADDR_MASK) << TEXADDR_SHIFT;
        t->lodoffset[3] = base & t->mask;
    }
    else {
        if ((t->lodmask & (1 << 0)) != 0u) {
            base += (((t->wmask >> 0) + 1) * ((t->hmask >> 0) + 1)) << bppscale;
        }
        t->lodoffset[1] = base & t->mask;
        if ((t->lodmask & (1 << 1)) != 0u) {
            base += (((t->wmask >> 1) + 1) * ((t->hmask >> 1) + 1)) << bppscale;
        }
        t->lodoffset[2] = base & t->mask;
        if ((t->lodmask & (1 << 2)) != 0u) {
            base += (((t->wmask >> 2) + 1) * ((t->hmask >> 2) + 1)) << bppscale;
        }
        t->lodoffset[3] = base & t->mask;
    }

    /* remaining LODs make sense */
    for (lod = 4; lod <= 8; lod++)
    {
        if ((t->lodmask & (1 << (lod - 1))) != 0u)
        {
            uint32_t size = ((t->wmask >> (lod - 1)) + 1) * ((t->hmask >> (lod - 1)) + 1);
            if (size < 4) {
                size = 4;
            }
            base += size << bppscale;
        }
        t->lodoffset[lod] = base & t->mask;
    }

    /* set the NCC lookup appropriately */
    t->texel[1] = t->texel[9] = t->ncc[TEXMODE_NCC_TABLE_SELECT(t->reg[textureMode].u)].texel;

    /* pick the lookup table */
    t->lookup = t->texel[TEXMODE_FORMAT(t->reg[textureMode].u)];

    /* compute the detail parameters */
    t->detailmax = TEXDETAIL_DETAIL_MAX(t->reg[tDetail].u);
    t->detailbias = (int8_t)(TEXDETAIL_DETAIL_BIAS(t->reg[tDetail].u) << 2) << 6;
    t->detailscale = TEXDETAIL_DETAIL_SCALE(t->reg[tDetail].u);

    /* no longer dirty */
    t->regdirty = false;

    /* check for separate RGBA filtering */
    assert(!TEXDETAIL_SEPARATE_RGBA_FILTER(t->reg[tDetail].u));
    //if (TEXDETAIL_SEPARATE_RGBA_FILTER(t->reg[tDetail].u))
    //	E_Exit("Separate RGBA filters!"); // voodoo 2 feature not implemented
}

/*************************************
 * Triangle rendering
 *************************************/

static void begin_triangle(voodoo_state* vs)
{
    /* Quick references */
    const voodoo_reg *regs = vs->reg;
    fbi_state *fbi = &vs->fbi;

    setup_vertex* sv = &fbi->svert[2];

    /* extract all the data from registers */

    sv->x = regs[sVx].f;
    sv->y = regs[sVy].f;
    sv->wb = regs[sWb].f;
    sv->w0 = regs[sWtmu0].f;
    sv->s0 = regs[sS_W0].f;
    sv->t0 = regs[sT_W0].f;
    sv->w1 = regs[sWtmu1].f;
    sv->s1 = regs[sS_Wtmu1].f;
    sv->t1 = regs[sT_Wtmu1].f;
    sv->a = regs[sAlpha].f;
    sv->r = regs[sRed].f;
    sv->g = regs[sGreen].f;
    sv->b = regs[sBlue].f;

    /* spread it across all three verts and reset the count */
    fbi->svert[0] = fbi->svert[1] = fbi->svert[2];
    fbi->sverts = 1;
}

/*-------------------------------------------------
    draw_triangle - execute the 'DrawTri'
    command
-------------------------------------------------*/
static void draw_triangle(voodoo_state* vs)
{
    const voodoo_reg *regs = vs->reg;
    fbi_state *fbi = &vs->fbi;

    setup_vertex* sv = &fbi->svert[2];

    /* for strip mode, shuffle vertex 1 down to 0 */
    if ((regs[sSetupMode].u & (1 << 16)) == 0u) {
        fbi->svert[0] = fbi->svert[1];
    }

    /* copy 2 down to 1 regardless */
    fbi->svert[1] = fbi->svert[2];

    /* extract all the data from registers */
    sv->x = regs[sVx].f;
    sv->y = regs[sVy].f;
    sv->wb = regs[sWb].f;
    sv->w0 = regs[sWtmu0].f;
    sv->s0 = regs[sS_W0].f;
    sv->t0 = regs[sT_W0].f;
    sv->w1 = regs[sWtmu1].f;
    sv->s1 = regs[sS_Wtmu1].f;
    sv->t1 = regs[sT_Wtmu1].f;
    sv->a = regs[sAlpha].f;
    sv->r = regs[sRed].f;
    sv->g = regs[sGreen].f;
    sv->b = regs[sBlue].f;

    /* if we have enough verts, go ahead and draw */
    if (++fbi->sverts >= 3) {
        setup_and_draw_triangle(vs);
    }
}

void setup_and_draw_triangle(voodoo_state *vs)
{
	/* Quick references */
	const voodoo_reg *regs = vs->reg;
	fbi_state *fbi = &vs->fbi;
	tmu_state *tmu0 = &vs->tmu[0];
	tmu_state *tmu1 = &vs->tmu[1];

	/* Vertex references */
	const setup_vertex *vertex0 = &fbi->svert[0];
	const setup_vertex *vertex1 = &fbi->svert[1];
	const setup_vertex *vertex2 = &fbi->svert[2];

	/* grab the X/Ys at least */
	fbi->ax = (int16_t)(vertex0->x * 16.0f);
	fbi->ay = (int16_t)(vertex0->y * 16.0f);
	fbi->bx = (int16_t)(vertex1->x * 16.0f);
	fbi->by = (int16_t)(vertex1->y * 16.0f);
	fbi->cx = (int16_t)(vertex2->x * 16.0f);
	fbi->cy = (int16_t)(vertex2->y * 16.0f);

	/* compute the divisor */
	const float divisor = 1.0f /
	                     ((vertex0->x - vertex1->x) * (vertex0->y - vertex2->y) -
	                      (vertex0->x - vertex2->x) * (vertex0->y - vertex1->y));

	/* backface culling */
	if ((regs[sSetupMode].u & 0x20000) != 0u) {
		int culling_sign = (regs[sSetupMode].u >> 18) & 1;
		const int divisor_sign = (int)(divisor < 0);

		/* if doing strips and ping pong is enabled, apply the ping pong */
		if ((regs[sSetupMode].u & 0x90000) == 0x00000) {
			culling_sign ^= (fbi->sverts - 3) & 1;
		}

		/* if our sign matches the culling sign, we're done for */
		if (divisor_sign == culling_sign) {
			return;
		}
	}

	/* compute the dx/dy values */
	const float dx1 = vertex0->y - vertex2->y;
	const float dx2 = vertex0->y - vertex1->y;
	const float dy1 = vertex0->x - vertex1->x;
	const float dy2 = vertex0->x - vertex2->x;

	/* set up R,G,B */
	float tdiv = divisor * 4096.0f;
	if ((regs[sSetupMode].u & (1 << 0)) != 0u) {
		fbi->startr = (int32_t)(vertex0->r * 4096.0f);
		fbi->drdx   = (int32_t)(((vertex0->r - vertex1->r) * dx1 -
                                      (vertex0->r - vertex2->r) * dx2) *
                                     tdiv);
		fbi->drdy   = (int32_t)(((vertex0->r - vertex2->r) * dy1 -
                                      (vertex0->r - vertex1->r) * dy2) *
                                     tdiv);
		fbi->startg = (int32_t)(vertex0->g * 4096.0f);
		fbi->dgdx   = (int32_t)(((vertex0->g - vertex1->g) * dx1 -
                                      (vertex0->g - vertex2->g) * dx2) *
                                     tdiv);
		fbi->dgdy   = (int32_t)(((vertex0->g - vertex2->g) * dy1 -
                                      (vertex0->g - vertex1->g) * dy2) *
                                     tdiv);
		fbi->startb = (int32_t)(vertex0->b * 4096.0f);
		fbi->dbdx   = (int32_t)(((vertex0->b - vertex1->b) * dx1 -
                                      (vertex0->b - vertex2->b) * dx2) *
                                     tdiv);
		fbi->dbdy   = (int32_t)(((vertex0->b - vertex2->b) * dy1 -
                                      (vertex0->b - vertex1->b) * dy2) *
                                     tdiv);
	}

	/* set up alpha */
	if ((regs[sSetupMode].u & (1 << 1)) != 0u) {
		fbi->starta = (int32_t)(vertex0->a * 4096.0f);
		fbi->dadx   = (int32_t)(((vertex0->a - vertex1->a) * dx1 -
                                      (vertex0->a - vertex2->a) * dx2) *
                                     tdiv);
		fbi->dady   = (int32_t)(((vertex0->a - vertex2->a) * dy1 -
                                      (vertex0->a - vertex1->a) * dy2) *
                                     tdiv);
	}

	/* set up Z */
	if ((regs[sSetupMode].u & (1 << 2)) != 0u) {
		fbi->startz = (int32_t)(vertex0->z * 4096.0f);
		fbi->dzdx   = (int32_t)(((vertex0->z - vertex1->z) * dx1 -
                                      (vertex0->z - vertex2->z) * dx2) *
                                     tdiv);
		fbi->dzdy   = (int32_t)(((vertex0->z - vertex2->z) * dy1 -
                                      (vertex0->z - vertex1->z) * dy2) *
                                     tdiv);
	}

	/* set up Wb */
	tdiv = divisor * 65536.0f * 65536.0f;
	if ((regs[sSetupMode].u & (1 << 3)) != 0u) {
		fbi->startw = tmu0->startw = tmu1->startw = (int64_t)(vertex0->wb * 65536.0f *
		                                                   65536.0f);
		fbi->dwdx = tmu0->dwdx = tmu1->dwdx =
		        (int64_t)(((vertex0->wb - vertex1->wb) * dx1 -
		                   (vertex0->wb - vertex2->wb) * dx2) *
		                  tdiv);
		fbi->dwdy = tmu0->dwdy = tmu1->dwdy =
		        (int64_t)(((vertex0->wb - vertex2->wb) * dy1 -
		                   (vertex0->wb - vertex1->wb) * dy2) *
		                  tdiv);
	}

	/* set up W0 */
	if ((regs[sSetupMode].u & (1 << 4)) != 0u) {
		tmu0->startw = tmu1->startw = (int64_t)(vertex0->w0 * 65536.0f * 65536.0f);
		tmu0->dwdx = tmu1->dwdx = (int64_t)(((vertex0->w0 - vertex1->w0) * dx1 -
		                                   (vertex0->w0 - vertex2->w0) * dx2) *
		                                  tdiv);
		tmu0->dwdy = tmu1->dwdy = (int64_t)(((vertex0->w0 - vertex2->w0) * dy1 -
		                                   (vertex0->w0 - vertex1->w0) * dy2) *
		                                  tdiv);
	}

	/* set up S0,T0 */
	if ((regs[sSetupMode].u & (1 << 5)) != 0u) {
		tmu0->starts = tmu1->starts = (int64_t)(vertex0->s0 * 65536.0f * 65536.0f);
		tmu0->dsdx = tmu1->dsdx = (int64_t)(((vertex0->s0 - vertex1->s0) * dx1 -
		                                   (vertex0->s0 - vertex2->s0) * dx2) *
		                                  tdiv);
		tmu0->dsdy = tmu1->dsdy = (int64_t)(((vertex0->s0 - vertex2->s0) * dy1 -
		                                   (vertex0->s0 - vertex1->s0) * dy2) *
		                                  tdiv);
		tmu0->startt = tmu1->startt = (int64_t)(vertex0->t0 * 65536.0f * 65536.0f);
		tmu0->dtdx = tmu1->dtdx = (int64_t)(((vertex0->t0 - vertex1->t0) * dx1 -
		                                   (vertex0->t0 - vertex2->t0) * dx2) *
		                                  tdiv);
		tmu0->dtdy = tmu1->dtdy = (int64_t)(((vertex0->t0 - vertex2->t0) * dy1 -
		                                   (vertex0->t0 - vertex1->t0) * dy2) *
		                                  tdiv);
	}

	/* set up W1 */
	if ((regs[sSetupMode].u & (1 << 6)) != 0u) {
		tmu1->startw = (int64_t)(vertex0->w1 * 65536.0f * 65536.0f);
		tmu1->dwdx   = (int64_t)(((vertex0->w1 - vertex1->w1) * dx1 -
                                       (vertex0->w1 - vertex2->w1) * dx2) *
                                      tdiv);
		tmu1->dwdy   = (int64_t)(((vertex0->w1 - vertex2->w1) * dy1 -
                                       (vertex0->w1 - vertex1->w1) * dy2) *
                                      tdiv);
	}

	/* set up S1,T1 */
	if ((regs[sSetupMode].u & (1 << 7)) != 0u) {
		tmu1->starts = (int64_t)(vertex0->s1 * 65536.0f * 65536.0f);
		tmu1->dsdx   = (int64_t)(((vertex0->s1 - vertex1->s1) * dx1 -
                                       (vertex0->s1 - vertex2->s1) * dx2) *
                                      tdiv);
		tmu1->dsdy   = (int64_t)(((vertex0->s1 - vertex2->s1) * dy1 -
                                       (vertex0->s1 - vertex1->s1) * dy2) *
                                      tdiv);
		tmu1->startt = (int64_t)(vertex0->t1 * 65536.0f * 65536.0f);
		tmu1->dtdx   = (int64_t)(((vertex0->t1 - vertex1->t1) * dx1 -
                                       (vertex0->t1 - vertex2->t1) * dx2) *
                                      tdiv);
		tmu1->dtdy   = (int64_t)(((vertex0->t1 - vertex2->t1) * dy1 -
                                       (vertex0->t1 - vertex1->t1) * dy2) *
                                      tdiv);
	}

	/* draw the triangle */
	voodoo_triangle(vs);
}

void voodoo_triangle(voodoo_state* vs)
{
    /* Set global for worker threads */
    v = vs;

    /* Quick references */
    const voodoo_reg *regs = vs->reg;
    fbi_state *fbi = &vs->fbi;
    tmu_state *tmu0 = &vs->tmu[0];
    tmu_state *tmu1 = &vs->tmu[1];

    /* determine the number of TMUs involved */
    int texcount = 0;
    if (!FBIINIT3_DISABLE_TMUS(regs[fbiInit3].u) &&
        FBZCP_TEXTURE_ENABLE(regs[fbzColorPath].u)) {
        texcount = 1;
        if ((vs->chipmask & 0x04) != 0) {
            texcount = 2;
        }
    }

    /* perform subpixel adjustments */
    if (FBZCP_CCA_SUBPIXEL_ADJUST(regs[fbzColorPath].u)) {
        const int32_t dx = 8 - (fbi->ax & 15);
        const int32_t dy = 8 - (fbi->ay & 15);

        /* adjust iterated R,G,B,A and W/Z */
        fbi->startr += (dy * fbi->drdy + dx * fbi->drdx) >> 4;
        fbi->startg += (dy * fbi->dgdy + dx * fbi->dgdx) >> 4;
        fbi->startb += (dy * fbi->dbdy + dx * fbi->dbdx) >> 4;
        fbi->starta += (dy * fbi->dady + dx * fbi->dadx) >> 4;
        fbi->startw += (dy * fbi->dwdy + dx * fbi->dwdx) >> 4;
        fbi->startz += mul_32x32_shift(dy, fbi->dzdy, 4) +
            mul_32x32_shift(dx, fbi->dzdx, 4);

        /* adjust iterated W/S/T for TMU 0 */
        if (texcount >= 1)
        {
            tmu0->startw += (dy * tmu0->dwdy + dx * tmu0->dwdx) >> 4;
            tmu0->starts += (dy * tmu0->dsdy + dx * tmu0->dsdx) >> 4;
            tmu0->startt += (dy * tmu0->dtdy + dx * tmu0->dtdx) >> 4;

            /* adjust iterated W/S/T for TMU 1 */
            if (texcount >= 2)
            {
                tmu1->startw += (dy * tmu1->dwdy + dx * tmu1->dwdx) >> 4;
                tmu1->starts += (dy * tmu1->dsdy + dx * tmu1->dsdx) >> 4;
                tmu1->startt += (dy * tmu1->dtdy + dx * tmu1->dtdx) >> 4;
            }
        }
    }

    /* fill in the vertex data */
    poly_vertex vert[3];
    vert[0].x = (float)fbi->ax * (1.0f / 16.0f);
    vert[0].y = (float)fbi->ay * (1.0f / 16.0f);
    vert[1].x = (float)fbi->bx * (1.0f / 16.0f);
    vert[1].y = (float)fbi->by * (1.0f / 16.0f);
    vert[2].x = (float)fbi->cx * (1.0f / 16.0f);
    vert[2].y = (float)fbi->cy * (1.0f / 16.0f);

    /* first sort by Y */
    const poly_vertex* v1 = &vert[0];
    const poly_vertex* v2 = &vert[1];
    const poly_vertex* v3 = &vert[2];
    if (v2->y < v1->y) {
        const poly_vertex* t = v1; v1 = v2; v2 = t;
    }
    if (v3->y < v2->y)
    {
        const poly_vertex* t = v2; v2 = v3; v3 = t;
        if (v2->y < v1->y) {
            t = v1; v1 = v2; v2 = t;
        }
    }

    /* compute some integral X/Y vertex values */
    const int32_t v1y = round_coordinate(v1->y);
    const int32_t v3y = round_coordinate(v3->y);

    DEBUG_VERBOSE("voodoo_triangle: v1(%f,%f) v2(%f,%f) v3(%f,%f)\n",
                  v1->x, v1->y, v2->x, v2->y, v3->x, v3->y);
    DEBUG_VERBOSE("  v1y=%d, v3y=%d, height=%d\n", v1y, v3y, v3y - v1y);

    /* clip coordinates */
    if (v3y <= v1y) {
        DEBUG_VERBOSE("  SKIPPED: v3y <= v1y\n");
        return;
    }

    /* determine the draw buffer */
    uint16_t* drawbuf;
    switch (FBZMODE_DRAW_BUFFER(regs[fbzMode].u)) {
    case 0: /* front buffer */
        drawbuf = (uint16_t*)(fbi->ram + fbi->rgboffs[fbi->frontbuf]);
        break;

    case 1: /* back buffer */
        drawbuf = (uint16_t*)(fbi->ram + fbi->rgboffs[fbi->backbuf]);
        break;

    default: /* reserved */ return;
    }

    /* prepare TMUs if texturing enabled */
    if (texcount >= 1)
    {
        prepare_tmu(tmu0);
        if (texcount >= 2) {
            prepare_tmu(tmu1);
        }
    }

    triangle_worker *tworker = &vs->tworker;
    tworker->v1 = *v1;
    tworker->v2 = *v2;
    tworker->v3 = *v3;
    tworker->drawbuf = drawbuf;
    tworker->v1y = v1y;
    tworker->v3y = v3y;
    triangle_worker_run(tworker);

    /* update stats */
    vs->reg[fbiTrianglesOut].u++;
}

static void sum_statistics(stats_block *dest, const stats_block *src)
{
    dest->pixels_in += src->pixels_in;
    dest->pixels_out += src->pixels_out;
    dest->chroma_fail += src->chroma_fail;
    dest->zfunc_fail += src->zfunc_fail;
    dest->afunc_fail += src->afunc_fail;
}

static void triangle_worker_work(const triangle_worker *tworker,
    const int32_t work_start, const int32_t work_end)
{
    DEBUG_VERBOSE("triangle_worker_work: v1y=%d, v3y=%d, work_start=%d, work_end=%d, totalpix=%d\n",
                  tworker->v1y, tworker->v3y, work_start, work_end, tworker->totalpix);

    /* determine the number of TMUs involved */
    uint32_t tmus = 0;
    uint32_t texmode0 = 0;
    uint32_t texmode1 = 0;

    if (!FBIINIT3_DISABLE_TMUS(v->reg[fbiInit3].u) && FBZCP_TEXTURE_ENABLE(v->reg[fbzColorPath].u))
    {
        tmus = 1;
        texmode0 = v->tmu[0].reg[textureMode].u;
        if ((v->chipmask & 0x04) != 0)
        {
            tmus = 2;
            texmode1 = v->tmu[1].reg[textureMode].u;
        }
        if (tworker->disable_bilinear_filter) /* force disable bilinear filter */
        {
            texmode0 &= ~6;
            texmode1 &= ~6;
        }
    }

    /* compute the slopes for each portion of the triangle */
    const poly_vertex v1 = tworker->v1;
    const poly_vertex v2 = tworker->v2;
    const poly_vertex v3 = tworker->v3;

    const float dxdy_v1v2 = (v2.y == v1.y) ? 0.0f
        : (v2.x - v1.x) / (v2.y - v1.y);
    const float dxdy_v1v3 = (v3.y == v1.y) ? 0.0f
        : (v3.x - v1.x) / (v3.y - v1.y);
    const float dxdy_v2v3 = (v3.y == v2.y) ? 0.0f
        : (v3.x - v2.x) / (v3.y - v2.y);

    stats_block my_stats = {0};

    /* The number of workers represents the total work, while the start and
       end represent a fraction (up to 100%) of the total total. */
    assert(work_end > 0 && tworker->num_work_units >= work_end);

    /* Suppress div-by-0 false positive */
    const int num_work_units = tworker->num_work_units ? tworker->num_work_units : 1;

    const int32_t from = tworker->totalpix * work_start / num_work_units;
    const int32_t to = tworker->totalpix * work_end / num_work_units;

    DEBUG_VERBOSE("  from=%d, to=%d, num_work_units=%d\n", from, to, num_work_units);
    DEBUG_VERBOSE("  entering scanline loop: v1y=%d, v3y=%d, iterations=%d\n",
                  tworker->v1y, tworker->v3y, tworker->v3y - tworker->v1y);

    int32_t curscan, scanend, sumpix, lastsum;
    for (curscan = tworker->v1y, scanend = tworker->v3y, sumpix = 0, lastsum = 0;
        curscan != scanend && lastsum < to;
        lastsum = sumpix, curscan++) {

        const float fully = (float)(curscan) + 0.5f;

        const float startx = v1.x + (fully - v1.y) * dxdy_v1v3;

        /* compute the ending X based on which part of the triangle we're in */
        const float stopx = (fully < v2.y
            ? (v1.x + (fully - v1.y) * dxdy_v1v2)
            : (v2.x + (fully - v2.y) * dxdy_v2v3));

        /* clamp to full pixels */
        poly_extent extent;
        extent.startx = round_coordinate(startx);
        extent.stopx = round_coordinate(stopx);

        /* force start < stop */
        if (extent.startx >= extent.stopx)
        {
            if (extent.startx == extent.stopx) {
                continue;
            }
            int32_t t = extent.startx;
            extent.startx = extent.stopx;
            extent.stopx = t;
        }

        sumpix += (extent.stopx - extent.startx);

        if (sumpix <= from) {
            continue;
        }
        if (lastsum < from) {
            extent.startx += (from - lastsum);
        }
        if (sumpix > to) {
            extent.stopx -= (sumpix - to);
        }

        raster_generic(v, tmus, texmode0, texmode1, tworker->drawbuf, curscan, &extent, &my_stats);
    }
    DEBUG_VERBOSE("  scanline loop done: curscan=%d, scanend=%d, lastsum=%d\n", curscan, scanend, lastsum);
    sum_statistics(&v->thread_stats[work_start], &my_stats);
}

static int do_triangle_work(triangle_worker *tworker)
{
    /* Extra load but this should ensure we don't overflow the index,
       with the fetch_add below in case of spurious wake-ups. */
    int i = atomic_load_explicit(&tworker->work_index, memory_order_acquire);
    if (i >= tworker->num_work_units) {
        return i;
    }

    i = atomic_fetch_add_explicit(&tworker->work_index, 1, memory_order_acq_rel);
    if (i < tworker->num_work_units) {
        triangle_worker_work(tworker, i, i + 1);
        int done = atomic_fetch_add_explicit(&tworker->done_count, 1, memory_order_acq_rel) + 1;
        if (done >= tworker->num_work_units) {
            pthread_mutex_lock(&tworker->mutex);
            pthread_cond_broadcast(&tworker->cond);
            pthread_mutex_unlock(&tworker->mutex);
        }
    }

    /* fetch_add returns the previous worker index.
       We want to return the current. */
    return i + 1;
}

static void *triangle_worker_thread_func(void *arg)
{
    triangle_worker *tworker = &v->tworker;
    (void)arg;
    while (atomic_load_explicit(&tworker->threads_active, memory_order_acquire)) {
        int i = do_triangle_work(tworker);
        if (i >= tworker->num_work_units) {
            pthread_mutex_lock(&tworker->mutex);
            while (atomic_load_explicit(&tworker->work_index, memory_order_acquire) >= tworker->num_work_units &&
                   atomic_load_explicit(&tworker->threads_active, memory_order_acquire)) {
                pthread_cond_wait(&tworker->cond, &tworker->mutex);
            }
            pthread_mutex_unlock(&tworker->mutex);
        }
    }
    return NULL;
}

static void triangle_worker_shutdown(triangle_worker *tworker)
{
    if (!atomic_load_explicit(&tworker->threads_active, memory_order_acquire)) {
        return;
    }
    atomic_store_explicit(&tworker->threads_active, false, memory_order_release);
    pthread_mutex_lock(&tworker->mutex);
    atomic_store_explicit(&tworker->work_index, 0, memory_order_release);
    pthread_cond_broadcast(&tworker->cond);
    pthread_mutex_unlock(&tworker->mutex);

    for (int i = 0; i < tworker->num_threads; i++) {
        pthread_join(tworker->threads[i], NULL);
    }
}

static void triangle_worker_run(triangle_worker *tworker)
{
    DEBUG_VERBOSE("triangle_worker_run: v1y=%d, v3y=%d, num_threads=%d, num_work_units=%d\n",
                  tworker->v1y, tworker->v3y, tworker->num_threads, tworker->num_work_units);

    if (!tworker->num_threads) {
        /* do not use threaded calculation */
        DEBUG_VERBOSE("  non-threaded path, calling triangle_worker_work\n");
        tworker->totalpix = 0xFFFFFFF;
        triangle_worker_work(tworker, 0, tworker->num_work_units);
        DEBUG_VERBOSE("  triangle_worker_work returned\n");
        return;
    }

    /* compute the slopes for each portion of the triangle */
    const poly_vertex v1 = tworker->v1;
    const poly_vertex v2 = tworker->v2;
    const poly_vertex v3 = tworker->v3;

    const float dxdy_v1v2 = (v2.y == v1.y) ? 0.0f
        : (v2.x - v1.x) / (v2.y - v1.y);
    const float dxdy_v1v3 = (v3.y == v1.y) ? 0.0f
        : (v3.x - v1.x) / (v3.y - v1.y);
    const float dxdy_v2v3 = (v3.y == v2.y) ? 0.0f
        : (v3.x - v2.x) / (v3.y - v2.y);

    int32_t pixsum = 0;
    int32_t curscan, scanend;
    for (curscan = tworker->v1y, scanend = tworker->v3y; curscan != scanend; curscan++)
    {
        const float fully = (float)(curscan) + 0.5f;
        const float startx = v1.x + (fully - v1.y) * dxdy_v1v3;

        /* compute the ending X based on which part of the triangle we're in */
        const float stopx = (fully < v2.y
            ? (v1.x + (fully - v1.y) * dxdy_v1v2)
            : (v2.x + (fully - v2.y) * dxdy_v2v3));

        /* clamp to full pixels */
        const int32_t istartx = round_coordinate(startx);
        const int32_t istopx = round_coordinate(stopx);

        /* force start < stop */
        pixsum += (istartx > istopx ? istartx - istopx : istopx - istartx);
    }
    tworker->totalpix = pixsum;

    /* Don't wake up threads for just a few pixels */
    if (tworker->totalpix <= 200)
    {
        triangle_worker_work(tworker, 0, tworker->num_work_units);
        return;
    }

    /* Spin up threads if not already active */
    if (!atomic_load_explicit(&tworker->threads_active, memory_order_acquire))
    {
        atomic_store_explicit(&tworker->threads_active, true, memory_order_release);

        for (int i = 0; i < tworker->num_threads; i++) {
            pthread_create(&tworker->threads[i], NULL, triangle_worker_thread_func, tworker);
        }
    }

    atomic_store_explicit(&tworker->done_count, 0, memory_order_release);

    /* Resetting this index triggers the worker threads to start working */
    pthread_mutex_lock(&tworker->mutex);
    atomic_store_explicit(&tworker->work_index, 0, memory_order_release);
    pthread_cond_broadcast(&tworker->cond);
    pthread_mutex_unlock(&tworker->mutex);

    /* Main thread also does the same work as the worker threads */
    while (do_triangle_work(tworker) < tworker->num_work_units);

    /* Wait until all work has been completed by the worker threads */
    pthread_mutex_lock(&tworker->mutex);
    while (atomic_load_explicit(&tworker->done_count, memory_order_acquire) < tworker->num_work_units) {
        pthread_cond_wait(&tworker->cond, &tworker->mutex);
    }
    pthread_mutex_unlock(&tworker->mutex);
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

static void voodoo_swap_buffers(voodoo_state* vs)
{
    /* keep a history of swap intervals */
    voodoo_reg *regs = vs->reg;

    regs[fbiSwapHistory].u = (regs[fbiSwapHistory].u << 4);

    /* rotate the buffers */
    fbi_state *fbi = &vs->fbi;

    /* Always swap for Voodoo 1 style, or if vblank_dont_swap is false */
    if (!fbi->vblank_dont_swap) {
        if (fbi->rgboffs[2] == (uint32_t)(~0)) {
            fbi->frontbuf = (uint8_t)(1 - fbi->frontbuf);
            fbi->backbuf = (uint8_t)(1 - fbi->frontbuf);
        }
        else {
            fbi->frontbuf = (uint8_t)((fbi->frontbuf + 1) % 3);
            fbi->backbuf = (uint8_t)((fbi->frontbuf + 1) % 3);
        }
    }
}


void voodoo_swapbuffer(voodoo_state* vs, uint32_t data)
{
    /* set the don't swap value for Voodoo 2 */
    vs->fbi.vblank_dont_swap = ((data >> 9) & 1) > 0;

    voodoo_swap_buffers(vs);
}

/*************************************
 *
 *  Stubs for DOSBox-specific functions
 *
 *************************************/

static void recompute_video_memory(voodoo_state *vs)
{
    /* We handle memory layout in grSstWinOpen - nothing to do here */
    (void)vs;
}

static void Voodoo_UpdateScreenStart(void)
{
    /* We handle display updates via display_present - nothing to do here */
}

static void soft_reset(voodoo_state *vs)
{
    /* Reset triangle setup state */
    vs->fbi.sverts = 0;
}

/*************************************
 *
 *  Voodoo register writes
 *
 *************************************/
void voodoo_reg_write(voodoo_state *v, uint32_t offset, uint32_t data)
{
    uint8_t chips = (uint8_t)((offset >> 8) & 0xf);

    int64_t data64;
    (void)data64; /* suppress unused warning until needed */

    if (chips == 0) {
        chips = 0xf;
    }
    chips &= v->chipmask;

    /* the first 64 registers can be aliased differently */
    const int is_aliased = (offset & 0x800c0) == 0x80000 && v->alt_regmap;

    const uint8_t regnum = is_aliased ? register_alias_map[offset & 0x3f]
        : (uint8_t)(offset & 0xff);

    /* first make sure this register is writable */
    if (v->regaccess && (v->regaccess[regnum] & REGISTER_WRITE) == 0)
    {
        return;
    }

    /* switch off the register */
    switch (regnum)
    {
        /* Vertex data is 12.4 formatted fixed point */
    case fvertexAx:
        data = float_to_int32(data, 4);
        /* fall through */
    case vertexAx:
        if ((chips & 1) != 0) {
            v->fbi.ax = (int16_t)(data & 0xffff);
        }
        break;

    case fvertexAy:
        data = float_to_int32(data, 4);
        /* fall through */
    case vertexAy:
        if ((chips & 1) != 0) {
            v->fbi.ay = (int16_t)(data & 0xffff);
        }
        break;

    case fvertexBx:
        data = float_to_int32(data, 4);
        /* fall through */
    case vertexBx:
        if ((chips & 1) != 0) {
            v->fbi.bx = (int16_t)(data & 0xffff);
        }
        break;

    case fvertexBy:
        data = float_to_int32(data, 4);
        /* fall through */
    case vertexBy:
        if ((chips & 1) != 0) {
            v->fbi.by = (int16_t)(data & 0xffff);
        }
        break;

    case fvertexCx:
        data = float_to_int32(data, 4);
        /* fall through */
    case vertexCx:
        if ((chips & 1) != 0) {
            v->fbi.cx = (int16_t)(data & 0xffff);
        }
        break;

    case fvertexCy:
        data = float_to_int32(data, 4);
        /* fall through */
    case vertexCy:
        if ((chips & 1) != 0) {
            v->fbi.cy = (int16_t)(data & 0xffff);
        }
        break;

        /* RGB data is 12.12 formatted fixed point */
    case fstartR:
        data = float_to_int32(data, 12);
        /* fall through */
    case startR:
        if ((chips & 1) != 0) {
            v->fbi.startr = (int32_t)(data << 8) >> 8;
        }
        break;

    case fstartG:
        data = float_to_int32(data, 12);
        /* fall through */
    case startG:
        if ((chips & 1) != 0) {
            v->fbi.startg = (int32_t)(data << 8) >> 8;
        }
        break;

    case fstartB:
        data = float_to_int32(data, 12);
        /* fall through */
    case startB:
        if ((chips & 1) != 0) {
            v->fbi.startb = (int32_t)(data << 8) >> 8;
        }
        break;

    case fstartA:
        data = float_to_int32(data, 12);
        /* fall through */
    case startA:
        if ((chips & 1) != 0) {
            v->fbi.starta = (int32_t)(data << 8) >> 8;
        }
        break;

    case fdRdX:
        data = float_to_int32(data, 12);
        /* fall through */
    case dRdX:
        if ((chips & 1) != 0) {
            v->fbi.drdx = (int32_t)(data << 8) >> 8;
        }
        break;

    case fdGdX:
        data = float_to_int32(data, 12);
        /* fall through */
    case dGdX:
        if ((chips & 1) != 0) {
            v->fbi.dgdx = (int32_t)(data << 8) >> 8;
        }
        break;

    case fdBdX:
        data = float_to_int32(data, 12);
        /* fall through */
    case dBdX:
        if ((chips & 1) != 0) {
            v->fbi.dbdx = (int32_t)(data << 8) >> 8;
        }
        break;

    case fdAdX:
        data = float_to_int32(data, 12);
        /* fall through */
    case dAdX:
        if ((chips & 1) != 0) {
            v->fbi.dadx = (int32_t)(data << 8) >> 8;
        }
        break;

    case fdRdY:
        data = float_to_int32(data, 12);
        /* fall through */
    case dRdY:
        if ((chips & 1) != 0) {
            v->fbi.drdy = (int32_t)(data << 8) >> 8;
        }
        break;

    case fdGdY:
        data = float_to_int32(data, 12);
        /* fall through */
    case dGdY:
        if ((chips & 1) != 0) {
            v->fbi.dgdy = (int32_t)(data << 8) >> 8;
        }
        break;

    case fdBdY:
        data = float_to_int32(data, 12);
        /* fall through */
    case dBdY:
        if ((chips & 1) != 0) {
            v->fbi.dbdy = (int32_t)(data << 8) >> 8;
        }
        break;

    case fdAdY:
        data = float_to_int32(data, 12);
        /* fall through */
    case dAdY:
        if ((chips & 1) != 0) {
            v->fbi.dady = (int32_t)(data << 8) >> 8;
        }
        break;

        /* Z data is 20.12 formatted fixed point */
    case fstartZ:
        data = float_to_int32(data, 12);
        /* fall through */
    case startZ:
        if ((chips & 1) != 0) {
            v->fbi.startz = (int32_t)data;
        }
        break;

    case fdZdX:
        data = float_to_int32(data, 12);
        /* fall through */
    case dZdX:
        if ((chips & 1) != 0) {
            v->fbi.dzdx = (int32_t)data;
        }
        break;

    case fdZdY:
        data = float_to_int32(data, 12);
        /* fall through */
    case dZdY:
        if ((chips & 1) != 0) {
            v->fbi.dzdy = (int32_t)data;
        }
        break;

        /* S,T data is 14.18 formatted fixed point, converted to 16.32 internally */
    case fstartS:
        data64 = float_to_int64(data, 32);
        if ((chips & 2) != 0) {
            v->tmu[0].starts = data64;
        }
        if ((chips & 4) != 0) {
            v->tmu[1].starts = data64;
        }
        break;
    case startS:
        if ((chips & 2) != 0) {
            v->tmu[0].starts = (int64_t)(int32_t)data << 14;
        }
        if ((chips & 4) != 0) {
            v->tmu[1].starts = (int64_t)(int32_t)data << 14;
        }
        break;

    case fstartT:
        data64 = float_to_int64(data, 32);
        if ((chips & 2) != 0) {
            v->tmu[0].startt = data64;
        }
        if ((chips & 4) != 0) {
            v->tmu[1].startt = data64;
        }
        break;
    case startT:
        if ((chips & 2) != 0) {
            v->tmu[0].startt = (int64_t)(int32_t)data << 14;
        }
        if ((chips & 4) != 0) {
            v->tmu[1].startt = (int64_t)(int32_t)data << 14;
        }
        break;

    case fdSdX:
        data64 = float_to_int64(data, 32);
        if ((chips & 2) != 0) {
            v->tmu[0].dsdx = data64;
        }
        if ((chips & 4) != 0) {
            v->tmu[1].dsdx = data64;
        }
        break;
    case dSdX:
        if ((chips & 2) != 0) {
            v->tmu[0].dsdx = (int64_t)(int32_t)data << 14;
        }
        if ((chips & 4) != 0) {
            v->tmu[1].dsdx = (int64_t)(int32_t)data << 14;
        }
        break;

    case fdTdX:
        data64 = float_to_int64(data, 32);
        if ((chips & 2) != 0) {
            v->tmu[0].dtdx = data64;
        }
        if ((chips & 4) != 0) {
            v->tmu[1].dtdx = data64;
        }
        break;
    case dTdX:
        if ((chips & 2) != 0) {
            v->tmu[0].dtdx = (int64_t)(int32_t)data << 14;
        }
        if ((chips & 4) != 0) {
            v->tmu[1].dtdx = (int64_t)(int32_t)data << 14;
        }
        break;

    case fdSdY:
        data64 = float_to_int64(data, 32);
        if ((chips & 2) != 0) {
            v->tmu[0].dsdy = data64;
        }
        if ((chips & 4) != 0) {
            v->tmu[1].dsdy = data64;
        }
        break;
    case dSdY:
        if ((chips & 2) != 0) {
            v->tmu[0].dsdy = (int64_t)(int32_t)data << 14;
        }
        if ((chips & 4) != 0) {
            v->tmu[1].dsdy = (int64_t)(int32_t)data << 14;
        }
        break;

    case fdTdY:
        data64 = float_to_int64(data, 32);
        if ((chips & 2) != 0) {
            v->tmu[0].dtdy = data64;
        }
        if ((chips & 4) != 0) {
            v->tmu[1].dtdy = data64;
        }
        break;
    case dTdY:
        if ((chips & 2) != 0) {
            v->tmu[0].dtdy = (int64_t)(int32_t)data << 14;
        }
        if ((chips & 4) != 0) {
            v->tmu[1].dtdy = (int64_t)(int32_t)data << 14;
        }
        break;

        /* W data is 2.30 formatted fixed point, converted to 16.32 internally */
    case fstartW:
        data64 = float_to_int64(data, 32);
        if ((chips & 1) != 0) {
            v->fbi.startw = data64;
        }
        if ((chips & 2) != 0) {
            v->tmu[0].startw = data64;
        }
        if ((chips & 4) != 0) {
            v->tmu[1].startw = data64;
        }
        break;
    case startW:
        if ((chips & 1) != 0) {
            v->fbi.startw = (int64_t)(int32_t)data << 2;
        }
        if ((chips & 2) != 0) {
            v->tmu[0].startw = (int64_t)(int32_t)data << 2;
        }
        if ((chips & 4) != 0) {
            v->tmu[1].startw = (int64_t)(int32_t)data << 2;
        }
        break;

    case fdWdX:
        data64 = float_to_int64(data, 32);
        if ((chips & 1) != 0) {
            v->fbi.dwdx = data64;
        }
        if ((chips & 2) != 0) {
            v->tmu[0].dwdx = data64;
        }
        if ((chips & 4) != 0) {
            v->tmu[1].dwdx = data64;
        }
        break;
    case dWdX:
        if ((chips & 1) != 0) {
            v->fbi.dwdx = (int64_t)(int32_t)data << 2;
        }
        if ((chips & 2) != 0) {
            v->tmu[0].dwdx = (int64_t)(int32_t)data << 2;
        }
        if ((chips & 4) != 0) {
            v->tmu[1].dwdx = (int64_t)(int32_t)data << 2;
        }
        break;

    case fdWdY:
        data64 = float_to_int64(data, 32);
        if ((chips & 1) != 0) {
            v->fbi.dwdy = data64;
        }
        if ((chips & 2) != 0) {
            v->tmu[0].dwdy = data64;
        }
        if ((chips & 4) != 0) {
            v->tmu[1].dwdy = data64;
        }
        break;
    case dWdY:
        if ((chips & 1) != 0) {
            v->fbi.dwdy = (int64_t)(int32_t)data << 2;
        }
        if ((chips & 2) != 0) {
            v->tmu[0].dwdy = (int64_t)(int32_t)data << 2;
        }
        if ((chips & 4) != 0) {
            v->tmu[1].dwdy = (int64_t)(int32_t)data << 2;
        }
        break;

        /* setup bits */
    case sARGB:
        if ((chips & 1) != 0)
        {
            v->reg[sAlpha].f = (float)RGB_ALPHA(data);
            v->reg[sRed].f = (float)RGB_RED(data);
            v->reg[sGreen].f = (float)RGB_GREEN(data);
            v->reg[sBlue].f = (float)RGB_BLUE(data);
        }
        break;

        /* mask off invalid bits for different cards */
    case fbzColorPath:
        /* Voodoo 2: no masking needed */
        if ((chips & 1) != 0) {
            v->reg[fbzColorPath].u = data;
        }
        break;

    case fbzMode:
        /* Voodoo 2: no masking needed */
        if ((chips & 1) != 0) {
#ifdef C_ENABLE_VOODOO_OPENGL
            if (v->ogl && v->active && (FBZMODE_Y_ORIGIN(v->reg[fbzMode].u) != FBZMODE_Y_ORIGIN(data))) {
                v->reg[fbzMode].u = data;
                voodoo_ogl_set_window(v);
            }
            else
#endif
            {
                v->reg[fbzMode].u = data;
            }
        }
        break;

    case fogMode:
        /* Voodoo 2: no masking needed */
        if ((chips & 1) != 0) {
            v->reg[fogMode].u = data;
        }
        break;

        /* triangle drawing */
    case triangleCMD:
    case ftriangleCMD:
        voodoo_triangle(v);
        break;

    case sBeginTriCMD:
        begin_triangle(v);
        break;

    case sDrawTriCMD:
        draw_triangle(v);
        break;

        /* other commands */
    case nopCMD:
        if ((data & 2) != 0u) {
            v->reg[fbiTrianglesOut].u = 0;
        }
        break;

    case fastfillCMD:
        voodoo_fastfill(v);
        break;

    case swapbufferCMD:
        voodoo_swapbuffer(v, data);
        break;

        /* gamma table access -- Voodoo/Voodoo2 only */
    case clutData:
        /* clutData - not implemented */
        break;

        /* external DAC access -- Voodoo/Voodoo2 only */
    case dacData:
        if ((chips & 1) != 0)
        {
            if ((data & 0x800) == 0u) {
                dacdata_w(&v->dac, (data >> 8) & 7, data & 0xff);
            }
            else {
                dacdata_r(&v->dac, (data >> 8) & 7);
            }
        }
        break;

        /* vertical sync rate -- Voodoo/Voodoo2 only */
    case hSync:
    case vSync:
    case backPorch:
    case videoDimensions:
        if ((chips & 1) != 0)
        {
            v->reg[regnum].u = data;
            if (v->reg[hSync].u != 0 && v->reg[vSync].u != 0 && v->reg[videoDimensions].u != 0)
            {
                const int hvis = v->reg[videoDimensions].u & 0x3ff;
                const int vvis = (v->reg[videoDimensions].u >> 16) & 0x3ff;

#ifdef C_ENABLE_VOODOO_DEBUG
                int htotal = ((v->reg[hSync].u >> 16) & 0x3ff) + 1 + (v->reg[hSync].u & 0xff) + 1;
                int vtotal = ((v->reg[vSync].u >> 16) & 0xfff) + (v->reg[vSync].u & 0xfff);

                int hbp = (v->reg[backPorch].u & 0xff) + 2;
                int vbp = (v->reg[backPorch].u >> 16) & 0xff;

                //attoseconds_t refresh = video_screen_get_frame_period(v->screen).attoseconds;
                attoseconds_t refresh = 0;
                attoseconds_t stdperiod, medperiod, vgaperiod;
                attoseconds_t stddiff, meddiff, vgadiff;

                /* compute the new period for standard res, medium res, and VGA res */
                stdperiod = HZ_TO_ATTOSECONDS(15750) * vtotal;
                medperiod = HZ_TO_ATTOSECONDS(25000) * vtotal;
                vgaperiod = HZ_TO_ATTOSECONDS(31500) * vtotal;

                /* compute a diff against the current refresh period */
                stddiff = stdperiod - refresh;
                if (stddiff < 0) stddiff = -stddiff;
                meddiff = medperiod - refresh;
                if (meddiff < 0) meddiff = -meddiff;
                vgadiff = vgaperiod - refresh;
                if (vgadiff < 0) vgadiff = -vgadiff;

                LOG(LOG_VOODOO, LOG_WARN)("hSync=%08X  vSync=%08X  backPorch=%08X  videoDimensions=%08X\n",
                    v->reg[hSync].u, v->reg[vSync].u, v->reg[backPorch].u, v->reg[videoDimensions].u);

                rectangle visarea;

                /* create a new visarea */
                visarea.min_x = hbp;
                visarea.max_x = hbp + hvis - 1;
                visarea.min_y = vbp;
                visarea.max_y = vbp + vvis - 1;

                /* keep within bounds */
                if (visarea.max_x > htotal - 1) visarea.max_x = htotal - 1;
                if (visarea.max_y > vtotal - 1) visarea.max_y = vtotal - 1;
                LOG(LOG_VOODOO, LOG_WARN)("Horiz: %d-%d (%d total)  Vert: %d-%d (%d total) -- ", visarea.min_x, visarea.max_x, htotal, visarea.min_y, visarea.max_y, vtotal);

                /* configure the screen based on which one matches the closest */
                if (stddiff < meddiff && stddiff < vgadiff)
                {
                    //video_screen_configure(v->screen, htotal, vtotal, &visarea, stdperiod);
                    LOG(LOG_VOODOO, LOG_WARN)("Standard resolution, %f Hz\n", ATTOSECONDS_TO_HZ(stdperiod));
                }
                else if (meddiff < vgadiff)
                {
                    //video_screen_configure(v->screen, htotal, vtotal, &visarea, medperiod);
                    LOG(LOG_VOODOO, LOG_WARN)("Medium resolution, %f Hz\n", ATTOSECONDS_TO_HZ(medperiod));
                }
                else
                {
                    //video_screen_configure(v->screen, htotal, vtotal, &visarea, vgaperiod);
                    LOG(LOG_VOODOO, LOG_WARN)("VGA resolution, %f Hz\n", ATTOSECONDS_TO_HZ(vgaperiod));
                }
#endif

                /* configure the new framebuffer info */
                const uint32_t new_width = (hvis + 1) & ~1;
                const uint32_t new_height = (vvis + 1) & ~1;

                if ((v->fbi.width != new_width) || (v->fbi.height != new_height)) {
                    v->fbi.width = new_width;
                    v->fbi.height = new_height;
#ifdef C_ENABLE_VOODOO_OPENGL
                    v->ogl_dimchange = true;
#endif
                }
                //v->fbi.xoffs = hbp;
                //v->fbi.yoffs = vbp;
                //v->fbi.vsyncscan = (v->reg[vSync].u >> 16) & 0xfff;

                /* recompute the time of VBLANK */
                //adjust_vblank_timer(v);

                /* if changing dimensions, update video memory layout */
                if (regnum == videoDimensions) {
                    recompute_video_memory(v);
                }

                Voodoo_UpdateScreenStart();
            }
        }
        break;

        /* fbiInit0 can only be written if initEnable says we can -- Voodoo/Voodoo2 only */
    case fbiInit0:
        if (((chips & 1) != 0) && INITEN_ENABLE_HW_INIT(v->pci.init_enable))
        {
            const bool new_output_on = FBIINIT0_VGA_PASSTHRU(data);

            if (v->output_on != new_output_on) {
                v->output_on = new_output_on;
                Voodoo_UpdateScreenStart();
            }

            v->reg[fbiInit0].u = data;
            if (FBIINIT0_GRAPHICS_RESET(data)) {
                soft_reset(v);
            }
            recompute_video_memory(v);
        }
        break;

        /* fbiInit5-7 are Voodoo 2-only; ignore them on anything else */
    case fbiInit5:
    case fbiInit6:
        if (v->vtype < VOODOO_2) {
            break;
        }
        /* fall through */

        /* fbiInitX can only be written if initEnable says we can -- Voodoo/Voodoo2 only */
        /* most of these affect memory layout, so always recompute that when done */
    case fbiInit1:
    case fbiInit2:
    case fbiInit4:
        if (((chips & 1) != 0) && INITEN_ENABLE_HW_INIT(v->pci.init_enable))
        {
            v->reg[regnum].u = data;
            recompute_video_memory(v);
        }
        break;

    case fbiInit3:
        if (((chips & 1) != 0) && INITEN_ENABLE_HW_INIT(v->pci.init_enable))
        {
            v->reg[regnum].u = data;
            v->alt_regmap = (FBIINIT3_TRI_REGISTER_REMAP(data) > 0);
            v->fbi.yorigin = FBIINIT3_YORIGIN_SUBTRACT(v->reg[fbiInit3].u);
            recompute_video_memory(v);
        }
        break;

        /* nccTable entries are processed and expanded immediately */
    case nccTable + 0:
    case nccTable + 1:
    case nccTable + 2:
    case nccTable + 3:
    case nccTable + 4:
    case nccTable + 5:
    case nccTable + 6:
    case nccTable + 7:
    case nccTable + 8:
    case nccTable + 9:
    case nccTable + 10:
    case nccTable + 11:
        if ((chips & 2) != 0) {
            ncc_table_write(&v->tmu[0].ncc[0], regnum - nccTable, data);
        }
        if ((chips & 4) != 0) {
            ncc_table_write(&v->tmu[1].ncc[0], regnum - nccTable, data);
        }
        break;

    case nccTable + 12:
    case nccTable + 13:
    case nccTable + 14:
    case nccTable + 15:
    case nccTable + 16:
    case nccTable + 17:
    case nccTable + 18:
    case nccTable + 19:
    case nccTable + 20:
    case nccTable + 21:
    case nccTable + 22:
    case nccTable + 23:
        if ((chips & 2) != 0) {
            ncc_table_write(&v->tmu[0].ncc[1], regnum - (nccTable + 12), data);
        }
        if ((chips & 4) != 0) {
            ncc_table_write(&v->tmu[1].ncc[1], regnum - (nccTable + 12), data);
        }
        break;

        /* fogTable entries are processed and expanded immediately */
    case fogTable + 0:
    case fogTable + 1:
    case fogTable + 2:
    case fogTable + 3:
    case fogTable + 4:
    case fogTable + 5:
    case fogTable + 6:
    case fogTable + 7:
    case fogTable + 8:
    case fogTable + 9:
    case fogTable + 10:
    case fogTable + 11:
    case fogTable + 12:
    case fogTable + 13:
    case fogTable + 14:
    case fogTable + 15:
    case fogTable + 16:
    case fogTable + 17:
    case fogTable + 18:
    case fogTable + 19:
    case fogTable + 20:
    case fogTable + 21:
    case fogTable + 22:
    case fogTable + 23:
    case fogTable + 24:
    case fogTable + 25:
    case fogTable + 26:
    case fogTable + 27:
    case fogTable + 28:
    case fogTable + 29:
    case fogTable + 30:
    case fogTable + 31:
        if ((chips & 1) != 0)
        {
            const int base = 2 * (regnum - fogTable);

            v->fbi.fogdelta[base + 0] = (uint8_t)(data & 0xff);
            v->fbi.fogblend[base + 0] = (uint8_t)((data >> 8) & 0xff);
            v->fbi.fogdelta[base + 1] = (uint8_t)((data >> 16) & 0xff);
            v->fbi.fogblend[base + 1] = (uint8_t)((data >> 24) & 0xff);
        }
        break;

        /* texture modifications cause us to recompute everything */
    case textureMode:
    case tLOD:
    case tDetail:
    case texBaseAddr:
    case texBaseAddr_1:
    case texBaseAddr_2:
    case texBaseAddr_3_8:
        if ((chips & 2) != 0)
        {
            v->tmu[0].reg[regnum].u = data;
            v->tmu[0].regdirty = true;
        }
        if ((chips & 4) != 0)
        {
            v->tmu[1].reg[regnum].u = data;
            v->tmu[1].regdirty = true;
        }
        break;

    case trexInit1:
        /* send tmu config data to the frame buffer */
        v->send_config = (TREXINIT_SEND_TMU_CONFIG(data) > 0);
        goto default_case;
        break;

    case clipLowYHighY:
    case clipLeftRight:
        if ((chips & 1) != 0) {
            v->reg[0x000 + regnum].u = data;
        }
#ifdef C_ENABLE_VOODOO_OPENGL
        if (v->ogl) {
            voodoo_ogl_clip_window(v);
        }
#endif
        break;

        /* these registers are referenced in the renderer; we must wait for pending work before changing */
    case chromaRange:
    case chromaKey:
    case alphaMode:
    case fogColor:
    case stipple:
    case zaColor:
    case color1:
    case color0:
        /* fall through to default implementation */

    /* by default, just feed the data to the chips */
    default:
    default_case:
        if ((chips & 1) != 0) {
            v->reg[FBI_REG_BASE + regnum].u = data;
        }
        if ((chips & 2) != 0) {
            v->reg[TMU0_REG_BASE + regnum].u = data;
        }
        if ((chips & 4) != 0) {
            v->reg[TMU1_REG_BASE + regnum].u = data;
        }
        if ((chips & 8) != 0) {
            v->reg[TMU2_REG_BASE + regnum].u = data;
        }
        break;
    }
}