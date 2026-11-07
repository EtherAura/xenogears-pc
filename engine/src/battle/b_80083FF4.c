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

u8 isTargetValid(u8 attacker, u8 target) {
    u8 result;

    result = 0;
    if (isBattleSlotFilled.m0_isBattleSlotFilled[target] != 0 && battleVisualEntities[target].m3_hidden == 0) {
        if (apConfigArray[target].m1_gearState == 0) {
            if (battleMechaInitData->m140_jumpPaths[battleVisualEntities[attacker].m0_positionSlot][battleVisualEntities[target].m0_positionSlot][0] == 0) {
                result = (battleEntities.m0_entities[target].m0_base.m7C_statusFlags[0] & 0xC001) == 0;
            }
        } else if ((battleEntities.m0_entities[target].mA4_gear.m7C_statusFlags & 0xC001) == 0) {
            result = 1;
        }
    }
    return result;
}

u8 abilityTargetSelectionInitSub0Sub0(u8 target, u8 includeKO) {
    u8 result;

    result = 0;
    if (isBattleSlotFilled.m0_isBattleSlotFilled[target] != 0 && battleVisualEntities[target].m3_hidden == 0) {
        if (apConfigArray[target].m1_gearState == 0) {
            result = 1;
            if (includeKO == 0) {
                result = (battleEntities.m0_entities[target].m0_base.m7C_statusFlags[0] & 0xC001) == 0;
            }
        } else if (includeKO != 0 || (battleEntities.m0_entities[target].mA4_gear.m7C_statusFlags & 0xC001) == 0) {
            result = 1;
        }
    }
    return result;
}

u8 getEntityToFace(u8 arg0) {
    u8 candidates[12];
    u8 sameSlot[12];
    s32 i;
    s32 count;
    s32 k;
    u8 tmp;

    numValidTarget = 0;
    for (i = 0; i < 12; i++) {
        candidates[i] = 0xFF;
        targetsPerPriority[i] = 0xFF;
        sameSlot[i] = 0;
    }
    if (arg0 < 3) {
        for (i = 3, count = 0; i < 11; i++) {
            if (isTargetValid(arg0, i) != 0) {
                candidates[count] = i;
                count++;
                numValidTarget++;
            }
        }
    } else {
        for (i = 0, count = 0; i < 3; i++) {
            if (isTargetValid(arg0, i) != 0) {
                candidates[count] = i;
                count++;
                numValidTarget++;
            }
        }
    }
    k = 0;
    for (i = 0; i < count; i++) {
        if (battleVisualEntities[arg0].m0_positionSlot == battleVisualEntities[candidates[i]].m0_positionSlot) {
            targetsPerPriority[k] = candidates[i];
            candidates[i] = 0xFF;
            sameSlot[k] = 1;
            k++;
        }
    }
    for (i = 0; i < count; i++) {
        if (candidates[i] != 0xFF) {
            targetsPerPriority[k] = candidates[i];
            k++;
        }
    }
    if (sameSlot[0] != 0) {
        for (i = 1; i < count; i++) {
            if (sameSlot[i] != 0) {
                if (battleEntities.m0_entities[targetsPerPriority[0]].m0_base.m4C_HP > battleEntities.m0_entities[targetsPerPriority[i]].m0_base.m4C_HP) {
                    tmp = targetsPerPriority[0];
                    targetsPerPriority[0] = targetsPerPriority[i];
                    targetsPerPriority[i] = tmp;
                }
            }
        }
    } else {
        for (i = 1; i < count; i++) {
            if (battleEntities.m0_entities[targetsPerPriority[0]].m0_base.m4C_HP > battleEntities.m0_entities[targetsPerPriority[i]].m0_base.m4C_HP) {
                tmp = targetsPerPriority[0];
                targetsPerPriority[0] = targetsPerPriority[i];
                targetsPerPriority[i] = tmp;
            }
        }
    }
    return targetsPerPriority[0];
}

