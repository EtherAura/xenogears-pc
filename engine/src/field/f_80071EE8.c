/*
 * FA status (US 0x80071EE8-0x80072140): gcc-2.7.2-psx -O2 -G0, aspsx 2.34
 * (also OK with -G8, and with gcc-2.6.0/2.6.3: nothing here tells them apart):
 *   initFieldMouse  OK  sets up a debug/print window (setMousePadBuffers.. with padReceiveBuffers)
 *   setFieldDrawEnvClip  OK*  Noah: setFieldDrawEnvClip
 *   initFieldDrawEnvs  OK*  Noah: initFieldDrawEnvs
 * OK* = code bytes identical; tools/trymatch.py reports "wrong relocation
 * target" only because it reads a %lo addend as signed 16 bits and the
 * second render context is at +0x80F4 (hi 0x1 / lo -0x7F0C). With the HI16
 * addend paired (work/agents/FA/trymatch_hilo.py) it prints OK; the linker
 * resolves the pair correctly.
 */
#include "common.h"
#include "psyq.h"
#include "field/field.h"

extern s32 fieldDrawEnvsInitialized;
extern u8 padReceiveBuffers[];

void setMousePadBuffers(void* arg0, void* arg1);
void setMouseCursorBounds(s32 arg0, s32 arg1, s32 arg2, s32 arg3);
void setMouseCursorPosition(s32 arg0, s32 arg1, s32 arg2);
void setupFieldDisplayEnv(void);
void initFieldDrawEnvsSub0(s32 w, s32 h);

void initFieldMouse(void) {
    setMousePadBuffers(padReceiveBuffers, padReceiveBuffers + 0x22);
    setMouseSpeedScale(3, 4);
    setMouseCursorBounds(0, 0x140, 0, 0xE0);
    setMouseCursorPosition(0, 0x50, 0x64);
    setMouseCursorPosition(1, 0xFA, 0x64);
    setMouseCursorBounds(0, 0x12C, 0xA, 0xDC);
}

void setFieldDrawEnvClip(s32 x, s32 y, s32 w, s32 h) {
    fieldRenderContext[0].m0_drawEnv.clip.x = x;
    fieldRenderContext[0].m0_drawEnv.clip.y = y;
    fieldRenderContext[0].m0_drawEnv.clip.w = w;
    fieldRenderContext[0].m0_drawEnv.clip.h = h;
    fieldRenderContext[1].m0_drawEnv.clip.x = x;
    fieldRenderContext[1].m0_drawEnv.clip.y = y + 0x100;
    fieldRenderContext[1].m0_drawEnv.clip.w = w;
    fieldRenderContext[1].m0_drawEnv.clip.h = h;
}

void initFieldDrawEnvs(void) {
    fieldDrawEnvsInitialized = 1;
    DrawSync(0);
    VSync(0);
    InitGeom();
    SetGeomOffset(0xA0, 0x70);
    SetDefDrawEnv(&fieldRenderContext[0].m0_drawEnv, 0, 0, 0x140, 0xE0);
    SetDefDrawEnv(&fieldRenderContext[1].m0_drawEnv, 0, 0x100, 0x140, 0xE0);
    SetDefDrawEnv(&fieldRenderContext[0].m5C_backgroundRect, 0, 0, 0x140, 0xE0);
    SetDefDrawEnv(&fieldRenderContext[1].m5C_backgroundRect, 0, 0x100, 0x140, 0xE0);
    SetDefDispEnv(&fieldRenderContext[0].mB8_displayEnv, 0, 0x100, 0x140, 0xE0);
    SetDefDispEnv(&fieldRenderContext[1].mB8_displayEnv, 0, 0, 0x140, 0xE0);
    setFieldDrawEnvClip(0, 0, 0x140, 0xE0);
    setupFieldDisplayEnv();
    fieldRenderContext[0].m0_drawEnv.r0 = 0;
    fieldRenderContext[0].m0_drawEnv.g0 = 0;
    fieldRenderContext[0].m0_drawEnv.b0 = 0;
    fieldRenderContext[1].m0_drawEnv.r0 = 0;
    fieldRenderContext[1].m0_drawEnv.g0 = 0;
    fieldRenderContext[1].m0_drawEnv.b0 = 0;
    fieldRenderContext[0].m0_drawEnv.dtd = 1;
    fieldRenderContext[1].m0_drawEnv.dtd = 1;
    VSync(0);
    PutDispEnv(&fieldRenderContext[1].mB8_displayEnv);
    PutDrawEnv(&fieldRenderContext[1].m0_drawEnv);
    initFieldDrawEnvsSub0(0x140, 0xF0);
}
