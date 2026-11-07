#ifndef FIELD_SCREEN_EFFECTS_H
#define FIELD_SCREEN_EFFECTS_H

/*
 * Field screen distortion + RGB fader state (Noah: field/screenDistortion.cpp,
 * field.cpp sScreenEffectSlot/screenEffects).
 *
 * One aggregate at 0x800B2078: resetRGBFaderToBlack (resetRGBFaderToBlack) stores
 * two fader fields through a register holding 0x800B2078 + index * 0x58
 * with offsets 0x8C/0x88, which GCC only produces when the slot array is a
 * member (at +0x4C) of an object that starts at 0x800B2078.
 */

#include "common.h"
#include "psyq.h"

/* Noah: sRGB32 (8.8 fixed-point colour) */
typedef struct {
    /* 0x0 */ s32 m0_R;
    /* 0x4 */ s32 m4_G;
    /* 0x8 */ s32 m8_B;
} RGB32;

/* Noah: sScreenEffectSlot (0x58 bytes) */
typedef struct {
    /* 0x00 */ DR_MODE m0_drawMode[2];
    /* 0x18 */ TILE m18_tile[2];
    /* 0x38 */ RGB32 m38_current;
    /* 0x44 */ RGB32 m44_step;
    /* 0x50 */ s16 m50_mode;        /* semi-transparency rate (GetTPage abr) */
    /* 0x52 */ s16 m52_isActive;
    /* 0x54 */ s16 m54_duration;
    /* 0x56 */ s16 m56;
} ScreenEffectSlot;

typedef struct {
    /* 0x00 */ s16 m0_screenDistortionRunning;
    /* 0x02 */ s16 m2_screenDistortionFadeOut;
    /* 0x04 */ s16 m4_distortionParamDivider;
    /* 0x06 */ s16 m6;
    /* 0x08 */ s32 m8_distortionParamDiff[6];
    /* 0x20 */ s32 m20_distortionParam[6];
    /* 0x38 */ s16 m38_phaseX;          /* wave phase along x (per-column angle start) */
    /* 0x3A */ s16 m3A_phaseY;          /* wave phase along y */
    /* 0x3C */ DR_MOVE* m3C_drawMove[2];   /* Noah: screenDistortionDrawMove */
    /* 0x44 */ POLY_FT4* m44_polys[2];     /* Noah: screenDistortionPolys */
    /* 0x4C */ ScreenEffectSlot m4C_screenEffects[2]; /* Noah: screenEffects */
} FieldScreenEffects;

extern FieldScreenEffects fieldScreenEffects;

/* Callers pass the frame's odd/even index, but the definition reads the
 * global g_frameOddOrEven itself. */
void updateScreenEffectSlots(OTag* ot, s32 oddOrEven);

#endif /* FIELD_SCREEN_EFFECTS_H */
