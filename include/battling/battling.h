#ifndef BATTLING_BATTLING_H
#define BATTLING_BATTLING_H

/*
 * Battling overlay (disc 01/0D, the gear-arena mini-game; loaded at
 * 0x8006FAF0 US, 0x8006F180 JP). Noah has no battling module: struct and
 * field names are descriptive guesses, fields are offset-tagged and the
 * offsets are certain wherever a field is named.
 */

#include "common.h"
#include "psyq.h"
#include "main/model.h"

/* ---- libgpu types and macros psyq.h lacks (standard layouts) --------------
 * Each block is guarded: when psyq.h gains the type it defines the matching
 * PSYQ_HAS_* macro and this copy drops out (DR_MOVE, DR_AREA and DR_OFFSET
 * are there now). */

void SetDrawMove(DR_MOVE* p, RECT* rect, s32 x, s32 y);

#ifndef getcode
#define getcode(p) (u8)(((P_TAG*)(p))->code)
#endif
#ifndef setSemiTrans
#define setSemiTrans(p, abe) \
    ((abe) ? setcode(p, getcode(p) | 0x02) : setcode(p, getcode(p) & ~0x02))
#endif
#ifndef setLineF2
#define setLineF2(p) setlen(p, 3), setcode(p, 0x40)
#endif
#ifndef setLineF3
#define setLineF3(p) setlen(p, 5), setcode(p, 0x48), (p)->pad = 0x55555555
#endif
#ifndef setLineF4
#define setLineF4(p) setlen(p, 6), setcode(p, 0x4C), (p)->pad = 0x55555555
#endif
#ifndef setTile
#define setTile(p) setlen(p, 3), setcode(p, 0x60)
#endif
#ifndef setTile1
#define setTile1(p) setlen(p, 2), setcode(p, 0x68)
#endif

/* ---- primitive views ------------------------------------------------------ */

/* Primitives viewed with packed words, as the battling code writes them
 * (xy as one u32, colours/uv as u16/u32). */
typedef struct {
    /* 0x00 */ OTag tag;
    /* 0x04 */ u32 rgbc;
    /* 0x08 */ u32 xy0;
    /* 0x0C */ u16 uv0;
    /* 0x0E */ u16 clut;
    /* 0x10 */ u32 wh;
} BattlingSprtWords; /* SPRT */

typedef struct {
    /* 0x00 */ OTag tag;
    /* 0x04 */ u8 r0, g0, b0, code;
    /* 0x08 */ u32 xy0;
    /* 0x0C */ u16 rg1;
    /* 0x0E */ u8 b1, pad1;
    /* 0x10 */ u32 xy1;
    /* 0x14 */ u16 rg2;
    /* 0x16 */ u8 b2, pad2;
    /* 0x18 */ u32 xy2;
    /* 0x1C */ u16 rg3;
    /* 0x1E */ u8 b3, pad3;
    /* 0x20 */ u32 xy3;
} BattlingPolyG4Words; /* POLY_G4 */

typedef struct {
    /* 0x00 */ OTag tag;
    /* 0x04 */ u32 rgbc;
    /* 0x08 */ u32 xy0;
    /* 0x0C */ u16 uv0;
    /* 0x0E */ u16 clut;
    /* 0x10 */ u32 xy1;
    /* 0x14 */ u16 uv1;
    /* 0x16 */ u16 tpage;
    /* 0x18 */ u32 xy2;
    /* 0x1C */ u16 uv2;
    /* 0x1E */ u16 pad1;
    /* 0x20 */ u32 xy3;
    /* 0x24 */ u16 uv3;
    /* 0x26 */ u16 pad2;
} BattlingPolyFT4Words; /* POLY_FT4 */

/* A 0x28-byte primitive slot (POLY_FT4 or SPRT). */
typedef union {
    POLY_FT4 ft4;
    SPRT sprt;
} BattlingPrim28; /* 0x28 */

/* ---- scene graph (initBattlingDispEnvs-resetBattlingParticleLineF4) ---------------------------- */

/* Scene-graph node (allocateHeap(0x9C), createBattlingNode). A tree of nodes,
 * each with a type (m0) and a type-specific payload (m4): 1 model
 * (BattlingModel), 2 skeleton (BattlingSkeleton), 5 instance
 * (BattlingInstance), 6 light (BattlingLight). A gear's skeleton bones are
 * nodes too (their m4C_localMatrix.t is the part position). */
typedef struct BattlingNode {
    /* 0x00 */ s32 m0_type;
    /* 0x04 */ void* m4_data;           /* payload, per type */
    /* 0x08 */ void (*m8_callback)(void);
    /* 0x0C */ MATRIX mC_worldMatrix;
    /* 0x2C */ SVECTOR m2C_position;
    /* 0x34 */ VECTOR m34_translation;  /* m2C widened; light direction for lights */
    /* 0x44 */ SVECTOR m44_rotation;    /* vy: the gear's facing */
    /* 0x4C */ MATRIX m4C_localMatrix;
    /* 0x6C */ MATRIX m6C_baseMatrix;
    /* 0x8C */ struct BattlingNode* m8C_parent;
    /* 0x90 */ struct BattlingNode* m90_next;
    /* 0x94 */ struct BattlingNode* m94_child;
    /* 0x98 */ s32 m98;
} BattlingNode; /* 0x9C */

/* Typed views of a node's payload (m4_data). */
#define NODE_MODEL(node) ((BattlingModel*)(node)->m4_data)
#define NODE_SKELETON(node) ((BattlingSkeleton*)(node)->m4_data)
#define NODE_LIGHT(node) ((BattlingLight*)(node)->m4_data)

/* Type-1 node payload: a model (allocateHeap(0x20), createBattlingModel). */
typedef struct BattlingModel {
    /* 0x00 */ s32 m0_flags;            /* 1: hidden (animation events 4/5), 2: initialised, 0x10: colour override */
    /* 0x04 */ s32 m4_renderMode;
    /* 0x08 */ u8* m8_prims[2];         /* double-buffered primitives */
    /* 0x10 */ void* m10_dynamicVertices;
    /* 0x14 */ ModelBlock* m14_modelBlock;
    /* 0x18 */ u8 m18_r;
    /* 0x19 */ u8 m19_g;
    /* 0x1A */ u8 m1A_b;
    /* 0x1B */ u8 m1B;
    /* 0x1C */ s32 m1C;
} BattlingModel; /* 0x20 */

/* Render layer (createBattlingLayer, allocateHeap(0x68)): two ordering tables of
 * m14 entries in one buffer, plus per-buffer draw area/offset and a clear
 * tile. */
typedef struct BattlingLayer {
    /* 0x00 */ s32 m0;
    /* 0x04 */ OTag* m4_ot[2];
    /* 0x0C */ OTag* mC_otLast[2];      /* &m4_ot[i][m14 - 1] */
    /* 0x14 */ s16 m14_otSize;
    /* 0x16 */ u8 m16_flags;            /* 1 set at creation; 0xC by setBattlingLayerArea */
    /* 0x17 */ u8 m17_otShift;          /* 14 - log2(m14) */
    /* 0x18 */ DR_AREA m18_drawArea[2];
    /* 0x30 */ DR_OFFSET m30_drawOffset[2];
    /* 0x48 */ TILE m48_clearTile[2];
} BattlingLayer; /* 0x68 */

/* Type-6 node payload (createBattlingLight, allocateHeap(0x14)): a light, as
 * main's MulRotationMatrix takes it (src/main/light.c LightDef). */
typedef struct BattlingLight {
    /* 0x00 */ s32 m0_x;                /* direction */
    /* 0x04 */ s32 m4_y;
    /* 0x08 */ s32 m8_z;
    /* 0x0C */ u16 mC_r;                /* colour */
    /* 0x0E */ u16 mE_g;
    /* 0x10 */ u16 m10_b;
    /* 0x12 */ u16 m12;
} BattlingLight; /* 0x14 */

