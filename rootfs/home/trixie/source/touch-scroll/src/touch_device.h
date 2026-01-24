/**
 * @file touch_device.h
 * @brief Header for touch device management.
 *
 * Defines structures for tracking individual touch slots (multitouch fingers)
 * and the overall device state.
 */

#ifndef TOUCH_DEVICE_H
#define TOUCH_DEVICE_H

#include <libevdev/libevdev.h>

#define MAX_SLOTS 10
#define MAX_DEVICES 4

/**
 * @brief Represents a single touch point (finger).
 */
struct touch_slot {
    int active; /**< 1 if finger is down, 0 otherwise */
    int x;      /**< Current X coordinate */
    int y;      /**< Current Y coordinate */
    int id;     /**< Tracking ID assigned by hardware */
};

/**
 * @brief Represents a physical touch device (e.g., touchscreen).
 */
struct touch_device {
    struct libevdev *evdev;         /**< libevdev context */
    int fd;                         /**< File descriptor */
    struct touch_slot slots[MAX_SLOTS]; /**< State of each multitouch slot */
    int current_slot;               /**< The slot currently being updated by events */
    int grabbed;                    /**< 1 if we have exclusive access, 0 otherwise */
    char path[512];                 /**< Device file path (e.g. /dev/input/eventX) */
};

/**
 * @brief Initialize device handling.
 */
void touch_device_init(void);

/**
 * @brief Scan for devices and return number found.
 * @return Number of devices found.
 */
int touch_device_scan(void);

/**
 * @brief Clean up all devices.
 */
void touch_device_cleanup(void);

/**
 * @brief Get access to the global devices array.
 * @param index Device index.
 * @return Pointer to device struct.
 */
struct touch_device *touch_device_get(int index);

/**
 * @brief Get the total number of devices found.
 * @return Count of devices.
 */
int touch_device_get_count(void);

#endif
