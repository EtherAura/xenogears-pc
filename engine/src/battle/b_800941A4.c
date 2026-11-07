/*
 * Attack resolution: startBattleAttackAnimation and the damage / status
 * computation (Noah battle.cpp). 0x800941A4-0x8009E268; gcc-2.6.3-psx,
 * ASPSX 2.34.
 */
#include "common.h"
#include "psyq.h"
#include "main/game_state.h"
#include "battle/battle.h"
#include "pc_gameplay.h"

/* Battle data block at 0x800CCCE8; retail reaches it both through the
 * initialised pointer pBattleEntities (= &battleEntities) and directly. */
extern BattleBss_800CCCE8* pBattleEntities;

/* startBattleAttackAnimation state */
extern sBattle800CDD40Sub* currentEntityBattleStats;
extern sBattleEntity* battleGetSlotStatusSub_currentBattleEntity;
extern GameStateGear* battleGetSlotStatusSub_currentBattleEntityGear;
extern u8 startBattleAttackAnimationVar0;
extern u8 battlePartyMemberCount;
extern u8 startBattleAttackAnimationVar1;
extern u8 startBattleAttackAnimationVar2;                      /* Noah: startBattleAttackAnimationVar2 (attacker) */
extern u8 startBattleAttackAnimationVar3;
extern u8 startBattleAttackAnimationVar4;                      /* Noah: startBattleAttackAnimationVar4 (target) */
extern sBattleEntity* startBattleAttackAnimationVar5;
extern GameStateGear* startBattleAttackAnimationVar6;
extern u8* startBattleAttackAnimationVar7;                     /* Noah: startBattleAttackAnimationVar7 (target entity + 0x148) */
extern void (*startBattleAttackAnimationVar8[8])(void);

/* Plain-symbol views of members of the 0x800CCCE8 block */
extern sBattle800D2204 D_800D2204[];
extern s32 damageDonePerAttack[11] PORT_ALIAS;
extern u8 initBattleAttackStatusArray0[11];
extern u16 entitiesHitInCurrentAttackBF;
extern u16 performAttack_type;
#define BATTLE_EFFECT_FLAGS battleEffectBF.m0_flags
void updateGearEntityStatus(GameStateGear* gear, GameStateCharacter* base, u8* timers);
s8 startBattleAttackAnimationVar8_2Sub0(u8 chance, u8 type, u16 mask);
void startBattleAttackAnimationVar8_2Sub1(u8 target, u8 type, u16 mask, u8 amount);
void applyBattleItemToEntity(u8 target, u8 arg1);
/* ---- BR3 declarations ---- */
void transferCharStatsBackToField(void);
s8 rollGearStatusEffect(u8 arg0);
s16 computeBattleDamageSub1(void);  /* BR2: retail returns sign-extended (lh / sra 16) */
void initGearHpFromCharacterHp(u8 idx, sBattleEntity* e);
s16 computeBattleDamageSub2(void);  /* BR2: retail returns sign-extended (sll/sra 16) */
extern void (*startGearAttackAnimationVar8[])(void);        /* gear counterpart of startBattleAttackAnimationVar8 (startBattleAttackAnimationVar8) */
extern u8* pAttackerGearMode;                     /* attacker entity + 0x148 (cf. startBattleAttackAnimationVar7 for the target) */
void spendBillyGearAmmo(void);
void postComputeGearAttackDone(void);
void selectGearAttackStatsForTarget(void);
void syncGearUntargetableFlag(u8 idx);
void recordLastGearAttackStats(void);
void setGearStatusEffectMessage(u8 type, u16 mask);
void computeBattleDamageSub3(u16* atk, u16* def, s8* hit);
extern u16 partySavedCommandEnabledBF[16];                 /* per-PC default m7A_commandEnabledBF (up to battleEnvBaseColors) */
extern BattleBss_800D2CA4 D_800D2CA4;      /* battleTickMain_var3 / var1 */
u8 canDisableAnotherPartyMember(void);
void setStatusEffectMessage(u8 type, u16 flag);
/* ---- BR2 declarations ---- */
#if VERSION_JP
void startBattleAttackAnimationSub0(u8 attacker); /* JP has no arg1: it always checks currentEntityBattleStats->mA & 0x100 */
#else
void startBattleAttackAnimationSub0(u8 attacker, u8 arg1);
#endif
void applyPinchAndCriticalBonus(u16* damage);
s8 computeBattleDamageSub0(void);
void setEtherAbilitiesTargetAll(u8 idx);
void startGearAttackAnimation(void);
void spendBillyAmmo(void);
void accumulatePartyDamageTaken(void);
void rollEtherAttackMiss(void);
void onPlayerAttackedByEnemy(void);
void postComputeAttachDone(void);
void rollWeaponStatusEffect(void);
void startBattleAttackAnimationVar8_2(void);
void rollStatusClearOnHit(void);
void initBattleAttackStatus(void);
void recordLastAttackStats(void);
s8 computeGearBattleDamageSub0(void);
u16 computeGearBattleDamageSub1(void);
u16 computeGearBattleDamageSub2(void);
s32 applyGearElementResistance(s32 damage);

/* Noah: startBattleAttackAnimation (resolves the current attack on every entity hit) */
u8 startBattleAttackAnimation(void) {
    s32 mask;
    u16 flags;
    u8 i;

    initBattleAttackStatus();
    startBattleAttackAnimationVar0 = 0;
    startBattleAttackAnimationVar1 = 0;
    startBattleAttackAnimationVar2 = pBattleEntities->m5FC1_performAttackSub3_attacker;
    battleGetSlotStatusSub_currentBattleEntity = &pBattleEntities->m0_entities[startBattleAttackAnimationVar2];
    battleGetSlotStatusSub_currentBattleEntityGear = &pBattleEntities->m0_entities[startBattleAttackAnimationVar2].mA4_gear;
    if (pBattleEntities->m0_entities[pBattleEntities->m5FC1_performAttackSub3_attacker].m15A_flags & 0x80) {
        startGearAttackAnimation();
        setEtherAbilitiesTargetAll(startBattleAttackAnimationVar2);
        IMPLICIT_RETURN(0);
    }
    if (startBattleAttackAnimationVar2 < 3) {
        currentEntityBattleStats = &pBattleEntities->m1058_partyBattleStats[startBattleAttackAnimationVar2][pBattleEntities->m5FC2_battleGetSlotStatusSub_current28Index];
    } else {
        currentEntityBattleStats = &pBattleEntities->m35D8_enemiesBattleStats[pBattleEntities->m5FC2_battleGetSlotStatusSub_current28Index];
    }
#if VERSION_JP
    startBattleAttackAnimationSub0(startBattleAttackAnimationVar2);
#else
    startBattleAttackAnimationSub0(startBattleAttackAnimationVar2, 1);
#endif
    if (currentEntityBattleStats->mA_attackFlags & 0x10) {
        startGearAttackAnimation();
        setEtherAbilitiesTargetAll(startBattleAttackAnimationVar2);
        accumulatePartyDamageTaken();
        IMPLICIT_RETURN(0);
    }
    startBattleAttackAnimationVar3 = 0;
    if ((currentEntityBattleStats->mA_attackFlags & 0x100) && battleGetSlotStatusSub_currentBattleEntity->m0_base.m56_battleCommandLoadout == 1) {
        rollEtherAttackMiss();
    }
    if (battleGetSlotStatusSub_currentBattleEntity->m0_base.m56_battleCommandLoadout == 4
        && (u8)(pBattleEntities->m5FC2_battleGetSlotStatusSub_current28Index - 4) < 2) {
        currentEntityBattleStats->m22_elementFlags = battleGetSlotStatusSub_currentBattleEntity->m0_base.m4_weaponSlots[pBattleEntities->m5FC2_battleGetSlotStatusSub_current28Index - 3].m0_weaponPower;
    }
    if ((battleGetSlotStatusSub_currentBattleEntity->m0_base.m7C_statusFlags[2] & 0x20) && (battleEntities.m5FAC_entitiesHitInCurrentAttackBF & 7)
        && battleEntities.m5FAC_entitiesHitInCurrentAttackBF < 7) {
        for (i = 0; i < 3; i++) {
            if (battleEntities.m0_entities[i].m0_base.m56_battleCommandLoadout == 3) {
                battleEntities.m5FAC_entitiesHitInCurrentAttackBF = 1 << i;
            }
        }
    }
    mask = 1;
    for (startBattleAttackAnimationVar4 = 0; startBattleAttackAnimationVar4 < 11; startBattleAttackAnimationVar4++) {
        if (mask & pBattleEntities->m5FAC_entitiesHitInCurrentAttackBF) {
            startBattleAttackAnimationVar5 = &pBattleEntities->m0_entities[startBattleAttackAnimationVar4];
            startBattleAttackAnimationVar6 = &pBattleEntities->m0_entities[startBattleAttackAnimationVar4].mA4_gear;
            startBattleAttackAnimationVar7 = &pBattleEntities->m0_entities[startBattleAttackAnimationVar4].m148_gearMode;
            if (startBattleAttackAnimationVar4 < 3) {
                onPlayerAttackedByEnemy();
            }
            startBattleAttackAnimationVar8[currentEntityBattleStats->m16_effectType]();
            postComputeAttachDone();
            if (pBattleEntities->m5FA0_initBattleAttackStatusArray0[startBattleAttackAnimationVar4] == 0) {
                flags = currentEntityBattleStats->mA_attackFlags;
                if (flags & 0x800) {
                    rollWeaponStatusEffect();
                } else if (flags & 0x4000) {
                    startBattleAttackAnimationVar8_2();
                } else if (flags & 4) {
                    rollStatusClearOnHit();
                }
            }
            if (currentEntityBattleStats->mA_attackFlags & 1) {
                pBattleEntities->m5FB0_performAttack_type = currentEntityBattleStats->m2_attackType;
            } else {
                pBattleEntities->m5FB0_performAttack_type = pBattleEntities->m5FC2_battleGetSlotStatusSub_current28Index;
            }
        }
        mask <<= 1;
    }
    recordLastAttackStats();
    setEtherAbilitiesTargetAll(startBattleAttackAnimationVar2);
    if (battleGetSlotStatusSub_currentBattleEntity->m0_base.m56_battleCommandLoadout == 4 && startBattleAttackAnimationVar1 == 0) {
        spendBillyAmmo();
    }
    if (startBattleAttackAnimationVar2 < 3 && pBattleEntities->m5FC2_battleGetSlotStatusSub_current28Index < 7
        && battleGetSlotStatusSub_currentBattleEntity->m0_base.m90_deathblowProgress[pBattleEntities->m5FC2_battleGetSlotStatusSub_current28Index] < 65000) {
        /* The same practice, scaled by the deathblow learning setting. */
        GameStateCharacter* pcC = &battleGetSlotStatusSub_currentBattleEntity->m0_base;
        u8 pcCombo = pBattleEntities->m5FC2_battleGetSlotStatusSub_current28Index;
        pcC->m90_deathblowProgress[pcCombo] = pcGameplayDeathblowPractice(pcC->m56_battleCommandLoadout, pcCombo,
            pcC->m90_deathblowProgress[pcCombo], pcC->m55_deathblowLearnRate + pcC->mA1_deathblowLearnBonus);
    }
    accumulatePartyDamageTaken();
    return startBattleAttackAnimationVar0;
}

/* Noah: postComputeAttachDone (after the damage roll: counters, status side effects, damage modifiers) */
void postComputeAttachDone(void) {
    s32 chance;
    u16 status;
    u16 count;
    u8 i;

    if (pBattleEntities->m5FA0_initBattleAttackStatusArray0[startBattleAttackAnimationVar4] == 0) {
        if (pBattleEntities->m5F6C_damageDonePerAttack[startBattleAttackAnimationVar4] == startBattleAttackAnimationVar5->m0_base.m4C_HP && startBattleAttackAnimationVar2 < 3) {
            count = ++battleGetSlotStatusSub_currentBattleEntity->m0_base.m3A_killCount;
            if (count > 65000) {
                battleGetSlotStatusSub_currentBattleEntity->m0_base.m3A_killCount = count - 1;
            }
        }
        status = startBattleAttackAnimationVar5->m0_base.m7C_statusFlags[2];
        startBattleAttackAnimationVar5->m0_base.m7C_statusFlags[2] = status & 0xEFFF;
        if ((status & 0x2000) && rand() % 100 < 70) {
            startBattleAttackAnimationVar5->m0_base.m7C_statusFlags[2] &= 0xDFFF;
        }
        if (currentEntityBattleStats->mA_attackFlags & 0x100) {
            if (startBattleAttackAnimationVar5->m0_base.m36_monsterFlags2 & 0x4000) {
                pBattleEntities->m5FA0_initBattleAttackStatusArray0[startBattleAttackAnimationVar4] = 2;
            }
            if ((startBattleAttackAnimationVar5->m0_base.m36_monsterFlags2 & 0x2000) && !(currentEntityBattleStats->m22_elementFlags & 0xF)) {
                pBattleEntities->m5FA0_initBattleAttackStatusArray0[startBattleAttackAnimationVar4] = 2;
            }
        }
        if (startBattleAttackAnimationVar5->m0_base.m32_accessoryFlags & 0x80) {
            chance = 60;
            if (startBattleAttackAnimationVar5->m0_base.m56_battleCommandLoadout == 0) {
                chance = 80;
            }
            if (rand() % 100 < chance) {
                pBattleEntities->m5F6C_damageDonePerAttack[startBattleAttackAnimationVar4] = (u32)pBattleEntities->m5F6C_damageDonePerAttack[startBattleAttackAnimationVar4] >> 1;
            } else {
                pBattleEntities->m5F6C_damageDonePerAttack[startBattleAttackAnimationVar4] = ((u32)pBattleEntities->m5F6C_damageDonePerAttack[startBattleAttackAnimationVar4] >> 1) + pBattleEntities->m5F6C_damageDonePerAttack[startBattleAttackAnimationVar4];
            }
        }
        if (startBattleAttackAnimationVar5->m0_base.m32_accessoryFlags & 0x20) {
            pBattleEntities->m5FA0_initBattleAttackStatusArray0[startBattleAttackAnimationVar2] = 0;
            pBattleEntities->m5F6C_damageDonePerAttack[startBattleAttackAnimationVar2] = pBattleEntities->m5F6C_damageDonePerAttack[startBattleAttackAnimationVar4];
        }
        if (startBattleAttackAnimationVar2 < 3 && startBattleAttackAnimationVar2 != startBattleAttackAnimationVar4) {
            if (pBattleEntities->m0_entities[startBattleAttackAnimationVar4].m0_base.m7C_statusFlags[0] & 2) {
                pBattleEntities->m0_entities[startBattleAttackAnimationVar4].m0_base.m7C_statusFlags[0] &= ~2;
                pBattleEntities->m0_entities[startBattleAttackAnimationVar4].m0_base.m7A_commandEnabledBF = partySavedCommandEnabledBF[startBattleAttackAnimationVar4];
            }
        }
        if (startBattleAttackAnimationVar5->m0_base.m56_battleCommandLoadout == 3 && startBattleAttackAnimationVar5->m0_base.m4C_HP <= (u32)damageDonePerAttack[startBattleAttackAnimationVar4]) {
            for (i = 3; i < 11; i++) {
                battleEntities.m0_entities[i].m0_base.m7C_statusFlags[2] &= ~0x20;
            }
        }
    }
    if (startBattleAttackAnimationVar5->m0_base.m36_monsterFlags2 & 0x8000) {
        switch (pBattleEntities->m5FA0_initBattleAttackStatusArray0[startBattleAttackAnimationVar4]) {
        case 0:
        case 5:
            pBattleEntities->m5FA0_initBattleAttackStatusArray0[startBattleAttackAnimationVar4] = 2;
            break;
        case 2:
            pBattleEntities->m5FA0_initBattleAttackStatusArray0[startBattleAttackAnimationVar4] = 0;
            break;
        }
    }
    if ((startBattleAttackAnimationVar5->m0_base.m84_statusFlags2[1] & 0x80) && pBattleEntities->m5FA0_initBattleAttackStatusArray0[startBattleAttackAnimationVar4] == 2) {
        pBattleEntities->m5F6C_damageDonePerAttack[startBattleAttackAnimationVar4] <<= 1;
    }
    if ((startBattleAttackAnimationVar5->m0_base.m84_statusFlags2[3] & 0x200) && pBattleEntities->m5FA0_initBattleAttackStatusArray0[startBattleAttackAnimationVar4] == 1) {
        pBattleEntities->m5F6C_damageDonePerAttack[startBattleAttackAnimationVar4] = 0;
    }
}

/* adds damageDonePerAttack[target] to the per-PC totals m5F54/m5F60 (gear or not) for PCs hit with status 0 */
void accumulatePartyDamageTaken(void) {
    u8 i;

    for (i = 0; i < 3; i++) {
        if (pBattleEntities->m5FA0_initBattleAttackStatusArray0[i] == 0) {
            if (pBattleEntities->m0_entities[i].m15A_flags & 0x80) {
                pBattleEntities->m5F60_gearDamageTaken[i] += pBattleEntities->m5F6C_damageDonePerAttack[startBattleAttackAnimationVar4];
            } else {
                pBattleEntities->m5F54_damageTaken[i] += pBattleEntities->m5F6C_damageDonePerAttack[startBattleAttackAnimationVar4];
            }
        }
    }
}

/* counts downed PCs (m7C & 0xC000) and PCs with m7C bit 1; for 1+2 or 2+1 clears bit 1 and restores m7A from partySavedCommandEnabledBF */
void updatePlayerWithStatusC000And2(void) {
    u8 downCount;
    u8 i;
    u8 confusedCount;

    downCount = 0;
    for (i = 0; i < 3; i++) {
        if (pBattleEntities->m0_entities[i].m0_base.m7C_statusFlags[0] & 0xC000) {
            downCount++;
        }
    }
    confusedCount = 0;
    for (i = 0; i < 3; i++) {
        if (pBattleEntities->m0_entities[i].m0_base.m7C_statusFlags[0] & 2) {
            confusedCount++;
        }
    }
    if (downCount == 1 && confusedCount == 2) {
        for (i = 0; i < 3; i++) {
            if (pBattleEntities->m0_entities[i].m0_base.m7C_statusFlags[0] & 2) {
                pBattleEntities->m0_entities[i].m0_base.m7C_statusFlags[0] &= ~2;
                pBattleEntities->m0_entities[i].m0_base.m7A_commandEnabledBF = partySavedCommandEnabledBF[i];
            }
        }
    }
    if (downCount == 2 && confusedCount == 1) {
        for (i = 0; i < 3; i++) {
            if (pBattleEntities->m0_entities[i].m0_base.m7C_statusFlags[0] & 2) {
                pBattleEntities->m0_entities[i].m0_base.m7C_statusFlags[0] &= ~2;
                pBattleEntities->m0_entities[i].m0_base.m7A_commandEnabledBF = partySavedCommandEnabledBF[i];
            }
        }
    }
}

