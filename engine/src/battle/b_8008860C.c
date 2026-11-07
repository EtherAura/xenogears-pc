/* Provisional file (split where JP links the code in another order). */
#include "common.h"
#include "psyq.h"
#include "main/game_state.h"
#include "main/filesystem.h"
#include "main/memory.h"
#include "main/dialog.h"
#include "main/debug_font.h"
#include "battle/battle.h"

extern sBattle800cdd40 partyBattleStats[3];
extern sBattle800CEF10 mechaBattleStats[3];
extern BattleBss_800D2C0A D_800D2C0A;            /* gear fuel costs ... */
extern sBattleMechaInitData* battleMechaInitData;
extern sBattleSlotLayout battleSlotLayout[];
extern u8 battleInputButton;
extern u8 startCharacterJumpToEnemyVar0;

/* ---- used by 0x80086028-0x8008B478 ---- */
extern u16 bitmaskCharacter[];
extern u16 bitmaskCharacterReverse[];                      /* Noah: bitmaskCharacterReverse {0x8000 .. 1} */
void playSoundEffect(s32);
void repositionTextRenderingPlanePrim(POLY_FT4* p, u16 x, u16 y, u8 u, u8 v, u8 w);
void battleInitPoly4(POLY_FT4* p, u8 odd, u8 mode);
extern u8 gearDeathblowAbilityIds[];
extern u8 currentBattleMode;
void battleUpdateInputs_mode1(u8);
void battleUpdateInputs_mode2(u8);
void battleUpdateInputs_mode0(u8);
void setupGearHudFrame(void);
void setupGearHudStatusIcons(void);
void setupGearHudBoosterLabel(void);
void setupGearHudModeLabels(void);
void setupGearHudAttackValue(void);
void setupGearHudInfinityChance(void);
void setupGearHudPercentValue(void);
void setupGearHudAgilityValue(void);
extern u8 printIntegerToStringBattleBuffer[9];
void battleSetupTextPolyAdditive(POLY_FT4* p);
extern u8 gearHudMarkerPathDone;
extern s32 gearHudMarkerOffsetX;
extern s32 gearHudMarkerOffsetY;
extern s32 gearHudMarkerStartX;
extern s32 gearHudMarkerStartY;
extern s32 gearHudMarkerEndX;
extern s32 gearHudMarkerEndY;
extern s32 gearHudMarkerStepX;
extern s32 gearHudMarkerStepY;
extern u8 gearHudMarkerReverseX;
extern u8 gearHudMarkerReverseY;
extern s32 gearHudMarkerSpeed;
void battleSetupTextPolySubtractive(POLY_FT4* p);
extern u8 battleTimeEnabled;
extern s32 battleMenuScrollOffset;
extern u8 battleMenuScrollRequest;
void battleTimeProgress(void);
void battleHandleInput(u8);
void battleChiMenuTextScrollTick(void);
extern s32* pRunningOnDTL;
extern s32 g_playTimeInVsync;                        /* play time in vsyncs */
extern u16 newPadButtonForDialogs;                        /* pad 0 newly pressed */
extern u16 newPadButtonForField;                        /* pad 0 pressed with auto-repeat */
extern u8 battleIsPaused;
extern u8 battleInputButton;
s32 isControllerConnected(s32);
void resumeMusic(void);
void pauseMusic(void);
void uploadBuiltinTexture(s32, s32);
s32 getInputOverflowed(void);
void resetInputs(void);
s32 loadInputFromVSyncBuffer(void);
void drawCircleMenuChi_updateSub5(u8 flag);
void abilityCancelSelection(u8 entity);
void updateMonsterScriptEntitiesVarByAtttack(u8, u8);
void performAttackSub6(u8);
void executeAbilitySub0(u16 mask, u8 entity);
void loadImageSync(RECT* rect, void* data);
extern u8 perApTable[8][3]; /* 8 rows up to the next label; its first byte is pBattleEntities's top byte */
void moveEntityToOtherEntity(u8 entity, u8 target);
void moveEntityToShootEntity(u8 entity, u8 target);
extern u8 battleDebugFontInitialized;
extern u8 battleDebugDisplay;
extern s32 gearHudMarkerTargets[];

