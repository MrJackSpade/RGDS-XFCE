/*
 * glide3x.h - Glide 3.x API definitions
 *
 * Based on 3dfx Glide SDK headers
 * This is the public API that games like Diablo 2 expect
 */

#ifndef GLIDE3X_H
#define GLIDE3X_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/***************************************************************************
    GLIDE BASIC TYPES
***************************************************************************/

typedef uint8_t   FxU8;
typedef int8_t    FxI8;
typedef uint16_t  FxU16;
typedef int16_t   FxI16;
typedef uint32_t  FxU32;
typedef int32_t   FxI32;
typedef int32_t   FxBool;
typedef float     FxFloat;
typedef double    FxDouble;

#define FXTRUE    1
#define FXFALSE   0

/* Calling convention macros - Glide uses stdcall on Windows */
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
***************************************************************************/

typedef FxU32 GrColor_t;
typedef FxU8  GrAlpha_t;
typedef FxU32 GrMipMapId_t;
typedef FxU8  GrFog_t;
typedef void* GrContext_t;

/* Chip ID for multi-TMU support */
typedef FxI32 GrChipID_t;
#define GR_TMU0         0
#define GR_TMU1         1
#define GR_TMU2         2
#define GR_FBI          3

/***************************************************************************
    GLIDE HARDWARE QUERY
***************************************************************************/

typedef struct {
    FxU32 hwVersion;
    FxBool isV2;
} GrHwConfiguration;

typedef struct {
    FxU32 width;
    FxU32 height;
    FxU32 memSize;       /* in bytes */
    FxU32 fbiRev;
    FxU32 nTMU;
    FxU32 tmuRev;
} GrVoodooConfig_t;

/***************************************************************************
    SCREEN RESOLUTION
***************************************************************************/

typedef FxI32 GrScreenResolution_t;
#define GR_RESOLUTION_320x200   0x0
#define GR_RESOLUTION_320x240   0x1
#define GR_RESOLUTION_400x256   0x2
#define GR_RESOLUTION_512x384   0x3
#define GR_RESOLUTION_640x200   0x4
#define GR_RESOLUTION_640x350   0x5
#define GR_RESOLUTION_640x400   0x6
#define GR_RESOLUTION_640x480   0x7
#define GR_RESOLUTION_800x600   0x8
#define GR_RESOLUTION_960x720   0x9
#define GR_RESOLUTION_856x480   0xa
#define GR_RESOLUTION_512x256   0xb
#define GR_RESOLUTION_1024x768  0xC
#define GR_RESOLUTION_1280x1024 0xD
#define GR_RESOLUTION_1600x1200 0xE

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
***************************************************************************/

typedef FxI32 GrColorFormat_t;
#define GR_COLORFORMAT_ARGB   0x0
#define GR_COLORFORMAT_ABGR   0x1
#define GR_COLORFORMAT_RGBA   0x2
#define GR_COLORFORMAT_BGRA   0x3

/***************************************************************************
    ORIGIN LOCATION
***************************************************************************/

typedef FxI32 GrOriginLocation_t;
#define GR_ORIGIN_UPPER_LEFT    0x0
#define GR_ORIGIN_LOWER_LEFT    0x1

/***************************************************************************
    TEXTURE STRUCTURES
***************************************************************************/

typedef FxI32 GrTextureFormat_t;
#define GR_TEXFMT_8BIT              0x0
#define GR_TEXFMT_RGB_332           GR_TEXFMT_8BIT
#define GR_TEXFMT_YIQ_422           0x1
#define GR_TEXFMT_ALPHA_8           0x2
#define GR_TEXFMT_INTENSITY_8       0x3
#define GR_TEXFMT_ALPHA_INTENSITY_44 0x4
#define GR_TEXFMT_P_8               0x5
#define GR_TEXFMT_RSVD0             0x6
#define GR_TEXFMT_RSVD1             0x7
#define GR_TEXFMT_16BIT             0x8
#define GR_TEXFMT_ARGB_8332         GR_TEXFMT_16BIT
#define GR_TEXFMT_AYIQ_8422         0x9
#define GR_TEXFMT_RGB_565           0xa
#define GR_TEXFMT_ARGB_1555         0xb
#define GR_TEXFMT_ARGB_4444         0xc
#define GR_TEXFMT_ALPHA_INTENSITY_88 0xd
#define GR_TEXFMT_AP_88             0xe
#define GR_TEXFMT_RSVD2             0xf

