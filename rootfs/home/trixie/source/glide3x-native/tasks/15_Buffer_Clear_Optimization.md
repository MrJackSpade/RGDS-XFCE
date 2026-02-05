# Task 15: Buffer Clear Optimization

## Summary
Optimize grBufferClear() by eliminating per-pixel multiplication overhead and using more efficient memory fill patterns.

## Current State

**File:** `src/glide3x_buffer.c:124-145`

```c
if (doColor) {
    int r = (color >> 16) & 0xFF;
    int g = (color >> 8) & 0xFF;
    int b = color & 0xFF;
    uint16_t color565 = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);

    for (y = 0; y < (int)g_voodoo->fbi.height; y++) {
        for (x = 0; x < (int)g_voodoo->fbi.width; x++) {
            dest[y * g_voodoo->fbi.rowpixels + x] = color565;
        }
    }
}

if (doDepth) {
    uint16_t depth16 = (uint16_t)(depth >> 16);
    for (y = 0; y < (int)g_voodoo->fbi.height; y++) {
        for (x = 0; x < (int)g_voodoo->fbi.width; x++) {
            depthbuf[y * g_voodoo->fbi.rowpixels + x] = depth16;
        }
    }
}
```

## Problem Analysis

### Issue 1: Per-Pixel Multiplication
Every pixel performs `y * rowpixels + x` to calculate the destination offset.

At 800x600 resolution:
- 480,000 pixels per buffer
- 480,000 multiplications per clear
- Color + depth = 960,000 multiplications per frame if both cleared

This is unnecessary because the address calculation is linear - we can compute the row base once and increment from there.

### Issue 2: No Row-Based Optimization
The inner loop iterates pixel-by-pixel when the fill value is uniform. For 16-bit values, a `memset`-like approach or vectorized fill would be faster.

### Issue 3: Separate Color/Depth Passes
Color and depth are cleared in separate passes. If both need clearing to the same value (common: black with max depth), a single pass could clear both.

## Proposed Solutions

### Solution 1: Per-Row Pointer Increment

Eliminate the multiplication by computing row base once:

```c
if (doColor) {
    uint16_t color565 = /* ... conversion ... */;
    uint16_t *row = dest;

    for (y = 0; y < height; y++) {
        for (x = 0; x < width; x++) {
            row[x] = color565;
        }
        row += rowpixels;  /* Move to next row */
    }
}
```

**Savings:** Eliminates 480,000 multiplications per buffer.

### Solution 2: Use wmemset or Optimized 16-bit Fill

For platforms with `wmemset` (POSIX) or similar:

```c
#include <wchar.h>

if (doColor) {
    uint16_t color565 = /* ... */;
    uint16_t *row = dest;

    for (y = 0; y < height; y++) {
        /* wmemset works on wchar_t (usually 16 or 32 bits) */
        /* May need platform-specific handling */
        for (x = 0; x < width; x++) {
            row[x] = color565;
        }
        row += rowpixels;
    }
}
```

Note: `wmemset` is for wide chars, not guaranteed 16-bit. A custom `memset16` is safer.

### Solution 3: Contiguous Buffer Optimization

If `rowpixels == width` (no row padding), clear entire buffer in one operation:

```c
if (doColor) {
    uint16_t color565 = /* ... */;

    if (rowpixels == width) {
        /* No padding - contiguous buffer */
        uint16_t *p = dest;
        uint16_t *end = dest + width * height;
        while (p < end) {
            *p++ = color565;
        }
    } else {
        /* Row-by-row with padding */
        /* ... Solution 1 code ... */
    }
}
```

### Solution 4: 32-bit Fill for 16-bit Values

Fill two pixels at once by combining two 16-bit values into a 32-bit write:

