/*
 * glide3x_texture.c - Texture management
 *
 * This module implements texture memory management and configuration:
 *   - grTexMinAddress(), grTexMaxAddress(): Query texture memory bounds
 *   - grTexSource(): Set current texture for rendering
 *   - grTexDownloadMipMap(): Download texture data to TMU
 *   - grTexDownloadMipMapLevel(): Download single mipmap level
 *   - grTexTextureMemRequired(): Calculate memory needed for texture
 *   - grTexCombine(): Configure texture combine mode
 *   - grTexFilterMode(): Set minification/magnification filtering
 *   - grTexClampMode(): Set S/T coordinate clamping/wrapping
 *   - grTexMipMapMode(): Configure mipmapping
 *   - grTexLodBiasValue(): Set LOD bias
 *   - grTexDownloadTable(): Download palette/NCC table
 *
 * TEXTURE MEMORY ARCHITECTURE:
 * Voodoo cards had dedicated texture memory per TMU (Texture Mapping Unit):
 *   - Voodoo 1: 2MB per TMU (1 TMU standard, 2 with SLI)
 *   - Voodoo 2: 2-4MB per TMU (2 TMUs standard)
 *   - Voodoo 3+: Shared texture/framebuffer memory (AGP textures)
 *
 * TEXTURE MEMORY IS NOT VIRTUAL:
 * Unlike modern GPUs with virtual memory and automatic paging, Voodoo
 * required explicit texture management:
 *
 *   1. Query available space: grTexMinAddress(), grTexMaxAddress()
 *   2. Allocate manually (no Glide function - app tracks addresses)
 *   3. Download data: grTexDownloadMipMap()
 *   4. Activate: grTexSource()
 *
 * Applications typically implemented a simple "texture cache":
 *   - Download frequently used textures once
 *   - Replace LRU (least recently used) textures when memory full
 *   - Track texture addresses in application data structures
 *
 * TEXTURE FORMATS (GR_TEXFMT_*):
 *
 * 8-bit formats (1 byte per texel):
 *   RGB_332:    3-3-2 bits for R-G-B (256 colors)
 *   YIQ_422:    Compressed format for video
 *   ALPHA_8:    8-bit alpha only (grayscale transparency mask)
 *   INTENSITY_8: 8-bit intensity (grayscale, used for lightmaps)
 *   ALPHA_INTENSITY_44: 4-bit alpha + 4-bit intensity
 *   P_8:        8-bit paletted (look up in 256-entry color table)
 *
 * 16-bit formats (2 bytes per texel):
 *   RGB_565:    5-6-5 bits for R-G-B (65536 colors, no alpha)
 *   ARGB_1555:  1 bit alpha + 5-5-5 RGB (32768 colors, binary alpha)
 *   ARGB_4444:  4-4-4-4 bits for A-R-G-B (4096 colors + 16 alpha levels)
 *   ALPHA_INTENSITY_88: 8-bit alpha + 8-bit intensity
 *   AP_88:      8-bit alpha + 8-bit palette index
 *
 * PALETTED TEXTURES:
 * P_8 format uses a 256-entry lookup table (palette) to map 8-bit indices
 * to 32-bit ARGB colors. Benefits:
 *   - Half the memory of 16-bit textures
 *   - Can represent wider color range than RGB_332
 *   - Palette animation effects (change palette, texture changes)
 *   - Common for character sprites, environmental textures
 *
 * The palette is downloaded via grTexDownloadTable() and applied to
 * all P_8 textures on that TMU.
 *
 * MIPMAPPING:
 * Mipmaps are pre-filtered smaller versions of a texture, used to
 * avoid aliasing (shimmering) when textures are viewed at a distance.
 *
 * Mipmap levels are numbered by log2(size):
 *   LOD 8: 256x256 (largest)
 *   LOD 7: 128x128
 *   LOD 6: 64x64
 *   LOD 5: 32x32
 *   LOD 4: 16x16
 *   LOD 3: 8x8
 *   LOD 2: 4x4
 *   LOD 1: 2x2
 *   LOD 0: 1x1 (smallest)
 *
 * The hardware selects the appropriate LOD based on texel-to-pixel ratio.
 * When between levels, trilinear filtering blends two LODs for smoothness.
 *
 * ASPECT RATIO:
 * Textures don't have to be square. Glide supports power-of-two ratios:
 *   8:1, 4:1, 2:1, 1:1, 1:2, 1:4, 1:8
 *
 * Non-square textures save memory when content is naturally non-square
 * (e.g., 256x32 for a terrain strip, 64x256 for a tall banner).
 *
 * TEXTURE COORDINATES:
 * S and T are the texture coordinate axes (equivalent to U and V).
 * Coordinate range depends on texture size:
 *   For 256x256: S,T = 0.0 to 255.0 maps across entire texture
 *   For 128x128: S,T = 0.0 to 127.0 maps across entire texture
 *
 * Coordinates outside this range are either wrapped (tiled) or clamped
 * based on grTexClampMode() setting.
 */

