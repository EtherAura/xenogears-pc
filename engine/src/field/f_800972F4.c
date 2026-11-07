
#include "common.h"
#include "psyq.h"
#include "main/sprite.h"
#include "main/debug_font.h"
#include "field/field.h"

#define FP_INT(x) (((s16*)&(x))[1])

extern FieldBss_800B2184 op9DVar1;
extern sFieldEntity* pCurrentFieldEntity;
void setVisualAnimation(SpriteActorCore* actor, s32 animation, sFieldEntity* entity);
void updateEntityEventCode3Sub1(SpriteActorCore* actor, s32 rotation, sFieldEntity* pEntity);
extern s32 g_breakCurrentScript;
extern s32 g_playerControlledActor;
extern s32 fieldDebugDisable;
extern FieldBss_800AF880 fieldSceneState;
void resetRGBFaderToBlack(s32 index);
void setupRGBFaderSlot0_fadeIn(s32 steps);
void setupRGBCalcSlot0_fadeToBlack(s32 steps);
s32 loadNewField(s32, s32);
s32 spriteWalkToPositionOrActor3d(s32 numSteps);
s32 spriteWalkToPositionOrActor(s32 numSteps);                   /* Noah: spriteWalkToPositionOrActor? */
s32 length2d(s32 x, s32 y);
s32 length1d(s32 x);
s32 distance3d(s32 x, s32 y, s32 z);
s32 stepInterpolateDirectionSub(s32, s32, s32);
void OP_10Sub(s32 mode);
s32 getAngleCos(s32 angle);
s32 getAngleSin(s32 angle);

void OP_C3(void) {
    g_breakCurrentScript = 1;
    pCurrentFieldScriptActor->mCC_scriptPC += 1;
}

void OP_START_FADE_IN(void) {
    resetRGBFaderToBlack(0);
    setupRGBFaderSlot0_fadeIn(getImmediateOrVariableUnsigned(1));
    pCurrentFieldScriptActor->mCC_scriptPC += 3;
}

void OP_START_FADE_TO_BLACK(void) {
    setupRGBCalcSlot0_fadeToBlack(getImmediateOrVariableUnsigned(1));
    pCurrentFieldScriptActor->mCC_scriptPC += 3;
}

void OP_78(void) {
    if (loadNewField(readU16FromScript(2), pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 1]) == 0) {
        pCurrentFieldScriptActor->mCC_scriptPC += 4;
    }
}

/* Skips a table of 3-byte entries (count read from the script). */
void OP_A6(void) {
    pCurrentFieldScriptActor->mCC_scriptPC += getImmediateOrVariableUnsigned(1) * 3 + 3;
}

/* Noah: getPlayerCharacterDir (8-way direction of the player actor) */
u16 getPlayerCharacterDir(void) {
    return ((fieldSceneState.m290_actorArray[g_playerControlledActor].m4C_scriptEntity->m106_currentRotation + 0x100) >> 9) + 2 & 7;
}

/* Opcode 0x55: walk towards another actor's position (walk mode 2). */
void OP_55(void) {
    sFieldScriptEntity* p = pCurrentFieldScriptActor;
    sFieldScriptEntity* other;

    p->m8C_scriptSlots[p->mCE_currentScriptSlot].m4_x17_walkMode = 2;
    other = fieldSceneState.m290_actorArray[pCurrentFieldScriptFile[p->mCC_scriptPC + 1]].m4C_scriptEntity;
    p->mD0_targetPositionOffset.vx = FP_INT(other->m20_position.vx);
    p->mD0_targetPositionOffset.vz = FP_INT(other->m20_position.vz);
    p->mD0_targetPositionOffset.vy = FP_INT(other->m20_position.vy);
    if (p->m8C_scriptSlots[p->mCE_currentScriptSlot].m4_x0_numSteps == 0xFFFF) {
        pCurrentFieldScriptActor->m8C_scriptSlots[pCurrentFieldScriptActor->mCE_currentScriptSlot].m4_x0_numSteps = getImmediateOrVariableUnsigned(5);
    }
    if (spriteWalkToPositionOrActor3d(getImmediateOrVariableUnsigned(5)) == 0) {
        pCurrentFieldScriptActor->mCC_scriptPC += 7;
    }
}

void OP_54(void) {
    sFieldScriptEntity* p = pCurrentFieldScriptActor;
    sFieldScriptEntity* other;

    p->m8C_scriptSlots[p->mCE_currentScriptSlot].m4_x17_walkMode = 2;
    other = fieldSceneState.m290_actorArray[pCurrentFieldScriptFile[p->mCC_scriptPC + 1]].m4C_scriptEntity;
    p->mD0_targetPositionOffset.vx = FP_INT(other->m20_position.vx);
    p->mD0_targetPositionOffset.vz = FP_INT(other->m20_position.vz);
    p->mD0_targetPositionOffset.vy = FP_INT(other->m20_position.vy);
    p->m8C_scriptSlots[p->mCE_currentScriptSlot].m4_x0_numSteps = 0xFFFF;
    if (spriteWalkToPositionOrActor3d(0xFFFF) == 0) {
        pCurrentFieldScriptActor->mCC_scriptPC += 5;
    }
}

