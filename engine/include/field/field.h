#ifndef FIELD_FIELD_H
#define FIELD_FIELD_H

/* Field overlay structures (Noah: NoahLib/field/field.h). Partial views:
 * only the fields the matched code touches are named, the rest is padding. */

#include "common.h"
#include "psyq.h"
#include "main/sprite.h"
#include "main/kernel.h"
#include "field_mecha/mecha.h"

/* Noah: sFieldScriptEntityScriptSlot. */
typedef struct {
    /* 0x0 */ u16 m0_scriptPC;
    /* 0x2 */ u8 m2_delay;
    /* 0x3 */ u8 m3_scriptIndex; /* 0xFF: none */
    /* 0x4 */ u32 m4_x0_numSteps : 16;
              u32 m4_x10_status : 2;
              u32 m4_x12_priority : 4; /* Noah: _3C0000; 0xF = free slot */
              u32 m4_x16 : 1;
              u32 m4_x17_walkMode : 2;
              u32 m4_x19 : 1;
              u32 m4_x1A : 1;
              u32 m4_x1B : 2;
              u32 m4_x1D : 3;
} sFieldScriptEntityScriptSlot; /* size 0x8 */

/* Noah: sFieldScriptEntity. */
typedef struct sFieldScriptEntity {
    /* 0x000 */ u32 m0_fieldScriptFlags; /* bit 0: update script disabled */
    /* 0x004 */ u32 m4_flags;
    /* 0x008 */ s16 m8_currentWalkMeshTriangle[4];
    /* 0x010 */ s16 m10_walkmeshId;
    /* 0x012 */ u16 m12;
    /* 0x014 */ u32 m14_currentTriangleFlag;
    /* 0x018 */ s16 m18_boundingVolume[3];
    /* 0x01E */ u16 m1E_collisionRadius; /* always read with lhu */
    /* 0x020 */ VECTOR m20_position;
    /* 0x030 */ VECTOR m30_stepVector;
    /* 0x040 */ VECTOR m40_externalStep;
    /* 0x050 */ VECTOR m50_surfaceNormal;
    /* 0x060 */ SVECTOR m60_interactionOffset;
    /* 0x068 */ s16 m68_oldPosition[3];
    /* 0x06E */ s16 m6E_stuckCounter;
    /* 0x070 */ s16 m70_rotationForRendering;
    /* 0x072 */ s16 m72_elevation;
    /* 0x074 */ u8 m74_touchedActor; /* 0xFF: none */
    /* 0x075 */ u8 m75_parentActorId; /* 0xFF: none (stored with ori 0xFF) */
    /* 0x076 */ u16 m76_timeScale; /* walk speed divisor (OP_SET_TIME_SCALE); always read with lhu */
    /* 0x078 */ u16 m78_stack[4];
    /* 0x080 */ u8 m80_dialogPortrait; /* 0xFF: none (lbu; stored with ori 0xFF) */
    /* 0x081 */ u8 m81_selectedMultichoiceLine; /* 0xFF: none (lbu, stored with ori 0xFF) */
    /* 0x082 */ u8 m82_dialogWindowSize[2]; /* lbu */
    /* 0x084 */ s32 m84_dialogWindowFlags;
    /* 0x088 */ s16 m88_dialogWindowPosition[2];
    /* 0x08C */ sFieldScriptEntityScriptSlot m8C_scriptSlots[8];
    /* 0x0CC */ u16 mCC_scriptPC;
    /* 0x0CE */ u8 mCE_currentScriptSlot;
    /* 0x0CF */ u8 mCF_scriptSlotWaitedOn;
    /* 0x0D0 */ VECTOR mD0_targetPositionOffset;
    /* 0x0E0 */ s16 mE0_rotationLimit;
    /* 0x0E2 */ u8 mE2_doorFrame;  /* door animation counter (OP_OPEN_DOOR/OP_CLOSE_DOOR; lbu + sltiu) */
    /* 0x0E3 */ u8 mE3_pushCounter;  /* counter (lbu + sltiu in updateEntityEventCode3) */
    /* 0x0E4 */ s16 mE4_playableCharacterId;
    /* 0x0E6 */ s16 mE6_idleAnimationId;
    /* 0x0E8 */ s16 mE8_currentAnimationId;
    /* 0x0EA */ s16 mEA_forcedAnimation;
    /* 0x0EC */ s16 mEC_elevation;
    /* 0x0EE */ s16 mEE_spriteSplitHeight;
    /* 0x0F0 */ s32 mF0_fallSpeed;
    /* 0x0F4 */ s16 mF4_scale3d[3];
    /* 0x0FA */ s16 mFA;
    /* 0x0FC */ u8 mFC_spriteColor[3];
    /* 0x0FF */ u8 mFF_spriteColor2[3];
    /* 0x102 */ s16 m102_numSteps;
    /* 0x104 */ s16 m104_rotation;
    /* 0x106 */ s16 m106_currentRotation;
    /* 0x108 */ s16 m108_rotation3;
    /* 0x10A */ u16 m10A_soundId;
    /* 0x10C */ u8 m10C_soundVolume;
    /* 0x10D */ u8 m10D_soundMode;  /* 0xFF stored with ori */
    /* 0x10E */ s16 m10E;
    /* 0x110 */ s16* m110_modelRotation2;
    /* 0x114 */ Vec2s16* m114_movementBoundingZone; /* 4 corners (heap) */
    /* 0x118 */ s32* m118_deformationStrength;
    /* 0x11C */ s16 m11C_lockedRotation;
    /* 0x11E */ s16 m11E_rotationSpeed;
    /* 0x120 */ void* m120_special2dAnimation;
    /* 0x124 */ s16 m124_special2dAnimationId;
    /* 0x126 */ u8 m126_spriteSource;
    /* 0x127 */ u8 m127_vramSlot;
    /* 0x128 */ u16 m128_attachedMechaBone; /* 0xFFFF stored with ori */
    /* 0x12A */ s16 m12A;
    /* 0x12C */ u32 m12C_x0 : 2;
                u32 m12C_x2 : 3;
                u32 m12C_x5_doorMoving : 1; /* OP_OPEN_DOOR/OP_CLOSE_DOOR */
                u32 m12C_x6_stackDepth : 3; /* OP_CALL/OP_RETURN stack (m78) */
                u32 m12C_x9 : 3;
                u32 m12C_xC : 1;
                u32 m12C_xD : 3;
                u32 m12C_x10 : 2;
                u32 m12C_x12 : 10;
                u32 m12C_x1C : 4;
    /* 0x130 */ u32 m130_x0 : 9;
                u32 m130_x9 : 10;
                u32 m130_x13 : 9;
                u32 m130_x1C : 2; /* Noah: m130.m28 */
                u32 m130_x1E : 2;
    /* 0x134 */ u32 m134_x0 : 4;  /* Noah: m134.m0 */
                u32 m134_x4 : 1;  /* Noah: m134.m4 */
                u32 m134_x5 : 2;
                u32 m134_x7 : 1;  /* Noah: m7_isModelRotation2Allocated */
                u32 m134_x8 : 24;
} sFieldScriptEntity; /* size 0x138 */

