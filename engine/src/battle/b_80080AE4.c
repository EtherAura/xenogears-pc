/*
 * Battle command menus, target selection, damage bookkeeping and the result
 * screen (Noah battle.cpp). 0x80080AE4-0x8008B478; gcc-2.6.3-psx, ASPSX 2.34.
 * Retail builds 0x80080AE4-0x80086028 and 0x80086028-0x8008B478 as different
 * source files (see isGearComboAttackAvailable's caller and battleOddOrEven's views).
 */
#include "common.h"
#include "psyq.h"
#include "main/game_state.h"
#include "main/filesystem.h"
#include "main/memory.h"
#include "main/dialog.h"
#include "main/debug_font.h"
#include "battle/battle.h"

extern sBattle800cdd40 partyBattleStats[3] PORT_ALIAS;
extern sBattle800CEF10 mechaBattleStats[3];
extern BattleBss_800D2C0A D_800D2C0A;            /* gear fuel costs ... */
extern sBattleMechaInitData* battleMechaInitData;
extern s32 battleOddOrEven;
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

s32 computeFacingForJump(u8 arg0) {
    s32 result;
    s32 minTicks;
    s32 i;
    s32 entity;
    s16* p;

    minTicks = 0xFF;
    i = isBattleSlotFilled.mB_currentEntryInRandomTurnOrder;
    do {
        entity = isBattleSlotFilled.mC_randomTurnOrder[i];
        p = &isBattleSlotFilled.m3A_numTicksBeforeReady[entity];
        i++;
        if (*p < minTicks && entity != arg0) {
            result = entity;
            minTicks = *(u8*)p;
        }
        if (i == 11) {
            i = 0;
        }
    } while (i != isBattleSlotFilled.mB_currentEntryInRandomTurnOrder);
    return result;
}

void handleMenuSelectEnemy_cancel_sub0(u8 arg0) {
    battleCurrentDamages[battleVar2->m2DA_indexInBattleVar48].m47_battleAnimationToPlay = 0xFE;
    battleCurrentDamages[battleVar2->m2DA_indexInBattleVar48].m23_battleEntityIndex = arg0;
    startCharacterJumpToEnemyVar0 = 0;
}

/* ends the current turn (m2DB=1) and refreshes the PC's menu state */
void freePlayerTurnStructs(void) {
    battleVar2->m2DB_turnEnded = 1;
    if (battleVar2->m2D3_currentEntityTurn < 3 && autoPartyMemberTurnActive == 0) {
        freeBattleVar1_AE();
        freeBattleVar1_96();
        freeBattleRenderStruct2(battleVar2->m2D3_currentEntityTurn);
        freeBillyBattleStructs(battleVar2->m2D3_currentEntityTurn);
    }
}

/* sets script entity arg0's m34 = 1 */
void mecha_battle_op3_2(u8 arg0) {
    sBattleRunningVar0Sub* p = battleEventVar0->m0_scriptEntities;
    p += arg0;
    p->m34_mechaSetupState = 1;
}

/* Automatic turn of a party member (random valid target) */
void performAutoPartyMemberTurn(u8 arg0) {
    u8 ret;
    u8 cmd[8];
    u8 targets[11];
    s32 i;
    u8* p;

    ret = 1;
    battleRenderDebugAndMain();
    for (i = 0; i < 11; i++) {
        targets[i] = 0xFF;
    }
    battleVar2->m2EA_notDefending = 1;
    battleVar2->m2DE_actionDone = 0;
    clearEntityFlag15A_1(arg0);
    chooseAutoBattleCommand(arg0, cmd, &battleVar2->m0_entityMenus[arg0].m1C_isCommandEnabled[7]);
    if ((u8)(cmd[0] - 1) < 2) {
        for (i = 0, p = targets; i < 11; i++) {
            if (isTargetValid(arg0, i) != 0) {
                *p++ = i;
            }
        }
        while ((abilitySelectedTarget = targets[randomInRange(0, 10)]) == 0xFF || abilitySelectedTarget == arg0) {
        }
    }
    initAnimSeqFromCharacterToCharacter(arg0, abilitySelectedTarget);
    battleVar2->m0_entityMenus[arg0].m3C_currentTarget = abilitySelectedTarget;
    setCameraVisibleEntities(characterIdToTargetBitmask(arg0) | characterIdToTargetBitmask(battleVar2->m0_entityMenus[arg0].m3C_currentTarget));
    battleRenderDebugAndMain();
    autoPartyMemberTurnActive = 1;
    switch (cmd[0]) {
    case 1:
        startCharacterJumpToEnemyVar1 = 0;
        if (battleCharacters[arg0] != 4) {
            battleCurrentDamages[battleVar2->m2DA_indexInBattleVar48].m23_battleEntityIndex = arg0;
            battleCurrentDamages[battleVar2->m2DA_indexInBattleVar48].m47_battleAnimationToPlay = 0xFD;
            battleCurrentDamages[battleVar2->m2DA_indexInBattleVar48].m3A_animationParam = 0;
            battleCurrentDamages[battleVar2->m2DA_indexInBattleVar48].m16_targetBitMask = characterIdToTargetBitmask(battleVar2->m0_entityMenus[arg0].m3C_currentTarget);
            battleVar2->m2DA_indexInBattleVar48++;
        }
        if (apConfigArray[arg0].m1_gearState == 0) {
            battleVar2->m2DC_attackIndex = 0;
        } else {
            battleVar2->m2DC_attackIndex = cmd[1];
        }
        startJumpAnimation(initJumpData(arg0, battleVar2->m0_entityMenus[arg0].m3C_currentTarget), arg0,
                      battleVar2->m0_entityMenus[arg0].m3C_currentTarget, computeFacingForJump(arg0));
        performAttack(arg0, cmd[1] + 1);
        handleMenuSelectEnemy_cancel_sub0(arg0);
        ret = 0;
        break;
    case 2:
        if (partyBattleStats[arg0].m0_commands[cmd[1] + 0x16].m13_cost <= battleEntities.m0_entities[arg0].m0_base.m50_MP) {
            battleEntities.m0_entities[arg0].m0_base.m50_MP -= partyBattleStats[arg0].m0_commands[cmd[1] + 0x16].m13_cost;
            battleVar2->m2E6_menuSelection = cmd[1];
            executeAbility(arg0);
            ret = 0;
        }
        break;
    case 4:
        callGear(arg0);
        ret = 0;
        battleVar2->m2DE_actionDone = 1;
        break;
    }
    if (ret) {
        characterPerformDefendAction(arg0);
        battleVar2->m2EA_notDefending = 0;
        battleVar2->m2DE_actionDone = 1;
    }
    while (battleVar2->m2DE_actionDone == 0 && battleRunningVar1[0] == 0 && battleVar2->m2DB_turnEnded == 0) {
        battleRenderDebugAndMain();
    }
    autoPartyMemberTurnActive = 0;
}

