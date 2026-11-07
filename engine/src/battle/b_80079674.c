/*
 * Monster AI script interpreter (Noah enemyScript.cpp): executeMonsterScript
 * (executeMonsterScript) and the opcode handlers dispatched by executeMonsterScriptLower
 * (executeMonsterScriptLower) and executeMonsterScriptUpper (executeMonsterScriptUpper).
 * 0x80079674-0x80080AE4; gcc-2.6.3-psx, ASPSX 2.34.
 */
#include "common.h"
#include "pc_gameplay.h"
#include "battle/battle.h"
#include "main/memory.h"
#include "main/filesystem.h"
#include "psyq.h"
#include "main/game_state.h"

u16 bitmaskCharacterCheck(u16 bitmask, u8 entity);

/* Main executable data (D_8005..., D_8006...: inside gameState and friends) and battle data/bss */
extern s16 battleScriptGameStateVars[];
extern u16 party1C_InitialValues[16];
extern void* gearCommandStrings;
extern u8 newBattleInputButton2;
extern u8 battleRunningVar1;
extern u8 D_800D2C08[];
extern u8 D_800D2CA4[];
extern sBattleItemList battleEffectBF;
extern u8 battleCharacters[];
extern u8 bBattleTickMode1;
extern u8 battleInputButton;
extern u8 battleSlotLayout[];
extern u8* battleMechaInitData;
extern u8 startCharacterJumpToEnemyVar0;
extern void* chiData;

/* Functions in main and in other battle files */
u8 randomInRange(s32 min, s32 max);
void battleRenderDebugAndMain(void);
void freeBattleVar0_A230(void);
void battleGetSlotStatus(void*);
void executeBattleCode(u8);
void monsterBattleOpcode_setSelfExperience(sBattleScriptExecutionContext* ctx, u8 entity);
s32 computeFacingForJump(u8);
void handleMenuSelectEnemy(u8);
void handleGearMenuSelectEnemy(u8);
void battleTickMainSub0(void);
void performAttackSub2(void);
void resetBattleVar2_2CC(void);
void moveEntityToOtherEntity(u8, u8);
void markEnemyDead(u8);
void updateGearHudFuel(u8);
u16 characterIdToReverseBitmask(u8);
void setFolderChiData(void);
void* allocateBattleHeap(s32 size, s32);
void idleBattleDuringLoading(void);
void setupGearTurnState(u8);
void clearEntityFlag15A_1(u8);

/* Prototypes of this file's functions */
void finishMonsterTurn(u8 entity);
void monsterJumpToTarget(u8 entity);
void updateMonsterScriptEntitiesVarByAtttack(u8 attacker, u8 target);
void advanceBattleScript(sBattleScriptExecutionContext* ctx);
void skipScriptIfFalse(sBattleScriptExecutionContext* ctx);
s32 executeMonsterScriptWhenAttacked(u8 entity);
void execMonsterScriptEndOfPlayerTurn(void);
u8 monsterBattleOpcode_countEnemiesSameRow_findFirstInBitmask(u16 bitmask);
u8 isMonsterTargetValid(u8 entity, u8 allowHidden);
u8 isEnemySlotTargetable(u8 entity, u8 allowHidden);
u8 isBattleEntityAliveAndVisible(u8 entity);
u8 monsterBattleOpcode_queueLanguageError(sBattleScriptExecutionContext* ctx, sBattleScriptContext* contexts, u8 entity, u8 index);
u8 monsterBattleOpcode_writeContextByte(sBattleScriptExecutionContext* ctx, sBattleScriptContext* contexts, u8 index);
void monsterBattleOpcode_writeContextByteVar(sBattleScriptExecutionContext* ctx, sBattleScriptContext* contexts, u8 entity, u8 index);
void monsterBattleOpcode_copyContextEntry(sBattleScriptExecutionContext* ctx, sBattleScriptContext* contexts);
void monsterBattleOpcode_setByteVar(sBattleScriptExecutionContext* ctx, u8 entity);
void monsterBattleOpcode_setWordVar(sBattleScriptExecutionContext* ctx, u8 entity);
void monsterBattleOpcode_setDwordVar(sBattleScriptExecutionContext* ctx, u8 entity);
void monsterBattleOpcode_setGameStateVar(sBattleScriptExecutionContext* ctx);
void monsterBattleOpcode_addByteVar(sBattleScriptExecutionContext* ctx, u8 entity);
void monsterBattleOpcode_subByteVar(sBattleScriptExecutionContext* ctx, u8 entity);
void monsterBattleOpcode_mulByteVar(sBattleScriptExecutionContext* ctx, u8 entity);
void monsterBattleOpcode_divByteVar(sBattleScriptExecutionContext* ctx, u8 entity);
void monsterBattleOpcode_moduloByteVar(sBattleScriptExecutionContext* ctx, u8 entity);
void monsterBattleOpcode_andByteVar(sBattleScriptExecutionContext* ctx, u8 entity);
void monsterBattleOpcode_orByteVar(sBattleScriptExecutionContext* ctx, u8 entity);
void monsterBattleOpcode_xorByteVar(sBattleScriptExecutionContext* ctx, u8 entity);
void monsterBattleOpcode_addWordVar(sBattleScriptExecutionContext* ctx, u8 entity);
void monsterBattleOpcode_subWordVar(sBattleScriptExecutionContext* ctx, u8 entity);
void monsterBattleOpcode_mulWordVar(sBattleScriptExecutionContext* ctx, u8 entity);
void monsterBattleOpcode_divWordVar(sBattleScriptExecutionContext* ctx, u8 entity);
void monsterBattleOpcode_moduloWordVar(sBattleScriptExecutionContext* ctx, u8 entity);
void monsterBattleOpcode_andWordVar(sBattleScriptExecutionContext* ctx, u8 entity);
void monsterBattleOpcode_orWordVar(sBattleScriptExecutionContext* ctx, u8 entity);
void monsterBattleOpcode_xorWordVar(sBattleScriptExecutionContext* ctx, u8 entity);
void monsterBattleOpcode_addByteVars(sBattleScriptExecutionContext* ctx, u8 entity);
void monsterBattleOpcode_subByteVars(sBattleScriptExecutionContext* ctx, u8 entity);
void monsterBattleOpcode_mulByteVars(sBattleScriptExecutionContext* ctx, u8 entity);
void monsterBattleOpcode_divByteVars(sBattleScriptExecutionContext* ctx, u8 entity);
void monsterBattleOpcode_moduloByteVars(sBattleScriptExecutionContext* ctx, u8 entity);
void monsterBattleOpcode_andByteVars(sBattleScriptExecutionContext* ctx, u8 entity);
void monsterBattleOpcode_orByteVars(sBattleScriptExecutionContext* ctx, u8 entity);
void monsterBattleOpcode_xorByteVars(sBattleScriptExecutionContext* ctx, u8 entity);
void monsterBattleOpcode_addWordVars(sBattleScriptExecutionContext* ctx, u8 entity);
void monsterBattleOpcode_subWordVars(sBattleScriptExecutionContext* ctx, u8 entity);
void monsterBattleOpcode_mulWordVars(sBattleScriptExecutionContext* ctx, u8 entity);
void monsterBattleOpcode_divWordVars(sBattleScriptExecutionContext* ctx, u8 entity);
void monsterBattleOpcode_moduloWordVars(sBattleScriptExecutionContext* ctx, u8 entity);
void monsterBattleOpcode_andWordVars(sBattleScriptExecutionContext* ctx, u8 entity);
void monsterBattleOpcode_orWordVars(sBattleScriptExecutionContext* ctx, u8 entity);
void monsterBattleOpcode_xorWordVars(sBattleScriptExecutionContext* ctx, u8 entity);
void monsterBattleOpcode_setSelfByteStat(sBattleScriptExecutionContext* ctx, u8 entity);
void monsterBattleOpcode_setSelfWordStat(sBattleScriptExecutionContext* ctx, u8 entity);
void monsterBattleOpcode_getTargetByteStat(sBattleScriptExecutionContext* ctx, u8 entity);
u8 monsterBattleOpcode_setTargetByteStat(sBattleScriptExecutionContext* ctx, u8 entity, u8 index);
void monsterBattleOpcode_getTargetWordStat(sBattleScriptExecutionContext* ctx, u8 entity);
u8 monsterBattleOpcode_setTargetWordStat(sBattleScriptExecutionContext* ctx, u8 entity, u8 index);
void monsterBattleOpcode_loadGearHpOrGold(sBattleScriptExecutionContext* ctx, u8 entity);
u8 monsterBattleOpcode_setTargetGearHp(sBattleScriptExecutionContext* ctx, u8 entity, u8 index);
void monsterBattleOpcode_loadGameStateVar(sBattleScriptExecutionContext* ctx, u8 entity);
void monsterBattleOpcode_storeGameStateVar(sBattleScriptExecutionContext* ctx, u8 entity);
void monsterBattleOpcode_copyByteVar(sBattleScriptExecutionContext* ctx, u8 entity);
void monsterBattleOpcode_copyWordVar(sBattleScriptExecutionContext* ctx, u8 entity);
void monsterBattleOpcode_copyDwordVar(sBattleScriptExecutionContext* ctx, u8 entity);
void monsterBattleOpcode_byteVarToWordVar(sBattleScriptExecutionContext* ctx, u8 entity);
void monsterBattleOpcode_wordVarToDwordVar(sBattleScriptExecutionContext* ctx, u8 entity);
void monsterBattleOpcode_clearByteVars(u8 entity);
void monsterBattleOpcode_clearWordVars(u8 entity);
void monsterBattleOpcode_setSelfGold(sBattleScriptExecutionContext* ctx, u8 entity);
void monsterBattleOpcode_setSelfDrop2(sBattleScriptExecutionContext* ctx, u8 entity);
void monsterBattleOpcode_setSelfDrop1(sBattleScriptExecutionContext* ctx, u8 entity);
void monsterBattleOpcode_writeContextWord(sBattleScriptExecutionContext* ctx, sBattleScriptContext* contexts, u8 index);
void monsterBattleOpcode_randomByteVar(sBattleScriptExecutionContext* ctx, u8 entity);
void monsterBattleOpcode_randomWordVar(sBattleScriptExecutionContext* ctx, u8 entity);
void monsterBattleOpcode_selectRandomPC(sBattleScriptExecutionContext* ctx, u8 entity);
void monsterBattleOpcode_selectRandomPCSameRow(sBattleScriptExecutionContext* ctx, u8 entity);
void monsterBattleOpcode_selectRandEnemySameRow(sBattleScriptExecutionContext* ctx, u8 entity);
void monsterBattleOpcode_selectRandomPCDiffRow(sBattleScriptExecutionContext* ctx, u8 entity);
void monsterBattleOpcode_selectRandEnemyDiffRow(sBattleScriptExecutionContext* ctx, u8 entity);
void monsterBattleOpcode_selectNextReadyPC(sBattleScriptExecutionContext* ctx, u8 entity);
void monsterBattleOpcode_selectNextReadyEnemy(sBattleScriptExecutionContext* ctx, u8 entity);
void monsterBattleOpcode_selectLowestHpPC(sBattleScriptExecutionContext* ctx, u8 entity);
void monsterBattleOpcode_selectLowestHpEnemy(sBattleScriptExecutionContext* ctx, u8 entity);
void monsterBattleOpcode_selectRandGearPCSameRow(sBattleScriptExecutionContext* ctx, u8 entity);
void monsterBattleOpcode_selectRandomGearPC(sBattleScriptExecutionContext* ctx, u8 entity);
void monsterBattleOpcode_selectRandomGearEnemy(sBattleScriptExecutionContext* ctx, u8 entity);
void monsterBattleOpcode_countPCsInRow(sBattleScriptExecutionContext* ctx, u8 entity);
void monsterBattleOpcode_countEnemiesInRow(sBattleScriptExecutionContext* ctx, u8 entity);
void monsterBattleOpcode_getRowTableValue(sBattleScriptExecutionContext* ctx, u8 entity);
void monsterBattleOpcode_countPCsSameRow(sBattleScriptExecutionContext* ctx, u8 entity);
void monsterBattleOpcode_countEnemiesSameRow(sBattleScriptExecutionContext* ctx, u8 entity);
void monsterBattleOpcode_getItemCount(sBattleScriptExecutionContext* ctx, u8 entity);
void monsterBattleOpcode_copyVarToContext(sBattleScriptExecutionContext* ctx, sBattleScriptContext* contexts, u8 entity, u8 index);
void monsterBattleOpcode_loadGold(sBattleScriptExecutionContext* ctx, u8 entity);
void monsterBattleOpcode_selectRandByLoadout(sBattleScriptExecutionContext* ctx, u8 entity);
void monsterBattleOpcode_loadSelfHitResult(sBattleScriptExecutionContext* ctx, u8 entity);
void monsterBattleOpcode_selectRandomHiddenEnemy(sBattleScriptExecutionContext* ctx, u8 entity);
void monsterBattleOpcode_selectRandomKOPC(sBattleScriptExecutionContext* ctx, u8 entity);
void monsterBattleOpcode_countAlivePCs(sBattleScriptExecutionContext* ctx, u8 entity);
void monsterBattleOpcode_countAliveEnemies(sBattleScriptExecutionContext* ctx, u8 entity);
void monsterBattleOpcode_selectLowestHpGearPC(sBattleScriptExecutionContext* ctx, u8 entity);
void monsterBattleOpcode_selectLowestHpGearEnemy(sBattleScriptExecutionContext* ctx, u8 entity);
void monsterBattleOpcode_selectRandPCByStat(sBattleScriptExecutionContext* ctx, u8 entity);
void monsterBattleOpcode_selectRandEnemyByStat(sBattleScriptExecutionContext* ctx, u8 entity);
void monsterBattleOpcode_selectRandGearPCByStat(sBattleScriptExecutionContext* ctx, u8 entity);
void monsterBattleOpcode_selectRandGearEnemyByStat(sBattleScriptExecutionContext* ctx, u8 entity);
void monsterBattleOpcode_selectAnyPCByStat(sBattleScriptExecutionContext* ctx, u8 entity);
void monsterBattleOpcode_selectAnyGearPCByStat(sBattleScriptExecutionContext* ctx, u8 entity);
void monsterBattleOpcode_setSelfHidden(sBattleScriptExecutionContext* ctx, u8 entity);
void monsterBattleOpcode_storeSelfBitmask(sBattleScriptExecutionContext* ctx, u8 entity);
void monsterBattleOpcode_getAlivePCsMask(sBattleScriptExecutionContext* ctx, u8 entity);
void monsterBattleOpcode_getAliveEnemiesMask(sBattleScriptExecutionContext* ctx, u8 entity);
void monsterBattleOpcode_getPCsSameRowMask(sBattleScriptExecutionContext* ctx, u8 entity);
void monsterBattleOpcode_getEnemiesSameRowMask(sBattleScriptExecutionContext* ctx, u8 entity);
void monsterBattleOpcode_resetSelfHitResult(u8 entity);
void monsterBattleOpcode_addDwordVars(sBattleScriptExecutionContext* ctx, u8 entity);
void monsterBattleOpcode_subDwordVars(sBattleScriptExecutionContext* ctx, u8 entity);
void monsterBattleOpcode_mulDwordVar(sBattleScriptExecutionContext* ctx, u8 entity);
void monsterBattleOpcode_divDwordVar(sBattleScriptExecutionContext* ctx, u8 entity);
void monsterBattleOpcode_setBattleEventVar(sBattleScriptExecutionContext* ctx);
void monsterBattleOpcode_setCommandEnabled(sBattleScriptExecutionContext* ctx, u8 entity);
void monsterBattleOpcode_setRowTableValue(sBattleScriptExecutionContext* ctx);
void monsterBattleOpcode_forceNextTurn(sBattleScriptExecutionContext* ctx, u8 entity);
void monsterBattleOpcode_resetSlotStatus(void);
s32 executeMonsterScriptUpper_81(sBattleScriptExecutionContext* ctx, u8 entity);
s32 executeMonsterScriptUpper_82(sBattleScriptExecutionContext* ctx, u8 entity);
s32 monsterBattleOpcode_ifByteVarLE(sBattleScriptExecutionContext* ctx, u8 entity);
s32 monsterBattleOpcode_ifWordVarLE(sBattleScriptExecutionContext* ctx, u8 entity);
s32 monsterBattleOpcode_ifByteVarGE(sBattleScriptExecutionContext* ctx, u8 entity);
s32 monsterBattleOpcode_ifWordVarGE(sBattleScriptExecutionContext* ctx, u8 entity);
s32 monsterBattleOpcode_ifByteVarsEqual(sBattleScriptExecutionContext* ctx, u8 entity);
s32 monsterBattleOpcode_ifWordVarsEqual(sBattleScriptExecutionContext* ctx, u8 entity);
s32 monsterBattleOpcode_ifByteVarsLE(sBattleScriptExecutionContext* ctx, u8 entity);
s32 monsterBattleOpcode_ifWordVarsLE(sBattleScriptExecutionContext* ctx, u8 entity);
s32 monsterBattleOpcode_ifByteVarAnd(sBattleScriptExecutionContext* ctx, u8 entity);
s32 monsterBattleOpcode_ifWordVarAnd(sBattleScriptExecutionContext* ctx, u8 entity);
s32 monsterBattleOpcode_ifByteVarsAnd(sBattleScriptExecutionContext* ctx, u8 entity);
s32 monsterBattleOpcode_ifWordVarsAnd(sBattleScriptExecutionContext* ctx, u8 entity);
s32 monsterBattleOpcode_ifByteVarNotEqual(sBattleScriptExecutionContext* ctx, u8 entity);
s32 monsterBattleOpcode_90(sBattleScriptExecutionContext* ctx, u8 entity);
s32 monsterBattleOpcode_ifByteVarsNotEqual(sBattleScriptExecutionContext* ctx, u8 entity);
s32 monsterBattleOpcode_ifWordVarsNotEqual(sBattleScriptExecutionContext* ctx, u8 entity);
s32 monsterBattleOpcode_ifDwordVarsEqual(sBattleScriptExecutionContext* ctx, u8 entity);
s32 monsterBattleOpcode_ifDwordVarsLE(sBattleScriptExecutionContext* ctx, u8 entity);
s32 monsterBattleOpcode_ifEntityKO(sBattleScriptExecutionContext* ctx);
s32 monsterBattleOpcode_ifSlotLayoutEmpty(sBattleScriptExecutionContext* ctx);
s32 monsterBattleOpcode_ifFirstTwoPCsDown(void);
s32 monsterBattleOpcode_ifAllEnemiesHidden(void);
s32 monsterBattleOpcode_ifSelfHidden(u8 entity);
u8 executeMonsterScriptLower(sBattleScriptExecutionContext* ctx, u8 entity, u8 index);
u8 executeMonsterScriptUpper(sBattleScriptExecutionContext* ctx, u8 entity);
void freeBillyBattleStructs(u8 entity);
void freeBattleVar1_AE(void);
void drawCircleMenuChi_loadData(u8 entity);
void freeBattleVar1_96(void);
void initBattleRenderStruct2(void);
void setupGearTurnHud(u8 entity);
void freeBattleRenderStruct2(u8 entity);
void setupTurnRenderLoop(u8 entity);

