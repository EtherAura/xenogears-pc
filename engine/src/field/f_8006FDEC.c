/*
 * FA status (US 0x8006FDEC-0x80071A64): gcc-2.7.2-psx -O2 -G0, aspsx 2.34.
 * Every OK also matches with the old -G8 preset. freeFieldData (and
 * setupRGBCalcSlot0_fadeToBlack in f_80071A64) differ under gcc-2.6.0/2.6.3 (register choice,
 * operand order) and most functions under gcc-2.7.2-cdk, so this is
 * gcc-2.7.2-psx.
 *   setupField3d  OK  Noah: setupField3d
 *   fieldDecompress  OK  Noah: fieldDecompress (LZSS wrapper, size unused)
 *   freeFieldData  OK  Noah: freeFieldData
 *   loadTimToVram  OK  TIM upload (pixels + CLUT) - suggest loadTimToVram
 *   loadFieldGraphics  OK  Noah: loadFieldGraphics
 *   freeFieldGraphics  OK  frees the field graphics buffer - suggest freeFieldGraphics
 *   convertSVectorToFixedVector  OK  SVECTOR -> VECTOR << 16 (no callers found)
 *   setIdentityMatrix  OK  Noah: setIdentityMatrix
 *   resetFieldDefault  OK  Noah: resetFieldDefault
 *   resetPortraitLoadingStatus  OK  Noah: resetPortraitLoadingStatus
 *   initFieldData  OK  Noah: initFieldData. Its .rodata (0x8006FAF4-0x8006FB04,
 *                      the unused VECTOR initializer {0,-100,2000,0}) comes out
 *                      exactly as retail.
 */
#include "common.h"
#include "pc_retail_heap.h"
#include "psyq.h"
#include "pc_interpolation.h"
#include "main/memory.h"
#include "main/filesystem.h"
#include "main/game_state.h"
#include "field/field.h"
#include "field/screen_effects.h"
#include "pc_convert.h"
#include "pc_gameplay.h"

/* Field .bss aggregates this file addresses through one base (see
 * include/field/field.h); other files still see the individual globals. */
extern FieldBss_800AF880 fieldSceneState;
extern FieldBss_800B2184 op9DVar1;

s32* allocateBufferForVramUpload(s32 count, s32 flags);
s32 loadImageFileToVram(s32 fileIndex, s32* buf, s32 channel, s32 flags, u16 arg4, u16 arg5, u16 arg6, u16 arg7, u16 arg8, u16 arg9);
void createRotationMatrix(SVECTOR* rot, MATRIX* m);
void brightenFieldCluts(void);

extern s32 fieldMapNumber;
extern s32 fieldGraphicsUploaded;
extern s32* fieldGraphicsAllocation;
extern s32 currentPortraitLoadingSlot;

/* Noah: portraitLoadingStatus entry */
typedef struct {
    /* 0x0 */ s16 m0_id;
    /* 0x2 */ s16 m2_status;
    /* 0x4 */ s16 m4_isAsymmetric;
} PortraitLoadingStatus;
extern PortraitLoadingStatus portraitLoadingStatus[3];


void lookAtDivided(MATRIX* m, VECTOR* eye, VECTOR* at, VECTOR* up);
void MulRotationMatrix(u16 index, LightDef* light);
void setupLightMatrix(MATRIX* m);
MATRIX* MulMatrix2(MATRIX* m0, MATRIX* m1);
void RotTrans(SVECTOR* v0, VECTOR* v1, s32* flag);

void setupField3d(s16* p) {
    s32 flag;

    lookAtDivided(&fieldSceneState.m110_cameraMatrix, &fieldSceneState.m0_cameraEye, &fieldSceneState.m10_cameraAt,
                  &fieldSceneState.m20_cameraUp);
    createRotationMatrix(&fieldSceneState.m1D4_projectionAngles, &fieldSceneState.m1E4_projectionMatrix);
    MulMatrix2(&fieldSceneState.m110_cameraMatrix, &fieldSceneState.m1E4_projectionMatrix);
    fieldSceneState.m248_lights[0].m0_x = *p++;
    fieldSceneState.m248_lights[0].m4_y = *p++;
    fieldSceneState.m248_lights[0].m8_z = *p++;
    p++;
    fieldSceneState.m248_lights[0].mC_r = *p++ << 3;
    fieldSceneState.m248_lights[0].mE_g = *p++ << 3;
    fieldSceneState.m248_lights[0].m10_b = *p++ << 3;
    p++;
    MulRotationMatrix(0, &fieldSceneState.m248_lights[0]);
    fieldSceneState.m248_lights[1].m0_x = *p++;
    fieldSceneState.m248_lights[1].m4_y = *p++;
    fieldSceneState.m248_lights[1].m8_z = *p++;
    p++;
    fieldSceneState.m248_lights[1].mC_r = *p++ << 3;
    fieldSceneState.m248_lights[1].mE_g = *p++ << 3;
    fieldSceneState.m248_lights[1].m10_b = *p++ << 3;
    p++;
    MulRotationMatrix(1, &fieldSceneState.m248_lights[1]);
    fieldSceneState.m248_lights[2].m0_x = *p++;
    fieldSceneState.m248_lights[2].m4_y = *p++;
    fieldSceneState.m248_lights[2].m8_z = *p++;
    p++;
    fieldSceneState.m248_lights[2].mC_r = *p++ << 3;
    fieldSceneState.m248_lights[2].mE_g = *p++ << 3;
    fieldSceneState.m248_lights[2].m10_b = *p++ << 3;
    p++;
    fieldSceneState.m248_lights[1] = fieldSceneState.m248_lights[0];
    fieldSceneState.m248_lights[2] = fieldSceneState.m248_lights[0];
    MulRotationMatrix(2, &fieldSceneState.m248_lights[2]);
    fieldSceneState.m284_ambient[0] = p[0] << 4;
    fieldSceneState.m284_ambient[1] = p[1] << 4;
    fieldSceneState.m284_ambient[2] = p[2] << 4;
    SetRotMatrix(&fieldSceneState.m110_cameraMatrix);
    SetTransMatrix(&fieldSceneState.m110_cameraMatrix);
    RotTrans(&fieldSceneState.m1DC_cameraRotation, (VECTOR*)fieldSceneState.m1E4_projectionMatrix.t, &flag);
    setupLightMatrix(&fieldSceneState.m1E4_projectionMatrix);
    SetRotMatrix(&fieldSceneState.m1E4_projectionMatrix);
    SetTransMatrix(&fieldSceneState.m1E4_projectionMatrix);
}