void drawCircleMenuAttackActive(s32 arg0) {
    switch (battleInputButton) {
    case 4:
    case 6:
    case 7: {
        u8 idx = arg0;
        if (battleVar2->m0_entityMenus[idx].m3C_currentTarget != 0xFF) {
            startCharacterJumpToEnemyVar0 = 0;
            startCharacterJumpToEnemy(idx);
            selectDefaultTarget(idx);
            initBattleG3Array();
            battleVar2->m2DD_currentActiveBattleMenu = 5;
            return;
        }
        battleSoundEffect2(0x4F);
        return;
    }
    case 1:
        if (battleVar2->m0_entityMenus[arg0 & 0xFF].m1C_isCommandEnabled[11] == 0) {
            battleVar2->m2DD_currentActiveBattleMenu = 2;
            return;
        }
        battleSoundEffect2(0x4F);
        return;
    case 2:
        battleVar2->m2DD_currentActiveBattleMenu = 3;
        return;
    case 3:
        if (battleVar2->m0_entityMenus[arg0 & 0xFF].m1C_isCommandEnabled[10] == 0) {
            battleVar2->m2DD_currentActiveBattleMenu = 4;
            return;
        }
        if (battleVar2->m2F6_doublePressPending != 0 && newBattleInputButton2 == 3) {
            if (battleVar2->m0_entityMenus[arg0 & 0xFF].m1C_isCommandEnabled[7] != 0) {
                battleSoundEffect2(0x4F);
            } else {
                battleVar2->m2DD_currentActiveBattleMenu = 10;
            }
            battleVar2->m2F6_doublePressPending = 0;
            return;
        }
        battleVar2->m2F6_doublePressPending = 1;
        battleSoundEffect2(0x4F);
        return;
    case 0:
        if (battleVar2->m0_entityMenus[arg0 & 0xFF].m1C_isCommandEnabled[5] == 0) {
            battleVar2->m2DD_currentActiveBattleMenu = 7;
            return;
        }
        battleSoundEffect2(0x4F);
        return;
    }
}

void drawCircleMenuItemActive(s32 arg0) {
    switch (battleInputButton) {
    case 4:
    case 6:
    case 7: {
        u8 idx = arg0;
        if (drawCircleMenuItem_update(idx) != 0) {
            executeItem(idx);
        } else {
            battleVar2->m2DD_currentActiveBattleMenu = 2;
        }
        return;
    }
    case 0:
        if (battleVar2->m0_entityMenus[arg0 & 0xFF].m1C_isCommandEnabled[9] == 0) {
            battleVar2->m2DD_currentActiveBattleMenu = 1;
            return;
        }
        if (battleVar2->m2F6_doublePressPending != 0 && newBattleInputButton2 == 0) {
            if (battleVar2->m0_entityMenus[arg0 & 0xFF].m1C_isCommandEnabled[5] != 0) {
                battleSoundEffect2(0x4F);
            } else {
                battleVar2->m2DD_currentActiveBattleMenu = 7;
            }
            battleVar2->m2F6_doublePressPending = 0;
            return;
        }
        battleVar2->m2F6_doublePressPending = 1;
        battleSoundEffect2(0x4F);
        return;
    case 2:
        battleVar2->m2DD_currentActiveBattleMenu = 3;
        return;
    case 3:
        if (battleVar2->m0_entityMenus[arg0 & 0xFF].m1C_isCommandEnabled[10] == 0) {
            battleVar2->m2DD_currentActiveBattleMenu = 4;
            return;
        }
        if (battleVar2->m2F6_doublePressPending != 0 && newBattleInputButton2 == 3) {
            if (battleVar2->m0_entityMenus[arg0 & 0xFF].m1C_isCommandEnabled[7] != 0) {
                battleSoundEffect2(0x4F);
            } else {
                battleVar2->m2DD_currentActiveBattleMenu = 10;
            }
            battleVar2->m2F6_doublePressPending = 0;
            return;
        }
        battleVar2->m2F6_doublePressPending = 1;
        battleSoundEffect2(0x4F);
        return;
    case 1:
        if (battleVar2->m0_entityMenus[arg0 & 0xFF].m1C_isCommandEnabled[11] == 0) {
            battleVar2->m2DD_currentActiveBattleMenu = 8;
            return;
        }
        battleSoundEffect2(0x4F);
        return;
    }
}

void drawCircleMenuDefend(s32 arg0) {
    switch (battleInputButton) {
    case 4:
    case 6:
    case 7:
        characterPerformDefendAction(arg0);
        battleVar2->m2EA_notDefending = 0;
        battleVar2->m2DE_actionDone = 1;
        return;
    case 0:
        if (battleVar2->m0_entityMenus[arg0 & 0xFF].m1C_isCommandEnabled[9] == 0) {
            battleVar2->m2DD_currentActiveBattleMenu = 1;
            return;
        }
        if (battleVar2->m2F6_doublePressPending != 0 && newBattleInputButton2 == 0) {
            if (battleVar2->m0_entityMenus[arg0 & 0xFF].m1C_isCommandEnabled[5] != 0) {
                battleSoundEffect2(0x4F);
            } else {
                battleVar2->m2DD_currentActiveBattleMenu = 7;
            }
            battleVar2->m2F6_doublePressPending = 0;
            return;
        }
        battleVar2->m2F6_doublePressPending = 1;
        battleSoundEffect2(0x4F);
        return;
    case 1:
        if (battleVar2->m0_entityMenus[arg0 & 0xFF].m1C_isCommandEnabled[11] == 0) {
            battleVar2->m2DD_currentActiveBattleMenu = 2;
            return;
        }
        battleSoundEffect2(0x4F);
        return;
    case 3:
        if (battleVar2->m0_entityMenus[arg0 & 0xFF].m1C_isCommandEnabled[10] == 0) {
            battleVar2->m2DD_currentActiveBattleMenu = 4;
            return;
        }
        if (battleVar2->m2F6_doublePressPending != 0 && newBattleInputButton2 == 3) {
            if (battleVar2->m0_entityMenus[arg0 & 0xFF].m1C_isCommandEnabled[7] != 0) {
                battleSoundEffect2(0x4F);
            } else {
                battleVar2->m2DD_currentActiveBattleMenu = 10;
            }
            battleVar2->m2F6_doublePressPending = 0;
            return;
        }
        battleVar2->m2F6_doublePressPending = 1;
        battleSoundEffect2(0x4F);
        return;
    case 2:
        if (battleVar2->m0_entityMenus[arg0 & 0xFF].m1C_isCommandEnabled[8] == 0) {
            battleVar2->m2DD_currentActiveBattleMenu = 9;
            return;
        }
        battleSoundEffect2(0x4F);
        return;
    }
}

void drawCircleMenuChi(s32 arg0) {
    switch (battleInputButton) {
    case 4:
    case 6:
    case 7: {
        u8 idx = arg0;
        drawCircleMenuChi_loadData(idx);
        if (drawCircleMenuChi_update(idx) != 0) {
            executeAbility(idx);
        } else {
            battleVar2->m2DD_currentActiveBattleMenu = 4;
        }
        return;
    }
    case 0:
        if (battleVar2->m0_entityMenus[arg0 & 0xFF].m1C_isCommandEnabled[9] == 0) {
            battleVar2->m2DD_currentActiveBattleMenu = 1;
            return;
        }
        if (battleVar2->m2F6_doublePressPending != 0 && newBattleInputButton2 == 0) {
            if (battleVar2->m0_entityMenus[arg0 & 0xFF].m1C_isCommandEnabled[5] != 0) {
                battleSoundEffect2(0x4F);
            } else {
                battleVar2->m2DD_currentActiveBattleMenu = 7;
            }
            battleVar2->m2F6_doublePressPending = 0;
            return;
        }
        battleVar2->m2F6_doublePressPending = 1;
        battleSoundEffect2(0x4F);
        return;
    case 1:
        if (battleVar2->m0_entityMenus[arg0 & 0xFF].m1C_isCommandEnabled[11] == 0) {
            battleVar2->m2DD_currentActiveBattleMenu = 2;
            return;
        }
        battleSoundEffect2(0x4F);
        return;
    case 2:
        battleVar2->m2DD_currentActiveBattleMenu = 3;
        return;
    case 3:
        if (battleVar2->m0_entityMenus[arg0 & 0xFF].m1C_isCommandEnabled[7] == 0) {
            battleVar2->m2DD_currentActiveBattleMenu = 10;
            return;
        }
        battleSoundEffect2(0x4F);
        return;
    }
}

