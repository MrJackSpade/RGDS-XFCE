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
/*
 * Voodoo hardware texture format mapping:
 *   0: RGB 3-3-2 (8-bit)
 *   1: YIQ 4-2-2 (8-bit, NCC)
 *   2: A 8 (8-bit alpha)
 *   3: I 8 (8-bit intensity)
 *   4: AI 4-4 (8-bit alpha-intensity)
 *   5: P 8 (8-bit palette)
 *   6: P 8 with alpha from texel
 *   7: Reserved
 *   8: ARGB 8-3-3-2 (16-bit)
 *   9: AYIQ 8-4-2-2 (16-bit, NCC with alpha)
 *  10: RGB 5-6-5 (16-bit)
 *  11: ARGB 1-5-5-5 (16-bit)
 *  12: ARGB 4-4-4-4 (16-bit)
 *  13: AI 8-8 (16-bit alpha-intensity)
 *  14: AP 8-8 (16-bit alpha + palette)
 *  15: Reserved
 *
 * Format < 8: 8-bit textures (1 byte per texel)
 * Format >= 8: 16-bit textures (2 bytes per texel)
 */
static int get_voodoo_format(GrTextureFormat_t format)
{
    switch (format) {
    case GR_TEXFMT_8BIT:               return 0;   /* RGB 3-3-2 (same as GR_TEXFMT_RGB_332) */
    case GR_TEXFMT_YIQ_422:            return 1;   /* YIQ 4-2-2 */
    case GR_TEXFMT_ALPHA_8:            return 2;   /* A 8 */
    case GR_TEXFMT_INTENSITY_8:        return 3;   /* I 8 */
    case GR_TEXFMT_ALPHA_INTENSITY_44: return 4;   /* AI 4-4 */
    case GR_TEXFMT_P_8:                return 5;   /* P 8 */
    case GR_TEXFMT_RGB_565:            return 10;  /* RGB 5-6-5 */
    case GR_TEXFMT_ARGB_1555:          return 11;  /* ARGB 1-5-5-5 */
    case GR_TEXFMT_ARGB_4444:          return 12;  /* ARGB 4-4-4-4 */
    case GR_TEXFMT_ALPHA_INTENSITY_88: return 13;  /* AI 8-8 */
    default:                           return 10;  /* Default RGB565 */
    }
}

/*
 * Helper: Check if format can be pre-converted to ARGB32
 *
 * YIQ_422 (NCC) and AP_88 formats are not pre-converted due to complexity.
 * P_8 (palettized) IS pre-converted and tracked for reconversion on palette change.
 */
static int can_preconvert(GrTextureFormat_t format)
{
    switch (format) {
    case GR_TEXFMT_YIQ_422:  /* NCC table - too complex to track */
    case GR_TEXFMT_AP_88:    /* Alpha + palette index - rarely used */
        return 0;
    default:
        return 1;
    }
}

/*
 * Helper: Track a P_8 texture region for palette reconversion
 *
 * When the palette changes, all tracked P_8 regions will be reconverted.
 */
static void track_p8_region(tmu_state *ts, uint32_t dest_addr, int num_texels)
{
    /* Check if this region overlaps with an existing one - if so, update it */
    for (int i = 0; i < ts->p8_region_count; i++) {
        uint32_t existing_start = ts->p8_regions[i].start_addr;
        uint32_t existing_end = existing_start + ts->p8_regions[i].num_texels;
        uint32_t new_end = dest_addr + num_texels;

        /* Check for overlap */
        if (dest_addr < existing_end && new_end > existing_start) {
            /* Merge: expand to cover both regions */
            uint32_t merged_start = (dest_addr < existing_start) ? dest_addr : existing_start;
            uint32_t merged_end = (new_end > existing_end) ? new_end : existing_end;
            ts->p8_regions[i].start_addr = merged_start;
            ts->p8_regions[i].num_texels = merged_end - merged_start;
            return;
        }
    }

    /* No overlap - add new region if space available */
    if (ts->p8_region_count < MAX_P8_REGIONS) {
        ts->p8_regions[ts->p8_region_count].start_addr = dest_addr;
        ts->p8_regions[ts->p8_region_count].num_texels = num_texels;
        ts->p8_region_count++;
    }
}

/*
 * Helper: Remove P_8 tracking for a region (when overwritten by non-P_8 texture)
 */
static void untrack_p8_region(tmu_state *ts, uint32_t dest_addr, int num_texels)
{
    uint32_t new_end = dest_addr + num_texels;

    for (int i = 0; i < ts->p8_region_count; ) {
        uint32_t existing_start = ts->p8_regions[i].start_addr;
        uint32_t existing_end = existing_start + ts->p8_regions[i].num_texels;

        /* Check for overlap */
        if (dest_addr < existing_end && new_end > existing_start) {
            /* Remove this entry by swapping with last */
            ts->p8_regions[i] = ts->p8_regions[ts->p8_region_count - 1];
            ts->p8_region_count--;
            /* Don't increment i - check the swapped entry */
        } else {
            i++;
        }
    }
}

/*
 * Helper: Pre-convert texture data to ARGB32 shadow buffer
 *
 * Uses the existing lookup tables from tmushare to convert texels.
 * This eliminates lookup table indirection during texture sampling.
 *
 * Parameters:
 *   ts         - TMU state (destination)
 *   dest_addr  - Byte address in TMU RAM where texture is stored
 *   data       - Source texture data (NULL to reconvert from ts->ram)
 *   format     - Texture format
 *   num_texels - Number of texels to convert
 */