#include "glide3x_state.h"

/*
 * Helper: Get bytes per texel for a format
 */
static int get_texel_bytes(GrTextureFormat_t format)
{
    switch (format) {
    case GR_TEXFMT_8BIT:
    case GR_TEXFMT_YIQ_422:
    case GR_TEXFMT_ALPHA_8:
    case GR_TEXFMT_INTENSITY_8:
    case GR_TEXFMT_ALPHA_INTENSITY_44:
    case GR_TEXFMT_P_8:
        return 1;
    case GR_TEXFMT_RGB_565:
    case GR_TEXFMT_ARGB_1555:
    case GR_TEXFMT_ARGB_4444:
    case GR_TEXFMT_ALPHA_INTENSITY_88:
    case GR_TEXFMT_AP_88:
        return 2;
    default:
        return 1;
    }
}

/*
 * Helper: Get Voodoo format index for TEXTUREMODE register
 */
static int get_voodoo_format(GrTextureFormat_t format)
{
    switch (format) {
    case GR_TEXFMT_8BIT:           return 0;
    case GR_TEXFMT_YIQ_422:        return 1;
    case GR_TEXFMT_ALPHA_8:        return 2;
    case GR_TEXFMT_INTENSITY_8:    return 3;
    case GR_TEXFMT_ALPHA_INTENSITY_44: return 4;
    case GR_TEXFMT_RGB_565:        return 5;
    case GR_TEXFMT_ARGB_1555:      return 6;
    case GR_TEXFMT_ARGB_4444:      return 7;
    case GR_TEXFMT_ALPHA_INTENSITY_88: return 8;
    case GR_TEXFMT_P_8:            return 9;
    default:                       return 5;  /* Default RGB565 */
    }
}

/*
 * Helper: Get texture dimension from LOD
 */
static int get_tex_size(GrLOD_t lod)
{
    switch (lod) {
    case GR_LOD_LOG2_256: return 256;
    case GR_LOD_LOG2_128: return 128;
    case GR_LOD_LOG2_64:  return 64;
    case GR_LOD_LOG2_32:  return 32;
    case GR_LOD_LOG2_16:  return 16;
    case GR_LOD_LOG2_8:   return 8;
    case GR_LOD_LOG2_4:   return 4;
    case GR_LOD_LOG2_2:   return 2;
    case GR_LOD_LOG2_1:   return 1;
    default:              return 256;
    }
}

/*
 * grTexMinAddress - Get minimum texture memory address
 *
 * Returns the lowest valid address for texture downloads on the TMU.
 * This is always 0 in our implementation.
 */
FxU32 __stdcall grTexMinAddress(GrChipID_t tmu)
{
    LOG_FUNC();
    (void)tmu;
    return 0;
}

/*
 * grTexMaxAddress - Get maximum texture memory address
 *
 * Returns the highest valid address for texture downloads.
 * This defines the usable texture memory range: 0 to grTexMaxAddress().
 */
FxU32 __stdcall grTexMaxAddress(GrChipID_t tmu)
{
    LOG_FUNC();
    if (!g_voodoo) return 0;
    int t = (tmu == GR_TMU0) ? 0 : 1;
    return g_voodoo->tmu[t].mask;  /* mask = memory_size - 1 */
}

/*
 * grTexSource - Set current texture for rendering
 *
 * From the 3dfx SDK:
 * "grTexSource() selects a texture that has been previously downloaded
 * to TMU memory as the current texture source for rendering."
 *
 * Parameters:
 *   tmu          - Which TMU (GR_TMU0, GR_TMU1)
 *   startAddress - Byte offset where texture was downloaded
 *   evenOdd      - Mipmap level mask (usually GR_MIPMAPLEVELMASK_BOTH)
 *   info         - GrTexInfo describing the texture
 *
 * After this call, triangles rendered will use this texture
 * (assuming texturing is enabled in the color combine).
 */
