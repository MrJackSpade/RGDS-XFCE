/*
 * voodoo_defs.h - Register definitions extracted from DOSBox-Staging voodoo.cpp
 *
 * SPDX-License-Identifier: BSD-3-Clause AND GPL-2.0-or-later
 * Original Copyright: Aaron Giles (MAME), kekko, Bernhard Schelling, DOSBox Staging Team
 */

#ifndef VOODOO_DEFS_H
#define VOODOO_DEFS_H

/*************************************
 * Voodoo model enumeration
 *************************************/

typedef enum {
    VOODOO_1,
    VOODOO_1_DTMU,
    VOODOO_2,
} VoodooModel;

/*************************************
 * Constants
 *************************************/

#define MAX_TMU                 2

/* Chip register base indices in the voodoo_state reg[] array.
 * Used for register dispatch and TMU initialization.
 * TMU registers are accessed as t->reg[textureMode], t->reg[tLOD], etc.
 * where t->reg points to &v->reg[TMU0_REG_BASE] or &v->reg[TMU1_REG_BASE].
 * TMU1 is at offset 0x400 bytes (0x100 dwords) from TMU0 per hardware spec. */
#define FBI_REG_BASE            0x000
#define TMU0_REG_BASE           0x100
#define TMU1_REG_BASE           0x200
#define TMU2_REG_BASE           0x300

#define RECIPLOG_LOOKUP_BITS    9
#define RECIPLOG_INPUT_PREC     32
#define RECIPLOG_LOOKUP_PREC    22
#define RECIP_OUTPUT_PREC       15
#define LOG_OUTPUT_PREC         8

/* LFB write flags */
#define LFB_RGB_PRESENT         1
#define LFB_ALPHA_PRESENT       2
#define LFB_DEPTH_PRESENT       4
#define LFB_DEPTH_PRESENT_MSW   8

/* Register access flags */
#define REGISTER_READ           0x01
#define REGISTER_WRITE          0x02
#define REGISTER_PIPELINED      0x04
#define REGISTER_FIFO           0x08
#define REGISTER_WRITETHRU      0x10

/*************************************
 * INITEN register bits
 *************************************/

#define INITEN_ENABLE_HW_INIT(val)          (((val) >> 0) & 1)
#define INITEN_ENABLE_PCI_FIFO(val)         (((val) >> 1) & 1)
#define INITEN_REMAP_INIT_TO_DAC(val)       (((val) >> 2) & 1)
#define INITEN_ENABLE_SNOOP0(val)           (((val) >> 4) & 1)
#define INITEN_SNOOP0_MEMORY_MATCH(val)     (((val) >> 5) & 1)
#define INITEN_SNOOP0_READWRITE_MATCH(val)  (((val) >> 6) & 1)
#define INITEN_ENABLE_SNOOP1(val)           (((val) >> 7) & 1)
#define INITEN_SNOOP1_MEMORY_MATCH(val)     (((val) >> 8) & 1)
#define INITEN_SNOOP1_READWRITE_MATCH(val)  (((val) >> 9) & 1)
#define INITEN_SLI_BUS_OWNER(val)           (((val) >> 10) & 1)
#define INITEN_SLI_ODD_EVEN(val)            (((val) >> 11) & 1)
#define INITEN_SECONDARY_REV_ID(val)        (((val) >> 12) & 0xf)
#define INITEN_MFCTR_FAB_ID(val)            (((val) >> 16) & 0xf)
#define INITEN_ENABLE_PCI_INTERRUPT(val)    (((val) >> 20) & 1)
#define INITEN_PCI_INTERRUPT_TIMEOUT(val)   (((val) >> 21) & 1)
#define INITEN_ENABLE_NAND_TREE_TEST(val)   (((val) >> 22) & 1)
#define INITEN_ENABLE_SLI_ADDRESS_SNOOP(val) (((val) >> 23) & 1)
#define INITEN_SLI_SNOOP_ADDRESS(val)       (((val) >> 24) & 0xff)

