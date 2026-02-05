# Task 03: Eliminate Per-Pixel Switch Statements

## Summary
Replace per-pixel switch statements with function pointers or specialized code paths selected at state-change time, removing branch overhead from the hot pixel loop.

## Current State

### Switch Statements in Pixel Loop
**File:** `src/voodoo_emu.c:773-807`

Inside the main pixel loop (`for (int32_t x = startx; x < stopx; x++)`):
```c
switch (FBZCP_CC_RGBSELECT(r_fbzColorPath)) { ... }  // lines 773-787
switch (FBZCP_CC_ASELECT(r_fbzColorPath)) { ... }    // lines 793-807
```

### Summary of All Per-Pixel Switches

| Location | Purpose | Cases | File |
|----------|---------|-------|------|
| lines 773-787 | RGB source select | 4 | voodoo_emu.c |
| lines 793-807 | Alpha source select | 4 | voodoo_emu.c |
| lines 502-542 | Src blend function | 10+ | voodoo_pipeline.h |
| lines 545-590 | Dst blend function | 10+ | voodoo_pipeline.h |
| lines 647-675 | Fog blend mode | 4 | voodoo_pipeline.h |
| lines 798-853 | Depth function | 8 | voodoo_pipeline.h |
| lines 970+ | Texture format | multiple | voodoo_pipeline.h |
| lines 1099-1135 | Tex RGB blend select | 6 | voodoo_pipeline.h |
| lines 1138-1171 | Tex alpha blend select | 6 | voodoo_pipeline.h |

**Total: 9+ switch statements per pixel** (when all features enabled)

## Problem
- State changes are rare (per-draw-call at most)
- Switches run millions of times per frame (once per pixel)
- Branch mispredictions are expensive, especially through x86 emulation (FEX)
- 800x600 frame with 50% coverage = ~480,000 pixels × 9 switches = **4.3 million branch decisions**

## Proposed Solutions

### Option A: Function Pointers (Moderate complexity)
Set function pointers on state change, call directly in pixel loop.

```c
// State change time (grColorCombine, grAlphaBlendFunction, etc.)
typedef void (*blend_func_t)(int *r, int *g, int *b, int sr, int sg, int sb, int dr, int dg, int db, int sa, int da);
blend_func_t src_blend_func;
blend_func_t dst_blend_func;

void select_blend_functions(uint32_t alphaMode) {
    switch (ALPHAMODE_SRCRGBBLEND(alphaMode)) {
        case 0: src_blend_func = blend_src_zero; break;
        case 1: src_blend_func = blend_src_src_alpha; break;
        // ...
    }
}

// Pixel loop - no switch
src_blend_func(&r, &g, &b, sr, sg, sb, dr, dg, db, sa, da);
```

**Pro:** Simple, maintainable
**Con:** Function call overhead per pixel, though less than switch

### Option B: Specialized Rasterizers (High complexity, best performance)
Generate specialized rasterizer functions for common state combinations.

```c
// Pre-generated or runtime-selected specialized functions
void rasterize_tex_blend_alphablend_nofog(...);
void rasterize_tex_modulate_noblend_fog(...);
// etc.

// State change time
select_rasterizer();  // Pick best match

// Draw time
current_rasterizer(triangle_data);  // No per-pixel switches
```

**Pro:** Best performance - no per-pixel overhead
**Con:** Combinatorial explosion of functions, complex to maintain

### Option C: Loop Inversion with Macro (Recommended for individual switches)
Move the switch **outside** the pixel loop, with the loop inside each case. Use a macro to avoid duplicating the common pixel loop code.

```c
// Define the common pixel loop body as a macro with a "hole" for varying code
#define PIXEL_LOOP_BODY(BLEND_CODE) \
    for (int32_t x = startx; x < stopx; x++) { \
        /* ... texture sampling, depth test, color combine ... */ \
        BLEND_CODE \
        /* ... write pixel ... */ \
    }

// Switch runs ONCE per scanline, not per pixel
switch (cached_blend_mode) {
    case BLEND_DISABLED:
        PIXEL_LOOP_BODY(/* nothing */)
        break;
    case BLEND_STANDARD_ALPHA:
        PIXEL_LOOP_BODY({
            int dpix = dest[x];
            int dr = (dpix >> 8) & 0xf8;
            // ... inline standard alpha blend ...
        })
        break;
    case BLEND_ADDITIVE:
        PIXEL_LOOP_BODY({
            int dpix = dest[x];
            r = clamp_to_uint8(r + ((dpix >> 8) & 0xf8));
            // ... inline additive blend ...
        })
        break;
    case BLEND_GENERIC:
        PIXEL_LOOP_BODY(APPLY_ALPHA_BLEND(...))  // Full switch fallback
        break;
}
```

