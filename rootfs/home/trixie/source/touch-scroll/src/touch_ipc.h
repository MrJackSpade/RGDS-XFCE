#ifndef TOUCH_IPC_H
#define TOUCH_IPC_H

#include <stdint.h>

#define TOUCH_IPC_SOCKET_PATH "/tmp/touch-scroll-proxy.sock"

// Message types sent FROM client TO server
#define TOUCH_IPC_MSG_REGISTER_REGION    1
#define TOUCH_IPC_MSG_UNREGISTER_REGION  2

// Message types sent FROM server TO client  
#define TOUCH_IPC_MSG_TOUCH_DOWN         10
#define TOUCH_IPC_MSG_TOUCH_MOVE         11
#define TOUCH_IPC_MSG_TOUCH_UP           12

// Region registration request
typedef struct {
    uint32_t type;          // TOUCH_IPC_MSG_REGISTER_REGION
    int32_t region_id;      // Unique ID for this region (client managed)
    int32_t screen_index;   // Which screen/touch device (0 or 1)
    int32_t x;              // Top-left X in screen coordinates
    int32_t y;              // Top-left Y in screen coordinates  
    int32_t width;          // Region width
    int32_t height;         // Region height
} TouchIpcRegisterMsg;

// Touch event forwarded to client
typedef struct {
    uint32_t type;          // TOUCH_IPC_MSG_TOUCH_DOWN, MOVE, or UP
    int32_t region_id;      // Which region this touch hit
    int32_t touch_id;       // For multi-touch tracking
    int32_t x;              // X coordinate relative to region top-left
    int32_t y;              // Y coordinate relative to region top-left
    int32_t abs_x;          // Absolute screen X (for reference)
    int32_t abs_y;          // Absolute screen Y (for reference)
} TouchIpcEventMsg;

// Generic message header for reading
typedef struct {
    uint32_t type;
} TouchIpcMsgHeader;

#endif
