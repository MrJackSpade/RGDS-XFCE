/**
 * @file virtual_mouse.c
 * @brief Implementation of the virtual mouse output device.
 *
 * Uses uinput to create a virtual input device that appears to the OS as a mouse.
 * Supports relative movement (mouse-like), absolute movement (touchscreen-like),
 * clicks, and high-resolution scrolling.
 */

#include "virtual_mouse.h"
#include <libevdev/libevdev.h>
#include <stdio.h>
#include <string.h>

static struct libevdev_uinput *uinput_dev = NULL;

/**
 * @brief Initialize the virtual mouse device via uinput.
 *
 * Configures the device with:
 * - Capabilities: REL (Relative), ABS (Absolute), KEY (Buttons)
 * - Name: "Touchscreen Virtual Mouse"
 * - Vendor/Product: 0x1234 / 0x5678
 *
 * @return int 0 on success, -1 on failure.
 */
int virtual_mouse_init(int max_x, int max_y) {
    struct libevdev *dev = libevdev_new();
    if (!dev) return -1;

    libevdev_set_name(dev, "Touchscreen Virtual Mouse");
    libevdev_set_id_vendor(dev, 0x1234);
    libevdev_set_id_product(dev, 0x5678);
    libevdev_set_id_bustype(dev, BUS_USB);

    // Capabilities - Relative axes (scroll only, no REL_X/REL_Y to avoid libinput treating as mouse)
    libevdev_enable_event_type(dev, EV_REL);
    libevdev_enable_event_code(dev, EV_REL, REL_WHEEL, NULL);
    libevdev_enable_event_code(dev, EV_REL, REL_WHEEL_HI_RES, NULL);
    libevdev_enable_event_code(dev, EV_REL, REL_HWHEEL, NULL);
    libevdev_enable_event_code(dev, EV_REL, REL_HWHEEL_HI_RES, NULL);

    // Capabilities - Keys (Buttons)
    libevdev_enable_event_type(dev, EV_KEY);
    libevdev_enable_event_code(dev, EV_KEY, BTN_LEFT, NULL);
    libevdev_enable_event_code(dev, EV_KEY, BTN_RIGHT, NULL);
    libevdev_enable_event_code(dev, EV_KEY, BTN_MIDDLE, NULL);

    // Capabilities - Absolute Positioning
    libevdev_enable_event_type(dev, EV_ABS);

    // Set absolute axis info based on detected device inputs
    struct input_absinfo absinfo_x = {0, 0, max_x, 0, 0, 0};
    struct input_absinfo absinfo_y = {0, 0, max_y, 0, 0, 0};
    libevdev_enable_event_code(dev, EV_ABS, ABS_X, &absinfo_x);
    libevdev_enable_event_code(dev, EV_ABS, ABS_Y, &absinfo_y);

    // Properties - INPUT_PROP_DIRECT for touchscreen-like absolute positioning
    libevdev_enable_property(dev, INPUT_PROP_DIRECT); 

    int err = libevdev_uinput_create_from_device(dev, LIBEVDEV_UINPUT_OPEN_MANAGED, &uinput_dev);
    libevdev_free(dev);

    if (err < 0) {
        fprintf(stderr, "Failed to create uinput device: %s\n", strerror(-err));
        return -1;
    }
    return 0;
}

/**
 * @brief Destroy the virtual mouse device.
 */
void virtual_mouse_cleanup(void) {
    if (uinput_dev) {
        libevdev_uinput_destroy(uinput_dev);
        uinput_dev = NULL;
    }
}

/**
 * @brief Move the virtual mouse relatively.
 * @param dx Horizontal delta.
 * @param dy Vertical delta.
 */
void virtual_mouse_move_rel(int dx, int dy) {
    if (!uinput_dev) return;
    libevdev_uinput_write_event(uinput_dev, EV_REL, REL_X, dx);
    libevdev_uinput_write_event(uinput_dev, EV_REL, REL_Y, dy);
    libevdev_uinput_write_event(uinput_dev, EV_SYN, SYN_REPORT, 0);
}

