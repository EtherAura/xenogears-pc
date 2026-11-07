/*
 * Battle loader, sprite actors, jump / attack animations and camera (Noah
 * battleLoader.cpp, battleSpriteLoader.cpp, battle.cpp). 0x800B7C28-0x800BD1FC;
 * gcc-2.7.2-cdk, ASPSX 2.56.
 */
#include "common.h"
#include "psyq.h"
#include "main/sprite.h"
#include "main/task.h"
#include "main/memory.h"
#include "main/filesystem.h"
#include "battle/battle.h"

#include "battle_loader/battle_loader.h"
#include "pc_gameplay.h"
s32 battlePreviewAbility(s32);
s32 battlePreviewAbilityLimit(s32);
s32 battlePreviewHide(SpriteActorCore*);
#include "pollhost.h"
#include "stack.h"
#include "gte.h"

extern u8 loadingFXFramentVar1;
extern s32 backupCameraMode;
extern SVECTOR battleCameraParamsVar0;
extern s16 updateTargetSelectionMarkerSub0_var0;
extern s16 isFxFragmentsSetup;
extern u8 fxFragmentInitialized;
extern u8 battleCameraVar0;
extern sJumpAnimationControlStruct* jumpAnimationControlStruct;
void stopCdRead(s32);
void battleRender(void);
void spriteActorSetPlayingAnimation(SpriteActorCore*, s32);
void abilityPlayAnimationSub1(s32);
u8 initFxFragments(void);
void waitBattleAnimationSpriteLoading(void);
void setBattleCameraMode(s32);
extern s16 battleMechaPartyTextureLocations[3][2];
extern u8 loadingFXFramentVar0;
extern s32* FxFragmentSpecialAnimation;
extern u8 battleAnimGearUnloadMode;
extern SpriteActorCore* processBattleAnimationSub0_var1;
extern u16 battleMechaTextureSlotsUsed;
extern s32* fxUploadBuffer;
extern s32* battleAnimationLoadingDest;
void removeOtherPartyActors(s32);
s32* allocateBufferForVramUpload(s32 count, s32 flags);
s32 loadImageFileToVram(s32 fileIndex, s32* buf, s32 channel, s32 flags, u16 arg4, u16 arg5, u16 arg6, u16 arg7, u16 arg8, u16 arg9);
extern SpriteVramData createSavePointMeshData_mode6;
extern u8 startJumpAnimationVar1;
extern s16 startJumpAnimationVar2;
extern u8 fxFragmentLoaded;
extern u8 battleDeathBlowMusicOverride;
void allocateSavePointMeshDataSub0(TaskHeader* owner, TaskHeader* task);
void allocateSavePointMeshDataSub0_callback(TaskHeader* task);
void initFieldEntitySub4Sub5Sub0(SpriteVramData* vram, AnimationBundle* bundle, Vec2s16 vramLocation, Vec2s16 clut, s32 unused);
s32 isVramPreBacked(void* spriteData);
SpriteActorCore* spriteBytecode2ExtendedE0(SpriteActorCore* actor, u16* anim, SpriteVramData* vram);
void spriteActorSetSpecialAnimation(SpriteActorCore* actor, void* animation);
void battleLoadFileFrom0xC(void);
u8* loadEffectFragmentsAndAudio(void*);
extern u8 currentBattleLoaderTransitionEffect;
extern u8 battleLoadDataVar2[];
extern void* battleLoadDataVar1;
extern void* battleLoadDataVar0_raw;
s32 waitReadCompletion(s32 mode);
void loadBattleLoader(void);
void battleLoaderUpdateMode1(void);
void battleLoaderUpdateMode2(void);
void battleLoaderUpdateMode3(void);
void battleLoaderUpdateMode4(void);
void battleStartEffect(void);
void mechaInitForBattle(void);
u8 loadBattleEnvironment(void*, void*, void*, MATRIX*, MATRIX*, u8*);
void setupBattleDrawEnvs(u8*, u8*);
void initBattleDefDrawContext(void);
extern BattleRenderStruct battleRenderStructs[2] PORT_ALIAS;
void SetGeomOffset(s32, s32);
extern SVECTOR battleCameraEye;
extern SVECTOR battleCameraAt;
void initBattle3dRendering(void);
void initBattleSpriteSystem(void);
void createBattleSpriteLoadingTask(void*);
void setSVector3(SVECTOR* v, s32 x, s32 y, s32 z);
typedef struct {
    /* 0x0 */ u16 m0_fileIndex;
    /* 0x2 */ s16 m2_soundId;
} BattleSoundFileEntry;
extern BattleSoundFileEntry battleEventSoundFiles[11]; /* JP splits it after [0] */
void loadWdsDataIfNeeded(void);
s32 batchStartLoadingFiles(LoadingBatchCommand*, s32, s32);
void loadSequence(void*);
void playSoundEffectInFreeSlot(s32);
s32 getPlayingSoundEffectMask(s32);
void unloadSequence(void*);
extern void* battleSpriteLoadTempBuffer;
void findOrDeleteStatusEffectSprite(SpriteActorCore*, s32, s32);
void freeMecha_battle(s32);
void releaseSpriteActor(s32);
extern u8 isBattleOverlayLoaded;
extern s32 allocateSavePointMeshDataSub0_var0;
extern u8 spriteBytecode2ExtendedE0_Var0;
extern s32 battleDefaultEntityScale;
extern s32 objectClippingMask;
extern s32 gDepthDivider;
extern s32 fieldDrawEnvsInitialized;
extern s32 battleDrawEnvCount;
void freeShapeTransfert(void);
void deleteAllTasks(void);
void freeMechaModule_battle(void);
void freeBattleEnvironment(void);
void initJumpAnimationStuff(void);
void initBattleUnkData0(void);
void resetSpriteCallbacks(void);
void resetBattleCameraMode(void);
void initUpdateTargetSelectionMarkerSub0_var0(void);
void resetLoadingFxFragmentFlag(void);
void stopControllerVibration(void);
extern u16 jumpAnimationActiveActorBF;
extern s32 allocateJumpAnimationStructVar0;
extern u8 battleAnimationSoundLoaded;
extern u8 startJumpAnimationVar3;
extern s32 startReadingBattleJumpAnimationVar0;
extern Wds* loadWdsDataNeeded;
sJumpAnimationControlStruct* allocateJumpAnimationStruct(void);
SpriteActorCore* setupBattleAnimationSpriteCore(s32);
void getSpriteCoreForAnimation(SpriteActorCore*);
void startReadingBattleJumpAnimation(SpriteActorCore*);
void waitBattleAnimationSoundLoaded(void);
void setupJumpAnimationStruct1C(s32);
void performMechaPlayAnimation(s32, s32, s32);
void jumpUpdatePosition(SpriteActorCore*);
void startJumpAnimationCallback(SpriteActorCore*);
void updateBattleAnimationDataLoading(void);
s32 isDamageDisplayPolysTask2Running(void);
void clearFxFragment(void);
void sampleBattleTerrainHeight(SpriteActorCore*);
void deleteJumpAnimationControlStruct(void);
extern u8 battleCameraGearHeightDisabled;
void freePlayerTurnStructs(void);
s32 processBattleAnimationSub0(void);
void waitForBattleAnimationsToSettle(void);
void freeDamageDisplayPolysTask2(void);
void battleRestartReservedMechaLoads(void);
void clearAnimationsEndOfTurn(s32);
void battleAnimationCallback_2Sub0(SpriteActorCore*, SpriteActorCore*);
void computeBattleCameraParams(s32);
void processBattleAnimation(SpriteActorCore*);
extern s16 delayBattleAnimationCounter;
extern u8 allocateJumpAnimationStructCallbackSpecial0;
void abilityPlayAnimation(s32, SpriteActorCore*);
void setNeedFxFragments(SpriteActorCore*);
extern s16 attackInProgress;
extern s16 abilityAnimationTable0[];
extern s16 abilityAnimationTable1[];
s32 getAllocationSize(void*);
void spriteActorSetDefaultAnimationId(SpriteActorCore* actor, s32 animationId);
u8* loadFragmentIsNotLoadedYet(void);
void setSpriteActorAngle(SpriteActorCore* actor, s16 angle);
void spriteActorSetDirection(SpriteActorCore* actor, s32 direction);
extern s16 gearAbilityAnimationId;
void swapBattleMechaSlots(s32, s32);
extern s32 isInJumpAnimationCallbackCode;
extern SpriteActorCore* needFxFragments;
extern SpriteActorCore* pSpriteCoreListHead[];
void jumpUpdatePositionSub0(SpriteActorCore*, SpriteActorCore*);
void battleAnimationCallback_5(s32);
s32 computerJumpDistanceToTarget(Vec2s16, Vec2s16);
void processMechaBattleAnimation(void);
void clearAnimationsEndOfTurnSub(void);
s32 getAngleSin(s32 angle);
s32 getAngleCos(s32 angle);
s32 findBattleTerrainTriangle(SVECTOR*, s32, s32);
s32 findBattleTerrainTriangleBruteForce(SVECTOR*);
void computeTerrainHeightAtTriangle(SVECTOR*, s32, VECTOR*);
s32 ratan2(s32 y, s32 x);
void Square0(VECTOR* v0, VECTOR* v1);
s32 SquareRoot0(s32 a);
void computeJumpXZStep(SpriteActorCore*, s32);
void applySpriteVerticalMovement(SpriteActorCore*);
extern u8 battleSpritesDisabled;
extern OTag* characterRenderingOT;
TaskHeader* allocateTaskPair(s32 size, TaskHeader* owner, void (*update)(TaskHeader*), void (*draw)(TaskHeader*), void (*del)(TaskHeader*));
void initBattleSpriteActorVram(SpriteActor* actor, AnimationBundle* bundle, s16 clutX, s16 clutY, s16 vramX, s16 vramY, s16 arg6, s32 vramParam);
void setGraphicEntityScale(SpriteActorCore* actor, s32 scale);
void renderSpriteActor(SpriteActorCore* actor, OTag* ot);
void SetRotMatrix(MATRIX* m);
void SetTransMatrix(MATRIX* m);
s32 RotTransPers(SVECTOR* v0, s32* sxy, s32* p, s32* flag);
void battleSpriteUpdate(TaskHeader*);
void battleSpriteRender(TaskHeader*);
void battleSpriteDelete(TaskHeader*);
void registerSpriteCallback2_2(TaskHeader* task);
void savePointCallbackC_sub1(SpriteActorCore* actor);
void registerSpriteCallback2Sub0(TaskHeader* task);
void OP_INIT_ENTITY_SCRIPT_sub0Sub9(SpriteActorCore* actor);
void updateSpriteMovement(SpriteActorCore* actor);
void PushMatrix(void);
void PopMatrix(void);
extern s32 allocateSavePointMeshDataSub0_var1;
extern s32 mainBattleSpriteCallback_phase5Var;
void createMechaLoadingTask(s32);
void dismissPartyGears(s32);
extern SpriteVramData createSavePointMeshData_mode5;
s32 initMechaForBattleSub0_battle(s32);
void initFieldEntitySub4Sub1(SpriteActorCore* actor);
void initFieldEntitySub4Sub2(SpriteActor* actor);
void battleMechaUpdate(TaskHeader*);
void battleMechaDraw(TaskHeader*);
void battleMechaDelete(TaskHeader*);
extern u8 preventGearLoadingInBattle;
extern u8 finalizeBattleMechaLoadingVar0;
extern u8 battleScriptLoaderTaskCreationInProgress;
TaskHeader* allocateUpdateTask(TaskHeader* owner, s32 size);
void createGearLoadingFileRequests(s32);
void startBattleGearLoading(TaskHeader*);
void mechaLoadingTask_phase0(TaskHeader*);
void mechaLoadingTask_phase1(TaskHeader*);
void initMechaForBattle(u32);
extern s32 battleIR0;
extern s32 allEntitiesToFitInView;
extern u8 battleCameraControlSpriteActiveCount;
extern s32 battleCameraModeInterpolation;
extern SVECTOR battleCameraUp;
extern u8 renderBattleSceneDisabled;
void setVector3(VECTOR* v, s32 x, s32 y, s32 z);
VECTOR* ApplyMatrix(MATRIX* m, SVECTOR* v0, VECTOR* v1);
void battleDrawEnv(MATRIX*, MATRIX*, s32, OTag*, s32, SVECTOR*, SVECTOR*, s32);
void lookAtNoDivide(MATRIX* m, SVECTOR* eye, SVECTOR* at, SVECTOR* up);

void battleIdleDuringLoading(void);
void clearBattleAnimationData(void);
void startJumpAnimationSub0(s32 arg0);
void setBattleCameraParamX(s16 arg0);
extern s32 battleCameraMode;
extern SpriteActorCore* battleCameraFollowActor;
extern s16 battleCameraFollowAngle;
extern s32 battleCameraFollowDistance;
extern SpriteTask* battleCameraModeCallback1; /* camera eye-target sprite task */
extern SpriteTask* battleCameraModeCallback2; /* camera at-target sprite task */
extern SVECTOR battleCameraEyeTarget_backup;
extern SVECTOR battleCameraAtTarget_backup;
static inline void fpToSVector3(SVECTOR* dst, FP_VEC3* src) {
    dst->vx = src->vx >> 16;
    dst->vy = src->vy >> 16;
    dst->vz = src->vz >> 16;
}
static inline void svecToFp(FP_VEC3* dst, SVECTOR* src) {
    dst->vx = src->vx << 16;
    dst->vy = src->vy << 16;
    dst->vz = src->vz << 16;
}
void battleCameraControlSpriteDelete(TaskHeader*);
void battleCameraControlSpriteUpdate(TaskHeader*);
extern BattleBlinkTask* characterBlinkTask;
extern s32 battleCameraVar2;
extern u16 battleCameraVar1;
void updateAllSubsprites(SpriteActorCore*);
void updateTargetSelectionMarkerSub0Sub2(TaskHeader*);
void updateCharacterBlinkColor(TaskHeader*);
extern u8 battleBlinkingPolyBuffer[0x170]; /* Noah: battleBlinkingPolyBuffer (main .text, 0x8001C76C-0x8001C8DC) */
extern MATRIX currentRenderingMatrix;
TaskHeader* findMatchingGraphicEntity(TaskHeader* owner, void (*callback)(TaskHeader*));
MATRIX* TransMatrix(MATRIX* m, VECTOR* v);
MATRIX* CompMatrix(MATRIX* m0, MATRIX* m1, MATRIX* m2);
s32 ReadGeomScreen(void);
MATRIX* ScaleMatrixL(MATRIX* m, VECTOR* v);
void renderCustomPolyBuffer(void* modelBlock, void* primBuffer, OTag* ot, s32 a3, s32 depth, s32 blend);
void clearShapeTransferEntry(void* p);
void* getCustomPolySubBuffer(void*, s32);
s32 getCustomPolySubBufferSize(void*);
void updateSpinningEnemySelectionCursor(TaskHeader*);
void drawSpinningEnemySelectionCursor(TaskHeader*);
void deleteSpinningEnemySelectionCursor(TaskHeader*);
void createSpinningEnemySelectionCursor(sBattleSpriteActor*);
void updateColorFromSin(SpriteActorCore*, s32);
void* memset(void* dst, s32 c, s32 n);
MATRIX* RotMatrixZYX(SVECTOR* r, MATRIX* m);
static inline void setEyeAt(SVECTOR* eye, SVECTOR* at, s16 x, s16 y, s16 z, s16 dx, s16 dy, s16 dz) {
    eye->vx = x;
    eye->vy = y;
    eye->vz = z;
    at->vx = x;
    at->vy = y;
    at->vz = z;
    eye->vx -= dx;
    eye->vy = y + dy;
    eye->vz -= dz;
}
/* DECLS END */

