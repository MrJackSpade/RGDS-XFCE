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
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <fcntl.h>
#include "touch_ipc.h"
#include "debug.h" // Include debug support

static int ipc_server_fd = -1;
#define MAX_IPC_CLIENTS 4
static int ipc_clients[MAX_IPC_CLIENTS];

static int create_ipc_server_socket(void) {
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    
    unlink(TOUCH_IPC_SOCKET_PATH);
    
    struct sockaddr_un addr = {0};
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, TOUCH_IPC_SOCKET_PATH, sizeof(addr.sun_path) - 1);
    
    if (bind(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        close(fd);
        return -1;
    }

    // Allow permissions for user-level clients (context-osk)
    chmod(TOUCH_IPC_SOCKET_PATH, 0666);

    
    if (listen(fd, 2) < 0) {
        close(fd);
        return -1;
    }
    
    fcntl(fd, F_SETFL, O_NONBLOCK);
    return fd;
}

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
    
    // Initialize IPC server
    for (int i = 0; i < MAX_IPC_CLIENTS; i++) ipc_clients[i] = -1;
    ipc_server_fd = create_ipc_server_socket();
    if (ipc_server_fd < 0) {
        fprintf(stderr, "Failed to create IPC server socket (continuing anyway)\n");
    } else {
        fprintf(stderr, "IPC Server listening on %s\n", TOUCH_IPC_SOCKET_PATH);
    }
    
    int num_devices = touch_device_get_count();
    // pfds size: devices + server + clients
    int max_pfds = num_devices + 1 + MAX_IPC_CLIENTS;
    struct pollfd *pfds = calloc(max_pfds, sizeof(struct pollfd));
    
    // Main Event Loop
    while (keep_running) {
        // Populate poll file descriptors
        int pfd_idx = 0;
        
        // 1. Touch Devices
        for (int i = 0; i < num_devices; i++) {
            struct touch_device *td = touch_device_get(i);
            pfds[pfd_idx].fd = td->fd;
            pfds[pfd_idx].events = POLLIN;
            pfds[pfd_idx].revents = 0;
            pfd_idx++;
        }
        
        // 2. IPC Server Socket
        int server_pfd_idx = -1;
        if (ipc_server_fd >= 0) {
            pfds[pfd_idx].fd = ipc_server_fd;
            pfds[pfd_idx].events = POLLIN;
            pfds[pfd_idx].revents = 0;
            server_pfd_idx = pfd_idx;
            pfd_idx++;
        }
        
        // 3. Connected Clients
        int client_pfd_indices[MAX_IPC_CLIENTS];
        for (int i = 0; i < MAX_IPC_CLIENTS; i++) {
            client_pfd_indices[i] = -1;
            if (ipc_clients[i] >= 0) {
                pfds[pfd_idx].fd = ipc_clients[i];
                pfds[pfd_idx].events = POLLIN;
                pfds[pfd_idx].revents = 0;
                client_pfd_indices[i] = pfd_idx;
                pfd_idx++;
            }
        }
        
        // Wait for events with a timeout (10ms tick)
        int ret = poll(pfds, pfd_idx, 10); 
        
        if (ret < 0) {
            if (errno == EINTR) continue; // Interrupted by signal, continue loop
            break; // Error error, exit loop
        }
        
        // Perform periodic tasks (e.g., checking for long-press timeouts)
        gesture_engine_tick();
        
        // Check Server Socket for new connections
        if (server_pfd_idx >= 0 && (pfds[server_pfd_idx].revents & POLLIN)) {
            int new_fd = accept(ipc_server_fd, NULL, NULL);
            if (new_fd >= 0) {
                fcntl(new_fd, F_SETFL, O_NONBLOCK);
                int added = 0;
                for (int i = 0; i < MAX_IPC_CLIENTS; i++) {
                    if (ipc_clients[i] == -1) {
                        ipc_clients[i] = new_fd;
                        added = 1;
                        fprintf(stderr, "IPC Client connected (fd %d)\n", new_fd);
                        break;
                    }
                }
                if (!added) {
                    close(new_fd); // Too many clients
                }
            }
        }

        // Check Clients for data
        for (int i = 0; i < MAX_IPC_CLIENTS; i++) {
            int pidx = client_pfd_indices[i];
            if (pidx >= 0 && (pfds[pidx].revents & (POLLIN | POLLHUP | POLLERR))) {
                if (pfds[pidx].revents & POLLIN) {
                    TouchIpcMsgHeader header;
                    int fd = ipc_clients[i];
                    ssize_t n = recv(fd, &header, sizeof(header), MSG_PEEK);
                    if (n > 0) {
                        if (header.type == TOUCH_IPC_MSG_REGISTER_REGION) {
                            TouchIpcRegisterMsg msg;
                            recv(fd, &msg, sizeof(msg), 0);
                            gesture_engine_register_region(msg.region_id, msg.screen_index, msg.x, msg.y, msg.width, msg.height, fd);
                        } else {
                            // Consume unknown or other messages
                             char buf[128];
                             recv(fd, buf, sizeof(buf), 0);
                        }
                    } else {
                        // Disconnect (0 or error)
                        gesture_engine_client_disconnect(ipc_clients[i]);
                        close(ipc_clients[i]);
                        ipc_clients[i] = -1;
                        fprintf(stderr, "IPC Client disconnected\n");
                    }
                } else {
                    // HUP or ERR
                    gesture_engine_client_disconnect(ipc_clients[i]);
                    close(ipc_clients[i]);
                    ipc_clients[i] = -1;
                    fprintf(stderr, "IPC Client disconnected (HUP/ERR)\n");
                }
            }
        }
        
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
