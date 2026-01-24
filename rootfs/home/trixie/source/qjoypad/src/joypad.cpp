#include <QApplication>

#include "joypad.h"

//for actually interacting with the joystick devices
#include <linux/joystick.h>
#include <linux/input.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <sys/ioctl.h>
#include <QHash>

// Macros for testing bits in evdev bitmaps
#define NBITS(x) ((((x)-1)/(sizeof(long)*8))+1)
#define TEST_BIT(array, bit) ((array[(bit)/(sizeof(long)*8)] >> ((bit)%(sizeof(long)*8))) & 1)

// Global mapping for evdev codes to joystick indices
static QHash<int, int> buttonCodeToIndex;
static QHash<int, int> axisCodeToIndex;
static bool mappingInitialized = false;

JoyPad::JoyPad( int i, int dev, QObject *parent )
    : QObject(parent), joydev(-1), axisCount(0), buttonCount(0), isEvdev(false), jpw(0), readNotifier(0), errorNotifier(0) {
    debug_mesg("Constructing the joypad device with index %d and fd %d\n", i, dev);
    //remember the index,
    index = i;

    //load data from the joystick device, if available.
    if (dev >= 0) {
        debug_mesg("Valid file handle, setting up handlers and reading axis configs...\n");
        open(dev);
        debug_mesg("done resetting and setting up device index %d\n", i);
    } else {
        debug_mesg("This joypad does not have a valid file handle, not setting up event listeners\n");
    }
    debug_mesg("Done constructing the joypad device %d\n", i);
}

JoyPad::~JoyPad() {
    close();
}

void JoyPad::close() {
    if (readNotifier) {
        disconnect(readNotifier, 0, 0, 0);

        readNotifier->blockSignals(true);
        readNotifier->setEnabled(false);

        delete readNotifier;
        readNotifier = 0;
    }
    if (errorNotifier) {
        disconnect(errorNotifier, 0, 0, 0);

        errorNotifier->blockSignals(true);
        errorNotifier->setEnabled(false);

        delete errorNotifier;
        errorNotifier = 0;
    }
    if (joydev >= 0) {
        if (::close(joydev) != 0) {
            debug_mesg("close(js%d %d): %s\n", index, joydev, strerror(errno));
        }
        joydev = -1;
    }
}

void JoyPad::open(int dev) {
    debug_mesg("resetting to dev\n");
    //remember the device file descriptor
    close();
    joydev = dev;
    isEvdev = false;

    char id[256];
    memset(id, 0, sizeof(id));

    // Try joystick API first
    if (ioctl(joydev, JSIOCGNAME(sizeof(id)), id) >= 0) {
        // It's a joystick device using the old API
        deviceId = id;
        isEvdev = false;

        //read in the number of axes / buttons
        axisCount = 0;
        ioctl (joydev, JSIOCGAXES, &axisCount);
        buttonCount = 0;
        ioctl (joydev, JSIOCGBUTTONS, &buttonCount);
    }
    else {
        // Try evdev API
        if (ioctl(joydev, EVIOCGNAME(sizeof(id)), id) >= 0) {
            deviceId = id;
            isEvdev = true;

            // Initialize evdev mappings
            if (!mappingInitialized) {
                // Map common gamepad buttons to indices
                int btnIdx = 0;
                for (int code = BTN_SOUTH; code <= BTN_THUMBR; code++) {
                    buttonCodeToIndex[code] = btnIdx++;
                }
                // Map D-pad buttons (0x220-0x227)
                for (int code = 0x220; code <= 0x227; code++) {
                    buttonCodeToIndex[code] = btnIdx++;
                }
                // Map KEY_HOME and other special keys that may be used
                buttonCodeToIndex[KEY_HOME] = btnIdx++;
                buttonCodeToIndex[KEY_HOMEPAGE] = btnIdx++;
                // Map generic joystick buttons
                for (int code = BTN_JOYSTICK; code < BTN_JOYSTICK + 32; code++) {
                    if (!buttonCodeToIndex.contains(code)) {
                        buttonCodeToIndex[code] = btnIdx++;
                    }
                }

                // Map common axes
                axisCodeToIndex[ABS_X] = 0;
                axisCodeToIndex[ABS_Y] = 1;
                axisCodeToIndex[ABS_Z] = 2;
                axisCodeToIndex[ABS_RX] = 3;
                axisCodeToIndex[ABS_RY] = 4;
                axisCodeToIndex[ABS_RZ] = 5;
                axisCodeToIndex[ABS_HAT0X] = 6;
                axisCodeToIndex[ABS_HAT0Y] = 7;

                mappingInitialized = true;
            }

            // Query actual capabilities
            unsigned long keybit[NBITS(KEY_MAX)] = {0};
            unsigned long absbit[NBITS(ABS_MAX)] = {0};

            axisCount = 0;
            buttonCount = 0;

            if (ioctl(joydev, EVIOCGBIT(EV_KEY, sizeof(keybit)), keybit) >= 0) {
                // Count buttons
                for (int i = 0; i < KEY_MAX; i++) {
                    if (TEST_BIT(keybit, i) && buttonCodeToIndex.contains(i)) {
                        int idx = buttonCodeToIndex[i];
                        if (idx >= buttonCount) {
                            buttonCount = idx + 1;
                        }
                    }
                }
            }

            if (ioctl(joydev, EVIOCGBIT(EV_ABS, sizeof(absbit)), absbit) >= 0) {
                // Count axes
                for (int i = 0; i < ABS_MAX; i++) {
                    if (TEST_BIT(absbit, i) && axisCodeToIndex.contains(i)) {
                        int idx = axisCodeToIndex[i];
                        if (idx >= axisCount) {
                            axisCount = idx + 1;
                        }
                    }
                }
            }
        }
        else {
            deviceId = "Unknown";
        }
    }

    //make sure that we have the axes we need.
    //if one that we need doesn't yet exist, add it in.
    //Note: if the current layout has a key assigned to an axis that did not
    //have a real joystick axis mapped to it, and this function suddenly brings
    //that axis into use, the key assignment will not be lost because the axis
    //will already exist and no new axis will be created.
    for (int i = axes.size(); i < axisCount; i++) {
        axes.append(new Axis( i, this ));
    }
    for (int i = buttons.size(); i < buttonCount; i++) {
        buttons.append(new Button( i, this ));
    }
    debug_mesg("Setting up joyDeviceListeners\n");
    readNotifier = new QSocketNotifier(joydev, QSocketNotifier::Read, this);
    connect(readNotifier, SIGNAL(activated(int)), this, SLOT(handleJoyEvents()));
    errorNotifier = new QSocketNotifier(joydev, QSocketNotifier::Exception, this);
    connect(errorNotifier, SIGNAL(activated(int)), this, SLOT(handleJoyEvents()));
    debug_mesg("Done setting up joyDeviceListeners\n");
    debug_mesg("done resetting to dev\n");
}

