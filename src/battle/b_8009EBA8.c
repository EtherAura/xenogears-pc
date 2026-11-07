/* Provisional file (split where JP links the code in another order). */
#include "common.h"
#include "psyq.h"
#include "gte.h"
#include "pc_geometry.h"
#include "stack.h"
#include "main/memory.h"
#include "main/filesystem.h"
#include "main/sprite.h"
#include "battle/battle.h"
#include "pc_convert.h"

/* Noah: sBackgroundPoly (main/background.c view; 0x34C bytes) */
typedef struct {
    /* 0x000 */ POLY_FT4 m0_ft4[16];
    /* 0x280 */ POLY_F4 m280_f4[4];
    /* 0x2E0 */ POLY_G4 m2E0_g4[2];
    /* 0x328 */ s32 m328_textureWidth;
    /* 0x32C */ s32 m32C_textureHeight;
    /* 0x330 */ s32 m330_scrollScale;
    /* 0x334 */ s16 m334[12];
} sBattleBackgroundPoly;

/* libgte screen coordinate pair (not in psyq.h yet) */
typedef struct {
    s16 vx, vy;
} DVECTOR;

/* Battle data block at 0x800CCCE8, reached through the initialised pointer
 * pBattleEntities (= &battleEntities). */
extern BattleBss_800CCCE8* pBattleEntities;
extern u8* startBattleAttackAnimationVar7;                           /* &battleEntities.m0_entities[target].m148 */
extern sBattle800CDD40Sub* currentEntityBattleStats;
extern sBattleEntity* battleGetSlotStatusSub_currentBattleEntity;
extern u8 startBattleAttackAnimationVar2;                            /* Noah: startBattleAttackAnimationVar2 (attacker) */
extern sBattleEntity* startBattleAttackAnimationVar5;                /* Noah: startBattleAttackAnimationVar5 (target) */
extern u8 startBattleAttackAnimationVar4;                            /* Noah: startBattleAttackAnimationVar4 (target) */
extern GameStateGear* battleGetSlotStatusSub_currentBattleEntityGear;
extern GameStateGear* startBattleAttackAnimationVar6;                /* Noah: startBattleAttackAnimationVar6 (target gear) */

/* ---- used by 0x800ADF1C-0x800B15D8 ---- */
extern sMechaInitVar2Sub* mechaEventAnimTracks[9]; /* Noah: battleArray800C3BAC */
sMechaInitVar2Sub* allocateAnimTrack_battle(sMechaInitVar2* arg0);
s32 releaseMechaAnimTrack_battle(sMechaInitVar2* arg0, sMechaInitVar2Sub* arg1);
void detachMechaBoneSubtree_battle(sMechaInitVar2* pool, s32 index, sMechaBone* bones, sMechaBone* bones2);

void setupBoneInterpolationTrack_battle(sLoadedMechas* pMecha, sMechaInitVar2* pool, sMechaBone* pBone, u8 flags, u8 mode, u8 tag,
                   u8 loop, s16 x, s16 y, s16 z, s16 tx, s16 ty, s16 tz, s16 duration);

void setBoneVisibility_battle(sLoadedMechas* pMecha, sMechaBone* pBone, s32 flags);
void updateMechaBoneAttachedSprite_battle(SpriteTask* task);
void mechaOp_62_battle(sLoadedMechas* pMecha, sMechaBone* pBone, u8 flags, s16 x, s16 y, s16 z);
SpriteTask* spawnEffectSprite(s32 animationId, SpriteVramData* vram, s16* position, s32 extraSize);
void spriteActorSetDirection(SpriteActorCore* actor, s32 direction);
void setGraphicEntityScale(SpriteActorCore* actor, s32 scale);
void setSpriteActorAngle(SpriteActorCore* actor, s16 angle);
void* getTaskUpdateFunction(TaskHeader* task);
s32 findBattleTerrainTriangle(); /* K&R: callers pass 3 arguments, the definition reads a 4th (a3) */
s32 findBattleTerrainTriangleBruteForce(SVECTOR* pos);
s32 computeTerrainHeightAtTriangle(SVECTOR* pos, s32 triangle, VECTOR* normal);
extern s32 objectClippingMask;
extern s32 allocateSavePointMeshDataSub0_var0;
extern u8 spriteBytecode2ExtendedE0_Var0;
extern u8 battleGearLoadTable1[20][2]; /* [gear]: file base, max extra file (0x800C3508-0x800C3530) */
extern s32 allocateJumpAnimationStructVar0;
extern sMechaInitVar4 battleMechaInitVar4[20];
extern s32 mechaInitVar4Counter_battle;                     /* Noah: mechaInitVar4Counter (battle) */
extern void* mechaModelBlocksBufferForLoading_battle;                   /* Noah: mechaModelBlocksBufferForLoading (battle) */
extern u8 battleMechaVar5;
extern LoadingBatchCommand* battleGearLoadCommands;
extern s16 battleMechaVar4;
extern s16 battle800c3b80;                     /* Noah: battle800c3b80 (wobble) */
extern u16 battleMechaVar1;
extern sMechaInitVar3 battleMechaInitVar3;
extern sMechaInitVar2 battleMechaInitVar2;
extern u16 updateTargetSelectionMarkerSub0_var0;
extern u16 mechaAnimSelfIndex;                     /* current mecha index */
extern u8 battleMechaVar3;
extern u8 battleMechaVar2;
extern u8 battle800c3df8;
extern u16 mechaPlayAnimationVar0_battle;                     /* Noah: mechaPlayAnimationVar0_battle (battle) */
extern s32 battleMechaVar0;
extern MATRIX* battle800d2fc0;                 /* Noah: battle800d2fc0 (colour matrix) */
extern sMechaInitVar5 mechaInitVar5_battle[2];       /* Noah: mechaInitVar5_battle (battle) */
extern s16 gMechaAngleBattle;
MATRIX* MulMatrix2(MATRIX* m0, MATRIX* m1);
void unloadSequence(void* seq);
void loadSequence(void* seq);
void* findSequenceInLoadedList(void* seq, s16 id);
u32 doPointerRelocation(u32* data);
s32 uploadTextureToVram(void* data, s16 mode, s16 x, s16 y, s16 mode2, s16 clutX, s16 clutY);
void getFilenameFromPC(s32 fileIndex);
void patchSelfModifyingCode(s32 a, s32 b, s32 c, s32 d);
s32 initModel5(void* model);
s32 undoFieldModelRelocation(void* model);
s32 getAllocationSize(void* data);
sMechaInitVar4* sMechaModel_init_battle(sModel* model, sMechaInitVar4* out);

sMechaBone* processMechaMesh_battle(sMechaInitVar4* model, sMechaDataTable1_C* entries, s32 mode, s32 useTpageAndClut,
                          s16 tpageX, s16 tpageY, s16 clutX, s16 clutY);

s32 updateMechAnimSub1_battle(sMechaBone* bones, s32 scale);
s32 updateMechAnimSub2_battle(sMechaBone* bones, s32 scale);
void freeMechaBones_battle(sMechaBone* bones);
void freeMechaModelBlocks_battle(sMechaInitVar4* p, s32 freeBlocks);

void submitMechaForRendering_battle(sLoadedMechas* pMecha, MATRIX* pMatrix, MATRIX* pArgs, s32 arg3, s32 arg4, OTag* ot,
                   s32 oddOrEven);

u32 updateMechAnimSub3_battle(sMechaInitVar2* pool, sMechaBone* bones, s32 id, s32 speed);
void submitAllMechaBonesForRendering_battle(sMechaInitVar4* model, sMechaBone* bones, MATRIX* m0, MATRIX* m1, s32 renderMode,
                   OTag* ot, s32 oddOrEven);
s32 mechaSetPoseFromKeyframe_battle(sMechaBone* bones, s16* p);
s32 mechaOP_13_battle(sMechaInitVar2* arg0, sMechaBone* bones, sMechaDataTable2_4_4* kf, s32 len, u32 interpType,
                  u32 isLooping, s32 tag);
void mechaOP_A_battle(sMechaInitVar2* arg0, sMechaBone* arg1, s32 arg2, s32 arg3);
void initMechaInitVar2Sub0_battle(sMechaInitVar2* arg0);
s32 mechaOP_11_battle(sMechaInitVar2* arg0, sMechaBone* bones, sMechaDataTable2_4_4* kf, u32 isLooping, s32 tag);
s32 mechaSetupKeyframeTracksWithPose_battle(sMechaInitVar2* arg0, sMechaBone* bones, sMechaDataTable2_4_4* kf, u32 isLooping, s32 tag);
void initMechaInitVar3Sub_battle(sMechaInitVar3* arg0);
sMechaInitVar3Sub* allocateMechaTrailSegment_battle(sMechaInitVar3* arg0, s16 arg1);
void updateAndRenderMechaCloth_battle(sLoadedMechas_114* arg0, SVECTOR* arg1, MATRIX* arg2, OTag* ot, s32 oddOrEven, s32 scale,
                   s16 floorY);
sMechaInitVar2* initMechaInitVar2_battle(sMechaInitVar2* arg0, s32 count);
void freeMechaInitVar2_battle(sMechaInitVar2* arg0);
void mechaOP_8_battle(sMechaInitVar2* arg0, sMechaBone* arg1);
void freeAnimTracks2_battle(sMechaInitVar2* arg0, sMechaBone* arg1, u8 arg2);
sMechaInitVar3* initMechaInitVar3_battle(sMechaInitVar3* arg0, s32 count);
void freeMechaInitVar3_battle(sMechaInitVar3* arg0);
void drawMechaWheelTrail_battle(sMechaInitVar3* p, MATRIX* pMatrix, s32 iterations, OTag* ot, s32 oddOrEven);
s16 envTileStepSine_battle(s16 arg0, s16 arg1, s16 arg2);
s16 envTileStepRampUp_battle(s16 arg0, s16 arg1, s16 arg2);
s16 envTileStepRampDown_battle(s16 arg0, s16 arg1, s16 arg2);
s16 envTileStepRampDownClamped_battle(s16 arg0, s16 arg1, s16 arg2);
void refreshAttachedMechaTransforms_battle(sLoadedMechas** mechas);
void finalizeMechaForEvent(s32 index);


void updateMechAnimSub4_battle(sLoadedMechas* pMecha, sMechaInitVar2* pool, s32 oddOrEven);
s32 setupMechaForEventSub0(void);
void snapMechaBoneToBattleTerrain(sLoadedMechas* pMecha);
void freeMechaExtraData(sLoadedMechas* pMecha);
void clearMechaEventAnimTracks(void);
void updateMechaEventAnim(sMechaInitVar2* pool, s32 iterations, s32 arg2, s32 oddOrEven);
void setupMechaForEventSub1(s32 a, s32 b);
void freeMechaCloth_battle(sLoadedMechas_114* arg0);

void mechaInitEnvironmentMechaMesh(s32 entryId, u16 flags, sMechaDataTable2* pData2, sMechaDataTable1* pData1, s16 tpageX,
                   s16 tpageY, s16 clutX, s16 clutY, SVECTOR* pPosition);

u8 addSaturatingColorDelta(s16 delta, s16 scale, s32 base);
s32 initMechaForBattleSub0_battle(s32 index);
void initMechSub110_battle(sLoadedMechas* pMecha);

void initMechaTransforms1_battle(sLoadedMechas* arg0, sMechaInitVar2* arg1, sMechaDataTable2_4_8** arg2,
                   sMechaDataTable2_4_4** arg3);

void initMechaAnimation_battle(sLoadedMechas* arg0, sLoadedMechas* arg1, sMechaInitVar2* arg2, s32 animationId);
s32 updateMechAnim_battle(sLoadedMechas* arg0, sMechaInitVar2* arg1, s32 iterations, s32 oddOrEven, s32 arg4);
void updateMechasBaseMatrixIsAttached_battle(sLoadedMechas* arg0);
void processMechaAnimData_battle(sLoadedMechas* pMecha, sMechaInitVar2* tracks, s32 flags, s32 iterations, s32 arg4);

/* Optional third file of a gear load (relocated): m4 = attachment records
 * (s16 count, pad, then {bone, x, y, z} u16 each), m8..mC = a mecha data
 * table 2 image, mC = the parts' data table 1. */
typedef struct {
    /* 0x00 */ s32 m0_count;
    /* 0x04 */ u16* m4_attachments;
    /* 0x08 */ void* m8_partsData2;
    /* 0x0C */ void* mC_partsModel;
} sBattleGearExtraFile;

s32 getAngleSin(s32);
void fadePaletteColors(s32 count, s32 level, u16* dst, u16* src);
void lerpPaletteColors(s32 count, s32 level, u16* dst, u16* src1, u16* src2);
s32 releaseMechaTrailSegment_battle(sMechaInitVar3* arg0, sMechaInitVar3Sub* arg1);
void freeAnimatedEnvTextureTile_battle(sLoadedMechas_118* arg0);
void fadeEnvTileColorMatrix_battle(sLoadedMechas_118* arg0, s16 arg1);
void lerpEnvTileColorMatrix_battle(sLoadedMechas_118* arg0, s16 arg1);
s16 updateAnimatedEnvTextureTile_battle(sLoadedMechas_118* arg0, s32 arg1);
extern s32 gDepthDivider; /* Noah: gDepthDivider (OT z shift) */
s32 isPointInsideTriangleXZ_battle(SVECTOR* v0, SVECTOR* v1, SVECTOR* v2, SVECTOR* point);
void getTriangleNormalAndAdjustY_battle(SVECTOR* v0, SVECTOR* v1, SVECTOR* v2, SVECTOR* pos, VECTOR* normal);
s32 findBattleTerrainTriangleRecursive(SVECTOR* pos, s32 triangle, s32 depth);
void executeBattleMoveImageCommands(sBattleImageMoveCommand* cmd);
void freeHeapIfNotNull(void* ptr);
void freeImageMoveCommand(sBattleImageMoveCommand* cmd);
s32 renderBackgroundPoly(void* poly, SVECTOR* eye, SVECTOR* at, MATRIX* m, OTag* ot, s32 oddOrEven);
s32 submitModelForRendering(void* block, void* prims, OTag* ot, s32 renderMode);

void renderBattleScenePolys(sMechaInitVar4* blocks, sMechaBone* bones, MATRIX* m, MATRIX* light, s32 arg4, OTag* ot,
                   s32 oddOrEven, s32 otSize);

void drawBattleHorizonBackdrop(sBattleHorizon* arg0, SVECTOR* eye, SVECTOR* at, MATRIX* m, OTag* ot, s32 oddOrEven);
void rebuildBattleEnvironmentVramBuffer(void);
void freeMecha_battle(s32);
extern sBattleBackgroundPoly* battleEnvBackgroundPolys[2];
extern sBattleImageMoveCommand battleMoveCommands[2];
extern sMechaBone* environmentModelConfigs;
extern sMechaInitVar4* environmentModelBlocks;
extern s32 battle800ccc5c;
s32 getMechaColliderRadius_battle(s32 mechaIdx);
void pushMechaAway(s32 mechaIdx, s32 x, s32 z, s32 minDist);
extern s16 battleEnvColliderCount;                       /* collider count */
extern s16* battleEnvColliders;                      /* colliders: {x, z, radius} */
extern u8* battleDrawEnvBgColor0;
extern u8* battleDrawEnvBgColor1;


/* ---- BR4: battle environment colour effects (0x800A5EB4-0x800A7064) ---- */
typedef struct {
    /* 0x0 */ u8 m0_enabled;
    /* 0x1 */ u8 m1_mode;
    /* 0x2 */ u8 m2_level;
    /* 0x3 */ s8 m3_rOffset;
    /* 0x4 */ s8 m4_gOffset;
    /* 0x5 */ s8 m5_bOffset;
} sBattleEnvColorEffect; /* 0x6 */
extern sBattleEnvColorEffect battleEnvColorEffects[4] PORT_ALIAS;
extern sLoadedMechas_118 battleEnvColorAnim;  /* environment CLUT (whole-palette) animation */
extern u8* battleEnvBaseColors;
extern void* battleEnvBlendedColors;
typedef struct {
    /* 0x00 */ u8 m0[0x88];
} sBattleEnvSavedColors; /* saved env colours (see restoreBattleEnvColors) */
extern s16 battleEnvTextureRectW;
extern s16 battleEnvTextureRectH;
extern s16 battleEnvTextureRectX;
extern s16 battleEnvTextureRectY;
void blendPaletteColors(s32 count, u16* src, u16* target, u16* dst, s32 rOfs, s32 gOfs, s32 bOfs, s32 mode, s32 blend);
void applyEnvColorEffectToColor(u8* out, s32 index, CVECTOR* col);
void restoreBattleEnvColors(void);
void setBattleEnvColorEffect(s32 index, s32 mode, s32 a2, s32 a3, s32 a4, s32 a5);


