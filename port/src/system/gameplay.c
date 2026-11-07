/* Optional gameplay settings. Original rates are the default. */
#include <errno.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include "battle/battle.h"
#include "pc_gameplay.h"
#include "gameplay.h"
#include "savestate.h"
#include "settings.h"

#ifndef XG_ENGINE_ENHANCED
#define XG_ENGINE_ENHANCED 0
#endif

static int clampRate(long percent) {
    return percent < 0 ? 0 : percent > 400 ? 400 : (int)percent;
}

int pcGameplayOriginalBugFixes(void) {
    return settingsBool("original_bug_fixes", 0);
}

void pcGameplaySetOriginalBugFixes(int enabled) {
    settingsDescribe("original_bug_fixes", "All validated original-game bug fixes together; off preserves retail behavior");
    settingsSetInt("original_bug_fixes", enabled != 0);
    settingsSave();
}

static int percentSetting(const char* key, int minimum, int maximum) {
    const char* text = settingsGet(key);
    char* end;
    long percent;
    if (text == NULL) return 100;
    errno = 0;
    percent = strtol(text, &end, 10);
    if (end == text || *end != '\0' || errno == ERANGE) return 100;
    return percent < minimum ? minimum : percent > maximum ? maximum : (int)percent;
}

int pcGameplayEncounterRate(void) {
    return percentSetting("encounter_rate", 0, 400);
}

void pcGameplaySetEncounterRate(int percent) {
    settingsDescribe("encounter_rate", "Random encounters: 0=off, 100=original, up to 400 percent");
    settingsSetInt("encounter_rate", clampRate(percent));
    settingsSave();
}

int pcGameplayEncounterTicks(int* fraction) {
    int rate = pcGameplayEncounterRate();
    if (rate == 0 || rate == 100) {
        *fraction = 0;
        return rate == 100;
    }
    int total = *fraction + rate;
    *fraction = total % 100;
    return total / 100;
}

/* The two original keys keep their names and now mean random battles, the
 * battles encounter-rate compensation is for; scripted battles (bosses, story
 * fights, enemies a script starts) have their own keys at the original 100%. */
static const char* const rewardKeys[GAMEPLAY_REWARD_COUNT] = {
    "exp_multiplier", "gold_multiplier", "scripted_exp_multiplier", "scripted_gold_multiplier"
};

const char* pcGameplayRewardKey(int reward) {
    return reward >= 0 && reward < GAMEPLAY_REWARD_COUNT ? rewardKeys[reward] : NULL;
}

int pcGameplayRewardMultiplier(int reward) {
    const char* key = pcGameplayRewardKey(reward);
    return key != NULL ? percentSetting(key, 0, 1000) : 100;
}

void pcGameplaySetRewardMultiplier(int reward, int percent) {
    const char* key = pcGameplayRewardKey(reward);
    if (key == NULL) return;
    settingsDescribe(key, reward == GAMEPLAY_REWARD_EXP || reward == GAMEPLAY_REWARD_GOLD
                              ? "Random-battle reward multiplier in percent, 0..1000; 100=original, independent of encounter rate"
                              : "Scripted-battle (boss, story) reward multiplier in percent, 0..1000; 100=original");
    settingsSetInt(key, percent < 0 ? 0 : percent > 1000 ? 1000 : percent);
    settingsSave();
}

/* Battle class. Only a battle the field or world-map encounter countdown
 * rolled is random: the roll records its config, and the battle that starts
 * next takes the class when it loads that same config without a battle-event
 * chain (battle event op 0x24). Every other start (field script ops 0x71 and
 * EX 0x84, chained battles, the debug selector) is scripted. The roll and the
 * script ops exclude each other (both need playMusicAuthorized, both clear
 * it), and a roll always leads into its battle. */
static int pendingRandomConfig = -1;
static int battleClass = GAMEPLAY_BATTLE_SCRIPTED;

void pcGameplayRandomBattle(int config) {
    pendingRandomConfig = config;
}

/* A retried battle (port/src/game/main/retry.c) starts like a chained one
 * but keeps the class of the battle it repeats. */
static int retryKeepsClass;

void pcGameplayBattleRetryClass(void) {
    retryKeepsClass = 1;
}