/*************************************
 * FBZCOLORPATH register bits
 *************************************/

/* Getter macros (extract field from register value) */
#define FBZCP_CC_RGBSELECT(val)             (((val) >> 0) & 3)
#define FBZCP_CC_ASELECT(val)               (((val) >> 2) & 3)
#define FBZCP_CC_LOCALSELECT(val)           (((val) >> 4) & 1)
#define FBZCP_CCA_LOCALSELECT(val)          (((val) >> 5) & 3)
#define FBZCP_CC_LOCALSELECT_OVERRIDE(val)  (((val) >> 7) & 1)
#define FBZCP_CC_ZERO_OTHER(val)            (((val) >> 8) & 1)
#define FBZCP_CC_SUB_CLOCAL(val)            (((val) >> 9) & 1)
#define FBZCP_CC_MSELECT(val)               (((val) >> 10) & 7)
#define FBZCP_CC_REVERSE_BLEND(val)         (((val) >> 13) & 1)
#define FBZCP_CC_ADD_ACLOCAL(val)           (((val) >> 14) & 3)
#define FBZCP_CC_INVERT_OUTPUT(val)         (((val) >> 16) & 1)
#define FBZCP_CCA_ZERO_OTHER(val)           (((val) >> 17) & 1)
#define FBZCP_CCA_SUB_CLOCAL(val)           (((val) >> 18) & 1)
#define FBZCP_CCA_MSELECT(val)              (((val) >> 19) & 7)
#define FBZCP_CCA_REVERSE_BLEND(val)        (((val) >> 22) & 1)
#define FBZCP_CCA_ADD_ACLOCAL(val)          (((val) >> 23) & 3)
#define FBZCP_CCA_INVERT_OUTPUT(val)        (((val) >> 25) & 1)
#define FBZCP_CCA_SUBPIXEL_ADJUST(val)      (((val) >> 26) & 1)
#define FBZCP_TEXTURE_ENABLE(val)           (((val) >> 27) & 1)
#define FBZCP_RGBZW_CLAMP(val)              (((val) >> 28) & 1)
#define FBZCP_ANTI_ALIAS(val)               (((val) >> 29) & 1)

/* Bit positions and masks for setting register values */
#define FBZCP_CC_RGBSELECT_SHIFT        0
#define FBZCP_CC_RGBSELECT_MASK         (0x3 << 0)
#define FBZCP_CC_ASELECT_SHIFT          2
#define FBZCP_CC_ASELECT_MASK           (0x3 << 2)
#define FBZCP_CC_LOCALSELECT_SHIFT      4
#define FBZCP_CC_LOCALSELECT_BIT        (1 << 4)
#define FBZCP_CCA_LOCALSELECT_SHIFT     5
#define FBZCP_CCA_LOCALSELECT_MASK      (0x3 << 5)
#define FBZCP_CC_ZERO_OTHER_BIT         (1 << 8)
#define FBZCP_CC_SUB_CLOCAL_BIT         (1 << 9)
#define FBZCP_CC_MSELECT_SHIFT          10
#define FBZCP_CC_MSELECT_MASK           (0x7 << 10)
#define FBZCP_CC_REVERSE_BLEND_BIT      (1 << 13)
#define FBZCP_CC_ADD_CLOCAL_BIT         (1 << 14)
#define FBZCP_CC_ADD_ALOCAL_BIT         (1 << 15)
#define FBZCP_CC_INVERT_OUTPUT_BIT      (1 << 16)
#define FBZCP_CCA_ZERO_OTHER_BIT        (1 << 17)
#define FBZCP_CCA_SUB_CLOCAL_BIT        (1 << 18)
#define FBZCP_CCA_MSELECT_SHIFT         19
#define FBZCP_CCA_MSELECT_MASK          (0x7 << 19)
#define FBZCP_CCA_REVERSE_BLEND_BIT     (1 << 22)
#define FBZCP_CCA_ADD_CLOCAL_BIT        (1 << 23)
#define FBZCP_CCA_ADD_ALOCAL_BIT        (1 << 24)
#define FBZCP_CCA_INVERT_OUTPUT_BIT     (1 << 25)
#define FBZCP_TEXTURE_ENABLE_BIT        (1 << 27)

