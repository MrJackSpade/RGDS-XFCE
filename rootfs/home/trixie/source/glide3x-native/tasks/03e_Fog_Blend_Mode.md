# Task 03e: Eliminate Per-Pixel Fog Blend Mode Switch

## Summary
Replace the per-pixel fog blend mode switch with a function pointer or conditional bypass.

## Current State

**File:** `src/voodoo_pipeline.h` lines 647-675

Inside `APPLY_FOGGING` macro:

```c
switch (FOGMODE_FOG_ADD(r_fogMode)) {
    case 0: /* fog blend mode 0 */
    case 1: /* fog blend mode 1 */
    case 2: /* fog blend mode 2 */
    case 3: /* fog blend mode 3 */
}
```

This switch runs once per pixel when fogging is enabled.

## Problem
- Only 4 cases, but runs ~480,000 times per frame when fog is enabled
- Fog is often disabled entirely, making this switch unnecessary
- When enabled, fog mode doesn't change during draw calls

## Proposed Solution

### Option A: Early Disable Check (Recommended First Step)
```c
// Check fog enabled ONCE at scanline start
if (!FOGMODE_ENABLE_FOG(r_fogMode)) {
    // Skip all fog processing in pixel loop
    PIXEL_LOOP_NO_FOG(...)
} else {
    switch (FOGMODE_FOG_ADD(r_fogMode)) {
        case 0: PIXEL_LOOP_FOG_MODE_0(...) break;
        case 1: PIXEL_LOOP_FOG_MODE_1(...) break;
        case 2: PIXEL_LOOP_FOG_MODE_2(...) break;
        case 3: PIXEL_LOOP_FOG_MODE_3(...) break;
    }
}
```

### Option B: Function Pointers
```c
typedef void (*fog_fn)(int *r, int *g, int *b, int fog_factor, int fog_r, int fog_g, int fog_b);
fog_fn cached_fog_func;  // NULL if fog disabled

// In pixel loop
if (cached_fog_func) {
    cached_fog_func(&r, &g, &b, fog_factor, fog_r, fog_g, fog_b);
}
```

### Option C: Fog Enable Flag in Pipeline
Add a boolean `fog_enabled` that's set at state change time:

```c
// Set in grFogMode()
v->fog_enabled = FOGMODE_ENABLE_FOG(fogMode);
v->fog_add_mode = FOGMODE_FOG_ADD(fogMode);

// In pixel loop - branch predictor should handle this well
if (v->fog_enabled) {
    // Inline fog for the specific mode (no switch)
}
```

## Files to Modify
- `src/voodoo_pipeline.h` - lines 647-675 (APPLY_FOGGING macro) and lines 609-704 (full fog section)
- `src/voodoo_state.h` - Add fog enable flag and cached mode
- `src/glide3x_fog.c` or equivalent - Update in `grFogMode()`

## Related State Changes
- `grFogMode()` sets fogMode
- `grFogColorValue()` sets fog color
- `grFogTable()` sets fog table

## Testing Requirements
- [ ] All fog modes render correctly when enabled
- [ ] Fog disabled renders correctly (no fog applied)
- [ ] Distance fog works
- [ ] Fog table interpolation works
- [ ] No visual differences from current implementation

## Dependencies
- Parent task: `tasks/03_Eliminate_PerPixel_Switches.md`
- Lower priority than blend functions (03c, 03d) since fog is often disabled

## Expected Impact
- MEDIUM IMPACT - depends on fog usage
- Many games don't use fog, so the early disable check has the most value
- When fog is used, eliminates ~480,000 branch decisions per frame
- Fog is less commonly used than blending, so lower priority
