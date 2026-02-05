/*
 * glide3x_combine.c - Color and Alpha combine unit configuration
 *
 * This module implements the Voodoo color combine unit configuration:
 *   - grColorCombine(): Configure how final pixel color is computed
 *   - grAlphaCombine(): Configure how final pixel alpha is computed
 *   - grConstantColorValue(): Set the constant color register
 *
 * THE COLOR COMBINE UNIT:
 * The color combine unit is the Voodoo's equivalent of a modern pixel shader,
 * though far more limited. It determines how the final pixel color is computed
 * from various input sources.
 *
 * The combine equation (simplified) is:
 *   output = (A - B) * C + D
 *
 * Where A, B, C, D can be configured to various sources:
 *   - Zero
 *   - Local color (vertex color or constant color)
 *   - Other color (texture color or upstream TMU output)
 *   - Local alpha / Other alpha / Texture alpha
 *
 * COMBINE SOURCES:
 *
 * LOCAL COLOR:
 *   - ITERATED: The vertex color, interpolated across the triangle.
 *               This is the per-vertex RGBA specified by the application.
 *   - CONSTANT: The color set by grConstantColorValue().
 *               Useful for applying a fixed tint or for flat shading.
 *
 * OTHER COLOR:
 *   - ITERATED: Same as local iterated (vertex color).
 *   - TEXTURE:  The color sampled from the texture map.
 *   - CONSTANT: Same as local constant.
 *
 * COMMON CONFIGURATIONS:
 *
 * 1. VERTEX COLOR ONLY (no texture):
 *    function=LOCAL, local=ITERATED, other=ignored
 *    Result: output = vertex_color
 *    Use: Flat-shaded geometry, debug visualization
 *
 * 2. TEXTURE ONLY (decal mode):
 *    function=OTHER, local=ignored, other=TEXTURE
 *    Result: output = texture_color
 *    Use: UI elements, billboards, unlit textures
 *
 * 3. MODULATED TEXTURE (texture * vertex color):
 *    function=SCALE_OTHER, factor=LOCAL, local=ITERATED, other=TEXTURE
 *    Result: output = texture_color * vertex_color
 *    Use: Standard lit textured geometry. Vertex color carries lighting,
 *         texture carries detail. This is the most common mode.
 *
 * 4. CONSTANT COLORED TEXTURE:
 *    function=SCALE_OTHER, factor=LOCAL, local=CONSTANT, other=TEXTURE
 *    Result: output = texture_color * constant_color
 *    Use: Tinting textures (e.g., team colors, damage flash)
 *
 * 5. BLEND TEXTURE WITH VERTEX:
 *    function=SCALE_OTHER_ADD_LOCAL, factor=TEXTURE_ALPHA, ...
 *    Result: output = texture * texture_alpha + vertex * (1 - texture_alpha)
 *    Use: Decals that fade into underlying surface
 *
 * ALPHA COMBINE:
 * The alpha combine unit works identically but only for the alpha channel.
 * It can use different settings than the RGB combine, allowing effects like:
 *   - RGB from texture, alpha from vertex (for per-vertex transparency)
 *   - RGB from vertex, alpha from texture (for texture-masked sprites)
 *
 * REGISTER MAPPING:
 * Our implementation stores combine settings in the fbzColorPath register:
 *   Bits 0-1:   CC_RGBSELECT (other color source)
 *   Bits 2-3:   CC_ASELECT (factor source)
 *   Bit 4:      CC_LOCALSELECT (local color source)
 *   Bits 5-7:   CC_MSELECT (function selection)
 *   Bit 8:      CC_ZERO_OTHER
 *   Bit 16:     CC_INVERT_OUTPUT
 *   Bits 17+:   Alpha combine settings (CCA_*)
 */

#include "glide3x_state.h"

