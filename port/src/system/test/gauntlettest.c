/* Gauntlets: the plan, settings and records (port/src/system/gauntlet.c) and
 * the game's side (port/src/game/main/gauntlet.c) with synthetic game state:
 * the parties, the skips, the chain from fight to fight, a retry, the run's
 * end putting everything back, the recoveries and the items carried. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "common.h"
#include "main/game_state.h"
#include "pc_gameplay.h"
#include "system/gauntlet.h"
#include "system/settings.h"
#include "test/env.h"

static int checks;
#include "test/check.h"

static uint32_t clock_;
uint32_t hwVSyncCount(void) { return clock_; }
#ifdef _WIN32
int pcGauntletGameRoster(unsigned char* ids, unsigned char* hasGear, unsigned char* names, unsigned char* party) {
    (void)ids, (void)hasGear, (void)names, (void)party;
    return 0;
}
#endif

#ifndef _WIN32
GameState gameState;
u8 overlayStateSaveBlock[0x8000];
#if !VERSION_JP
s32 savedPartyIsOnGear[3];
#endif
u8 battleConfigs[0x220];
u8 requestedBattleConfig, battleTransitionEffect, battleInitVar0, battleInitVar1;
u16 D_8006F94E;
static int armed;
s32 pcRetryArmed(void) { return armed; }
void pcGauntletCarry(GameState* g, const GameState* now, s32 recovery, u32 knockedOut);

static PcGauntletBoss bosses[4];

static void boss(int i, int disc, int field, int config, int gear, int fill) {
    PcGauntletBoss* b = &bosses[i];
    memset(b, 0, sizeof *b);
    b->disc = disc, b->field = field, b->config = config, b->gear = gear, b->story = i * 10;
    b->music = i == 0 ? 29 : -1;
    memset(b->table, fill, sizeof b->table);
    snprintf(b->name, sizeof b->name, "Boss %d", i);
}

/* Characters 0, 2 and 4 with the party; 0 and 2 with a Gear (4 without). */
static void world(void) {
    memset(&gameState, 0, sizeof gameState);
    gameState.m1D30_partyMemberBitField = 1 << 0 | 1 << 2 | 1 << 4;
    gameState.m1D32_partyFrameMask = 0x7FF;
    gameState.m1D34_currentParty[0] = 0, gameState.m1D34_currentParty[1] = 2, gameState.m1D34_currentParty[2] = 4;
    for (int c = 0; c < 11; c++) {
        gameState.m26C_party[c].mA0_gearNum = c == 4 ? 0xFF : c;
        gameState.m26C_party[c].m4E_MaxHP = 1000, gameState.m26C_party[c].m4C_HP = 600;
        gameState.m26C_party[c].m52_MaxMP = 100, gameState.m26C_party[c].m50_MP = 40;
        gameState.m26C_party[c].m3C_experience = 5000;
    }
    for (int g = 0; g < 20; g++) {
        gameState.m978_gears[g].m64_maxHp = 10000, gameState.m978_gears[g].m60_hp = 7000;
        gameState.m978_gears[g].m3A_maxFuel = 2000, gameState.m978_gears[g].m38_fuel = 900;
    }
    gameState.m2026_inventory0Ids[0] = 1, gameState.m1F90_inventory0Counts[0] = 5;
    gameState.m2026_inventory0Ids[1] = 7, gameState.m1F90_inventory0Counts[1] = 2;
    gameState.m1924_gold = 300;
    gameState.m22B1_isOnGear[0] = 0;
    memset(overlayStateSaveBlock, 0x5A, sizeof overlayStateSaveBlock);
    memset(battleConfigs, 0x77, sizeof battleConfigs);
    requestedBattleConfig = 3, battleTransitionEffect = 2, battleInitVar0 = 0, battleInitVar1 = 0;
    D_8006F94E = 22;
}

