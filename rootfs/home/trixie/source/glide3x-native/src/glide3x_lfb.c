/*
 * glide3x_lfb.c - Linear Frame Buffer (LFB) access
 *
 * This module implements direct CPU access to the framebuffer:
 *   - grLfbLock(): Lock a buffer for CPU read/write access
 *   - grLfbUnlock(): Release buffer lock
 *   - grLfbWriteRegion(): Write a rectangular region to buffer
 *   - grLfbReadRegion(): Read a rectangular region from buffer
 *
 * LFB CONCEPT:
 * The Linear Frame Buffer provides direct CPU access to video memory,
 * bypassing the 3D rendering pipeline. This allows applications to:
 *   - Write video frames directly (for FMV playback)
 *   - Perform 2D blitting operations
 *   - Read back rendered images (screenshots)
 *   - Apply CPU-based post-processing effects
 *
 * PERFORMANCE CONSIDERATIONS:
 * On original Voodoo hardware, LFB access was relatively slow because:
 *   1. Data traveled over the PCI bus (133 MB/s max)
 *   2. Memory access patterns weren't optimized for sequential transfer
 *   3. Reads were especially slow (read-back over PCI)
 *
 * Voodoo Banshee and later cards improved this with:
 *   - AGP bus for higher bandwidth
 *   - Unified Memory Architecture (UMA)
 *   - Write combining for sequential writes
 *
 * Our software implementation has no such bottlenecks, but the API
 * remains the same for compatibility.
 *
 * PIXEL FORMATS:
 * LFB supports various pixel formats for reads and writes:
 *   - RGB565 (16-bit): Default framebuffer format
 *   - ARGB1555 (16-bit): 1 bit alpha, 5 bits per color
 *   - ARGB4444 (16-bit): 4 bits per component
 *   - ARGB8888 (32-bit): 8 bits per component (Banshee+)
 *
 * The writeMode parameter to grLfbLock() specifies the format.
 * Our implementation uses RGB565 internally.
 *
 * PIXEL PIPELINE:
 * The pixelPipeline parameter to grLfbLock() determines whether writes
 * go through the pixel pipeline (depth test, alpha blend, etc.) or
 * write directly to memory. Pipeline mode is useful for effects like
 * drawing UI elements that should respect depth ordering.
 */

#include "glide3x_state.h"
#include <stdlib.h>  /* For malloc/free */

/*
 * Get bytes per pixel for a given LFB write mode
 */
static int get_writemode_bpp(GrLfbWriteMode_t mode)
{
    switch (mode) {
    case GR_LFBWRITEMODE_565:
    case GR_LFBWRITEMODE_555:
    case GR_LFBWRITEMODE_1555:
        return 2;
    case GR_LFBWRITEMODE_888:
        return 3;
    case GR_LFBWRITEMODE_8888:
        return 4;
    default:
        return 2;  /* Default to 16-bit */
    }
}

