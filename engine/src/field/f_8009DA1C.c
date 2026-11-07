
#include "common.h"
#include "pc_retail_heap.h"
#include "psyq.h"
#include "main/memory.h"
#include "main/filesystem.h"
#include "main/game_state.h"
#include "main/sprite.h"
#include "field/field.h"

#define FP_INT(x) (((s16*)&(x))[1])

extern s32 g_playerControlledActor;
extern s16 numMaxMechaOverlayEntries[];               /* Noah: numMaxMechaOverlayEntries (array-typed: the store may alias the PC) */
extern s32 fieldMovieStopRequest;
extern s32 onlyUpdateDirector;
s32 getCurrentDiscNumber(void);               /* Noah: getCurrentDiscNumber (main) */
extern u8* fieldActorSetupParams;                 /* Noah: fieldActorSetupParams (= fieldSceneState.m29C) */
void resetFieldScriptEntityGraphicEntity(s32 actorId, s32 vramSlot, void* setup, s32 clutBank, s32 clutX, s32 m126, s32 m134_4);
void OP_INIT_ENTITY_SCRIPT_sub1(void);
void OP_INIT_ENTITY_SCRIPT(void);
void freeMecha(s32 mecha);          /* mecha overlay */
extern FieldMechaState mechaList;     /* Noah: mechaList, mechaList2, numMechas */
void copyMatrixTranslation(MATRIX* dst, MATRIX* src); /* copy the translation */
void copyMatrixRotation(MATRIX* dst, MATRIX* src); /* copy the rotation */
s32 getCharacter(s32 index);
extern void* partyCharacterBuffers[3];            /* Noah: partyCharacterBuffers (main) */
extern s16 OPX_21_Sub0;
extern s32 currentScriptFinished;
extern u8 characterMappingTable[];
s32 setupActorToFieldEntryID(s32 entry);
extern s32 opA0_var1;                 /* Noah: opA0_var1 (= fieldSceneState.m170) */
extern s32 opA0_var0;                 /* Noah: opA0_var0 (= fieldSceneState.m18C) */
extern s32 loadCompleted;
s32 isLoadCompleted(void);
extern LoadingBatchCommand mechaOverlayBatchLoadingTable[16];
extern void* mechaDataTable2[4]; /* one per mecha (FieldMechaState.m0_mechaList) */
extern void* mechaDataTable1[4];
extern s16 fieldRandomBattleVar;
void checkForRandomEncounter(void);
extern s32 playerCanRun;
extern s16 updateCharacterVar1;
extern s32 g_menuIdToOpen;
extern s16 updateEntityEventCode4Var0;
extern u16 padButtonForDialogs;
s32 OP_UPDATE_CHARACTER_Sub0(sFieldScriptEntity* entity);
extern s32 numFollowStruct2;
extern s16 updateCharacterVar2;                 /* Noah: updateCharacterVar2 (= op9DVar1.m1BE_jumpCooldown) */
extern u8 updateCharacterVar4;
extern u8 updateCharacterVar0;
extern u16 updateCharacterRotationOrder1[16];
extern u16 updateCharacterRotationOrder2[16];
extern s16 camera2Tan;                 /* Noah: camera2Tan (= fieldSceneState.m10C) */
extern s32 g_breakCurrentScript;
extern DialogWindow gDialogWindows[4];
s32 findDialogWindowForCurrentActor(s32* windowIndex);
void spriteActorSetTimeScale(SpriteActorCore* actor, s32 timeScale);
void setCurrentActor2DPosition(s32 posX, s32 posZ);
void setCurrentActorElevation();
extern FieldBss_800AF880 fieldSceneState;   /* actorArray at +0x290, numWalkMesh at +0x2D4 */
u32 getWalkmeshTriangleFlag(sFieldScriptEntity* entity);
u16 getScriptEntryPoint(s32 entityId, s32 scriptIndex);
void OP_UPDATE_CHARACTER(void);
s32 getVariable(s32 index);
extern s32 fieldMapNumber;                 /* Noah: fieldMapNumber (main) */
extern FieldBss_800B2184 op9DVar1;   /* mE4 (0x800B2268): Noah pcInitVar1 */
extern s32 partyGearActorIds[3];
extern GameState* pKernelGameState;
void waitCdIdle(void);
void updatePartyFileSet(void);
void processLoadedPartyFiles(void);
void updateGearState(s32 slot);
void setVar(s32 index, s32 value);
void enterGear(s32 slot);
void exitGear(s32 slot);
s32 findPartySlotOfGearActor(s32 actor);

void DISABLE_DIALOG_ACTIVATION(void) {
    pCurrentFieldScriptActor->m0_fieldScriptFlags |= 0x20000;
    pCurrentFieldScriptActor->mCC_scriptPC++;
}

void OP_2B(void) {
    pCurrentFieldScriptActor->m0_fieldScriptFlags &= ~0x20000;
    pCurrentFieldScriptActor->mCC_scriptPC++;
}

void OP_CD(void) {
    pCurrentFieldScriptActor->m0_fieldScriptFlags |= 0x800000;
    pCurrentFieldScriptActor->mCC_scriptPC++;
}

void OP_CE(void) {
    pCurrentFieldScriptActor->m0_fieldScriptFlags &= ~0x800000;
    pCurrentFieldScriptActor->mCC_scriptPC++;
}

void OP_DELETE_ENTITY(void) {
    sFieldScriptEntity* entity;
    sFieldEntity* actor;
    s32 window;

    if (readCharacter(1) != 0xFF) {
        entity = fieldSceneState.m290_actorArray[readCharacter(1)].m4C_scriptEntity;
        entity->m0_fieldScriptFlags |= 1;
        entity->m4_flags |= 0x100000;
        actor = &fieldSceneState.m290_actorArray[readCharacter(1)];
        actor->m58_flags |= 0x20;
        if (findDialogWindowForCurrentActor(&window) == 0) {
            gDialogWindows[window].m414_keepOpen = 0;
        }
    }
    pCurrentFieldScriptActor->mCC_scriptPC += 2;
}

void OP_ENABLE_VM_FOR_CHARACTER(void) {
    if (readCharacter(1) != 0xFF) {
        fieldSceneState.m290_actorArray[readCharacter(1)].m4C_scriptEntity->m0_fieldScriptFlags &= ~1;
    }
    pCurrentFieldScriptActor->mCC_scriptPC += 2;
}

void OP_RESET_CHARACTER(void) {
    sFieldScriptEntity* entity;
    s32 window;

    if (readCharacter(1) != 0xFF) {
        entity = fieldSceneState.m290_actorArray[readCharacter(1)].m4C_scriptEntity;
        entity->m30_stepVector.vx = 0;
        entity->m30_stepVector.vy = 0;
        entity->m30_stepVector.vz = 0;
        entity->m40_externalStep.vx = 0;
        entity->m40_externalStep.vy = 0;
        entity->m40_externalStep.vz = 0;
        entity->m0_fieldScriptFlags |= 1;
        entity->m104_rotation = entity->m106_currentRotation = entity->m104_rotation | 0x8000;
        if (findDialogWindowForCurrentActor(&window) == 0) {
            gDialogWindows[window].m414_keepOpen = 0;
        }
    }
    pCurrentFieldScriptActor->mCC_scriptPC += 2;
}

void OP_WAIT(void) {
    if (pCurrentFieldScriptActor->m8C_scriptSlots[pCurrentFieldScriptActor->mCE_currentScriptSlot].m2_delay == 0) {
        pCurrentFieldScriptActor->m8C_scriptSlots[pCurrentFieldScriptActor->mCE_currentScriptSlot].m2_delay = getImmediateOrVariableUnsigned(1);
    } else {
        pCurrentFieldScriptActor->m8C_scriptSlots[pCurrentFieldScriptActor->mCE_currentScriptSlot].m2_delay--;
    }
    if (pCurrentFieldScriptActor->m8C_scriptSlots[pCurrentFieldScriptActor->mCE_currentScriptSlot].m2_delay == 0) {
        pCurrentFieldScriptActor->mCC_scriptPC += 3;
    }
    g_breakCurrentScript = 1;
}