/* ---- BR5 (0x800A8BF0-0x800B15D8) ---- */
extern u8 battleCameraVar0;
extern SVECTOR battleCameraEye; /* battle camera eye */
extern SVECTOR battleCameraAt; /* battle camera target */
extern s16 battleCameraGroundTriangle;     /* last ground triangle of getBattleCameraGroundHeight */
extern s16 battleCameraGroundHeightCache;     /* last ground height */
extern s16 battleCameraGroundQueryId;     /* last id */
void stopCdRead(s32);
void battleRender(void); /* waits one frame */
s32 mecha_battle_op3(void);
extern u8 mechaEventAnimWaitTag;  /* owner tag reported through mechaEventAnimWaitState */
extern u8 mechaEventAnimWaitState;  /* 1: a tagged track ran, 2: it ended */
extern u8 mechaEventAnimSnapCamera;  /* write the computed targets back into tracks 7/8 */
extern s16 mechaEventCameraYaw; /* camera event values (tracks 0-6) */
extern s16 mechaEventCameraPitch;
extern s16 mechaEventCameraDistance;
extern u16 mechaEventCameraHeight;
extern s16 mechaEventTargetYaw;
extern s16 mechaEventTargetDistance;
extern u16 mechaEventTargetHeight;
s16 isInsideBattleArena(SVECTOR* pos);
s16 getBattleCameraGroundHeight(s32 id);
s32 stepTowardWithMechaCollision(SVECTOR* target, SVECTOR* pos);
extern SpriteVramData createSavePointMeshData_mode5;
extern SpriteVramData createSavePointMeshData_mode6;
void playSoundEffectInFreeSlot(s32);
void setSoundEffectVolume(s32, s32);
void createAllDamageDisplaysIfAnimating(void);
void spawnMechaBoneAttachedSprite_battle(SpriteVramData* vram, s32 animationId, s16* position, s16 direction, s16 scale,
                   sMechaBoneAttachmentConfig* pConfig, sLoadedMechas* pMecha);
s32 getAngleCos(s32); /* main: cosine (4096 = 1.0) */

sMechaInitVar4* sMechaModel_init_battle(sModel* model, sMechaInitVar4* out) {
    u32 n;
    u32 i;

    resetMemoryAllocStats(4, 0);
    n = fieldModelRelocation(model);
    out->m0_blocks = allocateHeap(n * (s32)sizeof(sModelBlock*), 0);
    out->m4_count = n;
    if (out->m0_blocks != NULL) {
        for (i = 0; i < n; i++) {
#ifndef PLATFORM_PC
            out->m0_blocks[i] = &model->m10_blocks[i];
#else
            out->m0_blocks[i] = (sModelBlock*)pcModelBlocks(model) + i;
#endif
        }
    }
    return out;
}

sMechaBone* processMechaMesh_battle(sMechaInitVar4* model, sMechaDataTable1_C* entries, s32 mode, s32 useTpageAndClut,
                          s16 tpageX, s16 tpageY, s16 clutX, s16 clutY) {
    sMechaDataTable1_C* e;
    sMechaBone* bones;
    sMechaBone* b;
    s32 n;
    s32 boneCounter;
    u16 geom;
    u16 parent;

    resetMemoryAllocStats(4, 0);
    e = entries;
    n = 0;
    while (e->m0_geometryId < model->m4_count || e->m0_geometryId == 0xFFFF) {
        e++;
        n++;
    }
    if (n == 0) {
        return NULL;
    }
    n++;
    bones = allocateHeap(n * sizeof(sMechaBone), 0);
    if (bones == NULL) {
        return NULL;
    }
    e = entries;
    b = bones + 1;
    boneCounter = 1;
    geom = e->m0_geometryId;
    parent = e->m2_parentBone;
    bones->m4_needTranslationUpdate = 1;
    bones->m5_needRotationUpdate = 1;
    bones->m6_useYXZRotation = 1;
    bones->m4C_scale[0] = 0x1000;
    bones->m4C_scale[1] = 0x1000;
    bones->m4C_scale[2] = 0x1000;
    bones->m0_parentBone = NULL;
    bones->m7_isEnabled = 0;
    bones->m8_geometryId = 0xFFFF;
    bones->mA_numBones = n;
    bones->m68_prims[0] = NULL;
    bones->m68_prims[1] = NULL;
    bones->m54_rotationAngles.vx = 0;
    bones->m54_rotationAngles.vy = 0;
    bones->m54_rotationAngles.vz = 0;
    bones->m5C_translation[0] = 0;
    bones->m5C_translation[1] = 0;
    bones->m5C_translation[2] = 0;
    bones->m70_animTracks[0] = NULL;
    bones->m70_animTracks[1] = NULL;
    bones->m70_animTracks[2] = NULL;
    while (geom < model->m4_count || geom == 0xFFFF) {
        if (parent == 0xFFFF) {
            b->m0_parentBone = NULL;
        } else {
            b->m0_parentBone = bones + parent + 1;
        }
        b->mA_numBones = boneCounter++;
        b->m4_needTranslationUpdate = 1;
        b->m5_needRotationUpdate = 1;
        b->m7_isEnabled = 1;
        b->m4C_scale[0] = 0x1000;
        b->m4C_scale[1] = 0x1000;
        b->m4C_scale[2] = 0x1000;
        b->m6_useYXZRotation = 0;
        b->m52_billboardMode = 0;
        b->m8_geometryId = geom;
        if (geom != 0xFFFF) {
            initModel1(model->m0_blocks[geom], &b->m68_prims[0], &b->m68_prims[1]);
            if (b->m68_prims[0] == NULL) {
                freeMechaBones_battle(bones);
                return NULL;
            }
            if (useTpageAndClut) {
                setupOverrideTPage(tpageX, tpageY);
                setupOverrideClut(clutX, clutY);
            }
            initModel2(model->m0_blocks[geom], b->m68_prims[0], mode);
            memcpy(b->m68_prims[1], b->m68_prims[0], model->m0_blocks[geom]->m34_primBufferSize);
        } else {
            b->m68_prims[0] = NULL;
            b->m68_prims[1] = NULL;
        }
        b->m54_rotationAngles.vx = 0;
        b->m54_rotationAngles.vy = 0;
        b->m54_rotationAngles.vz = 0;
        b->m5C_translation[0] = 0;
        b->m5C_translation[1] = 0;
        b->m5C_translation[2] = 0;
        b->m70_animTracks[0] = NULL;
        b->m70_animTracks[1] = NULL;
        b->m70_animTracks[2] = NULL;
        b++;
        e++;
        geom = e->m0_geometryId;
        parent = e->m2_parentBone;
    }
    return bones;
}

s32 updateMechAnimSub1_battle(sMechaBone* bones, s32 scale) {
    MATRIX* m = SCRATCH_MATRIX_00;
    sMechaBone* b = bones;
    u32 i;
    u32 n;

    b->m2C_boneFinalMatrix.t[0] = b->m5C_translation[0];
    b->m2C_boneFinalMatrix.t[1] = b->m5C_translation[1];
    b->m2C_boneFinalMatrix.t[2] = b->m5C_translation[2];
    n = b->mA_numBones;
    if (b->m6_useYXZRotation != 0) {
        RotMatrixYXZ(&b->m54_rotationAngles, &b->m2C_boneFinalMatrix);
    } else {
        createRotationMatrix(&b->m54_rotationAngles, &b->m2C_boneFinalMatrix);
    }
    m->m[0][0] = (scale * bones->m4C_scale[0]) >> 12;
    m->m[0][1] = 0;
    m->m[0][2] = 0;
    m->m[1][0] = 0;
    m->m[1][1] = (scale * bones->m4C_scale[1]) >> 12;
    m->m[1][2] = 0;
    m->m[2][0] = 0;
    m->m[2][1] = 0;
    m->m[2][2] = (scale * bones->m4C_scale[2]) >> 12;
    MulMatrix0(&bones->m2C_boneFinalMatrix, m, &bones->mC_localMatrix);
    bones->mC_localMatrix.t[0] = bones->m2C_boneFinalMatrix.t[0];
    bones->mC_localMatrix.t[1] = bones->m2C_boneFinalMatrix.t[1];
    bones->mC_localMatrix.t[2] = bones->m2C_boneFinalMatrix.t[2];

    for (i = 1; i < n; i++) {
        bones++;
        if (bones->m5_needRotationUpdate) {
            if (bones->m6_useYXZRotation != 0) {
                RotMatrixYXZ(&bones->m54_rotationAngles, &bones->mC_localMatrix);
            } else {
                createRotationMatrix(&bones->m54_rotationAngles, &bones->mC_localMatrix);
            }
            bones->m5_needRotationUpdate = 0;
        }
        if (bones->m0_parentBone != NULL && bones->m0_parentBone->m4_needTranslationUpdate == 1) {
            bones->m4_needTranslationUpdate = 1;
        }
        if (bones->m4_needTranslationUpdate) {
            bones->mC_localMatrix.t[0] = bones->m5C_translation[0];
            bones->mC_localMatrix.t[1] = bones->m5C_translation[1];
            bones->mC_localMatrix.t[2] = bones->m5C_translation[2];
            if (bones->m0_parentBone != NULL) {
                CompMatrix(&bones->m0_parentBone->m2C_boneFinalMatrix, &bones->mC_localMatrix,
                           &bones->m2C_boneFinalMatrix);
            } else {
                bones->m2C_boneFinalMatrix = bones->mC_localMatrix;
            }
        }
    }
    for (i = 1; i < n; i++) {
        b++;
        b->m4_needTranslationUpdate = 0;
    }
    return n;
}

s32 updateMechAnimSub2_battle(sMechaBone* bones, s32 scale) {
    MATRIX* m = SCRATCH_MATRIX_00;
    sMechaBone* b = bones;
    u32 i;
    u32 n;

    b->m2C_boneFinalMatrix.t[0] = b->m5C_translation[0];
    b->m2C_boneFinalMatrix.t[1] = b->m5C_translation[1];
    b->m2C_boneFinalMatrix.t[2] = b->m5C_translation[2];
    n = b->mA_numBones;
    if (b->m6_useYXZRotation != 0) {
        RotMatrixYXZ(&b->m54_rotationAngles, &b->m2C_boneFinalMatrix);
    } else {
        createRotationMatrix(&b->m54_rotationAngles, &b->m2C_boneFinalMatrix);
    }
    m->m[0][0] = (scale * bones->m4C_scale[0]) >> 12;
    m->m[0][1] = 0;
    m->m[0][2] = 0;
    m->m[1][0] = 0;
    m->m[1][1] = (scale * bones->m4C_scale[1]) >> 12;
    m->m[1][2] = 0;
    m->m[2][0] = 0;
    m->m[2][1] = 0;
    m->m[2][2] = (scale * bones->m4C_scale[2]) >> 12;
    MulMatrix0(&bones->m2C_boneFinalMatrix, m, &bones->mC_localMatrix);
    bones->mC_localMatrix.t[0] = bones->m2C_boneFinalMatrix.t[0];
    bones->mC_localMatrix.t[1] = bones->m2C_boneFinalMatrix.t[1];
    bones->mC_localMatrix.t[2] = bones->m2C_boneFinalMatrix.t[2];

    for (i = 1; i < n; i++) {
        bones++;
        if (bones->m0_parentBone != NULL) {
            if (bones->m0_parentBone->m5_needRotationUpdate == 1) {
                bones->m5_needRotationUpdate = 1;
            }
            if (bones->m0_parentBone->m4_needTranslationUpdate == 1) {
                bones->m4_needTranslationUpdate = 1;
            }
        }
        if (bones->m5_needRotationUpdate) {
            MATRIX* sp = SCRATCH_MATRIX_00;

            if (bones->m6_useYXZRotation != 0) {
                RotMatrixYXZ(&bones->m54_rotationAngles, &bones->mC_localMatrix);
            } else {
                createRotationMatrix(&bones->m54_rotationAngles, &bones->mC_localMatrix);
            }
            sp->m[0][0] = bones->m4C_scale[0];
            sp->m[0][1] = 0;
            sp->m[0][2] = 0;
            sp->m[1][0] = 0;
            sp->m[1][1] = bones->m4C_scale[1];
            sp->m[1][2] = 0;
            sp->m[2][0] = 0;
            sp->m[2][1] = 0;
            sp->m[2][2] = bones->m4C_scale[2];
            MulMatrix0(&bones->mC_localMatrix, sp, &bones->mC_localMatrix);
            if (bones->m0_parentBone != NULL) {
                sp->m[0][0] = 0x1000000 / bones->m0_parentBone->m4C_scale[0];
                sp->m[0][1] = 0;
                sp->m[0][2] = 0;
                sp->m[1][0] = 0;
                sp->m[1][1] = 0x1000000 / bones->m0_parentBone->m4C_scale[1];
                sp->m[1][2] = 0;
                sp->m[2][0] = 0;
                sp->m[2][1] = 0;
                sp->m[2][2] = 0x1000000 / bones->m0_parentBone->m4C_scale[2];
                MulMatrix0(sp, &bones->mC_localMatrix, &bones->mC_localMatrix);
            }
        }
        if (bones->m4_needTranslationUpdate) {
            bones->mC_localMatrix.t[0] = bones->m5C_translation[0];
            bones->mC_localMatrix.t[1] = bones->m5C_translation[1];
            bones->mC_localMatrix.t[2] = bones->m5C_translation[2];
            if (bones->m0_parentBone != NULL) {
                CompMatrix(&bones->m0_parentBone->m2C_boneFinalMatrix, &bones->mC_localMatrix,
                           &bones->m2C_boneFinalMatrix);
            } else {
                bones->m2C_boneFinalMatrix = bones->mC_localMatrix;
            }
        }
    }
    for (i = 1; i < n; i++) {
        b++;
        b->m5_needRotationUpdate = 0;
        b->m4_needTranslationUpdate = 0;
    }
    return n;
}

/* (was already C) */
void func_8009F5B0(void) {
}

void submitAllMechaBonesForRendering_battle(sMechaInitVar4* model, sMechaBone* bones, MATRIX* m0, MATRIX* m1, s32 renderMode,
                   OTag* ot, s32 oddOrEven) {
    MATRIX* sp20 = SCRATCH_MATRIX_20;
    MATRIX* sp40 = SCRATCH_MATRIX_40;
    MATRIX* sp00;
    u32 i;
    u32 n;

    MulMatrix0(m1, &bones->m2C_boneFinalMatrix, sp20);
    CompMatrix(m0, &bones->mC_localMatrix, sp40);
    n = bones->mA_numBones;
    bones++;
    sp00 = SCRATCH_MATRIX_00;
    for (i = 1; i < n; bones++) {
        i++;
        if (bones->m8_geometryId != 0xFFFF) {
            MulMatrix0(sp20, &bones->m2C_boneFinalMatrix, sp00);
            SetLightMatrix(sp00);
            CompMatrix(sp40, &bones->m2C_boneFinalMatrix, sp00);
            SetRotMatrix(sp00);
            SetTransMatrix(sp00);
            submitModelForRendering(model->m0_blocks[bones->m8_geometryId], bones->m68_prims[oddOrEven], ot,
                                    renderMode);
        }
    }
}

void freeMechaBones_battle(sMechaBone* bones) {
    sMechaBone* b;
    s32 i;

    if (bones != NULL) {
        i = 0;
        b = bones;
        while (i < bones->mA_numBones) {
            i++;
            if (b->m68_prims[0] != NULL) {
                freeHeap(b->m68_prims[0]);
                b->m68_prims[0] = NULL;
                b->m68_prims[1] = NULL;
            }
            b++;
        }
        bones->mA_numBones = 0;
        freeHeap(bones);
    }
}

void freeMechaModelBlocks_battle(sMechaInitVar4* p, s32 freeBlocks) {
    u32 i;

    if (p != NULL) {
        for (i = 0; i < p->m4_count; i++) {
            if (p->m0_blocks != NULL && p->m0_blocks[i] != NULL && freeBlocks) {
                freeModelBlock(p->m0_blocks[i]);
            }
        }
        if (p->m0_blocks != NULL) {
            freeHeap(p->m0_blocks);
            p->m0_blocks = NULL;
        }
    }
}

#ifdef PLATFORM_PC
/* port/src/game/battle/gearpose.c: the Gear's halfway matrices on a step frame (Smooth motion), else the game's. */
s32 pcGearPoseFor(const sLoadedMechas* m);
MATRIX* pcGearPoseRoot(s32 s, MATRIX* game);
MATRIX* pcGearPoseFinal(s32 s, s32 bone, MATRIX* game);
#define GEAR_ROOT(m) pcGearPoseRoot(pose, m)
#define GEAR_FINAL(i, m) pcGearPoseFinal(pose, i, m)
#else
#define GEAR_ROOT(m) (m)
#define GEAR_FINAL(i, m) (m)
#endif

