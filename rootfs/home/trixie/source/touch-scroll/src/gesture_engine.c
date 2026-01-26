/**
 * @file gesture_engine.c
 * @brief Core State Machine for Touch Gestures.
 *
 * This file implements the main gesture recognition logic. It tracks the state of
 * fingers on the screen, detects gestures (tap, drag, scroll, right-click),
 * and dispatches actions to the virtual mouse.
 */

#include "gesture_engine.h"
#include "gestures/gesture_utils.h"
#include "gestures/click.h"
#include "gestures/drag.h"
#include "gestures/scroll.h"
#include "gestures/right_click.h"
#include "touch_device.h"
#include "virtual_mouse.h"
#include "debug.h" // Include debug logging
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string.h>
#include <sys/socket.h>
#include "touch_ipc.h"

#define MAX_REGISTERED_REGIONS 4

typedef struct {
    int active;
    int region_id;
    int screen_index;
    int x, y, width, height;
    int client_fd;
} RegisteredRegion;

static RegisteredRegion registered_regions[MAX_REGISTERED_REGIONS];
static int g_intercept_active_region = -1; // Index in registered_regions array, or -1 if none


static struct gesture_state g_state = {0};

// Screen configuration for dual-display setup
// Device 0 (event3) = bottom screen (DSI-1), y offset 480
// Device 1 (event2) = top screen (DSI-2), y offset 0
static int g_screen_y_offset[2] = {480, 0};  // Y offset per device
static int g_screen_height = 480;            // Height of each screen

/**
 * @brief Transform touch coordinates to screen coordinates.
 * @param dev_idx Device index (0 or 1).
 * @param x Touch X coordinate (passed through unchanged).
 * @param y Touch Y coordinate (transformed based on device).
 * @param out_x Output X coordinate.
 * @param out_y Output Y coordinate.
 */
static void transform_coords(int dev_idx, int x, int y, int *out_x, int *out_y) {
    *out_x = x;
    if (dev_idx >= 0 && dev_idx < 2) {
        *out_y = (y * g_screen_height / 480) + g_screen_y_offset[dev_idx];
    } else {
        *out_y = y;
    }
}

/**
 * @brief Initialize the gesture engine state.
 */
void gesture_engine_init(void) {
    memset(&g_state, 0, sizeof(g_state));
    g_state.state = GESTURE_STATE_IDLE;
    DEBUG_LOG("Gesture Engine Initialized (State: IDLE)\n");
}

/**
 * @brief recalculate the number of active fingers across all devices.
 * Updates g_state.fingers_count.
 */
static void update_finger_count(void) {
    int count = 0;
    int dev_count = touch_device_get_count();
    
    // Simple sum of all active slots across all devices
    for (int i = 0; i < dev_count; i++) {
        struct touch_device *td = touch_device_get(i);
        if (!td) continue;
        for (int s = 0; s < MAX_SLOTS; s++) {
            if (td->slots[s].active) count++;
        }
    }
    
    if (g_state.fingers_count != count) {
        DEBUG_LOG("Finger count changed: %d -> %d\n", g_state.fingers_count, count);
        g_state.fingers_count = count;
    }
}

/**
 * @brief Handle logic when a finger touches down.
 * @param x X coordinate of the touch.
 * @param y Y coordinate of the touch.
 */
static void handle_touch_down(int x, int y) {
    long long now = current_time_ms();

    if (g_state.state == GESTURE_STATE_IDLE) {
        // First finger down -> Start Touch Sequence
        g_state.state = GESTURE_STATE_TOUCH_START;
        DEBUG_LOG("State: IDLE -> TOUCH_START\n");
        g_state.start_time_ms = now;
        g_state.start_x = x;
        g_state.start_y = y;
        g_state.last_x = x;
        g_state.last_y = y;
        // Immediately position cursor at touch point (absolute mode)
        virtual_mouse_move_abs(x, y);
    } else if (g_state.state == GESTURE_STATE_CLICK_WAIT) {
        // Finger down again while waiting for double-tap
        long long diff = now - g_state.start_time_ms;
        if (diff < DOUBLE_TAP_TIMEOUT_MS) {
            // Success: Double tap detected -> Start Dragging
            g_state.state = GESTURE_STATE_DRAG_START;
            DEBUG_LOG("State: CLICK_WAIT -> DRAG_START (Double Tap)\n");
            // Position cursor and engage left button for drag
            virtual_mouse_move_abs(x, y);
            virtual_mouse_click(BTN_LEFT, 1);
        } else {
            // Too slow, treat as a new tap
            g_state.state = GESTURE_STATE_TOUCH_START;
            DEBUG_LOG("State: CLICK_WAIT -> TOUCH_START (Timeout)\n");
            g_state.start_time_ms = now;
            virtual_mouse_move_abs(x, y);
        }
        g_state.start_x = x;
        g_state.start_y = y;
        g_state.last_x = x;
        g_state.last_y = y;
    } else if (g_state.state == GESTURE_STATE_TOUCH_START || g_state.state == GESTURE_STATE_MOVING) {
        // Second finger down detected
        if (g_state.fingers_count >= 2) {
             DEBUG_LOG("State: %s -> SCROLLING (2+ fingers)\n",
                 (g_state.state == GESTURE_STATE_TOUCH_START) ? "TOUCH_START" : "MOVING");
             g_state.state = GESTURE_STATE_SCROLLING;
             // Reset tracking for scrolling
             g_state.last_x = x;
             g_state.last_y = y;
        }
    }
}

