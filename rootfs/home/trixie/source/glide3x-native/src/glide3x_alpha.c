/*
 * glide3x_alpha.c - Alpha testing and color masking
 *
 * This module implements alpha testing and write masks:
 *   - grAlphaTestFunction(): Set alpha comparison function
 *   - grAlphaTestReferenceValue(): Set alpha reference value
 *   - grColorMask(): Enable/disable color and alpha writes
 *
 * ALPHA TESTING CONCEPT:
 * Alpha testing is a binary accept/reject decision for each pixel based
 * on comparing the pixel's alpha value against a reference value.
 *
 * Unlike alpha blending (which creates smooth transparency), alpha testing
 * either draws the pixel fully or discards it completely. This creates
 * hard edges, which is desirable for:
 *
 *   - Vegetation (grass, leaves, fences)
 *   - Chain-link fences, gratings
 *   - Sprite cutouts (2D characters in 3D world)
 *   - Decals with alpha masks
 *   - Text rendering
 *
 * THE ALPHA TEST PIPELINE:
 *   1. Color combine produces final RGBA
 *   2. Alpha compare: test alpha against reference
 *   3. If test FAILS: pixel is discarded (no FB or depth write)
 *   4. If test PASSES: pixel continues to depth test, blend, write
 *
 * ALPHA TEST VS ALPHA BLEND:
 *
 *   ALPHA TEST:
 *   - Binary: pixel either drawn fully or not at all
 *   - No framebuffer read needed (faster)
 *   - Creates hard, potentially aliased edges
 *   - Works with depth buffer (discarded pixels don't write depth)
 *   - Draw order independent for opaque areas
 *   - Common test: alpha >= 0.5 (or 128 in 0-255 range)
 *
 *   ALPHA BLEND:
 *   - Continuous: pixel mixed proportionally with framebuffer
 *   - Requires framebuffer read (slower)
 *   - Creates smooth, anti-aliased edges
 *   - Problematic with depth buffer (what depth to write?)
 *   - Requires back-to-front draw order
 *
 * COMBINED USAGE:
 * Many games use both together:
 *   1. Alpha test to discard fully transparent pixels (alpha = 0)
 *      This avoids writing depth for invisible areas.
 *   2. Alpha blend for semi-transparent pixels (0 < alpha < 1)
 *      This creates smooth edges on the remaining pixels.
 *
 * Example for foliage sprite:
 *   grAlphaTestFunction(GR_CMP_GREATER);   // Discard alpha=0
 *   grAlphaTestReferenceValue(0);
 *   grAlphaBlendFunction(SRC_ALPHA, ONE_MINUS_SRC_ALPHA, ...);
 *
 * COLOR AND ALPHA MASKING:
 * grColorMask() controls which framebuffer channels are written:
 *
 *   - RGB enabled: Color pixels are written to framebuffer
 *   - RGB disabled: Color pixels not written (depth-only pass)
 *   - Alpha enabled: Alpha channel written (if FB has alpha)
 *   - Alpha disabled: Alpha channel not written
 *
 * Note: On Voodoo 1/2 with RGB565 framebuffer, there is no alpha channel
 * to write. The alpha mask affects the auxiliary buffer which typically
 * stores depth, not alpha.
 */

#include "glide3x_state.h"

