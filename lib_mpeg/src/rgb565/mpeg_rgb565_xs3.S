// Copyright 2026 XMOS LIMITED.
// This Software is subject to the terms of the XMOS Public Licence: Version 1.

#include "mpeg_config.h"
#if defined(__XS3A__) && MPEG_RGB565_VPU

/* Exact limited-range BT.601, 16 pixels in each of two rows. Coefficients are
 * decomposed to signed 16-bit multipliers without changing their Q16 values:
 * Y: 65536+10773; R: 2*65536-26475; G: 25674*U+(65536-12258)*V;
 * B: 2*65536+1129. Seed each accumulator with -32768 to cancel VLSAT's
 * rounding before shifting by 16: the result is the scalar arithmetic floor.
 * Channel quantisation commutes with clipping, so clamp directly to 5/6 bits.
 * Final MACCs build byte-swapped RGB565 in the raw low 16 accumulator bits;
 * VSTR deliberately bypasses signed saturation (pixels can exceed 32767).
 */
.text
.issue_mode dual
.align 4

#define FUNC mpeg_rgb565_vpu_block
#define NSTACK 64
#define U 8
#define V 16
#define DR 24
#define DG 32
#define DB 40
#define Y 48
/* U/V are dead after chroma setup; Y is dead once green has been formed. */
#define R U
#define G V
#define B 56
#define GHI Y
#define GLO G

#define input r0
#define out0 r1
#define out1 r2
#define rows r5
#define source r6
#define destination r7
#define shift3 r9
#define shift2 r10

#define C_ONES8 0
#define C_Y_OFFSET 1
#define C_Y_FRAC 2
#define C_R_FRAC 3
#define C_G_U 4
#define C_G_V_FRAC 5
#define C_B_FRAC 6
#define C_ONE 7
#define C_ACC_HIGH 8
#define C_ACC_LOW 9
#define C_SHIFT16 10
#define C_CLIP5 11
#define C_CLIP6 12
#define C_PACK_R 13
#define C_PACK_G 14
#define C_PACK_B 15
#define C_UNCLIP5 16
#define C_UNCLIP6 17

/* Read through the old r11 while the scalar slot prepares the next operand. */
#define NEXT_C(OP, K) { OP r11[0]; ldap r11, .L_constants+(K)*32 }
#define NEXT_S(OP, K) { OP r11[0]; ldaw r11, sp[K] }

.globl FUNC
.type FUNC,@function
.cc_top FUNC.function,FUNC
FUNC:
    dualentsp NSTACK
    std r4, r5, sp[0]
    std r6, r7, sp[1]
    std r8, r9, sp[2]
    stw r10, sp[6]
    ldc r11, 0x200
    vsetc r11
    ldap r11, .L_constants+C_ONES8*32
    { vclrdr; ldc r3, 16 }
    vldc r11[0]
    ldaw r11, input[r3]
    NEXT_S(vlmacc, U)
    { vstr r11[0]; ldc r3, 24 }
    vclrdr
    ldaw r11, input[r3]
    NEXT_S(vlmacc, V)
    vstr r11[0]
    ldc r11, 0x100
    vsetc r11

    /* Chroma offsets, shared by both luma rows. */
    ldap r11, .L_constants+C_ACC_HIGH*32
    NEXT_C(vldd, C_ACC_LOW)
    NEXT_C(vldr, C_R_FRAC)
    NEXT_S(vldc, V)
    NEXT_C(vlmacc, C_SHIFT16)
    NEXT_S(vlsat, V)
    NEXT_S(vladd, V)
    NEXT_S(vladd, DR)
    NEXT_C(vstr, C_ACC_HIGH)

    NEXT_C(vldd, C_ACC_LOW)
    NEXT_C(vldr, C_G_U)
    NEXT_S(vldc, U)
    NEXT_C(vlmacc, C_G_V_FRAC)
    NEXT_S(vldc, V)
    NEXT_C(vlmacc, C_SHIFT16)
    NEXT_S(vlsat, V)
    NEXT_S(vladd, DG)
    NEXT_C(vstr, C_ACC_HIGH)

    NEXT_C(vldd, C_ACC_LOW)
    NEXT_C(vldr, C_B_FRAC)
    NEXT_S(vldc, U)
    NEXT_C(vlmacc, C_SHIFT16)
    NEXT_S(vlsat, U)
    NEXT_S(vladd, U)
    NEXT_S(vladd, DB)
    vstr r11[0]

    mov source, input
    mov destination, out0
    ldaw r8, sp[R]
    ldaw r3, sp[G]
    ldaw r4, sp[B]
    ldaw r1, sp[GHI]
    ldc rows, 2
    ldc shift3, 3
    ldc shift2, 2
    neg r0, shift3
