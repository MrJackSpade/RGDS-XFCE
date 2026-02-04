/*
 * test_texture_memory.c - Rigorous texture memory write/read verification
 *
 * This test verifies that texture data written via grTexDownloadMipMap
 * is correctly stored in TMU memory and can be read back byte-for-byte.
 *
 * Test strategy:
 * 1. Write a known pattern (solid 0x80 = 128) to texture memory
 * 2. Read it back directly from TMU RAM
 * 3. Compare byte-by-byte
 * 4. If mismatch found, dump the entire TMU memory for analysis
 *
 * This isolates whether the problem is in WRITE or READ operations.
 */

#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "../src/glide3x.h"

/* Debug function prototypes (from glide3x_debug.c) - declared as stdcall */
FxU32 __stdcall grDebugReadTexMemory(GrChipID_t tmu, FxU32 address, FxU32 size, void *data);
FxU32 __stdcall grDebugGetTexMemorySize(GrChipID_t tmu);
FxBool __stdcall grDebugDumpTexMemory(GrChipID_t tmu, const char *filename);
FxU32 __stdcall grDebugGetTexLodOffset(GrChipID_t tmu, int lod);
void __stdcall grDebugGetTexParams(GrChipID_t tmu, FxU32 *params);
void __stdcall grDebugHexDump(const char *label, const void *data, FxU32 size);

#define LOG(fmt, ...) do { \
    char buf[512]; \
    snprintf(buf, sizeof(buf), fmt "\n", ##__VA_ARGS__); \
    OutputDebugStringA(buf); \
    printf("%s", buf); \
} while(0)

#define TEST_TEX_SIZE 16  /* Small texture for easy verification */
#define TEST_PATTERN 0x80 /* Solid 128 - easy to spot in hex dump */

/*
 * Print hex dump of data to stdout
 */
static void hex_dump(const char *label, const uint8_t *data, size_t size)
{
    printf("\n=== %s (%zu bytes) ===\n", label, size);
    for (size_t i = 0; i < size; i += 16) {
        printf("%04zX: ", i);
        for (size_t j = 0; j < 16 && i + j < size; j++) {
            printf("%02X ", data[i + j]);
        }
        printf("\n");
    }
    printf("=== END ===\n\n");
}

/*
 * Test 1: Simple byte pattern write/read
 * Write a solid 0x80 pattern and immediately read it back
 */