static void preconvert_texture_data(tmu_state *ts, uint32_t dest_addr,
                                    const void *data, GrTextureFormat_t format,
                                    int num_texels)
{
    (void)data;  /* Used only for distinguishing fresh uploads in debug builds */

    if (!ts->argb32_ram || !can_preconvert(format)) {
        return;
    }

    /* For P_8 textures, track the region for palette reconversion */
    if (format == GR_TEXFMT_P_8) {
        track_p8_region(ts, dest_addr, num_texels);
    } else {
        /* Non-P_8 texture may overwrite a P_8 region */
        untrack_p8_region(ts, dest_addr, num_texels);
    }

    /* Use provided data, or read from TMU RAM (for reconversion) */
    const uint8_t *src8 = data ? (const uint8_t *)data : &ts->ram[dest_addr];
    const uint16_t *src16 = (const uint16_t *)src8;
    uint32_t *dst = ts->argb32_ram;
    tmu_shared_state *share = &g_voodoo->tmushare;

    switch (format) {
    case GR_TEXFMT_8BIT:  /* RGB 3-3-2 */
        for (int i = 0; i < num_texels; i++) {
            uint32_t addr = (dest_addr + i) & ts->mask;
            dst[addr] = share->rgb332[src8[i]];
        }
        break;

    case GR_TEXFMT_ALPHA_8:
        for (int i = 0; i < num_texels; i++) {
            uint32_t addr = (dest_addr + i) & ts->mask;
            dst[addr] = share->alpha8[src8[i]];
        }
        break;

    case GR_TEXFMT_INTENSITY_8:
        for (int i = 0; i < num_texels; i++) {
            uint32_t addr = (dest_addr + i) & ts->mask;
            dst[addr] = share->int8[src8[i]];
        }
        break;

    case GR_TEXFMT_ALPHA_INTENSITY_44:
        for (int i = 0; i < num_texels; i++) {
            uint32_t addr = (dest_addr + i) & ts->mask;
            dst[addr] = share->ai44[src8[i]];
        }
        break;

    case GR_TEXFMT_P_8:  /* Palettized - use current palette */
        for (int i = 0; i < num_texels; i++) {
            uint32_t addr = (dest_addr + i) & ts->mask;
            dst[addr] = ts->palette[src8[i]];
        }
        break;

    case GR_TEXFMT_RGB_565:
        for (int i = 0; i < num_texels; i++) {
            uint32_t addr = (dest_addr + i * 2) & ts->mask;
            dst[addr] = share->rgb565[src16[i]];
        }
        break;

    case GR_TEXFMT_ARGB_1555:
        for (int i = 0; i < num_texels; i++) {
            uint32_t addr = (dest_addr + i * 2) & ts->mask;
            dst[addr] = share->argb1555[src16[i]];
        }
        break;

    case GR_TEXFMT_ARGB_4444:
        for (int i = 0; i < num_texels; i++) {
            uint32_t addr = (dest_addr + i * 2) & ts->mask;
            dst[addr] = share->argb4444[src16[i]];
        }
        break;

    case GR_TEXFMT_ALPHA_INTENSITY_88:
        /* AI88: high byte = alpha, low byte = intensity (grayscale) */
        for (int i = 0; i < num_texels; i++) {
            uint32_t addr = (dest_addr + i * 2) & ts->mask;
            uint16_t val = src16[i];
            int a = (val >> 8) & 0xFF;
            int intensity = val & 0xFF;
            dst[addr] = ((uint32_t)a << 24) | ((uint32_t)intensity << 16) |
                        ((uint32_t)intensity << 8) | (uint32_t)intensity;
        }
        break;

    default:
        /* Format not supported for pre-conversion */
        break;
    }
}

/*
 * Helper: Reconvert all P_8 texture regions after palette change
 */
static void reconvert_p8_textures(tmu_state *ts)
{
    if (!ts->argb32_ram) return;

    for (int i = 0; i < ts->p8_region_count; i++) {
        uint32_t addr = ts->p8_regions[i].start_addr;
        int num_texels = ts->p8_regions[i].num_texels;

        /* Reconvert from TMU RAM using new palette */
        const uint8_t *src = &ts->ram[addr];
        uint32_t *dst = ts->argb32_ram;

        for (int j = 0; j < num_texels; j++) {
            uint32_t texel_addr = (addr + j) & ts->mask;
            dst[texel_addr] = ts->palette[src[j]];
        }
    }
}

/*
 * Helper: Get texture dimension from LOD
 *
 * Glide 3.x LOD = log2(size), so size = 2^LOD = 1 << LOD
 * GR_LOD_LOG2_256=8, GR_LOD_LOG2_1=0
 */
static int get_tex_size(GrLOD_t lod)
{
    if (lod < 0) lod = 0;
    if (lod > 8) lod = 8;
    return 1 << lod;  /* 2^lod: lod=8 -> 256, lod=0 -> 1 */
}

/*---------------------------------------------------------------------------
** _grTexCalcBaseAddress support tables and function
** Copied from SDK ditex.c
**---------------------------------------------------------------------------*/