void updateTargetSelectionMarker(u8 arg0) {
    s32 i;

    if (battleVar2->m2E9_targetConfirmed == 0) {
        setCameraVisibleEntities(characterIdToTargetBitmask(battleVar2->m2E8_currentTarget));
        updateCharacterBlinkingTask(characterIdToTargetBitmask(battleVar2->m2E8_currentTarget));
        for (i = 0; i < 4; i++) {
            if (selectNewSlotByDirection(battleVar2->m2E8_currentTarget, i) != battleVar2->m2E8_currentTarget) {
                battleG3->mE6_isDirectionEnabled[i] = 1;
            } else {
                battleG3->mE6_isDirectionEnabled[i] = 0;
            }
        }
        if (D_800D2C0A.m2A_mode != 4) {
            updateTargetSelectionMarkerSub1(arg0);
        }
    }
}

/* Attack-target confirm: stores the target, queues damage entry 0xFD */
void removeBattleCursorDuringAttack(u8 arg0) {
    if (battleVar2->m2E9_targetConfirmed == 0) {
        battleVar2->m2E9_targetConfirmed = 1;
        updateCharacterBlinkingTask(0);
        battleVar2->m0_entityMenus[arg0].m3C_currentTarget = battleVar2->m2E8_currentTarget;
        freeBattleVar1_AE();
        freeBattleVar1_96();
        removeTargetSelectionCursors();
        setCameraVisibleEntities(characterIdToTargetBitmask(arg0) | characterIdToTargetBitmask(battleVar2->m0_entityMenus[arg0].m3C_currentTarget));
        if (battleCharacters[arg0] != 4) {
            battleCurrentDamages[battleVar2->m2DA_indexInBattleVar48].m23_battleEntityIndex = arg0;
            battleCurrentDamages[battleVar2->m2DA_indexInBattleVar48].m47_battleAnimationToPlay = 0xFD;
            battleCurrentDamages[battleVar2->m2DA_indexInBattleVar48].m3A_animationParam = 0;
            battleCurrentDamages[battleVar2->m2DA_indexInBattleVar48].m16_targetBitMask = characterIdToTargetBitmask(battleVar2->m0_entityMenus[arg0].m3C_currentTarget);
            battleVar2->m2DA_indexInBattleVar48++;
        }
        removeBillyWindow(arg0, 0);
    }
}

s32 handleMenuSelectEnemy(u8 arg0) {
    startCharacterJumpToEnemyVar0 = 0;
    if (battleVar2->m2E1_waitBeforeNextAttackInput != 0) {
        IMPLICIT_RETURN(0);
    }
    battleVar2->m2DF_currentAttackNumOfApUsed = 0;
    updateTargetSelectionMarker(arg0);
    switch (battleInputButton) {
    case 5:
        if (battleVar2->m2D4_remainingAP != battleVar2->m2D5_maxAP) {
            handleMenuSelectEnemy_cancel_sub0(arg0);
            battleVar2->m2E1_waitBeforeNextAttackInput = 1;
        } else {
            cancelJumpAnim();
            startCharacterJumpToEnemyVar0 = 1;
            battleVar1->m7B_apCounterLength = 0;
            battleVar1->mAF_drawApCounter = 0;
            battleVar2->m2DD_currentActiveBattleMenu = 1;
            removeTargetSelectionCursors();
            removeBillyWindow(arg0, 1);
        }
        break;
    case 4:
        if (battleVar2->m0_entityMenus[arg0].m1C_isCommandEnabled[2] != 0) {
            battleInputButton = 5;
            battleSoundEffect(0x4F);
        }
        break;
    case 7:
        if (battleVar2->m0_entityMenus[arg0].m1C_isCommandEnabled[1] != 0) {
            battleInputButton = 5;
            battleSoundEffect(0x4F);
        }
        break;
    case 6:
        if (battleVar2->m0_entityMenus[arg0].m1C_isCommandEnabled[0] != 0) {
            battleInputButton = 5;
            battleSoundEffect(0x4F);
        }
        break;
    }
    if (battleVar2->m2E9_targetConfirmed == 0) {
        switch (battleInputButton) {
        case 0:
            battleVar2->m2E8_currentTarget = selectNewSlotByDirection(battleVar2->m2E8_currentTarget, 0);
            initAnimSeqFromCharacterToCharacter(battleVar2->m2D3_currentEntityTurn, battleVar2->m2E8_currentTarget);
            battleSoundEffect(0x4C);
            break;
        case 1:
            battleVar2->m2E8_currentTarget = selectNewSlotByDirection(battleVar2->m2E8_currentTarget, 1);
            initAnimSeqFromCharacterToCharacter(battleVar2->m2D3_currentEntityTurn, battleVar2->m2E8_currentTarget);
            battleSoundEffect(0x4C);
            break;
        case 2:
            battleVar2->m2E8_currentTarget = selectNewSlotByDirection(battleVar2->m2E8_currentTarget, 2);
            initAnimSeqFromCharacterToCharacter(battleVar2->m2D3_currentEntityTurn, battleVar2->m2E8_currentTarget);
            battleSoundEffect(0x4C);
            break;
        case 3:
            battleVar2->m2E8_currentTarget = selectNewSlotByDirection(battleVar2->m2E8_currentTarget, 3);
            initAnimSeqFromCharacterToCharacter(battleVar2->m2D3_currentEntityTurn, battleVar2->m2E8_currentTarget);
            battleSoundEffect(0x4C);
            break;
        }
        initJumpData(arg0, battleVar2->m2E8_currentTarget);
    }
    battleVar1->mAF_drawApCounter = 1;
    switch (battleInputButton) {
    case 5:
        IMPLICIT_RETURN(0);
    case 4:
        if (handleMenuSelectEnemySub(4, arg0) != 0 && battleVar2->m2D4_remainingAP - 3 >= 0) {
            battleVar2->m2E4_deathblowCompleted++;
        }
        battleVar2->m2DF_currentAttackNumOfApUsed++;
    case 7:
        battleVar2->m2DF_currentAttackNumOfApUsed++;
    case 6:
        battleVar2->m2DF_currentAttackNumOfApUsed++;
        if (battleVar2->m2D4_remainingAP - battleVar2->m2DF_currentAttackNumOfApUsed >= 0) {
            battleSoundEffect(0x4D);
            removeBattleCursorDuringAttack(arg0);
            if (battleVar1->mCB_drawCommandRing != 0) {
                battleVar2->m2DD_currentActiveBattleMenu = 100;
                battleVar2->m2E2_previousActiveBattleMenu = 0xFF;
                battleVar2->m2E0_isBattleMenuDisabled = 1;
            } else {
                battleVar2->m2DD_currentActiveBattleMenu = 5;
                battleVar2->m2E2_previousActiveBattleMenu = 5;
            }
            battleVar2->m2D4_remainingAP -= battleVar2->m2DF_currentAttackNumOfApUsed;
            updateAPCounterDisplay();
            updateDeathblowComboInput(battleInputButton, arg0);
            battleVar2->m2E5_deathblowPerformed = performAttack(arg0, battleVar2->m2DF_currentAttackNumOfApUsed);
            isBattleSlotFilled.m3A_numTicksBeforeReady[arg0] =
                apTurnDelayTicks[battleVar2->m2D5_maxAP][battleVar2->m2D4_remainingAP] * 100 / 56;
            battleVar2->m2E3_hasAttacked = 1;
        } else {
            battleSoundEffect(0x4F);
        }
        if (battleVar2->m2D4_remainingAP == 0) {
            handleMenuSelectEnemy_cancel_sub0(arg0);
            battleVar2->m2E1_waitBeforeNextAttackInput = 1;
        }
        if (battleVar2->m2E5_deathblowPerformed != 0) {
            handleMenuSelectEnemy_cancel_sub0(arg0);
            battleVar2->m2E1_waitBeforeNextAttackInput = 1;
        }
        break;
    }
    FALLOFF_RETURN(0); /* result unused */
}

