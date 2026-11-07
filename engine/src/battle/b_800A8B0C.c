/* Battle overlay, 0x800A8B0C-0x800B15D8: the gear-library code after the cloth
 * object (b_800A7064.c), with divide checks like b_8009EBA8.c. Its .rodata
 * starts at 0x8007056C with processMechaAnimData_battle's jump tables. */
#include "common.h"
#include "psyq.h"
#include "gte.h"
#include "stack.h"
#include "main/memory.h"
#include "main/filesystem.h"
#include "main/sprite.h"
#include "battle/battle.h"
#include "pc_convert.h"
#include "pc_retail_heap.h"
void modelsBattleRigCreated(s32 slot);

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
/* port/src/game/battle/gearpose.c: Gears drawn at 60 Hz (Smooth motion) */
void pcGearPoseFrame(void);
void pcBattle60SceneAfterStep(void);
void pcGearPoseBefore(const sLoadedMechas* m, s32 iterations);
void pcGearPoseAfter(const sLoadedMechas* m);
void pcGearPoseAttach(const sLoadedMechas* m, const sLoadedMechas* parent);
s32 pcGearPoseFor(const sLoadedMechas* m);
MATRIX* pcGearPoseRoot(s32 s, MATRIX* game);
MATRIX* pcGearPoseFinal(s32 s, s32 bone, MATRIX* game);
int pcGameplayBattle60(void);
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
/* Its three parts. The PC keeps the file as loaded ([3][offsets], each from
 * the file's start) and reads the offsets where the PS1 relocated them. */
#define EXTRA_ATTACHMENTS(e) ((u16*)PC_RELOC_ENTRY(e, 1))
#define EXTRA_DATA2(e) PC_RELOC_ENTRY(e, 2)
#define EXTRA_PARTS(e) PC_RELOC_ENTRY(e, 3)

s32 getAngleSin(s32);
void fadePaletteColors(s32 count, s32 level, u16* dst, u16* src);
void lerpPaletteColors(s32 count, s32 level, u16* dst, u16* src1, u16* src2);
s32 releaseMechaTrailSegment_battle(sMechaInitVar3* arg0, sMechaInitVar3Sub* arg1);
void freeAnimatedEnvTextureTile_battle(sLoadedMechas_118* arg0);
void fadeEnvTileColorMatrix_battle(sLoadedMechas_118* arg0, s16 arg1);
void lerpEnvTileColorMatrix_battle(sLoadedMechas_118* arg0, s16 arg1);
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
extern sBattleEnvColorEffect battleEnvColorEffects[4];
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

/* defined in earlier files */
s32 initMechaTrailEmitter_battle(sLoadedMechas_110* p, void* arg1, s16 arg2, s32 arg3, s16 arg4, s16 arg5, u8 arg6, u8 arg7, u8 arg8, u8 arg9, u8 argA, u8 argB, s16 argC, s16 argD, s16 argE, s16 argF, s16 arg10, s16 arg11, s16 arg12);
void stopMechaTrailEmitter_battle(sLoadedMechas_110* p, s32 oddOrEven);

void mechaInitForBattle(void) {
    s32 i;

    battleMechaVar0 = 0;
    battleMechaVar1 = 0;
    battleMechaVar2 = 0;
    battleMechaVar3 = 0;
    battleMechaVar4 = 0;
    battleMechaVar5 = 1;
    initMechaInitVar2_battle(&battleMechaInitVar2, battleEnvMechaInitData->m348_trackPoolSize);
    initMechaInitVar3_battle(&battleMechaInitVar3, battleEnvMechaInitData->m34A_trailPoolSize);
    clearMechaEventAnimTracks();
    for (i = 0; i < 32; i++) {
        battleMechas[i] = NULL;
    }
    for (i = 0; i < 20; i++) {
        battleMechaInitVar4[i].m0_blocks = NULL;
    }
    for (i = 0; i < 2; i++) {
        mechaInitVar5_battle[i].m6_isActive = 0;
    }
}

/* Noah: mechaInitEnvironmentMechaMesh (battle copy of field_mecha
 * func_801E742C): builds battle mecha entryId (< 32) from its data tables,
 * with the battle config's shadow colour and scale and terrain snapping. */
void mechaInitEnvironmentMechaMesh(s32 entryId, u16 flags, sMechaDataTable2* pData2, sMechaDataTable1* pData1, s16 tpageX,
                   s16 tpageY, s16 clutX, s16 clutY, SVECTOR* pPosition) {
    sLoadedMechas* pMecha;
    sMechaDataTable2_4* pAnim;
    sMechaDataTable2_8* pSound;
    sMechaDataTable1_10* pTable10;
    void* pTextures;
    u8* pModel;
    u8* pBoneTable;
    s32 modelSize;
    s32 i;
    s32 j;
    u16* p;
    sLoadedMechas_114* p114;
    s32 count;
    void* pNewModel;
    s32 size;

    resetMemoryAllocStats(4, 0);
    if (entryId >= 32) {
        return;
    }
    if (battleMechas[entryId] != NULL) {
        return;
    }
    pMecha = allocateHeap((s32)offsetof(sLoadedMechas, m11C_previousTranslation), 0);
    /* The bytes stay as loaded, so every user converts (flag 1: the PS1
     * relocated them for an earlier mecha); freed at the end. */
    pData1 = pcConvertMechaData1(pData1);
    pMecha->m62_isSeqLoaded = 0;
    pMecha->m63_isSeq2Loaded = 0;
    if (!(flags & 4)) {
        pData2 = pcBattleMechaData2(pData2, 1);
        pSound = pData2->m8_sound;
        if (pSound->mC_seqEnd != pSound->m8_seq && findSequenceInLoadedList(pSound->m8_seq, 0) == NULL) {
            loadSequence(pSound->m8_seq);
            pMecha->m62_isSeqLoaded = 1;
        }
    }
    else if (!(flags & 0x40)) {
        pData2 = pcBattleMechaData2(pData2, 0); /* an earlier mecha's file (flag 4) */
    }
    pTable10 = pData1->m10_gearInfo;
    p = (u16*)pTable10->m4_params;
    pTextures = pData1->m4_textures;
    pModel = pData1->m8_modelBlocks;
    pBoneTable = pData1->mC_bones;
    battleMechas[entryId] = pMecha;
    pMecha->m24_dimensions[0] = p[1];
    pMecha->m24_dimensions[1] = p[2];
    pMecha->m24_dimensions[2] = p[3];
    pMecha->m2A_animQueue[0] = p[5];
    pMecha->m4A_flags = p[6];
    modelSize = (s32)(pBoneTable - pModel);
    if (pMecha->m4A_flags & 0x200) {
        patchSelfModifyingCode(2, 2, 0x40, 0x40);
    }
    if (!(flags & 1)) {
        i = 0;
        if (!(flags & 0x40)) {
            if (pMecha->m4A_flags & 4) {
                i = 0;
            } else {
                i = 1;
            }
        }
        uploadTextureToVram(pTextures, i, tpageX, tpageY, i, clutX, clutY);
        mechaModelBlocksBufferForLoading_battle = allocateHeap(modelSize, 1);
        memcpy(mechaModelBlocksBufferForLoading_battle, pModel, modelSize);
        for (mechaInitVar4Counter_battle = 0; mechaInitVar4Counter_battle < 20; mechaInitVar4Counter_battle++) {
            if (battleMechaInitVar4[mechaInitVar4Counter_battle].m0_blocks == NULL) {
                break;
            }
        }
        sMechaModel_init_battle(mechaModelBlocksBufferForLoading_battle, &battleMechaInitVar4[mechaInitVar4Counter_battle]);
    }
    pMecha->m0_model = &battleMechaInitVar4[mechaInitVar4Counter_battle];
    if (!(flags & 0x40)) {
        if (pMecha->m4A_flags & 4) {
            pMecha->m4_bones = processMechaMesh_battle(pMecha->m0_model, pBoneTable, 2, 0, 0, 0, 0, 0);
        } else {
            pMecha->m4_bones = processMechaMesh_battle(pMecha->m0_model, pBoneTable, 2, 1, tpageX, tpageY, clutX, clutY);
        }
    } else {
        pMecha->m4_bones = processMechaMesh_battle(pMecha->m0_model, pBoneTable, 0, 0, 0, 0, 0, 0);
    }
    if (pPosition != NULL) {
        pMecha->m4_bones->m5C_translation[0] = pPosition->vx;
        pMecha->m4_bones->m5C_translation[1] = pPosition->vy;
        pMecha->m4_bones->m5C_translation[2] = pPosition->vz;
    }
    if (!(flags & 1)) {
        pMecha->m90_vramCoords[0] = tpageX;
        pMecha->m90_vramCoords[1] = tpageY;
        pMecha->m90_vramCoords[2] = clutX;
        pMecha->m96_clutY = clutY;
    } else {
        pMecha->m90_vramCoords[0] = -1;
    }
    if (!(flags & 4) && !(flags & 0x80)) {
        pMecha->mAC_animData = pData2;
    } else {
        pMecha->mAC_animData = NULL;
    }
    for (i = 0; i < 2; i++) {
        SetPolyFT4(&pMecha->mB8_Polys[i]);
        SetSemiTrans(&pMecha->mB8_Polys[i], 1);
        pMecha->mB8_Polys[i].r0 = battleEnvMechaInitData->m478_r;
        pMecha->mB8_Polys[i].g0 = battleEnvMechaInitData->m479_g;
        pMecha->mB8_Polys[i].b0 = battleEnvMechaInitData->m47A_b;
        pMecha->mB8_Polys[i].clut = GetClut(0x30, 0x1CC);
        pMecha->mB8_Polys[i].tpage = GetTPage(0, 2, 0x380, 0);
        pMecha->mB8_Polys[i].u0 = 0xC0;
        pMecha->mB8_Polys[i].v0 = 0xC0;
        pMecha->mB8_Polys[i].u1 = 0xFE;
        pMecha->mB8_Polys[i].v1 = 0xC0;
        pMecha->mB8_Polys[i].u2 = 0xC0;
        pMecha->mB8_Polys[i].v2 = 0xFE;
        pMecha->mB8_Polys[i].u3 = 0xFE;
        pMecha->mB8_Polys[i].v3 = 0xFE;
    }
    if (!(flags & 0x40)) {
        pMecha->m1C_scale = ((s16)p[4] * battleEnvMechaInitData->m344_mechaScale) >> 12;
    } else {
        pMecha->m1C_scale = p[4];
    }
    pMecha->m10C_num110 = p[7];
    initMechSub110_battle(pMecha);
    pMecha->m10E_num118 = p[8];
    if (pMecha->m10E_num118 != 0) {
        pMecha->m118_colorAnims = allocateHeap(pMecha->m10E_num118 * sizeof(sLoadedMechas_118), 0);
        for (i = 0; i < pMecha->m10E_num118; i++) {
            pMecha->m118_colorAnims[i].m1A_isActive = 0;
            pMecha->m118_colorAnims[i].m4_baseColors = 0;
            pMecha->m118_colorAnims[i].m8_blendColors = 0;
            pMecha->m118_colorAnims[i].mC_outputColors = 0;
        }
    }
    pMecha->m10D_num114 = p[9];
    if (pMecha->m10D_num114 != 0) {
        p += 10;
        pMecha->m114_cloths = p114 = allocateHeap(pMecha->m10D_num114 * sizeof(sLoadedMechas_114), 0);
        for (i = 0; i < pMecha->m10D_num114; i++, p114++) {
            count = (s16)p[17];
            p114->m0_boneIndex = *p++;
            /* The PS1 compiler evaluates the arguments left to right, the PC's right to left. */
            initMechaCloth_battle(p114, pTable10->m8_clothData[i], p[0], p[1], p[2], p[3], p[4], count, tpageX + p[5],
                          tpageY + p[6], p[7], p[8], clutX + p[9], clutY, p[10], p[11], p[12], p[13], p[14], p[15]);
            p += 15 + 2;
            for (j = 0; j < count; j++) {
                p114->m18_colliders[j].m0_pos.pad = *p++;
                p114->m18_colliders[j].mE_radius = *p++;
                p114->m18_colliders[j].m0_pos.vx = *p++;
                p114->m18_colliders[j].m0_pos.vy = *p++;
                p114->m18_colliders[j].m0_pos.vz = *p++;
            }
        }
    }
    pMecha->m20_mechaEntryId = entryId;
    pMecha->m22_isBattleGear = 0;
    if (battleVisualEntities[entryId].m3_hidden != 0 && entryId < 11) {
        pMecha->m34_visible = 0;
    } else {
        pMecha->m34_visible = 1;
    }
    if (!(flags & 0x40)) {
        pAnim = pData2->m4_anim;
        pMecha->mB0_sound = pData2->m8_sound;
        initMechaTransforms1_battle(pMecha, &battleMechaInitVar2, pAnim->m8_bytecode, pAnim->m4_keyframes);
        initMechaAnimation_battle(pMecha, pMecha, &battleMechaInitVar2, 0);
        snapMechaBoneToBattleTerrain(pMecha);
    }
    if (!(flags & 2)) {
        initModel5(mechaModelBlocksBufferForLoading_battle);
        undoFieldModelRelocation(mechaModelBlocksBufferForLoading_battle);
        size = getAllocationSize(mechaModelBlocksBufferForLoading_battle);
        pNewModel = allocateHeap(size, 0);
        memcpy(pNewModel, mechaModelBlocksBufferForLoading_battle, size);
        freeHeap(mechaModelBlocksBufferForLoading_battle);
        freeMechaModelBlocks_battle(pMecha->m0_model, 0);
        sMechaModel_init_battle(pNewModel, pMecha->m0_model);
        pMecha->mA8_model = pNewModel;
    } else {
        pMecha->mA8_model = NULL;
    }
    freeHeap(pData1); /* the converted tables: the caller frees the file */
}

/* Noah: createGearLoadingFileRequests. Queues the gear model/animation files
 * of battle entity `entity` (plus its extra file) in battleGearLoadCommands and starts
 * loading them from directory 0x28. */
void createGearLoadingFileRequests(s32 entity);
void createGearLoadingFileRequests(s32 entity) {
    s32 dir[2];
    s32 gear;
    s32 extra;
    s32 base;
    s32 file;
    LoadingBatchCommand* cmds;
    LoadingBatchCommand* it;

    getCurrentDirectory(&dir[0], &dir[1]);
    setCurrentDirectory(0x28, 1);
    resetMemoryAllocStats(4, 0);
    gear = battleEntities.m0_entities[entity].m0_base.mA0_gearNum;
    extra = battleEntities.m0_entities[entity].mA4_gear.m5C_extraFileIndex;
    /* retail indexes the table from &[0][1] (max extra) and reads the file
     * base one byte before it */
    if ((&battleGearLoadTable1[0][1])[gear * 2] < extra) {
        extra = 0;
    }
    battleGearLoadCommands = cmds = allocateHeap(4 * (s32)sizeof(LoadingBatchCommand), 1);
    it = cmds;
    base = (&battleGearLoadTable1[0][1])[gear * 2 - 1];
    file = base + 1;
    it->m0_fileIndex = file;
    it->m4_loadPtr = allocateHeap(getFileSizeAligned(file), 1);
    it++;
    file = base + 2;
    it->m0_fileIndex = file;
    it->m4_loadPtr = allocateHeap(getFileSizeAligned(file), 0);
    it++;
    if (extra != 0) {
        file += extra;
        it->m0_fileIndex = file;
        it->m4_loadPtr = allocateHeap(getFileSizeAligned(file), 1);
        it++;
    }
    it->m0_fileIndex = 0;
    it->m4_loadPtr = NULL;
    batchStartLoadingFiles(battleGearLoadCommands, 0, 0);
    setCurrentDirectory(dir[0], dir[1]);
}

void createEffectMechaLoadingFileRequests(s32 arg0) {
    s32 dir[2];
    LoadingBatchCommand* cmds;
    s32 file;

    getCurrentDirectory(&dir[0], &dir[1]);
    setCurrentDirectory(0x28, 0);
    resetMemoryAllocStats(4, 0);
    battleGearLoadCommands = cmds = allocateHeap(3 * (s32)sizeof(LoadingBatchCommand), 1);
    arg0 *= 2;
    file = arg0 + 1;
    getFilenameFromPC(file);
    cmds[0].m0_fileIndex = file;
    cmds[0].m4_loadPtr = allocateHeap(getFileSizeAligned(file), 1);
    file = arg0 + 2;
    cmds[1].m0_fileIndex = file;
    cmds[1].m4_loadPtr = allocateHeap(getFileSizeAligned(file), 1);
    cmds[2].m0_fileIndex = 0;
    cmds[2].m4_loadPtr = NULL;
    batchStartLoadingFiles(battleGearLoadCommands, 0, 0);
    setCurrentDirectory(dir[0], dir[1]);
}

/* Noah: finalizeBattleMechaLoadingCommands. Builds mecha `index` from the
 * loaded gear files (battleGearLoadCommands), then the attachments of the optional third
 * file (mechas index * 2 + 13 + i, attached to `index`) or just its texture,
 * and frees the files (battleGearLoadCommands itself before its first file). */
void finalizeBattleMechaLoadingCommands(s32 index, s16 tpageX, s16 tpageY, s16 clutX, s16 clutY) {
    sBattleGearExtraFile* extra;
    u16* rec;
    s16 count;
    s32 i;
    s32 entry;
    s32 size;
    s32 flags;
    void* data;

    mechaInitEnvironmentMechaMesh(index, 0, battleGearLoadCommands[1].m4_loadPtr, battleGearLoadCommands[0].m4_loadPtr, tpageX, tpageY, clutX, clutY, NULL);
    battleMechas[index]->m38_syncFlag = 1;
    battleMechas[index]->m22_isBattleGear = 1;
    extra = battleGearLoadCommands[2].m4_loadPtr;
    if (extra != NULL) {
        rec = EXTRA_ATTACHMENTS(extra);
        if ((count = *(s16*)rec) != 0) {
            rec += 2;
            size = (s32)((u8*)EXTRA_PARTS(extra) - (u8*)EXTRA_DATA2(extra));
            data = allocateHeap(size, 0);
            memcpy(data, EXTRA_DATA2(extra), size);
            for (i = 0; i < count; i++) {
                flags = 7;
                if (i == 0) {
                    flags = 2;
                }
                if (i == count - 1) {
                    flags -= 2;
                }
                entry = index * 2 + 13 + i;
                mechaInitEnvironmentMechaMesh(entry, flags, data, EXTRA_PARTS(extra), tpageX, tpageY, clutX, clutY, NULL);
                battleMechas[entry]->m5E_attachBone = *rec++;
                battleMechas[entry]->m5C_attachMecha = index;
                battleMechas[entry]->m5D_attachRotation = 2;
                battleMechas[entry]->m36_disableGroundSnap = 1;
                battleMechas[entry]->m6A_attachOffset[0] = *rec++;
                battleMechas[entry]->m6A_attachOffset[1] = *rec++;
                battleMechas[entry]->m6A_attachOffset[2] = *rec++;
            }
        } else {
            uploadTextureToVram(EXTRA_DATA2(extra), 1, tpageX, tpageY, 1, clutX, clutY);
        }
        freeHeap(extra);
    }
    freeHeap(battleGearLoadCommands);
    DrawSync(0);
    freeHeap(battleGearLoadCommands->m4_loadPtr);
}

/* Enhanced, the 60 Hz battle (b_800B7C28.c): whether this frame's Gear step
 * moves the camera (an event track drives its eye or its target and the
 * half-step accumulator is due): only then does the scene wait for the step. */
s32 pcBattle60CameraStep(void) {
    s32 v = battleMechaVar0 + 1 + battle800ccc5c;
    if (battle800c3df8 == 0 || (mechaEventAnimTracks[7] == NULL && mechaEventAnimTracks[8] == NULL)) {
        return 0;
    }
    return v > 1;
}

