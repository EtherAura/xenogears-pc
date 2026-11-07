/* The native ripple's blocking loop, stepped by the isolated extras clock. */
#include "common.h"
#include "psyq.h"
#include "main/memory.h"
#include "battle/battle.h"
#include "battle_fx_screen_ripple/battle_fx_screen_ripple.h"
extern u8 fxScreenRippleAnimatePhase;
extern OTag* fxScreenRippleOT;
Fx08Effect* createBattleStartEffect(void);
void fxScreenRipple_update(TaskHeader*);
void fxScreenRipple_draw(TaskHeader*);
static Fx08Effect* viewRipple;
static s32 viewFrames;
void battleFxScreenViewStart(void) {
    RECT r;
    u16* pixels;
    s32 i;
    if (viewRipple) freeHeap(viewRipple);
    r.x = r.y = 0; r.w = 320; r.h = 224;
    pixels = allocateHeap(320 * 224 * 2, 0);
    StoreImage(&r, (u32*)pixels);
    for (i = 0; i < 320 * 224; i++) pixels[i] |= 0x8000;
    r.x = 0x2C0; r.y = 0x100;
    LoadImage(&r, (u32*)pixels);
    freeHeap(pixels);
    viewFrames = 0xA4;
    viewRipple = createBattleStartEffect();
}
s32 battleFxScreenViewActive(void) { return viewFrames != 0; }
s32 battleFxScreenViewState(void) { return viewFrames; }
void battleFxScreenViewStep(OTag* ot, s32 parity, s32 advance) {
    if (!viewFrames) return;
    fxScreenRippleOT = ot;
    battleBssBlock.m8C84_oddOrEven = parity;
    if (advance) fxScreenRipple_update((TaskHeader*)viewRipple);
    fxScreenRipple_draw((TaskHeader*)viewRipple);
    if (advance && --viewFrames == 0) {
        freeHeap(viewRipple); viewRipple = NULL;
    }
}
