# Task 10: Display Present Optimization

## Summary
Optimize the display present path by replacing per-scanline memcpy with bulk copy and reducing GDI overhead.

## Current State

**File:** `src/display_ddraw.c:268-357`

### Present Path
```c
// 1. Lock DirectDraw surface (BLOCKING)
hr = IDirectDrawSurface7_Lock(g_backbuf, NULL, &ddsd, DDLOCK_WAIT | DDLOCK_WRITEONLY, NULL);

// 2. Per-scanline memcpy loop
uint16_t *dst = (uint16_t*)ddsd.lpSurface;
int dst_pitch_pixels = ddsd.lPitch / 2;
int copy_width = (width < dst_pitch_pixels) ? width : dst_pitch_pixels;

for (int y = 0; y < height; y++) {
    memcpy(&dst[y * dst_pitch_pixels], &framebuffer[y * width], copy_width * 2);
}

// 3. Unlock
IDirectDrawSurface7_Unlock(g_backbuf, NULL);

// 4. GDI StretchBlt to window
StretchBlt(hdcWnd, 0, 0, client_w, client_h, hdcSurf, 0, 0, width, height, SRCCOPY);
```

### Problems

1. **600 memcpy calls per frame** at 800×600
   - Each memcpy has function call overhead
   - Memory prefetcher can't optimize across calls

2. **DDLOCK_WAIT blocks** if surface is busy
   - Synchronous wait on GPU/display

3. **GDI StretchBlt** for window resize
   - Slow software path
   - Called every frame even when sizes match

## Proposed Solutions

### 1. Single memcpy When Pitches Match

```c
if (dst_pitch_pixels == width) {
    // Pitches match - single bulk copy
    memcpy(dst, framebuffer, width * height * 2);
} else {
    // Pitches differ - per-scanline (current behavior)
    for (int y = 0; y < height; y++) {
        memcpy(&dst[y * dst_pitch_pixels], &framebuffer[y * width], copy_width * 2);
    }
}
```

**Impact:** Eliminates 599 function calls when pitches match (common case).

### 2. Request Matching Pitch from DirectDraw

When creating the DirectDraw surface, request a specific pitch:

```c
// When creating back buffer, try to get matching pitch
ddsd.dwFlags |= DDSD_PITCH;
ddsd.lPitch = width * 2;  // Request desired pitch
```

**Note:** DirectDraw may ignore this request depending on driver.

### 3. Skip StretchBlt When Sizes Match

```c
if (width == client_w && height == client_h) {
    // Sizes match - use faster Blt instead of StretchBlt
    BitBlt(hdcWnd, 0, 0, width, height, hdcSurf, 0, 0, SRCCOPY);
} else {
    // Need scaling
    SetStretchBltMode(hdcWnd, COLORONCOLOR);
    StretchBlt(hdcWnd, 0, 0, client_w, client_h, hdcSurf, 0, 0, width, height, SRCCOPY);
}
```

### 4. Avoid Lock/Unlock Overhead

Consider alternatives to the Lock/GetDC pattern:

```c
// Option A: Use IDirectDrawSurface7_BltFast from system memory surface
// Create a system memory surface for the framebuffer
// Blt from system memory to video memory (may be hardware accelerated)

// Option B: Use UpdateLayeredWindow for transparent/composited windows
// (Windows-specific, may not help on Wine)
```

### 5. Cache Surface Properties

Avoid recalculating every frame:

```c
// Cache at surface creation or first present
static int cached_pitch = -1;
static bool pitch_matches = false;

if (cached_pitch != dst_pitch_pixels) {
    cached_pitch = dst_pitch_pixels;
    pitch_matches = (dst_pitch_pixels == width);
}

// Use cached value
if (pitch_matches) {
    memcpy(dst, framebuffer, width * height * 2);
} else {
    // per-scanline path
}
```

### 6. Async Present (Advanced)

Decouple present from render completion:

```c
// Double-buffer the framebuffer
// Render to buffer A while presenting buffer B
// Swap on grBufferSwap

// Could combine with Task 06 (Async Rendering Pipeline)
```

## File to Modify

- `src/display_ddraw.c` - `display_present()` function (lines 268-357)

## Measurements Needed

Before optimizing, measure:
1. Time spent in `display_present()` per frame
2. Percentage of frame time in present vs render
3. Whether pitch typically matches or differs on target platform (Wine/DDraw)

## Risk Assessment
**Risk: LOW**
- Changes are localized to present path
- No effect on rendering correctness
- Easy to A/B test performance

## Testing Requirements
- [ ] Display output identical to before
- [ ] No tearing or artifacts
- [ ] Works with window resizing
- [ ] Performance improvement measurable
- [ ] Test on Wine/FEX target platform

## Expected Impact

| Optimization | Estimated Improvement |
|--------------|----------------------|
| Single memcpy (pitch match) | 1-2ms/frame |
| BitBlt vs StretchBlt (size match) | 0.5-1ms/frame |
| Skip redundant mode setting | Negligible |

At 60 FPS target, 16.6ms/frame budget. Saving 2ms = 12% of frame budget.

**Note:** Present overhead is post-rendering, so doesn't affect triangle throughput directly, but does affect total frame time and input latency.

## Dependencies
- Independent of rendering optimizations
- Could combine with `tasks/06_Async_Rendering_Pipeline.md` for async present

## Wine/FEX Considerations

On the target platform (ARM64 + Wine + FEX):
- DirectDraw is emulated by Wine
- GDI calls go through Wine's GDI implementation
- Actual display may use X11/Wayland underneath
- memcpy performance depends on FEX x86 emulation

The single-memcpy optimization should help regardless of platform, as it reduces the number of x86 function calls being emulated.

## Notes
- The `DDLOCK_WAIT` flag causes a blocking wait - this is correct for vsync'd rendering
- The pitch mismatch case exists for the "ZARDBLIZ" fix (line 326 comment)
- Consider if the framebuffer could be allocated with matching pitch from the start