u8 abilityTargetSelectionInitSub0(u8 targetMode, u8 includeKO, u8 partyFirst) {
    s32 i;
    s32 count;
    u8 start1;
    s32 count1;
    u8 start2;
    s32 count2;

    count2 = 0;
    for (i = 0; i < 12; i++) {
        targetsPerPriority[i] = 0xFF;
    }
    switch (targetMode) {
    case 0:
        start1 = 3;
        count1 = 8;
        break;
    case 1:
        start1 = 0;
        count1 = 3;
        break;
    case 2:
        start1 = 0;
        if (partyFirst == 0) {
            start1 = 3;
            count1 = 8;
            start2 = 0;
            count2 = 3;
        } else {
            count1 = 3;
            start2 = 3;
            count2 = 8;
        }
        break;
    }
    count = 0;
    numValidTarget = 0;
    abilityTargetBitmask = 0;
    i = start1;
    while (--count1 >= 0) {
        if (abilityTargetSelectionInitSub0Sub0(i, includeKO) != 0) {
            u16 mask;
            targetsPerPriority[count] = i;
            mask = characterIdToTargetBitmask(i);
            count++;
            abilityTargetBitmask |= mask;
            numValidTarget++;
        }
        i++;
    }
    i = start2;
    while (--count2 >= 0) {
        if (abilityTargetSelectionInitSub0Sub0(i, includeKO) != 0) {
            u16 mask;
            targetsPerPriority[count] = i;
            mask = characterIdToTargetBitmask(i);
            count++;
            abilityTargetBitmask |= mask;
            numValidTarget++;
        }
        i++;
    }
    return targetsPerPriority[0];
}

u8 abilityTargetSelectionInitSub1(u8 arg0) {
    s32 i;
    s32 count;
    s32 n;

    n = 8;
    for (i = 0; i < 12; i++) {
        targetsPerPriority[i] = 0xFF;
    }
    count = 0;
    numValidTarget = 0;
    abilityTargetBitmask = 0;
    i = 3;
    while (--n >= 0) {
        if (isTargetValid(arg0, i) != 0) {
            u16 mask;
            targetsPerPriority[count] = i;
            mask = characterIdToTargetBitmask(i);
            count++;
            abilityTargetBitmask |= mask;
            numValidTarget++;
        }
        i++;
    }
    return targetsPerPriority[0];
}

/* (a - b)^2, computed on |a - b| (retail multiplies in both arms) */
#define SQUARE_DIFF(a, b) ((a) - (b) < 0 ? ((b) - (a)) * ((b) - (a)) : ((a) - (b)) * ((a) - (b)))

u8 selectNewSlotByDirection(u8 inputSlot, u8 direction) {
    s32 best;
    u8 slot;
    s32 i;
    s32 found;
    s32 angle;
    s32 dist;

    best = 0xFFFFFF;
    slot = inputSlot;
    for (i = 0; i < 11; i++) {
        if (targetsPerPriority[i] != 0xFF && targetsPerPriority[i] != inputSlot) {
            found = 0;
            angle = ratan2(battleVisualEntities[targetsPerPriority[i]].mC_Z - battleVisualEntities[inputSlot].mC_Z,
                           battleVisualEntities[targetsPerPriority[i]].mA_X - battleVisualEntities[inputSlot].mA_X);
            switch (direction) {
            case 0:
                if ((u16)(angle + 0x200) < 0x400) {
                    found = 1;
                }
                break;
            case 1:
                if ((u16)(angle + 0x600) < 0x400) {
                    found = 1;
                }
                break;
            case 2:
                if ((u16)(angle + 0x800) < 0x200) {
                    found = 1;
                }
                if ((u16)(angle - 0x600) < 0x201) {
                    found = 1;
                }
                break;
            case 3:
                if ((u16)(angle - 0x200) < 0x400) {
                    found = 1;
                }
                break;
            }
            if (found) {
                dist = SQUARE_DIFF(battleVisualEntities[targetsPerPriority[i]].mC_Z, battleVisualEntities[inputSlot].mC_Z);
                dist += SQUARE_DIFF(battleVisualEntities[targetsPerPriority[i]].mA_X, battleVisualEntities[inputSlot].mA_X);
                if (dist < best) {
                    best = dist;
                    slot = targetsPerPriority[i];
                }
            }
        }
    }
    return slot;
}

