# Task 03g: Eliminate Per-Pixel Texture Format Switch

## Summary
Replace the per-pixel texture format decoding switch with format-specific sampling functions.

## Current State

**File:** `src/voodoo_pipeline.h` lines 970+ (approximate)

Inside texture sampling code:

```c
switch (TEXMODE_FORMAT(texMode)) {
    case 0:  /* 8-bit RGB332 */
    case 1:  /* 8-bit YIQ */
    case 2:  /* 8-bit ALPHA */
    case 3:  /* 8-bit INTENSITY */
    case 4:  /* 8-bit ALPHA+INTENSITY */
    case 5:  /* 8-bit PALETTE */
    case 6:  /* 8-bit unused? */
    case 7:  /* 8-bit unused? */
    case 8:  /* 16-bit ARGB8332 */
    case 9:  /* 16-bit AYIQ */
    case 10: /* 16-bit RGB565 */
    case 11: /* 16-bit ARGB1555 */
    case 12: /* 16-bit ARGB4444 */
    case 13: /* 16-bit ALPHA+INTENSITY */
    case 14: /* 16-bit AP88 (alpha+palette) */
    // ... more formats
}
```

This switch runs once per pixel per texture to decode the texture format.

## Problem
- Many cases (14+), runs ~480,000 times per frame per TMU
- Texture format is constant for entire draw call (per texture)
- Format decoding is expensive due to bit manipulation variations
- With 2 TMUs, could be 960,000+ switch evaluations per frame

## Proposed Solution

### Option A: Function Pointers (Recommended)
```c
typedef void (*tex_decode_fn)(const uint8_t *texData, int offset, rgba_t *out);
tex_decode_fn cached_tex_decode[2];  // One per TMU

// Format-specific decode functions
static inline void decode_rgb565(const uint8_t *texData, int offset, rgba_t *out) {
    uint16_t pixel = *(uint16_t*)(texData + offset * 2);
    out->r = (pixel >> 8) & 0xF8;
    out->g = (pixel >> 3) & 0xFC;
    out->b = (pixel << 3) & 0xF8;
    out->a = 255;
}

static inline void decode_argb1555(const uint8_t *texData, int offset, rgba_t *out) {
    uint16_t pixel = *(uint16_t*)(texData + offset * 2);
    out->r = (pixel >> 7) & 0xF8;
    out->g = (pixel >> 2) & 0xF8;
    out->b = (pixel << 3) & 0xF8;
    out->a = (pixel & 0x8000) ? 255 : 0;
}
// ... etc for each format

// Set when texture is bound
void update_texture_decoder(int tmu, uint32_t texMode) {
    switch (TEXMODE_FORMAT(texMode)) {
        case 10: cached_tex_decode[tmu] = decode_rgb565; break;
        case 11: cached_tex_decode[tmu] = decode_argb1555; break;
        // ...
    }
}
```

### Option B: Pre-converted Textures (See Task 02)
Convert all textures to a common format (ARGB8888) at upload time:
- Eliminates format switch entirely
- Higher memory usage but fastest runtime
- This is handled by Task 02, which should be done first

### Option C: Format Class Fast Paths
Group formats into classes that share similar decoding:

```c
// Fast path for 16-bit formats (most common)
if (cached_tex_format_class == TEX_CLASS_16BIT) {
    // All 16-bit formats read uint16_t, just differ in bit extraction
    uint16_t pixel = *(uint16_t*)(texData + offset * 2);
    cached_tex_decode[tmu](pixel, out);  // Simpler signature
}
```

## Files to Modify
- `src/voodoo_pipeline.h` - lines 970+ (TEXTURE_PIPELINE macro area)
- `src/voodoo_state.h` - Add function pointer fields per TMU
- `src/glide3x_texture.c` - Update in texture source functions

## Related State Changes
- `grTexSource()` sets the active texture
- `grTexDownloadMipMap()` could pre-select decoder

## Testing Requirements
- [ ] All texture formats decode correctly:
  - [ ] RGB332, YIQ, ALPHA, INTENSITY, AI
  - [ ] Palette (8-bit)
  - [ ] ARGB8332, AYIQ, RGB565, ARGB1555, ARGB4444
  - [ ] 16-bit AI, AP88
- [ ] Bilinear filtering works with all formats
- [ ] Multi-texture (TMU0 + TMU1) works
- [ ] No visual differences from current implementation

## Dependencies
- Parent task: `tasks/03_Eliminate_PerPixel_Switches.md`
- Related to Task 02 (Preconvert Textures) - if Task 02 is done, this becomes simpler
- Most complex switch to optimize - defer if others give enough speedup

## Expected Impact
- HIGH IMPACT - textures are used in almost every pixel
- Eliminates ~480,000+ branch decisions per frame per TMU
- Complex due to many formats, but high payoff
- Consider doing Task 02 first to simplify this
