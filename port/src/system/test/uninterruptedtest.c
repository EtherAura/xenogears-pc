/* Uninterrupted combos (xg-htn.35.155): the setting's persistence, and the
 * two decisions port/src/game/battle/combo.c gives the battle sprite VM, on
 * a synthetic deathblow script and attack queue. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "main/sprite.h"
#include "battle/battle.h"
#include "pc_gameplay.h"
#include "system/gameplay.h"
#include "test/env.h"

static int checks;
#include "test/check.h"

#ifndef _WIN32
BattleBss_800C3EB0 battleBssBlock;
s32 allocateJumpAnimationStructVar0;
/* The sprite set swap a skipped charge-up still does (the kernel's, stubbed). */
static int swaps;
void setAnimationBundle(SpriteActorCore* actor, void* bundle) {
    actor->m44_currentAnimationBundle = bundle;
    swaps++;
}
s32 pcComboSkipJump(SpriteActorCore* actor);
s32 pcComboSkipCall(SpriteActorCore* actor, const u8* op, const u8* target);
s32 pcComboSkipSound(SpriteActorCore* actor, const u8* op);

/* An animation as the attack bundle has it: a header whose second u16 gives
 * the script's start (setCurrentAnimationPtr: header + 2 + that), then the
 * opening every combo deathblow has (cc, the jump's guard and call, the
 * charge-up's call), and two routines: a jump (Fei's: its pose and rise, then
 * effect 0x61 landing 39 in front of the enemy) and a charge-up (c3 69 ..). */
static u8 animation[128];
enum { SCRIPT = 6, GUARD = SCRIPT + 3, JUMP_CALL = SCRIPT + 7, CHARGE_CALL = SCRIPT + 10, JUMP = 80, CHARGE = 100 };
static const u8 jumpRoutine[] = {0x90, 0xBE, 0x19, 0x18, 0x03, 0x01, 0xA1, 0xD8, 0xEC, 0x61, 0x27, 0x85};

static void queue(int playing, const u8* anims, int count) {
    memset(battleBssBlock.m138_currentDamages, 0xFF, sizeof(battleBssBlock.m138_currentDamages));
    for (int i = 0; i < count; i++) battleBssBlock.m138_currentDamages[i].m47_battleAnimationToPlay = anims[i];
    allocateJumpAnimationStructVar0 = playing + 1; /* processBattleAnimation has moved past it */
}

