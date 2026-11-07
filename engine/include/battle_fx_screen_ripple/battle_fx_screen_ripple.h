#ifndef FX_0E_08_H
#define FX_0E_08_H

#include "common.h"
#include "psyq.h"
#include "main/task.h"

/* One triangle of the 0E/08 screen-shatter effect (size 0x7C); compare
 * sBattleStartEffectSub. */
typedef struct {
    /* 0x00 */ s32 m0;
    /* 0x04 */ POLY_GT3 m4_poly[2];
    /* 0x54 */ SVECTOR m54_vertices[3];
    /* 0x6C */ s32 m6C_phase[3];        /* per vertex */
    /* 0x78 */ s32 m78;
} Fx08Triangle;

/* Vertex l of triangle t: retail addresses it by its byte offset, the PC by
 * the field (POLY_GT3 grows on the PC). */
#define WAVE_VERTEX(t, l) (&(t)->m54_vertices[l])

/* The 0E/08 effect (a task pair like sBattleStartEffect, size 0x10FA4):
 * the captured screen, cut into 2x14x20 triangles that ripple and fly off. */
typedef struct {
    /* 0x00 */ TaskHeader m0_updateTask;
    /* 0x1C */ TaskHeader m1C_drawTask;
    /* 0x38 */ s32 m38_brightness;
    /* 0x3C */ s32 m3C_phase;
    /* 0x40 */ s32 m40_amplitude;
    /* 0x44 */ s32 m44_frameCounter;
    /* 0x48 */ s32 m48_speed;
    /* 0x4C */ VECTOR m4C_translation;
    /* 0x5C */ SVECTOR m5C_rotation;
    /* 0x64 */ Fx08Triangle m64_triangles[2][14][20];
} Fx08Effect;

#endif