void setupResultExpValues(void);
void setupResultExpGainedValues(void);
extern u16* deathblowNameImages[3];
s32 addGearDeathblowNamePoly(u8 entity, s32 index, s32 x, u8 cmd, u16** image);
u16* allocateTextureRamForText(s32 n);
s32 addDeathblowNamePoly(u8 slot, s32 index, s32 x, u8 textIdx, u16** image, u8 offset);
extern u8 gearHudFrameGlyphs[4];

/* ---- used by 0x80080AE4-0x80086028 ---- */
extern u8 autoPartyMemberTurnActive;
extern u8 battleTickMainSub0Var1[11];   /* accumulated damage type per entity */
extern s16 battleTickMainSub0Var0[11];  /* accumulated damage value per entity */
extern s32 damageDonePerAttack[11];  /* damage value per entity (current hit) */
extern u8 initBattleAttackStatusArray0[11];   /* damage type per entity (current hit) */
extern u16 entitiesHitInCurrentAttackBF[1];
extern u16 performAttack_type;
extern u8 battleGetSlotStatusSub_current28Index;
extern u8 newBattleInputButton2;
void freeBillyBattleStructs(u8);
void initBattleG3Array(void);
void selectDefaultTarget(u8);
void startCharacterJumpToEnemy(u8);
u8 drawCircleMenuItem_update(u8);
void executeItem(u8);
void characterPerformDefendAction(u8);
void drawCircleMenuChi_loadData(u8);
u8 drawCircleMenuChi_update(u8);
void executeAbility(u8);
u8 drawCircleMenuGearSpecial_update(u8);
u8 useCombo(u8);
extern u8 battleRunningVar1[1]; /* Noah: battleRunningVar1 (an array: retail keeps its address in a register) */
extern u8 gearSwitchDisabled;
extern u8 targetsPerPriority[12];
extern u8 numValidTarget;
u8 getEntityToFace(u8);
u8 isTargetValid(u8, u8);
u8 abilityTargetSelectionInitSub0Sub0(u8, u8);
u8 abilityTargetSelectionInitSub0(u8, u8, u8);
u8 abilityTargetSelectionInitSub1(u8);
u16 abilityTargetSelectionInit(u16, u16, u8, u8, u8);
u16 abilitySelectTargetSub0(void);
void updateTargetSelectionMarkerSub1(u8);
void removeTargetSelectionCursors(void);
s32 battleSetupStringInPolyFT4Large(s32, POLY_FT4 (*)[2], s16, s16);
extern u8* deathblowInputSequences[13];  /* deathblow input sequences (7 buttons each) */
extern u8* deathblowAbilityIdsByLoadout[];    /* per command loadout: deathblow ability index per sequence */
u16 bitmaskCharacterCheckReverse(u16, u8);
u8 randomInRange(s32, s32);
void clearEntityFlag15A_1(u8);
void initAnimSeqFromCharacterToCharacter(s32 entity, s32 target);
s32 initJumpData(u8, u8);
s32 performAttack(u8, u8);
void handleMenuSelectEnemy_cancel_sub0(u8);
s32 computeFacingForJump(u8);
extern u8 startCharacterJumpToEnemyVar1;
void cancelJumpAnim(void);
void updateTargetSelectionMarker(u8);
void removeBattleCursorDuringAttack(u8);
void updateAPCounterDisplay(void);
void updateDeathblowComboInput(u8, u8);
u8 handleMenuSelectEnemySub(u8, u8);
void battleSoundEffect(u8);         /* Noah: battleSoundEffect2 */
u8 updateGearDeathblowComboInput(u8, u8);
s32 isGearComboAttackAvailable(s32 cmd, u8 entity);
void updateGearHudFuel(u8);
extern u8 healWhileOnGear;
extern u8 apTurnDelayTicks[6][8];      /* ticks table [maxAP][remainingAP]; its last row runs into apMarkOffsetsY */
void performAttackSub2(void);
u8 selectNewSlotByDirection(u8, u8);
void battleRenderDebugAndMain(void);
extern u16 abilityTargetBitmask;
extern u8 abilitySelectedTarget;
u16 characterOdToInverseTargetBitmask(u8);
u16 bitmaskCharacterCheck(u16, u8);
void callGearRelocateSlot(u8);
void callGear(u8);
void callGearClearStatus(u8);
void markEnemyDead(u8);
void freeBattleVar1_AE(void);
void freeBattleVar1_96(void);
void freeBattleRenderStruct2(u8);
void setDamageDone(u8);
void applyChangeToHpOrMp(u8);

