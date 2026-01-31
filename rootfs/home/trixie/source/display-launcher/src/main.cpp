/*
 * display-launcher - Launch applications on specific displays with window management
 *
 * Usage: display-launcher [OPTIONS] -- command [args...]
 *
 * Options:
 *   --display top|bottom      Target display (default: top)
 *   --size fullscreen|windowed  Window size mode (default: fullscreen)
 *   --timeout <ms>            Window detection timeout in ms (default: 5000)
 *   --name <substring>        Find window by name instead of PID
 *   --debug                   Enable debug output
 *   --help                    Show this help
 */

#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/Xutil.h>
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
    bool displayTop = true;       // true = top, false = bottom
    bool fullscreen = true;       // true = fullscreen, false = windowed
    int timeoutMs = 5000;
    std::string windowName;       // Optional: find by window name substring
    bool debug = false;
    std::vector<char*> command;
};

struct DisplayInfo {
    int x, y, width, height;
    std::string name;
};

bool g_debug = false;
bool g_xerror = false;

#define DEBUG(...) do { if (g_debug) { fprintf(stderr, "[DEBUG] "); fprintf(stderr, __VA_ARGS__); fprintf(stderr, "\n"); } } while(0)

// X error handler to catch BadWindow errors gracefully
int xerrorHandler(Display* /*dpy*/, XErrorEvent* err) {
    g_xerror = true;
    if (g_debug) {
        fprintf(stderr, "[DEBUG] X error: code=%d, request=%d, resource=0x%lx\n",
                err->error_code, err->request_code, err->resourceid);
    }
    return 0;
}

