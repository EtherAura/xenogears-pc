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
#ifndef PLATFORM_PC
u8 executeMonsterScriptWhenAttacked(u8);
#endif
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
extern s32 damageDonePerAttack[11] PORT_ALIAS; /* damage value per entity (current hit) */
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
#ifdef PLATFORM_PC
/* The table has ten entries but no end mark, and command loadouts go past it (the
 * debug selector's adult Emeralda has loadout 10): retail then reads the next words,
 * apTurnDelayTicks' first bytes, which are the same pointer as every entry (US
 * 0x800C3194, JP 0x800C2914, retail data; PCSX-Redux shows all sixteen words so in
 * a battle). The PC's table is followed by deathblowInputSequences instead, whose
 * bytes made a wild pointer: a deathblow from nowhere, attack index 0xFF, then
 * startBattleAttackAnimation's effect type 9 from an enemy's entry and a crash. */
static u8* pcDeathblowIds(u32 loadout) {
    if (loadout < 10) {
        return deathblowAbilityIdsByLoadout[loadout];
    }
    return deathblowAbilityIdsByLoadout[0];
}
#define DEATHBLOW_IDS(loadout) pcDeathblowIds(loadout)
#else
#define DEATHBLOW_IDS(loadout) deathblowAbilityIdsByLoadout[loadout]
#endif
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
#ifndef PLATFORM_PC
s32 hasPassiveModifierEndOfTurn(u8, s32*);
void displayPassiveModifiers(u8, s32, s32, s32);
#endif
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
#ifndef PLATFORM_PC
void applyBattleItemToTargets(u16);
#endif

/* resets battleTickMainSub0Var1[11]=0xFF, battleTickMainSub0Var0[11]=0 */
void battleTickMainSub0(void) {
    s32 i;
    for (i = 0; i < 11; i++) {
        battleTickMainSub0Var1[i] = 0xFF;
        battleTickMainSub0Var0[i] = 0;
    }
}

void performAttackSub2(void) {
    s32 i;

    for (i = 0; i < 11; i++) {
        battleCurrentDamages[battleVar2->m2DA_indexInBattleVar48].m0_damageValue[i] = 0;
        battleCurrentDamages[battleVar2->m2DA_indexInBattleVar48].m18_damageType[i] = 0xFF;
        battleCurrentDamages[battleVar2->m2DA_indexInBattleVar48].m24_totalDamages[i] = 0;
        battleCurrentDamages[battleVar2->m2DA_indexInBattleVar48].m3C_totalDamageColors[i] = 0xFF;
    }
}

/* Merges the current hit (damageDonePerAttack/initBattleAttackStatusArray0) into the accumulated
 * damage (battleTickMainSub0Var0/battleTickMainSub0Var1) and records both in damage entry arg0.
 * Types: 0/5 damage, 2 heal. */
void setDamageDone(u8 arg0) {
    s32 i;

    for (i = 0; i < 11; i++) {
        battleCurrentDamages[arg0].m0_damageValue[i] = damageDonePerAttack[i];
        battleCurrentDamages[arg0].m18_damageType[i] = initBattleAttackStatusArray0[i];
        switch (initBattleAttackStatusArray0[i]) {
        case 0:
        case 5:
            if (battleTickMainSub0Var1[i] == 0 || battleTickMainSub0Var1[i] == 5) {
                battleTickMainSub0Var0[i] += damageDonePerAttack[i];
            } else if (battleTickMainSub0Var1[i] == 2) {
                if ((s16)damageDonePerAttack[i] - battleTickMainSub0Var0[i] < 0) {
                    battleTickMainSub0Var0[i] = battleTickMainSub0Var0[i] - (s16)damageDonePerAttack[i];
                } else {
                    battleTickMainSub0Var0[i] = (s16)damageDonePerAttack[i] - battleTickMainSub0Var0[i];
                    battleTickMainSub0Var1[i] = 0;
                }
            } else {
                battleTickMainSub0Var0[i] = damageDonePerAttack[i];
                battleTickMainSub0Var1[i] = initBattleAttackStatusArray0[i];
            }
            break;
        case 2:
            if (battleTickMainSub0Var1[i] == 2) {
                battleTickMainSub0Var0[i] += damageDonePerAttack[i];
            } else if (battleTickMainSub0Var1[i] == 0 || battleTickMainSub0Var1[i] == 5) {
                if ((s16)damageDonePerAttack[i] - battleTickMainSub0Var0[i] < 0) {
                    battleTickMainSub0Var0[i] = battleTickMainSub0Var0[i] - (s16)damageDonePerAttack[i];
                } else {
                    battleTickMainSub0Var0[i] = (s16)damageDonePerAttack[i] - battleTickMainSub0Var0[i];
                    battleTickMainSub0Var1[i] = 2;
                }
            } else {
                battleTickMainSub0Var0[i] = damageDonePerAttack[i];
                battleTickMainSub0Var1[i] = initBattleAttackStatusArray0[i];
            }
            break;
        }
        battleCurrentDamages[arg0].m24_totalDamages[i] = battleTickMainSub0Var0[i];
        battleCurrentDamages[arg0].m3C_totalDamageColors[i] = battleTickMainSub0Var1[i];
    }
}

/* The heal paths store through the battle .bss aggregate re-based by i
 * entities (retail keeps &battleBssBlock + i * 0x170 in a register and reaches
 * the fields with 0x8E38 + offset displacements); everything else reads
 * and writes battleEntities. The chained "= u =" keeps the stored sum in a
 * register for the clamp test. */
#define HEALED_ENTITY(i) (((BattleBss_800C3EB0*)((sBattleEntity*)&battleBssBlock + (i)))->m8E38_entities[0])

