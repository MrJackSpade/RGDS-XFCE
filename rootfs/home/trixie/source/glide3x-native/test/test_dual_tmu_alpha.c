/*
 * test_dual_tmu_alpha.c - Test dual TMU alpha blending (Diablo 2 sprite scenario)
 *
 * This test reproduces the "black boxes behind sprites" issue seen in Diablo 2.
 *
 * Setup:
 * - TMU1: Background texture (checkerboard pattern)
 * - TMU0: Foreground sprite with alpha (white "T" on transparent background)
 *
 * The TMU combine should blend the sprite over the background using the
 * sprite's alpha channel:
 *   output = sprite * sprite_alpha + background * (1 - sprite_alpha)
 *
 * If alpha blending between TMUs is broken, we'll see:
 * - Black boxes where the sprite is (background not showing through)
 * - Or the sprite not appearing at all
 */

#include <windows.h>
#include <stdio.h>
#include <string.h>
#include "../src/glide3x.h"

#define LOG(fmt, ...) do { \
    char buf[512]; \
    snprintf(buf, sizeof(buf), fmt "\n", ##__VA_ARGS__); \
    OutputDebugStringA(buf); \
    printf("%s", buf); \
} while(0)

/* Create checkerboard RGB565 texture */
static void create_checker_rgb565(FxU16* data, int w, int h)
{
    FxU16 magenta = (0x1F << 11) | (0x00 << 5) | 0x1F;  /* Magenta */
    FxU16 cyan = (0x00 << 11) | (0x3F << 5) | 0x1F;     /* Cyan */

    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            data[y * w + x] = (((x >> 2) + (y >> 2)) & 1) ? magenta : cyan;
        }
    }
}

/* Create a "T" shaped sprite with ARGB1555 (1-bit alpha)
 * White letter, transparent background
 */
static void create_sprite_argb1555(FxU16* data, int w, int h)
{
    FxU16 opaque_white = (1 << 15) | (0x1F << 10) | (0x1F << 5) | 0x1F;  /* A=1, RGB=white */
    FxU16 transparent = 0;  /* A=0 */

    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            int is_letter = 0;

            /* "T" shape */
            if (y >= 2 && y <= 4 && x >= 2 && x <= 13) is_letter = 1;  /* Top bar */
            if (y >= 4 && y <= 13 && x >= 6 && x <= 9) is_letter = 1;  /* Stem */

            data[y * w + x] = is_letter ? opaque_white : transparent;
        }
    }
}

/* Create a sprite with ARGB4444 (4-bit alpha) for more detailed testing
 * Green center with varying alpha edges
 */
static void create_sprite_argb4444(FxU16* data, int w, int h)
{
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            /* Distance from center */
            int dx = x - w/2;
            int dy = y - h/2;
            int dist = (dx*dx + dy*dy);

            uint16_t a, r, g, b;

            if (dist < 16) {
                /* Center: fully opaque green */
                a = 0xF; r = 0x0; g = 0xF; b = 0x0;
            } else if (dist < 36) {
                /* Middle ring: 50% alpha green */
                a = 0x8; r = 0x0; g = 0xF; b = 0x0;
            } else if (dist < 64) {
                /* Outer ring: 25% alpha green */
                a = 0x4; r = 0x0; g = 0xF; b = 0x0;
            } else {
                /* Outside: fully transparent */
                a = 0x0; r = 0x0; g = 0x0; b = 0x0;
            }

            data[y * w + x] = (FxU16)((a << 12) | (r << 8) | (g << 4) | b);
        }
    }
}

/* Decode RGB565 pixel */
static void decode_rgb565(uint16_t pixel, uint8_t* r, uint8_t* g, uint8_t* b)
{
    *r = (uint8_t)(((pixel >> 11) & 0x1F) << 3);
    *g = (uint8_t)(((pixel >> 5) & 0x3F) << 2);
    *b = (uint8_t)((pixel & 0x1F) << 3);
}