void finishMonsterTurn(u8 entity) {
    if (apConfigArray[entity].m1_gearState != 0) {
        battleCurrentDamages[battleVar2->m2DA_indexInBattleVar48].m23_battleEntityIndex = entity;
        battleCurrentDamages[battleVar2->m2DA_indexInBattleVar48].m47_battleAnimationToPlay = 0x1B;
        battleVar2->m2DA_indexInBattleVar48++;
    }
    battleCurrentDamages[battleVar2->m2DA_indexInBattleVar48].m23_battleEntityIndex = entity;
    battleCurrentDamages[battleVar2->m2DA_indexInBattleVar48].m47_battleAnimationToPlay = 0xFE;
    startCharacterJumpToEnemyVar0 = 0;
}

void monsterJumpToTarget(u8 entity) {
    battleVar2->m2DA_indexInBattleVar48 = 0;
    battleTickMainSub0();
    performAttackSub2();
    battleCurrentDamages[battleVar2->m2DA_indexInBattleVar48].m23_battleEntityIndex = entity;
    battleRenderDebugAndMain();
    startJumpAnimation(1, entity, 0, computeFacingForJump(entity));
    executeBattleCode(entity);
    finishMonsterTurn(entity);
    while (battleVar2->m2DB_turnEnded == 0) {
        battleRenderDebugAndMain();
    }
}

void updateMonsterScriptEntitiesVarByAtttack(u8 attacker, u8 target) {
    s32 i;
    if (target >= 3) {
        u8 m = target - 3;
        monstersScriptsEntities[m].m20_s16Vars[7] = characterIdToTargetBitmask(attacker);
        for (i = 0; i < 5; i++) {
            monstersScriptsEntities[m].m30_s8Vars[9 + i] = D_800D2CA4[i];
        }
        if (performAttackSub3_var0.m0_performAttackSub3_var0 & characterIdToTargetBitmask(battleVar2->m0_entityMenus[attacker].m3C_currentTarget)) {
            monstersScriptsEntities[m].m30_s8Vars[14] = 1;
        } else {
            monstersScriptsEntities[m].m30_s8Vars[14] = 0;
        }
    }
}

void advanceBattleScript(sBattleScriptExecutionContext* ctx) {
    ctx->m0_scriptPtr += 4;
}

void skipScriptIfFalse(sBattleScriptExecutionContext* ctx) {
    while (*ctx->m0_scriptPtr >= 0x80) {
        advanceBattleScript(ctx);
    }
    while ((u8)(*ctx->m0_scriptPtr - 0x80) >= 0x70) {
        advanceBattleScript(ctx);
    }
}

void executeMonsterScript(u8 entity, s32 unused) { /* Noah: (char, int); the caller passes a status bit */
    u8 index = 0;
    sBattleScriptExecutionContext ctx;
    u8 m = entity - 3;
    u8* p;
    s32 i;

    ctx.m0_scriptPtr = monstersScriptsEntities[m].m0_scripts[0];
    p = (u8*)battleScriptContext;
    do {
        *p++ = 0;
    } while (p < (u8*)battleScriptContext + sizeof(battleScriptContext));
    for (i = 0; i < 32; i++) {
        battleCurrentDamages[i].m47_battleAnimationToPlay = 0xFF;
    }
    while (*ctx.m0_scriptPtr != 0xFD && *ctx.m0_scriptPtr != 0xFF) {
        if (*ctx.m0_scriptPtr >= 0x80) {
            if (executeMonsterScriptUpper(&ctx, m) == 0) {
                skipScriptIfFalse(&ctx);
            }
        } else {
            index = executeMonsterScriptLower(&ctx, m, index);
        }
    }
}

s32 executeMonsterScriptWhenAttacked(u8 entity) {
    s32 result = 0;
    sBattleScriptExecutionContext ctx;
    u8 m = entity - 3;
    u8* p;
    u8 index = 0;

    if (!(battleEntities.m0_entities[m + 3].m0_base.m7C_statusFlags[0] & 0x8000) || (battleEntities.m0_entities[m + 3].m0_base.m34_monsterFlags & 0x800)) {
        battleScriptContext[0].m0_attackType = 0;
        if (unknownMonsterStatus0[m].m0_hasWhenAttackedScript != 0) {
            ctx.m0_scriptPtr = monstersScriptsEntities[m].m0_scripts[2];
        p = (u8*)battleScriptContext;
        do {
            *p++ = 0;
        } while (p < (u8*)battleScriptContext + sizeof(battleScriptContext));
            while (*ctx.m0_scriptPtr != 0xFD && *ctx.m0_scriptPtr != 0xFF) {
                if (*ctx.m0_scriptPtr >= 0x80) {
                    if (executeMonsterScriptUpper(&ctx, m) == 0) {
                        skipScriptIfFalse(&ctx);
                    }
                } else {
                    if (*ctx.m0_scriptPtr == 0x62) {
                        result = 1;
                    }
                    index = executeMonsterScriptLower(&ctx, m, index);
                }
            }
        }
        if (battleScriptContext[0].m0_attackType != 0) {
            executeBattleCode(m + 3);
        }
    }
    return result;
}

void execMonsterScriptEndOfPlayerTurn(void) {
    sBattleScriptExecutionContext ctx;
    u8 savedTurn = battleVar2->m2D3_currentEntityTurn;
    s32 i;
    s32 j;
    u8* p;
    u8* end;
    u8 index = 0;

    for (i = 0; i < 8; i++) {
        if (unknownMonsterStatus0[i].m2_wasHit != 0 && unknownMonsterStatus0[i].m1_hasEndOfTurnScript != 0) {
            battleVar2->m2D3_currentEntityTurn = i + 3;
            ctx.m0_scriptPtr = monstersScriptsEntities[i].m0_scripts[3];
            p = (u8*)battleScriptContext;
            if (!(battleEntities.m0_entities[i + 3].m0_base.m7C_statusFlags[0] & 0x8000) || (battleEntities.m0_entities[i + 3].m0_base.m34_monsterFlags & 0x800)) {
                for (j = 0; j < 32; j++) {
                    battleCurrentDamages[j].m47_battleAnimationToPlay = 0xFF;
                }
                end = p + sizeof(battleScriptContext);
                do {
                    *p++ = 0;
                } while (p < end);
                while (*ctx.m0_scriptPtr != 0xFD && *ctx.m0_scriptPtr != 0xFF) {
                    if (*ctx.m0_scriptPtr >= 0x80) {
                        if (executeMonsterScriptUpper(&ctx, i) == 0) {
                            skipScriptIfFalse(&ctx);
                        }
                    } else {
                        index = executeMonsterScriptLower(&ctx, i, index);
                    }
                }
                battleVar2->m2DB_turnEnded = 0;
                monsterJumpToTarget(i + 3);
                updateBattleEventOverlay(0);
            }
        }
    }
    battleVar2->m2D3_currentEntityTurn = savedTurn;
}

void processBattleAnimation_subType_minus6(u8 entity) {
    battleVar1->mB0_isDialogWindowInitialized[4] = 1;
    previousEntityTurn.m8_nameLabels[entity].m5D_monsterNameIsDisplayed = 1;
}

void hideMonsterAttackName(u8 entity) {
    battleVar1->mB0_isDialogWindowInitialized[4] = 0;
    previousEntityTurn.m8_nameLabels[entity].m5D_monsterNameIsDisplayed = 0;
}

u8 monsterBattleOpcode_countEnemiesSameRow_findFirstInBitmask(u16 bitmask) {
    s32 i;
    for (i = 0; i < 11; i++) {
        if (bitmaskCharacterCheck(bitmask, i)) {
            break;
        }
    }
    return i;
}

u8 accessBattleEntityByteStat(u8 entity, u8 fieldId, u8 value, u8 read) {
    u8* p;
    u8 result;

    switch (fieldId) {
    case 0: p = &battleEntities.m0_entities[entity].m0_base.m4_weaponSlots[0].m0_weaponPower; break;
    case 1: p = &battleEntities.m0_entities[entity].m0_base.m56_battleCommandLoadout; break;
    case 2: p = &battleEntities.m0_entities[entity].mA4_gear.m3C_engineOutput; break;
    case 3: p = &battleEntities.m0_entities[entity].mA4_gear.m3F_attackMultiplier; break;
    case 4: p = &battleEntities.m0_entities[entity].m0_base.m58_attack; break;
    case 5: p = &battleEntities.m0_entities[entity].m0_base.m59_defence; break;
    case 6: p = &battleEntities.m0_entities[entity].m0_base.m5A_agility; break;
    case 7: p = &battleEntities.m0_entities[entity].m0_base.m2D_accessoryDefence; break;
    case 8: p = &battleEntities.m0_entities[entity].m0_base.m5D; break;
    case 9: p = &battleEntities.m0_entities[entity].m0_base.m5B_ether; break;
    case 10: p = &battleEntities.m0_entities[entity].m0_base.m5C_etherDefence; break;
    case 11: p = &battleEntities.m0_entities[entity].m0_base.m5E_hitPercentage; break;
    case 12: p = &battleEntities.m0_entities[entity].m0_base.m5F_evadePercentage; break;
    case 13: p = &battleEntities.m0_entities[entity].m0_base.m60_effectChance; break;
    case 14: p = &battleEntities.m0_entities[entity].m0_base.m61; break;
    case 15: p = &battleEntities.m0_entities[entity].m0_base.m64; break;
    case 16: p = &battleEntities.m0_entities[entity].m0_base.m65; break;
    case 17: p = &battleEntities.m0_entities[entity].m0_base.m66; break;
    case 18: p = &battleEntities.m0_entities[entity].m0_base.m67; break;
    case 19: p = &battleEntities.m0_entities[entity].mA4_gear.m98_agility; break;
    case 20: p = &battleEntities.m0_entities[entity].mA4_gear.m12_slots[0].m0_weaponPower; break;
    case 21: p = &battleEntities.m0_entities[entity].mA4_gear.m9C; break;
    case 22: p = &battleEntities.m0_entities[entity].mA4_gear.m9D; break;
    case 23: p = &battleEntities.m0_entities[entity].mA4_gear.m9E_etherAmp; break;
    }
    if (read == 0) {
        value = pcGameplayEnemyScriptValue(entity, 0, fieldId, value, 0);
        *p = value;
    } else {
        result = *p;
        result = pcGameplayEnemyScriptValue(entity, 0, fieldId, result, 1);
    }
    return result;
}

u16 accessBattleEntityWordStat(u8 entity, u8 fieldId, u16 value, u8 read) {
    u16* p;
    u16 result;

    switch (fieldId) {
    case 0: p = &battleEntities.m0_entities[entity].m0_base.m4E_MaxHP; break;
    case 1: p = &battleEntities.m0_entities[entity].m0_base.m4C_HP; break;
    case 2: p = &battleEntities.m0_entities[entity].m0_base.m7C_statusFlags[0]; break;
    case 3: p = &battleEntities.m0_entities[entity].m0_base.m7C_statusFlags[1]; break;
    case 4: p = &battleEntities.m0_entities[entity].m0_base.m7C_statusFlags[2]; break;
    case 5: p = &battleEntities.m0_entities[entity].m0_base.m7C_statusFlags[3]; break;
    case 6: p = &battleEntities.m0_entities[entity].m0_base.m84_statusFlags2[0]; break;
    case 7: p = &battleEntities.m0_entities[entity].m0_base.m84_statusFlags2[1]; break;
    case 8: p = &battleEntities.m0_entities[entity].m0_base.m84_statusFlags2[2]; break;
    case 9: p = &battleEntities.m0_entities[entity].m0_base.m84_statusFlags2[3]; break;
    case 10: p = &battleEntities.m0_entities[entity].m0_base.m84_statusFlags2[4]; break;
    case 11: p = &battleEntities.m0_entities[entity].m0_base.m84_statusFlags2[5]; break;
    case 12: p = &battleEntities.m0_entities[entity].mA4_gear.m6C; break;
    case 13: p = &battleEntities.m0_entities[entity].mA4_gear.m70_defence; break;
    case 14: p = &battleEntities.m0_entities[entity].mA4_gear.m72_etherDefence; break;
    case 15: p = &battleEntities.m0_entities[entity].mA4_gear.m7C_statusFlags; break;
    case 16: p = &battleEntities.m0_entities[entity].mA4_gear.m7E_statusFlags1; break;
    case 17: p = &battleEntities.m0_entities[entity].mA4_gear.m80_statusFlags2; break;
    case 18: p = &battleEntities.m0_entities[entity].mA4_gear.m82_statusFlags3; break;
    case 19: p = &battleEntities.m0_entities[entity].mA4_gear.m84_statusFlags4; break;
    case 20: p = &battleEntities.m0_entities[entity].mA4_gear.m86_statusFlags5; break;
    case 21: p = &battleEntities.m0_entities[entity].m0_base.m34_monsterFlags; break;
    case 22: p = &battleEntities.m0_entities[entity].m0_base.m36_monsterFlags2; break;
    case 23: p = &battleEntities.m0_entities[entity].m0_base.m38_weaknessFlags; break;
    }
    if (read == 0) {
        value = pcGameplayEnemyScriptValue(entity, 1, fieldId, value, 0);
        *p = value;
    } else {
        result = *p;
        result = pcGameplayEnemyScriptValue(entity, 1, fieldId, result, 1);
    }
    return result;
}