void applyChangeToHpOrMp(u8 arg0) {
    s32 i;
    s32 g;
    u16 u;
    u32 ug;

    for (i = 0; i < 11; i++) {
        if (isBattleSlotFilled.m0_isBattleSlotFilled[i] == 0) {
            continue;
        }
        if (battleEntities.m0_entities[i].m0_base.m7C_statusFlags[0] & 0x8000) {
            battleBssBlock.mA38_performAttackSub3_var0 |= characterIdToTargetBitmask(i);
            continue;
        }
        switch (battleCurrentDamages[arg0].m18_damageType[i]) {
        case 0:
        case 5:
        case 7:
        case 8:
            if (battleBssBlock.m4_visualEntities[i].m4_isGear == 0) {
                if ((s16)battleEntities.m0_entities[i].m0_base.m4C_HP - (s16)battleBssBlock.m138_currentDamages[arg0].m0_damageValue[i] > 0) {
                    battleEntities.m0_entities[i].m0_base.m4C_HP -= battleBssBlock.m138_currentDamages[arg0].m0_damageValue[i];
                } else {
                    battleEntities.m0_entities[i].m0_base.m4C_HP = 0;
                    battleBssBlock.mA38_performAttackSub3_var0 |= characterIdToTargetBitmask(i);
                    battleEntities.m0_entities[i].m0_base.m7C_statusFlags[0] |= 0x8000;
                    if (i >= 3) {
                        markEnemyDead(i);
                    }
                }
            } else {
                g = battleEntities.m0_entities[i].mA4_gear.m60_hp - battleBssBlock.m138_currentDamages[arg0].m0_damageValue[i];
                if (g > 0) {
                    battleEntities.m0_entities[i].mA4_gear.m60_hp = g;
                } else {
                    battleEntities.m0_entities[i].mA4_gear.m60_hp = 0;
                    battleBssBlock.mA38_performAttackSub3_var0 |= characterIdToTargetBitmask(i);
                    battleEntities.m0_entities[i].mA4_gear.m7C_statusFlags |= 0x8000;
                    battleEntities.m0_entities[i].m0_base.m7C_statusFlags[0] |= 0x8000;
                    if (i >= 3) {
                        markEnemyDead(i);
                    }
                }
            }
            battleVar2->m2EB_hpChanged[i] = 1;
            break;
        case 2:
            if (battleBssBlock.m4_visualEntities[i].m4_isGear != 0 && healWhileOnGear == 0) {
                HEALED_ENTITY(i).mA4_gear.m60_hp = ug = battleBssBlock.m138_currentDamages[arg0].m0_damageValue[i] + battleEntities.m0_entities[i].mA4_gear.m60_hp;
                if (battleEntities.m0_entities[i].mA4_gear.m64_maxHp < ug) {
                    battleEntities.m0_entities[i].mA4_gear.m60_hp = battleEntities.m0_entities[i].mA4_gear.m64_maxHp;
                }
            } else {
                HEALED_ENTITY(i).m0_base.m4C_HP = u = battleEntities.m0_entities[i].m0_base.m4C_HP + battleBssBlock.m138_currentDamages[arg0].m0_damageValue[i];
                if (battleEntities.m0_entities[i].m0_base.m4E_MaxHP < u) {
                    battleEntities.m0_entities[i].m0_base.m4C_HP = battleEntities.m0_entities[i].m0_base.m4E_MaxHP;
                }
            }
            battleVar2->m2EB_hpChanged[i] = 1;
            break;
        case 1:
        case 9:
            if ((s16)battleEntities.m0_entities[i].m0_base.m50_MP - (s16)battleBssBlock.m138_currentDamages[arg0].m0_damageValue[i] > 0) {
                battleEntities.m0_entities[i].m0_base.m50_MP -= battleBssBlock.m138_currentDamages[arg0].m0_damageValue[i];
            } else {
                battleEntities.m0_entities[i].m0_base.m50_MP = 0;
            }
            break;
        case 3:
            if (battleBssBlock.m4_visualEntities[i].m4_isGear == 0 || healWhileOnGear != 0) {
                HEALED_ENTITY(i).m0_base.m50_MP = u = battleEntities.m0_entities[i].m0_base.m50_MP + battleBssBlock.m138_currentDamages[arg0].m0_damageValue[i];
                if (battleEntities.m0_entities[i].m0_base.m52_MaxMP < u) {
                    battleEntities.m0_entities[i].m0_base.m50_MP = battleEntities.m0_entities[i].m0_base.m52_MaxMP;
                }
            }
            break;
        case 10:
            if (battleEntities.m0_entities[i].mA4_gear.m38_fuel - battleBssBlock.m138_currentDamages[arg0].m0_damageValue[i] > 0) {
                battleEntities.m0_entities[i].mA4_gear.m38_fuel = battleEntities.m0_entities[i].mA4_gear.m38_fuel - battleBssBlock.m138_currentDamages[arg0].m0_damageValue[i];
            } else {
                battleEntities.m0_entities[i].mA4_gear.m38_fuel = 0;
            }
            battleVar2->m2EB_hpChanged[i] = 1;
            break;
        case 11:
            HEALED_ENTITY(i).mA4_gear.m38_fuel = u = battleEntities.m0_entities[i].mA4_gear.m38_fuel + battleBssBlock.m138_currentDamages[arg0].m0_damageValue[i];
            if (battleEntities.m0_entities[i].mA4_gear.m3A_maxFuel < u) {
                battleEntities.m0_entities[i].mA4_gear.m38_fuel = battleEntities.m0_entities[i].mA4_gear.m3A_maxFuel;
            }
            battleVar2->m2EB_hpChanged[i] = 1;
            break;
        case 4:
        case 6:
            break;
        }
    }
}

/* Restores HP to max and clears status words m7C/m80 and m84/m88/m8C */
void restoreBattleEntityHpAndStatus(s32 arg0) {
    s32 i;

    battleEntities.m0_entities[arg0 & 0xFF].m0_base.m4C_HP = battleEntities.m0_entities[arg0 & 0xFF].m0_base.m4E_MaxHP;
    for (i = 0; i < 4; i += 2) {
        battleEntities.m0_entities[arg0 & 0xFF].m0_base.m7C_statusFlags[i] = 0;
    }
    for (i = 0; i < 6; i += 2) {
        battleEntities.m0_entities[arg0 & 0xFF].m0_base.m84_statusFlags2[i] = 0;
    }
}

/* applies up to 3 HP/MP/fuel changes from hasPassiveModifierEndOfTurn (per-turn regen?) through applyChangeToHpOrMp */
void applyPassiveModifiersAtEndOfTurn(s32 arg0) {
    s32 values[3];
    u8 idx;
    s32 i;

    idx = arg0;
    values[2] = 0;
    values[1] = 0;
    values[0] = 0;
    if (hasPassiveModifierEndOfTurn(idx, values) != 0) {
        for (i = 0; i < 3; i++) {
            if (values[i] != 0) {
                battleVar2->m2DA_indexInBattleVar48 = 0;
                performAttackSub2();
                battleCurrentDamages[0].m18_damageType[idx] = i + 8;
                battleCurrentDamages[0].m0_damageValue[idx] = values[i];
                applyChangeToHpOrMp(battleVar2->m2DA_indexInBattleVar48);
            }
        }
        displayPassiveModifiers(arg0, values[0], values[1], values[2]);
    }
}

