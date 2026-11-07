
#include "common.h"
#include "psyq.h"
#include "main/memory.h"
#include "main/sprite.h"
#include "main/debug_font.h"
#include "field/screen_effects.h"
#include "gte.h"
#include "pollhost.h"
#include "stack.h"
#include "field/field.h"
#include "pc_interpolation.h"
#include "pc_slide.h"
#include "pc_retail_heap.h"
#include "pc_speedup.h"

void func_80284EA4(void); /* debugger overlay */
s32 checkCameraCollision(VECTOR* pos, SVECTOR* line, s16* out);
s32 getAngleSin(s32 angle);
s32 getAngleCos(s32 angle);
extern s32 cameraCollisionState;
extern u16 compassData0[8];
extern u16 compassData1[8][16];
extern RECT compassDataRect;
extern s16 compassCardinalPointsLocation[4][2];
extern s16 compassArrowCurrentDirection;
extern s16 compassArrowTargetDirection;
extern s32 fieldCompassVar;
extern DR_MODE gDialogDrModes[2][16];
void drawCompassArrowSegment(OTag* ot, FieldQuad* quad, MATRIX* m, s32 buf);
void drawCompassLetters(OTag* ot, FieldQuad* quad, MATRIX* m, s32 buf);
extern u16 padButtonForScriptsPad1;            /* Noah: padButtonForScripts[1].m0_buttons */
extern u16 padButtonForDialogs;
extern u16 padButtonForDialogs2;
extern u16 g_padButtonForField;
extern u16 padButtonForField2;
extern u16 newPadButtonForScripts;            /* Noah: newPadButtonForScripts[0].m0_buttons (main) */
extern u16 newPadButtonForScripts2;            /* Noah: newPadButtonForScripts[1].m0_buttons (main) */
extern u16 newPadButtonForDialogs;            /* Noah: newPadButtonForDialogs (main) */
extern u16 newPadButtonForDialogs2;            /* Noah: newPadButtonForDialogs2 (main) */
extern u16 newPadButtonForField;            /* Noah: newPadButtonForField (main) */
extern u16 newPadButtonForField2;            /* Noah: newPadButtonForField2 (main) */
extern u16 inputAllowedMask2;
extern u16 inputAllowedMask;
extern s32 playMusicAuthorized;
extern s32 fieldMouseState[5]; /* mouse state (getMouseState) */
s32 loadInputFromVSyncBuffer(void);
void resetInputs(void);
s32 getMouseState(s32 port, s32* out);
#ifdef PLATFORM_PC
void portFieldSweepFrame(void); /* port/src/game/field/sweep.c */
#endif
extern s32 fieldProfileTimestamp;
extern s32 fieldCpuTime;
extern s32 paletteFadeUploadPending;
extern u8* paletteFadeBuffer;
extern RECT paletteFadeRect;
extern s32 fieldTransitionMode;
extern s32 dialogWindowFlag2;
void updateCamera(void);
void renderObjects(void);
void updateParticles(void);
void func_80281450(void);
void updateAndRenderScreenDistortion(void);
void renderDebugCameraDigits(void);
void drawWideImageSprites(void);
void func_80281400(void);
void updateDelayedFrees(void);
void shapeTransfert(void);
void executeFieldMoveImageCommands(void);
extern s32 fieldPolyCount;            /* Noah: fieldPolyCount (main) */
extern s32 fieldPolyCount2;            /* Noah: fieldPolyCount2 (main) */
extern s32 objectClippingMask;            /* Noah: objectClippingMask (main) */
extern s32 fieldModelDynamicVerticesUpdate_actorIndex;
extern s32 fieldModelDynamicVerticesUpdate_nodeIndex;
void setFogNearColor(u8 r, u8 g, u8 b);
void SetFarColor(s32 r, s32 g, s32 b);
void SetFogNearFar(s32 a, s32 b, s32 h);
void setupLightMatrix(MATRIX* m);
#ifndef PLATFORM_PC
void setAmbientColorFixed(s32 r, s32 g, s32 b); /* light.c: u16 parameters, passed unmasked here */
#else
void setAmbientColorFixed(u16 r, u16 g, u16 b);
#endif
void updateDynamicVertices(void* dyn);
s32 isObjectOcluded(sFieldEntitySub0* sub);
#ifdef PLATFORM_PC
extern s32 fieldMapNumber;
void pcFieldWideObject(sFieldEntitySub0* sub, MATRIX* proj, OTag* ot);
s32 pcFieldNpcMargin(void);
void pcFieldWideFrameStart(void);
#endif
s32 submitModelForRendering(ModelBlock* b, u8* prims, OTag* ot, s32 renderMode);
/* Retail calls it with four arguments: the fifth is whatever the stack holds.
 * After a field load (initFieldData -> doPCCollisionCheckAfterLoading, the
 * only caller) that slot holds a return address into
 * loadMechaOVerlayAndRegisterMechas left by an earlier call (Redux: US
 * 0x80077C78 for both calls), so EntityMoveCheck1 takes its "param_5 != 0,
 * >= 2" branch: the actor's ground height becomes its own height. Noah passes
 * 0, which instead leaves the walkmesh height and drops actors standing above
 * or below it (field sweep: fields 23, 26, 42...). */
#ifndef PLATFORM_PC
s32 EntityMoveCheck1(s32 actorId, s32 y, sFieldEntity* e, sFieldScriptEntity* se);
#else
#define EntityMoveCheck1(actorId, y, e, se) EntityMoveCheck1(actorId, y, e, se, 0x80077C78)
#endif
s32 findCharacterInParty(s32 character);
void initFollowStructForPlayer(s32 actorId);
extern s32 fieldScriptEntityAlreadyInitialized;
extern s32 fieldScriptInitVar0;
void deleteFieldEntitySub4(SpriteActorCore* actor);
SpriteActorCore* createSpriteActor(void* bundle, s16 clutX, s16 clutY, s16 vramX, s16 vramY, s16 arg5);
SpriteActorCore* createSpriteActorEX(void* bundle, s16 clutX, s16 clutY, s16 vramX, s16 vramY, s16 arg5, s32 vramParam);
void OP_INIT_ENTITY_SCRIPT_sub0Sub3(SpriteActorCore* actor, s32 count);
void getBoundingVolumeFromSprite(SpriteActorCore* actor, s32 unused, s32* pX, s32* pY, s32* pZ);
void OP_INIT_ENTITY_SCRIPT_sub0Sub5(SpriteActorCore* actor, s32 value);
void spriteActorSetPlayingAnimation(SpriteActorCore* actor, s32 animationId);
void fieldActorCallback(SpriteActorCore* actor);
extern s32 disableCharacterShadowsRendering;
extern s32 gearSwitchingEnabled;
extern s32 gDepthDivider;            /* OT depth shift (main) */
extern s32 mechaList3[];
extern CVECTOR fogNearColor;           /* fog colour for dpcs (main) */
u16 getCameraDirection(void);
void renderSpriteActor(SpriteActorCore* actor, OTag* ot);
void renderSpriteActorUpperPart(SpriteActorCore* actor, OTag* ot, s32 arg2);
void renderSpriteActorLowerPart(SpriteActorCore* actor, OTag* ot, s32 arg2);
extern s32 updateCameraInterpolationVar0;
extern s32 updateCameraInterpolationVar1;
extern FieldBss_800AF880 fieldSceneState;
void createRotationMatrix(SVECTOR* rot, MATRIX* m);
MATRIX* MulMatrix2(MATRIX* m0, MATRIX* m1);
void RotTrans(SVECTOR* v0, VECTOR* v1, s32* flag);
void AddPrims(void* ot, void* p0, void* p1);
void spriteActorSetColor(SpriteActorCore* actor, s32 r, s32 g, s32 b);
void func_802815B0(void);
extern s32 fieldDebugDisable;
extern s32 g_frameOddOrEven;
extern sFieldRenderContext* pCurrentFieldRenderingContext;
extern s32 fieldUseGroundOT;
extern s32 g_updateAllEntitiesSub2Var0;
extern u8 cameraDirectionToDollyBitmask[8];
extern s16 isFogSetup;
extern FieldBss_800B2184 op9DVar1;
extern s32 totalObjects;
extern s32 disableMechaRendering;
extern s32 D_800ADB50;
extern void* backgroundPoly;
extern s32 fieldMovieDisplayBuffer;
void setupObjectRenderModesSub0(sFieldEntitySub0* sub);
void updateMechasPosForField(void);
void renderMechas(MATRIX* m, MechaFieldArgs2* args, OTag* ot, s32 oddOrEven, s32 arg4);
void func_80281B00(const char* name);
void renderBackgroundPoly(void* poly, SVECTOR* eye, SVECTOR* at, MATRIX* m, OTag* ot, s32 oddOrEven);
void updateFieldInputs(void);
void updateCameraDolly(void);
s32 updateCameraInterpolationSub1(VECTOR* pos, s32 elevation);
void updateCameraInterpolationSub2(void);
void updateScriptAndMoveEntities(void);
s32 length2d(s32 x, s32 y);
void updateCameraInterpolation(void);
void setSpriteActorAngle(void* actor, s16 angle);
void spriteActorSetDirection(void* actor, s32 direction);
extern MATRIX g_cameraMatrix2;
extern MATRIX renderModelRotationMatrix;
extern s32 cameraDeltaTan;
void setIdentityMatrix(MATRIX* m);
void initFontPalettes(s16 x, s16 y);
extern u8 renderCharsDisabled;
void clearShapeTransfertTableEntry(s32 index);
void setCharacterRenderingOT(OTag* ot);
void setCurrentRenderingMatrix(MATRIX* m);
void uploadCharacterSprites(void);
void execSpritesCallbacksList1(void);
void execSpritesCallbacksList2(void);
void OP_INIT_ENTITY_SCRIPT_sub0Sub9(SpriteActorCore* actor);
void renderFieldCharacterSprites(OTag* ot, s32 oddOrEven);
void renderCharShadows(OTag* ot, s32 oddOrEven);
void exectueEntitiesUpdateFunction(void);
void stepDialogWindows(void);
void addDialogWindowsToOT(OTag* ot, s32 oddOrEven);
void lookAtDivided(MATRIX* m, VECTOR* eye, VECTOR* at, VECTOR* up);
void copyMatrix(MATRIX* dst, MATRIX* src);
void copyMatrixTranslation(MATRIX* dst, MATRIX* src);
void copyMatrixRotation(MATRIX* dst, MATRIX* src);
u32 stepInterpolateDirectionSub(s32 cur, s32 target, s32 step);
void computeCameraEyeFromAt(VECTOR* pos, VECTOR* center);

void resetMatrixTranslation(MATRIX* m) {
    m->t[2] = 0;
    m->t[1] = 0;
    m->t[0] = 0;
}

void computeProjectionMatrix(void) {
    VECTOR scale;
    MATRIX unused;
    MATRIX tmp;
    s32 flag[2];

    createRotationMatrix(&fieldSceneState.m150_projectionAngles, &fieldSceneState.m130_projectionTempMatrix);
    resetMatrixTranslation(&fieldSceneState.m130_projectionTempMatrix);
    CompMatrix(&fieldSceneState.m130_projectionTempMatrix, &fieldSceneState.m110_cameraMatrix, &tmp);
    copyMatrix(&fieldSceneState.m110_cameraMatrix, &tmp);
    createRotationMatrix(&fieldSceneState.m1D4_projectionAngles, &fieldSceneState.m224_projectionTempMatrix);
    resetMatrixTranslation(&fieldSceneState.m224_projectionTempMatrix);
    createRotationMatrix(&fieldSceneState.m1D4_projectionAngles, &fieldSceneState.m1E4_projectionMatrix);
    MulMatrix2(&fieldSceneState.m110_cameraMatrix, &fieldSceneState.m1E4_projectionMatrix);
    SetRotMatrix(&fieldSceneState.m110_cameraMatrix);
    SetTransMatrix(&fieldSceneState.m110_cameraMatrix);
    RotTrans(&fieldSceneState.m1DC_cameraRotation, (VECTOR*)fieldSceneState.m1E4_projectionMatrix.t, flag);
    scale.vx = fieldSceneState.m244_worldScaleFactor;
    scale.vy = fieldSceneState.m244_worldScaleFactor;
    scale.vz = fieldSceneState.m244_worldScaleFactor;
    ScaleMatrix(&fieldSceneState.m1E4_projectionMatrix, &scale);
    SetRotMatrix(&fieldSceneState.m1E4_projectionMatrix);
    SetTransMatrix(&fieldSceneState.m1E4_projectionMatrix);
}

