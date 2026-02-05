/*
 * glide3x_state.h - Shared state declarations for Glide implementation
 *
 * This header declares the global state variables that are shared across
 * all Glide API implementation modules. The actual definitions are in
 * glide3x_state.c.
 *
 * ARCHITECTURE NOTE:
 * Original Glide used thread-local storage for multi-context support.
 * Our implementation uses simple globals since we only support one context.
 * This simplifies the code significantly while still being compatible with
 * single-threaded game usage (which covers virtually all Glide games).
 */

#ifndef GLIDE3X_STATE_H
#define GLIDE3X_STATE_H

#include <windows.h>
#include <stdio.h>
#include <string.h>
#include "glide3x.h"
#include "voodoo_state.h"

/*************************************
 * Debug logging
 *
 * All Glide API calls can be logged to a file for debugging.
 * The log file is created on first write and flushed after each entry.
 *************************************/

/* Enable verbose logging - set to 1 to enable detailed transition debugging */
#define GLIDE_DEBUG_VERBOSE 1

/* Track 640x480 mode switches - only log after second switch */
extern int g_640x480_switch_count;
extern int g_logging_enabled;

/* Write a formatted message to the debug log */
void debug_log(const char *fmt, ...);

/* Verbose logging macro - only logs when GLIDE_DEBUG_VERBOSE is enabled */
#if GLIDE_DEBUG_VERBOSE
#define DEBUG_VERBOSE(...) debug_log(__VA_ARGS__)
#else
#define DEBUG_VERBOSE(...) ((void)0)
#endif

/*************************************
 * Core Glide state
 *
 * These variables represent the current Glide rendering context.
 *************************************/

/*
 * g_voodoo - The software Voodoo emulator state
 *
 * This is the heart of our implementation. It contains:
 * - FBI state (framebuffer, depth buffer, color combine settings)
 * - TMU state (texture memory, filtering, combine settings)
 * - All register values that control rendering
 *
 * Created by voodoo_create() in grGlideInit().
 * Destroyed by voodoo_destroy() in grGlideShutdown().
 */
extern voodoo_state *g_voodoo;

/*
 * g_context - The current Glide context handle
 *
 * In original Glide, this could identify one of multiple Voodoo boards.
 * We always return g_voodoo cast to GrContext_t since we only support
 * one context.
 *
 * NULL when no context is open.
 */
extern GrContext_t g_context;

/*
 * g_initialized - Whether grGlideInit() has been called
 *
 * FXTRUE after grGlideInit() succeeds.
 * FXFALSE before init or after grGlideShutdown().
 */
extern int g_initialized;

/*************************************
 * Screen/display state
 *************************************/

/*
 * g_screen_width, g_screen_height - Current resolution
 *
 * Set by grSstWinOpen() based on the requested GrScreenResolution_t.
 * Used throughout for buffer sizing and clipping.
 */
extern int g_screen_width;
extern int g_screen_height;

/*************************************
 * Rendering state
 *************************************/

/*
 * g_constant_color - Color set by grConstantColorValue()
 *
 * Used by the color combine unit when LOCAL or OTHER is set to CONSTANT.
 * Format is 0xAARRGGBB (32-bit ARGB).
 */
extern GrColor_t g_constant_color;

/*
 * g_render_buffer - Current rendering target
 *
 * 0 = front buffer (displayed)
 * 1 = back buffer (being rendered)
 *
 * Set by grRenderBuffer(). Most games render to back buffer,
 * then call grBufferSwap() to display.
 */
extern int g_render_buffer;

/*
 * g_color_format - Color component ordering from grSstWinOpen
 *
 * GR_COLORFORMAT_ARGB (0): Colors in A-R-G-B order (most common)
 * GR_COLORFORMAT_ABGR (1): Colors in A-B-G-R order (R and B swapped)
 */
extern int g_color_format;

/*
 * g_lfb_buffer_locked - Which buffer was locked for LFB writes
 *
 * -1 = no buffer locked
 * GR_BUFFER_FRONTBUFFER = front buffer locked
 * GR_BUFFER_BACKBUFFER = back buffer locked
 *
 * Used by grBufferSwap() to determine which buffer to present
 * when the application uses LFB writes instead of triangle rendering.
 */
extern int g_lfb_buffer_locked;

/*
 * g_lfb_write_mode - Pixel format for LFB writes
 *
 * Set by grLfbLock() to track the requested pixel format.
 * Used by write operations to convert incoming data to RGB565.
 */
extern GrLfbWriteMode_t g_lfb_write_mode;

/*
 * g_lfb_origin - Y coordinate origin for LFB operations
 *
 * GR_ORIGIN_UPPER_LEFT = Y=0 at top (DirectX style)
 * GR_ORIGIN_LOWER_LEFT = Y=0 at bottom (OpenGL style)
 */
extern GrOriginLocation_t g_lfb_origin;

/*
 * g_lfb_shadow_buffer - Shadow buffer for non-16-bit LFB write modes
 *
 * When a game requests a 32-bit writeMode (e.g., GR_LFBWRITEMODE_8888),
 * we can't give them a pointer to the 16-bit framebuffer directly.
 * Instead, we allocate a shadow buffer at the requested bit depth,
 * return that to the game, and convert it to 16-bit on unlock.
 */
extern uint8_t *g_lfb_shadow_buffer;
extern size_t g_lfb_shadow_buffer_size;
extern int g_lfb_shadow_width;
extern int g_lfb_shadow_height;
extern GrBuffer_t g_lfb_shadow_target;  /* Which buffer to write to on unlock */

/*************************************
 * Statistics counters (for debugging)
 *************************************/

extern int g_clear_count;
extern int g_swap_count;
extern int g_triangle_count;
extern int g_draw_call_count;
extern int g_lfb_lock_count;
extern int g_lfb_unlock_count;
extern int g_lfb_write_count;

/*************************************
 * Display interface
 *
 * These functions are implemented in display_ddraw.c
 *************************************/

/* Initialize display output (creates window if needed) */
extern int display_init(int width, int height, HWND hWindow);

/* Shutdown display output */
extern void display_shutdown(void);

/* Destroy the display window */
extern void display_destroy_window(void);

/* Present a framebuffer to the screen */
extern void display_present(uint16_t *framebuffer, int width, int height);

/*************************************
 * Helper functions
 *************************************/

/* Convert GrScreenResolution_t to width/height */
void get_resolution(GrScreenResolution_t res, int *width, int *height);

#endif /* GLIDE3X_STATE_H */
