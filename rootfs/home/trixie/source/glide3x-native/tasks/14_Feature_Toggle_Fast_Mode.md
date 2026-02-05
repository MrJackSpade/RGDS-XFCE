# Task 14: Feature Toggle / Fast Mode

## Summary
Add configurable quality settings to allow disabling expensive rendering features for performance. A "fast mode" could significantly speed up rendering on low-power devices.

## Current State

All rendering features are always enabled. There's no way to trade visual quality for performance.

## Feature Toggle Candidates

| Feature | Location | Impact | Default |
|---------|----------|--------|---------|
| Bilinear filtering | `TEXTURE_PIPELINE` | 4x texture fetches vs 1x for point sampling | ON |
| Fog | `APPLY_FOGGING` | Per-pixel switch + blend calculations | ON |
| Alpha blending | `APPLY_ALPHA_BLEND` | Framebuffer read + 2 switch statements | ON |
| Dithering | `APPLY_DITHER` | Table lookup per pixel | ON |
| Stippling | `PIXEL_PIPELINE_BEGIN` | Pattern check per pixel | ON |
| Mipmapping | `TEXTURE_PIPELINE` | LOD calculation per pixel | ON |

Games often don't use all features simultaneously. Disabling unused or less-important features can provide significant speedup.

## Proposed Implementation

### 1. Add Feature Flags to State

**File:** `src/voodoo_state.h`

```c
/* Feature toggle flags */
typedef struct {
    bool disable_bilinear;    /* Force point sampling */
    bool disable_fog;         /* Skip fog calculations */
    bool disable_dither;      /* Skip dithering */
    bool disable_mipmaps;     /* Always use LOD 0 */
    bool disable_stipple;     /* Skip stipple pattern check */
    /* Alpha blending can't be disabled - would break transparency */
} feature_flags_t;

/* In voodoo_state or triangle_worker */
feature_flags_t features;
```

**Note:** `disable_bilinear` already exists as `triangle_worker.disable_bilinear_filter` (voodoo_state.h:243).

### 2. Check Flags in Rendering Pipeline

**File:** `src/voodoo_pipeline.h`

```c
/* In TEXTURE_PIPELINE macro */
#define TEXTURE_PIPELINE(...) do { \
    if (v->features.disable_mipmaps) { \
        /* Skip LOD calculation, use base level */ \
        lod = t->lodmin; \
    } else { \
        /* Normal LOD calculation */ \
    } \
    \
    if (v->features.disable_bilinear || !bilinear_enabled) { \
        /* Point sampling - single fetch */ \
        texel = fetch_texel(t, s >> 18, t >> 18, lod); \
    } else { \
        /* Bilinear - 4 fetches + blend */ \
    } \
} while(0)
```

**File:** `src/voodoo_emu.c`

```c
/* In pixel loop, before APPLY_FOGGING */
if (!v->features.disable_fog && FOGMODE_ENABLE_FOG(fogMode)) {
    APPLY_FOGGING(...);
}

/* Before APPLY_DITHER */
if (!v->features.disable_dither) {
    APPLY_DITHER(...);
}
```

### 3. Expose via Glide API Extension or Environment Variable

**Option A: Environment Variables**
```c
/* In glide3x_init.c - grSstWinOpen() */
if (getenv("GLIDE_FAST_MODE")) {
    v->features.disable_bilinear = true;
    v->features.disable_fog = true;
    v->features.disable_dither = true;
    v->features.disable_mipmaps = true;
}

/* Or individual toggles */
if (getenv("GLIDE_NO_BILINEAR")) v->features.disable_bilinear = true;
if (getenv("GLIDE_NO_FOG")) v->features.disable_fog = true;
```

**Option B: Config File**
```ini
# glide3x.conf
[performance]
disable_bilinear = true
disable_fog = true
disable_dither = true
disable_mipmaps = true
```

**Option C: Glide Extension Function**
```c
/* Non-standard extension */
void __stdcall grFastMode(FxBool enable);
void __stdcall grDisableFeature(GrFeature_t feature);
```

**Recommendation:** Environment variables are simplest and don't require game modification.

