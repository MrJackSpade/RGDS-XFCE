/*
 * glide3x.h - Glide 3.x API definitions
 *
 * Based on 3dfx Glide SDK headers
 * This is the public API that games like Diablo 2 expect
 *
 * =============================================================================
 * GLIDE API OVERVIEW
 * =============================================================================
 *
 * Glide is 3dfx Interactive's low-level graphics API designed specifically for
 * their Voodoo line of 3D accelerators (Voodoo 1, Voodoo 2, Voodoo Banshee, etc.).
 * Unlike OpenGL or Direct3D, Glide provides direct hardware access with minimal
 * driver overhead, which made it extremely popular for games in the late 1990s.
 *
 * KEY ARCHITECTURAL CONCEPTS:
 *
 * 1. VOODOO HARDWARE ARCHITECTURE
 *    The Voodoo consists of several key components:
 *    - FBI (Frame Buffer Interface): Handles the framebuffer, depth buffer,
 *      color combining, alpha blending, and final pixel output
 *    - TMU (Texture Mapping Unit): Up to 3 TMUs handle texture mapping,
 *      filtering, and texture combining. Each TMU has its own texture memory.
 *    - DAC (Digital-to-Analog Converter): Converts digital framebuffer to
 *      analog video signal
 *
 * 2. RENDERING PIPELINE
 *    Glide uses a fixed-function pipeline:
 *    a) Vertex Processing: Application provides screen-space vertices with
 *       pre-computed 1/W values for perspective correction
 *    b) Triangle Setup: Hardware computes edge equations and gradients
 *    c) Rasterization: Scanline conversion with parameter interpolation
 *    d) Texture Mapping: TMUs fetch and filter texels
 *    e) Color Combine: FBI combines texture color with vertex color
 *    f) Alpha/Depth Test: Conditional pixel rejection
 *    g) Alpha Blend: Blending with existing framebuffer contents
 *    h) Framebuffer Write: Final pixel output
 *
 * 3. COORDINATE SYSTEM
 *    Glide 3.x works primarily in screen coordinates (post-projection).
 *    The application is responsible for transforming vertices from world
 *    space to screen space. Vertices include:
 *    - X, Y: Screen position (floating point pixels)
 *    - OOW (1/W): Perspective correction factor (essential for texturing)
 *    - Z: Depth value (for depth buffering)
 *    - RGBA: Vertex colors
 *    - S/W, T/W: Perspective-divided texture coordinates
 *
 * 4. TEXTURE MEMORY MODEL
 *    Each TMU has its own dedicated texture RAM (typically 2-4MB).
 *    Textures are addressed linearly from grTexMinAddress() to grTexMaxAddress().
 *    Applications must manually manage texture memory allocation.
 *    Mipmaps are stored contiguously in memory.
 *
 * 5. DOUBLE/TRIPLE BUFFERING
 *    Voodoo supports multiple color buffers for smooth animation:
 *    - Front buffer: Currently displayed
 *    - Back buffer: Being rendered to
 *    - Optional third buffer for triple buffering
 *    grBufferSwap() exchanges front and back buffers.
 *
 * =============================================================================
 */

#ifndef GLIDE3X_H
#define GLIDE3X_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/***************************************************************************
    GLIDE BASIC TYPES

    These fixed-width types ensure consistent behavior across platforms.
    The "Fx" prefix is traditional 3dfx naming convention.

    Historical note: These types predate C99's stdint.h, so Glide defined
    its own portable integer types. Modern implementations map them to
    standard fixed-width types.
***************************************************************************/

typedef uint8_t   FxU8;     /* Unsigned 8-bit integer */
typedef int8_t    FxI8;     /* Signed 8-bit integer */
typedef uint16_t  FxU16;    /* Unsigned 16-bit integer */
typedef int16_t   FxI16;    /* Signed 16-bit integer */
typedef uint32_t  FxU32;    /* Unsigned 32-bit integer */
typedef int32_t   FxI32;    /* Signed 32-bit integer */
typedef int32_t   FxBool;   /* Boolean (32-bit for ABI compatibility) */
typedef float     FxFloat;  /* 32-bit IEEE floating point */
typedef double    FxDouble; /* 64-bit IEEE floating point */

#define FXTRUE    1
#define FXFALSE   0

/*
 * Calling convention macros
 *
 * Glide uses stdcall calling convention on Windows. This is important
 * because many games expect the DLL to use stdcall, and mismatched
 * calling conventions cause stack corruption and crashes.
 *
 * stdcall: Callee cleans the stack (function pops its own arguments)
 * cdecl: Caller cleans the stack (default C convention)
 */
#ifdef _WIN32
#define FX_CALL __stdcall
#ifdef GLIDE3X_EXPORTS
#define FX_ENTRY __declspec(dllexport)
#else
#define FX_ENTRY __declspec(dllimport)
#endif
#else
#define FX_CALL
#define FX_ENTRY
#endif

/***************************************************************************
    GLIDE CONTEXT AND COLOR TYPES

    GrColor_t: Packed 32-bit ARGB color. Component order depends on
               GrColorFormat_t setting, but ARGB (0xAARRGGBB) is most common.

    GrAlpha_t: 8-bit alpha value (0=transparent, 255=opaque)

    GrFog_t: 8-bit fog intensity value used in fog table lookups

    GrContext_t: Opaque handle to a Glide rendering context. In original
                 hardware, this represented a specific Voodoo board.
***************************************************************************/

typedef FxU32 GrColor_t;      /* Packed ARGB color */
typedef FxU8  GrAlpha_t;      /* Alpha component */
typedef FxU32 GrMipMapId_t;   /* Mipmap identifier (deprecated in Glide 3.x) */
typedef FxU8  GrFog_t;        /* Fog table entry */
typedef void* GrContext_t;    /* Rendering context handle */

/*
 * Chip ID for multi-TMU support
 *
 * Voodoo hardware can have multiple TMUs (Texture Mapping Units).
 * Voodoo 1: Single TMU (GR_TMU0 only)
 * Voodoo 2: Dual TMU (GR_TMU0 and GR_TMU1)
 * Voodoo 2 SLI: Up to 4 TMUs across two cards
 *
 * TMUs are chained together for multi-texture effects:
 * TMU1 output -> TMU0 input -> FBI (final pixel)
 *
 * GR_FBI refers to the Frame Buffer Interface for operations that
 * don't involve texture mapping (like color combine settings).
 */
typedef FxI32 GrChipID_t;
#define GR_TMU0         0   /* First (or only) TMU */
#define GR_TMU1         1   /* Second TMU (Voodoo 2+) */
#define GR_TMU2         2   /* Third TMU (SLI configurations) */
#define GR_FBI          3   /* Frame Buffer Interface */

/***************************************************************************
    GLIDE HARDWARE QUERY STRUCTURES

    These structures return information about the detected Voodoo hardware.
    Games use this to determine available features and memory.
***************************************************************************/

/*
 * Basic hardware configuration (simplified)
 * Used by grSstQueryHardware() to identify the Voodoo variant.
 */
typedef struct {
    FxU32 hwVersion;  /* Hardware version identifier */
    FxBool isV2;      /* FXTRUE if Voodoo 2 or later */
} GrHwConfiguration;

/*
 * Detailed Voodoo configuration
 * Provides memory sizes and TMU count for capability detection.
 */
typedef struct {
    FxU32 width;      /* Maximum horizontal resolution */
    FxU32 height;     /* Maximum vertical resolution */
    FxU32 memSize;    /* Total framebuffer memory in bytes */
    FxU32 fbiRev;     /* FBI revision number */
    FxU32 nTMU;       /* Number of TMUs (1-3) */
    FxU32 tmuRev;     /* TMU revision number */
} GrVoodooConfig_t;

/***************************************************************************
    SCREEN RESOLUTION

    Voodoo supported specific fixed resolutions. Unlike modern GPUs,
    resolution was set at context creation time and couldn't be changed
    without recreating the context.

    The Voodoo 1 maxed out at 640x480 for most modes.
    Voodoo 2 added 800x600 and higher with SLI.

    Note: Some resolutions (like 320x200) were VGA legacy modes.
***************************************************************************/

typedef FxI32 GrScreenResolution_t;
#define GR_RESOLUTION_320x200   0x0   /* VGA Mode 13h compatible */
#define GR_RESOLUTION_320x240   0x1   /* Mode X / MCGA */
#define GR_RESOLUTION_400x256   0x2   /* Uncommon */
#define GR_RESOLUTION_512x384   0x3   /* SVGA */
#define GR_RESOLUTION_640x200   0x4   /* VGA wide mode */
#define GR_RESOLUTION_640x350   0x5   /* EGA compatible */
#define GR_RESOLUTION_640x400   0x6   /* VGA text mode resolution */
#define GR_RESOLUTION_640x480   0x7   /* Standard VGA - most common */
#define GR_RESOLUTION_800x600   0x8   /* SVGA - Voodoo 2 */
#define GR_RESOLUTION_960x720   0x9   /* Uncommon */
#define GR_RESOLUTION_856x480   0xa   /* Widescreen (rare) */
#define GR_RESOLUTION_512x256   0xb   /* Quarter-screen */
#define GR_RESOLUTION_1024x768  0xC   /* Voodoo 2 SLI / Voodoo 3 */
#define GR_RESOLUTION_1280x1024 0xD   /* Voodoo 3+ */
#define GR_RESOLUTION_1600x1200 0xE   /* Voodoo 4/5 */

/*
 * Refresh rate selection
 *
 * Early Voodoo boards generated their own video timing, so refresh rate
 * was a hardware parameter. Higher refresh rates reduce flicker on CRT
 * monitors but require more memory bandwidth.
 */