/* Scene (createBattlingScene, allocateHeap(0x28C)): a root node and three light
 * nodes, the render layer and a back colour. */
typedef struct BattlingScene {
    /* 0x000 */ s32 m0;
    /* 0x004 */ BattlingNode* m4_root;
    /* 0x008 */ BattlingNode* m8_lights[3];
    /* 0x014 */ BattlingNode m14_rootNode;
    /* 0x0B0 */ BattlingNode mB0_lightNodes[3];
    /* 0x284 */ BattlingLayer* m284_layer;
    /* 0x288 */ u8 m288_r;
    /* 0x289 */ u8 m289_g;
    /* 0x28A */ u8 m28A_b;
    /* 0x28B */ u8 m28B;
} BattlingScene; /* 0x28C */

/* Animation data (in a BattlingDataFile's m8 table): channel records, then
 * track records; the track keyframes follow at record-given offsets. */
typedef struct BattlingAnimRecord {
    /* 0x00 */ u8 m0_type;              /* & 0x7F: 3-5 rotation x/y/z, 6-8 position x/y/z */
    /* 0x01 */ u8 m1_bone;
    /* 0x02 */ u16 m2_value;            /* channel: value; track: keyframe offset */
} BattlingAnimRecord;

typedef struct BattlingAnimData {
    /* 0x00 */ s16 m0_frameCount;
    /* 0x02 */ s16 m2;
    /* 0x04 */ s16 m4_channelCount;
    /* 0x06 */ s16 m6_trackCount;
    /* 0x08 */ BattlingAnimRecord m8_records[1];
} BattlingAnimData;

typedef struct BattlingAnimChannel {
    /* 0x00 */ s16* m0_target;          /* &node->m44_rotation.vx ... */
    /* 0x04 */ u16 m4_value;
    /* 0x06 */ s16 m6_isAngle;
} BattlingAnimChannel; /* 0x08 */

typedef struct BattlingAnimTrack {
    /* 0x00 */ u8* m0_keysStart;
    /* 0x04 */ u8* m4_keys;
    /* 0x08 */ s16* m8_target;
    /* 0x0C */ u16 mC_wait;             /* frames to hold the current delta */
    /* 0x0E */ s16 mE_value;
    /* 0x10 */ s16 m10_delta;
    /* 0x12 */ s16 m12_isAngle;
} BattlingAnimTrack; /* 0x14 */

/* Bone record of a skeleton file (loadBattlingSkeleton): the file's m0 points to
 * a count, then these. */
typedef struct BattlingBoneData {
    /* 0x00 */ s16 m0_parent;           /* -1: the root */
    /* 0x02 */ s16 m2_model;            /* -1: none; else block index in the file's m4 Model */
    /* 0x04 */ s16 m4_rotation[3];
    /* 0x0A */ s16 mA_position[3];
} BattlingBoneData; /* 0x10 */

/* One animation of a skeleton (initBattlingAnim). */
typedef struct BattlingAnim {
    /* 0x00 */ BattlingAnimData* m0_data;
    /* 0x04 */ BattlingAnimChannel* m4_channels; /* one allocation with m8 */
    /* 0x08 */ BattlingAnimTrack* m8_tracks;
    /* 0x0C */ s32 mC;
    /* 0x10 */ s16 m10;
    /* 0x12 */ s16 m12_frame;                 /* frame (updateBattlingRound passes it to runBattlingGearAnimEvents) */
} BattlingAnim; /* 0x14 */

/* Type-2 node payload: a skeleton (createBattlingSkeleton, allocateHeap(0x1C);
 * built by loadBattlingSkeleton, freed by freeBattlingSkeleton). */
typedef struct BattlingSkeleton {
    /* 0x00 */ s16 m0_boneCount;
    /* 0x02 */ s16 m2_animCount;
    /* 0x04 */ BattlingNode** m4_bones;
    /* 0x08 */ BattlingAnim* m8_anims;
    /* 0x0C */ s32 mC;
    /* 0x10 */ s16 m10_scale[3];        /* 0x1000 */
    /* 0x16 */ s16 m16;
    /* 0x18 */ void* m18_boneData;
} BattlingSkeleton; /* 0x1C */

/* Relocatable data file header (relocateBattlingDataFile adds (self - m1C) to every
 * pointer). Field meanings unknown except mC (texture, uploadBattlingTexture) and m8
 * (a table: count, then count pointers, 0 = none). */
typedef struct BattlingDataFile {
#ifndef PLATFORM_PC
    /* 0x00 */ u8* m0_skeleton;
    /* 0x04 */ u8* m4_model;
    /* 0x08 */ u32* m8_table;
    /* 0x0C */ s32* mC_texture;
    /* 0x10 */ u8* m10_params;
    /* 0x14 */ u8* m14_animParams;
    /* 0x18 */ u8* m18_moves;
    /* 0x1C */ struct BattlingDataFile* m1C_base;
    /* 0x20 */ u8* m20_images;
    /* 0x24 */ u8* m24;
#else
    /* The PC leaves the file as loaded: addresses for a load at m1C_base,
     * read through BL_FILE (the table's entries likewise). */
    /* 0x00 */ u32 m0_skeleton;
    /* 0x04 */ u32 m4_model;
    /* 0x08 */ u32 m8_table;
    /* 0x0C */ u32 mC_texture;
    /* 0x10 */ u32 m10_params;
    /* 0x14 */ u32 m14_animParams;
    /* 0x18 */ u32 m18_moves;
    /* 0x1C */ u32 m1C_base;
    /* 0x20 */ u32 m20_images;
    /* 0x24 */ u32 m24;
#endif
} BattlingDataFile;

/* A pointer of a data file (relocated in place on the PS1); BL_FILE_AT for
 * an address the file holds elsewhere (0 = none). */
#ifndef PLATFORM_PC
#define BL_FILE(file, field) ((file)->field)
#else
#define BL_FILE_AT(file, addr) ((addr) != 0 ? (void*)((u8*)(file) + ((u32)(addr) - (file)->m1C_base)) : NULL)
#define BL_FILE(file, field) BL_FILE_AT(file, (file)->field)
#endif

/* Flat-shaded copy of a model block (initBattlingShadowModel, allocateHeap(0x18)):
 * one POLY_F3/POLY_F4 per face, double-buffered (a shadow/silhouette?). */
typedef struct BattlingFlatModel {
    /* 0x00 */ s16 m0_numVertices;
    /* 0x02 */ s16 m2_numPrims;
    /* 0x04 */ SVECTOR* m4_vertices;
    /* 0x08 */ SVECTOR* m8_screenVertices; /* m0 entries of 8 bytes */
    /* 0x0C */ ModelBlock* mC_block;
    /* 0x10 */ u8* m10_prims[2];
} BattlingFlatModel; /* 0x18 */

/* Type-5 node payload (createBattlingInstance, allocateHeap(0x10)): an instance of
 * another node (cloneBattlingShadowTree clones a node tree into these). */
typedef struct BattlingInstance {
    /* 0x00 */ s32 m0_type;             /* the source node's type */
    /* 0x04 */ BattlingNode* m4_source;
    /* 0x08 */ BattlingNode* m8_root;    /* battlingShadowRoot at creation */
    /* 0x0C */ BattlingFlatModel* mC_flatModel; /* type 1 only */
} BattlingInstance; /* 0x10 */

/* Coroutine context (createBattlingThread, allocateHeap(0x84)): the saved
 * registers r0-r31 (indexed by register number; the hand-written
 * resumeBattlingThread/yieldBattlingThread switch to and from it) and the stack. */
typedef struct BattlingThread {
    /* 0x00 */ uptr m0_regs[32];
    /* 0x80 */ u32* m80_stack;
} BattlingThread; /* 0x84 */

/* ---- particles (resetBattlingParticleLineF4-updateBattlingSparkBurst) ------------------------------ */

/* Common head of a particle: position (pad = remaining life, 0 = free) and
 * velocity. */
