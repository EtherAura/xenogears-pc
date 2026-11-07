#ifndef WORLDMAP_WORLDMAP_H
#define WORLDMAP_WORLDMAP_H

/* World map overlay (Noah: NoahLib/worldmap/). Field names are Noah's where
 * Noah has the field (worldmap.h, worldmapExit.h, worldmapFollowLeader.h,
 * worldmapDynamicCollisions.h, ...), offset-tagged. */

#include "common.h"
#include "psyq.h"
#include "main/model.h"
#include "main/sprite.h"

/* ---- PSY-Q SDK types and functions not in psyq.h yet ---------------------- */

/* libgpu DR_TWIN (texture window) */
typedef struct {
    OTag tag;
    u32 code[2];
} DR_TWIN;

void SetTexWindow(DR_TWIN* p, RECT* tw);
void SetDrawMove(DR_MOVE* p, RECT* rect, s32 x, s32 y);
MATRIX* RotMatrixY(s32 r, MATRIX* m);
MATRIX* RotMatrixZYX(SVECTOR* r, MATRIX* m);

/* ---- tasks ------------------------------------------------------------------ */

/* Noah: sWorldmapStateEntry (worldmap.h); one world map task/entity slot.
 * gWorldmapState (gWorldmapState) points at 64 of them. The fields from 0x50 on
 * are reused per task type. */
typedef struct sWorldmapStateEntry {
    /* 0x00 */ s16 m0_state;
    /* 0x02 */ s16 m2_waitTimer;
    /* 0x04 */ s16 m4_request;
    /* 0x06 */ s16 m6_targetEntity;
    /* 0x08 */ s32 m8;           /* not in Noah */
    /* 0x0C */ s32 mC;
    /* 0x10 */ s32 m10;
    /* 0x14 */ s32 m14;
    /* 0x18 */ s32 (*m18_init)(s32 index);
    /* 0x1C */ s32 (*m1C_update)(s32 index);
    /* 0x20 */ s16 m20_subState;
    /* 0x22 */ s16 m22_timer;
    /* 0x24 */ s16 m24_hidden;
    /* 0x26 */ s16 m26;
    /* 0x28 */ VECTOR m28_position;
    /* 0x38 */ VECTOR m38_step;
    /* 0x48 */ s16 m48_direction;
    /* 0x4A */ s16 m4A_stepScale;
    /* 0x4C */ SpriteActorCore* m4C_sprite; /* Noah: sSpriteActor* (createSpriteActor's result + 0) */
    /* 0x50 */ sptr m50;         /* script task: sWorldmapScriptIns* */
    /* 0x54 */ sptr m54;         /* director task: u16* state list */
    /* 0x58 */ sptr m58;         /* gears: how far back in the follow-leader data; director task: u16* timer list */
    /* 0x5C */ s32 m5C;
    /* 0x60 */ s32 m60;
    /* 0x64 */ sptr m64;         /* camera task: s16* pitch table (Noah m64_array); Yggdrasil: state */
    /* 0x68 */ sptr m68;         /* camera task: s16* (Noah m68_array) */
    /* 0x6C */ s32 m6C;
    /* 0x70 */ s32 m70;
    /* 0x74 */ s32 m74;          /* Yggdrasil: number of gears on board */
    /* 0x78 */ s32 m78;
    /* 0x7C */ s32 m7C;
} sWorldmapStateEntry; /* 0x80 */

/* Noah: sWorldmapState */
typedef struct sWorldmapState {
    /* 0x0000 */ sWorldmapStateEntry m0_entries[64];
} sWorldmapState;

/* Noah: sWorldTaskDefinition (worldmapTask.h); task tables end with a NULL m0_init. */
typedef struct {
    /* 0x0 */ s32 (*m0_init)(s32 index);
    /* 0x4 */ s32 (*m4_update)(s32 index);
} sWorldTaskDefinition;

/* Noah: sWorldmapModes (worldmap.cpp); table at 0x8009A058. */
typedef struct {
    /* 0x0 */ void (*m0_init)(void);
    /* 0x4 */ void (*m4_update)(void);
    /* 0x8 */ void (*m8_free)(void);
} sWorldmapModes;

