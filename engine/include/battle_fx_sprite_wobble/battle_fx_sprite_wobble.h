#ifndef FX_0E_04_H
#define FX_0E_04_H

#include "common.h"
#include "main/task.h"
#include "main/sprite.h"

/* The 0E/04 effect task (size 0x54): redraws a sprite actor's first
 * sub-sprite as 4-pixel rows displaced by a cosine wave. */
typedef struct {
    /* 0x00 */ TaskHeader m0_updateTask;
    /* 0x1C */ TaskHeader m1C_drawTask;
    /* 0x38 */ SpriteActorCore* m38_actor;
    /* 0x3C */ s32 m3C_amplitude;
    /* 0x40 */ s32 m40_amplitudeStep;   /* per row, 24.8 */
    /* 0x44 */ s32 m44_phaseStep;       /* per row */
    /* 0x48 */ s32 m48_phaseStepStep;   /* per row, 24.8 */
    /* 0x4C */ s32 m4C_phase;
    /* 0x50 */ s32 m50_phaseSpeed;      /* per frame */
} Fx04Effect;

#endif
