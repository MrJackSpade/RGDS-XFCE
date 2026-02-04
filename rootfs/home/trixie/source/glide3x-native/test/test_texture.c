/*
 * test_texture.c - Texture rendering test for software Glide3x DLL
 *
 * Tests texture functionality:
 * 1. Texture memory allocation
 * 2. Texture download (grTexDownloadMipMap)
 * 3. Texture source setup (grTexSource)
 * 4. Textured triangle drawing
 * 5. Multiple texture formats
 */

#include <windows.h>
#include <stdio.h>
#include <string.h>
#include "../src/glide3x.h"

/* Simple logging */
#define LOG(fmt, ...) do { \
    char buf[512]; \
    snprintf(buf, sizeof(buf), fmt "\n", ##__VA_ARGS__); \
    OutputDebugStringA(buf); \
    printf("%s", buf); \
} while(0)

/* Wait for user to press Enter - disabled for automated testing */
static void wait_for_input(const char *prompt)
{
    printf("\n>>> %s\n", prompt);
    fflush(stdout);
    /* Skip actual wait for automated testing */
}

/* Create a simple 64x64 RGB565 checkerboard texture */
static void create_checkerboard_rgb565(FxU16 *data, int width, int height, FxU16 color1, FxU16 color2)
{
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            int checker = ((x / 8) + (y / 8)) & 1;
            data[y * width + x] = checker ? color1 : color2;
        }
    }
}

/* Create a gradient texture (RGB565) */
static void create_gradient_rgb565(FxU16 *data, int width, int height)
{
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            /* Red increases left to right, Green increases top to bottom */
            int r = (x * 31) / width;
            int g = (y * 63) / height;
            int b = 15;
            data[y * width + x] = (FxU16)((r << 11) | (g << 5) | b);
        }
    }
}

/* Create a solid color texture (RGB565) */
static void create_solid_rgb565(FxU16 *data, int width, int height, FxU16 color)
{
    for (int i = 0; i < width * height; i++) {
        data[i] = color;
    }
}

