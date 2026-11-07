/* The native battle's move selection as data: which command entries a party
 * member or a Gear can use, and the actions an enemy's scripts queue for a
 * turn. Pure functions on bytes read from the disc (no game state). */
#ifndef PORT_EXTRAS_BATTLE_MOVES_H
#define PORT_EXTRAS_BATTLE_MOVES_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

/* What a character can ever learn, from the battle results' level-up data
 * (12/02 member 0, LZSS; include/battle_result/battle_result.h
 * sLevelUpCharData, 0x110 bytes per command loadout):
 *  deathblows      learnDeathblow walks m100 until a level of 0xFF: deathblow
 *                  d is command entry 7 + d (performAttack: index - 1 of
 *                  deathblowAbilityIdsByLoadout[] + 8);
 *  ethers          m0F0 until 0xFF: ether n is entry 22 + n (executeAbility);
 *  gearDeathblows  m0D0 until 0xFF (unlockGearAttacksFromDeathblows; the
 *                  loadout 8 list holds levels): Gear entries 3 + i;
 *  gearEthers      m0E0 until 0 (unlockGearChiAbilities): Gear entry 21 + i.
 * The game's own unlock-everything (m_801E3088.c debugUnlockAllAbilities)
 * gives the same deathblows and Ethers and all nine Gear deathblows; it
 * gives some pilots more Gear Ethers than this list (events unlock them),
 * so a Gear's Ethers are better read from its command table. */
typedef struct {
    int deathblows, ethers, gearDeathblows, gearEthers;
} XgBattleLearnable;
int xgBattleLearnable(const uint8_t *levelUp, int size, int loadout, XgBattleLearnable *out);

/* A party member's attack entries (perApTable, b_80085350.c performAttack):
 * 1 AP presses walk entries 0..3, 2 AP alternate 4 and 5, 3 AP is entry 6.
 * Returns the AP cost of plain-attack entry 0..6 and its place in that
 * cycle (from 1), 0 for any other entry. */
int xgBattleAttackCost(int entry, int *place);

/* One enemy turn as its scripts build it (b_80079674.c executeMonsterScript:
 * op 01 writes field m1 of the current battleScriptContext and completes
 * it when the field is m0; 02, 3D and 52 write fields without completing;
 * a guard (80..EF) runs its block, FF/FD ends the run). A turn is the
 * script's unguarded actions plus one guarded block's. */
#define XG_TURN_ACTIONS 24
typedef struct {
    int script;                           /* 0 the enemy's turn, 2 when attacked, 3 after the party's turn */
    int count;                            /* actions, battleScriptContext entries (m0 kind .. m6 target mask) */
    uint8_t actions[XG_TURN_ACTIONS][8];
    uint8_t variable[XG_TURN_ACTIONS];    /* bit k: field k came from a script variable (0 here) */
    int nameIndex;                        /* the first name it shows (op 12 or an action's m3), -1 none */
} XgBattleTurn;
/* Action kinds (executeBattleCode, b_800769E8.c). */
enum {
    XG_ACT_ATTACK = 1,   /* performAttack: m47 = m2, m1 the attack record */
    XG_ACT_MOVE = 2,     /* moveAndAttack: 0xFD, approach when m4|m5 is 0 */
    XG_ACT_IDLE = 3,     /* setIdleAnimation: 0xFC m4|m5 */
    XG_ACT_ANIMATION = 4,/* playAnimation: m47 = m4 */
    XG_ACT_REMOVE = 6,   /* removeMonster: 0xF9 */
    XG_ACT_NAME = 12,    /* showMonsterName m3: 0xFA */
    XG_ACT_ABILITY = 13, /* playAbility: 0xF4 m4|m5 (effect & 0x1FF, program >> 9) */
    XG_ACT_WAIT = 14,    /* statusEffect: 0xF7, a delay of m4 */
    XG_ACT_CAMERA = 15,  /* specialAnimation: 0xF6, the camera on m6 */
    XG_ACT_HIDE = 16,    /* hideMonsterName: 0xF8 */
};
/* Whether a script action plays in a preview: its kind is one of the above
 * (state changes 5, 7-11 are left out) and the fields it plays are literal. */
int xgBattleActionPlays(const uint8_t action[8], int variable);
/* The distinct turns of type `type` in a set's script file (0D/2+2n) that
 * play (every action does, at least one is an attack or ability or
 * animation), at most `max`; `found` (may be NULL) gets every turn seen,
 * playable or not. -1: not a set file or no such type. */
int xgBattleTurns(const uint8_t *set, int size, int type, XgBattleTurn *out, int max, int *found);

#ifdef __cplusplus
}
#endif
#endif
