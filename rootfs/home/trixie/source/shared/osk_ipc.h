#ifndef OSK_IPC_H
#define OSK_IPC_H

#include <stdint.h>

#define OSK_SOCKET_PATH "/tmp/context-osk.sock"

// Message types sent FROM context-osk TO touch-scroll
#define OSK_MSG_REGISTER_REGION    1
#define OSK_MSG_UNREGISTER_REGION  2

// Message types sent FROM touch-scroll TO context-osk  
#define OSK_MSG_TOUCH_DOWN         10
#define OSK_MSG_TOUCH_MOVE         11
#define OSK_MSG_TOUCH_UP           12

// Region registration request
typedef struct {
    uint32_t type;          // OSK_MSG_REGISTER_REGION
    int32_t region_id;      // Unique ID for this region (use 1 for now)
    int32_t screen_index;   // Which screen/touch device (0 or 1)
    int32_t x;              // Top-left X in screen coordinates
    int32_t y;              // Top-left Y in screen coordinates  
    int32_t width;          // Region width
    int32_t height;         // Region height
} OskRegisterMsg;

// Touch event forwarded to OSK
typedef struct {
    uint32_t type;          // OSK_MSG_TOUCH_DOWN, MOVE, or UP
    int32_t region_id;      // Which region this touch hit
    int32_t touch_id;       // For multi-touch tracking
    int32_t x;              // X coordinate relative to region top-left
    int32_t y;              // Y coordinate relative to region top-left
    int32_t abs_x;          // Absolute screen X (for reference)
    int32_t abs_y;          // Absolute screen Y (for reference)
} OskTouchEventMsg;

// Generic message header for reading
typedef struct {
    uint32_t type;
} OskMsgHeader;

#endif
