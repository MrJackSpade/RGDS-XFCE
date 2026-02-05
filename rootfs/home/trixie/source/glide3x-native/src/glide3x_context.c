/*
 * glide3x_context.c - Glide rendering context management
 *
 * This module handles creation and destruction of Glide rendering contexts:
 *   - grSstWinOpen(): Create rendering context (the main initialization)
 *   - grSstWinClose(): Destroy rendering context
 *   - grSelectContext(): Switch between contexts (multi-board support)
 *
 * CONTEXT CONCEPT:
 * A Glide "context" represents an active rendering surface. On original
 * hardware, each Voodoo board was a separate context. grSstWinOpen()
 * created the context, which involved:
 *   - Switching the display from VGA pass-through to Voodoo output
 *   - Allocating framebuffer memory for color and depth buffers
 *   - Setting the video timing for the requested resolution
 *
 * Our software implementation creates:
 *   - A Windows window or DirectDraw surface for display
 *   - Software framebuffers in the Voodoo emulator
 *   - Default rendering state
 *
 * SINGLE VS MULTI-CONTEXT:
 * Original Glide could support multiple Voodoo boards (for SLI or
 * separate rendering targets). We only support one context, so:
 *   - grSelectContext() is essentially a no-op
 *   - Multiple grSstWinOpen() calls return the existing context
 */

#include "glide3x_state.h"

/*
 * grSstWinOpen - Open a graphics context (rendering window/surface)
 *
 * From the 3dfx SDK:
 * "grSstWinOpen opens a graphics context on the specified hardware.
 * It allocates the required framebuffer and auxiliary buffer memory,
 * and configures the display mode."
 *
 * Parameters:
 *   hwnd - Window handle (HWND on Windows)
 *          Voodoo 1/2 ignored this (full-screen only).
 *          Voodoo Banshee+ used it for windowed mode.
 *          We use it for our display window parent.
 *
 *   resolution - Screen resolution (GR_RESOLUTION_*)
 *                Determines framebuffer dimensions.
 *
 *   refresh - Refresh rate (GR_REFRESH_*)
 *             Ignored in our implementation (OS controls this).
 *
 *   colorFormat - Color component order (GR_COLORFORMAT_ARGB/ABGR)
 *                 Affects how GrColor_t values are interpreted.
 *
 *   origin - Y-axis orientation
 *            GR_ORIGIN_UPPER_LEFT: Y=0 at top (DirectX style)
 *            GR_ORIGIN_LOWER_LEFT: Y=0 at bottom (OpenGL style)
 *
 *   numColorBuffers - Number of color buffers (2=double, 3=triple)
 *                     Affects framebuffer memory allocation.
 *
 *   numAuxBuffers - Number of auxiliary buffers (0 or 1)
 *                   Aux buffer stores depth (Z/W) values.
 *
 * Returns:
 *   Non-zero context handle on success, NULL on failure.
 *
 * Our implementation:
 *   - Allocates 4MB framebuffer RAM (matching Voodoo 2 spec)
 *   - Allocates 2MB per TMU for texture memory
 *   - Creates display window via display_init()
 *   - Sets up default rendering state
 */