/* Noah: startBattleAttackAnimationVar8_0 (physical / ether damage computation) */
void startBattleAttackAnimationVar8_0(void) {
    u16 attack;
    u16 defence;
    s8 result;
    s32 power;
    u16 immune;
    s32 atkMul;
    s32 defMul;
    s32 damage;

    power = currentEntityBattleStats->m11_power;
    if (currentEntityBattleStats->mA_attackFlags & 0x100) {
        immune = startBattleAttackAnimationVar5->m0_base.m34_monsterFlags & 0x4000;
    } else {
        immune = startBattleAttackAnimationVar5->m0_base.m34_monsterFlags & 0x8000;
    }
    if (immune) {
        pBattleEntities->m5FA0_initBattleAttackStatusArray0[startBattleAttackAnimationVar4] = 0;
        return;
    }
    result = computeBattleDamageSub0();
    attack = computeBattleDamageSub1();
    defence = computeBattleDamageSub2();
    if ((battleGetSlotStatusSub_currentBattleEntity->m0_base.m84_statusFlags2[2] | battleGetSlotStatusSub_currentBattleEntity->m0_base.m84_statusFlags2[3]) & 8) {
        attack += attack / 5;
    }
    if ((battleGetSlotStatusSub_currentBattleEntity->m0_base.m84_statusFlags2[2] | battleGetSlotStatusSub_currentBattleEntity->m0_base.m84_statusFlags2[3]) & 2) {
        attack += attack / 10;
    }
    if ((battleGetSlotStatusSub_currentBattleEntity->m0_base.m84_statusFlags2[2] | battleGetSlotStatusSub_currentBattleEntity->m0_base.m84_statusFlags2[3]) & 4) {
        attack -= attack / 5;
    }
    if ((battleGetSlotStatusSub_currentBattleEntity->m0_base.m84_statusFlags2[2] | battleGetSlotStatusSub_currentBattleEntity->m0_base.m84_statusFlags2[3]) & 1) {
        attack -= attack / 10;
    }
    if ((startBattleAttackAnimationVar5->m0_base.m84_statusFlags2[2] | startBattleAttackAnimationVar5->m0_base.m84_statusFlags2[3]) & 4) {
        defence += defence / 5;
    }
    if ((startBattleAttackAnimationVar5->m0_base.m84_statusFlags2[2] | startBattleAttackAnimationVar5->m0_base.m84_statusFlags2[3]) & 1) {
        defence += defence / 10;
    }
    if ((startBattleAttackAnimationVar5->m0_base.m84_statusFlags2[2] | startBattleAttackAnimationVar5->m0_base.m84_statusFlags2[3]) & 8) {
        defence -= defence / 5;
    }
    if ((startBattleAttackAnimationVar5->m0_base.m84_statusFlags2[2] | startBattleAttackAnimationVar5->m0_base.m84_statusFlags2[3]) & 2) {
        defence -= defence / 10;
    }
    if (currentEntityBattleStats->m22_elementFlags & 0x10) {
        if (!(startBattleAttackAnimationVar5->m0_base.m7C_statusFlags[3] & 0x40)) {
            startBattleAttackAnimationVar5->m0_base.m7C_statusFlags[2] |= 0x40;
        }
        if ((battleGetSlotStatusSub_currentBattleEntity->m0_base.m84_statusFlags2[4] | battleGetSlotStatusSub_currentBattleEntity->m0_base.m84_statusFlags2[5]) & 0x4000) {
            initBattleAttackStatusArray0[startBattleAttackAnimationVar2] = 3;
            damageDonePerAttack[startBattleAttackAnimationVar2] = (u16)(battleGetSlotStatusSub_currentBattleEntity->m0_base.m52_MaxMP / 10) << 1;
        }
        if ((battleGetSlotStatusSub_currentBattleEntity->m0_base.m84_statusFlags2[4] | battleGetSlotStatusSub_currentBattleEntity->m0_base.m84_statusFlags2[5]) & 0x1000) {
            initBattleAttackStatusArray0[startBattleAttackAnimationVar2] = 2;
            damageDonePerAttack[startBattleAttackAnimationVar2] = (u16)(battleGetSlotStatusSub_currentBattleEntity->m0_base.m4E_MaxHP / 10) << 1;
        }
    }
    if ((currentEntityBattleStats->m22_elementFlags & 0x20) && !(startBattleAttackAnimationVar5->m0_base.m7C_statusFlags[3] & 0x80)) {
        startBattleAttackAnimationVar5->m0_base.m7C_statusFlags[2] |= 0x80;
    }
    if (startBattleAttackAnimationVar5->m0_base.m7C_statusFlags[2] & 0x40) {
        defence -= defence >> 2;
        startBattleAttackAnimationVar5->m0_base.m7C_statusFlags[2] &= ~0x40;
    }
    if (battleGetSlotStatusSub_currentBattleEntity->m0_base.m7C_statusFlags[2] & 0x80) {
        attack -= attack >> 2;
        battleGetSlotStatusSub_currentBattleEntity->m0_base.m7C_statusFlags[2] &= ~0x80;
    }
    if (currentEntityBattleStats->mA_attackFlags & 0x400) {
        power = 20;
    }
    computeBattleDamageSub3(&attack, &defence, &result);
    if (currentEntityBattleStats->mA_attackFlags & 0x100) {
        atkMul = 5;
        defMul = 4;
    } else {
        atkMul = 4;
        defMul = 3;
    }
    if (defence != 0) {
        damage = atkMul * attack - defMul * defence;
    } else {
        damage = atkMul * attack;
    }
    switch (currentEntityBattleStats->m1A_attackFormula) {
    case 0:
    case 1:
        damage = (power * damage) / 20;
        break;
    case 2:
        break;
    }
    if (damage <= 0) {
        damage = 0;
    } else if (damage < 10) {
        damage += rand() % 2;
    } else {
        damage += rand() % (damage / 10 + 2);
    }
    switch (result) {
    case 1:
        if (damage > 0) {
            pBattleEntities->m5FA0_initBattleAttackStatusArray0[startBattleAttackAnimationVar4] = 0;
        } else {
            damage = 1;
            pBattleEntities->m5FA0_initBattleAttackStatusArray0[startBattleAttackAnimationVar4] = 0;
        }
        break;
    case 2:
        damage /= 2;
        pBattleEntities->m5FA0_initBattleAttackStatusArray0[startBattleAttackAnimationVar4] = 5;
        break;
    case 3:
        damage = 0;
        pBattleEntities->m5FA0_initBattleAttackStatusArray0[startBattleAttackAnimationVar4] = 4;
        break;
    case 4:
        pBattleEntities->m5FA0_initBattleAttackStatusArray0[startBattleAttackAnimationVar4] = 2;
        break;
    case 5:
        pBattleEntities->m5FA0_initBattleAttackStatusArray0[startBattleAttackAnimationVar4] = 7;
        break;
    }
    if (startBattleAttackAnimationVar3 && (currentEntityBattleStats->mA_attackFlags & 0x100) && damage != 0) {
        damage /= 3;
    }
    if (damage > 9999) {
        damage = 9999;
    }
    if (damage < 0) {
        damage = 0;
    }
    pBattleEntities->m5F6C_damageDonePerAttack[startBattleAttackAnimationVar4] = damage;
}

/* Noah: startBattleAttackAnimationVar8_1 (healing: ether * power) */
void startBattleAttackAnimationVar8_1(void) {
    s16 amount;
    u16 bits;

    amount = battleGetSlotStatusSub_currentBattleEntity->m0_base.m5B_ether * currentEntityBattleStats->m11_power;
    if (battleGetSlotStatusSub_currentBattleEntity->m0_base.m84_statusFlags2[3] & 0x2000) {
        amount *= 2;
    }
    bits = startBattleAttackAnimationVar5->m0_base.m84_statusFlags2[4] | startBattleAttackAnimationVar5->m0_base.m84_statusFlags2[5];
    if (bits & 0x100) {
        amount = (amount * 7) / 10;
    }
    if (bits & 0x200) {
        amount = (amount * 13) / 10;
    }
    if (battleEntities.m0_entities[startBattleAttackAnimationVar4].m15A_flags & 0x80) {
        amount = 0;
    }
    pBattleEntities->m5FA0_initBattleAttackStatusArray0[startBattleAttackAnimationVar4] = 2;
    pBattleEntities->m5F6C_damageDonePerAttack[startBattleAttackAnimationVar4] = amount;
}

/* Var8 slot 6: revive (clears the status words, heals m11 tenths of max HP) */
void startBattleAttackAnimationVar8_6(void);
void startBattleAttackAnimationVar8_6(void) {
#if !VERSION_JP
    if (battleEntities.m0_entities[startBattleAttackAnimationVar4].m0_base.m56_battleCommandLoadout == 2) {
        startBattleAttackAnimationSub0(startBattleAttackAnimationVar4, 1);
    }
#endif
    startBattleAttackAnimationVar5->m0_base.m7C_statusFlags[0] = 0;
    startBattleAttackAnimationVar5->m0_base.m7C_statusFlags[2] = 0;
    startBattleAttackAnimationVar5->m0_base.m84_statusFlags2[0] = 0;
    startBattleAttackAnimationVar5->m0_base.m84_statusFlags2[2] = 0;
    startBattleAttackAnimationVar5->m0_base.m84_statusFlags2[4] = 0;
    battleEntities.m5FA0_initBattleAttackStatusArray0[startBattleAttackAnimationVar4] = 2;
    battleEntities.m5F6C_damageDonePerAttack[startBattleAttackAnimationVar4] = (startBattleAttackAnimationVar5->m0_base.m4E_MaxHP * currentEntityBattleStats->m11_power) / 10;
    battleEntities.m5FB6_battleEffectBF |= 1 << startBattleAttackAnimationVar4;
}

/* status cure roll (m1D == 0x6E): clears the status words named by m1E */
void rollStatusClearOnHit(void) {
    if (rand() % 100 <= currentEntityBattleStats->m1C_chance && currentEntityBattleStats->m1D_statusType == 0x6E) {
        if (currentEntityBattleStats->m1E_statusMask & 0x8000) {
            startBattleAttackAnimationVar5->m0_base.m84_statusFlags2[0] = 0;
        }
        if (currentEntityBattleStats->m1E_statusMask & 0x4000) {
            startBattleAttackAnimationVar5->m0_base.m84_statusFlags2[1] = 0;
        }
        if (currentEntityBattleStats->m1E_statusMask & 0x2000) {
            startBattleAttackAnimationVar5->m0_base.m84_statusFlags2[2] = 0;
        }
        if (currentEntityBattleStats->m1E_statusMask & 0x1000) {
            startBattleAttackAnimationVar5->m0_base.m84_statusFlags2[3] = 0;
        }
        if (currentEntityBattleStats->m1E_statusMask & 0x800) {
            startBattleAttackAnimationVar5->m0_base.m84_statusFlags2[4] = 0;
        }
        if (currentEntityBattleStats->m1E_statusMask & 0x400) {
            startBattleAttackAnimationVar5->m0_base.m84_statusFlags2[5] = 0;
        }
        pBattleEntities->m5FC7_battleTickMain_var1 = 0x3A;
    }
}

void startBattleAttackAnimationVar8_2(void) {
    s8 result;

    result = startBattleAttackAnimationVar8_2Sub0(currentEntityBattleStats->m1C_chance, currentEntityBattleStats->m1D_statusType, currentEntityBattleStats->m1E_statusMask);
    if (currentEntityBattleStats->mA_attackFlags & 0x4000) {
        startBattleAttackAnimationVar8_2Sub1(startBattleAttackAnimationVar4, currentEntityBattleStats->m1D_statusType, currentEntityBattleStats->m1E_statusMask, 5);
    } else {
        startBattleAttackAnimationVar8_2Sub1(startBattleAttackAnimationVar4, currentEntityBattleStats->m1D_statusType, currentEntityBattleStats->m1E_statusMask, currentEntityBattleStats->m11_power);
        if (result != 1) {
            pBattleEntities->m5FA0_initBattleAttackStatusArray0[startBattleAttackAnimationVar4] = 6;
        }
    }
}

/* status-effect roll from the attacker's own m0/m2/m3 (called for mA & 0x800) */
void rollWeaponStatusEffect(void) {
    if (startBattleAttackAnimationVar8_2Sub0(battleGetSlotStatusSub_currentBattleEntity->m0_base.m2_weaponStatusChance, battleGetSlotStatusSub_currentBattleEntity->m0_base.m3_weaponStatusType, battleGetSlotStatusSub_currentBattleEntity->m0_base.m0_weaponStatusMask) == 1) {
        startBattleAttackAnimationVar8_2Sub1(startBattleAttackAnimationVar4, currentEntityBattleStats->m1D_statusType, currentEntityBattleStats->m1E_statusMask, 5);
    }
}

/* Var8 slot 5: status cure (m1D bits select the status words to clear), miss -> 6 */
void startBattleAttackAnimationVar8_5(void) {
    if (currentEntityBattleStats->m1C_chance < rand() % 100) {
        pBattleEntities->m5FA0_initBattleAttackStatusArray0[startBattleAttackAnimationVar4] = 6;
        return;
    }
    if (currentEntityBattleStats->m1D_statusType & 0x80) {
        startBattleAttackAnimationVar5->m0_base.m7C_statusFlags[0] &= 0xC000;
    }
    if (currentEntityBattleStats->m1D_statusType & 0x40) {
        startBattleAttackAnimationVar5->m0_base.m7C_statusFlags[2] = 0;
        startBattleAttackAnimationVar5->m0_base.m7A_commandEnabledBF &= ~0x20;
    }
    if (currentEntityBattleStats->m1D_statusType & 0x20) {
        startBattleAttackAnimationVar5->m0_base.m84_statusFlags2[0] = 0;
    }
    if (currentEntityBattleStats->m1D_statusType & 0x10) {
        startBattleAttackAnimationVar5->m0_base.m84_statusFlags2[2] = 0;
    }
    if (currentEntityBattleStats->m1D_statusType & 8) {
        startBattleAttackAnimationVar5->m0_base.m84_statusFlags2[4] = 0;
    }
}

/* Var8 slot 3: drain HP/MP (m1A: 0/1 attacker/target max HP, 2/3 max MP, 5: target HP - 1) */
void startBattleAttackAnimationVar8_3(void) {
    u8 chance;
    u16 base;
    u16 amount;

    /* A chance source other than 0/1 leaves retail's $s0: the dispatch loop's
     * target mask (startBattleAttackAnimation, startGearAttackAnimation). */
    chance = (u8)(1 << startBattleAttackAnimationVar4);
    switch (currentEntityBattleStats->m18_chanceSource) {
    case 0:
        chance = battleGetSlotStatusSub_currentBattleEntity->m0_base.m60_effectChance;
        break;
    case 1:
        chance = currentEntityBattleStats->m1C_chance;
        break;
    }
    if (chance < rand() % 100) {
        pBattleEntities->m5FA0_initBattleAttackStatusArray0[startBattleAttackAnimationVar4] = 6;
        return;
    }
    switch (currentEntityBattleStats->m1A_attackFormula) {
    case 0:
        base = battleGetSlotStatusSub_currentBattleEntity->m0_base.m4E_MaxHP;
        break;
    case 1:
        base = startBattleAttackAnimationVar5->m0_base.m4E_MaxHP;
        break;
    case 2:
        base = battleGetSlotStatusSub_currentBattleEntity->m0_base.m52_MaxMP;
        break;
    case 3:
        base = startBattleAttackAnimationVar5->m0_base.m52_MaxMP;
        break;
    }
    amount = (base * currentEntityBattleStats->m11_power) / 20;
    if (currentEntityBattleStats->m1A_attackFormula == 5) {
        amount = startBattleAttackAnimationVar5->m0_base.m4C_HP - 1;
    }
    switch (currentEntityBattleStats->m1A_attackFormula) {
    case 0:
    case 1:
    case 5:
        pBattleEntities->m5FA0_initBattleAttackStatusArray0[startBattleAttackAnimationVar2] = 2;
        pBattleEntities->m5FA0_initBattleAttackStatusArray0[startBattleAttackAnimationVar4] = 0;
        pBattleEntities->m5F6C_damageDonePerAttack[startBattleAttackAnimationVar2] = amount;
        pBattleEntities->m5F6C_damageDonePerAttack[startBattleAttackAnimationVar4] = amount;
        break;
    case 2:
    case 3:
        if ((startBattleAttackAnimationVar5->m0_base.m84_statusFlags2[2] | startBattleAttackAnimationVar5->m0_base.m84_statusFlags2[3]) & 0x200) {
            pBattleEntities->m5FA0_initBattleAttackStatusArray0[startBattleAttackAnimationVar4] = 6;
            return;
        }
        pBattleEntities->m5FA0_initBattleAttackStatusArray0[startBattleAttackAnimationVar2] = 3;
        pBattleEntities->m5FA0_initBattleAttackStatusArray0[startBattleAttackAnimationVar4] = 1;
        pBattleEntities->m5F6C_damageDonePerAttack[startBattleAttackAnimationVar2] = amount;
        pBattleEntities->m5F6C_damageDonePerAttack[startBattleAttackAnimationVar4] = amount;
        break;
    }
}

/* Var8 slot 4: fixed damage (m1A: HP / m11, HP - 1, missing HP, MP * 10, 1, HP, max HP, KO flag) */
void startBattleAttackAnimationVar8_4(void) {
    u8 chance;

    chance = (u8)(1 << startBattleAttackAnimationVar4); /* retail's $s0, as in Var8_3 */
    switch (currentEntityBattleStats->m18_chanceSource) {
    case 0:
        chance = battleGetSlotStatusSub_currentBattleEntity->m0_base.m60_effectChance;
        break;
    case 1:
        chance = currentEntityBattleStats->m1C_chance;
        break;
    }
    if (chance < rand() % 100) {
        pBattleEntities->m5FA0_initBattleAttackStatusArray0[startBattleAttackAnimationVar4] = 6;
        return;
    }
    pBattleEntities->m5FA0_initBattleAttackStatusArray0[startBattleAttackAnimationVar4] = 0;
    switch (currentEntityBattleStats->m1A_attackFormula) {
    case 0:
        if (pBattleEntities->m0_entities[startBattleAttackAnimationVar4].m15A_flags & 0x80) {
            pBattleEntities->m5F6C_damageDonePerAttack[startBattleAttackAnimationVar4] = R3000_DIV((u32)startBattleAttackAnimationVar6->m60_hp, currentEntityBattleStats->m11_power);
        } else {
            pBattleEntities->m5F6C_damageDonePerAttack[startBattleAttackAnimationVar4] = R3000_DIV(startBattleAttackAnimationVar5->m0_base.m4C_HP, currentEntityBattleStats->m11_power);
        }
        break;
    case 1:
        if (pBattleEntities->m0_entities[startBattleAttackAnimationVar4].m15A_flags & 0x80) {
            pBattleEntities->m5F6C_damageDonePerAttack[startBattleAttackAnimationVar4] = startBattleAttackAnimationVar6->m60_hp - 1;
        } else {
            pBattleEntities->m5F6C_damageDonePerAttack[startBattleAttackAnimationVar4] = startBattleAttackAnimationVar5->m0_base.m4C_HP - 1;
        }
        break;
    case 2:
        if (pBattleEntities->m0_entities[startBattleAttackAnimationVar2].m15A_flags & 0x80) {
            pBattleEntities->m5F6C_damageDonePerAttack[startBattleAttackAnimationVar4] = battleGetSlotStatusSub_currentBattleEntityGear->m64_maxHp - battleGetSlotStatusSub_currentBattleEntityGear->m60_hp;
        } else {
            pBattleEntities->m5F6C_damageDonePerAttack[startBattleAttackAnimationVar4] = battleGetSlotStatusSub_currentBattleEntity->m0_base.m4E_MaxHP - battleGetSlotStatusSub_currentBattleEntity->m0_base.m4C_HP;
        }
        break;
    case 3:
        pBattleEntities->m5F6C_damageDonePerAttack[startBattleAttackAnimationVar4] = startBattleAttackAnimationVar5->m0_base.m50_MP * 10;
        break;
    case 4:
        pBattleEntities->m5F6C_damageDonePerAttack[startBattleAttackAnimationVar4] = 1;
        break;
    case 5:
        pBattleEntities->m5F6C_damageDonePerAttack[startBattleAttackAnimationVar4] = startBattleAttackAnimationVar5->m0_base.m4C_HP;
        break;
    case 6:
        pBattleEntities->m5FA0_initBattleAttackStatusArray0[startBattleAttackAnimationVar4] = 0;
        if (battleEntities.m0_entities[startBattleAttackAnimationVar4].m15A_flags & 0x80) {
            pBattleEntities->m5F6C_damageDonePerAttack[startBattleAttackAnimationVar4] = startBattleAttackAnimationVar6->m64_maxHp;
            if ((u32)startBattleAttackAnimationVar6->m64_maxHp >= 10000) {
                startBattleAttackAnimationVar6->m64_maxHp = 9999;
            }
        } else {
            pBattleEntities->m5F6C_damageDonePerAttack[startBattleAttackAnimationVar4] = startBattleAttackAnimationVar5->m0_base.m4E_MaxHP;
            if (startBattleAttackAnimationVar5->m0_base.m4E_MaxHP >= 10000) {
                startBattleAttackAnimationVar5->m0_base.m4E_MaxHP = 9999;
            }
        }
        break;
    case 7:
        if (pBattleEntities->m0_entities[startBattleAttackAnimationVar4].m15A_flags & 0x80) {
            pBattleEntities->m5FA0_initBattleAttackStatusArray0[startBattleAttackAnimationVar4] = 6;
            break;
        }
        startBattleAttackAnimationVar5->m0_base.m7C_statusFlags[0] |= 1;
        startBattleAttackAnimationVar5->m0_base.m7C_statusFlags[2] |= 1;
        break;
    }
}

/* Noah: computeBattleDamageSub3 (element / resistance multipliers on attack and defence) */
void computeBattleDamageSub3(u16* attack, u16* defence, s8* result) {
    s8 isEther;
    u8 flag;
    u8 targetIsGear;
    u8 elements;
    u8 targetElements;
    u32 shifted;
    u32 bits;
    u16 tb;                     /* target status bits (m8C | m8E) */
    u8 attackerElements;
    s8 attackMul;
    s8 defenceMul;
    s8 flag2;

    isEther = 0;
    flag = 0;
    targetIsGear = pBattleEntities->m0_entities[startBattleAttackAnimationVar4].m15A_flags >> 7;
    elements = currentEntityBattleStats->m22_elementFlags & 0x3F;
    targetElements = *(u8*)&startBattleAttackAnimationVar5->m0_base.m38_weaknessFlags & 0x3F;
    if (!(pBattleEntities->m0_entities[startBattleAttackAnimationVar2].m15A_flags >> 7)) {
        bits = battleGetSlotStatusSub_currentBattleEntity->m0_base.m84_statusFlags2[4] | battleGetSlotStatusSub_currentBattleEntity->m0_base.m84_statusFlags2[5];
    } else {
        bits = battleGetSlotStatusSub_currentBattleEntityGear->m84_statusFlags4 | battleGetSlotStatusSub_currentBattleEntityGear->m86_statusFlags5;
    }
    attackerElements = bits >> 12;
    if (currentEntityBattleStats->mA_attackFlags & 0x100) {
        isEther = 1;
    }
    if (elements == 0 && attackerElements != 0) {
        elements = attackerElements;
    }
    if (elements & attackerElements) {
        flag = 1;
    }
    attackMul = 10;
    defenceMul = 10;
    if (elements & targetElements) {
        attackMul = 15;
        if (startBattleAttackAnimationVar5->m0_base.m38_weaknessFlags & 0x40) {
            attackMul = 18;
        }
        if (flag) {
            attackMul += 2;
        }
    }
    /* the do/while (0) blocks (a macro in the original?) keep the or result in its
     * own register for the bit 1 test; retail turns the first one into a store-flag */
    flag2 = 0;
    if (!targetIsGear) {
        tb = startBattleAttackAnimationVar5->m0_base.m84_statusFlags2[4] | startBattleAttackAnimationVar5->m0_base.m84_statusFlags2[5];
        targetElements = (tb & 0xF00) >> 8;
        bits = tb;
        do {
            if (tb & 2) {
                flag2 = 1;
            }
        } while (0);
    } else {
        tb = startBattleAttackAnimationVar6->m84_statusFlags4 | startBattleAttackAnimationVar6->m86_statusFlags5;
        targetElements = (tb & 0xF00) >> 8;
        bits = tb;
        do {
            if (tb & 2) {
                flag2 = 1;
            }
        } while (0);
    }
    if (elements & targetElements) {
        if (isEther) {
            attackMul -= 3;
            if (bits & 4) {
                attackMul -= 3;
            }
            if (bits & 8) {
                *result = 4;
            }
        }
        if ((elements & targetElements) && flag2) {
            attackMul -= 3;
            if (bits & 4) {
                attackMul -= 3;
            }
            if (bits & 8) {
                *result = 4;
            }
        }
    }
    shifted = targetElements << 8;
    switch (elements) {
    case 8:
        if (shifted & 0x400) attackMul += 3;
        break;
    case 1:
        if (shifted & 0x200) attackMul += 3;
        break;
    case 4:
        if (shifted & 0x800) attackMul += 3;
        break;
    case 2:
        if (shifted & 0x100) attackMul += 3;
        break;
    }
    if (attackMul <= 0) {
        attackMul = 1;
    }
    if (defenceMul == 0) {
        defenceMul = 1;
    }
    *attack = (*attack * attackMul) / 10;
    *defence = (defenceMul * *defence) / 10;
    if (battleGetSlotStatusSub_currentBattleEntity->m0_base.m32_accessoryFlags & 0x10) {
        *attack += *attack / 5;
    }
    if (startBattleAttackAnimationVar5->m0_base.m32_accessoryFlags & 0x10) {
        *defence += *defence / 5;
    }
}

