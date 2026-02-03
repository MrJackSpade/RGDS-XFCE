/*
 * glide3x_blend.c - Alpha blending configuration
 *
 * This module implements alpha blending:
 *   - grAlphaBlendFunction(): Configure source/destination blend factors
 *
 * ALPHA BLENDING CONCEPT:
 * Alpha blending combines the color of the pixel being rendered (source)
 * with the color already in the framebuffer (destination) to create
 * transparency and other effects.
 *
 * The blending equation is:
 *   final_rgb   = src_rgb * rgb_sf   + dst_rgb * rgb_df
 *   final_alpha = src_alpha * alpha_sf + dst_alpha * alpha_df
 *
 * Where:
 *   src   = pixel being rendered (from triangle/texture/combine)
 *   dst   = pixel already in framebuffer
 *   sf    = source factor (selected by grAlphaBlendFunction)
 *   df    = destination factor
 *
 * BLEND FACTORS (GR_BLEND_*):
 *   ZERO:                 0
 *   ONE:                  1
 *   SRC_COLOR:            src.rgb
 *   ONE_MINUS_SRC_COLOR:  1 - src.rgb
 *   SRC_ALPHA:            src.a
 *   ONE_MINUS_SRC_ALPHA:  1 - src.a
 *   DST_ALPHA:            dst.a
 *   ONE_MINUS_DST_ALPHA:  1 - dst.a
 *   DST_COLOR:            dst.rgb (Voodoo 2+)
 *   ONE_MINUS_DST_COLOR:  1 - dst.rgb (Voodoo 2+)
 *   ALPHA_SATURATE:       min(src.a, 1-dst.a) (for Porter-Duff)
 *   PREFOG_COLOR:         Fog color (special effect)
 *
 * COMMON BLEND MODES:
 *
 * 1. STANDARD TRANSPARENCY (source over destination):
 *    rgb_sf = SRC_ALPHA, rgb_df = ONE_MINUS_SRC_ALPHA
 *    Equation: final = src * src.a + dst * (1 - src.a)
 *    Use: Glass, water, semi-transparent objects
 *    When src.a = 1.0, src fully replaces dst (opaque)
 *    When src.a = 0.0, dst shows through completely (invisible)
 *    When src.a = 0.5, 50% blend
 *
 * 2. ADDITIVE BLENDING:
 *    rgb_sf = SRC_ALPHA (or ONE), rgb_df = ONE
 *    Equation: final = src * src.a + dst
 *    Use: Fire, explosions, glowing effects, light sources
 *    Colors add together, creating bright spots where effects overlap.
 *    Black (0,0,0) has no effect, white adds maximum brightness.
 *
 * 3. MULTIPLICATIVE BLENDING (modulate):
 *    rgb_sf = ZERO, rgb_df = SRC_COLOR
 *    Equation: final = dst * src
 *    Use: Shadows, darkening overlays, color tinting
 *    White (1,1,1) preserves dst, black (0,0,0) makes dst black.
 *    Gray values darken proportionally.
 *
 * 4. OPAQUE (disabled blending):
 *    rgb_sf = ONE, rgb_df = ZERO
 *    Equation: final = src
 *    Use: Normal opaque rendering
 *    Source completely replaces destination.
 *
 * 5. PREMULTIPLIED ALPHA:
 *    rgb_sf = ONE, rgb_df = ONE_MINUS_SRC_ALPHA
 *    Equation: final = src + dst * (1 - src.a)
 *    Use: Pre-multiplied alpha textures (more efficient blending)
 *    Source RGB already multiplied by alpha during texture creation.
 *
 * 6. SCREEN BLENDING:
 *    rgb_sf = ONE, rgb_df = ONE_MINUS_SRC_COLOR
 *    Equation: final = src + dst - src * dst
 *    Use: Lightening effects, inverse of multiply
 *
 * ALPHA CHANNEL BLENDING:
 * The alpha channel can use different factors than RGB, allowing effects like:
 *   - Accumulating alpha in the framebuffer for later use
 *   - Preserving destination alpha while blending RGB
 *   - Building alpha masks incrementally
 *
 * Note: Voodoo 1/2 with 16-bit framebuffer (RGB565) have no destination
 * alpha storage. DST_ALPHA reads as 1.0 (or 0.0 depending on mode).
 * True destination alpha requires ARGB4444 or 32-bit modes.
 *
 * PERFORMANCE:
 * Alpha blending requires reading the framebuffer (for destination color),
 * computing the blend, and writing back. This is slower than opaque
 * rendering which only writes. On Voodoo, blending was still hardware
 * accelerated but consumed fillrate bandwidth.
 *
 * DRAW ORDER:
 * For correct transparency, objects must be drawn back-to-front (painter's
 * algorithm). Front-to-back doesn't work because further objects need to
 * show through closer transparent surfaces. This is why transparent objects
 * are typically sorted and drawn after all opaque geometry.
 */