#define G3_ASPECT_TRANSLATE(__aspect) (0x3 - (__aspect))
#define SST_TEXTURE_ALIGN       0x10UL
#define SST_TEXTURE_ALIGN_MASK  (SST_TEXTURE_ALIGN - 0x01UL)

/* Bits per texel for each format - SDK ditex.c line 233 */
static const FxU32 _grBitsPerTexel[] =
{
  0x08,  /* GR_TEXFMT_8BIT == GR_TEXFMT_RGB_332    */
  0x08,  /* GR_TEXFMT_YIQ_422                      */
  0x08,  /* GR_TEXFMT_ALPHA_8                      */
  0x08,  /* GR_TEXFMT_INTENSITY_8                  */
  0x08,  /* GR_TEXFMT_ALPHA_INTENSITY_44           */
  0x08,  /* GR_TEXFMT_P_8                          */
  0x08,  /* GR_TEXFMT_RSVD0 == GR_TEXFMT_P_8_6666  */
  0x10,  /* GR_TEXFMT_RSVD1                        */
  0x10,  /* GR_TEXFMT_16BIT == GR_TEXFMT_ARGB_8332 */
  0x10,  /* GR_TEXFMT_AYIQ_8422                    */
  0x10,  /* GR_TEXFMT_RGB_565                      */
  0x10,  /* GR_TEXFMT_ARGB_1555                    */
  0x10,  /* GR_TEXFMT_ARGB_4444                    */
  0x10,  /* GR_TEXFMT_ALPHA_INTENSITY_88           */
  0x10,  /* GR_TEXFMT_AP_88                        */
  0x00,  /* GR_TEXFMT_RSVD2                        */
};

/* Mipmap offset table - SDK ditex.c line 1064 */
static const FxI32 _grMipMapOffset[4][16] =
{
  {  /* 8:1 and 1:8 aspect ratios */
      10927, /* Sum(256x32, 128x16, 64x8, 32x4, 16x2, 8x1, 4x1, 2x1, 1x1)    */
      10926, /* Sum(256x32, 128x16, 64x8, 32x4, 16x2, 8x1, 4x1, 2x1)         */
      10924, /* Sum(256x32, 128x16, 64x8, 32x4, 16x2, 8x1, 4x1)              */
      10920, /* Sum(256x32, 128x16, 64x8, 32x4, 16x2, 8x1)                   */
      10912, /* Sum(256x32, 128x16, 64x8, 32x4, 16x2)                        */
      10880, /* Sum(256x32, 128x16, 64x8, 32x4)                              */
      10752, /* Sum(256x32, 128x16, 64x8)                                    */
      10240, /* Sum(256x32, 128x16)                                          */
       8192, /* Sum(256x32)                                                  */
          0, /* Base address (beginning of 256x32 level)                     */
     -32768, /* - Sum(512x64)                                                */
    -163840, /* - Sum(1024x128, 512x64)                                      */
    -688128, /* - Sum(2048x256, 1024x128, 512x64)                            */
  },
  {  /* 4:1 and 1:4 aspect ratios */
       21847, /* Sum(256x64, 128x32, 64x16, 32x8, 16x4, 8x2, 4x1, 2x1, 1x1)  */
       21846, /* Sum(256x64, 128x32, 64x16, 32x8, 16x4, 8x2, 4x1, 2x1)       */
       21844, /* Sum(256x64, 128x32, 64x16, 32x8, 16x4, 8x2, 4x1)            */
       21840, /* Sum(256x64, 128x32, 64x16, 32x8, 16x4, 8x2)                 */
       21824, /* Sum(256x64, 128x32, 64x16, 32x8, 16x4)                      */
       21760, /* Sum(256x64, 128x32, 64x16, 32x8)                            */
       21504, /* Sum(256x64, 128x32, 64x16)                                  */
       20480, /* Sum(256x64, 128x32)                                         */
       16384, /* Sum(256x64)                                                 */
           0, /* Base address (beginning of 256x64 level)                    */
      -65536, /* - Sum(512x128)                                              */
     -327680, /* - Sum(1024x256, 512x128)                                    */
    -1376256, /* - Sum(2048x512, 1024x256, 512x128)                          */
  },
  {  /* 2:1 and 1:2 aspect ratios */
       43691, /* Sum(256x128, 128x64, 64x32, 32x16, 16x8, 8x4, 4x2, 2x1, 1x1)*/
       43690, /* Sum(256x128, 128x64, 64x32, 32x16, 16x8, 8x4, 4x2, 2x1)     */
       43688, /* Sum(256x128, 128x64, 64x32, 32x16, 16x8, 8x4, 4x2)          */
       43680, /* Sum(256x128, 128x64, 64x32, 32x16, 16x8, 8x4)               */
       43648, /* Sum(256x128, 128x64, 64x32, 32x16, 16x8)                    */
       43520, /* Sum(256x128, 128x64, 64x32, 32x16)                          */
       43008, /* Sum(256x128, 128x64, 64x32)                                 */
       40960, /* Sum(256x128, 128x64)                                        */
       32768, /* Sum(256x128)                                                */
           0, /* Base address (beginning of 256x128 level)                   */
     -131072, /* - Sum(512x256)                                              */
     -655360, /* - Sum(1024x512, 512x256)                                    */
    -2752512, /* - Sum(2048x1024, 1024x512, 512x256)                         */
  },
  {  /* 1:1 aspect ratio */
       87381, /* Sum(256x256, 128x128, 64x64, 32x32, 16x16, 8x8, 4x4, 2x2, 1)*/
       87380, /* Sum(256x256, 128x128, 64x64, 32x32, 16x16, 8x8, 4x4, 2x2)   */
       87376, /* Sum(256x256, 128x128, 64x64, 32x32, 16x16, 8x8, 4x4)        */
       87360, /* Sum(256x256, 128x128, 64x64, 32x32, 16x16, 8x8)             */
       87296, /* Sum(256x256, 128x128, 64x64, 32x32, 16x16)                  */
       87040, /* Sum(256x256, 128x128, 64x64, 32x32)                         */
       86016, /* Sum(256x256, 128x128, 64x64)                                */
       81920, /* Sum(256x256, 128x128)                                       */
       65536, /* Sum(256x256)                                                */
           0, /* Base address (beginning of 256x256 level)                   */
     -262144, /* - Sum(512x512)                                              */
    -1310720, /* - Sum(1024x1024, 512x512)                                   */
    -5505024, /* - Sum(2048x2048, 1024x1024, 512x512)                        */
  },
};