u8 isMonsterTargetValid(u8 entity, u8 allowHidden) {
    u8 result;

    result = 0;
    if (isBattleSlotFilled.m0_isBattleSlotFilled[entity] != 0 && battleVisualEntities[entity].m3_hidden == 0) {
        if ((battleEntities.m0_entities[entity].m0_base.m7C_statusFlags[0] & 0xC002) == 0) {
            if (allowHidden != 0) {
                result = 1;
            } else {
                result = 0;
                if (!(battleEntities.m0_entities[entity].m0_base.m84_statusFlags2[0] & 0x20)) {
                    result = 1;
                }
            }
        }
    }
    return result;
}

u8 isEnemySlotTargetable(u8 entity, u8 allowHidden) {
    u8 result;

    result = 0;
    if (isBattleSlotFilled.m0_isBattleSlotFilled[entity] != 0) {
        if ((battleEntities.m0_entities[entity].m0_base.m7C_statusFlags[0] & 0xC002) == 0) {
            if (allowHidden != 0) {
                result = 1;
            } else {
                result = 0;
                if (!(battleEntities.m0_entities[entity].m0_base.m84_statusFlags2[0] & 0x20)) {
                    result = 1;
                }
            }
        }
    }
    return result;
}

u8 isBattleEntityAliveAndVisible(u8 entity) {
    u8 result = 0;
    if (isBattleSlotFilled.m0_isBattleSlotFilled[entity] != 0 && battleVisualEntities[entity].m3_hidden == 0) {
        result = (battleEntities.m0_entities[entity].m0_base.m7C_statusFlags[0] & 0xC000) == 0;
    }
    return result;
}

u8 monsterBattleOpcode_queueLanguageError(sBattleScriptExecutionContext* ctx, sBattleScriptContext* contexts, u8 entity, u8 index) {
    contexts[index].m0_attackType = 0x80;
    contexts[index].m1_operand = ctx->m0_scriptPtr[0];
    contexts[index].m2_animationId = ctx->m0_scriptPtr[1];
    contexts[index].m3_nameIndex = ctx->m0_scriptPtr[2];
    contexts[index].m4_valueLow = ctx->m0_scriptPtr[3];
    return index + 1;
}

u8 monsterBattleOpcode_writeContextByte(sBattleScriptExecutionContext* ctx, sBattleScriptContext* contexts, u8 index) {
    u8 result = index;
    ((u8*)contexts)[index * 8 + ctx->m0_scriptPtr[1]] = ctx->m0_scriptPtr[2];
    if (ctx->m0_scriptPtr[1] == 0) {
        result = index + 1;
    }
    return result;
}

void monsterBattleOpcode_writeContextByteVar(sBattleScriptExecutionContext* ctx, sBattleScriptContext* contexts, u8 entity, u8 index) {
    ((u8*)contexts)[index * 8 + ctx->m0_scriptPtr[1]] = monstersScriptsEntities[entity].m30_s8Vars[ctx->m0_scriptPtr[2]];
}

void monsterBattleOpcode_copyContextEntry(sBattleScriptExecutionContext* ctx, sBattleScriptContext* contexts) {
    s32 i;
    for (i = 0; i < 8; i++) {
        ((u8*)contexts)[ctx->m0_scriptPtr[2] * 8 + i] = ((u8*)contexts)[ctx->m0_scriptPtr[1] * 8 + i];
    }
}

void monsterBattleOpcode_setByteVar(sBattleScriptExecutionContext* ctx, u8 entity) {
    monstersScriptsEntities[entity].m30_s8Vars[ctx->m0_scriptPtr[1]] = ctx->m0_scriptPtr[2];
}

void monsterBattleOpcode_setWordVar(sBattleScriptExecutionContext* ctx, u8 entity) {
    u16 v = ctx->m0_scriptPtr[2] | (ctx->m0_scriptPtr[3] << 8);
    monstersScriptsEntities[entity].m20_s16Vars[ctx->m0_scriptPtr[1]] = v;
}

void monsterBattleOpcode_setDwordVar(sBattleScriptExecutionContext* ctx, u8 entity) {
    s32 v = ((ctx->m0_scriptPtr[3] << 8) + ctx->m0_scriptPtr[2]) << 4;
    monstersScriptsEntities[entity].m10_s32Vars[ctx->m0_scriptPtr[1]] = v;
}

void monsterBattleOpcode_setGameStateVar(sBattleScriptExecutionContext* ctx) {
    battleScriptGameStateVars[ctx->m0_scriptPtr[1]] = ctx->m0_scriptPtr[2];
}

void monsterBattleOpcode_addByteVar(sBattleScriptExecutionContext* ctx, u8 entity) {
    u8* p = &monstersScriptsEntities[entity].m30_s8Vars[ctx->m0_scriptPtr[1]];
    s16 v = *p + ctx->m0_scriptPtr[2];
    if (v > 0xFF) {
        v = 0xFF;
    }
    *p = v;
}

void monsterBattleOpcode_subByteVar(sBattleScriptExecutionContext* ctx, u8 entity) {
    u8* p = &monstersScriptsEntities[entity].m30_s8Vars[ctx->m0_scriptPtr[1]];
    s16 v = *p - ctx->m0_scriptPtr[2];
    if (v < 0) {
        v = 0;
    }
    *p = v;
}

void monsterBattleOpcode_mulByteVar(sBattleScriptExecutionContext* ctx, u8 entity) {
    u8* p = &monstersScriptsEntities[entity].m30_s8Vars[ctx->m0_scriptPtr[1]];
    s16 v = *p * ctx->m0_scriptPtr[2];
    if (v >= 0x100) {
        v = 0xFF;
    }
    *p = v;
}

void monsterBattleOpcode_divByteVar(sBattleScriptExecutionContext* ctx, u8 entity) {
    u8* p = &monstersScriptsEntities[entity].m30_s8Vars[ctx->m0_scriptPtr[1]];
    *p = R3000_DIV(*p, ctx->m0_scriptPtr[2]);
}

void monsterBattleOpcode_moduloByteVar(sBattleScriptExecutionContext* ctx, u8 entity) {
    u8* p = &monstersScriptsEntities[entity].m30_s8Vars[ctx->m0_scriptPtr[1]];
    *p = R3000_MOD(*p, ctx->m0_scriptPtr[2]);
}

void monsterBattleOpcode_andByteVar(sBattleScriptExecutionContext* ctx, u8 entity) {
    monstersScriptsEntities[entity].m30_s8Vars[ctx->m0_scriptPtr[1]] &= ctx->m0_scriptPtr[2];
}

void monsterBattleOpcode_orByteVar(sBattleScriptExecutionContext* ctx, u8 entity) {
    monstersScriptsEntities[entity].m30_s8Vars[ctx->m0_scriptPtr[1]] |= ctx->m0_scriptPtr[2];
}

void monsterBattleOpcode_xorByteVar(sBattleScriptExecutionContext* ctx, u8 entity) {
    monstersScriptsEntities[entity].m30_s8Vars[ctx->m0_scriptPtr[1]] ^= ctx->m0_scriptPtr[2];
}

void monsterBattleOpcode_addWordVar(sBattleScriptExecutionContext* ctx, u8 entity) {
    s32 v = monstersScriptsEntities[entity].m20_s16Vars[ctx->m0_scriptPtr[1]] + ((ctx->m0_scriptPtr[3] << 8) + ctx->m0_scriptPtr[2]);
    if (v > 0xFFFF) {
        v = 0xFFFF;
    }
    monstersScriptsEntities[entity].m20_s16Vars[ctx->m0_scriptPtr[1]] = v;
}

void monsterBattleOpcode_subWordVar(sBattleScriptExecutionContext* ctx, u8 entity) {
    s32 v = monstersScriptsEntities[entity].m20_s16Vars[ctx->m0_scriptPtr[1]] - ((ctx->m0_scriptPtr[3] << 8) + ctx->m0_scriptPtr[2]);
    if (v < 0) {
        v = 0;
    }
    monstersScriptsEntities[entity].m20_s16Vars[ctx->m0_scriptPtr[1]] = v;
}

void monsterBattleOpcode_mulWordVar(sBattleScriptExecutionContext* ctx, u8 entity) {
    s32 v = monstersScriptsEntities[entity].m20_s16Vars[ctx->m0_scriptPtr[1]] * ((ctx->m0_scriptPtr[3] << 8) + ctx->m0_scriptPtr[2]);
    if (v > 0xFFFF) {
        v = 0xFFFF;
    }
    monstersScriptsEntities[entity].m20_s16Vars[ctx->m0_scriptPtr[1]] = v;
}

void monsterBattleOpcode_divWordVar(sBattleScriptExecutionContext* ctx, u8 entity) {
    monstersScriptsEntities[entity].m20_s16Vars[ctx->m0_scriptPtr[1]] =
        R3000_DIV(monstersScriptsEntities[entity].m20_s16Vars[ctx->m0_scriptPtr[1]], (ctx->m0_scriptPtr[3] << 8) + ctx->m0_scriptPtr[2]);
}

void monsterBattleOpcode_moduloWordVar(sBattleScriptExecutionContext* ctx, u8 entity) {
    monstersScriptsEntities[entity].m20_s16Vars[ctx->m0_scriptPtr[1]] =
        R3000_MOD(monstersScriptsEntities[entity].m20_s16Vars[ctx->m0_scriptPtr[1]], (ctx->m0_scriptPtr[3] << 8) + ctx->m0_scriptPtr[2]);
}

void monsterBattleOpcode_andWordVar(sBattleScriptExecutionContext* ctx, u8 entity) {
    monstersScriptsEntities[entity].m20_s16Vars[ctx->m0_scriptPtr[1]] &= ctx->m0_scriptPtr[2] + (ctx->m0_scriptPtr[3] << 8);
}

void monsterBattleOpcode_orWordVar(sBattleScriptExecutionContext* ctx, u8 entity) {
    monstersScriptsEntities[entity].m20_s16Vars[ctx->m0_scriptPtr[1]] |= ctx->m0_scriptPtr[2] + (ctx->m0_scriptPtr[3] << 8);
}

void monsterBattleOpcode_xorWordVar(sBattleScriptExecutionContext* ctx, u8 entity) {
    monstersScriptsEntities[entity].m20_s16Vars[ctx->m0_scriptPtr[1]] ^= ctx->m0_scriptPtr[2] + (ctx->m0_scriptPtr[3] << 8);
}

void monsterBattleOpcode_addByteVars(sBattleScriptExecutionContext* ctx, u8 entity) {
    s16 v = monstersScriptsEntities[entity].m30_s8Vars[ctx->m0_scriptPtr[1]] + monstersScriptsEntities[entity].m30_s8Vars[ctx->m0_scriptPtr[2]];
    if (v > 0xFF) {
        v = 0xFF;
    }
    monstersScriptsEntities[entity].m30_s8Vars[ctx->m0_scriptPtr[3]] = v;
}

void monsterBattleOpcode_subByteVars(sBattleScriptExecutionContext* ctx, u8 entity) {
    s16 v = monstersScriptsEntities[entity].m30_s8Vars[ctx->m0_scriptPtr[1]] - monstersScriptsEntities[entity].m30_s8Vars[ctx->m0_scriptPtr[2]];
    if (v < 0) {
        v = 0;
    }
    monstersScriptsEntities[entity].m30_s8Vars[ctx->m0_scriptPtr[3]] = v;
}

void monsterBattleOpcode_mulByteVars(sBattleScriptExecutionContext* ctx, u8 entity) {
    s16 v = monstersScriptsEntities[entity].m30_s8Vars[ctx->m0_scriptPtr[1]] * monstersScriptsEntities[entity].m30_s8Vars[ctx->m0_scriptPtr[2]];
    if (v > 0xFF) {
        v = 0xFF;
    }
    monstersScriptsEntities[entity].m30_s8Vars[ctx->m0_scriptPtr[3]] = v;
}

void monsterBattleOpcode_divByteVars(sBattleScriptExecutionContext* ctx, u8 entity) {
    monstersScriptsEntities[entity].m30_s8Vars[ctx->m0_scriptPtr[3]] = R3000_DIV(monstersScriptsEntities[entity].m30_s8Vars[ctx->m0_scriptPtr[1]], monstersScriptsEntities[entity].m30_s8Vars[ctx->m0_scriptPtr[2]]);
}

void monsterBattleOpcode_moduloByteVars(sBattleScriptExecutionContext* ctx, u8 entity) {
    monstersScriptsEntities[entity].m30_s8Vars[ctx->m0_scriptPtr[3]] = R3000_MOD(monstersScriptsEntities[entity].m30_s8Vars[ctx->m0_scriptPtr[1]], monstersScriptsEntities[entity].m30_s8Vars[ctx->m0_scriptPtr[2]]);
}

void monsterBattleOpcode_andByteVars(sBattleScriptExecutionContext* ctx, u8 entity) {
    monstersScriptsEntities[entity].m30_s8Vars[ctx->m0_scriptPtr[3]] = monstersScriptsEntities[entity].m30_s8Vars[ctx->m0_scriptPtr[1]] & monstersScriptsEntities[entity].m30_s8Vars[ctx->m0_scriptPtr[2]];
}

void monsterBattleOpcode_orByteVars(sBattleScriptExecutionContext* ctx, u8 entity) {
    monstersScriptsEntities[entity].m30_s8Vars[ctx->m0_scriptPtr[3]] = monstersScriptsEntities[entity].m30_s8Vars[ctx->m0_scriptPtr[1]] | monstersScriptsEntities[entity].m30_s8Vars[ctx->m0_scriptPtr[2]];
}

void monsterBattleOpcode_xorByteVars(sBattleScriptExecutionContext* ctx, u8 entity) {
    monstersScriptsEntities[entity].m30_s8Vars[ctx->m0_scriptPtr[3]] = monstersScriptsEntities[entity].m30_s8Vars[ctx->m0_scriptPtr[1]] ^ monstersScriptsEntities[entity].m30_s8Vars[ctx->m0_scriptPtr[2]];
}

void monsterBattleOpcode_addWordVars(sBattleScriptExecutionContext* ctx, u8 entity) {
    s32 v = monstersScriptsEntities[entity].m20_s16Vars[ctx->m0_scriptPtr[1]] + monstersScriptsEntities[entity].m20_s16Vars[ctx->m0_scriptPtr[2]];
    if (v > 0xFFFF) {
        v = 0xFFFF;
    }
    monstersScriptsEntities[entity].m20_s16Vars[ctx->m0_scriptPtr[3]] = v;
}

void monsterBattleOpcode_subWordVars(sBattleScriptExecutionContext* ctx, u8 entity) {
    s32 v = monstersScriptsEntities[entity].m20_s16Vars[ctx->m0_scriptPtr[1]] - monstersScriptsEntities[entity].m20_s16Vars[ctx->m0_scriptPtr[2]];
    if (v < 0) {
        v = 0;
    }
    monstersScriptsEntities[entity].m20_s16Vars[ctx->m0_scriptPtr[3]] = v;
}

void monsterBattleOpcode_mulWordVars(sBattleScriptExecutionContext* ctx, u8 entity) {
    s32 v = monstersScriptsEntities[entity].m20_s16Vars[ctx->m0_scriptPtr[1]] * monstersScriptsEntities[entity].m20_s16Vars[ctx->m0_scriptPtr[2]];
    if (v > 0xFFFF) {
        v = 0xFFFF;
    }
    monstersScriptsEntities[entity].m20_s16Vars[ctx->m0_scriptPtr[3]] = v;
}

void monsterBattleOpcode_divWordVars(sBattleScriptExecutionContext* ctx, u8 entity) {
    monstersScriptsEntities[entity].m20_s16Vars[ctx->m0_scriptPtr[3]] = R3000_DIV(monstersScriptsEntities[entity].m20_s16Vars[ctx->m0_scriptPtr[1]], monstersScriptsEntities[entity].m20_s16Vars[ctx->m0_scriptPtr[2]]);
}