/* Masks for clearing color combine / alpha combine sections */
#define FBZCP_CC_BITS_MASK              0x1FFFF       /* Bits 0-16: color combine */
#define FBZCP_CCA_BITS_MASK             ((0x3 << 2) | (0x3 << 5) | (0x1FF << 17))

/*************************************
 * ALPHAMODE register bits
 *************************************/

/* Getter macros */
#define ALPHAMODE_ALPHATEST(val)            (((val) >> 0) & 1)
#define ALPHAMODE_ALPHAFUNCTION(val)        (((val) >> 1) & 7)
#define ALPHAMODE_ALPHABLEND(val)           (((val) >> 4) & 1)
#define ALPHAMODE_ANTIALIAS(val)            (((val) >> 5) & 1)
#define ALPHAMODE_SRCRGBBLEND(val)          (((val) >> 8) & 15)
#define ALPHAMODE_DSTRGBBLEND(val)          (((val) >> 12) & 15)
#define ALPHAMODE_SRCALPHABLEND(val)        (((val) >> 16) & 15)
#define ALPHAMODE_DSTALPHABLEND(val)        (((val) >> 20) & 15)
#define ALPHAMODE_ALPHAREF(val)             (((val) >> 24) & 0xff)

/* Bit positions and masks for setting register values */
#define ALPHAMODE_ALPHATEST_BIT             (1 << 0)
#define ALPHAMODE_ALPHAFUNCTION_SHIFT       1
#define ALPHAMODE_ALPHAFUNCTION_MASK        (0x7 << 1)
#define ALPHAMODE_ALPHABLEND_BIT            (1 << 4)
#define ALPHAMODE_SRCRGBBLEND_SHIFT         8
#define ALPHAMODE_DSTRGBBLEND_SHIFT         12
#define ALPHAMODE_SRCALPHABLEND_SHIFT       16
#define ALPHAMODE_DSTALPHABLEND_SHIFT       20
#define ALPHAMODE_ALPHAREF_SHIFT            24
#define ALPHAMODE_ALPHAREF_MASK             (0xFF << 24)
#define ALPHAMODE_BLEND_BITS_MASK           0x00FFFFF0  /* Bits 4-23: all blend settings */

/*************************************
 * FOGMODE register bits
 *************************************/

#define FOGMODE_ENABLE_FOG(val)             (((val) >> 0) & 1)
#define FOGMODE_FOG_ADD(val)                (((val) >> 1) & 1)
#define FOGMODE_FOG_MULT(val)               (((val) >> 2) & 1)
#define FOGMODE_FOG_ZALPHA(val)             (((val) >> 3) & 3)
#define FOGMODE_FOG_CONSTANT(val)           (((val) >> 5) & 1)
#define FOGMODE_FOG_DITHER(val)             (((val) >> 6) & 1)
#define FOGMODE_FOG_ZONES(val)              (((val) >> 7) & 1)

/*************************************
 * FBZMODE register bits
 *************************************/

