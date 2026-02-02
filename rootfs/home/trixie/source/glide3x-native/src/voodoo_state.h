/*
 * voodoo_state.h - Voodoo emulator state structures
 *
 * SPDX-License-Identifier: BSD-3-Clause AND GPL-2.0-or-later
 * Original Copyright: Aaron Giles (MAME), kekko, Bernhard Schelling, DOSBox Staging Team
 *
 * Simplified for standalone Glide3x software renderer
 */

#ifndef VOODOO_STATE_H
#define VOODOO_STATE_H

#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "voodoo_types.h"
#include "voodoo_defs.h"

/*************************************
 * Core type definitions
 *************************************/

/* RGBA union for color manipulation */
typedef struct {
#ifndef WORDS_BIGENDIAN
    uint8_t b, g, r, a;
#else
    uint8_t a, r, g, b;
#endif
} rgba_t;

/* Voodoo register union */
typedef union {
    int32_t  i;
    uint32_t u;
    float    f;
    rgba_t   rgb;
} voodoo_reg;

typedef voodoo_reg rgb_union;

/*************************************
 * Statistics block (64 bytes)
 *************************************/

typedef struct {
    int32_t pixels_in;
    int32_t pixels_out;
    int32_t chroma_fail;
    int32_t zfunc_fail;
    int32_t afunc_fail;
    int32_t filler[64 / 4 - 5];
} stats_block;

/*************************************
 * NCC table for texture decompression
 *************************************/

typedef struct {
    bool        dirty;
    voodoo_reg *reg;
    int32_t     ir[4], ig[4], ib[4];
    int32_t     qr[4], qg[4], qb[4];
    int32_t     y[16];
    rgb_t      *palette;
    rgb_t      *palettea;
    rgb_t       texel[256];
} ncc_table;

/*************************************
 * TMU (Texture Mapping Unit) state
 *************************************/

typedef struct {
    uint8_t    *ram;              /* texture RAM */
    uint32_t    mask;             /* address mask */
    voodoo_reg *reg;              /* TMU registers */
    bool        regdirty;         /* registers changed? */

    /* Texture address constants */
    #define TEXADDR_MASK  0x0fffff
    #define TEXADDR_SHIFT 3

    /* Texture iteration state */
    int64_t     starts, startt;   /* starting S,T (14.18) */
    int64_t     startw;           /* starting W (2.30) */
    int64_t     dsdx, dtdx;       /* delta S,T per X */
    int64_t     dwdx;             /* delta W per X */
    int64_t     dsdy, dtdy;       /* delta S,T per Y */
    int64_t     dwdy;             /* delta W per Y */

    /* LOD parameters */
    int32_t     lodmin, lodmax;
    int32_t     lodbias;
    uint32_t    lodmask;
    uint32_t    lodoffset[9];
    int32_t     lodbasetemp;
    int32_t     detailmax;
    int32_t     detailbias;
    uint8_t     detailscale;

    /* Texture size masks */
    uint32_t    wmask;
    uint32_t    hmask;

    uint8_t     bilinear_mask;

    /* NCC tables */
    ncc_table   ncc[2];

    /* Texture lookup */
    const rgb_t *lookup;
    const rgb_t *texel[16];

    /* Palettes */
    rgb_t       palette[256];
    rgb_t       palettea[256];
} tmu_state;

/*************************************
 * TMU shared state (lookup tables)
 *************************************/

typedef struct {
    rgb_t rgb332[256];        /* RGB 3-3-2 lookup */
    rgb_t alpha8[256];        /* alpha 8-bit lookup */
    rgb_t int8[256];          /* intensity 8-bit lookup */
    rgb_t ai44[256];          /* alpha/intensity 4-4 lookup */
    rgb_t rgb565[65536];      /* RGB 5-6-5 lookup */
    rgb_t argb1555[65536];    /* ARGB 1-5-5-5 lookup */
    rgb_t argb4444[65536];    /* ARGB 4-4-4-4 lookup */
} tmu_shared_state;

/*************************************
 * Setup vertex (for triangle setup)
 *************************************/

typedef struct {
    float x, y;               /* X, Y coordinates */
    float a, r, g, b;         /* A, R, G, B values */
    float z, wb;              /* Z and broadcast W */
    float w0, s0, t0;         /* W, S, T for TMU 0 */
    float w1, s1, t1;         /* W, S, T for TMU 1 */
} setup_vertex;

/*************************************
 * FBI (Frame Buffer Interface) state
 *************************************/

