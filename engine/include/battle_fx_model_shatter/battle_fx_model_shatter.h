#ifndef FX_0E_05_H
#define FX_0E_05_H

#include "common.h"
#include "psyq.h"
#include "main/task.h"
#include "battle/battle.h"

/* One flying polygon of the 0E/05 "shatter" effect (size 0x54). */
typedef struct {
    /* 0x00 */ SVECTOR m0_rotation;
    /* 0x08 */ SVECTOR m8_rotationStep;
    /* 0x10 */ VECTOR m10_position;     /* 16.16 */
    /* 0x20 */ VECTOR m20_velocity;     /* 16.16 */
    /* 0x30 */ s32 m30_gravity;
    /* 0x34 */ SVECTOR m34_vertices[4]; /* relative to the centroid */
} Fx05Particle;

/* The 0E/05 effect task (size 0x74): breaks a custom poly model into its
 * polygons and throws them around. */
typedef struct {
    /* 0x00 */ TaskHeader m0_updateTask;
    /* 0x1C */ TaskHeader m1C_drawTask;
    /* 0x38 */ MATRIX m38_matrix;
    /* 0x58 */ s32 m58_count;
    /* 0x5C */ s32 m5C_timer;
    /* 0x60 */ s32 m60;
    /* 0x64 */ u8* m64_prims[2];        /* double-buffered GPU packets */
    /* 0x6C */ sCustomPolySubBuffer* m6C_model;
    /* 0x70 */ Fx05Particle* m70_particles;
} Fx05Effect;

#endif