void OP_51(void) {
    sFieldScriptEntity* p = pCurrentFieldScriptActor;

    if (p->m8C_scriptSlots[p->mCE_currentScriptSlot].m4_x17_walkMode == 0) {
        p->m8C_scriptSlots[p->mCE_currentScriptSlot].m4_x17_walkMode = 1;
        p->mD0_targetPositionOffset.vx = FP_INT(p->m20_position.vx);
        p->mD0_targetPositionOffset.vy = FP_INT(p->m20_position.vy);
        p->mD0_targetPositionOffset.vz = FP_INT(p->m20_position.vz);
    }
    if (pCurrentFieldScriptActor->m8C_scriptSlots[pCurrentFieldScriptActor->mCE_currentScriptSlot].m4_x0_numSteps == 0xFFFF) {
        pCurrentFieldScriptActor->m8C_scriptSlots[pCurrentFieldScriptActor->mCE_currentScriptSlot].m4_x0_numSteps = getImmediateOrVariableUnsigned(8);
    }
    if (spriteWalkToPositionOrActor3d(getImmediateOrVariableUnsigned(8)) == 0) {
        pCurrentFieldScriptActor->mCC_scriptPC += 10;
    }
}

void OP_50(void) {
    sFieldScriptEntity* p = pCurrentFieldScriptActor;

    if (p->m8C_scriptSlots[p->mCE_currentScriptSlot].m4_x17_walkMode == 0) {
        p->m8C_scriptSlots[p->mCE_currentScriptSlot].m4_x17_walkMode = 1;
        p->mD0_targetPositionOffset.vx = FP_INT(p->m20_position.vx);
        p->mD0_targetPositionOffset.vy = FP_INT(p->m20_position.vy);
        p->mD0_targetPositionOffset.vz = FP_INT(p->m20_position.vz);
    }
    pCurrentFieldScriptActor->m8C_scriptSlots[pCurrentFieldScriptActor->mCE_currentScriptSlot].m4_x0_numSteps = 0xFFFF;
    if (spriteWalkToPositionOrActor3d(0xFFFF) == 0) {
        pCurrentFieldScriptActor->mCC_scriptPC += 8;
    }
}

/* Noah: OP_44 (walk mode 3: towards an angle from the current position) */
void OP_45(void) {
    sFieldScriptEntity* p = pCurrentFieldScriptActor;

    if (p->m8C_scriptSlots[p->mCE_currentScriptSlot].m4_x17_walkMode == 0) {
        p->m8C_scriptSlots[p->mCE_currentScriptSlot].m4_x17_walkMode = 3;
        p->mD0_targetPositionOffset.vx = FP_INT(p->m20_position.vx);
        p->mD0_targetPositionOffset.vy = FP_INT(p->m20_position.vy);
        p->mD0_targetPositionOffset.vz = FP_INT(p->m20_position.vz);
    }
    if (pCurrentFieldScriptActor->m8C_scriptSlots[pCurrentFieldScriptActor->mCE_currentScriptSlot].m4_x0_numSteps == 0xFFFF) {
        pCurrentFieldScriptActor->m8C_scriptSlots[pCurrentFieldScriptActor->mCE_currentScriptSlot].m4_x0_numSteps = getImmediateOrVariableUnsigned(5);
    }
    if (spriteWalkToPositionOrActor3d(getImmediateOrVariableUnsigned(5)) == 0) {
        pCurrentFieldScriptActor->mCC_scriptPC += 8;
    }
}

void OP_4D(void) {
    if (pCurrentFieldScriptActor->m8C_scriptSlots[pCurrentFieldScriptActor->mCE_currentScriptSlot].m4_x0_numSteps == 0xFFFF) {
        pCurrentFieldScriptActor->m8C_scriptSlots[pCurrentFieldScriptActor->mCE_currentScriptSlot].m4_x0_numSteps = getImmediateOrVariableUnsigned(8);
    }
    if (spriteWalkToPositionOrActor3d(getImmediateOrVariableUnsigned(8)) == 0) {
        pCurrentFieldScriptActor->mCC_scriptPC += 10;
    }
}

void OP_4C(void) {
    pCurrentFieldScriptActor->m8C_scriptSlots[pCurrentFieldScriptActor->mCE_currentScriptSlot].m4_x0_numSteps = 0xFFFF;
    if (spriteWalkToPositionOrActor3d(0xFFFF) == 0) {
        pCurrentFieldScriptActor->mCC_scriptPC += 8;
    }
}

/* Noah: spriteWalkToPositionOrActor, variant that also moves along y
 * (m40 = normalised step towards the destination). */
