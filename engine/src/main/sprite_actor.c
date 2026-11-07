/*
 * Sprite actor helpers: vector setters, the animation stack, actor state
 * save/restore, movement, save-point callbacks, per-subgroup transforms.
 *
 * File 0x80021AD8-0x800248D4 (main executable); both ends are fixed by the
 * read-only data layout (see config/us/main.yaml). Built with GCC 2.7.2-cdk.
 */
#include "common.h"
#include "pc_retail_heap.h"
#include "psyq.h"
#include "main/memory.h"
#include "main/task.h"
#include "main/sprite.h"
#include "stack.h"

/* Whole-word view of the bitfield words at 0x3C and 0xA8..0xB3. */
/* The pointers before them are 8 bytes on the PC: the word at 0x3C is the one
 * before m40_draw, those from 0xA8 follow mA0_destination. */
#define SPRITE_WORD(actor, offset) \
    (*(u32*)((offset) == 0x3C ? (u8*)&(actor)->m40_draw - 4 : (u8*)&(actor)->mA0_destination + 8 + ((offset) - 0xA8)))
typedef char spriteWordCheck[__builtin_offsetof(SpriteActorCore, mAF_animationId) ==
                             __builtin_offsetof(SpriteActorCore, mA0_destination) + 8 + 7 ? 1 : -1];

s32 vec2dRatan2(Vec2s16 a, Vec2s16 b);
void spriteActorSetDirection(SpriteActorCore* actor, s32 direction);
void setSVector3(SVECTOR* v, s32 x, s32 y, s32 z);
void setVector3(VECTOR* v, s32 x, s32 y, s32 z);
void setTransparencyMode(SpriteActorCore* actor, u32 mode);
s32 addClamp255(s32 value, s32 delta);
SpriteActorCore* spriteBytecode2ExtendedE0(SpriteActorCore* actor, u16* anim, SpriteVramData* vram);
void pushByteOnAnimationStack(SpriteActorCore* actor, u8 value);
void battleCameraControlSpriteInit(SpriteTask* task);
void savePointCallback8Counted(TaskHeader* task);
void spriteBytecode2ExtendedE0_Sub0Sub0(TaskHeader* task, s32 mode);
extern u8 sizePerBytecodeTable[];
void executeSpriteBytecode2Sub1(SpriteActorCore* actor);
void pushU24OnAnimationStack(SpriteActorCore* actor, s32 value);
void executeSpriteBytecode(SpriteActorCore* actor, u8* stop, s32 stopStep);
/* Defined here (small, $gp-relative). Noah: loadVramSpriteParam. */
s32 loadVramSpriteParam = 0;
/* Unreferenced in main. */
u32 D_800591BC = 0xFE000400;
s32 D_800591C0 = 0;
extern s32 battleDefaultEntityScale;
extern u8 spriteBytecode2ExtendedE0_Var0;
extern SpriteActorCore* processBattleAnimationSub0_var1;
s32 initFieldEntitySub4Sub4(void* spriteData);
void setGraphicEntityScale(SpriteActorCore* actor, s32 scale);
void setAnimationBundle(SpriteActorCore* actor, AnimationBundle* bundle);
void setSpriteActorAngle(SpriteActorCore* actor, s16 angle);
SpriteTask* allocateSavePointMeshData(TaskHeader* parent, s32 extraSize);
s32 getSavePointCreationMode1(u16* p);
s32 getModeForSavePointMesh(u32 mode);
void initsFieldEntitySub4_B4(SpriteObject* obj);
void spriteBytecode2ExtendedE0_Sub0(SpriteTask* task);
SpriteActor* initializeSpriteActor(SpriteActor* arg0, AnimationBundle* bundle, s16 clutX, s16 clutY, s16 vramX, s16 vramY, s16 arg6);
SpriteActor* createSpriteActor(AnimationBundle* bundle, s16 clutX, s16 clutY, s16 vramX, s16 vramY, s16 arg5);
extern SpriteVramData createSavePointMeshData_mode6;
extern SpriteVramData createSavePointMeshData_mode5;
void spriteCallback_render2_updateMatrix(SpriteActorCore* actor);
void createRotationMatrix(SVECTOR* rot, MATRIX* m);
/* Defined here (small, $gp-relative). */
static s32 D_800592EC;
static RECT* currentNpcSpriteUploadRect;
static u16* currentNpcSpriteUploadDataPtr;
extern s32 fieldDrawEnvsInitialized;
extern u8 isBattleOverlayLoaded;
extern u8 battleScriptLoaderTaskCreationInProgress;
extern u8 initFieldVar4;
extern u8 initFieldVar5;
void registerSpriteCallback2(TaskHeader* task, TaskHeader* task2);
void registerSpriteCallback2Sub0(TaskHeader* task);
void allocateSavePointMeshDataSub0(TaskHeader* parent, TaskHeader* task);
void allocateSavePointMeshDataSub0_callback(TaskHeader* task);
void registerSpriteCallback2_2(TaskHeader* task);
void detachChildSpriteActors(TaskHeader* task);
void addToSpriteTransferList(SpriteActorCore* actor, s32 frame);
void savePointCallbackC_sub1(SpriteActorCore* actor);
s32 isVramPreBacked(void* spriteData);
void setupSpriteObjectMatrix(SpriteActorCore* actor);
void transfertNpcSpriteSheetElement(void);
void initFieldEntitySub4Sub1(SpriteActorCore* actor);
void spriteActorSetPlayingAnimation(SpriteActorCore* actor, s32 animationId);
void executeSpriteBytecode2(SpriteActorCore* actor);
void clearShapeTransferEntry(void* p);
s32 getAngleCos(s32 angle);
s32 getAngleSin(s32 angle);
void sampleBattleTerrainHeight(SpriteActorCore* actor);
void spriteActorSaveState(SpriteActorCore* actor, SpriteActorState* st);
void initFieldEntitySub4Sub5Sub0(SpriteVramData* vram, AnimationBundle* bundle, Vec2s16 vramLocation, Vec2s16 clut, s32 unused);
void computeStepFromMoveSpeed(SpriteActorCore* actor);
void applySpriteVerticalMovement(SpriteActorCore* actor);
s32 modulateSpeed(SpriteActorCore* actor, s32 value);
void updateSpriteMovement(SpriteActorCore* actor);
void savePointCallback8(TaskHeader* task);
void savePointCallbackC(SpriteTask* task);
void OP_INIT_ENTITY_SCRIPT_sub0Sub9(SpriteActorCore* actor);
void resetPerSubgroupTransforms(SpriteActorCore* actor);
void updateAllSubsprites(SpriteActorCore* actor);

s32 addClamp255(s32 value, s32 delta) {
    value += delta;
    if (value >= 0x100) {
        value = 0xFF;
    } else if (value < 0) {
        value = 0;
    }
    return value;
}

void setSVector3(SVECTOR* v, s32 x, s32 y, s32 z) {
    v->vx = x;
    v->vy = y;
    v->vz = z;
}

void setVector3(VECTOR* v, s32 x, s32 y, s32 z) {
    v->vx = x;
    v->vy = y;
    v->vz = z;
}

void copySVector3(SVECTOR* dst, SVECTOR* src) {
    dst->vx = src->vx;
    dst->vy = src->vy;
    dst->vz = src->vz;
}

void copyVector3(VECTOR* dst, VECTOR* src) {
    dst->vx = src->vx;
    dst->vy = src->vy;
    dst->vz = src->vz;
}

void spriteActorSetRawTexture(SpriteActorCore* actor) {
    actor->m2B_code |= 1;
    updateAllSubsprites(actor);
}

void spriteActorSetColor(SpriteActorCore* actor, s32 r, s32 g, s32 b) {
    actor->m28_r = r;
    actor->m29_g = g;
    actor->m2A_b = b;
    actor->m2B_code &= ~1;
    updateAllSubsprites(actor);
}

