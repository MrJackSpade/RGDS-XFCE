/*
 * test_dual_tmu.c - Test dual TMU texture compositing
 *
 * Uploads distinct textures to TMU0 and TMU1, then renders with various
 * compositing modes to verify correct TMU selection and blending.
 *
 * TMU0: 16x16 RED texture (solid red)
 * TMU1: 8x8 BLUE texture (solid blue)
 *
 * Tests:
 * 1. TMU0 only - should show RED
 * 2. TMU1 only - should show BLUE
 * 3. TMU0 * TMU1 (multiply) - should show dark/black (red * blue = 0)
 * 4. TMU0 + TMU1 (add) - should show MAGENTA (red + blue)
 */

#include <windows.h>
#include <stdio.h>
#include <string.h>
#include "../src/glide3x.h"

/* Debug function declarations (not in public header) */
void __stdcall grDebugGetTexParams(GrChipID_t tmu, FxU32 *params);
FxU32 __stdcall grDebugGetChipmask(void);

#define LOG(fmt, ...) do { \
    char buf[512]; \
    snprintf(buf, sizeof(buf), fmt "\n", ##__VA_ARGS__); \
    OutputDebugStringA(buf); \
    printf("%s", buf); \
} while(0)

/* Create solid color RGB565 texture */
static void create_solid_texture(FxU16 *data, int width, int height, FxU16 color)
{
    for (int i = 0; i < width * height; i++) {
        data[i] = color;
    }
}

/* RGB565 color helpers */
#define RGB565_RED   0xF800  /* 11111 000000 00000 */
#define RGB565_GREEN 0x07E0  /* 00000 111111 00000 */
#define RGB565_BLUE  0x001F  /* 00000 000000 11111 */
#define RGB565_WHITE 0xFFFF
#define RGB565_BLACK 0x0000