/* Noah: renderMechasBattle. Advances the battle mechas (animation,
 * mecha-vs-mecha pushback for the current mecha mechaAnimSelfIndex, event tracks),
 * then draws them with the target-selection back colour pulse. */
void pcTrailHalfBegin(void); /* b_8009EBA8.c */
void renderMechasBattle(MATRIX* pMatrix, MATRIX* pArgs, OTag* ot, s32 oddOrEven) {
    s32 iterations;
    s32 i;
    s32 j;
    u8 color[3];

    iterations = 0;
    pcGearPoseFrame();
    pcTrailHalfBegin(); /* Enhanced, 60 Hz: the trails' halfway heads of this frame (b_8009EBA8.c) */
    battleMechaVar0 += 1 + battle800ccc5c;
    if (battleMechaVar0 > 6) {
        battleMechaVar0 = 6;
    }
    while (battleMechaVar0 > 1) {
        battleMechaVar0 -= 2;
        iterations++;
    }
    battleMechaVar1 += iterations * 56;
    gMechaAngleBattle = (getAngleSin((s16)battleMechaVar1) + 0x1000) / 800 + 4;
    battleMechaVar4 += 0x80;
    battle800c3b80 = (getAngleSin(battleMechaVar4) + 0x1000) / 32;
    color[0] = addSaturatingColorDelta(battle800c3b80, 0x20, battleEnvMechaInitData->m474_backR);
    color[1] = addSaturatingColorDelta(battle800c3b80, 0x20, battleEnvMechaInitData->m475_backG);
    color[2] = addSaturatingColorDelta(battle800c3b80, 0x20, battleEnvMechaInitData->m476_backB);
    if (battleMechas[mechaAnimSelfIndex] != NULL && !(battleMechas[mechaAnimSelfIndex]->m4A_flags & 0x20)) {
        for (j = 0; j < 11; j++) {
            if (battleMechas[j] != NULL && mechaAnimSelfIndex != j && battleMechas[j]->m5C_attachMecha == 0xFF &&
                !((mechaPlayAnimationVar0_battle >> j) & 1) && battleMechas[j]->m34_visible != 0 &&
                battleMechas[mechaAnimSelfIndex]->m4_bones->m5C_translation[1] >
                    battleMechas[j]->m4_bones->m5C_translation[1] - initMechaForBattleSub0_battle(j) &&
                battleMechas[j]->m4_bones->m5C_translation[1] >
                    battleMechas[mechaAnimSelfIndex]->m4_bones->m5C_translation[1] - initMechaForBattleSub0_battle(mechaAnimSelfIndex)) {
                pushMechaAway(mechaAnimSelfIndex, battleMechas[j]->m4_bones->m5C_translation[0],
                              battleMechas[j]->m4_bones->m5C_translation[2],
                              getMechaColliderRadius_battle(j) + getMechaColliderRadius_battle(mechaAnimSelfIndex));
            }
        }
        finalizeMechaForEvent(mechaAnimSelfIndex);
    }
    for (i = 0; i < 32; i++) {
        if (battleMechas[i] != NULL) {
            updateMechAnim_battle(battleMechas[i], &battleMechaInitVar2, iterations, oddOrEven, battle800ccc5c);
        }
    }
    if (battle800c3df8 != 0) {
        updateMechaEventAnim(&battleMechaInitVar2, iterations, 0, oddOrEven);
    }
    for (i = 0; i < 32; i++) {
        if (battleMechas[i] != NULL && battleMechas[i]->m5C_attachMecha < 0xFF) {
            updateMechasBaseMatrixIsAttached_battle(battleMechas[i]);
            pcGearPoseAttach(battleMechas[i], battleMechas[battleMechas[i]->m5C_attachMecha]);
        }
    }
    refreshAttachedMechaTransforms_battle(battleMechas);
    pcBattle60SceneAfterStep(); /* Enhanced, 60 Hz: the scene from the camera halfway through the step (b_800B7C28.c) */
    SetColorMatrix(battle800d2fc0);
    if (battleMechaVar5 != 0) {
        for (i = 0; i < 31; i++) {
            if (battleMechas[i] != NULL) {
                if ((updateTargetSelectionMarkerSub0_var0 >> i) & 1) {
                    SetBackColor(color[0], color[1], color[2]);
                } else {
                    SetBackColor(battleEnvMechaInitData->m474_backR, battleEnvMechaInitData->m475_backG, battleEnvMechaInitData->m476_backB);
                }
                if (battleMechas[i]->m4A_flags & 0x40) {
                    objectClippingMask = 0;
                } else {
                    objectClippingMask = 1;
                }
                submitMechaForRendering_battle(battleMechas[i], pMatrix, pArgs, 1, battle800ccc5c, ot, oddOrEven);
                objectClippingMask = 0;
            }
        }
    }
    drawMechaWheelTrail_battle(&battleMechaInitVar3, pMatrix, iterations, ot, oddOrEven);
}

/* Noah: freeMechaModule_battle (battle) */
void freeMechaModule_battle(void) {
    s32 i;

    for (i = 0; i < 31; i++) {
        freeMecha_battle(i);
    }
    freeMechaInitVar2_battle(&battleMechaInitVar2);
    freeMechaInitVar3_battle(&battleMechaInitVar3);
}

/* Noah: freeMecha_battle (battle); entries 13-18 are the party gears' extra
 * parts, freed with their owner (index * 2 + 13 / + 14). */
void freeMecha_battle(s32 index) {
    s32 i;

    if (battleMechas[index] != NULL) {
        if (battleMechas[index]->mA8_model != NULL) {
            pcFreeModel(battleMechas[index]->mA8_model);
            freeHeap(battleMechas[index]->mA8_model);
            freeMechaModelBlocks_battle(battleMechas[index]->m0_model, 1);
        }
        if (battleMechas[index]->m62_isSeqLoaded) {
            unloadSequence(battleMechas[index]->mB0_sound->m8_seq);
        }
        if (battleMechas[index]->mAC_animData != NULL) {
            pcBattleFreeMechaData2(battleMechas[index]->mAC_animData);
        }
        if (index < 19 || index >= 31) {
            if (battleMechas[index]->m4_bones != NULL) {
                mechaOP_8_battle(&battleMechaInitVar2, battleMechas[index]->m4_bones);
                freeAnimTracks2_battle(&battleMechaInitVar2, battleMechas[index]->m4_bones, 0xFF);
                freeMechaBones_battle(battleMechas[index]->m4_bones);
                battleMechas[index]->m0_model = NULL;
                battleMechas[index]->m4_bones = NULL;
            }
            freeMechaExtraData(battleMechas[index]);
        } else if (battleMechas[index]->m4_bones != NULL) {
            mechaOP_8_battle(&battleMechaInitVar2, battleMechas[index]->m4_bones);
            freeAnimTracks2_battle(&battleMechaInitVar2, battleMechas[index]->m4_bones, 0xFF);
            freeHeap(battleMechas[index]->m4_bones);
        }
        if (battleMechas[index]->m10C_num110) {
            freeHeap(battleMechas[index]->m110_trails);
        }
        if (battleMechas[index]->m10E_num118) {
            for (i = 0; i < battleMechas[index]->m10E_num118; i++) {
                freeAnimatedEnvTextureTile_battle(&battleMechas[index]->m118_colorAnims[i]);
            }
            freeHeap(battleMechas[index]->m118_colorAnims);
        }
        if (battleMechas[index]->m10D_num114) {
            for (i = 0; i < battleMechas[index]->m10D_num114; i++) {
                freeMechaCloth_battle(&battleMechas[index]->m114_cloths[i]);
            }
            freeHeap(battleMechas[index]->m114_cloths);
        }
        freeHeap(battleMechas[index]);
        battleMechas[index] = NULL;
    }
    if (index < 3) {
        freeMecha_battle(index * 2 + 13);
        freeMecha_battle(index * 2 + 14);
    }
}

/* Noah: mechaPlayAnimation (battle) */
void mechaPlayAnimation_battle(u16 index, u16 arg1, s32 animationId) {
    mechaAnimSelfIndex = index;
    mechaPlayAnimationVar0_battle = arg1;
    if (battleMechas[index] != NULL) /* retail stores through NULL (low RAM 0x35) when the entity has no mecha */
    battleMechas[index]->m35_isEventAnimation = 0;
    if (battleMechas[index] != NULL) {
        initMechaAnimation_battle(battleMechas[index], battleMechas[index], &battleMechaInitVar2, animationId);
    }
}

void setupMechaForEvent(u16 index, u16 arg1, s32 animationId) {
    mechaAnimSelfIndex = index;
    mechaPlayAnimationVar0_battle = arg1;
    battleMechaVar3 = 1;
    if (battleMechas[index] != NULL) /* retail stores through NULL (low RAM 0x35) when the entity has no mecha */
    battleMechas[index]->m35_isEventAnimation = 1;
    allocateSavePointMeshDataSub0_var0 = 0;
    spriteBytecode2ExtendedE0_Var0 = 1;
    setupMechaForEventSub1(index, setupMechaForEventSub0());
    allocateJumpAnimationStructVar0 = 1;
    battleCurrentDamages[0].m18_damageType[setupMechaForEventSub0()] = 0;
    if (battleMechas[index] != NULL) {
        initMechaAnimation_battle(battleMechas[index], battleMechas[index], &battleMechaInitVar2, animationId);
    }
}

void mechaPlayAnimationPreserveState_battle(u16 index, u16 arg1, s32 animationId) {
    u16 savedIndex = mechaAnimSelfIndex;
    u16 savedMask = mechaPlayAnimationVar0_battle;

    mechaAnimSelfIndex = index;
    mechaPlayAnimationVar0_battle = arg1;
    if (battleMechas[index] != NULL) /* retail stores through NULL (low RAM 0x35) when the entity has no mecha */
    battleMechas[index]->m35_isEventAnimation = 0;
    if (battleMechas[index] != NULL) {
        if (index != setupMechaForEventSub0()) {
            initMechaAnimation_battle(battleMechas[index], battleMechas[index], &battleMechaInitVar2, animationId);
        }
    }
    mechaAnimSelfIndex = savedIndex;
    mechaPlayAnimationVar0_battle = savedMask;
}

u8 addSaturatingColorDelta(s16 delta, s16 scale, s32 base) {
    s16 result = base + (scale * delta) / 256;

    if (result > 0xFF) {
        result = 0xFF;
    }
    return result;
}

void mechaPlayAnimationFromSource_battle(sLoadedMechas* pOther, u16 index, u16 arg2, s32 animationId) {
    mechaAnimSelfIndex = index;
    mechaPlayAnimationVar0_battle = arg2;
    if (battleMechas[index] != NULL) /* retail stores through NULL (low RAM 0x35) when the entity has no mecha */
    battleMechas[index]->m35_isEventAnimation = 0;
    if (battleMechas[index] != NULL) {
        if (index != setupMechaForEventSub0()) {
            initMechaAnimation_battle(battleMechas[index], pOther, &battleMechaInitVar2, animationId);
        }
    }
}

/* Noah: initMechaForBattleSub0_battle (mecha height) */
s32 initMechaForBattleSub0_battle(s32 index) {
    sLoadedMechas* pMecha = battleMechas[index];
    s32 result = 0;

    if (pMecha != NULL) {
        result = (pMecha->m24_dimensions[0] * ((pMecha->m1C_scale * pMecha->m4_bones->m4C_scale[1]) >> 12)) >> 12;
    }
    return result;
}

s32 getMechaColliderRadius_battle(s32 index) {
    s32 a;
    s32 b;
    s32 result = 0;

    if (battleMechas[index] != NULL) {
        if (battleMechas[index]->m4A_flags & 8) {
            a = battleMechas[index]->m24_dimensions[1];
            b = battleMechas[index]->m4_bones->m4C_scale[0];
        } else {
            a = battleMechas[index]->m24_dimensions[2];
            b = battleMechas[index]->m4_bones->m4C_scale[2];
        }
        result = (a * ((battleMechas[index]->m1C_scale * b) >> 12)) >> 12;
    }
    return result;
}

void initMechSub110_battle(sLoadedMechas* pMecha) {
    sLoadedMechas_110* p;
    s32 i;

    if (pMecha->m10C_num110 != 0) {
        p = allocateHeap(pMecha->m10C_num110 * sizeof(sLoadedMechas_110), 0);
        for (i = 0; i < pMecha->m10C_num110; i++) {
            p[i].m0_boneIndex = -1;
            p[i].m8_segment = 0;
        }
        pMecha->m110_trails = p;
    }
}

void setBattleMechaIdleAnimation(s32 index, u8 arg1) {
    if (battleMechas[index] != NULL) {
        battleMechas[index]->m2A_animQueue[0] = arg1;
    }
}

void setBattleMechasVisible(s32 arg0) {
    battleMechaVar5 = arg0 & 1;
}

/* Swaps two battle mecha slots (the first is hidden, the second shown). */
void swapBattleMechaSlots(s32 a, s32 b) {
    sLoadedMechas* tmp;

    /* retail stores through NULL (low RAM 0x34) for a slot without a mecha */
    if (battleMechas[a] != NULL)
    battleMechas[a]->m34_visible = 0;
    if (battleMechas[b] != NULL)
    battleMechas[b]->m34_visible = 1;
    tmp = battleMechas[a];
    battleMechas[a] = battleMechas[b];
    battleMechas[b] = tmp;
}

/* First battleCurrentDamages[i..] animation that is not 0xF7 (0xFF -> 0xFE). */
u8 peekNextBattleAnimation(s32 i) {
    u8 anim;

    do {
        anim = battleCurrentDamages[i++].m47_battleAnimationToPlay;
    } while (anim == 0xF7);
    if (anim == 0xFF) {
        anim = 0xFE;
    }
    return anim;
}

void* getEnvTileStepFunction_battle(s32 arg0) {
    switch (arg0) {
    case 1:
        return envTileStepRampUp_battle;
    case 2:
        return envTileStepRampDown_battle;
    case 3:
        return envTileStepRampDownClamped_battle;
    default:
        return envTileStepSine_battle;
    }
}

void initMechaTransforms1_battle(sLoadedMechas* arg0, sMechaInitVar2* arg1, sMechaDataTable2_4_8** arg2,
                   sMechaDataTable2_4_4** arg3) {
    arg0->m8_bytecodeTable = arg2;
    arg0->mC_extraAnim = NULL;
    arg0->m10_bytecode0 = NULL;
    arg0->m14_keyframes = arg3;
    arg0->m18_extraKeyframes = NULL;
    arg0->m2A_animQueue[1] = 0;
    arg0->m98_eventFrame = -1;
    arg0->m3C_trackTag = -1;
    arg0->m5C_attachMecha = -1;
    arg0->m39_shadowShade = 0x6B;
    arg0->m58_targetMecha = 0;
    arg0->m35_isEventAnimation = 0;
    arg0->m37_useBoneScale = 0;
    arg0->m38_syncFlag = 0;
    arg0->m3A_savedAnimMask = -1;
    arg0->m70_angularVelocity[0] = 0;
    arg0->m70_angularVelocity[1] = 0;
    arg0->m70_angularVelocity[2] = 0;
    arg0->m76_angularAcceleration[0] = 0;
    arg0->m76_angularAcceleration[1] = 0;
    arg0->m76_angularAcceleration[2] = 0;
    arg0->m7C_velocity[0] = 0;
    arg0->m7C_velocity[1] = 0;
    arg0->m7C_velocity[2] = 0;
    arg0->m82_acceleration[0] = 0;
    arg0->m82_acceleration[1] = 0;
    arg0->m82_acceleration[2] = 0;
    arg0->m88_targetPosition[0] = 0;
    arg0->m88_targetPosition[1] = 0;
    arg0->m88_targetPosition[2] = 0;
    arg0->m8E_stepDistance = 1;
    arg0->m36_disableGroundSnap = 0;
    arg0->m1E_terrainTriangle = -1;
}

void initMechaAnimation_battle(sLoadedMechas* arg0, sLoadedMechas* arg1, sMechaInitVar2* arg2, s32 animationId) {
    if (arg0 != NULL && arg1 != NULL) {
        if (arg0->m2A_animQueue[1] != 0) {
            if (arg0->m2A_animQueue[1] < 5) {
                arg0->m2A_animQueue[1]++;
            }
            arg0->m2A_animQueue[arg0->m2A_animQueue[1]] = arg1->m20_mechaEntryId;
            arg0->m2A_animQueue[arg0->m2A_animQueue[1] + 4] = animationId;
        } else {
            if (animationId < 0x50) {
                arg0->m10_bytecode0 = arg1->m8_bytecodeTable[animationId];
            } else {
                /* An id past the extra file's programs (the debug selector's
                 * party in another party's event): the PS1 takes the file word
                 * after the table as an address (0x281C seen, BIOS RAM) and runs
                 * the bytes there; the PC runs an empty program instead. */
                static sMechaDataTable2_4_8 noProgram[1] = {0};
                if (animationId - 0x50 + 2 > (s32)(uptr)arg1->mC_extraAnim->m4_anim[0]) {
                    arg0->m10_bytecode0 = noProgram;
                } else
                arg0->m10_bytecode0 = arg1->mC_extraAnim->m4_anim[animationId - 0x50 + 2];
            }
            arg0->m42_loopCount = 0;
            arg0->m40_waitTimer = 0;
            arg0->m50_bytecode2 = NULL;
            arg0->m54_bytecode1 = NULL;
            arg0->m4C_bytecode3 = NULL;
            arg0->m10A_targetMask = mechaPlayAnimationVar0_battle;
            arg0->m23_extraLoadPending = 0;
            processMechaAnimData_battle(arg0, arg2, -1, 1, 0);
        }
    }
}

s32 updateMechAnim_battle(sLoadedMechas* arg0, sMechaInitVar2* arg1, s32 iterations, s32 oddOrEven, s32 arg4) {
    s32 updateFlag;
    s32 i;

    if (arg0->m0_model != NULL) {
        updateFlag = 0;
        if (arg0->m34_visible != 0) {
            snapMechaBoneToBattleTerrain(arg0);
            if (arg0->m37_useBoneScale != 0) {
                updateMechAnimSub2_battle(arg0->m4_bones, arg0->m1C_scale);
            } else {
                updateMechAnimSub1_battle(arg0->m4_bones, arg0->m1C_scale);
            }
            pcGearPoseBefore(arg0, iterations); /* Smooth motion: the pose drawn (port/src/game/battle/gearpose.c) */
            for (i = 0; i < iterations; i++) {
                updateFlag |= updateMechAnimSub3_battle(arg1, arg0->m4_bones, arg0->m3C_trackTag, arg0->m1C_scale);
                updateMechAnimSub4_battle(arg0, arg1, oddOrEven);
            }
        }
        processMechaAnimData_battle(arg0, arg1, updateFlag, iterations, arg4);
        pcGearPoseAfter(arg0);
    }
    return updateFlag;
}