void __stdcall grTexSource(GrChipID_t tmu, FxU32 startAddress, FxU32 evenOdd, GrTexInfo *info)
{
    LOG_FUNC();
    if (!g_voodoo || !info) return;

    int t = (tmu == GR_TMU0) ? 0 : 1;
    tmu_state *ts = &g_voodoo->tmu[t];

    (void)evenOdd;

    /* Set texture base address */
    ts->lodoffset[0] = startAddress & ts->mask;

    /* Calculate texture dimensions */
    int tex_width = get_tex_size(info->largeLodLog2);
    int tex_height = tex_width;

    /* Adjust for aspect ratio */
    switch (info->aspectRatioLog2) {
    case GR_ASPECT_LOG2_8x1: tex_height = tex_width >> 3; break;
    case GR_ASPECT_LOG2_4x1: tex_height = tex_width >> 2; break;
    case GR_ASPECT_LOG2_2x1: tex_height = tex_width >> 1; break;
    case GR_ASPECT_LOG2_1x1: break;
    case GR_ASPECT_LOG2_1x2: tex_width = tex_height >> 1; break;
    case GR_ASPECT_LOG2_1x4: tex_width = tex_height >> 2; break;
    case GR_ASPECT_LOG2_1x8: tex_width = tex_height >> 3; break;
    }

    ts->wmask = tex_width - 1;
    ts->hmask = tex_height - 1;

    /* Set format in TEXTUREMODE register */
    uint32_t texmode = ts->reg ? ts->reg->u : 0;
    texmode &= ~(0xF << 8);
    texmode |= (get_voodoo_format(info->format) << 8);
    if (ts->reg) ts->reg->u = texmode;

    /* Set lookup table based on format */
    switch (info->format) {
    case GR_TEXFMT_RGB_332:
        ts->lookup = g_voodoo->tmushare.rgb332;
        break;
    case GR_TEXFMT_ALPHA_8:
        ts->lookup = g_voodoo->tmushare.alpha8;
        break;
    case GR_TEXFMT_INTENSITY_8:
        ts->lookup = g_voodoo->tmushare.int8;
        break;
    case GR_TEXFMT_ALPHA_INTENSITY_44:
        ts->lookup = g_voodoo->tmushare.ai44;
        break;
    case GR_TEXFMT_P_8:
        ts->lookup = ts->palette;
        break;
    default:
        ts->lookup = NULL;  /* 16-bit formats don't need lookup */
        break;
    }

    ts->regdirty = 1;
}

/*
 * grTexDownloadMipMap - Download texture with all mipmap levels
 *
 * From the 3dfx SDK:
 * "grTexDownloadMipMap() downloads a texture mipmap to the specified TMU."
 *
 * Parameters:
 *   tmu          - Which TMU to download to
 *   startAddress - Destination address in TMU memory
 *   evenOdd      - Which levels to download (usually BOTH)
 *   info         - GrTexInfo with dimensions, format, and data pointer
 *
 * The info->data pointer should contain all mipmap levels contiguously,
 * from largest to smallest.
 */
void __stdcall grTexDownloadMipMap(GrChipID_t tmu, FxU32 startAddress, FxU32 evenOdd, GrTexInfo *info)
{
    LOG_FUNC();
    if (!g_voodoo || !info || !info->data) return;

    int t = (tmu == GR_TMU0) ? 0 : 1;
    tmu_state *ts = &g_voodoo->tmu[t];

    (void)evenOdd;

    int tex_width = get_tex_size(info->largeLodLog2);
    int tex_height = tex_width;

    switch (info->aspectRatioLog2) {
    case GR_ASPECT_LOG2_8x1: tex_height = tex_width >> 3; break;
    case GR_ASPECT_LOG2_4x1: tex_height = tex_width >> 2; break;
    case GR_ASPECT_LOG2_2x1: tex_height = tex_width >> 1; break;
    case GR_ASPECT_LOG2_1x1: break;
    case GR_ASPECT_LOG2_1x2: tex_width = tex_height >> 1; break;
    case GR_ASPECT_LOG2_1x4: tex_width = tex_height >> 2; break;
    case GR_ASPECT_LOG2_1x8: tex_width = tex_height >> 3; break;
    }

    int bpp = get_texel_bytes(info->format);
    int total_size = tex_width * tex_height * bpp;

    /* Add sizes for all mip levels if mipmapped */
    if (info->smallLodLog2 != info->largeLodLog2) {
        int w = tex_width;
        int h = tex_height;
        for (int lod = info->largeLodLog2; lod >= (int)info->smallLodLog2; lod--) {
            w = (w > 1) ? w >> 1 : 1;
            h = (h > 1) ? h >> 1 : 1;
            total_size += w * h * bpp;
        }
    }

    /* Copy to TMU RAM */
    uint32_t dest_addr = startAddress & ts->mask;
    if (dest_addr + total_size <= ts->mask + 1) {
        memcpy(&ts->ram[dest_addr], info->data, total_size);
    }

    ts->regdirty = 1;
}