static void enemyScriptForget(void);

void pcGameplayBattleStart(int config, int chained) {
    enemyScriptForget();
    if (retryKeepsClass) {
        retryKeepsClass = 0;
        pendingRandomConfig = -1;
        return;
    }
    battleClass = !chained && pendingRandomConfig >= 0 && config == pendingRandomConfig ? GAMEPLAY_BATTLE_RANDOM
                                                                                         : GAMEPLAY_BATTLE_SCRIPTED;
    pendingRandomConfig = -1;
}

int pcGameplayBattleClass(void) {
    return battleClass;
}

void pcGameplaySetBattleClass(int cls) {
    battleClass = cls == GAMEPLAY_BATTLE_RANDOM ? GAMEPLAY_BATTLE_RANDOM : GAMEPLAY_BATTLE_SCRIPTED;
    pendingRandomConfig = -1;
}

static unsigned int rewardScaled(unsigned int amount, int reward) {
    if (battleClass != GAMEPLAY_BATTLE_RANDOM) reward += GAMEPLAY_REWARD_SCRIPTED_EXP;
    int percent = pcGameplayRewardMultiplier(reward);
    if (percent == 100) return amount;
    uint64_t result = ((uint64_t)amount * (unsigned)percent + 50) / 100;
    /* Fit the native result counters: seven EXP digits, six gold-won digits.
     * The wallet retains its separate, original 9,999,999 cap. */
    unsigned int limit = reward == GAMEPLAY_REWARD_GOLD || reward == GAMEPLAY_REWARD_SCRIPTED_GOLD ? 999999 : 9999999;
    return result > limit ? limit : (unsigned int)result;
}

unsigned int pcGameplayExperience(unsigned int amount) {
    return rewardScaled(amount, GAMEPLAY_REWARD_EXP);
}

unsigned int pcGameplayGold(unsigned int amount) {
    return rewardScaled(amount, GAMEPLAY_REWARD_GOLD);
}

int pcGameplayDeathblowLearning(void) {
    return percentSetting("deathblow_learning", 0, 1000);
}

void pcGameplaySetDeathblowLearning(int percent) {
    settingsDescribe("deathblow_learning", "Deathblow practice per attack in percent, 0..1000; 100=original");
    settingsSetInt("deathblow_learning", percent < 0 ? 0 : percent > 1000 ? 1000 : percent);
    settingsSave();
}

/* Hundredths of a practice point carried to the same character's next
 * attack on the same counter. Never saved: at most one point per counter. */
static unsigned char practiceCarry[16][8];

unsigned int pcGameplayDeathblowPractice(unsigned int character, unsigned int combo, unsigned int progress,
                                         unsigned int increment) {
    int percent = pcGameplayDeathblowLearning();
    unsigned char none = 0;
    unsigned char* carry = character < 16 && combo < 8 ? &practiceCarry[character][combo] : &none;
    if (percent == 100) return (progress + increment) & 0xFFFF; /* the native u16 sum */
    if (percent == 0) {
        *carry = 0;
        return progress;
    }
    /* At most 510 x 1000 + 99: no overflow. The native gate admits progress
     * below 65000; a large step stops at the counter's 65535. */
    unsigned int total = increment * (unsigned)percent + *carry;
    *carry = (unsigned char)(total % 100);
    unsigned int result = progress + total / 100;
    return result > 65535 ? 65535 : result;
}

int pcGameplayBattleRetry(void) {
    return settingsBool("battle_retry", 0);
}

void pcGameplaySetBattleRetry(int enabled) {
    settingsDescribe("battle_retry", "Battle: offer Retry, from the battle's start, instead of the game over (0/1)");
    settingsSetInt("battle_retry", enabled != 0);
    settingsSave();
}

/* enemy_info: off, always, beaten (the earlier 0/1 read as off/always). */
int pcGameplayEnemyInfoMode(void) {
    const char* v = settingsGet("enemy_info");
    if (v == NULL) return GAMEPLAY_ENEMY_INFO_OFF;
    if (strcmp(v, "beaten") == 0) return GAMEPLAY_ENEMY_INFO_BEATEN;
    if (strcmp(v, "always") == 0 || strcmp(v, "1") == 0 || strcmp(v, "on") == 0 || strcmp(v, "true") == 0) {
        return GAMEPLAY_ENEMY_INFO_ALWAYS;
    }
    return GAMEPLAY_ENEMY_INFO_OFF;
}

