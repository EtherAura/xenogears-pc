/* JP build of the sprite-actor helpers for event actors: 0x801E9420 to the
 * end of .text; gcc-2.6.3-psx, ASPSX 2.34 like the rest of JP's overlay. */
#include "common.h"
#include "psyq.h"
#include "main/sprite.h"
#include "main/task.h"
#include "main/memory.h"
#include "battle/battle.h"
#include "battle_event/battle_event.h"

/* JP's symbol files name these battle functions and data by address only;
 * the PC links them under the US names the battle defines (data inside the
 * battle BSS block: port/src/game/battle/bss.c aliases). */
#define D_800CC25C D_800CCB3C
#define D_800C2D44 allocateJumpAnimationStructVar0
#define D_800C2D50 battleAnimationLoadingDest
#define D_800D2D98 numActiveSpriteCores
#define D_800C2F00 battleCameraVar0
#define D_800B90F4 startJumpAnimationCallback
#define func_800BE6FC spriteActorCoreRatan2
#define func_800BEF9C startSpriteDistanceWatch
#define func_800BE694 countBattleActorCoresActiveForBitmask
#define func_800BEEF0 isDamageDisplayPolysTask2Running
#define func_800BDEA8 battleRender
#define func_800B740C initFxFragments
#define func_800BBAB0 setCameraVisibleEntities
#define func_800BEA94 startReadingBattleJumpAnimation

extern SpriteActorCore* D_800CC25C[];  /* battleSpriteActorCores (US D_800CCB3C) */
extern s32 D_800C2D44;                 /* US allocateJumpAnimationStructVar0 */
extern void* D_800C2D50;               /* US battleAnimationLoadingDest */
extern u8 fxFragmentLoaded;
extern SpriteActorCore* pSpriteCoreListHead[];
extern s16 D_800D2D98;                 /* US numActiveSpriteCores */
extern s32 allocateSavePointMeshDataSub0_var0;
extern u8 spriteBytecode2ExtendedE0_Var0;
extern u8 D_800C2F00;                  /* US battleCameraVar0 */

void spriteActorSetPlayingAnimation(SpriteActorCore* actor, s32 animationId);
void spriteActorSetSpecialAnimation(SpriteActorCore* actor, void* anim);
void spriteActorSetDirection(SpriteActorCore* actor, s32 direction);
void setSpriteActorAngle(SpriteActorCore* actor, s16 angle);
void setAnimationBundle(SpriteActorCore* actor, void* bundle);
void setGraphicEntityScale(SpriteActorCore* actor, s32 scale);
void initFieldEntitySub4Sub1(SpriteActorCore* actor);
void initFieldEntitySub4Sub2(SpriteActor* actor);
TaskHeader* allocateTaskPair(s32 size, TaskHeader* owner, void (*update)(TaskHeader*), void (*draw)(TaskHeader*), void (*del)(TaskHeader*));
void registerSpriteCallback2_2(TaskHeader* task);
void registerSpriteCallback2Sub0(TaskHeader* task);
void allocateSavePointMeshDataSub0_callback(TaskHeader* task);
void battleIdleDuringLoading(void);
u8* loadFragmentIsNotLoadedYet(void);
s16 getRatan2ToDest(SpriteActorCore* actor);
s16 func_800BE6FC(SpriteActorCore* a, SpriteActorCore* b);   /* US spriteActorCoreRatan2 */
void func_800BEF9C(SpriteActorCore* actor, s32 minDistance, void (*callback)(SpriteActorCore*)); /* US startSpriteDistanceWatch */
void abilityPlayAnimation(s32 value, SpriteActorCore* actor);
s32 func_800BE694(u16 mask, SpriteActorCore** list, SpriteActorCore* target); /* US countBattleActorCoresActiveForBitmask */
s32 func_800BEEF0(void);               /* US isDamageDisplayPolysTask2Running */
void func_800BDEA8(void);              /* US battleRender */
void D_800B90F4(SpriteActorCore*);     /* US startJumpAnimationCallback */
void func_800BEA94(SpriteActorCore*);  /* US startReadingBattleJumpAnimation */
void startJumpAnimationSub0(s32);
void setBattleCameraMode(s32);
void battleSpriteUpdate(TaskHeader*);
void battleSpriteRender(TaskHeader*);
void battleSpriteDelete(TaskHeader*);

