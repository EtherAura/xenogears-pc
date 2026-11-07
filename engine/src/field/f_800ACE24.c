#include "common.h"
#include "psyq.h"
#include "field/field.h"
#include "main/game_state.h"

extern GameState* pKernelGameState;
extern s32 currentParty[3];          /* Noah: currentParty (0xFF: empty) */
extern s32 partyFieldActorIds[3];            /* Noah: partyToFieldEntityArrayMapping */
extern s16 partyGearSwitchFlags[3];            /* per party slot: isOnGear before a menu, then 1 = switch this slot */
extern s32 partyGearActorIds[3];            /* per party slot: actor id of the slot's gear */
extern sFieldState D_800AFA54;
extern s32 currentFieldActorId;
extern s32 gearSwitchingEnabled;               /* gears can be boarded on this map */

void copyActorPositionsAndMatrix(s32 dest, s32 source);
s32 savePartyMemberPositionVars(s32 slot);
void setVisualAnimation(SpriteActorCore* actor, s32 animation, sFieldEntity* entity);
void stopParticleEffect(s32 actorId, s32 slot);
void exitGear(s32 slot);
void enterGear(s32 slot);
void switchPartyGears(s32 toggle);

/* After a menu: switch the slots whose isOnGear changed (partyGearSwitchFlags holds the
 * state from before the menu). */
void applyPartyGearStateChanges(void) {
    s32 i;

    for (i = 0; i < 3; i++) {
        if (partyGearSwitchFlags[i] == pKernelGameState->m22B1_isOnGear[i]) {
            partyGearSwitchFlags[i] = 0;
        } else {
            partyGearSwitchFlags[i] = 1;
        }
    }
    switchPartyGears(0);
}

/* Everyone with a gear boards it if slot 0 is on foot, else everyone on a
 * gear leaves it. */
void togglePartyGears(void) {
    s32 i;

    partyGearSwitchFlags[0] = partyGearSwitchFlags[1] = partyGearSwitchFlags[2] = 0;
    if (pKernelGameState->m22B1_isOnGear[0] == 0) {
        for (i = 0; i < 3; i++) {
            if (pKernelGameState->m22B1_isOnGear[i] == 0 && getPartyMemberGearNum(currentParty[i]) != 0xFF) {
                partyGearSwitchFlags[i] = 1;
            }
        }
    } else {
        for (i = 0; i < 3; i++) {
            if (pKernelGameState->m22B1_isOnGear[i] == 1 && getPartyMemberGearNum(currentParty[i]) != 0xFF) {
                partyGearSwitchFlags[i] = 1;
            }
        }
    }
    switchPartyGears(1);
}

/* The slot's character leaves its gear: swap the sprite sheets back and park
 * the gear where the character stands. */
