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
#include "touch_ipc.h"
#include "Theme.h"
#include "UInput.h"

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
            cairo_set_source_surface(cr, btn.image_surface, btn.x, btn.y);
            cairo_paint(cr);
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
            
            cairo_text_extents_t extents;
            cairo_text_extents(cr, btn.label.c_str(), &extents);
            
            double x = btn.x + (btn.w / 2.0) - (extents.width / 2.0) - extents.x_bearing;
            double y = btn.y + (btn.h / 2.0) - (extents.height / 2.0) - extents.y_bearing;
            
            cairo_move_to(cr, x, y);
            cairo_show_text(cr, btn.label.c_str());
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
std::string last_app_path = "";

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

void check_context() {
    Window active = get_active_window();
    // Ignore if active window is ourself or None
    if (active != None && active != win && active != last_active_window) {
        last_active_window = active;
        std::string app_path = get_process_path(active);
        
        if (!app_path.empty() && app_path != last_app_path) {
            last_app_path = app_path;
            printf("Active App: %s\n", app_path.c_str());

            // Construct theme path
            // ~/.context-osk/themes/<path>.theme
            // e.g. /usr/bin/pcsx -> ~/.context-osk/themes/usr/bin/pcsx.theme
            
            const char* home = getenv("HOME");
            std::string theme_path;
            if (home) {
                theme_path = std::string(home) + "/.context-osk/themes" + app_path + ".theme";
            } else {
                 theme_path = "./themes" + app_path + ".theme";
            }

            printf("Looking for theme: %s\n", theme_path.c_str());
            
            // Reload theme
            if (current_theme.load(theme_path)) {
                printf("Theme loaded!\n");
                // Resize window if height changed
                 // Use theme height
                int win_h = current_theme.height;
                if (win_h <= 0) win_h = 200; 
                int win_y = screen_height - win_h;
                XMoveResizeWindow(dis, win, 0, win_y, screen_width, win_h);
                register_window_region(); // Re-register with new coordinates
            } else {
                 printf("Theme not found, sticking to current or default.\n");
            }
            render();
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
