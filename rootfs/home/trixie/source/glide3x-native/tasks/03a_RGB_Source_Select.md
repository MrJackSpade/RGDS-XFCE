# Task 03a: Eliminate Per-Pixel RGB Source Select Switch

## Summary
Replace the per-pixel RGB source selection switch with a function pointer or loop inversion pattern.

## Current State

**File:** `src/voodoo_emu.c` lines 773-787

```c
switch (FBZCP_CC_RGBSELECT(r_fbzColorPath)) {
    case 0: /* iterated RGB */
    case 1: /* texture RGB */
    case 2: /* color1 RGB */
    case 3: /* LFB RGB */
}
```

This switch runs once per pixel to select the source for RGB color values.

## Problem
- Only 4 cases, but runs ~480,000 times per frame (800x600 @ 50% coverage)
- Branch decisions are expensive under x86 emulation
- The selection doesn't change during scanline processing

## Proposed Solution

### Option A: Loop Inversion (Recommended)
Move switch outside the pixel loop, use macro for common loop body:

```c
#define RGB_PIXEL_LOOP(RGB_SELECT_CODE) \
    for (int32_t x = startx; x < stopx; x++) { \
        /* ... prior code ... */ \
        RGB_SELECT_CODE \
        /* ... rest of pixel processing ... */ \
    }

switch (FBZCP_CC_RGBSELECT(r_fbzColorPath)) {
    case 0: RGB_PIXEL_LOOP({ r = sr; g = sg; b = sb; }) break;
    case 1: RGB_PIXEL_LOOP({ r = texel.r; g = texel.g; b = texel.b; }) break;
    case 2: RGB_PIXEL_LOOP({ r = c1_r; g = c1_g; b = c1_b; }) break;
    case 3: RGB_PIXEL_LOOP({ r = lfb_r; g = lfb_g; b = lfb_b; }) break;
}
```

### Option B: Function Pointers
```c
typedef void (*rgb_select_fn)(int *r, int *g, int *b, ...);
rgb_select_fn cached_rgb_select;  // Set at state change time
```

## Files to Modify
- `src/voodoo_emu.c` - lines 773-787

## Related State Changes
- `src/glide3x_combine.c` - `grColorCombine()` sets fbzColorPath

## Testing Requirements
- [ ] All 4 RGB source modes render correctly
- [ ] No visual differences from current implementation
- [ ] Verify with games that use each mode

## Dependencies
- Parent task: `tasks/03_Eliminate_PerPixel_Switches.md`
- Can be combined with Task 03b (Alpha Source Select) since they're adjacent

## Expected Impact
- Eliminates ~480,000 branch decisions per frame for this switch
- Moderate impact - often case 0 or 1 (branch predictor may help)