/*
 * grLfbLock - Lock a buffer for direct CPU access
 *
 * From the 3dfx SDK:
 * "grLfbLock() gives the caller direct access to the specified buffer.
 * The buffer remains locked until grLfbUnlock() is called. While locked,
 * 3D rendering to the buffer is still possible."
 *
 * Parameters:
 *   type - Lock type (GR_LFB_*):
 *          READ_ONLY:  Lock for reading only
 *          WRITE_ONLY: Lock for writing only
 *          READ_WRITE: Lock for both (Banshee+)
 *          NOIDLE:     Don't wait for graphics idle before locking
 *
 *   buffer - Which buffer to lock:
 *            GR_BUFFER_FRONTBUFFER: Currently displayed buffer
 *            GR_BUFFER_BACKBUFFER:  Rendering target buffer
 *            GR_BUFFER_AUXBUFFER:   Depth/alpha buffer
 *
 *   writeMode - Pixel format for writes (GR_LFBWRITEMODE_*):
 *               565:      RGB565 (16-bit, no alpha)
 *               1555:     ARGB1555 (16-bit, 1-bit alpha)
 *               4444:     ARGB4444 (16-bit, 4-bit alpha)
 *               8888:     ARGB8888 (32-bit, full alpha)
 *               RESERVED: Use format from previous lock
 *
 *   origin - Y origin for LFB addressing:
 *            UPPER_LEFT: Y=0 at top (DirectX style)
 *            LOWER_LEFT: Y=0 at bottom (OpenGL style)
 *            Can differ from 3D rendering origin.
 *
 *   pixelPipeline - Enable pixel pipeline for writes:
 *                   FXFALSE: Direct memory access (fastest)
 *                   FXTRUE:  Go through depth test, blend, etc.
 *
 *   info - Output structure filled with:
 *          lfbPtr:        Pointer to buffer start
 *          strideInBytes: Bytes per row (includes padding)
 *          writeMode:     Actual write mode in effect
 *          origin:        Actual origin in effect
 *
 * Returns:
 *   FXTRUE on success, FXFALSE on failure.
 *
 * IMPORTANT USAGE NOTES:
 *
 * 1. Buffer pointers are valid only between Lock and Unlock.
 *    Don't cache them across frames.
 *
 * 2. Writing to the front buffer appears immediately on screen.
 *    This can cause tearing if done during display refresh.
 *
 * 3. The stride may be larger than width * bytesPerPixel due to
 *    hardware alignment requirements. Always use strideInBytes.
 *
 * 4. Our implementation tracks which buffer was locked so
 *    grBufferSwap() knows whether to present front or back buffer.
 *
 * SHADOW BUFFER FOR NON-16-BIT MODES:
 * When the application requests a non-16-bit write mode (e.g., 8888),
 * we allocate a shadow buffer at that bit depth, return it to the app,
 * and convert to 16-bit on grLfbUnlock(). This fixes the "ZARDBLIZ" bug
 * in Diablo 2 where 32-bit writes with 16-bit stride caused wrapping.
 */
FxBool __stdcall grLfbLock(GrLock_t type, GrBuffer_t buffer, GrLfbWriteMode_t writeMode,
                 GrOriginLocation_t origin, FxBool pixelPipeline, GrLfbInfo_t *info)
{
    g_lfb_lock_count++;
    {
        char dbg[128];
        snprintf(dbg, sizeof(dbg),
                 "glide3x: grLfbLock #%d (type=%d, buffer=%d, writeMode=%d)\n",
                 g_lfb_lock_count, type, buffer, writeMode);
        debug_log(dbg);
    }

    if (!g_voodoo || !info) return FXFALSE;

    /* Track which buffer is being locked for writes */
    if (type == GR_LFB_WRITE_ONLY || type == (GR_LFB_READ_ONLY + 1)) {
        g_lfb_buffer_locked = buffer;
    }

    /* Track writeMode and origin for format conversion during writes */
    g_lfb_write_mode = writeMode;
    g_lfb_origin = origin;

    (void)pixelPipeline;  /* We don't support pipeline mode */

    int width = g_voodoo->fbi.width;
    int height = g_voodoo->fbi.height;
    int bpp = get_writemode_bpp(writeMode);
    int stride = width * bpp;

    /*
     * For non-16-bit write modes, we need a shadow buffer because
     * our internal framebuffer is 16-bit RGB565.
     */
    if (bpp != 2 && type == GR_LFB_WRITE_ONLY) {
        /* Allocate or resize shadow buffer if needed */
        size_t needed_size = (size_t)stride * height;
        int need_init = 0;

        if (g_lfb_shadow_buffer_size < needed_size) {
            free(g_lfb_shadow_buffer);
            g_lfb_shadow_buffer = (uint8_t*)malloc(needed_size);
            if (!g_lfb_shadow_buffer) {
                g_lfb_shadow_buffer_size = 0;
                return FXFALSE;
            }
            g_lfb_shadow_buffer_size = needed_size;
            need_init = 1;  /* New buffer needs initialization */
        }

        /*
         * Only initialize the shadow buffer on first allocation.
         * On subsequent locks, keep the existing contents - this avoids
         * redundant framebuffer->shadow->framebuffer round-trips and
         * preserves 32-bit precision across frames.
         */
        if (need_init) {
            /* Initialize new shadow buffer to black (or could copy from FB) */
            memset(g_lfb_shadow_buffer, 0, needed_size);

            char dbg[128];
            snprintf(dbg, sizeof(dbg),
                     "glide3x: grLfbLock allocated new shadow buffer %zu bytes\n",
                     needed_size);
            debug_log(dbg);
        }

        /* Track shadow buffer parameters for unlock */
        g_lfb_shadow_width = width;
        g_lfb_shadow_height = height;
        g_lfb_shadow_target = buffer;

        info->size = sizeof(GrLfbInfo_t);
        info->lfbPtr = g_lfb_shadow_buffer;
        info->strideInBytes = stride;
        info->writeMode = writeMode;
        info->origin = origin;

        {
            char dbg[128];
            snprintf(dbg, sizeof(dbg),
                     "glide3x: grLfbLock using shadow buffer, stride=%d (bpp=%d)\n",
                     stride, bpp);
            debug_log(dbg);
        }
    } else {
        /* 16-bit mode or read-only: return direct framebuffer pointer */
        uint8_t *bufptr;
        switch (buffer) {
        case GR_BUFFER_FRONTBUFFER:
            bufptr = g_voodoo->fbi.ram + g_voodoo->fbi.rgboffs[g_voodoo->fbi.frontbuf];
            break;
        case GR_BUFFER_BACKBUFFER:
            bufptr = g_voodoo->fbi.ram + g_voodoo->fbi.rgboffs[g_voodoo->fbi.backbuf];
            break;
        case GR_BUFFER_AUXBUFFER:
        case GR_BUFFER_DEPTHBUFFER:
            bufptr = g_voodoo->fbi.ram + g_voodoo->fbi.auxoffs;
            break;
        default:
            return FXFALSE;
        }

        /* No shadow buffer in use */
        g_lfb_shadow_target = (GrBuffer_t)-1;

        info->size = sizeof(GrLfbInfo_t);
        info->lfbPtr = bufptr;
        info->strideInBytes = stride;  /* width * bpp (2 for 16-bit) */
        info->writeMode = writeMode;
        info->origin = origin;
    }

    {
        char dbg[128];
        snprintf(dbg, sizeof(dbg),
                 "glide3x: grLfbLock returning lfbPtr=%p stride=%d\n",
                 info->lfbPtr, info->strideInBytes);
        debug_log(dbg);
    }

    return FXTRUE;
}