/* Noah: sWorldmapConfig (worldmap.cpp); initWorldMap table at 0x8009B584. */
typedef struct {
    /* 0x0 */ s16 m0_baseFileId;
    /* 0x2 */ s16 m2_sizeX;
    /* 0x4 */ s16 m4_sizeZ;
    /* 0x6 */ s16 m6_exitMode;
} sWorldmapConfig;

/* World map event script: one instruction is an opcode (u16) and up to three
 * s16 arguments; handlers return the instruction length in halfwords (0 = yield). */
typedef struct {
    /* 0x0 */ u16 m0_op;
    /* 0x2 */ s16 m2_arg1;
    /* 0x4 */ s16 m4_arg2;
    /* 0x6 */ s16 m6_arg3;
} sWorldmapScriptIns;
typedef s32 (*WorldmapScriptOp)(sWorldmapStateEntry* e, s32 a1, s32 a2, s32 a3);

/* ---- rendering ---------------------------------------------------------------- */

/* Noah: sWorldmapRenderingStruct (worldmap.h); two at 0x8009BBC8,
 * pCurrentWorldmapRenderingStruct = pCurrentWorldmapRenderingStruct. */
typedef struct sWorldmapRenderingStruct {
    /* 0x00 */ DRAWENV m0_DrawEnv;
    /* 0x5C */ DISPENV m5C_DispEnv;
    /* 0x70 */ OTag* m70_OT;          /* 0x400 entries */
    /* 0x74 */ POLY_FT3* m74_polyFT3; /* 0x800 entries */
} sWorldmapRenderingStruct; /* 0x78 */

/* The 0x800 POLY_FT3 of a sWorldmapRenderingStruct (m74_polyFT3), copied as
 * one block (retail copies 0x10000 bytes in a 16-byte loop). */
typedef struct {
    POLY_FT3 m[0x800];
} sWorldmapPolyFT3Array;

/* Noah: sWorldmapCameraState (worldmap.h); worldmapCameraVector = worldmapCameraVector.
 * Also the lookAt parameters of setupWorldmapMatrix2/setupWorldmapMatrix1. */
typedef struct sWorldmapCameraState {
    /* 0x00 */ SVECTOR m0_eye;   /* eye */
    /* 0x08 */ SVECTOR m8_target;   /* look-at target */
    /* 0x10 */ VECTOR m10_up;   /* up vector */
} sWorldmapCameraState; /* 0x20 */

/* Camera smoothing work area: full delta and 1/8 step. */
typedef struct {
    /* 0x00 */ VECTOR m0_delta;
    /* 0x10 */ VECTOR m10_step;
} sWorldmapCameraDelta;

/* ---- models --------------------------------------------------------------------- */

/* Collision mesh of a world map model (sWorldmapModel.m44). */
typedef struct sWorldmapCollisionTri {
    /* 0x0 */ s16 m0_vertices[3];
    /* 0x6 */ s16 m6_neighbors[3];
    /* 0xC */ u16 mC_type;
} sWorldmapCollisionTri; /* 0xE */

typedef struct sWorldmapCollisionMesh {
    /* 0x0 */ s32 m0_numTriangles;
    /* 0x4 */ u32 m4_vertices; /* stays the offset from the mesh, as loaded */
    /* 0x8 */ sWorldmapCollisionTri m8_triangles[1];
} sWorldmapCollisionMesh;

/* The PS1 relocates m4_vertices in place (worldmapInitAllModels); the PC
 * keeps the loaded offset and adds it where the vertices are read. */
#define WM_MESH_VERTICES(mesh) ((SVECTOR*)((u8*)(mesh) + (mesh)->m4_vertices))

/* One model placement in the models config file (Noah: pWorldmapModelsConfig,
 * a u16 count followed by these). */
typedef struct sWorldmapModelConfig {
    /* 0x0 */ s16 m0_blockIndex;
    /* 0x2 */ s16 m2_flags;
    /* 0x4 */ s16 m4_x;
    /* 0x6 */ s16 m6_y;
    /* 0x8 */ s16 m8_z;
    /* 0xA */ s16 mA_rotX;
    /* 0xC */ s16 mC_rotY;
    /* 0xE */ s16 mE_rotZ;
} sWorldmapModelConfig;