void drawCircleMenuCombo(s32 arg0) {
    switch (battleInputButton) {
    case 4:
    case 6:
    case 7:
        arg0 &= 0xFF;
        if (battleVar2->m0_entityMenus[arg0].m3C_currentTarget != 0xFF) {
            if (useCombo(arg0) != 0) {
                battleVar2->m2E1_waitBeforeNextAttackInput = 1;
                return;
            }
            battleVar2->m2DD_currentActiveBattleMenu = 7;
            battleVar2->m2E2_previousActiveBattleMenu = 0xFF;
            return;
        }
        battleSoundEffect2(0x4F);
        return;
    case 1:
        if (battleVar2->m0_entityMenus[arg0 & 0xFF].m1C_isCommandEnabled[11] == 0) {
            battleVar2->m2DD_currentActiveBattleMenu = 8;
            return;
        }
        battleSoundEffect2(0x4F);
        return;
    case 2:
        if (battleVar2->m0_entityMenus[arg0 & 0xFF].m1C_isCommandEnabled[8] == 0) {
            battleVar2->m2DD_currentActiveBattleMenu = 9;
            return;
        }
        if (battleVar2->m2F6_doublePressPending != 0 && newBattleInputButton2 == 2) {
            battleVar2->m2DD_currentActiveBattleMenu = 3;
            battleVar2->m2F6_doublePressPending = 0;
            return;
        }
        battleVar2->m2F6_doublePressPending = 1;
        battleSoundEffect2(0x4F);
        return;
    case 3:
        if (battleVar2->m0_entityMenus[arg0 & 0xFF].m1C_isCommandEnabled[7] == 0) {
            battleVar2->m2DD_currentActiveBattleMenu = 10;
            return;
        }
        if (battleVar2->m2F6_doublePressPending != 0 && newBattleInputButton2 == 3) {
            if (battleVar2->m0_entityMenus[arg0 & 0xFF].m1C_isCommandEnabled[10] != 0) {
                battleSoundEffect2(0x4F);
            } else {
                battleVar2->m2DD_currentActiveBattleMenu = 4;
            }
            battleVar2->m2F6_doublePressPending = 0;
            return;
        }
        battleVar2->m2F6_doublePressPending = 1;
        battleSoundEffect2(0x4F);
        return;
    case 0:
        if (battleVar2->m0_entityMenus[arg0 & 0xFF].m1C_isCommandEnabled[9] == 0) {
            battleVar2->m2DD_currentActiveBattleMenu = 1;
            return;
        }
        battleSoundEffect2(0x4F);
        return;
    }
}

void drawCircleMenuItemSecondaryActive(s32 arg0) {
    switch (battleInputButton) {
    case 4:
    case 6:
    case 7: {
        u8 idx = arg0;
        if (drawCircleMenuItem_update(idx) != 0) {
            executeItem(idx);
        } else {
            battleVar2->m2DD_currentActiveBattleMenu = 8;
        }
        return;
    }
    case 0:
        if (battleVar2->m0_entityMenus[arg0 & 0xFF].m1C_isCommandEnabled[5] == 0) {
            battleVar2->m2DD_currentActiveBattleMenu = 7;
            return;
        }
        if (battleVar2->m2F6_doublePressPending != 0 && newBattleInputButton2 == 0) {
            if (battleVar2->m0_entityMenus[arg0 & 0xFF].m1C_isCommandEnabled[9] != 0) {
                battleSoundEffect2(0x4F);
            } else {
                battleVar2->m2DD_currentActiveBattleMenu = 1;
            }
            battleVar2->m2F6_doublePressPending = 0;
            return;
        }
        battleVar2->m2F6_doublePressPending = 1;
        battleSoundEffect2(0x4F);
        return;
    case 2:
        if (battleVar2->m0_entityMenus[arg0 & 0xFF].m1C_isCommandEnabled[8] == 0) {
            battleVar2->m2DD_currentActiveBattleMenu = 9;
            return;
        }
        if (battleVar2->m2F6_doublePressPending != 0 && newBattleInputButton2 == 2) {
            battleVar2->m2DD_currentActiveBattleMenu = 3;
            battleVar2->m2F6_doublePressPending = 0;
            return;
        }
        battleVar2->m2F6_doublePressPending = 1;
        battleSoundEffect2(0x4F);
        return;
    case 3:
        if (battleVar2->m0_entityMenus[arg0 & 0xFF].m1C_isCommandEnabled[7] == 0) {
            battleVar2->m2DD_currentActiveBattleMenu = 10;
            return;
        }
        if (battleVar2->m2F6_doublePressPending != 0 && newBattleInputButton2 == 3) {
            if (battleVar2->m0_entityMenus[arg0 & 0xFF].m1C_isCommandEnabled[10] != 0) {
                battleSoundEffect2(0x4F);
            } else {
                battleVar2->m2DD_currentActiveBattleMenu = 4;
            }
            battleVar2->m2F6_doublePressPending = 0;
            return;
        }
        battleVar2->m2F6_doublePressPending = 1;
        battleSoundEffect2(0x4F);
        return;
    case 1:
        if (battleVar2->m0_entityMenus[arg0 & 0xFF].m1C_isCommandEnabled[11] == 0) {
            battleVar2->m2DD_currentActiveBattleMenu = 2;
            return;
        }
        battleSoundEffect2(0x4F);
        return;
    }
}

void drawBattleMenuEscape(s32 arg0) {
    switch (battleInputButton) {
    case 4:
    case 6:
    case 7:
        if (tryToEscape(battleVar2->m2D3_currentEntityTurn) != 0) {
            battleRunningVar1[0] = 0x40;
        }
        battleVar2->m2DE_actionDone = 1;
        return;
    case 0:
        if (battleVar2->m0_entityMenus[arg0 & 0xFF].m1C_isCommandEnabled[5] == 0) {
            battleVar2->m2DD_currentActiveBattleMenu = 7;
            return;
        }
        if (battleVar2->m2F6_doublePressPending != 0 && newBattleInputButton2 == 0) {
            if (battleVar2->m0_entityMenus[arg0 & 0xFF].m1C_isCommandEnabled[9] != 0) {
                battleSoundEffect2(0x4F);
            } else {
                battleVar2->m2DD_currentActiveBattleMenu = 1;
            }
            battleVar2->m2F6_doublePressPending = 0;
            return;
        }
        battleVar2->m2F6_doublePressPending = 1;
        battleSoundEffect2(0x4F);
        return;
    case 1:
        if (battleVar2->m0_entityMenus[arg0 & 0xFF].m1C_isCommandEnabled[11] == 0) {
            battleVar2->m2DD_currentActiveBattleMenu = 8;
            return;
        }
        battleSoundEffect2(0x4F);
        return;
    case 3:
        if (battleVar2->m0_entityMenus[arg0 & 0xFF].m1C_isCommandEnabled[7] == 0) {
            battleVar2->m2DD_currentActiveBattleMenu = 10;
            return;
        }
        if (battleVar2->m2F6_doublePressPending != 0 && newBattleInputButton2 == 3) {
            if (battleVar2->m0_entityMenus[arg0 & 0xFF].m1C_isCommandEnabled[10] != 0) {
                battleSoundEffect2(0x4F);
            } else {
                battleVar2->m2DD_currentActiveBattleMenu = 4;
            }
            battleVar2->m2F6_doublePressPending = 0;
            return;
        }
        battleVar2->m2F6_doublePressPending = 1;
        battleSoundEffect2(0x4F);
        return;
    case 2:
        battleVar2->m2DD_currentActiveBattleMenu = 3;
        return;
    }
}