/* 263 260] */
void setupGearHudFrame(void) {
    s32 i;

    for (i = 0; i < 2; i++) {
        pBattleRenderStruct2->m5D70_count[0x4] += battleSetupStringInPolyFT4Large(gearHudFrameGlyphs[i], &pBattleRenderStruct2->m1720_frame[pBattleRenderStruct2->m5D70_count[0x4] * 2], 0xA0, 0x64);
    }
    pBattleRenderStruct2->m5D83_oddOrEven[0x0] = D_800CCB04.m30_battleOddOrEven;
    pBattleRenderStruct2->m5D70_count[0x0] = battleSetupStringInPolyFT4Large(0xA8, &pBattleRenderStruct2->m0[pBattleRenderStruct2->m5D70_count[0x0] * 2], 0xA0, 0x64);
    pBattleRenderStruct2->m5D83_oddOrEven[0xf] = D_800CCB04.m30_battleOddOrEven;
    for (i = 0; i < pBattleRenderStruct2->m5D70_count[0x4]; i++) {
        battleSetupTextPolyAdditive(&pBattleRenderStruct2->m1720_frame[i * 2 + pBattleRenderStruct2->m5D83_oddOrEven[0x0]]);
    }
    for (i = 0; i < pBattleRenderStruct2->m5D70_count[0x0]; i++) {
        battleSetupTextPolyAdditive(&pBattleRenderStruct2->m0[i * 2 + pBattleRenderStruct2->m5D83_oddOrEven[0xf]]);
    }
    for (i = 2; i < 4; i++) {
        pBattleRenderStruct2->m5D70_count[0xe] += battleSetupStringInPolyFT4Large(gearHudFrameGlyphs[i], &pBattleRenderStruct2->m2530_frameShadow[pBattleRenderStruct2->m5D70_count[0xe] * 2], 0xA0, 0x64);
    }
    pBattleRenderStruct2->m5D83_oddOrEven[0xa] = D_800CCB04.m30_battleOddOrEven;
    for (i = 0; i < pBattleRenderStruct2->m5D70_count[0xe]; i++) {
        battleSetupTextPolySubtractive(&pBattleRenderStruct2->m2530_frameShadow[i * 2 + pBattleRenderStruct2->m5D83_oddOrEven[0xa]]);
    }
}

/* 72 263 260] */
void startGearHudMarkerPath(s32 x0, s32 y0, s32 x1, s32 y1) {
    s32 dx;
    s32 dy;

    gearHudMarkerStartX = x0;
    gearHudMarkerStartY = y0;
    gearHudMarkerEndX = x1;
    gearHudMarkerEndY = y1;
    if (x1 != x0 && y1 != y0) {
        if (x1 < x0) {
            gearHudMarkerReverseX = 1;
            dx = x0 - x1;
        } else {
            dx = x1 - x0;
            gearHudMarkerReverseX = 0;
        }
        if (y1 < y0) {
            gearHudMarkerReverseY = 1;
            dy = y0 - y1;
        } else {
            dy = y1 - y0;
            gearHudMarkerReverseY = 0;
        }
        if (dx >= dy) {
            gearHudMarkerStepX = 0x100;
            gearHudMarkerStepY = R3000_DIV(dy << 8, dx);
        } else {
            gearHudMarkerStepY = 0x100;
            gearHudMarkerStepX = (dx << 8) / dy;
        }
        gearHudMarkerOffsetX = 0;
        gearHudMarkerOffsetY = 0;
        gearHudMarkerSpeed = randomInRange(1, 8);
        gearHudMarkerPathDone = 0;
    }
}

