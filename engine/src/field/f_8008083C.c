
#include "common.h"
#include "pc_retail_heap.h"
#include "pc_slide.h"
#include "psyq.h"
#include "main/memory.h"
#include "main/filesystem.h"
#include "main/sprite.h"
#include "main/sound.h"
#include "main/debug_font.h"
#include "field/field.h"
#include "gte.h"
#include "pc_gameplay.h"

/* "ERROR ID0 ACT=%d\n", this file's last string: it stays assembly because
 * retail pads it with two stray bytes ("ot") instead of zeros. */
extern char strErrorId0Act[];

/* Integer part of a 16.16 fixed-point value (its upper halfword). */
#define FP_INT(x) (((s16*)&(x))[1])

typedef struct {
    s32 m0[0x800 / 4];
} FieldSector; /* one CD sector */

/* Noah: sUpdateCameraAtStruct (the three nearest sound-emitting actors) */
typedef struct {
    /* 0x00 */ s32 m0_distance[4];
    /* 0x10 */ s32 m10_soundId[4];
    /* 0x20 */ s32 m20_scale[4];
    /* 0x30 */ s32 m30_slotInUse[4];
    /* 0x40 */ s32 m40_alreadyPlaying[4];
    /* 0x50 */ s32 m50_actorId[4];
    /* 0x60 */ SVECTOR m60_delta[3];
} sUpdateCameraAtStruct; /* 0x78 */

/* Noah: sFollowStruct (party follow history, followDataTable[32] at 0x800B14F0) */
typedef struct {
    /* 0x00 */ u32 m0_flags;
    /* 0x04 */ u32 m4_flags;
    /* 0x08 */ SVECTOR m8_pos;
    /* 0x10 */ s16 m10_maxY;
    /* 0x12 */ s16 m12_animationId;
    /* 0x14 */ s16 m14_rotation;
    /* 0x16 */ s16 m16_walkMeshTriangles[4];
    /* 0x20 */ VECTOR m20_stepAndNormal[2]; /* Noah: m20 (actor step), m30 (surface normal) */
    /* 0x40 */ u32 m40_currentTriangleFlag;
    /* 0x44 */ u8 m44_walkmeshId;
} sFollowStruct; /* 0x48 */

/* Noah: sEntityMoveCheck0Sub1 (scratch data of CheckCollisionWithMesh) */
typedef struct {
    /* 0x00 */ s32 m0_gteXY[4];
    /* 0x10 */ s32 m10_testPointXY;
    /* 0x14 */ SVECTOR m14_transformedVertices[4];
    /* 0x34 */ SVECTOR m34_testPosition;
    /* 0x3C */ s32 m3C_clipFlag;
    /* 0x40 */ MATRIX m40_transformMatrix;
    /* 0x60 */ MATRIX m60_modelMatrix;
    /* 0x80 */ MATRIX m80_viewMatrix;
    /* 0xA0 */ s32 mA0_smallestY;
    /* 0xA4 */ SVECTOR* mA4_vertices;
    /* 0xA8 */ s32 mA8;
    /* 0xAC */ u32 mAC_primType;
    /* 0xB0 */ SVECTOR mB0_rotation;
} sEntityMoveCheck0Sub1; /* 0xB8 */

/* Noah: sDynamicVerticesData (partial) */
typedef struct {
    /* 0x00 */ s32 (*m0_getScaleFunction)(void);
    /* 0x04 */ u8 m4[0x1C];
} sDynamicVertexData; /* 0x20 */

/* Noah: sDynamicVertices (partial; main/mime.c DynVertices) */
typedef struct {
    /* the block and its original vertices and normals: three pointers here,
     * so the count and the data sit where DynVertices has them */
    void* m0[3];
    /* 0x0C */ s32 mC_numDynamicVertices;
    /* 0x10 */ sDynamicVertexData* m10_perVertexData;
} sDynamicVertices;

typedef struct {
    u16 m0_time;
    u16 m2_value;
} FieldSeqEvent;

typedef struct {
    u8 m0[0x14];
    u16 m14;
} FieldSequence;

/* Positional sound slots (3 entries, channel = slot * 2): m0 = actor id,
 * m2 = sound id, 0xFFFF = free. */
typedef struct {
    /* 0x0 */ u16 m0_actorId;
    /* 0x2 */ u16 m2_soundId;
    /* 0x4 */ s16 m4;
} FieldSoundSlot;

s32 distance3d(s32 x, s32 y, s32 z);
extern s32 playMusicVar3;      /* Noah: playMusicVar3 (sectors of the music bank loaded) */
extern u8* musicLoadBuffer;
extern s32 fieldMusicLoadPending;
extern s32 currentlyLoadedWave;
extern s32 musicVar2;
extern s32 isMusicLoadingStartPending;
s32 releaseSectorSlot(u8* sector);
s32 sendSamples2(u8* src, s32 size);
void resetBattleAudioState(void);
void loadMusicPhase0(s32 fileIndex, s32 flags, void (*callback)(u8*));
void loadMusicPhase0UploadData(u8* sector);
void playMusicSub(void);
extern s32 updateEntityEventCode3Var1;
extern s32 updateEntityEventCode3Var2;
/* Noah: musicLookupTable (wave, flags). Full extent: the flags byte (+1) was once addressed as D_800ADFCD, the same
 * table from +1, and the PC must keep the two one object. */
extern u8 musicLookupTable[74][2];
extern u8 musicLookupFlags[]; /* &musicLookupTable[0][1]: the flags column, addressed on its own (retail codegen; D_800ADFCD) */
extern s32 needReloadWds;
extern s32 currentlyLoadedMusic;
extern u8 battleMusic[0x3200]; /* Noah: battleMusic (sequence buffer, up to fieldMouseState) */
extern s32 updateMusicState2Var1;
extern s32 updateMusicState2Var2;
extern s32 musicVar1;
extern s32 needStartMusicInstanceSub0;
extern s32 updateMusicState2Var3;
s32 updateMusicLoadingState(void);
s32 finishReloadingResidentWds(void);
void startReloadingResidentWds(void);
void* createMusicInstance(void* seq);
void startMusicInstance(void* inst, s32 vol, s32 arg2);
void resumeMusicInstance(void* inst, s32 vol, s32 arg2);
extern sFollowStruct followDataTable[32];
extern u8 noUpdatesToPartyMembers;
extern s32 entityUpdateVar0;
extern s32 mechaDeltaTranslationIndex;
s32 updateEntityEventCode3Sub0(sFieldScriptEntity* pEntity);
void exectueEntitiesUpdateFunction(void);
void func_80281B00(const char* name);      /* debug overlay: logFieldRenderingEvent */
void updateEntityEventCode2(s32 index, sFieldEntity* pEntity, sFieldScriptEntity* pScript);
void updateEntityEventCode3(s32 index, sFieldEntity* pEntity, sFieldScriptEntity* pScript);
void EntityMoveCheck0(s32 index, sFieldEntity* pEntity, sFieldScriptEntity* pScript);
s32 EntityMoveCheck1(s32 index, s32 maxAltitude, sFieldEntity* pEntity, sFieldScriptEntity* pScript, u32 arg4);
void startScriptsForCollisions(s32 index, sFieldEntity* pEntity, sFieldScriptEntity* pScript);
void updatePartyFollowLeader(void);
extern s16 fieldRandomBattleVar;
extern u8 g_PartyFollowDisabled;
void* allocateScratchBufferLocationForMoveCheck(s32 size);            /* Noah: allocateScratchBufferLocationForMoveCheck (words) */
void freeScratchBuffer(s32 size);             /* Noah: freeScratchBuffer (words) */
void resetInputs(void);
s32 CheckCollisionWithMesh(s32 actorId, ModelBlock* pModel, s32 stepX, s32 stepZ, s32* outputY, VECTOR* outputNormal);
extern s32 EntityMoveCheck0Var0;
void createRotationMatrix(SVECTOR* rot, MATRIX* m);  /* RotMatrix */
MATRIX* MulMatrix2(MATRIX* m0, MATRIX* m1);
void getTriangleNormalAndAdjustY(SVECTOR* v0, SVECTOR* v1, SVECTOR* v2, SVECTOR* pos, VECTOR* normal);
extern MATRIX renderModelRotationMatrix;
extern s32 numInitializedFieldScriptEntities[];
void initCharacterShadowPoly(void* p2dSprite);
void spriteActorSetDirection(SpriteActorCore* actor, s32 direction);
s32 getAngleCos(s32 angle); /* cos */
s32 getAngleSin(s32 angle); /* sin */
extern u16 padButtonForDialogs;
extern s16 windowOpenBF;
extern s32 startScriptsForCollisionsVar0;
extern s32 fieldDebugLinesVisible;      /* debug-build flag (outside retail RAM) */
u16 getScriptEntryPoint(s32 entityId, s32 scriptIndex);
extern u16 inputAllowedMask;
s32 EntityMoveCheck1Sub1(sFieldScriptEntity* pEntity, s32 walkmesh, s32* altitude, VECTOR* normal, s16* triangle, s32* altitude2);
/* Sound / music loading (main kernel sound library) */
void StopSoundEffect(s32);
/* The field overlay's view of PlaySoundEffect2 has s16 parameters (the caller
 * sign-extends; channel*2 is narrowed, which keeps CSE from sharing it with
 * the StopSoundEffect argument). */
void OP_C4SubSub1(s32 id, s16 volume, s16 pan, s32 channel);
void stopAllSounds(void);
void unloadSequence(void*);
void loadSequence(void*);
void playSoundEffectOnChannel(s32, s32);
u8* getNextStreamedSector(void);           /* next streamed CD sector */
s32* allocateBufferForVramUpload(s32 count, s32 flags); /* allocate a sector stream buffer */
void softReboot(void);
extern s32 loadCompleted;
extern s32* musicLoadBuffer2;            /* music stream buffer (Noah: musicLoadBuffer2) */
extern u8* lastStreamedMusicSector;
extern void (*musicLoadedCallback)(u8*);
extern FieldSeqEvent movieSoundEventTable[];
extern s32 movieSoundEventIndex;
extern s32 fieldMovieCurrentFrame;
extern FieldSequence* D_800B235C;
extern void* fieldSeq;
extern s32 loadFieldSeqVar0;
extern void* loadFieldSeqVar1;
extern void* residentWdsBuffer;
extern s32 playMusicSubVar2;
extern s16 playWorldmapMusic;
extern s32 playMusicSubVar0;
extern FieldSoundSlot actorSoundSlots[3];
extern FieldBss_800AF880 fieldSceneState;
void setSoundEffectChannelVolume(s32 channel, s32 volume);
void setSoundEffectChannelPan(s32 channel, s32 pan);
void getActorScreenPosition(s32 actorId, s32* outX, s32* outY);
void updateCameraAt(VECTOR* at);
void computeMechaPointLightColor(s32 slot, VECTOR* position);
extern s16 cameraTrackingMode;
extern s32 g_playerControlledActor;
extern void (*fieldScriptOpcodes_EX[])(void);
void resetParticleCreationSetup(s32);
s32 createParticleEffect(s32 actorId);
extern s32 g_breakCurrentScript;
void deleteFieldEntitySub4(SpriteActorCore* actor);
s32 length2d(s32 x, s32 z);
void updateEntityEventCode3Sub1(SpriteActorCore* actor, s32 rotation, sFieldEntity* pEntity);
void setVisualAnimation(SpriteActorCore* actor, s32 animationId, sFieldEntity* pEntity);
s32 findCharacterInParty(s32 character);
void spriteActorSetPlayingAnimation(SpriteActorCore* actor, s32 animationId);
extern FieldBss_800B2184 op9DVar1;
extern u8 updateEntityEventCode4Table0[];     /* Noah: updateEntityEventCode4Table0 (mecha animation map) */
void func_80281678(sFieldScriptEntity* pEntity); /* debug overlay */
extern s32 fieldDebugDisable;
void resetFieldScriptEntityValues(s32 index);
s32 fieldModelDynamicVerticesUpdate(void);
extern s16 spriteJumpDuration;
extern s32 fieldModelDynamicVerticesUpdate_actorIndex;
extern s32 fieldModelDynamicVerticesUpdate_nodeIndex;

void freeFieldEntity(s32 index) {
    sFieldScriptEntity* pScriptEntity;

    if (index < g_totalActors) {
        pScriptEntity = D_800AFB10[index].m4C_scriptEntity;
        if (pScriptEntity->m134_x7) {
            freeHeap(pScriptEntity->m110_modelRotation2);
        }
        if (pScriptEntity->m12C_xC) {
            freeHeap(pScriptEntity->m114_movementBoundingZone);
        }
        if (D_800AFB10[index].m58_flags & 0x2000) {
            freeHeap(pScriptEntity->m118_deformationStrength);
        }
        if (pScriptEntity->m124_special2dAnimationId != -1) {
            freeHeap(pScriptEntity->m120_special2dAnimation);
        }
        freeHeap(pScriptEntity);
        freeHeap(D_800AFB10[index].m8_2dSprite);
        deleteFieldEntitySub4(D_800AFB10[index].m4_pVramSpriteSheet);
    }
}

s32 getWalkmeshTriangleFlag(sFieldScriptEntity* pEntity) {
    s32 walkmeshId = (s16)pEntity->m10_walkmeshId;

    if ((pEntity->m4_flags >> (walkmeshId + 3)) & 1) {
        return 0;
    }
    return walkMeshVar1[walkMeshTriangle[walkmeshId][pEntity->m8_currentWalkMeshTriangle[walkmeshId]].mC_indexInWalkmeshData1];
}

s32 computeSpriteJumpHeight(SpriteActorCore* actor) {
    s32 speed = -0x14D000;
    s32 gravity = actor->m1C_gravity;
    s32 height = 0;

    spriteJumpDuration = 0;
    do {
        height += speed;
        spriteJumpDuration += 2;
        speed += gravity;
    } while (speed <= 0);
    return height >> 16;
}

s32 fieldModelDynamicVerticesUpdate(void) {
    return D_800AFB10[fieldModelDynamicVerticesUpdate_actorIndex].m4C_scriptEntity->m118_deformationStrength[fieldModelDynamicVerticesUpdate_nodeIndex++];
}

