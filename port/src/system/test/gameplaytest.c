/* Synthetic state calls the real encounter functions; no discs or saves. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "common.h"
#include "main/game_state.h"
#include "field/field.h"
#include "pc_gameplay.h"
#include "system/gameplay.h"
#include "system/settings.h"
#include "test/env.h"

GameState gameState;
FieldBss_800B2184 op9DVar1;
s32 playMusicAuthorized, fieldExecuteVar3, fieldChangePrevented;
s32 loadCompleted, fieldMusicLoadPending, mechaOverlayLoadingMode;
s32 bBattleSequenceInitialized, fieldDebugDisable;
s16 fieldRandomBattleVar;
u8 fieldTransitionCompleted, requestedBattleConfig, battleInitVar1;
u8 encounterProbabilityWeight[16];
s16 worldmapEncounterCountdowns[16];
s32 worldmapEncounterTimer, worldmapEncounterPeriod, worldmapEncounterTimerCount, worldmapEncounterTriggered;
static int draws, battles, refills, checks;

int rand(void) { draws++; return 32767; }
/* The Enhanced engine's encounter code asks whether this is a 60 Hz field's frame between: never here. */
__attribute__((weak)) s32 pcFieldOffTick(void) { return 0; }
void loadGameModeOverlay(s32 mode) { if (mode == 2) battles++; }
void func_80281204(s32 encounter) { (void)encounter; }
void setInitialEncounterData(void) {
    refills++;
    op9DVar1.m110_encounterDataCountdown = 1000;
    op9DVar1.m11C_encounterTriggerTimes[0] = 100;
}
s32 checkForRandomEncounter(void);
void updateWorldmapEncounterTimers(void);
void initWorldmapEncounterTimers(void);

#include "test/check.h"

static void rate(int value) {
    char text[32];
    snprintf(text, sizeof(text), "%d", value);
    testSetEnv("XG_ENCOUNTER_RATE", text);
}

static void field(void) {
    memset(&op9DVar1, 0, sizeof(op9DVar1));
    playMusicAuthorized = fieldExecuteVar3 = fieldChangePrevented = fieldTransitionCompleted = 1;
    fieldMusicLoadPending = fieldRandomBattleVar = loadCompleted = mechaOverlayLoadingMode = 0;
    bBattleSequenceInitialized = battles = refills = draws = 0;
    fieldDebugDisable = 1;
    op9DVar1.m114_encounterTimer = 1000;
    op9DVar1.m110_encounterDataCountdown = 1000;
    op9DVar1.m118_encounterCount = 1;
    op9DVar1.m11C_encounterTriggerTimes[0] = 100;
    memset(encounterProbabilityWeight, 0, sizeof(encounterProbabilityWeight));
    encounterProbabilityWeight[0] = 255;
}

static void world(void) {
    worldmapEncounterTimer = 1000;
    worldmapEncounterPeriod = 384;
    worldmapEncounterTimerCount = 1;
    worldmapEncounterTriggered = 0;
    memset(worldmapEncounterCountdowns, 0, sizeof(worldmapEncounterCountdowns));
    worldmapEncounterCountdowns[0] = 100;
    draws = 0;
}