void fieldDecompress(s32 arg0, void* arg1, void* arg2) {
    decompress(arg1, arg2);
}


extern void* rawFieldDialogBundle;
extern void* fieldTriggerData;
extern void* mechaOverlayBuffer;
extern s32 g_frameOddOrEven;
extern void* backgroundPoly;
extern FieldImageList fieldImageMoveCommands;

void deleteAllTasks(void);
void shapeTransfert(void);
void clearShapeTransfertTableEntry(s32 index);
void freeShapeTransfert(void);
void freeFieldEntity(s32 index);
void freeModelDynamicVertices(void* dyn);
void freeScreenDistortion(void);
void freeHeapIfNotNull(void* ptr);
void freeImageMoveCommand(void* cmd);
void deinitDebugFont(void);
void freeMechaModule(void);
void ClearCacheAfterOverlayLoad(void);
void freeHeapForUser(u8 user);
void freeDebugCameraDigits(void);

/* Field teardown: flush both frames, free the actors and field buffers. */
void freeFieldData(void) {
    s32 i;
    s32 next;
    sFieldEntitySub0* sub;

    ResetGraph(1);
    deleteAllTasks();
    for (i = 0; i < 2;) {
        shapeTransfert();
        DrawSync(0);
        next = i + 1;
        clearShapeTransfertTableEntry((g_frameOddOrEven + next) & 1);
        i = next;
        shapeTransfert();
        DrawSync(0);
        freeShapeTransfert();
    }
    for (i = 0; i < fieldSceneState.m28C_totalObjects; i++) {
        freeFieldEntity(i);
        if (!(fieldSceneState.m290_actorArray[i].m58_flags & 0x40)) {
            sub = fieldSceneState.m290_actorArray[i].m0_model;
            if (fieldSceneState.m290_actorArray[i].m58_flags & 0x2000) {
                freeModelDynamicVertices(sub->m14_dynamicVertices);
            }
            freeModelBlock(sub->m4_pModelBlock);
            freeHeap(sub->m8_primBuffers[0]);
            freeHeap(fieldSceneState.m290_actorArray[i].m0_model);
        }
    }
    freeScreenDistortion();
    freeHeap(fieldSceneState.m290_actorArray);
    freeHeap(fieldTriggerData);
    freeHeap(rawFieldDialogBundle);
    freeHeap(rawFieldScriptData);
    freeHeap(fieldSceneState.m298_walkMesh);
    for (i = 0; i < *fieldSceneState.m294_models; i++) {
        pcFreeModel((u8*)fieldSceneState.m294_models + fieldSceneState.m294_models[i + 1]);
    }
    freeHeap(fieldSceneState.m294_models);
    freeHeap(fieldSceneState.m29C_actorSetupParams);
    if (OPX_80Params.m32_OPX_82Param4 != 0) {
        freeHeapIfNotNull(backgroundPoly);
    }
    for (i = 0; i < fieldImageMoveCommands.m0_count; i++) {
        freeImageMoveCommand(fieldImageMoveCommands.m4_commands[i]);
        freeHeap(fieldImageMoveCommands.m84_buffers[i]);
        freeHeap(fieldImageMoveCommands.m4_commands[i]);
    }
    deinitDebugFont();
    fieldImageMoveCommands.m0_count = 0;
    if (op9DVar1.mE0_numMechas != 0) {
        freeMechaModule();
        freeHeap(mechaOverlayBuffer);
        ClearCacheAfterOverlayLoad();
    }
    op9DVar1.mE0_numMechas = 0;
    freeHeapForUser(3);
    freeDebugCameraDigits();
}

/* Upload a TIM: pixels at (px, py); CLUT at (cx, cy) unless cy is -1,
 * CLUT size overridden by cw/ch when non-zero. */
void loadTimToVram(u32* addr, s16 px, s16 py, s16 cx, s16 cy, s16 cw, s16 ch) {
    TIM_IMAGE tim;

    OpenTIM(addr);
    if (ReadTIM(&tim) != NULL) {
        if (tim.caddr != NULL) {
            if (cy != -1) {
                tim.crect->x = cx;
                tim.crect->y = cy;
            }
            if (cw != 0) {
                tim.crect->w = cw;
            }
            if (ch != 0) {
                tim.crect->h = ch;
            }
            LoadImage(tim.crect, tim.caddr);
        }
        if (tim.paddr != NULL) {
            tim.prect->x = px;
        }
        tim.prect->y = py;
        LoadImage(tim.prect, tim.paddr);
    }
}

void loadFieldGraphics(void) {
    if (fieldGraphicsUploaded == 0) {
        fieldGraphicsUploaded = 1;
        fieldGraphicsAllocation = allocateBufferForVramUpload(4, 1);
        loadImageFileToVram(((fieldMapNumber & 0xFFF) * 2) + 0xB9, fieldGraphicsAllocation, 0, 0, 0, 0, 0, 0, 0, 0);
    }
}

