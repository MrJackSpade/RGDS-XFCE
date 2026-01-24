/**
 * @file gesture_utils.h
 * @brief Utility definitions for gesture processing.
 *
 * Defines the comprehensive list of gesture states and configuration constants/timing.
 */

#ifndef GESTURE_UTILS_H
#define GESTURE_UTILS_H

#include <sys/time.h>
#include "../touch_device.h"

// Timeouts and Thresholds
#define CLICK_TIMEOUT_MS 200        /**< Max duration for a tap to be considered a click */
#define LONG_PRESS_TIMEOUT_MS 600   /**< Duration to hold for right-click */
#define DOUBLE_TAP_TIMEOUT_MS 300   /**< Max gap between taps for double-tap/drag */
#define MOVE_THRESHOLD 5            /**< Movement distance threshold to detect intent to move */

/**
 * @brief Enum representing the current state of the gesture engine.
 */
typedef enum {
    GESTURE_STATE_IDLE,             /**< No activity */
    GESTURE_STATE_TOUCH_START,      /**< One finger down, analyzing intent */
    GESTURE_STATE_MOVING,           /**< One finger moving (Cursor Control) */
    GESTURE_STATE_CLICK_WAIT,       /**< One finger released, waiting for potential double-tap */
    GESTURE_STATE_DRAG_START,       /**< Double-tap detected, checking for move */
    GESTURE_STATE_DRAGGING,         /**< Dragging (Left button held) */
    GESTURE_STATE_SCROLLING,        /**< Two or more fingers moving */
    GESTURE_STATE_RIGHT_CLICK_WAIT  /**< (Deprecated/Unused?) Waiting for right click timeout */
} GestureState;

/**
 * @brief Context structure holding the state of the gesture recognition.
 */
struct gesture_state {
    GestureState state;         /**< Current state machine state */
    long long start_time_ms;    /**< Timestamp when current state/gesture started */
    int start_x;                /**< X position at start of gesture */
    int start_y;                /**< Y position at start of gesture */
    int last_x;                 /**< Last known X position */
    int last_y;                 /**< Last known Y position */
    int fingers_count;          /**< Number of active fingers */
    int active_device;          /**< Device index that initiated the gesture */
};

/**
 * @brief Helper to get current monotonic time in milliseconds.
 * @return long long Timestamp in ms.
 */
long long current_time_ms(void);

#endif