void updateMechasBaseMatrixIsAttached_battle(sLoadedMechas* arg0) {
    MATRIX* m;
    SVECTOR v;

    m = (MATRIX*)0x1F800000;
    if (battleMechas[arg0->m5C_attachMecha] != NULL) {
        if (!(arg0->m4A_flags & 0x10)) {
            arg0->m34_visible = battleMechas[arg0->m5C_attachMecha]->m34_visible;
        }
        if (arg0->m34_visible != 0) {
            if (arg0->m5D_attachRotation != 0) {
                if (arg0->m5E_attachBone != 0) {
                    MulMatrix0(&battleMechas[arg0->m5C_attachMecha]->m4_bones[0].m2C_boneFinalMatrix,
                               &battleMechas[arg0->m5C_attachMecha]->m4_bones[arg0->m5E_attachBone].m2C_boneFinalMatrix, (MATRIX*)0x1F800000);
                } else {
                    m = &battleMechas[arg0->m5C_attachMecha]->m4_bones[0].m2C_boneFinalMatrix;
                }
                MulMatrix2(m, &arg0->m4_bones->mC_localMatrix);
                MulMatrix2(m, &arg0->m4_bones->m2C_boneFinalMatrix);
            }
            if (arg0->m5E_attachBone != 0) {
                CompMatrix(&battleMechas[arg0->m5C_attachMecha]->m4_bones[0].mC_localMatrix,
                           &battleMechas[arg0->m5C_attachMecha]->m4_bones[arg0->m5E_attachBone].m2C_boneFinalMatrix, m);
            } else {
                m = &battleMechas[arg0->m5C_attachMecha]->m4_bones[0].mC_localMatrix;
            }
            SetRotMatrix(m);
            SetTransMatrix(m);
            v.vx = arg0->m6A_attachOffset[0];
            v.vy = arg0->m6A_attachOffset[1];
            v.vz = arg0->m6A_attachOffset[2];
            gte_ldv0(&v);
            gte_rt();
            gte_stlvnl(arg0->m4_bones->mC_localMatrix.t);
            gte_stlvnl(arg0->m4_bones->m5C_translation);
        }
    } else {
        arg0->m5C_attachMecha = -1;
    }
}

extern u8 battleMechaExtraAnimFiles[];  /* extra animation file per gear */
extern u8 currentBattleLoaderTransitionEffect;
extern s16 gearAbilityAnimationId;
extern u8 fxFragmentInitialized;
sLoadedMechas_118* initAnimatedEnvTextureTile_battle(sLoadedMechas_118* p, sLoadedMechas_118* parent, u16 mode, u16 flags,
    sLoadedMechas_118_1C* src, s16 a5, s16 a6, s16 a7, s16 a8, s16 a9, s16 a10, s16 x, s16 y, s16 w, s16 h,
    s16 speed, s16 arg16, s16 arg17, s16 (*fn)());
void shrinkAllocation(void* p, s32 size);
void initFieldEntitySub4Sub5Sub0(SpriteVramData* vram, void* bundle, Vec2s16 vramLocation, Vec2s16 clut, s32 unused);
void releaseMechaEventAnimTracks(sMechaInitVar2* pool);
s32 isMechaEventAllowedForHitResult(s32 target, u8 mask);
/* PS1: retail calls it without a prototype in scope (arguments not narrowed),
 * and gcc rejects an empty-list declaration for these parameter types */
void startMechaEventAnimTrack(sMechaInitVar2* pool, s32 index, u8 type, u8 tag, s16 x, s16 y, s16 z, s16 dx, s16 dy,
                   s16 dz, s16 duration);
s32 getMechaSoundEffectIdBase_battle(sLoadedMechas* pMecha, s32 which);
void setupBoneRotationTrack_battle(sMechaInitVar2* pool, sMechaBone* pBone, s32 duration, s32 x, s32 y, s32 z);
void setupBoneLookAtTrack_battle(sMechaInitVar2* pool, sMechaBone* pBone, s32 type, s32 arg3, s32 arg4, s32 duration, s32 x, s32 y,
                   s32 z);
void mechaOP_11_b_battle(sLoadedMechas* pMecha, sMechaDataTable2_4_4* pKey, s32 loop);
void mechaOP_19_battle(sLoadedMechas* pMecha);
s32 computeMechaDistanceToTarget_battle(sLoadedMechas* pMecha);
void updateMechaTargetPosition_battle(sLoadedMechas* pMecha);
void reattachMechaBones_battle(sMechaBone* bones, sMechaBone* bones2);
s16 projectOnPlaneNormalScaled_battle(VECTOR* pDir, VECTOR* a, VECTOR* b, s32 scale);
u8 getMechaBitfieldForAnim_battle(sLoadedMechas* pMecha, u8 id, u16* pMask);
sMechaDataTable2_4_4* mechaGetKeyframe_battle(sLoadedMechas* pMecha, u8 id, s32* pFlag);
void fadeSoundEffectVolume(s32 id, s32 vol, s32 dur);
void mecha_battle_op3_2(u8 arg0);
u8 getBattleSlotLayout(u8 i);
void requestFxFragmentSetup(s16 arg0);
void queueMechaBattleAnimationStep(void);
void setBattleCameraMode4IfAllowed(void);
void setBattleCameraMode1IfAllowed(void);
void signalEffectMechaEvent(void);

/* Each argument extraction is a complete GNU C statement expression, so its
 * cursor update completes before another extraction executes. GCC documents
 * statement-expression evaluations as non-interleaved. The matched PSX
 * compiler evaluates these arguments in retail stream order, left to right.
 * s32 plus the explicit s16 cast preserves signed word loads. This is a
 * bytecode-read operation, with no assembly or register constraints.
 * Reference: https://gcc.gnu.org/onlinedocs/gcc/Statement-Exprs.html
 */
#define ANIM_WORD_ARG(cursor) ({ s32 word = (s16)*(cursor)++; word; })
/* PC: the PS1 compiler evaluates call arguments left to right, the PC's
 * right to left, so calls with several ANIM_WORD_ARGs read the words by
 * position (ANIM_WORD_AT) and advance the cursor after the call. */
#define ANIM_WORD_AT(cursor, i) ((s32)(s16)(cursor)[i])

/* Noah: processMechaAnimData_battle (battle copy of field_mecha func_801E39F0): the
 * mecha animation bytecode interpreter. Integrates the mecha's velocities
 * (arg4 + 1 steps), picks the program (m4C/m54/m50 triggers), then runs
 * opcodes until one stops (pNext = pCur; cont = 0). */