void submitMechaForRendering_battle(sLoadedMechas* mecha, MATRIX* m1, MATRIX* m2, s32 renderMode, s32 arg4, OTag* ot,
                   s32 oddOrEven) {
    SVECTOR sv;
    VECTOR v1;
    VECTOR v2;
    s32 z;
    s32 z2;
    u32 numBones;
    sMechaInitVar4* model;
    sMechaBone* b;
    sMechaBone* bones;
    u16 scale;
    MATRIX* sp20 = SCRATCH_MATRIX_20;
    MATRIX* sp40 = SCRATCH_MATRIX_40;
    MATRIX* sp00 = SCRATCH_MATRIX_00;
    s32 i;
    s32 j;
#ifdef PLATFORM_PC
    s32 pose;
#endif

    if (mecha->m34_visible != 0) {
        model = mecha->m0_model;
        b = mecha->m4_bones;
        scale = mecha->m1C_scale;
        bones = b;
        numBones = b->mA_numBones;
        for (i = 0; i < 2; i++) {
            if (mechaInitVar5_battle[i].m6_isActive != 0) {
                s16 dx;
                s16 dy;
                s16 dz;
                s32 len;

                dx = mechaInitVar5_battle[i].m0_worldPosition[0] - b->m5C_translation[0];
                dy = mechaInitVar5_battle[i].m0_worldPosition[1] -
                     ((b->m5C_translation[1] - mecha->m24_dimensions[0] * ((scale * b->m4C_scale[1]) >> 12)) >> 13);
                dz = mechaInitVar5_battle[i].m0_worldPosition[2] - b->m5C_translation[2];
                len = SquareRoot0(dx * dx + dy * dy + dz * dz) + 1;
                m2->m[i + 1][0] = ((dx << 12) / len) * mechaInitVar5_battle[i].m6_isActive / (mechaInitVar5_battle[i].m6_isActive + len);
                m2->m[i + 1][1] = ((dy << 12) / len) * mechaInitVar5_battle[i].m6_isActive / (mechaInitVar5_battle[i].m6_isActive + len);
                m2->m[i + 1][2] = ((dz << 12) / len) * mechaInitVar5_battle[i].m6_isActive / (mechaInitVar5_battle[i].m6_isActive + len);
            } else {
                m2->m[i + 1][0] = 0;
                m2->m[i + 1][1] = 0;
                m2->m[i + 1][2] = 0;
            }
        }
#ifdef PLATFORM_PC
        pose = pcGearPoseFor(mecha);
#endif
        CompMatrix(m1, GEAR_ROOT(&b->mC_localMatrix), sp40);
        if (!(mecha->m4A_flags & 1) && battleMechaVar2 != 0) {
            s32 s;

            CompMatrix(&b->mC_localMatrix, &mecha->m4_bones[1].m2C_boneFinalMatrix, sp00);
            SetRotMatrix(sp00);
            SetTransMatrix(sp00);
            sv.vx = 0;
            sv.vy = 0;
            sv.vz = 0x1000;
            gte_ldv0(&sv);
            gte_rt();
            gte_stlvnl(&v1);
            sv.vx = 0;
            sv.vy = 0;
            sv.vz = 0;
            gte_ldv0(&sv);
            gte_rt();
            gte_stlvnl(&v2);
            sv.vy = -ratan2(v1.vz - v2.vz, v1.vx - v2.vx);
            sv.vx = 0;
            sv.vz = 0;
            createRotationMatrix(&sv, sp20);
            sp20->t[0] = v2.vx;
            sp20->t[1] = mecha->m60_groundY;
            sp20->t[2] = v2.vz;
            CompMatrix(m1, sp20, sp20);
            s = mecha->m1C_scale - (mecha->m60_groundY - mecha->m4_bones->m5C_translation[1]) / 4;
            if (s < 0) {
                s = 0;
            }
            sp00->m[0][0] = s;
            sp00->m[0][1] = 0;
            sp00->m[0][2] = 0;
            sp00->m[1][0] = 0;
            sp00->m[1][1] = s;
            sp00->m[1][2] = 0;
            sp00->m[2][0] = 0;
            sp00->m[2][1] = 0;
            sp00->m[2][2] = s;
            MulMatrix(sp20, sp00);
            SetRotMatrix(sp20);
            SetTransMatrix(sp20);
            sv.vx = mecha->m24_dimensions[1];
            sv.vy = 0;
            sv.vz = mecha->m24_dimensions[2];
            gte_ldv0(&sv);
            gte_rtps();
            gte_stsxy(&mecha->mB8_Polys[oddOrEven].x0);
            gte_stszotz(&z);
            sv.vx = -mecha->m24_dimensions[1];
            gte_ldv0(&sv);
            gte_rtps();
            gte_stsxy(&mecha->mB8_Polys[oddOrEven].x1);
            gte_stszotz(&z2);
            if (z2 < z) {
                z = z2;
            }
            sv.vx = mecha->m24_dimensions[1];
            sv.vz = -mecha->m24_dimensions[2];
            gte_ldv0(&sv);
            gte_rtps();
            gte_stsxy(&mecha->mB8_Polys[oddOrEven].x2);
            gte_stszotz(&z2);
            if (z2 < z) {
                z = z2;
            }
            sv.vx = -mecha->m24_dimensions[1];
            gte_ldv0(&sv);
            gte_rtps();
            gte_stsxy(&mecha->mB8_Polys[oddOrEven].x3);
            gte_stszotz(&z2);
            if (z2 < z) {
                z = z2;
            }
            z >>= gDepthDivider;
            addPrim(ot + z, &mecha->mB8_Polys[oddOrEven]);
            if (z > 0x2D8) {
                z = 0x2D8;
            }
            mecha->m39_shadowShade = 0x6B - z / 8;
        }
        MulMatrix0(m2, GEAR_FINAL(0, &b->m2C_boneFinalMatrix), sp20);
        for (i = 1, b++; i < numBones; i++, b++) {
            if (b->m8_geometryId != 0xFFFF && b->m7_isEnabled) {
                MulMatrix0(sp20, GEAR_FINAL(i, &b->m2C_boneFinalMatrix), sp00);
                SetLightMatrix(sp00);
                CompMatrix(sp40, GEAR_FINAL(i, &b->m2C_boneFinalMatrix), sp00);
                if (b->m52_billboardMode > 0) {
                    sp00->m[0][0] = mecha->m1C_scale;
                    sp00->m[0][2] = 0;
                    sp00->m[1][0] = 0;
                    sp00->m[1][2] = 0;
                    sp00->m[2][0] = 0;
                    sp00->m[2][2] = mecha->m1C_scale;
                    if (b->m52_billboardMode == 1) {
                        sp00->m[0][1] = sp40->m[0][1];
                        sp00->m[1][1] = sp40->m[1][1];
                        sp00->m[2][1] = sp40->m[2][1];
                    } else {
                        sp00->m[0][1] = 0;
                        sp00->m[1][1] = mecha->m1C_scale;
                        sp00->m[2][1] = 0;
                    }
                }
                SetRotMatrix(sp00);
                SetTransMatrix(sp00);
                submitModelForRendering(model->m0_blocks[b->m8_geometryId], b->m68_prims[oddOrEven], ot,
                                        renderMode);
            }
        }
        {
            sLoadedMechas_114* e = mecha->m114_cloths;
            VECTOR out;
            SVECTOR pull;

            for (i = 0; i < mecha->m10D_num114; i++) {
                if (e->m0_boneIndex >= 0) {
                    sMechaClothCollider* coll;

                    pull.vx = (-gMechaAngleBattle * getAngleSin(bones->m54_rotationAngles.vy + 0x400)) / 4096;
                    pull.vz = (gMechaAngleBattle * getAngleCos(bones->m54_rotationAngles.vy + 0x400)) / 4096;
                    pull.vy = mecha->m3E_clothPullY;
                    CompMatrix(&bones->mC_localMatrix, &bones[e->m0_boneIndex].m2C_boneFinalMatrix, sp00);
                    SetRotMatrix(sp00);
                    SetTransMatrix(sp00);
                    for (j = 0; j < e->m4_numChains; j++) {
                        gte_ldv0(&e->m14_vertices[j]);
                        gte_rt();
                        gte_stlvnl(&out);
                        e->m1C_chains[j]->m4_position.vx = out.vx;
                        e->m1C_chains[j]->m4_position.vy = out.vy;
                        e->m1C_chains[j]->m4_position.vz = out.vz;
                    }
                    coll = e->m18_colliders;
                    for (j = 0; j < e->mA_numColliders; j++, coll++) {
                        CompMatrix(&bones->mC_localMatrix, &bones[coll->m0_pos.pad].m2C_boneFinalMatrix, sp00);
                        SetRotMatrix(sp00);
                        SetTransMatrix(sp00);
                        gte_ldv0(coll);
                        gte_rt();
                        gte_stlvnl(&out);
                        coll->m8_x = out.vx;
                        coll->mA_y = out.vy;
                        coll->mC_z = out.vz;
                    }
                    updateAndRenderMechaCloth_battle(e, &pull, m1, ot, oddOrEven, scale, mecha->m60_groundY);
                }
                e++;
            }
        }
        {
            sLoadedMechas_118* e = mecha->m118_colorAnims;

            for (i = 0; i < mecha->m10E_num118; i++, e++) {
                updateAnimatedEnvTextureTile_battle(e, arg4);
            }
        }
        {
            sLoadedMechas_110* tr = mecha->m110_trails;

            for (i = 0; i < mecha->m10C_num110; i++) {
#ifdef PLATFORM_PC
                /* A viewer's pose draw supplies -1: retain the trail history. */
                if (arg4 < 0) {
                    tr++;
                    continue;
                }
#endif
                if (tr->m0_boneIndex >= 0) {
                    tr->m5C_head = (tr->m5C_head - 1) & 7;
                    if (tr->m2_mode == 0) {
                        CompMatrix(sp40, &bones[tr->m0_boneIndex].m2C_boneFinalMatrix, sp00);
                        SetRotMatrix(sp00);
                        SetTransMatrix(sp00);
                        gte_ldv0(&tr->mC_p0);
                        gte_rtps();
                        gte_stsxy(tr->m1C_hist0.xy[tr->m5C_head]);
                        gte_ldv0(&tr->m14_p1);
                        gte_rtps();
                        gte_stsxy(tr->m3C_hist1.xy[tr->m5C_head]);
                        if (++tr->m5E_count == 0) {
                            continue;
                        }
                        if (tr->m60_maxCount < tr->m5E_count || tr->m8_segment == NULL) {
                            s32 k;

                            tr->m5E_count = 1;
                            tr->m8_segment = allocateMechaTrailSegment_battle(tr->m4_pool, tr->m3_semiTrans);
                            tr->m8_segment->mE_is3D = 0;
                            tr->m8_segment->m16_age = 0;
                            tr->m8_segment->m1E_lifetime = tr->m62_lifetime;
                            tr->m8_segment->m20_rgb[0] = tr->m64_rgb[0];
                            tr->m8_segment->m20_rgb[1] = tr->m64_rgb[1];
                            tr->m8_segment->m20_rgb[2] = tr->m64_rgb[2];
                            tr->m8_segment->m26_rgbStep[0] = tr->m6A_rgbStep[0];
                            tr->m8_segment->m26_rgbStep[1] = tr->m6A_rgbStep[1];
                            tr->m8_segment->m26_rgbStep[2] = tr->m6A_rgbStep[2];
                            k = (tr->m5C_head + tr->m5E_count) & 7;
                            tr->m8_segment->m0_x0 = (*(tr->m1C_hist0.xy + k))[0];
                            tr->m8_segment->m2_y0 = (*(tr->m1C_hist0.xy + k))[1];
                            tr->m8_segment->m10_x2 = (*(tr->m3C_hist1.xy + k))[0];
                            tr->m8_segment->m12_y2 = (*(tr->m3C_hist1.xy + k))[1];
                        }
                        tr->m8_segment->m8_x1 = (*(tr->m1C_hist0.xy + tr->m5C_head))[0];
                        tr->m8_segment->mA_y1 = (*(tr->m1C_hist0.xy + tr->m5C_head))[1];
                        tr->m8_segment->m18_x3 = (*(tr->m3C_hist1.xy + tr->m5C_head))[0];
                        tr->m8_segment->m1A_y3 = (*(tr->m3C_hist1.xy + tr->m5C_head))[1];
                    } else {
                        s32 cur;
                        VECTOR* a;
                        VECTOR* c;

                        CompMatrix(&bones->mC_localMatrix, &bones[tr->m0_boneIndex].m2C_boneFinalMatrix, sp00);
                        SetRotMatrix(sp00);
                        SetTransMatrix(sp00);
                        cur = tr->m5C_head & 1;
                        a = &tr->m1C_hist0.v[cur];
                        c = &tr->m3C_hist1.v[cur];
                        gte_ldv0(&tr->mC_p0);
                        gte_rt();
                        gte_stlvnl(a);
                        gte_ldv0(&tr->m14_p1);
                        gte_rt();
                        gte_stlvnl(c);
                        if (++tr->m5E_count == 0) {
                            continue;
                        }
                        if (tr->m60_maxCount < tr->m5E_count || tr->m8_segment == NULL) {
                            VECTOR* prev0;
                            VECTOR* prev1;

                            tr->m5E_count = 1;
                            tr->m8_segment = allocateMechaTrailSegment_battle(tr->m4_pool, tr->m3_semiTrans);
                            tr->m8_segment->mE_is3D = 1;
                            tr->m8_segment->m16_age = 0;
                            tr->m8_segment->m1E_lifetime = tr->m62_lifetime;
                            tr->m8_segment->m20_rgb[0] = tr->m64_rgb[0];
                            tr->m8_segment->m20_rgb[1] = tr->m64_rgb[1];
                            tr->m8_segment->m20_rgb[2] = tr->m64_rgb[2];
                            tr->m8_segment->m26_rgbStep[0] = tr->m6A_rgbStep[0];
                            tr->m8_segment->m26_rgbStep[1] = tr->m6A_rgbStep[1];
                            tr->m8_segment->m26_rgbStep[2] = tr->m6A_rgbStep[2];
                            prev0 = &tr->m1C_hist0.v[1 - cur];
                            tr->m8_segment->m0_x0 = prev0->vx;
                            tr->m8_segment->m2_y0 = prev0->vy;
                            tr->m8_segment->m4_z0 = prev0->vz;
                            prev1 = &tr->m3C_hist1.v[1 - cur];
                            tr->m8_segment->m10_x2 = prev1->vx;
                            tr->m8_segment->m12_y2 = prev1->vy;
                            tr->m8_segment->m14_z2 = prev1->vz;
                        }
                        tr->m8_segment->m8_x1 = a->vx;
                        tr->m8_segment->mA_y1 = a->vy;
                        tr->m8_segment->mC_z1 = a->vz;
                        tr->m8_segment->m18_x3 = c->vx;
                        tr->m8_segment->m1A_y3 = c->vy;
                        tr->m8_segment->m1C_z3 = c->vz;
                    }
                }
                tr++;
            }
        }
    }
}