static inline void copySVector3(SVECTOR* dst, SVECTOR* src) {
    dst->vx = src->vx;
    dst->vy = src->vy;
    dst->vz = src->vz;
}

/* Battle entity index of a sprite actor: mAC bits 0-1 are the upper two bits,
 * mA8 bits 30-31 the lower two (Noah: entityIdUpper2bit/entityId_bottom2bit).
 * Retail reads the low part first, into a temporary, and never preexpands it
 * the way GCC does calls (a statement expression, not an inline function). */
#if VERSION_JP
/* JP keeps the whole 4-bit entity index in mAC bits 0-3. */
#define BATTLE_ENTITY_ID(p) ((p)->mAC_x0)
#else
#define BATTLE_ENTITY_ID(p) ({ s32 _lo = (p)->mA8_x1E; ((p)->mAC_x0 << 2) | _lo; })
#endif

/* Noah resetLoadingFxFragmentFlag (loadingFXFramentVar1 = 0) */
void resetLoadingFxFragmentFlag(void) {
    loadingFXFramentVar1 = 0;
}

/* Noah abilityPlayAnimationSub1 (load FX fragment file 0x22+2n, dir 0xC) */
void abilityPlayAnimationSub1(s32 arg0) {
    RECT rect;
    s32 file;
    s32 file2;
    s32 mode;
    SpriteActorCore* actor;

    if (arg0 == 0xE3) {
        rect.w = 0x40;
        rect.h = 0x100;
        rect.x = battleMechaPartyTextureLocations[0][0];
        rect.y = battleMechaPartyTextureLocations[0][1];
        MoveImage(&rect, 0x280, 0x100);
        rect.w = 0x40;
        rect.h = 0x100;
        rect.x = battleMechaPartyTextureLocations[1][0];
        rect.y = battleMechaPartyTextureLocations[1][1];
        MoveImage(&rect, 0x240, 0x100);
        rect.w = 0x40;
        rect.h = 0x100;
        rect.x = battleMechaPartyTextureLocations[2][0];
        rect.y = battleMechaPartyTextureLocations[2][1];
        MoveImage(&rect, 0x200, 0x100);
        DrawSync(0);
    }
    clearBattleAnimationData();
    setCurrentDirectory(0xC, 2);
    file = arg0 * 2 + 0x22;
    loadingFXFramentVar0 = 1;
    file2 = arg0 * 2 + 0x23;
    loadingFXFramentVar1 = 1;
    FxFragmentSpecialAnimation = allocateHeap(getFileSizeAligned(file), 0);
    readFile(file, FxFragmentSpecialAnimation, 0, 0x80);
    battleIdleDuringLoading();
#if VERSION_JP
    mode = (*(u16*)((sptr)FxFragmentSpecialAnimation + FxFragmentSpecialAnimation[1]) >> 12) & 3;
#else
    mode = (*(u16*)(FxFragmentSpecialAnimation[1] + (sptr)FxFragmentSpecialAnimation) >> 12) & 3;
#endif
    if (mode != 0) {
        battleAnimGearUnloadMode = mode;
        actor = processBattleAnimationSub0_var1;
        setCameraVisibleEntities(0);
        if (mode == 1) {
            removeOtherPartyActors(-1);
            battleMechaTextureSlotsUsed = 0;
        } else {
            battleMechaTextureSlotsUsed &= ~(1 << BATTLE_ENTITY_ID(actor));
            removeOtherPartyActors(BATTLE_ENTITY_ID(actor));
        }
    }
    fxUploadBuffer = allocateBufferForVramUpload(8, 0);
    if (getFileSizeAligned(file2) > 0x10) {
        loadImageFileToVram(file2, fxUploadBuffer, 0, 0, 0, 0, 0, 0, 0, 0);
    }
    battleIdleDuringLoading();
    DrawSync(0);
    battleAnimationLoadingDest = FxFragmentSpecialAnimation;
    freeHeap(fxUploadBuffer);
}

/* Noah initFxFragments */
u8 initFxFragments(void) {
    Vec2s16 vramLocation;
    Vec2s16 clut;
    TaskHeader task;
    SpriteVramData vram;
    SpriteActorCore* owner;
    SpriteActorCore* actor;
    s32 result;
    u8* fragments;

    owner = processBattleAnimationSub0_var1;
    allocateSavePointMeshDataSub0(NULL, &task);
    task.m8_updateCallback = NULL;
    battleIdleDuringLoading();
    vramLocation.vx = 0x380;
    vramLocation.vy = 0x100;
    clut.vx = 0;
    clut.vy = 0x1F4;
    startJumpAnimationVar1 = 0;
    initFieldEntitySub4Sub5Sub0(&createSavePointMeshData_mode6, (AnimationBundle*)FxFragmentSpecialAnimation, vramLocation, clut, 0);
    battleLoadFileFrom0xC();
    result = 0;
    if (isVramPreBacked((void*)createSavePointMeshData_mode6.m0_spriteData)) {
        vram = *processBattleAnimationSub0_var1->m24_vramData;
        actor = spriteBytecode2ExtendedE0(processBattleAnimationSub0_var1, (u16*)(((u16*)createSavePointMeshData_mode6.m10_startOfAnimationContainer)[startJumpAnimationVar2 + 1] + createSavePointMeshData_mode6.m10_startOfAnimationContainer), &createSavePointMeshData_mode6);
    } else {
        result = 1;
        actor = processBattleAnimationSub0_var1;
        spriteActorSetSpecialAnimation(actor, FxFragmentSpecialAnimation);
        spriteActorSetPlayingAnimation(actor, -1);
    }
    fragments = loadEffectFragmentsAndAudio(FxFragmentSpecialAnimation);
    actor->m50_soundEffectSeq = fragments;
    owner->m50_soundEffectSeq = fragments;
    fxFragmentLoaded = 1;
    battleDeathBlowMusicOverride = 1;
    playBattleMusic(pCurrentBattleMusic, 0x60, 0x78);
    allocateSavePointMeshDataSub0_callback(&task);
    return result;
}

/* Noah setRenderingInfoForItemUser */
void setRenderingInfoForItemUser(SpriteActorCore* arg0);
void setRenderingInfoForItemUser(SpriteActorCore* arg0) {
    processBattleAnimationSub0_var1 = arg0;
}

/* sets isFxFragmentsSetup, clears fxFragmentInitialized */
void requestFxFragmentSetup(s16 arg0);
void requestFxFragmentSetup(s16 arg0) {
    isFxFragmentsSetup = arg0;
    fxFragmentInitialized = 0;
}

/* Noah fxFragmentSetup */
void fxFragmentSetup(s32 arg0);
void fxFragmentSetup(s32 arg0) {
    abilityPlayAnimationSub1(arg0);
    initFxFragments();
    fxFragmentInitialized = 1;
}

/* battle scene init: loader + per-type (0-5) setup, env load (switch, jtbl 0x80070A10) */
void startBattleLoader(s32 arg0) {
    u8 tmp;
    u8* p;
    currentBattleLoaderTransitionEffect = arg0;
    initBattleDefDrawContext();
    loadBattleLoader();
    switch (arg0) {
    case 1:
        waitReadCompletion(0);
        battleLoaderUpdateMode1();
        break;
    case 2:
        waitReadCompletion(0);
        battleLoaderUpdateMode2();
        break;
    case 3:
        waitReadCompletion(0);
        battleLoaderUpdateMode3();
        break;
    case 4:
        waitReadCompletion(0);
        battleLoaderUpdateMode4();
        break;
    case 0:
    case 5:
    default:
        battleStartEffect();
        break;
    }
    waitReadCompletion(0);
    mechaInitForBattle();
    battleBssBlock.mB70_renderStructs[0].m0_drawEnv.isbg = loadBattleEnvironment(battleLoadDataVar2, battleLoadDataVar1, battleLoadDataVar0_raw, &battleBssBlock.m8CE4_matrix800CCB94, &battleBssBlock.m8D04_matrix800CCBB4, &battleRenderStructs[0].m0_drawEnv.r0);
    setupBattleDrawEnvs(&battleRenderStructs[0].m0_drawEnv.r0, &battleBssBlock.mB70_renderStructs[1].m0_drawEnv.r0);
}

/* battle end/teardown: sound, restore camera vectors from battleEnvMechaInitData, SetDispMask(1) */
void initBattleGraphics(void* arg0);
void initBattleGraphics(void* arg0) {
    initBattle3dRendering();
    initBattleSpriteSystem();
    createBattleSpriteLoadingTask(arg0);
    unloadWds(wdsFile5);
    setSVector3(&battleCameraBlock.m4_eyeTarget, battleEnvMechaInitData->m47C_cameraPresets[0].m6_eye[0], battleEnvMechaInitData->m47C_cameraPresets[0].m6_eye[1],
                battleEnvMechaInitData->m47C_cameraPresets[0].m6_eye[2]);
    setSVector3(&battleCameraEye, battleEnvMechaInitData->m47C_cameraPresets[0].m6_eye[0], battleEnvMechaInitData->m47C_cameraPresets[0].m6_eye[1],
                battleEnvMechaInitData->m47C_cameraPresets[0].m6_eye[2]);
    setSVector3(&battleCameraBlock.mC_atTarget, battleEnvMechaInitData->m47C_cameraPresets[0].m0_at[0], battleEnvMechaInitData->m47C_cameraPresets[0].m0_at[1],
                battleEnvMechaInitData->m47C_cameraPresets[0].m0_at[2]);
    setSVector3(&battleCameraAt, battleEnvMechaInitData->m47C_cameraPresets[0].m0_at[0], battleEnvMechaInitData->m47C_cameraPresets[0].m0_at[1],
                battleEnvMechaInitData->m47C_cameraPresets[0].m0_at[2]);
    SetDispMask(1);
}

/* Noah initBattleDefDrawContext */
void initBattleDefDrawContext(void) {
    SetGeomOffset(0xA0, 0xA4);
    SetDefDispEnv(&battleBssBlock.mB70_renderStructs[0].m5C_dispEnv, 0, 0xE0, 0x140, 0xE0);
    SetDefDispEnv(&battleBssBlock.mB70_renderStructs[1].m5C_dispEnv, 0, 0, 0x140, 0xE0);
    SetDefDrawEnv(&battleBssBlock.mB70_renderStructs[0].m0_drawEnv, 0, 0, 0x140, 0xE0);
    SetDefDrawEnv(&battleBssBlock.mB70_renderStructs[1].m0_drawEnv, 0, 0xE0, 0x140, 0xE0);
    battleBssBlock.mB70_renderStructs[0].m5C_dispEnv.screen.y = battleBssBlock.mB70_renderStructs[1].m5C_dispEnv.screen.y = 10;
    battleBssBlock.mB70_renderStructs[0].m5C_dispEnv.screen.w = battleBssBlock.mB70_renderStructs[1].m5C_dispEnv.screen.w = 0x100;
    battleBssBlock.mB70_renderStructs[0].m5C_dispEnv.screen.x = battleBssBlock.mB70_renderStructs[1].m5C_dispEnv.screen.x = 0;
    battleBssBlock.mB70_renderStructs[0].m5C_dispEnv.screen.h = battleBssBlock.mB70_renderStructs[1].m5C_dispEnv.screen.h = 0xD8;
}

/* Noah battleIdleDuringLoading */
void battleIdleDuringLoading(void) {
    while (isCDBusy()) {
        battleRender();
    }
}

/* load+play a battle sound seq/wds pair (Noah: battleEvent OP48 target) */
void playBattleSoundFileAndWait(s32 arg0, s32 arg1);
void playBattleSoundFileAndWait(s32 arg0, s32 arg1) {
    LoadingBatchCommand cmds[4];
    u8* seq;
    void* wdsData;
    Wds* wds;
    s32 id;

    loadWdsDataIfNeeded();
    battleIdleDuringLoading();
    setCurrentDirectory(0x2C, 1);
    cmds[0].m0_fileIndex = battleEventSoundFiles[arg0].m0_fileIndex;
    cmds[0].m4_loadPtr = seq = allocateHeap(getFileSizeAligned(cmds[0].m0_fileIndex), 0);
    if (arg0 == 8) {
        cmds[1].m0_fileIndex = battleEventSoundFiles[8].m0_fileIndex + 1;
    } else {
        cmds[1].m0_fileIndex = (u16)(battleEventSoundFiles[arg0].m0_fileIndex + 1) + arg1;
    }
    cmds[1].m4_loadPtr = wdsData = allocateHeap(getFileSizeAligned(cmds[1].m0_fileIndex), 0);
    cmds[2].m4_loadPtr = NULL;
    cmds[2].m0_fileIndex = 0;
    batchStartLoadingFiles(cmds, 0, 0);
    battleIdleDuringLoading();
    loadSequence(seq);
    wds = loadWds(wdsData, 0);
    while ((s16)waitForMusic(0)) {
        battleRender();
    }
    id = battleEventSoundFiles[arg0].m2_soundId + arg1 + (*(u16*)(seq + 0x14) << 16);
    playSoundEffectInFreeSlot(id);
    while (getPlayingSoundEffectMask(id)) {
        battleRender();
    }
    unloadSequence(seq);
    unloadWds(wds);
    freeHeap(wdsData);
    freeHeap(seq);
}

/* battle end: per-type victory/defeat poses, release actors 3-10, reload sound bank (file 5) */
void playBattleEndAndReleaseEnemies(s32 arg0);
void playBattleEndAndReleaseEnemies(s32 arg0) {
    s32 i;
    s32 j;
    SpriteActorCore* actor;
    SpriteActorCore* actor2;
    void* data;

    switch (arg0) {
    case 0:
        setCameraVisibleEntities(-1);
        for (i = 0; i != 3; i++) {
            actor = battleBssBlock.m8C8C_spriteActorCores[i];
            if (actor != NULL && (s8)actor->mAF_animationId != 0x15) {
                findOrDeleteStatusEffectSprite(actor, 0, 2);
                spriteActorSetPlayingAnimation(actor, 0x18);
            }
        }
        break;
    case 2:
        setCameraVisibleEntities(0);
        battleCreateFader(0x28, 2, 0xFF, 0xFF, 0xFF);
        for (j = 0; j != 3; j++) {
            actor2 = battleBssBlock.m8C8C_spriteActorCores[j];
            if (actor2 != NULL && (s8)actor2->mAF_animationId != 0x15) {
                spriteActorSetPlayingAnimation(actor2, 5);
            }
        }
        for (j = 0x28; j > 0; j--) {
            battleRender();
        }
        break;
    case 1:
        break;
    }
    for (j = 3; j != 11; j++) {
        freeMecha_battle(j);
        releaseSpriteActor(j);
    }
    loadWdsDataIfNeeded();
    battleIdleDuringLoading();
    setCurrentDirectory(0x2C, 0);
    data = allocateHeap(getFileSizeAligned(5), 1);
    readFile(5, data, 0, 0x80);
    battleIdleDuringLoading();
    wdsFile5 = loadWds(data, 0);
    while ((s16)waitForMusic(0)) {
        battleRender();
    }
    freeHeap(data);
    freeHeap(battleSpriteLoadTempBuffer);
    battleBssBlock.m8DA8_timeEnabled = 0;
}