void processMechaAnimData_battle(sLoadedMechas* pMecha, sMechaInitVar2* tracks, s32 param_3, s32 param_4, s32 arg4) {
    VECTOR lv;
    SVECTOR sv;
    VECTOR normal;
    SVECTOR pos;
    sLoadedMechas* pOrig;
    sMechaDataTable2_4_8* pNext;
    sMechaDataTable2_4_8* pCur;
    s32 cont;
    s32 flag;
    s32 tmp;
    s16 op;
    u8 lo;
    u8 hi;
    u8 opLo;
    u8 opHi;
    u8 op3;
    u8 op4;
    sMechaDataTable2_4_4* keyframe;

    if (param_4 == 0) {
        return;
    }
    if (pMecha->m10_bytecode0 == NULL) {
        return;
    }
    resetMemoryAllocStats(4, 0);
    flag = 0;
    pOrig = pMecha;
    for (tmp = 0; tmp < arg4 + 1; tmp++) {
        pMecha->m70_angularVelocity[0] += pMecha->m76_angularAcceleration[0];
        pMecha->m70_angularVelocity[1] += pMecha->m76_angularAcceleration[1];
        pMecha->m70_angularVelocity[2] += pMecha->m76_angularAcceleration[2];
        pMecha->m7C_velocity[0] += pMecha->m82_acceleration[0];
        pMecha->m7C_velocity[1] += pMecha->m82_acceleration[1];
        pMecha->m7C_velocity[2] += pMecha->m82_acceleration[2];
        pMecha->m4_bones->m54_rotationAngles.vx += pMecha->m70_angularVelocity[0] >> 3;
        pMecha->m4_bones->m54_rotationAngles.vy += pMecha->m70_angularVelocity[1] >> 3;
        pMecha->m4_bones->m54_rotationAngles.vz += pMecha->m70_angularVelocity[2] >> 3;
        sv.vx = pMecha->m7C_velocity[0] * pMecha->m4_bones->m4C_scale[0] >> 12;
        sv.vy = pMecha->m7C_velocity[1] * pMecha->m4_bones->m4C_scale[1] >> 12;
        sv.vz = pMecha->m7C_velocity[2] * pMecha->m4_bones->m4C_scale[2] >> 12;
        ApplyMatrix(&pMecha->m4_bones->m2C_boneFinalMatrix, &sv, &lv);
        pMecha->m4_bones->m5C_translation[0] += pMecha->m1C_scale * lv.vx >> 12;
        pMecha->m4_bones->m5C_translation[1] += pMecha->m1C_scale * lv.vy >> 12;
        pMecha->m4_bones->m5C_translation[2] += pMecha->m1C_scale * lv.vz >> 12;
    }

    cont = 1;
    pNext = pMecha->m10_bytecode0;
    if (pMecha->m4C_bytecode3 != NULL && computeMechaDistanceToTarget_battle(pMecha) <= pMecha->m48_triggerDistance) {
        pNext = pMecha->m4C_bytecode3;
        pMecha->m4C_bytecode3 = NULL;
    } else {
        if (pMecha->m54_bytecode1 != NULL) {
            pos.vx = pMecha->m4_bones->m5C_translation[0];
            pos.vy = 0;
            pos.vz = pMecha->m4_bones->m5C_translation[2];
            pMecha->m1E_terrainTriangle = findBattleTerrainTriangle(&pos, pMecha->m1E_terrainTriangle, 4);
            if (pMecha->m1E_terrainTriangle < 0) {
                pMecha->m1E_terrainTriangle = findBattleTerrainTriangleBruteForce(&pos);
            }
            computeTerrainHeightAtTriangle(&pos, pMecha->m1E_terrainTriangle, &normal);
            if (pos.vy < pMecha->m4_bones->m5C_translation[1]) {
                pMecha->m4_bones->m5C_translation[1] = pos.vy;
                pNext = pMecha->m54_bytecode1;
                pMecha->m54_bytecode1 = NULL;
                goto skip;
            }
        }
        if (pMecha->m50_bytecode2 != NULL) {
            pMecha->m44_delayTimer += param_4;
            if (pMecha->m44_delayTimer >= pMecha->m46_delay) {
                pNext = pMecha->m50_bytecode2;
                pMecha->m50_bytecode2 = NULL;
            }
        }
    }
skip:
    if (pMecha->m58_targetMecha != 0) {
        updateMechaTargetPosition_battle(pMecha);
    }

    while (cont) {
        pCur = pNext;
        op = *pNext++;
        lo = op;
        hi = op >> 8;
        switch (lo) {
        case 0x00:
            pNext = pCur;
            cont = 0;
            break;
        case 0x01:
            if (param_3 != -1) {
                op = *pNext++;
                pMecha->m40_waitTimer += param_4;
                if (pMecha->m40_waitTimer < op) {
                    pNext = pCur;
                    cont = 0;
                    break;
                }
                pMecha->m40_waitTimer = 0;
                param_4 = 0;
                break;
            }
            pNext = pCur;
            cont = 0;
            break;
        case 0x02:
            if (pMecha->m35_isEventAnimation != 0) {
                if (mecha_battle_op3() != 0) {
                    pNext = pCur;
                    cont = 0;
                    break;
                }
                allocateSavePointMeshDataSub0_var0 = 0;
                spriteBytecode2ExtendedE0_Var0 = 0;
                pMecha->m35_isEventAnimation = 0;
                mecha_battle_op3_2(pMecha->m20_mechaEntryId);
            } else {
                flag = 1;
            }
            break;
        case 0x03:
            if (pMecha->m35_isEventAnimation != 0) {
                if (mecha_battle_op3() != 0) {
                    pNext = pCur;
                    cont = 0;
                    break;
                }
                allocateSavePointMeshDataSub0_var0 = 0;
                spriteBytecode2ExtendedE0_Var0 = 0;
                pMecha->m35_isEventAnimation = 0;
                mecha_battle_op3_2(pMecha->m20_mechaEntryId);
            }
            break;
        case 0x04:
            op = *pNext++;
            if (pMecha->mC_extraAnim == NULL) {
                s32 dir[2];
                s32 file;

                getCurrentDirectory(&dir[0], &dir[1]);
                setCurrentDirectory(0x28, 2);
                file = hi + battleMechaExtraAnimFiles[op];
                readFile(file, pMecha->mC_extraAnim = allocateHeap(getFileSizeAligned(file), 0), 0, 0);
                setCurrentDirectory(dir[0], dir[1]);
            }
            break;
        case 0x05:
            if (pMecha->m23_extraLoadPending == 0) {
                if (pMecha->mC_extraAnim != NULL && pMecha->m18_extraKeyframes == NULL) {
                    Vec2s16 vramLocation;
                    Vec2s16 clut;
                    sBattleMechaSoundData* pSound;
                    sMechaDataTable2_4* pAnim;

                    if (isCDBusy() == 0) {
                        /* The file stays as loaded; mC holds its native tables from here on
                         * (they own the file: freeMechaExtraData). */
                        void* raw = pMecha->mC_extraAnim;
                        pMecha->mC_extraAnim = (sMechaExtraAnim*)pcConvertMechaData2(raw);
                        pSound = (sBattleMechaSoundData*)((sMechaDataTable2*)pMecha->mC_extraAnim)->m8_sound;
                        pMecha->mB4_extraSound = (sMechaDataTable2_8*)pSound;
                        if (pSound->m18_texturesEnd != pSound->m14_textures) {
                            uploadTextureToVram(pSound->m14_textures, 1, 0x380, 0x100, 1, 0, 0x1D0);
                        }
                        if (pMecha->mB4_extraSound->mC_seqEnd != pMecha->mB4_extraSound->m8_seq && findSequenceInLoadedList(pMecha->mB4_extraSound->m8_seq, 0) == NULL) {
                            loadSequence(pMecha->mB4_extraSound->m8_seq);
                            pMecha->m63_isSeq2Loaded = 1;
                        } else {
                            pMecha->m63_isSeq2Loaded = 0;
                        }
                        pAnim = ((sMechaDataTable2*)pMecha->mC_extraAnim)->m4_anim;
                        pAnim = (sMechaDataTable2_4*)pAnim->m4_keyframes;
                        pMecha->m18_extraKeyframes = (sMechaDataTable2_4_4_array*)pAnim;
                        DrawSync(0);
                        shrinkAllocation(raw, (s32)(pMecha->mB4_extraSound->mC_seqEnd - (u8*)raw));
                        if (((sBattleMechaSoundData*)pMecha->mB4_extraSound)->m8_seq !=
                            ((sBattleMechaSoundData*)pMecha->mB4_extraSound)->m4_spriteBundle) {
                            vramLocation.vx = 0x380;
                            vramLocation.vy = 0x100;
                            clut.vx = 0;
                            clut.vy = 0x1D0;
                            initFieldEntitySub4Sub5Sub0(&createSavePointMeshData_mode6, ((sBattleMechaSoundData*)pMecha->mB4_extraSound)->m4_spriteBundle,
                                          vramLocation, clut, 0);
                        }
                        pMecha->m23_extraLoadPending = 1;
                    }
                    pNext = pCur;
                    cont = 0;
                }
            } else {
                if (isCDBusy() != 0) {
                    pNext = pCur;
                    cont = 0;
                    break;
                }
                pMecha->m23_extraLoadPending = 0;
            }
            break;
        case 0x06:
            freeMechaExtraData(pMecha);
            break;
        case 0x07:
            stopCdRead(0);
            break;
        case 0x08:
            mechaOP_8_battle(tracks, pMecha->m4_bones);
            mechaOP_19_battle(pMecha);
            break;
        case 0x09:
            freeAnimTracks2_battle(tracks, pMecha->m4_bones, hi);
            break;
        case 0x0A:
            mechaOP_A_battle(tracks, pMecha->m4_bones, hi, 7);
            break;
        case 0x0B: {
            sMechaBone* bones = pMecha->m4_bones;
            s32 count;
            s32 i;

            mechaOP_8_battle(tracks, bones);
            count = bones->mA_numBones - 1;
            for (i = 0; i < count; i++) {
                bones++;
                bones->m54_rotationAngles.vx = 0;
                bones->m54_rotationAngles.vy = 0;
                bones->m54_rotationAngles.vz = 0;
                bones->m5C_translation[0] = 0;
                bones->m5C_translation[1] = 0;
                bones->m5C_translation[2] = 0;
                bones->m4_needTranslationUpdate = 1;
                bones->m5_needRotationUpdate = 1;
            }
            break;
        }
        case 0x0C:
            pMecha->m70_angularVelocity[0] = 0;
            pMecha->m70_angularVelocity[1] = 0;
            pMecha->m70_angularVelocity[2] = 0;
            pMecha->m76_angularAcceleration[0] = 0;
            pMecha->m76_angularAcceleration[1] = 0;
            pMecha->m76_angularAcceleration[2] = 0;
            pMecha->m7C_velocity[0] = 0;
            pMecha->m7C_velocity[1] = 0;
            pMecha->m7C_velocity[2] = 0;
            pMecha->m82_acceleration[0] = 0;
            pMecha->m82_acceleration[1] = 0;
            pMecha->m82_acceleration[2] = 0;
            break;
        case 0x0D:
            mechaOP_A_battle(tracks, pMecha->m4_bones, hi, 1);
            break;
        case 0x0E:
            mechaOP_A_battle(tracks, pMecha->m4_bones, hi, 2);
            break;
        case 0x0F:
            setBattleCameraMode1IfAllowed();
            releaseMechaEventAnimTracks(tracks);
            battle800c3df8 = 0;
            break;
        case 0x10:
            mechaSetPoseFromKeyframe_battle(pMecha->m4_bones, (s16*)mechaGetKeyframe_battle(pMecha, hi, &tmp));
            break;
        case 0x11: {
            u16 w;

            keyframe = mechaGetKeyframe_battle(pMecha, hi, &tmp);
            w = *pNext++;
            op = w;
            if (tmp == 0) {
                w >>= 8;
                mechaOP_11_battle(tracks, pMecha->m4_bones, keyframe, w, (u8)op);
                param_3 = -1;
                pMecha->m8E_stepDistance = abs(keyframe->m10_stepDistance * (pMecha->m1C_scale * pMecha->m4_bones->m4C_scale[2] >> 12) >> 12);
                mechaOP_11_b_battle(pMecha, keyframe, w);
            }
            break;
        }
        case 0x12: {
            u16 w;

            keyframe = mechaGetKeyframe_battle(pMecha, hi, &tmp);
            w = *pNext++;
            op = w;
            if (tmp == 0) {
                mechaSetupKeyframeTracksWithPose_battle(tracks, pMecha->m4_bones, keyframe, w >> 8, (u8)op);
                param_3 = -1;
                pMecha->m8E_stepDistance = abs(keyframe->m10_stepDistance * (pMecha->m1C_scale * pMecha->m4_bones->m4C_scale[2] >> 12) >> 12);
            }
            break;
        }
        case 0x13: {
            sMechaDataTable2_4_4* keyframe;

            opHi = (u16)(op = *pNext++) >> 8;
            opLo = op;
            op4 = (u16)(op = *pNext++) >> 8;
            op3 = op;
            keyframe = mechaGetKeyframe_battle(pMecha, opLo, &tmp);
            param_3 = -1;
            mechaOP_13_battle(tracks, pMecha->m4_bones, keyframe, op4, hi, op3, opHi);
            mechaOP_19_battle(pMecha);
            break;
        }
        case 0x14: {
            sMechaDataTable2_4_8 w;
            u8 target;

            w = *pNext++;
            op = w;
            target = op & 0xFF;
            opHi = w >> 8;
            op4 = getMechaBitfieldForAnim_battle(pMecha, target, (u16*)&op);
            getMechaBitfieldForAnim_battle(pMecha, hi, (u16*)&op);
            op3 = pMecha->m2A_animQueue[1];
            if (hi == 0xFD) {
                pMecha->m2A_animQueue[1] = 0;
            }
            for (tmp = 0; tmp < 13; tmp++) {
                if ((op >> tmp) & 1) {
                    if (target == 0xFF) {
                        initMechaAnimation_battle(battleMechas[tmp], battleMechas[tmp], tracks, opHi);
                    } else {
                        initMechaAnimation_battle(battleMechas[tmp], battleMechas[op4], tracks, opHi);
                    }
                }
            }
            pMecha->m2A_animQueue[1] = op3;
            if (hi == 0xFD) {
                return;
            }
            break;
        }
        case 0x15: {
            sLoadedMechas* pNew;
            sMechaBone* newBones;

            op = *pNext++;
            for (tmp = 19; tmp < 31; tmp++) {
                if (battleMechas[tmp] == NULL) {
                    pNew = allocateHeap((s32)offsetof(sLoadedMechas, m11C_previousTranslation), 1);
                    break;
                }
            }
            *(sBattleLoadedMechasCopy*)pNew = *(sBattleLoadedMechasCopy*)pMecha;
            battleMechas[tmp] = pNew;
            modelsBattleRigCreated(tmp);
            pNew->m98_eventFrame = -1;
            pNew->m3C_trackTag = -1;
            pNew->m40_waitTimer = 0;
            pNew->m58_targetMecha = 0;
            pNew->m5C_attachMecha = -1;
            pNew->m90_vramCoords[0] = -1;
            pNew->m62_isSeqLoaded = 0;
            pNew->mA8_model = NULL;
            pNew->mAC_animData = NULL;
            pNew->m39_shadowShade = 0x6B;
            pNew->m10_bytecode0 = NULL;
            pNew->m21_sourceEntryId = pMecha->m20_mechaEntryId;
            pNew->m20_mechaEntryId = tmp;
            pNew->m10D_num114 = 0;
            pNew->m10E_num118 = 0;
            initMechSub110_battle(pNew);
            newBones = allocateHeap(pMecha->m4_bones->mA_numBones * sizeof(sMechaBone), 1);
            pNew->m4_bones = newBones;
            for (tmp = 0; tmp < pMecha->m4_bones->mA_numBones; tmp++) {
                newBones[tmp] = pMecha->m4_bones[tmp];
                if (pMecha->m4_bones[tmp].m0_parentBone != NULL) {
                    newBones[tmp].m0_parentBone =
                        &newBones[((uptr)pMecha->m4_bones[tmp].m0_parentBone - (uptr)pMecha->m4_bones) / sizeof(sMechaBone)];
                }
                newBones[tmp].m7_isEnabled = 0;
                newBones[tmp].m70_animTracks[0] = NULL;
                newBones[tmp].m70_animTracks[1] = NULL;
            }
            detachMechaBoneSubtree_battle(tracks, op, pMecha->m4_bones, newBones);
            if (hi != 0xFF) {
                initMechaAnimation_battle(pNew, pNew, tracks, hi);
            }
            break;
        }
        case 0x16:
            reattachMechaBones_battle(pMecha->m4_bones, battleMechas[pMecha->m21_sourceEntryId]->m4_bones);
            freeMecha_battle(pMecha->m20_mechaEntryId);
            if (hi != 0xFF) {
                initMechaAnimation_battle(battleMechas[pMecha->m21_sourceEntryId], battleMechas[pMecha->m21_sourceEntryId], tracks, hi);
            }
            return;
        case 0x17:
            freeMecha_battle(pMecha->m20_mechaEntryId);
            return;
        case 0x18:
            mechaOP_11_b_battle(pMecha, mechaGetKeyframe_battle(pMecha, hi, &tmp), (s16)*pNext++);
            break;
        case 0x19:
            mechaOP_19_battle(pMecha);
            break;
        case 0x1A: {
            RECT rect;
            s16 x;
            s16 y;
            s16 w;

            rect.x = *pNext++;
            rect.y = *pNext++;
            x = *pNext++;
            y = *pNext++;
            w = *pNext++;
            rect.w = (w + 1) / 2 * 2;
            rect.h = *pNext++;
            if (hi & 1) {
                if (pMecha->m90_vramCoords[0] < 0) {
                    break;
                }
                rect.x += pMecha->m90_vramCoords[0];
                rect.y += pMecha->m90_vramCoords[1];
                x += pMecha->m90_vramCoords[0];
                y += pMecha->m90_vramCoords[1];
            }
            MoveImage(&rect, x, y);
            break;
        }
        case 0x1B:
            if (hi < pMecha->m10E_num118) {
                sLoadedMechas_118* pParent;
                MATRIX* pColor;
                void* t;
                s16 x0, y0, a7, x1, y1, z1, x, y;
                u16 w;

                op = *pNext++;
                if ((u8)op != 0xFF && (u8)op < pMecha->m10E_num118) {
                    pParent = &pMecha->m118_colorAnims[(u8)op];
                } else {
                    pParent = NULL;
                }
                opHi = op >> 8;
                if ((u32)(opHi & 0x7F) < 4) {
                    pColor = NULL;
                } else {
                    pColor = battle800d2fc0;
                }
                w = *pNext++;
                op = w;
                op3 = op;
                t = getEnvTileStepFunction_battle(w >> 8);
                x0 = *pNext++;
                y0 = *pNext++;
                a7 = *pNext++;
                x1 = *pNext++;
                y1 = *pNext++;
                z1 = *pNext++;
                x = *pNext++;
                y = *pNext++;
                if (opHi & 0x80) {
                    if (pMecha->m90_vramCoords[0] < 0) {
                        pNext += 5;
                        break;
                    }
                    x += pMecha->m90_vramCoords[2];
                    y += pMecha->m96_clutY;
                    if ((op3 & 0xF) == 1) {
                        x0 += pMecha->m90_vramCoords[2];
                        y0 += pMecha->m96_clutY;
                    }
                    if ((op3 >> 4) == 1) {
                        x1 += pMecha->m90_vramCoords[2];
                        y1 += pMecha->m96_clutY;
                    }
                }
                initAnimatedEnvTextureTile_battle(&pMecha->m118_colorAnims[hi], pParent, opHi & 0x7F, op3 | 0x700, (sLoadedMechas_118_1C*)pColor,
                              x0, y0, a7, x1, y1, z1, x, y, ANIM_WORD_AT(pNext, 0), ANIM_WORD_AT(pNext, 1),
                              ANIM_WORD_AT(pNext, 2), ANIM_WORD_AT(pNext, 3), ANIM_WORD_AT(pNext, 4), t);
                pNext += 5;
            } else {
                pNext += 15;
            }
            break;
        case 0x1C:
            if (hi < pMecha->m10E_num118) {
                freeAnimatedEnvTextureTile_battle(&pMecha->m118_colorAnims[hi]);
            }
            break;
        case 0x1D: {
            u16 w0;
            u16 w1;

            w0 = *pNext++;
            op = w0;
            w1 = *pNext++;
            opLo = op;
            op = w1;
            setupBoneInterpolationTrack_battle(pMecha, tracks, &pMecha->m4_bones[hi], opLo, w0 >> 8, (u8)op, w1 >> 8,
                          ANIM_WORD_AT(pNext, 0), ANIM_WORD_AT(pNext, 1), ANIM_WORD_AT(pNext, 2), ANIM_WORD_AT(pNext, 3),
                          ANIM_WORD_AT(pNext, 4), ANIM_WORD_AT(pNext, 5), ANIM_WORD_AT(pNext, 6));
            pNext += 7;
            param_3 = -1;
            break;
        }
        case 0x1E:
            pMecha->m37_useBoneScale = hi;
            break;
        case 0x1F: {
            sLoadedMechas* p = battleMechas[getMechaBitfieldForAnim_battle(pOrig, hi, (u16*)&op)];

            if (p != NULL) {
                pMecha = p;
            }
            break;
        }
        case 0x20:
            if (param_3 != -1) {
                if (param_3 & 0x100) {
                    pNext = pCur;
                    cont = 0;
                    break;
                }
                break;
            }
            pNext = pCur;
            cont = 0;
            break;
        case 0x21:
            pMecha->m3C_trackTag = hi;
            if (param_3 != -1) {
                if (param_3 & 1) {
                    pNext = pCur;
                    cont = 0;
                    break;
                }
                break;
            }
            pNext = pCur;
            cont = 0;
            break;
        case 0x22:
            if (param_3 != -1) {
                op = *pNext++;
                if (hi == 0xFF) {
                    if (!(param_3 & 0x400)) {
                        pNext = pCur;
                        cont = 0;
                        break;
                    }
                    pMecha->m42_loopCount++;
                    if (pMecha->m42_loopCount < op) {
                        pNext = pCur;
                        cont = 0;
                        break;
                    }
                } else {
                    pMecha->m3C_trackTag = hi;
                    if (!(param_3 & 4)) {
                        pNext = pCur;
                        cont = 0;
                        break;
                    }
                    pMecha->m42_loopCount++;
                    if (pMecha->m42_loopCount < op) {
                        pNext = pCur;
                        cont = 0;
                        break;
                    }
                }
                pMecha->m42_loopCount = 0;
                break;
            }
            pNext = pCur;
            cont = 0;
            break;
        case 0x23:
            setBoneVisibility_battle(pMecha, &pMecha->m4_bones[(s16)*pNext++], hi);
            break;
        case 0x24:
            if (pMecha != NULL) {
                pMecha->m34_visible = hi & 1;
            }
            break;
        case 0x25: {
            MATRIX m;
            VECTOR d;
            VECTOR ax;
            VECTOR ay;
            VECTOR az;
            SVECTOR unit;
            u16 w;
            u16 ox;
            u16 oy;
            u16 oz;
            sMechaBone* bone;

            w = *pNext++;
            op = w;
            opHi = w >> 8;
            getMechaBitfieldForAnim_battle(pMecha, (u8)op, (u16*)&op);
            ox = *pNext++;
            oy = *pNext++;
            oz = *pNext++;
            for (tmp = 0; tmp < 13; tmp++) {
                if ((op >> tmp) & 1) {
                    if (battleMechas[tmp] != NULL) {
                        /* Nested do/while (0): register-allocation weight only (flow counts refs by loop
                         * depth), no code change; as in field_mecha func_801E39F0 (6+6 levels on opHi). */
                        do { do { do { do { do { do { battleMechas[tmp]->m5E_attachBone = opHi; } while (0); } while (0); } while (0); } while (0); } while (0); } while (0);
                        battleMechas[tmp]->m5C_attachMecha = pMecha->m20_mechaEntryId;
                        battleMechas[tmp]->m5D_attachRotation = hi & 2;
                        battleMechas[tmp]->m36_disableGroundSnap = 1;
                        if (hi & 1) {
                            do { do { do { do { do { do { bone = &pMecha->m4_bones[opHi]; } while (0); } while (0); } while (0); } while (0); } while (0); } while (0);
                            SetRotMatrix(&bone->m2C_boneFinalMatrix);
                            m.t[0] = 0;
                            m.t[1] = 0;
                            m.t[2] = 0;
                            SetTransMatrix(&m);
                            unit.vx = 0x1000;
                            unit.vy = 0;
                            unit.vz = 0;
                            gte_ldv0(&unit);
                            gte_rt();
                            gte_stlvnl(&ax);
                            unit.vx = 0;
                            unit.vy = 0x1000;
                            gte_ldv0(&unit);
                            gte_rt();
                            gte_stlvnl(&ay);
                            unit.vy = 0;
                            unit.vz = 0x1000;
                            gte_ldv0(&unit);
                            gte_rt();
                            gte_stlvnl(&az);
                            d.vx = battleMechas[tmp]->m4_bones->m5C_translation[0] - bone->m2C_boneFinalMatrix.t[0];
                            d.vy = battleMechas[tmp]->m4_bones->m5C_translation[1] - bone->m2C_boneFinalMatrix.t[1];
                            d.vz = battleMechas[tmp]->m4_bones->m5C_translation[2] - bone->m2C_boneFinalMatrix.t[2];
                            battleMechas[tmp]->m6A_attachOffset[0] = projectOnPlaneNormalScaled_battle(&d, &ay, &az, pMecha->m1C_scale);
                            battleMechas[tmp]->m6A_attachOffset[1] = projectOnPlaneNormalScaled_battle(&d, &az, &ax, pMecha->m1C_scale);
                            battleMechas[tmp]->m6A_attachOffset[2] = projectOnPlaneNormalScaled_battle(&d, &ax, &ay, pMecha->m1C_scale);
                        } else {
                            battleMechas[tmp]->m6A_attachOffset[0] = ox;
                            battleMechas[tmp]->m6A_attachOffset[1] = oy;
                            battleMechas[tmp]->m6A_attachOffset[2] = oz;
                        }
                    }
                }
            }
            break;
        }
        case 0x26:
            getMechaBitfieldForAnim_battle(pMecha, hi, (u16*)&op);
            for (tmp = 0; tmp < 13; tmp++) {
                if ((op >> tmp) & 1 && battleMechas[tmp] != NULL) battleMechas[tmp]->m5C_attachMecha = 0xFF;
            }
            break;
        case 0x27:
            if (pMecha->m37_useBoneScale != 0) {
                updateMechAnimSub2_battle(pMecha->m4_bones, pMecha->m1C_scale);
            } else {
                updateMechAnimSub1_battle(pMecha->m4_bones, pMecha->m1C_scale);
            }
            break;
        case 0x28:
        case 0x29: {
            s32 dist;
            s32 found;
            u16 w;
            sMechaBone* bone;

            dist = computeMechaDistanceToTarget_battle(pMecha);
            if (pMecha->m8E_stepDistance == 0) {
                pMecha->m8E_stepDistance = 1;
            }
            w = *pNext++;
            op = w;
            bone = &pMecha->m4_bones[(u8)op];
            opHi = w >> 8;
            dist = dist / pMecha->m8E_stepDistance;
            found = 0;
            if (bone->m70_animTracks[0] != NULL) {
                found = opHi == bone->m70_animTracks[0]->m10_currentTime;
            } else if (bone->m70_animTracks[1] != NULL) {
                if (bone->m70_animTracks[1]->m10_currentTime == opHi) {
                    found = 1;
                }
            }
            if (found) {
                if (lo == 0x28) {
                    if (dist < hi) {
                        break;
                    }
                    pNext = pCur;
                    cont = 0;
                    break;
                }
                if (!(dist < hi)) {
                    break;
                }
                pNext = pCur;
                cont = 0;
                break;
            }
            pNext = pCur;
            cont = 0;
            break;
        }
        case 0x2A:
            if (computeMechaDistanceToTarget_battle(pMecha) < pMecha->m8E_stepDistance) {
                break;
            }
            pNext = pCur;
            cont = 0;
            break;
        case 0x2B:
            if (computeMechaDistanceToTarget_battle(pMecha) > pMecha->m8E_stepDistance) {
                break;
            }
            pNext = pCur;
            cont = 0;
            break;
        case 0x2C:
        case 0x2D: {
            s32 dx;
            s32 dy;
            s32 dz;
            s32 dist;

            dx = (s16)*pNext++ - pMecha->m4_bones->m5C_translation[0];
            dy = (s16)*pNext++ - pMecha->m4_bones->m5C_translation[1];
            dz = (s16)*pNext++ - pMecha->m4_bones->m5C_translation[2];
            dist = SquareRoot0(dx * dx + dy * dy + dz * dz);
            if (lo == 0x2C) {
                if (dist < pMecha->m8E_stepDistance) {
                    break;
                }
                pNext = pCur;
                cont = 0;
                break;
            }
            if (dist > pMecha->m8E_stepDistance) {
                break;
            }
            pNext = pCur;
            cont = 0;
            break;
        }
        case 0x2E:
            pMecha->m48_triggerDistance = pMecha->m8E_stepDistance;
            op = *pNext++;
            if (hi != 0) {
                pMecha->m4C_bytecode3 = (sMechaDataTable2_4_8*)((u8*)pCur + op);
            } else {
                pMecha->m4C_bytecode3 = NULL;
            }
            break;
        case 0x2F:
            if (mecha_battle_op3() != 0) {
                pNext = pCur;
                cont = 0;
                break;
            }
            break;
        case 0x30:
            *pNext++ = 0;
            break;
        case 0x31: {
            sMechaDataTable2_4_8* p;
            u8 limit;

            op = *pNext++;
            p = (sMechaDataTable2_4_8*)((u8*)pCur + op);
            op = *p++;
            limit = op >> 8;
            op = *p + 1;
            *p++ = op;
            if (op < limit) {
                pNext = p;
            }
            break;
        }
        case 0x32:
            op = *pNext;
            pNext = (sMechaDataTable2_4_8*)((u8*)pCur + op);
            break;
        case 0x33:
            op = *pNext++;
            if (currentBattleLoaderTransitionEffect != 0) {
                pNext = (sMechaDataTable2_4_8*)((u8*)pCur + op);
            }
            break;
        case 0x34:
            op = *pNext++;
            if (pMecha->m22_isBattleGear == 0) {
                pNext = (sMechaDataTable2_4_8*)((u8*)pCur + op);
            }
            break;
        case 0x35:
            op = *pNext++;
            if (rand() >= 0x4000) {
                pNext = (sMechaDataTable2_4_8*)((u8*)pCur + op);
            }
            break;
        case 0x36:
            pMecha->m44_delayTimer = 0;
            pMecha->m46_delay = *pNext++;
            op = *pNext++;
            if (hi != 0) {
                pMecha->m50_bytecode2 = (sMechaDataTable2_4_8*)((u8*)pCur + op);
            } else {
                pMecha->m50_bytecode2 = NULL;
            }
            break;
        case 0x37:
            op = *pNext++;
            if (hi != 0) {
                pMecha->m54_bytecode1 = (sMechaDataTable2_4_8*)((u8*)pCur + op);
            } else {
                pMecha->m54_bytecode1 = NULL;
            }
            break;
        case 0x38: {
            u16 w;

            w = *pNext++;
            op = w;
            setupBoneLookAtTrack_battle(tracks, pMecha->m4_bones, 0, hi, (u8)op, w >> 8, pMecha->m88_targetPosition[0], pMecha->m88_targetPosition[1],
                          pMecha->m88_targetPosition[2]);
            break;
        }
        case 0x39: {
            u16 w;

            w = *pNext++;
            op = w;
            setupBoneLookAtTrack_battle(tracks, pMecha->m4_bones, 1, hi, (u8)op, w >> 8, pMecha->m88_targetPosition[0], pMecha->m88_targetPosition[1],
                          pMecha->m88_targetPosition[2]);
            break;
        }
        case 0x3A:
            if (peekNextBattleAnimation(2) != 0xFE && pMecha->m22_isBattleGear != 0 && pMecha->m35_isEventAnimation == 0) {
                break;
            }
            if (mecha_battle_op3() != 0) {
                pNext = pCur;
                cont = 0;
                break;
            }
            break;
        case 0x3B:
            op = *pNext++;
            if ((performAttackSub3_var0.m0_performAttackSub3_var0 >> pMecha->m20_mechaEntryId) & 1) {
                pNext = (sMechaDataTable2_4_8*)((u8*)pCur + op);
            }
            break;
        case 0x3C:
            op = *pNext++;
            opLo = (u8)op;
            opHi = op >> 8;
            fadeSoundEffectVolume(opLo + getMechaSoundEffectIdBase_battle(pMecha, hi), 0, opHi);
            break;
        case 0x3D: {
            op = pMecha->m2A_animQueue[1];
            if (op < 2) {
                break;
            }
            for (tmp = 1; tmp < op; tmp++) {
                if (pMecha->m2A_animQueue[tmp + 5] == hi) {
                    goto flush;
                }
            }
            break;
        }
        case 0x3E: {

            opLo = 1;
            for (tmp = 0; tmp < 13; tmp++) {
                if ((mechaPlayAnimationVar0_battle >> tmp) & 1) {
                    switch (battleCurrentDamages[allocateJumpAnimationStructVar0 - 1].m18_damageType[tmp]) {
                    case 0:
                    case 1:
                        /* do/while (0) here and on opLo below only weight the allocator (no code): retail
                         * allocates opHi (s2) and opLo (s1) ahead of pCur (s5), hi (s6), op3 (s7). */
                        do { do { opHi = 0; } while (0); } while (0);
                        break;
                    case 2:
                    case 3:
                    case 5:
                        do { do { opHi = 5; } while (0); } while (0);
                        break;
                    case 4:
                    default:
                        opHi = 4;
                        break;
                    }
                    if (opHi != hi && hi < 8) {
                        do { do { do { do { do { do { do { do { do { do { do { do { do { do { do { do { opLo = 0; } while (0); } while (0); } while (0); } while (0); } while (0); } while (0); } while (0); } while (0); } while (0); } while (0); } while (0); } while (0); } while (0); } while (0); } while (0); } while (0);
                    }
                    if (hi == 8 && (battleMechas[tmp] == NULL || !(battleMechas[tmp]->m4A_flags & 2))) {
                        do { do { do { do { do { do { do { do { do { do { do { do { do { do { do { do { opLo = 0; } while (0); } while (0); } while (0); } while (0); } while (0); } while (0); } while (0); } while (0); } while (0); } while (0); } while (0); } while (0); } while (0); } while (0); } while (0); } while (0);
                    }
                }
            }
            op = *pNext++;
            if (opLo) {
                pNext = (sMechaDataTable2_4_8*)((u8*)pCur + op);
            }
            break;
        }
        case 0x3F:
            createAllDamageDisplaysIfAnimating();
            break;
        case 0x40: {
            s16 x;
            s16 y;
            s16 z;

            x = *pNext++;
            y = *pNext++;
            z = *pNext++;
            setupBoneRotationTrack_battle(tracks, pMecha->m4_bones, hi, x, y, z);
            param_3 = -1;
            break;
        }
        case 0x41: {
            s32 x;
            s32 y;
            s32 z;

            x = *pNext++;
            y = *pNext++;
            z = *pNext++;
            {
                s16 rx = pMecha->m4_bones->m54_rotationAngles.vx + x;
                s16 ry = pMecha->m4_bones->m54_rotationAngles.vy + y;
                s16 rz = pMecha->m4_bones->m54_rotationAngles.vz + z;
                setupBoneRotationTrack_battle(tracks, pMecha->m4_bones, hi, rx, ry, rz);
            }
            param_3 = -1;
            break;
        }
        case 0x42:
        case 0x43: {
            s32 dx;
            s32 dy;
            s32 dz;
            s16 pitch;
            s16 yaw;
            s16 roll;

            dx = pMecha->m88_targetPosition[0] - pMecha->m4_bones->m5C_translation[0];
            dy = pMecha->m88_targetPosition[1] - pMecha->m4_bones->m5C_translation[1];
            dz = pMecha->m88_targetPosition[2] - pMecha->m4_bones->m5C_translation[2];
            if (lo == 0x43) {
                pitch = 0;
                dy = 0;
            } else {
                pitch = ratan2(dy, SquareRoot0(dx * dx + dz * dz));
            }
            yaw = ratan2(-dx, -dz);
            roll = 0;
            if (dx != 0 || dy != 0 || dz != 0) {
                setupBoneRotationTrack_battle(tracks, pMecha->m4_bones, hi, pitch, yaw, roll);
                param_3 = -1;
            }
            break;
        }
        case 0x44:
            pMecha->m70_angularVelocity[0] = *pNext++;
            pMecha->m70_angularVelocity[1] = *pNext++;
            pMecha->m70_angularVelocity[2] = *pNext++;
            break;
        case 0x45:
            pMecha->m70_angularVelocity[0] += *pNext++;
            pMecha->m70_angularVelocity[1] += *pNext++;
            pMecha->m70_angularVelocity[2] += *pNext++;
            break;
        case 0x46:
            pMecha->m76_angularAcceleration[0] = *pNext++;
            pMecha->m76_angularAcceleration[1] = *pNext++;
            pMecha->m76_angularAcceleration[2] = *pNext++;
            break;
        case 0x47:
            pMecha->m76_angularAcceleration[0] += *pNext++;
            pMecha->m76_angularAcceleration[1] += *pNext++;
            pMecha->m76_angularAcceleration[2] += *pNext++;
            break;
        case 0x48:
            pMecha->m36_disableGroundSnap = hi;
            break;
        case 0x49:
            pMecha->m4_bones->m5C_translation[0] = (s16)*pNext++;
            pMecha->m4_bones->m5C_translation[1] = (s16)*pNext++;
            pMecha->m4_bones->m5C_translation[2] = (s16)*pNext++;
            break;
        case 0x4A:
            if (hi == 0xFB) {
                s32 dx;
                s32 dy;
                s32 dz;
                s32 len;

                dx = pMecha->m4_bones->m5C_translation[0] - pMecha->m88_targetPosition[0];
                dy = pMecha->m4_bones->m5C_translation[1] - pMecha->m88_targetPosition[1];
                dz = pMecha->m4_bones->m5C_translation[2] - pMecha->m88_targetPosition[2];
                len = SquareRoot0(dx * dx + dy * dy + dz * dz);
                pMecha->m4_bones->m5C_translation[0] = pMecha->m88_targetPosition[0] + dx * pMecha->m8E_stepDistance / (len + 1);
                pMecha->m4_bones->m5C_translation[1] = pMecha->m88_targetPosition[1] + dy * pMecha->m8E_stepDistance / (len + 1);
                pMecha->m4_bones->m5C_translation[2] = pMecha->m88_targetPosition[2] + dz * pMecha->m8E_stepDistance / (len + 1);
            } else {
                u8 id = getMechaBitfieldForAnim_battle(pMecha, hi, (u16*)&op);

                pMecha->m4_bones->m5C_translation[0] = battleVisualEntities[id].mA_X;
                pMecha->m4_bones->m5C_translation[2] = battleVisualEntities[id].mC_Z;
            }
            break;
        case 0x4B:
            pMecha->m7C_velocity[0] = *pNext++;
            pMecha->m7C_velocity[1] = *pNext++;
            pMecha->m7C_velocity[2] = *pNext++;
            break;
        case 0x4C:
            pMecha->m7C_velocity[0] += *pNext++;
            pMecha->m7C_velocity[1] += *pNext++;
            pMecha->m7C_velocity[2] += *pNext++;
            break;
        case 0x4D:
            pMecha->m82_acceleration[0] = *pNext++;
            pMecha->m82_acceleration[1] = *pNext++;
            pMecha->m82_acceleration[2] = *pNext++;
            break;
        case 0x4E:
            pMecha->m82_acceleration[0] += *pNext++;
            pMecha->m82_acceleration[1] += *pNext++;
            pMecha->m82_acceleration[2] += *pNext++;
            break;
        case 0x4F: {
            s32 dx;
            s32 dy;
            s32 dz;
            s32 speed;

            dx = pMecha->m88_targetPosition[0] - pMecha->m4_bones->m5C_translation[0];
            dy = pMecha->m88_targetPosition[1] - pMecha->m4_bones->m5C_translation[1];
            dz = pMecha->m88_targetPosition[2] - pMecha->m4_bones->m5C_translation[2];
            if (hi == 0) {
                hi = 1;
            }
            speed = SquareRoot0(dx * dx + dy * dy + dz * dz) / hi;
            pMecha->m7C_velocity[2] = (speed << 12) / (pMecha->m1C_scale * pMecha->m4_bones->m4C_scale[2] >> 12) / 2;
            if (((ratan2(-dx, -dz) - (u16)pMecha->m4_bones->m54_rotationAngles.vy + 0x400) & 0xFFF) < 0x800) {
                pMecha->m7C_velocity[2] = -pMecha->m7C_velocity[2];
            }
            break;
        }
        case 0x50:
            pMecha->m58_targetMecha = 0;
            pMecha->m88_targetPosition[0] = *pNext++;
            pMecha->m88_targetPosition[1] = *pNext++;
            pMecha->m88_targetPosition[2] = *pNext++;
            break;
        case 0x51: {
            u8 id;

            pMecha->m58_targetMecha = 0;
            id = getMechaBitfieldForAnim_battle(pMecha, hi, (u16*)&op);
            pMecha->m88_targetPosition[0] = battleVisualEntities[id].mA_X;
            pMecha->m88_targetPosition[1] = battleVisualEntities[id].mE_Y;
            pMecha->m88_targetPosition[2] = battleVisualEntities[id].mC_Z;
            break;
        }
        case 0x52:
            pMecha->m58_targetMecha = hi;
            pMecha->m5A_targetBone = *pNext++;
            pMecha->m64_targetOffsetX = *pNext++;
            pMecha->m66_targetOffsetY = *pNext++;
            pMecha->m68_targetOffsetZ = *pNext++;
            if (hi != 0) {
                updateMechaTargetPosition_battle(pMecha);
            }
            break;
        case 0x53: {
            VECTOR normal;
            SVECTOR pos;

            pos.vx = pMecha->m88_targetPosition[0];
            pos.vy = 0;
            pos.vz = pMecha->m88_targetPosition[2];
            computeTerrainHeightAtTriangle(&pos, findBattleTerrainTriangleBruteForce(&pos), &normal);
            pMecha->m88_targetPosition[1] = pos.vy;
            break;
        }
        case 0x54:
            pMecha->m8E_stepDistance = (s16)*pNext++ * (pMecha->m1C_scale * pMecha->m4_bones->m4C_scale[2] >> 12) >> 12;
            break;
        case 0x55:
            pMecha->m8E_stepDistance += (s16)*pNext++ * (pMecha->m1C_scale * pMecha->m4_bones->m4C_scale[2] >> 12) >> 12;
            break;
        case 0x56:
            pMecha->m8E_stepDistance += *pNext++;
            break;
        case 0x57:
            tmp = getMechaBitfieldForAnim_battle(pMecha, hi, (u16*)&op);
            pMecha->m8E_stepDistance += getMechaColliderRadius_battle(tmp);
            break;
        case 0x58:
            pMecha->m58_targetMecha = hi;
            pMecha->m5A_targetBone = 0;
            pMecha->m64_targetOffsetX = 0;
            pMecha->m66_targetOffsetY = 0;
            pMecha->m68_targetOffsetZ = 0;
            if (hi != 0) {
                updateMechaTargetPosition_battle(pMecha);
            }
            break;
        case 0x59:
            pMecha->m58_targetMecha = 0;
            pMecha->m88_targetPosition[0] = battleEnvMechaInitData->m47C_cameraPresets[hi].m6_eye[0];
            pMecha->m88_targetPosition[1] = battleEnvMechaInitData->m47C_cameraPresets[hi].m6_eye[1];
            pMecha->m88_targetPosition[2] = battleEnvMechaInitData->m47C_cameraPresets[hi].m6_eye[2];
            break;
        case 0x5A:
            pMecha->m58_targetMecha = 0;
            pMecha->m88_targetPosition[0] = battleEnvMechaInitData->m47C_cameraPresets[hi].m0_at[0];
            pMecha->m88_targetPosition[1] = battleEnvMechaInitData->m47C_cameraPresets[hi].m0_at[1];
            pMecha->m88_targetPosition[2] = battleEnvMechaInitData->m47C_cameraPresets[hi].m0_at[2];
            break;
        case 0x5B:
            op = pMecha->m2A_animQueue[1];
            if (hi == 1 && op >= 2) {
                break;
            }
            pMecha->m2A_animQueue[1] = hi;
            if (hi != 2) {
                break;
            }
        flush:
            pMecha->m2A_animQueue[1] = 0;
            if (op < 2) {
                break;
            }
            for (tmp = 1; tmp < op; tmp++) {
                initMechaAnimation_battle(pMecha, battleMechas[pMecha->m2A_animQueue[tmp + 1]], tracks, pMecha->m2A_animQueue[tmp + 5]);
            }
            return;
        case 0x5C:
            op = *pNext++;
            if (pMecha->m88_targetPosition[0] == pMecha->m4_bones->m5C_translation[0] &&
                pMecha->m88_targetPosition[1] == pMecha->m4_bones->m5C_translation[1] &&
                pMecha->m88_targetPosition[2] == pMecha->m4_bones->m5C_translation[2]) {
                pNext = (sMechaDataTable2_4_8*)((u8*)pCur + op);
            }
            break;
        case 0x5D:
            op = *pNext++;
            pMecha->m4_bones[op].m52_billboardMode = hi;
            break;
        case 0x5E:
            op = *pNext++;
            pMecha->m1C_scale = op;
            break;
        case 0x5F:
            op = *pNext++;
            pMecha->m4A_flags = op;
            break;
        case 0x60: {
            u8 slot = battleVisualEntities[pMecha->m20_mechaEntryId].m0_positionSlot;

            pMecha->m88_targetPosition[0] = ((u16)battleEnvMechaInitData->m100_gearPositions[slot][0].vx + (u16)battleEnvMechaInitData->m100_gearPositions[slot][1].vx) >> 1;
            pMecha->m88_targetPosition[1] = 0;
            pMecha->m88_targetPosition[2] = ((u16)battleEnvMechaInitData->m100_gearPositions[slot][0].vy + (u16)battleEnvMechaInitData->m100_gearPositions[slot][1].vy) >> 1;
            break;
        }
        case 0x61:
            op = *pNext++;
            if ((lo = getBattleSlotLayout(pMecha->m20_mechaEntryId)) != 0) {
                pNext = (sMechaDataTable2_4_8*)((u8*)pCur + op);
            }
            break;
        case 0x62: {
            s32 bone;
            s32 x;

            bone = (s16)*pNext++;
            x = (s16)*pNext++;
            mechaOp_62_battle(pMecha, &pMecha->m4_bones[bone], hi, x, ANIM_WORD_AT(pNext, 0), ANIM_WORD_AT(pNext, 1));
            pNext += 2;
            break;
        }
        case 0x63: {
            u16 w;

            w = *pNext++;
            pMecha->m98_eventFrame = 0;
            pMecha->m9A_eventLoopLength = -1;
            pMecha->m9C_eventIndex = 0;
            pMecha->m9E_numEvents = hi;
            pMecha->mA0_eventCursor = (u8*)pNext;
            op = w;
            pNext = (sMechaDataTable2_4_8*)((u8*)pCur + (s16)w);
            break;
        }
        case 0x64:
            pMecha->m3E_clothPullY = *pNext++;
            break;
        case 0x65:
        case 0x66: {
            s16 mask;
            s16 type;
            s16 x;
            s16 y;
            s16 z;
            s16 tx;
            s16 tz;
            u16 w;

            w = *pNext++;
            opHi = w >> 8;
            op = w;
            opLo = op;
            w = *pNext++;
            op4 = w >> 8;
            op = w;
            op3 = op;
            if (lo == 0x65) {
                x = battleCameraAt.vx;
                y = battleCameraAt.vy;
                z = battleCameraAt.vz;
            } else {
                x = battleCameraEye.vx;
                y = battleCameraEye.vy;
                z = battleCameraEye.vz;
            }
            tx = getMechaBitfieldForAnim_battle(pMecha, opHi, (u16*)&mask);
            mask = *pNext++;
            type = 2;
            if (opHi == 0xF6) {
                tx = pMecha->m88_targetPosition[0];
                mask += pMecha->m88_targetPosition[1];
                tz = pMecha->m88_targetPosition[2];
            } else if (opHi == 0xF5) {
                tx = battleEnvMechaInitData->m47C_cameraPresets[op3].m0_at[0];
                mask += battleEnvMechaInitData->m47C_cameraPresets[op3].m0_at[1];
                tz = battleEnvMechaInitData->m47C_cameraPresets[op3].m0_at[2];
            } else if (opHi == 0xF4) {
                tx = battleEnvMechaInitData->m47C_cameraPresets[op3].m6_eye[0];
                mask += battleEnvMechaInitData->m47C_cameraPresets[op3].m6_eye[1];
                tz = battleEnvMechaInitData->m47C_cameraPresets[op3].m6_eye[2];
            } else {
                if (battleMechas[tx] != NULL) {
                    mask = initMechaForBattleSub0_battle(tx) * mask / 4096;
                    type = 0;
                    tz = -1;
                    if (opHi == 0xF9) {
                        for (tz = 0; tz < 13; tz++) {
                            if ((mechaPlayAnimationVar0_battle >> tz) & 1) {
                                break;
                            }
                        }
                    }
                } else {
                    mask = mask / 64;
                    type = 0;
                    tz = -1;
                }
            }
            /* do/while (0): weights lo above op4 and op4's refs past the caller-save threshold
             * (retail keeps op4 in a3, saved at 0x1C8 around calls). */
            do { do { startMechaEventAnimTrack(tracks, lo - 0x5E, hi + type, opLo, x, y, z, tx, mask, tz, (s16)op4); } while (0); } while (0);
            break;
        }
        case 0x67: {
            u16 w;
            s16 base;
            s16 value;
            s16 target;
            u16 a;

            w = *pNext++;
            opHi = w >> 8;
            op = w;
            w = *pNext++;
            opLo = op;
            op4 = w >> 8;
            op = w;
            a = *pNext++;
            op3 = op;
            op = *pNext++;
            if (hi & 0x20) {
                base = 0;
            } else {
                base = pMecha->m4_bones->m54_rotationAngles.vy;
            }
            hi &= 0x1F;
            if (opHi == 0) {
                value = mechaEventCameraYaw & 0xFFF;
            } else if (opHi == 1) {
                value = mechaEventTargetYaw & 0xFFF;
            } else if (opHi == 2) {
                value = mechaEventCameraPitch & 0xFFF;
            } else {
                if (opHi == 3) {
                    value = mechaEventCameraDistance;
                } else if (opHi == 4) {
                    value = mechaEventTargetDistance;
                } else if (opHi == 5) {
                    value = mechaEventTargetHeight;
                } else if (opHi == 6) {
                    value = mechaEventCameraHeight;
                }
                a = (s16)a * battleEnvMechaInitData->m344_mechaScale >> 12;
                op = op * battleEnvMechaInitData->m344_mechaScale >> 12;
            }
            if (op3 & 0x20) {
                target = value;
                value += a;
            } else {
                if (opHi < 2) {
                    value = (a + base) & 0xFFF;
                } else if (opHi < 3) {
                    value = a & 0xFFF;
                } else {
                    value = a;
                }
                target = value;
            }
            if (op3 & 0x40) {
                target += op;
            } else if (opHi < 3) {
                s16 start;

                if (opHi < 2) {
                    start = (a + base) & 0xFFF;
                } else {
                    start = a & 0xFFF;
                }
                target = (op - a) & 0xFFF;
                if (target >= 0x800) {
                    target |= 0xF000;
                }
                target += start;
            } else {
                target = op;
            }
            startMechaEventAnimTrack(tracks, opHi, hi + 2, opLo, value, 0, 0, target, 0, 0, (s16)op4);
            break;
        }
        case 0x68: {
            s32 dx;
            s32 dz;

            dx = battleCameraAt.vx - battleCameraEye.vx;
            dz = battleCameraAt.vz - battleCameraEye.vz;
            releaseMechaEventAnimTracks(tracks);
            setBattleCameraMode4IfAllowed();
            battle800c3df8 = 1;
            mechaEventAnimWaitTag = 0xFF;
            mechaEventTargetYaw = mechaEventCameraYaw = ratan2(dx, dz) & 0xFFF;
            mechaEventCameraPitch = 0;
            mechaEventCameraDistance = 0;
            mechaEventCameraHeight = 0;
            mechaEventAnimSnapCamera = 0;
            mechaEventTargetHeight = 0;
            mechaEventTargetDistance = 0;
            break;
        }
        case 0x69:
            if (mechaEventAnimWaitTag != 0xFF) {
                if (mechaEventAnimWaitState & 1) {
                    pNext = pCur;
                    cont = 0;
                    break;
                }
                mechaEventAnimWaitTag = 0xFF;
                break;
            }
            mechaEventAnimWaitTag = hi;
            pNext = pCur;
            cont = 0;
            break;
        case 0x6A:
            mechaEventAnimSnapCamera = 1;
            break;
        case 0x6B:
            op = *pNext++;
            pMecha->m4_bones[op].m6_useYXZRotation = hi;
            break;
        case 0x6C:
            if (isCDBusy() != 0) {
                pNext = pCur;
                cont = 0;
                break;
            }
            break;
        case 0x6D:
            pMecha->m38_syncFlag = hi & 1;
            break;
        case 0x6E: {
            u8 idx = getMechaBitfieldForAnim_battle(pMecha, hi, (u16*)&op);
            sLoadedMechas* p;

            op = *pNext++;
            p = battleMechas[idx];
            if (p != NULL && p->m38_syncFlag == (op & 1)) {
                pNext = pCur;
                cont = 0;
                break;
            }
            break;
        }
        case 0x6F:
            if (hi != 0) {
                pMecha->m3A_savedAnimMask = mechaPlayAnimationVar0_battle;
            } else {
                pMecha->m3A_savedAnimMask = -1;
            }
            break;
        case 0x70:
            op = *pNext++;
            if (pMecha->m3A_savedAnimMask == mechaPlayAnimationVar0_battle) {
                pNext = (sMechaDataTable2_4_8*)((u8*)pCur + op);
                cont = 0;
            }
            break;
        case 0x71:
            op = *pNext++;
            if (hi == 0) {
                gearAbilityAnimationId = op;
            }
            break;
        case 0x72:
            if (fxFragmentInitialized == 0) {
                pNext = pCur;
            }
            cont = 0;
            break;
        case 0x73:
            cont = 0;
            requestFxFragmentSetup(gearAbilityAnimationId);
            break;
        case 0x74:
            signalEffectMechaEvent();
            return;
        case 0x75:
            op = *pNext++;
            if ((ratan2(pMecha->m4_bones->m5C_translation[0] - pMecha->m88_targetPosition[0],
                        pMecha->m4_bones->m5C_translation[2] - pMecha->m88_targetPosition[2]) & 0xFFF) ==
                (pMecha->m4_bones->m54_rotationAngles.vy & 0xFFF)) {
                pNext = (sMechaDataTable2_4_8*)((u8*)pCur + op);
            }
            break;
        default:
            pNext--;
            cont = 0;
            break;
        }
    }
    pMecha->m10_bytecode0 = pNext;
    if (flag != 0) {
        queueMechaBattleAnimationStep();
    }
}