typedef struct {
    uint8_t    *ram;              /* frame buffer RAM */
    uint32_t    mask;             /* address mask */
    uint32_t    rgboffs[3];       /* offsets to RGB buffers */
    uint32_t    auxoffs;          /* offset to aux buffer */

    uint8_t     frontbuf;         /* front buffer index */
    uint8_t     backbuf;          /* back buffer index */

    uint32_t    yorigin;          /* Y origin subtract value */

    uint32_t    width;            /* frame buffer width */
    uint32_t    height;           /* frame buffer height */
    uint32_t    rowpixels;        /* pixels per row */
    uint32_t    tile_width;
    uint32_t    tile_height;
    uint32_t    x_tiles;

    uint8_t     vblank;
    bool        vblank_dont_swap;
    bool        vblank_flush_pending;

    /* Triangle setup state */
    int16_t     ax, ay;           /* vertex A (12.4) */
    int16_t     bx, by;           /* vertex B (12.4) */
    int16_t     cx, cy;           /* vertex C (12.4) */
    int32_t     startr, startg, startb, starta;  /* starting RGBA (12.12) */
    int32_t     startz;           /* starting Z (20.12) */
    int64_t     startw;           /* starting W (16.32) */
    int32_t     drdx, dgdx, dbdx, dadx;  /* delta RGBA per X */
    int32_t     dzdx;             /* delta Z per X */
    int64_t     dwdx;             /* delta W per X */
    int32_t     drdy, dgdy, dbdy, dady;  /* delta RGBA per Y */
    int32_t     dzdy;             /* delta Z per Y */
    int64_t     dwdy;             /* delta W per Y */

    stats_block lfb_stats;

    uint8_t     sverts;           /* setup vertex count */
    setup_vertex svert[3];        /* setup vertices */

    /* Fog tables */
    uint8_t     fogblend[64];
    uint8_t     fogdelta[64];
    uint8_t     fogdelta_mask;
} fbi_state;

/*************************************
 * DAC state (minimal)
 *************************************/

typedef struct {
    uint8_t reg[8];
    uint8_t read_result;
} dac_state;

/*************************************
 * Main Voodoo state
 *************************************/

typedef struct {
    uint8_t     chipmask;         /* available chips */

    voodoo_reg  reg[0x400];       /* raw registers */
    const uint8_t *regaccess;     /* register access flags */
    bool        alt_regmap;       /* alternate register map */

    dac_state   dac;
    fbi_state   fbi;
    tmu_state   tmu[MAX_TMU];
    tmu_shared_state tmushare;
    uint32_t    tmu_config;

    bool        send_config;
    bool        clock_enabled;
    bool        output_on;
    bool        active;

    /* For thread stats (simplified - single threaded for now) */
    stats_block thread_stats;

    /* Clip rectangle */
    int32_t     clip_left, clip_right;
    int32_t     clip_top, clip_bottom;

    /* Software renderer state (not in hardware regs) */
    int32_t     vp_x, vp_y;
    int32_t     vp_width, vp_height;
    int32_t     cull_mode;
    uint32_t    gamma_table[32];  /* Simple gamma table storage */
    
    /* Extra state for primitives */
    float       vertex_layout_offset; /* For grVertexLayout */
    
    /* Shadow state for shared registers */
    bool        alpha_mask;
    bool        depth_mask;
} voodoo_state;

/*************************************
 * Helper functions
 *************************************/

static inline int32_t clamp_to_uint16(int32_t val)
{
    if (val < 0) return 0;
    if (val > 0xffff) return 0xffff;
    return val;
}

static inline int32_t clamp_to_uint8(int32_t val)
{
    if (val < 0) return 0;
    if (val > 0xff) return 0xff;
    return val;
}

/*************************************
 * Function prototypes
 *************************************/

/* Initialization */
voodoo_state* voodoo_create(void);
void voodoo_destroy(voodoo_state *v);
void voodoo_init_fbi(fbi_state *f, int fbmem);
void voodoo_init_tmu(tmu_state *t, voodoo_reg *reg, int tmumem);
void voodoo_init_tmu_shared(tmu_shared_state *s);

/* Rendering */
void voodoo_triangle(voodoo_state *v);
void voodoo_fastfill(voodoo_state *v);
void voodoo_swapbuffer(voodoo_state *v);

/* Register access */
uint32_t voodoo_reg_read(voodoo_state *v, uint32_t offset);
void voodoo_reg_write(voodoo_state *v, uint32_t offset, uint32_t data);

/* LFB access */
uint32_t voodoo_lfb_read(voodoo_state *v, uint32_t offset);
void voodoo_lfb_write(voodoo_state *v, uint32_t offset, uint32_t data, uint32_t mem_mask);

/* Texture */
void voodoo_tex_write(voodoo_state *v, int tmu, uint32_t offset, uint32_t data);

#endif /* VOODOO_STATE_H */