/* A party character's sprite bundle buffer (Noah: partyCharacterBuffersRaw,
 * copied as one 0x14000-byte struct). */
typedef struct {
    u32 m0[0x14000 / 4];
} sPartyCharacterBuffer; /* size 0x14000 */

/* Light and model block layouts, as main's light.c / model.c use them. */
/* Light parameters passed to MulRotationMatrix (same layout as src/main/light.c) */
typedef struct {
    /* 0x00 */ s32 m0_x;
    /* 0x04 */ s32 m4_y;
    /* 0x08 */ s32 m8_z;
    /* 0x0C */ u16 mC_r;
    /* 0x0E */ u16 mE_g;
    /* 0x10 */ u16 m10_b;
} LightDef; /* 0x14 */

/* ModelBlock: include/main/model.h */

/* Noah: sFieldEntitySub0 */
typedef struct sFieldEntitySub0 {
    /* 0x00 */ s32 m0;
    /* 0x04 */ ModelBlock* m4_pModelBlock;
    /* 0x08 */ u8* m8_primBuffers[2];
    /* 0x10 */ s16 m10;
    /* 0x12 */ s16 m12_renderMode;
    /* 0x14 */ void* m14_dynamicVertices;
    /* 0x18 */ SVECTOR m18_boundingCenter;
    /* 0x20 */ s16 m20_boundingRadius;
} sFieldEntitySub0; /* 0x24 */

/* Noah: sFieldEntity (size 0x5C), actorArray element. */
typedef struct sFieldEntity {
    /* 0x00 */ sFieldEntitySub0* m0_model;
    /* 0x04 */ SpriteActorCore* m4_pVramSpriteSheet;
    /* 0x08 */ void* m8_2dSprite;
    /* 0x0C */ MATRIX mC_matrix;
    /* 0x2C */ MATRIX m2C_matrixBackup;
    /* 0x4C */ sFieldScriptEntity* m4C_scriptEntity;
    /* 0x50 */ SVECTOR m50_modelRotation;
    /* 0x58 */ u16 m58_flags;
    /* 0x5A */ u16 m5A_spriteFlags;
} sFieldEntity; /* size 0x5C */

/* Field state block from 0x800AFA54 (partial view). The original kept these
 * in one aggregate: GCC addresses 0x800AFA54 as &totalObjects - 0xB8
 * (restoreFieldStateFromSaveBlock), and it reloads the actor array pointer after every store
 * to a struct field (runFieldResumeScript), which it only does for a struct member. */
/* The part of the field state kept in the save block (0x74 bytes; restored
 * with memcpy by loadFieldStateFromSaveBlock/restoreFieldStateFromSaveBlock). */
typedef struct {
    /* 0x00 */ s16 m0[0x3A];
} sFieldStateSaved; /* size 0x74 */

/* On the PC this view starts 4 bytes past an 8-byte boundary of
 * FieldBss_800AF880, so it is packed to 4 to put its pointers where that
 * struct has them. */
#pragma pack(push, 4)
typedef struct {
    /* 0x00 */ sFieldStateSaved m0_saved;     /* 0x800AFA54 */
    /* 0x74 */ u8 m74[0x44];
    /* 0xB8 */ s32 mB8_numObjects;            /* 0x800AFB0C: actors + other objects */
    /* 0xBC */ sFieldEntity* mBC_actorArray;  /* 0x800AFB10, Noah: actorArray */
    s32* mC0_models;                          /* FieldBss_800AF880.m294_models */
    s32* mC4_walkMesh;                        /* FieldBss_800AF880.m298_walkMesh */
    /* 0xC8 */ u8* mC8_actorSetupParams;      /* 0x800AFB1C: bundle, offsets at +4 (Noah: fieldActorSetupParams) */
} sFieldState;
#pragma pack(pop)
/* declared in the files that use this view: extern sFieldState D_800AFA54; */

/* Noah: sParticle (size 0xC0) */
typedef struct sParticle {
    /* 0x00 */ s16 m0_isActive;
    /* 0x02 */ u16 m2_life2; /* lhu (updateParticle) */
    /* 0x04 */ u16 m4_life;  /* lhu (updateParticle) */
    /* 0x06 */ s16 m6_rotationAngle;
    /* 0x08 */ VECTOR m8_position;
    /* 0x18 */ VECTOR m18_positionStep;
    /* 0x28 */ VECTOR m28_positionStep2;
    /* 0x38 */ SVECTOR m38_scale;
    /* 0x40 */ SVECTOR m40_scaleStep;
    /* 0x48 */ u8 m48_color[4];
    /* 0x4C */ s8 m4C_colorStep[4];
    /* 0x50 */ POLY_FT4 m50_poly[2];
    /* 0xA0 */ SVECTOR mA0_worldspaceVertices[4];
} sParticle; /* size 0xC0 */

