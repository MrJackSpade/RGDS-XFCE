#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <cairo/cairo.h>
#include <cairo/cairo-xlib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <fcntl.h>
#include <sys/un.h>
#include <fcntl.h>
#include <fnmatch.h>
#include <dirent.h>
#include <time.h>
#include "touch_ipc.h"
#include "Theme.h"
#include "UInput.h"

// X11 keycodes for modifier keys
const int KEYCODE_LSHIFT = 50;
const int KEYCODE_RSHIFT = 62;
const int KEYCODE_CAPS = 66;

// Globals
Display* dis;
int screen;
Window win;
int screen_width, screen_height;
Theme current_theme;
Button* pressed_button = nullptr; // Track which button is currently held
int drag_start_y = 0;
bool is_dragging = false;

int touch_proxy_fd = -1; // Socket to touch-scroll
UInput uinput_dev;

int x_error_handler(Display* display, XErrorEvent* error) {
    if (error->error_code == BadWindow) {
        return 0;
    }
    char msg[80];
    XGetErrorText(display, error->error_code, msg, sizeof(msg));
    fprintf(stderr, "X Error: %s\n", msg);
    return 0;
}

void create_window() {
    dis = XOpenDisplay(NULL);
    if (!dis) {
        fprintf(stderr, "Cannot open display\n");
        exit(1);
    }
    screen = DefaultScreen(dis);
    screen_width = DisplayWidth(dis, screen);
    screen_height = DisplayHeight(dis, screen);

    // Use theme height
    int win_h = current_theme.height;
    if (win_h <= 0) win_h = 200; // Fallback
    int win_y = screen_height - win_h;
    
    XSetWindowAttributes attrs;
    attrs.override_redirect = True;
    attrs.background_pixel = BlackPixel(dis, screen);

    win = XCreateWindow(dis, RootWindow(dis, screen), 
                        0, win_y, screen_width, win_h, 
                        0, CopyFromParent, InputOutput, CopyFromParent,
                        CWOverrideRedirect | CWBackPixel, &attrs);

    XStoreName(dis, win, "context-osk");
    XClassHint *ch = XAllocClassHint();
    if (ch) {
        ch->res_name = (char*)"context-osk";
        ch->res_class = (char*)"Context-OSK";
        XSetClassHint(dis, win, ch);
        XFree(ch);
    }

    Atom type = XInternAtom(dis, "_NET_WM_WINDOW_TYPE", False);
    Atom value = XInternAtom(dis, "_NET_WM_WINDOW_TYPE_DOCK", False);
    XChangeProperty(dis, win, type, XA_ATOM, 32, PropModeReplace, (unsigned char*)&value, 1);

    // Select inputs
    XSelectInput(dis, win, ExposureMask | ButtonPressMask | ButtonReleaseMask | KeyPressMask | ButtonMotionMask);

    XMapWindow(dis, win);
    XFlush(dis);
}

// Helper functions for modifier-aware label display
bool is_shift_active() {
    for (const auto& btn : current_theme.buttons) {
        if ((btn.keycode == KEYCODE_LSHIFT || btn.keycode == KEYCODE_RSHIFT)
            && btn.is_pressed) {
            return true;
        }
    }
    return false;
}

bool is_caps_active() {
    for (const auto& btn : current_theme.buttons) {
        if (btn.keycode == KEYCODE_CAPS && btn.is_pressed) {
            return true;
        }
    }
    return false;
}

const std::string& get_effective_label(const Button& btn) {
    bool shift = is_shift_active();
    bool caps = is_caps_active();

    // For letter keys: Caps XOR Shift gives uppercase
    // If caps_label is set, use it when caps is active and shift is not
    if (caps && !shift && !btn.caps_label.empty()) {
        return btn.caps_label;
    }
    // Shift overrides (or Shift+Caps for letters gives lowercase via default label)
    if (shift && !btn.shift_label.empty()) {
        return btn.shift_label;
    }
    return btn.label;
}