/* Getter macros */
#define FBZMODE_ENABLE_CLIPPING(val)        (((val) >> 0) & 1)
#define FBZMODE_ENABLE_CHROMAKEY(val)       (((val) >> 1) & 1)
#define FBZMODE_ENABLE_STIPPLE(val)         (((val) >> 2) & 1)
#define FBZMODE_WBUFFER_SELECT(val)         (((val) >> 3) & 1)
#define FBZMODE_ENABLE_DEPTHBUF(val)        (((val) >> 4) & 1)
#define FBZMODE_DEPTH_FUNCTION(val)         (((val) >> 5) & 7)
#define FBZMODE_ENABLE_DITHERING(val)       (((val) >> 8) & 1)
#define FBZMODE_RGB_BUFFER_MASK(val)        (((val) >> 9) & 1)
#define FBZMODE_AUX_BUFFER_MASK(val)        (((val) >> 10) & 1)
#define FBZMODE_DITHER_TYPE(val)            (((val) >> 11) & 1)
#define FBZMODE_STIPPLE_PATTERN(val)        (((val) >> 12) & 1)
#define FBZMODE_ENABLE_ALPHA_MASK(val)      (((val) >> 13) & 1)
#define FBZMODE_DRAW_BUFFER(val)            (((val) >> 14) & 3)
#define FBZMODE_ENABLE_DEPTH_BIAS(val)      (((val) >> 16) & 1)
#define FBZMODE_Y_ORIGIN(val)               (((val) >> 17) & 1)
#define FBZMODE_ENABLE_ALPHA_PLANES(val)    (((val) >> 18) & 1)
#define FBZMODE_ALPHA_DITHER_SUBTRACT(val)  (((val) >> 19) & 1)
#define FBZMODE_DEPTH_SOURCE_COMPARE(val)   (((val) >> 20) & 1)
#define FBZMODE_DEPTH_FLOAT_SELECT(val)     (((val) >> 21) & 1)

/* Bit positions and masks for setting register values */
#define FBZMODE_ENABLE_CLIPPING_BIT         (1 << 0)
#define FBZMODE_ENABLE_CHROMAKEY_BIT        (1 << 1)
#define FBZMODE_ENABLE_STIPPLE_BIT          (1 << 2)
#define FBZMODE_WBUFFER_SELECT_BIT          (1 << 3)
#define FBZMODE_ENABLE_DEPTHBUF_BIT         (1 << 4)
#define FBZMODE_DEPTH_FUNCTION_SHIFT        5
#define FBZMODE_DEPTH_FUNCTION_MASK         (0x7 << 5)
#define FBZMODE_ENABLE_DITHERING_BIT        (1 << 8)
#define FBZMODE_RGB_BUFFER_MASK_BIT         (1 << 9)
#define FBZMODE_AUX_BUFFER_MASK_BIT         (1 << 10)
#define FBZMODE_DITHER_TYPE_BIT             (1 << 11)
#define FBZMODE_STIPPLE_PATTERN_BIT         (1 << 12)
#define FBZMODE_ENABLE_ALPHA_MASK_BIT       (1 << 13)
#define FBZMODE_DRAW_BUFFER_SHIFT           14
#define FBZMODE_DRAW_BUFFER_MASK            (0x3 << 14)
#define FBZMODE_ENABLE_DEPTH_BIAS_BIT       (1 << 16)
#define FBZMODE_Y_ORIGIN_BIT                (1 << 17)
#define FBZMODE_ENABLE_ALPHA_PLANES_BIT     (1 << 18)
#define FBZMODE_ALPHA_DITHER_SUBTRACT_BIT   (1 << 19)
#define FBZMODE_DEPTH_SOURCE_COMPARE_BIT    (1 << 20)
#define FBZMODE_DEPTH_FLOAT_SELECT_BIT      (1 << 21)

/*************************************
 * LFBMODE register bits
 *************************************/

#define LFBMODE_WRITE_FORMAT(val)           (((val) >> 0) & 0xf)
#define LFBMODE_WRITE_BUFFER_SELECT(val)    (((val) >> 4) & 3)
#define LFBMODE_READ_BUFFER_SELECT(val)     (((val) >> 6) & 3)
#define LFBMODE_ENABLE_PIXEL_PIPELINE(val)  (((val) >> 8) & 1)
#define LFBMODE_RGBA_LANES(val)             (((val) >> 9) & 3)
#define LFBMODE_WORD_SWAP_WRITES(val)       (((val) >> 11) & 1)
#define LFBMODE_BYTE_SWIZZLE_WRITES(val)    (((val) >> 12) & 1)
#define LFBMODE_Y_ORIGIN(val)               (((val) >> 13) & 1)
#define LFBMODE_WRITE_W_SELECT(val)         (((val) >> 14) & 1)
#define LFBMODE_WORD_SWAP_READS(val)        (((val) >> 15) & 1)
#define LFBMODE_BYTE_SWIZZLE_READS(val)     (((val) >> 16) & 1)

