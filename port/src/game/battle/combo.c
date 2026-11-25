/*
 * Uninterrupted combos (F1 > Gameplay, uninterrupted_combos; xg-htn.35.155).
 *
 * The Combo command (useCombo, b_8008B478.c) queues the chosen deathblows one
 * after another (queue animations 7-13: deathblow 0-6) and sets the combo
 * flag mA78_startPlayingAttackAnimationVar0 until the turn ends. Every party
 * member's deathblow script (the attack bundle, disc dir 0x2C) opens the same
 * way, measured in all nine bundles (US):
 *
 *   cc ..          (sets the script's second stack)
 *   f8 nn 00 88    if the combo flag is clear, jump over the next call:
 *   e2 ..          the jump into the ready stance (ops 0x86/0x87 wait for the
 *                  rise and the landing), so only a combo deathblow has it
 *   ...            script registers (df), the shout, the line and camera
 *                  effects, sound 0x44
 *   e2 ..          the charge-up: a routine that starts by swapping to the
 *                  pose bundle (battle sprite effect 0x69), holds the pose
 *                  while lines converge (about 33 vblanks), and swaps back
 *                  (0x68) before it returns
 *
 * then the deathblow's own move routines, which test the combo flag again for
 * their combo-only variants (Raijin's finisher flash). Billy fires from where
 * he stands: his scripts have no jump. With the option on, every deathblow
 * after the combo's first skips the charge-up routine, and a deathblow's
 * opening guard sees the flag clear (the jump skipped, as outside a combo)
 * unless the jump is needed (xg-htn.35.159, the user's choice): when it would
 * land on the enemy's other side (some deathblows start behind it: Citan's
 * first, Bart's, Rico's) or more than JUMP_MARGIN from where the attacker
 * stands (a previous deathblow can end right under the enemy or far from it).
 * Then it runs as the original's, at its own speed. Nothing else changes: the moves, their combo variants,
 * damage, AP and learning are the game's. Off: the original.
 *
 * The charge-up routine also ends by putting the actor on the deathblow's own
 * sprite set (effect 0x68: the attack bundle while the animation is one of
 * its own). Openings that move to the ordinary set before it (op 0x90:
 * Emeralda's deathblows 2 and 4 jump as part of the move, Billy's opening
 * routine) would otherwise play their move in ordinary poses (xg-htn.35.158),
 * so a skipped charge-up still does that.
 *
 * The focus sound, battle sound 0x44 (op b0 44 just before the charge-up's
 * call in every opening), plays for the combo's first deathblow only (the
 * user,); the character's own sound beside it (e8 4f) stays.
 */
#include "common.h"
#include "psyq.h"
#include "battle/battle.h"
#include "pc_gameplay.h"

extern s32 allocateJumpAnimationStructVar0;
void setAnimationBundle(SpriteActorCore* actor, void* bundle);

/* The bytes of the current animation's script past its start (its header:
 * setCurrentAnimationPtr), or -1. */
static s32 scriptOffset(SpriteActorCore* actor, const u8* at) {
    const u8* anim = actor->m58_startOfCurrentAnimation;

    if (anim == NULL) {
        return -1;
    }
    return (s32)(at - (anim + 2 + ((const u16*)anim)[1]));
}

/* The queue entry the battle plays now (processBattleAnimation moved past it
 * as it started it), when it is a deathblow of the Combo command played by
 * this actor's own deathblow script. */
static s32 comboDeathblow(SpriteActorCore* actor) {
    s32 index = allocateJumpAnimationStructVar0 - 1;
    s32 anim;

    if (!battleBssBlock.mA78_startPlayingAttackAnimationVar0 || index < 0 || index >= 32) {
        return -1;
    }
    anim = battleBssBlock.m138_currentDamages[index].m47_battleAnimationToPlay;
    if (anim < 7 || anim > 13 || (s8)actor->mAF_animationId != ~anim) {
        return -1;
    }
    return index;
}

/* How far the jump may land from where the attacker stands, along the line
 * to the enemy, before it is needed: the deathblows start 77-127 from the
 * enemy, and a previous one ends within about 20 of that (measured, all nine
 * characters) unless it ends somewhere else entirely (24-68 off). */
#define JUMP_MARGIN 24

/* Is the jump the guard at op guards needed? The guarded block (f8 nn 00 88:
 * nn bytes from the guard) ends with the jump's call (e2), and the jump
 * routine opens with its pose and rise, then effect 0x61 (ec 61 d: land d
 * scaled from the enemy, on its line; 39 or 64 in front, -39 or -38 behind),
 * within its first ten bytes in every bundle. Where it would land is
 * effect 0x61's own rule (b_800B3E04.c battleSpriteEffect): the enemy's x
 * plus d, mirrored when the enemy is left of its place, or (at its place)
 * when the attacker faces right. Without such a routine, keep the jump. */
