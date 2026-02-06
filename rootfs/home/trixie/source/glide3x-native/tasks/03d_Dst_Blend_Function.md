# Task 03d: Eliminate Per-Pixel Destination Blend Function Switch

## Summary
Replace the per-pixel destination blend function switch with a function pointer or loop inversion pattern.

## Current State

**File:** `src/voodoo_pipeline.h` lines 545-590

Inside `APPLY_ALPHA_BLEND` macro:

```c
switch (ALPHAMODE_DSTRGBBLEND(r_alphaMode)) {
    case 0:  /* ZERO */
    case 1:  /* SRC_ALPHA */
    case 2:  /* COLOR */
    case 3:  /* DST_ALPHA */
    case 4:  /* ONE */
    case 5:  /* ONE_MINUS_SRC_ALPHA */
    case 6:  /* ONE_MINUS_COLOR */
    case 7:  /* ONE_MINUS_DST_ALPHA */
    // ... potentially more cases
}
```

This switch runs once per pixel to determine how destination RGB is blended.

## Problem
- 10+ cases, runs ~480,000 times per frame
- Very expensive under x86 emulation
- Blend mode is constant for entire draw call
- This is one of the most impactful switches to optimize

## Proposed Solution

### Option A: Function Pointers (Recommended)
```c
// In voodoo_state.h
typedef void (*dst_blend_fn)(int *r, int *g, int *b, int sr, int sg, int sb,
                             int dr, int dg, int db, int sa, int da);
dst_blend_fn cached_dst_blend;

// Individual blend functions
static inline void blend_dst_zero(int *r, int *g, int *b, ...) {
    *r = *g = *b = 0;
}
static inline void blend_dst_one_minus_src_alpha(int *r, int *g, int *b, int sr, int sg, int sb,
                                                  int dr, int dg, int db, int sa, int da) {
    int inv_sa = 255 - sa;
    *r = (dr * inv_sa) >> 8;
    *g = (dg * inv_sa) >> 8;
    *b = (db * inv_sa) >> 8;
}
// ... etc

// Set at state change time in grAlphaBlendFunction()
void update_blend_functions(uint32_t alphaMode) {
    switch (ALPHAMODE_DSTRGBBLEND(alphaMode)) {
        case 0: cached_dst_blend = blend_dst_zero; break;
        case 5: cached_dst_blend = blend_dst_one_minus_src_alpha; break;
        // ...
    }
}
```

### Option B: Combined Src+Dst Fast Paths
The most common combination is SRC_ALPHA + ONE_MINUS_SRC_ALPHA. Create a combined fast path:

```c
typedef void (*combined_blend_fn)(int *r, int *g, int *b, int sr, int sg, int sb,
                                   int dr, int dg, int db, int sa);

// Standard alpha blend - most common case
static inline void blend_standard_alpha(int *r, int *g, int *b,
                                        int sr, int sg, int sb,
                                        int dr, int dg, int db, int sa) {
    int inv_sa = 255 - sa;
    *r = ((sr * sa) + (dr * inv_sa)) >> 8;
    *g = ((sg * sa) + (dg * inv_sa)) >> 8;
    *b = ((sb * sa) + (db * inv_sa)) >> 8;
}
```

## Files to Modify
- `src/voodoo_pipeline.h` - lines 545-590 (APPLY_ALPHA_BLEND macro)
- `src/voodoo_state.h` - Add function pointer fields
- `src/glide3x_blend.c` - Update in `grAlphaBlendFunction()`

## Related State Changes
- `grAlphaBlendFunction()` sets alphaMode

## Testing Requirements
- [ ] All destination blend modes render correctly:
  - [ ] GR_BLEND_ZERO
  - [ ] GR_BLEND_SRC_ALPHA
  - [ ] GR_BLEND_DST_COLOR
  - [ ] GR_BLEND_DST_ALPHA
  - [ ] GR_BLEND_ONE
  - [ ] GR_BLEND_ONE_MINUS_SRC_ALPHA
  - [ ] GR_BLEND_ONE_MINUS_DST_COLOR
  - [ ] GR_BLEND_ONE_MINUS_DST_ALPHA
- [ ] No visual differences from current implementation
- [ ] Test with games using additive blending (src=ONE, dst=ONE)

## Dependencies
- Parent task: `tasks/03_Eliminate_PerPixel_Switches.md`
- Should be done with Task 03c (Src Blend Function) as they work together

## Expected Impact
- HIGH IMPACT - blend functions are always used when alpha blending is enabled
- Eliminates ~480,000 branch decisions per frame
- Combined with 03c, this addresses the most expensive per-pixel switches