/* Draw a textured quad with both TMU coordinates */
static void draw_dual_tmu_quad(float x1, float y1, float x2, float y2)
{
    GrVertex v1, v2, v3, v4;

    memset(&v1, 0, sizeof(v1));
    memset(&v2, 0, sizeof(v2));
    memset(&v3, 0, sizeof(v3));
    memset(&v4, 0, sizeof(v4));

    v1.x = x1; v1.y = y1; v1.oow = 1.0f;
    v1.sow = 0.0f; v1.tow = 0.0f; v1.sow1 = 0.0f; v1.tow1 = 0.0f;

    v2.x = x2; v2.y = y1; v2.oow = 1.0f;
    v2.sow = 255.0f; v2.tow = 0.0f; v2.sow1 = 255.0f; v2.tow1 = 0.0f;

    v3.x = x2; v3.y = y2; v3.oow = 1.0f;
    v3.sow = 255.0f; v3.tow = 255.0f; v3.sow1 = 255.0f; v3.tow1 = 255.0f;

    v4.x = x1; v4.y = y2; v4.oow = 1.0f;
    v4.sow = 0.0f; v4.tow = 255.0f; v4.sow1 = 0.0f; v4.tow1 = 255.0f;

    v1.r = v1.g = v1.b = v1.a = 255.0f;
    v2.r = v2.g = v2.b = v2.a = 255.0f;
    v3.r = v3.g = v3.b = v3.a = 255.0f;
    v4.r = v4.g = v4.b = v4.a = 255.0f;

    grDrawTriangle(&v1, &v2, &v3);
    grDrawTriangle(&v1, &v3, &v4);
}