/* 72 263 260] */
void stepGearHudMarkerPath(void) {
    s32 i;

    for (i = 0; i < gearHudMarkerSpeed; i++) {
        if (gearHudMarkerReverseX != 0) {
            gearHudMarkerOffsetX -= gearHudMarkerStepX;
        } else {
            gearHudMarkerOffsetX += gearHudMarkerStepX;
        }
        if (gearHudMarkerReverseY != 0) {
            gearHudMarkerOffsetY -= gearHudMarkerStepY;
        } else {
            gearHudMarkerOffsetY += gearHudMarkerStepY;
        }
    }
    if (gearHudMarkerStepX == 0x100) {
        if (gearHudMarkerReverseX != 0) {
            if (gearHudMarkerOffsetX / 256 + gearHudMarkerStartX < gearHudMarkerEndX) {
                gearHudMarkerPathDone = 1;
            }
        } else if (gearHudMarkerOffsetX / 256 + gearHudMarkerStartX > gearHudMarkerEndX) {
            gearHudMarkerPathDone = 1;
        }
    } else {
        if (gearHudMarkerReverseY != 0) {
            if (gearHudMarkerOffsetY / 256 + gearHudMarkerStartY < gearHudMarkerEndY) {
                gearHudMarkerPathDone = 1;
            }
        } else if (gearHudMarkerOffsetY / 256 + gearHudMarkerStartY > gearHudMarkerEndY) {
            gearHudMarkerPathDone = 1;
        }
    }
}

/* Noah: renderBattleResultText (PSX 0x80088b80) */
void renderBattleResultText(void);
void renderBattleResultText(void) {
    s32 i;
    s32 j;
    s32 k;

    if (battleVar1->mAD_drawGearHud == 0) {
        return;
    }
    if (gearHudMarkerPathDone != 0) {
        if (randomInRange(0, 0x63) >= 0x60) {
            /* the PS1 compiler evaluates arguments left to right, the PC's right to left */
            u8 rx = randomInRange(0, 4);
            u8 ry = randomInRange(0, 4);
            startGearHudMarkerPath(pBattleRenderStruct2->m5D9C_markerX, pBattleRenderStruct2->m5D9E_markerY, gearHudMarkerTargets[rx], gearHudMarkerTargets[ry + 5]);
        }
    }
    if (gearHudMarkerPathDone == 0) {
        stepGearHudMarkerPath();
        pBattleRenderStruct2->m5D9C_markerX = gearHudMarkerStartX + gearHudMarkerOffsetX / 256;
        pBattleRenderStruct2->m5D9E_markerY = gearHudMarkerStartY + gearHudMarkerOffsetY / 256;
    }
    pBattleRenderStruct2->m5D70_count[0x2] = battleSetupStringInPolyFT4Large(0xB9, pBattleRenderStruct2->mD20_marker, pBattleRenderStruct2->m5D9C_markerX, pBattleRenderStruct2->m5D9E_markerY);
    pBattleRenderStruct2->m5D83_oddOrEven[0x11] = D_800CCB04.m30_battleOddOrEven;
    pBattleRenderStruct2->m5D70_count[0x1] = battleSetupStringInPolyFT4Large((pBattleRenderStruct2->m5D9C_markerX & 0xF) + 0xA9, pBattleRenderStruct2->m3C0_markerXFrame, 0xA0, 0x64);
    pBattleRenderStruct2->m5D83_oddOrEven[0x10] = D_800CCB04.m30_battleOddOrEven;
    pBattleRenderStruct2->m5D70_count[0x3] = battleSetupStringInPolyFT4Large(0x82, pBattleRenderStruct2->mFA0_markerOverlay, pBattleRenderStruct2->m5D9C_markerX, pBattleRenderStruct2->m5D9E_markerY);
    pBattleRenderStruct2->m5D83_oddOrEven[0x12] = D_800CCB04.m30_battleOddOrEven;
    for (i = 0; i < pBattleRenderStruct2->m5D70_count[0x2]; i++) {
        battleSetupTextPolyAdditive(&pBattleRenderStruct2->mD20_marker[i * 2 + pBattleRenderStruct2->m5D83_oddOrEven[0x11]]);
    }
    for (i = 0; i < pBattleRenderStruct2->m5D70_count[0x3]; i++) {
        battleSetupTextPolyAdditive(&pBattleRenderStruct2->mFA0_markerOverlay[i * 2 + pBattleRenderStruct2->m5D83_oddOrEven[0x12]]);
    }
    for (i = 0; i < pBattleRenderStruct2->m5D70_count[0x1]; i++) {
        battleSetupTextPolyAdditive(&pBattleRenderStruct2->m3C0_markerXFrame[i * 2 + pBattleRenderStruct2->m5D83_oddOrEven[0x10]]);
    }
    pBattleRenderStruct2->m5D83_oddOrEven[0x13] = battleSetupStringInPolyFT4Large((pBattleRenderStruct2->m5D9E_markerY & 0xF) + 0xC9, pBattleRenderStruct2->m32A0_markerYFrame, pBattleRenderStruct2->m5D9C_markerX, pBattleRenderStruct2->m5D9E_markerY);
    pBattleRenderStruct2->m5D83_oddOrEven[0x14] = D_800CCB04.m30_battleOddOrEven;
    for (i = 0; i < pBattleRenderStruct2->m5D83_oddOrEven[0x13]; i++) {
        battleSetupTextPolySubtractive(&pBattleRenderStruct2->m32A0_markerYFrame[i * 2 + pBattleRenderStruct2->m5D83_oddOrEven[0x14]]);
    }
    if (gearHudMarkerPathDone == 0) {
        for (i = 0; i < 2; i++) {
            for (j = 0; j < 10; j++) {
                k = i * 10 + j;
                battleSetupStringInPolyFT4Large(randomInRange(0, 9) + 0xBA, &pBattleRenderStruct2->m46A0_randomDigits[k * 2], 0x82 + j * 6, 0xA + i * 0xBD);
                battleSetupTextPolyAdditive(&pBattleRenderStruct2->m46A0_randomDigits[k * 2 + D_800CCB04.m30_battleOddOrEven]);
            }
        }
        pBattleRenderStruct2->m5D83_oddOrEven[0x15] = D_800CCB04.m30_battleOddOrEven;
    }
}