void resetFieldScriptEntityValues(s32 index) {
    VECTOR normals[4];
    SVECTOR positions[4];
    sFieldScriptEntity* pScript = D_800AFB10[index].m4C_scriptEntity;
    s32 i;

    pScript->m0_fieldScriptFlags = 0xB0;
    pScript->m4_flags = 0x800;
    pScript->m18_boundingVolume[0] = 0x10;
    pScript->m18_boundingVolume[1] = 0x60;
    pScript->m18_boundingVolume[2] = 0x10;
    pScript->m74_touchedActor = 0xFF;
    pScript->m40_externalStep.vx = 0;
    pScript->m40_externalStep.vy = 0;
    pScript->m40_externalStep.vz = 0;
    pScript->m30_stepVector.vx = 0;
    pScript->m30_stepVector.vy = 0;
    pScript->m30_stepVector.vz = 0;
    pScript->m60_interactionOffset.vz = 0;
    pScript->m60_interactionOffset.vx = 0;
    pScript->m60_interactionOffset.vy = 0;
    pScript->mD0_targetPositionOffset.vx = 0;
    pScript->mD0_targetPositionOffset.vy = 0;
    pScript->mD0_targetPositionOffset.vz = 0;
    pScript->mE6_idleAnimationId = 0;
    pScript->mEA_forcedAnimation = 0xFF;
    pScript->m75_parentActorId = 0xFF;
    pScript->mE2_doorFrame = 0;
    pScript->mCC_scriptPC = 0;
    pScript->m6E_stuckCounter = 0;
    pScript->m12C_x5_doorMoving = 0;
    pScript->m11E_rotationSpeed = 0x200;
    pScript->m1E_collisionRadius = pScript->m18_boundingVolume[0];
    pScript->m12C_x0 = 0;
    pScript->mFF_spriteColor2[2] = 0x80;
    pScript->mFF_spriteColor2[1] = 0x80;
    pScript->mFF_spriteColor2[0] = 0x80;
    pScript->mFC_spriteColor[2] = 0x80;
    pScript->mFC_spriteColor[1] = 0x80;
    pScript->mFC_spriteColor[0] = 0x80;
    pScript->m128_attachedMechaBone = 0xFFFF;
    pScript->m12C_x10 = 0;
    pScript->m130_x13 = 0;
    pScript->m130_x0 = 0;
    pScript->m130_x9 = 0;
    pScript->m12C_x12 = 0;
    for (i = 0; i < 8; i++) {
        pScript->m8C_scriptSlots[i].m4_x10_status = 0;
        pScript->m8C_scriptSlots[i].m4_x16 = 0;
        pScript->m8C_scriptSlots[i].m4_x17_walkMode = 0;
        pScript->m8C_scriptSlots[i].m4_x12_priority = 0xF;
        pScript->m8C_scriptSlots[i].m4_x0_numSteps = 0xFFFF;
        pScript->m8C_scriptSlots[i].m2_delay = 0;
        pScript->m8C_scriptSlots[i].m0_scriptPC = 0xFFFF;
        pScript->m8C_scriptSlots[i].m3_scriptIndex = 0xFF;
    }
    pScript->m120_special2dAnimation = NULL;
    pScript->mE4_playableCharacterId = 0xFF;
    pScript->m76_timeScale = 0x100;
    pScript->m82_dialogWindowSize[1] = 0;
    pScript->m82_dialogWindowSize[0] = 0;
    pScript->m88_dialogWindowPosition[1] = 0;
    pScript->m88_dialogWindowPosition[0] = 0;
    pScript->m84_dialogWindowFlags = 0;
    pScript->mCF_scriptSlotWaitedOn = 0;
    pScript->mCE_currentScriptSlot = 0;
    pScript->mE8_currentAnimationId = 0;
    pScript->m10_walkmeshId = 0;
    pScript->mEC_elevation = 0;
    pScript->m134_x7 = 0;
    pScript->m12C_x6_stackDepth = 0;
    pScript->m12C_x9 = 0;
    pScript->m12C_xC = 0;
    pScript->m134_x5 = 0;
    pScript->m102_numSteps = rand();
    pScript->mF4_scale3d[0] = 0x1000;
    pScript->mF4_scale3d[1] = 0x1000;
    pScript->mF4_scale3d[2] = 0x1000;
    pScript->m10D_soundMode = 0xFF;
    pScript->m80_dialogPortrait = 0xFF;
    pScript->m106_currentRotation = -0x8000;
    pScript->m104_rotation = -0x8000;
    pScript->m108_rotation3 = -0x8000;
    pScript->m124_special2dAnimationId = -1;
    pScript->mE3_pushCounter = 0;
    pScript->m8_currentWalkMeshTriangle[3] = 0;
    pScript->m8_currentWalkMeshTriangle[2] = 0;
    pScript->m8_currentWalkMeshTriangle[1] = 0;
    pScript->m8_currentWalkMeshTriangle[0] = 0;
    pScript->m12C_x2 = 0;
    for (i = 0; i < fieldSceneState.m2D4_numWalkMesh - 1; i++) {
        pScript->m8_currentWalkMeshTriangle[i] = findTriangleInWalkMesh((s16)D_800AFB10[index].mC_matrix.t[0], (s16)D_800AFB10[index].mC_matrix.t[2], i, &positions[i], &normals[i]);
        if (pScript->m8_currentWalkMeshTriangle[i] != -1 && pScript->m8_currentWalkMeshTriangle[i] >= (u32)g_walkMeshNumTrianglePerBlock[i]) {
            g_walkMeshNumTrianglePerBlock[i] = 0;
            normals[i].vx = 0;
            normals[i].vy = 0;
            normals[i].vz = 0;
            (positions + i)->vx = 0;
            (positions + i)->vy = 0;
            (positions + i)->vz = 0;
        }
    }
    pScript->m14_currentTriangleFlag = getWalkmeshTriangleFlag(pScript);
    pScript->m50_surfaceNormal.vx = (normals + pScript->m10_walkmeshId)->vx;
    pScript->m50_surfaceNormal.vy = (normals + pScript->m10_walkmeshId)->vy;
    pScript->m50_surfaceNormal.vz = (normals + pScript->m10_walkmeshId)->vz;
    if (!(fieldSceneState.m290_actorArray[index].m58_flags & 0x80)) {
        fieldSceneState.m290_actorArray[index].mC_matrix.t[1] = positions[pScript->m10_walkmeshId].vy;
    }
    pScript->m20_position.vx = fieldSceneState.m290_actorArray[index].mC_matrix.t[0] << 16;
    pScript->m20_position.vy = fieldSceneState.m290_actorArray[index].mC_matrix.t[1] << 16;
    pScript->m20_position.vz = fieldSceneState.m290_actorArray[index].mC_matrix.t[2] << 16;
    pScript->m72_elevation = fieldSceneState.m290_actorArray[index].mC_matrix.t[1];
}

void initFieldScriptEntity(s32 index) {
    u8 unused[0x60];
    s32 count = sizeof(sFieldScriptEntity) / 4;
    sFieldScriptEntity* pScript;
    sFieldEntitySub0* pSub0;
    u32* p;
    s32 i;

    if (index < g_totalActors) {
        numInitializedFieldScriptEntities[0]++;
        fieldSceneState.m290_actorArray[index].m4C_scriptEntity = allocateHeap(PC_HEAP_SIZED(sizeof(sFieldScriptEntity), 0x138), 0);
        p = (u32*)fieldSceneState.m290_actorArray[index].m4C_scriptEntity;
        for (i = 0; i < count; i++) {
            *p++ = 0;
        }
        /* The m5A store and the m4C read use the plain actorArray symbol, the
         * flags read the FieldBss_800AF880 aggregate: only the latter is
         * reloaded after the store, as retail does. */
        D_800AFB10[index].m5A_spriteFlags = 0;
        pScript = D_800AFB10[index].m4C_scriptEntity;
        if (fieldSceneState.m290_actorArray[index].m58_flags & 0x2000) {
            pSub0 = fieldSceneState.m290_actorArray[index].m0_model;
            pScript->m118_deformationStrength = allocateHeap(PC_HEAP_BYTES(0x80), 0);
            if (pSub0->m14_dynamicVertices != NULL) {
                for (i = 0; i < ((sDynamicVertices*)pSub0->m14_dynamicVertices)->mC_numDynamicVertices; i++) {
                    ((sDynamicVertices*)pSub0->m14_dynamicVertices)->m10_perVertexData[i].m0_getScaleFunction = fieldModelDynamicVerticesUpdate;
                    pScript->m118_deformationStrength[i] = 0;
                }
            }
        }
        resetFieldScriptEntityValues(index);
        fieldSceneState.m290_actorArray[index].m8_2dSprite = allocateHeap(PC_HEAP_SIZED(sizeof(FieldQuad), 0x70), 0);
        initCharacterShadowPoly(fieldSceneState.m290_actorArray[index].m8_2dSprite);
    }
}

void spriteActorSetTimeScale(SpriteActorCore* actor, s32 timeScale);
s32 pcFieldSubFrames(void); /* f_80072140.c */
extern u16 padButtonForScriptsPad1; /* f_80072140.c: the second pad's script presses */

/* Enhanced (xg-hbc.11): while the field runs two frames per native one, every
 * actor sprite's time scale is its entity's walk divisor at the field's rate
 * (animation waits, move speed and gravity are the sprite code's own dt
 * scaling), kept in step each frame as the schedule (dialogWindowFlag2) and
 * scripts (OP_SET_TIME_SCALE) change it. Nothing at the native rate. */
static s32 pcFieldSpriteScalesScaled; /* the sprites hold scaled values: restore them when the rate returns */
static void pcFieldSyncSpriteTimeScales(void) {
    s32 i;
    s32 scaled = pcFieldTimeScale(0x100) != 0x100;
    if (!scaled && !pcFieldSpriteScalesScaled) return;
    for (i = 0; i < g_totalActors; i++) {
        sFieldEntity* e = &fieldSceneState.m290_actorArray[i];
        if ((e->m58_flags & 0xF00) && e->m4_pVramSpriteSheet != NULL && e->m4C_scriptEntity != NULL) {
            SpriteActorCore* s = e->m4_pVramSpriteSheet;
            s32 oldScale = (s32)s->mAC_x7_timeScale;
            s32 newScale = pcFieldTimeScale((u16)e->m4C_scriptEntity->m76_timeScale);
            if (oldScale != newScale && oldScale != 0) {
                /* the sprite's gravity was computed from its time scale (setCurrentAnimationPtr,
                 * speed squared): keep it consistent with the scale it now carries */
                s->m1C_gravity = (s32)(((s64)s->m1C_gravity * oldScale * oldScale) / ((s64)newScale * newScale));
                /* xg-hbc.19.3: and its step (a frame's move, computed at the scale it had: the tick's
                 * native one), which a frame between's preview takes at its own scale */
                s->mC_step.vx = (s32)(((s64)s->mC_step.vx * oldScale) / newScale);
                s->mC_step.vy = (s32)(((s64)s->mC_step.vy * oldScale) / newScale);
                s->mC_step.vz = (s32)(((s64)s->mC_step.vz * oldScale) / newScale);
            }
            spriteActorSetTimeScale(s, newScale);
        }
    }
    pcFieldSpriteScalesScaled = scaled;
}



#define PC_FIELD_MAX_ACTORS 128
/* (xg-hbc.13's holds, which made the frame between repeat its tick's walk
 * speed and step verdict, are gone: since xg-hbc.19.3 the frame between is a
 * preview of the next native frame, pcFieldPreview* below, deciding from the
 * state that frame starts from.) */
static u8 pcFieldCulledAtTick[PC_FIELD_MAX_ACTORS]; /* xg-hbc.13: the tick frame's off-screen verdict, for the frame between */
/* The off-screen verdict (m4_flags 0x200, renderFieldCharacterSprites) an
 * actor's movement uses this frame: at 60 Hz the frame between uses its
 * tick's (see updateScriptAndMoveEntities). */
static s32 pcFieldActorCulled(s32 i, const sFieldScriptEntity* pScript) {
    s32 culled = (pScript->m4_flags & 0x600) == 0x200;
    if (pcFieldSubFrames() > 1 && i < PC_FIELD_MAX_ACTORS && pcFieldOffTick()) {
        culled = pcFieldCulledAtTick[i];
    }
    return culled;
}

void pcFieldScriptMoveBefore(void);
void pcFieldScriptMoveAfter(void);

/* ---- Enhanced (xg-hbc.19.3): the 60 Hz field's preview method ----
 * As the 60 Hz world map's (cf507b2): a frame between a native frame's tick
 * and the next is the first part of the next native frame, run from that
 * vblank's input: the actors' movement at 1/N of a native frame's step
 * (pcFieldTimeScale), the camera's carried parts. The tick puts the actors
 * back where the tick before left them (pcFieldPreviewRestore) and runs the
 * native frame whole at the native scales with every press of its vblanks
 * (f_80072140.c updateAllEntities), so positions, the off-screen verdicts and
 * everything the movement decides are the Original's at every tick, while a
 * press moves the leader on the vblank it arrives and motion stays even.
 * What the preview may change is kept at the first frame between's start
 * (after the native frame's second sprite step, pcFieldSpriteSteps): every
 * actor's entity, script entity (with its 3D rotation block), sprite (its
 * extra block and object), the party's follow trail, the slide-edge stack
 * words and the random seed. Everything else the preview leaves alone: the
 * script VM, the counters, the encounters and the camera's own state run on
 * the ticks as before. pcFieldPreviewSettle puts the native frame back before
 * the field leaves its loop (menus, battles, exits, the pause). */
#define PC_PREVIEW_ACTORS 96
typedef struct {
    sFieldEntity entity;
    sFieldScriptEntity script;
    s16 rotation2[6];
    SpriteActorCore sprite;
    SpriteActorExtra extra;
    SpriteObject object;
    const void* scriptPtr;
    const void* spritePtr;
    const void* extraPtr;
    const void* objectPtr;
    const void* rotation2Ptr;
} PcPreviewActor;
static PcPreviewActor pcPreviewActors[PC_PREVIEW_ACTORS];
static sFollowStruct pcPreviewFollow[32];
static s32 pcPreviewFollowMap[3];
static u32 pcPreviewSlide[4];
static u32 pcPreviewSeed;
static VECTOR pcPreviewCamTargets[2]; /* m30_cameraEye2, m40_cameraAt2 */
static s32 pcPreviewCamMoves[6];      /* the scripted moves' accumulators (target, eye start positions) */
static s32 pcPreviewScene[6];         /* scale, DIP, SCRZ accumulators and values */
static s32 pcPreviewCollision;
static s32 pcPreviewTans[3]; /* m10E_cameraTan, m10C_camera2Tan, cameraDeltaTan: the frame's, which the next one reads */
extern s32 cameraDeltaTan;
extern s32 cameraCollisionState;
static SpriteActorCore* pcPreviewTransfers; /* spriteTransfertListHead: the frames the native frame queued */
extern SpriteActorCore* spriteTransfertListHead;
static s32 pcPreviewCount;
static const void* pcPreviewArray;
static s32 pcPreviewValid;
u32 portRandSeed(void);
void srand(u32 seed);
extern u32 pcSlideEdgeStack[4];
void pcFieldPreviewDrop(void) {
    pcPreviewValid = 0;
}
void pcFieldPreviewKeep(void) {
    s32 i;
    if (pcPreviewValid || pcFieldSubFrames() == 1 || g_totalActors > PC_PREVIEW_ACTORS || D_800AFB10 == NULL) return;
    for (i = 0; i < g_totalActors; i++) {
        PcPreviewActor* k = &pcPreviewActors[i];
        sFieldEntity* e = &D_800AFB10[i];
        SpriteActorCore* a = e->m4_pVramSpriteSheet;
        k->entity = *e;
        k->scriptPtr = e->m4C_scriptEntity;
        if (e->m4C_scriptEntity != NULL) {
            k->script = *e->m4C_scriptEntity;
            k->rotation2Ptr = e->m4C_scriptEntity->m110_modelRotation2;
            if (k->rotation2Ptr != NULL) __builtin_memcpy(k->rotation2, k->rotation2Ptr, sizeof k->rotation2);
        }
        k->spritePtr = a;
        k->extraPtr = a != NULL ? a->m7C_extra : NULL;
        k->objectPtr = a != NULL ? a->m20_object : NULL;
        if (a != NULL) k->sprite = *a;
        if (k->extraPtr != NULL) k->extra = *a->m7C_extra;
        if (k->objectPtr != NULL) k->object = *a->m20_object;
    }
    __builtin_memcpy(pcPreviewFollow, followDataTable, sizeof pcPreviewFollow);
    __builtin_memcpy(pcPreviewFollowMap, partyToFollowStructMapping, sizeof pcPreviewFollowMap);
    __builtin_memcpy(pcPreviewSlide, pcSlideEdgeStack, sizeof pcPreviewSlide);
    pcPreviewSeed = portRandSeed();
    pcPreviewTransfers = spriteTransfertListHead;
    pcPreviewCamTargets[0] = fieldSceneState.m30_cameraEye2;
    pcPreviewCamTargets[1] = fieldSceneState.m40_cameraAt2;
    for (i = 0; i < 3; i++) {
        pcPreviewCamMoves[i] = fieldSceneState.mC0_cameraInterpolationTargetStartPosition[i];
        pcPreviewCamMoves[3 + i] = fieldSceneState.mE4_cameraInterpolationStartPosition[i];
    }
    pcPreviewScene[0] = fieldSceneState.m1A0_op9DVar3;
    pcPreviewScene[1] = fieldSceneState.m17E_sceneScale;
    pcPreviewScene[2] = fieldSceneState.m184_OP_A4Var1;
    pcPreviewScene[3] = fieldSceneState.m17C_sceneDIP;
    pcPreviewScene[4] = fieldSceneState.m194_OP_B6Var2;
    pcPreviewScene[5] = fieldSceneState.m178_sceneSCRZ;
    pcPreviewCollision = cameraCollisionState;
    pcPreviewTans[0] = fieldSceneState.m10E_cameraTan;
    pcPreviewTans[1] = fieldSceneState.m10C_camera2Tan;
    pcPreviewTans[2] = cameraDeltaTan;
    pcPreviewCount = g_totalActors;
    pcPreviewArray = D_800AFB10;
    pcPreviewValid = 1;
}
void pcFieldPreviewRestore(void) {
    s32 i;
    if (!pcPreviewValid) return;
    pcPreviewValid = 0;
    for (i = 0; i < g_totalActors && pcPreviewCount == g_totalActors && pcPreviewArray == D_800AFB10; i++) { /* (the actors changed: none put back) */
        PcPreviewActor* k = &pcPreviewActors[i];
        sFieldEntity* e = &D_800AFB10[i];
        if (e->m4C_scriptEntity != k->scriptPtr || e->m4_pVramSpriteSheet != k->spritePtr) continue;
        *e = k->entity;
        if (e->m4C_scriptEntity != NULL) {
            *e->m4C_scriptEntity = k->script;
            if (k->rotation2Ptr != NULL && e->m4C_scriptEntity->m110_modelRotation2 == k->rotation2Ptr) {
                __builtin_memcpy(e->m4C_scriptEntity->m110_modelRotation2, k->rotation2, sizeof k->rotation2);
            }
        }
        if (k->spritePtr != NULL) {
            SpriteActorCore* a = e->m4_pVramSpriteSheet;
            *a = k->sprite;
            if (k->extraPtr != NULL && a->m7C_extra == k->extraPtr) *a->m7C_extra = k->extra;
            if (k->objectPtr != NULL && a->m20_object == k->objectPtr) *a->m20_object = k->object;
        }
    }
    __builtin_memcpy(followDataTable, pcPreviewFollow, sizeof pcPreviewFollow);
    __builtin_memcpy(partyToFollowStructMapping, pcPreviewFollowMap, sizeof pcPreviewFollowMap);
    __builtin_memcpy(pcSlideEdgeStack, pcPreviewSlide, sizeof pcPreviewSlide);
    srand(pcPreviewSeed);
    /* The sprite frames a native frame queues (its animation steps, after its
     * draw) are uploaded at the next draw: natively the next tick's, here the
     * frame between's, after the snapshot. The sprites go back as queued (in
     * the list, the part count before the upload), so the list goes back too
     * and the tick's draw uploads them again. Without it the tick drew the new
     * frame's parts with the count of the first upload a field made (often 0:
     * the leader and NPCs vanished but on the vblank after a new frame, or lost
     * or gained parts; xg-hbc.19.7, the flicker). */
    spriteTransfertListHead = pcPreviewTransfers;
    fieldSceneState.m30_cameraEye2 = pcPreviewCamTargets[0];
    fieldSceneState.m40_cameraAt2 = pcPreviewCamTargets[1];
    for (i = 0; i < 3; i++) {
        fieldSceneState.mC0_cameraInterpolationTargetStartPosition[i] = pcPreviewCamMoves[i];
        fieldSceneState.mE4_cameraInterpolationStartPosition[i] = pcPreviewCamMoves[3 + i];
    }
    fieldSceneState.m1A0_op9DVar3 = pcPreviewScene[0];
    fieldSceneState.m17E_sceneScale = pcPreviewScene[1];
    fieldSceneState.m184_OP_A4Var1 = pcPreviewScene[2];
    fieldSceneState.m17C_sceneDIP = pcPreviewScene[3];
    fieldSceneState.m194_OP_B6Var2 = pcPreviewScene[4];
    fieldSceneState.m178_sceneSCRZ = pcPreviewScene[5];
    cameraCollisionState = pcPreviewCollision;
    fieldSceneState.m10E_cameraTan = pcPreviewTans[0];
    fieldSceneState.m10C_camera2Tan = pcPreviewTans[1];
    cameraDeltaTan = pcPreviewTans[2];
}
/* A party member following the leader takes its trail point whole each native
 * frame; a frame between (the next native frame's first part) puts it only the
 * part of the way the frame has reached from where the tick left it (it jumped
 * a whole step on the frame between and held on the tick). */
