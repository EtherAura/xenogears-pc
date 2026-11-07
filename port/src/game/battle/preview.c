/* Native battle startup for an owned extras world. No live-game pointers. */
#include "common.h"
#include "psyq.h"
#include "battle/battle.h"
#include "main/game_state.h"
#include "main/filesystem.h"
#include "main/memory.h"

extern u8 battleConfigs[];
extern u8 battleMusic[];
extern u8 battleInitVar0, battleInitVar1, battleTransitionEffect;
extern u16 requestedBattleConfig;
extern s32 runningOnDTL;
extern u8 autoPartyMemberTurnActive;
extern s16 fieldDrawEnvsInitialized;
extern u16 abilityAnimationTable0[], abilityAnimationTable1[];
extern s32 battlePartyFileMapping[][2];
extern s8 battleDebugSelectorGearMapping[16], battleDebugSelectorOmniGearMapping[16];
extern u8 drawBattleMode1Disabled;
extern s32 startJumpAnimationVar2, allocateJumpAnimationStructVar0;
extern sJumpAnimationControlStruct *jumpAnimationControlStruct;
static s32 previewEnemyId, previewEnemyGear;
void battlePreviewEnemy(s32 id, s32 gear) {
    previewEnemyId = id;
    previewEnemyGear = gear;
}
static s32 previewLoading, previewOwned, previewActor, previewFull, previewAbility = -1;
static u8 previewVisibility[32];
static void (*previewCapture)(void *, s32, s32);
static void *previewCaptureData;
void battlePreviewCapture(void (*capture)(void *, s32, s32), void *data) {
    previewCapture = capture;
    previewCaptureData = data;
}
void battlePreviewBeginFrame(void) {
    s32 i;
    if (previewOwned && !previewFull)
        for (i = 0; i < 32; i++)
            if (i != previewActor && battleMechas[i])
                battleMechas[i]->m34_visible = 0;
    if (previewCapture)
        previewCapture(previewCaptureData, 0, 0);
}
void battlePreviewEndFrame(void) {
    if (previewCapture)
        previewCapture(previewCaptureData, 1, battleBssBlock.m8C50_currentRenderStruct->m0_drawEnv.ofs[1]);
}
void mapsStartKernel(void);
void initGraphicsForBattle(void);
void battleMain(void);
void battleRender(void);
void spriteActorSetPlayingAnimation(SpriteActorCore *, s32);
void spriteActorSetCallback(SpriteActorCore *, void (*)(SpriteActorCore *));
void initAnimSeqFromCharacterToCharacter(s32, s32);
s32 initJumpData(s32, s32);
s32 computeFacingForJump(s32);
void performMechaPlayAnimation(s32, s32, s32);
void setCameraVisibleEntities(s32);
void executeBattleCode(u8);
void finishMonsterTurn(u8);
void battleTickMainSub0(void);
void performAttackSub2(void);
extern u8 battleTickMain_var0;
extern u16 entitiesHitInCurrentAttackBF[];

s32 battlePreviewAbilityLimit(s32 value) {
    if (previewOwned && previewAbility >= 0) {
        if (previewActor >= 3)
            return 0;
        return abilityAnimationTable1[battleBssBlock.m4_visualEntities[previewActor].m2_characterId];
    }
    return value;
}
s32 battlePreviewAbility(s32 value) {
    return previewOwned && previewAbility >= 0 ? previewAbility : value;
}
s32 battlePreviewHide(SpriteActorCore *core) {
    return previewOwned && !previewFull && core != battleBssBlock.m8C8C_spriteActorCores[previewActor];
}
s32 battlePreviewOwned(void) {
    return previewOwned;
}