#undef ANIM_WORD_ARG
#undef ANIM_WORD_AT

/* rotation anim track setup (twin 801E59D4) */
void setupBoneRotationTrack_battle(sMechaInitVar2* pool, sMechaBone* pBone, s32 duration, s32 x, s32 y, s32 z) {
    sMechaInitVar2Sub* pTrack;

    if (duration < 2) {
        pBone->m54_rotationAngles.vx = x;
        pBone->m54_rotationAngles.vy = y;
        pBone->m54_rotationAngles.vz = z;
        pBone->m5_needRotationUpdate = 1;
        return;
    }
    if (pBone->m54_rotationAngles.vx == x && pBone->m54_rotationAngles.vy == y && pBone->m54_rotationAngles.vz == z) {
        return;
    }
    if (pBone->m70_animTracks[0] != NULL) {
        pTrack = pBone->m70_animTracks[0];
    } else {
        pTrack = allocateAnimTrack_battle(pool);
    }
    if (pTrack != NULL) {
        pTrack->m0_inUse = 1;
        pTrack->m2_type = 3;
        pTrack->m1_isLooping = 0;
        pTrack->m3_tag = 0xFE;
        pTrack->m4.v.m4_x = pBone->m54_rotationAngles.vx;
        pTrack->m4.v.m6_y = pBone->m54_rotationAngles.vy;
        pTrack->m4.v.m8_z = pBone->m54_rotationAngles.vz;
        x = (x - pBone->m54_rotationAngles.vx) & 0xFFF;
        if (x >= 0x800) {
            x -= 0x1000;
        }
        pTrack->m4.v.mA_x = x;
        y = (y - pBone->m54_rotationAngles.vy) & 0xFFF;
        if (y >= 0x800) {
            y -= 0x1000;
        }
        pTrack->m4.v.mC_y = y;
        z = (z - pBone->m54_rotationAngles.vz) & 0xFFF;
        if (z >= 0x800) {
            z -= 0x1000;
        }
        pTrack->m4.v.mE_z = z;
        pTrack->m10_currentTime = 0;
        pTrack->m12_maxTime = duration;
        pBone->m70_animTracks[0] = pTrack;
    }
}

