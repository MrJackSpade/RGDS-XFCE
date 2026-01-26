// SPDX-FileCopyrightText: 2026 The DOSBox Staging Team
// SPDX-License-Identifier: GPL-2.0-or-later

#ifndef DOSBOX_DIRECT_INPUT_H
#define DOSBOX_DIRECT_INPUT_H

// Initializes direct input by scanning /dev/input/event* for keyboards.
void DirectInput_Init();

// Polls all open devices for new events and injects them into the mapper.
void DirectInput_Poll();

// Grabs/Ungrabs all mouse devices (exclusive access)
void DirectInput_SetMouseGrab(bool grab);

// Closes all open devices.
void DirectInput_Quit();

#endif // DOSBOX_DIRECT_INPUT_H
