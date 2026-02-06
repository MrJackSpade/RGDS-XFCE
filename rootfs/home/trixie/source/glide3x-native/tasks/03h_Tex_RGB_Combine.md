# Task 03h: Eliminate Per-Pixel Texture RGB Combine Switch

## Summary
Replace the per-pixel texture RGB combine mode switch with a function pointer or loop inversion pattern.

## Current State

**File:** `src/voodoo_pipeline.h` lines 1099-1135

Inside `TEXTURE_PIPELINE` macro:

```c
switch (TEXMODE_TC_MSELECT(texMode)) {
    case 0: /* Zero */
    case 1: /* C_local (texture color) */
    case 2: /* A_other (alpha from other TMU) */
    case 3: /* A_local (texture alpha) */
    case 4: /* detail factor */
    case 5: /* LOD fraction */
}
```

This switch selects how texture RGB is combined in the texture pipeline.

## Problem
- 6 cases, runs ~480,000 times per frame per TMU
- Texture combine mode is constant for entire draw call
- With 2 TMUs, could be 960,000+ switch evaluations per frame

## Proposed Solution

### Option A: Function Pointers (Recommended)
```c
typedef int (*tex_mselect_fn)(int c_local, int a_other, int a_local, int detail, int lod_frac);
tex_mselect_fn cached_tc_mselect[2];  // One per TMU

// Individual selection functions
static inline int tc_mselect_zero(int c_local, int a_other, int a_local, int detail, int lod_frac) {
    return 0;
}
static inline int tc_mselect_c_local(int c_local, int a_other, int a_local, int detail, int lod_frac) {
    return c_local;
}
static inline int tc_mselect_a_other(int c_local, int a_other, int a_local, int detail, int lod_frac) {
    return a_other;
}
// ... etc

// Set at state change time
void update_texture_combine(int tmu, uint32_t texMode) {
    switch (TEXMODE_TC_MSELECT(texMode)) {
        case 0: cached_tc_mselect[tmu] = tc_mselect_zero; break;
        case 1: cached_tc_mselect[tmu] = tc_mselect_c_local; break;
        case 2: cached_tc_mselect[tmu] = tc_mselect_a_other; break;
        // ...
    }
}
```

### Option B: Combined RGB+Alpha Function
Since Task 03i (Tex Alpha Combine) is related, combine both into one function:

```c
typedef void (*tex_combine_fn)(rgba_t *result, const rgba_t *c_local,
                                const rgba_t *c_other, int detail, int lod_frac);

// Set based on both TC_MSELECT and TCA_MSELECT
void update_texture_combine_full(int tmu, uint32_t texMode) {
    int rgb_mode = TEXMODE_TC_MSELECT(texMode);
    int alpha_mode = TEXMODE_TCA_MSELECT(texMode);
    // Select from 6*6 = 36 combinations, or use common fast paths
}
```

### Option C: Fast Path for Common Mode
Most games use simple texture modulation:

```c
if (cached_tc_mode == TC_MODE_C_LOCAL) {
    // Just use texture color directly
    result_r = texel.r;
    result_g = texel.g;
    result_b = texel.b;
} else {
    // Fallback to function pointer
    cached_tc_mselect[tmu](...)
}
```

## Files to Modify
- `src/voodoo_pipeline.h` - lines 1099-1135 (TEXTURE_PIPELINE macro)
- `src/voodoo_state.h` - Add function pointer fields per TMU
- `src/glide3x_combine.c` - Update in `grTexCombine()` or similar

## Related State Changes
- `grTexCombine()` sets texture combine modes
- `grTexCombineFunction()` may also be involved

## Testing Requirements
- [ ] All texture RGB combine modes work:
  - [ ] Zero
  - [ ] C_local (texture color)
  - [ ] A_other (alpha from other TMU)
  - [ ] A_local (texture alpha)
  - [ ] Detail factor
  - [ ] LOD fraction
- [ ] Multi-texture blending works correctly
- [ ] No visual differences from current implementation

## Dependencies
- Parent task: `tasks/03_Eliminate_PerPixel_Switches.md`
- Should be done with Task 03i (Tex Alpha Combine) as they're closely related
- Lower priority than blend functions (03c, 03d) and depth (03f)

## Expected Impact
- MEDIUM-HIGH IMPACT - texture combine is used whenever texturing is enabled
- Eliminates ~480,000+ branch decisions per frame per TMU
- Combined with 03i, addresses texture pipeline overhead