/* ether-attack miss roll: rand()%100 >= ether + stats.m14 -> battleTickMain_var1 = 0x38, Var3 = 1 */
void rollEtherAttackMiss(void) {
    s32 chance = battleGetSlotStatusSub_currentBattleEntity->m0_base.m5B_ether + currentEntityBattleStats->m14_etherHitBonus;

    if (rand() % 100 >= chance) {
        pBattleEntities->m5FC7_battleTickMain_var1 = 0x38;
        startBattleAttackAnimationVar3 = 1;
    }
}

/* Noah: onPlayerAttackedByEnemy (counterattack: swaps attacker and target) */
void onPlayerAttackedByEnemy(void) {
    s32 chance;
    u8 attacker;
    sBattleEntity* target;
    u16 status;
    u16 flags;

    if (!(startBattleAttackAnimationVar5->m0_base.m7C_statusFlags[0] & 0xA000)) {
        status = startBattleAttackAnimationVar5->m0_base.m7C_statusFlags[2];
        if (!(status & 0x1000)) {
            if ((startBattleAttackAnimationVar2 >= 3 || (status & 0x2000)) && (startBattleAttackAnimationVar5->m0_base.m84_statusFlags2[2] & 0x1000)) {
                flags = currentEntityBattleStats->mA_attackFlags;
                if (!(flags & 0x2000) && !(flags & 0x100)
                    && !(pBattleEntities->m0_entities[startBattleAttackAnimationVar2].m15A_flags & 0x80)) {
                    chance = 50;
                    if (startBattleAttackAnimationVar5->m0_base.m56_battleCommandLoadout == 0) {
                        chance = 60;
                    }
                    if (rand() % 100 <= chance && currentEntityBattleStats->m16_effectType != 2) {
                        attacker = startBattleAttackAnimationVar2;
                        startBattleAttackAnimationVar2 = startBattleAttackAnimationVar4;
                        target = battleGetSlotStatusSub_currentBattleEntity;
                        battleGetSlotStatusSub_currentBattleEntity = startBattleAttackAnimationVar5;
                        startBattleAttackAnimationVar4 = attacker;
                        startBattleAttackAnimationVar5 = target;
                        currentEntityBattleStats = &pBattleEntities->m1058_partyBattleStats[startBattleAttackAnimationVar2][20];
                        pBattleEntities->m5FA0_initBattleAttackStatusArray0[startBattleAttackAnimationVar2] = 7;
                        pBattleEntities->m5F6C_damageDonePerAttack[startBattleAttackAnimationVar2] = 0;
                        pBattleEntities->m5FC7_battleTickMain_var1 = 0x34;
                    }
                }
            }
        }
    }
}

/* Noah: computeBattleDamageSub0 (hit roll: 1 hit, 2 half damage, 3 miss, 5 when stats.mA & 2) */
s8 computeBattleDamageSub0(void) {
    s32 bonus = 0;
    s32 penalty = 0;
    s16 hit = battleGetSlotStatusSub_currentBattleEntity->m0_base.m5E_hitPercentage;
    u8 evade = startBattleAttackAnimationVar5->m0_base.m5F_evadePercentage;
    s16 chance;
    u16 flags;
    u16 status;

    if ((currentEntityBattleStats->mA_attackFlags & 0x40) && (battleEntities.m0_entities[startBattleAttackAnimationVar4].m15A_flags & 0x80)) {
        return 3;
    }
    if (battleGetSlotStatusSub_currentBattleEntity->m0_base.m56_battleCommandLoadout == 4) {
        if (currentEntityBattleStats->m10_weaponMask & 0x80) {
            if (gameStateAmmoByWeaponItem[battleGetSlotStatusSub_currentBattleEntity->m0_base.m6F_equipment[0]] == 0) {
                startBattleAttackAnimationVar1 = 1;
                return 3;
            }
        }
        if (currentEntityBattleStats->m10_weaponMask & 0x10) {
            if (gameStateAmmoByWeaponItem[battleGetSlotStatusSub_currentBattleEntity->m0_base.m6F_equipment[3]] == 0) {
                startBattleAttackAnimationVar1 = 1;
                return 3;
            }
        }
    }
    if (currentEntityBattleStats->mA_attackFlags & 0x200) {
        if (startBattleAttackAnimationVar5->m0_base.m34_monsterFlags & 8) {
            return 3;
        }
        if (battleEntities.m0_entities[startBattleAttackAnimationVar4].m15A_flags & 0x80) {
            return 3;
        }
    }
    flags = currentEntityBattleStats->mA_attackFlags;
    if (flags & 0x1000) {
        return 3;
    }
    status = startBattleAttackAnimationVar5->m0_base.m84_statusFlags2[0] | startBattleAttackAnimationVar5->m0_base.m84_statusFlags2[1];
    if (status & 0x100) {
        return 3;
    }
    if (startBattleAttackAnimationVar5->m0_base.m7C_statusFlags[0] & 0x2000) {
        return 1;
    }
    if (startBattleAttackAnimationVar5->m0_base.m7C_statusFlags[2] & 0x1000) {
        return 1;
    }
    if (flags & 0x8000) {
        return 1;
    }
    if (flags & 2) {
        return 5;
    }
    if (battleGetSlotStatusSub_currentBattleEntity->m0_base.m7C_statusFlags[0] & 0x400) {
        bonus -= 50;
    }
    if ((battleGetSlotStatusSub_currentBattleEntity->m0_base.m84_statusFlags2[0] | battleGetSlotStatusSub_currentBattleEntity->m0_base.m84_statusFlags2[1]) & 0x1000) {
        bonus += 30;
    }
    if (status & 0x800) {
        penalty += 50;
    }
    chance = hit + (s8)currentEntityBattleStats->m15_hitBonus - evade;

    if (pBattleEntities->m0_entities[startBattleAttackAnimationVar4].m15A_flags & 1) {
        if (rand() % 100 < 95) {
            return 2;
        }
        return 1;
    }
    if (status & 0x20) {
        if ((s16)(rand() % 100 - chance) >= 50) {
            return 3;
        }
        return 1;
    }
    if ((startBattleAttackAnimationVar5->m0_base.m84_statusFlags2[0] | startBattleAttackAnimationVar5->m0_base.m84_statusFlags2[1]) & 0x40) {
        if ((s16)(rand() % 100 - chance) >= 50) {
            return 2;
        }
        return 1;
    }
    if ((s16)(rand() % 100 - chance) >= (s16)(bonus - (s16)(penalty - 90))) {
        return 3;
    }
    if ((s16)(rand() % 100 - chance) >= (s16)(bonus - (s16)(penalty - 85))) {
        return 2;
    }
    return 1;
}

/* Noah: computeBattleDamageSub1 (attack power: weapons + attack, or ether; command modifiers) */
s16 computeBattleDamageSub1(void) {
    u8 weapons[5];
    u16 damage;
    u16 attack;
    u16 ether;
    u16 sum;
    u16 bits;
    u16 flags;
    u8 flags10;
    u8 mul;
    u8 i;

    if (pBattleEntities->m0_entities[startBattleAttackAnimationVar2].m15A_flags & 0x80) {
        for (i = 0; i < 3; i++) {
            weapons[i] = battleGetSlotStatusSub_currentBattleEntityGear->m12_slots[i].m0_weaponPower;
        }
        attack = battleGetSlotStatusSub_currentBattleEntityGear->m3C_engineOutput * battleGetSlotStatusSub_currentBattleEntityGear->m3F_attackMultiplier;
        if ((battleGetSlotStatusSub_currentBattleEntityGear->m80_statusFlags2 | battleGetSlotStatusSub_currentBattleEntityGear->m82_statusFlags3) & 0x1000) {
            attack += battleGetSlotStatusSub_currentBattleEntityGear->m3C_engineOutput * 2;
        }
    } else {
        for (i = 0; i < 4; i++) {
            weapons[i] = battleGetSlotStatusSub_currentBattleEntity->m0_base.m4_weaponSlots[i].m0_weaponPower;
        }
        attack = battleGetSlotStatusSub_currentBattleEntity->m0_base.m58_attack;
    }
    flags10 = currentEntityBattleStats->m10_weaponMask;
    ether = battleGetSlotStatusSub_currentBattleEntity->m0_base.m5B_ether;
    sum = 0;
    if (flags10 & 0x80) {
        sum = weapons[0];
    }
    if (flags10 & 0x40) {
        sum += weapons[1];
    }
    if (flags10 & 0x20) {
        sum += weapons[2];
    }
    if (flags10 & 0x10) {
        sum += weapons[3];
    }
    if (flags10 & 8) {
        sum += weapons[4];
    }
    if ((battleGetSlotStatusSub_currentBattleEntity->m0_base.m84_statusFlags2[4] | battleGetSlotStatusSub_currentBattleEntity->m0_base.m84_statusFlags2[5]) & 1) {
        sum += sum >> 1;
    }
    if (currentEntityBattleStats->mA_attackFlags & 0x100) {
        mul = 4;
        if ((battleGetSlotStatusSub_currentBattleEntity->m0_base.m84_statusFlags2[2] | battleGetSlotStatusSub_currentBattleEntity->m0_base.m84_statusFlags2[3]) & 0x8000) {
            mul = 5;
        }
        if (battleGetSlotStatusSub_currentBattleEntity->m0_base.m7C_statusFlags[2] & 0x400) {
            mul--;
        }
        ether = (ether * mul) / 4;
        if ((battleGetSlotStatusSub_currentBattleEntity->m0_base.m84_statusFlags2[2] | battleGetSlotStatusSub_currentBattleEntity->m0_base.m84_statusFlags2[3]) & 0x2000) {
            ether *= 2;
        }
    } else if (!(pBattleEntities->m0_entities[startBattleAttackAnimationVar2].m15A_flags & 0x80)) {
        bits = battleGetSlotStatusSub_currentBattleEntity->m0_base.m84_statusFlags2[0] | battleGetSlotStatusSub_currentBattleEntity->m0_base.m84_statusFlags2[1];
        mul = 4;
        if (bits & 0x2000) {
            mul = 5;
        }
        if (battleGetSlotStatusSub_currentBattleEntity->m0_base.m7C_statusFlags[0] & 0x200) {
            mul--;
        }
        if (bits & 0x400) {
            mul += 10 - R3000_DIV(battleGetSlotStatusSub_currentBattleEntity->m0_base.m4C_HP, battleGetSlotStatusSub_currentBattleEntity->m0_base.m4E_MaxHP / 10);
        }
        attack = (attack * mul) / 4;
    }
    flags = currentEntityBattleStats->mA_attackFlags;
    if (flags & 0x20) {
        attack = 0;
    }
    switch (currentEntityBattleStats->m1A_attackFormula) {
    case 0:
        damage = sum + attack;
        break;
    case 1:
        damage = ether;
        break;
    case 2:
        if (battleGetSlotStatusSub_currentBattleEntity->m0_base.m56_battleCommandLoadout == 4) {
            sum = (sum * 6) / 10;
        }
        if (flags & 0x100) {
            damage = (ether * currentEntityBattleStats->m11_power) / 20;
        } else {
            damage = ((sum + attack) * currentEntityBattleStats->m11_power) / 20;
        }
        break;
    }
    if (startBattleAttackAnimationVar2 < 3) {
        if (battleGetSlotStatusSub_currentBattleEntity->m0_base.m56_battleCommandLoadout == 7) {
            applyPinchAndCriticalBonus(&damage);
        }
        if (battleGetSlotStatusSub_currentBattleEntity->m0_base.m56_battleCommandLoadout == 4 || (currentEntityBattleStats->m22_elementFlags & 0x20)) {
            if (startBattleAttackAnimationVar5->m0_base.m38_weaknessFlags & 0x20) {
                damage += damage >> 2;
            }
            /* retail reads 0x38 as a word here (bitfields in the original) */
            if ((*(u32*)&startBattleAttackAnimationVar5->m0_base.m38_weaknessFlags & 0x60) == 0x60) {
                damage += damage >> 2;
            }
        }
        if (battleGetSlotStatusSub_currentBattleEntity->m0_base.m56_battleCommandLoadout == 8
            && !(battleEntities.m0_entities[startBattleAttackAnimationVar2].m15A_flags & 0x80)
            && (currentEntityBattleStats->mA_attackFlags & 0x100)) {
            damage = (damage * battleEntities.m0_entities[startBattleAttackAnimationVar2].mA4_gear.m9E_etherAmp) / 4;
        }
        if (battleGetSlotStatusSub_currentBattleEntity->m0_base.m56_battleCommandLoadout == 10) {
            damage += damage / 5;
        }
        if (currentEntityBattleStats->m22_elementFlags & 0x10) {
            if (startBattleAttackAnimationVar5->m0_base.m38_weaknessFlags & 0x10) {
                damage += damage >> 2;
                if (startBattleAttackAnimationVar5->m0_base.m38_weaknessFlags & 0x10) {
                    damage += damage >> 2;
                }
            }
        }
    }
    return damage;
}

/* Noah: computeBattleDamageSub2 (defence of the target: physical def + armour, or ether defence) */
s16 computeBattleDamageSub2(void) {
    u16 defence;
    u16 base;
    u16 ether;
    u16 result;
    u8 count;
    u8 i;

    if (pBattleEntities->m0_entities[startBattleAttackAnimationVar4].m15A_flags & 0x80) {
        base = startBattleAttackAnimationVar6->m70_defence;
    } else {
        defence = startBattleAttackAnimationVar5->m0_base.m59_defence;
        base = startBattleAttackAnimationVar5->m0_base.m2D_accessoryDefence;
    }
    ether = startBattleAttackAnimationVar5->m0_base.m5C_etherDefence;
    if (currentEntityBattleStats->mA_attackFlags & 0x100) {
        if ((startBattleAttackAnimationVar5->m0_base.m84_statusFlags2[2] | startBattleAttackAnimationVar5->m0_base.m84_statusFlags2[3]) & 0x4000) {
            ether = (ether * 3) / 2;
        }
    } else {
        if (pBattleEntities->m0_entities[startBattleAttackAnimationVar4].m15A_flags & 0x80) {
            goto noDefence;
        }
        /* Def UP is m84[0] bit 0x4000 (setStatusEffectMessage type 5, the
         * buff timers in updateBattleEntityStatus), but this tests 0x100 (a
         * target with 0x100 has already dodged: computeBattleDamageSub0), so
         * Def UP never raised defence; Eth Def UP (m84[2] 0x4000) does raise
         * ether defence above. Original-game bug fixes: test Def UP too. */
        if ((startBattleAttackAnimationVar5->m0_base.m84_statusFlags2[0] | startBattleAttackAnimationVar5->m0_base.m84_statusFlags2[1]) &
            (pcGameplayOriginalBugFixes() ? 0x4100 : 0x100)) {
            defence = (defence * 3) >> 1;
        }
    }
    if (pBattleEntities->m0_entities[startBattleAttackAnimationVar4].m15A_flags & 0x80) {
    noDefence:
        defence = 0;
    }
    switch (currentEntityBattleStats->m1B_defenceFormula) {
    case 0:
        result = base + defence;
        break;
    case 1:
        result = ether;
        break;
    case 2:
        result = base;
        break;
    }
    if (!(pBattleEntities->m0_entities[startBattleAttackAnimationVar4].m15A_flags & 0x80)) {
        count = 0;
        for (i = 0; i < 3; i++) {
            if (pBattleEntities->m0_entities[i].m0_base.m7C_statusFlags[0] & 0x8000) {
                count++;
            }
        }
        if ((startBattleAttackAnimationVar5->m0_base.m32_accessoryFlags & 4) && count && !(currentEntityBattleStats->mA_attackFlags & 0x100)) {
            result = (result * (4 - count)) / 4;
        }
        if ((startBattleAttackAnimationVar5->m0_base.m32_accessoryFlags & 2) && count && !(currentEntityBattleStats->mA_attackFlags & 0x100)) {
            result = (result * (count + 2)) / 2;
        }
        if ((startBattleAttackAnimationVar5->m0_base.m32_accessoryFlags & 1) && count) {
            result = (result * (count + 2)) / 2;
        }
    }
    return result;
}

/* (already C in src) | empty function */
void func_8009795C(void) {
}

/* Noah: startBattleAttackAnimationVar8_2Sub0 (status-effect roll: chance, type = status word, mask = bits) */
s8 startBattleAttackAnimationVar8_2Sub0(u8 chance, u8 type, u16 mask) {
    u16 others;
    if (pBattleEntities->m0_entities[startBattleAttackAnimationVar4].m15A_flags & 0x80) {
        return 0;
    }
    if (chance < rand() % 100) {
        return 0;
    }
    switch (type) {
    case 0:
        others = mask & ~2;
        if (startBattleAttackAnimationVar5->m0_base.m7C_statusFlags[1] & others) {
            return 0;
        }
        if (mask & 0x1000) {
            startBattleAttackAnimationVar5->m0_base.m84_statusFlags2[0] &= 0x7FFF;
        }
        break;
    case 2:
        if (mask & startBattleAttackAnimationVar5->m0_base.m7C_statusFlags[3]) {
            return 0;
        }
        break;
    case 5:
        if ((startBattleAttackAnimationVar5->m0_base.m7C_statusFlags[0] | startBattleAttackAnimationVar5->m0_base.m7C_statusFlags[1]) & 1) {
            return 0;
        }
        if (mask & 0x8000) {
            startBattleAttackAnimationVar5->m0_base.m7C_statusFlags[0] &= 0xEFFF;
        }
        break;
    case 7:
        if ((startBattleAttackAnimationVar5->m0_base.m7C_statusFlags[2] | startBattleAttackAnimationVar5->m0_base.m7C_statusFlags[3]) & 1) {
            return 0;
        }
        if (mask & 0xA) {
            if (startBattleAttackAnimationVar5->m0_base.m84_statusFlags2[2] & 5) {
                startBattleAttackAnimationVar5->m0_base.m84_statusFlags2[2] &= ~5;
                return 1;
            }
            break;
        }
        if (mask & 5) {
            if (startBattleAttackAnimationVar5->m0_base.m84_statusFlags2[2] & 0xA) {
                startBattleAttackAnimationVar5->m0_base.m84_statusFlags2[2] &= ~0xA;
                return 1;
            }
            break;
        }
    case 9:
        if (mask & 0xF000) {
            if (startBattleAttackAnimationVar5->m0_base.m84_statusFlags2[5] & 0xF000) {
                goto resisted;
            }
            startBattleAttackAnimationVar5->m0_base.m84_statusFlags2[4] &= 0xFFF;
        }
        if (mask & 0xF00) {
            if (startBattleAttackAnimationVar5->m0_base.m84_statusFlags2[5] & 0xF00) {
            resisted:
                pBattleEntities->m5FC7_battleTickMain_var1 = 0x39;
                return 0;
            }
            startBattleAttackAnimationVar5->m0_base.m84_statusFlags2[4] &= 0xF0FF;
        }
        break;
    }
    switch (type) {
    case 0:
        startBattleAttackAnimationVar5->m0_base.m7C_statusFlags[0] = (mask | startBattleAttackAnimationVar5->m0_base.m7C_statusFlags[0]) & ~2;
        if (mask & 2) {
            if (canDisableAnotherPartyMember()) {
                startBattleAttackAnimationVar5->m0_base.m7C_statusFlags[0] |= mask;
                pBattleEntities->m5FC7_battleTickMain_var1 = 0x31;
                startBattleAttackAnimationVar5->m0_base.m7A_commandEnabledBF = 0xFFEF;
            } else {
                pBattleEntities->m5FC7_battleTickMain_var1 = 0x32;
            }
        }
        break;
    case 2:
        startBattleAttackAnimationVar5->m0_base.m7C_statusFlags[2] |= mask;
        if (mask & 0x800) {
            startBattleAttackAnimationVar5->m0_base.m7A_commandEnabledBF |= 0x20;
        }
        break;
    case 5:
    case 7:
    case 9:
        startBattleAttackAnimationVar5->m0_base.m7C_statusFlags[type - 1] |= mask;
        break;
    }
    setStatusEffectMessage(type, mask);
    return 1;
}

void initBattleAttackStatus(void) {
    s16 i = 11;

    do {
        pBattleEntities->m5FA0_initBattleAttackStatusArray0[i] = 0xFF;
        pBattleEntities->m5F6C_damageDonePerAttack[i] = 0;
    } while (--i != -1);
}

