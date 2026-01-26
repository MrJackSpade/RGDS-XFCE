#include "UInput.h"
#include <fcntl.h>
#include <unistd.h>
#include <cstring>
#include <cstdio>
#include <sys/ioctl.h>
#include <linux/uinput.h>

UInput::UInput() : fd(-1) {}

UInput::~UInput() {
    destroy();
}

bool UInput::init() {
    fd = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
    if (fd < 0) {
        perror("Opening /dev/uinput");
        return false;
    }

    ioctl(fd, UI_SET_EVBIT, EV_KEY);
    ioctl(fd, UI_SET_EVBIT, EV_SYN);

    // Enable all keys 0-255 to support generic themes
    for (int i = 0; i < 256; i++) {
        ioctl(fd, UI_SET_KEYBIT, i);
    }

    struct uinput_setup usetup;
    memset(&usetup, 0, sizeof(usetup));
    usetup.id.bustype = BUS_USB;
    usetup.id.vendor = 0x1234; /* Sample vendor */
    usetup.id.product = 0x5678; /* Sample product */
    strcpy(usetup.name, "context-osk-virtual-keyboard");

    ioctl(fd, UI_DEV_SETUP, &usetup);
    ioctl(fd, UI_DEV_CREATE);
    
    printf("UInput device created.\n");
    return true;
}

void UInput::destroy() {
    if (fd >= 0) {
        ioctl(fd, UI_DEV_DESTROY);
        close(fd);
        fd = -1;
    }
}

void UInput::emit(int type, int code, int val) {
    if (fd < 0) return;
    
    struct input_event ie;
    ie.type = type;
    ie.code = code;
    ie.value = val;
    ie.time.tv_sec = 0;
    ie.time.tv_usec = 0;

    write(fd, &ie, sizeof(ie));
}

void UInput::send_key(int code, bool pressed) {
    emit(EV_KEY, code, pressed ? 1 : 0);
    emit(EV_SYN, SYN_REPORT, 0);
}
