/* Gear (mecha) overlay, 0x801E34BC-0x801E8590: the code after the cloth object
 * (fm_801E1A14.c), including the mecha loader mechaInitNewMecha, which calls the
 * cloth builder through a short-parameter prototype. .rodata from 0x801DC040.
 * GCC 2.6.3, -O2 -G0, ASPSX 2.34 with divide checks. */
#include "common.h"
#include "pc_retail_heap.h"
#include "psyq.h"
#include "gte.h"
#include "main/memory.h"
#include "main/sprite.h"
#include "main/sound.h"
#include "field_mecha/mecha.h"
#include "pc_convert.h"

/* ---- main executable ---- */
extern s32 gDepthDivider; /* Noah: gDepthDivider (OT z shift) */

void* getTaskUpdateFunction(TaskHeader* task);
void setSpriteActorAngle(SpriteActorCore* actor, s16 angle);
SpriteTask* spawnEffectSprite(s32 animationId, SpriteVramData* vram, s16* position, s32 extraSize);
void fadePaletteColors(s32 count, s32 level, u16* dst, u16* src);
void lerpPaletteColors(s32 count, s32 level, u16* dst, u16* src1, u16* src2);
s32 undoFieldModelRelocation(void* model);
s32 initModel5(void* model);
void patchSelfModifyingCode(s32 a, s32 b, s32 c, s32 d);
s32 getAllocationSize(void* data);
void* findSequenceInLoadedList(void* seq, s16 id);
MATRIX* createRotationMatrix(SVECTOR* r, MATRIX* m);
s32 getAngleCos(s32 angle);
s32 getAngleSin(s32 angle);
u32 doPointerRelocation(u32* data);
void loadSequence(void* data);
void setGraphicEntityScale(SpriteActorCore* actor, s32 scale);
/* main defines these two (u16, u16); this overlay's calls sign-extend, so it
 * keeps a local (s16, s16) prototype. */
void setupOverrideTPage(s16 x, s16 y);
void setupOverrideClut(s16 x, s16 y);
void spriteActorSetDirection(SpriteActorCore* actor, s32 direction);
s32 submitModelForRendering(sModelBlock* b, u8* prims, OTag* ot, s32 renderMode);
void unloadSequence(void* seq);
s32 uploadTextureToVram(void* data, s16 mode, s16 x, s16 y, s16 mode2, s16 clutX, s16 clutY);

/* libgte functions psyq.h does not declare yet */
MATRIX* MulMatrix2(MATRIX* m0, MATRIX* m1);

/* ---- cloth parts (fm_801E1A14.c): called through short parameters here,
 * defined with int gravity/scale/numColliders there ---- */
#ifndef PLATFORM_PC
void initMechaCloth(sLoadedMechas_114* obj, u16* data, s16 gravity, s16 scale, s16 x, s16 y, s16 z, s16 numColliders,
                   s16 tpx, s16 tpy, s16 uSpan, s16 vSpan, s16 clutX, s16 clutY, u8 frontR, u8 frontG, u8 frontB,
                   u8 backR, u8 backG, u8 backB);
#endif

/* ---- this overlay's data ---- */
extern s32 mechaBytecodeToggle;
extern sMechaInitVar4 mechaInitVar4[8];
extern s32 mechaInitVar4Counter;
extern void* mechaModelBlocksBufferForLoading;
extern u16 mechaPlayAnimationVar0;
extern s32 mechaInitVar0;
extern MATRIX* mechaFinalizeVar0;            /* Noah: mechaFinalizeVar0 (colour matrix) */
extern sMechaInitVar5 mechaInitVar5[2];
extern s16 gMechaAngle;
extern s16 mechaInitVar1;
extern sMechaInitVar3 mechaInitVar3;
extern sMechaInitVar2 mechaInitVar2;
extern u16 mechaPlayAnimationVar1;

/* defined in earlier files */
sMechaInitVar4* sMechaModel_init(sModel* model, sMechaInitVar4* out);
s32 updateMechAnimSub1(sMechaBone* bones, s32 scale);
s32 updateMechAnimSub2(sMechaBone* bones, s32 scale);
void freeMechaBones(sMechaBone* bones);
void freeMechaModelBlocks(sMechaInitVar4* p, s32 freeBlocks);
u32 updateMechAnimSub3(sMechaInitVar2* pool, sMechaBone* bones, s32 id, s32 speed);
sMechaInitVar2* initMechaInitVar2(sMechaInitVar2* arg0, s32 count);
void freeMechaInitVar2(sMechaInitVar2* arg0);
sMechaInitVar2Sub* allocateAnimTrack(sMechaInitVar2* arg0);
s32 releaseMechaAnimTrack(sMechaInitVar2* arg0, sMechaInitVar2Sub* arg1);
void mechaOP_8(sMechaInitVar2* arg0, sMechaBone* arg1);
void freeAnimTracks2(sMechaInitVar2* arg0, sMechaBone* arg1, u8 arg2);
sMechaInitVar3* initMechaInitVar3(sMechaInitVar3* arg0, s32 count);
void freeMechaInitVar3(sMechaInitVar3* arg0);
void drawMechaWheelTrail(sMechaInitVar3* arg0, MATRIX* m, s32 speed, OTag* ot, s32 oddOrEven);
void stopMechaTrailEmitter(sLoadedMechas_110* p, s32 oddOrEven);
s16 envTileStepSine(s16 arg0, s16 arg1, s16 arg2);
s32 envTileStepRampUp(s16 arg0, s16 arg1, s16 arg2);
s16 envTileStepRampDown(s16 arg0, s16 arg1, s16 arg2);
s16 envTileStepRampDownClamped(s16 arg0, s16 arg1, s16 arg2);
void freeAnimatedEnvTextureTile(sLoadedMechas_118* arg0);
void refreshAttachedMechaTransforms(sLoadedMechas** mechas);

void* getEnvTileStepFunction(s32 arg0) {
    switch (arg0) {
    case 1:
        return envTileStepRampUp;
    case 2:
        return envTileStepRampDown;
    case 3:
        return envTileStepRampDownClamped;
    default:
        return envTileStepSine;
    }
}

/* Noah: initMechaTransforms1. The constant stores (m3C/m5C/m39) come after
 * m98 in the source: sched2 hoists them with their li to the top. */
void initMechaTransforms1(sLoadedMechas* arg0, sMechaInitVar2* arg1, sMechaDataTable2_4_8** arg2,
                   sMechaDataTable2_4_4_array* arg3) {
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

void initMechaAnimation(sLoadedMechas* arg0, sLoadedMechas* arg1, sMechaInitVar2* arg2, s32 animationId) {
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
                arg0->m10_bytecode0 = arg1->mC_extraAnim->m4_anim[animationId - 0x50 + 2];
            }
            arg0->m42_loopCount = 0;
            arg0->m40_waitTimer = 0;
            arg0->m50_bytecode2 = NULL;
            arg0->m54_bytecode1 = NULL;
            arg0->m4C_bytecode3 = NULL;
            arg0->m10A_targetMask = mechaPlayAnimationVar0;
            arg0->m23_extraLoadPending = 0;
            processMechaAnimData(arg0, arg2, -1, 1, 0);
        }
    }
}