void callGear(u8 arg0) {
    s32 i;

    callGearRelocateSlot(arg0);
    callGearClearStatus(arg0);
    callGearSwapSprite(arg0);
    battleEntities.m0_entities[arg0].m15A_flags |= 0x80;
    markEnemyDead(arg0);
    apConfigArray[arg0].m1_gearState = 2;
    i = 0;
    if (battleCharacters[arg0] != 7) {
        battleVar0->m835C_apFuelLabels[arg0].m1E1_barMode = 2;
    }
    battleVisualEntities[arg0].m4_isGear = 1;
    battleVar2->m2EB_hpChanged[arg0] = 1;
    do {
        if (gameState.m1D34_currentParty[i] == battleCharacters[arg0] && gearSwitchDisabled == 0) {
            gameState.m22B1_isOnGear[i] = 1;
        }
        i++;
    } while (i < 3);
}

void drawCircleMenuCallGear(s32 arg0) {
    switch (battleInputButton) {
    case 4:
    case 6:
    case 7:
        callGear(arg0);
        battleVar2->m2DE_actionDone = 1;
        return;
    case 0:
        if (battleVar2->m0_entityMenus[arg0 & 0xFF].m1C_isCommandEnabled[5] == 0) {
            battleVar2->m2DD_currentActiveBattleMenu = 7;
            return;
        }
        if (battleVar2->m2F6_doublePressPending != 0 && newBattleInputButton2 == 0) {
            if (battleVar2->m0_entityMenus[arg0 & 0xFF].m1C_isCommandEnabled[9] != 0) {
                battleSoundEffect2(0x4F);
            } else {
                battleVar2->m2DD_currentActiveBattleMenu = 1;
            }
            battleVar2->m2F6_doublePressPending = 0;
            return;
        }
        battleVar2->m2F6_doublePressPending = 1;
        battleSoundEffect2(0x4F);
        return;
    case 1:
        if (battleVar2->m0_entityMenus[arg0 & 0xFF].m1C_isCommandEnabled[11] == 0) {
            battleVar2->m2DD_currentActiveBattleMenu = 8;
            return;
        }
        battleSoundEffect2(0x4F);
        return;
    case 2:
        if (battleVar2->m0_entityMenus[arg0 & 0xFF].m1C_isCommandEnabled[8] == 0) {
            battleVar2->m2DD_currentActiveBattleMenu = 9;
            return;
        }
        if (battleVar2->m2F6_doublePressPending != 0 && newBattleInputButton2 == 2) {
            battleVar2->m2DD_currentActiveBattleMenu = 3;
            battleVar2->m2F6_doublePressPending = 0;
            return;
        }
        battleVar2->m2F6_doublePressPending = 1;
        battleSoundEffect2(0x4F);
        return;
    case 3:
        if (battleVar2->m0_entityMenus[arg0 & 0xFF].m1C_isCommandEnabled[10] == 0) {
            battleVar2->m2DD_currentActiveBattleMenu = 4;
            return;
        }
        battleSoundEffect2(0x4F);
        return;
    }
}

/* Gear version of drawCircleMenuAttackActive (menu 0x10) */
void drawGearCircleMenuAttackActive(s32 arg0) {
    switch (battleInputButton) {
    case 4:
    case 6:
    case 7: {
        u8 idx = arg0;
        if (battleVar2->m0_entityMenus[idx].m3C_currentTarget != 0xFF) {
            startCharacterJumpToEnemy(idx);
            selectDefaultTarget(idx);
            battleVar2->m2DD_currentActiveBattleMenu = 0x19;
            initBattleG3Array();
            startCharacterJumpToEnemyVar0 = 0;
            return;
        }
        battleSoundEffect2(0x4F);
        return;
    }
    case 1:
        if (battleVar2->m0_entityMenus[arg0 & 0xFF].m1C_isCommandEnabled[12] == 0) {
            battleVar2->m2DD_currentActiveBattleMenu = 0x11;
            return;
        }
        battleSoundEffect2(0x4F);
        return;
    case 2:
        battleVar2->m2DD_currentActiveBattleMenu = 0x12;
        return;
    case 3:
        if (battleVar2->m0_entityMenus[arg0 & 0xFF].m1C_isCommandEnabled[6] == 0) {
            battleVar2->m2DD_currentActiveBattleMenu = 0x13;
            return;
        }
        if (battleVar2->m2F6_doublePressPending != 0 && newBattleInputButton2 == 3) {
            if (battleVar2->m0_entityMenus[arg0 & 0xFF].m1C_isCommandEnabled[4] != 0) {
                battleSoundEffect2(0x4F);
            } else {
                battleVar2->m2DD_currentActiveBattleMenu = 0x18;
            }
            battleVar2->m2F6_doublePressPending = 0;
            return;
        }
        battleVar2->m2F6_doublePressPending = 1;
        battleSoundEffect2(0x4F);
        return;
    case 0:
        if (battleVar2->m0_entityMenus[arg0 & 0xFF].m1C_isCommandEnabled[13] == 0) {
            battleVar2->m2DD_currentActiveBattleMenu = 0x15;
            return;
        }
        battleSoundEffect2(0x4F);
        return;
    }
}

/* Gear version of drawCircleMenuItemActive (menu 0x11) */
void drawGearCircleMenuItemActive(s32 arg0) {
    switch (battleInputButton) {
    case 4:
    case 6:
    case 7: {
        u8 idx = arg0;
        if (drawCircleMenuItem_update(idx) != 0) {
            executeItem(idx);
        } else {
            battleVar2->m2DD_currentActiveBattleMenu = 0x11;
        }
        return;
    }
    case 0:
        if (battleVar2->m0_entityMenus[arg0 & 0xFF].m1C_isCommandEnabled[9] == 0) {
            battleVar2->m2DD_currentActiveBattleMenu = 0x10;
            return;
        }
        battleSoundEffect2(0x4F);
        return;
    case 2:
        battleVar2->m2DD_currentActiveBattleMenu = 0x12;
        return;
    case 3:
        if (battleVar2->m0_entityMenus[arg0 & 0xFF].m1C_isCommandEnabled[6] == 0) {
            battleVar2->m2DD_currentActiveBattleMenu = 0x13;
            return;
        }
        if (battleVar2->m2F6_doublePressPending != 0 && newBattleInputButton2 == 3) {
            if (battleVar2->m0_entityMenus[arg0 & 0xFF].m1C_isCommandEnabled[4] != 0) {
                battleSoundEffect2(0x4F);
            } else {
                battleVar2->m2DD_currentActiveBattleMenu = 0x18;
            }
            battleVar2->m2F6_doublePressPending = 0;
            return;
        }
        battleVar2->m2F6_doublePressPending = 1;
        battleSoundEffect2(0x4F);
        return;
    case 1:
        if (battleVar2->m0_entityMenus[arg0 & 0xFF].m1C_isCommandEnabled[12] == 0) {
            battleVar2->m2DD_currentActiveBattleMenu = 0x16;
            return;
        }
        battleSoundEffect2(0x4F);
        return;
    }
}

