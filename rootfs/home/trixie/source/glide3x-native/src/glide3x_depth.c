/*
 * glide3x_depth.c - Depth buffer operations
 *
 * This module implements depth buffer (Z-buffer) configuration:
 *   - grDepthBufferMode(): Enable/disable and select Z or W buffering
 *   - grDepthBufferFunction(): Set depth comparison function
 *   - grDepthMask(): Enable/disable depth buffer writes
 *   - grDepthBiasLevel(): Set depth bias for decals
 *
 * DEPTH BUFFERING CONCEPT:
 * Depth buffering (also called Z-buffering) is the standard technique for
 * hidden surface removal. For each pixel, the depth buffer stores the
 * distance from the camera to the closest surface drawn so far.
 *
 * When rendering a new pixel:
 *   1. Compare its depth against the stored depth
 *   2. If comparison passes, draw pixel and update stored depth
 *   3. If comparison fails, discard pixel (it's behind something)
 *
 * This allows correct visibility regardless of draw order, unlike the
 * painter's algorithm which requires back-to-front sorting.
 *
 * Z-BUFFERING VS W-BUFFERING:
 *
 * Z-BUFFERING:
 * - Stores the normalized device coordinate Z value (after perspective divide)
 * - Z is non-linear: more precision near the far plane, less near camera
 * - Can cause "Z-fighting" for distant coplanar surfaces
 * - Formula: z_ndc = (z_eye * (f+n) + 2*f*n) / (z_eye * (f-n))
 *            where n=near plane, f=far plane
 *
 * W-BUFFERING:
 * - Stores the clip coordinate W (essentially eye-space Z before divide)
 * - W is linear: uniform precision throughout the depth range
 * - Better for large outdoor scenes with distant geometry
 * - Requires valid W values from the application (oow = 1/W)
 *
 * Voodoo supported both modes. Most games used Z-buffering because:
 * - Simpler setup (Z is always available after projection)
 * - Good enough for indoor scenes with moderate depth range
 * - Some games didn't provide correct W values
 *
 * DEPTH PRECISION:
 * The depth buffer stores 16-bit values (0-65535).
 *
 * For Z-buffering:
 *   - 0x0000 = near plane (closest)
 *   - 0xFFFF = far plane (farthest)
 *
 * For W-buffering:
 *   - Interpretation depends on projection matrix
 *   - Generally: smaller W = farther, larger W = closer
 *
 * DEPTH COMPARISON FUNCTIONS (GR_CMP_*):
 *   NEVER:    Never pass (pixel always discarded)
 *   LESS:     Pass if incoming < buffer (closer wins)
 *   EQUAL:    Pass if incoming == buffer (exact match)
 *   LEQUAL:   Pass if incoming <= buffer (closer or same wins)
 *   GREATER:  Pass if incoming > buffer (farther wins)
 *   NOTEQUAL: Pass if incoming != buffer
 *   GEQUAL:   Pass if incoming >= buffer
 *   ALWAYS:   Always pass (depth test disabled)
 *
 * Standard rendering uses LESS or LEQUAL. LEQUAL is more common because
 * it handles cases where the same geometry is drawn twice (multi-pass).
 *
 * DEPTH BIAS:
 * When two surfaces are at the same depth (coplanar), Z-fighting can occur
 * where pixels alternate between surfaces due to floating-point precision.
 * Depth bias adds a small offset to push one surface in front of the other.
 *
 * Common use: Decals (bullet holes, tire marks) on walls/ground.
 * Without bias, the decal Z-fights with the surface it's on.
 * With bias, the decal is pushed slightly toward the camera.
 *
 * DEPTH WRITES:
 * grDepthMask() controls whether pixels that pass the depth test also
 * update the depth buffer. Uses:
 *
 *   - Disabled for transparent surfaces: They should be visible behind
 *     other transparent surfaces, so don't write depth.
 *
 *   - Disabled for particle effects: Particles shouldn't occlude each
 *     other or affect subsequent depth tests.
 *
 *   - Disabled for HUD/UI: 2D elements drawn last shouldn't affect depth.
 *
 *   - Enabled for opaque geometry: Normal rendering updates depth.
 */

#include "glide3x_state.h"

