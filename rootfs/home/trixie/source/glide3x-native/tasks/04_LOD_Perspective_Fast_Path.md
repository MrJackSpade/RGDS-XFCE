# Task 04: LOD/Perspective Calculation Fast Path

## Summary
Add fast paths for LOD and perspective calculations when features are disabled or constant, eliminating unnecessary per-pixel math.

## Current State

### Perspective Correction (Per-Pixel)
**File:** `src/voodoo_pipeline.h:908-920`

```c
if (TEXMODE_ENABLE_PERSPECTIVE(TEXMODE))
{
    oow = fast_reciplog((ITERW), &lod);    // Expensive per-pixel!
    s = (int32_t)((oow * (ITERS)) >> 29);
    t = (int32_t)((oow * (ITERT)) >> 29);
    lod += (LODBASE);
}
else
{
    s = (int32_t)((ITERS) >> 14);
    t = (int32_t)((ITERT) >> 14);
    lod = (LODBASE);
}
```

### fast_reciplog() is Complex
**File:** `src/voodoo_pipeline.h:98-162`

Per-pixel `fast_reciplog()` does:
- Sign handling
- 64-bit overflow check
- Leading zero count
- Table lookup with interpolation (4 table reads)
- Multiple conditionals and shifts
- Returns both reciprocal and log2 value

This runs **every pixel** when perspective correction is enabled.

### LOD Calculation (Per-Pixel)
**File:** `src/voodoo_pipeline.h:927-942`

```c
lod += (TT)->lodbias;
if (TEXMODE_ENABLE_LOD_DITHER(TEXMODE))
    if (DITHER4)
        lod += (DITHER4)[(XX) & 3] << 4;
if (lod < (TT)->lodmin)
    lod = (TT)->lodmin;
if (lod > (TT)->lodmax)
    lod = (TT)->lodmax;

ilod = lod >> 8;
if (!(((TT)->lodmask >> ilod) & 1))
    ilod++;

texbase = (TT)->lodoffset[ilod];
smax = (TT)->wmask >> ilod;
tmax = (TT)->hmask >> ilod;
```

Even when mipmapping is disabled (`lodmin == lodmax`), this code still runs.

## Problem

1. **Perspective correction**: `fast_reciplog()` is expensive and runs every pixel
2. **LOD when constant**: If `lodmin == lodmax` (no mipmapping), LOD is known at setup time
3. **Combined overhead**: Many games don't use mipmapping or perspective-correct textures, but still pay the cost

## Proposed Solution

### Option A: Compile-time Specialization (Best)
Create specialized TEXTURE_PIPELINE variants for common cases:

```c
// No perspective, no LOD variation
#define TEXTURE_PIPELINE_FLAT(TT, XX, TEXMODE, LOOKUP, ITERS, ITERT, RESULT) \
do { \
    int32_t s = (int32_t)((ITERS) >> 14); \
    int32_t t = (int32_t)((ITERT) >> 14); \
    /* Use pre-computed texbase, smax, tmax from setup */ \
    ...
} while(0)

// With perspective, no LOD variation
#define TEXTURE_PIPELINE_PERSP_NOLOD(TT, XX, TEXMODE, LOOKUP, ITERS, ITERT, ITERW, RESULT) \
do { \
    int64_t oow = fast_reciplog((ITERW), NULL);  // Don't need log2 output
    int32_t s = (int32_t)((oow * (ITERS)) >> 29); \
    int32_t t = (int32_t)((oow * (ITERT)) >> 29); \
    /* Use pre-computed texbase, smax, tmax from setup */ \
    ...
} while(0)
```

### Option B: Runtime Fast Path (Simpler)
Add runtime checks at triangle setup, not per-pixel:

```c
// In prepare_tmu() or triangle setup:
if (t->lodmin == t->lodmax) {
    t->fast_lod = true;
    t->cached_ilod = t->lodmin >> 8;
    t->cached_texbase = t->lodoffset[t->cached_ilod];
    t->cached_smax = t->wmask >> t->cached_ilod;
    t->cached_tmax = t->hmask >> t->cached_ilod;
}
```

Then in TEXTURE_PIPELINE:
```c
if ((TT)->fast_lod) {
    // Skip LOD calculation, use cached values
    texbase = (TT)->cached_texbase;
    smax = (TT)->cached_smax;
    tmax = (TT)->cached_tmax;
} else {
    // Existing LOD calculation
    ...
}
```

### Option C: Simplified fast_reciplog() for Non-LOD Case
When LOD result isn't needed, use a simpler reciprocal:

```c
static inline int64_t fast_recip_only(int64_t value) {
    // Simpler version that doesn't compute log2
    // Can skip half the work
}
```

## Files to Modify

### For TEXTURE_PIPELINE changes:
- `src/voodoo_pipeline.h` - lines 896-1070+ (TEXTURE_PIPELINE macro)

### For fast_reciplog optimization:
- `src/voodoo_pipeline.h` - lines 98-162

### For setup-time precomputation:
- `src/voodoo_state.h` - Add cached LOD fields to `tmu_state`
- `src/voodoo_emu.c` - `prepare_tmu()` around line 1078

## Detection Conditions

When to use fast path:

| Condition | Check | Fast Path |
|-----------|-------|-----------|
| No mipmapping | `lodmin == lodmax` | Skip LOD calc, use cached texbase/smax/tmax |
| No perspective | `!TEXMODE_ENABLE_PERSPECTIVE` | Already handled, but could pre-select specialized code |
| No LOD dither | `!TEXMODE_ENABLE_LOD_DITHER` | Skip dither addition |
| No W clamp | `!TEXMODE_CLAMP_NEG_W || ITERW >= 0` | Skip W clamp check |

## Implementation Order

1. **Add cached LOD fields** to `tmu_state`
2. **Compute cached values** in `prepare_tmu()` when `lodmin == lodmax`
3. **Add fast path branch** in TEXTURE_PIPELINE
4. **Test thoroughly** - LOD bugs cause obvious mipmap artifacts
5. **Optional:** Create fully specialized macros for zero-overhead version

## Risk Assessment
**Risk: MEDIUM**
- LOD bugs cause visible artifacts (wrong mipmap level = blurry or aliased)
- Perspective bugs cause texture warping
- Need to verify detection conditions are correct
- Multiple code paths = more testing needed

## Testing Requirements
- [ ] Mipmapped textures still render correctly
- [ ] Non-mipmapped textures look the same
- [ ] Perspective-correct textures work
- [ ] Non-perspective textures work
- [ ] No visual differences from current implementation
- [ ] Performance improvement measurable on single-mipmap textures

## Expected Impact
- **No mipmapping case:** Eliminates LOD clamping, lodmask check, lodoffset lookup per-pixel
- **Combined with no perspective:** Could eliminate most of the TEXTURE_PIPELINE setup overhead
- Estimated: 5-15% speedup for games using single-level textures

## Dependencies
- Independent of `tasks/01_Enable_Threading.md`
- Independent of `tasks/02_Preconvert_Textures.md`
- Could combine with `tasks/03_Eliminate_PerPixel_Switches.md` (specialized rasterizers)

## Notes
- `lodmin` and `lodmax` are set via `grTexLodBiasValue` and `grTexMipMapMode`
- Many older games and simple scenes use single-level textures (no mipmapping)
- LODBASE is computed once per-triangle in `prepare_tmu()` via `fast_reciplog` - this is fine
- The per-pixel `fast_reciplog` in TEXTURE_PIPELINE is the target for optimization
