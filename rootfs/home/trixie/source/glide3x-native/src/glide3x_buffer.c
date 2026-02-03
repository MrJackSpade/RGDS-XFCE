/*
 * glide3x_buffer.c - Framebuffer operations
 *
 * This module handles framebuffer management:
 *   - grBufferClear(): Clear color and depth buffers
 *   - grBufferSwap(): Swap front/back buffers (page flip)
 *   - grRenderBuffer(): Select which buffer to render to
 *
 * DOUBLE BUFFERING:
 * The Voodoo uses double (or triple) buffering for smooth animation:
 *   1. BACK BUFFER: Application renders here (not visible)
 *   2. FRONT BUFFER: Currently displayed on screen
 *
 * The typical frame loop is:
 *   1. grBufferClear() - Clear back buffer
 *   2. grDrawTriangle() ... - Render scene to back buffer
 *   3. grBufferSwap() - Make back buffer visible, swap roles
 *
 * BUFFER MEMORY LAYOUT (16-bit color):
 *   Each buffer is width * height * 2 bytes
 *   Front: offset 0
 *   Back:  offset buffer_size
 *   Aux:   offset buffer_size * 3 (depth/alpha storage)
 */

#include "glide3x_state.h"

/* Diagnostic counter from voodoo_emu.c */
extern int diag_pixel_count;

/*
 * grBufferClear - Clear color and depth buffers
 *
 * From the 3dfx SDK:
 * "grBufferClear clears the buffers indicated by the current grColorMask
 * and grDepthMask settings. All enabled buffers are cleared to the
 * specified values."
 *
 * Parameters:
 *   color - 32-bit ARGB color to fill the color buffer
 *           Converted to RGB565 for our 16-bit framebuffer.
 *           Format: 0xAARRGGBB
 *
 *   alpha - Alpha value for auxiliary buffer alpha storage
 *           Most configurations store depth, not alpha, so this
 *           is often ignored.
 *
 *   depth - Depth value to fill the depth buffer
 *           32-bit value; upper 16 bits used for 16-bit depth buffer.
 *           For Z-buffering: 0x0000 = near, 0xFFFF = far
 *           For W-buffering: 0x0000 = far, 0xFFFF = near (inverted!)
 *
 * Performance note:
 * On real Voodoo hardware, buffer clears used a "fastfill" mode that
 * could clear memory extremely quickly via dedicated hardware. Our
 * software implementation iterates pixel by pixel, which is much slower.
 *
 * Clipping note:
 * The SDK states clears are constrained by grClipWindow(). Our current
 * implementation clears the entire buffer for simplicity.
 */
void __stdcall grBufferClear(GrColor_t color, GrAlpha_t alpha, FxU32 depth)
{
    g_clear_count++;
    {
        char dbg[128];
        snprintf(dbg, sizeof(dbg),
                 "glide3x: grBufferClear #%d (color=0x%08X, alpha=%u, depth=0x%08X)\n",
                 g_clear_count, color, alpha, depth);
        debug_log(dbg);
    }

    if (!g_voodoo || !g_voodoo->active) return;

    (void)alpha;  /* Alpha stored in aux buffer if enabled */

    uint16_t *dest;
    uint16_t *depthbuf;
    int x, y;

    /*
     * Check write masks - respect grColorMask and grDepthMask settings
     *
     * From the 3dfx SDK: "grBufferClear clears the buffers indicated by
     * the current grColorMask and grDepthMask settings."
     *
     * FBZMODE_RGB_BUFFER_MASK: bit 9, 1 = RGB writes enabled
     * FBZMODE_AUX_BUFFER_MASK: bit 10, 1 = depth/alpha writes enabled
     */
    uint32_t fbzmode = g_voodoo->reg[fbzMode].u;
    int doColor = FBZMODE_RGB_BUFFER_MASK(fbzmode);
    int doDepth = FBZMODE_AUX_BUFFER_MASK(fbzmode);

    /* Early return if nothing to clear */
    if (!doColor && !doDepth) {
        debug_log("glide3x: grBufferClear skipped (both masks disabled)\n");
        return;
    }

    /* Get target color buffer based on current render buffer setting */
    if (g_render_buffer == 0) {
        /* Front buffer */
        dest = (uint16_t*)(g_voodoo->fbi.ram +
                          g_voodoo->fbi.rgboffs[g_voodoo->fbi.frontbuf]);
    } else {
        /* Back buffer (normal case) */
        dest = (uint16_t*)(g_voodoo->fbi.ram +
                          g_voodoo->fbi.rgboffs[g_voodoo->fbi.backbuf]);
    }

    /* Depth buffer */
    depthbuf = (uint16_t*)(g_voodoo->fbi.ram + g_voodoo->fbi.auxoffs);

    /*
     * Clear color buffer only if RGB writes are enabled
     */
    if (0) {
        /*
         * Convert 32-bit ARGB color to RGB565
         *
         * ARGB8888: AAAA AAAA RRRR RRRR GGGG GGGG BBBB BBBB
         * RGB565:   RRRR RGGG GGGB BBBB
         *
         * R: 8 bits -> 5 bits (shift right 3)
         * G: 8 bits -> 6 bits (shift right 2)
         * B: 8 bits -> 5 bits (shift right 3)
         */
        int r = (color >> 16) & 0xFF;
        int g = (color >> 8) & 0xFF;
        int b = color & 0xFF;
        uint16_t color565 = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);

        /* TRAP: Catch black buffer clears */
        if (color565 == 0x0000) {
            trap_log("BUFFER CLEAR TRAP: Clearing %s buffer to BLACK (dest=%p, size=%dx%d)\n",
                    g_render_buffer == 0 ? "FRONT" : "BACK", (void*)dest,
                    g_voodoo->fbi.width, g_voodoo->fbi.height);
        }

        for (y = 0; y < (int)g_voodoo->fbi.height; y++) {
            for (x = 0; x < (int)g_voodoo->fbi.width; x++) {
                dest[y * g_voodoo->fbi.rowpixels + x] = color565;
            }
        }
    }

    /*
     * Clear depth buffer only if AUX writes are enabled
     *
     * The depth parameter is 32-bit, but our depth buffer is 16-bit.
     * We use the upper 16 bits as the clear value.
     */
    if (doDepth) {
        uint16_t depth16 = (uint16_t)(depth >> 16);
        for (y = 0; y < (int)g_voodoo->fbi.height; y++) {
            for (x = 0; x < (int)g_voodoo->fbi.width; x++) {
                depthbuf[y * g_voodoo->fbi.rowpixels + x] = depth16;
            }
        }
    }
}

