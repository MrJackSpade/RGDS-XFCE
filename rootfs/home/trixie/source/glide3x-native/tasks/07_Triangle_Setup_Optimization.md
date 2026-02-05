# Task 07: Triangle Setup Optimization

## Summary
Eliminate redundant calculations in per-triangle setup by computing shared values once and skipping unused TMU setup.

## Current State

**File:** `src/glide3x_draw.c:261-409`

### Redundant Area Calculation
`compute_gradients()` is called 12 times per triangle, and **each call recalculates the same area and inverse area**:

```c
static void compute_gradients(...) {
    float area = (bx - ax) * (cy - ay) - (cx - ax) * (by - ay);  // Same every time!
    float inv_area = 1.0f / area;  // Division repeated 12 times!
    ...
}
```

### Call Sites (12 total)
| Line | Purpose | Redundant? |
|------|---------|------------|
| 331 | Red gradient | Area recalculated |
| 332 | Green gradient | Area recalculated |
| 333 | Blue gradient | Area recalculated |
| 334 | Alpha gradient | Area recalculated |
| 354 | Z gradient | Area recalculated |
| 362 | W gradient | Area recalculated |
| 374 | TMU0 S gradient | Area recalculated |
| 375 | TMU0 T gradient | Area recalculated |
| 376 | TMU0 W gradient | **W computed again (same as line 362)** |
| 392 | TMU1 S gradient | Area recalculated |
| 393 | TMU1 T gradient | Area recalculated |
| 394 | TMU1 W gradient | **W computed 3rd time (same as 362, 376)** |

### W Gradient Computed 3 Times Identically
```c
// Line 362 - FBI W
compute_gradients(ax, ay, bx, by, cx, cy, va.oow, vb.oow, vc.oow, &dwdx_f, &dwdy_f);

// Line 376 - TMU0 W (IDENTICAL to above!)
compute_gradients(ax, ay, bx, by, cx, cy, va.oow, vb.oow, vc.oow, &dw0dx, &dw0dy);

// Line 394 - TMU1 W (IDENTICAL to above!)
compute_gradients(ax, ay, bx, by, cx, cy, va.oow, vb.oow, vc.oow, &dw1dx, &dw1dy);
```

### TMU1 Always Computed
Lines 392-404 compute TMU1 gradients unconditionally, even though many games only use TMU0. There's no check for whether TMU1 is actually enabled.

## Proposed Solution

### Step 1: Compute inv_area once, pass to gradient function

```c
// Compute area and inverse ONCE
float area = (bx - ax) * (cy - ay) - (cx - ax) * (by - ay);
if (area == 0.0f) {
    return;  // Degenerate triangle - skip entirely
}
float inv_area = 1.0f / area;

// Modified gradient function - no division
static inline void compute_gradient_fast(
    float ax, float ay, float bx, float by, float cx, float cy,
    float va, float vb, float vc,
    float inv_area,
    float *dpdx, float *dpdy)
{
    *dpdx = ((vb - va) * (cy - ay) - (vc - va) * (by - ay)) * inv_area;
    *dpdy = ((vc - va) * (bx - ax) - (vb - va) * (cx - ax)) * inv_area;
}
```

**Impact:** Eliminates 11 redundant divisions per triangle.

### Step 2: Compute W gradient once, reuse for TMU0/TMU1

```c
// Compute W gradient once
float dwdx, dwdy;
compute_gradient_fast(..., va.oow, vb.oow, vc.oow, inv_area, &dwdx, &dwdy);

// Reuse for FBI, TMU0, TMU1
fbi->dwdx = (int64_t)(dwdx * 4294967296.0);
fbi->dwdy = (int64_t)(dwdy * 4294967296.0);
tmu0->dwdx = fbi->dwdx;  // Same value!
tmu0->dwdy = fbi->dwdy;
tmu1->dwdx = fbi->dwdx;
tmu1->dwdy = fbi->dwdy;
```

**Impact:** Eliminates 2 redundant gradient calculations (6 multiplications, 4 subtractions each).

### Step 3: Skip TMU1 setup when not used

```c
if (FBZCP_TEXTURE_ENABLE(g_voodoo->reg[fbzColorPath].u)) {
    // TMU0 setup...

    // Only compute TMU1 if actually enabled
    if (g_voodoo->tmu[1].enabled) {  // Or check appropriate state flag
        // TMU1 setup...
    }
}
```

**Impact:** Eliminates 3 gradient calculations when TMU1 unused (common case).

### Step 4: Pre-compute edge deltas