/* battle start: snapshots each PC's stats, applies accessory and gear bonuses, per-character fixes */
void initPartyBattleStats(void) {
    u8 i;
    u8 j;
    u8* p148;
    sBattleEntity* entity;

    for (i = 0; i < 3; i++) {
        if (battleCharacters[i] == 0x7F) {
            continue;
        }
        battleGetSlotStatusSub_currentBattleEntity = &pBattleEntities->m0_entities[i];
        battleGetSlotStatusSub_currentBattleEntityGear = &pBattleEntities->m0_entities[i].mA4_gear;
        p148 = &pBattleEntities->m0_entities[i].m148_gearMode;
        if (battleCharacters[i] == 9) {
            gameState.m26C_party[9].m56_battleCommandLoadout = 9;
            battleGetSlotStatusSub_currentBattleEntity->m0_base.m56_battleCommandLoadout = 9;
        }
        if (battleCharacters[i] == 10) {
            gameState.m26C_party[10].m56_battleCommandLoadout = 10;
            battleGetSlotStatusSub_currentBattleEntity->m0_base.m56_battleCommandLoadout = 10;
        }
        if (battleGetSlotStatusSub_currentBattleEntity->m0_base.m56_battleCommandLoadout == 4) {
            pBattleEntities->m1028_savedStats[i][0] = battleGetSlotStatusSub_currentBattleEntity->m0_base.m4_weaponSlots[0].m0_weaponPower + battleGetSlotStatusSub_currentBattleEntity->m0_base.m4_weaponSlots[3].m0_weaponPower;
        } else {
            pBattleEntities->m1028_savedStats[i][0] = battleGetSlotStatusSub_currentBattleEntity->m0_base.m58_attack + battleGetSlotStatusSub_currentBattleEntity->m0_base.m4_weaponSlots[0].m0_weaponPower;
        }
        pBattleEntities->m1028_savedStats[i][1] = battleGetSlotStatusSub_currentBattleEntity->m0_base.m5E_hitPercentage;
        pBattleEntities->m1028_savedStats[i][2] = battleGetSlotStatusSub_currentBattleEntity->m0_base.m59_defence + battleGetSlotStatusSub_currentBattleEntity->m0_base.m2D_accessoryDefence;
        pBattleEntities->m1028_savedStats[i][3] = battleGetSlotStatusSub_currentBattleEntity->m0_base.m5F_evadePercentage;
        pBattleEntities->m1028_savedStats[i][4] = battleGetSlotStatusSub_currentBattleEntity->m0_base.m5B_ether;
        pBattleEntities->m1028_savedStats[i][5] = battleGetSlotStatusSub_currentBattleEntity->m0_base.m5C_etherDefence;
        pBattleEntities->m1028_savedStats[i][6] = battleGetSlotStatusSub_currentBattleEntity->m0_base.m5A_agility;
        pBattleEntities->mFD0_savedM3C[i][0] = battleGetSlotStatusSub_currentBattleEntity->m0_base.m3C_experience;
        pBattleEntities->mFD0_savedM3C[i][1] = battleGetSlotStatusSub_currentBattleEntity->m0_base.m40_experience2;
        pBattleEntities->m1000_savedMaxHpMp[i][0] = battleGetSlotStatusSub_currentBattleEntity->m0_base.m4E_MaxHP;
        pBattleEntities->m1000_savedMaxHpMp[i][1] = battleGetSlotStatusSub_currentBattleEntity->m0_base.m52_MaxMP;
        battleGetSlotStatusSub_currentBattleEntity->m0_base.m34_monsterFlags = 0;
        battleGetSlotStatusSub_currentBattleEntity->m0_base.m58_attack += battleGetSlotStatusSub_currentBattleEntity->m0_base.m28_attackBonus;
        battleGetSlotStatusSub_currentBattleEntity->m0_base.m59_defence += battleGetSlotStatusSub_currentBattleEntity->m0_base.m29_defenceBonus;
        battleGetSlotStatusSub_currentBattleEntity->m0_base.m5A_agility += battleGetSlotStatusSub_currentBattleEntity->m0_base.m2A_agilityBonus;
        battleGetSlotStatusSub_currentBattleEntity->m0_base.m5B_ether += battleGetSlotStatusSub_currentBattleEntity->m0_base.m2B_etherBonus;
        battleGetSlotStatusSub_currentBattleEntity->m0_base.m5C_etherDefence += battleGetSlotStatusSub_currentBattleEntity->m0_base.m2C_etherDefenceBonus;
        battleGetSlotStatusSub_currentBattleEntity->m0_base.m5E_hitPercentage += battleGetSlotStatusSub_currentBattleEntity->m0_base.m2E_hitBonus;
        battleGetSlotStatusSub_currentBattleEntity->m0_base.m5F_evadePercentage += battleGetSlotStatusSub_currentBattleEntity->m0_base.m2F_evadeBonus;
        if (battleGetSlotStatusSub_currentBattleEntity->m0_base.m5A_agility > 16) {
            battleGetSlotStatusSub_currentBattleEntity->m0_base.m5A_agility = 16;
        }
        if (battleGetSlotStatusSub_currentBattleEntity->m0_base.m32_accessoryFlags & 0x100) {
            battleGetSlotStatusSub_currentBattleEntity->m0_base.m5E_hitPercentage += battleGetSlotStatusSub_currentBattleEntity->m0_base.m5E_hitPercentage >> 2;
            battleGetSlotStatusSub_currentBattleEntity->m0_base.m5F_evadePercentage += battleGetSlotStatusSub_currentBattleEntity->m0_base.m5F_evadePercentage >> 2;
        }
        battleGetSlotStatusSub_currentBattleEntity->m0_base.m4C_HP += battleGetSlotStatusSub_currentBattleEntity->m0_base.m4E_MaxHP * battleGetSlotStatusSub_currentBattleEntity->m0_base.m30_maxHpBonus / 20;
        battleGetSlotStatusSub_currentBattleEntity->m0_base.m50_MP += battleGetSlotStatusSub_currentBattleEntity->m0_base.m52_MaxMP * battleGetSlotStatusSub_currentBattleEntity->m0_base.m31_maxMpBonus / 20;
        battleGetSlotStatusSub_currentBattleEntity->m0_base.m4E_MaxHP += battleGetSlotStatusSub_currentBattleEntity->m0_base.m4E_MaxHP * battleGetSlotStatusSub_currentBattleEntity->m0_base.m30_maxHpBonus / 20;
        battleGetSlotStatusSub_currentBattleEntity->m0_base.m52_MaxMP += battleGetSlotStatusSub_currentBattleEntity->m0_base.m52_MaxMP * battleGetSlotStatusSub_currentBattleEntity->m0_base.m31_maxMpBonus / 20;
        if (battleGetSlotStatusSub_currentBattleEntity->m0_base.m4C_HP > 999) {
            battleGetSlotStatusSub_currentBattleEntity->m0_base.m4C_HP = 999;
        }
        if (battleGetSlotStatusSub_currentBattleEntity->m0_base.m50_MP > 99) {
            battleGetSlotStatusSub_currentBattleEntity->m0_base.m50_MP = 99;
        }
        if (battleGetSlotStatusSub_currentBattleEntity->m0_base.m4E_MaxHP > 999) {
            battleGetSlotStatusSub_currentBattleEntity->m0_base.m4E_MaxHP = 999;
        }
        if (battleGetSlotStatusSub_currentBattleEntity->m0_base.m52_MaxMP > 99) {
            battleGetSlotStatusSub_currentBattleEntity->m0_base.m52_MaxMP = 99;
        }
        entity = &pBattleEntities->m0_entities[i];
        entity->m158_expShares[1] = entity->m158_expShares[0] = 5;
        p148[0] = 0;
        battleGetSlotStatusSub_currentBattleEntityGear->m70_defence += battleGetSlotStatusSub_currentBattleEntityGear->m40_accessoryDefence;
        battleGetSlotStatusSub_currentBattleEntityGear->m72_etherDefence += battleGetSlotStatusSub_currentBattleEntityGear->m42_accessoryEtherDefence;
        battleGetSlotStatusSub_currentBattleEntityGear->m68_weight += battleGetSlotStatusSub_currentBattleEntityGear->m44_accessoryWeight + battleGetSlotStatusSub_currentBattleEntityGear->m46;
        battleGetSlotStatusSub_currentBattleEntityGear->m9C += battleGetSlotStatusSub_currentBattleEntityGear->m4C;
        battleGetSlotStatusSub_currentBattleEntityGear->m9F_response += battleGetSlotStatusSub_currentBattleEntityGear->m4D_accessoryResponse;
        battleGetSlotStatusSub_currentBattleEntityGear->m98_agility += battleGetSlotStatusSub_currentBattleEntityGear->m4E_accessoryAgility - battleGetSlotStatusSub_currentBattleEntityGear->m4A_agilityPenalty;
        battleGetSlotStatusSub_currentBattleEntityGear->m9E_etherAmp += battleGetSlotStatusSub_currentBattleEntityGear->m54_accessoryEtherAmp;
        if (battleGetSlotStatusSub_currentBattleEntityGear->m4F_frameRepairLevel != 0 && battleGetSlotStatusSub_currentBattleEntity->m0_base.mA0_gearNum != 0x12) {
            pBattleEntities->m2228_mechaBattleStats[i][37].m24_fuelCost = ((u32)battleGetSlotStatusSub_currentBattleEntityGear->m64_maxHp / 10 * battleGetSlotStatusSub_currentBattleEntityGear->m4F_frameRepairLevel * 2) / 9;
            pBattleEntities->m2228_mechaBattleStats[i][37].m24_fuelCost = pBattleEntities->m2228_mechaBattleStats[i][37].m24_fuelCost / 10;
            pBattleEntities->m2228_mechaBattleStats[i][37].m24_fuelCost = pBattleEntities->m2228_mechaBattleStats[i][37].m24_fuelCost * 10;
        }
        p148[1] = 0;
        if (gameState.m16C0_abilities[battleGetSlotStatusSub_currentBattleEntity->m0_base.m56_battleCommandLoadout].m0_unlockedAbilitiesBitField[2] & 0x1C00) {
            p148[1] = 1;
        }
        if (gameState.m16C0_abilities[battleGetSlotStatusSub_currentBattleEntity->m0_base.m56_battleCommandLoadout].m0_unlockedAbilitiesBitField[2] & 0x380) {
            p148[1] = 2;
        }
        if (gameState.m16C0_abilities[battleGetSlotStatusSub_currentBattleEntity->m0_base.m56_battleCommandLoadout].m0_unlockedAbilitiesBitField[2] & 0x70) {
            p148[1] = 3;
        }
        battleGetSlotStatusSub_currentBattleEntityGear->m3F_attackMultiplier = battleGetSlotStatusSub_currentBattleEntityGear->m74_baseAttackMultiplier;
        battleGetSlotStatusSub_currentBattleEntityGear->m3D[1] = battleGetSlotStatusSub_currentBattleEntityGear->m74_baseAttackMultiplier;
        battleGetSlotStatusSub_currentBattleEntityGear->m3F_attackMultiplier += battleGetSlotStatusSub_currentBattleEntityGear->m55_accessoryBonuses[1];
        battleGetSlotStatusSub_currentBattleEntityGear->m3D[1] += battleGetSlotStatusSub_currentBattleEntityGear->m55_accessoryBonuses[1];
        if (battleGetSlotStatusSub_currentBattleEntity->m0_base.m84_statusFlags2[3] & 0x2000) {
            for (j = 0; j < 38; j++) {
                pBattleEntities->m1058_partyBattleStats[i][j].m13_cost <<= 1;
            }
            for (j = 0; j < 42; j++) {
                pBattleEntities->m2228_mechaBattleStats[i][j].m13_cost <<= 1;
            }
        }
        if (battleGetSlotStatusSub_currentBattleEntity->m0_base.m32_accessoryFlags & 0x4000) {
            for (j = 0; j < 38; j++) {
                pBattleEntities->m1058_partyBattleStats[i][j].m13_cost = (pBattleEntities->m1058_partyBattleStats[i][j].m13_cost + 1) >> 1;
            }
            for (j = 0; j < 42; j++) {
                pBattleEntities->m2228_mechaBattleStats[i][j].m13_cost = (pBattleEntities->m2228_mechaBattleStats[i][j].m13_cost + 1) >> 1;
            }
        }
        if (battleGetSlotStatusSub_currentBattleEntity->m0_base.m62_Level >= 50) {
            gameState.m16C0_abilities[battleGetSlotStatusSub_currentBattleEntity->m0_base.m56_battleCommandLoadout].m0_unlockedAbilitiesBitField[2] |= 8;
        }
        if (battleGetSlotStatusSub_currentBattleEntity->m0_base.m56_battleCommandLoadout == 7) {
            battleGetSlotStatusSub_currentBattleEntityGear->m60_hp = battleGetSlotStatusSub_currentBattleEntity->m0_base.m4C_HP * 50;
            battleGetSlotStatusSub_currentBattleEntityGear->m64_maxHp = battleGetSlotStatusSub_currentBattleEntity->m0_base.m4E_MaxHP * 50;
            battleGetSlotStatusSub_currentBattleEntityGear->m3C_engineOutput = battleGetSlotStatusSub_currentBattleEntity->m0_base.m58_attack;
            battleGetSlotStatusSub_currentBattleEntityGear->m70_defence = battleGetSlotStatusSub_currentBattleEntity->m0_base.m59_defence * 12;
            battleGetSlotStatusSub_currentBattleEntityGear->m72_etherDefence = battleGetSlotStatusSub_currentBattleEntity->m0_base.m5C_etherDefence * 6;
            battleGetSlotStatusSub_currentBattleEntityGear->m98_agility = battleGetSlotStatusSub_currentBattleEntity->m0_base.m5A_agility;
            battleGetSlotStatusSub_currentBattleEntityGear->m7E_statusFlags1 |= 0x3C4;
            if (battleGetSlotStatusSub_currentBattleEntity->m0_base.m7C_statusFlags[3] & 0x2000) {
                battleGetSlotStatusSub_currentBattleEntityGear->m7E_statusFlags1 |= 0x1000;
            }
        }
        if (battleGetSlotStatusSub_currentBattleEntity->m0_base.mA0_gearNum == 0xF) {
            gameState.m16C0_abilities[battleGetSlotStatusSub_currentBattleEntity->m0_base.m56_battleCommandLoadout].m1A_unlockedGearAbilities &= 0x8FFF;
        }
        if (battleGetSlotStatusSub_currentBattleEntity->m0_base.mA0_gearNum == 0x12) {
            battleGetSlotStatusSub_currentBattleEntity->m0_base.m7A_commandEnabledBF = 0x238;
            battleGetSlotStatusSub_currentBattleEntityGear->m38_fuel = 0x26AC;
            battleGetSlotStatusSub_currentBattleEntityGear->m3A_maxFuel = 0x26AC;
            p148[1] = 0;
            gameState.m16C0_abilities[3].m1A_unlockedGearAbilities = 0x8000;
        }
        if (gameState.m1930_fieldVarsBackup[0] >= 0xE7 && !(gameState.m2344[0x11] & 0x80)) {
            gameState.m26C_party[9].m6A_weaponModifier[0] = 0x27;
            gameState.m26C_party[9].m4_weaponSlots[0].m0_weaponPower = 0x1E;
            gameState.m2344[0x11] |= 0x80;
        }
    }
    battlePartyMemberCount = 3;
    for (i = 0; i < 3; i++) {
        if (battleCharacters[i] == 0x7F) {
            battlePartyMemberCount--;
        }
    }
    for (i = 0; i < 3; i++) {
        pBattleEntities->m5F54_damageTaken[i] = 0;
        pBattleEntities->m5F60_gearDamageTaken[i] = 0;
    }
}

/* battle start: saves each PC's command mask (partySavedCommandEnabledBF), applies the gear slot bonuses to m98 (max 16), then fixed game-state patches */
void battleStartPartyFixups(void) {
    u8 i;
    u8 j;

    for (i = 0; i < 3; i++) {
        if (battleCharacters[i] != 0x7F) {
            battleGetSlotStatusSub_currentBattleEntity = &pBattleEntities->m0_entities[i];
            battleGetSlotStatusSub_currentBattleEntityGear = &pBattleEntities->m0_entities[i].mA4_gear;
            partySavedCommandEnabledBF[i] = battleGetSlotStatusSub_currentBattleEntity->m0_base.m7A_commandEnabledBF;
            for (j = 0; j < 4; j++) {
                if (battleEntities.m6028_terrainAgility[j] != 0) {
                    battleGetSlotStatusSub_currentBattleEntityGear->m98_agility -= battleEntities.m6028_terrainAgility[j];
                    battleGetSlotStatusSub_currentBattleEntityGear->m98_agility += battleGetSlotStatusSub_currentBattleEntityGear->m50_terrainAgility[j];
                }
            }
            if (battleGetSlotStatusSub_currentBattleEntityGear->m98_agility > 16) {
                battleGetSlotStatusSub_currentBattleEntityGear->m98_agility = 16;
            }
        }
    }
    gameState.m26C_party[8].m5A_agility = 7;
    if (gameState.m16C0_abilities[8].m1A_unlockedGearAbilities & 0x2000) {
        gameState.m16C0_abilities[8].m0_unlockedAbilitiesBitField[1] |= 0x800;
    }
    if (gameState.m1930_fieldVarsBackup[0] < 0xBB) {
        gameState.m978_gears[0].m74_baseAttackMultiplier = 10;
        gameState.m978_gears[1].m74_baseAttackMultiplier = 10;
        gameState.m978_gears[11].m74_baseAttackMultiplier = 9;
        gameState.m978_gears[12].m74_baseAttackMultiplier = 9;
        gameState.m978_gears[13].m74_baseAttackMultiplier = 8;
        gameState.m978_gears[14].m74_baseAttackMultiplier = 12;
        gameState.m978_gears[15].m74_baseAttackMultiplier = 12;
        gameState.m978_gears[13].m2_frame = 0x58;
        gameState.m978_gears[7].m3_engine = 0;
        gameState.m978_gears[15].m8_armor = 0x28;
    }
}

/* initial ATB wait of an entity: 0xA5 - 9 * (agility - command m27) (min 9 * 1, capped 0xA0) with +-4 jitter */
u8 battleGetSlotStatusSub(u8 entity, s32 unused) {
    u16 speed;

    battleGetSlotStatusSub_currentBattleEntity = &pBattleEntities->m0_entities[entity];
    battleGetSlotStatusSub_currentBattleEntityGear = &pBattleEntities->m0_entities[entity].mA4_gear;
    if (entity < 3) {
        if (!(battleGetSlotStatusSub_currentBattleEntity->m15A_flags & 0x80)) {
            currentEntityBattleStats = &pBattleEntities->m1058_partyBattleStats[entity][pBattleEntities->m5FC2_battleGetSlotStatusSub_current28Index];
            if (battleGetSlotStatusSub_currentBattleEntity->m0_base.m5A_agility > (s8)currentEntityBattleStats->m26[1]) {
                speed = (battleGetSlotStatusSub_currentBattleEntity->m0_base.m5A_agility - (s8)currentEntityBattleStats->m26[1]) * 9;
            } else {
                speed = 9;
            }
        } else {
            currentEntityBattleStats = &pBattleEntities->m2228_mechaBattleStats[entity][pBattleEntities->m5FC2_battleGetSlotStatusSub_current28Index];
            if (battleGetSlotStatusSub_currentBattleEntity->mA4_gear.m98_agility > (s8)currentEntityBattleStats->m26[1]) {
                speed = (battleGetSlotStatusSub_currentBattleEntity->mA4_gear.m98_agility - (s8)currentEntityBattleStats->m26[1]) * 9;
            } else {
                speed = 9;
            }
        }
    } else {
        speed = battleGetSlotStatusSub_currentBattleEntity->m0_base.m5A_agility * 9;
    }
    if (speed > 0xA5) {
        speed = 0xA0;
    }
    speed = 0xA5 - speed;
    speed -= rand() % 8 - 4;
    updatePlayerWithStatusC000And2();
    return speed;
}

/* resolves one attack on every entity in entitiesHitInCurrentAttackBF (applyBattleItemToEntity) */
void applyBattleItemToTargets(u8 arg0) {
    s32 mask;
    u8 i;

    initBattleAttackStatus();
    mask = 1;
    for (i = 0; i < 11; i++) {
        if (mask & pBattleEntities->m5FAC_entitiesHitInCurrentAttackBF) {
            applyBattleItemToEntity(i, arg0);
        }
        mask <<= 1;
    }
    pBattleEntities->m5FB0_performAttack_type = D_800D2204[arg0].mA_attackType;
    if (battleEntities.m5FB6_battleEffectBF & 0x8000) {
        battleEntities.m5FB0_performAttack_type = 0xC2;
    }
}