/*
 * grTexDownloadMipMapLevel - Download a single mipmap level
 */
void __stdcall grTexDownloadMipMapLevel(GrChipID_t tmu, FxU32 startAddress, GrLOD_t thisLod,
                               GrLOD_t largeLod, GrAspectRatio_t aspectRatio,
                               GrTextureFormat_t format, FxU32 evenOdd, void *data)
{
    LOG_FUNC();
    if (!g_voodoo || !data) return;

    int t = (tmu == GR_TMU0) ? 0 : 1;
    tmu_state *ts = &g_voodoo->tmu[t];

    (void)evenOdd;
    (void)largeLod;

    int tex_width = get_tex_size(thisLod);
    int tex_height = tex_width;

    switch (aspectRatio) {
    case GR_ASPECT_LOG2_8x1: tex_height = tex_width >> 3; break;
    case GR_ASPECT_LOG2_4x1: tex_height = tex_width >> 2; break;
    case GR_ASPECT_LOG2_2x1: tex_height = tex_width >> 1; break;
    case GR_ASPECT_LOG2_1x1: break;
    case GR_ASPECT_LOG2_1x2: tex_width = tex_height >> 1; break;
    case GR_ASPECT_LOG2_1x4: tex_width = tex_height >> 2; break;
    case GR_ASPECT_LOG2_1x8: tex_width = tex_height >> 3; break;
    }

    if (tex_width < 1) tex_width = 1;
    if (tex_height < 1) tex_height = 1;

    int bpp = get_texel_bytes(format);
    int tex_size = tex_width * tex_height * bpp;

    uint32_t dest_addr = startAddress & ts->mask;
    if (dest_addr + tex_size <= ts->mask + 1) {
        memcpy(&ts->ram[dest_addr], data, tex_size);
    }

    ts->regdirty = 1;
}

/*
 * grTexTextureMemRequired - Calculate memory needed for a texture
 */
FxU32 __stdcall grTexTextureMemRequired(FxU32 evenOdd, GrTexInfo *info)
{
    LOG_FUNC();
    if (!info) return 0;

    (void)evenOdd;

    int tex_width = get_tex_size(info->largeLodLog2);
    int tex_height = tex_width;

    switch (info->aspectRatioLog2) {
    case GR_ASPECT_LOG2_8x1: tex_height = tex_width >> 3; break;
    case GR_ASPECT_LOG2_4x1: tex_height = tex_width >> 2; break;
    case GR_ASPECT_LOG2_2x1: tex_height = tex_width >> 1; break;
    case GR_ASPECT_LOG2_1x1: break;
    case GR_ASPECT_LOG2_1x2: tex_width = tex_height >> 1; break;
    case GR_ASPECT_LOG2_1x4: tex_width = tex_height >> 2; break;
    case GR_ASPECT_LOG2_1x8: tex_width = tex_height >> 3; break;
    }

    int bpp = get_texel_bytes(info->format);
    FxU32 total = tex_width * tex_height * bpp;

    if (info->smallLodLog2 != info->largeLodLog2) {
        int w = tex_width;
        int h = tex_height;
        for (int lod = info->largeLodLog2; lod >= (int)info->smallLodLog2; lod--) {
            w = (w > 1) ? w >> 1 : 1;
            h = (h > 1) ? h >> 1 : 1;
            total += w * h * bpp;
        }
    }

    return total;
}

/*
 * grTexCombine - Configure how TMU combines texture with input
 *
 * From the 3dfx SDK:
 * "grTexCombine() configures how a TMU combines its texture output
 * with the input from the downstream TMU (or iteration for TMU0)."
 */
