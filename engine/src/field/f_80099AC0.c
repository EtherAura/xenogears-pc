
#include "common.h"
#include "psyq.h"
#include "main/sprite.h"
#include "field/field.h"

#define FP_INT(x) (((s16*)&(x))[1])

s32 getAngleCos(s32 angle);                /* main: cosine (Noah: getAngleCos) */
s32 getAngleSin(s32 angle);                /* main: sine (Noah: getAngleSin) */
s32 getCurrentActorRotationIn8Cardinal(void);
void setCurrentActorTargetRotation(u16 rotation);
void OP_67Sub(u16 rotation);
void setCurrentActorRotation2(s32 rotation);
extern s32 fieldExecuteVar1;
/* Scripts index these with variables and bytes past their 8 entries (field
 * 129's actor 14 turns with index 12, which is actorDirectionTable2[4]): the
 * extent runs on through the next tables to partyPortraitsRects, so the PC
 * keeps them in one piece as the PS1 does. */
extern u16 actorDirectionTable[0x58];
extern u16 actorDirectionTable2[8];
extern u16 actorDirectionTable3[8];
extern s16 OP_B5SubTable[64];                   /* Noah: OP_B5SubTable [8][8] */
extern s32 currentFieldActorId;
s32 initFollowStructForPlayer(s32 actor);
s32 length1d(s32 x);
s32 length2d(s32 x, s32 y);
void setCurrentActor2DPosition(s32 posX, s32 posZ);
s32 walkPartyMemberToPosition(s32 partyIndex, s32 x, s32 z, s32 direction);
void resetPartyFollowStructs(void);
extern FieldBss_800AF880 fieldSceneState;
extern FieldBss_800B2184 op9DVar1;
extern s32 g_breakCurrentScript;
extern s32 g_playerControlledActor;
u16 getCameraDirection(void);
void OP_A4Sub(s32 dip, s32 steps);
void OP_B6Sub(s32 scrz, s32 steps);
void OP_B5Sub(s32 direction, s32 steps);
void startCameraDollyRotation(s32 clockwise, s32 steps);