static int plan(void) {
    PcGauntletRoster r;
    unsigned char p[3];
    int picks[8];
    pcGauntletRoster(&r);
    CHECK(r.count == 3 && r.ids[0] == 0 && r.ids[1] == 2 && r.ids[2] == 4);
    CHECK(r.hasGear[0] && r.hasGear[1] && !r.hasGear[2]);
    CHECK(r.party[0] == 0 && r.party[1] == 2 && r.party[2] == 4);
    /* Presets: story is every boss in order; character and gear filter. */
    CHECK(pcGauntletSelect(PC_GAUNTLET_STORY, bosses, 4, picks, 8) == 4 && picks[3] == 3);
    CHECK(pcGauntletSelect(PC_GAUNTLET_GEAR, bosses, 4, picks, 8) == 2 && picks[0] == 1 && picks[1] == 3);
    CHECK(pcGauntletSelect(PC_GAUNTLET_CHARACTER, bosses, 4, picks, 8) == 2 && picks[0] == 0 && picks[1] == 2);
    int order[3] = {2, 0, 3};
    pcGauntletSetCustom(bosses, order, 3);
    CHECK(strcmp(settingsGet("gauntlet_custom"), "1-024-15,1-002-2,2-654-12") == 0);
    CHECK(pcGauntletSelect(PC_GAUNTLET_CUSTOM, bosses, 4, picks, 8) == 3 && picks[0] == 2 && picks[2] == 3);
    /* Parties: picked, else the current one; a Gear fight only members with a Gear. */
    CHECK(!pcGauntletPartyFor(&bosses[0], p));
    CHECK(pcGauntletFightParty(&bosses[0], &r, p) == 3 && p[2] == 4);
    CHECK(pcGauntletFightParty(&bosses[1], &r, p) == 2 && p[0] == 0 && p[1] == 2 && p[2] == 0xFF);
    unsigned char pick[3] = {4, 0xFF, 2};
    pcGauntletSetPartyFor(&bosses[0], pick);
    unsigned char only4[3] = {4, 0xFF, 0xFF};
    pcGauntletSetPartyFor(&bosses[3], only4);
    pcGauntletSetPartyFor(&bosses[0], pick); /* replaced, not repeated */
    CHECK(strcmp(settingsGet("gauntlet_parties"), "2-654-12=4.x.x;1-002-2=4.x.2") == 0);
    CHECK(pcGauntletPartyFor(&bosses[0], p) && p[0] == 4 && p[1] == 0xFF && p[2] == 2);
    CHECK(pcGauntletFightParty(&bosses[0], &r, p) == 2 && p[0] == 4 && p[1] == 2);
    CHECK(pcGauntletFightParty(&bosses[3], &r, p) == 0); /* a Gear fight, nobody with a Gear: skipped */
    return 0;
}

