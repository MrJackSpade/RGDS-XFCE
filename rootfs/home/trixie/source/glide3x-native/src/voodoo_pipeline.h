/*
 * voodoo_pipeline.h - Pixel pipeline macros for Voodoo emulation
 *
 * SPDX-License-Identifier: BSD-3-Clause AND GPL-2.0-or-later
 *
 * Derived from DOSBox-Staging voodoo.cpp
 * Original Copyright: Aaron Giles (MAME), kekko, Bernhard Schelling, DOSBox Staging Team
 */

#ifndef VOODOO_PIPELINE_H
#define VOODOO_PIPELINE_H

#include <stdint.h>
#include <stdlib.h>
#include "voodoo_types.h"
#include "voodoo_defs.h"
#include "voodoo_state.h"

/*************************************
 * SIMD support via SIMDE
 *************************************/

#ifdef USE_SIMDE
#include "simde/x86/sse2.h"
#else
/* Fallback scalar implementation */
typedef struct { uint32_t u32[4]; } simde__m128i;
#endif

/*************************************
 * Helper functions
 *************************************/

/* Signed left shift that handles negative shifts (right shift) */
static inline int32_t left_shift_signed(int32_t value, int shift)
{
    if (shift >= 0)
        return value << shift;
    else
        return value >> (-shift);
}

/* Clamp int64 to int32 range */
static inline int32_t clamp_to_int32(int64_t value)
{
    if (value < INT32_MIN) return INT32_MIN;
    if (value > INT32_MAX) return INT32_MAX;
    return (int32_t)value;
}

/* Count leading zeros in a 32-bit value */
static inline int countl_zero_u32(uint32_t value)
{
    if (value == 0) return 32;
#if defined(__GNUC__) || defined(__clang__)
    return __builtin_clz(value);
#elif defined(_MSC_VER)
    unsigned long idx;
    _BitScanReverse(&idx, value);
    return 31 - idx;
#else
    int count = 0;
    while (!(value & 0x80000000)) {
        value <<= 1;
        count++;
    }
    return count;
#endif
}

/* mul_32x32_shift is defined in voodoo_types.h */

/* Clamp value to range */
static inline int32_t clamp_val(int32_t val, int32_t min_v, int32_t max_v)
{
    if (val < min_v) return min_v;
    if (val > max_v) return max_v;
    return val;
}

/*************************************
 * Reciprocal/log lookup table
 *************************************/

#define RECIPLOG_LOOKUP_BITS    9
#define RECIPLOG_INPUT_PREC     32
#define RECIPLOG_LOOKUP_PREC    22
#define RECIP_OUTPUT_PREC       15
#define LOG_OUTPUT_PREC         8

/* Long type macro for 64-bit constants */
#define LONGTYPE(x) ((int64_t)(x))

/* External reciplog table - initialized in voodoo_emu.c */
extern uint32_t voodoo_reciplog[(2 << RECIPLOG_LOOKUP_BITS) + 4];

/* Fast reciprocal with log2 computation */
static inline int64_t fast_reciplog(int64_t value, int32_t* log_2)
{
    uint32_t temp;
    uint32_t rlog;
    uint32_t interp;
    uint32_t *table;
    uint64_t recip;
    int neg = 0;
    int lz = 0;
    int exponent = 0;

    /* always work with unsigned numbers */
    if (value < 0) {
        value = -value;
        neg = 1;
    }

    /* if we've spilled out of 32 bits, push it down under 32 */
    if ((value & LONGTYPE(0xffff00000000)) != 0) {
        temp = (uint32_t)(value >> 16);
        exponent -= 16;
    } else {
        temp = (uint32_t)value;
    }

    /* if the resulting value is 0, the reciprocal is infinite */
    if (temp == 0) {
        *log_2 = 1000 << LOG_OUTPUT_PREC;
        return neg ? (int64_t)0x80000000 : (int64_t)0x7fffffff;
    }

    /* determine how many leading zeros in the value and shift it up high */
    lz = countl_zero_u32(temp);
    temp <<= lz;
    exponent += lz;

    /* compute a pointer to the table entries we want */
    table = &voodoo_reciplog[(temp >> (31 - RECIPLOG_LOOKUP_BITS - 1)) & ((2 << RECIPLOG_LOOKUP_BITS) - 2)];

    /* compute the interpolation value */
    interp = (temp >> (31 - RECIPLOG_LOOKUP_BITS - 8)) & 0xff;

    /* do a linear interpolation between the two nearest table values */
    rlog = (table[1] * (0x100 - interp) + table[3] * interp) >> 8;
    recip = (table[0] * (0x100 - interp) + table[2] * interp) >> 8;

    /* the log result is the fractional part of the log; round it to the output precision */
    rlog = (rlog + (1 << (RECIPLOG_LOOKUP_PREC - LOG_OUTPUT_PREC - 1))) >> (RECIPLOG_LOOKUP_PREC - LOG_OUTPUT_PREC);

    /* the exponent is the non-fractional part of the log */
    *log_2 = left_shift_signed(exponent - (31 - RECIPLOG_INPUT_PREC), LOG_OUTPUT_PREC) - rlog;

    /* adjust the exponent for final shift amount */
    exponent += (RECIP_OUTPUT_PREC - RECIPLOG_LOOKUP_PREC) - (31 - RECIPLOG_INPUT_PREC);

    /* shift by the exponent */
    if (exponent < 0) {
        recip >>= -exponent;
    } else {
        recip <<= exponent;
    }

    /* apply the original sign to the reciprocal */
    return neg ? -(int64_t)recip : (int64_t)recip;
}

/*************************************
 * Dithering tables defined in voodoo_types.h
 *************************************/

/*************************************
 * Bilinear filter (scalar fallback)
 *************************************/