/*
 * grColorCombine - Configure the color combine unit
 *
 * From the 3dfx SDK:
 * "grColorCombine() configures the color combine unit in the FBI which
 * determines how the final pixel color is computed from the texture color,
 * iterated vertex color, and constant color."
 *
 * Parameters:
 *   function - The combine operation (GR_COMBINE_FUNCTION_*):
 *              ZERO:                   output = 0
 *              LOCAL:                  output = local_color
 *              LOCAL_ALPHA:            output = local_alpha (replicated to RGB)
 *              SCALE_OTHER:            output = other_color * factor
 *              SCALE_OTHER_ADD_LOCAL:  output = other * factor + local
 *              SCALE_OTHER_ADD_LOCAL_ALPHA: output = other * factor + local_alpha
 *              SCALE_OTHER_MINUS_LOCAL: output = (other - local) * factor
 *              BLEND_OTHER:            output = other * factor + other * (1-factor)
 *              (More complex functions available on later hardware)
 *
 *   factor - Scale factor source (GR_COMBINE_FACTOR_*):
 *            ZERO:              0.0
 *            ONE:               1.0
 *            LOCAL:             local_color (RGB as factor)
 *            OTHER_ALPHA:       alpha from other source
 *            LOCAL_ALPHA:       alpha from local source
 *            TEXTURE_ALPHA:     alpha from texture
 *            TEXTURE_RGB:       RGB from texture (rare)
 *            DETAIL_FACTOR:     LOD-based detail factor
 *            ONE_MINUS_* variants for (1 - value)
 *
 *   local - Local color source (GR_COMBINE_LOCAL_*):
 *           ITERATED:  Interpolated vertex color
 *           CONSTANT:  grConstantColorValue() color
 *           (DEPTH on later hardware)
 *
 *   other - Other color source (GR_COMBINE_OTHER_*):
 *           ITERATED:  Interpolated vertex color
 *           TEXTURE:   Texture map output
 *           CONSTANT:  Constant color
 *           (NONE means use local only)
 *
 *   invert - If FXTRUE, final output is inverted: output = 1 - output
 *
 * Example for modulated texture (texture * vertex):
 *   grColorCombine(GR_COMBINE_FUNCTION_SCALE_OTHER,
 *                  GR_COMBINE_FACTOR_LOCAL,
 *                  GR_COMBINE_LOCAL_ITERATED,
 *                  GR_COMBINE_OTHER_TEXTURE,
 *                  FXFALSE);
 *
 * Example for decal texture (texture only):
 *   grColorCombine(GR_COMBINE_FUNCTION_LOCAL,
 *                  GR_COMBINE_FACTOR_NONE,
 *                  GR_COMBINE_LOCAL_NONE,
 *                  GR_COMBINE_OTHER_TEXTURE,
 *                  FXFALSE);
 *   Note: "LOCAL" function with "OTHER" source means use other directly.
 */
/* Track color combine calls for debugging */
static int g_colorcombine_count = 0;