u32 updateMechAnimSub3_battle(sMechaInitVar2* pool, sMechaBone* bones, s32 id, s32 speed) {
    SVECTOR sv;
    VECTOR v;
    u32 i;
    u32 n;
    u32 flags;
    sMechaInitVar2Sub* t;
    sMechaInitVar2Sub* t2;
    s16* p;

    flags = 0;
    n = bones->mA_numBones;
    for (i = 0; i < n; i++, bones++) {
        /* rotation */
        if (bones->m70_animTracks[0] != NULL) {
            u8 type;

            t = bones->m70_animTracks[0];
            type = t->m2_type;
            switch (type & 0xF) {
            case 0:
                p = (s16*)t->m4.stream.m8_cursor;
                if (!(type & 0x10)) {
                    bones->m54_rotationAngles.vx = *p++;
                    t->m4.stream.m8_cursor += 2;
                }
                if (!(type & 0x20)) {
                    bones->m54_rotationAngles.vy = *p++;
                    t->m4.stream.m8_cursor += 2;
                }
                if (!(type & 0x40)) {
                    bones->m54_rotationAngles.vz = *p;
                    t->m4.stream.m8_cursor += 2;
                }
                break;
            case 1:
                if (!(type & 0x10)) {
                    s32 d = *t->m4.stream.m8_cursor++;

                    if (d != -0x80) {
                        bones->m54_rotationAngles.vx += d;
                    } else {
                        s32 v;

                        d = (u8)*t->m4.stream.m8_cursor++;
                        v = d | (*t->m4.stream.m8_cursor++ << 8);
                        bones->m54_rotationAngles.vx = v;
                    }
                }
                if (!(type & 0x20)) {
                    s32 d = *t->m4.stream.m8_cursor++;

                    if (d != -0x80) {
                        bones->m54_rotationAngles.vy += d;
                    } else {
                        s32 v;

                        d = (u8)*t->m4.stream.m8_cursor++;
                        v = d | (*t->m4.stream.m8_cursor++ << 8);
                        bones->m54_rotationAngles.vy = v;
                    }
                }
                if (!(type & 0x40)) {
                    s32 d = *t->m4.stream.m8_cursor++;

                    if (d != -0x80) {
                        bones->m54_rotationAngles.vz += d;
                    } else {
                        s32 v;

                        d = (u8)*t->m4.stream.m8_cursor++;
                        v = d | (*t->m4.stream.m8_cursor++ << 8);
                        bones->m54_rotationAngles.vz = v;
                    }
                }
                break;
            case 2:
                p = (s16*)t->m4.stream.m8_cursor;
                if (!(type & 0x10)) {
                    bones->m54_rotationAngles.vx += *p++;
                    t->m4.stream.m8_cursor += 2;
                }
                if (!(type & 0x20)) {
                    bones->m54_rotationAngles.vy += *p++;
                    t->m4.stream.m8_cursor += 2;
                }
                if (!(type & 0x40)) {
                    bones->m54_rotationAngles.vz += *p;
                    t->m4.stream.m8_cursor += 2;
                }
                break;
            case 3: {
                s32 ratio = (s16)(t->m10_currentTime + 1);
                bones->m54_rotationAngles.vx = t->m4.v.m4_x + (t->m4.v.mA_x * ratio) / t->m12_maxTime;
                bones->m54_rotationAngles.vy = t->m4.v.m6_y + (t->m4.v.mC_y * ratio) / t->m12_maxTime;
                bones->m54_rotationAngles.vz = t->m4.v.m8_z + (t->m4.v.mE_z * ratio) / t->m12_maxTime;
                t2 = t;
                break;
            }
            case 4: {
                s16 maxTime = t->m12_maxTime;
                s16 dx = (t->m4.v.mA_x - bones->m54_rotationAngles.vx) / maxTime;
                s16 dy = (t->m4.v.mC_y - bones->m54_rotationAngles.vy) / maxTime;
                s16 dz = (t->m4.v.mE_z - bones->m54_rotationAngles.vz) / maxTime;
                t2 = t;
                if (dx == 0 && dy == 0 && dz == 0) {
                    t->m10_currentTime = maxTime;
                    bones->m54_rotationAngles.vx = t->m4.v.mA_x;
                    bones->m54_rotationAngles.vy = t->m4.v.mC_y;
                    bones->m54_rotationAngles.vz = t->m4.v.mE_z;
                } else {
                    bones->m54_rotationAngles.vx += dx;
                    bones->m54_rotationAngles.vy += dy;
                    bones->m54_rotationAngles.vz += dz;
                    t->m10_currentTime = 0;
                }
                break;
            }
            case 5:
                t->m4.v.m4_x += t->m4.v.mA_x;
                bones->m54_rotationAngles.vx += t->m4.v.m4_x;
                t->m4.v.m6_y += t->m4.v.mC_y;
                bones->m54_rotationAngles.vy += t->m4.v.m6_y;
                t->m4.v.m8_z += t->m4.v.mE_z;
                bones->m54_rotationAngles.vz += t->m4.v.m8_z;
                t2 = t;
                break;
            case 7:
            case 8: {
                s32 dx = t->m4.v.mA_x - bones->m5C_translation[0];
                s32 dy = t->m4.v.mC_y - bones->m5C_translation[1];
                s32 dz = t->m4.v.mE_z - bones->m5C_translation[2];
                s32 dist = SquareRoot0(dx * dx + dy * dy + dz * dz) + 1;
                s32 delta = (ratan2(-dx, -dz) - bones->m54_rotationAngles.vy) & 0xFFF;
                s16 cur = bones->m54_rotationAngles.vy;
                s32 step;

                t2 = t;
                if (delta >= 0x800) {
                    delta -= 0x1000;
                }
                step = t->m4.v.m6_y + ((dist + t->m10_currentTime) * t->m4.v.m8_z) / t->m4.v.m4_x;
                if ((delta < 0 ? -delta : delta) < step) {
                    bones->m54_rotationAngles.vy = cur + delta;
                } else if (delta >= 0) {
                    bones->m54_rotationAngles.vy = cur + step;
                } else {
                    bones->m54_rotationAngles.vy = cur - step;
                }
                if ((type & 0xF) == 7) {
                    s16 cur2;


                    delta = ratan2(dy, SquareRoot0(dx * dx + dz * dz));
                    cur2 = bones->m54_rotationAngles.vx;
                    delta = (delta - cur2) & 0xFFF;
                    if (delta >= 0x800) {
                        delta -= 0x1000;
                    }
                    if ((delta < 0 ? -delta : delta) < step) {
                        bones->m54_rotationAngles.vx = cur2 + delta;
                    } else if (delta >= 0) {
                        bones->m54_rotationAngles.vx = cur2 + step;
                    } else {
                        bones->m54_rotationAngles.vx = cur2 - step;
                    }
                }
                if (t2->m10_currentTime < 32000) {
                    t2->m10_currentTime += t2->m12_maxTime;
                }
                goto rotationDone;
            }
            }
            t->m10_currentTime++;
            if (t->m10_currentTime >= t->m12_maxTime) {
                if (t->m1_isLooping == 0) {
                    if (t->m3_tag == id) {
                        flags |= 2;
                    }
                    flags |= 0x200;
                    releaseMechaAnimTrack_battle(pool, t);
                    bones->m70_animTracks[0] = NULL;
                } else {
                    if (t->m3_tag == id) {
                        flags |= 4;
                    }
                    flags |= 0x400;
                    if ((type & 0xF) < 3) {
                        t->m10_currentTime = 0;
                        t->m4.stream.m8_cursor = t->m4.stream.m4_start;
                    } else {
                        t->m10_currentTime = -1;
                        if ((type & 0xF) == 5) {
                            t2->m4.v.mA_x = 0;
                            t2->m4.v.mC_y = 0;
                            t2->m4.v.mE_z = 0;
                        }
                    }
                }
            } else {
                if (t->m3_tag == id) {
                    flags |= 1;
                }
                flags |= 0x100;
            }
        rotationDone:
            bones->m5_needRotationUpdate = 1;
            bones->m4_needTranslationUpdate = 1;
        }

        /* translation */
        if (bones->m70_animTracks[1] != NULL) {
            u8 type;

            t = bones->m70_animTracks[1];
            type = t->m2_type;
            switch (type & 0xF) {
            case 0:
                p = (s16*)t->m4.stream.m8_cursor;
                if (!(type & 0x10)) {
                    bones->m5C_translation[0] = *p++;
                    t->m4.stream.m8_cursor += 2;
                }
                if (!(type & 0x20)) {
                    bones->m5C_translation[1] = *p++;
                    t->m4.stream.m8_cursor += 2;
                }
                if (!(type & 0x40)) {
                    bones->m5C_translation[2] = *p;
                    t->m4.stream.m8_cursor += 2;
                }
                break;
            case 1:
                if (!(type & 0x10)) {
                    s32 d = *t->m4.stream.m8_cursor++;

                    if (d != -0x80) {
                        bones->m5C_translation[0] += d;
                    } else {
                        d = (u8)*t->m4.stream.m8_cursor++;
                        bones->m5C_translation[0] = d | (*t->m4.stream.m8_cursor++ << 8);
                    }
                }
                if (!(type & 0x20)) {
                    s32 d = *t->m4.stream.m8_cursor++;

                    if (d != -0x80) {
                        bones->m5C_translation[1] += d;
                    } else {
                        d = (u8)*t->m4.stream.m8_cursor++;
                        bones->m5C_translation[1] = d | (*t->m4.stream.m8_cursor++ << 8);
                    }
                }
                if (!(type & 0x40)) {
                    s32 d = *t->m4.stream.m8_cursor++;

                    if (d != -0x80) {
                        bones->m5C_translation[2] += d;
                    } else {
                        d = (u8)*t->m4.stream.m8_cursor++;
                        bones->m5C_translation[2] = d | (*t->m4.stream.m8_cursor++ << 8);
                    }
                }
                break;
            case 2:
                p = (s16*)t->m4.stream.m8_cursor;
                if (!(type & 0x10)) {
                    sv.vx = *p++;
                    t->m4.stream.m8_cursor += 2;
                } else {
                    sv.vx = 0;
                }
                if (!(type & 0x20)) {
                    sv.vy = *p++;
                    t->m4.stream.m8_cursor += 2;
                } else {
                    sv.vy = 0;
                }
                if (!(type & 0x40)) {
                    sv.vz = *p;
                    t->m4.stream.m8_cursor += 2;
                } else {
                    sv.vz = 0;
                }
                sv.vx = (sv.vx * bones->m4C_scale[0]) >> 12;
                sv.vy = (sv.vy * bones->m4C_scale[1]) >> 12;
                sv.vz = (sv.vz * bones->m4C_scale[2]) >> 12;
                ApplyMatrix(&bones->m2C_boneFinalMatrix, &sv, &v);
                bones->m5C_translation[0] += (speed * v.vx) >> 12;
                bones->m5C_translation[1] += (speed * v.vy) >> 12;
                bones->m5C_translation[2] += (speed * v.vz) >> 12;
                break;
            case 3: {
                s32 ratio = (s16)(t->m10_currentTime + 1);
                bones->m5C_translation[0] = t->m4.v.m4_x + (t->m4.v.mA_x * ratio) / t->m12_maxTime;
                bones->m5C_translation[1] = t->m4.v.m6_y + (t->m4.v.mC_y * ratio) / t->m12_maxTime;
                bones->m5C_translation[2] = t->m4.v.m8_z + (t->m4.v.mE_z * ratio) / t->m12_maxTime;
                t2 = t;
                break;
            }
            case 4: {
                s16 maxTime = t->m12_maxTime;
                s16 dx = (t->m4.v.mA_x - bones->m5C_translation[0]) / maxTime;
                s16 dy = (t->m4.v.mC_y - bones->m5C_translation[1]) / maxTime;
                s16 dz = (t->m4.v.mE_z - bones->m5C_translation[2]) / maxTime;
                t2 = t;
                if (dx == 0 && dy == 0 && dz == 0) {
                    t->m10_currentTime = maxTime;
                    bones->m5C_translation[0] = t->m4.v.mA_x;
                    bones->m5C_translation[1] = t->m4.v.mC_y;
                    bones->m5C_translation[2] = t->m4.v.mE_z;
                } else {
                    bones->m5C_translation[0] += dx;
                    bones->m5C_translation[1] += dy;
                    bones->m5C_translation[2] += dz;
                    t->m10_currentTime = 0;
                }
                break;
            }
            case 5:
                t->m4.v.m4_x += t->m4.v.mA_x;
                bones->m5C_translation[0] += t->m4.v.m4_x;
                t->m4.v.m6_y += t->m4.v.mC_y;
                bones->m5C_translation[1] += t->m4.v.m6_y;
                t->m4.v.m8_z += t->m4.v.mE_z;
                bones->m5C_translation[2] += t->m4.v.m8_z;
                t2 = t;
                break;
            }
            t->m10_currentTime++;
            if (t->m10_currentTime >= t->m12_maxTime) {
                if (t->m1_isLooping == 0) {
                    if (t->m3_tag == id) {
                        flags |= 2;
                    }
                    flags |= 0x200;
                    releaseMechaAnimTrack_battle(pool, t);
                    bones->m70_animTracks[1] = NULL;
                } else {
                    if (t->m3_tag == id) {
                        flags |= 4;
                    }
                    flags |= 0x400;
                    if ((type & 0xF) < 3) {
                        t->m10_currentTime = 0;
                        t->m4.stream.m8_cursor = t->m4.stream.m4_start;
                    } else {
                        t->m10_currentTime = -1;
                        if ((type & 0xF) == 5) {
                            t2->m4.v.mA_x = 0;
                            t2->m4.v.mC_y = 0;
                            t2->m4.v.mE_z = 0;
                        }
                    }
                }
            } else {
                if (t->m3_tag == id) {
                    flags |= 1;
                }
                flags |= 0x100;
            }
            bones->m4_needTranslationUpdate = 1;
        }

        /* scale */
        if (bones->m70_animTracks[2] != NULL) {
            u8 type;

            t = bones->m70_animTracks[2];
            type = t->m2_type & 0xF;
            switch (type) {
            case 3: {
                s32 ratio = (s16)(t->m10_currentTime + 1);
                bones->m4C_scale[0] = t->m4.v.m4_x + (t->m4.v.mA_x * ratio) / t->m12_maxTime;
                bones->m4C_scale[1] = t->m4.v.m6_y + (t->m4.v.mC_y * ratio) / t->m12_maxTime;
                bones->m4C_scale[2] = t->m4.v.m8_z + (t->m4.v.mE_z * ratio) / t->m12_maxTime;
                t2 = t;
                break;
            }
            case 4: {
                s16 maxTime = t->m12_maxTime;
                s16 dx = (t->m4.v.mA_x - t->m4.v.m4_x) / maxTime;
                s16 dy = (t->m4.v.mC_y - t->m4.v.m6_y) / maxTime;
                s16 dz = (t->m4.v.mE_z - t->m4.v.m8_z) / maxTime;
                t2 = t;
                if (dx == 0 && dy == 0 && dz == 0) {
                    t->m10_currentTime = maxTime;
                    bones->m4C_scale[0] = t->m4.v.mA_x;
                    bones->m4C_scale[1] = t->m4.v.mC_y;
                    bones->m4C_scale[2] = t->m4.v.mE_z;
                } else {
                    t2->m4.v.m4_x += dx;
                    t2->m4.v.m6_y += dy;
                    t2->m4.v.m8_z += dz;
                    bones->m4C_scale[0] = t2->m4.v.m4_x;
                    bones->m4C_scale[1] = t2->m4.v.m6_y;
                    bones->m4C_scale[2] = t2->m4.v.m8_z;
                    t->m10_currentTime = 0;
                }
                break;
            }
            case 5:
                t->m4.v.m4_x += t->m4.v.mA_x;
                bones->m4C_scale[0] += t->m4.v.m4_x;
                t->m4.v.m6_y += t->m4.v.mC_y;
                bones->m4C_scale[1] += t->m4.v.m6_y;
                t->m4.v.m8_z += t->m4.v.mE_z;
                bones->m4C_scale[2] += t->m4.v.m8_z;
                t2 = t;
                break;
            }
            t->m10_currentTime++;
            if (t->m10_currentTime >= t->m12_maxTime) {
                if (t->m1_isLooping == 0) {
                    if (t->m3_tag == id) {
                        flags |= 2;
                    }
                    flags |= 0x200;
                    releaseMechaAnimTrack_battle(pool, t);
                    bones->m70_animTracks[2] = NULL;
                } else {
                    if (t->m3_tag == id) {
                        flags |= 4;
                    }
                    flags |= 0x400;
                    t->m10_currentTime = -1;
                    if (type == 5) {
                        t2->m4.v.mA_x = 0;
                        t2->m4.v.mC_y = 0;
                        t2->m4.v.mE_z = 0;
                    }
                }
            } else {
                if (t->m3_tag == id) {
                    flags |= 1;
                }
                flags |= 0x100;
            }
            bones->m5_needRotationUpdate = 1;
            bones->m4_needTranslationUpdate = 1;
        }
    }
    return flags;
}

s32 mechaSetPoseFromKeyframe_battle(sMechaBone* bones, s16* p) {
    sMechaDataTable2_4_4* kf = (sMechaDataTable2_4_4*)p;
    u16 flags;
    u16 numRotations;
    u16 numTranslations;
    u16 rotationCount = 0;
    u16 translationCount = 0;
    u16 n;
    s32 i;

    i = (s16)kf->m6_isStaticPose;
    numRotations = kf->mC_numRotations;
    numTranslations = kf->mE_numTranslations;
    flags = kf->m4_flags;
    p += 12;
    if (i == 0) {
        p += (numRotations + 1) * 3;
    }
    n = bones->mA_numBones - 1;
    for (i = 0; i < n; i++) {
        bones++;
        if (!(flags & 1) && rotationCount < numRotations) {
            s32 x = *p++;
            s32 y = *p++;
            s32 z = *p++;
            rotationCount++;
            if (bones->m54_rotationAngles.vx != x || bones->m54_rotationAngles.vy != y ||
                bones->m54_rotationAngles.vz != z) {
                if (bones->m70_animTracks[0] == NULL || bones->m70_animTracks[0]->m3_tag != 0xFF) {
                    bones->m54_rotationAngles.vx = x;
                    bones->m54_rotationAngles.vy = y;
                    bones->m54_rotationAngles.vz = z;
                    bones->m4_needTranslationUpdate = 1;
                    bones->m5_needRotationUpdate = 1;
                }
            }
        }
        if (!(flags & 2) && translationCount < numTranslations) {
            s32 x = *p++;
            s32 y = *p++;
            s32 z = *p++;
            translationCount++;
            if (bones->m5C_translation[0] != x || bones->m5C_translation[1] != y ||
                bones->m5C_translation[2] != z) {
                if (bones->m70_animTracks[1] == NULL || bones->m70_animTracks[1]->m3_tag != 0xFF) {
                    bones->m5C_translation[0] = x;
                    bones->m5C_translation[1] = y;
                    bones->m5C_translation[2] = z;
                    bones->m4_needTranslationUpdate = 1;
                }
            }
        }
    }
    return n;
}

s32 mechaOP_13_battle(sMechaInitVar2* arg0, sMechaBone* bones, sMechaDataTable2_4_4* kf, s32 len, u32 interpType,
                  u32 isLooping, s32 tag) {
    u16 numRot;
    u16 rotCount;
    u16 numTrans;
    u16 transCount;
    u16 numBones;
    u16 flags;
    s16* hdr;
    sMechaInitVar2Sub* track;
    s32 x;
    s32 y;
    s32 z;
    s32 i;

    hdr = (s16*)kf;
    if (len == 0) {
        len = 1;
    }
    rotCount = 0;
    transCount = 0;
    isLooping &= 1;
    numRot = kf->mC_numRotations;
    i = (s16)kf->m6_isStaticPose;
    numTrans = kf->mE_numTranslations;
    interpType &= 1;
    flags = kf->m4_flags;
    hdr += 12;
    if (i == 0) {
        hdr += (numRot + 1) * 3;
    }
    numBones = bones->mA_numBones - 1;
    for (i = 0; i < numBones; i++) {
        bones++;
        if (!(flags & 1) && rotCount < numRot) {
            x = *hdr++;
            y = *hdr++;
            z = *hdr++;
            rotCount++;
            if (bones->m54_rotationAngles.vx != x || bones->m54_rotationAngles.vy != y ||
                bones->m54_rotationAngles.vz != z) {
                if (bones->m70_animTracks[0] != NULL) {
                    track = bones->m70_animTracks[0];
                    if (track->m3_tag == 0xFF) {
                        goto next0;
                    }
                } else {
                    track = allocateAnimTrack_battle(arg0);
                }
                if (track != NULL) {
                    track->m0_inUse = 1;
                    track->m1_isLooping = isLooping;
                    track->m2_type = interpType + 3;
                    track->m3_tag = tag;
                    track->m4.v.m4_x = bones->m54_rotationAngles.vx;
                    track->m4.v.m6_y = bones->m54_rotationAngles.vy;
                    track->m4.v.m8_z = bones->m54_rotationAngles.vz;
                    x = (x - bones->m54_rotationAngles.vx) & 0xFFF;
                    if (x > 0x7FF) {
                        x -= 0x1000;
                    }
                    track->m4.v.mA_x = x;
                    y = (y - bones->m54_rotationAngles.vy) & 0xFFF;
                    if (y > 0x7FF) {
                        y -= 0x1000;
                    }
                    track->m4.v.mC_y = y;
                    z = (z - bones->m54_rotationAngles.vz) & 0xFFF;
                    if (z > 0x7FF) {
                        z -= 0x1000;
                    }
                    track->m4.v.mE_z = z;
                    if (interpType) {
                        track->m4.v.mA_x += bones->m54_rotationAngles.vx;
                        track->m4.v.mC_y += bones->m54_rotationAngles.vy;
                        track->m4.v.mE_z += bones->m54_rotationAngles.vz;
                    }
                    track->m10_currentTime = 0;
                    track->m12_maxTime = len;
                    bones->m70_animTracks[0] = track;
                    goto next0;
                }
            }
        }
        if (bones->m70_animTracks[0] != NULL && bones->m70_animTracks[0]->m3_tag != 0xFF) {
            releaseMechaAnimTrack_battle(arg0, bones->m70_animTracks[0]);
            bones->m70_animTracks[0] = NULL;
        }
    next0:
        if (!(flags & 2) && transCount < numTrans) {
            x = *hdr++;
            y = *hdr++;
            z = *hdr++;
            transCount++;
            if (bones->m5C_translation[0] != x || bones->m5C_translation[1] != y || bones->m5C_translation[2] != z) {
                if (bones->m70_animTracks[1] != NULL) {
                    track = bones->m70_animTracks[1];
                    if (track->m3_tag == 0xFF) {
                        continue;
                    }
                } else {
                    track = allocateAnimTrack_battle(arg0);
                }
                if (track != NULL) {
                    track->m0_inUse = 1;
                    track->m1_isLooping = isLooping;
                    track->m2_type = interpType + 3;
                    track->m3_tag = tag;
                    track->m4.v.m4_x = bones->m5C_translation[0];
                    track->m4.v.m6_y = bones->m5C_translation[1];
                    track->m4.v.m8_z = bones->m5C_translation[2];
                    if (interpType) {
                        track->m4.v.mA_x = x;
                        track->m4.v.mC_y = y;
                        track->m4.v.mE_z = z;
                    } else {
                        track->m4.v.mA_x = x - bones->m5C_translation[0];
                        track->m4.v.mC_y = y - bones->m5C_translation[1];
                        track->m4.v.mE_z = z - bones->m5C_translation[2];
                    }
                    track->m10_currentTime = 0;
                    track->m12_maxTime = len;
                    bones->m70_animTracks[1] = track;
                    continue;
                }
            }
        }
        if (bones->m70_animTracks[1] != NULL && bones->m70_animTracks[1]->m3_tag != 0xFF) {
            releaseMechaAnimTrack_battle(arg0, bones->m70_animTracks[1]);
            bones->m70_animTracks[1] = NULL;
        }
    }
    return numBones;
}