static inline rgb_t rgba_bilinear_filter(rgb_t rgb00, rgb_t rgb01, rgb_t rgb10,
                                         rgb_t rgb11, uint8_t u, uint8_t v)
{
    /* Extract ARGB components */
    uint32_t a00 = (rgb00 >> 24) & 0xff, r00 = (rgb00 >> 16) & 0xff, g00 = (rgb00 >> 8) & 0xff, b00 = rgb00 & 0xff;
    uint32_t a01 = (rgb01 >> 24) & 0xff, r01 = (rgb01 >> 16) & 0xff, g01 = (rgb01 >> 8) & 0xff, b01 = rgb01 & 0xff;
    uint32_t a10 = (rgb10 >> 24) & 0xff, r10 = (rgb10 >> 16) & 0xff, g10 = (rgb10 >> 8) & 0xff, b10 = rgb10 & 0xff;
    uint32_t a11 = (rgb11 >> 24) & 0xff, r11 = (rgb11 >> 16) & 0xff, g11 = (rgb11 >> 8) & 0xff, b11 = rgb11 & 0xff;

    /* Bilinear interpolation */
    uint32_t inv_u = 256 - u;
    uint32_t inv_v = 256 - v;

    uint32_t a = ((a00 * inv_u + a01 * u) * inv_v + (a10 * inv_u + a11 * u) * v) >> 16;
    uint32_t r = ((r00 * inv_u + r01 * u) * inv_v + (r10 * inv_u + r11 * u) * v) >> 16;
    uint32_t g = ((g00 * inv_u + g01 * u) * inv_v + (g10 * inv_u + g11 * u) * v) >> 16;
    uint32_t b = ((b00 * inv_u + b01 * u) * inv_v + (b10 * inv_u + b11 * u) * v) >> 16;

    return MAKE_ARGB(a, r, g, b);
}

/*************************************
 * Statistics macro
 *************************************/

#define ADD_STAT_COUNT(STATS, STATNAME) (STATS).STATNAME++;

/*************************************
 * Dithering macro
 *************************************/

extern void trap_log(const char *fmt, ...);
#define APPLY_DITHER(FBZMODE, XX, DITHER_LOOKUP, RR, GG, BB)                \
do                                                                          \
{                                                                           \
    /* apply dithering */                                                   \
    if (FBZMODE_ENABLE_DITHERING(FBZMODE))                                  \
    {                                                                       \
        /* look up the dither value from the appropriate matrix */          \
        const uint8_t *dith = &(DITHER_LOOKUP)[((XX) & 3) << 1];            \
                                                                            \
        /* apply dithering to R,G,B */                                      \
        (RR) = dith[((RR) << 3) + 0];                                       \
        (GG) = dith[((GG) << 3) + 1];                                       \
        (BB) = dith[((BB) << 3) + 0];                                       \
    }                                                                       \
    else                                                                    \
    {                                                                       \
        (RR) >>= 3;                                                         \
        (GG) >>= 2;                                                         \
        (BB) >>= 3;                                                         \
    }                                                                       \
}                                                                           \
while (0)

/*************************************
 * Clamping macros
 *************************************/

#define CLAMPED_ARGB(ITERR, ITERG, ITERB, ITERA, FBZCP, RESULT)             \
do {                                                                        \
    int32_t red   = (ITERR) >> 12;                                          \
    int32_t green = (ITERG) >> 12;                                          \
    int32_t blue  = (ITERB) >> 12;                                          \
    int32_t alpha = (ITERA) >> 12;                                          \
                                                                            \
    if (FBZCP_RGBZW_CLAMP(FBZCP) == 0) {                                    \
        red &= 0xfff;                                                       \
        (RESULT).rgb.r = (uint8_t)red;                                      \
        if (red == 0xfff)                                                   \
            (RESULT).rgb.r = 0;                                             \
        else if (red == 0x100)                                              \
            (RESULT).rgb.r = 0xff;                                          \
                                                                            \
        green &= 0xfff;                                                     \
        (RESULT).rgb.g = (uint8_t)green;                                    \
        if (green == 0xfff)                                                 \
            (RESULT).rgb.g = 0;                                             \
        else if (green == 0x100)                                            \
            (RESULT).rgb.g = 0xff;                                          \
                                                                            \
        blue &= 0xfff;                                                      \
        (RESULT).rgb.b = (uint8_t)blue;                                     \
        if (blue == 0xfff)                                                  \
            (RESULT).rgb.b = 0;                                             \
        else if (blue == 0x100)                                             \
            (RESULT).rgb.b = 0xff;                                          \
                                                                            \
        alpha &= 0xfff;                                                     \
        (RESULT).rgb.a = (uint8_t)alpha;                                    \
        if (alpha == 0xfff)                                                 \
            (RESULT).rgb.a = 0;                                             \
        else if (alpha == 0x100)                                            \
            (RESULT).rgb.a = 0xff;                                          \
    } else {                                                                \
        (RESULT).rgb.r = (red < 0) ? 0 : (red > 0xff) ? 0xff : (uint8_t)red;\
        (RESULT).rgb.g = (green < 0) ? 0 : (green > 0xff) ? 0xff : (uint8_t)green;\
        (RESULT).rgb.b = (blue < 0) ? 0 : (blue > 0xff) ? 0xff : (uint8_t)blue;\
        (RESULT).rgb.a = (alpha < 0) ? 0 : (alpha > 0xff) ? 0xff : (uint8_t)alpha;\
    }                                                                       \
} while (0)

#define CLAMPED_Z(ITERZ, FBZCP, RESULT)                                     \
do                                                                          \
{                                                                           \
    (RESULT) = (ITERZ) >> 12;                                               \
    if (FBZCP_RGBZW_CLAMP(FBZCP) == 0)                                      \
    {                                                                       \
        (RESULT) &= 0xfffff;                                                \
        if ((RESULT) == 0xfffff)                                            \
            (RESULT) = 0;                                                   \
        else if ((RESULT) == 0x10000)                                       \
            (RESULT) = 0xffff;                                              \
        else                                                                \
            (RESULT) &= 0xffff;                                             \
    }                                                                       \
    else                                                                    \
    {                                                                       \
        (RESULT) = clamp_to_uint16(RESULT);                                 \
    }                                                                       \
}                                                                           \
while (0)

#define CLAMPED_W(ITERW, FBZCP, RESULT)                                     \
do                                                                          \
{                                                                           \
    (RESULT) = (int16_t)((ITERW) >> 32);                                    \
    if (FBZCP_RGBZW_CLAMP(FBZCP) == 0)                                      \
    {                                                                       \
        (RESULT) &= 0xffff;                                                 \
        if ((RESULT) == 0xffff)                                             \
            (RESULT) = 0;                                                   \
        else if ((RESULT) == 0x100)                                         \
            (RESULT) = 0xff;                                                \
        (RESULT) &= 0xff;                                                   \
    }                                                                       \
    else                                                                    \
    {                                                                       \
        (RESULT) = clamp_to_uint8(RESULT);                                  \
    }                                                                       \
}                                                                           \
while (0)