typedef struct BattlingParticle {
    /* 0x00 */ s16 m0_x;
    /* 0x02 */ s16 m2_y;
    /* 0x04 */ s16 m4_z;
    /* 0x06 */ s16 m6_life;             /* 0 = free */
    /* 0x08 */ s16 m8_vx;
    /* 0x0A */ s16 mA_vy;
    /* 0x0C */ s16 mC_vz;
} BattlingParticle;

struct BattlingEmitter;
typedef void (*BattlingParticleInitFn)(void* particle, struct BattlingEmitter* e);
typedef void (*BattlingParticleResetFn)(void* particle);
typedef void (*BattlingParticleRenderFn)(void* particle, OTag* ot);
typedef void (*BattlingEmitterShapeFn)(struct BattlingEmitter* e, SVECTOR* out);

/* Particle types (battlingParticleTypes, 5 entries: LINE_F4, LINE_F3, LINE_F2 trails,
 * TILE, TILE_1). */
typedef struct BattlingParticleType {
    /* 0x00 */ BattlingParticleInitFn m0_init;
    /* 0x04 */ BattlingParticleResetFn m4_reset;
    /* 0x08 */ BattlingParticleRenderFn m8_render;
    /* 0x0C */ s32 mC_size;
} BattlingParticleType;

/* Particle emitter / pool (createBattlingEmitter(type, shape), allocateHeap(0x7C)):
 * sparks (battlingSparkEmitter), dust, the tutorial light battlingHitSparkEmitter. */
typedef struct BattlingEmitter {
    /* 0x00 */ s16 m0_gravity;
    /* 0x02 */ s16 m2;
    /* 0x04 */ s16 m4;
    /* 0x06 */ s16 m6;
    /* 0x08 */ s16 m8;
    /* 0x0A */ s16 mA_count;
    /* 0x0C */ s16 mC_particleSize;     /* pool stride */
    /* 0x0E */ s16 mE;
    /* 0x10 */ BattlingParticle* m10_particles;
    /* 0x14 */ s16 m14_x;
    /* 0x16 */ s16 m16_y;
    /* 0x18 */ s16 m18_z;
    /* 0x1A */ s16 m1A;
    /* 0x1C */ SVECTOR m1C_origin;
    /* 0x24 */ s16 m24_size[3];
    /* 0x2A */ s16 m2A;
    /* 0x2C */ s16 m2C_halfSize[3];
    /* 0x32 */ s16 m32;
    /* 0x34 */ SVECTOR m34_rotation;
    /* 0x3C */ SVECTOR m3C_rotation2;
    /* 0x44 */ s16 m44_spread;
    /* 0x46 */ s16 m46;
    /* 0x48 */ s16 m48_speedMin;
    /* 0x4A */ s16 m4A_speedRange;
    /* 0x4C */ s16 m4C;
    /* 0x4E */ s16 m4E_shape;
    /* 0x50 */ BattlingParticleResetFn m50_reset;
    /* 0x54 */ BattlingParticleRenderFn m54_render;
    /* 0x58 */ BattlingParticleInitFn m58_init;
    /* 0x5C */ BattlingEmitterShapeFn m5C_shape;
    /* 0x60 */ void* m60_groundBounce;               /* bounceBattlingParticleOnGround */
    /* 0x64 */ s16 m64;
    /* 0x66 */ s16 m66_type;
    /* 0x68 */ s16 m68;
    /* 0x6A */ s16 m6A_life;
    /* 0x6C */ s32 m6C;
    /* 0x70 */ s32 m70;
    /* 0x74 */ u8 m74_r;
    /* 0x75 */ u8 m75_g;
    /* 0x76 */ u8 m76_b;
    /* 0x77 */ u8 m77;
    /* 0x78 */ s32 m78;
} BattlingEmitter; /* 0x7C */

/* Typed particles: position, velocity, then per type a trail of previous
 * positions and two primitives (one per display buffer). */
typedef struct BattlingParticleLineF4 {
    /* 0x00 */ SVECTOR m0_pos;
    /* 0x08 */ SVECTOR m8_velocity;
    /* 0x10 */ SVECTOR m10_trail[3];
    /* 0x28 */ LINE_F4 m28_prims[2];
} BattlingParticleLineF4; /* 0x60 */

typedef struct BattlingParticleLineF3 {
    /* 0x00 */ SVECTOR m0_pos;
    /* 0x08 */ SVECTOR m8_velocity;
    /* 0x10 */ SVECTOR m10_trail[2];
    /* 0x20 */ LINE_F3 m20_prims[2];
} BattlingParticleLineF3; /* 0x50 */

typedef struct BattlingParticleLineF2 {
    /* 0x00 */ SVECTOR m0_pos;
    /* 0x08 */ SVECTOR m8_velocity;
    /* 0x10 */ SVECTOR m10_trail[1];
    /* 0x18 */ LINE_F2 m18_prims[2];
} BattlingParticleLineF2; /* 0x38 */

typedef struct BattlingParticleTile {
    /* 0x00 */ SVECTOR m0_pos;
    /* 0x08 */ SVECTOR m8_velocity;
    /* 0x10 */ TILE m10_prims[2];
} BattlingParticleTile; /* 0x30 */

typedef struct BattlingParticleTile1 {
    /* 0x00 */ SVECTOR m0_pos;
    /* 0x08 */ SVECTOR m8_velocity;
    /* 0x10 */ TILE_1 m10_prims[2];
} BattlingParticleTile1; /* 0x28 */

/* ---- the gears ------------------------------------------------------------ */

/* 4-byte move entries: gear->m80[move] (per move, 15), gear->m84 = current
 * move (battlingIdleMove while idle). */
typedef struct BattlingMove {
    /* 0x0 */ u8 m0_anim;
    /* 0x1 */ u8 m1_lunge;
    /* 0x2 */ u8 m2_effects;            /* nonzero: spawn hit sparks; set to 1 on init */
    /* 0x3 */ u8 m3_alive;
} BattlingMove;

/* gear->m8FC: per-gear parameters (camera parameters at 0x14..0x2C). */
typedef struct BattlingGearParams {
    /* 0x00 */ u8 m0_topPart;
    /* 0x01 */ u8 m1_foot1Part;
    /* 0x02 */ u8 m2_foot2Part;
    /* 0x03 */ u8 m3_bodyRadius;
    /* 0x04 */ s16 m4_foot1Vertex;
    /* 0x06 */ s16 m6_foot2Vertex;
    /* 0x08 */ s16 m8_topVertex;
    /* 0x0A */ u8 mA_pad[2];
    /* 0x0C */ u16 mC_speedScale;
    /* 0x0E */ u8 mE_hiddenPartCount;
    /* 0x0F */ u8 mF_moveScale;
    /* 0x10 */ u8 m10_glowR;
    /* 0x11 */ u8 m11_glowG;
    /* 0x12 */ u8 m12_glowB;
    /* 0x13 */ u8 m13_radius;          /* hit radius */
    /* 0x14 */ s16 m14_victoryPitch;                 /* s16 values copied to battlingVictoryPitch.. */
    /* 0x16 */ s16 m16_victorySpin;
    /* 0x18 */ s16 m18_victoryY;
    /* 0x1A */ s16 m1A_victoryZ;
    /* 0x1C */ s16 m1C_victoryScale;
    /* 0x1E */ s16 m1E_victoryYaw;
    /* 0x20 */ u16 m20_scale;
    /* 0x22 */ u8 m22_pad[2];
    /* 0x24 */ s16 m24_winCamAngle;
    /* 0x26 */ s16 m26_winCamHeight;
    /* 0x28 */ s16 m28_winCamTargetHeight;
    /* 0x2A */ s16 m2A_winCamDistance;
    /* 0x2C */ s16 m2C_winAnimLoops;
} BattlingGearParams;

/* Animation event list entry (in m8FC_params at m900_eventOffsets[anim]);
 * the list ends with m0_start == 0xFF. */