/* Mipmap offset table for trilinear split - SDK ditex.c line 1316 */
static const FxI32 _grMipMapOffset_Tsplit[4][16] =
{
  {  /* 8:1 and 1:8 aspect ratios */
        8741, /* Sum(256x32, 64x8, 16x2, 4x1, 1x1)         */
        2186, /* Sum(128x16, 32x4, 8x1, 2x1)               */
        8740, /* Sum(256x32, 64x8, 16x2, 4x1)              */
        2184, /* Sum(128x16, 32x4, 8x1)                    */
        8736, /* Sum(256x32, 64x8, 16x2)                   */
        2176, /* Sum(128x16, 32x4)                         */
        8704, /* Sum(256x32, 64x8)                         */
        2048, /* Sum(128x16)                               */
        8192, /* Sum(256x32)                               */
           0, /* Base address (beginning of 128x16 level)  */
           0, /* Base address (beginning of 256x32 level)  */
      -32768, /* - Sum(512x64)                             */
     -131072, /* - Sum(1024x128)                           */
     -557056, /* - Sum(2048x256, 512x64)                   */
  },
  {  /* 4:1 and 1:4 aspect ratios */
       17477, /* Sum(256x64, 64x16, 16x4, 4x1, 1x1)        */
        4370, /* Sum(128x32, 32x8, 8x2, 2x1)               */
       17476, /* Sum(256x64, 64x16, 16x4, 4x1)             */
        4368, /* Sum(128x32, 32x8, 8x2)                    */
       17472, /* Sum(256x64, 64x16, 16x4)                  */
        4352, /* Sum(128x32, 32x8)                         */
       17408, /* Sum(256x64, 64x16)                        */
        4096, /* Sum(128x32)                               */
       16384, /* Sum(256x64)                               */
           0, /* Base address (beginning of 128x32 level)  */
           0, /* Base address (beginning of 256x64 level)  */
      -65536, /* - Sum(512x128)                            */
     -262144, /* - Sum(1024x256)                           */
    -1114112, /* - Sum(2048x512, 512x128)                  */
  },
  {  /* 2:1 and 1:2 aspect ratios */
       34953, /* Sum(256x128, 64x32, 16x8, 4x2, 1x1)       */
        8738, /* Sum(128x64, 32x16, 8x4, 2x1)              */
       34952, /* Sum(256x128, 64x32, 16x8, 4x2)            */
        8736, /* Sum(128x64, 32x16, 8x4)                   */
       34944, /* Sum(256x128, 64x32, 16x8)                 */
        8704, /* Sum(128x64, 32x16)                        */
       34816, /* Sum(256x128, 64x32)                       */
        8192, /* Sum(128x64)                               */
       32768, /* Sum(256x128)                              */
           0, /* Base address (beginning of 128x64 level)  */
           0, /* Base address (beginning of 256x128 level) */
     -131072, /* - Sum(512x256)                            */
     -524288, /* - Sum(1024x512)                           */
    -2228224, /* - Sum(2048x1024, 512x256)                 */
  },
  {  /* 1:1 aspect ratio */
       69905, /* Sum(256x256, 64x64, 16x16, 4x4, 1x1)      */
       17476, /* Sum(128x128, 32x32, 8x8, 2x2)             */
       69904, /* Sum(256x256, 64x64, 16x16, 4x4)           */
       17472, /* Sum(128x128, 32x32, 8x8)                  */
       69888, /* Sum(256x256, 64x64, 16x16)                */
       17408, /* Sum(128x128, 32x32)                       */
       69632, /* Sum(256x256, 64x64)                       */
       16384, /* Sum(128x128)                              */
       65536, /* Sum(256x256)                              */
           0, /* Base address (beginning of 128x128 level) */
           0, /* Base address (beginning of 256x256 level) */
     -262144, /* - Sum(512x512)                            */
    -1048576, /* - Sum(1024x1024)                          */
    -4456448, /* - Sum(2048x2048, 512x512)                 */
  },
};

