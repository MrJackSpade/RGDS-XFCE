/*
 * glide3x_draw.c - Primitive drawing functions
 *
 * This module implements the core drawing operations:
 *   - grDrawTriangle(): Draw a single triangle
 *   - grDrawVertexArray(): Draw primitives from vertex pointer array
 *   - grDrawVertexArrayContiguous(): Draw from contiguous vertex buffer
 *   - grDrawPoint(): Draw a single point
 *   - grDrawLine(): Draw a line segment
 *   - grAADrawTriangle(): Draw anti-aliased triangle (stub)
 *
 * TRIANGLE RENDERING:
 * The triangle is the fundamental primitive in 3D graphics. All visible
 * geometry is ultimately composed of triangles. Even "quads" are two
 * triangles sharing an edge.
 *
 * Voodoo hardware was specifically optimized for triangle rendering:
 *   - Dedicated triangle setup logic
 *   - Parallel pixel processing
 *   - Pipelined texture mapping
 *   - High fill rate (millions of pixels/second)
 *
 * THE RENDERING PIPELINE:
 *
 * 1. VERTEX INPUT:
 *    Application provides three vertices with screen-space coordinates,
 *    colors, texture coordinates, and depth values.
 *
 * 2. TRIANGLE SETUP:
 *    - Compute edge equations (for rasterization bounds)
 *    - Compute parameter gradients (dP/dx, dP/dy) for interpolation
 *    - Determine winding (for culling)
 *
 * 3. RASTERIZATION:
 *    Convert triangle to pixels by scanning each row:
 *    - For each Y from top to bottom of triangle
 *    - For each X from left edge to right edge
 *    - Generate a pixel with interpolated parameters
 *
 * 4. PIXEL PIPELINE (per pixel):
 *    a. Texture lookup (sample texture at interpolated coordinates)
 *    b. Color combine (mix texture, vertex color, constant color)
 *    c. Alpha combine (compute final alpha)
 *    d. Fog application (blend toward fog color based on depth)
 *    e. Alpha test (discard if alpha comparison fails)
 *    f. Depth test (discard if depth comparison fails)
 *    g. Alpha blend (mix with existing framebuffer color)
 *    h. Dithering (reduce color banding on 16-bit output)
 *    i. Write to framebuffer and depth buffer
 *
 * VERTEX COORDINATE SYSTEM:
 * Glide operates in window coordinates (post-projection). The application
 * is responsible for transforming vertices from world space through the
 * view and projection matrices. Glide receives:
 *
 *   x, y   - Screen position in pixels (floating point)
 *            Origin is either upper-left or lower-left (grSstOrigin)
 *
 *   oow    - "One Over W" (1/W) for perspective correction
 *            W is the clip coordinate from projection
 *            For orthographic projection, oow = 1.0
 *            For perspective, oow = 1/distance (roughly)
 *
 *   ooz    - Z value for depth buffering (or 1/Z for W-buffering)
 *            Range depends on projection matrix
 *            Typically: 0 = near plane, 65535 = far plane
 *
 *   r,g,b,a - Vertex color (0-255 range)
 *             Interpolated across triangle for Gouraud shading
 *             Often carries pre-computed lighting
 *
 *   sow, tow - Texture coordinates divided by W (S/W, T/W)
 *              Perspective-divided for correct texture mapping
 *              Range 0-255 for 256x256 textures (varies)
 *
 * PERSPECTIVE CORRECTION:
 * For correct texture mapping, coordinates must be interpolated in a
 * perspective-correct manner. The trick is to interpolate (S/W, T/W, 1/W)
 * linearly across the triangle, then recover (S, T) by dividing:
 *   S = (S/W) / (1/W)
 *   T = (T/W) / (1/W)
 *
 * This is why vertices include oow (1/W) and perspective-divided coords.
 *
 * PRIMITIVE TYPES:
 *
 * TRIANGLES: Every 3 vertices form an independent triangle
 *   v0,v1,v2 -> tri0; v3,v4,v5 -> tri1; ...
 *
 * TRIANGLE_STRIP: Each vertex after first two forms a new triangle
 *   v0,v1,v2 -> tri0; v1,v2,v3 -> tri1 (flipped); v2,v3,v4 -> tri2; ...
 *   Winding alternates to maintain consistent facing.
 *   Efficient: N triangles need only N+2 vertices.
 *
 * TRIANGLE_FAN: First vertex shared by all triangles
 *   v0,v1,v2 -> tri0; v0,v2,v3 -> tri1; v0,v3,v4 -> tri2; ...
 *   Good for convex polygons centered on v0.
 *
 * CULLING:
 * Back-face culling discards triangles facing away from the camera.
 * Winding (clockwise vs counter-clockwise) determines facing.
 * Computed from signed area of triangle in screen space:
 *   area = (b.x-a.x)*(c.y-a.y) - (c.x-a.x)*(b.y-a.y)
 *   Positive area = CCW, Negative = CW
 */