void render() {
    cairo_surface_t *surface = cairo_xlib_surface_create(dis, win, DefaultVisual(dis, screen), screen_width, current_theme.height);
    cairo_t *cr = cairo_create(surface);
    
    // Double buffering: Push a group to draw offscreen first
    cairo_push_group(cr);

    // Draw background
    cairo_set_source_rgb(cr, current_theme.r, current_theme.g, current_theme.b);
    cairo_paint(cr);

    // Draw buttons
    for (const auto& btn : current_theme.buttons) {
        if (btn.image_surface) {
            // Image is already scaled to button dimensions
            // Clip to button area and draw
            cairo_save(cr);
            cairo_rectangle(cr, btn.x, btn.y, btn.w, btn.h);
            cairo_clip(cr);
            cairo_set_source_surface(cr, btn.image_surface, btn.x, btn.y);
            cairo_paint(cr);
            cairo_restore(cr);
        } else {
            // Fallback rendering
            cairo_set_source_rgb(cr, 0.4, 0.4, 0.4);
            cairo_rectangle(cr, btn.x, btn.y, btn.w, btn.h);
            cairo_fill(cr);

            cairo_set_source_rgb(cr, 1, 1, 1);
            cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
            cairo_set_font_size(cr, 14);

            // Text Clipping
            cairo_save(cr);
            cairo_rectangle(cr, btn.x, btn.y, btn.w, btn.h);
            cairo_clip(cr);

            const std::string& effective_label = get_effective_label(btn);
            cairo_text_extents_t extents;
            cairo_text_extents(cr, effective_label.c_str(), &extents);

            double x = btn.x + (btn.w / 2.0) - (extents.width / 2.0) - extents.x_bearing;
            double y = btn.y + (btn.h / 2.0) - (extents.height / 2.0) - extents.y_bearing;

            cairo_move_to(cr, x, y);
            cairo_show_text(cr, effective_label.c_str());
            cairo_restore(cr);
        }

        // Visual feedback for press (overlay)
        if (btn.is_pressed) {
             cairo_set_source_rgba(cr, 1, 1, 1, 0.3);
             cairo_rectangle(cr, btn.x, btn.y, btn.w, btn.h);
             cairo_fill(cr);
        }
    }

    // Flush group to surface
    cairo_pop_group_to_source(cr);
    cairo_paint(cr);

    cairo_destroy(cr);
    cairo_surface_destroy(surface);
}

Button* hit_test(int x, int y) {
    for (auto& btn : current_theme.buttons) {
        if (x >= btn.x && x < btn.x + btn.w && y >= btn.y && y < btn.y + btn.h) {
            return &btn;
        }
    }
    return nullptr;
}
// --- Input Handling Helpers ---

void handle_input_down(int x, int y) {
    drag_start_y = y;
    is_dragging = true;

    pressed_button = hit_test(x, y);
    if (pressed_button) {
        if (pressed_button->toggle) {
            // Toggle logic
            pressed_button->is_pressed = !pressed_button->is_pressed;
            if (pressed_button->keycode > 0) {
                // Convert X11 keycode to Linux input keycode
                int linux_code = pressed_button->keycode - 8;
                if (linux_code >= 0) {
                    uinput_dev.send_key(linux_code, pressed_button->is_pressed);
                }
            }
        } else {
            // Normal button input
            pressed_button->is_pressed = true;
            if (pressed_button->keycode > 0) {
                 int linux_code = pressed_button->keycode - 8;
                 if (linux_code >= 0) {
                     uinput_dev.send_key(linux_code, true);
                 }
            }
        }
        render();
    }
}

void handle_input_up(int x, int y) {
    (void)x; (void)y;
    is_dragging = false;

    if (pressed_button) {
        if (!pressed_button->toggle) {
             // Release normal button
             pressed_button->is_pressed = false;
             if (pressed_button->keycode > 0) {
                  int linux_code = pressed_button->keycode - 8;
                  if (linux_code >= 0) {
                      uinput_dev.send_key(linux_code, false);
                  }
             }
             
             // Release any OTHER active toggle buttons (Latching behavior)
             bool render_needed = false;
             for (auto& btn : current_theme.buttons) {
                 if (btn.toggle && btn.is_pressed && &btn != pressed_button) {
                     btn.is_pressed = false;
                     if (btn.keycode > 0) {
                         int linux_code = btn.keycode - 8;
                         if (linux_code >= 0) {
                            uinput_dev.send_key(linux_code, false);
                         }
                     }
                     render_needed = true;
                 }
             }
             if (render_needed) render(); // Optimized: Only render if we changed something
        }
        // For toggle buttons, we do nothing on release
        pressed_button = nullptr;
        render();
    }
}