typedef FxI32 GrLOD_t;
#define GR_LOD_LOG2_256     0x0
#define GR_LOD_LOG2_128     0x1
#define GR_LOD_LOG2_64      0x2
#define GR_LOD_LOG2_32      0x3
#define GR_LOD_LOG2_16      0x4
#define GR_LOD_LOG2_8       0x5
#define GR_LOD_LOG2_4       0x6
#define GR_LOD_LOG2_2       0x7
#define GR_LOD_LOG2_1       0x8

typedef FxI32 GrAspectRatio_t;
#define GR_ASPECT_LOG2_8x1  3
#define GR_ASPECT_LOG2_4x1  2
#define GR_ASPECT_LOG2_2x1  1
#define GR_ASPECT_LOG2_1x1  0
#define GR_ASPECT_LOG2_1x2  -1
#define GR_ASPECT_LOG2_1x4  -2
#define GR_ASPECT_LOG2_1x8  -3

typedef struct {
    GrLOD_t           smallLodLog2;
    GrLOD_t           largeLodLog2;
    GrAspectRatio_t   aspectRatioLog2;
    GrTextureFormat_t format;
    void              *data;
} GrTexInfo;

/***************************************************************************
    VERTEX STRUCTURE
***************************************************************************/

typedef struct {
    float x, y;           /* Screen coordinates */
    float ooz;            /* 1/z (ignored in Glide 3.x, use oow) */
    float oow;            /* 1/w (for perspective correction) */
    float r, g, b, a;     /* RGBA color components (0.0 - 255.0) */
    float z;              /* Z coordinate (depth) */
    float sow, tow;       /* s/w and t/w texture coordinates */
    float sow1, tow1;     /* TMU1 texture coordinates */
} GrVertex;

/***************************************************************************
    COMBINE FUNCTIONS
***************************************************************************/

typedef FxI32 GrCombineFunction_t;
#define GR_COMBINE_FUNCTION_ZERO        0x0
#define GR_COMBINE_FUNCTION_NONE        GR_COMBINE_FUNCTION_ZERO
#define GR_COMBINE_FUNCTION_LOCAL       0x1
#define GR_COMBINE_FUNCTION_LOCAL_ALPHA 0x2
#define GR_COMBINE_FUNCTION_SCALE_OTHER 0x3
#define GR_COMBINE_FUNCTION_BLEND_OTHER GR_COMBINE_FUNCTION_SCALE_OTHER
#define GR_COMBINE_FUNCTION_SCALE_OTHER_ADD_LOCAL 0x4
#define GR_COMBINE_FUNCTION_SCALE_OTHER_ADD_LOCAL_ALPHA 0x5
#define GR_COMBINE_FUNCTION_SCALE_OTHER_MINUS_LOCAL 0x6
#define GR_COMBINE_FUNCTION_SCALE_OTHER_MINUS_LOCAL_ADD_LOCAL 0x7
#define GR_COMBINE_FUNCTION_BLEND       GR_COMBINE_FUNCTION_SCALE_OTHER_MINUS_LOCAL_ADD_LOCAL
#define GR_COMBINE_FUNCTION_SCALE_OTHER_MINUS_LOCAL_ADD_LOCAL_ALPHA 0x8
#define GR_COMBINE_FUNCTION_SCALE_MINUS_LOCAL_ADD_LOCAL 0x9
#define GR_COMBINE_FUNCTION_BLEND_LOCAL GR_COMBINE_FUNCTION_SCALE_MINUS_LOCAL_ADD_LOCAL
#define GR_COMBINE_FUNCTION_SCALE_MINUS_LOCAL_ADD_LOCAL_ALPHA 0x10

