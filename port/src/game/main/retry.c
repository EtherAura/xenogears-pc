/*
 * Retry a lost battle (F1 > Gameplay > Retry lost battles, off by default:
 * pcGameplayBattleRetry, port/src/system/gameplay.c; xg-htn.35.89).
 *
 * A checkpoint in memory only, taken as each battle starts (battleMain,
 * src/battle/b_80070E2C.c, just before it copies its config), when the field
 * or the world map has already written everything the game needs to go on
 * after the battle:
 *   - gameState: the party, Gears, inventory, gold, the story variables
 *     (m1930, the field's saveStateToKernel), the field or world-map place
 *     (m231A-m2320) and the disc (m1930[82]);
 *   - the save block overlayStateSaveBlock, where the field (saveFieldStateToSaveBlock:
 *     its actors and variables) or the world map (backupWorldmapStateForBattle)
 *     keeps itself for its return, and US savedPartyIsOnGear (who rides a Gear);
 *   - the battle request: battleConfigs, requestedBattleConfig,
 *     battleTransitionEffect, battleInitVar1.
 * Nothing is written to a memory card: saves stay as they were.
 *
 * When the party is wiped in a battle that would end in the native game over
 * (battle_boot.c, battleRunningVar1 0x81) and the player chose Retry
 * (port/src/game/battle/retryprompt.c), the battle finishes as before a
 * chained battle (battleInitVar0 = config + 1, set at the choice), and
 * battle_boot restores the checkpoint and starts the battle again the way a
 * chained battle starts: its own transition, and victoryFanfarePlayed so battleMain
 * brings the battle music back up (playBattleMusic: a fade to full volume, a
 * no-op when it already is). Everything the lost attempt changed in those
 * places (items it used, the damage, a battle event's variables) goes back;
 * it earned no EXP or items (finishBattle gives none on a wipe). The random
 * seed keeps running, so a retry can play out differently. Enemy stats are
 * applied as the battle loads (pcGameplayEnemyStats), so difficulty changed
 * in F1 at the prompt applies to the retry.
 *
 * A gauntlet's battles (port/src/game/main/gauntlet.c) always have a
 * checkpoint and the choice, the setting on or off; there Game over ends
 * the run instead of the game.
 */
#include "common.h"
#include "main/game_state.h"
#include "pc_gameplay.h"

int pcGameplayBattleRetry(void);      /* port/src/system/gameplay.c: the setting */
void pcGameplayBattleRetryClass(void); /* the retry keeps its reward class */

extern u8 overlayStateSaveBlock[0x8000]; /* the save block (the PC's is 32 KB) */
#if !VERSION_JP
extern s32 savedPartyIsOnGear[3];
#endif
extern u8 battleConfigs[0x220];
extern u8 requestedBattleConfig;
extern u8 battleTransitionEffect;
extern u8 battleInitVar0;
extern u8 battleInitVar1;
extern u8 victoryFanfarePlayed;

static GameState savedState;
static u8 savedBlock[sizeof(overlayStateSaveBlock)];
#if !VERSION_JP
static s32 savedOnGear[3];
#endif
static u8 savedConfigs[sizeof(battleConfigs)];
static u8 savedConfig;
static u8 savedTransition;
static u8 savedInitVar1;
static s32 checkpointValid;
static s32 retryArmed;

/* battleMain, as a battle starts (a retried one too: the same state). */
void pcRetryCheckpoint(void) {
    retryArmed = 0;
    checkpointValid = pcGameplayBattleRetry() != 0 || pcGauntletState() == 2; /* a gauntlet's fight always */
    if (!checkpointValid) {
        return;
    }
    __builtin_memcpy(&savedState, &gameState, sizeof(savedState));
    __builtin_memcpy(savedBlock, overlayStateSaveBlock, sizeof(savedBlock));
#if !VERSION_JP
    __builtin_memcpy(savedOnGear, savedPartyIsOnGear, sizeof(savedOnGear));
#endif
    __builtin_memcpy(savedConfigs, battleConfigs, sizeof(savedConfigs));
    savedConfig = requestedBattleConfig;
    savedTransition = battleTransitionEffect;
    savedInitVar1 = battleInitVar1;
}

/* The prompt is offered only with a checkpoint of this battle. */
s32 pcRetryAvailable(void) {
    return checkpointValid && (pcGameplayBattleRetry() || pcGauntletState() == 2);
}

/* The player chose Retry for the battle that just ended (gauntlet.c). */
s32 pcRetryArmed(void) {
    return retryArmed;
}

/* The player chose Retry, before the battle finishes: battleInitVar0 set
 * now makes finishBattle prepare the next battle, as for a chain (battle
 * event op 0x24), instead of preloading the field's party files for the
 * game over (resetFieldAndPreloadPartyFiles: their pending loads would not
 * survive the next mode's fresh heap). battle_boot takes it. */
void pcRetryArm(void) {
    retryArmed = checkpointValid;
    if (retryArmed) {
        battleInitVar0 = savedConfig + 1;
    }
}

/* battle_boot.c, where the native game over would start: 1 when the battle
 * starts again from its checkpoint. */
int pcRetryRestore(void) {
    if (!retryArmed) {
        return 0;
    }
    retryArmed = 0;
    __builtin_memcpy(&gameState, &savedState, sizeof(savedState));
    __builtin_memcpy(overlayStateSaveBlock, savedBlock, sizeof(savedBlock));
#if !VERSION_JP
    __builtin_memcpy(savedPartyIsOnGear, savedOnGear, sizeof(savedOnGear));
#endif
    __builtin_memcpy(battleConfigs, savedConfigs, sizeof(savedConfigs));
    requestedBattleConfig = savedConfig;
    battleTransitionEffect = savedTransition;
    battleInitVar1 = savedInitVar1;
    battleInitVar0 = savedConfig + 1;
    victoryFanfarePlayed = 1;
    pcGameplayBattleRetryClass();
    return 1;
}
