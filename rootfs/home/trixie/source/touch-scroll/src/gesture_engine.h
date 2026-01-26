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

/**
 * @brief Register a region for touch interception.
 */
void gesture_engine_register_region(int region_id, int screen_idx, int x, int y, int w, int h, int client_fd);

/**
 * @brief Unregister a region.
 */
void gesture_engine_unregister_region(int region_id);

/**
 * @brief Handle client disconnection (cleanup regions).
 */
void gesture_engine_client_disconnect(int client_fd);

#endif