/*************************************
 * CHROMARANGE register bits
 *************************************/

#define CHROMARANGE_BLUE_EXCLUSIVE(val)     (((val) >> 24) & 1)
#define CHROMARANGE_GREEN_EXCLUSIVE(val)    (((val) >> 25) & 1)
#define CHROMARANGE_RED_EXCLUSIVE(val)      (((val) >> 26) & 1)
#define CHROMARANGE_UNION_MODE(val)         (((val) >> 27) & 1)
#define CHROMARANGE_ENABLE(val)             (((val) >> 28) & 1)

/*************************************
 * FBIINIT0-7 register bits
 *************************************/

#define FBIINIT0_VGA_PASSTHRU(val)          (((val) >> 0) & 1)
#define FBIINIT0_GRAPHICS_RESET(val)        (((val) >> 1) & 1)
#define FBIINIT0_FIFO_RESET(val)            (((val) >> 2) & 1)

#define FBIINIT1_VIDEO_TIMING_RESET(val)    (((val) >> 8) & 1)
#define FBIINIT1_SOFTWARE_OVERRIDE(val)     (((val) >> 9) & 1)
#define FBIINIT1_SOFTWARE_HSYNC(val)        (((val) >> 10) & 1)
#define FBIINIT1_SOFTWARE_VSYNC(val)        (((val) >> 11) & 1)
#define FBIINIT1_SOFTWARE_BLANK(val)        (((val) >> 12) & 1)

#define FBIINIT2_SWAP_BUFFER_ALGORITHM(val) (((val) >> 9) & 3)
#define FBIINIT2_VIDEO_BUFFER_OFFSET(val)   (((val) >> 11) & 0x1ff)

#define FBIINIT3_TRI_REGISTER_REMAP(val)    (((val) >> 0) & 1)
#define FBIINIT3_DISABLE_TMUS(val)          (((val) >> 6) & 1)
#define FBIINIT3_YORIGIN_SUBTRACT(val)      (((val) >> 22) & 0x3ff)

#define FBIINIT5_BUFFER_ALLOCATION(val)     (((val) >> 9) & 3)

/*************************************
 * TEXTUREMODE register bits
 *************************************/

#define TEXMODE_ENABLE_PERSPECTIVE(val)     (((val) >> 0) & 1)
#define TEXMODE_MINIFICATION_FILTER(val)    (((val) >> 1) & 1)
#define TEXMODE_MAGNIFICATION_FILTER(val)   (((val) >> 2) & 1)
#define TEXMODE_CLAMP_NEG_W(val)            (((val) >> 3) & 1)
#define TEXMODE_ENABLE_LOD_DITHER(val)      (((val) >> 4) & 1)
#define TEXMODE_NCC_TABLE_SELECT(val)       (((val) >> 5) & 1)
#define TEXMODE_CLAMP_S(val)                (((val) >> 6) & 1)
#define TEXMODE_CLAMP_T(val)                (((val) >> 7) & 1)
#define TEXMODE_FORMAT(val)                 (((val) >> 8) & 0xf)
#define TEXMODE_TC_ZERO_OTHER(val)          (((val) >> 12) & 1)
#define TEXMODE_TC_SUB_CLOCAL(val)          (((val) >> 13) & 1)
#define TEXMODE_TC_MSELECT(val)             (((val) >> 14) & 7)
#define TEXMODE_TC_REVERSE_BLEND(val)       (((val) >> 17) & 1)
#define TEXMODE_TC_ADD_ACLOCAL(val)         (((val) >> 18) & 3)
#define TEXMODE_TC_INVERT_OUTPUT(val)       (((val) >> 20) & 1)
#define TEXMODE_TCA_ZERO_OTHER(val)         (((val) >> 21) & 1)
#define TEXMODE_TCA_SUB_CLOCAL(val)         (((val) >> 22) & 1)
#define TEXMODE_TCA_MSELECT(val)            (((val) >> 23) & 7)
#define TEXMODE_TCA_REVERSE_BLEND(val)      (((val) >> 26) & 1)
#define TEXMODE_TCA_ADD_ACLOCAL(val)        (((val) >> 27) & 3)
#define TEXMODE_TCA_INVERT_OUTPUT(val)      (((val) >> 29) & 1)
#define TEXMODE_TRILINEAR(val)              (((val) >> 30) & 1)
#define TEXMODE_SEQ_8_DOWNLD(val)           (((val) >> 31) & 1)

