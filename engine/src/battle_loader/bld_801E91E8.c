/* Battle loader transition mode 3 (wave/triangles) and mode 4.
 * gcc-2.7.2-cdk, ASPSX 2.56. */
#include "common.h"
#include "psyq.h"
#include "main/memory.h"
#include "main/task.h"
#include "main/filesystem.h"
#include "stack.h"
#define mainBattleSpriteCallback_phase3 fieldMecha_func_801E6E48
#include "battle/battle.h"
#undef mainBattleSpriteCallback_phase3
#include "battle_loader/battle_loader.h"

/* main */
s32 addClamp255(s32 value, s32 delta);
void resetSpriteCallbacks(void);
void checkSoftReboot(void);
void startBattleLoadingDuringEffect2(void);
s32 waitReadCompletion(s32 mode);

/* this overlay */
extern OTag* battleWaveEffectOT;                /* wave transition OT */
extern u8 isBattleWaveEffectMode3;
void battleLoaderTick(u8 step);
void pcWideLoaderCapture(void);
sBattleWaveEffect* createBattleStartEffect(void);
void updateBattleWaveEffect(TaskHeader* task);
void renderBattleWaveEffect(TaskHeader* task);
void freeBattleWaveEffect(void* p);

/* Battle loader transition with the wave effect (a variant of
 * battleLoaderUpdateMode1; Noah: none) */
/* Enhanced, 60 Hz (bld_801E7F4C.c) */
void pcLoader60Begin(sBattleTileEffect* tile, sBattleWaveEffect* wave);
void pcLoader60Record(void);
void pcLoader60VSync(void);

void battleLoaderUpdateMode2(void);
void battleLoaderUpdateMode2(void) {
    s32 counter;
    s32 state;
    s32 n;
    u16* buf;
    u16* p;
    RECT r;
    s32 i;
    sBattleWaveEffect* e;

    counter = 0x52;
    resetSpriteCallbacks();
    state = 1;
    n = 0;
    buf = allocateHeap(0x30000, 1);
    p = buf;
    r.x = 0;
    r.y = 0;
    r.w = 0x140;
    r.h = 0xE0;
    StoreImage(&r, (u32*)buf);
    DrawSync(0);
    for (i = 0; i != 0x14000; i++) {
        *p++ |= 0x8000;
    }
    r.x = 0x2C0;
    r.y = 0x100;
    r.w = 0x140;
    r.h = 0xE0;
    LoadImage(&r, buf);
    DrawSync(0);
    pcWideLoaderCapture();
    freeHeap(buf);
    battleBssBlock.m8C50_currentRenderStruct = (battleBssBlock.m8C50_currentRenderStruct == &battleBssBlock.mB70_renderStructs[0]) ? &battleBssBlock.mB70_renderStructs[1] : &battleBssBlock.mB70_renderStructs[0];
    battleBssBlock.m8C54_currentOT = battleBssBlock.m8C50_currentRenderStruct->m70_ot;
    ClearOTagR(battleBssBlock.m8C54_currentOT, 0x1000);
    battleBssBlock.m8C84_oddOrEven = 0;
    battleBssBlock.m8C50_currentRenderStruct = &battleBssBlock.mB70_renderStructs[0];
    battleBssBlock.mB70_renderStructs[0].m0_drawEnv.isbg = 1;
    battleBssBlock.mB70_renderStructs[1].m0_drawEnv.isbg = 1;
    battleBssBlock.mB70_renderStructs[0].m0_drawEnv.r0 = 0;
    battleBssBlock.mB70_renderStructs[1].m0_drawEnv.r0 = 0;
    battleBssBlock.mB70_renderStructs[0].m0_drawEnv.g0 = 0;
    battleBssBlock.mB70_renderStructs[1].m0_drawEnv.g0 = 0;
    battleBssBlock.mB70_renderStructs[0].m0_drawEnv.b0 = 0;
    battleBssBlock.mB70_renderStructs[1].m0_drawEnv.b0 = 0;
    e = createBattleStartEffect();
    pcLoader60Begin(NULL, e);
    while (counter != 0 || state != 5) {
        if (counter > 0) {
            counter--;
        }
        battleBssBlock.m8C50_currentRenderStruct = (battleBssBlock.m8C50_currentRenderStruct == &battleBssBlock.mB70_renderStructs[0]) ? &battleBssBlock.mB70_renderStructs[1] : &battleBssBlock.mB70_renderStructs[0];
        battleBssBlock.m8C54_currentOT = battleBssBlock.m8C50_currentRenderStruct->m70_ot;
        ClearOTagR(battleBssBlock.m8C54_currentOT, 0x1000);
        battleBssBlock.m8C84_oddOrEven = 1 - battleBssBlock.m8C84_oddOrEven;
        battleWaveEffectOT = battleBssBlock.m8C54_currentOT;
        if (isCDBusy() == 0) {
            switch (state) {
            case 0:
                break;
            case 2:
                startBattleLoadingDuringEffect2();
                state++;
                break;
            case 1:
            case 3:
            case 4:
                battleLoaderTick(n);
                n++;
                state++;
                break;
            }
        }
        checkSoftReboot();
        battleBssBlock.mB70_renderStructs[battleBssBlock.m8C84_oddOrEven].m0_drawEnv.r0 = addClamp255(battleBssBlock.mB70_renderStructs[battleBssBlock.m8C84_oddOrEven].m0_drawEnv.r0, -0xC);
        battleBssBlock.mB70_renderStructs[battleBssBlock.m8C84_oddOrEven].m0_drawEnv.g0 = addClamp255(battleBssBlock.mB70_renderStructs[battleBssBlock.m8C84_oddOrEven].m0_drawEnv.g0, -0xC);
        battleBssBlock.mB70_renderStructs[battleBssBlock.m8C84_oddOrEven].m0_drawEnv.b0 = addClamp255(battleBssBlock.mB70_renderStructs[battleBssBlock.m8C84_oddOrEven].m0_drawEnv.b0, -0xC);
        STACK_SWITCH(PSX_SCRATCH(0x1F8003FC));
        updateBattleWaveEffect((TaskHeader*)e);
        renderBattleWaveEffect((TaskHeader*)e);
        STACK_RESTORE();
        pcLoader60Record(); /* Enhanced, 60 Hz (bld_801E7F4C.c) */
        DrawSync(0);
        pcLoader60VSync();
        PutDispEnv(&battleBssBlock.m8C50_currentRenderStruct->m5C_dispEnv);
        PutDrawEnv(&battleBssBlock.m8C50_currentRenderStruct->m0_drawEnv);
        DrawOTag(&battleBssBlock.m8C50_currentRenderStruct->m70_ot[0xFFF]);
    }
    pcLoader60Begin(NULL, NULL);
    freeBattleWaveEffect(e);
    SetDispMask(0);
    waitReadCompletion(0);
    battleLoaderTick(3);
}

void battleLoaderUpdateMode3(void);
void battleLoaderUpdateMode3(void) {
    isBattleWaveEffectMode3 = 1;
    battleLoaderUpdateMode2();
}
