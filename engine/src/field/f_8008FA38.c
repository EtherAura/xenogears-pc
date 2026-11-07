
#include "common.h"
#include "psyq.h"
#include "field/field.h"

#define FP_INT(x) (((s16*)&(x))[1])

/* The camera globals live in the 0x800AF880 field block (FieldBss_800AF880,
 * see include/field/field.h copy): retail treats them as struct members
 * (stores to them are not moved past script-PC loads). */
extern FieldBss_800AF880 fieldSceneState;
extern s32 g_breakCurrentScript;
extern FieldBss_800B2184 op9DVar1;
s32 getCharacterToEntity(s32 offset);
void createRotationMatrix(SVECTOR* rot, MATRIX* m);
s32 distance3d(s32 x, s32 y, s32 z);
s32 length2d(s32 x, s32 y);
s32 getAngleSin(s32 angle);
s32 getAngleCos(s32 angle);
void computeOrbit(s32* position, s32* center, s32 rotation);

void OP_WAIT_CAMERA(void) {
    s32 mask = getImmediateOrVariableUnsigned(1);
    s32 flags = 3;
    if (fieldSceneState.mBE_cameraInterpolationTargetNumSteps == 0) {
        flags = 2;
    }
    if (fieldSceneState.mE0_cameraInterpolationPositionNumSteps == 0) {
        flags &= 1;
    }
    g_breakCurrentScript = 1;
    if ((flags & mask) == 0) {
        pCurrentFieldScriptActor->mCC_scriptPC += 3;
    }
}

void OPX_6E(void) {
    fieldSceneState.m18C_opA0_var0 = fieldSceneState.m164_cameraRotationBetweenEyeAndAt.vy = getVar80(1, pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 3]);
    pCurrentFieldScriptActor->mCC_scriptPC += 4;
}

void OPX_6D(void) {
    fieldSceneState.mB0_op99Var1 = fieldSceneState.m178_sceneSCRZ * fieldSceneState.m17E_sceneScale >> 12;
    fieldSceneState.mB8_op99Var3 = fieldSceneState.m164_cameraRotationBetweenEyeAndAt.vy;
    fieldSceneState.mB6_op99Var2 = fieldSceneState.m17C_sceneDIP;
    fieldSceneState.mBA_op99Var4 = 0x1000;
    pCurrentFieldScriptActor->mCC_scriptPC += 1;
}

void OP_99(void) {
    fieldSceneState.mB4_cameraInterpolationMode = 1;
    fieldExectuteMaxCycles += 4;
    pCurrentFieldScriptActor->mCC_scriptPC += 1;
    fieldSceneState.mB0_op99Var1 = fieldSceneState.m178_sceneSCRZ * fieldSceneState.m17E_sceneScale >> 12;
    fieldSceneState.mBA_op99Var4 = 0x1000;
    fieldSceneState.m104_op99Var5 = 0xC;
    fieldSceneState.m108_op99Var6 = 0xC;
    fieldSceneState.m158_op99Var7 |= 0x8000;
    fieldSceneState.mB8_op99Var3 = fieldSceneState.m164_cameraRotationBetweenEyeAndAt.vy;
    fieldSceneState.mB6_op99Var2 = fieldSceneState.m17C_sceneDIP;
}

void OP_9A(void) {
    s32 steps;

    if (fieldSceneState.mB4_cameraInterpolationMode != 1) {
        if (fieldSceneState.mB4_cameraInterpolationMode >= 2) {
            return;
        }
        if (fieldSceneState.mB4_cameraInterpolationMode != 0) {
            return;
        }
        fieldSceneState.m158_op99Var7 &= 0x7FFF;
        pCurrentFieldScriptActor->mCC_scriptPC += 3;
    } else {
        steps = getImmediateOrVariableUnsigned(1);
        if (steps == 0) {
            fieldSceneState.mB4_cameraInterpolationMode = 0;
            fieldSceneState.m158_op99Var7 &= 0x7FFF;
            pCurrentFieldScriptActor->mCC_scriptPC += 3;
            op9DVar1.m54_cameraSnapFrames = 2;
        } else {
            fieldSceneState.mB4_cameraInterpolationMode = 2;
            fieldSceneState.m104_op99Var5 = steps;
            fieldSceneState.m108_op99Var6 = steps;
        }
        pCurrentFieldScriptActor->mCC_scriptPC += 3;
    }
}