typedef struct BattlingAnimEvent {
    /* 0x0 */ u8 m0_start;
    /* 0x1 */ u8 m1_end;
    /* 0x2 */ s16 m2_cmdOffset;         /* BattlingEffectCmd, from m8FC_params */
} BattlingAnimEvent;

/* Effect command read by runBattlingGearEffectCommand (spark/beam between two part
 * vertices). */
typedef struct BattlingEffectCmd {
    /* 0x0 */ u8 m0_kind;               /* animation event kind (runBattlingGearAnimEvents) */
    /* 0x1 */ u8 m1_type;               /* 0x10: beam; >= 0x20: other spawner; kind 4/5: part */
    /* 0x2 */ u8 m2_partA;
    /* 0x3 */ u8 m3_partB;
    /* 0x4 */ s16 m4_vertexA;
    /* 0x6 */ s16 m6_vertexB;
} BattlingEffectCmd;

/* battlingGearDefs[49] (0x20 bytes): per-gear definition, gear->m1600_def. */
typedef struct BattlingGearDef {
    /* 0x00 */ s16 m0_power;            /* move damage = mA_moves[move - 1] * m0 / 100 */
    /* 0x02 */ s16 m2;
    /* 0x04 */ s16 m4_unlockLevel;                  /* compared with D_8006EF64 (unlock level?) */
    /* 0x06 */ u8 m6_ai[4];             /* copied to BattlingAiState m10/m14/m18/m1C */
    /* 0x0A */ u8 mA_moves[14];         /* per move (move 1..14 at [move - 1]); 0: move/part absent */
    /* 0x18 */ u8 m18_etherPower;
    /* 0x19 */ u8 m19[7];
} BattlingGearDef; /* 0x20 */

/* gear->m9CC_replay[256] (0xC bytes): input/position record per frame;
 * m15CC points to the current frame's record, m15D0 to the next one. */
typedef struct BattlingReplayFrame {
    /* 0x00 */ s16 m0_x;
    /* 0x02 */ s16 m2_y;
    /* 0x04 */ s16 m4_z;
    /* 0x06 */ union {
        u16 flags;                      /* 0x8000: marked by addBattlingGearGauge; angle = m54_rotY, b12 = animation restarted this frame */
        struct {
            s16 angle : 12;
            u16 b12 : 1;
            u16 b13 : 1;
            s16 b14 : 1;                /* s16: extracted with slt (loadBattlingGearReplayFlags) */
            u16 b15 : 1;
        } bits;
    } m6_angleFlags;
    /* 0x08 */ u8 m8_anim;
    /* 0x09 */ u8 m9_frameStep;
    /* 0x0A */ u16 mA : 8;
    /* 0x0A */ u16 mA_b8 : 1;
    /* 0x0A */ u16 mA_pad : 7;
} BattlingReplayFrame; /* 0xC */

/* gear->mD0_status bits: side, CPU control, combo count, facing, ground type, animation/script state. */
typedef struct BattlingGearFlags {
    u32 b0 : 1;
    u32 b1 : 1;                         /* script op 34 */
    u32 b2 : 1;
    u32 b3_combo : 3;                   /* cleared by script op 34 */
    u32 b6 : 1;                         /* CPU-controlled (settings bytes 5/6) */
    u32 b7 : 3;
    u32 b10_burning : 1;                /* set while burning (ending) */
    u32 b11_animEnd : 1;
    u32 b12 : 1;
    u32 b13 : 2;
    u32 b15_scriptTurn : 1;             /* scripted turn (op 20) */
    u32 b16 : 1;
    u32 b17_facing : 1;
    u32 b18 : 1;
    u32 b19 : 1;
    u32 b20 : 3;
    u32 b23 : 1;
    u32 b24 : 1;
    u32 b25 : 1;
    u32 b26_effectStart : 1;            /* set during an effect event's first frame */
    u32 b27_side : 1;                   /* 0: left/player 1, 1: right/player 2 */
    u32 b28 : 1;
    u32 b29_ground : 2;                 /* getBattlingMapCell() >> 24 (ground type / guard) */
    u32 b31 : 1;
} BattlingGearFlags;

/* gear->mD4_cues bits: sound/glow cue latches (b0_1 walk/dash sound state, b2_3 its previous value, b4 landing sound, b5 glow, b6 dash refused). */
typedef struct BattlingGearFlags2 {
    u32 b0_1 : 2;
    u32 b2_3 : 2;
    u32 b4 : 1;
    u32 b5 : 1;                         /* 0x20: glow in the opponent's colour */
    u32 b6 : 1;
    u32 b7_31 : 25;
} BattlingGearFlags2;

/* gear->m104_shapes[16] (0x54 bytes, filled by setBattlingGearHitShape): a hit shape
 * (quad or segment: four points, the current and previous ends of a moving
 * part) of an attack. */
typedef struct BattlingGearShape {
    /* 0x00 */ VECTOR m0_points[4];     /* position, previous position, end, previous end */
    /* 0x40 */ s16 m40_effect;
    /* 0x42 */ u8 m42;
    /* 0x43 */ u8 m43_part;
    /* 0x44 */ union {                  /* the hit: state/type/damage; b0 new handle this frame, b1 single swept point (segment test) vs quad */
        u32 flags;
        struct {
            u32 b0 : 1;
            u32 b1 : 1;
            u32 b2 : 6;
            u32 state : 8;              /* 2 new, 1 previous frame, 0 free */
            u32 type : 8;
            u32 damage : 8;
        } bits;
        struct {
            u8 m44;
            u8 m45_state;
            u8 m46_type;
            u8 m47_damage;
        } b;
    } m44_hit;
    /* 0x48 */ u8 m48_frame;            /* battlingFrameCounter when last updated */
    /* 0x49 */ u8 m49[3];
    /* 0x4C */ s32 m4C_handle;          /* makeBattlingEffectHandle result */
    /* 0x50 */ BattlingMove* m50_move;   /* gear->m84_move */
} BattlingGearShape; /* 0x54 */

/* gear->m64C_projectiles[9] (0x44 bytes; fireBattlingProjectile fills a free one):
 * projectiles and attack spheres. */
typedef struct BattlingProjectile {
    /* 0x00 */ VECTOR m0_position;
    /* 0x10 */ VECTOR m10_prevPosition;
    /* 0x20 */ SVECTOR m20_velocity;     /* m28 scaled by m31 (scaleBattlingSVector) */
    /* 0x28 */ SVECTOR m28_direction;    /* normalizeBattlingVectorS(target delta) */
    /* 0x30 */ u8 m30_active;
    /* 0x31 */ u8 m31_speed;
    /* 0x32 */ u8 m32_drawType;          /* 0-5, see updateBattlingProjectiles */
    /* 0x33 */ u8 m33_moveType;          /* 1 accelerate, 2 homing ramp, 3 straight */
    /* 0x34 */ s32 m34_distance;         /* to the opponent's m94C */
    /* 0x38 */ s32 m38_handle;
    /* 0x3C */ s16 m3C_damage;
    /* 0x3E */ s16 m3E_homing;           /* LoadAverageShort12 weight */
    /* 0x40 */ s16 m40_life;
    /* 0x42 */ u8 m42[0x2];
} BattlingProjectile; /* 0x44 */

/* battlingProjectileKinds[]: projectile kinds. */
typedef struct BattlingProjectileKind {
    /* 0x0 */ u16 m0_homing;
    /* 0x2 */ u8 m2_drawType;
    /* 0x3 */ u8 m3_life;
    /* 0x4 */ u8 m4_speed;
    /* 0x5 */ u8 m5_moveType;
    /* 0x6 */ u8 m6_sound;
    /* 0x7 */ u8 m7;
} BattlingProjectileKind;

struct BattlingAiState;

/* One arena gear: battlingGear1 (side 0: player 1, pad 1, on the left) and
 * battlingGear2 (side 1: player 2); other globals (battlingCameraTarget,
 * battlingMessageWindow, battlingCameraPos) sit between the two. */