typedef FxI32 GrCombineFactor_t;
#define GR_COMBINE_FACTOR_ZERO          0x0
#define GR_COMBINE_FACTOR_NONE          GR_COMBINE_FACTOR_ZERO
#define GR_COMBINE_FACTOR_LOCAL         0x1
#define GR_COMBINE_FACTOR_OTHER_ALPHA   0x2
#define GR_COMBINE_FACTOR_LOCAL_ALPHA   0x3
#define GR_COMBINE_FACTOR_TEXTURE_ALPHA 0x4
#define GR_COMBINE_FACTOR_TEXTURE_RGB   0x5
#define GR_COMBINE_FACTOR_DETAIL_FACTOR GR_COMBINE_FACTOR_TEXTURE_ALPHA
#define GR_COMBINE_FACTOR_LOD_FRACTION  0x5
#define GR_COMBINE_FACTOR_ONE           0x8
#define GR_COMBINE_FACTOR_ONE_MINUS_LOCAL 0x9
#define GR_COMBINE_FACTOR_ONE_MINUS_OTHER_ALPHA 0xa
#define GR_COMBINE_FACTOR_ONE_MINUS_LOCAL_ALPHA 0xb
#define GR_COMBINE_FACTOR_ONE_MINUS_TEXTURE_ALPHA 0xc
#define GR_COMBINE_FACTOR_ONE_MINUS_DETAIL_FACTOR GR_COMBINE_FACTOR_ONE_MINUS_TEXTURE_ALPHA
#define GR_COMBINE_FACTOR_ONE_MINUS_LOD_FRACTION 0xd

typedef FxI32 GrCombineLocal_t;
#define GR_COMBINE_LOCAL_ITERATED   0x0
#define GR_COMBINE_LOCAL_CONSTANT   0x1
#define GR_COMBINE_LOCAL_NONE       GR_COMBINE_LOCAL_CONSTANT
#define GR_COMBINE_LOCAL_DEPTH      0x2

typedef FxI32 GrCombineOther_t;
#define GR_COMBINE_OTHER_ITERATED   0x0
#define GR_COMBINE_OTHER_TEXTURE    0x1
#define GR_COMBINE_OTHER_CONSTANT   0x2
#define GR_COMBINE_OTHER_NONE       GR_COMBINE_OTHER_CONSTANT

/***************************************************************************
    ALPHA/BLEND FUNCTIONS
***************************************************************************/

typedef FxI32 GrAlphaSource_t;
#define GR_ALPHASOURCE_CC_ALPHA         0x0
#define GR_ALPHASOURCE_ITERATED_ALPHA   0x1
#define GR_ALPHASOURCE_TEXTURE_ALPHA    0x2
#define GR_ALPHASOURCE_TEXTURE_ALPHA_TIMES_ITERATED_ALPHA 0x3

typedef FxI32 GrAlphaBlendFnc_t;
#define GR_BLEND_ZERO                   0x0
#define GR_BLEND_SRC_ALPHA              0x1
#define GR_BLEND_SRC_COLOR              0x2
#define GR_BLEND_DST_COLOR              GR_BLEND_SRC_COLOR
#define GR_BLEND_DST_ALPHA              0x3
#define GR_BLEND_ONE                    0x4
#define GR_BLEND_ONE_MINUS_SRC_ALPHA    0x5
#define GR_BLEND_ONE_MINUS_SRC_COLOR    0x6
#define GR_BLEND_ONE_MINUS_DST_COLOR    GR_BLEND_ONE_MINUS_SRC_COLOR
#define GR_BLEND_ONE_MINUS_DST_ALPHA    0x7
#define GR_BLEND_RESERVED_8             0x8
#define GR_BLEND_RESERVED_9             0x9
#define GR_BLEND_RESERVED_A             0xa
#define GR_BLEND_RESERVED_B             0xb
#define GR_BLEND_RESERVED_C             0xc
#define GR_BLEND_RESERVED_D             0xd
#define GR_BLEND_RESERVED_E             0xe
#define GR_BLEND_ALPHA_SATURATE         0xf
#define GR_BLEND_PREFOG_COLOR           GR_BLEND_ALPHA_SATURATE

