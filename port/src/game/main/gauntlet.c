/*
 * Gauntlets, the game's side (port/src/system/gauntlet.h has the run's plan
 * and the menu; xg-htn.35.17).
 *
 *   - The first fight starts where a random battle could (the field: the
 *     player could open the menu, port/src/game/field/gauntlet_field.c; the
 *     world map: an encounter could roll, on foot or in Gears): the fight's
 *     field table goes into battleConfigs, requestedBattleConfig is its
 *     config, as the field's random battles and the world map's encounters
 *     set them.
 *   - Each battle (battleMain, before the retry checkpoint): the first one
 *     keeps everything the game needs to go on afterwards, as the retry
 *     checkpoint does (port/src/game/main/retry.c: gameState, the field or
 *     world-map save block, savedPartyIsOnGear, the battle request); then the fight's
 *     table, party and Gears (gameState.m1D34_currentParty, m22B1_isOnGear,
 *     which loadPartyMembers reads).
 *   - Its outcome (battleMain, before finishBattle: port/src/game/battle/
 *     gauntlet.c): cleared (won, or the story's own outcome of a battle
 *     event) with a fight to come: battleInitVar0 chains it as battle event
 *     op 0x24 chains a battle; a battle event's own chain (battleInitVar0
 *     already set) goes on as part of the fight; a wipe the game would end
 *     in its game over: the Retry / Game over choice (retry.c keeps a
 *     checkpoint of each gauntlet battle); Game over, an escape or "quit
 *     after this fight": the run ends, and the party of before goes back
 *     into gameState so finishBattle preloads the field's party files for it.
 *   - battle_boot: the next fight starts from the state before the run with
 *     what is carried (the HP, EP, Gear HP and fuel as the battle left them,
 *     with the recovery; the items, never more of one than before the run);
 *     the run's end puts everything back and returns to the field or the
 *     world map (mode 1 or 3 by D_8006F94E, as after a battle). Nothing is
 *     won: EXP, gold, drops and learning go with the state they were added to.
 */
#include "common.h"
#include "main/game_state.h"
#include "pc_gameplay.h"

extern u8 overlayStateSaveBlock[0x8000]; /* the save block (the PC's is 32 KB) */
#if !VERSION_JP
extern s32 savedPartyIsOnGear[3];
#endif
extern u8 battleConfigs[0x220];
extern u8 requestedBattleConfig;
extern u8 battleTransitionEffect;
extern u8 battleInitVar0;
extern u8 battleInitVar1;
extern u16 D_8006F94E;
s32 pcRetryArmed(void); /* retry.c */
void pcGauntletCarry(GameState* g, const GameState* now, s32 recovery, u32 knockedOut);

/* port/src/system/gauntlet.h's recoveries */
enum { RECOVER_FULL, RECOVER_NONE, RECOVER_25, RECOVER_50, RECOVER_REVIVE };
enum { AFTER_NONE, AFTER_CHAIN, AFTER_END };

static GameState origin;
static u8 originBlock[sizeof(overlayStateSaveBlock)];
#if !VERSION_JP
static s32 originOnGear[3];
#endif
static u8 originConfigs[sizeof(battleConfigs)];
static u8 originConfig;
static u8 originTransition;
static u8 originInitVar1;
static s32 originTaken;
static s32 after;
static s32 cleared;
static u32 knocked;

/* The field or the world map: the first fight starts (1) now. */
int pcGauntletStartBattle(void) {
    s32 config;

    if (!pcGauntletTakeStart()) {
        return 0;
    }
    __builtin_memcpy(originConfigs, battleConfigs, sizeof(originConfigs));
    originConfig = requestedBattleConfig;
    config = pcGauntletFight(0, battleConfigs, NULL, NULL);
    if (config < 0) {
        pcGauntletFinish(0);
        return 0;
    }
    requestedBattleConfig = config;
    battleInitVar1 = 0;
    originTaken = 0;
    after = AFTER_NONE;
    return 1;
}