/* Noah: sParticleConfig (size 0x78) */
typedef struct sParticleConfig {
    /* 0x00 */ s16 m0;
    /* 0x02 */ u16 m2_sWait; /* lhu (updateParticles) */
    /* 0x04 */ u16 m4_eWait; /* lhu (updateParticles) */
    /* 0x06 */ s16 m6_maxParticles;
    /* 0x08 */ s32 m8_speed;
    /* 0x0C */ SVECTOR mC_sPos;
    /* 0x14 */ SVECTOR m14_ePos;
    /* 0x1C */ SVECTOR m1C_gravity;
    /* 0x24 */ s16 m24_speedMultiplier;
    /* 0x26 */ u16 m26_sRange; /* lhu (spawnParticle) */
    /* 0x28 */ u16 m28_eRange; /* lhu (spawnParticle) */
    /* 0x2A */ u16 m2A_flags;
    /* 0x2C */ sParticle* m2C_particles;
    /* 0x30 */ Vec2s16 m30_angleOffsets[8];
    /* 0x50 */ s16 m50_attachScale;
    /* 0x52 */ s16 m52_actorId;
    /* 0x54 */ s16 m54_shape;
    /* 0x56 */ u16 m56_psWait; /* lhu (spawnParticle) */
    /* 0x58 */ s16 m58_peWait;
    /* 0x5A */ SVECTOR m5A_scale;
    /* 0x62 */ SVECTOR m62_scaleOffset;
    /* 0x6A */ u8 m6A_color[4];
    /* 0x6E */ s8 m6E_colorOffset[4];
    /* 0x72 */ s16 m72_mechaIndex;
    /* 0x74 */ s16 m74_boneIndex;
    /* 0x76 */ s16 m76_rotationAngle;
} sParticleConfig; /* size 0x78 */

/* Noah: sParticleEffect (size 0x3C0) */
typedef struct sParticleEffect {
    /* 0x000 */ sParticleConfig m0_configs[8];
} sParticleEffect; /* size 0x3C0 */

extern sParticleEffect particleCreationSetup;

/* Noah: sFieldRenderContext (size 0x80F4; fieldRenderContext[2] at
 * 0x800B249C). */
typedef struct sFieldRenderContext {
    /* 0x0000 */ DRAWENV m0_drawEnv;
    /* 0x005C */ DRAWENV m5C_backgroundRect;
    /* 0x00B8 */ DISPENV mB8_displayEnv;
    /* 0x00CC */ OTag mCC_OT[0x1001];
    /* 0x40D0 */ OTag m40D0_secondaryOT[0x1001];
    /* 0x80D4 */ OTag m80D4_uiOT[8];
} sFieldRenderContext; /* size 0x80F4 */

/* Mecha globals at 0x800B21DC (field bss). Retail addresses these fields
 * relative to each other (e.g. &numMechas - 0x88 == mechaList), so they are
 * one object. Names from Noah (field/mecha/mechaOverlay.cpp). */
typedef struct {
    /* 0x00 */ s16 m0_mechaList[4];            /* 0x800B21DC: mecha id * 2 */
    /* 0x08 */ s16 m8_mechaCurrentAnimation[4]; /* 0x800B21E4 */
    /* 0x10 */ SVECTOR m10_initMechaTempVar[4]; /* 0x800B21EC */
    /* 0x30 */ s32 m30_mechaList3[4];           /* 0x800B220C */
    /* 0x40 */ MATRIX m40_mechaFieldArgs2[2];   /* 0x800B221C */
    /* 0x80 */ u8 m80_mechaBackColor[3];        /* 0x800B225C */
    /* 0x83 */ u8 m83_mechaList2[4];            /* 0x800B225F */
    /* 0x88 */ s32 m88_numMechas;               /* 0x800B2264 */
} FieldMechaState; /* size >= 0x8C */

/* Noah: sDialogWindow (field/dialogWindows.h), gDialogWindows[4] at
 * 0x800C2698 (US). Noah's flat fields mAC_Position..m3E0_Sprt are three
 * sub-structs here: retail builds their addresses from bases at window+0xAC,
 * +0x37C and +0x3C4 (e.g. &m3C8[odd] is (sym + 0x3C4 + i * 0x498) +
 * (odd * 12 + 4)), which only nested structs reproduce. */
#include "main/dialog.h"

/* The window frame: position, size, background tile and the 8 border
 * sprites (10 slots). */
typedef struct {
    /* 0x000 */ s16 m0_x;                            /* Noah: mAC_Position */
    /* 0x002 */ s16 m2_y;
    /* 0x004 */ s16 m4_width;                        /* Noah: mB0_Size */
    /* 0x006 */ s16 m6_height;
    /* 0x008 */ u8 m8[0x10];
    /* 0x018 */ DR_MODE m18_drawMode[2];             /* Noah: mC4 */
    /* 0x030 */ TILE m30_backgroundTile[2];          /* Noah: mDC_backgroundTile */
    /* 0x050 */ DR_MODE m50_borderDrawModes[2][10];  /* Noah: mFC */
    /* 0x140 */ SPRT m140_borders[2][10];            /* Noah: m1EC_windowBorders */
} DialogWindowFrame; /* size 0x2D0 */

/* The multiple-choice cursor (m0_state 0: choosing, -1: none). */
typedef struct {
    /* 0x00 */ s16 m0_state;                         /* Noah: m37C */
    /* 0x02 */ s16 m2_startOfMultiChoice;            /* Noah: m37E_startOfMultiChoice */
    /* 0x04 */ s16 m4_numMaxMultiChoiceSelection;    /* Noah: m380_numMaxMultiChoiceSelection */
    /* 0x06 */ s16 m6_currentMultiChoiceSelection;   /* Noah: m382_CurrentMultiChoiceSelection */
    /* 0x08 */ DR_MODE m8_drawMode[2];               /* Noah: m384 */
    /* 0x20 */ SPRT m20_sprt[2];                     /* Noah: m39C_Sprt */
} DialogWindowCursor; /* size 0x48 */

/* The animated "waiting for a button" icon at the text cursor (m0_state 0:
 * shown, -1: hidden). */
typedef struct {
    /* 0x00 */ s16 m0_state;                         /* Noah: m3C4 */
    /* 0x04 */ DR_MODE m4_drawMode[2];               /* Noah: m3C8 */
    /* 0x1C */ SPRT m1C_sprt[2];                     /* Noah: m3E0_Sprt */
} DialogWindowWaitIcon; /* size 0x44 */