void handle_input_move(int x, int y) {
    if (is_dragging) {
         int delta_y = y - drag_start_y;
         // Threshold: 50 pixels down
         if (delta_y > 50) {
             // Exit!
             if (pressed_button && !pressed_button->toggle && pressed_button->is_pressed) {
                 if (pressed_button->keycode > 0) {
                     int linux_code = pressed_button->keycode - 8;
                     if (linux_code >= 0) {
                         uinput_dev.send_key(linux_code, false);
                     }
                 }
             }
             exit(0);
         }
    }
}

// --- IPC Client ---

void register_window_region() {
    if (touch_proxy_fd < 0) return;
    
    // Determine current window geometry
    XWindowAttributes attrs;
    XGetWindowAttributes(dis, win, &attrs);
    
    // Logic for screen index based on Y position
    // If y >= 480, it's screen 0 (Bottom/DSI-1)
    // If y < 480, it's screen 1 (Top/DSI-2)
    int screen_idx = (attrs.y >= 480) ? 0 : 1;
    
    TouchIpcRegisterMsg msg;
    msg.type = TOUCH_IPC_MSG_REGISTER_REGION;
    msg.region_id = 1;
    msg.screen_index = screen_idx;
    msg.x = attrs.x;
    msg.y = attrs.y;
    msg.width = attrs.width;
    msg.height = attrs.height;
    
    send(touch_proxy_fd, &msg, sizeof(msg), 0);
    printf("Registered region: ID 1, Screen %d, [%d, %d] %dx%d\n", screen_idx, attrs.x, attrs.y, attrs.width, attrs.height);
}

void connect_touch_proxy() {
    touch_proxy_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (touch_proxy_fd < 0) {
        perror("Socket creation failed");
        return;
    }
    
    struct sockaddr_un addr = {0};
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, TOUCH_IPC_SOCKET_PATH, sizeof(addr.sun_path) - 1);
    
    if (connect(touch_proxy_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("Connect failed (touch-scroll not running?)");
        close(touch_proxy_fd);
        touch_proxy_fd = -1;
        return;
    }
    
    fcntl(touch_proxy_fd, F_SETFL, O_NONBLOCK);
    printf("Connected to touch-scroll proxy.\n");
    register_window_region();
}

void process_touch_events() {
    if (touch_proxy_fd < 0) return;
    
    TouchIpcEventMsg msg;
    while (recv(touch_proxy_fd, &msg, sizeof(msg), 0) == sizeof(msg)) {
        switch (msg.type) {
            case TOUCH_IPC_MSG_TOUCH_DOWN:
                handle_input_down(msg.x, msg.y); // Relative Coords
                break;
            case TOUCH_IPC_MSG_TOUCH_UP:
                handle_input_up(msg.x, msg.y); // Relative Coords
                break;
            case TOUCH_IPC_MSG_TOUCH_MOVE:
                handle_input_move(msg.x, msg.y); // Relative Coords
                break;
        }
    }
}

// Context Monitor
Window last_active_window = None;
std::string last_window_title = "";
std::string last_applied_theme_path = "";
struct timespec last_check_time = {0, 0};

// Cache for legacy path-based theme lookup
Window cached_legacy_window = None;
std::string cached_legacy_app_path = "";
std::string cached_legacy_theme_path = "";

