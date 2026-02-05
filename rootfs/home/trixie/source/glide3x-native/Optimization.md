# Glide3x Software Renderer Optimization

## Context
- Target: ARM64 SBCs running Wine/FEX-Emu (initially RK3568)
- Goal: General-purpose Glide3x software renderer for classic games
- Codebase: Based on Glide 3.x SDK + DOSBox-Staging Voodoo hardware emulation

## Task Files

Detailed optimization tasks are in `tasks/`:

| Task | Priority | Description |
|------|----------|-------------|
| [01_Enable_Threading.md](tasks/01_Enable_Threading.md) | HIGH | Enable multi-threaded triangle rasterization |
| [02_Preconvert_Textures.md](tasks/02_Preconvert_Textures.md) | HIGH | Pre-convert textures on upload to eliminate per-pixel lookup |
| [03_Eliminate_PerPixel_Switches.md](tasks/03_Eliminate_PerPixel_Switches.md) | HIGH | Remove switch statements from pixel loop via specialization |
| [04_LOD_Perspective_Fast_Path.md](tasks/04_LOD_Perspective_Fast_Path.md) | MEDIUM | Optimize LOD calculation for constant-LOD cases |
| [05_Blend_Mode_Fast_Paths.md](tasks/05_Blend_Mode_Fast_Paths.md) | MEDIUM | Loop inversion for blend modes |
| [06_Async_Rendering_Pipeline.md](tasks/06_Async_Rendering_Pipeline.md) | LOW | Decouple game thread from render thread |
| [07_Triangle_Setup_Optimization.md](tasks/07_Triangle_Setup_Optimization.md) | MEDIUM | Eliminate redundant gradient calculations |
| [08_Tile_Based_Rendering.md](tasks/08_Tile_Based_Rendering.md) | LOW | Render in tiles for cache locality |
| [09_Early_Out_Optimizations.md](tasks/09_Early_Out_Optimizations.md) | MEDIUM | Add bounding box rejection and alpha==0 skip |
| [10_Display_Present_Optimization.md](tasks/10_Display_Present_Optimization.md) | LOW | Single memcpy when pitches match |
| [11_Compiler_Optimization_Flags.md](tasks/11_Compiler_Optimization_Flags.md) | LOW | -O3, -ffast-math, -flto flags |
| [12_Extend_Dirty_Flag_System.md](tasks/12_Extend_Dirty_Flag_System.md) | MEDIUM | Track state changes for caching |
| [13_Data_Structure_Cache_Optimization.md](tasks/13_Data_Structure_Cache_Optimization.md) | LOW | Hot/cold field separation |
| [14_Feature_Toggle_Fast_Mode.md](tasks/14_Feature_Toggle_Fast_Mode.md) | LOW | Optional quality settings |
| [15_Buffer_Clear_Optimization.md](tasks/15_Buffer_Clear_Optimization.md) | MEDIUM | Eliminate per-pixel multiplication in grBufferClear |
| [16_LFB_Shadow_Buffer_Allocation.md](tasks/16_LFB_Shadow_Buffer_Allocation.md) | LOW | Pre-allocate shadow buffer to avoid runtime malloc |
| [17_GetProcAddress_Optimization.md](tasks/17_GetProcAddress_Optimization.md) | VERY LOW | Replace linear search with binary/hash lookup |

## Verified Non-Issues

The following were audited and found to be acceptable:

| Area | Finding |
|------|---------|
| grBufferSwap | Simple buffer pointer swap + present call - already minimal |
| grTexDownloadMipMap | Just memcpy - appropriate for upload path |
| State functions | Register writes only, not hot path |
| Scanline clipping | Done per-scanline in raster_generic, not per-pixel |
| Vertex unpacking | read_vertex_from_layout uses direct pointer access, no unnecessary copies |
| grLfbWriteRegion | Per-pixel format conversion is unavoidable for format translation |

## Implementation Order

Recommended order based on impact vs complexity:

1. **Task 01** - Enable Threading (easy win, potentially 2-4x speedup)
2. **Task 03** - Eliminate Per-Pixel Switches (biggest algorithmic improvement)
3. **Task 02** - Pre-convert Textures (eliminates 768KB lookup table thrashing)
4. **Task 07** - Triangle Setup (reduces per-triangle overhead)
5. **Task 15** - Buffer Clear Optimization (quick win, low risk)
6. **Task 09** - Early-Out Optimizations (low risk, moderate impact)
7. **Task 11** - Compiler Flags (zero code changes, potential 10-25% improvement)
8. Remaining tasks based on profiling results