/* battle overlay shutdown (free sprites/sequence, isBattleOverlayLoaded = 0) */
void shutdownBattleSpriteSystem(void);
void shutdownBattleSpriteSystem(void) {
    s32 i;

    if (battleBssBlock.m8C84_oddOrEven == 0) {
        battleRender();
    }
    DrawSync(0);
    for (i = 0; i != 11; i++) {
        releaseSpriteActor(i);
    }
    freeShapeTransfert();
    deleteAllTasks();
    unloadSequence(gBattleCurrentSoundEffectSeq);
    freeHeap(gBattleCurrentSoundEffectSeq);
    isBattleOverlayLoaded = 0;
    DrawSync(0);
    freeMechaModule_battle();
    freeBattleEnvironment();
    freeHeap(battleSpriteBundle);
}

/* battle overlay startup (isBattleOverlayLoaded = 1, sprite system init) - Noah initBattleSpriteSystem-like */
void initBattleSpriteSystem(void) {
    isBattleOverlayLoaded = 1;
    allocateSavePointMeshDataSub0_var0 = 0;
    spriteBytecode2ExtendedE0_Var0 = 0;
    battleDefaultEntityScale = 0x2000;
    initJumpAnimationStuff();
    initBattleUnkData0();
    resetSpriteCallbacks();
    resetBattleCameraMode();
    allocateShapeTransfert(0x5000, 0);
    initUpdateTargetSelectionMarkerSub0_var0();
    resetLoadingFxFragmentFlag();
    stopControllerVibration();
    objectClippingMask = 0;
}

/* count gears -> frame skip, swap/clear battle OT (Noah battleRender prologue part) */
void initBattle3dRendering(void) {
    s32 i;
    BattleRenderStruct* rs;

    i = 3;
    battleDrawEnvCount = 0;
    for (; i != 11; i++) {
        if (battleBssBlock.m4_visualEntities[i].m2_characterId < 0x11 && battleBssBlock.m4_visualEntities[i].m4_isGear) {
            battleDrawEnvCount++;
        }
    }
    fieldDrawEnvsInitialized = battleDrawEnvCount / 2 - 1;
    if (fieldDrawEnvsInitialized < 0) {
        fieldDrawEnvsInitialized = 0;
    }
    battleBssBlock.m8DAC_frameRate = fieldDrawEnvsInitialized;
    fieldDrawEnvsInitialized = 0;
    gDepthDivider = 2;
    rs = &battleBssBlock.mB70_renderStructs[0];
    if (battleBssBlock.m8C50_currentRenderStruct == rs) {
        rs++;
    }
    battleBssBlock.m8C50_currentRenderStruct = rs;
    battleBssBlock.m8C54_currentOT = rs->m70_ot;
    ClearOTagR(rs->m70_ot, 0x1000);
    battleBssBlock.m8C84_oddOrEven = 1;
    battleBssBlock.m8C50_currentRenderStruct = &battleBssBlock.mB70_renderStructs[1];
    battleBssBlock.m8DA8_timeEnabled = 0;
    battleBssBlock.mB70_renderStructs[1].m0_drawEnv.isbg = battleBssBlock.mB70_renderStructs[0].m0_drawEnv.isbg;
    battleBssBlock.mB70_renderStructs[1].m0_drawEnv.r0 = battleBssBlock.mB70_renderStructs[0].m0_drawEnv.r0;
    battleBssBlock.mB70_renderStructs[1].m0_drawEnv.g0 = battleBssBlock.mB70_renderStructs[0].m0_drawEnv.g0;
    battleBssBlock.mB70_renderStructs[1].m0_drawEnv.b0 = battleBssBlock.mB70_renderStructs[0].m0_drawEnv.b0;
}

/* Noah stopControllerVibration */
void stopControllerVibration(void) {
}

/* Noah startJumpAnimation */
void startJumpAnimation(s32 isBilly, s32 actor, s32 target, s32 facing) {
    SpriteActorCore* core;
    void* buf;
    s32 mode;

    startJumpAnimationSub0(0);
    if (jumpAnimationControlStruct != NULL) {
        for (;;) {
            POLLHOST();
        }
    }
    battleRender();
    battleRender();
    startJumpAnimationVar1 = 0;
    jumpAnimationActiveActorBF &= ~(1 << actor);
    jumpAnimationControlStruct = allocateJumpAnimationStruct();
    jumpAnimationControlStruct->m40_facing = facing;
    jumpAnimationControlStruct->m20_actor = actor;
    getSpriteCoreForAnimation(setupBattleAnimationSpriteCore(actor));
    startJumpAnimationVar2 = 0;
    core = jumpAnimationControlStruct->m4_spriteCore;
    if (battleBssBlock.m4_visualEntities[actor].m4_isGear == 0) {
        loadWdsDataIfNeeded();
        battleAnimationSoundLoaded = 0;
        setBattleCameraParamX(0xC0);
        if (isVramPreBacked((void*)core->m24_vramData->m0_spriteData) == 0) {
            startJumpAnimationVar3 = 0;
            if (battleAnimationLoadingDest == NULL || BATTLE_ENTITY_ID(core) != startReadingBattleJumpAnimationVar0) {
                startReadingBattleJumpAnimation(core);
            }
        } else {
            if (startJumpAnimationVar3 == 0) {
                battleIdleDuringLoading();
                setCurrentDirectory(0x2C, 0);
                buf = allocateHeap(getFileSizeAligned(7), 0);
                readFile(7, buf, 0, 0x80);
                battleIdleDuringLoading();
                loadWdsDataIfNeeded();
                loadWdsDataNeeded = loadWds(buf, 0);
                while ((s16)waitForMusic(0)) {
                    battleRender();
                }
                freeHeap(buf);
            }
            startJumpAnimationVar3 = 1;
        }
    } else {
        waitBattleAnimationSpriteLoading();
        waitBattleAnimationSoundLoaded();
        setupJumpAnimationStruct1C(8);
        performMechaPlayAnimation(actor, target, 0x1A);
        setBattleCameraParamX(0xC0);
    }
    if (isBilly == 0) {
        if (battleBssBlock.m4_visualEntities[actor].m4_isGear != 0) {
            performMechaPlayAnimation(actor, battleBssBlock.m138_currentDamages[allocateJumpAnimationStructVar0].m16_targetBitMask, 2);
        } else {
            /* JP (gcc 2.6.3) gives core s1 and actor s2 only when these two
             * uses of core weigh more in its reference count: flow.c weights
             * refs by loop depth, and do { } while (0) opens a loop. US
             * matches either way. */
            do {
                do {
                    spriteActorSetCallback(core, startJumpAnimationCallback);
                    jumpUpdatePosition(core);
                } while (0);
            } while (0);
        }
    } else {
        mode = 4;
        if (battleBssBlock.m4_visualEntities[actor].m4_isGear != 0) {
            mode = 9;
        }
        setupJumpAnimationStruct1C(mode);
    }
}

/* Noah waitBattleAnimationSpriteLoading */
void waitBattleAnimationSpriteLoading(void) {
    battleIdleDuringLoading();
    updateBattleAnimationDataLoading();
    while (allocateSavePointMeshDataSub0_var0 != isDamageDisplayPolysTask2Running()) {
        battleRender();
    }
    clearFxFragment();
    if (battleAnimationLoadingDest != NULL) {
        freeHeap(battleAnimationLoadingDest);
        battleAnimationLoadingDest = NULL;
    }
}

/* Noah clearBattleAnimationData */
void clearBattleAnimationData(void) {
    stopCdRead(0);
    waitBattleAnimationSpriteLoading();
}

/* end of a jump animation: put the actor back on its slot (or gear anim 0x1F) */
void cancelJumpAnim(void);
void cancelJumpAnim(void) {
    sJumpAnimationControlStruct* p;
    SpriteActorCore* core;
    s32 id;
#if VERSION_JP
#define bss (&battleBssBlock) /* JP reads the global directly each time */
#else
    BattleBss_800C3EB0* bss;
#endif

    if (jumpAnimationControlStruct != NULL) {
        setupBattleAnimationSpriteCore(jumpAnimationControlStruct->m20_actor);
#if !VERSION_JP
        bss = &battleBssBlock;
#endif
        p = jumpAnimationControlStruct;
        core = p->m4_spriteCore;
        id = BATTLE_ENTITY_ID(core);
        if (bss->m4_visualEntities[id].m4_isGear) {
            performMechaPlayAnimation(id, 0, 0x1F);
        } else {
            core->m0_position.vx = bss->m4_visualEntities[p->m24_entity].mA_X << 16;
            core->m0_position.vz = bss->m4_visualEntities[p->m24_entity].mC_Z << 16;
            sampleBattleTerrainHeight(core);
            core->m0_position.vy = core->m84_maxY << 16;
            spriteActorSetPlayingAnimation(core, core->mB0_x0_animationId);
            spriteActorSetCallback(core, NULL);
        }
        deleteJumpAnimationControlStruct();
    }
#undef bss
}

/* Noah deleteJumpAnimationControlStructEndOfAttack */
void deleteJumpAnimationControlStructEndOfAttack(void);
void deleteJumpAnimationControlStructEndOfAttack(void) {
    SpriteActorCore* core;
    s32 facing;
    s32 i;

    core = jumpAnimationControlStruct->m4_spriteCore;
    if (jumpAnimationControlStruct != NULL) {
        startJumpAnimationVar1 = 0;
        startJumpAnimationVar2 = 0;
        startJumpAnimationVar3 = 0;
        freePlayerTurnStructs();
        DrawSync(0);
        facing = jumpAnimationControlStruct->m40_facing;
        spriteActorSetCallback(core, NULL);
        processBattleAnimationSub0();
        while (allocateSavePointMeshDataSub0_var0 != isDamageDisplayPolysTask2Running()) {
            battleRender();
        }
        waitForBattleAnimationsToSettle();
        clearBattleAnimationData();
        startJumpAnimationSub0(1);
        freeDamageDisplayPolysTask2();
        updateBattleAnimationDataLoading();
        battleRestartReservedMechaLoads();
        battleCameraGearHeightDisabled = 0;
        setBattleCameraParamX(0xC0);
        deleteJumpAnimationControlStruct();
        clearAnimationsEndOfTurn(facing);
        if (battleDeathBlowMusicOverride != 0) {
            playBattleMusic(pCurrentBattleMusic, 0x7F, 0x50);
        }
        battleDeathBlowMusicOverride = 0;
        for (i = 0; i != 11; i++) {
            if (battleBssBlock.m8C8C_spriteActorCores[i] != NULL) {
                battleBssBlock.m8C8C_spriteActorCores[i]->mAC_x6 = 0;
            }
        }
    }
}

/* restore default animation, clear callback */
void resetSpriteToIdleAnimation(SpriteActorCore* arg0) {
    spriteActorSetPlayingAnimation(arg0, arg0->mB0_x0_animationId);
    spriteActorSetCallback(arg0, NULL);
}

/* Noah startPlayingAttackAnimation */
void startPlayingAttackAnimation(void);
void startPlayingAttackAnimation(void) {
    SpriteActorCore* core;
    SpriteActorCore* target;
    s32 mask;
    s32 idx;
    s16 targetX;

    setupBattleAnimationSpriteCore(battleBssBlock.m138_currentDamages[allocateJumpAnimationStructVar0].m23_battleEntityIndex);
    core = jumpAnimationControlStruct->m4_spriteCore;
    getSpriteCoreForAnimation(core);
    if (battleBssBlock.mA78_startPlayingAttackAnimationVar0 != 0) {
        target = core->m74_targetEntitySprite;
        target->mAC_x6 = 1;
        core->mAC_x6 = 1;
        battleAnimationCallback_2Sub0(core, target);
        target = core->m74_targetEntitySprite;
        mask = (1 << BATTLE_ENTITY_ID(core)) | (1 << BATTLE_ENTITY_ID(target));
        computeBattleCameraParams(mask & 0xFFFF);
        idx = BATTLE_ENTITY_ID(target);
        targetX = target->m0_position.vx >> 16;
        if (targetX != battleBssBlock.m4_visualEntities[idx].mA_X || (s16)(target->m0_position.vz >> 16) != battleBssBlock.m4_visualEntities[idx].mC_Z) {
            if (targetX < battleBssBlock.m4_visualEntities[idx].mA_X) {
                core->mA0_destination.vx = targetX - 0x50;
            } else {
                core->mA0_destination.vx = targetX + 0x50;
            }
        } else if ((s16)(core->m0_position.vx >> 16) < targetX) {
            core->mA0_destination.vx = targetX - 0x50;
        } else {
            core->mA0_destination.vx = targetX + 0x50;
        }
        core->mA0_destination.vz = target->m0_position.vz >> 16;
        core->mA0_destination.vy = 0;
        computeBattleCameraParams(mask & 0xFFFF);
        copySVector3(&battleCameraEye, &battleCameraBlock.m4_eyeTarget);
        copySVector3(&battleCameraAt, &battleCameraBlock.mC_atTarget);
        battleAnimationCallback_2Sub0(core, target);
    }
    updateBattleAnimationDataLoading();
    processBattleAnimation(core);
}

/* Noah abilityPlayAnimationSub0-like counter (jumpAnimationControlStruct->m34++) */
void queueMechaBattleAnimationStep(void);
void queueMechaBattleAnimationStep(void) {
    if (jumpAnimationControlStruct != NULL) {
        jumpAnimationControlStruct->m34_pendingSteps++;
    }
}

/* Noah processBattleAnimation_subType (switch, jtbl 0x80070A28) */
void processBattleAnimation_subType(SpriteActorCore* arg0, s32 arg1);
void processBattleAnimation_subType(SpriteActorCore* arg0, s32 arg1) {
    s32 v;

    switch (arg1) {
    case 0xFA:
        jumpAnimationControlStruct->m48_isStepDone = 1;
        spriteActorSetCallback(arg0, NULL);
        processBattleAnimation_subType_minus6(battleBssBlock.m138_currentDamages[allocateJumpAnimationStructVar0].m3A_animationParam);
        break;
    case 0xF8:
        jumpAnimationControlStruct->m48_isStepDone = 1;
        spriteActorSetCallback(arg0, NULL);
        hideMonsterAttackName(battleBssBlock.m138_currentDamages[allocateJumpAnimationStructVar0].m3A_animationParam);
        break;
    case 0xF7:
        jumpAnimationControlStruct->m48_isStepDone = 1;
        spriteActorSetCallback(arg0, NULL);
        delayBattleAnimationCounter = battleBssBlock.m138_currentDamages[allocateJumpAnimationStructVar0].m3A_animationParam;
        break;
    case 0xF5:
        setRenderingInfoForItemUser(arg0);
        v = battleBssBlock.m138_currentDamages[allocateJumpAnimationStructVar0].m3A_animationParam;
        startJumpAnimationVar2 = (v >> 9) & 0x3F;
        arg0->mAF_animationId = 0x11;
        abilityPlayAnimation(v & 0x1FF, arg0);
        setNeedFxFragments(arg0);
        spriteActorSetPlayingAnimation(arg0, 0x13);
        allocateJumpAnimationStructCallbackSpecial0 = 1;
        break;
    case 0xF4:
        setRenderingInfoForItemUser(arg0);
        v = battleBssBlock.m138_currentDamages[allocateJumpAnimationStructVar0].m3A_animationParam;
        startJumpAnimationVar2 = (v >> 9) & 0x3F;
        arg0->mAF_animationId = 0x11;
        abilityPlayAnimation(v & 0x1FF, arg0);
        break;
    case 0xF6:
        setCameraVisibleEntities(battleBssBlock.m138_currentDamages[allocateJumpAnimationStructVar0].m16_targetBitMask);
        break;
    case 0xF3:
        jumpAnimationControlStruct->m48_isStepDone = 1;
        jumpAnimationActiveActorBF |= battleBssBlock.m138_currentDamages[allocateJumpAnimationStructVar0].m3A_animationParam;
        break;
    }
}