s32 battlePreviewLoading(void) {
    return previewLoading;
}
s32 battlePreviewStart(s32 character, s32 arena, s32 enemySet, s32 gear) {
    void *data;
    s32 i;
    mapsStartKernel();
    runningOnDTL = -1;
    gameState.m1D30_partyMemberBitField = gameState.m1D32_partyFrameMask = 0x7FF;
    gameState.m1D34_currentParty[0] = character;
    gameState.m1D34_currentParty[1] = gameState.m1D34_currentParty[2] = 0xFF;
    for (i = 0; i < 11; i++) {
        gameState.m22B1_isOnGear[i] = 0;
        gameState.m26C_party[i].m4C_HP = gameState.m26C_party[i].m4E_MaxHP = 999;
        gameState.m26C_party[i].m50_MP = gameState.m26C_party[i].m52_MaxMP = 99;
    }
    setCurrentDirectory(0x20, 3);
    data = allocateHeap(getFileSizeAligned(7), 0);
    if (!data)
        return 0;
    readFile(7, data, 0, 0);
    memcpy(battleConfigs, data, 0x200);
    freeHeap(data);
    battleConfigs[0] = enemySet;
    battleConfigs[2] = arena;
    battleConfigs[1] = battleConfigs[3] = 0;
    battleConfigs[4] = 0;
    for (i = 8; i < 16; i++)
        battleConfigs[i] = 0x7F;
    battleConfigs[8] = previewEnemyId | (previewEnemyGear ? 0x80 : 0);
    battleConfigs[16] = 0;
    battleConfigs[24] = 0;
    requestedBattleConfig = 0;
    setCurrentDirectory(0x20, 2);
    data = allocateHeap(getFileSizeAligned(4), 0);
    if (!data)
        return 0;
    readFile(4, data, 0, 0);
    memcpy(battleMusic, data, getFileSizeAligned(4));
    freeHeap(data);
    battleInitVar0 = battleInitVar1 = 1;
    battleTransitionEffect = 1;
    fieldDrawEnvsInitialized = 1;
    initGraphicsForBattle();
    gameState.m22B1_isOnGear[0] = gear != 0;
    if (gear == 1)
        gameState.m26C_party[character].mA0_gearNum = battleDebugSelectorGearMapping[character];
    if (gear > 1) {
        gameState.m26C_party[character].mA0_gearNum = battleDebugSelectorOmniGearMapping[character];
    }
    /* The party Gear stands through any turn, as the characters' 999 HP do
     * on foot. At the default state's 1800 HP a boss's Ether can knock it
     * out while its model is unloaded for the effect (set 31 type 1, Aqua
     * Dhaum: battleMechas[0] is NULL through the turn); reloaded at the end
     * (finalizeBattleMechaLoadingCommands sets the sync flag) it comes back
     * into the knocked-out program, which never clears the flag that
     * clearAnimationsEndOfTurnSub waits on (xg-htn.35.99). 30000 HP is a
     * late-game frame's level. */
    if (gear && gameState.m26C_party[character].mA0_gearNum < 19) {
        GameStateGear *g = &gameState.m978_gears[gameState.m26C_party[character].mA0_gearNum];
        g->m60_hp = g->m64_maxHp = 30000;
    }
    previewLoading = 1;
    battleMain();
    previewLoading = 0;
    for (i = 0; i < 32; i++)
        previewVisibility[i] = battleMechas[i] ? battleMechas[i]->m34_visible : 0;
    previewOwned = 1;
    previewActor = previewFull = 0;
    drawBattleMode1Disabled = 1;
    return 1;
}
s32 battlePreviewInfo(s32 slot, s32 *out) {
    SpriteActorCore *core = battleBssBlock.m8C8C_spriteActorCores[slot];
    AnimationBundle *bundle;
    sLoadedMechas *model;
    if (!core)
        return 0;
    out[0] = battleBssBlock.m4_visualEntities[slot].m2_characterId;
    out[1] = battleBssBlock.m4_visualEntities[slot].m4_isGear;
    bundle = core->m48_defaultAnimationBundle;
    out[2] = bundle ? *(u16 *)((u8 *)bundle + bundle->m4_animationsOffset) & 63 : 0;
    model = battleMechas[slot];
    /* a Gear's programs: the table initMechaTransforms1_battle keeps (an
     * enemy Gear has no mAC_animData), inside its sMechaDataTable2_4 */
    if (out[1] && model && model->m8_bytecodeTable)
        out[2] = ((sMechaDataTable2_4 *)((u8 *)model->m8_bytecodeTable - offsetof(sMechaDataTable2_4, m8_bytecode)))
                     ->m0_count -
                 1;
    out[3] = slot < 3 && out[0] < 12 ? battlePartyFileMapping[out[0]][1] : 0;
    out[4] = slot < 3 && out[0] < 12 ? abilityAnimationTable0[out[0]] : -1;
    out[5] = slot < 3 && out[0] < 12 ? abilityAnimationTable1[out[0]] : -1;
    out[6] = slot < 3 ? battleEntities.m0_entities[slot].m0_base.m56_battleCommandLoadout : -1;
    out[7] = slot < 3 ? battleEntities.m0_entities[slot].m0_base.mA0_gearNum : -1;
    return 1;
}

/* The command entries the loader copied for a party slot (bld_801E4048.c):
 * on foot its 38, on a Gear the Gear's 41 (the 42nd is past the file). */
s32 battlePreviewCommands(s32 slot, s32 *types, s32 *flags) {
    sBattle800CDD40Sub *c;
    s32 i, n;
    if (slot < 0 || slot >= 3 || !battleBssBlock.m8C8C_spriteActorCores[slot])
        return 0;
    if (battleBssBlock.m4_visualEntities[slot].m4_isGear) {
        c = battleEntities.m2228_mechaBattleStats[slot];
        n = 41;
    } else {
        c = battleEntities.m1058_partyBattleStats[slot];
        n = 38;
    }
    for (i = 0; i < n; i++) {
        types[i] = c[i].m2_attackType;
        flags[i] = (u16)c[i].mA_attackFlags;
    }
    return n;
}

