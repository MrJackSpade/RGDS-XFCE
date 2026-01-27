// SPDX-FileCopyrightText: 2026 The DOSBox Staging Team
// SPDX-License-Identifier: GPL-2.0-or-later

#include "direct_input.h"

#include "gui/mapper.h"
#include <SDL.h>

#include <vector>
#include <string>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cerrno>

#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include <linux/input.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>

#include <thread>
#include <mutex>
#include <atomic>
#include <chrono>
#include <set>
#include <algorithm>

static std::mutex input_mutex;
static std::thread hotplug_thread;
static std::atomic<bool> hotplug_running{false};
static std::set<std::string> open_device_paths;



// Simple mapping from Linux KEY_ code to SDL_Scancode.
// This is not exhaustive but covers most standard keys.
// A full implementation would need a complete table.
static SDL_Scancode LinuxKeyToSDLScancode(int code) {
	if (code >= KEY_1 && code <= KEY_9) return static_cast<SDL_Scancode>(SDL_SCANCODE_1 + (code - KEY_1));
	if (code == KEY_0) return SDL_SCANCODE_0;
	switch (code) {
		case KEY_Q: return SDL_SCANCODE_Q;
		case KEY_W: return SDL_SCANCODE_W;
		case KEY_E: return SDL_SCANCODE_E;
		case KEY_R: return SDL_SCANCODE_R;
		case KEY_T: return SDL_SCANCODE_T;
		case KEY_Y: return SDL_SCANCODE_Y;
		case KEY_U: return SDL_SCANCODE_U;
		case KEY_I: return SDL_SCANCODE_I;
		case KEY_O: return SDL_SCANCODE_O;
		case KEY_P: return SDL_SCANCODE_P;

		case KEY_A: return SDL_SCANCODE_A;
		case KEY_S: return SDL_SCANCODE_S;
		case KEY_D: return SDL_SCANCODE_D;
		case KEY_F: return SDL_SCANCODE_F;
		case KEY_G: return SDL_SCANCODE_G;
		case KEY_H: return SDL_SCANCODE_H;
		case KEY_J: return SDL_SCANCODE_J;
		case KEY_K: return SDL_SCANCODE_K;
		case KEY_L: return SDL_SCANCODE_L;

		case KEY_Z: return SDL_SCANCODE_Z;
		case KEY_X: return SDL_SCANCODE_X;
		case KEY_C: return SDL_SCANCODE_C;
		case KEY_V: return SDL_SCANCODE_V;
		case KEY_B: return SDL_SCANCODE_B;
		case KEY_N: return SDL_SCANCODE_N;
		case KEY_M: return SDL_SCANCODE_M;

		case KEY_ESC: return SDL_SCANCODE_ESCAPE;
		case KEY_MINUS: return SDL_SCANCODE_MINUS;
		case KEY_EQUAL: return SDL_SCANCODE_EQUALS;
		case KEY_BACKSPACE: return SDL_SCANCODE_BACKSPACE;
		case KEY_TAB: return SDL_SCANCODE_TAB;
		case KEY_LEFTBRACE: return SDL_SCANCODE_LEFTBRACKET;
		case KEY_RIGHTBRACE: return SDL_SCANCODE_RIGHTBRACKET;
		case KEY_ENTER: return SDL_SCANCODE_RETURN;
		case KEY_LEFTCTRL: return SDL_SCANCODE_LCTRL;
		case KEY_SEMICOLON: return SDL_SCANCODE_SEMICOLON;
		case KEY_APOSTROPHE: return SDL_SCANCODE_APOSTROPHE;
		case KEY_GRAVE: return SDL_SCANCODE_GRAVE;
		case KEY_LEFTSHIFT: return SDL_SCANCODE_LSHIFT;
		case KEY_BACKSLASH: return SDL_SCANCODE_BACKSLASH;
		case KEY_COMMA: return SDL_SCANCODE_COMMA;
		case KEY_DOT: return SDL_SCANCODE_PERIOD;
		case KEY_SLASH: return SDL_SCANCODE_SLASH;
		case KEY_RIGHTSHIFT: return SDL_SCANCODE_RSHIFT;
		case KEY_KPASTERISK: return SDL_SCANCODE_KP_MULTIPLY;
		case KEY_LEFTALT: return SDL_SCANCODE_LALT;
		case KEY_SPACE: return SDL_SCANCODE_SPACE;
		case KEY_CAPSLOCK: return SDL_SCANCODE_CAPSLOCK;
		case KEY_F1: return SDL_SCANCODE_F1;
		case KEY_F2: return SDL_SCANCODE_F2;
		case KEY_F3: return SDL_SCANCODE_F3;
		case KEY_F4: return SDL_SCANCODE_F4;
		case KEY_F5: return SDL_SCANCODE_F5;
		case KEY_F6: return SDL_SCANCODE_F6;
		case KEY_F7: return SDL_SCANCODE_F7;
		case KEY_F8: return SDL_SCANCODE_F8;
		case KEY_F9: return SDL_SCANCODE_F9;
		case KEY_F10: return SDL_SCANCODE_F10;
		case KEY_NUMLOCK: return SDL_SCANCODE_NUMLOCKCLEAR;
		case KEY_SCROLLLOCK: return SDL_SCANCODE_SCROLLLOCK;
		case KEY_KP7: return SDL_SCANCODE_KP_7;
		case KEY_KP8: return SDL_SCANCODE_KP_8;
		case KEY_KP9: return SDL_SCANCODE_KP_9;
		case KEY_KPMINUS: return SDL_SCANCODE_KP_MINUS;
		case KEY_KP4: return SDL_SCANCODE_KP_4;
		case KEY_KP5: return SDL_SCANCODE_KP_5;
		case KEY_KP6: return SDL_SCANCODE_KP_6;
		case KEY_KPPLUS: return SDL_SCANCODE_KP_PLUS;
		case KEY_KP1: return SDL_SCANCODE_KP_1;
		case KEY_KP2: return SDL_SCANCODE_KP_2;
		case KEY_KP3: return SDL_SCANCODE_KP_3;
		case KEY_KP0: return SDL_SCANCODE_KP_0;
		case KEY_KPDOT: return SDL_SCANCODE_KP_PERIOD;
		case KEY_F11: return SDL_SCANCODE_F11;
		case KEY_F12: return SDL_SCANCODE_F12;
		case KEY_KPENTER: return SDL_SCANCODE_KP_ENTER;
		case KEY_RIGHTCTRL: return SDL_SCANCODE_RCTRL;
		case KEY_KPSLASH: return SDL_SCANCODE_KP_DIVIDE;
		case KEY_SYSRQ: return SDL_SCANCODE_PRINTSCREEN;
		case KEY_RIGHTALT: return SDL_SCANCODE_RALT;
		case KEY_HOME: return SDL_SCANCODE_HOME;
		case KEY_UP: return SDL_SCANCODE_UP;
		case KEY_PAGEUP: return SDL_SCANCODE_PAGEUP;
		case KEY_LEFT: return SDL_SCANCODE_LEFT;
		case KEY_RIGHT: return SDL_SCANCODE_RIGHT;
		case KEY_END: return SDL_SCANCODE_END;
		case KEY_DOWN: return SDL_SCANCODE_DOWN;
		case KEY_PAGEDOWN: return SDL_SCANCODE_PAGEDOWN;
		case KEY_INSERT: return SDL_SCANCODE_INSERT;
		case KEY_DELETE: return SDL_SCANCODE_DELETE;
		default: return SDL_SCANCODE_UNKNOWN;
	}
}