void spriteActorSetTimeScale(SpriteActorCore* actor, s32 timeScale) {
    actor->mAC_x7_timeScale = timeScale;
}

void spriteActorSetSpecialAnimation(SpriteActorCore* actor, void* animation) {
    actor->m4C_specialAnimation = animation;
}

void spriteActorSetCallback(SpriteActorCore* actor, void (*callback)(SpriteActorCore*)) {
    actor->m68 = callback;
}

void OP_INIT_ENTITY_SCRIPT_sub0Sub5(SpriteActorCore* actor, s32 value) {
    actor->m40_draw.raw = (actor->m40_draw.raw & ~0x1F00) | ((value & 0x1F) << 8);
}

u8 popByteFromAnimationStack(SpriteActorCore* actor) {
    u8 value = actor->m8E_stack[actor->m8C_stackPosition];
    actor->m8C_stackPosition++;
    return value;
}

s16 popS16FromAnimationStack(SpriteActorCore* actor) {
    s16 value = actor->m8E_stack[actor->m8C_stackPosition]
              + (actor->m8E_stack[actor->m8C_stackPosition + 1] << 8);
    actor->m8C_stackPosition += 2;
    return value;
}

s32 popU24FromAnimationStack(SpriteActorCore* actor) {
    s32 value = actor->m8E_stack[actor->m8C_stackPosition]
              + (actor->m8E_stack[actor->m8C_stackPosition + 1] << 8)
              + (actor->m8E_stack[actor->m8C_stackPosition + 2] << 16);
    actor->m8C_stackPosition += 3;
    return value;
}

void pushByteOnAnimationStack(SpriteActorCore* actor, u8 value) {
    actor->m8C_stackPosition--;
    actor->m8E_stack[actor->m8C_stackPosition] = value;
}

void pushU16OnAnimationStack(SpriteActorCore* actor, u16 value) {
    actor->m8C_stackPosition -= 2;
    actor->m8E_stack[actor->m8C_stackPosition] = value;
    actor->m8E_stack[actor->m8C_stackPosition + 1] = value >> 8;
}

void pushU24OnAnimationStack(SpriteActorCore* actor, s32 value) {
    actor->m8C_stackPosition -= 3;
    actor->m8E_stack[actor->m8C_stackPosition] = value;
    actor->m8E_stack[actor->m8C_stackPosition + 1] = value >> 8;
    actor->m8E_stack[actor->m8C_stackPosition + 2] = value >> 16;
}

void setFixedXZ(FP_VEC3* v, s32 x, s32 z) {
    v->vz = z << 16;
    v->vx = x << 16;
}

void spriteActorRestoreState(SpriteActorCore* actor, SpriteActorState* st) {
    s32 saved = fieldDrawEnvsInitialized;
    fieldDrawEnvsInitialized = 0;
    actor->m80_angle = st->m10_angle;
    actor->mAF_animationId = st->m14_animationId;
    actor->mB0_x0_animationId = st->m16_defaultAnimationId;
    actor->m20_object->m6_scale[0] = st->m24_scale[0];
    actor->m20_object->m6_scale[1] = st->m24_scale[1];
    actor->m20_object->m6_scale[2] = st->m24_scale[2];
    actor->m82_entityScale = st->m2C_entityScale;
    actor->m2C_scale = st->m2A_scale;
    spriteActorSetPlayingAnimation(actor, (s8)actor->mAF_animationId);
    while (actor->mA8_x16_bytecodeStep != st->m18_bytecodeStep) {
        OP_INIT_ENTITY_SCRIPT_sub0Sub9(actor);
        actor->m0_position.vx += actor->mC_step.vx;
        actor->m0_position.vz += actor->mC_step.vz;
        actor->m0_position.vy += actor->mC_step.vy;
        actor->mC_step.vy += actor->m1C_gravity;
    }
    actor->m0_position.vx = st->m0_position.vx;
    actor->m0_position.vy = st->m0_position.vy;
    actor->m0_position.vz = st->m0_position.vz;
    actor->m7C_extra->m0 = st->m1C;
    actor->m7C_extra->m4_gravityOverride = st->m20_gravityOverride;
    fieldDrawEnvsInitialized = saved;
}

void spriteActorSaveState(SpriteActorCore* actor, SpriteActorState* st) {
    st->m0_position.vx = actor->m0_position.vx;
    st->m0_position.vy = actor->m0_position.vy;
    st->m0_position.vz = actor->m0_position.vz;
    st->m10_angle = actor->m80_angle;
    st->m12_frame = actor->mA8_xB_frame;
    st->m14_animationId = (s8)actor->mAF_animationId;
    st->m16_defaultAnimationId = actor->mB0_x0_animationId;
    st->m18_bytecodeStep = actor->mA8_x16_bytecodeStep;
    st->m1C = actor->m7C_extra->m0;
    st->m20_gravityOverride = actor->m7C_extra->m4_gravityOverride;
    st->m24_scale[0] = actor->m20_object->m6_scale[0];
    st->m24_scale[1] = actor->m20_object->m6_scale[1];
    st->m24_scale[2] = actor->m20_object->m6_scale[2];
    st->m2C_entityScale = actor->m82_entityScale;
    st->m2A_scale = actor->m2C_scale;
}

void spriteActorSetDefaultAnimationId(SpriteActorCore* actor, s32 animationId) {
    actor->mB0_x0_animationId = animationId;
}

void spriteActorSetMoveSpeed(SpriteActorCore* actor, s32 speed) {
    actor->m18_moveSpeed = speed;
    computeStepFromMoveSpeed(actor);
}

void spriteActorSetDirection(SpriteActorCore* actor, s32 direction) {
    actor->m32_direction = direction;
    computeStepFromMoveSpeed(actor);
}

void setGraphicEntityScale(SpriteActorCore* actor, s32 scale) {
    SpriteObject* mesh = actor->m20_object;
    if (mesh != NULL) {
        mesh->m6_scale[0] = mesh->m6_scale[1] = mesh->m6_scale[2] = actor->m2C_scale = scale;
        actor->m3C_x1C_matrixDirty = 1;
    }
}

void spriteCallback_render2_updateMatrix(SpriteActorCore* actor) {
    if (actor->m3C_x1C_matrixDirty) {
        setupSpriteObjectMatrix(actor);
        actor->m3C_x1C_matrixDirty = 0;
    }
}

void setupSpriteObjectMatrix(SpriteActorCore* actor) {
    VECTOR unused;
    VECTOR scale;

    if (!(actor->m40_draw.raw & 1)) {
        scale.vx = actor->m20_object->m6_scale[0];
        scale.vy = actor->m20_object->m6_scale[1];
        scale.vz = actor->m20_object->m6_scale[2];
        createRotationMatrix((SVECTOR*)actor->m20_object->m0_rotation, &actor->m20_object->mC_matrix);
        ScaleMatrixL(&actor->m20_object->mC_matrix, &scale);
    } else {
        VECTOR scale2;
        MATRIX rotation;
        MATRIX scaleMatrix = {{{0x1000, 0, 0}, {0, 0x1000, 0}, {0, 0, 0x1000}}, {0, 0, 0}};
        scale2.vx = actor->m20_object->m6_scale[0];
        scale2.vy = actor->m20_object->m6_scale[1];
        scale2.vz = actor->m20_object->m6_scale[2];
        ScaleMatrixL(&scaleMatrix, &scale2);
        createRotationMatrix((SVECTOR*)actor->m20_object->m0_rotation, &rotation);
        MulMatrix0(&rotation, &scaleMatrix, &actor->m20_object->mC_matrix);
    }
    if ((u16)actor->m3A_scaleModifier != 0) {
        scale.vx = (u16)actor->m3A_scaleModifier >> 1;
        scale.vy = (u16)actor->m3A_scaleModifier >> 1;
        scale.vz = (u16)actor->m3A_scaleModifier >> 1;
        ScaleMatrix(&actor->m20_object->mC_matrix, &scale);
    }
}