## Preset Modes

### Fast Mode (Maximum Performance)
```
disable_bilinear = true   (4x fewer texture fetches)
disable_fog = true        (skip fog blend)
disable_dither = true     (skip dither lookup)
disable_mipmaps = true    (skip LOD calc)
```

### Balanced Mode (Some Quality)
```
disable_bilinear = false  (keep filtering)
disable_fog = true        (fog often subtle)
disable_dither = true     (dither less visible at higher res)
disable_mipmaps = false   (keep mipmaps for quality)
```

### Quality Mode (Default)
```
All features enabled
```

## Per-Feature Impact Analysis

### Bilinear Filtering
- **Cost:** 4 texture fetches + 3 lerps per pixel
- **Savings:** 75% reduction in texture bandwidth
- **Visual Impact:** Blocky textures, very noticeable
- **Games affected:** All textured games

### Fog
- **Cost:** Per-pixel fog factor calculation + color blend
- **Savings:** ~5-10% of pixel pipeline
- **Visual Impact:** No distance fade, less atmosphere
- **Games affected:** Games using fog for atmosphere/distance

### Dithering
- **Cost:** Table lookup + pattern application
- **Savings:** Small (~2-3%)
- **Visual Impact:** Slight banding in gradients (often unnoticeable)
- **Games affected:** Games with smooth gradients

### Mipmapping
- **Cost:** LOD calculation (fast_reciplog + math)
- **Savings:** ~5-10% of texture pipeline
- **Visual Impact:** Aliasing on distant textures, shimmer
- **Games affected:** Games with distant textured surfaces

### Stippling
- **Cost:** Pattern check per pixel
- **Savings:** Small (~1-2%)
- **Visual Impact:** Screen-door transparency won't work
- **Games affected:** Games using stipple for transparency (rare)

## Files to Modify

| File | Changes |
|------|---------|
| `src/voodoo_state.h` | Add `feature_flags_t` structure |
| `src/glide3x_init.c` | Parse environment variables, initialize flags |
| `src/voodoo_pipeline.h` | Add flag checks in `TEXTURE_PIPELINE` |
| `src/voodoo_emu.c` | Add flag checks for fog, dither, stipple |

## Risk Assessment
**Risk: LOW**
- Feature checks are simple conditionals
- Default behavior unchanged (all enabled)
- Easy to test each toggle independently
- No architectural changes required

## Testing Requirements
- [ ] Each feature can be toggled independently
- [ ] Default mode renders identically to current
- [ ] Fast mode provides measurable speedup
- [ ] No crashes when features disabled
- [ ] Visual differences are as expected (documented)

## Expected Impact

| Mode | Estimated Speedup | Visual Quality |
|------|-------------------|----------------|
| Fast Mode | 20-40% | Reduced (blocky textures, no fog) |
| Balanced Mode | 10-15% | Slightly reduced |
| Quality Mode | 0% (baseline) | Full |

**Note:** Actual impact depends on scene content. Fog-heavy scenes benefit most from disabling fog. Texture-heavy scenes benefit most from point sampling.

## Dependencies
- Independent of other tasks
- Could combine with Task 03/05 (specialized render functions could include feature-disabled variants)
- Simple to implement, good "quick win"

## Integration with Loop Inversion (Tasks 03/05)

If loop inversion is implemented, feature flags could select entire specialized functions:

```c
switch (feature_mode) {
    case FAST_MODE:
        render_triangle_fast(v, ...);  /* No bilinear, no fog, no dither */
        break;
    case BALANCED_MODE:
        render_triangle_balanced(v, ...);
        break;
    case QUALITY_MODE:
        render_triangle_quality(v, ...);
        break;
}
```

This avoids per-pixel flag checks entirely.

## Notes
- The existing `disable_bilinear_filter` flag in `triangle_worker` shows this pattern is already partially implemented
- Consider adding runtime toggle (hotkey?) for quick A/B comparison
- Could add FPS counter overlay to show impact of settings
- Some games may break if features they depend on are disabled (e.g., fog used for gameplay visibility)