/**
 * @brief Handle logic when a finger is lifted.
 */
static void handle_touch_up(void) {
    if (g_state.fingers_count == 0) {
        // If we were intercepting, end it
        if (g_intercept_active_region != -1) {
            if (registered_regions[g_intercept_active_region].active) {
                TouchIpcEventMsg msg = {
                    .type = TOUCH_IPC_MSG_TOUCH_UP,
                    .region_id = registered_regions[g_intercept_active_region].region_id,
                    .touch_id = 0,
                    .x = 0, // Last known? Or 0
                    .y = 0,
                    .abs_x = 0,
                    .abs_y = 0
                };
                send(registered_regions[g_intercept_active_region].client_fd, &msg, sizeof(msg), MSG_NOSIGNAL);
            }
            g_intercept_active_region = -1;
            DEBUG_LOG("Intercept Region Ended\n");
            g_state.state = GESTURE_STATE_IDLE; // Ensure state is idle
            return;
        }

        // All fingers lifted
        if (g_state.state == GESTURE_STATE_TOUCH_START) {
            // Finger down then up quickly without moving much -> CLICK
            handle_click_state();
            g_state.state = GESTURE_STATE_CLICK_WAIT;
            DEBUG_LOG("State: TOUCH_START -> CLICK_WAIT\n");
            g_state.start_time_ms = current_time_ms(); // Start timer for potential second tap
        } else if (g_state.state == GESTURE_STATE_DRAG_START || g_state.state == GESTURE_STATE_DRAGGING) {
            // Lifted during drag -> End Drag (Release Button)
            virtual_mouse_click(BTN_LEFT, 0);
            g_state.state = GESTURE_STATE_IDLE;
            DEBUG_LOG("State: DRAGGING -> IDLE\n");
        } else {
            // Reset to idle for other states (Moving, Scrolling, etc)
            if (g_state.state != GESTURE_STATE_IDLE) {
                DEBUG_LOG("State: %d -> IDLE\n", g_state.state);
            }
            g_state.state = GESTURE_STATE_IDLE;
        }
    } else if (g_state.state == GESTURE_STATE_SCROLLING && g_state.fingers_count < 2) {
        // If scrolling and we drop to 1 finger, detect what to do.
        // For now, revert to moving (cursor control)
        g_state.state = GESTURE_STATE_MOVING;
        DEBUG_LOG("State: SCROLLING -> MOVING (Dropped finger)\n");
        virtual_mouse_move_abs(g_state.last_x, g_state.last_y);
    }
}

/**
 * @brief Handle movement updates.
 * @param x Current X.
 * @param y Current Y.
 */
