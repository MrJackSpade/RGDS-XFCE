/*
 * test_alpha_blend.c - Alpha blending diagnostic test
 *
 * Tests multiple alpha blending scenarios to diagnose issues like
 * "text has black boxes behind it instead of proper texture".
 *
 * Test scenarios:
 * 1. ARGB_4444 texture blend (4-bit alpha)
 * 2. ARGB_1555 texture blend (1-bit binary alpha - common for text/sprites)
 * 3. Text-like overlay: partially transparent sprite on textured background
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

/* Create a solid color ARGB4444 texture
 * ARGB4444 format: AAAA RRRR GGGG BBBB (16 bits total)
 */
static void create_solid_argb4444(FxU16* data, int w, int h,
                                   uint8_t a, uint8_t r, uint8_t g, uint8_t b)
{
    uint16_t a4 = (a >> 4) & 0xF;
    uint16_t r4 = (r >> 4) & 0xF;
    uint16_t g4 = (g >> 4) & 0xF;
    uint16_t b4 = (b >> 4) & 0xF;

    FxU16 pixel = (FxU16)((a4 << 12) | (r4 << 8) | (g4 << 4) | b4);

    for (int i = 0; i < w * h; i++) {
        data[i] = pixel;
    }
}

/* Create a solid color ARGB1555 texture
 * ARGB1555 format: A RRRRR GGGGG BBBBB (16 bits total)
 * A = 0 means fully transparent, A = 1 means fully opaque
 */
static void create_solid_argb1555(FxU16* data, int w, int h,
                                   int opaque, uint8_t r, uint8_t g, uint8_t b)
{
    uint16_t a1 = opaque ? 1 : 0;
    uint16_t r5 = (r >> 3) & 0x1F;
    uint16_t g5 = (g >> 3) & 0x1F;
    uint16_t b5 = (b >> 3) & 0x1F;

    FxU16 pixel = (FxU16)((a1 << 15) | (r5 << 10) | (g5 << 5) | b5);

    for (int i = 0; i < w * h; i++) {
        data[i] = pixel;
    }
}

/* Create a "text-like" ARGB1555 texture:
 * - Opaque white pixels forming a letter shape
 * - Transparent (alpha=0) pixels elsewhere
 * This simulates how font textures typically work.
 */
static void create_text_argb1555(FxU16* data, int w, int h)
{
    /* Simple "T" shape pattern */
    FxU16 opaque_white = (1 << 15) | (0x1F << 10) | (0x1F << 5) | 0x1F;  /* A=1, R=G=B=31 */
    FxU16 transparent = 0;  /* A=0, RGB=0 */

    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            int is_letter = 0;

            /* Top bar of "T" (rows 2-4, full width) */
            if (y >= 2 && y <= 4 && x >= 2 && x <= 13) {
                is_letter = 1;
            }
            /* Vertical stem of "T" (rows 4-13, center columns) */
            if (y >= 4 && y <= 13 && x >= 6 && x <= 9) {
                is_letter = 1;
            }

            data[y * w + x] = is_letter ? opaque_white : transparent;
        }
    }
}

/* Create a checkerboard RGB565 texture (no alpha - for background) */
static void create_checker_rgb565(FxU16* data, int w, int h)
{
    /* Purple and cyan checkerboard */
    FxU16 color1 = (0x1F << 11) | (0x00 << 5) | 0x1F;  /* Magenta: R=max, G=0, B=max */
    FxU16 color2 = (0x00 << 11) | (0x3F << 5) | 0x1F;  /* Cyan: R=0, G=max, B=max */

    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            /* 4x4 checker pattern */
            data[y * w + x] = (((x >> 2) + (y >> 2)) & 1) ? color1 : color2;
        }
    }
}

/* Decode RGB565 pixel to components */
static void decode_rgb565(uint16_t pixel, uint8_t* r, uint8_t* g, uint8_t* b)
{
    *r = (uint8_t)(((pixel >> 11) & 0x1F) << 3);
    *g = (uint8_t)(((pixel >> 5) & 0x3F) << 2);
    *b = (uint8_t)((pixel & 0x1F) << 3);
}