void battleEventSpriteMoveDoneCallback(SpriteActorCore* actor);
void battleEventRestoreIdleAfterMove(SpriteActorCore* actor);

void battleEventSetSpriteCameraAnim(s32 slot, s32 anim) {
    SpriteActorCore* actor = D_800CC25C[slot];

    D_800C2D44 = 1;
    if (anim < 0 && fxFragmentLoaded == 0 && D_800C2D50 != NULL) {
        battleIdleDuringLoading();
        actor->m50_soundEffectSeq = loadFragmentIsNotLoadedYet();
        spriteActorSetSpecialAnimation(actor, D_800C2D50);
    }
    spriteActorSetPlayingAnimation(actor, anim);
    spriteActorSetCallback(actor, battleEventSpriteMoveDoneCallback);
    if (anim == 1) {
        actor->mB0_x0_animationId = 1;
    } else {
        actor->mB0_x0_animationId = -1;
    }
}

void battleEventHideSpriteEntity(s32 slot) {
    SpriteActorCore* actor = D_800CC25C[slot];
    spriteActorSetPlayingAnimation(actor, (s8)actor->mAF_animationId);
}

void battleEventShowSpriteEntity(s32 slot) {
    SpriteActorCore* actor = D_800CC25C[slot];
    actor->m9E_wait = 0;
    actor->m34_currentSpriteFrame = 0;
    actor->m40_draw.raw &= 0xFFFFFF03;
}

void battleEventSetSpriteFlip(s32 slot) {
    D_800CC25C[slot]->m9E_wait = 0;
}

void battleEventRestoreIdleAfterMove(SpriteActorCore* actor) {
    s32 anim = actor->mB0_x0_animationId;
    if (anim < 0) {
        anim = 1;
    }
    spriteActorSetPlayingAnimation(actor, anim);
}

void battleEventStartSpriteMove(s16 slot, s16 x, s16 y, s16 z) {
    SpriteActorCore* actor = D_800CC25C[slot];
    actor->mA0_destination.vx = x;
    actor->mA0_destination.vy = y;
    actor->mA0_destination.vz = z;
    spriteActorSetDirection(actor, getRatan2ToDest(actor));
    setSpriteActorAngle(actor, getRatan2ToDest(actor));
    spriteActorSetPlayingAnimation(actor, 2);
    func_800BEF9C(actor, 8, battleEventRestoreIdleAfterMove);
    spriteActorSetCallback(actor, battleEventSpriteMoveDoneCallback);
}

void battleEventStartSpriteMoveAlt(s16 slot, s16 x, s16 y, s16 z) {
    SpriteActorCore* actor = D_800CC25C[slot];
    actor->mA0_destination.vx = x;
    actor->mA0_destination.vy = y;
    actor->mA0_destination.vz = z;
    spriteActorSetPlayingAnimation(actor, 3);
    spriteActorSetCallback(actor, battleEventSpriteMoveDoneCallback);
}

void battleEventSwapSpritePrepare(s32 slot, s32 value) {
    SpriteActorCore* actor = D_800CC25C[slot];
    if ((u16)actor->m34_currentSpriteFrame == 0) {
        actor->mAF_animationId = 0;
    } else {
        actor->mAF_animationId = 0x11;
    }
    abilityPlayAnimation(value, actor);
}