void freeFieldGraphics(void) {
    if (fieldGraphicsUploaded == 1) {
        waitReadCompletion(0);
        DrawSync(0);
        freeHeap(fieldGraphicsAllocation);
        fieldGraphicsUploaded = 0;
    }
    brightenFieldCluts();
}

void convertSVectorToFixedVector(VECTOR* out, SVECTOR* in) {
    out->vx = in->vx << 16;
    out->vy = in->vy << 16;
    out->vz = in->vz << 16;
}

void setIdentityMatrix(MATRIX* m) {
    SVECTOR rot;
    rot.vx = 0;
    rot.vy = 0;
    rot.vz = 0;
    createRotationMatrix(&rot, m);
    m->t[0] = m->t[1] = m->t[2] = 0;
}



extern s32 fieldDebugDisable;
extern MATRIX occlusionTestMatrix;
extern u16 inputAllowedMask;
extern s16 padButtonForScriptsPad1;
extern s16 padButtonForDialogs;
extern s16 padButtonForDialogs2;
extern s16 g_padButtonForField;
extern s16 padButtonForField2;
extern s32 OPX8E_param1;
extern s32 OPX8E_param0;
extern s32 onlyUpdateDirector;
extern s32 fieldUseGroundOT;
extern s32 load2dAnimVar;
extern s32 screenDistrotionAllocated;
extern s32 updateEntityEventCode3Var1;
extern s32 EntityMoveCheck0Var0;
extern s32 iRam800adb70;
extern s32 loadCompleted;
extern s32 playerCanRun;
extern s32 fieldBattleRequested;
extern s32 cameraCollisionState;
extern s32 newBootMode;
extern s32 battleStartSaveWritePending;
extern s32 D_800ADB50;
extern s32 OPX_86Param;
extern s32 fieldTransitionInProgress;
extern u8 kernelAndFieldStatesSynced;
extern s32 fieldBattleMusicPending;
extern s32 bBattleSequenceInitialized;
extern s32 D_800B14A4;
extern s16 g_menuOpenArgFromScript;
extern s32 fullScreenTransitionEffectRenderMode;
extern s32 fullScreenTransitionEffectRenderParam;
extern s32 fieldTransitionFadeInLength;
extern s32 fieldTransitionMode;
extern s32 g_updateAllEntitiesSub2Var0;
extern s32 currentParticleCreatorId;
extern s16 updateCharacterVar1;
extern u8 fieldTransitionCompleted;
extern u8 renderCharsDisabled;
extern s32 paletteFadeUploadPending;
extern s16 wideImageSpritesVisible;
extern s32 fieldMovieStopRequest;
extern s32 fieldMovieStarted;
extern s32 disableParticles;
extern s32 g_menuIdToOpen;
extern s16 fieldScreenFadedOut;
extern s32 asyncLoadingVar1;
extern s32 fieldMoviePlayerStatus;
extern s32 fieldChangePrevented;
extern u16 inputAllowedMask2;
extern s32 updateEntityEventCode3Var2;
extern s16 deltaMoveVar0;
extern s16 windowOpenBF;
extern s16 fieldRandomBattleVar;
extern s32 numInitializedFieldScriptEntities;
extern s32 dialogWindowFlag2;
extern s32 fieldScriptEntityAlreadyInitialized;
extern s32 partyFieldActorIds[3];
extern s32 partyGearActorIds[3];
extern s32 gDepthDivider;
extern GameState* pKernelGameState;
extern s16 fieldVars[0x400];
extern MATRIX g_cameraMatrix2;
extern void* pCurrentFieldRenderingContext;

void func_8028125C(void);
void setFieldDrawEnvClip(s32 x, s32 y, s32 w, s32 h);
void resetInputs(void);
void resetCameraData(void);
void resetPortraitLoadingStatus(void);
void resetActorSoundSlots(void);
void resetParticleEffectsTable(void);
void initWideImageSprites(void);
void setIdentityMatrix(MATRIX* m);