s32 spriteWalkToPositionOrActor(s32 numSteps) {
    SpriteActorCore* sprite;
    sFieldScriptEntity* other;
    sFieldScriptEntity* p;
    VECTOR delta;
    s32 destX;
    s32 destZ;
    s32 curX;
    s32 curZ;
    s32 stepLength;
    s32 radius;
    s32 dist;
    s32 angle;
    sptr rotation; /* also holds pCurrentFieldScriptActor in case 2 (a second set keeps its load unboosted in sched1) */
    u32 flags;
    sFieldScriptEntity* ent; /* case 2: actorArray base, then pCurrentFieldScriptActor (two sets: see above) */

    radius = 0;
    destZ = 0;
    destX = 0;
    sprite = fieldSceneState.m290_actorArray[currentFieldActorId].m4_pVramSpriteSheet;
    if (fieldSceneState.m290_actorArray[currentFieldActorId].m4C_scriptEntity->m4_flags & 0x2000) {
        sprite->m18_moveSpeed = 0x8000000 / pCurrentFieldScriptActor->m76_timeScale /* base: the sprite scales it */;
    } else if (sprite->m18_moveSpeed == 0) {
        sprite->m18_moveSpeed = 0x4000000 / pCurrentFieldScriptActor->m76_timeScale /* base: the sprite scales it */;
    }
    stepLength = length1d(sprite->m18_moveSpeed >> 15) + 1;
    curX = FP_INT(pCurrentFieldScriptActor->m20_position.vx);
    curZ = FP_INT(pCurrentFieldScriptActor->m20_position.vz);
    switch (pCurrentFieldScriptActor->m8C_scriptSlots[pCurrentFieldScriptActor->mCE_currentScriptSlot].m4_x17_walkMode) {
    case 1:
        destX = getVar80(1, pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 5]) + pCurrentFieldScriptActor->mD0_targetPositionOffset.vx;
        destZ = getVar40(3, pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 5]) + pCurrentFieldScriptActor->mD0_targetPositionOffset.vz;
        break;
    case 2:
        if (readCharacter(1) == 0xFF) {
            return 0;
        }
        do {
            s32 i = readCharacter(1);
            ent = (sFieldScriptEntity*)fieldSceneState.m290_actorArray;
            other = ((sFieldEntity*)ent)[i].m4C_scriptEntity;
            rotation = (sptr)pCurrentFieldScriptActor;
        } while (0);
        radius = length1d(other->m1E_collisionRadius + ((sFieldScriptEntity*)rotation)->m1E_collisionRadius);
        destX = FP_INT(other->m20_position.vx);
        destZ = FP_INT(other->m20_position.vz);
        ent = pCurrentFieldScriptActor;
        if (pCurrentFieldScriptFile[ent->mCC_scriptPC + 1] == g_playerControlledActor) {
            ent->m0_fieldScriptFlags |= 0x200000;
        }
        break;
    case 3:
        angle = getImmediateOrVariableUnsigned(1) & 0xFFF;
        destX = pCurrentFieldScriptActor->mD0_targetPositionOffset.vx + ((getAngleSin(angle) << 12) >> 12);
        destZ = pCurrentFieldScriptActor->mD0_targetPositionOffset.vz + ((-(getAngleCos(angle) << 12)) >> 12);
        break;
    case 0:
    case 4:
        destX = getVar80(1, pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 5]);
        destZ = getVar40(3, pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 5]);
        break;
    }
    delta.vx = destX - curX;
    delta.vy = 0;
    delta.vz = destZ - curZ;
    dist = length2d(delta.vx, delta.vz);
    p = pCurrentFieldScriptActor;
    flags = p->m0_fieldScriptFlags | 0x400000;
    p->m0_fieldScriptFlags = flags;
    if (p->m8C_scriptSlots[p->mCE_currentScriptSlot].m4_x0_numSteps == 0 || stepLength + radius >= dist) {
        if (numSteps != 0) {
            if (!(flags & 0x8000)) {
                p->m106_currentRotation = p->m104_rotation = (u16)p->m106_currentRotation | 0x8000;
            } else {
                p->m106_currentRotation = p->m104_rotation = (u16)p->m11C_lockedRotation | 0x8000;
            }
        } else {
            pCurrentFieldScriptActor->m106_currentRotation = pCurrentFieldScriptActor->m104_rotation = fp_atan2(&delta);
        }
        pCurrentFieldScriptActor->m8C_scriptSlots[pCurrentFieldScriptActor->mCE_currentScriptSlot].m4_x0_numSteps = 0xFFFF;
        pCurrentFieldScriptActor->m8C_scriptSlots[pCurrentFieldScriptActor->mCE_currentScriptSlot].m4_x17_walkMode = 0;
        pCurrentFieldScriptActor->m0_fieldScriptFlags &= 0xFDDFF7FF;
        return 0;
    }
    p->m8C_scriptSlots[p->mCE_currentScriptSlot].m4_x0_numSteps--;
    rotation = fp_atan2(&delta);
    g_breakCurrentScript = 1;
    pCurrentFieldScriptActor->m104_rotation = rotation;
    pCurrentFieldScriptActor->m106_currentRotation = rotation;
    return -1;
}

void OP_2F(void) {
    setVar(readU16FromScript(1), pCurrentFieldScriptActor->mE4_playableCharacterId);
    pCurrentFieldScriptActor->mCC_scriptPC += 3;
}

void OP_30(void) {
    sFieldScriptEntity* entity = fieldSceneState.m290_actorArray[g_playerControlledActor].m4C_scriptEntity;
    setVar(readU16FromScript(1), entity->mE4_playableCharacterId);
    pCurrentFieldScriptActor->mCC_scriptPC += 3;
}

void OP_GET_ACTOR_FACING_ANGLE(void) {
    setVar(readU16FromScript(1), (((pCurrentFieldScriptActor->m106_currentRotation + 0x100) >> 9) + 2) & 7);
    pCurrentFieldScriptActor->mCC_scriptPC += 3;
}

void OP_GET_ACTOR_POSITION(void) {
    s32 actor = readCharacter(1);

    if (actor != 0xFF) {
        setVar(readU16FromScript(2), fieldSceneState.m290_actorArray[actor].m2C_matrixBackup.t[0]);
        setVar(readU16FromScript(4), fieldSceneState.m290_actorArray[actor].m2C_matrixBackup.t[2]);
        setVar(readU16FromScript(6), fieldSceneState.m290_actorArray[actor].m2C_matrixBackup.t[1]);
    }
    pCurrentFieldScriptActor->mCC_scriptPC += 8;
}

