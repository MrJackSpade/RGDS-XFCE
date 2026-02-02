/*
 * glide3x_fog.c - Fog (atmospheric effects)
 *
 * This module implements fog configuration:
 *   - grFogMode(): Enable/disable and configure fog source
 *   - grFogColorValue(): Set the fog color
 *   - grFogTable(): Download fog density table
 *
 * FOG CONCEPT:
 * Fog simulates atmospheric light scattering, causing distant objects to
 * fade toward a fog color. This serves multiple purposes:
 *
 *   1. DEPTH PERCEPTION: Fog provides a visual cue for distance.
 *   2. HIDE DRAW DISTANCE: Fog hides the far clip plane where geometry
 *      suddenly pops into view.
 *   3. ATMOSPHERE: Creates mood (misty morning, dusty desert, etc.)
 *   4. VISUAL INTEREST: Adds depth and layering to scenes.
 *
 * THE FOG EQUATION:
 * For each pixel, the final color is blended between the computed color
 * and the fog color based on a fog factor (0-255):
 *
 *   final_color = lerp(pixel_color, fog_color, fog_factor / 255)
 *
 * Where:
 *   fog_factor = 0:   No fog (pixel_color unchanged)
 *   fog_factor = 255: Full fog (fog_color completely replaces)
 *   fog_factor = 128: 50% blend
 *
 * FOG TABLE:
 * Voodoo uses a 64-entry lookup table that maps a depth-derived index
 * to a fog factor (0-255). This allows any fog density curve:
 *
 *   - LINEAR: Fog increases linearly with distance
 *   - EXPONENTIAL: Fog follows e^(-d), more realistic
 *   - EXPONENTIAL SQUARED: Fog follows e^(-d^2), denser near camera
 *   - CUSTOM: Any artist-designed curve
 *
 * FOG SOURCES (grFogMode):
 *
 *   DISABLE: No fog applied
 *
 *   WITH_TABLE_ON_W: Index fog table using 1/W
 *     Most common mode. W represents eye-space depth (before perspective
 *     divide), so fog varies with actual distance. Automatic.
 *
 *   WITH_TABLE_ON_FOGCOORD_EXT: Index using explicit fog coordinate
 *     Application provides per-vertex fog values. More control but
 *     requires extra vertex data and interpolation.
 *
 *   WITH_ITERATED_Z: Index using interpolated Z value
 *     Uses the Z coordinate (post-perspective). Less accurate than W
 *     for perspective projections.
 *
 *   WITH_ITERATED_ALPHA_EXT: Use vertex alpha as fog factor directly
 *     Special effect mode. Vertex alpha controls fog, not a table lookup.
 *     Used for per-vertex fog without a fog coordinate attribute.
 *
 * FOG TABLE INDEX CALCULATION:
 * The 64-entry table is indexed by depth, scaled to 0-63. The scaling
 * depends on the fog mode and near/far fog distances. For W-based fog:
 *
 *   index = clamp((1/W - fog_start) / (fog_end - fog_start) * 63, 0, 63)
 *
 * The guFogTableIndexToW() utility function helps compute appropriate
 * index values when filling the fog table.
 *
 * COMMON FOG PATTERNS:
 *
 * 1. LINEAR FOG:
 *    for(i=0; i<64; i++) table[i] = i * 4;  // 0 to 252
 *    Fog increases linearly from none at i=0 to nearly full at i=63.
 *
 * 2. EXPONENTIAL FOG (realistic):
 *    for(i=0; i<64; i++) {
 *        float d = (float)i / 63.0f;  // normalized distance
 *        table[i] = (GrFog_t)(255.0f * (1.0f - expf(-density * d)));
 *    }
 *    Fog follows natural light extinction curve.
 *
 * 3. EXPONENTIAL SQUARED (thick fog):
 *    for(i=0; i<64; i++) {
 *        float d = (float)i / 63.0f;
 *        table[i] = (GrFog_t)(255.0f * (1.0f - expf(-powf(density * d, 2))));
 *    }
 *    Fog is heavier near the camera, drops off more gradually.
 *
 * 4. STEP FOG (wall of fog):
 *    for(i=0; i<32; i++) table[i] = 0;    // Clear near
 *    for(i=32; i<64; i++) table[i] = 255;  // Full fog far
 *    Abrupt fog wall at a specific distance.
 *
 * UTILITY FUNCTIONS (not Glide API):
 * guFogGenerateExp(), guFogGenerateExp2(), guFogGenerateLinear()
 * These fill the fog table with common curves. They're "gu" (Glide utility)
 * functions, not core Glide API.
 *
 * PERFORMANCE:
 * Fog is applied per-pixel and is essentially free on Voodoo hardware
 * (parallel with other pixel operations). Our software implementation
 * adds some per-pixel cost but maintains the same behavior.
 */