void OP_SET_CAMERA_INTERPOLATION_RATE(void) {
    fieldSceneState.m104_op99Var5 = getImmediateOrVariableUnsigned(1);
    fieldSceneState.m108_op99Var6 = getImmediateOrVariableUnsigned(3);
    if (fieldSceneState.m104_op99Var5 == 0) {
        fieldSceneState.m104_op99Var5 = 1;
    }
    if (fieldSceneState.m108_op99Var6 == 0) {
        fieldSceneState.m108_op99Var6 = 1;
    }
    fieldExectuteMaxCycles++;
    pCurrentFieldScriptActor->mCC_scriptPC += 5;
}

void OP_60(void) {
    fieldSceneState.m70_cameraTargetOverride[0] = fieldSceneState.m40_cameraAt2.vx;
    fieldSceneState.m70_cameraTargetOverride[1] = fieldSceneState.m40_cameraAt2.vy;
    fieldSceneState.m70_cameraTargetOverride[2] = fieldSceneState.m40_cameraAt2.vz;
    fieldExectuteMaxCycles++;
    pCurrentFieldScriptActor->mCC_scriptPC += 1;
}

void OP_SET_CAMERA_TARGET_OVERRIDE(void) {
    fieldSceneState.m70_cameraTargetOverride[0] = getVar80(1, pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 7]) << 16;
    fieldSceneState.m70_cameraTargetOverride[2] = getVar40(3, pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 7]) << 16;
    fieldSceneState.m70_cameraTargetOverride[1] = getVar20(5, pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 7]) << 16;
    fieldExectuteMaxCycles++;
    pCurrentFieldScriptActor->mCC_scriptPC += 8;
}

void OP_SET_DESIRED_CAMERA_TARGET_TO_ENTITY(void) {
    sFieldScriptEntity* entity = fieldSceneState.m290_actorArray[getCharacterToEntity(1)].m4C_scriptEntity;
    fieldSceneState.m80_desiredCameraTarget[0] = entity->m20_position.vx;
    fieldSceneState.m80_desiredCameraTarget[1] = entity->m20_position.vy;
    fieldSceneState.m80_desiredCameraTarget[2] = entity->m20_position.vz;
    fieldExectuteMaxCycles++;
    pCurrentFieldScriptActor->mCC_scriptPC += 2;
}

void OP_SET_DESIRED_CAMERA_TARGET(void) {
    fieldSceneState.m80_desiredCameraTarget[0] = getVar80(1, pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 7]) << 16;
    fieldSceneState.m80_desiredCameraTarget[2] = getVar40(3, pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 7]) << 16;
    fieldSceneState.m80_desiredCameraTarget[1] = getVar20(5, pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 7]) << 16;
    fieldExectuteMaxCycles++;
    pCurrentFieldScriptActor->mCC_scriptPC += 8;
}

void OP_64(void) {
    fieldSceneState.m90_cameraPositionOverride[0] = fieldSceneState.m30_cameraEye2.vx;
    fieldSceneState.m90_cameraPositionOverride[1] = fieldSceneState.m30_cameraEye2.vy;
    fieldSceneState.m90_cameraPositionOverride[2] = fieldSceneState.m30_cameraEye2.vz;
    fieldExectuteMaxCycles++;
    pCurrentFieldScriptActor->mCC_scriptPC += 1;
}

void OP_SET_CAMERA_POSITION_OVERRIDE(void) {
    fieldSceneState.m90_cameraPositionOverride[0] = getVar80(1, pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 7]) << 16;
    fieldSceneState.m90_cameraPositionOverride[2] = getVar40(3, pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 7]) << 16;
    fieldSceneState.m90_cameraPositionOverride[1] = getVar20(5, pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 7]) << 16;
    fieldExectuteMaxCycles++;
    pCurrentFieldScriptActor->mCC_scriptPC += 8;
}