/* starts a battle dialog (applyBattleItemToTargets) */
void performItemOnTargets(s32 arg0, s16 arg1, u16 arg2) {
    performAttackSub3_var0.m0_performAttackSub3_var0 = 0;
    entitiesHitInCurrentAttackBF[0] = arg1;
    battleEntities.m5FAE_targetableMask = allPlayerCharacterBitmask;
    applyBattleItemToTargets(arg2);
}

/* setDamageDone + applyChangeToHpOrMp, battleVar1->mAD = 0 */
void performAttackSub6(u8 arg0) {
    setDamageDone(arg0);
    applyChangeToHpOrMp(arg0);
    battleVar1->mAD_drawGearHud = 0;
}

/* starts a battle dialog (startBattleAttackAnimation) */
void performAttackSub3(u8 arg0, u16 arg1, u16 arg2) {
    performAttackSub3_var0.m0_performAttackSub3_var0 = 0;
    battleEntities.m5FC1_performAttackSub3_attacker = arg0;
    battleGetSlotStatusSub_current28Index = battleVar2->m2DC_attackIndex + 0xFF;
    entitiesHitInCurrentAttackBF[0] = arg1;
    performAttack_type = arg2;
    battleEntities.m5FAE_targetableMask = allPlayerCharacterBitmask;
    startBattleAttackAnimation();
}

void updateAPCounterDisplay(void) {
    battleVar1->m7B_apCounterLength = 0;
    battleVar1->m7B_apCounterLength += battleSetupStringInPolyFT4Large(battleVar2->m2D4_remainingAP + 0xF, &battleVar0->m9C8_APCounterDisplayPolys[battleVar1->m7B_apCounterLength], 0x2A, 0xD0);
    battleVar1->m7B_apCounterLength += battleSetupStringInPolyFT4Large(0x19, &battleVar0->m9C8_APCounterDisplayPolys[battleVar1->m7B_apCounterLength], 0x32, 0xD0);
    battleVar1->m7B_apCounterLength += battleSetupStringInPolyFT4Large(battleVar2->m2D5_maxAP + 0xF, &battleVar0->m9C8_APCounterDisplayPolys[battleVar1->m7B_apCounterLength], 0x3A, 0xD0);
    battleVar1->mA4_oddOrEven7B = *(u8*)&D_800CCB04.m30_battleOddOrEven; /* u8 read (lbu) in this function */
}

void resetBattleVar2_2CC(void) {
    s32 i;
    for (i = 0; i < 7; i++) {
        battleVar2->m2CC_comboInput[i] = 0xFF;
    }
    battleVar2->m2D6_comboInputLength = 0;
}

/* Deathblow check: does the combo input (m2CC) match a known sequence the
 * character has learned? */
u8 handleMenuSelectEnemySub(u8 button, u8 entity) {
    s32 result;
    s32 k;
    s32 j;
    s32 found;
    u8* seq;

    result = 0;
    if (battleVar2->m2D6_comboInputLength != 0 && button == 4) {
        for (k = 0; k < 13; k++) {
            seq = deathblowInputSequences[k];
            for (j = 0; j < 7; j++) {
                if (battleVar2->m2CC_comboInput[j] != seq[j]) {
                    found = 0;
                    break;
                }
                found = 1;
            }
            if (found) {
                break;
            }
        }
        j = 0;
        found = k;
        switch (found) {
        case 0:
            j++;
        case 1:
            j++;
        case 2:
            j++;
        case 3:
            j++;
        case 4:
            j++;
        case 5:
            j++;
        case 6:
            j++;
        case 7:
            j++;
        case 8:
            j++;
        case 9:
            j++;
        case 10:
            j++;
        case 11:
            j++;
        case 12:
            if (bitmaskCharacterCheckReverse(gameState.m16C0_abilities[battleCharacters[entity]].m0_unlockedAbilitiesBitField[0],
                              DEATHBLOW_IDS(battleEntities.m0_entities[entity].m0_base.m56_battleCommandLoadout)[12 - j]) != 0) {
                result = 1;
            }
            break;
        }
    }
    return result;
}

/* 263 260] */
s32 addDeathblowNamePoly(u8 slot, s32 index, s32 x, u8 textIdx, u16** image, u8 offset) {
    RECT rect;
    s32 half = index / 2;
    s32 odd = index % 2;
    s32 width;
    s32 col;
    battleInitPoly4(&pBattleRenderStruct2->m5550_commandNames[index * 2], odd, 3);
    width = renderString(getDeathblowName(battleCharacters[slot], textIdx), *image, 0x1B, odd);
    rect.x = half * 30 + 0x3C0;
    rect.y = 0x1A;
    rect.w = 0x1E;
    rect.h = 0xD;
    LoadImage(&rect, *image);
    col = offset + 1;
    repositionTextRenderingPlanePrim(&pBattleRenderStruct2->m5550_commandNames[index * 2 + D_800CCB04.m30_battleOddOrEven], (x + col) * 16 + 0x50 + index * 4,
                  0xC8 - index * 16, half * 0x78, 0x1A, width);
    pBattleRenderStruct2->m5D70_count[0x10]++;
    return ++index;
}

/* Deathblow combo input: appends the pressed button (combo - 4) to the combo
 * buffer (m2CC/m2D6), redraws the button icons, then shows the deathblows the
 * input completes (loadout column 0xC - k) or leads into with one more button
 * (0x13 - k: button icon 8, 0x16 - k: icon 9); in auto mode (m2E4) it picks
 * the completed one (m2DC) and returns early.
 * The second icon's x is written (i + 1) * 16 + ...: combine folds it to
 * i * 16 + 0x64 after CSE, so i * 16 is recomputed for each call as retail
 * does. done = 1 after the addDeathblowNamePoly call keeps its li after move a2.
 * JP keeps the three name images in a local array (as updateGearDeathblowComboInput). */