void OPX_45(void) {
    pCurrentFieldScriptActor->mE6_idleAnimationId = pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 1];
    pCurrentFieldScriptActor->mCC_scriptPC += 2;
}

void OP_PLAY_ANIMATION(void) {
    pCurrentFieldScriptActor->m4_flags &= ~0x01000000;
    pCurrentFieldScriptActor->mEA_forcedAnimation = pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 1];
    pCurrentFieldScriptActor->mCC_scriptPC += 2;
}

void OP_PLAY_ANIMATION_EX(void) {
    OP_PLAY_ANIMATION();
    pCurrentFieldScriptActor->m4_flags &= ~0x10000;
}

void OP_WAIT_ANIMATION_END(void) {
    if (pCurrentFieldScriptActor->m4_flags & 0x10000) {
        pCurrentFieldScriptActor->mEA_forcedAnimation = 0xFF;
        pCurrentFieldScriptActor->mCC_scriptPC += 1;
    }
}

void OP_ROTATE_AWAY_FROM_ACTOR(void) {
    s32 actor = partyFieldActorIds[pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 1]];
    u16 rotation;

    if (actor != 0xFF) {
        sFieldScriptEntity* other = fieldSceneState.m290_actorArray[actor].m4C_scriptEntity;
        rotation = -ratan2(other->m20_position.vz - pCurrentFieldScriptActor->m20_position.vz, other->m20_position.vx - pCurrentFieldScriptActor->m20_position.vx) | 0x8000;
        pCurrentFieldScriptActor->m104_rotation = rotation;
        pCurrentFieldScriptActor->m106_currentRotation = rotation;
    }
    pCurrentFieldScriptActor->mCC_scriptPC += 2;
}

void OP_ROTATE_TO_ACTOR(void) {
    s32 actor = readCharacter(1);
    u16 rotation;

    if (actor != 0xFF) {
        sFieldScriptEntity* other = fieldSceneState.m290_actorArray[actor].m4C_scriptEntity;
        rotation = -ratan2(other->m20_position.vz - pCurrentFieldScriptActor->m20_position.vz, other->m20_position.vx - pCurrentFieldScriptActor->m20_position.vx) | 0x8000;
        pCurrentFieldScriptActor->m104_rotation = rotation;
        pCurrentFieldScriptActor->m106_currentRotation = rotation;
    }
    pCurrentFieldScriptActor->mCC_scriptPC += 2;
}

void OP_9D(void) {
    s32 target;

    fieldSceneState.m19C_op9DVar0 = pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 3];
    if (fieldSceneState.m19C_op9DVar0 == 0) {
        fieldSceneState.m19C_op9DVar0++;
        op9DVar1.m54_cameraSnapFrames += 2;
    }
    target = getImmediateOrVariableUnsigned(1);
    fieldSceneState.m1A4_op9DVar2 = -((fieldSceneState.m17E_sceneScale - target) << 16) / fieldSceneState.m19C_op9DVar0;
    fieldSceneState.m1A0_op9DVar3 = fieldSceneState.m17E_sceneScale << 16;
    fieldSceneState.m158_op99Var7 |= 1;
    pCurrentFieldScriptActor->mCC_scriptPC += 4;
}

void OP_A4Sub(s32 dip, s32 steps) {
    if (steps == 0) {
        op9DVar1.m54_cameraSnapFrames = 2;
        steps = 1;
    }
    fieldSceneState.m180_OP_A4Var0 = steps;
    fieldSceneState.m184_OP_A4Var1 = fieldSceneState.m17C_sceneDIP << 16;
    fieldSceneState.m158_op99Var7 |= 8;
    fieldSceneState.m188_OP_A4Var2 = -((fieldSceneState.m17C_sceneDIP - dip) << 16) / steps;
}

void OP_A4(void) {
    OP_A4Sub(getVar80(1, pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 3]), pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 3] & 0x7F);
    pCurrentFieldScriptActor->mCC_scriptPC += 4;
}

