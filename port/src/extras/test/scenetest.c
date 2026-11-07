/* Start ownership and validation without a disc image or renderer. */
#include <stdio.h>
#include <string.h>
#include "common.h"
#include "main/game_state.h"
#include "extras/gallery_scene.h"
#include "test/env.h"

GameState gameState;
static int disc = 1, swapOK = 1, reloads;
int cdBootDiscNumber(void) { return disc; }
int cdSwapForReset(void) { if (!swapOK) return 0; disc = 3 - disc; return 1; }
void cdBootTablesReload(void) { reloads++; }
void portStartState(void);
void portStartPrepare(void);
int portStartPending(void);
int portBootMode(void);
static int checks;
#include "test/check.h"

int main(void) {
    CHECK(!portSceneReplayActive());
    XgScene scene = {.field = 14, .setsTo = 6, .progress = 3, .entry = 1, .disc = 1};
    PortStartRequest r;
    const XgScenePreset* p = xgScenePreset(&scene, "US");
    CHECK(p != NULL && xgScenePreset(&scene, "JP") == p);
    CHECK(xgScenePreset(&scene, "EU") == NULL && xgScenePreset(&scene, NULL) == NULL);
    xgSceneStart(&scene, "US", &r);
    CHECK(xgScenePresetMatches(p, &r));
    CHECK(xgSceneStartProtection(p, &r, 0) == NULL);
    r.entry++; CHECK(!xgScenePresetMatches(p, &r)); r.entry--;
    r.party[1] = 255; CHECK(!xgScenePresetMatches(p, &r)); r.party[1] = 2;
    r.party[1] = 255;
    CHECK(xgSceneStartProtection(p, &r, 0) != NULL);
    CHECK(xgSceneStartProtection(p, &r, 1) == NULL);
    r.party[1] = 2;
    r.entry++;
    CHECK(xgSceneStartProtection(p, &r, 0) != NULL);
    r.entry--;
    r.variableCount = 1; r.variables[0] = (PortStartVariable){225, -1};
    CHECK(!xgScenePresetMatches(p, &r));
    r.disc = 2; r.party[1] = 255;
    CHECK(portRequestStart(&r) && portStartPending() && portBootMode() == 1);
    CHECK(!portSceneReplayActive()); /* a request has not started playback */
    r.progress = 99; r.variables[0].value = 123; /* request owns a copy */
    PortStartRequest bad = r;
    bad.party[1] = 0; CHECK(!portRequestStart(&bad));
    bad = r; bad.party[0] = 11; CHECK(!portRequestStart(&bad));
    bad = r; bad.variables[0].index = 512; CHECK(!portRequestStart(&bad));
    bad.variables[0].index = 0; CHECK(!portRequestStart(&bad));
    bad.variables[0].index = 1; CHECK(!portRequestStart(&bad));
    bad.variables[0].index = 82; CHECK(!portRequestStart(&bad));
    bad = r; bad.variableCount = PORT_START_VARIABLES + 1; CHECK(!portRequestStart(&bad));
    bad = r; bad.variableCount = 2; bad.variables[1] = bad.variables[0]; CHECK(!portRequestStart(&bad));
    bad = r; bad.variables[0].value = 65536; CHECK(!portRequestStart(&bad));
    bad = r; bad.field = -1; CHECK(!portRequestStart(&bad));
    CHECK(!portRequestStart(NULL));
    portStartPrepare(); CHECK(disc == 2 && reloads == 1 && portStartPending());
    memset(&gameState, 0, sizeof(gameState));
    testSetEnv("XG_PARTY", "9,8,7"); testSetEnv("XG_GAMESTATE", "0x1930=123");
    portStartState();
    CHECK(portSceneReplayActive());
    CHECK(!portStartPending() && portBootMode() == 1);
    CHECK(gameState.m231A_fieldID == 14 && gameState.m2320_worldmapMode == 1);
    CHECK(gameState.m1930_fieldVarsBackup[0] == 3 && gameState.m1930_fieldVarsBackup[82] == 1);
    CHECK(gameState.m1930_fieldVarsBackup[225] == 65535);
    CHECK(gameState.m1D30_partyMemberBitField == 1 && gameState.m1D34_currentParty[0] == 0 &&
          gameState.m1D34_currentParty[1] == 255 && gameState.m1D34_currentParty[2] == 255);
    testSetEnv("XG_PARTY", ""); testSetEnv("XG_GAMESTATE", "");
    xgSceneStart(&scene, "JP", &r); CHECK(portRequestStart(&r));
    portStartPrepare(); memset(&gameState, 0, sizeof(gameState)); portStartState();
    CHECK(disc == 1 && gameState.m1930_fieldVarsBackup[225] == 0); /* no prior request's patches */
    CHECK(gameState.m1D30_partyMemberBitField == 5 && gameState.m1D34_currentParty[1] == 2);
    r.disc = 2; swapOK = 0; CHECK(portRequestStart(&r));
    portStartPrepare(); memset(&gameState, 0, sizeof(gameState)); portStartState();
    CHECK(disc == 1 && gameState.m1930_fieldVarsBackup[82] == 0); /* actual drive on failed swap */
    scene.field = 19; scene.setsTo = 10; scene.entry = 7; scene.progress = 8;
    CHECK(xgScenePreset(&scene, "US") == NULL);
    xgSceneStart(&scene, "US", &r);
    CHECK(r.field == 19 && r.entry == 7 && r.progress == 8 && !r.variableCount);
    CHECK(!xgScenePresetMatches(NULL, &r));
    CHECK(xgSceneStartProtection(NULL, &r, 0) != NULL);
    CHECK(xgSceneStartProtection(NULL, &r, 1) == NULL);
    CHECK(xgSceneStartProtection(NULL, NULL, 1) != NULL);
    scene = (XgScene){.field = 80, .setsTo = 55, .progress = 54, .entry = 1, .disc = 1};
    p = xgScenePreset(&scene, "US");
    CHECK(p != NULL && xgScenePreset(&scene, "JP") == p);
    xgSceneStart(&scene, "US", &r);
    CHECK(r.party[0] == 0 && r.party[1] == 255 && r.party[2] == 255 && r.entry == 1);
    r.party[1] = 2; CHECK(!xgScenePresetMatches(p, &r));
    scene = (XgScene){.field = 74, .setsTo = 56, .progress = 55, .entry = 0, .disc = 1};
    p = xgScenePreset(&scene, "JP");
    CHECK(p != NULL && xgScenePreset(&scene, "US") == p);
    xgSceneStart(&scene, "JP", &r);
    CHECK(r.entry == 6 && r.progress == 55 && r.party[0] == 0 && r.party[1] == 255);
    CHECK(xgScenePresetMatches(p, &r));
    scene.disc = 2; CHECK(xgScenePreset(&scene, "US") == NULL);
    scene.disc = 1; scene.setsTo = 57; CHECK(xgScenePreset(&scene, "US") == NULL);
    /* xg-qkf: the entrances once blocked have verified presets, and no
     * entrance is blocked for a custom start (the crashes were PC defects).
     * xg-55m: arrivals whose field start sets the party and the story, and
     * fields whose start sets the party whatever comes in. */
    static const struct { int field, setsTo, scanEntry, entry, progress, party[3]; } qkf[] = {
        {13, 7, 0, 1, 6, {0, 255, 255}},
        {79, 57, 0, 0, 56, {0, 255, 255}},
        {134, 93, 0, 0, 90, {3, 2, 255}},
        {315, 142, 3, 4, 141, {0, 1, 255}}, {315, 146, 8, 8, 144, {0, 3, 255}}, {411, 174, 2, 2, 171, {0, 1, 255}},
        {315, 141, 4, 4, 140, {0, 1, 255}}, {315, 145, 4, 3, 144, {0, 3, 255}}, {312, 144, 2, 2, 143, {0, 3, 2}},
        {313, 148, 2, 2, 147, {1, 2, 3}}, {412, 203, 4, 4, 202, {0, 1, 2}}, {325, 145, 0, 0, 144, {1, 255, 255}},
        {453, 208, 3, 3, 207, {0, 8, 7}}, {511, 210, 0, 0, 209, {0, 2, 1}},
        {314, 143, 2, 2, 142, {0, 1, 255}}, {290, 145, 1, 1, 144, {0, 3, 2}},
        {314, 146, 2, 4, 144, {0, 3, 1}}, {286, 147, 0, 0, 146, {0, 1, 2}},
        {320, 149, 0, 0, 148, {1, 2, 3}}, {420, 175, 0, 1, 174, {0, 1, 255}},
        {529, 213, 0, 4, 210, {0, 1, 255}},
        {23, 27, 0, 0, 24, {0, 255, 255}}, {23, 32, 0, 0, 31, {0, 255, 255}},
        {24, 33, 0, 0, 32, {0, 255, 255}}, {24, 36, 0, 0, 34, {0, 2, 1}},
        {35, 38, 16, 16, 37, {0, 2, 255}}, {303, 204, 4, 4, 203, {0, 1, 2}},
        {62, 45, 0, 0, 43, {0, 2, 255}}, {72, 48, 0, 0, 45, {0, 255, 255}},
        {73, 54, 0, 0, 52, {0, 3, 255}}, {104, 61, 0, 0, 60, {0, 3, 2}},
        {80, 59, 2, 2, 58, {0, 255, 255}}, {218, 105, 0, 0, 102, {0, 255, 255}},
        {225, 129, 0, 0, 123, {0, 1, 5}},
        {314, 141, 2, 0, 140, {0, 1, 255}}, {327, 155, 2, 2, 154, {0, 1, 2}},
        {335, 158, 0, 0, 155, {1, 3, 5}}, {360, 160, 0, 0, 158, {0, 1, 2}},
        {293, 167, 0, 0, 166, {0, 1, 2}},
        {437, 176, 0, 3, 175, {0, 1, 255}}, {423, 177, 0, 0, 176, {0, 1, 2}},
        {473, 181, 0, 0, 180, {0, 1, 2}},
        {538, 216, 0, 0, 213, {0, 1, 255}}, {579, 228, 0, 0, 222, {0, 1, 255}},
        {593, 234, 0, 0, 231, {0, 255, 255}}, {595, 234, 0, 0, 231, {0, 255, 255}},
        {460, 242, 0, 0, 241, {0, 1, 255}}, {467, 243, 0, 0, 242, {0, 1, 255}},
        {452, 209, 0, 0, 208, {0, 8, 7}}};
    for (int i = 0; i < (int)(sizeof(qkf) / sizeof(qkf[0])); i++) {
        scene = (XgScene){.field = qkf[i].field, .setsTo = qkf[i].setsTo, .progress = 0, .entry = qkf[i].scanEntry, .disc = 1};
        p = xgScenePreset(&scene, "US");
        CHECK(p != NULL && xgScenePreset(&scene, "JP") == p);
        xgSceneStart(&scene, "US", &r);
        CHECK(r.field == qkf[i].field && r.entry == qkf[i].entry && r.progress == qkf[i].progress);
        CHECK(r.party[0] == qkf[i].party[0] && r.party[1] == qkf[i].party[1] && r.party[2] == qkf[i].party[2]);
        CHECK(xgScenePresetMatches(p, &r) && xgSceneStartProtection(p, &r, 0) == NULL);
        CHECK(xgSceneStartProblem(&r) == NULL);
        if (r.field == 134 || (r.field == 314 && scene.setsTo == 143) ||
            r.field == 290 || r.field == 452 || r.field == 79 || r.field == 420 ||
            r.field == 303 || r.field == 437) {
            /* Carried story flags are part of the verified request; an
             * edited value must restore the override requirement. */
            int index = r.field == 134 || r.field == 314 || r.field == 420 || r.field == 437 ? 42 :
                        r.field == 303 ? 137 : r.field == 290 ? 198 : r.field == 79 ? 225 : 360;
            int value = r.field == 134 || r.field == 437 ? 2 : r.field == 314 ? 9 :
                        r.field == 303 ? 1 : r.field == 420 ? 4 : r.field == 290 ? 32768 : r.field == 79 ? 4610 : 8;
            CHECK(r.variableCount == (r.field == 437 ? 3 : r.field == 452 || r.field == 420 || r.field == 303 ? 2 : 1));
            CHECK(r.variables[0].index == index && r.variables[0].value == value);
            if (r.field == 452)
                CHECK(r.variables[1].index == 361 && r.variables[1].value == 7);
            if (r.field == 420)
                CHECK(r.variables[1].index == 266 && r.variables[1].value == 256);
            if (r.field == 303)
                CHECK(r.variables[1].index == 138 && r.variables[1].value == 2);
            if (r.field == 437) {
                CHECK(r.variables[1].index == 255 && r.variables[1].value == 512);
                CHECK(r.variables[2].index == 266 && r.variables[2].value == 256);
            }
            r.variables[0].value++;
            CHECK(!xgScenePresetMatches(p, &r) && xgSceneStartProtection(p, &r, 0) != NULL);
            r.variables[0].value--;
            CHECK(portRequestStart(&r));
            portStartPrepare(); memset(&gameState, 0, sizeof(gameState)); portStartState();
            CHECK(gameState.m1930_fieldVarsBackup[index] == value);
            if (r.field == 452) CHECK(gameState.m1930_fieldVarsBackup[361] == 7);
            if (r.field == 420) CHECK(gameState.m1930_fieldVarsBackup[266] == 256);
            if (r.field == 303) CHECK(gameState.m1930_fieldVarsBackup[138] == 2);
            if (r.field == 437)
                CHECK(gameState.m1930_fieldVarsBackup[255] == 512 && gameState.m1930_fieldVarsBackup[266] == 256);
        }
    }
    static const struct { int field, setsTo, entry, progress, party[3]; } disc2[] = {
        {709, 298, 1, 295, {0, 255, 255}}, {711, 300, 0, 298, {0, 255, 255}},
        {713, 301, 0, 300, {0, 255, 255}}, {712, 302, 0, 301, {0, 255, 255}},
        {714, 303, 0, 302, {0, 4, 9}}};
    for (int i = 0; i < (int)(sizeof(disc2) / sizeof(disc2[0])); i++) {
        scene = (XgScene){.field = disc2[i].field, .setsTo = disc2[i].setsTo, .disc = 2};
        p = xgScenePreset(&scene, "US");
        CHECK(p != NULL && xgScenePreset(&scene, "JP") == p);
        xgSceneStart(&scene, "US", &r);
        CHECK(r.disc == 2 && r.entry == disc2[i].entry && r.progress == disc2[i].progress);
        CHECK(r.party[0] == disc2[i].party[0] && r.party[1] == disc2[i].party[1] && r.party[2] == disc2[i].party[2]);
        CHECK(!r.variableCount && xgScenePresetMatches(p, &r) && xgSceneStartProtection(p, &r, 0) == NULL);
        CHECK(portRequestStart(&r));
        swapOK = 1; portStartPrepare(); memset(&gameState, 0, sizeof(gameState)); portStartState();
        CHECK(disc == 2 && gameState.m1930_fieldVarsBackup[82] == 1);
        CHECK(gameState.m231A_fieldID == disc2[i].field && gameState.m1D34_currentParty[1] == disc2[i].party[1]);
        scene.disc = 1; CHECK(xgScenePreset(&scene, "US") == NULL);
    }
    scene = (XgScene){.field = 713, .setsTo = 303, .progress = 302, .entry = 1, .disc = 2};
    CHECK(xgScenePreset(&scene, "US") == NULL); /* unresolved return stays protected */
    xgSceneStart(&scene, "US", &r);
    CHECK(xgSceneStartProtection(NULL, &r, 0) != NULL);
    r.field = 315; r.entry = 3; CHECK(xgSceneStartProblem(&r) == NULL);
    CHECK(xgSceneStartProtection(NULL, &r, 1) == NULL && xgSceneStartProtection(NULL, &r, 0) != NULL);
    portStartPrepare(); /* a reset without a new request ends replay policy */
    portStartState();
    CHECK(!portSceneReplayActive() && portBootMode() == -1);
    r = (PortStartRequest){.field = 80, .entry = 1, .progress = 54, .disc = 1, .party = {0, 0, 255}};
    CHECK(!portRequestStart(&r)); /* override never bypasses structural checks */
    r.party[1] = 11; CHECK(!portRequestStart(&r));
    r.party[0] = 255; r.party[1] = 2; CHECK(!portRequestStart(&r));
    r.party[1] = 255; CHECK(!portRequestStart(&r));
    printf("Gallery scene starts: %d checks passed\n", checks);
    return 0;
}