#include "glide3x_state.h"

/*
 * grFogMode - Enable/disable and configure fog
 *
 * From the 3dfx SDK:
 * "grFogMode() enables table-based fog and specifies how the fog table
 * index is derived."
 *
 * Parameters:
 *   mode - Fog mode (GR_FOG_*):
 *          DISABLE:               No fog
 *          WITH_TABLE_ON_W:       Index table by 1/W (perspective depth)
 *          WITH_TABLE_ON_FOGCOORD_EXT: Index by explicit fog coordinate
 *          WITH_ITERATED_Z:       Index by Z value
 *          WITH_ITERATED_ALPHA_EXT: Use vertex alpha as fog factor
 *          ADD2, MULT2:           Voodoo 2 extended modes
 *
 * The fog table (set by grFogTable()) maps the fog index to a blend
 * factor. The most common mode is WITH_TABLE_ON_W for automatic
 * distance-based fog.
 */
void __stdcall grFogMode(GrFogMode_t mode)
{
    LOG_FUNC();
    if (!g_voodoo) return;

    /*
     * fogMode register:
     *   Bit 0:   Enable fog
     *   Bit 1:   Fog add (vs blend)
     *   Bit 2:   Fog multiply
     *   Bits 3-4: Fog source (0=W, 1=Z, 2=alpha, 3=fogcoord)
     */
    g_voodoo->reg[fogMode].u = mode;
}

/*
 * grFogColorValue - Set the fog color
 *
 * From the 3dfx SDK:
 * "grFogColorValue() sets the color that pixels are fogged toward."
 *
 * Parameters:
 *   fogcolor - 32-bit ARGB fog color (0xAARRGGBB)
 *              Alpha component is typically ignored.
 *
 * Common choices:
 *   - Gray (0x808080): General purpose outdoor fog
 *   - White (0xFFFFFF): Bright mist, snow
 *   - Blue-gray (0x8090A0): Atmospheric haze
 *   - Black (0x000000): Darkness, void
 *   - Match sky: For seamless horizon blending
 *
 * The fog color should generally match or complement the sky/background
 * to avoid obvious color discontinuities at the fog boundary.
 */
void __stdcall grFogColorValue(GrColor_t fogcolor)
{
    LOG_FUNC();
    if (!g_voodoo) return;
    g_voodoo->reg[fogColor].u = fogcolor;
}

/*
 * grFogTable - Download fog density table
 *
 * From the 3dfx SDK:
 * "grFogTable() downloads a 64-entry table that maps depth indices to
 * fog blend factors."
 *
 * Parameters:
 *   ft - Array of 64 GrFog_t (uint8_t) values
 *        Entry 0:  Fog factor at nearest depth (usually 0)
 *        Entry 63: Fog factor at farthest depth (usually 255)
 *        Each value is 0-255:
 *          0 = no fog (pixel color unchanged)
 *          255 = full fog (fog color replaces pixel)
 *
 * The table is indexed by the selected fog source (W, Z, fogcoord, etc.)
 * normalized to the range 0-63.
 *
 * Example: Linear fog from 0% to 100%
 *   GrFog_t table[64];
 *   for (int i = 0; i < 64; i++) {
 *       table[i] = (GrFog_t)(i * 4);  // 0, 4, 8, ... 252
 *   }
 *   grFogTable(table);
 *
 * Example: Exponential fog with density 2.0
 *   for (int i = 0; i < 64; i++) {
 *       float d = (float)i / 63.0f;
 *       float fog = 1.0f - expf(-2.0f * d);
 *       table[i] = (GrFog_t)(fog * 255.0f);
 *   }
 *   grFogTable(table);
 */
void __stdcall grFogTable(const GrFog_t ft[])
{
    LOG_FUNC();
    if (!g_voodoo || !ft) return;

    /* Copy all 64 entries */
    for (int i = 0; i < 64; i++) {
        g_voodoo->fbi.fogblend[i] = ft[i];
    }
}