typedef FxI32 GrCmpFnc_t;
#define GR_CMP_NEVER    0x0
#define GR_CMP_LESS     0x1
#define GR_CMP_EQUAL    0x2
#define GR_CMP_LEQUAL   0x3
#define GR_CMP_GREATER  0x4
#define GR_CMP_NOTEQUAL 0x5
#define GR_CMP_GEQUAL   0x6
#define GR_CMP_ALWAYS   0x7

/***************************************************************************
    BUFFER TYPES
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
***************************************************************************/

typedef FxI32 GrLfbWriteMode_t;
#define GR_LFBWRITEMODE_565        0x0
#define GR_LFBWRITEMODE_555        0x1
#define GR_LFBWRITEMODE_1555       0x2
#define GR_LFBWRITEMODE_RESERVED1  0x3
#define GR_LFBWRITEMODE_888        0x4
#define GR_LFBWRITEMODE_8888       0x5
#define GR_LFBWRITEMODE_RESERVED2  0x6
#define GR_LFBWRITEMODE_RESERVED3  0x7
#define GR_LFBWRITEMODE_RESERVED4  0x8
#define GR_LFBWRITEMODE_RESERVED5  0x9
#define GR_LFBWRITEMODE_RESERVED6  0xa
#define GR_LFBWRITEMODE_RESERVED7  0xb
#define GR_LFBWRITEMODE_565_DEPTH  0xc
#define GR_LFBWRITEMODE_555_DEPTH  0xd
#define GR_LFBWRITEMODE_1555_DEPTH 0xe
#define GR_LFBWRITEMODE_ZA16       0xf
#define GR_LFBWRITEMODE_ANY        0xFF

typedef FxI32 GrLock_t;
#define GR_LFB_READ_ONLY  0x00
#define GR_LFB_WRITE_ONLY 0x01
#define GR_LFB_IDLE       0x00
#define GR_LFB_NOIDLE     0x10

typedef struct {
    int            size;
    void          *lfbPtr;
    FxU32          strideInBytes;
    GrLfbWriteMode_t writeMode;
    GrOriginLocation_t origin;
} GrLfbInfo_t;

/***************************************************************************
    TEXTURE FILTER/CLAMP/MIPMAP ENUMS
***************************************************************************/

typedef FxI32 GrTextureFilterMode_t;
#define GR_TEXTUREFILTER_POINT_SAMPLED  0x0
#define GR_TEXTUREFILTER_BILINEAR       0x1

typedef FxI32 GrTextureClampMode_t;
#define GR_TEXTURECLAMP_WRAP    0x0
#define GR_TEXTURECLAMP_CLAMP   0x1

typedef FxI32 GrMipMapMode_t;
#define GR_MIPMAP_DISABLE               0x0
#define GR_MIPMAP_NEAREST               0x1
#define GR_MIPMAP_NEAREST_DITHER        0x2

/***************************************************************************
    FOG/DITHER/CHROMAKEY/CULL ENUMS
***************************************************************************/

typedef FxI32 GrFogMode_t;
#define GR_FOG_DISABLE          0x0
#define GR_FOG_WITH_TABLE_ON_FOGCOORD_EXT 0x1
#define GR_FOG_WITH_TABLE_ON_Q  0x2
#define GR_FOG_WITH_TABLE_ON_W  GR_FOG_WITH_TABLE_ON_Q
#define GR_FOG_WITH_ITERATED_Z  0x3
#define GR_FOG_WITH_ITERATED_ALPHA_EXT 0x4
#define GR_FOG_MULT2            0x100
#define GR_FOG_ADD2             0x200

typedef FxI32 GrDitherMode_t;
#define GR_DITHER_DISABLE       0x0
#define GR_DITHER_2x2           0x1
#define GR_DITHER_4x4           0x2

typedef FxI32 GrChromakeyMode_t;
#define GR_CHROMAKEY_DISABLE    0x0
#define GR_CHROMAKEY_ENABLE     0x1

