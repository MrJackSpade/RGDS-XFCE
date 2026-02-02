/*
 * test_glide.c - Simple test program for software Glide3x DLL
 *
 * Tests basic functionality:
 * 1. Initialization
 * 2. Window creation
 * 3. Buffer clear
 * 4. Triangle drawing
 * 5. Buffer swap
 * 6. Shutdown
 */

#include <windows.h>
#include <stdio.h>
#include "../src/glide3x.h"

/* Simple logging */
#define LOG(fmt, ...) do { \
    char buf[256]; \
    snprintf(buf, sizeof(buf), fmt "\n", ##__VA_ARGS__); \
    OutputDebugStringA(buf); \
    printf("%s", buf); \
} while(0)

int main(int argc, char *argv[])
{
    GrContext_t ctx;
    GrVertex v1, v2, v3;
    int frame;

    (void)argc;
    (void)argv;

    LOG("=== Glide3x Test Program ===");

    /* Test 1: Get version before init */
    LOG("Test 1: grGlideGetVersion");
    {
        char version[80] = {0};
        grGlideGetVersion(version);
        LOG("  Version: %s", version);
    }

    /* Test 2: Initialize */
    LOG("Test 2: grGlideInit");
    grGlideInit();
    LOG("  Init complete");

    /* Test 3: Query hardware */
    LOG("Test 3: grSstQueryHardware");
    {
        GrHwConfiguration hwconfig;
        FxBool result = grSstQueryHardware(&hwconfig);
        LOG("  Result: %d, hwVersion: 0x%x, isV2: %d",
            result, hwconfig.hwVersion, hwconfig.isV2);
    }

    /* Test 4: Open window */
    LOG("Test 4: grSstWinOpen (640x480)");
    ctx = grSstWinOpen(
        0,                          /* hwnd (0 = create own) */
        GR_RESOLUTION_640x480,      /* resolution */
        GR_REFRESH_60Hz,            /* refresh */
        GR_COLORFORMAT_ARGB,        /* color format */
        GR_ORIGIN_UPPER_LEFT,       /* origin */
        2,                          /* num color buffers */
        1                           /* num aux buffers */
    );

    if (!ctx) {
        LOG("  FAILED: grSstWinOpen returned NULL");
        grGlideShutdown();
        return 1;
    }
    LOG("  Context: %p", ctx);

    /* Test 5: Query screen size */
    LOG("Test 5: grSstScreenWidth/Height");
    {
        float w = grSstScreenWidth();
        float h = grSstScreenHeight();
        LOG("  Screen: %.0f x %.0f", w, h);
    }

    /* Test 6: Set up rendering state */
    LOG("Test 6: Set rendering state");
    grColorCombine(
        GR_COMBINE_FUNCTION_LOCAL,
        GR_COMBINE_FACTOR_NONE,
        GR_COMBINE_LOCAL_ITERATED,
        GR_COMBINE_OTHER_NONE,
        FXFALSE
    );
    grAlphaCombine(
        GR_COMBINE_FUNCTION_LOCAL,
        GR_COMBINE_FACTOR_NONE,
        GR_COMBINE_LOCAL_ITERATED,
        GR_COMBINE_OTHER_NONE,
        FXFALSE
    );
    grDepthBufferMode(GR_CMP_ALWAYS);
    grDepthMask(FXFALSE);
    LOG("  State set");

    /* Test 7: Clear and draw */
    LOG("Test 7: Draw frames");

    for (frame = 0; frame < 60; frame++) {
        /* Clear to blue */
        grBufferClear(0x000040, 0, 0xFFFFFFFF);

        /* Set up a simple triangle */
        memset(&v1, 0, sizeof(v1));
        memset(&v2, 0, sizeof(v2));
        memset(&v3, 0, sizeof(v3));

        /* Red vertex at top */
        v1.x = 320.0f;
        v1.y = 100.0f;
        v1.oow = 1.0f;
        v1.r = 255.0f;
        v1.g = 0.0f;
        v1.b = 0.0f;
        v1.a = 255.0f;

        /* Green vertex at bottom-left */
        v2.x = 160.0f;
        v2.y = 380.0f;
        v2.oow = 1.0f;
        v2.r = 0.0f;
        v2.g = 255.0f;
        v2.b = 0.0f;
        v2.a = 255.0f;

        /* Blue vertex at bottom-right */
        v3.x = 480.0f;
        v3.y = 380.0f;
        v3.oow = 1.0f;
        v3.r = 0.0f;
        v3.g = 0.0f;
        v3.b = 255.0f;
        v3.a = 255.0f;

        /* Draw the triangle */
        grDrawTriangle(&v1, &v2, &v3);

        /* Swap buffers */
        grBufferSwap(1);

        /* Small delay */
        Sleep(16);

        if (frame == 0) {
            LOG("  Frame 0 complete");
        }
    }
    LOG("  Drew %d frames", frame);

    /* Test 8: Close window */
    LOG("Test 8: grSstWinClose");
    grSstWinClose(ctx);
    LOG("  Window closed");

    /* Test 9: Shutdown */
    LOG("Test 9: grGlideShutdown");
    grGlideShutdown();
    LOG("  Shutdown complete");

    LOG("=== All tests passed ===");
    return 0;
}
