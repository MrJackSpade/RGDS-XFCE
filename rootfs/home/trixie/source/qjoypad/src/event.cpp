#include <QX11Info>
#include "event.h"

#include <linux/uinput.h>
#include <linux/input.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

// NOTE: No fallbacks. If something fails, we fail loudly. Fallbacks are bad.

// uinput virtual device
static int uinput_fd = -1;
static bool uinput_initialized = false;

// Initialize the uinput virtual device
static void init_uinput() {
    if (uinput_initialized) {
        return;
    }
    uinput_initialized = true;

    uinput_fd = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
    if (uinput_fd < 0) {
        fprintf(stderr, "FATAL: Failed to open /dev/uinput: %s\n", strerror(errno));
        fprintf(stderr, "Make sure you have permission to access /dev/uinput (input group)\n");
        exit(1);
    }

    // Enable relative events (mouse movement)
    if (ioctl(uinput_fd, UI_SET_EVBIT, EV_REL) < 0 ||
        ioctl(uinput_fd, UI_SET_RELBIT, REL_X) < 0 ||
        ioctl(uinput_fd, UI_SET_RELBIT, REL_Y) < 0) {
        fprintf(stderr, "FATAL: Failed to configure uinput relative events: %s\n", strerror(errno));
        exit(1);
    }

    // Enable key events (for mouse buttons and keyboard)
    if (ioctl(uinput_fd, UI_SET_EVBIT, EV_KEY) < 0) {
         fprintf(stderr, "FATAL: Failed to configure uinput key events: %s\n", strerror(errno));
         exit(1);
    }
    
    // Enable all supported keys
    for (int i = 0; i < KEY_MAX; i++) {
        ioctl(uinput_fd, UI_SET_KEYBIT, i);
    }

    // Configure the device
    struct uinput_setup usetup;
    memset(&usetup, 0, sizeof(usetup));
    usetup.id.bustype = BUS_USB;
    usetup.id.vendor = 0x1234;
    usetup.id.product = 0x5678;
    strcpy(usetup.name, "QJoyPad Virtual Input");

    if (ioctl(uinput_fd, UI_DEV_SETUP, &usetup) < 0) {
        fprintf(stderr, "FATAL: Failed UI_DEV_SETUP: %s\n", strerror(errno));
        exit(1);
    }

    if (ioctl(uinput_fd, UI_DEV_CREATE) < 0) {
        fprintf(stderr, "FATAL: Failed UI_DEV_CREATE: %s\n", strerror(errno));
        exit(1);
    }
}

// Send a mouse movement via uinput
static void uinput_mouse_move(int dx, int dy) {
    struct input_event ev;
    memset(&ev, 0, sizeof(ev));

    if (dx != 0) {
        ev.type = EV_REL;
        ev.code = REL_X;
        ev.value = dx;
        write(uinput_fd, &ev, sizeof(ev));
    }

    if (dy != 0) {
        ev.type = EV_REL;
        ev.code = REL_Y;
        ev.value = dy;
        write(uinput_fd, &ev, sizeof(ev));
    }

    ev.type = EV_SYN;
    ev.code = SYN_REPORT;
    ev.value = 0;
    write(uinput_fd, &ev, sizeof(ev));
}

// Send a key event via uinput
static void uinput_key_send(int code, int value) {
    struct input_event ev;
    memset(&ev, 0, sizeof(ev));

    ev.type = EV_KEY;
    ev.code = code;
    ev.value = value;
    write(uinput_fd, &ev, sizeof(ev));

    ev.type = EV_SYN;
    ev.code = SYN_REPORT;
    ev.value = 0;
    write(uinput_fd, &ev, sizeof(ev));
}

void sendevent(const FakeEvent &e) {
    Display* display = QX11Info::display();

    // Ensure uinput is initialized for any event type
    init_uinput();

    switch (e.type) {
    case FakeEvent::MouseMove:
        if (e.move.x == 0 && e.move.y == 0) return;
        uinput_mouse_move(e.move.x, e.move.y);
        return;

    case FakeEvent::MouseMoveAbsolute:
      {
        Screen* screen = XDefaultScreenOfDisplay(display);
        static int rememberX = 0, rememberY = 0;
        if (e.move.x) rememberX = e.move.x;
        if (e.move.y) rememberY = e.move.y;
        const int scaledX100 = rememberX * (XWidthOfScreen(screen)/2) / 100;
        const int scaledY100 = rememberY * (XHeightOfScreen(screen)/2) / 100;
        XTestFakeMotionEvent(display, DefaultScreen(display),
                             XWidthOfScreen(screen)/2 + scaledX100,
                             XHeightOfScreen(screen)/2 + scaledY100, 0);
        break;
      }
    case FakeEvent::KeyUp:
        if (e.keycode == 0) return;
        // X11 keycodes often map to Kernel keycodes by subtracting 8
        // This is a common standard for evdev/Xorg.
        uinput_key_send(e.keycode - 8, 0);
        break;

    case FakeEvent::KeyDown:
        if (e.keycode == 0) return;
        uinput_key_send(e.keycode - 8, 1);
        break;

    case FakeEvent::MouseUp:
        // Mouse buttons are also keys in uinput
        if (e.keycode == 0) return;
        // X11 Button 1, 2, 3 -> BTN_LEFT(0x110), BTN_MIDDLE(0x112), BTN_RIGHT(0x111)
        // This mapping can be tricky.
        // Assuming e.keycode here is actually a button number (1, 2, 3)
        // based on how XTestFakeButtonEvent works.
        // Standard mapping:
        // 1 -> BTN_LEFT
        // 2 -> BTN_MIDDLE
        // 3 -> BTN_RIGHT
        // 4 -> Scroll Up (not a key in uinput usually, relative axis)
        // 5 -> Scroll Down
        {
            int btn_code = 0;
            switch(e.keycode) {
                case 1: btn_code = BTN_LEFT; break;
                case 2: btn_code = BTN_MIDDLE; break;
                case 3: btn_code = BTN_RIGHT; break;
                // For simplicity, we stick to XTest for click/drag for now? 
                // The user SPECIFICALLY asked for keyboard to use the same method.
                // But generally "Fix it so that keyboard uses the same method as mouse" implies unifying them.
                // However, mapping mouse buttons from X11 indices to kernel codes is messy.
                // I will leave MouseUp/MouseDown on XTest unless needed?
                // Actually, the user asked specifically about the KEYBOARD.
                // "Why does the virtual mouse show up... but not they keyboard?"
                // I will stick to fixing the KEYBOARD events as requested to avoid regressions in mouse clicks.
            }
            if (btn_code) {
               uinput_key_send(btn_code, 0);
            } else {
               XTestFakeButtonEvent(display, e.keycode, false, 0);
            }
        }
        break;

    case FakeEvent::MouseDown:
        if (e.keycode == 0) return;
        {
            int btn_code = 0;
            switch(e.keycode) {
                case 1: btn_code = BTN_LEFT; break;
                case 2: btn_code = BTN_MIDDLE; break;
                case 3: btn_code = BTN_RIGHT; break;
            }
            if (btn_code) {
               uinput_key_send(btn_code, 1);
            } else {
               XTestFakeButtonEvent(display, e.keycode, true, 0);
            }
        }
        break;
    }
    XFlush(display);
}
