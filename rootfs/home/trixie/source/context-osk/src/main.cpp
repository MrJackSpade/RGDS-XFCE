#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <X11/extensions/XTest.h>
#include <cairo/cairo.h>
#include <cairo/cairo-xlib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "Theme.h"

// Globals
Display* dis;
int screen;
Window win;
int screen_width, screen_height;
Theme current_theme;
Button* pressed_button = nullptr; // Track which button is currently held
int drag_start_y = 0;
bool is_dragging = false;

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
            } else {
                 printf("Theme not found, sticking to current or default.\n");
            }
            render();
        }
    }
}

int main(int argc, char** argv) {
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

    XEvent event;
    while (1) {
        if (XPending(dis) > 0) {
            XNextEvent(dis, &event);
            if (event.type == Expose && event.xexpose.count == 0) {
                render();
            } 
            else if (event.type == ButtonPress) {
                drag_start_y = event.xbutton.y;
                is_dragging = true;

                pressed_button = hit_test(event.xbutton.x, event.xbutton.y);
                if (pressed_button) {
                    if (pressed_button->toggle) {
                        // Toggle logic
                        pressed_button->is_pressed = !pressed_button->is_pressed;
                        if (pressed_button->keycode > 0) {
                            XTestFakeKeyEvent(dis, pressed_button->keycode, pressed_button->is_pressed, 0);
                            XFlush(dis);
                        }
                    } else {
                        // Normal button input
                        pressed_button->is_pressed = true;
                        if (pressed_button->keycode > 0) {
                             XTestFakeKeyEvent(dis, pressed_button->keycode, True, 0);
                             XFlush(dis);
                        }
                    }
                    render();
                }
            }
            else if (event.type == MotionNotify) {
                if (is_dragging) {
                     int delta_y = event.xmotion.y - drag_start_y;
                     // Threshold: 50 pixels down
                     if (delta_y > 50) {
                         // Exit!
                         // Release any pressed keys to avoid stuck keys
                         if (pressed_button && !pressed_button->toggle && pressed_button->is_pressed) {
                             if (pressed_button->keycode > 0) {
                                 XTestFakeKeyEvent(dis, pressed_button->keycode, False, 0);
                                 XFlush(dis);
                             }
                         }
                         exit(0);
                     }
                }
            }
            else if (event.type == ButtonRelease) {
                is_dragging = false;

                if (pressed_button) {
                    if (!pressed_button->toggle) {
                         // Release normal button
                         pressed_button->is_pressed = false;
                         if (pressed_button->keycode > 0) {
                              XTestFakeKeyEvent(dis, pressed_button->keycode, False, 0);
                              XFlush(dis);
                         }
                         
                         // Release any OTHER active toggle buttons (Latching behavior)
                         // "Stay held until a new key is clicked" -> New key click happened now (this release)
                         bool render_needed = false;
                         for (auto& btn : current_theme.buttons) {
                             if (btn.toggle && btn.is_pressed && &btn != pressed_button) {
                                 btn.is_pressed = false;
                                 if (btn.keycode > 0) {
                                     XTestFakeKeyEvent(dis, btn.keycode, False, 0);
                                 }
                                 render_needed = true;
                             }
                         }
                         if (render_needed) XFlush(dis);
                    }
                    // For toggle buttons, we do nothing on release
                    pressed_button = nullptr;
                    render();
                }
            }
        } else {
            check_context();
            usleep(100000); // 100ms poll
        }
    }
    return 0;
}