s32 pcFieldSubFrames(void);
s32 pcFieldSubIndex(void);
static void pcFieldPreviewFollower(s32 index, sFieldEntity* e, sFieldScriptEntity* se, SpriteActorCore* sp) {
    s32 n = pcFieldSubFrames();
    s32 k = pcFieldSubIndex();
    s32 c;
    if (n == 1 || !pcFieldOffTick() || !pcPreviewValid || index < 0 || index >= pcPreviewCount || k <= 0) return;
    for (c = 0; c < 3; c++) {
        s32 from = (&pcPreviewActors[index].script.m20_position.vx)[c];
        s32 to = (&se->m20_position.vx)[c];
        s32 at = from + (s32)(((long long)(to - from) * k) / n);
        (&se->m20_position.vx)[c] = at;
        (&sp->m0_position.vx)[c] = at;
        e->mC_matrix.t[c] = at >> 16;
    }
}

/* The field leaves its frame loop (menu, battle, exit, pause): the native frame
 * back, and the rest of its vblanks passed (the Original's frame lasts its N
 * vblanks before the loop goes on: pcFieldNativeFrameEnd, f_80072140.c). */
void pcFieldNativeFrameEnd(void);
void pcFieldPreviewSettle(void) {
    pcFieldPreviewRestore();
    pcFieldNativeFrameEnd();
}

void updateScriptAndMoveEntities(void) {
    s32 i;
    sFieldEntity* pEntity;
    sFieldScriptEntity* pScript;

    entityUpdateVar0 = -1;
    pcFieldSyncSpriteTimeScales();
    if (!pcFieldOffTick()) { /* Enhanced: scripts at the native cadence (the tick reads its native frame's presses itself) */
        pcFieldScriptMoveBefore(); /* Enhanced: what the scripts move, drawn through the tick (f_80072140.c) */
        exectueEntitiesUpdateFunction();
        pcFieldScriptMoveAfter();
    }
    for (i = 0; i < g_totalActors; i++) {
        fieldSceneState.m290_actorArray[i].m4C_scriptEntity->m68_oldPosition[0] = fieldSceneState.m290_actorArray[i].m4C_scriptEntity->m20_position.vx >> 16;
        fieldSceneState.m290_actorArray[i].m4C_scriptEntity->m68_oldPosition[1] = fieldSceneState.m290_actorArray[i].m4C_scriptEntity->m20_position.vy >> 16;
        fieldSceneState.m290_actorArray[i].m4C_scriptEntity->m68_oldPosition[2] = fieldSceneState.m290_actorArray[i].m4C_scriptEntity->m20_position.vz >> 16;
    }
    if (fieldDebugDisable == 0) {
        func_80281B00("EVENT CODE");
    }
    mechaDeltaTranslationIndex = 0;
    for (i = 0; i < g_totalActors; i++) {
        s32 culled;
        pEntity = &fieldSceneState.m290_actorArray[i];
        pScript = pEntity->m4C_scriptEntity;
        /* An actor the last frame's render projected off screen (m4_flags
         * 0x200, renderFieldCharacterSprites) does not move this frame.
         * Enhanced (xg-hbc.13): at 60 Hz the tick frame's render sees the
         * camera halfway through its native step, so its verdict is not the
         * Original's; the frame between keeps the verdict its tick used, and
         * the pair moves or waits together, as the native frame did (the
         * verdict for the next pair comes from the frame between's render,
         * whose camera is where the native frame's end has it). Lahan's
         * opening pan put an NPC off screen for three frames instead of two
         * and it lost half a step, missed its walk's end and stood still. */
        if (pcFieldSubFrames() > 1 && i < PC_FIELD_MAX_ACTORS && !pcFieldOffTick()) {
            pcFieldCulledAtTick[i] = (u8)((pScript->m4_flags & 0x600) == 0x200);
        }
        culled = pcFieldActorCulled(i, pScript);
        if ((pEntity->m58_flags & 0xF80) == 0x200) {
            if (!(pScript->m0_fieldScriptFlags & 0x10001)) {
                if (!culled) {
                    pScript->m14_currentTriangleFlag = getWalkmeshTriangleFlag(pScript);
                    updateEntityEventCode2(i, pEntity, pScript);
                    updateEntityEventCode3(i, pEntity, pScript);
                }
            } else if ((pScript->m4_flags & 0x1000000) && !(pScript->m0_fieldScriptFlags & 0x10000)) {
                if (pScript->mE8_currentAnimationId != pScript->mEA_forcedAnimation) {
                    pScript->mEA_forcedAnimation = 2;
                    fieldSceneState.m290_actorArray[i].m4C_scriptEntity->mE8_currentAnimationId = fieldSceneState.m290_actorArray[i].m4C_scriptEntity->mEA_forcedAnimation;
                    spriteActorSetPlayingAnimation(fieldSceneState.m290_actorArray[i].m4_pVramSpriteSheet, pScript->mEA_forcedAnimation);
                }
            } else if (pScript->m4_flags & 0x200000) {
                if (fieldSceneState.m290_actorArray[i].m4C_scriptEntity->mE8_currentAnimationId != fieldSceneState.m290_actorArray[i].m4C_scriptEntity->mEA_forcedAnimation) {
                    fieldSceneState.m290_actorArray[i].m4C_scriptEntity->mE8_currentAnimationId = fieldSceneState.m290_actorArray[i].m4C_scriptEntity->mEA_forcedAnimation;
                    pcSlideEdgeAnimationScope(1);
                    setVisualAnimation(fieldSceneState.m290_actorArray[i].m4_pVramSpriteSheet, fieldSceneState.m290_actorArray[i].m4C_scriptEntity->mEA_forcedAnimation, &fieldSceneState.m290_actorArray[i]);
                    pcSlideEdgeAnimationScope(0);
                }
            }
        }
    }
    if (fieldDebugDisable == 0) {
        func_80281B00("MOV CHECK0");
    }
    pEntity = &fieldSceneState.m290_actorArray[g_playerControlledActor];
    EntityMoveCheck0(g_playerControlledActor, pEntity, pEntity->m4C_scriptEntity);
    if (fieldDebugDisable == 0) {
        func_80281B00("MOV CHECK1");
    }
    for (i = 0; i < g_totalActors; i++) {
        if (fieldSceneState.m290_actorArray[i].m58_flags & 0xF00) {
            pEntity = &fieldSceneState.m290_actorArray[i];
            pScript = pEntity->m4C_scriptEntity;
            if (!pcFieldActorCulled(i, pScript) && (pEntity->m58_flags & 0xF80) == 0x200 &&
                !(pScript->m0_fieldScriptFlags & 0x10001) && i != g_playerControlledActor) {
                EntityMoveCheck1(i, 0x7FFFFFFF, pEntity, pScript, 0);
                if (fieldSceneState.m290_actorArray[i].m4_pVramSpriteSheet->m7C_extra->mC_animationSignal == 1) {
                    pScript->m0_fieldScriptFlags &= ~0x800;
                }
            }
        }
    }
    if (fieldDebugDisable == 0) {
        func_80281B00("MOV CHECK2");
    }
    if (fieldRandomBattleVar == 0 && noUpdatesToPartyMembers == 0) {
        startScriptsForCollisions(g_playerControlledActor, &D_800AFB10[g_playerControlledActor], fieldSceneState.m290_actorArray[g_playerControlledActor].m4C_scriptEntity);
    }
    updateEntityEventCode3Var2 = 1;
    updatePartyFollowLeader();
    if (fieldDebugDisable == 0) {
        func_80281B00("MOV CHECK3");
    }
}

/* Noah: updatePartyFollowLeader. The follow flags g_PartyFollowDisabled (m4B),
 * g_playerControlledActor (mE8, the player actor) and D_800B234E (m1CA) are read through
 * the FieldBss_800B2184 aggregate (the first test then loads &m1CA into a
 * register like retail); partyToFollowStructMapping stays a plain symbol. */
static void pcFieldPreviewFollower(s32 index, sFieldEntity* e, sFieldScriptEntity* se, SpriteActorCore* sp);
void updatePartyFollowLeader(void) {
    s32 i;
    sFieldEntity* e;
    sFieldScriptEntity* p;
    s32 j;
    sFieldEntity* pEntity;
    sFieldScriptEntity* pScript;
    SpriteActorCore* pSprite;
    s32 partyPosition;
    s32* pMapping;
    u32 flags;
    u32 triangleFlags;
    s32 delay;
    s16 animation;

    if (op9DVar1.m1CA_playerIsntPartyLeader != 0) {
        for (i = 0; i < g_totalActors; i++) {
            if ((D_800AFB10[i].m4C_scriptEntity->m0_fieldScriptFlags & 0x1000000) && i != op9DVar1.mE8_playerControlledActor &&
                !(D_800AFB10[i].m58_flags & 0x20)) {
                pScript = D_800AFB10[i].m4C_scriptEntity;
                pEntity = &D_800AFB10[i];
                pSprite = pEntity->m4_pVramSpriteSheet;
                if (pScript->mE8_currentAnimationId != pScript->mE6_idleAnimationId) {
                    pScript->mE8_currentAnimationId = pScript->mE6_idleAnimationId;
                    if (pScript->mE8_currentAnimationId < 0) {
                        pScript->mE8_currentAnimationId = 0;
                    }
                    setVisualAnimation(pSprite, pScript->mE8_currentAnimationId, pEntity);
                }
            }
        }
        return;
    }
    for (i = 0; i < g_totalActors; i++) {
        e = (sFieldEntity*)(i * sizeof(sFieldEntity) + (sptr)D_800AFB10);
        p = e->m4C_scriptEntity;
        if (!(p->m0_fieldScriptFlags & 0x1000000) || i == op9DVar1.mE8_playerControlledActor || (e->m58_flags & 0x20)) {
            continue;
        }
        pEntity = e;
        pScript = p;
        pSprite = pEntity->m4_pVramSpriteSheet;
        partyPosition = findCharacterInParty(pScript->mE4_playableCharacterId);
        if (partyPosition == -1) {
            continue;
        }
        pMapping = &partyToFollowStructMapping[partyPosition];
        updateEntityEventCode3Sub1(pSprite, followDataTable[*pMapping].m14_rotation, pEntity);
        flags = followDataTable[*pMapping].m0_flags;
        triangleFlags = pScript->m14_currentTriangleFlag;
        if (op9DVar1.m4B_partyFollowDisabled == 1) {
            *pMapping = (partyToFollowStructMapping[0] + 1) & 0x1F;
            goto copy;
        }
        if (!(flags & 0x800)) {
            pScript->m4_flags &= ~0x1000;
            if (triangleFlags & 0x420000) {
                goto check;
            }
            if (entityUpdateVar0 == -1) {
                if (pSprite->m84_maxY != FP_INT(pScript->m20_position.vy)) {
                    goto check;
                }
                if (pScript->mE8_currentAnimationId != 6) {
                    if (pScript->mE8_currentAnimationId == pScript->mE6_idleAnimationId) {
                        continue;
                    }
                    pScript->mE8_currentAnimationId = pScript->mE6_idleAnimationId;
                    if (pScript->mE8_currentAnimationId < 0) {
                        pScript->mE8_currentAnimationId = 0;
                    }
                    setVisualAnimation(pSprite, pScript->mE8_currentAnimationId, pEntity);
                    continue;
                }
                pScript->m4_flags |= 0x1000;
                continue;
            }
            delay = 0x14;
            if (partyPosition == 1) {
                delay = 0xA;
            }
            if (((partyToFollowStructMapping[0] + delay) & 0x1F) != *pMapping) {
                continue;
            }
        }
    check:
        if (partyToFollowStructMapping[partyPosition] == partyToFollowStructMapping[0]) {
            pScript->m0_fieldScriptFlags &= ~0x800;
            pScript->mE8_currentAnimationId = pScript->mE6_idleAnimationId;
            if (pScript->mE8_currentAnimationId < 0) {
                pScript->mE8_currentAnimationId = 0;
            }
            setVisualAnimation(pSprite, pScript->mE8_currentAnimationId, pEntity);
            continue;
        }
    copy:
        if (flags & 0x800) {
            pScript->m0_fieldScriptFlags |= 0x800;
        } else {
            pScript->m0_fieldScriptFlags &= ~0x800;
        }
        animation = followDataTable[partyToFollowStructMapping[partyPosition]].m12_animationId;
        if (pScript->mE8_currentAnimationId != animation) {
            pScript->mE8_currentAnimationId = animation;
            if (pScript->mE8_currentAnimationId < 0) {
                pScript->mE8_currentAnimationId = 0;
            }
            setVisualAnimation(pSprite, pScript->mE8_currentAnimationId, pEntity);
        }
        for (j = 0; j < 4; j++) {
            pScript->m8_currentWalkMeshTriangle[j] = followDataTable[partyToFollowStructMapping[partyPosition]].m16_walkMeshTriangles[j];
        }
        pScript->m10_walkmeshId = followDataTable[partyToFollowStructMapping[partyPosition]].m44_walkmeshId;
        pScript->m50_surfaceNormal.vx = (&followDataTable[partyToFollowStructMapping[partyPosition]].m20_stepAndNormal[1])->vx;
        pScript->m50_surfaceNormal.vy = (&followDataTable[partyToFollowStructMapping[partyPosition]].m20_stepAndNormal[1])->vy;
        pScript->m50_surfaceNormal.vz = (&followDataTable[partyToFollowStructMapping[partyPosition]].m20_stepAndNormal[1])->vz;
        pSprite->mC_step.vx = (&followDataTable[partyToFollowStructMapping[partyPosition]].m20_stepAndNormal[0])->vx;
        pSprite->mC_step.vy = (&followDataTable[partyToFollowStructMapping[partyPosition]].m20_stepAndNormal[0])->vy;
        pSprite->mC_step.vz = (&followDataTable[partyToFollowStructMapping[partyPosition]].m20_stepAndNormal[0])->vz;
        pEntity->mC_matrix.t[0] = followDataTable[partyToFollowStructMapping[partyPosition]].m8_pos.vx;
        pEntity->mC_matrix.t[1] = followDataTable[partyToFollowStructMapping[partyPosition]].m8_pos.vy;
        pEntity->mC_matrix.t[2] = followDataTable[partyToFollowStructMapping[partyPosition]].m8_pos.vz;
        pScript->m20_position.vx = pSprite->m0_position.vx = pEntity->mC_matrix.t[0] << 16;
        pScript->m20_position.vy = pSprite->m0_position.vy = pEntity->mC_matrix.t[1] << 16;
        pScript->m20_position.vz = pSprite->m0_position.vz = pEntity->mC_matrix.t[2] << 16;
        pcFieldPreviewFollower((s32)(pEntity - D_800AFB10), pEntity, pScript, pSprite); /* Enhanced (xg-hbc.19.3) */
        pSprite->m84_maxY = followDataTable[partyToFollowStructMapping[partyPosition]].m10_maxY;
        pScript->m104_rotation = pScript->m106_currentRotation = followDataTable[partyToFollowStructMapping[partyPosition]].m14_rotation;
        partyToFollowStructMapping[partyPosition] = (partyToFollowStructMapping[partyPosition] - 1) & 0x1F;
    }
}

