/**
 * @file scroll.h
 * @brief Scrolling logic.
 */

#ifndef GESTURE_SCROLL_H
#define GESTURE_SCROLL_H

#include "gesture_utils.h"

/**
 * @brief Handle scrolling movement (two-finger drag).
 * Calculates acceleration/multiplier and sends scroll events.
 * @param state Current gesture state.
 * @param x Current X position.
 * @param y Current Y position.
 */
void handle_scroll_update(struct gesture_state *state, int x, int y);

#endif