/* 263 260] */
void setupGearHudStatusIcons(void) {
    s32 i;
    s32 y;

    pBattleRenderStruct2->m5DA1_count = 0;
    for (i = 0, y = 0x6E << 16; i < 5; i++) {
        if (bitmaskCharacterCheckReverse(battleEntities.m5F24_gearTurn.m24_statusFlags, i)) {
            pBattleRenderStruct2->m5DA1_count += battleSetupStringInPolyFT4Large(i + 0xC4, &pBattleRenderStruct2->m4CE0_statusGlyphs[pBattleRenderStruct2->m5DA1_count * 2], 0xE0, y >> 16);
            y += 10 << 16;
        }
    }
    pBattleRenderStruct2->m5DA0_oddOrEven = D_800CCB04.m30_battleOddOrEven;
    pBattleRenderStruct2->m5DA2_blinkTimer = 0;
}

/* 263 260] */
void setupGearHudBoosterLabel(void) {
    s32 id;
    s32 i;

    id = 0xA0;
    if (battleEntities.m5F24_gearTurn.m2C_boosterActive != 0) {
        id = 0xA1;
    }
    pBattleRenderStruct2->m5D70_count[0x5] = battleSetupStringInPolyFT4Large(id, pBattleRenderStruct2->m3AC0_boosterLabel, 0xA0, 0x64);
    pBattleRenderStruct2->m5D83_oddOrEven[0x1] = D_800CCB04.m30_battleOddOrEven;
    for (i = 0; i < pBattleRenderStruct2->m5D70_count[0x5]; i++) {
        battleSetupTextPolyAdditive(&pBattleRenderStruct2->m3AC0_boosterLabel[i * 2 + pBattleRenderStruct2->m5D83_oddOrEven[0x1]]);
    }
}

void setupGearHudModeLabels(void);
/* battleOddOrEven (s32) read as its low byte: this function loads it with
 * lbu straight from the symbol, twice (through the D_800CCB04 aggregate the
 * address is CSE'd into s0). */
void setupGearHudModeLabels(void) {
    s32 i;
    u8 id0 = battleEntities.m5F24_gearTurn.m28_mode - 0x5D;
    u8 id1 = battleEntities.m5F24_gearTurn.m28_mode - 0x25;

    pBattleRenderStruct2->m5D70_count[0x7] = battleSetupStringInPolyFT4Large(id0, pBattleRenderStruct2->m3E80_modeLabel, 0xA0, 0x64);
    pBattleRenderStruct2->m5D83_oddOrEven[0x3] = battleOddOrEven;
    pBattleRenderStruct2->m5D70_count[0xf] = battleSetupStringInPolyFT4Large(id1, pBattleRenderStruct2->m43D0_modeLabelShadow, 0xA0, 0x64);
    pBattleRenderStruct2->m5D83_oddOrEven[0xb] = battleOddOrEven;
    for (i = 0; i < pBattleRenderStruct2->m5D70_count[0x7]; i++) {
        battleSetupTextPolyAdditive(&pBattleRenderStruct2->m3E80_modeLabel[i * 2 + pBattleRenderStruct2->m5D83_oddOrEven[0x3]]);
    }
    for (i = 0; i < pBattleRenderStruct2->m5D70_count[0xf]; i++) {
        battleSetupTextPolySubtractive(&pBattleRenderStruct2->m43D0_modeLabelShadow[i * 2 + pBattleRenderStruct2->m5D83_oddOrEven[0xb]]);
    }
}