/* Noah: sWorldmapModel (worldmap.h); worldmapModels = worldmapModels. */
typedef struct sWorldmapModel {
    /* 0x00 */ s16 m0_hidden;          /* Noah: u16 (worldmapInitAllModels/renderWorldmapModels need lh) */
    /* 0x02 */ s16 m2_modelBlockIndex; /* Noah: u16 */
    /* 0x04 */ s16 m4_flags;
    /* 0x06 */ s16 m6;
    /* 0x08 */ VECTOR m8_position;         /* position */
    /* 0x18 */ SVECTOR m18_rotation;
    /* 0x20 */ MATRIX m20_rotationMatrix;
    /* 0x40 */ ModelBlock* m40_modelBlock;
    /* 0x44 */ sWorldmapCollisionMesh* m44_collisionMesh;
    /* 0x48 */ u8* m48_primBuffers[2];        /* per-frame prim buffers (POLY_FT4 ...) */
    /* 0x50 */ struct sWorldmapModel* m50_parentModel;
} sWorldmapModel; /* 0x54 */

/* ---- exits, collisions, follow-leader ------------------------------------------ */

/* Noah: sWorldmapExitDef (worldmapExit.h); tables at *worldmapExitsDefs[0..3] end
 * with m8 == -1. */
typedef struct sWorldmapExitDef {
    /* 0x0 */ s16 m0_X;
    /* 0x2 */ s16 m2_Y;
    /* 0x4 */ s16 m4_width;
    /* 0x6 */ s16 m6_height;
    /* 0x8 */ s16 m8_destinationField;
    /* 0xA */ s16 mA_returnWorldmapMode;
    /* 0xC */ s16 mC_newWorldmapMode;
    /* 0xE */ s16 mE_type;
} sWorldmapExitDef; /* 0x10 */

/* Noah: sWorldmapDynamicCollision (worldmapDynamicCollisions.h);
 * 32 of them at worldmapDynamicCollisions, count currentWorldmapDynamicCollisionSlot. */
typedef struct {
    /* 0x00 */ s16 m0_entityIndex;
    /* 0x04 */ s32 m4_position[3];
    /* 0x10 */ s32 m10_height;
    /* 0x14 */ s16 m14_radius;           /* Noah: s32; s16 here (addWorldmapDynamicCollisions, processWorldmapDynamicCollisions need sh/lh) */
} sWorldmapDynamicCollision; /* 0x18 */

/* Noah: sWorldmapFollowLeader (worldmapFollowLeader.h);
 * worldmapFollowLeaderData = worldmapFollowLeaderData[32], index worldmapFollowLeaderDataIndex. */
typedef struct {
    /* 0x00 */ VECTOR m0_position;
    /* 0x10 */ s16 m10_direction;
    /* 0x12 */ s16 m12;
} sWorldmapFollowLeader; /* 0x14 */

/* worldmapFollowLeaderData as one block (saved/restored while a menu runs). */
typedef struct {
    sWorldmapFollowLeader m[32];
} sWorldmapFollowLeaderBlock; /* 0x280 */

/* ---- particles ---------------------------------------------------------------- */

/* An SVECTOR as spawnWorldmapParticlesDirected moves it: vx/vy as one word, then vz. */
typedef struct sWorldmapSVecXYZ {
    /* 0x00 */ s32 m0_vxvy;
    /* 0x04 */ s16 m4_vz;
} sWorldmapSVecXYZ;

/* A world map particle emitter, 0x54 bytes: 0x200 of them at worldmapFile1Buffer_18,
 * in groups of 8 (group * 8 + i). Not in Noah (worldmapParticles.cpp is a stub). */