typedef struct {
    /* 0x000 */ DR_MODE m0_drawMode[2];
    /* 0x018 */ DialogWindow18 m18_text;
    /* 0x0A8 */ u8* mA8_dialogPointer;   /* Noah: m18.m90_dialogPointer */
    /* 0x0AC */ DialogWindowFrame mAC_frame;
    /* 0x37C */ DialogWindowCursor m37C_cursor;
    /* 0x3C4 */ DialogWindowWaitIcon m3C4_waitIcon;
    /* 0x408 */ s16 m408_openAnimationCounter;
    /* 0x40A */ s16 m40A_waitIconDelay;
    /* 0x40C */ u16 m40C_flags;
    /* 0x40E */ s16 m40E_state;
    /* 0x410 */ u16 m410_priority;                /* priority, 0xFFFF: free */
    /* 0x412 */ s16 m412_onTop;
    /* 0x414 */ s16 m414_keepOpen;
    /* 0x416 */ s16 m416_fieldActorId;
    /* 0x418 */ s16 m418_actorId;
    /* 0x41C */ s32 m41C_openOffset[2];
    /* 0x424 */ s32 m424_openOffsetStep[2];
    /* 0x42C */ DR_MODE m42C_portraitDrawMode[2];
    /* 0x444 */ POLY_FT4 m444_portraitPoly[2];
    /* 0x494 */ u8 m494_hasPortrait;
    /* 0x495 */ u8 m495_portrait;
} DialogWindow; /* size 0x498 */

/* ---- Field data layouts and .bss aggregates (region FA) ---- */

/* Noah: fieldVramMapping entry */
typedef struct {
    /* 0x0 */ u16 m0_vramX;
    /* 0x2 */ u16 m2_vramY;
    /* 0x4 */ s16 m4;
    /* 0x6 */ s16 m6;
} FieldVramMapping;

/* Copied as one 2-aligned block (hence the runtime alignment test). */
typedef struct {
    FieldVramMapping m[32];
} FieldVramMappingTable; /* 0x100 */

extern FieldVramMappingTable fieldVramMapping;

/* Noah: rawFieldBundle header (sizes of the packed sections, then their
 * offsets from the bundle start) */
typedef struct {
    /* 0x000 */ FieldVramMappingTable m0_vramMapping;
    /* 0x100 */ s32 m100[3];
    /* 0x10C */ s32 m10C_imagesSize;
    /* 0x110 */ s32 m110_walkMeshSize;
    /* 0x114 */ s32 m114_modelsSize;
    /* 0x118 */ s32 m118_actorSetupSize;
    /* 0x11C */ s32 m11C_images2Size;
    /* 0x120 */ s32 m120_scriptSize;
    /* 0x124 */ s32 m124_encounterSize;
    /* 0x128 */ s32 m128_dialogSize;
    /* 0x12C */ s32 m12C_triggerSize;
    /* 0x130 */ s32 m130_imagesOffset;
    /* 0x134 */ s32 m134_walkMeshOffset;
    /* 0x138 */ s32 m138_modelsOffset;
    /* 0x13C */ s32 m13C_actorSetupOffset;
    /* 0x140 */ s32 m140_images2Offset;
    /* 0x144 */ s32 m144_scriptOffset;
    /* 0x148 */ s32 m148_encounterOffset;
    /* 0x14C */ s32 m14C_dialogOffset;
    /* 0x150 */ s32 m150_triggerOffset;
    /* 0x154 */ s16 m154_3dSetup[0x1C];
    /* 0x18C */ u16 m18C_numObjects;
    /* 0x18E */ u16 m18E;
    /* 0x190 */ u16 m190_objects[1]; /* 8 u16 per object */
} FieldBundle;

/* Decompressed field script file (Noah: rawFieldScriptData) */
typedef struct {
    u8 m0[0x40];
} FieldScriptActorEntry;

typedef struct {
    /* 0x00 */ u8 m0_unsignedVarMask[0x80];
    /* 0x80 */ s32 m80_numActors;                       /* Noah: g_totalActors */
    /* 0x84 */ FieldScriptActorEntry m84_actors[1];     /* then the script code */
} FieldScriptData;

extern FieldScriptData* rawFieldScriptData;

/* Noah: mechaFieldArgs2 entry (std::array<MATRIX, 2>; initMechaFieldArgs2 fills it) */
typedef MATRIX MechaFieldArgs2; /* 0x20 */

/* A textured quad: model-space corners and a double-buffered POLY_FT4
 * (Noah: sFieldCompassVar2). */
typedef struct {
    /* 0x00 */ SVECTOR m0_vertices[4];
    /* 0x20 */ POLY_FT4 m20_poly[2];
} FieldQuad; /* size 0x70 */

extern FieldQuad fieldCompassVar2[25]; /* [0x15..0x18]: the shadow quads */
extern u16 compassData2[16];            /* compass CLUT row */