void mechaOP_A_battle(sMechaInitVar2* arg0, sMechaBone* arg1, s32 arg2, s32 arg3) {
    if (arg2 < arg1->mA_numBones) {
        arg1 = &arg1[arg2];
        if (arg1->m70_animTracks[0] != NULL && (arg3 & 1)) {
            releaseMechaAnimTrack_battle(arg0, arg1->m70_animTracks[0]);
            arg1->m70_animTracks[0] = NULL;
        }
        if (arg1->m70_animTracks[1] != NULL && (arg3 & 2)) {
            releaseMechaAnimTrack_battle(arg0, arg1->m70_animTracks[1]);
            arg1->m70_animTracks[1] = NULL;
        }
        if (arg1->m70_animTracks[2] != NULL && (arg3 & 4)) {
            releaseMechaAnimTrack_battle(arg0, arg1->m70_animTracks[2]);
            arg1->m70_animTracks[2] = NULL;
        }
    }
}

sMechaInitVar2* initMechaInitVar2_battle(sMechaInitVar2* arg0, s32 count) {
    if (count > 0) {
        arg0->m6_count = count;
        resetMemoryAllocStats(4, 0);
        arg0->m0_tracks = allocateHeap(count * sizeof(sMechaInitVar2Sub), 0);
        if (arg0->m0_tracks == NULL) {
            return NULL;
        }
    } else {
        return NULL;
    }
    initMechaInitVar2Sub0_battle(arg0);
    return arg0;
}

void freeMechaInitVar2_battle(sMechaInitVar2* arg0) {
    arg0->m4_firstFree = 0;
    if (arg0->m0_tracks != NULL) {
        freeHeap(arg0->m0_tracks);
    }
    arg0->m0_tracks = NULL;
}

void initMechaInitVar2Sub0_battle(sMechaInitVar2* arg0) {
    sMechaInitVar2Sub* p;
    s32 i;
    s32 pad[1]; /* unused: gives the retail 8-byte frame */

    if (arg0->m0_tracks != NULL) {
        p = arg0->m0_tracks;
        arg0->m4_firstFree = 0;
        for (i = 0; i < (u16)arg0->m6_count; i++) {
            p->m0_inUse = 0;
            p++;
        }
    }
}

sMechaInitVar2Sub* allocateAnimTrack_battle(sMechaInitVar2* arg0) {
    sMechaInitVar2Sub* track;

    if (arg0->m4_firstFree < arg0->m6_count) {
        track = &arg0->m0_tracks[arg0->m4_firstFree];
        if (track->m0_inUse == 0) {
            arg0->m4_firstFree++;
            while (arg0->m4_firstFree < arg0->m6_count) {
                if (arg0->m0_tracks[arg0->m4_firstFree].m0_inUse == 0) {
                    break;
                }
                arg0->m4_firstFree++;
            }
            return track;
        }
        return NULL;
    }
    return NULL;
}

s32 releaseMechaAnimTrack_battle(sMechaInitVar2* arg0, sMechaInitVar2Sub* arg1) {
    s32 idx;

    if (arg1 == NULL) {
        return -1;
    }
    idx = (s32)(((uptr)arg1 - (uptr)arg0->m0_tracks) / sizeof(sMechaInitVar2Sub));
    if (idx < arg0->m4_firstFree) {
        arg0->m4_firstFree = idx;
    }
    arg1->m0_inUse = 0;
    return idx;
}

s32 mechaOP_11_battle(sMechaInitVar2* arg0, sMechaBone* bones, sMechaDataTable2_4_4* kf, u32 isLooping, s32 tag) {
    u16 numBones;
    u16 numRot;
    u16 numTrans;
    s32 duration;
    u16 flags;
    u16* hdr;
    u8* interp;
    u8* data;
    sMechaInitVar2Sub* track;
    s8* p;
    s32 i;

    hdr = (u16*)kf;
    if (kf->m6_isStaticPose != 0) {
        mechaOP_8_battle(arg0, bones);
        mechaSetPoseFromKeyframe_battle(bones, (s16*)kf);
        return 1;
    }
    numRot = kf->mC_numRotations;
    numBones = bones->mA_numBones;
    numTrans = kf->mE_numTranslations;
    isLooping &= 1;
    if (numRot + 1 < numBones) {
        numBones = numRot + 1;
    }
    duration = kf->m2_keyDuration;
    if (isLooping == 0) {
        duration--;
    }
    flags = kf->m4_flags;
    hdr += 12; /* kf->m18: kf and the header cursor share a register */
    data = (u8*)(hdr + (numRot + 1) * 3);
    if (!(flags & 1)) {
        data += numRot * 6;
    }
    if (!(flags & 2)) {
        data += numTrans * 6;
    }
    for (i = 0; i < numBones; i++) {
        interp = (u8*)(hdr + 2);
        if (*hdr != 0xFFFF) {
            if (bones->m70_animTracks[0] != NULL) {
                track = bones->m70_animTracks[0];
                if (track->m3_tag == 0xFF) {
                    goto next0;
                }
            } else {
                track = allocateAnimTrack_battle(arg0);
            }
            if (track != NULL) {
                track->m0_inUse = 1;
                track->m1_isLooping = isLooping;
                track->m2_type = interp[0];
                track->m3_tag = tag;
                p = (s8*)data + *hdr;
                track->m10_currentTime = 0;
                track->m12_maxTime = duration;
                track->m4.stream.m4_start = p;
                track->m4.stream.m8_cursor = p;
                bones->m70_animTracks[0] = track;
            }
        } else {
            if (bones->m70_animTracks[0] != NULL && i != 0 && bones->m70_animTracks[0]->m3_tag != 0xFF) {
                releaseMechaAnimTrack_battle(arg0, bones->m70_animTracks[0]);
                bones->m70_animTracks[0] = NULL;
            }
        }
    next0:
        hdr++;
        if (*hdr != 0xFFFF) {
            if (bones->m70_animTracks[1] != NULL) {
                track = bones->m70_animTracks[1];
                if (track->m3_tag == 0xFF) {
                    goto next1;
                }
            } else {
                track = allocateAnimTrack_battle(arg0);
            }
            if (track != NULL) {
                track->m0_inUse = 1;
                track->m1_isLooping = isLooping;
                track->m2_type = interp[1];
                track->m3_tag = tag;
                p = (s8*)data + *hdr;
                track->m10_currentTime = 0;
                track->m12_maxTime = duration;
                track->m4.stream.m4_start = p;
                track->m4.stream.m8_cursor = p;
                bones->m70_animTracks[1] = track;
            }
        } else {
            if (bones->m70_animTracks[1] != NULL && i != 0 && bones->m70_animTracks[1]->m3_tag != 0xFF) {
                releaseMechaAnimTrack_battle(arg0, bones->m70_animTracks[1]);
                bones->m70_animTracks[1] = NULL;
            }
        }
    next1:
        hdr += 2;
        bones++;
    }
    return 0;
}

s32 mechaSetupKeyframeTracksWithPose_battle(sMechaInitVar2* arg0, sMechaBone* bones, sMechaDataTable2_4_4* kf, u32 isLooping, s32 tag) {
    /* the declaration order fixes the retail spill-slot order */
    u8* interp;
    u16 numRot;
    u16 numTrans;
    u16 numBones;
    u16 duration;
    u16 rotCount;
    u16 transCount;
    u16 flags;
    u16* hdr;
    s16* pose;
    u8* data;
    sMechaInitVar2Sub* track;
    s8* p;
    s32 i;

    hdr = (u16*)kf;
    if (kf->m6_isStaticPose != 0) {
        mechaOP_8_battle(arg0, bones);
        mechaSetPoseFromKeyframe_battle(bones, (s16*)kf);
        return 1;
    }
    isLooping &= 1;
    numRot = kf->mC_numRotations;
    rotCount = 0;
    numBones = bones->mA_numBones;
    numTrans = kf->mE_numTranslations;
    transCount = 0;
    if (numRot + 1 < numBones) {
        numBones = numRot + 1;
    }
    duration = kf->m2_keyDuration;
    if (isLooping == 0) {
        duration--;
    }
    flags = kf->m4_flags;
    hdr += 12;
    pose = (s16*)(hdr + (numRot + 1) * 3);
    data = (u8*)pose;
    if (!(flags & 1)) {
        data += numRot * 6;
    }
    if (!(flags & 2)) {
        data += numTrans * 6;
    }
    for (i = 0; i < numBones; i++) {
        interp = (u8*)(hdr + 2);
        if (*hdr != 0xFFFF) {
            if (bones->m70_animTracks[0] != NULL) {
                track = bones->m70_animTracks[0];
                if (track->m3_tag == 0xFF) {
                    goto skip0;
                }
            } else {
                track = allocateAnimTrack_battle(arg0);
            }
            if (!(flags & 1) && i != 0 && rotCount < numRot) {
                bones->m54_rotationAngles.vx = *pose++;
                rotCount++;
                bones->m54_rotationAngles.vy = *pose++;
                bones->m54_rotationAngles.vz = *pose++;
                bones->m4_needTranslationUpdate = 1;
                bones->m5_needRotationUpdate = 1;
            }
            if (track != NULL) {
                track->m0_inUse = 1;
                track->m1_isLooping = isLooping;
                track->m2_type = interp[0];
                track->m3_tag = tag;
                p = (s8*)data + *hdr;
                track->m10_currentTime = 0;
                track->m12_maxTime = duration;
                track->m4.stream.m4_start = p;
                track->m4.stream.m8_cursor = p;
                bones->m70_animTracks[0] = track;
            }
        } else {
        skip0:
            if (!(flags & 1) && i != 0 && rotCount < numRot) {
                pose += 3;
                rotCount++;
            }
        }
        hdr++;
        if (*hdr != 0xFFFF) {
            if (bones->m70_animTracks[1] != NULL) {
                track = bones->m70_animTracks[1];
                if (track->m3_tag == 0xFF) {
                    goto skip1;
                }
            } else {
                track = allocateAnimTrack_battle(arg0);
            }
            if (!(flags & 2) && i != 0 && transCount < numTrans) {
                bones->m5C_translation[0] = *pose++;
                transCount++;
                bones->m5C_translation[1] = *pose++;
                bones->m5C_translation[2] = *pose++;
                bones->m4_needTranslationUpdate = 1;
            }
            if (track != NULL) {
                track->m0_inUse = 1;
                track->m1_isLooping = isLooping;
                track->m2_type = interp[1];
                track->m3_tag = tag;
                p = (s8*)data + *hdr;
                track->m10_currentTime = 0;
                track->m12_maxTime = duration;
                track->m4.stream.m4_start = p;
                track->m4.stream.m8_cursor = p;
                bones->m70_animTracks[1] = track;
            }
        } else {
        skip1:
            if (!(flags & 2) && i != 0 && transCount < numTrans) {
                pose += 3;
                transCount++;
            }
        }
        hdr += 2;
        bones++;
    }
    return 0;
}

void mechaOP_8_battle(sMechaInitVar2* arg0, sMechaBone* arg1) {
    s32 i;
    s32 n;
    sMechaBone* p;

    n = arg1->mA_numBones;
    p = arg1;
    for (i = 0; i < n; i++) {
        if (p->m70_animTracks[0] != NULL && p->m70_animTracks[0]->m3_tag != 0xFF) {
            releaseMechaAnimTrack_battle(arg0, p->m70_animTracks[0]);
            p->m70_animTracks[0] = NULL;
        }
        if (p->m70_animTracks[1] != NULL && p->m70_animTracks[1]->m3_tag != 0xFF) {
            releaseMechaAnimTrack_battle(arg0, p->m70_animTracks[1]);
            p->m70_animTracks[1] = NULL;
        }
        if (p->m70_animTracks[2] != NULL && p->m70_animTracks[2]->m3_tag != 0xFF) {
            releaseMechaAnimTrack_battle(arg0, p->m70_animTracks[2]);
            p->m70_animTracks[2] = NULL;
        }
        p++;
    }
}

void freeAnimTracks2_battle(sMechaInitVar2* arg0, sMechaBone* arg1, u8 arg2) {
    s32 i;
    s32 n;
    sMechaBone* p;

    n = arg1->mA_numBones;
    p = arg1;
    for (i = 0; i < n; i++) {
        if (p->m70_animTracks[0] != NULL && p->m70_animTracks[0]->m3_tag == arg2) {
            releaseMechaAnimTrack_battle(arg0, p->m70_animTracks[0]);
            p->m70_animTracks[0] = NULL;
        }
        if (p->m70_animTracks[1] != NULL && p->m70_animTracks[1]->m3_tag == arg2) {
            releaseMechaAnimTrack_battle(arg0, p->m70_animTracks[1]);
            p->m70_animTracks[1] = NULL;
        }
        if (p->m70_animTracks[2] != NULL && p->m70_animTracks[2]->m3_tag == arg2) {
            releaseMechaAnimTrack_battle(arg0, p->m70_animTracks[2]);
            p->m70_animTracks[2] = NULL;
        }
        p++;
    }
}

sMechaInitVar3* initMechaInitVar3_battle(sMechaInitVar3* arg0, s32 count) {
    resetMemoryAllocStats(4, 0);
    arg0->m4_count = count;
    arg0->m6_next = 0;
    arg0->m0_entries = allocateHeap((count + 1) * sizeof(sMechaInitVar3Sub), 0);
    if (arg0->m0_entries != NULL) {
        initMechaInitVar3Sub_battle(arg0);
    } else {
        return NULL;
    }
    return arg0;
}

void freeMechaInitVar3_battle(sMechaInitVar3* arg0) {
    arg0->m4_count = 0;
    arg0->m6_next = 0;
    if (arg0->m0_entries != NULL) {
        freeHeap(arg0->m0_entries);
    }
    arg0->m0_entries = NULL;
}

void initMechaInitVar3Sub_battle(sMechaInitVar3* arg0) {
    sMechaInitVar3Sub* p;
    POLY_FT4* poly;
    s32 i;
    s32 j;

    p = arg0->m0_entries;
    for (i = 0; i < arg0->m4_count + 1; i++) {
        p->m16_age = -1;
        p->m1E_lifetime = 0;
        for (j = 0; j < 2; j++) {
            poly = &p->m2C_polys[j];
            SetPolyFT4(poly);
            SetSemiTrans(poly, 1);
            p->m2C_polys[j].clut = GetClut(0, 0x1CD);
            p->m2C_polys[j].tpage = GetTPage(0, 1, 0x380, 0);
            p->m2C_polys[j].u2 = 0xF;
            p->m2C_polys[j].u0 = 0;
            p->m2C_polys[j].v0 = 0xC1;
            p->m2C_polys[j].u1 = 0;
            p->m2C_polys[j].v1 = 0xC1;
            p->m2C_polys[j].v2 = 0xC1;
            p->m2C_polys[j].u3 = 0xF;
            p->m2C_polys[j].v3 = 0xC1;
        }
        p++;
    }
}

sMechaInitVar3Sub* allocateMechaTrailSegment_battle(sMechaInitVar3* arg0, s16 arg1) {
    sMechaInitVar3Sub* p;

    if (arg0->m6_next < arg0->m4_count) {
        p = &arg0->m0_entries[arg0->m6_next];
        if (p->m16_age == -1) {
            arg0->m6_next++;
            while (arg0->m6_next < arg0->m4_count) {
                if (arg0->m0_entries[arg0->m6_next].m16_age == -1) {
                    break;
                }
                arg0->m6_next++;
            }
            SetSemiTrans(&p->m2C_polys[0], arg1);
            SetSemiTrans(&p->m2C_polys[1], arg1);
            return p;
        }
    }
    return &arg0->m0_entries[arg0->m4_count];
}

s32 releaseMechaTrailSegment_battle(sMechaInitVar3* arg0, sMechaInitVar3Sub* arg1) {
    s32 idx;

    idx = (s32)(((uptr)arg1 - (uptr)arg0->m0_entries) / sizeof(sMechaInitVar3Sub));
    if (idx <= arg0->m6_next) {
        arg0->m6_next = idx;
    }
    arg1->m16_age = -1;
    return idx;
}