```c
if (doColor) {
    uint16_t color565 = /* ... */;
    uint32_t color32 = (uint32_t)color565 | ((uint32_t)color565 << 16);

    uint16_t *row = dest;
    for (y = 0; y < height; y++) {
        uint32_t *row32 = (uint32_t*)row;
        int x32 = width / 2;

        /* Fill pairs of pixels */
        for (int i = 0; i < x32; i++) {
            row32[i] = color32;
        }

        /* Handle odd width */
        if (width & 1) {
            row[width - 1] = color565;
        }

        row += rowpixels;
    }
}
```

**Note:** Requires 4-byte alignment of row pointer. Check buffer allocation alignment.

### Solution 5: SIMD Fill (SSE2/NEON)

For maximum performance, use SIMD to fill 8 or 16 pixels at once:

```c
#ifdef __SSE2__
#include <emmintrin.h>

if (doColor) {
    uint16_t color565 = /* ... */;
    __m128i fill = _mm_set1_epi16(color565);  /* 8x 16-bit values */

    uint16_t *row = dest;
    for (y = 0; y < height; y++) {
        int x = 0;

        /* SIMD fill - 8 pixels at a time */
        for (; x + 7 < width; x += 8) {
            _mm_storeu_si128((__m128i*)&row[x], fill);
        }

        /* Scalar remainder */
        for (; x < width; x++) {
            row[x] = color565;
        }

        row += rowpixels;
    }
}
#endif
```

**Note:** Given FEX emulation context, SSE2 may not provide benefits. The simple row-based optimization (Solution 1) is likely best for emulated x86.

### Solution 6: Combined Color+Depth Clear

If clearing both buffers to common values, interleave to improve cache locality:

```c
if (doColor && doDepth) {
    uint16_t color565 = /* ... */;
    uint16_t depth16 = /* ... */;

    uint16_t *color_row = dest;
    uint16_t *depth_row = depthbuf;

    for (y = 0; y < height; y++) {
        for (x = 0; x < width; x++) {
            color_row[x] = color565;
            depth_row[x] = depth16;
        }
        color_row += rowpixels;
        depth_row += rowpixels;
    }
} else if (doColor) {
    /* ... color only ... */
} else if (doDepth) {
    /* ... depth only ... */
}
```

**Trade-off:** Better cache locality vs. additional branching. May or may not help depending on buffer layout.

## Recommended Implementation

Start with **Solution 1** (per-row pointer increment) as it's:
- Simple to implement
- No platform dependencies
- Guaranteed improvement
- Low risk

Then benchmark and consider **Solution 4** (32-bit fill) if further optimization needed.

## Files to Modify

| File | Changes |
|------|---------|
| `src/glide3x_buffer.c` | Update `grBufferClear()` loops |

## Risk Assessment
**Risk: LOW**
- Isolated change to one function
- Easy to verify correctness (visual: screen should clear to correct color)
- No API changes
- Easy to revert if issues found

## Testing Requirements
- [ ] Color buffer clears to correct color
- [ ] Depth buffer clears to correct value
- [ ] Works with various resolutions (640x480, 800x600, 1024x768)
- [ ] Works when rowpixels != width (if applicable)
- [ ] No visual artifacts
- [ ] Performance improvement measurable

## Expected Impact

| Metric | Before | After |
|--------|--------|-------|
| Multiplications per frame | 960,000 | 0 |
| Memory access pattern | Random-ish | Sequential |
| Estimated speedup | - | 5-10% of grBufferClear time |

**Note:** Buffer clears happen once per frame, not per-pixel in the render loop. This optimization has moderate impact on overall frame time (likely < 1% total), but it's a quick win with low risk.

## Dependencies
- Independent of other tasks
- No prerequisites
- Can be implemented at any time

## Notes
- The SDK comment mentions real Voodoo hardware used "fastfill" mode for buffer clears - dedicated hardware that was extremely fast. Our software path will never match that, but we can at least avoid obviously inefficient patterns.
- Consider whether clip window should constrain clears (SDK says yes, current implementation ignores it). That's a separate correctness issue.
