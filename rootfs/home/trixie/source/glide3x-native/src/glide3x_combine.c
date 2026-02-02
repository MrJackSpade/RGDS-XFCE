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
void __stdcall grColorCombine(GrCombineFunction_t function, GrCombineFactor_t factor,
                    GrCombineLocal_t local, GrCombineOther_t other, FxBool invert)
{
    LOG_FUNC();
    if (!g_voodoo) return;

    /* Build fbzColorPath register value */
    uint32_t val = g_voodoo->reg[fbzColorPath].u;

    /* Clear color combine bits (lower 17 bits) */
    val &= ~0x1FFFF;

    /*
     * Bit layout for color combine:
     *   Bits 0-1:  CC_RGBSELECT (other color source)
     *   Bits 2-3:  CC_ASELECT (factor source for scale operations)
     *   Bit 4:     CC_LOCALSELECT (0=iterated, 1=constant)
     *   Bits 5-7:  CC_MSELECT (multiply factor source)
     *   Bit 8:     CC_ZERO_OTHER (if set, other=0)
     *   Bit 9:     CC_SUB_CLOCAL (subtract local instead of add)
     *   Bit 10:    CC_ADD_ALOCAL (add local alpha)
     *   Bits 11-13: CC_ADD (additional add factor)
     *   Bit 14:    CC_ADD_ACLOCAL
     *   Bit 15:    CC_INVERT_OUTPUT_SELECT
     *   Bit 16:    CC_INVERT_OUTPUT
     */

    /* Set CC_RGBSELECT based on other source */
    val |= (other & 3);

    /* Set CC_ASELECT (factor for scaling) */
    val |= ((factor & 3) << 2);

    /* Set CC_LOCALSELECT (local color source) */
    val |= ((local & 1) << 4);

    /* Set function-specific bits */
    switch (function) {
    case GR_COMBINE_FUNCTION_ZERO:
        val |= (1 << 8);  /* CC_ZERO_OTHER - zero the output */
        break;
    case GR_COMBINE_FUNCTION_LOCAL:
        /* Output = local color (but we still need other for some modes) */
        break;
    case GR_COMBINE_FUNCTION_LOCAL_ALPHA:
        /* Output = local alpha replicated to RGB */
        val |= (1 << 4);  /* Use local alpha */
        break;
    case GR_COMBINE_FUNCTION_SCALE_OTHER:
        /* Output = other * factor */
        break;
    case GR_COMBINE_FUNCTION_SCALE_OTHER_ADD_LOCAL:
        /* Output = other * factor + local */
        break;
    case GR_COMBINE_FUNCTION_SCALE_OTHER_MINUS_LOCAL:
        /* Output = (other - local) * factor */
        val |= (1 << 9);  /* CC_SUB_CLOCAL */
        break;
    default:
        /* Other functions use default path */
        break;
    }

    /* Set invert bit */
    if (invert) {
        val |= (1 << 16);  /* CC_INVERT_OUTPUT */
    }

    g_voodoo->reg[fbzColorPath].u = val;
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
void __stdcall grAlphaCombine(GrCombineFunction_t function, GrCombineFactor_t factor,
                    GrCombineLocal_t local, GrCombineOther_t other, FxBool invert)
{
    LOG_FUNC();
    if (!g_voodoo) return;

    /* Build fbzColorPath register value (alpha portion) */
    uint32_t val = g_voodoo->reg[fbzColorPath].u;

    /*
     * Alpha combine bits start at bit 17:
     *   Bit 17:    CCA_ZERO_OTHER
     *   Bit 18:    CCA_SUB_CLOCAL
     *   Bits 19-20: CCA_LOCALSELECT
     *   Bits 21-23: CCA_MSELECT
     *   Bit 24:    CCA_INVERT_OUTPUT
     *   Bit 25:    CCA_ADD_ALOCAL
     */

    /* Clear alpha combine bits */
    val &= ~(0x1FF << 17);

    /* Set function-specific bits */
    if (function == GR_COMBINE_FUNCTION_ZERO) {
        val |= (1 << 17);  /* CCA_ZERO_OTHER */
    }

    if (invert) {
        val |= (1 << 25);  /* CCA_INVERT_OUTPUT */
    }

    /* These parameters affect the combine in more complex ways */
    (void)factor;
    (void)local;
    (void)other;

    g_voodoo->reg[fbzColorPath].u = val;
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
void __stdcall grConstantColorValue(GrColor_t value)
{
    LOG_FUNC();
    g_constant_color = value;
    if (g_voodoo) {
        g_voodoo->reg[color0].u = value;
    }
}
