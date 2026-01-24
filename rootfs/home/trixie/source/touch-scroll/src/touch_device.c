/**
 * @file touch_device.c
 * @brief Implementation of touch device discovery and management.
 *
 * This module scans /dev/input for compatible touchscreen events, grabs exclusive access,
 * and manages the lifecycle of touch devices using libevdev.
 */

#include "touch_device.h"
#include "debug.h" // Include debug logging
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <linux/input.h>

static struct touch_device devices[MAX_DEVICES];
static int num_devices = 0;

/**
 * @brief Initialize the touch device storage.
 */
void touch_device_init(void) {
    memset(devices, 0, sizeof(devices));
    num_devices = 0;
}

/**
 * @brief Scan for compatible touch devices in /dev/input.
 *
 * Scans directory for event devices. Valid devices must:
 * 1. Have "Goodix" in the name (WARNING: This is specific to one hardware type!)
 * 2. Support Multitouch (EV_ABS/ABS_MT_SLOT)
 * 3. Support Touch Button (EV_KEY/BTN_TOUCH)
 *
 * @return int Number of devices found.
 */
int touch_device_scan(void) {
    DEBUG_LOG("Scanning /dev/input for devices...\n");
    DIR *dir = opendir("/dev/input");
    if (!dir) {
        DEBUG_LOG("Failed to open /dev/input directory.\n");
        return 0;
    }

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL && num_devices < MAX_DEVICES) {
        if (strncmp(entry->d_name, "event", 5) != 0) continue;

        char devpath[512];
        snprintf(devpath, sizeof(devpath), "/dev/input/%s", entry->d_name);
        
        DEBUG_LOG("Checking device: %s\n", devpath);

        int fd = open(devpath, O_RDONLY | O_NONBLOCK);
        if (fd < 0) {
            DEBUG_LOG("  -> Failed to open %s (errno=%d)\n", devpath, errno);
            continue;
        }

        struct libevdev *dev = NULL;
        if (libevdev_new_from_fd(fd, &dev) < 0) {
            DEBUG_LOG("  -> Failed to create libevdev instance for %s\n", devpath);
            close(fd);
            continue;
        }

        const char *name = libevdev_get_name(dev);
        DEBUG_LOG("  -> Name: '%s'\n", name ? name : "NULL");
        
        int has_mt = libevdev_has_event_code(dev, EV_ABS, ABS_MT_SLOT);
        int has_touch = libevdev_has_event_code(dev, EV_KEY, BTN_TOUCH);
        
        DEBUG_LOG("  -> Capabilities: MT=%d, TOUCH=%d\n", has_mt, has_touch);

        // Filter for Goodix touchscreens.
        // WARNING: This filter is extremely restrictive. If your device is not a "Goodix" screen,
        // it will be ignored here. To fix, remove '&& strstr(name, "Goodix")' or change the target name.
        if (name && strstr(name, "Goodix") && has_mt && has_touch) {
            // Grab the device to prevent other listeners (like X11/Wayland) from processing events
            int grab_result = ioctl(fd, EVIOCGRAB, 1);
            if (grab_result < 0) {
                fprintf(stderr, "Warning: Could not grab %s: %s\n", devpath, strerror(errno));
                devices[num_devices].grabbed = 0;
            } else {
                fprintf(stderr, "Grabbed exclusive access to %s\n", devpath);
                DEBUG_LOG("  -> Successfully grabbed %s\n", devpath);
                devices[num_devices].grabbed = 1;
            }
            
            devices[num_devices].evdev = dev;
            devices[num_devices].fd = fd;
            snprintf(devices[num_devices].path, sizeof(devices[num_devices].path), "%s", devpath);
            devices[num_devices].current_slot = 0;
            memset(devices[num_devices].slots, 0, sizeof(devices[num_devices].slots));
            
            fprintf(stderr, "Found touchscreen %d: %s\n", num_devices, devpath);
            num_devices++;
            
            
            // Reverted: User has dual screens, so we must scan all devices.
            // Duplicate counting will have to be accepted or handled via smart ID tracking if it becomes an issue.
            DEBUG_LOG("  -> Registered valid device %d\n", num_devices - 1);
            
        } else {
            if (!has_mt) DEBUG_LOG("  -> REJECTED provided capabilities (No Multitouch)\n");
            else if (!has_touch) DEBUG_LOG("  -> REJECTED provided capabilities (No Touch Button)\n");
            else if (!strstr(name, "Goodix")) DEBUG_LOG("  -> REJECTED name mismatch (Expected 'Goodix')\n");
            
            libevdev_free(dev);
            close(fd);
        }
    }
    closedir(dir);
    return num_devices;
}

/**
 * @brief Clean up and release all touch devices.
 *
 * Ungrabs devices and closes file descriptors.
 */
void touch_device_cleanup(void) {
    for (int i = 0; i < num_devices; i++) {
        if (devices[i].grabbed && devices[i].fd >= 0) {
            ioctl(devices[i].fd, EVIOCGRAB, 0); // Release exclusive grab
            devices[i].grabbed = 0;
        }
        if (devices[i].evdev) {
            libevdev_free(devices[i].evdev);
            devices[i].evdev = NULL;
        }
        if (devices[i].fd >= 0) {
            close(devices[i].fd);
            devices[i].fd = -1;
        }
    }
    num_devices = 0;
}

/**
 * @brief Get a pointer to a touch device struct.
 * @param index Index of the device.
 * @return struct touch_device* Pointer to the device or NULL if invalid.
 */
struct touch_device *touch_device_get(int index) {
    if (index < 0 || index >= num_devices) return NULL;
    return &devices[index];
}

/**
 * @brief Get the total count of found devices.
 * @return int Number of devices.
 */
int touch_device_get_count(void) {
    return num_devices;
}
