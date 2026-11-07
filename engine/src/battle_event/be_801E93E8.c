/* Sprite-actor helpers for event actors (US build): 0x801E93E8-0x801E9B5C;
 * gcc-2.7.2-cdk, ASPSX 2.56 (split %hi addresses). JP builds its own version
 * of this tail with the rest of the overlay (be_801E9420.c). */
#include "common.h"
#include "psyq.h"
#include "main/sprite.h"
#include "main/task.h"
#include "main/memory.h"
#include "battle/battle.h"
#include "battle_event/battle_event.h"

extern s32 allocateJumpAnimationStructVar0;
extern void* battleAnimationLoadingDest;
extern u8 fxFragmentLoaded;
extern SpriteActorCore* pSpriteCoreListHead[];
extern s16 numActiveSpriteCores;
extern s32 allocateSavePointMeshDataSub0_var0;
extern u8 spriteBytecode2ExtendedE0_Var0;
extern u8 battleCameraVar0;

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
void startSpriteDistanceWatch(SpriteActorCore* actor, s32 minDistance, void (*callback)(SpriteActorCore*));
void abilityPlayAnimation(s32 value, SpriteActorCore* actor);
s32 isDamageDisplayPolysTask2Running(void);
void battleRender(void);
void startJumpAnimationCallback(SpriteActorCore*);
void startReadingBattleJumpAnimation(SpriteActorCore*);
void startJumpAnimationSub0(s32);
void setBattleCameraMode(s32);
void battleSpriteUpdate(TaskHeader*);
void battleSpriteRender(TaskHeader*);
void battleSpriteDelete(TaskHeader*);

void battleEventSpriteMoveDoneCallback(SpriteActorCore* actor);
void battleEventRestoreIdleAfterMove(SpriteActorCore* actor);

/* 0x801E93E8 (split into be_801E5160.c for now): the sprite callback that
 * clears the actor's battleEventVar0->m804 "moving" flag. */
void battleEventSpriteMoveDoneCallback(SpriteActorCore* actor) {
    battleEventVar0->m804_isMoving[BATTLE_SLOT(actor)] = 0;
    spriteActorSetCallback(actor, NULL);
}

void battleEventSetSpriteCameraAnim(s32 slot, s32 anim) {
    SpriteActorCore* actor;

    allocateJumpAnimationStructVar0 = 1;
    actor = battleBssBlock.m8C8C_spriteActorCores[slot];
    if (anim < 0 && fxFragmentLoaded == 0 && battleAnimationLoadingDest != NULL) {
        battleIdleDuringLoading();
        actor->m50_soundEffectSeq = loadFragmentIsNotLoadedYet();
        spriteActorSetSpecialAnimation(actor, battleAnimationLoadingDest);
    }
    spriteActorSetPlayingAnimation(actor, anim);
    spriteActorSetCallback(actor, battleEventSpriteMoveDoneCallback);
    if (anim == 1) {
        actor->mB0_x0_animationId = anim;
    } else {
        actor->mB0_x0_animationId = -1;
    }
}

void battleEventHideSpriteEntity(s32 slot) {
    SpriteActorCore* actor = battleBssBlock.m8C8C_spriteActorCores[slot];
    spriteActorSetPlayingAnimation(actor, (s8)actor->mAF_animationId);
}

void battleEventShowSpriteEntity(s32 slot) {
    SpriteActorCore* actor = battleBssBlock.m8C8C_spriteActorCores[slot];
    actor->m9E_wait = 0;
    actor->m34_currentSpriteFrame = 0;
    actor->m40_draw.raw &= 0xFFFFFF03;
}

void battleEventSetSpriteFlip(s32 slot) {
    battleBssBlock.m8C8C_spriteActorCores[slot]->m9E_wait = 0;
}

void battleEventRestoreIdleAfterMove(SpriteActorCore* actor) {
    s32 anim = actor->mB0_x0_animationId;
    if (anim < 0) {
        anim = 1;
    }
    spriteActorSetPlayingAnimation(actor, anim);
}