void printUsage(const char* progName) {
    fprintf(stderr, "Usage: %s [OPTIONS] -- command [args...]\n\n", progName);
    fprintf(stderr, "Options:\n");
    fprintf(stderr, "  --display top|bottom      Target display (default: top)\n");
    fprintf(stderr, "  --size fullscreen|windowed  Window size mode (default: fullscreen)\n");
    fprintf(stderr, "  --timeout <ms>            Window detection timeout in ms (default: 5000)\n");
    fprintf(stderr, "  --name <substring>        Find window by name instead of PID\n");
    fprintf(stderr, "  --debug                   Enable debug output\n");
    fprintf(stderr, "  --help                    Show this help\n");
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
        else if (strcmp(argv[i], "--debug") == 0) {
            config.debug = true;
            g_debug = true;
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
            if (strcmp(argv[i], "fullscreen") == 0) {
                config.fullscreen = true;
            } else if (strcmp(argv[i], "windowed") == 0) {
                config.fullscreen = false;
            } else {
                fprintf(stderr, "Error: --size must be 'fullscreen' or 'windowed'\n");
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
        else if (strcmp(argv[i], "--name") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "Error: --name requires an argument\n");
                return false;
            }
            i++;
            config.windowName = argv[i];
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

    std::vector<DisplayInfo> outputs;

    for (int i = 0; i < res->noutput; i++) {
        XRROutputInfo* outInfo = XRRGetOutputInfo(dpy, res, res->outputs[i]);
        if (!outInfo) continue;

        if (outInfo->connection == RR_Connected && outInfo->crtc) {
            XRRCrtcInfo* crtcInfo = XRRGetCrtcInfo(dpy, res, outInfo->crtc);
            if (crtcInfo) {
                DisplayInfo out;
                out.x = crtcInfo->x;
                out.y = crtcInfo->y;
                out.width = crtcInfo->width;
                out.height = crtcInfo->height;
                out.name = outInfo->name ? outInfo->name : "unknown";
                outputs.push_back(out);
                DEBUG("Found display: %s at (%d,%d) %dx%d",
                      out.name.c_str(), out.x, out.y, out.width, out.height);
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

    DEBUG("Sorted displays (top to bottom):");
    for (size_t i = 0; i < outputs.size(); i++) {
        DEBUG("  [%zu] %s at (%d,%d)", i, outputs[i].name.c_str(), outputs[i].x, outputs[i].y);
    }

    // Select the appropriate display
    size_t idx = top ? 0 : (outputs.size() > 1 ? 1 : 0);
    info = outputs[idx];

    return true;
}

std::string getWindowName(Display* dpy, Window win) {
    g_xerror = false;
    char* name = nullptr;
    if (XFetchName(dpy, win, &name) && name) {
        XSync(dpy, False);
        if (!g_xerror) {
            std::string result(name);
            XFree(name);
            return result;
        }
        XFree(name);
    }

    // Try _NET_WM_NAME
    Atom netWmName = XInternAtom(dpy, "_NET_WM_NAME", True);
    Atom utf8 = XInternAtom(dpy, "UTF8_STRING", True);
    if (netWmName != None) {
        Atom type;
        int format;
        unsigned long nitems, after;
        unsigned char* data = nullptr;

        g_xerror = false;
        if (XGetWindowProperty(dpy, win, netWmName, 0, 256, False, utf8,
                               &type, &format, &nitems, &after, &data) == Success && data) {
            XSync(dpy, False);
            if (!g_xerror) {
                std::string result((char*)data);
                XFree(data);
                return result;
            }
            XFree(data);
        }
    }

    return "";
}

Window findWindowByName(Display* dpy, Window root, const std::string& nameSubstr) {
    Window parent, *children = nullptr;
    unsigned int nchildren;

    if (!XQueryTree(dpy, root, &root, &parent, &children, &nchildren)) {
        return None;
    }

    Window result = None;

    for (unsigned int i = 0; i < nchildren && result == None; i++) {
        std::string winName = getWindowName(dpy, children[i]);
        if (!winName.empty() && winName.find(nameSubstr) != std::string::npos) {
            DEBUG("Found window by name: '%s' (0x%lx)", winName.c_str(), children[i]);
            result = children[i];
        }

        if (result == None) {
            result = findWindowByName(dpy, children[i], nameSubstr);
        }
    }

    if (children) XFree(children);
    return result;
}

// Get the window's PID from _NET_WM_PID property
pid_t getWindowPid(Display* dpy, Window win) {
    Atom pidAtom = XInternAtom(dpy, "_NET_WM_PID", True);
    if (pidAtom == None) return 0;

    Atom type;
    int format;
    unsigned long nitems, after;
    unsigned char* data = nullptr;
    pid_t result = 0;

    g_xerror = false;
    if (XGetWindowProperty(dpy, win, pidAtom, 0, 1, False, XA_CARDINAL,
                           &type, &format, &nitems, &after, &data) == Success) {
        XSync(dpy, False);
        if (!g_xerror && data && nitems > 0) {
            result = *(pid_t*)data;
        }
        if (data) XFree(data);
    }
    return result;
}

// Check if childPid is a descendant of parentPid
bool isChildProcess(pid_t parentPid, pid_t childPid) {
    if (childPid == parentPid) return true;

    char path[256];
    snprintf(path, sizeof(path), "/proc/%d/stat", childPid);

    FILE* f = fopen(path, "r");
    if (!f) return false;

    int pid;
    char comm[256];
    char state;
    int ppid;

    if (fscanf(f, "%d %s %c %d", &pid, comm, &state, &ppid) == 4) {
        fclose(f);
        if (ppid == parentPid) return true;
        if (ppid > 1) return isChildProcess(parentPid, ppid);
    } else {
        fclose(f);
    }
    return false;
}

// Find window by PID using _NET_CLIENT_LIST (like wmctrl does)
Window findWindowByPid(Display* dpy, Window root, pid_t pid) {
    Atom clientListAtom = XInternAtom(dpy, "_NET_CLIENT_LIST", True);
    if (clientListAtom == None) {
        DEBUG("_NET_CLIENT_LIST atom not found");
        return None;
    }

    Atom type;
    int format;
    unsigned long nitems, after;
    unsigned char* data = nullptr;

    g_xerror = false;
    if (XGetWindowProperty(dpy, root, clientListAtom, 0, 65536, False, XA_WINDOW,
                           &type, &format, &nitems, &after, &data) != Success || !data) {
        DEBUG("Failed to get _NET_CLIENT_LIST");
        return None;
    }

    Window* clients = (Window*)data;
    Window result = None;

    DEBUG("Checking %lu windows in _NET_CLIENT_LIST for PID %d", nitems, pid);

    for (unsigned long i = 0; i < nitems && result == None; i++) {
        // Get PID first - if window is invalid, this returns 0
        pid_t winPid = getWindowPid(dpy, clients[i]);
        if (winPid == 0) {
            DEBUG("  Window 0x%lx: (invalid or no PID)", clients[i]);
            continue;
        }

        std::string name = getWindowName(dpy, clients[i]);
        DEBUG("  Window 0x%lx: PID=%d, name='%s'", clients[i], winPid, name.c_str());

        // Check if window's PID matches or is a child of our launched process
        if (isChildProcess(pid, winPid)) {
            DEBUG("Found window by PID %d (window PID %d): '%s' (0x%lx)",
                  pid, winPid, name.c_str(), clients[i]);
            result = clients[i];
        }
    }

    XFree(data);
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
    XSync(dpy, False);
}

void moveResizeWindow(Display* dpy, Window win, int x, int y, int w, int h) {
    // Use _NET_MOVERESIZE_WINDOW for better WM compatibility
    XEvent event;
    memset(&event, 0, sizeof(event));

    Atom moveResize = XInternAtom(dpy, "_NET_MOVERESIZE_WINDOW", False);

    event.xclient.type = ClientMessage;
    event.xclient.window = win;
    event.xclient.message_type = moveResize;
    event.xclient.format = 32;
    // Gravity + flags for x, y, width, height
    event.xclient.data.l[0] = (1 << 8) | (1 << 9) | (1 << 10) | (1 << 11) | 0; // StaticGravity + all flags
    event.xclient.data.l[1] = x;
    event.xclient.data.l[2] = y;
    event.xclient.data.l[3] = w;
    event.xclient.data.l[4] = h;

    XSendEvent(dpy, DefaultRootWindow(dpy), False,
               SubstructureRedirectMask | SubstructureNotifyMask, &event);
    XSync(dpy, False);
}

bool positionWindow(Display* dpy, Window win, const DisplayInfo& display, bool fullscreen) {
    Atom stateFullscreen = XInternAtom(dpy, "_NET_WM_STATE_FULLSCREEN", False);
    Atom stateAbove = XInternAtom(dpy, "_NET_WM_STATE_ABOVE", False);
    Atom stateMaxH = XInternAtom(dpy, "_NET_WM_STATE_MAXIMIZED_HORZ", False);
    Atom stateMaxV = XInternAtom(dpy, "_NET_WM_STATE_MAXIMIZED_VERT", False);

    DEBUG("Positioning window 0x%lx to display %s (%d,%d %dx%d), fullscreen=%d",
          win, display.name.c_str(), display.x, display.y, display.width, display.height, fullscreen);

    // First, remove any existing fullscreen/maximized state
    DEBUG("Removing existing window states...");
    sendNetWMState(dpy, win, false, stateFullscreen);
    sendNetWMState(dpy, win, false, stateMaxH, stateMaxV);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Move window to target display position
    DEBUG("Moving window to (%d, %d)...", display.x, display.y);
    XMoveWindow(dpy, win, display.x, display.y);
    XSync(dpy, False);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    if (fullscreen) {
        // Set fullscreen and above states (like the original script did)
        DEBUG("Setting fullscreen and above states...");
        sendNetWMState(dpy, win, true, stateFullscreen, stateAbove);
    } else {
        // Resize to fit within display with some margin
        int margin = 20;
        int w = display.width - margin * 2;
        int h = display.height - margin * 2;
        DEBUG("Resizing window to %dx%d at (%d, %d)...", w, h, display.x + margin, display.y + margin);
        moveResizeWindow(dpy, win, display.x + margin, display.y + margin, w, h);
    }

    // Raise the window
    XRaiseWindow(dpy, win);
    XSync(dpy, False);

    DEBUG("Window positioning complete");
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

    // Install error handler to catch BadWindow errors gracefully
    XSetErrorHandler(xerrorHandler);

    // Get target display geometry
    DisplayInfo displayInfo;
    if (!getDisplayGeometry(dpy, config.displayTop, displayInfo)) {
        XCloseDisplay(dpy);
        return 1;
    }

    printf("Target display: %s (%s at %d,%d %dx%d)\n",
           config.displayTop ? "top" : "bottom",
           displayInfo.name.c_str(),
           displayInfo.x, displayInfo.y, displayInfo.width, displayInfo.height);
    printf("Size mode: %s\n", config.fullscreen ? "fullscreen" : "windowed");
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
    printf("Waiting for window (PID %d", pid);
    if (!config.windowName.empty()) {
        printf(", name contains '%s'", config.windowName.c_str());
    }
    printf(")...\n");

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

        // Try to find the window
        if (!config.windowName.empty()) {
            win = findWindowByName(dpy, DefaultRootWindow(dpy), config.windowName);
        }
        if (win == None) {
            win = findWindowByPid(dpy, DefaultRootWindow(dpy), pid);
        }

        if (win == None) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }

    if (win != None) {
        std::string winName = getWindowName(dpy, win);
        printf("Found window 0x%lx ('%s'), positioning...\n", win, winName.c_str());
        positionWindow(dpy, win, displayInfo, config.fullscreen);
        printf("Done.\n");
    }

    XCloseDisplay(dpy);
    return 0;
}