typedef struct BattlingGear {
    /* 0x0000 */ VECTOR m0_position;
    /* 0x0010 */ VECTOR m10_velocity;
    /* 0x0020 */ VECTOR m20_knockback;
    /* 0x0030 */ VECTOR m30_lungeVelocity;
    /* 0x0040 */ s32 m40_moveSpeed;
    /* 0x0044 */ s32 m44_lungeSpeed;
    /* 0x0048 */ s32 m48_speed;          /* 0xFF, 0xF0 while scripted, 0 */
    /* 0x004C */ u8 m4C_animation;
    /* 0x004D */ u8 m4D;
    /* 0x004E */ u8 m4E_lastAnimation;
    /* 0x004F */ u8 m4F_animSpeed;
    /* 0x0050 */ s16 m50_animFrame;      /* last frame whose events ran */
    /* 0x0052 */ u8 m52_moveAnimSpeed;
    /* 0x0053 */ u8 m53;
    /* 0x0054 */ s32 m54_rotY;
    /* 0x0058 */ s32 m58_targetRotation; /* script: turn command */
    /* 0x005C */ BattlingNode* m5C_model; /* skeleton root node (type 2) */
    /* 0x0060 */ BattlingNode* m60_node;  /* follows the scene root's matrix */
    /* 0x0064 */ u8 m64_pad[0x8];
    /* 0x006C */ VECTOR m6C_prevPosition;
    /* 0x007C */ u8* m7C_animParams;
    /* 0x0080 */ BattlingMove* m80_moves; /* per move (m9C3_move) */
    /* 0x0084 */ BattlingMove* m84_move;  /* current move */
    /* 0x0088 */ s32 m88_inputLeft;
    /* 0x008C */ s32 m8C_inputUp;
    /* 0x0090 */ s32 m90_inputRight;
    /* 0x0094 */ s32 m94_inputDown;
    /* 0x0098 */ s32 m98_accel;
    /* 0x009C */ s32 m9C_decel;
    /* 0x00A0 */ u8 mA0_pad[0x8];
    /* 0x00A8 */ s32 mA8;
    /* 0x00AC */ u8 mAC_pad[0x4];
    /* 0x00B0 */ s32 mB0_groundY;
    /* 0x00B4 */ s16 mB4_hp;
    /* 0x00B6 */ s16 mB6_gauge;          /* capped at 0x1000; the overflow / 20 costs hp; HUD bar >> 6 */
    /* 0x00B8 */ s16 mB8_barHp;
    /* 0x00BA */ s16 mBA_damage;
    /* 0x00BC */ s16 mBC_maxHp;
    /* 0x00BE */ s16 mBE_gaugeCost;
    /* 0x00C0 */ u8 mC0_hpBar;
    /* 0x00C1 */ u8 mC1_bar;
    /* 0x00C2 */ u8 mC2_flash;
    /* 0x00C3 */ u8 mC3_backDashTimer;
    /* 0x00C4 */ u8 mC4_moveState;
    /* 0x00C5 */ u8 mC5_action;
    /* 0x00C6 */ u8 mC6_prevMoveState;
    /* 0x00C7 */ u8 mC7_pad;
    /* 0x00C8 */ s16 mC8_comboTimer;
    /* 0x00CA */ s16 mCA_downTimer;
    /* 0x00CC */ s16 mCC_sideInputRotation;
    /* 0x00CE */ s16 mCE_inputRotation;
    /* 0x00D0 */ union {
        u32 flags;                      /* 0x2 AI, 0x38 cleared by the AI, 0x40 CPU, 0x100 slowed,
                                         * 0x8000 from ai->mE, 0x8000000 player 2 side,
                                         * 0x60000000 2-bit state */
        BattlingGearFlags bits;
    } mD0_status;
    /* 0x00D4 */ union {
        u32 flags;
        BattlingGearFlags2 bits;
    } mD4_cues;
    /* 0x00D8 */ struct BattlingGear* mD8_opponent;
    /* 0x00DC */ u8 mDC_pad[0xC];
    /* 0x00E8 */ s32 mE8_stun;
    /* 0x00EC */ u8 mEC_pad[0x4];
    /* 0x00F0 */ s16 mF0_comboHits;
    /* 0x00F2 */ s16 mF2_wins;           /* rounds won (0..3 markers) */
    /* 0x00F4 */ s32 mF4;
    /* 0x00F8 */ u8 mF8_pad[0x4];
    /* 0x00FC */ s32 mFC_turnRotation;
    /* 0x0100 */ s32 m100_hitType;
    /* 0x0104 */ BattlingGearShape m104_shapes[16];
    /* 0x0644 */ s32 m644_moveDamage;
    /* 0x0648 */ s32 m648_baseRotation;
    /* 0x064C */ BattlingProjectile m64C_projectiles[9];
    /* 0x08B0 */ u8 m8B0_pad[0x44];
    /* 0x08F4 */ s32 m8F4_nearestProjectileDist;
    /* 0x08F8 */ BattlingProjectile* m8F8_nearestProjectile;
    /* 0x08FC */ BattlingGearParams* m8FC_params;
    /* 0x0900 */ s16* m900_eventOffsets;  /* per animation, into m8FC_params (m8FC_params + 0x34) */
    /* 0x0904 */ u8* m904_hiddenParts;
    /* 0x0908 */ u8 m908_hiddenPartCount;
    /* 0x0909 */ u8 m909_gearId;         /* index into battlingGearEntries / battlingGearDefs */
    /* 0x090A */ u8 m90A_flags;
    /* 0x090B */ u8 m90B_dustTimer;
    /* 0x090C */ s16 m90C_attackRotation;
    /* 0x090E */ u8 m90E_holdAnimation;
    /* 0x090F */ u8 m90F_pad;
    /* 0x0910 */ u8 m910_damageSfxTimer;
    /* 0x0911 */ u8 m911_partCount;
    /* 0x0912 */ u8 m912_count;
    /* 0x0913 */ u8 m913_pad;
    /* 0x0914 */ s16 m914_inputLock;
    /* 0x0916 */ s16 m916_stagger;
    /* 0x0918 */ u8 m918_glow;           /* light intensity, fades by 0x18 */
    /* 0x0919 */ u8 m919_pad[0x3];
    /* 0x091C */ VECTOR m91C_lastHitPoint;
    /* 0x092C */ VECTOR m92C_topPoint;
    /* 0x093C */ VECTOR m93C_footPoint;
    /* 0x094C */ VECTOR m94C_target;     /* projectiles home on it */
    /* 0x095C */ VECTOR m95C_prevFootPoint;
    /* 0x096C */ s16 m96C_prevFoot2Y;
    /* 0x096E */ s16 m96E_prevFoot1Y;
    /* 0x0970 */ s32 m970_hitDamage;
    /* 0x0974 */ VECTOR m974_hitPoint;
    /* 0x0984 */ u8 m984_pad[0x14];
    /* 0x0998 */ s16 m998_stepStartFrame;
    /* 0x099A */ s16 m99A_stepFrames;
    /* 0x099C */ s16 m99C_blendSteps;
    /* 0x099E */ s16 m99E_frameAccum;
    /* 0x09A0 */ u8 m9A0_inputQueue[32];
    /* 0x09C0 */ u8 m9C0_inputQueueWrite;
    /* 0x09C1 */ u8 m9C1_inputQueueRead;
    /* 0x09C2 */ u8 m9C2_inputQueueCount;
    /* 0x09C3 */ u8 m9C3_move;           /* current move (index of m80_moves, battlingMoveNames) */
    /* 0x09C4 */ u8 m9C4_pad[0x8];
    /* 0x09CC */ BattlingReplayFrame m9CC_replay[256];
    /* 0x15CC */ BattlingReplayFrame* m15CC_replayCur;
    /* 0x15D0 */ BattlingReplayFrame* m15D0_replayNext;
    /* 0x15D4 */ CVECTOR m15D4_color;    /* glow colour; &m15D4_color is passed to setBattlingEffectColor/loadBattlingGearColorClut */
    /* 0x15D8 */ s16 m15D8[7];
    /* 0x15E6 */ u8 m15E6_pad[0x2];
    /* 0x15E8 */ s16 m15E8;
    /* 0x15EA */ s16 m15EA;
    /* 0x15EC */ s16 m15EC;
    /* 0x15EE */ s16 m15EE;
    /* 0x15F0 */ s16 m15F0_moveScale;
    /* 0x15F2 */ s16 m15F2_speedScale;
    /* 0x15F4 */ s16 m15F4_baseSpeedScale;
    /* 0x15F6 */ s16 m15F6;
    /* 0x15F8 */ s16 m15F8;
    /* 0x15FA */ u8 m15FA_pad[0x2];
    /* 0x15FC */ struct BattlingAiState* m15FC_ai;
    /* 0x1600 */ BattlingGearDef* m1600_def;
    /* 0x1604 */ POLY_FT4 m1604_shadow[2];
    /* 0x1654 */ s32 m1654_etherHpLoss;
    /* 0x1658 */ s32 m1658_boostHpLoss;
    /* 0x165C */ s32 m165C_etherPresses;
    /* 0x1660 */ s32 m1660_attackPresses;
    /* 0x1664 */ u8* m1664_hitSounds;    /* 7 bytes: sound pair index per hit kind, 0xFF = none */
    /* 0x1668 */ s16 m1668_hitsTaken;
    /* 0x166A */ u8 m166A_pad[0x2];
} BattlingGear; /* 0x166C */

