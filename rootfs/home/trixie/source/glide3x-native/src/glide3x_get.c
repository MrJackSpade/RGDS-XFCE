/*
 * glide3x_get.c - State queries and function lookup
 *
 * This module implements state query and capability detection:
 *   - grGet(): Query numeric Glide state and capabilities
 *   - grGetString(): Query string Glide state
 *   - grGetProcAddress(): Look up function pointers by name
 *   - grSstQueryHardware(), grSstQueryBoards(): Hardware detection
 *   - grSstSelect(): Select active hardware
 *
 * STATE QUERIES (grGet):
 * Applications use grGet() to discover hardware capabilities and current
 * state. This allows adaptive quality settings:
 *
 *   - Texture memory: Adjust texture resolution based on available VRAM
 *   - Max texture size: Cap texture dimensions
 *   - Number of TMUs: Enable/disable multi-texture effects
 *   - Framebuffer memory: Determine available color buffers
 *
 * STRING QUERIES (grGetString):
 * Returns descriptive strings for:
 *   - GR_HARDWARE: Board model ("Voodoo2", "Voodoo Banshee", etc.)
 *   - GR_RENDERER: Renderer name
 *   - GR_VENDOR: Board manufacturer
 *   - GR_VERSION: Glide version string
 *   - GR_EXTENSION: Supported extensions
 *
 * FUNCTION LOOKUP (grGetProcAddress):
 * Glide 3.x added grGetProcAddress() for extension discovery.
 * This mirrors the OpenGL wglGetProcAddress() pattern:
 *   1. Call grGetProcAddress("grExtensionFunction")
 *   2. If non-NULL, extension is supported
 *   3. Call through returned function pointer
 *
 * This allows applications to use extension features when available
 * without hard dependencies.
 *
 * HARDWARE QUERIES:
 * grSstQueryHardware() and grSstQueryBoards() detect installed Voodoo
 * hardware. In our software implementation, we report a single
 * emulated Voodoo 2 board.
 *
 * CAPABILITY CONSTANTS:
 * Our implementation reports capabilities matching a well-equipped
 * Voodoo 2 board:
 *   - 4MB framebuffer
 *   - 2MB texture memory per TMU
 *   - 3 TMUs (for D2GL compatibility)
 *   - 256x256 max texture size
 *   - 16-bit depth buffer
 *   - RGB565 color format
 */

#include "glide3x_state.h"
#include <string.h>

/*
 * grGet - Query Glide state and capabilities
 *
 * From the 3dfx SDK:
 * "grGet() returns information about the current Glide state and the
 * capabilities of the graphics hardware."
 *
 * Parameters:
 *   pname   - Which value to query (GR_* constant)
 *   plength - Size of params buffer in bytes
 *   params  - Output buffer for result(s)
 *
 * Returns:
 *   Number of bytes written to params, or 0 on error.
 */
FxU32 __stdcall grGet(FxU32 pname, FxU32 plength, FxI32 *params)
{
    char dbg[128];
    snprintf(dbg, sizeof(dbg), "glide3x: grGet(pname=%u, plength=%u)\n", pname, plength);
    debug_log(dbg);

    if (!params || plength < 4) return 0;

    switch (pname) {
    /*
     * Hardware configuration
     */
    case GR_NUM_BOARDS:
        /* Number of Glide-compatible boards installed */
        *params = 1;
        return 4;

    case GR_NUM_FB:
        /* Number of color buffers (front + back) */
        *params = 2;
        return 4;

    case GR_NUM_TMU:
        /* Number of texture mapping units
         * Voodoo 1: 1 TMU, Voodoo 2: 2 TMUs
         * We report 3 for D2GL compatibility */
        *params = 3;
        return 4;

    /*
     * Memory configuration
     */
    case GR_MEMORY_FB:
        /* Framebuffer memory in bytes (4MB) */
        *params = 4 * 1024 * 1024;
        return 4;

    case GR_MEMORY_TMU:
        /* Texture memory per TMU in bytes (2MB) */
        *params = 2 * 1024 * 1024;
        return 4;

    case GR_MEMORY_UMA:
        /* Unified Memory Architecture (Banshee+)
         * 0 = separate FB/texture memory (Voodoo 1/2) */
        *params = 0;
        return 4;

    /*
     * Buffer configuration
     */
    case GR_NUM_SWAP_HISTORY_BUFFER:
        /* Number of swap buffers in history (for timing) */
        *params = 0;
        return 4;

    /*
     * Depth buffer configuration
     */
    case GR_BITS_DEPTH:
        /* Depth buffer bits (16-bit) */
        *params = 16;
        return 4;

    /*
     * Color format configuration
     */
    case GR_BITS_RGBA:
        /* Color component bits (RGB565: 5-6-5-0) */
        if (plength >= 16) {
            params[0] = 5;  /* Red bits */
            params[1] = 6;  /* Green bits */
            params[2] = 5;  /* Blue bits */
            params[3] = 0;  /* Alpha bits (none in 565) */
            return 16;
        }
        return 0;

    /*
     * Texture limits
     */
    case GR_MAX_TEXTURE_SIZE:
        /* Maximum texture dimension (256x256) */
        *params = 256;
        return 4;

    case GR_MAX_TEXTURE_ASPECT_RATIO:
        /* Maximum aspect ratio (8:1) as log2 */
        *params = 3;
        return 4;

    case GR_TEXTURE_ALIGN:
        /* Texture alignment requirement in bytes */
        *params = 256;
        return 4;

    /*
     * Gamma table configuration
     */
    case GR_GAMMA_TABLE_ENTRIES:
        /* Number of gamma table entries */
        *params = 256;
        return 4;

    case GR_BITS_GAMMA:
        /* Bits per gamma entry */
        *params = 8;
        return 4;

    default:
        snprintf(dbg, sizeof(dbg), "glide3x: grGet UNKNOWN pname=%u\n", pname);
        debug_log(dbg);
        *params = 0;
        return 4;
    }
}