void updateDeathblowComboInput(u8 combo, u8 entity) {
    s32 n;
    s32 done;
    s32 i;
    s32 k;
    s32 j;
    s32 found;
    u8 icon;
#if VERSION_JP
    u16* images[3];
#define NAME_IMAGES images
#else
#define NAME_IMAGES deathblowNameImages
#endif

    n = 0;
    done = 0;
#if VERSION_JP
    for (k = 0; k < 3; k++) {
        images[k] = allocateTextureRamForText(0x1B);
    }
#else
    /* retail walks deathblowNameImages by byte offset (a pointer apart) */
    for (j = 0; j < 3 * (s32)sizeof(u16*); j += (s32)sizeof(u16*)) {
        *(u16**)((u8*)deathblowNameImages + j) = allocateTextureRamForText(0x1E);
    }
#endif
    battleVar2->m2CC_comboInput[battleVar2->m2D6_comboInputLength] = combo - 4;
    battleVar2->m2D6_comboInputLength++;
    if (battleVar2->m2E4_deathblowCompleted != 0) {
        battleVar2->m2CC_comboInput[battleVar2->m2D6_comboInputLength - 1] = 0xFF;
    }
    for (i = 0; i < 13; i++) {
        for (k = 0; k < 7; k++) {
            if (battleVar2->m2CC_comboInput[k] != deathblowInputSequences[i][k]) {
                found = 0;
                break;
            }
            found = 1;
        }
        if (found) {
            break;
        }
    }
    found = i;
    if (battleVar2->m2E4_deathblowCompleted != 0) {
        battleVar2->m2CC_comboInput[battleVar2->m2D6_comboInputLength - 1] = combo - 4;
    }
    pBattleRenderStruct2->m5D70_count[0x11] = 0;
    pBattleRenderStruct2->m5D70_count[0x10] = 0;
    for (i = 0; i < battleVar2->m2D6_comboInputLength; i++) {
        switch (battleVar2->m2CC_comboInput[i]) {
        case 0:
            icon = 0x5D;
            break;
        case 2:
            icon = 0x5E;
            break;
        case 3:
            icon = 0x5F;
            break;
        }
        pBattleRenderStruct2->m5D70_count[0x11] += battleSetupStringInPolyFT4Large(icon, &pBattleRenderStruct2->m5640_comboInputIcons[pBattleRenderStruct2->m5D70_count[0x11] * 2], i * 16 + 0x50, 0xD0 - n * 16);
    }
    k = 0;
    switch (found) {
    case 0:
        k++;
    case 1:
        k++;
    case 2:
        k++;
    case 3:
        k++;
    case 4:
        k++;
    case 5:
        k++;
    case 6:
        k++;
    case 7:
        k++;
    case 8:
        k++;
    case 9:
        k++;
    case 10:
        k++;
    case 11:
        k++;
    case 12:
        if (bitmaskCharacterCheckReverse(gameState.m16C0_abilities[battleCharacters[entity]].m0_unlockedAbilitiesBitField[0],
                          DEATHBLOW_IDS(battleEntities.m0_entities[entity].m0_base.m56_battleCommandLoadout)[0xC - k]) &&
            battleVar2->m0_entityMenus[entity].m1C_isCommandEnabled[2] == 0) {
            if (battleVar2->m2E4_deathblowCompleted == 0) {
                pBattleRenderStruct2->m5D70_count[0x11] += battleSetupStringInPolyFT4Large(7, &pBattleRenderStruct2->m5640_comboInputIcons[pBattleRenderStruct2->m5D70_count[0x11] * 2], i * 16 + 0x50, 0xD0 - n * 16);
            } else {
                i--;
            }
            n = addDeathblowNamePoly(entity, n, i, DEATHBLOW_IDS(battleEntities.m0_entities[entity].m0_base.m56_battleCommandLoadout)[0xC - k],
                              &NAME_IMAGES[n / 2], 0);
            done = 1;
        }
        break;
    }
    if (battleVar2->m2E4_deathblowCompleted != 0 && done != 0) {
        pBattleRenderStruct2->m5D83_oddOrEven[0xD] = D_800CCB04.m30_battleOddOrEven;
        battleVar2->m2DC_attackIndex = DEATHBLOW_IDS(battleEntities.m0_entities[entity].m0_base.m56_battleCommandLoadout)[0xC - k] + 8;
        pBattleRenderStruct2->m5D83_oddOrEven[0xC] = D_800CCB04.m30_battleOddOrEven;
        battleVar1->mA8_drawComboInput = 1;
        battleRenderDebugAndMain();
#if VERSION_JP
        for (k = 0; k < 3; k++) {
            freeHeap(images[k]);
        }
#else
        for (j = 0; j < 3 * (s32)sizeof(u16*); j += (s32)sizeof(u16*)) {
            freeHeap(*(u16**)((u8*)deathblowNameImages + j));
        }
#endif
        return;
    }
    k = 0;
    switch (found) {
    case 0:
        k++;
    case 1:
        k++;
    case 2:
        k++;
    case 3:
        k++;
    case 4:
        k++;
    case 5:
        k++;
    case 8:
        if (bitmaskCharacterCheckReverse(gameState.m16C0_abilities[battleCharacters[entity]].m0_unlockedAbilitiesBitField[0],
                          DEATHBLOW_IDS(battleEntities.m0_entities[entity].m0_base.m56_battleCommandLoadout)[0x13 - k]) &&
            battleVar2->m0_entityMenus[entity].m1C_isCommandEnabled[0] == 0 && battleVar2->m0_entityMenus[entity].m1C_isCommandEnabled[2] == 0) {
            pBattleRenderStruct2->m5D70_count[0x11] += battleSetupStringInPolyFT4Large(8, &pBattleRenderStruct2->m5640_comboInputIcons[pBattleRenderStruct2->m5D70_count[0x11] * 2], i * 16 + 0x54, 0xD0 - n * 16);
            pBattleRenderStruct2->m5D70_count[0x11] += battleSetupStringInPolyFT4Large(7, &pBattleRenderStruct2->m5640_comboInputIcons[pBattleRenderStruct2->m5D70_count[0x11] * 2], (i + 1) * 16 + 0x54, 0xD0 - n * 16);
            n = addDeathblowNamePoly(entity, n, i, DEATHBLOW_IDS(battleEntities.m0_entities[entity].m0_base.m56_battleCommandLoadout)[0x13 - k],
                              &NAME_IMAGES[n / 2], 1);
        }
        break;
    }
    k = 0;
    switch (found) {
    case 0:
        k++;
    case 1:
        k++;
    case 3:
        if (bitmaskCharacterCheckReverse(gameState.m16C0_abilities[battleCharacters[entity]].m0_unlockedAbilitiesBitField[0],
                          DEATHBLOW_IDS(battleEntities.m0_entities[entity].m0_base.m56_battleCommandLoadout)[0x16 - k]) &&
            battleVar2->m0_entityMenus[entity].m1C_isCommandEnabled[1] == 0 && battleVar2->m0_entityMenus[entity].m1C_isCommandEnabled[2] == 0) {
            pBattleRenderStruct2->m5D70_count[0x11] += battleSetupStringInPolyFT4Large(9, &pBattleRenderStruct2->m5640_comboInputIcons[pBattleRenderStruct2->m5D70_count[0x11] * 2], i * 16 + 0x58, 0xD0 - n * 16);
            pBattleRenderStruct2->m5D70_count[0x11] += battleSetupStringInPolyFT4Large(7, &pBattleRenderStruct2->m5640_comboInputIcons[pBattleRenderStruct2->m5D70_count[0x11] * 2], (i + 1) * 16 + 0x58, 0xD0 - n * 16);
            addDeathblowNamePoly(entity, n, i, DEATHBLOW_IDS(battleEntities.m0_entities[entity].m0_base.m56_battleCommandLoadout)[0x16 - k],
                          &NAME_IMAGES[n / 2], 1);
        }
        break;
    }
    pBattleRenderStruct2->m5D83_oddOrEven[0xD] = D_800CCB04.m30_battleOddOrEven;
    pBattleRenderStruct2->m5D83_oddOrEven[0xC] = D_800CCB04.m30_battleOddOrEven;
    battleVar1->mA8_drawComboInput = 1;
    battleRenderDebugAndMain();
#if VERSION_JP
    for (k = 0; k < 3; k++) {
        freeHeap(images[k]);
    }
#else
    for (j = 0; j < 3 * (s32)sizeof(u16*); j += (s32)sizeof(u16*)) {
        freeHeap(*(u16**)((u8*)deathblowNameImages + j));
    }
#endif
}
#undef NAME_IMAGES