/* Field globals 0x800AF880-0x800AFB56 (one aggregate, see above). */
typedef struct {
    /* 0x000 */ VECTOR m0_cameraEye;          /* 0x800AF880 Noah: g_cameraEye */
    /* 0x010 */ VECTOR m10_cameraAt;          /* 0x800AF890 Noah: g_cameraAt */
    /* 0x020 */ VECTOR m20_cameraUp;          /* 0x800AF8A0 Noah: g_cameraUp */
    /* 0x030 */ VECTOR m30_cameraEye2;        /* 0x800AF8B0 Noah: g_cameraEye2 */
    /* 0x040 */ VECTOR m40_cameraAt2;         /* 0x800AF8C0 Noah: g_cameraAt2 */
    /* 0x050 */ VECTOR m50_cameraUp2;         /* 0x800AF8D0 (vy = 0x10000000 like cameraUp) */
    /* 0x060 */ VECTOR m60_cameraOffset;      /* 0x800AF8E0 Noah: g_fieldCameraOffset (shake) */
    /* 0x070 */ s32 m70_cameraTargetOverride[3];   /* 0x800AF8F0 Noah: cameraTargetOverride */
    /* 0x07C */ s32 m7C;
    /* 0x080 */ s32 m80_desiredCameraTarget[3];    /* 0x800AF900 Noah: desiredCameraTarget */
    /* 0x08C */ s32 m8C;
    /* 0x090 */ s32 m90_cameraPositionOverride[3]; /* 0x800AF910 Noah: cameraPositionOverride */
    /* 0x09C */ s32 m9C;
    /* 0x0A0 */ s32 mA0_desiredCameraPosition[3];  /* 0x800AF920 Noah: desiredCameraPosition */
    /* 0x0AC */ s32 mAC;
    /* 0x0B0 */ s32 mB0_op99Var1;             /* 0x800AF930 Noah: op99Var1 */
    /* 0x0B4 */ s16 mB4_cameraInterpolationMode; /* 0x800AF934 Noah: cameraInterpolationMode */
    /* 0x0B6 */ s16 mB6_op99Var2;             /* 0x800AF936 Noah: op99Var2 */
    /* 0x0B8 */ s16 mB8_op99Var3;             /* 0x800AF938 Noah: op99Var3 */
    /* 0x0BA */ s16 mBA_op99Var4;                      /* 0x800AF93A = 0x1000 (Noah: op99Var4) */
    /* 0x0BC */ u16 mBC_cameraInterpolationFlags; /* 0x800AF93C Noah: cameraInterpolationFlags */
    /* 0x0BE */ s16 mBE_cameraInterpolationTargetNumSteps; /* 0x800AF93E Noah: cameraInterpolationTargetNumSteps */
    /* 0x0C0 */ s32 mC0_cameraInterpolationTargetStartPosition[3]; /* 0x800AF940 */
    /* 0x0CC */ s32 mCC;
    /* 0x0D0 */ s32 mD0_cameraInterpolationTargetStep[3]; /* 0x800AF950 */
    /* 0x0DC */ s32 mDC;
    /* 0x0E0 */ s16 mE0_cameraInterpolationPositionNumSteps; /* 0x800AF960 (Noah: s32; retail uses lh/sh) */
    /* 0x0E2 */ s16 mE2;
    /* 0x0E4 */ s32 mE4_cameraInterpolationStartPosition[3]; /* 0x800AF964 */
    /* 0x0F0 */ s32 mF0;
    /* 0x0F4 */ s32 mF4_cameraInterpolationPositionStep[3]; /* 0x800AF974 */
    /* 0x100 */ s32 m100;
    /* 0x104 */ s32 m104_op99Var5;            /* 0x800AF984 Noah: op99Var5 (camera at speed) */
    /* 0x108 */ s32 m108_op99Var6;            /* 0x800AF988 Noah: op99Var6 (camera eye speed) */
    /* 0x10C */ s16 m10C_camera2Tan;          /* 0x800AF98C Noah: camera2Tan */
    /* 0x10E */ s16 m10E_cameraTan;           /* 0x800AF98E Noah: cameraTan */
    /* 0x110 */ MATRIX m110_cameraMatrix;     /* 0x800AF990 Noah: g_cameraMatrix */
    /* 0x130 */ MATRIX m130_projectionTempMatrix; /* 0x800AF9B0 Noah: computeProjectionMatrixTempMatrix */
    /* 0x150 */ SVECTOR m150_projectionAngles; /* 0x800AF9D0 Noah: computeProjectionMatrixAngles */
    /* 0x158 */ s32 m158_op99Var7;            /* 0x800AF9D8 Noah: op99Var7 (camera flags) */
    /* 0x15C */ s16 m15C_cameraLimits[4];     /* 0x800AF9DC Noah: cameraLimits */
    /* 0x164 */ SVECTOR m164_cameraRotationBetweenEyeAndAt; /* 0x800AF9E4 Noah: cameraRotationBetweenEyeAndAt */
    /* 0x16C */ s32 m16C_cameraDollyVar1;     /* 0x800AF9EC Noah: cameraDollyVar1 */
    /* 0x170 */ s32 m170_opA0_var1;           /* 0x800AF9F0 Noah: opA0_var1 */
    /* 0x174 */ u8 m174_dollySet;             /* 0x800AF9F4 Noah: DollySet? (opcode 0x97) */
    /* 0x175 */ u8 m175_dollyStop;            /* 0x800AF9F5 Noah: DollyStop */
    /* 0x176 */ s16 m176_cameraDollyVar0;     /* 0x800AF9F6 Noah: cameraDollyVar0 */
    /* 0x178 */ s32 m178_sceneSCRZ;           /* 0x800AF9F8 Noah: sceneSCRZ */
    /* 0x17C */ s16 m17C_sceneDIP;            /* 0x800AF9FC Noah: sceneDIP */
    /* 0x17E */ s16 m17E_sceneScale;          /* 0x800AF9FE Noah: sceneScale */
    /* 0x180 */ s16 m180_OP_A4Var0;           /* 0x800AFA00 Noah: OP_A4Var0 */
    /* 0x182 */ s16 m182;
    /* 0x184 */ s32 m184_OP_A4Var1;           /* 0x800AFA04 Noah: OP_A4Var1 */
    /* 0x188 */ s32 m188_OP_A4Var2;           /* 0x800AFA08 Noah: OP_A4Var2 */
    /* 0x18C */ s32 m18C_opA0_var0;           /* 0x800AFA0C Noah: opA0_var0 */
    /* 0x190 */ s16 m190_OP_B6SubVar0;        /* 0x800AFA10 Noah: OP_B6SubVar0 */
    /* 0x192 */ s16 m192;
    /* 0x194 */ s32 m194_OP_B6Var2;           /* 0x800AFA14 Noah: OP_B6Var2 (s16 in Noah) */
    /* 0x198 */ s32 m198_OP_B6Var1;           /* 0x800AFA18 Noah: OP_B6Var1 (s16 in Noah) */
    /* 0x19C */ s16 m19C_op9DVar0;            /* 0x800AFA1C Noah: op9DVar0 */
    /* 0x19E */ s16 m19E;
    /* 0x1A0 */ s32 m1A0_op9DVar3;            /* 0x800AFA20 Noah: op9DVar3 */
    /* 0x1A4 */ s32 m1A4_op9DVar2;            /* 0x800AFA24 Noah: op9DVar2 */
    /* 0x1A8 */ s16 m1A8_cameraShakeEnabled;  /* 0x800AFA28 Noah: cameraShakeEnabled */
    /* 0x1AA */ s16 m1AA_cameraShakeCounter;  /* 0x800AFA2A */
    /* 0x1AC */ s16 m1AC_cameraShakeStopping;                     /* 0x800AFA2C */
    /* 0x1AE */ s16 m1AE;
    /* 0x1B0 */ s32 m1B0_cameraShakeAmplitude[3]; /* 0x800AFA30 (high halves used as rand() scale) */
    /* 0x1BC */ s32 m1BC_cameraShakeStep[3];  /* 0x800AFA3C */
    /* 0x1C8 */ s16 m1C8_savedCameraDirection;                     /* 0x800AFA48 saved camera direction (opcode 0x9E) */
    /* 0x1CA */ s16 m1CA;
    /* 0x1CC */ s32 m1CC_savedSceneSCRZ;                     /* 0x800AFA4C saved sceneSCRZ (opcode 0x9E) */
    /* 0x1D0 */ s16 m1D0_savedSceneDIP;                     /* 0x800AFA50 saved sceneDIP (opcode 0x9E) */
    /* 0x1D2 */ s16 m1D2;
    /* 0x1D4 */ SVECTOR m1D4_projectionAngles; /* 0x800AFA54 Noah: g_cameraProjectionAngles */
    /* 0x1DC */ SVECTOR m1DC_cameraRotation;  /* 0x800AFA5C Noah: g_cameraRotation */
    /* 0x1E4 */ MATRIX m1E4_projectionMatrix; /* 0x800AFA64 Noah: g_currentProjectionMatrix */
    /* 0x204 */ MATRIX m204_worldScaleMatrix; /* 0x800AFA84 Noah: worldScaleMatrix */
    /* 0x224 */ MATRIX m224_projectionTempMatrix; /* 0x800AFAA4 Noah: computeProjectionMatrixTempMatrix2 */
    /* 0x244 */ s32 m244_worldScaleFactor;    /* 0x800AFAC4 Noah: worldScaleFactor */
    /* 0x248 */ LightDef m248_lights[3];      /* 0x800AFAC8 */
    /* 0x284 */ s16 m284_ambient[3];          /* 0x800AFB04 */
    /* 0x28A */ s16 m28A;
    /* 0x28C */ s32 m28C_totalObjects;        /* 0x800AFB0C Noah: totalObjects */
    /* 0x290 */ sFieldEntity* m290_actorArray; /* 0x800AFB10 Noah: actorArray */
    /* 0x294 */ s32* m294_models;             /* 0x800AFB14 Noah: rawFieldModels */
    /* 0x298 */ s32* m298_walkMesh;           /* 0x800AFB18 Noah: g_walkMesh */
    /* 0x29C */ s32* m29C_actorSetupParams;   /* 0x800AFB1C Noah: rawFieldActorSetupParams */
    /* 0x2A0 */ s32* m2A0_walkMeshVar1;       /* 0x800AFB20 Noah: walkMeshVar1 */
    /* 0x2A4 */ s32* m2A4_walkMeshTriangle[4]; /* 0x800AFB24 */
    /* 0x2B4 */ s32* m2B4_walkMeshVertices[4]; /* 0x800AFB34 */
    /* 0x2C4 */ s32 m2C4_walkMeshNumTrianglePerBlock[4]; /* 0x800AFB44 */
    /* 0x2D4 */ s16 m2D4_numWalkMesh;         /* 0x800AFB54 Noah: g_numWalkMesh */
} FieldBss_800AF880;