void initFollowStructForPlayer(s32 index) {
    sFieldEntity* pEntity = &D_800AFB10[index];
    sFieldScriptEntity* pScript = pEntity->m4C_scriptEntity;
    SpriteActorCore* pSprite = pEntity->m4_pVramSpriteSheet;
    s32 i;

    if (index == g_playerControlledActor && noUpdatesToPartyMembers == 0) {
        (&followDataTable[partyToFollowStructMapping[0]].m20_stepAndNormal[0])->vx = pSprite->mC_step.vx;
        (&followDataTable[partyToFollowStructMapping[0]].m20_stepAndNormal[0])->vy = pSprite->mC_step.vy;
        (&followDataTable[partyToFollowStructMapping[0]].m20_stepAndNormal[0])->vz = pSprite->mC_step.vz;
        (&followDataTable[partyToFollowStructMapping[0]].m20_stepAndNormal[1])->vx = pScript->m50_surfaceNormal.vx;
        (&followDataTable[partyToFollowStructMapping[0]].m20_stepAndNormal[1])->vy = pScript->m50_surfaceNormal.vy;
        (&followDataTable[partyToFollowStructMapping[0]].m20_stepAndNormal[1])->vz = pScript->m50_surfaceNormal.vz;
        followDataTable[partyToFollowStructMapping[0]].m14_rotation = pScript->m106_currentRotation & 0xFFF;
        followDataTable[partyToFollowStructMapping[0]].m10_maxY = pSprite->m84_maxY;
        followDataTable[partyToFollowStructMapping[0]].m8_pos.vx = pScript->m20_position.vx >> 16;
        followDataTable[partyToFollowStructMapping[0]].m8_pos.vy = pScript->m20_position.vy >> 16;
        followDataTable[partyToFollowStructMapping[0]].m8_pos.vz = pScript->m20_position.vz >> 16;
        followDataTable[partyToFollowStructMapping[0]].m12_animationId = pScript->mE8_currentAnimationId;
        followDataTable[partyToFollowStructMapping[0]].m40_currentTriangleFlag = pScript->m14_currentTriangleFlag;
        followDataTable[partyToFollowStructMapping[0]].m0_flags = pScript->m0_fieldScriptFlags;
        followDataTable[partyToFollowStructMapping[0]].m4_flags = pScript->m4_flags;
        for (i = 0; i < 4; i++) {
            followDataTable[partyToFollowStructMapping[0]].m16_walkMeshTriangles[i] = pScript->m8_currentWalkMeshTriangle[i];
        }
        followDataTable[partyToFollowStructMapping[0]].m44_walkmeshId = pScript->m10_walkmeshId;
        entityUpdateVar0 = 0;
        partyToFollowStructMapping[0] = (partyToFollowStructMapping[0] - 1) & 0x1F;
    }
}

s32 OP_UPDATE_CHARACTER_Sub0(sFieldScriptEntity* pEntity) {
    u32 flags = (pEntity->m0_fieldScriptFlags >> 9) & 3;
    return -((flags & (pEntity->m14_currentTriangleFlag >> 3)) != 0);
}

void updateEntityEventCode3Sub1(SpriteActorCore* actor, s32 rotation, sFieldEntity* pEntity) {
    s32 speed;
    u32 flags;
    s32 idx;

    if (!(pEntity->m58_flags & 0x40)) {
        speed = ((s32)(0x40000 / pcFieldTimeScale((u16)pEntity->m4C_scriptEntity->m76_timeScale)) >> 8) * 0x20;
        if (!(rotation & 0x8000)) {
            actor->mC_step.vx = ((getAngleSin(rotation & 0xFFF) * speed) >> 12) * pEntity->m4C_scriptEntity->mF4_scale3d[0];
            actor->mC_step.vz = ((-(getAngleCos(rotation & 0xFFF) * speed)) >> 12) * pEntity->m4C_scriptEntity->mF4_scale3d[2];
        } else {
            actor->mC_step.vx = 0;
            actor->mC_step.vz = 0;
        }
    } else if (!((s16)rotation & 0x8000)) {
        flags = pEntity->m4C_scriptEntity->m4_flags;
        if (!(flags & 0x2000)) {
            if (!(flags & 0x80000)) {
                spriteActorSetDirection(actor, (s16)rotation);
            } else {
                speed = ((s32)(0x40000 / pcFieldTimeScale((u16)pEntity->m4C_scriptEntity->m76_timeScale)) >> 8) * 0x20;
                actor->mC_step.vx = ((getAngleSin(rotation & 0xFFF) * speed) >> 12) * pEntity->m4C_scriptEntity->mF4_scale3d[0];
                actor->mC_step.vz = ((-(getAngleCos(rotation & 0xFFF) * speed)) >> 12) * pEntity->m4C_scriptEntity->mF4_scale3d[2];
                actor->m18_moveSpeed = 0x4000000 / (u16)pEntity->m4C_scriptEntity->m76_timeScale /* base: the sprite scales it */;
            }
        } else if (!(flags & 0x20000)) {
            speed = ((s32)(0x80000 / pcFieldTimeScale((u16)pEntity->m4C_scriptEntity->m76_timeScale)) >> 8) * 0x20;
            actor->mC_step.vx = ((getAngleSin(rotation & 0xFFF) * speed) >> 12) * pEntity->m4C_scriptEntity->mF4_scale3d[0];
            actor->mC_step.vz = ((-(getAngleCos(rotation & 0xFFF) * speed)) >> 12) * pEntity->m4C_scriptEntity->mF4_scale3d[2];
        } else {
            idx = pEntity->m4C_scriptEntity->m12C_xD;
            actor->mC_step.vx = -loadedMechas[idx]->m128_deltaTranslation[0] << 16;
            actor->mC_step.vz = -loadedMechas[idx]->m128_deltaTranslation[2] << 16;
        }
    } else {
        actor->mC_step.vx = 0;
        actor->mC_step.vz = 0;
    }
    actor->mC_step.vx &= 0xFFFFF000;
    actor->mC_step.vz &= 0xFFFFF000;
}

void setVisualAnimation(SpriteActorCore* actor, s32 animationId, sFieldEntity* pEntity) {
    if (pEntity->m58_flags & 0x40) {
        if (animationId != 3 && op9DVar1.m1C0_customJumpMode == 0) {
            pEntity->m4C_scriptEntity->m0_fieldScriptFlags &= ~0x800;
        }
        if (animationId == 0xFF) {
            animationId = 0;
        }
        if (animationId != op9DVar1.m1C2_jumpAnimationId) {
            pEntity->m4C_scriptEntity->m0_fieldScriptFlags &= ~0x800;
        }
        if (!(pEntity->m4C_scriptEntity->m4_flags & 0x2000)) {
            if (!(pEntity->m4C_scriptEntity->m4_flags & 0x1000000)) {
                pcSlideEdgeAnimation(animationId, fieldSceneState.m290_actorArray, pEntity,
                                     fieldSceneState.m28C_totalObjects, sizeof(sFieldEntity));
                spriteActorSetPlayingAnimation(actor, animationId);
            }
        } else if (animationId < 0x10) {
            mechaPlayAnimation(pEntity->m4C_scriptEntity->m12C_xD, 0, updateEntityEventCode4Table0[animationId]);
            op9DVar1.m60_mechaCurrentAnimation[pEntity->m4C_scriptEntity->m12C_xD] = updateEntityEventCode4Table0[animationId];
        } else {
            animationId -= 0x10;
            mechaPlayAnimation(pEntity->m4C_scriptEntity->m12C_xD, 0, animationId);
            op9DVar1.m60_mechaCurrentAnimation[pEntity->m4C_scriptEntity->m12C_xD] = animationId;
        }
    }
}

s32 isPositionInEntityScriptBoundingVolume(s32 testedX, s32 testedZ, sFieldScriptEntity* pEntity, s32 extraRadius) {
    s32 tested = (testedX << 16) + testedZ;
    s32 minX = (FP_INT(pEntity->m20_position.vx) - (u16)pEntity->m18_boundingVolume[0] - extraRadius) << 16;
    s32 maxZ = FP_INT(pEntity->m20_position.vz) + (u16)pEntity->m18_boundingVolume[2] + extraRadius;
    s32 minXmaxZ = minX + maxZ;
    s32 maxX = (FP_INT(pEntity->m20_position.vx) + (u16)pEntity->m18_boundingVolume[0] + extraRadius) << 16;
    s32 maxXmaxZ = maxX + maxZ;
    s32 minZ = FP_INT(pEntity->m20_position.vz) - (u16)pEntity->m18_boundingVolume[2] - extraRadius;
    s32 maxXminZ = maxX + minZ;
    s32 minXminZ = minX + minZ;

    if (NormalClip(minXmaxZ, maxXmaxZ, tested) < 0 || NormalClip(maxXmaxZ, maxXminZ, tested) < 0 ||
        NormalClip(maxXminZ, minXminZ, tested) < 0 || NormalClip(minXminZ, minXmaxZ, tested) < 0) {
        return -1;
    }
    if (fieldDebugDisable == 0) {
        func_80281678(pEntity);
    }
    return 0;
}

s32 updateEntityEventCode3Sub2(VECTOR* pDelta, sFieldScriptEntity* pEntity) {
    s32 position;
    s16* zone;
    s32 zone0, zone1, zone2, zone3;

    if (pEntity->m12C_xC) {
        position = (((pEntity->m20_position.vx + pDelta->vx) >> 16) << 16) + ((pEntity->m20_position.vz + pDelta->vz) >> 16);
        zone = pEntity->m114_movementBoundingZone;
        zone0 = (zone[0] << 16) + zone[1];
        zone1 = (zone[2] << 16) + zone[3];
        zone2 = (zone[4] << 16) + zone[5];
        zone3 = (zone[6] << 16) + zone[7];
        if (NormalClip(zone0, zone1, position) < 0 || NormalClip(zone1, zone2, position) < 0 ||
            NormalClip(zone2, zone3, position) < 0) {
            return -1;
        }
        return (NormalClip(zone3, zone0, position) < 0) ? -1 : 0;
    }
    return 0;
}

s32 computeDistanceBetweenActors(s32 actor0, s32 actor1) {
    sFieldScriptEntity* p1 = D_800AFB10[actor1].m4C_scriptEntity;
    sFieldScriptEntity* p0 = D_800AFB10[actor0].m4C_scriptEntity;
    s32 x1 = FP_INT(p1->m20_position.vx);
    s32 z1 = FP_INT(p1->m20_position.vz);

    return length2d(x1 - FP_INT(p0->m20_position.vx), z1 - FP_INT(p0->m20_position.vz));
}

extern s16 deltaMoveTable0[4];
extern u16 deltaMoveTable1[8];
extern u16 deltaMoveVar0;
void computeDeltaMove(VECTOR* delta, s32 speed, u32 angle);
s32 computeDistanceBetweenActors(s32 actor0, s32 actor1);

void updateEntityEventCode2(s32 index, sFieldEntity* pEntity, sFieldScriptEntity* pScript) {
    VECTOR deltaMove;
    VECTOR v60;
    VECTOR v50;
    VECTOR unused;
    SVECTOR rot;
    u32 flags;
    s32 dx;
    s32 dz;
    s32 limit;
    s32 apply;
    SpriteActorCore* pSprite;
    s32 x0, z0, x1, z1;
    s32 dist;
    s32 angle;
    s32 dy;
    s32 f0;
    s32 t;

    dx = 0;
    dz = 0;
    flags = 0;
    if (!((pScript->m4_flags >> (pScript->m10_walkmeshId + 3)) & 1) && noUpdatesToPartyMembers == 0) {
        flags = pScript->m14_currentTriangleFlag;
    }
    pSprite = pEntity->m4_pVramSpriteSheet;
    computeDeltaMove(&deltaMove, deltaMoveTable0[(flags >> 9) & 3], (deltaMoveTable1[(flags >> 11) & 7] + deltaMoveVar0) & 0xFFF);
    if (!(pScript->m0_fieldScriptFlags & 0x41800)) {
        f0 = pScript->mF0_fallSpeed;
        if (flags & 0x420000) {
            v60.vx = -(pScript->m50_surfaceNormal.vx * pScript->m50_surfaceNormal.vy) >> 15;
            v60.vy = 0;
            v60.vz = -(pScript->m50_surfaceNormal.vz * pScript->m50_surfaceNormal.vy) >> 15;
            if (v60.vx == 0) {
                v60.vx = 1;
            }
            v60.vy = 1;
            if (v60.vz == 0) {
                v60.vz = 1;
            }
            VectorNormal(&v60, &v50);
            if (v50.vx == 0) {
                v50.vx = 1;
            }
            if (v50.vy == 0) {
                v50.vy = 1;
            }
            if (v50.vz == 0) {
                v50.vz = 1;
            }
            dx = (v50.vx * (f0 >> 17)) << 4;
            dz = (v50.vz * (f0 >> 17)) << 4;
            limit = 0xC;
            if (flags & 0x400000) {
                limit = 0x18;
            }
            /* Enhanced: vertical physics runs every frame as the native code does,
             * with the sprite's gravity at its (doubled) time scale: the sprite
             * code's own dt scaling (a quarter per frame). Tick-only variants
             * were measured worse: the ground snap must follow every half step
             * (the walk-mesh layer is chosen by height). */
            if ((pScript->mF0_fallSpeed >> 16) >= limit) {
                pScript->mF0_fallSpeed = limit << 16;
            } else {
                pScript->mF0_fallSpeed += pSprite->m1C_gravity;
            }
            pSprite->mC_step.vy = pScript->mF0_fallSpeed >> 1;
        }
        if (flags & 0x400000) {
            pScript->m40_externalStep.vx += dx;
            pScript->m40_externalStep.vz += dz;
            pScript->m104_rotation |= 0x8000;
        }
        if (pScript->m74_touchedActor != 0xFF) {
            goto touched;
        }
        if (flags & 0x20000) {
            pScript->m40_externalStep.vx += dx;
            pScript->m40_externalStep.vz += dz;
        }
        apply = flags & 0x8000;
    } else {
        apply = flags & 0x4000;
    }
    if (apply) {
        pScript->m40_externalStep.vx += deltaMove.vx;
        pScript->m40_externalStep.vy += deltaMove.vy;
        pScript->m40_externalStep.vz += deltaMove.vz;
    }
    if (pScript->m74_touchedActor == 0xFF) {
        goto end;
    }
touched:
    if ((D_800AFB10[pScript->m74_touchedActor].m4C_scriptEntity->m4_flags & 0xC0) == 0xC0 && (pScript->m134_x7 || !pcFieldOffTick())) {
        /* (Enhanced, xg-hbc.19.3: a frame between's preview allocates nothing; the tick does) */
        if (!pScript->m134_x7) {
            pScript->m110_modelRotation2 = allocateHeap(PC_HEAP_BYTES(0xC), 0);
            pScript->m134_x7 = 1;
        }
        rot.vx = D_800AFB10[pScript->m74_touchedActor].m50_modelRotation.vx - pScript->m110_modelRotation2[0];
        rot.vy = D_800AFB10[pScript->m74_touchedActor].m50_modelRotation.vy - pScript->m110_modelRotation2[1];
        rot.vz = D_800AFB10[pScript->m74_touchedActor].m50_modelRotation.vz - pScript->m110_modelRotation2[2];
        angle = rot.vy;
        pScript->m110_modelRotation2[1] = D_800AFB10[pScript->m74_touchedActor].m50_modelRotation.vy;
        x1 = fieldSceneState.m290_actorArray[pScript->m74_touchedActor].m4C_scriptEntity->m20_position.vx;
        x0 = pScript->m20_position.vx;
        z0 = pScript->m20_position.vz;
        z1 = fieldSceneState.m290_actorArray[pScript->m74_touchedActor].m4C_scriptEntity->m20_position.vz;
        if (!(pScript->m104_rotation & 0x8000)) {
            pScript->m110_modelRotation2[4] = computeDistanceBetweenActors(index, pScript->m74_touchedActor);
        }
        dist = pScript->m110_modelRotation2[4];
        t = (s16)ratan2(z1 - z0, x1 - x0) - angle;
        dy = t - 0x800;
        pScript->m40_externalStep.vx += x1 + ((getAngleSin(dy) * dist) << 4) - x0;
        pScript->m40_externalStep.vz += z1 + ((getAngleCos(dy) * dist) << 4) - z0;
    }
end:
    if ((pScript->m4_flags & 0x22000) == 0x22000) {
        t = ((loadedMechas[mechaDeltaTranslationIndex]->m128_deltaTranslation[0] << 16) / pcFieldTimeScale(pScript->m76_timeScale)) << 8;
        pScript->m40_externalStep.vx -= t;
        pScript->m40_externalStep.vz -= ((loadedMechas[mechaDeltaTranslationIndex]->m128_deltaTranslation[2] << 16) / pcFieldTimeScale(pScript->m76_timeScale)) << 8;
        mechaDeltaTranslationIndex++;
    }
}

extern u16 padButtonForScripts;      /* Noah: padButtonForScripts[0].m0_buttons */
extern s32 updateEntityEventCode3Var0;
extern s32 playerCanRun;
s32 updateEntityEventCode3Sub3(VECTOR* position, sFieldScriptEntity* entity, SVECTOR* edge, s16 angle);
s32 updateEntityEventCode3Sub4(VECTOR* position, sFieldScriptEntity* entity, SVECTOR* edge, s16 angle);
/* The wall edge Sub3/Sub4 slide along (buf below): a move blocked inside its
 * triangle (collisionFlag 0 or 7) leaves it unset, and Sub3Sub2 still slides
 * along it, so retail moves the entity along whatever its stack holds there:
 * the 16 bytes at updateScriptAndMoveEntities's sp - 0x38. These words are
 * what the PS1 has there (PCSX-Redux, write breakpoints on them, every disc 1
 * field and the New Game; work/agents/NPFIX/gamec/NOTES.md). In the field
 * loop the last stores before an entity's call are, in this order:
 *  - the previous frame's renderCompass (its matrices m232.t[1..2] and m200's
 *    first four halves, f_80072140.c), then in a scene with gears renderMechas
 *    (field_mecha: processMechaAnimData's saved registers, the scale
 *    submitMechaForRendering keeps), then each open dialog window's width
 *    (dialogWindowSetupForRendering, f_8007DCF8.c);
 *  - word 0: executeFieldScript's saved $ra, for each entity script run this
 *    frame (exectueEntitiesUpdateFunction, f_800A19B0.c);
 *  - the edge an earlier entity's call left (nothing writes them between
 *    the calls).
 * Loader writers below also update known words before the first warm-up
 * frame. The fallback for still-unmodeled words is the New Game gear scene. */
