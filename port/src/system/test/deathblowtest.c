/* The deathblow learning setting: its arithmetic, persistence and the real
 * native attack resolution (startBattleAttackAnimation) that adds practice. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "main/game_state.h"
#include "battle/battle.h"
#include "pc_gameplay.h"
#include "system/gameplay.h"
#include "test/env.h"

static int checks, draws;
int rand(void) { ++draws; return 0; }
#include "test/check.h"

static void learning(int percent) {
    char text[32]; snprintf(text, sizeof(text), "%d", percent);
    testSetEnv("XG_DEATHBLOW_LEARNING", text);
}

#ifndef _WIN32
GameState gameState;
BattleBss_800CCCE8 battleEntities;
BattleBss_800CCCE8* pBattleEntities = &battleEntities;
u8 startBattleAttackAnimation(void);
/* The battle overlay's state the attack resolution reads (b_800941A4.c). */
u8 startBattleAttackAnimationVar0, startBattleAttackAnimationVar1, startBattleAttackAnimationVar2;
u8 startBattleAttackAnimationVar3, startBattleAttackAnimationVar4;
sBattleEntity* startBattleAttackAnimationVar5;
GameStateGear* startBattleAttackAnimationVar6;
u8* startBattleAttackAnimationVar7;
sBattleEntity* battleGetSlotStatusSub_currentBattleEntity;
GameStateGear* battleGetSlotStatusSub_currentBattleEntityGear;
sBattle800CDD40Sub* currentEntityBattleStats;
u8* pAttackerGearMode;
u8 battlePartyMemberCount;
u16 partySavedCommandEnabledBF[16];
s32 damageDonePerAttack[11];
static void noEffect(void) {}
void (*startBattleAttackAnimationVar8[8])(void) = {noEffect, noEffect, noEffect, noEffect,
                                                   noEffect, noEffect, noEffect, noEffect};
void (*startGearAttackAnimationVar8[8])(void) = {noEffect, noEffect, noEffect, noEffect,
                                                 noEffect, noEffect, noEffect, noEffect};
void spendBillyGearAmmo(void) {}
void setGearStatusEffectMessage(u8 type, u16 mask) { (void)type; (void)mask; }

/* One eligible attack by party slot 0 on combo counter 'combo' with no
 * targets: the native function then only does its bookkeeping. */
static unsigned attack(int combo) {
    battleEntities.m5FC1_performAttackSub3_attacker = 0;
    battleEntities.m5FC2_battleGetSlotStatusSub_current28Index = combo;
    battleEntities.m5FAC_entitiesHitInCurrentAttackBF = 0;
    startBattleAttackAnimation();
    return battleEntities.m0_entities[0].m0_base.m90_deathblowProgress[combo < 8 ? combo : 0];
}

static int native(void) {
    GameStateCharacter* c = &battleEntities.m0_entities[0].m0_base;
    memset(&battleEntities, 0, sizeof(battleEntities));
    c->m56_battleCommandLoadout = 2; /* Citan */
    c->m55_deathblowLearnRate = 3;
    c->mA1_deathblowLearnBonus = 1;

    learning(100);
    CHECK(attack(2) == 4 && attack(2) == 8);
    CHECK(c->m90_deathblowProgress[1] == 0 && c->m90_deathblowProgress[3] == 0);
    learning(0);
    CHECK(attack(2) == 8 && attack(2) == 8);
    learning(150);
    CHECK(attack(2) == 14 && attack(2) == 20);
    learning(110); /* 4.4 a hit: the remainder carries */
    CHECK(attack(2) == 24 && attack(2) == 28 && attack(2) == 33 && attack(2) == 37 && attack(2) == 42);
    learning(1000);
    CHECK(attack(2) == 82);

    /* Native gates: deathblows (counters 7+), enemies, Gear attacks and
     * counters at 65000 or more get no practice. */
    c->m90_deathblowProgress[0] = 0;
    CHECK(attack(7) == 0 && c->m90_deathblowProgress[7] == 0);
    battleEntities.m5FC1_performAttackSub3_attacker = 3;
    battleEntities.m0_entities[3].m0_base.m55_deathblowLearnRate = 5;
    battleEntities.m5FC2_battleGetSlotStatusSub_current28Index = 1;
    startBattleAttackAnimation();
    CHECK(battleEntities.m0_entities[3].m0_base.m90_deathblowProgress[1] == 0);
    battleEntities.m0_entities[0].m15A_flags = 0x80;
    CHECK(attack(4) == 0);
    battleEntities.m0_entities[0].m15A_flags = 0;
    c->m90_deathblowProgress[5] = 65000;
    CHECK(attack(5) == 65000);

    /* The counter's top: a large step stops at 65535; native 100% adds as before. */
    c->m55_deathblowLearnRate = 255; c->mA1_deathblowLearnBonus = 255;
    c->m90_deathblowProgress[6] = 64999;
    CHECK(attack(6) == 65535);
    learning(100);
    c->m90_deathblowProgress[6] = 64999;
    CHECK(attack(6) == 65509);
    return 0;
}
#endif