void pcGameplaySetEnemyInfoMode(int mode) {
    settingsDescribe("enemy_info", "Battle: Select toggles a window with each enemy's name, HP and statuses: off, always, or beaten (HP only for enemy types beaten before, bestiary.txt)");
    settingsSet("enemy_info", mode == GAMEPLAY_ENEMY_INFO_BEATEN ? "beaten" : mode == GAMEPLAY_ENEMY_INFO_ALWAYS ? "always" : "off");
    settingsSave();
}

int pcGameplayEnemyInfo(void) {
    return pcGameplayEnemyInfoMode() != GAMEPLAY_ENEMY_INFO_OFF;
}

int pcGameplayEnemyInfoKnown(void) {
    return pcGameplayEnemyInfoMode() == GAMEPLAY_ENEMY_INFO_BEATEN;
}

/* The enemy info window's Select (port/src/game/battle/enemyinfo.c): a lone
 * Select toggles it, on its release. A shoulder button (L1, R1, L2, R2) held
 * when Select goes down, or going down while it is held, makes a chord
 * (battle speed's Select+R2, rewind's Select+L2) that toggles nothing.
 * previous, held: the pad's buttons at the last battle frame and now (the
 * game's active-high bits); *chord carries the chord from call to call. */
int pcGameplaySelectTap(int previous, int held, int* chord) {
    int pressed = held & ~previous;
    int released = previous & ~held;
    if (pressed & 0x100) {
        *chord = (held & 0x000F) != 0;
    } else if ((held & 0x100) && (pressed & 0x000F)) {
        *chord = 1;
    }
    return (released & 0x100) && !*chord;
}

static const char* const enemyKeys[GAMEPLAY_ENEMY_STAT_COUNT] = {
    "enemy_hp", "enemy_attack", "enemy_ether", "enemy_defence", "enemy_ether_defence", "enemy_agility"
};

const char* pcGameplayEnemyKey(int stat) {
    return stat >= 0 && stat < GAMEPLAY_ENEMY_STAT_COUNT ? enemyKeys[stat] : NULL;
}

int pcGameplayHardMode(void) {
    const char* mode = settingsGet("difficulty");
    return mode != NULL && strcmp(mode, "hard") == 0;
}

void pcGameplaySetHardMode(int hard) {
    settingsDescribe("difficulty", "Difficulty: normal or hard (curated enemy HP/offense; story encounters retain their scripted profile)");
    settingsSet("difficulty", hard ? "hard" : "normal");
    settingsSave();
}

int pcGameplayEnemyMultiplier(int stat) {
    const char* key = pcGameplayEnemyKey(stat);
    return key != NULL ? percentSetting(key, 25, 400) : 100;
}

void pcGameplaySetEnemyMultiplier(int stat, int percent) {
    const char* key = pcGameplayEnemyKey(stat);
    if (key == NULL) return;
    settingsDescribe(key, "Enemy stat multiplier in percent, 25..400; stacks with the chosen difficulty");
    settingsSetInt(key, percent < 25 ? 25 : percent > 400 ? 400 : percent);
    settingsSave();
}

/* Combine before rounding or saturation, so Hard x 50% HP cancels exactly.
 * Keep zero stats and dead/negative-HP sentinels intact. */
static int scaled(int value, int hard, int custom, int limit) {
    if (value <= 0) return value;
    uint64_t result = ((uint64_t)value * (unsigned)hard * (unsigned)custom + 5000) / 10000;
    return result > (unsigned)limit ? limit : result < 1 ? 1 : (int)result;
}

/* Ratios are authored from our native replays, not a reference patch.
 * The opening Rankar and Alpha Weltall are story encounters; Margie and
 * Miang in the Ramsus fight are support actors. Keep their Hard profile
 * neutral. User multipliers still compose with these profiles. The keys
 * (set: m0_battleId, type: the record loadEnemies copies) are the same
 * enemies in the JP set files on both discs (tools/test_hard_profile_ids.py). */
