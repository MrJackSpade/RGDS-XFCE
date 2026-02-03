/*
 * glide3x_state.c - Shared state definitions for Glide implementation
 *
 * This file defines all global state variables used by the Glide API.
 * All other glide3x_*.c modules include glide3x_state.h to access these.
 *
 * MEMORY LAYOUT:
 * The Voodoo emulator (g_voodoo) contains all hardware state:
 *   - fbi: Frame Buffer Interface - color buffers, depth buffer, combine settings
 *   - tmu[]: Texture Mapping Units - texture memory, filtering, coordinates
 *   - reg[]: Hardware register array - all configurable state bits
 *
 * INITIALIZATION ORDER:
 * 1. DLL loads -> DllMain called -> nothing initialized yet
 * 2. grGlideInit() -> g_voodoo allocated, g_initialized = 1
 * 3. grSstWinOpen() -> display init, buffers allocated, g_context set
 * 4. Rendering... -> state modified, triangles drawn
 * 5. grSstWinClose() -> display shutdown, g_context = NULL
 * 6. grGlideShutdown() -> g_voodoo freed, g_initialized = 0
 */

#include "glide3x_state.h"
#include <stdarg.h>

/*************************************
 * Debug logging implementation
 *************************************/

FILE *g_debug_log = NULL;
static FILE *g_trap_log = NULL;
int g_call_count = 0;

/* Deduplication state for consecutive identical messages */
static char g_last_msg[512] = {0};
static int g_last_msg_count = 0;

/*
 * debug_log_output - Internal function to write a message
 *
 * Outputs to both file and Windows debug console.
 */
static void debug_log_output(const char *msg)
{
    if (!g_debug_log) {
        g_debug_log = fopen("C:\\glide3x_debug.log", "w");
    }
    if (g_debug_log) {
        fputs(msg, g_debug_log);
        fflush(g_debug_log);
    }
    OutputDebugStringA(msg);
}

/*
 * trap_log - Write a trap message to the debug log file
 *
 * This function is always enabled (unlike debug_log which can be disabled).
 * Used by diagnostic traps to catch black pixel writes.
 */
void trap_log(const char *fmt, ...)
{
    if (!g_trap_log) {
        g_trap_log = fopen("C:\\glide3x_debug.log", "a");
    }
    if (g_trap_log) {
        va_list args;
        va_start(args, fmt);
        vfprintf(g_trap_log, fmt, args);
        va_end(args);
        fflush(g_trap_log);
    }
}

/*
 * debug_log_flush - Flush any pending deduplicated message
 *
 * Call this before shutdown to ensure the last message and its
 * count are written to the log.
 *
 * Only outputs if count > 1 (duplicates occurred). Messages with
 * count == 1 were already output on first occurrence.
 */
void debug_log_flush(void)
{
    if (g_last_msg_count > 1) {
        char buf[600];
        /* Remove trailing newline from stored message for formatting */
        size_t len = strlen(g_last_msg);
        if (len > 0 && g_last_msg[len - 1] == '\n') {
            g_last_msg[len - 1] = '\0';
        }
        snprintf(buf, sizeof(buf), "%s (Called %d times)\n", g_last_msg, g_last_msg_count);
        debug_log_output(buf);
    }
    /* Note: count == 1 means message was already output, no action needed */
    g_last_msg[0] = '\0';
    g_last_msg_count = 0;
}

/*
 * debug_log - Write a message to the debug log file
 *
 * Creates the log file on first call. All output is flushed immediately
 * to ensure logs are captured even if the application crashes.
 *
 * Deduplicates consecutive identical messages by counting them and
 * outputting "message (Called N times)" when a different message arrives.
 *
 * Also outputs to Windows debug console via OutputDebugStringA()
 * so messages appear in debugger output windows.
 */
void debug_log(const char *msg)
{
    /* LOGGING DISABLED - uncomment below to re-enable */
    (void)msg;
    return;

    /*
    if (g_last_msg_count > 0 && strcmp(msg, g_last_msg) == 0) {
        g_last_msg_count++;
        return;
    }

    debug_log_flush();

    strncpy(g_last_msg, msg, sizeof(g_last_msg) - 1);
    g_last_msg[sizeof(g_last_msg) - 1] = '\0';
    g_last_msg_count = 1;

    debug_log_output(msg);
    */
}