/* gear->m15FC_ai: CPU opponent state and personality (second argument of the
 * AI callbacks practiceAiBackDash...): battlingGear1Ai (player 1), battlingGear2Ai
 * (player 2). m10/m18/m1C/m24 are chances/limits compared with
 * rand() & 0xFF or with gear fields. */
typedef struct BattlingAiState {
    /* 0x00 */ struct BattlingGear* m0_gear;
    /* 0x04 */ s16 m4_timer;
    /* 0x06 */ u8 m6;
    /* 0x07 */ u8 m7;
    /* 0x08 */ u8 m8_state;
    /* 0x09 */ u8 m9_repeatCount;
    /* 0x0A */ s16 mA_turn;
    /* 0x0C */ s16 mC_speed;
    /* 0x0E */ u8 mE_boost;
    /* 0x0F */ u8 mF_comLevel;
    /* 0x10 */ s32 m10_aiParam0;
    /* 0x14 */ s32 m14_aiParam1;
    /* 0x18 */ s32 m18_aiParam2;
    /* 0x1C */ s32 m1C_aiParam3;
    /* 0x20 */ s32 m20_waitStep;
    /* 0x24 */ s32 m24_gaugeLimit;
    /* 0x28 */ s32 m28_retreatDistance;
    /* 0x2C */ u32 m2C_b0_7 : 8;
    /* 0x2C */ u32 m2C_b8 : 1;
    /* 0x2C */ u32 m2C_b9 : 1;
    /* 0x2C */ u32 m2C_b10 : 1;
    /* 0x2C */ u32 m2C_b11 : 1;
    /* 0x2C */ u32 m2C_b12 : 1;
    /* 0x2C */ u32 m2C_b13 : 1;
    /* 0x2C */ u32 m2C_pad14 : 2;
    /* 0x2E */ u32 m2C_b16_23 : 8;      /* byte 0x2E (enterBattlingAiApproach clears it with sb) */
    /* 0x2F */ u32 m2C_pad24 : 8;
    /* 0x30 */ s16 m30_hitsTaken;
} BattlingAiState; /* 0x34 */

/* battlingSideStates[2] (by gear side): per-side text/HUD display and attack
 * bookkeeping. */
typedef struct BattlingSideState {
    /* 0x00 */ char* m0_text;
    /* 0x04 */ u8 m4_strength;
    /* 0x05 */ u8 m5_pad[3];
    /* 0x08 */ char* m8_prevText;
    /* 0x0C */ s32 mC_startFrame;
    /* 0x10 */ s32 m10_endFrame;
} BattlingSideState; /* 0x14 */

/* battlingGearEntries[49] (0xC bytes): gear list (names and model files in
 * battlingGearNames). */
typedef struct BattlingGearEntry {
    /* 0x0 */ s32 m0_id;
    /* 0x4 */ char* m4_modelName;       /* "sol_13" ... */
    /* 0x8 */ char* m8_name;
} BattlingGearEntry;

/* battlingGearAnimInfo[], indexed by animation id. */
typedef struct BattlingAnimInfo {
    /* 0x0 */ u8 m0_type;
    /* 0x1 */ s8 m1_next;
} BattlingAnimInfo; /* 0x2 */

/* battlingSettings: battling settings / mode block, right after gear 1. Its
 * fields must be struct fields (updateBattlingFight, updateBattlingPracticeAi: in-struct
 * loads keep retail's schedule); updateBattlingPracticeAi still reads 0x80099DA2 as
 * the plain scalar D_80099DA2 (CSE keeps it across the struct stores).
 * Bytes are changed with adjustBattlingMenuValue by the menus. */
typedef struct BattlingSettings {
    /* 0x00 */ u8 m0_comLevel;          /* "COM LEVEL"; copied to BattlingAiState::mF */
    /* 0x01 */ u8 m1;
    /* 0x02 */ u8 m2_frameSkip;         /* index into battlingFrameRates (fps); copied to battlingFrameSkipTimer */
    /* 0x03 */ u8 m3_p1Vibration;       /* pad 1 vibration on/off */
    /* 0x04 */ u8 m4_p2Vibration;       /* pad 2 vibration on/off */
    /* 0x05 */ u8 m5_p1Com;             /* player 1 is the CPU (-> gear mD0_status bit 6) */
    /* 0x06 */ u8 m6_p2Com;             /* player 2 is the CPU (-> gear mD0_status bit 6); set by updateBattlingPracticeAi */
    /* 0x07 */ u8 m7_winsNeeded;        /* rounds to win the match */
    /* 0x08 */ u8 m8;
    /* 0x09 */ u8 m9_speedIndex;        /* index into battlingMotionSpeeds */
    /* 0x0A */ u8 mA_comAi;             /* CPU behaviour (battlingComAiNames names); also read as the scalar D_80099DA2 */
    /* 0x0B */ u8 mB;
    /* 0x0C */ s16 mC_speed;            /* battlingMotionSpeeds[m9_speedIndex], copied to battlingSpeedScale */
    /* 0x0E */ u8 mE_pad[2];
} BattlingSettings; /* 0x10 */

/* main 0x8006F978: Battling progress kept in the kernel (8 flag bytes = 64
 * "cleared" bits, then a packed settings word read back into the settings
 * bytes battlingSettings). Retail names the word D_8006F980. */
typedef struct BattlingKernelState {
    /* 0x00 */ u8 m0_flags[8];
    /* 0x08 */ u32 m8_valid : 4;        /* 1 once written */
    /* 0x08 */ u32 m8_x4 : 1;           /* <-> settings m3_p1Vibration */
    /* 0x08 */ u32 m8_x5 : 1;           /* <-> settings m4_p2Vibration */
    /* 0x08 */ u32 m8_x6 : 7;           /* <-> settings m7_winsNeeded */
    /* 0x08 */ u32 m8_xD : 3;           /* <-> settings m0_comLevel */
    /* 0x08 */ u32 m8_x10_allCleared : 1; /* set when flags 0..48 (but 22) are all set */
} BattlingKernelState;

/* ---- sprite effects (loadBattlingGearColorClut-spawnBattlingBeamEffect) ------------------------- */