void battleEventSwapSpriteCommit(s32 dst, s32 src) {
    SpriteActorCore* actor = D_800CC25C[dst];

    currentJumpAnimationBitMask = 0xFFFF;
    D_800D2D98 = func_800BE694(0xFFFF, pSpriteCoreListHead, actor);
    if (D_800D2D98 == 0) {
        pSpriteCoreListHead[0] = actor;
    }
    actor->m74_targetEntitySprite = pSpriteCoreListHead[0];
    while (allocateSavePointMeshDataSub0_var0 != func_800BEEF0()) {
        func_800BDEA8();
    }
    allocateSavePointMeshDataSub0_var0 = 0;
    spriteBytecode2ExtendedE0_Var0 = 1;
    if (func_800B740C()) {
        spriteActorSetCallback(actor, D_800B90F4);
    } else {
        if ((u16)actor->m34_currentSpriteFrame != 0) {
            spriteActorSetPlayingAnimation(actor, 0x12);
        }
        func_800BBAB0(currentJumpAnimationBitMask);
    }
    while (allocateSavePointMeshDataSub0_var0 != func_800BEEF0()) {
        func_800BDEA8();
    }
    allocateSavePointMeshDataSub0_var0 = 0;
    spriteBytecode2ExtendedE0_Var0 = 0;
}

void battleEventStartCharacterSpriteLoad(s32 slot, u16 src) {
    SpriteActorCore* actor = D_800CC25C[slot];
    SpriteActorCore* target = D_800CC25C[src];

    pSpriteCoreListHead[1] = NULL;
    pSpriteCoreListHead[0] = target;
    actor->m74_targetEntitySprite = target;
    setSpriteActorAngle(actor, func_800BE6FC(actor, target));
    spriteActorSetDirection(actor, func_800BE6FC(actor, target));
    if (slot < 3) {
        func_800BEA94(actor);
    }
}

void battleEvent_setSpriteAnimation(sBattleSpriteActor* task, s32 anim) {
    spriteActorSetPlayingAnimation(&task->m38_spriteActor.m0_spriteActorCore, anim);
}

sBattleSpriteActor* battleEvent_createSpriteEntity(void* bundle, SVECTOR* pos) {
    s32 saved = allocateSavePointMeshDataSub0_var0;
    sBattleSpriteActor* task = (sBattleSpriteActor*)allocateTaskPair((s32)sizeof(sBattleSpriteActor), NULL, battleSpriteUpdate, battleSpriteRender, battleSpriteDelete);
    SpriteActor* actor = &task->m38_spriteActor;

    allocateSavePointMeshDataSub0_var0 = saved;
    task->m0_updateTask.m14_ownerId.m31 = 0;
    actor->m0_spriteActorCore.m6C_owner = task;
    task->m0_updateTask.m4_data = actor;
    task->m1C_drawTask.m4_data = actor;
    initFieldEntitySub4Sub1(&actor->m0_spriteActorCore);
    initFieldEntitySub4Sub2(actor);
    actor->m0_spriteActorCore.m48_defaultAnimationBundle = bundle;
    actor->m0_spriteActorCore.m40_draw.bits.xD_mode = 4;
    actor->m0_spriteActorCore.m3C_x0_type = 0;
    setAnimationBundle(&actor->m0_spriteActorCore, bundle);
    actor->m0_spriteActorCore.m0_position.vx = pos->vx << 16;
    actor->m0_spriteActorCore.m0_position.vy = pos->vy << 16;
    actor->m0_spriteActorCore.m0_position.vz = pos->vz << 16;
    actor->m0_spriteActorCore.mB0_x0_animationId = 0;
    actor->m0_spriteActorCore.m32_direction = 0;
    setGraphicEntityScale(&actor->m0_spriteActorCore, 0x2000);
    actor->m0_spriteActorCore.m82_entityScale = 0x2000;
    actor->m0_spriteActorCore.m9E_wait = 0;
    startJumpAnimationSub0(0);
    setBattleCameraMode(0);
    D_800C2F00 = 1;
    return task;
}

void battleEventDestroySpriteTaskCallback(TaskHeader* task) {
    registerSpriteCallback2_2(task);
    registerSpriteCallback2Sub0(&((TaskHeaderPair*)task)->m1C_drawTask);
    allocateSavePointMeshDataSub0_callback(task);
    freeHeap(task);
    startJumpAnimationSub0(1);
    setBattleCameraMode(1);
    D_800C2F00 = 0;
}

void battleEventResetSpriteDisposeState(void) {
    startJumpAnimationSub0(0);
    setBattleCameraMode(0);
    D_800C2F00 = 1;
}