/*
 * grGetString - Query string Glide state
 *
 * From the 3dfx SDK:
 * "grGetString() returns a string describing an aspect of the Glide
 * library or hardware."
 *
 * Parameters:
 *   pname - Which string to query (GR_* constant)
 *
 * Returns:
 *   Pointer to static string, or empty string if unknown.
 */
const char* __stdcall grGetString(FxU32 pname)
{
    char dbg[128];
    snprintf(dbg, sizeof(dbg), "glide3x: grGetString(pname=%u)\n", pname);
    debug_log(dbg);

    switch (pname) {
    case GR_EXTENSION:
        /* Space-separated extension list
         * Empty (space) indicates no extensions */
        return " ";

    case GR_HARDWARE:
        /* Hardware model name */
        return "Voodoo2";

    case GR_RENDERER:
        /* Renderer description */
        return "Glide3x Software";

    case GR_VENDOR:
        /* Hardware vendor (for compatibility with D2GL) */
        return "3Dfx Interactive";

    case GR_VERSION:
        /* Glide version string */
        return "3.1";

    default:
        return "";
    }
}

/*
 * Function pointer table for grGetProcAddress
 */
typedef void (*GrProc)(void);

static struct {
    const char *name;
    GrProc proc;
} g_proc_table[] = {
    /* Initialization */
    {"grGlideInit", (GrProc)grGlideInit},
    {"grGlideShutdown", (GrProc)grGlideShutdown},
    {"grGlideGetVersion", (GrProc)grGlideGetVersion},

    /* Context management */
    {"grSstWinOpen", (GrProc)grSstWinOpen},
    {"grSstWinClose", (GrProc)grSstWinClose},
    {"grSstQueryHardware", (GrProc)grSstQueryHardware},
    {"grSstQueryBoards", (GrProc)grSstQueryBoards},
    {"grSstSelect", (GrProc)grSstSelect},
    {"grSelectContext", (GrProc)grSelectContext},

    /* Buffer operations */
    {"grBufferClear", (GrProc)grBufferClear},
    {"grBufferSwap", (GrProc)grBufferSwap},
    {"grRenderBuffer", (GrProc)grRenderBuffer},

    /* Drawing */
    {"grDrawTriangle", (GrProc)grDrawTriangle},
    {"grDrawVertexArray", (GrProc)grDrawVertexArray},
    {"grDrawVertexArrayContiguous", (GrProc)grDrawVertexArrayContiguous},

    /* Combine */
    {"grColorCombine", (GrProc)grColorCombine},
    {"grAlphaCombine", (GrProc)grAlphaCombine},
    {"grConstantColorValue", (GrProc)grConstantColorValue},

    /* Blending */
    {"grAlphaBlendFunction", (GrProc)grAlphaBlendFunction},

    /* Alpha test */
    {"grAlphaTestFunction", (GrProc)grAlphaTestFunction},
    {"grAlphaTestReferenceValue", (GrProc)grAlphaTestReferenceValue},
    {"grColorMask", (GrProc)grColorMask},

    /* Depth buffer */
    {"grDepthBufferFunction", (GrProc)grDepthBufferFunction},
    {"grDepthBufferMode", (GrProc)grDepthBufferMode},
    {"grDepthMask", (GrProc)grDepthMask},
    {"grDepthBiasLevel", (GrProc)grDepthBiasLevel},

    /* Clipping */
    {"grClipWindow", (GrProc)grClipWindow},

    /* Texture */
    {"grTexSource", (GrProc)grTexSource},
    {"grTexDownloadMipMap", (GrProc)grTexDownloadMipMap},
    {"grTexFilterMode", (GrProc)grTexFilterMode},
    {"grTexClampMode", (GrProc)grTexClampMode},
    {"grTexCombine", (GrProc)grTexCombine},
    {"grTexMipMapMode", (GrProc)grTexMipMapMode},
    {"grTexLodBiasValue", (GrProc)grTexLodBiasValue},
    {"grTexMinAddress", (GrProc)grTexMinAddress},
    {"grTexMaxAddress", (GrProc)grTexMaxAddress},
    {"grTexTextureMemRequired", (GrProc)grTexTextureMemRequired},

    /* LFB */
    {"grLfbLock", (GrProc)grLfbLock},
    {"grLfbUnlock", (GrProc)grLfbUnlock},
    {"grLfbWriteRegion", (GrProc)grLfbWriteRegion},
    {"grLfbReadRegion", (GrProc)grLfbReadRegion},

    /* Fog */
    {"grFogMode", (GrProc)grFogMode},
    {"grFogColorValue", (GrProc)grFogColorValue},
    {"grFogTable", (GrProc)grFogTable},

    /* Misc */
    {"grSstOrigin", (GrProc)grSstOrigin},
    {"grCoordinateSpace", (GrProc)grCoordinateSpace},
    {"grVertexLayout", (GrProc)grVertexLayout},
    {"grGet", (GrProc)grGet},
    {"grGetString", (GrProc)grGetString},
    {"grFinish", (GrProc)grFinish},
    {"grFlush", (GrProc)grFlush},
    {"grSstScreenWidth", (GrProc)grSstScreenWidth},
    {"grSstScreenHeight", (GrProc)grSstScreenHeight},
    {"grDitherMode", (GrProc)grDitherMode},
    {"grChromakeyMode", (GrProc)grChromakeyMode},
    {"grChromakeyValue", (GrProc)grChromakeyValue},
    {"grCullMode", (GrProc)grCullMode},

    /* End marker */
    {NULL, NULL}
};