static s32 jumpNeeded(SpriteActorCore* actor, const u8* op) {
    SpriteActorCore* target = actor->m74_targetEntitySprite;
    const u8* call = op + (s16)(op[1] | (op[2] << 8)) - 3;
    const u8* jump;
    s32 i, d, tx, tz, home, ax, az, x;

    if (target == NULL || call < op + 4 || call[0] != 0xE2) {
        return 1;
    }
    jump = call + (s16)(call[1] | (call[2] << 8));
    for (i = 0; i < 16 && !(jump[i] == 0xEC && jump[i + 1] == 0x61); i++) {
    }
    if (i == 16) {
        return 1;
    }
    d = ((s8)jump[i + 2] * actor->m2C_scale) / 4096;
    tx = (s16)(target->m0_position.vx >> 16);
    tz = (s16)(target->m0_position.vz >> 16);
    home = battleBssBlock.m4_visualEntities[BATTLE_SLOT(target)].mA_X;
    if (tx == home) {
        if (!actor->mAC_x2_facing) {
            d = -d;
        }
    } else if (tx < home) {
        d = -d;
    }
    x = tx + d;
    ax = (s16)(actor->m0_position.vx >> 16);
    az = (s16)(actor->m0_position.vz >> 16);
    if ((ax < tx) != (x < tx)) {
        return 1; /* it lands on the enemy's other side */
    }
    return abs(abs(ax - tx) - abs(d)) + abs(az - tz) > JUMP_MARGIN;
}

/* The deathblow script's opening guard (f8 nn 00 88 right after the leading
 * cc) of a combo deathblow: 1 when it should see the combo flag clear (the
 * jump skipped). */
s32 pcComboSkipJump(SpriteActorCore* actor) {
    const u8* op = actor->m64_spriteByteCode;

    if (!pcGameplayUninterruptedCombos() || comboDeathblow(actor) < 0 || scriptOffset(actor, op) != 3) {
        return 0;
    }
    return !jumpNeeded(actor, op);
}

/* A call (e2) at op to target: 1 when it is a combo deathblow's charge-up
 * after the combo's first deathblow, which then goes on without it. The
 * charge-up is the routine that opens with battle sprite effect 0x69
 * (c3 69), called from the script's own opening (within its first 64 bytes:
 * 11 to 39 in the nine bundles), never from a move. */
s32 pcComboSkipCall(SpriteActorCore* actor, const u8* op, const u8* target) {
    s32 index, offset, i;

    if (target[0] != 0xC3 || target[1] != 0x69 || !pcGameplayUninterruptedCombos()) {
        return 0;
    }
    index = comboDeathblow(actor);
    offset = scriptOffset(actor, op);
    if (index < 0 || offset < 0 || offset >= 64) {
        return 0;
    }
    for (i = 0; i < index; i++) {
        s32 anim = battleBssBlock.m138_currentDamages[i].m47_battleAnimationToPlay;
        if (anim >= 7 && anim <= 13) {
            /* An earlier deathblow of this combo had its charge-up: skip this
             * one's, ending on the sprite set it would end on (effect 0x68). */
            void* own = (s8)actor->mAF_animationId < 0 ? actor->m4C_specialAnimation : actor->m48_defaultAnimationBundle;
            if (actor->m44_currentAnimationBundle != own) {
                setAnimationBundle(actor, own);
                actor->mB0_xA = own == actor->m4C_specialAnimation;
            }
            return 1;
        }
    }
    return 0;
}

/* Op 0xB0 (a battle sound) at op: 1 for the focus sound (0x44) in the opening
 * of a Combo deathblow after the first, which then goes on without it. */
s32 pcComboSkipSound(SpriteActorCore* actor, const u8* op) {
    s32 index, offset, i;

    if (op[1] != 0x44 || !pcGameplayUninterruptedCombos()) {
        return 0;
    }
    index = comboDeathblow(actor);
    offset = scriptOffset(actor, op);
    if (index < 0 || offset < 0 || offset >= 64) {
        return 0;
    }
    for (i = 0; i < index; i++) {
        s32 anim = battleBssBlock.m138_currentDamages[i].m47_battleAnimationToPlay;
        if (anim >= 7 && anim <= 13) {
            return 1;
        }
    }
    return 0;
}
