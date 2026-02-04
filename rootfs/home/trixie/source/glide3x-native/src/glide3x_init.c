/*
 * glide3x_init.c - Glide library initialization and shutdown
 *
 * This module contains the fundamental lifecycle functions for Glide:
 *   - grGlideInit(): Initialize the library
 *   - grGlideShutdown(): Cleanup and release resources
 *   - grGlideGetVersion(): Return version string
 *
 * INITIALIZATION SEQUENCE:
 * Applications must call these functions in order:
 *   1. grGlideInit()          - Initialize library
 *   2. grSstQueryHardware()   - Detect available boards (optional)
 *   3. grSstSelect()          - Select which board to use (optional)
 *   4. grSstWinOpen()         - Open rendering context
 *   ... rendering ...
 *   5. grSstWinClose()        - Close rendering context
 *   6. grGlideShutdown()      - Cleanup library
 *
 * HISTORICAL NOTE:
 * Original Glide also registered an atexit() handler to ensure cleanup
 * even if the application forgot to call grGlideShutdown(). We handle
 * this via DllMain's DLL_PROCESS_DETACH instead.
 */

#include "glide3x_state.h"

/*
 * grGlideInit - Initialize the Glide library
 *
 * From the 3dfx SDK:
 * "grGlideInit() initializes the Glide library, setting internal state
 * to known values before any other Glide functions are called. It should
 * be called once at the beginning of an application that uses Glide."
 *
 * Our implementation:
 * - Creates the software Voodoo emulator state (voodoo_create())
 * - Sets g_initialized flag
 * - Does NOT initialize display (that's grSstWinOpen's job)
 *
 * This function is idempotent - calling it multiple times is safe.
 * Subsequent calls after the first are no-ops.
 *
 * On real hardware, this function would:
 * - Detect and enumerate Voodoo boards via PCI
 * - Map memory-mapped registers
 * - Initialize driver state structures
 */
void __stdcall grGlideInit(void)
{
    if (g_initialized) {
        return;
    }

    /* Create voodoo emulator state */
    g_voodoo = voodoo_create();
    if (!g_voodoo) {
        return;
    }

    g_initialized = 1;
}

/*
 * grGlideShutdown - Shutdown the Glide library
 *
 * From the 3dfx SDK:
 * "grGlideShutdown() should be called once, during application termination.
 * It ensures that the graphics subsystem is returned to its pre-Glide state."
 *
 * Our implementation:
 * - Closes any open context (calls grSstWinClose if needed)
 * - Destroys the Voodoo emulator state
 * - Clears the g_initialized flag
 *
 * Note: Window destruction is handled in DllMain's DLL_PROCESS_DETACH
 * to avoid issues with invalid WndProc pointers.
 *
 * On real hardware, this function would:
 * - Restore VGA pass-through mode (Voodoo 1/2)
 * - Unmap memory-mapped registers
 * - Release any allocated resources
 * - Allow the VGA card to resume display control
 */
void __stdcall grGlideShutdown(void)
{
    if (!g_initialized) {
        return;
    }

    /* Close context if still open */
    if (g_context) {
        grSstWinClose(g_context);
        g_context = NULL;
    }

    /* Destroy voodoo emulator state */
    if (g_voodoo) {
        voodoo_destroy(g_voodoo);
        g_voodoo = NULL;
    }

    g_initialized = 0;
}

/*
 * grGlideGetVersion - Get Glide library version string
 *
 * From the 3dfx SDK:
 * "grGlideGetVersion() returns a string describing the version of Glide."
 *
 * Parameters:
 *   version - Output buffer, must be at least 80 characters
 *
 * The version string format varied across Glide versions:
 *   Glide 2.x: "Glide 2.4x"
 *   Glide 3.x: "Glide 3.0 Apr 22 1998 12:25:52"
 *
 * We return a custom string indicating this is a software implementation.
 */
void __stdcall grGlideGetVersion(char version[80])
{
    
    strcpy(version, "Glide3x Software 1.0 (DOSBox-Staging derived)");
}
