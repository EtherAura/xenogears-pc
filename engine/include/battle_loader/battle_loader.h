#ifndef OVL_0C_04_H
#define OVL_0C_04_H

/*
 * Battle loader overlay (disc 0C/04, loaded at 0x801E4000): Noah
 * battle/battleLoader.cpp (bld_801E4048.c), battleSpriteLoader.cpp /
 * battleMechaLoadingTask.cpp / battleStartEffect.cpp (bld_801E62E0.c,
 * bld_801E91E8.c). Offset-tagged; Noah names where it has them.
 */

#include "common.h"
#include "main/task.h"
#include "main/filesystem.h"
#include "battle/battle.h"

/* Noah: sBattleConfig (battleConfig.h, size 0x20); currentBattleConfig = currentBattleConfig (main .bss) */
typedef struct {
    /* 0x00 */ u8 m0_battleId;                    /* battle id: files m0 * 2 + 2 / + 3 in dir 0xC (Noah: s8) */
    /* 0x01 */ u8 m1_flags;
    /* 0x02 */ s8 m2_arenaId;
    /* 0x03 */ s8 m3_eventDataIndex;
    /* 0x04 */ u8 m4[12];               /* Noah: s8; party slot / monster ids, 0x80: gear */
    /* 0x10 */ u8 m10_monsterFlags[8];               /* Noah: s8 */
    /* 0x18 */ u8 m18_monsterPositions[8];               /* Noah: s8 */
} sBattleConfig;

extern sBattleConfig currentBattleConfig;

/* One entity of the battle sprite config file (Noah: sBattleSpriteConfigs::m8) */
typedef struct {
    /* 0x0 */ s32 m0_spriteControlOffset;
    /* 0x4 */ u32 m4_spriteDataOffset;  /* < 8: sprite sheet already uploaded by entity [m4];
                                         * mecha entries: owner entity index */
    /* 0x8 */ u8 m8_isMecha;
    /* 0x9 */ u8 m9[2];
    /* 0xB */ u8 mB_vramParam;
} sBattleSpriteConfigEntry;

/* Noah: sBattleSpriteConfigs (battle config file 3) */
typedef struct {
    /* 0x0 */ u8 m0_numEntities;
    /* 0x1 */ u8 m1_vramColumn;                    /* first sprite sheet VRAM column (x 0x40 + 0x140) */
    /* 0x2 */ u8 m2[2];
    /* 0x4 */ s32 m4_size;
    /* 0x8 */ sBattleSpriteConfigEntry m8_entries[1]; /* [m0_numEntities] */
} sBattleSpriteConfigs;

/* Noah: sBattleSpriteLoaderTask (battleSpriteLoader.cpp); allocateUpdateTask(NULL, 0x78) */
typedef struct {
    /* 0x00 */ TaskHeader m0_task;
    /* 0x1C */ u8 m1C[4];
    /* 0x20 */ void* m20_spriteConfig;                       /* sprite config file (Noah: sBattleSpriteConfigs*) */
    /* 0x24 */ void* m24_spriteBundle;                       /* sprite animation bundle (file 2) */
    /* 0x28 */ void* m28_texture;                       /* texture (file 1) */
    /* 0x2C */ void* m2C_seq;                   /* sound effect sequence (file 3) */
    /* 0x30 */ LoadingBatchCommand m30_loadingCommands[4];
    /* 0x50 */ LoadingBatchCommand m50_loadingCommandsPhase2[4];
    /* 0x70 */ u8 m70[0x90 - 0x70];
    /* 0x90 */ s32 m90_frameToDeleteCount;
} sBattleSpriteLoaderTask;

/* Battle start "tile" transition (Noah: battleLoaderUpdateMode1's tile
 * effect, MissingCode there): the battle start effect's triangles on a
 * [2][7][10] grid. A task pair, allocateHeap(0x440C). */
typedef struct {
    /* 0x00 */ TaskHeader m0_updateTask;
    /* 0x1C */ TaskHeader m1C_drawTask;
    /* 0x38 */ s32 m38_frameCounter;
    /* 0x3C */ sBattleStartEffectSub m3C_tiles[2][7][10];
} sBattleTileEffect; /* 0x440C */

