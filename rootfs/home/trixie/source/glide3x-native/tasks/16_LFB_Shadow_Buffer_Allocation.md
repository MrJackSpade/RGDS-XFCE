# Task 16: LFB Shadow Buffer Allocation Optimization

## Summary
Avoid repeated malloc/free calls for the LFB shadow buffer by pre-allocating at maximum resolution during context initialization.

## Current State

**File:** `src/glide3x_lfb.c:163-177`

```c
if (bpp != 2 && type == GR_LFB_WRITE_ONLY) {
    /* Allocate or resize shadow buffer if needed */
    size_t needed_size = (size_t)stride * height;
    int need_init = 0;

    if (g_lfb_shadow_buffer_size < needed_size) {
        free(g_lfb_shadow_buffer);
        g_lfb_shadow_buffer = (uint8_t*)malloc(needed_size);
        if (!g_lfb_shadow_buffer) {
            g_lfb_shadow_buffer_size = 0;
            return FXFALSE;
        }
        g_lfb_shadow_buffer_size = needed_size;
        need_init = 1;  /* New buffer needs initialization */
    }
    // ...
}
```

## Problem Analysis

### When Shadow Buffer Is Used
The shadow buffer is allocated when:
1. LFB write mode is non-16-bit (e.g., 8888 for 32-bit)
2. Lock type is `GR_LFB_WRITE_ONLY`

This happens for games using 32-bit LFB writes, such as:
- Diablo 2 video playback (32-bit cinematics)
- Games with 32-bit UI overlays
- Full-motion video (FMV) sequences

### Issue: Allocation in Hot Path
The current code allocates/frees during `grLfbLock()`, which may be called:
- Every frame during video playback
- Multiple times per frame for UI updates
- Repeatedly during FMV sequences

Even with the "only grow" pattern (only reallocates if `needed_size > current_size`), the first allocation on each session and any resolution changes trigger malloc.

### Secondary Issue: Fragmentation
Repeated malloc/free of large buffers (e.g., 800x600x4 = 1.92MB) can cause heap fragmentation, especially on systems with limited memory.

## Proposed Solutions

### Solution 1: Pre-allocate at Max Resolution

Allocate shadow buffer during `grSstWinOpen()` at maximum expected resolution:

**File:** `src/glide3x_init.c`
```c
FxBool __stdcall grSstWinOpen(..., FxU32 res, ...) {
    // ... existing initialization ...

    /* Pre-allocate LFB shadow buffer for maximum resolution */
    /* Max supported: 1024x768 at 4 bytes/pixel = 3.145MB */
    size_t max_lfb_size = 1024 * 768 * 4;

    if (!g_lfb_shadow_buffer) {
        g_lfb_shadow_buffer = (uint8_t*)malloc(max_lfb_size);
        if (g_lfb_shadow_buffer) {
            g_lfb_shadow_buffer_size = max_lfb_size;
        }
        /* Non-fatal if allocation fails - will retry in grLfbLock */
    }

    // ...
}
```

**File:** `src/glide3x_lfb.c`
```c
/* grLfbLock now just uses pre-allocated buffer */
if (bpp != 2 && type == GR_LFB_WRITE_ONLY) {
    size_t needed_size = (size_t)stride * height;

    /* Should already be allocated, but handle edge case */
    if (g_lfb_shadow_buffer_size < needed_size) {
        /* Fallback allocation (shouldn't normally happen) */
        free(g_lfb_shadow_buffer);
        g_lfb_shadow_buffer = (uint8_t*)malloc(needed_size);
        // ...
    }

    /* Use existing buffer */
    // ...
}
```

### Solution 2: High-Water-Mark Allocator

Keep the current lazy allocation but never shrink:

```c
/* Global - never freed until context close */
static uint8_t *g_lfb_shadow_buffer = NULL;
static size_t g_lfb_shadow_buffer_capacity = 0;  /* High-water mark */

/* In grLfbLock */
if (bpp != 2 && type == GR_LFB_WRITE_ONLY) {
    size_t needed_size = (size_t)stride * height;

    if (g_lfb_shadow_buffer_capacity < needed_size) {
        /* Grow buffer (never shrink) */
        uint8_t *new_buf = (uint8_t*)realloc(g_lfb_shadow_buffer, needed_size);
        if (!new_buf) {
            return FXFALSE;
        }
        g_lfb_shadow_buffer = new_buf;
        g_lfb_shadow_buffer_capacity = needed_size;
    }

    /* g_lfb_shadow_buffer_size tracks current "logical" size for conversion */
    g_lfb_shadow_buffer_size = needed_size;
    // ...
}
```