static int hardPercent(int set, int type, int stat) {
    if (!pcGameplayHardMode()) return 100;
    if ((set == 11 && type == 0) || (set == 13 && type == 1) ||
        (set == 45 && (type == 0 || type == 2))) return 100;
    /* The Aveh corporal formation is an early resource check. */
    if (set == 4 && type == 3) {
        if (stat == GAMEPLAY_ENEMY_HP) return 150;
        if (stat == GAMEPLAY_ENEMY_ATTACK || stat == GAMEPLAY_ENEMY_ETHER) return 125;
    }
    /* Shakhan's drain and Hammer's self-destruction punish a doubled fight
     * duration: the recorded level-50/65 Gear fixtures clear Normal but wipe
     * at 2x/1.5x. Keep a longer fight with a smaller offense increase. */
    if ((set == 53 && type == 0) || (set == 55 && type == 1)) {
        if (stat == GAMEPLAY_ENEMY_HP) return 150;
        if (stat == GAMEPLAY_ENEMY_ATTACK || stat == GAMEPLAY_ENEMY_ETHER) return 125;
    }
    if (stat == GAMEPLAY_ENEMY_HP) return 200;
    if (stat == GAMEPLAY_ENEMY_ATTACK || stat == GAMEPLAY_ENEMY_ETHER) return 150;
    return 100;
}

/* The script VM speaks original stat units. Its literal HP thresholds,
 * phase resets and stat changes therefore keep their meaning at any setting.
 * A current HP read uses the effective max-HP ratio, including saturation.
 * Snapshots also recover an unchanged byte stat after saturation/rounding. */
enum { SCRIPT_CHAR_MAX, SCRIPT_CHAR_HP, SCRIPT_GEAR_MAX, SCRIPT_GEAR_HP,
       SCRIPT_ATTACK, SCRIPT_ETHER, SCRIPT_DEFENCE, SCRIPT_ETHER_DEFENCE,
       SCRIPT_AGILITY, SCRIPT_GEAR_ATTACK, SCRIPT_GEAR_DEFENCE, SCRIPT_GEAR_ETHER_DEFENCE, SCRIPT_STATS };
typedef struct { int percent, original, modified; } ScriptStat;
static ScriptStat enemyScriptStats[8][SCRIPT_STATS];

/* As a battle starts, before its loader: the last battle's slots mean
 * nothing now. A slot this battle leaves empty (percent 0) translates
 * nothing, rather than at the scale an earlier battle, perhaps under other
 * settings, gave it. */
static void enemyScriptForget(void) {
    memset(enemyScriptStats, 0, sizeof enemyScriptStats);
}

static const unsigned char scriptStats[SCRIPT_STATS] = {
    GAMEPLAY_ENEMY_HP, GAMEPLAY_ENEMY_HP, GAMEPLAY_ENEMY_HP, GAMEPLAY_ENEMY_HP,
    GAMEPLAY_ENEMY_ATTACK, GAMEPLAY_ENEMY_ETHER, GAMEPLAY_ENEMY_DEFENCE, GAMEPLAY_ENEMY_ETHER_DEFENCE,
    GAMEPLAY_ENEMY_AGILITY, GAMEPLAY_ENEMY_ATTACK, GAMEPLAY_ENEMY_DEFENCE, GAMEPLAY_ENEMY_ETHER_DEFENCE
};

static int statLimit(int field, int value) {
    if (field <= SCRIPT_CHAR_HP) return INT16_MAX;
    if (field <= SCRIPT_GEAR_HP) return INT32_MAX;
    if (field >= SCRIPT_GEAR_DEFENCE) return 65535;
    if (field == SCRIPT_AGILITY) return value > 16 ? value : 16;
    return 255;
}