void OP_66(void) {
    sFieldScriptEntity* entity = fieldSceneState.m290_actorArray[getCharacterToEntity(1)].m4C_scriptEntity;
    fieldSceneState.mA0_desiredCameraPosition[0] = entity->m20_position.vx;
    fieldSceneState.mA0_desiredCameraPosition[1] = entity->m20_position.vy;
    fieldSceneState.mA0_desiredCameraPosition[2] = entity->m20_position.vz;
    fieldExectuteMaxCycles++;
    pCurrentFieldScriptActor->mCC_scriptPC += 2;
}

void OP_SET_DESIRED_CAMERA_POSITION(void) {
    fieldSceneState.mA0_desiredCameraPosition[0] = getVar80(1, pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 7]) << 16;
    fieldSceneState.mA0_desiredCameraPosition[2] = getVar40(3, pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 7]) << 16;
    fieldSceneState.mA0_desiredCameraPosition[1] = getVar20(5, pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 7]) << 16;
    fieldExectuteMaxCycles++;
    pCurrentFieldScriptActor->mCC_scriptPC += 8;
}

void OP_AB(void) {
    fieldSceneState.m80_desiredCameraTarget[0] = fieldSceneState.m70_cameraTargetOverride[0] = fieldSceneState.m40_cameraAt2.vx;
    fieldSceneState.m80_desiredCameraTarget[1] = fieldSceneState.m70_cameraTargetOverride[1] = fieldSceneState.m40_cameraAt2.vy;
    fieldSceneState.m80_desiredCameraTarget[2] = fieldSceneState.m70_cameraTargetOverride[2] = fieldSceneState.m40_cameraAt2.vz;
    fieldSceneState.m90_cameraPositionOverride[0] = fieldSceneState.m30_cameraEye2.vx;
    fieldSceneState.m90_cameraPositionOverride[1] = fieldSceneState.m30_cameraEye2.vy;
    fieldSceneState.m90_cameraPositionOverride[2] = fieldSceneState.m30_cameraEye2.vz;
    fieldSceneState.mA0_desiredCameraPosition[0] = fieldSceneState.m30_cameraEye2.vx;
    fieldSceneState.mA0_desiredCameraPosition[1] = fieldSceneState.m30_cameraEye2.vy;
    fieldSceneState.mA0_desiredCameraPosition[2] = fieldSceneState.m30_cameraEye2.vz;
    fieldExectuteMaxCycles++;
    pCurrentFieldScriptActor->mCC_scriptPC += 1;
}