static int test_byte_pattern_roundtrip(void)
{
    LOG("=== TEST 1: Byte Pattern Roundtrip ===");

    /* Allocate texture data - 8-bit format (1 byte per texel) */
    int tex_size = TEST_TEX_SIZE;
    int tex_bytes = tex_size * tex_size;
    uint8_t *write_data = (uint8_t *)malloc(tex_bytes);
    uint8_t *read_data = (uint8_t *)malloc(tex_bytes);

    if (!write_data || !read_data) {
        LOG("FAILED: Memory allocation");
        free(write_data);
        free(read_data);
        return 1;
    }

    /* Fill with solid pattern */
    memset(write_data, TEST_PATTERN, tex_bytes);

    LOG("Write buffer (first 64 bytes):");
    hex_dump("WRITE DATA", write_data, tex_bytes < 64 ? tex_bytes : 64);

    /* Set up texture info - use INTENSITY_8 (8-bit grayscale) */
    GrTexInfo info;
    memset(&info, 0, sizeof(info));
    info.smallLodLog2 = GR_LOD_LOG2_16;  /* 16x16 */
    info.largeLodLog2 = GR_LOD_LOG2_16;
    info.aspectRatioLog2 = GR_ASPECT_LOG2_1x1;
    info.format = GR_TEXFMT_INTENSITY_8;
    info.data = write_data;

    /* Calculate expected size */
    FxU32 expected_size = grTexTextureMemRequired(GR_MIPMAPLEVELMASK_BOTH, &info);
    LOG("Texture: %dx%d, format=INTENSITY_8, expected size=%u bytes",
        tex_size, tex_size, expected_size);

    /* Download to TMU at address 0 */
    FxU32 tex_addr = grTexMinAddress(GR_TMU0);
    LOG("Downloading to TMU0 at address 0x%08X", tex_addr);

    grTexDownloadMipMap(GR_TMU0, tex_addr, GR_MIPMAPLEVELMASK_BOTH, &info);

    /* Immediately read back from the same address */
    LOG("Reading back from TMU0 at address 0x%08X (%d bytes)", tex_addr, tex_bytes);
    FxU32 bytes_read = grDebugReadTexMemory(GR_TMU0, tex_addr, tex_bytes, read_data);

    LOG("Bytes read: %u", bytes_read);
    LOG("Read buffer (first 64 bytes):");
    hex_dump("READ DATA", read_data, tex_bytes < 64 ? tex_bytes : 64);

    /* Compare byte-by-byte */
    int mismatches = 0;
    int first_mismatch = -1;
    for (int i = 0; i < tex_bytes; i++) {
        if (write_data[i] != read_data[i]) {
            if (first_mismatch == -1) {
                first_mismatch = i;
            }
            mismatches++;
            if (mismatches <= 10) {
                LOG("MISMATCH at offset %d: wrote 0x%02X, read 0x%02X",
                    i, write_data[i], read_data[i]);
            }
        }
    }

    if (mismatches > 0) {
        LOG("FAILED: %d mismatches found (first at offset %d)", mismatches, first_mismatch);

        /* Dump full TMU memory for analysis */
        LOG("Dumping full TMU0 memory to C:\\tmu0_dump.bin");
        grDebugDumpTexMemory(GR_TMU0, "C:\\tmu0_dump.bin");

        /* Also get texture params */
        FxU32 params[8];
        grDebugGetTexParams(GR_TMU0, params);
        LOG("Texture params:");
        LOG("  wmask=0x%X, hmask=0x%X", params[0], params[1]);
        LOG("  lodmin=%d, lodmax=%d", params[2], params[3]);
        LOG("  lodoffset[0]=0x%X", params[4]);
        LOG("  textureMode=0x%08X", params[5]);
        LOG("  tLOD=0x%08X", params[6]);
        LOG("  texBaseAddr=0x%08X", params[7]);

        free(write_data);
        free(read_data);
        return 1;
    }

    LOG("PASSED: All %d bytes match", tex_bytes);

    free(write_data);
    free(read_data);
    return 0;
}

/*
 * Test 2: 16-bit RGB565 pattern write/read
 */
static int test_rgb565_pattern_roundtrip(void)
{
    LOG("\n=== TEST 2: RGB565 Pattern Roundtrip ===");

    int tex_size = TEST_TEX_SIZE;
    int tex_bytes = tex_size * tex_size * 2;  /* 2 bytes per pixel */
    uint16_t *write_data = (uint16_t *)malloc(tex_bytes);
    uint16_t *read_data = (uint16_t *)malloc(tex_bytes);

    if (!write_data || !read_data) {
        LOG("FAILED: Memory allocation");
        free(write_data);
        free(read_data);
        return 1;
    }

    /* Fill with a recognizable pattern: 0x8080 (each pixel is 0x8080) */
    for (int i = 0; i < tex_size * tex_size; i++) {
        write_data[i] = 0x8080;
    }

    LOG("Write buffer (first 32 bytes):");
    hex_dump("WRITE DATA", (uint8_t*)write_data, 32);

    /* Set up texture info - RGB565 */
    GrTexInfo info;
    memset(&info, 0, sizeof(info));
    info.smallLodLog2 = GR_LOD_LOG2_16;
    info.largeLodLog2 = GR_LOD_LOG2_16;
    info.aspectRatioLog2 = GR_ASPECT_LOG2_1x1;
    info.format = GR_TEXFMT_RGB_565;
    info.data = write_data;

    FxU32 expected_size = grTexTextureMemRequired(GR_MIPMAPLEVELMASK_BOTH, &info);
    LOG("Texture: %dx%d, format=RGB_565, expected size=%u bytes",
        tex_size, tex_size, expected_size);

    /* Download to TMU at address 0x1000 (offset to not conflict with test 1) */
    FxU32 tex_addr = 0x1000;
    LOG("Downloading to TMU0 at address 0x%08X", tex_addr);

    grTexDownloadMipMap(GR_TMU0, tex_addr, GR_MIPMAPLEVELMASK_BOTH, &info);

    /* Read back */
    LOG("Reading back from TMU0 at address 0x%08X (%d bytes)", tex_addr, tex_bytes);
    FxU32 bytes_read = grDebugReadTexMemory(GR_TMU0, tex_addr, tex_bytes, read_data);

    LOG("Bytes read: %u", bytes_read);
    LOG("Read buffer (first 32 bytes):");
    hex_dump("READ DATA", (uint8_t*)read_data, 32);

    /* Compare */
    int mismatches = 0;
    for (int i = 0; i < tex_size * tex_size; i++) {
        if (write_data[i] != read_data[i]) {
            mismatches++;
            if (mismatches <= 10) {
                LOG("MISMATCH at pixel %d: wrote 0x%04X, read 0x%04X",
                    i, write_data[i], read_data[i]);
            }
        }
    }

    if (mismatches > 0) {
        LOG("FAILED: %d mismatches found", mismatches);
        free(write_data);
        free(read_data);
        return 1;
    }

    LOG("PASSED: All %d pixels match", tex_size * tex_size);

    free(write_data);
    free(read_data);
    return 0;
}

