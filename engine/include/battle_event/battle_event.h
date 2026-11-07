#ifndef BATTLE_EVENT_BATTLE_EVENT_H
#define BATTLE_EVENT_BATTLE_EVENT_H

/*
 * Battle event overlay (disc 20/01, loaded at 0x801E5000 by the battle
 * overlay): the scripted-battle bytecode interpreter. Noah:
 * battle/battleEvent/battleEvent.cpp/.h. Its state, Noah's
 * sBattleRunningVar0 (battleEventVar0), is in battle/battle.h.
 */

#include "common.h"
#include "psyq.h"
#include "battle/battle.h"

/* Noah: sBattleEventVar2 as loaded from disc (the decompressed event data):
 * m40 script entity count, then m40 x 8 entry points, then the bytecode. */
typedef struct {
    /* 0x00 */ u8 m0[0x40];
    /* 0x40 */ u32 m40_count;
    /* 0x44 */ s16 m44_scriptBytecodeEntryPoints[1][8];
} sBattleEventData;

/* battleOddOrEven (0x800CCB34, BattleBss_800CCB04.m30 in battle/battle.h)
 * seen as a one-field aggregate: retail reloads it after every struct store,
 * which GCC 2.6 only does for a MEM_IN_STRUCT access. A plain D_800CCB04
 * aggregate would do too, but JP's symbol file only names battleOddOrEven.
 * The PC sees the plain s32 (battle/battle.h). */
typedef struct {
    /* 0x0 */ s32 m0_battleOddOrEven;
} BattleOddOrEven;
#define BATTLE_ODD_OR_EVEN battleOddOrEven

/* battleVar0 (battle/battle.h sBattleVar0) fields used here. JP's
 * sBattleVar0 differs: m27C8 sits at 0x24A8 (0x320 lower) and m835C at
 * 0x867C (0x320 higher); JP's battle layout is not described yet, so JP
 * reaches them through this view. */
#define BATTLE_VAR0_M27C8(p) ((p)->m27C8_selectionCursor)
#define BATTLE_VAR0_M835C(p) ((p)->m835C_apFuelLabels)

/* battleEntities[i].m0_base.m36 (battle/battle.h sBattleEntity, 0x170 bytes,
 * battleEntities + 0x36) as the array retail indexes; JP's symbol file names
 * D_800CCD1E but not battleEntities. */
typedef struct {
    /* 0x000 */ u16 m0_m36;
    /* 0x002 */ u8 m2[0x170 - 2];
} sBattleEntityFrom36;
extern sBattleEntityFrom36 D_800CCD1E[11] PORT_ALIAS;

/* currentBattleConfig.m3_eventDataIndex (Noah; main .bss 0x8006F9DC + 3).
 * Retail keeps its address in a register, as for a struct member. */
typedef struct {
    /* 0x0 */ u8 m0_eventDataIndex;
} BattleConfigM3;
extern BattleConfigM3 D_8006F9DF;

extern sBattleEventData* battleEventVar2;

/* Event sprite actors (be_801E93E8.c; JP be_801E9420.c). The opcode handlers
 * (be_801E5160.c) declare the others with the types retail passes, so only
 * the PC sees those. */
void battleEventSwapSpriteCommit(s32 dst, s32 src);
void battleEventSwapSpritePrepare(s32 slot, s32 value);
void battleEventHideSpriteEntity(s32 slot);
void battleEventSetSpriteCameraAnim(s32 slot, s32 anim);
void battleEventSetSpriteFlip(s32 slot);
void battleEventShowSpriteEntity(s32 slot);
void battleEventStartCharacterSpriteLoad(s32 slot, u16 src);
void battleEventStartSpriteMove(s16 slot, s16 x, s16 y, s16 z);
void battleEventStartSpriteMoveAlt(s16 slot, s16 x, s16 y, s16 z);

#endif /* BATTLE_EVENT_BATTLE_EVENT_H */