typedef FxI32 GrScreenRefresh_t;
#define GR_REFRESH_60Hz   0x0
#define GR_REFRESH_70Hz   0x1
#define GR_REFRESH_72Hz   0x2
#define GR_REFRESH_75Hz   0x3
#define GR_REFRESH_80Hz   0x4
#define GR_REFRESH_90Hz   0x5
#define GR_REFRESH_100Hz  0x6
#define GR_REFRESH_85Hz   0x7
#define GR_REFRESH_120Hz  0x8

/***************************************************************************
    COLOR FORMAT

    Controls the component ordering for GrColor_t values passed to Glide.
    Most games use ARGB, but some (particularly console ports) use ABGR.

    This affects:
    - grConstantColorValue()
    - grBufferClear()
    - grFogColorValue()
    - Vertex color interpretation
***************************************************************************/

typedef FxI32 GrColorFormat_t;
#define GR_COLORFORMAT_ARGB   0x0   /* Alpha, Red, Green, Blue (most common) */
#define GR_COLORFORMAT_ABGR   0x1   /* Alpha, Blue, Green, Red */
#define GR_COLORFORMAT_RGBA   0x2   /* Red, Green, Blue, Alpha */
#define GR_COLORFORMAT_BGRA   0x3   /* Blue, Green, Red, Alpha */

/***************************************************************************
    ORIGIN LOCATION

    Controls where Y=0 is located on screen:

    UPPER_LEFT: Y=0 is at top of screen, Y increases downward
                Standard for Windows/2D graphics

    LOWER_LEFT: Y=0 is at bottom of screen, Y increases upward
                Standard for OpenGL/mathematical coordinates

    This affects framebuffer addressing and vertex Y coordinate interpretation.
    Games typically use UPPER_LEFT for consistency with 2D operations.
***************************************************************************/

typedef FxI32 GrOriginLocation_t;
#define GR_ORIGIN_UPPER_LEFT    0x0   /* Y=0 at top (Windows-style) */
#define GR_ORIGIN_LOWER_LEFT    0x1   /* Y=0 at bottom (OpenGL-style) */

/***************************************************************************
    TEXTURE STRUCTURES AND FORMATS

    Voodoo's TMU supports various texture formats to balance quality vs.
    memory usage. Understanding these formats is crucial for proper texture
    handling.

    TEXTURE FORMAT CATEGORIES:

    8-bit formats (1 byte per texel):
    - RGB_332: 3 bits red, 3 bits green, 2 bits blue (256 colors)
    - ALPHA_8: 8-bit alpha only (grayscale alpha mask)
    - INTENSITY_8: 8-bit intensity (grayscale)
    - ALPHA_INTENSITY_44: 4 bits alpha, 4 bits intensity
    - P_8: 8-bit palette index (uses color palette lookup table)
    - YIQ_422: Compressed format using YIQ color space

    16-bit formats (2 bytes per texel):
    - RGB_565: 5 bits red, 6 bits green, 5 bits blue (65536 colors, no alpha)
    - ARGB_1555: 1 bit alpha, 5 bits each RGB (32768 colors + transparency)
    - ARGB_4444: 4 bits each ARGB (4096 colors + 16 alpha levels)
    - ALPHA_INTENSITY_88: 8 bits alpha, 8 bits intensity

    Memory calculation:
    Texture memory = width * height * bytes_per_texel
    With mipmaps: sum of all mip levels
***************************************************************************/

typedef FxI32 GrTextureFormat_t;
#define GR_TEXFMT_8BIT              0x0   /* Generic 8-bit */
#define GR_TEXFMT_RGB_332           GR_TEXFMT_8BIT  /* 3-3-2 RGB */
#define GR_TEXFMT_YIQ_422           0x1   /* Compressed YIQ */
#define GR_TEXFMT_ALPHA_8           0x2   /* 8-bit alpha only */
#define GR_TEXFMT_INTENSITY_8       0x3   /* 8-bit grayscale */
#define GR_TEXFMT_ALPHA_INTENSITY_44 0x4  /* 4-4 alpha-intensity */
#define GR_TEXFMT_P_8               0x5   /* 8-bit palettized */
#define GR_TEXFMT_RSVD0             0x6   /* Reserved */
#define GR_TEXFMT_RSVD1             0x7   /* Reserved */
#define GR_TEXFMT_16BIT             0x8   /* Generic 16-bit */
#define GR_TEXFMT_ARGB_8332         GR_TEXFMT_16BIT  /* 8-3-3-2 ARGB */
#define GR_TEXFMT_AYIQ_8422         0x9   /* Alpha + YIQ */
#define GR_TEXFMT_RGB_565           0xa   /* 5-6-5 RGB (no alpha) */
#define GR_TEXFMT_ARGB_1555         0xb   /* 1-5-5-5 ARGB (punchthrough) */
#define GR_TEXFMT_ARGB_4444         0xc   /* 4-4-4-4 ARGB */
#define GR_TEXFMT_ALPHA_INTENSITY_88 0xd  /* 8-8 alpha-intensity */
#define GR_TEXFMT_AP_88             0xe   /* 8-8 alpha-palette */
#define GR_TEXFMT_RSVD2             0xf   /* Reserved */

/*
 * Level of Detail (LOD) - Mipmap levels
 *
 * Mipmaps are pre-filtered versions of textures at progressively smaller
 * sizes. Each LOD level is half the size of the previous:
 *
 * LOD 0: 256x256 (largest)
 * LOD 1: 128x128
 * LOD 2: 64x64
 * ...
 * LOD 8: 1x1 (smallest)
 *
 * The TMU automatically selects the appropriate LOD based on the
 * texture's projected size on screen (via 1/W values).
 *
 * Mipmapping reduces aliasing (shimmering) on distant textures and
 * improves cache efficiency by accessing smaller texture data.
 */
typedef FxI32 GrLOD_t;
#define GR_LOD_LOG2_256     0x8   /* 256 pixels (largest) - log2(256)=8 */
#define GR_LOD_LOG2_128     0x7   /* 128 pixels - log2(128)=7 */
#define GR_LOD_LOG2_64      0x6   /* 64 pixels - log2(64)=6 */
#define GR_LOD_LOG2_32      0x5   /* 32 pixels - log2(32)=5 */
#define GR_LOD_LOG2_16      0x4   /* 16 pixels - log2(16)=4 */
#define GR_LOD_LOG2_8       0x3   /* 8 pixels - log2(8)=3 */
#define GR_LOD_LOG2_4       0x2   /* 4 pixels - log2(4)=2 */
#define GR_LOD_LOG2_2       0x1   /* 2 pixels - log2(2)=1 */
#define GR_LOD_LOG2_1       0x0   /* 1 pixel (smallest) - log2(1)=0 */

/*
 * Aspect Ratio - Non-square texture support
 *
 * Voodoo supports non-square textures with power-of-two aspect ratios.
 * This saves texture memory for thin/wide textures like fences, banners.
 *
 * Positive values: wider than tall (e.g., 8x1 = 256x32)
 * Zero: square (e.g., 1x1 = 256x256)
 * Negative values: taller than wide (e.g., 1x8 = 32x256)
 *
 * Note: Both dimensions must still be power-of-two.
 */
typedef FxI32 GrAspectRatio_t;
#define GR_ASPECT_LOG2_8x1  3    /* 8:1 (e.g., 256x32) */
#define GR_ASPECT_LOG2_4x1  2    /* 4:1 (e.g., 256x64) */
#define GR_ASPECT_LOG2_2x1  1    /* 2:1 (e.g., 256x128) */
#define GR_ASPECT_LOG2_1x1  0    /* 1:1 Square (e.g., 256x256) */
#define GR_ASPECT_LOG2_1x2  -1   /* 1:2 (e.g., 128x256) */
#define GR_ASPECT_LOG2_1x4  -2   /* 1:4 (e.g., 64x256) */
#define GR_ASPECT_LOG2_1x8  -3   /* 1:8 (e.g., 32x256) */

/*
 * GrTexInfo - Texture descriptor structure
 *
 * This structure describes a texture's properties for download and binding.
 *
 * smallLodLog2: Smallest mipmap level to use (usually GR_LOD_LOG2_1)
 * largeLodLog2: Largest mipmap level (the base texture size)
 * aspectRatioLog2: Width:height ratio
 * format: Texel format (RGB_565, ARGB_1555, etc.)
 * data: Pointer to texture data (all mip levels concatenated)
 *
 * Mipmap data layout in memory:
 * [LOD 0 (largest)][(LOD 1)][(LOD 2)]...[LOD N (smallest)]
 * Each level immediately follows the previous.
 */
typedef struct {
    GrLOD_t           smallLodLog2;   /* Smallest mip level */
    GrLOD_t           largeLodLog2;   /* Largest mip level (base texture) */
    GrAspectRatio_t   aspectRatioLog2; /* Aspect ratio */
    GrTextureFormat_t format;          /* Texel format */
    void              *data;           /* Texture data pointer */
} GrTexInfo;