/*
 * Test 3: Scan entire TMU memory to find the pattern
 * If tests fail, this helps locate where data actually went
 */
static int test_scan_for_pattern(void)
{
    LOG("\n=== TEST 3: Scan TMU Memory for Pattern ===");

    FxU32 tmu_size = grDebugGetTexMemorySize(GR_TMU0);
    LOG("TMU0 memory size: %u bytes (0x%X)", tmu_size, tmu_size);

    /* Read entire TMU memory */
    uint8_t *tmu_data = (uint8_t *)malloc(tmu_size);
    if (!tmu_data) {
        LOG("FAILED: Cannot allocate %u bytes for TMU dump", tmu_size);
        return 1;
    }

    FxU32 bytes_read = grDebugReadTexMemory(GR_TMU0, 0, tmu_size, tmu_data);
    LOG("Read %u bytes from TMU0", bytes_read);

    /* Scan for runs of 0x80 (our test pattern) */
    LOG("Scanning for runs of 0x80...");
    int in_run = 0;
    int run_start = 0;
    int runs_found = 0;

    for (FxU32 i = 0; i < bytes_read; i++) {
        if (tmu_data[i] == TEST_PATTERN) {
            if (!in_run) {
                run_start = i;
                in_run = 1;
            }
        } else {
            if (in_run) {
                int run_length = i - run_start;
                if (run_length >= 16) {  /* Only report runs of 16+ bytes */
                    LOG("  Found run of 0x%02X at offset 0x%04X, length=%d bytes",
                        TEST_PATTERN, run_start, run_length);
                    runs_found++;
                }
                in_run = 0;
            }
        }
    }

    /* Check if we ended in a run */
    if (in_run) {
        int run_length = bytes_read - run_start;
        if (run_length >= 16) {
            LOG("  Found run of 0x%02X at offset 0x%04X, length=%d bytes",
                TEST_PATTERN, run_start, run_length);
            runs_found++;
        }
    }

    if (runs_found == 0) {
        LOG("WARNING: No significant runs of 0x%02X found in TMU memory!", TEST_PATTERN);
        LOG("Dumping first 512 bytes of TMU memory:");
        hex_dump("TMU0 START", tmu_data, 512);
    } else {
        LOG("Found %d run(s) of pattern 0x%02X", runs_found, TEST_PATTERN);
    }

    /* Also scan for 0x8080 (16-bit pattern) */
    LOG("\nScanning for 0x8080 word pattern...");
    runs_found = 0;
    in_run = 0;

    for (FxU32 i = 0; i < bytes_read - 1; i += 2) {
        uint16_t word = tmu_data[i] | (tmu_data[i+1] << 8);
        if (word == 0x8080) {
            if (!in_run) {
                run_start = i;
                in_run = 1;
            }
        } else {
            if (in_run) {
                int run_length = i - run_start;
                if (run_length >= 32) {
                    LOG("  Found run of 0x8080 at offset 0x%04X, length=%d bytes",
                        run_start, run_length);
                    runs_found++;
                }
                in_run = 0;
            }
        }
    }

    if (in_run) {
        int run_length = bytes_read - run_start;
        if (run_length >= 32) {
            LOG("  Found run of 0x8080 at offset 0x%04X, length=%d bytes",
                run_start, run_length);
            runs_found++;
        }
    }

    if (runs_found == 0) {
        LOG("WARNING: No significant runs of 0x8080 found in TMU memory!");
    }

    free(tmu_data);
    return 0;
}

