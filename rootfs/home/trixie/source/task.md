# task.md - OSK Touch Input Integration

## OBJECTIVE

Modify `touch-scroll` (mouse proxy service) and `context-osk` (on-screen keyboard) so that touch events occurring within the OSK's screen region are intercepted by the proxy and forwarded to the OSK via Unix socket, instead of being passed through to the unified virtual mouse device.

---

## PROBLEM STATEMENT

Current behavior:
1. `touch-scroll` grabs both physical touchscreen devices using EVIOCGRAB
2. `touch-scroll` processes gestures and forwards events to a unified virtual mouse device
3. Fullscreen applications on Screen 1 receive ALL touch events, including those meant for the OSK on Screen 2
4. `context-osk` cannot receive touch input because the underlying device is grabbed by `touch-scroll`

Desired behavior:
1. `touch-scroll` intercepts touches that occur within a registered rectangular region (the OSK area)
2. Those intercepted touches are sent to `context-osk` via Unix socket IPC
3. Touches outside the registered region continue to flow through to the unified virtual device as normal
4. `context-osk` receives touch coordinates and handles key logic internally

---

## ARCHITECTURE OVERVIEW

```
┌─────────────────────────────────────────────────────────────┐
│                      touch-scroll                           │
│                                                             │
│  [Physical Touch Devices] ──► [Event Loop]                  │
│                                    │                        │
│                                    ▼                        │
│                          ┌────────────────────┐             │
│                          │ Is touch inside a  │             │
│                          │ registered region? │             │
│                          └────────┬───────────┘             │
│                                   │                         │
│                     YES           │          NO             │
│                 ┌─────────────────┴──────────────┐          │
│                 ▼                                ▼          │
│     [Send to region owner via socket]    [Normal processing]│
│                 │                                │          │
│                 ▼                                ▼          │
│         context-osk                    Unified virtual mouse│
└─────────────────────────────────────────────────────────────┘
```

---

## IMPLEMENTATION TASKS

Complete these tasks in order. Do not skip ahead. Do not refactor unrelated code.

### PHASE 1: Define the IPC Protocol

Create a new shared header file that both projects will include.

**Create file: `shared/osk_ipc.h`**

This header defines:

```c
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
```

**DO NOT** add any other message types. This is the complete protocol.

---

### PHASE 2: Modify touch-scroll (Server Side)

#### Task 2.1: Add region storage

Add to touch-scroll's state/context structure:

```c
#define MAX_REGISTERED_REGIONS 4

typedef struct {
    int active;
    int region_id;
    int screen_index;
    int x, y, width, height;
    int client_fd;  // Socket FD to send events to
} RegisteredRegion;

// Add to your main state struct:
RegisteredRegion registered_regions[MAX_REGISTERED_REGIONS];
int osk_server_fd;  // Listening socket
```

#### Task 2.2: Create Unix socket server

At startup, create and listen on the Unix socket:

```c
int create_osk_server_socket(void) {
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    
    unlink(OSK_SOCKET_PATH);  // Remove old socket file
    
    struct sockaddr_un addr = {0};
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, OSK_SOCKET_PATH, sizeof(addr.sun_path) - 1);
    
    bind(fd, (struct sockaddr*)&addr, sizeof(addr));
    listen(fd, 2);
    
    // Make non-blocking
    fcntl(fd, F_SETFL, O_NONBLOCK);
    
    return fd;
}
```

**Call this once during touch-scroll initialization.**

#### Task 2.3: Accept client connections

In the main event loop, check for new connections:

```c
void check_osk_connections(int server_fd) {
    int client_fd = accept(server_fd, NULL, NULL);
    if (client_fd >= 0) {
        fcntl(client_fd, F_SETFL, O_NONBLOCK);
        // Store client_fd for later use when processing registration messages
        // Add to your poll/select/epoll set
    }
}
```

#### Task 2.4: Handle registration messages

When data arrives on a client socket:

```c
void handle_osk_client_message(int client_fd) {
    OskMsgHeader header;
    
    if (recv(client_fd, &header, sizeof(header), MSG_PEEK) < sizeof(header)) {
        return;
    }
    
    if (header.type == OSK_MSG_REGISTER_REGION) {
        OskRegisterMsg msg;
        recv(client_fd, &msg, sizeof(msg), 0);
        
        // Find empty slot and store
        for (int i = 0; i < MAX_REGISTERED_REGIONS; i++) {
            if (!registered_regions[i].active) {
                registered_regions[i].active = 1;
                registered_regions[i].region_id = msg.region_id;
                registered_regions[i].screen_index = msg.screen_index;
                registered_regions[i].x = msg.x;
                registered_regions[i].y = msg.y;
                registered_regions[i].width = msg.width;
                registered_regions[i].height = msg.height;
                registered_regions[i].client_fd = client_fd;
                break;
            }
        }
    }
    else if (header.type == OSK_MSG_UNREGISTER_REGION) {
        // Mark region as inactive
    }
}
```

#### Task 2.5: Intercept touches in registered regions

**This is the critical integration point.**

Find the location in touch-scroll where touch events are processed BEFORE being sent to the virtual device. Add a check:

```c
// Returns 1 if touch was consumed by a region, 0 otherwise
int check_region_intercept(int screen_index, int abs_x, int abs_y, 
                           int touch_id, int event_type) {
    
    for (int i = 0; i < MAX_REGISTERED_REGIONS; i++) {
        RegisteredRegion *r = &registered_regions[i];
        
        if (!r->active) continue;
        if (r->screen_index != screen_index) continue;
        
        // Hit test
        if (abs_x >= r->x && abs_x < r->x + r->width &&
            abs_y >= r->y && abs_y < r->y + r->height) {
            
            // Build and send touch event to OSK
            OskTouchEventMsg msg = {
                .type = event_type,  // OSK_MSG_TOUCH_DOWN, MOVE, or UP
                .region_id = r->region_id,
                .touch_id = touch_id,
                .x = abs_x - r->x,   // Relative to region
                .y = abs_y - r->y,
                .abs_x = abs_x,
                .abs_y = abs_y
            };
            
            send(r->client_fd, &msg, sizeof(msg), MSG_NOSIGNAL);
            
            return 1;  // Consumed
        }
    }
    
    return 0;  // Not consumed, proceed normally
}
```

#### Task 2.6: Integrate intercept check into event loop

Find where touch-scroll processes touch events. The exact location depends on existing code structure. Look for where:
- Touch down events are detected
- Touch position updates occur
- Touch up events are detected

**Before** the code that forwards to the virtual mouse device, add:

```c
// Map to screen coordinates first (use existing mapping logic)
int screen_x = /* ... existing coordinate mapping ... */;
int screen_y = /* ... existing coordinate mapping ... */;

// Check if this should go to a registered region instead
int event_type;
if (/* this is touch down */) event_type = OSK_MSG_TOUCH_DOWN;
else if (/* this is touch up */) event_type = OSK_MSG_TOUCH_UP;
else event_type = OSK_MSG_TOUCH_MOVE;

if (check_region_intercept(source_screen_index, screen_x, screen_y, 
                           touch_id, event_type)) {
    // Event was consumed by OSK region
    // DO NOT forward to virtual mouse device
    continue;  // or return, depending on loop structure
}

// ... existing code to forward to virtual device ...
```

#### Task 2.7: Handle client disconnection

When a client socket closes:

```c
void handle_osk_client_disconnect(int client_fd) {
    // Deactivate all regions owned by this client
    for (int i = 0; i < MAX_REGISTERED_REGIONS; i++) {
        if (registered_regions[i].client_fd == client_fd) {
            registered_regions[i].active = 0;
        }
    }
    close(client_fd);
}
```

---

### PHASE 3: Modify context-osk (Client Side)

#### Task 3.1: Connect to touch-scroll

At startup, after the OSK window is created and positioned:

```c
int connect_to_touch_proxy(void) {
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    
    struct sockaddr_un addr = {0};
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, OSK_SOCKET_PATH, sizeof(addr.sun_path) - 1);
    
    if (connect(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("Failed to connect to touch-scroll");
        close(fd);
        return -1;
    }
    
    fcntl(fd, F_SETFL, O_NONBLOCK);
    return fd;
}
```