/*************************************
 * Chroma key macro
 *************************************/

#define APPLY_CHROMAKEY(VV, STATS, FBZMODE, COLOR)                          \
do                                                                          \
{                                                                           \
    if (FBZMODE_ENABLE_CHROMAKEY(FBZMODE))                                  \
    {                                                                       \
        /* non-range version */                                             \
        if (!CHROMARANGE_ENABLE((VV)->reg[chromaRange].u))                  \
        {                                                                   \
            if ((((COLOR).u ^ (VV)->reg[chromaKey].u) & 0xffffff) == 0)     \
            {                                                               \
                ADD_STAT_COUNT(STATS, chroma_fail)                          \
                goto skipdrawdepth;                                         \
            }                                                               \
        }                                                                   \
        /* tricky range version */                                          \
        else                                                                \
        {                                                                   \
            int32_t low, high, test;                                        \
            int results = 0;                                                \
                                                                            \
            /* check blue */                                                \
            low = (VV)->reg[chromaKey].rgb.b;                               \
            high = (VV)->reg[chromaRange].rgb.b;                            \
            test = (COLOR).rgb.b;                                           \
            results = (test >= low && test <= high);                        \
            results ^= CHROMARANGE_BLUE_EXCLUSIVE((VV)->reg[chromaRange].u);\
            results <<= 1;                                                  \
                                                                            \
            /* check green */                                               \
            low = (VV)->reg[chromaKey].rgb.g;                               \
            high = (VV)->reg[chromaRange].rgb.g;                            \
            test = (COLOR).rgb.g;                                           \
            results |= (test >= low && test <= high);                       \
            results ^= CHROMARANGE_GREEN_EXCLUSIVE((VV)->reg[chromaRange].u);\
            results <<= 1;                                                  \
                                                                            \
            /* check red */                                                 \
            low = (VV)->reg[chromaKey].rgb.r;                               \
            high = (VV)->reg[chromaRange].rgb.r;                            \
            test = (COLOR).rgb.r;                                           \
            results |= (test >= low && test <= high);                       \
            results ^= CHROMARANGE_RED_EXCLUSIVE((VV)->reg[chromaRange].u); \
                                                                            \
            /* final result */                                              \
            if (CHROMARANGE_UNION_MODE((VV)->reg[chromaRange].u))           \
            {                                                               \
                if (results != 0)                                           \
                {                                                           \
                    ADD_STAT_COUNT(STATS, chroma_fail)                      \
                    goto skipdrawdepth;                                     \
                }                                                           \
            }                                                               \
            else                                                            \
            {                                                               \
                if (results == 7)                                           \
                {                                                           \
                    ADD_STAT_COUNT(STATS, chroma_fail)                      \
                    goto skipdrawdepth;                                     \
                }                                                           \
            }                                                               \
        }                                                                   \
    }                                                                       \
}                                                                           \
while (0)

/*************************************
 * Alpha masking macro
 *************************************/

#define APPLY_ALPHAMASK(VV, STATS, FBZMODE, AA)                             \
do                                                                          \
{                                                                           \
    if (FBZMODE_ENABLE_ALPHA_MASK(FBZMODE))                                 \
    {                                                                       \
        if (((AA) & 1) == 0)                                                \
        {                                                                   \
            ADD_STAT_COUNT(STATS, afunc_fail)                               \
            goto skipdrawdepth;                                             \
        }                                                                   \
    }                                                                       \
}                                                                           \
while (0)

/*************************************
 * Alpha testing macro
 *************************************/

#define APPLY_ALPHATEST(VV, STATS, ALPHAMODE, AA)                           \
do                                                                          \
{                                                                           \
    if (ALPHAMODE_ALPHATEST(ALPHAMODE))                                     \
    {                                                                       \
        uint8_t alpharef = (VV)->reg[alphaMode].rgb.a;                      \
        switch (ALPHAMODE_ALPHAFUNCTION(ALPHAMODE))                         \
        {                                                                   \
            case 0:     /* alphaOP = never */                               \
                ADD_STAT_COUNT(STATS, afunc_fail)                           \
                goto skipdrawdepth;                                         \
                                                                            \
            case 1:     /* alphaOP = less than */                           \
                if ((AA) >= alpharef)                                       \
                {                                                           \
                    ADD_STAT_COUNT(STATS, afunc_fail)                       \
                    goto skipdrawdepth;                                     \
                }                                                           \
                break;                                                      \
                                                                            \
            case 2:     /* alphaOP = equal */                               \
                if ((AA) != alpharef)                                       \
                {                                                           \
                    ADD_STAT_COUNT(STATS, afunc_fail)                       \
                    goto skipdrawdepth;                                     \
                }                                                           \
                break;                                                      \
                                                                            \
            case 3:     /* alphaOP = less than or equal */                  \
                if ((AA) > alpharef)                                        \
                {                                                           \
                    ADD_STAT_COUNT(STATS, afunc_fail)                       \
                    goto skipdrawdepth;                                     \
                }                                                           \
                break;                                                      \
                                                                            \
            case 4:     /* alphaOP = greater than */                        \
                if ((AA) <= alpharef)                                       \
                {                                                           \
                    ADD_STAT_COUNT(STATS, afunc_fail)                       \
                    goto skipdrawdepth;                                     \
                }                                                           \
                break;                                                      \
                                                                            \
            case 5:     /* alphaOP = not equal */                           \
                if ((AA) == alpharef)                                       \
                {                                                           \
                    ADD_STAT_COUNT(STATS, afunc_fail)                       \
                    goto skipdrawdepth;                                     \
                }                                                           \
                break;                                                      \
                                                                            \
            case 6:     /* alphaOP = greater than or equal */               \
                if ((AA) < alpharef)                                        \
                {                                                           \
                    ADD_STAT_COUNT(STATS, afunc_fail)                       \
                    goto skipdrawdepth;                                     \
                }                                                           \
                break;                                                      \
                                                                            \
            case 7:     /* alphaOP = always */                              \
                break;                                                      \
        }                                                                   \
    }                                                                       \
}                                                                           \
while (0)