/* Bit positions and masks for setting register values */
#define TEXMODE_MINIFICATION_FILTER_BIT     (1 << 1)
#define TEXMODE_MAGNIFICATION_FILTER_BIT    (1 << 2)
#define TEXMODE_CLAMP_S_BIT                 (1 << 6)
#define TEXMODE_CLAMP_T_BIT                 (1 << 7)
#define TEXMODE_FORMAT_SHIFT                8
#define TEXMODE_FORMAT_MASK                 (0xF << 8)
#define TEXMODE_TC_ZERO_OTHER_BIT           (1 << 12)
#define TEXMODE_TC_SUB_CLOCAL_BIT           (1 << 13)
#define TEXMODE_TC_MSELECT_SHIFT            14
#define TEXMODE_TC_MSELECT_MASK             (0x7 << 14)
#define TEXMODE_TC_REVERSE_BLEND_BIT        (1 << 17)
#define TEXMODE_TC_ADD_CLOCAL_BIT           (1 << 18)
#define TEXMODE_TC_ADD_ALOCAL_BIT           (1 << 19)
#define TEXMODE_TC_INVERT_OUTPUT_BIT        (1 << 20)
#define TEXMODE_TCA_ZERO_OTHER_BIT          (1 << 21)
#define TEXMODE_TCA_SUB_CLOCAL_BIT          (1 << 22)
#define TEXMODE_TCA_MSELECT_SHIFT           23
#define TEXMODE_TCA_MSELECT_MASK            (0x7 << 23)
#define TEXMODE_TCA_REVERSE_BLEND_BIT       (1 << 26)
#define TEXMODE_TCA_ADD_CLOCAL_BIT          (1 << 27)
#define TEXMODE_TCA_ADD_ALOCAL_BIT          (1 << 28)
#define TEXMODE_TCA_INVERT_OUTPUT_BIT       (1 << 29)
/* Mask for clearing all texture combine bits (RGB + Alpha) */
#define TEXMODE_TC_BITS_MASK                (0x1FF << 12)
#define TEXMODE_TCA_BITS_MASK               (0x1FF << 21)
/* Combined mask to clear both filter bits */
#define TEXMODE_FILTER_MASK                 ((1 << 1) | (1 << 2))

/*************************************
 * TEXLOD register bits
 *************************************/

#define TEXLOD_LODMIN(val)                  (((val) >> 0) & 0x3f)
#define TEXLOD_LODMAX(val)                  (((val) >> 6) & 0x3f)
#define TEXLOD_LODBIAS(val)                 (((val) >> 12) & 0x3f)
#define TEXLOD_LOD_ODD(val)                 (((val) >> 18) & 1)
#define TEXLOD_LOD_TSPLIT(val)              (((val) >> 19) & 1)
#define TEXLOD_LOD_S_IS_WIDER(val)          (((val) >> 20) & 1)
#define TEXLOD_LOD_ASPECT(val)              (((val) >> 21) & 3)
#define TEXLOD_LOD_ZEROFRAC(val)            (((val) >> 23) & 1)
#define TEXLOD_TMULTIBASEADDR(val)          (((val) >> 24) & 1)
#define TEXLOD_TDATA_SWIZZLE(val)           (((val) >> 25) & 1)
#define TEXLOD_TDATA_SWAP(val)              (((val) >> 26) & 1)
#define TEXLOD_TDIRECT_WRITE(val)           (((val) >> 27) & 1)

