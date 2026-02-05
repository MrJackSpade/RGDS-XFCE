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
 * Our implementation reports capabilities matching D2GL's values:
 *   - 4MB framebuffer
 *   - 16MB texture memory per TMU (matches D2GL)
 *   - 3 TMUs (for D2GL compatibility)
 *   - 1 framebuffer (matches D2GL)
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
static int g_grget_count = 0;

FxU32 __stdcall grGet(FxU32 pname, FxU32 plength, FxI32 *params)
{
    g_grget_count++;

    if (!params || plength < 4) {
        DEBUG_VERBOSE("grGet #%d: pname=0x%X FAILED (params=%p, plength=%d)\n",
                      g_grget_count, pname, params, plength);
        DEBUG_VERBOSE("grGet: returning 0\n");
        return 0;
    }

    /* ALWAYS log grGet calls - critical for debugging what the game checks */
    int should_log = 1;

    switch (pname) {
    /*
     * Hardware configuration
     */
    case GR_NUM_BOARDS:
        /* Number of Glide-compatible boards installed */
        *params = 1;
        if (should_log) DEBUG_VERBOSE("grGet #%d: GR_NUM_BOARDS -> %d\n", g_grget_count, *params);
        DEBUG_VERBOSE("grGet: returning 4\n");
        return 4;

    case GR_NUM_FB:
        /* Number of color buffers - D2GL returns 1 */
        *params = 1;
        if (should_log) DEBUG_VERBOSE("grGet #%d: GR_NUM_FB -> %d\n", g_grget_count, *params);
        DEBUG_VERBOSE("grGet: returning 4\n");
        return 4;

    case GR_NUM_TMU:
        /* Number of texture mapping units
         * Voodoo 1: 1 TMU, Voodoo 2: 2 TMUs
         * We report 3 for D2GL compatibility */
        *params = 3;
        if (should_log) DEBUG_VERBOSE("grGet #%d: GR_NUM_TMU -> %d\n", g_grget_count, *params);
        DEBUG_VERBOSE("grGet: returning 4\n");
        return 4;

    /*
     * Memory configuration
     */
    case GR_MEMORY_FB:
        /* Framebuffer memory in bytes (4MB) */
        *params = 4 * 1024 * 1024;
        if (should_log) DEBUG_VERBOSE("grGet #%d: GR_MEMORY_FB -> %d\n", g_grget_count, *params);
        DEBUG_VERBOSE("grGet: returning 4\n");
        return 4;

    case GR_MEMORY_TMU:
        /* Texture memory per TMU in bytes - D2GL uses 16MB */
        *params = 16 * 1024 * 1024;
        if (should_log) DEBUG_VERBOSE("grGet #%d: GR_MEMORY_TMU -> %d\n", g_grget_count, *params);
        DEBUG_VERBOSE("grGet: returning 4\n");
        return 4;

    case GR_MEMORY_UMA:
        /* Unified Memory Architecture (Banshee+)
         * 0 = separate FB/texture memory (Voodoo 1/2) */
        *params = 0;
        if (should_log) DEBUG_VERBOSE("grGet #%d: GR_MEMORY_UMA -> %d\n", g_grget_count, *params);
        DEBUG_VERBOSE("grGet: returning 4\n");
        return 4;

    /*
     * Buffer configuration
     */
    case GR_NUM_SWAP_HISTORY_BUFFER:
        /* Number of swap buffers in history (for timing) */
        *params = 0;
        DEBUG_VERBOSE("grGet: returning 4\n");
        return 4;

    /*
     * Depth buffer configuration
     */
    case GR_BITS_DEPTH:
        /* Depth buffer bits (16-bit) */
        *params = 16;
        DEBUG_VERBOSE("grGet: returning 4\n");
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
            DEBUG_VERBOSE("grGet: returning 16\n");
            return 16;
        }
        DEBUG_VERBOSE("grGet: returning 0\n");
        return 0;

    /*
     * Texture limits
     */
    case GR_MAX_TEXTURE_SIZE:
        /* Maximum texture dimension (256x256) */
        *params = 256;
        if (should_log) DEBUG_VERBOSE("grGet #%d: GR_MAX_TEXTURE_SIZE -> %d\n", g_grget_count, *params);
        DEBUG_VERBOSE("grGet: returning 4\n");
        return 4;

    case GR_MAX_TEXTURE_ASPECT_RATIO:
        /* Maximum aspect ratio (8:1) as log2 */
        *params = 3;
        if (should_log) DEBUG_VERBOSE("grGet #%d: GR_MAX_TEXTURE_ASPECT_RATIO -> %d\n", g_grget_count, *params);
        DEBUG_VERBOSE("grGet: returning 4\n");
        return 4;

    case GR_TEXTURE_ALIGN:
        /* Texture alignment requirement in bytes */
        *params = 256;
        if (should_log) DEBUG_VERBOSE("grGet #%d: GR_TEXTURE_ALIGN -> %d\n", g_grget_count, *params);
        DEBUG_VERBOSE("grGet: returning 4\n");
        return 4;

    /*
     * Gamma table configuration
     */
    case GR_GAMMA_TABLE_ENTRIES:
        /* Number of gamma table entries */
        *params = 256;
        DEBUG_VERBOSE("grGet: returning 4\n");
        return 4;

    case GR_BITS_GAMMA:
        /* Bits per gamma entry */
        *params = 8;
        DEBUG_VERBOSE("grGet: returning 4\n");
        return 4;

    default:
        *params = 0;
        DEBUG_VERBOSE("grGet #%d: UNKNOWN pname=0x%X -> 0\n", g_grget_count, pname);
        DEBUG_VERBOSE("grGet: returning 0\n");
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
static int g_grgetstring_count = 0;

const char* __stdcall grGetString(FxU32 pname)
{
    g_grgetstring_count++;
    const char *result;

    switch (pname) {
    case GR_EXTENSION:
        /* Space-separated extension list
         * Empty (space) indicates no extensions */
        result = " ";
        break;

    case GR_HARDWARE:
        /* Hardware model name */
        result = "Voodoo2";
        break;

    case GR_RENDERER:
        /* Renderer description */
        result = "Glide3x Software";
        break;

    case GR_VENDOR:
        /* Hardware vendor (for compatibility with D2GL) */
        result = "3Dfx Interactive";
        break;

    case GR_VERSION:
        /* Glide version string */
        result = "3.1";
        break;

    default:
        result = "";
        break;
    }

    DEBUG_VERBOSE("grGetString #%d: pname=0x%X -> \"%s\"\n",
                  g_grgetstring_count, pname, result);

    DEBUG_VERBOSE("grGetString: returning \"%s\"\n", result);
    return result;
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
static int g_grgetprocaddress_count = 0;

GrProc __stdcall grGetProcAddress(char *procName)
{
    g_grgetprocaddress_count++;

    if (!procName) {
        DEBUG_VERBOSE("grGetProcAddress #%d: NULL name\n", g_grgetprocaddress_count);
        DEBUG_VERBOSE("grGetProcAddress: returning NULL\n");
        return NULL;
    }

    /* ALWAYS log every lookup attempt - this is critical for debugging */
    for (int i = 0; g_proc_table[i].name != NULL; i++) {
        if (strcmp(g_proc_table[i].name, procName) == 0) {
            DEBUG_VERBOSE("grGetProcAddress #%d: \"%s\" -> FOUND (%p)\n",
                          g_grgetprocaddress_count, procName, g_proc_table[i].proc);
            DEBUG_VERBOSE("grGetProcAddress: returning %p\n", g_proc_table[i].proc);
            return g_proc_table[i].proc;
        }
    }

    /* Always log NOT FOUND - this could be why the game skips rendering */
    DEBUG_VERBOSE("grGetProcAddress #%d: \"%s\" -> *** NOT FOUND ***\n",
                  g_grgetprocaddress_count, procName);
    DEBUG_VERBOSE("grGetProcAddress: returning NULL\n");
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
    DEBUG_VERBOSE("grSstQueryHardware: called\n");
    if (!hwconfig) {
        DEBUG_VERBOSE("grSstQueryHardware: returning FXFALSE (null ptr)\n");
        return FXFALSE;
    }

    hwconfig->hwVersion = 0x0200;  /* Voodoo 2 */
    hwconfig->isV2 = FXTRUE;

    DEBUG_VERBOSE("grSstQueryHardware: returning FXTRUE\n");
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
    DEBUG_VERBOSE("grSstQueryBoards: called\n");
    if (hwconfig) {
        grSstQueryHardware(hwconfig);
    }

    DEBUG_VERBOSE("grSstQueryBoards: returning 1\n");
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
    DEBUG_VERBOSE("grSstSelect: which_sst=%d\n", which_sst);
    
    (void)which_sst;
    DEBUG_VERBOSE("grSstSelect: returning VOID\n");
}
