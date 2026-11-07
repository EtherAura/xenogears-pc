/* Reward settings, the random/scripted battle class, plus the actual native
 * EXP sharing/bonus calculation. */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "battle/battle.h"
#include "pc_gameplay.h"
#include "system/gameplay.h"
#include "test/env.h"

static GameState state;
static BattleBss_800CCCE8 entities;
GameState* pResultGameState = &state;
BattleBss_800CCCE8* pResultBattleEntities = &entities;
GameStateCharacter* pLevelUpCharacter = &state.m26C_party[0];
u32 characterExpGain, characterEtherExpGain;
void computeExperienceToAdd(u32 exp, s16 slot, s16 mode);
static int checks, draws;
int rand(void) { ++draws; return 0; }
#include "test/check.h"

static void rate(const char* key, int percent) {
    char text[32]; snprintf(text, sizeof(text), "%d", percent);
    testSetEnv(key, text);
}

int main(int argc, char** argv) {
    if (argc != 3) return 2;
    char path[1200]; snprintf(path, sizeof(path), "%s/rewards.ini", argv[1]);
    testSetEnv("XG_SETTINGS", path);
    if (!strcmp(argv[2], "write")) {
        rate("XG_EXP_MULTIPLIER", 500);
        pcGameplaySetRewardMultiplier(GAMEPLAY_REWARD_EXP, 225);
        pcGameplaySetRewardMultiplier(GAMEPLAY_REWARD_GOLD, 400);
        pcGameplaySetRewardMultiplier(GAMEPLAY_REWARD_SCRIPTED_EXP, 50);
        pcGameplaySetRewardMultiplier(GAMEPLAY_REWARD_SCRIPTED_GOLD, 1000);
        CHECK(pcGameplayRewardMultiplier(GAMEPLAY_REWARD_EXP) == 225);
        CHECK(pcGameplayRewardMultiplier(GAMEPLAY_REWARD_GOLD) == 400);
        CHECK(pcGameplayRewardMultiplier(GAMEPLAY_REWARD_SCRIPTED_EXP) == 50);
        CHECK(pcGameplayRewardMultiplier(GAMEPLAY_REWARD_SCRIPTED_GOLD) == 1000);
    } else if (!strcmp(argv[2], "read")) {
        CHECK(pcGameplayRewardMultiplier(GAMEPLAY_REWARD_EXP) == 225);
        CHECK(pcGameplayRewardMultiplier(GAMEPLAY_REWARD_GOLD) == 400);
        CHECK(pcGameplayRewardMultiplier(GAMEPLAY_REWARD_SCRIPTED_EXP) == 50);
        CHECK(pcGameplayRewardMultiplier(GAMEPLAY_REWARD_SCRIPTED_GOLD) == 1000);
        CHECK(pcGameplayBattleClass() == GAMEPLAY_BATTLE_SCRIPTED); /* before any roll */
        CHECK(pcGameplayExperience(20) == 10 && pcGameplayGold(20) == 200);
        pcGameplayRandomBattle(2); pcGameplayBattleStart(2, 0);
        CHECK(pcGameplayExperience(20) == 45 && pcGameplayGold(20) == 80);
    } else {
        /* A settings file from before the split: its global values become
         * the random-battle values; scripted battles stay original. */
        snprintf(path, sizeof(path), "%s/legacy.ini", argv[1]);
        FILE* legacy = fopen(path, "w");
        CHECK(legacy != NULL);
        fputs("# Battle reward multiplier in percent, 0..1000; 100=original, independent of encounter rate\n"
              "exp_multiplier=300\ngold_multiplier=150\n", legacy);
        fclose(legacy);
        testSetEnv("XG_SETTINGS", path);
        CHECK(pcGameplayRewardMultiplier(GAMEPLAY_REWARD_EXP) == 300);
        CHECK(pcGameplayRewardMultiplier(GAMEPLAY_REWARD_GOLD) == 150);
        CHECK(pcGameplayRewardMultiplier(GAMEPLAY_REWARD_SCRIPTED_EXP) == 100);
        CHECK(pcGameplayRewardMultiplier(GAMEPLAY_REWARD_SCRIPTED_GOLD) == 100);
        CHECK(pcGameplayExperience(20) == 20 && pcGameplayGold(20) == 20);
        pcGameplayRandomBattle(0); pcGameplayBattleStart(0, 0);
        CHECK(pcGameplayBattleClass() == GAMEPLAY_BATTLE_RANDOM);
        CHECK(pcGameplayExperience(20) == 60 && pcGameplayGold(20) == 30);

        /* Classification: only the battle loading the rolled config, not
         * chained, is random; a roll is used once. */
        pcGameplayBattleStart(0, 0); CHECK(pcGameplayBattleClass() == GAMEPLAY_BATTLE_SCRIPTED);
        pcGameplayRandomBattle(3); pcGameplayBattleStart(4, 0); CHECK(pcGameplayBattleClass() == GAMEPLAY_BATTLE_SCRIPTED);
        pcGameplayBattleStart(3, 0); CHECK(pcGameplayBattleClass() == GAMEPLAY_BATTLE_SCRIPTED);
        pcGameplayRandomBattle(3); pcGameplayBattleStart(3, 1); CHECK(pcGameplayBattleClass() == GAMEPLAY_BATTLE_SCRIPTED);
        pcGameplayRandomBattle(15); pcGameplayBattleStart(15, 0); CHECK(pcGameplayBattleClass() == GAMEPLAY_BATTLE_RANDOM);
        pcGameplayBattleStart(15, 1); CHECK(pcGameplayBattleClass() == GAMEPLAY_BATTLE_SCRIPTED);
        CHECK(pcGameplayRewardKey(GAMEPLAY_REWARD_SCRIPTED_EXP) && !strcmp(pcGameplayRewardKey(GAMEPLAY_REWARD_SCRIPTED_EXP), "scripted_exp_multiplier"));
        CHECK(pcGameplayRewardKey(GAMEPLAY_REWARD_SCRIPTED_GOLD) && !strcmp(pcGameplayRewardKey(GAMEPLAY_REWARD_SCRIPTED_GOLD), "scripted_gold_multiplier"));

        /* Scripted battles take only their own keys, with the same rounding and limits. */
        rate("XG_EXP_MULTIPLIER", 100); rate("XG_GOLD_MULTIPLIER", 100);
        rate("XG_SCRIPTED_EXP_MULTIPLIER", 200); rate("XG_SCRIPTED_GOLD_MULTIPLIER", 50);
        CHECK(pcGameplayExperience(17) == 34 && pcGameplayGold(17) == 9);
        rate("XG_SCRIPTED_EXP_MULTIPLIER", 1000); rate("XG_SCRIPTED_GOLD_MULTIPLIER", 1000);
        CHECK(pcGameplayExperience(UINT32_MAX) == 9999999 && pcGameplayGold(UINT32_MAX) == 999999);
        rate("XG_SCRIPTED_EXP_MULTIPLIER", 0); rate("XG_SCRIPTED_GOLD_MULTIPLIER", 0);
        CHECK(pcGameplayExperience(17) == 0 && pcGameplayGold(17) == 0);
        rate("XG_SCRIPTED_EXP_MULTIPLIER", 100); rate("XG_SCRIPTED_GOLD_MULTIPLIER", 100);
        CHECK(pcGameplayExperience(UINT32_MAX) == UINT32_MAX && pcGameplayGold(UINT32_MAX) == UINT32_MAX);
        rate("XG_EXP_MULTIPLIER", 0); rate("XG_GOLD_MULTIPLIER", 0);
        CHECK(pcGameplayExperience(17) == 17 && pcGameplayGold(17) == 17);

        /* The original random-battle checks. */
        pcGameplaySetBattleClass(GAMEPLAY_BATTLE_RANDOM);
        rate("XG_EXP_MULTIPLIER", 100); rate("XG_GOLD_MULTIPLIER", 100);
        CHECK(pcGameplayRewardMultiplier(GAMEPLAY_REWARD_EXP) == 100);
        CHECK(pcGameplayRewardMultiplier(GAMEPLAY_REWARD_GOLD) == 100);
        CHECK(pcGameplayRewardKey(-1) == NULL && pcGameplayRewardKey(GAMEPLAY_REWARD_COUNT) == NULL);
        CHECK(pcGameplayRewardMultiplier(-1) == 100);
        pcGameplaySetRewardMultiplier(-1, 200);
        CHECK(pcGameplayExperience(UINT32_MAX) == UINT32_MAX && pcGameplayGold(UINT32_MAX) == UINT32_MAX);
        rate("XG_EXP_MULTIPLIER", 200); rate("XG_GOLD_MULTIPLIER", 400);
        CHECK(pcGameplayExperience(17) == 34 && pcGameplayGold(17) == 68);
        CHECK(pcGameplayExperience(0) == 0 && pcGameplayGold(0) == 0);
        rate("XG_EXP_MULTIPLIER", 50); rate("XG_GOLD_MULTIPLIER", 25);
        CHECK(pcGameplayExperience(17) == 9 && pcGameplayGold(17) == 4);
        CHECK(pcGameplayExperience(1) == 1 && pcGameplayGold(1) == 0);
        rate("XG_EXP_MULTIPLIER", 0); rate("XG_GOLD_MULTIPLIER", 0);
        CHECK(pcGameplayExperience(UINT32_MAX) == 0 && pcGameplayGold(UINT32_MAX) == 0);
        rate("XG_EXP_MULTIPLIER", 1000); rate("XG_GOLD_MULTIPLIER", 1000);
        CHECK(pcGameplayExperience(999999) == 9999990 && pcGameplayGold(99999) == 999990);
        CHECK(pcGameplayExperience(UINT32_MAX) == 9999999 && pcGameplayGold(UINT32_MAX) == 999999);
        rate("XG_EXP_MULTIPLIER", -1); rate("XG_GOLD_MULTIPLIER", 2000);
        CHECK(pcGameplayRewardMultiplier(GAMEPLAY_REWARD_EXP) == 0);
        CHECK(pcGameplayRewardMultiplier(GAMEPLAY_REWARD_GOLD) == 1000);
        testSetEnv("XG_EXP_MULTIPLIER", "oops"); testSetEnv("XG_GOLD_MULTIPLIER", "999999999999999999999999");
        CHECK(pcGameplayRewardMultiplier(GAMEPLAY_REWARD_EXP) == 100 && pcGameplayRewardMultiplier(GAMEPLAY_REWARD_GOLD) == 100);
        testSetEnv("XG_EXP_MULTIPLIER", "200x");
        CHECK(pcGameplayRewardMultiplier(GAMEPLAY_REWARD_EXP) == 100);

#ifndef _WIN32
        /* Neutral retains the native minimums, rounding, bonuses and reserves. */
        entities.m0_entities[0].m158_expShares[0] = 1;
        entities.m0_entities[0].m158_expShares[1] = 3;
        state.m26C_party[0].m32_accessoryFlags = 0x3000;
        rate("XG_EXP_MULTIPLIER", 100);
        computeExperienceToAdd(101, 0, 0);
        CHECK(characterExpGain == 37 && characterEtherExpGain == 168);
        CHECK(entities.mFE8_experienceToAdd[0][0] == 37 && entities.mFE8_experienceToAdd[0][1] == 168);
        for (int gear = 0; gear < 2; ++gear) {
            entities.m0_entities[0].m15A_flags = gear ? 0x80 : 0;
            rate("XG_EXP_MULTIPLIER", 200);
            computeExperienceToAdd(101, 0, 0);
            CHECK(characterExpGain == 74 && characterEtherExpGain == 336);
            CHECK(entities.mFE8_experienceToAdd[0][0] == 74 && entities.mFE8_experienceToAdd[0][1] == 336);
            computeExperienceToAdd(101, 0xff, 1);
            CHECK(characterExpGain == 150 && characterEtherExpGain == 150);
            CHECK(entities.mFE8_experienceToAdd[0][0] == 74); /* reserves do not overwrite the visible slot */
        }
        rate("XG_EXP_MULTIPLIER", 150);
        computeExperienceToAdd(101, 0, 0);
        CHECK(characterExpGain == 56 && characterEtherExpGain == 252);
        state.m22B6_gameFlags = 0x8000;
        rate("XG_EXP_MULTIPLIER", 200);
        computeExperienceToAdd(101, 0, 0);
        CHECK(characterExpGain == 302 && characterEtherExpGain == 452);
        rate("XG_EXP_MULTIPLIER", 0);
        computeExperienceToAdd(101, 0, 0);
        CHECK(characterExpGain == 0 && characterEtherExpGain == 0);
        CHECK(entities.mFE8_experienceToAdd[0][0] == 0 && entities.mFE8_experienceToAdd[0][1] == 0);
        computeExperienceToAdd(101, 0xff, 1);
        CHECK(characterExpGain == 0 && characterEtherExpGain == 0);
        state.m22B6_gameFlags = 0; state.m26C_party[0].m32_accessoryFlags = 0;
        rate("XG_EXP_MULTIPLIER", 100);
        computeExperienceToAdd(0, 0, 0);
        CHECK(characterExpGain == 1 && characterEtherExpGain == 1);
        rate("XG_EXP_MULTIPLIER", 0);
        computeExperienceToAdd(0, 0, 0);
        CHECK(characterExpGain == 0 && characterEtherExpGain == 0); /* override native minimum at 0% */
        CHECK(state.m26C_party[0].m3C_experience == 0 && state.m26C_party[0].m40_experience2 == 0);
#endif
        CHECK(draws == 0);
    }
    printf("rewards %s: %d checks passed\n", argv[2], checks);
    return 0;
}