/*************************************
 * Alpha blending macro
 *************************************/

#define APPLY_ALPHA_BLEND(FBZMODE, ALPHAMODE, XX, DITHER, RR, GG, BB, AA)   \
do {                                                                        \
    if (ALPHAMODE_ALPHABLEND(ALPHAMODE)) {                                  \
        int dpix = dest[XX];                                                \
        int dr   = (dpix >> 8) & 0xf8;                                      \
        int dg   = (dpix >> 3) & 0xfc;                                      \
        int db   = (dpix << 3) & 0xf8;                                      \
        int da = (FBZMODE_ENABLE_ALPHA_PLANES(FBZMODE) && depth)            \
                       ? depth[XX]                                          \
                       : 0xff;                                              \
        const int sr_val = (RR);                                            \
        const int sg_val = (GG);                                            \
        const int sb_val = (BB);                                            \
        const int sa_val = (AA);                                            \
        int ta = 0;                                                         \
                                                                            \
        /* apply dither subtraction */                                      \
        if ((FBZMODE_ALPHA_DITHER_SUBTRACT(FBZMODE)) && (DITHER)) {         \
            int dith = (DITHER)[(XX) & 3];                                  \
            dr = ((dr << 1) + 15 - dith) >> 1;                              \
            dg = ((dg << 2) + 15 - dith) >> 2;                              \
            db = ((db << 1) + 15 - dith) >> 1;                              \
        }                                                                   \
                                                                            \
        /* compute source portion */                                        \
        switch (ALPHAMODE_SRCRGBBLEND(ALPHAMODE)) {                         \
        default:                                                            \
        case 0: (RR) = (GG) = (BB) = 0; break;                              \
        case 1:                                                             \
            (RR) = (sr_val * (sa_val + 1)) >> 8;                            \
            (GG) = (sg_val * (sa_val + 1)) >> 8;                            \
            (BB) = (sb_val * (sa_val + 1)) >> 8;                            \
            break;                                                          \
        case 2:                                                             \
            (RR) = (sr_val * (dr + 1)) >> 8;                                \
            (GG) = (sg_val * (dg + 1)) >> 8;                                \
            (BB) = (sb_val * (db + 1)) >> 8;                                \
            break;                                                          \
        case 3:                                                             \
            (RR) = (sr_val * (da + 1)) >> 8;                                \
            (GG) = (sg_val * (da + 1)) >> 8;                                \
            (BB) = (sb_val * (da + 1)) >> 8;                                \
            break;                                                          \
        case 4: break;                                                      \
        case 5:                                                             \
            (RR) = (sr_val * (0x100 - sa_val)) >> 8;                        \
            (GG) = (sg_val * (0x100 - sa_val)) >> 8;                        \
            (BB) = (sb_val * (0x100 - sa_val)) >> 8;                        \
            break;                                                          \
        case 6:                                                             \
            (RR) = (sr_val * (0x100 - dr)) >> 8;                            \
            (GG) = (sg_val * (0x100 - dg)) >> 8;                            \
            (BB) = (sb_val * (0x100 - db)) >> 8;                            \
            break;                                                          \
        case 7:                                                             \
            (RR) = (sr_val * (0x100 - da)) >> 8;                            \
            (GG) = (sg_val * (0x100 - da)) >> 8;                            \
            (BB) = (sb_val * (0x100 - da)) >> 8;                            \
            break;                                                          \
        case 15:                                                            \
            ta = (sa_val < (0x100 - da)) ? sa_val : (0x100 - da);           \
            (RR) = (sr_val * (ta + 1)) >> 8;                                \
            (GG) = (sg_val * (ta + 1)) >> 8;                                \
            (BB) = (sb_val * (ta + 1)) >> 8;                                \
            break;                                                          \
        }                                                                   \
                                                                            \
        /* add in dest portion */                                           \
        switch (ALPHAMODE_DSTRGBBLEND(ALPHAMODE)) {                         \
        default:                                                            \
        case 0: break;                                                      \
        case 1:                                                             \
            (RR) += (dr * (sa_val + 1)) >> 8;                               \
            (GG) += (dg * (sa_val + 1)) >> 8;                               \
            (BB) += (db * (sa_val + 1)) >> 8;                               \
            break;                                                          \
        case 2:                                                             \
            (RR) += (dr * (sr_val + 1)) >> 8;                               \
            (GG) += (dg * (sg_val + 1)) >> 8;                               \
            (BB) += (db * (sb_val + 1)) >> 8;                               \
            break;                                                          \
        case 3:                                                             \
            (RR) += (dr * (da + 1)) >> 8;                                   \
            (GG) += (dg * (da + 1)) >> 8;                                   \
            (BB) += (db * (da + 1)) >> 8;                                   \
            break;                                                          \
        case 4:                                                             \
            (RR) += dr;                                                     \
            (GG) += dg;                                                     \
            (BB) += db;                                                     \
            break;                                                          \
        case 5:                                                             \
            (RR) += (dr * (0x100 - sa_val)) >> 8;                           \
            (GG) += (dg * (0x100 - sa_val)) >> 8;                           \
            (BB) += (db * (0x100 - sa_val)) >> 8;                           \
            break;                                                          \
        case 6:                                                             \
            (RR) += (dr * (0x100 - sr_val)) >> 8;                           \
            (GG) += (dg * (0x100 - sg_val)) >> 8;                           \
            (BB) += (db * (0x100 - sb_val)) >> 8;                           \
            break;                                                          \
        case 7:                                                             \
            (RR) += (dr * (0x100 - da)) >> 8;                               \
            (GG) += (dg * (0x100 - da)) >> 8;                               \
            (BB) += (db * (0x100 - da)) >> 8;                               \
            break;                                                          \
        case 15:                                                            \
            (RR) += (dr * (prefogr + 1)) >> 8;                              \
            (GG) += (dg * (prefogg + 1)) >> 8;                              \
            (BB) += (db * (prefogb + 1)) >> 8;                              \
            break;                                                          \
        }                                                                   \
                                                                            \
        /* blend alpha */                                                   \
        (AA) = 0;                                                           \
        if (ALPHAMODE_SRCALPHABLEND(ALPHAMODE) == 4)                        \
            (AA) = sa_val;                                                  \
        if (ALPHAMODE_DSTALPHABLEND(ALPHAMODE) == 4)                        \
            (AA) += da;                                                     \
                                                                            \
        /* clamp */                                                         \
        (RR) = clamp_to_uint8(RR);                                          \
        (GG) = clamp_to_uint8(GG);                                          \
        (BB) = clamp_to_uint8(BB);                                          \
        (AA) = clamp_to_uint8(AA);                                          \
    }                                                                       \
} while (0)