/* Effect templates battlingEffectTemplates[] (0x14 bytes each). */
typedef struct BattlingEffectTemplate {
    /* 0x00 */ u8* m0_u;                /* per-frame u */
    /* 0x04 */ u8* m4_v;                /* per-frame v */
    /* 0x08 */ u8 m8_w;
    /* 0x09 */ u8 m9_h;
    /* 0x0A */ u8 mA_frameCount;
    /* 0x0B */ u8 mB_size;              /* 0..2: corner set */
    /* 0x0C */ s16 mC_clut;             /* s16 (lh); 0: per-frame clut from battlingEffect0FrameCluts */
    /* 0x0E */ u16 mE_tpage;
    /* 0x10 */ s16 m10_accel;
    /* 0x12 */ u16 m12;
} BattlingEffectTemplate; /* 0x14 */

struct BattlingEffect;

/* BattlingEffect::m60_view views (offsets relative to 0x60). */
typedef struct BattlingEffectAnim { /* type 0: animated sprite (spawnBattlingSpriteEffect) */
    /* 0x00 */ BattlingEffectTemplate* m0_template;
    /* 0x04 */ u16 m4_accel;
    /* 0x06 */ u16 m6_speed;
} BattlingEffectAnim;

typedef struct BattlingEffectTrail { /* type 1: trail (spawnBattlingTrailEffect, drawn by drawBattlingTrailEffects) */
    /* 0x00 */ s16 m0_x0;               /* screen edge points of this segment */
    /* 0x02 */ s16 m2_y0;
    /* 0x04 */ s16 m4_x1;
    /* 0x06 */ s16 m6_y1;
    /* 0x08 */ s16 m8_sx;               /* screen centre */
    /* 0x0A */ s16 mA_sy;
    /* 0x0C */ struct BattlingEffect* mC_prev; /* last frame's segment */
    /* 0x10 */ u16 m10_frame;
    /* 0x12 */ s16 m12_id;
    /* 0x14 */ s32 m14_width;
    /* 0x18 */ s32 m18_owner;
} BattlingEffectTrail;

typedef struct BattlingEffectBeam { /* type 2: beam (spawnBattlingBeamEffect, drawn by drawBattlingBeamEffects) */
    /* 0x00 */ struct BattlingEffect* m0_prev;
    /* 0x04 */ s16 m4_pos[3];
    /* 0x0A */ u16 mA_frame;
    /* 0x0C */ s16 mC_id;
    /* 0x0E */ s16 mE;
    /* 0x10 */ s32 m10_owner;
} BattlingEffectBeam;

/* 0x7C-byte entries of battlingSpriteEffects[60]: sprite effects. m53 selects the
 * kind and the view of 0x60..0x7B. */
typedef struct BattlingEffect {
    /* 0x00 */ POLY_FT4 m0_polys[2];
    /* 0x50 */ u8 m50_active;
    /* 0x51 */ s8 m51_frame;
    /* 0x52 */ u8 m52_frameCount;
    /* 0x53 */ u8 m53_type;             /* 0: animated sprite, 1: trail, 2: beam */
    /* 0x54 */ s16 m54_x;
    /* 0x56 */ u16 m56_y;
    /* 0x58 */ s16 m58_z;
    /* 0x5A */ u8 m5A_pad[0x6];
    /* 0x60 */ union {                  /* view selected by m53_type */
        BattlingEffectAnim anim;
        BattlingEffectTrail trail;
        BattlingEffectBeam beam;
    } m60_view;
} BattlingEffect; /* 0x7C */

/* Scratchpad (0x1F800000) layout used by the effect and line renderers. */
typedef struct BattlingScratch {
    /* 0x00 */ VECTOR m0_camera;        /* copy of battlingViewOrigin */
    /* 0x10 */ SVECTOR m10_v[4];
    /* 0x30 */ VECTOR m30_corners[6];   /* billboard corner offsets (drawBattlingSpriteEffects) */
    /* 0x90 */ u8 m90_pad[0x20];
    /* 0xB0 */ s32 mB0_sz;
} BattlingScratch;

#define BATTLING_SCRATCH ((BattlingScratch*)PSX_SCRATCH(0x1F800000))

/* ---- 3D effects and lines (drawBattlingEffects-showBattlingAnalogWarning) ------------------- */

/* Debug/effect 3D line (battlingLines[100]): a LINE_F2 whose two ends are
 * projected from m10_p0/m18_p1 by drawBattlingLines. */
typedef struct BattlingDebugLine {
    /* 0x00 */ LINE_F2 m0_line;
    /* 0x10 */ SVECTOR m10_p0;
    /* 0x18 */ SVECTOR m18_p1;
} BattlingDebugLine; /* 0x20 */

/* 0x80095938: 100 timed effect lines (two LINE_F2, double-buffered). */
typedef struct BattlingEffectLine {
    /* 0x00 */ LINE_F2 m0_line[2];
    /* 0x20 */ SVECTOR m20_start;
    /* 0x28 */ SVECTOR m28_end;
    /* 0x30 */ s16 m30_timer;           /* 0 = free */
    /* 0x32 */ s16 m32;
} BattlingEffectLine; /* 0x34 */

/* Arena height map cell (*battlingHeightMap: 128x128, indexed by (x >> 8) +
 * (z >> 8) * 128): height in m0; bits 8-9 of m2 (0x03000000 of the word)
 * raise it by 0x100 (type 1) or 0x40 (type 3). */
typedef struct BattlingMapCell {
    /* 0x0 */ s16 m0_height;
    /* 0x2 */ s16 m2_flags;
} BattlingMapCell; /* 0x4 */

/* 0xC-byte debris particle of the array at *battlingSplashParticles (540 entries; each
 * has a TILE in battlingSplashTiles[2]). */
typedef struct BattlingDebris {
    /* 0x0 */ s16 m0_x;
    /* 0x2 */ s16 m2_y;
    /* 0x4 */ s16 m4_z;
    /* 0x6 */ s16 m6_life;
    /* 0x8 */ s8 m8_vx;                    /* x velocity */
    /* 0x9 */ s8 m9_vz;                    /* z velocity */
    /* 0xA */ s16 mA_vy;                   /* y velocity */
} BattlingDebris;

/* 0xA-byte dust particle of the array at *battlingDustParticles (255 entries; drawn
 * as TILE_1s in *battlingDustTiles[2]). */
typedef struct BattlingDust {
    /* 0x0 */ s16 m0_x;
    /* 0x2 */ s16 m2_y;
    /* 0x4 */ s16 m4_z;
    /* 0x6 */ s8 m6_life;
    /* 0x7 */ s8 m7_vy;
    /* 0x8 */ s16 m8_groundY;
} BattlingDust;

/* ---- font, text and menus (showBattlingAnalogWarning-updateBattlingGearGlowLights) ------------------- */

/* Font glyph (battlingFontGlyphs, indexed by getBattlingGlyph). */
typedef struct BattlingGlyph {
    /* 0x0 */ u8 m0_u;
    /* 0x1 */ u8 m1_v;
    /* 0x2 */ u8 m2_width;
    /* 0x3 */ u8 m3_height;
} BattlingGlyph;

/* Texture page + sprite pair (battlingAnalogWarningSprites[2]: AddPrim'd sprite then tpage). */
typedef struct BattlingSprtTPage {
    /* 0x00 */ DR_TPAGE m0_tpage;
    /* 0x08 */ SPRT m8_sprt;
} BattlingSprtTPage; /* 0x1C */

/* A one-line text label (image rendered by renderString, drawn as a sprite
 * per display buffer): battlingHelpLabels[2]. Set up by initBattlingHelpLabels (both share
 * one 0x6B4-byte heap buffer). */
typedef struct BattlingLabel {
    /* 0x00 */ u16* m0_image;           /* 4bpp line image (shared by both labels) */
    /* 0x04 */ SPRT m4_sprt[2];         /* per display buffer */
    /* 0x2C */ s16 m2C_width;           /* printed width */
    /* 0x2E */ s16 m2E_x;               /* centred x: (320 - width) / 2 */
} BattlingLabel; /* 0x30 */

