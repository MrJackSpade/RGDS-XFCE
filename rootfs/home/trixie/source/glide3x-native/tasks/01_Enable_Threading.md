# Task 01: Enable Threading

## Summary
Enable the existing but disabled threading infrastructure in the Voodoo emulator.

## Current State
**File:** `src/voodoo_emu.c:349`
```c
const int num_threads = 0;
```

The threading code is completely disabled. At line 1716:
```c
if (!tworker->num_threads) {
    /* do not use threaded calculation */
    triangle_worker_work(tworker, 0, tworker->num_work_units);
    return;
}
```

All rendering is single-threaded despite parallel-capable code existing.

## Proposed Change
Change `num_threads` from `0` to a configurable value (e.g., `3` for a 4-core system, or `cpu_count - 1`).

## Implementation Details

### Option A: Hardcoded value
```c
const int num_threads = 3;  // Leave 1 core for game/Wine/FEX
```

### Option B: Environment variable (preferred)
```c
static int get_num_threads(void) {
    const char *env = getenv("GLIDE3X_THREADS");
    if (env) {
        int n = atoi(env);
        if (n >= 0 && n <= TRIANGLE_WORKER_MAX_THREADS)
            return n;
    }
    // Default: 3 threads (good for 4-core ARM SBCs)
    return 3;
}
```

### Option C: Runtime CPU detection
```c
#ifdef _WIN32
#include <windows.h>
static int get_num_threads(void) {
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    int cores = si.dwNumberOfProcessors;
    return (cores > 1) ? cores - 1 : 0;
}
#endif
```

## Files to Modify
- `src/voodoo_emu.c` - Change `num_threads` initialization

## Threading Infrastructure (Already Exists)
- `triangle_worker` struct in `voodoo_state.h:239-267`
- `triangle_worker_thread_func()` at `voodoo_emu.c:1679`
- `triangle_worker_run()` at `voodoo_emu.c:1713`
- `triangle_worker_shutdown()` at `voodoo_emu.c:1697`
- Work distribution via `atomic_int work_index`
- Synchronization via `pthread_mutex_t` and `pthread_cond_t`

## Risk Assessment
**Risk: LOW**
- Threading infrastructure already exists and was designed for this
- Code paths are already thread-safe (uses atomics, mutex)
- Easy to revert if issues arise

## Testing Requirements
- [ ] Basic rendering still works (run test_texture.exe)
- [ ] No visual corruption or artifacts
- [ ] No crashes or hangs
- [ ] Performance improvement measurable
- [ ] Test with different thread counts (0, 1, 2, 3, 4)

## Expected Impact
- **Immediate parallelization** of triangle rasterization across scanlines
- On 4-core RK3568: potentially 2-3x speedup for CPU-bound rendering
- Actual gain depends on triangle size and work distribution

## Dependencies
None - this is a standalone change.

## Notes
- `TRIANGLE_WORKER_MAX_THREADS` is defined as 8 in `voodoo_state.h:236`
- Stats are accumulated per-thread in `thread_stats[]` array
- Work is distributed by pixel count across scanlines for load balancing
