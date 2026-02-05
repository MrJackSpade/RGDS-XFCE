# Task 09: Early-Out Optimizations

## Summary
Add early-out checks to skip expensive work when possible, including alpha==0 transparency skip and triangle-level culling.

## Current State

### Pixel Pipeline Execution Order
**Files:** `src/voodoo_emu.c:730-900+`, `src/voodoo_pipeline.h`

```
Per-pixel pipeline:
1. Stipple check (PIXEL_PIPELINE_BEGIN)           → early-out ✓
2. Depth test (voodoo_pipeline.h:788-854)         → early-out BEFORE texture ✓
3. TEXTURE_PIPELINE                               → EXPENSIVE
4. Chroma key (voodoo_emu.c:790)                  → early-out AFTER texture
5. Alpha mask (voodoo_emu.c:810)                  → early-out AFTER texture
6. Alpha test (voodoo_emu.c:813)                  → early-out AFTER texture
7. Color combine                                  → ...
8. Fog (PIXEL_PIPELINE_MODIFY)                    → ...
9. Alpha blend (PIXEL_PIPELINE_MODIFY)            → reads framebuffer
10. Dither + Write (PIXEL_PIPELINE_FINISH)        → writes framebuffer
```

### What's Good
- **Depth test before texture:** Pixels failing depth skip the expensive TEXTURE_PIPELINE
- **Stipple check is first:** Failed stipple skips everything

### What's Suboptimal
- **Chroma key after texture:** Must sample texture to check chroma key
- **Alpha test after texture:** Alpha may come from texture, so can't move earlier
- **No alpha==0 skip:** Fully transparent pixels still do blending and write

### What's Missing
- **Triangle bounding box rejection:** No check if triangle is entirely off-screen before rasterization
- **Alpha==0 early-out:** Skip blend/write when final alpha is 0 and blend mode would produce invisible result

## Early-Out Opportunities

### 1. Alpha == 0 Transparency Skip (NEW)
**Location:** After alpha test, before blending (voodoo_emu.c ~line 813)

When final alpha is 0 AND blend mode is standard alpha blend (src=SRC_ALPHA), the pixel contribution is zero:
```
out = src * 0 + dst * (1 - 0) = dst
```

No need to read framebuffer, blend, or write.

```c
// After alpha test, before color combine
if (ALPHAMODE_ALPHABLEND(r_alphaMode)) {
    // Check if src alpha is 0 and src blend factor is SRC_ALPHA
    if (c_other.rgb.a == 0 && ALPHAMODE_SRCRGBBLEND(r_alphaMode) == 1) {
        // Result would be invisible, skip to next pixel
        goto skipdrawdepth;
    }
}
```

**Caveat:** This only works for certain blend modes. Need to verify blend factors.

### 2. Triangle Bounding Box Rejection (NEW)
**Location:** `src/glide3x_draw.c` grDrawTriangle(), before gradient calculation

```c
void __stdcall grDrawTriangle(const GrVertex *a, const GrVertex *b, const GrVertex *c)
{
    // ... vertex unpacking ...

    // Compute bounding box
    float min_x = fminf(fminf(ax, bx), cx);
    float max_x = fmaxf(fmaxf(ax, bx), cx);
    float min_y = fminf(fminf(ay, by), cy);
    float max_y = fmaxf(fmaxf(ay, by), cy);

    // Quick reject if entirely outside clip region
    if (max_x < g_voodoo->clip_left || min_x > g_voodoo->clip_right ||
        max_y < g_voodoo->clip_top || min_y > g_voodoo->clip_bottom) {
        return;  // Triangle entirely off-screen
    }

    // ... continue with gradient calculation ...
}
```

**Impact:** Avoids all gradient calculation and rasterizer setup for off-screen triangles.

### 3. Degenerate Triangle Early-Out (VERIFY)
**Location:** `src/glide3x_draw.c`

Check if already exists - skip triangles with zero area before doing any work.

### 4. Iterated Alpha Early-Out (CONDITIONAL)
**Location:** Before TEXTURE_PIPELINE

When alpha comes from iteration (not texture) AND alpha test is enabled with alpha=0 failing:

