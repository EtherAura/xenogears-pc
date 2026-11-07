#ifndef FX_0E_02_H
#define FX_0E_02_H

#include "common.h"
#include "psyq.h"
#include "main/task.h"

/* One tile of the 0E/02 effect (size 0x74). */
typedef struct {
    /* 0x00 */ s32 m0;
    /* 0x04 */ POLY_FT3 m4_poly[2];
    /* 0x44 */ SVECTOR m44_vertices[3];
    /* 0x5C */ SVECTOR m5C_screen[3];
} Fx02Tile;

/* The 0E/02 effect task (a task pair like sBattleStartEffect, size 0xE840). */
typedef struct {
    /* 0x00 */ TaskHeader m0_updateTask;
    /* 0x1C */ TaskHeader m1C_drawTask;
    /* 0x38 */ s32 m38_frameCounter;
    /* 0x3C */ s32 m3C;
    /* 0x40 */ Fx02Tile m40_tiles[2][16][16];
} Fx02Effect;

#endif