void exitGear(s32 slot) {
    SpriteActorCore* sheet;
    SpriteActorCore* gearSheet;

    pKernelGameState->m22B1_isOnGear[slot] = 0;
    gearSheet = D_800AFA54.mBC_actorArray[partyGearActorIds[slot]].m4_pVramSpriteSheet;
    sheet = D_800AFA54.mBC_actorArray[partyFieldActorIds[slot]].m4_pVramSpriteSheet;
    D_800AFA54.mBC_actorArray[partyFieldActorIds[slot]].m4_pVramSpriteSheet = gearSheet;
    D_800AFA54.mBC_actorArray[partyGearActorIds[slot]].m4_pVramSpriteSheet = sheet;
    D_800AFA54.mBC_actorArray[partyGearActorIds[slot]].m58_flags = (D_800AFA54.mBC_actorArray[partyGearActorIds[slot]].m58_flags & 0xF07F) | 0x200;
    D_800AFA54.mBC_actorArray[partyGearActorIds[slot]].m58_flags &= ~0x20;
    D_800AFA54.mBC_actorArray[partyGearActorIds[slot]].m4C_scriptEntity->m0_fieldScriptFlags &= ~1;
    copyActorPositionsAndMatrix(partyGearActorIds[slot], partyFieldActorIds[slot]);
    D_800AFA54.mBC_actorArray[partyFieldActorIds[slot]].m4_pVramSpriteSheet->m0_position.vx = D_800AFA54.mBC_actorArray[partyFieldActorIds[slot]].m4C_scriptEntity->m20_position.vx;
    D_800AFA54.mBC_actorArray[partyFieldActorIds[slot]].m4_pVramSpriteSheet->m0_position.vy = D_800AFA54.mBC_actorArray[partyFieldActorIds[slot]].m4C_scriptEntity->m20_position.vy;
    D_800AFA54.mBC_actorArray[partyFieldActorIds[slot]].m4_pVramSpriteSheet->m0_position.vz = D_800AFA54.mBC_actorArray[partyFieldActorIds[slot]].m4C_scriptEntity->m20_position.vz;
    D_800AFA54.mBC_actorArray[partyFieldActorIds[slot]].m4C_scriptEntity->m0_fieldScriptFlags |= 0x400;
    D_800AFA54.mBC_actorArray[partyFieldActorIds[slot]].m4C_scriptEntity->m0_fieldScriptFlags &= ~0x300;
    D_800AFA54.mBC_actorArray[partyGearActorIds[slot]].m4C_scriptEntity->m0_fieldScriptFlags &= ~0x1800;
    D_800AFA54.mBC_actorArray[partyFieldActorIds[slot]].m4C_scriptEntity->m0_fieldScriptFlags &= ~0x1800;
    D_800AFA54.mBC_actorArray[partyGearActorIds[slot]].m4C_scriptEntity->m108_rotation3 = D_800AFA54.mBC_actorArray[partyFieldActorIds[slot]].m4C_scriptEntity->m108_rotation3;
    D_800AFA54.mBC_actorArray[partyGearActorIds[slot]].m4C_scriptEntity->m106_currentRotation = D_800AFA54.mBC_actorArray[partyFieldActorIds[slot]].m4C_scriptEntity->m106_currentRotation;
    D_800AFA54.mBC_actorArray[partyGearActorIds[slot]].m4C_scriptEntity->mE8_currentAnimationId = D_800AFA54.mBC_actorArray[partyGearActorIds[slot]].m4C_scriptEntity->mE6_idleAnimationId;
    D_800AFA54.mBC_actorArray[partyFieldActorIds[slot]].m4C_scriptEntity->mE8_currentAnimationId = D_800AFA54.mBC_actorArray[partyFieldActorIds[slot]].m4C_scriptEntity->mE6_idleAnimationId;
    setVisualAnimation(D_800AFA54.mBC_actorArray[partyGearActorIds[slot]].m4_pVramSpriteSheet, 6, &D_800AFA54.mBC_actorArray[partyGearActorIds[slot]]);
    setVisualAnimation(D_800AFA54.mBC_actorArray[partyFieldActorIds[slot]].m4_pVramSpriteSheet, D_800AFA54.mBC_actorArray[partyFieldActorIds[slot]].m4C_scriptEntity->mE6_idleAnimationId,
                       &D_800AFA54.mBC_actorArray[partyFieldActorIds[slot]]);
    savePartyMemberPositionVars(slot);
    stopParticleEffect(partyGearActorIds[slot], 0);
}

