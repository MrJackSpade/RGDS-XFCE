# Task 03f: Eliminate Per-Pixel Depth Function Switch

## Summary
Replace the per-pixel depth comparison function switch with a function pointer or loop inversion pattern.

## Current State

**File:** `src/voodoo_pipeline.h` lines 798-853

Inside `PIXEL_PIPELINE_BEGIN` macro:

```c
switch (FBZMODE_DEPTH_FUNCTION(r_fbzMode)) {
    case 0: depthTest = (newDepth <  destDepth); break;  /* NEVER? or LT */
    case 1: depthTest = (newDepth <  destDepth); break;  /* LESS */
    case 2: depthTest = (newDepth == destDepth); break;  /* EQUAL */
    case 3: depthTest = (newDepth <= destDepth); break;  /* LEQUAL */
    case 4: depthTest = (newDepth >  destDepth); break;  /* GREATER */
    case 5: depthTest = (newDepth != destDepth); break;  /* NOTEQUAL */
    case 6: depthTest = (newDepth >= destDepth); break;  /* GEQUAL */
    case 7: depthTest = 1; break;                         /* ALWAYS */
}
```

This switch runs once per pixel to determine depth test comparison.

## Problem
- 8 cases, runs ~480,000 times per frame
- Depth testing is almost always enabled
- Most games use LEQUAL (case 3) or LESS (case 1) exclusively
- Depth function doesn't change during draw calls

## Proposed Solution

### Option A: Function Pointers (Recommended)
```c
typedef int (*depth_cmp_fn)(uint16_t newDepth, uint16_t destDepth);
depth_cmp_fn cached_depth_cmp;

// Individual comparison functions
static inline int depth_never(uint16_t newDepth, uint16_t destDepth) { return 0; }
static inline int depth_less(uint16_t newDepth, uint16_t destDepth) { return newDepth < destDepth; }
static inline int depth_equal(uint16_t newDepth, uint16_t destDepth) { return newDepth == destDepth; }
static inline int depth_lequal(uint16_t newDepth, uint16_t destDepth) { return newDepth <= destDepth; }
static inline int depth_greater(uint16_t newDepth, uint16_t destDepth) { return newDepth > destDepth; }
static inline int depth_notequal(uint16_t newDepth, uint16_t destDepth) { return newDepth != destDepth; }
static inline int depth_gequal(uint16_t newDepth, uint16_t destDepth) { return newDepth >= destDepth; }
static inline int depth_always(uint16_t newDepth, uint16_t destDepth) { return 1; }

// Set at state change time
void update_depth_function(uint32_t fbzMode) {
    static const depth_cmp_fn depth_funcs[8] = {
        depth_never, depth_less, depth_equal, depth_lequal,
        depth_greater, depth_notequal, depth_gequal, depth_always
    };
    cached_depth_cmp = depth_funcs[FBZMODE_DEPTH_FUNCTION(fbzMode)];
}

// Pixel loop - no switch
depthTest = cached_depth_cmp(newDepth, destDepth);
```

### Option B: Fast Path for Common Cases
```c
// Most games use LEQUAL
if (cached_depth_func == DEPTH_LEQUAL) {
    depthTest = (newDepth <= destDepth);
} else if (cached_depth_func == DEPTH_LESS) {
    depthTest = (newDepth < destDepth);
} else {
    // Fallback to function pointer or switch
    depthTest = cached_depth_cmp(newDepth, destDepth);
}
```

### Option C: Depth Test Disable Fast Path
```c
// If depth test disabled, skip entire depth section
if (!FBZMODE_ENABLE_DEPTH(r_fbzMode)) {
    // No depth test, no depth write check needed
} else {
    depthTest = cached_depth_cmp(newDepth, destDepth);
    if (!depthTest) continue;  // Skip pixel
}
```

## Files to Modify
- `src/voodoo_pipeline.h` - lines 798-853 (PIXEL_PIPELINE_BEGIN macro)
- `src/voodoo_state.h` - Add function pointer field
- `src/glide3x_depth.c` or equivalent - Update depth state functions

## Related State Changes
- `grDepthBufferFunction()` sets the depth comparison
- `grDepthBufferMode()` enables/disables depth testing

## Testing Requirements
- [ ] All depth functions work:
  - [ ] GR_CMP_NEVER
  - [ ] GR_CMP_LESS
  - [ ] GR_CMP_EQUAL
  - [ ] GR_CMP_LEQUAL
  - [ ] GR_CMP_GREATER
  - [ ] GR_CMP_NOTEQUAL
  - [ ] GR_CMP_GEQUAL
  - [ ] GR_CMP_ALWAYS
- [ ] Depth buffer writes work correctly
- [ ] Z-fighting behavior unchanged
- [ ] No visual differences from current implementation

## Dependencies
- Parent task: `tasks/03_Eliminate_PerPixel_Switches.md`
- Recommended as FIRST switch to optimize (per parent task suggestion)
- Only 8 cases, always used, good starting point

## Expected Impact
- HIGH IMPACT - depth test is almost always enabled
- Eliminates ~480,000 branch decisions per frame
- Simple optimization with low risk
- Good proof of concept for other switches
