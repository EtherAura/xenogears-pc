/* Exercise the real loader: fresh enemies only, never party or source data. */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include "battle/battle.h"
#include "battle_loader/battle_loader.h"
#include "pc_gameplay.h"
#include "system/gameplay.h"
#include "test/env.h"

sBattleConfig currentBattleConfig;
BattleBss_800C3EB0 battleBssBlock;
BattleBss_800CCCE8 battleEntities;
sUnkMonsterStatus unknownMonsterStatus0[8];
sBattleScriptContext battleScriptContext[32];
u8 battleMonsterMapping[11];
GameState gameState;
u8 accessBattleEntityByteStat(u8, u8, u8, u8);
u16 accessBattleEntityWordStat(u8, u8, u16, u8);
void monsterBattleOpcode_loadGearHpOrGold(sBattleScriptExecutionContext*, u8);
u8 monsterBattleOpcode_setTargetGearHp(sBattleScriptExecutionContext*, u8, u8);
void executeBattleCode_copyMonsterToSlot(u8, u8, u8);
sBattleScriptEntity monstersScriptsEntities[8];
u8* battleConfigFile2;
u8* currentBattleSpecificStrings;
u16 bBattleTickMode0;
void loadEnemies(void);
void bzero(void* p, size_t n) { memset(p, 0, n); }
static int checks;
#include "test/check.h"

static void normal(void) {
    testSetEnv("XG_DIFFICULTY", "normal");
    for (int i = 0; i < GAMEPLAY_ENEMY_STAT_COUNT; i++) {
        char key[96] = "XG_";
        const char* k = pcGameplayEnemyKey(i);
        for (int j = 0; k[j]; j++) key[j+3] = k[j] >= 'a' && k[j] <= 'z' ? k[j]-'a'+'A' : k[j];
        testSetEnv(key, "100");
    }
}

#ifndef _WIN32
BattleBss_800D2DCC isBattleSlotFilled;
sBattleVar48 battleCurrentDamages[32];
static sBattleVar2 nativeBattleVars;
sBattleVar2* battleVar2 = &nativeBattleVars;
sBattleVisualEntity battleVisualEntities[11];
sBattleSlotLayout battleSlotLayout[32];
sApStruct apConfigArray[11];
u8 healWhileOnGear;
u16 bitmaskCharacter[16] = {1, 2, 4, 8, 16, 32, 64, 128, 256, 512, 1024, 2048, 4096, 8192, 16384, 32768};
void applyChangeToHpOrMp(u8);