u16 getCameraDirection(void) {
    return (7 - ((fieldSceneState.m10C_camera2Tan - 0x100) >> 9)) & 7;
}

void OP_A5(void) {
    setVar(readU16FromScript(1), getCameraDirection());
    pCurrentFieldScriptActor->mCC_scriptPC += 3;
}

void OP_WAIT_FOR_CAMERA_ANIMATION_FLAG_CLEAR(void) {
    if ((fieldSceneState.m158_op99Var7 & pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 1]) == 0) {
        pCurrentFieldScriptActor->mCC_scriptPC += 2;
    } else {
        g_breakCurrentScript = 1;
    }
}

void OP_B2(void) {
    if ((fieldSceneState.m158_op99Var7 & pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 1]) == 0) {
        pCurrentFieldScriptActor->mCC_scriptPC += 2;
    } else {
        g_breakCurrentScript = 1;
    }
}

void OP_97(void) {
    fieldSceneState.m174_dollySet = getImmediateOrVariableUnsigned(1);
    pCurrentFieldScriptActor->mCC_scriptPC += 3;
}

void OP_SET_DOLLY_STOP(void) {
    fieldSceneState.m175_dollyStop = getImmediateOrVariableUnsigned(1);
    pCurrentFieldScriptActor->mCC_scriptPC += 3;
}

void OP_GET_ANGLE_COS(void) {
    s32 var = readU16FromScript(1);
    s32 angle = getVar40(3, pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 7]);
    s32 length = getVar20(5, pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 7]);
    setVar(var, (getAngleCos(angle) * length) >> 12);
    pCurrentFieldScriptActor->mCC_scriptPC += 8;
}

void OP_GET_ANGLE_SIN(void) {
    s32 var = readU16FromScript(1);
    s32 angle = getVar40(3, pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 7]);
    s32 length = getVar20(5, pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 7]);
    setVar(var, (getAngleSin(angle) * length) >> 12);
    pCurrentFieldScriptActor->mCC_scriptPC += 8;
}

void OP_CA(void) {
    s32 var = readU16FromScript(1);
    s32 y = getVar40(3, pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 7]);
    s32 x = getVar20(5, pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 7]);
    setVar(var, (s16)ratan2(y, x));
    pCurrentFieldScriptActor->mCC_scriptPC += 8;
}

s32 getCurrentActorRotationIn8Cardinal(void) {
    return (((pCurrentFieldScriptActor->m106_currentRotation + 0x100) >> 9) + 2) & 7;
}

void setCurrentActorTargetRotation(u16 rotation) {
    if (fieldExecuteVar1 == 0) {
        pCurrentFieldScriptActor->m104_rotation = rotation | 0x8000;
        pCurrentFieldScriptActor->m106_currentRotation = rotation | 0x8000;
        pCurrentFieldScriptActor->m108_rotation3 = rotation | 0x8000;
    }
    pCurrentFieldScriptActor->m104_rotation = rotation | 0x8000;
    pCurrentFieldScriptActor->m106_currentRotation = rotation | 0x8000;
    pCurrentFieldScriptActor->mCC_scriptPC += 3;
}

void OP_67Sub(u16 rotation) {
    sFieldScriptEntity* entity;

    if (readCharacter(1) != 0xFF) {
        entity = fieldSceneState.m290_actorArray[readCharacter(1)].m4C_scriptEntity;
        if (fieldExecuteVar1 == 0) {
            entity->m104_rotation = rotation | 0x8000;
            entity->m106_currentRotation = rotation | 0x8000;
            entity->m108_rotation3 = rotation | 0x8000;
        }
        entity->m104_rotation = rotation | 0x8000;
        entity->m106_currentRotation = rotation | 0x8000;
    }
    pCurrentFieldScriptActor->mCC_scriptPC += 4;
}