/* Noah processBattleAnimation (switch, jtbl 0x80070A48) */
void processBattleAnimation(SpriteActorCore* arg0) {
    s32 anim;
    SpriteActorCore* src;
    SpriteActorCore* target;
    s32 animId;
    s32 ability;
    void* p;

    if (delayBattleAnimationCounter != 0) {
        delayBattleAnimationCounter = delayBattleAnimationCounter - 1;
        return;
    }
    anim = battleBssBlock.m138_currentDamages[allocateJumpAnimationStructVar0].m47_battleAnimationToPlay;
    switch (anim) {
    case 0xF3:
    case 0xF4:
    case 0xF5:
    case 0xF6:
    case 0xF7:
    case 0xF8:
    case 0xFA:
        processBattleAnimation_subType(arg0, anim);
        break;
    case 0xF9:
        spriteActorSetPlayingAnimation(arg0, 5);
        setCameraVisibleEntities(0);
        return;
    case 0xFF:
        spriteActorSetCallback(arg0, NULL);
        jumpAnimationControlStruct->m48_isStepDone = 1;
        if (processBattleAnimationSub0() == 0) {
            if (arg0->mA8_x1C != 0) {
                return;
            }
            if (battleBssBlock.m4_visualEntities[BATTLE_ENTITY_ID(arg0)].m3_hidden != 0) {
                return;
            }
            spriteActorSetPlayingAnimation(arg0, arg0->mB0_x0_animationId);
            return;
        }
    case 0xFE:
        jumpAnimationControlStruct->m48_isStepDone = 0;
        arg0 = setupBattleAnimationSpriteCore(jumpAnimationControlStruct->m20_actor);
        getSpriteCoreForAnimation(arg0);
        spriteActorSetCallback(arg0, startJumpAnimationCallback);
        setupJumpAnimationStruct1C(5);
        if ((s16)(arg0->m0_position.vx >> 16) == battleBssBlock.m4_visualEntities[jumpAnimationControlStruct->m24_entity].mA_X
            && (s16)(arg0->m0_position.vz >> 16) == battleBssBlock.m4_visualEntities[jumpAnimationControlStruct->m24_entity].mC_Z) {
            startJumpAnimationCallback(arg0);
            return;
        }
        arg0->mA0_destination.vx = battleBssBlock.m4_visualEntities[jumpAnimationControlStruct->m24_entity].mA_X;
        arg0->mA0_destination.vz = battleBssBlock.m4_visualEntities[jumpAnimationControlStruct->m24_entity].mC_Z;
        arg0->mA0_destination.vy = 0;
        spriteActorSetPlayingAnimation(arg0, 4);
        return;
    case 0xFD:
        jumpAnimationControlStruct->m48_isStepDone = 1;
        if (battleBssBlock.m138_currentDamages[allocateJumpAnimationStructVar0].m3A_animationParam == 0) {
            jumpAnimationControlStruct->m48_isStepDone = 0;
            jumpUpdatePosition(arg0);
            spriteActorSetCallback(arg0, startJumpAnimationCallback);
        }
        allocateJumpAnimationStructVar0++;
        return;
    case 0xFC:
        jumpAnimationControlStruct->m48_isStepDone = 1;
        spriteActorSetCallback(arg0, NULL);
        spriteActorSetDefaultAnimationId(arg0, battleBssBlock.m138_currentDamages[allocateJumpAnimationStructVar0].m3A_animationParam);
        allocateJumpAnimationStructVar0++;
        return;
    case 0xFB:
        jumpAnimationControlStruct->m48_isStepDone = 1;
        spriteActorSetCallback(arg0, NULL);
        src = battleBssBlock.m8C8C_spriteActorCores[battleBssBlock.m138_currentDamages[allocateJumpAnimationStructVar0].m3A_animationParam];
        arg0->m24_vramData = src->m24_vramData;
        arg0->m7C_extra->mE_vramLocation = src->m7C_extra->mE_vramLocation;
        arg0->m3C_x1E_bundleChanged = 1;
        freeHeap(arg0->m20_object->m2C_subSpriteBuffer);
        p = allocateHeap(getAllocationSize(src->m20_object->m2C_subSpriteBuffer), 0);
        arg0->m20_object->m30_subSprites = arg0->m20_object->m2C_subSpriteBuffer = p;
        allocateJumpAnimationStructVar0++;
        return;
    default:
        target = arg0->m74_targetEntitySprite;
        attackInProgress = 0;
        setRenderingInfoForItemUser(arg0);
        spriteActorSetCallback(arg0, startJumpAnimationCallback);
        arg0->m7C_extra->m8 = battleBssBlock.m138_currentDamages[allocateJumpAnimationStructVar0].m18_damageType[BATTLE_ENTITY_ID(target)];
        if (isVramPreBacked((void*)arg0->m24_vramData->m0_spriteData) == 0) {
            if (anim >= 0x10) {
                ability = battlePreviewAbility(-1);
                if (ability < 0) {
                ability = anim - 0x10;
                ability += abilityAnimationTable0[battleBssBlock.m4_visualEntities[BATTLE_ENTITY_ID(arg0)].m2_characterId];
                }
                if (ability < battlePreviewAbilityLimit(battlePreviewAbility(-1) >= 0 ? 0 : abilityAnimationTable1[battleBssBlock.m4_visualEntities[BATTLE_ENTITY_ID(arg0)].m2_characterId])) {
                    arg0->mAF_animationId = 0x1C;
                } else {
                    arg0->mAF_animationId = 0x11;
                }
                abilityPlayAnimation(ability, arg0);
                setNeedFxFragments(arg0);
            } else {
                animId = anim;
                if (fxFragmentLoaded == 0 && battleAnimationLoadingDest != NULL) {
                    battleIdleDuringLoading();
                    arg0->m50_soundEffectSeq = loadFragmentIsNotLoadedYet();
                    spriteActorSetSpecialAnimation(arg0, battleAnimationLoadingDest);
                }
                if (jumpAnimationControlStruct->m20_actor == BATTLE_ENTITY_ID(arg0)) {
                    spriteActorSetPlayingAnimation(arg0, ~animId);
                } else {
                    spriteActorSetPlayingAnimation(arg0, animId);
                }
            }
        } else {
            animId = anim;
            spriteActorSetPlayingAnimation(arg0, animId);
            computeBattleCameraParams((1 << BATTLE_ENTITY_ID(arg0)) | (1 << BATTLE_ENTITY_ID(arg0->m74_targetEntitySprite)));
        }
        jumpAnimationControlStruct->m48_isStepDone = 0;
        allocateJumpAnimationStructVar0++;
        return;
    }
    allocateJumpAnimationStructVar0++;
}

/* Noah startJumpAnimationCallback */
void startJumpAnimationCallback(SpriteActorCore* arg0) {
    jumpAnimationControlStruct->m48_isStepDone = 1;
    jumpAnimationControlStruct->m49_completedMode = jumpAnimationControlStruct->m1C_mode;
}

/* Noah battleAnimationCallback_2Sub0 (face each other) */
void battleAnimationCallback_2Sub0(SpriteActorCore* arg0, SpriteActorCore* arg1) {
    if (arg0 != arg1) {
        setSpriteActorAngle(arg0, spriteActorCoreRatan2(arg0, arg1));
        spriteActorSetDirection(arg0, (s16)spriteActorCoreRatan2(arg0, arg1));
        if ((s8)arg1->mAF_animationId != 0x15) {
            setSpriteActorAngle(arg1, spriteActorCoreRatan2(arg1, arg0));
            spriteActorSetDirection(arg1, (s16)spriteActorCoreRatan2(arg1, arg0));
        }
    }
}

/* Noah battleAnimationCallback_2 */
void battleAnimationCallback_2(SpriteActorCore* arg0, SpriteActorCore* arg1);
void battleAnimationCallback_2(SpriteActorCore* arg0, SpriteActorCore* arg1) {
    arg0->m0_position.vx = arg0->mA0_destination.vx << 16;
    arg0->m0_position.vz = arg0->mA0_destination.vz << 16;
    jumpAnimationControlStruct->m48_isStepDone = 1;
    setupJumpAnimationStruct1C(4);
    spriteActorSetPlayingAnimation(arg0, arg0->mB0_x0_animationId);
    battleAnimationCallback_2Sub0(arg0, arg1);
}

/* Noah: battle attack-animation step (plays m47_battleAnimationToPlay).
 * The nested do/while(0) around the 0xFD call only steers register
 * allocation (loop depth weights the refs of actor so global.c ranks it
 * between core and anim, and the tail's local allocation follows); the
 * block-local j gives the shared performMechaPlayAnimation tail retail's registers.
 * JP (gcc-2.6.3): core, actor, anim are declared in that order and the
 * 0xF3.. case call sits in a one-shot loop (JP only), so all three get the
 * same priority and global.c takes them in pseudo order (core s0, actor s1,
 * anim s2). */
void processMechaBattleAnimation(void) {
    SpriteActorCore* core;
    s32 actor;
    s32 anim;
    s32 v;
    s32 i;

    if (delayBattleAnimationCounter != 0) {
        delayBattleAnimationCounter = delayBattleAnimationCounter - 1;
        return;
    }
    updateBattleAnimationDataLoading();
    setupBattleAnimationSpriteCore(battleBssBlock.m138_currentDamages[allocateJumpAnimationStructVar0].m23_battleEntityIndex);
    getSpriteCoreForAnimation(jumpAnimationControlStruct->m4_spriteCore);
    actor = jumpAnimationControlStruct->m24_entity;
    core = battleBssBlock.m8C8C_spriteActorCores[actor];
    anim = battleBssBlock.m138_currentDamages[allocateJumpAnimationStructVar0].m47_battleAnimationToPlay;
    setupJumpAnimationStruct1C(8);
    if (jumpAnimationControlStruct->m4A != 0) {
        battleBssBlock.mA73 = 0;
    }
    jumpAnimationControlStruct->m4A = 0;
    switch (anim) {
    case 0xFF:
        setupJumpAnimationStruct1C(9);
        return;
    case 0xFE:
        battleIdleDuringLoading();
        performMechaPlayAnimation(jumpAnimationControlStruct->m20_actor, battleBssBlock.m138_currentDamages[allocateJumpAnimationStructVar0].m16_targetBitMask, 4);
        setupJumpAnimationStruct1C(0xA);
        return;
    case 0xFC:
        setBattleMechaIdleAnimation(actor, battleBssBlock.m138_currentDamages[allocateJumpAnimationStructVar0].m3A_animationParam);
        setupJumpAnimationStruct1C(9);
        allocateJumpAnimationStructVar0++;
        return;
    case 0xFD:
        do {
            do {
                s32 j = allocateJumpAnimationStructVar0;

                allocateJumpAnimationStructVar0 = j + 1;
                performMechaPlayAnimation(actor, battleBssBlock.m138_currentDamages[j].m16_targetBitMask, 2);
            } while (0);
        } while (0);
        return;
    case 0xF3:
    case 0xF5:
    case 0xF6:
    case 0xF7:
    case 0xF8:
    case 0xFA:
#if VERSION_JP
        /* one-shot loop: weights core's and anim's refs for JP's allocation */
        do {
            processBattleAnimation_subType(core, anim);
        } while (0);
#else
        processBattleAnimation_subType(core, anim);
#endif
        setupJumpAnimationStruct1C(9);
        allocateJumpAnimationStructVar0++;
        return;
    case 0xFB:
        setupJumpAnimationStruct1C(9);
        swapBattleMechaSlots(BATTLE_ENTITY_ID(core), battleBssBlock.m138_currentDamages[allocateJumpAnimationStructVar0].m3A_animationParam);
        allocateJumpAnimationStructVar0++;
        return;
    case 0xF4:
        setupJumpAnimationStruct1C(9);
        i = allocateJumpAnimationStructVar0;
        allocateJumpAnimationStructVar0 = i + 1;
        v = battleBssBlock.m138_currentDamages[i].m3A_animationParam;
        startJumpAnimationVar2 = (v >> 9) & 0x3F;
        gearAbilityAnimationId = v & 0x1FF;
        return;
    default:
        jumpAnimationControlStruct->m4A = 1;
        setRenderingInfoForItemUser(core);
        {
            s32 j = allocateJumpAnimationStructVar0;

            allocateJumpAnimationStructVar0 = j + 1;
            performMechaPlayAnimation(actor, battleBssBlock.m138_currentDamages[j].m16_targetBitMask, anim);
        }
        return;
    }
}

/* Noah allocateJumpAnimationStructCallback (switch, jtbl 0x80070AB8) */
void allocateJumpAnimationStructCallback(sJumpAnimationControlStruct* arg0);
void allocateJumpAnimationStructCallback(sJumpAnimationControlStruct* arg0) {
    SpriteActorCore* core;
    SpriteActorCore* target;
    SpriteActorCore* p;
    s32 id;
    s32 dist;

    if (isInJumpAnimationCallbackCode != 0) {
        return;
    }
    isInJumpAnimationCallbackCode = 1;
    jumpAnimationControlStruct = arg0;
    core = arg0->m4_spriteCore;
    target = arg0->m4C_target;
    if (needFxFragments != NULL) {
        needFxFragments = NULL;
        if (initFxFragments()) {
            spriteActorSetCallback(core, startJumpAnimationCallback);
        } else {
            if (BATTLE_ENTITY_ID(core) < 3) {
                if (allocateJumpAnimationStructCallbackSpecial0 != 0) {
                    spriteActorSetPlayingAnimation(core, 0x13);
                } else {
                    spriteActorSetPlayingAnimation(core, 0x12);
                }
                allocateJumpAnimationStructCallbackSpecial0 = 0;
            }
            if (core->m48_defaultAnimationBundle != NULL) {
                setupJumpAnimationStruct1C(7);
            }
        }
    }
    if (jumpAnimationControlStruct->m34_pendingSteps != 0) {
        processMechaBattleAnimation();
        jumpAnimationControlStruct->m34_pendingSteps--;
    }
    if (jumpAnimationControlStruct->m49_completedMode != 0) {
        switch (jumpAnimationControlStruct->m49_completedMode) {
        case 4:
            p = pSpriteCoreListHead[0];
            id = BATTLE_ENTITY_ID(p);
            if (id < 3 && battleBssBlock.m4_visualEntities[id].m4_isGear == 0
                && battleBssBlock.m138_currentDamages[allocateJumpAnimationStructVar0 - 1].m18_damageType[id] == 7) {
                setRenderingInfoForItemUser(p);
                spriteActorSetPlayingAnimation(p, 0x1B);
                while ((s8)p->mAF_animationId == 0x1B) {
                    battleRender();
                }
            }
            break;
        case 6:
            jumpUpdatePositionSub0(core, core->m74_targetEntitySprite);
            break;
        case 2:
            battleAnimationCallback_2(core, core->m74_targetEntitySprite);
            break;
        case 5:
            if (battleBssBlock.m4_visualEntities[BATTLE_ENTITY_ID(core)].m3_hidden == 0) {
                spriteActorSetPlayingAnimation(core, core->mB0_x0_animationId);
            }
            battleAnimationCallback_5(jumpAnimationControlStruct->m24_entity);
            setupJumpAnimationStruct1C(0xA);
            break;
        }
        jumpAnimationControlStruct->m49_completedMode = 0;
    }
    switch (jumpAnimationControlStruct->m1C_mode) {
    case 7:
        if (allocateSavePointMeshDataSub0_var0 == isDamageDisplayPolysTask2Running()) {
            setupJumpAnimationStruct1C(4);
            jumpAnimationControlStruct->m48_isStepDone = 1;
            updateBattleAnimationDataLoading();
        }
        break;
    case 10:
        if (allocateSavePointMeshDataSub0_var0 == isDamageDisplayPolysTask2Running()) {
            deleteJumpAnimationControlStructEndOfAttack();
        }
        break;
    case 9:
        processMechaBattleAnimation();
        break;
    case 8:
        if (battleAnimGearUnloadMode != 0 && allocateSavePointMeshDataSub0_var0 == isDamageDisplayPolysTask2Running()) {
            if (startJumpAnimationVar1 != 0) {
                if (battleAnimGearUnloadMode == 1) {
                    jumpAnimationControlStruct->m34_pendingSteps++;
                }
                battleRestartReservedMechaLoads();
            }
        }
        break;
    case 2: {
        Vec2s16 pos;
        Vec2s16 dest;
        pos.vx = core->m0_position.vx >> 16;
        pos.vy = core->m0_position.vz >> 16;
        dest.vx = core->mA0_destination.vx;
        dest.vy = core->mA0_destination.vz;
        dist = computerJumpDistanceToTarget(pos, dest);
        if (jumpAnimationControlStruct->m44_distanceToTarget < dist) {
            battleAnimationCallback_2(core, target);
        } else {
            jumpAnimationControlStruct->m44_distanceToTarget = dist;
        }
        break;
    }
    case 6: {
        Vec2s16 pos;
        Vec2s16 dest2;
        pos.vx = core->m0_position.vx >> 16;
        pos.vy = core->m0_position.vz >> 16;
        dest2.vx = core->mA0_destination.vx;
        dest2.vy = core->mA0_destination.vz;
        dist = computerJumpDistanceToTarget(pos, dest2);
        if (jumpAnimationControlStruct->m44_distanceToTarget < dist) {
            jumpUpdatePositionSub0(core, target);
        } else {
            jumpAnimationControlStruct->m44_distanceToTarget = dist;
        }
        break;
    }
    case 4:
        if (jumpAnimationControlStruct->m48_isStepDone != 0) {
            startPlayingAttackAnimation();
        }
        break;
    }
    isInJumpAnimationCallbackCode = 0;
}