#include "glide3x_state.h"
#include <math.h>

/*
 * compute_gradients - Compute parameter gradients for interpolation
 *
 * For perspective-correct interpolation, we need to know how each
 * parameter changes per-pixel in X and Y. Given a triangle ABC with
 * parameter values Va, Vb, Vc, we solve for dV/dx and dV/dy.
 *
 * The math uses Cramer's rule on the linear system defined by the
 * three vertices. The signed area of the triangle normalizes the result.
 *
 * Parameters:
 *   ax,ay,bx,by,cx,cy - Triangle vertex positions
 *   va, vb, vc        - Parameter values at each vertex
 *   dpdx, dpdy        - Output: gradient in X and Y
 *
 * A zero-area triangle (degenerate) returns zero gradients.
 */
static void compute_gradients(
    float ax, float ay, float bx, float by, float cx, float cy,
    float va, float vb, float vc,
    float *dpdx, float *dpdy)
{
    /* Compute signed area of triangle (2x actual area) */
    float area = (bx - ax) * (cy - ay) - (cx - ax) * (by - ay);
    if (area == 0.0f) {
        *dpdx = 0.0f;
        *dpdy = 0.0f;
        return;
    }
    float inv_area = 1.0f / area;

    /* Gradients via Cramer's rule */
    *dpdx = ((vb - va) * (cy - ay) - (vc - va) * (by - ay)) * inv_area;
    *dpdy = ((vc - va) * (bx - ax) - (vb - va) * (cx - ax)) * inv_area;
}

/*
 * grDrawTriangle - Render a single triangle
 *
 * From the 3dfx SDK:
 * "grDrawTriangle() renders a triangle defined by three vertices.
 * The vertices must be in screen coordinates (post-projection) with
 * pre-computed 1/W values for perspective correction."
 *
 * Parameters:
 *   a, b, c - Pointers to GrVertex structures defining the triangle.
 *             The vertex data is read immediately; pointers don't need
 *             to remain valid after the call returns.
 *
 * Our implementation:
 *   1. Applies viewport offset to vertex positions
 *   2. Performs culling check if enabled
 *   3. Converts coordinates to fixed-point
 *   4. Computes parameter gradients
 *   5. Sets up texture coordinates if texturing enabled
 *   6. Calls the software rasterizer (voodoo_triangle)
 */