/***************************************************************************
    VERTEX STRUCTURE

    GrVertex is the fundamental primitive unit in Glide 3.x. Unlike modern
    APIs where vertices are abstract data processed by shaders, Glide
    vertices contain specific, hardware-expected fields.

    CRITICAL: All coordinates are in SCREEN SPACE. The application must
    perform all 3D transformations (model, view, projection) before
    submitting vertices to Glide. This was intentional - it allowed
    3dfx to focus hardware transistors on rasterization rather than
    geometry processing.

    FIELD DESCRIPTIONS:

    x, y: Screen coordinates in pixels. (0,0) is top-left or bottom-left
          depending on GrOriginLocation_t. Floating point for sub-pixel
          accuracy.

    ooz (1/z): Reciprocal of Z coordinate. Was used in Glide 2.x for
               depth buffering but replaced by 'z' field in Glide 3.x.
               Kept for structure compatibility.

    oow (1/w): THE MOST IMPORTANT FIELD FOR 3D RENDERING.
               Reciprocal of homogeneous W coordinate from projection.
               Used for:
               1. Perspective-correct texture mapping
               2. W-buffer depth testing (alternative to Z-buffer)
               3. Fog computation

               For a standard projection: oow = 1.0 / vertex_z_distance
               Larger oow = closer to camera

    r, g, b, a: Vertex color components. Range 0.0 to 255.0 (NOT 0-1).
                These are interpolated across the triangle and combined
                with texture colors via the color combine unit.

    z: Depth value for Z-buffering. Typically in range 0.0 to 65535.0.
       Larger z = further from camera.
       Only used when Z-buffer mode is enabled (vs W-buffer).

    sow, tow (s/w, t/w): Texture coordinates pre-divided by W.
                         S = horizontal texture axis (0-1 = one texture wrap)
                         T = vertical texture axis
                         Division by W enables perspective correction.
                         The TMU multiplies by W during rasterization.

    sow1, tow1: Texture coordinates for second TMU (multi-texturing).
                Used for effects like lightmaps, detail textures, etc.

    PERSPECTIVE CORRECTION MATH:
    If you have (s, t, w) in clip space:
      sow = s / w
      tow = t / w
      oow = 1 / w

    During rasterization, the TMU computes:
      s_corrected = sow / oow = s
      t_corrected = tow / oow = t

    This gives proper perspective without per-pixel division.
***************************************************************************/

typedef struct {
    float x, y;           /* Screen coordinates (pixels) */
    float ooz;            /* 1/z (deprecated, use oow) */
    float oow;            /* 1/w (perspective correction) */
    float r, g, b, a;     /* RGBA color (0.0 - 255.0) */
    float z;              /* Z depth value */
    float sow, tow;       /* TMU0 texture coordinates (s/w, t/w) */
    float sow1, tow1;     /* TMU1 texture coordinates */
} GrVertex;

/***************************************************************************
    COLOR COMBINE FUNCTIONS

    The Color Combine unit in the FBI determines how the final pixel color
    is computed from various inputs. This is Glide's equivalent of a
    pixel shader, but with fixed functions selected via registers.

    INPUTS TO COLOR COMBINE:
    - LOCAL: Iterated vertex color
    - OTHER: Texture color from TMU (or constant color if texturing disabled)
    - CONSTANT: Color set via grConstantColorValue()

    COMBINE EQUATION (simplified):
    result = FUNCTION(OTHER, LOCAL, FACTOR)

    Common configurations:
    - Flat color: FUNCTION=LOCAL, uses vertex color only
    - Texture replace: FUNCTION=OTHER, uses texture color only
    - Texture modulate: OTHER * LOCAL, texture multiplied by vertex color
    - Texture blend: lerp(LOCAL, OTHER, FACTOR), smooth transition
***************************************************************************/

typedef FxI32 GrCombineFunction_t;
#define GR_COMBINE_FUNCTION_ZERO        0x0   /* Output black (0,0,0,0) */
#define GR_COMBINE_FUNCTION_NONE        GR_COMBINE_FUNCTION_ZERO
#define GR_COMBINE_FUNCTION_LOCAL       0x1   /* Output = LOCAL (vertex color) */
#define GR_COMBINE_FUNCTION_LOCAL_ALPHA 0x2   /* Output = LOCAL alpha broadcast to RGB */
#define GR_COMBINE_FUNCTION_SCALE_OTHER 0x3   /* Output = OTHER * FACTOR */
#define GR_COMBINE_FUNCTION_BLEND_OTHER GR_COMBINE_FUNCTION_SCALE_OTHER
#define GR_COMBINE_FUNCTION_SCALE_OTHER_ADD_LOCAL 0x4       /* OTHER*FACTOR + LOCAL */
#define GR_COMBINE_FUNCTION_SCALE_OTHER_ADD_LOCAL_ALPHA 0x5 /* OTHER*FACTOR + LOCAL.a */
#define GR_COMBINE_FUNCTION_SCALE_OTHER_MINUS_LOCAL 0x6     /* OTHER*FACTOR - LOCAL */
#define GR_COMBINE_FUNCTION_SCALE_OTHER_MINUS_LOCAL_ADD_LOCAL 0x7  /* (OTHER-LOCAL)*FACTOR + LOCAL = lerp */
#define GR_COMBINE_FUNCTION_BLEND       GR_COMBINE_FUNCTION_SCALE_OTHER_MINUS_LOCAL_ADD_LOCAL
#define GR_COMBINE_FUNCTION_SCALE_OTHER_MINUS_LOCAL_ADD_LOCAL_ALPHA 0x8
#define GR_COMBINE_FUNCTION_SCALE_MINUS_LOCAL_ADD_LOCAL 0x9  /* (1-LOCAL)*FACTOR + LOCAL */
#define GR_COMBINE_FUNCTION_BLEND_LOCAL GR_COMBINE_FUNCTION_SCALE_MINUS_LOCAL_ADD_LOCAL
#define GR_COMBINE_FUNCTION_SCALE_MINUS_LOCAL_ADD_LOCAL_ALPHA 0x10

/*
 * Combine Factor - Determines the blending factor in combine equations
 */
typedef FxI32 GrCombineFactor_t;
#define GR_COMBINE_FACTOR_ZERO          0x0   /* Factor = 0.0 */
#define GR_COMBINE_FACTOR_NONE          GR_COMBINE_FACTOR_ZERO
#define GR_COMBINE_FACTOR_LOCAL         0x1   /* Factor = LOCAL color */
#define GR_COMBINE_FACTOR_OTHER_ALPHA   0x2   /* Factor = OTHER alpha */
#define GR_COMBINE_FACTOR_LOCAL_ALPHA   0x3   /* Factor = LOCAL alpha */
#define GR_COMBINE_FACTOR_TEXTURE_ALPHA 0x4   /* Factor = texture alpha */
#define GR_COMBINE_FACTOR_TEXTURE_RGB   0x5   /* Factor = texture RGB */
#define GR_COMBINE_FACTOR_DETAIL_FACTOR GR_COMBINE_FACTOR_TEXTURE_ALPHA
#define GR_COMBINE_FACTOR_LOD_FRACTION  0x5   /* Factor = LOD blend fraction */
#define GR_COMBINE_FACTOR_ONE           0x8   /* Factor = 1.0 */
#define GR_COMBINE_FACTOR_ONE_MINUS_LOCAL 0x9        /* Factor = 1 - LOCAL */
#define GR_COMBINE_FACTOR_ONE_MINUS_OTHER_ALPHA 0xa  /* Factor = 1 - OTHER.a */
#define GR_COMBINE_FACTOR_ONE_MINUS_LOCAL_ALPHA 0xb  /* Factor = 1 - LOCAL.a */
#define GR_COMBINE_FACTOR_ONE_MINUS_TEXTURE_ALPHA 0xc
#define GR_COMBINE_FACTOR_ONE_MINUS_DETAIL_FACTOR GR_COMBINE_FACTOR_ONE_MINUS_TEXTURE_ALPHA
#define GR_COMBINE_FACTOR_ONE_MINUS_LOD_FRACTION 0xd

/*
 * Combine Local - Selects the LOCAL input source
 */
typedef FxI32 GrCombineLocal_t;
#define GR_COMBINE_LOCAL_ITERATED   0x0   /* Iterated vertex color */
#define GR_COMBINE_LOCAL_CONSTANT   0x1   /* Constant color register */
#define GR_COMBINE_LOCAL_NONE       GR_COMBINE_LOCAL_CONSTANT
#define GR_COMBINE_LOCAL_DEPTH      0x2   /* Depth value (special effects) */

/*
 * Combine Other - Selects the OTHER input source
 */
typedef FxI32 GrCombineOther_t;
#define GR_COMBINE_OTHER_ITERATED   0x0   /* Iterated vertex color */
#define GR_COMBINE_OTHER_TEXTURE    0x1   /* Texture color from TMU */
#define GR_COMBINE_OTHER_CONSTANT   0x2   /* Constant color register */
#define GR_COMBINE_OTHER_NONE       GR_COMBINE_OTHER_CONSTANT

/***************************************************************************
    ALPHA/BLEND FUNCTIONS

    These control transparency effects via alpha blending and alpha testing.

    ALPHA TESTING:
    Conditionally discards pixels based on their alpha value compared to
    a reference value. Used for "cutout" transparency (fully transparent
    or fully opaque, no blending). Very efficient because rejected pixels
    skip framebuffer read/write.

    ALPHA BLENDING:
    Combines the incoming pixel color with the existing framebuffer color
    based on alpha values. Used for translucent effects (glass, smoke, etc).

    Blend equation:
    result = (src_color * src_factor) + (dst_color * dst_factor)

    Where src = incoming pixel, dst = existing framebuffer pixel.
***************************************************************************/

typedef FxI32 GrAlphaSource_t;
#define GR_ALPHASOURCE_CC_ALPHA         0x0   /* Constant color alpha */
#define GR_ALPHASOURCE_ITERATED_ALPHA   0x1   /* Iterated vertex alpha */
#define GR_ALPHASOURCE_TEXTURE_ALPHA    0x2   /* Texture alpha */
#define GR_ALPHASOURCE_TEXTURE_ALPHA_TIMES_ITERATED_ALPHA 0x3

/*
 * Alpha Blend Functions
 *
 * These specify the source and destination blend factors.
 *
 * Common combinations:
 * - Standard alpha: SRC=SRC_ALPHA, DST=ONE_MINUS_SRC_ALPHA
 * - Additive: SRC=ONE, DST=ONE (good for fire, glows)
 * - Multiplicative: SRC=DST_COLOR, DST=ZERO (good for shadows)
 * - Pre-multiplied alpha: SRC=ONE, DST=ONE_MINUS_SRC_ALPHA
 */