void initFieldEntitySub4Sub5Sub0(SpriteVramData* vram, AnimationBundle* bundle, Vec2s16 vramLocation, Vec2s16 clut, s32 unused) {
    u8 x;
    vram->m4_vramLocation = vramLocation;
    vram->m8_clut = clut;
    vram->mC_clutData = (void*)(bundle->mC_offset + (sptr)bundle);
    vram->m0_spriteData = (uptr)(bundle->m8_spriteDataOffset + (sptr)bundle);
    vram->m10_startOfAnimationContainer = (uptr)(bundle->m4_animationsOffset + (sptr)bundle);
    initFieldVar4 = 0;
    if (isBattleOverlayLoaded != 0) {
        x = (*(u16*)vram->m10_startOfAnimationContainer >> 6) & 0x3F;
        if (x != 0) {
            initFieldVar5 = x;
        }
    }
}

void setAnimationBundle(SpriteActorCore* actor, AnimationBundle* bundle) {
    SpriteVramData* vram = actor->m24_vramData;
    Vec2s16 unused; /* only reserves the 8 extra bytes of retail's frame */
    if (bundle != NULL) {
        if (bundle != actor->m44_currentAnimationBundle) {
            initFieldEntitySub4Sub5Sub0(vram, bundle, vram->m4_vramLocation, vram->m8_clut, actor->m3C_x14);
            actor->m44_currentAnimationBundle = bundle;
            actor->m3C_x1E_bundleChanged = 1;
        }
        if (isBattleOverlayLoaded != 0) {
            if (isVramPreBacked((void*)vram->m0_spriteData) == 0) {
                vram->m4_vramLocation.vy = 0x100;
                vram->m4_vramLocation.vx = 0x300;
            } else {
                vram->m4_vramLocation = actor->m7C_extra->mE_vramLocation;
            }
        }
    }
}

void setSpriteActorAngle(SpriteActorCore* actor, s16 angle) {
    s32 prevDir;

    actor->m80_angle = angle;
    prevDir = actor->mA8_x11;
    if ((angle + 0x400) & 1) {
        actor->mAC_x2_facing = 1;
    } else {
        actor->mAC_x2_facing = 0;
    }
    if (actor->m48_defaultAnimationBundle == NULL) {
        return;
    }
    switch (actor->mA8_x14) {
    case 0:
        if (((s16)(angle + 0x400) & 0xFFF) > 0x800) {
            actor->mAC_x2_facing = 1;
        } else {
            actor->mAC_x2_facing = 0;
        }
        {
            s32 o;
            o = *(u16*)(actor->m58_startOfCurrentAnimation + 4) + 4;
            actor->m54_frameTable = (u16*)(o + (sptr)actor->m58_startOfCurrentAnimation);
            actor->mA8_x11 = 0;
            actor->m5C = actor->m58_startOfCurrentAnimation + 6;
        }
        break;
    case 1:
        angle = ((angle + 0x600) >> 10) & 3;
        if (angle < 3) {
            actor->m54_frameTable = (u16*)(((u16*)actor->m58_startOfCurrentAnimation)[angle + 2] + (sptr)&((u16*)actor->m58_startOfCurrentAnimation)[angle + 2]);
            actor->mAC_x2_facing = 0;
        } else {
            s32 o = *(u16*)(actor->m58_startOfCurrentAnimation + 6) + 6;
            actor->m54_frameTable = (u16*)(o + (sptr)actor->m58_startOfCurrentAnimation);
            actor->mAC_x2_facing = 1;
        }
        actor->mA8_x11 = angle;
        break;
    case 2:
        angle = ((angle + 0x500) >> 9) & 7;
        if (angle < 5) {
            actor->m54_frameTable = (u16*)(((u16*)actor->m58_startOfCurrentAnimation)[angle + 2] + (sptr)&((u16*)actor->m58_startOfCurrentAnimation)[angle + 2]);
            actor->mAC_x2_facing = 0;
        } else {
            angle = (angle - 5) ^ 3;
            actor->m54_frameTable = (u16*)(((u16*)actor->m58_startOfCurrentAnimation)[angle + 2] + (sptr)&((u16*)actor->m58_startOfCurrentAnimation)[angle + 2]);
            actor->mAC_x2_facing = 1;
        }
        actor->mA8_x11 = angle;
        break;
    }
    if (prevDir != actor->mA8_x11) {
        s32 wait;
        u8* code;
        s32 step;
        wait = actor->m9E_wait;
        code = actor->m64_spriteByteCode;
        step = actor->mA8_x16_bytecodeStep;
        {
            s32 o = *(u16*)(actor->m58_startOfCurrentAnimation + 2) + 2;
            actor->m64_spriteByteCode = (u8*)(o + (sptr)actor->m58_startOfCurrentAnimation);
        }
        actor->mA8_xB_frame = 0x3F;
        actor->mA8_x16_bytecodeStep = 0;
        executeSpriteBytecode(actor, code, step);
        actor->m9E_wait = wait;
    }
    actor->m3C_x3_flipX = actor->mAC_x3 ^ actor->mAC_x2_facing;
}

/* Noah: executeSpriteBytecode - fast-forward the bytecode to (stop, stopStep). */
void executeSpriteBytecode(SpriteActorCore* actor, u8* stop, s32 stopStep) {
    u8* p;
    u8* next;
    u8 op;
    s32 n = PC_SPRITE_UNSET_COUNT; /* ops 0x40-0x7F: the PS1's $s3, or an earlier op's (main/sprite.h); the
                                    * caller (setSpriteActorAngle) puts the wait back */

loop:
    p = actor->m64_spriteByteCode;
    if (p == stop && actor->mA8_x16_bytecodeStep == stopStep) {
        return;
    }
    op = *p;
    next = p + 1;
    if (op < 0x80) {
        actor->m64_spriteByteCode = next;
        if (op < 0x10) {
            addToSpriteTransferList(actor, (u16)actor->m34_currentSpriteFrame + 1);
            n = (op & 0xF) + 1;
        } else if (op < 0x20) {
            actor->mA8_xB_frame++;
            executeSpriteBytecode2Sub1(actor);
            n = (op & 0xF) + 1;
        } else if (op < 0x30) {
            addToSpriteTransferList(actor, (u16)actor->m34_currentSpriteFrame - 1);
            n = (op & 0xF) + 1;
        }
        if (op < 0x40) {
            n = (op & 0xF) + 1;
        }
        actor->m9E_wait += n;
        if (++actor->mA8_x16_bytecodeStep == 0) {
            actor->mA8_x16_bytecodeStep--;
        }
        goto loop;
    }
    switch (op) {
    case 0x80:
    case 0x81:
    case 0x82:
        return;
    case 0xBE: {
        s32 v = p[1] | (p[2] << 8);
        actor->mAC_x3 = v >> 9;
        actor->m3C_x3_flipX = actor->mAC_x3 ^ actor->mAC_x2_facing;
        if ((u16)actor->m34_currentSpriteFrame != (v & 0x1FF)) {
            addToSpriteTransferList(actor, v & 0x1FF);
        }
        actor->m9E_wait += ((v >> 11) & 0xF) + 1;
        break;
    }
    case 0xE2: {
        s16 off = p[1] + (((s8*)next)[1] << 8);
        pushU24OnAnimationStack(actor, (s32)(uptr)(p + 3));
        actor->m64_spriteByteCode += off;
        goto loop;
    }
    case 0xB3:
        actor->mA8_xB_frame = ((s8*)p)[1];
        break;
    case 0x86:
        if (p == stop) {
            return;
        }
        break;
    case 0x87:
        if (p == stop) {
            return;
        }
        break;
    case 0x97:
        if (p == stop) {
            return;
        }
        break;
    }
    actor->m64_spriteByteCode += sizePerBytecodeTable[op - 0x80];
    goto loop;
}