void OP_SETUP_CAMERA_INTERPOLATION_STEPS(void) {
    VECTOR delta;
    VECTOR dir;
    s32 distance;
    s32 speed;

    switch (pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 1] & 0xF) {
    case 0:
        fieldSceneState.mBE_cameraInterpolationTargetNumSteps = getImmediateOrVariableUnsigned(2);
        if (fieldSceneState.mBE_cameraInterpolationTargetNumSteps == 0) {
            fieldSceneState.mBE_cameraInterpolationTargetNumSteps++;
            fieldSceneState.m104_op99Var5 = 1;
        }
        fieldSceneState.mD0_cameraInterpolationTargetStep[0] = (fieldSceneState.m80_desiredCameraTarget[0] - fieldSceneState.m70_cameraTargetOverride[0]) / fieldSceneState.mBE_cameraInterpolationTargetNumSteps;
        fieldSceneState.mD0_cameraInterpolationTargetStep[1] = (fieldSceneState.m80_desiredCameraTarget[1] - fieldSceneState.m70_cameraTargetOverride[1]) / fieldSceneState.mBE_cameraInterpolationTargetNumSteps;
        fieldSceneState.mD0_cameraInterpolationTargetStep[2] = (fieldSceneState.m80_desiredCameraTarget[2] - fieldSceneState.m70_cameraTargetOverride[2]) / fieldSceneState.mBE_cameraInterpolationTargetNumSteps;
        fieldSceneState.mC0_cameraInterpolationTargetStartPosition[0] = fieldSceneState.m70_cameraTargetOverride[0];
        fieldSceneState.mC0_cameraInterpolationTargetStartPosition[1] = fieldSceneState.m70_cameraTargetOverride[1];
        fieldSceneState.mC0_cameraInterpolationTargetStartPosition[2] = fieldSceneState.m70_cameraTargetOverride[2];
        fieldSceneState.mBC_cameraInterpolationFlags |= 1;
        if (pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 1] & 0x80) {
            fieldSceneState.m10_cameraAt.vx = fieldSceneState.m70_cameraTargetOverride[0];
            fieldSceneState.m10_cameraAt.vy = fieldSceneState.m70_cameraTargetOverride[1];
            fieldSceneState.m10_cameraAt.vz = fieldSceneState.m70_cameraTargetOverride[2];
        }
        break;
    case 2:
        delta.vx = (fieldSceneState.m70_cameraTargetOverride[0] - fieldSceneState.m80_desiredCameraTarget[0]) >> 16;
        delta.vy = (fieldSceneState.m70_cameraTargetOverride[1] - fieldSceneState.m80_desiredCameraTarget[1]) >> 16;
        delta.vz = (fieldSceneState.m70_cameraTargetOverride[2] - fieldSceneState.m80_desiredCameraTarget[2]) >> 16;
        VectorNormal(&delta, &dir);
        distance = distance3d((fieldSceneState.m70_cameraTargetOverride[0] - fieldSceneState.m80_desiredCameraTarget[0]) >> 16,
                                 (fieldSceneState.m70_cameraTargetOverride[1] - fieldSceneState.m80_desiredCameraTarget[1]) >> 16,
                                 (fieldSceneState.m70_cameraTargetOverride[2] - fieldSceneState.m80_desiredCameraTarget[2]) >> 16);
        speed = getImmediateOrVariableUnsigned(2);
        fieldSceneState.mBE_cameraInterpolationTargetNumSteps = R3000_DIV(distance, speed);
        fieldSceneState.mD0_cameraInterpolationTargetStep[0] = -(dir.vx * speed) << 4;
        fieldSceneState.mD0_cameraInterpolationTargetStep[1] = -(dir.vy * speed) << 4;
        fieldSceneState.mD0_cameraInterpolationTargetStep[2] = -(dir.vz * speed) << 4;
        fieldSceneState.mC0_cameraInterpolationTargetStartPosition[0] = fieldSceneState.m70_cameraTargetOverride[0];
        fieldSceneState.mC0_cameraInterpolationTargetStartPosition[1] = fieldSceneState.m70_cameraTargetOverride[1];
        fieldSceneState.mC0_cameraInterpolationTargetStartPosition[2] = fieldSceneState.m70_cameraTargetOverride[2];
        fieldSceneState.mBC_cameraInterpolationFlags |= 1;
        if (pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 1] & 0x80) {
            fieldSceneState.m10_cameraAt.vx = fieldSceneState.m70_cameraTargetOverride[0];
            fieldSceneState.m10_cameraAt.vy = fieldSceneState.m70_cameraTargetOverride[1];
            fieldSceneState.m10_cameraAt.vz = fieldSceneState.m70_cameraTargetOverride[2];
        }
        break;
    case 3:
        delta.vx = (fieldSceneState.m90_cameraPositionOverride[0] - fieldSceneState.mA0_desiredCameraPosition[0]) >> 16;
        delta.vy = (fieldSceneState.m90_cameraPositionOverride[1] - fieldSceneState.mA0_desiredCameraPosition[1]) >> 16;
        delta.vz = (fieldSceneState.m90_cameraPositionOverride[2] - fieldSceneState.mA0_desiredCameraPosition[2]) >> 16;
        VectorNormal(&delta, &dir);
        distance = distance3d((fieldSceneState.m90_cameraPositionOverride[0] - fieldSceneState.mA0_desiredCameraPosition[0]) >> 16,
                                 (fieldSceneState.m90_cameraPositionOverride[1] - fieldSceneState.mA0_desiredCameraPosition[1]) >> 16,
                                 (fieldSceneState.m90_cameraPositionOverride[2] - fieldSceneState.mA0_desiredCameraPosition[2]) >> 16);
        speed = getImmediateOrVariableUnsigned(2);
        fieldSceneState.mE0_cameraInterpolationPositionNumSteps = R3000_DIV(distance, speed);
        fieldSceneState.mF4_cameraInterpolationPositionStep[0] = -(dir.vx * speed) << 4;
        fieldSceneState.mF4_cameraInterpolationPositionStep[1] = -(dir.vy * speed) << 4;
        fieldSceneState.mF4_cameraInterpolationPositionStep[2] = -(dir.vz * speed) << 4;
        fieldSceneState.mE4_cameraInterpolationStartPosition[0] = fieldSceneState.m90_cameraPositionOverride[0];
        fieldSceneState.mE4_cameraInterpolationStartPosition[1] = fieldSceneState.m90_cameraPositionOverride[1];
        fieldSceneState.mE4_cameraInterpolationStartPosition[2] = fieldSceneState.m90_cameraPositionOverride[2];
        fieldSceneState.mBC_cameraInterpolationFlags |= 2;
        if (pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 1] & 0x80) {
            fieldSceneState.m0_cameraEye.vx = fieldSceneState.m90_cameraPositionOverride[0];
            fieldSceneState.m0_cameraEye.vy = fieldSceneState.m90_cameraPositionOverride[1];
            fieldSceneState.m0_cameraEye.vz = fieldSceneState.m90_cameraPositionOverride[2];
        }
        break;
    case 1:
        fieldSceneState.mE0_cameraInterpolationPositionNumSteps = getImmediateOrVariableUnsigned(2);
        if (fieldSceneState.mE0_cameraInterpolationPositionNumSteps == 0) {
            fieldSceneState.mE0_cameraInterpolationPositionNumSteps++;
            fieldSceneState.m108_op99Var6 = 1;
        }
        fieldSceneState.mF4_cameraInterpolationPositionStep[0] = (fieldSceneState.mA0_desiredCameraPosition[0] - fieldSceneState.m90_cameraPositionOverride[0]) / fieldSceneState.mE0_cameraInterpolationPositionNumSteps;
        fieldSceneState.mF4_cameraInterpolationPositionStep[1] = (fieldSceneState.mA0_desiredCameraPosition[1] - fieldSceneState.m90_cameraPositionOverride[1]) / fieldSceneState.mE0_cameraInterpolationPositionNumSteps;
        fieldSceneState.mF4_cameraInterpolationPositionStep[2] = (fieldSceneState.mA0_desiredCameraPosition[2] - fieldSceneState.m90_cameraPositionOverride[2]) / fieldSceneState.mE0_cameraInterpolationPositionNumSteps;
        fieldSceneState.mE4_cameraInterpolationStartPosition[0] = fieldSceneState.m90_cameraPositionOverride[0];
        fieldSceneState.mE4_cameraInterpolationStartPosition[1] = fieldSceneState.m90_cameraPositionOverride[1];
        fieldSceneState.mE4_cameraInterpolationStartPosition[2] = fieldSceneState.m90_cameraPositionOverride[2];
        fieldSceneState.mBC_cameraInterpolationFlags |= 2;
        if (pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 1] & 0x80) {
            fieldSceneState.m0_cameraEye.vx = fieldSceneState.m90_cameraPositionOverride[0];
            fieldSceneState.m0_cameraEye.vy = fieldSceneState.m90_cameraPositionOverride[1];
            fieldSceneState.m0_cameraEye.vz = fieldSceneState.m90_cameraPositionOverride[2];
        }
        break;
    }
    pCurrentFieldScriptActor->mCC_scriptPC += 4;
}