/* A menu entry (0x14 bytes; tables at 0x800913B8-0x800915AC). */
typedef struct BattlingMenuItem {
    /* 0x00 */ u8 m0_flags;             /* 1: callback without confirm, 2: widest width, 4: skipped */
    /* 0x01 */ u8 m1_labelId;           /* help label text index (showBattlingHelpLabel) */
    /* 0x02 */ s16 m2;
    /* 0x04 */ char* m4_text;
    /* 0x08 */ void (*m8_callback)(s32 arg);
    /* 0x0C */ s32 mC_arg;              /* passed to m8_callback */
    /* 0x10 */ s16 m10;
    /* 0x12 */ s16 m12_halfWidth;
} BattlingMenuItem; /* 0x14 */

/* A menu (0x3C bytes; eight at battlingMenus; battlingCurrentMenu = current). */
typedef struct BattlingMenu {
    /* 0x00 */ u8 m0_leftMargin;        /* 0: items centred at x 160 */
    /* 0x01 */ u8 m1_pad[3];
    /* 0x04 */ BattlingMenuItem* m4_items;
    /* 0x08 */ s16 m8_count;
    /* 0x0A */ s16 mA_parent;           /* menu index to go back to (openBattlingMenu) */
    /* 0x0C */ void (*mC_draw)(struct BattlingMenu*);
    /* 0x10 */ s16 m10;
    /* 0x12 */ s16 m12_selected;
    /* 0x14 */ s16 m14_y;
    /* 0x16 */ s16 m16_x;
    /* 0x18 */ s32 m18;
    /* 0x1C */ TILE m1C_tiles[2];       /* background, per display buffer ([1] = copy of [0]) */
} BattlingMenu; /* 0x3C */

/* Gear portrait VRAM slots (*battlingPortraitSlots, 49 entries on a 7x7 grid built by
 * initBattlingPortraitSlots; each gear's CLUT + 8-bit image are LoadImage'd there). */
typedef struct BattlingGearPortrait {
    /* 0x00 */ RECT m0_clutRect;
    /* 0x08 */ RECT m8_imageRect;
    /* 0x10 */ s32 m10;
} BattlingGearPortrait; /* 0x14 */

/* Stage colour set (0x11 bytes, byte-aligned; table battlingStageColorSets indexed by
 * battlingStageColorIndex). */
typedef struct BattlingStageColors {
    /* 0x00 */ u8 m0_topR;
    /* 0x01 */ u8 m1_topG;
    /* 0x02 */ u8 m2_topB;
    /* 0x03 */ u8 m3;
    /* 0x04 */ u8 m4_backR;                   /* -> battlingBackColorR */
    /* 0x05 */ u8 m5_backG;                   /* -> battlingBackColorG */
    /* 0x06 */ u8 m6_backB;                   /* -> battlingBackColorB */
    /* 0x07 */ u8 m7;
    /* 0x08 */ u8 m8_farR;              /* fog far colour, bottom of the backdrop */
    /* 0x09 */ u8 m9_farG;
    /* 0x0A */ u8 mA_farB;
    /* 0x0B */ u8 mB;
    /* 0x0C */ u8 mC_r;                 /* setFogNearColor (model colour) */
    /* 0x0D */ u8 mD_g;
    /* 0x0E */ u8 mE_b;
    /* 0x0F */ u8 mF;
    /* 0x10 */ u8 m10_dim;              /* halves the gear glow lights */
} BattlingStageColors; /* 0x11 */

/* ---- display (allocBattlingLoadBuffer-initBattlingDispEnvs) --------------------------------- */

/* The two display buffers at 0x8009A0D8 (stride 0xF8; battlingDisplayBuffer = the
 * current one): draw/display env, a one-entry OT and the prims chained to
 * it, the draw area/offset and a clear tile. */
typedef struct BattlingDisplayBuffer {
    /* 0x00 */ DRAWENV m0_drawEnv;
    /* 0x5C */ DISPENV m5C_dispEnv;
    /* 0x70 */ OTag m70_ot;
    /* 0x74 */ OTag m74_sprtTag;         /* SPRT_16-style prim, len 3 */
    /* 0x78 */ u8 m78_r, m79_g, m7A_b, m7B_code;
    /* 0x7C */ s16 m7C_x, m7E_y;
    /* 0x80 */ u16 m80_uv;
    /* 0x82 */ u16 m82_clut;
    /* 0x84 */ u8 m84[0x4C];
    /* 0xD0 */ DR_AREA mD0_drawArea;
    /* 0xDC */ DR_OFFSET mDC_drawOffset;
    /* 0xE8 */ TILE mE8_clearTile;
} BattlingDisplayBuffer; /* 0xF8 */

/* 0x8009A2F8: HUD primitive block, double-buffered (x2 = 0x620). Left
 * half built from the battlingHudPoints point table, right half mirrored. */
typedef struct BattlingHudPrims {
    /* 0x000 */ DR_TPAGE m0_tpage;
    /* 0x008 */ DR_TPAGE m8_tpage;
    /* 0x010 */ LINE_F4 m10_frame[4];     /* merged in pairs (MargePrim) */
    /* 0x080 */ POLY_F4 m80_bars[6];
    /* 0x110 */ DR_TPAGE m110_tpage;
    /* 0x118 */ POLY_F4 m118_bars[6];     /* copy of m80_bars, grey */
    /* 0x1A8 */ POLY_F4 m1A8_bars[6];     /* copy of m80_bars, red */
    /* 0x238 */ POLY_F3 m238_tris[3];
    /* 0x274 */ POLY_F3 m274_trisMirror[3]; /* mirror of m238_tris */
    /* 0x2B0 */ POLY_F4 m2B0_quads[4];
} BattlingHudPrims; /* 0x310 */

/* 0x8009A928: up to 31 debug axis markers (count battlingDebugMarkerCount): three lines
 * (x red, y green, z blue) per buffer around m60_pos. */
typedef struct BattlingMarker {
    /* 0x00 */ LINE_F2 m0_lines[2][3];
    /* 0x60 */ SVECTOR m60_pos;
} BattlingMarker; /* 0x68 */

/* ---- sound (func_8008E64C-) ----------------------------------------------- */

/* Positional sound channels battlingSoundChannels[4] (0x24 bytes each). */
typedef struct BattlingSoundChannel {
    /* 0x00 */ u8 m0_mode;              /* 0: plain, 1: fixed position m10, 2: follows *m20 */
    /* 0x01 */ u8 m1_active;
    /* 0x02 */ u8 m2_voice;             /* channel * 2 */
    /* 0x03 */ u8 m3_tag;
    /* 0x04 */ u16 m4_age;              /* frames since start, saturates at 0xFFFF */
    /* 0x06 */ u16 m6;
    /* 0x08 */ s32 m8_soundId;
    /* 0x0C */ u32 mC_voiceMask;
    /* 0x10 */ VECTOR m10_position;
    /* 0x20 */ VECTOR* m20_follow;
} BattlingSoundChannel; /* 0x24 */

#ifdef PLATFORM_PC
/* ---- the wide picture (port/src/game/battling/wide.c, include/pc_wide.h) --- */
void pcWideBattlingGroundCone(s32 x, s32 z, s32 angle, s32 narrow);
void pcWideBattlingGround(OTag* ot, s32 camX, s32 camZ);
void pcWideBattlingFireReset(void);
void pcWideBattlingFireUpdate(const s16* game);
void pcWideBattlingFire(OTag* ot, s32 c, s32 tpage);
void pcWideBattlingCapture(void);
void pcWideBattlingBlur(const u16* game);
void pcWideBattlingDemo(OTag* ot);
void pcWideBattlingHud(OTag* ot);
void pcWideBattlingGlyphs(s32 first, s32 count, s32 side);
void pcWideBattlingCursor(void* prim, s32 x, s32 y);
#endif

#endif /* BATTLING_BATTLING_H */