typedef FxI32 GrAlphaBlendFnc_t;
#define GR_BLEND_ZERO                   0x0   /* 0 */
#define GR_BLEND_SRC_ALPHA              0x1   /* src.a */
#define GR_BLEND_SRC_COLOR              0x2   /* src.rgb */
#define GR_BLEND_DST_COLOR              GR_BLEND_SRC_COLOR  /* dst.rgb (same as src_color for dst factor) */
#define GR_BLEND_DST_ALPHA              0x3   /* dst.a */
#define GR_BLEND_ONE                    0x4   /* 1 */
#define GR_BLEND_ONE_MINUS_SRC_ALPHA    0x5   /* 1 - src.a */
#define GR_BLEND_ONE_MINUS_SRC_COLOR    0x6   /* 1 - src.rgb */
#define GR_BLEND_ONE_MINUS_DST_COLOR    GR_BLEND_ONE_MINUS_SRC_COLOR
#define GR_BLEND_ONE_MINUS_DST_ALPHA    0x7   /* 1 - dst.a */
#define GR_BLEND_RESERVED_8             0x8
#define GR_BLEND_RESERVED_9             0x9
#define GR_BLEND_RESERVED_A             0xa
#define GR_BLEND_RESERVED_B             0xb
#define GR_BLEND_RESERVED_C             0xc
#define GR_BLEND_RESERVED_D             0xd
#define GR_BLEND_RESERVED_E             0xe
#define GR_BLEND_ALPHA_SATURATE         0xf   /* min(src.a, 1-dst.a) */
#define GR_BLEND_PREFOG_COLOR           GR_BLEND_ALPHA_SATURATE

/*
 * Comparison functions for alpha test, depth test, and stencil test
 *
 * These are standard comparison operations:
 * test PASSES if: (incoming_value FUNCTION reference_value)
 */
typedef FxI32 GrCmpFnc_t;
#define GR_CMP_NEVER    0x0   /* Always fail */
#define GR_CMP_LESS     0x1   /* Pass if incoming < reference */
#define GR_CMP_EQUAL    0x2   /* Pass if incoming == reference */
#define GR_CMP_LEQUAL   0x3   /* Pass if incoming <= reference */
#define GR_CMP_GREATER  0x4   /* Pass if incoming > reference */
#define GR_CMP_NOTEQUAL 0x5   /* Pass if incoming != reference */
#define GR_CMP_GEQUAL   0x6   /* Pass if incoming >= reference */
#define GR_CMP_ALWAYS   0x7   /* Always pass */

/***************************************************************************
    BUFFER TYPES

    Voodoo maintains multiple buffers in its framebuffer memory:

    FRONTBUFFER: Currently being displayed on screen
    BACKBUFFER: Being rendered to (double-buffering)
    AUXBUFFER: Auxiliary buffer, typically used for depth/alpha
    DEPTHBUFFER: Z/W depth values (16-bit)
    ALPHABUFFER: Per-pixel alpha values (for destination alpha blending)
    TRIPLEBUFFER: Third color buffer for triple-buffering
***************************************************************************/

typedef FxI32 GrBuffer_t;
#define GR_BUFFER_FRONTBUFFER   0x0
#define GR_BUFFER_BACKBUFFER    0x1
#define GR_BUFFER_AUXBUFFER     0x2
#define GR_BUFFER_DEPTHBUFFER   0x3
#define GR_BUFFER_ALPHABUFFER   0x4
#define GR_BUFFER_TRIPLEBUFFER  0x5

/***************************************************************************
    LFB (LINEAR FRAME BUFFER) TYPES

    The Linear Frame Buffer provides direct CPU access to Voodoo's
    framebuffer memory. This is used for:
    - Software rendering fallbacks
    - Video playback (blitting frames directly)
    - Screen captures
    - Loading pre-rendered backgrounds
    - 2D UI elements

    LFB access bypasses the 3D pipeline and allows pixel-level manipulation.

    WRITE MODES:
    The write mode specifies the format of data being written. Voodoo
    will convert to its native format (RGB565 for color, 16-bit for depth).

    IMPORTANT: LFB writes can be slower than triangle rendering because
    they don't benefit from the command FIFO and may stall the GPU.
***************************************************************************/

typedef FxI32 GrLfbWriteMode_t;
#define GR_LFBWRITEMODE_565        0x0   /* RGB 5-6-5 */
#define GR_LFBWRITEMODE_555        0x1   /* RGB 5-5-5 */
#define GR_LFBWRITEMODE_1555       0x2   /* ARGB 1-5-5-5 */
#define GR_LFBWRITEMODE_RESERVED1  0x3
#define GR_LFBWRITEMODE_888        0x4   /* RGB 8-8-8 (24-bit) */
#define GR_LFBWRITEMODE_8888       0x5   /* ARGB 8-8-8-8 (32-bit) */
#define GR_LFBWRITEMODE_RESERVED2  0x6
#define GR_LFBWRITEMODE_RESERVED3  0x7
#define GR_LFBWRITEMODE_RESERVED4  0x8
#define GR_LFBWRITEMODE_RESERVED5  0x9
#define GR_LFBWRITEMODE_RESERVED6  0xa
#define GR_LFBWRITEMODE_RESERVED7  0xb
#define GR_LFBWRITEMODE_565_DEPTH  0xc   /* RGB565 with depth */
#define GR_LFBWRITEMODE_555_DEPTH  0xd   /* RGB555 with depth */
#define GR_LFBWRITEMODE_1555_DEPTH 0xe   /* ARGB1555 with depth */
#define GR_LFBWRITEMODE_ZA16       0xf   /* 16-bit depth/alpha only */
#define GR_LFBWRITEMODE_ANY        0xFF

/*
 * LFB Lock Types
 *
 * READ_ONLY: Lock for reading (screen capture, readback)
 * WRITE_ONLY: Lock for writing (blitting, video playback)
 *
 * IDLE vs NOIDLE:
 * IDLE: Wait for GPU to finish all pending operations before lock
 * NOIDLE: Lock immediately (may get incomplete frame data)
 */
typedef FxI32 GrLock_t;
#define GR_LFB_READ_ONLY  0x00
#define GR_LFB_WRITE_ONLY 0x01
#define GR_LFB_IDLE       0x00   /* Wait for GPU idle */
#define GR_LFB_NOIDLE     0x10   /* Don't wait */

/*
 * LFB Info structure
 *
 * Returned by grLfbLock() with information needed to access the buffer.
 *
 * lfbPtr: Direct pointer to framebuffer memory (IMPORTANT: may be
 *         write-combined memory - read-modify-write is slow!)
 *
 * strideInBytes: Number of bytes per scanline. This may be larger than
 *                (width * bytes_per_pixel) due to memory alignment.
 *                Always use stride for Y addressing!
 *
 *                pixel_address = lfbPtr + (y * strideInBytes) + (x * bytes_per_pixel)
 */
typedef struct {
    int            size;           /* Size of this structure (for versioning) */
    void          *lfbPtr;         /* Pointer to buffer memory */
    FxU32          strideInBytes;  /* Bytes per scanline */
    GrLfbWriteMode_t writeMode;    /* Current write format */
    GrOriginLocation_t origin;     /* Y=0 location */
} GrLfbInfo_t;

/***************************************************************************
    TEXTURE FILTER/CLAMP/MIPMAP ENUMS

    These control texture sampling behavior in the TMU.
***************************************************************************/

/*
 * Texture Filter Modes
 *
 * POINT_SAMPLED: Nearest-neighbor filtering. Picks the single texel closest
 *                to the sample point. Fast but produces blocky artifacts.
 *
 * BILINEAR: Linear interpolation of 4 nearest texels. Smoother but slightly
 *           slower and blurrier on magnification.
 *
 * Note: Voodoo also supports trilinear filtering (blending between mip
 * levels) when mipmapping is enabled, controlled separately.
 */
typedef FxI32 GrTextureFilterMode_t;
#define GR_TEXTUREFILTER_POINT_SAMPLED  0x0   /* Nearest neighbor */
#define GR_TEXTUREFILTER_BILINEAR       0x1   /* Linear interpolation */

/*
 * Texture Clamp/Wrap Modes
 *
 * WRAP: Texture coordinates repeat (tile). S=1.5 samples same as S=0.5.
 *       Good for repeating patterns (bricks, grass, etc).
 *
 * CLAMP: Coordinates are clamped to [0,1]. Values outside this range
 *        use the edge texel. Good for non-repeating textures.
 */
typedef FxI32 GrTextureClampMode_t;
#define GR_TEXTURECLAMP_WRAP    0x0   /* Tile/repeat */
#define GR_TEXTURECLAMP_CLAMP   0x1   /* Clamp to edge */

/*
 * Mipmap Modes
 *
 * DISABLE: No mipmapping. Always use base LOD. Sharp but aliased.
 *
 * NEAREST: Select nearest mipmap level based on projected size.
 *          "Popping" artifacts when LOD changes.
 *
 * NEAREST_DITHER: Dithered LOD selection. Reduces popping by randomizing
 *                 LOD selection at level boundaries.
 */
typedef FxI32 GrMipMapMode_t;
#define GR_MIPMAP_DISABLE               0x0   /* No mipmapping */
#define GR_MIPMAP_NEAREST               0x1   /* Nearest mip level */
#define GR_MIPMAP_NEAREST_DITHER        0x2   /* Dithered mip selection */

/***************************************************************************
    FOG/DITHER/CHROMAKEY/CULL ENUMS
***************************************************************************/

