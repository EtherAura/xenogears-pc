/* The retry setting's checkpoint (port/src/game/main/retry.c) with synthetic
 * game state: what it keeps, what a restore brings back, when it applies. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "common.h"
#include "main/game_state.h"
#include "pc_gameplay.h"
#include "system/gameplay.h"
#include "test/env.h"

static int checks;
#include "test/check.h"

#ifndef _WIN32
GameState gameState;
u8 overlayStateSaveBlock[0x8000];
#if !VERSION_JP
s32 savedPartyIsOnGear[3];
#endif
u8 battleConfigs[0x220];
u8 requestedBattleConfig, battleTransitionEffect, battleInitVar0, battleInitVar1, victoryFanfarePlayed;
s32 pcRetryAvailable(void);
void pcRetryArm(void);
int pcGauntletState(void) { return 0; } /* no gauntlet (port/src/system/gauntlet.c) */

static void fill(int seed) {
    memset(&gameState, seed, sizeof(gameState));
    memset(overlayStateSaveBlock, seed + 1, sizeof(overlayStateSaveBlock));
#if !VERSION_JP
    savedPartyIsOnGear[0] = savedPartyIsOnGear[1] = savedPartyIsOnGear[2] = seed + 2;
#endif
    memset(battleConfigs, seed + 3, sizeof(battleConfigs));
    requestedBattleConfig = seed + 4;
    battleTransitionEffect = seed + 5;
    battleInitVar1 = seed & 1;
}

static int same(int seed) {
    GameState g;
    u8 block[0x8000];
    u8 configs[0x220];
    memset(&g, seed, sizeof(g));
    memset(block, seed + 1, sizeof(block));
    memset(configs, seed + 3, sizeof(configs));
    return !memcmp(&g, &gameState, sizeof(g)) && !memcmp(block, overlayStateSaveBlock, sizeof(block)) &&
#if !VERSION_JP
           savedPartyIsOnGear[2] == seed + 2 &&
#endif
           !memcmp(configs, battleConfigs, sizeof(configs)) && requestedBattleConfig == (u8)(seed + 4) &&
           battleTransitionEffect == (u8)(seed + 5) && battleInitVar1 == (seed & 1);
}

static int native(void) {
    /* Off: no checkpoint, no prompt, nothing restored. */
    testSetEnv("XG_BATTLE_RETRY", "0");
    fill(0x10);
    pcRetryCheckpoint();
    CHECK(!pcRetryAvailable());
    pcRetryArm();
    CHECK(battleInitVar0 == 0);
    fill(0x40);
    CHECK(!pcRetryRestore() && same(0x40));

    /* On: the checkpoint as the battle starts; the lost attempt changes
     * everything; a restore without the player's choice does nothing. */
    testSetEnv("XG_BATTLE_RETRY", "1");
    fill(0x21);
    pcRetryCheckpoint();
    CHECK(pcRetryAvailable());
    fill(0x52);
    CHECK(!pcRetryRestore() && same(0x52));
    /* Retry: the chain is set at the choice, the restore brings back every
     * byte, once. */
    victoryFanfarePlayed = 0;
    pcRetryArm();
    CHECK(battleInitVar0 == (u8)(0x21 + 4 + 1));
    CHECK(pcRetryRestore());
    CHECK(same(0x21) && battleInitVar0 == (u8)(0x21 + 4 + 1) && victoryFanfarePlayed == 1);
    pcGameplayBattleStart(requestedBattleConfig, 1); /* the battle starts again */
    battleInitVar0 = 0;
    CHECK(!pcRetryRestore() && battleInitVar0 == 0);

    /* A new battle starts a new checkpoint and forgets an unused choice. */
    fill(0x30);
    pcRetryCheckpoint();
    pcRetryArm();
    battleInitVar0 = 0;
    fill(0x31);
    pcRetryCheckpoint(); /* the next battle */
    CHECK(!pcRetryRestore() && same(0x31));
    /* The setting turned off after the checkpoint: no prompt. */
    testSetEnv("XG_BATTLE_RETRY", "0");
    CHECK(!pcRetryAvailable());

    /* The retried battle keeps its reward class though it starts as a chain. */
    testSetEnv("XG_BATTLE_RETRY", "1");
    pcGameplayRandomBattle(3);
    pcGameplayBattleStart(3, 0);
    CHECK(pcGameplayBattleClass() == GAMEPLAY_BATTLE_RANDOM);
    fill(0x60);
    pcRetryCheckpoint();
    pcRetryArm();
    CHECK(pcRetryRestore());
    pcGameplayBattleStart(requestedBattleConfig, 1);
    CHECK(pcGameplayBattleClass() == GAMEPLAY_BATTLE_RANDOM);
    pcGameplayBattleStart(requestedBattleConfig, 1); /* a real chain after it */
    CHECK(pcGameplayBattleClass() == GAMEPLAY_BATTLE_SCRIPTED);
    return 0;
}
#endif

int main(int argc, char** argv) {
    if (argc != 3) return 2;
    char path[1200]; snprintf(path, sizeof(path), "%s/retry.ini", argv[1]);
    testSetEnv("XG_SETTINGS", path);
    if (!strcmp(argv[2], "write")) {
        pcGameplaySetBattleRetry(1);
        CHECK(pcGameplayBattleRetry());
    } else if (!strcmp(argv[2], "read")) {
        CHECK(pcGameplayBattleRetry());
    } else {
        CHECK(!pcGameplayBattleRetry());
        testSetEnv("XG_BATTLE_RETRY", "on"); CHECK(pcGameplayBattleRetry());
        testSetEnv("XG_BATTLE_RETRY", "junk"); CHECK(!pcGameplayBattleRetry());
#ifndef _WIN32
        if (native()) return 1;
#endif
    }
    printf("retry %s: %d checks passed\n", argv[2], checks);
    return 0;
}
