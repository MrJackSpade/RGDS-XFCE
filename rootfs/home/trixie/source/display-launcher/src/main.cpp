/*
 * display-launcher - Launch applications on specific displays with window management
 *
 * Usage: display-launcher [OPTIONS] -- command [args...]
 *
 * Options:
 *   --display top|bottom    Target display (default: top)
 *   --size maximize|windowed  Window size mode (default: maximize)
 *   --timeout <ms>          Window detection timeout in ms (default: 5000)
 *   --help                  Show this help
 */

#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/extensions/Xrandr.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>
#include <sys/wait.h>
#include <chrono>
#include <thread>
#include <vector>
#include <string>

struct Config {
    bool displayTop = true;      // true = top, false = bottom
    bool maximize = true;        // true = maximize, false = windowed
    int timeoutMs = 5000;
    std::vector<char*> command;
};

struct DisplayInfo {
    int x, y, width, height;
};

void printUsage(const char* progName) {
    fprintf(stderr, "Usage: %s [OPTIONS] -- command [args...]\n\n", progName);
    fprintf(stderr, "Options:\n");
    fprintf(stderr, "  --display top|bottom    Target display (default: top)\n");
    fprintf(stderr, "  --size maximize|windowed  Window size mode (default: maximize)\n");
    fprintf(stderr, "  --timeout <ms>          Window detection timeout in ms (default: 5000)\n");
    fprintf(stderr, "  --help                  Show this help\n");
}

bool parseArgs(int argc, char** argv, Config& config) {
    int i = 1;

    while (i < argc) {
        if (strcmp(argv[i], "--") == 0) {
            i++;
            break;
        }

        if (strcmp(argv[i], "--help") == 0) {
            printUsage(argv[0]);
            exit(0);
        }
        else if (strcmp(argv[i], "--display") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "Error: --display requires an argument\n");
                return false;
            }
            i++;
            if (strcmp(argv[i], "top") == 0) {
                config.displayTop = true;
            } else if (strcmp(argv[i], "bottom") == 0) {
                config.displayTop = false;
            } else {
                fprintf(stderr, "Error: --display must be 'top' or 'bottom'\n");
                return false;
            }
        }
        else if (strcmp(argv[i], "--size") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "Error: --size requires an argument\n");
                return false;
            }
            i++;
            if (strcmp(argv[i], "maximize") == 0) {
                config.maximize = true;
            } else if (strcmp(argv[i], "windowed") == 0) {
                config.maximize = false;
            } else {
                fprintf(stderr, "Error: --size must be 'maximize' or 'windowed'\n");
                return false;
            }
        }
        else if (strcmp(argv[i], "--timeout") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "Error: --timeout requires an argument\n");
                return false;
            }
            i++;
            config.timeoutMs = atoi(argv[i]);
            if (config.timeoutMs <= 0) {
                fprintf(stderr, "Error: --timeout must be a positive integer\n");
                return false;
            }
        }
        else if (argv[i][0] == '-') {
            fprintf(stderr, "Error: Unknown option '%s'\n", argv[i]);
            return false;
        }
        else {
            // Start of command without --
            break;
        }
        i++;
    }

    // Remaining arguments are the command
    while (i < argc) {
        config.command.push_back(argv[i]);
        i++;
    }
    config.command.push_back(nullptr);

    if (config.command.size() <= 1) {
        fprintf(stderr, "Error: No command specified\n");
        return false;
    }

    return true;
}

bool getDisplayGeometry(Display* dpy, bool top, DisplayInfo& info) {
    Window root = DefaultRootWindow(dpy);
    XRRScreenResources* res = XRRGetScreenResources(dpy, root);

    if (!res) {
        fprintf(stderr, "Error: Failed to get screen resources\n");
        return false;
    }

    // Collect all active outputs with their positions
    struct OutputInfo {
        int x, y, width, height;
    };
    std::vector<OutputInfo> outputs;

    for (int i = 0; i < res->noutput; i++) {
        XRROutputInfo* outInfo = XRRGetOutputInfo(dpy, res, res->outputs[i]);
        if (!outInfo) continue;

        if (outInfo->connection == RR_Connected && outInfo->crtc) {
            XRRCrtcInfo* crtcInfo = XRRGetCrtcInfo(dpy, res, outInfo->crtc);
            if (crtcInfo) {
                OutputInfo out;
                out.x = crtcInfo->x;
                out.y = crtcInfo->y;
                out.width = crtcInfo->width;
                out.height = crtcInfo->height;
                outputs.push_back(out);
                XRRFreeCrtcInfo(crtcInfo);
            }
        }
        XRRFreeOutputInfo(outInfo);
    }

    XRRFreeScreenResources(res);

    if (outputs.empty()) {
        fprintf(stderr, "Error: No active displays found\n");
        return false;
    }

    // Sort by Y position (top to bottom)
    for (size_t i = 0; i < outputs.size() - 1; i++) {
        for (size_t j = i + 1; j < outputs.size(); j++) {
            if (outputs[j].y < outputs[i].y) {
                std::swap(outputs[i], outputs[j]);
            }
        }
    }

    // Select the appropriate display
    size_t idx = top ? 0 : (outputs.size() > 1 ? 1 : 0);
    info.x = outputs[idx].x;
    info.y = outputs[idx].y;
    info.width = outputs[idx].width;
    info.height = outputs[idx].height;

    return true;
}