/* Noah clearAnimationsEndOfTurn */
void clearAnimationsEndOfTurn(s32 arg0) {
    allocateSavePointMeshDataSub0_var0 = 0;
    spriteBytecode2ExtendedE0_Var0 = 0;
    clearAnimationsEndOfTurnSub();
    if (battleBssBlock.m4_visualEntities[arg0].m4_isGear != 0) {
        clearBattleAnimationData();
        waitBattleAnimationSoundLoaded();
    } else if (arg0 < 3) {
        if (battleBssBlock.m8C8C_spriteActorCores[arg0] != NULL) {
            startReadingBattleJumpAnimation(battleBssBlock.m8C8C_spriteActorCores[arg0]);
        }
    } else {
        clearBattleAnimationData();
    }
    setBattleCameraParamX(0xC0);
}

/* Noah computeJumpXZStep (direction + XZ step from speed) */
void computeJumpXZStep(SpriteActorCore* arg0, s32 arg1) {
    s32 speed;

    speed = arg0->m18_moveSpeed >> 3;
    arg0->m32_direction = arg1;
    arg0->mC_step.vx = ((getAngleSin((s16)arg1) >> 1) * speed) >> 8;
    arg0->mC_step.vz = -((getAngleCos(arg0->m32_direction) >> 1) * speed) >> 8;
}

/* jump arc setup to mA0 target (terrain height, ratan2, vertical speed) */
void computeSpriteJumpArc(SpriteActorCore* arg0);
void computeSpriteJumpArc(SpriteActorCore* arg0) {
    VECTOR delta;
    SVECTOR dest;
    VECTOR normal;
    s32 index;
    s32 dy;
    s32 angle;
    s32 dist;

    setSVector3(&dest, arg0->mA0_destination.vx, arg0->mA0_destination.vy, arg0->mA0_destination.vz);
    index = findBattleTerrainTriangle(&dest, arg0->m78_terrainTriangle, 4);
    if (index < 0) {
        index = findBattleTerrainTriangleBruteForce(&dest);
    }
    computeTerrainHeightAtTriangle(&dest, index, &normal);
    if (dest.vy > arg0->mA0_destination.vy) {
        dest.vy = arg0->mA0_destination.vy;
    }
    dy = ((dest.vy << 16) - arg0->m0_position.vy) >> 16;
    delta.vx = arg0->mA0_destination.vx - (s16)(arg0->m0_position.vx >> 16);
    delta.vz = arg0->mA0_destination.vz - (s16)(arg0->m0_position.vz >> 16);
    angle = -ratan2(delta.vz, delta.vx);
    Square0(&delta, &delta);
    dist = SquareRoot0(delta.vx + delta.vz);
    arg0->mC_step.vy = R3000_DIV((-arg0->m1C_gravity * dist) << 4, arg0->m18_moveSpeed >> 11) + R3000_DIV(arg0->m18_moveSpeed * dy, dist);
    computeJumpXZStep(arg0, (s16)angle);
}

/* Noah initSpriteJumpToTarget */
void initSpriteJumpToTarget(SpriteActorCore* arg0);
void initSpriteJumpToTarget(SpriteActorCore* arg0) {
    VECTOR delta;
    SVECTOR dest;
    VECTOR normal;
    s32 speed;
    s32 angle;
    s32 dist;
    s32 index;

    speed = -R3000_DIV(arg0->mC_step.vy << 1, arg0->m1C_gravity);
    arg0->m0_position.vx &= 0xFFFF0000;
    arg0->m0_position.vy &= 0xFFFF0000;
    arg0->m0_position.vz &= 0xFFFF0000;
    delta.vx = arg0->mA0_destination.vx - (s16)(arg0->m0_position.vx >> 16);
    delta.vz = arg0->mA0_destination.vz - (s16)(arg0->m0_position.vz >> 16);
    delta.vy = 0;
    angle = -ratan2(delta.vz, delta.vx);
    Square0(&delta, &delta);
    dist = SquareRoot0(delta.vx + delta.vz);
    if (speed != 0) {
        arg0->m18_moveSpeed = (dist << 16) / speed;
    } else {
        arg0->m18_moveSpeed = 0;
    }
    setSVector3(&dest, arg0->mA0_destination.vx, arg0->mA0_destination.vy, arg0->mA0_destination.vz);
    index = findBattleTerrainTriangle(&dest, arg0->m78_terrainTriangle, 4);
    if (index < 0) {
        index = findBattleTerrainTriangleBruteForce(&dest);
    }
    computeTerrainHeightAtTriangle(&dest, index, &normal);
    if (dest.vy > arg0->mA0_destination.vy) {
        dest.vy = arg0->mA0_destination.vy;
    }
    dist = (dest.vy << 16) - arg0->m0_position.vy;
    if (speed != 0) {
        arg0->mC_step.vy += dist / speed;
    }
    computeJumpXZStep(arg0, (s16)angle);
    applySpriteVerticalMovement(arg0);
}

/* snap actor to battle terrain (triangle index m78, height m84) */
void sampleBattleTerrainHeight(SpriteActorCore* arg0) {
    SVECTOR pos;
    VECTOR normal;
    s32 index;

    pos.vx = arg0->m0_position.vx >> 16;
    pos.vy = arg0->m0_position.vy >> 16;
    pos.vz = arg0->m0_position.vz >> 16;
    index = findBattleTerrainTriangle(&pos, arg0->m78_terrainTriangle, 4);
    if (index < 0) {
        index = findBattleTerrainTriangleBruteForce(&pos);
    }
    computeTerrainHeightAtTriangle(&pos, index, &normal);
    arg0->m84_maxY = pos.vy;
    arg0->m78_terrainTriangle = index;
}

/* Noah allocateBattleSpriteActor (0x19C task: update BAC50, draw BAB0C, delete BABDC) */
sBattleSpriteActor* allocateBattleSpriteActor(AnimationBundle* bundle, s16 clutX, s16 clutY, s16 vramX, s16 vramY, s16 arg5, s16 x, s16 y, s16 z, s16 animationId, s16 direction, s32 arg11, s32 arg12, s32 vramParam);
sBattleSpriteActor* allocateBattleSpriteActor(AnimationBundle* bundle, s16 clutX, s16 clutY, s16 vramX, s16 vramY, s16 arg5, s16 x, s16 y, s16 z, s16 animationId, s16 direction, s32 arg11, s32 arg12, s32 vramParam) {
    sBattleSpriteActor* task;
    SpriteActor* actor;

    task = (sBattleSpriteActor*)allocateTaskPair((s32)sizeof(sBattleSpriteActor), NULL, battleSpriteUpdate, battleSpriteRender, battleSpriteDelete);
    actor = &task->m38_spriteActor;
    task->m0_updateTask.m4_data = actor;
    task->m1C_drawTask.m4_data = actor;
    task->m1C_drawTask.m0_owner = NULL;
    initBattleSpriteActorVram(actor, bundle, clutX, clutY, vramX, vramY, arg5, vramParam);
    actor->m0_spriteActorCore.m6C_owner = task;
    actor->m0_spriteActorCore.m0_position.vx = x << 16;
    actor->m0_spriteActorCore.m0_position.vy = y << 16;
    actor->m0_spriteActorCore.m0_position.vz = z << 16;
    actor->m0_spriteActorCore.mB0_x0_animationId = animationId;
    actor->m0_spriteActorCore.m3C_x2 = 1;
    actor->m0_spriteActorCore.m32_direction = direction;
    setGraphicEntityScale(&actor->m0_spriteActorCore, 0x2000);
    actor->m0_spriteActorCore.m82_entityScale = 0x2000;
    actor->m0_spriteActorCore.m78_terrainTriangle = 0;
    spriteActorSetPlayingAnimation(&actor->m0_spriteActorCore, animationId);
    return task;
}

void pcMouseBattleSprite(const void* actor, SVECTOR* v); /* port/src/system/mouse_battle.c */
/* Noah battleSpriteRender (unused VECTOR local reserves a 16-byte frame slot) */
void battleSpriteRender(TaskHeader* arg0) {
    SVECTOR v;
    s32 sxy;
    s32 flag;
    SpriteActorCore* actor;
    s32 ot;

    if (battleSpritesDisabled == 0) {
        actor = arg0->m4_data;
        if (battlePreviewHide(actor)) return;
        v.vx = actor->m0_position.vx >> 16;
        v.vy = actor->m0_position.vy >> 16;
        v.vz = actor->m0_position.vz >> 16;
        SetRotMatrix(&battleCameraBlock.m20_renderingMatrix);
        SetTransMatrix(&battleCameraBlock.m20_renderingMatrix);
        ot = (RotTransPers(&v, &sxy, &sxy, &flag) >> gDepthDivider) + actor->m30_otOffset;
        if (flag & 0x8000) {
            ot = 0;
        }
        actor->m2E_otIndex = ot;
        pcMouseBattleSprite(actor, &v); /* where it is on the screen: a click can choose it (mouse_battle.c) */
        if (ot - 1U < 0xFFF) {
            VECTOR unused; /* unused: reserves retail's 16-byte frame slot after flag */
            renderSpriteActor(actor, &characterRenderingOT[ot]);
        }
    }
}

/* Noah battleSpriteDelete */
void battleSpriteDelete(TaskHeader* arg0) {
    sBattleSpriteActor* task = (sBattleSpriteActor*)arg0;
    SpriteActor* actor = &task->m38_spriteActor;

    if (actor->m0_spriteActorCore.m20_object->m2C_subSpriteBuffer != NULL) {
        freeHeap(actor->m0_spriteActorCore.m20_object->m2C_subSpriteBuffer);
    }
    registerSpriteCallback2_2(&task->m0_updateTask);
    savePointCallbackC_sub1(&actor->m0_spriteActorCore);
    registerSpriteCallback2Sub0(&task->m1C_drawTask);
    allocateSavePointMeshDataSub0_callback(&task->m0_updateTask);
    freeHeap(task);
}

/* Noah battleSpriteUpdate */
void battleSpriteUpdate(TaskHeader* arg0) {
    SpriteActorCore* actor = arg0->m4_data;

    if (battleSpritesDisabled == 0) {
        OP_INIT_ENTITY_SCRIPT_sub0Sub9(actor);
        updateSpriteMovement(actor);
        if (actor->mAC_x6) {
            OP_INIT_ENTITY_SCRIPT_sub0Sub9(actor);
            updateSpriteMovement(actor);
        }
    }
}

/* project a battle actor to screen (x, y, depth, box) with the battle matrix */
void projectBattleActorToScreen(s32 entity, s16* sx, s16* sy, s16* depth, s16* arg4, s16* arg5, s16* arg6);
void projectBattleActorToScreen(s32 entity, s16* sx, s16* sy, s16* depth, s16* arg4, s16* arg5, s16* arg6) {
    SVECTOR v;
    Vec2s16 sxy;
    s32 flag;
    SpriteActorCore* actor;

    actor = battleBssBlock.m8C8C_spriteActorCores[entity];
    v.vx = actor->m0_position.vx >> 16;
    v.vy = actor->m0_position.vy >> 16;
    v.vz = actor->m0_position.vz >> 16;
    PushMatrix();
    SetRotMatrix(&battleCameraBlock.m20_renderingMatrix);
    SetTransMatrix(&battleCameraBlock.m20_renderingMatrix);
    *depth = RotTransPers(&v, (s32*)&sxy, &flag, &flag) >> 4;
    *sx = sxy.vx;
    *sy = sxy.vy;
    *arg4 = sxy.vx - 0x30;
    *arg5 = 0x30;
    *arg6 = sxy.vx - 0x18;
    PopMatrix();
}

/* Noah releaseSpriteActor */
void releaseSpriteActor(s32 arg0) {
    sBattleSpriteActor* task;

    task = battleBssBlock.m8CB8_spriteActors[arg0];
    if (task != NULL) {
        findOrDeleteStatusEffectSprite((SpriteActorCore*)task, 0, 2);
        if (arg0 < 3) {
            if (battleBssBlock.m8D24_visualBuffers[arg0].m0_spriteData != NULL) {
                freeHeap(battleBssBlock.m8D24_visualBuffers[arg0].m0_spriteData);
            }
            battleBssBlock.m8D24_visualBuffers[arg0].m0_spriteData = NULL;
        }
        task->m0_updateTask.mC_deleteCallback(&task->m0_updateTask);
        registerSpriteCallback2_2(&task->m0_updateTask);
        battleBssBlock.m8C8C_spriteActorCores[arg0] = NULL;
        battleBssBlock.m8CB8_spriteActors[arg0] = NULL;
    }
}

/* Noah battleAnimationCallback_5 (face default direction) */
void battleAnimationCallback_5(s32 arg0) {
    SpriteActorCore* actor;
    s16 dir;

    actor = battleBssBlock.m8C8C_spriteActorCores[arg0];
    if ((s8)actor->mAF_animationId != 0x15) {
        dir = (battleBssBlock.m4_visualEntities[arg0].m6_direction != 0) << 11;
        setSpriteActorAngle(actor, dir);
        spriteActorSetDirection(actor, dir);
    }
}

/* Noah stopBattleVibration (empty) */
void stopBattleVibration(u8 entity, s32 mask) {
}