/* Enhanced (xg-hbc.19.3): the 60 Hz field's 20 and 15 fps rooms run three or
 * four frames to a native frame, their sprites' time scales three or four
 * times the native one; divided by three the speed and gravity factors here
 * lose up to a per cent (a scripted walk fell 2.6 units behind in 13 native
 * frames). While the field draws such a room (pcSpritePrecise, set by
 * updateAndRenderField) they keep 8 more bits; at the native scales the
 * results are the same as the game's. */
int pcGameplaySpritePrecise(void); /* port/src/system/gameplay.c */
#define pcSpritePrecise pcGameplaySpritePrecise()
void computeStepFromMoveSpeed(SpriteActorCore* actor) {
    s32 speed = ((actor->m18_moveSpeed >> 4) << 8) / (s32)actor->mAC_x7_timeScale;
    if (pcSpritePrecise) {
        s64 fine = ((s64)((actor->m18_moveSpeed >> 4) << 8) << 8) / (s32)actor->mAC_x7_timeScale;
        actor->mC_step.vx = (s32)(((s64)(getAngleSin(actor->m32_direction) >> 2) * fine) >> 14);
        actor->mC_step.vz = (s32)((-((s64)(getAngleCos(actor->m32_direction) >> 2) * fine)) >> 14); /* as the game: negated, then shifted */
        return;
    }
    actor->mC_step.vx = ((getAngleSin(actor->m32_direction) >> 2) * speed) >> 6;
    actor->mC_step.vz = -((getAngleCos(actor->m32_direction) >> 2) * speed) >> 6;
}
/* The gravity's time factor: gravity * (0x10000 / scale)^2 / 256 / 256 (the game's, truncated as it does it). */
s32 pcSpriteGravityScaled(s32 gravity, s32 timeScale) {
    s32 inv;
    if (pcSpritePrecise) {
        return (s32)(((s64)gravity * (s64)(0x100000000LL / ((s64)timeScale * timeScale))) / 65536);
    }
    inv = 0x10000 / timeScale;
    return gravity * ((inv * inv) / 256) / 256;
}

s32 getSpriteNumVerticalStrides(s32* p) {
    return *p;
}

/* LoadImage(currentNpcSpriteUploadRect, currentNpcSpriteUploadDataPtr) on a scratch stack. */
void transfertNpcSpriteSheetElement(void) {
    u8* block = allocateHeap(PC_HEAP_BYTES(0x2000), 1);
    STACK_SWITCH(block + 0x1F00);
    LoadImage(currentNpcSpriteUploadRect, currentNpcSpriteUploadDataPtr);
    STACK_RESTORE();
    freeHeap(block);
}

void uploadNpcSpriteSheet(s32* data, s32 x, s16 y) {
    RECT rect;
    s32* p = data;
    s32 count = *p++;
    s32 offset = 0;
    u16* img;
    s32 o;
    rect.y = y;
    while (count-- > 0) {
        img = (u16*)((o = *p++) + (sptr)data);
        rect.w = *img++;
        rect.h = *img++;
        rect.x = x + offset;
        offset += 0x40;
        currentNpcSpriteUploadRect = &rect;
        currentNpcSpriteUploadDataPtr = img;
        transfertNpcSpriteSheetElement();
    }
}

void applySpriteVerticalMovement(SpriteActorCore* actor) {
    s32 step;
    s32 vel;
    s32 gravity;
    s32 maxY;
    if (!actor->m3C_x1A) {
        sampleBattleTerrainHeight(actor);
        step = actor->mC_step.vy;
        if (step > 0 && actor->m1C_gravity > 0) {
            if ((s16)(actor->m0_position.vy >> 16) == actor->m84_maxY) {
                return;
            }
            actor->m0_position.vy += modulateSpeed(actor, step >> 4) * 16;
            maxY = actor->m84_maxY;
            if ((actor->m0_position.vy >> 16) >= maxY) {
                vel = -actor->mC_step.vy * actor->mA8_x1_bounce;
                actor->m0_position.vy = maxY << 16;
                if (vel < 0) {
                    vel += 0xFF;
                }
                gravity = actor->m1C_gravity;
                vel >>= 8;
                actor->mC_step.vy = vel;
                if (abs(vel) < abs(gravity)) {
                    actor->mC_step.vy = 0;
                }
                return;
            }
        } else {
            actor->m0_position.vy += modulateSpeed(actor, step >> 4) * 16;
            if ((actor->m0_position.vy >> 16) >= actor->m84_maxY) {
                actor->m0_position.vy = actor->m84_maxY << 16;
            }
        }
        actor->mC_step.vy += actor->m1C_gravity;
    } else {
        actor->m0_position.vy += modulateSpeed(actor, actor->mC_step.vy >> 4) * 16;
        actor->mC_step.vy += actor->m1C_gravity;
    }
}

s32 modulateSpeed(SpriteActorCore* actor, s32 value) {
    u16 scale;
    if ((scale = actor->m3A_scaleModifier) != 0) {
        value = value * scale;
        value = value / 1024;
    }
    return value;
}

void updateSpriteMovement(SpriteActorCore* actor) {
    actor->m0_position.vx += modulateSpeed(actor, actor->mC_step.vx >> 4) * 16;
    actor->m0_position.vz += modulateSpeed(actor, actor->mC_step.vz >> 4) * 16;
    applySpriteVerticalMovement(actor);
}

void executeSpriteBytecode2Sub1(SpriteActorCore* actor) {
    s32 idx = actor->mA8_xB_frame;
    u16 entry;
    s32 frame;
    if (idx < 0) {
        idx = 0;
    }
    entry = actor->m54_frameTable[idx];
    frame = entry & 0x1FF;
    if (entry & 0x200) {
        actor->mAC_x3 = 1;
    } else {
        actor->mAC_x3 = 0;
    }
    actor->m3C_x3_flipX = actor->mAC_x3 ^ actor->mAC_x2_facing;
    addToSpriteTransferList(actor, frame);
}

void savePointCallback8(TaskHeader* task) {
    SpriteActorCore* actor = task->m4_data;
    OP_INIT_ENTITY_SCRIPT_sub0Sub9(actor);
    updateSpriteMovement(actor);
    if (actor->m64_spriteByteCode != NULL) {
        if (!actor->mAC_x6) {
            return;
        }
        OP_INIT_ENTITY_SCRIPT_sub0Sub9(actor);
        updateSpriteMovement(actor);
        if (actor->m64_spriteByteCode != NULL) {
            return;
        }
    }
    task->mC_deleteCallback(task);
}

void savePointCallback8Counted(TaskHeader* task) {
    D_800592EC++;
    savePointCallback8(task);
}

void savePointCallbackC(SpriteTask* task) {
    SpriteActorCore* actor = task->m0_updateTask.m4_data;
    TaskHeader* t = &task->m0_updateTask;
    if (actor->m20_object != NULL && actor->m20_object->m2C_subSpriteBuffer != NULL) {
        clearShapeTransferEntry(actor->m20_object->m2C_subSpriteBuffer);
    }
    if (actor->m3C_x0_type == 1) {
        if (actor->m20_object->m34_perSubgroupTransform != NULL) {
            freeHeap(actor->m20_object->m34_perSubgroupTransform);
        }
    }
    if (actor->mAC_x5) {
        registerSpriteCallback2_2(t);
    }
    if (actor->mB0_xB) {
        detachChildSpriteActors(t);
    }
    if (actor->m3C_x0_type == 1) {
        savePointCallbackC_sub1(actor);
    }
    allocateSavePointMeshDataSub0_callback(t);
    registerSpriteCallback2Sub0(t + 1);
    freeHeap(task);
}