s32 battlePreviewPresentation(s32 slot, s32 full) {
    s32 i;
    if (!battleBssBlock.m8C8C_spriteActorCores[slot])
        return 0;
    previewActor = slot;
    previewFull = full;
    if (full)
        for (i = 0; i < 32; i++)
            if (battleMechas[i])
                battleMechas[i]->m34_visible = previewVisibility[i];
    drawBattleMode1Disabled = !full;
    return 1;
}

/* Kind 5: command entry `id` of a party slot as startBattleAttackAnimation
 * picks its animation (b_800941A4.c: on foot the entry's attack type when
 * its flags have bit 0, else the entry index; a Gear's attack type), and
 * whether the turn walks to the target: attacks and deathblows do
 * (startCharacterJumpToEnemy), Ethers play in place (executeAbility), and
 * Billy always shoots from his slot (initJumpData). -1: no such entry. */
static s32 previewCommand(s32 slot, s32 id, s32 *inPlace) {
    sBattle800CDD40Sub *c;
    if (slot >= 3)
        return -1;
    if (battleBssBlock.m4_visualEntities[slot].m4_isGear) {
        s32 info[8];
        if (id >= 41 || !battlePreviewInfo(slot, info))
            return -1;
        c = &battleEntities.m2228_mechaBattleStats[slot][id];
        *inPlace = id >= 21;
        /* a program this Gear has (another Gear's special: none) */
        if ((c->m2_attackType & 0xFF) >= info[2])
            return -1;
        return c->m2_attackType & 0xFF;
    }
    if (id >= 38)
        return -1;
    c = &battleEntities.m1058_partyBattleStats[slot][id];
    *inPlace = id >= 22;
    return (c->mA_attackFlags & 1 ? c->m2_attackType : id) & 0xFF;
}

/* Kind 6: an enemy's turn, the battleScriptContext actions its scripts
 * queue (port/src/extras/battle_moves.c), run as monsterJumpToTarget runs
 * them (b_80079674.c): in place, executeBattleCode builds the animation
 * entries (names, approach, attacks with their damage, abilities, waits)
 * and finishMonsterTurn ends it (a Gear's 0x1B, then the return). */
s32 battlePreviewTurn(s32 slot, s32 target, const u8 *actions, s32 count, s32 full) {
    SpriteActorCore *core = battleBssBlock.m8C8C_spriteActorCores[slot];
    s32 i, n;
    if (!core || !battleBssBlock.m8C8C_spriteActorCores[target] || slot < 3 || count <= 0 || count > 31)
        return 0;
    battlePreviewPresentation(slot, full);
    previewAbility = -1;
    core->m74_targetEntitySprite = battleBssBlock.m8C8C_spriteActorCores[target];
    battleVar2->m2D3_currentEntityTurn = slot;
    battleVar2->m0_entityMenus[slot].m3C_currentTarget = target;
    battleVar2->m2DA_indexInBattleVar48 = 0;
    battleVar2->m2DB_turnEnded = 0;
    battleTickMain_var0 = 1;
    entitiesHitInCurrentAttackBF[0] = 0;
    memset(battleScriptContext, 0, sizeof(battleScriptContext));
    for (i = n = 0; i < count; i++) {
        /* actor-only: the enemy stays on its mark (no moveAndAttack) */
        if (!full && actions[i * 8] == 2)
            continue;
        memcpy(&battleScriptContext[n++], actions + i * 8, 8);
    }
    for (i = 0; i < 32; i++)
        battleBssBlock.m138_currentDamages[i].m47_battleAnimationToPlay = 0xFF;
    battleTickMainSub0();
    performAttackSub2();
    battleBssBlock.m138_currentDamages[0].m23_battleEntityIndex = slot;
    startJumpAnimation(1, slot, target, computeFacingForJump(slot));
    executeBattleCode(slot);
    finishMonsterTurn(slot);
    return 1;
}

