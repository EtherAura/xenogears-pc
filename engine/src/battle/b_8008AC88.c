/* Provisional file (split where JP links the code in another order). */
#include "common.h"
#include "psyq.h"
#include "main/game_state.h"
#include "main/filesystem.h"
#include "main/memory.h"
#include "main/dialog.h"
#include "main/debug_font.h"
#include "battle/battle.h"

extern sBattle800cdd40 partyBattleStats[3] PORT_ALIAS;
extern sBattle800CEF10 mechaBattleStats[3] PORT_ALIAS;
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

/* 263 260] */
void executeAbilitySub0(u16 mask, u8 entity) {
    s32 i;
    u16 bits = mask & 0xFFF8;

    for (i = 0; i < 8; i++) {
        if (bitmaskCharacterCheck(bits, i + 3) != 0 && !(battleEntities.m0_entities[i + 3].m0_base.m34_monsterFlags & 0x800)) {
            bits &= characterOdToInverseTargetBitmask(i + 3);
        }
    }
    if (bits != 0) {
        battleCurrentDamages[battleVar2->m2DA_indexInBattleVar48].m23_battleEntityIndex = entity;
        battleCurrentDamages[battleVar2->m2DA_indexInBattleVar48].m47_battleAnimationToPlay = 0xF3;
        battleCurrentDamages[battleVar2->m2DA_indexInBattleVar48].m3A_animationParam = bits;
        battleVar2->m2DA_indexInBattleVar48++;
    }
}

/* 263 260] */
void executeAbility(u8 entity) {
    s32 i;
    u16 mask;
    s32 flags;

    startCharacterJumpToEnemyVar0 = 0;
    updateCharacterBlinkingTask(0);
    for (i = 0; i < 32; i++) {
        battleCurrentDamages[i].m47_battleAnimationToPlay = 0xFF;
    }
    if (apConfigArray[entity].m1_gearState == 0) {
        battleVar2->m2DC_attackIndex = battleVar2->m2E6_menuSelection + 0x17;
        flags = partyBattleStats[entity].m0_commands[battleVar2->m2E6_menuSelection + 0x16].m0_flags & 7;
    } else {
        battleVar2->m2DC_attackIndex = battleVar2->m2E6_menuSelection + 0x16;
        flags = mechaBattleStats[entity].m0_commands[battleVar2->m2E6_menuSelection + 0x15].m0_flags & 7;
    }
    if (flags != 0) {
        mask = abilityTargetBitmask;
    } else {
        mask = characterIdToTargetBitmask(abilitySelectedTarget);
    }
    executeAbilitySub0(mask, entity);
    performAttackSub2();
    performAttackSub3(entity, mask, battleVar2->m2DC_attackIndex - 1);
    performAttackSub6(battleVar2->m2DA_indexInBattleVar48);
    setCameraVisibleEntities(entitiesHitInCurrentAttackBF[0] | characterIdToTargetBitmask(entity));
    startJumpAnimation(1, entity, battleVar2->m0_entityMenus[entity].m3C_currentTarget, computeFacingForJump(entity));
    battleCurrentDamages[battleVar2->m2DA_indexInBattleVar48].m47_battleAnimationToPlay = performAttack_type;
    battleCurrentDamages[battleVar2->m2DA_indexInBattleVar48].m23_battleEntityIndex = entity;
    battleCurrentDamages[battleVar2->m2DA_indexInBattleVar48].m16_targetBitMask = entitiesHitInCurrentAttackBF[0];
    for (i = 3; i < 11; i++) {
        if (bitmaskCharacterCheck(entitiesHitInCurrentAttackBF[0], i)) {
            updateMonsterScriptEntitiesVarByAtttack(entity, i);
        }
    }
    battleVar2->m2DA_indexInBattleVar48++;
    handleMenuSelectEnemy_cancel_sub0(entity);
    while (battleVar2->m2DB_turnEnded == 0) {
        battleRenderDebugAndMain();
    }
}

/* 72 263 260] */
void drawCircleMenuChi_updateSub5(u8 flag) {
    battleVar1->m9C_renderBA8 = battleVar1->m9D_render1E68 = battleVar1->m9E_render27C8 = 0;
    battleVar1->mB0_isDialogWindowInitialized[0] = battleVar1->mB0_isDialogWindowInitialized[1] =
        battleVar1->mB0_isDialogWindowInitialized[2] = battleVar1->mB0_isDialogWindowInitialized[3] = 0;
    battleVar1->mB7_openMenuType = 0;
    if (flag == 0) {
        battleVar1->mCB_drawCommandRing = 1;
    }
}

/* 72 263 260] */
void abilityCancelSelection(u8 entity) {
    battleVar1->m9C_renderBA8 = battleVar1->m9D_render1E68 = battleVar1->m9E_render27C8 = 1;
    battleVar1->mB0_isDialogWindowInitialized[0] = battleVar1->mB0_isDialogWindowInitialized[1] =
        battleVar1->mB0_isDialogWindowInitialized[2] = battleVar1->mB0_isDialogWindowInitialized[3] = 1;
    battleVar1->mB7_openMenuType = 1;
    setCameraVisibleEntities(characterIdToTargetBitmask(entity) | characterIdToTargetBitmask(battleVar2->m0_entityMenus[entity].m3C_currentTarget));
    updateCharacterBlinkingTask(characterIdToTargetBitmask(battleVar2->m0_entityMenus[entity].m3C_currentTarget));
}

/* 72 263 260] */
u8 useAbility(u8 entity, u8 cmd, u8 page) {
    u8 result = 0;
    u8 playSound = 1;
    u8 ok = 0;
    u16 flags;
    u8 cost;

    if (apConfigArray[entity].m1_gearState == 0) {
        flags = partyBattleStats[entity].m0_commands[page * 2 + cmd + 0x16].m0_flags;
        cost = partyBattleStats[entity].m0_commands[page * 2 + cmd + 0x16].m13_cost;
        ok = bitmaskCharacterCheckReverse(gameState.m16C0_abilities[battleCharacters[entity]].m0_unlockedAbilitiesBitField[1], cmd + page * 2) != 0;
    } else {
        flags = mechaBattleStats[entity].m0_commands[page * 2 + cmd + 0x15].m0_flags;
        cost = mechaBattleStats[entity].m0_commands[page * 2 + cmd + 0x15].m13_cost;
        if (bitmaskCharacterCheckReverse(gameState.m16C0_abilities[battleCharacters[entity]].m0_unlockedAbilitiesBitField[3], cmd + page * 2)) {
            ok = 1;
        }
    }
    if (battleEntities.m0_entities[entity].m0_base.m50_MP >= cost && ok) {
        drawCircleMenuChi_updateSub5(1);
        battleVar1->mC6_isTargetSelectionCursorVisible = 1;
        if (abilitySelectTarget(flags, entity, 0)) {
            battleEntities.m0_entities[entity].m0_base.m50_MP -= cost;
            result = 1;
        } else {
            battleVar1->mC6_isTargetSelectionCursorVisible = 0;
            abilityCancelSelection(entity);
        }
        playSound = 0;
    }
    if (playSound) {
        battleSoundEffect2(0x4F);
    }
    return result;
}