The edge deltas `(cy - ay)`, `(by - ay)`, `(bx - ax)`, `(cx - ax)` are used in every gradient calculation but never change:

```c
// Compute once
float dy_ca = cy - ay;
float dy_ba = by - ay;
float dx_ba = bx - ax;
float dx_ca = cx - ax;

// Use in gradient calc
*dpdx = ((vb - va) * dy_ca - (vc - va) * dy_ba) * inv_area;
*dpdy = ((vc - va) * dx_ba - (vb - va) * dx_ca) * inv_area;
```

**Impact:** Eliminates 4 subtractions × 12 calls = 48 subtractions per triangle.

## Optimized Implementation

```c
void __stdcall grDrawTriangle(const GrVertex *a, const GrVertex *b, const GrVertex *c)
{
    // ... validation, vertex unpacking ...

    // Compute triangle edge deltas ONCE
    float dx_ba = bx - ax, dy_ba = by - ay;
    float dx_ca = cx - ax, dy_ca = cy - ay;

    // Compute area and check for degenerate
    float area = dx_ba * dy_ca - dx_ca * dy_ba;
    if (area == 0.0f) return;
    float inv_area = 1.0f / area;

    // Inline macro for gradient calculation (no function call overhead)
    #define GRADIENT(va, vb, vc, dpdx, dpdy) do { \
        float dv_ba = (vb) - (va); \
        float dv_ca = (vc) - (va); \
        *(dpdx) = (dv_ba * dy_ca - dv_ca * dy_ba) * inv_area; \
        *(dpdy) = (dv_ca * dx_ba - dv_ba * dx_ca) * inv_area; \
    } while(0)

    // Color gradients
    float drdx, drdy, dgdx, dgdy, dbdx, dbdy, dadx, dady;
    GRADIENT(va.r, vb.r, vc.r, &drdx, &drdy);
    GRADIENT(va.g, vb.g, vc.g, &dgdx, &dgdy);
    GRADIENT(va.b, vb.b, vc.b, &dbdx, &dbdy);
    GRADIENT(va.a, vb.a, vc.a, &dadx, &dady);

    // Z gradient
    float dzdx, dzdy;
    GRADIENT(va.ooz, vb.ooz, vc.ooz, &dzdx, &dzdy);

    // W gradient (compute ONCE, reuse)
    float dwdx, dwdy;
    GRADIENT(va.oow, vb.oow, vc.oow, &dwdx, &dwdy);

    // ... set fbi values ...

    if (FBZCP_TEXTURE_ENABLE(...)) {
        // TMU0
        float ds0dx, ds0dy, dt0dx, dt0dy;
        GRADIENT(va.sow0, vb.sow0, vc.sow0, &ds0dx, &ds0dy);
        GRADIENT(va.tow0, vb.tow0, vc.tow0, &dt0dx, &dt0dy);
        // W reused from above

        // TMU1 - only if enabled
        if (tmu1_enabled) {
            float ds1dx, ds1dy, dt1dx, dt1dy;
            GRADIENT(va.sow1, vb.sow1, vc.sow1, &ds1dx, &ds1dy);
            GRADIENT(va.tow1, vb.tow1, vc.tow1, &dt1dx, &dt1dy);
            // W reused from above
        }
    }

    #undef GRADIENT
}
```

## Files to Modify

- `src/glide3x_draw.c` - `grDrawTriangle()` function (lines 261-409)
- `src/glide3x_draw.c` - `compute_gradients()` function (lines 221-238) - may be removed or simplified

## Risk Assessment
**Risk: LOW**
- Math is straightforward - same calculations, just reorganized
- Easy to verify correctness (gradients should be identical)
- No changes to pixel loop or state management

## Testing Requirements
- [ ] Triangles render identically to before
- [ ] No visual artifacts from gradient changes
- [ ] TMU1-only games still work (if any exist)
- [ ] Performance improvement measurable

## Expected Impact
- Eliminates 11 divisions per triangle (expensive operation)
- Eliminates 2 redundant W gradient calculations
- Eliminates 3 gradient calculations when TMU1 unused
- Eliminates ~48 subtractions per triangle
- With 5000 triangles/frame: **55,000 fewer divisions per frame**
- Estimated: 5-10% speedup for triangle-heavy scenes

## Dependencies
- Independent of all other tasks
- Low risk, can be done early

## Notes
- The culling check at lines 311-319 also computes area - could share with gradient setup
- `grDrawVertexArrayContiguous` just calls `grDrawTriangle` in a loop - improvements apply automatically
- Consider batching multiple triangles' setup if async pipeline (Task 06) is implemented