typedef FxI32 GrCullMode_t;
#define GR_CULL_DISABLE         0x0
#define GR_CULL_NEGATIVE        0x1
#define GR_CULL_POSITIVE        0x2

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
#define GR_WINDOW_COORDS        0x0
#define GR_CLIP_COORDS          0x1

/* Draw modes for grDrawVertexArray */
#define GR_POINTS                       0
#define GR_LINES                        2
#define GR_TRIANGLE_STRIP               4
#define GR_TRIANGLE_FAN                 5
#define GR_TRIANGLES                    6
#define GR_TRIANGLE_STRIP_CONTINUE      7
#define GR_TRIANGLE_FAN_CONTINUE        8

/* grGet parameter names - values from official Glide SDK */
#define GR_BITS_DEPTH                   0x01
#define GR_BITS_RGBA                    0x02
#define GR_GAMMA_TABLE_ENTRIES          0x05
#define GR_MAX_TEXTURE_SIZE             0x0a
#define GR_MAX_TEXTURE_ASPECT_RATIO     0x0b
#define GR_MEMORY_FB                    0x0c
#define GR_MEMORY_TMU                   0x0d
#define GR_MEMORY_UMA                   0x0e
#define GR_NUM_BOARDS                   0x0f
#define GR_NUM_FB                       0x11
#define GR_NUM_SWAP_HISTORY_BUFFER      0x12
#define GR_NUM_TMU                      0x13
#define GR_TEXTURE_ALIGN                0x24
#define GR_BITS_GAMMA                   0x2a

/* grGetString parameter names - values from official Glide SDK */
#define GR_EXTENSION            0xa0
#define GR_HARDWARE             0xa1
#define GR_RENDERER             0xa2
#define GR_VENDOR               0xa3
#define GR_VERSION              0xa4

/***************************************************************************
    GLIDE API FUNCTIONS
***************************************************************************/

/* Initialization */
FX_ENTRY void FX_CALL grGlideInit(void);
FX_ENTRY void FX_CALL grGlideShutdown(void);
FX_ENTRY void FX_CALL grGlideGetVersion(char version[80]);

/* Context Management */
FX_ENTRY GrContext_t FX_CALL grSstWinOpen(
    FxU32 hwnd,
    GrScreenResolution_t resolution,
    GrScreenRefresh_t refresh,
    GrColorFormat_t colorFormat,
    GrOriginLocation_t origin,
    int numColorBuffers,
    int numAuxBuffers
);
FX_ENTRY FxBool FX_CALL grSstWinClose(GrContext_t context);
FX_ENTRY FxBool FX_CALL grSelectContext(GrContext_t context);

/* Hardware Query */
FX_ENTRY FxBool FX_CALL grSstQueryHardware(GrHwConfiguration *hwconfig);
FX_ENTRY FxU32 FX_CALL grSstQueryBoards(GrHwConfiguration *hwconfig);
FX_ENTRY void FX_CALL grSstSelect(int which_sst);

/* Buffer Operations */
FX_ENTRY void FX_CALL grBufferClear(GrColor_t color, GrAlpha_t alpha, FxU32 depth);
FX_ENTRY void FX_CALL grBufferSwap(FxU32 swap_interval);
FX_ENTRY FxBool FX_CALL grLfbLock(GrLock_t type, GrBuffer_t buffer, GrLfbWriteMode_t writeMode,
                 GrOriginLocation_t origin, FxBool pixelPipeline, GrLfbInfo_t *info);
FX_ENTRY FxBool FX_CALL grLfbUnlock(GrLock_t type, GrBuffer_t buffer);
FX_ENTRY FxBool FX_CALL grLfbWriteRegion(GrBuffer_t dst_buffer, FxU32 dst_x, FxU32 dst_y,
                         GrLfbSrcFmt_t src_format, FxU32 src_width, FxU32 src_height,
                         FxBool pixelPipeline, FxI32 src_stride, void *src_data);
FX_ENTRY FxBool FX_CALL grLfbReadRegion(GrBuffer_t src_buffer, FxU32 src_x, FxU32 src_y,
                        FxU32 src_width, FxU32 src_height,
                        FxU32 dst_stride, void *dst_data);