void reallocateSubSpriteBuffer(SpriteActorCore* actor, s32 count, s32 flag) {
    void* p;
    if (actor->m20_object->m2C_subSpriteBuffer != NULL) {
        freeHeap(actor->m20_object->m2C_subSpriteBuffer);
    }
    p = allocateHeap(PC_HEAP_BYTES(count * 0x18), flag);
    actor->m20_object->m30_subSprites = actor->m20_object->m2C_subSpriteBuffer = p;
}

void allocateSpriteVramLocations(SpriteActorCore* actor, s32 count, s32 flag) {
    actor->m7C_extra->m18_vramLocations = allocateHeap(PC_HEAP_BYTES(count * 4), flag);
    actor->m7C_extra->m18_vramLocations[1] = actor->m24_vramData->m4_vramLocation.vy;
    actor->m7C_extra->m18_vramLocations[0] = actor->m24_vramData->m4_vramLocation.vx;
}

void deleteFieldEntitySub4(SpriteActorCore* actor) {
    if (actor->mA8_x0 == 1) {
        if (actor->m7C_extra->m18_vramLocations != NULL) {
            freeHeap(actor->m7C_extra->m18_vramLocations);
        }
    }
    savePointCallbackC_sub1(actor);
    freeHeap(actor->m20_object->m2C_subSpriteBuffer);
    freeHeap(actor);
}

s32 vec2dRatan2(Vec2s16 a, Vec2s16 b) {
    VECTOR d;
    d.vx = a.vx - b.vx;
    d.vz = a.vy - b.vy;
    return -ratan2(d.vz, d.vx) & 0xFFF;
}

void setSpriteActorStaticFrame(SpriteActorCore* actor, s32 frame, s32 flipX, s32 x4) {
    actor->m9E_wait = 0;
    actor->mA8_x14 = 0;
    actor->mA8_x11 = 0;
    actor->m3C_x4 = x4;
    actor->m3C_x3_flipX = flipX;
    addToSpriteTransferList(actor, frame);
}

s32 func_800231E0(AnimationBundle* b) {
    s32* p;
    p = (s32*)(b->mC_offset + (sptr)b);
    return *p;
}

s32 func_800231F8(AnimationBundle* b) {
    s32* p;
    p = (s32*)(b->mC_offset + (sptr)b);
    return p[1] + 1;
}

void OP_INIT_ENTITY_SCRIPT_sub0Sub9(SpriteActorCore* actor) {
    s32 i = 0;
    if (fieldDrawEnvsInitialized + 1 != 0) {
        do {
            if (actor->m9E_wait != 0) {
                if (--actor->m9E_wait == 0) {
                    executeSpriteBytecode2(actor);
                }
            }
            i++;
        } while (i != fieldDrawEnvsInitialized + 1);
    }
}

void setTransparencyMode(SpriteActorCore* actor, u32 mode) {
    actor->m3C_x5_blendMode = mode;
    if ((mode & 7) != 0) {
        actor->m2B_code |= 2;
    } else {
        actor->m2B_code &= 0xFD;
    }
    if (actor->m40_draw.bits.xD_mode == 8 || actor->m40_draw.bits.xD_mode == 9) {
        if (actor->m3C_x5_blendMode != 0) {
            actor->m3C_x5_blendMode--;
        }
    } else {
        updateAllSubsprites(actor);
    }
}

void OP_INIT_ENTITY_SCRIPT_sub0Sub3(SpriteActorCore* actor, s32 count) {
    void* p;
    freeHeap(actor->m20_object->m2C_subSpriteBuffer);
    p = allocateHeap(PC_HEAP_BYTES(count * 0x18), 0);
    actor->m20_object->m2C_subSpriteBuffer = actor->m20_object->m30_subSprites = p;
}

SpriteTask* allocateSavePointMeshData(TaskHeader* parent, s32 extraSize) {
    SpriteTask* task;
    TaskHeader* sub;
    SpriteActorCore* actor;
    s32 retailExtra = pcHeapTakeRequest();
    task = allocateHeap(PC_HEAP_SIZED(extraSize + sizeof(SpriteTask), retailExtra < 0 ? PC_HEAP_UNKNOWN : retailExtra + 0xEC), battleScriptLoaderTaskCreationInProgress);
    allocateSavePointMeshDataSub0(parent, &task->m0_updateTask);
    sub = &task->m1C_drawTask;
    registerSpriteCallback2(&task->m0_updateTask, sub);
    actor = &task->m38_actor;
    {
        extern void battleFxViewInitActor(SpriteActorCore*);
        battleFxViewInitActor(actor);
    }
    initFieldEntitySub4Sub1(actor);
    task->m0_updateTask.m4_data = actor;
    sub->m4_data = actor;
    setTaskUpdateFunction(&task->m0_updateTask, savePointCallback8);
    setTaskDeleteFunction(&task->m0_updateTask, (void (*)(TaskHeader*))savePointCallbackC);
    return task;
}

s32 getSavePointCreationMode1(u16* p) {
    s32 mode = (*p >> 8) & 7;
    if ((*p >> 14) & 1) {
        mode += 8;
    }
    return mode;
}

s32 getModeForSavePointMesh(u32 mode) {
    s32 r;
    switch (mode) {
    case 0: case 5: case 6: case 10: case 11: case 12: case 13: case 14:
        r = 1;
        break;
    case 1: case 4: case 8: case 9:
        r = 0;
        break;
    case 2: case 7: case 15:
        r = 2;
        break;
    }
    return r;
}

void resetPerSubgroupTransforms(SpriteActorCore* actor) {
    s32 i;
    for (i = 0; i != 8; i++) {
        actor->m20_object->m34_perSubgroupTransform[i].m0_translateX = 0;
        actor->m20_object->m34_perSubgroupTransform[i].m1_translateY = 0;
        actor->m20_object->m34_perSubgroupTransform[i].m2_rotateX = 0;
        actor->m20_object->m34_perSubgroupTransform[i].m4_rotateY = 0;
        actor->m20_object->m34_perSubgroupTransform[i].m6_rotateZ = 0;
    }
}

/* Noah: setCurrentAnimationPtr. */
void setCurrentAnimationPtr(SpriteActorCore* actor, u16* anim) {
    s32 offset;
    s32 gravityFactor;
    s32 scale;
    s32 speed;

    actor->m58_startOfCurrentAnimation = (u8*)anim;
    offset = anim[1] + 2;
    actor->m64_spriteByteCode = (u8*)(offset + (sptr)anim);
    actor->mA8_x14 = anim[0];
    offset = anim[2] + 4;
    actor->m54_frameTable = (u16*)(offset + (sptr)anim);
    gravityFactor = (anim[0] >> 2) & 0x3F;
    if (gravityFactor & 0x20) {
        gravityFactor |= ~0x3F;
    }
    actor->m1C_gravity = gravityFactor << 10;
    scale = (fieldDrawEnvsInitialized + 1) * (fieldDrawEnvsInitialized + 1) * actor->m82_entityScale;
    actor->m1C_gravity *= scale / 4096;
    speed = 0x10000 / (s32)actor->mAC_x7_timeScale;
    if (pcSpritePrecise) {
        actor->m1C_gravity = pcSpriteGravityScaled(actor->m1C_gravity, actor->mAC_x7_timeScale); /* Enhanced (above) */
    } else {
        actor->m1C_gravity *= (speed * speed) / 256;
        actor->m1C_gravity /= 256;
    }
    if (((anim[0] >> 11) & 1) == 0) {
        actor->mC_step.vz = 0;
        actor->mC_step.vy = 0;
        actor->mC_step.vx = 0;
        actor->m18_moveSpeed = 0;
    }
    if (actor->m20_object != NULL) {
        if (((anim[0] >> 12) & 1) == 0) {
            actor->m20_object->m0_rotation[1] = actor->m20_object->m0_rotation[0] = actor->m20_object->m0_rotation[2] = 0;
            setupSpriteObjectMatrix(actor);
        }
        if (((anim[0] >> 13) & 1) == 0) {
            if (isBattleOverlayLoaded != 0) {
                setGraphicEntityScale(actor, battleDefaultEntityScale);
            }
        }
        if (isBattleOverlayLoaded != 0) {
            setupSpriteObjectMatrix(actor);
        }
        if (actor->m3C_x0_type == 1) {
            actor->m20_object->m3C_anchorX = actor->m20_object->m3D_anchorY = 0;
            if (actor->m40_draw.bits.x14 == 0 && actor->m20_object->m34_perSubgroupTransform != NULL) {
                resetPerSubgroupTransforms(actor);
            }
        }
    }
    actor->m8C_stackPosition = 0x10;
    actor->mA8_x1_bounce = 0;
    actor->m30_otOffset = 0;
    actor->mA8_x16_bytecodeStep = 0;
    actor->mA8_x1C = 2;
    actor->mA8_xB_frame = 0x3F;
    actor->m9E_wait = 1;
    if (actor->m7C_extra != NULL && actor->mA8_x0 == 1) {
        actor->m7C_extra->m0 = actor->m7C_extra->m4_gravityOverride = 0;
        actor->m7C_extra->mC_animationSignal = 0;
    }
}

