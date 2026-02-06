# Task 03i: Eliminate Per-Pixel Texture Alpha Combine Switch

## Summary
Replace the per-pixel texture alpha combine mode switch with a function pointer or loop inversion pattern.

## Current State

**File:** `src/voodoo_pipeline.h` lines 1138-1171

Inside `TEXTURE_PIPELINE` macro:

```c
switch (TEXMODE_TCA_MSELECT(texMode)) {
    case 0: /* Zero */
    case 1: /* C_local (texture color - used as alpha?) */
    case 2: /* A_other (alpha from other TMU) */
    case 3: /* A_local (texture alpha) */
    case 4: /* detail factor */
    case 5: /* LOD fraction */
}
```

This switch selects how texture alpha is combined in the texture pipeline.

## Problem
- 6 cases, runs ~480,000 times per frame per TMU
- Texture combine mode is constant for entire draw call
- With 2 TMUs, could be 960,000+ switch evaluations per frame

## Proposed Solution

### Option A: Function Pointers (Recommended)
```c
typedef int (*tex_aselect_fn)(int c_local, int a_other, int a_local, int detail, int lod_frac);
tex_aselect_fn cached_tca_mselect[2];  // One per TMU

// Individual selection functions (same as RGB, can reuse)
static inline int tca_mselect_zero(int c_local, int a_other, int a_local, int detail, int lod_frac) {
    return 0;
}
static inline int tca_mselect_a_local(int c_local, int a_other, int a_local, int detail, int lod_frac) {
    return a_local;
}
static inline int tca_mselect_a_other(int c_local, int a_other, int a_local, int detail, int lod_frac) {
    return a_other;
}
// ... etc

// Set at state change time
void update_texture_alpha_combine(int tmu, uint32_t texMode) {
    switch (TEXMODE_TCA_MSELECT(texMode)) {
        case 0: cached_tca_mselect[tmu] = tca_mselect_zero; break;
        case 3: cached_tca_mselect[tmu] = tca_mselect_a_local; break;
        // ...
    }
}
```

### Option B: Combined with RGB (Preferred)
Since this is closely related to Task 03h, combine both RGB and Alpha selection:

```c
// Combined function that handles both RGB and Alpha combine
typedef void (*tex_combine_full_fn)(int *out_r, int *out_g, int *out_b, int *out_a,
                                     int tex_r, int tex_g, int tex_b, int tex_a,
                                     int other_r, int other_g, int other_b, int other_a,
                                     int detail, int lod_frac);

// Common combinations as fast paths
static inline void tex_combine_local_local(...) {
    // RGB from texture, Alpha from texture - most common
    *out_r = tex_r;
    *out_g = tex_g;
    *out_b = tex_b;
    *out_a = tex_a;
}

static inline void tex_combine_local_other(...) {
    // RGB from texture, Alpha from other TMU
    *out_r = tex_r;
    *out_g = tex_g;
    *out_b = tex_b;
    *out_a = other_a;
}
```

### Option C: State Flags
Pre-compute simple flags for common modes:

```c
// In state structure
bool tex_use_local_rgb;
bool tex_use_local_alpha;

// In pixel loop
if (tex_use_local_rgb && tex_use_local_alpha) {
    // Fast path: just use texture directly
    result = texel;
} else {
    // Fallback to function pointer
}
```

## Files to Modify
- `src/voodoo_pipeline.h` - lines 1138-1171 (TEXTURE_PIPELINE macro)
- `src/voodoo_state.h` - Add function pointer fields per TMU
- `src/glide3x_combine.c` - Update in `grTexCombine()` or similar

## Related State Changes
- `grTexCombine()` sets texture combine modes
- `grTexCombineFunction()` may also be involved

## Testing Requirements
- [ ] All texture alpha combine modes work:
  - [ ] Zero
  - [ ] C_local
  - [ ] A_other (alpha from other TMU)
  - [ ] A_local (texture alpha)
  - [ ] Detail factor
  - [ ] LOD fraction
- [ ] Transparency from textures works correctly
- [ ] Multi-texture alpha blending works
- [ ] No visual differences from current implementation

## Dependencies
- Parent task: `tasks/03_Eliminate_PerPixel_Switches.md`
- Should be done with Task 03h (Tex RGB Combine) as they're closely related
- Lower priority than blend functions (03c, 03d) and depth (03f)

## Expected Impact
- MEDIUM-HIGH IMPACT - texture combine is used whenever texturing is enabled
- Eliminates ~480,000+ branch decisions per frame per TMU
- Combined with 03h, addresses texture pipeline overhead

## Implementation Note
Tasks 03h and 03i should likely be implemented together since they're in the same code area and affect the same TEXTURE_PIPELINE macro. A combined approach would:
1. Create a single function pointer for full texture combine (RGB + Alpha)
2. Pre-generate common combinations as specialized functions
3. Fall back to generic combine for rare modes
