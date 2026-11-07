/*
 * Battle entry point and battle graphics initialisation.
 *
 * Provisional file 0x8001B6C4-0x8001B970 (main executable); bounds come from
 * compiler/assembler changes and may move. Built with GCC 2.7.2 (PSY-Q), ASPSX < 2.50.
 */
#include "common.h"
#include "main/kernel.h"
#include "psyq.h"
#include "main/filesystem.h"
#include "main/battle.h"
#include "main/debug_font.h"
#include "pc_gameplay.h"
#include "main/game_state.h"

s32 debugFontNop();
void battleMain(void);
void setGameMode(s32);
void bootGame(s32);
void initGraphicsForBattle(void);
void initBattleRenderStructs(BattleRenderStruct* arg0);
extern s32* pRunningOnDTL;
extern u8 battleInitVar0;
extern u8 battleInitVar1;
extern s16 D_8006F952;
extern BattleRenderStruct D_800C4A20[2] PORT_ALIAS;
extern u8 D_800C48EA;
extern u8 battleEventMoviePending; /* battle BSS: a battle event asked for a movie next */
u8 battleDebugDisplay;

void battleEntryPoint(void) {
    s32 mode;

    battleDebugDisplay = 1;
    waitReadCompletion(0);
    setCurrentDirectory(0xC, 0);
    if (*pRunningOnDTL != -1) {
        setDebugFontBuffer(0x80200000);
        initDebugFont(0x10, 0x10, 0x140, 0x100, 0x3E8, 0, 0x340, 0, 0x340, 0x20, 0);
    }
    initGraphicsForBattle();
    battleMain();
    /* A gauntlet's next fight, or the end of its run: the state of before
     * the run, back to the field or world map (port/src/game/main/gauntlet.c). */
    mode = pcGauntletBattleEnd();
    if (mode >= 0) {
        setGameMode(mode);
    } else
    if (D_800C48EA == 1 || D_800C48EA == 0x40 || D_800C48EA == 0x21) {
        mode = 6;
        if (battleEventMoviePending == 0) {
            if (battleInitVar0 == 0) {
                mode = 3;
                if ((gameState.m231A_fieldID & 0x7FF) < 0x400) {
                    mode = 1;
                }
            } else {
                mode = 2;
            }
        }
        setGameMode(mode);
    } else if (D_800C48EA == 0x81) {
        /* The retry setting: the player chose Retry, the battle starts again
         * from its checkpoint (port/src/game/main/retry.c). */
        if (pcRetryRestore()) {
            setGameMode(2);
        } else
        {
            resetFieldScriptEntityInit();
            gameState.m231A_fieldID = 0x1EA;
            gameState.m231C_cameraYaw = 0;
            D_8006F952 = 0;
            gameState.m2320_worldmapMode = 0;
            setGameMode(1);
        }
    }
    if (battleInitVar0 == 0) {
        battleInitVar1 = 1;
    }
    bootGame(0);
}

void initGraphicsForBattle(void) {
    ResetGraph(1);
    debugFontNop(0x300, 0);
    debugFontNop(debugFontNop(8, 0x10, 0x140, 0xF0, 0, 0x1000));
    InitGeom();
    SetGeomOffset(0xA0, 0xB4);
    SetGeomScreen(0x200);
    SetDefDispEnv(&D_800C4A20[0].m5C_dispEnv, 0, 0xE0, 0x140, 0xE0);
    SetDefDrawEnv(&D_800C4A20[0].m0_drawEnv, 0, 0, 0x140, 0xE0);
    SetDefDispEnv(&D_800C4A20[1].m5C_dispEnv, 0, 0, 0x140, 0xE0);
    SetDefDrawEnv(&D_800C4A20[1].m0_drawEnv, 0, 0xE0, 0x140, 0xE0);
    initBattleRenderStructs(&D_800C4A20[0]);
    initBattleRenderStructs(&D_800C4A20[1]);
}

void initBattleRenderStructs(BattleRenderStruct* arg0) {
    arg0->m0_drawEnv.isbg = 1;
    arg0->m0_drawEnv.dtd = 1;
    arg0->m0_drawEnv.r0 = 0x3C;
    arg0->m0_drawEnv.g0 = 0x78;
    arg0->m0_drawEnv.b0 = 0x78;
}
