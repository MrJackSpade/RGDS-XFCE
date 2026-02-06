# Task 03b: Eliminate Per-Pixel Alpha Source Select Switch

## Summary
Replace the per-pixel alpha source selection switch with a function pointer or loop inversion pattern.

## Current State

**File:** `src/voodoo_emu.c` lines 793-807

```c
switch (FBZCP_CC_ASELECT(r_fbzColorPath)) {
    case 0: /* iterated alpha */
    case 1: /* texture alpha */
    case 2: /* color1 alpha */
    case 3: /* LFB alpha */
}
```

This switch runs once per pixel to select the source for alpha values.

## Problem
- Only 4 cases, but runs ~480,000 times per frame (800x600 @ 50% coverage)
- Branch decisions are expensive under x86 emulation
- The selection doesn't change during scanline processing

## Proposed Solution

### Option A: Loop Inversion (Recommended)
Move switch outside the pixel loop, use macro for common loop body:

```c
#define ALPHA_PIXEL_LOOP(ALPHA_SELECT_CODE) \
    for (int32_t x = startx; x < stopx; x++) { \
        /* ... prior code ... */ \
        ALPHA_SELECT_CODE \
        /* ... rest of pixel processing ... */ \
    }

switch (FBZCP_CC_ASELECT(r_fbzColorPath)) {
    case 0: ALPHA_PIXEL_LOOP({ a = sa; }) break;
    case 1: ALPHA_PIXEL_LOOP({ a = texel.a; }) break;
    case 2: ALPHA_PIXEL_LOOP({ a = c1_a; }) break;
    case 3: ALPHA_PIXEL_LOOP({ a = lfb_a; }) break;
}
```

### Option B: Combined with RGB Select
Since this is adjacent to the RGB source select (Task 03a), combine both into a single loop inversion with 16 combinations (4 RGB × 4 Alpha), or use nested switches outside the loop.

```c
switch ((FBZCP_CC_RGBSELECT(r_fbzColorPath) << 2) | FBZCP_CC_ASELECT(r_fbzColorPath)) {
    case 0: /* RGB=iter, A=iter */ PIXEL_LOOP(...) break;
    case 1: /* RGB=iter, A=tex */  PIXEL_LOOP(...) break;
    // ... 14 more cases ...
}
```

## Files to Modify
- `src/voodoo_emu.c` - lines 793-807

## Related State Changes
- `src/glide3x_combine.c` - `grAlphaCombine()` sets fbzColorPath

## Testing Requirements
- [ ] All 4 alpha source modes render correctly
- [ ] No visual differences from current implementation
- [ ] Verify with games that use each mode (especially texture alpha for transparency)

## Dependencies
- Parent task: `tasks/03_Eliminate_PerPixel_Switches.md`
- Should be combined with Task 03a (RGB Source Select) for best results

## Expected Impact
- Eliminates ~480,000 branch decisions per frame for this switch
- Moderate impact - often case 0 or 1 (branch predictor may help)