std::string get_process_path(Window w) {
    Atom type;
    int format;
    unsigned long nitems, bytes_after;
    unsigned char *prop = NULL;
    Atom pid_atom = XInternAtom(dis, "_NET_WM_PID", True);
    if (pid_atom == None) return "";

    if (XGetWindowProperty(dis, w, pid_atom, 0, 1, False, XA_CARDINAL, 
                           &type, &format, &nitems, &bytes_after, &prop) == Success) {
        if (prop != NULL) {
            unsigned long pid = *((unsigned long*)prop);
            XFree(prop);
            
            char proc_path[256];
            snprintf(proc_path, sizeof(proc_path), "/proc/%lu/exe", pid);
            char exe_path[1024];
            ssize_t len = readlink(proc_path, exe_path, sizeof(exe_path)-1);
            if (len != -1) {
                exe_path[len] = '\0';
                return std::string(exe_path);
            }
        }
    }
    return "";
}

Window get_active_window() {
    Atom type;
    int format;
    unsigned long nitems, bytes_after;
    unsigned char *prop = NULL;
    Atom active_atom = XInternAtom(dis, "_NET_ACTIVE_WINDOW", True);

    if (XGetWindowProperty(dis, RootWindow(dis, screen), active_atom, 0, 1, False, XA_WINDOW, 
                           &type, &format, &nitems, &bytes_after, &prop) == Success) {
        if (prop != NULL) {
            Window w = *((Window*)prop);
            XFree(prop);
            return w;
        }
    }
    return None;
}

std::string get_window_title(Window w) {
    Atom type;
    int format;
    unsigned long nitems, bytes_after;
    unsigned char *prop = NULL;
    Atom net_wm_name = XInternAtom(dis, "_NET_WM_NAME", True);

    // Try _NET_WM_NAME first (UTF-8)
    if (net_wm_name != None && XGetWindowProperty(dis, w, net_wm_name, 0, 1024, False,
        XInternAtom(dis, "UTF8_STRING", True), &type, &format, &nitems, &bytes_after, &prop) == Success) {
        if (prop != NULL) {
            std::string title((char*)prop);
            XFree(prop);
            return title;
        }
    }

    // Fallback to WM_NAME
    if (XGetWindowProperty(dis, w, XA_WM_NAME, 0, 1024, False, XA_STRING,
        &type, &format, &nitems, &bytes_after, &prop) == Success) {
        if (prop != NULL) {
            std::string title((char*)prop);
            XFree(prop);
            return title;
        }
    }
    return "";
}

// Get legacy path-based theme with caching
// Only re-checks if the window handle has changed
std::string get_legacy_path_based_theme(Window active) {
    // Return cached result if window hasn't changed
    if (active == cached_legacy_window) {
        return cached_legacy_theme_path;
    }

    // Window changed, update cache and get process path
    cached_legacy_window = active;
    cached_legacy_app_path = get_process_path(active);
    cached_legacy_theme_path = "";

    if (!cached_legacy_app_path.empty()) {
        const char* home = getenv("HOME");
        std::string themes_dir = home ? (std::string(home) + "/.context-osk/themes") : "./themes";
        std::string theme_path = themes_dir + cached_legacy_app_path + ".theme";

        if (access(theme_path.c_str(), F_OK) != -1) {
            cached_legacy_theme_path = theme_path;
        }
    }

    return cached_legacy_theme_path;
}

// Get window-title-based theme by scanning directory for wildcard matches
std::string get_window_title_based_theme(const std::string& win_title) {
    if (win_title.empty()) {
        return "";
    }

    const char* home = getenv("HOME");
    std::string themes_dir = home ? (std::string(home) + "/.context-osk/themes") : "./themes";

    DIR* dir = opendir(themes_dir.c_str());
    if (!dir) {
        return "";
    }

    std::string theme_path;
    struct dirent* ent;
    while ((ent = readdir(dir)) != NULL) {
        std::string fname = ent->d_name;
        if (fname.length() > 6 && fname.substr(fname.length() - 6) == ".theme") {
            std::string full = themes_dir + "/" + fname;
            std::string pattern = Theme::peek_match_pattern(full);
            if (!pattern.empty()) {
                if (fnmatch(pattern.c_str(), win_title.c_str(), 0) == 0) {
                    theme_path = full;
                    printf("Match found: '%s' matches pattern '%s' in %s\n",
                           win_title.c_str(), pattern.c_str(), fname.c_str());
                    break;
                }
            }
        }
    }
    closedir(dir);

    return theme_path;
}