/**
 * @brief Move the virtual mouse to an absolute position.
 * @param x X coordinate.
 * @param y Y coordinate.
 */
void virtual_mouse_move_abs(int x, int y) {
    if (!uinput_dev) return;
    libevdev_uinput_write_event(uinput_dev, EV_ABS, ABS_X, x);
    libevdev_uinput_write_event(uinput_dev, EV_ABS, ABS_Y, y);
    libevdev_uinput_write_event(uinput_dev, EV_SYN, SYN_REPORT, 0);
}

/**
 * @brief Signal touch down (finger contact).
 * Must be called before sending absolute coordinates.
 * @param x Initial X coordinate.
 * @param y Initial Y coordinate.
 */
void virtual_mouse_touch_down(int x, int y) {
    if (!uinput_dev) return;
    libevdev_uinput_write_event(uinput_dev, EV_KEY, BTN_TOUCH, 1);
    libevdev_uinput_write_event(uinput_dev, EV_ABS, ABS_X, x);
    libevdev_uinput_write_event(uinput_dev, EV_ABS, ABS_Y, y);
    libevdev_uinput_write_event(uinput_dev, EV_SYN, SYN_REPORT, 0);
}

/**
 * @brief Signal touch up (finger lifted).
 */
void virtual_mouse_touch_up(void) {
    if (!uinput_dev) return;
    libevdev_uinput_write_event(uinput_dev, EV_KEY, BTN_TOUCH, 0);
    libevdev_uinput_write_event(uinput_dev, EV_SYN, SYN_REPORT, 0);
}

/**
 * @brief Send a button click event.
 * @param button_code The EV_KEY code (e.g., BTN_LEFT).
 * @param value 1 for press, 0 for release.
 */
void virtual_mouse_click(unsigned int button_code, int value) {
    if (!uinput_dev) return;
    libevdev_uinput_write_event(uinput_dev, EV_KEY, button_code, value);
    libevdev_uinput_write_event(uinput_dev, EV_SYN, SYN_REPORT, 0);
}

/**
 * @brief Send standard scroll events (notches).
 * @param v_delta Vertical notches.
 * @param h_delta Horizontal notches.
 */
void virtual_mouse_scroll(int v_delta, int h_delta) {
    if (!uinput_dev) return;
    if (v_delta) libevdev_uinput_write_event(uinput_dev, EV_REL, REL_WHEEL, v_delta);
    if (h_delta) libevdev_uinput_write_event(uinput_dev, EV_REL, REL_HWHEEL, h_delta);
    libevdev_uinput_write_event(uinput_dev, EV_SYN, SYN_REPORT, 0);
}

/**
 * @brief Send high-resolution scroll events.
 * @param v_hires Vertical units (usually multiples of 120).
 * @param h_hires Horizontal units.
 */
/**
 * @brief Send high-resolution scroll events.
 * @param v_hires Vertical units (usually multiples of 120).
 * @param h_hires Horizontal units.
 */
void virtual_mouse_scroll_hires(int v_hires, int h_hires) {
    if (!uinput_dev) return;
    if (v_hires) {
        libevdev_uinput_write_event(uinput_dev, EV_REL, REL_WHEEL_HI_RES, v_hires);
        // FIX: Send standard low-res events for compatibility.
        // 120 units = 1 notch.
        // Simple accumulation or direct mapping:
        // Here we just map integer notches if possible.
        int v_notch = v_hires / 120;
        if (v_notch != 0) {
             libevdev_uinput_write_event(uinput_dev, EV_REL, REL_WHEEL, v_notch);
        }
    }
    if (h_hires) {
        libevdev_uinput_write_event(uinput_dev, EV_REL, REL_HWHEEL_HI_RES, h_hires);
        int h_notch = h_hires / 120;
        if (h_notch != 0) {
             libevdev_uinput_write_event(uinput_dev, EV_REL, REL_HWHEEL, h_notch);
        }
    }
    libevdev_uinput_write_event(uinput_dev, EV_SYN, SYN_REPORT, 0);
}
