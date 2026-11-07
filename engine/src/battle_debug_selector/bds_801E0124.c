/* The battle debug selector (Noah battleDebugSelector.cpp).
 * gcc-2.6.3-psx, ASPSX 2.34. */
#include "common.h"
#include "psyq.h"
#include "main/battle.h"
#include "main/game_state.h"
#include "main/memory.h"
#include "main/filesystem.h"
#include "main/debug_font.h"

extern BattleRenderStruct battleRenderStructs[2] PORT_ALIAS;
extern BattleRenderStruct* pCurrentBattleRenderStruct;
extern u8 battleInputButton;
extern void* battleDebugSelectorMusicBuffer;
extern u8 battleInitVar0;
extern u8 requestedBattleConfig;
extern u8 battleTransitionEffect;
extern u8 battleConfigs[];
extern u8 battleMusic[];

extern const char* battleDebugSelectorLines[4];
extern const char* battleDebugSelectorCharacterNames[12];
extern s8 battleDebugSelectorGearMapping[16];
extern s8 battleDebugSelectorOmniGearMapping[16];
extern s32 battleDebugSelectorTableValues[4][3];
extern u8 battleDebugSelectorIsCellEnabled[4][3];

void battleHandleInput(u8);
void* allocateBattleHeap(s32 size, s32 flags);
void openMusicBattleFolder(void);
void battleSetDebugInventory(void);
void deinitDebugFont(void);
void resetBattleAudioState(void);

void battleDebugSelectorInit(void) {
    s32 i;
    s32 j;

    PutDrawEnv(&battleRenderStructs[0].m0_drawEnv);
    PutDrawEnv(&battleRenderStructs[1].m0_drawEnv);
    PutDispEnv(&battleRenderStructs[0].m5C_dispEnv);
    PutDispEnv(&battleRenderStructs[1].m5C_dispEnv);
    SetDispMask(1);
    gameState.m1D34_currentParty[0] = 0;
    gameState.m1D34_currentParty[1] = 1;
    gameState.m1D34_currentParty[2] = 2;
    battleDebugSelectorTableValues[0][0] = 3;
    battleDebugSelectorTableValues[1][0] = 0;
    battleDebugSelectorTableValues[1][1] = 1;
    battleDebugSelectorTableValues[1][2] = 2;
    battleDebugSelectorTableValues[2][0] = battleDebugSelectorTableValues[2][1] = battleDebugSelectorTableValues[2][2] = 0;
    battleDebugSelectorTableValues[3][0] = 0;
    for (i = 0; i < 4; i++) {
        for (j = 0; j < 3; j++) {
            battleDebugSelectorIsCellEnabled[i][j] = 1;
        }
    }
    battleDebugSelectorIsCellEnabled[0][1] = battleDebugSelectorIsCellEnabled[0][2] = battleDebugSelectorIsCellEnabled[3][1] = battleDebugSelectorIsCellEnabled[3][2] = 0;
}

