/**
 * @file drag.h
 * @brief Cursor movement and Dragging logic.
 */

#ifndef GESTURE_DRAG_H
#define GESTURE_DRAG_H

#include "gesture_utils.h"

/**
 * @brief Move the cursor (absolute positioning).
 * @param state Current gesture state.
 * @param x New X coordinate.
 * @param y New Y coordinate.
 */
void handle_cursor_move(struct gesture_state *state, int x, int y);

/**
 * @brief Handle dragging movement (Button held down).
 * @param state Current gesture state.
 * @param x New X coordinate.
 * @param y New Y coordinate.
 */
void handle_drag_move(struct gesture_state *state, int x, int y);

#endif