static int nativeHp(void) {
    const unsigned bases[] = {1, 16383, 16384, 32767, 32768, 50000, 65535};
    const unsigned damageTypes[] = {0, 5, 7, 8};
    for (int hard = 0; hard < 2; hard++)
    for (int custom = 100; custom <= 400; custom += 100)
    for (unsigned b = 0; b < sizeof(bases) / sizeof(*bases); b++)
    for (unsigned t = 0; t < sizeof(damageTypes) / sizeof(*damageTypes); t++) {
        /* Unmodified out-of-range retail values keep their original behavior. */
        if (!hard && custom == 100) continue;
        normal();
        testSetEnv("XG_DIFFICULTY", hard ? "hard" : "normal");
        char text[16]; snprintf(text, sizeof(text), "%d", custom);
        testSetEnv("XG_ENEMY_HP", text);
        memset(&battleEntities, 0, sizeof(battleEntities));
        memset(&battleBssBlock, 0, sizeof(battleBssBlock));
        memset(&isBattleSlotFilled, 0, sizeof(isBattleSlotFilled));
        memset(&nativeBattleVars, 0, sizeof(nativeBattleVars));
        memset(battleCurrentDamages, 0, sizeof(battleCurrentDamages));
        sBattleEntity* e = &battleEntities.m0_entities[3];
        e->m0_base.m4C_HP = e->m0_base.m4E_MaxHP = bases[b];
        e->mA4_gear.m60_hp = e->mA4_gear.m64_maxHp = 99999;
        pcGameplayEnemyStats(e);
        unsigned hp = e->m0_base.m4C_HP;
        CHECK(hp > 1 && hp <= 32767 && e->m0_base.m4E_MaxHP == hp);
        isBattleSlotFilled.m0_isBattleSlotFilled[3] = 1;
        battleSlotLayout[8].m0_count = battleSlotLayout[8].m1_mask = 1;
        battleCurrentDamages[0].m18_damageType[3] = damageTypes[t];
        battleBssBlock.m138_currentDamages[0].m0_damageValue[3] = 1;
        applyChangeToHpOrMp(0);
        if (e->m0_base.m4C_HP != hp - 1) {
            fprintf(stderr, "native HP: hard=%d custom=%d base=%u loaded=%u after 1 damage=%u\n",
                    hard, custom, bases[b], hp, e->m0_base.m4C_HP);
        }
        CHECK(e->m0_base.m4C_HP == hp - 1);
        CHECK(!(e->m0_base.m7C_statusFlags[0] & 0x8000));
        CHECK(battleBssBlock.mA38_performAttackSub3_var0 == 0);
        CHECK(battleSlotLayout[8].m0_count == 1 && nativeBattleVars.m2EB_hpChanged[3] == 1);

        /* The standalone fixtures do not alias the aggregates as the game
         * linker does. Healing's store is in battleBssBlock; its cap store
         * and subsequent reads are in battleEntities. Check each address. */
        battleCurrentDamages[0].m18_damageType[3] = 2;
        applyChangeToHpOrMp(0);
        CHECK(battleBssBlock.m8E38_entities[3].m0_base.m4C_HP == hp);
        e->m0_base.m4C_HP = hp;
        applyChangeToHpOrMp(0);
        CHECK(e->m0_base.m4C_HP == hp);
        CHECK(!(e->m0_base.m7C_statusFlags[0] & 0x8000));

        battleCurrentDamages[0].m18_damageType[3] = damageTypes[t];
        battleBssBlock.m138_currentDamages[0].m0_damageValue[3] = hp;
        applyChangeToHpOrMp(0);
        CHECK(e->m0_base.m4C_HP == 0 && (e->m0_base.m7C_statusFlags[0] & 0x8000));
        CHECK(battleBssBlock.mA38_performAttackSub3_var0 == characterIdToTargetBitmask(3));
        CHECK(battleSlotLayout[8].m0_count == 0);

        /* Gear HP follows its independent signed 32-bit path. */
        battleBssBlock.mA38_performAttackSub3_var0 = 0;
        e->m0_base.m7C_statusFlags[0] = 0;
        battleBssBlock.m4_visualEntities[3].m4_isGear = 0x80;
        battleBssBlock.m138_currentDamages[0].m0_damageValue[3] = 1;
        int gearHp = e->mA4_gear.m60_hp;
        applyChangeToHpOrMp(0);
        CHECK(e->mA4_gear.m60_hp == gearHp - 1 && gearHp > 99999);
        CHECK(!(e->m0_base.m7C_statusFlags[0] & 0x8000));
    }
    normal();
    return 0;
}
#endif

/* Native VM access, phase changes, capped HP, story controls and copied
 * summons. Values below are the independently read Ramsus/Deus scripts. */