#if VERSION_JP
u32 pcSlideEdgeStack[4] = { 0x800A17E8, 1, 0x17E, 0x801E86A8 };
#else
u32 pcSlideEdgeStack[4] = { 0x800A2258, 1, 0x17E, 0x801E86A8 };
#endif

static void pcSlideEdgeLoad(SVECTOR* edge) {
    s32 i;

    for (i = 0; i < 2; i++) {
        edge[i].vx = (s16)pcSlideEdgeStack[i * 2];
        edge[i].vy = (s16)(pcSlideEdgeStack[i * 2] >> 16);
        edge[i].vz = (s16)pcSlideEdgeStack[i * 2 + 1];
        edge[i].pad = (s16)(pcSlideEdgeStack[i * 2 + 1] >> 16);
    }
}

static void pcSlideEdgeStore(SVECTOR* edge) {
    s32 i;

    for (i = 0; i < 2; i++) {
        pcSlideEdgeStack[i * 2] = (u16)edge[i].vx | ((u32)(u16)edge[i].vy << 16);
        pcSlideEdgeStack[i * 2 + 1] = (u16)edge[i].vz | ((u32)(u16)edge[i].pad << 16);
    }
}
void updateEntityEventCode3(s32 index, sFieldEntity* pEntity, sFieldScriptEntity* pScript) {
    VECTOR step;
    SVECTOR buf[2];
    SpriteActorCore* pSprite;
    s32 rotation;
    s16 walk;
    s32 moveMask;
    s32 result;
    s32 frozen;
    s32 backup;

    rotation = pScript->m104_rotation;
    pSprite = pEntity->m4_pVramSpriteSheet;
    updateEntityEventCode3Var0 = index;
    if (pScript->m0_fieldScriptFlags & 0x1000000) {
        updateEntityEventCode3Var0 = index;
        return;
    }
    walk = 1;
    if ((pScript->m0_fieldScriptFlags & 0x4000) && (padButtonForScripts & 0x40) && playerCanRun == 1) {
        walk = 2;
    }
    if ((pScript->m0_fieldScriptFlags & 0x1800) && pScript->mE8_currentAnimationId != walk) {
        switch (pScript->mE8_currentAnimationId) {
        case 1:
            walk = 1;
            break;
        case 2:
            walk = 2;
            break;
        }
    }
    if (pScript->mE3_pushCounter > 8 && !pcFieldOffTick()) { /* Enhanced (xg-hbc.19.3): native frames */
        pScript->mE3_pushCounter--;
    }
    moveMask = pScript->m40_externalStep.vx | pScript->m40_externalStep.vy | pScript->m40_externalStep.vz;
    if (updateEntityEventCode3Sub0(pScript) == -1) {
        moveMask = 1;
    }
    frozen = rotation & 0x8000;
    if (!frozen || moveMask || (pScript->m0_fieldScriptFlags & 0x40800)) {
        if (!(rotation & 0x8000)) {
            updateEntityEventCode3Sub1(pSprite, (s16)rotation, pEntity);
            step.vx = pSprite->mC_step.vx;
            step.vy = pSprite->mC_step.vy;
            step.vz = pSprite->mC_step.vz;
            step.vx += pScript->m40_externalStep.vx;
            step.vy += pScript->m40_externalStep.vy;
            step.vz += pScript->m40_externalStep.vz;
            pScript->m106_currentRotation = rotation;
        } else {
            rotation = pScript->m106_currentRotation & 0xFFF;
            step.vx = pScript->m40_externalStep.vx;
            step.vy = pScript->m40_externalStep.vy;
            step.vz = pScript->m40_externalStep.vz;
        }
        if (updateEntityEventCode3Sub2(&step, pScript) != 0) {
            goto notMoved;
        }
        if (step.vx != 0 || step.vz != 0) {
            rotation = -(s16)ratan2(step.vz, step.vx) & 0xFFF;
        }
        result = -1;
        if (pScript->m8_currentWalkMeshTriangle[pScript->m10_walkmeshId] != -1) {
            backup = pScript->m0_fieldScriptFlags;
            if (index == g_playerControlledActor) {
                if (partyFieldActorIds[1] != 0xFF) {
                    pScript->m0_fieldScriptFlags = backup | (D_800AFB10[partyFieldActorIds[1]].m4C_scriptEntity->m0_fieldScriptFlags & 0x600);
                }
                if (partyFieldActorIds[2] != 0xFF) {
                    pScript->m0_fieldScriptFlags |= D_800AFB10[partyFieldActorIds[2]].m4C_scriptEntity->m0_fieldScriptFlags & 0x600;
                }
            }
            pcSlideEdgeLoad(buf);
            if (!(pScript->m0_fieldScriptFlags & 0x41800) && pScript->m74_touchedActor == 0xFF && updateEntityEventCode3Var1 == 0) {
                result = updateEntityEventCode3Sub3(&step, pScript, buf, (s16)rotation);
            } else {
                result = updateEntityEventCode3Sub4(&step, pScript, buf, (s16)rotation);
            }
            pcSlideEdgeStore(buf);
            pScript->m0_fieldScriptFlags = (pScript->m0_fieldScriptFlags & ~0x600) | (backup & 0x600);
        }
        if (result == -1) {
            goto notMoved;
        }
        goto moved;
    } else {
        pScript->m104_rotation |= 0x8000;
        walk = pScript->mE6_idleAnimationId;
    }
notMoved:
    pScript->mF0_fallSpeed = 0x10000;
    pScript->m40_externalStep.vx = 0;
    pScript->m40_externalStep.vy = 0;
    pScript->m40_externalStep.vz = 0;
    step.vx = 0;
    step.vy = 0;
    step.vz = 0;
    pSprite->mC_step.vx = 0;
    pSprite->mC_step.vz = 0;
    pScript->m106_currentRotation |= 0x8000;
moved:
    pScript->m4_flags &= ~0x1000;
    if (pScript->m0_fieldScriptFlags & 0x800) {
        if (op9DVar1.m1C0_customJumpMode == 0) {
            if (FP_INT(pSprite->m0_position.vy) != pSprite->m84_maxY) {
                if (walk == 2) {
                    pSprite->m18_moveSpeed = pSprite->m82_entityScale * 0x60;
                } else {
                    pSprite->m18_moveSpeed = pSprite->m82_entityScale * 0x30;
                }
            } else {
                pSprite->m18_moveSpeed = 0;
            }
        }
        walk = op9DVar1.m1C2_jumpAnimationId;
    } else {
        if (pScript->m104_rotation & 0x8000) {
            walk = pScript->mE6_idleAnimationId;
        }
        if (getWalkmeshTriangleFlag(pScript) & 0x200000) {
            if ((pScript->m104_rotation & 0x8000) && pScript->mE8_currentAnimationId == 6) {
                pScript->m4_flags |= 0x1000;
            }
            walk = 6;
        }
    }
    if (pScript->mEA_forcedAnimation != 0xFF) {
        walk = pScript->mEA_forcedAnimation;
    }
    if (pScript->mE8_currentAnimationId != walk && !(pScript->m0_fieldScriptFlags & 0x2000000)) {
        pScript->mE8_currentAnimationId = walk;
        setVisualAnimation(pSprite, walk, pEntity);
    }
    if (pScript->m14_currentTriangleFlag & 0x100) {
        step.vx >>= 1;
        step.vz >>= 1;
    }
    pScript->m30_stepVector.vx = step.vx;
    pScript->m30_stepVector.vy = step.vy;
    pScript->m30_stepVector.vz = step.vz;
    pScript->m40_externalStep.vx = 0;
    pScript->m40_externalStep.vy = 0;
    pScript->m40_externalStep.vz = 0;
}

s32 decayTowardZeroClamped(s32 value, s32 limit) {
    if (value < 0) {
        value += 0x4000;
        if (value < -limit) {
            value = -limit;
        }
        if (value > 0) {
            value = 0;
        }
    } else {
        value -= 0x4000;
        if (limit < value) {
            value = limit;
        }
        if (value < 0) {
            value = 0;
        }
    }
    return value;
}

void Vec3ToSVec3(SVECTOR* out, VECTOR* in) {
    out->vx = in->vx >> 16;
    out->vy = in->vy >> 16;
    out->vz = in->vz >> 16;
}

void turnMovingActorQuarterTurn(SpriteActorCore* actor, sFieldScriptEntity* pScript, sFieldEntity* pEntity, s32 arg3) {
    s16 rotation;
    

    if (pScript->m30_stepVector.vx != 0 || pScript->m30_stepVector.vz != 0) {
        rotation = pScript->m106_currentRotation;
        if (!(rotation & 0x8000)) {
            if (arg3 & 1) {
                pScript->m104_rotation = (rotation - 0x400) & 0xFFF;
                pScript->m106_currentRotation = pScript->m104_rotation;
            } else {
                pScript->m104_rotation = (rotation + 0x400) & 0xFFF;
                pScript->m106_currentRotation = pScript->m104_rotation;
            }
            updateEntityEventCode3Sub1(actor, pScript->m104_rotation, pEntity);
            pScript->m106_currentRotation = pScript->m104_rotation = pScript->m106_currentRotation | 0x8000;
        }
    }
}

s32 CheckCollisionWithMesh(s32 actorId, ModelBlock* pModel, s32 stepX, s32 stepZ, s32* outputY, VECTOR* outputNormal) {
    sEntityMoveCheck0Sub1* p;
    u32* pBlock;
    MATRIX* m1;
    MATRIX* m2;
    MATRIX* m3;
    s32 i;
    s32 j;
    u32 header;
    u32 type;
    s32 count;

    p = allocateScratchBufferLocationForMoveCheck(0xB8);
    pBlock = (u32*)pModel->m10_meshBlocks;
    p->mA4_vertices = pModel->m8_vertices;
    p->m10_testPointXY = (stepX << 16) + stepZ;
    p->mA0_smallestY = 0x7FFFFFFF;
    switch (fieldSceneState.m290_actorArray[actorId].m4C_scriptEntity->m12C_x0) {
    case 1:
        p->mB0_rotation.vx = fieldSceneState.m290_actorArray[actorId].m4C_scriptEntity->m70_rotationForRendering;
        p->mB0_rotation.vy = 0;
        p->mB0_rotation.vz = 0;
        goto rotation;
    case 2:
        p->mB0_rotation.vx = 0;
        p->mB0_rotation.vy = fieldSceneState.m290_actorArray[actorId].m4C_scriptEntity->m70_rotationForRendering;
        p->mB0_rotation.vz = 0;
        goto rotation;
    case 3:
        p->mB0_rotation.vx = 0;
        p->mB0_rotation.vy = 0;
        p->mB0_rotation.vz = fieldSceneState.m290_actorArray[actorId].m4C_scriptEntity->m70_rotationForRendering;
    rotation:
        createRotationMatrix(&p->mB0_rotation, &p->m60_modelMatrix);
        MulMatrix2(&fieldSceneState.m290_actorArray[actorId].mC_matrix, &p->m60_modelMatrix);
        p->m60_modelMatrix.t[0] = fieldSceneState.m290_actorArray[actorId].mC_matrix.t[0];
        p->m60_modelMatrix.t[1] = fieldSceneState.m290_actorArray[actorId].mC_matrix.t[1];
        p->m60_modelMatrix.t[2] = fieldSceneState.m290_actorArray[actorId].mC_matrix.t[2];
        CompMatrix(&fieldSceneState.m224_projectionTempMatrix, &p->m60_modelMatrix, &p->m40_transformMatrix);
        break;
    default:
        p->m40_transformMatrix.t[0] = p->m40_transformMatrix.t[1] = p->m40_transformMatrix.t[2] = 0;
        p->m80_viewMatrix.t[0] = p->m80_viewMatrix.t[1] = p->m80_viewMatrix.t[2] = 0;
        m2 = &p->m80_viewMatrix;
        if (fieldSceneState.m290_actorArray[actorId].m4C_scriptEntity->m75_parentActorId != 0xFF) {
            CompMatrix(&fieldSceneState.m224_projectionTempMatrix, &renderModelRotationMatrix, m2);
            m1 = &D_800AFB10[D_800AFB10[actorId].m4C_scriptEntity->m75_parentActorId].m2C_matrixBackup;
            m3 = m2;
            m2 = &p->m60_modelMatrix;
            CompMatrix(m3, m1, m2);
            CompMatrix(m2, &D_800AFB10[actorId].mC_matrix, &p->m40_transformMatrix);
        } else {
            m3 = &fieldSceneState.m224_projectionTempMatrix;
            m1 = &renderModelRotationMatrix;
            CompMatrix(m3, m1, m2);
            CompMatrix(m2, &D_800AFB10[actorId].mC_matrix, &p->m40_transformMatrix);
        }
        break;
    }
    SetRotMatrix(&p->m40_transformMatrix);
    SetTransMatrix(&p->m40_transformMatrix);
    for (i = pModel->m6_numMeshBlocks; i > 0; i--) {
        header = *pBlock;
        type = header & 0xFF;
        count = header >> 16;
        p->mAC_primType = type;
        if (type == 0xC4 || type == 0xC8) {
            pBlock++;
            continue;
        }
        pBlock++;
        if (!(header & 8)) {
            for (j = 0; j < count; j++) {
                RotTransSV(&p->mA4_vertices[((u16*)pBlock)[0]], &p->m14_transformedVertices[0], &p->m3C_clipFlag);
                RotTransSV(&p->mA4_vertices[((u16*)pBlock)[1]], &p->m14_transformedVertices[1], &p->m3C_clipFlag);
                pBlock++;
                RotTransSV(&p->mA4_vertices[((u16*)pBlock)[0]], &p->m14_transformedVertices[2], &p->m3C_clipFlag);
                pBlock++;
                p->m0_gteXY[0] = (p->m14_transformedVertices[0].vx << 16) + p->m14_transformedVertices[0].vz;
                p->m0_gteXY[1] = (p->m14_transformedVertices[1].vx << 16) + p->m14_transformedVertices[1].vz;
                p->m0_gteXY[2] = (p->m14_transformedVertices[2].vx << 16) + p->m14_transformedVertices[2].vz;
                if (NormalClip(p->m0_gteXY[0], p->m0_gteXY[1], p->m10_testPointXY) >= 0 &&
                    NormalClip(p->m0_gteXY[1], p->m0_gteXY[2], p->m10_testPointXY) >= 0 &&
                    NormalClip(p->m0_gteXY[2], p->m0_gteXY[0], p->m10_testPointXY) >= 0 &&
                    NormalClip(p->m0_gteXY[0], p->m0_gteXY[1], p->m0_gteXY[2]) >= 0) {
                    p->m34_testPosition.vx = stepX;
                    p->m34_testPosition.vz = stepZ;
                    getTriangleNormalAndAdjustY(&p->m14_transformedVertices[0], &p->m14_transformedVertices[1], &p->m14_transformedVertices[2], &p->m34_testPosition, outputNormal);
                    if (p->m34_testPosition.vy < p->mA0_smallestY) {
                        p->mA0_smallestY = p->m34_testPosition.vy;
                    }
                }
            }
        } else {
            for (j = 0; j < count; j++) {
                RotTransSV(&p->mA4_vertices[((u16*)pBlock)[0]], &p->m14_transformedVertices[0], &p->m3C_clipFlag);
                RotTransSV(&p->mA4_vertices[((u16*)pBlock)[1]], &p->m14_transformedVertices[1], &p->m3C_clipFlag);
                pBlock++;
                RotTransSV(&p->mA4_vertices[((u16*)pBlock)[0]], &p->m14_transformedVertices[2], &p->m3C_clipFlag);
                RotTransSV(&p->mA4_vertices[((u16*)pBlock)[1]], &p->m14_transformedVertices[3], &p->m3C_clipFlag);
                pBlock++;
                p->m0_gteXY[0] = (p->m14_transformedVertices[0].vx << 16) + p->m14_transformedVertices[0].vz;
                p->m0_gteXY[1] = (p->m14_transformedVertices[1].vx << 16) + p->m14_transformedVertices[1].vz;
                p->m0_gteXY[2] = (p->m14_transformedVertices[2].vx << 16) + p->m14_transformedVertices[2].vz;
                p->m0_gteXY[3] = (p->m14_transformedVertices[3].vx << 16) + p->m14_transformedVertices[3].vz;
                if (NormalClip(p->m0_gteXY[0], p->m0_gteXY[1], p->m10_testPointXY) >= 0 &&
                    NormalClip(p->m0_gteXY[1], p->m0_gteXY[3], p->m10_testPointXY) >= 0 &&
                    NormalClip(p->m0_gteXY[3], p->m0_gteXY[2], p->m10_testPointXY) >= 0 &&
                    NormalClip(p->m0_gteXY[2], p->m0_gteXY[0], p->m10_testPointXY) >= 0 &&
                    NormalClip(p->m0_gteXY[0], p->m0_gteXY[1], p->m0_gteXY[2]) >= 0) {
                    p->m34_testPosition.vx = stepX;
                    p->m34_testPosition.vz = stepZ;
                    if (NormalClip(p->m0_gteXY[1], p->m0_gteXY[2], p->m10_testPointXY) >= 0) {
                        getTriangleNormalAndAdjustY(&p->m14_transformedVertices[0], &p->m14_transformedVertices[1], &p->m14_transformedVertices[2], &p->m34_testPosition, outputNormal);
                    } else {
                        getTriangleNormalAndAdjustY(&p->m14_transformedVertices[1], &p->m14_transformedVertices[3], &p->m14_transformedVertices[2], &p->m34_testPosition, outputNormal);
                    }
                    if (p->m34_testPosition.vy < p->mA0_smallestY) {
                        p->mA0_smallestY = p->m34_testPosition.vy;
                    }
                }
            }
        }
    }
    if (p->mA0_smallestY == 0x7FFFFFFF) {
        freeScratchBuffer(0xB8);
        return -1;
    }
    *outputY = p->mA0_smallestY;
    freeScratchBuffer(0xB8);
    return 0;
}