/*---------------------------------------------------------------------------
** _grTexCalcBaseAddress
** Copied from SDK ditex.c line 1748
**---------------------------------------------------------------------------*/
static FxU32
_grTexCalcBaseAddress( FxU32 start, GrLOD_t large_lod,
                       GrAspectRatio_t aspect, GrTextureFormat_t format,
                       FxU32 odd_even_mask )
{
  FxU32 sum_of_lod_sizes;

  /* Validate format */
  if (format >= sizeof(_grBitsPerTexel)/sizeof(_grBitsPerTexel[0]) ||
      _grBitsPerTexel[format] == 0) {
    return start;
  }

  /* Mirror aspect ratios: 1:2, 1:4, 1:8 use same offsets as 2:1, 4:1, 8:1 */
  if ( aspect > G3_ASPECT_TRANSLATE(GR_ASPECT_LOG2_1x1) )
    aspect = G3_ASPECT_TRANSLATE(GR_ASPECT_LOG2_1x8) - aspect;

  if ( odd_even_mask == GR_MIPMAPLEVELMASK_BOTH ) {
    sum_of_lod_sizes = _grMipMapOffset[aspect][large_lod + 1];
  } else {
    if (((odd_even_mask == GR_MIPMAPLEVELMASK_EVEN) && (large_lod & 1)) ||
        ((odd_even_mask == GR_MIPMAPLEVELMASK_ODD) && !(large_lod & 1)))
      large_lod += 1;
    else
      large_lod += 2;

    sum_of_lod_sizes = _grMipMapOffset_Tsplit[aspect][large_lod];
  }

  /* Convert from texels to bytes */
  sum_of_lod_sizes *= _grBitsPerTexel[format]; /* bits  */
  sum_of_lod_sizes >>= 3;                      /* bytes */

  /* Clamp the size down to alignment boundary */
  sum_of_lod_sizes &= ~SST_TEXTURE_ALIGN_MASK;

  return ( start - sum_of_lod_sizes );
}

/*
 * grTexMinAddress - Get minimum texture memory address
 *
 * Returns the lowest valid address for texture downloads on the TMU.
 * This is always 0 in our implementation.
 */
