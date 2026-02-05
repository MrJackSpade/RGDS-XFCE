# Task 13: Data Structure Cache Optimization

## Summary
Reorganize data structures to improve cache locality by separating hot (per-pixel) fields from cold (per-triangle/per-frame) fields.

## Current State

**File:** `src/voodoo_state.h`

### Structure Size Analysis

| Structure | Approx Size | Issue |
|-----------|-------------|-------|
| `tmu_shared_state` | ~768KB | Contains 3 × 65536-entry lookup tables (256KB each). Far exceeds L1 (32KB) and L2 (256-512KB) cache. |
| `tmu_state` | ~500+ bytes | Mixes hot (per-pixel) and cold (per-triangle) fields. |
| `fbi_state` | ~250 bytes | Similar hot/cold mixing. |
| `voodoo_state` | ~780KB+ | Embeds `tmushare` (768KB lookup tables). Single access to any field may evict useful cache lines. |
| `stats_block` | 64 bytes | Padded to cache line - good for multi-threading. ✓ |

### tmu_state Field Analysis (lines 76-120)

**Hot fields** (accessed per-pixel during texture sampling):
```c
uint8_t    *ram;              /* texture RAM pointer */
const rgb_t *lookup;          /* current texture lookup table */
const rgb_t *texel[16];       /* texel format lookup pointers */
uint32_t    wmask, hmask;     /* texture size masks */
uint8_t     bilinear_mask;    /* bilinear enable flag */
```

**Cold fields** (set per-triangle, not accessed per-pixel):
```c
int64_t     starts, startt;   /* starting S,T (set once per scanline) */
int64_t     startw;           /* starting W */
int64_t     dsdx, dtdx, dwdx; /* deltas per X */
int64_t     dsdy, dtdy, dwdy; /* deltas per Y */
int32_t     lodmin, lodmax, lodbias;
uint32_t    lodmask, lodoffset[9];
int32_t     detailmax, detailbias;
uint8_t     detailscale;
ncc_table   ncc[2];           /* ~1KB each */
rgb_t       palette[256];     /* 1KB */
rgb_t       palettea[256];    /* 1KB */
```

**Problem:** When the pixel loop accesses `ram` or `wmask`, it may pull in cache lines containing `starts`, `dsdx`, etc. that won't be used until next scanline setup.

### fbi_state Field Analysis (lines 152-197)

**Hot fields** (accessed per-pixel):
```c
uint8_t    *ram;              /* framebuffer RAM pointer */
uint32_t    mask;             /* address mask */
uint32_t    yorigin;          /* Y origin subtract */
uint32_t    rowpixels;        /* pixels per row */
```

**Cold fields** (set per-triangle):
```c
int16_t     ax, ay, bx, by, cx, cy;  /* vertices */
int32_t     startr, startg, startb, starta, startz;
int64_t     startw;
int32_t     drdx, dgdx, dbdx, dadx, dzdx;
int64_t     dwdx;
int32_t     drdy, dgdy, dbdy, dady, dzdy;
int64_t     dwdy;
setup_vertex svert[3];        /* ~72 bytes each = 216 bytes */
uint8_t     fogblend[64], fogdelta[64];
```

### tmu_shared_state Analysis (lines 126-134)

```c
typedef struct {
    rgb_t rgb332[256];        /*   1KB - 8-bit formats */
    rgb_t alpha8[256];        /*   1KB */
    rgb_t int8[256];          /*   1KB */
    rgb_t ai44[256];          /*   1KB */
    rgb_t rgb565[65536];      /* 256KB - 16-bit formats */
    rgb_t argb1555[65536];    /* 256KB */
    rgb_t argb4444[65536];    /* 256KB */
} tmu_shared_state;           /* Total: ~768KB */
```

**Problem:** These lookup tables are used for per-pixel texture format conversion. Random access patterns (texture coordinates) cause constant cache misses. Each bilinear-filtered pixel does 4 lookups.

**Note:** This is addressed by Task 02 (Pre-convert Textures) - eliminating these lookups entirely.

## Proposed Solutions

### Solution 1: Split Hot/Cold Fields

Separate frequently-accessed fields into their own structures:

```c
/* Hot TMU state - fits in 1-2 cache lines */
typedef struct {
    uint8_t    *ram;              /* 8 bytes */
    const rgb_t *lookup;          /* 8 bytes */
    uint32_t    wmask, hmask;     /* 8 bytes */
    uint8_t     bilinear_mask;    /* 1 byte */
    uint8_t     pad[7];           /* alignment */
    /* Total: 32 bytes = 1 cache line */
} tmu_hot_state;

/* Cold TMU state - accessed per-triangle */
typedef struct {
    int64_t     starts, startt, startw;
    int64_t     dsdx, dtdx, dwdx;
    int64_t     dsdy, dtdy, dwdy;
    /* ... LOD params, NCC tables, palettes ... */
} tmu_cold_state;

/* Hot FBI state */
typedef struct {
    uint8_t    *ram;
    uint32_t    mask;
    uint32_t    yorigin;
    uint32_t    rowpixels;
    /* Total: ~24 bytes */
} fbi_hot_state;
```