/* = field_mecha func_801E0398 (Noah drawMechaWheelTrail_battle) */
void drawMechaWheelTrail_battle(sMechaInitVar3* arg0, MATRIX* m, s32 speed, OTag* ot, s32 oddOrEven) {
    sMechaInitVar3Sub* p;
    s32 i;
    s32 z;

    SetRotMatrix(m);
    SetTransMatrix(m);
    p = arg0->m0_entries;
    for (i = 0; i < arg0->m4_count; i++, p++) {
        if (p->m16_age == -1) {
            continue;
        }
        if (p->m16_age >= p->m1E_lifetime) {
#ifdef PLATFORM_PC
            if (speed > 0)
#endif
            releaseMechaTrailSegment_battle(arg0, p);
            continue;
        }
        p->m2C_polys[oddOrEven].r0 = p->m20_rgb[0] >> 6;
        p->m2C_polys[oddOrEven].g0 = p->m20_rgb[1] >> 6;
        p->m2C_polys[oddOrEven].b0 = p->m20_rgb[2] >> 6;
        if (p->mE_is3D == 0) {
            p->m2C_polys[oddOrEven].x0 = p->m0_x0;
            p->m2C_polys[oddOrEven].y0 = p->m2_y0;
            p->m2C_polys[oddOrEven].x1 = p->m8_x1;
            p->m2C_polys[oddOrEven].y1 = p->mA_y1;
            p->m2C_polys[oddOrEven].x2 = p->m10_x2;
            p->m2C_polys[oddOrEven].y2 = p->m12_y2;
            p->m2C_polys[oddOrEven].x3 = p->m18_x3;
            p->m2C_polys[oddOrEven].y3 = p->m1A_y3;
            addPrim(ot, &p->m2C_polys[oddOrEven]);
        } else {
            gte_ldv3(&p->m0_x0, &p->m8_x1, &p->m10_x2);
            gte_rtpt();
            gte_stsxy3(&p->m2C_polys[oddOrEven].x0, &p->m2C_polys[oddOrEven].x1, &p->m2C_polys[oddOrEven].x2);
            gte_stszotz(&z);
            z >>= gDepthDivider;
            gte_ldv0(&p->m18_x3);
            gte_rtps();
            gte_stsxy(&p->m2C_polys[oddOrEven].x3);
#ifdef PLATFORM_PC
            /* Projectiles may leave the preview camera's depth range. */
            if ((u32)z < 0x1000)
#endif
            addPrim(ot + z, &p->m2C_polys[oddOrEven]);
        }
        p->m16_age += speed;
        p->m20_rgb[0] -= p->m26_rgbStep[0] * speed;
        p->m20_rgb[1] -= p->m26_rgbStep[1] * speed;
        p->m20_rgb[2] -= p->m26_rgbStep[2] * speed;
    }
}

/* = func_801E0698 (init sLoadedMechas_110; bare return) */
s32 initMechaTrailEmitter_battle(sLoadedMechas_110* p, void* arg1, s16 arg2, s32 arg3, s16 arg4, s16 arg5, u8 arg6, u8 arg7, u8 arg8, u8 arg9, u8 argA, u8 argB, s16 argC, s16 argD, s16 argE, s16 argF, s16 arg10, s16 arg11, s16 arg12) {
    s32 dx;
    s32 dy;
    s32 dz;

    if (p == NULL) {
        IMPLICIT_RETURN(0);
    }
    p->m3_semiTrans = arg12;
    p->m5E_count = -1;
    p->m0_boneIndex = arg2;
    p->m2_mode = arg3;
    p->m4_pool = arg1;
    p->mC_p0.vx = argC;
    p->mC_p0.vy = argD;
    p->mC_p0.vz = argE;
    p->m14_p1.vx = argF;
    p->m14_p1.vy = arg10;
    p->m14_p1.vz = arg11;
    p->m5C_head = 0;
    if (arg4 < 7) {
        p->m60_maxCount = arg4;
    } else {
        p->m60_maxCount = 7;
    }
    p->m64_rgb[0] = arg6 << 6;
    p->m64_rgb[1] = arg7 << 6;
    p->m64_rgb[2] = arg8 << 6;
    p->m6A_rgbStep[0] = (p->m64_rgb[0] - (arg9 << 6)) / arg5;
    p->m6A_rgbStep[1] = (p->m64_rgb[1] - (argA << 6)) / arg5;
    p->m6A_rgbStep[2] = (p->m64_rgb[2] - (argB << 6)) / arg5;
    p->m62_lifetime = arg5;
    p->m8_segment = 0;
    return 0;
}

/* = func_801E0844 (CLUT step fn: end) */
void stopMechaTrailEmitter_battle(sLoadedMechas_110* p, s32 oddOrEven) {
    p->m0_boneIndex = -1;
}

/* = func_801E0850 (CLUT step fn: sine) */
s16 envTileStepSine_battle(s16 arg0, s16 arg1, s16 arg2) {
    return arg2 + (getAngleSin(arg0) + 0x1000) / arg1;
}

/* = func_801E08D4 (CLUT step fn: ramp up, -1 past 32) */
s16 envTileStepRampUp_battle(s16 arg0, s16 arg1, s16 arg2) {
    arg2 += arg0 / arg1;
    if (arg2 >= 0x21) {
        return -1;
    }
    return arg2;
}

/* = func_801E0938 (CLUT step fn: ramp down) */
s16 envTileStepRampDown_battle(s16 arg0, s16 arg1, s16 arg2) {
    return arg2 - arg0 / arg1;
}

/* = func_801E0988 (CLUT step fn: ramp down, clamped) */
s16 envTileStepRampDownClamped_battle(s16 arg0, s16 arg1, s16 arg2) {
    s16 ret;

    ret = 0x20 - arg0 / arg1;
    if (ret < arg2) {
        ret = arg2;
    }
    return ret;
}

/* = func_801E0A00 (init sLoadedMechas_118 CLUT anim) */
sLoadedMechas_118* initAnimatedEnvTextureTile_battle(sLoadedMechas_118* p, sLoadedMechas_118* parent, u16 mode, u16 flags,
    sLoadedMechas_118_1C* src, s16 a5, s16 a6, s16 a7, s16 a8, s16 a9, s16 a10, s16 x, s16 y, s16 w, s16 h,
    s16 speed, s16 arg16, s16 arg17, s16 (*fn)()) {
    RECT rect;
    s32 half;
    s32 i;
    s32 j;
    s32 color;
    u16 v;

    if (p->m1A_isActive != 0) {
        return NULL;
    }
    resetMemoryAllocStats(4, 0);
    p->m1A_isActive = 1;
    p->m10_mode = mode;
    p->m11_dirty = 0;
    p->m0_parent = parent;
    p->m14_time = 0;
    p->m18_level = 0xFFFF;
    p->m16_speed = speed;
    p->m20_stepDivisor = arg16;
    p->m22_stepBase = arg17;
    p->m24 = fn;
    p->m1C_colorMatrix = src;
    if (!(mode & 1)) {
        flags &= 0xFD0F;
    }
    if (mode & 4) {
        flags &= 0xFEFF;
    }
    if (w == 0) {
        w = 0x100;
    }
    if (h == 0) {
        h = 0x100;
    }
    switch (mode) {
    case 0:
    case 1:
        half = (w + 1) / 2;
        w = half * 2;
        p->m28_rect.x = x;
        p->m28_rect.y = y;
        p->m28_rect.w = w;
        p->m28_rect.h = h;
        p->m12_numColors = w * h;
        if (flags & 0x100) {
            p->mC_outputColors = allocateHeap(w * h * 2, 0);
        }
        if (flags & 0x200) {
            p->m8_blendColors = allocateHeap(w * h * 2, 0);
        }
        if (flags & 0x400) {
            p->m4_baseColors = allocateHeap(w * h * 2, 0);
        }
        switch (flags & 0xF) {
        case 1:
            rect.x = a5;
            rect.y = a6;
            rect.w = w;
            rect.h = h;
            StoreImage(&rect, (u32*)p->m4_baseColors);
            DrawSync(0);
            break;
        case 2:
            color = ((a7 & 0x3F) << 10) + ((a6 & 0x1F) << 5) | (a5 & 0x1F);
            for (i = 0; i < w * h; i++) {
                p->m4_baseColors[i] = color;
            }
            break;
        }
        switch ((flags >> 4) & 0xF) {
        case 1:
            rect.x = a8;
            rect.y = a9;
            rect.w = w;
            rect.h = h;
            StoreImage(&rect, (u32*)p->m8_blendColors);
            DrawSync(0);
            break;
        case 2:
            color = ((a10 & 0x3F) << 10) + ((a9 & 0x1F) << 5) | (a8 & 0x1F);
            for (i = 0; i < w * h; i++) {
                p->m8_blendColors[i] = color;
            }
            break;
        }
        break;
    case 4:
    case 5:
        p->m28_rect.x = x;
        p->m28_rect.y = y;
        p->m28_rect.w = w;
        p->m28_rect.h = h;
        p->m12_numColors = w * h;
        if (flags & 0x100) {
            p->mC_outputColors = allocateHeap(w * h * 2, 0);
        }
        if (flags & 0x200) {
            p->m8_blendColors = allocateHeap(w * h * 2, 0);
        }
        if (flags & 0x400) {
            p->m4_baseColors = allocateHeap(w * h * 2, 0);
        }
        switch (flags & 0xF) {
        case 1:
            for (i = 0; i < h; i++) {
                for (j = 0; j < w; j++) {
                    p->m4_baseColors[i * w + j] = src[a6 + i].m0_values[a5 + j];
                }
            }
            break;
        case 2:
            for (i = 0; i < h; i++) {
                for (j = 0; j < w; j++) {
                    switch ((y + i) % 3) {
                    case 0:
                        v = a5;
                        break;
                    case 1:
                        v = a6;
                        break;
                    case 2:
                        v = a7;
                        break;
                    }
                    p->m4_baseColors[i * w + j] = v;
                }
            }
            break;
        }
        switch ((flags >> 4) & 0xF) {
        case 1:
            for (i = 0; i < h; i++) {
                for (j = 0; j < w; j++) {
                    p->m8_blendColors[i * w + j] = src[a9 + i].m0_values[a8 + j];
                }
            }
            break;
        case 2:
            for (i = 0; i < h; i++) {
                for (j = 0; j < w; j++) {
                    switch ((y + i) % 3) {
                    case 0:
                        v = a8;
                        break;
                    case 1:
                        v = a9;
                        break;
                    case 2:
                        v = a10;
                        break;
                    }
                    p->m8_blendColors[i * w + j] = v;
                }
            }
            break;
        }
        break;
    }
    return p;
}

/* = func_801E1258 (Noah updateAnimatedEnvTextureTile_battle;) */
s16 updateAnimatedEnvTextureTile_battle(sLoadedMechas_118* arg0, s32 arg1) {
    sLoadedMechas_118* parent;
    RECT src;
    RECT dst;
    s16 v;
    u16* dstBuf;
    u16* srcBuf;
    s32 x;
    s32 y;

    if (arg0->m1A_isActive == 0) {
        return -1;
    }
    arg0->m14_time += arg0->m16_speed * (arg1 + 1);
    v = arg0->m24(arg0->m14_time, arg0->m20_stepDivisor, arg0->m22_stepBase);
    if (v < 0) {
        freeAnimatedEnvTextureTile_battle(arg0);
        return v;
    }
    if (v == arg0->m18_level) {
        IMPLICIT_RETURN(0); /* sic: no value; retail returns whatever is in v0 (m18) */
    }
    arg0->m18_level = v;
    switch (arg0->m10_mode) {
    case 0:
        fadePaletteColors(arg0->m12_numColors, v, arg0->mC_outputColors, arg0->m4_baseColors);
        if (arg0->m0_parent == NULL) {
            LoadImage(&arg0->m28_rect, arg0->mC_outputColors);
        }
        break;
    case 1:
        lerpPaletteColors(arg0->m12_numColors, v, arg0->mC_outputColors, arg0->m8_blendColors, arg0->m4_baseColors);
        if (arg0->m0_parent == NULL) {
            LoadImage(&arg0->m28_rect, arg0->mC_outputColors);
        }
        break;
    case 4:
        fadeEnvTileColorMatrix_battle(arg0, v);
        break;
    case 5:
        lerpEnvTileColorMatrix_battle(arg0, v);
        break;
    }
    parent = arg0->m0_parent;
    if (parent != NULL && parent->m1A_isActive != 0) {
        if (parent->m28_rect.x < arg0->m28_rect.x) {
            dst.x = arg0->m28_rect.x - parent->m28_rect.x;
            src.x = 0;
            dst.w = parent->m28_rect.x + parent->m28_rect.w - arg0->m28_rect.x;
        } else {
            dst.x = 0;
            src.x = parent->m28_rect.x - arg0->m28_rect.x;
            dst.w = arg0->m28_rect.x + arg0->m28_rect.w - parent->m28_rect.x;
        }
        if (parent->m28_rect.y < arg0->m28_rect.y) {
            dst.y = arg0->m28_rect.y - parent->m28_rect.y;
            src.y = 0;
            dst.h = parent->m28_rect.y + parent->m28_rect.h - arg0->m28_rect.y;
        } else {
            dst.y = 0;
            src.y = parent->m28_rect.y - arg0->m28_rect.y;
            dst.h = arg0->m28_rect.y + arg0->m28_rect.h - parent->m28_rect.y;
        }
        if (dst.w > 0 && dst.h > 0) {
            parent->m11_dirty = 1;
            dstBuf = parent->m4_baseColors;
            if (parent->m10_mode < 4) {
                srcBuf = arg0->mC_outputColors;
                for (y = 0; y < dst.h; y++) {
                    for (x = 0; x < dst.w; x++) {
                        *(dstBuf + dst.x + x + (dst.y + y) * parent->m28_rect.w) = *(srcBuf + src.x + x + (src.y + y) * arg0->m28_rect.w);
                    }
                }
            } else {
                for (y = 0; y < dst.h; y++) {
                    for (x = 0; x < dst.w; x++) {
                        *(dstBuf + dst.x + x + (dst.y + y) * parent->m28_rect.w) = arg0->m1C_colorMatrix[src.y + y].m0_values[src.x + x];
                    }
                }
            }
        }
    }
    return v;
}

/* = func_801E165C (free sLoadedMechas_118) */
void freeAnimatedEnvTextureTile_battle(sLoadedMechas_118* arg0) {
    if (arg0->m1A_isActive != 0) {
        if (arg0->m4_baseColors != NULL) {
            if (arg0->m10_mode < 4) {
                LoadImage(&arg0->m28_rect, arg0->m4_baseColors);
            }
            freeHeap(arg0->m4_baseColors);
            arg0->m4_baseColors = NULL;
        }
        if (arg0->m8_blendColors != NULL) {
            freeHeap(arg0->m8_blendColors);
            arg0->m8_blendColors = NULL;
        }
        if (arg0->mC_outputColors != NULL) {
            freeHeap(arg0->mC_outputColors);
            arg0->mC_outputColors = NULL;
        }
        arg0->m1A_isActive = 0;
    }
}

/* = func_801E1708 (scale CLUT) */
void fadeEnvTileColorMatrix_battle(sLoadedMechas_118* arg0, s16 arg1) {
    u16* src;
    s32 x;
    s32 y;

    src = arg0->m4_baseColors;
    for (y = 0; y < arg0->m28_rect.h; y++) {
        for (x = 0; x < arg0->m28_rect.w; x++) {
            arg0->m1C_colorMatrix[arg0->m28_rect.y + y].m0_values[arg0->m28_rect.x + x] = (*src * arg1) / 32;
            src++;
        }
    }
}

/* = func_801E17B8 (blend CLUT; "Handwritten" flag bogus) */
void lerpEnvTileColorMatrix_battle(sLoadedMechas_118* arg0, s16 arg1) {
    u16* src;
    u16* src2;
    s32 x;
    s32 y;

    src = arg0->m4_baseColors;
    src2 = arg0->m8_blendColors;
    for (y = 0; y < arg0->m28_rect.h; y++) {
        for (x = 0; x < arg0->m28_rect.w; x++) {
            arg0->m1C_colorMatrix[arg0->m28_rect.y + y].m0_values[arg0->m28_rect.x + x] = *src2 + ((*src - *src2) * arg1) / 32;
            src++;
            src2++;
        }
    }
}

void refreshAttachedMechaTransforms_battle(sLoadedMechas** mechas) {
    MATRIX* m;
    VECTOR out;
    sMechaBone* bones;
    s32 i;

    m = (MATRIX*)PSX_SCRATCH(0x1F800000);
    for (i = 0; i < 2; i++) {
        if (mechaInitVar5_battle[i].m6_isActive != 0) {
            if (mechaInitVar5_battle[i].m10_mechaIndex >= 0 && mechas[mechaInitVar5_battle[i].m10_mechaIndex] != NULL) {
                bones = mechas[mechaInitVar5_battle[i].m10_mechaIndex]->m4_bones;
                CompMatrix(&bones->mC_localMatrix, &bones[mechaInitVar5_battle[i].m12_boneIndex + 1].m2C_boneFinalMatrix, m);
                SetRotMatrix(m);
                SetTransMatrix(m);
                gte_ldv0(&mechaInitVar5_battle[i].m8_localPosition);
                gte_rt();
                gte_stlvnl(&out);
                mechaInitVar5_battle[i].m0_worldPosition[0] = out.vx;
                mechaInitVar5_battle[i].m0_worldPosition[1] = out.vy;
                mechaInitVar5_battle[i].m0_worldPosition[2] = out.vz;
            } else {
                mechaInitVar5_battle[i].m0_worldPosition[0] = mechaInitVar5_battle[i].m8_localPosition.vx;
                mechaInitVar5_battle[i].m0_worldPosition[1] = mechaInitVar5_battle[i].m8_localPosition.vy;
                mechaInitVar5_battle[i].m0_worldPosition[2] = mechaInitVar5_battle[i].m8_localPosition.vz;
            }
        }
    }
}

