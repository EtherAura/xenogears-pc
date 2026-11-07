#include "common.h"
#include "psyq.h"
#include "field/screen_effects.h"
#include "pc_gameplay.h"

void SetTile(void* p);
void SetDrawMode(DR_MODE* p, s32 dfe, s32 dtd, s32 tpage, RECT* tw);

/* draw-mode texture windows, one per fader slot */
extern RECT rgbFaderTexWindows[2];
s32 pcFieldFaderDrawn(s32 index, s32 component, s32 cur); /* f_80071A64.c */

void resetRGBFaderToBlack(s32 index) {
    TILE* p = &fieldScreenEffects.m4C_screenEffects[index].m18_tile[0];

    SetTile(p);
    SetSemiTrans(p, 1);
    fieldScreenEffects.m4C_screenEffects[index].m18_tile[0].w = 0x140;
    fieldScreenEffects.m4C_screenEffects[index].m18_tile[0].h = 0xE0;
    fieldScreenEffects.m4C_screenEffects[index].m18_tile[0].x0 = 0;
    fieldScreenEffects.m4C_screenEffects[index].m18_tile[0].y0 = 0;
    fieldScreenEffects.m4C_screenEffects[index].m18_tile[1] = fieldScreenEffects.m4C_screenEffects[index].m18_tile[0];
    fieldScreenEffects.m4C_screenEffects[index].m52_isActive = 0;
    fieldScreenEffects.m4C_screenEffects[index].m54_duration = 0;
    fieldScreenEffects.m4C_screenEffects[index].m38_current.m0_R =
        fieldScreenEffects.m4C_screenEffects[index].m38_current.m4_G =
        fieldScreenEffects.m4C_screenEffects[index].m38_current.m8_B = 0;
    fieldScreenEffects.m4C_screenEffects[index].m50_mode = 2;
}

/* LIBGPU setRGB0 (not in psyq.h yet) */
#define setRGB0(p, _r0, _g0, _b0) ((p)->r0 = (_r0), (p)->g0 = (_g0), (p)->b0 = (_b0))

/* Render the active RGB faders: fader i goes into ot[i == 1] as a
 * full-screen semi-transparent TILE plus its DR_MODE; a fader whose colour
 * has reached black switches itself off. */
void renderRGBFaders(OTag* ot, s32 buf) {
    s32 i;

    for (i = 0; i < 2; i++) {
        if (fieldScreenEffects.m4C_screenEffects[i].m52_isActive != 0) {
            OTag* otp;

            rgbFaderTexWindows[i].x = 0;
            rgbFaderTexWindows[i].y = 0;
            rgbFaderTexWindows[i].w = 0x140;
            rgbFaderTexWindows[i].h = 0xE0;
            SetDrawMode(&fieldScreenEffects.m4C_screenEffects[i].m0_drawMode[buf], 0, 0,
                        GetTPage(0, fieldScreenEffects.m4C_screenEffects[i].m50_mode, 0, 0), &rgbFaderTexWindows[i]);
            /* (Enhanced: drawn through the native frame's step at 60 Hz, f_80071A64.c) */
            setRGB0(&fieldScreenEffects.m4C_screenEffects[i].m18_tile[buf],
                    pcFieldFaderDrawn(i, 0, fieldScreenEffects.m4C_screenEffects[i].m38_current.m0_R) >> 8,
                    pcFieldFaderDrawn(i, 1, fieldScreenEffects.m4C_screenEffects[i].m38_current.m4_G) >> 8,
                    pcFieldFaderDrawn(i, 2, fieldScreenEffects.m4C_screenEffects[i].m38_current.m8_B) >> 8);
            {
                /* a plain ot + (i == 1) becomes a branch, not xori/sltiu,
                 * and puts ot first in the addu */
                s32 second = (i == 1);
                otp = (OTag*)(second * sizeof(OTag) + (uptr)ot);
            }
            pcFieldPicAdd(PC_FIELD_PIC_FADERS, fieldScreenEffects.m4C_screenEffects[i].m18_tile[buf].r0 |
                                                   fieldScreenEffects.m4C_screenEffects[i].m18_tile[buf].g0 << 8 |
                                                   fieldScreenEffects.m4C_screenEffects[i].m18_tile[buf].b0 << 16); /* Enhanced oracle */
            addPrim(otp, &fieldScreenEffects.m4C_screenEffects[i].m18_tile[buf]);
            addPrim(otp, &fieldScreenEffects.m4C_screenEffects[i].m0_drawMode[buf]);
            if ((fieldScreenEffects.m4C_screenEffects[i].m38_current.m0_R >> 8) == 0 &&
                (fieldScreenEffects.m4C_screenEffects[i].m38_current.m4_G >> 8) == 0 &&
                (fieldScreenEffects.m4C_screenEffects[i].m38_current.m8_B >> 8) == 0) {
                fieldScreenEffects.m4C_screenEffects[i].m52_isActive = 0;
                fieldScreenEffects.m4C_screenEffects[i].m54_duration = 0;
            }
        }
    }
}