static void enemyStats(void* entity, int set, int type, ScriptStat* snapshot) {
    sBattleEntity* e = entity;
    int before[SCRIPT_STATS] = {
        e->m0_base.m4E_MaxHP, e->m0_base.m4C_HP, e->mA4_gear.m64_maxHp, e->mA4_gear.m60_hp,
        e->m0_base.m58_attack, e->m0_base.m5B_ether, e->m0_base.m59_defence, e->m0_base.m5C_etherDefence,
        e->m0_base.m5A_agility, e->mA4_gear.m3F_attackMultiplier, e->mA4_gear.m70_defence, e->mA4_gear.m72_etherDefence
    };
    int after[SCRIPT_STATS];
    for (int i = 0; i < SCRIPT_STATS; i++) {
        int stat = scriptStats[i];
        int percent = hardPercent(set, type, stat) * pcGameplayEnemyMultiplier(stat);
        after[i] = percent == 10000 ? before[i] : scaled(before[i], percent, 1, statLimit(i, before[i]));
        if (snapshot != NULL) snapshot[i] = (ScriptStat){percent, before[i], after[i]};
    }
    e->m0_base.m4E_MaxHP = after[SCRIPT_CHAR_MAX];
    e->m0_base.m4C_HP = after[SCRIPT_CHAR_HP];
    e->mA4_gear.m64_maxHp = after[SCRIPT_GEAR_MAX];
    e->mA4_gear.m60_hp = after[SCRIPT_GEAR_HP];
    e->m0_base.m58_attack = after[SCRIPT_ATTACK];
    e->m0_base.m5B_ether = after[SCRIPT_ETHER];
    e->m0_base.m59_defence = after[SCRIPT_DEFENCE];
    e->m0_base.m5C_etherDefence = after[SCRIPT_ETHER_DEFENCE];
    e->m0_base.m5A_agility = after[SCRIPT_AGILITY];
    e->mA4_gear.m3F_attackMultiplier = after[SCRIPT_GEAR_ATTACK];
    e->mA4_gear.m70_defence = after[SCRIPT_GEAR_DEFENCE];
    e->mA4_gear.m72_etherDefence = after[SCRIPT_GEAR_ETHER_DEFENCE];
}

/* Entities outside the battle's enemy slots (the bestiary's rows, tests):
 * no script snapshot. The loader's copies go through pcGameplayEnemyLoaded,
 * a script's copy of a monster through pcGameplayEnemyCopied. */
void pcGameplayEnemyStats(void* entity) { enemyStats(entity, -1, -1, NULL); }
void pcGameplayEnemyStatsForType(void* entity, int set, int type) { enemyStats(entity, set, type, NULL); }

void pcGameplayEnemyLoaded(void* entity, int slot, int set, int type) {
    enemyStats(entity, set, type, slot >= 3 && slot < 11 ? enemyScriptStats[slot - 3] : NULL);
}

void pcGameplayEnemyCopied(int dest, int source) {
    if (dest != source && dest >= 3 && dest < 11 && source >= 3 && source < 11)
        memcpy(enemyScriptStats[dest - 3], enemyScriptStats[source - 3], sizeof enemyScriptStats[0]);
}

int pcGameplayEnemyScriptValue(int slot, int kind, int field, int value, int read) {
    int mapped = -1;
    if (slot < 3 || slot >= 11) return value;
    if (kind == 0) { /* accessBattleEntityByteStat fields */
        switch (field) {
        case 3: mapped = SCRIPT_GEAR_ATTACK; break;
        case 4: mapped = SCRIPT_ATTACK; break;
        case 5: mapped = SCRIPT_DEFENCE; break;
        case 6: mapped = SCRIPT_AGILITY; break;
        case 9: mapped = SCRIPT_ETHER; break;
        case 10: mapped = SCRIPT_ETHER_DEFENCE; break;
        }
    } else if (kind == 1) { /* accessBattleEntityWordStat fields */
        switch (field) {
        case 0: mapped = SCRIPT_CHAR_MAX; break;
        case 1: mapped = SCRIPT_CHAR_HP; break;
        case 13: mapped = SCRIPT_GEAR_DEFENCE; break;
        case 14: mapped = SCRIPT_GEAR_ETHER_DEFENCE; break;
        }
    } else if (kind == 2 && (field == 0 || field == 1)) {
        mapped = field == 0 ? SCRIPT_GEAR_MAX : SCRIPT_GEAR_HP;
    }
    if (mapped < 0) return value;
    ScriptStat* stat = &enemyScriptStats[slot - 3][mapped];
    if (stat->percent == 0 || stat->percent == 10000) return value;
    if (read) {
        if (value <= 0) return value;
        if (value == stat->modified) return stat->original;
        if (mapped == SCRIPT_CHAR_HP || mapped == SCRIPT_GEAR_HP) {
            ScriptStat* max = stat - 1;
            if (max->original > 0 && max->modified > 0) {
                uint64_t result = ((uint64_t)value * (unsigned)max->original + (unsigned)max->modified / 2) /
                                  (unsigned)max->modified;
                return result < 1 ? 1 : result > INT32_MAX ? INT32_MAX : (int)result;
            }
        }
        uint64_t result = ((uint64_t)value * 10000 + (unsigned)stat->percent / 2) / (unsigned)stat->percent;
        return result < 1 ? 1 : result > INT32_MAX ? INT32_MAX : (int)result;
    }
    stat->original = value;
    /* Zero/death and one-HP story transitions stay exact. */
    stat->modified = mapped <= SCRIPT_GEAR_HP && value <= 1 ? value :
        scaled(value, stat->percent, 1, statLimit(mapped, value));
    return stat->modified;
}