void __stdcall grColorCombine(GrCombineFunction_t function, GrCombineFactor_t factor,
                    GrCombineLocal_t local, GrCombineOther_t other, FxBool invert)
{
    g_colorcombine_count++;

    /* ALWAYS log - critical for debugging rendering issues */
    DEBUG_VERBOSE("grColorCombine #%d: func=%d, factor=%d, local=%d, other=%d, invert=%d\n",
                  g_colorcombine_count, function, factor, local, other, invert);

    if (!g_voodoo) {
        DEBUG_VERBOSE("grColorCombine: returning VOID\n");
        return;
    }

    /* Build fbzColorPath register value
     *
     * Register bit layout (per h3defs.h / voodoo_defs.h):
     *   Bits 0-1:   CC_RGBSELECT      - other color source (iterated, texture, color1)
     *   Bit 4:      CC_LOCALSELECT    - local color source (iterated or color0)
     *   Bit 8:      CC_ZERO_OTHER     - zero the other input
     *   Bit 9:      CC_SUB_CLOCAL     - subtract c_local
     *   Bits 10-12: CC_MSELECT        - blend factor select (from factor parameter)
     *   Bit 13:     CC_REVERSE_BLEND  - invert blend factor (1-factor)
     *   Bit 14:     CC_ADD_CLOCAL     - add c_local to result
     *   Bit 15:     CC_ADD_ALOCAL     - add a_local to result (as RGB)
     *   Bit 16:     CC_INVERT_OUTPUT  - invert final output
     */
    uint32_t val = g_voodoo->reg[fbzColorPath].u;

    /* Clear color combine bits (0-16).
     * Preserve alpha combine bits (17-26), texture enable (27), and other high bits.
     * Both grColorCombine and grAlphaCombine can set TEXTURE_ENABLE (bit 27).
     */
    val &= ~FBZCP_CC_BITS_MASK;

    /* CC_RGBSELECT (bits 0-1): other color source */
    val |= ((other & 0x3) << FBZCP_CC_RGBSELECT_SHIFT);

    /*
     * Set TEXTURE_ENABLE (bit 27) if color combine requires texture.
     * Per 3dfx SDK: cc_requires_texture when:
     *   - other == GR_COMBINE_OTHER_TEXTURE
     *   - factor == GR_COMBINE_FACTOR_TEXTURE_ALPHA
     *   - factor == GR_COMBINE_FACTOR_TEXTURE_RGB
     */
    if (other == GR_COMBINE_OTHER_TEXTURE ||
        (factor & 0x7) == GR_COMBINE_FACTOR_TEXTURE_ALPHA ||
        (factor & 0x7) == GR_COMBINE_FACTOR_TEXTURE_RGB) {
        val |= FBZCP_TEXTURE_ENABLE_BIT;
    }

    /* CC_LOCALSELECT (bit 4): local color source */
    val |= ((local & 0x1) << FBZCP_CC_LOCALSELECT_SHIFT);

    /* Handle reverse blend based on factor
     * Factor values 0x0-0x7 are base factors
     * Factor values 0x8-0xF are "one minus" versions
     * When using base factors (bit 3 = 0), set REVERSE_BLEND
     * When using "one minus" factors (bit 3 = 1), don't set REVERSE_BLEND
     */
    if ((factor & 0x8) == 0) {
        val |= FBZCP_CC_REVERSE_BLEND_BIT;
    }

    /* CC_MSELECT (bits 10-12): blend factor source (strip high bit used for reverse) */
    val |= ((factor & 0x7) << FBZCP_CC_MSELECT_SHIFT);

    /* CC_INVERT_OUTPUT (bit 16) */
    if (invert) {
        val |= FBZCP_CC_INVERT_OUTPUT_BIT;
    }

    /* Set bits based on combine function
     * The color combine equation is:
     *   output = (CC_ZERO_OTHER ? 0 : other) * factor
     *          - (CC_SUB_CLOCAL ? local : 0)
     *          + (CC_ADD_CLOCAL ? local : 0)
     *          + (CC_ADD_ALOCAL ? local.a : 0)
     */
    switch (function) {
    case GR_COMBINE_FUNCTION_ZERO:
        /* output = 0 */
        val |= FBZCP_CC_ZERO_OTHER_BIT;
        break;

    case GR_COMBINE_FUNCTION_LOCAL:
        /* output = local */
        val |= FBZCP_CC_ZERO_OTHER_BIT;
        val |= FBZCP_CC_ADD_CLOCAL_BIT;
        break;

    case GR_COMBINE_FUNCTION_LOCAL_ALPHA:
        /* output = local.alpha (broadcast to RGB) */
        val |= FBZCP_CC_ZERO_OTHER_BIT;
        val |= FBZCP_CC_ADD_ALOCAL_BIT;
        break;

    case GR_COMBINE_FUNCTION_SCALE_OTHER:
        /* output = other * factor */
        /* No additional bits needed */
        break;

    case GR_COMBINE_FUNCTION_SCALE_OTHER_ADD_LOCAL:
        /* output = other * factor + local */
        val |= FBZCP_CC_ADD_CLOCAL_BIT;
        break;

    case GR_COMBINE_FUNCTION_SCALE_OTHER_ADD_LOCAL_ALPHA:
        /* output = other * factor + local.alpha */
        val |= FBZCP_CC_ADD_ALOCAL_BIT;
        break;

    case GR_COMBINE_FUNCTION_SCALE_OTHER_MINUS_LOCAL:
        /* output = other * factor - local */
        val |= FBZCP_CC_SUB_CLOCAL_BIT;
        break;

    case GR_COMBINE_FUNCTION_SCALE_OTHER_MINUS_LOCAL_ADD_LOCAL:
        /* output = (other - local) * factor + local = lerp(local, other, factor) */
        val |= FBZCP_CC_SUB_CLOCAL_BIT;
        val |= FBZCP_CC_ADD_CLOCAL_BIT;
        break;

    case GR_COMBINE_FUNCTION_SCALE_OTHER_MINUS_LOCAL_ADD_LOCAL_ALPHA:
        /* output = (other - local) * factor + local.alpha */
        val |= FBZCP_CC_SUB_CLOCAL_BIT;
        val |= FBZCP_CC_ADD_ALOCAL_BIT;
        break;

    case GR_COMBINE_FUNCTION_SCALE_MINUS_LOCAL_ADD_LOCAL:
        /* output = -local * factor + local = local * (1 - factor) */
        val |= FBZCP_CC_ZERO_OTHER_BIT;
        val |= FBZCP_CC_SUB_CLOCAL_BIT;
        val |= FBZCP_CC_ADD_CLOCAL_BIT;
        break;

    case GR_COMBINE_FUNCTION_SCALE_MINUS_LOCAL_ADD_LOCAL_ALPHA:
        /* output = -local * factor + local.alpha */
        val |= FBZCP_CC_ZERO_OTHER_BIT;
        val |= FBZCP_CC_SUB_CLOCAL_BIT;
        val |= FBZCP_CC_ADD_ALOCAL_BIT;
        break;
    }

    g_voodoo->reg[fbzColorPath].u = val;
    DEBUG_VERBOSE("grColorCombine: returning VOID\n");
}