static void handle_motion(int x, int y) {
    int dx = abs(x - g_state.start_x);
    int dy = abs(y - g_state.start_y);
    int dist = dx*dx + dy*dy; // Squared distance
    
    // Check if we moved enough to transition from TAP to MOVE
    if (g_state.state == GESTURE_STATE_TOUCH_START) {
        // FIX: Check for scrolling HERE as well, in case slight movement happened before 2nd finger logic
        // or if 2nd finger appeared during "motion" processing.
        if (g_state.fingers_count >= 2) {
            g_state.state = GESTURE_STATE_SCROLLING;
            g_state.last_x = x;
            g_state.last_y = y;
            DEBUG_LOG("State: TOUCH_START -> SCROLLING (2+ fingers detected during check)\n");
            return;
        }

        if (dist > MOVE_THRESHOLD * MOVE_THRESHOLD) {
            g_state.state = GESTURE_STATE_MOVING;
            DEBUG_LOG("State: TOUCH_START -> MOVING (Dist: %d)\n", dist);
        }
        // Update cursor position even before transition to MOVING
        virtual_mouse_move_abs(x, y);
    }

    // Dispatch movement to appropriate handler
    if (g_state.state == GESTURE_STATE_MOVING) {
        // FIX: Allow transition to SCROLLING from MOVING if 2nd finger appears
        if (g_state.fingers_count >= 2) {
             g_state.state = GESTURE_STATE_SCROLLING;
             g_state.last_x = x;
             g_state.last_y = y;
             DEBUG_LOG("State: MOVING -> SCROLLING (2+ fingers detected)\n");
             return;
        }
        DEBUG_LOG("Motion: Moving Cursor to %d, %d\n", x, y);
        handle_cursor_move(&g_state, x, y);
    } else if (g_state.state == GESTURE_STATE_DRAG_START) {
        if (dist > MOVE_THRESHOLD * MOVE_THRESHOLD / 2) { // Lower threshold for drag
             g_state.state = GESTURE_STATE_DRAGGING;
             DEBUG_LOG("State: DRAG_START -> DRAGGING\n");
        }
    } else if (g_state.state == GESTURE_STATE_DRAGGING) {
        handle_drag_move(&g_state, x, y);
    } else if (g_state.state == GESTURE_STATE_SCROLLING) {
        handle_scroll_update(&g_state, x, y);
    }
    
    // Update last known position for delta calculations
    g_state.last_x = x;
    g_state.last_y = y;
}

/**
 * @brief Process a single raw input event from libevdev.
 * @param dev_idx device index to process for.
 * @param ev The input event.
 */
void gesture_engine_process(int dev_idx, struct input_event *ev) {
    struct touch_device *td = touch_device_get(dev_idx);
    if (!td) return;

    if (ev->type == EV_ABS) {
        switch (ev->code) {
            case ABS_MT_SLOT:
                td->current_slot = ev->value;
                if (td->current_slot >= MAX_SLOTS) td->current_slot = 0;
                break;
                
            case ABS_MT_TRACKING_ID:
                if (ev->value == -1) {
                    td->slots[td->current_slot].active = 0;
                } else {
                    td->slots[td->current_slot].active = 1;
                    td->slots[td->current_slot].id = ev->value;
                }
                update_finger_count();
                if (ev->value == -1) handle_touch_up();
                break;
                
            case ABS_MT_POSITION_X:
                if (td->slots[td->current_slot].active) {
                    td->slots[td->current_slot].x = ev->value;
                }
                break;
                
            case ABS_MT_POSITION_Y:
                if (td->slots[td->current_slot].active) {
                    td->slots[td->current_slot].y = ev->value;
                }
                break;
        }
    } else if (ev->type == EV_SYN && ev->code == SYN_REPORT) {
        // End of frame. Process the current state of fingers.
        int active_fingers = g_state.fingers_count;
        if (active_fingers > 0) {
            // Find primary finger (e.g. first active slot found)
            int tx = 0, ty = 0;
            int found_dev = -1;

            for (int i = 0; i < touch_device_get_count(); i++) {
                struct touch_device *d = touch_device_get(i);
                for (int s = 0; s < MAX_SLOTS; s++) {
                    if (d->slots[s].active) {
                        tx = d->slots[s].x;
                        ty = d->slots[s].y;
                        found_dev = i;
                        break;
                    }
                }
                if (found_dev >= 0) break;
            }

            if (found_dev >= 0) {
                // Transform coordinates based on which device
                int screen_x, screen_y;
                transform_coords(found_dev, tx, ty, &screen_x, &screen_y);

                // --- INTERCEPT CHECK START ---
                if (g_intercept_active_region != -1) {
                    // Already intercepting, continue sending MOVE
                    if (registered_regions[g_intercept_active_region].active) {
                        RegisteredRegion *r = &registered_regions[g_intercept_active_region];
                        TouchIpcEventMsg msg = {
                            .type = TOUCH_IPC_MSG_TOUCH_MOVE,
                            .region_id = r->region_id,
                            .touch_id = 0, // simplified
                            .x = screen_x - r->x,
                            .y = screen_y - r->y,
                            .abs_x = screen_x,
                            .abs_y = screen_y
                        };
                        send(r->client_fd, &msg, sizeof(msg), MSG_NOSIGNAL);
                    }
                    return; // Skip normal processing
                }
                
                // Not currently intercepting, check if we should start
                if (g_state.state == GESTURE_STATE_IDLE || g_state.state == GESTURE_STATE_CLICK_WAIT) { 
                    for (int i = 0; i < MAX_REGISTERED_REGIONS; i++) {
                        if (!registered_regions[i].active) continue;
                        if (registered_regions[i].screen_index != found_dev) continue;
                        
                        // Hit test
                        if (screen_x >= registered_regions[i].x && 
                            screen_x < registered_regions[i].x + registered_regions[i].width &&
                            screen_y >= registered_regions[i].y && 
                            screen_y < registered_regions[i].y + registered_regions[i].height) {
                            
                            // Start Intercept
                            g_intercept_active_region = i;
                            DEBUG_LOG("Intercept Region Started (ID: %d)\n", registered_regions[i].region_id);
                            
                            TouchIpcEventMsg msg = {
                                .type = TOUCH_IPC_MSG_TOUCH_DOWN,
                                .region_id = registered_regions[i].region_id,
                                .touch_id = 0,
                                .x = screen_x - registered_regions[i].x,
                                .y = screen_y - registered_regions[i].y,
                                .abs_x = screen_x,
                                .abs_y = screen_y
                            };
                            send(registered_regions[i].client_fd, &msg, sizeof(msg), MSG_NOSIGNAL);
                            return; // Skip normal processing
                        }
                    }
                }
                // --- INTERCEPT CHECK END ---

                // Track active device for gesture state
                if (g_state.state == GESTURE_STATE_IDLE) {
                    g_state.active_device = found_dev;
                }

                if (g_state.state == GESTURE_STATE_IDLE || g_state.state == GESTURE_STATE_CLICK_WAIT) {
                    handle_touch_down(screen_x, screen_y);
                } else {
                    handle_motion(screen_x, screen_y);
                }
            }
        }
    }
}