void OP_MAKE_VISIBLE_BY_ID(void) {
    sFieldEntity* actor;

    if (readCharacter(1) != 0xFF) {
        actor = &fieldSceneState.m290_actorArray[readCharacter(1)];
        if (!(actor->m4C_scriptEntity->m4_flags & 0x100000)) {
            actor->m58_flags &= ~0x20;
            actor->m4C_scriptEntity->m4_flags &= ~0x2000000;
        }
    }
    pCurrentFieldScriptActor->mCC_scriptPC += 2;
}

void OP_MAKE_INVISIBLE_BY_ID(void) {
    sFieldEntity* actor;

    if (readCharacter(1) != 0xFF) {
        actor = &fieldSceneState.m290_actorArray[readCharacter(1)];
        actor->m58_flags |= 0x20;
    }
    pCurrentFieldScriptActor->mCC_scriptPC += 2;
}

void OP_MAKE_VISIBLE_ACTIVE(void) {
    sFieldScriptEntity* entity;
    sFieldEntity* actor = &fieldSceneState.m290_actorArray[currentFieldActorId];
    actor->m58_flags &= ~0x20;
    entity = pCurrentFieldScriptActor;
    entity->mE8_currentAnimationId = 0xFF;
    entity->m4_flags &= ~0x2000000;
    entity->mCC_scriptPC++;
}

void OPX_C4(void) {
    sFieldEntity* actor;

    if (readCharacter(1) != 0xFF) {
        actor = &fieldSceneState.m290_actorArray[readCharacter(1)];
        actor->m4C_scriptEntity->m4_flags |= 0x2000000;
        actor->m4C_scriptEntity->m4_flags |= 0x800;
    }
    pCurrentFieldScriptActor->mCC_scriptPC += 2;
}

void OPX_C3(void) {
    pCurrentFieldScriptActor->m4_flags |= 0x2000800;
    pCurrentFieldScriptActor->mCC_scriptPC++;
}

void OP_MAKE_INVISIBLE_ACTIVE(void) {
    sFieldEntity* actor = &fieldSceneState.m290_actorArray[currentFieldActorId];
    actor->m58_flags |= 0x20;
    pCurrentFieldScriptActor->mCC_scriptPC += 1;
}

void OP_SET_TIME_SCALE(void) {
    s32 scale = getImmediateOrVariableUnsigned(1);
    pCurrentFieldScriptActor->m76_timeScale = scale;
    spriteActorSetTimeScale(fieldSceneState.m290_actorArray[currentFieldActorId].m4_pVramSpriteSheet,
                            pcFieldTimeScale((u16)scale)); /* Enhanced: at the field's logic rate */
    pCurrentFieldScriptActor->mCC_scriptPC += 3;
}

void OP_SET_CURRENT_ACTOR_FLAGS(void) {
    u32 value = getImmediateOrVariableUnsigned(1);
    u32 flags = (value & 1) << 7;
    if (value & 4) {
        flags |= 0x20;
    }
    if (value & 8) {
        flags |= 0x10;
    }
    if (value & 0x10) {
        flags |= 8;
    }
    if (value & 0x20) {
        flags |= 4;
    }
    if (value & 0x40) {
        flags |= 0x8000000;
    }
    pCurrentFieldScriptActor->m0_fieldScriptFlags = (pCurrentFieldScriptActor->m0_fieldScriptFlags & ~0x80000BC) | flags;
    pCurrentFieldScriptActor->mCC_scriptPC += 3;
}

/* Noah OP_1F: m4_flags bits 0-2 = byte & 7, bits 3-5 = byte >> 4 (one value temp for both) */
void OP_1F(void) {
    s32 v;

    v = pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 1] & 7;
    pCurrentFieldScriptActor->m4_flags = (pCurrentFieldScriptActor->m4_flags & ~7) | v;
    v = ((pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 1] >> 4) & 7) << 3;
    pCurrentFieldScriptActor->m4_flags = (pCurrentFieldScriptActor->m4_flags & ~0x38) | v;
    pCurrentFieldScriptActor->mCC_scriptPC += 2;
}

void OP_UPDATE_ELEVATION(void) {
    pCurrentFieldScriptActor->mEC_elevation = 0;
    pCurrentFieldScriptActor->m0_fieldScriptFlags = (pCurrentFieldScriptActor->m0_fieldScriptFlags & ~0x40000) | 0x400000;
    pCurrentFieldScriptActor->mCC_scriptPC++;
    pCurrentFieldScriptActor->m72_elevation = pCurrentFieldScriptActor->m20_position.vy >> 16;
}

void OP_SET_CURRENT_ACTOR_POSITION_3D(void) {
    setCurrentActor2DPosition(readS16FromScript(1), readS16FromScript(3));
    setCurrentActorElevation(readS16FromScript(5));
    pCurrentFieldScriptActor->m0_fieldScriptFlags |= 0x40000;
    pCurrentFieldScriptActor->mCC_scriptPC += 7;
}

void OP_SET_CURRENT_ACTOR_ELEVATION(void) {
    setCurrentActorElevation(getVar80(1, pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 3]));
    pCurrentFieldScriptActor->m0_fieldScriptFlags |= 0x40000;
    pCurrentFieldScriptActor->mCC_scriptPC += 4;
}

s16 readS16FromScriptFile(s32 offset) {
    return (s16)(pCurrentFieldScriptFile[offset] + (pCurrentFieldScriptFile + offset)[1] * 0x100);
}

void OP_1B(void) {
    s32 x;
    pCurrentFieldScriptActor->m10_walkmeshId = pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 5];
    x = getVar80(1, pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 6]);
    setCurrentActor2DPosition(x, getVar40(3, pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 6]));
    pCurrentFieldScriptActor->m4_flags &= ~0x200000;
    pCurrentFieldScriptActor->m0_fieldScriptFlags &= ~0x10000;
    pCurrentFieldScriptActor->mCC_scriptPC += 7;
}

void OP_SET_ENTITY_WALKMESH_LAYER(void) {
    pCurrentFieldScriptActor->m10_walkmeshId = pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 1];
    setCurrentActor2DPosition(fieldSceneState.m290_actorArray[currentFieldActorId].m4C_scriptEntity->m20_position.vx >> 16, fieldSceneState.m290_actorArray[currentFieldActorId].m4C_scriptEntity->m20_position.vz >> 16);
    pCurrentFieldScriptActor->mCC_scriptPC += 2;
}

void OP_SET_ACTOR_POSITION_2D(void) {
    s32 x = getVar80(1, pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 5]);
    setCurrentActor2DPosition(x, getVar40(3, pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 5]));
    pCurrentFieldScriptActor->m4_flags &= ~0x200000;
    pCurrentFieldScriptActor->m0_fieldScriptFlags &= ~0x10000;
    pCurrentFieldScriptActor->mCC_scriptPC += 6;
}