GrContext_t __stdcall grSstWinOpen(
    FxU32 hwnd,
    GrScreenResolution_t resolution,
    GrScreenRefresh_t refresh,
    GrColorFormat_t colorFormat,
    GrOriginLocation_t origin,
    int numColorBuffers,
    int numAuxBuffers)
{
    char dbg[128];

    DEBUG_VERBOSE("=== grSstWinOpen CALLED ===\n");
    DEBUG_VERBOSE("  hwnd=%p, resolution=%d, refresh=%d\n", (void*)(uintptr_t)hwnd, resolution, refresh);
    DEBUG_VERBOSE("  colorFormat=%d, origin=%d\n", colorFormat, origin);
    DEBUG_VERBOSE("  numColorBuffers=%d, numAuxBuffers=%d\n", numColorBuffers, numAuxBuffers);
    DEBUG_VERBOSE("  g_context=%p, g_initialized=%d\n", g_context, g_initialized);

    /* Suppress unused parameter warnings */
    (void)refresh;
    (void)numColorBuffers;
    (void)numAuxBuffers;

    /* Store color format for palette interpretation */
    g_color_format = colorFormat;

    /* Auto-initialize if app forgot to call grGlideInit */
    if (!g_initialized) {
        DEBUG_VERBOSE("  Auto-initializing Glide (was not initialized)\n");
        grGlideInit();
    }

    /* Return existing context if already open */
    if (g_context) {
        DEBUG_VERBOSE("  Returning existing context %p\n", g_context);
        DEBUG_VERBOSE("grSstWinOpen: returning %p\n", g_context);
        return g_context;
    }

    /* Get resolution dimensions */
    get_resolution(resolution, &g_screen_width, &g_screen_height);

    /* Track 640x480 switches - enable logging after second switch */
    if (g_screen_width == 640 && g_screen_height == 480) {
        g_640x480_switch_count++;
        if (g_640x480_switch_count >= 2 && !g_logging_enabled) {
            g_logging_enabled = 1;
        }
    }

    DEBUG_VERBOSE("  Resolved to %dx%d\n", g_screen_width, g_screen_height);

    /*
     * Initialize FBI (Frame Buffer Interface)
     *
     * The FBI handles:
     * - Color buffer storage and swapping
     * - Depth buffer storage
     * - Color combine (mixing texture and vertex colors)
     * - Alpha blending
     * - Dithering
     *
     * We allocate 4MB of RAM, typical for Voodoo 2.
     * Buffer layout:
     *   Offset 0: Front color buffer
     *   Offset W*H*2: Back color buffer
     *   Offset W*H*4: Triple buffer (if enabled)
     *   Offset W*H*6: Depth buffer
     */

    /* Check if FBI needs full initialization or just dimension update */
    int fbi_was_initialized = (g_voodoo->fbi.ram != NULL &&
                               g_voodoo->fbi.width == g_screen_width &&
                               g_voodoo->fbi.height == g_screen_height);

    DEBUG_VERBOSE("  FBI state: ram=%p, width=%d, height=%d\n",
                  g_voodoo->fbi.ram, g_voodoo->fbi.width, g_voodoo->fbi.height);
    DEBUG_VERBOSE("  FBI preservation check: %s\n", fbi_was_initialized ? "PRESERVING" : "REINITIALIZING");

    if (fbi_was_initialized) {
        /* Skip reinitialization to preserve framebuffer content */
        DEBUG_VERBOSE("  Skipping FBI reinit (same dimensions)\n");
    } else {
        voodoo_init_fbi(&g_voodoo->fbi, 4 * 1024 * 1024);
        g_voodoo->fbi.width = g_screen_width;
        g_voodoo->fbi.height = g_screen_height;
        g_voodoo->fbi.rowpixels = g_screen_width;

        /* Calculate buffer offsets (16-bit = 2 bytes per pixel) */
        int buffer_size = g_screen_width * g_screen_height * 2;
        g_voodoo->fbi.rgboffs[0] = 0;                    /* Front buffer */
        g_voodoo->fbi.rgboffs[1] = buffer_size;          /* Back buffer */
        g_voodoo->fbi.rgboffs[2] = buffer_size * 2;      /* Triple buffer */
        g_voodoo->fbi.auxoffs = buffer_size * 3;         /* Depth buffer */

        g_voodoo->fbi.frontbuf = 0;
        g_voodoo->fbi.backbuf = 1;
    }

    /*
     * Set Y origin
     *
     * UPPER_LEFT: Y increases downward (DirectX convention)
     * LOWER_LEFT: Y increases upward (OpenGL convention)
     *
     * The yorigin value is subtracted from Y coordinates during rendering
     * to flip the coordinate system when needed.
     */
    if (origin == GR_ORIGIN_LOWER_LEFT) {
        g_voodoo->fbi.yorigin = g_screen_height - 1;
    } else {
        g_voodoo->fbi.yorigin = 0;
    }

    /*
     * Initialize TMUs (Texture Mapping Units)
     *
     * Each TMU has:
     * - Dedicated texture RAM (2MB each in our case)
     * - Texture coordinate iterators
     * - Filtering and LOD logic
     * - Texture combine settings
     *
     * TMU0 is closest to the framebuffer, TMU1 feeds into TMU0.
     * For multi-texture effects, the TMUs are chained together.
     *
     * IMPORTANT: Only initialize TMUs if not already set up, to preserve
     * texture data across grSstWinOpen calls (games may upload textures
     * before calling grSstWinOpen again).
     */
    if (g_voodoo->tmu[0].ram == NULL) {
        /* tmushare must be initialized first - TMU init references its lookup tables */
        voodoo_init_tmu_shared(&g_voodoo->tmushare);
        /* TMU register base addresses per DOSBox/hardware spec.
         * This allows t->reg[textureMode] etc. to access the correct absolute register */
        voodoo_init_tmu(g_voodoo,
                        &g_voodoo->tmu[0],
                        &g_voodoo->reg[TMU0_REG_BASE],
                        2 * 1024 * 1024);
        /* Enable TMU0 in chipmask per DOSBox voodoo.cpp line 7253 */
        g_voodoo->chipmask |= 0x02;

        voodoo_init_tmu(g_voodoo,
                        &g_voodoo->tmu[1],
                        &g_voodoo->reg[TMU1_REG_BASE],
                        2 * 1024 * 1024);
        /* Enable TMU1 in chipmask per DOSBox voodoo.cpp line 7257 */
        g_voodoo->chipmask |= 0x04;
    } else {
        /* Ensure chipmask is still set even if TMUs were preserved */
        g_voodoo->chipmask |= 0x06;  /* TMU0 + TMU1 */
    }

    /* Initialize vertex layout to default (disabled) */
    g_voodoo->vl_xy_offset = -1;
    g_voodoo->vl_z_offset = -1;
    g_voodoo->vl_w_offset = -1;
    g_voodoo->vl_q_offset = -1;
    g_voodoo->vl_a_offset = -1;
    g_voodoo->vl_rgb_offset = -1;
    g_voodoo->vl_pargb_offset = -1;
    g_voodoo->vl_st0_offset = -1;
    g_voodoo->vl_st1_offset = -1;
    g_voodoo->vl_q0_offset = -1;
    g_voodoo->vl_q1_offset = -1;

    /* Initialize display output */
    if (!display_init(g_screen_width, g_screen_height, (HWND)hwnd)) {
        DEBUG_VERBOSE("grSstWinOpen: returning NULL (display_init failed)\n");
        return NULL;
    }

    /*
     * Set up default rendering state
     */

    /* Clip rectangle: full screen */
    g_voodoo->clip_left = 0;
    g_voodoo->clip_right = g_screen_width;
    g_voodoo->clip_top = 0;
    g_voodoo->clip_bottom = g_screen_height;

    /* Viewport: full screen */
    g_voodoo->vp_x = 0;
    g_voodoo->vp_y = 0;
    g_voodoo->vp_width = g_screen_width;
    g_voodoo->vp_height = g_screen_height;

    /* Culling: disabled by default */
    g_voodoo->cull_mode = GR_CULL_DISABLE;

    /* Buffer masks: color enabled, alpha disabled, depth enabled */
    g_voodoo->alpha_mask = false;
    g_voodoo->depth_mask = true;

    /* Default render target: back buffer */
    g_render_buffer = 1;

    /*
     * Initialize fbzMode with correct default state:
     *   Enable clipping = enabled (required for Y clipping in rasterizer)
     *   RGB buffer mask = enabled (writes enabled)
     *   Aux buffer mask = enabled (writes enabled, since depth_mask = true)
     *   Draw buffer = 1 (back buffer)
     *
     * Note: FBZMODE_ENABLE_CLIPPING_BIT corresponds to SST_ENRECTCLIP in the
     * 3dfx SDK, which is enabled by default during grSstWinOpen(). Without this,
     * triangles with negative Y coordinates cause buffer underruns.
     */
    g_voodoo->reg[fbzMode].u |= FBZMODE_ENABLE_CLIPPING_BIT |
                                FBZMODE_RGB_BUFFER_MASK_BIT |
                                FBZMODE_AUX_BUFFER_MASK_BIT |
                                (1 << FBZMODE_DRAW_BUFFER_SHIFT);

    /*
     * Initialize clip rectangle to full screen.
     * The SDK calls grClipWindow(0, 0, width, height) during grSstWinOpen().
     * Without this, clipping is enabled but the clip rect is 0,0 -> 0,0,
     * causing all pixels to be clipped (black screen).
     */
    g_voodoo->reg[clipLeftRight].u = (0 << 16) | g_voodoo->fbi.width;
    g_voodoo->reg[clipLowYHighY].u = (0 << 16) | g_voodoo->fbi.height;

    g_voodoo->active = true;
    g_context = (GrContext_t)g_voodoo;

    DEBUG_VERBOSE("=== grSstWinOpen SUCCESS ===\n");
    DEBUG_VERBOSE("  Returning context %p, active=%d\n", g_context, g_voodoo->active);
    DEBUG_VERBOSE("  FBI: frontbuf=%d, backbuf=%d\n", g_voodoo->fbi.frontbuf, g_voodoo->fbi.backbuf);
    DEBUG_VERBOSE("  Offsets: rgb[0]=%d, rgb[1]=%d, aux=%d\n",
                  g_voodoo->fbi.rgboffs[0], g_voodoo->fbi.rgboffs[1], g_voodoo->fbi.auxoffs);

    DEBUG_VERBOSE("grSstWinOpen: returning %p\n", g_context);
    return g_context;
}