/*
 * grAlphaCombine - Configure the alpha combine unit
 *
 * From the 3dfx SDK:
 * "grAlphaCombine() configures the alpha combine unit. This unit determines
 * how the alpha component of the pixel color is computed."
 *
 * The alpha combine unit works identically to the color combine unit,
 * but only affects the alpha channel. This allows separate control
 * of color and transparency.
 *
 * Parameters: Same as grColorCombine(), but operate on alpha values.
 *
 * Common configurations:
 *
 * 1. VERTEX ALPHA:
 *    function=LOCAL, local=ITERATED
 *    Result: alpha = vertex_alpha
 *    Use: Per-vertex transparency
 *
 * 2. TEXTURE ALPHA:
 *    function=LOCAL, local=TEXTURE (via other)
 *    Result: alpha = texture_alpha
 *    Use: Sprites with alpha masks
 *
 * 3. CONSTANT ALPHA:
 *    function=LOCAL, local=CONSTANT
 *    Result: alpha = constant_alpha
 *    Use: Uniform transparency (fade effects)
 *
 * 4. MODULATED ALPHA (texture * vertex):
 *    function=SCALE_OTHER, factor=LOCAL, local=ITERATED, other=TEXTURE
 *    Result: alpha = texture_alpha * vertex_alpha
 *    Use: Sprites that can be faded per-vertex
 *
 * Note: Alpha combine result feeds into:
 *   - Alpha test (conditional pixel discard)
 *   - Alpha blend (mixing with framebuffer)
 */
static int g_alphacombine_count = 0;