/*
 * grGetProcAddress - Look up function pointer by name
 *
 * From the 3dfx SDK:
 * "grGetProcAddress() returns the address of the specified Glide function."
 *
 * Parameters:
 *   procName - Name of function to look up
 *
 * Returns:
 *   Function pointer, or NULL if not found.
 *
 * This allows extensions and optional features to be discovered at runtime.
 */
GrProc __stdcall grGetProcAddress(char *procName)
{
    LOG_FUNC();
    if (!procName) return NULL;

    for (int i = 0; g_proc_table[i].name != NULL; i++) {
        if (strcmp(g_proc_table[i].name, procName) == 0) {
            return g_proc_table[i].proc;
        }
    }

    return NULL;
}

/*
 * grSstQueryHardware - Query hardware configuration
 *
 * From the 3dfx SDK:
 * "grSstQueryHardware() returns information about the Glide-compatible
 * hardware installed in the system."
 *
 * Parameters:
 *   hwconfig - Output structure filled with hardware info
 *
 * Returns:
 *   FXTRUE if hardware detected, FXFALSE otherwise.
 *
 * We report an emulated Voodoo 2 board.
 */
FxBool __stdcall grSstQueryHardware(GrHwConfiguration *hwconfig)
{
    LOG_FUNC();
    debug_log("glide3x: grSstQueryHardware called\n");

    if (!hwconfig) return FXFALSE;

    hwconfig->hwVersion = 0x0200;  /* Voodoo 2 */
    hwconfig->isV2 = FXTRUE;

    return FXTRUE;
}

/*
 * grSstQueryBoards - Query number of installed boards
 *
 * Returns:
 *   Number of Glide-compatible boards (always 1 for us).
 */
FxU32 __stdcall grSstQueryBoards(GrHwConfiguration *hwconfig)
{
    LOG_FUNC();
    debug_log("glide3x: grSstQueryBoards called\n");

    if (hwconfig) {
        grSstQueryHardware(hwconfig);
    }

    return 1;
}

/*
 * grSstSelect - Select active hardware board
 *
 * For multi-board configurations, selects which board subsequent
 * Glide calls operate on. We only support one board.
 */
void __stdcall grSstSelect(int which_sst)
{
    LOG_FUNC();
    (void)which_sst;
}
