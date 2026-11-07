#ifndef FX_0E_06_H
#define FX_0E_06_H

#include "common.h"
#include "main/task.h"
#include "main/sprite.h"

/* The 0E/06 effect task (size 0x4C): keeps a sprite actor in place while
 * accumulating its movement, and draws the actor tiled horizontally across
 * the screen, scrolled by the accumulated movement. */
typedef struct {
    /* 0x00 */ TaskHeader m0_updateTask;
    /* 0x1C */ TaskHeader m1C_drawTask;
    /* 0x38 */ FP_VEC3 m38_scroll;      /* 16.16 */
    /* 0x44 */ s32 m44;
    /* 0x48 */ SpriteActorCore* m48_actor;
} Fx06Effect;

/* Sub-sprite bounding box (getFxTiledSpriteBounds). */
typedef struct {
    /* 0x0 */ s16 m0_minY;
    /* 0x2 */ s16 m2_minX;
    /* 0x4 */ s16 m4_maxY;
    /* 0x6 */ s16 m6_maxX;
} Fx06Bounds;

#endif
