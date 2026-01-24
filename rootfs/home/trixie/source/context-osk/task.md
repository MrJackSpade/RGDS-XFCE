# Task: Build Context-Aware Virtual Keyboard (context-osk)

## Phase 1: Foundation & Windowing
- [x] Create project structure (src, themes, makefile) <!-- id: 20 -->
- [x] Implement basic X11 window (borderless, no focus, bottom screen) <!-- id: 21 -->
- [x] Verify window properties on target (persistence, z-order) <!-- id: 22 -->

## Phase 2: Configuration & Theme Engine
- [x] Define Theme Configuration Format (JSON/INI) <!-- id: 23 -->
- [x] Implement Config Parser (loading button rects, images, keys) <!-- id: 24 -->
- [x] Implement "Default" theme fallback logic <!-- id: 25 -->

## Phase 3: Rendering & Input
- [x] Implement Rendering Engine (Cairo or XLib, loading images) <!-- id: 26 -->
- [x] Implement Hit Testing & Touch/Click handling <!-- id: 27 -->
- [x] Implement Key Injection (XTest or uinput) ensuring duration match <!-- id: 28 -->
- [x] Implement Toggle Key logic <!-- id: 29 -->

## Phase 4: Context Awareness
- [x] Implement Active Window Polling (X11 `_NET_ACTIVE_WINDOW`) <!-- id: 30 -->
- [x] Implement Process Executable Path resolution (`/proc/PID/exe`) <!-- id: 31 -->
- [x] Implement Dynamic Theme Switching logic <!-- id: 32 -->

## Phase 5: Polish & Integration
- [x] Optimize performance (redraw efficiency) <!-- id: 33 -->
- [x] Create installation script and systemd user service <!-- id: 34 -->
- [x] Final end-to-end verification <!-- id: 35 -->

## Phase 6: Default Theme Expansion (TKL Layout)
- [x] Plan grid system and unit sizes (34px unit, 640px width)
- [x] Map all TKL keys to grid coordinates and X11 keycodes
- [x] Generate and write `default.theme` content
- [x] Verify keypad layout on device

## Phase 7: Debugging & Refinement
- [x] Fix Theme Parser for TKL section syntax
- [x] Fix Rendering Loop (Click cause redraw)
- [x] Fix Text Alignment (Center align)
- [x] Fix Flickering (Double Buffering)
- [x] Update System Installation

## Phase 8: Advanced Input & Rendering
- [x] Implement Text Clipping (Cairo clip) <!-- id: 36 -->
- [x] Update Theme Generator support for `toggle` property <!-- id: 37 -->
- [x] Implement Sticky/Latch Modifier Key Logic <!-- id: 38 -->

# Task: Install Midori Browser
- [x] Download and install Midori .deb package <!-- id: 39 -->

# Task: Cleanup
- [x] Remove Epiphany browser <!-- id: 40 -->