typedef struct sWorldmapParticle {
    /* 0x00 */ s32 m0_timerInit;      /* copied to m4 when the group is (re)started */
    /* 0x04 */ s32 m4_timer;          /* low s16: delay, high s16: repeat count */
    /* 0x08 */ s16 m8_maxParticles;
    /* 0x0A */ s16 mA_numParticles;
    /* 0x0C */ s16 mC_life;           /* particle m4/m6 template, copied as one word */
    /* 0x0E */ s16 mE_active;
    /* 0x10 */ s16 m10_period;
    /* 0x12 */ s16 m12_periodTimer;
    /* 0x14 */ SVECTOR m14_position;
    /* 0x1C */ SVECTOR m1C_rotation;
    /* 0x24 */ SVECTOR m24_spawnDir;
    /* 0x2C */ SVECTOR m2C_targetDir;
    /* 0x34 */ s32 m34_speed;
    /* 0x38 */ SVECTOR m38_accel;
    /* 0x40 */ u16 m40_spawnRadius;
    /* 0x42 */ u16 m42_targetRadius;
    /* 0x44 */ s32 m44_scale;               /* copied to particle m38 */
    /* 0x48 */ s32 m48_scaleStep;               /* copied to particle m3C */
    /* 0x4C */ union {
                   s32 m0_raw;
                   struct {
                       u8 m0_r;
                       u8 m1_g;
                       u8 m2_b;
                       u8 m3_flags;   /* 0x80 active, 0x40/0x20 flat, 0x10 timed, 8/4 fixed radius, 3 blend */
                   } m0_c;
               } m4C_color;
    /* 0x50 */ s32 m50_colorStep;
} sWorldmapParticle; /* 0x54 */

/* A live particle, 0x4C bytes; 0x100 of them allocated into worldmapParticleInstances. */
typedef struct sWorldmapParticle2 {
    /* 0x00 */ s16 m0_emitter;
    /* 0x02 */ s16 m2_angle;
    /* 0x04 */ s16 m4_life;       /* 0x04..0x07 also read/cleared as one word */
    /* 0x06 */ s16 m6_active;
    /* 0x08 */ VECTOR m8_position;
    /* 0x18 */ VECTOR m18_velocity;
    /* 0x28 */ VECTOR m28_accel;
    /* 0x38 */ u16 m38_scaleX;
    /* 0x3A */ u16 m3A_scaleY;
    /* 0x3C */ u16 m3C_scaleXStep;
    /* 0x3E */ u16 m3E_scaleYStep;
    /* 0x40 */ s32 m40_color;     /* r,g,b bytes + code */
    /* 0x44 */ s32 m44_colorStep; /* signed r,g,b steps (0x47: flags, bit 0 = rotate) */
    /* 0x48 */ u16 m48_tpage;
    /* 0x4A */ u16 m4A;
} sWorldmapParticle2; /* 0x4C */

/* World map file 1 table at worldmapFile1Buffer_14 (0x100 entries): offsets from the
 * table start, relocated in place by initWorldmapBillboards (0 stays 0). */
typedef struct {
    /* 0x0 */ s32 m0_offset;
    /* 0x4 */ s32 m4_count;
} sWorldmapOffsetEntry;

/* ---- world streaming (worldmapWorldStreaming.cpp) ------------------------------ */

/* Noah: sWorldmapCellFromCDSub: one queued CD read; the lists are sorted by
 * sector (unsigned compare) and end with a 0 sector. */
typedef struct {
    /* 0x0 */ u32 m0_sector;
    /* 0x4 */ s32 m4_size;
    /* 0x8 */ u8* m8_buffer;
} sWorldmapCellFromCDSub; /* 0xC */

/* Noah: sWorldmapCellFromCD (0x10 of them at *0x8009BE08) */
typedef struct {
    /* 0x000 */ sWorldmapCellFromCDSub m0_reads[88];
} sWorldmapCellFromCD; /* 0x420 */

/* Noah: sWorldmapCellFromPC entry (PC/DTL build): PCopen name, lseek
 * offset, read size and buffer. */
typedef struct {
    /* 0x0 */ char* m0_filename;
    /* 0x4 */ u32 m4_offset;
    /* 0x8 */ s32 m8_size;
    /* 0xC */ u8* mC_buffer;
} sWorldmapCellFromPCSub; /* 0x10 */