void updateScriptActor3dRotation(s32 index) {
    VECTOR scale;

    scale.vx = fieldSceneState.m290_actorArray[index].m4C_scriptEntity->mF4_scale3d[0];
    scale.vy = fieldSceneState.m290_actorArray[index].m4C_scriptEntity->mF4_scale3d[1];
    scale.vz = fieldSceneState.m290_actorArray[index].m4C_scriptEntity->mF4_scale3d[2];
    createRotationMatrix(&fieldSceneState.m290_actorArray[index].m50_modelRotation, &fieldSceneState.m290_actorArray[index].mC_matrix);
    ScaleMatrix(&fieldSceneState.m290_actorArray[index].mC_matrix, &scale);
}

void setProjectionMatrixForField(void) {
    computeProjectionMatrix();
    SetRotMatrix(&fieldSceneState.m1E4_projectionMatrix);
    SetTransMatrix(&fieldSceneState.m1E4_projectionMatrix);
    if (fieldDebugDisable == 0) {
        func_802815B0();
    }
}

s32 updateCameraDollySub0(u32 mask, s32 dir) {
    s32 i;
    s32 n;

    for (i = 0, n = 0; i < 8; i++) {
        if (!(mask & cameraDirectionToDollyBitmask[dir++ & 7])) {
            return n;
        }
        n++;
    }
    return 0;
}

s32 updateCameraDollySub1(u32 mask, s32 dir) {
    s32 i;
    s32 n;

    for (i = 0, n = 0; i < 8; i++) {
        if (!(mask & cameraDirectionToDollyBitmask[dir-- & 7])) {
            return n;
        }
        n++;
    }
    return 0;
}

/* Noah: updateCameraInterpolationSub1Sub0 (intersection of two 2D lines) */
void updateCameraInterpolationSub1Sub0(s16* a, s16* b, s16* out) {
    VECTOR na;
    VECTOR nb;
    VECTOR d;
    s32 t;
    s32 r;

    d.vx = a[2] - a[0];
    d.vy = 0;
    d.vz = a[3] - a[1];
    VectorNormal(&d, &na);
    d.vx = b[2] - b[0];
    d.vy = 0;
    d.vz = b[3] - b[1];
    VectorNormal(&d, &nb);
    t = (nb.vx * na.vz - nb.vz * na.vx) >> 12;
    if (t == 0) {
        r = 0;
    } else {
        r = ((b[1] - a[1]) * na.vx - (b[0] - a[0]) * na.vz) / t;
    }
    out[0] = b[0] + (r * nb.vx >> 12);
    out[1] = b[1] + (r * nb.vz >> 12);
}

/* Noah: resetCameraData. The stores after setIdentityMatrix are relative to
 * the register holding &m104: GCC only does that once a pointer to the whole
 * aggregate is live, and it then folds the p-> stores back to absolute ones
 * (retail: cameraEye.vy/vz absolute, everything else relative). */
void resetCameraData(void) {
    FieldBss_800AF880* p;

    fieldSceneState.m104_op99Var5 = 8;
    fieldSceneState.m108_op99Var6 = 8;
    fieldSceneState.m16C_cameraDollyVar1 = 0x400000;
    fieldSceneState.m170_opA0_var1 = 0x8000000;
    fieldSceneState.m164_cameraRotationBetweenEyeAndAt.vy = 0x800;
    fieldSceneState.m1B0_cameraShakeAmplitude[2] = 0;
    fieldSceneState.m1B0_cameraShakeAmplitude[1] = 0;
    fieldSceneState.m1B0_cameraShakeAmplitude[0] = 0;
    fieldSceneState.m1BC_cameraShakeStep[2] = 0;
    fieldSceneState.m1BC_cameraShakeStep[1] = 0;
    fieldSceneState.m1BC_cameraShakeStep[0] = 0;
    fieldSceneState.m1A8_cameraShakeEnabled = 0;
    fieldSceneState.m1AC_cameraShakeStopping = 0;
    fieldSceneState.m158_op99Var7 = 0;
    fieldSceneState.m10E_cameraTan = 0;
    fieldSceneState.m10C_camera2Tan = 0;
    fieldSceneState.m174_dollySet = 0;
    fieldSceneState.m175_dollyStop = 0;
    fieldSceneState.m176_cameraDollyVar0 = 0;
    fieldSceneState.m164_cameraRotationBetweenEyeAndAt.vx = 0;
    fieldSceneState.m164_cameraRotationBetweenEyeAndAt.vz = 0;
    fieldSceneState.m18C_opA0_var0 = 0x800;
    fieldSceneState.m150_projectionAngles.vx = 0;
    fieldSceneState.m150_projectionAngles.vy = 0;
    fieldSceneState.m150_projectionAngles.vz = 0;
    setIdentityMatrix(&fieldSceneState.m130_projectionTempMatrix);
    p = &fieldSceneState;
    fieldSceneState.m0_cameraEye.vx = 0;
    p->m0_cameraEye.vy = 0;
    p->m0_cameraEye.vz = 0;
    fieldSceneState.m10_cameraAt.vx = 0;
    fieldSceneState.m10_cameraAt.vy = 0;
    fieldSceneState.m10_cameraAt.vz = 0;
    fieldSceneState.m20_cameraUp.vx = 0;
    fieldSceneState.m20_cameraUp.vy = 0x10000000;
    fieldSceneState.m20_cameraUp.vz = 0;
    fieldSceneState.m60_cameraOffset.vx = 0;
    fieldSceneState.m60_cameraOffset.vy = 0;
    fieldSceneState.m60_cameraOffset.vz = 0;
    fieldSceneState.m30_cameraEye2.vx = 0;
    fieldSceneState.m30_cameraEye2.vy = 0;
    fieldSceneState.m30_cameraEye2.vz = 0;
    fieldSceneState.m40_cameraAt2.vx = 0;
    fieldSceneState.m40_cameraAt2.vy = 0;
    fieldSceneState.m40_cameraAt2.vz = 0;
    fieldSceneState.m50_cameraUp2.vx = 0;
    fieldSceneState.m50_cameraUp2.vy = 0x10000000;
    fieldSceneState.m50_cameraUp2.vz = 0;
    fieldSceneState.m17C_sceneDIP = 0x1E;
    fieldSceneState.m178_sceneSCRZ = 0x200;
    fieldSceneState.m180_OP_A4Var0 = 0;
    fieldSceneState.m190_OP_B6SubVar0 = 0;
    fieldSceneState.m19C_op9DVar0 = 0;
    fieldSceneState.m17E_sceneScale = 0x1000;
    fieldSceneState.mB4_cameraInterpolationMode = 0;
    fieldSceneState.mBC_cameraInterpolationFlags = 0;
    fieldSceneState.mBE_cameraInterpolationTargetNumSteps = 0;
    fieldSceneState.mE0_cameraInterpolationPositionNumSteps = 0;
}

void updateCameraDolly(void) {
    s32 a;

    if (fieldSceneState.m174_dollySet != 0xFF && fieldSceneState.m175_dollyStop != 0xFF) {
        if (fieldSceneState.m176_cameraDollyVar0 == 0) {
            if (fieldSceneState.m174_dollySet & cameraDirectionToDollyBitmask[(fieldSceneState.m164_cameraRotationBetweenEyeAndAt.vy & 0xFFF) >> 9]) {
                if (fieldSceneState.m16C_cameraDollyVar1 != -0x400000 && fieldSceneState.m16C_cameraDollyVar1 != 0x400000) {
                    fieldSceneState.m16C_cameraDollyVar1 = 0x400000;
                    fieldSceneState.m18C_opA0_var0 += 0x200;
                }
                fieldSceneState.m176_cameraDollyVar0 = 8;
            }
            if (fieldSceneState.m175_dollyStop & cameraDirectionToDollyBitmask[(fieldSceneState.m164_cameraRotationBetweenEyeAndAt.vy & 0xFFF) >> 9]) {
                a = updateCameraDollySub0(fieldSceneState.m175_dollyStop, (fieldSceneState.m164_cameraRotationBetweenEyeAndAt.vy & 0xFFF) >> 9);
                if (updateCameraDollySub1(fieldSceneState.m175_dollyStop, (fieldSceneState.m164_cameraRotationBetweenEyeAndAt.vy & 0xFFF) >> 9) < a) {
                    fieldSceneState.m16C_cameraDollyVar1 = -0x400000;
                    fieldSceneState.m18C_opA0_var0 -= 0x200;
                } else {
                    fieldSceneState.m16C_cameraDollyVar1 = 0x400000;
                    fieldSceneState.m18C_opA0_var0 += 0x200;
                }
                fieldSceneState.m176_cameraDollyVar0 = 8;
            }
        }
        if ((padButtonForScripts & 4) && !(fieldSceneState.m158_op99Var7 & 0x8000) && fieldSceneState.m176_cameraDollyVar0 == 0 &&
            !(fieldSceneState.m175_dollyStop & cameraDirectionToDollyBitmask[(fieldSceneState.m164_cameraRotationBetweenEyeAndAt.vy - 0x200 & 0xFFF) >> 9])) {
            fieldSceneState.m16C_cameraDollyVar1 = -0x400000;
            fieldSceneState.m176_cameraDollyVar0 = 8;
            fieldSceneState.m18C_opA0_var0 -= 0x200;
        }
        if ((padButtonForScripts & 8) && !(fieldSceneState.m158_op99Var7 & 0x8000) && fieldSceneState.m176_cameraDollyVar0 == 0 &&
            !(fieldSceneState.m175_dollyStop & cameraDirectionToDollyBitmask[(fieldSceneState.m164_cameraRotationBetweenEyeAndAt.vy + 0x200 & 0xFFF) >> 9])) {
            fieldSceneState.m16C_cameraDollyVar1 = 0x400000;
            fieldSceneState.m176_cameraDollyVar0 = 8;
            fieldSceneState.m18C_opA0_var0 += 0x200;
        }
    }
    if (fieldSceneState.m176_cameraDollyVar0 != 0) {
        fieldSceneState.m170_opA0_var1 += fieldSceneState.m16C_cameraDollyVar1;
        fieldSceneState.m164_cameraRotationBetweenEyeAndAt.vy = fieldSceneState.m170_opA0_var1 >> 16;
        if (--fieldSceneState.m176_cameraDollyVar0 == 0) {
            fieldSceneState.m164_cameraRotationBetweenEyeAndAt.vy = fieldSceneState.m18C_opA0_var0;
        }
    } else {
        fieldSceneState.m164_cameraRotationBetweenEyeAndAt.vy = fieldSceneState.m18C_opA0_var0;
    }
    if (fieldDebugDisable == 0) {
        func_80284EA4();
    }
}

s32 updateCameraInterpolationSub1(VECTOR* pos, s32 elevation) {
    SVECTOR col[2];
    s16 line[4];
    s16 col2[4];
    s16 out[2];
    VECTOR p;
    MATRIX unused;

    p.vx = pos->vx;
    p.vy = 0;
    p.vz = pos->vz;
    if (checkCameraCollision(&p, col, col2) == -1) {
        line[0] = col[0].vx;
        line[1] = col[0].vz;
        line[2] = col[1].vx;
        line[3] = col[1].vz;
        updateCameraInterpolationSub1Sub0(line, col2, out);
        fieldSceneState.m40_cameraAt2.vx = out[0] << 16;
        fieldSceneState.m40_cameraAt2.vz = out[1] << 16;
        if (op9DVar1.m49_cameraFollowHeight == 0) {
            if (cameraCollisionState == 0) {
                fieldSceneState.m40_cameraAt2.vy = elevation << 16;
                cameraCollisionState = 1;
            }
        } else {
            fieldSceneState.m40_cameraAt2.vy = pos->vy - 0x200000;
        }
    } else {
        fieldSceneState.m40_cameraAt2.vx = pos->vx;
        cameraCollisionState = 0;
        fieldSceneState.m40_cameraAt2.vy = pos->vy;
        fieldSceneState.m40_cameraAt2.vz = pos->vz;
        fieldSceneState.m40_cameraAt2.vy -= 0x200000;
    }
    fieldSceneState.m30_cameraEye2.vy = ((-(getAngleSin((fieldSceneState.m17C_sceneDIP * 0x5B >> 3) + 0xC00) * fieldSceneState.m178_sceneSCRZ * 0x20) >> 16) *
                                    fieldSceneState.m17E_sceneScale * 16) + fieldSceneState.m40_cameraAt2.vy;
    fieldSceneState.m30_cameraEye2.vz = ((getAngleCos((fieldSceneState.m17C_sceneDIP * 0x5B >> 3) + 0xC00) * fieldSceneState.m178_sceneSCRZ * 0x20 >> 16) *
                                    fieldSceneState.m17E_sceneScale * 16) + fieldSceneState.m40_cameraAt2.vz;
    fieldSceneState.m30_cameraEye2.vx = fieldSceneState.m40_cameraAt2.vx;
    computeCameraEyeFromAt(&fieldSceneState.m30_cameraEye2, &fieldSceneState.m40_cameraAt2);
    if (fieldSceneState.m158_op99Var7 & 1) {
        if (fieldSceneState.m19C_op9DVar0 != 0) {
            fieldSceneState.m1A0_op9DVar3 += fieldSceneState.m1A4_op9DVar2;
            fieldSceneState.m17E_sceneScale = fieldSceneState.m1A0_op9DVar3 >> 16;
        }
        if (--fieldSceneState.m19C_op9DVar0 == 0) {
            fieldSceneState.m158_op99Var7 &= 0xFFFE;
        }
    }
    if (fieldSceneState.m158_op99Var7 & 8) {
        fieldSceneState.m184_OP_A4Var1 += fieldSceneState.m188_OP_A4Var2;
        fieldSceneState.m17C_sceneDIP = fieldSceneState.m184_OP_A4Var1 >> 16;
        if (--fieldSceneState.m180_OP_A4Var0 == 0) {
            fieldSceneState.m158_op99Var7 &= 0xFFF7;
        }
    }
    FALLOFF_RETURN(0); /* result unused */
}