extern s32* walkMeshVar1; /* = FieldBss_800AF880.m2A0 */

/* Background-poly parameters set by script opcodes EX 0x80-0x82
 * (0x800B0080-0x800B00B4). */
typedef struct {
    /* 0x00 */ s16 m0_OPX_80Params[8];        /* 0x800B0080 */
    /* 0x10 */ s32 m10_OPX_81Params[3];       /* 0x800B0090 */
    /* 0x1C */ s32 m1C;
    /* 0x20 */ u8 m20_OPX_82Param0[3][4];     /* 0x800B00A0 (R, G, B, pad) */
    /* 0x2C */ s16 m2C_OPX_82Param3[3];       /* 0x800B00AC */
    /* 0x32 */ s16 m32_OPX_82Param4;          /* 0x800B00B2 */
} FieldBss_800B0080;

extern FieldBss_800B0080 OPX_80Params;

/* Field globals 0x800B2184-0x800B2359 (one aggregate, see above). */
typedef struct {
    /* 0x000 */ SVECTOR m0_renderModelRotationAngles; /* 0x800B2184 Noah: renderModelRotationAngles */
    /* 0x008 */ s16 m8_entityMoveSpeedFactor;              /* 0x800B218C = 0x1000 (Noah: entityMoveSpeedFactor?) */
    /* 0x00A */ s16 mA_isFogSetup;              /* 0x800B218E */
    /* 0x00C */ u8 mC_fogNearColor[3];            /* 0x800B2190 = 0x80 */
    /* 0x00F */ u8 mF;
    /* 0x010 */ u8 m10_fogFarColor[3];           /* 0x800B2194 = 0xFF */
    /* 0x013 */ u8 m13;
    /* 0x014 */ s16 m14_fogNear;             /* 0x800B2198 = 0x15E0 */
    /* 0x016 */ s16 m16_fogFar;             /* 0x800B219A = 0x300C */
    /* 0x018 */ u8 m18_backgroundClearColor[4];           /* 0x800B219C */
    /* 0x01C */ s16 m1C_OPX_0CParams0[3];          /* 0x800B21A0 = 0x100 */
    /* 0x022 */ s16 m22_OPX_0CParams1[3];          /* 0x800B21A6 = 0x200 */
    /* 0x028 */ s16 m28_soundMaxDistance;             /* 0x800B21AC = 0x3FF */
    /* 0x02A */ s16 m2A_objectStepX;             /* 0x800B21AE */
    /* 0x02C */ s16 m2C_objectStepZ;             /* 0x800B21B0 */
    /* 0x02E */ s16 m2E_objectStepY;             /* 0x800B21B2 */
    /* 0x030 */ s16 m30_spriteTurnSpeed;             /* 0x800B21B4 = 0x80 */
    /* 0x032 */ s16 m32;
    /* 0x034 */ s32 m34_lastSoundEffectId;             /* 0x800B21B8 */
    /* 0x038 */ s32 m38_objectOffsetX;             /* 0x800B21BC */
    /* 0x03C */ s32 m3C_objectOffsetY;             /* 0x800B21C0 */
    /* 0x040 */ s32 m40_objectOffsetZ;             /* 0x800B21C4 */
    /* 0x044 */ s32 m44;
    /* 0x048 */ u8 m48_noUpdatesToPartyMembers;              /* 0x800B21CC */
    /* 0x049 */ u8 m49_cameraFollowHeight;              /* 0x800B21CD */
    /* 0x04A */ u8 m4A_partyPositioned;              /* 0x800B21CE */
    /* 0x04B */ u8 m4B_partyFollowDisabled;              /* 0x800B21CF */
    /* 0x04C */ u8 m4C_menuDisabled;              /* 0x800B21D0 */
    /* 0x04D */ u8 m4D_compassDisabled;              /* 0x800B21D1 */
    /* 0x04E */ u8 m4E_objectMoveFlags;              /* 0x800B21D2 */
    /* 0x04F */ u8 m4F;
    /* 0x050 */ s16 m50_linkOTIndex; /* 0x800B21D4 Noah: linkOTIndex */
    /* 0x052 */ s16 m52_dialogWindowOpenAnimationNumFrames;             /* 0x800B21D6 */
    /* 0x054 */ s32 m54_cameraSnapFrames;             /* 0x800B21D8 */
    /* 0x058 */ s16 m58_mechaList[4];     /* 0x800B21DC Noah: mechaList */
    /* 0x060 */ s16 m60_mechaCurrentAnimation[4]; /* 0x800B21E4 Noah: mechaCurrentAnimation */
    /* 0x068 */ u8 m68[0x30];
    /* 0x098 */ MechaFieldArgs2 m98_mechaFieldArgs2[2]; /* 0x800B221C Noah: mechaFieldArgs2 */
    /* 0x0D8 */ u8 mD8_mechaBackColor[3]; /* 0x800B225C Noah: mechaBackColor */
    /* 0x0DB */ u8 mDB;
    /* 0x0DC */ s32 mDC;
    /* 0x0E0 */ s32 mE0_numMechas;             /* 0x800B2264 */
    /* 0x0E4 */ s32 mE4_partySpriteSetupBase;             /* 0x800B2268 */
    /* 0x0E8 */ s32 mE8_playerControlledActor;             /* 0x800B226C Noah: g_playerControlledActor */
    /* 0x0EC */ s16 mEC_encounterMusicIds[16];         /* 0x800B2270 */
    /* 0x10C */ s16 m10C_battleMusicId;            /* 0x800B2290 */
    /* 0x10E */ s16 m10E;
    /* 0x110 */ s32 m110_encounterDataCountdown;            /* 0x800B2294 Noah: g_encounterDataCountdown */
    /* 0x114 */ s32 m114_encounterTimer;            /* 0x800B2298 Noah: g_encounterTimer */
    /* 0x118 */ s32 m118_encounterCount;            /* 0x800B229C Noah: g_encounterCount */
    /* 0x11C */ u16 m11C_encounterTriggerTimes[32];        /* 0x800B22A0 Noah: g_encounterTriggerTime */
    /* 0x15C */ s16 m15C_cameraTrackingMode;            /* 0x800B22E0 */
    /* 0x15E */ s16 m15E_lightActorIds[3];         /* 0x800B22E2 */
    /* 0x164 */ SVECTOR m164_lightNearColors[3];       /* 0x800B22E8 (mecha opcodes; per slot of m15E) */
    /* 0x17C */ SVECTOR m17C_lightFarColors[3];       /* 0x800B2300 */
    /* 0x194 */ s32 m194_lightRadii[3];           /* 0x800B2318 */
    /* 0x1A0 */ SVECTOR m1A0_lightPositions[3];       /* 0x800B2324 */
    /* 0x1B8 */ u16 m1B8_keptSoundChannels;            /* 0x800B233C: sound channels to keep (bit i: channel i*2) */
    /* 0x1BA */ u16 m1BA_actorCameraTracked; /* 0x800B233E Noah: actorCameraTracked */
    /* 0x1BC */ s16 m1BC_jumpCooldownReset;
    /* 0x1BE */ s16 m1BE_jumpCooldown;            /* 0x800B2342 */
    /* 0x1C0 */ s16 m1C0_customJumpMode;            /* 0x800B2344 */
    /* 0x1C2 */ s16 m1C2_jumpAnimationId; /* 0x800B2346 Noah: jumpAnimationId */
    /* 0x1C4 */ s16 m1C4_warpPartyRequest;            /* 0x800B2348 */
    /* 0x1C6 */ s16 m1C6_numMaxMechaOverlayEntries; /* 0x800B234A */
    /* 0x1C8 */ s16 m1C8_gearSwitchOverride;            /* 0x800B234C */
    /* 0x1CA */ s16 m1CA_playerIsntPartyLeader;            /* 0x800B234E */
    /* 0x1CC */ s32 m1CC_savedPlayerScriptFlags;            /* 0x800B2350 */
    /* 0x1D0 */ u8 m1D0_reverseDpadDirections;             /* 0x800B2354 */
    /* 0x1D1 */ u8 m1D1_randomBattleTransitionEffect;             /* 0x800B2355 */
    /* 0x1D2 */ u8 m1D2_fieldBattleTransitionEffect;             /* 0x800B2356 */
    /* 0x1D3 */ u8 m1D3_spriteFogDisabled;             /* 0x800B2357 */
    /* 0x1D4 */ u8 m1D4_pauseDisabled;             /* 0x800B2358 */
    /* 0x1D5 */ u8 m1D5[7];
    /* 0x1DC */ s32 m1DC_partyToFollowStructMapping[3]; /* 0x800B2360 Noah: partyToFollowStructMapping */
} FieldBss_800B2184;