/*************************************
 * Fogging macro
 *************************************/

#define APPLY_FOGGING(VV, FOGMODE, FBZCP, XX, DITHER4, RR, GG, BB, ITERZ, ITERW, ITERAXXX) \
do                                                                          \
{                                                                           \
    if (FOGMODE_ENABLE_FOG(FOGMODE))                                        \
    {                                                                       \
        rgb_union fogcolor = (VV)->reg[fogColor];                           \
        int32_t fr, fg, fb;                                                 \
                                                                            \
        /* constant fog bypasses everything else */                         \
        if (FOGMODE_FOG_CONSTANT(FOGMODE))                                  \
        {                                                                   \
            fr = fogcolor.rgb.r;                                            \
            fg = fogcolor.rgb.g;                                            \
            fb = fogcolor.rgb.b;                                            \
        }                                                                   \
        else                                                                \
        {                                                                   \
            int32_t fogblend = 0;                                           \
                                                                            \
            /* if fog_add is zero, we start with the fog color */           \
            if (FOGMODE_FOG_ADD(FOGMODE) == 0)                              \
            {                                                               \
                fr = fogcolor.rgb.r;                                        \
                fg = fogcolor.rgb.g;                                        \
                fb = fogcolor.rgb.b;                                        \
            }                                                               \
            else                                                            \
                fr = fg = fb = 0;                                           \
                                                                            \
            /* if fog_mult is zero, we subtract the incoming color */       \
            if (FOGMODE_FOG_MULT(FOGMODE) == 0)                             \
            {                                                               \
                fr -= (RR);                                                 \
                fg -= (GG);                                                 \
                fb -= (BB);                                                 \
            }                                                               \
                                                                            \
            /* fog blending mode */                                         \
            switch (FOGMODE_FOG_ZALPHA(FOGMODE))                            \
            {                                                               \
                case 0:     /* fog table */                                 \
                {                                                           \
                    int32_t delta = (VV)->fbi.fogdelta[wfloat >> 10];       \
                    int32_t deltaval;                                       \
                    deltaval = (delta & (VV)->fbi.fogdelta_mask) *          \
                                ((wfloat >> 2) & 0xff);                     \
                    if (FOGMODE_FOG_ZONES(FOGMODE) && (delta & 2))          \
                        deltaval = -deltaval;                               \
                    deltaval >>= 6;                                         \
                    if (FOGMODE_FOG_DITHER(FOGMODE))                        \
                        if (DITHER4)                                        \
                            deltaval += (DITHER4)[(XX) & 3];                \
                    deltaval >>= 4;                                         \
                    fogblend = (VV)->fbi.fogblend[wfloat >> 10] + deltaval; \
                    break;                                                  \
                }                                                           \
                case 1:     /* iterated A */                                \
                    fogblend = (ITERAXXX).rgb.a;                            \
                    break;                                                  \
                case 2:     /* iterated Z */                                \
                    CLAMPED_Z((ITERZ), FBZCP, fogblend);                    \
                    fogblend >>= 8;                                         \
                    break;                                                  \
                case 3:     /* iterated W - Voodoo 2 only */                \
                    CLAMPED_W((ITERW), FBZCP, fogblend);                    \
                    break;                                                  \
            }                                                               \
                                                                            \
            /* perform the blend */                                         \
            fogblend++;                                                     \
            fr = (fr * fogblend) >> 8;                                      \
            fg = (fg * fogblend) >> 8;                                      \
            fb = (fb * fogblend) >> 8;                                      \
        }                                                                   \
                                                                            \
        /* if fog_mult is 0, we add this to the original color */           \
        if (FOGMODE_FOG_MULT(FOGMODE) == 0)                                 \
        {                                                                   \
            (RR) += fr;                                                     \
            (GG) += fg;                                                     \
            (BB) += fb;                                                     \
        }                                                                   \
        else                                                                \
        {                                                                   \
            (RR) = fr;                                                      \
            (GG) = fg;                                                      \
            (BB) = fb;                                                      \
        }                                                                   \
                                                                            \
        /* clamp */                                                         \
        (RR) = clamp_to_uint8(RR);                                          \
        (GG) = clamp_to_uint8(GG);                                          \
        (BB) = clamp_to_uint8(BB);                                          \
    }                                                                       \
}                                                                           \
while (0)

/*************************************
 * Pixel pipeline macros
 *************************************/

