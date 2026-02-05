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
static int g_clipwindow_count = 0;

void __stdcall grClipWindow(FxU32 minx, FxU32 miny, FxU32 maxx, FxU32 maxy)
{
    g_clipwindow_count++;
    DEBUG_VERBOSE("grClipWindow #%d: (%d,%d)-(%d,%d) [%dx%d]\n",
                  g_clipwindow_count, minx, miny, maxx, maxy,
                  maxx - minx, maxy - miny);

    if (!g_voodoo) {
        DEBUG_VERBOSE("grClipWindow: returning VOID\n");
        return;
    }

    g_voodoo->clip_left = minx;
    g_voodoo->clip_right = maxx;
    g_voodoo->clip_top = miny;
    g_voodoo->clip_bottom = maxy;

    /* Store in hardware registers for rasterizer */
    g_voodoo->reg[clipLeftRight].u = (minx << 16) | maxx;
    g_voodoo->reg[clipLowYHighY].u = (miny << 16) | maxy;
    DEBUG_VERBOSE("grClipWindow: returning VOID\n");
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
static int g_dithermode_count = 0;
void __stdcall grDitherMode(GrDitherMode_t mode)
{
    g_dithermode_count++;
    DEBUG_VERBOSE("grDitherMode #%d: mode=%d\n", g_dithermode_count, mode);
    if (!g_voodoo) {
        DEBUG_VERBOSE("grDitherMode: returning VOID\n");
        return;
    }

    uint32_t val = g_voodoo->reg[fbzMode].u;

    if (mode == GR_DITHER_DISABLE) {
        val &= ~FBZMODE_ENABLE_DITHERING_BIT;
    } else {
        val |= FBZMODE_ENABLE_DITHERING_BIT;
        if (mode == GR_DITHER_2x2) {
            val |= FBZMODE_DITHER_TYPE_BIT;  /* 2x2 pattern */
        } else {
            val &= ~FBZMODE_DITHER_TYPE_BIT; /* 4x4 pattern */
        }
    }

    g_voodoo->reg[fbzMode].u = val;
    DEBUG_VERBOSE("grDitherMode: returning VOID\n");
}

/*
 * grChromakeyMode - Enable/disable chromakey transparency
 *
 * Parameters:
 *   mode - GR_CHROMAKEY_DISABLE: Normal rendering
 *          GR_CHROMAKEY_ENABLE:  Discard pixels matching key color
 */
static int g_chromakeymode_count = 0;
void __stdcall grChromakeyMode(GrChromakeyMode_t mode)
{
    g_chromakeymode_count++;
    DEBUG_VERBOSE("grChromakeyMode #%d: mode=%d\n", g_chromakeymode_count, mode);
    if (!g_voodoo) {
        DEBUG_VERBOSE("grChromakeyMode: returning VOID\n");
        return;
    }

    uint32_t val = g_voodoo->reg[fbzMode].u;
    if (mode == GR_CHROMAKEY_ENABLE) {
        val |= FBZMODE_ENABLE_CHROMAKEY_BIT;
    } else {
        val &= ~FBZMODE_ENABLE_CHROMAKEY_BIT;
    }
    g_voodoo->reg[fbzMode].u = val;
    DEBUG_VERBOSE("grChromakeyMode: returning VOID\n");
}

/*
 * grChromakeyValue - Set chromakey color
 *
 * Parameters:
 *   value - 32-bit color to treat as transparent (in game's color format)
 *
 * The SDK's _grSwizzleColor converts from game's color format to internal ARGB.
 * Palette data is already in ARGB format (not converted), so chromakey must
 * also be in ARGB to match during the comparison.
 */
static int g_chromakeyvalue_count = 0;
void __stdcall grChromakeyValue(GrColor_t value)
{
    g_chromakeyvalue_count++;
    DEBUG_VERBOSE("grChromakeyValue #%d: value=0x%08X\n", g_chromakeyvalue_count, value);
    if (!g_voodoo) {
        DEBUG_VERBOSE("grChromakeyValue: returning VOID\n");
        return;
    }

    /* Convert from game's color format to internal ARGB format
     * (matches SDK's _grSwizzleColor function in diglide.c) */
    uint32_t argb;
    switch (g_color_format) {
    case GR_COLORFORMAT_ARGB:  /* 0xAARRGGBB - already in ARGB */
        argb = value;
        break;
    case GR_COLORFORMAT_ABGR:  /* 0xAABBGGRR - swap R and B */
        {
            uint32_t r = value & 0x00ff;
            uint32_t b = (value >> 16) & 0xff;
            argb = (value & 0xff00ff00) | (r << 16) | b;
        }
        break;
    case GR_COLORFORMAT_RGBA:  /* 0xRRGGBBAA - rotate to ARGB */
        {
            uint32_t b = (value & 0x0000ff00) >> 8;
            uint32_t g = (value & 0x00ff0000) >> 16;
            uint32_t r = (value & 0xff000000) >> 24;
            uint32_t a = (value & 0x000000ff);
            argb = (a << 24) | (r << 16) | (g << 8) | b;
        }
        break;
    case GR_COLORFORMAT_BGRA:  /* 0xBBGGRRAA - rotate and swap to ARGB */
        {
            uint32_t b = (value & 0xff000000) >> 24;
            uint32_t g = (value & 0x00ff0000) >> 16;
            uint32_t r = (value & 0x0000ff00) >> 8;
            uint32_t a = (value & 0x000000ff);
            argb = (a << 24) | (r << 16) | (g << 8) | b;
        }
        break;
    default:
        argb = value;
        break;
    }

    g_voodoo->reg[chromaKey].u = argb;
    DEBUG_VERBOSE("grChromakeyValue: returning VOID\n");
}

/*
 * grSstOrigin - Set Y coordinate origin
 *
 * Parameters:
 *   origin - GR_ORIGIN_UPPER_LEFT: Y=0 at top (DirectX style)
 *            GR_ORIGIN_LOWER_LEFT: Y=0 at bottom (OpenGL style)
 */
static int g_sstorigin_count = 0;

void __stdcall grSstOrigin(GrOriginLocation_t origin)
{
    g_sstorigin_count++;
    DEBUG_VERBOSE("grSstOrigin #%d: origin=%d (%s)\n",
                  g_sstorigin_count, origin,
                  origin == GR_ORIGIN_LOWER_LEFT ? "LOWER_LEFT" : "UPPER_LEFT");

    if (!g_voodoo) {
        DEBUG_VERBOSE("grSstOrigin: returning VOID\n");
        return;
    }

    if (origin == GR_ORIGIN_LOWER_LEFT) {
        g_voodoo->fbi.yorigin = g_voodoo->fbi.height - 1;
        g_voodoo->reg[fbzMode].u |= FBZMODE_Y_ORIGIN_BIT;
    } else {
        g_voodoo->fbi.yorigin = 0;
        g_voodoo->reg[fbzMode].u &= ~FBZMODE_Y_ORIGIN_BIT;
    }
    DEBUG_VERBOSE("grSstOrigin: returning VOID\n");
}

/*
 * grCoordinateSpace - Set coordinate space mode
 *
 * Glide 3.x introduced this for window vs normalized coordinates.
 * We always use window coordinates, so this is a no-op.
 */
static int g_coordinatespace_count = 0;
void __stdcall grCoordinateSpace(GrCoordinateSpaceMode_t mode)
{
    g_coordinatespace_count++;
    DEBUG_VERBOSE("grCoordinateSpace #%d: mode=%d\n", g_coordinatespace_count, mode);
    (void)mode;
    DEBUG_VERBOSE("grCoordinateSpace: returning VOID\n");
}

/*
 * grVertexLayout - Configure vertex attribute layout
 *
 * Glide 3.x allowed flexible vertex formats via this function.
 * Games call this to specify where each attribute (x, y, z, color, texcoords)
 * is located within their vertex structure.
 *
 * GR_PARAM values:
 *   1 = XY, 2 = Z, 3 = W, 4 = Q, 16 = A, 32 = RGB, 48 = PARGB
 *   64 = ST0, 65 = ST1, 80 = Q0, 81 = Q1
 */
static int g_vertexlayout_count = 0;

void __stdcall grVertexLayout(FxU32 param, FxI32 offset, FxU32 mode)
{
    g_vertexlayout_count++;

    const char *param_name = "UNKNOWN";
    switch (param) {
    case 1:  param_name = "XY"; break;
    case 2:  param_name = "Z"; break;
    case 3:  param_name = "W"; break;
    case 4:  param_name = "Q"; break;
    case 16: param_name = "A"; break;
    case 32: param_name = "RGB"; break;
    case 48: param_name = "PARGB"; break;
    case 64: param_name = "ST0"; break;
    case 65: param_name = "ST1"; break;
    case 80: param_name = "Q0"; break;
    case 81: param_name = "Q1"; break;
    }

    DEBUG_VERBOSE("grVertexLayout #%d: param=%d(%s), offset=%d, mode=%d\n",
                  g_vertexlayout_count, param, param_name, offset, mode);

    if (!g_voodoo) {
        DEBUG_VERBOSE("grVertexLayout: returning VOID\n");
        return;
    }

    /* mode=0 disables, mode=1 enables */
    int32_t off = (mode == 1) ? offset : -1;

    switch (param) {
    case 1:  g_voodoo->vl_xy_offset = off; break;    /* GR_PARAM_XY */
    case 2:  g_voodoo->vl_z_offset = off; break;     /* GR_PARAM_Z */
    case 3:  g_voodoo->vl_w_offset = off; break;     /* GR_PARAM_W */
    case 4:  g_voodoo->vl_q_offset = off; break;     /* GR_PARAM_Q */
    case 16: g_voodoo->vl_a_offset = off; break;     /* GR_PARAM_A */
    case 32: g_voodoo->vl_rgb_offset = off; break;   /* GR_PARAM_RGB */
    case 48: g_voodoo->vl_pargb_offset = off; break; /* GR_PARAM_PARGB */
    case 64: g_voodoo->vl_st0_offset = off; break;   /* GR_PARAM_ST0 */
    case 65: g_voodoo->vl_st1_offset = off; break;   /* GR_PARAM_ST1 */
    case 80: g_voodoo->vl_q0_offset = off; break;    /* GR_PARAM_Q0 */
    case 81: g_voodoo->vl_q1_offset = off; break;    /* GR_PARAM_Q1 */
    }
    DEBUG_VERBOSE("grVertexLayout: returning VOID\n");
}

/*
 * grViewport - Set viewport transformation
 *
 * The viewport maps normalized device coordinates to screen coordinates.
 * We also update the clip window to match.
 */
static int g_viewport_count = 0;
void __stdcall grViewport(FxI32 x, FxI32 y, FxI32 width, FxI32 height)
{
    g_viewport_count++;
    DEBUG_VERBOSE("grViewport #%d: x=%d, y=%d, w=%d, h=%d\n", g_viewport_count, x, y, width, height);
    if (!g_voodoo) {
        DEBUG_VERBOSE("grViewport: returning VOID\n");
        return;
    }

    g_voodoo->vp_x = x;
    g_voodoo->vp_y = y;
    g_voodoo->vp_width = width;
    g_voodoo->vp_height = height;

    grClipWindow(x, y, x + width, y + height);
    DEBUG_VERBOSE("grViewport: returning VOID\n");
}

/*
 * grEnable / grDisable - Toggle features
 *
 * Glide 3.x feature toggles. Most are handled by other functions.
 */
static int g_enable_count = 0;
void __stdcall grEnable(GrEnableMode_t mode)
{
    g_enable_count++;
    DEBUG_VERBOSE("grEnable #%d: mode=%d\n", g_enable_count, mode);
    (void)mode;
    DEBUG_VERBOSE("grEnable: returning VOID\n");
}

static int g_disable_count = 0;
void __stdcall grDisable(GrEnableMode_t mode)
{
    g_disable_count++;
    DEBUG_VERBOSE("grDisable #%d: mode=%d\n", g_disable_count, mode);
    (void)mode;
    DEBUG_VERBOSE("grDisable: returning VOID\n");
}

/*
 * grLoadGammaTable - Load custom gamma correction table
 *
 * Parameters:
 *   nentries - Number of entries (typically 32 or 256)
 *   red, green, blue - Arrays of gamma-corrected values
 */
static int g_loadgammatable_count = 0;
void __stdcall grLoadGammaTable(FxU32 nentries, FxU32 *red, FxU32 *green, FxU32 *blue)
{
    g_loadgammatable_count++;
    DEBUG_VERBOSE("grLoadGammaTable #%d: nentries=%d\n", g_loadgammatable_count, nentries);
    if (!g_voodoo) {
        DEBUG_VERBOSE("grLoadGammaTable: returning VOID\n");
        return;
    }

    if (nentries > 32) nentries = 32;

    for (uint32_t i = 0; i < nentries; i++) {
        uint32_t r = red[i] & 0xFF;
        uint32_t g = green[i] & 0xFF;
        uint32_t b = blue[i] & 0xFF;
        g_voodoo->gamma_table[i] = (r << 16) | (g << 8) | b;
    }
    DEBUG_VERBOSE("grLoadGammaTable: returning VOID\n");
}

/*
 * guGammaCorrectionRGB - Generate and load gamma table from values
 *
 * Parameters:
 *   red, green, blue - Gamma exponents (1.0 = linear, 2.2 = typical CRT)
 */
static int g_gugammacorrectionrgb_count = 0;
void __stdcall guGammaCorrectionRGB(float red, float green, float blue)
{
    g_gugammacorrectionrgb_count++;
    DEBUG_VERBOSE("guGammaCorrectionRGB #%d: r=%f, g=%f, b=%f\n", g_gugammacorrectionrgb_count, red, green, blue);
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
    DEBUG_VERBOSE("guGammaCorrectionRGB: returning VOID\n");
}

/*
 * grSstScreenWidth / grSstScreenHeight - Get screen dimensions
 */
static int g_screenwidth_count = 0;
static int g_screenheight_count = 0;

float __stdcall grSstScreenWidth(void)
{
    g_screenwidth_count++;
    DEBUG_VERBOSE("grSstScreenWidth #%d: returning %d\n",
                  g_screenwidth_count, g_screen_width);
    DEBUG_VERBOSE("grSstScreenWidth: returning %d\n", g_screen_width);
    return (float)g_screen_width;
}

float __stdcall grSstScreenHeight(void)
{
    g_screenheight_count++;
    DEBUG_VERBOSE("grSstScreenHeight #%d: returning %d\n",
                  g_screenheight_count, g_screen_height);
    DEBUG_VERBOSE("grSstScreenHeight: returning %d\n", g_screen_height);
    return (float)g_screen_height;
}

/*
 * grFinish / grFlush - Synchronization
 *
 * On hardware, these would wait for pending operations to complete.
 * Our software renderer is synchronous, so these are no-ops.
 */
static int g_finish_count = 0;
static int g_flush_count = 0;

void __stdcall grFinish(void)
{
    g_finish_count++;
    DEBUG_VERBOSE("grFinish #%d\n", g_finish_count);
    DEBUG_VERBOSE("grFinish: returning VOID\n");
}

void __stdcall grFlush(void)
{
    g_flush_count++;
    DEBUG_VERBOSE("grFlush #%d\n", g_flush_count);
    DEBUG_VERBOSE("grFlush: returning VOID\n");
}

/*
 * grErrorSetCallback - Set error callback function
 *
 * We don't use callbacks for error reporting.
 */
static int g_errorsetcallback_count = 0;
void __stdcall grErrorSetCallback(void (*fnc)(const char *string, FxBool fatal))
{
    g_errorsetcallback_count++;
    DEBUG_VERBOSE("grErrorSetCallback #%d: fnc=%p\n", g_errorsetcallback_count, fnc);
    (void)fnc;
    DEBUG_VERBOSE("grErrorSetCallback: returning VOID\n");
}

/*
 * grSstIdle - Wait for graphics subsystem to become idle
 *
 * On hardware, this waits for all pending rendering commands to complete.
 * Our software renderer is synchronous, so this is a no-op.
 */
static int g_sstidle_count = 0;

void __stdcall grSstIdle(void)
{
    g_sstidle_count++;
    DEBUG_VERBOSE("grSstIdle #%d\n", g_sstidle_count);
    /* Software renderer is always idle after each operation */
    DEBUG_VERBOSE("grSstIdle: returning VOID\n");
}

/*
 * grSstStatus - Get graphics subsystem status
 *
 * Returns status bits indicating:
 *   Bit 6: FBI graphics engine busy (0 = idle)
 *   Bit 7: TMU graphics engine busy (0 = idle)
 *   Bit 8: SST-1 TREX busy (0 = idle)
 *
 * Our software renderer is always idle, so return 0.
 */
static int g_sststatus_count = 0;

FxU32 __stdcall grSstStatus(void)
{
    g_sststatus_count++;
    DEBUG_VERBOSE("grSstStatus #%d: returning 0 (idle)\n", g_sststatus_count);
    DEBUG_VERBOSE("grSstStatus: returning 0\n");
    return 0;  /* Always idle */
}

/*
 * grBufferNumPending - Get number of pending buffer swaps
 *
 * Returns the number of buffer swap operations that have been requested
 * but not yet completed. Used for frame pacing and to determine if
 * the application should wait before submitting more work.
 *
 * Our implementation doesn't queue swaps, so always return 0.
 */
static int g_buffernumpending_count = 0;

FxI32 __stdcall grBufferNumPending(void)
{
    g_buffernumpending_count++;
    DEBUG_VERBOSE("grBufferNumPending #%d: returning 0\n", g_buffernumpending_count);
    DEBUG_VERBOSE("grBufferNumPending: returning 0\n");
    return 0;  /* No pending swaps */
}
