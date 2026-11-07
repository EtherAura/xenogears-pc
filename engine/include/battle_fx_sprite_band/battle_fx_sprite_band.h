#ifndef FX_0E_07_H
#define FX_0E_07_H

#include "common.h"
#include "main/task.h"
#include "main/sprite.h"

/* The 0E/07 effect task (size 0x5C): pins a sprite actor at its start
 * position and draws it as a horizontal band of 16 copies scrolled by its
 * move speed. */
typedef struct {
    /* 0x00 */ TaskHeader m0_updateTask;
    /* 0x1C */ TaskHeader m1C_drawTask;
    /* 0x38 */ FP_VEC3 m38_scroll;      /* 16.16 */
    /* 0x44 */ s32 m44;
    /* 0x48 */ FP_VEC3 m48_position;    /* the actor's position at creation */
    /* 0x54 */ s32 m54;
    /* 0x58 */ SpriteActorCore* m58_actor;
} Fx07Effect;

/* Sub-sprite bounding box (getFxSpriteBandBounds). */
typedef struct {
    /* 0x0 */ s16 m0_minY;
    /* 0x2 */ s16 m2_minX;
    /* 0x4 */ s16 m4_maxY;
    /* 0x6 */ s16 m6_maxX;
} Fx07Bounds;

#endif
