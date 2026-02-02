# Glide3x-Native Implementation Issues

This document provides a comprehensive analysis of the glide3x-native implementation, identifying issues that need to be resolved to conform to the Glide 3.x API specifications. The DOSBox-Staging voodoo.cpp implementation is used as the reference for corrections.

---

## Table of Contents

1. [Critical Issues](#critical-issues)
2. [Color Combine Issues](#color-combine-issues)
3. [Alpha Combine Issues](#alpha-combine-issues)
4. [Alpha Blending Issues](#alpha-blending-issues)
5. [Depth Buffer Issues](#depth-buffer-issues)
6. [Texture Pipeline Issues](#texture-pipeline-issues)
7. [Fog Implementation Issues](#fog-implementation-issues)
8. [Triangle Rasterization Issues](#triangle-rasterization-issues)
9. [LFB (Linear Frame Buffer) Issues](#lfb-issues)
10. [Missing/Incomplete Functions](#missing-functions)
11. [Register Mapping Issues](#register-mapping-issues)

---

## Critical Issues

### 1. grColorCombine() - Incorrect Register Bit Mapping

**File:** `glide3x.c:859-889`

**Issue:** The `grColorCombine()` function incorrectly maps the combine parameters to the `fbzColorPath` register bits.

**Current Implementation:**
```c
val &= ~0x1FFFF;  /* Clear color combine bits */
val |= (other & 3);           /* CC_RGBSELECT */
val |= ((factor & 3) << 2);   /* CC_ASELECT - WRONG! */
val |= ((local & 1) << 4);    /* CC_LOCALSELECT */
if (function == GR_COMBINE_FUNCTION_ZERO) {
    val |= (1 << 8);  /* CC_ZERO_OTHER */
}
```

**Expected (per DOSBox-Staging voodoo_defs.h):**
- Bits 0-1: `CC_RGBSELECT` - Select RGB source (0=iterated, 1=texture, 2=color1)
- Bits 2-3: `CC_ASELECT` - Select alpha source (separate from factor!)
- Bit 4: `CC_LOCALSELECT` - Select local color source
- Bits 5-6: `CCA_LOCALSELECT` - Missing in current implementation
- Bit 7: `CC_LOCALSELECT_OVERRIDE` - Not implemented
- Bit 8: `CC_ZERO_OTHER` - Zero the other input
- Bit 9: `CC_SUB_CLOCAL` - Subtract c_local
- Bits 10-12: `CC_MSELECT` - Blend factor select (NOT mapped from factor parameter correctly)
- Bit 13: `CC_REVERSE_BLEND` - Reverse blend
- Bits 14-15: `CC_ADD_ACLOCAL` - Add a_local or c_local
- Bit 16: `CC_INVERT_OUTPUT` - Invert output

**Fix Required:** Complete rewrite of parameter-to-register mapping based on the Glide combine function definitions.

---

### 2. grAlphaCombine() - Incomplete Implementation

**File:** `glide3x.c:891-916`

**Issue:** The `grAlphaCombine()` function ignores most parameters and only sets a few bits.

**Current Implementation:**
```c
val &= ~(0x1FF << 17);
if (function == GR_COMBINE_FUNCTION_ZERO) {
    val |= (1 << 17);  /* CCA_ZERO_OTHER */
}
if (invert) {
    val |= (1 << 25);  /* CCA_INVERT_OUTPUT */
}
(void)factor;  // IGNORED!
(void)local;   // IGNORED!
(void)other;   // IGNORED!
```

**Expected (per voodoo_defs.h):**
- Bit 17: `CCA_ZERO_OTHER`
- Bit 18: `CCA_SUB_CLOCAL`
- Bits 19-21: `CCA_MSELECT` (blend factor - NOT implemented)
- Bit 22: `CCA_REVERSE_BLEND` (NOT implemented)
- Bits 23-24: `CCA_ADD_ACLOCAL` (NOT implemented)
- Bit 25: `CCA_INVERT_OUTPUT`

**Fix Required:** Implement full alpha combine mapping similar to color combine.

---

### 3. grAlphaBlendFunction() - Incorrect Register Layout

**File:** `glide3x.c:966-983`

**Issue:** The alpha blend function bits are shifted incorrectly.

**Current Implementation:**
```c
val |= (rgb_sf & 0xF) << 8;
val |= (rgb_df & 0xF) << 12;
val |= (alpha_sf & 0xF) << 16;
val |= (alpha_df & 0xF) << 20;
val |= (1 << 4);  /* Enable alpha blending */
```

**Expected (per voodoo_defs.h ALPHAMODE):**
- Bit 0: `ALPHATEST` - Alpha test enable
- Bits 1-3: `ALPHAFUNCTION` - Alpha test function
- Bit 4: `ALPHABLEND` - Alpha blend enable (CORRECT)
- Bit 5: `ANTIALIAS` - Anti-aliasing
- **Bits 8-11:** `SRCRGBBLEND` (CORRECT)
- **Bits 12-15:** `DSTRGBBLEND` (CORRECT)
- **Bits 16-19:** `SRCALPHABLEND` (CORRECT)
- **Bits 20-23:** `DSTALPHABLEND` (CORRECT)
- Bits 24-31: `ALPHAREF` - Alpha reference value

**Note:** The bit positions are correct, but the function unconditionally enables alpha blending even when sf=ONE and df=ZERO (which should disable blending effectively). The existing alpha test and reference value bits may be clobbered.

**Fix Required:** Preserve existing ALPHAMODE bits when setting blend function.

---

## Color Combine Issues

### 4. CC_MSELECT Mapping Error

**File:** `glide3x.c:874`

**Issue:** The `factor` parameter is used for `CC_ASELECT` instead of `CC_MSELECT`.

Per the Glide spec, `GrCombineFactor_t` should map to the multiply select (`CC_MSELECT`) which determines what multiplies the intermediate result:
- 0 = zero
- 1 = c_local
- 2 = a_other
- 3 = a_local
- 4 = detail factor (LOD)
- 5 = LOD fraction

**Current code incorrectly uses factor for alpha select (bits 2-3).**

---

### 5. Missing CC_SUB_CLOCAL Support

**Issue:** The `GR_COMBINE_FUNCTION_SCALE_OTHER_MINUS_LOCAL` and similar functions require `CC_SUB_CLOCAL` bit to be set, which is not implemented.

---

### 6. Missing CC_ADD_ACLOCAL Support

**Issue:** The `GR_COMBINE_FUNCTION_SCALE_OTHER_ADD_LOCAL` functions require `CC_ADD_ACLOCAL` bits (14-15), which are not implemented.

---

## Alpha Combine Issues

### 7. CCA_MSELECT Not Implemented

**File:** `glide3x.c:911`

**Issue:** The alpha factor parameter is completely ignored. The alpha multiply select should control:
- 0 = zero
- 1 = c_local (alpha component)
- 2 = a_other
- 3 = a_local
- 4 = detail factor
- 5 = LOD fraction

---

## Alpha Blending Issues

### 8. Destination Alpha Read in APPLY_ALPHA_BLEND

**File:** `voodoo_pipeline.h:483-485`

**Issue:** The macro reads destination alpha from the depth buffer when alpha planes are enabled:
```c
int da = (FBZMODE_ENABLE_ALPHA_PLANES(FBZMODE) && depth)
               ? depth[XX]
               : 0xff;
```

This is correct for Voodoo 2 but may need verification that `depth` points to the correct buffer when alpha planes are enabled (the aux buffer stores alpha, not depth, in that mode).

---

### 9. Missing Blend Mode 8-14 Implementation

**File:** `voodoo_pipeline.h:501-540`

**Issue:** Source and destination blend modes 8-14 are not implemented (reserved in Voodoo 1, some used in Voodoo 2+). The switch cases jump from 7 to 15.

---

## Depth Buffer Issues

### 10. grDepthBufferMode() Incomplete

**File:** `glide3x.c:2281-2303`

**Issue:** The function doesn't handle `GR_DEPTHBUFFER_ZBUFFER_COMPARE_TO_BIAS` or `GR_DEPTHBUFFER_WBUFFER_COMPARE_TO_BIAS` modes which use `FBZMODE_DEPTH_SOURCE_COMPARE` bit.

**Current Implementation:**
```c
if (mode == GR_DEPTHBUFFER_DISABLE) {
    val &= ~(1 << 4);
} else {
    val |= (1 << 4);
}
if (mode == GR_DEPTHBUFFER_WBUFFER) {
    val |= (1 << 3);
} else {
    val &= ~(1 << 3);
}
```

**Missing:**
- `FBZMODE_DEPTH_SOURCE_COMPARE` (bit 20) for bias modes
- `FBZMODE_DEPTH_FLOAT_SELECT` (bit 21) for floating-point depth

---

### 11. Z vs W Coordinate Interpretation

**File:** `glide3x.c:1154-1167`

**Issue:** The Z/W gradient computation may have precision issues:
```c
fbi->startz = (int32_t)(a->ooz * 4096.0f);  // 20.12 fixed point
fbi->startw = (int64_t)(a->oow * 4294967296.0);  // 16.32 fixed point
```

The Glide spec states `ooz` should be the depth value for Z-buffering, but many games use `oow` for both W-buffering and Z-buffering. The implementation should check `FBZMODE_WBUFFER_SELECT` to determine which to use.

---

## Texture Pipeline Issues

### 12. Simplified TEXTURE_PIPELINE Macro

**File:** `voodoo_pipeline.h:944-1035`

**Issue:** The `TEXTURE_PIPELINE` macro is a simplified version that:
- Doesn't fully implement LOD calculation (always uses `lodbase = 0`)
- Bilinear filtering is incomplete (noted as "Simplified - full bilinear would need 3 more fetches")
- Missing trilinear filtering support

**DOSBox-Staging reference (voodoo.cpp:1920-2251) implements:**
- Full LOD calculation with `fast_reciplog()`
- Proper mipmap level selection
- Complete bilinear filtering with 4 texel fetches
- LOD dithering
- Detail texture support
- Trilinear blending

---

### 13. Texture Coordinate Precision

**File:** `grDrawTriangle()` at `glide3x.c:1193-1203`

**Issue:** Texture coordinates are converted to 14.18 fixed point:
```c
tmu0->starts = (int64_t)(s0a * 262144.0);  /* * 2^18 */
```

But the reference implementation uses different precision for S/T (14.18) vs W (2.30):
```c
tmu0->startw = (int64_t)(w0a * 1073741824.0);  /* * 2^30 */
```

The W coordinate should be the texture divide value, not `vertex->oow`. This may cause perspective correction errors.

---

### 14. Missing NCC Table Support

**File:** `grTexDownloadTable()` at `glide3x.c:2392-2434`

**Issue:** NCC (Narrow Channel Compression) table download is logged but not properly implemented:
```c
case GR_TEXTABLE_NCC0:
case GR_TEXTABLE_NCC1:
    // WARNING: NCC Table download is complex. Logging first words:
```

The DOSBox-Staging reference has full NCC table decode logic that converts YUV-like encoded textures.

---

### 15. grTexSource() Missing Mipmap Offset Calculation

**File:** `glide3x.c:1514-1574`

**Issue:** Only `lodoffset[0]` is set. The reference implementation calculates offsets for all mipmap levels:
```c
ts->lodoffset[0] = startAddress & ts->mask;
```

**Expected:** Calculate `lodoffset[1]` through `lodoffset[8]` for each mipmap level.

---

### 16. grTexCombine() Incomplete Bit Mapping

**File:** `glide3x.c:1797-1833`

**Issue:** The texture combine register mapping is incomplete:
- `TC_REVERSE_BLEND` bit position is wrong (should be bit 17, code sets bit 20)
- `TCA_REVERSE_BLEND` bit position is wrong (should be bit 26, code sets bit 29)
- Missing `TC_ADD_ACLOCAL` and `TCA_ADD_ACLOCAL` based on function parameter

---

## Fog Implementation Issues

### 17. Fog Table Initialization

**File:** `voodoo_emu.c:166-167`

**Issue:** Fog blend and delta arrays are initialized to zero:
```c
memset(f->fogblend, 0, sizeof(f->fogblend));
memset(f->fogdelta, 0, sizeof(f->fogdelta));
```

The `fogdelta` array should be computed from the fog table to enable interpolation between table entries. The DOSBox reference computes `fogdelta` when the fog table is written.

---

### 18. Fog Table Has 64 Entries but DOSBox Uses 32

**File:** `grFogTable()` at `glide3x.c:2061-2070`

**Issue:** The implementation accepts 64 entries:
```c
for (int i = 0; i < 64; i++) {
    g_voodoo->fbi.fogblend[i] = ft[i];
}
```

The DOSBox reference uses 32 fog table entries with packed format (2 entries per 32-bit write). Verify fog table indexing matches hardware.

---

## Triangle Rasterization Issues

### 19. Edge Walking Algorithm

**File:** `voodoo_emu.c:518-543`

**Issue:** The triangle rasterization uses a simplified scanline algorithm:
```c
float startx = v1x + (fully - v1y) * dxdy_v1v3;
if (fully < v2y)
    stopx = v1x + (fully - v1y) * dxdy_v1v2;
else
    stopx = v2x + (fully - v2y) * dxdy_v2v3;
```

This doesn't handle:
- Sub-pixel precision correctly (Voodoo uses 12.4 fixed-point for coordinates)
- Diamond-exit rules for pixel coverage
- The case where the middle vertex is on the left vs right edge

The DOSBox reference handles these cases explicitly.

---

### 20. Culling in grDrawTriangle() Uses Wrong Coordinates

**File:** `glide3x.c:1120-1129`

**Issue:** The viewport offset is applied AFTER culling check setup but the culling uses the post-offset coordinates. The area calculation should use the original or the offset coordinates consistently:
```c
ax += (float)g_voodoo->vp_x; ay += (float)g_voodoo->vp_y;
// ...
if (g_voodoo->cull_mode != GR_CULL_DISABLE) {
    float area = (bx - ax) * (cy - ay) - (cx - ax) * (by - ay);
```

This may cause incorrect culling when viewport is offset.

---

### 21. Iterator Start Values Don't Account for Pixel Center

**File:** `voodoo_emu.c:555-564`

**Issue:** The starting iterator values are computed from vertex A coordinates:
```c
int32_t dx = istartx - (fbi->ax >> 4);
int32_t dy = y - (fbi->ay >> 4);
```

Voodoo hardware samples at pixel centers (x+0.5, y+0.5), which affects where parameters should start. The reference implementation adjusts for this.

---

## LFB Issues

### 22. grLfbLock() Doesn't Track Write Mode

**File:** `glide3x.c:706-756`

**Issue:** The `writeMode` parameter is ignored:
```c
(void)writeMode;
info->writeMode = GR_LFBWRITEMODE_565;  // Always returns 565
```

The LFB supports multiple write formats:
- `GR_LFBWRITEMODE_565` - RGB 5-6-5
- `GR_LFBWRITEMODE_555` - RGB 5-5-5
- `GR_LFBWRITEMODE_1555` - ARGB 1-5-5-5
- `GR_LFBWRITEMODE_888` - RGB 8-8-8
- `GR_LFBWRITEMODE_8888` - ARGB 8-8-8-8
- `GR_LFBWRITEMODE_Z32` - 32-bit depth

The pixel pipeline mode is also ignored.

---

### 23. grLfbWriteRegion() Ignores Source Format

**File:** `glide3x.c:2483-2524`

**Issue:** The `src_format` parameter is ignored:
```c
(void)src_format;
// ...
memcpy(&dest[(dst_y + y) * g_voodoo->fbi.rowpixels + dst_x],
       &src[y * src_stride], src_width * 2);  // Assumes 16-bit
```

The function should convert from the source format (which can be 8888, 565, 1555, etc.) to the framebuffer format.

---

## Missing Functions

> **Note:** Documentation for each missing function must be provided per the reference implementation source code found in `references/glide3x/h5/glide3/src/`. This reference contains the original 3dfx Glide 3.x SDK implementation with full function signatures, parameter documentation, and behavioral specifications.

### 24. grTexDownloadMipMapLevelPartial() Not Implemented

**Issue:** This function for partial mipmap updates is not present.

**Reference:** See `references/glide3x/h5/glide3/src/gtex.c` for implementation details.

### 25. grTexDetailControl() Not Implemented

**Issue:** Detail texture control for multi-texture blending based on LOD.

**Reference:** See `references/glide3x/h5/glide3/src/gtex.c` for implementation details.

### 26. grTexNCCTable() Not Implemented

**Issue:** Per-TMU NCC table selection is not implemented.

**Reference:** See `references/glide3x/h5/glide3/src/gtex.c` for implementation details.

### 27. grSplash() Not Implemented

**Issue:** The Glide splash screen function (though rarely needed).

**Reference:** See `references/glide3x/h5/glide3/src/gsplash.c` for implementation details.

### 28. grBufferNumPending() Not Implemented

**Issue:** Query for pending buffer swaps.

**Reference:** See `references/glide3x/h5/glide3/src/gbanner.c` or `gsst.c` for implementation details.

### 29. grSstIdle() Not Implemented

**Issue:** Wait for graphics idle.

**Reference:** See `references/glide3x/h5/glide3/src/gsst.c` for implementation details.

### 30. grSstStatus() Not Implemented

**Issue:** Query SST status register.

**Reference:** See `references/glide3x/h5/glide3/src/gsst.c` for implementation details.

---

## Register Mapping Issues

### 31. clipLeftRight/clipLowYHighY Encoding

**File:** `glide3x.c:1940-1941`

**Issue:** The clip register encoding may have X/Y swapped:
```c
g_voodoo->reg[clipLeftRight].u = (minx << 16) | maxx;
g_voodoo->reg[clipLowYHighY].u = (miny << 16) | maxy;
```

Per the DOSBox reference:
- `clipLeftRight` bits 0-9 = right, bits 16-25 = left
- `clipLowYHighY` bits 0-9 = high Y (bottom), bits 16-25 = low Y (top)

This is confusing but the current implementation may actually be correct. Verify with test cases.

---

### 32. fbzMode Default Value

**File:** `voodoo_emu.c:109`

**Issue:** Default fbzMode enables RGB buffer writes:
```c
v->reg[fbzMode].u = (1 << 9);  /* RGB buffer write enabled */
```

Per voodoo_defs.h, bit 9 is `FBZMODE_RGB_BUFFER_MASK`. When set to 1, writes ARE enabled (inverted logic from what some might expect). This appears correct.

---

### 33. TMU Register Base Address

**File:** `glide3x.c:396`

**Issue:** TMU1 register base calculation:
```c
voodoo_init_tmu(&g_voodoo->tmu[1], &g_voodoo->reg[textureMode + 0x100/4], ...);
```

Per the hardware spec, TMU1 registers start at offset 0x400 from TMU0, not 0x100. The register array indexing may be incorrect.

---

## Summary of Priority Fixes

### High Priority (Functionality Breaking):
1. **grColorCombine()** - Fix register bit mapping
2. **grAlphaCombine()** - Implement full parameter mapping
3. **TEXTURE_PIPELINE** - Implement proper LOD calculation
4. **grTexSource()** - Calculate all mipmap level offsets
5. **Triangle rasterization** - Fix edge walking for sub-pixel precision

### Medium Priority (Visual Artifacts):
6. **grTexCombine()** - Fix bit positions
7. **Fog table** - Compute fog deltas
8. **Bilinear filtering** - Complete 4-texel fetch
9. **grDepthBufferMode()** - Add bias mode support
10. **LFB format support** - Handle multiple formats

### Low Priority (Compatibility):
11. Missing API functions
12. NCC table support
13. Detail texture support
14. Trilinear filtering

---

## Implementation Checklist

### Critical Issues
- [ ] Issue 1: grColorCombine() - Fix register bit mapping for CC_MSELECT, CC_SUB_CLOCAL, CC_ADD_ACLOCAL
- [ ] Issue 2: grAlphaCombine() - Implement full CCA_MSELECT, CCA_REVERSE_BLEND, CCA_ADD_ACLOCAL mapping
- [ ] Issue 3: grAlphaBlendFunction() - Preserve existing ALPHAMODE bits

### Color Combine Issues
- [ ] Issue 4: Fix CC_MSELECT mapping from factor parameter
- [ ] Issue 5: Implement CC_SUB_CLOCAL support for subtract functions
- [ ] Issue 6: Implement CC_ADD_ACLOCAL support for add functions

### Alpha Combine Issues
- [ ] Issue 7: Implement CCA_MSELECT from factor parameter

### Alpha Blending Issues
- [ ] Issue 8: Verify destination alpha buffer handling
- [ ] Issue 9: Implement blend modes 8-14 if needed for Voodoo 2+

### Depth Buffer Issues
- [ ] Issue 10: Add GR_DEPTHBUFFER_*_COMPARE_TO_BIAS modes
- [ ] Issue 11: Verify Z vs W coordinate handling with FBZMODE_WBUFFER_SELECT

### Texture Pipeline Issues
- [ ] Issue 12: Implement full LOD calculation in TEXTURE_PIPELINE
- [ ] Issue 13: Fix texture W coordinate for perspective correction
- [ ] Issue 14: Implement NCC table decoding
- [ ] Issue 15: Calculate all mipmap level offsets in grTexSource()
- [ ] Issue 16: Fix grTexCombine() bit positions for TC_REVERSE_BLEND

### Fog Implementation Issues
- [ ] Issue 17: Compute fogdelta values when fog table is written
- [ ] Issue 18: Verify fog table entry count and indexing

### Triangle Rasterization Issues
- [ ] Issue 19: Implement proper sub-pixel precision and diamond-exit rules
- [ ] Issue 20: Fix culling coordinate consistency
- [ ] Issue 21: Account for pixel center sampling

### LFB Issues
- [ ] Issue 22: Implement all LFB write modes in grLfbLock()
- [ ] Issue 23: Handle source format conversion in grLfbWriteRegion()

### Missing Functions
- [ ] Issue 24: Implement grTexDownloadMipMapLevelPartial()
- [ ] Issue 25: Implement grTexDetailControl()
- [ ] Issue 26: Implement grTexNCCTable()
- [ ] Issue 27: Implement grSplash()
- [ ] Issue 28: Implement grBufferNumPending()
- [ ] Issue 29: Implement grSstIdle()
- [ ] Issue 30: Implement grSstStatus()

### Register Mapping Issues
- [ ] Issue 31: Verify clipLeftRight/clipLowYHighY encoding
- [ ] Issue 32: Verify fbzMode default value (appears correct)
- [ ] Issue 33: Verify TMU1 register base address calculation

---

## References

- DOSBox-Staging voodoo.cpp: `references/dosbox-staging/voodoo.cpp`
- 3dfx Glide 3.x SDK: `references/glide3x/h5/glide3/src/`
- Voodoo register definitions: `src/voodoo_defs.h`
