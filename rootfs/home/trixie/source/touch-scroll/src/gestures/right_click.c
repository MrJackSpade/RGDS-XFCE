/**
 * @file right_click.c
 * @brief Implementation of right-click logic.
 */

#include "right_click.h"
#include "../virtual_mouse.h"
#include <linux/input.h>
#include <stdio.h>

/**
 * @brief Checks if the user has held their finger down long enough to trigger a right-click.
 * @param state Current gesture context.
 * @return int 1 if timeout exceeded, 0 otherwise.
 */
int check_right_click_trigger(struct gesture_state *state) {
    long long now = current_time_ms();
    if (now - state->start_time_ms >= LONG_PRESS_TIMEOUT_MS) {
        return 1;
    }
    return 0;
}

/**
 * @brief Executes a right-click.
 * Emulates a Right Button Down and Right Button Up event sequence.
 */
void perform_right_click(void) {
    virtual_mouse_click(BTN_RIGHT, 1);
    virtual_mouse_click(BTN_RIGHT, 0);
    fprintf(stderr, "Right click triggered\n");
}
