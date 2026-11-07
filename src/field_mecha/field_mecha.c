/*
 * Gear (mecha) model / animation / render overlay (disc 10/0C, loaded at
 * 0x801DC000 by field and the gear shop; Noah: fieldMechaOverlay).
 * GCC 2.6.3, -O2 -G0, ASPSX 2.34 with the divide checks kept (maspsx
 * --expand-div). initMechaCloth and processMechaAnimData are still assembly.
 */
#include "common.h"
#include "pc_retail_heap.h"
#include "psyq.h"
#include "gte.h"
#include "main/memory.h"
#include "main/sprite.h"
#include "main/sound.h"
#include "field_mecha/mecha.h"
#include "pc_convert.h"
#include "pc_interpolation.h"

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

sMechaInitVar4* sMechaModel_init(sModel* model, sMechaInitVar4* out) {
    u32 n;
    u32 i;

    resetMemoryAllocStats(4, 0);
    n = fieldModelRelocation(model);
    out->m0_blocks = allocateHeap(PC_HEAP_SIZED(n * sizeof(sModelBlock*), n * 0x4), 0);
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

/* Noah: processMechaMesh. Builds the bone array (root + one bone per
 * hierarchy entry) and the primitive buffers of each bone's geometry. */
sMechaBone* processMechaMesh(sMechaInitVar4* model, sMechaDataTable1_C* entries, s32 mode, s32 useTpageAndClut,
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
    bones = allocateHeap(PC_HEAP_SIZED(n * sizeof(sMechaBone), n * 0x7c), 0);
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
                freeMechaBones(bones);
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

/* Noah: updateMechAnimSub1. Root: final matrix from the root rotation and
 * translation, local matrix scaled; bones: rotation matrices rebuilt when
 * flagged, then the hierarchy composed. Returns the bone count. */
s32 updateMechAnimSub1(sMechaBone* bones, s32 scale) {
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

/* Noah: updateMechAnimSub2. Like updateMechAnimSub1, but the rotation and
 * translation flags propagate from the parent and each rebuilt bone matrix
 * is scaled by its own scale and unscaled by its parent's. Returns the bone
 * count. */
s32 updateMechAnimSub2(sMechaBone* bones, s32 scale) {
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

void func_801DCC34(void) {
}

/* Submit every bone with geometry (no per-bone enable check); the matrices
 * go through the scratchpad. Noah: close to submitMechaForRendering. */
void submitAllMechaBonesForRendering(sMechaInitVar4* model, sMechaBone* bones, MATRIX* m0, MATRIX* m1, s32 renderMode,
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
#ifdef PLATFORM_PC
            pcInterpolationObjectType(bones, model->m0_blocks[bones->m8_geometryId], PC_INTERP_MECHA);
#endif
            submitModelForRendering(model->m0_blocks[bones->m8_geometryId], bones->m68_prims[oddOrEven], ot,
                                    renderMode);
#ifdef PLATFORM_PC
            pcInterpolationObject(0, 0);
#endif
        }
    }
}

/* Free a bone array and its primitive buffers. */
void freeMechaBones(sMechaBone* bones) {
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

void freeMechaModelBlocks(sMechaInitVar4* p, s32 freeBlocks) {
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

/* Noah: submitMechaForRendering. Shadow quad under the root (unless
 * m4A_flags & 1), every enabled bone's model, then the attached effects
 * (m114 cloth parts, m118 CLUT animations, m110 trails). */
void submitMechaForRendering(sLoadedMechas* mecha, MATRIX* m1, MATRIX* m2, s32 renderMode, s32 arg4, OTag* ot,
                   s32 oddOrEven) {
    SVECTOR sv;
    VECTOR v1;
    VECTOR v2;
    s32 z;
    s32 z2;
    sMechaBone* bones;
    u16 scale;
    MATRIX* sp20 = SCRATCH_MATRIX_20;
    MATRIX* sp40 = SCRATCH_MATRIX_40;
    MATRIX* sp00 = SCRATCH_MATRIX_00;
    sMechaBone* b;
    sMechaInitVar4* model;
    u32 numBones;
    s32 i;
    s32 j;

    if (mecha->m34_visible != 0) {
        b = mecha->m4_bones;
        model = mecha->m0_model;
        scale = mecha->m1C_scale;
        numBones = b->mA_numBones;
        bones = b;
        CompMatrix(m1, &b->mC_localMatrix, sp40);
        if (!(mecha->m4A_flags & 1)) {
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
#ifdef PLATFORM_PC
            pcInterpolationKeyType(mecha, mecha->m0_model, 0x10000, &mecha->mB8_Polys[oddOrEven], PC_INTERP_SHADOW);
#endif
            addPrim(ot + z, &mecha->mB8_Polys[oddOrEven]);
            if (z > 0x2D8) {
                z = 0x2D8;
            }
            mecha->m39_shadowShade = 0x6B - z / 8;
        }
        MulMatrix0(m2, &b->m2C_boneFinalMatrix, sp20);
        for (i = 1, b++; i < numBones; i++, b++) {
            if (b->m8_geometryId != 0xFFFF && b->m7_isEnabled) {
                MulMatrix0(sp20, &b->m2C_boneFinalMatrix, sp00);
                SetLightMatrix(sp00);
                CompMatrix(sp40, &b->m2C_boneFinalMatrix, sp00);
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
#ifdef PLATFORM_PC
                pcInterpolationObjectType(b, model->m0_blocks[b->m8_geometryId], PC_INTERP_MECHA);
#endif
                submitModelForRendering(model->m0_blocks[b->m8_geometryId], b->m68_prims[oddOrEven], ot,
                                        renderMode);
#ifdef PLATFORM_PC
                pcInterpolationObject(0, 0);
#endif
            }
        }
        {
            sLoadedMechas_114* e = mecha->m114_cloths;
            VECTOR out;
            SVECTOR pull;

            for (i = 0; i < mecha->m10D_num114; i++) {
                if (e->m0_boneIndex >= 0) {
                    sMechaClothCollider* coll;

                    pull.vx = (-gMechaAngle * getAngleSin(bones->m54_rotationAngles.vy + 0x400)) / 4096;
                    pull.vz = (gMechaAngle * getAngleCos(bones->m54_rotationAngles.vy + 0x400)) / 4096;
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
                    updateAndRenderMechaCloth(e, &pull, m1, ot, oddOrEven, scale, mecha->m60_groundY);
                }
                e++;
            }
        }
        {
            sLoadedMechas_118* e = mecha->m118_colorAnims;

            for (i = 0; i < mecha->m10E_num118; i++, e++) {
                updateAnimatedEnvTextureTile(e, arg4);
            }
        }
        {
            sLoadedMechas_110* tr = mecha->m110_trails;

            for (i = 0; i < mecha->m10C_num110; i++) {
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
                            tr->m8_segment = allocateMechaTrailSegment(tr->m4_pool, tr->m3_semiTrans);
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
                            tr->m8_segment = allocateMechaTrailSegment(tr->m4_pool, tr->m3_semiTrans);
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

/* Noah: updateMechAnimSub3. Advances every bone's rotation, translation and
 * scale animation tracks by one frame; returns flags for the tracks owned
 * by `id` (1/2/4: running/finished/looped) and for all tracks (0x100/0x200/
 * 0x400). */
u32 updateMechAnimSub3(sMechaInitVar2* pool, sMechaBone* bones, s32 id, s32 speed) {
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
                    releaseMechaAnimTrack(pool, t);
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
                    releaseMechaAnimTrack(pool, t);
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
                    releaseMechaAnimTrack(pool, t);
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

/* Noah: mechaSetPoseFromKeyframe. Copies a keyframe's rotations and
 * translations into the bones (skipping bones whose animation track is
 * locked) and flags the changed ones. Returns the bone count - 1.
 * The keyframe comes in as its data pointer (the parameter register walks
 * the data); the loop counter first holds the header's m6. */
s32 mechaSetPoseFromKeyframe(sMechaBone* bones, s16* p) {
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

s32 mechaOP_13(sMechaInitVar2* arg0, sMechaBone* bones, sMechaDataTable2_4_4* kf, s32 len, u32 interpType,
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
                    track = allocateAnimTrack(arg0);
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
            releaseMechaAnimTrack(arg0, bones->m70_animTracks[0]);
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
                    track = allocateAnimTrack(arg0);
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
            releaseMechaAnimTrack(arg0, bones->m70_animTracks[1]);
            bones->m70_animTracks[1] = NULL;
        }
    }
    return numBones;
}

void mechaOP_A(sMechaInitVar2* arg0, sMechaBone* arg1, s32 arg2, s32 arg3) {
    if (arg2 < arg1->mA_numBones) {
        arg1 = &arg1[arg2];
        if (arg1->m70_animTracks[0] != NULL && (arg3 & 1)) {
            releaseMechaAnimTrack(arg0, arg1->m70_animTracks[0]);
            arg1->m70_animTracks[0] = NULL;
        }
        if (arg1->m70_animTracks[1] != NULL && (arg3 & 2)) {
            releaseMechaAnimTrack(arg0, arg1->m70_animTracks[1]);
            arg1->m70_animTracks[1] = NULL;
        }
        if (arg1->m70_animTracks[2] != NULL && (arg3 & 4)) {
            releaseMechaAnimTrack(arg0, arg1->m70_animTracks[2]);
            arg1->m70_animTracks[2] = NULL;
        }
    }
}

sMechaInitVar2* initMechaInitVar2(sMechaInitVar2* arg0, s32 count) {
    if (count > 0) {
        arg0->m6_count = count;
        resetMemoryAllocStats(4, 0);
        arg0->m0_tracks = allocateHeap(PC_HEAP_SIZED(count * sizeof(sMechaInitVar2Sub), count * 0x14), 0);
        if (arg0->m0_tracks == NULL) {
            return NULL;
        }
    } else {
        return NULL;
    }
    initMechaInitVar2Sub0(arg0);
    return arg0;
}

void freeMechaInitVar2(sMechaInitVar2* arg0) {
    arg0->m4_firstFree = 0;
    if (arg0->m0_tracks != NULL) {
        freeHeap(arg0->m0_tracks);
    }
    arg0->m0_tracks = NULL;
}

void initMechaInitVar2Sub0(sMechaInitVar2* arg0) {
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

sMechaInitVar2Sub* allocateAnimTrack(sMechaInitVar2* arg0) {
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

s32 releaseMechaAnimTrack(sMechaInitVar2* arg0, sMechaInitVar2Sub* arg1) {
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

s32 mechaOP_11(sMechaInitVar2* arg0, sMechaBone* bones, sMechaDataTable2_4_4* kf, u32 isLooping, s32 tag) {
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
        mechaOP_8(arg0, bones);
        mechaSetPoseFromKeyframe(bones, (s16*)kf);
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
                track = allocateAnimTrack(arg0);
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
                releaseMechaAnimTrack(arg0, bones->m70_animTracks[0]);
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
                track = allocateAnimTrack(arg0);
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
                releaseMechaAnimTrack(arg0, bones->m70_animTracks[1]);
                bones->m70_animTracks[1] = NULL;
            }
        }
    next1:
        hdr += 2;
        bones++;
    }
    return 0;
}

s32 mechaSetupKeyframeTracksWithPose(sMechaInitVar2* arg0, sMechaBone* bones, sMechaDataTable2_4_4* kf, u32 isLooping, s32 tag) {
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
        mechaOP_8(arg0, bones);
        mechaSetPoseFromKeyframe(bones, (s16*)kf);
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
                track = allocateAnimTrack(arg0);
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
                track = allocateAnimTrack(arg0);
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

void mechaOP_8(sMechaInitVar2* arg0, sMechaBone* arg1) {
    s32 i;
    s32 n;
    sMechaBone* p;

    n = arg1->mA_numBones;
    p = arg1;
    for (i = 0; i < n; i++) {
        if (p->m70_animTracks[0] != NULL && p->m70_animTracks[0]->m3_tag != 0xFF) {
            releaseMechaAnimTrack(arg0, p->m70_animTracks[0]);
            p->m70_animTracks[0] = NULL;
        }
        if (p->m70_animTracks[1] != NULL && p->m70_animTracks[1]->m3_tag != 0xFF) {
            releaseMechaAnimTrack(arg0, p->m70_animTracks[1]);
            p->m70_animTracks[1] = NULL;
        }
        if (p->m70_animTracks[2] != NULL && p->m70_animTracks[2]->m3_tag != 0xFF) {
            releaseMechaAnimTrack(arg0, p->m70_animTracks[2]);
            p->m70_animTracks[2] = NULL;
        }
        p++;
    }
}

void freeAnimTracks2(sMechaInitVar2* arg0, sMechaBone* arg1, u8 arg2) {
    s32 i;
    s32 n;
    sMechaBone* p;

    n = arg1->mA_numBones;
    p = arg1;
    for (i = 0; i < n; i++) {
        if (p->m70_animTracks[0] != NULL && p->m70_animTracks[0]->m3_tag == arg2) {
            releaseMechaAnimTrack(arg0, p->m70_animTracks[0]);
            p->m70_animTracks[0] = NULL;
        }
        if (p->m70_animTracks[1] != NULL && p->m70_animTracks[1]->m3_tag == arg2) {
            releaseMechaAnimTrack(arg0, p->m70_animTracks[1]);
            p->m70_animTracks[1] = NULL;
        }
        if (p->m70_animTracks[2] != NULL && p->m70_animTracks[2]->m3_tag == arg2) {
            releaseMechaAnimTrack(arg0, p->m70_animTracks[2]);
            p->m70_animTracks[2] = NULL;
        }
        p++;
    }
}

sMechaInitVar3* initMechaInitVar3(sMechaInitVar3* arg0, s32 count) {
    resetMemoryAllocStats(4, 0);
    arg0->m4_count = count;
    arg0->m6_next = 0;
    arg0->m0_entries = allocateHeap(PC_HEAP_SIZED((count + 1) * sizeof(sMechaInitVar3Sub), (count + 1) * 0x7c), 0);
    if (arg0->m0_entries != NULL) {
        initMechaInitVar3Sub(arg0);
    } else {
        return NULL;
    }
    return arg0;
}

void freeMechaInitVar3(sMechaInitVar3* arg0) {
    arg0->m4_count = 0;
    arg0->m6_next = 0;
    if (arg0->m0_entries != NULL) {
        freeHeap(arg0->m0_entries);
    }
    arg0->m0_entries = NULL;
}

void initMechaInitVar3Sub(sMechaInitVar3* arg0) {
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
            p->m2C_polys[j].tpage = GetTPage(0, 1, 0x340, 0x100);
            p->m2C_polys[j].u2 = 0xF;
            p->m2C_polys[j].u0 = 0;
            p->m2C_polys[j].v0 = 0xBD;
            p->m2C_polys[j].u1 = 0;
            p->m2C_polys[j].v1 = 0xBD;
            p->m2C_polys[j].v2 = 0xBD;
            p->m2C_polys[j].u3 = 0xF;
            p->m2C_polys[j].v3 = 0xBD;
        }
        p++;
    }
}

sMechaInitVar3Sub* allocateMechaTrailSegment(sMechaInitVar3* arg0, s16 arg1) {
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

s32 releaseMechaTrailSegment(sMechaInitVar3* arg0, sMechaInitVar3Sub* arg1) {
    s32 idx;

    idx = (s32)(((uptr)arg1 - (uptr)arg0->m0_entries) / sizeof(sMechaInitVar3Sub));
    if (idx <= arg0->m6_next) {
        arg0->m6_next = idx;
    }
    arg1->m16_age = -1;
    return idx;
}

void drawMechaWheelTrail(sMechaInitVar3* arg0, MATRIX* m, s32 speed, OTag* ot, s32 oddOrEven) {
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
            releaseMechaTrailSegment(arg0, p);
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
            addPrim(ot + z, &p->m2C_polys[oddOrEven]);
        }
        p->m16_age += speed;
        p->m20_rgb[0] -= p->m26_rgbStep[0] * speed;
        p->m20_rgb[1] -= p->m26_rgbStep[1] * speed;
        p->m20_rgb[2] -= p->m26_rgbStep[2] * speed;
    }
}

s32 initMechaTrailEmitter(sLoadedMechas_110* p, void* arg1, s16 arg2, u8 arg3, s16 arg4, s16 arg5, u8 arg6, u8 arg7, u8 arg8,
                  u8 arg9, u8 argA, u8 argB, s16 argC, s16 argD, s16 argE, s16 argF, s16 arg10, s16 arg11, s16 arg12) {
    s32 dx; /* unused */
    s32 dy;
    s32 dz;

    if (p == NULL) {
        IMPLICIT_RETURN(0); /* sic: bare return in a function that returns 0 below */
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
    p->m8_segment = NULL;
    return 0;
}

void stopMechaTrailEmitter(sLoadedMechas_110* p, s32 oddOrEven) {
    p->m0_boneIndex = -1;
}

s16 envTileStepSine(s16 arg0, s16 arg1, s16 arg2) {
    return arg2 + (getAngleSin(arg0) + 0x1000) / arg1;
}

s32 envTileStepRampUp(s16 arg0, s16 arg1, s16 arg2) {
    s16 r;

    r = arg2 + arg0 / arg1;
    if (r > 0x20) {
        return -1;
    }
    return r;
}

s16 envTileStepRampDown(s16 arg0, s16 arg1, s16 arg2) {
    return arg2 - arg0 / arg1;
}

s16 envTileStepRampDownClamped(s16 arg0, s16 arg1, s16 arg2) {
    s16 r;

    r = 0x20 - arg0 / arg1;
    if (r < arg2) {
        r = arg2;
    }
    return r;
}

sLoadedMechas_118* initAnimatedEnvTextureTile(sLoadedMechas_118* p, sLoadedMechas_118* parent, u16 mode, u16 flags,
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
            p->mC_outputColors = allocateHeap(PC_HEAP_BYTES(w * h * 2), 0);
        }
        if (flags & 0x200) {
            p->m8_blendColors = allocateHeap(PC_HEAP_BYTES(w * h * 2), 0);
        }
        if (flags & 0x400) {
            p->m4_baseColors = allocateHeap(PC_HEAP_BYTES(w * h * 2), 0);
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
            p->mC_outputColors = allocateHeap(PC_HEAP_BYTES(w * h * 2), 0);
        }
        if (flags & 0x200) {
            p->m8_blendColors = allocateHeap(PC_HEAP_BYTES(w * h * 2), 0);
        }
        if (flags & 0x400) {
            p->m4_baseColors = allocateHeap(PC_HEAP_BYTES(w * h * 2), 0);
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

s16 updateAnimatedEnvTextureTile(sLoadedMechas_118* arg0, s32 arg1) {
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
        freeAnimatedEnvTextureTile(arg0);
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
        fadeEnvTileColorMatrix(arg0, v);
        break;
    case 5:
        lerpEnvTileColorMatrix(arg0, v);
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
                        *(dstBuf + dst.x + x + (dst.y + y) * parent->m28_rect.w) =
                            *(srcBuf + src.x + x + (src.y + y) * arg0->m28_rect.w);
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

void freeAnimatedEnvTextureTile(sLoadedMechas_118* arg0) {
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

void fadeEnvTileColorMatrix(sLoadedMechas_118* arg0, s16 arg1) {
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

void lerpEnvTileColorMatrix(sLoadedMechas_118* arg0, s16 arg1) {
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

void refreshAttachedMechaTransforms(sLoadedMechas** mechas) {
    MATRIX* m;
    VECTOR out;
    sMechaBone* bones;
    s32 i;

    m = (MATRIX*)PSX_SCRATCH(0x1F800000);
    for (i = 0; i < 2; i++) {
        if (mechaInitVar5[i].m6_isActive != 0) {
            if (mechaInitVar5[i].m10_mechaIndex >= 0 && mechas[mechaInitVar5[i].m10_mechaIndex] != NULL) {
                bones = mechas[mechaInitVar5[i].m10_mechaIndex]->m4_bones;
                CompMatrix(&bones->mC_localMatrix, &bones[mechaInitVar5[i].m12_boneIndex + 1].m2C_boneFinalMatrix, m);
                SetRotMatrix(m);
                SetTransMatrix(m);
                gte_ldv0(&mechaInitVar5[i].m8_localPosition);
                gte_rt();
                gte_stlvnl(&out);
                mechaInitVar5[i].m0_worldPosition[0] = out.vx;
                mechaInitVar5[i].m0_worldPosition[1] = out.vy;
                mechaInitVar5[i].m0_worldPosition[2] = out.vz;
            } else {
                mechaInitVar5[i].m0_worldPosition[0] = mechaInitVar5[i].m8_localPosition.vx;
                mechaInitVar5[i].m0_worldPosition[1] = mechaInitVar5[i].m8_localPosition.vy;
                mechaInitVar5[i].m0_worldPosition[2] = mechaInitVar5[i].m8_localPosition.vz;
            }
        }
    }
}

/* 0x801E1A14-0x801E34BC: cloth parts, see fm_801E1A14.c (BG6) */
