/* Battle debugger text pages: entity HP, script contexts, current damages,
 * monster script flags, character stats. gcc-2.7.2-psx, ASPSX 2.34. */
#include "common.h"
#include "psyq.h"
#include "main/game_state.h"
#include "main/debug_font.h"
#include "battle/battle.h"

extern u8 battleDebugDisplay;
void setDebugFontCursorPosition(s32 x, s32 y);

/* Battle debug text pages (battleDebugDisplay = 1..4): entity HP and script
 * contexts, current damages, monster script flags, work/character stats. */
void drawBattleDebugTextPages(void);
void drawBattleDebugTextPages(void) {
    s32 i;
    s32 j;
    s32 x;
    s32 y;
    s32 y2;

    switch (battleDebugDisplay) {
    case 1:
        setDebugFontCursorPosition(0, 0);
        for (i = 3; i < 11; i++) {
            if (battleBssBlock.m4_visualEntities[i].m4_isGear == 0) {
                logToScreen("%d,", battleBssBlock.m8E38_entities[i].m0_base.m4C_HP);
            } else {
                logToScreen("%d,", battleBssBlock.m8E38_entities[i].mA4_gear.m60_hp);
            }
        }
        logToScreen("\n");
        logToScreen("No  Cd  Cl  An  P1  P2  P3  Tg\n");
        for (i = 0; i < 23; i++) {
            y = (i + 2) * 8;
            setDebugFontCursorPosition(0, y);
            logToScreen("%X", i);
            setDebugFontCursorPosition(0x24, y);
            logToScreen("%X", battleScriptContext[i].m0_attackType);
            setDebugFontCursorPosition(0x48, y);
            logToScreen("%X", battleScriptContext[i].m1_operand);
            setDebugFontCursorPosition(0x6C, y);
            logToScreen("%X", battleScriptContext[i].m2_animationId);
            setDebugFontCursorPosition(0x90, y);
            logToScreen("%X", battleScriptContext[i].m3_nameIndex);
            setDebugFontCursorPosition(0xB4, y);
            logToScreen("%X", battleScriptContext[i].m4_valueLow);
            setDebugFontCursorPosition(0xD8, y);
            logToScreen("%X", battleScriptContext[i].m5_valueHigh);
            setDebugFontCursorPosition(0xFC, y);
            logToScreen("%X", battleScriptContext[i].m6_mask);
        }
        break;
    case 2:
        setDebugFontCursorPosition(0, 0x20);
        logToScreen("No  An  Sb  Tg  No  An  Sb  Tg  \n");
        for (i = 0; i < 32; i++) {
            y2 = i / 2;
            x = (i % 2) * 0x90;
            y2 = (y2 + 5) * 8;
            setDebugFontCursorPosition(x, y2);
            logToScreen("%X", i);
            setDebugFontCursorPosition(x + 0x24, y2);
            logToScreen("%X", battleCurrentDamages[i].m47_battleAnimationToPlay);
            setDebugFontCursorPosition(x + 0x48, y2);
            logToScreen("%X", battleCurrentDamages[i].m3A_animationParam);
            setDebugFontCursorPosition(x + 0x6C, y2);
            logToScreen("%X", battleCurrentDamages[i].m16_targetBitMask);
        }
        break;
    case 3:
        if (battleVar2->m2D3_currentEntityTurn >= 3) {
            setDebugFontCursorPosition(0, 0x50);
            logToScreen("bFlag\n");
            for (i = 0; i < 8; i++) {
                logToScreen("%X ", monstersScriptsEntities[battleVar2->m2D3_currentEntityTurn - 3].m30_s8Vars[i]);
            }
            logToScreen("\n");
            for (i = 0; i < 8; i++) {
                logToScreen("%X ", monstersScriptsEntities[battleVar2->m2D3_currentEntityTurn - 3].m30_s8Vars[i + 8]);
            }
            logToScreen("\nhFlag\n");
            for (i = 0; i < 4; i++) {
                logToScreen("%X ", monstersScriptsEntities[battleVar2->m2D3_currentEntityTurn - 3].m20_s16Vars[i]);
            }
            logToScreen("\n");
            for (i = 0; i < 4; i++) {
                logToScreen("%X ", monstersScriptsEntities[battleVar2->m2D3_currentEntityTurn - 3].m20_s16Vars[i + 4]);
            }
            logToScreen("\nlFlag\n");
            for (i = 0; i < 2; i++) {
                logToScreen("%X ", monstersScriptsEntities[battleVar2->m2D3_currentEntityTurn - 3].m10_s32Vars[i]);
            }
            logToScreen("\n");
            for (i = 0; i < 2; i++) {
                logToScreen("%X ", monstersScriptsEntities[battleVar2->m2D3_currentEntityTurn - 3].m10_s32Vars[i + 2]);
            }
        }
        break;
    case 4:
        setDebugFontCursorPosition(0, 0x20);
        for (i = 0; i < 3; i++) {
            logToScreen("\nWork#%d:", i);
            for (j = 0; j < 7; j++) {
                logToScreen(" %d", battleBssBlock.m8E38_entities[i].m0_base.m90_deathblowProgress[j]);
            }
        }
        logToScreen("\n");
        for (i = 0; i < 11; i++) {
            logToScreen("\nChar#%d:", i);
            for (j = 0; j < 7; j++) {
                logToScreen(" %d", gameState.m26C_party[i].m90_deathblowProgress[j]);
            }
        }
        break;
    }
}