/* battleMain, before the retry checkpoint. */
void pcGauntletBattleSetup(void) {
    u8 party[3];
    s32 gear;
    s32 i;

    if (pcGauntletState() != 2) {
        return;
    }
    if (!originTaken) {
        originTaken = 1;
        __builtin_memcpy(&origin, &gameState, sizeof(origin));
        __builtin_memcpy(originBlock, overlayStateSaveBlock, sizeof(originBlock));
#if !VERSION_JP
        __builtin_memcpy(originOnGear, savedPartyIsOnGear, sizeof(originOnGear));
#endif
        originTransition = battleTransitionEffect;
        originInitVar1 = battleInitVar1;
    }
    if (pcGauntletFight(pcGauntletCurrent(), battleConfigs, party, &gear) < 0) {
        return;
    }
    for (i = 0; i < 3; i++) {
        gameState.m1D34_currentParty[i] = party[i];
        gameState.m22B1_isOnGear[i] = party[i] != 0xFF && gear;
    }
}

/* battleMain, after its loop (before finishBattle): 1 in a gauntlet. */
int pcGauntletOver(int running, int eventLoss, int nativeChain, unsigned int knockedOut) {
    s32 next;
    s32 i;

    if (pcGauntletState() != 2 || !originTaken) {
        return 0;
    }
    knocked = knockedOut;
    cleared = 0;
    if (running == 0x81 && !eventLoss) {
        if (pcRetryArmed()) {
            pcGauntletRetried(); /* retry.c starts it again */
            return 1;
        }
        after = AFTER_END; /* Game over */
    } else if (running & 0x40) {
        after = AFTER_END; /* an escape */
    } else if (nativeChain) {
        return 1; /* the fight's battle event chains another of its battles */
    } else {
        next = pcGauntletFight(pcGauntletCurrent() + 1, NULL, NULL, NULL);
        if (next < 0) {
            after = AFTER_END;
            cleared = 1;
        } else if (pcGauntletQuitting()) {
            after = AFTER_END;
        } else {
            battleInitVar0 = next + 1;
            after = AFTER_CHAIN;
            return 1;
        }
    }
    for (i = 0; i < 3; i++) {
        gameState.m1D34_currentParty[i] = origin.m1D34_currentParty[i];
        gameState.m22B1_isOnGear[i] = origin.m22B1_isOnGear[i];
    }
    return 1;
}

static u32 recover(u32 value, u32 max, s32 recovery) {
    switch (recovery) {
    case RECOVER_FULL:
        return max;
    case RECOVER_25:
        value += max / 4;
        break;
    case RECOVER_50:
        value += max / 2;
        break;
    }
    return value > max ? max : value;
}

/* Items of a category: never more of one than before the run. */
static void carryItems(u8* counts, const u8* ids, const u8* nowCounts, const u8* nowIds, s32 n) {
    s32 j;
    s32 k;
    u32 have;

    for (j = 0; j < n; j++) {
        if (counts[j] == 0) {
            continue;
        }
        have = 0;
        if (nowIds[j] == ids[j]) {
            have = nowCounts[j];
        } else {
            for (k = 0; k < n; k++) {
                if (nowIds[k] == ids[j] && nowCounts[k] != 0) {
                    have = nowCounts[k];
                    break;
                }
            }
        }
        if (have < counts[j]) {
            counts[j] = have;
        }
    }
}

/* The next fight's gameState: the state before the run (g) with what the
 * battle that ended (now) carries over. */
