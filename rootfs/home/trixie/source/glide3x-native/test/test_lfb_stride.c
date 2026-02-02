/*
 * test_lfb_stride.c - Test to reproduce Diablo 2 "ZARDBLIZ" LFB stride bug
 *
 * This test reproduces the exact sequence of Glide calls that Diablo 2 uses
 * on its title screen. The bug manifests as wrapped/garbled text when the
 * game writes 32-bit ARGB8888 pixels but the implementation returns a 16-bit stride.
 *
 * Expected behavior: Text "BLIZZARD" should appear correctly left-to-right
 * Bug behavior: Text wraps at 320 pixels showing "ZARDBLIZ" (split at midpoint)
 *
 * The root cause:
 *   - D2 calls grLfbLock with writeMode=GR_LFBWRITEMODE_8888 (32-bit)
 *   - Implementation returns stride=1280 (640 * 2 bytes for 16-bit)
 *   - D2 uses this stride to address pixels: ptr + y*1280 + x*4
 *   - At x=320, the offset is 320*4=1280, which wraps to the next row
 *   - Result: Right half of image appears on the left of the next row
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

#define WIDTH 640
#define HEIGHT 480

/*
 * Draw text pattern to visualize the stride issue
 * We draw "BLIZZARD" as a simple block pattern across the width
 * If stride is wrong, it will appear garbled
 */
static void draw_test_pattern_32bit(uint32_t *buffer, int stride_bytes, int width, int height)
{
    int stride_pixels = stride_bytes / 4;  /* Stride in 32-bit pixels */

    LOG("draw_test_pattern_32bit: stride_bytes=%d, stride_pixels=%d, width=%d",
        stride_bytes, stride_pixels, width);

    /* Clear to black */
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            buffer[y * stride_pixels + x] = 0xFF000000; /* Black, opaque */
        }
    }

    /* Draw colored bands across the full width to show wrapping
     * Band 1 (y=100-119): Red    at x=0-159
     * Band 2 (y=100-119): Green  at x=160-319
     * Band 3 (y=100-119): Blue   at x=320-479
     * Band 4 (y=100-119): Yellow at x=480-639
     *
     * If stride is correct: RGBY horizontal bands
     * If stride wraps at 320: RG on row 100, BY on row 101
     */
    for (int y = 100; y < 120; y++) {
        for (int x = 0; x < width; x++) {
            uint32_t color;
            if (x < 160)
                color = 0xFFFF0000; /* Red */
            else if (x < 320)
                color = 0xFF00FF00; /* Green */
            else if (x < 480)
                color = 0xFF0000FF; /* Blue */
            else
                color = 0xFFFFFF00; /* Yellow */

            buffer[y * stride_pixels + x] = color;
        }
    }

    /* Draw a diagonal line - if stride is wrong, it will appear broken */
    for (int i = 0; i < 200; i++) {
        int x = 200 + i;
        int y = 200 + i / 2;
        if (x < width && y < height) {
            buffer[y * stride_pixels + x] = 0xFFFFFFFF; /* White */
        }
    }

    /* Draw text-like pattern: "BLIZZARD" approximation
     * Each letter is 60 pixels wide, 8 letters = 480 pixels
     * Starting at x=80, ending at x=560
     */
    uint32_t letter_colors[8] = {
        0xFFFF0000, /* B - Red */
        0xFFFF7F00, /* L - Orange */
        0xFFFFFF00, /* I - Yellow */
        0xFF00FF00, /* Z - Green */
        0xFF00FFFF, /* Z - Cyan */
        0xFF0000FF, /* A - Blue */
        0xFF7F00FF, /* R - Purple */
        0xFFFF00FF, /* D - Magenta */
    };

    for (int letter = 0; letter < 8; letter++) {
        int start_x = 80 + letter * 60;
        for (int y = 220; y < 280; y++) {
            for (int x = start_x; x < start_x + 50; x++) {
                if (x < width) {
                    buffer[y * stride_pixels + x] = letter_colors[letter];
                }
            }
        }
    }

    /* Draw markers at specific X positions to verify alignment */
    for (int y = 50; y < 70; y++) {
        buffer[y * stride_pixels + 0] = 0xFFFFFFFF;     /* X=0: White */
        buffer[y * stride_pixels + 319] = 0xFFFFFFFF;   /* X=319: White */
        buffer[y * stride_pixels + 320] = 0xFFFF0000;   /* X=320: Red (midpoint) */
        buffer[y * stride_pixels + 639] = 0xFFFFFFFF;   /* X=639: White */
    }

    LOG("draw_test_pattern_32bit: Pattern drawn");
}

