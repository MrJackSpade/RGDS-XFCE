/**
 * @file right_click.h
 * @brief Right-click (long press) logic.
 */

#ifndef GESTURE_RIGHT_CLICK_H
#define GESTURE_RIGHT_CLICK_H

#include "gesture_utils.h"

/**
 * @brief Check if a right-click should be triggered based on hold time.
 * @param state Current gesture state.
 * @return int 1 if triggered, 0 otherwise.
 */
int check_right_click_trigger(struct gesture_state *state);

/**
 * @brief Execute a right-click event.
 */
void perform_right_click(void);

#endif