/*
 * Fog Modes
 *
 * Fog simulates atmospheric scattering by blending objects toward a fog
 * color based on distance. This adds depth cues and hides the far clip plane.
 *
 * DISABLE: No fog
 *
 * WITH_TABLE_ON_FOGCOORD: Use fog table indexed by explicit fog coordinate
 *                         (vertex-specified fog density).
 *
 * WITH_TABLE_ON_W: Use fog table indexed by 1/W (depth-based fog).
 *                  Most common mode. Automatically computes fog from depth.
 *
 * WITH_ITERATED_Z: Use interpolated Z as fog index.
 *
 * WITH_ITERATED_ALPHA: Use interpolated alpha as fog density.
 *                      Allows per-vertex fog control.
 *
 * Fog equation: final_color = lerp(pixel_color, fog_color, fog_factor)
 * Where fog_factor comes from the 64-entry fog table.
 */
typedef FxI32 GrFogMode_t;
#define GR_FOG_DISABLE          0x0
#define GR_FOG_WITH_TABLE_ON_FOGCOORD_EXT 0x1
#define GR_FOG_WITH_TABLE_ON_Q  0x2   /* Q = 1/W */
#define GR_FOG_WITH_TABLE_ON_W  GR_FOG_WITH_TABLE_ON_Q
#define GR_FOG_WITH_ITERATED_Z  0x3
#define GR_FOG_WITH_ITERATED_ALPHA_EXT 0x4
#define GR_FOG_MULT2            0x100  /* 2x fog intensity */
#define GR_FOG_ADD2             0x200  /* Add 2x fog */

/*
 * Dither Modes
 *
 * Dithering adds noise to hide color banding artifacts when converting
 * from higher bit depth (24-bit internal) to lower (16-bit framebuffer).
 *
 * DISABLE: No dithering. May show banding in gradients.
 * 2x2: 2x2 ordered dither matrix. Faster, more visible pattern.
 * 4x4: 4x4 ordered dither matrix. Better quality, less visible pattern.
 */
typedef FxI32 GrDitherMode_t;
#define GR_DITHER_DISABLE       0x0
#define GR_DITHER_2x2           0x1
#define GR_DITHER_4x4           0x2

/*
 * Chroma Key Mode
 *
 * Chroma keying (color keying) makes pixels of a specific color transparent.
 * This is a simple form of transparency that doesn't require alpha testing.
 *
 * Used for: Sprites with rectangular bounds but irregular shapes,
 *           simple transparency without alpha channel in texture.
 *
 * Pixels matching the chromakey color are discarded, showing whatever
 * was previously in the framebuffer.
 */
typedef FxI32 GrChromakeyMode_t;
#define GR_CHROMAKEY_DISABLE    0x0
#define GR_CHROMAKEY_ENABLE     0x1

/*
 * Cull Mode
 *
 * Backface culling rejects triangles facing away from the camera.
 * This nearly doubles performance for closed (solid) objects by
 * not drawing their invisible back faces.
 *
 * Triangle facing is determined by the signed area of the triangle
 * in screen space (based on vertex winding order).
 *
 * DISABLE: Draw all triangles regardless of facing.
 * NEGATIVE: Cull clockwise triangles (OpenGL default).
 * POSITIVE: Cull counter-clockwise triangles (Direct3D default).
 *
 * Convention tip: Counter-clockwise front face is most common.
 */
typedef FxI32 GrCullMode_t;
#define GR_CULL_DISABLE         0x0   /* No culling */
#define GR_CULL_NEGATIVE        0x1   /* Cull negative (CW) winding */
#define GR_CULL_POSITIVE        0x2   /* Cull positive (CCW) winding */

typedef FxI32 GrLfbSrcFmt_t;
#define GR_LFB_SRC_FMT_565      0x0
#define GR_LFB_SRC_FMT_555      0x1
#define GR_LFB_SRC_FMT_1555     0x2
#define GR_LFB_SRC_FMT_888      0x3
#define GR_LFB_SRC_FMT_8888     0x4
#define GR_LFB_SRC_FMT_565_DEPTH 0x5
#define GR_LFB_SRC_FMT_555_DEPTH 0x6
#define GR_LFB_SRC_FMT_1555_DEPTH 0x7
#define GR_LFB_SRC_FMT_ZA16     0x8
#define GR_LFB_SRC_FMT_RLE16    0x9

typedef FxI32 GrCoordinateSpaceMode_t;
#define GR_WINDOW_COORDS        0x0   /* Screen-space coordinates */
#define GR_CLIP_COORDS          0x1   /* Clip-space coordinates (rare) */

/*
 * Draw modes for grDrawVertexArray
 *
 * These control how vertex arrays are interpreted as primitives.
 *
 * TRIANGLES: Every 3 vertices form an independent triangle.
 *            Vertices: 0-1-2, 3-4-5, 6-7-8, ...
 *
 * TRIANGLE_STRIP: Each vertex after the first two forms a triangle
 *                 with the previous two. Very efficient for meshes.
 *                 Triangles: 0-1-2, 2-1-3, 2-3-4, 4-3-5, ...
 *                 (winding alternates to maintain consistent facing)
 *
 * TRIANGLE_FAN: All triangles share the first vertex. Good for convex
 *               polygons and circles.
 *               Triangles: 0-1-2, 0-2-3, 0-3-4, ...
 */
#define GR_POINTS                       0
#define GR_LINES                        2
#define GR_TRIANGLE_STRIP               4
#define GR_TRIANGLE_FAN                 5
#define GR_TRIANGLES                    6
#define GR_TRIANGLE_STRIP_CONTINUE      7   /* Continue previous strip */
#define GR_TRIANGLE_FAN_CONTINUE        8   /* Continue previous fan */

/* grGet parameter names - values from official Glide SDK */
#define GR_BITS_DEPTH                   0x01  /* Depth buffer bits (16) */
#define GR_BITS_RGBA                    0x02  /* Color buffer bits (5,6,5,0) */
#define GR_GAMMA_TABLE_ENTRIES          0x05  /* Gamma table size */
#define GR_MAX_TEXTURE_SIZE             0x0a  /* Max texture dimension (256) */
#define GR_MAX_TEXTURE_ASPECT_RATIO     0x0b  /* Max aspect (3 = 8:1) */
#define GR_MEMORY_FB                    0x0c  /* Framebuffer memory (bytes) */
#define GR_MEMORY_TMU                   0x0d  /* Texture memory (bytes) */
#define GR_MEMORY_UMA                   0x0e  /* Unified memory (bytes) */
#define GR_NUM_BOARDS                   0x0f  /* Number of boards */
#define GR_NUM_FB                       0x11  /* Number of framebuffers */
#define GR_NUM_SWAP_HISTORY_BUFFER      0x12  /* Swap history size */
#define GR_NUM_TMU                      0x13  /* Number of TMUs */
#define GR_TEXTURE_ALIGN                0x24  /* Texture alignment */
#define GR_BITS_GAMMA                   0x2a  /* Gamma bits */

/* grGetString parameter names - values from official Glide SDK */
#define GR_EXTENSION            0xa0  /* Extension string */
#define GR_HARDWARE             0xa1  /* Hardware name */
#define GR_RENDERER             0xa2  /* Renderer name */
#define GR_VENDOR               0xa3  /* Vendor name */
#define GR_VERSION              0xa4  /* Glide version string */

/***************************************************************************
    GLIDE API FUNCTIONS

    The following sections document each Glide API function.
***************************************************************************/

/* ==========================================================================
 * INITIALIZATION AND SHUTDOWN
 *
 * These functions initialize the Glide library and detect Voodoo hardware.
 * grGlideInit() MUST be called before any other Glide function.
 * ========================================================================== */

/*
 * grGlideInit - Initialize the Glide library
 *
 * This must be the first Glide function called. It:
 * - Initializes internal Glide state
 * - Detects and enumerates Voodoo hardware
 * - Sets up communication with the driver
 *
 * On our software implementation: Creates the Voodoo emulator state and
 * initializes lookup tables for reciprocal, dithering, etc.
 */
FX_ENTRY void FX_CALL grGlideInit(void);

/*
 * grGlideShutdown - Clean up and release Glide resources
 *
 * Call this when done using Glide (before program exit). It:
 * - Closes any open rendering contexts
 * - Releases video modes
 * - Frees allocated resources
 *
 * On our implementation: Destroys emulator state and shuts down DirectDraw.
 */
FX_ENTRY void FX_CALL grGlideShutdown(void);

/*
 * grGlideGetVersion - Get Glide version string
 *
 * @param version: Buffer to receive version string (80 chars minimum)
 *
 * Returns a human-readable version string identifying the Glide build.
 * Games may check this for compatibility.
 */
FX_ENTRY void FX_CALL grGlideGetVersion(char version[80]);

/* ==========================================================================
 * CONTEXT MANAGEMENT
 *
 * A Glide context represents an active rendering session on a Voodoo board.
 * In multi-Voodoo setups, each board would have its own context.
 * ========================================================================== */

/*
 * grSstWinOpen - Create a Glide rendering context
 *
 * This is the primary context creation function. It:
 * - Initializes the display at the specified resolution
 * - Allocates framebuffers and depth buffers
 * - Initializes TMU memory
 * - Sets default rendering state
 *
 * @param hwnd: Window handle (HWND on Windows). Pass 0 for fullscreen.
 * @param resolution: Screen resolution (GR_RESOLUTION_xxx)
 * @param refresh: Refresh rate (GR_REFRESH_xxx)
 * @param colorFormat: Color component ordering (usually GR_COLORFORMAT_ARGB)
 * @param origin: Y coordinate origin (UPPER_LEFT or LOWER_LEFT)
 * @param numColorBuffers: Number of color buffers (2 for double-buffering)
 * @param numAuxBuffers: Number of aux buffers (1 for depth buffer)
 *
 * @return: Context handle, or NULL on failure
 *
 * On our implementation: Creates DirectDraw surfaces and initializes the
 * software framebuffer, depth buffer, and TMU memory.
 */