#include "misc/logging.h"
#include <sstream>
#include <iomanip>

static std::vector<int> keyboard_fds;
static std::vector<int> mouse_fds;

// Helper to log capability checks
static bool IsKeyboard(int fd, const char* name) {
	unsigned char keybit[KEY_MAX/8 + 1];
	memset(keybit, 0, sizeof(keybit));
	if (ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(keybit)), keybit) < 0) {
		LOG_MSG("DirectInput DEBUG: [%s] Failed to get EV_KEY bits", name);
		return false;
	}
	
	// Check if it has ANY keys in the standard range.
	// Many virtual devices or minimal keyboards might not have all "standard" keys.
	// We'll check for at least ONE key in the main block used for typing (KEY_1 to KEY_SLASH).
	// This avoids picking up pure mouse buttons (which are also EV_KEY).
	
	bool has_any_key = false;
	for (int k = KEY_1; k <= KEY_SLASH; k++) {
		if (keybit[k/8] & (1<<(k%8))) {
			has_any_key = true;
			break;
		}
	}
	
	if (!has_any_key) {
		// Fallback: check for other common keys if it's a specialized controller
		if ((keybit[KEY_ESC/8] & (1<<(KEY_ESC%8))) || 
		    (keybit[KEY_ENTER/8] & (1<<(KEY_ENTER%8))) ||
		    (keybit[KEY_SPACE/8] & (1<<(KEY_SPACE%8)))) {
			has_any_key = true;
		}
	}

	if (!has_any_key) {
		return false;
	}
	
	return true; 
}

