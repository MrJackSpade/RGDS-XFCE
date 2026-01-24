/**
 * @file virtual_mouse.h
 * @brief Header for virtual mouse output.
 *
 * Defines the interface for sending mouse events to the system.
 */

#ifndef VIRTUAL_MOUSE_H
#define VIRTUAL_MOUSE_H

#include <libevdev/libevdev-uinput.h>

/**
 * @brief Initialize the virtual mouse device.
 * @param max_x Maximum X value for absolute positioning.
 * @param max_y Maximum Y value for absolute positioning.
 * @return 0 on success, -1 on failure.
 */
int virtual_mouse_init(int max_x, int max_y);

/**
 * @brief Destroy the virtual mouse device.
 */
void virtual_mouse_cleanup(void);

/**
 * @brief Send relative movement (Standard mouse behavior).
 * @param dx Horizontal delta.
 * @param dy Vertical delta.
 */
void virtual_mouse_move_rel(int dx, int dy);

/**
 * @brief Send absolute position (Touchscreen/Tablet behavior).
 * @param x X coordinate.
 * @param y Y coordinate.
 */
void virtual_mouse_move_abs(int x, int y);

/**
 * @brief Signal touch down with initial position.
 * @param x Initial X coordinate.
 * @param y Initial Y coordinate.
 */
void virtual_mouse_touch_down(int x, int y);

/**
 * @brief Signal touch up (finger lifted).
 */
void virtual_mouse_touch_up(void);

/**
 * @brief Send button events.
 * @param button_code EV_KEY code (e.g., BTN_LEFT).
 * @param value 1 = pressed, 0 = released.
 */
void virtual_mouse_click(unsigned int button_code, int value);

/**
 * @brief Send standard scroll events.
 * @param v_delta Vertical scroll notches.
 * @param h_delta Horizontal scroll notches.
 */
void virtual_mouse_scroll(int v_delta, int h_delta);

/**
 * @brief Send high-resolution scroll events.
 * @param v_hires Vertical units.
 * @param h_hires Horizontal units.
 */
void virtual_mouse_scroll_hires(int v_hires, int h_hires);

#endif
