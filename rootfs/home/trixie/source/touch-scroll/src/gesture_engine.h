/**
 * @file gesture_engine.h
 * @brief Header for the gesture recognition engine.
 *
 * Defines the public interface for initializing and driving the gesture engine.
 */

#ifndef GESTURE_ENGINE_H
#define GESTURE_ENGINE_H

#include <linux/input.h>

/**
 * @brief Initialize the gesture engine.
 * Resets internal state to IDLE.
 */
void gesture_engine_init(void);

/**
 * @brief Process incoming input events from a touch device.
 * @param dev_idx Index of device in touch_device list.
 * @param ev The input event to process.
 */
void gesture_engine_process(int dev_idx, struct input_event *ev);

/**
 * @brief Run periodic checks (timers for long press, etc).
 * Should be called at a regular interval (e.g. 10ms).
 */
void gesture_engine_tick(void);

#endif
