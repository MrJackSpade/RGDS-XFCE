# Task 08: Tile-Based Rendering

## Summary
Render in tiles instead of scanlines to improve cache locality. Framebuffer and depth buffer access patterns become more cache-friendly when processing small rectangular regions.

## Current State

### Scanline-Based Rendering
**File:** `src/voodoo_emu.c`

The current renderer processes triangles scanline by scanline:

```
Triangle covering Y=100 to Y=200:
  Process scanline Y=100 (X=50 to X=150)
  Process scanline Y=101 (X=48 to X=152)
  Process scanline Y=102 (X=46 to X=154)
  ...
```

Each scanline writes to a different memory region of the framebuffer. For a 800x600 framebuffer at 16-bit:
- Scanline stride = 1600 bytes
- Processing 100 scanlines = 100 × 1600 = 160KB of framebuffer touched
- Far exceeds L1 cache (32KB) and potentially L2 (256KB)

### Current Work Distribution
**File:** `src/voodoo_emu.c:1557-1605`

Work units are divided by **pixel count**, not spatial locality:
```c
const int32_t from = tworker->totalpix * work_start / num_work_units;
const int32_t to = tworker->totalpix * work_end / num_work_units;
```

This distributes scanlines across threads but doesn't help cache locality.

## Problem

### Cache Thrashing
For bilinear-filtered texturing with blending:
- **Per pixel reads:** 4 texture samples + 1 depth read + 1 framebuffer read (for blending)
- **Per pixel writes:** 1 framebuffer write + 1 depth write

With scanline processing:
- Framebuffer: 800 pixels × 2 bytes = 1600 bytes per scanline
- Depth buffer: 800 pixels × 2 bytes = 1600 bytes per scanline
- After 20 scanlines: 64KB touched (exceeds L1)
- After 80 scanlines: 256KB touched (exceeds L2)

### Memory Access Pattern (Current)
```
Scanline 0:   [####################################] → cache miss
Scanline 1:   [####################################] → cache miss (scanline 0 evicted)
Scanline 2:   [####################################] → cache miss (scanline 1 evicted)
...
```

## Proposed Solution: Tile-Based Rendering

### Concept
Divide the framebuffer into small tiles (e.g., 32×32 or 64×64 pixels) and process each tile completely before moving to the next.

```
Instead of:           Do this:
  ████████████          ██░░░░░░░░░░
  ████████████          ██░░░░░░░░░░
  ████████████          ░░░░░░░░░░░░
  ░░░░░░░░░░░░          ░░░░░░░░░░░░
  (by scanline)         (by tile)
```

### Memory Access Pattern (Tiled)
```
Tile (0,0):   [##]     → fits in L1 cache
              [##]     → still in cache!

Tile (1,0):   [##]     → cache miss, but tile (0,0) work is done
              [##]
```

### Tile Size Selection

| Tile Size | Framebuffer | Depth | Total | Fits In |
|-----------|-------------|-------|-------|---------|
| 16×16 | 512 B | 512 B | 1 KB | L1 easily |
| 32×32 | 2 KB | 2 KB | 4 KB | L1 |
| 64×64 | 8 KB | 8 KB | 16 KB | L1 (half) |
| 128×128 | 32 KB | 32 KB | 64 KB | L2 |

**Recommendation:** 32×32 tiles (4KB total) for best L1 utilization.

## Implementation Approach

### Step 1: Tile Data Structure

```c
typedef struct {
    int x0, y0;      // Tile origin
    int x1, y1;      // Tile bounds (exclusive)
    uint16_t *fb;    // Pointer to framebuffer tile start
    uint16_t *depth; // Pointer to depth buffer tile start
} render_tile_t;
```

### Step 2: Bin Triangles to Tiles

Before rasterizing, determine which tiles each triangle overlaps:

```c
void bin_triangle_to_tiles(triangle_t *tri, tile_list_t *tiles) {
    // Compute triangle bounding box
    int min_x = min3(tri->v0.x, tri->v1.x, tri->v2.x);
    int max_x = max3(tri->v0.x, tri->v1.x, tri->v2.x);
    int min_y = min3(tri->v0.y, tri->v1.y, tri->v2.y);
    int max_y = max3(tri->v0.y, tri->v1.y, tri->v2.y);

    // Find overlapping tiles
    int tile_x0 = min_x / TILE_SIZE;
    int tile_x1 = max_x / TILE_SIZE;
    int tile_y0 = min_y / TILE_SIZE;
    int tile_y1 = max_y / TILE_SIZE;

    // Add triangle to each overlapping tile's list
    for (int ty = tile_y0; ty <= tile_y1; ty++) {
        for (int tx = tile_x0; tx <= tile_x1; tx++) {
            add_to_tile_list(&tiles[ty * tiles_x + tx], tri);
        }
    }
}
```

### Step 3: Render Tiles

