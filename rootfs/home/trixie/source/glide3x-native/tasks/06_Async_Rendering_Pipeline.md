# Task 06: Async Rendering Pipeline

## Summary
Decouple the game thread from the render thread by making draw calls non-blocking. The game thread queues commands while the render thread processes them in parallel.

## Current State (Synchronous)

### Draw Call Flow
**File:** `src/glide3x_draw.c:261-409`

```
Game Thread:
  grDrawTriangle(a, b, c)
    -> setup triangle data in fbi_state
    -> voodoo_triangle(g_voodoo)      // BLOCKS until rasterization complete
       -> raster_generic()
          -> pixel loop (millions of iterations)
    <- returns
  grDrawTriangle(...)                  // Next triangle waits
  ...
  grBufferSwap()
    -> display_present()
```

**Problem:** The game cannot prepare the next triangle while the current one is being rasterized. With thousands of triangles per frame, this serialization is a major bottleneck.

### Key Functions

| Function | File | Current Behavior |
|----------|------|-----------------|
| `grDrawTriangle()` | glide3x_draw.c:261 | Blocks until triangle is fully rasterized |
| `voodoo_triangle()` | voodoo_emu.c | Synchronous rasterization |
| `grBufferSwap()` | glide3x_buffer.c:177 | Presents buffer, may wait for vsync |

## Proposed Architecture

### Command Buffer Approach

```
Game Thread:                          Render Thread:
  grDrawTriangle(a, b, c)
    -> copy triangle to cmd buffer
    <- return immediately              -> dequeue triangle
                                       -> voodoo_triangle()
  grDrawTriangle(...)                     (rasterizing in parallel)
    -> copy to cmd buffer
    <- return immediately
  ...
  grBufferSwap()
    -> signal "end of frame"
    -> wait for render thread          -> finish all queued triangles
       to complete frame               -> signal "frame done"
    <- present buffer
```

### Data Structures

```c
// Command types
typedef enum {
    CMD_DRAW_TRIANGLE,
    CMD_DRAW_LINE,
    CMD_DRAW_POINT,
    CMD_BUFFER_SWAP,
    CMD_STATE_CHANGE,
    // ... other commands
} cmd_type_t;

// Triangle command (most common)
typedef struct {
    cmd_type_t type;
    UnpackedVertex v[3];
    // Snapshot of relevant state at submission time
    uint32_t fbzColorPath;
    uint32_t alphaMode;
    uint32_t fbzMode;
    // ... other state needed for rasterization
} cmd_triangle_t;

// Command buffer (ring buffer)
typedef struct {
    uint8_t *data;
    size_t capacity;
    atomic_size_t write_pos;  // Game thread writes here
    atomic_size_t read_pos;   // Render thread reads here
    pthread_mutex_t mutex;
    pthread_cond_t not_empty;
    pthread_cond_t not_full;
} cmd_buffer_t;
```

### Thread Communication

```c
// In voodoo_state or new async_state struct:
typedef struct {
    cmd_buffer_t cmd_buffer;
    pthread_t render_thread;
    atomic_bool running;
    atomic_bool frame_complete;
    pthread_mutex_t frame_mutex;
    pthread_cond_t frame_done_cond;
} async_render_state;
```

## Implementation Steps

### Step 1: Create Command Buffer Infrastructure
- Ring buffer for commands
- Lock-free or low-contention enqueue/dequeue
- Handle buffer full condition (block or grow)

### Step 2: Modify Draw Calls to Queue Commands
**File:** `src/glide3x_draw.c`

```c
void __stdcall grDrawTriangle(const GrVertex *a, const GrVertex *b, const GrVertex *c)
{
    // ... existing validation ...

    // Instead of:
    //   voodoo_triangle(g_voodoo);

    // Do:
    cmd_triangle_t cmd;
    cmd.type = CMD_DRAW_TRIANGLE;
    // Copy vertex data
    // Snapshot current render state
    enqueue_command(&g_voodoo->async.cmd_buffer, &cmd);
}
```

### Step 3: Create Render Thread
**File:** `src/voodoo_emu.c` (or new file)

```c
void *render_thread_func(void *arg) {
    voodoo_state *v = (voodoo_state *)arg;

    while (v->async.running) {
        cmd_t *cmd = dequeue_command(&v->async.cmd_buffer);

        switch (cmd->type) {
            case CMD_DRAW_TRIANGLE:
                // Restore state from command
                // Call voodoo_triangle()
                break;
            case CMD_BUFFER_SWAP:
                // Signal frame complete
                break;
            // ...
        }
    }
    return NULL;
}
```

### Step 4: Modify grBufferSwap for Synchronization
**File:** `src/glide3x_buffer.c`

```c
void __stdcall grBufferSwap(FxU32 swap_interval)
{
    // Queue end-of-frame marker
    cmd_t cmd = { .type = CMD_BUFFER_SWAP };
    enqueue_command(&g_voodoo->async.cmd_buffer, &cmd);

    // Wait for render thread to finish the frame
    pthread_mutex_lock(&g_voodoo->async.frame_mutex);
    while (!g_voodoo->async.frame_complete) {
        pthread_cond_wait(&g_voodoo->async.frame_done_cond,
                          &g_voodoo->async.frame_mutex);
    }
    g_voodoo->async.frame_complete = false;
    pthread_mutex_unlock(&g_voodoo->async.frame_mutex);

    // Now present
    display_present(...);
}
```