#define PIXEL_PIPELINE_BEGIN(VV, STATS, XX, YY, FBZCOLORPATH, FBZMODE, ITERZ, ITERW, ZACOLOR, STIPPLE) \
do                                                                          \
{                                                                           \
    int32_t depthval, wfloat;                                               \
    int32_t prefogr, prefogg, prefogb;                                      \
    int32_t r, g, b, a;                                                     \
                                                                            \
    /* handle stippling */                                                  \
    if (FBZMODE_ENABLE_STIPPLE(FBZMODE))                                    \
    {                                                                       \
        /* rotate mode */                                                   \
        if (FBZMODE_STIPPLE_PATTERN(FBZMODE) == 0)                          \
        {                                                                   \
            (STIPPLE) = ((STIPPLE) << 1) | ((STIPPLE) >> 31);               \
            if (((STIPPLE) & 0x80000000) == 0)                              \
            {                                                               \
                goto skipdrawdepth;                                         \
            }                                                               \
        }                                                                   \
        /* pattern mode */                                                  \
        else                                                                \
        {                                                                   \
            int stipple_index = (((YY) & 3) << 3) | (~(XX) & 7);            \
            if ((((STIPPLE) >> stipple_index) & 1) == 0)                    \
            {                                                               \
                goto skipdrawdepth;                                         \
            }                                                               \
        }                                                                   \
    }                                                                       \
                                                                            \
    /* compute "floating point" W value (used for depth and fog) */         \
    if ((ITERW) & LONGTYPE(0xffff00000000))                                 \
        wfloat = 0x0000;                                                    \
    else                                                                    \
    {                                                                       \
        uint32_t temp = (uint32_t)(ITERW);                                  \
        if ((temp & 0xffff0000) == 0)                                       \
            wfloat = 0xffff;                                                \
        else                                                                \
        {                                                                   \
            int exp = countl_zero_u32(temp);                                \
            int right_shift = (19 - exp) > 0 ? (19 - exp) : 0;              \
            wfloat = ((exp << 12) | ((~temp >> right_shift) & 0xfff));      \
            if (wfloat < 0xffff) wfloat++;                                  \
        }                                                                   \
    }                                                                       \
                                                                            \
    /* compute depth value (W or Z) for this pixel */                       \
    if (FBZMODE_WBUFFER_SELECT(FBZMODE) == 0)                               \
        CLAMPED_Z(ITERZ, FBZCOLORPATH, depthval);                           \
    else if (FBZMODE_DEPTH_FLOAT_SELECT(FBZMODE) == 0)                      \
        depthval = wfloat;                                                  \
    else                                                                    \
    {                                                                       \
        if ((ITERZ) & 0xf0000000)                                           \
            depthval = 0x0000;                                              \
        else                                                                \
        {                                                                   \
            uint32_t temp = (ITERZ) << 4;                                   \
            if ((temp & 0xffff0000) == 0)                                   \
                depthval = 0xffff;                                          \
            else                                                            \
            {                                                               \
                int exp = countl_zero_u32(temp);                            \
                int right_shift = (19 - exp) > 0 ? (19 - exp) : 0;          \
                depthval = ((exp << 12) | ((~temp >> right_shift) & 0xfff));\
                if (depthval < 0xffff) depthval++;                          \
            }                                                               \
        }                                                                   \
    }                                                                       \
                                                                            \
    /* add the bias */                                                      \
    if (FBZMODE_ENABLE_DEPTH_BIAS(FBZMODE))                                 \
    {                                                                       \
        depthval += (int16_t)(ZACOLOR);                                     \
        depthval = clamp_to_uint16(depthval);                               \
    }                                                                       \
                                                                            \
    /* handle depth buffer testing */                                       \
    if (FBZMODE_ENABLE_DEPTHBUF(FBZMODE))                                   \
    {                                                                       \
        int32_t depthsource;                                                \
        if (FBZMODE_DEPTH_SOURCE_COMPARE(FBZMODE) == 0)                     \
            depthsource = depthval;                                         \
        else                                                                \
            depthsource = (uint16_t)(ZACOLOR);                              \
                                                                            \
        /* test against the depth buffer */                                 \
        switch (FBZMODE_DEPTH_FUNCTION(FBZMODE))                            \
        {                                                                   \
            case 0:     /* depthOP = never */                               \
                ADD_STAT_COUNT(STATS, zfunc_fail)                           \
                goto skipdrawdepth;                                         \
            case 1:     /* depthOP = less than */                           \
                if (depth)                                                  \
                    if (depthsource >= depth[XX])                           \
                    {                                                       \
                        ADD_STAT_COUNT(STATS, zfunc_fail)                   \
                        goto skipdrawdepth;                                 \
                    }                                                       \
                break;                                                      \
            case 2:     /* depthOP = equal */                               \
                if (depth)                                                  \
                    if (depthsource != depth[XX])                           \
                    {                                                       \
                        ADD_STAT_COUNT(STATS, zfunc_fail)                   \
                        goto skipdrawdepth;                                 \
                    }                                                       \
                break;                                                      \
            case 3:     /* depthOP = less than or equal */                  \
                if (depth)                                                  \
                    if (depthsource > depth[XX])                            \
                    {                                                       \
                        ADD_STAT_COUNT(STATS, zfunc_fail)                   \
                        goto skipdrawdepth;                                 \
                    }                                                       \
                break;                                                      \
            case 4:     /* depthOP = greater than */                        \
                if (depth)                                                  \
                    if (depthsource <= depth[XX])                           \
                    {                                                       \
                        ADD_STAT_COUNT(STATS, zfunc_fail)                   \
                        goto skipdrawdepth;                                 \
                    }                                                       \
                break;                                                      \
            case 5:     /* depthOP = not equal */                           \
                if (depth)                                                  \
                    if (depthsource == depth[XX])                           \
                    {                                                       \
                        ADD_STAT_COUNT(STATS, zfunc_fail)                   \
                        goto skipdrawdepth;                                 \
                    }                                                       \
                break;                                                      \
            case 6:     /* depthOP = greater than or equal */               \
                if (depth)                                                  \
                    if (depthsource < depth[XX])                            \
                    {                                                       \
                        ADD_STAT_COUNT(STATS, zfunc_fail)                   \
                        goto skipdrawdepth;                                 \
                    }                                                       \
                break;                                                      \
            case 7:     /* depthOP = always */                              \
                break;                                                      \
        }                                                                   \
    }

#define PIXEL_PIPELINE_MODIFY(VV, DITHER, DITHER4, XX, FBZMODE, FBZCOLORPATH, ALPHAMODE, FOGMODE, ITERZ, ITERW, ITERAXXX) \
    /* perform fogging */                                                   \
    prefogr = r;                                                            \
    prefogg = g;                                                            \
    prefogb = b;                                                            \
    APPLY_FOGGING(VV, FOGMODE, FBZCOLORPATH, XX, DITHER4, r, g, b,          \
                    ITERZ, ITERW, ITERAXXX);                                \
    /* perform alpha blending */                                            \
    APPLY_ALPHA_BLEND(FBZMODE, ALPHAMODE, XX, DITHER, r, g, b, a);

#define PIXEL_PIPELINE_FINISH(VV, DITHER_LOOKUP, XX, dest, depth, FBZMODE)  \
    /* write to framebuffer */                                              \
    if (FBZMODE_RGB_BUFFER_MASK(FBZMODE))                                   \
    {                                                                       \
        /* apply dithering */                                               \
        APPLY_DITHER(FBZMODE, XX, DITHER_LOOKUP, r, g, b);                  \
        (dest)[XX] = (uint16_t)((r << 11) | (g << 5) | b);                                                  \
    }                                                                       \
    /* write to aux buffer */                                               \
    if ((depth) && FBZMODE_AUX_BUFFER_MASK(FBZMODE))                        \
    {                                                                       \
        if (FBZMODE_ENABLE_ALPHA_PLANES(FBZMODE) == 0)                      \
            (depth)[XX] = (uint16_t)depthval;                               \
        else                                                                \
            (depth)[XX] = (uint16_t)a;                                      \
    }

#define PIXEL_PIPELINE_END(STATS)                                           \
    /* track pixel writes */                                                \
    ADD_STAT_COUNT(STATS, pixels_out)                                       \
skipdrawdepth:                                                              \
    ;                                                                       \
}                                                                           \
while (0)

