# Task 12: Extend Dirty Flag System

## Summary
Extend the existing dirty flag system to track more state changes, enabling caching of computed values that currently get recomputed unnecessarily.

## Current State

### Existing Dirty Flags
**Files:** `src/voodoo_emu.c:1037-1051`, `src/voodoo_state.h:62,80`

Two dirty flags currently exist:

#### 1. `tmu_state.regdirty` (voodoo_state.h:80)
Tracks TMU register changes. Set when:
- `grTexSource()` changes texture parameters (glide3x_texture.c:583,646,690,739)
- `grTexFilterMode()` changes filter settings (glide3x_texture.c:1006)
- TMU registers written directly (voodoo_emu.c:2656,2661)

Cleared when: `recompute_texture_params()` is called (voodoo_emu.c:1173)

**What it enables:** Skip `recompute_texture_params()` if nothing changed.

#### 2. `ncc_table.dirty` (voodoo_state.h:62)
Tracks NCC color conversion table changes. Set when:
- TMU initialized (voodoo_emu.c:501)

Cleared when: `ncc_table_update()` is called

**What it enables:** Skip NCC table regeneration if unchanged.

### Where Dirty Flags Are Checked
**File:** `src/voodoo_emu.c:1031-1051`

```c
static void prepare_tmu(tmu_state* t)
{
    /* if the texture parameters are dirty, update them */
    if (t->regdirty)
    {
        recompute_texture_params(t);

        /* ensure that the NCC tables are up to date */
        if ((TEXMODE_FORMAT(t->reg[textureMode].u) & 7) == 1)
        {
            ncc_table* n = &t->ncc[TEXMODE_NCC_TABLE_SELECT(t->reg[textureMode].u)];
            t->texel[1] = t->texel[9] = n->texel;
            if (n->dirty) {
                ncc_table_update(n);
            }
        }
    }
    // ... continues with LOD calculation
}
```

`prepare_tmu()` is called per-triangle (lines 1529-1531).

## What's Missing

### 1. Texture DATA Dirty Flag
**Problem:** Texture *data* (the actual texel bytes) has no dirty tracking.

When `grTexDownloadMipMap*()` uploads new texture data, there's no flag to indicate the pre-converted cache (if we add one per Task 02) needs invalidation.

**Needed for:** Task 02 (Pre-convert Textures)

### 2. Combine State Dirty Flag
**Problem:** `grTexCombine` and `grColorCombine` parameters are re-evaluated in switch statements per pixel.

Even though the combine mode rarely changes mid-frame, the renderer checks every pixel.

**Needed for:** Task 03 (Eliminate Per-Pixel Switches)

### 3. Blend State Dirty Flag
**Problem:** `grAlphaBlendFunction` parameters checked per-pixel in switches.

**Needed for:** Task 05 (Blend Mode Fast Paths)

### 4. Depth/Alpha Test State Dirty Flag
**Problem:** Depth function, alpha test function checked per-pixel.

**Needed for:** Task 03 (Eliminate Per-Pixel Switches)

## Proposed Solution

### Add New Dirty Flags to voodoo_state.h

```c
typedef struct {
    // ... existing fields ...

    /* Extended dirty flags */
    bool combine_dirty;      /* grTexCombine/grColorCombine changed */
    bool blend_dirty;        /* grAlphaBlendFunction changed */
    bool depth_dirty;        /* grDepthBufferFunction changed */
    bool alpha_test_dirty;   /* grAlphaTestFunction changed */
    bool fog_dirty;          /* grFogMode changed */

    /* Cached specialized function pointers (set when dirty cleared) */
    void (*cached_pixel_func)(/*...*/);  /* Specialized pixel pipeline */
    int cached_blend_mode;               /* Enum for blend fast-path */
} fbi_state;

typedef struct {
    // ... existing tmu_state fields ...

    /* Texture data dirty tracking */
    uint32_t data_dirty_start;  /* First modified address (or UINT32_MAX if clean) */
    uint32_t data_dirty_end;    /* Last modified address */
} tmu_state;
```

### Set Dirty Flags on State Change