void updateCameraInterpolationSub2(void) {
    s32 at;
    s32 eye;
    s32 d;

    if (fieldSceneState.m158_op99Var7 & 0x10) {
        if (fieldSceneState.m190_OP_B6SubVar0 != 0) {
            fieldSceneState.m194_OP_B6Var2 += fieldSceneState.m198_OP_B6Var1;
            fieldSceneState.m178_sceneSCRZ = fieldSceneState.m194_OP_B6Var2 >> 16;
        }
        if (--fieldSceneState.m190_OP_B6SubVar0 < 0) {
            fieldSceneState.m158_op99Var7 &= 0xFFEF;
            fieldSceneState.m190_OP_B6SubVar0 = 0;
        }
    }
    if (op9DVar1.m54_cameraSnapFrames != 0) {
        fieldSceneState.m104_op99Var5 = 1;
        fieldSceneState.m108_op99Var6 = 1;
        op9DVar1.m54_cameraSnapFrames--;
    }
    at = fieldSceneState.m104_op99Var5 * fieldSceneState.m104_op99Var5;
    eye = fieldSceneState.m108_op99Var6 * fieldSceneState.m108_op99Var6;
    if ((fieldSceneState.m0_cameraEye.vx >> 16) != (fieldSceneState.m30_cameraEye2.vx >> 16)) {
        d = (fieldSceneState.m30_cameraEye2.vx - fieldSceneState.m0_cameraEye.vx) >> 16;
        if (d * d >= eye) {
            fieldSceneState.m0_cameraEye.vx += R3000_DIV(fieldSceneState.m30_cameraEye2.vx - fieldSceneState.m0_cameraEye.vx, fieldSceneState.m108_op99Var6);
        }
    }
    if ((fieldSceneState.m0_cameraEye.vz >> 16) != (fieldSceneState.m30_cameraEye2.vz >> 16)) {
        d = (fieldSceneState.m30_cameraEye2.vz - fieldSceneState.m0_cameraEye.vz) >> 16;
        if (d * d >= eye) {
            fieldSceneState.m0_cameraEye.vz += R3000_DIV(fieldSceneState.m30_cameraEye2.vz - fieldSceneState.m0_cameraEye.vz, fieldSceneState.m108_op99Var6);
        }
    }
    if ((fieldSceneState.m0_cameraEye.vy >> 16) != (fieldSceneState.m30_cameraEye2.vy >> 16)) {
        d = (fieldSceneState.m30_cameraEye2.vy - fieldSceneState.m0_cameraEye.vy) >> 16;
        if (d * d >= eye) {
            fieldSceneState.m0_cameraEye.vy += R3000_DIV(fieldSceneState.m30_cameraEye2.vy - fieldSceneState.m0_cameraEye.vy, fieldSceneState.m108_op99Var6);
        }
    }
    if ((fieldSceneState.m10_cameraAt.vx >> 16) != (fieldSceneState.m40_cameraAt2.vx >> 16)) {
        d = (fieldSceneState.m40_cameraAt2.vx - fieldSceneState.m10_cameraAt.vx) >> 16;
        if (d * d >= at) {
            fieldSceneState.m10_cameraAt.vx += R3000_DIV(fieldSceneState.m40_cameraAt2.vx - fieldSceneState.m10_cameraAt.vx, fieldSceneState.m104_op99Var5);
        }
    }
    if ((fieldSceneState.m10_cameraAt.vz >> 16) != (fieldSceneState.m40_cameraAt2.vz >> 16)) {
        d = (fieldSceneState.m40_cameraAt2.vz - fieldSceneState.m10_cameraAt.vz) >> 16;
        if (d * d >= at) {
            fieldSceneState.m10_cameraAt.vz += R3000_DIV(fieldSceneState.m40_cameraAt2.vz - fieldSceneState.m10_cameraAt.vz, fieldSceneState.m104_op99Var5);
        }
    }
    if ((fieldSceneState.m10_cameraAt.vy >> 16) != (fieldSceneState.m40_cameraAt2.vy >> 16)) {
        d = (fieldSceneState.m40_cameraAt2.vy - fieldSceneState.m10_cameraAt.vy) >> 16;
        if (d * d >= at) {
            fieldSceneState.m10_cameraAt.vy += R3000_DIV(fieldSceneState.m40_cameraAt2.vy - fieldSceneState.m10_cameraAt.vy, fieldSceneState.m104_op99Var5);
        }
    }
    fieldSceneState.m60_cameraOffset.vx = 0;
    fieldSceneState.m60_cameraOffset.vy = 0;
    fieldSceneState.m60_cameraOffset.vz = 0;
    if (fieldSceneState.m1A8_cameraShakeEnabled != 0) {
        if (fieldSceneState.m1AA_cameraShakeCounter != 0) {
            fieldSceneState.m1B0_cameraShakeAmplitude[0] += fieldSceneState.m1BC_cameraShakeStep[0];
            fieldSceneState.m1B0_cameraShakeAmplitude[1] += fieldSceneState.m1BC_cameraShakeStep[1];
            fieldSceneState.m1B0_cameraShakeAmplitude[2] += fieldSceneState.m1BC_cameraShakeStep[2];
        } else if (fieldSceneState.m1AC_cameraShakeStopping != 0) {
            fieldSceneState.m1B0_cameraShakeAmplitude[2] = 0;
            fieldSceneState.m1B0_cameraShakeAmplitude[1] = 0;
            fieldSceneState.m1B0_cameraShakeAmplitude[0] = 0;
            fieldSceneState.m1A8_cameraShakeEnabled = 0;
            fieldSceneState.m1AC_cameraShakeStopping = 0;
        }
        fieldSceneState.m60_cameraOffset.vx = rand() * (fieldSceneState.m1B0_cameraShakeAmplitude[0] >> 16);
        fieldSceneState.m60_cameraOffset.vy = rand() * (fieldSceneState.m1B0_cameraShakeAmplitude[1] >> 16);
        fieldSceneState.m60_cameraOffset.vz = rand() * (fieldSceneState.m1B0_cameraShakeAmplitude[2] >> 16);
        if (fieldSceneState.m60_cameraOffset.vx < 0) {
            fieldSceneState.m60_cameraOffset.vx = 0;
            fieldSceneState.m1B0_cameraShakeAmplitude[0] = 0;
        }
        if (fieldSceneState.m60_cameraOffset.vy < 0) {
            fieldSceneState.m60_cameraOffset.vy = 0;
            fieldSceneState.m1B0_cameraShakeAmplitude[1] = 0;
        }
        if (fieldSceneState.m60_cameraOffset.vz < 0) {
            fieldSceneState.m60_cameraOffset.vz = 0;
            fieldSceneState.m1B0_cameraShakeAmplitude[2] = 0;
        }
        if (fieldSceneState.m1AA_cameraShakeCounter > 0) {
            fieldSceneState.m1AA_cameraShakeCounter--;
        }
    }
}

void updateCameraInterpolation(void) {
    VECTOR pos;
    VECTOR normal;
    SVECTOR tri;
    s32 l3;
    s32 l4;

    switch (fieldSceneState.mB4_cameraInterpolationMode) {
    case 2:
        updateCameraInterpolationVar0 = 0;
        updateCameraInterpolationVar1++;
        if (updateCameraInterpolationVar1 > 0x40) {
            fieldSceneState.mB4_cameraInterpolationMode = 0;
        }
        goto common;
    case 0:
        updateCameraInterpolationVar1 = 0;
        if (!(updateCameraInterpolationVar0 & 3)) {
            if (fieldSceneState.m104_op99Var5 < 9) {
                fieldSceneState.m104_op99Var5 = 8;
            } else {
                fieldSceneState.m104_op99Var5 -= 2;
            }
            if (fieldSceneState.m108_op99Var6 < 9) {
                fieldSceneState.m108_op99Var6 = 8;
            } else {
                fieldSceneState.m108_op99Var6 -= 2;
            }
        }
        updateCameraInterpolationVar0++;
common:
        updateCameraDolly();
        pos.vx = fieldSceneState.m290_actorArray[op9DVar1.m1BA_actorCameraTracked].m4C_scriptEntity->m20_position.vx;
        pos.vy = fieldSceneState.m290_actorArray[op9DVar1.m1BA_actorCameraTracked].m4C_scriptEntity->m20_position.vy;
        pos.vz = fieldSceneState.m290_actorArray[op9DVar1.m1BA_actorCameraTracked].m4C_scriptEntity->m20_position.vz;
        updateCameraInterpolationSub1(&pos, fieldSceneState.m290_actorArray[op9DVar1.m1BA_actorCameraTracked].m4C_scriptEntity->m72_elevation);
        if (!(fieldSceneState.m158_op99Var7 & 0x4000)) {
            findTriangleInWalkMesh(fieldSceneState.m30_cameraEye2.vx >> 16, fieldSceneState.m30_cameraEye2.vz >> 16, fieldSceneState.m2D4_numWalkMesh - 1,
                          &tri, &normal);
            if ((fieldSceneState.m30_cameraEye2.vy >> 16) > tri.vy) {
                fieldSceneState.m30_cameraEye2.vy = tri.vy << 16;
            }
        }
        if (fieldSceneState.mB4_cameraInterpolationMode == 2) {
            l3 = length2d((fieldSceneState.m40_cameraAt2.vx >> 16) - (fieldSceneState.m10_cameraAt.vx >> 16),
                          (fieldSceneState.m40_cameraAt2.vz >> 16) - (fieldSceneState.m10_cameraAt.vz >> 16));
            l4 = length2d((fieldSceneState.m30_cameraEye2.vx >> 16) - (fieldSceneState.m0_cameraEye.vx >> 16),
                          (fieldSceneState.m30_cameraEye2.vz >> 16) - (fieldSceneState.m0_cameraEye.vz >> 16));
            if (l3 < 0x80 && l4 < 0x80) {
                fieldSceneState.mB4_cameraInterpolationMode = 0;
            }
        }
        break;
    case 1:
        updateCameraInterpolationVar0 = 0;
        updateCameraInterpolationVar1 = 0;
        if (fieldSceneState.mBC_cameraInterpolationFlags & 1) {
            if (fieldSceneState.mBE_cameraInterpolationTargetNumSteps != 0) {
                fieldSceneState.mC0_cameraInterpolationTargetStartPosition[0] += fieldSceneState.mD0_cameraInterpolationTargetStep[0];
                fieldSceneState.mC0_cameraInterpolationTargetStartPosition[1] += fieldSceneState.mD0_cameraInterpolationTargetStep[1];
                fieldSceneState.mC0_cameraInterpolationTargetStartPosition[2] += fieldSceneState.mD0_cameraInterpolationTargetStep[2];
            }
            if (--fieldSceneState.mBE_cameraInterpolationTargetNumSteps == 0) {
                fieldSceneState.mBC_cameraInterpolationFlags &= ~1;
            }
            fieldSceneState.m40_cameraAt2.vx = fieldSceneState.mC0_cameraInterpolationTargetStartPosition[0];
            fieldSceneState.m40_cameraAt2.vy = fieldSceneState.mC0_cameraInterpolationTargetStartPosition[1];
            fieldSceneState.m40_cameraAt2.vz = fieldSceneState.mC0_cameraInterpolationTargetStartPosition[2];
        }
        if (fieldSceneState.mBC_cameraInterpolationFlags & 2) {
            if (fieldSceneState.mE0_cameraInterpolationPositionNumSteps != 0) {
                fieldSceneState.mE4_cameraInterpolationStartPosition[0] += fieldSceneState.mF4_cameraInterpolationPositionStep[0];
                fieldSceneState.mE4_cameraInterpolationStartPosition[1] += fieldSceneState.mF4_cameraInterpolationPositionStep[1];
                fieldSceneState.mE4_cameraInterpolationStartPosition[2] += fieldSceneState.mF4_cameraInterpolationPositionStep[2];
            }
            if (--fieldSceneState.mE0_cameraInterpolationPositionNumSteps == 0) {
                fieldSceneState.mBC_cameraInterpolationFlags &= ~2;
            }
            fieldSceneState.m30_cameraEye2.vx = fieldSceneState.mE4_cameraInterpolationStartPosition[0];
            fieldSceneState.m30_cameraEye2.vy = fieldSceneState.mE4_cameraInterpolationStartPosition[1];
            fieldSceneState.m30_cameraEye2.vz = fieldSceneState.mE4_cameraInterpolationStartPosition[2];
        }
        break;
    }
    updateCameraInterpolationSub2();
    fieldSceneState.m164_cameraRotationBetweenEyeAndAt.vy &= 0xFFF;
}