/* translation anim track setup (twin 801E5B50) */
void setupBoneLookAtTrack_battle(sMechaInitVar2* pool, sMechaBone* pBone, s32 type, s32 arg3, s32 arg4, s32 duration, s32 x, s32 y, s32 z) {
    sMechaInitVar2Sub* pTrack;

    if (pBone->m70_animTracks[0] != NULL) {
        pTrack = pBone->m70_animTracks[0];
    } else {
        pTrack = allocateAnimTrack_battle(pool);
    }
    if (pTrack != NULL) {
        pTrack->m0_inUse = 1;
        pTrack->m2_type = type + 7;
        pTrack->m1_isLooping = 0;
        pTrack->m3_tag = 0xFE;
        pTrack->m4.v.m4_x = SquareRoot0((x - pBone->m5C_translation[0]) * (x - pBone->m5C_translation[0]) +
                                 (y - pBone->m5C_translation[1]) * (y - pBone->m5C_translation[1]) +
                                 (z - pBone->m5C_translation[2]) * (z - pBone->m5C_translation[2])) + 1;
        pTrack->m4.v.m6_y = arg3;
        pTrack->m4.v.m8_z = arg4;
        pTrack->m4.v.mA_x = x;
        pTrack->m4.v.mC_y = y;
        pTrack->m4.v.mE_z = z;
        pTrack->m10_currentTime = 0;
        pTrack->m12_maxTime = duration;
        pBone->m70_animTracks[0] = pTrack;
    }
}

/* mechaOP_11_b] start keyframe event stream (twin 801E5C74) */
void mechaOP_11_b_battle(sLoadedMechas* pMecha, sMechaDataTable2_4_4* pKey, s32 loop) {
    if (pKey->m12_numEvents != 0) {
        pMecha->m98_eventFrame = 0;
        if (loop != 0) {
            pMecha->m9A_eventLoopLength = pKey->m2_keyDuration;
        } else {
            pMecha->m9A_eventLoopLength = -1;
        }
        pMecha->m9C_eventIndex = 0;
        pMecha->m9E_numEvents = pKey->m12_numEvents;
        pMecha->mA4_eventStream = pMecha->mA0_eventCursor = (u8*)pKey + pKey->m14_eventsOffset;
    } else {
        pMecha->m98_eventFrame = -1;
    }
}

/* sound bank id of source 0..3 (801E5CD8 + source 3 = gCurrentBattleEventSeq) */
s32 getMechaSoundEffectIdBase_battle(sLoadedMechas* pMecha, s32 which) {
    if (which == 0) {
        return gBattleCurrentSoundEffectSeq->u14.m14_id << 16;
    }
    if (which == 1) {
        return *(u16*)(pMecha->mB0_sound->m8_seq + 0x14) << 16;
    }
    if (which == 2) {
        return *(u16*)(pMecha->mB4_extraSound->m8_seq + 0x14) << 16;
    }
    if (which == 3) {
        return gCurrentBattleEventSeq->u14.m14_id << 16;
    }
    FALLOFF_RETURN(3); /* any other bank: the last compare's 3 */
}

/* Noah: mechaAnimEventStream (battle copy of field_mecha func_801E5D44):
 * runs the current keyframe's events for this frame: 1 sprite effect on a
 * bone, 2 light setup, 3/4 bone trails, 5 sound, 6 createAllDamageDisplaysIfAnimating, 7 bone
 * visibility, 8 animations on the targets, 9 CLUT/colour animations. */
void updateMechAnimSub4_battle(sLoadedMechas* pMecha, sMechaInitVar2* pool, s32 oddOrEven) {
    sMechaAnimEvent* p;
    sLoadedMechas_118* pParent;
    MATRIX* pColor;
    s32 i;
    s16 x0, y0, x1, y1, z1;
    void* t;
    SVECTOR pos;
    VECTOR out;
    VECTOR normal;

    if (pMecha->m98_eventFrame < 0) {
        return;
    }
    for (; pMecha->m9C_eventIndex < pMecha->m9E_numEvents; pMecha->m9C_eventIndex++) {
        p = (sMechaAnimEvent*)pMecha->mA0_eventCursor;
        if (pMecha->m98_eventFrame != p->m0_frame) {
            break;
        }
        switch (p->m2_type) {
        case 1: {
            sMechaAnimEventEffect* e;
            MATRIX* m;
            s16 target;
            s16 anim;
            s32 dir;
            s16 scale;
            SpriteVramData* vram;
            sLoadedMechas* pSrc;

            target = 0;
            m = SCRATCH_MATRIX_00;
            e = (sMechaAnimEventEffect*)p;
            if (e->m4_flags & 0x80) {
                anim = battleEntities.m0_entities[pMecha->m20_mechaEntryId].mA4_gear.m5D_weaponAnimOffsets[e->m3_animationId] - 1 + e->m3_animationId;
            } else {
                anim = e->m3_animationId;
            }
            if ((e->mC_mode & 0x80) || (e->m4_flags & 0x80)) {
                pSrc = battleMechas[setupMechaForEventSub0()];
                if (pSrc == NULL) {
                    target = setupMechaForEventSub0() + 1;
                    pSrc = pMecha;
                }
            } else {
                pSrc = pMecha;
            }
            if (anim >= 0) {
                if (e->m5_boneIndex != 0) {
                    CompMatrix(&pSrc->m4_bones->mC_localMatrix, &pSrc->m4_bones[e->m5_boneIndex].m2C_boneFinalMatrix,
                               m);
                } else {
                    *m = pSrc->m4_bones->mC_localMatrix;
                }
                if (target != 0) {
                    m->t[0] = battleVisualEntities[target - 1].mA_X;
                    m->t[1] = battleVisualEntities[target - 1].mE_Y;
                    m->t[2] = battleVisualEntities[target - 1].mC_Z;
                }
                SetRotMatrix(m);
                SetTransMatrix(m);
                pos.vx = e->m6_x;
                pos.vy = e->m8_y;
                pos.vz = e->mA_z;
                gte_ldv0(&pos);
                gte_rt();
                gte_stlvnl(&out);
                pos.vx = out.vx;
                pos.vz = out.vz;
                if ((e->mC_mode & 0x7F) == 1) {
                    pos.vy = pMecha->m60_groundY;
                } else if ((e->mC_mode & 0x7F) == 2) {
                    pos.vx = battleEnvMechaInitData->m4DC_effectGround[0];
                    pos.vy = battleEnvMechaInitData->m4DC_effectGround[1];
                    pos.vz = battleEnvMechaInitData->m4DC_effectGround[2];
                    computeTerrainHeightAtTriangle(&pos, findBattleTerrainTriangleBruteForce(&pos), &normal);
                } else {
                    pos.vy = out.vy;
                }
                if (e->mD_absoluteDirection != 0) {
                    dir = e->mE_direction;
                } else {
                    dir = pMecha->m4_bones->m54_rotationAngles.vy + 0x400 + e->mE_direction;
                }
                scale = (e->m10_scale * pMecha->m1C_scale) >> 8;
                vram = &createSavePointMeshData_mode5;
                if (e->m12_vramData != 0) {
                    vram = &createSavePointMeshData_mode6;
                }
                if (isMechaEventAllowedForHitResult(setupMechaForEventSub0(), e->m4_flags)) {
                    spawnMechaBoneAttachedSprite_battle(vram, anim, &pos.vx, dir, scale, (sMechaBoneAttachmentConfig*)e, pMecha);
                }
            }
            pMecha->mA0_eventCursor += 0x14;
            break;
        }
        case 2:
            if (p->m4 != 0) {
                if (p->m3_index < 2) {
                    sMechaAnimEventLight* e = (sMechaAnimEventLight*)p;

                    if (e->m5_isAbsolute != 0) {
                        mechaInitVar5_battle[e->m3_lightIndex].m10_mechaIndex = -1;
                    } else {
                        mechaInitVar5_battle[e->m3_lightIndex].m10_mechaIndex = pMecha->m20_mechaEntryId;
                    }
                    mechaInitVar5_battle[e->m3_lightIndex].m12_boneIndex = e->m6_boneIndex;
                    battle800d2fc0->m[0][e->m3_lightIndex + 1] = e->m7_r << 4;
                    battle800d2fc0->m[1][e->m3_lightIndex + 1] = e->m8_g << 4;
                    battle800d2fc0->m[2][e->m3_lightIndex + 1] = e->m9_b << 4;
                    mechaInitVar5_battle[e->m3_lightIndex].m8_localPosition.vx = e->mA_x;
                    mechaInitVar5_battle[e->m3_lightIndex].m8_localPosition.vy = e->mC_y;
                    mechaInitVar5_battle[e->m3_lightIndex].m8_localPosition.vz = e->mE_z;
                    mechaInitVar5_battle[e->m3_lightIndex].m6_isActive = e->m10_isActive;
                }
                pMecha->mA0_eventCursor += 0x12;
            } else {
                mechaInitVar5_battle[p->m3_index].m6_isActive = 0;
                pMecha->mA0_eventCursor += 6;
            }
            break;
        case 3:
        case 4: {
            sMechaAnimEventTrail* e = (sMechaAnimEventTrail*)p;

            /* A detached rig has no emitters. Retail can address low RAM here. */
            if (pMecha->m110_trails == NULL || e->m3_emitter >= pMecha->m10C_num110) {
                pMecha->mA0_eventCursor += e->m4_start ? 0x1C : 6;
                break;
            }
            stopMechaTrailEmitter_battle(&pMecha->m110_trails[e->m3_emitter], oddOrEven);
            if (e->m4_start != 0) {
                if (e->m3_emitter < pMecha->m10C_num110) {
                    initMechaTrailEmitter_battle(&pMecha->m110_trails[e->m3_emitter], &battleMechaInitVar3, e->m5_boneIndex, e->m2_type - 3,
                                  e->m6_maxCount, e->m7_lifetime, e->m8_colors[0], e->m8_colors[1], e->m8_colors[2], e->m8_colors[3], e->m8_colors[4],
                                  e->m8_colors[5], e->mE_edgePoints[0], e->mE_edgePoints[1], e->mE_edgePoints[2], e->mE_edgePoints[3], e->mE_edgePoints[4], e->mE_edgePoints[5], e->m1A_semiTrans);
                }
                pMecha->mA0_eventCursor += 0x1C;
            } else {
                pMecha->mA0_eventCursor += 6;
            }
            break;
        }
        case 5: {
            sMechaAnimEventSound* e = (sMechaAnimEventSound*)p;
            s32 bank;
            s32 bank8;
            s16 volume;

            if (isMechaEventAllowedForHitResult(setupMechaForEventSub0(), e->m4_mask)) {
                bank8 = 0;
                if (e->m7 == 1 && battleEnvMechaInitData->m35F_soundMode != 0) {
                    playSoundEffectInFreeSlot((*(u16*)(pMecha->mB0_sound->m8_seq + 0x14) << 16) | (battleEnvMechaInitData->m35F_soundMode + 10));
                }
                bank = getMechaSoundEffectIdBase_battle(pMecha, e->m5_bank);
                if (battleEnvMechaInitData->m35F_soundMode == 3 && e->m7 == 2) {
                    bank8 = 8;
                }
                playSoundEffectInFreeSlot(bank + e->m3_sound + bank8);
                if (e->m6_sound2 != 0) {
                    playSoundEffectInFreeSlot(bank + e->m6_sound2 + bank8);
                }
                volume = pMecha->m39_shadowShade;
                if (battleEnvMechaInitData->m35F_soundMode == 3 && e->m7 != 2) {
                    volume = volume * 60 / 107;
                }
                setSoundEffectVolume(bank + e->m3_sound + bank8, volume);
                if (e->m6_sound2 != 0) {
                    setSoundEffectVolume(bank + e->m6_sound2 + bank8, volume);
                }
            }
            pMecha->mA0_eventCursor += 8;
            break;
        }
        case 6:
            createAllDamageDisplaysIfAnimating();
            pMecha->mA0_eventCursor += 4;
            break;
        case 7:
            pMecha->m4_bones[p->m4].m7_isEnabled = p->m5 & 1;
            pMecha->mA0_eventCursor += 6;
            break;
        case 8: {
            sMechaAnimEventAnim* e = (sMechaAnimEventAnim*)p;
            s16 savedId = mechaAnimSelfIndex;
            u16 savedMask = mechaPlayAnimationVar0_battle;
            s32 useOther;
            s32 anim;

            for (i = 0; i < 13; i++) {
                if ((pMecha->m10A_targetMask >> i) & 1) {
                    useOther = e->m4_useOther;
                    anim = -1;
                    if (battleMechas[i] != NULL && (battleMechas[i]->m4A_flags & 2)) {
                        useOther = 0;
                    }
                    switch (battleCurrentDamages[allocateJumpAnimationStructVar0 - 1].m18_damageType[i]) {
                    case 0:
                    case 1:
                        if (battleMechas[i] != NULL && (battleMechas[i]->m4A_flags & 2)) {
                            anim = e->m5_anim[1];
                        } else {
                            anim = e->m5_anim[0];
                        }
                        break;
                    case 5:
                        anim = e->m5_anim[2];
                        break;
                    case 4:
                        anim = e->m5_anim[3];
                        break;
                    case 2:
                    case 3:
                        anim = e->m5_anim[4];
                        break;
                    }
                    if (!isMechaEventAllowedForHitResult(i, e->m3_mask)) {
                        anim = -1;
                    }
                    if (battleMechas[i] != NULL && anim > 0) {
                        if (useOther) {
                            mechaPlayAnimationFromSource_battle(pMecha, i, 1 << savedId, anim);
                        } else {
                            mechaPlayAnimationPreserveState_battle(i, 1 << savedId, anim);
                        }
                    }
                }
            }
            mechaAnimSelfIndex = savedId;
            mechaPlayAnimationVar0_battle = savedMask;
            pMecha->mA0_eventCursor += 0xA;
            break;
        }
        case 9:
            if (p->m4 != 0) {
                if (p->m3_index < pMecha->m10E_num118) {
                    if (p->m5 != 0xFF && p->m5 < pMecha->m10E_num118) {
                        pParent = &pMecha->m118_colorAnims[p->m5];
                    } else {
                        pParent = NULL;
                    }
                    if ((p->m6_tileMode & 0x7F) < 4) {
                        pColor = NULL;
                    } else {
                        pColor = battle800d2fc0;
                    }
                    t = getEnvTileStepFunction_battle(p->m7_stepFunctionId);
                    x0 = p->m8_x;
                    y0 = p->mA_y;
                    x1 = p->mC_blendSource0;
                    y1 = p->mE_blendSource1;
                    z1 = p->m10_blendSource2;
                    if (p->m6_tileMode & 0x80) {
                        if (pMecha->m90_vramCoords[0] < 0) {
                            continue;
                        }
                        x0 += pMecha->m90_vramCoords[2];
                        y0 += pMecha->m96_clutY;
                        if ((p->m12_sourceFlags >> 4) == 1) {
                            x1 += pMecha->m90_vramCoords[2];
                            y1 += pMecha->m96_clutY;
                        }
                    }
                    initAnimatedEnvTextureTile_battle(&pMecha->m118_colorAnims[p->m3_index], pParent, p->m6_tileMode & 0x7F, p->m12_sourceFlags | 0x700,
                                  (sLoadedMechas_118_1C*)pColor, x0, y0, 0, x1, y1, z1, x0, y0, p->m13_width, p->m14_height, p->m16_speed,
                                  p->m18_stepDivisor, p->m1A_stepBase, t);
                }
                pMecha->mA0_eventCursor += 0x1C;
            } else {
                freeAnimatedEnvTextureTile_battle(&pMecha->m118_colorAnims[p->m3_index]);
                pMecha->mA0_eventCursor += 6;
            }
            break;
        }
    }
    pMecha->m98_eventFrame++;
    if (pMecha->m9A_eventLoopLength >= 0 && pMecha->m98_eventFrame >= pMecha->m9A_eventLoopLength) {
        pMecha->m98_eventFrame = 0;
        pMecha->m9C_eventIndex = 0;
        pMecha->mA0_eventCursor = pMecha->mA4_eventStream;
    }
}