/* 263 260] */
void setupGearHudAttackValue(void) {
    s32 i;
    s32 x;

    printIntegerToStringBattle(battleEntities.m5F24_gearTurn.m1E_attackPower);
    for (i = 0, x = 0x11A << 16; i < 5; i++) {
        if (printIntegerToStringBattleBuffer[i + 4] != 0xFF) {
            pBattleRenderStruct2->m5D70_count[0x8] += battleSetupStringInPolyFT4Large(printIntegerToStringBattleBuffer[i + 4] + 0x92, &pBattleRenderStruct2->m4E70_attackDigits[pBattleRenderStruct2->m5D70_count[0x8] * 2], x >> 16, 0x46);
            x += 6 << 16;
        }
    }
    pBattleRenderStruct2->m5D83_oddOrEven[0x4] = D_800CCB04.m30_battleOddOrEven;
    for (i = 0; i < pBattleRenderStruct2->m5D70_count[0x8]; i++) {
        battleSetupTextPolyAdditive(&pBattleRenderStruct2->m4E70_attackDigits[i * 2 + pBattleRenderStruct2->m5D83_oddOrEven[0x4]]);
    }
}

/* 263 260] */
void setupGearHudInfinityChance(void) {
    s32 i;
    s32 n;
    s32 x;

    if (battleEntities.m5F24_gearTurn.m28_mode < 4) {
        printIntegerToStringBattle(battleEntities.m5F24_gearTurn.m2E_chance);
        for (i = 0, n = 0, x = 0x11A << 16; i < 2; i++) {
            if (printIntegerToStringBattleBuffer[i + 7] != 0xFF) {
                pBattleRenderStruct2->m5D70_count[0x9] += battleSetupStringInPolyFT4Large(printIntegerToStringBattleBuffer[i + 7] + 0x92, &pBattleRenderStruct2->m4FB0_infinityChanceDigits[pBattleRenderStruct2->m5D70_count[0x9] * 2], x >> 16, 0x4E);
                x += 6 << 16;
                n++;
            }
        }
        pBattleRenderStruct2->m5D70_count[0x9] += battleSetupStringInPolyFT4Large(0x9D, &pBattleRenderStruct2->m4FB0_infinityChanceDigits[pBattleRenderStruct2->m5D70_count[0x9] * 2], n * 6 + 0x11A, 0x4E);
    } else {
        pBattleRenderStruct2->m5D70_count[0x9] = battleSetupStringInPolyFT4Large(0xA2, pBattleRenderStruct2->m4FB0_infinityChanceDigits, 0x11A, 0x4E);
    }
    pBattleRenderStruct2->m5D83_oddOrEven[0x5] = D_800CCB04.m30_battleOddOrEven;
    for (i = 0; i < pBattleRenderStruct2->m5D70_count[0x9]; i++) {
        battleSetupTextPolyAdditive(&pBattleRenderStruct2->m4FB0_infinityChanceDigits[i * 2 + pBattleRenderStruct2->m5D83_oddOrEven[0x5]]);
    }
}

/* 263 260] */
void setupGearHudPercentValue(void) {
    s32 i;
    s32 n;
    s32 x;

    printIntegerToStringBattle(battleEntities.m5F24_gearTurn.m2A_defenceDown);
    for (i = 0, n = 0, x = 0x11A << 16; i < 3; i++) {
        if (printIntegerToStringBattleBuffer[i + 6] != 0xFF) {
            pBattleRenderStruct2->m5D70_count[0xa] += battleSetupStringInPolyFT4Large(printIntegerToStringBattleBuffer[i + 6] + 0x92, &pBattleRenderStruct2->m50A0_defenceDownDigits[pBattleRenderStruct2->m5D70_count[0xa] * 2], x >> 16, 0x56);
            x += 6 << 16;
            n++;
        }
    }
    pBattleRenderStruct2->m5D70_count[0xa] += battleSetupStringInPolyFT4Large(0x9D, &pBattleRenderStruct2->m50A0_defenceDownDigits[pBattleRenderStruct2->m5D70_count[0xa] * 2], n * 6 + 0x11A, 0x56);
    pBattleRenderStruct2->m5D83_oddOrEven[0x6] = D_800CCB04.m30_battleOddOrEven;
    for (i = 0; i < pBattleRenderStruct2->m5D70_count[0xa]; i++) {
        battleSetupTextPolyAdditive(&pBattleRenderStruct2->m50A0_defenceDownDigits[i * 2 + pBattleRenderStruct2->m5D83_oddOrEven[0x6]]);
    }
}