/* Noah callGearSwapSprite */
void callGearSwapSprite(s32 arg0) {
    s32 saved;
    SpriteActorCore* actor;

    setCameraVisibleEntities(1 << arg0);
    setCameraVisibleEntities(0);
    saved = allocateSavePointMeshDataSub0_var1;
    actor = battleBssBlock.m8C8C_spriteActorCores[arg0];
    clearBattleAnimationData();
    spriteActorSetPlayingAnimation(actor, 0x16);
    while (actor->m9E_wait != 0 && (s8)actor->mAF_animationId == 0x16) {
        battleRender();
    }
    while (allocateSavePointMeshDataSub0_var1 != saved) {
        battleRender();
    }
    releaseSpriteActor(arg0);
    battleRender();
    battleRender();
    createMechaLoadingTask(arg0);
    while (mainBattleSpriteCallback_phase5Var != 0) {
        battleRender();
    }
}

/* delete the party sprite actors without a default bundle except arg0, then dismissPartyGears(arg0) */
void removeOtherPartyActors(s32 arg0) {
    s32 i;
    SpriteActorCore* actor;
    TaskHeader* owner;

    for (i = 0; i != 3; i++) {
        if (i != arg0) {
            actor = battleBssBlock.m8C8C_spriteActorCores[i];
            if (actor != NULL && actor->m48_defaultAnimationBundle == NULL) {
                owner = actor->m6C_owner;
                owner->mC_deleteCallback(owner);
                battleBssBlock.m8C8C_spriteActorCores[i] = NULL;
                battleBssBlock.m8CB8_spriteActors[i] = NULL;
            }
        }
    }
    dismissPartyGears(arg0);
}

/* update callback of a sprite attached to a gear (follows bone 0 translation) */
void battleMechaUpdate(TaskHeader* arg0) {
    SVECTOR unused; /* unused: retail's frame has an 8-byte local slot */
    SpriteActorCore* actor;
    sLoadedMechas* mecha;

    actor = arg0->m4_data;
    mecha = battleMechas[BATTLE_ENTITY_ID(actor)];
    if (mecha != NULL) {
        if (actor->m9E_wait == 0) {
            actor->m64_spriteByteCode = NULL;
        }
        if (actor->m64_spriteByteCode != NULL) {
            OP_INIT_ENTITY_SCRIPT_sub0Sub9(actor);
            updateSpriteMovement(actor);
            if (actor->mAC_x6) {
                OP_INIT_ENTITY_SCRIPT_sub0Sub9(actor);
                updateSpriteMovement(actor);
            }
        }
        actor->m0_position.vx = mecha->m4_bones->m5C_translation[0] << 16;
        actor->m0_position.vy = mecha->m4_bones->m5C_translation[1] << 16;
        actor->m0_position.vz = mecha->m4_bones->m5C_translation[2] << 16;
    }
}

/* draw callback of the gear-attached sprite (OT depth only) */
void battleMechaDraw(TaskHeader* arg0) {
    SVECTOR v;
    s32 sxy;
    s32 flag;
    SpriteActorCore* actor;
    s32 ot;

    actor = arg0->m4_data;
    actor->m36_height = initMechaForBattleSub0_battle(BATTLE_ENTITY_ID(actor));
    actor->m38_centerHeight = (u16)actor->m36_height / 2;
    v.vx = actor->m0_position.vx >> 16;
    v.vy = actor->m0_position.vy >> 16;
    v.vz = actor->m0_position.vz >> 16;
    SetRotMatrix(&battleCameraBlock.m20_renderingMatrix);
    SetTransMatrix(&battleCameraBlock.m20_renderingMatrix);
    ot = (RotTransPers(&v, &sxy, &sxy, &flag) >> gDepthDivider) + actor->m30_otOffset;
    if (flag & 0x8000) {
        ot = 0;
    }
    actor->m2E_otIndex = ot;
}

/* delete callback of the gear-attached sprite (unused SVECTOR keeps retail's frame) */
void battleMechaDelete(TaskHeader* arg0) {
    SVECTOR unused; /* unused: retail's frame has an 8-byte local slot */
    sBattleSpriteActor* task = (sBattleSpriteActor*)arg0;

    registerSpriteCallback2Sub0(&task->m1C_drawTask);
    allocateSavePointMeshDataSub0_callback(&task->m0_updateTask);
    freeHeap(task);
}

/* create the sprite actor attached to a gear for entity arg0 (update BB13C, draw BB248, delete BB314) */
void initMechaForBattle(u32 arg0) {
    sBattleSpriteActor* task;
    SpriteActor* actor;
    u8 saved;

    if (battleBssBlock.m8C8C_spriteActorCores[arg0] == NULL) {
        saved = spriteBytecode2ExtendedE0_Var0;
        spriteBytecode2ExtendedE0_Var0 = 0;
        task = (sBattleSpriteActor*)allocateTaskPair((s32)sizeof(sBattleSpriteActor), NULL, battleMechaUpdate, battleMechaDraw, battleMechaDelete);
        actor = &task->m38_spriteActor;
        actor->m0_spriteActorCore.m6C_owner = task;
        task->m0_updateTask.m4_data = actor;
        task->m1C_drawTask.m4_data = actor;
        initFieldEntitySub4Sub1(&actor->m0_spriteActorCore);
        initFieldEntitySub4Sub2(actor);
        actor->m0_spriteActorCore.m40_draw.bits.xD_mode = 4;
        actor->m0_spriteActorCore.m6C_owner = task;
        actor->m0_spriteActorCore.m3C_x0_type = 0;
        actor->m0_spriteActorCore.mA8_x0 = 0;
        actor->m0_spriteActorCore.m7C_extra->m8 = 0;
        actor->m0_spriteActorCore.m7C_extra->mC_animationSignal = 0;
        actor->m0_spriteActorCore.m82_entityScale = battleDefaultEntityScale;
        actor->m0_spriteActorCore.m0_position.vx = battleBssBlock.m4_visualEntities[arg0].mA_X << 16;
        actor->m0_spriteActorCore.m0_position.vz = battleBssBlock.m4_visualEntities[arg0].mC_Z << 16;
        actor->m0_spriteActorCore.m0_position.vy = 0;
        actor->m0_spriteActorCore.m36_height = initMechaForBattleSub0_battle(arg0);
        actor->m0_spriteActorCore.m82_entityScale = 0x2000;
        actor->m0_spriteActorCore.m38_centerHeight = (u16)actor->m0_spriteActorCore.m36_height / 2;
        setGraphicEntityScale(&actor->m0_spriteActorCore, 0x2000);
        actor->m0_spriteActorCore.m24_vramData = &createSavePointMeshData_mode5;
        battleBssBlock.m8C8C_spriteActorCores[arg0] = &actor->m0_spriteActorCore;
        battleBssBlock.m8CB8_spriteActors[arg0] = task;
        actor->m0_spriteActorCore.m4C_specialAnimation = NULL;
        actor->m0_spriteActorCore.m48_defaultAnimationBundle = NULL;
        spriteBytecode2ExtendedE0_Var0 = saved;
#if VERSION_JP
        actor->m0_spriteActorCore.mAC_x0 = arg0;
#else
        actor->m0_spriteActorCore.mA8_x1E = arg0;
        actor->m0_spriteActorCore.mAC_x0 = arg0 >> 2;
#endif
    }
}

/* Noah finalizeBattleMechaLoading */
void finalizeBattleMechaLoading(sBattleMechaLoaderTask* arg0);
void finalizeBattleMechaLoading(sBattleMechaLoaderTask* arg0) {
    s32 i;
    u32 mask;

    for (i = 0, mask = 1; i != 3; i++, mask <<= 1) {
        if ((battleMechaTextureSlotsUsed & mask) == 0) {
            battleMechaTextureSlotsUsed |= mask;
            break;
        }
    }
    finalizeBattleMechaLoadingCommands(arg0->m1C_entity, battleMechaPartyTextureLocations[i][0], battleMechaPartyTextureLocations[i][1], 0, arg0->m1C_entity + 0x1C0);
    preventGearLoadingInBattle--;
    initMechaForBattle(arg0->m1C_entity);
    arg0->m0_task.mC_deleteCallback(&arg0->m0_task);
    if (--mainBattleSpriteCallback_phase5Var == 0) {
        finalizeBattleMechaLoadingVar0 = 1;
    }
}

/* Noah mechaLoadingTask_phase1 (runs on a scratch stack: STACK_SWITCH) */
void mechaLoadingTask_phase1(TaskHeader* arg0) {
    u8* stack;

    if (isCDBusy() == 0) {
        stack = allocateHeap(0x1000, 1);
        STACK_SWITCH(stack + 0xF00);
        finalizeBattleMechaLoading((sBattleMechaLoaderTask*)arg0);
        STACK_RESTORE();
        freeHeap(stack);
    }
}

/* Noah startBattleGearLoading */
void startBattleGearLoading(TaskHeader* arg0) {
    createGearLoadingFileRequests(((sBattleMechaLoaderTask*)arg0)->m1C_entity);
    preventGearLoadingInBattle++;
    setTaskUpdateFunction(arg0, mechaLoadingTask_phase1);
}

/* Noah mechaLoadingTask_phase0 (scratch stack) */
void mechaLoadingTask_phase0(TaskHeader* arg0) {
    u8* stack;

    if (isCDBusy() == 0 && preventGearLoadingInBattle == 0) {
        stack = allocateHeap(0x1000, 1);
        STACK_SWITCH(stack + 0xF00);
        startBattleGearLoading(arg0);
        STACK_RESTORE();
        freeHeap(stack);
    }
}

/* Noah createMechaLoadingTask */
void createMechaLoadingTask(s32 arg0) {
    u8 saved;
    sBattleMechaLoaderTask* task;

    saved = spriteBytecode2ExtendedE0_Var0;
    spriteBytecode2ExtendedE0_Var0 = 0;
    battleScriptLoaderTaskCreationInProgress = 1;
    task = (sBattleMechaLoaderTask*)allocateUpdateTask(NULL, (s32)sizeof(sBattleMechaLoaderTask) - (s32)sizeof(TaskHeader));
    setTaskUpdateFunction(&task->m0_task, mechaLoadingTask_phase0);
    task->m1C_entity = arg0;
    battleScriptLoaderTaskCreationInProgress = 0;
    mainBattleSpriteCallback_phase5Var++;
    spriteBytecode2ExtendedE0_Var0 = saved;
}

/* battle camera init (mode vars, setBattleCameraMode(0)) - Noah initBattle3dRendering part */
void resetBattleCameraMode(void) {
    battleIR0 = 0x200;
    allEntitiesToFitInView = -1;
    battleCameraControlSpriteActiveCount = 0;
    battleCameraModeInterpolation = 1;
    setBattleCameraMode(0);
}

/* look-at matrix from eye/at/up (VectorNormal/OuterProduct12) - Noah lookAtNoDivide-like */
void lookAtNoDivide(MATRIX* m, SVECTOR* eye, SVECTOR* at, SVECTOR* up) {
    VECTOR v;
    VECTOR z;
    VECTOR x;
    VECTOR y;

    setVector3(&v, at->vx - eye->vx, at->vy - eye->vy, at->vz - eye->vz);
    y.vx = up->vx;
    y.vy = up->vy;
    y.vz = up->vz;
    VectorNormal(&v, &z);
    OuterProduct12(&y, &z, &v);
    VectorNormal(&v, &x);
    OuterProduct12(&z, &x, &v);
    VectorNormal(&v, &y);
    m->m[0][0] = x.vx;
    m->m[0][1] = x.vy;
    m->m[0][2] = x.vz;
    m->m[1][0] = y.vx;
    m->m[1][1] = y.vy;
    m->m[1][2] = y.vz;
    m->m[2][0] = z.vx;
    m->m[2][1] = z.vy;
    m->m[2][2] = z.vz;
    PushMatrix();
    ApplyMatrix(m, eye, &v);
    m->t[0] = -v.vx;
    m->t[1] = -v.vy;
    m->t[2] = -v.vz;
    PopMatrix();
}

/* ---- Enhanced: the 60 Hz battle (xg-hbc.19.2, docs/ENGINE_PLAN.md "60 Hz battle") ----
 * The battle runs every vblank, but the Gears' animation program steps every
 * other one (renderMechasBattle's half-step accumulator), and with it what it
 * moves: the Gears (port/src/game/battle/gearpose.c draws them halfway on a
 * step frame) and, in a Gear's attack, the camera (its event tracks,
 * updateMechaEventAnim). The game draws the scene with the camera from before
 * the frame's step; with battle60, on a frame whose step moves the camera,
 * the scene waits for the step and is drawn from the camera halfway between
 * before and after it, as the Gears are, the game's camera untouched: on a step frame the picture is halfway, on the
 * next the game's. The rendering matrices go back to the game's once the
 * frame is drawn. */
static s32 pcBattle60Defer;       /* the scene waits for the Gears' step */
static s32 pcBattle60Restore;     /* the rendering matrices are the picture's until the frame ends */
static SVECTOR pcBattle60Eye;     /* the camera before the step */
static SVECTOR pcBattle60At;
static MATRIX pcBattle60Matrix;   /* the game's rendering matrices */
static MATRIX pcBattle60Current;
SVECTOR pcBattle60DrawnEye;       /* what the frame was drawn from (the oracle's BR line) */
SVECTOR pcBattle60DrawnAt;
extern MATRIX currentRenderingMatrix;
int pcGameplayBattle60(void);
void hwClockHold(int on); /* port/src/system/hw.c */

s32 pcBattle60CameraStep(void); /* b_800A8B0C.c */
void pcBattle60SceneBegin(void) {
    pcBattle60Defer = pcGameplayBattle60() && pcBattle60CameraStep();
    pcBattle60Restore = 0;
    pcBattle60Eye = battleCameraEye;
    pcBattle60At = battleCameraAt;
    pcBattle60DrawnEye = battleCameraEye;
    pcBattle60DrawnAt = battleCameraAt;
}
static s16 pcBattle60Mid(s16 a, s16 b) {
    s32 d = b - a;
    if (d > 0x800 || d < -0x800) return b; /* a cut: drawn where it is */
    return (s16)(a + d / 2);
}
/* renderMechasBattle, after the Gears' step: the scene from the halfway camera. */
void pcBattle60SceneAfterStep(void) {
    SVECTOR eye;
    SVECTOR at;

    if (!pcBattle60Defer) return;
    pcBattle60Defer = 0;
    eye = battleCameraEye;
    at = battleCameraAt;
    if (eye.vx != pcBattle60Eye.vx || eye.vy != pcBattle60Eye.vy || eye.vz != pcBattle60Eye.vz ||
        at.vx != pcBattle60At.vx || at.vy != pcBattle60At.vy || at.vz != pcBattle60At.vz) {
        eye.vx = pcBattle60Mid(pcBattle60Eye.vx, eye.vx);
        eye.vy = pcBattle60Mid(pcBattle60Eye.vy, eye.vy);
        eye.vz = pcBattle60Mid(pcBattle60Eye.vz, eye.vz);
        at.vx = pcBattle60Mid(pcBattle60At.vx, at.vx);
        at.vy = pcBattle60Mid(pcBattle60At.vy, at.vy);
        at.vz = pcBattle60Mid(pcBattle60At.vz, at.vz);
        pcBattle60Matrix = battleCameraBlock.m20_renderingMatrix;
        pcBattle60Current = currentRenderingMatrix;
        pcBattle60Restore = 1;
        hwClockHold(1); /* the PS1 does not build this matrix: no time on the stepped clock */
        lookAtNoDivide(&battleCameraBlock.m20_renderingMatrix, &eye, &at, &battleCameraUp);
        hwClockHold(0);
        battleCameraBlock.m20_renderingMatrix.t[0] += battleSceneRenderingMatrixOffset.vx;
        battleCameraBlock.m20_renderingMatrix.t[1] += battleSceneRenderingMatrixOffset.vy;
        battleCameraBlock.m20_renderingMatrix.t[2] += battleSceneRenderingMatrixOffset.vz;
        currentRenderingMatrix = battleCameraBlock.m20_renderingMatrix;
    } else {
        eye = pcBattle60Eye;
        at = pcBattle60At;
    }
    pcBattle60DrawnEye = eye;
    pcBattle60DrawnAt = at;
    if (renderBattleSceneDisabled == 0) {
        battleDrawEnv(&battleCameraBlock.m20_renderingMatrix, 0, 0, battleBssBlock.m8C54_currentOT, battleBssBlock.m8C84_oddOrEven, &eye, &at, 0x1000);
    }
}
/* Once the frame is drawn: the game's rendering matrices back. */
void pcBattle60SceneEnd(void) {
    pcBattle60Defer = 0;
    if (pcBattle60Restore) {
        battleCameraBlock.m20_renderingMatrix = pcBattle60Matrix;
        currentRenderingMatrix = pcBattle60Current;
        pcBattle60Restore = 0;
    }
}