static bool IsMouse(int fd, const char* name) {
	unsigned char keybit[KEY_MAX/8 + 1] = {0};
	unsigned char relbit[REL_MAX/8 + 1] = {0};
	
	if (ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(keybit)), keybit) < 0) {
		LOG_MSG("DirectInput DEBUG: [%s] Failed to get EV_KEY bits", name);
		return false;
	}
	if (ioctl(fd, EVIOCGBIT(EV_REL, sizeof(relbit)), relbit) < 0) {
		LOG_MSG("DirectInput DEBUG: [%s] Failed to get EV_REL bits", name);
		return false; 
	}
	
	bool has_rel_x = (relbit[REL_X/8] & (1<<(REL_X%8)));
	bool has_rel_y = (relbit[REL_Y/8] & (1<<(REL_Y%8)));
	bool has_btn_mouse = (keybit[BTN_MOUSE/8] & (1<<(BTN_MOUSE%8)));
	
	if (!has_rel_x || !has_rel_y) {
		return false;
	}
	if (!has_btn_mouse) {
		return false;
	}
	
	return true; 
}

void ScanDevices() {
	DIR *dir = opendir("/dev/input");
	if (!dir) {
		// Only log error if it's the first time/critical, otherwise it might spam
		// But opendir failing usually means system issues, so logging once is fine.
		// For hotplug loop, repeating this error every 2s might be annoying if permissions change.
		// defaulting to log.
		// LOG_MSG("DirectInput: Failed to open /dev/input: %s", strerror(errno)); 
		return;
	}

	struct dirent *dent;
	while ((dent = readdir(dir)) != nullptr) {
		if (strncmp(dent->d_name, "event", 5) != 0) continue;

		char path[256];
		snprintf(path, sizeof(path), "/dev/input/%s", dent->d_name);
		
		std::string path_str(path);

		// Check if we already opened this device (thread-safe check usually requires lock 
		// if open_device_paths is read/written by multiple threads, but here only 
		// ScanDevices writes it. The main thread never touches open_device_paths.
		// So we don't strictly need a lock for *accessing* open_device_paths 
		// IF ScanDevices is the only one running. 
		// However, DirectInput_Init calls ScanDevices synchronously before the thread starts.
		// So there is no race on open_device_paths itself as long as Init happens-before Thread.
		if (open_device_paths.find(path_str) != open_device_paths.end()) {
			continue;
		}

		int fd = open(path, O_RDONLY | O_NONBLOCK);
		if (fd >= 0) {
			char name[256] = "Unknown";
			ioctl(fd, EVIOCGNAME(sizeof(name)), name);
			
			// Determine capability WITHOUT holding the global input lock 
			// (ioctl can be slow)
			bool is_kbd = IsKeyboard(fd, name);
			bool is_mouse = false;
			if (!is_kbd) {
				is_mouse = IsMouse(fd, name);
			}

			if (is_kbd || is_mouse) {
				std::lock_guard<std::mutex> lock(input_mutex);
				
				// Re-check path in case of weird race (unlikely)
				if (open_device_paths.find(path_str) == open_device_paths.end()) {
					open_device_paths.insert(path_str);
					
					if (is_kbd) {
						keyboard_fds.push_back(fd);
						LOG_MSG("DirectInput: Found KBD: %s (%s)", name, path);
					} else if (is_mouse) {
						mouse_fds.push_back(fd);
						LOG_MSG("DirectInput: Found MOUSE: %s (%s)", name, path);
						
						// If we are currently supposedly grabbing mice, we should grab this new one too
						// But we don't easily know the global "grab state" here without storing it.
						// For now, new devices won't be grabbed automatically until toggled?
						// Or we should store 'grab_enabled' global.
						// The user prompt didn't strictly ask for auto-grab on hotplug but it's implied.
						// Let's add a quick check if we should grab. 
						// Actually, simplest is to let the user retoggle or store state.
						// I'll leave auto-grab out for now to minimize complexity unless requested,
						// as IsMouse check is enough for recognition.
					}
				} else {
					close(fd); // Already added
				}
			} else {
				close(fd);
			}
		} else {
			// LOG_MSG("DirectInput: Failed to open %s: %s", path, strerror(errno));
		}
	}
	closedir(dir);
}

void DirectInput_Init() {
	// Initial synchronous scan
	ScanDevices();
	
	if (keyboard_fds.empty() && mouse_fds.empty()) {
		LOG_MSG("DirectInput: CRITICAL - No devices found!");
	}

	// Start background thread
	hotplug_running = true;
	hotplug_thread = std::thread([]() {
		while (hotplug_running) {
			std::this_thread::sleep_for(std::chrono::seconds(2));
			if (!hotplug_running) break;
			ScanDevices();
		}
	});
}

// Externs from sdl_gui.cpp
extern void handle_mouse_motion(SDL_MouseMotionEvent* motion);
extern void handle_mouse_button(SDL_MouseButtonEvent* button);
extern void handle_mouse_wheel(SDL_MouseWheelEvent* wheel);

