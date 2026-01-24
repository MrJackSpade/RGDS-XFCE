# Implementation Plan: Context-Aware Virtual Keyboard (context-osk)

## Goal
Build a lightweight, context-aware virtual keyboard for Linux (X11) that switches themes based on the active application.

## Architecture
- **Language**: C++
- **Dependencies**: Xlib, XXtst (XTest), XRandR (screen size), Cairo (rendering), `stb_image` (textures).
- **Build System**: Makefile

### Components
1.  **Core (Main Loop)**: Handles X11 events, timer for polling (if needed), and signals.
2.  **WindowManager**: Creates an `OverrideRedirect` window docked to the bottom. Sets `_NET_WM_WINDOW_TYPE_DOCK` or `TOOLBAR` and inputs hints to avoid focus.
3.  **ThemeManager**: Loads theme files matching the active window's executable path.
4.  **Renderer**: Uses Cairo to draw the background and buttons.
5.  **ContextMonitor**: Listens for `PropertyNotify` on the Root Window for `_NET_ACTIVE_WINDOW` changes. Resolves PID to binary path.
6.  **InputInjector**: Uses XTest to simulate key presses matching the duration of user touch/click.

## Theme Configuration
JSON-like or INI format.
Example path: `~/.context-osk/themes/usr/local/bin/pcsx.theme`

```ini
[layout]
height=400
bg_color=#1a1a1a
# calculated or fixed

[button]
label=A
x=800
y=200
w=64
h=64
image=btn_a.png
keycode=38
type=normal # or toggle
```

## Step-by-Step Implementation

### Step 1: Boilerplate & Window
- Set up directory structure.
- Get a basic X11 window showing on the target (red box test).
- Ensure it stays on top and input passes through to app (except when clicking buttons? No, "click duration match" implies we capture click).
- *Correction*: Window must take pointer input but *not* keyboard focus.

### Step 2: Theme Parsing
- Implement a parser.
- Create default theme with assets.

### Step 3: Context Switching
- Implement `XSelectInput` on Root for property changes.
- Read `/proc/<pid>/exe`.
- Load new theme on change.

### Step 4: Input & Rendering
- Render buttons.
- Handle `ButtonPress` and `ButtonRelease`.
- Map to XTest key code.

## Verification
- Deploy to `trixie`.
- Launch random apps and verify theme switch.
- Verify typing in terminal works.