/* build battle rendering matrix from camera eye/at + offset, render scene */
void renderBattleScene(void);
void renderBattleScene(void) {
    BattleCamera_800D309C* cam;

    lookAtNoDivide(&battleCameraBlock.m20_renderingMatrix, &battleCameraEye, &battleCameraAt, &battleCameraUp);
    cam = &battleCameraBlock;
    cam->m20_renderingMatrix.t[0] += battleSceneRenderingMatrixOffset.vx;
    cam->m20_renderingMatrix.t[1] += battleSceneRenderingMatrixOffset.vy;
    cam->m20_renderingMatrix.t[2] += battleSceneRenderingMatrixOffset.vz;
    if (pcBattle60Defer) {
        /* Enhanced, 60 Hz: drawn after the Gears' step (pcBattle60SceneAfterStep) */
    } else if (renderBattleSceneDisabled == 0) {
        battleDrawEnv(&battleCameraBlock.m20_renderingMatrix, 0, 0, battleBssBlock.m8C54_currentOT, battleBssBlock.m8C84_oddOrEven, &battleCameraEye, &battleCameraAt, 0x1000);
    }
}

/* Noah updateBattleCamera (GTE gpf12 via gte.h; unused locals keep retail's frame layout) */
void updateBattleCamera(void);
void updateBattleCamera(void) {

    switch (battleCameraMode) {
    case 0:
        break;
    case 1:
        computeBattleCameraParams(allEntitiesToFitInView);
        break;
    case 2:
        fpToSVector3(&battleCameraBlock.m4_eyeTarget, &previousCameraEye2);
        fpToSVector3(&battleCameraBlock.mC_atTarget, &previousCameraAt2);
        break;
    case 3: {
        SVECTOR at;
        SVECTOR eye;

        eye.vx = at.vx = battleCameraFollowActor->m0_position.vx >> 16;
        at.vy = battleCameraFollowActor->m0_position.vy >> 16;
        at.vz = battleCameraFollowActor->m0_position.vz >> 16;
        eye.vz = at.vz - (getAngleSin(battleCameraFollowAngle) * battleCameraFollowDistance) / 4096;
        eye.vy = at.vy - (getAngleCos(battleCameraFollowAngle) * battleCameraFollowDistance) / 4096;
        battleCameraBlock.m4_eyeTarget = eye;
        battleCameraBlock.mC_atTarget = at;
        break;
    }
    }
    if (battleCameraModeInterpolation == 1) {
        gte_lddp(battleIR0);
#if VERSION_JP
        /* JP reads the targets through a pointer, the current eye/at directly */
        {
            VECTOR difference;
            SVECTOR* tgt = &battleCameraBlock.m4_eyeTarget;

            difference.vx = tgt->vx - battleCameraEye.vx;
            difference.vy = tgt->vy - battleCameraEye.vy;
            difference.vz = tgt->vz - battleCameraEye.vz;
            gte_ldlvl(&difference);
            gte_gpf12();
            gte_stlvl(&difference);
            if (difference.vx | difference.vz | difference.vy) {
                battleCameraEye.vx += difference.vx;
                battleCameraEye.vy += difference.vy;
                battleCameraEye.vz += difference.vz;
            } else {
                battleCameraEye.vx = tgt->vx;
                battleCameraEye.vy = tgt->vy;
                battleCameraEye.vz = tgt->vz;
            }
        }
        {
            VECTOR difference;
            SVECTOR* tgt = &battleCameraBlock.mC_atTarget;

            difference.vx = tgt->vx - battleCameraAt.vx;
            difference.vy = tgt->vy - battleCameraAt.vy;
            difference.vz = tgt->vz - battleCameraAt.vz;
            gte_ldlvl(&difference);
            gte_gpf12();
            gte_stlvl(&difference);
            if (difference.vx | difference.vz | difference.vy) {
                battleCameraAt.vx += difference.vx;
                battleCameraAt.vy += difference.vy;
                battleCameraAt.vz += difference.vz;
            } else {
                battleCameraAt.vx = tgt->vx;
                battleCameraAt.vy = tgt->vy;
                battleCameraAt.vz = tgt->vz;
            }
        }
#else
        {
            VECTOR difference;

            difference.vx = battleCameraBlock.m4_eyeTarget.vx - battleCameraEye.vx;
            difference.vy = battleCameraBlock.m4_eyeTarget.vy - battleCameraEye.vy;
            difference.vz = battleCameraBlock.m4_eyeTarget.vz - battleCameraEye.vz;
            gte_ldlvl(&difference);
            gte_gpf12();
            gte_stlvl(&difference);
            if (difference.vx | difference.vz | difference.vy) {
                battleCameraEye.vx += difference.vx;
                battleCameraEye.vy += difference.vy;
                battleCameraEye.vz += difference.vz;
            } else {
                copySVector3(&battleCameraEye, &battleCameraBlock.m4_eyeTarget);
            }
        }
        {
            VECTOR difference;

            difference.vx = battleCameraBlock.mC_atTarget.vx - battleCameraAt.vx;
            difference.vy = battleCameraBlock.mC_atTarget.vy - battleCameraAt.vy;
            difference.vz = battleCameraBlock.mC_atTarget.vz - battleCameraAt.vz;
            gte_ldlvl(&difference);
            gte_gpf12();
            gte_stlvl(&difference);
            if (difference.vx | difference.vz | difference.vy) {
                battleCameraAt.vx += difference.vx;
                battleCameraAt.vy += difference.vy;
                battleCameraAt.vz += difference.vz;
            } else {
                copySVector3(&battleCameraAt, &battleCameraBlock.mC_atTarget);
            }
        }
#endif
    }
    {
        /* unused0-2 only reserve retail's frame slots (temp40 at sp+0x40,
         * squareVector at sp+0x60; sp+0x10 is reused from the blocks above) */
        VECTOR unused0;
        MATRIX unused1;
        VECTOR temp40;
        VECTOR unused2;
        VECTOR squareVector;
        s32 dist;

    temp40.vx = battleCameraAt.vx - battleCameraEye.vx;
    temp40.vy = battleCameraAt.vy - battleCameraEye.vy;
    temp40.vz = battleCameraAt.vz - battleCameraEye.vz;
    Square0(&temp40, &squareVector);
    dist = SquareRoot0(squareVector.vx + squareVector.vz);
    battleCameraBlock.m1C_distance = SquareRoot0(squareVector.vx + squareVector.vy + squareVector.vz);
    battleCameraBlock.m14_orientation.vy = -ratan2(temp40.vz, temp40.vx);
    battleCameraBlock.m14_orientation.vx = -ratan2(temp40.vy, dist);
    battleCameraBlock.m14_orientation.vz = 0;
    }
}





/* delete callback of a camera-target sprite (restore eye/at target) */
void battleCameraControlSpriteDelete(TaskHeader* arg0) {
    SpriteActorCore* actor;

    actor = arg0->m4_data;
    if (actor->m40_draw.bits.xD_mode == 10) {
        if (battleCameraModeCallback1 == (SpriteTask*)arg0) {
            battleCameraModeCallback1 = NULL;
            if (battleCameraVar0 == 0) {
                copySVector3(&battleCameraBlock.m4_eyeTarget, &battleCameraEyeTarget_backup);
            }
        }
    } else if (battleCameraModeCallback2 == (SpriteTask*)arg0) {
        battleCameraModeCallback2 = NULL;
        if (battleCameraVar0 == 0) {
            copySVector3(&battleCameraBlock.mC_atTarget, &battleCameraAtTarget_backup);
        }
    }
    if (actor->mAC_x5) {
        registerSpriteCallback2_2(arg0);
    }
    allocateSavePointMeshDataSub0_callback(arg0);
    registerSpriteCallback2Sub0(arg0 + 1);
    freeHeap(arg0);
    if (--battleCameraControlSpriteActiveCount == 0) {
        setBattleCameraMode(backupCameraMode);
    }
}

/* update callback of a camera-target sprite (writes previousCameraEye2/At2) */
void battleCameraControlSpriteUpdate(TaskHeader* arg0) {
    SpriteActorCore* actor;

    actor = arg0->m4_data;
    OP_INIT_ENTITY_SCRIPT_sub0Sub9(actor);
    updateSpriteMovement(actor);
    if (actor->m40_draw.bits.xD_mode == 10) {
        previousCameraEye2.vx = actor->m0_position.vx;
        previousCameraEye2.vy = actor->m0_position.vy;
        previousCameraEye2.vz = actor->m0_position.vz;
    } else {
        previousCameraAt2.vx = actor->m0_position.vx;
        previousCameraAt2.vy = actor->m0_position.vy;
        previousCameraAt2.vz = actor->m0_position.vz;
    }
    if (actor->m64_spriteByteCode != NULL) {
        if (!actor->mAC_x6) {
            return;
        }
        OP_INIT_ENTITY_SCRIPT_sub0Sub9(actor);
        updateSpriteMovement(actor);
        if (actor->m40_draw.bits.xD_mode == 10) {
            previousCameraEye2.vx = actor->m0_position.vx;
        previousCameraEye2.vy = actor->m0_position.vy;
        previousCameraEye2.vz = actor->m0_position.vz;
        } else {
            previousCameraAt2.vx = actor->m0_position.vx;
        previousCameraAt2.vy = actor->m0_position.vy;
        previousCameraAt2.vz = actor->m0_position.vz;
        }
        if (actor->m64_spriteByteCode != NULL) {
            return;
        }
    }
    arg0->mC_deleteCallback(arg0);
}

/* Noah battleCameraControlSpriteInit (register an eye/at camera-target sprite) */
void battleCameraControlSpriteInit(SpriteTask* arg0) {
    SpriteActorCore* actor = &arg0->m38_actor;

    if (actor->m40_draw.bits.xD_mode == 10) {
        if (battleCameraModeCallback1 != NULL) {
            if ((u16)battleCameraModeCallback1->m38_actor.m34_currentSpriteFrame != 1 && (u16)actor->m34_currentSpriteFrame == 1) {
                battleCameraModeCallback1->m0_updateTask.mC_deleteCallback(&battleCameraModeCallback1->m0_updateTask);
                battleCameraModeCallback1 = arg0;
            } else {
                actor->m9E_wait = 0;
                actor->m64_spriteByteCode = NULL;
            }
        } else {
            battleCameraModeCallback1 = arg0;
            copySVector3(&battleCameraEyeTarget_backup, &battleCameraBlock.m4_eyeTarget);
        }
    } else if (battleCameraModeCallback2 != NULL) {
        if ((u16)battleCameraModeCallback2->m38_actor.m34_currentSpriteFrame != 1 && (u16)actor->m34_currentSpriteFrame == 1) {
            battleCameraModeCallback2->m0_updateTask.mC_deleteCallback(&battleCameraModeCallback2->m0_updateTask);
            battleCameraModeCallback2 = arg0;
        } else {
            actor->m9E_wait = 0;
            actor->m64_spriteByteCode = NULL;
        }
    } else {
        battleCameraModeCallback2 = arg0;
        copySVector3(&battleCameraAtTarget_backup, &battleCameraBlock.mC_atTarget);
    }
    battleCameraControlSpriteActiveCount++;
    if (actor->mAC_x2_facing) {
        actor->m32_direction = 0x800;
    } else {
        actor->m32_direction = 0;
    }
    setTaskDeleteFunction(&arg0->m0_updateTask, battleCameraControlSpriteDelete);
    setTaskUpdateFunction(&arg0->m0_updateTask, battleCameraControlSpriteUpdate);
    setBattleCameraMode(2);
}

/* Noah setBattleCameraMode */
void setBattleCameraMode(s32 arg0) {
    battleCameraMode = arg0;
    battleCameraModeInterpolation = 1;
    switch (arg0) {
    case 4:
        battleCameraModeInterpolation = 5;
        break;
    case 2:
        svecToFp(&previousCameraEye2, &battleCameraBlock.m4_eyeTarget);
        svecToFp(&previousCameraAt2, &battleCameraBlock.mC_atTarget);
        break;
    default:
        if (battleCameraModeCallback1 != NULL) {
            battleCameraModeCallback1->m0_updateTask.mC_deleteCallback(&battleCameraModeCallback1->m0_updateTask);
            battleCameraModeCallback1 = NULL;
        }
        if (battleCameraModeCallback2 != NULL) {
            battleCameraModeCallback2->m0_updateTask.mC_deleteCallback(&battleCameraModeCallback2->m0_updateTask);
            battleCameraModeCallback2 = NULL;
        }
        break;
    }
}

/* setter backupCameraMode */
void startJumpAnimationSub0(s32 arg0) {
    backupCameraMode = arg0;
}

/* Noah setCameraVisibleEntities */
void setCameraVisibleEntities(s32 arg0) {
    if (battleCameraVar0 == 0) {
        setBattleCameraMode(1);
        computeBattleCameraParams(arg0);
    }
    battleCameraVar1 = battleCameraVar2;
}

/* setter battleCameraParamsVar0 */
void setBattleCameraParamX(s16 arg0) {
    battleCameraParamsVar0.vx = arg0;
}