/*
 * grSstWinClose - Close a graphics context
 *
 * From the 3dfx SDK:
 * "grSstWinClose() closes the specified graphics context and releases
 * any associated resources."
 *
 * Parameters:
 *   context - The context to close (from grSstWinOpen)
 *
 * Returns:
 *   FXTRUE on success, FXFALSE on failure.
 *
 * On real hardware, this would:
 * - Switch back to VGA pass-through mode
 * - Release framebuffer memory
 * - Restore original video mode
 */
FxBool __stdcall grSstWinClose(GrContext_t context)
{
    DEBUG_VERBOSE("=== grSstWinClose CALLED ===\n");
    DEBUG_VERBOSE("  context=%p, g_context=%p\n", context, g_context);

    if (context != g_context) {
        DEBUG_VERBOSE("  ERROR: context mismatch, returning FXFALSE\n");
        return FXFALSE;
    }

    DEBUG_VERBOSE("  Calling display_shutdown()\n");
    display_shutdown();
    g_context = NULL;
    DEBUG_VERBOSE("  g_context set to NULL, returning FXTRUE\n");

    return FXTRUE;
}

/*
 * grSelectContext - Switch to a different rendering context
 *
 * From the 3dfx SDK:
 * "grSelectContext() makes the specified context the current context
 * for subsequent Glide calls."
 *
 * Parameters:
 *   context - The context to make current
 *
 * Returns:
 *   FXTRUE if the context was successfully selected.
 *
 * This was used in multi-board configurations where an application
 * might render to multiple Voodoo cards. Since we only support one
 * context, this is essentially a validation check.
 */
static int g_selectcontext_count = 0;

FxBool __stdcall grSelectContext(GrContext_t context)
{
    g_selectcontext_count++;

    FxBool result = (context == g_context) ? FXTRUE : FXFALSE;

    /* ALWAYS log - critical for debugging rendering issues */
    DEBUG_VERBOSE("grSelectContext #%d: context=%p, g_context=%p, result=%d\n",
                  g_selectcontext_count, context, g_context, result);

    return result;
}