void func_80083994(void) {
}

void startScriptsForCollisions(s32 index, sFieldEntity* pPlayerEntity, sFieldScriptEntity* pPlayer) {
    VECTOR v1;
    VECTOR v2;
    VECTOR v3;
    VECTOR v4;
    s32 bTrigger;
    s32 newValue;
    s32 radius8;
    s32 radius20;
    s32 playerY;
    s32 playerBottom;
    s16 rot;
    s32 playerX;
    s32 playerZ;
    s32 i;
    s32 j;
    sFieldScriptEntity* pTested;
    s32 scriptIndex;
    s32 yDiff;
    s32 angle;
    s32 dir;
    s32 diff;
    s32 distance;

    bTrigger = 0;
    newValue = 7;
    playerY = FP_INT(pPlayer->m20_position.vy);
    playerBottom = playerY - (u16)pPlayer->m18_boundingVolume[1];
    radius8 = pPlayer->m1E_collisionRadius + 8;
    radius20 = pPlayer->m1E_collisionRadius + 0x20;
    rot = pPlayer->m106_currentRotation & 0xFFF;
    playerX = FP_INT(pPlayer->m20_position.vx);
    playerZ = FP_INT(pPlayer->m20_position.vz);
    for (i = 0; i < g_totalActors; i++) {
        pTested = D_800AFB10[i].m4C_scriptEntity;
        scriptIndex = 0xFF;
        if (!(pTested->m0_fieldScriptFlags & 1) && pPlayer->m74_touchedActor != i) {
            yDiff = FP_INT(pTested->m20_position.vy) + pTested->m60_interactionOffset.vy;
            if (pTested->m4_flags & 0x180) {
                if (pTested->m4_flags & 0x100) {
                    if ((padButtonForDialogs & 0x20) && !bTrigger && !(pTested->m4_flags & 0x4000000)) {
                        if (!(pTested->m0_fieldScriptFlags & 0x220000) && windowOpenBF == 0) {
                            bTrigger = 1;
                            scriptIndex = 2;
                            newValue = 3;
                            v1.vx = FP_INT(pTested->m20_position.vx) - playerX + pTested->m60_interactionOffset.vx;
                            v1.vz = FP_INT(pTested->m20_position.vz) - playerZ + pTested->m60_interactionOffset.vz;
                            angle = -ratan2(v1.vz, v1.vx);
                            dir = (angle >> 9) & 7;
                            pTested->m12C_x9 = dir;
                        }
                    } else if (!(pTested->m0_fieldScriptFlags & 0xA20000)) {
                        scriptIndex = 3;
                        newValue = 4;
                        v1.vx = FP_INT(pTested->m20_position.vx) - playerX + pTested->m60_interactionOffset.vx;
                        v1.vz = FP_INT(pTested->m20_position.vz) - playerZ + pTested->m60_interactionOffset.vz;
                        dir = -(ratan2(v1.vz, v1.vx) >> 9) & 7;
                        pTested->m12C_x9 = dir;
                        if (startScriptsForCollisionsVar0 == 0 && (pTested->m0_fieldScriptFlags & 0x8000000)) {
                            startScriptsForCollisionsVar0 = 1;
                            pPlayerEntity->m4_pVramSpriteSheet->mC_step.vy = 0;
                        }
                    }
                } else {
                    startScriptsForCollisionsVar0 = 0;
                }
            }
            if (pTested->m0_fieldScriptFlags & 0x2000) {
                if (playerBottom <= yDiff && yDiff - (u16)pTested->m18_boundingVolume[1] <= playerY && i != index &&
                    isPositionInEntityScriptBoundingVolume(playerX, playerZ, pTested, 0x10) == 0) {
                    if ((padButtonForDialogs & 0x20) && !bTrigger && !(pTested->m4_flags & 0x4000000)) {
                        if (!(pTested->m0_fieldScriptFlags & 0x220000) && windowOpenBF == 0) {
                            v1.vx = FP_INT(pTested->m20_position.vx) - playerX + pTested->m60_interactionOffset.vx;
                            v1.vz = FP_INT(pTested->m20_position.vz) - playerZ + pTested->m60_interactionOffset.vz;
                            angle = -ratan2(v1.vz, v1.vx);
                            dir = (angle >> 9) & 7;
                            diff = (rot - (angle & 0xFFF)) & 0xFFF;
                            if (!(pTested->m4_flags & 0x40000) || (u32)(diff - 700) > 0xA88) {
                                bTrigger = 1;
                                scriptIndex = 2;
                                newValue = 3;
                                pTested->m12C_x9 = dir;
                                if (fieldDebugDisable == 0) {
                                    fieldDebugLinesVisible = 1;
                                }
                            }
                        }
                    } else if (!(pTested->m0_fieldScriptFlags & 0xA20000)) {
                        scriptIndex = 3;
                        newValue = 4;
                        v1.vx = FP_INT(pTested->m20_position.vx) - playerX + pTested->m60_interactionOffset.vx;
                        v1.vz = FP_INT(pTested->m20_position.vz) - playerZ + pTested->m60_interactionOffset.vz;
                        dir = -(ratan2(v1.vz, v1.vx) >> 9) & 7;
                        pTested->m12C_x9 = dir;
                        if (fieldDebugDisable == 0) {
                            fieldDebugLinesVisible = 1;
                        }
                    }
                }
            } else {
                v1.vx = FP_INT(pTested->m20_position.vx) - playerX + pTested->m60_interactionOffset.vx;
                v1.vz = FP_INT(pTested->m20_position.vz) - playerZ + pTested->m60_interactionOffset.vz;
                v1.vy = radius20 + pTested->m1E_collisionRadius;
                gte_ldlvl(&v1);
                gte_sqr0();
                gte_stlvnl(&v2);
                if (v2.vx + v2.vz < v2.vy && playerBottom <= yDiff && yDiff - (u16)pTested->m18_boundingVolume[1] <= playerY &&
                    i != index) {
                    v1.vx = FP_INT(pTested->m20_position.vx) - playerX + pTested->m60_interactionOffset.vx;
                    v1.vz = FP_INT(pTested->m20_position.vz) - playerZ + pTested->m60_interactionOffset.vz;
                    Square0(&v1, &v2);
                    v3.vx = radius8 + pTested->m1E_collisionRadius;
                    v3.vz = radius20 + pTested->m1E_collisionRadius;
                    distance = v2.vx + v2.vz;
                    Square0(&v3, &v4);
                    if (distance < v4.vz && (padButtonForDialogs & 0x20) && !bTrigger && !(pTested->m4_flags & 0x4000000)) {
                        if (!(pTested->m0_fieldScriptFlags & 0x220000)) {
                            angle = -ratan2(v1.vz, v1.vx);
                            dir = (angle >> 9) & 7;
                            diff = (rot - (angle & 0xFFF)) & 0xFFF;
                            if ((u32)(diff - 700) > 0xA88 && windowOpenBF == 0) {
                                bTrigger = 1;
                                scriptIndex = 2;
                                newValue = 3;
                                pTested->m12C_x9 = dir;
                            }
                        }
                    } else if (!(pTested->m0_fieldScriptFlags & 0xA20000) && distance < v4.vx) {
                        scriptIndex = 3;
                        newValue = 4;
                        dir = -(ratan2(v1.vz, v1.vx) >> 9) & 7;
                        pTested->m12C_x9 = dir;
                    }
                }
            }
        }
        if (scriptIndex != 0xFF) {
            for (j = 0; j < 8; j++) {
                if (pTested->m8C_scriptSlots[j].m3_scriptIndex == (u8)scriptIndex) {
                    break;
                }
            }
            if (j == 8) {
                for (j = 0; j < 8; j++) {
                    if (pTested->m8C_scriptSlots[j].m4_x12_priority == 0xF && pTested->m8C_scriptSlots[j].m4_x16 == 0) {
                        pTested->m8C_scriptSlots[j].m0_scriptPC = getScriptEntryPoint(i, scriptIndex);
                        pTested->m8C_scriptSlots[j].m3_scriptIndex = scriptIndex;
                        pTested->m8C_scriptSlots[j].m4_x12_priority = newValue;
                        pTested->m104_rotation = pTested->m106_currentRotation = pTested->m106_currentRotation | 0x8000;
                        break;
                    }
                }
            }
        }
    }
}

void EntityMoveCheck0(s32 index, sFieldEntity* pPlayerEntity, sFieldScriptEntity* pPlayer) {
    FP_VEC3 post;
    SVECTOR sfp;
    VECTOR normal;
    s32 y;
    s32 stepX;
    s32 stepZ;
    s32 playerBottom;
    u32 playerFlags;
    s32 finalState;
    s32 finalCount;
    u32 playerTouched;
    VECTOR* scratch;
    s32 bestDistance;
    s32 playerY;
    s32 i;
    sFieldScriptEntity* pCur;
    u32 entityFlags;
    u32 flags4;
    s32 testedY;

    scratch = allocateScratchBufferLocationForMoveCheck(0x20);
    post.vx = pPlayer->m20_position.vx;
    post.vy = pPlayer->m20_position.vy;
    post.vz = pPlayer->m20_position.vz;
    post.vx += pPlayer->m30_stepVector.vx;
    post.vy += pPlayer->m30_stepVector.vy;
    post.vz += pPlayer->m30_stepVector.vz;
    Vec3ToSVec3(&sfp, (VECTOR*)&post);
    stepX = sfp.vx;
    stepZ = sfp.vz;
    playerY = FP_INT(pPlayer->m20_position.vy);
    playerBottom = playerY - (u16)pPlayer->m18_boundingVolume[1];
    bestDistance = 0x7FFFFFFF;
    finalState = 0;
    finalCount = 0;
    playerFlags = pPlayer->m0_fieldScriptFlags;
    playerTouched = pPlayer->m74_touchedActor;
    for (i = 0; i < g_totalActors; i++) {
        if (i == index) {
            continue;
        }
        if (D_800AFB10[i].m4C_scriptEntity->m0_fieldScriptFlags & 1) {
            continue;
        }
        pCur = D_800AFB10[i].m4C_scriptEntity;
        entityFlags = pCur->m0_fieldScriptFlags;
        pCur->m4_flags &= ~0xC100;
        if (pCur->m4_flags & 0x80) {
            if (CheckCollisionWithMesh(i, fieldSceneState.m290_actorArray[i].m0_model->m4_pModelBlock, stepX, stepZ, &y, &normal)) {
                pCur->m4_flags &= ~0xC00000;
                continue;
            }
            if (fieldDebugDisable == 0) {
                sprintf_screen("POLYCHECK %d\n", i);
            }
            pCur->m4_flags |= 0x100;
            testedY = y + (u16)pCur->m18_boundingVolume[1];
            if (pPlayer->m74_touchedActor == i) {
                pPlayer->m50_surfaceNormal.vx = normal.vx;
                pPlayer->m50_surfaceNormal.vy = normal.vy;
                pPlayer->m50_surfaceNormal.vz = normal.vz;
                pCur->m4_flags |= 0x4000;
            }
        } else {
            if (entityFlags & 0x2000) {
                if (isPositionInEntityScriptBoundingVolume(stepX, stepZ, pCur, 0)) {
                    pCur->m4_flags &= ~0xC00000;
                    continue;
                }
            } else {
                scratch[0].vx = ((pCur->m20_position.vx + pCur->m30_stepVector.vx) >> 16) - stepX;
                scratch[0].vz = ((pCur->m20_position.vz + pCur->m30_stepVector.vz) >> 16) - stepZ;
                scratch[0].vy = pPlayer->m1E_collisionRadius + pCur->m1E_collisionRadius;
                Square0(&scratch[0], &scratch[1]);
                if (scratch[1].vx + scratch[1].vz >= scratch[1].vy) {
                    pCur->m4_flags &= ~0xC00000;
                    continue;
                }
            }
            if (pPlayer->m14_currentTriangleFlag & 0x400000) {
                if (fieldDebugDisable == 0) {
                    sprintf_screen("HITOFF\n");
                }
                continue;
            }
            if ((entityFlags | playerFlags) & 0x80) {
                continue;
            }
            if (noUpdatesToPartyMembers != 0) {
                continue;
            }
            testedY = FP_INT(pCur->m20_position.vy);
            y = testedY - (u16)pCur->m18_boundingVolume[1];
        }
        if (pPlayer->m74_touchedActor == i && (playerFlags & 0x40800) == 0) {
            goto touching;
        }
        if (testedY >= playerBottom && playerY >= y) {
            if (playerY < y + 0x10 || (pCur->m4_flags & 0x800000)) {
            touching:
                bestDistance = y;
                pCur->m4_flags |= 0x800000;
                pPlayer->m40_externalStep.vx = pCur->m30_stepVector.vx;
                pPlayer->m40_externalStep.vy = pCur->m30_stepVector.vy;
                pPlayer->m40_externalStep.vz = pCur->m30_stepVector.vz;
                finalCount = 2;
                if ((playerFlags & 0x40800) == 0) {
                    pPlayer->m74_touchedActor = i;
                    finalState = 1;
                }
            } else {
                if (!(entityFlags & 0x10)) {
                    if (pCur->mE3_pushCounter < 0x30 && !pcFieldOffTick()) { /* Enhanced (xg-hbc.19.3): a push builds up
                                                                               * over native frames (at 60 Hz it gave way twice as soon) */
                        pCur->mE3_pushCounter += 2;
                    }
                    if (pCur->mE3_pushCounter > 0x20) {
                        pCur->m40_externalStep.vx += pPlayer->m30_stepVector.vx / 4;
                        pCur->m40_externalStep.vz += pPlayer->m30_stepVector.vz / 4;
                        pPlayer->m30_stepVector.vx = 0;
                        pPlayer->m30_stepVector.vy = 0;
                        pPlayer->m30_stepVector.vz = 0;
                        pPlayer->m40_externalStep.vx = pPlayer->m30_stepVector.vx / 4;
                        pPlayer->m40_externalStep.vy = 0;
                        pPlayer->m40_externalStep.vz = pPlayer->m30_stepVector.vz / 4;
                        goto done;
                    }
                }
                pCur->m40_externalStep.vx = 0;
                pCur->m40_externalStep.vy = 0;
                pCur->m40_externalStep.vz = 0;
                pCur->m30_stepVector.vx = 0;
                pCur->m30_stepVector.vy = 0;
                pCur->m30_stepVector.vz = 0;
                pPlayer->m30_stepVector.vx = 0;
                pPlayer->m30_stepVector.vy = 0;
                pPlayer->m30_stepVector.vz = 0;
                pPlayer->m40_externalStep.vx = 0;
                pPlayer->m40_externalStep.vy = 0;
                pPlayer->m40_externalStep.vz = 0;
            }
        } else {
            pCur->m4_flags &= ~0x100;
            if (playerY < y) {
                pCur->m4_flags |= 0x800000;
                if (y < bestDistance) {
                    bestDistance = y;
                }
            } else {
                pCur->m4_flags &= ~0x800000;
            }
        }
    done:
        pCur->m4_flags |= 0x400000;
    }
    if (updateEntityEventCode3Var1 != 0) {
        bestDistance = EntityMoveCheck0Var0;
        finalState = 0;
        finalCount++;
    }
    if (finalState == 0) {
        pPlayer->m74_touchedActor = 0xFF;
    } else {
        D_800AFB10[pPlayer->m74_touchedActor].m4C_scriptEntity->m4_flags |= 0x8000;
        if (playerTouched == 0xFF && (pPlayer->m134_x7 || !pcFieldOffTick())) {
            /* (Enhanced, xg-hbc.19.3: a frame between's preview allocates nothing; the tick does) */
            if (!pPlayer->m134_x7) {
                pPlayer->m110_modelRotation2 = allocateHeap(PC_HEAP_BYTES(0xC), 0);
                pPlayer->m134_x7 = 1;
            }
            pPlayer->m110_modelRotation2[0] = fieldSceneState.m290_actorArray[pPlayer->m74_touchedActor].m50_modelRotation.vx;
            pPlayer->m110_modelRotation2[1] = fieldSceneState.m290_actorArray[pPlayer->m74_touchedActor].m50_modelRotation.vy;
            pPlayer->m110_modelRotation2[2] = fieldSceneState.m290_actorArray[pPlayer->m74_touchedActor].m50_modelRotation.vz;
            pPlayer->m110_modelRotation2[4] = computeDistanceBetweenActors(index, pPlayer->m74_touchedActor);
        }
    }
    if (!(pPlayer->m0_fieldScriptFlags & 0x10000) && !(pPlayer->m4_flags & 0x200000)) {
        EntityMoveCheck1(index, bestDistance, pPlayerEntity, pPlayer, finalCount);
    }
    if (D_800AFB10[index].m4_pVramSpriteSheet->m7C_extra->mC_animationSignal == 1) {
        if (!pcFieldOffTick()) {
            resetInputs(); /* (Enhanced, xg-hbc.19.3: the input buffer is the game's: not from a preview) */
        }
        pPlayer->m0_fieldScriptFlags &= ~0x800;
    }
    freeScratchBuffer(0x20);
}