void setCurrentActor2DPosition(s32 posX, s32 posZ) {
    VECTOR normals[4];
    SVECTOR positions[4];
    SpriteActorCore* sprite = fieldSceneState.m290_actorArray[currentFieldActorId].m4_pVramSpriteSheet;
    sFieldScriptEntity* entity;
    s32 i;

    /* An actor on the last walkmesh layer (field 418's actor 4: layer 3 of
     * 4) reads a position and a normal the loop below never fills: stack
     * garbage on the PS1 too. Redux has a zero position there (height 0) and
     * an arbitrary normal; the PC takes zeros for both. */
    for (i = 0; i < 4; i++) {
        positions[i].vx = positions[i].vy = positions[i].vz = 0;
        normals[i].vx = normals[i].vy = normals[i].vz = 0;
    }
    for (i = 0; i < fieldSceneState.m2D4_numWalkMesh - 1; i++) {
        pCurrentFieldScriptActor->m8_currentWalkMeshTriangle[i] = findTriangleInWalkMesh(posX, posZ, i, &positions[i], &normals[i]);
    }
    pCurrentFieldScriptActor->m14_currentTriangleFlag = getWalkmeshTriangleFlag(pCurrentFieldScriptActor);
    pCurrentFieldScriptActor->m50_surfaceNormal.vx = (normals + pCurrentFieldScriptActor->m10_walkmeshId)->vx;
    pCurrentFieldScriptActor->m50_surfaceNormal.vy = (normals + pCurrentFieldScriptActor->m10_walkmeshId)->vy;
    pCurrentFieldScriptActor->m50_surfaceNormal.vz = (normals + pCurrentFieldScriptActor->m10_walkmeshId)->vz;
    fieldSceneState.m290_actorArray[currentFieldActorId].m2C_matrixBackup.t[0] = fieldSceneState.m290_actorArray[currentFieldActorId].mC_matrix.t[0] = posX;
    fieldSceneState.m290_actorArray[currentFieldActorId].m2C_matrixBackup.t[1] = fieldSceneState.m290_actorArray[currentFieldActorId].mC_matrix.t[1] = positions[pCurrentFieldScriptActor->m10_walkmeshId].vy;
    fieldSceneState.m290_actorArray[currentFieldActorId].m2C_matrixBackup.t[2] = fieldSceneState.m290_actorArray[currentFieldActorId].mC_matrix.t[2] = posZ;
    sprite->m84_maxY = positions[pCurrentFieldScriptActor->m10_walkmeshId].vy;
    pCurrentFieldScriptActor->m20_position.vx = posX << 16;
    pCurrentFieldScriptActor->m20_position.vy = positions[pCurrentFieldScriptActor->m10_walkmeshId].vy << 16;
    pCurrentFieldScriptActor->m20_position.vz = posZ << 16;
    pCurrentFieldScriptActor->m72_elevation = positions[pCurrentFieldScriptActor->m10_walkmeshId].vy;
    sprite->m0_position.vx = pCurrentFieldScriptActor->m20_position.vx;
    sprite->m0_position.vy = pCurrentFieldScriptActor->m20_position.vy;
    sprite->m0_position.vz = pCurrentFieldScriptActor->m20_position.vz;
    pCurrentFieldScriptActor->m40_externalStep.vx = 0;
    pCurrentFieldScriptActor->m40_externalStep.vy = 0;
    pCurrentFieldScriptActor->m40_externalStep.vz = 0;
    pCurrentFieldScriptActor->m30_stepVector.vx = 0;
    pCurrentFieldScriptActor->m30_stepVector.vy = 0;
    pCurrentFieldScriptActor->m30_stepVector.vz = 0;
    pCurrentFieldScriptActor->mD0_targetPositionOffset.vx = 0;
    pCurrentFieldScriptActor->mD0_targetPositionOffset.vy = 0;
    pCurrentFieldScriptActor->mD0_targetPositionOffset.vz = 0;
    pCurrentFieldScriptActor->m60_interactionOffset.vy = 0;
    pCurrentFieldScriptActor->m60_interactionOffset.vx = 0;
    pCurrentFieldScriptActor->m60_interactionOffset.vz = 0;
    sprite->mC_step.vx = 0;
    sprite->mC_step.vy = 0;
    sprite->mC_step.vz = 0;
    pCurrentFieldScriptActor->mF0_fallSpeed = 0;
    pCurrentFieldScriptActor->mEC_elevation = 0;
    pCurrentFieldScriptActor->m72_elevation = pCurrentFieldScriptActor->m20_position.vy >> 16;
    pCurrentFieldScriptActor->m0_fieldScriptFlags = (pCurrentFieldScriptActor->m0_fieldScriptFlags & ~0x40000) | 0x400000;
}

void setCurrentActorElevation(elevation)
    s16 elevation;
{
    s32 unused[6];
    sFieldScriptEntity* entity = pCurrentFieldScriptActor;
    entity->m20_position.vy = elevation << 16;
    entity->mEC_elevation = elevation;
    entity->m72_elevation = elevation;
}

void OP_SET_BOUNDING_VOLUME(void) {
    if (pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 1] != 0) {
        pCurrentFieldScriptActor->m18_boundingVolume[0] = pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 1] << 1;
    }
    if (pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 2] != 0) {
        pCurrentFieldScriptActor->m18_boundingVolume[2] = pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 2] << 1;
    }
    if (pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 3] != 0) {
        pCurrentFieldScriptActor->m18_boundingVolume[1] = pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 3] << 1;
    }
    if (pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 4] != 0) {
        pCurrentFieldScriptActor->m1E_collisionRadius = pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 4] << 1;
    }
    pCurrentFieldScriptActor->mCC_scriptPC += 5;
}

void OP_SETUP_MOVEMENT_BOUNDING_ZONE(void) {
    if (!pCurrentFieldScriptActor->m12C_xC) {
        pCurrentFieldScriptActor->m114_movementBoundingZone = allocateHeap(PC_HEAP_BYTES(0x10), 0);
    }
    pCurrentFieldScriptActor->m12C_xC = 1;
    pCurrentFieldScriptActor->m114_movementBoundingZone[0].vx = getVar80(1, pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 0x11]);
    pCurrentFieldScriptActor->m114_movementBoundingZone[0].vy = getVar40(3, pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 0x11]);
    pCurrentFieldScriptActor->m114_movementBoundingZone[1].vx = getVar20(5, pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 0x11]);
    pCurrentFieldScriptActor->m114_movementBoundingZone[1].vy = getVar10(7, pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 0x11]);
    pCurrentFieldScriptActor->m114_movementBoundingZone[2].vx = getVar08(9, pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 0x11]);
    pCurrentFieldScriptActor->m114_movementBoundingZone[2].vy = getVar04(0xB, pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 0x11]);
    pCurrentFieldScriptActor->m114_movementBoundingZone[3].vx = getVar02(0xD, pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 0x11]);
    pCurrentFieldScriptActor->m114_movementBoundingZone[3].vy = getVar01(0xF, pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 0x11]);
    pCurrentFieldScriptActor->mCC_scriptPC += 0x12;
}

s32 isScriptAlreadyRunning(sFieldScriptEntity* entity, s32 scriptIndex) {
    s32 i;
    for (i = 0; i < 8; i++) {
        if (scriptIndex == entity->m8C_scriptSlots[i].m3_scriptIndex) {
            return -1;
        }
    }
    return 0;
}

void OP_RUN_ENTITY_SCRIPT_ASYNC(void) {
    s32 entityId;
    sFieldScriptEntity* entity;
    s32 i;

    if (readCharacter(1) != 0xFF) {
        entityId = readCharacter(1);
        entity = fieldSceneState.m290_actorArray[entityId].m4C_scriptEntity;
        if (entity->m4_flags & 0x100000) {
            pCurrentFieldScriptActor->m8C_scriptSlots[pCurrentFieldScriptActor->mCE_currentScriptSlot].m4_x10_status = 0;
            entity->m8C_scriptSlots[pCurrentFieldScriptActor->mCF_scriptSlotWaitedOn].m4_x16 = 0;
        } else if (isScriptAlreadyRunning(entity, pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 2] & 0x1F) != -1) {
            for (i = 0; i < 8; i++) {
                if (entity->m8C_scriptSlots[i].m4_x12_priority == 0xF && !entity->m8C_scriptSlots[i].m4_x16) {
                    goto found;
                }
            }
            return;
        found:
            entity->m8C_scriptSlots[i].m0_scriptPC = getScriptEntryPoint(entityId, pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 2] & 0x1F);
            entity->m8C_scriptSlots[i].m4_x12_priority = pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 2] >> 5;
            entity->m8C_scriptSlots[i].m3_scriptIndex = pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 2] & 0x1F;
        }
    }
    pCurrentFieldScriptActor->mCC_scriptPC += 3;
}