/* Rendering State */
FX_ENTRY void FX_CALL grColorCombine(GrCombineFunction_t function, GrCombineFactor_t factor,
                    GrCombineLocal_t local, GrCombineOther_t other, FxBool invert);
FX_ENTRY void FX_CALL grAlphaCombine(GrCombineFunction_t function, GrCombineFactor_t factor,
                    GrCombineLocal_t local, GrCombineOther_t other, FxBool invert);
FX_ENTRY void FX_CALL grAlphaBlendFunction(GrAlphaBlendFnc_t rgb_sf, GrAlphaBlendFnc_t rgb_df,
                          GrAlphaBlendFnc_t alpha_sf, GrAlphaBlendFnc_t alpha_df);
FX_ENTRY void FX_CALL grAlphaTestFunction(GrCmpFnc_t function);
FX_ENTRY void FX_CALL grAlphaTestReferenceValue(GrAlpha_t value);

typedef FxI32 GrDepthBufferMode_t;
#define GR_DEPTHBUFFER_DISABLE      0x0
#define GR_DEPTHBUFFER_ZBUFFER      0x1
#define GR_DEPTHBUFFER_WBUFFER      0x2
#define GR_DEPTHBUFFER_ZBUFFER_COMPARE_ONLY 0x3
#define GR_DEPTHBUFFER_WBUFFER_COMPARE_ONLY 0x4

FX_ENTRY void FX_CALL grDepthBufferMode(GrDepthBufferMode_t mode);
FX_ENTRY void FX_CALL grDepthBufferFunction(GrCmpFnc_t function);
FX_ENTRY void FX_CALL grDepthMask(FxBool mask);
FX_ENTRY void FX_CALL grDepthBiasLevel(FxI32 level);
FX_ENTRY void FX_CALL grDitherMode(GrDitherMode_t mode);
FX_ENTRY void FX_CALL grChromakeyMode(GrChromakeyMode_t mode);
FX_ENTRY void FX_CALL grChromakeyValue(GrColor_t value);
FX_ENTRY void FX_CALL grCullMode(GrCullMode_t mode);

/* Drawing */
FX_ENTRY void FX_CALL grDrawTriangle(const GrVertex *a, const GrVertex *b, const GrVertex *c);
FX_ENTRY void FX_CALL grDrawVertexArray(FxU32 mode, FxU32 count, void *pointers);
FX_ENTRY void FX_CALL grDrawVertexArrayContiguous(FxU32 mode, FxU32 count, void *vertices, FxU32 stride);

/* Texture Management */
FX_ENTRY FxU32 FX_CALL grTexMinAddress(GrChipID_t tmu);
FX_ENTRY FxU32 FX_CALL grTexMaxAddress(GrChipID_t tmu);
FX_ENTRY void FX_CALL grTexSource(GrChipID_t tmu, FxU32 startAddress, FxU32 evenOdd, GrTexInfo *info);
FX_ENTRY void FX_CALL grTexDownloadMipMap(GrChipID_t tmu, FxU32 startAddress, FxU32 evenOdd, GrTexInfo *info);
FX_ENTRY void FX_CALL grTexDownloadMipMapLevel(GrChipID_t tmu, FxU32 startAddress, GrLOD_t thisLod,
                               GrLOD_t largeLod, GrAspectRatio_t aspectRatio,
                               GrTextureFormat_t format, FxU32 evenOdd, void *data);
FX_ENTRY FxU32 FX_CALL grTexTextureMemRequired(FxU32 evenOdd, GrTexInfo *info);
FX_ENTRY void FX_CALL grTexCombine(GrChipID_t tmu, GrCombineFunction_t rgb_function,
                  GrCombineFactor_t rgb_factor, GrCombineFunction_t alpha_function,
                  GrCombineFactor_t alpha_factor, FxBool rgb_invert, FxBool alpha_invert);
FX_ENTRY void FX_CALL grTexFilterMode(GrChipID_t tmu, GrTextureFilterMode_t minfilter_mode,
                     GrTextureFilterMode_t magfilter_mode);