/**
 * @brief Periodic tick function.
 * Should be called periodically (e.g., every 10ms) to handle timeouts.
 */
void gesture_engine_tick(void) {
    long long now = current_time_ms();
    
    // Check for Long Press (Right Click)
    if (g_state.state == GESTURE_STATE_TOUCH_START) {
        if (check_right_click_trigger(&g_state)) {
            perform_right_click();
            g_state.state = GESTURE_STATE_IDLE; // Reset after right click
            DEBUG_LOG("State: TOUCH_START -> IDLE (Right Click Triggered)\n");
        }
    }
    
    // Check for Double Tap Timeout (Click Wait)
    if (g_state.state == GESTURE_STATE_CLICK_WAIT) {
        if (now - g_state.start_time_ms > DOUBLE_TAP_TIMEOUT_MS) {
            // Timed out waiting for second tap.
            // Since we already performed the 'click' on the first Up event,
            // we simply reset to IDLE.
            g_state.state = GESTURE_STATE_IDLE;
            DEBUG_LOG("State: CLICK_WAIT -> IDLE (Double Tap Timeout)\n");
        }
    }
}

void gesture_engine_register_region(int region_id, int screen_idx, int x, int y, int w, int h, int client_fd) {
    // 1. Check if region already exists for this client (Update it)
    for (int i = 0; i < MAX_REGISTERED_REGIONS; i++) {
        if (registered_regions[i].active && 
            registered_regions[i].region_id == region_id && 
            registered_regions[i].client_fd == client_fd) {
            
            registered_regions[i].screen_index = screen_idx;
            registered_regions[i].x = x;
            registered_regions[i].y = y;
            registered_regions[i].width = w;
            registered_regions[i].height = h;
            DEBUG_LOG("Updated region %d: Screen %d, [%d, %d] %dx%d\n", region_id, screen_idx, x, y, w, h);
            return;
        }
    }

    // 2. Find new slot
    for (int i = 0; i < MAX_REGISTERED_REGIONS; i++) {
        if (!registered_regions[i].active) {
            registered_regions[i].active = 1;
            registered_regions[i].region_id = region_id;
            registered_regions[i].screen_index = screen_idx;
            registered_regions[i].x = x;
            registered_regions[i].y = y;
            registered_regions[i].width = w;
            registered_regions[i].height = h;
            registered_regions[i].client_fd = client_fd;
            DEBUG_LOG("Registered region %d: Screen %d, [%d, %d] %dx%d\n", region_id, screen_idx, x, y, w, h);
            return;
        }
    }
    DEBUG_LOG("Failed to register region: storage full\n");
}

void gesture_engine_unregister_region(int region_id) {
    for (int i = 0; i < MAX_REGISTERED_REGIONS; i++) {
        if (registered_regions[i].active && registered_regions[i].region_id == region_id) {
            registered_regions[i].active = 0;
            if (g_intercept_active_region == i) {
                g_intercept_active_region = -1;
            }
            break;
        }
    }
}

void gesture_engine_client_disconnect(int client_fd) {
    for (int i = 0; i < MAX_REGISTERED_REGIONS; i++) {
        if (registered_regions[i].active && registered_regions[i].client_fd == client_fd) {
            registered_regions[i].active = 0;
            if (g_intercept_active_region == i) {
                g_intercept_active_region = -1;
            }
        }
    }
}