static int decisions(void) {
    static const u8 combo[] = {7, 8, 10, 0xFE};
    static const u8 opening[] = {0xF3, 9, 0xFE};
    static SpriteActorCore actor, enemy;

    memset(animation, 0, sizeof(animation));
    animation[2] = SCRIPT - 2;
    memcpy(animation + SCRIPT, "\xcc\x00\x00\xf8\x07\x00\x88\xe2\x00\x00\xe2\x00\x00\x8e", 14);
    animation[JUMP_CALL + 1] = JUMP - JUMP_CALL;
    memcpy(animation + JUMP, jumpRoutine, sizeof(jumpRoutine));
    animation[CHARGE] = 0xC3, animation[CHARGE + 1] = 0x69;
    memset(&actor, 0, sizeof(actor));
    actor.m58_startOfCurrentAnimation = animation;
    /* Where the last deathblow left the attacker: 80 right of the enemy at
     * its place, facing it (the jump lands 39 * 8192 / 4096 = 78 right). */
    memset(&enemy, 0, sizeof(enemy));
    actor.m74_targetEntitySprite = &enemy;
    actor.m2C_scale = 8192;
    actor.mAC_x2_facing = 1;
    actor.m0_position.vx = 780 << 16, actor.m0_position.vz = 760 << 16;
    enemy.m0_position.vx = 700 << 16, enemy.m0_position.vz = 760 << 16;
    battleBssBlock.m4_visualEntities[0].mA_X = 700, battleBssBlock.m4_visualEntities[0].mC_Z = 760;
    battleBssBlock.mA78_startPlayingAttackAnimationVar0 = 1;

    /* Off: the original, whatever the battle. */
    testSetEnv("XG_UNINTERRUPTED_COMBOS", "0");
    queue(1, combo, 4);
    actor.mAF_animationId = ~8;
    actor.m64_spriteByteCode = animation + GUARD;
    CHECK(!pcComboSkipJump(&actor));
    CHECK(!pcComboSkipCall(&actor, animation + CHARGE_CALL, animation + CHARGE));

    testSetEnv("XG_UNINTERRUPTED_COMBOS", "1");
    /* The first deathblow: no jump, its charge-up kept. */
    queue(0, combo, 4);
    actor.mAF_animationId = ~7;
    CHECK(pcComboSkipJump(&actor));
    CHECK(!pcComboSkipCall(&actor, animation + CHARGE_CALL, animation + CHARGE));
    /* The second and third: neither. */
    queue(1, combo, 4);
    actor.mAF_animationId = ~8;
    CHECK(pcComboSkipJump(&actor));
    CHECK(pcComboSkipCall(&actor, animation + CHARGE_CALL, animation + CHARGE));
    queue(2, combo, 4);
    actor.mAF_animationId = ~10;
    CHECK(pcComboSkipCall(&actor, animation + CHARGE_CALL, animation + CHARGE));
    /* A skipped charge-up ends on the deathblow's own sprite set, as the
     * routine does (effect 0x68), when an opening had left the ordinary one. */
    {
        static int own, ordinary;
        actor.m4C_specialAnimation = &own;
        actor.m48_defaultAnimationBundle = &ordinary;
        actor.m44_currentAnimationBundle = &own;
        swaps = 0;
        CHECK(pcComboSkipCall(&actor, animation + CHARGE_CALL, animation + CHARGE));
        CHECK(swaps == 0); /* already on it */
        actor.m44_currentAnimationBundle = &ordinary;
        actor.mB0_xA = 0;
        CHECK(pcComboSkipCall(&actor, animation + CHARGE_CALL, animation + CHARGE));
        CHECK(swaps == 1 && actor.m44_currentAnimationBundle == &own && actor.mB0_xA == 1);
        /* The combo's first deathblow keeps its charge-up, which does it itself. */
        queue(0, combo, 4);
        actor.mAF_animationId = ~7;
        actor.m44_currentAnimationBundle = &ordinary;
        CHECK(!pcComboSkipCall(&actor, animation + CHARGE_CALL, animation + CHARGE));
        CHECK(swaps == 1 && actor.m44_currentAnimationBundle == &ordinary);
        actor.m44_currentAnimationBundle = actor.m48_defaultAnimationBundle = actor.m4C_specialAnimation = NULL;
        queue(2, combo, 4);
        actor.mAF_animationId = ~10;
    }
    /* The jump stays when the deathblow needs it, at its own speed: when it
     * lands on the enemy's other side or more than 24 off where the attacker
     * stands (along the line to the enemy, or off it). */
    actor.m64_spriteByteCode = animation + GUARD;
    CHECK(pcComboSkipJump(&actor)); /* 80 vs 78: no jump */
    actor.m0_position.vx = 800 << 16; /* 22 off */
    CHECK(pcComboSkipJump(&actor));
    actor.m0_position.vx = 820 << 16; /* 42 off */
    CHECK(!pcComboSkipJump(&actor));
    actor.m0_position.vx = 740 << 16; /* 38 off: right under the enemy */
    CHECK(!pcComboSkipJump(&actor));
    actor.m0_position.vx = 780 << 16, actor.m0_position.vz = 790 << 16; /* 30 off the enemy's line */
    CHECK(!pcComboSkipJump(&actor));
    actor.m0_position.vz = 760 << 16;
    animation[JUMP + 10] = 0xD9; /* a deathblow that starts 39 behind the enemy */
    CHECK(!pcComboSkipJump(&actor));
    actor.m0_position.vx = 622 << 16, actor.mAC_x2_facing = 0; /* from its left, facing it: lands right */
    CHECK(!pcComboSkipJump(&actor));
    animation[JUMP + 10] = 0x27;
    /* Left of its place, the enemy is jumped to from the left, whatever the
     * facing (effect 0x61's rule). */
    enemy.m0_position.vx = 650 << 16, actor.m0_position.vx = 730 << 16, actor.mAC_x2_facing = 1;
    CHECK(!pcComboSkipJump(&actor));
    actor.m0_position.vx = 572 << 16;
    CHECK(pcComboSkipJump(&actor));
    enemy.m0_position.vx = 700 << 16, actor.m0_position.vx = 780 << 16;
    /* A routine without effect 0x61 where it opens: keep the jump. */
    animation[JUMP + 8] = 0x00;
    CHECK(!pcComboSkipJump(&actor));
    animation[JUMP + 8] = 0xEC;
    CHECK(pcComboSkipJump(&actor));
    /* The focus sound (b0 44) once: not in the second and third openings,
     * any other sound, nor a 0x44 from a move. */
    {
        static const u8 focus[] = {0xB0, 0x44}, other[] = {0xB0, 0x40};
        memcpy(animation + SCRIPT + 20, focus, 2);
        memcpy(animation + SCRIPT + 24, other, 2);
        memcpy(animation + SCRIPT + 70, focus, 2);
        CHECK(pcComboSkipSound(&actor, animation + SCRIPT + 20));
        CHECK(!pcComboSkipSound(&actor, animation + SCRIPT + 24));
        CHECK(!pcComboSkipSound(&actor, animation + SCRIPT + 70));
        queue(0, combo, 4);
        actor.mAF_animationId = ~7;
        CHECK(!pcComboSkipSound(&actor, animation + SCRIPT + 20));
        testSetEnv("XG_UNINTERRUPTED_COMBOS", "0");
        queue(2, combo, 4);
        actor.mAF_animationId = ~10;
        CHECK(!pcComboSkipSound(&actor, animation + SCRIPT + 20));
        testSetEnv("XG_UNINTERRUPTED_COMBOS", "1");
    }
    /* Only the opening: a guard further on, a call to another routine, a
     * charge-up routine called from a move (past the first 64 bytes). */
    actor.m64_spriteByteCode = animation + GUARD + 4;
    CHECK(!pcComboSkipJump(&actor));
    CHECK(!pcComboSkipCall(&actor, animation + JUMP_CALL, animation + JUMP));
    CHECK(!pcComboSkipCall(&actor, animation + SCRIPT + 64, animation + CHARGE));
    /* A queue's first deathblow after a camera entry (0xF3) keeps its charge-up. */
    queue(1, opening, 3);
    actor.mAF_animationId = ~9;
    actor.m64_spriteByteCode = animation + GUARD;
    CHECK(pcComboSkipJump(&actor));
    CHECK(!pcComboSkipCall(&actor, animation + CHARGE_CALL, animation + CHARGE));
    /* Not the Combo command (a normal turn's deathblow, the flag clear), not
     * this actor's deathblow script, not a deathblow. */
    queue(1, combo, 4);
    actor.mAF_animationId = ~8;
    battleBssBlock.mA78_startPlayingAttackAnimationVar0 = 0;
    CHECK(!pcComboSkipJump(&actor));
    CHECK(!pcComboSkipCall(&actor, animation + CHARGE_CALL, animation + CHARGE));
    battleBssBlock.mA78_startPlayingAttackAnimationVar0 = 1;
    actor.mAF_animationId = 8;
    CHECK(!pcComboSkipJump(&actor));
    CHECK(!pcComboSkipCall(&actor, animation + CHARGE_CALL, animation + CHARGE));
    static const u8 attacks[] = {1, 2, 0xFE};
    queue(1, attacks, 3);
    actor.mAF_animationId = ~2;
    CHECK(!pcComboSkipJump(&actor));
    CHECK(!pcComboSkipCall(&actor, animation + CHARGE_CALL, animation + CHARGE));
    return 0;
}
#endif

int main(int argc, char** argv) {
    if (argc != 3) return 2;
    char path[1200]; snprintf(path, sizeof(path), "%s/uninterrupted.ini", argv[1]);
    testSetEnv("XG_SETTINGS", path);
    if (!strcmp(argv[2], "write")) {
        CHECK(!pcGameplayUninterruptedCombos());
        pcGameplaySetUninterruptedCombos(1);
        CHECK(pcGameplayUninterruptedCombos());
    } else if (!strcmp(argv[2], "read")) {
        CHECK(pcGameplayUninterruptedCombos());
    } else {
        CHECK(!pcGameplayUninterruptedCombos()); /* off by default */
#ifndef _WIN32
        if (decisions()) return 1;
#endif
    }
    printf("uninterrupted combos %s: %d checks passed\n", argv[2], checks);
    return 0;
}