void resetFieldDefault(void) {
    VECTOR unused; /* reserves sp+0x10 */
    SVECTOR rot;
    s32 i;

    if (fieldDebugDisable == 0) {
        func_8028125C();
    }
    setFieldDrawEnvClip(0, 0, 0x140, 0xE0);
    resetInputs();
    inputAllowedMask = 0xFFFF;
    padButtonForScripts = 0;
    padButtonForScriptsPad1 = 0;
    padButtonForDialogs = 0;
    padButtonForDialogs2 = 0;
    g_padButtonForField = 0;
    padButtonForField2 = 0;
    op9DVar1.m1D2_fieldBattleTransitionEffect = 5;
    op9DVar1.m1C2_jumpAnimationId = 3;
    op9DVar1.m1C6_numMaxMechaOverlayEntries = 0x40;
    op9DVar1.m1C8_gearSwitchOverride = 0xFF;
    fieldMovieParams.m18_soundSequenceId = 0xFF;
    OPX8E_param1 = 0;
    OPX8E_param0 = 0;
    op9DVar1.m38_objectOffsetX = 0;
    op9DVar1.m3C_objectOffsetY = 0;
    op9DVar1.m40_objectOffsetZ = 0;
    op9DVar1.mE0_numMechas = 0;
    op9DVar1.m34_lastSoundEffectId = 0;
    op9DVar1.m4D_compassDisabled = 0;
    op9DVar1.m4C_menuDisabled = 0;
    fieldScreenEffects.m8_distortionParamDiff[5] = 0;
    fieldScreenEffects.m8_distortionParamDiff[4] = 0;
    fieldScreenEffects.m8_distortionParamDiff[3] = 0;
    fieldScreenEffects.m8_distortionParamDiff[2] = 0;
    fieldScreenEffects.m8_distortionParamDiff[1] = 0;
    fieldScreenEffects.m8_distortionParamDiff[0] = 0;
    fieldScreenEffects.m3A_phaseY = 0;
    fieldScreenEffects.m38_phaseX = 0;
    op9DVar1.mE4_partySpriteSetupBase = 0;
    op9DVar1.m4A_partyPositioned = 0;
    op9DVar1.m2C_objectStepZ = 0;
    op9DVar1.m2E_objectStepY = 0;
    op9DVar1.m2A_objectStepX = 0;
    fieldScreenEffects.m0_screenDistortionRunning = 0;
    op9DVar1.m49_cameraFollowHeight = 0;
    op9DVar1.m18_backgroundClearColor[3] = 0;
    op9DVar1.m18_backgroundClearColor[2] = 0;
    op9DVar1.m18_backgroundClearColor[1] = 0;
    op9DVar1.m18_backgroundClearColor[0] = 0;
    op9DVar1.m48_noUpdatesToPartyMembers = 0;
    op9DVar1.m4B_partyFollowDisabled = 0;
    op9DVar1.m4E_objectMoveFlags = 0;
    onlyUpdateDirector = 0;
    fieldUseGroundOT = 0;
    load2dAnimVar = 0;
    screenDistrotionAllocated = 0;
    updateEntityEventCode3Var1 = 0;
    EntityMoveCheck0Var0 = 0;
    iRam800adb70 = 0;
    loadCompleted = 0;
    playerCanRun = 0;
    fieldBattleRequested = 0;
    cameraCollisionState = 0;
    newBootMode = 0;
    battleStartSaveWritePending = 0;
    D_800ADB50 = 0;
    OPX_86Param = 0;
    fieldTransitionInProgress = 0;
    kernelAndFieldStatesSynced = 0;
    fieldBattleMusicPending = 0;
    bBattleSequenceInitialized = 0;
    op9DVar1.m15C_cameraTrackingMode = 0;
    op9DVar1.m1B8_keptSoundChannels = 0;
    D_800B14A4 = 0;
    g_menuOpenArgFromScript = 0;
    fullScreenTransitionEffectRenderMode = 0;
    fullScreenTransitionEffectRenderParam = 0;
    fieldTransitionFadeInLength = 0x20;
    fieldTransitionMode = 2;
    op9DVar1.m28_soundMaxDistance = 0x3FF;
    g_updateAllEntitiesSub2Var0 = 4;
    currentParticleCreatorId = 0;
    updateCharacterVar1 = 0;
    op9DVar1.m1BA_actorCameraTracked = 0;
    op9DVar1.m1C0_customJumpMode = 0;
    op9DVar1.m1BE_jumpCooldown = 0;
    op9DVar1.m1C4_warpPartyRequest = 0;
    op9DVar1.m1CA_playerIsntPartyLeader = 0;
    op9DVar1.m1D1_randomBattleTransitionEffect = 0;
    fieldTransitionCompleted = 0;
    renderCharsDisabled = 0;
    op9DVar1.m1D3_spriteFogDisabled = 0;
    op9DVar1.m1D0_reverseDpadDirections = 0;
    paletteFadeUploadPending = 0;
    op9DVar1.m1CC_savedPlayerScriptFlags = 0;
    wideImageSpritesVisible = 0;
    op9DVar1.m1D4_pauseDisabled = 0;
    fieldMovieStopRequest = 0;
    fieldMovieStarted = 0;
    disableParticles = 0;
    g_menuIdToOpen = 0xFF;
    rot.vx = 0;
    rot.vy = 0;
    rot.vz = 0;
    createRotationMatrix(&rot, &occlusionTestMatrix);
    for (i = 0; i < 3; i++) {
        op9DVar1.m15E_lightActorIds[i] = -1;
    }
    fieldSceneState.mBA_op99Var4 = 0x1000;
    fieldScreenFadedOut = 1;
    op9DVar1.m50_linkOTIndex = 0x720;
    op9DVar1.m1C_OPX_0CParams0[2] = 0x100;
    op9DVar1.m1C_OPX_0CParams0[1] = 0x100;
    op9DVar1.m1C_OPX_0CParams0[0] = 0x100;
    op9DVar1.m22_OPX_0CParams1[2] = 0x200;
    op9DVar1.m22_OPX_0CParams1[1] = 0x200;
    op9DVar1.m22_OPX_0CParams1[0] = 0x200;
    op9DVar1.m30_spriteTurnSpeed = 0x80;
    asyncLoadingVar1 = 0xFF;
    op9DVar1.m8_entityMoveSpeedFactor = 0x1000;
    op9DVar1.m0_renderModelRotationAngles.vx = 0;
    op9DVar1.m0_renderModelRotationAngles.vy = 0;
    op9DVar1.m0_renderModelRotationAngles.vz = 0;
    fieldMoviePlayerStatus = -1;
    for (i = 0; i < 16; i++) {
        op9DVar1.mEC_encounterMusicIds[i] = 0x1D;
    }
    op9DVar1.m10C_battleMusicId = 0x1D;
    fieldChangePrevented = -1;
    op9DVar1.m54_cameraSnapFrames = 2;
    inputAllowedMask2 = 0xFFFF;
    op9DVar1.mC_fogNearColor[2] = 0x80;
    op9DVar1.mC_fogNearColor[1] = 0x80;
    op9DVar1.mC_fogNearColor[0] = 0x80;
    op9DVar1.m10_fogFarColor[2] = 0xFF;
    op9DVar1.m10_fogFarColor[1] = 0xFF;
    op9DVar1.m10_fogFarColor[0] = 0xFF;
    op9DVar1.m14_fogNear = 0x15E0;
    op9DVar1.m16_fogFar = 0x300C;
    g_frameOddOrEven = 0;
    updateEntityEventCode3Var2 = 0;
    fieldImageMoveCommands.m0_count = 0;
    op9DVar1.mE8_playerControlledActor = 0;
    deltaMoveVar0 = 0;
    windowOpenBF = 0;
    fieldRandomBattleVar = 0;
    numInitializedFieldScriptEntities = 0;
    dialogWindowFlag2 = 0;
    pcFieldFrameIndex = 0; /* Enhanced: the 60 Hz tick parity starts with the field */
    op9DVar1.mA_isFogSetup = 0;
    op9DVar1.m52_dialogWindowOpenAnimationNumFrames = 8;
    if (fieldScriptEntityAlreadyInitialized == 0) {
        for (i = 0; i < 3; i++) {
            partyFieldActorIds[i] = 0xFF;
            partyGearActorIds[i] = 0xFF;
        }
    }
    for (i = 0; i < 32; i++) {
        op9DVar1.m11C_encounterTriggerTimes[i] = 0xFFFF;
    }
    op9DVar1.m118_encounterCount = 0;
    op9DVar1.m114_encounterTimer = 0;
    gDepthDivider = 2;
    for (i = 0; i < 0x200; i++) {
        fieldVars[i] = pKernelGameState->m1930_fieldVarsBackup[i];
        fieldVars[i + 0x200] = 0;
    }
    SetGeomScreen(0x200);
    setIdentityMatrix(&fieldSceneState.m110_cameraMatrix);
    setIdentityMatrix(&g_cameraMatrix2);
    setIdentityMatrix(&fieldSceneState.m1E4_projectionMatrix);
    setIdentityMatrix(&fieldSceneState.m224_projectionTempMatrix);
    fieldSceneState.m1D4_projectionAngles.vx = 0;
    fieldSceneState.m1D4_projectionAngles.vy = 0;
    fieldSceneState.m1D4_projectionAngles.vz = 0;
    fieldSceneState.m1DC_cameraRotation.vx = 0;
    fieldSceneState.m1DC_cameraRotation.vy = 0;
    fieldSceneState.m1DC_cameraRotation.vz = 0;
    fieldSceneState.m244_worldScaleFactor = 0x3000;
    createRotationMatrix(&fieldSceneState.m1D4_projectionAngles, &fieldSceneState.m1E4_projectionMatrix);
    pCurrentFieldRenderingContext = &fieldRenderContext[0];
    resetCameraData();
    resetPortraitLoadingStatus();
    resetActorSoundSlots();
    resetParticleEffectsTable();
    initWideImageSprites();
}

