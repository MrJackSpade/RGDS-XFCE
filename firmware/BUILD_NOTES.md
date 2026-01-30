# RK3568 (Cortex-A55) Build Notes

## Current Configuration
- **Optimization:** `CONFIG_CC_OPTIMIZE_FOR_SIZE=y` (smaller binary, saves RAM)
- **Target:** ARM64 / RK3568 (Anbernic RG-DS)

## Performance Tuning Options (for later)

### Option 1: Switch to Performance Optimization
```
# In .config, change:
CONFIG_CC_OPTIMIZE_FOR_SIZE=n
CONFIG_CC_OPTIMIZE_FOR_PERFORMANCE=y
```

### Option 2: Add Cortex-A55 Specific Tuning
Add to build command:
```bash
make ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- \
    KCFLAGS="-mcpu=cortex-a55 -mtune=cortex-a55" -j8
```

Or modify `arch/arm64/Makefile` to add these flags permanently.

### Option 3: LTO (Link Time Optimization)
```
CONFIG_LTO_CLANG_THIN=y  # Requires clang cross-compiler
```

## Trade-offs
- **Size optimization:** Smaller kernel, lower memory usage, slightly slower
- **Performance optimization:** Larger kernel, faster code paths, more cache pressure
- **CPU tuning:** Better instruction scheduling for Cortex-A55, marginal gains

## Build Command
```bash
make ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- -j8
```
