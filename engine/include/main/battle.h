#ifndef MAIN_BATTLE_H
#define MAIN_BATTLE_H

/* Battle state kept in the main executable. */

#include "common.h"
#include "psyq.h"

/* Noah: sBattleRenderStruct; battleRenderStructs[2] at 0x800C4A20, size 0x4070 */
typedef struct {
    DRAWENV m0_drawEnv;
    DISPENV m5C_dispEnv;
    OTag m70_ot[0x1000];
} BattleRenderStruct;

/* Noah: sBattleVisualEntity (size 0x1C); battleVisualEntities[11] = battleVisualEntities */
typedef struct {
    /* 0x00 */ u8 m0_positionSlot;
    /* 0x01 */ u8 m1_indexInSlot;
    /* 0x02 */ u8 m2_characterId;
    /* 0x03 */ u8 m3_hidden;
    /* 0x04 */ u8 m4_isGear;
    /* 0x05 */ u8 m5;
    /* 0x06 */ u8 m6_direction;       /* Noah: s8 */
    /* 0x07 */ u8 m7;
    /* 0x08 */ u8 m8[2];
    /* 0x0A */ u16 mA_X;              /* Noah: s16; retail reads lhu */
    /* 0x0C */ u16 mC_Z;              /* Noah: s16 */
    /* 0x0E */ u16 mE_Y;
    /* 0x10 */ u8 m10[0x1C - 0x10];
} sBattleVisualEntity;

/* In the battle block at 0x800C3EB0 (battle.h D_800C3EB0.m4_visualEntities);
 * main's sprite code reads them too. */
extern sBattleVisualEntity battleVisualEntities[11] PORT_ALIAS;

/* The battle config file from its data + 4 (battle_env.c loads it; the
 * layout is in include/battle/battle.h). */
struct sBattleMechaInitData;
extern struct sBattleMechaInitData* battleEnvMechaInitData;

#endif /* MAIN_BATTLE_H */