/* ---- Dialogue -------------------------------------------------------------------------------- */

/* text_speed: 1 = original, 2..8 that many printer steps a frame, 0 = instant
 * (written "instant"). */
int pcGameplayTextSpeed(void) {
    const char* text = settingsGet("text_speed");
    if (text != NULL && strcmp(text, "instant") == 0) return 0;
    int speed = settingsInt("text_speed", 1); /* unset or not a number: original */
    return speed < 1 ? 1 : speed > 8 ? 8 : speed;
}

void pcGameplaySetTextSpeed(int speed) {
    settingsDescribe("text_speed", "Dialogue text: 1=original, 2..8 times as fast, instant; timed text keeps its pace");
    if (speed <= 0) {
        settingsSet("text_speed", "instant");
    } else {
        settingsSetInt("text_speed", speed > 8 ? 8 : speed);
    }
    settingsSave();
}

int pcGameplayTextHold(void) {
    return settingsBool("text_hold_fast", 0);
}

void pcGameplaySetTextHold(int enabled) {
    settingsDescribe("text_hold_fast", "Dialogue text: holding the confirm button shows it at once (0/1)");
    settingsSetInt("text_hold_fast", enabled != 0);
    settingsSave();
}

int pcGameplayTextAutoAdvance(void) {
    int seconds = settingsInt("text_auto_advance", 0);
    return seconds < 0 ? 0 : seconds > 10 ? 10 : seconds;
}

void pcGameplaySetTextAutoAdvance(int seconds) {
    settingsDescribe("text_auto_advance", "Dialogue: seconds a finished page waits before it continues itself, 0=off; "
                                          "never answers a choice");
    settingsSetInt("text_auto_advance", seconds < 0 ? 0 : seconds > 10 ? 10 : seconds);
    settingsSave();
}

/* A string paces itself when it has escape 0x0F with 0x01 (characters per
 * step), 0x02 or 0x0D (close by itself after a delay) or 0x0E (delay between
 * characters) -- the game's timed and slow text, which keeps its original
 * pace. A pause (0x0F 0x00) stays a pause: faster text stops at it, waits
 * its frames, then goes on. Lengths follow updateDialogTextImage (src/main/dialog.c):
 * 0x0F 0x03 takes four bytes, 0x04 and 0x0B two, the others three; a byte
 * below the font's two-byte lead is one character. Unparsable text counts
 * as timed. */
int pcGameplayTimedText(const unsigned char* s, int lead) {
    if (s == NULL) return 1;
    for (int i = 0; i < 8192;) {
        unsigned c = s[i];
        if (c == 0) return 0;
        if (c == 0x0F) {
            unsigned e = s[i + 1];
            if (e == 0x01 || e == 0x02 || e == 0x0D || e == 0x0E) return 1;
            i += e == 0x03 ? 4 : e == 0x04 || e == 0x0B ? 2 : 3;
        } else if (c < (unsigned)lead) {
            i += 1;
        } else {
            i += 2;
        }
    }
    return 1;
}

/* The last strings the printer started, by window: whether each paces itself. */
static struct { const void* window; int timed; } dialogueWindows[8];
static unsigned dialogueNext;