/* Noah: computeCameraEyeFromAt */
void computeCameraEyeFromAt(VECTOR* pos, VECTOR* center) {
    MATRIX m;
    VECTOR d;
    VECTOR r;

    PushMatrix();
    createRotationMatrix(&fieldSceneState.m164_cameraRotationBetweenEyeAndAt, &m);
    d.vx = center->vx - pos->vx;
    d.vy = center->vy - pos->vy;
    d.vz = center->vz - pos->vz;
    ApplyMatrixLV(&m, &d, &r);
    pos->vx = r.vx + center->vx;
    pos->vz = r.vz + center->vz;
    PopMatrix();
}

/* SVECTOR-ish (s32 pairs) -> keep the high halves: v[i] = v[i] >> 16 */
void truncateVectorFixedPoint(s32* v) {
    v[0] = ((s16*)v)[1];
    v[1] = ((s16*)v)[3];
    v[2] = ((s16*)v)[5];
}

void lookAtDivided(MATRIX* m, VECTOR* eye, VECTOR* at, VECTOR* up) {
    VECTOR d;
    VECTOR z;
    VECTOR x;
    VECTOR y;
    SVECTOR e;

    d.vx = (at->vx - eye->vx) >> 16;
    d.vy = (at->vy - eye->vy) >> 16;
    d.vz = (at->vz - eye->vz) >> 16;
    y.vx = up->vx;
    y.vy = up->vy;
    y.vz = up->vz;
    y.vx >>= 16;
    y.vy >>= 16;
    y.vz >>= 16;
    VectorNormal(&d, &z);
    OuterProduct12(&y, &z, &d);
    VectorNormal(&d, &x);
    OuterProduct12(&z, &x, &d);
    VectorNormal(&d, &y);
    m->m[0][0] = x.vx;
    m->m[0][1] = x.vy;
    m->m[0][2] = x.vz;
    m->m[1][0] = y.vx;
    m->m[1][1] = y.vy;
    m->m[1][2] = y.vz;
    m->m[2][0] = z.vx;
    m->m[2][1] = z.vy;
    m->m[2][2] = z.vz;
    e.vx = (eye->vx >> 16) * 3;
    e.vy = (eye->vy >> 16) * 3;
    e.vz = (eye->vz >> 16) * 3;
    ApplyMatrix(m, &e, &d);
    m->t[0] = -d.vx;
    m->t[1] = -d.vy;
    m->t[2] = -d.vz;
}

u32 stepInterpolateDirectionSub(s32 cur, s32 target, s32 step) {
    if (((cur - target) & 0xFFF) < 0x800) {
        cur -= step;
        if (((cur - target) & 0xFFF) >= 0x800) {
            cur = target;
        }
    } else {
        cur += step;
        if (((cur - target) & 0xFFF) < 0x800) {
            cur = target;
        }
    }
    return cur & 0xFFF;
}

u32 stepInterpolateDirection(s32 cur, s32 target, s32 step) {
    u32 r;

    if (g_updateAllEntitiesSub2Var0 == 0) {
        r = stepInterpolateDirectionSub(cur, target, step);
    } else {
        r = target & 0xFFF;
    }
    return r;
}

void updateAllEntities(void) {
    VECTOR eye;
    VECTOR at;
    s32 i;
    sFieldScriptEntity* se;
    s32 step;
    u32 target;
    u32 flags;
    s32 cur;

    updateScriptAndMoveEntities();
    createRotationMatrix(&op9DVar1.m0_renderModelRotationAngles, &renderModelRotationMatrix);
    renderModelRotationMatrix.t[2] = 0;
    renderModelRotationMatrix.t[1] = 0;
    renderModelRotationMatrix.t[0] = 0;
    fieldSceneState.m10E_cameraTan = ratan2(fieldSceneState.m10_cameraAt.vz - fieldSceneState.m0_cameraEye.vz,
                                       fieldSceneState.m10_cameraAt.vx - fieldSceneState.m0_cameraEye.vx) - 0x400;
    fieldSceneState.m10C_camera2Tan = ratan2(fieldSceneState.m40_cameraAt2.vz - fieldSceneState.m30_cameraEye2.vz,
                                        fieldSceneState.m40_cameraAt2.vx - fieldSceneState.m30_cameraEye2.vx) - 0x400;
    cameraDeltaTan = ratan2(length2d((fieldSceneState.m10_cameraAt.vx - fieldSceneState.m0_cameraEye.vx) >> 16,
                                 (fieldSceneState.m10_cameraAt.vz - fieldSceneState.m0_cameraEye.vz) >> 16),
                        (fieldSceneState.m10_cameraAt.vy - fieldSceneState.m0_cameraEye.vy) >> 16);
    STACK_SWITCH(PSX_SCRATCH(0x1F8003FC));
    updateCameraInterpolation();
    STACK_RESTORE();
    eye.vx = fieldSceneState.m0_cameraEye.vx;
    eye.vy = fieldSceneState.m0_cameraEye.vy;
    eye.vz = fieldSceneState.m0_cameraEye.vz;
    eye.vx += fieldSceneState.m60_cameraOffset.vx;
    eye.vy += fieldSceneState.m60_cameraOffset.vy;
    eye.vz += fieldSceneState.m60_cameraOffset.vz;
    at.vx = fieldSceneState.m10_cameraAt.vx;
    at.vy = fieldSceneState.m10_cameraAt.vy;
    at.vz = fieldSceneState.m10_cameraAt.vz;
    at.vx += fieldSceneState.m60_cameraOffset.vx;
    at.vy += fieldSceneState.m60_cameraOffset.vy;
    at.vz += fieldSceneState.m60_cameraOffset.vz;
    if (g_updateAllEntitiesSub2Var0 != 0) {
        lookAtDivided(&fieldSceneState.m110_cameraMatrix, &eye, &at, &fieldSceneState.m20_cameraUp);
        g_cameraMatrix2 = fieldSceneState.m110_cameraMatrix;
    } else {
        fieldSceneState.m110_cameraMatrix = g_cameraMatrix2;
        lookAtDivided(&g_cameraMatrix2, &eye, &at, &fieldSceneState.m20_cameraUp);
    }
    STACK_SWITCH(PSX_SCRATCH(0x1F8003FC));
    setProjectionMatrixForField();
    STACK_RESTORE();
    for (i = 0; i < g_totalActors; i++) {
        if ((fieldSceneState.m290_actorArray[i].m58_flags & 0xF40) && !(fieldSceneState.m290_actorArray[i].m58_flags & 0x20)) {
            se = fieldSceneState.m290_actorArray[i].m4C_scriptEntity;
            flags = se->m4_flags;
            if (!(flags & 0x100000) && (flags & 0x600) != 0x200) {
                if (!(se->m0_fieldScriptFlags & 0x8000)) {
                    target = se->m14_currentTriangleFlag;
                    if (!(target & 0x200000) || (step = 0x200, (se->m0_fieldScriptFlags & 0x1800))) {
                        if (!(flags & 0x2000)) {
                            step = se->m11E_rotationSpeed;
                        } else {
                            step = op9DVar1.m30_spriteTurnSpeed;
                        }
                        cur = se->m108_rotation3;
                        target = se->m106_currentRotation;
                    } else {
                        target >>= 11;
                        target -= 2;
                        target &= 7;
                        target <<= 9;
                        cur = se->m108_rotation3;
                    }
                    se->m108_rotation3 = stepInterpolateDirection(cur, target, step);
                }
                if (renderCharsDisabled == 0) {
                    if (!(se->m4_flags & 0x1000000)) {
                        setSpriteActorAngle(fieldSceneState.m290_actorArray[i].m4_pVramSpriteSheet,
                                      fieldSceneState.m10E_cameraTan + fieldSceneState.m290_actorArray[i].m4C_scriptEntity->m108_rotation3);
                    } else {
                        spriteActorSetDirection(fieldSceneState.m290_actorArray[i].m4_pVramSpriteSheet,
                                                fieldSceneState.m290_actorArray[i].m4C_scriptEntity->m108_rotation3);
                    }
                }
            }
        }
    }
    if (fieldDebugDisable == 0) {
        func_80281B00("MATRIX    ");
    }
}

void setupObjectRenderModes(void) {
    s32 i;
    sFieldEntity* e;
    sFieldEntitySub0* sub;

    for (i = 0; i < totalObjects; i++) {
        e = &fieldSceneState.m290_actorArray[i];
        if (!(e->m58_flags & 0x40)) {
            sub = e->m0_model;
            setupObjectRenderModesSub0(sub);
            if (isFogSetup != 0) {
                if (e->m58_flags & 0x10) {
                    sub->m12_renderMode = 5;
                } else {
                    sub->m12_renderMode = 4;
                }
            } else if (e->m58_flags & 0xC) {
                sub->m12_renderMode = 1;
            } else if (e->m58_flags & 0x4000) {
                sub->m12_renderMode = 3;
            } else if (e->m58_flags & 0x10) {
                sub->m12_renderMode = 2;
            } else {
                sub->m12_renderMode = 0;
            }
        }
    }
}

/* Noah: clearFieldOrderingTable (first half) */
void setupFieldRenderingContext(void) {
    if (fieldDebugDisable == 0) {
        POLLHOST();
    }
    g_frameOddOrEven = (g_frameOddOrEven + 1) % 2;
    pCurrentFieldRenderingContext = &fieldRenderContext[g_frameOddOrEven];
    ClearOTagR(pCurrentFieldRenderingContext->m80D4_uiOT, 8);
}

void clearFieldOrderingTable(void) {
#ifdef PLATFORM_PC
    pcFieldWideFrameStart(); /* src/field/f_800A55B8.c */
#endif
    setupFieldRenderingContext();
    ClearOTagR(pCurrentFieldRenderingContext->mCC_OT, 0x1000);
    if (fieldUseGroundOT != 0) {
        ClearOTagR(pCurrentFieldRenderingContext->m40D0_secondaryOT, 0x1000);
    }
}

void copyMatrix(MATRIX* dst, MATRIX* src) {
    copyMatrixRotation(dst, src);
    copyMatrixTranslation(dst, src);
}

/* copy the translation */
void copyMatrixTranslation(MATRIX* dst, MATRIX* src) {
    dst->t[0] = src->t[0];
    dst->t[1] = src->t[1];
    dst->t[2] = src->t[2];
}

/* copy the rotation */
void copyMatrixRotation(MATRIX* dst, MATRIX* src) {
    dst->m[0][0] = src->m[0][0];
    dst->m[0][1] = src->m[0][1];
    dst->m[0][2] = src->m[0][2];
    dst->m[1][0] = src->m[1][0];
    dst->m[1][1] = src->m[1][1];
    dst->m[1][2] = src->m[1][2];
    dst->m[2][0] = src->m[2][0];
    dst->m[2][1] = src->m[2][1];
    dst->m[2][2] = src->m[2][2];
}