Window findWindowByPid(Display* dpy, Window root, pid_t pid) {
    Atom pidAtom = XInternAtom(dpy, "_NET_WM_PID", True);
    if (pidAtom == None) return None;

    Window parent, *children = nullptr;
    unsigned int nchildren;

    if (!XQueryTree(dpy, root, &root, &parent, &children, &nchildren)) {
        return None;
    }

    Window result = None;

    for (unsigned int i = 0; i < nchildren && result == None; i++) {
        Atom type;
        int format;
        unsigned long nitems, after;
        unsigned char* data = nullptr;

        if (XGetWindowProperty(dpy, children[i], pidAtom, 0, 1, False, XA_CARDINAL,
                               &type, &format, &nitems, &after, &data) == Success) {
            if (data) {
                if (nitems > 0) {
                    pid_t winPid = *(pid_t*)data;
                    if (winPid == pid) {
                        result = children[i];
                    }
                }
                XFree(data);
            }
        }

        if (result == None) {
            result = findWindowByPid(dpy, children[i], pid);
        }
    }

    if (children) XFree(children);
    return result;
}

void sendNetWMState(Display* dpy, Window win, bool add, Atom state1, Atom state2 = None) {
    XEvent event;
    memset(&event, 0, sizeof(event));

    event.xclient.type = ClientMessage;
    event.xclient.window = win;
    event.xclient.message_type = XInternAtom(dpy, "_NET_WM_STATE", False);
    event.xclient.format = 32;
    event.xclient.data.l[0] = add ? 1 : 0;  // _NET_WM_STATE_ADD or _NET_WM_STATE_REMOVE
    event.xclient.data.l[1] = state1;
    event.xclient.data.l[2] = state2;
    event.xclient.data.l[3] = 1;  // Source indication: normal application

    XSendEvent(dpy, DefaultRootWindow(dpy), False,
               SubstructureRedirectMask | SubstructureNotifyMask, &event);
}

bool positionWindow(Display* dpy, Window win, const DisplayInfo& display, bool maximize) {
    // First, ensure the window is not in any special state
    Atom stateFullscreen = XInternAtom(dpy, "_NET_WM_STATE_FULLSCREEN", False);
    Atom stateMaxH = XInternAtom(dpy, "_NET_WM_STATE_MAXIMIZED_HORZ", False);
    Atom stateMaxV = XInternAtom(dpy, "_NET_WM_STATE_MAXIMIZED_VERT", False);

    // Remove any existing maximized/fullscreen state
    sendNetWMState(dpy, win, false, stateFullscreen);
    sendNetWMState(dpy, win, false, stateMaxH, stateMaxV);
    XSync(dpy, False);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // Move window to target display
    XMoveWindow(dpy, win, display.x, display.y);
    XSync(dpy, False);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    if (maximize) {
        // Set maximized state (covers the display but keeps window decorations)
        sendNetWMState(dpy, win, true, stateMaxH, stateMaxV);
    } else {
        // Resize to fit within display with some margin
        int margin = 20;
        XResizeWindow(dpy, win, display.width - margin * 2, display.height - margin * 2);
        XMoveWindow(dpy, win, display.x + margin, display.y + margin);
    }

    // Raise the window
    XRaiseWindow(dpy, win);
    XSync(dpy, False);

    return true;
}

int main(int argc, char** argv) {
    Config config;

    if (!parseArgs(argc, argv, config)) {
        printUsage(argv[0]);
        return 1;
    }

    // Open display early to validate
    Display* dpy = XOpenDisplay(nullptr);
    if (!dpy) {
        fprintf(stderr, "Error: Cannot open X display\n");
        return 1;
    }

    // Get target display geometry
    DisplayInfo displayInfo;
    if (!getDisplayGeometry(dpy, config.displayTop, displayInfo)) {
        XCloseDisplay(dpy);
        return 1;
    }

    printf("Target display: %s (%d,%d %dx%d)\n",
           config.displayTop ? "top" : "bottom",
           displayInfo.x, displayInfo.y, displayInfo.width, displayInfo.height);
    printf("Size mode: %s\n", config.maximize ? "maximize" : "windowed");
    printf("Launching: %s\n", config.command[0]);

    // Fork and exec the command
    pid_t pid = fork();

    if (pid < 0) {
        perror("fork");
        XCloseDisplay(dpy);
        return 1;
    }

    if (pid == 0) {
        // Child process
        execvp(config.command[0], config.command.data());
        perror("execvp");
        _exit(127);
    }

    // Parent process - wait for window to appear
    printf("Waiting for window from PID %d...\n", pid);

    Window win = None;
    auto startTime = std::chrono::steady_clock::now();

    while (win == None) {
        auto elapsed = std::chrono::steady_clock::now() - startTime;
        if (std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count() >= config.timeoutMs) {
            fprintf(stderr, "Warning: Timeout waiting for window\n");
            break;
        }

        // Check if child is still running
        int status;
        pid_t result = waitpid(pid, &status, WNOHANG);
        if (result != 0) {
            if (WIFEXITED(status)) {
                fprintf(stderr, "Error: Process exited with code %d\n", WEXITSTATUS(status));
            } else {
                fprintf(stderr, "Error: Process terminated abnormally\n");
            }
            XCloseDisplay(dpy);
            return 1;
        }

        win = findWindowByPid(dpy, DefaultRootWindow(dpy), pid);
        if (win == None) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }

    if (win != None) {
        printf("Found window 0x%lx, positioning...\n", win);
        positionWindow(dpy, win, displayInfo, config.maximize);
        printf("Done.\n");
    }

    XCloseDisplay(dpy);
    return 0;
}