FxU32 __stdcall grTexMinAddress(GrChipID_t tmu)
{
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
    if (!g_voodoo) {
        return 0;
    }
    int t = (tmu == GR_TMU0) ? 0 : 1;
    FxU32 result = g_voodoo->tmu[t].mask;  /* mask = memory_size - 1 */
    return result;
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
 *
 * This function writes three hardware registers:
 *   - textureMode: texture format and filtering settings
 *   - tLOD: LOD range, aspect ratio, evenOdd settings
 *   - texBaseAddr: base address in texture memory
 *
 * Reference: SDK gtex.c lines 2853-2998
 */
void __stdcall grTexSource(GrChipID_t tmu, FxU32 startAddress, FxU32 evenOdd, GrTexInfo *info)
{
    if (!g_voodoo || !info) {
        return;
    }

    int t = (tmu == GR_TMU0) ? 0 : 1;
    tmu_state *ts = &g_voodoo->tmu[t];

    /*-------------------------------------------------------------
      Compute textureMode register
      SDK gtex.c lines 2853-2932
      -------------------------------------------------------------*/
    uint32_t texMode = ts->reg[textureMode].u;

    /* Clear format bits, preserve filtering/clamp settings */
    texMode &= ~TEXMODE_FORMAT_MASK;

    /* Convert Glide format to Voodoo hardware format */
    int vfmt = get_voodoo_format(info->format);
    
    /* Set format and enable perspective/W clamp (SDK lines 2929-2932) */
    texMode |= (vfmt << TEXMODE_FORMAT_SHIFT);
    /* SST_TPERSP_ST and SST_TCLAMPW are handled by grHints/grTexFilterMode */
    
    ts->reg[textureMode].u = texMode;

    /*-------------------------------------------------------------
      Compute tLOD register (keep LODBIAS intact)
      SDK gtex.c lines 2934-2962
      
      SDK uses _g3LodXlat macro to translate Glide LOD to hardware LOD:
        _g3LodXlat(lod, tBig) = g3LodXlat_base[tBig] - lod
        For tBig=false (textures ≤256): = 8 - lod
      
      Glide LOD: 8=256x256, 6=64x64, 0=1x1 (log2 of size)
      Hardware LOD: 0=largest mipmap, higher=smaller mipmaps
      -------------------------------------------------------------*/
    
    /* _g3LodXlat translation for non-big textures */
    int largeLOD = 8 - info->largeLodLog2;
    int smallLOD = 8 - info->smallLodLog2;
    
    /* G3_ASPECT_TRANSLATE: SDK uses 0x3 - aspect for aspect ratio indexing */
    int aspectTranslated = 0x3 - info->aspectRatioLog2;

    /* Bit definitions for tLOD register (from voodoo_defs.h) */
    #define TLOD_LODMIN_SHIFT       0
    #define TLOD_LODMIN_MASK        0x3F
    #define TLOD_LODMAX_SHIFT       6
    #define TLOD_LODMAX_MASK        (0x3F << 6)
    #define TLOD_LOD_ODD            (1 << 18)
    #define TLOD_LOD_TSPLIT         (1 << 19)
    #define TLOD_LOD_S_IS_WIDER     (1 << 20)
    #define TLOD_LOD_ASPECT_SHIFT   21
    #define TLOD_LOD_ASPECT_MASK    (0x3 << 21)
    #define TLOD_LOD_FRACBITS       2

    /* Read current tLOD to preserve LODBIAS (bits 12-17) */
    uint32_t tLod = ts->reg[tLOD].u;

    /* Clear bits we're going to set */
    tLod &= ~(TLOD_LODMIN_MASK | TLOD_LODMAX_MASK |
              TLOD_LOD_ASPECT_MASK | TLOD_LOD_TSPLIT |
              TLOD_LOD_ODD | TLOD_LOD_S_IS_WIDER);

    /* SST_TLOD_MINMAX_INT: (largeLOD << 2) | (smallLOD << 8)
     * This puts integer LOD values into 4.2 fixed point format */
    tLod |= ((largeLOD << TLOD_LOD_FRACBITS) << TLOD_LODMIN_SHIFT);
    tLod |= ((smallLOD << TLOD_LOD_FRACBITS) << TLOD_LODMAX_SHIFT);

    /* Set evenOdd bits - SDK _gr_evenOdd_xlate_table */
    switch (evenOdd) {
    case GR_MIPMAPLEVELMASK_EVEN:
        tLod |= TLOD_LOD_TSPLIT;
        break;
    case GR_MIPMAPLEVELMASK_ODD:
        tLod |= TLOD_LOD_TSPLIT | TLOD_LOD_ODD;
        break;
    case GR_MIPMAPLEVELMASK_BOTH:
    default:
        /* No additional bits */
        break;
    }

    /* Set aspect ratio bits - SDK _gr_aspect_xlate_table */
    switch (aspectTranslated) {
    case 0: /* 8:1 */
        tLod |= (3 << TLOD_LOD_ASPECT_SHIFT) | TLOD_LOD_S_IS_WIDER;
        break;
    case 1: /* 4:1 */
        tLod |= (2 << TLOD_LOD_ASPECT_SHIFT) | TLOD_LOD_S_IS_WIDER;
        break;
    case 2: /* 2:1 */
        tLod |= (1 << TLOD_LOD_ASPECT_SHIFT) | TLOD_LOD_S_IS_WIDER;
        break;
    case 3: /* 1:1 */
        /* No bits set */
        break;
    case 4: /* 1:2 */
        tLod |= (1 << TLOD_LOD_ASPECT_SHIFT);
        break;
    case 5: /* 1:4 */
        tLod |= (2 << TLOD_LOD_ASPECT_SHIFT);
        break;
    case 6: /* 1:8 */
        tLod |= (3 << TLOD_LOD_ASPECT_SHIFT);
        break;
    }

    ts->reg[tLOD].u = tLod;

    /*-------------------------------------------------------------
      Compute texBaseAddr register
      SDK gtex.c lines 2843-2847, uses _grTexCalcBaseAddress

      The SDK returns a byte address that may wrap negative. We need
      to convert this to DOSBox's register format which stores
      addresses divided by 8 (TEXADDR_SHIFT=3, TEXADDR_MASK=0x0fffff).

      For DOSBox: base = (texBaseAddr.u & TEXADDR_MASK) << TEXADDR_SHIFT
      So we store: texBaseAddr.u = (byteAddress & tmu_mask) >> 3
      -------------------------------------------------------------*/
    {
        FxU32 baseAddress = _grTexCalcBaseAddress(startAddress,
                                                   info->largeLodLog2,
                                                   G3_ASPECT_TRANSLATE(info->aspectRatioLog2),
                                                   info->format,
                                                   evenOdd);
        /* Mask to texture memory size and convert to DOSBox register format */
        FxU32 maskedBase = baseAddress & ts->mask;  /* Wrap to 2MB */
        FxU32 regValue = maskedBase >> 3;           /* Convert to 8-byte units */
        ts->reg[texBaseAddr].u = regValue;
    }

    /*-------------------------------------------------------------
      Set tmu_state fields for DOSBox compatibility
      These are extracted by recompute_texture_params but we set
      them here for consistency.
      -------------------------------------------------------------*/
    ts->lodmin = TEXLOD_LODMIN(tLod) << 6;
    ts->lodmax = TEXLOD_LODMAX(tLod) << 6;

    /* Mark registers as dirty so recompute_texture_params will run */
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
    if (!g_voodoo || !info || !info->data) {
        return;
    }

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

        /* Pre-convert to ARGB32 shadow buffer for faster sampling */
        int num_texels = total_size / bpp;
        preconvert_texture_data(ts, dest_addr, info->data, info->format, num_texels);
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
    if (!g_voodoo || !data) {
        return;
    }

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

        /* Pre-convert to ARGB32 shadow buffer for faster sampling */
        int num_texels = tex_width * tex_height;
        preconvert_texture_data(ts, dest_addr, data, format, num_texels);
    }

    ts->regdirty = 1;
}

/*
 * grTexDownloadMipMapLevelPartial - Download part of a single mipmap level
 */