/* Noah: sWorldmapCellFromPC (0x10 of them at *0x8009D3C0) */
typedef struct {
    /* 0x000 */ sWorldmapCellFromPCSub m0_reads[88];
} sWorldmapCellFromPC; /* 0x580 */

/* Noah: worldmapGrid / worldmapGrid2 (std::array<u16, 81>): the 9x9 chunk
 * ids around the player; copied as a whole (2-byte aligned block move). */
typedef struct {
    s16 m[81];
} sWorldmapGrid;

/* ---- files ---------------------------------------------------------------------- */

/* World map file 1 (Noah: worldmapFile1Buffer): offsets from its start,
 * relocated by finalizeWorldMapFileLoading. */
typedef struct {
    /* 0x00 */ s32 m0;
    /* 0x04 */ s32 m4_locationsOffset;    /* -> sWorldmapFile1Sub4 */
    /* 0x08 */ s32 m8_modelsOffset;    /* model files */
    /* 0x0C */ s32 mC_collisionMeshesOffset;
    /* 0x10 */ s32 m10_modelsConfigOffset;   /* models config */
    /* 0x14 */ s32 m14_billboardsOffset;
    /* 0x18 */ s32 m18_particlesOffset;
    /* 0x1C */ s32 m1C_textOffset;
    /* 0x20 */ s32 m20_texAnims1Offset;
    /* 0x24 */ s32 m24_texAnims2Offset;
    /* 0x28 */ s32 m28;
    /* 0x2C */ s32 m2C_encounterTableOffsets[16];
} sWorldmapFile1Header;

typedef struct {
    /* 0x0 */ s32 m0_initialPositionsOffset;     /* initial positions (8 bytes each) */
    /* 0x4 */ s32 m4_exitGroupsOffset;     /* exit groups (4 offsets) */
} sWorldmapFile1Sub4;

/* Sound bank/sequence file loaded to fieldSeq (Noah: fieldSeq); only the
 * header field the world map reads (sound ids are (m14 << 16) | n). */
typedef struct {
    /* 0x00 */ u8 m0[0x14];
    /* 0x14 */ u16 m14_id;
} sWorldmapSeqHeader;

/* ---- ground marks, animated textures, encounters, menu backup -------------------- */

/* Ground track mark (ring of 16, worldmapTrackMarks). */
typedef struct {
    /* 0x0 */ s16 m0_x;
    /* 0x2 */ s16 m2_type;
    /* 0x4 */ s16 m4_z;
    /* 0x6 */ s16 m6;
} sWorldmapTrackMark;

/* Animated texture (VRAM frame upload) definition and runtime state. */
typedef struct {
    /* 0x0 */ RECT m0_rect;
    /* 0x8 */ s32 m8;
    /* 0xC */ s16* mC_frames; /* {pixel offset, duration} pairs; a duration < 0 loops */
} sWorldmapTexAnimDef;

typedef struct {
    /* 0x0 */ u8* m0_pixels;
    /* 0x4 */ sWorldmapTexAnimDef* m4_def;
    /* 0x8 */ s16 m8_frame;
    /* 0xA */ s16 mA_timer;
} sWorldmapTexAnim;

/* Random-encounter countdowns (worldmapEncounterCountdowns), copied as one block. */
typedef struct {
    s16 m[16];
} sWorldmapEncounterTimers;

/* Battle setup block copied to main's 0x800658DC before an encounter. */
typedef struct {
    u8 m[0x200];
} sWorldmapBattleSetup;

/* Encounter table (world map file data): a 0x200-byte battle setup, then 16
 * group weights per scenario range. */
typedef struct {
    /* 0x000 */ sWorldmapBattleSetup m0_battleSetup;
    /* 0x200 */ u8 m200_weights[1][16];
} sWorldmapEncounterTable;