/* Gear version of drawCircleMenuDefend (menu 0x12): recharges fuel */
void drawGearCircleMenuCharge(s32 arg0) {
    switch (battleInputButton) {
    case 4:
    case 6:
    case 7: {
        u8 idx = arg0;
        characterPerformDefendAction(idx);
        battleEntities.m0_entities[idx].mA4_gear.m38_fuel += D_800D2C0A.m28_refuel;
        if (battleEntities.m0_entities[idx].mA4_gear.m38_fuel > battleEntities.m0_entities[idx].mA4_gear.m3A_maxFuel) {
            battleEntities.m0_entities[idx].mA4_gear.m38_fuel = battleEntities.m0_entities[idx].mA4_gear.m3A_maxFuel;
        }
        battleVar2->m2EB_hpChanged[idx] = 1;
        battleVar2->m2EA_notDefending = 0;
        battleVar2->m2DE_actionDone = 1;
        return;
    }
    case 0:
        if (battleVar2->m0_entityMenus[arg0 & 0xFF].m1C_isCommandEnabled[9] == 0) {
            battleVar2->m2DD_currentActiveBattleMenu = 0x10;
            return;
        }
        battleSoundEffect2(0x4F);
        return;
    case 1:
        if (battleVar2->m0_entityMenus[arg0 & 0xFF].m1C_isCommandEnabled[12] == 0) {
            battleVar2->m2DD_currentActiveBattleMenu = 0x11;
            return;
        }
        battleSoundEffect2(0x4F);
        return;
    case 3:
        if (battleVar2->m0_entityMenus[arg0 & 0xFF].m1C_isCommandEnabled[6] == 0) {
            battleVar2->m2DD_currentActiveBattleMenu = 0x13;
            return;
        }
        if (battleVar2->m2F6_doublePressPending != 0 && newBattleInputButton2 == 3) {
            if (battleVar2->m0_entityMenus[arg0 & 0xFF].m1C_isCommandEnabled[4] != 0) {
                battleSoundEffect2(0x4F);
            } else {
                battleVar2->m2DD_currentActiveBattleMenu = 0x18;
            }
            battleVar2->m2F6_doublePressPending = 0;
            return;
        }
        battleVar2->m2F6_doublePressPending = 1;
        battleSoundEffect2(0x4F);
        return;
    case 2:
        if (battleVar2->m0_entityMenus[arg0 & 0xFF].m1C_isCommandEnabled[8] == 0) {
            battleVar2->m2DD_currentActiveBattleMenu = 0x17;
            return;
        }
        battleSoundEffect2(0x4F);
        return;
    }
}

/* Gear version of drawCircleMenuChi (menu 0x13) */
void drawGearCircleMenuChi(s32 arg0) {
    switch (battleInputButton) {
    case 4:
    case 6:
    case 7: {
        u8 idx = arg0;
        drawCircleMenuChi_loadData(idx);
        if (drawCircleMenuChi_update(idx) != 0) {
            executeAbility(idx);
        } else {
            battleVar2->m2DD_currentActiveBattleMenu = 0x13;
        }
        return;
    }
    case 0:
        if (battleVar2->m0_entityMenus[arg0 & 0xFF].m1C_isCommandEnabled[9] == 0) {
            battleVar2->m2DD_currentActiveBattleMenu = 0x10;
            return;
        }
        battleSoundEffect2(0x4F);
        return;
    case 1:
        if (battleVar2->m0_entityMenus[arg0 & 0xFF].m1C_isCommandEnabled[12] == 0) {
            battleVar2->m2DD_currentActiveBattleMenu = 0x11;
            return;
        }
        battleSoundEffect2(0x4F);
        return;
    case 2:
        battleVar2->m2DD_currentActiveBattleMenu = 0x12;
        return;
    case 3:
        if (battleVar2->m0_entityMenus[arg0 & 0xFF].m1C_isCommandEnabled[4] == 0) {
            battleVar2->m2DD_currentActiveBattleMenu = 0x18;
            return;
        }
        battleSoundEffect2(0x4F);
        return;
    }
}

/* Gear menu 0x18 (Noah Menu_24?): toggles the gear's m80 0x8000 mode bit */
void drawGearCircleMenuBooster(s32 arg0) {
    switch (battleInputButton) {
    case 4:
    case 6:
    case 7: {
        u8 idx = arg0;
        u16 v = battleEntities.m0_entities[idx].mA4_gear.m80_statusFlags2;
        if (v & 0x8000) {
            battleEntities.m0_entities[idx].mA4_gear.m80_statusFlags2 = v & 0x7FFF;
            battleEntities.m0_entities[idx].m0_base.m84_statusFlags2[0] &= 0x7FFF;
        } else {
            battleEntities.m0_entities[idx].mA4_gear.m7C_statusFlags &= 0xFFDF;
            battleEntities.m0_entities[idx].m0_base.m7C_statusFlags[0] &= 0xEFFF;
            battleEntities.m0_entities[idx].mA4_gear.m80_statusFlags2 |= 0x8000;
            battleEntities.m0_entities[idx].m0_base.m84_statusFlags2[0] |= 0x8000;
        }
        battleVar2->m2DE_actionDone = 1;
        return;
    }
    case 1:
        if (battleVar2->m0_entityMenus[arg0 & 0xFF].m1C_isCommandEnabled[12] == 0) {
            battleVar2->m2DD_currentActiveBattleMenu = 0x16;
            return;
        }
        battleSoundEffect2(0x4F);
        return;
    case 2:
        if (battleVar2->m0_entityMenus[arg0 & 0xFF].m1C_isCommandEnabled[8] == 0) {
            battleVar2->m2DD_currentActiveBattleMenu = 0x17;
            return;
        }
        if (battleVar2->m2F6_doublePressPending != 0 && newBattleInputButton2 == 2) {
            battleVar2->m2DD_currentActiveBattleMenu = 0x12;
            battleVar2->m2F6_doublePressPending = 0;
            return;
        }
        battleVar2->m2F6_doublePressPending = 1;
        battleSoundEffect2(0x4F);
        return;
    case 3:
        if (battleVar2->m0_entityMenus[arg0 & 0xFF].m1C_isCommandEnabled[4] == 0) {
            battleVar2->m2DD_currentActiveBattleMenu = 0x18;
            return;
        }
        if (battleVar2->m2F6_doublePressPending != 0 && newBattleInputButton2 == 3) {
            if (battleVar2->m0_entityMenus[arg0 & 0xFF].m1C_isCommandEnabled[6] != 0) {
                battleSoundEffect2(0x4F);
            } else {
                battleVar2->m2DD_currentActiveBattleMenu = 0x13;
            }
            battleVar2->m2F6_doublePressPending = 0;
            return;
        }
        battleVar2->m2F6_doublePressPending = 1;
        battleSoundEffect2(0x4F);
        return;
    case 0:
        if (battleVar2->m0_entityMenus[arg0 & 0xFF].m1C_isCommandEnabled[9] == 0) {
            battleVar2->m2DD_currentActiveBattleMenu = 0x10;
            return;
        }
        battleSoundEffect2(0x4F);
        return;
    }
}