/*
 * Test 4: Replicate the actual rendering path
 *
 * This is the critical test - it does exactly what a game does:
 * 1. Upload a 64x64 texture (like Diablo 2)
 * 2. Call grTexSource
 * 3. Check what the pipeline will actually read from (lodoffset[ilod])
 * 4. Verify if texture data is there or zeros (black)
 */
static int test_pipeline_read_path(void)
{
    LOG("\n=== TEST 4: Pipeline Read Path (The Real Bug) ===");

    /* Create a 64x64 texture filled with recognizable pattern */
    int tex_size = 64;
    int tex_bytes = tex_size * tex_size * 2;
    uint16_t *tex_data = (uint16_t *)malloc(tex_bytes);
    if (!tex_data) {
        LOG("FAILED: Memory allocation");
        return 1;
    }

    /* Fill with solid 0xAAAA pattern - easy to spot */
    for (int i = 0; i < tex_size * tex_size; i++) {
        tex_data[i] = 0xAAAA;
    }

    GrTexInfo info;
    memset(&info, 0, sizeof(info));
    info.smallLodLog2 = GR_LOD_LOG2_64;  /* 64x64 texture */
    info.largeLodLog2 = GR_LOD_LOG2_64;
    info.aspectRatioLog2 = GR_ASPECT_LOG2_1x1;
    info.format = GR_TEXFMT_RGB_565;
    info.data = tex_data;

    FxU32 tex_addr = 0x0;  /* Upload at address 0, like games do */

    /* Step 1: Download texture */
    LOG("Step 1: Download 64x64 RGB565 texture to address 0x%X", tex_addr);
    LOG("        Texture filled with 0xAAAA pattern");
    grTexDownloadMipMap(GR_TMU0, tex_addr, GR_MIPMAPLEVELMASK_BOTH, &info);

    /* Verify write succeeded */
    uint16_t verify[4];
    grDebugReadTexMemory(GR_TMU0, tex_addr, 8, verify);
    LOG("        Verify at 0x%X: %04X %04X %04X %04X",
        tex_addr, verify[0], verify[1], verify[2], verify[3]);

    /* Step 2: Set texture source (this triggers recompute_texture_params) */
    LOG("Step 2: Call grTexSource");
    grTexSource(GR_TMU0, tex_addr, GR_MIPMAPLEVELMASK_BOTH, &info);

    /* Step 3: Get the computed parameters */
    FxU32 params[8];
    grDebugGetTexParams(GR_TMU0, params);

    FxU32 wmask = params[0];
    FxU32 hmask = params[1];
    FxU32 lodmin = params[2];
    FxU32 lodmax = params[3];
    FxU32 lodoffset0 = params[4];

    LOG("Step 3: Check computed parameters");
    LOG("        wmask = 0x%X, hmask = 0x%X", wmask, hmask);
    LOG("        lodmin = %d, lodmax = %d", lodmin, lodmax);
    LOG("        lodoffset[0] = 0x%X", lodoffset0);

    /* Step 4: Calculate ilod - this is what the pipeline does */
    int ilod = lodmin >> 8;
    LOG("Step 4: Calculate ilod = lodmin >> 8 = %d >> 8 = %d", lodmin, ilod);

    /* Step 5: Get lodoffset[ilod] - where pipeline will actually read */
    FxU32 actual_read_addr = grDebugGetTexLodOffset(GR_TMU0, ilod);
    LOG("Step 5: Pipeline will read from lodoffset[%d] = 0x%X", ilod, actual_read_addr);

    /* Step 6: Check what's at that address */
    LOG("Step 6: Read from pipeline's actual read address (0x%X)", actual_read_addr);
    uint16_t pipeline_data[4];
    grDebugReadTexMemory(GR_TMU0, actual_read_addr, 8, pipeline_data);
    LOG("        Data at 0x%X: %04X %04X %04X %04X",
        actual_read_addr, pipeline_data[0], pipeline_data[1], pipeline_data[2], pipeline_data[3]);

    /* Step 7: Analyze the result - only check what matters for rendering */
    LOG("");
    LOG("=== ANALYSIS ===");
    LOG("Texture uploaded to: 0x%X", tex_addr);
    LOG("Pipeline reads from: 0x%X (lodoffset[%d])", actual_read_addr, ilod);

    int is_bug = 0;

    /* The ONLY thing that matters: does the pipeline read the correct data? */
    if (pipeline_data[0] != 0xAAAA) {
        LOG("FAILED: Pipeline reads WRONG data!");
        LOG("  Expected 0xAAAA (our texture)");
        LOG("  Got 0x%04X (probably zeros = BLACK)", pipeline_data[0]);
        LOG("");
        LOG("  Texture was uploaded to: 0x%X", tex_addr);
        LOG("  Pipeline reads from: 0x%X (lodoffset[%d])", actual_read_addr, ilod);
        if (actual_read_addr != tex_addr) {
            LOG("  Address mismatch: pipeline reads %d bytes away from texture!",
                (int)(actual_read_addr - tex_addr));
        }
        is_bug = 1;
    } else {
        LOG("Pipeline reads CORRECT data (0xAAAA) from address 0x%X", actual_read_addr);
    }

    free(tex_data);

    if (is_bug) {
        LOG("");
        LOG("ROOT CAUSE: ilod=%d causes read from lodoffset[%d]=0x%X", ilod, ilod, actual_read_addr);
        LOG("            but texture data is at 0x%X", tex_addr);
        LOG("FAILED");
        return 1;
    }

    LOG("PASSED: Pipeline reads from correct address");
    return 0;
}