/* Draw a textured quad */
static void draw_quad(float x1, float y1, float x2, float y2)
{
    GrVertex v1, v2, v3, v4;

    memset(&v1, 0, sizeof(v1));
    memset(&v2, 0, sizeof(v2));
    memset(&v3, 0, sizeof(v3));
    memset(&v4, 0, sizeof(v4));

    v1.x = x1; v1.y = y1; v1.oow = 1.0f; v1.sow = 0.0f;   v1.tow = 0.0f;
    v2.x = x2; v2.y = y1; v2.oow = 1.0f; v2.sow = 255.0f; v2.tow = 0.0f;
    v3.x = x2; v3.y = y2; v3.oow = 1.0f; v3.sow = 255.0f; v3.tow = 255.0f;
    v4.x = x1; v4.y = y2; v4.oow = 1.0f; v4.sow = 0.0f;   v4.tow = 255.0f;

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
    FxU32 tex_bg_addr, tex_fg_4444_addr, tex_fg_1555_addr, tex_text_addr;
    GrTexInfo tex_bg_info, tex_fg_4444_info, tex_fg_1555_info, tex_text_info;
    FxU16 tex_bg_data[16 * 16];       /* Background: checkerboard */
    FxU16 tex_fg_4444_data[16 * 16];  /* ARGB4444: 50% green */
    FxU16 tex_fg_1555_data[16 * 16];  /* ARGB1555: opaque green */
    FxU16 tex_text_data[16 * 16];     /* ARGB1555: text pattern */
    GrLfbInfo_t lfb_info;

    (void)argc;
    (void)argv;

    LOG("=== Comprehensive Alpha Blending Test ===");
    LOG("");
    LOG("Testing alpha compositing with multiple texture formats:");
    LOG("  1. ARGB_4444 (4-bit alpha) - continuous transparency");
    LOG("  2. ARGB_1555 (1-bit alpha) - binary transparency");
    LOG("  3. Text overlay simulation");
    LOG("");

    /* Create textures */
    /* Background: checkerboard pattern (magenta/cyan) - RGB565 no alpha */
    create_checker_rgb565(tex_bg_data, 16, 16);

    /* Foreground 4444: 50% transparent green */
    create_solid_argb4444(tex_fg_4444_data, 16, 16, 0x80, 0x00, 0xFF, 0x00);

    /* Foreground 1555: fully opaque green (alpha bit = 1) */
    create_solid_argb1555(tex_fg_1555_data, 16, 16, 1, 0x00, 0xFF, 0x00);

    /* Text: "T" pattern - white on transparent */
    create_text_argb1555(tex_text_data, 16, 16);

    LOG("Textures created:");
    LOG("  Background (RGB565 checker): first pixel = 0x%04X", tex_bg_data[0]);
    LOG("  FG ARGB4444 (50%% green): first pixel = 0x%04X", tex_fg_4444_data[0]);
    LOG("  FG ARGB1555 (opaque green): first pixel = 0x%04X", tex_fg_1555_data[0]);
    LOG("  Text ARGB1555: transparent=0x%04X, opaque=0x%04X", tex_text_data[0], tex_text_data[3*16+7]);

    /* Initialize Glide */
    grGlideInit();
    grSstSelect(0);

    ctx = grSstWinOpen(0, GR_RESOLUTION_640x480, GR_REFRESH_60Hz,
                       GR_COLORFORMAT_ARGB, GR_ORIGIN_UPPER_LEFT, 2, 1);
    if (!ctx) {
        LOG("FAILED: grSstWinOpen returned NULL");
        grGlideShutdown();
        return 1;
    }

    LOG("Context opened successfully");

    /* Allocate texture memory */
    tex_bg_addr = grTexMinAddress(GR_TMU0);
    tex_fg_4444_addr = tex_bg_addr + (16 * 16 * 2);
    tex_fg_1555_addr = tex_fg_4444_addr + (16 * 16 * 2);
    tex_text_addr = tex_fg_1555_addr + (16 * 16 * 2);

    /* Set up texture info structures */
    memset(&tex_bg_info, 0, sizeof(tex_bg_info));
    tex_bg_info.smallLodLog2 = GR_LOD_LOG2_16;
    tex_bg_info.largeLodLog2 = GR_LOD_LOG2_16;
    tex_bg_info.aspectRatioLog2 = GR_ASPECT_LOG2_1x1;
    tex_bg_info.format = GR_TEXFMT_RGB_565;
    tex_bg_info.data = tex_bg_data;

    memset(&tex_fg_4444_info, 0, sizeof(tex_fg_4444_info));
    tex_fg_4444_info.smallLodLog2 = GR_LOD_LOG2_16;
    tex_fg_4444_info.largeLodLog2 = GR_LOD_LOG2_16;
    tex_fg_4444_info.aspectRatioLog2 = GR_ASPECT_LOG2_1x1;
    tex_fg_4444_info.format = GR_TEXFMT_ARGB_4444;
    tex_fg_4444_info.data = tex_fg_4444_data;

    memset(&tex_fg_1555_info, 0, sizeof(tex_fg_1555_info));
    tex_fg_1555_info.smallLodLog2 = GR_LOD_LOG2_16;
    tex_fg_1555_info.largeLodLog2 = GR_LOD_LOG2_16;
    tex_fg_1555_info.aspectRatioLog2 = GR_ASPECT_LOG2_1x1;
    tex_fg_1555_info.format = GR_TEXFMT_ARGB_1555;
    tex_fg_1555_info.data = tex_fg_1555_data;

    memset(&tex_text_info, 0, sizeof(tex_text_info));
    tex_text_info.smallLodLog2 = GR_LOD_LOG2_16;
    tex_text_info.largeLodLog2 = GR_LOD_LOG2_16;
    tex_text_info.aspectRatioLog2 = GR_ASPECT_LOG2_1x1;
    tex_text_info.format = GR_TEXFMT_ARGB_1555;
    tex_text_info.data = tex_text_data;

    /* Download all textures */
    grTexDownloadMipMap(GR_TMU0, tex_bg_addr, GR_MIPMAPLEVELMASK_BOTH, &tex_bg_info);
    grTexDownloadMipMap(GR_TMU0, tex_fg_4444_addr, GR_MIPMAPLEVELMASK_BOTH, &tex_fg_4444_info);
    grTexDownloadMipMap(GR_TMU0, tex_fg_1555_addr, GR_MIPMAPLEVELMASK_BOTH, &tex_fg_1555_info);
    grTexDownloadMipMap(GR_TMU0, tex_text_addr, GR_MIPMAPLEVELMASK_BOTH, &tex_text_info);

    /* Configure texture filtering */
    grTexFilterMode(GR_TMU0, GR_TEXTUREFILTER_POINT_SAMPLED, GR_TEXTUREFILTER_POINT_SAMPLED);
    grTexMipMapMode(GR_TMU0, GR_MIPMAP_DISABLE, FXFALSE);

    /* Disable depth buffer */
    grDepthBufferMode(GR_DEPTHBUFFER_DISABLE);
    grDepthMask(FXFALSE);

    /* Clear screen to dark gray (so we can see black vs transparent) */
    grBufferClear(0x00202020, 0, 0xFFFFFFFF);

    /* TMU combine: output = texture */
    grTexCombine(GR_TMU0,
                 GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE,
                 GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE,
                 FXFALSE, FXFALSE);

    /* Color combine: output = texture RGB */
    grColorCombine(GR_COMBINE_FUNCTION_SCALE_OTHER, GR_COMBINE_FACTOR_ONE,
                   GR_COMBINE_LOCAL_NONE, GR_COMBINE_OTHER_TEXTURE, FXFALSE);

    /* Alpha combine: output = texture alpha */
    grAlphaCombine(GR_COMBINE_FUNCTION_SCALE_OTHER, GR_COMBINE_FACTOR_ONE,
                   GR_COMBINE_LOCAL_NONE, GR_COMBINE_OTHER_TEXTURE, FXFALSE);

    /*
     * TEST 1: ARGB_4444 blend (50% transparent green over checker)
     */
    LOG("");
    LOG("=== TEST 1: ARGB_4444 (50%% alpha) over checkerboard ===");

    /* Draw checkerboard background */
    grAlphaBlendFunction(GR_BLEND_ONE, GR_BLEND_ZERO, GR_BLEND_ONE, GR_BLEND_ZERO);
    grTexSource(GR_TMU0, tex_bg_addr, GR_MIPMAPLEVELMASK_BOTH, &tex_bg_info);
    draw_quad(50.0f, 50.0f, 114.0f, 114.0f);
    LOG("  Drew background at (50,50)-(114,114)");

    /* Draw 50% green overlay WITH alpha blending */
    grAlphaBlendFunction(GR_BLEND_SRC_ALPHA, GR_BLEND_ONE_MINUS_SRC_ALPHA,
                         GR_BLEND_ONE, GR_BLEND_ZERO);
    grTexSource(GR_TMU0, tex_fg_4444_addr, GR_MIPMAPLEVELMASK_BOTH, &tex_fg_4444_info);
    draw_quad(66.0f, 66.0f, 130.0f, 130.0f);
    LOG("  Drew 50%% green overlay at (66,66)-(130,130)");

    /*
     * TEST 2: ARGB_1555 (binary alpha) - common for sprites
     */
    LOG("");
    LOG("=== TEST 2: ARGB_1555 (binary alpha) - opaque green ===");

    /* Draw checkerboard background */
    grAlphaBlendFunction(GR_BLEND_ONE, GR_BLEND_ZERO, GR_BLEND_ONE, GR_BLEND_ZERO);
    grTexSource(GR_TMU0, tex_bg_addr, GR_MIPMAPLEVELMASK_BOTH, &tex_bg_info);
    draw_quad(200.0f, 50.0f, 264.0f, 114.0f);
    LOG("  Drew background at (200,50)-(264,114)");

    /* Draw opaque green (alpha=1) - should fully cover background */
    grAlphaBlendFunction(GR_BLEND_SRC_ALPHA, GR_BLEND_ONE_MINUS_SRC_ALPHA,
                         GR_BLEND_ONE, GR_BLEND_ZERO);
    grTexSource(GR_TMU0, tex_fg_1555_addr, GR_MIPMAPLEVELMASK_BOTH, &tex_fg_1555_info);
    draw_quad(216.0f, 66.0f, 280.0f, 130.0f);
    LOG("  Drew opaque green overlay at (216,66)-(280,130)");

    /*
     * TEST 3: Text overlay - "T" shape (white on transparent)
     * This simulates how text rendering typically works
     */
    LOG("");
    LOG("=== TEST 3: Text overlay (ARGB_1555 - white T on transparent) ===");

    /* Draw checkerboard background */
    grAlphaBlendFunction(GR_BLEND_ONE, GR_BLEND_ZERO, GR_BLEND_ONE, GR_BLEND_ZERO);
    grTexSource(GR_TMU0, tex_bg_addr, GR_MIPMAPLEVELMASK_BOTH, &tex_bg_info);
    draw_quad(350.0f, 50.0f, 414.0f, 114.0f);
    LOG("  Drew background at (350,50)-(414,114)");

    /* Draw text "T" with alpha blending - transparent parts should show background */
    grAlphaBlendFunction(GR_BLEND_SRC_ALPHA, GR_BLEND_ONE_MINUS_SRC_ALPHA,
                         GR_BLEND_ONE, GR_BLEND_ZERO);
    grTexSource(GR_TMU0, tex_text_addr, GR_MIPMAPLEVELMASK_BOTH, &tex_text_info);
    draw_quad(350.0f, 50.0f, 414.0f, 114.0f);
    LOG("  Drew text overlay at same position");

    /*
     * TEST 4: Text with ALPHA TEST instead of blend
     * Alpha test discards pixels with alpha below threshold
     */
    LOG("");
    LOG("=== TEST 4: Text with ALPHA TEST (discard transparent pixels) ===");

    /* Draw checkerboard background */
    grAlphaBlendFunction(GR_BLEND_ONE, GR_BLEND_ZERO, GR_BLEND_ONE, GR_BLEND_ZERO);
    grTexSource(GR_TMU0, tex_bg_addr, GR_MIPMAPLEVELMASK_BOTH, &tex_bg_info);
    draw_quad(350.0f, 200.0f, 414.0f, 264.0f);
    LOG("  Drew background at (350,200)-(414,264)");

    /* Draw text with ALPHA TEST enabled (discard if alpha == 0) */
    /* No blending - either draw or don't */
    grAlphaBlendFunction(GR_BLEND_ONE, GR_BLEND_ZERO, GR_BLEND_ONE, GR_BLEND_ZERO);
    grAlphaTestFunction(GR_CMP_GREATER);  /* Pass if alpha > ref */
    grAlphaTestReferenceValue(0x00);       /* Reference = 0, so alpha > 0 passes */
    grTexSource(GR_TMU0, tex_text_addr, GR_MIPMAPLEVELMASK_BOTH, &tex_text_info);
    draw_quad(350.0f, 200.0f, 414.0f, 264.0f);
    LOG("  Drew text with alpha test at same position");

    /* Disable alpha test for further rendering */
    grAlphaTestFunction(GR_CMP_ALWAYS);

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

        LOG("LFB locked: stride=%d pixels", stride);

        /* Sample Test 1: ARGB4444 blend */
        {
            int x = 82, y = 82;  /* Overlap region */
            uint16_t pixel = fb[y * stride + x];
            uint8_t r, g, b;
            decode_rgb565(pixel, &r, &g, &b);
            LOG("");
            LOG("TEST 1 (ARGB_4444 blend) at (%d,%d): 0x%04X -> R=%d G=%d B=%d", x, y, pixel, r, g, b);
            if (g > 50 && (r > 50 || b > 50)) {
                LOG("  -> PASS: Shows blend of green + checker background");
            } else if (r < 30 && g > 100 && b < 30) {
                LOG("  -> POSSIBLE ISSUE: Pure green, background not blending through");
            } else if (r < 30 && g < 30 && b < 30) {
                LOG("  -> FAIL: BLACK - alpha blending not working!");
            }
        }

        /* Sample Test 2: ARGB1555 opaque */
        {
            int x = 232, y = 82;  /* Overlap region */
            uint16_t pixel = fb[y * stride + x];
            uint8_t r, g, b;
            decode_rgb565(pixel, &r, &g, &b);
            LOG("");
            LOG("TEST 2 (ARGB_1555 opaque) at (%d,%d): 0x%04X -> R=%d G=%d B=%d", x, y, pixel, r, g, b);
            if (r < 30 && g > 200 && b < 30) {
                LOG("  -> PASS: Opaque green (alpha=1 means fully covering)");
            } else if (r < 30 && g < 30 && b < 30) {
                LOG("  -> FAIL: BLACK - 1-bit alpha not working!");
            }
        }

        /* Sample Test 3: Text blend - on letter vs off letter */
        {
            /* On the letter "T" (should be white) */
            int x_on = 382, y_on = 66;
            uint16_t pixel_on = fb[y_on * stride + x_on];
            uint8_t r_on, g_on, b_on;
            decode_rgb565(pixel_on, &r_on, &g_on, &b_on);

            /* Off the letter (transparent, should show background checker) */
            int x_off = 360, y_off = 90;
            uint16_t pixel_off = fb[y_off * stride + x_off];
            uint8_t r_off, g_off, b_off;
            decode_rgb565(pixel_off, &r_off, &g_off, &b_off);

            LOG("");
            LOG("TEST 3 (Text alpha blend):");
            LOG("  ON letter T at (%d,%d): 0x%04X -> R=%d G=%d B=%d", x_on, y_on, pixel_on, r_on, g_on, b_on);
            LOG("  OFF letter  at (%d,%d): 0x%04X -> R=%d G=%d B=%d", x_off, y_off, pixel_off, r_off, g_off, b_off);

            int letter_ok = (r_on > 200 && g_on > 200 && b_on > 200);  /* White */
            int bg_ok = (r_off > 50 || g_off > 50 || b_off > 50);      /* Not black */

            if (letter_ok && bg_ok) {
                LOG("  -> PASS: White letter visible, background shows through transparent parts");
            } else if (letter_ok && !bg_ok) {
                LOG("  -> PARTIAL: Letter OK, but transparent shows BLACK instead of background!");
                LOG("     This is the 'black box behind text' issue!");
            } else if (!letter_ok && !bg_ok) {
                LOG("  -> FAIL: Everything is dark - alpha not working at all");
            }
        }

        /* Sample Test 4: Text with alpha test */
        {
            int x_on = 382, y_on = 216;
            uint16_t pixel_on = fb[y_on * stride + x_on];
            uint8_t r_on, g_on, b_on;
            decode_rgb565(pixel_on, &r_on, &g_on, &b_on);

            int x_off = 360, y_off = 240;
            uint16_t pixel_off = fb[y_off * stride + x_off];
            uint8_t r_off, g_off, b_off;
            decode_rgb565(pixel_off, &r_off, &g_off, &b_off);

            LOG("");
            LOG("TEST 4 (Text alpha test):");
            LOG("  ON letter T at (%d,%d): 0x%04X -> R=%d G=%d B=%d", x_on, y_on, pixel_on, r_on, g_on, b_on);
            LOG("  OFF letter  at (%d,%d): 0x%04X -> R=%d G=%d B=%d", x_off, y_off, pixel_off, r_off, g_off, b_off);

            int letter_ok = (r_on > 200 && g_on > 200 && b_on > 200);
            int bg_ok = (r_off > 50 || g_off > 50 || b_off > 50);

            if (letter_ok && bg_ok) {
                LOG("  -> PASS: Alpha test working - discards alpha=0 pixels");
            } else if (letter_ok && !bg_ok) {
                LOG("  -> ISSUE: Alpha test not discarding transparent pixels");
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