s32 battlePreviewPlay(s32 slot, s32 target, s32 kind, s32 id, s32 program, s32 full) {
    SpriteActorCore *core = battleBssBlock.m8C8C_spriteActorCores[slot];
    s32 anim = id, inPlace = 0;
    if (!core || !battleBssBlock.m8C8C_spriteActorCores[target])
        return 0;
    if (kind == 5 && (anim = previewCommand(slot, id, &inPlace)) < 0)
        return 0;
    battlePreviewPresentation(slot, full);
    previewAbility = kind == 2 ? id : -1;
    core->m74_targetEntitySprite = battleBssBlock.m8C8C_spriteActorCores[target];
    if (kind == 0) {
        if (battleBssBlock.m4_visualEntities[slot].m4_isGear)
            performMechaPlayAnimation(slot, 1 << target, id);
        else {
            spriteActorSetCallback(core, NULL);
            spriteActorSetPlayingAnimation(core, id);
        }
        return 1;
    }
    autoPartyMemberTurnActive = 1; /* automatic turn: no interactive menu allocations */
    battleVar2->m2D3_currentEntityTurn = slot;
    battleVar2->m0_entityMenus[slot].m3C_currentTarget = target;
    battleVar2->m2DA_indexInBattleVar48 = 0;
    initAnimSeqFromCharacterToCharacter(slot, target);
    {
        s32 entry, entity;
        for (entry = 0; entry < 32; entry++)
            for (entity = 0; entity < 11; entity++) {
                battleBssBlock.m138_currentDamages[entry].m18_damageType[entity] = 0xFF;
                battleBssBlock.m138_currentDamages[entry].m3C_totalDamageColors[entity] = 0xFF;
            }
        battleBssBlock.m138_currentDamages[0].m18_damageType[target] = 0;
        battleBssBlock.m138_currentDamages[0].m0_damageValue[target] = 10;
        battleBssBlock.m138_currentDamages[0].m24_totalDamages[target] = 10;
        battleBssBlock.m138_currentDamages[0].m3C_totalDamageColors[target] = 0;
    }
    /* initJumpData: 1 for Billy, who never leaves his slot */
    if (initJumpData(slot, target) && slot < 3)
        inPlace = 1;
    battleBssBlock.m138_currentDamages[0].m16_targetBitMask = 1 << target;
    battleBssBlock.m138_currentDamages[0].m47_battleAnimationToPlay = kind == 2 ? 0x10 : anim;
    battleBssBlock.m138_currentDamages[1].m47_battleAnimationToPlay = 0xFE;
    startJumpAnimation(kind == 2 || inPlace || !full, slot, target, computeFacingForJump(slot));
    startJumpAnimationVar2 = kind == 2 ? program : 0;
    return 1;
}
/* A move's sequence is still running: the jump animation's controller
 * lives until deleteJumpAnimationControlStructEndOfAttack (b_800B7C28.c). */
s32 battlePreviewActive(void) {
    return jumpAnimationControlStruct != NULL;
}

s32 battlePreviewFrame(void) {
    if (!previewFull)
        setCameraVisibleEntities(1 << previewActor);
    battleRender();
    return battleBssBlock.m8C50_currentRenderStruct->m0_drawEnv.ofs[1];
}

void battlePreviewDump(void) {
    SpriteActorCore *c = battleBssBlock.m8C8C_spriteActorCores[previewActor];
    printf("battle state: actor %d pos %d %d %d step %d %d %d anim %d controller %d index %d camera %d\n",
           previewActor, c->m0_position.vx >> 16, c->m0_position.vy >> 16, c->m0_position.vz >> 16,
           c->mC_step.vx, c->mC_step.vy, c->mC_step.vz, (s8)c->mAF_animationId,
           jumpAnimationControlStruct ? jumpAnimationControlStruct->m1C_mode : -1,
           allocateJumpAnimationStructVar0, battleCameraBlock.m1C_distance);
    /* the Gears: the program each runs (the table entry it is in) and the
     * sync flag clearAnimationsEndOfTurnSub waits on */
    {
        s32 i, k, best;
        s32 info[8];
        for (i = 0; i < 11; i++) {
            sLoadedMechas *m = battleMechas[i];
            if (!m || !m->m8_bytecodeTable) {
                if (i < 4 && battleBssBlock.m4_visualEntities[i].m4_isGear)
                    printf("battle gear: slot %d %s\n", i, m ? "has no programs" : "has no model");
                continue;
            }
            if (!battlePreviewInfo(i, info))
                info[2] = 0;
            best = -1;
            for (k = 0; k < info[2]; k++)
                if (m->m8_bytecodeTable[k] && m->m8_bytecodeTable[k] <= m->m10_bytecode0 &&
                    (best < 0 || m->m8_bytecodeTable[k] > m->m8_bytecodeTable[best]))
                    best = k;
            printf("battle gear: slot %d sync %d visible %d program %d +%d op %04X wait %d loop %d hp %d/%d\n", i,
                   m->m38_syncFlag, m->m34_visible, best,
                   best >= 0 ? (s32)((u8 *)m->m10_bytecode0 - (u8 *)m->m8_bytecodeTable[best]) : -1,
                   m->m10_bytecode0 ? *(u16 *)m->m10_bytecode0 : 0xFFFF, m->m40_waitTimer, m->m42_loopCount,
                   battleEntities.m0_entities[i].mA4_gear.m60_hp, battleEntities.m0_entities[i].mA4_gear.m64_maxHp);
        }
    }
}