s32 spriteWalkToPositionOrActor3d(s32 numSteps) {
    VECTOR unused;
    VECTOR delta;
    VECTOR normal;
    VECTOR step;
    SpriteActorCore* sprite;
    s32 destX;
    s32 destY;
    s32 destZ;
    s32 curX;
    s32 curY;
    s32 curZ;
    s32 stepLength;
    s32 radius;
    s32 noRotation;
    s32 dist;
    s32 scale;
    s32 angle;
    u16 rotation;
    sFieldScriptEntity* p;
    u32 flags;

    noRotation = -1;
    destY = 0;
    destZ = 0;
    sprite = fieldSceneState.m290_actorArray[currentFieldActorId].m4_pVramSpriteSheet;
    destX = 0;
    if (fieldSceneState.m290_actorArray[currentFieldActorId].m4C_scriptEntity->m4_flags & 0x2000) {
        sprite->m18_moveSpeed = 0x8000000 / pCurrentFieldScriptActor->m76_timeScale /* base: the sprite scales it */;
    } else {
        sprite->m18_moveSpeed = 0x4000000 / pCurrentFieldScriptActor->m76_timeScale /* base: the sprite scales it */;
    }
    stepLength = length1d(sprite->m18_moveSpeed >> 15) + 1;
    radius = 0;
    switch (pCurrentFieldScriptActor->m8C_scriptSlots[pCurrentFieldScriptActor->mCE_currentScriptSlot].m4_x17_walkMode) {
    case 0:
        destX = getVar80(1, pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 5]);
        destZ = getVar40(3, pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 5]);
        destY = getVar20(6, pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 5]);
        break;
    case 1:
        destX = getVar80(1, pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 5]) + pCurrentFieldScriptActor->mD0_targetPositionOffset.vx;
        destZ = getVar40(3, pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 5]) + pCurrentFieldScriptActor->mD0_targetPositionOffset.vz;
        destY = pCurrentFieldScriptActor->mD0_targetPositionOffset.vy + getVar20(6, pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 5]);
        break;
    case 2:
        if (readCharacter(1) == 0xFF) {
            return 0;
        }
        radius = length1d(fieldSceneState.m290_actorArray[readCharacter(1)].m4C_scriptEntity->m1E_collisionRadius + pCurrentFieldScriptActor->m1E_collisionRadius);
        destX = pCurrentFieldScriptActor->mD0_targetPositionOffset.vx;
        destZ = pCurrentFieldScriptActor->mD0_targetPositionOffset.vz;
        destY = getVar80(2, pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 4]);
        break;
    case 3:
        angle = getImmediateOrVariableUnsigned(1) & 0xFFF;
        destX = (pCurrentFieldScriptActor->mD0_targetPositionOffset.vx + (getAngleSin(angle) << 5)) >> 12;
        destZ = pCurrentFieldScriptActor->mD0_targetPositionOffset.vz + (-(getAngleCos(angle) << 5) >> 12);
        destY = pCurrentFieldScriptActor->mD0_targetPositionOffset.vy + getVar80(3, pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 7]);
        break;
    }
    curX = FP_INT(pCurrentFieldScriptActor->m20_position.vx);
    curZ = FP_INT(pCurrentFieldScriptActor->m20_position.vz);
    curY = FP_INT(pCurrentFieldScriptActor->m20_position.vy);
    delta.vx = curX - destX;
    delta.vy = curY - destY;
    delta.vz = curZ - destZ;
    VectorNormal(&delta, &normal);
    scale = sprite->m18_moveSpeed >> 8;
    step.vx = -(normal.vx * scale >> 4);
    step.vy = -(normal.vy * scale >> 4);
    step.vz = -(normal.vz * scale >> 4);
    pCurrentFieldScriptActor->m40_externalStep.vx = step.vx;
    pCurrentFieldScriptActor->m40_externalStep.vy = step.vy;
    pCurrentFieldScriptActor->m40_externalStep.vz = step.vz;
    pCurrentFieldScriptActor->m40_externalStep.vy = 0;
    dist = distance3d(destX - curX, destY - curY, destZ - curZ);
    if (FP_INT(pCurrentFieldScriptActor->m40_externalStep.vx) == 0 && FP_INT(pCurrentFieldScriptActor->m40_externalStep.vz) == 0) {
        noRotation = 0;
    }
    p = pCurrentFieldScriptActor;
    flags = p->m0_fieldScriptFlags | 0x400000;
    p->m0_fieldScriptFlags = flags;
    if (p->m8C_scriptSlots[p->mCE_currentScriptSlot].m4_x0_numSteps == 0 || stepLength + radius >= dist) {
        if (noRotation == -1) {
            if (numSteps != 0) {
                if (!(flags & 0x8000)) {
                    rotation = p->m106_currentRotation | 0x8000;
                } else {
                    rotation = p->m11C_lockedRotation | 0x8000;
                }
                p->m104_rotation = rotation;
                p->m106_currentRotation = rotation;
            } else {
                rotation = fp_atan2(&step) | 0x8000;
                pCurrentFieldScriptActor->m104_rotation = rotation;
                pCurrentFieldScriptActor->m106_currentRotation = rotation;
            }
        }
        pCurrentFieldScriptActor->mEC_elevation = (pCurrentFieldScriptActor->m20_position.vy + step.vy) >> 16;
        pCurrentFieldScriptActor->m8C_scriptSlots[pCurrentFieldScriptActor->mCE_currentScriptSlot].m4_x17_walkMode = 0;
        pCurrentFieldScriptActor->m8C_scriptSlots[pCurrentFieldScriptActor->mCE_currentScriptSlot].m4_x0_numSteps = 0xFFFF;
        return 0;
    }
    if (noRotation == -1) {
        rotation = fp_atan2(&step) | 0x8000;
        pCurrentFieldScriptActor->m104_rotation = rotation;
        pCurrentFieldScriptActor->m106_currentRotation = rotation;
    }
    pCurrentFieldScriptActor->mEC_elevation = (pCurrentFieldScriptActor->m20_position.vy + step.vy) >> 16;
    pCurrentFieldScriptActor->m0_fieldScriptFlags |= 0x40000;
    pCurrentFieldScriptActor->m8C_scriptSlots[pCurrentFieldScriptActor->mCE_currentScriptSlot].m4_x0_numSteps--;
    g_breakCurrentScript = 1;
    return -1;
}