void monsterBattleOpcode_moduloWordVars(sBattleScriptExecutionContext* ctx, u8 entity) {
    monstersScriptsEntities[entity].m20_s16Vars[ctx->m0_scriptPtr[3]] = R3000_MOD(monstersScriptsEntities[entity].m20_s16Vars[ctx->m0_scriptPtr[1]], monstersScriptsEntities[entity].m20_s16Vars[ctx->m0_scriptPtr[2]]);
}

void monsterBattleOpcode_andWordVars(sBattleScriptExecutionContext* ctx, u8 entity) {
    monstersScriptsEntities[entity].m20_s16Vars[ctx->m0_scriptPtr[3]] = monstersScriptsEntities[entity].m20_s16Vars[ctx->m0_scriptPtr[1]] & monstersScriptsEntities[entity].m20_s16Vars[ctx->m0_scriptPtr[2]];
}

void monsterBattleOpcode_orWordVars(sBattleScriptExecutionContext* ctx, u8 entity) {
    monstersScriptsEntities[entity].m20_s16Vars[ctx->m0_scriptPtr[3]] = monstersScriptsEntities[entity].m20_s16Vars[ctx->m0_scriptPtr[1]] | monstersScriptsEntities[entity].m20_s16Vars[ctx->m0_scriptPtr[2]];
}

void monsterBattleOpcode_xorWordVars(sBattleScriptExecutionContext* ctx, u8 entity) {
    monstersScriptsEntities[entity].m20_s16Vars[ctx->m0_scriptPtr[3]] = monstersScriptsEntities[entity].m20_s16Vars[ctx->m0_scriptPtr[1]] ^ monstersScriptsEntities[entity].m20_s16Vars[ctx->m0_scriptPtr[2]];
}

void monsterBattleOpcode_setSelfByteStat(sBattleScriptExecutionContext* ctx, u8 entity) {
    accessBattleEntityByteStat(entity + 3, ctx->m0_scriptPtr[1], ctx->m0_scriptPtr[2], 0);
}

void monsterBattleOpcode_setSelfWordStat(sBattleScriptExecutionContext* ctx, u8 entity) {
    accessBattleEntityWordStat(entity + 3, ctx->m0_scriptPtr[1], ctx->m0_scriptPtr[2] | (ctx->m0_scriptPtr[3] << 8), 0);
}

void monsterBattleOpcode_getTargetByteStat(sBattleScriptExecutionContext* ctx, u8 entity) {
    monstersScriptsEntities[entity].m30_s8Vars[ctx->m0_scriptPtr[1]] =
        accessBattleEntityByteStat(monsterBattleOpcode_countEnemiesSameRow_findFirstInBitmask(monstersScriptsEntities[entity].m20_s16Vars[ctx->m0_scriptPtr[3]]), ctx->m0_scriptPtr[2], 0, 1);
}

u8 monsterBattleOpcode_setTargetByteStat(sBattleScriptExecutionContext* ctx, u8 entity, u8 index) {
    u8 result = index;
    u8 target = monsterBattleOpcode_countEnemiesSameRow_findFirstInBitmask(monstersScriptsEntities[entity].m20_s16Vars[ctx->m0_scriptPtr[3]]);
    if (target < 3) {
        battleScriptContext[0].m0_attackType = 0x20;
        result = index + 1;
    } else {
        accessBattleEntityByteStat(target, ctx->m0_scriptPtr[2], monstersScriptsEntities[entity].m30_s8Vars[ctx->m0_scriptPtr[1]], 0);
    }
    return result;
}

void monsterBattleOpcode_getTargetWordStat(sBattleScriptExecutionContext* ctx, u8 entity) {
    monstersScriptsEntities[entity].m20_s16Vars[ctx->m0_scriptPtr[1]] =
        accessBattleEntityWordStat(monsterBattleOpcode_countEnemiesSameRow_findFirstInBitmask(monstersScriptsEntities[entity].m20_s16Vars[ctx->m0_scriptPtr[3]]), ctx->m0_scriptPtr[2], 0, 1);
}

u8 monsterBattleOpcode_setTargetWordStat(sBattleScriptExecutionContext* ctx, u8 entity, u8 index) {
    u8 result = index;
    u8 target = monsterBattleOpcode_countEnemiesSameRow_findFirstInBitmask(monstersScriptsEntities[entity].m20_s16Vars[ctx->m0_scriptPtr[3]]);
    if (target < 3) {
        battleScriptContext[0].m0_attackType = 0x20;
        result = index + 1;
    } else {
        accessBattleEntityWordStat(target, ctx->m0_scriptPtr[2], monstersScriptsEntities[entity].m20_s16Vars[ctx->m0_scriptPtr[1]], 0);
    }
    return result;
}

void monsterBattleOpcode_loadGearHpOrGold(sBattleScriptExecutionContext* ctx, u8 entity) {
    switch (ctx->m0_scriptPtr[2]) {
    case 1:
        {
            u8 target = monsterBattleOpcode_countEnemiesSameRow_findFirstInBitmask(monstersScriptsEntities[entity].m20_s16Vars[ctx->m0_scriptPtr[3]]);
            monstersScriptsEntities[entity].m10_s32Vars[ctx->m0_scriptPtr[1]] =
                pcGameplayEnemyScriptValue(target, 2, 1, battleEntities.m0_entities[target].mA4_gear.m60_hp, 1);
        }
        break;
    case 2:
        monstersScriptsEntities[entity].m10_s32Vars[ctx->m0_scriptPtr[1]] = gameState.m1924_gold;
        break;
    }
}

u8 monsterBattleOpcode_setTargetGearHp(sBattleScriptExecutionContext* ctx, u8 entity, u8 index) {
    u8 result = index;
    u8 target = monsterBattleOpcode_countEnemiesSameRow_findFirstInBitmask(monstersScriptsEntities[entity].m20_s16Vars[ctx->m0_scriptPtr[3]]);
    if (target < 3) {
        battleScriptContext[0].m0_attackType = 0x20;
        result = index + 1;
    } else if (ctx->m0_scriptPtr[2] == 0) {
        battleEntities.m0_entities[target].mA4_gear.m64_maxHp = pcGameplayEnemyScriptValue(
            target, 2, 0, monstersScriptsEntities[entity].m10_s32Vars[ctx->m0_scriptPtr[1]], 0);
    } else {
        battleEntities.m0_entities[target].mA4_gear.m60_hp = pcGameplayEnemyScriptValue(
            target, 2, 1, monstersScriptsEntities[entity].m10_s32Vars[ctx->m0_scriptPtr[1]], 0);
    }
    return result;
}

void monsterBattleOpcode_loadGameStateVar(sBattleScriptExecutionContext* ctx, u8 entity) {
    monstersScriptsEntities[entity].m20_s16Vars[ctx->m0_scriptPtr[1]] = battleScriptGameStateVars[ctx->m0_scriptPtr[2]];
}

void monsterBattleOpcode_storeGameStateVar(sBattleScriptExecutionContext* ctx, u8 entity) {
    battleScriptGameStateVars[ctx->m0_scriptPtr[2]] = monstersScriptsEntities[entity].m20_s16Vars[ctx->m0_scriptPtr[1]];
}

void monsterBattleOpcode_copyByteVar(sBattleScriptExecutionContext* ctx, u8 entity) {
    monstersScriptsEntities[entity].m30_s8Vars[ctx->m0_scriptPtr[2]] = monstersScriptsEntities[entity].m30_s8Vars[ctx->m0_scriptPtr[1]];
}

void monsterBattleOpcode_copyWordVar(sBattleScriptExecutionContext* ctx, u8 entity) {
    monstersScriptsEntities[entity].m20_s16Vars[ctx->m0_scriptPtr[2]] = monstersScriptsEntities[entity].m20_s16Vars[ctx->m0_scriptPtr[1]];
}

void monsterBattleOpcode_copyDwordVar(sBattleScriptExecutionContext* ctx, u8 entity) {
    monstersScriptsEntities[entity].m10_s32Vars[ctx->m0_scriptPtr[2]] = monstersScriptsEntities[entity].m10_s32Vars[ctx->m0_scriptPtr[1]];
}

void monsterBattleOpcode_byteVarToWordVar(sBattleScriptExecutionContext* ctx, u8 entity) {
    monstersScriptsEntities[entity].m20_s16Vars[ctx->m0_scriptPtr[2]] = monstersScriptsEntities[entity].m30_s8Vars[ctx->m0_scriptPtr[1]];
}

void monsterBattleOpcode_wordVarToDwordVar(sBattleScriptExecutionContext* ctx, u8 entity) {
    monstersScriptsEntities[entity].m10_s32Vars[ctx->m0_scriptPtr[2]] = monstersScriptsEntities[entity].m20_s16Vars[ctx->m0_scriptPtr[1]];
}

void monsterBattleOpcode_clearByteVars(u8 entity) {
    s32 i;
    for (i = 0; i < 16; i++) {
        monstersScriptsEntities[entity].m30_s8Vars[i] = 0;
    }
}

void monsterBattleOpcode_clearWordVars(u8 entity) {
    s32 i;
    for (i = 0; i < 8; i++) {
        monstersScriptsEntities[entity].m20_s16Vars[i] = 0;
    }
}

void monsterBattleOpcode_setSelfExperience(sBattleScriptExecutionContext* ctx, u8 entity) {
    s32 hi = ctx->m0_scriptPtr[2] << 8;
    battleEntities.m0_entities[entity + 3].m14C_experience = ctx->m0_scriptPtr[1] | hi;
}

void monsterBattleOpcode_setSelfGold(sBattleScriptExecutionContext* ctx, u8 entity) {
    u16 v = ctx->m0_scriptPtr[1] | (ctx->m0_scriptPtr[2] << 8);
    battleEntities.m0_entities[entity + 3].m156_gold = v;
}

void monsterBattleOpcode_setSelfDrop2(sBattleScriptExecutionContext* ctx, u8 entity) {
    battleEntities.m0_entities[entity + 3].m155_dropCategory2 = ctx->m0_scriptPtr[1];
    battleEntities.m0_entities[entity + 3].m153_dropItem2 = ctx->m0_scriptPtr[2];
    battleEntities.m0_entities[entity + 3].m151_dropChance2 = ctx->m0_scriptPtr[3];
}

void monsterBattleOpcode_setSelfDrop1(sBattleScriptExecutionContext* ctx, u8 entity) {
    battleEntities.m0_entities[entity + 3].m154_dropCategory1 = ctx->m0_scriptPtr[1];
    battleEntities.m0_entities[entity + 3].m152_dropItem1 = ctx->m0_scriptPtr[2];
    battleEntities.m0_entities[entity + 3].m150_dropChance1 = ctx->m0_scriptPtr[3];
}

void monsterBattleOpcode_writeContextWord(sBattleScriptExecutionContext* ctx, sBattleScriptContext* contexts, u8 index) {
    ((u8*)contexts)[index * 8 + ctx->m0_scriptPtr[1]] = ctx->m0_scriptPtr[2];
    (&((u8*)contexts)[index * 8 + ctx->m0_scriptPtr[1]])[1] = ctx->m0_scriptPtr[3];
}

void monsterBattleOpcode_randomByteVar(sBattleScriptExecutionContext* ctx, u8 entity) {
    monstersScriptsEntities[entity].m30_s8Vars[ctx->m0_scriptPtr[1]] = randomInRange(0, ctx->m0_scriptPtr[2]);
}

void monsterBattleOpcode_randomWordVar(sBattleScriptExecutionContext* ctx, u8 entity) {
    monstersScriptsEntities[entity].m20_s16Vars[ctx->m0_scriptPtr[1]] = getRandomValueInRange(0, ctx->m0_scriptPtr[2] | (ctx->m0_scriptPtr[3] << 8));
}

void monsterBattleOpcode_selectRandomPC(sBattleScriptExecutionContext* ctx, u8 entity) {
    u8 tested[3];
    u8 target;

    tested[0] = tested[1] = tested[2] = 0;
    monstersScriptsEntities[entity].m20_s16Vars[ctx->m0_scriptPtr[1]] = 0;
    while (!(tested[0] & tested[1] & tested[2])) {
        target = randomInRange(0, 2);
        if (!tested[target]) {
            if (isMonsterTargetValid(target, ctx->m0_scriptPtr[2])) {
                if (apConfigArray[target].m1_gearState == 0) {
                    monstersScriptsEntities[entity].m20_s16Vars[ctx->m0_scriptPtr[1]] = characterIdToTargetBitmask(target);
                    return;
                }
            }
            tested[target] = 1;
        }
    }
}

void monsterBattleOpcode_selectRandomPCSameRow(sBattleScriptExecutionContext* ctx, u8 entity) {
    u8 tested[3];
    u8 target;

    tested[0] = tested[1] = tested[2] = 0;
    monstersScriptsEntities[entity].m20_s16Vars[ctx->m0_scriptPtr[1]] = 0;
    while (!(tested[0] & tested[1] & tested[2])) {
        target = randomInRange(0, 2);
        if (!tested[target]) {
            if (isMonsterTargetValid(target, ctx->m0_scriptPtr[2])) {
                if (battleVisualEntities[entity + 3].m0_positionSlot == battleVisualEntities[target].m0_positionSlot) {
                    if (apConfigArray[target].m1_gearState == 0) {
                        monstersScriptsEntities[entity].m20_s16Vars[ctx->m0_scriptPtr[1]] = characterIdToTargetBitmask(target);
                        return;
                    }
                }
            }
            tested[target] = 1;
        }
    }
}

void monsterBattleOpcode_selectRandEnemySameRow(sBattleScriptExecutionContext* ctx, u8 entity) {
    s32 i = 3;
    s32 count = 0;
    u8 candidates[8];
    monstersScriptsEntities[entity].m20_s16Vars[ctx->m0_scriptPtr[1]] = 0;
    for (; i < 11; i++) {
        if (isMonsterTargetValid(i, ctx->m0_scriptPtr[2])) {
            if (battleVisualEntities[entity + 3].m0_positionSlot == battleVisualEntities[i].m0_positionSlot) {
                if (apConfigArray[i].m1_gearState == 0) {
                    candidates[count] = i;
                    count++;
                }
            }
        }
    }
    if (count != 0) {
        monstersScriptsEntities[entity].m20_s16Vars[ctx->m0_scriptPtr[1]] = characterIdToTargetBitmask(candidates[(u8)randomInRange(0, (u8)(count - 1))]);
    }
}

void monsterBattleOpcode_selectRandomPCDiffRow(sBattleScriptExecutionContext* ctx, u8 entity) {
    u8 tested[3];
    u8 target;

    tested[0] = tested[1] = tested[2] = 0;
    monstersScriptsEntities[entity].m20_s16Vars[ctx->m0_scriptPtr[1]] = 0;
    while (!(tested[0] & tested[1] & tested[2])) {
        target = randomInRange(0, 2);
        if (!tested[target]) {
            if (isMonsterTargetValid(target, ctx->m0_scriptPtr[2])) {
                if (battleVisualEntities[entity + 3].m0_positionSlot != battleVisualEntities[target].m0_positionSlot) {
                    if (apConfigArray[target].m1_gearState == 0) {
                        monstersScriptsEntities[entity].m20_s16Vars[ctx->m0_scriptPtr[1]] = characterIdToTargetBitmask(target);
                        return;
                    }
                }
            }
            tested[target] = 1;
        }
    }
}

void monsterBattleOpcode_selectRandEnemyDiffRow(sBattleScriptExecutionContext* ctx, u8 entity) {
    s32 i = 3;
    s32 count = 0;
    u8 candidates[8];
    monstersScriptsEntities[entity].m20_s16Vars[ctx->m0_scriptPtr[1]] = 0;
    for (; i < 11; i++) {
        if (isMonsterTargetValid(i, ctx->m0_scriptPtr[2])) {
            if (battleVisualEntities[entity + 3].m0_positionSlot != battleVisualEntities[i].m0_positionSlot) {
                if (apConfigArray[i].m1_gearState == 0) {
                    candidates[count] = i;
                    count++;
                }
            }
        }
    }
    if (count != 0) {
        monstersScriptsEntities[entity].m20_s16Vars[ctx->m0_scriptPtr[1]] = characterIdToTargetBitmask(candidates[(u8)randomInRange(0, (u8)(count - 1))]);
    }
}

