/*
 * voodoo_types.h - Core types extracted from DOSBox-Staging voodoo.cpp
 *
 * SPDX-License-Identifier: BSD-3-Clause
 * Original Copyright: Aaron Giles (MAME), kekko, Bernhard Schelling
 *
 * Extracted for standalone Glide3x software renderer
 */

#ifndef VOODOO_TYPES_H
#define VOODOO_TYPES_H

#include <stdint.h>
#include <stdbool.h>

/***************************************************************************
    BASIC TYPE DEFINITIONS
***************************************************************************/

typedef int64_t attoseconds_t;

#define ATTOSECONDS_PER_SECOND_SQRT     ((attoseconds_t)1000000000)
#define ATTOSECONDS_PER_SECOND          (ATTOSECONDS_PER_SECOND_SQRT * ATTOSECONDS_PER_SECOND_SQRT)
#define ATTOSECONDS_TO_HZ(x)            ((double)ATTOSECONDS_PER_SECOND / (double)(x))
#define HZ_TO_ATTOSECONDS(x)            ((attoseconds_t)(ATTOSECONDS_PER_SECOND / (x)))

#define MAX_VERTEX_PARAMS               6

/* poly_extent describes start/end points for a scanline */
typedef struct {
    int32_t startx;  /* starting X coordinate (inclusive) */
    int32_t stopx;   /* ending X coordinate (exclusive) */
} poly_extent;

/* an rgb_t is a single combined R,G,B (and optionally alpha) value */
typedef uint32_t rgb_t;

/* an rgb15_t is a single combined 15-bit R,G,B value */
typedef uint16_t rgb15_t;

/* macros to assemble rgb_t values */
#define MAKE_ARGB(a,r,g,b)  ((((a) & 0xff) << 24) | (((r) & 0xff) << 16) | (((g) & 0xff) << 8) | ((b) & 0xff))
#define MAKE_RGB(r,g,b)     (MAKE_ARGB(255,r,g,b))

/* macros to extract components from rgb_t values */
#define RGB_ALPHA(rgb)      (((rgb) >> 24) & 0xff)
#define RGB_RED(rgb)        (((rgb) >> 16) & 0xff)
#define RGB_GREEN(rgb)      (((rgb) >> 8) & 0xff)
#define RGB_BLUE(rgb)       ((rgb) & 0xff)

/* common colors */
#define RGB_BLACK           (MAKE_ARGB(255,0,0,0))
#define RGB_WHITE           (MAKE_ARGB(255,255,255,255))

/* poly_vertex for triangle rendering */
typedef struct {
    float x;  /* X coordinate */
    float y;  /* Y coordinate */
} poly_vertex;

/***************************************************************************
    INLINE HELPER FUNCTIONS
***************************************************************************/

static inline uint8_t pal5bit(uint8_t bits)
{
    bits &= 0x1f;
    return (bits << 3) | (bits >> 2);
}

static inline int32_t mul_32x32_shift(int32_t a, int32_t b, int8_t shift)
{
    return (int32_t)(((int64_t)a * (int64_t)b) >> shift);
}

/***************************************************************************
    PIXEL EXTRACTION MACROS
***************************************************************************/

#define EXTRACT_565_TO_888(val, a, b, c)                    \
    (a) = (((val) >> 8) & 0xf8) | (((val) >> 13) & 0x07);   \
    (b) = (((val) >> 3) & 0xfc) | (((val) >> 9) & 0x03);    \
    (c) = (((val) << 3) & 0xf8) | (((val) >> 2) & 0x07);

#define EXTRACT_x555_TO_888(val, a, b, c)                    \
    (a) = (((val) >> 7) & 0xf8) | (((val) >> 12) & 0x07);    \
    (b) = (((val) >> 2) & 0xf8) | (((val) >> 7) & 0x07);     \
    (c) = (((val) << 3) & 0xf8) | (((val) >> 2) & 0x07);

#define EXTRACT_555x_TO_888(val, a, b, c)                    \
    (a) = (((val) >> 8) & 0xf8) | (((val) >> 13) & 0x07);    \
    (b) = (((val) >> 3) & 0xf8) | (((val) >> 8) & 0x07);     \
    (c) = (((val) << 2) & 0xf8) | (((val) >> 3) & 0x07);

#define EXTRACT_1555_TO_8888(val, a, b, c, d)                \
    (a) = ((int16_t)(val) >> 15) & 0xff;                     \
    EXTRACT_x555_TO_888(val, b, c, d)

#define EXTRACT_5551_TO_8888(val, a, b, c, d)                \
    EXTRACT_555x_TO_888(val, a, b, c)                        \
    (d) = ((val) & 0x0001) ? 0xff : 0x00;

#define EXTRACT_x888_TO_888(val, a, b, c)                    \
    (a) = ((val) >> 16) & 0xff;                              \
    (b) = ((val) >> 8) & 0xff;                               \
    (c) = ((val) >> 0) & 0xff;

#define EXTRACT_888x_TO_888(val, a, b, c)                    \
    (a) = ((val) >> 24) & 0xff;                              \
    (b) = ((val) >> 16) & 0xff;                              \
    (c) = ((val) >> 8) & 0xff;

#define EXTRACT_8888_TO_8888(val, a, b, c, d)                \
    (a) = ((val) >> 24) & 0xff;                              \
    (b) = ((val) >> 16) & 0xff;                              \
    (c) = ((val) >> 8) & 0xff;                               \
    (d) = ((val) >> 0) & 0xff;

#define EXTRACT_4444_TO_8888(val, a, b, c, d)                \
    (a) = (((val) >> 8) & 0xf0) | (((val) >> 12) & 0x0f);    \
    (b) = (((val) >> 4) & 0xf0) | (((val) >> 8) & 0x0f);     \
    (c) = (((val) >> 0) & 0xf0) | (((val) >> 4) & 0x0f);     \
    (d) = (((val) << 4) & 0xf0) | (((val) >> 0) & 0x0f);

#define EXTRACT_332_TO_888(val, a, b, c)                                                              \
    (a) = (((val) >> 0) & 0xe0) | (((val) >> 3) & 0x1c) | (((val) >> 6) & 0x03);                      \
    (b) = (((val) << 3) & 0xe0) | (((val) >> 0) & 0x1c) | (((val) >> 3) & 0x03);                      \
    (c) = (((val) << 6) & 0xc0) | (((val) << 4) & 0x30) | (((val) << 2) & 0xc0) | (((val) << 0) & 0x03);

/***************************************************************************
    ENDIAN HANDLING
***************************************************************************/

#ifndef WORDS_BIGENDIAN
#define NATIVE_ENDIAN_VALUE_LE_BE(leval,beval)  (leval)
#else
#define NATIVE_ENDIAN_VALUE_LE_BE(leval,beval)  (beval)
#endif

#define BYTE4_XOR_LE(a)     ((a) ^ NATIVE_ENDIAN_VALUE_LE_BE(0,3))
#define BYTE_XOR_LE(a)      ((a) ^ NATIVE_ENDIAN_VALUE_LE_BE(0,1))

/***************************************************************************
    DITHERING TABLES
***************************************************************************/

static const uint8_t dither_matrix_4x4[16] = {
    0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5
};

static const uint8_t dither_matrix_2x2[16] = {
    2, 10, 2, 10, 14, 6, 14, 6, 2, 10, 2, 10, 14, 6, 14, 6
};

#endif /* VOODOO_TYPES_H */