void __stdcall grTexDownloadMipMapLevelPartial(GrChipID_t tmu, FxU32 startAddress, GrLOD_t thisLod,
                               GrLOD_t largeLod, GrAspectRatio_t aspectRatio,
                               GrTextureFormat_t format, FxU32 evenOdd, void *data,
                               int start, int end)
{
    if (!g_voodoo || !data) {
        return;
    }

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
    int row_bytes = tex_width * bpp;

    /* Calculate offset to the starting row */
    uint32_t dest_addr = (startAddress + start * row_bytes) & ts->mask;
    int num_rows = end - start + 1;
    int copy_size = num_rows * row_bytes;

    if (dest_addr + copy_size <= ts->mask + 1) {
        memcpy(&ts->ram[dest_addr], data, copy_size);

        /* Pre-convert to ARGB32 shadow buffer for faster sampling */
        int num_texels = num_rows * tex_width;
        preconvert_texture_data(ts, dest_addr, data, format, num_texels);
    }

    ts->regdirty = 1;
}

/*
 * grTexTextureMemRequired - Calculate memory needed for a texture
 */
FxU32 __stdcall grTexTextureMemRequired(FxU32 evenOdd, GrTexInfo *info)
{
    (void)evenOdd;

    if (!info) {
        return 0;
    }

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
    if (!g_voodoo) {
        return;
    }

    int t = (tmu == GR_TMU0) ? 0 : 1;
    tmu_state *ts = &g_voodoo->tmu[t];

    if (!ts->reg) {
        return;
    }

    uint32_t val = ts->reg[textureMode].u;

    /* Clear texture combine bits (RGB and Alpha) */
    val &= ~(TEXMODE_TC_BITS_MASK | TEXMODE_TCA_BITS_MASK);

    /* RGB combine */
    if (rgb_function == GR_COMBINE_FUNCTION_ZERO)
        val |= TEXMODE_TC_ZERO_OTHER_BIT;

    /* TC_MSELECT: blend factor source */
    val |= ((rgb_factor & 0x7) << TEXMODE_TC_MSELECT_SHIFT);

    /* TC_REVERSE_BLEND: set for base factors (0-7), clear for ONE_MINUS (8-F) */
    if ((rgb_factor & 0x8) == 0)
        val |= TEXMODE_TC_REVERSE_BLEND_BIT;

    if (rgb_function == GR_COMBINE_FUNCTION_LOCAL ||
        rgb_function == GR_COMBINE_FUNCTION_SCALE_OTHER_ADD_LOCAL ||
        rgb_function == GR_COMBINE_FUNCTION_SCALE_OTHER_MINUS_LOCAL_ADD_LOCAL ||
        rgb_function == GR_COMBINE_FUNCTION_SCALE_MINUS_LOCAL_ADD_LOCAL)
        val |= TEXMODE_TC_ADD_CLOCAL_BIT;

    if (rgb_function == GR_COMBINE_FUNCTION_LOCAL_ALPHA ||
        rgb_function == GR_COMBINE_FUNCTION_SCALE_OTHER_ADD_LOCAL_ALPHA ||
        rgb_function == GR_COMBINE_FUNCTION_SCALE_OTHER_MINUS_LOCAL_ADD_LOCAL_ALPHA ||
        rgb_function == GR_COMBINE_FUNCTION_SCALE_MINUS_LOCAL_ADD_LOCAL_ALPHA)
        val |= TEXMODE_TC_ADD_ALOCAL_BIT;

    if (rgb_function == GR_COMBINE_FUNCTION_SCALE_OTHER_MINUS_LOCAL ||
        rgb_function == GR_COMBINE_FUNCTION_SCALE_OTHER_MINUS_LOCAL_ADD_LOCAL ||
        rgb_function == GR_COMBINE_FUNCTION_SCALE_OTHER_MINUS_LOCAL_ADD_LOCAL_ALPHA)
        val |= TEXMODE_TC_SUB_CLOCAL_BIT;

    if (rgb_invert)
        val |= TEXMODE_TC_INVERT_OUTPUT_BIT;

    /* Alpha combine */
    if (alpha_function == GR_COMBINE_FUNCTION_ZERO)
        val |= TEXMODE_TCA_ZERO_OTHER_BIT;

    /* TCA_MSELECT: blend factor source */
    val |= ((alpha_factor & 0x7) << TEXMODE_TCA_MSELECT_SHIFT);

    /* TCA_REVERSE_BLEND: set for base factors (0-7), clear for ONE_MINUS (8-F) */
    if ((alpha_factor & 0x8) == 0)
        val |= TEXMODE_TCA_REVERSE_BLEND_BIT;

    if (alpha_function == GR_COMBINE_FUNCTION_LOCAL ||
        alpha_function == GR_COMBINE_FUNCTION_LOCAL_ALPHA ||
        alpha_function == GR_COMBINE_FUNCTION_SCALE_OTHER_ADD_LOCAL ||
        alpha_function == GR_COMBINE_FUNCTION_SCALE_OTHER_ADD_LOCAL_ALPHA ||
        alpha_function == GR_COMBINE_FUNCTION_SCALE_OTHER_MINUS_LOCAL_ADD_LOCAL ||
        alpha_function == GR_COMBINE_FUNCTION_SCALE_OTHER_MINUS_LOCAL_ADD_LOCAL_ALPHA ||
        alpha_function == GR_COMBINE_FUNCTION_SCALE_MINUS_LOCAL_ADD_LOCAL ||
        alpha_function == GR_COMBINE_FUNCTION_SCALE_MINUS_LOCAL_ADD_LOCAL_ALPHA)
        val |= TEXMODE_TCA_ADD_ALOCAL_BIT;

    if (alpha_function == GR_COMBINE_FUNCTION_SCALE_OTHER_MINUS_LOCAL ||
        alpha_function == GR_COMBINE_FUNCTION_SCALE_OTHER_MINUS_LOCAL_ADD_LOCAL ||
        alpha_function == GR_COMBINE_FUNCTION_SCALE_OTHER_MINUS_LOCAL_ADD_LOCAL_ALPHA)
        val |= TEXMODE_TCA_SUB_CLOCAL_BIT;

    if (alpha_invert)
        val |= TEXMODE_TCA_INVERT_OUTPUT_BIT;

    ts->reg[textureMode].u = val;
}