extern s32 partyToFollowStructMapping[3]; /* = FieldBss_800B2184.m1DC; read as its own symbol */

/* The field save block copies two PS1 address ranges that span several
 * globals (backgroundPoly.., fieldScreenEffects..): on the PC they are copied global by
 * global (port/src/game/field/save_block.c). */
#define FIELD_SAVE_BACKGROUND 0 /* PS1: 0x800B007C, 0x38 bytes */
#define FIELD_SAVE_EFFECTS 1    /* PS1: 0x800B2078, 0x2E4 bytes */
s32 pcFieldSaveRangeSize(s32 range);
u8* pcFieldSaveRange(u8* cursor, s32 range, s32 load);

/* 0x800AFEA8: loaded field images (m104 added in round 11). m84 buffers are the TIM data, m104 their sizes (OPX_40 writes a
 * byte into one). */
typedef struct {
    /* 0x000 */ s16 m0_count;
    /* 0x004 */ void* m4_commands[0x20];
    /* 0x084 */ void* m84_buffers[0x20];
    /* 0x104 */ s16 m104_sizes[0x20];
} FieldImageList;

/* 0x800C3A20: parameters of the movie played by startFieldTransition (set by
 * script opcodes EX 0x60/0x67/0xA0). Retail reaches m1A and mE through one
 * base register, so this is one object. */