/*
 * grAlphaTestFunction - Set alpha test comparison function
 *
 * From the 3dfx SDK:
 * "grAlphaTestFunction() sets the function used to compare the alpha value
 * of the pixel being rendered against the alpha reference value set by
 * grAlphaTestReferenceValue()."
 *
 * Parameters:
 *   function - Comparison function (GR_CMP_*):
 *              NEVER:    Always discard (nothing passes)
 *              ALWAYS:   Always pass (alpha test disabled)
 *              LESS:     Pass if pixel_alpha < reference
 *              LEQUAL:   Pass if pixel_alpha <= reference
 *              EQUAL:    Pass if pixel_alpha == reference
 *              GEQUAL:   Pass if pixel_alpha >= reference (most common)
 *              GREATER:  Pass if pixel_alpha > reference
 *              NOTEQUAL: Pass if pixel_alpha != reference
 *
 * Common configurations:
 *
 * 1. CUTOUT SPRITES (discard transparent pixels):
 *    grAlphaTestFunction(GR_CMP_GEQUAL);
 *    grAlphaTestReferenceValue(128);  // 50% threshold
 *    Result: Pixels with alpha >= 128 drawn, others discarded.
 *
 * 2. BINARY MASK (full alpha or nothing):
 *    grAlphaTestFunction(GR_CMP_NOTEQUAL);
 *    grAlphaTestReferenceValue(0);
 *    Result: Only pixels with non-zero alpha are drawn.
 *
 * 3. INVERTED MASK (draw only transparent areas):
 *    grAlphaTestFunction(GR_CMP_LESS);
 *    grAlphaTestReferenceValue(128);
 *    Result: Only pixels with alpha < 128 drawn (unusual effect).
 *
 * 4. DISABLED (all pixels pass):
 *    grAlphaTestFunction(GR_CMP_ALWAYS);
 *    Result: No alpha test, all pixels proceed to next stage.
 *
 * Note: Alpha test uses the alpha from the alpha combine unit output,
 * after texture lookup and combine operations but before blending.
 */
void __stdcall grAlphaTestFunction(GrCmpFnc_t function)
{
    LOG("grAlphaTestFunction(%d)", function);
    if (!g_voodoo) return;

    /*
     * alphaMode register layout:
     *   Bit 0:     Alpha test enable (auto-enabled when function set)
     *   Bits 1-3:  Alpha test function (3 bits, 8 functions)
     *   Bit 4:     Alpha blend enable
     *   Bits 8-11:  RGB source blend factor
     *   Bits 12-15: RGB dest blend factor
     *   Bits 16-19: Alpha source blend factor
     *   Bits 20-23: Alpha dest blend factor
     *   Bits 24-31: Alpha reference value
     */

    uint32_t val = g_voodoo->reg[alphaMode].u;

    /* Clear and set alpha function */
    val &= ~ALPHAMODE_ALPHAFUNCTION_MASK;
    val |= ((function & 0x7) << ALPHAMODE_ALPHAFUNCTION_SHIFT);

    /* Enable alpha test if function is not ALWAYS */
    if (function != GR_CMP_ALWAYS) {
        val |= ALPHAMODE_ALPHATEST_BIT;
    } else {
        val &= ~ALPHAMODE_ALPHATEST_BIT;
    }

    g_voodoo->reg[alphaMode].u = val;
}

/*
 * grAlphaTestReferenceValue - Set alpha test reference value
 *
 * From the 3dfx SDK:
 * "grAlphaTestReferenceValue() sets the reference value that the pixel's
 * alpha is compared against during alpha testing."
 *
 * Parameters:
 *   value - Reference alpha value (0-255)
 *           0   = fully transparent reference
 *           128 = half transparent reference
 *           255 = fully opaque reference
 *
 * The comparison is: pixel_alpha <op> reference
 * Where <op> is the function set by grAlphaTestFunction().
 *
 * Choosing a reference value:
 *
 *   - 0: Only discard pixels with exactly zero alpha
 *   - 1-16: Discard nearly transparent pixels (soft threshold)
 *   - 128: Common "50% cutoff" for binary transparency
 *   - 255: Only pass fully opaque pixels
 *
 * For cutout sprites with anti-aliased edges:
 * Use a value like 128 to get reasonably clean edges while
 * preserving some of the anti-aliased boundary pixels.
 *
 * For hard-edged masks (fonts, UI):
 * Use a low value (1-16) to preserve all visible pixels.
 */