void OP_RUN_ENTITY_SCRIPT_UNKMODE(void) {
    s32 entityId;
    sFieldScriptEntity* entity;
    s32 i;

    if (readCharacter(1) != 0xFF) {
        entityId = readCharacter(1);
        entity = fieldSceneState.m290_actorArray[entityId].m4C_scriptEntity;
        if (entity->m4_flags & 0x100000) {
            pCurrentFieldScriptActor->m8C_scriptSlots[pCurrentFieldScriptActor->mCE_currentScriptSlot].m4_x10_status = 0;
            entity->m8C_scriptSlots[pCurrentFieldScriptActor->mCF_scriptSlotWaitedOn].m4_x16 = 0;
        } else {
            switch (pCurrentFieldScriptActor->m8C_scriptSlots[pCurrentFieldScriptActor->mCE_currentScriptSlot].m4_x10_status) {
            case 0:
                if (isScriptAlreadyRunning(entity, pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 2] & 0x1F) == -1) {
                    break;
                }
                for (i = 0; i < 8; i++) {
                    if (entity->m8C_scriptSlots[i].m4_x12_priority == 0xF && !entity->m8C_scriptSlots[i].m4_x16) {
                        goto found;
                    }
                }
                return;
            case 1:
                if (entity->mCE_currentScriptSlot == pCurrentFieldScriptActor->mCF_scriptSlotWaitedOn ||
                    entity->m8C_scriptSlots[pCurrentFieldScriptActor->mCF_scriptSlotWaitedOn].m4_x12_priority == 0xF) {
                    pCurrentFieldScriptActor->mCC_scriptPC += 3;
                    pCurrentFieldScriptActor->m8C_scriptSlots[pCurrentFieldScriptActor->mCE_currentScriptSlot].m4_x10_status = 0;
                    entity->m8C_scriptSlots[pCurrentFieldScriptActor->mCF_scriptSlotWaitedOn].m4_x16 = 0;
                    return;
                }
                g_breakCurrentScript = 1;
                return;
            found:
                entity->m8C_scriptSlots[i].m0_scriptPC = getScriptEntryPoint(entityId, pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 2] & 0x1F);
                entity->m8C_scriptSlots[i].m4_x12_priority = pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 2] >> 5;
                entity->m8C_scriptSlots[pCurrentFieldScriptActor->mCF_scriptSlotWaitedOn].m4_x16 = 1;
                entity->m8C_scriptSlots[i].m3_scriptIndex = pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 2] & 0x1F;
                pCurrentFieldScriptActor->mCF_scriptSlotWaitedOn = i;
                pCurrentFieldScriptActor->m8C_scriptSlots[pCurrentFieldScriptActor->mCE_currentScriptSlot].m4_x10_status = 1;
                return;
            default:
                return;
            }
        }
    }
    pCurrentFieldScriptActor->mCC_scriptPC += 3;
}

void OP_RUN_ENTITY_SCRIPT_BLOCKING(void) {
    s32 entityId;
    sFieldScriptEntity* entity;
    s32 i;

    if (readCharacter(1) != 0xFF) {
        entityId = readCharacter(1);
        entity = fieldSceneState.m290_actorArray[entityId].m4C_scriptEntity;
        if (entity->m4_flags & 0x100000) {
            pCurrentFieldScriptActor->m8C_scriptSlots[pCurrentFieldScriptActor->mCE_currentScriptSlot].m4_x10_status = 0;
            entity->m8C_scriptSlots[pCurrentFieldScriptActor->mCF_scriptSlotWaitedOn].m4_x16 = 0;
        } else {
            switch (pCurrentFieldScriptActor->m8C_scriptSlots[pCurrentFieldScriptActor->mCE_currentScriptSlot].m4_x10_status) {
            case 0:
                if (isScriptAlreadyRunning(entity, pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 2] & 0x1F) == -1) {
                    break;
                }
                for (i = 0; i < 8; i++) {
                    if (entity->m8C_scriptSlots[i].m4_x12_priority == 0xF && !entity->m8C_scriptSlots[i].m4_x16) {
                        goto found;
                    }
                }
                return;
            case 1:
                if (entity->mCE_currentScriptSlot == pCurrentFieldScriptActor->mCF_scriptSlotWaitedOn ||
                    entity->m8C_scriptSlots[pCurrentFieldScriptActor->mCF_scriptSlotWaitedOn].m4_x12_priority == 0xF) {
                    pCurrentFieldScriptActor->m8C_scriptSlots[pCurrentFieldScriptActor->mCE_currentScriptSlot].m4_x10_status = 2;
                    return;
                }
                g_breakCurrentScript = 1;
                return;
            case 2:
                if (entity->m8C_scriptSlots[pCurrentFieldScriptActor->mCF_scriptSlotWaitedOn].m4_x12_priority == 0xF) {
                    pCurrentFieldScriptActor->m8C_scriptSlots[pCurrentFieldScriptActor->mCE_currentScriptSlot].m4_x10_status = 0;
                    entity->m8C_scriptSlots[pCurrentFieldScriptActor->mCF_scriptSlotWaitedOn].m4_x16 = 0;
                    pCurrentFieldScriptActor->mCC_scriptPC += 3;
                    return;
                }
                g_breakCurrentScript = 1;
                return;
            found:
                entity->m8C_scriptSlots[i].m0_scriptPC = getScriptEntryPoint(entityId, pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 2] & 0x1F);
                entity->m8C_scriptSlots[i].m4_x12_priority = pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 2] >> 5;
                entity->m8C_scriptSlots[pCurrentFieldScriptActor->mCF_scriptSlotWaitedOn].m4_x16 = 1;
                pCurrentFieldScriptActor->mCF_scriptSlotWaitedOn = i;
                pCurrentFieldScriptActor->m8C_scriptSlots[pCurrentFieldScriptActor->mCE_currentScriptSlot].m4_x10_status = 1;
                entity->m8C_scriptSlots[i].m3_scriptIndex = pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 2] & 0x1F;
                return;
            default:
                return;
            }
        }
    }
    pCurrentFieldScriptActor->mCC_scriptPC += 3;
}

void OP_RAND_ROTATION(void) {
    s32 rotation = pCurrentFieldScriptActor->m106_currentRotation;
    pCurrentFieldScriptActor->m102_numSteps++;
    if ((pCurrentFieldScriptActor->m102_numSteps & 0xF) == 0) {
        s32 tmp;
        if ((rand() & 1) == 0) {
            tmp = pCurrentFieldScriptActor->m106_currentRotation + 0x200;
        } else {
            tmp = pCurrentFieldScriptActor->m106_currentRotation - 0x200;
        }
        rotation = tmp & 0xFFF;
    }
    g_breakCurrentScript = 1;
    pCurrentFieldScriptActor->m104_rotation = rotation;
    pCurrentFieldScriptActor->mCC_scriptPC++;
}

void OP_WALK_RANDOM_DIRECTION(void) {
    s32 rotation = pCurrentFieldScriptActor->m106_currentRotation;
    s32 r;
    pCurrentFieldScriptActor->m102_numSteps++;
    if ((pCurrentFieldScriptActor->m102_numSteps & 0xF) == 0) {
        r = rand();
        if (r & 0x30) {
            rotation = pCurrentFieldScriptActor->m106_currentRotation = pCurrentFieldScriptActor->m106_currentRotation | 0x8000;
        } else {
            s32 tmp;
            if ((r & 1) == 0) {
                tmp = pCurrentFieldScriptActor->m106_currentRotation + 0x200;
            } else {
                tmp = pCurrentFieldScriptActor->m106_currentRotation - 0x200;
            }
            rotation = tmp & 0xFFF;
        }
    }
    g_breakCurrentScript = 1;
    pCurrentFieldScriptActor->m104_rotation = rotation;
    pCurrentFieldScriptActor->mCC_scriptPC++;
}

void OP_UPDATE_CHARACTER_INFINITLY(void) {
    u16 pc = pCurrentFieldScriptActor->mCC_scriptPC;
    OP_UPDATE_CHARACTER();
    g_breakCurrentScript = 1;
    pCurrentFieldScriptActor->mCC_scriptPC = pc;
}