const QString &JoyPad::getDeviceId() const {
    return deviceId;
}

QString JoyPad::getName() const {
    return tr("Joystick %1 (%2)").arg(index+1).arg(deviceId);
}

int JoyPad::getIndex() const {
    return index;
}

void JoyPad::toDefault() {
    //to reset the whole, reset all the parts.
    foreach (Axis *axis, axes) {
        axis->toDefault();
    }
    foreach (Button *button, buttons) {
        button->toDefault();
    }
}

bool JoyPad::isDefault() {
    //if any of the parts are not at default, then the whole isn't either.
    foreach (Axis *axis, axes) {
        if (!axis->isDefault()) return false;
    }
    foreach (Button *button, buttons) {
        if (!button->isDefault()) return false;
    }
    return true;
}

bool JoyPad::readConfig( QTextStream &stream ) {
    toDefault();

    QString word;
    QChar ch = 0;
    int num = 0;

    stream >> word;
    while (!word.isNull() && word != "}") {
        word = word.toLower();
        if (word == "button") {
            stream >> num;
            if (num > 0) {
                stream >> ch;
                if (ch != ':') {
                    errorBox(tr("Layout file error"), tr("Expected ':', found '%1'.").arg(ch));
                    return false;
                }
                for (int i = buttons.size(); i < num; ++ i) {
                    buttons.append(new Button(i, this));
                }
                if (!buttons[num-1]->read( stream )) {
                    errorBox(tr("Layout file error"), tr("Error reading Button %1").arg(num));
                    return false;
                }
            }
            else {
                stream.readLine();
            }
        }
        else if (word == "axis") {
            stream >> num;
            if (num > 0) {
                stream >> ch;
                if (ch != ':') {
                    errorBox(tr("Layout file error"), tr("Expected ':', found '%1'.").arg(ch));
                    return false;
                }
                for (int i = axes.size(); i < num; ++ i) {
                    axes.append(new Axis(i, this));
                }
                if (!axes[num-1]->read(stream)) {
                    errorBox(tr("Layout file error"), tr("Error reading Axis %1").arg(num));
                    return false;
                }
            }
        }
        else {
            errorBox(tr("Layout file error"), tr("Error while reading layout. Unrecognized word: %1").arg(word));
            return false;
        }
        stream >> word;
    }
    return true;
}