/* Noah: initFieldEntitySub4Sub1. */
void initFieldEntitySub4Sub1(SpriteActorCore* actor) {
    s32 gravity;
    u32* flags;

    SPRITE_WORD(actor, 0x3C) = 0;
    actor->m2B_code = 0x2D;
    actor->m40_draw.raw = 0;
    actor->m3A_scaleModifier = 0;
    actor->m30_otOffset = 0;
    actor->m32_direction = 0;
    actor->m34_currentSpriteFrame = 0;
    SPRITE_WORD(actor, 0xA8) = 0;
    actor->m3C_x2 = 0;
    actor->m3C_x3_flipX = 0;
    actor->m3C_x4 = 0;
    actor->m3C_x14 = 0;
    actor->m3C_x18 = 0;
    actor->m3C_x10 = 0;
    actor->m40_draw.bits.x2_subSpriteCount = 0;
    actor->m40_draw.bits.x8 = 0;
    actor->m40_draw.bits.xD_mode = 0;
    flags = &SPRITE_WORD(actor, 0xA8);
    flags[1] = 0;
    flags[2] = 0;
    actor->mB0_x0_animationId = 0;
    actor->mAF_animationId = 0;
    gravity = (fieldDrawEnvsInitialized + 1) * ((fieldDrawEnvsInitialized + 1) << 14) * actor->m82_entityScale;
    actor->mA8_x11 = 0;
    actor->mAC_x7_timeScale = 0x100;
    actor->m1C_gravity = gravity / 4096;
    actor->m64_spriteByteCode = NULL;
    actor->m70_parent = NULL;
    actor->m44_currentAnimationBundle = NULL;
    actor->m68 = NULL;
    actor->m80_angle = 0;
    actor->m8C_stackPosition = 0x10;
    actor->m84_maxY = 0;
    actor->m6C_owner = NULL;
    actor->m50_soundEffectSeq = NULL;
}

/* Noah: initsFieldEntitySub4_B4. */
void initsFieldEntitySub4_B4(SpriteObject* obj) {
    obj->m0_rotation[0] = 0;
    obj->m0_rotation[1] = 0;
    obj->m0_rotation[2] = 0;
    obj->m2C_subSpriteBuffer = NULL;
}

/* Noah: createSavePointMeshDataMode0. */
void createSavePointMeshDataMode0(SpriteActorCore* actor) {
    actor->m20_object = NULL;
}

/* Noah: createSavePointMeshDataMode2 (3D model object after the actor). */
void createSavePointMeshDataMode2(SpriteActorCore* actor) {
    actor->m20_object = (SpriteObject*)(actor + 1);
    initsFieldEntitySub4_B4(actor->m20_object);
    ((ModelObject*)actor->m20_object)->m34_modelBlock = NULL;
    ((ModelObject*)actor->m20_object)->m40 = 0;
}

/* Noah: initFieldEntitySub4Sub2. */
void initFieldEntitySub4Sub2(SpriteActor* actor) {
    actor->m0_spriteActorCore.m20_object = &actor->mB4_spriteObject;
    initsFieldEntitySub4_B4(&actor->mB4_spriteObject);
    actor->m0_spriteActorCore.m7C_extra = &actor->mF4_extra;
    actor->m0_spriteActorCore.m20_object->m34_perSubgroupTransform = actor->m124_subgroupTransforms;
    actor->m0_spriteActorCore.m24_vramData = &actor->m110_vramData;
    actor->m0_spriteActorCore.m20_object->m38_pNext = NULL;
}

/* Noah: createSavePointMeshDataMode1 (sprite object + sub-sprites after the actor). */
void createSavePointMeshDataMode1(SpriteActorCore* actor) {
    actor->m20_object = (SpriteObject*)(actor + 1);
    initsFieldEntitySub4_B4(actor->m20_object);
    actor->m20_object->m30_subSprites = (SubSprite*)((SpriteObject*)(actor + 1) + 1); /* 0xF4 on the PS1 */
    actor->m20_object->m34_perSubgroupTransform = NULL;
    actor->m20_object->m38_pNext = NULL;
}

/* Noah: createSavePointMeshData. */
SpriteTask* createSavePointMeshData(s32 mode1, s32 mode2, SpriteVramData* vram, s32 extraSize, TaskHeader* parent) {
    SpriteTask* task;
    SpriteActorCore* actor;
    s32 size;
    s32 retailExtra = pcHeapTakeRequest();

    switch (mode2) {
    case 0:
        size = 0;
        task = allocateSavePointMeshData(parent, PC_HEAP_SIZED(extraSize, retailExtra));
        createSavePointMeshDataMode0(&task->m38_actor);
        break;
    case 1:
        if (mode1 == 5) {
            vram = &createSavePointMeshData_mode5;
        }
        if (mode1 == 6) {
            vram = &createSavePointMeshData_mode6;
        }
        size = (initFieldEntitySub4Sub4((void*)vram->m0_spriteData) - 1) * sizeof(SubSprite) + (sizeof(SpriteObject) + sizeof(SubSprite));
        task = allocateSavePointMeshData(parent, PC_HEAP_SIZED(size + extraSize, retailExtra < 0 ? PC_HEAP_UNKNOWN : size - (s32)sizeof(SpriteObject) + 0x40 + retailExtra));
        createSavePointMeshDataMode1(&task->m38_actor);
        break;
    case 2:
        size = sizeof(ModelObject);
        task = allocateSavePointMeshData(parent, PC_HEAP_SIZED(extraSize + (s32)sizeof(ModelObject), retailExtra < 0 ? PC_HEAP_UNKNOWN : retailExtra + 0x54));
        createSavePointMeshDataMode2(&task->m38_actor);
        break;
    }
    actor = &task->m38_actor;
    actor->m6C_owner = task;
    actor->m86_thisSize = size + sizeof(SpriteTask);
    actor->m24_vramData = vram;
    return task;
}