void __stdcall grDrawTriangle(const GrVertex *a, const GrVertex *b, const GrVertex *c)
{
    if (!g_voodoo || !g_voodoo->active) return;

    g_triangle_count++;
    {
        char dbg[256];
        snprintf(dbg, sizeof(dbg),
                 "glide3x: grDrawTriangle #%d a=(%.1f,%.1f) b=(%.1f,%.1f) c=(%.1f,%.1f)\n",
                 g_triangle_count, a->x, a->y, b->x, b->y, c->x, c->y);
        debug_log(dbg);
    }

    fbi_state *fbi = &g_voodoo->fbi;

    /* Get vertex positions with viewport offset */
    float ax = a->x + (float)g_voodoo->vp_x;
    float ay = a->y + (float)g_voodoo->vp_y;
    float bx = b->x + (float)g_voodoo->vp_x;
    float by = b->y + (float)g_voodoo->vp_y;
    float cx = c->x + (float)g_voodoo->vp_x;
    float cy = c->y + (float)g_voodoo->vp_y;

    /* Culling check */
    if (g_voodoo->cull_mode != GR_CULL_DISABLE) {
        float area = (bx - ax) * (cy - ay) - (cx - ax) * (by - ay);
        if (g_voodoo->cull_mode == GR_CULL_POSITIVE && area > 0) return;
        if (g_voodoo->cull_mode == GR_CULL_NEGATIVE && area < 0) return;
    }

    /* Convert to 12.4 fixed point for rasterizer */
    fbi->ax = (int16_t)(ax * 16.0f);
    fbi->ay = (int16_t)(ay * 16.0f);
    fbi->bx = (int16_t)(bx * 16.0f);
    fbi->by = (int16_t)(by * 16.0f);
    fbi->cx = (int16_t)(cx * 16.0f);
    fbi->cy = (int16_t)(cy * 16.0f);

    /* Compute color gradients */
    float drdx, drdy, dgdx, dgdy, dbdx, dbdy, dadx, dady;
    compute_gradients(ax, ay, bx, by, cx, cy, a->r, b->r, c->r, &drdx, &drdy);
    compute_gradients(ax, ay, bx, by, cx, cy, a->g, b->g, c->g, &dgdx, &dgdy);
    compute_gradients(ax, ay, bx, by, cx, cy, a->b, b->b, c->b, &dbdx, &dbdy);
    compute_gradients(ax, ay, bx, by, cx, cy, a->a, b->a, c->a, &dadx, &dady);

    /* Set up start values and gradients in 12.12 fixed point */
    fbi->startr = (int32_t)(a->r * 4096.0f);
    fbi->startg = (int32_t)(a->g * 4096.0f);
    fbi->startb = (int32_t)(a->b * 4096.0f);
    fbi->starta = (int32_t)(a->a * 4096.0f);

    fbi->drdx = (int32_t)(drdx * 4096.0f);
    fbi->dgdx = (int32_t)(dgdx * 4096.0f);
    fbi->dbdx = (int32_t)(dbdx * 4096.0f);
    fbi->dadx = (int32_t)(dadx * 4096.0f);

    fbi->drdy = (int32_t)(drdy * 4096.0f);
    fbi->dgdy = (int32_t)(dgdy * 4096.0f);
    fbi->dbdy = (int32_t)(dbdy * 4096.0f);
    fbi->dady = (int32_t)(dady * 4096.0f);

    /* Set up Z/W gradients */
    float dzdx_f, dzdy_f;
    compute_gradients(ax, ay, bx, by, cx, cy, a->ooz, b->ooz, c->ooz, &dzdx_f, &dzdy_f);

    fbi->startz = (int32_t)(a->ooz * 4096.0f);
    fbi->dzdx = (int32_t)(dzdx_f * 4096.0f);
    fbi->dzdy = (int32_t)(dzdy_f * 4096.0f);

    /* W in 16.32 fixed point */
    float dwdx_f, dwdy_f;
    compute_gradients(ax, ay, bx, by, cx, cy, a->oow, b->oow, c->oow, &dwdx_f, &dwdy_f);
    fbi->startw = (int64_t)(a->oow * 4294967296.0);
    fbi->dwdx = (int64_t)(dwdx_f * 4294967296.0);
    fbi->dwdy = (int64_t)(dwdy_f * 4294967296.0);

    /* Set up texture coordinates if texturing enabled */
    if (FBZCP_TEXTURE_ENABLE(g_voodoo->reg[fbzColorPath].u)) {
        tmu_state *tmu0 = &g_voodoo->tmu[0];

        float s0a = a->sow, t0a = a->tow, w0a = a->oow;
        float s0b = b->sow, t0b = b->tow, w0b = b->oow;
        float s0c = c->sow, t0c = c->tow, w0c = c->oow;

        float ds0dx, ds0dy, dt0dx, dt0dy, dw0dx, dw0dy;
        compute_gradients(ax, ay, bx, by, cx, cy, s0a, s0b, s0c, &ds0dx, &ds0dy);
        compute_gradients(ax, ay, bx, by, cx, cy, t0a, t0b, t0c, &dt0dx, &dt0dy);
        compute_gradients(ax, ay, bx, by, cx, cy, w0a, w0b, w0c, &dw0dx, &dw0dy);

        /* S/T in 14.18 fixed point, W in 2.30 */
        tmu0->starts = (int64_t)(s0a * 262144.0);
        tmu0->startt = (int64_t)(t0a * 262144.0);
        tmu0->startw = (int64_t)(w0a * 1073741824.0);

        tmu0->dsdx = (int64_t)(ds0dx * 262144.0);
        tmu0->dtdx = (int64_t)(dt0dx * 262144.0);
        tmu0->dwdx = (int64_t)(dw0dx * 1073741824.0);

        tmu0->dsdy = (int64_t)(ds0dy * 262144.0);
        tmu0->dtdy = (int64_t)(dt0dy * 262144.0);
        tmu0->dwdy = (int64_t)(dw0dy * 1073741824.0);
    }

    /* Call the software rasterizer */
    voodoo_triangle(g_voodoo);
}