/* Noah computeBattleCameraParams (fit camera to the entities in a u16 mask) */
void computeBattleCameraParams(s32 arg0) {
#if VERSION_JP
    VECTOR sum = { 0, 0, 0, 0 }; /* JP copies it from a zeroed .rodata block */
#else
    VECTOR sum;
#endif
    SVECTOR eye;
    SVECTOR at;
    MATRIX m;
    VECTOR d;
    SVECTOR pos;
    u16 sxy[2];
    SVECTOR v;
    s32 dummy[2];
    MATRIX m2;
#if VERSION_JP
    VECTOR d2;
#endif
    VECTOR out;
    SVECTOR v2;
    MATRIX m3;
    VECTOR unused;
    SVECTOR v3;
    SpriteActorCore* actor;
    u32 mask;
    s32 i;
    s32 n;
    s32 x;
    s32 minX, minY, minZ;
    s32 maxX, maxY, maxZ;
    s32 tx, ty;   /* screen offsets; also hold the Z/Y averages below */
    s32 dist;
    s32 best;
    s32 scale;

#if !VERSION_JP
    memset(&sum, 0, sizeof(VECTOR));
#endif
    best = 0;
    allEntitiesToFitInView = arg0;
    for (i = 0, n = 0, mask = arg0; i != 11; i++, mask = (u16)mask >> 1) {
        if ((mask & 1) && battleBssBlock.m4_visualEntities[i].m3_hidden == 0 && (actor = battleBssBlock.m8C8C_spriteActorCores[i]) != NULL) {
            n++;
            sum.vx += actor->m0_position.vx >> 1;
            sum.vy += actor->m0_position.vy >> 1;
            sum.vz += actor->m0_position.vz >> 1;
        }
    }
    if (n == 0) {
        return;
    }
    i = 0;
    /* the Z/Y averages go through tx and dist (retail keeps the extra
     * register copies such reused variables produce) */
#if VERSION_JP
    sum.vx = maxX = minX = (sum.vx / n) * 2;
    maxY = minY = sum.vy = tx = (sum.vy / n) * 2;
    maxZ = minZ = sum.vz = (sum.vz / n) * 2;
#else
    maxX = minX = (sum.vx / n) * 2;
    maxZ = minZ = tx = (sum.vz / n) * 2;
    maxY = minY = dist = (sum.vy / n) * 2;
    sum.vx = minX;
    sum.vy = minY;
    sum.vz = minZ;
#endif
    mask = arg0;
    for (; i != 11; i++, mask = (u16)mask >> 1) {
        if ((mask & 1) && battleBssBlock.m4_visualEntities[i].m3_hidden == 0 && (actor = battleBssBlock.m8C8C_spriteActorCores[i]) != NULL) {
            x = actor->m0_position.vx;
            if (maxX < x) {
                maxX = x;
            }
            if (x < minX) {
                minX = x;
            }
            x = actor->m0_position.vz;
            if (maxZ < x) {
                maxZ = x;
            }
            if (x < minZ) {
                minZ = x;
            }
            x = actor->m0_position.vy;
            if (maxY < x) {
                maxY = x;
            }
            if (x < minY) {
                minY = x;
            }
        }
    }
    sum.vx = (minX + maxX) / 2;
    sum.vy = (maxY + minY) / 2;
    sum.vz = (minZ + maxZ) / 2;
    sum.vx >>= 16;
    sum.vy >>= 16;
    sum.vz >>= 16;
    RotMatrixZYX(&battleCameraParamsVar0, &m);
    v.vx = 0;
    v.vy = 0;
    v.vz = ReadGeomScreen() << 3;
    ApplyMatrix(&m, &v, &d);
    setEyeAt(&eye, &at, sum.vx, sum.vy, sum.vz, d.vx, d.vy, d.vz);
    lookAtNoDivide(&m, &eye, &at, &battleCameraUp);
    SetRotMatrix(&m);
    SetTransMatrix(&m);
    for (i = 0, mask = arg0; i != 11; i++, mask = (u16)mask >> 1) {
        if ((mask & 1) && battleBssBlock.m4_visualEntities[i].m3_hidden == 0 && (actor = battleBssBlock.m8C8C_spriteActorCores[i]) != NULL) {
            pos.vx = actor->m0_position.vx >> 16;
            pos.vy = actor->m0_position.vy >> 16;
            pos.vz = actor->m0_position.vz >> 16;
            RotTransPers(&pos, (s32*)sxy, &dummy[0], &dummy[1]);
            tx = sxy[0];
            ty = sxy[1];
            tx -= 0xA0;
            ty -= 0xA4;
            dist = ((tx * 0x40000) >> 16) * ((tx * 0x40000) >> 16);
            dist += ((ty * 0x40000) >> 16) * ((ty * 0x40000) >> 16);
            sxy[0] = tx;
            sxy[1] = ty;
            tx *= 4;
            ty *= 4;
            sxy[0] = tx;
            sxy[1] = ty;
            if (best < dist) {
                best = dist;
            }
            if (battleBssBlock.m4_visualEntities[i].m4_isGear && battleCameraGearHeightDisabled == 0) {
                pos.vy -= actor->m36_height;
                RotTransPers(&pos, (s32*)sxy, &dummy[0], &dummy[1]);
                tx = sxy[0];
                ty = sxy[1];
                tx -= 0xA0;
                ty -= 0xA4;
                dist = ((tx * 0x40000) >> 16) * ((tx * 0x40000) >> 16);
                dist += ((ty * 0x40000) >> 16) * ((ty * 0x40000) >> 16);
                sxy[0] = tx;
                sxy[1] = ty;
                tx *= 4;
                ty *= 4;
                sxy[0] = tx;
                sxy[1] = ty;
                if (best < dist) {
                    best = dist;
                }
            }
        }
    }
    best = SquareRoot0(best);
    if (best < 0x78) {
        RotMatrixZYX(&battleCameraParamsVar0, &m2);
        v2.vx = 0;
        v2.vy = 0;
        v2.vz = ReadGeomScreen() << 1;
        battleCameraVar2 = ReadGeomScreen() << 1;
#if VERSION_JP
        ApplyMatrix(&m2, &v2, &d2);
        setEyeAt(&eye, &at, sum.vx, sum.vy, sum.vz, d2.vx, d2.vy, d2.vz);
#else
        /* The PS1 writes the VECTOR over the SVECTOR pos and the word after
         * it (sxy, dead here); on the PC that word is some other local, so
         * the result goes to a VECTOR of its own. */
        ApplyMatrix(&m2, &v2, &out);
        setEyeAt(&eye, &at, sum.vx, sum.vy, sum.vz, out.vx, out.vy, out.vz);
#endif
        copySVector3(&battleCameraBlock.m4_eyeTarget, &eye);
        copySVector3(&battleCameraBlock.mC_atTarget, &at);
    } else {
        scale = (best << 14) / 0x78;
        scale = scale * 2;
        scale *= ReadGeomScreen();
        scale >>= 14;
        battleCameraVar2 = scale;
        RotMatrixZYX(&battleCameraParamsVar0, &m3);
        v3.vx = 0;
        v3.vy = 0;
        v3.vz = scale;
        ApplyMatrix(&m3, &v3, &out);
        setEyeAt(&eye, &at, sum.vx, sum.vy, sum.vz, out.vx, out.vy, out.vz);
        copySVector3(&battleCameraBlock.m4_eyeTarget, &eye);
        copySVector3(&battleCameraBlock.mC_atTarget, &at);
    }
}

/* if (!battleCameraVar0) setBattleCameraMode(4) */
void setBattleCameraMode4IfAllowed(void);
void setBattleCameraMode4IfAllowed(void) {
    if (battleCameraVar0 == 0) {
        /* First Deus's attack effect waits for the normal camera after
         * its mecha script takes camera ownership (0x68). Finish that move
         * before mode 4 disables interpolation, or both scripts wait forever. */
        if (pcGameplayOriginalBugFixes() && currentBattleConfig.m0_battleId == 0x40 &&
            currentBattleConfig.m2_arenaId == 0x33 && battleCameraMode == 1 &&
            battleCameraModeInterpolation == 1) {
            copySVector3(&battleCameraEye, &battleCameraBlock.m4_eyeTarget);
            copySVector3(&battleCameraAt, &battleCameraBlock.mC_atTarget);
        }
        setBattleCameraMode(4);
    }
}

/* if (!battleCameraVar0) setBattleCameraMode(1) */
void setBattleCameraMode1IfAllowed(void);
void setBattleCameraMode1IfAllowed(void) {
    if (battleCameraVar0 == 0) {
        setBattleCameraMode(1);
    }
}

/* Noah updateColorFromSin */
void updateColorFromSin(SpriteActorCore* arg0, s32 arg1) {
    s32 c;

    c = getAngleCos(arg1 << 6) + 0x1000;
    c >>= 6;
    c += 0x80;
    if (c >= 0x100) {
        c = 0xFF;
    }
    arg0->m28_r = c;
    arg0->m29_g = c;
    arg0->m2A_b = c;
    updateAllSubsprites(arg0);
}

/* delete the character blinking task */
void updateTargetSelectionMarkerSub0Sub2(TaskHeader* arg0) {
    BattleBlinkTask* task = characterBlinkTask;

    if (task != NULL) {
        task->m1C_actor->m2B_code |= 1;
        allocateSavePointMeshDataSub0_callback(&task->m0_task);
        freeHeap(task);
        characterBlinkTask = NULL;
    }
}

/* character blinking task update (colour pulse) */
void updateCharacterBlinkColor(TaskHeader* arg0) {
    BattleBlinkTask* task = (BattleBlinkTask*)arg0;
    SpriteActorCore* actor;
    s32 phase;
    s32 c;

    actor = task->m1C_actor;
    phase = task->m20_phase << 6;
    c = getAngleCos(phase) + 0x1000;
    c >>= 6;
    c += 0x80;
    if (c >= 0x100) {
        c = 0xFF;
    }
    actor->m28_r = c;
    c = getAngleSin(phase) + 0x1000;
    c >>= 6;
    c += 0x80;
    if (c >= 0x100) {
        c = 0xFF;
    }
    actor->m29_g = c;
    actor->m2A_b = c;
    updateAllSubsprites(actor);
    task->m20_phase++;
}

/* Noah updateCharacterBlinkingTask (create blink task for the current entity) */
void updateTargetSelectionMarkerSub0Sub1(void);
void updateTargetSelectionMarkerSub0Sub1(void) {
    BattleBlinkTask* task;
    SpriteActorCore* actor;

    if (characterBlinkTask != NULL) {
        if (characterBlinkTask->m24_entity == battleBssBlock.mA72_currentEntity) {
            return;
        }
        updateTargetSelectionMarkerSub0Sub2(NULL);
    }
    if (battleBssBlock.m8CB8_spriteActors[battleBssBlock.mA72_currentEntity] != NULL) {
        task = (BattleBlinkTask*)allocateUpdateTask(&battleBssBlock.m8CB8_spriteActors[battleBssBlock.mA72_currentEntity]->m0_updateTask, (s32)sizeof(BattleBlinkTask) - (s32)sizeof(TaskHeader));
        characterBlinkTask = task;
        setTaskUpdateFunction(&task->m0_task, updateCharacterBlinkColor);
        setTaskDeleteFunction(&task->m0_task, updateTargetSelectionMarkerSub0Sub2);
        if (spriteBytecode2ExtendedE0_Var0 != 0) {
            allocateSavePointMeshDataSub0_var0--;
        }
        task->m0_task.m14_ownerId.m31 = 0;
        actor = battleBssBlock.m8C8C_spriteActorCores[battleBssBlock.mA72_currentEntity];
        task->m1C_actor = actor;
        task->m24_entity = battleBssBlock.mA72_currentEntity;
        actor->m2B_code &= ~1;
    }
}

/* clears updateTargetSelectionMarkerSub0_var0 */
void initUpdateTargetSelectionMarkerSub0_var0(void) {
    updateTargetSelectionMarkerSub0_var0 = 0;
}

/* update target selection markers for a u16 entity mask (create/delete spinning cursors) */
void updateCharacterBlinkingTask(u16 arg0) {
    s32 i;
    sBattleSpriteActor* task;
    TaskHeader* cursor;

    updateTargetSelectionMarkerSub0_var0 = arg0;
    if (arg0 != 0) {
        updateTargetSelectionMarkerSub0Sub1();
    } else {
        updateTargetSelectionMarkerSub0Sub2(NULL);
    }
    for (i = 0; i != 11; i++, arg0 >>= 1) {
        if (arg0 & 1) {
            task = battleBssBlock.m8CB8_spriteActors[i];
            if (task != NULL && findMatchingGraphicEntity(&task->m0_updateTask, updateSpinningEnemySelectionCursor) == NULL) {
                createSpinningEnemySelectionCursor(task);
            }
        } else {
            task = battleBssBlock.m8CB8_spriteActors[i];
            if (task != NULL) {
                cursor = findMatchingGraphicEntity(&task->m0_updateTask, updateSpinningEnemySelectionCursor);
                if (cursor != NULL) {
                    cursor->mC_deleteCallback(cursor);
                }
            }
        }
    }
}

/* Noah drawSpinningEnemySelectionCursor */
void drawSpinningEnemySelectionCursor(TaskHeader* arg0) {
    MATRIX m;
    VECTOR scale;
    sBattleSpinningEnemySelectionCursor* cursor;
    s32 s;

    cursor = arg0->m4_data;
    TransMatrix(&m, &cursor->m3C_translation);
    createRotationMatrix(&cursor->m4C_rotation, &m);
    CompMatrix(&currentRenderingMatrix, &m, &m);
    s = ReadGeomScreen() << 12;
    if (m.t[2] != 0) {
        s = s / m.t[2];
    }
    s = R3000_DIV(0x1000000, s) / 2;
    setVector3(&scale, s, s, s);
    ScaleMatrixL(&m, &scale);
    SetRotMatrix(&m);
    SetTransMatrix(&m);
    renderCustomPolyBuffer(cursor->m64_customPolySubBuffer, cursor->m5C_buffer[battleBssBlock.m8C84_oddOrEven], characterRenderingOT, 0, 0, 0);
}

/* Noah updateSpinningEnemySelectionCursor */
void updateSpinningEnemySelectionCursor(TaskHeader* arg0) {
    sBattleSpinningEnemySelectionCursor* cursor = arg0->m4_data;
    SpriteActorCore* actor = cursor->m38_actor;

    cursor->m3C_translation.vx = actor->m0_position.vx >> 16;
    cursor->m3C_translation.vy = actor->m0_position.vy >> 16;
    cursor->m3C_translation.vz = actor->m0_position.vz >> 16;
    cursor->m3C_translation.vy = cursor->m3C_translation.vy - cursor->m54_actorHeight - 0x20;
    cursor->m58_tick++;
    updateColorFromSin(actor, cursor->m58_tick);
    cursor->m4C_rotation.vy += 0x10;
}

/* Noah deleteSpinningEnemySelectionCursor */
void deleteSpinningEnemySelectionCursor(TaskHeader* arg0) {
    sBattleSpinningEnemySelectionCursor* cursor = arg0->m4_data;
    SpriteActorCore* actor = cursor->m38_actor;

    actor->m2B_code |= 1;
    updateAllSubsprites(actor);
    clearShapeTransferEntry(cursor->m5C_buffer[0]);
    registerSpriteCallback2_2(arg0);
    allocateSavePointMeshDataSub0_callback(arg0);
    freeHeap(cursor);
}

/* Noah createSpinningEnemySelectionCursor */
void createSpinningEnemySelectionCursor(sBattleSpriteActor* arg0) {
    sBattleSpinningEnemySelectionCursor* cursor;
    SpriteActorCore* actor;
    s32 size;
    u8* buffer;
    u8* buffer2;

    actor = arg0->m0_updateTask.m4_data;
    cursor = (sBattleSpinningEnemySelectionCursor*)allocateTaskPair((s32)sizeof(sBattleSpinningEnemySelectionCursor), &arg0->m0_updateTask, updateSpinningEnemySelectionCursor, drawSpinningEnemySelectionCursor, deleteSpinningEnemySelectionCursor);
    cursor->m38_actor = actor;
    cursor->m54_actorHeight = (u16)actor->m36_height;
    if (spriteBytecode2ExtendedE0_Var0 != 0) {
        allocateSavePointMeshDataSub0_var0--;
    }
    cursor->m0_updateTask.m14_ownerId.m31 = 0;
    setSVector3(&cursor->m4C_rotation, 0, 0, 0);
    size = getCustomPolySubBufferSize(getCustomPolySubBuffer(battleBlinkingPolyBuffer, 0));
    buffer = allocateHeap(size * 2, 0);
    copyCustomPolySubBuffer(getCustomPolySubBuffer(battleBlinkingPolyBuffer, 0), buffer, 0, 1);
    buffer2 = buffer + size;
    memcpy(buffer2, buffer, size);
    cursor->m5C_buffer[0] = buffer;
    cursor->m5C_buffer[1] = buffer2;
    cursor->m64_customPolySubBuffer = getCustomPolySubBuffer(battleBlinkingPolyBuffer, 0);
    actor->m2B_code &= ~1;
    updateAllSubsprites(actor);
    updateSpinningEnemySelectionCursor(&cursor->m0_updateTask);
}