s32 updateEntityEventCode3Sub0(sFieldScriptEntity* pEntity) {
    if (pEntity->m14_currentTriangleFlag & 0x420000) {
        goto fail;
    }
    if (updateEntityEventCode3Var1 != 0) {
        return -1;
    }
    if (pEntity->m30_stepVector.vx != 0) {
        return -1;
    }
    if (pEntity->m30_stepVector.vy != 0) {
        return -1;
    }
    if (pEntity->m30_stepVector.vz != 0) {
        return -1;
    }
    if (updateEntityEventCode3Var2 != 1) {
        return -1;
    }
    if (pEntity->m74_touchedActor != 0xFF) {
        return -1;
    }
    if (pEntity->m0_fieldScriptFlags & 0x401800) {
        return -1;
    }
    if ((pEntity->m4_flags & 1) && pEntity->m10_walkmeshId == 0) {
        return -1;
    }
    if ((pEntity->m4_flags & 2) && pEntity->m10_walkmeshId == 1) {
        return -1;
    }
    if ((pEntity->m4_flags & 4) && pEntity->m10_walkmeshId == 2) {
        return -1;
    }
    return 0;
fail:
    return -1;
}

s32 EntityMoveCheck1(s32 entityIndex, s32 maxAltitude, sFieldEntity* pFieldEntity, sFieldScriptEntity* pScript, u32 param_5) {
    s32 altitudes[4];
    s32 altitudes2[4];
    s32 walkmeshIds[4];
    s16 triangleIds[4];
    VECTOR normals[4];
    s32 position[4];
    s16 triangles[4];
    SpriteActorCore* pSprite;
    s16 walkmeshId;
    s32 i;
    s32 j;
    s32 count;
    s32 altitude;
    u32 flag;
    s32 swap;
    s32 value;

    pSprite = D_800AFB10[entityIndex].m4_pVramSpriteSheet;
    if (entityIndex == g_playerControlledActor) {
        inputAllowedMask = 0xFFFF;
    }
    if (pScript->m0_fieldScriptFlags & 0x1000000) {
        return -1;
    }
    if (pScript->m4_flags & 0x200000) {
        return -1;
    }
    if (pScript->m0_fieldScriptFlags & 0x10000) {
        return -1;
    }
    if ((entityIndex != g_playerControlledActor || g_PartyFollowDisabled != 1) && pSprite->mC_step.vy == 0 && !updateEntityEventCode3Sub0(pScript) &&
        pSprite->m84_maxY == FP_INT(pScript->m20_position.vy)) {
        return -1;
    }
    position[0] = pScript->m20_position.vx;
    position[1] = pScript->m20_position.vy;
    position[2] = pScript->m20_position.vz;
    walkmeshId = pScript->m10_walkmeshId;
    for (i = 0; i < 4; i++) {
        triangles[i] = pScript->m8_currentWalkMeshTriangle[i];
    }
    for (i = 0; i < 4; i++) {
        walkmeshIds[i] = i;
        altitudes[i] = 0x7FFFFFFF;
        altitudes2[i] = 0x7FFFFFFF;
    }
    for (count = 0; count < fieldSceneState.m2D4_numWalkMesh - 1; count++) {
        if (EntityMoveCheck1Sub1(pScript, count, &altitudes[count], &normals[count], &triangleIds[count], &altitudes2[count])) {
            break;
        }
    }
    if (pScript->m4_flags & 1) {
        altitudes[0] = 0x7FFFFFFF;
        altitudes2[0] = 0x7FFFFFFF;
    }
    if (pScript->m4_flags & 2) {
        altitudes[1] = 0x7FFFFFFF;
        altitudes2[1] = 0x7FFFFFFF;
    }
    if (pScript->m4_flags & 4) {
        altitudes[2] = 0x7FFFFFFF;
        altitudes2[2] = 0x7FFFFFFF;
    }
    altitude = altitudes[pScript->m10_walkmeshId];
    for (i = 0; i < 2; i++) {
        for (j = 0; j < 2; j++) {
            if (altitudes[j] > altitudes[j + 1]) {
                swap = altitudes[j + 1];
                altitudes[j + 1] = altitudes[j];
                altitudes[j] = swap;
                value = altitudes2[j + 1];
                altitudes2[j + 1] = altitudes2[j];
                altitudes2[j] = value;
                value = walkmeshIds[j + 1];
                walkmeshIds[j + 1] = walkmeshIds[j];
                walkmeshIds[j] = value;
            }
        }
    }
    if (count == fieldSceneState.m2D4_numWalkMesh - 1) {
        for (i = 0; i < fieldSceneState.m2D4_numWalkMesh - 1; i++) {
            pScript->m8_currentWalkMeshTriangle[i] = triangleIds[i];
        }
        value = FP_INT(pScript->m20_position.vy);
        if (value < altitude || (pScript->m0_fieldScriptFlags & 0x1800)) {
            for (i = 0; i < fieldSceneState.m2D4_numWalkMesh - 1; i++) {
                if (value <= altitudes[i]) {
                    pScript->m10_walkmeshId = walkmeshIds[i];
                    break;
                }
            }
        } else {
            for (i = 0; i < fieldSceneState.m2D4_numWalkMesh - 1; i++) {
                if (pScript->m10_walkmeshId == walkmeshIds[i]) {
                    break;
                }
            }
        }
        if ((getWalkmeshTriangleFlag(pScript) & 4) && i != 0 && pScript->m10_walkmeshId <= fieldSceneState.m2D4_numWalkMesh - 1) {
            i--;
            pScript->m10_walkmeshId = walkmeshIds[i];
        }
        flag = getWalkmeshTriangleFlag(pScript);
        if ((u8)((pScript->m0_fieldScriptFlags >> 8) & 7) & (flag >> 5)) {
            if (fieldDebugDisable == 0) {
                sprintf_screen("ERROR ID1 ACT=%d\n", entityIndex);
            }
        } else if (flag & 0x800000) {
            if (fieldDebugDisable == 0) {
                sprintf_screen(strErrorId0Act, entityIndex); /* "ERROR ID0 ACT=%d\n" */
            }
        } else {
            goto normal;
        }
        if (entityIndex == g_playerControlledActor) {
            inputAllowedMask = 0xFFF;
        }
        pScript->m20_position.vy += pSprite->mC_step.vy;
        goto blocked;
    normal:
        pScript->m20_position.vx += pScript->m30_stepVector.vx;
        pScript->m20_position.vz += pScript->m30_stepVector.vz;
        for (i = 0; i < fieldSceneState.m2D4_numWalkMesh - 1; i++) {
            if (pScript->m10_walkmeshId == walkmeshIds[i]) {
                pSprite->m84_maxY = altitudes[i];
                break;
            }
        }
        VectorNormal(&normals[pScript->m10_walkmeshId], &pScript->m50_surfaceNormal);
    } else {
        pScript->mF0_fallSpeed = 0;
    }

    if (updateEntityEventCode3Var1 != 0) {
        if (param_5 < 2) {
            pSprite->m84_maxY = maxAltitude;
        }
    } else if (param_5 != 0) {
        if (pSprite->m84_maxY < maxAltitude + 10) {
            pScript->m74_touchedActor = 0xFF;
        }
        pSprite->m84_maxY = maxAltitude;
        pScript->m20_position.vy = maxAltitude << 16;
    }
    if (pScript->m0_fieldScriptFlags & 0x40000) {
        pScript->m20_position.vy = pScript->mEC_elevation << 16;
        pSprite->mC_step.vy = 0;
    }
    pScript->m20_position.vy += pSprite->mC_step.vy;
    flag = getWalkmeshTriangleFlag(pScript);
    if (pScript->m10_walkmeshId != walkmeshId) {
        pScript->m0_fieldScriptFlags &= ~0x4000000;
    }
    if (!(pScript->m0_fieldScriptFlags & 0x4000000) && pSprite->m84_maxY > FP_INT(pScript->m20_position.vy)) {
        if (pSprite->m84_maxY != FP_INT(pScript->m20_position.vy)) {
            pSprite->mC_step.vy += pSprite->m1C_gravity;
        }
        pScript->m0_fieldScriptFlags |= 0x1000;
        pScript->mF0_fallSpeed = pSprite->mC_step.vy;
    } else {
        if (!(flag & 0x420000)) {
            pScript->mF0_fallSpeed = 0;
        }
        if (pSprite->mC_step.vy > 0) {
            pSprite->mC_step.vy = 0;
        }
        pScript->m0_fieldScriptFlags &= ~0x401000;
        pScript->m20_position.vy = pSprite->m84_maxY << 16;
    }
    pScript->m0_fieldScriptFlags &= ~0x4000000;
    for (i = 0; i < fieldSceneState.m2D4_numWalkMesh - 1; i++) {
        if (altitudes[i] < FP_INT(pScript->m20_position.vy) &&
            FP_INT(pScript->m20_position.vy) - (u16)pScript->m18_boundingVolume[1] < altitudes2[i] &&
            altitudes[i] != altitudes2[i]) {
            break;
        }
    }
    if (i == fieldSceneState.m2D4_numWalkMesh - 1 &&
        ((swap = ((s8*)fieldSceneState.m2A4_walkMeshTriangle[pScript->m10_walkmeshId])[pScript->m8_currentWalkMeshTriangle[pScript->m10_walkmeshId] * 0xE + 0xD] * 4) >= 0 ||
         FP_INT(pScript->m20_position.vy) - (u16)pScript->m18_boundingVolume[1] >= swap + pSprite->m84_maxY)) {
        pSprite->m0_position.vx = pScript->m20_position.vx;
        pSprite->m0_position.vy = pScript->m20_position.vy;
        pSprite->m0_position.vz = pScript->m20_position.vz;
        fieldSceneState.m290_actorArray[entityIndex].mC_matrix.t[0] = FP_INT(pScript->m20_position.vx);
        fieldSceneState.m290_actorArray[entityIndex].mC_matrix.t[1] = FP_INT(pScript->m20_position.vy);
        fieldSceneState.m290_actorArray[entityIndex].mC_matrix.t[2] = FP_INT(pScript->m20_position.vz);
        pScript->m14_currentTriangleFlag = getWalkmeshTriangleFlag(pScript);
    } else {
        pScript->m20_position.vx = position[0];
        pScript->m20_position.vz = position[2];
        pScript->m10_walkmeshId = walkmeshId;
        pScript->mF0_fallSpeed = 0;
        for (i = 0; i < 4; i++) {
            pScript->m8_currentWalkMeshTriangle[i] = triangles[i];
        }
        if (pSprite->m84_maxY != FP_INT(pScript->m20_position.vy)) {
            pSprite->mC_step.vy += pSprite->m1C_gravity;
        }
        if (pSprite->mC_step.vy < 0) {
            pSprite->mC_step.vy = 0;
            pScript->m20_position.vy = position[1];
        }
        pSprite->m0_position.vx = pScript->m20_position.vx;
        pSprite->m0_position.vy = pScript->m20_position.vy;
        pSprite->m0_position.vz = pScript->m20_position.vz;
        fieldSceneState.m290_actorArray[entityIndex].mC_matrix.t[1] = FP_INT(pScript->m20_position.vy);
    }
    initFollowStructForPlayer(entityIndex);
    return 0;
blocked:
    pScript->m20_position.vx = position[0];
    pScript->m20_position.vz = position[2];
    pScript->m10_walkmeshId = walkmeshId;
    pScript->mF0_fallSpeed = 0;
    for (i = 0; i < 4; i++) {
        pScript->m8_currentWalkMeshTriangle[i] = triangles[i];
    }
    if (pSprite->m84_maxY > FP_INT(pScript->m20_position.vy)) {
        if (pSprite->m84_maxY != FP_INT(pScript->m20_position.vy)) {
            pSprite->mC_step.vy += pSprite->m1C_gravity;
        }
    } else {
        if (pSprite->mC_step.vy > 0) {
            pSprite->mC_step.vy = 0;
        }
        pScript->m0_fieldScriptFlags &= ~0x401000;
        pScript->m20_position.vy = pSprite->m84_maxY << 16;
    }
    pSprite->m0_position.vx = pScript->m20_position.vx;
    pSprite->m0_position.vy = pScript->m20_position.vy;
    pSprite->m0_position.vz = pScript->m20_position.vz;
    fieldSceneState.m290_actorArray[entityIndex].mC_matrix.t[1] = FP_INT(pScript->m20_position.vy);
    initFollowStructForPlayer(entityIndex);
    return 0;
}

s32 loadMusicPhase0Update(void) {
    u8* result = getNextStreamedSector();
    lastStreamedMusicSector = result;
    if (result != 0) {
        musicLoadedCallback(result);
        return 0;
    }
    if (isCDBusy() == 0 && lastStreamedMusicSector == 0) {
        freeHeap(musicLoadBuffer2);
        loadCompleted = 0;
        return -1;
    }
    return 0;
}

void loadMusicPhase0(s32 fileIndex, s32 flags, void (*callback)(u8*)) {
    loadCompleted = 1;
    musicLoadBuffer2 = allocateBufferForVramUpload(8, flags);
    pcSlideEdgeMusicRead(readFile(fileIndex, musicLoadBuffer2, 0, 0x100));
    musicLoadedCallback = callback;
}

void OP_C4SubSub1(s32 id, s16 volume, s16 pan, s32 channel) {
    channel &= 7;
    StopSoundEffect(channel * 2);
    PlaySoundEffect2(id, channel * 2, volume, pan);
    pcFieldSfxPlayed(id, volume, pan, channel); /* a menu's sounds may cut a loop: sfxkeep.c */
}

void playFieldSoundEffect(s32 id, u32 channel) {
    channel &= 7;
    if (id == 0) {
        StopSoundEffect(channel * 2);
    } else {
        op9DVar1.m34_lastSoundEffectId = id;
        OP_C4SubSub1(id, 0x7F, 0x40, channel);
    }
}

void updateMovieSoundEvents(void) {
    if (fieldMovieParams.m18_soundSequenceId != 0xFF) {
        for (;;) {
            u16* time = &movieSoundEventTable[movieSoundEventIndex].m0_time;
            u16* pValue;
            u16 v;
            if (fieldMovieCurrentFrame < *time + fieldMovieParams.mC_startFrame) {
                break;
            }
            pValue = &movieSoundEventTable[movieSoundEventIndex].m2_value;
            v = *pValue;
            playSoundEffectOnChannel((v & 0xFF) | (D_800B235C->m14 << 16), (v >> 7) & 0xE);
            movieSoundEventIndex++;
        }
    }
}

void unloadMovieSoundSequence(void) {
    if (fieldMovieParams.m18_soundSequenceId != 0xFF) {
        stopAllSounds();
        unloadSequence(D_800B235C);
        freeHeap(D_800B235C);
    }
}

void loadMovieSoundSequence(void) {
    s32 n;
    s32 file;
    s32 i, j;

    if (fieldMovieParams.m18_soundSequenceId != 0xFF) {
        n = fieldMovieParams.m18_soundSequenceId;
        stopAllSounds();
        setCurrentDirectory(0x1C, 0);
        file = n + 0x115;
        D_800B235C = allocateHeap(PC_HEAP_BYTES(getFileSizeAligned(file)), 1);
        readFile(file, D_800B235C, 0, 0x80);
        waitReadCompletion(0);
        loadSequence(D_800B235C);
        waitForMusic(0x10);
        setCurrentDirectory(4, 0);
        j = 0;
        for (i = 0; i < n + 1; i++) {
            for (;;) {
                u16* p = &movieSoundEventTable[j].m0_time;
                if (*p == 0xFFFF) {
                    break;
                }
                j++;
            }
            j++;
            movieSoundEventIndex = j;
        }
    }
}

void loadFieldSeq(void) {
    s32 size;

    setCurrentDirectory(4, 0);
    size = getFileSizeAligned(0xA8);
    fieldSeq = allocateHeap(PC_HEAP_BYTES(size), 0);
    flagAllocation(fieldSeq);
    if (loadFieldSeqVar0 == -1) {
        readFile(0xA8, fieldSeq, 0, 0x80);
        waitReadCompletion(0);
    } else {
        __builtin_memcpy(fieldSeq, loadFieldSeqVar1, size);
        unflagAllocation(loadFieldSeqVar1);
        freeHeap(loadFieldSeqVar1);
    }
    loadSequence(fieldSeq);
    waitForMusic(0x10);
    setCurrentDirectory(4, 0);
    loadFieldSeqVar0 = -1;
}