int main(int argc, char** argv) {
    if (argc != 3) return 2;
    char path[1200]; snprintf(path, sizeof(path), "%s/deathblow.ini", argv[1]);
    testSetEnv("XG_SETTINGS", path);
    if (!strcmp(argv[2], "write")) {
        pcGameplaySetDeathblowLearning(275);
        CHECK(pcGameplayDeathblowLearning() == 275);
    } else if (!strcmp(argv[2], "read")) {
        CHECK(pcGameplayDeathblowLearning() == 275);
        CHECK(pcGameplayDeathblowPractice(0, 0, 10, 4) == 21); /* 11 points, 0 carried */
    } else {
        CHECK(pcGameplayDeathblowLearning() == 100);
        /* 100% is the native u16 sum, whatever the inputs. */
        CHECK(pcGameplayDeathblowPractice(0, 0, 10, 4) == 14);
        CHECK(pcGameplayDeathblowPractice(99, 99, 64999, 510) == 65509);
        learning(-5); CHECK(pcGameplayDeathblowLearning() == 0);
        learning(5000); CHECK(pcGameplayDeathblowLearning() == 1000);
        testSetEnv("XG_DEATHBLOW_LEARNING", "fast"); CHECK(pcGameplayDeathblowLearning() == 100);
        learning(0);
        CHECK(pcGameplayDeathblowPractice(1, 3, 500, 255) == 500);
        /* Fractions carry per character and counter, never across them. */
        learning(50);
        CHECK(pcGameplayDeathblowPractice(1, 3, 0, 1) == 0);
        CHECK(pcGameplayDeathblowPractice(1, 4, 0, 1) == 0);
        CHECK(pcGameplayDeathblowPractice(2, 3, 0, 1) == 0);
        CHECK(pcGameplayDeathblowPractice(1, 3, 0, 1) == 1);
        CHECK(pcGameplayDeathblowPractice(1, 4, 0, 1) == 1);
        CHECK(pcGameplayDeathblowPractice(2, 3, 0, 1) == 1);
        /* 1% of one point: 100 attacks make one point, exactly. */
        learning(1);
        unsigned p = 0;
        for (int i = 0; i < 99; ++i) p = pcGameplayDeathblowPractice(5, 0, p, 1);
        CHECK(p == 0);
        CHECK(pcGameplayDeathblowPractice(5, 0, p, 1) == 1);
        /* Total over many attacks: floor(n * increment * percent / 100). */
        learning(333);
        p = 0;
        for (int i = 0; i < 1000; ++i) p = pcGameplayDeathblowPractice(6, 1, p, 3);
        CHECK(p == 9990);
        learning(1000);
        CHECK(pcGameplayDeathblowPractice(0, 2, 64999, 510) == 65535);
        CHECK(pcGameplayDeathblowPractice(0, 2, 0, 510) == 5100);
        /* Out-of-range identifiers still scale, without a carry. */
        learning(150);
        CHECK(pcGameplayDeathblowPractice(200, 9, 0, 3) == 4 && pcGameplayDeathblowPractice(200, 9, 0, 3) == 4);
#ifndef _WIN32
        if (native()) return 1;
#endif
        CHECK(draws == 0);
    }
    printf("deathblow %s: %d checks passed\n", argv[2], checks);
    return 0;
}