void renderCompass(void) {
    MATRIX m232;
    MATRIX m200;
    MATRIX m168;
    MATRIX m136;
    MATRIX m104;
    SVECTOR rot;
    VECTOR eye;
    VECTOR at;
    s32 i;
    s32 j;
    u8 stop;

    stop = fieldSceneState.m175_dollyStop;
    for (i = 0; i < 8; i++) {
        if (stop & compassData0[i]) {
            for (j = 0; j < 16; j++) {
                compassData1[0][i * 16 + j] = 0;
            }
        } else {
            for (j = 0; j < 16; j++) {
                compassData1[0][i * 16 + j] = compassData2[j];
            }
        }
    }
    compassDataRect.w = 0x80;
    LoadImage(&compassDataRect, compassData1);
    SetGeomScreen(0x80);
    SetGeomOffset(0x10A, 0xA6);
    at.vx = 0;
    at.vy = 0;
    at.vz = 0;
    eye.vx = 0;
    eye.vy = fieldSceneState.m0_cameraEye.vy - fieldSceneState.m10_cameraAt.vy;
    eye.vz = -length2d((fieldSceneState.m0_cameraEye.vx - fieldSceneState.m10_cameraAt.vx) >> 16,
                      (fieldSceneState.m0_cameraEye.vz - fieldSceneState.m10_cameraAt.vz) >> 16) << 16;
    lookAtDivided(&m200, &eye, &at, &fieldSceneState.m20_cameraUp);
    setIdentityMatrix(&m232);
    m232.t[2] = 0x80;
    SetRotMatrix(&m232);
    SetTransMatrix(&m232);
    compassArrowTargetDirection = fieldSceneState.m290_actorArray[op9DVar1.m1BA_actorCameraTracked].m4C_scriptEntity->m106_currentRotation + (s16)(fieldSceneState.m10E_cameraTan + 0x400);
    compassArrowCurrentDirection = stepInterpolateDirection(compassArrowCurrentDirection, compassArrowTargetDirection, 0x40);
    rot.vx = 0;
    rot.vy = compassArrowCurrentDirection;
    rot.vz = 0;
    resetMatrixTranslation(&m168);
    createRotationMatrix(&rot, &m168);
    MulMatrix2(&m200, &m168);
    m168.t[2] = 0x1000;
    CompMatrix(&m232, &m168, &m136);
    if (op9DVar1.m4D_compassDisabled == 0 && g_updateAllEntitiesSub2Var0 == 0 && fieldCompassVar == 0) {
        for (i = 0x14; i < 0x15; i++) {
            drawCompassArrowSegment(pCurrentFieldRenderingContext->m80D4_uiOT, &fieldCompassVar2[i], &m136, g_frameOddOrEven);
        }
    }
    setIdentityMatrix(&m168);
    MulMatrix2(&m200, &m168);
    m168.t[2] = 0x1000;
    CompMatrix(&m232, &m168, &m136);
    MulMatrix0(&m232, &m168, &fieldSceneState.m204_worldScaleMatrix);
    SetRotMatrix(&m232);
    SetTransMatrix(&m232);
    setIdentityMatrix(&m168);
    MulMatrix2(&fieldSceneState.m110_cameraMatrix, &m168);
    m168.t[2] = 0x1000;
    CompMatrix(&m232, &m168, &m136);
    copyMatrix(&m232, &m136);
    rot.vx = 0x400;
    rot.vy = 0;
    rot.vz = 0;
    createRotationMatrix(&rot, &m104);
    if (op9DVar1.m4D_compassDisabled == 0 && g_updateAllEntitiesSub2Var0 == 0 && fieldCompassVar == 0) {
        for (i = 0x10; i < 0x14; i++) {
            setIdentityMatrix(&m168);
            m168.t[0] = *compassCardinalPointsLocation[i - 0x10];
            m168.t[2] = *(compassCardinalPointsLocation[i - 0x10] + 1);
            CompMatrix(&m232, &m168, &m136);
            copyMatrixRotation(&m136, &m104);
            drawCompassLetters(pCurrentFieldRenderingContext->m80D4_uiOT, &fieldCompassVar2[i], &m136, g_frameOddOrEven);
        }
        for (i = 0; i < 0x10; i++) {
            drawCompassArrowSegment(pCurrentFieldRenderingContext->m80D4_uiOT, &fieldCompassVar2[i], &m232, g_frameOddOrEven);
        }
        for (i = 0x15; i < 0x19; i++) {
            drawCompassArrowSegment(pCurrentFieldRenderingContext->m80D4_uiOT, &fieldCompassVar2[i], &m232, g_frameOddOrEven);
        }
    }
#ifdef PLATFORM_PC
    /* The wide picture keeps the compass at the same distance from its right
     * edge (include/pc_wide.h). Its polygons are HUD for the display copy's
     * texture classes: filters, and the plain texture keys of packs. */
    for (i = 0; i < 0x19; i++) {
        pcWideAnchorRight(&fieldCompassVar2[i].m20_poly[g_frameOddOrEven]);
        pcTextureClass(&fieldCompassVar2[i].m20_poly[g_frameOddOrEven], PC_TEX_HUD);
    }
#endif
    addPrim(pCurrentFieldRenderingContext->m80D4_uiOT, &gDialogDrModes[g_frameOddOrEven][1]);
    SetGeomOffset(0xA0, 0x70);
    SetGeomScreen(fieldSceneState.m178_sceneSCRZ);
#ifdef PLATFORM_PC
    /* On the PS1 m232.t[1..2] and the first four halves of m200 are the stack
     * words under the next frame's wall-slide edge buffer (f_8008083C.c
     * pcSlideEdgeStack). */
    pcSlideEdgeStack[0] = m232.t[1];
    pcSlideEdgeStack[1] = m232.t[2];
    pcSlideEdgeStack[2] = (u16)m200.m[0][0] | ((u32)(u16)m200.m[0][1] << 16);
    pcSlideEdgeStack[3] = (u16)m200.m[0][2] | ((u32)(u16)m200.m[1][0] << 16);
#endif
}

s32 shoudlGroundOTBeEnabled(void) {
    s32 i;
    sFieldEntity* e;

    for (i = 0; i < totalObjects; i++) {
        e = &fieldSceneState.m290_actorArray[i];
        if (!(e->m58_flags & 0x40)) {
            if (e->m58_flags & 0x8000) {
                return 1;
            }
        }
    }
    return 0;
}

void updateFieldInputs(void) {
    padButtonForScripts = 0;
    padButtonForScriptsPad1 = 0;
    padButtonForDialogs = 0;
    padButtonForDialogs2 = 0;
    g_padButtonForField = 0;
    padButtonForField2 = 0;
    while (loadInputFromVSyncBuffer()) {
        padButtonForScripts |= newPadButtonForScripts & inputAllowedMask2;
        padButtonForScriptsPad1 |= newPadButtonForScripts2;
        padButtonForDialogs |= newPadButtonForDialogs & inputAllowedMask2;
        padButtonForDialogs2 |= newPadButtonForDialogs2;
        g_padButtonForField |= newPadButtonForField & inputAllowedMask2;
        padButtonForField2 |= newPadButtonForField2;
    }
    padButtonForScripts &= inputAllowedMask;
    padButtonForDialogs &= inputAllowedMask;
    g_padButtonForField &= inputAllowedMask;
    resetInputs();
    getMouseState(1, fieldMouseState);
    if (g_updateAllEntitiesSub2Var0 != 0) {
        padButtonForScripts = 0;
        padButtonForScriptsPad1 = 0;
        padButtonForDialogs = 0;
        padButtonForDialogs2 = 0;
        g_padButtonForField = 0;
        padButtonForField2 = 0;
    }
    if (playMusicAuthorized == 0) {
        padButtonForDialogs &= ~0x80;
    }
}

void renderObjects(void) {
    SVECTOR rot;
    VECTOR scale2;
    VECTOR scale1;
    MATRIX world;
    MATRIX proj;
    MATRIX rotm;
    s32 i;
    sFieldEntity* e;
    sFieldEntitySub0* sub;
    s32 force;
    u16 v;

    fieldPolyCount = 0;
    fieldPolyCount2 = 0;
    if (op9DVar1.mA_isFogSetup != 0) {
        setFogNearColor(op9DVar1.mC_fogNearColor[0], op9DVar1.mC_fogNearColor[1], op9DVar1.mC_fogNearColor[2]);
        SetFarColor(op9DVar1.m10_fogFarColor[0], op9DVar1.m10_fogFarColor[1], op9DVar1.m10_fogFarColor[2]);
        SetFogNearFar(op9DVar1.m14_fogNear, op9DVar1.m16_fogFar, fieldSceneState.m178_sceneSCRZ);
    }
    scale1.vx = 0x800;
    scale1.vy = 0x800;
    scale1.vz = 0x800;
    scale2.vx = fieldSceneState.m244_worldScaleFactor;
    scale2.vy = fieldSceneState.m244_worldScaleFactor;
    scale2.vz = fieldSceneState.m244_worldScaleFactor;
    ScaleMatrix(&fieldSceneState.m204_worldScaleMatrix, &scale2);
    CompMatrix(&fieldSceneState.m1E4_projectionMatrix, &renderModelRotationMatrix, &world);
    objectClippingMask = 0;
    op9DVar1.m38_objectOffsetX += op9DVar1.m2A_objectStepX;
    op9DVar1.m3C_objectOffsetY += op9DVar1.m2E_objectStepY;
    op9DVar1.m40_objectOffsetZ += op9DVar1.m2C_objectStepZ;
    for (i = 0; i < totalObjects; i++) {
        fieldSceneState.m290_actorArray[i].m2C_matrixBackup = fieldSceneState.m290_actorArray[i].mC_matrix;
        force = 0;
        if (fieldSceneState.m290_actorArray[i].m58_flags & 0x40) {
            continue;
        }
        e = &fieldSceneState.m290_actorArray[i];
        if (i < g_totalActors) {
            switch (e->m4C_scriptEntity->m12C_x0) {
            case 1:
                rot.vx = e->m4C_scriptEntity->m70_rotationForRendering;
                rot.vy = 0;
                rot.vz = 0;
                goto rotate;
            case 2:
                rot.vx = 0;
                rot.vy = e->m4C_scriptEntity->m70_rotationForRendering;
                rot.vz = 0;
                goto rotate;
            case 3:
                rot.vx = 0;
                rot.vy = 0;
                rot.vz = e->m4C_scriptEntity->m70_rotationForRendering;
            rotate:
                createRotationMatrix(&rot, &rotm);
                MulMatrix2(&fieldSceneState.m290_actorArray[i].mC_matrix, &rotm);
                rotm.t[0] = fieldSceneState.m290_actorArray[i].mC_matrix.t[0];
                rotm.t[1] = fieldSceneState.m290_actorArray[i].mC_matrix.t[1];
                rotm.t[2] = fieldSceneState.m290_actorArray[i].mC_matrix.t[2];
                CompMatrix(&fieldSceneState.m1E4_projectionMatrix, &rotm, &proj);
                break;
            default:
                v = e->m4C_scriptEntity->m128_attachedMechaBone;
                if (v != 0xFFFF) {
                    getMechaBoneMatrix(&fieldSceneState.m290_actorArray[i].m2C_matrixBackup, &fieldSceneState.m290_actorArray[i].m2C_matrixBackup,
                                  v >> 12, v & 0xFFF);
                    CompMatrix(&fieldSceneState.m1E4_projectionMatrix, &fieldSceneState.m290_actorArray[i].m2C_matrixBackup, &rotm);
                    CompMatrix(&rotm, &fieldSceneState.m290_actorArray[i].mC_matrix, &proj);
                    CompMatrix(&fieldSceneState.m290_actorArray[i].m2C_matrixBackup, &fieldSceneState.m290_actorArray[i].mC_matrix,
                               &fieldSceneState.m290_actorArray[i].m2C_matrixBackup);
                } else {
                    if (e->m4C_scriptEntity->m75_parentActorId == 0xFF) {
                        goto moved;
                    }
                    CompMatrix(&fieldSceneState.m1E4_projectionMatrix,
                               &fieldSceneState.m290_actorArray[fieldSceneState.m290_actorArray[i].m4C_scriptEntity->m75_parentActorId].m2C_matrixBackup, &rotm);
                    CompMatrix(&rotm, &fieldSceneState.m290_actorArray[i].mC_matrix, &proj);
                    CompMatrix(&fieldSceneState.m290_actorArray[fieldSceneState.m290_actorArray[i].m4C_scriptEntity->m75_parentActorId].m2C_matrixBackup,
                               &fieldSceneState.m290_actorArray[i].mC_matrix, &fieldSceneState.m290_actorArray[i].m2C_matrixBackup);
                }
                break;
            }
        } else {
            if ((op9DVar1.m4E_objectMoveFlags & 0x7F) == 0) {
                e->mC_matrix.t[0] += op9DVar1.m2A_objectStepX;
                e->mC_matrix.t[1] += op9DVar1.m2E_objectStepY;
                e->mC_matrix.t[2] += op9DVar1.m2C_objectStepZ;
            }
        moved:
            if ((op9DVar1.m4E_objectMoveFlags & 0x7F) == 1) {
                e->mC_matrix.t[0] += op9DVar1.m2A_objectStepX;
                e->mC_matrix.t[1] += op9DVar1.m2E_objectStepY;
                e->mC_matrix.t[2] += op9DVar1.m2C_objectStepZ;
            }
            if (op9DVar1.m4E_objectMoveFlags & 0x80) {
                force = 1;
            }
            gte_CompMatrix(&world, &e->mC_matrix, &proj);
            if (fieldSceneState.m290_actorArray[i].m58_flags & 3) {
                if ((fieldSceneState.m290_actorArray[i].m58_flags & 3) == 1) {
                    MulMatrix0(&fieldSceneState.m204_worldScaleMatrix, &fieldSceneState.m290_actorArray[i].mC_matrix, &proj);
                } else {
                    copyMatrixRotation(&proj, &fieldSceneState.m290_actorArray[i].mC_matrix);
                    ScaleMatrix(&proj, &scale2);
                }
                MulMatrix2(&fieldSceneState.m130_projectionTempMatrix, &proj);
            }
        }
        sub = e->m0_model;
        if (sub->m12_renderMode == 1) {
            rotm = proj;
            ScaleMatrix(&rotm, &scale1);
            setupLightMatrix(&rotm);
            setAmbientColorFixed(fieldSceneState.m284_ambient[0], fieldSceneState.m284_ambient[1], fieldSceneState.m284_ambient[2]);
        }
        objectClippingMask = 0;
        if (fieldSceneState.m290_actorArray[i].m58_flags & 0x20) {
            continue;
        }
        if ((e->m58_flags & 0x2000) && sub->m14_dynamicVertices != 0) {
            fieldModelDynamicVerticesUpdate_actorIndex = i;
            fieldModelDynamicVerticesUpdate_nodeIndex = 0;
            updateDynamicVertices(sub->m14_dynamicVertices);
        }
        gte_SetRotMatrix(&proj);
        gte_SetTransMatrix(&proj);
        if (isObjectOcluded(sub) == 0 || force == 1) {
#ifdef PLATFORM_PC
            pcInterpolationObject(e, sub->m4_pModelBlock);
#endif
            gte_SetRotMatrix(&proj);
            gte_SetTransMatrix(&proj);
            if (!(e->m58_flags & 0x8000)) {
                submitModelForRendering(sub->m4_pModelBlock, sub->m8_primBuffers[g_frameOddOrEven], pCurrentFieldRenderingContext->mCC_OT, sub->m12_renderMode);
            } else {
                submitModelForRendering(sub->m4_pModelBlock, sub->m8_primBuffers[g_frameOddOrEven], pCurrentFieldRenderingContext->m40D0_secondaryOT, sub->m12_renderMode);
            }
#ifdef PLATFORM_PC
            pcInterpolationObject(0, 0);
#endif
        }
#ifdef PLATFORM_PC
        else if (pcWideCull != 0 || pcVerticalCull != 0) {
            /* Off the 4:3 screen: drawn into the wide picture only, if it
             * shows in its margins (port/src/game/field/wide.c). */
            pcInterpolationObject(e, sub->m4_pModelBlock);
            pcFieldWideObject(sub, &proj,
                              !(e->m58_flags & 0x8000) ? pCurrentFieldRenderingContext->mCC_OT
                                                       : pCurrentFieldRenderingContext->m40D0_secondaryOT);
            pcInterpolationObject(0, 0);
        }
#endif
    }
    if (fieldDebugDisable == 0) {
        func_80281B00("MODEL     ");
    }
}

