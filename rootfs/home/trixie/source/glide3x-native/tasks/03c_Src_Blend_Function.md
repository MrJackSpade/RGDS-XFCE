# Task 03c: Eliminate Per-Pixel Source Blend Function Switch

## Summary
Replace the per-pixel source blend function switch with a function pointer or loop inversion pattern.

## Current State

**File:** `src/voodoo_pipeline.h` lines 502-542

Inside `APPLY_ALPHA_BLEND` macro:

```c
switch (ALPHAMODE_SRCRGBBLEND(r_alphaMode)) {
    case 0:  /* ZERO */
    case 1:  /* SRC_ALPHA */
    case 2:  /* COLOR */
    case 3:  /* DST_ALPHA */
    case 4:  /* ONE */
    case 5:  /* ONE_MINUS_SRC_ALPHA */
    case 6:  /* ONE_MINUS_COLOR */
    case 7:  /* ONE_MINUS_DST_ALPHA */
    case 8:  /* SATURATE (special) */
    case 15: /* ALOCAL (alpha local) */
    // ... potentially more cases
}
```

This switch runs once per pixel to determine how source RGB is blended.

## Problem
- 10+ cases, runs ~480,000 times per frame
- Very expensive under x86 emulation
- Blend mode is constant for entire draw call
- This is one of the most impactful switches to optimize

## Proposed Solution

### Option A: Function Pointers (Recommended)
```c
// In voodoo_state.h
typedef void (*src_blend_fn)(int *r, int *g, int *b, int sr, int sg, int sb,
                             int dr, int dg, int db, int sa, int da);
src_blend_fn cached_src_blend;

// Individual blend functions
static inline void blend_src_zero(int *r, int *g, int *b, ...) {
    *r = *g = *b = 0;
}
static inline void blend_src_src_alpha(int *r, int *g, int *b, int sr, int sg, int sb,
                                        int dr, int dg, int db, int sa, int da) {
    *r = (sr * sa) >> 8;
    *g = (sg * sa) >> 8;
    *b = (sb * sa) >> 8;
}
// ... etc

// Set at state change time in grAlphaBlendFunction()
void update_blend_functions(uint32_t alphaMode) {
    switch (ALPHAMODE_SRCRGBBLEND(alphaMode)) {
        case 0: cached_src_blend = blend_src_zero; break;
        case 1: cached_src_blend = blend_src_src_alpha; break;
        // ...
    }
}

// Pixel loop - no switch
cached_src_blend(&r, &g, &b, sr, sg, sb, dr, dg, db, sa, da);
```

### Option B: Fast Path for Common Modes
Most games use only a few blend combinations. Create fast paths:

```c
if (cached_blend_mode == BLEND_SRC_ALPHA_ONE_MINUS_SRC_ALPHA) {
    // Most common: standard alpha blend, inline
    r = (sr * sa + dr * (255 - sa)) >> 8;
    // ...
} else {
    // Generic fallback
    APPLY_ALPHA_BLEND(...);
}
```

## Files to Modify
- `src/voodoo_pipeline.h` - lines 502-542 (APPLY_ALPHA_BLEND macro)
- `src/voodoo_state.h` - Add function pointer fields
- `src/glide3x_blend.c` - Update in `grAlphaBlendFunction()`

## Related State Changes
- `grAlphaBlendFunction()` sets alphaMode

## Testing Requirements
- [ ] All blend modes render correctly:
  - [ ] GR_BLEND_ZERO
  - [ ] GR_BLEND_SRC_ALPHA
  - [ ] GR_BLEND_DST_COLOR
  - [ ] GR_BLEND_DST_ALPHA
  - [ ] GR_BLEND_ONE
  - [ ] GR_BLEND_ONE_MINUS_SRC_ALPHA
  - [ ] GR_BLEND_ONE_MINUS_DST_COLOR
  - [ ] GR_BLEND_ONE_MINUS_DST_ALPHA
  - [ ] GR_BLEND_SRC_ALPHA_SATURATE
- [ ] No visual differences from current implementation
- [ ] Test transparency effects in multiple games

## Dependencies
- Parent task: `tasks/03_Eliminate_PerPixel_Switches.md`
- Should be done with Task 03d (Dst Blend Function) as they work together

## Expected Impact
- HIGH IMPACT - blend functions are always used when alpha blending is enabled
- Eliminates ~480,000 branch decisions per frame
- Combined with 03d, this addresses the most expensive per-pixel switches