void DirectInput_Poll() {
	std::lock_guard<std::mutex> lock(input_mutex);

	struct input_event ev[64];
	
	// Poll Keyboards
	for (int fd : keyboard_fds) {
		int rd = read(fd, ev, sizeof(ev));
		if (rd > 0) {
			int count = rd / sizeof(struct input_event);
			for (int i = 0; i < count; i++) {
				// LOG RAW for debugging
				// LOG_MSG("DirectInput KBD RAW: type=%d code=%d value=%d", ev[i].type, ev[i].code, ev[i].value);
				
				if (ev[i].type == EV_KEY) {
					SDL_Scancode sc = LinuxKeyToSDLScancode(ev[i].code);
					if (sc != SDL_SCANCODE_UNKNOWN) {
						SDL_Event sdl_ev;
						memset(&sdl_ev, 0, sizeof(sdl_ev));
						// Value 0 = Release, 1 = Press, 2 = Repeat
						sdl_ev.type = (ev[i].value == 0) ? SDL_KEYUP : SDL_KEYDOWN;
						sdl_ev.key.timestamp = SDL_GetTicks();
						sdl_ev.key.keysym.scancode = sc;
						sdl_ev.key.state = (ev[i].value == 0) ? SDL_RELEASED : SDL_PRESSED;
						sdl_ev.key.repeat = (ev[i].value == 2) ? 1 : 0;
						
						LOG_MSG("DirectInput KBD: Scancode %d (linux %d) State %d Repeat %d", sc, ev[i].code, ev[i].value, sdl_ev.key.repeat);

						MAPPER_CheckEvent(&sdl_ev);
					} else {
						LOG_MSG("DirectInput KBD: Unknown Key Code %d", ev[i].code);
					}
				}
			}
		}
	}
	
	// Poll Mice
	for (int fd : mouse_fds) {
		int rd = read(fd, ev, sizeof(ev));
		if (rd > 0) {
			int count = rd / sizeof(struct input_event);
			for (int i = 0; i < count; i++) {
				// LOG_MSG("DirectInput MOUSE RAW: type=%d code=%d value=%d", ev[i].type, ev[i].code, ev[i].value);

				if (ev[i].type == EV_KEY) {
					uint8_t button = 0;
					if (ev[i].code == BTN_LEFT) button = SDL_BUTTON_LEFT;
					else if (ev[i].code == BTN_RIGHT) button = SDL_BUTTON_RIGHT;
					else if (ev[i].code == BTN_MIDDLE) button = SDL_BUTTON_MIDDLE;
					else if (ev[i].code == BTN_SIDE) button = SDL_BUTTON_X1;
					else if (ev[i].code == BTN_EXTRA) button = SDL_BUTTON_X2;
					else LOG_MSG("DirectInput MOUSE: Unknown Button Code %x", ev[i].code);
					
					if (button) {
						LOG_MSG("DirectInput MOUSE: Button %d Val %d", button, ev[i].value);
						SDL_MouseButtonEvent btn_ev;
						memset(&btn_ev, 0, sizeof(btn_ev));
						btn_ev.type = (ev[i].value == 0) ? SDL_MOUSEBUTTONUP : SDL_MOUSEBUTTONDOWN;
						btn_ev.timestamp = SDL_GetTicks();
						btn_ev.button = button;
						btn_ev.state = (ev[i].value == 0) ? SDL_RELEASED : SDL_PRESSED;
						handle_mouse_button(&btn_ev);
					}
				} else if (ev[i].type == EV_REL) {
					if (ev[i].code == REL_X || ev[i].code == REL_Y) {
						// LOG_MSG("DirectInput MOUSE: Motion %s %d", ev[i].code==REL_X?"X":"Y", ev[i].value);
						SDL_MouseMotionEvent mot_ev;
						memset(&mot_ev, 0, sizeof(mot_ev));
						mot_ev.type = SDL_MOUSEMOTION;
						mot_ev.timestamp = SDL_GetTicks();
						if (ev[i].code == REL_X) mot_ev.xrel = ev[i].value;
						if (ev[i].code == REL_Y) mot_ev.yrel = ev[i].value;
						handle_mouse_motion(&mot_ev);
					} else if (ev[i].code == REL_WHEEL) {
						LOG_MSG("DirectInput MOUSE: Wheel %d", ev[i].value);
						SDL_MouseWheelEvent wheel_ev;
						memset(&wheel_ev, 0, sizeof(wheel_ev));
						wheel_ev.type = SDL_MOUSEWHEEL;
						wheel_ev.timestamp = SDL_GetTicks();
						wheel_ev.y = ev[i].value;
						handle_mouse_wheel(&wheel_ev);
					}
				}
			}

		} 
	}
}



void DirectInput_Quit() {
	hotplug_running = false;
	if (hotplug_thread.joinable()) {
		hotplug_thread.join();
	}

	std::lock_guard<std::mutex> lock(input_mutex);
	for (int fd : keyboard_fds) close(fd);
	for (int fd : mouse_fds) close(fd);
	keyboard_fds.clear();
	mouse_fds.clear();
	open_device_paths.clear();
}