static int scriptStats(void) {
    sBattleEntity e = {0};
    e.m0_base.m4C_HP = e.m0_base.m4E_MaxHP = 800;
    e.m0_base.m58_attack = 48;
    e.m0_base.m5B_ether = 180;
    e.m0_base.m5A_agility = 20;
    e.mA4_gear.m60_hp = e.mA4_gear.m64_maxHp = 80000;
    e.mA4_gear.m3F_attackMultiplier = 10;
    for (int hard = 0; hard < 2; hard++) {
        normal(); testSetEnv("XG_DIFFICULTY", hard ? "hard" : "normal");
        for (int custom = 25; custom <= 400; custom += 25) {
            char text[16]; snprintf(text, sizeof text, "%d", custom);
            testSetEnv("XG_ENEMY_HP", text);
            sBattleEntity* enemy = &battleEntities.m0_entities[3];
            *enemy = e;
            pcGameplayEnemyLoaded(enemy, 3, 45, 1);
            int percent = (hard ? 200 : 100) * custom;
            CHECK(enemy->m0_base.m4C_HP == (800 * percent + 5000) / 10000);
            CHECK(pcGameplayEnemyScriptValue(3, 1, 1, enemy->m0_base.m4C_HP, 1) == 800);
            CHECK(pcGameplayEnemyScriptValue(3, 0, 9, enemy->m0_base.m5B_ether, 1) == 180);
            CHECK(pcGameplayEnemyScriptValue(3, 0, 6, enemy->m0_base.m5A_agility, 1) == 20);
#ifndef _WIN32
            /* Ramsus's second phase: 400 max / 350 current, one scaling. */
            accessBattleEntityWordStat(3, 0, 400, 0);
            accessBattleEntityWordStat(3, 1, 350, 0);
            CHECK(enemy->m0_base.m4E_MaxHP == (400 * percent + 5000) / 10000);
            CHECK(enemy->m0_base.m4C_HP == (350 * percent + 5000) / 10000);
            CHECK(accessBattleEntityWordStat(3, 1, 0, 1) == 350);
            accessBattleEntityByteStat(3, 9, 200, 0);
            CHECK(enemy->m0_base.m5B_ether == (hard ? 255 : 200));
            CHECK(accessBattleEntityByteStat(3, 9, 0, 1) == 200);
            /* Status masks, engine output and party fields are native. */
            accessBattleEntityWordStat(3, 3, 0xffff, 0);
            CHECK(accessBattleEntityWordStat(3, 3, 0, 1) == 0xffff);
            accessBattleEntityByteStat(3, 2, 95, 0);
            CHECK(accessBattleEntityByteStat(3, 2, 0, 1) == 95);
            accessBattleEntityWordStat(0, 0, 400, 0);
            CHECK(accessBattleEntityWordStat(0, 0, 0, 1) == 400);
            /* Final Deus's HP written from its dword variable. */
            unsigned char code[4] = {0x2f, 0, 0, 1};
            sBattleScriptExecutionContext ctx = {0}; ctx.m0_scriptPtr = code;
            monstersScriptsEntities[0].m20_s16Vars[1] = 1 << 3;
            monstersScriptsEntities[0].m10_s32Vars[0] = 80000;
            CHECK(monsterBattleOpcode_setTargetGearHp(&ctx, 0, 0) == 0);
            code[2] = 1;
            CHECK(monsterBattleOpcode_setTargetGearHp(&ctx, 0, 0) == 0);
            CHECK(enemy->mA4_gear.m60_hp == ((uint64_t)80000 * percent + 5000) / 10000);
            code[0] = 0x2e;
            monsterBattleOpcode_loadGearHpOrGold(&ctx, 0);
            CHECK(monstersScriptsEntities[0].m10_s32Vars[0] == 80000);
            /* Script copying a monster copies its scale metadata too. */
            battleScriptContext[0].m6_mask = 1 << 3;
            nativeBattleVars.m2DA_indexInBattleVar48 = 0;
            executeBattleCode_copyMonsterToSlot(4, 0, 0);
            CHECK(memcmp(enemy, &battleEntities.m0_entities[4], sizeof e) == 0);
            CHECK(accessBattleEntityWordStat(4, 1, 0, 1) == 350);
            CHECK(pcGameplayEnemyScriptValue(4, 2, 1, battleEntities.m0_entities[4].mA4_gear.m60_hp, 1) == 80000);
#endif
            for (int value = 0; value <= 1; value++) {
                CHECK(pcGameplayEnemyScriptValue(3, 1, 1, value, 0) == value);
                CHECK(pcGameplayEnemyScriptValue(3, 2, 1, value, 0) == value);
            }
        }
    }
    normal(); testSetEnv("XG_DIFFICULTY", "hard");
    const int story[][2] = {{11, 0}, {13, 1}, {45, 0}, {45, 2}};
    for (unsigned i = 0; i < sizeof story / sizeof *story; i++) {
        sBattleEntity actor = e;
        pcGameplayEnemyStatsForType(&actor, story[i][0], story[i][1]);
        CHECK(memcmp(&actor, &e, sizeof e) == 0);
    }
    sBattleEntity corporal = e;
    pcGameplayEnemyStatsForType(&corporal, 4, 3);
    CHECK(corporal.m0_base.m4E_MaxHP == 1200 && corporal.m0_base.m58_attack == 60);
    for (int set = 53; set <= 55; set += 2) {
        sBattleEntity boss = e;
        pcGameplayEnemyStatsForType(&boss, set, set == 55);
        CHECK(boss.m0_base.m4E_MaxHP == 1200);
        CHECK(boss.m0_base.m58_attack == 60);
        testSetEnv("XG_ENEMY_HP", "50");
        boss = e; pcGameplayEnemyStatsForType(&boss, set, set == 55);
        CHECK(boss.m0_base.m4E_MaxHP == 600);
        testSetEnv("XG_ENEMY_HP", "100");
    }
    testSetEnv("XG_ENEMY_HP", "200");
    sBattleEntity actor = e;
    pcGameplayEnemyStatsForType(&actor, 13, 1);
    CHECK(actor.mA4_gear.m60_hp == 160000);
    /* With a saturated custom HP maximum, half actual HP is still half
     * original HP to the script; it does not enter its phase prematurely. */
    e.m0_base.m4E_MaxHP = e.m0_base.m4C_HP = 20000;
    pcGameplayEnemyLoaded(&e, 3, 45, 1);
    CHECK(e.m0_base.m4E_MaxHP == 32767);
    CHECK(pcGameplayEnemyScriptValue(3, 1, 1, 16384, 1) == 10000);
    CHECK(pcGameplayEnemyScriptValue(3, 2, 1, -1, 0) == -1);
    normal();
    return 0;
}