void OP_FOLLOW_CHARACTER(void) {
    sFieldScriptEntity* p = pCurrentFieldScriptActor;

    p->m8C_scriptSlots[p->mCE_currentScriptSlot].m4_x17_walkMode = 2;
    if (p->m8C_scriptSlots[p->mCE_currentScriptSlot].m4_x0_numSteps == 0xFFFF) {
        pCurrentFieldScriptActor->m8C_scriptSlots[pCurrentFieldScriptActor->mCE_currentScriptSlot].m4_x0_numSteps = getImmediateOrVariableUnsigned(2);
    }
    if (spriteWalkToPositionOrActor(getImmediateOrVariableUnsigned(2)) == 0) {
        pCurrentFieldScriptActor->mCC_scriptPC += 4;
    }
}

void OP_52(void) {
    sFieldScriptEntity* p = pCurrentFieldScriptActor;

    p->m8C_scriptSlots[p->mCE_currentScriptSlot].m4_x17_walkMode = 2;
    p->m8C_scriptSlots[p->mCE_currentScriptSlot].m4_x0_numSteps = 0xFFFF;
    if (spriteWalkToPositionOrActor(0xFFFF) == 0) {
        pCurrentFieldScriptActor->mCC_scriptPC += 2;
    }
}

void OP_44(void) {
    sFieldScriptEntity* p = pCurrentFieldScriptActor;

    if (p->m8C_scriptSlots[p->mCE_currentScriptSlot].m4_x17_walkMode == 0) {
        p->m8C_scriptSlots[p->mCE_currentScriptSlot].m4_x17_walkMode = 3;
        p->mD0_targetPositionOffset.vx = FP_INT(p->m20_position.vx);
        p->mD0_targetPositionOffset.vy = FP_INT(p->m20_position.vy);
        p->mD0_targetPositionOffset.vz = FP_INT(p->m20_position.vz);
    }
    if (pCurrentFieldScriptActor->m8C_scriptSlots[pCurrentFieldScriptActor->mCE_currentScriptSlot].m4_x0_numSteps == 0xFFFF) {
        pCurrentFieldScriptActor->m8C_scriptSlots[pCurrentFieldScriptActor->mCE_currentScriptSlot].m4_x0_numSteps = getImmediateOrVariableUnsigned(3);
    }
    if (spriteWalkToPositionOrActor(getImmediateOrVariableUnsigned(3)) == 0) {
        pCurrentFieldScriptActor->mCC_scriptPC += 5;
    }
}

void OP_4F(void) {
    sFieldScriptEntity* p = pCurrentFieldScriptActor;

    if (p->m8C_scriptSlots[p->mCE_currentScriptSlot].m4_x17_walkMode == 0) {
        p->m8C_scriptSlots[p->mCE_currentScriptSlot].m4_x17_walkMode = 1;
        p->mD0_targetPositionOffset.vx = FP_INT(p->m20_position.vx);
        p->mD0_targetPositionOffset.vy = FP_INT(p->m20_position.vy);
        p->mD0_targetPositionOffset.vz = FP_INT(p->m20_position.vz);
    }
    if (pCurrentFieldScriptActor->m8C_scriptSlots[pCurrentFieldScriptActor->mCE_currentScriptSlot].m4_x0_numSteps == 0xFFFF) {
        pCurrentFieldScriptActor->m8C_scriptSlots[pCurrentFieldScriptActor->mCE_currentScriptSlot].m4_x0_numSteps = getImmediateOrVariableUnsigned(6);
    }
    if (spriteWalkToPositionOrActor(getImmediateOrVariableUnsigned(6)) == 0) {
        pCurrentFieldScriptActor->mCC_scriptPC += 8;
    }
}

void OP_4E(void) {
    sFieldScriptEntity* p = pCurrentFieldScriptActor;

    if (p->m8C_scriptSlots[p->mCE_currentScriptSlot].m4_x17_walkMode == 0) {
        p->m8C_scriptSlots[p->mCE_currentScriptSlot].m4_x17_walkMode = 1;
        p->mD0_targetPositionOffset.vx = FP_INT(p->m20_position.vx);
        p->mD0_targetPositionOffset.vy = FP_INT(p->m20_position.vy);
        p->mD0_targetPositionOffset.vz = FP_INT(p->m20_position.vz);
    }
    pCurrentFieldScriptActor->m8C_scriptSlots[pCurrentFieldScriptActor->mCE_currentScriptSlot].m4_x0_numSteps = 0xFFFF;
    if (spriteWalkToPositionOrActor(0xFFFF) == 0) {
        pCurrentFieldScriptActor->mCC_scriptPC += 6;
    }
}

void OP_4B(void) {
    sFieldScriptEntity* p = pCurrentFieldScriptActor;

    p->m8C_scriptSlots[p->mCE_currentScriptSlot].m4_x17_walkMode = 0;
    if (p->m8C_scriptSlots[p->mCE_currentScriptSlot].m4_x0_numSteps == 0xFFFF) {
        pCurrentFieldScriptActor->m8C_scriptSlots[pCurrentFieldScriptActor->mCE_currentScriptSlot].m4_x0_numSteps = getImmediateOrVariableUnsigned(6);
    }
    if (spriteWalkToPositionOrActor(getImmediateOrVariableUnsigned(6)) == 0) {
        pCurrentFieldScriptActor->mCC_scriptPC += 8;
    }
}