int main(int argc, char** argv) {
    if (argc != 3) return 2;
    char path[1200];
    snprintf(path, sizeof(path), "%s/settings.ini", argv[1]);
    testSetEnv("XG_SETTINGS", path);
    if (!strcmp(argv[2], "write")) {
        rate(400);
        pcGameplaySetEncounterRate(75);
        CHECK(pcGameplayEncounterRate() == 75);
        CHECK(settingsOverride("encounter_rate") == NULL);
        CHECK(settingsSave());
    } else if (!strcmp(argv[2], "read")) {
        CHECK(pcGameplayEncounterRate() == 75);
        rate(200);
        CHECK(pcGameplayEncounterRate() == 200);
    } else {
        CHECK(pcGameplayEncounterRate() == 100);
        const int rates[] = {0, 1, 25, 33, 50, 75, 99, 100, 125, 150, 200, 399, 400};
        for (unsigned r = 0; r < sizeof(rates)/sizeof(*rates); r++) {
            rate(rates[r]);
            int fraction = 0, sum = 0;
            for (int i = 0; i < 10000; i++) {
                int ticks = pcGameplayEncounterTicks(&fraction);
                CHECK(ticks >= 0 && ticks <= 4 && fraction >= 0 && fraction < 100);
                sum += ticks;
            }
            CHECK(sum == rates[r] * 100 && fraction == 0);
        }
        rate(-5); CHECK(pcGameplayEncounterRate() == 0);
        rate(900); CHECK(pcGameplayEncounterRate() == 400);
        testSetEnv("XG_ENCOUNTER_RATE", "1junk"); CHECK(pcGameplayEncounterRate() == 100);
        testSetEnv("XG_ENCOUNTER_RATE", "999999999999999999999999"); CHECK(pcGameplayEncounterRate() == 100);
        CHECK(draws == 0);

#ifndef _WIN32
        /* PE keeps unrelated callback tables from these complete game TUs;
         * Linux tests the native functions, Windows tests settings/math. */
        for (int r = 0; r <= 400; r += 50) {
            /* Neutral clears fractional clock state between test fixtures. */
            rate(100); field(); checkForRandomEncounter();
            world(); updateWorldmapEncounterTimers();
            field(); world(); rate(r);
            if (r == 0) {
                for (int i = 0; i < 500; i++) {
                    checkForRandomEncounter(); updateWorldmapEncounterTimers();
                }
                CHECK(op9DVar1.m110_encounterDataCountdown == 1000);
                CHECK(op9DVar1.m11C_encounterTriggerTimes[0] == 100);
                CHECK(worldmapEncounterTimer == 1000 && worldmapEncounterCountdowns[0] == 100 && worldmapEncounterTriggered == 0);
                CHECK(draws == 0 && battles == 0 && refills == 0);
                pcGameplayBattleStart(0, 0); /* no roll: a scripted battle for rewards */
                CHECK(pcGameplayBattleClass() == GAMEPLAY_BATTLE_SCRIPTED);
                continue;
            }
            int calls = (10000 + r - 1) / r;
            for (int i = 1; i <= calls; i++) {
                checkForRandomEncounter(); updateWorldmapEncounterTimers();
                CHECK((battles != 0) == (i == calls));
                CHECK((worldmapEncounterTriggered != 0) == (i == calls));
            }
            CHECK(battles == 1 && draws == 1 && bBattleSequenceInitialized == 1);
            CHECK(op9DVar1.m110_encounterDataCountdown == 900 && worldmapEncounterTimer == 900);
            CHECK(requestedBattleConfig == 0 && playMusicAuthorized == 0);
            /* The field's roll makes the battle loading its config random. */
            pcGameplayBattleStart(requestedBattleConfig, 0);
            CHECK(pcGameplayBattleClass() == GAMEPLAY_BATTLE_RANDOM);
        }

        /* Existing disabled areas and startup/load guards still prevent fights. */
        rate(400); field(); fieldRandomBattleVar = -1;
        for (int i = 0; i < 100; i++) checkForRandomEncounter();
        CHECK(battles == 0 && op9DVar1.m110_encounterDataCountdown == 1000);
        field(); playMusicAuthorized = 0; checkForRandomEncounter();
        CHECK(draws == 0 && battles == 0);
        rate(100); world(); worldmapEncounterTimer = 1;
        updateWorldmapEncounterTimers();
        CHECK(draws == 1 && worldmapEncounterTimer == 384 && worldmapEncounterCountdowns[0] == 32767 % 384);
        gameState.m1834_vehicleMode = 0; initWorldmapEncounterTimers(); CHECK(worldmapEncounterPeriod == 384);
        gameState.m1834_vehicleMode = 0x4000; initWorldmapEncounterTimers(); CHECK(worldmapEncounterPeriod == 768);
#endif
    }
    printf("gameplay %s: %d checks passed\n", argv[2], checks);
    return 0;
}
