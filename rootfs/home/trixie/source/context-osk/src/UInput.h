#ifndef UINPUT_H
#define UINPUT_H

#include <linux/uinput.h>

class UInput {
public:
    UInput();
    ~UInput();

    bool init();
    void destroy();
    void send_key(int code, bool pressed);

private:
    int fd;
    void emit(int type, int code, int val);
};

#endif