/* Noah: spriteBytecode2ExtendedE0 (spawns a child effect sprite). */
SpriteActorCore* spriteBytecode2ExtendedE0(SpriteActorCore* actor, u16* anim, SpriteVramData* vram) {
    u8 saved;
    s32 mode1;
    s32 mode2;
    SpriteTask* task;
    SpriteActorCore* child;
    s32 id;
    s32 lo;

    saved = spriteBytecode2ExtendedE0_Var0;
    actor->mB0_xB = 1;
    if (actor->mB0_x8) {
        spriteBytecode2ExtendedE0_Var0 = 0;
    }
    mode1 = getSavePointCreationMode1(anim);
    if (mode1 == 3) {
        mode1 = actor->m40_draw.bits.xD_mode;
    }
    mode2 = getModeForSavePointMesh(mode1);
    task = createSavePointMeshData(mode1, mode2, vram, PC_HEAP_BYTES(0), actor->m6C_owner);
    child = &task->m38_actor;
    task->m0_updateTask.m14_ownerId.m29 = 1;
    child->m40_draw.bits.xD_mode = mode1;
    vram = child->m24_vramData;
    child->m3C_x0_type = mode2;
    child->m40_draw.bits.x8 = actor->m40_draw.bits.x8;
    child->m3C_x3_flipX = actor->m3C_x3_flipX;
    child->m3C_x4 = actor->m3C_x4;
    child->m3C_x8 = actor->m3C_x8;
    child->m40_draw.bits.x12 = actor->m40_draw.bits.x12;
    child->m3C_x1A = 1;
    child->m3C_x2 = 0;
    child->m18_moveSpeed = actor->m18_moveSpeed;
    child->m32_direction = actor->m32_direction;
    child->m2C_scale = actor->m2C_scale;
    child->m24_vramData = vram; /* no-op store: cdk's post-reload CSE drops it, JP's 2.6.0 keeps it */
    child->m34_currentSpriteFrame = actor->m34_currentSpriteFrame;
    if ((child->mB0_x9 = actor->mB0_x9) != 0) {
        child->m3A_scaleModifier = actor->m3A_scaleModifier;
        child->m40_draw.bits.x8 = 3;
    }
#if VERSION_JP
    child->mAC_x0 = actor->mAC_x0;
#else
    lo = actor->mA8_x1E;
    id = (actor->mAC_x0 << 2) | lo;
    child->mA8_x1E = id;
    child->mAC_x0 = id >> 2;
#endif
    child->mB0_x8 = actor->mB0_x8;
    child->mAC_x6 = actor->mAC_x6;
    child->mAC_x7_timeScale = actor->mAC_x7_timeScale;
    child->mAC_x2_facing = actor->mAC_x2_facing;
    child->mA8_x0 = 0;
    if (actor->mA8_x0 == 0) {
        child->m7C_extra = actor->m7C_extra;
    } else {
        child->m7C_extra = NULL;
    }
    child->m70_parent = actor;
    child->m44_currentAnimationBundle = actor->m44_currentAnimationBundle;
    child->m48_defaultAnimationBundle = actor->m48_defaultAnimationBundle;
    child->m74_targetEntitySprite = actor->m74_targetEntitySprite;
    child->m82_entityScale = actor->m82_entityScale;
    child->m50_soundEffectSeq = actor->m50_soundEffectSeq;
    child->m8D_parentAnimationId = actor->mAF_animationId;
    child->m78_terrainTriangle = actor->m78_terrainTriangle;
    child->m0_position.vx = actor->m0_position.vx;
    child->m0_position.vy = actor->m0_position.vy;
    child->m0_position.vz = actor->m0_position.vz;
    child->mC_step.vx = actor->mC_step.vx;
    child->mC_step.vy = actor->mC_step.vy;
    child->mC_step.vz = actor->mC_step.vz;
    if (mode2 != 0) {
        /* A spawner without an object (battle effects on a plain sprite):
         * the PS1 copies low BIOS RAM through NULL, the u16s at 0..0xA (PCSX-Redux:
         * words 0x00000003 0x275A0C80 0x03400008). */
        static const s16 lowRam[6] = {3, 0, 0xC80, 0x275A, 8, 0x340};
        const s16* src = actor->m20_object != NULL ? actor->m20_object->m0_rotation : lowRam;
        child->m20_object->m0_rotation[0] = src[0];
        child->m20_object->m0_rotation[1] = src[1];
        child->m20_object->m0_rotation[2] = src[2];
        child->m20_object->m6_scale[0] = src[3];
        child->m20_object->m6_scale[1] = src[4];
        child->m20_object->m6_scale[2] = src[5];
    }
    setCurrentAnimationPtr(child, anim);
    spriteBytecode2ExtendedE0_Sub0(task);
    spriteBytecode2ExtendedE0_Var0 = saved;
    return child;
}

/* Spawns an effect sprite from animation `animationId` of `vram`'s container. */
SpriteTask* spawnEffectSprite(s32 animationId, SpriteVramData* vram, s16* position, s32 extraSize) {
    u16* anim;
    s32 mode1;
    s32 mode2;
    SpriteTask* task;
    SpriteActorCore* child;
    SpriteActorCore* ref;
    s32 id;
    s32 lo;
    s32 retailExtra = pcHeapTakeRequest();

    anim = (u16*)(((u16*)vram->m10_startOfAnimationContainer)[animationId + 1] + vram->m10_startOfAnimationContainer);
    mode1 = getSavePointCreationMode1(anim);
    mode2 = getModeForSavePointMesh(mode1);
    task = createSavePointMeshData(mode1, mode2, vram, PC_HEAP_SIZED(extraSize, retailExtra), NULL);
    task->m0_updateTask.m14_ownerId.m29 = 1;
    child = &task->m38_actor;
    vram = child->m24_vramData;
    child->m70_parent = NULL;
    child->m74_targetEntitySprite = NULL;
    if (isBattleOverlayLoaded != 0 && (ref = processBattleAnimationSub0_var1) != NULL) {
        child->m44_currentAnimationBundle = ref->m44_currentAnimationBundle;
        child->m48_defaultAnimationBundle = ref->m48_defaultAnimationBundle;
        child->m74_targetEntitySprite = ref->m74_targetEntitySprite;
        child->m18_moveSpeed = ref->m18_moveSpeed;
        child->m32_direction = ref->m32_direction;
        child->m40_draw.bits.x8 = ref->m40_draw.bits.x8;
        child->m3C_x3_flipX = ref->m3C_x3_flipX;
        child->m3C_x4 = ref->m3C_x4;
        child->m3C_x8 = ref->m3C_x8;
        child->m2C_scale = ref->m2C_scale;
        child->m3C_x1A = 1;
        child->m3C_x2 = 0;
        child->mAC_x2_facing = ref->mAC_x2_facing;
        child->mAC_x7_timeScale = ref->mAC_x7_timeScale;
        child->m7C_extra = ref->m7C_extra;
        child->m7C_extra = ref->m7C_extra;
#if VERSION_JP
        child->mAC_x0 = ref->mAC_x0;
#else
        lo = ref->mA8_x1E;
        id = (ref->mAC_x0 << 2) | lo;
        child->mA8_x1E = id;
        child->mAC_x0 = id >> 2;
#endif
        child->m50_soundEffectSeq = ref->m50_soundEffectSeq;
        child->m8D_parentAnimationId = ref->mAF_animationId;
    }
    child->m44_currentAnimationBundle = NULL;
    child->m48_defaultAnimationBundle = NULL;
    child->m34_currentSpriteFrame = 0;
    child->m24_vramData = vram;
    child->m40_draw.bits.xD_mode = mode1;
    child->m3C_x0_type = mode2;
    child->m82_entityScale = battleDefaultEntityScale;
    child->m0_position.vx = position[0] << 16;
    child->m0_position.vy = position[1] << 16;
    child->m0_position.vz = position[2] << 16;
    setCurrentAnimationPtr(child, anim);
    spriteBytecode2ExtendedE0_Sub0(task);
    return task;
}

