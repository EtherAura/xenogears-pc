#ifndef PORT_EXTRAS_BESTIARY_DATA_H
#define PORT_EXTRAS_BESTIARY_DATA_H

/*
 * The bestiary's data (xg-htn.35.25), read from the player's discs as the
 * battle loader reads it (src/battle_loader/bld_801E4048.c loadEnemies):
 *
 *   0D/2+2n   enemy set n: u16 per type id k (+0, the type's 4 monster
 *             script offsets), u16 at +0x30 the set's string table (u32
 *             count, u16 offsets: string k+1 is type k's name, the actions'
 *             name indexes the attacks'), the 0x170-byte battle entity of
 *             each type id from +0x32 (include/battle/battle.h sBattleEntity:
 *             a GameStateCharacter, a GameStateGear, then experience, drops
 *             and gold)
 *   0D/3+2n   the set's visuals: entry k a sprite or a 3D model
 *   01/07     the text bundle: item names by inventory category (src/main/
 *             dialog.c getWeaponName etc.)
 *
 * A record is one (disc, set, type id); bestiary.cpp groups records of the
 * same monster. Pure data: no game state, no sandbox.
 */

#include <stdint.h>

#include "disc.h"

#ifdef __cplusplus
extern "C" {
#endif

#define XG_BEAST_RECORD 0x170

typedef struct {
    int chance;   /* % */
    int item;     /* id in its category */
    int category; /* 0 weapon, 1 accessory, 2 item, 3 Gear weapon, 4 Gear accessory */
    char name[64];
} XgBeastDrop;

typedef struct {
    int disc, set, type;
    char name[64];   /* "" when the font's codes have no spelling here (JP kanji) */
    int visual;      /* 1 sprite, -1 3D model, 0 none */
    int gear;        /* its numbers are a Gear's (GameStateGear) */
    int hp, level, attack, defence, agility, ether, etherDefence, hit, evade;
    int exp, gold;
    XgBeastDrop drops[2];
    int weakness;    /* GameStateCharacter m38: low 6 bits elements, 0x40 stronger */
    int resist[2];   /* m84_statusFlags2[4], [5] */
    int monsterFlags;  /* m34 */
    int monsterFlags2; /* m36 */
    int gearResist;  /* GameStateGear m6E_elementResistanceMask */
    uint8_t raw[XG_BEAST_RECORD];
    uint64_t identity; /* the record, the name and the visual's bytes: equal for the same monster */
} XgBeast;

/* An action of a type's monster scripts (src/battle/b_80079674.c: every op
 * 4 bytes; op 01 builds an 8-byte action, its m0 last; executeBattleCode
 * b_800769E8.c runs it). Only literal fields are known statically. */
typedef struct {
    int kind;      /* 1 an attack (performAttack), 13 an ability effect (playAbility),
                    * 12 a line it says (a quoted string shown with "show name") */
    int script;    /* 0 the enemy's turn, 2 when it is attacked, 3 at the end of the party's turn */
    int attack;    /* kind 1: m1, the shared attack table's record; -1 from a variable */
    int animation; /* kind 1: m2 (below 0x10 a pose of its 0D/3+2n program); -1 variable */
    int nameIndex; /* m3, a string of the set's table; -1 none or variable */
    int value;     /* kind 13: m4 | m5 << 8 (effect = value & 0x1FF, program = value >> 9 & 0x3F); -1 variable */
    char name[64];
    /* kind 1, from the attack table (include/battle/battle.h sBattle800CDD40Sub) */
    int power, elements, flags, hitBonus, statusType, statusMask, statusChance;
} XgBeastAttack;

/* Type `type` of set `set`: its distinct literal actions, at most `max`. */
int xgBeastAttacks(XgDisc* disc, int set, int type, XgBeastAttack* out, int max);

/* Where battles happen (a battle config, include/battle_loader/battle_loader.h
 * sBattleConfig: m0 the enemy set, m1 flags (0x20 its battle event, 0x40 no
 * Gears, 0x80 no escape), m2 the arena, m3 the event, enemy slots m4[4..11]
 * (0x7F empty, 0x80 a Gear, & 0x7F the type id)). */
enum { XG_PLACE_FIELD_RANDOM, XG_PLACE_FIELD_SCRIPTED, XG_PLACE_WORLD };
typedef struct {
    int disc, kind;
    int field;                /* fields: the field number */
    int world, zone, range;   /* the world map: configuration (0..16), zone (0..15), story range (0..3) */
    int config;               /* the config's index in its table */
    int set, flags, arena, event;
    int types[8];
    int weight, total;        /* random battles: its weight of the table's total (+1 on a field) */
    int randomMaybe;          /* a field whose encounter op takes its numbers from variables */
} XgBeastPlace;

/* Field n's battles: its random table (archive section 6: 16 configs and 16
 * weights, checkForRandomEncounter; listed when a reachable op F7 sets a
 * timer and a count) and the configs its scripts start (ops 71 and FE 84,
 * immediate indexes). *story: the lowest story progress its scripts set
 * (op 87), -1 none. Returns the count (at most max). */
int xgBeastFieldPlaces(XgDisc* disc, int field, XgBeastPlace* out, int max, int* story);
/* World map configuration c: every config a zone can roll in a story range
 * (archive +0x2C: 16 zone tables, 16 configs then weights[4][16]). */
int xgBeastWorldPlaces(XgDisc* disc, int world, XgBeastPlace* out, int max);
/* The world map configuration's earliest story progress (initWorldMap
 * thresholds for 0..8; -1 for the others, chosen by the game's mode) and a
 * story range's lower bound (0, 0x36, 0xC9, 0x154). */
int xgBeastWorldStory(int world);
int xgBeastRangeStory(int range);

/* Every enemy type of every set on this disc, appended to *out (realloc'd);
 * returns the new count, -1 on a read error. */
int xgBeastsRead(XgDisc* disc, XgBeast** out, int count);

/* The disc's item name (category as XgBeastDrop), "" if none. */
int xgBeastItemName(XgDisc* disc, int category, int item, char* out, int outSize);

/* The set's string i (0-based as getDialogParamPointer), UTF-8; 0 if none. */
int xgBeastSetString(XgDisc* disc, int set, int i, char* out, int outSize);

/* A record's numbers as the port's gameplay options make them at battle
 * load (system/gameplay.c pcGameplayEnemyStats, Experience, Gold). */
void xgBeastWithOptions(const XgBeast* in, XgBeast* out);

#ifdef __cplusplus
}
#endif
#endif
