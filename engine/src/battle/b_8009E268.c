/*
 * Battle copy of the gear (mecha) library (Noah field/mecha/mechaOverlay.cpp),
 * battle terrain (battleTerrain.cpp) and the battle mecha glue (battle.cpp,
 * battleMechaLoadingTask.cpp). 0x8009E268-0x800B15D8; gcc-2.6.3-psx,
 * ASPSX 2.34; the divides keep their checks (--expand-div).
 */
#include "common.h"
#include "psyq.h"
#include "gte.h"
#include "stack.h"
#include "main/memory.h"
#include "main/filesystem.h"
#include "main/sprite.h"
#include "battle/battle.h"
#include "main/game_state.h"

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

void startGearAttackAnimationVar8_2(void) {
    startBattleAttackAnimationVar6->m99_defenceDown = 0;
}

void startGearAttackAnimationVar8_3(void) {
    u32 damage = (u32)(startBattleAttackAnimationVar6->m4F_frameRepairLevel * startBattleAttackAnimationVar6->m64_maxHp) / 10;

    pBattleEntities->m5FA0_initBattleAttackStatusArray0[startBattleAttackAnimationVar4] = 2;
    pBattleEntities->m5F6C_damageDonePerAttack[startBattleAttackAnimationVar4] = damage;
}

void startGearAttackAnimationVar8_4(void) {
    u32 damage = (u32)(currentEntityBattleStats->m11_power * startBattleAttackAnimationVar6->m64_maxHp) / 20;

    pBattleEntities->m5FA0_initBattleAttackStatusArray0[startBattleAttackAnimationVar4] = 2;
    pBattleEntities->m5F6C_damageDonePerAttack[startBattleAttackAnimationVar4] = damage;
}

void startGearAttackAnimationVar8_10(void) {
    u32 damage = battleGetSlotStatusSub_currentBattleEntity->m0_base.m5B_ether * currentEntityBattleStats->m11_power;

    pBattleEntities->m5FA0_initBattleAttackStatusArray0[startBattleAttackAnimationVar4] = 2;
    pBattleEntities->m5F6C_damageDonePerAttack[startBattleAttackAnimationVar4] = damage;
}

void startGearAttackAnimationVar8_5(void) {
    *startBattleAttackAnimationVar7 = 4;
    battleEntities.m0_entities[startBattleAttackAnimationVar4].m15C_statusTimers[6] = 3;
}

void startGearAttackAnimationVar8_6(void) {
    s32 damage = (startBattleAttackAnimationVar6->m3A_maxFuel * currentEntityBattleStats->m11_power) / 20;

    pBattleEntities->m5FA0_initBattleAttackStatusArray0[startBattleAttackAnimationVar4] = 10;
    pBattleEntities->m5F6C_damageDonePerAttack[startBattleAttackAnimationVar4] = damage;
}

void startGearAttackAnimationVar8_7(void) {
    s32 damage = (startBattleAttackAnimationVar6->m3A_maxFuel * currentEntityBattleStats->m11_power) / 20;

    pBattleEntities->m5FA0_initBattleAttackStatusArray0[startBattleAttackAnimationVar4] = 11;
    pBattleEntities->m5F6C_damageDonePerAttack[startBattleAttackAnimationVar4] = damage;
}

void startGearAttackAnimationVar8_8(void) {
    startBattleAttackAnimationVar6->m7C_statusFlags &= 0xF80B;
    startBattleAttackAnimationVar5->m0_base.m7A_commandEnabledBF &= 0xFFDF;
}

s32 isBillyGearAmmoLoaded(u8 arg0) {
    sBattleData5750* e = &pBattleEntities->m5B38_gearAmmoItems[arg0];

    if (gameState.m978_gears[gameState.m26C_party[4].mA0_gearNum].m12_slots[0].m3_weaponId == e->mF_id
        || gameState.m978_gears[gameState.m26C_party[4].mA0_gearNum].m12_slots[1].m3_weaponId == e->mF_id) {
        return 1;
    }
    return gameState.m978_gears[gameState.m26C_party[4].mA0_gearNum].m12_slots[2].m3_weaponId == e->mF_id;
}

/* (loop test needs (&arr[i])->, plain arr[i]. swaps one addu) */
void loadBillyGearAmmo(u8 item, u8 slot) {
    sBattleData5750* e = &pBattleEntities->m5750_gearItems[item];
    u8 g = gameState.m26C_party[4].mA0_gearNum;
    u8 i;

    if (gameState.m978_gears[g].m12_slots[0].m3_weaponId == e->mF_id) {
        slot = 0;
    }
    if (gameState.m978_gears[g].m12_slots[1].m3_weaponId == e->mF_id) {
        slot = 1;
    }
    if (gameState.m978_gears[g].m12_slots[2].m3_weaponId == e->mF_id) {
        slot = 3;
    }
    gameState.m978_gears[g].m12_slots[slot].m0_weaponPower = e->mE_power;
    gameState.m978_gears[g].m12_slots[slot].m2_weaponStatusType = e->m11_statusType;
    gameState.m978_gears[g].m12_slots[slot].m1_weaponStatusChance = e->m10_statusChance;
    gameState.m978_gears[g].m12_slots[slot].m2_weaponStatusType = e->m11_statusType;
    gameState.m978_gears[g].m4_slotItem[slot] = item;
    gameStateAmmoByWeaponItem[item] = e->mC_ammoCount;
    for (i = 0; i < 3; i++) {
        if ((&pBattleEntities->m0_entities[i])->m0_base.m56_battleCommandLoadout == 4) {
            pBattleEntities->m0_entities[i].mA4_gear.m12_slots[slot].m0_weaponPower = e->mE_power;
            pBattleEntities->m0_entities[i].mA4_gear.m12_slots[slot].m2_weaponStatusType = e->m11_statusType;
            pBattleEntities->m0_entities[i].mA4_gear.m12_slots[slot].m1_weaponStatusChance = e->m10_statusChance;
            pBattleEntities->m0_entities[i].mA4_gear.m12_slots[slot].m2_weaponStatusType = e->m11_statusType;
            pBattleEntities->m0_entities[i].mA4_gear.m4_slotItem[slot] = item;
        }
    }
}