/* Opcode EX 0x1D: fieldObjectRenderingVar2 (0x800B21AE) xyz from script, flag 0x80 at 0x800B21D2.
 * Written through the op9DVar1 aggregate: the byte store must count as a
 * struct store so the PC load is not hoisted above it. */
void OPX_1D(void) {
    op9DVar1.m2A_objectStepX = getVar80(1, pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 7]);
    op9DVar1.m2C_objectStepZ = getVar40(3, pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 7]);
    op9DVar1.m2E_objectStepY = getVar20(5, pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 7]);
    op9DVar1.m4E_objectMoveFlags |= 0x80;
    pCurrentFieldScriptActor->mCC_scriptPC += 8;
}

void OPX_DEBUG_PRINT_FLAG_VALUE(void) {
    if (fieldDebugDisable == 0) {
        s32 value = getVariable(readU16FromScript(1));
        sprintf_screen("DEB=%xh %d \n", value, value);
    }
    if (0) {
        sprintf_screen(""); /* retail keeps this unreferenced "" after the format string */
    }
    pCurrentFieldScriptActor->mCC_scriptPC += 3;
}

/* Opcode EX 0x73: var = 2D distance between two points */
void OPX_73(void) {
    s32 x0 = getVar40(3, pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 11]);
    s32 z0 = getVar20(5, pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 11]);
    s32 x1 = getVar10(7, pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 11]);
    s32 z1 = getVar08(9, pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 11]);
    s32 var = readU16FromScript(1);
    setVar(var, length2d(x1 - x0, z1 - z0));
    pCurrentFieldScriptActor->mCC_scriptPC += 12;
}

/* Opcode EX 0x76: var = 3D distance between two points (x, z, y each) */
void OPX_76(void) {
    s32 x0 = getVar40(3, pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 15]);
    s32 z0 = getVar20(5, pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 15]);
    s32 y0 = getVar20(7, pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 15]);
    s32 x1 = getVar10(9, pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 15]);
    s32 z1 = getVar08(11, pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 15]);
    s32 y1 = getVar08(13, pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 15]);
    s32 var = readU16FromScript(1);
    setVar(var, distance3d(x1 - x0, y1 - y0, z1 - z0));
    pCurrentFieldScriptActor->mCC_scriptPC += 16;
}

void OPX_72(void) {
    s32 a = getVar40(3, pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 9]);
    s32 b = getVar20(5, pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 9]);
    s32 result = stepInterpolateDirectionSub(a, b, getVar10(7, pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 9]));
    setVar(readU16FromScript(1), result);
    pCurrentFieldScriptActor->mCC_scriptPC += 10;
}

/* Opcode EX 0x71: var = current actor rotation (0..0xFFF) */
void OPX_71(void) {
    setVar(readU16FromScript(1), pCurrentFieldScriptActor->m106_currentRotation & 0xFFF);
    pCurrentFieldScriptActor->mCC_scriptPC += 3;
}

/* Opcode EX 0x75: var = rotation (0..0xFFF) of another actor */
void OPX_75(void) {
    s32 actor = readCharacter(1);
    if (actor != 0xFF) {
        sFieldScriptEntity* p = fieldSceneState.m290_actorArray[actor].m4C_scriptEntity;
        setVar(readU16FromScript(2), p->m106_currentRotation & 0xFFF);
    }
    pCurrentFieldScriptActor->mCC_scriptPC += 4;
}

/* Noah: OPX_1C (teleport the current actor to a script position) */
void OPX_1C(void) {
    SpriteActorCore* sprite = fieldSceneState.m290_actorArray[currentFieldActorId].m4_pVramSpriteSheet;
    sFieldScriptEntity* p;
    s32 i;

    pCurrentFieldScriptActor->m0_fieldScriptFlags |= 0x10000;
    pCurrentFieldScriptActor->m4_flags |= 0x200000;
    pCurrentFieldScriptActor->m20_position.vx = getVar80(1, pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 7]) << 16;
    pCurrentFieldScriptActor->m20_position.vz = getVar40(3, pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 7]) << 16;
    pCurrentFieldScriptActor->m20_position.vy = getVar20(5, pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 7]) << 16;
    p = pCurrentFieldScriptActor;
    i = currentFieldActorId;
    fieldSceneState.m290_actorArray[i].mC_matrix.t[0] = FP_INT(p->m20_position.vx);
    fieldSceneState.m290_actorArray[i].mC_matrix.t[1] = FP_INT(p->m20_position.vy);
    fieldSceneState.m290_actorArray[i].mC_matrix.t[2] = FP_INT(p->m20_position.vz);
    /* An actor without a sprite (field 396's actor 29, in its init script):
     * the PS1 writes the position over low BIOS RAM at 0..0xB (Redux). */
    if (sprite != NULL)
    {
        sprite->m0_position.vx = p->m20_position.vx;
        sprite->m0_position.vy = p->m20_position.vy;
        sprite->m0_position.vz = p->m20_position.vz;
    }
    p->mCC_scriptPC += 8;
}