/* 72 263 260] */
s32 isGearComboAttackAvailable(s32 cmd, u8 entity) {
    s32 idx = cmd + (battleVar2->m2CC_comboInput[0] + 1) * 3;
    s32 result = 1;

    if (battleVar2->m2D6_comboInputLength != 0) {
        if (battleEntities.m5F24_gearTurn.m28_mode == 4) {
            idx = cmd + 12;
        }
        if (bitmaskCharacterCheckReverse(gameState.m16C0_abilities[battleCharacters[entity]].m0_unlockedAbilitiesBitField[2], gearDeathblowAbilityIds[(u8)idx]) == 0) {
            result = 0;
        } else if (battleVar2->m2CC_comboInput[0] != 0xFF && battleEntities.m5F24_gearTurn.m28_mode != 4 && battleEntities.m5F24_gearTurn.m28_mode < battleVar2->m2CC_comboInput[0] + 1) {
            result = 0;
        }
    }
    return result;
}

/* Draws a gear command's name and its fuel cost: the name goes through
 * renderString/LoadImage, the cost's digits (printIntegerToStringBattleBuffer[5..8],
 * 0xFF = blank) are uploaded one 6x13 glyph each next to it. */
s32 addGearDeathblowNamePoly(u8 entity, s32 index, s32 x, u8 cmd, u16** image) {
    RECT uploads[5];   /* [0]: name, [1..4]: fuel digits */
    s32 half;
    s32 odd;
    s32 width;
    s32 n;
    sptr offset;
    s32 xpos;
    s32 i;
    RECT* rect;
    u8* p;

    n = 0;
    half = index / 2;
    odd = index % 2;
    battleInitPoly4(&pBattleRenderStruct2->m5550_commandNames[index * 2], odd, 3);
    p = &printIntegerToStringBattleBuffer[5];
    width = renderString(getGearDeathblowName(battleEntities.m0_entities[entity].m0_base.mA0_gearNum, gearDeathblowAbilityIds[cmd]), *image, 0x1B, odd);
    uploads[0].x = half * 30 + 0x3C0;
    uploads[0].y = 0x1A;
    uploads[0].w = 0x1E;
    uploads[0].h = 0xD;
    LoadImage(&uploads[0], *image);
    repositionTextRenderingPlanePrim(&pBattleRenderStruct2->m5550_commandNames[index * 2 + D_800CCB04.m30_battleOddOrEven], index * 4 + (x + 1) * 16 + 0x86,
                  0xC8 - index * 16, half * 0x78, 0x1A, width);
    battleInitPoly4(&pBattleRenderStruct2->m5C80_fuelCosts[index * 2], 0, 3);
    /* fuel costs at 0x800D2C0C, reached through battleOddOrEven's address */
#ifdef PLATFORM_PC
    printIntegerToStringBattle(battleEntities.m5F24_gearTurn.m0_fuelCosts[cmd]); /* the views differ on the PC */
#else
    printIntegerToStringBattle(D_800CCB04.m6108_gearTurn.m0_fuelCosts[cmd]);
#endif
    for (i = 0, xpos = index * 8 + 0x3DE; i < 4; i++) {
        if (p[i] != 0xFF) {
            /* retail keeps the digit's byte offset and turns it into the
             * address of the RECT before it (stores at +8..+14) */
            offset = n;
            offset <<= 3;
            rect = &uploads[1];
            rect = (RECT*)((u8*)rect + offset);
            offset += (sptr)uploads;
            ((RECT*)offset)[1].x = xpos;
            ((RECT*)offset)[1].y = 0;
            ((RECT*)offset)[1].w = 6;
            ((RECT*)offset)[1].h = 13;
            xpos += 2;
            loadImageSync(rect, *(u8**)((u8*)itemLabelsIds + PTR_TABLE_OFS(p[i], sBattleTextImage)));
            n++;
        }
    }
    repositionTextRenderingPlanePrim(&pBattleRenderStruct2->m5C80_fuelCosts[index * 2 + D_800CCB04.m30_battleOddOrEven], index * 4 + (x + 1) * 16 + 0xEA,
                  0xC8 - index * 16, index * 32 + 0x78, 0, n * 8);
    pBattleRenderStruct2->m5D70_count[0x10]++;
    pBattleRenderStruct2->m5D70_count[0x12]++;
    return ++index;
}