void monsterBattleOpcode_selectNextReadyPC(sBattleScriptExecutionContext* ctx, u8 entity) {
    s32 i;
    s32 best = 0xFF;
    s32 bestIndex = 0;
    for (i = 0; i < 3; i++) {
        if (isMonsterTargetValid(i, ctx->m0_scriptPtr[2])) {
            if (battleSlotStatusVar0.m16_numTicksBeforeReady[i] <= best) {
                best = (u8)battleSlotStatusVar0.m16_numTicksBeforeReady[i];
                bestIndex = i;
            }
        }
    }
    monstersScriptsEntities[entity].m20_s16Vars[ctx->m0_scriptPtr[1]] = characterIdToTargetBitmask(bestIndex);
}

void monsterBattleOpcode_selectNextReadyEnemy(sBattleScriptExecutionContext* ctx, u8 entity) {
    s32 i;
    s32 best = 0xFF;
    s32 bestIndex = 0;
    for (i = 3; i < 11; i++) {
        if (isMonsterTargetValid(i, ctx->m0_scriptPtr[2])) {
            if (battleSlotStatusVar0.m16_numTicksBeforeReady[i] <= best && i != entity + 3) {
                best = (u8)battleSlotStatusVar0.m16_numTicksBeforeReady[i];
                bestIndex = i;
            }
        }
    }
    monstersScriptsEntities[entity].m20_s16Vars[ctx->m0_scriptPtr[1]] = characterIdToTargetBitmask(bestIndex);
}

void monsterBattleOpcode_selectLowestHpPC(sBattleScriptExecutionContext* ctx, u8 entity) {
    s32 i;
    u16 best = 0xFFFF;
    s32 bestIndex = 0;
    for (i = 0; i < 3; i++) {
        if (isMonsterTargetValid(i, ctx->m0_scriptPtr[2])) {
            if (battleEntities.m0_entities[i].m0_base.m4C_HP <= best) {
                best = battleEntities.m0_entities[i].m0_base.m4C_HP;
                bestIndex = i;
            }
        }
    }
    monstersScriptsEntities[entity].m20_s16Vars[ctx->m0_scriptPtr[1]] = characterIdToTargetBitmask(bestIndex);
}

void monsterBattleOpcode_selectLowestHpEnemy(sBattleScriptExecutionContext* ctx, u8 entity) {
    s32 i;
    u16 best = 0xFFFF;
    s32 bestIndex = 0;
    for (i = 3; i < 11; i++) {
        if (isMonsterTargetValid(i, ctx->m0_scriptPtr[2])) {
            if (battleEntities.m0_entities[i].m0_base.m4C_HP <= best) {
                best = battleEntities.m0_entities[i].m0_base.m4C_HP;
                bestIndex = i;
            }
        }
    }
    monstersScriptsEntities[entity].m20_s16Vars[ctx->m0_scriptPtr[1]] = characterIdToTargetBitmask(bestIndex);
}

void monsterBattleOpcode_selectRandGearPCSameRow(sBattleScriptExecutionContext* ctx, u8 entity) {
    u8 tested[3];
    u8 target;

    tested[0] = tested[1] = tested[2] = 0;
    monstersScriptsEntities[entity].m20_s16Vars[ctx->m0_scriptPtr[1]] = 0;
    while (!(tested[0] & tested[1] & tested[2])) {
        target = randomInRange(0, 2);
        if (!tested[target]) {
            if (isMonsterTargetValid(target, ctx->m0_scriptPtr[2]) && apConfigArray[target].m1_gearState != 0) {
                if (battleVisualEntities[entity + 3].m0_positionSlot == battleVisualEntities[target].m0_positionSlot) {
                    monstersScriptsEntities[entity].m20_s16Vars[ctx->m0_scriptPtr[1]] = characterIdToTargetBitmask(target);
                    return;
                }
            }
            tested[target] = 1;
        }
    }
}

void monsterBattleOpcode_selectRandomGearPC(sBattleScriptExecutionContext* ctx, u8 entity) {
    u8 tested[3];
    u8 target;

    tested[0] = tested[1] = tested[2] = 0;
    monstersScriptsEntities[entity].m20_s16Vars[ctx->m0_scriptPtr[1]] = 0;
    while (!(tested[0] & tested[1] & tested[2])) {
        target = randomInRange(0, 2);
        if (!tested[target]) {
            if (isMonsterTargetValid(target, ctx->m0_scriptPtr[2])) {
                if (apConfigArray[target].m1_gearState != 0) {
                    monstersScriptsEntities[entity].m20_s16Vars[ctx->m0_scriptPtr[1]] = characterIdToTargetBitmask(target);
                    return;
                }
            }
            tested[target] = 1;
        }
    }
}

void monsterBattleOpcode_selectRandomGearEnemy(sBattleScriptExecutionContext* ctx, u8 entity) {
    s32 i = 3;
    s32 count = 0;
    u8 candidates[8];
    monstersScriptsEntities[entity].m20_s16Vars[ctx->m0_scriptPtr[1]] = 0;
    for (; i < 11; i++) {
        if (isMonsterTargetValid(i, ctx->m0_scriptPtr[2]) && apConfigArray[i].m1_gearState != 0) {
            candidates[count++] = i;
        }
    }
    if (count != 0) {
        monstersScriptsEntities[entity].m20_s16Vars[ctx->m0_scriptPtr[1]] = characterIdToTargetBitmask(candidates[(u8)randomInRange(0, (u8)(count - 1))]);
    }
}

void monsterBattleOpcode_countPCsInRow(sBattleScriptExecutionContext* ctx, u8 entity) {
    s32 i;
    u8 count = 0;
    for (i = 0; i < 3; i++) {
        if (isMonsterTargetValid(i, 0) && battleVisualEntities[i].m0_positionSlot == ctx->m0_scriptPtr[2]) {
            count++;
        }
    }
    monstersScriptsEntities[entity].m30_s8Vars[ctx->m0_scriptPtr[1]] = count;
}

void monsterBattleOpcode_countEnemiesInRow(sBattleScriptExecutionContext* ctx, u8 entity) {
    s32 i;
    u8 count = 0;
    for (i = 3; i < 11; i++) {
        if (isMonsterTargetValid(i, 0) && battleVisualEntities[i].m0_positionSlot == ctx->m0_scriptPtr[2]) {
            count++;
        }
    }
    monstersScriptsEntities[entity].m30_s8Vars[ctx->m0_scriptPtr[1]] = count;
}

void monsterBattleOpcode_getRowTableValue(sBattleScriptExecutionContext* ctx, u8 entity) {
    u8 target = monsterBattleOpcode_countEnemiesSameRow_findFirstInBitmask(monstersScriptsEntities[entity].m20_s16Vars[ctx->m0_scriptPtr[2]]);
    monstersScriptsEntities[entity].m30_s8Vars[ctx->m0_scriptPtr[1]] = (battleMechaInitData + ((battleVisualEntities[target].m0_positionSlot << 3) + (battleVisualEntities[entity + 3].m0_positionSlot << 6)))[0x140];
}

void monsterBattleOpcode_countPCsSameRow(sBattleScriptExecutionContext* ctx, u8 entity) {
    s32 i;
    u8 count = 0;
    for (i = 0; i < 3; i++) {
        if (isMonsterTargetValid(i, 0) && battleVisualEntities[i].m0_positionSlot == battleVisualEntities[monsterBattleOpcode_countEnemiesSameRow_findFirstInBitmask(monstersScriptsEntities[entity].m20_s16Vars[ctx->m0_scriptPtr[2]])].m0_positionSlot) {
            count++;
        }
    }
    monstersScriptsEntities[entity].m30_s8Vars[ctx->m0_scriptPtr[1]] = count;
}

void monsterBattleOpcode_countEnemiesSameRow(sBattleScriptExecutionContext* ctx, u8 entity) {
    s32 i;
    u8 count = 0;
    for (i = 3; i < 11; i++) {
        if (isMonsterTargetValid(i, 0) && battleVisualEntities[i].m0_positionSlot == battleVisualEntities[monsterBattleOpcode_countEnemiesSameRow_findFirstInBitmask(monstersScriptsEntities[entity].m20_s16Vars[ctx->m0_scriptPtr[2]])].m0_positionSlot) {
            count++;
        }
    }
    monstersScriptsEntities[entity].m30_s8Vars[ctx->m0_scriptPtr[1]] = count;
}

void monsterBattleOpcode_getItemCount(sBattleScriptExecutionContext* ctx, u8 entity) {
    s32 i;
    monstersScriptsEntities[entity].m30_s8Vars[ctx->m0_scriptPtr[1]] = 0;
    for (i = 0; i < 0x30; i++) {
        if (battleEffectBF.m42_ids[i] == ctx->m0_scriptPtr[2]) {
            monstersScriptsEntities[entity].m30_s8Vars[ctx->m0_scriptPtr[1]] = battleEffectBF.m12_counts[i];
            return;
        }
    }
}

void monsterBattleOpcode_copyVarToContext(sBattleScriptExecutionContext* ctx, sBattleScriptContext* contexts, u8 entity, u8 index) {
    u16 v = monstersScriptsEntities[entity].m20_s16Vars[ctx->m0_scriptPtr[2]];
    ((u8*)contexts)[index * 8 + ctx->m0_scriptPtr[1]] = v;
    (&((u8*)contexts)[index * 8 + ctx->m0_scriptPtr[1]])[1] = v >> 8;
}

void monsterBattleOpcode_loadGold(sBattleScriptExecutionContext* ctx, u8 entity) {
    monstersScriptsEntities[entity].m10_s32Vars[ctx->m0_scriptPtr[1]] = gameState.m1924_gold;
}

void monsterBattleOpcode_selectRandByLoadout(sBattleScriptExecutionContext* ctx, u8 entity) {
    s32 i = 0;
    s32 count = 0;
    u8 candidates[12];
    monstersScriptsEntities[entity].m20_s16Vars[ctx->m0_scriptPtr[1]] = 0;
    for (; i < 11; i++) {
        if (isBattleEntityAliveAndVisible(i) && battleEntities.m0_entities[i].m0_base.m56_battleCommandLoadout == ctx->m0_scriptPtr[2]) {
            candidates[count++] = i;
        }
    }
    if (count != 0) {
        monstersScriptsEntities[entity].m20_s16Vars[ctx->m0_scriptPtr[1]] = characterIdToTargetBitmask(candidates[(u8)randomInRange(0, (u8)(count - 1))]);
    }
}

void monsterBattleOpcode_loadSelfHitResult(sBattleScriptExecutionContext* ctx, u8 entity) {
    monstersScriptsEntities[entity].m10_s32Vars[ctx->m0_scriptPtr[1]] = battleEntities.m5FA0_initBattleAttackStatusArray0[entity + 3];
}

void monsterBattleOpcode_selectRandomHiddenEnemy(sBattleScriptExecutionContext* ctx, u8 entity) {
    s32 i = 3;
    s32 count = 0;
    u8 candidates[8];
    monstersScriptsEntities[entity].m20_s16Vars[ctx->m0_scriptPtr[1]] = 0;
    for (; i < 11; i++) {
        if (isEnemySlotTargetable(i, ctx->m0_scriptPtr[2]) && (battleVisualEntities[i].m3_hidden & 0x80)) {
            candidates[count++] = i;
        }
    }
    if (count != 0) {
        monstersScriptsEntities[entity].m20_s16Vars[ctx->m0_scriptPtr[1]] = characterIdToTargetBitmask(candidates[(u8)randomInRange(0, (u8)(count - 1))]);
    }
}

void monsterBattleOpcode_selectRandomKOPC(sBattleScriptExecutionContext* ctx, u8 entity) {
    u8 tested[3];
    u8 target;

    tested[0] = tested[1] = tested[2] = 0;
    monstersScriptsEntities[entity].m20_s16Vars[ctx->m0_scriptPtr[1]] = 0;
    while (!(tested[0] & tested[1] & tested[2])) {
        target = randomInRange(0, 2);
        if (!tested[target]) {
            if (battleEntities.m0_entities[target].m0_base.m7C_statusFlags[0] & 0x8000) {
                if (!(battleEntities.m0_entities[target].m0_base.m7C_statusFlags[0] & 0x4002)) {
                    monstersScriptsEntities[entity].m20_s16Vars[ctx->m0_scriptPtr[1]] = characterIdToTargetBitmask(target);
                    return;
                }
            }
            tested[target] = 1;
        }
    }
}

void monsterBattleOpcode_countAlivePCs(sBattleScriptExecutionContext* ctx, u8 entity) {
    s32 i;
    u8 count = 0;
    for (i = 0; i < 3; i++) {
        if (!(battleEntities.m0_entities[i].m0_base.m7C_statusFlags[0] & 0xC000)) {
            count++;
        }
    }
    monstersScriptsEntities[entity].m30_s8Vars[ctx->m0_scriptPtr[1]] = count;
}

void monsterBattleOpcode_countAliveEnemies(sBattleScriptExecutionContext* ctx, u8 entity) {
    s32 i;
    u8 count = 0;
    for (i = 3; i < 11; i++) {
        if (isBattleSlotFilled.m0_isBattleSlotFilled[i] != 0 && !(battleEntities.m0_entities[i].m0_base.m7C_statusFlags[0] & 0xC000) && battleVisualEntities[i].m3_hidden == 0) {
            count++;
        }
    }
    monstersScriptsEntities[entity].m30_s8Vars[ctx->m0_scriptPtr[1]] = count;
}

void monsterBattleOpcode_selectLowestHpGearPC(sBattleScriptExecutionContext* ctx, u8 entity) {
    s32 i;
    u32 best = 0xFFFFFFFF;
    s32 bestIndex = 0;
    for (i = 0; i < 3; i++) {
        if (isMonsterTargetValid(i, ctx->m0_scriptPtr[2]) && apConfigArray[i].m1_gearState != 0) {
            if (battleEntities.m0_entities[i].mA4_gear.m60_hp <= best) {
                best = battleEntities.m0_entities[i].mA4_gear.m60_hp;
                bestIndex = i;
            }
        }
    }
    monstersScriptsEntities[entity].m20_s16Vars[ctx->m0_scriptPtr[1]] = characterIdToTargetBitmask(bestIndex);
}

void monsterBattleOpcode_selectLowestHpGearEnemy(sBattleScriptExecutionContext* ctx, u8 entity) {
    s32 i;
    u32 best = 0xFFFFFFFF;
    s32 bestIndex = 0;
    for (i = 3; i < 11; i++) {
        if (isMonsterTargetValid(i, ctx->m0_scriptPtr[2]) && apConfigArray[i].m1_gearState != 0) {
            if (battleEntities.m0_entities[i].m0_base.m4C_HP <= best) {
                best = battleEntities.m0_entities[i].m0_base.m4C_HP;
                bestIndex = i;
            }
        }
    }
    monstersScriptsEntities[entity].m20_s16Vars[ctx->m0_scriptPtr[1]] = characterIdToTargetBitmask(bestIndex);
}

void monsterBattleOpcode_selectRandPCByStat(sBattleScriptExecutionContext* ctx, u8 entity) {
    s32 i = 0;
    s32 count = 0;
    u8 candidates[8];
    monstersScriptsEntities[entity].m20_s16Vars[ctx->m0_scriptPtr[2]] = 0;
    for (; i < 3; i++) {
        if (isMonsterTargetValid(i, 0) && (monstersScriptsEntities[entity].m20_s16Vars[ctx->m0_scriptPtr[3]] & accessBattleEntityWordStat(i, ctx->m0_scriptPtr[1], 0, 1))) {
            candidates[count++] = i;
        }
    }
    if (count != 0) {
        monstersScriptsEntities[entity].m20_s16Vars[ctx->m0_scriptPtr[2]] = characterIdToTargetBitmask(candidates[(u8)randomInRange(0, (u8)(count - 1))]);
    }
}