void OPX_ROTATE_CHARACTER_TO_CHARACTER(void) {
    sFieldScriptEntity* target;
    sFieldScriptEntity* entity;
    u16 rotation;

    if (readCharacter(1) != 0xFF && readCharacter(2) != 0xFF) {
        target = fieldSceneState.m290_actorArray[readCharacter(2)].m4C_scriptEntity;
        entity = fieldSceneState.m290_actorArray[readCharacter(1)].m4C_scriptEntity;
        rotation = -ratan2(target->m20_position.vz - entity->m20_position.vz, target->m20_position.vx - entity->m20_position.vx) | 0x8000;
        if (fieldExecuteVar1 == 0) {
            entity->m104_rotation = rotation;
            entity->m106_currentRotation = rotation;
            entity->m108_rotation3 = rotation;
        }
        entity->m104_rotation = rotation;
        entity->m106_currentRotation = rotation;
    }
    pCurrentFieldScriptActor->mCC_scriptPC += 3;
}

void setCurrentActorRotation2(s32 rotation) {
    u16 r = ((rotation - fieldSceneState.m10C_camera2Tan) & 0xFFF) | 0x8000;
    pCurrentFieldScriptActor->m104_rotation = r;
    pCurrentFieldScriptActor->m106_currentRotation = r;
    if (fieldExecuteVar1 == 0) {
        pCurrentFieldScriptActor->m108_rotation3 = r;
    }
    pCurrentFieldScriptActor->mCC_scriptPC += 3;
}

void OP_ROTATE_ACTOR_CLOCKWISE(void) {
    s32 n = getImmediateOrVariableUnsigned(1);
    setCurrentActorTargetRotation(actorDirectionTable[(n + getCurrentActorRotationIn8Cardinal()) & 7]);
}

void OP_ROTATE_ACTOR_ANTICLOCKWISE(void) {
    s32 n = getImmediateOrVariableUnsigned(1);
    setCurrentActorTargetRotation(actorDirectionTable[(getCurrentActorRotationIn8Cardinal() - n) & 7]);
}

void OP_SET_CURRENT_ACTOR_ROTATION_WORLD_RELATIVE(void) {
    OP_67Sub(actorDirectionTable[getImmediateOrVariableUnsigned(2)]);
}

void OP_68(void) {
    OP_67Sub((actorDirectionTable[getImmediateOrVariableUnsigned(2)] - fieldSceneState.m10C_camera2Tan) & 0xFFF);
}

void OP_SET_CURRENT_ACTOR_ROTATION(void) {
    setCurrentActorTargetRotation(actorDirectionTable[getImmediateOrVariableUnsigned(1)]);
}

void OP_SET_ACTOR_ROTATION2(void) {
    setCurrentActorRotation2(actorDirectionTable[getImmediateOrVariableUnsigned(1)]);
}

void OP_ROTATE_CAMERA_RELATIVE(void) {
    sFieldScriptEntity* entity = pCurrentFieldScriptActor;
    u16 r = ((actorDirectionTable2[pCurrentFieldScriptFile[entity->mCC_scriptPC + 1]] - fieldSceneState.m10C_camera2Tan) & 0xFFF) | 0x8000;
    entity->m104_rotation = r;
    entity->m106_currentRotation = r;
    if (fieldExecuteVar1 == 0) {
        entity->m108_rotation3 = r;
    }
    pCurrentFieldScriptActor->mCC_scriptPC += 2;
}

void OP_SET_CURRENT_ACTOR_CARDINAL_DIRECTION(void) {
    sFieldScriptEntity* entity = pCurrentFieldScriptActor;
    u16 r = actorDirectionTable3[pCurrentFieldScriptFile[entity->mCC_scriptPC + 1]] | 0x8000;
    entity->m104_rotation = r;
    entity->m106_currentRotation = r;
    if (fieldExecuteVar1 == 0) {
        entity->m108_rotation3 = r;
    }
    pCurrentFieldScriptActor->mCC_scriptPC += 2;
}

void OP_B7(void) {
    fieldSceneState.m158_op99Var7 |= 0x4000;
    pCurrentFieldScriptActor->mCC_scriptPC += 1;
}

void OP_ENABLE_CAMERA_HEIGHT_CHECK(void) {
    fieldSceneState.m158_op99Var7 &= 0xBFFF;
    pCurrentFieldScriptActor->mCC_scriptPC += 1;
}