void renderMechasInField(void) {
    if (disableMechaRendering == 0) {
        if (op9DVar1.mE0_numMechas != 0) {
            updateMechasPosForField();
            SetBackColor(op9DVar1.mD8_mechaBackColor[0], op9DVar1.mD8_mechaBackColor[1], op9DVar1.mD8_mechaBackColor[2]);
            renderMechas(&fieldSceneState.m1E4_projectionMatrix, op9DVar1.m98_mechaFieldArgs2, pCurrentFieldRenderingContext->mCC_OT, g_frameOddOrEven, 1);
        }
        if (fieldDebugDisable == 0) {
            func_80281B00("GEAR      ");
        }
    }
}

void renderChars(void) {
    s32 i;

    if (renderCharsDisabled != 1) {
        clearShapeTransfertTableEntry(g_frameOddOrEven);
        setCharacterRenderingOT(pCurrentFieldRenderingContext->mCC_OT);
        setCurrentRenderingMatrix(&fieldSceneState.m1E4_projectionMatrix);
        uploadCharacterSprites();
        execSpritesCallbacksList1();
        execSpritesCallbacksList2();
        renderFieldCharacterSprites(pCurrentFieldRenderingContext->mCC_OT, g_frameOddOrEven);
        for (i = 0; i < g_totalActors; i++) {
            if (!(fieldSceneState.m290_actorArray[i].m58_flags & 0x20) && (fieldSceneState.m290_actorArray[i].m58_flags & 0x40)) {
                if ((fieldSceneState.m290_actorArray[i].m4C_scriptEntity->m4_flags & 0x600) != 0x200 && !(fieldSceneState.m290_actorArray[i].m4C_scriptEntity->m4_flags & 0x1000) &&
                    !(fieldSceneState.m290_actorArray[i].m4C_scriptEntity->m0_fieldScriptFlags & 1)) {
                    OP_INIT_ENTITY_SCRIPT_sub0Sub9(fieldSceneState.m290_actorArray[i].m4_pVramSpriteSheet);
                }
            } else if (fieldSceneState.m290_actorArray[i].m4C_scriptEntity->m4_flags & 0x1000000) {
                OP_INIT_ENTITY_SCRIPT_sub0Sub9(fieldSceneState.m290_actorArray[i].m4_pVramSpriteSheet);
            }
        }
        renderCharShadows(pCurrentFieldRenderingContext->mCC_OT, g_frameOddOrEven);
        if (fieldDebugDisable == 0) {
            func_80281B00("CHAR      ");
        }
    }
}

void linkOT(OTag* dst, OTag* ot, s32 index) {
    AddPrims(dst, ot + index, ot);
}

void renderBackgroundPolyIfEnabled(void) {
    SVECTOR eye;
    SVECTOR at;

    if (OPX_80Params.m32_OPX_82Param4 != 0 && D_800ADB50 == 0) {
        eye.vx = (s16)(fieldSceneState.m0_cameraEye.vx >> 16);
        eye.vy = (s16)(fieldSceneState.m0_cameraEye.vy >> 16);
        eye.vz = (s16)(fieldSceneState.m0_cameraEye.vz >> 16);
        at.vx = (s16)(fieldSceneState.m10_cameraAt.vx >> 16);
        at.vy = (s16)(fieldSceneState.m10_cameraAt.vy >> 16);
        at.vz = (s16)(fieldSceneState.m10_cameraAt.vz >> 16);
#ifdef PLATFORM_PC
        /* A background as big as the screen (the title's picture, the map
         * screens) is a picture, not a panorama: the wide picture does not
         * repeat it into its margins (src/main/background_render.c lays the
         * margin strips while pcWideView is set). */
        if (OPX_80Params.m0_OPX_80Params[2] <= 0x140 && OPX_80Params.m0_OPX_80Params[3] >= 0xE0 &&
            !pcWideCapturedScene(OPX_80Params.m0_OPX_80Params[0], OPX_80Params.m0_OPX_80Params[1],
                                 OPX_80Params.m0_OPX_80Params[2], OPX_80Params.m0_OPX_80Params[3])) {
            s32 view = pcWideView;

            pcWideView = 0;
            renderBackgroundPoly(backgroundPoly, &eye, &at, &fieldSceneState.m1E4_projectionMatrix,
                                 pCurrentFieldRenderingContext->m40D0_secondaryOT + op9DVar1.m50_linkOTIndex - 1,
                                 g_frameOddOrEven);
            pcWideView = view;
            return;
        }
#endif
        renderBackgroundPoly(backgroundPoly, &eye, &at, &fieldSceneState.m1E4_projectionMatrix,
                             pCurrentFieldRenderingContext->m40D0_secondaryOT + op9DVar1.m50_linkOTIndex - 1, g_frameOddOrEven);
    }
}

void updateAndRenderField(void) {
    RECT rect;
    s32 start;
    s32 now;
    s32 t;

#ifdef PLATFORM_PC
    portFieldSweepFrame(); /* XG_SWEEP_LOG (port/src/game/field/sweep.c) */
    pcInterpolationFrameBegin(fieldMapNumber & 0xFFF, pCurrentFieldRenderingContext->m0_drawEnv.clip.x,
                              pCurrentFieldRenderingContext->m0_drawEnv.clip.y,
                              pCurrentFieldRenderingContext->m0_drawEnv.clip.w,
                              pCurrentFieldRenderingContext->m0_drawEnv.clip.h);
#endif
    fieldProfileTimestamp = VSync(1);
    start = VSync(-1);
#ifdef PLATFORM_PC
    gtePrecisionBeginDraw();
#endif
    updateAllEntities();
    updateCamera();
    if (fieldDebugDisable == 0) {
        func_80281B00("SEFFECT   ");
    }
    updateScreenEffectSlots(pCurrentFieldRenderingContext->m80D4_uiOT, g_frameOddOrEven);
    if (fieldDebugDisable == 0) {
        func_80281B00("MESSAGE   ");
    }
    renderCompass();
    STACK_SWITCH(PSX_SCRATCH(0x1F8003FC));
    renderObjects();
    renderChars();
    updateParticles();
    if (fieldDebugDisable == 0) {
        func_80281450();
    }
    updateAndRenderScreenDistortion();
    STACK_RESTORE();
    renderDebugCameraDigits();
    renderBackgroundPolyIfEnabled();
    renderMechasInField();
    drawWideImageSprites();
    if (fieldDebugDisable == 0) {
        func_80281400();
        func_80281B00("FntPrint  ");
    }
    fieldCpuTime = VSync(1);
    DrawSync(0);
    stepDialogWindows();
    addDialogWindowsToOT(pCurrentFieldRenderingContext->m80D4_uiOT, g_frameOddOrEven);
#ifdef PLATFORM_PC
    pcFieldVSync();
#else
    VSync(0);
#endif
    updateDelayedFrees();
#ifdef PLATFORM_PC
    /* Present the completed front buffer before clearing the back buffer.
     * Enhanced GPU work can carry the next library safe point past a
     * vblank; clearing first would expose the empty buffer being reused. */
    PutDispEnv(&pCurrentFieldRenderingContext->mB8_displayEnv);
#endif
    if (g_updateAllEntitiesSub2Var0 == 0) {
        ClearImage(&pCurrentFieldRenderingContext->m5C_backgroundRect.clip, op9DVar1.m18_backgroundClearColor[0], op9DVar1.m18_backgroundClearColor[1], op9DVar1.m18_backgroundClearColor[2]);
    } else if (fieldTransitionMode == 3) {
        rect.x = 0x2C0;
        rect.y = 0x100;
        rect.w = 0x140;
        rect.h = 0xE0;
        MoveImage(&rect, 0, g_frameOddOrEven << 8);
    } else {
        ClearImage(&pCurrentFieldRenderingContext->m5C_backgroundRect.clip, 0, 0, 0);
    }
#ifndef PLATFORM_PC
    PutDispEnv(&pCurrentFieldRenderingContext->mB8_displayEnv);
#endif
    PutDrawEnv(&pCurrentFieldRenderingContext->m0_drawEnv);
    if (fieldDebugDisable == 0) {
        fieldProfileTimestamp = VSync(1);
    }
    shapeTransfert();
    if (fieldDebugDisable == 0) {
        func_80281B00("ShapeTrans");
    }
    executeFieldMoveImageCommands();
    if (paletteFadeUploadPending != 0) {
        LoadImage(&paletteFadeRect, paletteFadeBuffer);
        paletteFadeUploadPending = 0;
    }
    if (fieldDebugDisable == 0) {
        func_80281B00("LineScroll");
    }
    if (g_updateAllEntitiesSub2Var0 == 0) {
        if (fieldUseGroundOT != 0) {
            linkOT(&pCurrentFieldRenderingContext->mCC_OT[op9DVar1.m50_linkOTIndex], pCurrentFieldRenderingContext->m40D0_secondaryOT, op9DVar1.m50_linkOTIndex);
        }
        linkOT(&pCurrentFieldRenderingContext->m80D4_uiOT[7], pCurrentFieldRenderingContext->mCC_OT, op9DVar1.m50_linkOTIndex);
    }
    DrawOTag(&pCurrentFieldRenderingContext->m80D4_uiOT[7]);
#ifdef PLATFORM_PC
    pcInterpolationFrameEnd();
    gtePrecisionEndDraw();
#endif
    do {
        now = VSync(-1);
        t = dialogWindowFlag2 + 2;
    } while (now < start + t);
}