/*
 * Convert shadow buffer to 16-bit framebuffer
 */
static void convert_shadow_to_framebuffer(GrBuffer_t buffer)
{
    if (!g_voodoo || !g_lfb_shadow_buffer) return;

    /* Get destination buffer */
    uint16_t *dest;
    switch (buffer) {
    case GR_BUFFER_FRONTBUFFER:
        dest = (uint16_t*)(g_voodoo->fbi.ram +
                           g_voodoo->fbi.rgboffs[g_voodoo->fbi.frontbuf]);
        break;
    case GR_BUFFER_BACKBUFFER:
        dest = (uint16_t*)(g_voodoo->fbi.ram +
                           g_voodoo->fbi.rgboffs[g_voodoo->fbi.backbuf]);
        break;
    default:
        return;
    }

    int width = g_lfb_shadow_width;
    int height = g_lfb_shadow_height;
    int bpp = get_writemode_bpp(g_lfb_write_mode);
    int src_stride = width * bpp;
    int dst_stride = g_voodoo->fbi.rowpixels;

    {
        char dbg[128];
        snprintf(dbg, sizeof(dbg),
                 "glide3x: Converting shadow buffer %dx%d bpp=%d to framebuffer\n",
                 width, height, bpp);
        debug_log(dbg);
    }

    for (int y = 0; y < height; y++) {
        uint16_t *dst_row = &dest[y * dst_stride];
        uint8_t *src_row = &g_lfb_shadow_buffer[y * src_stride];

        switch (g_lfb_write_mode) {
        case GR_LFBWRITEMODE_8888:
            /* Convert ARGB8888 to RGB565 */
            {
                uint32_t *src32 = (uint32_t*)src_row;
                for (int x = 0; x < width; x++) {
                    uint32_t pix = src32[x];
                    uint8_t r = (pix >> 16) & 0xFF;
                    uint8_t g = (pix >> 8) & 0xFF;
                    uint8_t b = pix & 0xFF;
                    dst_row[x] = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
                }
            }
            break;

        case GR_LFBWRITEMODE_888:
            /* Convert RGB888 to RGB565 */
            for (int x = 0; x < width; x++) {
                uint8_t b = src_row[x * 3 + 0];
                uint8_t g = src_row[x * 3 + 1];
                uint8_t r = src_row[x * 3 + 2];
                dst_row[x] = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
            }
            break;

        case GR_LFBWRITEMODE_555:
            /* Convert RGB555 to RGB565 */
            {
                uint16_t *src16 = (uint16_t*)src_row;
                for (int x = 0; x < width; x++) {
                    uint16_t pix = src16[x];
                    uint16_t r = (pix >> 10) & 0x1F;
                    uint16_t g = (pix >> 5) & 0x1F;
                    uint16_t b = pix & 0x1F;
                    dst_row[x] = (r << 11) | (g << 6) | b;
                }
            }
            break;

        case GR_LFBWRITEMODE_1555:
            /* Convert ARGB1555 to RGB565 (discard alpha) */
            {
                uint16_t *src16 = (uint16_t*)src_row;
                for (int x = 0; x < width; x++) {
                    uint16_t pix = src16[x];
                    uint16_t r = (pix >> 10) & 0x1F;
                    uint16_t g = (pix >> 5) & 0x1F;
                    uint16_t b = pix & 0x1F;
                    dst_row[x] = (r << 11) | (g << 6) | b;
                }
            }
            break;

        default:
            /* 565 or unknown - direct copy */
            memcpy(dst_row, src_row, width * 2);
            break;
        }
    }
}