void __stdcall grTexCombine(GrChipID_t tmu, GrCombineFunction_t rgb_function,
                  GrCombineFactor_t rgb_factor, GrCombineFunction_t alpha_function,
                  GrCombineFactor_t alpha_factor, FxBool rgb_invert, FxBool alpha_invert)
{
    LOG_FUNC();
    if (!g_voodoo) return;

    int t = (tmu == GR_TMU0) ? 0 : 1;
    tmu_state *ts = &g_voodoo->tmu[t];

    if (!ts->reg) return;

    uint32_t val = ts->reg->u;

    /* Clear texture combine bits */
    val &= ~((0x1FF << 12) | (0x1FF << 21));

    /* RGB combine */
    if (rgb_function == GR_COMBINE_FUNCTION_ZERO)
        val |= (1 << 12);
    if (rgb_function == GR_COMBINE_FUNCTION_LOCAL)
        val |= (1 << 13);
    val |= ((rgb_factor & 7) << 14);
    if (rgb_invert)
        val |= (1 << 20);

    /* Alpha combine */
    if (alpha_function == GR_COMBINE_FUNCTION_ZERO)
        val |= (1 << 21);
    if (alpha_function == GR_COMBINE_FUNCTION_LOCAL)
        val |= (1 << 22);
    val |= ((alpha_factor & 7) << 23);
    if (alpha_invert)
        val |= (1 << 29);

    ts->reg->u = val;
}

/*
 * grTexFilterMode - Set texture filtering modes
 */
void __stdcall grTexFilterMode(GrChipID_t tmu, GrTextureFilterMode_t minfilter_mode,
                     GrTextureFilterMode_t magfilter_mode)
{
    LOG_FUNC();
    if (!g_voodoo) return;

    int t = (tmu == GR_TMU0) ? 0 : 1;
    tmu_state *ts = &g_voodoo->tmu[t];

    if (!ts->reg) return;

    uint32_t val = ts->reg->u;
    val &= ~(3 << 1);

    if (minfilter_mode == GR_TEXTUREFILTER_BILINEAR)
        val |= (1 << 1);
    if (magfilter_mode == GR_TEXTUREFILTER_BILINEAR)
        val |= (1 << 2);

    ts->reg->u = val;
}

/*
 * grTexClampMode - Set texture coordinate clamping/wrapping
 */
void __stdcall grTexClampMode(GrChipID_t tmu, GrTextureClampMode_t s_clamp, GrTextureClampMode_t t_clamp)
{
    LOG_FUNC();
    if (!g_voodoo) return;

    int t = (tmu == GR_TMU0) ? 0 : 1;
    tmu_state *ts = &g_voodoo->tmu[t];

    if (!ts->reg) return;

    uint32_t val = ts->reg->u;
    val &= ~((1 << 6) | (1 << 7));

    if (s_clamp == GR_TEXTURECLAMP_CLAMP)
        val |= (1 << 6);
    if (t_clamp == GR_TEXTURECLAMP_CLAMP)
        val |= (1 << 7);

    ts->reg->u = val;
}

/*
 * grTexMipMapMode - Configure mipmapping mode
 */
void __stdcall grTexMipMapMode(GrChipID_t tmu, GrMipMapMode_t mode, FxBool lodBlend)
{
    LOG_FUNC();
    if (!g_voodoo) return;

    int t = (tmu == GR_TMU0) ? 0 : 1;
    tmu_state *ts = &g_voodoo->tmu[t];

    if (mode == GR_MIPMAP_DISABLE) {
        ts->lodmin = 0;
        ts->lodmax = 0;
    } else {
        ts->lodmin = 0;
        ts->lodmax = 8;
    }

    (void)lodBlend;
}

/*
 * grTexLodBiasValue - Set LOD bias
 */
void __stdcall grTexLodBiasValue(GrChipID_t tmu, float bias)
{
    LOG_FUNC();
    if (!g_voodoo) return;

    int t = (tmu == GR_TMU0) ? 0 : 1;
    tmu_state *ts = &g_voodoo->tmu[t];

    ts->lodbias = (int32_t)(bias * 16.0f);
}

/*
 * grTexDownloadTable - Download palette or NCC table
 */
void __stdcall grTexDownloadTable(GrTexTable_t type, void *data)
{
    LOG_FUNC();
    if (!g_voodoo || !data) return;

    LOG("  type=%d", type);

    for (int t = 0; t < 2; t++) {
        tmu_state *ts = &g_voodoo->tmu[t];

        switch (type) {
        case GR_TEXTABLE_NCC0:
        case GR_TEXTABLE_NCC1:
            if (t == 0) {
                LOG("  WARNING: NCC Table download not fully implemented");
            }
            break;

        case GR_TEXTABLE_PALETTE:
        case GR_TEXTABLE_PALETTE_6666_EXT:
            {
                const uint32_t* pal = (const uint32_t*)data;
                for (int i = 0; i < 256; i++) {
                    ts->palette[i] = pal[i];
                }
            }
            break;

        default:
            if (t == 0) LOG("  Unknown table type %d", type);
            break;
        }

        ts->regdirty = 1;
    }
}