void __stdcall grAlphaTestReferenceValue(GrAlpha_t value)
{
    LOG("grAlphaTestReferenceValue(%d)", value);
    if (!g_voodoo) return;

    uint32_t val = g_voodoo->reg[alphaMode].u;

    /* Clear and set alpha reference value */
    val &= ~ALPHAMODE_ALPHAREF_MASK;
    val |= ((value & 0xFF) << ALPHAMODE_ALPHAREF_SHIFT);

    g_voodoo->reg[alphaMode].u = val;
}

/*
 * grColorMask - Enable/disable color and alpha buffer writes
 *
 * From the 3dfx SDK:
 * "grColorMask() enables or disables color buffer writes and alpha buffer
 * writes independently."
 *
 * Parameters:
 *   rgb   - FXTRUE to enable RGB writes, FXFALSE to disable
 *   alpha - FXTRUE to enable alpha writes, FXFALSE to disable
 *
 * When RGB writes are disabled:
 *   - Pixels are still processed (depth test, etc.)
 *   - But color is not written to framebuffer
 *   - Useful for depth-only passes
 *
 * When alpha writes are disabled:
 *   - Alpha channel not modified (if framebuffer has alpha)
 *   - On RGB565, this affects the auxiliary buffer
 *
 * Common usage patterns:
 *
 * 1. DEPTH-ONLY PREPASS:
 *    grColorMask(FXFALSE, FXFALSE);  // No color writes
 *    grDepthMask(FXTRUE);            // Depth writes enabled
 *    Draw occluders (fills depth buffer only)...
 *    grColorMask(FXTRUE, FXFALSE);   // Re-enable color
 *    Draw scene (depth already established)
 *    Benefit: Early-Z rejection for overdraw reduction
 *
 * 2. COLOR-ONLY PASS:
 *    grColorMask(FXTRUE, FXFALSE);
 *    grDepthMask(FXFALSE);
 *    Draw HUD/UI elements that shouldn't affect depth...
 *
 * 3. SHADOW MAPPING (fill shadow buffer):
 *    grColorMask(FXFALSE, FXFALSE);
 *    Draw scene from light's perspective (depth only)...
 *
 * Note: On Voodoo with 16-bit FB, the "alpha" mask actually controls
 * the auxiliary buffer (depth/alpha). We track both masks and only
 * disable aux writes when both are false.
 */
void __stdcall grColorMask(FxBool rgb, FxBool alpha)
{
    LOG("grColorMask(rgb=%d, alpha=%d)", rgb, alpha);
    if (!g_voodoo) return;

    /* Update shadow state for tracking */
    g_voodoo->alpha_mask = alpha;

    /*
     * fbzMode register:
     *   Bit 9:  RGB mask (1 = disable writes)
     *   Bit 10: Aux mask (1 = disable writes)
     *           Aux buffer shared by depth and alpha
     */

    uint32_t val = g_voodoo->reg[fbzMode].u;

    /*
     * RGB mask: FBZMODE_RGB_BUFFER_MASK_BIT
     * In Voodoo hardware, bit SET = writes ENABLED (mask allows writes)
     * This matches voodoo_create() and the rasterizer check.
     */
    if (rgb) {
        val |= FBZMODE_RGB_BUFFER_MASK_BIT;
    } else {
        val &= ~FBZMODE_RGB_BUFFER_MASK_BIT;
    }

    /*
     * Aux mask: FBZMODE_AUX_BUFFER_MASK_BIT
     * Same convention: bit SET = writes ENABLED
     * Enable writes if EITHER alpha OR depth mask is enabled.
     * This is because the aux buffer stores depth (which we want
     * to write when depth_mask is true) and potentially alpha.
     */
    if (g_voodoo->alpha_mask || g_voodoo->depth_mask) {
        val |= FBZMODE_AUX_BUFFER_MASK_BIT;
    } else {
        val &= ~FBZMODE_AUX_BUFFER_MASK_BIT;
    }

    g_voodoo->reg[fbzMode].u = val;
    LOG("  fbzMode updated: 0x%08X", val);
}