/* World map state saved in main's memory while a menu runs (0x8005A4E4). */
typedef struct {
    /* 0x0000 */ sWorldmapState m0_tasks;
    /* 0x2000 */ VECTOR m2000_position;
    /* 0x2010 */ s32 m2010_direction;
    /* 0x2014 */ s32 m2014_encounterPeriod;
    /* 0x2018 */ s32 m2018_encounterCount;
    /* 0x201C */ s32 m201C_encounterTimer;
    /* 0x2020 */ sWorldmapEncounterTimers m2020_encounters;
    /* 0x2040 */ sWorldmapFollowLeaderBlock m2040_followLeader;
    /* 0x22C0 */ s32 m22C0_followLeaderIndex;
    /* 0x22C4 */ SVECTOR m22C4_rotation;
    /* 0x22CC */ s32 m22CC_cameraDistance;
    /* 0x22D0 */ s32 m22D0_geometryOffsetY;
    /* 0x22D4 */ VECTOR m22D4_gridInputPosition;
    /* 0x22E4 */ SVECTOR m22E4_gridPosition;
    /* 0x22EC */ VECTOR m22EC_cameraTarget;
} sWorldmapMenuBackup;

/* ---- scratchpad ------------------------------------------------------------------ */

/* Scratchpad (0x1F800000) as the world map code uses it. Noah names the
 * pieces after their addresses (VECTOR_1f800000, SVECTOR_1f8000a0,
 * DAT_1f800288, ...). Functions that lay it out differently keep a local
 * view in their file. */
typedef struct sWorldmapScratch {
    /* 0x000 */ VECTOR m0;
    /* 0x010 */ VECTOR m10;
    /* 0x020 */ VECTOR m20;
    /* 0x030 */ VECTOR m30;          /* follow-leader reset position */
    /* 0x040 */ VECTOR m40;          /* NormalClip arguments (sxy0, sxy1, sxy2) / grid origin */
    /* 0x050 */ VECTOR m50_edgeDir2;          /* model edge 2 normal (getWorldmapModelTriangleEdgeFlags) */
    /* 0x060 */ VECTOR m60_probePosition;
    /* 0x070 */ VECTOR m70_contactPoint;          /* model contact point (getWorldmapModelTriangleContact) */
    /* 0x080 */ VECTOR m80_contactNormal;          /* model contact normal (getWorldmapModelTriangleContact) */
    /* 0x090 */ VECTOR m90_checkResult;          /* checkWorldmapPosition output */
    /* 0x0A0 */ SVECTOR mA0;         /* particle position; elevation corner heights (.vy) */
    /* 0x0A8 */ SVECTOR mA8;         /* particle rotation */
    /* 0x0B0 */ SVECTOR mB0_quadCorner2;
    /* 0x0B8 */ SVECTOR mB8_quadCorner3;
    /* 0x0C0 */ SVECTOR mC0_quadMidpoint01;
    /* 0x0C8 */ SVECTOR mC8_quadMidpoint02;
    /* 0x0D0 */ SVECTOR mD0_quadMidpoint13;
    /* 0x0D8 */ SVECTOR mD8_quadMidpoint23;
    /* 0x0E0 */ SVECTOR mE0_quadCenter;
    /* 0x0E8 */ u8 mE8[0xF0 - 0xE8];
    /* 0x0F0 */ MATRIX mF0_rotation;
    /* 0x110 */ MATRIX m110;
    /* 0x130 */ MATRIX m130;
    /* 0x150 */ MATRIX m150_directionBasis;
    /* 0x170 */ u8 m170[0x288 - 0x170];
    /* 0x288 */ u16 m288_clut[0x40];
    /* 0x308 */ u16 m308_tpage[8];
    /* 0x318 */ VECTOR m318_groundOrigin;
    /* 0x328 */ SVECTOR m328_cellCorners[4];
    /* 0x348 */ u8 m348[0x350 - 0x348];
    /* 0x350 */ MATRIX m350_groundModelMatrix;
    /* 0x370 */ MATRIX m370_groundMatrix;
} sWorldmapScratch;

/* Functions some PS1 callers declare their own way (unmasked int
 * arguments): the PC sees the definitions' prototypes here, the callers keep
 * their views under #ifndef PLATFORM_PC. */
void addWorldmapDynamicCollisions(s16 arg0, VECTOR* arg1, s16 arg2, s16 arg3); /* w_8008BD1C.c */

#endif /* WORLDMAP_WORLDMAP_H */
