/*
 * glide3x_debug.c - Debug and diagnostic functions for testing
 *
 * These functions provide direct access to internal state for testing
 * and debugging purposes. They allow verification that texture data
 * was written correctly without going through the rendering pipeline.
 *
 * This file should be included in builds but these functions are
 * purely for diagnostic purposes.
 */

#include "glide3x_state.h"

/*
 * grDebugReadTexMemory - Read raw bytes from TMU texture memory
 *
 * @param tmu: TMU to read from (GR_TMU0 or GR_TMU1)
 * @param address: Starting address in TMU memory
 * @param size: Number of bytes to read
 * @param data: Buffer to receive data (must be at least 'size' bytes)
 *
 * @return: Number of bytes actually read (0 on error)
 *
 * This function provides direct access to TMU RAM, bypassing all
 * texture addressing logic. Use this to verify that grTexDownloadMipMap
 * actually wrote data to the expected location.
 */
FxU32 __stdcall grDebugReadTexMemory(GrChipID_t tmu, FxU32 address, FxU32 size, void *data)
{
    if (!g_voodoo || !data || size == 0) return 0;

    int t = (tmu == GR_TMU0) ? 0 : 1;
    tmu_state *ts = &g_voodoo->tmu[t];

    /* Clamp to available memory */
    uint32_t addr = address & ts->mask;
    uint32_t max_read = (ts->mask + 1) - addr;
    if (size > max_read) size = max_read;

    memcpy(data, &ts->ram[addr], size);
    return size;
}

/*
 * grDebugGetTexMemorySize - Get total TMU memory size
 *
 * @param tmu: TMU to query (GR_TMU0 or GR_TMU1)
 * @return: Total texture memory size in bytes
 */
FxU32 __stdcall grDebugGetTexMemorySize(GrChipID_t tmu)
{
    if (!g_voodoo) return 0;
    int t = (tmu == GR_TMU0) ? 0 : 1;
    return g_voodoo->tmu[t].mask + 1;
}

/*
 * grDebugDumpTexMemory - Dump TMU memory to a binary file
 *
 * @param tmu: TMU to dump (GR_TMU0 or GR_TMU1)
 * @param filename: Output filename
 *
 * @return: FXTRUE on success, FXFALSE on failure
 *
 * Dumps the entire TMU memory to a raw binary file for external analysis.
 * The file can be examined with a hex editor to find texture data.
 */
FxBool __stdcall grDebugDumpTexMemory(GrChipID_t tmu, const char *filename)
{
    if (!g_voodoo || !filename) return FXFALSE;

    int t = (tmu == GR_TMU0) ? 0 : 1;
    tmu_state *ts = &g_voodoo->tmu[t];

    FILE *f = fopen(filename, "wb");
    if (!f) return FXFALSE;

    fwrite(ts->ram, 1, ts->mask + 1, f);
    fclose(f);

    trap_log("grDebugDumpTexMemory: Dumped TMU%d (%u bytes) to %s\n",
             t, ts->mask + 1, filename);
    return FXTRUE;
}

/*
 * grDebugGetTexLodOffset - Get the computed LOD offset for a TMU
 *
 * @param tmu: TMU to query (GR_TMU0 or GR_TMU1)
 * @param lod: LOD level (0-8, where 0 is largest)
 * @return: Byte offset in TMU memory for that LOD
 *
 * This returns the internal lodoffset array value, which shows
 * where each mipmap level is expected to be found in memory.
 */
FxU32 __stdcall grDebugGetTexLodOffset(GrChipID_t tmu, int lod)
{
    if (!g_voodoo || lod < 0 || lod > 8) return 0;
    int t = (tmu == GR_TMU0) ? 0 : 1;
    return g_voodoo->tmu[t].lodoffset[lod];
}

/*
 * grDebugGetTexParams - Get current texture parameters
 *
 * @param tmu: TMU to query
 * @param params: Array of at least 8 FxU32 to receive:
 *                [0] = wmask (width-1)
 *                [1] = hmask (height-1)
 *                [2] = lodmin
 *                [3] = lodmax
 *                [4] = lodoffset[0] (base address)
 *                [5] = textureMode register
 *                [6] = tLOD register
 *                [7] = texBaseAddr register
 */
void __stdcall grDebugGetTexParams(GrChipID_t tmu, FxU32 *params)
{
    if (!g_voodoo || !params) return;

    int t = (tmu == GR_TMU0) ? 0 : 1;
    tmu_state *ts = &g_voodoo->tmu[t];

    params[0] = ts->wmask;
    params[1] = ts->hmask;
    params[2] = ts->lodmin;
    params[3] = ts->lodmax;
    params[4] = ts->lodoffset[0];
    params[5] = ts->reg[textureMode].u;
    params[6] = ts->reg[tLOD].u;
    params[7] = ts->reg[texBaseAddr].u;
}

/*
 * grDebugHexDump - Print a hex dump of memory to log
 *
 * @param label: Description string for the dump
 * @param data: Data to dump
 * @param size: Number of bytes to dump (capped at 256)
 */
void __stdcall grDebugHexDump(const char *label, const void *data, FxU32 size)
{
    if (!data || size == 0) return;
    if (size > 256) size = 256;

    const uint8_t *bytes = (const uint8_t *)data;

    trap_log("=== HEX DUMP: %s (%u bytes) ===\n", label ? label : "data", size);

    for (FxU32 i = 0; i < size; i += 16) {
        char line[80];
        int pos = snprintf(line, sizeof(line), "%04X: ", i);

        /* Hex bytes */
        for (int j = 0; j < 16 && i + j < size; j++) {
            pos += snprintf(line + pos, sizeof(line) - pos, "%02X ", bytes[i + j]);
        }
        /* Pad if less than 16 bytes */
        for (int j = size - i; j < 16; j++) {
            pos += snprintf(line + pos, sizeof(line) - pos, "   ");
        }

        /* ASCII representation */
        pos += snprintf(line + pos, sizeof(line) - pos, " |");
        for (int j = 0; j < 16 && i + j < size; j++) {
            uint8_t c = bytes[i + j];
            pos += snprintf(line + pos, sizeof(line) - pos, "%c",
                           (c >= 32 && c < 127) ? c : '.');
        }
        snprintf(line + pos, sizeof(line) - pos, "|");

        trap_log("%s\n", line);
    }
    trap_log("=== END HEX DUMP ===\n");
}