void battleEventStartSpriteMove(s16 slot, s16 x, s16 y, s16 z) {
    SpriteActorCore* actor = battleBssBlock.m8C8C_spriteActorCores[slot];
    actor->mA0_destination.vx = x;
    actor->mA0_destination.vy = y;
    actor->mA0_destination.vz = z;
    spriteActorSetDirection(actor, getRatan2ToDest(actor));
    setSpriteActorAngle(actor, getRatan2ToDest(actor));
    spriteActorSetPlayingAnimation(actor, 2);
    startSpriteDistanceWatch(actor, 8, battleEventRestoreIdleAfterMove);
    spriteActorSetCallback(actor, battleEventSpriteMoveDoneCallback);
}

void battleEventStartSpriteMoveAlt(s16 slot, s16 x, s16 y, s16 z) {
    SpriteActorCore* actor = battleBssBlock.m8C8C_spriteActorCores[slot];
    actor->mA0_destination.vx = x;
    actor->mA0_destination.vy = y;
    actor->mA0_destination.vz = z;
    spriteActorSetPlayingAnimation(actor, 3);
    spriteActorSetCallback(actor, battleEventSpriteMoveDoneCallback);
}

void battleEventSwapSpritePrepare(s32 slot, s32 value) {
    SpriteActorCore* actor = battleBssBlock.m8C8C_spriteActorCores[slot];
    if ((u16)actor->m34_currentSpriteFrame == 0) {
        actor->mAF_animationId = 0;
    } else {
        actor->mAF_animationId = 0x11;
    }
    abilityPlayAnimation(value, actor);
}

void battleEventSwapSpriteCommit(s32 dst, s32 src) {
    SpriteActorCore* actor = battleBssBlock.m8C8C_spriteActorCores[dst];
    u16 mask = 0xFFFF;

    currentJumpAnimationBitMask = mask;
    numActiveSpriteCores = countBattleActorCoresActiveForBitmask(mask, pSpriteCoreListHead, actor);
    if (numActiveSpriteCores == 0) {
        pSpriteCoreListHead[0] = actor;
    }
    actor->m74_targetEntitySprite = pSpriteCoreListHead[0];
    while (allocateSavePointMeshDataSub0_var0 != isDamageDisplayPolysTask2Running()) {
        battleRender();
    }
    allocateSavePointMeshDataSub0_var0 = 0;
    spriteBytecode2ExtendedE0_Var0 = 1;
    if (initFxFragments()) {
        spriteActorSetCallback(actor, startJumpAnimationCallback);
    } else {
        if ((u16)actor->m34_currentSpriteFrame != 0) {
            spriteActorSetPlayingAnimation(actor, 0x12);
        }
        setCameraVisibleEntities(currentJumpAnimationBitMask);
    }
    while (allocateSavePointMeshDataSub0_var0 != isDamageDisplayPolysTask2Running()) {
        battleRender();
    }
    allocateSavePointMeshDataSub0_var0 = 0;
    spriteBytecode2ExtendedE0_Var0 = 0;
}

void battleEventStartCharacterSpriteLoad(s32 slot, u16 src) {
    SpriteActorCore* actor = battleBssBlock.m8C8C_spriteActorCores[slot];
    SpriteActorCore** list = pSpriteCoreListHead;
    SpriteActorCore* target = battleBssBlock.m8C8C_spriteActorCores[src];

    list[1] = NULL;
    pSpriteCoreListHead[0] = target;
    actor->m74_targetEntitySprite = target;
    setSpriteActorAngle(actor, spriteActorCoreRatan2(actor, target));
    spriteActorSetDirection(actor, spriteActorCoreRatan2(actor, target));
    if (slot < 3) {
        startReadingBattleJumpAnimation(actor);
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
    battleCameraVar0 = 1;
    return task;
}

void battleEventDestroySpriteTaskCallback(TaskHeader* task) {
    registerSpriteCallback2_2(task);
    registerSpriteCallback2Sub0(&((TaskHeaderPair*)task)->m1C_drawTask);
    allocateSavePointMeshDataSub0_callback(task);
    freeHeap(task);
    startJumpAnimationSub0(1);
    setBattleCameraMode(1);
    battleCameraVar0 = 0;
}

void battleEventResetSpriteDisposeState(void) {
    startJumpAnimationSub0(0);
    setBattleCameraMode(0);
    battleCameraVar0 = 1;
}