/*************************************
 * Texture format fetch macros
 *************************************/

/* Fetch texel from 8-bit texture formats */
#define FETCH_TEXEL_8BIT(RESULT, LOOKUP, RAM, ADDRESS)                      \
do {                                                                        \
    uint8_t texel_val = (RAM)[(ADDRESS) & 0xfffff];                         \
    (RESULT) = (LOOKUP)[texel_val];                                         \
} while (0)

/* Fetch texel from 16-bit texture formats */
#define FETCH_TEXEL_16BIT(RESULT, LOOKUP, RAM, ADDRESS)                     \
do {                                                                        \
    uint32_t addr = (ADDRESS) & 0xfffff;                                    \
    uint16_t texel_val = *(uint16_t*)&(RAM)[addr];                          \
    (RESULT) = (LOOKUP)[texel_val];                                         \
} while (0)

/*************************************
 * Texture coordinate computation
 *************************************/

#define COMPUTE_TEX_COORDS(ITERS, ITERT, ITERW, TEXMODE, SS, TT, LODBASE)   \
do {                                                                        \
    int32_t _oow_log2;                                                      \
    int64_t _oow = fast_reciplog((ITERW), &_oow_log2);                       \
                                                                            \
    /* Perspective correct S and T */                                       \
    (SS) = (int32_t)(((ITERS) * _oow) >> 29);                               \
    (TT) = (int32_t)(((ITERT) * _oow) >> 29);                               \
                                                                            \
    /* Compute LOD (simplified - use base LOD) */                           \
    (LODBASE) = 0;                                                          \
} while (0)

/*************************************
 * Texture lookup with clamping/wrapping
 *************************************/

#define APPLY_TEX_CLAMP_WRAP(TEXMODE, VAL, MASK)                            \
do {                                                                        \
    if (TEXMODE_CLAMP_S(TEXMODE)) {                                         \
        if ((VAL) < 0) (VAL) = 0;                                           \
        if ((VAL) > (int32_t)(MASK)) (VAL) = (MASK);                        \
    } else {                                                                \
        (VAL) &= (MASK);                                                    \
    }                                                                       \
} while (0)

/*************************************
 * TEXTURE_PIPELINE - fetches and combines texel with color
 *************************************/

#define TEXTURE_PIPELINE(VV, TMU, XX, DITHER4, TEXMODE,                     \
                         ITERS, ITERT, ITERW, RESULT)                       \
do {                                                                        \
    int32_t ss, tt, lodbase;                                                \
    tmu_state *_tmu = &(VV)->tmu[TMU];                                      \
    rgb_t _texel;                                                           \
                                                                            \
    /* Compute perspective-correct texture coordinates */                   \
    COMPUTE_TEX_COORDS(ITERS, ITERT, ITERW, TEXMODE, ss, tt, lodbase);      \
                                                                            \
    /* Apply clamp/wrap to S and T */                                       \
    int32_t _ss = ss >> 8;                                                  \
    int32_t _tt = tt >> 8;                                                  \
    if (TEXMODE_CLAMP_S(TEXMODE)) {                                         \
        if (_ss < 0) _ss = 0;                                               \
        if (_ss > (int32_t)_tmu->wmask) _ss = _tmu->wmask;                  \
    } else {                                                                \
        _ss &= _tmu->wmask;                                                 \
    }                                                                       \
    if (TEXMODE_CLAMP_T(TEXMODE)) {                                         \
        if (_tt < 0) _tt = 0;                                               \
        if (_tt > (int32_t)_tmu->hmask) _tt = _tmu->hmask;                  \
    } else {                                                                \
        _tt &= _tmu->hmask;                                                 \
    }                                                                       \
                                                                            \
    /* Compute texel address based on LOD */                                \
    uint32_t _texaddr = _tmu->lodoffset[lodbase] +                          \
                        _tt * ((_tmu->wmask + 1) >> lodbase) + _ss;         \
                                                                            \
    /* Fetch texel based on format */                                       \
    int _texfmt = TEXMODE_FORMAT(TEXMODE);                                  \
    switch (_texfmt) {                                                      \
    case 0:  /* 8-bit indexed */                                            \
    case 1:  /* 8-bit YIQ */                                                \
    case 2:  /* 8-bit Alpha */                                              \
    case 3:  /* 8-bit Intensity */                                          \
    case 4:  /* 8-bit Alpha+Intensity */                                    \
    case 9:  /* 8-bit palette */                                            \
    case 10: /* NCC table 0 */                                              \
    case 11: /* NCC table 1 */                                              \
        _texel = _tmu->lookup ? _tmu->lookup[_tmu->ram[_texaddr & _tmu->mask]] \
                              : (VV)->tmushare.int8[_tmu->ram[_texaddr & _tmu->mask]]; \
        break;                                                              \
                                                                            \
    case 5:  /* 16-bit RGB 5-6-5 */                                         \
        {                                                                   \
            uint32_t addr16 = (_texaddr * 2) & _tmu->mask;                  \
            uint16_t texval16 = *(uint16_t*)&_tmu->ram[addr16];             \
            _texel = (VV)->tmushare.rgb565[texval16];                       \
        }                                                                   \
        break;                                                              \
                                                                            \
    case 6:  /* 16-bit ARGB 1-5-5-5 */                                      \
        {                                                                   \
            uint32_t addr16 = (_texaddr * 2) & _tmu->mask;                  \
            uint16_t texval16 = *(uint16_t*)&_tmu->ram[addr16];             \
            _texel = (VV)->tmushare.argb1555[texval16];                     \
        }                                                                   \
        break;                                                              \
                                                                            \
    case 7:  /* 16-bit ARGB 4-4-4-4 */                                      \
        {                                                                   \
            uint32_t addr16 = (_texaddr * 2) & _tmu->mask;                  \
            uint16_t texval16 = *(uint16_t*)&_tmu->ram[addr16];             \
            _texel = (VV)->tmushare.argb4444[texval16];                     \
        }                                                                   \
        break;                                                              \
                                                                            \
    case 8:  /* 8-bit alpha+intensity interleaved */                        \
        _texel = (VV)->tmushare.ai44[_tmu->ram[_texaddr & _tmu->mask]];     \
        break;                                                              \
                                                                            \
    default:                                                                \
        _texel = MAKE_ARGB(255, 255, 0, 255);  /* Magenta = unsupported */  \
        break;                                                              \
    }                                                                       \
                                                                            \
    /* Apply bilinear filtering if enabled */                               \
    if (TEXMODE_MAGNIFICATION_FILTER(TEXMODE) ||                            \
        TEXMODE_MINIFICATION_FILTER(TEXMODE)) {                             \
        /* Get fractional parts for filtering */                            \
        uint8_t _ufrac = (ss >> 0) & 0xff;                                  \
        uint8_t _vfrac = (tt >> 0) & 0xff;                                  \
        if (_ufrac || _vfrac) {                                             \
            /* Fetch neighboring texels and blend */                        \
            /* (Simplified - full bilinear would need 3 more fetches) */    \
        }                                                                   \
    }                                                                       \
                                                                            \
    (RESULT) = _texel;                                                      \
} while (0)

