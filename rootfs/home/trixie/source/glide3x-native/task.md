# Software Glide3x for x86 Guest (Wine/Hangover)

## Project Goal

Create a **software-rendering Glide3x DLL** that runs entirely inside the x86 guest space under FEX emulation. This avoids the syscall overhead of Wine's graphics stack for simple 2D sprite games like Diablo 2.

**Source:** Extract and adapt the software Voodoo renderer from [DOSBox-Staging](https://github.com/dosbox-staging/dosbox-staging/blob/main/src/hardware/video/voodoo.cpp) (BSD-3-Clause + GPL-2.0-or-later licensed).

---

## Source Code Analysis (DOSBox-Staging voodoo.cpp)

### What We Have
- **~8000 lines** of battle-tested Voodoo 1/2 emulation
- **SIMDE for SIMD** - Uses `simde/x86/sse2.h` which translates SSE2 to ARM NEON automatically
- **Multi-threaded triangle rasterization** - Already optimized
- **BSD-3-Clause licensed** core (Aaron Giles' MAME code)

### Code Structure
```
voodoo.cpp contains:
├── Type definitions (rgb_t, poly_vertex, etc.)
├── voodoo_state - Main emulator state
├── fbi_state - Framebuffer Interface (our render target)
│   └── uint8_t* ram - Direct framebuffer memory (16-bit RGB565)
├── tmu_state - Texture Mapping Unit state
├── Pixel pipeline macros (PIXEL_PIPELINE_BEGIN/MODIFY/FINISH)
├── triangle() - Main triangle rasterization
├── draw_triangle() - High-level draw command
└── DOSBox integration (to be removed)
```

### Dependencies to Remove
| DOSBox Component | Purpose | Replacement |
|------------------|---------|-------------|
| `dosbox.h` | Core types | Our own types |
| `config/config.h` | Settings | Hardcoded for D2 |
| `cpu/paging.h` | PCI memory mapping | Not needed |
| `hardware/pci_bus.h` | PCI emulation | Not needed |
| `hardware/pic.h` | Interrupts | Not needed |
| `gui/render.h` | Display output | DirectDraw blit |

### Dependencies to Keep
| Component | Purpose |
|-----------|---------|
| `simde/x86/sse2.h` | Cross-platform SIMD (ARM compatible!) |
| Standard C++ | Threading, containers |
| SDL_cpuinfo.h | CPU feature detection (optional) |

---

## Rationale: Why Software Rendering May Be Faster

### Current Path (Wine Graphics Stack)
```
x86 Glide call (grDrawTriangle)
    │
    ▼ (FEX emulates i386 code)
i386-windows/glide3x.dll (or nglide wrapper)
    │
    ▼ (Wine syscall via Wow64Transition)
EXIT FEX EMULATION ← syscall overhead here
    │
    ▼
wow64.dll (32→64 bit argument marshalling)
    │
    ▼
aarch64-windows/ntdll.dll
    │
    ▼
aarch64-unix/*.so (Wine Unix layer)
    │
    ▼
dlopen(libEGL/libvulkan)
    │
    ▼
Native GPU driver
    │
    ▼
RETURN TO FEX ← syscall return overhead here
```

**Every draw call triggers this entire chain.** For a game doing thousands of sprite blits per frame, this overhead accumulates.

### Proposed Path (Software Rendering in Guest)
```
x86 Glide call (grDrawTriangle)
    │
    ▼ (FEX emulates i386 code)
i386-windows/glide3x.dll (OUR SOFTWARE RENDERER)
    │
    ▼ (all rendering in emulated x86 - no syscall)
Software rasterizer writes to framebuffer in memory
    │
    ▼ (only on buffer swap - once per frame)
Single syscall to blit framebuffer to screen
```

**Hypothesis:** For 2D sprite games, the cost of emulating software rendering math is less than the cost of thousands of syscall round-trips per frame.

---

## Why This Makes Sense for Diablo 2

1. **D2 is 2D sprites** - No complex 3D, just blitting pre-rendered art
2. **Glide API is simple** - Triangles with textures, that's basically it
3. **D2 removed software rendering in 1.14a** - So we can't just use that option
4. **640x480 resolution** - Tiny by modern standards, easy to software render
5. **Frame rate target is 25fps** - D2's internal tick rate, not demanding

---

## Architecture

### Component Overview
```
┌─────────────────────────────────────────────────────────┐
│                 x86 Guest Space (FEX)                   │
│                                                         │
│  ┌─────────────┐    ┌──────────────────────────────┐   │
│  │ Diablo 2    │───►│ glide3x.dll (our impl)       │   │
│  │ (x86 exe)   │    │                              │   │
│  └─────────────┘    │  ┌────────────────────────┐  │   │
│                     │  │ Software Rasterizer    │  │   │
│                     │  │ - Triangle filling     │  │   │
│                     │  │ - Texture sampling     │  │   │
│                     │  │ - Alpha blending       │  │   │
│                     │  └───────────┬────────────┘  │   │
│                     │              │               │   │
│                     │  ┌───────────▼────────────┐  │   │
│                     │  │ Framebuffer (640x480)  │  │   │
│                     │  │ in guest memory        │  │   │
│                     │  └───────────┬────────────┘  │   │
│                     └──────────────┼──────────────┘   │
│                                    │                   │
└────────────────────────────────────┼───────────────────┘
                                     │ (grBufferSwap only)
                                     ▼
┌────────────────────────────────────────────────────────┐
│              Native ARM64 (once per frame)             │
│                                                        │
│  Wine GDI BitBlt or DirectDraw primary surface flip   │
│                       │                                │
│                       ▼                                │
│              Display (DRM/KMS or X11)                  │
└────────────────────────────────────────────────────────┘
```

### Key Design Decisions

1. **Build as i386 Windows DLL** - Runs entirely under FEX emulation
2. **Software rasterizer in C** - Simple, portable, no GPU dependencies
3. **Minimize syscalls** - Only touch native code on buffer swap
4. **Target DirectDraw primary surface** - Use Wine's existing display path for final blit

---

## Implementation Plan

### Phase 1: Extract Core Renderer
Extract from DOSBox-Staging voodoo.cpp:
- [ ] Copy voodoo.cpp to workspace (DONE - `dosbox-staging/voodoo.cpp`)
- [ ] Create standalone header with types (rgb_t, poly_vertex, voodoo_reg, etc.)
- [ ] Extract `fbi_state`, `tmu_state`, `voodoo_state` structures
- [ ] Extract pixel pipeline macros (PIXEL_PIPELINE_*)
- [ ] Extract `triangle()` and supporting functions
- [ ] Remove DOSBox dependencies, add stubs where needed

### Phase 2: Add SIMDE
- [ ] Download SIMDE headers (single-header library)
- [ ] Verify SSE2→NEON translation works
- [ ] Test basic SIMD operations compile for i386-mingw

### Phase 3: Create Glide3x API Wrapper
Map Glide3x calls to voodoo_state operations:
```c
grGlideInit()     → init_fbi(), init_tmu(), init_tmu_shared()
grSstWinOpen()    → configure fbi.width/height, create DirectDraw surface
grDrawTriangle()  → triangle()
grTexDownload()   → copy to tmu.ram
grBufferSwap()    → blit fbi.ram to DirectDraw surface
```

### Phase 4: Display Output (Minimal Wine Interaction)
- [ ] Create DirectDraw primary surface on grSstWinOpen
- [ ] On grBufferSwap: Lock surface, memcpy fbi.ram, Unlock, Flip
- [ ] This is the ONLY syscall per frame

### Phase 5: Build & Test
- [ ] Cross-compile with i686-w64-mingw32-gcc
- [ ] Link against DirectDraw (ddraw.lib)
- [ ] Test with simple Glide test program
- [ ] Test with Diablo 2

### Phase 6: Optimization (if needed)
- Multi-threading already in DOSBox code
- SIMDE handles SIMD translation
- Profile to find actual bottlenecks

---

## File Structure

```
glide3x-native/
├── task.md                      # This file
├── dosbox-staging/
│   └── voodoo.cpp               # Original DOSBox-Staging source (reference)
├── src/
│   ├── glide3x.c                # DLL entry point, Glide API implementation
│   ├── glide3x.h                # Glide3x public API types
│   ├── glide3x.def              # DLL exports
│   ├── voodoo_types.h           # Extracted types from voodoo.cpp
│   ├── voodoo_state.h           # fbi_state, tmu_state, voodoo_state
│   ├── voodoo_raster.c          # Extracted triangle/pixel pipeline
│   ├── voodoo_texture.c         # Extracted texture handling
│   └── display_ddraw.c          # DirectDraw output (only syscall point)
├── simde/                       # SIMDE headers (SSE2→NEON translation)
│   └── x86/sse2.h
├── Makefile                     # Cross-compile for i386-windows
└── test/
    └── glide_test.c             # Basic functionality test
```

---

## Glide3x API Subset (D2 Required)

Based on what Diablo 2 actually calls:

### Initialization
| Function | Notes |
|----------|-------|
| `grGlideInit` | Set up internal state |
| `grGlideShutdown` | Cleanup |
| `grSstWinOpen` | Create rendering context, set up DirectDraw surface |
| `grSstWinClose` | Destroy context |

### Rendering
| Function | Notes |
|----------|-------|
| `grDrawTriangle` | Main primitive - D2 uses this extensively |
| `grDrawVertexArray` | Batch triangles |
| `grBufferClear` | Clear framebuffer |
| `grBufferSwap` | **Only syscall point** - blit to screen |

### Textures
| Function | Notes |
|----------|-------|
| `grTexSource` | Bind texture |
| `grTexDownloadMipMap` | Upload texture data |
| `grTexMinAddress` | Texture memory management |
| `grTexMaxAddress` | Texture memory management |

### State
| Function | Notes |
|----------|-------|
| `grColorCombine` | Color blending mode |
| `grAlphaCombine` | Alpha blending mode |
| `grAlphaBlendFunction` | Blend equation |
| `grDepthBufferMode` | Probably disabled for 2D |
| `grDepthMask` | Probably disabled for 2D |

### Misc
| Function | Notes |
|----------|-------|
| `grSstQueryHardware` | Return fake 3dfx hardware info |
| `grSstQueryBoards` | Return 1 board |
| `grGlideGetVersion` | Return version string |

---

## Build Environment

### Cross-compilation for i386-windows
```bash
# Using MinGW
i686-w64-mingw32-gcc -shared -o glide3x.dll src/*.c -Wl,--out-implib,libglide3x.a

# Or using Wine's winegcc
winegcc -m32 -shared -o glide3x.dll.so src/*.c
```

### Installation
```bash
# Copy to Wine prefix
cp glide3x.dll ~/.wine-hangover/drive_c/windows/syswow64/
# or to game directory
cp glide3x.dll /path/to/diablo2/
```

---

## Performance Considerations

### Why This Might Be Faster
1. **Syscall avoidance** - Hundreds of draw calls stay in guest
2. **No WoW64 marshalling** - Everything is 32-bit throughout
3. **No Wine translation** - Direct memory operations
4. **Predictable memory access** - Good for FEX's JIT

### Why This Might Be Slower
1. **FEX emulating math** - Software rendering is compute-heavy
2. **No SIMD** - Unless FEX handles SSE2 well
3. **Single-threaded** - Unless we add async rendering

### Benchmark Plan
1. Run D2 with nGlide (current Wine path) - measure FPS
2. Run D2 with our software glide3x.dll - measure FPS
3. Profile where time is spent (FEX emulation vs syscalls)

---

## Existing Software Glide Implementations

Research these for reference:
- [ ] **OpenGlide** - Open source Glide→OpenGL wrapper (has software fallback?)
- [ ] **dgVoodoo** - Has software rendering mode
- [ ] **Glidos** - Software Glide for DOS games
- [ ] **MAME's 3dfx emulation** - Cycle-accurate but overkill
- [ ] **Wine's built-in glide3x** - Check if it has software path

---

## Previous Approach (Abandoned)

Previously considered FEX thunking:
```
x86 app → FEX thunk → Native ARM64 Glide implementation
```

This was abandoned because:
1. **Hangover bundles FEX differently** - No standalone thunk infrastructure
2. **Wine already bridges to native graphics** - Would duplicate that work
3. **Still requires syscalls** - Thunks exit emulation too
4. **Complex build requirements** - Need full FEX source tree

The software rendering approach is simpler and may actually be faster for 2D games.

---

## Success Criteria

1. D2 launches and renders correctly with our glide3x.dll
2. Performance is equal to or better than nGlide path
3. No visual artifacts in normal gameplay
4. Stable over extended play sessions

---

## Next Steps

1. [ ] Search for existing software Glide implementations
2. [ ] Create stub DLL with all exports
3. [ ] Implement framebuffer + grBufferSwap (via DirectDraw)
4. [ ] Implement basic triangle rasterization
5. [ ] Test with D2
6. [ ] Profile and optimize