int main(int argc, char *argv[])
{
    GrContext_t ctx;
    GrVertex v1, v2, v3, v4;
    int frame;
    FxU32 tex_start_addr;
    GrTexInfo tex_info;
    FxU16 *tex_data;
    int tex_size = 64;

    (void)argc;
    (void)argv;

    LOG("=== Glide3x Texture Test ===");

    /* Allocate texture data */
    tex_data = (FxU16 *)malloc(tex_size * tex_size * sizeof(FxU16));
    if (!tex_data) {
        LOG("FAILED: Could not allocate texture data");
        return 1;
    }

    /* Initialize Glide */
    LOG("Step 1: Initialize Glide");
    grGlideInit();
    grSstSelect(0);

    /* Open window */
    LOG("Step 2: Open window (640x480)");
    ctx = grSstWinOpen(
        0,
        GR_RESOLUTION_640x480,
        GR_REFRESH_60Hz,
        GR_COLORFORMAT_ARGB,
        GR_ORIGIN_UPPER_LEFT,
        2,
        1
    );

    if (!ctx) {
        LOG("FAILED: grSstWinOpen returned NULL");
        free(tex_data);
        grGlideShutdown();
        return 1;
    }
    LOG("  Context: %p", ctx);

    /* Query texture memory */
    LOG("Step 3: Query texture memory");
    {
        FxU32 min_addr = grTexMinAddress(GR_TMU0);
        FxU32 max_addr = grTexMaxAddress(GR_TMU0);
        LOG("  TMU0: min=0x%08X, max=0x%08X, size=%u KB",
            min_addr, max_addr, (max_addr - min_addr) / 1024);
        tex_start_addr = min_addr;
    }

    /* Set up texture info */
    LOG("Step 4: Set up texture info (64x64 RGB565)");
    memset(&tex_info, 0, sizeof(tex_info));
    tex_info.smallLodLog2 = GR_LOD_LOG2_64;
    tex_info.largeLodLog2 = GR_LOD_LOG2_64;
    tex_info.aspectRatioLog2 = GR_ASPECT_LOG2_1x1;
    tex_info.format = GR_TEXFMT_RGB_565;
    tex_info.data = tex_data;

    /* Calculate texture memory needed */
    {
        FxU32 tex_mem = grTexTextureMemRequired(GR_MIPMAPLEVELMASK_BOTH, &tex_info);
        LOG("  Texture memory required: %u bytes", tex_mem);
    }

    /* Create checkerboard pattern (red/white) */
    LOG("Step 5: Create checkerboard texture");
    /* RGB565: Red = 0xF800, White = 0xFFFF */
    create_checkerboard_rgb565(tex_data, tex_size, tex_size, 0xF800, 0xFFFF);

    /* Download texture */
    LOG("Step 6: Download texture to TMU0");
    grTexDownloadMipMap(GR_TMU0, tex_start_addr, GR_MIPMAPLEVELMASK_BOTH, &tex_info);
    LOG("  Download complete");

    /* Set texture source */
    LOG("Step 7: Set texture source");
    grTexSource(GR_TMU0, tex_start_addr, GR_MIPMAPLEVELMASK_BOTH, &tex_info);
    LOG("  Texture source set");

    /* Configure texture combine - use texture color */
    LOG("Step 8: Configure texture/color combine");
    grTexCombine(
        GR_TMU0,
        GR_COMBINE_FUNCTION_LOCAL,      /* RGB = texture */
        GR_COMBINE_FACTOR_NONE,
        GR_COMBINE_FUNCTION_LOCAL,      /* Alpha = texture */
        GR_COMBINE_FACTOR_NONE,
        FXFALSE,
        FXFALSE
    );

    /* Color combine: use texture */
    grColorCombine(
        GR_COMBINE_FUNCTION_SCALE_OTHER,
        GR_COMBINE_FACTOR_ONE,
        GR_COMBINE_LOCAL_NONE,
        GR_COMBINE_OTHER_TEXTURE,
        FXFALSE
    );

    grAlphaCombine(
        GR_COMBINE_FUNCTION_LOCAL,
        GR_COMBINE_FACTOR_NONE,
        GR_COMBINE_LOCAL_CONSTANT,
        GR_COMBINE_OTHER_NONE,
        FXFALSE
    );
    grConstantColorValue(0xFFFFFFFF);

    /* Disable depth test for simplicity */
    grDepthBufferMode(GR_DEPTHBUFFER_DISABLE);
    grDepthMask(FXFALSE);

    /* Set up texture filtering */
    grTexFilterMode(GR_TMU0, GR_TEXTUREFILTER_BILINEAR, GR_TEXTUREFILTER_BILINEAR);
    grTexMipMapMode(GR_TMU0, GR_MIPMAP_DISABLE, FXFALSE);

    /* Draw frames */
    LOG("Step 9: Draw textured quad (as two triangles)");
    wait_for_input("You should see a RED/WHITE CHECKERBOARD texture on a dark blue background");

    for (frame = 0; frame < 180; frame++) {  /* 3 seconds at 60fps */
        /* Clear to dark blue */
        grBufferClear(0x00002080, 0, 0xFFFFFFFF);

        /* Set up vertices for a textured quad */
        /* Quad from (100,100) to (540,380) */
        memset(&v1, 0, sizeof(v1));
        memset(&v2, 0, sizeof(v2));
        memset(&v3, 0, sizeof(v3));
        memset(&v4, 0, sizeof(v4));

        /* Top-left */
        v1.x = 100.0f;
        v1.y = 100.0f;
        v1.oow = 1.0f;
        v1.sow = 0.0f;
        v1.tow = 0.0f;
        v1.r = v1.g = v1.b = v1.a = 255.0f;

        /* Top-right */
        v2.x = 540.0f;
        v2.y = 100.0f;
        v2.oow = 1.0f;
        v2.sow = 255.0f;  /* S coord (0-255 maps to 0-1 in texture) */
        v2.tow = 0.0f;
        v2.r = v2.g = v2.b = v2.a = 255.0f;

        /* Bottom-right */
        v3.x = 540.0f;
        v3.y = 380.0f;
        v3.oow = 1.0f;
        v3.sow = 255.0f;
        v3.tow = 255.0f;
        v3.r = v3.g = v3.b = v3.a = 255.0f;

        /* Bottom-left */
        v4.x = 100.0f;
        v4.y = 380.0f;
        v4.oow = 1.0f;
        v4.sow = 0.0f;
        v4.tow = 255.0f;
        v4.r = v4.g = v4.b = v4.a = 255.0f;

        /* Draw quad as two triangles */
        grDrawTriangle(&v1, &v2, &v3);  /* Top-right triangle */
        grDrawTriangle(&v1, &v3, &v4);  /* Bottom-left triangle */

        /* Change texture every 60 frames */
        if (frame == 59) {
            wait_for_input("Next: GRADIENT texture (red left-to-right, green top-to-bottom)");
            LOG("  Switching to gradient texture");
            create_gradient_rgb565(tex_data, tex_size, tex_size);
            grTexDownloadMipMap(GR_TMU0, tex_start_addr, GR_MIPMAPLEVELMASK_BOTH, &tex_info);
        } else if (frame == 119) {
            wait_for_input("Next: GREEN/BLUE CHECKERBOARD texture");
            LOG("  Switching to green/blue checkerboard");
            /* Green = 0x07E0, Blue = 0x001F */
            create_checkerboard_rgb565(tex_data, tex_size, tex_size, 0x07E0, 0x001F);
            grTexDownloadMipMap(GR_TMU0, tex_start_addr, GR_MIPMAPLEVELMASK_BOTH, &tex_info);
        }

        grBufferSwap(1);
        Sleep(16);

        if (frame == 0) {
            LOG("  Frame 0 rendered");
        }
    }

    LOG("  Drew %d frames", frame);

    /* Test: Draw without texture (vertex colors only) */
    LOG("Step 10: Test vertex colors (no texture)");
    wait_for_input("Next: RGB TRIANGLE with vertex colors (no texture) - red/green/blue corners");
    grColorCombine(
        GR_COMBINE_FUNCTION_LOCAL,
        GR_COMBINE_FACTOR_NONE,
        GR_COMBINE_LOCAL_ITERATED,
        GR_COMBINE_OTHER_NONE,
        FXFALSE
    );

    for (frame = 0; frame < 60; frame++) {
        grBufferClear(0x00400000, 0, 0xFFFFFFFF);

        /* RGB triangle with vertex colors */
        v1.x = 320.0f; v1.y = 100.0f; v1.oow = 1.0f;
        v1.r = 255.0f; v1.g = 0.0f; v1.b = 0.0f; v1.a = 255.0f;

        v2.x = 160.0f; v2.y = 380.0f; v2.oow = 1.0f;
        v2.r = 0.0f; v2.g = 255.0f; v2.b = 0.0f; v2.a = 255.0f;

        v3.x = 480.0f; v3.y = 380.0f; v3.oow = 1.0f;
        v3.r = 0.0f; v3.g = 0.0f; v3.b = 255.0f; v3.a = 255.0f;

        grDrawTriangle(&v1, &v2, &v3);
        grBufferSwap(1);
        Sleep(16);
    }
    LOG("  Vertex color test complete");
    wait_for_input("Test complete. Review results above.");

    /* Cleanup */
    LOG("Step 11: Cleanup");
    free(tex_data);
    grSstWinClose(ctx);
    grGlideShutdown();

    LOG("=== Texture test complete ===");
    return 0;
}