void monsterBattleOpcode_selectRandEnemyByStat(sBattleScriptExecutionContext* ctx, u8 entity) {
    s32 count = 0;
    s32 i = 3;
    u8 candidates[8];
    monstersScriptsEntities[entity].m20_s16Vars[ctx->m0_scriptPtr[2]] = 0;
    for (; i < 11; i++) {
        if (isMonsterTargetValid(i, 0) && (monstersScriptsEntities[entity].m20_s16Vars[ctx->m0_scriptPtr[3]] & accessBattleEntityWordStat(i, ctx->m0_scriptPtr[1], 0, 1))) {
            candidates[count++] = i;
        }
    }
    if (count != 0) {
        monstersScriptsEntities[entity].m20_s16Vars[ctx->m0_scriptPtr[2]] = characterIdToTargetBitmask(candidates[(u8)randomInRange(0, (u8)(count - 1))]);
    }
}

void monsterBattleOpcode_selectRandGearPCByStat(sBattleScriptExecutionContext* ctx, u8 entity) {
    s32 i = 0;
    s32 count = 0;
    u8 candidates[8];
    monstersScriptsEntities[entity].m20_s16Vars[ctx->m0_scriptPtr[2]] = 0;
    for (; i < 3; i++) {
        if (isMonsterTargetValid(i, 0) && apConfigArray[i].m1_gearState != 0 && (monstersScriptsEntities[entity].m20_s16Vars[ctx->m0_scriptPtr[3]] & accessBattleEntityWordStat(i, ctx->m0_scriptPtr[1], 0, 1))) {
            candidates[count++] = i;
        }
    }
    if (count != 0) {
        monstersScriptsEntities[entity].m20_s16Vars[ctx->m0_scriptPtr[2]] = characterIdToTargetBitmask(candidates[(u8)randomInRange(0, (u8)(count - 1))]);
    }
}

void monsterBattleOpcode_selectRandGearEnemyByStat(sBattleScriptExecutionContext* ctx, u8 entity) {
    s32 i = 3;
    s32 count = 0;
    u8 candidates[8];
    monstersScriptsEntities[entity].m20_s16Vars[ctx->m0_scriptPtr[2]] = 0;
    for (; i < 11; i++) {
        if (isMonsterTargetValid(i, 0) && apConfigArray[i].m1_gearState != 0 && (monstersScriptsEntities[entity].m20_s16Vars[ctx->m0_scriptPtr[3]] & accessBattleEntityWordStat(i, ctx->m0_scriptPtr[1], 0, 1))) {
            candidates[count++] = i;
        }
    }
    if (count != 0) {
        monstersScriptsEntities[entity].m20_s16Vars[ctx->m0_scriptPtr[2]] = characterIdToTargetBitmask(candidates[(u8)randomInRange(0, (u8)(count - 1))]);
    }
}

void monsterBattleOpcode_selectAnyPCByStat(sBattleScriptExecutionContext* ctx, u8 entity) {
    s32 i = 0;
    s32 count = 0;
    u8 candidates[8];
    monstersScriptsEntities[entity].m20_s16Vars[ctx->m0_scriptPtr[2]] = 0;
    for (; i < 3; i++) {
        if (isMonsterTargetValid(i, 1) && (monstersScriptsEntities[entity].m20_s16Vars[ctx->m0_scriptPtr[3]] & accessBattleEntityWordStat(i, ctx->m0_scriptPtr[1], 0, 1))) {
            candidates[count++] = i;
        }
    }
    if (count != 0) {
        monstersScriptsEntities[entity].m20_s16Vars[ctx->m0_scriptPtr[2]] = characterIdToTargetBitmask(candidates[(u8)randomInRange(0, (u8)(count - 1))]);
    }
}

void monsterBattleOpcode_selectAnyGearPCByStat(sBattleScriptExecutionContext* ctx, u8 entity) {
    s32 i = 0;
    s32 count = 0;
    u8 candidates[8];
    monstersScriptsEntities[entity].m20_s16Vars[ctx->m0_scriptPtr[2]] = 0;
    for (; i < 3; i++) {
        if (isMonsterTargetValid(i, 1) && apConfigArray[i].m1_gearState != 0 && (monstersScriptsEntities[entity].m20_s16Vars[ctx->m0_scriptPtr[3]] & accessBattleEntityWordStat(i, ctx->m0_scriptPtr[1], 0, 1))) {
            candidates[count++] = i;
        }
    }
    if (count != 0) {
        monstersScriptsEntities[entity].m20_s16Vars[ctx->m0_scriptPtr[2]] = characterIdToTargetBitmask(candidates[(u8)randomInRange(0, (u8)(count - 1))]);
    }
}

void monsterBattleOpcode_setSelfHidden(sBattleScriptExecutionContext* ctx, u8 entity) {
    battleVisualEntities[(entity) + 3].m3_hidden = ctx->m0_scriptPtr[1];
    if (ctx->m0_scriptPtr[1] & 0x80) {
        moveEntityToOtherEntity(entity + 3, entity + 3);
    } else {
        markEnemyDead(entity + 3);
    }
}

void monsterBattleOpcode_storeSelfBitmask(sBattleScriptExecutionContext* ctx, u8 entity) {
    monstersScriptsEntities[entity].m20_s16Vars[ctx->m0_scriptPtr[1]] = characterIdToTargetBitmask(entity + 3);
}

void monsterBattleOpcode_getAlivePCsMask(sBattleScriptExecutionContext* ctx, u8 entity) {
    u16 mask = 0;
    s32 i;
    for (i = 2; i >= 0; i--) {
        if (isBattleEntityAliveAndVisible(i)) {
            switch (ctx->m0_scriptPtr[2]) {
            case 1:
                if (apConfigArray[i].m1_gearState != 0) {
                    mask |= 1;
                }
                break;
            case 2:
                if (apConfigArray[i].m1_gearState == 0) {
                    mask |= 1;
                }
                break;
            default:
                mask |= 1;
                break;
            }
        }
        mask <<= 1;
    }
    monstersScriptsEntities[entity].m20_s16Vars[ctx->m0_scriptPtr[1]] = mask >> 1;
}

void monsterBattleOpcode_getAliveEnemiesMask(sBattleScriptExecutionContext* ctx, u8 entity) {
    u16 mask = 0;
    s32 i;
    for (i = 10; i >= 3; i--) {
        if (isBattleEntityAliveAndVisible(i)) {
            switch (ctx->m0_scriptPtr[2]) {
            case 1:
                if (apConfigArray[i].m1_gearState != 0) {
                    mask |= 1;
                }
                break;
            case 2:
                if (apConfigArray[i].m1_gearState == 0) {
                    mask |= 1;
                }
                break;
            default:
                mask |= 1;
                break;
            }
        }
        mask <<= 1;
    }
    monstersScriptsEntities[entity].m20_s16Vars[ctx->m0_scriptPtr[1]] = mask << 2;
}

void monsterBattleOpcode_getPCsSameRowMask(sBattleScriptExecutionContext* ctx, u8 entity) {
    u16 mask = 0;
    s32 i;
    for (i = 2; i >= 0; i--) {
        if (isBattleEntityAliveAndVisible(i) && battleVisualEntities[monsterBattleOpcode_countEnemiesSameRow_findFirstInBitmask(monstersScriptsEntities[entity].m20_s16Vars[ctx->m0_scriptPtr[2]])].m0_positionSlot == battleVisualEntities[i].m0_positionSlot) {
            mask |= 1;
        }
        mask <<= 1;
    }
    monstersScriptsEntities[entity].m20_s16Vars[ctx->m0_scriptPtr[1]] = mask >> 1;
}

void monsterBattleOpcode_getEnemiesSameRowMask(sBattleScriptExecutionContext* ctx, u8 entity) {
    u16 mask = 0;
    s32 i;
    for (i = 10; i >= 3; i--) {
        if (isBattleEntityAliveAndVisible(i) && battleVisualEntities[monsterBattleOpcode_countEnemiesSameRow_findFirstInBitmask(monstersScriptsEntities[entity].m20_s16Vars[ctx->m0_scriptPtr[2]])].m0_positionSlot == battleVisualEntities[i].m0_positionSlot) {
            mask |= 1;
        }
        mask <<= 1;
    }
    monstersScriptsEntities[entity].m20_s16Vars[ctx->m0_scriptPtr[1]] = mask << 2;
}

void monsterBattleOpcode_resetSelfHitResult(u8 entity) {
    battleEntities.m5F6C_damageDonePerAttack[entity + 3] = 0;
    battleEntities.m5FA0_initBattleAttackStatusArray0[entity + 3] = 4;
}

void monsterBattleOpcode_addDwordVars(sBattleScriptExecutionContext* ctx, u8 entity) {
    monstersScriptsEntities[entity].m10_s32Vars[ctx->m0_scriptPtr[3]] = monstersScriptsEntities[entity].m10_s32Vars[ctx->m0_scriptPtr[1]] + monstersScriptsEntities[entity].m10_s32Vars[ctx->m0_scriptPtr[2]];
}

void monsterBattleOpcode_subDwordVars(sBattleScriptExecutionContext* ctx, u8 entity) {
    monstersScriptsEntities[entity].m10_s32Vars[ctx->m0_scriptPtr[3]] = monstersScriptsEntities[entity].m10_s32Vars[ctx->m0_scriptPtr[1]] - monstersScriptsEntities[entity].m10_s32Vars[ctx->m0_scriptPtr[2]];
}

void monsterBattleOpcode_mulDwordVar(sBattleScriptExecutionContext* ctx, u8 entity) {
    monstersScriptsEntities[entity].m10_s32Vars[ctx->m0_scriptPtr[1]] *= ctx->m0_scriptPtr[2];
}

void monsterBattleOpcode_divDwordVar(sBattleScriptExecutionContext* ctx, u8 entity) {
    monstersScriptsEntities[entity].m10_s32Vars[ctx->m0_scriptPtr[1]] = R3000_DIV((u32)monstersScriptsEntities[entity].m10_s32Vars[ctx->m0_scriptPtr[1]], ctx->m0_scriptPtr[2]);
}

void monsterBattleOpcode_setBattleEventVar(sBattleScriptExecutionContext* ctx) {
    /* Without a battle event (no battle_event overlay data, as in a debug-selector battle whose
     * enemy script expects one: selector 12/11), battleEventVar0 is NULL and the PS1 writes the
     * s16 into low kernel RAM at 0x394 + 2 * n (the BIOS A(nn) table), which nothing here reads
     * back; the PC skips the write. */
    if (battleEventVar0 == NULL) {
        return;
    }
    battleEventVar0->m394_vars[ctx->m0_scriptPtr[1]] = ctx->m0_scriptPtr[2];
}

void monsterBattleOpcode_setCommandEnabled(sBattleScriptExecutionContext* ctx, u8 entity) {
    s32 i;
    u8 enable = ctx->m0_scriptPtr[2] != 0;
    for (i = 0; i < 3; i++) {
        if (enable) {
            battleEntities.m0_entities[i].m0_base.m7A_commandEnabledBF |= characterIdToReverseBitmask(ctx->m0_scriptPtr[1] + 7);
        } else {
            battleEntities.m0_entities[i].m0_base.m7A_commandEnabledBF &= ~characterIdToReverseBitmask(ctx->m0_scriptPtr[1] + 7);
        }
    }
}

void monsterBattleOpcode_setRowTableValue(sBattleScriptExecutionContext* ctx) {
    (battleMechaInitData + ((ctx->m0_scriptPtr[2] << 3) + (ctx->m0_scriptPtr[1] << 6)))[0x140] = ctx->m0_scriptPtr[3];
}

void monsterBattleOpcode_forceNextTurn(sBattleScriptExecutionContext* ctx, u8 entity) {
    bBattleTickMode1 = monsterBattleOpcode_countEnemiesSameRow_findFirstInBitmask(monstersScriptsEntities[entity].m20_s16Vars[ctx->m0_scriptPtr[1]]) + 1;
}

void monsterBattleOpcode_resetSlotStatus(void) {
    u8 buf[16];
    battleGetSlotStatus(buf);
}

s32 executeMonsterScriptUpper_81(sBattleScriptExecutionContext* ctx, u8 entity) {
    return monstersScriptsEntities[entity].m30_s8Vars[ctx->m0_scriptPtr[1]] == ctx->m0_scriptPtr[2];
}

s32 executeMonsterScriptUpper_82(sBattleScriptExecutionContext* ctx, u8 entity) {
    u16 v = ctx->m0_scriptPtr[2] | (ctx->m0_scriptPtr[3] << 8);
    return monstersScriptsEntities[entity].m20_s16Vars[ctx->m0_scriptPtr[1]] == v;
}

s32 monsterBattleOpcode_ifByteVarLE(sBattleScriptExecutionContext* ctx, u8 entity) {
    return monstersScriptsEntities[entity].m30_s8Vars[ctx->m0_scriptPtr[1]] <= ctx->m0_scriptPtr[2];
}

s32 monsterBattleOpcode_ifWordVarLE(sBattleScriptExecutionContext* ctx, u8 entity) {
    u16 v = ctx->m0_scriptPtr[2] | (ctx->m0_scriptPtr[3] << 8);
    return monstersScriptsEntities[entity].m20_s16Vars[ctx->m0_scriptPtr[1]] <= v;
}

s32 monsterBattleOpcode_ifByteVarGE(sBattleScriptExecutionContext* ctx, u8 entity) {
    return monstersScriptsEntities[entity].m30_s8Vars[ctx->m0_scriptPtr[1]] >= ctx->m0_scriptPtr[2];
}

s32 monsterBattleOpcode_ifWordVarGE(sBattleScriptExecutionContext* ctx, u8 entity) {
    u16 v = ctx->m0_scriptPtr[2] | (ctx->m0_scriptPtr[3] << 8);
    return monstersScriptsEntities[entity].m20_s16Vars[ctx->m0_scriptPtr[1]] >= v;
}

s32 monsterBattleOpcode_ifByteVarsEqual(sBattleScriptExecutionContext* ctx, u8 entity) {
    return monstersScriptsEntities[entity].m30_s8Vars[ctx->m0_scriptPtr[1]] == monstersScriptsEntities[entity].m30_s8Vars[ctx->m0_scriptPtr[2]];
}

s32 monsterBattleOpcode_ifWordVarsEqual(sBattleScriptExecutionContext* ctx, u8 entity) {
    return monstersScriptsEntities[entity].m20_s16Vars[ctx->m0_scriptPtr[1]] == monstersScriptsEntities[entity].m20_s16Vars[ctx->m0_scriptPtr[2]];
}

s32 monsterBattleOpcode_ifByteVarsLE(sBattleScriptExecutionContext* ctx, u8 entity) {
    return monstersScriptsEntities[entity].m30_s8Vars[ctx->m0_scriptPtr[1]] <= monstersScriptsEntities[entity].m30_s8Vars[ctx->m0_scriptPtr[2]];
}

s32 monsterBattleOpcode_ifWordVarsLE(sBattleScriptExecutionContext* ctx, u8 entity) {
    return monstersScriptsEntities[entity].m20_s16Vars[ctx->m0_scriptPtr[1]] <= monstersScriptsEntities[entity].m20_s16Vars[ctx->m0_scriptPtr[2]];
}

s32 monsterBattleOpcode_ifByteVarAnd(sBattleScriptExecutionContext* ctx, u8 entity) {
    return (monstersScriptsEntities[entity].m30_s8Vars[ctx->m0_scriptPtr[1]] & ctx->m0_scriptPtr[2]) != 0;
}

s32 monsterBattleOpcode_ifWordVarAnd(sBattleScriptExecutionContext* ctx, u8 entity) {
    u16 v = ctx->m0_scriptPtr[2] + (ctx->m0_scriptPtr[3] << 8);
    return (monstersScriptsEntities[entity].m20_s16Vars[ctx->m0_scriptPtr[1]] & v) != 0;
}

s32 monsterBattleOpcode_ifByteVarsAnd(sBattleScriptExecutionContext* ctx, u8 entity) {
    return (monstersScriptsEntities[entity].m30_s8Vars[ctx->m0_scriptPtr[1]] & monstersScriptsEntities[entity].m30_s8Vars[ctx->m0_scriptPtr[2]]) != 0;
}

s32 monsterBattleOpcode_ifWordVarsAnd(sBattleScriptExecutionContext* ctx, u8 entity) {
    return (monstersScriptsEntities[entity].m20_s16Vars[ctx->m0_scriptPtr[1]] & monstersScriptsEntities[entity].m20_s16Vars[ctx->m0_scriptPtr[2]]) != 0;
}