void OP_B6Sub(s32 scrz, s32 steps) {
    if (steps != 0) {
        fieldSceneState.m158_op99Var7 |= 0x10;
        fieldSceneState.m190_OP_B6SubVar0 = steps;
        fieldSceneState.m194_OP_B6Var2 = fieldSceneState.m178_sceneSCRZ << 16;
        fieldSceneState.m198_OP_B6Var1 = -((fieldSceneState.m178_sceneSCRZ - scrz) << 16) / steps;
    } else {
        fieldSceneState.m178_sceneSCRZ = scrz;
        fieldSceneState.m190_OP_B6SubVar0 = 0;
        op9DVar1.m54_cameraSnapFrames += 2;
    }
    fieldSceneState.m158_op99Var7 &= 0xDFFF;
}

s32 walkPartyMemberToPosition(s32 partyIndex, s32 x, s32 z, s32 direction) {
    s32 actor;
    SpriteActorCore* sprite;
    sFieldScriptEntity* entity;
    s32 stepLength;
    s32 dist;
    s32 dx;
    s32 dz;
    VECTOR delta;
    sFieldScriptEntity* savedActor;
    s32 savedId;

    actor = partyFieldActorIds[partyIndex];
    if (actor == 0xFF || (fieldSceneState.m290_actorArray[actor].m58_flags & 0x20)) {
        return 0;
    }
    sprite = fieldSceneState.m290_actorArray[actor].m4_pVramSpriteSheet;
    entity = fieldSceneState.m290_actorArray[actor].m4C_scriptEntity;
    if (sprite->m18_moveSpeed == 0) {
        sprite->m18_moveSpeed = 0x4000000 / entity->m76_timeScale /* base: the sprite scales it */;
    }
    stepLength = length1d(sprite->m18_moveSpeed >> 15) + 1;
    dx = x - FP_INT(entity->m20_position.vx);
    dz = z - FP_INT(entity->m20_position.vz);
    delta.vy = 0;
    delta.vx = dx;
    delta.vz = dz;
    dist = length2d(dx, dz);
    entity->m0_fieldScriptFlags |= 0x400000;
    if (stepLength < dist) {
        goto walk;
    }
finish:
    if (!(entity->m0_fieldScriptFlags & 0x8000)) {
        if (direction == 0xFF) {
            entity->m106_currentRotation = entity->m104_rotation = (u16)entity->m106_currentRotation | 0x8000;
        } else {
            entity->m106_currentRotation = entity->m104_rotation = actorDirectionTable[direction] | 0x8000;
        }
    } else {
        entity->m106_currentRotation = entity->m104_rotation = (u16)entity->m11C_lockedRotation | 0x8000;
    }
    entity->m20_position.vx = x << 16;
    entity->m20_position.vz = z << 16;
    entity->m6E_stuckCounter = 0;
    entity->m0_fieldScriptFlags &= 0xFDDFF7FF;
    return 0;
walk:
    if (entity->m68_oldPosition[0] == FP_INT(entity->m20_position.vx) &&
        entity->m68_oldPosition[1] == FP_INT(entity->m20_position.vy) &&
        entity->m68_oldPosition[2] == FP_INT(entity->m20_position.vz)) {
        entity->m6E_stuckCounter++;
    } else {
        entity->m6E_stuckCounter = 0;
    }
    {
        s16 rotation = fp_atan2(&delta);
        entity->m104_rotation = rotation;
        entity->m106_currentRotation = rotation;
    }
    if (entity->m6E_stuckCounter >= 0x41 || op9DVar1.m1C4_warpPartyRequest != 0) {
        savedActor = pCurrentFieldScriptActor;
        savedId = currentFieldActorId;
        pCurrentFieldScriptActor = entity;
        currentFieldActorId = partyFieldActorIds[partyIndex];
        setCurrentActor2DPosition(x, z);
        pCurrentFieldScriptActor = savedActor;
        currentFieldActorId = savedId;
        goto finish;
    }
    return -1;
}

void OPX_43(void) {
    op9DVar1.m4B_partyFollowDisabled = 1;
    pCurrentFieldScriptActor->mCC_scriptPC += 1;
}