void __stdcall grAlphaCombine(GrCombineFunction_t function, GrCombineFactor_t factor,
                    GrCombineLocal_t local, GrCombineOther_t other, FxBool invert)
{
    g_alphacombine_count++;
    DEBUG_VERBOSE("grAlphaCombine #%d: func=%d, factor=%d, local=%d, other=%d, invert=%d\n",
                  g_alphacombine_count, function, factor, local, other, invert);

    if (!g_voodoo) {
        DEBUG_VERBOSE("grAlphaCombine: returning VOID\n");
        return;
    }

    /* Build fbzColorPath register value (alpha portion)
     *
     * Register bit layout for alpha combine:
     *   Bits 2-3:   ASELECT          - other alpha source (iterated, texture, c1)
     *   Bits 5-6:   ALOCALSELECT     - local alpha source (iterated, c0, z, w)
     *   Bit 17:     CCA_ZERO_OTHER   - zero the other input
     *   Bit 18:     CCA_SUB_CLOCAL   - subtract local alpha
     *   Bits 19-21: CCA_MSELECT      - blend factor select
     *   Bit 22:     CCA_REVERSE_BLEND - invert blend factor (1-factor)
     *   Bit 23:     CCA_ADD_CLOCAL   - add local (unused for alpha, CCA_ADD_ALOCAL used instead)
     *   Bit 24:     CCA_ADD_ALOCAL   - add local alpha to result
     *   Bit 25:     CCA_INVERT_OUTPUT - invert final output
     */
    uint32_t val = g_voodoo->reg[fbzColorPath].u;

    /* Clear alpha combine bits:
     * - ASELECT (bits 2-3)
     * - ALOCALSELECT (bits 5-6)
     * - CCA bits (bits 17-25)
     * Note: We preserve bit 27 (TEXTURE_ENABLE) - grColorCombine handles it,
     * but we can also set it if alpha combine requires texture.
     */
    val &= ~FBZCP_CCA_BITS_MASK;

    /* ASELECT (bits 2-3): other alpha source */
    val |= ((other & 0x3) << FBZCP_CC_ASELECT_SHIFT);

    /*
     * Set TEXTURE_ENABLE (bit 27) if alpha combine requires texture.
     * This complements grColorCombine - either function can enable texturing.
     * Per 3dfx SDK: ac_requires_texture when:
     *   - other == GR_COMBINE_OTHER_TEXTURE
     *   - factor == GR_COMBINE_FACTOR_TEXTURE_ALPHA
     */
    if (other == GR_COMBINE_OTHER_TEXTURE ||
        (factor & 0x7) == GR_COMBINE_FACTOR_TEXTURE_ALPHA) {
        val |= FBZCP_TEXTURE_ENABLE_BIT;
    }

    /* ALOCALSELECT (bits 5-6): local alpha source */
    val |= ((local & 0x3) << FBZCP_CCA_LOCALSELECT_SHIFT);

    /* Handle reverse blend based on factor
     * Factor values 0x0-0x7 are base factors
     * Factor values 0x8-0xF are "one minus" versions
     * When using base factors (bit 3 = 0), set REVERSE_BLEND
     */
    if ((factor & 0x8) == 0) {
        val |= FBZCP_CCA_REVERSE_BLEND_BIT;
    }

    /* CCA_MSELECT (bits 19-21): blend factor source */
    val |= ((factor & 0x7) << FBZCP_CCA_MSELECT_SHIFT);

    /* CCA_INVERT_OUTPUT (bit 25) */
    if (invert) {
        val |= FBZCP_CCA_INVERT_OUTPUT_BIT;
    }

    /* Set bits based on combine function
     * The alpha combine equation mirrors color combine:
     *   output = (CCA_ZERO_OTHER ? 0 : other) * factor
     *          - (CCA_SUB_CLOCAL ? local : 0)
     *          + (CCA_ADD_ALOCAL ? local : 0)
     */
    switch (function) {
    case GR_COMBINE_FUNCTION_ZERO:
        /* output = 0 */
        val |= FBZCP_CCA_ZERO_OTHER_BIT;
        break;

    case GR_COMBINE_FUNCTION_LOCAL:
    case GR_COMBINE_FUNCTION_LOCAL_ALPHA:
        /* output = local (for alpha, LOCAL and LOCAL_ALPHA are equivalent) */
        val |= FBZCP_CCA_ZERO_OTHER_BIT;
        val |= FBZCP_CCA_ADD_ALOCAL_BIT;
        break;

    case GR_COMBINE_FUNCTION_SCALE_OTHER:
        /* output = other * factor */
        /* No additional bits needed */
        break;

    case GR_COMBINE_FUNCTION_SCALE_OTHER_ADD_LOCAL:
    case GR_COMBINE_FUNCTION_SCALE_OTHER_ADD_LOCAL_ALPHA:
        /* output = other * factor + local */
        val |= FBZCP_CCA_ADD_ALOCAL_BIT;
        break;

    case GR_COMBINE_FUNCTION_SCALE_OTHER_MINUS_LOCAL:
        /* output = other * factor - local */
        val |= FBZCP_CCA_SUB_CLOCAL_BIT;
        break;

    case GR_COMBINE_FUNCTION_SCALE_OTHER_MINUS_LOCAL_ADD_LOCAL:
    case GR_COMBINE_FUNCTION_SCALE_OTHER_MINUS_LOCAL_ADD_LOCAL_ALPHA:
        /* output = (other - local) * factor + local */
        val |= FBZCP_CCA_SUB_CLOCAL_BIT;
        val |= FBZCP_CCA_ADD_ALOCAL_BIT;
        break;

    case GR_COMBINE_FUNCTION_SCALE_MINUS_LOCAL_ADD_LOCAL:
    case GR_COMBINE_FUNCTION_SCALE_MINUS_LOCAL_ADD_LOCAL_ALPHA:
        /* output = -local * factor + local */
        val |= FBZCP_CCA_ZERO_OTHER_BIT;
        val |= FBZCP_CCA_SUB_CLOCAL_BIT;
        val |= FBZCP_CCA_ADD_ALOCAL_BIT;
        break;
    }

    g_voodoo->reg[fbzColorPath].u = val;
    DEBUG_VERBOSE("grAlphaCombine: returning VOID\n");
}