s32 updateMechAnim(sLoadedMechas* arg0, sMechaInitVar2* arg1, s32 iterations, s32 oddOrEven, s32 arg4) {
    s32 updateFlag;
    s32 i;

    if (arg0->m0_model != NULL) {
        updateFlag = 0;
        if (arg0->m34_visible != 0) {
            updateMechAnimSetupRootAltitude(arg0);
            if (arg0->m37_useBoneScale != 0) {
                updateMechAnimSub2(arg0->m4_bones, arg0->m1C_scale);
            } else {
                updateMechAnimSub1(arg0->m4_bones, arg0->m1C_scale);
            }
            for (i = 0; i < iterations; i++) {
                updateFlag |= updateMechAnimSub3(arg1, arg0->m4_bones, arg0->m3C_trackTag, arg0->m1C_scale);
                updateMechAnimSub4(arg0, arg1, oddOrEven);
            }
        }
        processMechaAnimData(arg0, arg1, updateFlag, iterations, arg4);
    }
    return updateFlag; /* sic: not set when m0_model is NULL */
}

void updateMechasBaseMatrixIsAttached(sLoadedMechas* arg0) {
    MATRIX* m;
    SVECTOR v;

    m = (MATRIX*)PSX_SCRATCH(0x1F800000);
    if (loadedMechas[arg0->m5C_attachMecha] != NULL) {
        if (!(arg0->m4A_flags & 0x10)) {
            arg0->m34_visible = loadedMechas[arg0->m5C_attachMecha]->m34_visible;
        }
        if (arg0->m34_visible != 0) {
            if (arg0->m5D_attachRotation != 0) {
                if (arg0->m5E_attachBone != 0) {
                    MulMatrix0(&loadedMechas[arg0->m5C_attachMecha]->m4_bones[0].m2C_boneFinalMatrix,
                               &loadedMechas[arg0->m5C_attachMecha]->m4_bones[arg0->m5E_attachBone].m2C_boneFinalMatrix, (MATRIX*)PSX_SCRATCH(0x1F800000));
                } else {
                    m = &loadedMechas[arg0->m5C_attachMecha]->m4_bones[0].m2C_boneFinalMatrix;
                }
                MulMatrix2(m, &arg0->m4_bones->mC_localMatrix);
                MulMatrix2(m, &arg0->m4_bones->m2C_boneFinalMatrix);
            }
            if (arg0->m5E_attachBone != 0) {
                CompMatrix(&loadedMechas[arg0->m5C_attachMecha]->m4_bones[0].mC_localMatrix,
                           &loadedMechas[arg0->m5C_attachMecha]->m4_bones[arg0->m5E_attachBone].m2C_boneFinalMatrix, m);
            } else {
                m = &loadedMechas[arg0->m5C_attachMecha]->m4_bones[0].mC_localMatrix;
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

/* ---- used by processMechaAnimData ---- */
s32 isCDBusy(void);
void fadeSoundEffectVolume(s32 id, s32 vol, s32 dur);
void signalFieldMechaAnimEvent(void);

void processMechaAnimData(sLoadedMechas* pMecha, sMechaInitVar2* tracks, s32 param_3, s32 param_4, s32 arg4) {
    VECTOR lv;
    SVECTOR sv;
    RECT rect;
    s32 tmp;
    s16 op;
    sLoadedMechas* pOrig;
    sMechaDataTable2_4_8* pNext;
    sMechaDataTable2_4_8* pCur;
    s32 cont;
    s32 local_78;
    u8 lo;
    u8 hi;
    s32 opLo;
    u8 opHi;
    u8 op3;
    sMechaDataTable2_4_4* keyframe;

    if (param_4 == 0) {
        return;
    }
    if (pMecha->m10_bytecode0 == NULL) {
        return;
    }
    resetMemoryAllocStats(4, 0);
    local_78 = 0;
    pOrig = pMecha;
    for (tmp = 0; tmp < param_4; tmp++) {
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
    if (pMecha->m4C_bytecode3 != NULL && computeMechaDistanceToTarget(pMecha) <= pMecha->m48_triggerDistance) {
        pNext = pMecha->m4C_bytecode3;
        pMecha->m4C_bytecode3 = NULL;
    } else {
        if (pMecha->m54_bytecode1 != NULL) {
            SVECTOR unused;
            SVECTOR pos;

            pos.vx = pMecha->m4_bones->m5C_translation[0];
            pos.vy = 0;
            pos.vz = pMecha->m4_bones->m5C_translation[2];
            pos.vy = pMecha->m60_groundY;
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
        updateMechaTargetPosition(pMecha);
    }

    while (cont) {
        pCur = pNext;
        op = *pNext++;
        lo = op & 0xFF;
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
            local_78 = 1;
            break;
        case 0x03:
            local_78 = 1;
            break;
        case 0x08:
            mechaOP_8(tracks, pMecha->m4_bones);
            mechaOP_19(pMecha);
            break;
        case 0x0A:
            mechaOP_A(tracks, pMecha->m4_bones, hi, 7);
            break;
        case 0x0B: {
            sMechaBone* bones = pMecha->m4_bones;
            s32 count;
            s32 i;

            mechaOP_8(tracks, bones);
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
            mechaOP_A(tracks, pMecha->m4_bones, hi, 1);
            break;
        case 0x0E:
            mechaOP_A(tracks, pMecha->m4_bones, hi, 2);
            break;
        case 0x10:
            mechaSetPoseFromKeyframe(pMecha->m4_bones, mechaGetKeyframe(pMecha, hi, &tmp));
            break;
        case 0x11: {
            u32 w;

            keyframe = mechaGetKeyframe(pMecha, hi, &tmp);
            w = *pNext++;
            op = w;
            if (tmp == 0) {
                mechaOP_11(tracks, pMecha->m4_bones, keyframe, w >> 8, (u8)op);
                param_3 = -1;
                pMecha->m8E_stepDistance = abs(keyframe->m10_stepDistance * (pMecha->m1C_scale * pMecha->m4_bones->m4C_scale[2] >> 12) >> 12);
                mechaOP_11_b(pMecha, keyframe, w >> 8);
            }
            break;
        }
        case 0x13: {
            sMechaDataTable2_4_8 w;

            w = *pNext++;
            opHi = w >> 8;
            op = w;
            opLo = (u8)op;
            w = *pNext++;
            lo = w >> 8;
            op = w;
            op3 = (u8)op;
            keyframe = mechaGetKeyframe(pMecha, opLo, &tmp);
            if (mechaBytecodeToggle != 0) {
                mechaSetPoseFromKeyframe(pMecha->m4_bones, keyframe);
            } else {
                mechaOP_13(tracks, pMecha->m4_bones, keyframe, lo, hi, op3, opHi);
            }
            param_3 = -1;
            mechaOP_19(pMecha);
            break;
        }
        case 0x14: {
            sMechaDataTable2_4_8 w;
            u8 target;

            w = *pNext++;
            op = w;
            target = op & 0xFF;
            opHi = w >> 8;
            lo = getMechaBitfieldForAnim(pMecha, target, &op);
            getMechaBitfieldForAnim(pMecha, hi, &op);
            op3 = pMecha->m2A_animQueue[1];
            if (hi == 0xFD) {
                pMecha->m2A_animQueue[1] = 0;
            }
            for (tmp = 0; tmp < 8; tmp++) {
                if ((op >> tmp) & 1) {
                    if (target == 0xFF) {
                        initMechaAnimation(loadedMechas[tmp], loadedMechas[tmp], tracks, opHi);
                    } else {
                        initMechaAnimation(loadedMechas[tmp], loadedMechas[lo], tracks, opHi);
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
            for (tmp = 8; tmp < 10; tmp++) {
                if (loadedMechas[tmp] == NULL) {
                    pNew = allocateHeap(PC_HEAP_SIZED(sizeof(sLoadedMechas), 0x134), 1);
                    break;
                }
            }
            *pNew = *pMecha;
            loadedMechas[tmp] = pNew;
            pNew->m98_eventFrame = -1;
            pNew->m3C_trackTag = -1;
            pNew->m5C_attachMecha = -1;
            pNew->m40_waitTimer = 0;
            pNew->m58_targetMecha = 0;
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
            initMechSub110(pNew);
            newBones = allocateHeap(PC_HEAP_SIZED(pMecha->m4_bones->mA_numBones * sizeof(sMechaBone), pMecha->m4_bones->mA_numBones * 0x7c), 1);
            pNew->m4_bones = newBones;
            for (tmp = 0; tmp < pMecha->m4_bones->mA_numBones; tmp++) {
                newBones[tmp] = pMecha->m4_bones[tmp];
                if (pMecha->m4_bones[tmp].m0_parentBone != NULL) {
                    newBones[tmp].m0_parentBone =
                        &newBones[((sptr)pMecha->m4_bones[tmp].m0_parentBone - (sptr)pMecha->m4_bones) / sizeof(sMechaBone)];
                }
                newBones[tmp].m7_isEnabled = 0;
                newBones[tmp].m70_animTracks[0] = NULL;
                newBones[tmp].m70_animTracks[1] = NULL;
            }
            detachMechaBoneSubtree(tracks, op, pMecha->m4_bones, newBones);
            if (hi != 0xFF) {
                initMechaAnimation(pNew, pNew, tracks, hi);
            }
            break;
        }
        case 0x16:
            reattachMechaBones(pMecha->m4_bones, loadedMechas[pMecha->m21_sourceEntryId]->m4_bones);
            freeMecha(pMecha->m20_mechaEntryId);
            if (hi != 0xFF) {
                initMechaAnimation(loadedMechas[pMecha->m21_sourceEntryId], loadedMechas[pMecha->m21_sourceEntryId], tracks, hi);
            }
            return;
        case 0x17:
            freeMecha(pMecha->m20_mechaEntryId);
            return;
        case 0x18:
            mechaOP_11_b(pMecha, mechaGetKeyframe(pMecha, hi, &tmp), (s16)*pNext++);
            break;
        case 0x19:
            mechaOP_19(pMecha);
            break;
        case 0x1A: {
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
        case 0x1D: {
            sMechaDataTable2_4_8 w0;
            sMechaDataTable2_4_8 w1;

            w0 = *pNext++;
            op = w0;
            opLo = (u8)op;
            w1 = *pNext++;
            op = w1;
#ifndef PLATFORM_PC
            setupBoneInterpolationTrack(pMecha, tracks, &pMecha->m4_bones[hi], opLo, w0 >> 8, (u8)op, w1 >> 8, (s16)*pNext++,
                          (s16)*pNext++, (s16)*pNext++, (s16)*pNext++, (s16)*pNext++, (s16)*pNext++, (s16)*pNext++);
#else
            /* The PS1 compiler evaluates the arguments left to right, the PC's right to left. */
            setupBoneInterpolationTrack(pMecha, tracks, &pMecha->m4_bones[hi], opLo, w0 >> 8, (u8)op, w1 >> 8, (s16)pNext[0],
                          (s16)pNext[1], (s16)pNext[2], (s16)pNext[3], (s16)pNext[4], (s16)pNext[5], (s16)pNext[6]);
            pNext += 7;
#endif
            param_3 = -1;
            break;
        }
        case 0x1E:
            pMecha->m37_useBoneScale = hi;
            break;
        case 0x1F: {
            sLoadedMechas* p = loadedMechas[getMechaBitfieldForAnim(pOrig, hi, &op)];

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
            setBoneVisibility(pMecha, &pMecha->m4_bones[(s16)*pNext++], hi);
            break;
        case 0x24:
            if (pMecha != NULL) {
                pMecha->m34_visible = hi & 1;
            }
            break;
        case 0x25: {
            VECTOR d;
            MATRIX m;
            VECTOR ax;
            VECTOR ay;
            VECTOR az;
            SVECTOR unit;
            sMechaDataTable2_4_8 w;
            u16 ox;
            u16 oy;
            u16 oz;
            sMechaBone* bone;

            w = *pNext++;
            op = w;
            opHi = w >> 8;
            getMechaBitfieldForAnim(pMecha, (u8)op, &op);
            ox = *pNext++;
            oy = *pNext++;
            oz = *pNext++;
            for (tmp = 0; tmp < 8; tmp++) {
                if ((op >> tmp) & 1) {
                    if (loadedMechas[tmp] != NULL) {
                        /* The nested do/while (0) only weights opHi for the register allocator
                         * (flow counts refs by loop depth; 12 levels in all): retail gives opHi s2
                         * ahead of hi (s5). No code change. */
                        do { do { do { do { do { do { loadedMechas[tmp]->m5E_attachBone = opHi; } while (0); } while (0); } while (0); } while (0); } while (0); } while (0);
                        loadedMechas[tmp]->m5C_attachMecha = pMecha->m20_mechaEntryId;
                        loadedMechas[tmp]->m5D_attachRotation = hi & 2;
                        loadedMechas[tmp]->m36_disableGroundSnap = 1;
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
                            d.vx = loadedMechas[tmp]->m4_bones->m5C_translation[0] - bone->m2C_boneFinalMatrix.t[0];
                            d.vy = loadedMechas[tmp]->m4_bones->m5C_translation[1] - bone->m2C_boneFinalMatrix.t[1];
                            d.vz = loadedMechas[tmp]->m4_bones->m5C_translation[2] - bone->m2C_boneFinalMatrix.t[2];
                            loadedMechas[tmp]->m6A_attachOffset[0] = projectOnPlaneNormalScaled(&d, &ay, &az, pMecha->m1C_scale);
                            loadedMechas[tmp]->m6A_attachOffset[1] = projectOnPlaneNormalScaled(&d, &az, &ax, pMecha->m1C_scale);
                            loadedMechas[tmp]->m6A_attachOffset[2] = projectOnPlaneNormalScaled(&d, &ax, &ay, pMecha->m1C_scale);
                        } else {
                            loadedMechas[tmp]->m6A_attachOffset[0] = ox;
                            loadedMechas[tmp]->m6A_attachOffset[1] = oy;
                            loadedMechas[tmp]->m6A_attachOffset[2] = oz;
                        }
                    }
                }
            }
            break;
        }
        case 0x26:
            getMechaBitfieldForAnim(pMecha, hi, &op);
            for (tmp = 0; tmp < 8; tmp++) {
                if ((op >> tmp) & 1 && loadedMechas[tmp] != NULL) loadedMechas[tmp]->m5C_attachMecha = 0xFF;
            }
            break;
        case 0x27:
            if (pMecha->m37_useBoneScale != 0) {
                updateMechAnimSub2(pMecha->m4_bones, pMecha->m1C_scale);
            } else {
                updateMechAnimSub1(pMecha->m4_bones, pMecha->m1C_scale);
            }
            break;
        case 0x28:
        case 0x29: {
            s32 dist;
            s32 found;
            sMechaBone* bone;

            dist = computeMechaDistanceToTarget(pMecha);
            if (pMecha->m8E_stepDistance == 0) {
                pMecha->m8E_stepDistance = 1;
            }
            op = *pNext++;
            opHi = op >> 8;
            bone = &pMecha->m4_bones[(u8)op];
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
                } else {
                    if (!(dist < hi)) {
                        break;
                    }
                    pNext = pCur;
                    cont = 0;
                    break;
                }
            }
            pNext = pCur;
            cont = 0;
            break;
        }
        case 0x2A:
            if (computeMechaDistanceToTarget(pMecha) < pMecha->m8E_stepDistance) {
                break;
            }
            pNext = pCur;
            cont = 0;
            break;
        case 0x2B:
            if (computeMechaDistanceToTarget(pMecha) > pMecha->m8E_stepDistance) {
                break;
            }
            pNext = pCur;
            cont = 0;
            break;
        case 0x2E:
            pMecha->m48_triggerDistance = pMecha->m8E_stepDistance;
            op = *pNext++;
            if (hi != 0) {
                pMecha->m4C_bytecode3 = (sMechaDataTable2_4_8*)((u8*)pCur + op);
            } else {
                pMecha->m4C_bytecode3 = NULL;
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
        case 0x38:
            {
                u16 w = *pNext++;

                op = w;
                setupBoneLookAtTrack(tracks, pMecha->m4_bones, 0, hi, (u8)op, w >> 8, pMecha->m88_targetPosition[0], pMecha->m88_targetPosition[1], pMecha->m88_targetPosition[2]);
            }
            break;
        case 0x39:
            {
                u16 w = *pNext++;

                op = w;
                setupBoneLookAtTrack(tracks, pMecha->m4_bones, 1, hi, (u8)op, w >> 8, pMecha->m88_targetPosition[0], pMecha->m88_targetPosition[1], pMecha->m88_targetPosition[2]);
            }
            break;
        case 0x33:
        case 0x34:
        case 0x3B:
            op = *pNext++;
            break;
        case 0x3C:
            op = *pNext++;
            opLo = (u8)op;
            opHi = op >> 8;
            fadeSoundEffectVolume(opLo + getMechaSoundEffectIdBase(pMecha, hi), 0, opHi);
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
        case 0x40: {
            s16 x;
            s16 y;
            s16 z;

            x = *pNext++;
            y = *pNext++;
            z = *pNext++;
            setupBoneRotationTrack(tracks, pMecha->m4_bones, hi, x, y, z);
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
                setupBoneRotationTrack(tracks, pMecha->m4_bones, hi, rx, ry, rz);
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
                setupBoneRotationTrack(tracks, pMecha->m4_bones, hi, pitch, yaw, roll);
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
            tmp = getMechaBitfieldForAnim(pMecha, hi, &op);
            pMecha->m8E_stepDistance += getMechaColliderRadius(tmp);
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
                initMechaAnimation(pMecha, loadedMechas[pMecha->m2A_animQueue[tmp + 1]], tracks, pMecha->m2A_animQueue[tmp + 5]);
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
        case 0x62: {
            s32 bone;
            s32 x;

            bone = (s16)*pNext++;
            x = (s16)*pNext++;
#ifndef PLATFORM_PC
            mechaOp_62(pMecha, &pMecha->m4_bones[bone], hi, x, (s16)*pNext++, (s16)*pNext++);
#else
            /* The PS1 compiler evaluates the arguments left to right, the PC's right to left. */
            mechaOp_62(pMecha, &pMecha->m4_bones[bone], hi, x, (s16)pNext[0], (s16)pNext[1]);
            pNext += 2;
#endif
            break;
        }
        case 0x63: {
            sMechaDataTable2_4_8 w;

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
            u8 idx = getMechaBitfieldForAnim(pMecha, hi, &op);
            sLoadedMechas* p;

            op = *pNext++;
            p = loadedMechas[idx];
            if (p != NULL && p->m38_syncFlag == (op & 1)) {
                pNext = pCur;
                cont = 0;
                break;
            }
            break;
        }
        case 0x6F:
            if (hi != 0) {
                pMecha->m3A_savedAnimMask = mechaPlayAnimationVar0;
            } else {
                pMecha->m3A_savedAnimMask = -1;
            }
            break;
        case 0x70:
            op = *pNext++;
            if (pMecha->m3A_savedAnimMask == mechaPlayAnimationVar0) {
                pNext = (sMechaDataTable2_4_8*)((u8*)pCur + op);
                cont = 0;
                break;
            }
            break;
        case 0x04:
        case 0x05:
        case 0x06:
        case 0x07:
        case 0x09:
        case 0x0F:
        case 0x12:
        case 0x1B:
        case 0x1C:
        case 0x2C:
        case 0x2D:
        case 0x2F:
        case 0x3A:
        case 0x3E:
        case 0x3F:
        case 0x51:
        case 0x52:
        case 0x53:
        case 0x58:
        case 0x59:
        case 0x5A:
        case 0x60:
        case 0x61:
        case 0x65:
        case 0x66:
        case 0x67:
        case 0x68:
        case 0x69:
        case 0x6A:
            break;
        default:
            pNext--;
            cont = 0;
            break;
        }
    }
    pMecha->m10_bytecode0 = pNext;
    if (local_78 != 0) {
        signalFieldMechaAnimEvent();
    }
}

void setupBoneRotationTrack(sMechaInitVar2* pool, sMechaBone* pBone, s32 duration, s32 x, s32 y, s32 z) {
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
        pTrack = allocateAnimTrack(pool);
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

void setupBoneLookAtTrack(sMechaInitVar2* pool, sMechaBone* pBone, s32 type, s32 arg3, s32 arg4, s32 duration, s32 x, s32 y,
                   s32 z) {
    sMechaInitVar2Sub* pTrack;

    if (pBone->m70_animTracks[0] != NULL) {
        pTrack = pBone->m70_animTracks[0];
    } else {
        pTrack = allocateAnimTrack(pool);
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

void mechaOP_11_b(sLoadedMechas* pMecha, sMechaDataTable2_4_4* pKey, s32 loop) {
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

s32 getMechaSoundEffectIdBase(sLoadedMechas* pMecha, s32 which) {
    if (which == 0) {
        return gBattleCurrentSoundEffectSeq->u14.m14_id << 16;
    } else if (which != 1) {
        if (which == 2) {
            return *(u16*)(pMecha->mB4_extraSound->m8_seq + 0x14) << 16;
        }
    } else {
        return *(u16*)(pMecha->mB0_sound->m8_seq + 0x14) << 16;
    }
    FALLOFF_RETURN(2); /* any other bank: the last compare's 2 */
}

void updateMechAnimSub4(sLoadedMechas* pMecha, sMechaInitVar2* pool, s32 oddOrEven) {
    sMechaAnimEvent* p;
    sLoadedMechas_118* pParent;
    MATRIX* pColor;
    s32 i;
    s16 useOther; /* sic: never set; retail reads its spill slot 0x50(sp) */
    s16 x0, y0, x1, y1, z1;
    void* t;

    if (pMecha->m98_eventFrame < 0) {
        return;
    }
    for (; pMecha->m9C_eventIndex < pMecha->m9E_numEvents; pMecha->m9C_eventIndex++) {
        p = (sMechaAnimEvent*)pMecha->mA0_eventCursor;
        if (pMecha->m98_eventFrame != p->m0_frame) {
            break;
        }
        switch (p->m2_type) {
        case 1:
            pMecha->mA0_eventCursor += 0x14;
            break;
        case 2:
            if (p->m4 != 0) {
                if (p->m3_index < 2) {
                    sMechaAnimEventLight* e = (sMechaAnimEventLight*)p;

                    if (e->m5_isAbsolute != 0) {
                        mechaInitVar5[e->m3_lightIndex].m10_mechaIndex = -1;
                    } else {
                        mechaInitVar5[e->m3_lightIndex].m10_mechaIndex = pMecha->m20_mechaEntryId;
                    }
                    mechaInitVar5[e->m3_lightIndex].m12_boneIndex = e->m6_boneIndex;
                    mechaFinalizeVar0->m[0][e->m3_lightIndex + 1] = e->m7_r << 4;
                    mechaFinalizeVar0->m[1][e->m3_lightIndex + 1] = e->m8_g << 4;
                    mechaFinalizeVar0->m[2][e->m3_lightIndex + 1] = e->m9_b << 4;
                    mechaInitVar5[e->m3_lightIndex].m8_localPosition.vx = e->mA_x;
                    mechaInitVar5[e->m3_lightIndex].m8_localPosition.vy = e->mC_y;
                    mechaInitVar5[e->m3_lightIndex].m8_localPosition.vz = e->mE_z;
                    mechaInitVar5[e->m3_lightIndex].m6_isActive = e->m10_isActive;
                }
                pMecha->mA0_eventCursor += 0x12;
            } else {
                mechaInitVar5[p->m3_index].m6_isActive = 0;
                pMecha->mA0_eventCursor += 6;
            }
            break;
        case 3:
        case 4:
            stopMechaTrailEmitter(&pMecha->m110_trails[p->m3_index], oddOrEven);
            if (p->m4 != 0) {
                pMecha->mA0_eventCursor += 0x1C;
            } else {
                pMecha->mA0_eventCursor += 6;
            }
            break;
        case 5:
            pMecha->mA0_eventCursor += 8;
            break;
        case 6:
            pMecha->mA0_eventCursor += 4;
            break;
        case 7:
            pMecha->m4_bones[p->m4].m7_isEnabled = p->m5 & 1;
            pMecha->mA0_eventCursor += 6;
            break;
        case 8: {
            sMechaAnimEvent* e = p;
            s16 savedId = mechaPlayAnimationVar1;
            u16 savedMask = mechaPlayAnimationVar0;
            s32 mask = 1 << savedId;

            for (i = 0; i < 8; i++) {
                if ((pMecha->m10A_targetMask >> i) & 1) {
                    s32 anim = e->m5;

                    if (loadedMechas[i] != NULL && anim > 0) {
                        if (useOther) {
                            mechaPlayAnimationFromSource(pMecha, i, mask, anim);
                        } else {
                            mechaPlayAnimation(i, mask, anim);
                        }
                    }
                }
            }
            mechaPlayAnimationVar1 = savedId;
            mechaPlayAnimationVar0 = savedMask;
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
                        pColor = mechaFinalizeVar0;
                    }
                    t = getEnvTileStepFunction(p->m7_stepFunctionId);
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
                    initAnimatedEnvTextureTile(&pMecha->m118_colorAnims[p->m3_index], pParent, p->m6_tileMode & 0x7F, p->m12_sourceFlags | 0x700,
                                  (sLoadedMechas_118_1C*)pColor, x0, y0, 0, x1, y1, z1, x0, y0, p->m13_width, p->m14_height, p->m16_speed,
                                  p->m18_stepDivisor, p->m1A_stepBase, t);
                }
                pMecha->mA0_eventCursor += 0x1C;
            } else {
                freeAnimatedEnvTextureTile(&pMecha->m118_colorAnims[p->m3_index]);
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

void mechaOP_19(sLoadedMechas* pMecha) {
    pMecha->m98_eventFrame = -1;
}

s32 computeMechaDistanceToTarget(sLoadedMechas* pMecha) {
    sMechaBone* bones = pMecha->m4_bones;
    s32 dx = pMecha->m88_targetPosition[0] - bones->m5C_translation[0];
    s32 dy = pMecha->m88_targetPosition[1] - bones->m5C_translation[1];
    s32 dz = pMecha->m88_targetPosition[2] - bones->m5C_translation[2];

    return SquareRoot0(dx * dx + dy * dy + dz * dz);
}

void updateMechaTargetPosition(sLoadedMechas* pMecha) {
    VECTOR out;
    s32 unused; /* reserves the retail stack slot */
    s32 id;
    sLoadedMechas* pTarget;
    MATRIX* m;
    sMechaInitVar2Sub* pTrack;

    id = 0;
    if (pMecha->m58_targetMecha == 0xFF) {
        for (id = 0; id < 8; id++) {
            if ((pMecha->m10A_targetMask >> id) & 1) {
                break;
            }
        }
    }
    if (pMecha->m58_targetMecha == 0xFE) {
        id = mechaPlayAnimationVar1;
    }
    if (pMecha->m58_targetMecha == 0xFD) {
        id = pMecha->m20_mechaEntryId;
    }
    if (pMecha->m58_targetMecha == 0xFC) {
        id = pMecha->m21_sourceEntryId;
    }
    if (pMecha->m58_targetMecha == 0xFA) {
        id = 10;
    }
    if (pMecha->m58_targetMecha > 0 && pMecha->m58_targetMecha < 0x80) {
        id = pMecha->m58_targetMecha - 1;
    }
    pTarget = loadedMechas[id];
    if (pTarget != NULL && id != pMecha->m20_mechaEntryId) {
        if (pMecha->m5A_targetBone != 0) {
            m = (MATRIX*)PSX_SCRATCH(0x1F800000);
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
    }
}

void detachMechaBoneSubtree(sMechaInitVar2* pool, s32 index, sMechaBone* bones, sMechaBone* bones2) {
    sMechaBone* p = bones;
    s32 n = bones->mA_numBones;
    s32 i;

    bones[index].m7_isEnabled = 0;
    bones2[index].m7_isEnabled = 1;
    releaseMechaAnimTrack(pool, bones[index].m70_animTracks[0]);
    bones[index].m70_animTracks[0] = NULL;
    releaseMechaAnimTrack(pool, bones[index].m70_animTracks[1]);
    bones[index].m70_animTracks[1] = NULL;
    releaseMechaAnimTrack(pool, bones[index].m70_animTracks[2]);
    bones[index].m70_animTracks[2] = NULL;
    for (i = 1; i < n; i++) {
        p++;
        if (p->m0_parentBone == &bones[index]) {
            detachMechaBoneSubtree(pool, p->mA_numBones, bones, bones2);
        }
    }
}

void reattachMechaBones(sMechaBone* bones, sMechaBone* bones2) {
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

s16 projectOnPlaneNormalScaled(VECTOR* pDir, VECTOR* a, VECTOR* b, s32 scale) {
    VECTOR v;
    s32 dot;
    s32 len;

    OuterProduct12(a, b, &v);
    dot = v.vx * pDir->vx + v.vy * pDir->vy + v.vz * pDir->vz;
    len = SquareRoot0(v.vx * v.vx + v.vy * v.vy + v.vz * v.vz) + 1;
    dot = (dot << 4) / len;
    return (dot << 8) / scale;
}

s32 getFirstAnimTargetMecha(void) {
    s32 i;

    for (i = 0; i < 8; i++) {
        if ((mechaPlayAnimationVar0 >> i) & 1) {
            break;
        }
    }
    return i;
}

u8 getMechaBitfieldForAnim(sLoadedMechas* pMecha, u8 id, u16* pMask) {
    if (id == 0xFF) {
        id = getFirstAnimTargetMecha();
    } else if (id == 0xFE) {
        id = mechaPlayAnimationVar1;
    } else if (id == 0xFD || id == 0xF9) {
        id = pMecha->m20_mechaEntryId;
    } else if (id == 0xFC) {
        id = pMecha->m21_sourceEntryId;
    } else if (id == 0xFA) {
        id = 10;
    } else if (id == 0xF8) {
        id = pMecha->m20_mechaEntryId * 2 + 8;
    } else if (id == 0xF7) {
        id = pMecha->m20_mechaEntryId * 2 + 9;
    }
    *pMask = 1 << id;
    return id;
}

sMechaDataTable2_4_4* mechaGetKeyframe(sLoadedMechas* pMecha, u8 id, s32* pFlag) {
    *pFlag = 0;
    if (id >= 0xFE) {
        id = pMecha->m2A_animQueue[0] & 0x7F;
        *pFlag = pMecha->m2A_animQueue[0] & 0x80;
    }
    if (id < 0x40) {
        return pMecha->m14_keyframes->m4_keyframes[id];
    }
    return (pMecha->m18_extraKeyframes->m4_keyframes - 0x40)[id];
}

void setupBoneInterpolationTrack(sLoadedMechas* pMecha, sMechaInitVar2* pool, sMechaBone* pBone, u8 flags, u8 mode, u8 tag,
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
        pTrack = allocateAnimTrack(pool);
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
                setupBoneInterpolationTrack(pMecha, pool, p, flags, mode, tag, loop, x, y, z, tx, ty, tz, duration);
            }
        }
    }
}

void setBoneVisibility(sLoadedMechas* pMecha, sMechaBone* pBone, s32 flags) {
    sMechaBone* p;
    s32 i;

    pBone->m7_isEnabled = flags & 1;
    if (flags & 0x80) {
        p = pMecha->m4_bones;
        for (i = 1; i < pMecha->m4_bones->mA_numBones; i++) {
            p++;
            if (p->m0_parentBone == pBone) {
                setBoneVisibility(pMecha, p, flags);
            }
        }
    }
}

void spawnMechaBoneAttachedSprite(SpriteVramData* vram, s32 animationId, s16* position, s16 direction, s16 scale,
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
        setTaskUpdateFunction(&task->m0_updateTask, (void (*)(TaskHeader*))updateMechaBoneAttachedSprite);
        pAttach->m10_offset.vx = pConfig->m6_offsetX;
        pAttach->m10_offset.vy = pConfig->m8_offsetY;
        pAttach->m10_offset.vz = pConfig->mA_offsetZ;
        pAttach->mE_snapToMechaAltitude = pConfig->mC_snapToMechaAltitude;
    }
}

void updateMechaBoneAttachedSprite(SpriteTask* task) {
    VECTOR out;
    sMechaBoneAttachment* pAttach;
    MATRIX* m;

    pAttach = (sMechaBoneAttachment*)((u8*)task + (s16)task->m38_actor.m86_thisSize);
#ifdef PLATFORM_PC
    if (pAttach->m8_mecha->m4_bones == NULL) {
        /* The battle copy's guard (updateMechaBoneAttachedSprite_battle): an effect still
         * running on a mecha freeMecha released would read the local matrix through NULL; the
         * PS1 reads low RAM there, zero at 0x0C-0x2B, so the composed matrix is zero. Retail
         * never installs this callback (spawnMechaBoneAttachedSprite has no caller in US or
         * JP, and updateMechAnimSub4 skips the effect event that spawns it in battle): parity
         * with the battle copy only. */
        static MATRIX zero;
        m = &zero;
    } else
#endif
    if (pAttach->mC_boneIndex != 0) {
        m = (MATRIX*)PSX_SCRATCH(0x1F800000);
        CompMatrix(&pAttach->m8_mecha->m4_bones->mC_localMatrix,
                   &pAttach->m8_mecha->m4_bones[pAttach->mC_boneIndex].m2C_boneFinalMatrix, m);
    } else {
        m = &pAttach->m8_mecha->m4_bones->mC_localMatrix;
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

void mechaOp_62(sLoadedMechas* pMecha, sMechaBone* pBone, u8 flags, s16 x, s16 y, s16 z) {
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
                mechaOp_62(pMecha, p, flags, x, y, z);
            }
        }
    }
}

void updateMechAnimSetupRootAltitude(sLoadedMechas* pMecha) {
    s16 buf[10];

    buf[9] = pMecha->m60_groundY;
    if (pMecha->m36_disableGroundSnap == 0) {
        pMecha->m4_bones->m5C_translation[1] = buf[9];
    }
}

void getMechaBoneMatrix(MATRIX* pOut, void* unused, s32 mechaIndex, s32 boneIndex) {
    MATRIX tmp; /* unused: reserves the retail stack frame */
    sLoadedMechas* pMecha = loadedMechas[mechaIndex];

    if (pMecha != NULL) {
        if (boneIndex != 0) {
            CompMatrix(&pMecha->m4_bones->mC_localMatrix, &pMecha->m4_bones[boneIndex].m2C_boneFinalMatrix, pOut);
        } else {
            *pOut = pMecha->m4_bones->mC_localMatrix;
        }
    }
}

void setMechaBytecodeToggle(s32 arg0) {
    mechaBytecodeToggle = arg0 & 1;
}

void mechaInitOverlay(s32 count) {
    s32 i;

    mechaInitVar0 = 0;
    mechaInitVar1 = 0;
    initMechaInitVar2(&mechaInitVar2, count);
    initMechaInitVar3(&mechaInitVar3, 0x10);
    for (i = 0; i < 10; i++) {
        loadedMechas[i] = NULL;
    }
    for (i = 0; i < 8; i++) {
        mechaInitVar4[i].m0_blocks = NULL;
    }
    for (i = 0; i < 2; i++) {
        mechaInitVar5[i].m6_isActive = 0;
    }
}

void mechaInitNewMecha(s32 entryId, u16 flags, sMechaDataTable2* pData2, sMechaDataTable1* pData1, s16 tpageX,
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
    if (entryId >= 10) {
        return;
    }
    if (loadedMechas[entryId] != NULL) {
        return;
    }
    pMecha = allocateHeap(PC_HEAP_SIZED(sizeof(sLoadedMechas), 0x134), 0);
    if (!(flags & 1)) {
#ifndef PLATFORM_PC
        doPointerRelocation((u32*)pData1);
        doPointerRelocation((u32*)pData1->m10_gearInfo);
#else
        pData1 = pcConvertMechaData1(pData1);
#endif
    }
    pMecha->m62_isSeqLoaded = 0;
    pMecha->m63_isSeq2Loaded = 0;
    if (!(flags & 4)) {
#ifndef PLATFORM_PC
        doPointerRelocation((u32*)pData2);
        doPointerRelocation((u32*)pData2->m8_sound);
        pAnim = pData2->m4_anim;
        doPointerRelocation((u32*)pAnim);
        doPointerRelocation((u32*)pAnim->m4_keyframes);
#else
        pData2 = pcConvertMechaData2(pData2);
#endif
        pSound = pData2->m8_sound;
        if (pSound->mC_seqEnd != pSound->m8_seq && findSequenceInLoadedList(pSound->m8_seq, 0) == NULL) {
            loadSequence(pSound->m8_seq);
            pMecha->m62_isSeqLoaded = 1;
        }
    }
    pTable10 = pData1->m10_gearInfo;
    p = (u16*)pTable10->m4_params;
    pTextures = pData1->m4_textures;
    pModel = pData1->m8_modelBlocks;
    pBoneTable = pData1->mC_bones;
    loadedMechas[entryId] = pMecha;
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
        mechaModelBlocksBufferForLoading = allocateHeap(PC_HEAP_BYTES(modelSize), 1);
        memcpy(mechaModelBlocksBufferForLoading, pModel, modelSize);
        for (mechaInitVar4Counter = 0; mechaInitVar4Counter < 8; mechaInitVar4Counter++) {
            if (mechaInitVar4[mechaInitVar4Counter].m0_blocks == NULL) {
                break;
            }
        }
        sMechaModel_init(mechaModelBlocksBufferForLoading, &mechaInitVar4[mechaInitVar4Counter]);
    }
    pMecha->m0_model = &mechaInitVar4[mechaInitVar4Counter];
    if (!(flags & 0x40)) {
        if (pMecha->m4A_flags & 4) {
            pMecha->m4_bones = processMechaMesh(pMecha->m0_model, (sMechaDataTable1_C*)pBoneTable, 2, 0, 0, 0, 0, 0);
        } else {
            pMecha->m4_bones =
                processMechaMesh(pMecha->m0_model, (sMechaDataTable1_C*)pBoneTable, 2, 1, tpageX, tpageY, clutX, clutY);
        }
    } else {
        pMecha->m4_bones = processMechaMesh(pMecha->m0_model, (sMechaDataTable1_C*)pBoneTable, 0, 0, 0, 0, 0, 0);
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
        pMecha->mB8_Polys[i].r0 = 0x40;
        pMecha->mB8_Polys[i].g0 = 0x40;
        pMecha->mB8_Polys[i].b0 = 0x40;
        pMecha->mB8_Polys[i].clut = GetClut(0x100, 0xF3);
        pMecha->mB8_Polys[i].tpage = GetTPage(0, 2, 0x280, 0x100);
        pMecha->mB8_Polys[i].u0 = 0;
        pMecha->mB8_Polys[i].v0 = 0xE0;
        pMecha->mB8_Polys[i].u1 = 0xF;
        pMecha->mB8_Polys[i].v1 = 0xE0;
        pMecha->mB8_Polys[i].u2 = 0;
        pMecha->mB8_Polys[i].v2 = 0xEF;
        pMecha->mB8_Polys[i].u3 = 0xF;
        pMecha->mB8_Polys[i].v3 = 0xEF;
    }
    pMecha->m1C_scale = p[4];
    pMecha->m10C_num110 = p[7];
    initMechSub110(pMecha);
    pMecha->m10E_num118 = p[8];
    if (pMecha->m10E_num118 != 0) {
        pMecha->m118_colorAnims = allocateHeap(PC_HEAP_SIZED(pMecha->m10E_num118 * sizeof(sLoadedMechas_118), pMecha->m10E_num118 * 0x30), 0);
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
        pMecha->m114_cloths = p114 = allocateHeap(PC_HEAP_SIZED(pMecha->m10D_num114 * sizeof(sLoadedMechas_114), pMecha->m10D_num114 * 0x24), 0);
        for (i = 0; i < pMecha->m10D_num114; i++, p114++) {
            count = (s16)p[17];
            p114->m0_boneIndex = *p++;
#ifndef PLATFORM_PC
            initMechaCloth(p114, pTable10->m8_clothData[i], *p++, *p++, *p++, *p++, *p++, count, tpageX + *p++,
                          tpageY + *p++, *p++, *p++, clutX + *p++, clutY, *p++, *p++, *p++, *p++, *p++, *p);
            p += 2;
#else
            /* The PS1 compiler evaluates the arguments left to right, the PC's right to left. */
            initMechaCloth(p114, pTable10->m8_clothData[i], p[0], p[1], p[2], p[3], p[4], count, tpageX + p[5],
                          tpageY + p[6], p[7], p[8], clutX + p[9], clutY, p[10], p[11], p[12], p[13], p[14], p[15]);
            p += 15 + 2;
#endif
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
    pMecha->m34_visible = 1;
    if (!(flags & 0x40)) {
        pAnim = pData2->m4_anim;
        pMecha->mB0_sound = pData2->m8_sound;
        initMechaTransforms1(pMecha, &mechaInitVar2, pAnim->m8_bytecode, pAnim->m4_keyframes);
        initMechaAnimation(pMecha, pMecha, &mechaInitVar2, 0);
    }
    if (!(flags & 2)) {
        initModel5(mechaModelBlocksBufferForLoading);
        undoFieldModelRelocation(mechaModelBlocksBufferForLoading);
        size = getAllocationSize(mechaModelBlocksBufferForLoading);
        pNewModel = allocateHeap(PC_HEAP_SIZED(size, pcHeapSize(mechaModelBlocksBufferForLoading)), 0);
        memcpy(pNewModel, mechaModelBlocksBufferForLoading, size);
        freeHeap(mechaModelBlocksBufferForLoading);
        freeMechaModelBlocks(pMecha->m0_model, 0);
        sMechaModel_init(pNewModel, pMecha->m0_model);
        pMecha->mA8_model = pNewModel;
    } else {
        pMecha->mA8_model = NULL;
    }
#ifdef PLATFORM_PC
    if (!(flags & 1)) {
        freeHeap(pData1); /* the converted tables: the caller frees the file */
    }
#endif
}

void renderMechas(MATRIX* pMatrix, MATRIX* pArgs, OTag* ot, s32 oddOrEven, s32 flag) {
    s32 iterations;
    s32 i;
    SVECTOR unused; /* reserves the retail stack slot */

    iterations = 0;
    mechaInitVar0 += 1 + flag;
    if (mechaInitVar0 > 6) {
        mechaInitVar0 = 6;
    }
    while (mechaInitVar0 > 1) {
        mechaInitVar0 -= 2;
        iterations++;
    }
    mechaInitVar1 += iterations * 56;
    gMechaAngle = (getAngleSin(mechaInitVar1) + 0x1000) / 800 + 4;
    for (i = 0; i < 10; i++) {
        if (loadedMechas[i] != NULL) {
            loadedMechas[i]->m11C_previousTranslation[0] = loadedMechas[i]->m4_bones->m5C_translation[0];
            loadedMechas[i]->m11C_previousTranslation[1] = loadedMechas[i]->m4_bones->m5C_translation[1];
            loadedMechas[i]->m11C_previousTranslation[2] = loadedMechas[i]->m4_bones->m5C_translation[2];
#ifndef PLATFORM_PC
            updateMechAnim(loadedMechas[i], &mechaInitVar2, iterations, oddOrEven, 1);
#else
            {
                /* In the field, processMechaAnimData's saved $s1-$s3 (updateMechAnim's step
                 * count, or this loop's i + 1 for a hidden gear; its update flags; the pool
                 * &mechaInitVar2) are the PS1 stack words 1-3 under the next frame's
                 * wall-slide edge buffer (src/field/f_8008083C.c pcSlideEdgeStack). */
                s32 hasModel = loadedMechas[i]->m0_model != NULL;
                s32 visible = loadedMechas[i]->m34_visible;
                s32 flags = updateMechAnim(loadedMechas[i], &mechaInitVar2, iterations, oddOrEven, 1);

                if (hasModel) {
                    pcSlideEdgeStack[1] = visible ? iterations : i + 1;
                    pcSlideEdgeStack[2] = visible ? flags : 0;
                    pcSlideEdgeStack[3] = 0x801E86A8; /* &mechaInitVar2 (US and JP) */
                }
            }
#endif
        }
    }
    for (i = 0; i < 10; i++) {
        if (loadedMechas[i] != NULL && loadedMechas[i]->m5C_attachMecha < 0xFF) {
            updateMechasBaseMatrixIsAttached(loadedMechas[i]);
        }
    }
    refreshAttachedMechaTransforms(loadedMechas);
    SetColorMatrix(mechaFinalizeVar0);
    for (i = 0; i < 10; i++) {
        if (loadedMechas[i] != NULL) {
            loadedMechas[i]->m128_deltaTranslation[0] =
                loadedMechas[i]->m11C_previousTranslation[0] - loadedMechas[i]->m4_bones->m5C_translation[0];
            loadedMechas[i]->m128_deltaTranslation[1] =
                loadedMechas[i]->m11C_previousTranslation[1] - loadedMechas[i]->m4_bones->m5C_translation[1];
            loadedMechas[i]->m128_deltaTranslation[2] =
                loadedMechas[i]->m11C_previousTranslation[2] - loadedMechas[i]->m4_bones->m5C_translation[2];
#ifdef PLATFORM_PC
            if (loadedMechas[i]->m34_visible != 0) {
                u32 bones = pcHeapAddress(loadedMechas[i]->m4_bones);
                /* submitMechaForRendering spills its bone-array base at
                 * sp+0x78 (US/JP), the field caller's later edge word0. */
                if (bones) pcSlideEdgeStack[0] = bones;
                /* submitMechaForRendering keeps the gear's scale (a halfword) in the PS1
                 * stack word 2 under the edge buffer (pcSlideEdgeStack, above). */
                pcSlideEdgeStack[2] = (pcSlideEdgeStack[2] & 0xFFFF0000) | (u16)loadedMechas[i]->m1C_scale;
            }
#endif
            submitMechaForRendering(loadedMechas[i], pMatrix, pArgs, 1, 1, ot, oddOrEven);
        }
    }
    drawMechaWheelTrail(&mechaInitVar3, pMatrix, iterations, ot, oddOrEven);
}

void freeMechaModule(void) {
    s32 i;

    for (i = 0; i < 10; i++) {
        freeMecha(i);
    }
    freeMechaInitVar2(&mechaInitVar2);
    freeMechaInitVar3(&mechaInitVar3);
}

void freeMecha(s32 index) {
    s32 i;

    if (loadedMechas[index] != NULL) {
        if (loadedMechas[index]->mA8_model != NULL) {
#ifdef PLATFORM_PC
            pcFreeModel(loadedMechas[index]->mA8_model);
#endif
            freeHeap(loadedMechas[index]->mA8_model);
            freeMechaModelBlocks(loadedMechas[index]->m0_model, 1);
        }
        if (loadedMechas[index]->m62_isSeqLoaded) {
            unloadSequence(loadedMechas[index]->mB0_sound->m8_seq);
        }
        if (loadedMechas[index]->mAC_animData != NULL) {
#ifndef PLATFORM_PC
            freeHeap(loadedMechas[index]->mAC_animData);
#else
            pcFreeMechaData2(loadedMechas[index]->mAC_animData);
#endif
        }
        if (index != 8 && index != 9) {
            if (loadedMechas[index]->m4_bones != NULL) {
                mechaOP_8(&mechaInitVar2, loadedMechas[index]->m4_bones);
                freeAnimTracks2(&mechaInitVar2, loadedMechas[index]->m4_bones, 0xFF);
                freeMechaBones(loadedMechas[index]->m4_bones);
                loadedMechas[index]->m0_model = NULL;
                loadedMechas[index]->m4_bones = NULL;
            }
        } else if (loadedMechas[index]->m4_bones != NULL) {
            mechaOP_8(&mechaInitVar2, loadedMechas[index]->m4_bones);
            freeAnimTracks2(&mechaInitVar2, loadedMechas[index]->m4_bones, 0xFF);
            freeHeap(loadedMechas[index]->m4_bones);
        }
        if (loadedMechas[index]->m10C_num110) {
            freeHeap(loadedMechas[index]->m110_trails);
        }
        if (loadedMechas[index]->m10E_num118) {
            for (i = 0; i < loadedMechas[index]->m10E_num118; i++) {
                freeAnimatedEnvTextureTile(&loadedMechas[index]->m118_colorAnims[i]);
            }
            freeHeap(loadedMechas[index]->m118_colorAnims);
        }
        if (loadedMechas[index]->m10D_num114) {
            for (i = 0; i < loadedMechas[index]->m10D_num114; i++) {
                freeMechaCloth(&loadedMechas[index]->m114_cloths[i]);
            }
            freeHeap(loadedMechas[index]->m114_cloths);
        }
        freeHeap(loadedMechas[index]);
        loadedMechas[index] = NULL;
    }
}

/* A script can animate a gear slot that holds none (field 323 started
 * without the party's gears): retail clears the flag before its NULL test,
 * a byte of low BIOS RAM (0x35). The PC skips that store. */
#ifdef PLATFORM_PC
#define CLEAR_EVENT_ANIMATION(m) \
    if ((m) != NULL) (m)->m35_isEventAnimation = 0
#else
#define CLEAR_EVENT_ANIMATION(m) (m)->m35_isEventAnimation = 0
#endif

void mechaPlayAnimation(u16 index, u16 arg1, s32 animationId) {
    mechaPlayAnimationVar1 = index;
    mechaPlayAnimationVar0 = arg1;
    CLEAR_EVENT_ANIMATION(loadedMechas[index]);
    if (loadedMechas[index] != NULL) {
        initMechaAnimation(loadedMechas[index], loadedMechas[index], &mechaInitVar2, animationId);
    }
}

void mechaPlayAnimationFromSource(sLoadedMechas* pOther, u16 index, u16 arg2, s32 animationId) {
    mechaPlayAnimationVar1 = index;
    mechaPlayAnimationVar0 = arg2;
    CLEAR_EVENT_ANIMATION(loadedMechas[index]);
    if (loadedMechas[index] != NULL) {
        if (index != getFirstAnimTargetMecha()) {
            initMechaAnimation(loadedMechas[index], pOther, &mechaInitVar2, animationId);
        }
    }
}

s32 initMechaForBattleSub0(s32 index) {
    sLoadedMechas* pMecha = loadedMechas[index];
    s32 result = 0;

    if (pMecha != NULL) {
        result = (pMecha->m24_dimensions[0] * ((pMecha->m1C_scale * pMecha->m4_bones->m4C_scale[1]) >> 12)) >> 12;
    }
    return result;
}

s32 getMechaColliderRadius(s32 index) {
    s32 a;
    s32 b;
    s32 result = 0;

    if (loadedMechas[index] != NULL) {
        if (loadedMechas[index]->m4A_flags & 8) {
            a = loadedMechas[index]->m24_dimensions[1];
            b = loadedMechas[index]->m4_bones->m4C_scale[0];
        } else {
            a = loadedMechas[index]->m24_dimensions[2];
            b = loadedMechas[index]->m4_bones->m4C_scale[2];
        }
        result = (a * ((loadedMechas[index]->m1C_scale * b) >> 12)) >> 12;
    }
    return result;
}

void initMechSub110(sLoadedMechas* pMecha) {
    sLoadedMechas_110* p;
    s32 i;

    if (pMecha->m10C_num110 != 0) {
        p = allocateHeap(PC_HEAP_SIZED(pMecha->m10C_num110 * sizeof(sLoadedMechas_110), pMecha->m10C_num110 * 0x70), 0);
        for (i = 0; i < pMecha->m10C_num110; i++) {
            p[i].m0_boneIndex = -1;
            p[i].m8_segment = NULL;
        }
        pMecha->m110_trails = p;
    }
}