**Pro:** No per-pixel overhead, common code stays in one place, moderate code duplication
**Con:** Macro debugging can be tricky, still some code expansion

**Key insight:** The switch moves from running 480,000 times (per pixel) to running once per scanline.

See also: `tasks/05_Blend_Mode_Fast_Paths.md` for detailed blend mode application of this pattern.

### Option D: Full Specialized Rasterizers (High complexity)
Use macros to generate complete specialized rasterizer functions for common state combinations.

```c
#define MAKE_RASTERIZER(NAME, RGB_SELECT, A_SELECT, SRC_BLEND, DST_BLEND) \
    static void rasterize_##NAME(...) { \
        /* Inline the specific case logic */ \
    }

MAKE_RASTERIZER(mode_0_0_1_5, 0, 0, 1, 5)  // Common D2 mode
MAKE_RASTERIZER(mode_1_1_1_5, 1, 1, 1, 5)  // Another common mode
```

**Pro:** Maximum performance - entire rasterizer specialized
**Con:** Combinatorial explosion, all rasterizer code duplicated per variant

### Option E: Hybrid Approach
1. Identify the 5-10 most common state combinations via runtime profiling
2. Create specialized fast paths for those combinations
3. Fall back to generic switch-based code for rare combinations

## Files to Modify

### For color/alpha source selection:
- `src/voodoo_emu.c` - lines 773-807

### For blend functions:
- `src/voodoo_pipeline.h` - `APPLY_ALPHA_BLEND` macro (lines 477-604)

### For fog:
- `src/voodoo_pipeline.h` - `APPLY_FOGGING` macro (lines 609-704)

### For depth testing:
- `src/voodoo_pipeline.h` - `PIXEL_PIPELINE_BEGIN` macro (lines 798-853)

### For texture combine:
- `src/voodoo_pipeline.h` - `TEXTURE_PIPELINE` macro (lines 1099-1171)

### For state tracking:
- `src/voodoo_state.h` - Add function pointer fields or rasterizer selector
- `src/glide3x_combine.c` - Update `grColorCombine`, `grAlphaCombine`
- `src/glide3x_blend.c` - Update `grAlphaBlendFunction`

## Implementation Order (Suggested)

1. **Depth function** - Only 8 cases, always used, good starting point
2. **Blend functions** - High impact, 10+ cases each but well-defined
3. **RGB/Alpha source select** - 4 cases each, straightforward
4. **Fog blend mode** - 4 cases, often disabled
5. **Texture combine** - Most complex, defer if others give enough speedup

## Risk Assessment
**Risk: MEDIUM-HIGH**
- Many code paths to modify
- Easy to introduce subtle rendering bugs
- Need comprehensive testing across different games/modes
- Function pointer overhead might not be much better than switch on some CPUs

## Testing Requirements
- [ ] All blend modes render correctly
- [ ] All depth functions work
- [ ] Fog renders correctly
- [ ] Texture combine modes all work
- [ ] No visual differences from current implementation
- [ ] Performance improvement measurable
- [ ] Test with multiple games using different state combinations

## Expected Impact
- Eliminates millions of branch decisions per frame
- Reduces branch misprediction penalties
- Especially beneficial under x86 emulation where branches are expensive
- Estimated: 10-30% speedup depending on implementation approach

## Dependencies
- Should be done after `tasks/01_Enable_Threading.md` (easier to test)
- Independent of `tasks/02_Preconvert_Textures.md`

## Notes
- The `r_fbzColorPath`, `r_fbzMode`, `r_alphaMode`, `r_fogMode` values are cached at the start of `raster_generic()` - they don't change during scanline processing
- The TEXMODE values are also constant per-triangle
- This constancy is what makes function pointer selection viable