void OPX_A8(void) {
    setVar(readU16FromScript(1), FP_INT(fieldSceneState.m10_cameraAt.vx));
    setVar(readU16FromScript(3), FP_INT(fieldSceneState.m10_cameraAt.vz));
    setVar(readU16FromScript(5), FP_INT(fieldSceneState.m10_cameraAt.vy));
    fieldExectuteMaxCycles++;
    pCurrentFieldScriptActor->mCC_scriptPC += 7;
}

void OPX_A9(void) {
    setVar(readU16FromScript(1), FP_INT(fieldSceneState.m0_cameraEye.vx));
    setVar(readU16FromScript(3), FP_INT(fieldSceneState.m0_cameraEye.vz));
    setVar(readU16FromScript(5), FP_INT(fieldSceneState.m0_cameraEye.vy));
    fieldExectuteMaxCycles++;
    pCurrentFieldScriptActor->mCC_scriptPC += 7;
}

void OP_AD(void) {
    setVar(readU16FromScript(1), FP_INT(fieldSceneState.m40_cameraAt2.vx));
    setVar(readU16FromScript(3), FP_INT(fieldSceneState.m40_cameraAt2.vz));
    setVar(readU16FromScript(5), FP_INT(fieldSceneState.m40_cameraAt2.vy));
    fieldExectuteMaxCycles++;
    pCurrentFieldScriptActor->mCC_scriptPC += 7;
}