u8 updateGearDeathblowComboInput(u8 cmd, u8 entity) {
    s32 n;
    s32 result;
    u8 sel;
    s32 i;
    s32 j;
    u8 id;
    u8 icon;
#if VERSION_JP
    u16* images[3];
#define NAME_IMAGES images
#else
#define NAME_IMAGES deathblowNameImages
#endif

    sel = (cmd + 1) * 3;
    n = 0;
    result = 0;
    if (!(battleEntities.m0_entities[battleVar2->m0_entityMenus[entity].m3C_currentTarget].m15A_flags & 0x80) && battleEntities.m5F24_gearTurn.m28_mode != 4) {
        battleVar2->m2DC_attackIndex = gearDeathblowAbilityIds[cmd];
        return 1;
    }
    if ((battleEntities.m5F24_gearTurn.m28_mode != 0 && battleEntities.m5F24_gearTurn.m28_mode - 1 >= cmd) || battleVar2->m2E7_gearAttackCount != 0) {
        /* retail walks deathblowNameImages by byte offset (a pointer apart) */
#if VERSION_JP
        for (j = 0; j < 3; j++) {
            images[j] = allocateTextureRamForText(0x1B);
        }
#else
        for (j = 0; j < 3 * (s32)sizeof(u16*); j += (s32)sizeof(u16*)) {
            *(u16**)((u8*)deathblowNameImages + j) = allocateTextureRamForText(0x1E);
        }
#endif
        battleVar2->m2CC_comboInput[battleVar2->m2D6_comboInputLength] = cmd;
        battleVar2->m2D6_comboInputLength++;
        if (battleVar2->m2E4_deathblowCompleted != 0) {
            battleVar2->m2CC_comboInput[battleVar2->m2D6_comboInputLength - 1] = 0xFF;
            battleVar2->m2CC_comboInput[battleVar2->m2D6_comboInputLength - 1] = cmd;
            sel = cmd + (battleVar2->m2CC_comboInput[0] + 1) * 3;
        }
        pBattleRenderStruct2->m5D70_count[0x11] = 0;
        pBattleRenderStruct2->m5D70_count[0x10] = 0;
        pBattleRenderStruct2->m5D70_count[0x12] = 0;
        for (i = 0; i < battleVar2->m2D6_comboInputLength; i++) {
            if (battleVar2->m2CC_comboInput[i] != 0xFF) {
                switch (battleVar2->m2CC_comboInput[i]) {
                case 0:
                    icon = 0x5E;
                    break;
                case 1:
                    icon = 0x5F;
                    break;
                case 2:
                    icon = 0x5D;
                    break;
                }
                pBattleRenderStruct2->m5D70_count[0x11] += battleSetupStringInPolyFT4Large(icon, &pBattleRenderStruct2->m5640_comboInputIcons[pBattleRenderStruct2->m5D70_count[0x11] * 2], i * 16 + 0x80, 0xD0 - n * 16);
            }
        }
        if (battleEntities.m5F24_gearTurn.m28_mode == 4) {
            if (cmd != 0xFF) {
                sel = cmd + 12;
            } else {
                sel = 12;
            }
        }
        id = sel;
        if (bitmaskCharacterCheckReverse(gameState.m16C0_abilities[battleCharacters[entity]].m0_unlockedAbilitiesBitField[2], gearDeathblowAbilityIds[id])) {
            if (battleVar2->m2E4_deathblowCompleted == 0) {
                if (battleVar2->m0_entityMenus[entity].m1C_isCommandEnabled[0] == 0) {
                    pBattleRenderStruct2->m5D70_count[0x11] += battleSetupStringInPolyFT4Large(8, &pBattleRenderStruct2->m5640_comboInputIcons[pBattleRenderStruct2->m5D70_count[0x11] * 2], i * 16 + 0x80, 0xD0 - n * 16);
                    n = addGearDeathblowNamePoly(entity, n, i - 1, id, &NAME_IMAGES[n / 2]);
                }
                result = 1;
            } else {
                n = addGearDeathblowNamePoly(entity, n, i - 1, id, &NAME_IMAGES[n / 2]);
                result = 1;
            }
        }
        if (battleVar2->m2E4_deathblowCompleted != 0 && result != 0) {
            result = 0;
            if (battleVar2->m2CC_comboInput[0] == 0xFF) {
                sel = cmd + 12;
            }
            pBattleRenderStruct2->m5D83_oddOrEven[0xD] = D_800CCB04.m30_battleOddOrEven;
            battleVar2->m2DC_attackIndex = gearDeathblowAbilityIds[sel];
            pBattleRenderStruct2->m5D83_oddOrEven[0xC] = D_800CCB04.m30_battleOddOrEven;
            pBattleRenderStruct2->m5D83_oddOrEven[0xE] = D_800CCB04.m30_battleOddOrEven;
            battleVar1->mA8_drawComboInput = 1;
            battleRenderDebugAndMain();
#if VERSION_JP
            for (j = 0; j < 3; j++) {
                freeHeap(images[j]);
            }
#else
            for (j = 0; j < 3 * (s32)sizeof(u16*); j += (s32)sizeof(u16*)) {
                freeHeap(*(u16**)((u8*)deathblowNameImages + j));
            }
#endif
            return result;
        }
        battleVar2->m2DC_attackIndex = cmd;
        if (battleEntities.m5F24_gearTurn.m28_mode != 4) {
            id = (cmd + 1) * 3 + 1;
        } else {
            id = 0xD;
        }
        if (bitmaskCharacterCheckReverse(gameState.m16C0_abilities[battleCharacters[entity]].m0_unlockedAbilitiesBitField[2], gearDeathblowAbilityIds[id]) &&
            battleVar2->m0_entityMenus[entity].m1C_isCommandEnabled[1] == 0) {
            pBattleRenderStruct2->m5D70_count[0x11] += battleSetupStringInPolyFT4Large(9, &pBattleRenderStruct2->m5640_comboInputIcons[pBattleRenderStruct2->m5D70_count[0x11] * 2], i * 16 + 0x84, 0xD0 - n * 16);
            n = addGearDeathblowNamePoly(entity, n, i, id, &NAME_IMAGES[n / 2]);
            result = 1;
        }
        if (battleEntities.m5F24_gearTurn.m28_mode != 4) {
            id = (cmd + 1) * 3 + 2;
        } else {
            id = 0xE;
        }
        if (bitmaskCharacterCheckReverse(gameState.m16C0_abilities[battleCharacters[entity]].m0_unlockedAbilitiesBitField[2], gearDeathblowAbilityIds[id]) &&
            battleVar2->m0_entityMenus[entity].m1C_isCommandEnabled[2] == 0) {
            pBattleRenderStruct2->m5D70_count[0x11] += battleSetupStringInPolyFT4Large(7, &pBattleRenderStruct2->m5640_comboInputIcons[pBattleRenderStruct2->m5D70_count[0x11] * 2], i * 16 + 0x88, 0xD0 - n * 16);
            addGearDeathblowNamePoly(entity, n, i, id, &NAME_IMAGES[n / 2]);
            result = 1;
        }
        pBattleRenderStruct2->m5D83_oddOrEven[0xD] = D_800CCB04.m30_battleOddOrEven;
        pBattleRenderStruct2->m5D83_oddOrEven[0xC] = D_800CCB04.m30_battleOddOrEven;
        pBattleRenderStruct2->m5D83_oddOrEven[0xE] = D_800CCB04.m30_battleOddOrEven;
        battleVar1->mA8_drawComboInput = 1;
        battleRenderDebugAndMain();
#if VERSION_JP
        for (j = 0; j < 3; j++) {
            freeHeap(images[j]);
        }
#else
        for (j = 0; j < 3 * (s32)sizeof(u16*); j += (s32)sizeof(u16*)) {
            freeHeap(*(u16**)((u8*)deathblowNameImages + j));
        }
#endif
    } else {
        battleVar2->m2DC_attackIndex = cmd;
        result = 1;
    }
    return result;
}
#undef NAME_IMAGES