void battleDrawEnv(MATRIX* m, MATRIX* light, s32 arg2, OTag* ot, s32 oddOrEven, SVECTOR* eye, SVECTOR* at, s32 otSize) {
    sLoadedMechas_118* tile;
    s32 i;

    if (light != NULL) {
        SetLightMatrix(light);
    }
    tile = battleMechas[31]->m118_colorAnims;
    for (i = 0; i < battleMechas[31]->m10E_num118; i++, tile++) {
        updateAnimatedEnvTextureTile_battle(tile, battle800ccc5c);
    }
    STACK_SWITCH(PSX_SCRATCH(0x1F8003FC));
    rebuildBattleEnvironmentVramBuffer();
    STACK_RESTORE();
    for (i = 0; i < 2; i++) {
        executeBattleMoveImageCommands(&battleMoveCommands[i]);
    }
    if (environmentModelConfigs != NULL) {
        renderBattleScenePolys(environmentModelBlocks, environmentModelConfigs, m, light, arg2, ot, oddOrEven, otSize);
    }
    for (i = 0; i < 2; i++) {
        renderBackgroundPoly(battleEnvBackgroundPolys[i], eye, at, m, ot + otSize - 1, oddOrEven);
    }
    drawBattleHorizonBackdrop(battle800c3ea0, eye, at, m, ot + otSize - 1, oddOrEven);
}

/* (free environment: polys, move commands, battle800c3ea0) */
void freeBattleEnvironment(void) {
    s32 i;

    freeMecha_battle(0x1F);
    environmentModelConfigs = NULL;
    if (battleEnvMechaInitData != NULL) {
        freeHeap(battleEnvMechaInitData);
    }
    battleEnvMechaInitData = NULL;
    for (i = 0; i < 2; i++) {
        if (battleEnvBackgroundPolys[i] != NULL) {
            freeHeapIfNotNull(battleEnvBackgroundPolys[i]);
        }
        battleEnvBackgroundPolys[i] = NULL;
    }
    if (battle800c3ea0 != 0) {
        freeHeap(battle800c3ea0);
    }
    battle800c3ea0 = 0;
    for (i = 0; i < 2; i++) {
        freeImageMoveCommand(&battleMoveCommands[i]);
    }
}

void renderBattleScenePolys(sMechaInitVar4* blocks, sMechaBone* bones, MATRIX* m, MATRIX* light, s32 arg4, OTag* ot,
                   s32 oddOrEven, s32 otSize) {
    MATRIX* tmp;
    s32 backup;
    u32 i;
    u32 n;
    s32 mode;

    tmp = (MATRIX*)PSX_SCRATCH(0x1F800040);
    backup = gDepthDivider;
    n = bones->mA_numBones - 1;
    bones++;
    for (i = 0; i < n; i++, bones++) {
        if (bones->m8_geometryId != 0xFFFF && bones->m7_isEnabled) {
            CompMatrix(m, &bones->m2C_boneFinalMatrix, tmp);
            if (bones->m52_billboardMode == 1 || bones->m52_billboardMode == 2) {
                tmp->m[0][0] = 0x1000;
                tmp->m[0][2] = 0;
                tmp->m[1][0] = 0;
                tmp->m[1][2] = 0;
                tmp->m[2][0] = 0;
                tmp->m[2][2] = 0x1000;
                if (bones->m52_billboardMode == 1) {
                    tmp->m[0][1] = m->m[0][1];
                    tmp->m[1][1] = m->m[1][1];
                    tmp->m[2][1] = m->m[2][1];
                } else {
                    tmp->m[0][1] = 0;
                    tmp->m[1][1] = 0x1000;
                    tmp->m[2][1] = 0;
                }
            }
            SetRotMatrix(tmp);
            SetTransMatrix(tmp);
            switch (bones->m52_billboardMode) {
            case 4:
                mode = 2;
                break;
            case 5:
                mode = 3;
                break;
            case 6:
                mode = 4;
                break;
            case 7:
                mode = 5;
                break;
            default:
                mode = 0;
                break;
            }
            if (bones->m52_billboardMode == 0) {
                gDepthDivider = 0x10;
                submitModelForRendering(blocks->m0_blocks[bones->m8_geometryId], bones->m68_prims[oddOrEven], ot + otSize - 1, mode);
            } else {
                gDepthDivider = backup;
                submitModelForRendering(blocks->m0_blocks[bones->m8_geometryId], bones->m68_prims[oddOrEven], ot, mode);
            }
        }
    }
    gDepthDivider = backup;
}

s32 pushOutOfEnvironmentColliders(SVECTOR* center, SVECTOR* pos) {
    s16* p;
    s32 i;
    s32 result;
    s16 cx;
    s16 cz;
    s16 r;
    s32 dx;
    s32 dz;
    s32 d;

    result = 0;
    p = battleEnvColliders;
    for (i = 0; i < battleEnvColliderCount; i++) {
        cx = *p++ - center->vx;
        dx = pos->vx - cx;
        cz = *p++ - center->vz;
        dz = pos->vz - cz;
        r = *p++;
        d = SquareRoot0(dx * dx + dz * dz) + 1;
        if (d < r) {
            pos->vx = cx + dx * r / d;
            pos->vz = cz + dz * r / d;
            result = 1;
            break;
        }
    }
    return result;
}

void finalizeMechaForEvent(s32 mechaIdx) {
    s16* p;
    s32 i;
    s16 x;
    s16 z;
    s16 r;

    p = battleEnvColliders;
    for (i = 0; i < battleEnvColliderCount; i++) {
        x = *p++;
        z = *p++;
        r = *p++;
        pushMechaAway(mechaIdx, x, z, getMechaColliderRadius_battle(mechaIdx) + r);
    }
}