void resetPortraitLoadingStatus(void) {
    s32 i;
    for (i = 0; i < 3; i++) {
        portraitLoadingStatus[i].m0_id = 0xFF;
        portraitLoadingStatus[i].m2_status = 0xFF;
    }
    currentPortraitLoadingSlot = 0;
}


/* A model in the models bundle: 0x10-byte header, then the blocks */
typedef struct {
    u8 m0[0x10];
    ModelBlock m10_block;
} FieldModel;

extern u8* rawFieldBundle;                  /* Noah: rawFieldBundle (layout: FieldBundle) */
/* Retail reads the bundle header through plain word pointers, not struct
 * fields (the loads stay ordered after stores to scalar globals). */
#define BUNDLE_S32(off) (*(s32*)(rawFieldBundle + (off)))
/* Noah: battleConfigs (main). The field decompresses 0x210-0x217 bytes into
 * it: the 16 configs and encounterProbabilityWeight (+0x200), up to
 * partyCharacterBuffersCompressed (+0x220). The full extent keeps them one object on the PC. */
extern u8 battleConfigs[0x220];
extern s32 g_totalActors;
extern u8* pCurrentFieldScriptFile;
extern s32 walkMeshVar4;
extern s32 fieldExecuteVar1;
extern MATRIX renderModelRotationMatrix;
extern s32 currentFieldId1;
extern s32 currentFieldId0;


void initCompassPoly(FieldQuad* p, s32 x, s32 y, s32 mode);
void initCompassShadowPoly(void);
void uploadFieldImages(void* data);
void* initModelDynamicVertices(ModelBlock* block, s32 flags);
s32 initModel5(ModelBlock* block);
void initFieldScriptEntity(s32 index);
void func_802812A4(void);
void initDialogWindows(void);
void resetAllRGBFadersToBlack(void);
void unflagAllocation(void* p);
void resetSpriteCallbacks(void);
void initMechaFieldArgs2(MechaFieldArgs2* p, s32 a1, s32 a2, s32 a3, s32 a4, s32 a5, s32 a6, s32 a7, s32 a8, s32 a9);
void startAllEntityScripts(void);
void* createBackgroundPoly(s32 arg0, s32 arg1, s32 arg2, s32 arg3, s32 arg4, s32 arg5, s32 arg6, s32 arg7,
                           s32* vec, u8* colors, s16 arg10, s16 arg11, s16 arg12);
void loadMechaOVerlayAndRegisterMechas(void);
void reloadFieldActorsFromSaveBlock(void);
void setupObjectRenderModes(void);
void doPCCollisionCheckAfterLoading(void);
s32 shoudlGroundOTBeEnabled(void);
void setSpriteActorAngle(void* actor, s16 angle);
void spriteActorSetDirection(void* actor, s32 direction);

void pcFieldWideScene(void);
extern s32 rawFieldBundleSize;              /* rawFieldBundle size */
void cdFieldNotOnDisc(s32 field); /* port/src/cd/placeholder.c */

