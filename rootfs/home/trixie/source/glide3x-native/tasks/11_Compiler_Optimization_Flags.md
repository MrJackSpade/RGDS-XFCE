# Task 11: Compiler Optimization Flags

## Summary
Experiment with compiler optimization flags to improve generated code quality without code changes.

## Current State

**File:** `Makefile:26`

```makefile
CFLAGS = -O2 -Wall -Wextra $(ARCH_CFLAGS)
```

For i686 (x86):
```makefile
ARCH_CFLAGS = -msse2
```

For aarch64:
```makefile
ARCH_CFLAGS =  # empty
```

## Flags to Try

### 1. -O3 (Aggressive Optimization)

```makefile
CFLAGS = -O3 -Wall -Wextra $(ARCH_CFLAGS)
```

**What it does:**
- Enables all `-O2` optimizations plus:
- More aggressive inlining
- Loop unrolling
- Vectorization (SIMD)
- Function cloning for specialization

**Pros:**
- Can significantly speed up tight loops
- Better use of CPU features

**Cons:**
- Larger code size (may hurt instruction cache)
- Can sometimes be slower than `-O2` for some workloads
- Longer compile times

**Risk:** Low - just a flag change, easy to revert

### 2. -ffast-math (Relaxed Floating Point)

```makefile
CFLAGS += -ffast-math
```

**What it does:**
- Allows reordering of floating-point operations
- Assumes no NaN/Inf values
- Enables reciprocal approximations
- Allows FMA (fused multiply-add) contractions

**Pros:**
- Can significantly speed up float-heavy code
- Enables SIMD for more float operations

**Cons:**
- May change rendering results slightly
- Can break code that relies on IEEE float behavior
- `fast_reciplog()` and gradient calculations may behave differently

**Risk:** Medium - need to verify no visual artifacts

**Sub-flags if full -ffast-math is too aggressive:**
```makefile
# Pick specific optimizations:
CFLAGS += -fno-math-errno        # Don't set errno for math functions
CFLAGS += -funsafe-math-optimizations  # Allow reordering
CFLAGS += -ffinite-math-only     # Assume no NaN/Inf
CFLAGS += -fno-trapping-math     # Assume no FP traps
```

### 3. -flto (Link-Time Optimization)

```makefile
CFLAGS += -flto
LDFLAGS += -flto
```

**What it does:**
- Defers optimization until link time
- Allows cross-module inlining
- Whole-program optimization

**Pros:**
- Can inline across .c files (e.g., inline `compute_gradients` into `grDrawTriangle`)
- Better dead code elimination
- More optimization opportunities

**Cons:**
- Much longer link times
- Debugging becomes harder
- May interact poorly with some linker features

**Risk:** Low-Medium - may need linker adjustments

### 4. -march=native / Target-Specific Flags

For builds targeting specific hardware:

```makefile
# If building ON the target machine:
CFLAGS += -march=native -mtune=native

# Or for specific targets:
CFLAGS += -march=i686 -mtune=generic  # Current default-ish
CFLAGS += -march=x86-64 -mtune=generic  # If 64-bit x86 ever used
```

**Note:** Since this is cross-compiled for Wine/FEX on ARM64, the x86 flags are what FEX will emulate. More advanced x86 features (AVX, etc.) may not emulate efficiently.

### 5. Profile-Guided Optimization (PGO)

**Step 1: Instrumented build**
```makefile
CFLAGS += -fprofile-generate
LDFLAGS += -fprofile-generate
```

**Step 2: Run on target to collect profiles**
```bash
# Run test suite or game on ARM64+Wine+FEX
# This generates .gcda profile files
```

**Step 3: Optimized rebuild**
```makefile
CFLAGS += -fprofile-use
LDFLAGS += -fprofile-use
```

**Complexity:** HIGH
- Requires running on target platform to collect profiles
- Cross-compilation makes this awkward
- Profile data may not transfer between machines

**Recommendation:** Defer PGO until other optimizations are exhausted.

## Testing Matrix

| Flag Combo | Build | Test | Benchmark |
|------------|-------|------|-----------|
| `-O2` (baseline) | ✓ | ✓ | Record baseline FPS |
| `-O3` | ✓ | ✓ | Compare to baseline |
| `-O3 -ffast-math` | ✓ | Visual check | Compare to baseline |
| `-O3 -flto` | ✓ | ✓ | Compare to baseline |
| `-O3 -ffast-math -flto` | ✓ | Visual check | Compare to baseline |

## Implementation

### Makefile Changes

```makefile
# Add optimization level selection
OPT_LEVEL ?= 2
CFLAGS = -O$(OPT_LEVEL) -Wall -Wextra $(ARCH_CFLAGS)

# Optional flags (enable via make FAST_MATH=1 LTO=1)
ifdef FAST_MATH
    CFLAGS += -ffast-math
endif

ifdef LTO
    CFLAGS += -flto
    LDFLAGS += -flto
endif
```

Usage:
```bash
make                           # Default -O2
make OPT_LEVEL=3               # Use -O3
make OPT_LEVEL=3 FAST_MATH=1   # -O3 with fast-math
make OPT_LEVEL=3 LTO=1         # -O3 with LTO
```

## Files to Modify

- `Makefile` - Add flag options

## Risk Assessment
**Risk: LOW to MEDIUM**
- `-O3` and `-flto`: Low risk, just optimization level
- `-ffast-math`: Medium risk, may affect rendering precision

## Testing Requirements
- [ ] Build succeeds with each flag combination
- [ ] No crashes or hangs
- [ ] Visual output matches baseline (especially with `-ffast-math`)
- [ ] Performance improvement measurable
- [ ] Test on target Wine/FEX platform

## Expected Impact

| Flag | Expected Speedup | Code Size |
|------|------------------|-----------|
| `-O3` | 5-15% | +10-20% |
| `-ffast-math` | 5-10% (float-heavy code) | Similar |
| `-flto` | 5-10% (cross-module inlining) | May decrease |
| Combined | 10-25% | Variable |

**Note:** Actual impact depends heavily on:
- How well FEX emulates the generated x86 code
- Whether hot loops benefit from specific optimizations
- Cache effects from code size changes

## Dependencies
- Independent of code changes
- Should be tested AFTER code optimizations to measure true impact
- Can be done in parallel with other tasks

## FEX Emulation Considerations

Since the DLL runs through FEX (x86 emulation on ARM64):
- Complex x86 instructions may not emulate faster
- Simpler instruction sequences may actually be better
- SIMD (SSE2) is already enabled; AVX probably won't help through emulation
- Branch prediction behavior differs between native and emulated

**Recommendation:** Benchmark on actual target platform, not just x86 native.

## Notes
- The `-msse2` flag is already present for x86 builds
- MinGW GCC may have different optimization characteristics than native GCC
- Consider testing with both GCC and Clang (if available in MinGW toolchain)