/* Noah OP_UPDATE_CHARACTER: player movement / jump input */
void OP_UPDATE_CHARACTER(void) {
    s32 i;
    s32 found;
    s32 rotation = 0;
    u8 unused[0x48]; /* unused; only reserves stack (retail frame is 0x60) */

    if (pCurrentFieldScriptActor->m0_fieldScriptFlags & 0x4000) {
        for (i = 0; i < 4; i++) {
            if (gDialogWindows[i].m37C_cursor.m0_state == 0) {
                break;
            }
        }
        found = 0;
        if (i == 4) {
            found = -1;
        }
        if (found == -1 && fieldRandomBattleVar == 0) {
            if ((padButtonForScripts >> 12) != 0) {
                checkForRandomEncounter();
            }
            playerCanRun = 1;
            if (pCurrentFieldScriptActor->m14_currentTriangleFlag & 0x400000) {
                if (pCurrentFieldScriptActor->m68_oldPosition[0] == FP_INT(pCurrentFieldScriptActor->m20_position.vx) &&
                    pCurrentFieldScriptActor->m68_oldPosition[1] == FP_INT(pCurrentFieldScriptActor->m20_position.vy) &&
                    pCurrentFieldScriptActor->m68_oldPosition[2] == FP_INT(pCurrentFieldScriptActor->m20_position.vz)) {
                    updateCharacterVar1++;
                }
            } else {
                updateCharacterVar1 = 0;
            }
            if (updateCharacterVar1 >= 0x21 && (updateCharacterVar1 = 0x20, (padButtonForScripts & 0x80)) &&
                !(pCurrentFieldScriptActor->m0_fieldScriptFlags & 0x1800) && g_menuIdToOpen == 0xFF) {
                goto tryJump;
            }
            /* Enhanced (xg-hbc.14, .19.3): the jump reads the new-press mask on
             * the script tick, which reads every sample of its native frame. */
            if (updateEntityEventCode4Var0 == 0) {
                if ((padButtonForDialogs & 0x80) && !(pCurrentFieldScriptActor->m0_fieldScriptFlags & 0x1800) &&
                    !(pCurrentFieldScriptActor->m14_currentTriangleFlag & 0x400000) && g_menuIdToOpen == 0xFF) {
                tryJump:
                    if (OP_UPDATE_CHARACTER_Sub0(pCurrentFieldScriptActor) == 0) {
                        pCurrentFieldScriptActor->m0_fieldScriptFlags |= 0x800;
                        numFollowStruct2 = partyToFollowStructMapping[0];
                    }
                }
            } else {
                if ((padButtonForDialogs & 0x80) && updateCharacterVar2 == 0 && g_menuIdToOpen == 0xFF &&
                    OP_UPDATE_CHARACTER_Sub0(pCurrentFieldScriptActor) == 0) {
                    pCurrentFieldScriptActor->m0_fieldScriptFlags |= 0x800;
                    numFollowStruct2 = partyToFollowStructMapping[0];
                    pCurrentFieldScriptActor->mE8_currentAnimationId = 0xFF;
                    op9DVar1.m1BE_jumpCooldown = op9DVar1.m1BC_jumpCooldownReset;
                }
                if (updateCharacterVar2 != 0) {
                    op9DVar1.m1BE_jumpCooldown--;
                }
            }
            if (updateCharacterVar4 == 0) {
                rotation = updateCharacterRotationOrder1[(padButtonForScripts >> 12) ^ 0xF];
            } else {
                rotation = updateCharacterRotationOrder2[(padButtonForScripts >> 12) ^ 0xF];
            }
            if (!(rotation & 0x8000)) {
                rotation = (rotation - camera2Tan) & 0xFFF;
            }
            pCurrentFieldScriptActor->m104_rotation = rotation;
        } else {
            pCurrentFieldScriptActor->m104_rotation = rotation | 0x8000;
        }
    } else if (updateCharacterVar0 == 0) {
        pCurrentFieldScriptActor->m0_fieldScriptFlags |= 0x1000000;
    }
    pCurrentFieldScriptActor->mCC_scriptPC++;
}

s32 findCharacterInParty(s32 character) {
    s32 i;
    if (character == 0xFF) {
        return -1;
    }
    for (i = 0; i < 3; i++) {
        if (currentParty[i] == 0xFF) {
            break;
        }
        if (currentParty[i] == character) {
            return i;
        }
    }
    return -1;
}

/* Noah setupActorToFieldEntryID: place the current actor at field entry `entry`
 * (7-byte records in the script header: x, z, walkmesh, camera and actor direction) */
s32 setupActorToFieldEntryID(s32 entry) {
    s32 offset;
    s32 rotation;

    if (pCurrentFieldScriptFile[0] != 0xFF) {
        return 0;
    }
    offset = entry * 7;
    pCurrentFieldScriptActor->m10_walkmeshId = pCurrentFieldScriptFile[offset + 5];
    setCurrentActor2DPosition(readS16FromScriptFile(offset + 1), readS16FromScriptFile(offset + 3));
    rotation = ((pCurrentFieldScriptFile[offset + 6] + 4) & 7) << 9;
    if (pCurrentFieldScriptFile[offset + 6] == 0xFF) {
        rotation = ((getVariable(8) + 4) & 7) << 9;
    }
    fieldSceneState.m164_cameraRotationBetweenEyeAndAt.vy = rotation;
    opA0_var0 = (s16)rotation;
    opA0_var1 = rotation << 16;
    rotation = (((pCurrentFieldScriptFile[offset + 7] - 2) & 7) << 9) | 0x8000;
    if (pCurrentFieldScriptFile[offset + 7] == 0xFF) {
        rotation = (((getVariable(6) - 2) & 7) << 9) | 0x8000;
    }
    pCurrentFieldScriptActor->m108_rotation3 = pCurrentFieldScriptActor->m106_currentRotation = pCurrentFieldScriptActor->m104_rotation = rotation;
    return 0;
}

void OP_SWITCH_MAP_TO_GEARS(void) {
    fieldMapNumber |= 0xC000;
    waitCdIdle();
    updatePartyFileSet();
    processLoadedPartyFiles();
    op9DVar1.mE4_partySpriteSetupBase = pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 1];
    pCurrentFieldScriptActor->mCC_scriptPC += 2;
}

s32 findPartySlotOfGearActor(s32 actor) {
    s32 i;
    for (i = 0; i < 3; i++) {
        if (partyGearActorIds[i] == actor) {
            return i;
        }
    }
    return 0xFF;
}

void OP_SET_ON_GEAR(void) {
    s32 slot = getImmediateOrVariableUnsigned(1);
    if (slot > 2) {
        slot = 2;
    }
    pKernelGameState->m22B1_isOnGear[slot] = 1;
    updateGearState(slot);
    pCurrentFieldScriptActor->mCC_scriptPC += 3;
}

void OP_SET_OFF_GEAR(void) {
    s32 slot = getImmediateOrVariableUnsigned(1);
    if (slot > 2) {
        slot = 2;
    }
    pKernelGameState->m22B1_isOnGear[slot] = 0;
    updateGearState(slot);
    pCurrentFieldScriptActor->mCC_scriptPC += 3;
}

void updateGearState(s32 slot) {
    switch (slot) {
    case 0:
        setVar(0x2A, fieldMapNumber & 0xFFF);
        setVar(0x2C, 0);
        setVar(0x2E, 0);
        break;
    case 1:
        setVar(0x30, fieldMapNumber & 0xFFF);
        setVar(0x32, 0);
        setVar(0x34, 0);
        break;
    case 2:
        setVar(0x36, fieldMapNumber & 0xFFF);
        setVar(0x38, 0);
        setVar(0x3A, 0);
        break;
    }
}

void OPX_1F(void) {
    s32 slot = findPartySlotOfGearActor(currentFieldActorId);
    if (slot != 0xFF && pKernelGameState->m22B1_isOnGear[slot] == 0) {
        enterGear(slot);
    }
    pCurrentFieldScriptActor->mCC_scriptPC += 1;
}

void OPX_20(void) {
    s32 slot = pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 1];
    if (currentParty[slot] != 0xFF && pKernelGameState->m22B1_isOnGear[slot] != 0) {
        exitGear(slot);
    }
    pCurrentFieldScriptActor->mCC_scriptPC += 2;
}