/*************************************
 * Texture combine - combines texel with incoming color
 *************************************/

#define APPLY_TEXTURE_COMBINE(TEXMODE, C_LOCAL, C_OTHER, A_LOCAL, A_OTHER, RESULT_R, RESULT_G, RESULT_B, RESULT_A) \
do {                                                                        \
    int32_t _tr, _tg, _tb, _ta;                                             \
    int32_t _c_other_r = (C_OTHER).rgb.r;                                   \
    int32_t _c_other_g = (C_OTHER).rgb.g;                                   \
    int32_t _c_other_b = (C_OTHER).rgb.b;                                   \
    int32_t _c_local_r = (C_LOCAL).rgb.r;                                   \
    int32_t _c_local_g = (C_LOCAL).rgb.g;                                   \
    int32_t _c_local_b = (C_LOCAL).rgb.b;                                   \
    int32_t _a_local = (A_LOCAL);                                           \
    int32_t _a_other = (A_OTHER);                                           \
                                                                            \
    /* Zero other if requested */                                           \
    if (TEXMODE_TC_ZERO_OTHER(TEXMODE)) {                                   \
        _c_other_r = _c_other_g = _c_other_b = 0;                           \
    }                                                                       \
                                                                            \
    /* Subtract c_local if requested */                                     \
    if (TEXMODE_TC_SUB_CLOCAL(TEXMODE)) {                                   \
        _tr = _c_other_r - _c_local_r;                                      \
        _tg = _c_other_g - _c_local_g;                                      \
        _tb = _c_other_b - _c_local_b;                                      \
    } else {                                                                \
        _tr = _c_other_r;                                                   \
        _tg = _c_other_g;                                                   \
        _tb = _c_other_b;                                                   \
    }                                                                       \
                                                                            \
    /* Multiply select */                                                   \
    int32_t _blend = 0;                                                     \
    switch (TEXMODE_TC_MSELECT(TEXMODE)) {                                  \
    case 0: _blend = 0; break;                                              \
    case 1: _blend = _c_local_r; break;  /* c_local */                      \
    case 2: _blend = _a_other; break;    /* a_other */                      \
    case 3: _blend = _a_local; break;    /* a_local */                      \
    case 4: _blend = _a_local; break;    /* detail */                       \
    case 5: _blend = (C_LOCAL).rgb.a; break; /* LOD fraction */             \
    default: _blend = 0; break;                                             \
    }                                                                       \
                                                                            \
    /* Apply blend and reverse blend */                                     \
    if (TEXMODE_TC_REVERSE_BLEND(TEXMODE))                                  \
        _blend = 0x100 - _blend;                                            \
                                                                            \
    _tr = (_tr * (_blend + 1)) >> 8;                                        \
    _tg = (_tg * (_blend + 1)) >> 8;                                        \
    _tb = (_tb * (_blend + 1)) >> 8;                                        \
                                                                            \
    /* Add a_clocal or c_local */                                           \
    switch (TEXMODE_TC_ADD_ACLOCAL(TEXMODE)) {                              \
    case 0: break;  /* nothing */                                           \
    case 1:         /* c_local */                                           \
        _tr += _c_local_r;                                                  \
        _tg += _c_local_g;                                                  \
        _tb += _c_local_b;                                                  \
        break;                                                              \
    case 2:         /* a_local */                                           \
        _tr += _a_local;                                                    \
        _tg += _a_local;                                                    \
        _tb += _a_local;                                                    \
        break;                                                              \
    }                                                                       \
                                                                            \
    /* Invert output */                                                     \
    if (TEXMODE_TC_INVERT_OUTPUT(TEXMODE)) {                                \
        _tr = 0xff - _tr;                                                   \
        _tg = 0xff - _tg;                                                   \
        _tb = 0xff - _tb;                                                   \
    }                                                                       \
                                                                            \
    /* Alpha combine (simplified) */                                        \
    if (TEXMODE_TCA_ZERO_OTHER(TEXMODE))                                    \
        _ta = 0;                                                            \
    else                                                                    \
        _ta = _a_other;                                                     \
                                                                            \
    if (TEXMODE_TCA_SUB_CLOCAL(TEXMODE))                                    \
        _ta -= _a_local;                                                    \
                                                                            \
    if (TEXMODE_TCA_ADD_ACLOCAL(TEXMODE) == 1)                              \
        _ta += _a_local;                                                    \
                                                                            \
    if (TEXMODE_TCA_INVERT_OUTPUT(TEXMODE))                                 \
        _ta = 0xff - _ta;                                                   \
                                                                            \
    /* Clamp results */                                                     \
    (RESULT_R) = clamp_to_uint8(_tr);                                       \
    (RESULT_G) = clamp_to_uint8(_tg);                                       \
    (RESULT_B) = clamp_to_uint8(_tb);                                       \
    (RESULT_A) = clamp_to_uint8(_ta);                                       \
} while (0)

#endif /* VOODOO_PIPELINE_H */
