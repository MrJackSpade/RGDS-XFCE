/*
 * test_texture_simple.c - Simple texture verification test
 *
 * Creates a 16x16 texture where each pixel has a unique value (0x01YX)
 * so we can verify texture sampling is working correctly.
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

int main(int argc, char *argv[])
{
    GrContext_t ctx;
    GrVertex v1, v2, v3, v4;
    FxU32 tex_start_addr;
    GrTexInfo tex_info;
    FxU16 tex_data[16 * 16];
    int x, y;

    (void)argc;
    (void)argv;

    LOG("=== Simple Texture Test ===");
    LOG("Creating 16x16 texture with unique values per pixel");

    /* Create texture with visually distinct colors */
    /* Row 0: Red gradient (increasing red) */
    /* Row 1: Green gradient */
    /* Row 2: Blue gradient */
    /* Row 3+: Mix based on position */
    for (y = 0; y < 16; y++) {
        for (x = 0; x < 16; x++) {
            int r, g, b;
            if (y == 0) { r = x * 2; g = 0; b = 0; }           /* Red row */
            else if (y == 1) { r = 0; g = x * 4; b = 0; }      /* Green row */
            else if (y == 2) { r = 0; g = 0; b = x * 2; }      /* Blue row */
            else { r = x * 2; g = y * 4; b = 15; }             /* Mixed */
            /* RGB565: RRRRR GGGGGG BBBBB */
            tex_data[y * 16 + x] = (FxU16)((r << 11) | (g << 5) | b);
        }
    }

    /* Log what we're uploading */
    LOG("Texture row 0 (red gradient): %04X %04X %04X %04X ...",
        tex_data[0], tex_data[1], tex_data[2], tex_data[3]);
    LOG("Texture row 1 (green gradient): %04X %04X %04X %04X ...",
        tex_data[16], tex_data[17], tex_data[18], tex_data[19]);

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

    tex_start_addr = grTexMinAddress(GR_TMU0);

    /* Set up texture info for 16x16 RGB565 */
    memset(&tex_info, 0, sizeof(tex_info));
    tex_info.smallLodLog2 = GR_LOD_LOG2_16;  /* 16x16 */
    tex_info.largeLodLog2 = GR_LOD_LOG2_16;
    tex_info.aspectRatioLog2 = GR_ASPECT_LOG2_1x1;
    tex_info.format = GR_TEXFMT_RGB_565;
    tex_info.data = tex_data;

    /* Download and set texture */
    grTexDownloadMipMap(GR_TMU0, tex_start_addr, GR_MIPMAPLEVELMASK_BOTH, &tex_info);
    grTexSource(GR_TMU0, tex_start_addr, GR_MIPMAPLEVELMASK_BOTH, &tex_info);

    /* Configure to show texture directly */
    grTexCombine(GR_TMU0,
                 GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE,
                 GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE,
                 FXFALSE, FXFALSE);

    grColorCombine(GR_COMBINE_FUNCTION_SCALE_OTHER, GR_COMBINE_FACTOR_ONE,
                   GR_COMBINE_LOCAL_NONE, GR_COMBINE_OTHER_TEXTURE, FXFALSE);

    grAlphaCombine(GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE,
                   GR_COMBINE_LOCAL_CONSTANT, GR_COMBINE_OTHER_NONE, FXFALSE);
    grConstantColorValue(0xFFFFFFFF);

    grDepthBufferMode(GR_DEPTHBUFFER_DISABLE);
    grDepthMask(FXFALSE);

    /* Use point sampling so we get exact texel values */
    grTexFilterMode(GR_TMU0, GR_TEXTUREFILTER_POINT_SAMPLED, GR_TEXTUREFILTER_POINT_SAMPLED);
    grTexMipMapMode(GR_TMU0, GR_MIPMAP_DISABLE, FXFALSE);

    LOG("Drawing textured quad...");

    /* Clear and draw */
    grBufferClear(0x00000000, 0, 0xFFFFFFFF);

    /* Draw a quad that maps 1:1 to texture - 16x16 pixels on screen */
    /* Position at (100,100) to (116,116) with tex coords 0-255 */
    memset(&v1, 0, sizeof(v1));
    memset(&v2, 0, sizeof(v2));
    memset(&v3, 0, sizeof(v3));
    memset(&v4, 0, sizeof(v4));

    /* 16x16 quad so 1 screen pixel = 1 texel (tex coords 0-255 for 16 texels) */
    v1.x = 100.0f; v1.y = 100.0f; v1.oow = 1.0f; v1.sow = 0.0f;   v1.tow = 0.0f;
    v2.x = 116.0f; v2.y = 100.0f; v2.oow = 1.0f; v2.sow = 255.0f; v2.tow = 0.0f;
    v3.x = 116.0f; v3.y = 116.0f; v3.oow = 1.0f; v3.sow = 255.0f; v3.tow = 255.0f;
    v4.x = 100.0f; v4.y = 116.0f; v4.oow = 1.0f; v4.sow = 0.0f;   v4.tow = 255.0f;

    v1.r = v1.g = v1.b = v1.a = 255.0f;
    v2.r = v2.g = v2.b = v2.a = 255.0f;
    v3.r = v3.g = v3.b = v3.a = 255.0f;
    v4.r = v4.g = v4.b = v4.a = 255.0f;

    grDrawTriangle(&v1, &v2, &v3);
    grDrawTriangle(&v1, &v3, &v4);

    grBufferSwap(1);

    LOG("Rendered. Sleeping 2 seconds...");
    Sleep(2000);

    grSstWinClose(ctx);
    grGlideShutdown();

    LOG("=== Test complete ===");
    return 0;
}