void OP_AE(void) {
    setVar(readU16FromScript(1), FP_INT(fieldSceneState.m30_cameraEye2.vx));
    setVar(readU16FromScript(3), FP_INT(fieldSceneState.m30_cameraEye2.vz));
    setVar(readU16FromScript(5), FP_INT(fieldSceneState.m30_cameraEye2.vy));
    fieldExectuteMaxCycles++;
    pCurrentFieldScriptActor->mCC_scriptPC += 7;
}

void OP_AF(void) {
    if (pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 3] == 0) {
        setVar(readU16FromScript(1), fieldSceneState.mB8_op99Var3);
    } else {
        fieldSceneState.mB8_op99Var3 = readU16FromScript(1);
    }
    fieldExectuteMaxCycles++;
    pCurrentFieldScriptActor->mCC_scriptPC += 4;
}

void OP_B0(void) {
    if (pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 3] == 0) {
        setVar(readU16FromScript(1), fieldSceneState.mB6_op99Var2);
    } else {
        fieldSceneState.mB6_op99Var2 = readU16FromScript(1);
    }
    fieldExectuteMaxCycles++;
    pCurrentFieldScriptActor->mCC_scriptPC += 4;
}

void OP_B1(void) {
    if (pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 3] == 0) {
        setVar(readU16FromScript(1), fieldSceneState.mB0_op99Var1);
    } else {
        fieldSceneState.mB0_op99Var1 = readU16FromScript(1);
    }
    fieldExectuteMaxCycles++;
    pCurrentFieldScriptActor->mCC_scriptPC += 4;
}

void OP_F0(void) {
    setVar(readU16FromScript(1), fieldSceneState.mB8_op99Var3);
    setVar(readU16FromScript(3), fieldSceneState.mB6_op99Var2);
    setVar(readU16FromScript(5), fieldSceneState.mB0_op99Var1);
    fieldExectuteMaxCycles++;
    pCurrentFieldScriptActor->mCC_scriptPC += 7;
}

void OP_F3(void) {
    s32 target[3];
    s32 position[3];
    s32 distance;
    s32 yaw;
    s32 pitch;

    target[0] = fieldSceneState.m80_desiredCameraTarget[0];
    target[1] = fieldSceneState.m80_desiredCameraTarget[1];
    target[2] = fieldSceneState.m80_desiredCameraTarget[2];
    position[0] = fieldSceneState.mA0_desiredCameraPosition[0];
    position[1] = fieldSceneState.mA0_desiredCameraPosition[1];
    position[2] = fieldSceneState.mA0_desiredCameraPosition[2];
    distance = distance3d((position[0] - target[0]) >> 16, (position[1] - target[1]) >> 16, (position[2] - target[2]) >> 16) / 2;
    fieldSceneState.mBA_op99Var4 = 0x1000;
    yaw = ((u16)-ratan2(target[2] - position[2], target[0] - position[0]) - 0x400) & 0xFFF;
    pitch = (-ratan2(length2d((position[0] - target[0]) >> 16, (position[2] - target[2]) >> 16), (target[1] - position[1]) >> 16) * 360 >> 12) + 0x5B;
    setVar(readU16FromScript(1), yaw);
    setVar(readU16FromScript(3), pitch);
    setVar(readU16FromScript(5), distance);
    fieldExectuteMaxCycles++;
    pCurrentFieldScriptActor->mCC_scriptPC += 7;
}