s32 savePartyMemberPositionVars(s32 slot) {
    s32 walkmesh;
    s32* map = partyFieldActorIds;

    if (partyFieldActorIds[slot] != 0xFF) {
        walkmesh = fieldSceneState.m290_actorArray[partyFieldActorIds[slot]].m4C_scriptEntity->m10_walkmeshId << 14;
        switch (slot) {
        case 0:
            setVar(0x2A, (fieldMapNumber & 0xFFF) | walkmesh);
            setVar(0x2C, fieldSceneState.m290_actorArray[map[0]].m4C_scriptEntity->m20_position.vx >> 16);
            setVar(0x2E, fieldSceneState.m290_actorArray[map[0]].m4C_scriptEntity->m20_position.vz >> 16);
            break;
        case 1:
            setVar(0x30, (fieldMapNumber & 0xFFF) | walkmesh);
            setVar(0x32, fieldSceneState.m290_actorArray[partyFieldActorIds[1]].m4C_scriptEntity->m20_position.vx >> 16);
            setVar(0x34, fieldSceneState.m290_actorArray[partyFieldActorIds[1]].m4C_scriptEntity->m20_position.vz >> 16);
            break;
        case 2:
            setVar(0x36, (fieldMapNumber & 0xFFF) | walkmesh);
            setVar(0x38, fieldSceneState.m290_actorArray[partyFieldActorIds[2]].m4C_scriptEntity->m20_position.vx >> 16);
            setVar(0x3A, fieldSceneState.m290_actorArray[partyFieldActorIds[2]].m4C_scriptEntity->m20_position.vz >> 16);
            break;
        }
    }
    FALLOFF_RETURN(0); /* result unused */
}

void OP_5CSub0(s32 slot, s32* entityParam, s32* posX, s32* posZ) {
    switch (slot) {
    case 0:
        *entityParam = getVariable(0x2A);
        *posX = getVariable(0x2C);
        *posZ = getVariable(0x2E);
        break;
    case 1:
        *entityParam = getVariable(0x30);
        *posX = getVariable(0x32);
        *posZ = getVariable(0x34);
        break;
    case 2:
        *entityParam = getVariable(0x36);
        *posX = getVariable(0x38);
        *posZ = getVariable(0x3A);
        break;
    }
}

/* Noah OP_5C: set up the current actor as party member (slot = imm/var at pc+1, clamped to 2) */
void OP_5C(void) {
    sFieldEntity* actor = &fieldSceneState.m290_actorArray[currentFieldActorId];
    s32 slot;
    s32* character;
    s32 sameField;
    s32 entityParam;
    s32 posX;
    s32 posZ;

    slot = getImmediateOrVariableUnsigned(1);
    if (slot > 2) {
        slot = 2;
    }
    partyGearActorIds[slot] = currentFieldActorId;
    character = &currentParty[slot];
    sameField = 1;
    if (*character != 0xFF && getPartyMemberGearNum(*character) != 0xFF) {
        OP_5CSub0(slot, &entityParam, &posX, &posZ);
        pCurrentFieldScriptActor->m10_walkmeshId = (entityParam >> 14) & 3;
        if ((fieldMapNumber & 0xFFF) != (entityParam & 0x3FFF)) {
            sameField = 0;
            posX = 0;
            posZ = 0;
            pCurrentFieldScriptActor->m10_walkmeshId = 0;
        }
        if (pKernelGameState->m22B1_isOnGear[slot] != 0) {
            sameField = 0;
        } else if (*character == 7) {
            sameField = 0;
        }
        actor->m58_flags = (actor->m58_flags & 0xF07F) | 0x200;
        resetFieldScriptEntityGraphicEntity(currentFieldActorId, slot, partyCharacterBuffers[slot], 1, 0, slot, 1);
        fieldSceneState.m290_actorArray[currentFieldActorId].m58_flags &= ~0x20;
        if ((fieldMapNumber & 0xFFF) != (entityParam & 0x3FFF)) {
            pCurrentFieldScriptActor->m10_walkmeshId = 0;
        }
        setCurrentActor2DPosition(posX, posZ);
        OP_INIT_ENTITY_SCRIPT_sub1();
        pCurrentFieldScriptActor->m0_fieldScriptFlags = (pCurrentFieldScriptActor->m0_fieldScriptFlags | 0x400) & ~0x300;
        if (!sameField) {
            fieldSceneState.m290_actorArray[currentFieldActorId].m58_flags |= 0x20;
        }
    } else {
        OP_INIT_ENTITY_SCRIPT();
        pCurrentFieldScriptActor->mCC_scriptPC += 2;
        pCurrentFieldScriptActor->m4_flags |= 0x800;
        return;
    }
    if (fieldSceneState.m2D4_numWalkMesh - 1 < pCurrentFieldScriptActor->m10_walkmeshId) {
        pCurrentFieldScriptActor->m10_walkmeshId = 0;
    }
    pCurrentFieldScriptActor->m0_fieldScriptFlags |= 0x20000;
    pCurrentFieldScriptActor->m4_flags |= 0xC00;
    pCurrentFieldScriptActor->mCC_scriptPC += 3;
}

/* Noah copyActorPositions: copy walkmesh state, position and matrices of actor source to actor dest */
void copyActorPositionsAndMatrix(s32 dest, s32 source) {
    sFieldScriptEntity* dst = fieldSceneState.m290_actorArray[dest].m4C_scriptEntity;
    sFieldScriptEntity* src = fieldSceneState.m290_actorArray[source].m4C_scriptEntity;
    s32 i;

    for (i = 0; i < 4; i++) {
        dst->m8_currentWalkMeshTriangle[i] = src->m8_currentWalkMeshTriangle[i];
    }
    dst->m10_walkmeshId = src->m10_walkmeshId;
    dst->mEC_elevation = src->mEC_elevation;
    dst->m72_elevation = src->m72_elevation;
    dst->m50_surfaceNormal.vx = src->m50_surfaceNormal.vx;
    dst->m50_surfaceNormal.vy = src->m50_surfaceNormal.vy;
    dst->m50_surfaceNormal.vz = src->m50_surfaceNormal.vz;
    dst->m20_position.vx = src->m20_position.vx;
    dst->m20_position.vy = src->m20_position.vy;
    dst->m20_position.vz = src->m20_position.vz;
    copyMatrixRotation(&fieldSceneState.m290_actorArray[dest].mC_matrix, &fieldSceneState.m290_actorArray[source].mC_matrix);
    copyMatrixTranslation(&fieldSceneState.m290_actorArray[dest].mC_matrix, &fieldSceneState.m290_actorArray[source].mC_matrix);
    fieldSceneState.m290_actorArray[dest].m4_pVramSpriteSheet->m0_position.vx = fieldSceneState.m290_actorArray[source].m4C_scriptEntity->m20_position.vx;
    fieldSceneState.m290_actorArray[dest].m4_pVramSpriteSheet->m0_position.vy = fieldSceneState.m290_actorArray[source].m4C_scriptEntity->m20_position.vy;
    fieldSceneState.m290_actorArray[dest].m4_pVramSpriteSheet->m0_position.vz = fieldSceneState.m290_actorArray[source].m4C_scriptEntity->m20_position.vz;
}

/* Noah OPX_21: OP_INIT_ENTITY_PC variant (CLUT bank 2, no player setup) */
void OPX_21(void) {
    sFieldEntity* actor = &fieldSceneState.m290_actorArray[currentFieldActorId];
    s32 slot;

    slot = findCharacterInParty(getCharacter(getImmediateOrVariableUnsigned(1)));
    actor->m58_flags = (actor->m58_flags & 0xF07F) | 0x200;
    if (slot != -1) {
        resetFieldScriptEntityGraphicEntity(currentFieldActorId, slot, partyCharacterBuffers[slot], 2, 0, slot, 1);
        OPX_21_Sub0 = -0xC0;
        fieldSceneState.m290_actorArray[currentFieldActorId].m58_flags &= ~0x20;
        OP_INIT_ENTITY_SCRIPT_sub1();
        pCurrentFieldScriptActor->m0_fieldScriptFlags = (pCurrentFieldScriptActor->m0_fieldScriptFlags | 0x100) & ~0x80;
        fieldSceneState.m290_actorArray[currentFieldActorId].m58_flags &= ~0x20;
    } else {
        resetFieldScriptEntityGraphicEntity(currentFieldActorId, 0, partyCharacterBuffers[0], 1, 0, 0, 1);
        pCurrentFieldScriptActor->m0_fieldScriptFlags |= 1;
        currentScriptFinished = 1;
        g_breakCurrentScript = 1;
        pCurrentFieldScriptActor->m4_flags |= 0x100000;
    }
    pCurrentFieldScriptActor->mCC_scriptPC += 3;
}