/*
 * grConstantColorValue - Set the constant color register
 *
 * From the 3dfx SDK:
 * "grConstantColorValue() sets the constant color that can be used
 * by the color and alpha combine units."
 *
 * Parameters:
 *   value - 32-bit ARGB color value (0xAARRGGBB format)
 *           A = Alpha (bits 24-31)
 *           R = Red   (bits 16-23)
 *           G = Green (bits 8-15)
 *           B = Blue  (bits 0-7)
 *
 * The constant color is used when:
 *   - grColorCombine local = GR_COMBINE_LOCAL_CONSTANT
 *   - grColorCombine other = GR_COMBINE_OTHER_CONSTANT
 *   - grAlphaCombine local = GR_COMBINE_LOCAL_CONSTANT
 *
 * Common uses:
 *   1. FLAT SHADING:
 *      Set color combine to LOCAL=CONSTANT, set constant to flat color.
 *      All pixels drawn with the same color.
 *
 *   2. TINTING:
 *      Set color combine to SCALE_OTHER, factor=CONSTANT.
 *      Multiply texture by constant to apply tint.
 *
 *   3. FADE EFFECTS:
 *      Set alpha combine to CONSTANT, vary constant over time.
 *      Objects fade in/out uniformly.
 *
 *   4. TEAM COLORS:
 *      Modulate grayscale textures with team color constant.
 *      Same texture, different colors.
 *
 * Note: The constant color is global state. It persists until changed.
 * Games often set it once during initialization and rely on vertex colors
 * for most variation.
 */
static int g_constantcolor_count = 0;

void __stdcall grConstantColorValue(GrColor_t value)
{
    g_constantcolor_count++;
    DEBUG_VERBOSE("grConstantColorValue #%d: value=0x%08X\n", g_constantcolor_count, value);
    
    g_constant_color = value;
    if (g_voodoo) {
        g_voodoo->reg[color0].u = value;
    }
    DEBUG_VERBOSE("grConstantColorValue: returning VOID\n");
}