/* One triangle of the "wave" battle transition (createBattleStartEffect; twin code in
 * battle_fx_screen_ripple at 0x801FC11C-0x801FC470) */
typedef struct {
    /* 0x00 */ u8 m0[4];
    /* 0x04 */ POLY_GT3 m4_poly[2];
    /* 0x54 */ SVECTOR m54_vertices[3];
    /* 0x6C */ s32 m6C_distance[3];       /* vertex distance from the centre (sin phase) */
    /* 0x78 */ u8 m78[4];
} sBattleWaveTriangle; /* 0x7C */

/* Vertex l of triangle t: retail addresses it by its byte offset, the PC by
 * the field (POLY_GT3 grows on the PC). */
#define WAVE_VERTEX(t, l) (&(t)->m54_vertices[l])

typedef struct {
    /* 0x00 */ TaskHeader m0_updateTask;
    /* 0x1C */ TaskHeader m1C_drawTask;
    /* 0x38 */ s32 m38_brightness;
    /* 0x3C */ s32 m3C_phase;
    /* 0x40 */ s32 m40_amplitude;
    /* 0x44 */ s32 m44_frameCounter;
    /* 0x48 */ s32 m48_speed;
    /* 0x4C */ VECTOR m4C_position;
    /* 0x5C */ SVECTOR m5C_rotation;
    /* 0x64 */ sBattleWaveTriangle m64_triangles[2][14][20];
} sBattleWaveEffect; /* 0x10FA4 */

/* One background layer of the battle environment config (0x28) */
typedef struct {
    /* 0x00 */ VECTOR m0_horizonPos;               /* -> createBackgroundPoly's vec */
    /* 0x10 */ u16 m10_vramX;
    /* 0x12 */ u16 m12_vramY;
    /* 0x14 */ u16 m14_width;
    /* 0x16 */ u16 m16_height;
    /* 0x18 */ u16 m18_type;            /* 1, 2: background poly, 3: horizon, 5: backdrop colour, 7: VRAM scroll */
    /* 0x1A */ u16 m1A_clutX;
    /* 0x1C */ u16 m1C_clutY;
    /* 0x1E */ u16 m1E;
    /* 0x20 */ u16 m20;
    /* 0x22 */ u16 m22_groundBandScale;
    /* 0x24 */ u16 m24_zoomDivisor;
    /* 0x26 */ u16 m26_zoomStartDistance;
} sBattleEnvLayer;

/* The environment part of the battle config file, at sBattleMechaInitData + 0x340 */
typedef struct {
    /* 0x000 */ u8 m0_terrainAgility[4];               /* -> battleEntities.m6028_terrainAgility */
    /* 0x004 */ u8 m4[0x1E - 0x4];      /* battle mecha scale, arena bounds, ... (sBattleMechaInitData) */
    /* 0x01E */ u8 m1E_useBackColor;
    /* 0x01F */ u8 m1F;
    /* 0x020 */ sBattleEnvLayer m20_layers[6];
    /* 0x110 */ s16 m110_horizonGeomScreen;
    /* 0x112 */ s16 m112_horizonTiltScale;
    /* 0x114 */ s16 m114_horizonScrollU;
    /* 0x116 */ s16 m116_horizonScrollV;
    /* 0x118 */ CVECTOR m118_colors[3];
    /* 0x124 */ s16 m124_light[3];
    /* 0x12A */ u8 m12A[2];
    /* 0x12C */ s16 m12C_color[3];
    /* 0x132 */ u8 m132[2];
    /* 0x134 */ u8 m134_backR;
    /* 0x135 */ u8 m135_backG;
    /* 0x136 */ u8 m136_backB;
} sBattleEnvConfig;

/* The environment gear's data (a relocated sMechaDataTable1 with one more
 * entry: per-bone initial translations) */
typedef struct {
    /* 0x00 */ s32 m0_count;
    /* 0x04 */ void* m4_textures;
    /* 0x08 */ u8* m8_modelBlocks;
    /* 0x0C */ u8* mC_bones;
    /* 0x10 */ void* m10_gearInfo;
    /* 0x14 */ s16* m14_boneInit;       /* per bone from 1: x, y, z, m52 */
} sBattleEnvMechaData;

#endif /* OVL_0C_04_H */