/* applies item record `item` (sBattleItemData) to entity `target` */
void applyBattleItemToEntity(u8 target, u8 item) {
    sBattleEntity* e;
    sBattleItemData* it;
    GameStateGear* gear;
    u8 done;
    u8 i;
    s32 ten;
    s32 fifty;
    s32 damage;

    e = &battleEntities.m0_entities[target];
    it = &battleEntities.m5518_items[item];
    gear = &battleEntities.m0_entities[target].mA4_gear;
    done = 0;
    ten = 10;
    if (it->mA.item.mA_flags & 0x8000) {
        fifty = 50;
        damage = it->m8_power * fifty;
        initBattleAttackStatusArray0[target] = 2;
        battleEntities.m5F6C_damageDonePerAttack[target] = damage;
        if (e->m0_base.m84_statusFlags2[1] & 0x80) {
            pBattleEntities->m5F6C_damageDonePerAttack[target] *= 2;
        }
        if (e->m0_base.m36_monsterFlags2 & 0x8000) {
            initBattleAttackStatusArray0[target] = 0;
        }
        done = 1;
    }
    if (it->mA.item.mA_flags & 0x4000) {
        damageDonePerAttack[target] = ten * it->m8_power;
        done++;
        initBattleAttackStatusArray0[target] = 3;
    }
    if (done) {
        return;
    }
    if ((battleEntities.m0_entities[target].m15A_flags & 0x80) && it->mA.item.mA_flags != 1) {
        D_800D2CA4.mB_battleTickMain_var1 = 0x30;
        BATTLE_EFFECT_FLAGS |= 0x8000;
        return;
    }
    if (it->mA.item.mA_flags & 0x2000) {
        e->m0_base.m7C_statusFlags[0] &= 0x8000;
    }
    if (it->mA.item.mA_flags & 0x1000) {
        e->m0_base.m7C_statusFlags[2] = 0;
        e->m0_base.m7A_commandEnabledBF &= ~0x20;
    }
    if (it->mA.item.mA_flags & 0x100) {
#if !VERSION_JP
        if (battleEntities.m0_entities[target].m0_base.m56_battleCommandLoadout == 2) {
            startBattleAttackAnimationSub0(target, 0);
        }
#endif
        e->m0_base.m7C_statusFlags[0] = 0;
        e->m0_base.m7C_statusFlags[2] = 0;
        e->m0_base.m84_statusFlags2[0] = 0;
        e->m0_base.m84_statusFlags2[2] = 0;
        e->m0_base.m84_statusFlags2[4] = 0;
        e->m0_base.m4C_HP = e->m0_base.m4E_MaxHP * it->m8_power / 10;
        pBattleEntities->m5FB6_battleEffectBF |= 1 << target;
    }
    if (it->mA.item.mA_flags & 0x800) {
        e->m0_base.m84_statusFlags2[0] |= it->mA.item.mC_status;
        startBattleAttackAnimationVar8_2Sub1(target, 5, it->mA.item.mC_status, 5);
        setStatusEffectMessage(5, it->mA.item.mC_status);
    }
    if (it->mA.item.mA_flags & 0x400) {
        e->m0_base.m84_statusFlags2[2] |= it->mA.item.mC_status;
        startBattleAttackAnimationVar8_2Sub1(target, 7, it->mA.item.mC_status, 5);
        setStatusEffectMessage(7, it->mA.item.mC_status);
    }
    if (it->mA.item.mA_flags & 0x200) {
        e->m0_base.m84_statusFlags2[4] |= it->mA.item.mC_status;
        if ((it->mA.item.mC_status & 0xF000) && !(e->m0_base.m84_statusFlags2[5] & 0xF000)) {
            e->m0_base.m84_statusFlags2[4] &= 0xFFF;
            e->m0_base.m84_statusFlags2[4] |= it->mA.item.mC_status;
            startBattleAttackAnimationVar8_2Sub1(target, 9, it->mA.item.mC_status, 5);
            setStatusEffectMessage(9, it->mA.item.mC_status);
        }
        if ((it->mA.item.mC_status & 0xF00) && !(e->m0_base.m84_statusFlags2[5] & 0xF00)) {
            e->m0_base.m84_statusFlags2[4] &= 0xF0FF;
            e->m0_base.m84_statusFlags2[4] |= it->mA.item.mC_status;
            startBattleAttackAnimationVar8_2Sub1(target, 9, it->mA.item.mC_status, 5);
            setStatusEffectMessage(9, it->mA.item.mC_status);
        }
    }
    if (it->mA.item.mA_flags & 0x20) {
        e->m0_base.m7C_statusFlags[1] |= 0x3F7C;
    }
    if (it->mA.item.mA_flags & 0x10) {
        e->m0_base.m7C_statusFlags[1] |= 0xFFFE;
    }
    if (it->mA.item.mA_flags & 8) {
        e->m0_base.m84_statusFlags2[it->m8_power] = 0;
        switch (it->m8_power) {
        case 0:
            pBattleEntities->m5FC7_battleTickMain_var1 = 0x35;
            break;
        case 2:
            pBattleEntities->m5FC7_battleTickMain_var1 = 0x36;
            break;
        case 4:
            pBattleEntities->m5FC7_battleTickMain_var1 = 0x37;
            break;
        }
    }
    if ((it->mA.item.mA_flags & 0x80) && (it->mA.item.mC_status & 2)) {
        if (target >= 3) {
            pBattleEntities->m5FC7_battleTickMain_var1 = 0x33;
            return;
        }
        if (it->m9_duration == 0) {
            if (canDisableAnotherPartyMember()) {
                e->m0_base.m7C_statusFlags[0] |= it->mA.item.mC_status;
                pBattleEntities->m5FC7_battleTickMain_var1 = 0x31;
                e->m0_base.m7A_commandEnabledBF = 0xFFEF;
            } else {
                pBattleEntities->m5FC7_battleTickMain_var1 = 0x32;
            }
        } else {
            e->m0_base.m7C_statusFlags[0] &= ~it->mA.item.mC_status;
            e->m0_base.m7A_commandEnabledBF = partySavedCommandEnabledBF[target];
        }
    }
    if (it->mA.item.mA_flags == 1) {
        switch (it->m8_power) {
        case 10:
            e->m0_base.m38_weaknessFlags = 0;
            e->m0_base.m38_weaknessFlags = it->mA.item.mC_status;
            break;
        case 11:
            if (!(e->m0_base.m7C_statusFlags[1] & it->mA.item.mC_status)) {
                e->m0_base.m7C_statusFlags[0] |= it->mA.item.mC_status;
                startBattleAttackAnimationVar8_2Sub1(target, 0, it->mA.item.mC_status, it->m9_duration);
                setStatusEffectMessage(0, it->mA.item.mC_status);
            }
            break;
        case 12:
            if (!(e->m0_base.m7C_statusFlags[3] & it->mA.item.mC_status)) {
                e->m0_base.m7C_statusFlags[2] |= it->mA.item.mC_status;
                startBattleAttackAnimationVar8_2Sub1(target, 2, it->mA.item.mC_status, it->m9_duration);
                setStatusEffectMessage(2, it->mA.item.mC_status);
            }
            break;
        case 13:
            gear->m99_defenceDown += (u8)it->mA.item.mC_status;
            pBattleEntities->m5FC7_battleTickMain_var1 = 0x28;
            break;
        case 14:
            gameState.m26C_party[e->m0_base.m56_battleCommandLoadout].m44_expToNextLevel = 1;
            gameState.m26C_party[e->m0_base.m56_battleCommandLoadout].m48_expToNextLevel2 = 1;
            break;
        case 15:
            for (i = 0; i < 7; i++) {
                e->m0_base.m90_deathblowProgress[i] += 10;
            }
            break;
        }
    }
}

u8 canDisableAnotherPartyMember(void) {
    u8 result;
    u8 i;
    u8 count;

    result = 0;
    switch (battlePartyMemberCount) {
    case 1:
        break;
    case 2:
        count = 0;
        for (i = 0; i < 3; i++) {
            if (pBattleEntities->m0_entities[i].m0_base.m7C_statusFlags[0] & 0xC002) {
                count++;
            }
        }
        if (count == 0) {
            result = 1;
        }
        break;
    case 4:
        break;
    case 3:
        count = 0;
        for (i = 0; i < 3; i++) {
            if (pBattleEntities->m0_entities[i].m0_base.m7C_statusFlags[0] & 0xC002) {
                count++;
            }
        }
        if (count != 2) {
            result = 1;
        }
        break;
    }
    return result;
}

/* (switch case order + 'default: slot = 0xF' between cases gives retail's store-flag) */
void startBattleAttackAnimationVar8_2Sub1(u8 entity, u8 type, u16 flag, u8 duration) {
    u8 slot;
    sBattleEntity* e;
    u16 bits;

    slot = 0xF;
    if (battleEntities.m0_entities[startBattleAttackAnimationVar2].m0_base.m84_statusFlags2[3] & 0x2000) {
        duration *= 2;
    }
    e = &battleEntities.m0_entities[entity];
    if (type == 0) {
        switch (flag) {
        case 0x2000:
            slot = 0;
            break;
        default:
            slot = 0xF;
            break;
        case 0x1000:
            slot = 1;
            break;
        }
    }
    if (type == 2) {
        switch (flag) {
        case 0x1000:
            slot = 2;
            break;
        case 0x800:
            slot = 3;
            break;
        }
    }
    if (type == 5) {
        switch (flag) {
        case 0x8000:
            slot = 4;
            break;
        case 0x4000:
            slot = 5;
            break;
        }
    }
    if (type == 7) {
        switch (flag) {
        case 0x8000:
            slot = 9;
            break;
        case 0x4000:
            slot = 0xA;
            break;
        case 0x1000:
            slot = 0xB;
            break;
        }
    }
    if (type == 9) {
        switch (flag) {
        case 0x1000:
        case 0x2000:
        case 0x4000:
        case 0x8000:
            slot = 0xC;
            break;
        case 0x100:
        case 0x200:
        case 0x400:
        case 0x800:
            slot = 0xD;
            break;
        }
    }
    if (slot != 0xF) {
        switch (type) {
        case 5:
        case 7:
        case 9:
            bits = startBattleAttackAnimationVar5->m0_base.m84_statusFlags2[2] | startBattleAttackAnimationVar5->m0_base.m84_statusFlags2[3];
            if (bits & 5) {
                duration *= 2;
            }
            if (bits & 0xA) {
                duration /= 2;
            }
            break;
        }
        if (e->m0_base.m32_accessoryFlags & 0x40) {
            switch (type) {
            case 5:
            case 7:
            case 9:
                duration *= 2;
                break;
            }
        }
        battleEntities.m0_entities[entity].m15C_statusTimers[slot] = duration;
    }
}

/* ('x = x - 1' on u8 gives retail's addiu 0xFF; (m84&b)&&!(m86&b) folds to one lw) */
u16 updateBattleEntityStatus(u8 idx) {
    sBattleEntity* e;
    GameStateGear* gear;
    u8* timers;
    u16 r;

    e = &battleEntities.m0_entities[idx];
    gear = &battleEntities.m0_entities[idx].mA4_gear;
    timers = battleEntities.m0_entities[idx].m15C_statusTimers;
    if (battleEntities.m0_entities[idx].m15A_flags & 0x80) {
        updateGearEntityStatus(gear, &e->m0_base, timers);
        return 0;
    }
    r = 0;
    if (e->m0_base.m7C_statusFlags[0] & 0x1000) {
        timers[1] = timers[1] - 1;
        if (timers[1] == 0) {
            r |= 0x4000;
            e->m0_base.m7C_statusFlags[0] &= ~0x1000;
        }
    }
    if (e->m0_base.m7C_statusFlags[2] & 0x1000) {
        timers[2] = timers[2] - 1;
        if (timers[2] == 0) {
            r |= 0x2000;
            e->m0_base.m7C_statusFlags[2] &= ~0x1000;
        }
    }
    if (e->m0_base.m7C_statusFlags[2] & 0x800) {
        timers[3] = timers[3] - 1;
        if (timers[3] == 0) {
            r |= 0x1000;
            e->m0_base.m7C_statusFlags[2] &= ~0x800;
            e->m0_base.m7A_commandEnabledBF &= ~0x20;
        }
    }
    if ((e->m0_base.m84_statusFlags2[0] & 0x8000) && !(e->m0_base.m84_statusFlags2[1] & 0x8000)) {
        timers[4] = timers[4] - 1;
        if (timers[4] == 0) {
            r |= 0x800;
            e->m0_base.m84_statusFlags2[0] &= ~0x8000;
        }
    }
    if ((e->m0_base.m84_statusFlags2[0] & 0x4000) && !(e->m0_base.m84_statusFlags2[1] & 0x4000)) {
        timers[5] = timers[5] - 1;
        if (timers[5] == 0) {
            r |= 0x400;
            e->m0_base.m84_statusFlags2[0] &= ~0x4000;
        }
    }
    if ((e->m0_base.m84_statusFlags2[0] & 0x2000) && !(e->m0_base.m84_statusFlags2[1] & 0x2000)) {
        timers[6] = timers[6] - 1;
        if (timers[6] == 0) {
            r |= 0x200;
            e->m0_base.m84_statusFlags2[0] &= ~0x2000;
        }
    }
    if ((e->m0_base.m84_statusFlags2[0] & 0x1000) && !(e->m0_base.m84_statusFlags2[1] & 0x1000)) {
        timers[7] = timers[7] - 1;
        if (timers[7] == 0) {
            r |= 0x100;
            e->m0_base.m84_statusFlags2[0] &= ~0x1000;
        }
    }
    if ((e->m0_base.m84_statusFlags2[0] & 0x800) && !(e->m0_base.m84_statusFlags2[1] & 0x800)) {
        timers[8] = timers[8] - 1;
        if (timers[8] == 0) {
            r |= 0x80;
            e->m0_base.m84_statusFlags2[0] &= ~0x800;
        }
    }
    if ((e->m0_base.m84_statusFlags2[2] & 0x8000) && !(e->m0_base.m84_statusFlags2[3] & 0x8000)) {
        timers[9] = timers[9] - 1;
        if (timers[9] == 0) {
            r |= 0x40;
            e->m0_base.m84_statusFlags2[2] &= ~0x8000;
        }
    }
    if ((e->m0_base.m84_statusFlags2[2] & 0x4000) && !(e->m0_base.m84_statusFlags2[3] & 0x4000)) {
        timers[0xA] = timers[0xA] - 1;
        if (timers[0xA] == 0) {
            r |= 0x20;
            e->m0_base.m84_statusFlags2[2] &= ~0x4000;
        }
    }
    if ((e->m0_base.m84_statusFlags2[2] & 0x1000) && !(e->m0_base.m84_statusFlags2[3] & 0x1000)) {
        timers[0xB] = timers[0xB] - 1;
        if (timers[0xB] == 0) {
            r |= 0x10;
            e->m0_base.m84_statusFlags2[2] &= ~0x1000;
        }
    }
    if ((e->m0_base.m84_statusFlags2[4] & 0xF000) && !(e->m0_base.m84_statusFlags2[5] & 0xF000)) {
        timers[0xC] = timers[0xC] - 1;
        if (timers[0xC] == 0) {
            r |= 8;
            e->m0_base.m84_statusFlags2[4] &= 0xFFF;
        }
    }
    if ((e->m0_base.m84_statusFlags2[4] & 0xF00) && !(e->m0_base.m84_statusFlags2[5] & 0xF00)) {
        timers[0xD] = timers[0xD] - 1;
        if (timers[0xD] == 0) {
            r |= 4;
            e->m0_base.m84_statusFlags2[4] &= 0xF0FF;
        }
    }
    return r;
}

void updateGearEntityStatus(GameStateGear* gear, GameStateCharacter* base, u8* timers) {
    if (gear->m7C_statusFlags & 0x200) {
        timers[1] = timers[1] - 1;
        if (timers[1] == 0) {
            gear->m7C_statusFlags &= ~0x200;
        }
    }
    if (gear->m7C_statusFlags & 0x100) {
        timers[2] = timers[2] - 1;
        if (timers[2] == 0) {
            gear->m7C_statusFlags &= ~0x100;
        }
    }
    if (gear->m7C_statusFlags & 0x80) {
        timers[3] = timers[3] - 1;
        if (timers[3] == 0) {
            gear->m7C_statusFlags &= ~0x80;
        }
    }
    if (gear->m7C_statusFlags & 0x20) {
        timers[4] = timers[4] - 1;
        if (timers[4] == 0) {
            gear->m7C_statusFlags &= ~0x20;
            base->m7C_statusFlags[0] &= ~0x1000;
        }
    }
    if (gear->m7C_statusFlags & 0x10) {
        timers[5] = timers[5] - 1;
        if (timers[5] == 0) {
            gear->m7C_statusFlags &= ~0x10;
        }
    }
    if (gear->m7C_statusFlags & 0xF000) {
        timers[7] = timers[7] - 1;
        if (timers[7] == 0) {
            gear->m7C_statusFlags &= 0xFFF;
        }
    }
    if (gear->m7C_statusFlags & 0xF00) {
        timers[8] = timers[8] - 1;
        if (timers[8] == 0) {
            gear->m7C_statusFlags &= 0xF0FF;
        }
    }
    if (gear->m80_statusFlags2 & 0x1000) {
        timers[0xA] = timers[0xA] - 1;
        if (timers[0xA] == 0) {
            gear->m80_statusFlags2 &= ~0x1000;
        }
    }
    if (gear->m80_statusFlags2 & 0x40) {
        timers[0xB] = timers[0xB] - 1;
        if (timers[0xB] == 0) {
            gear->m80_statusFlags2 &= ~0x40;
        }
    }
    if (gear->m80_statusFlags2 & 0x20) {
        timers[0xC] = timers[0xC] - 1;
        if (timers[0xC] == 0) {
            gear->m80_statusFlags2 &= ~0x20;
        }
    }
}

void recordLastAttackStats(void) {
    u16 bits;

    bits = (battleGetSlotStatusSub_currentBattleEntity->m0_base.m84_statusFlags2[4] | battleGetSlotStatusSub_currentBattleEntity->m0_base.m84_statusFlags2[5]) >> 12;
    pBattleEntities->m5FBC = currentEntityBattleStats->m20;
    pBattleEntities->m5FBD = currentEntityBattleStats->m21;
    pBattleEntities->m5FBE_lastAttackElements = currentEntityBattleStats->m22_elementFlags;
    pBattleEntities->m5FBF = currentEntityBattleStats->m23;
    pBattleEntities->m5FC0_lastAttackIndex = pBattleEntities->m5FC2_battleGetSlotStatusSub_current28Index;
    if (!(pBattleEntities->m5FBE_lastAttackElements & 0x3F)) {
        pBattleEntities->m5FBE_lastAttackElements |= bits;
    }
}

void expandHitMaskToSide(void) {
    if (pBattleEntities->m5FAC_entitiesHitInCurrentAttackBF & 7) {
        pBattleEntities->m5FAC_entitiesHitInCurrentAttackBF = pBattleEntities->m5FAE_targetableMask & 7;
    }
    if (pBattleEntities->m5FAC_entitiesHitInCurrentAttackBF & 0x7F8) {
        pBattleEntities->m5FAC_entitiesHitInCurrentAttackBF = pBattleEntities->m5FAE_targetableMask & 0x7F8;
    }
}

u8 getEntityStatusAnimationIndex(u8 idx) {
    sBattleEntity* e;
    u16 s;

    e = &pBattleEntities->m0_entities[idx];
    s = e->m0_base.m7C_statusFlags[0];
    if (s & 0x4000) {
        return 8;
    }
    if (s & 0x8000) {
        return 1;
    }
    if (s & 0x2000) {
        return 2;
    }
    if (e->m0_base.m7C_statusFlags[2] & 0x1000) {
        return 3;
    }
    if (e->m0_base.m7C_statusFlags[2] & 0x2000) {
        return 4;
    }
    if (s & 0x800) {
        return 5;
    }
    if (s & 0x1000) {
        return 6;
    }
    if (s & 2) {
        return 3;
    }
    if ((e->m0_base.m84_statusFlags2[0] | e->m0_base.m84_statusFlags2[1]) & 0x8000) {
        return 7;
    }
    if (e->m0_base.m4C_HP < (e->m0_base.m4E_MaxHP >> 3)) {
        return 5;
    }
    return 0;
}

u16 getEntityStatusEffectBitmask(u8 idx) {
    sBattleEntity* e;
    u16 r;
    u16 s;
    u16 bits;

    e = &pBattleEntities->m0_entities[idx];
    r = 0;
    s = e->m0_base.m7C_statusFlags[0];
    if (s & 0x8000) {
        return 0;
    }
    if (e->m0_base.m7C_statusFlags[2] & 0x1000) {
        r = 0x8000;
    }
    if (e->m0_base.m7C_statusFlags[2] & 0x2000) {
        r |= 0x4000;
    }
    if (s & 0x800) {
        r |= 0x2000;
    }
    bits = e->m0_base.m84_statusFlags2[4] | e->m0_base.m84_statusFlags2[5];
    if (bits & 0xF00) {
        r |= (bits & 0xF00) >> 3;
    }
    if (bits & 0xF000) {
        r |= (bits & 0xF000) >> 3;
    }
    return r;
}

u8 getAbilityHitChance(u8 entity, u8 command) {
    u8 v;
    sBattle800CDD40Sub* p;
    sBattleEntity* e;

    p = &pBattleEntities->m1058_partyBattleStats[entity][command];
    e = &pBattleEntities->m0_entities[entity];
    v = p->m14_etherHitBonus + e->m0_base.m5B_ether;
    if (v > 100) {
        v = 100;
    }
    return v;
}

/* per-turn gear state (BattleGearTurn at 0x800D2C0C): fuel costs, refuel
 * amount, status flags, infinity (mode 4) chance and countdown */