void selectDefaultTarget(u8 arg0) {
    s32 found;
    s32 i;

    battleVar2->m2E8_currentTarget = battleVar2->m0_entityMenus[arg0].m3C_currentTarget;
    found = 0;
    getEntityToFace(arg0);
    for (i = 0; i < numValidTarget; i++) {
        if (targetsPerPriority[i] == battleVar2->m0_entityMenus[arg0].m3C_currentTarget) {
            found++;
            break;
        }
    }
    if (found == 0) {
        battleVar2->m2E8_currentTarget = targetsPerPriority[0];
    }
}

/* Target selection loop for an attack (returns 1: confirmed, 0: cancelled) */
u8 battleSelectTargetLoop(s32 arg0) {
    u8 state;
    u8 target;
    u8 idx;

    idx = arg0;
    state = 2;
    target = battleVar2->m2E8_currentTarget;
    battleInputButton = 8;
    do {
        if (battleInputButton == 8) {
            s32 w; /* compared through a variable: a constant 8 would share the outer loop's register */
            do {
                setCameraVisibleEntities(characterIdToTargetBitmask(arg0) | characterIdToTargetBitmask(target));
                updateCharacterBlinkingTask(characterIdToTargetBitmask(target));
                battleRenderDebugAndMain();
                w = 8;
            } while (battleInputButton == w);
        }
        switch (battleInputButton) {
        case 4:
        case 6:
        case 7:
            state = 1;
            battleVar2->m0_entityMenus[idx].m3C_currentTarget = target;
            break;
        case 5:
            setCameraVisibleEntities(characterIdToTargetBitmask(idx) | characterIdToTargetBitmask(battleVar2->m0_entityMenus[idx].m3C_currentTarget));
            state = 0;
            updateCharacterBlinkingTask(characterIdToTargetBitmask(battleVar2->m0_entityMenus[idx].m3C_currentTarget));
            break;
        case 0:
            target = selectNewSlotByDirection(target, 0);
            break;
        case 1:
            target = selectNewSlotByDirection(target, 1);
            break;
        case 2:
            target = selectNewSlotByDirection(target, 2);
            break;
        case 3:
            target = selectNewSlotByDirection(target, 3);
            break;
        }
        battleInputButton = 8;
    } while (state == 2);
    return state;
}

u16 abilitySelectTargetSub0(void) {
    u16 result;
    s32 i;

    result = 0;
    for (i = 0; i < 11; i++) {
        if (bitmaskCharacterCheck(abilityTargetBitmask, i) != 0 &&
            battleVisualEntities[abilitySelectedTarget].m0_positionSlot == battleVisualEntities[i].m0_positionSlot) {
            result |= characterIdToTargetBitmask(i);
        }
    }
    return result;
}

/* Noah: abilityTargetSelectionInit. The KO-target scan sits in a
 * do { } while (0): the extra loop level weights its register references
 * (flow counts refs by loop depth), which gives global-alloc retail's order
 * j=s0, count=s1, idx=s2, i*0x170=s3, i=s4, i*8=s5. The 0x4000 branch fills
 * the list with count too (retail runs that loop in s1). */
