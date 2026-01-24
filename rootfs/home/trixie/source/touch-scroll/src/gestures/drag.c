/**
 * @file drag.c
 * @brief Implementation of drag and move operations.
 */

#include "drag.h"
#include "../virtual_mouse.h"

/**
 * @brief Move the virtual cursor to the specified coordinates.
 * Uses absolute positioning as requested.
 * @param state The gesture state (unused).
 * @param x Target X.
 * @param y Target Y.
 */
void handle_cursor_move(struct gesture_state *state, int x, int y) {
   (void)state;
   virtual_mouse_move_abs(x, y);
}

/**
 * @brief Move the virtual cursor while dragging.
 * Identical to move, but contextually happens while button is held.
 * @param state The gesture state (unused).
 * @param x Target X.
 * @param y Target Y.
 */
void handle_drag_move(struct gesture_state *state, int x, int y) {
   (void)state;
   virtual_mouse_move_abs(x, y);
}