void computeOrbit(s32* position, s32* center, s32 rotation) {
    MATRIX m;
    VECTOR delta;
    VECTOR out;
    SVECTOR rot;

    rot.vx = 0;
    rot.vy = rotation;
    rot.vz = 0;
    PushMatrix();
    createRotationMatrix(&rot, &m);
    delta.vx = center[0] - position[0];
    delta.vy = center[1] - position[1];
    delta.vz = center[2] - position[2];
    ApplyMatrixLV(&m, &delta, &out);
    position[0] = out.vx + center[0];
    position[2] = out.vz + center[2];
    PopMatrix();
}

void OP_EB(void) {
    s32 center[3];
    s32 position[3];
    s32 rotation;
    s32 angle;
    s32 radius;
    s32 a;

    center[0] = getVar80(1, pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 0xD]) << 16;
    center[2] = getVar40(3, pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 0xD]) << 16;
    center[1] = getVar20(5, pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 0xD]) << 16;
    rotation = getVar10(7, pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 0xD]);
    a = getVar08(9, pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 0xD]);
    radius = getVar04(0xB, pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 0xD]);
    angle = (a * 0xB60 >> 8) + 0xC00;
    position[1] = (getAngleSin(angle) * radius * -0x20 >> 16) * fieldSceneState.mBA_op99Var4 * 16 + center[1];
    position[2] = (getAngleCos(angle) * radius * 0x20 >> 16) * fieldSceneState.mBA_op99Var4 * 16 + center[2];
    position[0] = center[0];
    computeOrbit(position, center, rotation);
    setVar(readU16FromScript(0xE), FP_INT(position[0]));
    setVar(readU16FromScript(0x10), FP_INT(position[2]));
    setVar(readU16FromScript(0x12), FP_INT(position[1]));
    fieldExectuteMaxCycles++;
    pCurrentFieldScriptActor->mCC_scriptPC += 0x14;
}

void OP_ORBIT(void) {
    s32 center[3];
    s32 position[3];
    s32 rotation;
    s32 angle;
    s32 radius;
    s32 a;

    switch (pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 1]) {
    case 0:
        center[0] = fieldSceneState.m70_cameraTargetOverride[0];
        center[1] = fieldSceneState.m70_cameraTargetOverride[1];
        center[2] = fieldSceneState.m70_cameraTargetOverride[2];
        break;
    case 1:
        center[0] = fieldSceneState.m80_desiredCameraTarget[0];
        center[1] = fieldSceneState.m80_desiredCameraTarget[1];
        center[2] = fieldSceneState.m80_desiredCameraTarget[2];
        break;
    case 2:
        center[0] = fieldSceneState.m90_cameraPositionOverride[0];
        center[1] = fieldSceneState.m90_cameraPositionOverride[1];
        center[2] = fieldSceneState.m90_cameraPositionOverride[2];
        break;
    case 3:
        center[0] = fieldSceneState.mA0_desiredCameraPosition[0];
        center[1] = fieldSceneState.mA0_desiredCameraPosition[1];
        center[2] = fieldSceneState.mA0_desiredCameraPosition[2];
        break;
    }
    rotation = getVar80(2, pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 8]);
    a = getVar40(4, pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 8]);
    radius = getVar20(6, pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 8]);
    angle = (a * 0xB60 >> 8) + 0xC00;
    position[1] = (getAngleSin(angle) * radius * -0x20 >> 16) * fieldSceneState.mBA_op99Var4 * 16 + center[1];
    position[2] = (getAngleCos(angle) * radius * 0x20 >> 16) * fieldSceneState.mBA_op99Var4 * 16 + center[2];
    position[0] = center[0];
    computeOrbit(position, center, rotation);
    setVar(readU16FromScript(9), FP_INT(position[0]));
    setVar(readU16FromScript(0xB), FP_INT(position[2]));
    setVar(readU16FromScript(0xD), FP_INT(position[1]));
    fieldExectuteMaxCycles++;
    pCurrentFieldScriptActor->mCC_scriptPC += 0xF;
}