/*************************************
 * Core Glide state
 *************************************/

voodoo_state *g_voodoo = NULL;
GrContext_t g_context = NULL;
int g_initialized = 0;

/*************************************
 * Screen/display state
 *************************************/

int g_screen_width = 640;
int g_screen_height = 480;

/*************************************
 * Rendering state
 *************************************/

GrColor_t g_constant_color = 0xFFFFFFFF;
int g_render_buffer = 1;  /* Default: back buffer */
int g_active_tmu = 0;     /* Which TMU was last configured via grTexSource */
int g_color_format = 0;   /* GR_COLORFORMAT_ARGB=0, GR_COLORFORMAT_ABGR=1 */
int g_lfb_buffer_locked = -1;
GrLfbWriteMode_t g_lfb_write_mode = GR_LFBWRITEMODE_565;
GrOriginLocation_t g_lfb_origin = GR_ORIGIN_UPPER_LEFT;

/* Shadow buffer for non-16-bit LFB modes */
uint8_t *g_lfb_shadow_buffer = NULL;
size_t g_lfb_shadow_buffer_size = 0;
int g_lfb_shadow_width = 0;
int g_lfb_shadow_height = 0;
GrBuffer_t g_lfb_shadow_target = 0;

/*************************************
 * Statistics counters
 *************************************/

int g_clear_count = 0;
int g_swap_count = 0;
int g_triangle_count = 0;
int g_draw_call_count = 0;
int g_lfb_lock_count = 0;
int g_lfb_unlock_count = 0;
int g_lfb_write_count = 0;

/*************************************
 * Resolution mapping
 *
 * Glide uses enumerated resolution constants rather than raw dimensions.
 * This reflects the fixed-resolution nature of early Voodoo hardware.
 *
 * Voodoo 1: Max 640x480 (800x600 with SLI)
 * Voodoo 2: Max 800x600 (1024x768 with SLI)
 * Voodoo 3+: Higher resolutions supported
 *************************************/

void get_resolution(GrScreenResolution_t res, int *width, int *height)
{
    switch (res) {
    case GR_RESOLUTION_320x200:  *width = 320;  *height = 200;  break;
    case GR_RESOLUTION_320x240:  *width = 320;  *height = 240;  break;
    case GR_RESOLUTION_400x256:  *width = 400;  *height = 256;  break;
    case GR_RESOLUTION_512x384:  *width = 512;  *height = 384;  break;
    case GR_RESOLUTION_640x200:  *width = 640;  *height = 200;  break;
    case GR_RESOLUTION_640x350:  *width = 640;  *height = 350;  break;
    case GR_RESOLUTION_640x400:  *width = 640;  *height = 400;  break;
    case GR_RESOLUTION_640x480:  *width = 640;  *height = 480;  break;
    case GR_RESOLUTION_800x600:  *width = 800;  *height = 600;  break;
    case GR_RESOLUTION_1024x768: *width = 1024; *height = 768;  break;
    default:                     *width = 640;  *height = 480;  break;
    }
}

/*************************************
 * DLL entry point
 *
 * Windows calls DllMain when the DLL is loaded/unloaded or when
 * threads attach/detach. We use this for:
 * - Process attach: Disable thread notifications for performance
 * - Process detach: Ensure cleanup if app didn't call grGlideShutdown()
 *************************************/

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved)
{
    (void)hinstDLL;
    (void)lpvReserved;

    switch (fdwReason) {
    case DLL_PROCESS_ATTACH:
        /* Delete old log file to start fresh */
        DeleteFileA("glide3x_debug.log");
        debug_log("glide3x: DLL_PROCESS_ATTACH\n");
        /* Disable thread library calls for performance */
        DisableThreadLibraryCalls(hinstDLL);
        break;

    case DLL_PROCESS_DETACH:
        debug_log("glide3x: DLL_PROCESS_DETACH\n");
        /*
         * Emergency cleanup if app didn't call grGlideShutdown().
         * This can happen if app crashes or calls ExitProcess().
         */
        if (g_initialized) {
            grGlideShutdown();
        }
        /* Destroy window to prevent invalid WndProc callbacks */
        display_destroy_window();
        /* Flush any pending log messages with counts */
        debug_log_flush();
        break;
    }
    return TRUE;
}