void drawBattleHorizonBackdrop(sBattleHorizon* h, SVECTOR* eye, SVECTOR* at, MATRIX* m, OTag* ot, s32 oddOrEven) {
    SVECTOR unused;
    MATRIX mat;
    MATRIX rotY;
    SVECTOR rot;
    VECTOR d;
    DVECTOR sxy0;
    DVECTOR sxy1;
    VECTOR dir;
    SVECTOR pos;
    SVECTOR n;
    s32 opz;
    s32 angle;
    s32 tilt;
    u16 u;
    u16 v;
    s16 half;
    s32 size;
    s32 i;
    s32 j;
    s32 k;
    u8 u0;
    u8 u1;
    u8 tu;
    s32 mv;
    SVECTOR* vtx;

    if (h != NULL) {
        addPrim(ot, &h->m3C_drawModes[oddOrEven]);
        dir.vx = at->vx - eye->vx;
        dir.vy = 0;
        dir.vz = at->vz - eye->vz;
        VectorNormalS(&dir, &n);
        pos.vx = n.vx * h->m18_horizonDistance / 4096 + at->vx;
        pos.vy = h->m16_horizonY;
        pos.vz = n.vz * h->m18_horizonDistance / 4096 + at->vz;
        SetRotMatrix(m);
        SetTransMatrix(m);
        gte_ldv0(&pos);
        gte_rtps();
        gte_stsxy(&sxy0);
        sxy0.vx = sxy0.vy;
        if (sxy0.vy > 240) {
            sxy0.vy = 240;
        }
        pos.vx = n.vx * h->m18_horizonDistance / 4096 * h->mE_groundBandScale / 256 + at->vx;
        pos.vy = h->m16_horizonY * h->mE_groundBandScale / 256;
        pos.vz = n.vz * h->m18_horizonDistance / 4096 * h->mE_groundBandScale / 256 + at->vz;
        gte_ldv0(&pos);
        gte_rtps();
        gte_stsxy(&sxy1);
        if (sxy1.vy >= 0 && sxy0.vy < 240) {
            h->m173C_gradient[oddOrEven + 2].y0 = sxy0.vy;
            h->m173C_gradient[oddOrEven + 2].y1 = sxy0.vy;
            h->m173C_gradient[oddOrEven + 2].y2 = sxy1.vy;
            h->m173C_gradient[oddOrEven + 2].y3 = sxy1.vy;
            addPrim(ot, &h->m173C_gradient[oddOrEven + 2]);
        }
        if (sxy1.vy < 0) {
            sxy1.vy = 0;
        }
        if (sxy1.vy < 240) {
            h->m16DC_flat[oddOrEven + 2].y0 = sxy1.vy;
            h->m16DC_flat[oddOrEven + 2].y1 = sxy1.vy;
            addPrim(ot, &h->m16DC_flat[oddOrEven + 2]);
        }
        pos.vx = n.vx * h->m18_horizonDistance / 4096 * h->mC_skyBandScale / 256 + at->vx;
        pos.vy = h->m16_horizonY * h->mC_skyBandScale / 256;
        pos.vz = n.vz * h->m18_horizonDistance / 4096 * h->mC_skyBandScale / 256 + at->vz;
        gte_ldv0(&pos);
        gte_rtps();
        gte_stsxy(&sxy1);
        sxy1.vy = sxy0.vy * 2 - sxy1.vy - 8;
        if (sxy0.vx >= 0 && sxy0.vx < 480 && sxy1.vy < 240) {
            h->m173C_gradient[oddOrEven].y0 = sxy0.vx;
            h->m173C_gradient[oddOrEven].y1 = sxy0.vx;
            h->m173C_gradient[oddOrEven].y2 = sxy1.vy;
            h->m173C_gradient[oddOrEven].y3 = sxy1.vy;
            addPrim(ot, &h->m173C_gradient[oddOrEven]);
        }
        d.vx = at->vx - eye->vx;
        d.vy = 0;
        d.vz = at->vz - eye->vz;
        angle = ratan2(at->vy - eye->vy, SquareRoot0(d.vx * d.vx + d.vz * d.vz));
        tilt = (angle - 0x100) * h->m12_tiltScale / 512 * (0x400 - abs(angle)) / 1024;
        SetGeomScreen(h->m10_geomScreen);
        rot.vx = 0;
        rot.vy = -ratan2(d.vx, d.vz);
        rot.vz = 0;
        createRotationMatrix(&rot, &rotY);
        rotY.t[0] = 0;
        rotY.t[1] = 0;
        rotY.t[2] = 0;
        rot.vx = tilt;
        rot.vy = -rot.vy;
        RotMatrixYXZ(&rot, &mat);
        VectorNormalS(&d, &rot);
        d.vx = eye->vx + rot.vx * 2;
        d.vy = eye->vy / 4 - h->mA_height;
        d.vz = eye->vz + rot.vz * 2;
        mat.t[0] = d.vx;
        mat.t[1] = d.vy;
        mat.t[2] = d.vz;
        CompMatrix(&mat, &rotY, &mat);
        CompMatrix(m, &mat, &mat);
        SetRotMatrix(&mat);
        SetTransMatrix(&mat);
        h->m0_u += h->m4_du;
        h->m2_v += h->m6_dv;
        u = (h->m0_u / 16 + d.vx / 12) & ((size = h->m8_texSize) - 1);
        v = (h->m2_v / 16 + d.vz / 12) & (size - 1);
        k = oddOrEven * 64;
        vtx = h->m54_grid;
        half = (s16)size / 2;
        for (i = 0; i < 8; i++) {
            for (j = 0; j < 8; j++, k++, vtx++) {
                mv = (i & 1) * half;
                tu = u;
                gte_ldv3(&vtx[0], &vtx[1], &vtx[9]);
                gte_rtpt();
                gte_nclip();
                gte_stopz(&opz);
#ifdef PLATFORM_PC
#define SKY_QUAD (*skyQuad)
                POLY_FT4* skyQuad = &h->m2DC_quads[k];
                GteState saved;
                int rescue = opz < 0 && pcGeometryRescue();
                if (rescue) {
                    skyQuad = pcGeometryAlloc(skyQuad, sizeof(*skyQuad));
                    if (skyQuad) gteSave(&saved);
                }
                if (opz >= 0 || (rescue && skyQuad)) {
#else
#define SKY_QUAD h->m2DC_quads[k]
                if (opz >= 0) {
#endif
                    gte_stsxy3(&SKY_QUAD.x0, &SKY_QUAD.x1, &SKY_QUAD.x2);
                    gte_ldv0(&vtx[10]);
                    gte_rtps();
                    gte_stsxy(&SKY_QUAD.x3);
                    SKY_QUAD.v0 = mv + v;
                    SKY_QUAD.v1 = mv + v;
                    SKY_QUAD.v2 = mv + v + half - 1;
                    SKY_QUAD.v3 = mv + v + half - 1;
                    u0 = (j & 1) * half;
                    u0 += tu;
                    u1 = u0 + half - 1;
                    SKY_QUAD.u0 = u0;
                    SKY_QUAD.u2 = u0;
                    SKY_QUAD.u1 = u1;
                    SKY_QUAD.u3 = u1;
                    addPrim(ot, &SKY_QUAD);
#ifdef PLATFORM_PC
                    if (rescue) gteLoad(&saved);
#endif
                }
#undef SKY_QUAD
            }
            vtx++;
        }
        SetGeomScreen(0x200);
        if (sxy0.vy >= 0) {
            h->m16DC_flat[oddOrEven].y2 = sxy0.vy;
            h->m16DC_flat[oddOrEven].y3 = sxy0.vy;
            addPrim(ot, &h->m16DC_flat[oddOrEven]);
        }
        addPrim(ot, &h->m24_drawModes[oddOrEven]);
    }
}

/* (getter: battleTerrainVertices) */
SVECTOR* getBattleTerrainVertices(void) {
    return battleTerrainVertices;
}

/* (getter: battleTerrainTriangles) */
sBattleTerrainTriangle* getBattleTerrainTriangles(void) {
    return battleTerrainTriangles;
}

s32 findBattleTerrainTriangleBruteForce(SVECTOR* pos) {
    s32 i;
    sBattleTerrainTriangle* tri;

    if (battleTerrainVertices == NULL) {
        return -1;
    }
    if (battleTerrainTriangles == NULL) {
        return -1;
    }
    for (i = 0; i < battleTerrainTriangleCount; i++) {
        tri = &battleTerrainTriangles[i];
        if (isPointInsideTriangleXZ_battle(&battleTerrainVertices[tri->m0_vertexIndices[0]], &battleTerrainVertices[tri->m0_vertexIndices[1]],
                          &battleTerrainVertices[tri->m0_vertexIndices[2]], pos) == -1) {
            return i;
        }
    }
    return -1;
}

s32 computeTerrainHeightAtTriangle(SVECTOR* pos, s32 triangle, VECTOR* normal) {
    sBattleTerrainTriangle* tri;

    if (battleTerrainVertices == NULL || battleTerrainTriangles == NULL || triangle < 0) {
        return -1;
    }
    tri = &battleTerrainTriangles[triangle];
    getTriangleNormalAndAdjustY_battle(&battleTerrainVertices[tri->m0_vertexIndices[0]], &battleTerrainVertices[tri->m0_vertexIndices[1]],
                  &battleTerrainVertices[tri->m0_vertexIndices[2]], pos, normal);
    return battleTerrainTriangles[triangle].mC_flags;
}


/* Noah: findBattleTerrainTriangle. K&R definition: the callers pass three
 * arguments and the loop result starts as the fourth argument register. */
s32 findBattleTerrainTriangle(pos, triangle, depth, result)
    SVECTOR* pos;
    s32 triangle;
    s32 depth;
    s32 result;
{
    s32 i;

    if (battleTerrainVertices == NULL) {
        return -1;
    }
    if (battleTerrainTriangles == NULL) {
        return -1;
    }
    if (triangle >= battleTerrainTriangleCount) {
        return -1;
    }
    for (i = 0; i < depth; i++) {
        result = findBattleTerrainTriangleRecursive(pos, triangle, depth);
        if (result >= 0) {
            break;
        }
    }
    battleTerrainSearchGeneration++;
    if (battleTerrainSearchGeneration == 0) {
        battleTerrainSearchGeneration = 1;
        for (i = 0; i < battleTerrainTriangleCount; i++) {
            battleTerrainTriangles[i].mD_visited = 0;
        }
    }
    return result;
}

/* Noah: isPointInsideTriangleXZ_battle (battleTerrain.cpp): -1 inside, 0 outside */
s32 isPointInsideTriangleXZ_battle(SVECTOR* v0, SVECTOR* v1, SVECTOR* v2, SVECTOR* point) {
    VECTOR edge;
    VECTOR toPoint;
    VECTOR cross;

    edge.vx = v1->vx - v0->vx;
    edge.vy = 0;
    edge.vz = v1->vz - v0->vz;
    toPoint.vx = point->vx - v0->vx;
    toPoint.vy = 0;
    toPoint.vz = point->vz - v0->vz;
    OuterProduct0(&edge, &toPoint, &cross);
    if (cross.vy < 0) {
        goto fail;
    }
    edge.vx = v2->vx - v1->vx;
    edge.vy = 0;
    edge.vz = v2->vz - v1->vz;
    toPoint.vx = point->vx - v1->vx;
    toPoint.vy = 0;
    toPoint.vz = point->vz - v1->vz;
    OuterProduct0(&edge, &toPoint, &cross);
    if (cross.vy >= 0) {
        goto ok;
    }
fail:
    return 0;
ok:
    edge.vx = v0->vx - v2->vx;
    edge.vy = 0;
    edge.vz = v0->vz - v2->vz;
    toPoint.vx = point->vx - v2->vx;
    toPoint.vy = 0;
    toPoint.vz = point->vz - v2->vz;
    OuterProduct0(&edge, &toPoint, &cross);
    return ~cross.vy >> 31;
}

/* Noah: computeTerrainHeightAtTriangle's normal/height part (field:
 * getTriangleNormalAndAdjustY) */
void getTriangleNormalAndAdjustY_battle(SVECTOR* v0, SVECTOR* v1, SVECTOR* v2, SVECTOR* pos, VECTOR* normal) {
    VECTOR n01;
    VECTOR n02;
    VECTOR edge;

    edge.vx = v1->vx - v0->vx;
    edge.vy = v1->vy - v0->vy;
    edge.vz = v1->vz - v0->vz;
    VectorNormal(&edge, &n01);
    edge.vx = v2->vx - v0->vx;
    edge.vy = v2->vy - v0->vy;
    edge.vz = v2->vz - v0->vz;
    VectorNormal(&edge, &n02);
    OuterProduct12(&n01, &n02, normal);
    if (normal->vy == 0) {
        pos->vy = 0;
    } else {
        pos->vy = v0->vy + (-((pos->vx - v0->vx) * normal->vx) - (pos->vz - v0->vz) * normal->vz) / normal->vy;
    }
}

s32 findBattleTerrainTriangleRecursive(SVECTOR* pos, s32 triangle, s32 depth) {
    s32 result;

    if (triangle < 0) {
        return -1;
    }
    if (battleTerrainTriangles[triangle].mD_visited != battleTerrainSearchGeneration) {
        battleTerrainTriangles[triangle].mD_visited = battleTerrainSearchGeneration;
        if (isPointInsideTriangleXZ_battle(&battleTerrainVertices[battleTerrainTriangles[triangle].m0_vertexIndices[0]], &battleTerrainVertices[battleTerrainTriangles[triangle].m0_vertexIndices[1]],
                          &battleTerrainVertices[battleTerrainTriangles[triangle].m0_vertexIndices[2]], pos) == -1) {
            return triangle;
        }
    }
    if (depth > 0) {
        result = findBattleTerrainTriangleRecursive(pos, battleTerrainTriangles[triangle].m6_neighborIndices[0], depth - 1);
        if (result >= 0) {
            return result;
        }
        result = findBattleTerrainTriangleRecursive(pos, battleTerrainTriangles[triangle].m6_neighborIndices[1], depth - 1);
        if (result >= 0) {
            return result;
        }
        result = findBattleTerrainTriangleRecursive(pos, battleTerrainTriangles[triangle].m6_neighborIndices[2], depth - 1);
        if (result >= 0) {
            return result;
        }
    }
    return -1;
}

/* (setter battleDrawEnvBgColor0/battleDrawEnvBgColor1) */
void setupBattleDrawEnvs(u8* arg0, u8* arg1) {
    battleDrawEnvBgColor0 = arg0;
    battleDrawEnvBgColor1 = arg1;
}

void initBattleEnvColorEffects(void) {
    sLoadedMechas_118* p;
    u8* dst;
    s32 i;

    for (i = 0; i < 4; i++) {
        battleEnvColorEffects[i].m0_enabled = 0;
    }
    p = battleMechas[31]->m118_colorAnims;
    for (i = 0; i < battleMechas[31]->m10E_num118; i++) {
        if (p->m1A_isActive != 0) {
            p->m0_parent = &battleEnvColorAnim;
        }
        p++;
    }
    initAnimatedEnvTextureTile_battle(&battleEnvColorAnim, NULL, 1, 0x601, NULL, battleEnvTextureRectX, battleEnvTextureRectY, 0, 0, 0, 0, battleEnvTextureRectX, battleEnvTextureRectY,
                  battleEnvTextureRectW, battleEnvTextureRectH, 0, 0, 0, NULL);
    battleEnvColorAnim.mC_outputColors = NULL;
    battleEnvBaseColors = allocateHeap(0x88, 1);
    battleEnvBlendedColors = allocateHeap(0x88, 1);
    dst = battleEnvBaseColors;
    for (i = 0; i < 4; i++) {
        if (battle800c3ea0 != NULL) {
            dst[0] = battle800c3ea0->m173C_gradient[i].r0;
            dst[1] = battle800c3ea0->m173C_gradient[i].g0;
            dst[2] = battle800c3ea0->m173C_gradient[i].b0;
            dst += 4;
            dst[0] = battle800c3ea0->m173C_gradient[i].r1;
            dst[1] = battle800c3ea0->m173C_gradient[i].g1;
            dst[2] = battle800c3ea0->m173C_gradient[i].b1;
            dst += 4;
            dst[0] = battle800c3ea0->m173C_gradient[i].r2;
            dst[1] = battle800c3ea0->m173C_gradient[i].g2;
            dst[2] = battle800c3ea0->m173C_gradient[i].b2;
            dst += 4;
            dst[0] = battle800c3ea0->m173C_gradient[i].r3;
            dst[1] = battle800c3ea0->m173C_gradient[i].g3;
            dst[2] = battle800c3ea0->m173C_gradient[i].b3;
            dst += 4;
            dst[0] = battle800c3ea0->m16DC_flat[i].r0;
            dst[1] = battle800c3ea0->m16DC_flat[i].g0;
            dst[2] = battle800c3ea0->m16DC_flat[i].b0;
            dst += 4;
        } else {
            dst += 20;
        }
        if (battleEnvBackgroundPolys[0] != NULL) {
            dst[0] = battleEnvBackgroundPolys[0]->m280_f4[i].r0;
            dst[1] = battleEnvBackgroundPolys[0]->m280_f4[i].g0;
            dst[2] = battleEnvBackgroundPolys[0]->m280_f4[i].b0;
        }
        dst += 4;
    }
    for (i = 0; i < 2; i++) {
        if (battleEnvBackgroundPolys[0] != NULL) {
            dst[0] = battleEnvBackgroundPolys[0]->m2E0_g4[i].r0;
            dst[1] = battleEnvBackgroundPolys[0]->m2E0_g4[i].g0;
            dst[2] = battleEnvBackgroundPolys[0]->m2E0_g4[i].b0;
            dst += 4;
            dst[0] = battleEnvBackgroundPolys[0]->m2E0_g4[i].r1;
            dst[1] = battleEnvBackgroundPolys[0]->m2E0_g4[i].g1;
            dst[2] = battleEnvBackgroundPolys[0]->m2E0_g4[i].b1;
            dst += 4;
            dst[0] = battleEnvBackgroundPolys[0]->m2E0_g4[i].r2;
            dst[1] = battleEnvBackgroundPolys[0]->m2E0_g4[i].g2;
            dst[2] = battleEnvBackgroundPolys[0]->m2E0_g4[i].b2;
            dst += 4;
            dst[0] = battleEnvBackgroundPolys[0]->m2E0_g4[i].r3;
            dst[1] = battleEnvBackgroundPolys[0]->m2E0_g4[i].g3;
            dst[2] = battleEnvBackgroundPolys[0]->m2E0_g4[i].b3;
            dst += 4;
        } else {
            dst += 16;
        }
    }
    dst[0] = battleDrawEnvBgColor0[0];
    dst[1] = battleDrawEnvBgColor0[1];
    dst[2] = battleDrawEnvBgColor0[2];
    dst += 4;
    dst[0] = battleDrawEnvBgColor1[0];
    dst[1] = battleDrawEnvBgColor1[1];
    dst[2] = battleDrawEnvBgColor1[2];
}

void setBattleEnvColorEffect(s32 index, s32 mode, s32 a2, s32 a3, s32 a4, s32 a5) {
    if (index < 4) {
        if (mode >= 0) {
            battleEnvColorAnim.m11_dirty = 1;
            battleEnvColorEffects[index].m0_enabled = 1;
            battleEnvColorEffects[index].m1_mode = mode;
            battleEnvColorEffects[index].m2_level = a2;
            battleEnvColorEffects[index].m3_rOffset = a3;
            battleEnvColorEffects[index].m4_gOffset = a4;
            battleEnvColorEffects[index].m5_bOffset = a5;
        } else {
            battleEnvColorEffects[index].m0_enabled = 0;
        }
    }
}

void restoreBattleEnvColors(void) {
    u8* src;
    s32 i;

    src = battleEnvBaseColors;
    for (i = 0; i < 4; i++) {
        if (battle800c3ea0 != NULL) {
            battle800c3ea0->m173C_gradient[i].r0 = src[0];
            battle800c3ea0->m173C_gradient[i].g0 = src[1];
            battle800c3ea0->m173C_gradient[i].b0 = src[2];
            src += 4;
            battle800c3ea0->m173C_gradient[i].r1 = src[0];
            battle800c3ea0->m173C_gradient[i].g1 = src[1];
            battle800c3ea0->m173C_gradient[i].b1 = src[2];
            src += 4;
            battle800c3ea0->m173C_gradient[i].r2 = src[0];
            battle800c3ea0->m173C_gradient[i].g2 = src[1];
            battle800c3ea0->m173C_gradient[i].b2 = src[2];
            src += 4;
            battle800c3ea0->m173C_gradient[i].r3 = src[0];
            battle800c3ea0->m173C_gradient[i].g3 = src[1];
            battle800c3ea0->m173C_gradient[i].b3 = src[2];
            src += 4;
            battle800c3ea0->m16DC_flat[i].r0 = src[0];
            battle800c3ea0->m16DC_flat[i].g0 = src[1];
            battle800c3ea0->m16DC_flat[i].b0 = src[2];
            src += 4;
        } else {
            src += 20;
        }
        if (battleEnvBackgroundPolys[0] != NULL) {
            battleEnvBackgroundPolys[0]->m280_f4[i].r0 = src[0];
            battleEnvBackgroundPolys[0]->m280_f4[i].g0 = src[1];
            battleEnvBackgroundPolys[0]->m280_f4[i].b0 = src[2];
        }
        src += 4;
    }
    for (i = 0; i < 2; i++) {
        if (battleEnvBackgroundPolys[0] != NULL) {
            battleEnvBackgroundPolys[0]->m2E0_g4[i].r0 = src[0];
            battleEnvBackgroundPolys[0]->m2E0_g4[i].g0 = src[1];
            battleEnvBackgroundPolys[0]->m2E0_g4[i].b0 = src[2];
            src += 4;
            battleEnvBackgroundPolys[0]->m2E0_g4[i].r1 = src[0];
            battleEnvBackgroundPolys[0]->m2E0_g4[i].g1 = src[1];
            battleEnvBackgroundPolys[0]->m2E0_g4[i].b1 = src[2];
            src += 4;
            battleEnvBackgroundPolys[0]->m2E0_g4[i].r2 = src[0];
            battleEnvBackgroundPolys[0]->m2E0_g4[i].g2 = src[1];
            battleEnvBackgroundPolys[0]->m2E0_g4[i].b2 = src[2];
            src += 4;
            battleEnvBackgroundPolys[0]->m2E0_g4[i].r3 = src[0];
            battleEnvBackgroundPolys[0]->m2E0_g4[i].g3 = src[1];
            battleEnvBackgroundPolys[0]->m2E0_g4[i].b3 = src[2];
            src += 4;
        } else {
            src += 16;
        }
    }
    battleDrawEnvBgColor0[0] = src[0];
    battleDrawEnvBgColor0[1] = src[1];
    battleDrawEnvBgColor0[2] = src[2];
    src += 4;
    battleDrawEnvBgColor1[0] = src[0];
    battleDrawEnvBgColor1[1] = src[1];
    battleDrawEnvBgColor1[2] = src[2];
}

void applyEnvColorEffectToColor(u8* out, s32 index, CVECTOR* col) {
    s32 r;
    s32 g;
    s32 b;
    s32 avg;

    r = battleEnvColorEffects[index].m3_rOffset * 8;
    g = battleEnvColorEffects[index].m4_gOffset * 8;
    b = battleEnvColorEffects[index].m5_bOffset * 8;
    if (battleEnvColorEffects[index].m0_enabled != 0) {
        switch (battleEnvColorEffects[index].m1_mode) {
        case 0:
            r += col->r;
            g += col->g;
            b += col->b;
            break;
        case 1:
            r += col->r >> 1;
            g += col->g >> 1;
            b += col->b >> 1;
            break;
        case 2:
            r += col->r >> 2;
            g += col->g >> 2;
            b += col->b >> 2;
            break;
        case 3:
            avg = (col->r + col->g + col->b) / 3;
            r += avg;
            g += avg;
            b += avg;
            break;
        case 4:
            r = 0;
            g = 0;
            b = 0;
            break;
        }
    }
    r += (col->r - r) * battleEnvColorEffects[index].m2_level / 32;
    g += (col->g - g) * battleEnvColorEffects[index].m2_level / 32;
    b += (col->b - b) * battleEnvColorEffects[index].m2_level / 32;
    if (r >= 0x100) {
        col->r = 0xFF;
    } else if (r < 0) {
        col->r = 0;
    } else {
        col->r = r;
    }
    if (g >= 0x100) {
        col->g = 0xFF;
    } else if (g < 0) {
        col->g = 0;
    } else {
        col->g = g;
    }
    if (b >= 0x100) {
        col->b = 0xFF;
    } else if (b < 0) {
        col->b = 0;
    } else {
        col->b = b;
    }
    out[0] = col->r;
    out[1] = col->g;
    out[2] = col->b;
}

void rebuildBattleEnvironmentVramBuffer(void) {
    CVECTOR* src;
    s32 i;
    s32 j;

    if (battleEnvColorAnim.m11_dirty != 0 && battleEnvColorAnim.m1A_isActive != 0) {
        battleEnvColorAnim.m11_dirty = 0;
        memcpy(battleEnvColorAnim.m8_blendColors, battleEnvColorAnim.m4_baseColors, battleEnvTextureRectW * battleEnvTextureRectH * 2);
        *(sBattleEnvSavedColors*)battleEnvBlendedColors = *(sBattleEnvSavedColors*)battleEnvBaseColors;
        for (i = 0; i < 4; i++) {
            if (battleEnvColorEffects[i].m0_enabled == 0) {
                continue;
            }
            switch (battleEnvColorEffects[i].m1_mode) {
            case 0:
            case 1:
            case 2:
            case 3:
                blendPaletteColors(battleEnvTextureRectW * battleEnvTextureRectH, battleEnvColorAnim.m8_blendColors, battleEnvColorAnim.m8_blendColors, battleEnvColorAnim.m8_blendColors,
                              battleEnvColorEffects[i].m3_rOffset, battleEnvColorEffects[i].m4_gOffset, battleEnvColorEffects[i].m5_bOffset, battleEnvColorEffects[i].m1_mode,
                              battleEnvColorEffects[i].m2_level);
                break;
            case 4:
                fadePaletteColors(battleEnvTextureRectW * battleEnvTextureRectH, battleEnvColorEffects[i].m2_level, battleEnvColorAnim.m8_blendColors, battleEnvColorAnim.m4_baseColors);
                break;
            }
            src = battleEnvBlendedColors;
            for (j = 0; j < 4; j++) {
                if (battle800c3ea0 != NULL) {
                    applyEnvColorEffectToColor(&battle800c3ea0->m173C_gradient[j].r0, i, src++);
                    applyEnvColorEffectToColor(&battle800c3ea0->m173C_gradient[j].r1, i, src++);
                    applyEnvColorEffectToColor(&battle800c3ea0->m173C_gradient[j].r2, i, src++);
                    applyEnvColorEffectToColor(&battle800c3ea0->m173C_gradient[j].r3, i, src++);
                    applyEnvColorEffectToColor(&battle800c3ea0->m16DC_flat[j].r0, i, src++);
                } else {
                    src += 5;
                }
                if (battleEnvBackgroundPolys[0] != NULL) {
                    applyEnvColorEffectToColor(&battleEnvBackgroundPolys[0]->m280_f4[j].r0, i, src++);
                } else {
                    src++;
                }
            }
            for (j = 0; j < 2; j++) {
                if (battleEnvBackgroundPolys[0] != NULL) {
                    applyEnvColorEffectToColor(&battleEnvBackgroundPolys[0]->m2E0_g4[j].r0, i, src++);
                    applyEnvColorEffectToColor(&battleEnvBackgroundPolys[0]->m2E0_g4[j].r1, i, src++);
                    applyEnvColorEffectToColor(&battleEnvBackgroundPolys[0]->m2E0_g4[j].r2, i, src++);
                    applyEnvColorEffectToColor(&battleEnvBackgroundPolys[0]->m2E0_g4[j].r3, i, src++);
                } else {
                    src += 4;
                }
            }
            applyEnvColorEffectToColor(battleDrawEnvBgColor0, i, src++);
            applyEnvColorEffectToColor(battleDrawEnvBgColor1, i, src);
        }
        LoadImage(&battleEnvColorAnim.m28_rect, battleEnvColorAnim.m8_blendColors);
    }
}

void freeBattleEnvColorEffects(void) {
    sLoadedMechas_118* p;
    s32 i;

    if (battleEnvColorAnim.m1A_isActive != 0) {
        p = battleMechas[31]->m118_colorAnims;
        for (i = 0; i < battleMechas[31]->m10E_num118; i++) {
            if (p->m1A_isActive != 0) {
                p->m0_parent = NULL;
            }
            p++;
        }
        LoadImage(&battleEnvColorAnim.m28_rect, battleEnvColorAnim.m4_baseColors);
        DrawSync(0);
        freeAnimatedEnvTextureTile_battle(&battleEnvColorAnim);
    }
    restoreBattleEnvColors();
    freeHeap(battleEnvBaseColors);
    freeHeap(battleEnvBlendedColors);
}

/* 0x800A7064-0x800A8B0C: cloth parts, see b_800A7064.c (BG6; twin of field_mecha fm_801E1A14.c) */