/* Gear version of drawCircleMenuItemSecondaryActive (menu 0x16) */
void drawGearCircleMenuItemSecondaryActive(s32 arg0) {
    switch (battleInputButton) {
    case 4:
    case 6:
    case 7: {
        u8 idx = arg0;
        if (drawCircleMenuItem_update(idx) != 0) {
            executeItem(idx);
        } else {
            battleVar2->m2DD_currentActiveBattleMenu = 0x16;
        }
        return;
    }
    case 0:
        if (battleVar2->m0_entityMenus[arg0 & 0xFF].m1C_isCommandEnabled[13] == 0) {
            battleVar2->m2DD_currentActiveBattleMenu = 0x15;
            return;
        }
        if (battleVar2->m2F6_doublePressPending != 0 && newBattleInputButton2 == 0) {
            if (battleVar2->m0_entityMenus[arg0 & 0xFF].m1C_isCommandEnabled[9] != 0) {
                battleSoundEffect2(0x4F);
            } else {
                battleVar2->m2DD_currentActiveBattleMenu = 0x10;
            }
            battleVar2->m2F6_doublePressPending = 0;
            return;
        }
        battleVar2->m2F6_doublePressPending = 1;
        battleSoundEffect2(0x4F);
        return;
    case 2:
        if (battleVar2->m0_entityMenus[arg0 & 0xFF].m1C_isCommandEnabled[8] == 0) {
            battleVar2->m2DD_currentActiveBattleMenu = 0x17;
            return;
        }
        if (battleVar2->m2F6_doublePressPending != 0 && newBattleInputButton2 == 2) {
            battleVar2->m2DD_currentActiveBattleMenu = 0x12;
            battleVar2->m2F6_doublePressPending = 0;
            return;
        }
        battleVar2->m2F6_doublePressPending = 1;
        battleSoundEffect2(0x4F);
        return;
    case 3:
        if (battleVar2->m0_entityMenus[arg0 & 0xFF].m1C_isCommandEnabled[4] == 0) {
            battleVar2->m2DD_currentActiveBattleMenu = 0x18;
            return;
        }
        if (battleVar2->m2F6_doublePressPending != 0 && newBattleInputButton2 == 3) {
            if (battleVar2->m0_entityMenus[arg0 & 0xFF].m1C_isCommandEnabled[6] != 0) {
                battleSoundEffect2(0x4F);
            } else {
                battleVar2->m2DD_currentActiveBattleMenu = 0x13;
            }
            battleVar2->m2F6_doublePressPending = 0;
            return;
        }
        battleVar2->m2F6_doublePressPending = 1;
        battleSoundEffect2(0x4F);
        return;
    case 1:
        if (battleVar2->m0_entityMenus[arg0 & 0xFF].m1C_isCommandEnabled[12] == 0) {
            battleVar2->m2DD_currentActiveBattleMenu = 0x11;
            return;
        }
        battleSoundEffect2(0x4F);
        return;
    }
}

/* Gear version of drawBattleMenuEscape (menu 0x17) */
void drawGearBattleMenuEscape(s32 arg0) {
    switch (battleInputButton) {
    case 4:
    case 6:
    case 7:
        if (tryToEscape(battleVar2->m2D3_currentEntityTurn) != 0) {
            battleRunningVar1[0] = 0x40;
        }
        battleVar2->m2DE_actionDone = 1;
        return;
    case 0:
        if (battleVar2->m0_entityMenus[arg0 & 0xFF].m1C_isCommandEnabled[13] == 0) {
            battleVar2->m2DD_currentActiveBattleMenu = 0x15;
            return;
        }
        if (battleVar2->m2F6_doublePressPending != 0 && newBattleInputButton2 == 0) {
            if (battleVar2->m0_entityMenus[arg0 & 0xFF].m1C_isCommandEnabled[9] != 0) {
                battleSoundEffect2(0x4F);
            } else {
                battleVar2->m2DD_currentActiveBattleMenu = 0x10;
            }
            battleVar2->m2F6_doublePressPending = 0;
            return;
        }
        battleVar2->m2F6_doublePressPending = 1;
        battleSoundEffect2(0x4F);
        return;
    case 1:
        if (battleVar2->m0_entityMenus[arg0 & 0xFF].m1C_isCommandEnabled[12] == 0) {
            battleVar2->m2DD_currentActiveBattleMenu = 0x16;
            return;
        }
        battleSoundEffect2(0x4F);
        return;
    case 3:
        if (battleVar2->m0_entityMenus[arg0 & 0xFF].m1C_isCommandEnabled[4] == 0) {
            battleVar2->m2DD_currentActiveBattleMenu = 0x18;
            return;
        }
        if (battleVar2->m2F6_doublePressPending != 0 && newBattleInputButton2 == 3) {
            if (battleVar2->m0_entityMenus[arg0 & 0xFF].m1C_isCommandEnabled[6] != 0) {
                battleSoundEffect2(0x4F);
            } else {
                battleVar2->m2DD_currentActiveBattleMenu = 0x13;
            }
            battleVar2->m2F6_doublePressPending = 0;
            return;
        }
        battleVar2->m2F6_doublePressPending = 1;
        battleSoundEffect2(0x4F);
        return;
    case 2:
        battleVar2->m2DD_currentActiveBattleMenu = 0x12;
        return;
    }
}

/* Gear menu 0x18 (gear chi/abilities) */
void drawGearCircleMenuSpecial(s32 arg0) {
    switch (battleInputButton) {
    case 4:
    case 6:
    case 7: {
        u8 idx = arg0;
        loadGearCommandText(idx);
        if (drawCircleMenuGearSpecial_update(idx) != 0) {
            executeAbility(idx);
        } else {
            battleVar2->m2DD_currentActiveBattleMenu = 0x18;
        }
        return;
    }
    case 0:
        if (battleVar2->m0_entityMenus[arg0 & 0xFF].m1C_isCommandEnabled[13] == 0) {
            battleVar2->m2DD_currentActiveBattleMenu = 0x15;
            return;
        }
        if (battleVar2->m2F6_doublePressPending != 0 && newBattleInputButton2 == 0) {
            if (battleVar2->m0_entityMenus[arg0 & 0xFF].m1C_isCommandEnabled[9] != 0) {
                battleSoundEffect2(0x4F);
            } else {
                battleVar2->m2DD_currentActiveBattleMenu = 0x10;
            }
            battleVar2->m2F6_doublePressPending = 0;
            return;
        }
        battleVar2->m2F6_doublePressPending = 1;
        battleSoundEffect2(0x4F);
        return;
    case 1:
        if (battleVar2->m0_entityMenus[arg0 & 0xFF].m1C_isCommandEnabled[12] == 0) {
            battleVar2->m2DD_currentActiveBattleMenu = 0x16;
            return;
        }
        battleSoundEffect2(0x4F);
        return;
    case 2:
        if (battleVar2->m0_entityMenus[arg0 & 0xFF].m1C_isCommandEnabled[8] == 0) {
            battleVar2->m2DD_currentActiveBattleMenu = 0x17;
            return;
        }
        if (battleVar2->m2F6_doublePressPending != 0 && newBattleInputButton2 == 2) {
            battleVar2->m2DD_currentActiveBattleMenu = 0x12;
            battleVar2->m2F6_doublePressPending = 0;
            return;
        }
        battleVar2->m2F6_doublePressPending = 1;
        battleSoundEffect2(0x4F);
        return;
    case 3:
        if (battleVar2->m0_entityMenus[arg0 & 0xFF].m1C_isCommandEnabled[6] == 0) {
            battleVar2->m2DD_currentActiveBattleMenu = 0x13;
            return;
        }
        battleSoundEffect2(0x4F);
        return;
    }
}