s32 monsterBattleOpcode_ifByteVarNotEqual(sBattleScriptExecutionContext* ctx, u8 entity) {
    return monstersScriptsEntities[entity].m30_s8Vars[ctx->m0_scriptPtr[1]] != ctx->m0_scriptPtr[2];
}

s32 monsterBattleOpcode_90(sBattleScriptExecutionContext* ctx, u8 entity) {
    u16 v = ctx->m0_scriptPtr[2] | (ctx->m0_scriptPtr[3] << 8);
    return monstersScriptsEntities[entity].m20_s16Vars[ctx->m0_scriptPtr[1]] != v;
}

s32 monsterBattleOpcode_ifByteVarsNotEqual(sBattleScriptExecutionContext* ctx, u8 entity) {
    return monstersScriptsEntities[entity].m30_s8Vars[ctx->m0_scriptPtr[1]] != monstersScriptsEntities[entity].m30_s8Vars[ctx->m0_scriptPtr[2]];
}

s32 monsterBattleOpcode_ifWordVarsNotEqual(sBattleScriptExecutionContext* ctx, u8 entity) {
    return monstersScriptsEntities[entity].m20_s16Vars[ctx->m0_scriptPtr[1]] != monstersScriptsEntities[entity].m20_s16Vars[ctx->m0_scriptPtr[2]];
}

s32 monsterBattleOpcode_ifDwordVarsEqual(sBattleScriptExecutionContext* ctx, u8 entity) {
    return monstersScriptsEntities[entity].m10_s32Vars[ctx->m0_scriptPtr[1]] == monstersScriptsEntities[entity].m10_s32Vars[ctx->m0_scriptPtr[2]];
}

s32 monsterBattleOpcode_ifDwordVarsLE(sBattleScriptExecutionContext* ctx, u8 entity) {
    return (u32)monstersScriptsEntities[entity].m10_s32Vars[ctx->m0_scriptPtr[1]] <= (u32)monstersScriptsEntities[entity].m10_s32Vars[ctx->m0_scriptPtr[2]];
}

s32 monsterBattleOpcode_ifEntityKO(sBattleScriptExecutionContext* ctx) {
    return battleEntities.m0_entities[ctx->m0_scriptPtr[1]].m0_base.m7C_statusFlags[0] >> 15;
}

s32 monsterBattleOpcode_ifSlotLayoutEmpty(sBattleScriptExecutionContext* ctx) {
    return battleSlotLayout[ctx->m0_scriptPtr[1] * 4] == 0;
}

s32 monsterBattleOpcode_ifFirstTwoPCsDown(void) {
    return (allPlayerCharacterBitmask & 3) == 0;
}

s32 monsterBattleOpcode_ifAllEnemiesHidden(void) {
    s32 result = 1;
    s32 i;
    for (i = 0; i < 8; i++) {
        if ((allPlayerCharacterBitmask >> 5) != 0 && !(battleVisualEntities[i + 3].m3_hidden & 0x80)) {
            result = 0;
            break;
        }
    }
    return result;
}

s32 monsterBattleOpcode_ifSelfHidden(u8 entity) {
    return battleVisualEntities[entity + 3].m3_hidden >> 7;
}

u8 executeMonsterScriptLower(sBattleScriptExecutionContext* ctx, u8 entity, u8 index) {
    sBattleScriptContext* contexts = battleScriptContext;

    switch (*ctx->m0_scriptPtr) {
    case 0x1:
        index = monsterBattleOpcode_writeContextByte(ctx, contexts, index);
        break;
    case 0x2:
        monsterBattleOpcode_writeContextByteVar(ctx, contexts, entity, index);
        break;
    case 0x3:
        monsterBattleOpcode_copyContextEntry(ctx, contexts);
        break;
    case 0x4:
        monsterBattleOpcode_setByteVar(ctx, entity);
        break;
    case 0x5:
        monsterBattleOpcode_setWordVar(ctx, entity);
        break;
    case 0x6:
        monsterBattleOpcode_setDwordVar(ctx, entity);
        break;
    case 0x7:
        monsterBattleOpcode_setGameStateVar(ctx);
        break;
    case 0x8:
        monsterBattleOpcode_addByteVar(ctx, entity);
        break;
    case 0x9:
        monsterBattleOpcode_subByteVar(ctx, entity);
        break;
    case 0xA:
        monsterBattleOpcode_mulByteVar(ctx, entity);
        break;
    case 0xB:
        monsterBattleOpcode_divByteVar(ctx, entity);
        break;
    case 0xC:
        monsterBattleOpcode_moduloByteVar(ctx, entity);
        break;
    case 0xD:
        monsterBattleOpcode_andByteVar(ctx, entity);
        break;
    case 0xE:
        monsterBattleOpcode_orByteVar(ctx, entity);
        break;
    case 0xF:
        monsterBattleOpcode_xorByteVar(ctx, entity);
        break;
    case 0x10:
        monsterBattleOpcode_addWordVar(ctx, entity);
        break;
    case 0x11:
        monsterBattleOpcode_subWordVar(ctx, entity);
        break;
    case 0x12:
        monsterBattleOpcode_mulWordVar(ctx, entity);
        break;
    case 0x13:
        monsterBattleOpcode_divWordVar(ctx, entity);
        break;
    case 0x14:
        monsterBattleOpcode_moduloWordVar(ctx, entity);
        break;
    case 0x15:
        monsterBattleOpcode_andWordVar(ctx, entity);
        break;
    case 0x16:
        monsterBattleOpcode_orWordVar(ctx, entity);
        break;
    case 0x17:
        monsterBattleOpcode_xorWordVar(ctx, entity);
        break;
    case 0x18:
        monsterBattleOpcode_addByteVars(ctx, entity);
        break;
    case 0x19:
        monsterBattleOpcode_subByteVars(ctx, entity);
        break;
    case 0x1A:
        monsterBattleOpcode_mulByteVars(ctx, entity);
        break;
    case 0x1B:
        monsterBattleOpcode_divByteVars(ctx, entity);
        break;
    case 0x1C:
        monsterBattleOpcode_moduloByteVars(ctx, entity);
        break;
    case 0x1D:
        monsterBattleOpcode_andByteVars(ctx, entity);
        break;
    case 0x1E:
        monsterBattleOpcode_orByteVars(ctx, entity);
        break;
    case 0x1F:
        monsterBattleOpcode_xorByteVars(ctx, entity);
        break;
    case 0x20:
        monsterBattleOpcode_addWordVars(ctx, entity);
        break;
    case 0x21:
        monsterBattleOpcode_subWordVars(ctx, entity);
        break;
    case 0x22:
        monsterBattleOpcode_mulWordVars(ctx, entity);
        break;
    case 0x23:
        monsterBattleOpcode_divWordVars(ctx, entity);
        break;
    case 0x24:
        monsterBattleOpcode_moduloWordVars(ctx, entity);
        break;
    case 0x25:
        monsterBattleOpcode_andWordVars(ctx, entity);
        break;
    case 0x26:
        monsterBattleOpcode_orWordVars(ctx, entity);
        break;
    case 0x27:
        monsterBattleOpcode_xorWordVars(ctx, entity);
        break;
    case 0x28:
        monsterBattleOpcode_setSelfByteStat(ctx, entity);
        break;
    case 0x29:
        monsterBattleOpcode_setSelfWordStat(ctx, entity);
        break;
    case 0x2A:
        monsterBattleOpcode_getTargetByteStat(ctx, entity);
        break;
    case 0x2B:
        index = monsterBattleOpcode_setTargetByteStat(ctx, entity, index);
        break;
    case 0x2C:
        monsterBattleOpcode_getTargetWordStat(ctx, entity);
        break;
    case 0x2D:
        index = monsterBattleOpcode_setTargetWordStat(ctx, entity, index);
        break;
    case 0x2E:
        monsterBattleOpcode_loadGearHpOrGold(ctx, entity);
        break;
    case 0x2F:
        index = monsterBattleOpcode_setTargetGearHp(ctx, entity, index);
        break;
    case 0x30:
        monsterBattleOpcode_loadGameStateVar(ctx, entity);
        break;
    case 0x31:
        monsterBattleOpcode_storeGameStateVar(ctx, entity);
        break;
    case 0x32:
        monsterBattleOpcode_copyByteVar(ctx, entity);
        break;
    case 0x33:
        monsterBattleOpcode_copyWordVar(ctx, entity);
        break;
    case 0x34:
        monsterBattleOpcode_copyDwordVar(ctx, entity);
        break;
    case 0x35:
        monsterBattleOpcode_byteVarToWordVar(ctx, entity);
        break;
    case 0x36:
        monsterBattleOpcode_wordVarToDwordVar(ctx, entity);
        break;
    case 0x37:
        monsterBattleOpcode_clearByteVars(entity);
        break;
    case 0x38:
        monsterBattleOpcode_clearWordVars(entity);
        break;
    case 0x39:
        monsterBattleOpcode_setSelfExperience(ctx, entity);
        break;
    case 0x3A:
        monsterBattleOpcode_setSelfGold(ctx, entity);
        break;
    case 0x3B:
        monsterBattleOpcode_setSelfDrop2(ctx, entity);
        break;
    case 0x3C:
        monsterBattleOpcode_setSelfDrop1(ctx, entity);
        break;
    case 0x3D:
        monsterBattleOpcode_writeContextWord(ctx, contexts, index);
        break;
    case 0x3E:
        monsterBattleOpcode_randomByteVar(ctx, entity);
        break;
    case 0x3F:
        monsterBattleOpcode_randomWordVar(ctx, entity);
        break;
    case 0x40:
        monsterBattleOpcode_selectRandomPC(ctx, entity);
        break;
    case 0x41:
        monsterBattleOpcode_selectRandomPCSameRow(ctx, entity);
        break;
    case 0x42:
        monsterBattleOpcode_selectRandEnemySameRow(ctx, entity);
        break;
    case 0x43:
        monsterBattleOpcode_selectRandomPCDiffRow(ctx, entity);
        break;
    case 0x44:
        monsterBattleOpcode_selectRandEnemyDiffRow(ctx, entity);
        break;
    case 0x45:
        monsterBattleOpcode_selectNextReadyPC(ctx, entity);
        break;
    case 0x46:
        monsterBattleOpcode_selectNextReadyEnemy(ctx, entity);
        break;
    case 0x47:
        monsterBattleOpcode_selectLowestHpPC(ctx, entity);
        break;
    case 0x48:
        monsterBattleOpcode_selectLowestHpEnemy(ctx, entity);
        break;
    case 0x49:
        monsterBattleOpcode_selectRandGearPCSameRow(ctx, entity);
        break;
    case 0x4A:
        monsterBattleOpcode_selectRandomGearPC(ctx, entity);
        break;
    case 0x4B:
        monsterBattleOpcode_selectRandomGearEnemy(ctx, entity);
        break;
    case 0x4C:
        monsterBattleOpcode_countPCsInRow(ctx, entity);
        break;
    case 0x4D:
        monsterBattleOpcode_countEnemiesInRow(ctx, entity);
        break;
    case 0x4E:
        monsterBattleOpcode_getRowTableValue(ctx, entity);
        break;
    case 0x4F:
        monsterBattleOpcode_countPCsSameRow(ctx, entity);
        break;
    case 0x50:
        monsterBattleOpcode_countEnemiesSameRow(ctx, entity);
        break;
    case 0x51:
        monsterBattleOpcode_getItemCount(ctx, entity);
        break;
    case 0x52:
        monsterBattleOpcode_copyVarToContext(ctx, contexts, entity, index);
        break;
    case 0x53:
        monsterBattleOpcode_loadGold(ctx, entity);
        break;
    case 0x54:
        monsterBattleOpcode_selectRandByLoadout(ctx, entity);
        break;
    case 0x55:
        monsterBattleOpcode_loadSelfHitResult(ctx, entity);
        break;
    case 0x56:
        monsterBattleOpcode_selectRandomHiddenEnemy(ctx, entity);
        break;
    case 0x57:
        monsterBattleOpcode_selectRandomKOPC(ctx, entity);
        break;
    case 0x58:
        monsterBattleOpcode_countAlivePCs(ctx, entity);
        break;
    case 0x59:
        monsterBattleOpcode_countAliveEnemies(ctx, entity);
        break;
    case 0x5A:
        monsterBattleOpcode_selectLowestHpGearPC(ctx, entity);
        break;
    case 0x5B:
        monsterBattleOpcode_selectLowestHpGearEnemy(ctx, entity);
        break;
    case 0x5C:
        monsterBattleOpcode_selectRandPCByStat(ctx, entity);
        break;
    case 0x5D:
        monsterBattleOpcode_selectRandEnemyByStat(ctx, entity);
        break;
    case 0x5E:
        monsterBattleOpcode_selectRandGearPCByStat(ctx, entity);
        break;
    case 0x5F:
        monsterBattleOpcode_selectRandGearEnemyByStat(ctx, entity);
        break;
    case 0x60:
        monsterBattleOpcode_selectAnyPCByStat(ctx, entity);
        break;
    case 0x61:
        monsterBattleOpcode_selectAnyGearPCByStat(ctx, entity);
        break;
    case 0x62:
        break;
    case 0x63:
        monsterBattleOpcode_setSelfHidden(ctx, entity);
        break;
    case 0x64:
        monsterBattleOpcode_storeSelfBitmask(ctx, entity);
        break;
    case 0x65:
        monsterBattleOpcode_getAlivePCsMask(ctx, entity);
        break;
    case 0x66:
        monsterBattleOpcode_getAliveEnemiesMask(ctx, entity);
        break;
    case 0x67:
        monsterBattleOpcode_getPCsSameRowMask(ctx, entity);
        break;
    case 0x68:
        monsterBattleOpcode_getEnemiesSameRowMask(ctx, entity);
        break;
    case 0x69:
        monsterBattleOpcode_resetSelfHitResult(entity);
        break;
    case 0x6A:
        monsterBattleOpcode_addDwordVars(ctx, entity);
        break;
    case 0x6B:
        monsterBattleOpcode_subDwordVars(ctx, entity);
        break;
    case 0x6C:
        monsterBattleOpcode_mulDwordVar(ctx, entity);
        break;
    case 0x6D:
        monsterBattleOpcode_divDwordVar(ctx, entity);
        break;
    case 0x70:
        monsterBattleOpcode_setBattleEventVar(ctx);
        break;
    case 0x71:
        monsterBattleOpcode_setCommandEnabled(ctx, entity);
        break;
    case 0x72:
        monsterBattleOpcode_setRowTableValue(ctx);
        break;
    case 0x73:
        monsterBattleOpcode_forceNextTurn(ctx, entity);
        break;
    case 0x74:
        monsterBattleOpcode_resetSlotStatus();
        break;
    default:
        index = monsterBattleOpcode_queueLanguageError(ctx, contexts, entity, index);
        break;
    }
    advanceBattleScript(ctx);
    return index;
}

