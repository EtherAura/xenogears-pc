/*
 * A jump pressed as a random battle comes (Original-game bug fixes;
 * xg-htn.35.156).
 *
 * A random encounter is rolled from the player's update while a direction is
 * held, on the ground or in the air (checkForRandomEncounter). From then until
 * the battle starts (bBattleSequenceInitialized, then fieldBattleMusicPending
 * once the field loop has started the battle music: 13-20 vblanks, measured
 * in Black Moon Forest) the game ignores the jump button
 * (updateFieldInputs: padButtonForDialogs bit 0x80 while playMusicAuthorized
 * is 0), so a jump pressed then is lost: the player stands still and the
 * battle comes. The loop's first step waits for a jump in the air to land
 * (isFieldBattlePrevented); its second, the battle itself, does not, which is
 * why no new jump may start. With the fix the pending battle lets one jump
 * through and its start waits for that jump to land, as the first step does.
 * Only one: a jump under way when the encounter came is that one, and spent,
 * the button is ignored again, so a battle can't be put off by jumping.
 * Scripted battles (OP_70/71: playMusicAuthorized 0 without either flag) keep
 * the original. Off: the original.
 */
#include "common.h"
#include "field/field.h"
#include "pc_gameplay.h"

extern s32 bBattleSequenceInitialized;
extern s32 fieldBattleMusicPending;
extern s16 updateEntityEventCode4Var0;
extern s32 g_playerControlledActor;
extern FieldBss_800AF880 fieldSceneState;

/* The one jump of this pending battle has been taken (a plain value: saved
 * with the field module, reset whenever no random battle is pending). */
static s32 jumpTaken;

static s32 randomBattlePending(void) {
    return bBattleSequenceInitialized == 1 || fieldBattleMusicPending == 1;
}

static s32 playerJumping(void) {
    sFieldEntity* actors = fieldSceneState.m290_actorArray;

    return actors != NULL && actors[g_playerControlledActor].m4C_scriptEntity != NULL &&
           (actors[g_playerControlledActor].m4C_scriptEntity->m0_fieldScriptFlags & 0x800);
}

/* updateFieldInputs, where the original drops the jump button while a battle
 * is pending: 1 to keep it this frame. */
s32 pcFieldBattleJumpAllowed(void) {
    if (!randomBattlePending()) {
        jumpTaken = 0;
        return 0;
    }
    if (!pcGameplayOriginalBugFixes()) {
        return 0;
    }
    if (playerJumping()) {
        jumpTaken = 1;
    }
    return !jumpTaken;
}

/* The field loop's battle start, once the battle music has started: 1 while
 * the player's jump has yet to land (isFieldBattlePrevented's test, for the
 * step it leaves out). */
s32 pcFieldBattleWaitsForLanding(void) {
    return fieldBattleMusicPending == 1 && updateEntityEventCode4Var0 == 0 && pcGameplayOriginalBugFixes() &&
           playerJumping();
}
