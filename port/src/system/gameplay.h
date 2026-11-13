#ifndef PORT_GAMEPLAY_H
#define PORT_GAMEPLAY_H

int pcGameplayEncounterRate(void);
void pcGameplaySetEncounterRate(int percent);

enum GameplayEnemyStat {
    GAMEPLAY_ENEMY_HP, GAMEPLAY_ENEMY_ATTACK, GAMEPLAY_ENEMY_ETHER,
    GAMEPLAY_ENEMY_DEFENCE, GAMEPLAY_ENEMY_ETHER_DEFENCE, GAMEPLAY_ENEMY_AGILITY,
    GAMEPLAY_ENEMY_STAT_COUNT
};
int pcGameplayHardMode(void);
void pcGameplaySetHardMode(int hard);
int pcGameplayEnemyMultiplier(int stat);
void pcGameplaySetEnemyMultiplier(int stat, int percent);
const char* pcGameplayEnemyKey(int stat);

/* EXP and gold for random battles (the original keys) and for scripted ones. */
enum GameplayReward {
    GAMEPLAY_REWARD_EXP, GAMEPLAY_REWARD_GOLD, GAMEPLAY_REWARD_SCRIPTED_EXP, GAMEPLAY_REWARD_SCRIPTED_GOLD,
    GAMEPLAY_REWARD_COUNT
};
int pcGameplayRewardMultiplier(int reward);
void pcGameplaySetRewardMultiplier(int reward, int percent);
const char* pcGameplayRewardKey(int reward);

/* The class of the battle that started last (pc_gameplay.h records it). */
enum GameplayBattleClass { GAMEPLAY_BATTLE_SCRIPTED, GAMEPLAY_BATTLE_RANDOM };
int pcGameplayBattleClass(void);
void pcGameplaySetBattleClass(int cls); /* tests */

/* Deathblow practice per eligible attack, 0..1000 percent (100: original). */
int pcGameplayDeathblowLearning(void);
void pcGameplaySetDeathblowLearning(int percent);

/* Retry a battle lost to the native game over from its start
 * (port/src/game/main/retry.c, port/src/game/battle/retryprompt.c). */
int pcGameplayBattleRetry(void);
void pcGameplaySetBattleRetry(int enabled);
void pcGameplayBattleRetryClass(void);

/* The enemy info window in battle (port/src/game/battle/enemyinfo.c), and
 * its rule to show an enemy's HP only once the party has beaten that enemy
 * type before (the extras' bestiary record). */
#define GAMEPLAY_ENEMY_INFO_OFF 0
#define GAMEPLAY_ENEMY_INFO_ALWAYS 1
#define GAMEPLAY_ENEMY_INFO_BEATEN 2
int pcGameplayEnemyInfoMode(void);
void pcGameplaySetEnemyInfoMode(int mode);
int pcGameplayEnemyInfo(void);      /* the window at all (mode != off) */
int pcGameplayEnemyInfoKnown(void); /* HP only once beaten (mode beaten) */
/* A lone Select's release toggles the window; Select with a shoulder button
 * is a chord (*chord: its state between calls). */
int pcGameplaySelectTap(int previous, int held, int* chord);

/* Dialogue: text_speed (1 original, 2..8, 0 instant), holding confirm for
 * instant text, auto-advance after 1..10 seconds (0 off). */
int pcGameplayTextSpeed(void);
void pcGameplaySetTextSpeed(int speed);
int pcGameplayTextHold(void);
void pcGameplaySetTextHold(int enabled);
int pcGameplayTextAutoAdvance(void);
void pcGameplaySetTextAutoAdvance(int seconds);
int pcGameplayTimedText(const unsigned char* text, int twoByteLead);

int pcGameplayOriginalBugFixes(void);
/* The Enhanced engine's 60 Hz field (default on there; the Original never asks). */
int pcGameplayField60(void);
void pcGameplaySetField60(int on);
/* The Enhanced engine's 60 Hz 20/15 fps rooms (field60slow; default on there,
 * always off in the Original), and the stored setting in either engine. */
int pcGameplayField60Slow(void);
int pcGameplayField60SlowSetting(void);
/* The Enhanced engine's 60 Hz world map (default on there; the Original never asks). */
int pcGameplayWorldmap60(void);
void pcGameplaySetWorldmap60(int on);
/* The Enhanced engine's 60 Hz battle (default on there; always off in the Original). */
int pcGameplayBattle60(void);
void pcGameplaySetBattle60(int on);
void pcGameplaySetOriginalBugFixes(int enabled);

#endif