void OP_ED(void) {
    s32 v[3];

    switch (pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 1]) {
    case 0:
        v[0] = fieldSceneState.m70_cameraTargetOverride[0];
        v[1] = fieldSceneState.m70_cameraTargetOverride[1];
        v[2] = fieldSceneState.m70_cameraTargetOverride[2];
        break;
    case 1:
        v[0] = fieldSceneState.m80_desiredCameraTarget[0];
        v[1] = fieldSceneState.m80_desiredCameraTarget[1];
        v[2] = fieldSceneState.m80_desiredCameraTarget[2];
        break;
    case 2:
        v[0] = fieldSceneState.m90_cameraPositionOverride[0];
        v[1] = fieldSceneState.m90_cameraPositionOverride[1];
        v[2] = fieldSceneState.m90_cameraPositionOverride[2];
        break;
    case 3:
        v[0] = fieldSceneState.mA0_desiredCameraPosition[0];
        v[1] = fieldSceneState.mA0_desiredCameraPosition[1];
        v[2] = fieldSceneState.mA0_desiredCameraPosition[2];
        break;
    }
    setVar(readU16FromScript(2), FP_INT(v[0]));
    setVar(readU16FromScript(4), FP_INT(v[2]));
    setVar(readU16FromScript(6), FP_INT(v[1]));
    fieldExectuteMaxCycles++;
    pCurrentFieldScriptActor->mCC_scriptPC += 8;
}

void OP_EE(void) {
    s32 v[3];

    switch (pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 1]) {
    case 0:
        v[0] = fieldSceneState.m70_cameraTargetOverride[0];
        v[1] = fieldSceneState.m70_cameraTargetOverride[1];
        v[2] = fieldSceneState.m70_cameraTargetOverride[2];
        break;
    case 1:
        v[0] = fieldSceneState.m80_desiredCameraTarget[0];
        v[1] = fieldSceneState.m80_desiredCameraTarget[1];
        v[2] = fieldSceneState.m80_desiredCameraTarget[2];
        break;
    case 2:
        v[0] = fieldSceneState.m90_cameraPositionOverride[0];
        v[1] = fieldSceneState.m90_cameraPositionOverride[1];
        v[2] = fieldSceneState.m90_cameraPositionOverride[2];
        break;
    case 3:
        v[0] = fieldSceneState.mA0_desiredCameraPosition[0];
        v[1] = fieldSceneState.mA0_desiredCameraPosition[1];
        v[2] = fieldSceneState.mA0_desiredCameraPosition[2];
        break;
    }
    switch (pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 2]) {
    case 0:
        fieldSceneState.m70_cameraTargetOverride[0] = v[0];
        fieldSceneState.m70_cameraTargetOverride[1] = v[1];
        fieldSceneState.m70_cameraTargetOverride[2] = v[2];
        break;
    case 1:
        fieldSceneState.m80_desiredCameraTarget[0] = v[0];
        fieldSceneState.m80_desiredCameraTarget[1] = v[1];
        fieldSceneState.m80_desiredCameraTarget[2] = v[2];
        break;
    case 2:
        fieldSceneState.m90_cameraPositionOverride[0] = v[0];
        fieldSceneState.m90_cameraPositionOverride[1] = v[1];
        fieldSceneState.m90_cameraPositionOverride[2] = v[2];
        break;
    case 3:
        fieldSceneState.mA0_desiredCameraPosition[0] = v[0];
        fieldSceneState.mA0_desiredCameraPosition[1] = v[1];
        fieldSceneState.mA0_desiredCameraPosition[2] = v[2];
        break;
    }
    fieldExectuteMaxCycles++;
    pCurrentFieldScriptActor->mCC_scriptPC += 3;
}