FX_ENTRY GrContext_t FX_CALL grSstWinOpen(
    FxU32 hwnd,
    GrScreenResolution_t resolution,
    GrScreenRefresh_t refresh,
    GrColorFormat_t colorFormat,
    GrOriginLocation_t origin,
    int numColorBuffers,
    int numAuxBuffers
);

/*
 * grSstWinClose - Close a Glide rendering context
 *
 * @param context: Context to close (from grSstWinOpen)
 * @return: FXTRUE on success
 *
 * Releases all resources associated with the context and restores the
 * previous video mode.
 */
FX_ENTRY FxBool FX_CALL grSstWinClose(GrContext_t context);

/*
 * grSelectContext - Switch to a different rendering context
 *
 * @param context: Context to activate
 * @return: FXTRUE on success
 *
 * In multi-board configurations, this switches which Voodoo receives
 * subsequent Glide calls. For single-board setups, this is usually a no-op.
 */
FX_ENTRY FxBool FX_CALL grSelectContext(GrContext_t context);

/* ==========================================================================
 * HARDWARE QUERY
 *
 * These functions query Voodoo hardware capabilities before opening a context.
 * ========================================================================== */

/*
 * grSstQueryHardware - Query hardware configuration
 *
 * @param hwconfig: Structure to receive hardware info
 * @return: FXTRUE if Voodoo hardware detected
 *
 * Games use this to check for Voodoo presence and capabilities.
 */
FX_ENTRY FxBool FX_CALL grSstQueryHardware(GrHwConfiguration *hwconfig);

/*
 * grSstQueryBoards - Get number of Voodoo boards
 *
 * @param hwconfig: Optional structure to receive config (can be NULL)
 * @return: Number of Voodoo boards detected
 */
FX_ENTRY FxU32 FX_CALL grSstQueryBoards(GrHwConfiguration *hwconfig);

/*
 * grSstSelect - Select which Voodoo board to use
 *
 * @param which_sst: Board index (0 = first board)
 *
 * In multi-Voodoo systems, selects which board for subsequent operations.
 */
FX_ENTRY void FX_CALL grSstSelect(int which_sst);

/* ==========================================================================
 * BUFFER OPERATIONS
 *
 * These functions manage the framebuffer and perform buffer swaps.
 * ========================================================================== */

/*
 * grBufferClear - Clear the framebuffer and depth buffer
 *
 * @param color: Clear color (ARGB format based on grColorFormat)
 * @param alpha: Alpha value for auxiliary buffer clear
 * @param depth: Depth value for depth buffer clear (typically 0xFFFF = far)
 *
 * Clears the current render target. The clear respects the scissor rectangle
 * if clipping is enabled.
 *
 * Performance note: Hardware clear is much faster than drawing a quad.
 */
FX_ENTRY void FX_CALL grBufferClear(GrColor_t color, GrAlpha_t alpha, FxU32 depth);

/*
 * grBufferSwap - Swap front and back buffers
 *
 * @param swap_interval: VSync wait count (0 = no wait, 1 = wait 1 VBlank, etc.)
 *
 * This displays the completed back buffer and makes the old front buffer
 * available for rendering. The canonical Glide rendering loop is:
 *
 * while (running) {
 *     grBufferClear(...);
 *     // Draw scene to back buffer
 *     grDrawTriangle(...);
 *     grBufferSwap(1);  // Display and wait for VSync
 * }
 *
 * swap_interval > 0 provides VSync to prevent tearing.
 * swap_interval = 0 gives maximum frame rate but may tear.
 */
FX_ENTRY void FX_CALL grBufferSwap(FxU32 swap_interval);

/*
 * grLfbLock - Lock framebuffer for direct CPU access
 *
 * @param type: Lock type (GR_LFB_READ_ONLY or GR_LFB_WRITE_ONLY)
 * @param buffer: Which buffer to lock
 * @param writeMode: Pixel format for writes
 * @param origin: Y origin for the locked region
 * @param pixelPipeline: FXTRUE to process writes through pixel pipeline
 * @param info: Structure to receive lock information
 *
 * @return: FXTRUE on success
 *
 * Provides direct pointer access to framebuffer memory.
 * MUST call grLfbUnlock() when done!
 *
 * Warning: LFB memory may be write-combined. Avoid read-modify-write.
 */
FX_ENTRY FxBool FX_CALL grLfbLock(GrLock_t type, GrBuffer_t buffer, GrLfbWriteMode_t writeMode,
                 GrOriginLocation_t origin, FxBool pixelPipeline, GrLfbInfo_t *info);

/*
 * grLfbUnlock - Unlock previously locked framebuffer
 *
 * @param type: Lock type (must match grLfbLock call)
 * @param buffer: Buffer to unlock
 * @return: FXTRUE on success
 *
 * Must be called after grLfbLock() to release the buffer.
 */
FX_ENTRY FxBool FX_CALL grLfbUnlock(GrLock_t type, GrBuffer_t buffer);

/*
 * grLfbWriteRegion - Write a rectangular region to framebuffer
 *
 * @param dst_buffer: Destination buffer
 * @param dst_x, dst_y: Destination coordinates
 * @param src_format: Source pixel format
 * @param src_width, src_height: Source dimensions
 * @param pixelPipeline: Process through pixel pipeline
 * @param src_stride: Source row stride in bytes
 * @param src_data: Source pixel data
 *
 * @return: FXTRUE on success
 *
 * Efficiently copies a rectangle of pixels to the framebuffer.
 * Used for video playback, background images, UI elements.
 */
FX_ENTRY FxBool FX_CALL grLfbWriteRegion(GrBuffer_t dst_buffer, FxU32 dst_x, FxU32 dst_y,
                         GrLfbSrcFmt_t src_format, FxU32 src_width, FxU32 src_height,
                         FxBool pixelPipeline, FxI32 src_stride, void *src_data);

/*
 * grLfbReadRegion - Read a rectangular region from framebuffer
 *
 * @param src_buffer: Source buffer
 * @param src_x, src_y: Source coordinates
 * @param src_width, src_height: Region dimensions
 * @param dst_stride: Destination row stride in bytes
 * @param dst_data: Destination buffer
 *
 * @return: FXTRUE on success
 *
 * Reads pixels back from framebuffer. Used for screen capture, effects.
 */
FX_ENTRY FxBool FX_CALL grLfbReadRegion(GrBuffer_t src_buffer, FxU32 src_x, FxU32 src_y,
                        FxU32 src_width, FxU32 src_height,
                        FxU32 dst_stride, void *dst_data);

/* ==========================================================================
 * RENDERING STATE
 *
 * These functions configure the pixel pipeline for various effects.
 * ========================================================================== */

/*
 * grColorCombine - Configure the FBI color combine unit
 *
 * @param function: Combine function (how inputs are combined)
 * @param factor: Blend factor for scaling
 * @param local: Source for LOCAL input
 * @param other: Source for OTHER input (typically texture)
 * @param invert: Invert the final result
 *
 * This is the core of Glide's "pixel shader" equivalent. Common setups:
 *
 * Flat shading (vertex color only):
 *   grColorCombine(GR_COMBINE_FUNCTION_LOCAL, ...)
 *
 * Texture replace (texture color only):
 *   grColorCombine(GR_COMBINE_FUNCTION_SCALE_OTHER, GR_COMBINE_FACTOR_ONE,
 *                  GR_COMBINE_LOCAL_ITERATED, GR_COMBINE_OTHER_TEXTURE, ...)
 *
 * Texture modulate (texture * vertex color):
 *   grColorCombine(GR_COMBINE_FUNCTION_SCALE_OTHER, GR_COMBINE_FACTOR_LOCAL,
 *                  GR_COMBINE_LOCAL_ITERATED, GR_COMBINE_OTHER_TEXTURE, ...)
 */
FX_ENTRY void FX_CALL grColorCombine(GrCombineFunction_t function, GrCombineFactor_t factor,
                    GrCombineLocal_t local, GrCombineOther_t other, FxBool invert);

/*
 * grAlphaCombine - Configure the FBI alpha combine unit
 *
 * Similar to grColorCombine but for the alpha channel.
 * Alpha combine output goes to alpha testing and blending.
 */
FX_ENTRY void FX_CALL grAlphaCombine(GrCombineFunction_t function, GrCombineFactor_t factor,
                    GrCombineLocal_t local, GrCombineOther_t other, FxBool invert);

/*
 * grAlphaBlendFunction - Set alpha blending factors
 *
 * @param rgb_sf: Source RGB blend factor
 * @param rgb_df: Destination RGB blend factor
 * @param alpha_sf: Source alpha blend factor
 * @param alpha_df: Destination alpha blend factor
 *
 * Blend equation: result = (src * src_factor) + (dst * dst_factor)
 *
 * Common configurations:
 *
 * Standard transparency:
 *   grAlphaBlendFunction(GR_BLEND_SRC_ALPHA, GR_BLEND_ONE_MINUS_SRC_ALPHA,
 *                        GR_BLEND_ZERO, GR_BLEND_ZERO)
 *
 * Additive blending (fire, glow):
 *   grAlphaBlendFunction(GR_BLEND_ONE, GR_BLEND_ONE, ...)
 *
 * Multiplicative (shadows):
 *   grAlphaBlendFunction(GR_BLEND_DST_COLOR, GR_BLEND_ZERO, ...)
 */
FX_ENTRY void FX_CALL grAlphaBlendFunction(GrAlphaBlendFnc_t rgb_sf, GrAlphaBlendFnc_t rgb_df,
                          GrAlphaBlendFnc_t alpha_sf, GrAlphaBlendFnc_t alpha_df);

/*
 * grAlphaTestFunction - Set alpha test comparison
 *
 * @param function: Comparison function (GR_CMP_xxx)
 *
 * Pixels fail the alpha test if their alpha doesn't pass the comparison
 * against the reference value (set by grAlphaTestReferenceValue).
 */
