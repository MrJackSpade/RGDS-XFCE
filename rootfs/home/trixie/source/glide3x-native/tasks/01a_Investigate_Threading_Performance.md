# Task 01a: Investigate Threading Performance

## Summary
Threading was enabled but only provided ~50% FPS increase instead of expected 2-3x speedup. Investigate the bottleneck.

## Background
Task 01 enabled the threading infrastructure with `cores - 1` worker threads. However, real-world testing showed only ~50% performance improvement, suggesting a bottleneck elsewhere.

## Possible Causes to Investigate

### 1. Lock Contention
- Check if mutex/condition variable usage creates serialization points
- Look for unnecessary locking in hot paths
- **Files:** `src/voodoo_emu.c` (triangle_worker functions)

### 2. Work Distribution Imbalance
- Small triangles bypass threading (< 200 pixels threshold at line ~1759)
- Work unit granularity may be too coarse or too fine
- **Check:** `num_work_units = (num_threads + 1) * 4`

### 3. Memory Bandwidth Saturation
- All threads write to same framebuffer (`tworker->drawbuf`)
- Cache line bouncing between cores
- Consider if ARM SBC memory bandwidth is the limit

### 4. Amdahl's Law - Serial Sections
- Triangle setup is single-threaded (slope calculations before dispatch)
- State updates between triangles are serial
- Texture fetches may serialize on shared TMU state

### 5. False Sharing
- Check if `triangle_worker` struct has cache line alignment issues
- `atomic_int work_index` and `atomic_int done_count` may be on same cache line

### 6. Thread Spin-up Overhead
- Threads are created on first triangle > 200 pixels
- Condition variable wake-up latency

## Investigation Steps

1. [ ] Profile with `perf` to identify hotspots
2. [ ] Measure time spent in triangle_worker_run() vs actual rasterization
3. [ ] Count how many triangles bypass threading (< 200 pixels)
4. [ ] Check cache miss rates with `perf stat`
5. [ ] Test with different `GLIDE3X_THREADS` values (1, 2, 3, 4)
6. [ ] Add timing instrumentation to measure:
   - Work distribution overhead
   - Thread synchronization overhead
   - Actual pixel processing time

## Metrics to Collect
- FPS with 0, 1, 2, 3, 4 threads
- Average triangle size (pixels)
- Percentage of triangles using threading vs single-threaded path
- CPU utilization per core during rendering

## Expected Findings
One of:
- Memory bandwidth limited (all cores waiting on RAM)
- Too much serial work (Amdahl's Law)
- Synchronization overhead dominates for small triangles
- Work distribution is uneven

## Dependencies
- Task 01: Enable Threading (completed)

## Notes
- 50% improvement is still valuable, but understanding the limit helps prioritize other optimizations
- If memory-bound, texture pre-conversion (Task 02) may help more than additional threading work