void OP_10(void) {
    pCurrentFieldScriptActor->m8C_scriptSlots[pCurrentFieldScriptActor->mCE_currentScriptSlot].m4_x0_numSteps = 0xFFFF;
    OP_10Sub(0);
}

void OP_11(void) {
    if (pCurrentFieldScriptActor->m8C_scriptSlots[pCurrentFieldScriptActor->mCE_currentScriptSlot].m4_x0_numSteps == 0xFFFF) {
        pCurrentFieldScriptActor->m8C_scriptSlots[pCurrentFieldScriptActor->mCE_currentScriptSlot].m4_x0_numSteps = getImmediateOrVariableUnsigned(0xB);
    }
    OP_10Sub(1);
}

/* Noah: OP_10Sub (move the current actor in a straight line to a script
 * position over m102 steps; mode 1 = OP_11 with 2 extra bytes). */
void OP_10Sub(s32 mode) {
    VECTOR currentPosition;
    SpriteActorCore* sprite;
    sFieldScriptEntity* p;
    s32 walkSpeed;
    s32 x;
    s32 y;
    s32 z;
    s32 animation;
    s32 i;
    s16 rotation;

    sprite = fieldSceneState.m290_actorArray[currentFieldActorId].m4_pVramSpriteSheet;
    if (fieldSceneState.m290_actorArray[currentFieldActorId].m4C_scriptEntity->m4_flags & 0x2000) {
        /* the walk-to budget in script ticks (phase 2: the scripts tick at the native cadence): base divisor */
        walkSpeed = (0x8000000 / pCurrentFieldScriptActor->m76_timeScale) >> 16;
    } else {
        walkSpeed = (0x4000000 / pCurrentFieldScriptActor->m76_timeScale) >> 16;
    }
    if (walkSpeed == 0) {
        walkSpeed = 1;
    }
    p = pCurrentFieldScriptActor;
    p->m0_fieldScriptFlags |= 0x10000;
    animation = 1;
    if (pCurrentFieldScriptFile[p->mCC_scriptPC + 1] == 0) {
        x = getVar80(2, pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 8]) << 16;
        z = getVar40(4, pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 8]) << 16;
        y = getVar20(6, pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 8]) << 16;
        pCurrentFieldScriptActor->m102_numSteps = distance3d((x - pCurrentFieldScriptActor->m20_position.vx) >> 16, (y - pCurrentFieldScriptActor->m20_position.vy) >> 16,
                                                  (z - pCurrentFieldScriptActor->m20_position.vz) >> 16) / walkSpeed;
        if (pCurrentFieldScriptActor->m102_numSteps == 0) {
            pCurrentFieldScriptActor->m102_numSteps++;
        }
        pCurrentFieldScriptActor->mD0_targetPositionOffset.vx = (x - pCurrentFieldScriptActor->m20_position.vx) / pCurrentFieldScriptActor->m102_numSteps;
        pCurrentFieldScriptActor->mD0_targetPositionOffset.vy = (y - pCurrentFieldScriptActor->m20_position.vy) / pCurrentFieldScriptActor->m102_numSteps;
        pCurrentFieldScriptActor->mD0_targetPositionOffset.vz = (z - pCurrentFieldScriptActor->m20_position.vz) / pCurrentFieldScriptActor->m102_numSteps;
        if ((x >> 16) != FP_INT(pCurrentFieldScriptActor->m20_position.vx) || (z >> 16) != FP_INT(pCurrentFieldScriptActor->m20_position.vz)) {
            rotation = -ratan2(pCurrentFieldScriptActor->mD0_targetPositionOffset.vz >> 16, FP_INT(pCurrentFieldScriptActor->mD0_targetPositionOffset.vx));
            pCurrentFieldScriptActor->m104_rotation = rotation;
            pCurrentFieldScriptActor->m106_currentRotation = rotation;
        }
        pCurrentFieldScriptActor->mCC_scriptPC += 9;
    } else {
        if (p->m102_numSteps <= 0 || p->m8C_scriptSlots[p->mCE_currentScriptSlot].m4_x0_numSteps == 0) {
            p->mCC_scriptPC -= 9;
            if (p->m8C_scriptSlots[p->mCE_currentScriptSlot].m4_x0_numSteps != 0) {
                currentPosition.vx = p->m20_position.vx;
                currentPosition.vy = p->m20_position.vy;
                currentPosition.vz = p->m20_position.vz;
                pCurrentFieldScriptActor->m20_position.vx = getVar80(2, pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 8]) << 16;
                pCurrentFieldScriptActor->m20_position.vz = getVar40(4, pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 8]) << 16;
                pCurrentFieldScriptActor->m20_position.vy = getVar20(6, pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 8]) << 16;
                pCurrentFieldScriptActor->m30_stepVector.vx = pCurrentFieldScriptActor->m20_position.vx - currentPosition.vx;
                pCurrentFieldScriptActor->m30_stepVector.vy = pCurrentFieldScriptActor->m20_position.vy - currentPosition.vy;
                pCurrentFieldScriptActor->m30_stepVector.vz = pCurrentFieldScriptActor->m20_position.vz - currentPosition.vz;
            }
            animation = pCurrentFieldScriptActor->mE6_idleAnimationId;
            if (mode == 0) {
                pCurrentFieldScriptActor->mCC_scriptPC += 11;
            } else {
                pCurrentFieldScriptActor->mCC_scriptPC += 13;
            }
            pCurrentFieldScriptActor->m8C_scriptSlots[pCurrentFieldScriptActor->mCE_currentScriptSlot].m4_x0_numSteps = 0xFFFF;
        } else {
            p->m20_position.vx += p->mD0_targetPositionOffset.vx;
            p->m20_position.vz += p->mD0_targetPositionOffset.vz;
            p->m20_position.vy += p->mD0_targetPositionOffset.vy;
            p->m30_stepVector.vx = p->mD0_targetPositionOffset.vx;
            p->m30_stepVector.vy = p->mD0_targetPositionOffset.vy;
            p->m30_stepVector.vz = p->mD0_targetPositionOffset.vz;
            p->m8C_scriptSlots[p->mCE_currentScriptSlot].m4_x0_numSteps--;
            g_breakCurrentScript = 1;
        }
        {
            sFieldScriptEntity* q = pCurrentFieldScriptActor;
            i = currentFieldActorId;
            q->m102_numSteps--;
            fieldSceneState.m290_actorArray[i].mC_matrix.t[0] = FP_INT(q->m20_position.vx);
            fieldSceneState.m290_actorArray[i].mC_matrix.t[1] = FP_INT(q->m20_position.vy);
            fieldSceneState.m290_actorArray[i].mC_matrix.t[2] = FP_INT(q->m20_position.vz);
            sprite->m0_position.vx = q->m20_position.vx;
            sprite->m0_position.vy = q->m20_position.vy;
            sprite->m0_position.vz = q->m20_position.vz;
        }
    }
    if (pCurrentFieldScriptActor->mEA_forcedAnimation != 0xFF) {
        animation = pCurrentFieldScriptActor->mEA_forcedAnimation;
    }
    if (pCurrentFieldScriptActor->mE8_currentAnimationId != animation && !(pCurrentFieldScriptActor->m0_fieldScriptFlags & 0x2000000)) {
        pCurrentFieldScriptActor->mE8_currentAnimationId = animation;
        setVisualAnimation(sprite, animation, pCurrentFieldEntity);
    }
    updateEntityEventCode3Sub1(sprite, pCurrentFieldScriptActor->m104_rotation, pCurrentFieldEntity);
}