/*
 * grTexFilterMode - Set texture filtering modes
 */
void __stdcall grTexFilterMode(GrChipID_t tmu, GrTextureFilterMode_t minfilter_mode,
                     GrTextureFilterMode_t magfilter_mode)
{
    if (!g_voodoo) {
        return;
    }

    int t = (tmu == GR_TMU0) ? 0 : 1;
    tmu_state *ts = &g_voodoo->tmu[t];

    if (!ts->reg) {
        return;
    }

    uint32_t val = ts->reg[textureMode].u;
    val &= ~TEXMODE_FILTER_MASK;

    if (minfilter_mode == GR_TEXTUREFILTER_BILINEAR)
        val |= TEXMODE_MINIFICATION_FILTER_BIT;
    if (magfilter_mode == GR_TEXTUREFILTER_BILINEAR)
        val |= TEXMODE_MAGNIFICATION_FILTER_BIT;

    ts->reg[textureMode].u = val;
}

/*
 * grTexClampMode - Set texture coordinate clamping/wrapping
 */
void __stdcall grTexClampMode(GrChipID_t tmu, GrTextureClampMode_t s_clamp, GrTextureClampMode_t t_clamp)
{
    if (!g_voodoo) {
        return;
    }

    int t = (tmu == GR_TMU0) ? 0 : 1;
    tmu_state *ts = &g_voodoo->tmu[t];

    if (!ts->reg) {
        return;
    }

    uint32_t val = ts->reg[textureMode].u;
    val &= ~(TEXMODE_CLAMP_S_BIT | TEXMODE_CLAMP_T_BIT);

    if (s_clamp == GR_TEXTURECLAMP_CLAMP)
        val |= TEXMODE_CLAMP_S_BIT;
    if (t_clamp == GR_TEXTURECLAMP_CLAMP)
        val |= TEXMODE_CLAMP_T_BIT;

    ts->reg[textureMode].u = val;
}

/*
 * grTexMipMapMode - Configure mipmapping mode
 */
void __stdcall grTexMipMapMode(GrChipID_t tmu, GrMipMapMode_t mode, FxBool lodBlend)
{
    if (!g_voodoo) {
        return;
    }

    int t = (tmu == GR_TMU0) ? 0 : 1;
    tmu_state *ts = &g_voodoo->tmu[t];

    /* LOD values use DOSBox scaled format: actual_lod << 6
     * For mipmap disable: use only LOD 0 (largest mip)
     * For mipmap enable: use full LOD range 0-8
     */
    if (mode == GR_MIPMAP_DISABLE) {
        ts->lodmin = 0 << 6;
        ts->lodmax = 0 << 6;
    } else {
        ts->lodmin = 0 << 6;
        ts->lodmax = 8 << 6;
    }

    (void)lodBlend;
}

/*
 * grTexLodBiasValue - Set LOD bias
 */
void __stdcall grTexLodBiasValue(GrChipID_t tmu, float bias)
{
    if (!g_voodoo) {
        return;
    }

    int t = (tmu == GR_TMU0) ? 0 : 1;
    tmu_state *ts = &g_voodoo->tmu[t];

    ts->lodbias = (int32_t)(bias * 16.0f);
}

/*
 * grTexDownloadTable - Download palette or NCC table
 */
void __stdcall grTexDownloadTable(GrTexTable_t type, void *data)
{
    if (!g_voodoo || !data) {
        return;
    }

    for (int t = 0; t < 2; t++) {
        tmu_state *ts = &g_voodoo->tmu[t];

        switch (type) {
        case GR_TEXTABLE_NCC0:
        case GR_TEXTABLE_NCC1:
            /* TODO: NCC decompression tables for YIQ textures - not implemented */
            break;

        case GR_TEXTABLE_PALETTE:
        case GR_TEXTABLE_PALETTE_6666_EXT:
            {
                const uint32_t* pal = (const uint32_t*)data;
                for (int i = 0; i < 256; i++) {
                    /*
                     * Palette data is always in ARGB format regardless of grColorFormat.
                     * The SDK's _grTexDownloadPalette does NOT call _grSwizzleColor.
                     * Games provide palette data pre-formatted in ARGB.
                     */
                    ts->palette[i] = pal[i];
                }

                /* Reconvert all P_8 textures with the new palette */
                if (ts->p8_region_count > 0) {
                    debug_log("Palette change: reconverting %d P_8 texture regions on TMU%d\n",
                              ts->p8_region_count, t);
                }
                reconvert_p8_textures(ts);
            }
            break;

        default:
            break;
        }

        ts->regdirty = 1;
    }
}