void updateFieldDialogsOverMovie(void) {
    RECT rect;

    updateFieldInputs();
    exectueEntitiesUpdateFunction();
    stepDialogWindows();
    addDialogWindowsToOT(pCurrentFieldRenderingContext->m80D4_uiOT, g_frameOddOrEven);
    DrawSync(0);
    VSync(0);
    rect.x = 0x140;
    rect.w = 0x140;
    rect.y = ((fieldMovieDisplayBuffer + 1) & 1) << 8;
    rect.h = 0xE0;
    MoveImage(&rect, 0, g_frameOddOrEven << 8);
    PutDispEnv(&pCurrentFieldRenderingContext->mB8_displayEnv);
    PutDrawEnv(&pCurrentFieldRenderingContext->m0_drawEnv);
    DrawOTag(&pCurrentFieldRenderingContext->m80D4_uiOT[7]);
}

/* Build a rotation matrix whose second row is the given (unit) up vector. */
void computeMatrixFromUpVector(MATRIX* m, VECTOR* up) {
    VECTOR z = { 0, 0, 0x1000, 0 };
    VECTOR x;
    VECTOR t;

    OuterProduct12(&z, up, &t);
    VectorNormal(&t, &x);
    OuterProduct12(&x, up, &t);
    VectorNormal(&t, &z);
    m->m[0][0] = x.vx;
    m->m[0][1] = x.vy;
    m->m[0][2] = x.vz;
    m->m[1][0] = up->vx;
    m->m[1][1] = up->vy;
    m->m[1][2] = up->vz;
    m->m[2][0] = z.vx;
    m->m[2][1] = z.vy;
    m->m[2][2] = z.vz;
}

void setSpriteColorIfNoFog(SpriteActorCore* actor, u8* rgb) {
    if (isFogSetup == 0) {
        spriteActorSetColor(actor, rgb[0], rgb[1], rgb[2]);
    }
}