/* A run of bosses 0 (character), 1 (Gear), 3 (Gear: skipped): two fights. */
static int run(int recovery) {
    PcGauntletRoster r;
    int picks[3] = {0, 1, 3};
    pcGauntletSetRecovery(recovery);
    world();
    pcGauntletRoster(&r);
    CHECK(pcGauntletBegin("custom", bosses, picks, 3, &r) == 2);
    CHECK(pcGauntletBegin("again", bosses, picks, 3, &r) == 0); /* one at a time */
    int current, count, retries, skipped, quitting;
    CHECK(pcGauntletStatus(&current, &count, &retries, &skipped, &quitting) == PC_GAUNTLET_WAITING);
    CHECK(count == 2 && skipped == 1 && pcGauntletState() == 1);
    GameState before = gameState;
    u8 configs[0x220];
    memcpy(configs, battleConfigs, sizeof configs);
    /* The field starts it: fight 0's table and config. */
    clock_ = 1000;
    CHECK(pcGauntletStartBattle());
    CHECK(pcGauntletState() == 2 && requestedBattleConfig == 2 && battleConfigs[0] == 0x10 && battleInitVar1 == 0);
    CHECK(pcGauntletFightMusic(0) == 29 && pcGauntletFightMusic(1) == -1 && pcGauntletFightMusic(2) == -1);
    CHECK(!pcGauntletStartBattle()); /* once */
    /* The field saves itself for the battle; battleMain sets the fight up. */
    memset(overlayStateSaveBlock, 0x33, 64);
    pcGauntletBattleSetup();
    CHECK(gameState.m1D34_currentParty[0] == 4 && gameState.m1D34_currentParty[1] == 2 &&
          gameState.m1D34_currentParty[2] == 0xFF);
    CHECK(gameState.m22B1_isOnGear[0] == 0 && gameState.m22B1_isOnGear[1] == 0);
    /* The battle: damage, an item used, a drop, EXP; character 4 knocked out. */
    gameState.m26C_party[4].m4C_HP = 1, gameState.m26C_party[2].m4C_HP = 900, gameState.m26C_party[2].m50_MP = 0;
    gameState.m1F90_inventory0Counts[0] = 3, gameState.m2026_inventory0Ids[2] = 9, gameState.m1F90_inventory0Counts[2] = 1;
    gameState.m26C_party[2].m3C_experience = 9999, gameState.m1924_gold = 900;
    gameState.m978_gears[2].m60_hp = 100;
    /* Won, a fight to come: the chain. */
    CHECK(pcGauntletOver(1, 0, 0, 1 << 4) == 1 && battleInitVar0 == 6); /* fight 1's config 5 + 1 */
    CHECK(pcGauntletBattleEnd() == 2 && pcGauntletCurrent() == 1);
    /* Nothing won; the items used stay used; the recovery. */
    CHECK(gameState.m26C_party[2].m3C_experience == 5000 && gameState.m1924_gold == 300);
    CHECK(gameState.m1F90_inventory0Counts[0] == 3 && gameState.m1F90_inventory0Counts[1] == 2 &&
          gameState.m1F90_inventory0Counts[2] == 0);
    CHECK(gameState.m1D34_currentParty[0] == 0); /* the party of before, until the next set-up */
    switch (recovery) {
    case PC_GAUNTLET_FULL:
        CHECK(gameState.m26C_party[4].m4C_HP == 1000 && gameState.m26C_party[2].m50_MP == 100);
        CHECK(gameState.m978_gears[2].m60_hp == 10000 && gameState.m978_gears[2].m38_fuel == 2000);
        break;
    case PC_GAUNTLET_NONE:
        CHECK(gameState.m26C_party[4].m4C_HP == 1 && gameState.m26C_party[2].m4C_HP == 900);
        CHECK(gameState.m26C_party[2].m50_MP == 0 && gameState.m978_gears[2].m60_hp == 100);
        break;
    case PC_GAUNTLET_25:
        CHECK(gameState.m26C_party[4].m4C_HP == 251 && gameState.m26C_party[2].m4C_HP == 1000);
        CHECK(gameState.m26C_party[2].m50_MP == 25 && gameState.m978_gears[2].m60_hp == 2600);
        CHECK(gameState.m978_gears[2].m38_fuel == 1400);
        break;
    case PC_GAUNTLET_50:
        CHECK(gameState.m26C_party[4].m4C_HP == 501 && gameState.m26C_party[2].m50_MP == 50);
        CHECK(gameState.m978_gears[2].m60_hp == 5100);
        break;
    case PC_GAUNTLET_REVIVE:
        CHECK(gameState.m26C_party[4].m4C_HP == 100 && gameState.m26C_party[2].m4C_HP == 900);
        CHECK(gameState.m26C_party[2].m50_MP == 0 && gameState.m978_gears[2].m60_hp == 100);
        break;
    }
    CHECK(!memcmp(overlayStateSaveBlock, "\x33\x33", 2)); /* the save block as the field left it */
    /* Fight 1 (a Gear fight): Gears for 0 and 2. */
    requestedBattleConfig = battleInitVar0 - 1, battleInitVar0 = 0;
    pcGauntletBattleSetup();
    CHECK(battleConfigs[0] == 0x11 && gameState.m1D34_currentParty[0] == 0 && gameState.m1D34_currentParty[1] == 2);
    CHECK(gameState.m22B1_isOnGear[0] == 1 && gameState.m22B1_isOnGear[1] == 1 && gameState.m22B1_isOnGear[2] == 0);
    /* A wipe and Retry: the retry's path, the run goes on. */
    armed = 1;
    CHECK(pcGauntletOver(0x81, 0, 1, 0) == 1);
    CHECK(pcGauntletBattleEnd() == -1);
    pcGauntletStatus(NULL, NULL, &retries, NULL, NULL);
    CHECK(retries == 1 && pcGauntletCurrent() == 1);
    armed = 0;
    battleInitVar0 = 0;
    pcGauntletBattleSetup();
    /* A battle event chains another battle of the fight: it goes on. */
    battleInitVar0 = 9;
    CHECK(pcGauntletOver(1, 0, 1, 0) == 1 && pcGauntletBattleEnd() == -1 && pcGauntletCurrent() == 1);
    battleInitVar0 = 0;
    /* Won the last: cleared; everything back, back to the field. */
    gameState.m1924_gold = 12345;
    clock_ = 1600;
    CHECK(pcGauntletOver(1, 0, 0, 0) == 1);
    CHECK(gameState.m1D34_currentParty[0] == 0 && gameState.m1D34_currentParty[2] == 4); /* finishBattle's preload */
    CHECK(pcGauntletBattleEnd() == 1);
    CHECK(!memcmp(&gameState, &before, sizeof before));
    CHECK(!memcmp(battleConfigs, configs, sizeof configs) && requestedBattleConfig == 3);
    CHECK(battleTransitionEffect == 2 && battleInitVar0 == 0 && battleInitVar1 == 0);
    CHECK(overlayStateSaveBlock[0] == 0x33); /* the field's save block from the battle's start */
    CHECK(pcGauntletState() == 0 && pcGauntletLastResult() == 1);
    return 0;
}