typedef struct { sBattleSlotPositions m0[8]; } sBattleJumpPointView;
typedef struct {
    u8 pad[0x140];
    u8 m140[8][8];
} sJumpView;
s32 initJumpData(u8 entity, u8 target) {
    s32 result = 0;
    s32 i;
    u8* p;
    u8* blk;
    u8* row;
    sBattleMechaInitData* cfg;

    for (i = 0; i < 9; i++) {
        battleBssBlock.mA3C_jumpPoints[i].m0_x = 0xFFFF;
        battleBssBlock.mA3C_jumpPoints[i].m2_z = 0xFFFF;
    }
    battleBssBlock.mA3C_jumpPoints[0].m0_x = battleVisualEntities[entity].mA_X;
    battleBssBlock.mA3C_jumpPoints[0].m2_z = battleVisualEntities[entity].mC_Z;
    battleBssBlock.mA3C_jumpPoints[0].m4_flags = 0;
    for (i = 1; i < 8; i++) {
        cfg = battleMechaInitData;
        blk = (u8*)cfg + (battleVisualEntities[entity].m0_positionSlot << 6);
        row = (u8*)(battleVisualEntities[target].m0_positionSlot * 8 + (sptr)blk) + 0x140;
        p = row + i;
        if (*p == 0xFF) {
            break;
        }
        battleBssBlock.mA3C_jumpPoints[i].m0_x = ((sBattleJumpPointView*)battleMechaInitData)->m0[*p & 7].m0_party[0].vx;
        battleBssBlock.mA3C_jumpPoints[i].m2_z = ((sBattleJumpPointView*)battleMechaInitData)->m0[((sJumpView*)((u8*)battleMechaInitData + (battleVisualEntities[entity].m0_positionSlot << 6)))->m140[battleVisualEntities[target].m0_positionSlot][i] & 7].m0_party[0].vy;
        battleBssBlock.mA3C_jumpPoints[i].m4_flags = ((sJumpView*)((u8*)battleMechaInitData + (battleVisualEntities[entity].m0_positionSlot << 6)))->m140[battleVisualEntities[target].m0_positionSlot][i] & 0x80;
    }
    if (battleCharacters[entity] == 4) {
        result = 1;
    }
    return result;
}

/* 72 263 260] */
void initAnimSeqFromCharacterToCharacter(s32 entity, s32 target) {
    s32 i;

    for (i = 0; i < 32; i++) {
        battleCurrentDamages[i].m47_battleAnimationToPlay = 0xFF;
        battleCurrentDamages[i].m23_battleEntityIndex = entity;
        battleCurrentDamages[i].m16_targetBitMask = characterIdToTargetBitmask(target);
    }
}

/* 72 263 260] */
void startCharacterJumpToEnemy(u8 idx) {
    s32 n;

    updateAPCounterDisplay();
    initAnimSeqFromCharacterToCharacter(idx, battleVar2->m0_entityMenus[idx].m3C_currentTarget);
    startCharacterJumpToEnemyVar0 = 0;
    n = initJumpData(idx, battleVar2->m0_entityMenus[idx].m3C_currentTarget);
    startJumpAnimation(n, idx, battleVar2->m0_entityMenus[idx].m3C_currentTarget, computeFacingForJump(idx));
    startCharacterJumpToEnemyVar0 = 1;
    startCharacterJumpToEnemyVar1 = 0;
}

s32 performAttack(u8 entity, u8 arg1) {
    u8 result = 0;
    u8 idx;

    if (startCharacterJumpToEnemyVar1 == 0) {
        if (apConfigArray[entity].m1_gearState == 0) {
            if (battleCharacters[entity] != 4) {
                moveEntityToOtherEntity(entity, battleVar2->m0_entityMenus[entity].m3C_currentTarget);
            }
        } else {
            moveEntityToShootEntity(entity, battleVar2->m0_entityMenus[entity].m3C_currentTarget);
        }
        startCharacterJumpToEnemyVar1 = 1;
    }
    performAttackSub2();
    if (apConfigArray[entity].m1_gearState == 0) {
        if (battleVar2->m2DC_attackIndex < 8) {
            s32 column = arg1; /* an int copy: retail masks arg1 before the row index */
            battleVar2->m2DC_attackIndex = perApTable[battleVar2->m2DC_attackIndex][column];
        } else if (bitmaskCharacterCheckReverse(gameState.m16C0_abilities[battleCharacters[entity]].m0_unlockedAbilitiesBitField[0], battleVar2->m2DC_attackIndex - 8)) {
            result = 1;
        } else {
            battleVar2->m2DC_attackIndex = 7;
        }
    } else {
        battleVar2->m2DC_attackIndex++;
    }
    if (battleEntities.m0_entities[battleVar2->m0_entityMenus[entity].m3C_currentTarget].m0_base.m34_monsterFlags & 0x800) {
        battleCurrentDamages[battleVar2->m2DA_indexInBattleVar48].m23_battleEntityIndex = entity;
        battleCurrentDamages[battleVar2->m2DA_indexInBattleVar48].m47_battleAnimationToPlay = 0xF3;
        battleCurrentDamages[battleVar2->m2DA_indexInBattleVar48].m3A_animationParam = characterIdToTargetBitmask(battleVar2->m0_entityMenus[entity].m3C_currentTarget);
        battleVar2->m2DA_indexInBattleVar48++;
    }
    performAttackSub3(entity, characterIdToTargetBitmask(battleVar2->m0_entityMenus[entity].m3C_currentTarget), battleVar2->m2DC_attackIndex - 1);
    idx = battleVar2->m2DA_indexInBattleVar48;
    battleCurrentDamages[battleVar2->m2DA_indexInBattleVar48].m23_battleEntityIndex = entity;
    battleCurrentDamages[battleVar2->m2DA_indexInBattleVar48].m16_targetBitMask = entitiesHitInCurrentAttackBF[0];
    battleCurrentDamages[battleVar2->m2DA_indexInBattleVar48].m47_battleAnimationToPlay = performAttack_type;
    battleVar2->m2DA_indexInBattleVar48++;
    updateMonsterScriptEntitiesVarByAtttack(entity, battleVar2->m0_entityMenus[entity].m3C_currentTarget);
    if (battleVar2->m0_entityMenus[entity].m3C_currentTarget >= 3 && executeMonsterScriptWhenAttacked(battleVar2->m0_entityMenus[entity].m3C_currentTarget)) {
        result = 1;
    }
    performAttackSub6(idx);
    return result;
}