int main(int argc, char *argv[])
{
    GrContext_t ctx;
    FxU32 tmu0_addr, tmu1_addr;
    GrTexInfo tex_bg_info, tex_sprite_1555_info, tex_sprite_4444_info;
    FxU16 tex_bg[16 * 16];
    FxU16 tex_sprite_1555[16 * 16];
    FxU16 tex_sprite_4444[16 * 16];
    GrLfbInfo_t lfb_info;

    (void)argc;
    (void)argv;

    LOG("=== Dual TMU Alpha Blending Test (Diablo 2 Sprite Scenario) ===");
    LOG("");
    LOG("This test reproduces the 'black boxes behind sprites' issue.");
    LOG("");
    LOG("Setup:");
    LOG("  TMU1: Background (magenta/cyan checkerboard)");
    LOG("  TMU0: Foreground sprite with alpha");
    LOG("");

    /* Create textures */
    create_checker_rgb565(tex_bg, 16, 16);
    create_sprite_argb1555(tex_sprite_1555, 16, 16);
    create_sprite_argb4444(tex_sprite_4444, 16, 16);

    LOG("Textures created:");
    LOG("  Background checker: 0x%04X / 0x%04X", tex_bg[0], tex_bg[4]);
    LOG("  Sprite ARGB1555: transparent=0x%04X, opaque=0x%04X",
        tex_sprite_1555[0], tex_sprite_1555[3*16+7]);
    LOG("  Sprite ARGB4444 center: 0x%04X", tex_sprite_4444[8*16+8]);

    /* Initialize Glide */
    grGlideInit();
    grSstSelect(0);

    ctx = grSstWinOpen(0, GR_RESOLUTION_640x480, GR_REFRESH_60Hz,
                       GR_COLORFORMAT_ARGB, GR_ORIGIN_UPPER_LEFT, 2, 1);
    if (!ctx) {
        LOG("FAILED: grSstWinOpen");
        grGlideShutdown();
        return 1;
    }

    /* Get texture addresses */
    tmu0_addr = grTexMinAddress(GR_TMU0);
    tmu1_addr = grTexMinAddress(GR_TMU1);

    LOG("TMU addresses: TMU0=0x%X, TMU1=0x%X", tmu0_addr, tmu1_addr);

    /* Setup texture info */
    memset(&tex_bg_info, 0, sizeof(tex_bg_info));
    tex_bg_info.smallLodLog2 = GR_LOD_LOG2_16;
    tex_bg_info.largeLodLog2 = GR_LOD_LOG2_16;
    tex_bg_info.aspectRatioLog2 = GR_ASPECT_LOG2_1x1;
    tex_bg_info.format = GR_TEXFMT_RGB_565;
    tex_bg_info.data = tex_bg;

    memset(&tex_sprite_1555_info, 0, sizeof(tex_sprite_1555_info));
    tex_sprite_1555_info.smallLodLog2 = GR_LOD_LOG2_16;
    tex_sprite_1555_info.largeLodLog2 = GR_LOD_LOG2_16;
    tex_sprite_1555_info.aspectRatioLog2 = GR_ASPECT_LOG2_1x1;
    tex_sprite_1555_info.format = GR_TEXFMT_ARGB_1555;
    tex_sprite_1555_info.data = tex_sprite_1555;

    memset(&tex_sprite_4444_info, 0, sizeof(tex_sprite_4444_info));
    tex_sprite_4444_info.smallLodLog2 = GR_LOD_LOG2_16;
    tex_sprite_4444_info.largeLodLog2 = GR_LOD_LOG2_16;
    tex_sprite_4444_info.aspectRatioLog2 = GR_ASPECT_LOG2_1x1;
    tex_sprite_4444_info.format = GR_TEXFMT_ARGB_4444;
    tex_sprite_4444_info.data = tex_sprite_4444;

    /* Download textures */
    LOG("Downloading background to TMU1...");
    grTexDownloadMipMap(GR_TMU1, tmu1_addr, GR_MIPMAPLEVELMASK_BOTH, &tex_bg_info);

    LOG("Downloading ARGB1555 sprite to TMU0...");
    grTexDownloadMipMap(GR_TMU0, tmu0_addr, GR_MIPMAPLEVELMASK_BOTH, &tex_sprite_1555_info);

    /* Common state */
    grDepthBufferMode(GR_DEPTHBUFFER_DISABLE);
    grDepthMask(FXFALSE);
    grTexFilterMode(GR_TMU0, GR_TEXTUREFILTER_POINT_SAMPLED, GR_TEXTUREFILTER_POINT_SAMPLED);
    grTexFilterMode(GR_TMU1, GR_TEXTUREFILTER_POINT_SAMPLED, GR_TEXTUREFILTER_POINT_SAMPLED);
    grTexMipMapMode(GR_TMU0, GR_MIPMAP_DISABLE, FXFALSE);
    grTexMipMapMode(GR_TMU1, GR_MIPMAP_DISABLE, FXFALSE);

    /* No framebuffer alpha blending - we want to test TMU combine only */
    grAlphaBlendFunction(GR_BLEND_ONE, GR_BLEND_ZERO, GR_BLEND_ONE, GR_BLEND_ZERO);

    grBufferClear(0x00202020, 0, 0xFFFFFFFF);  /* Dark gray background */

    /* Color/Alpha combine: pass through texture result */
    grColorCombine(GR_COMBINE_FUNCTION_SCALE_OTHER, GR_COMBINE_FACTOR_ONE,
                   GR_COMBINE_LOCAL_NONE, GR_COMBINE_OTHER_TEXTURE, FXFALSE);
    grAlphaCombine(GR_COMBINE_FUNCTION_SCALE_OTHER, GR_COMBINE_FACTOR_ONE,
                   GR_COMBINE_LOCAL_NONE, GR_COMBINE_OTHER_TEXTURE, FXFALSE);

    /*
     * TEST 1: ARGB1555 sprite using SCALE_OTHER_MINUS_LOCAL_ADD_LOCAL
     *
     * This should produce: output = other*(1-alpha) + local*alpha
     * Which is: output = background*(1-sprite_alpha) + sprite*sprite_alpha
     *
     * TMU1 (background) -> c_other
     * TMU0 (sprite) -> c_local
     * Blend factor: ONE_MINUS_LOCAL_ALPHA
     */
    LOG("");
    LOG("=== TEST 1: ARGB1555 sprite blend (SCALE_OTHER_MINUS_LOCAL_ADD_LOCAL) ===");

    grTexSource(GR_TMU0, tmu0_addr, GR_MIPMAPLEVELMASK_BOTH, &tex_sprite_1555_info);
    grTexSource(GR_TMU1, tmu1_addr, GR_MIPMAPLEVELMASK_BOTH, &tex_bg_info);

    /* TMU1: output its texture (background) as c_other for TMU0 */
    grTexCombine(GR_TMU1,
                 GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE,
                 GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE,
                 FXFALSE, FXFALSE);

    /* TMU0: blend local (sprite) with other (background) using local alpha
     * Function: (other - local) * factor + local = lerp(local, other, factor)
     * With factor = ONE_MINUS_LOCAL_ALPHA, this gives:
     *   output = (other - local) * (1 - local_alpha) + local
     *          = other * (1 - alpha) - local * (1 - alpha) + local
     *          = other * (1 - alpha) + local * alpha
     */
    grTexCombine(GR_TMU0,
                 GR_COMBINE_FUNCTION_SCALE_OTHER_MINUS_LOCAL_ADD_LOCAL,
                 GR_COMBINE_FACTOR_ONE_MINUS_LOCAL_ALPHA,
                 GR_COMBINE_FUNCTION_SCALE_OTHER_MINUS_LOCAL_ADD_LOCAL,
                 GR_COMBINE_FACTOR_ONE_MINUS_LOCAL_ALPHA,
                 FXFALSE, FXFALSE);

    draw_dual_tmu_quad(50.0f, 50.0f, 150.0f, 150.0f);
    LOG("  Drew at (50,50)-(150,150)");

    /*
     * TEST 2: Same but using BLEND function variant
     */
    LOG("");
    LOG("=== TEST 2: ARGB1555 using BLEND variant ===");

    /* Try alternative: SCALE_OTHER with alpha factor, then add local */
    grTexCombine(GR_TMU0,
                 GR_COMBINE_FUNCTION_BLEND, GR_COMBINE_FACTOR_LOCAL_ALPHA,
                 GR_COMBINE_FUNCTION_BLEND, GR_COMBINE_FACTOR_LOCAL_ALPHA,
                 FXFALSE, FXFALSE);

    draw_dual_tmu_quad(200.0f, 50.0f, 300.0f, 150.0f);
    LOG("  Drew at (200,50)-(300,150)");

    /*
     * TEST 3: ARGB4444 sprite with gradient alpha
     */
    LOG("");
    LOG("=== TEST 3: ARGB4444 sprite (gradient alpha) ===");

    grTexDownloadMipMap(GR_TMU0, tmu0_addr, GR_MIPMAPLEVELMASK_BOTH, &tex_sprite_4444_info);
    grTexSource(GR_TMU0, tmu0_addr, GR_MIPMAPLEVELMASK_BOTH, &tex_sprite_4444_info);

    grTexCombine(GR_TMU0,
                 GR_COMBINE_FUNCTION_SCALE_OTHER_MINUS_LOCAL_ADD_LOCAL,
                 GR_COMBINE_FACTOR_ONE_MINUS_LOCAL_ALPHA,
                 GR_COMBINE_FUNCTION_SCALE_OTHER_MINUS_LOCAL_ADD_LOCAL,
                 GR_COMBINE_FACTOR_ONE_MINUS_LOCAL_ALPHA,
                 FXFALSE, FXFALSE);

    draw_dual_tmu_quad(350.0f, 50.0f, 450.0f, 150.0f);
    LOG("  Drew at (350,50)-(450,150)");

    /*
     * TEST 4: TMU0 only (no TMU1) - sprite should show on gray background
     * This verifies TMU0 texture is correct
     */
    LOG("");
    LOG("=== TEST 4: TMU0 only (sprite on gray, no TMU1) ===");

    grTexDownloadMipMap(GR_TMU0, tmu0_addr, GR_MIPMAPLEVELMASK_BOTH, &tex_sprite_1555_info);
    grTexSource(GR_TMU0, tmu0_addr, GR_MIPMAPLEVELMASK_BOTH, &tex_sprite_1555_info);

    /* TMU0: just output local (the sprite) */
    grTexCombine(GR_TMU0,
                 GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE,
                 GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE,
                 FXFALSE, FXFALSE);

    /* TMU1: disabled (won't contribute) */
    grTexCombine(GR_TMU1,
                 GR_COMBINE_FUNCTION_ZERO, GR_COMBINE_FACTOR_NONE,
                 GR_COMBINE_FUNCTION_ZERO, GR_COMBINE_FACTOR_NONE,
                 FXFALSE, FXFALSE);

    draw_dual_tmu_quad(50.0f, 200.0f, 150.0f, 300.0f);
    LOG("  Drew at (50,200)-(150,300)");

    /*
     * TEST 5: TMU1 only - background should show
     */
    LOG("");
    LOG("=== TEST 5: TMU1 only (background, no sprite) ===");

    grTexSource(GR_TMU1, tmu1_addr, GR_MIPMAPLEVELMASK_BOTH, &tex_bg_info);

    /* TMU1: output its texture */
    grTexCombine(GR_TMU1,
                 GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE,
                 GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE,
                 FXFALSE, FXFALSE);

    /* TMU0: pass through TMU1 */
    grTexCombine(GR_TMU0,
                 GR_COMBINE_FUNCTION_SCALE_OTHER, GR_COMBINE_FACTOR_ONE,
                 GR_COMBINE_FUNCTION_SCALE_OTHER, GR_COMBINE_FACTOR_ONE,
                 FXFALSE, FXFALSE);

    draw_dual_tmu_quad(200.0f, 200.0f, 300.0f, 300.0f);
    LOG("  Drew at (200,200)-(300,300)");

    /* Swap and display */
    grBufferSwap(1);
    Sleep(100);

    LOG("");
    LOG("=== Sampling framebuffer ===");

    memset(&lfb_info, 0, sizeof(lfb_info));
    lfb_info.size = sizeof(lfb_info);

    if (grLfbLock(GR_LFB_READ_ONLY, GR_BUFFER_FRONTBUFFER, GR_LFBWRITEMODE_ANY,
                  GR_ORIGIN_UPPER_LEFT, FXFALSE, &lfb_info)) {

        uint16_t* fb = (uint16_t*)lfb_info.lfbPtr;
        int stride = lfb_info.strideInBytes / 2;

        LOG("LFB locked: stride=%d", stride);

        /* Test 1 analysis */
        {
            /* On the white "T" letter - should be white */
            int x_on = 100, y_on = 75;
            uint16_t pixel_on = fb[y_on * stride + x_on];
            uint8_t r_on, g_on, b_on;
            decode_rgb565(pixel_on, &r_on, &g_on, &b_on);

            /* Off the letter - should show checkerboard */
            int x_off = 60, y_off = 120;
            uint16_t pixel_off = fb[y_off * stride + x_off];
            uint8_t r_off, g_off, b_off;
            decode_rgb565(pixel_off, &r_off, &g_off, &b_off);

            LOG("");
            LOG("TEST 1 (ARGB1555 blend):");
            LOG("  ON letter at (%d,%d): 0x%04X -> R=%d G=%d B=%d", x_on, y_on, pixel_on, r_on, g_on, b_on);
            LOG("  OFF letter at (%d,%d): 0x%04X -> R=%d G=%d B=%d", x_off, y_off, pixel_off, r_off, g_off, b_off);

            int letter_white = (r_on > 200 && g_on > 200 && b_on > 200);
            int bg_colorful = (r_off > 50 || g_off > 50 || b_off > 50);
            int bg_black = (r_off < 30 && g_off < 30 && b_off < 30);

            if (letter_white && bg_colorful) {
                LOG("  -> PASS: White letter, checkerboard background visible!");
            } else if (letter_white && bg_black) {
                LOG("  -> ISSUE: Letter OK, but background is BLACK!");
                LOG("     This is the 'black box' issue - TMU1 not blending through!");
            } else if (!letter_white && bg_black) {
                LOG("  -> FAIL: Everything dark - TMU combine not working");
            } else {
                LOG("  -> UNEXPECTED: letter_white=%d, bg_colorful=%d", letter_white, bg_colorful);
            }
        }

        /* Test 4 analysis (TMU0 only) */
        {
            int x_on = 100, y_on = 225;
            uint16_t pixel_on = fb[y_on * stride + x_on];
            uint8_t r_on, g_on, b_on;
            decode_rgb565(pixel_on, &r_on, &g_on, &b_on);

            int x_off = 60, y_off = 270;
            uint16_t pixel_off = fb[y_off * stride + x_off];
            uint8_t r_off, g_off, b_off;
            decode_rgb565(pixel_off, &r_off, &g_off, &b_off);

            LOG("");
            LOG("TEST 4 (TMU0 only - no blend):");
            LOG("  ON letter at (%d,%d): 0x%04X -> R=%d G=%d B=%d", x_on, y_on, pixel_on, r_on, g_on, b_on);
            LOG("  OFF letter at (%d,%d): 0x%04X -> R=%d G=%d B=%d", x_off, y_off, pixel_off, r_off, g_off, b_off);

            int letter_white = (r_on > 200 && g_on > 200 && b_on > 200);
            int bg_black = (r_off < 50 && g_off < 50 && b_off < 50);

            if (letter_white && bg_black) {
                LOG("  -> OK: White letter on black (transparent areas show clear color)");
            } else if (!letter_white) {
                LOG("  -> ISSUE: Letter not white - TMU0 texture problem");
            }
        }

        /* Test 5 analysis (TMU1 only) */
        {
            int x = 250, y = 250;
            uint16_t pixel = fb[y * stride + x];
            uint8_t r, g, b;
            decode_rgb565(pixel, &r, &g, &b);

            LOG("");
            LOG("TEST 5 (TMU1 only - background):");
            LOG("  At (%d,%d): 0x%04X -> R=%d G=%d B=%d", x, y, pixel, r, g, b);

            if ((r > 150 && b > 150) || (g > 150 && b > 150)) {
                LOG("  -> PASS: Checkerboard visible (magenta or cyan)");
            } else if (r < 50 && g < 50 && b < 50) {
                LOG("  -> FAIL: Black - TMU1 not rendering");
            }
        }

        grLfbUnlock(GR_LFB_READ_ONLY, GR_BUFFER_FRONTBUFFER);
    } else {
        LOG("ERROR: Failed to lock LFB");
    }

    LOG("");
    LOG("Displaying for 5 seconds...");
    Sleep(5000);

    grSstWinClose(ctx);
    grGlideShutdown();

    LOG("");
    LOG("=== Test complete ===");
    return 0;
}
