/*
 * FA status (US 0x80071A64-0x80071EE8): gcc-2.7.2-psx -O2 -G0, aspsx 2.34
 * (also OK with the -G8 preset; setupRGBCalcSlot0_fadeToBlack differs under gcc-2.6.x):
 *   resetAllRGBFadersToBlack  OK  Noah: resetAllRGBFadersToBlack
 *   updateRGBFader  OK  updates one screen fader (Noah: none; suggest updateRGBFader)
 *   updateScreenEffectSlots  OK  updates both faders + renderRGBFaders (suggest updateScreenEffectSlots)
 *   setupRGBCalcMode  OK  Noah: setupRGBCalcMode
 *   setupRGBCalcSlot0_fadeToBlack  OK  Noah: setupRGBCalcSlot0_fadeToBlack
 *   setupRGBFaderSlot0_fadeIn  OK  Noah: setupRGBFaderSlot0_fadeIn
 * Retail's div here has no zero/overflow checks; the preset (no
 * --expand-div) reproduces that.
 */
#include "common.h"
#include "psyq.h"
#include "field/field.h"
#include "field/screen_effects.h"

extern s32 D_800ADC04;
extern s16 fieldScreenFadedOut;
extern s32 g_frameOddOrEven; /* Noah: g_frameOddOrEven? */
s32 pcFieldOffTick(void);

void resetRGBFaderToBlack(s32 index);
void renderRGBFaders(OTag* ot, s32 frame);

void resetAllRGBFadersToBlack(void) {
    resetRGBFaderToBlack(0);
    resetRGBFaderToBlack(1);
}

void updateRGBFader(s32 index) {
    if (fieldScreenEffects.m4C_screenEffects[index].m52_isActive != 0) {
        if (fieldScreenEffects.m4C_screenEffects[index].m54_duration <= 0) {
            fieldScreenEffects.m4C_screenEffects[index].m54_duration = 0;
            if (fieldScreenFadedOut != 1) {
                if (fieldScreenEffects.m4C_screenEffects[index].m38_current.m0_R == 0 && fieldScreenEffects.m4C_screenEffects[index].m38_current.m4_G == 0 &&
                    fieldScreenEffects.m4C_screenEffects[index].m38_current.m8_B == 0) {
                    fieldScreenEffects.m4C_screenEffects[index].m52_isActive = 0;
                }
            }
        } else {
            fieldScreenEffects.m4C_screenEffects[index].m38_current.m0_R += fieldScreenEffects.m4C_screenEffects[index].m44_step.m0_R;
            if ((fieldScreenEffects.m4C_screenEffects[index].m38_current.m0_R >> 8) >= 0x100) {
                fieldScreenEffects.m4C_screenEffects[index].m38_current.m0_R = 0xFF00;
            }
            if (fieldScreenEffects.m4C_screenEffects[index].m38_current.m0_R < 0) {
                fieldScreenEffects.m4C_screenEffects[index].m38_current.m0_R = 0;
            }
            fieldScreenEffects.m4C_screenEffects[index].m38_current.m4_G += fieldScreenEffects.m4C_screenEffects[index].m44_step.m4_G;
            if ((fieldScreenEffects.m4C_screenEffects[index].m38_current.m4_G >> 8) >= 0x100) {
                fieldScreenEffects.m4C_screenEffects[index].m38_current.m4_G = 0xFF00;
            }
            if (fieldScreenEffects.m4C_screenEffects[index].m38_current.m4_G < 0) {
                fieldScreenEffects.m4C_screenEffects[index].m38_current.m4_G = 0;
            }
            fieldScreenEffects.m4C_screenEffects[index].m38_current.m8_B += fieldScreenEffects.m4C_screenEffects[index].m44_step.m8_B;
            if ((fieldScreenEffects.m4C_screenEffects[index].m38_current.m8_B >> 8) >= 0x100) {
                fieldScreenEffects.m4C_screenEffects[index].m38_current.m8_B = 0xFF00;
            }
            if (fieldScreenEffects.m4C_screenEffects[index].m38_current.m8_B < 0) {
                fieldScreenEffects.m4C_screenEffects[index].m38_current.m8_B = 0;
            }
            fieldScreenEffects.m4C_screenEffects[index].m54_duration--;
        }
    }
}

/* Enhanced (xg-hbc.19.3): at 60 Hz a fader's colour changes on the tick
 * frames (its own steps, phase 3, and what the scripts set) and is drawn
 * through the change (pcFieldDrawn, f_80072140.c): renderRGBFaders
 * (f_8007D93C.c) draws from pcFieldFaderDrawn, which keeps the colour each
 * frame drew; a change of more than PC_FADER_CUT a channel is a cut. */
s32 pcFieldSubFrames(void);
s32 pcFieldSubIndex(void);
s32 pcFieldDrawn(s32 prev, s32 cur);
#define PC_FADER_CUT 0x6000
static s32 pcFaderLast[2][3];  /* the game's colour the frame before */
static s8 pcFaderLastOn[2];
static s32 pcFaderPrev[2][3];  /* ... at the tick: what the native frame before showed */
static s8 pcFaderPrevOn[2];
static s8 pcFaderDrawing;      /* renderRGBFaders is drawing for updateScreenEffectSlots */
static s32 pcFaderSlotsLinked; /* the slots it linked (bit per slot), read once by pcFieldFaderSlotsLinked */
s32 pcFieldFaderDrawn(s32 index, s32 component, s32 cur) {
    s32 prev;
    if (!pcFaderDrawing || pcFieldSubFrames() == 1) {
        return cur;
    }
    pcFaderLast[index][component] = cur;
    pcFaderLastOn[index] = 2; /* drawn this frame */
    prev = pcFaderPrev[index][component];
    if (!pcFaderPrevOn[index] || prev - cur > PC_FADER_CUT || cur - prev > PC_FADER_CUT) {
        return cur;
    }
    return pcFieldDrawn(prev, cur);
}