/*
 * Same test using 16-bit format for comparison
 */
static void draw_test_pattern_16bit(uint16_t *buffer, int stride_bytes, int width, int height)
{
    int stride_pixels = stride_bytes / 2;  /* Stride in 16-bit pixels */

    LOG("draw_test_pattern_16bit: stride_bytes=%d, stride_pixels=%d, width=%d",
        stride_bytes, stride_pixels, width);

    /* Clear to black */
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            buffer[y * stride_pixels + x] = 0x0000;
        }
    }

    /* Draw same colored bands in RGB565 */
    for (int y = 100; y < 120; y++) {
        for (int x = 0; x < width; x++) {
            uint16_t color;
            if (x < 160)
                color = 0xF800; /* Red */
            else if (x < 320)
                color = 0x07E0; /* Green */
            else if (x < 480)
                color = 0x001F; /* Blue */
            else
                color = 0xFFE0; /* Yellow */

            buffer[y * stride_pixels + x] = color;
        }
    }

    /* Draw diagonal line */
    for (int i = 0; i < 200; i++) {
        int x = 200 + i;
        int y = 200 + i / 2;
        if (x < width && y < height) {
            buffer[y * stride_pixels + x] = 0xFFFF; /* White */
        }
    }

    /* Draw "BLIZZARD" pattern */
    uint16_t letter_colors[8] = {
        0xF800, /* B - Red */
        0xFBE0, /* L - Orange */
        0xFFE0, /* I - Yellow */
        0x07E0, /* Z - Green */
        0x07FF, /* Z - Cyan */
        0x001F, /* A - Blue */
        0x781F, /* R - Purple */
        0xF81F, /* D - Magenta */
    };

    for (int letter = 0; letter < 8; letter++) {
        int start_x = 80 + letter * 60;
        for (int y = 220; y < 280; y++) {
            for (int x = start_x; x < start_x + 50; x++) {
                if (x < width) {
                    buffer[y * stride_pixels + x] = letter_colors[letter];
                }
            }
        }
    }

    /* Draw alignment markers */
    for (int y = 50; y < 70; y++) {
        buffer[y * stride_pixels + 0] = 0xFFFF;
        buffer[y * stride_pixels + 319] = 0xFFFF;
        buffer[y * stride_pixels + 320] = 0xF800; /* Red at midpoint */
        buffer[y * stride_pixels + 639] = 0xFFFF;
    }

    LOG("draw_test_pattern_16bit: Pattern drawn");
}