u8 executeMonsterScriptUpper(sBattleScriptExecutionContext* ctx, u8 entity) {
    u8 result = 0;
    u8 exitScript = 0;
    u8 accum = 0;

    do {
        switch (*ctx->m0_scriptPtr) {
        case 0x80:
        case 0x9A:
            result = 1;
            break;
        case 0x99:
            exitScript = 1;
            break;
        case 0x81:
            result = executeMonsterScriptUpper_81(ctx, entity);
            break;
        case 0x82:
            result = executeMonsterScriptUpper_82(ctx, entity);
            break;
        case 0x83:
            result = monsterBattleOpcode_ifByteVarLE(ctx, entity);
            break;
        case 0x84:
            result = monsterBattleOpcode_ifWordVarLE(ctx, entity);
            break;
        case 0x85:
            result = monsterBattleOpcode_ifByteVarGE(ctx, entity);
            break;
        case 0x86:
            result = monsterBattleOpcode_ifWordVarGE(ctx, entity);
            break;
        case 0x87:
            result = monsterBattleOpcode_ifByteVarsEqual(ctx, entity);
            break;
        case 0x88:
            result = monsterBattleOpcode_ifWordVarsEqual(ctx, entity);
            break;
        case 0x89:
            result = monsterBattleOpcode_ifByteVarsLE(ctx, entity);
            break;
        case 0x8A:
            result = monsterBattleOpcode_ifWordVarsLE(ctx, entity);
            break;
        case 0x8B:
            result = monsterBattleOpcode_ifByteVarAnd(ctx, entity);
            break;
        case 0x8C:
            result = monsterBattleOpcode_ifWordVarAnd(ctx, entity);
            break;
        case 0x8D:
            result = monsterBattleOpcode_ifByteVarsAnd(ctx, entity);
            break;
        case 0x8E:
            result = monsterBattleOpcode_ifWordVarsAnd(ctx, entity);
            break;
        case 0x8F:
            result = monsterBattleOpcode_ifByteVarNotEqual(ctx, entity);
            break;
        case 0x90:
            result = monsterBattleOpcode_90(ctx, entity);
            break;
        case 0x91:
            result = monsterBattleOpcode_ifByteVarsNotEqual(ctx, entity);
            break;
        case 0x92:
            result = monsterBattleOpcode_ifWordVarsNotEqual(ctx, entity);
            break;
        case 0x93:
            result = monsterBattleOpcode_ifDwordVarsEqual(ctx, entity);
            break;
        case 0x94:
            result = monsterBattleOpcode_ifDwordVarsLE(ctx, entity);
            break;
        case 0x95:
            result = monsterBattleOpcode_ifEntityKO(ctx);
            break;
        case 0x96:
            result = monsterBattleOpcode_ifSlotLayoutEmpty(ctx);
            break;
        case 0x97:
            result = monsterBattleOpcode_ifFirstTwoPCsDown();
            break;
        case 0x98:
            result = monsterBattleOpcode_ifAllEnemiesHidden();
            break;
        case 0x9B:
            result = monsterBattleOpcode_ifSelfHidden(entity);
            break;
        }
        advanceBattleScript(ctx);
    } while (exitScript && (accum |= result, *ctx->m0_scriptPtr >= 0x80));
    if (exitScript) {
        result = accum;
    }
    return result;
}

void freeBillyBattleStructs(u8 entity) {
    if (battleCharacters[entity] == 4 && battleVar1->m8E_isBillyWindowOpen != 0) {
        clearBattleDialogWindow(0);
        freeBattleVar0_A230();
        battleVar1->m8E_isBillyWindowOpen = 0;
    }
}

void setAPMarksPositions(u8 maxAP, u8 remainingAP);
extern u8 apMarkOffsetsY[9][6] PORT_ALIAS; /* rows by max AP; the last runs into the next table */
void setAPMarksPositions(u8 maxAP, u8 remainingAP) {
    s32 i;

    if (maxAP == remainingAP) {
        remainingAP--;
    }
    for (i = 0; i < maxAP - 1; i++) {
        (battleVar0->m908_apMarks + (i * 2 + D_800CCB04.m30_battleOddOrEven))->x0 = 0xC;
        (battleVar0->m908_apMarks + (i * 2 + D_800CCB04.m30_battleOddOrEven))->y0 = apMarkOffsetsY[maxAP][i] + 0x5E;
        (battleVar0->m908_apMarks + (i * 2 + D_800CCB04.m30_battleOddOrEven))->x1 = 0x12;
        (battleVar0->m908_apMarks + (i * 2 + D_800CCB04.m30_battleOddOrEven))->y1 = apMarkOffsetsY[maxAP][i] + 0x5E;
    }
    battleVar1->m97_apMarkCount = remainingAP;
    battleVar1->m98_apMarksOddOrEven = D_800CCB04.m30_battleOddOrEven;
}

void freeBattleVar1_AE(void) {
    if (battleVar1->mAE_isChiMenuDataLoaded) {
        freeHeap(chiData);
        battleVar1->mAE_isChiMenuDataLoaded = 0;
    }
}

void drawCircleMenuChi_loadData(u8 entity) {
    s32 fileIndex;
    if (battleVar1->mAE_isChiMenuDataLoaded == 0) {
        idleBattleDuringLoading();
        fileIndex = 2;
        if (apConfigArray[entity].m1_gearState == 0) {
            fileIndex = 1;
        }
        setFolderChiData();
        chiData = allocateBattleHeap(getFileSizeAligned(fileIndex), 0);
        readFile(fileIndex, chiData, 0, 0x80);
        idleBattleDuringLoading();
        battleVar1->mAE_isChiMenuDataLoaded = 1;
    }
}

void freeBattleVar1_96(void) {
    if (battleVar1->m96_isGearCommandTextLoaded) {
        freeHeap(gearCommandStrings);
        battleVar1->m96_isGearCommandTextLoaded = 0;
    }
}

void loadGearCommandText(u8 entity) { /* the caller passes the entity; unused */
    if (battleVar1->m96_isGearCommandTextLoaded == 0) {
        idleBattleDuringLoading();
        setFolderChiData();
        gearCommandStrings = allocateBattleHeap(getFileSizeAligned(3), 0);
        readFile(3, gearCommandStrings, 0, 0x80);
        idleBattleDuringLoading();
        battleVar1->m96_isGearCommandTextLoaded = 1;
    }
}

void initBattleRenderStruct2(void) {
    pBattleRenderStruct2 = allocateBattleHeap(sizeof(sBattleRenderStruct2), 0);
    bzero(pBattleRenderStruct2, sizeof(sBattleRenderStruct2));
    pBattleRenderStruct2->m5D9C_markerX = 0xA0;
    pBattleRenderStruct2->m5D9E_markerY = 100;
}

void setupGearTurnHud(u8 entity) {
    s32 i;
    s32 n;
    u8 v;
    u8 d;
    s32 off;

    setupGearTurnState(entity);
    i = 2;
    off = entity * 64;
    for (; i < 5; i++) {
        v = battleEntities.m5F24_gearTurn.m0_fuelCosts[i - 2]; /* D_800D2C08 + i * 2, past the 8-byte m5F20 */
        ((u8*)battleVar2 + off)[8 + i * 4 + 0] = 0xFF;
        ((u8*)battleVar2 + off)[8 + i * 4 + 1] = 0xFF;
        ((u8*)battleVar2 + off)[8 + i * 4 + 2] = 0xFF;
        n = 0;
        d = v / 100;
        if (d != 0) {
            v -= d * 100;
            n = 1;
            ((u8*)battleVar2 + off)[8 + i * 4 + 0] = d + 0x83;
        }
        d = v / 10;
        if (d != 0 || n != 0) {
            v -= d * 10;
            ((u8*)battleVar2 + off)[8 + i * 4 + n++] = d + 0x83;
        }
        ((u8*)battleVar2 + off)[8 + i * 4 + n] = v + 0x83;
    }
    initBattleRenderStruct2();
    if (battleCharacters[entity] != 7) {
        setupGearHudTexts(entity);
        battleRenderDebugAndMain();
        updateGearHudFuel(entity);
        battleRenderDebugAndMain();
        battleVar1->mAD_drawGearHud = 1;
        battleVar1->mC7_drawGearFuel = 1;
    }
}

void freeBattleRenderStruct2(u8 entity) {
    battleVar1->mAD_drawGearHud = 0;
    battleVar1->mC7_drawGearFuel = 0;
    battleVar1->mA8_drawComboInput = 0;
    if (apConfigArray[entity].m1_gearState == 2) {
        apConfigArray[entity].m1_gearState = 1;
    }
    freeHeap(pBattleRenderStruct2);
}

void setupTurnRenderLoop(u8 entity) {
    s32 i;
    u8 hasItem;
    u8 target;

    battleVar2->m2EA_notDefending = 1;
    battleVar2->m2E9_targetConfirmed = 0;
    battleVar2->m2DE_actionDone = 0;
    battleVar2->m2E0_isBattleMenuDisabled = 0;
    battleVar2->m2E1_waitBeforeNextAttackInput = 0;
    battleVar2->m2E2_previousActiveBattleMenu = 0xFF;
    battleVar2->m2E3_hasAttacked = 0;
    battleVar2->m2E4_deathblowCompleted = 0;
    battleVar2->m2DC_attackIndex = 0;
    battleVar2->m2D4_remainingAP = battleVar2->m2D5_maxAP = apConfigArray[entity].m4_currentAP;
    battleVar1->m90_perPCTimeBarStatus[entity] = 3;
    battleVar2->m2E7_gearAttackCount = 0;
    newBattleInputButton2 = 0xFF;
    battleBssBlock.mA79 = 0;
    while (battleVar1->m90_perPCTimeBarStatus[entity] != 1) {
        battleRenderDebugAndMain();
    }
    startCharacterJumpToEnemyVar0 = 1;
    battleSoundEffect2(0x5A);
    startCharacterJumpToEnemyVar0 = 0;
    for (i = 0; i < 16; i++) {
        battleVar2->m0_entityMenus[entity].m1C_isCommandEnabled[i] = battleEntities.m0_entities[entity].m0_base.m7A_commandEnabledBF & party1C_InitialValues[i];
    }
    if (battleCharacters[entity] == 7 && apConfigArray[entity].m1_gearState != 0) {
        battleVar2->m0_entityMenus[entity].m1C_isCommandEnabled[13] = party1C_InitialValues[13];
        battleVar2->m0_entityMenus[entity].m1C_isCommandEnabled[4] = party1C_InitialValues[4];
    }
    if (battleCharacters[entity] == 4) {
        if (apConfigArray[entity].m1_gearState == 0) {
            if (gameStateAmmoByWeaponItem[gameState.m26C_party[4].m6F_equipment[0]] == 0) {
                battleVar2->m0_entityMenus[entity].m1C_isCommandEnabled[0] = party1C_InitialValues[0];
            }
            if (gameStateAmmoByWeaponItem[gameState.m26C_party[4].m6F_equipment[3]] == 0) {
                battleVar2->m0_entityMenus[entity].m1C_isCommandEnabled[2] = party1C_InitialValues[2];
            }
        } else {
            if (gameStateAmmoByGearItem[gameState.m978_gears[gameState.m26C_party[4].mA0_gearNum].m4_slotItem[0]] == 0) {
                battleVar2->m0_entityMenus[entity].m1C_isCommandEnabled[0] = party1C_InitialValues[0];
            }
            if (gameStateAmmoByGearItem[gameState.m978_gears[gameState.m26C_party[battleCharacters[entity]].mA0_gearNum].m4_slotItem[3]] == 0) {
                battleVar2->m0_entityMenus[entity].m1C_isCommandEnabled[2] = party1C_InitialValues[2];
            }
        }
    }
    if (apConfigArray[entity].m1_gearState == 0) {
        if (battleVar2->m0_entityMenus[entity].m1C_isCommandEnabled[9] == 0) {
            battleVar2->m2DD_currentActiveBattleMenu = 1;
            previousNewBattleInputButton[0] = 0;
        } else {
            battleVar2->m2DD_currentActiveBattleMenu = 4;
            previousNewBattleInputButton[0] = 3;
        }
        if (battleEntities.m0_entities[entity].m0_base.m7C_statusFlags[0] & 2) {
            battleVar2->m2DD_currentActiveBattleMenu = 2;
            previousNewBattleInputButton[0] = 1;
        }
        initBattleRenderStruct2();
    } else {
        if (battleVar2->m0_entityMenus[entity].m1C_isCommandEnabled[9] == 0) {
            battleVar2->m2DD_currentActiveBattleMenu = 0x10;
            previousNewBattleInputButton[0] = 0;
        } else {
            battleVar2->m2DD_currentActiveBattleMenu = 0x13;
            previousNewBattleInputButton[0] = 3;
        }
        setupGearTurnHud(entity);
    }
    resetBattleVar2_2CC();
    clearEntityFlag15A_1(entity);
    setCameraVisibleEntities(characterIdToTargetBitmask(entity) | characterIdToTargetBitmask(battleVar2->m0_entityMenus[entity].m3C_currentTarget));
    target = battleVar2->m0_entityMenus[entity].m3C_currentTarget;
    if ((u8)battleVar2->m0_entityMenus[entity].m3C_currentTarget == 0xFF) {
        target = 0;
    }
    updateCharacterBlinkingTask(characterIdToTargetBitmask(target));
    while (battleVar2->m2DE_actionDone == 0 && battleRunningVar1 == 0 && battleVar2->m2DB_turnEnded == 0) {
        if (apConfigArray[entity].m1_gearState == 0) {
            setAPMarksPositions(battleVar2->m2D5_maxAP, battleVar2->m2D4_remainingAP);
        }
        if (battleVar2->m2E2_previousActiveBattleMenu != battleVar2->m2DD_currentActiveBattleMenu) {
            battleVar2->m2F6_doublePressPending = 0;
            battleVar2->m2E2_previousActiveBattleMenu = battleVar2->m2DD_currentActiveBattleMenu;
            battleVar1->mCB_drawCommandRing = 0;
            battleVar1->mA3_641COddOrEven = updateBattleMenuSpriteForMenu(entity, battleVar2->m2DD_currentActiveBattleMenu, battleVar2->m2E0_isBattleMenuDisabled);
            battleVar1->mCB_drawCommandRing = 1;
        }
        if (battleInputButton == 0xFF) {
            break;
        }
        battleRenderDebugAndMain();
        if (battleVar2->m2DB_turnEnded != 0) {
            break;
        }
        if (battleVar2->m2E0_isBattleMenuDisabled == 0) {
            startCharacterJumpToEnemyVar0 = 1;
        }
        switch (battleVar2->m2DD_currentActiveBattleMenu) {
        case 1:
            drawCircleMenuAttackActive(entity);
            break;
        case 2:
            drawCircleMenuItemActive(entity);
            break;
        case 3:
            drawCircleMenuDefend(entity);
            break;
        case 4:
            drawCircleMenuChi(entity);
            break;
        case 100:
            battleVar2->m2E2_previousActiveBattleMenu = 0xFF;
        case 5:
            handleMenuSelectEnemy(entity);
            break;
        case 7:
            drawCircleMenuCombo(entity);
            break;
        case 8:
            drawCircleMenuItemSecondaryActive(entity);
            break;
        case 9:
            drawBattleMenuEscape(entity);
            break;
        case 10:
            drawCircleMenuCallGear(entity);
            break;
        case 16:
            drawGearCircleMenuAttackActive(entity);
            break;
        case 17:
            drawGearCircleMenuItemActive(entity);
            break;
        case 18:
            drawGearCircleMenuCharge(entity);
            break;
        case 19:
            drawGearCircleMenuChi(entity);
            break;
        case 21:
            drawGearCircleMenuBooster(entity);
            break;
        case 22:
            drawGearCircleMenuItemSecondaryActive(entity);
            break;
        case 23:
            drawGearBattleMenuEscape(entity);
            break;
        case 24:
            drawGearCircleMenuSpecial(entity);
            break;
        case 101:
            battleVar2->m2E2_previousActiveBattleMenu = 0xFF;
        case 25:
            handleGearMenuSelectEnemy(entity);
            break;
        }
    }
    battleVar1->mCB_drawCommandRing = 0;
    battleVar1->mAF_drawApCounter = 0;
    battleVar1->m90_perPCTimeBarStatus[entity] = 0;
    battleVar1->m7F_drawTimeBarFill[entity] = 0;
    if (battleVar2->m2E3_hasAttacked) {
        apConfigArray[entity].m0_storedAP += battleVar2->m2D4_remainingAP;
        if (apConfigArray[entity].m0_storedAP > 0x1C) {
            apConfigArray[entity].m0_storedAP = 0x1C;
        }
    }
    startCharacterJumpToEnemyVar0 = 0;
    stopBattleVibration(entity, 0x100);
    if (battleVar2->m2DE_actionDone != 0 || battleRunningVar1 != 0) {
        freeBattleVar1_AE();
        freeBattleVar1_96();
        freeBattleRenderStruct2(entity);
        freeBillyBattleStructs(entity);
    }
    battleSlotStatusVar0.m16_numTicksBeforeReady[entity] = battleSlotStatusVar0.m0_battleSlotStatusVar0[entity];
    battleVar1->m7C_hpDisplayDirty[entity] = 1;
    battleBssBlock.mA78_startPlayingAttackAnimationVar0 = 0;
}