/* Gear version of handleMenuSelectEnemy (attacks cost fuel) */
s32 handleGearMenuSelectEnemy(u8 arg0) {
    s32 count;
    u16 fuel;
    u8 ok;
    u8 cont;
    s32 cost;

    count = 0;
    ok = 0;
    startCharacterJumpToEnemyVar0 = 0;
    fuel = battleEntities.m0_entities[arg0].mA4_gear.m38_fuel;
    if (battleVar2->m2E1_waitBeforeNextAttackInput != 0) {
        IMPLICIT_RETURN(0);
    }
    updateTargetSelectionMarker(arg0);
    switch (battleInputButton) {
    case 5:
        cont = 1;
        if (battleVar2->m2E7_gearAttackCount != 0 && D_800D2C0A.m2A_mode != 4) {
            handleMenuSelectEnemy_cancel_sub0(arg0);
            battleVar2->m2E1_waitBeforeNextAttackInput = cont;
            cont = 0;
        }
        if (cont) {
            battleVar1->mA8_drawComboInput = 0;
            cancelJumpAnim();
            startCharacterJumpToEnemyVar0 = 1;
            battleVar2->m2DD_currentActiveBattleMenu = 0x10;
            removeTargetSelectionCursors();
            if (D_800D2C0A.m2A_mode != 4) {
                removeBillyWindow(arg0, 1);
            } else {
                battleVar2->m2E7_gearAttackCount = 0;
                battleVar2->m2D6_comboInputLength = 0;
            }
        }
        break;
    case 4:
        if (battleVar2->m0_entityMenus[arg0].m1C_isCommandEnabled[2] != 0) {
            battleInputButton = 5;
            battleSoundEffect(0x4F);
        }
        break;
    case 7:
        if (battleVar2->m0_entityMenus[arg0].m1C_isCommandEnabled[1] != 0) {
            battleInputButton = 5;
            battleSoundEffect(0x4F);
        }
        break;
    case 6:
        if (battleVar2->m0_entityMenus[arg0].m1C_isCommandEnabled[0] != 0) {
            battleInputButton = 5;
            battleSoundEffect(0x4F);
        }
        break;
    }
    if (battleVar2->m2E9_targetConfirmed == 0) {
        switch (battleInputButton) {
        case 0:
            battleVar2->m2E8_currentTarget = selectNewSlotByDirection(battleVar2->m2E8_currentTarget, 0);
            battleSoundEffect(0x4C);
            break;
        case 1:
            battleVar2->m2E8_currentTarget = selectNewSlotByDirection(battleVar2->m2E8_currentTarget, 1);
            battleSoundEffect(0x4C);
            break;
        case 2:
            battleVar2->m2E8_currentTarget = selectNewSlotByDirection(battleVar2->m2E8_currentTarget, 2);
            battleSoundEffect(0x4C);
            break;
        case 3:
            battleVar2->m2E8_currentTarget = selectNewSlotByDirection(battleVar2->m2E8_currentTarget, 3);
            battleSoundEffect(0x4C);
            break;
        }
    }
    battleVar1->m7C_hpDisplayDirty[arg0] = 1;
    if (battleVar2->m2E7_gearAttackCount != 0) {
        if (D_800D2C0A.m2A_mode != 4 && (D_800D2C0A.m2A_mode == 0 || D_800D2C0A.m2A_mode - 1 < battleVar2->m2CC_comboInput[0])) {
            handleMenuSelectEnemy_cancel_sub0(arg0);
            battleVar2->m2E1_waitBeforeNextAttackInput = 1;
            IMPLICIT_RETURN(0);
        }
        if (bitmaskCharacterCheck(performAttackSub3_var0.m0_performAttackSub3_var0, battleVar2->m0_entityMenus[arg0].m3C_currentTarget) != 0) {
            handleMenuSelectEnemy_cancel_sub0(arg0);
            battleVar2->m2E1_waitBeforeNextAttackInput = 1;
            IMPLICIT_RETURN(0);
        }
    }
    switch (battleInputButton) {
    case 5:
        IMPLICIT_RETURN(0);
    default:
        if (D_800D2C0A.m2A_mode != 4) {
            IMPLICIT_RETURN(0);
        }
        if (battleVar2->m2E7_gearAttackCount != 0) {
            IMPLICIT_RETURN(0);
        }
        battleVar2->m2E7_gearAttackCount++;
        updateGearDeathblowComboInput(0xFF, arg0);
        IMPLICIT_RETURN(0);
    case 4:
        count++;
    case 7:
        count++;
    case 6:
        count++;
        if (D_800D2C0A.m2A_mode == 4 && battleVar2->m2E7_gearAttackCount == 0) {
            battleVar2->m2E7_gearAttackCount++;
            updateGearDeathblowComboInput(0xFF, arg0);
            IMPLICIT_RETURN(0);
        }
        /* Retail calls it as u8 isGearComboAttackAvailable(u8, u8) (a different source file
         * from its s32 definition below: the file boundary is misplaced). */
        if (((u8 (*)(u8, u8))isGearComboAttackAvailable)(count - 1, arg0) == 0) {
            IMPLICIT_RETURN(0);
        }
        if (battleVar2->m2D6_comboInputLength == 0) {
            cost = D_800D2C0A.m0_fuelCosts[count];
        } else if (D_800D2C0A.m2A_mode == 4) {
            cost = D_800D2C0A.m0_fuelCosts[D_800D2C0A.m2A_mode * 3 + count];
        } else {
            cost = D_800D2C0A.m0_fuelCosts[(battleVar2->m2CC_comboInput[0] + 1) * 3 + count];
        }
        if (fuel - cost >= 0) {
            ok = 1;
        }
        if (ok) {
            battleSoundEffect(0x4D);
            battleBssBlock.mA79 = 1;
            removeBattleCursorDuringAttack(arg0);
            if (battleVar2->m2D6_comboInputLength != 0) {
                battleVar2->m2E4_deathblowCompleted++;
            }
            if (battleVar1->mCB_drawCommandRing != 0) {
                battleVar2->m2DD_currentActiveBattleMenu = 0x65;
                battleVar2->m2E2_previousActiveBattleMenu = 0xFF;
                battleVar2->m2E0_isBattleMenuDisabled = 1;
            } else {
                battleVar2->m2DD_currentActiveBattleMenu = 0x19;
                battleVar2->m2E2_previousActiveBattleMenu = 0x19;
            }
            battleVar2->m2E4_deathblowCompleted = updateGearDeathblowComboInput(count - 1, arg0) == 0;
            performAttack(arg0, count);
            battleEntities.m0_entities[arg0].mA4_gear.m38_fuel -= D_800D2C0A.m0_fuelCosts[battleVar2->m2DC_attackIndex];
            updateGearHudFuel(arg0);
            battleVar2->m2E7_gearAttackCount++;
        } else {
            startCharacterJumpToEnemyVar0 = 1;
            battleSoundEffect2(0x4F);
            startCharacterJumpToEnemyVar0 = 0;
        }
        if (battleVar2->m2E4_deathblowCompleted != 0) {
            handleMenuSelectEnemy_cancel_sub0(arg0);
            battleVar2->m2E1_waitBeforeNextAttackInput = 1;
        }
        break;
    }
    FALLOFF_RETURN(0); /* result unused */
}