void pcGameplayDialogueText(const void* window, const unsigned char* text, int lead) {
    unsigned i;
    for (i = 0; i < 8 && dialogueWindows[i].window != window; i++) {
    }
    if (i == 8) {
        i = dialogueNext++ % 8;
        dialogueWindows[i].window = window;
    }
    dialogueWindows[i].timed = pcGameplayTimedText(text, lead);
}

int pcGameplayDialogueSteps(const void* window, int confirmHeld) {
    int speed = pcGameplayTextSpeed();
    int hold = confirmHeld && pcGameplayTextHold();
    if (speed == 1 && !hold) return 0;
    for (unsigned i = 0; i < 8; i++) {
        if (dialogueWindows[i].window == window) {
            if (dialogueWindows[i].timed) return 0;
            return hold || speed == 0 ? 1000 : speed - 1;
        }
    }
    return 0; /* a string started before the hook saw it: original pace */
}

/* One timer per context (0 the field, 1 battle dialogue): how long a page
 * has waited for the confirm button, in the game's play-time vsyncs (they
 * stop while the game is paused). */
static uint32_t autoSince[2];
static int autoWaiting[2];

int pcGameplayDialogueAuto(int context, int waiting, unsigned int now) {
    int seconds = pcGameplayTextAutoAdvance();
    context = context != 0;
    if (seconds == 0 || !waiting) {
        autoWaiting[context] = 0;
        return 0;
    }
    if (!autoWaiting[context]) {
        autoWaiting[context] = 1;
        autoSince[context] = now;
        return 0;
    }
    if (now - autoSince[context] < (uint32_t)seconds * 60) return 0;
    autoWaiting[context] = 0;
    return 1;
}

/* ---- save states (savestate.h) ---- */

/* The pending battle class, practice carries, the dialogue windows' pacing
 * (window pointers into game memory) and the auto-advance timers. */
void gameplayStateIo(XgState* s) {
    XG_STATE_VAR(s, enemyScriptStats);
    XG_STATE_VAR(s, pendingRandomConfig);
    XG_STATE_VAR(s, battleClass);
    XG_STATE_VAR(s, retryKeepsClass);
    XG_STATE_VAR(s, practiceCarry);
    XG_STATE_VAR(s, dialogueWindows);
    XG_STATE_VAR(s, dialogueNext);
    XG_STATE_VAR(s, autoSince);
    XG_STATE_VAR(s, autoWaiting);
}

/* xg-hbc.11: the Enhanced engine's 60 Hz field (engine/src/field, docs/
 * ENGINE_PLAN.md). field60 (XG_FIELD60 for one run) runs a field's logic every
 * vblank instead of every second one, with movement, sprite animation and the
 * camera at their authored speed and scripts, dialogue, RNG and effects at
 * their authored cadence; fields on the slower 3- and 4-vblank schedules keep
 * theirs. On by default in the Enhanced engine; the Original never asks. */
static int field60Checked, field60On;
static void field60Read(void) {
    if (!field60Checked) {
        field60Checked = 1;
        field60On = settingsBool("field60", XG_ENGINE_ENHANCED);
    }
}
int pcGameplayField60(void) {
    field60Read();
    return field60On;
}
void pcGameplaySetField60(int on) {
    field60Checked = 1;
    field60On = on != 0;
    settingsDescribe("field60", "field60: 1 runs fields at 60 Hz in the Enhanced engine (input every vblank, movement and "
                                "camera every frame, scripts at their authored cadence), 0 the authored 30 Hz; "
                                "XG_FIELD60 overrides for one run");
    settingsSet("field60", on ? "1" : "0");
}
/* xg-hbc.19.3: the rooms on the 3- and 4-vblank schedules (20 and 15 fps, op
 * D6) run at 60 Hz with the rest of the field (field60; the preview method,
 * engine/src/field/f_8008083C.c pcFieldPreview*), exact against the Original at
 * every native frame (docs/ENGINE_PLAN.md "60 Hz field, remaining motion").
 * field60slow (XG_FIELD60SLOW=0 for one run) only exists to turn them back to
 * their authored cadence for a comparison; on by default, no F1 row: '60 Hz
 * fields' covers them. */
