# Task 05: Blend Mode Fast Paths via Loop Inversion

## Summary
Invert the control flow so that blend mode selection happens once per draw call, with the pixel loop inside each case, eliminating per-pixel switch overhead entirely.

## Current State (Bad)

**File:** `src/voodoo_emu.c` and `src/voodoo_pipeline.h`

```c
// Current: switch INSIDE the pixel loop
for (int32_t x = startx; x < stopx; x++) {
    // ... texture sampling, etc ...

    APPLY_ALPHA_BLEND(...);  // Contains 2 switch statements

    // ... write pixel ...
}
```

The `APPLY_ALPHA_BLEND` macro (voodoo_pipeline.h:477-603) contains two switches that run **every pixel**:
- `switch (ALPHAMODE_SRCRGBBLEND(ALPHAMODE))` - 10 cases
- `switch (ALPHAMODE_DSTRGBBLEND(ALPHAMODE))` - 10 cases

## Proposed Solution: Loop Inversion

Move the blend mode decision **outside** the pixel loop:

```c
// Better: pixel loop INSIDE the switch
switch (cached_blend_mode) {
    case BLEND_MODE_DISABLED:
        for (int32_t x = startx; x < stopx; x++) {
            // ... texture sampling ...
            // NO blend code at all
            // ... write pixel ...
        }
        break;

    case BLEND_MODE_STANDARD_ALPHA:
        for (int32_t x = startx; x < stopx; x++) {
            // ... texture sampling ...
            // Inline: out = src*sa + dst*(1-sa)
            int dpix = dest[x];
            int dr = (dpix >> 8) & 0xf8;
            int dg = (dpix >> 3) & 0xfc;
            int db = (dpix << 3) & 0xf8;
            int inv_sa = 0x100 - a;
            r = (r * (a + 1) + dr * inv_sa) >> 8;
            g = (g * (a + 1) + dg * inv_sa) >> 8;
            b = (b * (a + 1) + db * inv_sa) >> 8;
            // ... write pixel ...
        }
        break;

    case BLEND_MODE_ADDITIVE:
        for (int32_t x = startx; x < stopx; x++) {
            // ... texture sampling ...
            // Inline: out = src + dst
            int dpix = dest[x];
            r = clamp_to_uint8(r + ((dpix >> 8) & 0xf8));
            g = clamp_to_uint8(g + ((dpix >> 3) & 0xfc));
            b = clamp_to_uint8(b + ((dpix << 3) & 0xf8));
            // ... write pixel ...
        }
        break;

    // ... other common modes ...

    case BLEND_MODE_GENERIC:
        for (int32_t x = startx; x < stopx; x++) {
            // ... texture sampling ...
            APPLY_ALPHA_BLEND(...);  // Full switch version for rare modes
            // ... write pixel ...
        }
        break;
}
```

## Why This is Different from Task 03

| Approach | Per-Pixel Overhead | Code Duplication |
|----------|-------------------|------------------|
| Task 03 Option A (function pointers) | Function call per pixel | Low |
| Task 03 Option B (full specialized rasterizers) | None | Very high (entire rasterizer duplicated) |
| **Task 05 (loop inversion)** | None | Medium (just the pixel loop duplicated) |

Task 05 is a middle ground: duplicate only the inner pixel loop, not the entire rasterizer setup code.

## Common Blend Modes to Specialize

| Mode | Src | Dst | Formula | Usage |
|------|-----|-----|---------|-------|
| Disabled | - | - | `out = src` | Opaque geometry |
| Standard Alpha | 1 | 5 | `out = src*sa + dst*(1-sa)` | Transparency |
| Additive | 4 | 4 | `out = src + dst` | Particles, glow |
| Modulate | 2 | 0 | `out = src * dst` | Lightmaps |
| Pre-multiplied | 4 | 5 | `out = src + dst*(1-sa)` | Pre-mult textures |

## Implementation Approach