/*************************************
 * TEXDETAIL register bits
 *************************************/

#define TEXDETAIL_DETAIL_MAX(val)           (((val) >> 0) & 0xff)
#define TEXDETAIL_DETAIL_BIAS(val)          (((val) >> 8) & 0x3f)
#define TEXDETAIL_DETAIL_SCALE(val)         (((val) >> 14) & 7)
#define TEXDETAIL_RGB_MIN_FILTER(val)       (((val) >> 17) & 1)
#define TEXDETAIL_RGB_MAG_FILTER(val)       (((val) >> 18) & 1)
#define TEXDETAIL_ALPHA_MIN_FILTER(val)     (((val) >> 19) & 1)
#define TEXDETAIL_ALPHA_MAG_FILTER(val)     (((val) >> 20) & 1)
#define TEXDETAIL_SEPARATE_RGBA_FILTER(val) (((val) >> 21) & 1)

/*************************************
 * TREXINIT register bits
 *************************************/

#define TREXINIT_SEND_TMU_CONFIG(val)       (((val) >> 18) & 1)

/*************************************
 * Register indices
 *************************************/

typedef enum {
    status       = 0x000/4,
    intrCtrl     = 0x004/4,
    vertexAx     = 0x008/4,
    vertexAy     = 0x00c/4,
    vertexBx     = 0x010/4,
    vertexBy     = 0x014/4,
    vertexCx     = 0x018/4,
    vertexCy     = 0x01c/4,
    startR       = 0x020/4,
    startG       = 0x024/4,
    startB       = 0x028/4,
    startZ       = 0x02c/4,
    startA       = 0x030/4,
    startS       = 0x034/4,
    startT       = 0x038/4,
    startW       = 0x03c/4,
    dRdX         = 0x040/4,
    dGdX         = 0x044/4,
    dBdX         = 0x048/4,
    dZdX         = 0x04c/4,
    dAdX         = 0x050/4,
    dSdX         = 0x054/4,
    dTdX         = 0x058/4,
    dWdX         = 0x05c/4,
    dRdY         = 0x060/4,
    dGdY         = 0x064/4,
    dBdY         = 0x068/4,
    dZdY         = 0x06c/4,
    dAdY         = 0x070/4,
    dSdY         = 0x074/4,
    dTdY         = 0x078/4,
    dWdY         = 0x07c/4,
    triangleCMD  = 0x080/4,
    fvertexAx    = 0x088/4,
    fvertexAy    = 0x08c/4,
    fvertexBx    = 0x090/4,
    fvertexBy    = 0x094/4,
    fvertexCx    = 0x098/4,
    fvertexCy    = 0x09c/4,
    fstartR      = 0x0a0/4,
    fstartG      = 0x0a4/4,
    fstartB      = 0x0a8/4,
    fstartZ      = 0x0ac/4,
    fstartA      = 0x0b0/4,
    fstartS      = 0x0b4/4,
    fstartT      = 0x0b8/4,
    fstartW      = 0x0bc/4,
    fdRdX        = 0x0c0/4,
    fdGdX        = 0x0c4/4,
    fdBdX        = 0x0c8/4,
    fdZdX        = 0x0cc/4,
    fdAdX        = 0x0d0/4,
    fdSdX        = 0x0d4/4,
    fdTdX        = 0x0d8/4,
    fdWdX        = 0x0dc/4,
    fdRdY        = 0x0e0/4,
    fdGdY        = 0x0e4/4,
    fdBdY        = 0x0e8/4,
    fdZdY        = 0x0ec/4,
    fdAdY        = 0x0f0/4,
    fdSdY        = 0x0f4/4,
    fdTdY        = 0x0f8/4,
    fdWdY        = 0x0fc/4,
    ftriangleCMD = 0x100/4,
    fbzColorPath = 0x104/4,
    fogMode      = 0x108/4,
    alphaMode    = 0x10c/4,
    fbzMode      = 0x110/4,
    lfbMode      = 0x114/4,
    clipLeftRight= 0x118/4,
    clipLowYHighY= 0x11c/4,
    nopCMD       = 0x120/4,
    fastfillCMD  = 0x124/4,
    swapbufferCMD= 0x128/4,
    fogColor     = 0x12c/4,
    zaColor      = 0x130/4,
    chromaKey    = 0x134/4,
    chromaRange  = 0x138/4,
    userIntrCMD  = 0x13c/4,
    stipple      = 0x140/4,
    color0       = 0x144/4,
    color1       = 0x148/4,
    fbiPixelsIn  = 0x14c/4,
    fbiChromaFail= 0x150/4,
    fbiZfuncFail = 0x154/4,
    fbiAfuncFail = 0x158/4,
    fbiPixelsOut = 0x15c/4,
    fogTable     = 0x160/4,
    /* ... more registers ... */
    fbiInit4     = 0x200/4,
    vRetrace     = 0x204/4,
    backPorch    = 0x208/4,
    videoDimensions = 0x20c/4,
    fbiInit0     = 0x210/4,
    fbiInit1     = 0x214/4,
    fbiInit2     = 0x218/4,
    fbiInit3     = 0x21c/4,
    hSync        = 0x220/4,
    vSync        = 0x224/4,
    clutData     = 0x228/4,
    dacData      = 0x22c/4,
    maxRgbDelta  = 0x230/4,
    hvRetrace    = 0x240/4,
    fbiInit5     = 0x244/4,
    fbiInit6     = 0x248/4,
    fbiInit7     = 0x24c/4,
    fbiSwapHistory = 0x258/4,
    fbiTrianglesOut = 0x25c/4,
    sSetupMode   = 0x260/4,
    sVx          = 0x264/4,
    sVy          = 0x268/4,
    sARGB        = 0x26c/4,
    sRed         = 0x270/4,
    sGreen       = 0x274/4,
    sBlue        = 0x278/4,
    sAlpha       = 0x27c/4,
    sVz          = 0x280/4,
    sWb          = 0x284/4,
    sW0          = 0x288/4,
    sWtmu0       = 0x288/4,  /* alias for sW0 */
    sS0          = 0x28c/4,
    sS_W0        = 0x28c/4,  /* alias for sS0 */
    sT0          = 0x290/4,
    sT_W0        = 0x290/4,  /* alias for sT0 */
    sW1          = 0x294/4,
    sWtmu1       = 0x294/4,  /* alias for sW1 */
    sS1          = 0x298/4,
    sS_Wtmu1     = 0x298/4,  /* alias for sS1 */
    sT1          = 0x29c/4,
    sT_Wtmu1     = 0x29c/4,  /* alias for sT1 */
    sDrawTriCMD  = 0x2a0/4,
    sBeginTriCMD = 0x2a4/4,
    /* TMU registers start at 0x300 */
    textureMode  = 0x300/4,
    tLOD         = 0x304/4,
    tDetail      = 0x308/4,
    texBaseAddr  = 0x30c/4,
    texBaseAddr_1= 0x310/4,
    texBaseAddr_2= 0x314/4,
    texBaseAddr_3_8= 0x318/4,
    trexInit0    = 0x31c/4,
    trexInit1    = 0x320/4,
    nccTable     = 0x324/4,
} voodoo_reg_t;

#endif /* VOODOO_DEFS_H */
