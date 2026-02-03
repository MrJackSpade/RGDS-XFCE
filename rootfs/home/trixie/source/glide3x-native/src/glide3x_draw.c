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
 * Unpacked vertex data - read from raw vertex using layout offsets
 */
typedef struct {
    float x, y;
    float ooz, oow;
    float r, g, b, a;
    float sow, tow;
} UnpackedVertex;

/*
 * read_vertex_from_layout - Read vertex data using grVertexLayout offsets
 *
 * This reads vertex attributes from raw vertex data based on the offsets
 * configured by grVertexLayout(). If a particular attribute wasn't configured,
 * a default value is used.
 */
static void read_vertex_from_layout(const uint8_t *raw, UnpackedVertex *v)
{
    /* Defaults */
    v->x = v->y = 0.0f;
    v->ooz = 0.0f;
    v->oow = 1.0f;
    v->r = v->g = v->b = 255.0f;
    v->a = 255.0f;
    v->sow = v->tow = 0.0f;

    if (!g_voodoo) return;

    /* XY position (always 2 floats) */
    if (g_voodoo->vl_xy_offset >= 0) {
        const float *xy = (const float *)(raw + g_voodoo->vl_xy_offset);
        v->x = xy[0];
        v->y = xy[1];
    }

    /* Packed ARGB color (uint32) */
    if (g_voodoo->vl_pargb_offset >= 0) {
        uint32_t pargb = *(const uint32_t *)(raw + g_voodoo->vl_pargb_offset);
        v->a = (float)((pargb >> 24) & 0xFF);
        v->r = (float)((pargb >> 16) & 0xFF);
        v->g = (float)((pargb >> 8) & 0xFF);
        v->b = (float)(pargb & 0xFF);
    }
    /* RGB as separate floats */
    else if (g_voodoo->vl_rgb_offset >= 0) {
        const float *rgb = (const float *)(raw + g_voodoo->vl_rgb_offset);
        v->r = rgb[0];
        v->g = rgb[1];
        v->b = rgb[2];
    }

    /* Alpha as separate float */
    if (g_voodoo->vl_a_offset >= 0) {
        v->a = *(const float *)(raw + g_voodoo->vl_a_offset);
    }

    /* Q (1/W) for perspective - can be Q or Q0 */
    if (g_voodoo->vl_q0_offset >= 0) {
        v->oow = *(const float *)(raw + g_voodoo->vl_q0_offset);
    } else if (g_voodoo->vl_q_offset >= 0) {
        v->oow = *(const float *)(raw + g_voodoo->vl_q_offset);
    } else if (g_voodoo->vl_w_offset >= 0) {
        float w = *(const float *)(raw + g_voodoo->vl_w_offset);
        v->oow = (w != 0.0f) ? 1.0f / w : 1.0f;
    }

    /* Z for depth buffer */
    if (g_voodoo->vl_z_offset >= 0) {
        v->ooz = *(const float *)(raw + g_voodoo->vl_z_offset);
    }

    /* Texture coordinates S,T (2 floats) - use ST0 or ST1 based on active TMU */
    int32_t st_offset = (g_active_tmu == 0) ? g_voodoo->vl_st0_offset : g_voodoo->vl_st1_offset;
    if (st_offset < 0) st_offset = g_voodoo->vl_st0_offset;  /* Fallback to ST0 */
    if (st_offset >= 0) {
        const float *st = (const float *)(raw + st_offset);
        v->sow = st[0];
        v->tow = st[1];
    }
}

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

    fbi_state *fbi = &g_voodoo->fbi;

    /* Read vertices using layout offsets if configured, otherwise use GrVertex struct */
    UnpackedVertex va, vb, vc;

    if (g_voodoo->vl_xy_offset >= 0) {
        /* Use layout-based reading */
        read_vertex_from_layout((const uint8_t *)a, &va);
        read_vertex_from_layout((const uint8_t *)b, &vb);
        read_vertex_from_layout((const uint8_t *)c, &vc);
    } else {
        /* Use GrVertex struct directly */
        va.x = a->x; va.y = a->y;
        va.ooz = a->ooz; va.oow = a->oow;
        va.r = a->r; va.g = a->g; va.b = a->b; va.a = a->a;
        va.sow = a->sow; va.tow = a->tow;

        vb.x = b->x; vb.y = b->y;
        vb.ooz = b->ooz; vb.oow = b->oow;
        vb.r = b->r; vb.g = b->g; vb.b = b->b; vb.a = b->a;
        vb.sow = b->sow; vb.tow = b->tow;

        vc.x = c->x; vc.y = c->y;
        vc.ooz = c->ooz; vc.oow = c->oow;
        vc.r = c->r; vc.g = c->g; vc.b = c->b; vc.a = c->a;
        vc.sow = c->sow; vc.tow = c->tow;
    }

    {
        char dbg[256];
        snprintf(dbg, sizeof(dbg),
                 "glide3x: grDrawTriangle #%d a=(%.1f,%.1f) b=(%.1f,%.1f) c=(%.1f,%.1f)\n",
                 g_triangle_count, va.x, va.y, vb.x, vb.y, vc.x, vc.y);
        debug_log(dbg);
    }

    /* Get vertex positions with viewport offset */
    float ax = va.x + (float)g_voodoo->vp_x;
    float ay = va.y + (float)g_voodoo->vp_y;
    float bx = vb.x + (float)g_voodoo->vp_x;
    float by = vb.y + (float)g_voodoo->vp_y;
    float cx = vc.x + (float)g_voodoo->vp_x;
    float cy = vc.y + (float)g_voodoo->vp_y;

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
    compute_gradients(ax, ay, bx, by, cx, cy, va.r, vb.r, vc.r, &drdx, &drdy);
    compute_gradients(ax, ay, bx, by, cx, cy, va.g, vb.g, vc.g, &dgdx, &dgdy);
    compute_gradients(ax, ay, bx, by, cx, cy, va.b, vb.b, vc.b, &dbdx, &dbdy);
    compute_gradients(ax, ay, bx, by, cx, cy, va.a, vb.a, vc.a, &dadx, &dady);

    /* Set up start values and gradients in 12.12 fixed point */
    fbi->startr = (int32_t)(va.r * 4096.0f);
    fbi->startg = (int32_t)(va.g * 4096.0f);
    fbi->startb = (int32_t)(va.b * 4096.0f);
    fbi->starta = (int32_t)(va.a * 4096.0f);

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
    compute_gradients(ax, ay, bx, by, cx, cy, va.ooz, vb.ooz, vc.ooz, &dzdx_f, &dzdy_f);

    fbi->startz = (int32_t)(va.ooz * 4096.0f);
    fbi->dzdx = (int32_t)(dzdx_f * 4096.0f);
    fbi->dzdy = (int32_t)(dzdy_f * 4096.0f);

    /* W in 16.32 fixed point */
    float dwdx_f, dwdy_f;
    compute_gradients(ax, ay, bx, by, cx, cy, va.oow, vb.oow, vc.oow, &dwdx_f, &dwdy_f);
    fbi->startw = (int64_t)(va.oow * 4294967296.0);
    fbi->dwdx = (int64_t)(dwdx_f * 4294967296.0);
    fbi->dwdy = (int64_t)(dwdy_f * 4294967296.0);

    /* Set up texture coordinates if texturing enabled */
    if (FBZCP_TEXTURE_ENABLE(g_voodoo->reg[fbzColorPath].u)) {
        /* Use the active TMU (set by grTexSource) for texture coordinates */
        tmu_state *tmu0 = &g_voodoo->tmu[g_active_tmu];

        float s0a = va.sow, t0a = va.tow, w0a = va.oow;
        float s0b = vb.sow, t0b = vb.tow, w0b = vb.oow;
        float s0c = vc.sow, t0c = vc.tow, w0c = vc.oow;

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

        /* Debug: log texture coordinate setup */
        {
            char dbg[256];
            snprintf(dbg, sizeof(dbg),
                     "TEXSETUP: sow=(%f,%f,%f) tow=(%f,%f,%f) oow=(%f,%f,%f)\n",
                     s0a, s0b, s0c, t0a, t0b, t0c, w0a, w0b, w0c);
            debug_log(dbg);
            snprintf(dbg, sizeof(dbg),
                     "TEXSETUP: starts=%lld startt=%lld dsdx=%lld dtdx=%lld\n",
                     (long long)tmu0->starts, (long long)tmu0->startt,
                     (long long)tmu0->dsdx, (long long)tmu0->dtdx);
            debug_log(dbg);
        }
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
    if (!g_voodoo) return;
    g_voodoo->cull_mode = mode;
}
