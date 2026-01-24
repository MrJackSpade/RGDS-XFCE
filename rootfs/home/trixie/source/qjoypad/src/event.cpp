#include <QX11Info>
#include "event.h"

#include <linux/uinput.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

// NOTE: No fallbacks. If something fails, we fail loudly. Fallbacks are bad.

// uinput virtual mouse device
static int uinput_fd = -1;
static bool uinput_initialized = false;

// Initialize the uinput virtual mouse device
static void init_uinput_mouse() {
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

    // Enable key events (for mouse buttons if we want them later)
    ioctl(uinput_fd, UI_SET_EVBIT, EV_KEY);
    ioctl(uinput_fd, UI_SET_KEYBIT, BTN_LEFT);
    ioctl(uinput_fd, UI_SET_KEYBIT, BTN_RIGHT);
    ioctl(uinput_fd, UI_SET_KEYBIT, BTN_MIDDLE);

    // Configure the device
    struct uinput_setup usetup;
    memset(&usetup, 0, sizeof(usetup));
    usetup.id.bustype = BUS_USB;
    usetup.id.vendor = 0x1234;
    usetup.id.product = 0x5678;
    strcpy(usetup.name, "QJoyPad Virtual Mouse");

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

void sendevent(const FakeEvent &e) {
    Display* display = QX11Info::display();

    switch (e.type) {
    case FakeEvent::MouseMove:
        if (e.move.x == 0 && e.move.y == 0) return;
        init_uinput_mouse();
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
        XTestFakeKeyEvent(display, e.keycode, false, 0);
        break;

    case FakeEvent::KeyDown:
        if (e.keycode == 0) return;
        XTestFakeKeyEvent(display, e.keycode, true, 0);
        break;

    case FakeEvent::MouseUp:
        if (e.keycode == 0) return;
        XTestFakeButtonEvent(display, e.keycode, false, 0);
        break;

    case FakeEvent::MouseDown:
        if (e.keycode == 0) return;
        XTestFakeButtonEvent(display, e.keycode, true, 0);
        break;
    }
    XFlush(display);
}