```c
// In glide3x_state.c - grTexCombine()
void __stdcall grTexCombine(GrChipID_t tmu, ...) {
    // ... existing code ...
    g_voodoo->fbi.combine_dirty = true;
}

// In glide3x_state.c - grAlphaBlendFunction()
void __stdcall grAlphaBlendFunction(...) {
    // ... existing code ...
    g_voodoo->fbi.blend_dirty = true;
}

// In glide3x_texture.c - grTexDownloadMipMapLevel()
void __stdcall grTexDownloadMipMapLevel(...) {
    // ... existing memcpy ...

    // Track dirty data range
    tmu_state *ts = &g_voodoo->tmu[tmu];
    uint32_t start_addr = ...;
    uint32_t end_addr = start_addr + size;

    if (ts->data_dirty_start > start_addr)
        ts->data_dirty_start = start_addr;
    if (ts->data_dirty_end < end_addr)
        ts->data_dirty_end = end_addr;
}
```

### Check and Clear on Use

```c
// In voodoo_emu.c - before rendering
static void prepare_render_state(voodoo_state *v) {
    /* Check if we need to recompute specialized functions */
    if (v->fbi.combine_dirty || v->fbi.blend_dirty ||
        v->fbi.depth_dirty || v->fbi.alpha_test_dirty) {

        /* Select optimized pixel pipeline based on current state */
        v->fbi.cached_pixel_func = select_pixel_pipeline(v);
        v->fbi.cached_blend_mode = compute_blend_mode_enum(v);

        v->fbi.combine_dirty = false;
        v->fbi.blend_dirty = false;
        v->fbi.depth_dirty = false;
        v->fbi.alpha_test_dirty = false;
    }
}

// For texture data, check at texture use time
static void prepare_tmu(tmu_state* t) {
    // ... existing regdirty check ...

    /* Check if texture data needs reconversion */
    if (t->data_dirty_start != UINT32_MAX) {
        invalidate_texture_cache(t, t->data_dirty_start, t->data_dirty_end);
        t->data_dirty_start = UINT32_MAX;
        t->data_dirty_end = 0;
    }
}
```

## Files to Modify

| File | Changes |
|------|---------|
| `src/voodoo_state.h` | Add new dirty flag fields to `fbi_state` and `tmu_state` |
| `src/glide3x_state.c` | Set dirty flags in `grTexCombine`, `grColorCombine`, `grAlphaBlendFunction`, `grDepthBufferFunction`, etc. |
| `src/glide3x_texture.c` | Track texture data dirty range in `grTexDownloadMipMap*` |
| `src/voodoo_emu.c` | Check dirty flags before rendering, select specialized code paths |
| `src/glide3x_init.c` | Initialize dirty flags on context creation |

## State Change Functions to Instrument

| Function | Dirty Flag to Set |
|----------|-------------------|
| `grTexCombine()` | `combine_dirty` |
| `grColorCombine()` | `combine_dirty` |
| `grAlphaCombine()` | `combine_dirty` |
| `grAlphaBlendFunction()` | `blend_dirty` |
| `grDepthBufferFunction()` | `depth_dirty` |
| `grDepthBufferMode()` | `depth_dirty` |
| `grAlphaTestFunction()` | `alpha_test_dirty` |
| `grAlphaTestReferenceValue()` | `alpha_test_dirty` |
| `grFogMode()` | `fog_dirty` |
| `grTexDownloadMipMapLevel()` | `data_dirty_start/end` |
| `grTexDownloadMipMapLevelPartial()` | `data_dirty_start/end` |

## Risk Assessment
**Risk: LOW-MEDIUM**
- Adding flags is non-invasive
- Must ensure ALL state-change paths set the flag (missing one = stale cache = rendering bugs)
- Testing: visual diff against non-cached path

## Testing Requirements
- [ ] Every state-change function sets appropriate dirty flag
- [ ] Dirty flags properly cleared after cache update
- [ ] No visual differences from non-cached rendering
- [ ] State changes mid-frame correctly invalidate cache
- [ ] Texture uploads correctly invalidate data cache

## Expected Impact
- **Alone:** Minimal - flags are just infrastructure
- **With Task 02:** Enables texture cache invalidation
- **With Task 03/05:** Enables specialized render function selection
- **Combined:** Foundation for 10-30% speedup from other tasks

## Dependencies
- **Prerequisite for:** Task 02, Task 03, Task 05
- **Independent of:** Tasks 01, 04, 06-11

This is infrastructure work that enables other optimizations. Implement alongside or before the tasks that depend on it.

## Notes
- The palette caching (`ncc_table.texel[256]`) is a good example of the pattern - dirty flag triggers recomputation of lookup table
- Consider whether dirty flags should be per-TMU or global
- Texture data tracking could use a simpler "any data changed" bool if range tracking is too complex