/* Moves an entity next to its target (joins the target's slot when it has
 * room, first free sub-position). The chained monsterOffset = 0 keeps the
 * offset choice an if/else for jump.c (no store-flag), which also makes
 * cse drop the target values at the join as retail does. */
void moveEntityToOtherEntity(u8 entity, u8 target) {
    u8 offset;
    s32 i;
    u8 monsterOffset;

    if (battleVisualEntities[entity].m0_positionSlot != battleVisualEntities[target].m0_positionSlot) {
        monsterOffset = (entity >= 3) * 8;
        if (target < 3) {
            offset = monsterOffset;
        } else {
            offset = monsterOffset = 0;
        }
        if (battleSlotLayout[battleVisualEntities[target].m0_positionSlot + offset].m0_count < 4) {
            markEnemyDead(entity);
            battleSlotLayout[battleVisualEntities[target].m0_positionSlot + offset].m0_count++;
            for (i = 0; i < 4; i++) {
                if (bitmaskCharacterCheck(battleSlotLayout[battleVisualEntities[target].m0_positionSlot + offset].m1_mask, i) == 0) {
                    break;
                }
            }
            battleVisualEntities[entity].m0_positionSlot = battleVisualEntities[target].m0_positionSlot;
            battleVisualEntities[entity].m1_indexInSlot = i;
            battleSlotLayout[battleVisualEntities[entity].m0_positionSlot + offset].m1_mask |= characterIdToTargetBitmask(battleVisualEntities[entity].m1_indexInSlot);
            if (entity < 3) {
                battleVisualEntities[entity].mA_X = battleMechaInitData->m4_slotPositions[battleVisualEntities[entity].m0_positionSlot].m0_party[battleVisualEntities[entity].m1_indexInSlot].vx;
                battleVisualEntities[entity].mC_Z = battleMechaInitData->m4_slotPositions[battleVisualEntities[entity].m0_positionSlot].m0_party[battleVisualEntities[entity].m1_indexInSlot].vy;
            } else {
                battleVisualEntities[entity].mA_X = battleMechaInitData->m4_slotPositions[battleVisualEntities[entity].m0_positionSlot].mC_monsters[battleVisualEntities[entity].m1_indexInSlot].vx;
                battleVisualEntities[entity].mC_Z = battleMechaInitData->m4_slotPositions[battleVisualEntities[entity].m0_positionSlot].mC_monsters[battleVisualEntities[entity].m1_indexInSlot].vy;
            }
        }
    }
}

/* Moves an entity into its target's position slot (no Noah name).
 * The visual entities are reached through the battleBssBlock aggregate: its
 * sym+4 addresses make GCC assume they may alias the battleSlotLayout stores, so
 * the slot is reloaded after each store as in retail. entity < 3 is
 * computed before the target test (retail keeps it in v1). */
void moveEntityToShootEntity(u8 entity, u8 target) {
    u8 offset;
    s32 entityIsParty;

    if (battleBssBlock.m4_visualEntities[entity].m0_positionSlot != battleBssBlock.m4_visualEntities[target].m0_positionSlot) {
        entityIsParty = entity < 3;
        if (target < 3) {
            offset = 0x18;
            if (entityIsParty) {
                offset = 0x10;
            }
        } else {
            offset = 0x10;
        }
        if (battleSlotLayout[battleBssBlock.m4_visualEntities[target].m0_positionSlot + offset].m0_count == 0) {
            markEnemyDead(entity);
            battleBssBlock.m4_visualEntities[entity].m0_positionSlot = battleBssBlock.m4_visualEntities[target].m0_positionSlot;
            battleBssBlock.m4_visualEntities[entity].m1_indexInSlot = 0;
            battleSlotLayout[battleBssBlock.m4_visualEntities[entity].m0_positionSlot + offset].m0_count = 1;
            battleSlotLayout[battleBssBlock.m4_visualEntities[entity].m0_positionSlot + offset].m1_mask = 1;
            if (entity < 3) {
                battleBssBlock.m4_visualEntities[entity].mA_X = battleMechaInitData->m100_gearPositions[battleBssBlock.m4_visualEntities[entity].m0_positionSlot][0].vx;
                battleBssBlock.m4_visualEntities[entity].mC_Z = battleMechaInitData->m100_gearPositions[battleBssBlock.m4_visualEntities[entity].m0_positionSlot][0].vy;
            } else {
                battleBssBlock.m4_visualEntities[entity].mA_X = battleMechaInitData->m100_gearPositions[battleBssBlock.m4_visualEntities[entity].m0_positionSlot][1].vx;
                battleBssBlock.m4_visualEntities[entity].mC_Z = battleMechaInitData->m100_gearPositions[battleBssBlock.m4_visualEntities[entity].m0_positionSlot][1].vy;
            }
        }
    }
}

/* 72 263 260] */
void markEnemyDead(u8 entity) {
    u8 offset = (entity >= 3) * 8;

    if (apConfigArray[entity].m1_gearState != 0) {
        offset |= 0x10;
    }
    battleSlotLayout[battleVisualEntities[entity].m0_positionSlot + offset].m0_count--;
    battleSlotLayout[battleVisualEntities[entity].m0_positionSlot + offset].m1_mask &= characterOdToInverseTargetBitmask(battleVisualEntities[entity].m1_indexInSlot);
}

void callGearRelocateSlot(u8 entity) {
    u8 slot;
    u8 newSlot;
    s32 i;

    slot = battleVisualEntities[entity].m0_positionSlot;
    if (battleSlotLayout[slot + 16].m0_count == 0) {
        newSlot = slot;
    } else {
        for (i = 0; i < 8; i++) {
            if (battleSlotLayout[i + 16].m0_count == 0) {
                newSlot = i;
                break;
            }
        }
    }
    battleVisualEntities[entity].m0_positionSlot = newSlot;
    battleVisualEntities[entity].m1_indexInSlot = 0;
    battleSlotLayout[battleVisualEntities[entity].m0_positionSlot + 16].m1_mask = 1;
    battleSlotLayout[battleVisualEntities[entity].m0_positionSlot + 16].m0_count = 1;
    battleVisualEntities[entity].mA_X = battleMechaInitData->m100_gearPositions[battleVisualEntities[entity].m0_positionSlot][0].vx;
    battleVisualEntities[entity].mC_Z = battleMechaInitData->m100_gearPositions[battleVisualEntities[entity].m0_positionSlot][0].vy;
}

/* 72 263 260] */
u8 getBattleSlotLayout(u8 i) {
    return battleSlotLayout[battleVisualEntities[i].m0_positionSlot + 0x18].m0_count;
}