/* Noah OP_INIT_ENTITY_PC */
void OP_INIT_ENTITY_PC(void) {
    sFieldEntity* actor = &fieldSceneState.m290_actorArray[currentFieldActorId];
    s32 character;
    s32 slot;
    s32 pcInit;

    character = getCharacter(getImmediateOrVariableUnsigned(1));
    slot = findCharacterInParty(character);
    pCurrentFieldScriptActor->mE4_playableCharacterId = character;
    actor->m58_flags = (actor->m58_flags & 0xF07F) | 0x200;
    if (slot != -1) {
        if (slot == 0) {
            op9DVar1.mE8_playerControlledActor = currentFieldActorId;
            op9DVar1.m1BA_actorCameraTracked = currentFieldActorId;
            pCurrentFieldScriptActor->m0_fieldScriptFlags = (pCurrentFieldScriptActor->m0_fieldScriptFlags | 0x4400) & ~0x80;
        }
        partyFieldActorIds[slot] = currentFieldActorId;
        pcInit = op9DVar1.mE4_partySpriteSetupBase;
        if (pcInit != 0) {
            s32 moff = characterMappingTable[character] * 4 + 4;
            s32 poff = pcInit * 4;

            resetFieldScriptEntityGraphicEntity(currentFieldActorId, characterMappingTable[character] + pcInit,
                                                (void*)(*(s32*)(fieldActorSetupParams + moff + poff) + (sptr)fieldActorSetupParams),
                                                0, 0, (characterMappingTable[character] + op9DVar1.mE4_partySpriteSetupBase) | 0x80, 1);
            pCurrentFieldScriptActor->m0_fieldScriptFlags = (pCurrentFieldScriptActor->m0_fieldScriptFlags | 0x400) & ~0x300;
            if (pKernelGameState->m22B1_isOnGear[slot] != 0) {
                SpriteActorCore* sprite = fieldSceneState.m290_actorArray[currentFieldActorId].m4_pVramSpriteSheet;

                fieldSceneState.m290_actorArray[currentFieldActorId].m4_pVramSpriteSheet = fieldSceneState.m290_actorArray[partyGearActorIds[slot]].m4_pVramSpriteSheet;
                fieldSceneState.m290_actorArray[partyGearActorIds[slot]].m4_pVramSpriteSheet = sprite;
                pCurrentFieldScriptActor->m0_fieldScriptFlags = (pCurrentFieldScriptActor->m0_fieldScriptFlags | 0x200) & ~0x500;
            }
        } else {
            resetFieldScriptEntityGraphicEntity(currentFieldActorId, slot, partyCharacterBuffers[slot], 1, 0, slot, 1);
            pCurrentFieldScriptActor->m0_fieldScriptFlags = (pCurrentFieldScriptActor->m0_fieldScriptFlags | 0x400) & ~0x300;
        }
        OPX_21_Sub0 = -0xC0;
        fieldSceneState.m290_actorArray[currentFieldActorId].m58_flags &= ~0x20;
        setupActorToFieldEntryID(getVariable(2));
        OP_INIT_ENTITY_SCRIPT_sub1();
        pCurrentFieldScriptActor->m4_flags &= ~0x800;
    } else {
        resetFieldScriptEntityGraphicEntity(currentFieldActorId, 0, partyCharacterBuffers[0], 1, 0, 0, 1);
        pCurrentFieldScriptActor->m0_fieldScriptFlags |= 1;
        currentScriptFinished = 1;
        g_breakCurrentScript = 1;
        pCurrentFieldScriptActor->m4_flags |= 0x100000;
    }
    pCurrentFieldScriptActor->m0_fieldScriptFlags |= 0x20000;
    pCurrentFieldScriptActor->m4_flags |= 0x400;
    pCurrentFieldScriptActor->mCC_scriptPC += 3;
}

/* Noah enablePlayerFlag80 */
void enablePlayerFlag80(void) {
    fieldSceneState.m290_actorArray[g_playerControlledActor].m4C_scriptEntity->m0_fieldScriptFlags |= 0x80;
}

/* Noah OP_INIT_ENTITY_SCRIPT_sub1: copy the script position to the actor matrices and its sprite */
void OP_INIT_ENTITY_SCRIPT_sub1(void) {
    s32 id = currentFieldActorId;
    SpriteActorCore* sprite = fieldSceneState.m290_actorArray[id].m4_pVramSpriteSheet;

    fieldSceneState.m290_actorArray[id].m2C_matrixBackup.t[0] = fieldSceneState.m290_actorArray[id].mC_matrix.t[0] = pCurrentFieldScriptActor->m20_position.vx >> 16;
    fieldSceneState.m290_actorArray[id].m2C_matrixBackup.t[1] = fieldSceneState.m290_actorArray[id].mC_matrix.t[1] = pCurrentFieldScriptActor->m20_position.vy >> 16;
    fieldSceneState.m290_actorArray[id].m2C_matrixBackup.t[2] = fieldSceneState.m290_actorArray[id].mC_matrix.t[2] = pCurrentFieldScriptActor->m20_position.vz >> 16;
    sprite->m0_position.vx = pCurrentFieldScriptActor->m20_position.vx;
    sprite->m0_position.vy = pCurrentFieldScriptActor->m20_position.vy;
    sprite->m0_position.vz = pCurrentFieldScriptActor->m20_position.vz;
    sprite->mC_step.vy = 0;
    pCurrentFieldScriptActor->m72_elevation = sprite->m84_maxY = pCurrentFieldScriptActor->m20_position.vy >> 16;
}

/* Noah OP_INIT_ENTITY_SCRIPT */
void OP_INIT_ENTITY_SCRIPT(void) {
    resetFieldScriptEntityGraphicEntity(currentFieldActorId, 0, (void*)(((s32*)fieldActorSetupParams)[1] + (sptr)fieldActorSetupParams), 0, 0, 0x80, 1);
    OP_INIT_ENTITY_SCRIPT_sub1();
    pCurrentFieldScriptActor->m0_fieldScriptFlags |= 0x100;
    pCurrentFieldScriptActor->m4_flags |= 0x800;
    pCurrentFieldScriptActor->mCC_scriptPC++;
}

/* Noah OP_SET_MAX_MECHA */
void OP_SET_MAX_MECHA(void) {
    numMaxMechaOverlayEntries[0] = getImmediateOrVariableUnsigned(1);
    pCurrentFieldScriptActor->mCC_scriptPC += 3;
}

/* Noah OP_GET_CURRENT_DISC_NUMBER */
void OP_GET_CURRENT_DISC_NUMBER(void) {
    setVar(readU16FromScript(1), getCurrentDiscNumber());
    pCurrentFieldScriptActor->mCC_scriptPC += 3;
}

/* Noah OPX_CC: wait (stay on this opcode) while onlyUpdateDirector is set */
void OPX_CC(void) {
    if (onlyUpdateDirector == 0) {
        pCurrentFieldScriptActor->mCC_scriptPC++;
    } else {
        pCurrentFieldScriptActor->mCC_scriptPC--;
    }
    g_breakCurrentScript = 1;
}

/* Noah OPX_CB */
void OPX_CB(void) {
    g_breakCurrentScript = 1;
    fieldMovieStopRequest++;
    pCurrentFieldScriptActor->mCC_scriptPC++;
}