```c
// If alpha source is iterated (not texture)
if (FBZCP_CC_ASELECT(r_fbzColorPath) == 0) {  // iterated alpha
    // And alpha test would fail for alpha=0
    if (ALPHAMODE_ALPHATEST(r_alphaMode) &&
        ALPHAMODE_ALPHAFUNCTION(r_alphaMode) == 4 &&  // greater than
        iterargb.rgb.a == 0) {
        // Skip texture sampling - we know alpha test will fail
        goto skipdrawdepth;
    }
}
```

**Caveat:** Only works when alpha source is iterated, not texture.

### 5. Chromakey with Known Color (CONDITIONAL)
When RGB comes from iteration/color register (not texture), chroma key could be checked before texture sampling.

```c
// If RGB source is NOT texture
if (FBZCP_CC_RGBSELECT(r_fbzColorPath) != 1) {  // not texture
    // Can check chroma key before texture sampling
    APPLY_CHROMAKEY(...);
    // If passed, then sample texture for alpha only
}
```

**Caveat:** Rarely applicable - most rendering uses texture RGB.

## Implementation Priority

| Optimization | Impact | Complexity | Priority |
|--------------|--------|------------|----------|
| Triangle bounding box reject | High for UI/menus | Low | 1 |
| Alpha==0 skip (standard blend) | Medium | Low | 2 |
| Iterated alpha early-out | Low (rare case) | Low | 3 |
| Chromakey non-texture | Very low (rare) | Medium | 4 |

## Files to Modify

| File | Changes |
|------|---------|
| `src/glide3x_draw.c` | Add bounding box rejection in `grDrawTriangle()` |
| `src/voodoo_emu.c` | Add alpha==0 early-out in pixel loop |
| `src/voodoo_state.h` | May need to cache clip bounds for quick access |

## Risk Assessment
**Risk: LOW**
- Early-outs are additive - existing path still works
- Easy to test (visual output should be identical)
- Can be enabled/disabled independently

## Testing Requirements
- [ ] No visual differences from current rendering
- [ ] Off-screen triangles properly rejected
- [ ] Alpha==0 pixels properly skipped
- [ ] Performance improvement measurable
- [ ] Edge cases: triangles partially on-screen, alpha exactly 0 vs near-0

## Expected Impact
- **Bounding box rejection:** Major speedup for games with UI elements, menus, off-screen geometry
- **Alpha==0 skip:** Moderate speedup for particle effects, transparency-heavy scenes
- **Combined:** Estimated 5-15% improvement depending on scene content

## Dependencies
- Independent of other tasks
- Can be done early (low risk, easy win)

## Blend Modes Where Alpha==0 Skip is Safe

| Src Factor | Dst Factor | Safe to Skip? | Reason |
|------------|------------|---------------|--------|
| SRC_ALPHA (1) | ONE_MINUS_SRC_ALPHA (5) | YES | `src*0 + dst*1 = dst` |
| SRC_ALPHA (1) | ONE (4) | YES | `src*0 + dst = dst` |
| SRC_ALPHA (1) | ZERO (0) | YES | `src*0 + 0 = 0` (but still writes black) |
| ONE (4) | ONE_MINUS_SRC_ALPHA (5) | NO | `src + dst*1 = src + dst` |
| ONE (4) | ZERO (0) | NO | `src + 0 = src` |

Only safe when src factor uses SRC_ALPHA and we can prove result equals destination.

## Current Clip Window Implementation

**File:** `src/voodoo_emu.c:670-694`

```c
/* Y clipping buys us the whole scanline */
if (scry < (int32_t)((regs[clipLowYHighY].u >> 16) & 0x3ff) ||
    scry >= (int32_t)(regs[clipLowYHighY].u & 0x3ff)) {
    return;  // Skip entire scanline
}

/* X clipping */
int32_t tempclip = (regs[clipLeftRight].u >> 16) & 0x3ff;
if (startx < tempclip) startx = tempclip;
tempclip = regs[clipLeftRight].u & 0x3ff;
if (stopx >= tempclip) stopx = tempclip - 1;
```

This is per-scanline. Triangle-level rejection would happen earlier, before `voodoo_triangle()` is called.

## Notes
- Many games have debug/UI geometry that may be clipped off-screen
- Particle systems often have many fully-transparent particles
- The depth-before-texture optimization is already helping significantly
- Alpha test ordering can't be changed when alpha comes from texture
