# Task 02: Pre-convert Textures on Upload

## Summary
Convert textures from Glide formats (RGB565, ARGB1555, etc.) to ARGB32 at upload time, eliminating per-sample lookup table indirection during rendering.

## Current State

### Texture Upload (Just memcpy)
**File:** `src/glide3x_texture.c:643, 687, 736`
```c
memcpy(&ts->ram[dest_addr], info->data, total_size);
```
Textures are stored in their original Glide format with no pre-processing.

### Texture Sampling (Per-pixel lookup)
**File:** `src/voodoo_pipeline.h:970-1065`

Every texture sample does:
```
ram[address] -> lookup_table[texel_value] -> ARGB result
```

For **bilinear filtering** (common case), this means **per pixel**:
- 4 RAM reads (4 texel corners)
- 4 lookup table reads (format conversion)
- Bilinear blend calculation

### Lookup Tables
**File:** `src/voodoo_state.h:126-134`
```c
typedef struct {
    rgb_t rgb332[256];        /* RGB 3-3-2 lookup */
    rgb_t alpha8[256];        /* alpha 8-bit lookup */
    rgb_t int8[256];          /* intensity 8-bit lookup */
    rgb_t ai44[256];          /* alpha/intensity 4-4 lookup */
    rgb_t rgb565[65536];      /* RGB 5-6-5 lookup - 256KB */
    rgb_t argb1555[65536];    /* ARGB 1-5-5-5 lookup - 256KB */
    rgb_t argb4444[65536];    /* ARGB 4-4-4-4 lookup - 256KB */
} tmu_shared_state;
```
Total: ~768KB of lookup tables that far exceed CPU cache sizes.

## Problem
- 768KB lookup tables guarantee cache thrashing
- Random access pattern (indexed by texel value) = constant cache misses
- Each bilinear-filtered pixel does 4 random lookups into these tables
- L1 cache is typically 32KB, L2 is 256-512KB

## Proposed Change
Convert textures to ARGB32 format on upload (`grTexDownloadMipMap*`), then read ARGB directly during sampling without lookup table indirection.

## Implementation Approach

### Option A: Shadow Buffer (Safest)
- Allocate separate ARGB32 buffer alongside original texture RAM
- On upload, convert and store in both locations
- Modify TEXTURE_PIPELINE to read from ARGB buffer
- Pro: Preserves original data for games that read back textures
- Con: 2x texture memory usage

### Option B: In-Place Conversion (Memory efficient)
- Convert textures in-place to ARGB32
- Requires 4 bytes per texel instead of 1-2
- Need to remap texture addresses (2x or 4x address multiplier)
- Pro: No extra allocation
- Con: More complex address calculation, can't support texture readback in original format

### Option C: Lazy Conversion with Cache
- Keep original texture data
- Convert and cache ARGB data on first sample
- Use dirty flags to invalidate cache on re-upload
- Pro: Only converts textures actually used
- Con: First-frame stutter, cache management complexity

## Files to Modify

### For texture upload:
- `src/glide3x_texture.c`
  - `grTexDownloadMipMap()` - line 601, memcpy at 643 - Complete mipmap chain
  - `grTexDownloadMipMapLevel()` - line 652, memcpy at 687 - Single mipmap level
  - `grTexDownloadMipMapLevelPartial()` - line 696, memcpy at 736 - Partial level update
  - `grTexDownloadTable()` - line 972 - Palette/NCC tables (special handling needed)

**Note:** All three texture data functions ultimately do `memcpy(&ts->ram[dest_addr], data, size)`.
The conversion should happen at these memcpy points, or in a wrapper called by all three.

### For texture sampling:
- `src/voodoo_pipeline.h`
  - `TEXTURE_PIPELINE` macro (lines 896-1227)
  - Remove/bypass lookup table indirection

### For state tracking:
- `src/voodoo_state.h`
  - Add ARGB buffer pointer to `tmu_state`
  - Or add converted texture storage

## Format Conversion Reference

| Glide Format | Bits | Conversion |
|--------------|------|------------|
| GR_TEXFMT_RGB_332 | 8 | R[7:5], G[4:2], B[1:0] -> ARGB |
| GR_TEXFMT_A_8 | 8 | Alpha only |
| GR_TEXFMT_I_8 | 8 | Intensity (grayscale) |
| GR_TEXFMT_AI_44 | 8 | Alpha[7:4], Intensity[3:0] |
| GR_TEXFMT_RGB_565 | 16 | R[15:11], G[10:5], B[4:0] |
| GR_TEXFMT_ARGB_1555 | 16 | A[15], R[14:10], G[9:5], B[4:0] |
| GR_TEXFMT_ARGB_4444 | 16 | A[15:12], R[11:8], G[7:4], B[3:0] |
| GR_TEXFMT_P_8 | 8 | Palette index (needs palette lookup) |
| GR_TEXFMT_YIQ_422 | 8 | NCC compressed (needs NCC table) |

**Special cases requiring table lookup:**
- **P_8 (Palettized):** Index into 256-entry palette from `grTexDownloadTable(GR_TEXTABLE_PALETTE)`
- **YIQ_422 (NCC):** NCC compressed, needs NCC table from `grTexDownloadTable(GR_TEXTABLE_NCC0/1)`

**Palette change complication:** If a game uploads a P_8 texture, then later changes the palette via `grTexDownloadTable`, pre-converted textures would show wrong colors. Solutions:
1. Don't pre-convert palettized textures (keep lookup for P_8/YIQ only)
2. Track which textures use palettes and reconvert on palette change
3. Store palette index in pre-converted data and apply palette at sample time

## Risk Assessment
**Risk: MEDIUM**
- Need to handle all texture formats correctly
- Memory usage increases (2x for 16-bit textures, 4x for 8-bit)
- Must maintain compatibility with texture readback if games use it
- Palette/NCC textures need special handling

## Testing Requirements
- [ ] All texture formats render correctly
- [ ] No visual differences from current implementation
- [ ] Memory usage acceptable
- [ ] Performance improvement measurable
- [ ] Palette changes via `grTexDownloadTable` still work
- [ ] Mipmap levels all convert correctly

## Expected Impact
- Eliminates 4 lookup table accesses per bilinear-filtered pixel
- Converts random 768KB table access to sequential texture memory access
- Better cache utilization
- Estimated: significant speedup for texture-heavy scenes

## Dependencies
None - can be done independently of threading changes.