/* mecha of the current actor (m12C bits 13-15): 0 = clear loadedMechas[i]->m34, 1 = freeMecha + mE0-- */
void OPX_CA(void) {
    sFieldScriptEntity* entity = pCurrentFieldScriptActor;
    s32 mecha = entity->m12C_xD;

    entity->m4_flags &= ~0x2000;
    switch (pCurrentFieldScriptFile[entity->mCC_scriptPC + 1]) {
    case 0:
        /* Field 411's story-174 scene (actor 9) runs actor 13's routine 19
         * (hide, then free its gear) twice: the second time the slot is NULL
         * (freeMecha clears it) and retail stores into low BIOS RAM (0x34,
         * unused per psx-spx), then goes on: a Beetle run of that scene
         * reaches story 174. The PC skips the store. */
        if (loadedMechas[mecha] != NULL)
        loadedMechas[mecha]->m34_visible = 0;
        pCurrentFieldScriptActor->mCC_scriptPC += 2;
        break;
    case 1:
        freeMecha(entity->m12C_xD);
        mechaList.m88_numMechas--;
        pCurrentFieldScriptActor->mCC_scriptPC += 2;
        break;
    }
    g_breakCurrentScript = 1;
}

/* Noah OP_LOAD_NEW_MECHA: 0 = clear the mecha's m34, 1 = start loading mecha (imm/var at pc+5), 2 = finish */
void OP_LOAD_NEW_MECHA(void) {
    s32 mecha;

    if (loadCompleted != 0 || isLoadCompleted() != 0) {
        g_breakCurrentScript = 1;
        pCurrentFieldScriptActor->mCC_scriptPC -= 1;
        return;
    }
    mecha = pCurrentFieldScriptActor->m12C_xD;
    pCurrentFieldScriptActor->m4_flags &= ~0x2000;
    setCurrentDirectory(4, 0);
    switch (pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 1]) {
    case 0:
        loadedMechas[mecha]->m34_visible = 0;
        pCurrentFieldScriptActor->mCC_scriptPC += 2;
        break;
    case 1:
        freeMecha(pCurrentFieldScriptActor->m12C_xD);
        mechaList.m0_mechaList[mecha] = getImmediateOrVariableUnsigned(5) * 2;
        mechaOverlayBatchLoadingTable[0].m0_fileIndex = mechaList.m0_mechaList[mecha] + 0x6BA;
        mechaOverlayBatchLoadingTable[0].m4_loadPtr = mechaDataTable2[mecha] =
            allocateHeap(PC_HEAP_BYTES(getFileSizeAligned(mechaList.m0_mechaList[mecha] + 0x6BA)), 0);
        mechaOverlayBatchLoadingTable[1].m0_fileIndex = mechaList.m0_mechaList[mecha] + 0x6BB;
        mechaOverlayBatchLoadingTable[1].m4_loadPtr = mechaDataTable1[mecha] =
            allocateHeap(PC_HEAP_BYTES(getFileSizeAligned(mechaList.m0_mechaList[mecha] + 0x6BB)), 1);
        mechaOverlayBatchLoadingTable[2].m0_fileIndex = 0;
        mechaOverlayBatchLoadingTable[2].m4_loadPtr = NULL;
        batchStartLoadingFiles(mechaOverlayBatchLoadingTable, 0, 0);
        pCurrentFieldScriptActor->mCC_scriptPC += 2;
        break;
    case 2:
        if (waitReadCompletion(1) == 0) {
            getImmediateOrVariableUnsigned(2);
            mechaInitNewMecha(mecha, 0, mechaDataTable2[mecha], mechaDataTable1[mecha],
                          0x240 - (mecha + mechaList.m83_mechaList2[mecha]) * 0x40, 0x100, 0, mecha + 0xFC,
                          &mechaList.m10_initMechaTempVar[mecha]);
            mechaList.m30_mechaList3[mecha] = loadedMechas[mecha]->m1C_scale;
            freeHeap(mechaDataTable1[mecha]);
            pCurrentFieldScriptActor->mCC_scriptPC += 4;
            pCurrentFieldScriptActor->m4_flags |= 0x2000;
            loadedMechas[mecha]->m1C_scale = (pCurrentFieldScriptActor->mF4_scale3d[0] * 5) >> 6;
            loadedMechas[mecha]->m60_groundY = pCurrentFieldScriptActor->m20_position.vy >> 16;
            loadedMechas[mecha]->m4_bones->m5C_translation[0] = pCurrentFieldScriptActor->m20_position.vx >> 16;
            loadedMechas[mecha]->m4_bones->m5C_translation[2] = pCurrentFieldScriptActor->m20_position.vz >> 16;
        } else {
            pCurrentFieldScriptActor->mCC_scriptPC -= 1;
        }
        break;
    }
    g_breakCurrentScript = 1;
}

/* Noah OP_ADD_ENTITY_TO_MECHA_LIST */
void OP_ADD_ENTITY_TO_MECHA_LIST(void) {
    sFieldEntity* actor;
    s32 mecha;

    actor = &fieldSceneState.m290_actorArray[currentFieldActorId];
    actor->m58_flags = (actor->m58_flags & 0xF07F) | 0x200;
    mecha = getImmediateOrVariableUnsigned(1);
    resetFieldScriptEntityGraphicEntity(currentFieldActorId, 0, (void*)(((s32*)fieldActorSetupParams)[1] + (sptr)fieldActorSetupParams), 0, 0, 0x80, 1);
    OP_INIT_ENTITY_SCRIPT_sub1();
    pCurrentFieldScriptActor->mCC_scriptPC += 3;
    pCurrentFieldScriptActor->m0_fieldScriptFlags |= 0x100;
    fieldSceneState.m290_actorArray[currentFieldActorId].m58_flags &= ~0x20;
    pCurrentFieldScriptActor->m4_flags = (pCurrentFieldScriptActor->m4_flags | 0x2000) & ~0x800;
    mechaList.m0_mechaList[mechaList.m88_numMechas] = mecha * 2;
    mechaList.m83_mechaList2[mechaList.m88_numMechas] = 0;
    pCurrentFieldScriptActor->m12C_xD = mechaList.m88_numMechas;
    mechaList.m88_numMechas++;
}

/* OP_INIT_ENTITY_NPC with a CLUT x (imm/var at pc+3) and the m134 bit set */
void OPX_15(void) {
    sFieldEntity* actor;
    s32 index;
    void* setup;

    actor = &fieldSceneState.m290_actorArray[currentFieldActorId];
    actor->m58_flags = (actor->m58_flags & 0xF07F) | 0x200;
    index = getImmediateOrVariableUnsigned(1);
    setup = (void*)(((s32*)fieldActorSetupParams)[index + 1] + (sptr)fieldActorSetupParams);
    resetFieldScriptEntityGraphicEntity(currentFieldActorId, index, setup, 0, getImmediateOrVariableUnsigned(3), index | 0x80, 1);
    OP_INIT_ENTITY_SCRIPT_sub1();
    pCurrentFieldScriptActor->mCC_scriptPC += 5;
    pCurrentFieldScriptActor->m0_fieldScriptFlags = (pCurrentFieldScriptActor->m0_fieldScriptFlags | 0x100) & ~0x80;
    pCurrentFieldScriptActor->m4_flags &= ~0x800;
    fieldSceneState.m290_actorArray[currentFieldActorId].m58_flags &= ~0x20;
}

/* Noah OP_INIT_ENTITY_NPC */
void OP_INIT_ENTITY_NPC(void) {
    sFieldEntity* actor;
    s32 index;

    actor = &fieldSceneState.m290_actorArray[currentFieldActorId];
    actor->m58_flags = (actor->m58_flags & 0xF07F) | 0x200;
    index = getImmediateOrVariableUnsigned(1);
    resetFieldScriptEntityGraphicEntity(currentFieldActorId, index, (void*)(((s32*)fieldActorSetupParams)[index + 1] + (sptr)fieldActorSetupParams), 0, 0, index | 0x80, 0);
    OP_INIT_ENTITY_SCRIPT_sub1();
    pCurrentFieldScriptActor->mCC_scriptPC += 3;
    pCurrentFieldScriptActor->m0_fieldScriptFlags = (pCurrentFieldScriptActor->m0_fieldScriptFlags | 0x100) & ~0x80;
    pCurrentFieldScriptActor->m4_flags &= ~0x800;
    fieldSceneState.m290_actorArray[currentFieldActorId].m58_flags &= ~0x20;
}
