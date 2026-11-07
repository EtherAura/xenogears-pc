#ifndef MAIN_SPRITE_H
#define MAIN_SPRITE_H

/*
 * Sprite actors: the kernel's animated 2D sprite objects, shared by field,
 * battle and menus (Noah: fieldGraphicObject.cpp). Field names carry their
 * offset; unknown fields keep a bare offset name.
 */

#include "common.h"
#include "psyq.h"
#include "main/task.h"

/* The sprite bytecode keeps return addresses in 24 bits on the animation
 * stack, and the PS1 takes the top byte from the current bytecode pointer.
 * The PC takes the address nearest the current one with those low 24 bits
 * (a call and its return lie within one bytecode block). */
static inline u8* pcU24Address(u8* near, s32 low24) {
    uptr n = (uptr)near;
    uptr p = (n & ~(uptr)0xFFFFFF) | ((uptr)low24 & 0xFFFFFF);

    if (p > n + 0x800000) {
        p -= 0x1000000;
    } else if (p + 0x800000 < n) {
        p += 0x1000000;
    }
    return (u8*)p;
}

/* Three fixed-point coordinates without VECTOR's padding word (Noah: FP_VEC3). */
typedef struct {
    s32 vx, vy, vz;
} FP_VEC3;

/* Two s16 coordinates (Noah: sVec2_s16). Alignment 2, so copies use lwl/lwr. */
typedef struct {
    s16 vx, vy;
} Vec2s16;

/* Sprite animation bundle header: offsets relative to the header itself. */
typedef struct {
    /* 0x00 */ s32 m0;
    /* 0x04 */ s32 m4_animationsOffset;
    /* 0x08 */ s32 m8_spriteDataOffset;
    /* 0x0C */ s32 mC_offset;
} AnimationBundle;

/* Per-subgroup sprite transform (Noah: sFieldEntitySub4_124). */
typedef struct {
    /* 0x00 */ s8 m0_translateX;
    /* 0x01 */ s8 m1_translateY;
    /* 0x02 */ s16 m2_rotateX;
    /* 0x04 */ s16 m4_rotateY;
    /* 0x06 */ s16 m6_rotateZ;
} SubgroupTransform; /* 0x8 */

/* One sub-sprite quad (Noah: sFieldEntitySub4_B4_sub). */
typedef struct {
    /* 0x00 */ s16 m0_offsetX;
    /* 0x02 */ s16 m2_offsetY;
    /* 0x04 */ u8 m4_texcoordX;
    /* 0x05 */ u8 m5_texcoordY;
    /* 0x06 */ u8 m6_width;
    /* 0x07 */ u8 m7_height;
    /* 0x08 */ s8 m8_widthAdjust;
    /* 0x09 */ s8 m9_heightAdjust;
    /* 0x0A */ u16 mA_tpage;
    /* 0x0C */ u16 mC_clut;
    /* 0x0E */ u16 mE;
    /* 0x10 */ u32 m10_colorAndCode;
    /* 0x14 */ union {                     /* sub-sprite attributes: transform slot, colour depth, tile flips */
                   u32 raw;
                   struct {
                       u32 x0_transformId : 3;
                       u32 x3_8bit : 1;         /* texture colour depth: 0 4-bit, 1 8-bit */
                       u32 x4_tileFlipX : 1;
                       u32 x5_tileFlipY : 1;
                       u32 x6 : 26;
                   } bits;
               } m14_attrs;
} SubSprite; /* 0x18 */

struct SpriteActorCore;

/* SpriteActorCore::m20 for sprite actors (Noah: sFieldEntitySub4_B4). */
typedef struct SpriteObject {
    /* 0x00 */ s16 m0_rotation[3];
    /* 0x06 */ s16 m6_scale[3];
    /* 0x0C */ MATRIX mC_matrix;
    /* 0x2C */ SubSprite* m2C_subSpriteBuffer;
    /* 0x30 */ SubSprite* m30_subSprites;
    /* 0x34 */ SubgroupTransform* m34_perSubgroupTransform; /* [8] */
    /* 0x38 */ struct SpriteActorCore* m38_pNext;
    /* 0x3C */ s8 m3C_anchorX;
    /* 0x3D */ s8 m3D_anchorY;                          /* y anchor */
} SpriteObject;

/* SpriteActorCore::m20 for 3D-model actors (m3C_x0_type != 1). */
typedef struct {
    /* 0x00 */ s16 m0_rotation[3];
    /* 0x06 */ s16 m6_scale[3];
    /* 0x0C */ MATRIX mC_matrix;
    /* 0x2C */ void* m2C_primBuffers[2];
    /* 0x34 */ void* m34_modelBlock;
    /* 0x38 */ void* m38;
    /* 0x3C */ u32 m3C;
    /* 0x40 */ u32 m40;
    /* 0x44 */ SVECTOR m44_lightRotation;
    /* 0x4C */ u16 m4C_lightColor[3];
} ModelObject;