// Get default theme path
std::string get_default_theme_path() {
    const char* home = getenv("HOME");
    if (home) {
        std::string home_default = std::string(home) + "/.context-osk/default.theme";
        if (access(home_default.c_str(), F_OK) != -1) {
            return home_default;
        }
    }

    // Fallback to current directory
    if (access("default.theme", F_OK) != -1) {
        return "default.theme";
    }

    return "";
}

// Resolve theme path by trying legacy path-based first, then window-title-based, then default
std::string resolve_theme_path(Window active, const std::string& win_title) {
    // 1. Try legacy path-based theme (with caching)
    std::string theme_path = get_legacy_path_based_theme(active);

    // 2. If not found, try window-title-based theme
    if (theme_path.empty()) {
        theme_path = get_window_title_based_theme(win_title);
    }

    // 3. If still not found, use default theme
    if (theme_path.empty()) {
        theme_path = get_default_theme_path();
    }

    return theme_path;
}

void check_context() {
    // Throttling: Run every 100ms
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    long diff_ms = (now.tv_sec - last_check_time.tv_sec) * 1000 +
                   (now.tv_nsec - last_check_time.tv_nsec) / 1000000;
    if (diff_ms < 100) return;
    last_check_time = now;

    Window active = get_active_window();
    // Ignore if active window is ourself or None
    if (active != None && active != win) {
        std::string win_title = get_window_title(active);

        // Check for changes (Window Handle OR Title)
        if (active != last_active_window || win_title != last_window_title) {
            last_active_window = active;
            last_window_title = win_title;

            printf("Context Change - Window ID: %lu | Title: %s\n", active, win_title.c_str());

            // Resolve theme path using new modular approach
            std::string theme_path = resolve_theme_path(active, win_title);

            // Only apply theme if it differs from the last applied theme
            if (!theme_path.empty() && theme_path != last_applied_theme_path) {
                printf("Loading theme: %s\n", theme_path.c_str());
                if (current_theme.load(theme_path)) {
                    last_applied_theme_path = theme_path;
                    int win_h = current_theme.height;
                    if (win_h <= 0) win_h = 200;
                    int win_y = screen_height - win_h;
                    XMoveResizeWindow(dis, win, 0, win_y, screen_width, win_h);
                    register_window_region();
                    render();
                } else {
                    // Failed to load, don't update last_applied_theme_path
                    printf("Failed to load theme: %s\n", theme_path.c_str());
                }
            } else if (!theme_path.empty()) {
                printf("Theme unchanged, skipping reload: %s\n", theme_path.c_str());
            }
        }
    }
}

int main(int argc, char** argv) {
    XSetErrorHandler(x_error_handler);
    // Load default theme initially
    // Try explicit path first, then relative
    const char* home = getenv("HOME");
    std::string default_path = "default.theme";
    if (home) {
        default_path = std::string(home) + "/.context-osk/default.theme";
    }

    if (!current_theme.load(default_path)) {
        printf("Warning: Could not load default.theme at %s\n", default_path.c_str());
        // Load fallback if needed
    }

    create_window();
    connect_touch_proxy(); // Connect and register
    
    if (!uinput_dev.init()) {
        fprintf(stderr, "Failed to initialize uinput device. Check permissions on /dev/uinput.\n");
        // We can continue running, but input won't work.
    }

    XEvent event;
    while (1) {
        if (XPending(dis) > 0) {
            XNextEvent(dis, &event);
            if (event.type == Expose && event.xexpose.count == 0) {
                render();
            } 
            else if (event.type == ButtonPress) {
                handle_input_down(event.xbutton.x, event.xbutton.y);
            }
            else if (event.type == MotionNotify) {
                handle_input_move(event.xmotion.x, event.xmotion.y);
            }
            else if (event.type == ButtonRelease) {
                handle_input_up(event.xbutton.x, event.xbutton.y);
            }
        } else {
            process_touch_events(); // Check for IPC events
            check_context();
            usleep(10000); // Reduce sleep to 10ms for better responsiveness
        }
    }
    return 0;
}