typedef struct {
    /* 0x00 */ s16 m0_movieId;
    /* 0x02 */ u16 m2_buffer0X;
    /* 0x04 */ u16 m4_buffer0Y;
    /* 0x06 */ u16 m6_buffer1X;
    /* 0x08 */ u16 m8_buffer1Y;
    /* 0x0A */ u16 mA_sectorOffset;
    /* 0x0C */ u16 mC_startFrame;
    /* 0x0E */ u16 mE_lastFrame;
    /* 0x10 */ u16 m10_modeAndSize[3];
    /* 0x16 */ s16 m16_is24Bit;      /* 1: 24-bit display */
    /* 0x18 */ s16 m18_soundSequenceId;      /* 0xFF: no sound sequence */
    /* 0x1A */ s16 m1A_loop;      /* non-zero: play until skipped */
} MovieParams;

extern MovieParams fieldMovieParams;

/* ---- Script VM globals and helpers (fieldScriptSupport.cpp) ---- */

extern u8* pCurrentFieldScriptFile;
extern sFieldScriptEntity* pCurrentFieldScriptActor;
extern s32 fieldExectuteMaxCycles;
extern u16 padButtonForScripts;                 /* Noah: padButtonForScripts[0].m0_buttons */
extern u16 padButtonForScripts2;                 /* Noah: padButtonForScripts2 (accumulated input) */
extern s16 fieldVars[];
extern s32 currentFieldActorId;
/* Enhanced (xg-hbc.11): a per-frame time scale (an entity's walk divisor
 * m76_timeScale, a sprite's time scale) at the field's current logic rate:
 * doubled while the 60 Hz field runs two frames per native one, else as is.
 * f_80072140.c. */
s32 pcFieldTimeScale(s32 base);
/* Enhanced (xg-hbc.11 phase 2): at 60 Hz the field's scripts, encounter tick
 * and dialog counters keep the native 30 Hz cadence: they step on "tick"
 * frames (every other frame since the field started) and skip the frames in
 * between, which only move, animate and draw. 0 at the native rate. */
s32 pcFieldOffTick(void);
/* The scripts' presses from the frame between ticks, presented to the script VM
 * and the camera dolly on the tick frame only (never to the per-frame readers:
 * the talk/interaction check, run/menu input, which saw them on their own frame). */
extern s32 pcFieldFrameIndex; /* frames since the field started (reset by the field init) */
/* Phase 3: a per-frame step (a rotation per frame) at the field's rate: halved, at least 1, at 60 Hz. */
s32 pcFieldStepDown(s32 step);
/* A per-frame 16.16 step at the field's rate: halved at 60 Hz (two frames make the native step). */
extern sFieldEntity* D_800AFB10;       /* fieldSceneState.m290_actorArray (Noah: actorArray), which f_8008083C.c reaches by its own address */
extern s32 g_totalActors;                 /* Noah: actorArray.size() */

/* main */
extern s32 currentParty[3];              /* Noah: currentParty (0xFF: empty) */
extern s32 asyncPartyCharacterLoadingTable[3];
extern s32 partyFieldActorIds[3];              /* Noah: partyToFieldEntityArrayMapping */
void fadePaletteColors(s32 count, s32 level, u16* dst, u16* src);

extern sFieldRenderContext fieldRenderContext[2];

s32 getVariable(s32 index);
void setVar(s32 index, s32 value);
s16 readS16FromScript(s32 offset);
u16 readU16FromScript(s32 offset);
s32 getImmediateOrVariableUnsigned(s32 offset);
s32 readCharacter(s32 offset);
s32 getVar80(s32 offset, s32 flags);
s32 getVar40(s32 offset, s32 flags);
s32 getVar20(s32 offset, s32 flags);
s32 getVar10(s32 offset, s32 flags);
s32 getVar08(s32 offset, s32 flags);
s32 getVar04(s32 offset, s32 flags);
s32 getVar02(s32 offset, s32 flags);
s32 getVar01(s32 offset, s32 flags);

/* Noah: sWalkMeshBundle::sTriangleData */
typedef struct {
    /* 0x0 */ s16 m0_verticeIndex[3];
    /* 0x6 */ s16 m6_connectivity[3];
    /* 0xC */ u8 mC_indexInWalkmeshData1;
    /* 0xD */ s8 mD_thicknessDiv4;
} sWalkMeshTriangle; /* size 0xE */

extern sWalkMeshTriangle* walkMeshTriangle[4] PORT_ALIAS;
extern SVECTOR* walkMeshVertices[4] PORT_ALIAS;
extern s32 g_walkMeshNumTrianglePerBlock[4] PORT_ALIAS;

/* ---- Field functions called across files ---- */
s32 createDialogWindow(s16 x, s16 y, s32 dialogIndex, s32 windowIndex, s32 width, s32 height, s32 fieldActorId,
                       s32 actorId, s32 mode, u32 flags, u32 flags2);
s32 findTriangleInWalkMesh(s32 posX, s32 posZ, s32 walkmeshId, SVECTOR* position, VECTOR* triangleNormal);
s32 fp_atan2(VECTOR* v);
void setMouseSpeedScale(u16 speedX, u16 speedY);

/* Some retail callers of these were compiled with other argument types or
 * counts: on the PS1 every file keeps its own declaration. */
s32 EntityMoveCheck1(s32 entityIndex, s32 maxAltitude, sFieldEntity* pFieldEntity, sFieldScriptEntity* pScript,
                     u32 param_5);
void OP_C4SubSub1(s32 id, s16 volume, s16 pan, s32 channel);
u16 getScriptEntryPoint(s32 entityId, s32 scriptIndex);
void loadFieldSeq(void);
void loadTimToVram(u32* addr, s16 px, s16 py, s16 cx, s16 cy, s16 cw, s16 ch);
void playMusic(s32 musicId);
void setPolyFT4UVsClamped(POLY_FT4* p, s16 u0, s16 v0, s16 u1, s16 v1, s16 u2, s16 v2, s16 u3, s16 v3);
void setScriptMarkerColor(s32 index, u8 r, u8 g, u8 b);

#endif /* FIELD_FIELD_H */