/*
 * grDepthBufferMode - Configure depth buffer operation
 *
 * From the 3dfx SDK:
 * "grDepthBufferMode() configures the depth buffer mode. The depth buffer
 * is used for hidden surface removal."
 *
 * Parameters:
 *   mode - One of GR_DEPTHBUFFER_*:
 *          DISABLE:              Depth testing and writing disabled
 *          ZBUFFER:              Standard Z-buffering
 *          WBUFFER:              W-buffering (linear depth)
 *          ZBUFFER_COMPARE_TO_BIAS: Z compare against bias value
 *          WBUFFER_COMPARE_TO_BIAS: W compare against bias value
 *
 * The "compare to bias" modes compare incoming depth against the
 * grDepthBiasLevel() value instead of the stored buffer value.
 * This is useful for certain special effects.
 *
 * Note: This function affects both depth testing and depth writes.
 * For finer control, use grDepthMask() after setting the mode.
 */
void __stdcall grDepthBufferMode(GrDepthBufferMode_t mode)
{
    if (!g_voodoo) return;

    /*
     * fbzMode register depth-related bits:
     *   Bit 3:  W buffer select (0=Z, 1=W)
     *   Bit 4:  Depth buffer enable
     *   Bit 5-7: Depth function (set separately)
     *   Bit 20: Depth bias enable (compare to bias value)
     */

    uint32_t val = g_voodoo->reg[fbzMode].u;

    /* Clear depth-related bits first */
    val &= ~(FBZMODE_WBUFFER_SELECT_BIT | FBZMODE_ENABLE_DEPTHBUF_BIT | FBZMODE_DEPTH_SOURCE_COMPARE_BIT);

    /* Enable depth buffer */
    if (mode != GR_DEPTHBUFFER_DISABLE) {
        val |= FBZMODE_ENABLE_DEPTHBUF_BIT;
    }

    /* W-buffer select */
    if (mode == GR_DEPTHBUFFER_WBUFFER || mode == GR_DEPTHBUFFER_WBUFFER_COMPARE_TO_BIAS) {
        val |= FBZMODE_WBUFFER_SELECT_BIT;
    }

    /* Compare to bias value instead of buffer */
    if (mode == GR_DEPTHBUFFER_ZBUFFER_COMPARE_TO_BIAS ||
        mode == GR_DEPTHBUFFER_WBUFFER_COMPARE_TO_BIAS) {
        val |= FBZMODE_DEPTH_SOURCE_COMPARE_BIT;
    }

    g_voodoo->reg[fbzMode].u = val;
}

/*
 * grDepthBufferFunction - Set depth comparison function
 *
 * From the 3dfx SDK:
 * "grDepthBufferFunction() sets the function used to compare incoming
 * depth values against values in the depth buffer."
 *
 * Parameters:
 *   func - Comparison function (GR_CMP_*):
 *          NEVER:    Always fail
 *          LESS:     Pass if incoming < buffer
 *          EQUAL:    Pass if incoming == buffer
 *          LEQUAL:   Pass if incoming <= buffer
 *          GREATER:  Pass if incoming > buffer
 *          NOTEQUAL: Pass if incoming != buffer
 *          GEQUAL:   Pass if incoming >= buffer
 *          ALWAYS:   Always pass
 *
 * For standard Z-buffering (small Z = near):
 *   Use LESS or LEQUAL so nearer objects win.
 *
 * For reverse-Z or W-buffering (large = near):
 *   Use GREATER or GEQUAL so nearer objects win.
 *
 * LEQUAL is more robust than LESS:
 *   - Handles exact depth matches (same triangle drawn twice)
 *   - Works with multi-pass rendering
 *   - Prevents flickering on coplanar surfaces
 */
void __stdcall grDepthBufferFunction(GrCmpFnc_t func)
{
    if (!g_voodoo) return;

    /*
     * fbzMode register:
     *   Bits 5-7: Depth comparison function (3 bits for 8 functions)
     */

    uint32_t val = g_voodoo->reg[fbzMode].u;

    /* Clear and set depth function bits */
    val &= ~FBZMODE_DEPTH_FUNCTION_MASK;
    val |= ((func & 0x7) << FBZMODE_DEPTH_FUNCTION_SHIFT);

    g_voodoo->reg[fbzMode].u = val;
}