/* Noah: OP_57 (jump/arc to a script position; mode = script[PC+1] & 3) */
void OP_57(void) {
    VECTOR normals[4];
    SVECTOR positions[8];
    SpriteActorCore* sprite;
    s32 numSteps;
    s32 z;
    s32 x;
    s32 y;
    s32 walkmesh;
    s32 h;
    s32 temp;

    sprite = fieldSceneState.m290_actorArray[currentFieldActorId].m4_pVramSpriteSheet;
    pCurrentFieldScriptActor->m0_fieldScriptFlags |= 0x10000;
    switch (pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 1] & 3) {
    case 0:
        numSteps = getVar10(8, pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 10]);
    block:
        if (numSteps == 0) {
            numSteps = 1;
        }
        x = getVar80(2, pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 10]);
        z = getVar40(4, pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 10]);
        if (!(pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 1] & 0x80)) {
            y = getVar20(6, pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 10]);
        } else {
            walkmesh = getVar20(6, pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 10]);
            findTriangleInWalkMesh(x, z, walkmesh, &positions[walkmesh], &normals[walkmesh]);
            y = positions[walkmesh].vy;
            pCurrentFieldScriptActor->m10_walkmeshId = walkmesh;
        }
        sprite->mC_step.vy = -(sprite->m1C_gravity * numSteps / 2);
        sprite->mC_step.vy += R3000_DIV((y << 16) - pCurrentFieldScriptActor->m20_position.vy, numSteps);
        pCurrentFieldScriptActor->mD0_targetPositionOffset.vy = 0;
        pCurrentFieldScriptActor->mE0_rotationLimit = numSteps;
        pCurrentFieldScriptActor->m102_numSteps = 0;
        pCurrentFieldScriptActor->mCC_scriptPC += 11;
        pCurrentFieldScriptActor->mD0_targetPositionOffset.vx = ((x << 16) - pCurrentFieldScriptActor->m20_position.vx) / (numSteps + 1);
        pCurrentFieldScriptActor->mD0_targetPositionOffset.vz = ((z << 16) - pCurrentFieldScriptActor->m20_position.vz) / (numSteps + 1);
        break;
    case 1:
        x = getVar80(2, pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 10]);
        z = getVar40(4, pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 10]);
        x = (x << 16) - pCurrentFieldScriptActor->m20_position.vx;
        z = (z << 16) - pCurrentFieldScriptActor->m20_position.vz;
        numSteps = getVar10(8, pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 10]);
        numSteps = R3000_DIV(length2d(x >> 16, z >> 16), numSteps);
        goto block;
    case 2:
        x = getVar80(2, pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 10]);
        z = getVar40(4, pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 10]);
        y = getVar20(6, pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 10]);
        h = -getVar10(8, pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 10]);
        y = (y << 16) - pCurrentFieldScriptActor->m20_position.vy;
        sprite->mC_step.vy = -(SquareRoot0(FP_INT(sprite->m1C_gravity) * (h << 1)) << 16);
        numSteps = SquareRoot0(h);
        temp = h - (y >> 16);
        numSteps = SquareRoot0(temp < 0 ? -temp : temp);
        if (numSteps < 0) {
            numSteps = -numSteps;
        }
        goto block;
    case 3:
        if (pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 1] == 0xF) {
            for (walkmesh = 0; walkmesh < fieldSceneState.m2D4_numWalkMesh - 1; walkmesh++) {
                pCurrentFieldScriptActor->m8_currentWalkMeshTriangle[walkmesh] = findTriangleInWalkMesh(FP_INT(pCurrentFieldScriptActor->m20_position.vx), FP_INT(pCurrentFieldScriptActor->m20_position.vz), walkmesh, &positions[walkmesh], &normals[walkmesh]);
            }
            pCurrentFieldScriptActor->m0_fieldScriptFlags &= ~0x10000;
            pCurrentFieldScriptActor->m4_flags &= ~0x200000;
            pCurrentFieldScriptActor->mCC_scriptPC += 2;
        } else {
            if (pCurrentFieldScriptActor->m102_numSteps < pCurrentFieldScriptActor->mE0_rotationLimit) {
                pCurrentFieldScriptActor->m20_position.vx += pCurrentFieldScriptActor->mD0_targetPositionOffset.vx;
                pCurrentFieldScriptActor->m20_position.vz += pCurrentFieldScriptActor->mD0_targetPositionOffset.vz;
                pCurrentFieldScriptActor->m20_position.vy += sprite->mC_step.vy;
                sprite->mC_step.vy += sprite->m1C_gravity;
                if ((pCurrentFieldScriptActor->mD0_targetPositionOffset.vx != 0 || pCurrentFieldScriptActor->mD0_targetPositionOffset.vz != 0) && !(pCurrentFieldScriptActor->m0_fieldScriptFlags & 0x8000)) {
                    temp = fp_atan2(&pCurrentFieldScriptActor->mD0_targetPositionOffset) | 0x8000;
                    pCurrentFieldScriptActor->m104_rotation = temp;
                    pCurrentFieldScriptActor->m106_currentRotation = temp;
                }
            } else {
                pCurrentFieldScriptActor->mCC_scriptPC -= 11;
                x = getVar80(2, pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 10]);
                z = getVar40(4, pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 10]);
                if (pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 1] & 0x80) {
                    walkmesh = getVar20(6, pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 10]);
                    pCurrentFieldScriptActor->m8_currentWalkMeshTriangle[walkmesh] = findTriangleInWalkMesh(x, z, walkmesh, &positions[walkmesh], &normals[walkmesh]);
                    y = positions[walkmesh].vy;
                } else {
                    y = getVar20(6, pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 10]);
                }
                sprite->mC_step.vy = 0;
                pCurrentFieldScriptActor->m20_position.vx = x << 16;
                pCurrentFieldScriptActor->m20_position.vy = y << 16;
                pCurrentFieldScriptActor->m20_position.vz = z << 16;
                pCurrentFieldScriptActor->m0_fieldScriptFlags &= ~0x10000;
                pCurrentFieldScriptActor->m4_flags &= ~0x200000;
                pCurrentFieldScriptActor->mCC_scriptPC += 13;
            }
            fieldSceneState.m290_actorArray[currentFieldActorId].mC_matrix.t[0] = FP_INT(pCurrentFieldScriptActor->m20_position.vx);
            fieldSceneState.m290_actorArray[currentFieldActorId].mC_matrix.t[1] = FP_INT(pCurrentFieldScriptActor->m20_position.vy);
            fieldSceneState.m290_actorArray[currentFieldActorId].mC_matrix.t[2] = FP_INT(pCurrentFieldScriptActor->m20_position.vz);
            sprite->m0_position.vx = pCurrentFieldScriptActor->m20_position.vx;
            sprite->m0_position.vy = pCurrentFieldScriptActor->m20_position.vy;
            sprite->m0_position.vz = pCurrentFieldScriptActor->m20_position.vz;
            pCurrentFieldScriptActor->m102_numSteps++;
        }
        break;
    }
    g_breakCurrentScript = 1;
}