/*
 * grLfbUnlock - Release a locked buffer
 *
 * From the 3dfx SDK:
 * "grLfbUnlock() releases the lock on the specified buffer that was
 * acquired with grLfbLock()."
 *
 * Parameters:
 *   type   - Must match the type used in grLfbLock()
 *   buffer - Must match the buffer used in grLfbLock()
 *
 * Returns:
 *   FXTRUE on success, FXFALSE if buffer wasn't locked.
 *
 * SPECIAL BEHAVIOR:
 * If a WRITE_ONLY lock is released on the front buffer, we immediately
 * present that buffer to the display. This ensures LFB writes to the
 * front buffer are visible without requiring a grBufferSwap().
 *
 * SHADOW BUFFER CONVERSION:
 * If a shadow buffer was used (for non-16-bit write modes), we convert
 * it to the 16-bit framebuffer here before presenting.
 *
 * This behavior is important for applications that:
 *   - Play fullscreen video directly to the front buffer
 *   - Draw UI elements after the main rendering pass
 *   - Don't use double buffering
 */
FxBool __stdcall grLfbUnlock(GrLock_t type, GrBuffer_t buffer)
{
    g_lfb_unlock_count++;
    {
        char dbg[128];
        snprintf(dbg, sizeof(dbg),
                 "glide3x: grLfbUnlock #%d (type=%d, buffer=%d)\n",
                 g_lfb_unlock_count, type, buffer);
        debug_log(dbg);
    }

    if (!g_voodoo) return FXFALSE;

    /* If shadow buffer was used, convert it to framebuffer */
    if (type == GR_LFB_WRITE_ONLY && g_lfb_shadow_target == buffer && g_lfb_shadow_buffer) {
        convert_shadow_to_framebuffer(buffer);
        g_lfb_shadow_target = (GrBuffer_t)-1;  /* Mark shadow as processed */
    }

    /* If this was a write lock on front buffer, present immediately */
    if (type == GR_LFB_WRITE_ONLY && buffer == GR_BUFFER_FRONTBUFFER) {
        uint16_t *frontbuf = (uint16_t*)(g_voodoo->fbi.ram +
                                          g_voodoo->fbi.rgboffs[g_voodoo->fbi.frontbuf]);
        {
            char dbg[128];
            snprintf(dbg, sizeof(dbg),
                     "glide3x: grLfbUnlock presenting front buffer\n");
            debug_log(dbg);
        }
        display_present(frontbuf, g_voodoo->fbi.width, g_voodoo->fbi.height);
    }

    return FXTRUE;
}