u16 abilityTargetSelectionInit(u16 flags, u16 flags2, u8 self, u8 mode, u8 useSelfTargets) {
    u8 includeKO;
    u8 partyFirst;
    u8 targetMode;
    u16 mask;
    s32 count;
    s32 i;

    includeKO = 0;
    switch (mode) {
    case 0:
        flags = flags2;
        break;
    case 1:
        flags = 0x3000;
        break;
    case 2:
        flags = 0x2001;
        break;
    }
    if (flags & 0x8000) {
        includeKO = 1;
    }
    if (flags & 0x1000) {
        partyFirst = 0;
        targetMode = 0;
        mask = 0xFFF8;
    } else {
        partyFirst = 1;
        targetMode = 1;
        mask = 7;
    }
    if (flags & 0x2000) {
        targetMode = 2;
        mask = 0xFFFF;
    }
    if (useSelfTargets != 0) {
        abilitySelectedTarget = abilityTargetSelectionInitSub1(self);
    } else {
        abilitySelectedTarget = abilityTargetSelectionInitSub0(targetMode, includeKO, partyFirst);
    }
    abilityTargetBitmask = mask & abilityTargetBitmask;
    if (flags & 0x8000) {
        count = 0;
        abilitySelectedTarget = 0xFF;
        do {
            for (i = 0; i < 11; i++) {
                u8 j = i;
                s32 idx = j;
                if (bitmaskCharacterCheck(abilityTargetBitmask, idx) != 0) {
                    if (((apConfigArray[i].m1_gearState == 0) ? battleEntities.m0_entities[i].m0_base.m7C_statusFlags[0] : battleEntities.m0_entities[i].mA4_gear.m7C_statusFlags) & 0x8000) {
                        abilitySelectedTarget = j;
                        targetsPerPriority[count] = j;
                        count++;
                    } else {
                        abilityTargetBitmask &= characterOdToInverseTargetBitmask(idx);
                    }
                }
            }
        } while (0);
        for (; count < 11; count++) {
            targetsPerPriority[count] = 0xFF;
        }
    } else if (flags & 0x4000) {
        abilitySelectedTarget = self;
        abilityTargetBitmask = characterIdToTargetBitmask(self);
        for (count = 0; count < 11; count++) {
            targetsPerPriority[count] = 0xFF;
        }
    }
    return flags;
}

/* Noah: abilitySelectTarget (returns 0: confirmed, 1: cancelled, -1: no target) */
u8 abilitySelectTarget(u16 flags, u8 self, u8 useSelfTargets) {
    u8 prevButton;
    u16 savedMask;
    u8 result;
    s32 i;
    u8 target;

    prevButton = 0xFE;
    abilityTargetSelectionInit(flags, flags, self, 0, useSelfTargets);
    savedMask = abilityTargetBitmask;
    result = abilitySelectedTarget == 0xFF;
    if (result == 0) {
        do {
            battleRenderDebugAndMain();
            switch (flags & 0xF) {
            case 0:
                updateCharacterBlinkingTask(characterIdToTargetBitmask(abilitySelectedTarget));
                setCameraVisibleEntities(characterIdToTargetBitmask(abilitySelectedTarget));
                break;
            case 1:
                updateCharacterBlinkingTask(abilityTargetBitmask);
                setCameraVisibleEntities(abilityTargetBitmask);
                break;
            case 2:
                savedMask = abilitySelectTargetSub0();
                updateCharacterBlinkingTask(savedMask);
                setCameraVisibleEntities(savedMask);
                break;
            }
            switch (battleInputButton) {
            case 5:
                result = 1;
                break;
            case 4:
                abilityTargetBitmask = savedMask;
                result = 2;
                break;
            case 0:
                target = selectNewSlotByDirection(abilitySelectedTarget, 0);
                if (bitmaskCharacterCheck(abilityTargetBitmask, target) != 0) {
                    abilitySelectedTarget = target;
                }
                break;
            case 1:
                target = selectNewSlotByDirection(abilitySelectedTarget, 1);
                if (bitmaskCharacterCheck(abilityTargetBitmask, target) != 0) {
                    abilitySelectedTarget = target;
                }
                break;
            case 2:
                target = selectNewSlotByDirection(abilitySelectedTarget, 2);
                if (bitmaskCharacterCheck(abilityTargetBitmask, target) != 0) {
                    abilitySelectedTarget = target;
                }
                break;
            case 3:
                target = selectNewSlotByDirection(abilitySelectedTarget, 3);
                if (bitmaskCharacterCheck(abilityTargetBitmask, target) != 0) {
                    abilitySelectedTarget = target;
                }
                break;
            case 6:
            case 7:
                break;
            }
            if (battleInputButton != prevButton) {
                prevButton = battleInputButton;
                for (i = 0; i < 4; i++) {
                    if (selectNewSlotByDirection(abilitySelectedTarget, i) != abilitySelectedTarget) {
                        battleG3->mE6_isDirectionEnabled[i] = 1;
                    } else {
                        battleG3->mE6_isDirectionEnabled[i] = 0;
                    }
                }
            }
        } while (result == 0);
    }
    return result - 1;
}

s32 getDirectionBetween2BattleEntities(u8 a, u8 b) {
    return battleVisualEntities[a].mA_X > battleVisualEntities[b].mA_X;
}