void battleDebugSelectorMainLoop(void) {
    s32 cursorX;
    s32 cursorY;
    u8 continueMenu;
    s32 line;
    s32 column;
    s32 value;
    BattleRenderStruct* p;

    continueMenu = 1;
    cursorY = 0;
    cursorX = 0;
    do {
        battleHandleInput(0);
        p = battleRenderStructs;
        if (pCurrentBattleRenderStruct == p) {
            p = &battleRenderStructs[1];
        }
        pCurrentBattleRenderStruct = p;
        ClearOTagR(p->m70_ot, 0x1000);

        switch (battleInputButton) {
        case 14:
            continueMenu = 0;
            break;
        case 1:
        case 13:
            cursorY++;
            if (cursorY >= 4) {
                cursorY = 0;
            }
            if (battleDebugSelectorIsCellEnabled[cursorY][cursorX] == 0) {
                cursorX = 0;
            }
            break;
        case 3:
            cursorY--;
            if (cursorY < 0) {
                cursorY = 3;
            }
            if (battleDebugSelectorIsCellEnabled[cursorY][cursorX] == 0) {
                cursorX = 0;
            }
            break;
        case 0:
            cursorX++;
            if (cursorX >= 3) {
                cursorX = 0;
            }
            if (battleDebugSelectorIsCellEnabled[cursorY][cursorX] == 0) {
                cursorX = 0;
            }
            break;
        case 2:
            cursorX--;
            if (cursorX < 0) {
                cursorX = 0;
            }
            if (battleDebugSelectorIsCellEnabled[cursorY][cursorX] == 0) {
                cursorX = 0;
            }
            break;
        case 5:
            if (--battleDebugSelectorTableValues[cursorY][cursorX] < 0) {
                switch (cursorY) {
                case 0:
                    battleDebugSelectorTableValues[cursorY][cursorX] = 0xF;
                    break;
                case 1:
                case 3:
                    battleDebugSelectorTableValues[cursorY][cursorX] = 0xFF;
                    break;
                case 2:
                    battleDebugSelectorTableValues[cursorY][cursorX] = 2;
                    break;
                }
            }
            break;
        case 4:
            battleDebugSelectorTableValues[cursorY][cursorX]++;
            switch (cursorY) {
            case 0:
                if (battleDebugSelectorTableValues[cursorY][cursorX] >= 0x10) {
                    battleDebugSelectorTableValues[cursorY][cursorX] = 0;
                }
                break;
            case 1:
                if (battleDebugSelectorTableValues[cursorY][cursorX] >= 0xC) {
                    battleDebugSelectorTableValues[cursorY][cursorX] = 0;
                }
                break;
            case 2:
                if (battleDebugSelectorTableValues[cursorY][cursorX] >= 3) {
                    battleDebugSelectorTableValues[cursorY][cursorX] = 0;
                }
                break;
            case 3:
                if (battleDebugSelectorTableValues[cursorY][cursorX] >= 0x100) {
                    battleDebugSelectorTableValues[cursorY][cursorX] = 0;
                }
                break;
            }
            break;
        case 7:
            if (cursorY == 3) {
                battleDebugSelectorTableValues[cursorY][cursorX] += 10;
                if (battleDebugSelectorTableValues[cursorY][cursorX] >= 0x100) {
                    battleDebugSelectorTableValues[cursorY][cursorX] = 0;
                }
            }
            break;
        case 8:
            break;
        }

        logToScreen("\n\n");
        for (line = 0; line < 4; line++) {
            logToScreen("\n\n%s", battleDebugSelectorLines[line]);
            for (column = 0; column < 3; column++) {
                if (battleDebugSelectorIsCellEnabled[line][column] == 0) {
                    continue;
                }
                switch (line) {
                case 0:
                    if (cursorY == line && column == cursorX) {
                        logToScreen("[%d] ", battleDebugSelectorTableValues[line][column]);
                    } else {
                        logToScreen("%d ", battleDebugSelectorTableValues[line][column]);
                    }
                    break;
                case 2:
                    if (cursorY == line && column == cursorX) {
                        switch (battleDebugSelectorTableValues[line][column]) {
                        case 0:
                            logToScreen("[Off] ");
                            break;
                        case 1:
                            logToScreen("[Nml] ");
                            break;
                        case 2:
                            logToScreen("[Bar] ");
                            break;
                        }
                    } else {
                        switch (battleDebugSelectorTableValues[line][column]) {
                        case 0:
                            logToScreen("Off ");
                            break;
                        case 1:
                            logToScreen("Nml ");
                            break;
                        case 2:
                            logToScreen("Bar ");
                            break;
                        }
                    }
                    break;
                case 3:
                    if (cursorY == line && column == cursorX) {
                        value = battleDebugSelectorTableValues[line][column];
                        if (value < 0xFD) {
                            logToScreen("[%d] ", value);
                        } else {
                            switch (value - 0xFD) {
                            case 0:
                                logToScreen("[Event3] ", value);
                                break;
                            case 1:
                                logToScreen("[Event2] ", value);
                                break;
                            case 2:
                                logToScreen("[Event1] ", value);
                                break;
                            }
                        }
                    } else {
                        value = battleDebugSelectorTableValues[line][column];
                        if (value < 0xFD) {
                            logToScreen("%d ", value);
                        } else {
                            switch (value - 0xFD) {
                            case 0:
                                logToScreen("Event3 ", value);
                                break;
                            case 1:
                                logToScreen("Event2 ", value);
                                break;
                            case 2:
                                logToScreen("Event1 ", value);
                                break;
                            }
                        }
                    }
                    break;
                case 1:
                    if (cursorY == line && column == cursorX) {
                        if (battleDebugSelectorTableValues[line][column] >= 12) {
                            logToScreen("[%s] ", battleDebugSelectorCharacterNames[11]);
                        } else {
                            logToScreen("[%s] ", battleDebugSelectorCharacterNames[battleDebugSelectorTableValues[line][column]]);
                        }
                    } else {
                        if (battleDebugSelectorTableValues[line][column] >= 12) {
                            logToScreen("%s ", battleDebugSelectorCharacterNames[11]);
                        } else {
                            logToScreen("%s ", battleDebugSelectorCharacterNames[battleDebugSelectorTableValues[line][column]]);
                        }
                    }
                    break;
                }
            }
        }
        logToScreen("\n\n     LU       Start  to Battle");
        logToScreen("\n   LL  LR     Maru   +");
        logToScreen("\n     LD       Batsu  -");
        printDebugTextOnScreen(pCurrentBattleRenderStruct->m70_ot);
        DrawSync(0);
        VSync(0);
        PutDrawEnv(&pCurrentBattleRenderStruct->m0_drawEnv);
        PutDispEnv(&pCurrentBattleRenderStruct->m5C_dispEnv);
        DrawOTag(&pCurrentBattleRenderStruct->m70_ot[0xFFF]);
    } while (continueMenu);
}

