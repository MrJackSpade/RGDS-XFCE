#ifndef QJOYPAD_CONSTANT_H
#define QJOYPAD_CONSTANT_H

//How many cycles there are per click.
#define FREQ 10

//How many milliseconds per cycle.
//Note: 16ms (~60Hz) is used instead of 5ms (200Hz) to prevent overwhelming
//applications like DOSBox with mouse events. Mouse movement is scaled to compensate.
#define MSEC 16
#define MSEC_MOUSE_SCALE 3.2f  // 16/5 - compensates for lower event rate

//events will be generated every FREQ * MSEC milliseconds. The duration of the
//event can be anywhere between 0 * MSEC and FREQ * MSEC. This means there will
//be FREQ + 1 levels of gradation.


//maximum range of values from joystick driver
#define JOYMAX 32767
#define JOYMIN -32767

//maximum number of defined keys
#define MAXKEY 255

//fastest the mouse can go. Completely arbitrary.
#define MAXMOUSESPEED 5000

#define SENSITIVITY_MIN	1e-8F
#define SENSITIVITY_MAX	1e+8F

#endif