/* Game over (or an escape) ends the run: everything back, no record. */
static int ended(int running) {
    PcGauntletRoster r;
    int picks[2] = {0, 2};
    world();
    D_8006F94E = 0x405; /* the world map */
    pcGauntletRoster(&r);
    CHECK(pcGauntletBegin("story", bosses, picks, 2, &r) == 2);
    GameState before = gameState;
    CHECK(pcGauntletStartBattle());
    pcGauntletBattleSetup();
    gameState.m1924_gold = 1;
    armed = 0;
    CHECK(pcGauntletOver(running, 0, 0, 0) == 1 && battleInitVar0 == 0);
    CHECK(pcGauntletBattleEnd() == 3);
    CHECK(!memcmp(&gameState, &before, sizeof before) && pcGauntletLastResult() == 0);
    /* Quit after this fight, with one to come: the run ends after it, not cleared. */
    CHECK(pcGauntletBegin("story", bosses, picks, 2, &r) == 2);
    CHECK(pcGauntletStartBattle());
    pcGauntletBattleSetup();
    pcGauntletCancel();
    CHECK(pcGauntletOver(1, 0, 0, 0) == 1 && battleInitVar0 == 0 && pcGauntletBattleEnd() == 3);
    CHECK(pcGauntletLastResult() == 0);
    /* A story loss (a battle event's own outcome) counts as cleared. */
    CHECK(pcGauntletBegin("story", bosses, picks, 2, &r) == 2);
    CHECK(pcGauntletStartBattle());
    pcGauntletBattleSetup();
    CHECK(pcGauntletOver(0x81, 1, 0, 0) == 1 && battleInitVar0 == 16); /* boss 2's config 15 + 1 */
    CHECK(pcGauntletBattleEnd() == 2);
    pcGauntletBattleSetup();
    battleInitVar0 = 0;
    CHECK(pcGauntletOver(1, 0, 0, 0) == 1 && pcGauntletBattleEnd() == 3 && pcGauntletLastResult() == 1);
    /* Not in a run: the game's own. */
    CHECK(pcGauntletOver(1, 0, 0, 0) == 0 && pcGauntletBattleEnd() == -1);
    /* A waiting run can be cancelled. */
    CHECK(pcGauntletBegin("story", bosses, picks, 2, &r) == 2);
    pcGauntletCancel();
    CHECK(pcGauntletState() == 0 && !pcGauntletStartBattle());
    return 0;
}

static int records(const char* dir) {
    char path[1200];
    snprintf(path, sizeof path, "%s/gauntlets.json", dir);
    /* One per run cleared: the five recoveries' custom runs, then two story ones. */
    PcGauntletRecord rec[8];
    CHECK(pcGauntletRecords(rec, 8) == 7);
    CHECK(pcGauntletRecords(rec, 4) == 4);
    CHECK(strcmp(rec[0].gauntlet, "story") == 0 && rec[0].fights == 2 && strcmp(rec[1].gauntlet, "story") == 0);
    CHECK(strcmp(rec[2].gauntlet, "custom") == 0 && rec[2].vblanks == 600 && rec[2].retries == 1);
    CHECK(strcmp(rec[2].recovery, "revive") == 0 && strcmp(rec[2].difficulty, "normal") == 0 && rec[2].skipped == 1);
    CHECK(strcmp(rec[3].recovery, "50") == 0);
    FILE* f = fopen(path, "rb");
    CHECK(f != NULL);
    char text[8192];
    size_t got = fread(text, 1, sizeof text - 1, f);
    fclose(f);
    text[got] = 0;
    CHECK(text[0] == '[' && strstr(text, "\"bosses\":[\"1-002-2\",\"1-020-5\"]") != NULL);
    CHECK(strstr(text, "\"parties\":[[4,2],[0,2]]") != NULL);
    return 0;
}

