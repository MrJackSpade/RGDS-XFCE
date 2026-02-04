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
    /* RE-ENABLED: perform fogging */                                       \
    prefogr = r;                                                            \
    prefogg = g;                                                            \
    prefogb = b;                                                            \
    APPLY_FOGGING(VV, FOGMODE, FBZCOLORPATH, XX, DITHER4, r, g, b,          \
                    ITERZ, ITERW, ITERAXXX);                                \
    /* RE-ENABLED: perform alpha blending */                                \
    APPLY_ALPHA_BLEND(FBZMODE, ALPHAMODE, XX, DITHER, r, g, b, a);

#define PIXEL_PIPELINE_FINISH(VV, DITHER_LOOKUP, XX, dest, depth, FBZMODE)  \
    /* write to framebuffer */                                              \
    if (FBZMODE_RGB_BUFFER_MASK(FBZMODE))                                   \
    {                                                                       \
        /* apply dithering */                                               \
        APPLY_DITHER(FBZMODE, XX, DITHER_LOOKUP, r, g, b);                  \
        (dest)[XX] = (uint16_t)((r << 11) | (g << 5) | b);                  \
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
 * TEXTURE_PIPELINE - fetches and combines texel with color
 *************************************/
 /* VALIDATED */

#define TEXTURE_PIPELINE(TT, XX, DITHER4, TEXMODE, COTHER, LOOKUP, LODBASE, ITERS, ITERT, ITERW, RESULT) \
do																				\
{																				\
	int32_t blendr, blendg, blendb, blenda;										\
	int32_t tr, tg, tb, ta;														\
	int32_t s, t, lod, ilod;														\
	int64_t oow;																	\
	int32_t smax, tmax;															\
	uint32_t texbase;																\
	rgb_union c_local;															\
																				\
	/* determine the S/T/LOD values for this texture */							\
	if (TEXMODE_ENABLE_PERSPECTIVE(TEXMODE))									\
	{																			\
		oow = fast_reciplog((ITERW), &lod);										\
		s = (int32_t)((oow * (ITERS)) >> 29);										\
		t = (int32_t)((oow * (ITERT)) >> 29);										\
		lod += (LODBASE);														\
	}																			\
	else																		\
	{																			\
		s = (int32_t)((ITERS) >> 14);												\
		t = (int32_t)((ITERT) >> 14);												\
		lod = (LODBASE);														\
	}																			\
																				\
	/* clamp W */																\
	if (TEXMODE_CLAMP_NEG_W(TEXMODE) && (ITERW) < 0)							\
		s = t = 0;																\
																				\
	/* clamp the LOD */															\
	lod += (TT)->lodbias;														\
	if (TEXMODE_ENABLE_LOD_DITHER(TEXMODE))										\
		if (DITHER4)															\
			lod += (DITHER4)[(XX) & 3] << 4;										\
	if (lod < (TT)->lodmin)														\
		lod = (TT)->lodmin;														\
	if (lod > (TT)->lodmax)														\
		lod = (TT)->lodmax;														\
																				\
	/* now the LOD is in range; if we don't own this LOD, take the next one */	\
	ilod = lod >> 8;															\
	if (!(((TT)->lodmask >> ilod) & 1))											\
		ilod++;																	\
																				\
	/* fetch the texture base */												\
	texbase = (TT)->lodoffset[ilod];											\
																				\
	/* compute the maximum s and t values at this LOD */						\
	smax = (TT)->wmask >> ilod;													\
	tmax = (TT)->hmask >> ilod;													\
																				\
	/* determine whether we are point-sampled or bilinear */					\
	if ((lod == (TT)->lodmin && !TEXMODE_MAGNIFICATION_FILTER(TEXMODE)) ||		\
		(lod != (TT)->lodmin && !TEXMODE_MINIFICATION_FILTER(TEXMODE)))			\
	{																			\
		/* point sampled */														\
																				\
		uint32_t texel0;															\
																				\
		/* adjust S/T for the LOD and strip off the fractions */				\
		s >>= ilod + 18;														\
		t >>= ilod + 18;														\
																				\
		/* clamp/wrap S/T if necessary */										\
		if (TEXMODE_CLAMP_S(TEXMODE))											\
			s = clamp_val(s, 0, smax);											\
		if (TEXMODE_CLAMP_T(TEXMODE))											\
			t = clamp_val(t, 0, tmax);											\
		s &= smax;																\
		t &= tmax;																\
		t *= smax + 1;															\
																				\
		/* fetch texel data */													\
		if (TEXMODE_FORMAT(TEXMODE) < 8)										\
		{																		\
			texel0 = (TT)->ram[(texbase + t + s) & (TT)->mask];					\
			c_local.u = (LOOKUP)[texel0];										\
		}																		\
		else																	\
		{																		\
			texel0 = *(uint16_t *)&(TT)->ram[(texbase + 2*(t + s)) & (TT)->mask];	\
			if (TEXMODE_FORMAT(TEXMODE) >= 10 && TEXMODE_FORMAT(TEXMODE) <= 12)	\
				c_local.u = (LOOKUP)[texel0];									\
			else																\
				c_local.u = ((LOOKUP)[texel0 & 0xff] & 0xffffff) |				\
							((texel0 & 0xff00) << 16);							\
		}																		\
	}																			\
	else																		\
	{																			\
		/* bilinear filtered */													\
																				\
		uint32_t texel0, texel1, texel2, texel3;									\
		uint8_t sfrac, tfrac;														\
		int32_t s1, t1;															\
																				\
		/* adjust S/T for the LOD and strip off all but the low 8 bits of */	\
		/* the fraction */														\
		s >>= ilod + 10;														\
		t >>= ilod + 10;														\
																				\
		/* also subtract 1/2 texel so that (0.5,0.5) = a full (0,0) texel */	\
		s -= 0x80;																\
		t -= 0x80;																\
																				\
		/* extract the fractions */												\
		sfrac = (uint8_t)(s & (TT)->bilinear_mask);								\
		tfrac = (uint8_t)(t & (TT)->bilinear_mask);								\
																				\
		/* now toss the rest */													\
		s >>= 8;																\
		t >>= 8;																\
		s1 = s + 1;																\
		t1 = t + 1;																\
																				\
		/* clamp/wrap S/T if necessary */										\
		if (TEXMODE_CLAMP_S(TEXMODE))											\
		{																		\
			s = clamp_val(s, 0, smax);											\
			s1 = clamp_val(s1, 0, smax);										\
		}																		\
		if (TEXMODE_CLAMP_T(TEXMODE))											\
		{																		\
			t = clamp_val(t, 0, tmax);											\
			t1 = clamp_val(t1, 0, tmax);										\
		}																		\
		s &= smax;																\
		s1 &= smax;																\
		t &= tmax;																\
		t1 &= tmax;																\
		t *= smax + 1;															\
		t1 *= smax + 1;															\
																				\
		/* fetch texel data */													\
		if (TEXMODE_FORMAT(TEXMODE) < 8)										\
		{																		\
			texel0 = (TT)->ram[(texbase + t + s) & (TT)->mask];					\
			texel1 = (TT)->ram[(texbase + t + s1) & (TT)->mask];				\
			texel2 = (TT)->ram[(texbase + t1 + s) & (TT)->mask];				\
			texel3 = (TT)->ram[(texbase + t1 + s1) & (TT)->mask];				\
			texel0 = (LOOKUP)[texel0];											\
			texel1 = (LOOKUP)[texel1];											\
			texel2 = (LOOKUP)[texel2];											\
			texel3 = (LOOKUP)[texel3];											\
		}																		\
		else																	\
		{																		\
			texel0 = *(uint16_t *)&(TT)->ram[(texbase + 2*(t + s)) & (TT)->mask];	\
			texel1 = *(uint16_t *)&(TT)->ram[(texbase + 2*(t + s1)) & (TT)->mask];\
			texel2 = *(uint16_t *)&(TT)->ram[(texbase + 2*(t1 + s)) & (TT)->mask];\
			texel3 = *(uint16_t *)&(TT)->ram[(texbase + 2*(t1 + s1)) & (TT)->mask];\
			if (TEXMODE_FORMAT(TEXMODE) >= 10 && TEXMODE_FORMAT(TEXMODE) <= 12)	\
			{																	\
				texel0 = (LOOKUP)[texel0];										\
				texel1 = (LOOKUP)[texel1];										\
				texel2 = (LOOKUP)[texel2];										\
				texel3 = (LOOKUP)[texel3];										\
			}																	\
			else																\
			{																	\
				texel0 = ((LOOKUP)[texel0 & 0xff] & 0xffffff) | 				\
							((texel0 & 0xff00) << 16);							\
				texel1 = ((LOOKUP)[texel1 & 0xff] & 0xffffff) | 				\
							((texel1 & 0xff00) << 16);							\
				texel2 = ((LOOKUP)[texel2 & 0xff] & 0xffffff) | 				\
							((texel2 & 0xff00) << 16);							\
				texel3 = ((LOOKUP)[texel3 & 0xff] & 0xffffff) | 				\
							((texel3 & 0xff00) << 16);							\
			}																	\
		}																		\
																				\
		/* weigh in each texel */												\
		c_local.u = rgba_bilinear_filter(texel0, texel1, texel2, texel3, sfrac, tfrac);\
	}																			\
																				\
	/* select zero/other for RGB */												\
	if (!TEXMODE_TC_ZERO_OTHER(TEXMODE))										\
	{																			\
		tr = (COTHER).rgb.r;														\
		tg = (COTHER).rgb.g;														\
		tb = (COTHER).rgb.b;														\
	}																			\
	else																		\
		tr = tg = tb = 0;														\
																				\
	/* select zero/other for alpha */											\
	if (!TEXMODE_TCA_ZERO_OTHER(TEXMODE))										\
		ta = (COTHER).rgb.a;														\
	else																		\
		ta = 0;																	\
																				\
	/* potentially subtract c_local */											\
	if (TEXMODE_TC_SUB_CLOCAL(TEXMODE))											\
	{																			\
		tr -= c_local.rgb.r;													\
		tg -= c_local.rgb.g;													\
		tb -= c_local.rgb.b;													\
	}																			\
	if (TEXMODE_TCA_SUB_CLOCAL(TEXMODE))										\
		ta -= c_local.rgb.a;													\
																				\
	/* blend RGB */																\
	switch (TEXMODE_TC_MSELECT(TEXMODE))										\
	{																			\
		default:	/* reserved */												\
		case 0:		/* zero */													\
			blendr = blendg = blendb = 0;										\
			break;																\
																				\
		case 1:		/* c_local */												\
			blendr = c_local.rgb.r;												\
			blendg = c_local.rgb.g;												\
			blendb = c_local.rgb.b;												\
			break;																\
																				\
		case 2:		/* a_other */												\
			blendr = blendg = blendb = (COTHER).rgb.a;							\
			break;																\
																				\
		case 3:		/* a_local */												\
			blendr = blendg = blendb = c_local.rgb.a;							\
			break;																\
																				\
		case 4:		/* LOD (detail factor) */									\
			if ((TT)->detailbias <= lod)										\
				blendr = blendg = blendb = 0;									\
			else																\
			{																	\
				blendr = ((((TT)->detailbias - lod) << (TT)->detailscale) >> 8);\
				if (blendr > (TT)->detailmax)									\
					blendr = (TT)->detailmax;									\
				blendg = blendb = blendr;										\
			}																	\
			break;																\
																				\
		case 5:		/* LOD fraction */											\
			blendr = blendg = blendb = lod & 0xff;								\
			break;																\
	}																			\
																				\
	/* blend alpha */															\
	switch (TEXMODE_TCA_MSELECT(TEXMODE))										\
	{																			\
		default:	/* reserved */												\
		case 0:		/* zero */													\
			blenda = 0;															\
			break;																\
																				\
		case 1:		/* c_local */												\
			blenda = c_local.rgb.a;												\
			break;																\
																				\
		case 2:		/* a_other */												\
			blenda = (COTHER).rgb.a;												\
			break;																\
																				\
		case 3:		/* a_local */												\
			blenda = c_local.rgb.a;												\
			break;																\
																				\
		case 4:		/* LOD (detail factor) */									\
			if ((TT)->detailbias <= lod)										\
				blenda = 0;														\
			else																\
			{																	\
				blenda = ((((TT)->detailbias - lod) << (TT)->detailscale) >> 8);\
				if (blenda > (TT)->detailmax)									\
					blenda = (TT)->detailmax;									\
			}																	\
			break;																\
																				\
		case 5:		/* LOD fraction */											\
			blenda = lod & 0xff;												\
			break;																\
	}																			\
																				\
	/* reverse the RGB blend */													\
	if (!TEXMODE_TC_REVERSE_BLEND(TEXMODE))										\
	{																			\
		blendr ^= 0xff;															\
		blendg ^= 0xff;															\
		blendb ^= 0xff;															\
	}																			\
																				\
	/* reverse the alpha blend */												\
	if (!TEXMODE_TCA_REVERSE_BLEND(TEXMODE))									\
		blenda ^= 0xff;															\
																				\
	/* do the blend */															\
	tr = (tr * (blendr + 1)) >> 8;												\
	tg = (tg * (blendg + 1)) >> 8;												\
	tb = (tb * (blendb + 1)) >> 8;												\
	ta = (ta * (blenda + 1)) >> 8;												\
																				\
	/* add clocal or alocal to RGB */											\
	switch (TEXMODE_TC_ADD_ACLOCAL(TEXMODE))									\
	{																			\
		case 3:		/* reserved */												\
		case 0:		/* nothing */												\
			break;																\
																				\
		case 1:		/* add c_local */											\
			tr += c_local.rgb.r;												\
			tg += c_local.rgb.g;												\
			tb += c_local.rgb.b;												\
			break;																\
																				\
		case 2:		/* add_alocal */											\
			tr += c_local.rgb.a;												\
			tg += c_local.rgb.a;												\
			tb += c_local.rgb.a;												\
			break;																\
	}																			\
																				\
	/* add clocal or alocal to alpha */											\
	if (TEXMODE_TCA_ADD_ACLOCAL(TEXMODE))										\
		ta += c_local.rgb.a;													\
																				\
	/* clamp */																	\
	(RESULT).rgb.r = (tr < 0) ? 0 : (tr > 0xff) ? 0xff : (uint8_t)tr;				\
	(RESULT).rgb.g = (tg < 0) ? 0 : (tg > 0xff) ? 0xff : (uint8_t)tg;				\
	(RESULT).rgb.b = (tb < 0) ? 0 : (tb > 0xff) ? 0xff : (uint8_t)tb;				\
	(RESULT).rgb.a = (ta < 0) ? 0 : (ta > 0xff) ? 0xff : (uint8_t)ta;				\
																				\
	/* invert */																\
	if (TEXMODE_TC_INVERT_OUTPUT(TEXMODE))										\
		(RESULT).u ^= 0x00ffffff;													\
	if (TEXMODE_TCA_INVERT_OUTPUT(TEXMODE))										\
		(RESULT).rgb.a ^= 0xff;													\
}																				\
while (0)

#endif /* VOODOO_PIPELINE_H */