void pcGauntletCarry(GameState* g, const GameState* now, s32 recovery, u32 knockedOut) {
    s32 i;
    u32 max;
    u32 hp;

    carryItems(g->m1D38_inventory1Counts, g->m1D9C_inventory1Ids, now->m1D38_inventory1Counts, now->m1D9C_inventory1Ids, 100);
    carryItems(g->m1E00_inventory2Counts, g->m1EC8_inventory2Ids, now->m1E00_inventory2Counts, now->m1EC8_inventory2Ids, 200);
    carryItems(g->m1F90_inventory0Counts, g->m2026_inventory0Ids, now->m1F90_inventory0Counts, now->m2026_inventory0Ids, 150);
    carryItems(g->m20BC_inventory3Counts, g->m2120_inventory3Ids, now->m20BC_inventory3Counts, now->m2120_inventory3Ids, 100);
    carryItems(g->m2184_inventory4Counts, g->m221A_inventory4Ids, now->m2184_inventory4Counts, now->m221A_inventory4Ids, 150);
    for (i = 0; i < 11; i++) {
        GameStateCharacter* c = &g->m26C_party[i];
        max = c->m4E_MaxHP;
        hp = recover(now->m26C_party[i].m4C_HP, max, recovery);
        if (recovery == RECOVER_REVIVE && (knockedOut & (1 << i)) && hp < max / 10) {
            hp = max / 10;
        }
        if (hp == 0 && max != 0 && (knockedOut & (1 << i))) {
            hp = 1; /* as finishBattle leaves one knocked out */
        }
        c->m4C_HP = hp;
        c->m50_MP = recover(now->m26C_party[i].m50_MP, c->m52_MaxMP, recovery);
    }
    for (i = 0; i < 20; i++) {
        GameStateGear* gear = &g->m978_gears[i];
        gear->m60_hp = recover((u32)now->m978_gears[i].m60_hp, (u32)gear->m64_maxHp, recovery);
        if (gear->m60_hp == 0) {
            gear->m60_hp = (u32)gear->m64_maxHp / 10; /* as finishBattle leaves one destroyed */
        }
        gear->m38_fuel = recover(now->m978_gears[i].m38_fuel, gear->m3A_maxFuel, recovery);
    }
}

static void restoreOrigin(void) {
    __builtin_memcpy(overlayStateSaveBlock, originBlock, sizeof(originBlock));
#if !VERSION_JP
    __builtin_memcpy(savedPartyIsOnGear, originOnGear, sizeof(originOnGear));
#endif
    battleTransitionEffect = originTransition;
    battleInitVar1 = originInitVar1;
}

/* battle_boot, where it picks the next mode: -1 the game's own. */
int pcGauntletBattleEnd(void) {
    static GameState now;
    s32 kind = after;

    if (pcGauntletState() != 2 || kind == AFTER_NONE) {
        return -1;
    }
    after = AFTER_NONE;
    if (kind == AFTER_CHAIN) {
        __builtin_memcpy(&now, &gameState, sizeof(now));
        __builtin_memcpy(&gameState, &origin, sizeof(gameState));
        pcGauntletCarry(&gameState, &now, pcGauntletRecovery(), knocked);
        restoreOrigin();
        pcGauntletNext();
        return 2;
    }
    __builtin_memcpy(&gameState, &origin, sizeof(gameState));
    restoreOrigin();
    __builtin_memcpy(battleConfigs, originConfigs, sizeof(originConfigs));
    requestedBattleConfig = originConfig;
    battleInitVar0 = 0;
    originTaken = 0;
    pcGauntletFinish(cleared);
    return (D_8006F94E & 0x7FF) < 0x400 ? 1 : 3;
}

/* The members with the party now, for the menu (gauntlet.h PcGauntletRoster). */
int pcGauntletGameRoster(unsigned char* ids, unsigned char* hasGear, unsigned char* names, unsigned char* party) {
    u32 mask = gameState.m1D30_partyMemberBitField & gameState.m1D32_partyFrameMask & 0x7FF;
    s32 n = 0;
    s32 i;
    s32 k;

    for (i = 0; i < 11; i++) {
        if (!(mask & (1 << i))) {
            continue;
        }
        ids[n] = i;
        hasGear[n] = gameState.m26C_party[i].mA0_gearNum != 0xFF;
        for (k = 0; k < 20; k++) {
            names[n * 20 + k] = gameState.m0_names[i][k];
        }
        n++;
    }
    for (i = 0; i < 3; i++) {
        party[i] = gameState.m1D34_currentParty[i];
    }
    return n;
}