/*
 * grBufferSwap - Display rendered frame and swap buffers
 *
 * From the 3dfx SDK:
 * "grBufferSwap() makes the back buffer visible by swapping the roles
 * of the front and back buffers. The actual buffer swap is synchronized
 * to vertical retrace."
 *
 * Parameters:
 *   swap_interval - Number of vertical retraces to wait before swapping:
 *                   0 = swap immediately (may cause tearing)
 *                   1 = wait for next retrace (60fps max at 60Hz)
 *                   2 = wait for every other retrace (30fps max)
 *                   etc.
 *
 * Vsync note:
 * Vsync was important on CRT monitors to prevent visible "tearing"
 * where the top and bottom of the screen show different frames.
 * We ignore swap_interval and swap immediately.
 *
 * LFB note:
 * If LFB writes targeted the front buffer, we present that instead
 * of the normal back buffer. This handles games that write directly
 * to the front buffer for video playback, etc.
 *
 * Page flipping:
 * After the swap, the buffer indices are exchanged:
 *   - What was the back buffer is now front (displayed)
 *   - What was the front buffer is now back (render target)
 */
void __stdcall grBufferSwap(FxU32 swap_interval)
{
    g_swap_count++;

    if (!g_voodoo || !g_voodoo->active) return;

    (void)swap_interval;  /* Ignored - we don't do vsync */

    uint16_t *presentbuf;

    /* Determine which buffer to present */
    if (g_lfb_buffer_locked == GR_BUFFER_FRONTBUFFER) {
        /* LFB writes went to front buffer - present that */
        presentbuf = (uint16_t*)(g_voodoo->fbi.ram +
                                 g_voodoo->fbi.rgboffs[g_voodoo->fbi.frontbuf]);
    } else {
        /* Normal case: present the back buffer */
        presentbuf = (uint16_t*)(g_voodoo->fbi.ram +
                                 g_voodoo->fbi.rgboffs[g_voodoo->fbi.backbuf]);
    }

    /* Send to display */
    display_present(presentbuf, g_voodoo->fbi.width, g_voodoo->fbi.height);

    /* Reset LFB lock tracking for next frame */
    g_lfb_buffer_locked = -1;

    /* Swap buffer indices */
    uint8_t temp = g_voodoo->fbi.frontbuf;
    g_voodoo->fbi.frontbuf = g_voodoo->fbi.backbuf;
    g_voodoo->fbi.backbuf = temp;

    /* Reset diagnostic logging counter for next frame */
    diag_pixel_count = 0;
}

/*
 * grRenderBuffer - Select rendering target buffer
 *
 * From the 3dfx SDK:
 * "grRenderBuffer() selects the buffer that will be the target for
 * subsequent rendering operations."
 *
 * Parameters:
 *   buffer - Which buffer to render to:
 *            GR_BUFFER_FRONTBUFFER - Draw directly to displayed buffer
 *            GR_BUFFER_BACKBUFFER - Draw to hidden back buffer (normal)
 *
 * Usage:
 * Most applications render exclusively to the back buffer, then swap.
 * Rendering to the front buffer causes immediate display but may show
 * partial frames (tearing).
 *
 * Some uses for front buffer rendering:
 * - Simple 2D games without double buffering
 * - Debugging (see rendering as it happens)
 * - Overlay effects drawn after the main swap
 */
void __stdcall grRenderBuffer(GrBuffer_t buffer)
{
    

    if (buffer == GR_BUFFER_FRONTBUFFER) {
        g_render_buffer = 0;
    } else {
        g_render_buffer = 1;
    }

    /* Update fbzMode register for voodoo_triangle */
    if (g_voodoo) {
        uint32_t val = g_voodoo->reg[fbzMode].u;
        val &= ~FBZMODE_DRAW_BUFFER_MASK;
        val |= (g_render_buffer << FBZMODE_DRAW_BUFFER_SHIFT);
        g_voodoo->reg[fbzMode].u = val;
    }
}