This is essentially what the current code does, but using `realloc` instead of `free+malloc` can sometimes avoid copies if the allocator can extend in place.

### Solution 3: Static Buffer

For embedded/constrained systems, use a static buffer:

```c
/* Fixed-size static buffer - no runtime allocation */
#define MAX_LFB_SHADOW_SIZE (1024 * 768 * 4)  /* 3.145MB */
static uint8_t g_lfb_shadow_buffer_static[MAX_LFB_SHADOW_SIZE];
static uint8_t *g_lfb_shadow_buffer = g_lfb_shadow_buffer_static;
static size_t g_lfb_shadow_buffer_size = MAX_LFB_SHADOW_SIZE;
```

**Pros:** Zero allocation overhead, predictable memory usage
**Cons:** 3MB always allocated even if LFB never used; increases DLL size

### Solution 4: Resolution-Based Pre-allocation

Allocate based on actual window resolution (not max):

**File:** `src/glide3x_init.c`
```c
FxBool __stdcall grSstWinOpen(..., FxU32 res, ...) {
    // ... get width/height from res ...

    /* Pre-allocate shadow buffer for this resolution at max bpp (4) */
    size_t shadow_size = width * height * 4;

    g_lfb_shadow_buffer = (uint8_t*)malloc(shadow_size);
    g_lfb_shadow_buffer_size = shadow_size;

    // ...
}
```

## Recommended Implementation

**Solution 1** (pre-allocate at max resolution) is recommended because:
- Simple to implement
- One-time allocation at context creation
- No reallocation during gameplay
- 3MB is acceptable for desktop/SBC targets

For memory-constrained systems, consider **Solution 4** (resolution-based) to avoid over-allocation.

## Files to Modify

| File | Changes |
|------|---------|
| `src/glide3x_init.c` | Pre-allocate shadow buffer in `grSstWinOpen()` |
| `src/glide3x_init.c` | Free shadow buffer in `grSstWinClose()` |
| `src/glide3x_lfb.c` | Simplify `grLfbLock()` to use pre-allocated buffer |
| `src/glide3x_state.h` | Possibly move shadow buffer globals if needed |

## Cleanup Required

Ensure shadow buffer is freed on context close:

**File:** `src/glide3x_init.c`
```c
FxBool __stdcall grSstWinClose(void) {
    // ... existing cleanup ...

    /* Free LFB shadow buffer */
    if (g_lfb_shadow_buffer) {
        free(g_lfb_shadow_buffer);
        g_lfb_shadow_buffer = NULL;
        g_lfb_shadow_buffer_size = 0;
    }

    // ...
}
```

## Risk Assessment
**Risk: LOW**
- Shadow buffer is isolated functionality
- Only affects non-16-bit LFB modes
- Easy to test with Diablo 2 cinematics
- Fallback to current behavior if pre-allocation fails

## Testing Requirements
- [ ] Diablo 2 video playback works correctly
- [ ] No crashes on context open/close
- [ ] No memory leaks (shadow buffer freed on close)
- [ ] Handles resolution changes gracefully
- [ ] Works if pre-allocation fails (fallback path)

## Expected Impact

| Scenario | Before | After |
|----------|--------|-------|
| First LFB lock | malloc | No-op (pre-allocated) |
| Subsequent locks | No-op | No-op |
| Video frame | ~0 (already allocated) | ~0 |
| Memory fragmentation | Possible | Reduced |

**Overall Impact:** LOW to MODERATE

This optimization only affects games using non-16-bit LFB writes. For those games (like Diablo 2), it eliminates potential allocation overhead during video playback. For games not using this feature, there's no impact (except slightly higher memory usage from pre-allocation).

## Dependencies
- Independent of other tasks
- No prerequisites
- Low priority - only implement if LFB-heavy games show performance issues

## Notes
- The current code already implements a "grow-only" pattern, so the worst case (repeated allocations) shouldn't happen in practice
- The main benefit is eliminating the initial allocation during gameplay and reducing fragmentation
- Consider making pre-allocation optional via environment variable if memory is tight
- The shadow buffer exists to fix the "ZARDBLIZ" bug in Diablo 2 where 32-bit writes with 16-bit stride caused corruption