//only actually writes something if this JoyPad is NON DEFAULT.
void JoyPad::write( QTextStream &stream ) {
    if (!axes.empty() || !buttons.empty()) {
        stream << "Joystick " << (index+1) << " {\n";
        foreach (Axis *axis, axes) {
            if (!axis->isDefault()) {
                axis->write(stream);
            }
        }
        foreach (Button *button, buttons) {
            if (!button->isDefault()) {
                button->write(stream);
            }
        }
        stream << "}\n\n";
    }
}

void JoyPad::release() {
    foreach (Axis *axis, axes) {
        axis->release();
    }
    foreach (Button *button, buttons) {
        button->release();
    }
}

void JoyPad::jsevent(const js_event &msg) {
    //if there is a JoyPadWidget around, ie, if the joypad is being edited
    if (jpw != NULL && hasFocus) {
        //tell the dialog there was an event. It will use this to flash
        //the appropriate button, if necesary.
        jpw->jsevent(msg);
        return;
    }
    //if the dialog is open, stop here. We don't want to signal ourselves with
    //the input we generate.
    if (qApp->activeWindow() != 0 && qApp->activeModalWidget() != 0) return;

    //otherwise, lets create us a fake event! Pass on the event to whichever
    //Button or Axis was pressed and let them decide what to do with it.
    unsigned int type = msg.type & ~JS_EVENT_INIT;
    if (type == JS_EVENT_AXIS) {
        debug_mesg("DEBUG: passing on an axis event\n");
        debug_mesg("DEBUG: %d %d\n", msg.number, msg.value);
        if (msg.number < axes.size()) axes[msg.number]->jsevent(msg.value);
        else debug_mesg("DEBUG: axis index out of range: %d\n", msg.value);
    }
    else if (type == JS_EVENT_BUTTON) {
        debug_mesg("DEBUG: passing on a button event\n");
        debug_mesg("DEBUG: %d %d\n", msg.number, msg.value);
        if (msg.number < buttons.size()) buttons[msg.number]->jsevent(msg.value);
        else debug_mesg("DEBUG: button index out of range: %d\n", msg.value);
    }
}

JoyPadWidget* JoyPad::widget( QWidget* parent, int i) {
    //create the widget and remember it.
    jpw = new JoyPadWidget(this, i, parent);
    return jpw;
}

void JoyPad::handleJoyEvents() {
    // For both APIs: read ALL events but only process the FINAL state of each
    // axis/button. Intermediate values are discarded - we only care where
    // the joystick ended up, not the path it took to get there.

    QHash<int, int> lastAxisValue;
    QHash<int, int> lastButtonValue;

    if (!isEvdev) {
        // Read as js_event (joystick API)
        js_event msg;
        while (read(joydev, &msg, sizeof(js_event)) == sizeof(js_event)) {
            unsigned int type = msg.type & ~JS_EVENT_INIT;
            if (type == JS_EVENT_AXIS) {
                lastAxisValue[msg.number] = msg.value;
            } else if (type == JS_EVENT_BUTTON) {
                lastButtonValue[msg.number] = msg.value;
            }
        }
    }
    else {
        // Read as input_event (evdev)
        struct input_event ev;
        while (read(joydev, &ev, sizeof(struct input_event)) == sizeof(struct input_event)) {
            if (ev.type == EV_KEY) {
                if (buttonCodeToIndex.contains(ev.code)) {
                    lastButtonValue[buttonCodeToIndex[ev.code]] = ev.value;
                }
            }
            else if (ev.type == EV_ABS) {
                if (axisCodeToIndex.contains(ev.code)) {
                    // Convert from evdev range to joystick range (-32767 to 32767)
                    lastAxisValue[axisCodeToIndex[ev.code]] = (ev.value - 519) * 65;
                }
            }
        }
    }

    // Process only the final state for each axis
    for (auto it = lastAxisValue.begin(); it != lastAxisValue.end(); ++it) {
        js_event jse;
        jse.type = JS_EVENT_AXIS;
        jse.number = it.key();
        jse.value = it.value();
        jsevent(jse);
    }

    // Process only the final state for each button
    for (auto it = lastButtonValue.begin(); it != lastButtonValue.end(); ++it) {
        js_event jse;
        jse.type = JS_EVENT_BUTTON;
        jse.number = it.key();
        jse.value = it.value();
        jsevent(jse);
    }
}

void JoyPad::releaseWidget() {
    //this is how we know that there is no longer a JoyPadWidget around.
    jpw = 0;
}

void JoyPad::errorRead() {
    debug_mesg("There was an error reading off of the device with fd %d, disabling\n", joydev);
    close();
    debug_mesg("Done disabling device with fd %d\n", joydev);
}

void JoyPad::focusChange(bool focusState) {
	hasFocus = !focusState;
}