int main(int argc, char *argv[])
{
    GrContext_t ctx;
    int failures = 0;

    (void)argc;
    (void)argv;

    LOG("=======================================================");
    LOG("  TEXTURE MEMORY VERIFICATION TEST");
    LOG("=======================================================");
    LOG("");

    /* Initialize Glide */
    LOG("Initializing Glide...");
    grGlideInit();
    grSstSelect(0);

    /* Open context */
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
        LOG("FAILED: Could not open Glide context");
        grGlideShutdown();
        return 1;
    }

    LOG("Glide initialized, context=%p", ctx);
    LOG("TMU0 memory: min=0x%X, max=0x%X",
        grTexMinAddress(GR_TMU0), grTexMaxAddress(GR_TMU0));
    LOG("");

    /* Run tests */
    failures += test_byte_pattern_roundtrip();
    failures += test_rgb565_pattern_roundtrip();
    failures += test_scan_for_pattern();
    failures += test_pipeline_read_path();

    LOG("\n=======================================================");
    if (failures == 0) {
        LOG("  ALL TESTS PASSED");
    } else {
        LOG("  %d TEST(S) FAILED", failures);

        /* On failure, dump full TMU memory */
        LOG("\nDumping TMU0 memory to C:\\tmu0_full_dump.bin for analysis");
        grDebugDumpTexMemory(GR_TMU0, "C:\\tmu0_full_dump.bin");
    }
    LOG("=======================================================");

    /* Cleanup */
    grSstWinClose(ctx);
    grGlideShutdown();

    return failures;
}