#include "glide3x_state.h"

/*
 * grAlphaBlendFunction - Configure alpha blending factors
 *
 * From the 3dfx SDK:
 * "grAlphaBlendFunction() specifies the blend function used when alpha
 * blending is enabled. Alpha blending allows for effects like transparency,
 * particles, and anti-aliased edges."
 *
 * Parameters:
 *   rgb_sf   - Source factor for RGB channels
 *   rgb_df   - Destination factor for RGB channels
 *   alpha_sf - Source factor for alpha channel
 *   alpha_df - Destination factor for alpha channel
 *
 * All factors are GR_BLEND_* constants.
 *
 * Calling this function automatically enables alpha blending.
 * To disable, call with ONE/ZERO (opaque factors).
 *
 * Examples:
 *
 * Standard transparency:
 *   grAlphaBlendFunction(GR_BLEND_SRC_ALPHA, GR_BLEND_ONE_MINUS_SRC_ALPHA,
 *                        GR_BLEND_ZERO, GR_BLEND_ZERO);
 *
 * Additive blending:
 *   grAlphaBlendFunction(GR_BLEND_SRC_ALPHA, GR_BLEND_ONE,
 *                        GR_BLEND_ZERO, GR_BLEND_ZERO);
 *
 * Disable blending:
 *   grAlphaBlendFunction(GR_BLEND_ONE, GR_BLEND_ZERO,
 *                        GR_BLEND_ONE, GR_BLEND_ZERO);
 */
void __stdcall grAlphaBlendFunction(GrAlphaBlendFnc_t rgb_sf, GrAlphaBlendFnc_t rgb_df,
                          GrAlphaBlendFnc_t alpha_sf, GrAlphaBlendFnc_t alpha_df)
{
    if (!g_voodoo) return;

    /*
     * alphaMode register layout:
     *   Bit 0:     Alpha test enable
     *   Bits 1-3:  Alpha test function
     *   Bit 4:     Alpha blend enable
     *   Bits 8-11:  RGB source factor
     *   Bits 12-15: RGB destination factor
     *   Bits 16-19: Alpha source factor
     *   Bits 20-23: Alpha destination factor
     *   Bits 24-31: Alpha reference value
     */

    /* Start with existing register value to preserve alpha test settings */
    uint32_t val = g_voodoo->reg[alphaMode].u;

    /* Clear blend-related bits (4-23) but preserve:
     * - Bits 0-3: Alpha test (enable + function)
     * - Bits 24-31: Alpha reference value
     */
    val &= ~ALPHAMODE_BLEND_BITS_MASK;

    /* Pack blend factors */
    val |= (rgb_sf & 0xF) << ALPHAMODE_SRCRGBBLEND_SHIFT;
    val |= (rgb_df & 0xF) << ALPHAMODE_DSTRGBBLEND_SHIFT;
    val |= (alpha_sf & 0xF) << ALPHAMODE_SRCALPHABLEND_SHIFT;
    val |= (alpha_df & 0xF) << ALPHAMODE_DSTALPHABLEND_SHIFT;

    /* Enable alpha blending */
    val |= ALPHAMODE_ALPHABLEND_BIT;

    g_voodoo->reg[alphaMode].u = val;
}
