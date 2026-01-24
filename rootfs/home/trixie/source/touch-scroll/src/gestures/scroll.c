/**
 * @file scroll.c
 * @brief Implementation of scrolling logic.
 */

#include "scroll.h"
#include "../virtual_mouse.h"

// Hi-res scroll units per pixel of finger movement.
// 120 is one standard "detent". Lower values = slower scrolling.
#define HIRES_PER_PIXEL 10

/**
 * @brief Process scroll updates based on finger movement.
 * Calculates the delta from the last position and converts it to high-resolution
 * scroll events.
 * @param state Current gesture state.
 * @param x Current X position.
 * @param y Current Y position.
 */
void handle_scroll_update(struct gesture_state *state, int x, int y) {
    int dy = y - state->last_y;
    int dx = x - state->last_x;
    
    // Note: Direction mapping.
    // Standard Touchscreen logic: Finger moves UP (Y decreases) -> Content moves UP (Scroll Down).
    // Standard Mouse Wheel: Rolling "Back" (towards user) -> Scroll Down.
    //
    // Here we map physical delta directly.
    
    if (dy != 0 || dx != 0) {
        // Vertical Scroll
        int v_hires = dy * HIRES_PER_PIXEL;
        
        // Horizontal Scroll
        int h_hires = dx * HIRES_PER_PIXEL;
        
        virtual_mouse_scroll_hires(v_hires, h_hires);
    }
}
