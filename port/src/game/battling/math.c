/*
 * Battling's small GTE and memory helpers (hand-written assembly at
 * 0x80073064 and 0x8008DDFC in the PS1 build; config/handwritten.yaml). PC
 * versions that drive the GTE the same way, through include/gte.h.
 */
#include "common.h"
#include "psyq.h"
#include "gte.h"

#define GTE_IR0 8
#define GTE_IR1 9
#define GTE_IR2 10
#define GTE_IR3 11
#define GTE_IRGB 28
#define GTE_ORGB 29
#define GTE_MAC1 25
#define GTE_MAC2 26
#define GTE_MAC3 27

/* GPF of (x, y, z) by scale: IRn = clamp((scale * n) >> (sf * 12)). */
static void scale3(s32 x, s32 y, s32 z, s32 scale, s32 sf, SVECTOR* out) {
    gteMtc2(GTE_IR0, scale);
    gteMtc2(GTE_IR1, x);
    gteMtc2(GTE_IR2, y);
    gteMtc2(GTE_IR3, z);
    if (sf) {
        gte_gpf12();
    } else {
        gte_gpf0();
    }
    out->vx = gteMfc2(GTE_IR1);
    out->vy = gteMfc2(GTE_IR2);
    out->vz = gteMfc2(GTE_IR3);
}

/* out = v * scale / 4096 (saturated to s16). */
void scaleBattlingSVector(SVECTOR* v, SVECTOR* out, s32 scale) {
    scale3(v->vx, v->vy, v->vz, scale, 1, out);
}

/* out = v * scale (saturated to s16). */
void func_800730AC(SVECTOR* v, SVECTOR* out, s32 scale) {
    scale3(v->vx, v->vy, v->vz, scale, 0, out);
}

/* out = (the low halves of a VECTOR's x, y, z) * scale / 4096. */
void func_800730F4(VECTOR* v, SVECTOR* out, s32 scale) {
    scale3((s16)v->vx, (s16)v->vy, (s16)v->vz, scale, 1, out);
}

/* out = (RT * v) * scale / 4096: the vector rotated by the GTE rotation matrix
 * (MVMVA, sf=1, no translation), then scaled (GPF). The MAC results go back
 * into IR1-3 as 16-bit values. */
void rotateScaleBattlingSVector(SVECTOR* v, SVECTOR* out, s32 scale) {
    u32 x;
    u32 y;
    u32 z;

    gte_ldv0(v);
    gteMtc2(GTE_IR0, scale);
    gte_rtv0();
    x = gteMfc2(GTE_MAC1);
    y = gteMfc2(GTE_MAC2);
    z = gteMfc2(GTE_MAC3);
    gteMtc2(GTE_IR1, x);
    gteMtc2(GTE_IR2, y);
    gteMtc2(GTE_IR3, z);
    gte_gpf12();
    out->vx = gteMfc2(GTE_IR1);
    out->vy = gteMfc2(GTE_IR2);
    out->vz = gteMfc2(GTE_IR3);
}

/* In place over 320-pixel rows, from p up to end: each pixel becomes the
 * average of itself, its right neighbour and the two below them, per 5-bit
 * channel (through the GTE's IRGB/ORGB conversions; bit 15 is dropped). The
 * pixels a store overwrites have all been read already. */
void blurBattlingImage(u16* p, u16* end) {
    s32 r0, g0, b0; /* the top pixel, as IR1-3 */
    s32 r1, g1, b1; /* the bottom pixel */
    s32 r, g, b;
    u32 top;
    u32 bottom;

    gteMtc2(GTE_IRGB, p[0]);
    r0 = gteMfc2(GTE_IR1);
    g0 = gteMfc2(GTE_IR2);
    b0 = gteMfc2(GTE_IR3);
    gteMtc2(GTE_IRGB, p[0x140]);
    r1 = gteMfc2(GTE_IR1);
    g1 = gteMfc2(GTE_IR2);
    b1 = gteMfc2(GTE_IR3);
    p++;
    top = p[0];
    bottom = p[0x140];
    do {
        gteMtc2(GTE_IRGB, top);
        r = r0;
        g = g0;
        b = b0;
        r0 = gteMfc2(GTE_IR1);
        g0 = gteMfc2(GTE_IR2);
        b0 = gteMfc2(GTE_IR3);
        r += r0 + r1;
        g += g0 + g1;
        b += b0 + b1;
        gteMtc2(GTE_IRGB, bottom);
        r1 = gteMfc2(GTE_IR1);
        g1 = gteMfc2(GTE_IR2);
        b1 = gteMfc2(GTE_IR3);
        gteMtc2(GTE_IR1, (r + r1) >> 2);
        gteMtc2(GTE_IR2, (g + g1) >> 2);
        gteMtc2(GTE_IR3, (b + b1) >> 2);
        p++;
        if (p != end) { /* the PS1 also reads the pair after the last one */
            top = p[0];
            bottom = p[0x140];
        }
        p[-2] = gteMfc2(GTE_ORGB);
    } while (p != end);
}

/* Scales the matrix's columns by scale's x, y and z (/4096, saturated): the
 * matrix becomes the GTE rotation matrix and each column is RT times a
 * one-axis vector (MVMVA, sf=1). */
void scaleBattlingMatrixBySVector(MATRIX* m, s16* scale) {
    u32* w = (u32*)m;
    s32 i;
    s32 col;
    s16 c[3];

    gteCtc2(0, w[0]);
    gteCtc2(1, w[1]);
    gteCtc2(2, w[2]);
    gteCtc2(3, w[3]);
    gteCtc2(4, w[4]);
    for (col = 0; col < 3; col++) {
        for (i = 0; i < 3; i++) {
            gteMtc2(GTE_IR1 + i, i == col ? (u16)scale[col] : 0);
        }
        gte_rtir();
        for (i = 0; i < 3; i++) {
            c[i] = gteMfc2(GTE_IR1 + i);
        }
        for (i = 0; i < 3; i++) {
            m->m[i][col] = c[i];
        }
    }
}

/* Copies size bytes (a positive multiple of 4) a word at a time. */
void copyBattlingWords(void* dst, void* src, s32 size) {
    u32* d = (u32*)dst;
    u32* s = (u32*)src;
    u32* end = (u32*)((u8*)src + size);

    do {
        *d++ = *s++;
    } while (s != end);
}