/*
 * grLfbWriteRegion - Write a rectangular region to the framebuffer
 *
 * From the 3dfx SDK:
 * "grLfbWriteRegion() writes a rectangular region of pixels from system
 * memory to the specified buffer. This is typically faster than locking
 * the buffer and writing manually for large regions."
 *
 * Parameters:
 *   dst_buffer    - Destination buffer (front, back, or aux)
 *   dst_x, dst_y  - Destination position in buffer
 *   src_format    - Format of source data (GR_LFB_SRC_FMT_*)
 *   src_width     - Width of region in pixels
 *   src_height    - Height of region in pixels
 *   pixelPipeline - Enable pixel pipeline for writes
 *   src_stride    - Bytes per row in source data
 *   src_data      - Pointer to source pixel data
 *
 * Returns:
 *   FXTRUE on success, FXFALSE on failure.
 *
 * SOURCE FORMATS:
 *   565:     RGB565 (16-bit, most common)
 *   1555:    ARGB1555 (16-bit with 1-bit alpha)
 *   4444:    ARGB4444 (16-bit with 4-bit alpha)
 *   8888:    ARGB8888 (32-bit with 8-bit alpha)
 *   RLE16:   Run-length encoded 16-bit
 *
 * Our implementation assumes RGB565 source format and direct memory copy.
 *
 * COMMON USES:
 *   - Video playback: Decompress frames to system memory, blit to FB
 *   - Loading screens: Display pre-rendered images
 *   - UI overlays: Render 2D elements to back buffer
 *
 * PERFORMANCE NOTE:
 * For repeated small writes, grLfbLock/Unlock may be faster.
 * For single large writes, grLfbWriteRegion is more convenient.
 */
FxBool __stdcall grLfbWriteRegion(GrBuffer_t dst_buffer, FxU32 dst_x, FxU32 dst_y,
                         GrLfbSrcFmt_t src_format, FxU32 src_width, FxU32 src_height,
                         FxBool pixelPipeline, FxI32 src_stride, void *src_data)
{
    g_lfb_write_count++;
    {
        char dbg[128];
        snprintf(dbg, sizeof(dbg),
                 "glide3x: grLfbWriteRegion #%d (buf=%d, x=%u, y=%u, w=%u, h=%u)\n",
                 g_lfb_write_count, dst_buffer, dst_x, dst_y, src_width, src_height);
        debug_log(dbg);
    }

    if (!g_voodoo || !src_data) return FXFALSE;

    (void)pixelPipeline;   /* We don't support pipeline mode */

    /* Get destination buffer */
    uint16_t *dest;
    switch (dst_buffer) {
    case GR_BUFFER_FRONTBUFFER:
        dest = (uint16_t*)(g_voodoo->fbi.ram +
                           g_voodoo->fbi.rgboffs[g_voodoo->fbi.frontbuf]);
        break;
    case GR_BUFFER_BACKBUFFER:
        dest = (uint16_t*)(g_voodoo->fbi.ram +
                           g_voodoo->fbi.rgboffs[g_voodoo->fbi.backbuf]);
        break;
    case GR_BUFFER_AUXBUFFER:
    case GR_BUFFER_DEPTHBUFFER:
        dest = (uint16_t*)(g_voodoo->fbi.ram + g_voodoo->fbi.auxoffs);
        break;
    default:
        return FXFALSE;
    }

    /* Copy data row by row with format conversion if needed */
    uint8_t *src = (uint8_t*)src_data;

    for (FxU32 y = 0; y < src_height; y++) {
        uint16_t *dst_row = &dest[(dst_y + y) * g_voodoo->fbi.rowpixels + dst_x];

        switch (src_format) {
        case GR_LFB_SRC_FMT_565:
            /* Direct copy - native format */
            memcpy(dst_row, &src[y * src_stride], src_width * 2);
            break;

        case GR_LFB_SRC_FMT_555:
            /* Convert RGB555 to RGB565 */
            {
                uint16_t *src_row = (uint16_t*)&src[y * src_stride];
                for (FxU32 x = 0; x < src_width; x++) {
                    uint16_t pix = src_row[x];
                    /* RGB555: -RRRRRGGGGGBBBBB -> RGB565: RRRRRGGGGGGBBBBB */
                    uint16_t r = (pix >> 10) & 0x1F;
                    uint16_t g = (pix >> 5) & 0x1F;
                    uint16_t b = pix & 0x1F;
                    dst_row[x] = (r << 11) | (g << 6) | b;
                }
            }
            break;

        case GR_LFB_SRC_FMT_1555:
            /* Convert ARGB1555 to RGB565 (discard alpha) */
            {
                uint16_t *src_row = (uint16_t*)&src[y * src_stride];
                for (FxU32 x = 0; x < src_width; x++) {
                    uint16_t pix = src_row[x];
                    /* ARGB1555: ARRRRRGGGGGBBBBB -> RGB565: RRRRRGGGGGGBBBBB */
                    uint16_t r = (pix >> 10) & 0x1F;
                    uint16_t g = (pix >> 5) & 0x1F;
                    uint16_t b = pix & 0x1F;
                    dst_row[x] = (r << 11) | (g << 6) | b;
                }
            }
            break;

        case GR_LFB_SRC_FMT_888:
            /* Convert RGB888 to RGB565 */
            {
                uint8_t *src_row = &src[y * src_stride];
                for (FxU32 x = 0; x < src_width; x++) {
                    uint8_t b = src_row[x * 3 + 0];
                    uint8_t g = src_row[x * 3 + 1];
                    uint8_t r = src_row[x * 3 + 2];
                    dst_row[x] = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
                }
            }
            break;

        case GR_LFB_SRC_FMT_8888:
            /* Convert ARGB8888 to RGB565 (discard alpha) */
            {
                uint32_t *src_row = (uint32_t*)&src[y * src_stride];
                for (FxU32 x = 0; x < src_width; x++) {
                    uint32_t pix = src_row[x];
                    uint8_t r = (pix >> 16) & 0xFF;
                    uint8_t g = (pix >> 8) & 0xFF;
                    uint8_t b = pix & 0xFF;
                    dst_row[x] = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
                }
            }
            break;

        default:
            /* Unknown format - try direct copy assuming 16-bit */
            memcpy(dst_row, &src[y * src_stride], src_width * 2);
            break;
        }
    }

    return FXTRUE;
}