/*
 * grDepthMask - Enable/disable depth buffer writes
 *
 * From the 3dfx SDK:
 * "grDepthMask() enables or disables writing to the depth buffer."
 *
 * Parameters:
 *   mask - FXTRUE to enable writes, FXFALSE to disable
 *
 * Note: This only affects writes, not reads. Depth testing still occurs
 * based on grDepthBufferMode() and grDepthBufferFunction().
 *
 * Common usage patterns:
 *
 * 1. OPAQUE GEOMETRY:
 *    grDepthMask(FXTRUE);
 *    grDepthBufferMode(GR_DEPTHBUFFER_ZBUFFER);
 *    Draw opaque objects...
 *
 * 2. TRANSPARENT GEOMETRY (after opaques):
 *    grDepthMask(FXFALSE);  // Don't update depth
 *    grDepthBufferMode(GR_DEPTHBUFFER_ZBUFFER);  // Still test depth
 *    Draw transparent objects back-to-front...
 *
 * 3. SKY/BACKGROUND:
 *    grDepthMask(FXFALSE);  // Don't update depth
 *    grDepthBufferFunction(GR_CMP_ALWAYS);  // Always pass
 *    Draw sky dome...
 *    Then restore normal depth settings for scene.
 *
 * 4. DEPTH-ONLY PASS (for shadow mapping, etc.):
 *    grDepthMask(FXTRUE);
 *    grColorMask(FXFALSE, FXFALSE);  // No color writes
 *    Draw occluders to build depth buffer...
 */
void __stdcall grDepthMask(FxBool mask)
{
    if (!g_voodoo) return;

    /* Update shadow state for tracking */
    g_voodoo->depth_mask = mask;

    /*
     * fbzMode register:
     *   Bit 10: Auxiliary buffer write mask (shared with alpha)
     *           In Voodoo hardware, bit SET = writes ENABLED
     *           This matches the rasterizer check in PIXEL_PIPELINE_FINISH.
     *
     * The aux buffer contains depth (and optionally alpha).
     * We only disable writes if BOTH depth_mask AND alpha_mask are false.
     */
    uint32_t val = g_voodoo->reg[fbzMode].u;

    if (g_voodoo->alpha_mask || g_voodoo->depth_mask) {
        val |= FBZMODE_AUX_BUFFER_MASK_BIT;
    } else {
        val &= ~FBZMODE_AUX_BUFFER_MASK_BIT;
    }

    g_voodoo->reg[fbzMode].u = val;
}

/*
 * grDepthBiasLevel - Set depth bias for decals
 *
 * From the 3dfx SDK:
 * "grDepthBiasLevel() sets a constant value that is added to the depth
 * value of each pixel. This is used to prevent Z-fighting between
 * coplanar polygons."
 *
 * Parameters:
 *   level - Depth bias value (signed 16-bit)
 *           Positive values push geometry toward camera
 *           Negative values push geometry away from camera
 *
 * The bias is added to the computed depth before comparison/storage.
 * The appropriate value depends on:
 *   - Depth buffer precision (16-bit in our case)
 *   - Depth range (near/far clip planes)
 *   - The "gap" needed between surfaces
 *
 * Typical values range from 1 to 1000, with common values around 16-128.
 *
 * Usage example for bullet hole decals:
 *   1. Draw wall normally
 *   2. grDepthBiasLevel(16);  // Push decal toward camera
 *   3. Draw bullet hole texture on wall
 *   4. grDepthBiasLevel(0);   // Reset for next object
 *
 * Note: Too much bias causes decals to "float" visibly above surfaces.
 * Too little bias doesn't fully prevent Z-fighting.
 * The optimal value is the minimum that eliminates fighting.
 */
void __stdcall grDepthBiasLevel(FxI32 level)
{
    
    if (!g_voodoo) return;

    /*
     * zaColor register layout:
     *   Bits 0-15:  Depth bias value (signed)
     *   Bits 16-31: Alpha value for constant alpha mode
     *
     * We preserve the alpha portion while updating bias.
     */
    g_voodoo->reg[zaColor].u = (g_voodoo->reg[zaColor].u & 0xFFFF0000) | (level & 0xFFFF);
}