int main(int argc, char *argv[])
{
    GrContext_t ctx;
    GrLfbInfo_t lfb_info;
    int test_pass = 1;

    (void)argc;
    (void)argv;

    LOG("=== LFB Stride Test (Diablo 2 BLIZZARD bug reproduction) ===");
    LOG("");
    LOG("This test verifies that grLfbLock returns correct stride for different write modes.");
    LOG("The bug: When writeMode=8888 (32-bit), stride should be width*4, not width*2");
    LOG("");

    /* Initialize Glide */
    LOG("Initializing Glide...");
    grGlideInit();

    /* Open window at 640x480 (same as D2 title screen) */
    LOG("Opening 640x480 window...");
    grSstSelect(0);
    ctx = grSstWinOpen(
        0,
        GR_RESOLUTION_640x480,
        GR_REFRESH_60Hz,
        GR_COLORFORMAT_ARGB,
        GR_ORIGIN_UPPER_LEFT,
        2, 1
    );

    if (!ctx) {
        LOG("FAILED: grSstWinOpen returned NULL");
        grGlideShutdown();
        return 1;
    }
    LOG("Context opened: %p", ctx);

    /* Clear buffer first */
    grBufferClear(0x00000000, 0, 0xFFFF);
    grBufferSwap(1);

    /*
     * TEST 1: 16-bit LFB write (should work correctly)
     */
    LOG("");
    LOG("=== TEST 1: grLfbLock with writeMode=565 (16-bit) ===");

    memset(&lfb_info, 0, sizeof(lfb_info));
    lfb_info.size = sizeof(lfb_info);

    if (grLfbLock(GR_LFB_WRITE_ONLY, GR_BUFFER_FRONTBUFFER,
                  GR_LFBWRITEMODE_565, GR_ORIGIN_UPPER_LEFT,
                  FXFALSE, &lfb_info)) {

        LOG("  lfbPtr:        %p", lfb_info.lfbPtr);
        LOG("  strideInBytes: %d", lfb_info.strideInBytes);
        LOG("  writeMode:     %d", lfb_info.writeMode);
        LOG("  Expected stride for 16-bit: %d", WIDTH * 2);

        if (lfb_info.strideInBytes == WIDTH * 2) {
            LOG("  PASS: Stride is correct for 16-bit mode");
        } else {
            LOG("  FAIL: Stride mismatch! Got %d, expected %d",
                lfb_info.strideInBytes, WIDTH * 2);
            test_pass = 0;
        }

        /* Draw test pattern */
        draw_test_pattern_16bit((uint16_t*)lfb_info.lfbPtr,
                                lfb_info.strideInBytes, WIDTH, HEIGHT);

        grLfbUnlock(GR_LFB_WRITE_ONLY, GR_BUFFER_FRONTBUFFER);
    } else {
        LOG("  FAIL: grLfbLock failed");
        test_pass = 0;
    }

    LOG("");
    LOG("16-bit test complete. Displaying for 5 seconds...");
    Sleep(5000);

    /*
     * TEST 2: 32-bit LFB write (this is where the bug occurs)
     * D2 uses this mode for title screen rendering
     */
    LOG("");
    LOG("=== TEST 2: grLfbLock with writeMode=8888 (32-bit) - THE BUG TEST ===");

    memset(&lfb_info, 0, sizeof(lfb_info));
    lfb_info.size = sizeof(lfb_info);

    if (grLfbLock(GR_LFB_WRITE_ONLY, GR_BUFFER_FRONTBUFFER,
                  GR_LFBWRITEMODE_8888, GR_ORIGIN_UPPER_LEFT,
                  FXFALSE, &lfb_info)) {

        LOG("  lfbPtr:        %p", lfb_info.lfbPtr);
        LOG("  strideInBytes: %d", lfb_info.strideInBytes);
        LOG("  writeMode:     %d", lfb_info.writeMode);
        LOG("  Expected stride for 32-bit: %d", WIDTH * 4);
        LOG("  Buggy stride (16-bit):      %d", WIDTH * 2);

        if (lfb_info.strideInBytes == WIDTH * 4) {
            LOG("  PASS: Stride is correct for 32-bit mode");
        } else if (lfb_info.strideInBytes == WIDTH * 2) {
            LOG("  FAIL: Stride is 16-bit! This causes the ZARDBLIZ bug");
            LOG("        When writing 32-bit pixels with 16-bit stride:");
            LOG("        - Each row only holds %d pixels (640 expected)",
                lfb_info.strideInBytes / 4);
            LOG("        - Content after x=%d wraps to next row",
                lfb_info.strideInBytes / 4);
            test_pass = 0;
        } else {
            LOG("  FAIL: Unexpected stride %d", lfb_info.strideInBytes);
            test_pass = 0;
        }

        /* Draw test pattern - this will show the bug visually */
        LOG("");
        LOG("Drawing 32-bit test pattern...");
        LOG("If stride is wrong, the colored bars will wrap at x=320");
        LOG("Correct:  [RED][GREEN][BLUE][YELLOW] on one row");
        LOG("Buggy:    [RED][GREEN] on row 100, [BLUE][YELLOW] on row 101");

        draw_test_pattern_32bit((uint32_t*)lfb_info.lfbPtr,
                                lfb_info.strideInBytes, WIDTH, HEIGHT);

        grLfbUnlock(GR_LFB_WRITE_ONLY, GR_BUFFER_FRONTBUFFER);
    } else {
        LOG("  FAIL: grLfbLock failed");
        test_pass = 0;
    }

    LOG("");
    LOG("=== 32-BIT TEST RESULT (THE BUG) ===");
    LOG("If you see colored bands WRAPPED (split across rows), the bug is confirmed.");
    LOG("Displaying for 10 seconds...");
    Sleep(10000);

    /*
     * TEST 3: Verify stride for all write modes (no visual, just log)
     */
    LOG("");
    LOG("=== TEST 3: Stride verification for all write modes ===");

    struct {
        GrLfbWriteMode_t mode;
        const char *name;
        int expected_bpp;
    } modes[] = {
        { GR_LFBWRITEMODE_565,  "565 (RGB565)",    2 },
        { GR_LFBWRITEMODE_555,  "555 (RGB555)",    2 },
        { GR_LFBWRITEMODE_1555, "1555 (ARGB1555)", 2 },
        { GR_LFBWRITEMODE_888,  "888 (RGB888)",    3 },
        { GR_LFBWRITEMODE_8888, "8888 (ARGB8888)", 4 },
    };

    for (size_t i = 0; i < sizeof(modes)/sizeof(modes[0]); i++) {
        memset(&lfb_info, 0, sizeof(lfb_info));
        lfb_info.size = sizeof(lfb_info);

        if (grLfbLock(GR_LFB_WRITE_ONLY, GR_BUFFER_BACKBUFFER,
                      modes[i].mode, GR_ORIGIN_UPPER_LEFT,
                      FXFALSE, &lfb_info)) {

            int expected_stride = WIDTH * modes[i].expected_bpp;
            LOG("  Mode %-20s: stride=%4d (expected %4d) %s",
                modes[i].name,
                lfb_info.strideInBytes,
                expected_stride,
                (lfb_info.strideInBytes == expected_stride) ? "PASS" : "FAIL");

            if (lfb_info.strideInBytes != expected_stride) {
                test_pass = 0;
            }

            grLfbUnlock(GR_LFB_WRITE_ONLY, GR_BUFFER_BACKBUFFER);
        } else {
            LOG("  Mode %-20s: FAIL (lock failed)", modes[i].name);
            test_pass = 0;
        }
    }

    /* Cleanup */
    LOG("");
    LOG("=== TEST RESULT: %s ===", test_pass ? "ALL PASSED" : "FAILED");

    if (!test_pass) {
        LOG("");
        LOG("To fix the ZARDBLIZ bug:");
        LOG("1. grLfbLock must return stride based on writeMode, not internal format");
        LOG("2. For 32-bit modes, allocate a shadow buffer");
        LOG("3. On grLfbUnlock, convert shadow buffer to 16-bit framebuffer");
    }

    LOG("");
    LOG("Waiting 3 seconds before shutdown...");
    Sleep(3000);

    LOG("Shutting down...");
    grSstWinClose(ctx);
    grGlideShutdown();

    return test_pass ? 0 : 1;
}
