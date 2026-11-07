#ifndef BATTLE_PC_WIDE_FX_H
#define BATTLE_PC_WIDE_FX_H

/*
 * Helpers for the battle effects' pieces that only the wide picture has
 * (include/pc_wide.h): the screen transitions, the shatter and the ripple add
 * columns of triangles beside the 4:3 picture, and the tiled sprite more
 * copies. They drive the GTE directly instead of through libgte: every libgte
 * call is a safe point (port/src/system/hw.c), and under XG_CLOCK=step each
 * one moves time on, so the extra calls would change when interrupts arrive
 * and a wide run would stop matching the 4:3 one. The callers save and
 * restore the GTE around them (gteSave/gteLoad).
 */

#include "gte.h"

/* RotTransPers3 without its safe point: the three screen positions, the
 * flags, and SZ3 / 4 (its return value). */
static inline s32 pcWideRotTransPers3(SVECTOR* v0, SVECTOR* v1, SVECTOR* v2, void* sxy0, void* sxy1, void* sxy2,
                                      s32* flag) {
    s32 sz;

    gte_ldv3(v0, v1, v2);
    gte_rtpt();
    gte_stsxy3(sxy0, sxy1, sxy2);
    gte_stflg(flag);
    gte_stsz(&sz);
    return sz >> 2;
}

/* RotTransPers (one vertex) without its safe point; returns SZ / 4. */
static inline s32 pcWideRotTransPers(SVECTOR* v, void* sxy, s32* flag) {
    s32 sz;

    gte_ldv0(v);
    gte_rtps();
    gte_stsxy(sxy);
    gte_stflg(flag);
    gte_stsz(&sz);
    return sz >> 2;
}

/* RotAverage4 without its safe point: four screen positions (the fourth
 * through the last FIFO slot, as libgte does), the flags, and OTZ. */
static inline s32 pcWideRotAverage4(SVECTOR* v0, SVECTOR* v1, SVECTOR* v2, SVECTOR* v3, void* sxy0, void* sxy1,
                                    void* sxy2, void* sxy3, s32* flag) {
    s32 f, otz;

    gte_ldv3(v0, v1, v2);
    gte_rtpt();
    gte_stsxy3(sxy0, sxy1, sxy2);
    gte_stflg(&f);
    gte_ldv0(v3);
    gte_rtps();
    gte_stsxy(sxy3);
    gte_stflg(flag);
    *flag |= f;
    gte_avsz4();
    gte_stotz(&otz);
    return otz;
}

/* SetRotMatrix and SetTransMatrix of m, with its translation t. */
static inline void pcWideSetMatrix(MATRIX* m, VECTOR* t) {
    m->t[0] = t->vx;
    m->t[1] = t->vy;
    m->t[2] = t->vz;
    gte_SetRotMatrix(m);
    gte_SetTransMatrix(m);
}

/* The integer square root (SquareRoot0's job). */
static inline s32 pcWideSqrt(s32 a) {
    u32 r = 0, bit = 1u << 30, n = a > 0 ? (u32)a : 0;

    while (bit > n) {
        bit >>= 2;
    }
    while (bit != 0) {
        if (n >= r + bit) {
            n -= r + bit;
            r = (r >> 1) + bit;
        } else {
            r >>= 1;
        }
        bit >>= 2;
    }
    return (s32)r;
}

/* An angle of 4096 per turn, as ratan2 gives it (to about a degree). */
static inline s32 pcWideAtan2(s32 y, s32 x) {
    s32 ax = x < 0 ? -x : x;
    s32 ay = y < 0 ? -y : y;
    s32 q, a;

    if (ax == 0 && ay == 0) {
        return 0;
    }
    /* atan(t) ~ t * pi/4 + 0.273 t (1 - t) on [0, 1], t = q / 4096 */
    q = ax >= ay ? (s32)(((s64)ay << 12) / ax) : (s32)(((s64)ax << 12) / ay);
    a = (512 * q + (s32)((s64)178 * q * (4096 - q) / 4096)) / 4096;
    if (ay > ax) {
        a = 1024 - a;
    }
    if (x < 0) {
        a = 2048 - a;
    }
    return y < 0 ? -a : a;
}

#endif
