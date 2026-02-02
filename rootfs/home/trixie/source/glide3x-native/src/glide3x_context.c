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

    /* Suppress unused parameter warnings */
    (void)refresh;
    (void)colorFormat;
    (void)numColorBuffers;
    (void)numAuxBuffers;

    snprintf(dbg, sizeof(dbg),
             "glide3x: grSstWinOpen(hwnd=0x%x, res=%d, origin=%d)\n",
             (unsigned int)hwnd, resolution, origin);
    debug_log(dbg);

    /* Auto-initialize if app forgot to call grGlideInit */
    if (!g_initialized) {
        grGlideInit();
    }

    /* Return existing context if already open */
    if (g_context) {
        debug_log("glide3x: grSstWinOpen - context already open\n");
        return g_context;
    }

    /* Get resolution dimensions */
    get_resolution(resolution, &g_screen_width, &g_screen_height);
    snprintf(dbg, sizeof(dbg),
             "glide3x: grSstWinOpen - resolution %dx%d\n",
             g_screen_width, g_screen_height);
    debug_log(dbg);

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
    debug_log("glide3x: grSstWinOpen - init FBI\n");
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
     */
    debug_log("glide3x: grSstWinOpen - init TMUs\n");
    voodoo_init_tmu(&g_voodoo->tmu[0],
                    &g_voodoo->reg[textureMode],
                    2 * 1024 * 1024);
    voodoo_init_tmu(&g_voodoo->tmu[1],
                    &g_voodoo->reg[textureMode + 0x100/4],
                    2 * 1024 * 1024);
    voodoo_init_tmu_shared(&g_voodoo->tmushare);

    /* Initialize display output */
    debug_log("glide3x: grSstWinOpen - init display\n");
    if (!display_init(g_screen_width, g_screen_height, (HWND)hwnd)) {
        debug_log("glide3x: Failed to initialize display\n");
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
    g_voodoo->reg[fbzMode].u |= (1 << 14);

    g_voodoo->active = true;
    g_context = (GrContext_t)g_voodoo;

    debug_log("glide3x: grSstWinOpen complete\n");
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
    LOG_FUNC();

    if (context != g_context) {
        debug_log("glide3x: grSstWinClose - invalid context\n");
        return FXFALSE;
    }

    display_shutdown();
    g_context = NULL;

    debug_log("glide3x: grSstWinClose complete\n");
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
FxBool __stdcall grSelectContext(GrContext_t context)
{
    LOG_FUNC();

    if (context == g_context) {
        return FXTRUE;
    }

    debug_log("glide3x: grSelectContext - invalid context\n");
    return FXFALSE;
}