void OPX_44(void) {
    s32 i;

    op9DVar1.m48_noUpdatesToPartyMembers = op9DVar1.m4B_partyFollowDisabled = 0;
    partyToFollowStructMapping[0] = partyToFollowStructMapping[1] = partyToFollowStructMapping[2] = 0;
    op9DVar1.m4A_partyPositioned = 0;
    for (i = 0; i < 0x20; i++) {
        initFollowStructForPlayer(g_playerControlledActor);
    }
    pCurrentFieldScriptActor->mCC_scriptPC += 1;
}

void OPX_24(void) {
    sFieldScriptEntity* leader = fieldSceneState.m290_actorArray[partyFieldActorIds[0]].m4C_scriptEntity;
    s32 x = FP_INT(leader->m20_position.vx);
    s32 z = FP_INT(leader->m20_position.vz);
    s32 done;

    done = walkPartyMemberToPosition(0, x, z, 0xFF) == 0;
    if (walkPartyMemberToPosition(1, x, z, 0xFF) == 0) {
        done |= 2;
    }
    if (walkPartyMemberToPosition(2, x, z, 0xFF) == 0) {
        done |= 4;
    }
    g_breakCurrentScript = 1;
    if (done == 7) {
        pCurrentFieldScriptActor->mCC_scriptPC += 1;
        op9DVar1.m48_noUpdatesToPartyMembers = 0;
        op9DVar1.m1C4_warpPartyRequest = 0;
        resetPartyFollowStructs();
    } else {
        op9DVar1.m48_noUpdatesToPartyMembers = 1;
        pCurrentFieldScriptActor->mCC_scriptPC -= 1;
    }
}

void resetPartyFollowStructs(void) {
    s32 i;

    partyToFollowStructMapping[0] = partyToFollowStructMapping[1] = partyToFollowStructMapping[2] = 0;
    op9DVar1.m4A_partyPositioned = 0;
    for (i = 0; i < 0x20; i++) {
        initFollowStructForPlayer(g_playerControlledActor);
    }
}

void OPX_23(void) {
    s32 i;
    s32 done;
    sFieldScriptEntity* entity;

    if (getVar80(1, pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 0xD]) == 0x7FFF) {
        pCurrentFieldScriptActor->mCC_scriptPC += 0x14;
        op9DVar1.m4A_partyPositioned = 1;
        for (i = 0; i < 3; i++) {
            if (partyFieldActorIds[i] != 0xFF) {
                entity = fieldSceneState.m290_actorArray[partyFieldActorIds[i]].m4C_scriptEntity;
                entity->m106_currentRotation = entity->m104_rotation = entity->m106_currentRotation | 0x8000;
            }
        }
        return;
    }
    done = walkPartyMemberToPosition(0, getVar80(1, pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 0xD]), getVar40(3, pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 0xD]), getImmediateOrVariableUnsigned(0xE)) == 0;
    if (walkPartyMemberToPosition(1, getVar20(5, pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 0xD]), getVar10(7, pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 0xD]), getImmediateOrVariableUnsigned(0x10)) == 0) {
        done |= 2;
    }
    if (walkPartyMemberToPosition(2, getVar08(9, pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 0xD]), getVar04(0xB, pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 0xD]), getImmediateOrVariableUnsigned(0x12)) == 0) {
        done |= 4;
    }
    g_breakCurrentScript = 1;
    if (done == 7) {
        op9DVar1.m1C4_warpPartyRequest = 0;
        pCurrentFieldScriptActor->mCC_scriptPC += 0x14;
        op9DVar1.m48_noUpdatesToPartyMembers = 0;
    } else {
        op9DVar1.m48_noUpdatesToPartyMembers = 1;
        pCurrentFieldScriptActor->mCC_scriptPC -= 1;
    }
    op9DVar1.m4A_partyPositioned = 1;
}

void OPX_22(void) {
    setVar(readU16FromScript(1), fieldSceneState.m178_sceneSCRZ);
    pCurrentFieldScriptActor->mCC_scriptPC += 3;
}

void OP_B6(void) {
    s32 scrz = getImmediateOrVariableUnsigned(1);
    OP_B6Sub(scrz, getImmediateOrVariableUnsigned(3));
    pCurrentFieldScriptActor->mCC_scriptPC += 5;
}

