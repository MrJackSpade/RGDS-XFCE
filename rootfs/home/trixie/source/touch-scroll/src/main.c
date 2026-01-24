/**
 * @file main.c
 * @brief Entry point for the Touch Mouse application.
 *
 * This file sets up the main event loop, initializes the touch device and
 * virtual mouse interfaces, and processes input events via the gesture engine.
 */

#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include <poll.h>
#include <errno.h>
#include <string.h> // for strcmp
#include "touch_device.h"
#include "virtual_mouse.h"
#include "gesture_engine.h"
#include "debug.h" // Include debug support

static volatile int keep_running = 1;

/**
 * @brief Signal handler to gracefully shutdown the application.
 * @param sig The signal number caught (e.g., SIGINT).
 */
static void signal_handler(int sig) {
    (void)sig; // Unused parameter
    keep_running = 0;
}

/**
 * @brief Cleanup function called on program exit.
 *
 * Ensures that device grabs are released and the virtual mouse device is destroyed.
 */
static void cleanup(void) {
    fprintf(stderr, "Cleaning up...\n");
    touch_device_cleanup();
    virtual_mouse_cleanup();
    fprintf(stderr, "Done.\n");
}

int main(int argc, char *argv[]) {
    // Check for --debug flag
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--debug") == 0) {
            g_debug_mode = 1;
            fprintf(stderr, "Debug mode enabled.\n");
        }
    }

    // Register signal handlers for graceful termination
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    signal(SIGHUP, signal_handler);
    
    // Register cleanup function to run on exit
    atexit(cleanup);
    
    // Initialize touch device subsystem
    touch_device_init();
    
    // Scan for compatible touch devices
    // NOTE: If no devices are found, check the "Goodix" filter in touch_device.c
    if (touch_device_scan() == 0) {
        fprintf(stderr, "No supported touchscreens found.\n");
        return 1;
    }
    
    // Retrieve the first device to check its resolution
    struct touch_device *primary_dev = touch_device_get(0);
    int max_x = 4096; // Default fallback
    int max_y = 4096;
    int num_screens = touch_device_get_count();

    if (primary_dev && primary_dev->evdev) {
        const struct input_absinfo *abs_x = libevdev_get_abs_info(primary_dev->evdev, ABS_MT_POSITION_X);
        const struct input_absinfo *abs_y = libevdev_get_abs_info(primary_dev->evdev, ABS_MT_POSITION_Y);

        if (abs_x) max_x = abs_x->maximum;
        if (abs_y) max_y = abs_y->maximum;

        fprintf(stderr, "Detected Single Screen: %dx%d\n", max_x + 1, max_y + 1);
    }

    // For dual-screen setup, double the Y dimension
    int total_max_y = (max_y + 1) * num_screens - 1;
    fprintf(stderr, "Total Desktop: %dx%d (%d screens)\n", max_x + 1, total_max_y + 1, num_screens);

    // Initialize the virtual mouse (uinput) device with full desktop dimensions
    if (virtual_mouse_init(max_x, total_max_y) < 0) {
        fprintf(stderr, "Failed to create virtual mouse.\n");
        return 1;
    }
    
    // Initialize the gesture state machine
    gesture_engine_init();
    
    fprintf(stderr, "Touch Mouse Interface Ready.\n");
    
    int num_devices = touch_device_get_count();
    struct pollfd *pfds = calloc(num_devices, sizeof(struct pollfd));
    
    // Main Event Loop
    while (keep_running) {
        // Populate poll file descriptors for all active touch devices
        for (int i = 0; i < num_devices; i++) {
            struct touch_device *td = touch_device_get(i);
            pfds[i].fd = td->fd;
            pfds[i].events = POLLIN;
        }
        
        // Wait for events with a timeout (10ms tick)
        int ret = poll(pfds, num_devices, 10); 
        
        if (ret < 0) {
            if (errno == EINTR) continue; // Interrupted by signal, continue loop
            break; // Error error, exit loop
        }
        
        // Perform periodic tasks (e.g., checking for long-press timeouts)
        gesture_engine_tick();
        
        // Process any pending input events
        for (int i = 0; i < num_devices; i++) {
            if (pfds[i].revents & POLLIN) {
                struct touch_device *td = touch_device_get(i);
                struct input_event ev;
                
                // Read all available events from this device
                while (libevdev_next_event(td->evdev, LIBEVDEV_READ_FLAG_NORMAL, &ev) == LIBEVDEV_READ_STATUS_SUCCESS) {
                    gesture_engine_process(i, &ev);
                }
            }
        }
    }
    
    free(pfds);
    return 0;
}