FX_ENTRY void FX_CALL grTexClampMode(GrChipID_t tmu, GrTextureClampMode_t s_clamp, GrTextureClampMode_t t_clamp);
FX_ENTRY void FX_CALL grTexMipMapMode(GrChipID_t tmu, GrMipMapMode_t mode, FxBool lodBlend);
FX_ENTRY void FX_CALL grTexLodBiasValue(GrChipID_t tmu, float bias);

/* Misc */
FX_ENTRY void FX_CALL grConstantColorValue(GrColor_t value);
FX_ENTRY void FX_CALL grClipWindow(FxU32 minx, FxU32 miny, FxU32 maxx, FxU32 maxy);
FX_ENTRY void FX_CALL grRenderBuffer(GrBuffer_t buffer);
FX_ENTRY float FX_CALL grSstScreenWidth(void);
FX_ENTRY float FX_CALL grSstScreenHeight(void);
FX_ENTRY void FX_CALL grSstOrigin(GrOriginLocation_t origin);
FX_ENTRY void FX_CALL grCoordinateSpace(GrCoordinateSpaceMode_t mode);
FX_ENTRY void FX_CALL grVertexLayout(FxU32 param, FxI32 offset, FxU32 mode);
FX_ENTRY FxU32 FX_CALL grGet(FxU32 pname, FxU32 plength, FxI32 *params);
FX_ENTRY const char* FX_CALL grGetString(FxU32 pname);
FX_ENTRY void FX_CALL grFinish(void);
FX_ENTRY void FX_CALL grFlush(void);

/* Fog */
FX_ENTRY void FX_CALL grFogMode(GrFogMode_t mode);
FX_ENTRY void FX_CALL grFogColorValue(GrColor_t fogcolor);
FX_ENTRY void FX_CALL grFogTable(const GrFog_t ft[]);

/* Additional drawing functions */
FX_ENTRY void FX_CALL grDrawPoint(const void *pt);
FX_ENTRY void FX_CALL grDrawLine(const void *v1, const void *v2);
FX_ENTRY void FX_CALL grAADrawTriangle(const void *a, const void *b, const void *c,
                      FxBool ab_antialias, FxBool bc_antialias, FxBool ca_antialias);

/* Enable/Disable mode type */
typedef FxI32 GrEnableMode_t;
#define GR_PASSTHRU         0x0
#define GR_SHAMELESS_PLUG   0x1
#define GR_VIDEO_SMOOTHING  0x2
#define GR_AA_ORDERED       0x3
#define GR_ALLOW_MIPMAP_DITHER 0x4

FX_ENTRY void FX_CALL grEnable(GrEnableMode_t mode);
FX_ENTRY void FX_CALL grDisable(GrEnableMode_t mode);

/* Additional state functions */
FX_ENTRY void FX_CALL grColorMask(FxBool rgb, FxBool a);
FX_ENTRY void FX_CALL grViewport(FxI32 x, FxI32 y, FxI32 width, FxI32 height);

/* Function pointer type */
typedef void (*GrProc)(void);
typedef void (*GrErrorCallbackFnc_t)(const char *string, FxBool fatal);

/* Dynamic function lookup */
FX_ENTRY GrProc FX_CALL grGetProcAddress(char *procName);
FX_ENTRY void FX_CALL grErrorSetCallback(GrErrorCallbackFnc_t fnc);

/* Gamma functions */
FX_ENTRY void FX_CALL grLoadGammaTable(FxU32 nentries, FxU32 *red, FxU32 *green, FxU32 *blue);
FX_ENTRY void FX_CALL guGammaCorrectionRGB(float red, float green, float blue);

/* Texture table type */
typedef FxI32 GrTexTable_t;
#define GR_TEXTABLE_NCC0    0x0
#define GR_TEXTABLE_NCC1    0x1
#define GR_TEXTABLE_PALETTE 0x2
#define GR_TEXTABLE_PALETTE_6666_EXT 0x3

FX_ENTRY void FX_CALL grTexDownloadTable(GrTexTable_t type, void *data);

#ifdef __cplusplus
}
#endif

#endif /* GLIDE3X_H */