FX_ENTRY void FX_CALL grAlphaTestFunction(GrCmpFnc_t function);

/*
 * grAlphaTestReferenceValue - Set alpha test reference
 *
 * @param value: Reference alpha value (0-255)
 */
FX_ENTRY void FX_CALL grAlphaTestReferenceValue(GrAlpha_t value);

/*
 * Depth buffer modes
 *
 * DISABLE: No depth testing (2D mode)
 * ZBUFFER: Use Z values for depth comparison
 * WBUFFER: Use W values (1/W) for depth - more precision at distance
 * ZBUFFER_COMPARE_TO_BIAS: Compare Z against zaColor bias value (for decals)
 * WBUFFER_COMPARE_TO_BIAS: Compare W against zaColor bias value
 */
typedef FxI32 GrDepthBufferMode_t;
#define GR_DEPTHBUFFER_DISABLE      0x0
#define GR_DEPTHBUFFER_ZBUFFER      0x1
#define GR_DEPTHBUFFER_WBUFFER      0x2
#define GR_DEPTHBUFFER_ZBUFFER_COMPARE_TO_BIAS 0x3
#define GR_DEPTHBUFFER_WBUFFER_COMPARE_TO_BIAS 0x4
/* Legacy aliases */
#define GR_DEPTHBUFFER_ZBUFFER_COMPARE_ONLY GR_DEPTHBUFFER_ZBUFFER_COMPARE_TO_BIAS
#define GR_DEPTHBUFFER_WBUFFER_COMPARE_ONLY GR_DEPTHBUFFER_WBUFFER_COMPARE_TO_BIAS

/*
 * grDepthBufferMode - Enable/configure depth buffering
 *
 * @param mode: Depth buffer mode
 *
 * Z-buffer vs W-buffer:
 * - Z-buffer: Linear depth, even precision. Good for indoor scenes.
 * - W-buffer: Perspective depth, more precision near camera. Good for
 *   outdoor scenes with large depth range.
 */
FX_ENTRY void FX_CALL grDepthBufferMode(GrDepthBufferMode_t mode);

/*
 * grDepthBufferFunction - Set depth test comparison
 *
 * @param function: Comparison function (GR_CMP_xxx)
 *
 * Common settings:
 * - GR_CMP_LESS: Pass if new Z < existing Z (standard)
 * - GR_CMP_LEQUAL: Pass if new Z <= existing Z (allows coplanar)
 * - GR_CMP_ALWAYS: Always pass (disable depth test but keep writes)
 */
FX_ENTRY void FX_CALL grDepthBufferFunction(GrCmpFnc_t function);

/*
 * grDepthMask - Enable/disable depth buffer writes
 *
 * @param mask: FXTRUE to enable writes, FXFALSE to disable
 *
 * Disable depth writes for:
 * - Transparent objects (draw after opaques, read-only depth test)
 * - Decals (prevent z-fighting)
 * - Particles
 */
FX_ENTRY void FX_CALL grDepthMask(FxBool mask);

/*
 * grDepthBiasLevel - Add constant offset to depth values
 *
 * @param level: Bias value (positive = push away from camera)
 *
 * Use to prevent z-fighting on coplanar surfaces (decals, shadows).
 */
FX_ENTRY void FX_CALL grDepthBiasLevel(FxI32 level);

/*
 * grDitherMode - Set dithering mode
 *
 * @param mode: Dither mode (DISABLE, 2x2, 4x4)
 *
 * Dithering reduces banding in gradients on 16-bit framebuffers.
 * 4x4 gives best quality; disable for UI elements.
 */
FX_ENTRY void FX_CALL grDitherMode(GrDitherMode_t mode);

/*
 * grChromakeyMode - Enable/disable chroma keying
 *
 * @param mode: ENABLE or DISABLE
 */
FX_ENTRY void FX_CALL grChromakeyMode(GrChromakeyMode_t mode);

/*
 * grChromakeyValue - Set chroma key color
 *
 * @param value: Color to treat as transparent
 *
 * Pixels matching this color are discarded.
 */
FX_ENTRY void FX_CALL grChromakeyValue(GrColor_t value);

/*
 * grCullMode - Set backface culling mode
 *
 * @param mode: Cull mode (DISABLE, NEGATIVE, POSITIVE)
 */
FX_ENTRY void FX_CALL grCullMode(GrCullMode_t mode);

/* ==========================================================================
 * DRAWING FUNCTIONS
 * ========================================================================== */

/*
 * grDrawTriangle - Draw a single triangle
 *
 * @param a, b, c: Pointers to the three vertices
 *
 * The fundamental Glide drawing primitive. All 3D objects are composed
 * of triangles submitted through this function or vertex arrays.
 *
 * Vertices must contain valid values for all fields used by the current
 * rendering state (position, color, texture coords as needed).
 *
 * Triangle winding determines front/back face for culling.
 */
FX_ENTRY void FX_CALL grDrawTriangle(const GrVertex *a, const GrVertex *b, const GrVertex *c);

/*
 * grDrawVertexArray - Draw primitives from a vertex array
 *
 * @param mode: Primitive type (TRIANGLES, STRIP, FAN, etc.)
 * @param count: Number of vertices
 * @param pointers: Array of vertex pointers
 *
 * More efficient than individual grDrawTriangle calls for batched geometry.
 */
FX_ENTRY void FX_CALL grDrawVertexArray(FxU32 mode, FxU32 count, void *pointers);

/*
 * grDrawVertexArrayContiguous - Draw from contiguous vertex buffer
 *
 * @param mode: Primitive type
 * @param count: Number of vertices
 * @param vertices: Contiguous vertex buffer
 * @param stride: Bytes between vertices
 *
 * Even more efficient - no pointer indirection.
 */
FX_ENTRY void FX_CALL grDrawVertexArrayContiguous(FxU32 mode, FxU32 count, void *vertices, FxU32 stride);

/* ==========================================================================
 * TEXTURE MANAGEMENT
 * ========================================================================== */

/*
 * grTexMinAddress - Get minimum texture address
 *
 * @param tmu: TMU to query
 * @return: Minimum valid texture address (always 0)
 *
 * Texture addresses form a linear address space in TMU memory.
 */
FX_ENTRY FxU32 FX_CALL grTexMinAddress(GrChipID_t tmu);

/*
 * grTexMaxAddress - Get maximum texture address
 *
 * @param tmu: TMU to query
 * @return: Maximum valid texture address (TMU memory size - 1)
 */
FX_ENTRY FxU32 FX_CALL grTexMaxAddress(GrChipID_t tmu);

/*
 * grTexSource - Set current texture source
 *
 * @param tmu: TMU to configure
 * @param startAddress: Texture start address in TMU memory
 * @param evenOdd: Mipmap even/odd selection (usually GR_MIPMAPLEVELMASK_BOTH)
 * @param info: Texture descriptor
 *
 * Binds a previously downloaded texture as the current texture for the TMU.
 * The texture must have been downloaded with grTexDownloadMipMap first.
 */
FX_ENTRY void FX_CALL grTexSource(GrChipID_t tmu, FxU32 startAddress, FxU32 evenOdd, GrTexInfo *info);

/*
 * grTexDownloadMipMap - Download complete mipmap chain
 *
 * @param tmu: TMU to download to
 * @param startAddress: Destination address in TMU memory
 * @param evenOdd: Mipmap even/odd selection
 * @param info: Texture descriptor (data pointer must be valid)
 *
 * Uploads all mipmap levels of a texture to TMU memory.
 * info->data must point to all mip levels concatenated.
 */
FX_ENTRY void FX_CALL grTexDownloadMipMap(GrChipID_t tmu, FxU32 startAddress, FxU32 evenOdd, GrTexInfo *info);

/*
 * grTexDownloadMipMapLevel - Download single mipmap level
 *
 * Uploads a single LOD level. Used for partial texture updates.
 */
FX_ENTRY void FX_CALL grTexDownloadMipMapLevel(GrChipID_t tmu, FxU32 startAddress, GrLOD_t thisLod,
                               GrLOD_t largeLod, GrAspectRatio_t aspectRatio,
                               GrTextureFormat_t format, FxU32 evenOdd, void *data);

/*
 * grTexDownloadMipMapLevelPartial - Download part of a single mipmap level
 *
 * Uploads a portion of a single LOD level. Used for streaming/partial texture updates.
 * @param start: Starting row to upload
 * @param end: Ending row to upload (inclusive)
 */
FX_ENTRY void FX_CALL grTexDownloadMipMapLevelPartial(GrChipID_t tmu, FxU32 startAddress, GrLOD_t thisLod,
                               GrLOD_t largeLod, GrAspectRatio_t aspectRatio,
                               GrTextureFormat_t format, FxU32 evenOdd, void *data,
                               int start, int end);

/*
 * grTexTextureMemRequired - Calculate texture memory requirement
 *
 * @param evenOdd: Mipmap selection
 * @param info: Texture descriptor
 * @return: Bytes required
 *
 * Use to allocate texture memory before downloading.
 */
FX_ENTRY FxU32 FX_CALL grTexTextureMemRequired(FxU32 evenOdd, GrTexInfo *info);

/*
 * grTexCombine - Configure TMU texture combine
 *
 * @param tmu: TMU to configure
 * @param rgb_function: RGB combine function
 * @param rgb_factor: RGB blend factor
 * @param alpha_function: Alpha combine function
 * @param alpha_factor: Alpha blend factor
 * @param rgb_invert: Invert RGB result
 * @param alpha_invert: Invert alpha result
 *
 * In multi-texture (dual-TMU) configurations, this controls how
 * TMU1's output combines with TMU0's output before reaching the FBI.
 */
