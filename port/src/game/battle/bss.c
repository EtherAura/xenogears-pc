/*
 * Battle BSS that the game C reads both as one object and through names
 * inside it (symbol-file or splat names at fixed PS1 addresses), as in
 * port/src/game/field/bss.c. On the PS1 those names are addresses inside the
 * block; on the PC the block's layout differs (pointers and ordering-table
 * tags are 8 bytes), so each name becomes an alias at its member's PC offset.
 * tools/pc_link.py would otherwise give every name its own storage, and the
 * views would drift apart. Defining the names here (in the battle module)
 * also makes every sub-overlay (battle_loader, battle_event, battle_result,
 * battle_debug*) and main use the battle's copy.
 */
#include "common.h"
#include "psyq.h"
#include "battle/battle.h"

/* 0x800C3EB0-0x800D2D28: BattleBss_800C3EB0, whose tail from +0x8E38 is the
 * start of BattleBss_800CCCE8 (battleEntities) and its rest after it. */
typedef union {
    BattleBss_800C3EB0 m0_head;
    struct {
        u8 m0[__builtin_offsetof(BattleBss_800C3EB0, m8E38_entities)];
        BattleBss_800CCCE8 m8E38_entities;
    } m0_tail;
} BattleBssBlock;

/* Defined, not tentative, so the aliases have a section to refer to; under
 * its own C name, as battle.h declares battleBssBlock with the head's type. */
BattleBssBlock battleBssUnion __asm__("battleBssBlock") = { 0 };

/* pBattleEntities (battle .data): the address of battleEntities. */
BattleBss_800CCCE8* pBattleEntities = &battleBssUnion.m0_tail.m8E38_entities;

/* Noah's battleMechas; [31] (the environment) also has its own name. */
sLoadedMechas* battleMechas[32] = { 0 };

/* name = (char*)&block + offset. Emitted from a function only because
 * top-level asm takes no operands. */
#define ALIAS_AT(name, block, offset) \
    __asm__(".globl " #name "\n.set " #name ", " #block " + %c0" : : "i"(offset))
#define HEAD(member) __builtin_offsetof(BattleBss_800C3EB0, member)
#define ENTITIES HEAD(m8E38_entities)
/* A PS1 offset into BattleBss_800CCCE8: the same up to m5F20_levelUpData (a
 * pointer, 8 bytes on the PC), 4 more after it. */
#define ENT(ps1Offset) \
    (ENTITIES + ((ps1Offset) < 0x5F24 ? (ps1Offset) \
                                      : __builtin_offsetof(BattleBss_800CCCE8, m5F24_gearTurn) + (ps1Offset) - 0x5F24))

static void __attribute__((used)) battleBssAliases(void) {
    ALIAS_AT(battleVisualEntities, battleBssBlock, HEAD(m4_visualEntities));
    ALIAS_AT(battleCurrentDamages, battleBssBlock, HEAD(m138_currentDamages));
    ALIAS_AT(performAttackSub3_var0, battleBssBlock, HEAD(mA38_performAttackSub3_var0));
    ALIAS_AT(battleRunningVar1, battleBssBlock, HEAD(mA3A_battleRunningVar1));
    ALIAS_AT(D_800C48EA, battleBssBlock, HEAD(mA3A_battleRunningVar1)); /* main's name */
    ALIAS_AT(battleTickMain_var2, battleBssBlock, HEAD(mA72_currentEntity));
    ALIAS_AT(drawBattleMode1Disabled, battleBssBlock, HEAD(mA7A));
    ALIAS_AT(battleRenderStructs, battleBssBlock, HEAD(mB70_renderStructs));
    ALIAS_AT(D_800C4A20, battleBssBlock, HEAD(mB70_renderStructs)); /* main's name */
    ALIAS_AT(pCurrentBattleRenderStruct, battleBssBlock, HEAD(m8C50_currentRenderStruct));
    /* BattleBss_800CCB04 view: its first members line up on the PC too. */
    ALIAS_AT(D_800CCB04, battleBssBlock, HEAD(m8C54_currentOT));
    ALIAS_AT(battleOddOrEven, battleBssBlock, HEAD(m8C84_oddOrEven));
    ALIAS_AT(D_800CCB3C, battleBssBlock, HEAD(m8C8C_spriteActorCores));
    ALIAS_AT(battleTimeEnabled, battleBssBlock, HEAD(m8DA8_timeEnabled));
    ALIAS_AT(battle800ccc5c, battleBssBlock, HEAD(m8DAC_frameRate));

    /* BattleBss_800CCCE8 and the names inside it (PS1 address - 0x800CCCE8). */
    ALIAS_AT(battleEntities, battleBssBlock, ENT(0));
    ALIAS_AT(D_800CCD1E, battleBssBlock, ENT(0x36));
    ALIAS_AT(partyBattleStats, battleBssBlock, ENT(0x1058));
    ALIAS_AT(mechaBattleStats, battleBssBlock, ENT(0x2228));
    ALIAS_AT(D_800D2204, battleBssBlock, ENT(0x551C));
    ALIAS_AT(D_800D2C08, battleBssBlock, ENT(0x5F20)); /* m5F20_levelUpData (battle_result) */
    ALIAS_AT(D_800D2C0A, battleBssBlock, ENT(0x5F24) - 2); /* BattleBss_800D2C0A: the fuel costs 1-based */
    ALIAS_AT(D_800D2C31, battleBssBlock, ENT(0x5F49));
    ALIAS_AT(D_800D2C34, battleBssBlock, ENT(0x5F4C));
    ALIAS_AT(damageDonePerAttack, battleBssBlock, ENT(0x5F6C));
    ALIAS_AT(initBattleAttackStatusArray0, battleBssBlock, ENT(0x5FA0));
    ALIAS_AT(entitiesHitInCurrentAttackBF, battleBssBlock, ENT(0x5FAC));
    ALIAS_AT(performAttack_type, battleBssBlock, ENT(0x5FB0));
    ALIAS_AT(battleEffectBF, battleBssBlock, ENT(0x5FB6));
    ALIAS_AT(D_800D2CA4, battleBssBlock, ENT(0x5FBC));
    ALIAS_AT(battleGetSlotStatusSub_current28Index, battleBssBlock, ENT(0x5FC2));
    ALIAS_AT(battleCharacters, battleBssBlock, ENT(0x603C));
}

/* The PC aliases must land on the members the PS1 names. */
typedef char battleBssCheck1[ENT(0x5F24) == ENTITIES + __builtin_offsetof(BattleBss_800CCCE8, m5F24_gearTurn) ? 1 : -1];
typedef char battleBssCheck2[ENT(0x603C) == ENTITIES + __builtin_offsetof(BattleBss_800CCCE8, m603C_battleCharacters) ? 1 : -1];
typedef char battleBssCheck3[ENT(0x5FC2) == ENTITIES + __builtin_offsetof(BattleBss_800CCCE8, m5FC2_battleGetSlotStatusSub_current28Index) ? 1 : -1];
typedef char battleBssCheck4[HEAD(m8C84_oddOrEven) - HEAD(m8C54_currentOT) == __builtin_offsetof(BattleBss_800CCB04, m30_battleOddOrEven) ? 1 : -1];
typedef char battleBssCheck5[HEAD(m9E08_shownExperience) == ENT(0xFD0) ? 1 : -1];