## State Snapshotting Challenge

The biggest complexity is handling **state changes** between draw calls:

```c
grAlphaBlendFunction(SRC_ALPHA, ONE_MINUS_SRC_ALPHA, ...);
grDrawTriangle(a, b, c);  // Uses alpha blend
grAlphaBlendFunction(ONE, ONE, ...);
grDrawTriangle(d, e, f);  // Uses additive blend
```

**Options:**

1. **Snapshot all state per-triangle** (simple but memory-heavy)
   - Each command includes full render state
   - ~100-200 bytes per triangle

2. **State change commands** (complex but efficient)
   - Queue state changes as separate commands
   - Render thread maintains state
   - Must handle state at frame boundaries

3. **State versioning** (middle ground)
   - Maintain state snapshots indexed by version
   - Commands reference state version
   - GC old versions when render thread catches up

## Files to Modify

| File | Changes |
|------|---------|
| `src/voodoo_state.h` | Add `async_render_state` struct |
| `src/voodoo_emu.c` | Add render thread, modify `voodoo_triangle()` |
| `src/glide3x_draw.c` | Modify all draw functions to queue commands |
| `src/glide3x_buffer.c` | Modify `grBufferSwap()` for sync |
| `src/glide3x_init.c` | Initialize async state, start render thread |
| `src/glide3x_state.c` | Queue state changes (blend, depth, fog, etc.) |
| `src/glide3x_texture.c` | Handle texture uploads (may need sync point) |

## Synchronization Points

Some operations **must** block:

| Operation | Reason |
|-----------|--------|
| `grBufferSwap()` | Need frame complete before present |
| `grLfbReadRegion()` | Reading from framebuffer being written |
| `grLfbLock()` (write) | Direct framebuffer access |
| Texture upload to in-use texture | Data race |
| `grSstIdle()` | Explicit wait for GPU idle |

## Risk Assessment
**Risk: HIGH**
- Major architectural change
- Race conditions and deadlocks possible
- State management complexity
- Memory overhead for command buffer
- Debugging async issues is difficult

## Testing Requirements
- [ ] Single-threaded fallback still works
- [ ] No visual corruption from race conditions
- [ ] No deadlocks under various conditions
- [ ] grLfbReadRegion works correctly (sync point)
- [ ] State changes apply correctly across async boundary
- [ ] Performance improvement measurable
- [ ] Memory usage acceptable

## Expected Impact
- Game thread can prepare triangles while render thread draws
- Potentially 1.5-2x throughput improvement
- Better CPU utilization on multi-core systems
- May reduce perceived stutter

## Dependencies
- Should be done **after** `tasks/01_Enable_Threading.md` (simpler parallelization first)
- Should be done **after** algorithmic optimizations (Tasks 02-05)
- Independent of `tasks/07_Tile_Based_Rendering.md` (but could combine)

## Alternative: Double-Buffered Command Queues

Instead of a single ring buffer, use two command queues:

```
Frame N:   Game -> Queue A     Render <- Queue B (frame N-1)
Frame N+1: Game -> Queue B     Render <- Queue A (frame N)
```

**Pro:** No contention between game and render threads
**Con:** One frame of latency added

## Sync Functions to Investigate

**File:** `src/glide3x_misc.c`

The following functions are currently no-ops because the renderer is synchronous. With an async pipeline, they may need real implementations:

| Function | Current | Async Behavior Needed |
|----------|---------|----------------------|
| `grFinish()` (line 385) | No-op | Wait for render thread to complete all queued commands |
| `grFlush()` (line 389) | No-op | Ensure commands are submitted (may not wait for completion) |
| `grSstIdle()` (line 410) | No-op | Wait for render thread to become idle |
| `grSstStatus()` (line 426) | Returns 0 (idle) | Return actual busy/idle status based on command queue |
| `grBufferNumPending()` (line 441) | Returns 0 | Return count of pending buffer swaps in queue |

### Investigation Questions
- Which games actually call these functions?
- Do any games use `grSstStatus()` to poll for completion?
- Does `grBufferNumPending()` affect frame pacing logic in games?
- Should `grFlush()` be a full sync or just "submit pending"?

### Code References
```c
// Current implementations (all no-ops):
void __stdcall grFinish(void) { }
void __stdcall grFlush(void) { }
void __stdcall grSstIdle(void) { /* Software renderer is always idle */ }
FxU32 __stdcall grSstStatus(void) { return 0; /* Always idle */ }
FxI32 __stdcall grBufferNumPending(void) { return 0; /* No pending swaps */ }
```

## Notes
- The existing `triangle_worker` threading (Task 01) parallelizes within a single triangle
- This task parallelizes across triangles (game vs render thread)
- These are complementary - both can be enabled together
- Start with simple version (full state snapshot), optimize later if needed