void renderFieldCharacterSprites(OTag* ot, s32 oddOrEven) {
    SVECTOR v;
    SVECTOR pos;
    VECTOR scale;
    MATRIX m;
    MATRIX unused; /* reserves 0x20 bytes of the frame */
    MATRIX rot;
    u8 color[4];
    s32 sxy;
    s32 p;
    s32 flag;
    s32 depth;
    s32 dip;
    s32 dir;
    s32 i;
    s32 j;
    sFieldScriptEntity* se;
    SpriteActorCore* sprite;
    s32 d;
    u16 flags;
    u32 f4;
    s32 x;
    s32 y;

    dip = fieldSceneState.m17C_sceneDIP;
    dir = getCameraDirection() & 0xFFFF;
    copyMatrixRotation(&rot, &fieldSceneState.m130_projectionTempMatrix);
    pos.vx = 0;
    pos.vy = (dip / 3) * -2;
    pos.vz = 0;
    j = 0;
    for (i = 0; i < g_totalActors; i++) {
        flags = fieldSceneState.m290_actorArray[i].m58_flags;
        if (flags & 0x40) {
            se = fieldSceneState.m290_actorArray[i].m4C_scriptEntity;
            sprite = fieldSceneState.m290_actorArray[i].m4_pVramSpriteSheet;
            f4 = se->m4_flags;
            fieldSceneState.m290_actorArray[i].m2C_matrixBackup = fieldSceneState.m290_actorArray[i].mC_matrix;
            if (!(f4 & 0x2000)) {
                gte_CompMatrix(&fieldSceneState.m1E4_projectionMatrix, &fieldSceneState.m290_actorArray[i].mC_matrix, &m);
                gte_SetRotMatrix(&m);
                gte_SetTransMatrix(&m);
                gte_RotTransPers(&pos, &sxy, &p, &flag, &depth);
                y = sxy >> 16;
                x = (s16)sxy;
#ifdef PLATFORM_PC
                /* With a wide picture, NPCs in its margins are on screen too
                 * (the user's choice: their animation, scripts and dialogs
                 * run there; XG_WIDE_NPC=ps1 keeps the PS1's test). */
                if ((u32)(y + 9) < 0x143 && (u32)(x + 0x27 + pcFieldNpcMargin()) < 0x18F + 2 * (u32)pcFieldNpcMargin()) {
#else
                if ((u32)(y + 9) < 0x143 && (u32)(x + 0x27) < 0x18F) {
#endif
                    se->m4_flags &= ~0x200;
                } else {
                    se->m4_flags |= 0x200;
                }
                if (disableCharacterShadowsRendering != 0 || (flags & 0x20) || flag < 0) {
                    continue;
                }
                scale.vx = se->mF4_scale3d[0] * 3 >> 2;
                scale.vy = se->mF4_scale3d[1] * 3 >> 2;
                scale.vz = se->mF4_scale3d[2] * 3 >> 2;
                if (se->mE4_playableCharacterId == 7 && gearSwitchingEnabled != 0) {
                    scale.vx = scale.vx * 5 >> 2;
                    scale.vy = scale.vy * 5 >> 2;
                    scale.vz = scale.vz * 5 >> 2;
                }
                ((SpriteObject*)sprite->m20_object)->mC_matrix = rot;
                ScaleMatrix(&((SpriteObject*)sprite->m20_object)->mC_matrix, &scale);
                if (fieldSceneState.m290_actorArray[i].m4C_scriptEntity->m14_currentTriangleFlag & 0x200000) {
                    switch ((dir - ((fieldSceneState.m290_actorArray[i].m4C_scriptEntity->m14_currentTriangleFlag >> 11) - 2 & 7)) & 7) {
                    case 1:
                    case 2:
                    case 3:
                        v.vx = 0;
                        v.vy = -0x80;
                        v.vz = 0;
                        depth = RotTransPers(&v, &sxy, &p, &flag);
                        break;
                    case 5:
                    case 6:
                    case 7:
                        v.vx = 0;
                        v.vy = 0x80;
                        v.vz = 0;
                        depth = RotTransPers(&v, &sxy, &p, &flag);
                        break;
                    }
                }
                if (op9DVar1.m1D3_spriteFogDisabled == 0 && op9DVar1.mA_isFogSetup != 0) {
                    gte_ldrgb(&fogNearColor);
                    gte_dpcs();
                    gte_strgb(color);
                    spriteActorSetColor(fieldSceneState.m290_actorArray[i].m4_pVramSpriteSheet, color[0], color[1], color[2]);
                }
                depth >>= gDepthDivider;
                if (depth >= 2) {
                    depth -= 2;
                }
                if ((u16)(se->mE8_currentAnimationId + 0x22) < 2) {
                    if (se->m4_flags & 0x2000000) {
                        continue;
                    }
                    spriteActorSetColor(sprite, se->mFC_spriteColor[0], se->mFC_spriteColor[1], se->mFC_spriteColor[2]);
                    sprite->m3C_x8 = 0xEF;
                    renderSpriteActor(sprite, ot + depth - 16);
                    v.vx = 0;
                    v.vy = 0x12C;
                    v.vz = 0;
                    d = RotTransPers(&v, &sxy, &p, &flag) >> gDepthDivider;
                    spriteActorSetColor(sprite, se->mFF_spriteColor2[0], se->mFF_spriteColor2[1], se->mFF_spriteColor2[2]);
                    sprite->m3C_x8 = 0xF7;
                    renderSpriteActor(sprite, &ot[d]);
                } else {
                    sprite->m3C_x8 = 0;
                    if (se->m4_flags & 0x2000000) {
                        continue;
                    }
                    if (se->m134_x5 == 0) {
                        setSpriteColorIfNoFog(sprite, se->mFC_spriteColor);
                        renderSpriteActor(sprite, &ot[depth]);
                        continue;
                    }
                    if (se->m134_x5 & 1) {
                        setSpriteColorIfNoFog(sprite, se->mFC_spriteColor);
                        v.vx = 0;
                        v.vy = (se->mEE_spriteSplitHeight - dip / 3) * 2;
                        v.vz = 0;
                        d = RotTransPers(&v, &sxy, &p, &flag) >> gDepthDivider;
                        if (d >= 2) {
                            d -= 2;
                        }
                        renderSpriteActorUpperPart(sprite, &ot[d], se->mEE_spriteSplitHeight);
                    }
                    if (se->m134_x5 & 2) {
                        setSpriteColorIfNoFog(sprite, se->mFF_spriteColor2);
                        renderSpriteActorLowerPart(sprite, &ot[depth], se->mEE_spriteSplitHeight);
                    }
                }
            } else if (disableMechaRendering == 0) {
                if (!(se->m0_fieldScriptFlags & 0x10000) && !(se->m14_currentTriangleFlag & 0x200002) && !(se->m4_flags & 0x800)) {
                    loadedMechas[j]->m4A_flags &= ~1;
                } else {
                    loadedMechas[j]->m4A_flags |= 1;
                }
                if (!(flags & 0x20)) {
                    loadedMechas[j]->m34_visible = 1;
                } else {
                    loadedMechas[j]->m34_visible = 0;
                }
                if (!(se->m4_flags & 0x20000)) {
                    loadedMechas[j]->m4_bones[0].m54_rotationAngles.vy = se->m108_rotation3 + 0xC00;
                } else {
                    se->m106_currentRotation = se->m108_rotation3 = loadedMechas[j]->m4_bones[0].m54_rotationAngles.vy - 0xC00;
                }
                loadedMechas[j]->m1C_scale = se->mF4_scale3d[0] * mechaList3[j] >> 12;
                loadedMechas[j]->m60_groundY = se->m20_position.vy >> 16;
                loadedMechas[j]->m4_bones[0].m5C_translation[0] = se->m20_position.vx >> 16;
                loadedMechas[j]->m4_bones[0].m5C_translation[2] = se->m20_position.vz >> 16;
                j++;
                se->m4_flags &= ~0x200;
            }
        }
    }
}

void renderCharShadows(OTag* ot, s32 oddOrEven) {
    VECTOR z;
    VECTOR d;
    VECTOR c;
    MATRIX m;
    MATRIX r;
    MATRIX proj;
    VECTOR scale;
    s32 p;
    s32 flag;
    s32 i;
    sFieldEntity* e;
    sFieldScriptEntity* se;
    s32 otz;
    s32 sx;
    s32 sy;
    s32 sz;

    proj = fieldSceneState.m1E4_projectionMatrix;
    if (disableCharacterShadowsRendering == 0) {
        for (i = 0; i < g_totalActors; i++) {
            e = &fieldSceneState.m290_actorArray[i];
            if ((*(u32*)&e->m58_flags & 0x60) == 0x40) {
                se = e->m4C_scriptEntity;
                if (!(se->m4_flags & 0x102200)) {
                if (!(se->m4_flags & 0x800) && !(se->m0_fieldScriptFlags & 0x10000) &&
                    !(se->m14_currentTriangleFlag & 0x200002)) {
                    z.vx = 0;
                    z.vy = 0;
                    z.vz = 0x1000;
                    gte_OuterProduct12(&z, &e->m4C_scriptEntity->m50_surfaceNormal, &c);
                    VectorNormal(&c, &d);
                    gte_OuterProduct12(&d, &e->m4C_scriptEntity->m50_surfaceNormal, &c);
                    VectorNormal(&c, &z);
                    m.m[0][0] = d.vx;
                    m.m[0][1] = d.vy;
                    m.m[0][2] = d.vz;
                    m.m[1][0] = e->m4C_scriptEntity->m50_surfaceNormal.vx;
                    m.m[1][1] = e->m4C_scriptEntity->m50_surfaceNormal.vy;
                    m.m[1][2] = e->m4C_scriptEntity->m50_surfaceNormal.vz;
                    m.m[2][0] = z.vx;
                    m.m[2][1] = z.vy;
                    m.m[2][2] = z.vz;
                    m.t[0] = e->mC_matrix.t[0];
                    m.t[1] = e->m4_pVramSpriteSheet->m84_maxY;
                    m.t[2] = e->mC_matrix.t[2];
                    gte_CompMatrix(&fieldSceneState.m1E4_projectionMatrix, &m, &r);
                    sx = e->m4C_scriptEntity->mF4_scale3d[0] * 0xC00;
                    scale.vx = sx >> 12;
                    sy = e->m4C_scriptEntity->mF4_scale3d[1] * 0xC00;
                    scale.vy = sy >> 12;
                    sz = e->m4C_scriptEntity->mF4_scale3d[2] * 0xC00;
                    scale.vz = sz >> 12;
                    if (gearSwitchingEnabled != 0 && (e->m4C_scriptEntity->m0_fieldScriptFlags & 0x400)) {
                        scale.vx = sx >> 14;
                        scale.vy = sy >> 14;
                        scale.vz = sz >> 14;
                    }
                    ScaleMatrix(&r, &scale);
                    gte_SetRotMatrix(&r);
                    gte_SetTransMatrix(&r);
                    otz = RotAverage4(&((FieldQuad*)e->m8_2dSprite)->m0_vertices[0], &((FieldQuad*)e->m8_2dSprite)->m0_vertices[1],
                                      &((FieldQuad*)e->m8_2dSprite)->m0_vertices[2], &((FieldQuad*)e->m8_2dSprite)->m0_vertices[3],
                                      &((FieldQuad*)e->m8_2dSprite)->m20_poly[oddOrEven].x0, &((FieldQuad*)e->m8_2dSprite)->m20_poly[oddOrEven].x1,
                                      &((FieldQuad*)e->m8_2dSprite)->m20_poly[oddOrEven].x2, &((FieldQuad*)e->m8_2dSprite)->m20_poly[oddOrEven].x3,
                                      &p, &flag);
                    otz >>= gDepthDivider;
#ifdef PLATFORM_PC
                    pcInterpolationKeyType(e, e->m8_2dSprite, 0x10000, &((FieldQuad*)e->m8_2dSprite)->m20_poly[oddOrEven], PC_INTERP_SHADOW);
#endif
                    addPrim(ot + otz, &((FieldQuad*)e->m8_2dSprite)->m20_poly[oddOrEven]);
                }
                }
            }
        }
    }
}

void fieldActorCallback(SpriteActorCore* actor) {
    fieldSceneState.m290_actorArray[actor->m7C_extra->m14_actorId].m4C_scriptEntity->m4_flags |= 0x10000;
}

void resetFieldScriptEntityGraphicEntity(s32 actorId, s32 vramSlot, void* setup, s32 clutBank, s32 clutX, s32 m126, s32 m134_4) {
    FP_VEC3 bv;
    SpriteActorCore* sprite;
    s32 vramX;
    s32 vramY;

    resetMemoryAllocStats(8, 0);
    fieldSceneState.m290_actorArray[actorId].m4C_scriptEntity->m127_vramSlot = vramSlot;
    fieldSceneState.m290_actorArray[actorId].m4C_scriptEntity->m126_spriteSource = m126;
    fieldSceneState.m290_actorArray[actorId].m4C_scriptEntity->m134_x0 = clutX;
    fieldSceneState.m290_actorArray[actorId].m4C_scriptEntity->m130_x1C = clutBank;
    fieldSceneState.m290_actorArray[actorId].m4C_scriptEntity->m134_x4 = m134_4;
    if (clutBank == 0) {
        vramY = fieldVramMapping.m[vramSlot].m2_vramY;
        vramX = fieldVramMapping.m[vramSlot].m0_vramX;
        if (clutX == 0) {
            if (fieldSceneState.m290_actorArray[actorId].m5A_spriteFlags & 1) {
                deleteFieldEntitySub4(fieldSceneState.m290_actorArray[actorId].m4_pVramSpriteSheet);
            }
            fieldSceneState.m290_actorArray[actorId].m4_pVramSpriteSheet = sprite = createSpriteActor(setup, 0x100, vramSlot + 0x1E0, vramX, vramY, 0x40);
        } else {
            if (fieldSceneState.m290_actorArray[actorId].m5A_spriteFlags & 1) {
                deleteFieldEntitySub4(fieldSceneState.m290_actorArray[actorId].m4_pVramSpriteSheet);
            }
            fieldSceneState.m290_actorArray[actorId].m4_pVramSpriteSheet = sprite = createSpriteActorEX(setup, clutX * 16 + 0x100, vramSlot + 0x1E0, vramX, vramY, 0x40, clutX);
        }
    } else {
        if (fieldSceneState.m290_actorArray[actorId].m5A_spriteFlags & 1) {
            deleteFieldEntitySub4(fieldSceneState.m290_actorArray[actorId].m4_pVramSpriteSheet);
        }
        if (clutBank == 1) {
            vramY = vramSlot * 0x40 + 0x100;
            sprite = createSpriteActor(setup, 0x100, vramSlot + 0xE0, 0x280, vramY, 8);
            OP_INIT_ENTITY_SCRIPT_sub0Sub3(fieldSceneState.m290_actorArray[actorId].m4_pVramSpriteSheet = sprite, 0x20);
        } else {
            vramY = vramSlot * 0x40 + 0x100;
            sprite = createSpriteActor(setup, 0x100, vramSlot + 0xE3, 0x2A0, vramY, 8);
            OP_INIT_ENTITY_SCRIPT_sub0Sub3(fieldSceneState.m290_actorArray[actorId].m4_pVramSpriteSheet = sprite, 0x20);
        }
    }
    fieldSceneState.m290_actorArray[actorId].m5A_spriteFlags |= 1;
    getBoundingVolumeFromSprite(sprite, 0, &bv.vx, &bv.vy, &bv.vz);
    OP_INIT_ENTITY_SCRIPT_sub0Sub5(sprite, 3);
    sprite->m2C_scale = 0xC00;
    sprite->m82_entityScale = 0x2000;
    if (fieldScriptEntityAlreadyInitialized == 0) {
        sprite->m0_position.vx = fieldSceneState.m290_actorArray[actorId].m4C_scriptEntity->m20_position.vx;
        sprite->m0_position.vy = fieldSceneState.m290_actorArray[actorId].m4C_scriptEntity->m20_position.vy;
        sprite->m0_position.vz = fieldSceneState.m290_actorArray[actorId].m4C_scriptEntity->m20_position.vz;
        sprite->m84_maxY = fieldSceneState.m290_actorArray[actorId].mC_matrix.t[1];
        sprite->mC_step.vy = 0;
        sprite->mC_step.vx = 0;
        sprite->mC_step.vy = 0;
        sprite->mC_step.vz = 0;
        sprite->m1C_gravity = 0x10000;
        if (clutBank == 0) {
            fieldSceneState.m290_actorArray[actorId].m4C_scriptEntity->m18_boundingVolume[1] = bv.vy * 2;
        } else {
            fieldSceneState.m290_actorArray[actorId].m4C_scriptEntity->m18_boundingVolume[1] = 0x40;
        }
    }
    if (op9DVar1.mA_isFogSetup != 0) {
        sprite->m40_draw.raw |= 0x40000;
    }
    spriteActorSetPlayingAnimation(sprite, 0);
    spriteActorSetDirection(sprite, 0);
    resetMemoryAllocStats(8, 0);
    sprite->m7C_extra->m14_actorId = actorId;
    spriteActorSetCallback(sprite, fieldActorCallback);
    if (m134_4 == 0) {
        OP_INIT_ENTITY_SCRIPT_sub0Sub9(sprite);
        execSpritesCallbacksList2();
        if (sprite->m7C_extra->mC_animationSignal == 0xFF) {
            fieldSceneState.m290_actorArray[actorId].m4C_scriptEntity->mEA_forcedAnimation = 0xFF;
            fieldSceneState.m290_actorArray[actorId].m4C_scriptEntity->m4_flags |= 0x1000000;
            sprite->m0_position.vx = fieldSceneState.m290_actorArray[actorId].m4C_scriptEntity->m20_position.vx;
            sprite->m0_position.vy = fieldSceneState.m290_actorArray[actorId].m4C_scriptEntity->m20_position.vy;
            sprite->m0_position.vz = fieldSceneState.m290_actorArray[actorId].m4C_scriptEntity->m20_position.vz;
        }
    }
    fieldSceneState.m290_actorArray[actorId].m2C_matrixBackup.t[0] = fieldSceneState.m290_actorArray[actorId].mC_matrix.t[0] =
        fieldSceneState.m290_actorArray[actorId].m4C_scriptEntity->m20_position.vx >> 16;
    fieldSceneState.m290_actorArray[actorId].m2C_matrixBackup.t[1] = fieldSceneState.m290_actorArray[actorId].mC_matrix.t[1] =
        fieldSceneState.m290_actorArray[actorId].m4C_scriptEntity->m20_position.vy >> 16;
    fieldSceneState.m290_actorArray[actorId].m2C_matrixBackup.t[2] = fieldSceneState.m290_actorArray[actorId].mC_matrix.t[2] =
        fieldSceneState.m290_actorArray[actorId].m4C_scriptEntity->m20_position.vz >> 16;
    sprite->m84_maxY = fieldSceneState.m290_actorArray[actorId].mC_matrix.t[1];
    sprite->m0_position.vx = fieldSceneState.m290_actorArray[actorId].m4C_scriptEntity->m20_position.vx;
    sprite->m0_position.vy = fieldSceneState.m290_actorArray[actorId].m4C_scriptEntity->m20_position.vy;
    sprite->m0_position.vz = fieldSceneState.m290_actorArray[actorId].m4C_scriptEntity->m20_position.vz;
    fieldScriptInitVar0++;
}

/* set the semi-transparency bit of every CLUT entry */
void setClutSemiTransBits(u32* p, s32 size) {
    s32 n = size / 2;

    while (--n != -1) {
        *p++ |= 0x80008000;
    }
}

void uploadFieldImages(u32* data) {
    TIM_IMAGE tim;

    OpenTIM(data);
    while (ReadTIM(&tim)) {
        if (tim.caddr) {
            LoadImage(tim.crect, tim.caddr);
        }
        if (tim.paddr) {
            LoadImage(tim.prect, tim.paddr);
        }
    }
}

void doPCCollisionCheckAfterLoading(void) {
    s32 i;
    sFieldEntity* e;
    sFieldScriptEntity* se;
    s32 slot;
    SpriteActorCore* sprite;

#ifdef PLATFORM_PC
    pcSlideEdgePartyInit(PC_SLIDE_PARTY_FIRST, fieldSceneState.m28C_totalObjects);
#endif
    EntityMoveCheck1(op9DVar1.mE8_playerControlledActor, fieldSceneState.m290_actorArray[op9DVar1.mE8_playerControlledActor].m4C_scriptEntity->m20_position.vy >> 16, &fieldSceneState.m290_actorArray[op9DVar1.mE8_playerControlledActor],
                     fieldSceneState.m290_actorArray[op9DVar1.mE8_playerControlledActor].m4C_scriptEntity);
    for (i = 0; i < g_totalActors; i++) {
        e = &fieldSceneState.m290_actorArray[i];
        se = e->m4C_scriptEntity;
        if ((e->m58_flags & 0xF80) == 0x200) {
            slot = findCharacterInParty(se->mE4_playableCharacterId);
            if (slot != -1) {
                sprite = fieldSceneState.m290_actorArray[i].m4_pVramSpriteSheet;
                if (slot != 0) {
#ifdef PLATFORM_PC
                    pcSlideEdgePartyInit(i, pcHeapElement(fieldSceneState.m290_actorArray, e,
                                        fieldSceneState.m28C_totalObjects, sizeof(sFieldEntity), 0x5C));
                    pcSlideEdgePartySprite(sprite);
#endif
                    EntityMoveCheck1(i, fieldSceneState.m290_actorArray[i].m4C_scriptEntity->m20_position.vy >> 16, e, se);
                    sprite->m0_position.vx = fieldSceneState.m290_actorArray[op9DVar1.mE8_playerControlledActor].m4_pVramSpriteSheet->m0_position.vx;
                    sprite->m0_position.vy = fieldSceneState.m290_actorArray[op9DVar1.mE8_playerControlledActor].m4_pVramSpriteSheet->m0_position.vy;
                    sprite->m0_position.vz = fieldSceneState.m290_actorArray[op9DVar1.mE8_playerControlledActor].m4_pVramSpriteSheet->m0_position.vz;
                    e->mC_matrix.t[0] = fieldSceneState.m290_actorArray[op9DVar1.mE8_playerControlledActor].mC_matrix.t[0];
                    e->mC_matrix.t[1] = fieldSceneState.m290_actorArray[op9DVar1.mE8_playerControlledActor].mC_matrix.t[1];
                    e->mC_matrix.t[2] = fieldSceneState.m290_actorArray[op9DVar1.mE8_playerControlledActor].mC_matrix.t[2];
                }
            }
        }
    }
    partyToFollowStructMapping[0] = partyToFollowStructMapping[1] = partyToFollowStructMapping[2] = 0;
    for (i = 0; i < 0x20; i++) {
        initFollowStructForPlayer(op9DVar1.mE8_playerControlledActor);
    }
}

void initFontSystem(void) {
    if (fieldDebugDisable == 0) {
        setDebugFontBuffer(0x80270000);
        initDebugFont(0x10, 0x10, 0x130, 0xE0, 0x400, 4, 0x3C0, 0x100, 0x100, 0x1FF, 0);
    }
    initFontPalettes(0x100, 0xF0);
}