/* 263 260] */
void setupGearHudAgilityValue(void) {
    s32 i;
    s32 x;

    printIntegerToStringBattle(battleEntities.m5F24_gearTurn.m29_agility);
    for (i = 0, x = 0x11A << 16; i < 2; i++) {
        if (printIntegerToStringBattleBuffer[i + 7] != 0xFF) {
            pBattleRenderStruct2->m5D70_count[0xb] += battleSetupStringInPolyFT4Large(printIntegerToStringBattleBuffer[i + 7] + 0x92, &pBattleRenderStruct2->m51E0_agilityDigits[pBattleRenderStruct2->m5D70_count[0xb] * 2], x >> 16, 0x5E);
            x += 6 << 16;
        }
    }
    pBattleRenderStruct2->m5D83_oddOrEven[0x7] = D_800CCB04.m30_battleOddOrEven;
    for (i = 0; i < pBattleRenderStruct2->m5D70_count[0xb]; i++) {
        battleSetupTextPolyAdditive(&pBattleRenderStruct2->m51E0_agilityDigits[i * 2 + pBattleRenderStruct2->m5D83_oddOrEven[0x7]]);
    }
}

/* 263 260] */
void updateGearHudFuel(u8 entity) {
    s32 i;
    s32 x;

    pBattleRenderStruct2->m5D70_count[0xc] = 0;
    printIntegerToStringBattle(battleEntities.m0_entities[entity].mA4_gear.m38_fuel);
    for (i = 0, x = 0x20 << 16; i < 4; i++) {
        if (printIntegerToStringBattleBuffer[i + 5] != 0xFF) {
            pBattleRenderStruct2->m5D70_count[0xc] += battleSetupStringInPolyFT4Large(printIntegerToStringBattleBuffer[i + 5] + 0x92, &pBattleRenderStruct2->m5280_fuelDigits[pBattleRenderStruct2->m5D70_count[0xc] * 2], x >> 16, 0xCC);
        }
        x += 8 << 16;
    }
    pBattleRenderStruct2->m5D83_oddOrEven[0x8] = D_800CCB04.m30_battleOddOrEven;
    pBattleRenderStruct2->m5D70_count[0xd] = 0;
    printIntegerToStringBattle(battleEntities.m0_entities[entity].mA4_gear.m3A_maxFuel);
    for (i = 0, x = 0x48 << 16; i < 4; i++) {
        if (printIntegerToStringBattleBuffer[i + 5] != 0xFF) {
            pBattleRenderStruct2->m5D70_count[0xd] += battleSetupStringInPolyFT4Large(printIntegerToStringBattleBuffer[i + 5] + 0x92, &pBattleRenderStruct2->m53C0_maxFuelDigits[pBattleRenderStruct2->m5D70_count[0xd] * 2], x >> 16, 0xCC);
            x += 8 << 16;
        }
    }
    pBattleRenderStruct2->m5D70_count[0xd] += battleSetupStringInPolyFT4Large(0x9C, &pBattleRenderStruct2->m53C0_maxFuelDigits[pBattleRenderStruct2->m5D70_count[0xd] * 2], 0x41, 0xCC);
    pBattleRenderStruct2->m5D83_oddOrEven[0x9] = D_800CCB04.m30_battleOddOrEven;
}

/* 72 263 260] */
void setupGearHudTexts(u8 entity) { /* the caller passes the entity; unused */
    setupGearHudFrame();
    setupGearHudStatusIcons();
    setupGearHudBoosterLabel();
    setupGearHudModeLabels();
    setupGearHudAttackValue();
    setupGearHudInfinityChance();
    setupGearHudPercentValue();
    setupGearHudAgilityValue();
}