/*
 * grDrawVertexArray - Draw primitives from an array of vertex pointers
 *
 * From the 3dfx SDK:
 * "grDrawVertexArray() draws primitives defined by an array of pointers
 * to vertices."
 *
 * Parameters:
 *   mode     - Primitive type (GR_TRIANGLES, GR_TRIANGLE_STRIP, etc.)
 *   count    - Number of vertices in the array
 *   pointers - Array of GrVertex* pointers
 *
 * This is more flexible than grDrawVertexArrayContiguous() because
 * vertices can be scattered in memory.
 */
void __stdcall grDrawVertexArray(FxU32 mode, FxU32 count, void *pointers)
{
    GrVertex **verts = (GrVertex **)pointers;
    FxU32 i;

    g_draw_call_count++;
    {
        char dbg[128];
        snprintf(dbg, sizeof(dbg),
                 "glide3x: grDrawVertexArray(mode=%u, count=%u)\n", mode, count);
        debug_log(dbg);
    }

    if (count < 3) return;

    switch (mode) {
    case GR_TRIANGLES:
        for (i = 0; i + 2 < count; i += 3) {
            grDrawTriangle(verts[i], verts[i + 1], verts[i + 2]);
        }
        break;

    case GR_TRIANGLE_STRIP:
    case GR_TRIANGLE_STRIP_CONTINUE:
        for (i = 0; i + 2 < count; i++) {
            if (i & 1) {
                grDrawTriangle(verts[i + 1], verts[i], verts[i + 2]);
            } else {
                grDrawTriangle(verts[i], verts[i + 1], verts[i + 2]);
            }
        }
        break;

    case GR_TRIANGLE_FAN:
    case GR_TRIANGLE_FAN_CONTINUE:
        for (i = 1; i + 1 < count; i++) {
            grDrawTriangle(verts[0], verts[i], verts[i + 1]);
        }
        break;

    default:
        break;
    }
}

/*
 * grDrawVertexArrayContiguous - Draw from contiguous vertex buffer
 *
 * From the 3dfx SDK:
 * "grDrawVertexArrayContiguous() draws primitives from a contiguous
 * array of vertices."
 *
 * Parameters:
 *   mode     - Primitive type
 *   count    - Number of vertices
 *   vertices - Pointer to first vertex
 *   stride   - Bytes between consecutive vertices
 *
 * More efficient than pointer array when vertices are packed together.
 * The stride parameter allows for interleaved vertex attributes.
 */