/* Noah: createSpriteActorEX. */
SpriteActor* createSpriteActorEX(AnimationBundle* bundle, s16 clutX, s16 clutY, s16 vramX, s16 vramY, s16 arg5, s32 vramParam) {
    SpriteActor* actor;
    loadVramSpriteParam = vramParam;
    actor = createSpriteActor(bundle, clutX, clutY, vramX, vramY, arg5);
    loadVramSpriteParam = 0;
    return actor;
}

/* Noah: initBattleSpriteActorVram. */
void initBattleSpriteActorVram(SpriteActor* actor, AnimationBundle* bundle, s16 clutX, s16 clutY, s16 vramX, s16 vramY, s16 arg6, s32 vramParam) {
    loadVramSpriteParam = vramParam;
    initializeSpriteActor(actor, bundle, clutX, clutY, vramX, vramY, arg6);
    loadVramSpriteParam = 0;
}

/* Noah: initializeSpriteActor. */
SpriteActor* initializeSpriteActor(SpriteActor* arg0, AnimationBundle* bundle, s16 clutX, s16 clutY, s16 vramX, s16 vramY, s16 arg6) {
    SpriteActor* actor = arg0;
    AnimationBundle* setup = bundle;
    s32 count;
    SubSprite* sub;

    initFieldEntitySub4Sub1(&actor->m0_spriteActorCore);
    initFieldEntitySub4Sub2(actor);
    setGraphicEntityScale(&actor->m0_spriteActorCore, 0x1000);
    actor->m0_spriteActorCore.m40_draw.bits.xD_mode = 0;
    actor->m0_spriteActorCore.m3C_x0_type = 1;
    if (isBattleOverlayLoaded != 0) {
        actor->m0_spriteActorCore.mA8_x0 = 0;
        actor->m0_spriteActorCore.m7C_extra->m8 = 0;
        actor->m0_spriteActorCore.m7C_extra->mC_animationSignal = 0;
    } else {
        actor->m0_spriteActorCore.mA8_x0 = 1;
        actor->m0_spriteActorCore.m7C_extra->m18_vramLocations = NULL;
    }
    actor->m0_spriteActorCore.m6C_owner = actor;
    actor->m0_spriteActorCore.m3C_x14 = loadVramSpriteParam;
    actor->m0_spriteActorCore.m3C_x10 = loadVramSpriteParam;
    count = initFieldEntitySub4Sub4((void*)(setup->m8_spriteDataOffset + (sptr)setup));
    sub = allocateHeap(PC_HEAP_SIZED(count * sizeof(SubSprite), count * 0x18), 0);
    actor->m0_spriteActorCore.m20_object->m30_subSprites = actor->m0_spriteActorCore.m20_object->m2C_subSpriteBuffer = sub;
    actor->m0_spriteActorCore.m24_vramData->m4_vramLocation.vx = vramX;
    actor->m0_spriteActorCore.m24_vramData->m4_vramLocation.vy = vramY;
    actor->m0_spriteActorCore.m24_vramData->m8_clut.vx = clutX;
    actor->m0_spriteActorCore.m24_vramData->m8_clut.vy = clutY;
    actor->m0_spriteActorCore.m48_defaultAnimationBundle = bundle;
    setAnimationBundle(&actor->m0_spriteActorCore, bundle);
    actor->m0_spriteActorCore.m60_endOfAnimationContainer = (u8*)actor->m0_spriteActorCore.m24_vramData->m10_startOfAnimationContainer
        + ((*(u16*)actor->m0_spriteActorCore.m24_vramData->m10_startOfAnimationContainer & 0x3F) + 1) * 2;
    spriteActorSetPlayingAnimation(&actor->m0_spriteActorCore, 0);
    return actor;
}

/* Noah: createSpriteActor. */
SpriteActor* createSpriteActor(AnimationBundle* bundle, s16 clutX, s16 clutY, s16 vramX, s16 vramY, s16 arg5) {
    SpriteActor* actor = allocateHeap(PC_HEAP_SIZED(sizeof(SpriteActor), 0x164), 0);
    actor->m0_spriteActorCore.m86_thisSize = sizeof(SpriteActor);
    return initializeSpriteActor(actor, bundle, clutX, clutY, vramX, vramY, arg5);
}

void spriteActorSetPlayingAnimation(SpriteActorCore* actor, s32 animationId) {
    uptr container;
    u16* anim;

    if (actor->m48_defaultAnimationBundle == NULL) {
        actor->m64_spriteByteCode = NULL;
        return;
    }
    if (actor->m44_currentAnimationBundle == actor->m48_defaultAnimationBundle) {
        actor->mB0_xA = 0;
    } else {
        actor->mB0_xA = 1;
    }
    if (animationId < 0) {
        setAnimationBundle(actor, actor->m4C_specialAnimation);
        if (isBattleOverlayLoaded != 0 && isVramPreBacked((void*)actor->m24_vramData->m0_spriteData) == 0) {
            actor->m24_vramData->m4_vramLocation.vy = 0x100;
            actor->m24_vramData->m4_vramLocation.vx = 0x300;
        }
    } else {
        setAnimationBundle(actor, actor->m48_defaultAnimationBundle);
        if (isBattleOverlayLoaded != 0) {
            actor->m24_vramData->m4_vramLocation = actor->m7C_extra->mE_vramLocation;
        }
    }
    actor->mAF_animationId = animationId;
    if (animationId < 0) {
        animationId = ~animationId;
    }
    container = actor->m24_vramData->m10_startOfAnimationContainer;
    anim = (u16*)(((u16*)container)[animationId + 1] + container);
    actor->m40_draw.bits.x14 = 1;
    actor->m58_startOfCurrentAnimation = (u8*)anim;
    setCurrentAnimationPtr(actor, anim);
    setSpriteActorAngle(actor, actor->m80_angle);
}

void spriteBytecode2ExtendedE0_Sub0(SpriteTask* task) {
    TaskHeader* header = &task->m0_updateTask;
    TaskHeader* sub = &task->m1C_drawTask;
    SpriteActorCore* actor = &task->m38_actor;
    s32 mode = actor->m40_draw.bits.xD_mode;

    switch (mode) {
    case 0:
    case 14:
        break;
    case 12:
        actor->m34_currentSpriteFrame = 1;
        mode = actor->m40_draw.bits.xD_mode -= 2;
        battleCameraControlSpriteInit(task);
        actor->m0_position.vx = previousCameraEye2.vx;
        actor->m0_position.vy = previousCameraEye2.vy;
        actor->m0_position.vz = previousCameraEye2.vz;
        break;
    case 10:
        actor->m34_currentSpriteFrame = 0;
        battleCameraControlSpriteInit(task);
        actor->m0_position.vx = previousCameraEye2.vx;
        actor->m0_position.vy = previousCameraEye2.vy;
        actor->m0_position.vz = previousCameraEye2.vz;
        break;
    case 13:
        actor->m34_currentSpriteFrame = 1;
        mode = actor->m40_draw.bits.xD_mode -= 2;
        battleCameraControlSpriteInit(task);
        actor->m0_position.vx = previousCameraEye2.vx;
        actor->m0_position.vy = previousCameraEye2.vy;
        actor->m0_position.vz = previousCameraEye2.vz;
        break;
    case 11:
        actor->m34_currentSpriteFrame = 0;
        battleCameraControlSpriteInit(task);
        actor->m0_position.vx = previousCameraAt2.vx;
        actor->m0_position.vy = previousCameraAt2.vy;
        actor->m0_position.vz = previousCameraAt2.vz;
        break;
    case 7:
        setTaskUpdateFunction(header, savePointCallback8Counted);
        break;
    case 8:
        actor->m2B_code = 0x68;
        actor->m34_currentSpriteFrame = 1;
        break;
    case 9:
        actor->m36_height = 3;
        actor->m2B_code = 0x60;
        actor->m34_currentSpriteFrame = 1;
        break;
    }
    spriteBytecode2ExtendedE0_Sub0Sub0(sub, mode);
}