void initFieldData(void) {
    VECTOR unused = { 0, -100, 2000, 0 }; /* rodata 0x8006FAF4 */
    s32 i;
    s32 j;
    s32 count;
    s32 size;
    s32* buf;
    s32* buf2;
    s32* p;
    u16* setup;
    sFieldEntitySub0* sub;
    sptr offset; /* one temp reused by several loops (keeps it a global pseudo) */

    /* A field this disc holds only as a 24-byte placeholder (disc 2): no
     * header to read. The PS1 stops in decompress with an address error. */
    if (rawFieldBundleSize < 0x190) {
        cdFieldNotOnDisc(fieldMapNumber & 0xFFF);
    }
    pcFieldWideScene(); /* the opt-out list (port/src/game/field/wide.c) */
    pcInterpolationScene();
    resetFieldDefault();
    fieldVramMapping = *(FieldVramMappingTable*)rawFieldBundle;
    for (i = 0; i < 4; i++) {
        for (j = 0; j < 4; j++) {
            initCompassPoly(&fieldCompassVar2[i * 4 + j], j, i, 0);
        }
    }
    initCompassPoly(&fieldCompassVar2[0x10], 4, 4, 1);
    initCompassPoly(&fieldCompassVar2[0x11], 5, 5, 1);
    initCompassPoly(&fieldCompassVar2[0x12], 6, 6, 1);
    initCompassPoly(&fieldCompassVar2[0x13], 7, 7, 1);
    initCompassPoly(&fieldCompassVar2[0x14], 8, 8, 1);
    initCompassShadowPoly();

    size = BUNDLE_S32(0x10C) + 0x10;
    buf = allocateHeap(PC_HEAP_BYTES(size), 1);
    fieldDecompress(size, (u8*)(BUNDLE_S32(0x130) + (sptr)rawFieldBundle), buf);
    p = buf;
    count = *p;
    for (i = 0; i < count; i++) {
        uploadFieldImages((u8*)(*++p + (sptr)buf));
    }

    size = BUNDLE_S32(0x11C) + 0x10;
    buf2 = allocateHeap(PC_HEAP_BYTES(size), 0);
    fieldDecompress(size, (u8*)(BUNDLE_S32(0x140) + (sptr)rawFieldBundle), buf2);
    p = buf2;
    count = *p;
    for (i = 0; i < count; i++) {
        u16 x = fieldVramMapping.m[i].m0_vramX;
        u16 y = fieldVramMapping.m[i].m2_vramY;
        p++;
        if (fieldVramMapping.m[i].m6 == 0) {
            uploadNpcSpriteSheet((s32*)(*p + (sptr)buf2), x, y);
        }
    }
    /* every NPC slot's sprite pack cels decode in the background, whichever way
     * its sheet arrives (this section, or the texture file as in Lahan; xg-hbc.23), and their
     * poses with the party's (xg-hbc.26) and the party's cels; the texture pack's room pages
     * start loading here too, ahead of the room's first frame (xg-hbc.27) */
    pcSpritePackParty(currentParty[0], currentParty[1], currentParty[2]);
    pcSpritePackPreload(fieldMapNumber & 0xFFF, -1);
    DrawSync(0);
    freeHeap(buf);
    freeHeap(buf2);

    size = BUNDLE_S32(0x114) + 0x10;
    fieldSceneState.m294_models = allocateHeap(PC_HEAP_BYTES(size), 0);
    fieldDecompress(size, (u8*)(BUNDLE_S32(0x138) + (sptr)rawFieldBundle), fieldSceneState.m294_models);
    buf = fieldSceneState.m294_models;
    for (i = 0; i < *fieldSceneState.m294_models; i++) {
        offset = *++buf;
        fieldModelRelocation((Model*)(offset + (sptr)fieldSceneState.m294_models));
    }

    fieldDecompress(BUNDLE_S32(0x124) + 0x10, (u8*)(BUNDLE_S32(0x148) + (sptr)rawFieldBundle),
                  battleConfigs);

    size = BUNDLE_S32(0x120) + 0x10;
    rawFieldScriptData = allocateHeap(PC_HEAP_BYTES(size), 0);
    fieldDecompress(size, (u8*)(BUNDLE_S32(0x144) + (sptr)rawFieldBundle), rawFieldScriptData);
    {
        u8* script = (u8*)rawFieldScriptData + 0x84;
        g_totalActors = rawFieldScriptData->m80_numActors;
        pCurrentFieldScriptFile = script + g_totalActors * 0x40;
        /* Original-game bug fixes in the scripts (port/src/game/field/scriptfix.c) */
        pcFieldScriptFixes(fieldMapNumber & 0xFFF, pCurrentFieldScriptFile,
                           BUNDLE_S32(0x120) - 0x84 - g_totalActors * 0x40);
    }

    size = BUNDLE_S32(0x12C) + 0x10;
    fieldTriggerData = allocateHeap(PC_HEAP_BYTES(size), 0);
    fieldDecompress(size, (u8*)(BUNDLE_S32(0x150) + (sptr)rawFieldBundle), fieldTriggerData);
    {
        size = BUNDLE_S32(0x128) + 0x10;
        rawFieldDialogBundle = allocateHeap(PC_HEAP_BYTES(size), 0);
        fieldDecompress(size, (u8*)(BUNDLE_S32(0x14C) + (sptr)rawFieldBundle), rawFieldDialogBundle);
    }
    size = BUNDLE_S32(0x110) + 0x10;
    fieldSceneState.m298_walkMesh = allocateHeap(PC_HEAP_BYTES(size), 0);
    fieldDecompress(size, (u8*)(BUNDLE_S32(0x134) + (sptr)rawFieldBundle), fieldSceneState.m298_walkMesh);
    buf = fieldSceneState.m298_walkMesh;
    fieldSceneState.m2D4_numWalkMesh = *fieldSceneState.m298_walkMesh;
    buf++;
    for (i = 0; i < 4; i++) {
        fieldSceneState.m2C4_walkMeshNumTrianglePerBlock[i] = (u32)*buf++ / 14;
    }
    fieldSceneState.m2A0_walkMeshVar1 = (s32*)(*buf++ + (sptr)fieldSceneState.m298_walkMesh);
    for (i = 0; i < fieldSceneState.m2D4_numWalkMesh; i++) {
        fieldSceneState.m2A4_walkMeshTriangle[i] = (s32*)(*buf++ + (sptr)fieldSceneState.m298_walkMesh);
        fieldSceneState.m2B4_walkMeshVertices[i] = (s32*)(*buf++ + (sptr)fieldSceneState.m298_walkMesh);
    }
    walkMeshVar4 = (s32)(fieldSceneState.m2A4_walkMeshTriangle[0] - fieldSceneState.m2A0_walkMeshVar1);

    size = BUNDLE_S32(0x118) + 0x10;
    fieldSceneState.m29C_actorSetupParams = allocateHeap(PC_HEAP_BYTES(size), 0);
    fieldDecompress(size, (u8*)(BUNDLE_S32(0x13C) + (sptr)rawFieldBundle),
                  fieldSceneState.m29C_actorSetupParams);

    fieldSceneState.m15C_cameraLimits[0] = 1;
    fieldSceneState.m15C_cameraLimits[1] = 1;
    fieldSceneState.m15C_cameraLimits[2] = 1;
    fieldSceneState.m15C_cameraLimits[3] = 1;
    setupField3d((s16*)(rawFieldBundle + 0x154));

    count = *(u16*)(rawFieldBundle + 0x18C);
    setup = (u16*)(rawFieldBundle + 0x190);
    fieldSceneState.m28C_totalObjects = count;
    {
        /* n is block-local (its multiply chain shares one callee-saved reg);
         * the loop bound is the function-wide `size`. */
        s32 n = count * sizeof(sFieldEntity) / 4;
        s32* q = allocateHeap(PC_HEAP_SIZED(n * 4, count * 0x5C), 0);
        fieldSceneState.m290_actorArray = (sFieldEntity*)q;
        size = n;
        for (i = 0; i < size; i++) {
            *q++ = 0;
        }
    }

    for (i = 0; i < fieldSceneState.m28C_totalObjects; i++) {
        fieldSceneState.m290_actorArray[i].m58_flags = *setup++;
        fieldSceneState.m290_actorArray[i].m50_modelRotation.vx = setup[0];
        fieldSceneState.m290_actorArray[i].m50_modelRotation.vy = setup[1];
        fieldSceneState.m290_actorArray[i].m50_modelRotation.vz = setup[2];
        setup += 3;
        fieldSceneState.m290_actorArray[i].m2C_matrixBackup.t[0] = fieldSceneState.m290_actorArray[i].mC_matrix.t[0] = setup[0];
        fieldSceneState.m290_actorArray[i].m2C_matrixBackup.t[1] = fieldSceneState.m290_actorArray[i].mC_matrix.t[1] = setup[1];
        fieldSceneState.m290_actorArray[i].m2C_matrixBackup.t[2] = fieldSceneState.m290_actorArray[i].mC_matrix.t[2] = setup[2];
        setup += 3;
        if (!(fieldSceneState.m290_actorArray[i].m58_flags & 0x40)) {
            sub = allocateHeap(PC_HEAP_SIZED(sizeof(sFieldEntitySub0), 0x24), 0);
            fieldSceneState.m290_actorArray[i].m0_model = sub;
            p = (s32*)(*setup * 4 + (sptr)fieldSceneState.m294_models);
            offset = p[1] + (sptr)fieldSceneState.m294_models;
            sub->m4_pModelBlock = pcModelBlocks((void*)offset);
            initModel1(sub->m4_pModelBlock, &sub->m8_primBuffers[0], &sub->m8_primBuffers[1]);
            initModel2(sub->m4_pModelBlock, sub->m8_primBuffers[0], (fieldSceneState.m290_actorArray[i].m58_flags & 0xC) >> 2);
            memcpy(sub->m8_primBuffers[1], sub->m8_primBuffers[0], sub->m4_pModelBlock->m34_primBufferSize);
            if (fieldSceneState.m290_actorArray[i].m58_flags & 0x2000) {
                resetMemoryAllocStats(3, 0);
                sub->m14_dynamicVertices = initModelDynamicVertices(sub->m4_pModelBlock, 0);
                resetMemoryAllocStats(8, 0);
            }
            /* initModel5 expects a Model: given a block, the PS1 sets bit 1 of
             * the block's m4_numPrims (as m4_flags), and its heap shrink gives up
             * on the zero words before the block (the model header's m8/mC,
             * read as a heap header). */
            sub->m4_pModelBlock->m4_numPrims |= 2;
            initFieldScriptEntity(i);
        } else {
            fieldSceneState.m290_actorArray[i].m58_flags |= 0x20;
            fieldSceneState.m290_actorArray[i].m50_modelRotation.vx = 0;
            fieldSceneState.m290_actorArray[i].m50_modelRotation.vy = 0;
            fieldSceneState.m290_actorArray[i].m50_modelRotation.vz = 0;
            initFieldScriptEntity(i);
        }
        setup++;
    }

    if (fieldDebugDisable == 0) {
        func_802812A4();
    }
    initDialogWindows();
    resetAllRGBFadersToBlack();
    unflagAllocation(rawFieldBundle);
    freeHeap(rawFieldBundle);
    resetMemoryAllocStats(5, 0);
    allocateShapeTransfert(0x3C00, 0);
    resetSpriteCallbacks();
    resetMemoryAllocStats(8, 0);
    initMechaFieldArgs2(&op9DVar1.m98_mechaFieldArgs2[1], 0x800, 0, 0, 0x800, 0, 0, 0x800, 0, 0);
    initMechaFieldArgs2(&op9DVar1.m98_mechaFieldArgs2[0], 0x1F8, -0xFC1, -0x1F8, 0, 0, 0, 0, 0, 0);
    op9DVar1.mD8_mechaBackColor[2] = 0x1E;
    op9DVar1.mD8_mechaBackColor[1] = 0x1E;
    op9DVar1.mD8_mechaBackColor[0] = 0x1E;
    OPX_80Params.m0_OPX_80Params[2] = 0x140;
    OPX_80Params.m0_OPX_80Params[7] = 0;
    OPX_80Params.m20_OPX_82Param0[2][1] = 0;
    OPX_80Params.m20_OPX_82Param0[2][0] = 0;
    OPX_80Params.m20_OPX_82Param0[1][2] = 0;
    OPX_80Params.m20_OPX_82Param0[1][1] = 0;
    OPX_80Params.m20_OPX_82Param0[1][0] = 0;
    OPX_80Params.m20_OPX_82Param0[0][2] = 0;
    OPX_80Params.m20_OPX_82Param0[0][1] = 0;
    OPX_80Params.m10_OPX_81Params[0] = OPX_80Params.m20_OPX_82Param0[0][0] = 0;
    OPX_80Params.m10_OPX_81Params[2] = 0x1000;
    OPX_80Params.m2C_OPX_82Param3[2] = 0;
    OPX_80Params.m2C_OPX_82Param3[1] = 0;
    OPX_80Params.m2C_OPX_82Param3[0] = 0;
    OPX_80Params.m0_OPX_80Params[6] = 0;
    OPX_80Params.m0_OPX_80Params[5] = 0;
    OPX_80Params.m0_OPX_80Params[4] = 0;
    OPX_80Params.m0_OPX_80Params[3] = 0;
    OPX_80Params.m0_OPX_80Params[1] = 0;
    OPX_80Params.m0_OPX_80Params[0] = 0;
    OPX_80Params.m32_OPX_82Param4 = 0;
    OPX_80Params.m10_OPX_81Params[1] = 0;
    OPX_80Params.m20_OPX_82Param0[2][2] = 0x20;
    fieldExecuteVar1 = 0;
    startAllEntityScripts();
    fieldExecuteVar1 = 1;
    createRotationMatrix(&op9DVar1.m0_renderModelRotationAngles, &renderModelRotationMatrix);
    renderModelRotationMatrix.t[2] = 0;
    renderModelRotationMatrix.t[1] = 0;
    renderModelRotationMatrix.t[0] = 0;
    if (OPX_80Params.m32_OPX_82Param4 != 0) {
        backgroundPoly = createBackgroundPoly(OPX_80Params.m0_OPX_80Params[0], OPX_80Params.m0_OPX_80Params[1],
                                          OPX_80Params.m0_OPX_80Params[2], OPX_80Params.m0_OPX_80Params[3],
                                          OPX_80Params.m0_OPX_80Params[4], OPX_80Params.m0_OPX_80Params[5],
                                          OPX_80Params.m0_OPX_80Params[6], OPX_80Params.m0_OPX_80Params[7],
                                          OPX_80Params.m10_OPX_81Params, OPX_80Params.m20_OPX_82Param0[0],
                                          OPX_80Params.m2C_OPX_82Param3[0], OPX_80Params.m2C_OPX_82Param3[1],
                                          OPX_80Params.m2C_OPX_82Param3[2]);
    }
    resetMemoryAllocStats(8, 0);
    fieldSceneState.m40_cameraAt2.vx = fieldSceneState.m290_actorArray[op9DVar1.m1BA_actorCameraTracked].mC_matrix.t[0] << 16;
    fieldSceneState.m40_cameraAt2.vy = fieldSceneState.m290_actorArray[op9DVar1.m1BA_actorCameraTracked].mC_matrix.t[1] << 16;
    fieldSceneState.m40_cameraAt2.vz = fieldSceneState.m290_actorArray[op9DVar1.m1BA_actorCameraTracked].mC_matrix.t[2] << 16;
    for (i = 0; i < fieldSceneState.m28C_totalObjects; i++) {
        createRotationMatrix(&fieldSceneState.m290_actorArray[i].m50_modelRotation, &fieldSceneState.m290_actorArray[i].mC_matrix);
        fieldSceneState.m290_actorArray[i].m2C_matrixBackup = fieldSceneState.m290_actorArray[i].mC_matrix;
    }
    loadMechaOVerlayAndRegisterMechas();
    reloadFieldActorsFromSaveBlock();
    currentFieldId1 = -1;
    currentFieldId0 = -1;
    setupObjectRenderModes();
    doPCCollisionCheckAfterLoading();
    if (OPX_80Params.m32_OPX_82Param4 == 0) {
        fieldUseGroundOT = shoudlGroundOTBeEnabled();
    } else {
        fieldUseGroundOT = 1;
    }
    for (i = 0; i < g_totalActors; i++) {
        if (fieldSceneState.m290_actorArray[i].m58_flags & 0x40) {
            sFieldScriptEntity* e = fieldSceneState.m290_actorArray[i].m4C_scriptEntity;
            if (!(e->m4_flags & 0x1000000)) {
                setSpriteActorAngle(fieldSceneState.m290_actorArray[i].m4_pVramSpriteSheet, fieldSceneState.m10E_cameraTan + e->m108_rotation3);
            } else {
                spriteActorSetDirection(fieldSceneState.m290_actorArray[i].m4_pVramSpriteSheet, e->m108_rotation3);
            }
        }
    }
}
