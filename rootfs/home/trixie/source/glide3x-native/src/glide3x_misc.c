/*
 * glide3x_misc.c - Miscellaneous Glide functions
 *
 * This module implements various utility and configuration functions:
 *   - grClipWindow(): Set scissor rectangle
 *   - grDitherMode(): Configure dithering
 *   - grChromakeyMode(), grChromakeyValue(): Color key transparency
 *   - grSstOrigin(): Set Y coordinate origin
 *   - grCoordinateSpace(), grVertexLayout(): Vertex format configuration
 *   - grViewport(): Set viewport transformation
 *   - grEnable(), grDisable(): Feature toggle
 *   - grLoadGammaTable(), guGammaCorrectionRGB(): Gamma correction
 *   - grSstScreenWidth(), grSstScreenHeight(): Query screen dimensions
 *   - grFinish(), grFlush(): Synchronization
 *   - grErrorSetCallback(): Error handling
 *
 * CLIPPING:
 * grClipWindow() defines a scissor rectangle. Pixels outside this
 * rectangle are discarded regardless of other tests. This is useful for:
 *   - Split-screen multiplayer (each player gets a viewport)
 *   - UI panels that shouldn't be drawn over
 *   - Debug visualization of specific areas
 *
 * DITHERING:
 * Voodoo outputs 16-bit color (RGB565) but computes colors at higher
 * precision internally. Dithering adds small noise to reduce visible
 * color banding in gradients. Without dithering, gradients show
 * obvious "steps" between colors.
 *
 * Dither patterns:
 *   - 4x4: Better quality, more random appearance
 *   - 2x2: Simpler pattern, slightly more visible
 *
 * CHROMAKEY (COLOR KEY):
 * Chromakey discards pixels matching a specific color, creating
 * "punch-through" transparency. Unlike alpha, chromakey is binary:
 * exact match = transparent, otherwise opaque.
 *
 * Common uses:
 *   - Sprites with solid-color backgrounds (magenta = transparent)
 *   - Legacy content without alpha channels
 *   - Video overlay effects
 *
 * Limitation: Only exact matches work. Near-chromakey colors are visible.
 *
 * GAMMA CORRECTION:
 * CRT monitors have non-linear brightness response (gamma ~2.2).
 * Without correction, images appear too dark with washed-out contrast.
 * Gamma tables map linear color values to perceptually correct output.
 *
 * Voodoo stored a hardware gamma table that was applied during
 * display output. Our software implementation stores the table
 * but actual gamma application depends on the display system.
 */

#include "glide3x_state.h"
#include <math.h>

/*
 * grClipWindow - Set scissor rectangle
 *
 * From the 3dfx SDK:
 * "grClipWindow() defines a rectangular region outside of which pixels
 * are not drawn."
 *
 * Parameters:
 *   minx, miny - Upper-left corner (inclusive)
 *   maxx, maxy - Lower-right corner (exclusive)
 *
 * Coordinates are in screen space. The default clip window is the
 * full screen (0, 0, width, height).
 *
 * Note: grBufferClear() should ideally respect the clip window.
 * Our current implementation clears the entire buffer for simplicity.
 */
void __stdcall grClipWindow(FxU32 minx, FxU32 miny, FxU32 maxx, FxU32 maxy)
{
    LOG_FUNC();
    if (!g_voodoo) return;

    g_voodoo->clip_left = minx;
    g_voodoo->clip_right = maxx;
    g_voodoo->clip_top = miny;
    g_voodoo->clip_bottom = maxy;

    /* Store in hardware registers for rasterizer */
    g_voodoo->reg[clipLeftRight].u = (minx << 16) | maxx;
    g_voodoo->reg[clipLowYHighY].u = (miny << 16) | maxy;
}

/*
 * grDitherMode - Configure color dithering
 *
 * From the 3dfx SDK:
 * "grDitherMode() enables or disables dithering and selects the
 * dithering pattern."
 *
 * Parameters:
 *   mode - GR_DITHER_DISABLE: No dithering (banding visible)
 *          GR_DITHER_2x2:     2x2 ordered dither pattern
 *          GR_DITHER_4x4:     4x4 ordered dither pattern (better)
 *
 * Dithering trades spatial resolution for color resolution.
 * At low resolutions, the dither pattern may be visible.
 * At higher resolutions, it creates smooth gradients.
 */
void __stdcall grDitherMode(GrDitherMode_t mode)
{
    LOG_FUNC();
    if (!g_voodoo) return;

    uint32_t val = g_voodoo->reg[fbzMode].u;

    if (mode == GR_DITHER_DISABLE) {
        val &= ~(1 << 8);  /* Disable dithering */
    } else {
        val |= (1 << 8);   /* Enable dithering */
        if (mode == GR_DITHER_2x2) {
            val |= (1 << 11);  /* 2x2 pattern */
        } else {
            val &= ~(1 << 11); /* 4x4 pattern */
        }
    }

    g_voodoo->reg[fbzMode].u = val;
}

/*
 * grChromakeyMode - Enable/disable chromakey transparency
 *
 * Parameters:
 *   mode - GR_CHROMAKEY_DISABLE: Normal rendering
 *          GR_CHROMAKEY_ENABLE:  Discard pixels matching key color
 */
void __stdcall grChromakeyMode(GrChromakeyMode_t mode)
{
    LOG_FUNC();
    if (!g_voodoo) return;

    uint32_t val = g_voodoo->reg[fbzMode].u;
    if (mode == GR_CHROMAKEY_ENABLE) {
        val |= (1 << 1);
    } else {
        val &= ~(1 << 1);
    }
    g_voodoo->reg[fbzMode].u = val;
}