int main(int argc, char** argv) {
    if (argc < 3) return 2;
    char path[1200];
    snprintf(path, sizeof(path), "%s/combat.ini", argv[1]);
    testSetEnv("XG_SETTINGS", path);
    if (!strcmp(argv[2], "write")) {
        pcGameplaySetHardMode(1);
        pcGameplaySetEnemyMultiplier(GAMEPLAY_ENEMY_HP, 50);
        pcGameplaySetEnemyMultiplier(GAMEPLAY_ENEMY_ATTACK, 175);
        CHECK(pcGameplayHardMode());
        CHECK(pcGameplayEnemyMultiplier(GAMEPLAY_ENEMY_HP) == 50);
    } else if (!strcmp(argv[2], "read")) {
        CHECK(pcGameplayHardMode());
        CHECK(pcGameplayEnemyMultiplier(GAMEPLAY_ENEMY_HP) == 50);
        CHECK(pcGameplayEnemyMultiplier(GAMEPLAY_ENEMY_ATTACK) == 175);
    } else {
        CHECK(sizeof(sBattleEntity) == 0x170);
        CHECK(scriptStats() == 0);
#ifndef _WIN32
        CHECK(nativeHp() == 0);
#endif
        normal();
        sBattleEntity e, original;
        memset(&e, 0xa5, sizeof(e)); original = e;
        pcGameplayEnemyStats(&e); CHECK(memcmp(&e, &original, sizeof(e)) == 0);
        memset(&original, 0, sizeof(original));
        original.m0_base.m4C_HP = original.m0_base.m4E_MaxHP = 300;
        original.m0_base.m58_attack = original.m0_base.m5B_ether = 100;
        original.m0_base.m59_defence = original.m0_base.m5C_etherDefence = 20;
        original.m0_base.m5A_agility = 10;
        original.mA4_gear.m60_hp = original.mA4_gear.m64_maxHp = 5000;
        original.mA4_gear.m3F_attackMultiplier = 10;
        original.mA4_gear.m70_defence = original.mA4_gear.m72_etherDefence = 100;
        testSetEnv("XG_DIFFICULTY", "hard"); e = original;
        pcGameplayEnemyStats(&e);
        CHECK(e.m0_base.m4C_HP == 600 && e.m0_base.m4E_MaxHP == 600);
        CHECK(e.m0_base.m58_attack == 150 && e.m0_base.m5B_ether == 150);
        CHECK(e.mA4_gear.m60_hp == 10000 && e.mA4_gear.m64_maxHp == 10000);
        CHECK(e.mA4_gear.m3F_attackMultiplier == 15);
        CHECK(e.m0_base.m5A_agility == 10 && e.m0_base.m59_defence == 20);
        testSetEnv("XG_ENEMY_HP", "50"); testSetEnv("XG_ENEMY_ATTACK", "200");
        testSetEnv("XG_ENEMY_ETHER", "200"); testSetEnv("XG_ENEMY_DEFENCE", "50");
        testSetEnv("XG_ENEMY_ETHER_DEFENCE", "50"); testSetEnv("XG_ENEMY_AGILITY", "200");
        e = original; pcGameplayEnemyStats(&e);
        CHECK(e.m0_base.m4C_HP == 300 && e.mA4_gear.m60_hp == 5000);
        CHECK(e.m0_base.m58_attack == 255 && e.m0_base.m5B_ether == 255);
        CHECK(e.mA4_gear.m3F_attackMultiplier == 30);
        CHECK(e.m0_base.m59_defence == 10 && e.mA4_gear.m70_defence == 50);
        CHECK(e.m0_base.m5C_etherDefence == 10 && e.mA4_gear.m72_etherDefence == 50);
        CHECK(e.m0_base.m5A_agility == 16);
        normal(); testSetEnv("XG_DIFFICULTY", "hard"); testSetEnv("XG_ENEMY_HP", "400");
        e = original; e.m0_base.m4E_MaxHP = 50000; e.mA4_gear.m64_maxHp = INT32_MAX;
        e.m0_base.m4C_HP = 0; e.mA4_gear.m60_hp = -1;
        pcGameplayEnemyStats(&e);
        CHECK(e.m0_base.m4E_MaxHP == 32767 && e.mA4_gear.m64_maxHp == INT32_MAX);
        CHECK(e.m0_base.m4C_HP == 0 && e.mA4_gear.m60_hp == -1);
        /* Other modified stats must not clamp original HP. Hard x 50%
         * cancels before saturation, even for unused/sentinel high values. */
        for (int hard = 0; hard < 2; hard++) {
            normal(); testSetEnv("XG_DIFFICULTY", hard ? "hard" : "normal");
            testSetEnv("XG_ENEMY_HP", hard ? "50" : "100");
            testSetEnv("XG_ENEMY_ATTACK", "200");
            e = original;
            e.m0_base.m4C_HP = 50000; e.m0_base.m4E_MaxHP = 65535;
            pcGameplayEnemyStats(&e);
            CHECK(e.m0_base.m4C_HP == 50000 && e.m0_base.m4E_MaxHP == 65535);
            CHECK(e.mA4_gear.m60_hp == original.mA4_gear.m60_hp);
            CHECK(e.mA4_gear.m64_maxHp == original.mA4_gear.m64_maxHp);
        }
        normal(); testSetEnv("XG_ENEMY_HP", "25"); e = original;
        e.m0_base.m58_attack = 0; e.m0_base.m5A_agility = 20; pcGameplayEnemyStats(&e);
        CHECK(e.m0_base.m4C_HP == 75 && e.m0_base.m58_attack == 0 && e.m0_base.m5A_agility == 20);
        testSetEnv("XG_ENEMY_AGILITY", "150"); e = original; e.m0_base.m5A_agility = 20;
        pcGameplayEnemyStats(&e); CHECK(e.m0_base.m5A_agility == 20);
        testSetEnv("XG_ENEMY_AGILITY", "50"); e = original; e.m0_base.m5A_agility = 20;
        pcGameplayEnemyStats(&e); CHECK(e.m0_base.m5A_agility == 10);
        testSetEnv("XG_ENEMY_AGILITY", "100");
        testSetEnv("XG_ENEMY_ATTACK", "junk"); CHECK(pcGameplayEnemyMultiplier(GAMEPLAY_ENEMY_ATTACK) == 100);
        testSetEnv("XG_ENEMY_ATTACK", "0"); CHECK(pcGameplayEnemyMultiplier(GAMEPLAY_ENEMY_ATTACK) == 25);
        testSetEnv("XG_ENEMY_ATTACK", "999"); CHECK(pcGameplayEnemyMultiplier(GAMEPLAY_ENEMY_ATTACK) == 400);

#ifndef _WIN32
        /* Loader uses fresh copies on repeat loads and leaves PCs untouched. */
        unsigned char file[0x32 + 2 * 0x170 + 16] = {0};
        uint16_t scripts = 0x32 + 2 * 0x170;
        memcpy(file, &scripts, 2); memcpy(file + 2, &scripts, 2);
        memset(file + scripts + 4, 0xff, 4);
        memcpy(file + 0x32, &original, sizeof(original));
        memcpy(file + 0x32 + 0x170, &original, sizeof(original));
        unsigned char source[sizeof(file)]; memcpy(source, file, sizeof(file));
        battleConfigFile2 = file;
        memset(&battleEntities, 0, sizeof(battleEntities));
        memset(&battleBssBlock, 0, sizeof(battleBssBlock));
        memset(battleEntities.m0_entities, 0xa5, 3 * sizeof(sBattleEntity));
        sBattleEntity party[3]; memcpy(party, battleEntities.m0_entities, sizeof(party));
        for (int i = 3; i < 11; i++) battleBssBlock.m4_visualEntities[i].m2_characterId = 0x7f;
        battleBssBlock.m4_visualEntities[3].m2_characterId = 0;
        battleBssBlock.m4_visualEntities[4].m2_characterId = 1;
        battleBssBlock.m4_visualEntities[4].m4_isGear = 0x80;
        normal(); testSetEnv("XG_DIFFICULTY", "hard");
        for (int repeat = 0; repeat < 3; repeat++) {
            loadEnemies();
            CHECK(battleEntities.m0_entities[3].m0_base.m4C_HP == 600);
            CHECK(battleEntities.m0_entities[4].mA4_gear.m60_hp == 10000);
            CHECK(memcmp(party, battleEntities.m0_entities, sizeof(party)) == 0);
            CHECK(memcmp(file, source, sizeof(file)) == 0);
            CHECK(battleEntities.m0_entities[5].m0_base.m4C_HP == 0);
        }
        normal(); loadEnemies();
        CHECK(memcmp(&battleEntities.m0_entities[3], &original, sizeof(original)) == 0);
        CHECK(memcmp(&battleEntities.m0_entities[4], &original, sizeof(original)) == 0);

        sBattleEntity large = original;
        large.m0_base.m4C_HP = large.m0_base.m4E_MaxHP = 16384;
        memcpy(file + 0x32, &large, sizeof(large));
        memcpy(source, file, sizeof(file));
        testSetEnv("XG_DIFFICULTY", "hard");
        for (int repeat = 0; repeat < 3; repeat++) {
            loadEnemies();
            CHECK(battleEntities.m0_entities[3].m0_base.m4C_HP == 32767);
            CHECK(battleEntities.m0_entities[3].m0_base.m4E_MaxHP == 32767);
            CHECK(memcmp(party, battleEntities.m0_entities, sizeof(party)) == 0);
            CHECK(memcmp(file, source, sizeof(file)) == 0);
            memset(&isBattleSlotFilled, 0, sizeof(isBattleSlotFilled));
            isBattleSlotFilled.m0_isBattleSlotFilled[3] = 1;
            battleCurrentDamages[0].m18_damageType[3] = 0;
            battleBssBlock.m138_currentDamages[0].m0_damageValue[3] = 1;
            applyChangeToHpOrMp(0);
            CHECK(battleEntities.m0_entities[3].m0_base.m4C_HP == 32766);
            CHECK(!(battleEntities.m0_entities[3].m0_base.m7C_statusFlags[0] & 0x8000));
        }
        normal(); loadEnemies();
        CHECK(memcmp(&battleEntities.m0_entities[3], &large, sizeof(large)) == 0);

        /* A battle start forgets the last battle's scale metadata: a slot
         * the next battle leaves empty translates nothing, though the last
         * one scaled it under Hard. With the options off, every script value
         * is the original one. */
        testSetEnv("XG_DIFFICULTY", "hard");
        battleBssBlock.m4_visualEntities[5].m2_characterId = 1;
        pcGameplayBattleStart(0, 0); loadEnemies();
        CHECK(battleEntities.m0_entities[5].m0_base.m4E_MaxHP == 600);
        CHECK(pcGameplayEnemyScriptValue(5, 1, 0, 600, 1) == 300);
        normal();
        battleBssBlock.m4_visualEntities[5].m2_characterId = 0x7f;
        pcGameplayBattleStart(0, 0); loadEnemies();
        for (int field = 0; field <= 1; field++) {
            CHECK(pcGameplayEnemyScriptValue(5, 1, field, 300, 0) == 300);
            CHECK(pcGameplayEnemyScriptValue(5, 1, field, 300, 1) == 300);
        }
        CHECK(pcGameplayEnemyScriptValue(5, 0, 4, 100, 0) == 100);
#endif
    }
    printf("combat %s: %d checks passed\n", argv[2], checks);
    return 0;
}
