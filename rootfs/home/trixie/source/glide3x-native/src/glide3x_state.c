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

/*************************************
 * Debug logging implementation
 *************************************/

FILE *g_debug_log = NULL;
int g_call_count = 0;

/*
 * debug_log - Write a message to the debug log file
 *
 * Creates the log file on first call. All output is flushed immediately
 * to ensure logs are captured even if the application crashes.
 *
 * Also outputs to Windows debug console via OutputDebugStringA()
 * so messages appear in debugger output windows.
 */
void debug_log(const char *msg)
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
int g_lfb_buffer_locked = -1;

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
        break;
    }
    return TRUE;
}