/*
 * grChromakeyValue - Set chromakey color
 *
 * Parameters:
 *   value - 32-bit ARGB color to treat as transparent
 *           Typically bright magenta (0xFFFF00FF) or similar
 */
void __stdcall grChromakeyValue(GrColor_t value)
{
    LOG_FUNC();
    if (!g_voodoo) return;
    g_voodoo->reg[chromaKey].u = value;
}

/*
 * grSstOrigin - Set Y coordinate origin
 *
 * Parameters:
 *   origin - GR_ORIGIN_UPPER_LEFT: Y=0 at top (DirectX style)
 *            GR_ORIGIN_LOWER_LEFT: Y=0 at bottom (OpenGL style)
 */
void __stdcall grSstOrigin(GrOriginLocation_t origin)
{
    LOG_FUNC();
    if (!g_voodoo) return;

    if (origin == GR_ORIGIN_LOWER_LEFT) {
        g_voodoo->fbi.yorigin = g_voodoo->fbi.height - 1;
        g_voodoo->reg[fbzMode].u |= (1 << 17);
    } else {
        g_voodoo->fbi.yorigin = 0;
        g_voodoo->reg[fbzMode].u &= ~(1 << 17);
    }
}

/*
 * grCoordinateSpace - Set coordinate space mode
 *
 * Glide 3.x introduced this for window vs normalized coordinates.
 * We always use window coordinates, so this is a no-op.
 */
void __stdcall grCoordinateSpace(GrCoordinateSpaceMode_t mode)
{
    LOG_FUNC();
    (void)mode;
}

/*
 * grVertexLayout - Configure vertex attribute layout
 *
 * Glide 3.x allowed flexible vertex formats via this function.
 * Our implementation uses a fixed GrVertex structure, so this is a no-op.
 */
void __stdcall grVertexLayout(FxU32 param, FxI32 offset, FxU32 mode)
{
    LOG_FUNC();
    (void)param;
    (void)offset;
    (void)mode;
}

/*
 * grViewport - Set viewport transformation
 *
 * The viewport maps normalized device coordinates to screen coordinates.
 * We also update the clip window to match.
 */
void __stdcall grViewport(FxI32 x, FxI32 y, FxI32 width, FxI32 height)
{
    LOG_FUNC();
    if (!g_voodoo) return;

    g_voodoo->vp_x = x;
    g_voodoo->vp_y = y;
    g_voodoo->vp_width = width;
    g_voodoo->vp_height = height;

    grClipWindow(x, y, x + width, y + height);
}

/*
 * grEnable / grDisable - Toggle features
 *
 * Glide 3.x feature toggles. Most are handled by other functions.
 */
void __stdcall grEnable(GrEnableMode_t mode)
{
    LOG_FUNC();
    (void)mode;
}

void __stdcall grDisable(GrEnableMode_t mode)
{
    LOG_FUNC();
    (void)mode;
}

/*
 * grLoadGammaTable - Load custom gamma correction table
 *
 * Parameters:
 *   nentries - Number of entries (typically 32 or 256)
 *   red, green, blue - Arrays of gamma-corrected values
 */
void __stdcall grLoadGammaTable(FxU32 nentries, FxU32 *red, FxU32 *green, FxU32 *blue)
{
    LOG("grLoadGammaTable(entries=%d)", nentries);
    if (!g_voodoo) return;

    if (nentries > 32) nentries = 32;

    for (uint32_t i = 0; i < nentries; i++) {
        uint32_t r = red[i] & 0xFF;
        uint32_t g = green[i] & 0xFF;
        uint32_t b = blue[i] & 0xFF;
        g_voodoo->gamma_table[i] = (r << 16) | (g << 8) | b;
    }
}

/*
 * guGammaCorrectionRGB - Generate and load gamma table from values
 *
 * Parameters:
 *   red, green, blue - Gamma exponents (1.0 = linear, 2.2 = typical CRT)
 */
void __stdcall guGammaCorrectionRGB(float red, float green, float blue)
{
    LOG("guGammaCorrectionRGB(%.2f, %.2f, %.2f)", red, green, blue);

    FxU32 r_table[32], g_table[32], b_table[32];

    for (int i = 0; i < 32; i++) {
        float i_f = (float)i / 31.0f;

        float r_val = (red > 0.0f) ? powf(i_f, 1.0f / red) : i_f;
        float g_val = (green > 0.0f) ? powf(i_f, 1.0f / green) : i_f;
        float b_val = (blue > 0.0f) ? powf(i_f, 1.0f / blue) : i_f;

        r_table[i] = (FxU32)(r_val * 255.0f);
        g_table[i] = (FxU32)(g_val * 255.0f);
        b_table[i] = (FxU32)(b_val * 255.0f);
    }

    grLoadGammaTable(32, r_table, g_table, b_table);
}

/*
 * grSstScreenWidth / grSstScreenHeight - Get screen dimensions
 */
float __stdcall grSstScreenWidth(void)
{
    LOG_FUNC();
    return (float)g_screen_width;
}

float __stdcall grSstScreenHeight(void)
{
    LOG_FUNC();
    return (float)g_screen_height;
}

/*
 * grFinish / grFlush - Synchronization
 *
 * On hardware, these would wait for pending operations to complete.
 * Our software renderer is synchronous, so these are no-ops.
 */
void __stdcall grFinish(void)
{
    LOG_FUNC();
}

void __stdcall grFlush(void)
{
    LOG_FUNC();
}

/*
 * grErrorSetCallback - Set error callback function
 *
 * We don't use callbacks for error reporting.
 */
void __stdcall grErrorSetCallback(void (*fnc)(const char *string, FxBool fatal))
{
    LOG_FUNC();
    (void)fnc;
}