/* (jtbl_800704CC; 'x -= 1' not 'x--') */
void spendBillyGearAmmo(void) {
    u8 idx;

    switch (pBattleEntities->m5FC2_battleGetSlotStatusSub_current28Index) {
    case 0:
        idx = battleGetSlotStatusSub_currentBattleEntityGear->m4_slotItem[0];
        if (gameStateAmmoByGearItem[idx] != 0) {
            gameStateAmmoByGearItem[idx] -= 1;
        }
        break;
    case 3:
    case 4:
    case 5:
    case 6:
    case 7:
    case 8:
    case 9:
    case 10:
    case 11:
    case 12:
    case 13:
    case 14:
        idx = battleGetSlotStatusSub_currentBattleEntityGear->m4_slotItem[0];
        if (gameStateAmmoByGearItem[idx] != 0) {
            gameStateAmmoByGearItem[idx] -= 1;
        }
        goto slot3;
    case 15:
        idx = battleGetSlotStatusSub_currentBattleEntityGear->m4_slotItem[0];
        if (gameStateAmmoByGearItem[idx] != 0) {
            gameStateAmmoByGearItem[idx] -= 1;
        }
        break;
    case 2:
    case 17:
    slot3:
        idx = battleGetSlotStatusSub_currentBattleEntityGear->m4_slotItem[3];
        if (gameStateAmmoByGearItem[idx] != 0) {
            gameStateAmmoByGearItem[idx] -= 1;
        }
        break;
    }
}

void setGearStatusEffectMessage(u8 arg0, u16 arg1) {
    switch (arg0) {
    case 0:
        switch (arg1) {
        case 0x400: pBattleEntities->m5FC7_battleTickMain_var1 = 0x24; break;
        case 0x200: pBattleEntities->m5FC7_battleTickMain_var1 = 0x25; break;
        case 0x100: pBattleEntities->m5FC7_battleTickMain_var1 = 0x26; break;
        case 0x80: pBattleEntities->m5FC7_battleTickMain_var1 = 0x27; break;
        case 0x40: pBattleEntities->m5FC7_battleTickMain_var1 = 0x28; break;
        case 0x20: pBattleEntities->m5FC7_battleTickMain_var1 = 0x29; break;
        case 0x10: pBattleEntities->m5FC7_battleTickMain_var1 = 0x2A; break;
        case 0x4: pBattleEntities->m5FC7_battleTickMain_var1 = 0x2B; break;
        }
        break;
    case 1:
        switch (arg1) {
        case 0x1000: pBattleEntities->m5FC7_battleTickMain_var1 = 0x2D; break;
        case 0x800: pBattleEntities->m5FC7_battleTickMain_var1 = 0x2E; break;
        case 0x400: pBattleEntities->m5FC7_battleTickMain_var1 = 0x2F; break;
        case 0x40: pBattleEntities->m5FC7_battleTickMain_var1 = 0x15; break;
        case 0x20: pBattleEntities->m5FC7_battleTickMain_var1 = 0x16; break;
        case 0x2: case 0x8: pBattleEntities->m5FC7_battleTickMain_var1 = 0x19; break;
        case 0x1: case 0x4: pBattleEntities->m5FC7_battleTickMain_var1 = 0x1A; break;
        }
        break;
    case 3:
        switch (arg1) {
        case 0x8000: pBattleEntities->m5FC7_battleTickMain_var1 = 0x1B; break;
        case 0x4000: pBattleEntities->m5FC7_battleTickMain_var1 = 0x1C; break;
        case 0x2000: pBattleEntities->m5FC7_battleTickMain_var1 = 0x1D; break;
        case 0x1000: pBattleEntities->m5FC7_battleTickMain_var1 = 0x1E; break;
        case 0x400: pBattleEntities->m5FC7_battleTickMain_var1 = 0x1F; break;
        case 0x800: pBattleEntities->m5FC7_battleTickMain_var1 = 0x20; break;
        case 0x100: pBattleEntities->m5FC7_battleTickMain_var1 = 0x21; break;
        case 0x200: pBattleEntities->m5FC7_battleTickMain_var1 = 0x22; break;
        }
        break;
    }
}