```c
void render_frame_tiled(voodoo_state *v) {
    int tiles_x = (v->fbi.width + TILE_SIZE - 1) / TILE_SIZE;
    int tiles_y = (v->fbi.height + TILE_SIZE - 1) / TILE_SIZE;

    for (int ty = 0; ty < tiles_y; ty++) {
        for (int tx = 0; tx < tiles_x; tx++) {
            render_tile(v, tx, ty);
        }
    }
}

void render_tile(voodoo_state *v, int tx, int ty) {
    tile_list_t *tile = &v->tiles[ty * tiles_x + tx];

    // Process all triangles that touch this tile
    for (int i = 0; i < tile->count; i++) {
        triangle_t *tri = tile->triangles[i];
        // Rasterize triangle, clipped to tile bounds
        raster_triangle_clipped(v, tri,
            tx * TILE_SIZE, ty * TILE_SIZE,
            (tx + 1) * TILE_SIZE, (ty + 1) * TILE_SIZE);
    }
}
```

### Step 4: Parallel Tile Processing

Tiles are independent and can be processed in parallel:

```c
// Distribute tiles across threads
void render_frame_tiled_parallel(voodoo_state *v) {
    int total_tiles = tiles_x * tiles_y;

    #pragma omp parallel for  // Or use existing thread pool
    for (int t = 0; t < total_tiles; t++) {
        int tx = t % tiles_x;
        int ty = t / tiles_x;
        render_tile(v, tx, ty);
    }
}
```

**Note:** Unlike scanline parallelization, tile parallelization has no overlap—each tile's framebuffer/depth region is independent.

## Challenges

### 1. Triangle Spanning Multiple Tiles
A large triangle may span many tiles. Options:
- **Bin to all overlapping tiles:** Simple but triangle processed multiple times
- **Clip to tile bounds:** Each tile processes only its portion
- **Split triangles:** Complex, may create many small triangles

### 2. Depth Buffer Correctness
With per-tile processing, depth comparisons within a tile are correct, but triangles in different tiles may have different draw orders.

**Solution:** Process tiles in consistent order, or use per-tile depth sorting.

### 3. Blending Order
Alpha blending requires triangles to be processed in submission order.

**Solution:**
- Sort triangles within each tile by submission order
- Or: Only use tiling for opaque geometry, fall back to scanline for transparent

### 4. State Changes Mid-Frame
If render state changes between triangles, tiles must respect state boundaries.

**Solution:** Store state with each binned triangle, or flush tiles on state change.

### 5. Memory Overhead
Need to store triangle lists per tile.

**Estimate:** 800×600 with 32×32 tiles = 25×19 = 475 tiles. With 5000 triangles averaging 4 tiles each = 20,000 triangle references × 8 bytes = 160KB.

## Integration with Existing Threading

The current `triangle_worker` divides scanlines across threads. Options:

1. **Replace:** Use tile-based work distribution instead of scanline-based
2. **Hybrid:** Use scanlines within tiles, but process tiles in order
3. **Dual mode:** Scanline for small triangles, tiled for large scenes

## Files to Modify

| File | Changes |
|------|---------|
| `src/voodoo_state.h` | Add tile structures, tile lists |
| `src/voodoo_emu.c` | Add tile binning, tile rendering |
| `src/glide3x_draw.c` | Bin triangles on draw call |
| `src/glide3x_buffer.c` | Flush tiles on `grBufferSwap` |

## Risk Assessment
**Risk: HIGH**
- Major architectural change to rendering pipeline
- Complex interaction with state changes and blending
- Memory overhead for tile lists
- Need to handle edge cases (triangles spanning many tiles)

## Testing Requirements
- [ ] Visual output identical to scanline rendering
- [ ] Depth testing works correctly across tile boundaries
- [ ] Alpha blending order preserved
- [ ] Performance improvement measurable
- [ ] Memory usage acceptable
- [ ] Works with all triangle sizes (tiny to screen-filling)

## Expected Impact
- Framebuffer/depth buffer accesses stay in L1/L2 cache
- Reduced memory bandwidth
- Better parallelization (tiles are independent)
- Estimated: 10-20% speedup for cache-bound scenes

## Dependencies
- Could build on `tasks/01_Enable_Threading.md` for parallel tile processing
- Independent of `tasks/06_Async_Rendering_Pipeline.md` but could combine
- Should be done after simpler optimizations (Tasks 02-05, 07)

## Alternative: Deferred Rendering

Instead of immediate rasterization, defer all triangles to end of frame:

1. `grDrawTriangle` → store triangle in list
2. `grBufferSwap` → bin all triangles to tiles, render tiles

**Pro:** Single pass over triangles for binning
**Con:** Large memory for triangle storage, latency increase

## Notes
- Many modern GPUs use tile-based rendering (PowerVR, ARM Mali)
- DOSBox's approach is scanline-based, not tile-based
- Tile size should be tuned based on target cache sizes
- Consider dynamic tile sizing based on scene complexity