/*
 * grLfbReadRegion - Read a rectangular region from the framebuffer
 *
 * From the 3dfx SDK:
 * "grLfbReadRegion() reads a rectangular region of pixels from the
 * specified buffer to system memory."
 *
 * Parameters:
 *   src_buffer        - Source buffer (front, back, or aux)
 *   src_x, src_y      - Source position in buffer
 *   src_width         - Width of region in pixels
 *   src_height        - Height of region in pixels
 *   dst_stride        - Bytes per row in destination buffer
 *   dst_data          - Pointer to destination buffer
 *
 * Returns:
 *   FXTRUE on success, FXFALSE on failure.
 *
 * OUTPUT FORMAT:
 * Data is always read in the framebuffer's native format (RGB565 in our case).
 * The application is responsible for format conversion if needed.
 *
 * COMMON USES:
 *   - Screenshots: Read back the front buffer after rendering
 *   - Feedback effects: Read rendered image for CPU processing
 *   - Debugging: Verify rendered output
 *
 * PERFORMANCE NOTE:
 * Framebuffer reads are historically slow because:
 *   - Data flows from GPU to CPU (reverse of normal direction)
 *   - May require graphics pipeline flush
 *   - Can stall the CPU waiting for data
 *
 * Our software implementation doesn't have these issues, but the API
 * semantics are preserved.
 */
FxBool __stdcall grLfbReadRegion(GrBuffer_t src_buffer, FxU32 src_x, FxU32 src_y,
                        FxU32 src_width, FxU32 src_height,
                        FxU32 dst_stride, void *dst_data)
{
    

    if (!g_voodoo || !dst_data) return FXFALSE;

    /* Get source buffer */
    uint16_t *src;
    switch (src_buffer) {
    case GR_BUFFER_FRONTBUFFER:
        src = (uint16_t*)(g_voodoo->fbi.ram +
                          g_voodoo->fbi.rgboffs[g_voodoo->fbi.frontbuf]);
        break;
    case GR_BUFFER_BACKBUFFER:
        src = (uint16_t*)(g_voodoo->fbi.ram +
                          g_voodoo->fbi.rgboffs[g_voodoo->fbi.backbuf]);
        break;
    case GR_BUFFER_AUXBUFFER:
    case GR_BUFFER_DEPTHBUFFER:
        src = (uint16_t*)(g_voodoo->fbi.ram + g_voodoo->fbi.auxoffs);
        break;
    default:
        return FXFALSE;
    }

    /* Copy data row by row */
    uint8_t *dst = (uint8_t*)dst_data;
    for (FxU32 y = 0; y < src_height; y++) {
        memcpy(&dst[y * dst_stride],
               &src[(src_y + y) * g_voodoo->fbi.rowpixels + src_x],
               src_width * 2);
    }

    return FXTRUE;
}
