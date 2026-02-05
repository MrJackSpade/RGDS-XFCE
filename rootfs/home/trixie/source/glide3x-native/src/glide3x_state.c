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

static FILE *g_debug_log = NULL;
static int g_debug_log_first_access = 1;
static LARGE_INTEGER g_debug_log_start_time;
static LARGE_INTEGER g_debug_log_freq;

/* Track 640x480 mode switches - only log after second switch */
int g_640x480_switch_count = 0;
int g_logging_enabled = 0;

/*
 * debug_log - Write a formatted message to the debug log file
 *
 * Creates/truncates the log file on first call. All output is flushed
 * immediately to ensure logs are captured even if the application crashes.
 * Each line is prefixed with a millisecond timestamp relative to first log call.
 */
void debug_log(const char *fmt, ...)
{
    /* Only log after second 640x480 switch to focus on the problem area */
    if (!g_logging_enabled) return;

    if (!g_debug_log) {
        if (g_debug_log_first_access) {
            DeleteFileA("C:\\glide3x_debug.log");
            QueryPerformanceFrequency(&g_debug_log_freq);
            QueryPerformanceCounter(&g_debug_log_start_time);
            g_debug_log_first_access = 0;
        }
        g_debug_log = fopen("C:\\glide3x_debug.log", "a");
    }
    if (g_debug_log) {
        /* Get elapsed time in milliseconds */
        LARGE_INTEGER now;
        QueryPerformanceCounter(&now);
        double elapsed_ms = (double)(now.QuadPart - g_debug_log_start_time.QuadPart) * 1000.0 / (double)g_debug_log_freq.QuadPart;

        /* Print timestamp prefix */
        fprintf(g_debug_log, "[%10.3f] ", elapsed_ms);

        va_list args;
        va_start(args, fmt);
        vfprintf(g_debug_log, fmt, args);
        va_end(args);
        fflush(g_debug_log);
    }
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
        /* Disable thread library calls for performance */
        DisableThreadLibraryCalls(hinstDLL);
        break;

    case DLL_PROCESS_DETACH:
        /*
         * Emergency cleanup if app didn't call grGlideShutdown().
         * This can happen if app crashes or calls ExitProcess().
         */
        if (g_initialized) {
            grGlideShutdown();
        }
        /* Destroy window to prevent invalid WndProc callbacks */
        display_destroy_window();
        /* Close log file if open */
        if (g_debug_log) {
            fclose(g_debug_log);
            g_debug_log = NULL;
        }
        break;
    }
    return TRUE;
}