#### Task 3.2: Register the OSK region

After connecting, send the registration:

```c
void register_osk_region(int sock_fd, int screen_idx, int x, int y, int w, int h) {
    OskRegisterMsg msg = {
        .type = OSK_MSG_REGISTER_REGION,
        .region_id = 1,
        .screen_index = screen_idx,
        .x = x,
        .y = y,
        .width = w,
        .height = h
    };
    
    send(sock_fd, &msg, sizeof(msg), 0);
}
```

**Call this with the OSK window's actual screen position and size.**

#### Task 3.3: Receive and process touch events

In the OSK's main loop, check for incoming touch events:

```c
void process_touch_events_from_proxy(int sock_fd) {
    OskTouchEventMsg msg;
    
    while (recv(sock_fd, &msg, sizeof(msg), 0) == sizeof(msg)) {
        switch (msg.type) {
            case OSK_MSG_TOUCH_DOWN:
                handle_touch_down(msg.x, msg.y, msg.touch_id);
                break;
                
            case OSK_MSG_TOUCH_MOVE:
                handle_touch_move(msg.x, msg.y, msg.touch_id);
                break;
                
            case OSK_MSG_TOUCH_UP:
                handle_touch_up(msg.x, msg.y, msg.touch_id);
                break;
        }
    }
}
```

#### Task 3.4: Integrate with existing event loop

Add the proxy socket FD to whatever event loop mechanism context-osk uses (poll, select, epoll, or GUI toolkit event loop).

The touch coordinates in the messages are **relative to the OSK region's top-left corner**, so they should map directly to the OSK's internal coordinate system.

---

## FILE CHANGES SUMMARY

### New files to create:
- `shared/osk_ipc.h` - Protocol definitions (shared between both projects)

### Files to modify in touch-scroll:
- Main source file: Add socket server, region storage, intercept logic
- Add includes for `osk_ipc.h`, `<sys/socket.h>`, `<sys/un.h>`

### Files to modify in context-osk:
- Main source file: Add socket client, registration, event receiving
- Add includes for `osk_ipc.h`, `<sys/socket.h>`, `<sys/un.h>`

---

## CONSTRAINTS AND WARNINGS

### DO NOT:
- Modify the virtual mouse device output logic beyond adding the intercept check
- Change the existing gesture recognition in touch-scroll
- Add additional message types beyond what is specified
- Use any IPC mechanism other than Unix stream sockets
- Add threading to either project
- Refactor existing code structure unnecessarily

### COORDINATE SYSTEM NOTES:
- touch-scroll already maps raw touch coordinates to screen coordinates
- The registered region uses screen coordinates
- Events sent to context-osk have coordinates relative to the region (0,0 = top-left of OSK)
- Ensure the screen_index in registration matches how touch-scroll identifies which physical device an event came from

### ERROR HANDLING:
- If send() fails with EPIPE or ECONNRESET, call handle_osk_client_disconnect()
- If context-osk cannot connect to socket, it should retry periodically or fall back to non-functional state
- Both processes should handle the other process restarting

---

## TESTING PROCEDURE

1. Start touch-scroll first
2. Verify socket file exists: `ls -la /tmp/context-osk.sock`
3. Start context-osk
4. Verify connection in touch-scroll logs
5. Touch inside the OSK region on screen 2 - should be handled by OSK
6. Touch outside the OSK region on screen 2 - should pass through to apps
7. Touch anywhere on screen 1 - should pass through to apps (unaffected)

---

## IMPLEMENTATION ORDER

1. Create `shared/osk_ipc.h`
2. Implement socket server in touch-scroll (Tasks 2.1-2.4)
3. Test that touch-scroll creates socket and accepts connections (use `nc -U /tmp/context-osk.sock`)
4. Implement intercept logic in touch-scroll (Tasks 2.5-2.7)
5. Implement socket client in context-osk (Tasks 3.1-3.4)
6. Integration test

Complete each phase fully before moving to the next.