int pcGameplayField60Slow(void) {
#if XG_ENGINE_ENHANCED
    static int field60SlowChecked, field60SlowOn;
    if (!field60SlowChecked) {
        field60SlowChecked = 1;
        field60SlowOn = settingsBool("field60slow", 1);
    }
    return field60SlowOn;
#else
    return 0;
#endif
}
int pcGameplayFieldFrameVBlanks(int native) {
    field60Read();
    if (!field60On) return native;
    return native <= 2 ? 1 : native / 2;
}

/* xg-hbc.19.1: the Enhanced engine's 60 Hz world map (engine/src/worldmap/
 * w_80070CFC.c, docs/ENGINE_PLAN.md "60 Hz world map"). worldmap60
 * (XG_WORLDMAP60 for one run) runs the walking world map's loop every vblank:
 * its logic, input, encounters and RNG on every other frame exactly as the
 * Original's, the party, vehicles and camera drawn halfway in between. On by
 * default in the Enhanced engine; read when the world map's loop starts; the
 * Original never asks. */
static int worldmap60Checked, worldmap60On;
int pcGameplayWorldmap60(void) {
    if (!worldmap60Checked) {
        worldmap60Checked = 1;
        worldmap60On = settingsBool("worldmap60", XG_ENGINE_ENHANCED);
    }
    return worldmap60On;
}
void pcGameplaySetWorldmap60(int on) {
    worldmap60Checked = 1;
    worldmap60On = on != 0;
    settingsDescribe("worldmap60", "worldmap60: 1 runs the world map at 60 Hz in the Enhanced engine (movement and camera "
                                   "every frame, logic and encounters at the authored cadence), 0 the authored 30 Hz; "
                                   "XG_WORLDMAP60 overrides for one run");
    settingsSet("worldmap60", on ? "1" : "0");
}

/* xg-hbc.19.2: the Enhanced engine's 60 Hz battle (engine/src/battle,
 * port/src/game/battle/gearpose.c; docs/ENGINE_PLAN.md "60 Hz battle").
 * battle60 (XG_BATTLE60 for one run): what a battle advances every other
 * vblank (the Gears' animation steps and what follows them) moves every vblank,
 * the battle itself as the Original's. On by default in the Enhanced engine;
 * always off in the Original, whatever the settings file says. */
static int battle60Checked, battle60On;
int pcGameplayBattle60(void) {
#if XG_ENGINE_ENHANCED
    if (!battle60Checked) {
        battle60Checked = 1;
        battle60On = settingsBool("battle60", 1);
    }
    return battle60On;
#else
    return 0;
#endif
}
void pcGameplaySetBattle60(int on) {
    battle60Checked = 1;
    battle60On = on != 0;
    settingsDescribe("battle60", "battle60: 1 runs battles at 60 Hz in the Enhanced engine (the Gears' animation and what "
                                 "follows it every frame, the battle itself as the original), 0 the original's cadence; "
                                 "XG_BATTLE60 overrides for one run");
    settingsSet("battle60", on ? "1" : "0");
}

/* xg-hbc.19.3: the 60 Hz field draws a 20 or 15 fps room (three or four frames
 * a native frame): the sprites' speed and gravity factors keep more bits
 * (engine/src/main/sprite_actor.c). Set and cleared within one field frame;
 * kept here, the game modules' own data being the retail image's. */
static int spritePrecise;
void pcGameplaySetSpritePrecise(int on) {
    spritePrecise = on != 0;
}
int pcGameplaySpritePrecise(void) {
    return spritePrecise;
}

static int dialogStepSkip; /* xg-hbc.11: the 60 Hz field's off-tick frame, while its dialogs draw */
void pcGameplaySetDialogStepSkip(int skip) {
    dialogStepSkip = skip;
}
int pcGameplayDialogStepSkip(void) {
    return dialogStepSkip;
}
/* xg-hbc.19.5: a 60 Hz field frame between whose window the next tick clears
 * for its next page (the confirm's preview, field/f_8007DCF8.c): its text
 * lines are not drawn. Drawing only. */
static int dialogHideLines;
void pcGameplaySetDialogHideLines(int on) {
    dialogHideLines = on;
}
int pcGameplayDialogHideLines(void) {
    return dialogHideLines;
}