void __stdcall grDrawVertexArrayContiguous(FxU32 mode, FxU32 count, void *vertices, FxU32 stride)
{
    uint8_t *vdata = (uint8_t *)vertices;
    FxU32 i;

    g_draw_call_count++;
    {
        char dbg[128];
        snprintf(dbg, sizeof(dbg),
                 "glide3x: grDrawVertexArrayContiguous(mode=%u, count=%u, stride=%u)\n",
                 mode, count, stride);
        debug_log(dbg);
    }

    if (count < 3 || stride == 0) return;

    switch (mode) {
    case GR_TRIANGLES:
        for (i = 0; i + 2 < count; i += 3) {
            grDrawTriangle(
                (GrVertex*)(vdata + i * stride),
                (GrVertex*)(vdata + (i + 1) * stride),
                (GrVertex*)(vdata + (i + 2) * stride)
            );
        }
        break;

    case GR_TRIANGLE_STRIP:
    case GR_TRIANGLE_STRIP_CONTINUE:
        for (i = 0; i + 2 < count; i++) {
            if (i & 1) {
                grDrawTriangle(
                    (GrVertex*)(vdata + (i + 1) * stride),
                    (GrVertex*)(vdata + i * stride),
                    (GrVertex*)(vdata + (i + 2) * stride)
                );
            } else {
                grDrawTriangle(
                    (GrVertex*)(vdata + i * stride),
                    (GrVertex*)(vdata + (i + 1) * stride),
                    (GrVertex*)(vdata + (i + 2) * stride)
                );
            }
        }
        break;

    case GR_TRIANGLE_FAN:
    case GR_TRIANGLE_FAN_CONTINUE:
        for (i = 1; i + 1 < count; i++) {
            grDrawTriangle(
                (GrVertex*)vdata,
                (GrVertex*)(vdata + i * stride),
                (GrVertex*)(vdata + (i + 1) * stride)
            );
        }
        break;

    default:
        break;
    }
}

/*
 * grDrawPoint - Draw a single point
 *
 * Glide didn't have native point rendering, but some wrappers add it.
 * We emulate by drawing a tiny triangle.
 */
void __stdcall grDrawPoint(const void *pt)
{
    LOG_FUNC();
    const GrVertex *v = (const GrVertex*)pt;
    if (!v) return;

    GrVertex v1 = *v;
    GrVertex v2 = *v;
    GrVertex v3 = *v;

    v2.x += 1.0f;
    v3.y += 1.0f;

    grDrawTriangle(&v1, &v2, &v3);
}

/*
 * grDrawLine - Draw a line segment
 *
 * Emulated by drawing a thin triangle.
 */
void __stdcall grDrawLine(const void *v1_in, const void *v2_in)
{
    LOG_FUNC();
    const GrVertex *v1 = (const GrVertex*)v1_in;
    const GrVertex *v2 = (const GrVertex*)v2_in;

    if (!v1 || !v2) return;

    GrVertex a = *v1;
    GrVertex b = *v2;
    GrVertex c = *v2;

    c.x += 0.5f;
    c.y += 0.5f;

    grDrawTriangle(&a, &b, &c);
}

/*
 * grAADrawTriangle - Draw anti-aliased triangle
 *
 * Anti-aliased triangle rendering was a feature of Voodoo hardware.
 * We don't implement true AA, just draw a normal triangle.
 */
void __stdcall grAADrawTriangle(const void *a, const void *b, const void *c,
                      FxBool ab_antialias, FxBool bc_antialias, FxBool ca_antialias)
{
    LOG_FUNC();
    (void)ab_antialias;
    (void)bc_antialias;
    (void)ca_antialias;
    grDrawTriangle(a, b, c);
}

/*
 * grCullMode - Set back-face culling mode
 *
 * From the 3dfx SDK:
 * "grCullMode() enables or disables culling of back-facing or
 * front-facing triangles."
 *
 * Parameters:
 *   mode - GR_CULL_DISABLE:  Draw all triangles
 *          GR_CULL_NEGATIVE: Cull clockwise (negative area) triangles
 *          GR_CULL_POSITIVE: Cull counter-clockwise (positive area)
 */
void __stdcall grCullMode(GrCullMode_t mode)
{
    LOG("grCullMode(%d)", mode);
    if (!g_voodoo) return;
    g_voodoo->cull_mode = mode;
}