/* The slot's character boards its gear (actor currentFieldActorId). */
void enterGear(s32 slot) {
    SpriteActorCore* sheet;

    sheet = D_800AFA54.mBC_actorArray[currentFieldActorId].m4_pVramSpriteSheet;
    D_800AFA54.mBC_actorArray[currentFieldActorId].m4_pVramSpriteSheet = D_800AFA54.mBC_actorArray[partyFieldActorIds[slot]].m4_pVramSpriteSheet;
    D_800AFA54.mBC_actorArray[partyFieldActorIds[slot]].m4_pVramSpriteSheet = sheet;
    D_800AFA54.mBC_actorArray[partyFieldActorIds[slot]].m4_pVramSpriteSheet->m0_position.vx = D_800AFA54.mBC_actorArray[partyFieldActorIds[slot]].m4C_scriptEntity->m20_position.vx;
    D_800AFA54.mBC_actorArray[partyFieldActorIds[slot]].m4_pVramSpriteSheet->m0_position.vy = D_800AFA54.mBC_actorArray[partyFieldActorIds[slot]].m4C_scriptEntity->m20_position.vy;
    D_800AFA54.mBC_actorArray[partyFieldActorIds[slot]].m4_pVramSpriteSheet->m0_position.vz = D_800AFA54.mBC_actorArray[partyFieldActorIds[slot]].m4C_scriptEntity->m20_position.vz;
    D_800AFA54.mBC_actorArray[currentFieldActorId].m58_flags |= 0x20;
    D_800AFA54.mBC_actorArray[partyFieldActorIds[slot]].m4C_scriptEntity->m0_fieldScriptFlags |= 0x200;
    D_800AFA54.mBC_actorArray[partyFieldActorIds[slot]].m4C_scriptEntity->m0_fieldScriptFlags &= ~0x500;
    pKernelGameState->m22B1_isOnGear[slot] = 1;
    D_800AFA54.mBC_actorArray[partyGearActorIds[slot]].m4C_scriptEntity->m0_fieldScriptFlags &= ~0x1800;
    D_800AFA54.mBC_actorArray[partyFieldActorIds[slot]].m4C_scriptEntity->m0_fieldScriptFlags &= ~0x1800;
    D_800AFA54.mBC_actorArray[partyGearActorIds[slot]].m4C_scriptEntity->mE8_currentAnimationId = D_800AFA54.mBC_actorArray[partyGearActorIds[slot]].m4C_scriptEntity->mE6_idleAnimationId;
    D_800AFA54.mBC_actorArray[partyFieldActorIds[slot]].m4C_scriptEntity->mE8_currentAnimationId = D_800AFA54.mBC_actorArray[partyFieldActorIds[slot]].m4C_scriptEntity->mE6_idleAnimationId;
    setVisualAnimation(D_800AFA54.mBC_actorArray[partyGearActorIds[slot]].m4_pVramSpriteSheet, D_800AFA54.mBC_actorArray[partyGearActorIds[slot]].m4C_scriptEntity->mE6_idleAnimationId,
                       &D_800AFA54.mBC_actorArray[partyGearActorIds[slot]]);
    setVisualAnimation(D_800AFA54.mBC_actorArray[partyFieldActorIds[slot]].m4_pVramSpriteSheet, D_800AFA54.mBC_actorArray[partyFieldActorIds[slot]].m4C_scriptEntity->mE6_idleAnimationId,
                       &D_800AFA54.mBC_actorArray[partyFieldActorIds[slot]]);
    stopParticleEffect(partyGearActorIds[slot], 0);
    savePartyMemberPositionVars(slot);
}

/* Reapply the script flags of the characters riding a gear. */
void restoreOnGearScriptFlags(void) {
    s32 i;

    if (gearSwitchingEnabled != 0) {
        for (i = 0; i < 3; i++) {
            if (partyFieldActorIds[i] != 0xFF && pKernelGameState->m22B1_isOnGear[i] == 1) {
                D_800AFA54.mBC_actorArray[partyFieldActorIds[i]].m4C_scriptEntity->m0_fieldScriptFlags |= 0x200;
                D_800AFA54.mBC_actorArray[partyFieldActorIds[i]].m4C_scriptEntity->m0_fieldScriptFlags &= ~0x500;
            }
        }
    }
}

/* Switch every marked slot: toggle = 0 applies isOnGear, toggle = 1 inverts it. */
void switchPartyGears(s32 toggle) {
    s32 i;

    if (gearSwitchingEnabled != 0) {
        for (i = 0; i < 3; i++) {
            if (partyFieldActorIds[i] != 0xFF && partyGearSwitchFlags[i] == 1) {
                currentFieldActorId = partyGearActorIds[i];
                if (pKernelGameState->m22B1_isOnGear[i] == 0) {
                    if (toggle != 0) {
                        enterGear(i);
                    } else {
                        exitGear(i);
                    }
                } else {
                    if (toggle == 0) {
                        enterGear(i);
                    } else {
                        exitGear(i);
                    }
                }
            }
        }
    }
}