void OP_SPRITE_WALK_TO_POSITION_AND_WAIT(void) {
    pCurrentFieldScriptActor->m8C_scriptSlots[pCurrentFieldScriptActor->mCE_currentScriptSlot].m4_x17_walkMode = 0;
    pCurrentFieldScriptActor->m8C_scriptSlots[pCurrentFieldScriptActor->mCE_currentScriptSlot].m4_x0_numSteps = 0xFFFF;
    if (spriteWalkToPositionOrActor(0xFFFF) == 0) {
        pCurrentFieldScriptActor->mCC_scriptPC += 6;
    }
}

s32 distance3d(s32 x, s32 y, s32 z) {
    VECTOR in;
    VECTOR out;

    in.vx = x;
    in.vy = y;
    in.vz = z;
    Square0(&in, &out);
    return SquareRoot0(out.vx + out.vy + out.vz);
}

s32 length2d(s32 x, s32 y) {
    VECTOR in;
    VECTOR out;

    in.vx = x;
    in.vy = y;
    in.vz = 0;
    Square0(&in, &out);
    return SquareRoot0(out.vx + out.vy);
}

s32 length1d(s32 x) {
    VECTOR in;
    VECTOR out;

    in.vx = x;
    Square0(&in, &out);
    return SquareRoot0(out.vx);
}