.L_row:
    /* Widen recentered luma, restore Y-16, then apply the exact Q16 scale. */
    ldc r11, 0x200
    vclrdr
    vsetc r11
    ldap r11, .L_constants+C_ONES8*32
    vldc r11[0]
    ldc r11, 0x100
    vlmacc source[0]
    { vsetc r11; ldap r11, .L_constants+C_Y_OFFSET*32 }
    NEXT_S(vladd, Y)
    NEXT_C(vstr, C_ACC_HIGH)
    NEXT_C(vldd, C_ACC_LOW)
    NEXT_C(vldr, C_Y_FRAC)
    NEXT_S(vldc, Y)
    NEXT_C(vlmacc, C_SHIFT16)
    NEXT_S(vlsat, Y)
    NEXT_S(vladd, Y)
    NEXT_S(vstr, DR)

    NEXT_S(vladd, R)
    /* Saturating add/subtract around 32767 clips the quantised channel to
     * its positive maximum; VPOS provides the lower bound of zero. */
    NEXT_C(vstr, C_CLIP5)
    vlashr r8[0], shift3
    NEXT_C(vladd, C_UNCLIP5)
    NEXT_S(vladd, Y)
    vpos
    vstr r8[0]

    NEXT_S(vldr, DB)
    NEXT_S(vladd, B)
    NEXT_C(vstr, C_CLIP5)
    vlashr r4[0], shift3
    NEXT_C(vladd, C_UNCLIP5)
    NEXT_S(vladd, DG)
    vpos
    vstr r4[0]

    /* VLSUB computes memory minus vR, hence DG must be loaded before Y. */
    NEXT_S(vldr, Y)
    NEXT_S(vlsub, G)
    NEXT_C(vstr, C_CLIP6)
    vlashr r3[0], shift2
    NEXT_C(vladd, C_UNCLIP6)
    NEXT_S(vladd, GHI)
    vpos
    vstr r3[0]
    vlashr r3[0], shift3
    NEXT_S(vstr, G)
    vlashr r1[0], r0
    NEXT_S(vlsub, GLO)
    /* Byte-swapped pixel = R5*8 + (G6>>3) + (G6&7)*8192 + B5*256.
     * Store accumulator low halves directly; do not saturate to signed 16. */
    NEXT_C(vstr, C_PACK_R)
    vclrdr
    NEXT_S(vldc, R)
    NEXT_C(vlmacc, C_ONE)
    NEXT_S(vldc, GHI)
    NEXT_C(vlmacc, C_PACK_G)
    NEXT_S(vldc, GLO)
    NEXT_C(vlmacc, C_PACK_B)
    NEXT_S(vldc, B)
    { vlmacc r11[0]; ldc r11, 32 }
    { vstr destination[0]; add source, source, r11 }
    { mov destination, out1; sub rows, rows, 1 }
    bt rows, .L_row
    ldw r10, sp[6]
    ldd r8, r9, sp[2]
    ldd r6, r7, sp[1]
    ldd r4, r5, sp[0]
    retsp NSTACK

.align 8
#define V16(X) .short X,X,X,X,X,X,X,X,X,X,X,X,X,X,X,X
.L_constants:
    .byte 1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1
    V16(112)
    V16(10773)
    V16(-26475)
    V16(25674)
    V16(-12258)
    V16(1129)
    V16(1)
    V16(-1)
    V16(-32768)
    V16(16)
    V16(32736)
    V16(32704)
    V16(8)
    V16(8192)
    V16(256)
    V16(-32736)
    V16(-32704)
.L_end:
.cc_bottom FUNC.function
.set FUNC.nstackwords,NSTACK; .global FUNC.nstackwords
.set FUNC.maxcores,1; .global FUNC.maxcores
.set FUNC.maxtimers,0; .global FUNC.maxtimers
.set FUNC.maxchanends,0; .global FUNC.maxchanends
.size FUNC,.L_end-FUNC
#endif