void updateScreenEffectSlots(OTag* ot, s32 oddOrEven) {
    s32 i;
    if (D_800ADC04 == 2 && !pcFieldOffTick()) { /* Enhanced phase 3: fades step at the native cadence */
        updateRGBFader(0);
        updateRGBFader(1);
    }
    for (i = 0; i < 2; i++) {
        if (pcFieldSubIndex() == 0) { /* the tick: what the native frame before showed */
            s32 k;
            pcFaderPrevOn[i] = pcFaderLastOn[i] == 2; /* drawn the frame before (renderRGBFaders draws the active ones) */
            for (k = 0; k < 3; k++) pcFaderPrev[i][k] = pcFaderLast[i][k];
        }
        pcFaderLastOn[i] = 0;
    }
    pcFaderSlotsLinked = (fieldScreenEffects.m4C_screenEffects[0].m52_isActive != 0) |
                         (fieldScreenEffects.m4C_screenEffects[1].m52_isActive != 0) << 1;
    pcFaderDrawing = 1;
    renderRGBFaders(ot, g_frameOddOrEven);
    pcFaderDrawing = 0;
}

/* Enhanced (xg-hbc.19.4, f_800A55B8.c renderScreenTransition): the slots the
 * last updateScreenEffectSlots linked, once (a frame without it has none). */
s32 pcFieldFaderSlotsLinked(void) {
    s32 m = pcFaderSlotsLinked;
    pcFaderSlotsLinked = 0;
    return m;
}

void setupRGBCalcMode(s32 index, s32 steps, s32 r, s32 g, s32 b, s32 mode) {
    s32 stepR = R3000_DIV((r << 8) - fieldScreenEffects.m4C_screenEffects[index].m38_current.m0_R, steps);
    s32 stepG = R3000_DIV((g << 8) - fieldScreenEffects.m4C_screenEffects[index].m38_current.m4_G, steps);
    s32 stepB = R3000_DIV((b << 8) - fieldScreenEffects.m4C_screenEffects[index].m38_current.m8_B, steps);
    fieldScreenEffects.m4C_screenEffects[index].m54_duration = steps;
    fieldScreenEffects.m4C_screenEffects[index].m52_isActive = 1;
    fieldScreenEffects.m4C_screenEffects[index].m50_mode = mode;
    fieldScreenEffects.m4C_screenEffects[index].m44_step.m0_R = stepR;
    fieldScreenEffects.m4C_screenEffects[index].m44_step.m4_G = stepG;
    fieldScreenEffects.m4C_screenEffects[index].m44_step.m8_B = stepB;
}

void setupRGBCalcSlot0_fadeToBlack(s32 steps) {
    s32 step;
    if (fieldScreenFadedOut != 1) {
        fieldScreenFadedOut = 1;
        if (D_800ADC04 == 2) {
            step = R3000_DIV(0xFF00, steps);
            fieldScreenEffects.m4C_screenEffects[0].m38_current.m8_B = 0;
            fieldScreenEffects.m4C_screenEffects[0].m38_current.m4_G = 0;
            fieldScreenEffects.m4C_screenEffects[0].m38_current.m0_R = 0;
            fieldScreenEffects.m4C_screenEffects[0].m54_duration = steps;
            fieldScreenEffects.m4C_screenEffects[0].m52_isActive = 1;
            fieldScreenEffects.m4C_screenEffects[0].m50_mode = 2;
            fieldScreenEffects.m4C_screenEffects[0].m44_step.m8_B = step;
            fieldScreenEffects.m4C_screenEffects[0].m44_step.m4_G = step;
            fieldScreenEffects.m4C_screenEffects[0].m44_step.m0_R = step;
        }
    }
}

void setupRGBFaderSlot0_fadeIn(s32 steps) {
    s32 step;
    if (fieldScreenFadedOut != 0) {
        fieldScreenFadedOut = 0;
        if (D_800ADC04 == 2) {
            step = R3000_DIV(-0x10000, steps);
            fieldScreenEffects.m4C_screenEffects[0].m38_current.m8_B = 0xFF00;
            fieldScreenEffects.m4C_screenEffects[0].m38_current.m4_G = 0xFF00;
            fieldScreenEffects.m4C_screenEffects[0].m38_current.m0_R = 0xFF00;
            fieldScreenEffects.m4C_screenEffects[0].m52_isActive = 1;
            fieldScreenEffects.m4C_screenEffects[0].m54_duration = steps;
            fieldScreenEffects.m4C_screenEffects[0].m50_mode = 2;
            fieldScreenEffects.m4C_screenEffects[0].m44_step.m8_B = step;
            fieldScreenEffects.m4C_screenEffects[0].m44_step.m4_G = step;
            fieldScreenEffects.m4C_screenEffects[0].m44_step.m0_R = step;
        }
    }
}