void OP_B5Sub(s32 direction, s32 steps) {
    s32 camera = getCameraDirection();
    if (steps == 0) {
        steps = 1;
        op9DVar1.m54_cameraSnapFrames += 2;
    }
    fieldSceneState.m16C_cameraDollyVar1 = (OP_B5SubTable[camera * 8 + direction] << 25) / steps;
    fieldSceneState.m176_cameraDollyVar0 = steps;
    fieldSceneState.m18C_opA0_var0 = ((direction + 4) & 7) << 9;
}

void startCameraDollyRotation(s32 clockwise, s32 steps) {
    if (steps == 0) {
        steps = 1;
        op9DVar1.m54_cameraSnapFrames += 2;
    }
    if (clockwise == 0) {
        fieldSceneState.m16C_cameraDollyVar1 = 0x2000000 / steps;
        fieldSceneState.m18C_opA0_var0 += 0x200;
    } else {
        fieldSceneState.m16C_cameraDollyVar1 = -0x2000000 / steps;
        fieldSceneState.m18C_opA0_var0 -= 0x200;
    }
    fieldSceneState.m176_cameraDollyVar0 = steps;
}

void OP_C7(void) {
    if (fieldSceneState.m176_cameraDollyVar0 == 0) {
        startCameraDollyRotation(0, getImmediateOrVariableUnsigned(1));
        pCurrentFieldScriptActor->mCC_scriptPC += 3;
    }
    g_breakCurrentScript = 1;
}

void OP_C8(void) {
    if (fieldSceneState.m176_cameraDollyVar0 == 0) {
        startCameraDollyRotation(0, getImmediateOrVariableUnsigned(1));
        pCurrentFieldScriptActor->mCC_scriptPC += 3;
    }
    g_breakCurrentScript = 1;
}

void OP_B5(void) {
    s32 direction = getImmediateOrVariableUnsigned(1);
    s32 steps = getImmediateOrVariableUnsigned(3);
    s32 d;
    s16 a;

    if (fieldExecuteVar1 == 0) {
        d = (direction + 4) & 7;
        a = d << 9;
        fieldSceneState.m164_cameraRotationBetweenEyeAndAt.vy = d << 9;
        fieldSceneState.m18C_opA0_var0 = a;
        pCurrentFieldScriptActor->mCC_scriptPC += 5;
    } else {
        if (fieldSceneState.m176_cameraDollyVar0 != 0) {
            goto end;
        }
        OP_B5Sub(direction, steps);
        pCurrentFieldScriptActor->mCC_scriptPC += 5;
    }
end:
    g_breakCurrentScript = 1;
}

void OP_9E(void) {
    if (fieldSceneState.m176_cameraDollyVar0 == 0) {
        fieldSceneState.m1C8_savedCameraDirection = getCameraDirection();
        fieldSceneState.m1CC_savedSceneSCRZ = fieldSceneState.m178_sceneSCRZ;
        fieldSceneState.m1D0_savedSceneDIP = fieldSceneState.m17C_sceneDIP;
        pCurrentFieldScriptActor->mCC_scriptPC += 1;
    }
}

void OP_9F(void) {
    if (fieldSceneState.m176_cameraDollyVar0 == 0) {
        OP_B5Sub(fieldSceneState.m1C8_savedCameraDirection, 0x20);
        OP_B6Sub(fieldSceneState.m1CC_savedSceneSCRZ, 0x20);
        OP_A4Sub(fieldSceneState.m1D0_savedSceneDIP, 0x20);
        pCurrentFieldScriptActor->mCC_scriptPC += 1;
    }
}

void OP_SET_SCREEN_GEOMETRY(void) {
    s32 d;
    s16 a;

    fieldSceneState.m17C_sceneDIP = getImmediateOrVariableUnsigned(3);
    d = (getImmediateOrVariableUnsigned(1) + 4) & 7;
    a = d << 9;
    fieldSceneState.m18C_opA0_var0 = a;
    fieldSceneState.m164_cameraRotationBetweenEyeAndAt.vy = d << 9;
    fieldSceneState.m170_opA0_var1 = a << 16;
    fieldSceneState.m178_sceneSCRZ = getImmediateOrVariableUnsigned(5);
    SetGeomScreen(fieldSceneState.m178_sceneSCRZ);
    pCurrentFieldScriptActor->mCC_scriptPC += 7;
}