static void draw_textured_quad(float x, float y, float size)
{
    GrVertex v1, v2, v3, v4;

    memset(&v1, 0, sizeof(v1));
    memset(&v2, 0, sizeof(v2));
    memset(&v3, 0, sizeof(v3));
    memset(&v4, 0, sizeof(v4));

    /* Texture coords span full texture (0-255) */
    /* TMU0: sow/tow, TMU1: sow1/tow1 */
    v1.x = x;        v1.y = y;        v1.oow = 1.0f;
    v1.sow = 0.0f;   v1.tow = 0.0f;   v1.sow1 = 0.0f;   v1.tow1 = 0.0f;

    v2.x = x + size; v2.y = y;        v2.oow = 1.0f;
    v2.sow = 255.0f; v2.tow = 0.0f;   v2.sow1 = 255.0f; v2.tow1 = 0.0f;

    v3.x = x + size; v3.y = y + size; v3.oow = 1.0f;
    v3.sow = 255.0f; v3.tow = 255.0f; v3.sow1 = 255.0f; v3.tow1 = 255.0f;

    v4.x = x;        v4.y = y + size; v4.oow = 1.0f;
    v4.sow = 0.0f;   v4.tow = 255.0f; v4.sow1 = 0.0f;   v4.tow1 = 255.0f;

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
    GrTexInfo tex_info_16x16, tex_info_8x8;
    FxU16 tex_red[16 * 16];
    FxU16 tex_blue[8 * 8];

    (void)argc;
    (void)argv;

    LOG("=== Dual TMU Test ===");
    LOG("TMU0: 16x16 RED texture");
    LOG("TMU1: 8x8 BLUE texture");

    /* Create textures */
    create_solid_texture(tex_red, 16, 16, RGB565_RED);
    create_solid_texture(tex_blue, 8, 8, RGB565_BLUE);

    LOG("Created textures: RED=0x%04X, BLUE=0x%04X", RGB565_RED, RGB565_BLUE);

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

    /* Get texture addresses */
    tmu0_addr = grTexMinAddress(GR_TMU0);
    tmu1_addr = grTexMinAddress(GR_TMU1);

    LOG("TMU0 base addr: 0x%X", tmu0_addr);
    LOG("TMU1 base addr: 0x%X", tmu1_addr);

    /* Query internal state for diagnostics */
    {
        FxU32 chipmask = grDebugGetChipmask();
        LOG("Chipmask: 0x%02X (FBI=%d TMU0=%d TMU1=%d)",
            chipmask,
            (chipmask & 0x01) ? 1 : 0,
            (chipmask & 0x02) ? 1 : 0,
            (chipmask & 0x04) ? 1 : 0);
        LOG("  Expected: 0x07 (FBI=1 TMU0=1 TMU1=1) for dual-TMU to work");

        FxU32 params[8];
        grDebugGetTexParams(GR_TMU0, params);
        LOG("TMU0 state: lodmin=%u lodmax=%u", params[2], params[3]);
        grDebugGetTexParams(GR_TMU1, params);
        LOG("TMU1 state: lodmin=%u lodmax=%u", params[2], params[3]);
    }

    /* Setup TMU0 texture info (16x16 RED) */
    memset(&tex_info_16x16, 0, sizeof(tex_info_16x16));
    tex_info_16x16.smallLodLog2 = GR_LOD_LOG2_16;
    tex_info_16x16.largeLodLog2 = GR_LOD_LOG2_16;
    tex_info_16x16.aspectRatioLog2 = GR_ASPECT_LOG2_1x1;
    tex_info_16x16.format = GR_TEXFMT_RGB_565;
    tex_info_16x16.data = tex_red;

    /* Setup TMU1 texture info (8x8 BLUE) */
    memset(&tex_info_8x8, 0, sizeof(tex_info_8x8));
    tex_info_8x8.smallLodLog2 = GR_LOD_LOG2_8;
    tex_info_8x8.largeLodLog2 = GR_LOD_LOG2_8;
    tex_info_8x8.aspectRatioLog2 = GR_ASPECT_LOG2_1x1;
    tex_info_8x8.format = GR_TEXFMT_RGB_565;
    tex_info_8x8.data = tex_blue;

    /* Download textures to TMUs */
    LOG("Downloading RED to TMU0...");
    grTexDownloadMipMap(GR_TMU0, tmu0_addr, GR_MIPMAPLEVELMASK_BOTH, &tex_info_16x16);

    LOG("Downloading BLUE to TMU1...");
    grTexDownloadMipMap(GR_TMU1, tmu1_addr, GR_MIPMAPLEVELMASK_BOTH, &tex_info_8x8);

    /* Common state */
    grDepthBufferMode(GR_DEPTHBUFFER_DISABLE);
    grDepthMask(FXFALSE);
    grTexFilterMode(GR_TMU0, GR_TEXTUREFILTER_POINT_SAMPLED, GR_TEXTUREFILTER_POINT_SAMPLED);
    grTexFilterMode(GR_TMU1, GR_TEXTUREFILTER_POINT_SAMPLED, GR_TEXTUREFILTER_POINT_SAMPLED);
    grTexMipMapMode(GR_TMU0, GR_MIPMAP_DISABLE, FXFALSE);
    grTexMipMapMode(GR_TMU1, GR_MIPMAP_DISABLE, FXFALSE);

    grBufferClear(0x00404040, 0, 0xFFFFFFFF);  /* Gray background */

    /*
     * TEST 1: TMU0 only (should be RED)
     */
    LOG("Test 1: TMU0 only (expect RED)");
    grTexSource(GR_TMU0, tmu0_addr, GR_MIPMAPLEVELMASK_BOTH, &tex_info_16x16);

    /* TMU0: output local color (the texture) */
    grTexCombine(GR_TMU0,
                 GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE,
                 GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE,
                 FXFALSE, FXFALSE);

    /* Color combine: use texture */
    grColorCombine(GR_COMBINE_FUNCTION_SCALE_OTHER, GR_COMBINE_FACTOR_ONE,
                   GR_COMBINE_LOCAL_NONE, GR_COMBINE_OTHER_TEXTURE, FXFALSE);
    grAlphaCombine(GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE,
                   GR_COMBINE_LOCAL_CONSTANT, GR_COMBINE_OTHER_NONE, FXFALSE);
    grConstantColorValue(0xFFFFFFFF);

    draw_textured_quad(50, 50, 100);

    /*
     * TEST 2: TMU1 only (should be BLUE)
     */
    LOG("Test 2: TMU1 only (expect BLUE)");
    grTexSource(GR_TMU1, tmu1_addr, GR_MIPMAPLEVELMASK_BOTH, &tex_info_8x8);

    /* TMU1: output local color (the texture) */
    grTexCombine(GR_TMU1,
                 GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE,
                 GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE,
                 FXFALSE, FXFALSE);

    /* TMU0: pass through TMU1's output */
    grTexCombine(GR_TMU0,
                 GR_COMBINE_FUNCTION_SCALE_OTHER, GR_COMBINE_FACTOR_ONE,
                 GR_COMBINE_FUNCTION_SCALE_OTHER, GR_COMBINE_FACTOR_ONE,
                 FXFALSE, FXFALSE);

    draw_textured_quad(200, 50, 100);

    /*
     * TEST 3: TMU0 modulated by TMU1 (RED * BLUE = should be dark/black)
     */
    LOG("Test 3: TMU0 * TMU1 (expect BLACK - red*blue=0)");
    grTexSource(GR_TMU0, tmu0_addr, GR_MIPMAPLEVELMASK_BOTH, &tex_info_16x16);
    grTexSource(GR_TMU1, tmu1_addr, GR_MIPMAPLEVELMASK_BOTH, &tex_info_8x8);

    /* TMU1: output local color */
    grTexCombine(GR_TMU1,
                 GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE,
                 GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE,
                 FXFALSE, FXFALSE);

    /* TMU0: local * other (multiply TMU0 by TMU1) */
    grTexCombine(GR_TMU0,
                 GR_COMBINE_FUNCTION_SCALE_OTHER, GR_COMBINE_FACTOR_LOCAL,
                 GR_COMBINE_FUNCTION_SCALE_OTHER, GR_COMBINE_FACTOR_LOCAL,
                 FXFALSE, FXFALSE);

    draw_textured_quad(350, 50, 100);

    /*
     * TEST 4: TMU0 + TMU1 (RED + BLUE = MAGENTA)
     */
    LOG("Test 4: TMU0 + TMU1 (expect MAGENTA)");
    grTexSource(GR_TMU0, tmu0_addr, GR_MIPMAPLEVELMASK_BOTH, &tex_info_16x16);
    grTexSource(GR_TMU1, tmu1_addr, GR_MIPMAPLEVELMASK_BOTH, &tex_info_8x8);

    /* TMU1: output local color */
    grTexCombine(GR_TMU1,
                 GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE,
                 GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE,
                 FXFALSE, FXFALSE);

    /* TMU0: local + other (add TMU0 and TMU1) */
    grTexCombine(GR_TMU0,
                 GR_COMBINE_FUNCTION_SCALE_OTHER_ADD_LOCAL, GR_COMBINE_FACTOR_ONE,
                 GR_COMBINE_FUNCTION_SCALE_OTHER_ADD_LOCAL, GR_COMBINE_FACTOR_ONE,
                 FXFALSE, FXFALSE);

    draw_textured_quad(500, 50, 100);

    /*
     * TEST 5: Swapped - TMU1 texture on TMU0, TMU0 texture on TMU1
     * Upload RED to TMU1, BLUE to TMU0, render TMU0 only
     * Should show BLUE (proves TMU0 is really being used)
     */
    LOG("Test 5: Swapped textures - BLUE on TMU0 (expect BLUE)");
    grTexDownloadMipMap(GR_TMU0, tmu0_addr, GR_MIPMAPLEVELMASK_BOTH, &tex_info_8x8);  /* BLUE to TMU0 */
    grTexDownloadMipMap(GR_TMU1, tmu1_addr, GR_MIPMAPLEVELMASK_BOTH, &tex_info_16x16); /* RED to TMU1 */

    grTexSource(GR_TMU0, tmu0_addr, GR_MIPMAPLEVELMASK_BOTH, &tex_info_8x8);

    grTexCombine(GR_TMU0,
                 GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE,
                 GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE,
                 FXFALSE, FXFALSE);

    draw_textured_quad(50, 200, 100);

    /*
     * TEST 6: Same as test 5, but use TMU1 only
     * Should show RED (proves TMU1 is really being used)
     */
    LOG("Test 6: Swapped textures - RED on TMU1 (expect RED)");
    grTexSource(GR_TMU1, tmu1_addr, GR_MIPMAPLEVELMASK_BOTH, &tex_info_16x16);

    grTexCombine(GR_TMU1,
                 GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE,
                 GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE,
                 FXFALSE, FXFALSE);

    grTexCombine(GR_TMU0,
                 GR_COMBINE_FUNCTION_SCALE_OTHER, GR_COMBINE_FACTOR_ONE,
                 GR_COMBINE_FUNCTION_SCALE_OTHER, GR_COMBINE_FACTOR_ONE,
                 FXFALSE, FXFALSE);

    draw_textured_quad(200, 200, 100);

    grBufferSwap(1);

    LOG("");
    LOG("Expected results:");
    LOG("  Row 1: RED, BLUE, BLACK, MAGENTA");
    LOG("  Row 2: BLUE, RED");
    LOG("");
    LOG("Sleeping 10 seconds to view results...");
    Sleep(10000);

    grSstWinClose(ctx);
    grGlideShutdown();

    LOG("=== Test complete ===");
    return 0;
}