void setupGearTurnState(u8 idx) {
    sBattleEntity* e = &battleEntities.m0_entities[idx];
    GameStateGear* gear = &battleEntities.m0_entities[idx].mA4_gear;
    u8* mode = &battleEntities.m0_entities[idx].m148_gearMode;
    sBattle800CDD40Sub* stats = battleEntities.m2228_mechaBattleStats[idx];
    BattleGearTurn* t = &battleEntities.m5F24_gearTurn;
    u8 i;
    u8 chance;

    /* At full HP retail increments an unset chance: $s1, which the caller
     * setupGearTurnHud holds at the entity index (battle_event passes 0;
     * PCSX-Redux, a selector gear battle: $s1 == idx at every such turn). */
    chance = idx;
    for (i = 0; i < 15; i++) {
        t->m0_fuelCosts[i] = stats->m24_fuelCost;
        stats++;
    }
    if (gear->m55_accessoryBonuses[2] != 0) {
        t->m26_refuel = gear->m55_accessoryBonuses[2] * t->m0_fuelCosts[0];
    } else {
        t->m26_refuel = 30;
    }
    switch (*mode) {
    case 1:
    case 2:
    case 3:
        t->m26_refuel += *mode * 20;
        break;
    case 4:
        t->m26_refuel *= 10;
        break;
    }
    t->m1E_attackPower = gear->m3C_engineOutput * gear->m3F_attackMultiplier;
    t->m1E_attackPower += gear->m12_slots[0].m0_weaponPower;
    if (e->m0_base.m56_battleCommandLoadout == 4) {
        t->m1E_attackPower += gear->m12_slots[2].m0_weaponPower;
    }
    t->m29_agility = gear->m98_agility;
    t->m2A_defenceDown = gear->m99_defenceDown;
    if (gear->m64_maxHp != gear->m60_hp) {
        chance = R3000_DIV((u32)(gear->m64_maxHp - gear->m60_hp), (u32)gear->m64_maxHp / 10);
        if (chance == 0) {
            chance++;
        }
    } else {
        chance++;
    }
    chance = chance * (e->m0_base.m54_infinityCharge + 5);
    if (!(gameState.m22B6_gameFlags & 0x4000)) {
        chance = 0;
    }
    if (e->m0_base.m62_Level < 50) {
        chance = 0;
    }
    if (e->m0_base.mA0_gearNum == 3) {
        chance = 0;
    }
    if (e->m0_base.mA0_gearNum == 15) {
        chance = 99;
    }
    if (chance >= 100) {
        chance = 99;
    }
    t->m2E_chance = chance;
    t->m24_statusFlags = 0;
    if (gear->m7C_statusFlags & 0x100) {
        t->m24_statusFlags = 0x8000;
    }
    if (gear->m7C_statusFlags & 0x200) {
        t->m24_statusFlags |= 0x4000;
    }
    if (gear->m7C_statusFlags & 0x80) {
        t->m24_statusFlags |= 0x2000;
    }
    if (gear->m7C_statusFlags & 0x10) {
        t->m24_statusFlags |= 0x1000;
    }
    if (gear->m38_fuel < gear->m3A_maxFuel >> 3) {
        t->m24_statusFlags |= 0x800;
    } else {
        t->m24_statusFlags &= ~0x800;
    }
    if (gear->m38_fuel == 0) {
        gear->m80_statusFlags2 &= 0x7FFF;
        e->m0_base.m84_statusFlags2[0] &= 0x7FFF;
    }
    if (gear->m80_statusFlags2 & 0x8000) {
        t->m2C_boosterActive = 1;
    } else {
        t->m2C_boosterActive = 0;
    }
    if (*mode == 4) {
        t->m28_mode = 4;
        if (--battleEntities.m0_entities[idx].m15C_statusTimers[6] == 0) {
            gear->m80_statusFlags2 &= ~0x4000;
            *mode = 0;
            e->m0_base.m54_infinityCharge = 0;
        }
    } else {
        t->m28_mode = *mode;
        if (*mode == 3 && (gameState.m22B6_gameFlags & 0x4000) && rand() % 100 < chance) {
            gear->m80_statusFlags2 |= 0x4000;
            battleEntities.m0_entities[idx].m15C_statusTimers[6] = 3;
            if (battleEntities.m0_entities[idx].m0_base.m32_accessoryFlags & 0x40) {
                battleEntities.m0_entities[idx].m15C_statusTimers[6] = 6;
            }
            *mode += 1;
        }
    }
}

u8 func_8009A7B8(u8 idx) {
    return gameState.m26C_party[idx].m4_weaponSlots[0].m1[0];
}

/* 1 if Billy has ammo idx + 50 (item record 48 + idx) loaded in a gun slot;
 * &items[48] then [idx], so 48 * 16 folds into the lbu offset */
s32 isBillyAmmoLoaded(u8 idx) {
    u8 id = (&pBattleEntities->m5518_items[48])[idx].m6_id;

    if (gameState.m26C_party[4].m4_weaponSlots[0].m1[1] == id || gameState.m26C_party[4].m4_weaponSlots[1].m1[1] == id
        || gameState.m26C_party[4].m4_weaponSlots[2].m1[1] == id) {
        return 1;
    }
    return gameState.m26C_party[4].m4_weaponSlots[3].m1[1] == id;
}

/* Billy's ammo: loads ammo record id into the gun slot that already holds
 * it (else into slot), for gameState's Billy and battle entities with
 * loadout 4. The character's first 0x20 bytes are four 8-byte weapon
 * records (u16 attack, m2, m3 type, m4 power, m5, m6 id, m7): record k's
 * bytes 2/3/4/6 are m4[k].m1[-3], m1[-2], m0, m1[1]. Written as
 * decl-based ARRAY_REFs so gameState+off folds into each %lo (m4[k - 1].m7
 * does not match) and the stores keep their order. */
void loadBillyAmmo(u8 id, u8 slot) {
    sBattleItemData* e = &pBattleEntities->m5518_items[id - 2];
    sBattleEntity* ent;
    u8 i;

    if (gameState.m26C_party[4].m4_weaponSlots[0].m1[1] == e->m6_id) {
        slot = 0;
    }
    if (gameState.m26C_party[4].m4_weaponSlots[1].m1[1] == e->m6_id) {
        slot = 1;
    }
    if (gameState.m26C_party[4].m4_weaponSlots[2].m1[1] == e->m6_id) {
        slot = 2;
    }
    if (gameState.m26C_party[4].m4_weaponSlots[3].m1[1] == e->m6_id) {
        slot = 3;
    }
    gameState.m26C_party[4].m4_weaponSlots[slot].m0_weaponPower = e->mA.ammo.mC_power;
    gameState.m26C_party[4].m4_weaponSlots[slot].m1[-2] = e->mA.ammo.mB_statusType;
    gameState.m26C_party[4].m4_weaponSlots[slot].m1[-3] = e->mA.ammo.mA_statusChance;
    gameState.m26C_party[4].m4_weaponSlots[slot].m1[-2] = e->mA.ammo.mB_statusType;
    gameState.m26C_party[4].m6F_equipment[slot] = id;
    gameStateAmmoByWeaponItem[id] = e->m3_ammoCount;
    gameState.m26C_party[4].m6F_equipment[slot] = id;
    for (i = 0; i < 3; i++) {
        ent = &pBattleEntities->m0_entities[i];
        if (ent->m0_base.m56_battleCommandLoadout == 4) {
            ent->m0_base.m4_weaponSlots[slot].m0_weaponPower = e->mA.ammo.mC_power;
            ent->m0_base.m4_weaponSlots[slot].m1[-2] = e->mA.ammo.mB_statusType;
            ent->m0_base.m4_weaponSlots[slot].m1[-3] = e->mA.ammo.mA_statusChance;
            ent->m0_base.m4_weaponSlots[slot].m1[-2] = e->mA.ammo.mB_statusType;
            ent->m0_base.m6F_equipment[slot] = id;
        }
    }
}

s32 tryToEscape(u8 entity) { /* the callers pass the current entity; unused */
    pBattleEntities->m5FC2_battleGetSlotStatusSub_current28Index = 0;
    if (rand() % 100 < 50) {
        transferCharStatsBackToField();
        return 1;
    }
    return 0;
}

void characterPerformDefendAction(u8 idx) {
    pBattleEntities->m5FC2_battleGetSlotStatusSub_current28Index = 0;
    pBattleEntities->m0_entities[idx].m15A_flags |= 1;
    if ((pBattleEntities->m0_entities[idx].m15A_flags & 0x80) && (pBattleEntities->m0_entities[idx].mA4_gear.m82_statusFlags3 & 0x10)) {
        pBattleEntities->m0_entities[idx].mA4_gear.m7C_statusFlags &= 0xFE4F;
        pBattleEntities->m0_entities[idx].m0_base.m7C_statusFlags[0] &= 0xEFFF;
    }
    if (battleEntities.m5F24_gearTurn.m28_mode == 4) {
        pBattleEntities->m5FC7_battleTickMain_var1 = 0x3D;
    }
}

void clearEntityFlag15A_1(u8 idx) {
    pBattleEntities->m0_entities[idx].m15A_flags &= 0xFE;
}

void setEtherAbilitiesTargetAll(u8 idx) {
    u16 v;
    sBattle800CDD40Sub* s;
    sBattle800CDD40Sub* p;

    if (pBattleEntities->m0_entities[idx].m15A_flags & 0x80) {
        v = pBattleEntities->m0_entities[idx].mA4_gear.m80_statusFlags2 & 0x2000;
    } else {
        v = pBattleEntities->m0_entities[idx].m0_base.m84_statusFlags2[2] & 0x400;
    }
    if (v) {
        if (pBattleEntities->m0_entities[idx].m15A_flags & 0x80) {
            s = pBattleEntities->m2228_mechaBattleStats[idx];
            s[21].m0_flags = 1;
            s[23].m0_flags = 1;
            s[24].m0_flags = 1;
            s[25].m0_flags = 1;
            s[26].m0_flags = 1;
            s[27].m0_flags = 1;
            s[28].m0_flags = 1;
        } else {
            p = pBattleEntities->m1058_partyBattleStats[idx];
            p[22].m0_flags = 1;
            p[24].m0_flags = 1;
            p[25].m0_flags = 1;
            p[26].m0_flags = 1;
            p[27].m0_flags = 1;
            p[28].m0_flags = 1;
            p[29].m0_flags = 1;
            p[30].m0_flags = 1;
            p[31].m0_flags = 1;
            p[32].m0_flags = 1;
        }
    }
}