FX_ENTRY void FX_CALL grTexCombine(GrChipID_t tmu, GrCombineFunction_t rgb_function,
                  GrCombineFactor_t rgb_factor, GrCombineFunction_t alpha_function,
                  GrCombineFactor_t alpha_factor, FxBool rgb_invert, FxBool alpha_invert);

/*
 * grTexFilterMode - Set texture filtering
 *
 * @param tmu: TMU to configure
 * @param minfilter_mode: Minification filter
 * @param magfilter_mode: Magnification filter
 *
 * Minification: When texture appears smaller than native (far away)
 * Magnification: When texture appears larger than native (close up)
 */
FX_ENTRY void FX_CALL grTexFilterMode(GrChipID_t tmu, GrTextureFilterMode_t minfilter_mode,
                     GrTextureFilterMode_t magfilter_mode);

/*
 * grTexClampMode - Set texture wrap/clamp
 *
 * @param tmu: TMU to configure
 * @param s_clamp: S (horizontal) clamp mode
 * @param t_clamp: T (vertical) clamp mode
 */
FX_ENTRY void FX_CALL grTexClampMode(GrChipID_t tmu, GrTextureClampMode_t s_clamp, GrTextureClampMode_t t_clamp);

/*
 * grTexMipMapMode - Set mipmapping mode
 *
 * @param tmu: TMU to configure
 * @param mode: Mipmap mode
 * @param lodBlend: Enable trilinear blending between LODs
 */
FX_ENTRY void FX_CALL grTexMipMapMode(GrChipID_t tmu, GrMipMapMode_t mode, FxBool lodBlend);

/*
 * grTexLodBiasValue - Set LOD bias
 *
 * @param tmu: TMU to configure
 * @param bias: LOD bias (negative = sharper/more aliased, positive = blurrier)
 *
 * Shifts the automatic LOD selection. Use to tweak texture sharpness.
 */
FX_ENTRY void FX_CALL grTexLodBiasValue(GrChipID_t tmu, float bias);

/* ==========================================================================
 * MISCELLANEOUS FUNCTIONS
 * ========================================================================== */

/*
 * grConstantColorValue - Set constant color register
 *
 * @param value: Color value
 *
 * The constant color is used by combine units when GR_COMBINE_xxx_CONSTANT
 * is selected.
 */
FX_ENTRY void FX_CALL grConstantColorValue(GrColor_t value);

/*
 * grClipWindow - Set scissor/clip rectangle
 *
 * @param minx, miny: Top-left corner
 * @param maxx, maxy: Bottom-right corner (exclusive)
 *
 * Pixels outside this rectangle are discarded. Useful for UI regions,
 * split-screen, portals.
 */
FX_ENTRY void FX_CALL grClipWindow(FxU32 minx, FxU32 miny, FxU32 maxx, FxU32 maxy);

/*
 * grRenderBuffer - Select render target buffer
 *
 * @param buffer: FRONTBUFFER or BACKBUFFER
 *
 * Normally rendering goes to BACKBUFFER. Render to FRONTBUFFER for
 * immediate display (used by some games for UI).
 */
FX_ENTRY void FX_CALL grRenderBuffer(GrBuffer_t buffer);

/*
 * grSstScreenWidth/Height - Get screen dimensions
 */
FX_ENTRY float FX_CALL grSstScreenWidth(void);
FX_ENTRY float FX_CALL grSstScreenHeight(void);

/*
 * grSstOrigin - Change Y coordinate origin
 *
 * @param origin: UPPER_LEFT or LOWER_LEFT
 */
FX_ENTRY void FX_CALL grSstOrigin(GrOriginLocation_t origin);

/*
 * grCoordinateSpace - Set coordinate space mode
 *
 * @param mode: WINDOW_COORDS or CLIP_COORDS
 *
 * Almost always WINDOW_COORDS (screen space).
 */
FX_ENTRY void FX_CALL grCoordinateSpace(GrCoordinateSpaceMode_t mode);

/*
 * grVertexLayout - Define vertex attribute layout
 *
 * @param param: Vertex parameter ID
 * @param offset: Byte offset in vertex structure
 * @param mode: Parameter mode
 *
 * Used with vertex arrays to define custom vertex formats.
 * Our implementation uses fixed GrVertex structure.
 */
FX_ENTRY void FX_CALL grVertexLayout(FxU32 param, FxI32 offset, FxU32 mode);

/*
 * grGet - Query Glide state/capabilities
 *
 * @param pname: Parameter name (GR_xxx)
 * @param plength: Buffer size in bytes
 * @param params: Buffer to receive values
 * @return: Bytes written
 */
FX_ENTRY FxU32 FX_CALL grGet(FxU32 pname, FxU32 plength, FxI32 *params);

/*
 * grGetString - Get Glide string
 *
 * @param pname: String identifier
 * @return: String pointer (static, do not free)
 */
FX_ENTRY const char* FX_CALL grGetString(FxU32 pname);

/*
 * grFinish/grFlush - Synchronization
 *
 * grFinish: Wait for all pending operations to complete
 * grFlush: Ensure commands are submitted (but don't wait)
 *
 * Our software implementation is synchronous, so these are no-ops.
 */
FX_ENTRY void FX_CALL grFinish(void);
FX_ENTRY void FX_CALL grFlush(void);

/* ==========================================================================
 * FOG FUNCTIONS
 * ========================================================================== */

/*
 * grFogMode - Enable and configure fog
 *
 * @param mode: Fog mode flags
 */
FX_ENTRY void FX_CALL grFogMode(GrFogMode_t mode);

/*
 * grFogColorValue - Set fog color
 *
 * @param fogcolor: RGB fog color
 */
FX_ENTRY void FX_CALL grFogColorValue(GrColor_t fogcolor);

/*
 * grFogTable - Set fog table
 *
 * @param ft: 64-entry fog intensity table
 *
 * Table maps depth/W values to fog intensity (0=no fog, 255=full fog).
 * Build with linear or exponential falloff as desired.
 */
FX_ENTRY void FX_CALL grFogTable(const GrFog_t ft[]);

/* ==========================================================================
 * ADDITIONAL DRAWING FUNCTIONS
 * ========================================================================== */

FX_ENTRY void FX_CALL grDrawPoint(const void *pt);
FX_ENTRY void FX_CALL grDrawLine(const void *v1, const void *v2);
FX_ENTRY void FX_CALL grAADrawTriangle(const void *a, const void *b, const void *c,
                      FxBool ab_antialias, FxBool bc_antialias, FxBool ca_antialias);

/* ==========================================================================
 * ENABLE/DISABLE FUNCTIONS
 * ========================================================================== */

typedef FxI32 GrEnableMode_t;
#define GR_PASSTHRU         0x0   /* VGA passthrough */
#define GR_SHAMELESS_PLUG   0x1   /* Show 3dfx logo on init */
#define GR_VIDEO_SMOOTHING  0x2   /* Output filtering */
#define GR_AA_ORDERED       0x3   /* Ordered AA */
#define GR_ALLOW_MIPMAP_DITHER 0x4

FX_ENTRY void FX_CALL grEnable(GrEnableMode_t mode);
FX_ENTRY void FX_CALL grDisable(GrEnableMode_t mode);

/* ==========================================================================
 * STATE FUNCTIONS
 * ========================================================================== */

FX_ENTRY void FX_CALL grColorMask(FxBool rgb, FxBool a);
FX_ENTRY void FX_CALL grViewport(FxI32 x, FxI32 y, FxI32 width, FxI32 height);

/* ==========================================================================
 * DYNAMIC FUNCTION LOOKUP
 * ========================================================================== */

typedef void (*GrProc)(void);
typedef void (*GrErrorCallbackFnc_t)(const char *string, FxBool fatal);

/*
 * grGetProcAddress - Get function pointer by name
 *
 * @param procName: Function name string
 * @return: Function pointer, or NULL if not found
 *
 * Allows games to check for and use extension functions.
 */
FX_ENTRY GrProc FX_CALL grGetProcAddress(char *procName);

/*
 * grErrorSetCallback - Set error callback
 *
 * @param fnc: Callback function for error reporting
 */
FX_ENTRY void FX_CALL grErrorSetCallback(GrErrorCallbackFnc_t fnc);

/* ==========================================================================
 * GAMMA CORRECTION
 * ========================================================================== */

/*
 * grLoadGammaTable - Load custom gamma LUT
 *
 * @param nentries: Number of entries
 * @param red, green, blue: Gamma tables for each channel
 */
FX_ENTRY void FX_CALL grLoadGammaTable(FxU32 nentries, FxU32 *red, FxU32 *green, FxU32 *blue);

/*
 * guGammaCorrectionRGB - Set gamma correction
 *
 * @param red, green, blue: Gamma values for each channel (1.0 = linear)
 *
 * Convenience function that builds and loads gamma tables.
 */
FX_ENTRY void FX_CALL guGammaCorrectionRGB(float red, float green, float blue);

/* ==========================================================================
 * TEXTURE TABLE FUNCTIONS
 * ========================================================================== */

typedef FxI32 GrTexTable_t;
#define GR_TEXTABLE_NCC0    0x0   /* NCC decompression table 0 */
#define GR_TEXTABLE_NCC1    0x1   /* NCC decompression table 1 */
#define GR_TEXTABLE_PALETTE 0x2   /* 256-color palette */
#define GR_TEXTABLE_PALETTE_6666_EXT 0x3  /* Extended palette */

/*
 * grTexDownloadTable - Download texture lookup table
 *
 * @param type: Table type
 * @param data: Table data
 *
 * NCC tables: Used for YIQ compressed textures
 * Palette: 256 ARGB entries for paletted textures (P_8 format)
 */
FX_ENTRY void FX_CALL grTexDownloadTable(GrTexTable_t type, void *data);

#ifdef __cplusplus
}
#endif

#endif /* GLIDE3X_H */