/* mechaOP_19] (twin 801E632C) */
void mechaOP_19_battle(sLoadedMechas* pMecha) {
    pMecha->m98_eventFrame = -1;
}

/* computeMechaDistanceToTarget] (twin 801E6338) */
s32 computeMechaDistanceToTarget_battle(sLoadedMechas* pMecha) {
    sMechaBone* bones = pMecha->m4_bones;
    s32 dx = pMecha->m88_targetPosition[0] - bones->m5C_translation[0];
    s32 dy = pMecha->m88_targetPosition[1] - bones->m5C_translation[1];
    s32 dz = pMecha->m88_targetPosition[2] - bones->m5C_translation[2];

    return SquareRoot0(dx * dx + dy * dy + dz * dz);
}

/* target position from a mecha bone, else battleVisualEntities (801E63A8 + else) */
void updateMechaTargetPosition_battle(sLoadedMechas* pMecha) {
    VECTOR out;
    s32 unused; /* reserves the retail stack slot */
    s32 id;
    sLoadedMechas* pTarget;
    MATRIX* m;
    sMechaInitVar2Sub* pTrack;

    id = 0;
    if (pMecha->m58_targetMecha == 0xFF) {
        for (id = 0; id < 13; id++) {
            if ((pMecha->m10A_targetMask >> id) & 1) {
                break;
            }
        }
    }
    if (pMecha->m58_targetMecha == 0xFE) {
        id = mechaAnimSelfIndex;
    }
    if (pMecha->m58_targetMecha == 0xFD) {
        id = pMecha->m20_mechaEntryId;
    }
    if (pMecha->m58_targetMecha == 0xFC) {
        id = pMecha->m21_sourceEntryId;
    }
    if (pMecha->m58_targetMecha == 0xFA) {
        id = 0x1F;
    }
    if (pMecha->m58_targetMecha > 0 && pMecha->m58_targetMecha < 0x80) {
        id = pMecha->m58_targetMecha - 1;
    }
    pTarget = battleMechas[id];
    if (pTarget != NULL && id != pMecha->m20_mechaEntryId) {
        if (pMecha->m5A_targetBone != 0) {
            m = (MATRIX*)0x1F800000;
            CompMatrix(&pTarget->m4_bones->mC_localMatrix, &pTarget->m4_bones[pMecha->m5A_targetBone].m2C_boneFinalMatrix, m);
        } else {
            m = &pTarget->m4_bones->mC_localMatrix;
        }
        SetRotMatrix(m);
        SetTransMatrix(m);
        gte_ldv0(&pMecha->m64_targetOffsetX);
        gte_rt();
        gte_stlvnl(&out);
        pMecha->m88_targetPosition[0] = out.vx;
        pMecha->m88_targetPosition[1] = out.vy;
        pMecha->m88_targetPosition[2] = out.vz;
        pTrack = pMecha->m4_bones->m70_animTracks[0];
        if (pTrack != NULL && (pTrack->m2_type == 7 || pTrack->m2_type == 8)) {
            pTrack->m4.v.mA_x = out.vx;
            pTrack->m4.v.mC_y = out.vy;
            pTrack->m4.v.mE_z = out.vz;
        }
    } else {
        pMecha->m88_targetPosition[0] = battleVisualEntities[id].mA_X;
        pMecha->m88_targetPosition[1] = battleVisualEntities[id].mE_Y;
        pMecha->m88_targetPosition[2] = battleVisualEntities[id].mC_Z;
    }
}

/* hide bone subtree, release tracks (twin 801E6578) */
void detachMechaBoneSubtree_battle(sMechaInitVar2* pool, s32 index, sMechaBone* bones, sMechaBone* bones2) {
    sMechaBone* p = bones;
    s32 n = bones->mA_numBones;
    s32 i;

    bones[index].m7_isEnabled = 0;
    bones2[index].m7_isEnabled = 1;
    releaseMechaAnimTrack_battle(pool, bones[index].m70_animTracks[0]);
    bones[index].m70_animTracks[0] = NULL;
    releaseMechaAnimTrack_battle(pool, bones[index].m70_animTracks[1]);
    bones[index].m70_animTracks[1] = NULL;
    releaseMechaAnimTrack_battle(pool, bones[index].m70_animTracks[2]);
    bones[index].m70_animTracks[2] = NULL;
    for (i = 1; i < n; i++) {
        p++;
        if (p->m0_parentBone == &bones[index]) {
            detachMechaBoneSubtree_battle(pool, p->mA_numBones, bones, bones2);
        }
    }
}

/* move enabled flags between bone sets (twin 801E6668) */
void reattachMechaBones_battle(sMechaBone* bones, sMechaBone* bones2) {
    s32 n = bones->mA_numBones;
    s32 i;

    for (i = 1; i < n; i++) {
        bones++;
        bones2++;
        if (bones->m7_isEnabled) {
            bones->m7_isEnabled = 0;
            bones2->m7_isEnabled = 1;
        }
    }
}

/* signed angle via OuterProduct12 (twin 801E66BC) */
s16 projectOnPlaneNormalScaled_battle(VECTOR* pDir, VECTOR* a, VECTOR* b, s32 scale) {
    VECTOR v;
    s32 dot;
    s32 len;

    OuterProduct12(a, b, &v);
    dot = v.vx * pDir->vx + v.vy * pDir->vy + v.vz * pDir->vz;
    len = SquareRoot0(v.vx * v.vx + v.vy * v.vy + v.vz * v.vz) + 1;
    dot = (dot << 4) / len;
    return (dot << 8) / scale;
}

/* lowest set bit of mechaPlayAnimationVar0_battle, 13 slots (801E67F8) */
s32 setupMechaForEventSub0(void) {
    s32 i;

    for (i = 0; i < 13; i++) {
        if ((mechaPlayAnimationVar0_battle >> i) & 1) {
            break;
        }
    }
    return i;
}

/* battleGetMechaBitfieldForAnim] (801E6830, ids 0x1F/13/14) */
u8 getMechaBitfieldForAnim_battle(sLoadedMechas* pMecha, u8 id, u16* pMask) {
    if (id == 0xFF) {
        id = setupMechaForEventSub0();
    } else if (id == 0xFE) {
        id = mechaAnimSelfIndex;
    } else if (id == 0xFD || id == 0xF9) {
        id = pMecha->m20_mechaEntryId;
    } else if (id == 0xFC) {
        id = pMecha->m21_sourceEntryId;
    } else if (id == 0xFA) {
        id = 0x1F;
    } else if (id == 0xF8) {
        id = pMecha->m20_mechaEntryId * 2 + 13;
    } else if (id == 0xF7) {
        id = pMecha->m20_mechaEntryId * 2 + 14;
    }
    *pMask = 1 << id;
    return id;
}

/* mechaGetKeyframe variant (801E6910 + id 0xFF from getGearAnimStatusBits status bits) */
sMechaDataTable2_4_4* mechaGetKeyframe_battle(sLoadedMechas* pMecha, u8 id, s32* pFlag) {
    s32 r;

    *pFlag = 0;
    if (id >= 0xFE) {
        if (id == 0xFF) {
            r = getGearAnimStatusBits(pMecha->m20_mechaEntryId);
            if ((r & 4) && (pMecha->m4A_flags & 0x100)) {
                pMecha->m2A_animQueue[0] = 0x1B;
            } else if ((r & 2) && (pMecha->m4A_flags & 0x80)) {
                if (!(battleEntities.m0_entities[pMecha->m20_mechaEntryId].m0_base.m36_monsterFlags2 & 1)) {
                    pMecha->m2A_animQueue[0] = 6;
                }
            } else if (!(pMecha->m4A_flags & 0x400)) {
                pMecha->m2A_animQueue[0] = 1;
            }
            if (r & 1) {
                pMecha->m2A_animQueue[0] |= 0x80;
            }
        }
        id = pMecha->m2A_animQueue[0] & 0x7F;
        *pFlag = pMecha->m2A_animQueue[0] & 0x80;
    }
    if (id < 0x40) {
        return pMecha->m14_keyframes->m4_keyframes[id];
    }
    return (pMecha->m18_extraKeyframes->m4_keyframes - 0x40)[id];
}

/* anim-track variant of mechaOp_62 (twin 801E6974) */
void setupBoneInterpolationTrack_battle(sLoadedMechas* pMecha, sMechaInitVar2* pool, sMechaBone* pBone, u8 flags, u8 mode, u8 tag,
                   u8 loop, s16 x, s16 y, s16 z, s16 tx, s16 ty, s16 tz, s16 duration) {
    sMechaInitVar2Sub* pTrack;
    s16 bx, by, bz;
    s16 cx, cy, cz;
    u8 kind;
    sMechaBone* p;
    s32 i;

    kind = flags & 7;
    if (kind == 0) {
        pTrack = pBone->m70_animTracks[0];
    } else if (kind == 1) {
        pTrack = pBone->m70_animTracks[1];
    } else {
        pTrack = pBone->m70_animTracks[2];
    }
    if (pTrack == NULL) {
        pTrack = allocateAnimTrack_battle(pool);
    }
    if (pTrack != NULL) {
        pTrack->m0_inUse = 1;
        pTrack->m1_isLooping = loop;
        pTrack->m2_type = mode + 3;
        pTrack->m3_tag = tag;
        if (flags & 0x20) {
            if (kind == 0) {
                bx = pBone->m54_rotationAngles.vx;
                by = pBone->m54_rotationAngles.vy;
                bz = pBone->m54_rotationAngles.vz;
            } else if (kind == 1) {
                bx = pBone->m5C_translation[0];
                by = pBone->m5C_translation[1];
                bz = pBone->m5C_translation[2];
            } else {
                bx = pBone->m4C_scale[0];
                by = pBone->m4C_scale[1];
                bz = pBone->m4C_scale[2];
            }
        } else {
            bx = 0;
            by = 0;
            bz = 0;
        }
        if (flags & 0x40) {
            if (kind == 0) {
                cx = pBone->m54_rotationAngles.vx;
                cy = pBone->m54_rotationAngles.vy;
                cz = pBone->m54_rotationAngles.vz;
            } else if (kind == 1) {
                cx = pBone->m5C_translation[0];
                cy = pBone->m5C_translation[1];
                cz = pBone->m5C_translation[2];
            } else {
                cx = pBone->m4C_scale[0];
                cy = pBone->m4C_scale[1];
                cz = pBone->m4C_scale[2];
            }
        } else {
            cx = 0;
            cy = 0;
            cz = 0;
        }
        pTrack->m4.v.m4_x = x + bx;
        pTrack->m4.v.m6_y = y + by;
        pTrack->m4.v.m8_z = z + bz;
        if (mode == 0) {
            pTrack->m4.v.mA_x = tx + cx - pTrack->m4.v.m4_x;
            pTrack->m4.v.mC_y = ty + cy - pTrack->m4.v.m6_y;
            pTrack->m4.v.mE_z = tz + cz - pTrack->m4.v.m8_z;
        } else {
            pTrack->m4.v.mA_x = tx + cx;
            pTrack->m4.v.mC_y = ty + cy;
            pTrack->m4.v.mE_z = tz + cz;
        }
        pTrack->m10_currentTime = 0;
        pTrack->m12_maxTime = duration;
        if (kind == 0) {
            if (mode < 2) {
                pBone->m54_rotationAngles.vx = pTrack->m4.v.m4_x;
                pBone->m54_rotationAngles.vy = pTrack->m4.v.m6_y;
                pBone->m54_rotationAngles.vz = pTrack->m4.v.m8_z;
            }
            pBone->m70_animTracks[0] = pTrack;
        } else if (kind == 1) {
            if (mode < 2) {
                pBone->m5C_translation[0] = pTrack->m4.v.m4_x;
                pBone->m5C_translation[1] = pTrack->m4.v.m6_y;
                pBone->m5C_translation[2] = pTrack->m4.v.m8_z;
            }
            pBone->m70_animTracks[1] = pTrack;
        } else {
            if (mode < 2) {
                pBone->m4C_scale[0] = pTrack->m4.v.m4_x;
                pBone->m4C_scale[1] = pTrack->m4.v.m6_y;
                pBone->m4C_scale[2] = pTrack->m4.v.m8_z;
            }
            pBone->m70_animTracks[2] = pTrack;
        }
    }
    if (flags & 0x80) {
        p = pMecha->m4_bones;
        for (i = 1; i < pMecha->m4_bones->mA_numBones; i++) {
            p++;
            if (p->m0_parentBone == pBone) {
                setupBoneInterpolationTrack_battle(pMecha, pool, p, flags, mode, tag, loop, x, y, z, tx, ty, tz, duration);
            }
        }
    }
}

/* setBoneVisibility] (twin 801E6D94) */
void setBoneVisibility_battle(sLoadedMechas* pMecha, sMechaBone* pBone, s32 flags) {
    sMechaBone* p;
    s32 i;

    pBone->m7_isEnabled = flags & 1;
    if (flags & 0x80) {
        p = pMecha->m4_bones;
        for (i = 1; i < pMecha->m4_bones->mA_numBones; i++) {
            p++;
            if (p->m0_parentBone == pBone) {
                setBoneVisibility_battle(pMecha, p, flags);
            }
        }
    }
}

/* spawn sprite effect on a mecha bone (twin 801E6E48) */
void spawnMechaBoneAttachedSprite_battle(SpriteVramData* vram, s32 animationId, s16* position, s16 direction, s16 scale,
                   sMechaBoneAttachmentConfig* pConfig, sLoadedMechas* pMecha) {
    SpriteTask* task;
    SpriteActorCore* actor;
    sMechaBoneAttachment* pAttach;

    task = spawnEffectSprite(animationId, vram, position, PC_HEAP_SIZED(sizeof(sMechaBoneAttachment), 0x18));
    actor = &task->m38_actor;
    spriteActorSetDirection(actor, direction);
    setSpriteActorAngle(actor, direction);
    setGraphicEntityScale(actor, scale);
    pAttach = (sMechaBoneAttachment*)((u8*)task + (s16)task->m38_actor.m86_thisSize);
    pAttach->m8_mecha = pMecha;
    pAttach->mC_boneIndex = pConfig->m5_boneIndex;
    if (pConfig->m13_attach) {
        pAttach->m4_updateCallback = getTaskUpdateFunction(&task->m0_updateTask);
        setTaskUpdateFunction(&task->m0_updateTask, (void (*)(TaskHeader*))updateMechaBoneAttachedSprite_battle);
        pAttach->m10_offset.vx = pConfig->m6_offsetX;
        pAttach->m10_offset.vy = pConfig->m8_offsetY;
        pAttach->m10_offset.vz = pConfig->mA_offsetZ;
        pAttach->mE_snapToMechaAltitude = pConfig->mC_snapToMechaAltitude;
    }
}

/* its draw callback, GTE macros (twin 801E6F64) */
void updateMechaBoneAttachedSprite_battle(SpriteTask* task) {
    VECTOR out;
    sMechaBoneAttachment* pAttach;
    MATRIX* m;
    s32 pose;

    pAttach = (sMechaBoneAttachment*)((u8*)task + (s16)task->m38_actor.m86_thisSize);
    /* Enhanced, 60 Hz battles (xg-hbc.19.2): on a step frame the Gear is drawn
     * halfway through its program step (gearpose.c, built by renderMechasBattle
     * before the sprite tasks run), and an effect anchored to one of its bones
     * is placed from that pose, so it keeps to the drawn Gear; the game sets
     * this position anew every vblank from the bones, nothing reads it across
     * frames. The bones themselves are the game's. */
    pose = pcGameplayBattle60() && pAttach->m8_mecha->m4_bones != NULL ? pcGearPoseFor(pAttach->m8_mecha) : -1;
    if (pAttach->m8_mecha->m4_bones == NULL) {
        /* The mecha's bones are gone (playBattleEndAndReleaseEnemies released it at the end of
         * an event battle, selector -1/3) while this effect still runs: the PS1 reads the local
         * matrix through NULL from low RAM, zero at 0x0C-0x2B, so whatever the bone's matrix the
         * composed one is zero and the sprite goes to the origin. */
        static MATRIX zero;
        m = &zero;
    } else
    if (pAttach->mC_boneIndex != 0) {
        m = (MATRIX*)0x1F800000;
        CompMatrix(pcGearPoseRoot(pose, &pAttach->m8_mecha->m4_bones->mC_localMatrix),
                   pcGearPoseFinal(pose, pAttach->mC_boneIndex, &pAttach->m8_mecha->m4_bones[pAttach->mC_boneIndex].m2C_boneFinalMatrix), m);
    } else {
        m = pcGearPoseRoot(pose, &pAttach->m8_mecha->m4_bones->mC_localMatrix);
    }
    SetRotMatrix(m);
    SetTransMatrix(m);
    gte_ldv0(&pAttach->m10_offset);
    gte_rt();
    gte_stlvnl(&out);
    if (pAttach->mE_snapToMechaAltitude != 0) {
        out.vy = pAttach->m8_mecha->m60_groundY;
    }
    task->m38_actor.m0_position.vx = out.vx << 16;
    task->m38_actor.m0_position.vy = out.vy << 16;
    task->m38_actor.m0_position.vz = out.vz << 16;
    pAttach->m4_updateCallback(task);
}

/* mechaOp_62] (twin 801E7094) */
void mechaOp_62_battle(sLoadedMechas* pMecha, sMechaBone* pBone, u8 flags, s16 x, s16 y, s16 z) {
    sMechaBone* p;
    s32 i;

    if ((flags & 7) == 0) {
        if (flags & 0x20) {
            pBone->m54_rotationAngles.vx = x + pBone->m54_rotationAngles.vx;
            pBone->m54_rotationAngles.vy = y + pBone->m54_rotationAngles.vy;
            pBone->m54_rotationAngles.vz = z + pBone->m54_rotationAngles.vz;
        } else {
            pBone->m54_rotationAngles.vx = x;
            pBone->m54_rotationAngles.vy = y;
            pBone->m54_rotationAngles.vz = z;
        }
    } else if ((flags & 7) == 1) {
        if (flags & 0x20) {
            pBone->m5C_translation[0] = x + pBone->m5C_translation[0];
            pBone->m5C_translation[1] = y + pBone->m5C_translation[1];
            pBone->m5C_translation[2] = z + pBone->m5C_translation[2];
        } else {
            pBone->m5C_translation[0] = x;
            pBone->m5C_translation[1] = y;
            pBone->m5C_translation[2] = z;
        }
    } else if (flags & 0x20) {
        pBone->m4C_scale[0] = x + pBone->m4C_scale[0];
        pBone->m4C_scale[1] = y + pBone->m4C_scale[1];
        pBone->m4C_scale[2] = z + pBone->m4C_scale[2];
    } else {
        pBone->m4C_scale[0] = x;
        pBone->m4C_scale[1] = y;
        pBone->m4C_scale[2] = z;
    }
    pBone->m4_needTranslationUpdate = 1;
    pBone->m5_needRotationUpdate = 1;
    if (flags & 0x80) {
        p = pMecha->m4_bones;
        for (i = 1; i < pMecha->m4_bones->mA_numBones; i++) {
            p++;
            if (p->m0_parentBone == pBone) {
                mechaOp_62_battle(pMecha, p, flags, x, y, z);
            }
        }
    }
}