### Solution 2: Cache Line Alignment

Ensure hot fields are aligned to cache line boundaries:

```c
typedef struct {
    /* Cache line 0: Hot fields */
    uint8_t    *ram;
    const rgb_t *lookup;
    uint32_t    wmask, hmask;
    uint8_t     bilinear_mask;
    uint8_t     _pad0[7];

    /* Cache line 1+: Cold fields */
    __attribute__((aligned(64)))
    int64_t     starts, startt, startw;
    // ...
} tmu_state;
```

### Solution 3: Pointer Indirection for Hot State

Store hot state separately and pass pointer to pixel loop:

```c
/* In voodoo_state */
typedef struct {
    // ...
    tmu_hot_state tmu_hot[MAX_TMU];  /* Hot state grouped together */
    tmu_cold_state tmu_cold[MAX_TMU]; /* Cold state separate */
    // ...
} voodoo_state;

/* Pixel loop receives only hot state */
static inline void sample_texture(tmu_hot_state *hot, int s, int t) {
    // All needed data in 1-2 cache lines
}
```

### Solution 4: Remove Embedded Lookup Tables

Move `tmu_shared_state` out of `voodoo_state`:

```c
/* Before: tmushare embedded in voodoo_state */
typedef struct {
    // ...
    tmu_shared_state tmushare;  /* 768KB! */
    // ...
} voodoo_state;

/* After: tmushare allocated separately */
typedef struct {
    // ...
    tmu_shared_state *tmushare;  /* 8 byte pointer */
    // ...
} voodoo_state;
```

This doesn't fix cache misses in the lookup tables themselves, but prevents accessing unrelated `voodoo_state` fields from evicting other cached data.

**Note:** This is a partial fix. Task 02 (Pre-convert Textures) eliminates the lookup tables entirely, which is the real solution.

## Implementation Priority

| Change | Impact | Complexity | Priority |
|--------|--------|------------|----------|
| Split tmu_state hot/cold | Medium | Medium | 1 |
| Split fbi_state hot/cold | Low-Medium | Medium | 2 |
| Move tmushare to pointer | Low | Low | 3 |
| Cache line alignment | Low | Low | 4 |

**Note:** If Task 02 (Pre-convert Textures) is implemented, the lookup table issue disappears. Focus on hot/cold separation first.

## Files to Modify

| File | Changes |
|------|---------|
| `src/voodoo_state.h` | Restructure `tmu_state`, `fbi_state`, optionally `voodoo_state` |
| `src/voodoo_emu.c` | Update field access for new structure layout |
| `src/voodoo_pipeline.h` | Update TEXTURE_PIPELINE macro for new layout |
| `src/glide3x_*.c` | Update any direct state access |

## Risk Assessment
**Risk: MEDIUM**
- Significant code churn across multiple files
- Must update all field accesses
- Easy to miss an access and cause crash/corruption
- Performance improvement may be small on modern CPUs with large caches

## Testing Requirements
- [ ] All field accesses updated correctly
- [ ] No crashes or memory corruption
- [ ] Rendering output identical to before
- [ ] Performance improvement measurable (or at least no regression)
- [ ] Test on target ARM64 platform (smaller caches than x86)

## Expected Impact

| Platform | L1 Cache | L2 Cache | Expected Benefit |
|----------|----------|----------|------------------|
| RK3568 (target) | 32KB | 256KB | Medium - tight caches benefit from locality |
| Modern x86 | 32-48KB | 256-512KB | Low - larger caches mask poor locality |

Estimated improvement: 2-5% for hot/cold separation alone. Combined with Task 02 (eliminating lookup tables): 10-20%.

## Dependencies
- **Synergy with:** Task 02 (eliminates 768KB lookup tables)
- **Independent of:** Most other tasks
- **Consider after:** Tasks 01-03 (bigger impact, lower risk)

## Cache Line Size Reference

| Platform | L1 Line Size |
|----------|--------------|
| ARM Cortex-A55 (RK3568) | 64 bytes |
| x86-64 | 64 bytes |

Hot state should ideally fit in 1-2 cache lines (64-128 bytes).

## Current Field Layout (for reference)

### tmu_state byte offsets (approximate)
```
0x00: ram (8)
0x08: mask (4)
0x0C: reg (8)
0x14: regdirty (1)
0x18: starts (8)  <- cold starts here
0x20: startt (8)
0x28: startw (8)
...
```

### fbi_state byte offsets (approximate)
```
0x00: ram (8)
0x08: mask (4)
0x0C: rgboffs[3] (12)
0x18: auxoffs (4)
...
0x30+: ax, ay, ... <- cold starts here
```

## Notes
- The `stats_block` is already cache-line padded (64 bytes) for multi-threading - good practice to follow
- ARM64 may have different cache characteristics than x86 - profile on target
- Consider using `__builtin_prefetch()` for predictable access patterns
- Structure packing (`__attribute__((packed))`) should be avoided - alignment matters more than size for cache performance
