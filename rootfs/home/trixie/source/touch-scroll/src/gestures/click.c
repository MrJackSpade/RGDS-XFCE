/**
 * @file click.c
 * @brief Implementation of click event handling.
 */

#include "click.h"
#include "../virtual_mouse.h"
#include <linux/input.h>
#include <stdlib.h> // abs

/**
 * @brief Execute a left click.
 * Sends a Button Down followed immediately by Button Up.
 * @return int Always returns 1.
 */
int handle_click_state(void) {
    // Immediate click execution.
    virtual_mouse_click(BTN_LEFT, 1);
    virtual_mouse_click(BTN_LEFT, 0);
    return 1;
}