/*
 * SpriteActorCore::m24_vramData (Noah: sFieldEntitySub4_110). The two data
 * pointers are integers (uptr): retail adds offsets as `offset + base`,
 * which gcc only emits for integer arithmetic.
 */
typedef struct {
    /* 0x00 */ uptr m0_spriteData;
    /* 0x04 */ Vec2s16 m4_vramLocation;
    /* 0x08 */ Vec2s16 m8_clut;
    /* 0x0C */ u8* mC_clutData;
    /* 0x10 */ uptr m10_startOfAnimationContainer;
} SpriteVramData; /* 0x14 */

/* SpriteActorCore::m7C (Noah: sFieldEntitySub4_F4). */
typedef struct {
    /* 0x00 */ s32 m0;
    /* 0x04 */ s32 m4_gravityOverride;
    /* 0x08 */ s32 m8;
    /* 0x0C */ u16 mC_animationSignal;                    /* field reads it zero-extended */
    /* 0x0E */ Vec2s16 mE_vramLocation;
    /* 0x12 */ s16 m12;
    /* 0x14 */ s16 m14_actorId;
    /* 0x16 */ s16 m16;
    /* 0x18 */ s16* m18_vramLocations;
} SpriteActorExtra;

/* Sprite actor (Noah: sSpriteActorCore), 0xB4 bytes. */
typedef struct SpriteActorCore {
    /* 0x00 */ FP_VEC3 m0_position;
    /* 0x0C */ FP_VEC3 mC_step;
    /* 0x18 */ s32 m18_moveSpeed;
    /* 0x1C */ s32 m1C_gravity;
    /* 0x20 */ SpriteObject* m20_object;
    /* 0x24 */ SpriteVramData* m24_vramData;
    /* 0x28 */ u8 m28_r;
    /* 0x29 */ u8 m29_g;
    /* 0x2A */ u8 m2A_b;
    /* 0x2B */ u8 m2B_code;                   /* bit 0: raw texture (no colour blend) */
    /* 0x2C */ s16 m2C_scale;
    /* 0x2E */ s16 m2E_otIndex;
    /* 0x30 */ s16 m30_otOffset;
    /* 0x32 */ s16 m32_direction;
    /* 0x34 */ s16 m34_currentSpriteFrame;
    /* 0x36 */ s16 m36_height;
    /* 0x38 */ s16 m38_centerHeight;
    /* 0x3A */ s16 m3A_scaleModifier;
    /* 0x3C */ u32 m3C_x0_type : 2;           /* 1: sprite (m20 is a SpriteObject) */
               u32 m3C_x2 : 1;
               u32 m3C_x3_flipX : 1;
               u32 m3C_x4 : 1;
               u32 m3C_x5_blendMode : 3;      /* 0: opaque, else semi-transparency mode + 1 */
               u32 m3C_x8 : 8;
               u32 m3C_x10 : 4;
               u32 m3C_x14 : 4;
               u32 m3C_x18 : 1;
               u32 m3C_x19 : 1;
               u32 m3C_x1A : 1;                /* no battle terrain sampling */
               u32 m3C_x1B : 1;
               u32 m3C_x1C_matrixDirty : 1;
               u32 m3C_x1D : 1;
               u32 m3C_x1E_bundleChanged : 1;
               u32 m3C_x1F : 1;
    /* 0x40 */ union {                     /* draw control: sub-sprite count, scale shift (x8), creation mode, list/tint/subgroup bits */
                   u32 raw;
                   struct {
                       u32 x0 : 1;
                       u32 x1 : 1;
                       u32 x2_subSpriteCount : 6;
                       u32 x8 : 5;
                       u32 xD_mode : 4;          /* save point creation mode */
                       u32 x11 : 1;
                       u32 x12 : 1;              /* copied to child sprites */
                       u32 x13 : 1;
                       u32 x14 : 1;              /* keep per-subgroup transforms */
                       u32 x15 : 11;
                   } bits;
               } m40_draw;
    /* 0x44 */ void* m44_currentAnimationBundle;
    /* 0x48 */ void* m48_defaultAnimationBundle;
    /* 0x4C */ void* m4C_specialAnimation;
    /* 0x50 */ u8* m50_soundEffectSeq;
    /* 0x54 */ u16* m54_frameTable;
    /* 0x58 */ u8* m58_startOfCurrentAnimation;
    /* 0x5C */ u8* m5C;
    /* 0x60 */ u8* m60_endOfAnimationContainer;
    /* 0x64 */ u8* m64_spriteByteCode;
    /* 0x68 */ void (*m68)(struct SpriteActorCore*);
    /* 0x6C */ void* m6C_owner;
    /* 0x70 */ struct SpriteActorCore* m70_parent;
    /* 0x74 */ struct SpriteActorCore* m74_targetEntitySprite;
    /* 0x78 */ u32 m78_terrainTriangle;
    /* 0x7C */ SpriteActorExtra* m7C_extra;
    /* 0x80 */ s16 m80_angle;
    /* 0x82 */ s16 m82_entityScale;
    /* 0x84 */ s16 m84_maxY;
    /* 0x86 */ u16 m86_thisSize;
    /* 0x88 */ u8* m88_stack2;
    /* 0x8C */ s8 m8C_stackPosition;
    /* 0x8D */ s8 m8D_parentAnimationId;
    /* 0x8E */ u8 m8E_stack[16];
    /* 0x9E */ s16 m9E_wait;
    /* 0xA0 */ SVECTOR mA0_destination;
    /* 0xA8 */ u32 mA8_x0 : 1;
               u32 mA8_x1_bounce : 10;
               u32 mA8_xB_frame : 6;
               u32 mA8_x11 : 3;
               u32 mA8_x14 : 2;
               u32 mA8_x16_bytecodeStep : 6;
               u32 mA8_x1C : 2;
               u32 mA8_x1E : 2;
#if VERSION_JP
    /* 0xAC */ u32 mAC_x0 : 4;                /* JP: 4 bits, so everything up to 0xAF sits 2 bits higher */
#else
    /* 0xAC */ u32 mAC_x0 : 2;
#endif
               u32 mAC_x2_facing : 1;
               u32 mAC_x3 : 1;
               u32 mAC_x4 : 1;
               u32 mAC_x5 : 1;
               u32 mAC_x6 : 1;
               u32 mAC_x7_timeScale : 12;
#if VERSION_JP
               u32 mAC_x13 : 3;
#else
               u32 mAC_x13 : 5;
#endif
    /* 0xAF */ u8 mAF_animationId;            /* read as (s8) */
    /* 0xB0 */ s32 mB0_x0_animationId : 8;
               u32 mB0_x8 : 1;
               u32 mB0_x9 : 1;
               u32 mB0_xA : 1;
               u32 mB0_xB : 1;
               u32 mB0_xC : 20;
} SpriteActorCore;