/* snapMechaBoneToBattleTerrain] */
void snapMechaBoneToBattleTerrain(sLoadedMechas* pMecha) {
    u8 normal[16];
    SVECTOR pos;

    pos.vx = pMecha->m4_bones->m5C_translation[0];
    pos.vy = pMecha->m4_bones->m5C_translation[1];
    pos.vz = pMecha->m4_bones->m5C_translation[2];
    pMecha->m1E_terrainTriangle = findBattleTerrainTriangle(&pos, pMecha->m1E_terrainTriangle, 4);
    if (pMecha->m1E_terrainTriangle < 0) {
        pMecha->m1E_terrainTriangle = findBattleTerrainTriangleBruteForce(&pos);
    }
    computeTerrainHeightAtTriangle(&pos, pMecha->m1E_terrainTriangle, normal);
    pMecha->m60_groundY = pos.vy;
    if (pMecha->m36_disableGroundSnap == 0) {
        pMecha->m4_bones->m5C_translation[1] = pos.vy;
    }
}

/* free mecha sequence/data (mC, m18; unload mB4 seq) */
void freeMechaExtraData(sLoadedMechas* pMecha) {
    if (pMecha->mC_extraAnim != NULL) {
        if (pMecha->m63_isSeq2Loaded != 0) {
            unloadSequence(pMecha->mB4_extraSound->m8_seq);
            pMecha->m63_isSeq2Loaded = 0;
        }
        if (pMecha->m18_extraKeyframes != NULL) {
            pcFreeMechaData2((sMechaDataTable2*)pMecha->mC_extraAnim); /* converted: tables and file */
        } else {
            freeHeap(pMecha->mC_extraAnim); /* still the file as loaded */
        }
        pMecha->mC_extraAnim = NULL;
        pMecha->m18_extraKeyframes = NULL;
    }
}

/* initBattle800B00D0] clear mechaEventAnimTracks[9] */
void clearMechaEventAnimTracks(void) {
    s32 i;

    for (i = 8; i >= 0; i--) {
        mechaEventAnimTracks[i] = NULL;
    }
}

/* release mechaEventAnimTracks[9] tracks, then clear */
void releaseMechaEventAnimTracks(sMechaInitVar2* pool) {
    s32 i;

    for (i = 0; i < 9; i++) {
        if (mechaEventAnimTracks[i] != NULL) {
            releaseMechaAnimTrack_battle(pool, mechaEventAnimTracks[i]);
        }
    }
    clearMechaEventAnimTracks();
}

/* Starts event-animation track `index` (mechaEventAnimTracks[index], allocated from pool
 * on first use) unless battleCameraVar0 is set. */
void startMechaEventAnimTrack(sMechaInitVar2* pool, s32 index, u8 type, u8 tag, s16 x, s16 y, s16 z, s16 dx, s16 dy,
                   s16 dz, s16 duration) {
    sMechaInitVar2Sub* pTrack;

    if (battleCameraVar0 == 0) {
        if (mechaEventAnimTracks[index] == NULL) {
            mechaEventAnimTracks[index] = allocateAnimTrack_battle(pool);
        }
        pTrack = mechaEventAnimTracks[index];
        if (pTrack != NULL) {
            pTrack->m0_inUse = 1;
            pTrack->m1_isLooping = 0;
            pTrack->m2_type = type;
            pTrack->m3_tag = tag;
            pTrack->m4.v.m4_x = x;
            pTrack->m4.v.m6_y = y;
            pTrack->m4.v.m8_z = z;
            pTrack->m4.v.mA_x = dx;
            pTrack->m4.v.mC_y = dy;
            pTrack->m4.v.mE_z = dz;
            pTrack->m10_currentTime = 0;
            pTrack->m12_maxTime = duration;
        }
    }
}

/* Noah: updateMechaEventAnim. Runs the nine battle camera event tracks
 * (mechaEventAnimTracks) `iterations` times: tracks 0-6 animate camera parameters,
 * 7 the camera target and 8 the eye (orbiting the target, kept inside the
 * arena and above the ground). */
void updateMechaEventAnim(sMechaInitVar2* pool, s32 iterations, s32 arg2, s32 id) {
    s32 n;
    s32 slot;
    sMechaInitVar2Sub* pTrack;
    sMechaInitVar2Sub* p2;
    sMechaInitVar2Sub* p;
    s16 x;
    s16 y;
    s16 z;
    s16 outX;
    s16 outY;
    s16 outZ;
    u8 type;
    s32 target;
    s32 h;
    s32 d;
    s16 eyeY;
    s16 ground;
    SVECTOR pos;

    mechaEventAnimWaitState = 0;
    for (n = 0; n < iterations; n++) {
        for (slot = 0; slot < 9; slot++) {
            p = mechaEventAnimTracks[slot];
            if (p == NULL) {
                continue;
            }
            pTrack = p;
            p2 = pTrack;
            type = pTrack->m2_type;
            if ((type & 0xF) < 2) {
                target = pTrack->m4.v.mA_x;
                if (battleMechas[target] != NULL) {
                    x = battleMechas[target]->m4_bones->m5C_translation[0];
                    y = battleMechas[target]->m4_bones->m5C_translation[1] - pTrack->m4.v.mC_y;
                    z = battleMechas[target]->m4_bones->m5C_translation[2];
                } else {
                    x = battleVisualEntities[target].mA_X;
                    y = battleVisualEntities[target].mE_Y - pTrack->m4.v.mC_y;
                    z = battleVisualEntities[target].mC_Z;
                }
                target = p2->m4.v.mE_z;
                if (target >= 0) {
                    if (battleMechas[target] != NULL) {
                        x = (x + battleMechas[target]->m4_bones->m5C_translation[0]) / 2;
                        y = (y + battleMechas[target]->m4_bones->m5C_translation[1] - p2->m4.v.mC_y) / 2;
                        z = (z + battleMechas[target]->m4_bones->m5C_translation[2]) / 2;
                    } else {
                        x = (x + battleVisualEntities[target].mA_X) / 2;
                        y = (y + battleVisualEntities[target].mE_Y - p2->m4.v.mC_y) / 2;
                        z = (z + battleVisualEntities[target].mC_Z) / 2;
                    }
                }
            } else {
                x = pTrack->m4.v.mA_x;
                y = pTrack->m4.v.mC_y;
                z = pTrack->m4.v.mE_z;
            }
            if (slot == 7) {
                y -= mechaEventTargetHeight;
                x += -mechaEventTargetDistance * getAngleCos(mechaEventTargetYaw) / 4096;
                z += -mechaEventTargetDistance * getAngleSin(mechaEventTargetYaw) / 4096;
            }
            if (slot == 8) {
                h = mechaEventCameraDistance * getAngleCos(mechaEventCameraPitch) / 4096;
                d = mechaEventCameraDistance * getAngleSin(mechaEventCameraPitch) / 4096;
                x += -d * getAngleCos(mechaEventCameraYaw) / 4096;
                eyeY = y + (-h - mechaEventCameraHeight);
                y = eyeY;
                z += -d * getAngleSin(mechaEventCameraYaw) / 4096;
                pos.vx = x;
                pos.vy = y;
                pos.vz = z;
                if (stepTowardWithMechaCollision(&battleCameraAt, &pos) != 0) {
                    x = pos.vx;
                    z = pos.vz;
                }
                ground = getBattleCameraGroundHeight(id);
                if (eyeY > ground) {
                    y = ground;
                }
            }
            if (mechaEventAnimSnapCamera != 0 && slot >= 7) {
                p2->m4.v.m4_x = x;
                p2->m4.v.m6_y = y;
                p2->m4.v.m8_z = z;
            }
            switch (type & 1) {
            case 0: {
                s16 ratio = pTrack->m10_currentTime + 1;

                outX = p2->m4.v.m4_x + (x - p2->m4.v.m4_x) * ratio / pTrack->m12_maxTime;
                outY = p2->m4.v.m6_y + (y - p2->m4.v.m6_y) * ratio / pTrack->m12_maxTime;
                outZ = p2->m4.v.m8_z + (z - p2->m4.v.m8_z) * ratio / pTrack->m12_maxTime;
                break;
            }
            case 1: {
                s16 maxTime = pTrack->m12_maxTime;
                s16 dx = (x - p2->m4.v.m4_x) / maxTime;
                s16 dy = (y - p2->m4.v.m6_y) / maxTime;
                s16 dz = (z - p2->m4.v.m8_z) / maxTime;

                outX = p2->m4.v.m4_x;
                outY = p2->m4.v.m6_y;
                outZ = p2->m4.v.m8_z;
                if (dx == 0 && dy == 0 && dz == 0) {
                    pTrack->m10_currentTime = maxTime;
                } else {
                    outX += dx;
                    outY += dy;
                    outZ += dz;
                    pTrack->m10_currentTime = 0;
                }
                p2->m4.v.m4_x = outX;
                p2->m4.v.m6_y = outY;
                p2->m4.v.m8_z = outZ;
                break;
            }
            }
            pTrack->m10_currentTime++;
            if (pTrack->m10_currentTime >= pTrack->m12_maxTime) {
                if (slot >= 7) {
                    pTrack->m10_currentTime = pTrack->m12_maxTime - 1;
                } else {
                    if (pTrack->m3_tag == mechaEventAnimWaitTag) {
                        mechaEventAnimWaitState |= 2;
                    }
                    releaseMechaAnimTrack_battle(pool, pTrack);
                    mechaEventAnimTracks[slot] = NULL;
                }
            } else if (pTrack->m3_tag == mechaEventAnimWaitTag) {
                mechaEventAnimWaitState |= 1;
            }
            switch (slot) {
            case 0:
                mechaEventCameraYaw = outX;
                break;
            case 1:
                mechaEventTargetYaw = outX;
                break;
            case 2:
                mechaEventCameraPitch = outX;
                break;
            case 3:
                mechaEventCameraDistance = outX;
                break;
            case 4:
                mechaEventTargetDistance = outX;
                break;
            case 5:
                mechaEventTargetHeight = outX;
                break;
            case 6:
                mechaEventCameraHeight = outX;
                break;
            case 7:
                battleCameraAt.vx = outX;
                battleCameraAt.vy = outY;
                battleCameraAt.vz = outZ;
                break;
            case 8:
                battleCameraEye.vx = outX;
                battleCameraEye.vy = outY;
                battleCameraEye.vz = outZ;
                break;
            }
        }
    }
    mechaEventAnimSnapCamera = 0;
}

/* Noah-style name: isInsideBattleArena. 1 when pos lies strictly inside the
 * arena bounds of the battle config (m34C..m352). */
s16 isInsideBattleArena(SVECTOR* pos) {
    if (pos->vx > battleEnvMechaInitData->m34E_minX && pos->vx < battleEnvMechaInitData->m34C_maxX && pos->vz > battleEnvMechaInitData->m350_minZ &&
        pos->vz < battleEnvMechaInitData->m352_maxZ) {
        return 1;
    }
    return 0;
}

/* Ground height (terrain y) at a point 512 units from the camera eye toward
 * its target (y step / 4), for camera collision; keeps the last triangle in
 * battleCameraGroundTriangle and the last height in battleCameraGroundHeightCache; id changes force a search. */
s16 getBattleCameraGroundHeight(s32 id) {
    VECTOR normal;
    SVECTOR pos;
    s16 dx;
    s32 dy;
    s16 dz;
    s32 len;
    s16 tri;

    dx = battleCameraAt.vx - battleCameraEye.vx;
    dy = (battleCameraAt.vy - battleCameraEye.vy) / 4;
    dz = battleCameraAt.vz - battleCameraEye.vz;
    len = SquareRoot0(dx * dx + dy * dy + dz * dz) + 1;
    tri = -1;
    pos.vx = battleCameraEye.vx + (dx << 9) / len;
    pos.vy = battleCameraEye.vy + (dy << 9) / len;
    pos.vz = battleCameraEye.vz + (dz << 9) / len;
    if (isInsideBattleArena(&pos) && id != battleCameraGroundQueryId) {
        battleCameraGroundQueryId = id;
        tri = findBattleTerrainTriangle(&pos, battleCameraGroundTriangle, 5);
        if (tri < 0) {
            tri = findBattleTerrainTriangleBruteForce(&pos);
            if (tri < 0) {
                goto fail;
            }
        }
        battleCameraGroundTriangle = tri;
    }
    if (tri >= 0) {
        computeTerrainHeightAtTriangle(&pos, battleCameraGroundTriangle, &normal);
        battleCameraGroundHeightCache = pos.vy;
    } else {
    fail:
        pos.vy = battleCameraGroundHeightCache;
    }
    return pos.vy;
}

/* Pushes pos (x/z) out of the nearest-radius mecha collider it overlaps
 * after a step of dir: each visible mecha i < 11 whose top (translation y -
 * initMechaForBattleSub0_battle) is above pos has a circle of radius getMechaColliderRadius_battle(i) + 0x80
 * around its position minus dir / 6; the largest overlapping circle wins and
 * pos is moved onto its edge. Returns the mecha index + 1, or 0. */
s32 pushOutOfMechaColliders(SVECTOR* dir, SVECTOR* pos) {
    s16 best;
    s32 i;
    s16 r;
    s16 bestR;
    s16 outX;
    s16 outZ;
    s16 cx;
    s16 cz;
    s16 dx;
    s16 dz;
    s32 d;
    sMechaBone* pBone;

    best = -1;
    for (i = 0; i < 11; i++) {
        r = getMechaColliderRadius_battle(i) + 0x80;
        if (battleMechas[i] != NULL && battleMechas[i]->m34_visible != 0 &&
            pos->vy > battleMechas[i]->m4_bones->m5C_translation[1] - initMechaForBattleSub0_battle(i) && (best < 0 || r > bestR)) {
            pBone = battleMechas[i]->m4_bones;
            cx = pBone->m5C_translation[0] - dir->vx / 6;
            dx = pos->vx - cx;
            cz = pBone->m5C_translation[2] - dir->vz / 6;
            dz = pos->vz - cz;
            d = SquareRoot0(dx * dx + dz * dz) + 1;
            if (d < r) {
                best = i;
                bestR = r;
                outX = cx + dx * r / d;
                outZ = cz + dz * r / d;
            }
        }
    }
    if (best >= 0) {
        pos->vx = outX;
        pos->vz = outZ;
        return best + 1;
    }
    return 0;
}

/* Moves pos toward target (x/z only): the step is the unit direction scaled
 * to 512 and resolved against the mecha colliders by pushOutOfMechaColliders (its result: the
 * mecha hit + 1, or 0). */
s32 stepTowardWithMechaCollision(SVECTOR* target, SVECTOR* pos) {
    SVECTOR dir;
    s32 len;

    dir.vx = target->vx - pos->vx;
    dir.vz = target->vz - pos->vz;
    len = SquareRoot0(dir.vx * dir.vx + dir.vz * dir.vz) + 1;
    dir.vx = (dir.vx << 9) / len;
    dir.vz = (dir.vz << 9) / len;
    return pushOutOfMechaColliders(&dir, pos);
}

/* Noah: pushMechaAway. Pushes mecha mechaIdx away from (x, z) until the two
 * are minDist apart in x/z. */
void pushMechaAway(s32 mechaIdx, s32 x, s32 z, s32 minDist) {
    s32 dx;
    s32 dz;
    s32 dist;
    s32 angle;

    dx = x - battleMechas[mechaIdx]->m4_bones->m5C_translation[0];
    dz = z - battleMechas[mechaIdx]->m4_bones->m5C_translation[2];
    dist = SquareRoot0(dx * dx + dz * dz) + 1;
    if (dist < minDist) {
        angle = ratan2(dz, -dx);
        if ((u32)(((battleMechas[mechaIdx]->m4_bones->m54_rotationAngles.vy - angle) & 0xFFF) - 0x401) < 0x7FF) {
            angle += 0x800;
        }
        battleMechas[mechaIdx]->m4_bones->m5C_translation[0] =
            x - dx * minDist / dist - getAngleCos(angle) * (minDist - dist) / 4096;
        battleMechas[mechaIdx]->m4_bones->m5C_translation[2] =
            z - dz * minDist / dist - getAngleSin(angle) * (minDist - dist) / 4096;
    }
}

s32 isMechaEventAllowedForHitResult(s32 target, u8 mask) {
    s32 ret = 0;

    switch (battleCurrentDamages[allocateJumpAnimationStructVar0 - 1].m18_damageType[target]) {
    case 0:
    case 1:
        if (!(mask & 1)) {
            ret = 1;
        }
        break;
    case 5:
        if (!(mask & 2)) {
            ret = 1;
        }
        break;
    case 4:
        if (!(mask & 4)) {
            ret = 1;
        }
        break;
    case 2:
    case 3:
        if (!(mask & 8)) {
            ret = 1;
        }
        break;
    }
    if (mask == 0) {
        ret = 1;
    }
    return ret;
}

/* Waits until no mecha has a pending load (m38) and mecha_battle_op3 is idle,
 * then, if any mecha holds animation data (mC), waits for the CD and frees
 * it (freeMechaExtraData). */
void clearAnimationsEndOfTurnSub(void) {
    s32 i;
    s32 busy;
    s32 hasData;
    sLoadedMechas* p;

    hasData = 0;
    do {
        busy = 0;
        for (i = 0; i < 11; i++) {
            p = battleMechas[i];
            if (p != NULL) {
                if (p->m38_syncFlag != 0) {
                    busy = 1;
                }
                if (p->mC_extraAnim != NULL) {
                    hasData = 1;
                }
            }
        }
        if (mecha_battle_op3() != 0) {
            busy = 1;
        }
        if (busy != 0) {
            battleRender();
        }
    } while (busy != 0);
    if (hasData != 0) {
        stopCdRead(0);
        busy = 1;
        while (1) {
            if (isCDBusy() == 0) {
                for (i = 0; i < 11; i++) {
                    if (battleMechas[i] != NULL && battleMechas[i]->mC_extraAnim != NULL) {
                        freeMechaExtraData(battleMechas[i]);
                    }
                }
                busy = 0;
            }
            if (busy == 0) {
                break;
            }
            battleRender();
        }
    }
}

void setBattleMechaVar2(void) {
    battleMechaVar2 = 1;
}

/* Plays animation 5 on the first three mechas except `skip`, waits until
 * they finish (m34/m38 clear), then releases them (freeMecha_battle). */
void dismissPartyGears(s32 skip) {
    s32 i;
    s32 busy;

    for (i = 0; i < 3; i++) {
        if (i != skip) {
            initMechaAnimation_battle(battleMechas[i], battleMechas[i], &battleMechaInitVar2, 5);
        }
    }
    do {
        busy = 0;
        for (i = 0; i < 3; i++) {
            if (i != skip && battleMechas[i] != NULL &&
                (battleMechas[i]->m34_visible != 0 || battleMechas[i]->m38_syncFlag != 0)) {
                busy = 1;
            }
        }
        battleRender();
    } while (busy != 0);
    battleRender();
    for (i = 0; i < 3; i++) {
        if (i != skip) {
            freeMecha_battle(i);
        }
    }
}