void battleDebugSelector(void) {
    s32 i;
    s32 size;
    u8* buf;
    s32 n;

    battleDebugSelectorInit();
    battleDebugSelectorMainLoop();
    n = 0;
    for (i = 0; i < 3; i++) {
        gameState.m22B1_isOnGear[i] = 0;
        gameState.m1D34_currentParty[i] = 0xFF;
        if (battleDebugSelectorTableValues[1][i] < 11) {
            gameState.m1D34_currentParty[n] = battleDebugSelectorTableValues[1][i];
            switch (battleDebugSelectorTableValues[2][i]) {
            case 0:
                gameState.m22B1_isOnGear[n] = 0;
                break;
            case 2:
                gameState.m26C_party[gameState.m1D34_currentParty[n]].mA0_gearNum = battleDebugSelectorOmniGearMapping[gameState.m1D34_currentParty[n]];
                gameState.m22B1_isOnGear[n] = 1;
                break;
            case 1:
                gameState.m26C_party[gameState.m1D34_currentParty[n]].mA0_gearNum = battleDebugSelectorGearMapping[gameState.m1D34_currentParty[n]];
                gameState.m22B1_isOnGear[n] = 1;
                break;
            }
            n++;
        }
    }
    setCurrentDirectory(0x20, 3);
    if (battleDebugSelectorTableValues[3][0] < 0xFD) {
        i = battleDebugSelectorTableValues[3][0] + 7;
    } else {
        i = battleDebugSelectorTableValues[3][0] - 0xF9;
    }
    requestedBattleConfig = battleDebugSelectorTableValues[0][0];
    if (requestedBattleConfig == 0xF && i == 7) {
        requestedBattleConfig = 3;
        battleInitVar0 = 3;
    }
    size = getFileSizeAligned(i);
    resetMemoryAllocStats(2, 0);
    buf = allocateBattleHeap(size, 1);
    readFile(i, buf, 0, 0x80);
    waitReadCompletion(0);
    memmove(battleConfigs, buf, 0x200);
    freeHeap(buf);
    resetBattleAudioState();
    openMusicBattleFolder();
    size = getFileSizeAligned(4);
    battleDebugSelectorMusicBuffer = allocateBattleHeap(size, 1);
    readFile(4, battleDebugSelectorMusicBuffer, 0, 0x80);
    waitReadCompletion(0);
    memmove(battleMusic, battleDebugSelectorMusicBuffer, size);
    freeHeap(battleDebugSelectorMusicBuffer);
    battleSetDebugInventory();
    for (i = 0; i < 11; i++) {
        gameState.m26C_party[i].m4C_HP = 999;
        gameState.m26C_party[i].m4E_MaxHP = 999;
        gameState.m26C_party[i].m50_MP = 99;
        gameState.m26C_party[i].m52_MaxMP = 99;
    }
    gameState.m1D30_partyMemberBitField = 0xFFFF;
    deinitDebugFont();
    battleTransitionEffect = requestedBattleConfig & 3;
}