/* A sprite actor allocated with all its parts (createSpriteActor), 0x164 bytes. */
typedef struct {
    /* 0x000 */ SpriteActorCore m0_spriteActorCore;
    /* 0x0B4 */ SpriteObject mB4_spriteObject;
    /* 0x0F4 */ SpriteActorExtra mF4_extra;
    /* 0x110 */ SpriteVramData m110_vramData;
    /* 0x124 */ SubgroupTransform m124_subgroupTransforms[8];
} SpriteActor;

/* Snapshot of a sprite actor's animation state (see spriteActorSaveState/spriteActorRestoreState). */
typedef struct {
    /* 0x00 */ FP_VEC3 m0_position;
    /* 0x0C */ s32 mC;
    /* 0x10 */ s16 m10_angle;
    /* 0x12 */ s16 m12_frame;
    /* 0x14 */ s16 m14_animationId;
    /* 0x16 */ s16 m16_defaultAnimationId;
    /* 0x18 */ s16 m18_bytecodeStep;
    /* 0x1A */ s16 m1A;
    /* 0x1C */ s32 m1C;
    /* 0x20 */ s32 m20_gravityOverride;
    /* 0x24 */ s16 m24_scale[3];
    /* 0x2A */ s16 m2A_scale;
    /* 0x2C */ s16 m2C_entityScale;
} SpriteActorState;

/* Task pair followed by a sprite actor (Noah: sSavePointMeshAbstract), 0xEC bytes. */
typedef struct {
    /* 0x00 */ TaskHeader m0_updateTask;
    /* 0x1C */ TaskHeader m1C_drawTask;
    /* 0x38 */ SpriteActorCore m38_actor;
} SpriteTask;

/* Camera eye and target that camera-target sprites follow (battle writes them). */
extern FP_VEC3 previousCameraEye2;
extern FP_VEC3 previousCameraAt2;

/* shape_transfer.c */
void allocateShapeTransfert(s32 size, s32 flags);

/* sprite_actor.c */
void setCurrentAnimationPtr(SpriteActorCore* actor, u16* anim);
void allocateSpriteVramLocations(SpriteActorCore* actor, s32 count, s32 flag);
void spriteActorSetCallback(SpriteActorCore* actor, void (*callback)(SpriteActorCore*));

/* Functions some PS1 callers declare their own way (unmasked int
 * arguments): the PC sees the definitions' prototypes here, the callers keep
 * their views under #ifndef PLATFORM_PC. */
void uploadNpcSpriteSheet(s32* data, s32 x, s16 y); /* sprite_actor.c */

/* Sprite bytecode ops 0x40-0x7F set no frame count, and both interpreters
 * (executeSpriteBytecode2, executeSpriteBytecode) add their n unset: on the PS1
 * that is $s3, the caller's, which holds 0xFFFC0000 all through the field's
 * sprite tick (PCSX-Redux, US field 592: actor 9's animation 6 starts with op
 * 0x50, the wait becomes 0x8000 at time scale 96, one tick makes it 0x7FFF and
 * the pose holds until the next animation; Beetle PSX the same). */
#define PC_SPRITE_UNSET_COUNT ((s32)0xFFFC0000)

#endif /* MAIN_SPRITE_H */