#if VERSION_JP
void startBattleAttackAnimationSub0(u8 idx) {
#else
void startBattleAttackAnimationSub0(u8 idx, u8 arg1) {
#endif
    u16 v;
    sBattle800CDD40Sub* s;
    sBattle800CDD40Sub* p;

#if VERSION_JP
    if (currentEntityBattleStats->mA_attackFlags & 0x100) {
#else
    if (arg1 == 0 || (currentEntityBattleStats->mA_attackFlags & 0x100)) {
#endif
        if (pBattleEntities->m0_entities[idx].m15A_flags & 0x80) {
            v = pBattleEntities->m0_entities[idx].mA4_gear.m80_statusFlags2 & 0x2000;
        } else {
            v = pBattleEntities->m0_entities[idx].m0_base.m84_statusFlags2[2] & 0x400;
        }
        if (v) {
            if (pBattleEntities->m0_entities[idx].m15A_flags & 0x80) {
                s = pBattleEntities->m2228_mechaBattleStats[idx];
                s[21].m0_flags = 0x2000;
                s[23].m0_flags = 0x2000;
                s[24].m0_flags = 0x2000;
                s[25].m0_flags = 0x2000;
                s[26].m0_flags = 0x2000;
                s[27].m0_flags = 0x2000;
                s[28].m0_flags = 0x2000;
                pBattleEntities->m0_entities[idx].mA4_gear.m80_statusFlags2 &= ~0x2000;
            } else {
                p = pBattleEntities->m1058_partyBattleStats[idx];
                p[22].m0_flags = 0x2000;
                p[24].m0_flags = 0x2000;
                p[25].m0_flags = 0x2000;
                p[26].m0_flags = 0x2000;
                p[27].m0_flags = 0x2000;
                p[28].m0_flags = 0x2000;
                p[29].m0_flags = 0x2000;
                p[30].m0_flags = 0x2000;
                p[31].m0_flags = 0x2000;
                p[32].m0_flags = 0x2000;
                pBattleEntities->m0_entities[idx].m0_base.m84_statusFlags2[2] &= ~0x400;
            }
        }
    }
}

/* per-turn regen amounts: out[0] HP, out[1] MP, out[2] fuel; 1 if any */
u8 hasPassiveModifierEndOfTurn(u8 idx, s32* out) {
    sBattleEntity* e;
    u16 s;
    u8 r;

    e = &pBattleEntities->m0_entities[idx];
    s = e->m0_base.m7C_statusFlags[0];
    r = 0;
    if (s & 0x8000) {
        return 0;
    }
    if (s & 0x800) {
        r = 1;
        *out = (u16)(e->m0_base.m4E_MaxHP / 20);
    }
    out++;
    if (e->m0_base.m7C_statusFlags[2] & 0x200) {
        r = 1;
        *out = (u16)(e->m0_base.m52_MaxMP / 20);
    }
    if (e->mA4_gear.m7C_statusFlags & 0x200) {
        r = 1;
        *out = (u16)(e->m0_base.m52_MaxMP / 20);
    }
    out++;
    *out = 0;
    if (e->mA4_gear.m7C_statusFlags & 0x80) {
        r = 1;
        *out = (u16)(e->mA4_gear.m3A_maxFuel / 50);
    }
    if (e->mA4_gear.m80_statusFlags2 & 0x8000) {
        r = 1;
        *out += (u16)(e->mA4_gear.m3A_maxFuel / 50);
    }
    return r;
}

void callGearClearStatus(u8 idx) {
    sBattleEntity* e;
    u8 i;

    pBattleEntities->m5FC2_battleGetSlotStatusSub_current28Index = 0;
    e = &pBattleEntities->m0_entities[idx];
    e->m0_base.m7C_statusFlags[0] = 0;
    e->m0_base.m84_statusFlags2[0] = 0;
    e->m0_base.m84_statusFlags2[2] = 0;
    e->m0_base.m84_statusFlags2[4] = 0;
    e->m0_base.m7C_statusFlags[2] &= 0x2000;
    if (e->m0_base.m56_battleCommandLoadout == 7) {
        initGearHpFromCharacterHp(idx, e);
    }
    if (e->m0_base.m56_battleCommandLoadout == 3) {
        for (i = 3; i < 11; i++) {
            battleEntities.m0_entities[i].m0_base.m7C_statusFlags[2] &= ~0x20;
        }
    }
}

/* spends one round of the ammo loaded for the current command slot */
void spendBillyAmmo(void) {
    switch (pBattleEntities->m5FC2_battleGetSlotStatusSub_current28Index) {
    case 0:
    case 1:
    case 2:
    case 3:
        if (gameStateAmmoByWeaponItem[battleGetSlotStatusSub_currentBattleEntity->m0_base.m6F_equipment[0]] != 0) {
            gameStateAmmoByWeaponItem[battleGetSlotStatusSub_currentBattleEntity->m0_base.m6F_equipment[0]] = gameStateAmmoByWeaponItem[battleGetSlotStatusSub_currentBattleEntity->m0_base.m6F_equipment[0]] - 1;
        }
        break;
    case 4:
    case 5:
        break;
    case 7:
    case 8:
    case 9:
    case 10:
    case 11:
    case 12:
    case 13:
    case 14:
    case 15:
    case 16:
    case 17:
    case 18:
    case 19:
        if (gameStateAmmoByWeaponItem[battleGetSlotStatusSub_currentBattleEntity->m0_base.m6F_equipment[0]] != 0) {
            gameStateAmmoByWeaponItem[battleGetSlotStatusSub_currentBattleEntity->m0_base.m6F_equipment[0]] = gameStateAmmoByWeaponItem[battleGetSlotStatusSub_currentBattleEntity->m0_base.m6F_equipment[0]] - 1;
        }
    case 6:
        if (gameStateAmmoByWeaponItem[battleGetSlotStatusSub_currentBattleEntity->m0_base.m6F_equipment[3]] != 0) {
            gameStateAmmoByWeaponItem[battleGetSlotStatusSub_currentBattleEntity->m0_base.m6F_equipment[3]] = gameStateAmmoByWeaponItem[battleGetSlotStatusSub_currentBattleEntity->m0_base.m6F_equipment[3]] - 1;
        }
        break;
    }
}

void initForcedGearBattlePartyStats(void) {
    u8 i;
    sBattleEntity* e;

    for (i = 1; i < 3; i++) {
        e = &pBattleEntities->m0_entities[i];
        e->m0_base.m4C_HP = 100;
        e->m0_base.m4E_MaxHP = 100;
        e->m0_base.m5E_hitPercentage = 20;
        e->m0_base.m5F_evadePercentage = 15;
        e->m0_base.m7A_commandEnabledBF = 0x1FBF;
        e->m149[0] = 0;
    }
}

/* gear HP = character HP * 50, capped at 99999 */
void initGearHpFromCharacterHp(u8 idx, sBattleEntity* e) {
    GameStateGear* gear = &pBattleEntities->m0_entities[idx].mA4_gear;

    if (e->m0_base.m4E_MaxHP * 50 > 99999) {
        if (e->m0_base.m4C_HP * 50 > 99999) {
            gear->m60_hp = 99999;
        } else {
            gear->m60_hp = e->m0_base.m4C_HP * 50;
        }
        gear->m64_maxHp = 99999;
    } else {
        gear->m60_hp = e->m0_base.m4C_HP * 50;
        gear->m64_maxHp = e->m0_base.m4E_MaxHP * 50;
    }
}

/* debug: every inventory0 item x99 and all abilities unlocked */
void battleSetDebugInventory(void) {
    u8 i;

    for (i = 1; i < 48; i++) {
        gameState.m2026_inventory0Ids[i] = i;
        gameState.m1F90_inventory0Counts[i] = 99;
    }
    gameState.m16C0_abilities[0].m0_unlockedAbilitiesBitField[0] = 0xFFF8;
    gameState.m16C0_abilities[0].m0_unlockedAbilitiesBitField[1] = 0xFF00;
    gameState.m16C0_abilities[0].m0_unlockedAbilitiesBitField[2] = 0xFFFF;
    gameState.m16C0_abilities[0].m0_unlockedAbilitiesBitField[3] = 0xFE00;
    gameState.m16C0_abilities[0].m1A_unlockedGearAbilities = 0xF000;
    gameState.m16C0_abilities[0].m17_energyPerTurn = 7;
    gameState.m16C0_abilities[1].m0_unlockedAbilitiesBitField[0] = 0xFFE0;
    gameState.m16C0_abilities[1].m0_unlockedAbilitiesBitField[1] = 0xFFF0;
    gameState.m16C0_abilities[1].m0_unlockedAbilitiesBitField[2] = 0xFFFF;
    gameState.m16C0_abilities[1].m0_unlockedAbilitiesBitField[3] = 0xFFF0;
    gameState.m16C0_abilities[1].m1A_unlockedGearAbilities = 0xC000;
    gameState.m16C0_abilities[1].m17_energyPerTurn = 7;
    gameState.m16C0_abilities[2].m0_unlockedAbilitiesBitField[0] = 0xFFE0;
    gameState.m16C0_abilities[2].m0_unlockedAbilitiesBitField[1] = 0xFFE0;
    gameState.m16C0_abilities[2].m0_unlockedAbilitiesBitField[2] = 0xFFFF;
    gameState.m16C0_abilities[2].m0_unlockedAbilitiesBitField[3] = 0xFF00;
    gameState.m16C0_abilities[2].m1A_unlockedGearAbilities = 0x8000;
    gameState.m16C0_abilities[2].m17_energyPerTurn = 7;
    gameState.m16C0_abilities[3].m0_unlockedAbilitiesBitField[0] = 0xFFE0;
    gameState.m16C0_abilities[3].m0_unlockedAbilitiesBitField[1] = 0xFFC0;
    gameState.m16C0_abilities[3].m0_unlockedAbilitiesBitField[2] = 0xFFFF;
    gameState.m16C0_abilities[3].m0_unlockedAbilitiesBitField[3] = 0xFF00;
    gameState.m16C0_abilities[3].m1A_unlockedGearAbilities = 0xF000;
    gameState.m16C0_abilities[3].m17_energyPerTurn = 7;
    gameState.m16C0_abilities[4].m0_unlockedAbilitiesBitField[0] = 0xFFC0;
    gameState.m16C0_abilities[4].m0_unlockedAbilitiesBitField[1] = 0xFFC0;
    gameState.m16C0_abilities[4].m0_unlockedAbilitiesBitField[2] = 0xFFFF;
    gameState.m16C0_abilities[4].m0_unlockedAbilitiesBitField[3] = 0xFC00;
    gameState.m16C0_abilities[4].m1A_unlockedGearAbilities = 0xE000;
    gameState.m16C0_abilities[4].m17_energyPerTurn = 7;
    gameState.m16C0_abilities[5].m0_unlockedAbilitiesBitField[0] = 0xFFC0;
    gameState.m16C0_abilities[5].m0_unlockedAbilitiesBitField[1] = 0xF000;
    gameState.m16C0_abilities[5].m0_unlockedAbilitiesBitField[2] = 0xFFFF;
    gameState.m16C0_abilities[5].m0_unlockedAbilitiesBitField[3] = 0xF000;
    gameState.m16C0_abilities[5].m1A_unlockedGearAbilities = 0x8000;
    gameState.m16C0_abilities[5].m17_energyPerTurn = 7;
    gameState.m16C0_abilities[6].m0_unlockedAbilitiesBitField[0] = 0xFFC0;
    gameState.m16C0_abilities[6].m0_unlockedAbilitiesBitField[1] = 0xFF00;
    gameState.m16C0_abilities[6].m0_unlockedAbilitiesBitField[2] = 0xFFFF;
    gameState.m16C0_abilities[6].m0_unlockedAbilitiesBitField[3] = 0xFF00;
    gameState.m16C0_abilities[6].m1A_unlockedGearAbilities = 0x8000;
    gameState.m16C0_abilities[6].m17_energyPerTurn = 7;
    gameState.m16C0_abilities[7].m0_unlockedAbilitiesBitField[0] = 0x0;
    gameState.m16C0_abilities[7].m0_unlockedAbilitiesBitField[1] = 0xFF00;
    gameState.m16C0_abilities[7].m0_unlockedAbilitiesBitField[2] = 0x0;
    gameState.m16C0_abilities[7].m0_unlockedAbilitiesBitField[3] = 0xFF00;
    gameState.m16C0_abilities[7].m1A_unlockedGearAbilities = 0x0;
    gameState.m16C0_abilities[7].m17_energyPerTurn = 7;
    gameState.m16C0_abilities[8].m0_unlockedAbilitiesBitField[0] = 0x0;
    gameState.m16C0_abilities[8].m0_unlockedAbilitiesBitField[1] = 0xF800;
    gameState.m16C0_abilities[8].m0_unlockedAbilitiesBitField[2] = 0xFFFF;
    gameState.m16C0_abilities[8].m0_unlockedAbilitiesBitField[3] = 0x0;
    gameState.m16C0_abilities[8].m1A_unlockedGearAbilities = 0xE000;
    gameState.m16C0_abilities[8].m17_energyPerTurn = 7;
    gameState.m16C0_abilities[9].m0_unlockedAbilitiesBitField[0] = 0xFFE0;
    gameState.m16C0_abilities[9].m0_unlockedAbilitiesBitField[1] = 0xFFE0;
    gameState.m16C0_abilities[9].m0_unlockedAbilitiesBitField[2] = 0xFFFF;
    gameState.m16C0_abilities[9].m0_unlockedAbilitiesBitField[3] = 0xFFE0;
    gameState.m16C0_abilities[9].m1A_unlockedGearAbilities = 0x8000;
    gameState.m16C0_abilities[9].m17_energyPerTurn = 7;
    gameState.m16C0_abilities[10].m0_unlockedAbilitiesBitField[0] = 0xFFC0;
    gameState.m16C0_abilities[10].m0_unlockedAbilitiesBitField[1] = 0xFF00;
    gameState.m16C0_abilities[10].m0_unlockedAbilitiesBitField[2] = 0xFFFF;
    gameState.m16C0_abilities[10].m0_unlockedAbilitiesBitField[3] = 0xFF00;
    gameState.m16C0_abilities[10].m1A_unlockedGearAbilities = 0xC000;
    gameState.m16C0_abilities[10].m17_energyPerTurn = 7;
}

/* raises a damage value by how many party members are below 1/2 and 1/4 HP, then a random crit */
void applyPinchAndCriticalBonus(u16* damage) {
    u8 count;
    u8 i;
    sBattleEntity* e;
    GameStateGear* gear;
    s32 chance;
    s32 mul;
    u16 bits;

    count = 0;
    for (i = 0; i < 3; i++) {
        e = &pBattleEntities->m0_entities[i];
        gear = &e->mA4_gear;
        if (e->m0_base.m56_battleCommandLoadout == 0) {
            if (battleEntities.m0_entities[i].m15A_flags & 0x80) {
                if ((u32)e->mA4_gear.m60_hp < (u32)e->mA4_gear.m64_maxHp / 2) {
                    count++;
                }
                if ((u32)e->mA4_gear.m60_hp < (u32)e->mA4_gear.m64_maxHp / 4) {
                    count++;
                }
            } else {
                if (e->m0_base.m4C_HP < e->m0_base.m4E_MaxHP / 2) {
                    count++;
                }
                if (e->m0_base.m4C_HP < e->m0_base.m4E_MaxHP / 4) {
                    count++;
                }
            }
        }
        if (e->m0_base.m56_battleCommandLoadout == 3) {
            if (battleEntities.m0_entities[i].m15A_flags & 0x80) {
                if ((u32)gear->m60_hp < (u32)gear->m64_maxHp / 2) {
                    count++;
                }
                if ((u32)gear->m60_hp < (u32)gear->m64_maxHp / 4) {
                    count++;
                }
            } else {
                if (e->m0_base.m4C_HP < e->m0_base.m4E_MaxHP / 2) {
                    count++;
                }
                if (e->m0_base.m4C_HP < e->m0_base.m4E_MaxHP / 4) {
                    count++;
                }
            }
        }
    }
    if (count) {
        *damage += count * (*damage >> 1);
    }
    chance = 10;
    mul = 3;
    bits = battleGetSlotStatusSub_currentBattleEntity->m0_base.m32_accessoryFlags;
    if (bits & 0x400) {
        mul = 4;
    }
    if (bits & 0x200) {
        chance = 60;
    }
    if (rand() % 100 < chance) {
        *damage = (mul * *damage) >> 1;
    }
}

/* status-effect message (battleTickMain_var1) for a status type / flag */
void setStatusEffectMessage(u8 type, u16 flag) {
    switch (type) {
    case 0:
        switch (flag) {
        case 0x2000:
            pBattleEntities->m5FC7_battleTickMain_var1 = 0x1;
            break;
        case 0x1000:
            pBattleEntities->m5FC7_battleTickMain_var1 = 0x2;
            break;
        case 0x800:
            pBattleEntities->m5FC7_battleTickMain_var1 = 0x3;
            break;
        case 0x400:
            pBattleEntities->m5FC7_battleTickMain_var1 = 0x4;
            break;
        case 0x200:
            pBattleEntities->m5FC7_battleTickMain_var1 = 0x5;
            break;
        case 0x1:
            pBattleEntities->m5FC7_battleTickMain_var1 = 0x8;
            break;
        }
        break;
    case 2:
        switch (flag) {
        case 0x2000:
            pBattleEntities->m5FC7_battleTickMain_var1 = 0x9;
            break;
        case 0x1000:
            pBattleEntities->m5FC7_battleTickMain_var1 = 0xA;
            break;
        case 0x800:
            pBattleEntities->m5FC7_battleTickMain_var1 = 0xB;
            break;
        case 0x400:
            pBattleEntities->m5FC7_battleTickMain_var1 = 0xC;
            break;
        case 0x1:
            pBattleEntities->m5FC7_battleTickMain_var1 = 0xD;
            break;
        case 0x20:
            pBattleEntities->m5FC7_battleTickMain_var1 = 0x7;
            break;
        }
        break;
    case 5:
        switch (flag) {
        case 0x8000:
            pBattleEntities->m5FC7_battleTickMain_var1 = 0xE;
            break;
        case 0x4000:
            pBattleEntities->m5FC7_battleTickMain_var1 = 0xF;
            break;
        case 0x2000:
            pBattleEntities->m5FC7_battleTickMain_var1 = 0x10;
            break;
        case 0x1000:
            pBattleEntities->m5FC7_battleTickMain_var1 = 0x11;
            break;
        case 0x800:
            pBattleEntities->m5FC7_battleTickMain_var1 = 0x12;
            break;
        case 0x1800:
            pBattleEntities->m5FC7_battleTickMain_var1 = 0x13;
            break;
        }
        break;
    case 7:
        switch (flag) {
        case 0x8000:
            pBattleEntities->m5FC7_battleTickMain_var1 = 0x15;
            break;
        case 0x4000:
            pBattleEntities->m5FC7_battleTickMain_var1 = 0x16;
            break;
        case 0x1000:
            pBattleEntities->m5FC7_battleTickMain_var1 = 0x18;
            break;
        case 0x2:
        case 0x8:
            pBattleEntities->m5FC7_battleTickMain_var1 = 0x19;
            break;
        case 0x1:
        case 0x4:
            pBattleEntities->m5FC7_battleTickMain_var1 = 0x1A;
            break;
        }
        break;
    case 9:
        switch (flag) {
        case 0x8000:
            pBattleEntities->m5FC7_battleTickMain_var1 = 0x1B;
            break;
        case 0x4000:
            pBattleEntities->m5FC7_battleTickMain_var1 = 0x1C;
            break;
        case 0x2000:
            pBattleEntities->m5FC7_battleTickMain_var1 = 0x1D;
            break;
        case 0x1000:
            pBattleEntities->m5FC7_battleTickMain_var1 = 0x1E;
            break;
        case 0x400:
            pBattleEntities->m5FC7_battleTickMain_var1 = 0x1F;
            break;
        case 0x800:
            pBattleEntities->m5FC7_battleTickMain_var1 = 0x20;
            break;
        case 0x100:
            pBattleEntities->m5FC7_battleTickMain_var1 = 0x21;
            break;
        case 0x200:
            pBattleEntities->m5FC7_battleTickMain_var1 = 0x22;
            break;
        }
        break;
    }
}

/* automatic command choice for an uncontrolled character: out[0] command
 * (1 attack, 2 ability, 4 ...), out[1] its argument */
void chooseAutoBattleCommand(u8 idx, u8* out, s16* arg2) {
    sBattleEntity* e = &battleEntities.m0_entities[idx];
    u16 mask;
    u8 count;
    u16 r;

    if (!(battleEntities.m0_entities[idx].m15A_flags & 0x80)) {
        if (rand() % 100 < 10 && !(e->m0_base.m7A_commandEnabledBF & 0x100) && *arg2 == 0) {
            out[0] = 4;
            return;
        }
        switch (e->m0_base.m56_battleCommandLoadout) {
        case 3:
            mask = 0xC3C0;
            count = 10;
            break;
        case 4:
            mask = 0xDF80;
            count = 10;
            break;
        case 5:
            mask = 0x1000;
            count = 4;
            break;
        case 7:
            mask = 0xE000;
            count = 3;
            break;
        case 0:
        case 8:
            mask = 0xC000;
            count = 2;
            break;
        case 2:
        case 9:
            mask = 0xBFE0;
            count = 11;
            break;
        case 1:
        case 6:
        case 10:
            mask = 0xF000;
            count = 4;
            break;
        }
        if (rand() % 100 < 25 && !(e->m0_base.m7A_commandEnabledBF & 0x20)) {
            out[0] = 2;
            r = rand() % count;
            if (gameState.m16C0_abilities[e->m0_base.m56_battleCommandLoadout].m0_unlockedAbilitiesBitField[1] & ((0x8000 >> r) & mask)) {
                out[1] = r;
                return;
            }
        }
        if (e->m0_base.m56_battleCommandLoadout == 8) {
            out[0] = 0;
            out[1] = 0;
            return;
        }
    }
    out[0] = 1;
    if (rand() % 100 < 80) {
        if (rand() % 100 >= 60) {
            out[1] = 1;
        } else {
            out[1] = 0;
        }
    } else {
        out[1] = 2;
    }
}

/* Noah: startBattleAttackAnimationVar8[7] */
void startBattleAttackAnimationVar8_7(void) {
    pBattleEntities->m5FA0_initBattleAttackStatusArray0[startBattleAttackAnimationVar4] = 2;
    pBattleEntities->m5F6C_damageDonePerAttack[startBattleAttackAnimationVar4] = (u32)(currentEntityBattleStats->m11_power * startBattleAttackAnimationVar6->m64_maxHp) / 20;
}

void transferCharStatsBackToField(void) {
    u8 i;
    SVECTOR unused; /* reserves retail's 8-byte frame */
    u8 j;
    sBattleEntity* e;
    GameStateCharacter* c;
    GameStateGear* g;
    GameStateGear* eg;

    for (i = 0; i < 3; i++) {
        if (battleCharacters[i] != 0x7F) {
            e = &battleEntities.m0_entities[i];
            c = &gameState.m26C_party[e->m0_base.m56_battleCommandLoadout];
            eg = &battleEntities.m0_entities[i].mA4_gear;
            g = &gameState.m978_gears[e->m0_base.mA0_gearNum];
            if (e->m0_base.m56_battleCommandLoadout == 7 && (battleEntities.m0_entities[i].m15A_flags & 0x80)) {
                e->m0_base.m4C_HP = (u32)(eg->m60_hp + 1) / 50;
                if (e->m0_base.m4C_HP == 0) {
                    e->m0_base.m4C_HP = 1;
                }
            }
            c->m4C_HP = e->m0_base.m4C_HP;
            c->m50_MP = e->m0_base.m50_MP;
            if (c->m4C_HP > c->m4E_MaxHP) {
                c->m4C_HP = c->m4E_MaxHP;
            }
            if (c->m50_MP > c->m52_MaxMP) {
                c->m50_MP = c->m52_MaxMP;
            }
            for (j = 0; j < 7; j++) {
                c->m90_deathblowProgress[j] = e->m0_base.m90_deathblowProgress[j];
            }
            c->m3A_killCount = e->m0_base.m3A_killCount;
            if (e->m0_base.m7C_statusFlags[0] & 0xC000) {
                c->m4C_HP = 1;
            }
            switch (e->m0_base.mA0_gearNum) {
            case 0:
            case 1:
            case 2:
            case 3:
            case 4:
            case 5:
            case 6:
            case 8:
            case 9:
            case 10:
            case 11:
            case 12:
            case 13:
            case 14:
            case 15:
            case 16:
                g->m60_hp = eg->m60_hp;
                g->m38_fuel = eg->m38_fuel;
                if ((u32)g->m64_maxHp < (u32)g->m60_hp) {
                    g->m60_hp = g->m64_maxHp;
                }
                if (g->m38_fuel > g->m3A_maxFuel) {
                    g->m38_fuel = g->m3A_maxFuel;
                }
                if (eg->m7C_statusFlags & 0x8000) {
                    g->m60_hp = (u32)g->m64_maxHp / 10;
                }
                break;
            }
        }
    }
}

u8 getGearAnimStatusBits(u8 idx) {
    GameStateCharacter* c = &battleEntities.m0_entities[idx].m0_base;
    GameStateGear* gear = &battleEntities.m0_entities[idx].mA4_gear;
    u8* m148 = &battleEntities.m0_entities[idx].m148_gearMode;
    u8 r;
    u16 t;

    t = gear->m7C_statusFlags & 0x400;
    r = t != 0;
    if ((u32)gear->m60_hp < (u32)gear->m64_maxHp >> 3 && !(c->m36_monsterFlags2 & 1)) {
        r |= 2;
    }
    if (*m148 == 4) {
        r |= 4;
    }
    return r;
}

void startGearInfinityMode(u8 idx) {
    battleEntities.m0_entities[idx].m148_gearMode = 4;
    battleEntities.m0_entities[idx].m15C_statusTimers[6] = 3;
    gameState.m16C0_abilities[0].m1A_unlockedGearAbilities |= 0x4000;
}

void func_8009C134(void) {
    u8 i;

    for (i = 0; i < 3; i++) {
        if (battleEntities.m0_entities[i].m0_base.m56_battleCommandLoadout == 3) {
            battleEntities.m5FAC_entitiesHitInCurrentAttackBF = 1 << i;
        }
    }
}

/* startBattleAttackAnimation, gear attacker branch */
void startGearAttackAnimation(void) {
    u16 mask;

    if (startBattleAttackAnimationVar2 < 3) {
        currentEntityBattleStats = &pBattleEntities->m2228_mechaBattleStats[pBattleEntities->m5FC1_performAttackSub3_attacker]
                                                         [pBattleEntities->m5FC2_battleGetSlotStatusSub_current28Index];
    } else {
        currentEntityBattleStats = &pBattleEntities->m35D8_enemiesBattleStats[pBattleEntities->m5FC2_battleGetSlotStatusSub_current28Index];
    }
    /* retail passes only a0 here (a1 is left over): unprototyped call */
    ((void (*)())startBattleAttackAnimationSub0)(startBattleAttackAnimationVar2);
    pAttackerGearMode = &pBattleEntities->m0_entities[pBattleEntities->m5FC1_performAttackSub3_attacker].m148_gearMode;
    startBattleAttackAnimationVar3 = 0;
    if ((currentEntityBattleStats->mA_attackFlags & 0x100) && battleGetSlotStatusSub_currentBattleEntity->m0_base.m56_battleCommandLoadout == 1) {
        rollEtherAttackMiss();
    }
    if ((battleGetSlotStatusSub_currentBattleEntity->m0_base.mA0_gearNum == 5 || battleGetSlotStatusSub_currentBattleEntity->m0_base.mA0_gearNum == 13)
        && pBattleEntities->m5FC2_battleGetSlotStatusSub_current28Index == 1) {
        currentEntityBattleStats->m22_elementFlags = battleGetSlotStatusSub_currentBattleEntityGear->m12_slots[1].m0_weaponPower;
    }
    mask = 1;
    for (startBattleAttackAnimationVar4 = 0; startBattleAttackAnimationVar4 < 11; startBattleAttackAnimationVar4++) {
        if (mask & pBattleEntities->m5FAC_entitiesHitInCurrentAttackBF) {
            startBattleAttackAnimationVar5 = &pBattleEntities->m0_entities[startBattleAttackAnimationVar4];
            startBattleAttackAnimationVar6 = &pBattleEntities->m0_entities[startBattleAttackAnimationVar4].mA4_gear;
            startBattleAttackAnimationVar7 = &pBattleEntities->m0_entities[startBattleAttackAnimationVar4].m148_gearMode;
            selectGearAttackStatsForTarget();
            startGearAttackAnimationVar8[currentEntityBattleStats->m16_effectType]();
            if (pBattleEntities->m5FA0_initBattleAttackStatusArray0[startBattleAttackAnimationVar4] == 0) {
                if (currentEntityBattleStats->mA_attackFlags & 0x800) {
                    rollGearStatusEffect(1);
                } else if (currentEntityBattleStats->mA_attackFlags & 0x4000) {
                    rollGearStatusEffect(0);
                }
            }
            pBattleEntities->m5FB0_performAttack_type = currentEntityBattleStats->m2_attackType;
            postComputeGearAttackDone();
            syncGearUntargetableFlag(startBattleAttackAnimationVar4);
        }
        mask <<= 1;
    }
    if (battleGetSlotStatusSub_currentBattleEntity->m0_base.m56_battleCommandLoadout == 4) {
        spendBillyGearAmmo();
    }
    recordLastGearAttackStats();
}

/* after a gear attack: attacker mode / m54 updates, then per-target damage modifiers */
void postComputeGearAttackDone(void) {
    s32 chance;

    if (startBattleAttackAnimationVar2 < 3) {
        if (pBattleEntities->m5FC2_battleGetSlotStatusSub_current28Index < 3) {
            if (++(*pAttackerGearMode) > pAttackerGearMode[1]) {
                (*pAttackerGearMode)--;
            }
        }
        if (pBattleEntities->m5FC2_battleGetSlotStatusSub_current28Index >= 3
            && pBattleEntities->m5FC2_battleGetSlotStatusSub_current28Index < 6) {
            *pAttackerGearMode = battleEntities.m5F24_gearTurn.m28_mode - 1;
        }
        if (pBattleEntities->m5FC2_battleGetSlotStatusSub_current28Index >= 6
            && pBattleEntities->m5FC2_battleGetSlotStatusSub_current28Index < 9) {
            *pAttackerGearMode = battleEntities.m5F24_gearTurn.m28_mode - 2;
        }
        if (pBattleEntities->m5FC2_battleGetSlotStatusSub_current28Index >= 9
            && pBattleEntities->m5FC2_battleGetSlotStatusSub_current28Index < 12) {
            *pAttackerGearMode = battleEntities.m5F24_gearTurn.m28_mode - 3;
        }
        if (pBattleEntities->m5FC2_battleGetSlotStatusSub_current28Index >= 3
            && pBattleEntities->m5FC2_battleGetSlotStatusSub_current28Index < 12) {
            battleGetSlotStatusSub_currentBattleEntity->m0_base.m54_infinityCharge += pBattleEntities->m5FC2_battleGetSlotStatusSub_current28Index / 3;
        }
        if (pBattleEntities->m5FC2_battleGetSlotStatusSub_current28Index == 5) {
            battleGetSlotStatusSub_currentBattleEntity->m0_base.m54_infinityCharge += 1;
        }
        if (pBattleEntities->m5FC2_battleGetSlotStatusSub_current28Index == 8) {
            battleGetSlotStatusSub_currentBattleEntity->m0_base.m54_infinityCharge += 2;
        }
        if (pBattleEntities->m5FC2_battleGetSlotStatusSub_current28Index == 11) {
            battleGetSlotStatusSub_currentBattleEntity->m0_base.m54_infinityCharge += 3;
        }
    }
    if (pBattleEntities->m5FA0_initBattleAttackStatusArray0[startBattleAttackAnimationVar4] == 0 && (startBattleAttackAnimationVar5->m0_base.m7C_statusFlags[2] & 0x2000)
        && rand() % 100 < 80) {
        startBattleAttackAnimationVar5->m0_base.m7C_statusFlags[2] &= ~0x2000;
        startBattleAttackAnimationVar6->m7C_statusFlags &= ~0x1000;
    }
    if (pBattleEntities->m5FA0_initBattleAttackStatusArray0[startBattleAttackAnimationVar4] == 10 && (startBattleAttackAnimationVar6->m7E_statusFlags1 & 0x80)) {
        pBattleEntities->m5F6C_damageDonePerAttack[startBattleAttackAnimationVar4] = 0;
    }
    if (pBattleEntities->m5FA0_initBattleAttackStatusArray0[startBattleAttackAnimationVar4] == 0) {
        if (startBattleAttackAnimationVar5->m0_base.m32_accessoryFlags & 0x80) {
            chance = 60;
            if (startBattleAttackAnimationVar5->m0_base.m56_battleCommandLoadout == 0) {
                chance = 80;
            }
            if (rand() % 100 < chance) {
                pBattleEntities->m5F6C_damageDonePerAttack[startBattleAttackAnimationVar4] = (u32)pBattleEntities->m5F6C_damageDonePerAttack[startBattleAttackAnimationVar4] >> 1;
            } else {
                pBattleEntities->m5F6C_damageDonePerAttack[startBattleAttackAnimationVar4] += (u32)pBattleEntities->m5F6C_damageDonePerAttack[startBattleAttackAnimationVar4] >> 1;
            }
        }
        if (startBattleAttackAnimationVar5->m0_base.m32_accessoryFlags & 0x20) {
            pBattleEntities->m5FA0_initBattleAttackStatusArray0[startBattleAttackAnimationVar2] = 0;
            pBattleEntities->m5F6C_damageDonePerAttack[startBattleAttackAnimationVar2] = pBattleEntities->m5F6C_damageDonePerAttack[startBattleAttackAnimationVar4];
        }
    }
    if ((startBattleAttackAnimationVar5->m0_base.m84_statusFlags2[3] & 0x200) && pBattleEntities->m5FA0_initBattleAttackStatusArray0[startBattleAttackAnimationVar4] == 1) {
        pBattleEntities->m5F6C_damageDonePerAttack[startBattleAttackAnimationVar4] = 0;
    }
}

/* gear variant of recordLastAttackStats */
void recordLastGearAttackStats(void) {
    u16 bits;

    bits = (battleGetSlotStatusSub_currentBattleEntityGear->m84_statusFlags4 | battleGetSlotStatusSub_currentBattleEntity->m0_base.m84_statusFlags2[3]) >> 12;
    pBattleEntities->m5FBC = currentEntityBattleStats->m20;
    pBattleEntities->m5FBD = currentEntityBattleStats->m21;
    pBattleEntities->m5FBE_lastAttackElements = currentEntityBattleStats->m22_elementFlags;
    pBattleEntities->m5FBF = currentEntityBattleStats->m23;
    pBattleEntities->m5FC0_lastAttackIndex = pBattleEntities->m5FC2_battleGetSlotStatusSub_current28Index;
    if (!(pBattleEntities->m5FBE_lastAttackElements & 0x3F)) {
        pBattleEntities->m5FBE_lastAttackElements |= bits;
    }
}

void selectGearAttackStatsForTarget(void) {
    if (pBattleEntities->m5FC2_battleGetSlotStatusSub_current28Index >= 12
        && pBattleEntities->m5FC2_battleGetSlotStatusSub_current28Index < 15
        && !(pBattleEntities->m0_entities[startBattleAttackAnimationVar4].m15A_flags & 0x80)) {
        currentEntityBattleStats += 3;
    } else if (pBattleEntities->m5FC2_battleGetSlotStatusSub_current28Index < 3
               && !(pBattleEntities->m0_entities[startBattleAttackAnimationVar4].m15A_flags & 0x80)) {
        currentEntityBattleStats += 15;
    }
}

void syncGearUntargetableFlag(u8 idx) {
    GameStateGear* gear = &battleEntities.m0_entities[idx].mA4_gear;
    GameStateCharacter* c = &battleEntities.m0_entities[idx].m0_base;

    if (gear->m80_statusFlags2 & 0x200) {
        c->m84_statusFlags2[0] |= 0x20;
    } else {
        c->m84_statusFlags2[0] &= ~0x20;
    }
}

/* gear version of Noah startBattleAttackAnimationVar8_0 (damage computation) */
void startGearAttackAnimationVar8_0(void) {
    u16 atk;
    u16 def;
    s8 hit;
    s32 mul;
    s32 damage;
    s32 atkMul;
    s32 defMul;
    u8 resist;

    mul = currentEntityBattleStats->m11_power;
    hit = computeGearBattleDamageSub0();
    atk = computeGearBattleDamageSub1();
    def = computeGearBattleDamageSub2();
    computeBattleDamageSub3(&atk, &def, &hit);
    if ((battleGetSlotStatusSub_currentBattleEntityGear->m80_statusFlags2 | battleGetSlotStatusSub_currentBattleEntityGear->m82_statusFlags3) & 8) {
        atk += atk / 5;
    }
    if ((battleGetSlotStatusSub_currentBattleEntityGear->m80_statusFlags2 | battleGetSlotStatusSub_currentBattleEntityGear->m82_statusFlags3) & 2) {
        atk += atk / 10;
    }
    if ((battleGetSlotStatusSub_currentBattleEntityGear->m80_statusFlags2 | battleGetSlotStatusSub_currentBattleEntityGear->m82_statusFlags3) & 4) {
        atk -= atk / 5;
    }
    if ((battleGetSlotStatusSub_currentBattleEntityGear->m80_statusFlags2 | battleGetSlotStatusSub_currentBattleEntityGear->m82_statusFlags3) & 1) {
        atk -= atk / 10;
    }
    if ((startBattleAttackAnimationVar6->m80_statusFlags2 | startBattleAttackAnimationVar6->m82_statusFlags3) & 4) {
        def += def / 5;
    }
    if ((startBattleAttackAnimationVar6->m80_statusFlags2 | startBattleAttackAnimationVar6->m82_statusFlags3) & 1) {
        def += def / 10;
    }
    if ((startBattleAttackAnimationVar6->m80_statusFlags2 | startBattleAttackAnimationVar6->m82_statusFlags3) & 8) {
        def -= def / 5;
    }
    if ((startBattleAttackAnimationVar6->m80_statusFlags2 | startBattleAttackAnimationVar6->m82_statusFlags3) & 2) {
        def -= def / 10;
    }
    if (currentEntityBattleStats->m22_elementFlags & 0x10) {
        if (!(startBattleAttackAnimationVar5->m0_base.m7C_statusFlags[3] & 0x40)) {
            startBattleAttackAnimationVar5->m0_base.m7C_statusFlags[2] |= 0x40;
        }
        if ((battleGetSlotStatusSub_currentBattleEntityGear->m84_statusFlags4 | battleGetSlotStatusSub_currentBattleEntityGear->m86_statusFlags5) & 0x4000) {
            initBattleAttackStatusArray0[startBattleAttackAnimationVar2] = 3;
            damageDonePerAttack[startBattleAttackAnimationVar2] = (u16)(battleGetSlotStatusSub_currentBattleEntity->m0_base.m52_MaxMP / 10) << 1;
        }
        if ((battleGetSlotStatusSub_currentBattleEntityGear->m84_statusFlags4 | battleGetSlotStatusSub_currentBattleEntityGear->m86_statusFlags5) & 0x1000) {
            initBattleAttackStatusArray0[startBattleAttackAnimationVar2] = 2;
            damageDonePerAttack[startBattleAttackAnimationVar2] = (u32)battleGetSlotStatusSub_currentBattleEntityGear->m64_maxHp / 10 << 1;
        }
    }
    if ((currentEntityBattleStats->m22_elementFlags & 0x20) && !(startBattleAttackAnimationVar5->m0_base.m7C_statusFlags[3] & 0x80)) {
        startBattleAttackAnimationVar5->m0_base.m7C_statusFlags[2] |= 0x80;
    }
    if (startBattleAttackAnimationVar5->m0_base.m7C_statusFlags[2] & 0x40) {
        def -= def >> 2;
        startBattleAttackAnimationVar5->m0_base.m7C_statusFlags[2] &= ~0x40;
    }
    if (battleGetSlotStatusSub_currentBattleEntity->m0_base.m7C_statusFlags[2] & 0x80) {
        atk -= atk >> 2;
        battleGetSlotStatusSub_currentBattleEntity->m0_base.m7C_statusFlags[2] &= ~0x80;
    }
    if (currentEntityBattleStats->mA_attackFlags & 0x400) {
        mul = 20;
    }
    if (currentEntityBattleStats->mA_attackFlags & 0x100) {
        atkMul = 5;
        defMul = 4;
    } else {
        atkMul = 4;
        defMul = 3;
    }
    if (def != 0) {
        damage = atkMul * atk - defMul * def;
    } else {
        damage = atkMul * atk;
    }
    switch (currentEntityBattleStats->m1A_attackFormula) {
    case 0:
    case 1:
        damage = mul * damage / 20;
        break;
    case 2:
        break;
    }
    if (damage <= 0) {
        damage = 0;
    } else if (damage < 15) {
        damage += rand() % 3;
    } else {
        damage += rand() % (damage / 15 + 2);
    }
    if (currentEntityBattleStats->m8_elements != 0) {
        damage = applyGearElementResistance(damage);
    }
    switch (hit) {
    case 1:
        pBattleEntities->m5FA0_initBattleAttackStatusArray0[startBattleAttackAnimationVar4] = 0;
        break;
    case 2:
        pBattleEntities->m5FA0_initBattleAttackStatusArray0[startBattleAttackAnimationVar4] = 5;
        resist = startBattleAttackAnimationVar6->m9C;
        if (resist >= 10) {
            resist = 9;
        }
        if (damage != 0) {
            damage = damage * (10 - resist) / 20;
        }
        break;
    case 3:
        damage = 0;
        pBattleEntities->m5FA0_initBattleAttackStatusArray0[startBattleAttackAnimationVar4] = 4;
        break;
    case 4:
        pBattleEntities->m5FA0_initBattleAttackStatusArray0[startBattleAttackAnimationVar4] = 2;
        break;
    }
    if (startBattleAttackAnimationVar3 && (currentEntityBattleStats->mA_attackFlags & 0x100) && damage != 0) {
        damage /= 3;
    }
    if (damage >= 10000) {
        damage = 9999;
    }
    if (damage < 0) {
        damage = 0;
    }
    pBattleEntities->m5F6C_damageDonePerAttack[startBattleAttackAnimationVar4] = damage;
}

void startGearAttackAnimationVar8_1(void) {
    if (rollGearStatusEffect(0) == 0) {
        pBattleEntities->m5FA0_initBattleAttackStatusArray0[startBattleAttackAnimationVar4] = 6;
    }
}

/* gear version of Noah computeBattleDamageSub0 (hit roll): 1 hit, 2 (critical), 3 miss */
s8 computeGearBattleDamageSub0(void) {
    s16 critBonus;
    s16 evadeBonus;
    u16 flags;
    u8 ammo;
    s16 hit;
    s16 evade;
    s8 v;
    s16 diff;
    u16 bits;

    critBonus = 0;
    evadeBonus = 0;
    if ((currentEntityBattleStats->mA_attackFlags & 0x200) && (startBattleAttackAnimationVar5->m0_base.m34_monsterFlags & 8)) {
        return 3;
    }
    flags = currentEntityBattleStats->mA_attackFlags;
    if (flags & 0x1000) {
        return 3;
    }
    if ((startBattleAttackAnimationVar5->m0_base.m84_statusFlags2[0] | startBattleAttackAnimationVar5->m0_base.m84_statusFlags2[1]) & 0x100) {
        return 3;
    }
    if (startBattleAttackAnimationVar5->m0_base.m7C_statusFlags[0] & 0x2000) {
        return 1;
    }
    if (startBattleAttackAnimationVar5->m0_base.m7C_statusFlags[2] & 0x1000) {
        return 1;
    }
    if (flags & 0x8000) {
        return 1;
    }
    evade = startBattleAttackAnimationVar5->m0_base.m5F_evadePercentage;
    ammo = gameStateAmmoByGearItem[battleGetSlotStatusSub_currentBattleEntityGear->m4_slotItem[0]];
    hit = battleGetSlotStatusSub_currentBattleEntity->m0_base.m5E_hitPercentage;
    if (ammo == 0) {
        hit += (s8)battleGetSlotStatusSub_currentBattleEntityGear->m9F_response;
    }
    v = startBattleAttackAnimationVar6->m9F_response;
    if (v != 0) {
        evade += v / 2;
    }
    if (battleGetSlotStatusSub_currentBattleEntity->m0_base.m56_battleCommandLoadout == 4) {
        if ((currentEntityBattleStats->m10_weaponMask & 0x80) && ammo == 0) {
            return 3;
        }
        if ((currentEntityBattleStats->m10_weaponMask & 0x20) && gameStateAmmoByGearItem[battleGetSlotStatusSub_currentBattleEntityGear->m4_slotItem[3]] == 0) {
            return 3;
        }
    }
    if ((battleGetSlotStatusSub_currentBattleEntityGear->m80_statusFlags2 | battleGetSlotStatusSub_currentBattleEntityGear->m82_statusFlags3) & 0x800) {
        hit += hit / 2;
    }
    if (battleGetSlotStatusSub_currentBattleEntityGear->m7C_statusFlags & 0x10) {
        critBonus = 60;
    }
    if (currentEntityBattleStats->mA_attackFlags & 0x1000) {
        return 3;
    }
    if (pBattleEntities->m0_entities[startBattleAttackAnimationVar4].m15A_flags & 0x80) {
        if ((startBattleAttackAnimationVar6->m80_statusFlags2 | startBattleAttackAnimationVar6->m82_statusFlags3) & 0x400) {
            evade += evade / 2;
        }
        if (startBattleAttackAnimationVar6->m7C_statusFlags & 0x10) {
            evadeBonus = 30;
        }
        if (startBattleAttackAnimationVar6->m7C_statusFlags & 0xC00) {
            return 1;
        }
    } else {
        hit = hit / 2;
        if (startBattleAttackAnimationVar5->m0_base.m7C_statusFlags[0] & 0x2000) {
            return 1;
        }
        if (startBattleAttackAnimationVar5->m0_base.m7C_statusFlags[2] & 0x1000) {
            return 1;
        }
        if ((startBattleAttackAnimationVar5->m0_base.m84_statusFlags2[0] | startBattleAttackAnimationVar5->m0_base.m84_statusFlags2[1]) & 0x800) {
            evade += evade / 2;
        }
    }
    if (pBattleEntities->m0_entities[startBattleAttackAnimationVar4].m15A_flags & 1) {
        if (rand() % 100 < 95) {
            return 2;
        }
        return 1;
    }
    diff = hit + (s8)currentEntityBattleStats->m15_hitBonus - evade;
    bits = startBattleAttackAnimationVar5->m0_base.m84_statusFlags2[0] | startBattleAttackAnimationVar5->m0_base.m84_statusFlags2[1];
    if (bits & 0x20) {
        if ((s16)(rand() % 100 - diff) >= 50) {
            return 3;
        }
        return 1;
    }
    if (bits & 0x40) {
        if ((s16)(rand() % 100 - diff) >= 50) {
            return 2;
        }
        return 1;
    }
    if ((s16)(rand() % 100 - diff) >= (s16)(evadeBonus - (critBonus - 85))) {
        return 3;
    }
    if ((s16)(rand() % 100 - diff) >= (s16)(evadeBonus - (critBonus - 85))) {
        return 2;
    }
    return 1;
}

u16 computeGearBattleDamageSub1(void) {
    u16 v;
    u8 mul;

    v = computeBattleDamageSub1();
    if (currentEntityBattleStats->mA_attackFlags & 0x100) {
        mul = battleGetSlotStatusSub_currentBattleEntityGear->m9E_etherAmp;
        if ((battleGetSlotStatusSub_currentBattleEntityGear->m7C_statusFlags & 0x100) || mul == 0) {
            mul = 4;
        }
        v = (mul * v) / 4;
        if ((battleGetSlotStatusSub_currentBattleEntityGear->m80_statusFlags2 | battleGetSlotStatusSub_currentBattleEntityGear->m82_statusFlags3) & 0x40) {
            v += v >> 1;
        }
    }
    return v;
}

u16 computeGearBattleDamageSub2(void) {
    u16 v;
    u16 extra;
    u8 r;

    v = computeBattleDamageSub2();
    if (currentEntityBattleStats->mA_attackFlags & 0x100) {
        r = startBattleAttackAnimationVar6->m99_defenceDown;
        extra = startBattleAttackAnimationVar6->m72_etherDefence;
        if (r != 0 && extra != 0) {
            extra = extra * (100 - r) / 100;
        }
        if (pBattleEntities->m0_entities[startBattleAttackAnimationVar4].m15A_flags & 0x80) {
            v += extra;
        }
        if ((startBattleAttackAnimationVar6->m80_statusFlags2 | startBattleAttackAnimationVar6->m82_statusFlags3) & 0x20) {
            v += v >> 1;
        }
    } else {
        r = startBattleAttackAnimationVar6->m99_defenceDown;
        if (r != 0) {
            v = v * (100 - r) / 100;
        }
    }
    return v;
}

/* scales damage by the target gear's resistance to the attack's element */
s32 applyGearElementResistance(s32 damage) {
    u8 i;
    u8 element;
    u8 resist;

    for (i = 0; i < 16; i++) {
        if (currentEntityBattleStats->m8_elements & (0x8000 >> i)) {
            element = i;
            break;
        }
    }
    resist = startBattleAttackAnimationVar6->m88_elementResistances[element];
    if (resist != 0) {
        if (resist < 20) {
            damage = damage * (20 - resist) / 20;
        } else {
            damage = 0;
        }
    }
    return damage;
}

/* status effect roll against a gear target (weapon: the attacker gear's
 * weapon effect; else the command's m1C chance / m1D type / m1E mask) */
s8 rollGearStatusEffect(u8 fromWeapon) {
    sBattleEntity* e;
    GameStateCharacter* base;
    s8 chance;
    u8 type;
    u16 mask;
    u8 duration;

    e = &pBattleEntities->m0_entities[startBattleAttackAnimationVar4];
    base = &e->m0_base;
    if (fromWeapon) {
        chance = battleGetSlotStatusSub_currentBattleEntityGear->m12_slots[0].m1_weaponStatusChance;
        type = battleGetSlotStatusSub_currentBattleEntityGear->m12_slots[0].m2_weaponStatusType;
        mask = battleGetSlotStatusSub_currentBattleEntityGear->m10_weaponStatusMask;
        duration = 5;
    } else {
        chance = currentEntityBattleStats->m1C_chance;
        type = currentEntityBattleStats->m1D_statusType;
        mask = currentEntityBattleStats->m1E_statusMask;
        duration = currentEntityBattleStats->m11_power;
    }
    if (!(pBattleEntities->m0_entities[startBattleAttackAnimationVar4].m15A_flags & 0x80)) {
        return 0;
    }
    if (chance < rand() % 100) {
        return 0;
    }
    switch (type) {
    case 0:
        if ((mask & 0x20) && (startBattleAttackAnimationVar6->m80_statusFlags2 & 0x8000)) {
            startBattleAttackAnimationVar6->m80_statusFlags2 &= 0x7FFF;
            startBattleAttackAnimationVar5->m0_base.m84_statusFlags2[0] &= 0x7FFF;
            return 1;
        }
        break;
    case 1:
        if (mask & 0xA) {
            if (startBattleAttackAnimationVar6->m80_statusFlags2 & 5) {
                startBattleAttackAnimationVar6->m80_statusFlags2 &= ~5;
                return 1;
            }
        } else if (mask & 5) {
            if (startBattleAttackAnimationVar6->m80_statusFlags2 & 0xA) {
                startBattleAttackAnimationVar6->m80_statusFlags2 &= ~0xA;
                return 1;
            }
        }
        break;
    }
    if (type == 0) {
        if (mask & startBattleAttackAnimationVar6->m7E_statusFlags1) {
            return 0;
        }
        switch (mask) {
        case 0x400:
            e->m15C_statusTimers[0] = duration;
            startBattleAttackAnimationVar6->m7C_statusFlags &= ~0x400;
            base->m7C_statusFlags[0] |= 0x2000;
            break;
        case 0x1000:
            startBattleAttackAnimationVar6->m7C_statusFlags &= ~0x1000;
            base->m7C_statusFlags[2] |= 0x2000;
            break;
        case 0x200:
            e->m15C_statusTimers[1] = duration;
            break;
        case 0x100:
            e->m15C_statusTimers[2] = duration;
            break;
        case 0x80:
            e->m15C_statusTimers[3] = duration;
            break;
        case 0x20:
            e->m15C_statusTimers[4] = duration;
            base->m7C_statusFlags[0] |= 0x1000;
            break;
        case 0x10:
            e->m15C_statusTimers[5] = duration;
            break;
        }
        startBattleAttackAnimationVar6->m7C_statusFlags |= mask;
    }
    if (type == 1) {
        startBattleAttackAnimationVar6->m80_statusFlags2 |= mask;
        switch (mask) {
        case 0x1000:
            e->m15C_statusTimers[10] = duration;
            break;
        case 0x40:
            e->m15C_statusTimers[11] = duration;
            break;
        case 0x20:
            e->m15C_statusTimers[12] = duration;
            break;
        }
    }
    if (type == 3) {
        if (mask & 0xF000) {
            if (startBattleAttackAnimationVar6->m86_statusFlags5 & 0xF000) {
                return 0;
            }
            startBattleAttackAnimationVar6->m84_statusFlags4 = mask | (startBattleAttackAnimationVar6->m84_statusFlags4 & 0xFFF);
            e->m15C_statusTimers[7] = duration;
        }
        if (mask & 0xF00) {
            if (startBattleAttackAnimationVar6->m86_statusFlags5 & 0xF00) {
                return 0;
            }
            startBattleAttackAnimationVar6->m84_statusFlags4 = mask | (startBattleAttackAnimationVar6->m84_statusFlags4 & 0xF0FF);
            e->m15C_statusTimers[8] = duration;
        }
    }
    if (type == 10) {
        startBattleAttackAnimationVar6->m84_statusFlags4 &= ~mask;
    }
    if (type == 11 && !(startBattleAttackAnimationVar6->m7E_statusFlags1 & 0x40)) {
        startBattleAttackAnimationVar6->m99_defenceDown += mask;
        if (startBattleAttackAnimationVar6->m99_defenceDown >= 100) {
            startBattleAttackAnimationVar6->m99_defenceDown = 99;
        }
        type = 0;
        mask = 0x40;
    }
    if (type == 12) {
        pBattleEntities->m5FA0_initBattleAttackStatusArray0[startBattleAttackAnimationVar4] = 0;
        pBattleEntities->m5F6C_damageDonePerAttack[startBattleAttackAnimationVar4] = (u32)startBattleAttackAnimationVar6->m60_hp / mask;
    }
    if (type == 13) {
        pBattleEntities->m5FA0_initBattleAttackStatusArray0[startBattleAttackAnimationVar4] = 0;
        pBattleEntities->m5F6C_damageDonePerAttack[startBattleAttackAnimationVar4] = startBattleAttackAnimationVar6->m60_hp - 1;
    }
    if (type == 14) {
        startBattleAttackAnimationVar6->m80_statusFlags2 = 0;
        startBattleAttackAnimationVar6->m84_statusFlags4 = 0;
        if (mask == 1) {
            startBattleAttackAnimationVar6->m82_statusFlags3 = 0;
            startBattleAttackAnimationVar6->m86_statusFlags5 = 0;
        }
    }
    if (type == 16) {
        startBattleAttackAnimationVar6->m7C_statusFlags |= 1;
        startBattleAttackAnimationVar5->m0_base.m7C_statusFlags[0] |= 0x80;
    }
    if (type == 15) {
        startBattleAttackAnimationVar6->m7C_statusFlags &= ~1;
        startBattleAttackAnimationVar5->m0_base.m7C_statusFlags[0] &= ~0x80;
    }
    setGearStatusEffectMessage(type, mask);
    return 1;
}