### Step 1: Add blend mode cache to state
**File:** `src/voodoo_state.h`

```c
typedef enum {
    BLEND_MODE_DISABLED,
    BLEND_MODE_STANDARD_ALPHA,
    BLEND_MODE_ADDITIVE,
    BLEND_MODE_MODULATE,
    BLEND_MODE_PREMULT_ALPHA,
    BLEND_MODE_GENERIC
} blend_mode_t;

// Add to fbi_state or voodoo_state:
blend_mode_t cached_blend_mode;
```

### Step 2: Detect blend mode at state change
**File:** `src/glide3x_blend.c`

```c
void grAlphaBlendFunction(...) {
    // ... existing code ...

    // Cache the blend mode for fast dispatch
    v->cached_blend_mode = detect_blend_mode(v->reg[alphaMode]);
}

static blend_mode_t detect_blend_mode(uint32_t alphaMode) {
    if (!ALPHAMODE_ALPHABLEND(alphaMode))
        return BLEND_MODE_DISABLED;

    int src = ALPHAMODE_SRCRGBBLEND(alphaMode);
    int dst = ALPHAMODE_DSTRGBBLEND(alphaMode);

    if (src == 1 && dst == 5) return BLEND_MODE_STANDARD_ALPHA;
    if (src == 4 && dst == 4) return BLEND_MODE_ADDITIVE;
    if (src == 2 && dst == 0) return BLEND_MODE_MODULATE;
    if (src == 4 && dst == 5) return BLEND_MODE_PREMULT_ALPHA;

    return BLEND_MODE_GENERIC;
}
```

### Step 3: Create specialized pixel loops
**File:** `src/voodoo_emu.c`

Use macros to avoid duplicating non-blend code:

```c
#define PIXEL_LOOP_CORE(BLEND_CODE) \
    for (int32_t x = startx; x < stopx; x++) { \
        /* ... all the texture/color/depth code ... */ \
        BLEND_CODE \
        /* ... write pixel ... */ \
    }

// In raster_generic():
switch (v->cached_blend_mode) {
    case BLEND_MODE_DISABLED:
        PIXEL_LOOP_CORE(/* no blend */)
        break;
    case BLEND_MODE_STANDARD_ALPHA:
        PIXEL_LOOP_CORE(BLEND_STANDARD_ALPHA_INLINE(x, r, g, b, a))
        break;
    // ...
}
```

## Files to Modify

- `src/voodoo_state.h` - Add `blend_mode_t` enum and cache field
- `src/glide3x_blend.c` - Detect and cache blend mode in `grAlphaBlendFunction()`
- `src/voodoo_emu.c` - Restructure `raster_generic()` with inverted loops

## Risk Assessment
**Risk: MEDIUM**
- Significant restructuring of the main rasterization loop
- Need to ensure all pixel loop variants stay in sync
- Macro approach can make debugging harder

## Testing Requirements
- [ ] All blend modes render correctly
- [ ] No visual differences from current implementation
- [ ] Performance improvement measurable
- [ ] Test games that use different blend modes

## Expected Impact
- Eliminates 2 switch statements (20+ cases total) from the hot pixel loop
- Switch runs once per scanline/triangle instead of once per pixel
- Estimated: 15-25% speedup for scenes with blending

## Relationship to Task 03

This task focuses specifically on blend modes with the loop inversion pattern. Task 03 covers all 9+ per-pixel switches with multiple approaches (function pointers, specialized rasterizers, etc.).

These tasks can be:
1. **Combined:** Apply loop inversion to all per-pixel switches (becomes Task 03 Option B)
2. **Sequential:** Do Task 05 first for quick win, then Task 03 for remaining switches
3. **Alternative:** Choose one approach for all switches

## Notes
- The `PIXEL_LOOP_CORE` macro approach keeps non-blend code in one place
- Could extend this pattern to other per-pixel switches (depth test, fog, etc.)
- If extended to all switches, this becomes the "specialized rasterizers" approach