/* A recovery of each kind on one member, the inventory rules alone. */
static int carry(void) {
    static GameState g, now;
    memset(&g, 0, sizeof g);
    g.m26C_party[1].m4E_MaxHP = 999, g.m26C_party[1].m4C_HP = 999, g.m26C_party[1].m52_MaxMP = 9;
    g.m2120_inventory3Ids[0] = 4, g.m20BC_inventory3Counts[0] = 2; /* a Gear item */
    g.m2120_inventory3Ids[1] = 6, g.m20BC_inventory3Counts[1] = 1;
    now = g;
    now.m26C_party[1].m4C_HP = 0; /* a story loss leaves 0 */
    now.m2120_inventory3Ids[0] = 6, now.m20BC_inventory3Counts[0] = 1; /* the list moved */
    now.m2120_inventory3Ids[1] = 8, now.m20BC_inventory3Counts[1] = 3;
    GameState out = g;
    pcGauntletCarry(&out, &now, PC_GAUNTLET_NONE, 1 << 1);
    CHECK(out.m26C_party[1].m4C_HP == 1 && out.m20BC_inventory3Counts[0] == 0 && out.m20BC_inventory3Counts[1] == 1);
    CHECK(out.m2120_inventory3Ids[1] == 6);
    out = g;
    pcGauntletCarry(&out, &now, PC_GAUNTLET_REVIVE, 1 << 1);
    CHECK(out.m26C_party[1].m4C_HP == 99);
    return 0;
}
#endif

int main(int argc, char** argv) {
    if (argc != 3) return 2;
    char path[1200], records_[1200];
    snprintf(path, sizeof path, "%s/gauntlet.ini", argv[1]);
    snprintf(records_, sizeof records_, "%s/gauntlets.json", argv[1]);
    testSetEnv("XG_SETTINGS", path);
    testSetEnv("XG_GAUNTLET_RECORDS", records_);
    if (!strcmp(argv[2], "write")) {
        pcGauntletSetPreset(PC_GAUNTLET_GEAR);
        pcGauntletSetRecovery(PC_GAUNTLET_50);
        CHECK(settingsSave());
    } else if (!strcmp(argv[2], "read")) {
        CHECK(pcGauntletPreset() == PC_GAUNTLET_GEAR && pcGauntletRecoverySetting() == PC_GAUNTLET_50);
    } else {
        CHECK(pcGauntletPreset() == PC_GAUNTLET_STORY && pcGauntletRecoverySetting() == PC_GAUNTLET_FULL);
        testSetEnv("XG_GAUNTLET_RECOVERY", "revive");
        CHECK(pcGauntletRecoverySetting() == PC_GAUNTLET_REVIVE);
        testSetEnv("XG_GAUNTLET_RECOVERY", "bogus");
        CHECK(pcGauntletRecoverySetting() == PC_GAUNTLET_FULL);
        testSetEnv("XG_GAUNTLET_PRESET", "custom");
        CHECK(pcGauntletPreset() == PC_GAUNTLET_CUSTOM);
#ifndef _WIN32
        remove(records_);
        boss(0, 1, 2, 2, 0, 0x10);
        boss(1, 1, 20, 5, 1, 0x11);
        boss(2, 1, 24, 15, 0, 0x12);
        boss(3, 2, 654, 12, 1, 0x13);
        world();
        if (plan()) return 1;
        for (int k = 0; k < PC_GAUNTLET_RECOVERIES; k++) {
            if (run(k)) return 1;
        }
        if (ended(0x81) || ended(0x40)) return 1;
        if (records(argv[1]) || carry()) return 1;
#endif
    }
    printf("gauntlets %s: %d checks passed\n", argv[2], checks);
    return 0;
}
