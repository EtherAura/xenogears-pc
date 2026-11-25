/* A jump pressed as a random battle comes (Original-game bug fixes;
 * xg-htn.35.156): port/src/game/field/battlejump.c on a synthetic field, the
 * pending battle's two phases and the one jump it lets through. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "field/field.h"
#include "pc_gameplay.h"
#include "system/gameplay.h"
#include "test/env.h"

static int checks;
#include "test/check.h"

#ifndef _WIN32
s32 bBattleSequenceInitialized;
s32 fieldBattleMusicPending;
s16 updateEntityEventCode4Var0;
s32 g_playerControlledActor;
FieldBss_800AF880 fieldSceneState;
s32 pcFieldBattleJumpAllowed(void);
s32 pcFieldBattleWaitsForLanding(void);

static sFieldEntity actors[3];
static sFieldScriptEntity player;

static void jumping(int on) {
    player.m0_fieldScriptFlags = on ? 0x800 : 0;
}

static int decisions(void) {
    fieldSceneState.m290_actorArray = actors;
    g_playerControlledActor = 2;
    actors[2].m4C_scriptEntity = &player;

    /* Off: the original, the button dropped while the battle is pending. */
    testSetEnv("XG_ORIGINAL_BUG_FIXES", "0");
    bBattleSequenceInitialized = 1;
    CHECK(!pcFieldBattleJumpAllowed());
    jumping(1);
    fieldBattleMusicPending = 1, bBattleSequenceInitialized = 0;
    CHECK(!pcFieldBattleWaitsForLanding());

    testSetEnv("XG_ORIGINAL_BUG_FIXES", "1");
    /* No battle pending: nothing to do. */
    jumping(0);
    fieldBattleMusicPending = 0;
    CHECK(!pcFieldBattleJumpAllowed());
    CHECK(!pcFieldBattleWaitsForLanding());
    /* The encounter rolled with the player on the ground: one jump goes
     * through, in either phase, and none after it. */
    bBattleSequenceInitialized = 1;
    CHECK(pcFieldBattleJumpAllowed());
    bBattleSequenceInitialized = 0, fieldBattleMusicPending = 1;
    CHECK(pcFieldBattleJumpAllowed());
    CHECK(!pcFieldBattleWaitsForLanding());
    jumping(1); /* it started */
    CHECK(!pcFieldBattleJumpAllowed());
    CHECK(pcFieldBattleWaitsForLanding()); /* the battle waits for it */
    jumping(0); /* landed */
    CHECK(!pcFieldBattleJumpAllowed());
    CHECK(!pcFieldBattleWaitsForLanding());
    /* The battle starts: the next pending battle has its jump again. */
    fieldBattleMusicPending = 0;
    CHECK(!pcFieldBattleJumpAllowed());
    bBattleSequenceInitialized = 1;
    CHECK(pcFieldBattleJumpAllowed());
    /* Rolled in the air: that jump was the one. */
    bBattleSequenceInitialized = 0;
    pcFieldBattleJumpAllowed();
    jumping(1);
    bBattleSequenceInitialized = 1;
    CHECK(!pcFieldBattleJumpAllowed());
    /* A scripted jump mode (updateEntityEventCode4Var0) is left as it is. */
    bBattleSequenceInitialized = 0, fieldBattleMusicPending = 1, updateEntityEventCode4Var0 = 1;
    CHECK(!pcFieldBattleWaitsForLanding());
    updateEntityEventCode4Var0 = 0;
    /* No actors (between fields): no jump to wait for. */
    fieldSceneState.m290_actorArray = NULL;
    CHECK(!pcFieldBattleWaitsForLanding());
    return 0;
}
#endif

int main(int argc, char** argv) {
    if (argc != 3) return 2;
    char path[1200]; snprintf(path, sizeof(path), "%s/battlejump.ini", argv[1]);
    testSetEnv("XG_SETTINGS", path);
    if (!strcmp(argv[2], "write")) {
        pcGameplaySetOriginalBugFixes(1);
        CHECK(pcGameplayOriginalBugFixes());
    } else if (!strcmp(argv[2], "read")) {
        CHECK(pcGameplayOriginalBugFixes());
    } else {
        CHECK(!pcGameplayOriginalBugFixes());
#ifndef _WIN32
        if (decisions()) return 1;
#endif
    }
    printf("battle jump %s: %d checks passed\n", argv[2], checks);
    return 0;
}