void unloadFieldSeq(void) {
    unloadSequence(fieldSeq);
    unflagAllocation(fieldSeq);
    freeHeap(fieldSeq);
    loadFieldSeqVar0 = -1;
}

void loadMusicPhase0UploadData(u8* sector) {
    switch (playMusicVar3) {
    case 0:
    case 1:
    case 2:
    case 3:
        ((FieldSector*)musicLoadBuffer)[playMusicVar3] = *(FieldSector*)sector;
        playMusicVar3++;
        releaseSectorSlot(sector);
        if (playMusicVar3 == 4) {
            worldmapWdsResult = playMusic2((Wds*)musicLoadBuffer, 0x2000, 0);
        }
        break;
    case 4:
        waitForMusic(0x10);
        *(FieldSector*)musicLoadBuffer = *(FieldSector*)sector;
        sendSamples2(musicLoadBuffer, 0x800);
        releaseSectorSlot(sector);
        break;
    }
}

void playMusic(s32 musicId) {
    s32 wave;

    waitReadCompletion(0);
    resetBattleAudioState();
    if (musicId == 0xFF) {
        fieldMusicLoadPending = 0;
    } else {
        setCurrentDirectory(0x1C, 0);
        if (musicLookupFlags[musicId * 2] == 1) {
            playMusicSub();
        }
        wave = musicLookupFlags[musicId * 2 - 1]; /* musicLookupTable[musicId][0] */
        if (wave != 0xFF) {
            s32 file = wave * 2 + 0x13;
            if (currentlyLoadedWave != wave) {
                loadMusicPhase0(file, 1, loadMusicPhase0UploadData);
                musicVar2 = 1;
                playMusicVar3 = 0;
                musicLoadBuffer = allocateHeap(PC_HEAP_BYTES(0x2000), 1);
            }
        }
        setCurrentDirectory(4, 0);
        fieldMusicLoadPending = -1;
        isMusicLoadingStartPending = 1;
    }
}

s32 updateMusicLoadingState(void) {
    s32 i;
    for (i = 0; i < 5; i++) {
        if (loadMusicPhase0Update() == -1) {
            return 0;
        }
    }
    return -1;
}

s32 updateMusicState2(s32 musicId) {
    s32 result = musicVar2;

    if (result == 1) {
        if (updateMusicLoadingState() == -1) {
            return -1;
        }
        waitForMusic(0x10);
        freeHeap(musicLoadBuffer);
        musicVar2 = 0;
        needReloadWds = result;
        currentlyLoadedWave = musicLookupTable[musicId][0];
    }
    if (musicLookupTable[musicId][1] == 0) {
        if (playMusicSubVar2 == 0) {
            startReloadingResidentWds();
            return -1;
        }
        if (playMusicSubVar2 & 0x80) {
            if (finishReloadingResidentWds() == -1) {
                return -1;
            }
        }
    }
    if (isMusicLoadingStartPending == 1) {
        if (currentlyLoadedMusic != musicId) {
            setCurrentDirectory(0x1C, 0);
            readFile(musicId * 2 + 0x14, battleMusic, 0, 0x80);
            updateMusicState2Var1 = 1;
            setCurrentDirectory(4, 0);
        }
        isMusicLoadingStartPending = 0;
        return -1;
    }
    if (isCDBusy() != 0) {
        return -1;
    }
    if (updateMusicState2Var1 == 1) {
        if (updateMusicState2Var2 == 0) {
            pMusic = createMusicInstance(battleMusic);
            if (musicVar1 == -1) {
                startMusicInstance(pMusic, 0x7F, 0);
            } else {
                startMusicInstance(pMusic, 0, 0);
                playBattleMusic(pMusic, 0, 0);
            }
            /* Original-game bug fixes (port/src/game/field/scriptfix.c) */
            pcFieldMusicStarted(musicId, pMusic);
        } else {
            pMusic = (void*)savedMusicInstance;
            resumeMusicInstance((void*)savedMusicInstance, 0x7F, 0xF0);
            updateMusicState2Var2 = 0;
            savedMusicInstance = 0;
        }
        updateMusicState2Var1 = 0;
        needStartMusicInstanceSub0 = 1;
        currentlyLoadedMusic = musicId;
    }
    musicVar1 = -1;
    updateMusicState2Var3 = 1;
    return 0;
}

void clearMusic2(void) {
    if (savedMusicInstance != 0) {
        startMusicInstanceSub0(savedMusicInstance);
        freeSoundInstance(savedMusicInstance);
        savedMusicInstance = 0;
    }
}

s32 finishReloadingResidentWds(void) {
    if (isCDBusy() != 0) {
        return -1;
    }
    wdsFile3 = fieldWdsSlot1 = loadWds(residentWdsBuffer, 0);
    waitForMusic(0x10);
    freeHeap(residentWdsBuffer);
    playMusicSubVar2 = 1;
    playWorldmapMusic = 0;
    playMusicSubVar0 = 0;
    return 0;
}

void startReloadingResidentWds(void) {
    setCurrentDirectory(0x1C, 0);
    residentWdsBuffer = allocateHeap(PC_HEAP_BYTES(getFileSizeAligned(3)), 1);
    readFile(3, residentWdsBuffer, 0, 0x80);
    setCurrentDirectory(4, 0);
    playMusicSubVar2 = 0x80;
}

void playMusicSub(void) {
    if (playMusicSubVar0 == 0) {
        playWorldmapMusic = 1;
        unloadWds(fieldWdsSlot1);
        playMusicSubVar0 = 1;
    }
    playMusicSubVar2 = 0;
}

void computeDistanceVolume(s32 value, s32* out, s32 scale) {
    s32 max = op9DVar1.m28_soundMaxDistance;
    u32 t;

    if (max < value) {
        value = max;
    }
    t = (0x80 - ((R3000_DIV(0x7F0000, max) * value) >> 16)) << 16;
    *out = (t / 127 * scale) >> 16;
}

/* Update the volume and pan of the sounds attached to an actor. */
void updateActorSoundVolumeAndPan(s32 arg0, s32 scale, s32 arg2, s32 distance, s32 actorId) {
    s32 volume;
    s32 x;
    s32 y;
    s32 i;
    s32 pan;

    for (i = 0; i < 3; i++) {
        if (actorSoundSlots[i].m0_actorId == actorId) {
            computeDistanceVolume(distance, &volume, scale);
            getActorScreenPosition(actorId, &x, &y);
            if (x > 0x140) {
                x = 0x13F;
            }
            if (x < 0) {
                x = 0;
            }
            pan = (x * 0x6666) >> 16;
            setSoundEffectChannelVolume(i * 2, volume);
            setSoundEffectChannelPan(i * 2, pan);
        }
    }
}

/* Screen position of an actor (projection matrix * actor matrix). */
void getActorScreenPosition(s32 actorId, s32* outX, s32* outY) {
    SVECTOR v;
    MATRIX m;
    s32 sxy;
    s32 p;
    s32 flag;
    sFieldEntity* e;

    readCharacter(1);
    e = &fieldSceneState.m290_actorArray[actorId];
    CompMatrix(&fieldSceneState.m1E4_projectionMatrix, &e->mC_matrix, &m);
    v.vx = 0;
    v.vy = 0;
    v.vz = 0;
    SetRotMatrix(&m);
    SetTransMatrix(&m);
    RotTransPers(&v, &sxy, &p, &flag);
    *outY = ((s16*)&sxy)[1];
    *outX = ((s16*)&sxy)[0];
}

/* Play a sound attached to an actor in the first free slot. */
void playActorAttachedSound(s32 soundId, s32 scale, s32 arg2, s32 distance, s32 actorId) {
    s32 volume;
    s32 x;
    s32 y;
    s32 i;
    s32 pan;

    for (i = 0; i < 3; i++) {
        if (actorSoundSlots[i].m2_soundId == 0xFFFF) {
            actorSoundSlots[i].m2_soundId = soundId;
            actorSoundSlots[i].m0_actorId = actorId;
            computeDistanceVolume(distance, &volume, scale);
            getActorScreenPosition(actorId, &x, &y);
            if (x > 0x140) {
                x = 0x13F;
            }
            if (x < 0) {
                x = 0;
            }
            pan = (x * 0x6666) >> 16;
            StopSoundEffect(i * 2);
            PlaySoundEffect2(soundId, i * 2, volume, pan);
            return;
        }
    }
}

/* Noah: OPX_13Sub. Stop the sound attached to an actor. */
void OPX_13Sub(s32 actorId) {
    s32 i;
    for (i = 0; i < 3; i++) {
        if (actorSoundSlots[i].m0_actorId == actorId) {
            StopSoundEffect(i * 2);
            actorSoundSlots[i].m2_soundId = 0xFFFF;
            actorSoundSlots[i].m0_actorId = 0xFFFF;
            return;
        }
    }
}

s32 findActorSoundSlot(s32 a0, s32 actorId) {
    s32 i = 0;
    if (a0 != -1) {
        for (i = 0; i < 3; i++) {
            if (actorSoundSlots[i].m0_actorId == actorId) {
                return i;
            }
        }
    }
    return -1;
}

void resetActorSoundSlots(void) {
    s32 i;
    for (i = 0; i < 3; i++) {
        actorSoundSlots[i].m0_actorId = 0xFFFF;
        actorSoundSlots[i].m2_soundId = 0xFFFF;
    }
}

void stopFieldSoundEffects(void) {
    s32 i;
    for (i = 0; i < 3; i++) {
        actorSoundSlots[i].m2_soundId = 0xFFFF;
        actorSoundSlots[i].m0_actorId = 0xFFFF;
    }
    for (i = 0; i < 4; i++) {
        if (!(op9DVar1.m1B8_keptSoundChannels & 1)) {
            StopSoundEffect(i * 2);
        }
        op9DVar1.m1B8_keptSoundChannels >>= 1;
    }
}

/* Noah: updateCameraAt (positional sounds of the actors nearest to the camera) */
void updateCameraAt(VECTOR* at) {
    sUpdateCameraAtStruct s;
    sFieldScriptEntity* pScript;
    s32 i;
    s32 dist;
    s32 slot;
    s32 j;
    SVECTOR* pDelta;

    for (i = 0; i < 3; i++) {
        s.m0_distance[i] = 0xFFFF;
        s.m10_soundId[i] = -1;
        s.m40_alreadyPlaying[i] = 0;
        s.m30_slotInUse[i] = 0;
        s.m50_actorId[i] = 0;
        s.m20_scale[i] = 0;
    }
    for (i = 0; i < g_totalActors; i++) {
        pScript = D_800AFB10[i].m4C_scriptEntity;
        if (pScript->m10D_soundMode != 0xFF) {
            dist = distance3d(FP_INT(at->vx) - FP_INT(pScript->m20_position.vx),
                                 FP_INT(at->vy) - FP_INT(pScript->m20_position.vy),
                                 FP_INT(at->vz) - FP_INT(pScript->m20_position.vz));
            if (s.m0_distance[0] < s.m0_distance[1]) {
                slot = (s.m0_distance[1] < s.m0_distance[2]) ? 2 : 1;
            } else {
                slot = (s.m0_distance[0] < s.m0_distance[2]) << 1;
            }
            if (dist < s.m0_distance[slot]) {
                s.m50_actorId[slot] = i;
                s.m0_distance[slot] = dist;
                s.m10_soundId[slot] = fieldSceneState.m290_actorArray[i].m4C_scriptEntity->m10A_soundId;
                s.m20_scale[slot] = fieldSceneState.m290_actorArray[i].m4C_scriptEntity->m10C_soundVolume;
                (s.m60_delta + slot)->vx = (at->vx >> 16) - (fieldSceneState.m290_actorArray[i].m4C_scriptEntity->m20_position.vx >> 16);
                (s.m60_delta + slot)->vy = (at->vy >> 16) - (fieldSceneState.m290_actorArray[i].m4C_scriptEntity->m20_position.vy >> 16);
                (s.m60_delta + slot)->vz = (at->vz >> 16) - (fieldSceneState.m290_actorArray[i].m4C_scriptEntity->m20_position.vz >> 16);
            }
        } else {
            pScript->m10D_soundMode = 0xFF;
        }
    }
    for (i = 0; i < 3; i++) {
        j = findActorSoundSlot(s.m10_soundId[i], s.m50_actorId[i]);
        if (j != -1) {
            s.m30_slotInUse[j] = 1;
            s.m40_alreadyPlaying[i] = 1;
        }
    }
    for (i = 0; i < 3; i++) {
        if (s.m30_slotInUse[i] == 0 && actorSoundSlots[i].m2_soundId != 0xFFFF) {
            StopSoundEffect(i * 2);
            actorSoundSlots[i].m2_soundId = 0xFFFF;
            actorSoundSlots[i].m0_actorId = 0xFFFF;
        }
    }
    for (i = 0, pDelta = s.m60_delta; i < 3; i++) {
        if (s.m10_soundId[i] != -1) {
            if (s.m40_alreadyPlaying[i] == 1) {
                updateActorSoundVolumeAndPan(s.m10_soundId[i], s.m20_scale[i], pDelta->vx, s.m0_distance[i], s.m50_actorId[i]);
            } else {
                playActorAttachedSound(s.m10_soundId[i], s.m20_scale[i], pDelta->vx, s.m0_distance[i], s.m50_actorId[i]);
            }
        }
        pDelta++;
    }
}

void updateCamera(void) {
    switch (cameraTrackingMode) {
    case 0:
        updateCameraAt(&D_800AFB10[g_playerControlledActor].m4C_scriptEntity->m20_position);
        break;
    case 1:
        updateCameraAt(&fieldSceneState.m0_cameraEye);
        break;
    case 2:
        updateCameraAt(&fieldSceneState.m10_cameraAt);
        break;
    }
}

void OP_EXTENDED_OPCODE(void) {
    fieldScriptOpcodes_EX[pCurrentFieldScriptFile[++pCurrentFieldScriptActor->mCC_scriptPC]]();
}

void computeMechaPointLightColor(s32 slot, VECTOR* position) {
    s32 dx, dy, dz;
    s32 dist;

    dx = R3000_DIV((op9DVar1.m164_lightNearColors[slot].vx - op9DVar1.m17C_lightFarColors[slot].vx) << 16, op9DVar1.m194_lightRadii[slot]);
    dy = R3000_DIV((op9DVar1.m164_lightNearColors[slot].vy - op9DVar1.m17C_lightFarColors[slot].vy) << 16, op9DVar1.m194_lightRadii[slot]);
    dz = R3000_DIV((op9DVar1.m164_lightNearColors[slot].vz - op9DVar1.m17C_lightFarColors[slot].vz) << 16, op9DVar1.m194_lightRadii[slot]);
    dist = op9DVar1.m194_lightRadii[slot] - distance3d(op9DVar1.m1A0_lightPositions[slot].vx - FP_INT(position->vx),
                                                 op9DVar1.m1A0_lightPositions[slot].vy - FP_INT(position->vy),
                                                 op9DVar1.m1A0_lightPositions[slot].vz - FP_INT(position->vz));
    op9DVar1.m98_mechaFieldArgs2[1].m[0][slot] = (dx * dist) >> 16;
    op9DVar1.m98_mechaFieldArgs2[1].m[1][slot] = (dy * dist) >> 16;
    op9DVar1.m98_mechaFieldArgs2[1].m[2][slot] = (dz * dist) >> 16;
}

void updateMechasPosForField(void) {
    s32 i;
    for (i = 0; i < 3; i++) {
        if (op9DVar1.m15E_lightActorIds[i] != -1) {
            computeMechaPointLightColor(i, &D_800AFB10[op9DVar1.m15E_lightActorIds[i]].m4C_scriptEntity->m20_position);
        }
    }
}

void OP_73(void) {
    s32 value;
    switch (pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 1]) {
    case 0:
        pCurrentFieldScriptActor->mCC_scriptPC += 2;
        break;
    case 1:
        getImmediateOrVariableUnsigned(2);
        value = getImmediateOrVariableUnsigned(4);
        getImmediateOrVariableUnsigned(6);
        resetParticleCreationSetup(currentFieldActorId);
        particleCreationSetup.m0_configs[0].m2A_flags = 0x14;
        particleCreationSetup.m0_configs[0].m0 = 1;
        particleCreationSetup.m0_configs[0].m6_maxParticles = 0x10;
        particleCreationSetup.m0_configs[0].m72_mechaIndex = 0;
        particleCreationSetup.m0_configs[0].m74_boneIndex = value;
        if (value == 0x27) {
            particleCreationSetup.m0_configs[0].m74_boneIndex = 0x20;
        } else {
            particleCreationSetup.m0_configs[0].m74_boneIndex = 0x22;
        }
        particleCreationSetup.m0_configs[0].m4_eWait = 0x1000;
        createParticleEffect(currentFieldActorId);
        pCurrentFieldScriptActor->mCC_scriptPC += 8;
        break;
    }
}

#if !VERSION_JP

/* US only: JP's extended opcode table ends at 0xE1 */
void OPX_E2(void) {
    softReboot();
    g_breakCurrentScript = 1;
    pCurrentFieldScriptActor->mCC_scriptPC++;
}

#endif
