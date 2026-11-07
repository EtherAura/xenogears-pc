
#include "common.h"
#include "pc_gameplay.h"
#include "pc_retail_heap.h"
#include "psyq.h"
#include "main/memory.h"
#include "main/filesystem.h"
#include "main/game_state.h"
#include "main/sprite.h"
#include "field/field.h"

#define FP_INT(x) (((s16*)&(x))[1])

/* Noah: sFieldTrigger (4 corners x, y, z) */
typedef struct {
    /* 0x00 */ s16 m0_corner0[3];
    /* 0x06 */ s16 m6_corner1[3];
    /* 0x0C */ s16 mC_corner2[3];
    /* 0x12 */ s16 m12_corner3[3];
} sFieldTrigger; /* 0x18 */

void resetFieldScriptActor(void);
extern s16 deltaMoveVar0[]; /* array-typed: the store may alias the PC */
extern sFieldTrigger* fieldTriggerData;
extern FieldBss_800AF880 fieldSceneState;
s32 getCharacterToEntity(s32 offset);
extern s32 g_updateAllEntitiesSub2Var0;
void projectCharacterToScreen(s32* x, s32* y);
s32 distance3d(s32 x, s32 y, s32 z);
/* Fog / background clear colour (FieldBss_800B2184 fields) */
extern s16 isFogSetup;
void setupObjectRenderModes(void);
/* Noah: cameraLimits */
extern s16 cameraLimits[4];
/* Queued VRAM moves (EX 0xE1 OP_VRAM_OPERATION) */
extern RECT vramRectCommands[32];
extern s16 vramRectCommandDestX[32];
extern s16 vramRectCommandDestY[32];
extern u16 vramRectCommandIndex;
extern FieldImageList fieldImageMoveCommands;
extern s32 disableParticles;
/* main filesystem.c: sets up a VRAM scroll command (a6/a7 are passed as s16 here) */
void* setupImageMoveCommand(void* cmd, s16 x, s16 y, s16 width, s16 height, s16 count, s16 a6, s16 a7, void* speeds);
void executeBattleMoveImageCommands(void* cmd);
extern s32 dialogWindowFlag2;
extern s16 dialogWindowOpenAnimationNumFrames;
extern u16 inputAllowedMask2[];
s32 getWalkMeshVarByte(s32 index, s32 byte);
void setWalkMeshVarByte(s32 index, s32 byte, s32 value);
extern sFieldEntity* pCurrentFieldEntity;
s32 getAngleSin(s32 angle);
s32 getAngleCos(s32 angle);
extern s32 playMusicAuthorized;
extern s32 fieldExecuteVar3;
extern s32 fieldChangePrevented;
extern s16 fieldRandomBattleVar[]; /* Noah: fieldRandomBattleVar ([0]; array-typed: store ordering) */
extern s32 g_playerControlledActor;
extern s32 g_breakCurrentScript;
extern s32 fieldMapNumber;  /* Noah: fieldMapNumber (main) */
s32 length1d(s32 x);
s32 length2d(s32 x, s32 y);
void setVisualAnimation(SpriteActorCore* actor, s32 animation, sFieldEntity* entity);
void SaveFieldAndDirections(void);
extern FieldBss_800B2184 op9DVar1;
extern s32 loadCompleted;
extern s32 fieldMusicLoadPending;  /* Noah: fieldMusicLoadPending (main) */
extern s32 load2dAnimVar;
void enablePlayerFlag80(void);
u16 getPlayerCharacterDir(void);
u16 getCameraDirection(void);
extern s32 bootModeReady;
extern s32 newBootMode;
extern GameState* pKernelGameState; /* Noah: pKernelGameState (main) */
extern u8 kernelAndFieldStatesSynced;
void syncKernelAndFieldStates(void);
void OP_CHANGE_FIELD_WHEN_READY_Sub(void);     /* Noah: OP_CHANGE_FIELD_WHEN_READY_Sub (empty) */
extern s32 iRam800adb70;
extern s32 fieldTransitionMode;
extern s32 fieldTransitionFadeInLength;
void OP_CHANGE_FIELD_WHEN_READY2(void);
extern u8 battleTransitionEffect;  /* Noah: battleTransitionEffect (main) */
extern u8 requestedBattleConfig;  /* Noah: requestedBattleConfig (main) */
extern u8 battleInitVar1;  /* Noah: battleInitVar1 (main) */
extern s32 fieldExecuteVar2;
extern s32 fieldBattleRequested;
extern s32 battleStartSaveWritePending;
extern u8 fieldBattleTransitionEffect;  /* Noah: fieldBattleTransitionEffect (FieldBss_800B2184.m1D2) */
extern s32 g_menuOpenCount; /* Noah: g_menuOpenCount (main) */
extern s32 g_menuIdToOpen;
extern u8 g_menuOpenArg;  /* Noah: g_menuOpenArg (main) */
extern s16 g_menuOpenArgFromScript;
extern s32 op99Var7[];
extern u8 g_menuDisabled[]; /* [0] Noah: g_menuDisabled, [1] g_compassDisabled (FieldBss_800B2184.m4C/m4D) */
void updateScriptActor3dRotation(s32 actor);
void playFieldSoundEffect(s32 a, s32 b);
extern s32 fieldScriptTimerTicks;
extern s32 fieldScriptTimerMode;

void OP_SETUP_FOG(void) {
    op9DVar1.mC_fogNearColor[0] = getImmediateOrVariableUnsigned(1);
    op9DVar1.mC_fogNearColor[1] = getImmediateOrVariableUnsigned(3);
    op9DVar1.mC_fogNearColor[2] = getImmediateOrVariableUnsigned(5);
    op9DVar1.m10_fogFarColor[0] = getImmediateOrVariableUnsigned(7);
    op9DVar1.m10_fogFarColor[1] = getImmediateOrVariableUnsigned(9);
    op9DVar1.m10_fogFarColor[2] = getImmediateOrVariableUnsigned(0xB);
    op9DVar1.m14_fogNear = getImmediateOrVariableUnsigned(0xD);
    op9DVar1.m16_fogFar = getImmediateOrVariableUnsigned(0xF);
    isFogSetup = 1;
    setupObjectRenderModes();
    pCurrentFieldScriptActor->mCC_scriptPC += 0x11;
}

void OP_SET_CAMERA_LIMITS(void) {
    cameraLimits[0] = readS16FromScript(1);
    cameraLimits[1] = readS16FromScript(3);
    cameraLimits[2] = readS16FromScript(5);
    cameraLimits[3] = -readS16FromScript(7);
    pCurrentFieldScriptActor->mCC_scriptPC += 9;
}

void OP_SET_BACKGROUND_CLEAR_COLOR(void) {
    op9DVar1.m18_backgroundClearColor[0] = getImmediateOrVariableUnsigned(1);
    op9DVar1.m18_backgroundClearColor[1] = getImmediateOrVariableUnsigned(3);
    op9DVar1.m18_backgroundClearColor[2] = getImmediateOrVariableUnsigned(5);
    pCurrentFieldScriptActor->mCC_scriptPC += 7;
}

void OP_E4(void) {
}

void moveOrClearVramRect(x, y, w, h, dx, dy, clear)
    s16 x;
s16 y;
s16 w;
s16 h;
s32 dx;
s16 dy;
s32 clear;
{
    s32 i = vramRectCommandIndex & 0x1F;

    vramRectCommands[i].y = y;
    vramRectCommands[i].x = x;
    vramRectCommands[i].w = w;
    vramRectCommands[i].h = h;
    vramRectCommandDestX[i] = dx;
    vramRectCommandDestY[i] = dy;
    if (clear == 0) {
        MoveImage(&vramRectCommands[i], vramRectCommandDestX[i], vramRectCommandDestY[i]);
    } else {
        ClearImage(&vramRectCommands[i], 0, 0, 0);
    }
    vramRectCommandIndex++;
}

void OP_VRAM_OPERATION(void) {
    s32 a = getVar80(1, pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 0xD]);
    s32 b = getVar40(3, pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 0xD]);
    RECT unused;

    if (a == 0 && b == 0) {
        s32 c = getVar20(5, pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 0xD]);
        s32 d = getVar10(7, pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 0xD]);
        s32 e = getVar08(9, pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 0xD]);
        s32 f = getVar04(0xB, pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 0xD]);
        moveOrClearVramRect(c, d, e, f, 0, 0, 1);
    } else {
        s32 c = getVar20(5, pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 0xD]);
        s32 d = getVar10(7, pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 0xD]);
        s32 e = getVar08(9, pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 0xD]);
        s32 f = getVar04(0xB, pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 0xD]);
        moveOrClearVramRect(a, b, c, d, e, f, 0);
    }
    pCurrentFieldScriptActor->mCC_scriptPC += 0xE;
}

void OP_DD(void) {
    pCurrentFieldScriptActor->m134_x5 = getVar80(1, pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 5]);
    pCurrentFieldScriptActor->mEE_spriteSplitHeight = getVar40(3, pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 5]);
    pCurrentFieldScriptActor->mCC_scriptPC += 6;
}

void OP_E0(void) {
    sFieldScriptEntity* entity;

    if (readCharacter(1) != 0xFF) {
        entity = fieldSceneState.m290_actorArray[readCharacter(1)].m4C_scriptEntity;
        entity->m134_x5 = getVar80(2, pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 6]);
        entity->mEE_spriteSplitHeight = getVar40(4, pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 6]);
    }
    pCurrentFieldScriptActor->mCC_scriptPC += 7;
}

void OP_SET_DEFORMATION_STRENGTH(void) {
    s32 index = getImmediateOrVariableUnsigned(1);
    s32 strength = getImmediateOrVariableUnsigned(3);

    if (strength >= 0x1000) {
        strength = 0xFFF;
    }
    if (fieldSceneState.m290_actorArray[currentFieldActorId].m58_flags & 0x2000) {
        pCurrentFieldScriptActor->m118_deformationStrength[index] = strength;
    }
    pCurrentFieldScriptActor->mCC_scriptPC += 5;
}

void OP_DC(void) {
    s32 a = getVariable(readU16FromScript(1));
    s32 b = getVariable(readU16FromScript(3));

    setVar(readU16FromScript(3), a);
    setVar(readU16FromScript(1), b);
    pCurrentFieldScriptActor->mCC_scriptPC += 5;
}

/* Enhanced (xg-hbc.19.3): the texture scrolls advance their rows a native
 * frame's speed each call; at 60 Hz each frame takes its part of it
 * (pcFieldSplit, f_80072140.c: they scrolled twice as fast), the speeds the
 * scripts set (OPX_40) left as they are. */
s32 pcFieldSubFrames(void);
s32 pcFieldSplit(s32 d);
void executeFieldMoveImageCommands(void) {
    s32 i;

    for (i = 0; i < fieldImageMoveCommands.m0_count; i++) {
        ImageMoveCommand* cmd = fieldImageMoveCommands.m4_commands[i];
        if (pcFieldSubFrames() > 1 && cmd != NULL && cmd->m10_speeds != NULL && cmd->mA_count > 0 && cmd->mA_count <= 256) {
            u8 part[256];
            u8* speeds = cmd->m10_speeds;
            s32 r;
            for (r = 0; r < cmd->mA_count; r++) {
                part[r] = (u8)(s8)pcFieldSplit((s8)speeds[r]);
            }
            cmd->m10_speeds = part;
            executeBattleMoveImageCommands(cmd);
            cmd->m10_speeds = speeds;
        } else {
            executeBattleMoveImageCommands(cmd);
        }
        if (cmd != NULL && cmd->m14_offsets != NULL) { /* the oracle: where the rows are drawn from */
            s32 r;
            for (r = 0; r < cmd->mA_count; r++) {
                pcFieldPicAdd(PC_FIELD_PIC_SCROLL, (u16)((s16)cmd->m14_offsets[r] >> 4) % cmd->m4_width);
            }
        }
    }
}

void OPX_40(void) {
    s32 index = getImmediateOrVariableUnsigned(1);
    s32 offset = getImmediateOrVariableUnsigned(3);
    s32 value = readU16FromScript(5);

    if (offset < fieldImageMoveCommands.m104_sizes[index]) {
        ((u8*)fieldImageMoveCommands.m84_buffers[index])[offset] = value;
    }
    pCurrentFieldScriptActor->mCC_scriptPC += 7;
}

void OP_DA(void) {
    s32 size;
    s32 i;
    s32 fill;
    s32 x;
    s32 y;
    s32 w;
    s32 h;
    s32 a6;

    if (disableParticles == 0 && fieldImageMoveCommands.m0_count < 0x20) {
        size = readU16FromScript(9);
        fieldImageMoveCommands.m104_sizes[fieldImageMoveCommands.m0_count] = size;
        fieldImageMoveCommands.m84_buffers[fieldImageMoveCommands.m0_count] = allocateHeap(PC_HEAP_BYTES(size + 1), 0);
        fieldImageMoveCommands.m4_commands[fieldImageMoveCommands.m0_count] = allocateHeap(PC_HEAP_SIZED((s32)sizeof(ImageMoveCommand), (s32)0x18), 0);
        fill = readU16FromScript(0xF);
        for (i = 0; i < size; i++) {
            ((u8*)fieldImageMoveCommands.m84_buffers[fieldImageMoveCommands.m0_count])[i] = fill;
        }
        x = (s16)readU16FromScript(1);
        y = (s16)readU16FromScript(3);
        w = (s16)readU16FromScript(5);
        h = (s16)readU16FromScript(7);
        a6 = (s16)readU16FromScript(0xB);
        setupImageMoveCommand(fieldImageMoveCommands.m4_commands[fieldImageMoveCommands.m0_count], x, y, w, h, size, a6, readU16FromScript(0xD),
                      fieldImageMoveCommands.m84_buffers[fieldImageMoveCommands.m0_count]);
        fieldImageMoveCommands.m0_count++;
    }
    pCurrentFieldScriptActor->mCC_scriptPC += 0x11;
}

void OP_0F(void) {
    pCurrentFieldScriptActor->mCC_scriptPC++;
}

void OP_0E(void) {
    pCurrentFieldScriptActor->mCC_scriptPC++;
}

s32 getWalkMeshVarByte(s32 index, s32 byte) {
    switch (byte) {
    case 0:
        return ((u8*)&walkMeshVar1[index])[0];
    case 1:
        return ((u8*)&walkMeshVar1[index])[1];
    case 2:
        return ((u8*)&walkMeshVar1[index])[2];
    case 3:
        return ((u8*)&walkMeshVar1[index])[3];
    }
    return 0;
}

void setWalkMeshVarByte(s32 index, s32 byte, s32 value) {
    switch (byte) {
    case 0:
        walkMeshVar1[index] = (walkMeshVar1[index] & ~0xFF) | value;
        break;
    case 1:
        value <<= 8;
        walkMeshVar1[index] = (walkMeshVar1[index] & 0xFFFF00FF) | value;
        break;
    case 2:
        value <<= 16;
        walkMeshVar1[index] = (walkMeshVar1[index] & 0xFF00FFFF) | value;
        break;
    case 3:
        value <<= 24;
        walkMeshVar1[index] = (walkMeshVar1[index] & 0x00FFFFFF) | value;
        break;
    }
}

void OP_D6(void) {
    switch (dialogWindowFlag2 = getImmediateOrVariableUnsigned(1)) {
    case 0:
        dialogWindowOpenAnimationNumFrames = 8;
        break;
    case 1:
        dialogWindowOpenAnimationNumFrames = 6;
        break;
    case 2:
        dialogWindowOpenAnimationNumFrames = 4;
        break;
    }
    pCurrentFieldScriptActor->mCC_scriptPC += 3;
}

void OP_D5(void) {
    inputAllowedMask2[0] = readU16FromScript(1);
    pCurrentFieldScriptActor->mCC_scriptPC += 3;
}

void OP_80(void) {
    setWalkMeshVarByte(pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 1], pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 2],
                  getImmediateOrVariableUnsigned(3));
    pCurrentFieldScriptActor->mCC_scriptPC += 5;
}

void OP_81(void) {
    s32 v = getWalkMeshVarByte(pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 1], pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 2]);

    v |= getImmediateOrVariableUnsigned(3);
    setWalkMeshVarByte(pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 1], pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 2], v);
    pCurrentFieldScriptActor->mCC_scriptPC += 5;
}

void OP_83(void) {
    s32 v = getWalkMeshVarByte(pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 1], pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 2]);

    v &= getImmediateOrVariableUnsigned(3);
    setWalkMeshVarByte(pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 1], pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 2], v);
    pCurrentFieldScriptActor->mCC_scriptPC += 5;
}

void OP_46(void) {
    pCurrentFieldScriptActor->m60_interactionOffset.vx = (u32)(getAngleSin(pCurrentFieldEntity->m50_modelRotation.vy) * 9) >> 10;
    pCurrentFieldScriptActor->m60_interactionOffset.vz = (getAngleCos(pCurrentFieldEntity->m50_modelRotation.vy) * -0x24) >> 12;
    pCurrentFieldScriptActor->m4_flags |= 0x800;
    pCurrentFieldScriptActor->mCC_scriptPC++;
}

s32 walkAndChangeField(s32 angle, s32 mode, s32 x, s32 z) {
    sFieldScriptEntity* entity;
    SpriteActorCore* sprite;
    s32 minDist;
    s32 startX;
    s32 startZ;
    VECTOR delta;
    s16 rotation;
    s32 a;
    s32 targetX;
    s32 targetZ;

    fieldRandomBattleVar[0] = -1;
    entity = fieldSceneState.m290_actorArray[op9DVar1.mE8_playerControlledActor].m4C_scriptEntity;
    sprite = fieldSceneState.m290_actorArray[op9DVar1.mE8_playerControlledActor].m4_pVramSpriteSheet;
    entity->m4_flags |= 0x38;
    sprite->m18_moveSpeed = 0x80000;
    minDist = length1d(8) * 2;
    startX = FP_INT(entity->m20_position.vx);
    startZ = FP_INT(entity->m20_position.vz);
    if (mode == 0) {
        if (playMusicAuthorized == 0 || fieldExecuteVar3 == 0) {
            g_breakCurrentScript = 1;
        }
        if (fieldChangePrevented != 0) {
            s32 var = getImmediateOrVariableUnsigned(4);
            s32 map = getImmediateOrVariableUnsigned(2);
            SaveFieldAndDirections();
            fieldChangePrevented = 0;
            setVar(2, var);
            fieldMapNumber = map;
        }
        a = pCurrentFieldEntity->m50_modelRotation.vy + angle - 0x400;
        targetX = pCurrentFieldScriptActor->m60_interactionOffset.vx + FP_INT(pCurrentFieldScriptActor->m20_position.vx) + ((getAngleSin(a) * 0x28) >> 12);
        targetZ = pCurrentFieldScriptActor->m60_interactionOffset.vz + FP_INT(pCurrentFieldScriptActor->m20_position.vz) + ((getAngleCos(a) * -0x28) >> 12);
    } else {
        targetX = x;
        targetZ = z;
    }
    delta.vx = targetX - startX;
    delta.vy = 0;
    delta.vz = targetZ - startZ;
    if (minDist < length2d(delta.vx, delta.vz)) {
        goto check;
    }
tail:
    rotation = entity->m106_currentRotation | 0x8000;
    entity->m104_rotation = rotation;
    entity->m106_currentRotation = rotation;
    sprite->m18_moveSpeed = 0;
    entity->mE8_currentAnimationId = 0;
    setVisualAnimation(sprite, 0, &fieldSceneState.m290_actorArray[op9DVar1.mE8_playerControlledActor]);
    g_breakCurrentScript = 1;
    entity->m8C_scriptSlots[entity->mCE_currentScriptSlot].m4_x0_numSteps = 0xFFFF;
    entity->m8C_scriptSlots[entity->mCE_currentScriptSlot].m4_x17_walkMode = 0;
    entity->m0_fieldScriptFlags &= ~0x200000;
    if (mode == 1) {
        entity->m4_flags &= ~0x38;
    }
    entity->m6E_stuckCounter = 0;
    pCurrentFieldScriptActor->mCC_scriptPC += 6;
    return 0;
check:
    if (entity->m68_oldPosition[0] == FP_INT(entity->m20_position.vx) &&
        entity->m68_oldPosition[1] == FP_INT(entity->m20_position.vy) &&
        entity->m68_oldPosition[2] == FP_INT(entity->m20_position.vz)) {
        entity->m6E_stuckCounter++;
    } else {
        entity->m6E_stuckCounter = 0;
    }
    if (entity->m6E_stuckCounter >= 0x41) {
        goto tail;
    }
    rotation = fp_atan2(&delta);
    entity->m104_rotation = rotation;
    entity->m106_currentRotation = rotation;
    g_breakCurrentScript = 1;
    if (mode != 0) {
        pCurrentFieldScriptActor->mCC_scriptPC--;
    }
    return -1;
}

void OPX_68(void) {
    s32 x;
    s32 z;

    if (playMusicAuthorized == 0 || fieldExecuteVar3 == 0 || loadCompleted != 0 || fieldMusicLoadPending == -1 || load2dAnimVar != 0) {
        g_breakCurrentScript = 1;
        pCurrentFieldScriptActor->mCC_scriptPC--;
        return;
    }
    x = getVar80(1, pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 5]);
    z = getVar40(3, pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 5]);
    if (op9DVar1.m1CC_savedPlayerScriptFlags == 0) {
        op9DVar1.m1CC_savedPlayerScriptFlags = fieldSceneState.m290_actorArray[g_playerControlledActor].m4C_scriptEntity->m0_fieldScriptFlags;
    }
    enablePlayerFlag80();
    if (walkAndChangeField(0, 1, x, z) == 0) {
        if (!(op9DVar1.m1CC_savedPlayerScriptFlags & 0x80)) {
            fieldSceneState.m290_actorArray[g_playerControlledActor].m4C_scriptEntity->m0_fieldScriptFlags &= ~0x80;
        }
        op9DVar1.m1CC_savedPlayerScriptFlags = 0;
    }
}

void OP_EA(void) {
    if (playMusicAuthorized == 0 || fieldExecuteVar3 == 0 || loadCompleted != 0 || fieldMusicLoadPending == -1 || load2dAnimVar != 0) {
        g_breakCurrentScript = 1;
    } else {
        enablePlayerFlag80();
        walkAndChangeField(0, 0, 0, 0);
    }
}

void OP_WALK_AND_CHANGE_FIELD(void) {
    if (playMusicAuthorized == 0 || fieldExecuteVar3 == 0 || loadCompleted != 0 || fieldMusicLoadPending == -1 || load2dAnimVar != 0) {
        g_breakCurrentScript = 1;
    } else {
        enablePlayerFlag80();
        walkAndChangeField(0x3E0, 0, 0, 0);
    }
}

void SaveFieldAndDirections(void) {
    setVar(4, fieldMapNumber & 0x3FFF);
    setVar(6, getPlayerCharacterDir());
    setVar(8, getCameraDirection());
    setVar(0x12, (s16)(getVariable(0x12) + 1));
}

void OP_SET_BOOTMODE(void) {
    if (bootModeReady != 0) {
        fieldRandomBattleVar[0] = -1;
        bootModeReady = 0;
        newBootMode = getImmediateOrVariableUnsigned(1);
    }
    pCurrentFieldScriptActor->mCC_scriptPC += 3;
}

void OP_WORLDMAP(void) {
    s32 yaw;

    if (playMusicAuthorized == 0 || fieldExecuteVar3 == 0 || loadCompleted != 0 || fieldMusicLoadPending == -1 || load2dAnimVar != 0) {
        g_breakCurrentScript = 1;
    } else {
        syncKernelAndFieldStates();
        fieldRandomBattleVar[0] = -1;
        fieldExecuteVar3 = 0;
        pKernelGameState->m231A_fieldID = getVar80(1, pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 9]);
        pKernelGameState->m231E_worldmapInitialPositionIndex = getVar40(3, pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 9]);
        yaw = getVar20(5, pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 9]);
        if (((yaw & 0xFFFF) == 0xFFFF) | (yaw == -1)) {
            pKernelGameState->m231C_cameraYaw = (fieldSceneState.m164_cameraRotationBetweenEyeAndAt.vy + 0x800) & 0xFFF;
        } else {
            pKernelGameState->m231C_cameraYaw = (yaw + 0x800) & 0xFFF;
        }
        pKernelGameState->m2320_worldmapMode = getVar10(7, pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 9]);
        OP_CHANGE_FIELD_WHEN_READY_Sub();
        kernelAndFieldStatesSynced = 1;
        g_breakCurrentScript = 1;
        pCurrentFieldScriptActor->mCC_scriptPC += 0xA;
    }
}

void OP_CHANGE_FIELD_WHEN_READY_Sub(void) {
}

void OP_CUSTOM_FIELD_TRANSITION(void) {
    if (playMusicAuthorized == 0 || fieldExecuteVar3 == 0 || loadCompleted != 0 || fieldMusicLoadPending == -1 || load2dAnimVar != 0) {
        g_breakCurrentScript = 1;
    } else {
        OP_CHANGE_FIELD_WHEN_READY2();
        OP_CHANGE_FIELD_WHEN_READY_Sub();
        fieldTransitionMode = getImmediateOrVariableUnsigned(0);
        fieldTransitionFadeInLength = getImmediateOrVariableUnsigned(2);
        pCurrentFieldScriptActor->mCC_scriptPC += 4;
    }
}

void OP_CHANGE_FIELD_WHEN_READY2(void) {
    if (playMusicAuthorized == 0 || fieldExecuteVar3 == 0 || loadCompleted != 0 || fieldMusicLoadPending == -1 || load2dAnimVar != 0 ||
        iRam800adb70 != 0) {
        g_breakCurrentScript = 1;
    } else {
        fieldRandomBattleVar[0] = -1;
        if (fieldChangePrevented != 0) {
            s32 var = getImmediateOrVariableUnsigned(3);
            s32 map = getImmediateOrVariableUnsigned(1);
            SaveFieldAndDirections();
            fieldChangePrevented = 0;
            setVar(2, var);
            fieldMapNumber = map;
            OP_CHANGE_FIELD_WHEN_READY_Sub();
        }
        g_breakCurrentScript = 1;
        pCurrentFieldScriptActor->mCC_scriptPC += 5;
    }
}

void OPX_84(void) {
    s32 nextField;

    if (playMusicAuthorized == 0 || fieldExecuteVar3 == 0 || fieldChangePrevented == 0 || loadCompleted != 0 || fieldMusicLoadPending == -1 ||
        load2dAnimVar != 0) {
        g_breakCurrentScript = 1;
        pCurrentFieldScriptActor->mCC_scriptPC--;
    } else {
        battleTransitionEffect = fieldBattleTransitionEffect;
        requestedBattleConfig = getImmediateOrVariableUnsigned(1);
        battleInitVar1 = 0;
        playMusicAuthorized = 0;
        fieldExecuteVar2 = 0;
        fieldBattleRequested = 1;
        nextField = getImmediateOrVariableUnsigned(5);
        if (nextField != 0x7FFF) {
            s32 var = getImmediateOrVariableUnsigned(7);
            SaveFieldAndDirections();
            setVar(2, var);
            fieldMapNumber = nextField;
            battleStartSaveWritePending = 1;
        }
        g_breakCurrentScript = 1;
        pCurrentFieldScriptActor->mCC_scriptPC += 9;
    }
}

void OP_71(void) {
    if (playMusicAuthorized == 0 || fieldExecuteVar3 == 0 || fieldChangePrevented == 0 || loadCompleted != 0 || fieldMusicLoadPending == -1 ||
        load2dAnimVar != 0) {
        g_breakCurrentScript = 1;
    } else {
        battleTransitionEffect = fieldBattleTransitionEffect;
        requestedBattleConfig = getImmediateOrVariableUnsigned(1);
        battleInitVar1 = 0;
        playMusicAuthorized = 0;
        fieldExecuteVar2 = 0;
        fieldBattleRequested = 1;
        g_breakCurrentScript = 1;
        pCurrentFieldScriptActor->mCC_scriptPC += 3;
    }
}

void OP_82(void) {
    s32 v = getWalkMeshVarByte(pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 1], pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 2]);

    setVar(readU16FromScript(3), v);
    pCurrentFieldScriptActor->mCC_scriptPC += 5;
}

void OP_WAIT_MENU(void) {
    if (g_menuOpenCount == 0) {
        pCurrentFieldScriptActor->mCC_scriptPC++;
    } else {
        pCurrentFieldScriptActor->mCC_scriptPC--;
    }
    g_breakCurrentScript = 1;
}

void OP_OPEN_NORMAL_MENU(void) {
    g_breakCurrentScript = 1;
    g_menuIdToOpen = 0;
    g_menuOpenArg = g_menuOpenArgFromScript;
    g_menuOpenCount++;
    pCurrentFieldScriptActor->mCC_scriptPC++;
}

void OPX_DA(void) {
    g_menuOpenArg = 1;
    g_menuIdToOpen = 6;
    g_breakCurrentScript = 1;
    g_menuOpenCount++;
    pCurrentFieldScriptActor->mCC_scriptPC++;
}

void OP_OPEN_LOAD_GAME_MENU(void) {
    g_menuIdToOpen = 2;
    g_breakCurrentScript = 1;
    g_menuOpenCount++;
    pCurrentFieldScriptActor->mCC_scriptPC++;
}

void OPX_58(void) {
    g_menuOpenArg = getImmediateOrVariableUnsigned(1);
    g_menuIdToOpen = 3;
    g_breakCurrentScript = 1;
    g_menuOpenCount++;
    pCurrentFieldScriptActor->mCC_scriptPC += 3;
}

void OPX_CF(void) {
    s32 var;
    s32 map;

    fieldRandomBattleVar[0] = -1;
    var = getImmediateOrVariableUnsigned(3);
    map = getImmediateOrVariableUnsigned(1);
    SaveFieldAndDirections();
    setVar(2, var);
    fieldMapNumber = map;
    OP_CHANGE_FIELD_WHEN_READY_Sub();
    g_menuIdToOpen = 1;
    g_breakCurrentScript = 1;
    g_menuOpenCount++;
    pCurrentFieldScriptActor->mCC_scriptPC += 5;
}

void OPX_56(void) {
    s32 v = getImmediateOrVariableUnsigned(1);

    g_menuIdToOpen = 1;
    g_breakCurrentScript = 1;
    pKernelGameState->m1930_fieldVarsBackup[1] = v;
    pKernelGameState->m2320_worldmapMode = v;
    fieldVars[1] = v;
    g_menuOpenCount++;
    pCurrentFieldScriptActor->mCC_scriptPC += 3;
}

void OPX_59(void) {
    g_menuOpenArg = getImmediateOrVariableUnsigned(1);
    g_menuIdToOpen = 4;
    g_breakCurrentScript = 1;
    g_menuOpenCount++;
    pCurrentFieldScriptActor->mCC_scriptPC += 3;
}

void OPX_5A(void) {
    g_menuOpenArg = getImmediateOrVariableUnsigned(1);
    g_menuIdToOpen = 5;
    g_breakCurrentScript = 1;
    g_menuOpenCount++;
    pCurrentFieldScriptActor->mCC_scriptPC += 3;
}

void OP_76(void) {
    op99Var7[0] &= 0x7FFF;
    pCurrentFieldScriptActor->mCC_scriptPC++;
}

void OP_77(void) {
    op99Var7[0] |= 0x8000;
    pCurrentFieldScriptActor->mCC_scriptPC++;
}

void OP_ENABLE_PLAYER_CONTROLS(void) {
    fieldRandomBattleVar[0] = 0;
    g_menuDisabled[0] = 0;
    g_menuDisabled[1] = 0;
    op99Var7[0] &= 0x3FFF;
    pCurrentFieldScriptActor->mCC_scriptPC++;
}

void OP_START_CINEMATIC(void) {
    fieldRandomBattleVar[0] = -1;
    g_menuDisabled[0] = 1;
    g_menuDisabled[1] = 1;
    op99Var7[0] |= 0xC000;
    if (playMusicAuthorized == 0 || fieldExecuteVar3 == 0) {
        g_breakCurrentScript = 1;
        pCurrentFieldScriptActor->mCC_scriptPC--;
    } else {
        pCurrentFieldScriptActor->mCC_scriptPC++;
    }
}

void OP_ENABLE_MENU(void) {
    g_menuDisabled[0] = 0;
    pCurrentFieldScriptActor->mCC_scriptPC++;
}

void OP_DISABLE_MENU(void) {
    g_menuDisabled[0] = 1;
    pCurrentFieldScriptActor->mCC_scriptPC++;
}

void OPX_51(void) {
    g_menuDisabled[1] = 0;
    pCurrentFieldScriptActor->mCC_scriptPC++;
}

void OP_DISABLE_COMPASS(void) {
    g_menuDisabled[1] = 1;
    pCurrentFieldScriptActor->mCC_scriptPC++;
}

void OP_DISABLE_RANDOM_BATTLES(void) {
    fieldRandomBattleVar[0] = 0;
    pCurrentFieldScriptActor->mCC_scriptPC++;
}

void OP_15(void) {
    if (playMusicAuthorized == 0 || fieldExecuteVar3 == 0) {
        g_breakCurrentScript = 1;
    } else {
        fieldRandomBattleVar[0] = -1;
        pCurrentFieldScriptActor->mCC_scriptPC++;
    }
}

void OP_48(void) {
    u16 offset = readU16FromScript(1) + getImmediateOrVariableUnsigned(5);

    setVar(readU16FromScript(3), pCurrentFieldScriptFile[offset]);
    pCurrentFieldScriptActor->mCC_scriptPC += 7;
}

void OP_49(void) {
    u16 offset = readU16FromScript(1) + getImmediateOrVariableUnsigned(5);

    if (pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 7] == 0) {
        setVar(readU16FromScript(3), pCurrentFieldScriptFile[offset] | (pCurrentFieldScriptFile[offset + 1] << 8));
    } else {
        setVar(readU16FromScript(3), (s16)(pCurrentFieldScriptFile[offset] + (pCurrentFieldScriptFile[offset + 1] << 8)));
    }
    pCurrentFieldScriptActor->mCC_scriptPC += 8;
}

void OP_OPEN_DOOR(void) {
    sFieldScriptEntity* e;

    if (!(pCurrentFieldScriptActor->m0_fieldScriptFlags & 0x100000)) {
        if (!pCurrentFieldScriptActor->m12C_x5_doorMoving) {
            pCurrentFieldScriptActor->m12C_x5_doorMoving = 1;
            pCurrentFieldScriptActor->mE2_doorFrame = 0;
            playFieldSoundEffect(8, 3);
        } else {
            pCurrentFieldScriptActor->mE2_doorFrame++;
            e = pCurrentFieldScriptActor;
            if (e->mE2_doorFrame < 0x1F) {
                if (pCurrentFieldScriptFile[e->mCC_scriptPC + 1] == 0) {
                    fieldSceneState.m290_actorArray[currentFieldActorId].m50_modelRotation.vy += 0x20;
                } else {
                    fieldSceneState.m290_actorArray[currentFieldActorId].m50_modelRotation.vy -= 0x20;
                }
            } else {
                e->mE2_doorFrame = 0;
                e->m0_fieldScriptFlags |= 0x100000;
                e->m12C_x5_doorMoving = 0;
                pCurrentFieldScriptActor->mCC_scriptPC += 2;
            }
        }
    } else {
        pCurrentFieldScriptActor->mCC_scriptPC += 2;
    }
    updateScriptActor3dRotation(currentFieldActorId);
}

void OP_CLOSE_DOOR(void) {
    sFieldScriptEntity* e;

    if (pCurrentFieldScriptActor->m0_fieldScriptFlags & 0x100000) {
        if (!pCurrentFieldScriptActor->m12C_x5_doorMoving) {
            pCurrentFieldScriptActor->m12C_x5_doorMoving = 1;
            pCurrentFieldScriptActor->mE2_doorFrame = 0;
            playFieldSoundEffect(8, 3);
        } else {
            pCurrentFieldScriptActor->mE2_doorFrame++;
            e = pCurrentFieldScriptActor;
            if (e->mE2_doorFrame < 0x1F) {
                if (pCurrentFieldScriptFile[e->mCC_scriptPC + 1] == 0) {
                    fieldSceneState.m290_actorArray[currentFieldActorId].m50_modelRotation.vy -= 0x20;
                } else {
                    fieldSceneState.m290_actorArray[currentFieldActorId].m50_modelRotation.vy += 0x20;
                }
            } else {
                e->mE2_doorFrame = 0;
                e->m0_fieldScriptFlags &= ~0x100000;
                e->m12C_x5_doorMoving = 0;
                pCurrentFieldScriptActor->mCC_scriptPC += 2;
            }
        }
    } else {
        pCurrentFieldScriptActor->mCC_scriptPC += 2;
    }
    updateScriptActor3dRotation(currentFieldActorId);
}

void OP_E8(void) {
    s32 angle;
    s32 s;
    s32 c;
    sFieldScriptEntity* e;

    if (!(pCurrentFieldScriptActor->m0_fieldScriptFlags & 0x100000)) {
        if (!pCurrentFieldScriptActor->m12C_x5_doorMoving) {
            pCurrentFieldScriptActor->m12C_x5_doorMoving = 1;
            pCurrentFieldScriptActor->mE2_doorFrame = 0;
            playFieldSoundEffect(8, 3);
            pCurrentFieldScriptActor->mD0_targetPositionOffset.vx = pCurrentFieldScriptActor->m20_position.vx;
            pCurrentFieldScriptActor->mD0_targetPositionOffset.vy = pCurrentFieldScriptActor->m20_position.vy;
            pCurrentFieldScriptActor->mD0_targetPositionOffset.vz = pCurrentFieldScriptActor->m20_position.vz;
        } else {
            pCurrentFieldScriptActor->mE2_doorFrame++;
            if (pCurrentFieldScriptActor->mE2_doorFrame < getImmediateOrVariableUnsigned(3)) {
                switch (getImmediateOrVariableUnsigned(5)) {
                case 0x1000:
                    pCurrentFieldScriptActor->mD0_targetPositionOffset.vy -= getImmediateOrVariableUnsigned(1) * 16;
                    pCurrentFieldEntity->mC_matrix.t[1] = FP_INT(pCurrentFieldScriptActor->mD0_targetPositionOffset.vz);
                    break;
                case 0x1001:
                    pCurrentFieldScriptActor->mD0_targetPositionOffset.vy += getImmediateOrVariableUnsigned(1) * 16;
                    pCurrentFieldEntity->mC_matrix.t[1] = FP_INT(pCurrentFieldScriptActor->mD0_targetPositionOffset.vz);
                    break;
                default:
                    angle = pCurrentFieldEntity->m50_modelRotation.vy + getImmediateOrVariableUnsigned(5) - 0x400;
                    s = getAngleSin(angle);
                    pCurrentFieldScriptActor->mD0_targetPositionOffset.vx += s * getImmediateOrVariableUnsigned(1);
                    c = getAngleCos(angle);
                    pCurrentFieldScriptActor->mD0_targetPositionOffset.vz -= c * getImmediateOrVariableUnsigned(1);
                    pCurrentFieldEntity->mC_matrix.t[0] = FP_INT(pCurrentFieldScriptActor->mD0_targetPositionOffset.vx);
                    pCurrentFieldEntity->mC_matrix.t[2] = FP_INT(pCurrentFieldScriptActor->mD0_targetPositionOffset.vz);
                    break;
                }
            } else {
                e = pCurrentFieldScriptActor;
                e->mE2_doorFrame = 0;
                e->m0_fieldScriptFlags |= 0x100000;
                e->m12C_x5_doorMoving = 0;
                pCurrentFieldScriptActor->mCC_scriptPC += 7;
            }
        }
    } else {
        pCurrentFieldScriptActor->mCC_scriptPC += 7;
    }
    updateScriptActor3dRotation(currentFieldActorId);
}

void OP_E9(void) {
    s32 angle;
    s32 s;
    s32 c;
    sFieldScriptEntity* e;

    if (pCurrentFieldScriptActor->m0_fieldScriptFlags & 0x100000) {
        if (!pCurrentFieldScriptActor->m12C_x5_doorMoving) {
            pCurrentFieldScriptActor->m12C_x5_doorMoving = 1;
            pCurrentFieldScriptActor->mE2_doorFrame = 0;
            playFieldSoundEffect(8, 3);
        } else {
            pCurrentFieldScriptActor->mE2_doorFrame++;
            if (pCurrentFieldScriptActor->mE2_doorFrame < getImmediateOrVariableUnsigned(3)) {
                switch (getImmediateOrVariableUnsigned(5)) {
                case 0x1000:
                    pCurrentFieldScriptActor->mD0_targetPositionOffset.vy -= getImmediateOrVariableUnsigned(1) * 16;
                    pCurrentFieldEntity->mC_matrix.t[1] = FP_INT(pCurrentFieldScriptActor->mD0_targetPositionOffset.vz);
                    break;
                case 0x1001:
                    pCurrentFieldScriptActor->mD0_targetPositionOffset.vy += getImmediateOrVariableUnsigned(1) * 16;
                    pCurrentFieldEntity->mC_matrix.t[1] = FP_INT(pCurrentFieldScriptActor->mD0_targetPositionOffset.vz);
                    break;
                default:
                    angle = pCurrentFieldEntity->m50_modelRotation.vy + getImmediateOrVariableUnsigned(5) - 0x400;
                    s = getAngleSin(angle);
                    pCurrentFieldScriptActor->mD0_targetPositionOffset.vx -= s * getImmediateOrVariableUnsigned(1);
                    c = getAngleCos(angle);
                    pCurrentFieldScriptActor->mD0_targetPositionOffset.vz += c * getImmediateOrVariableUnsigned(1);
                    pCurrentFieldEntity->mC_matrix.t[0] = FP_INT(pCurrentFieldScriptActor->mD0_targetPositionOffset.vx);
                    pCurrentFieldEntity->mC_matrix.t[2] = FP_INT(pCurrentFieldScriptActor->mD0_targetPositionOffset.vz);
                    break;
                }
            } else {
                e = pCurrentFieldScriptActor;
                e->mE2_doorFrame = 0;
                e->m0_fieldScriptFlags &= ~0x100000;
                e->m12C_x5_doorMoving = 0;
                pCurrentFieldScriptActor->mCC_scriptPC += 7;
            }
        }
    } else {
        pCurrentFieldScriptActor->mCC_scriptPC += 7;
    }
    updateScriptActor3dRotation(currentFieldActorId);
}

void OP_94(void) {
    s32 a;

    fieldScriptTimerTicks = 0;
    fieldScriptTimerMode = 0xFF;
    a = getImmediateOrVariableUnsigned(1);
    setVar(0xA, ((a << 8) & 0xFF00) | (getImmediateOrVariableUnsigned(3) & 0xFF));
    pCurrentFieldScriptActor->mCC_scriptPC += 5;
}

void OP_95(void) {
    fieldScriptTimerMode = pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 1];
    pCurrentFieldScriptActor->mCC_scriptPC += 2;
}

void OP_96(void) {
    fieldScriptTimerTicks = 0;
    fieldScriptTimerMode = 0xFF;
    pCurrentFieldScriptActor->mCC_scriptPC++;
}

void OP_SET_OBJECT_SWIVEL_ON_X_AXIS(void) {
    pCurrentFieldScriptActor->m12C_x0 = 1;
    pCurrentFieldScriptActor->m70_rotationForRendering = getImmediateOrVariableUnsigned(1);
    pCurrentFieldScriptActor->mCC_scriptPC += 3;
}

void OP_D8(void) {
    pCurrentFieldScriptActor->m12C_x0 = 2;
    pCurrentFieldScriptActor->m70_rotationForRendering = getImmediateOrVariableUnsigned(1);
    pCurrentFieldScriptActor->mCC_scriptPC += 3;
}

void OP_SET_OBJECT_SWIVEL_ON_Z_AXIS(void) {
    pCurrentFieldScriptActor->m12C_x0 = 3;
    pCurrentFieldScriptActor->m70_rotationForRendering = getImmediateOrVariableUnsigned(1);
    pCurrentFieldScriptActor->mCC_scriptPC += 3;
}

void OP_FA(void) {
    sFieldEntity* entity;

    if (readCharacter(2) != 0xFF) {
        entity = &fieldSceneState.m290_actorArray[readCharacter(2)];
        switch (pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 1]) {
        case 0:
            entity->m50_modelRotation.vx += getImmediateOrVariableUnsigned(3);
            break;
        case 1:
            entity->m50_modelRotation.vx -= getImmediateOrVariableUnsigned(3);
            break;
        case 2:
            entity->m50_modelRotation.vy += getImmediateOrVariableUnsigned(3);
            break;
        case 3:
            entity->m50_modelRotation.vy -= getImmediateOrVariableUnsigned(3);
            break;
        case 4:
            entity->m50_modelRotation.vz += getImmediateOrVariableUnsigned(3);
            break;
        case 5:
            entity->m50_modelRotation.vz -= getImmediateOrVariableUnsigned(3);
            break;
        }
        updateScriptActor3dRotation(readCharacter(2));
    }
    pCurrentFieldScriptActor->mCC_scriptPC += 5;
}

void OP_SET_ANGLE_AXIS_ROTATION(void) {
    switch (pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 3]) {
    case 0:
        fieldSceneState.m290_actorArray[currentFieldActorId].m50_modelRotation.vx = getImmediateOrVariableUnsigned(1);
        break;
    case 1:
        fieldSceneState.m290_actorArray[currentFieldActorId].m50_modelRotation.vy = getImmediateOrVariableUnsigned(1);
        break;
    case 2:
        fieldSceneState.m290_actorArray[currentFieldActorId].m50_modelRotation.vz = getImmediateOrVariableUnsigned(1);
        break;
    }
    pCurrentFieldScriptActor->mCC_scriptPC += 4;
    updateScriptActor3dRotation(currentFieldActorId);
}

void OP_BD(void) {
    s16 v = getImmediateOrVariableUnsigned(1);
    s32 id = currentFieldActorId;

    fieldSceneState.m290_actorArray[id].m50_modelRotation.vx += v;
    pCurrentFieldScriptActor->mCC_scriptPC += 3;
    updateScriptActor3dRotation(id);
}

void OP_ROTATION_3D_YAW_SUBTRACT(void) {
    s16 v = getImmediateOrVariableUnsigned(1);
    s32 id = currentFieldActorId;

    fieldSceneState.m290_actorArray[id].m50_modelRotation.vx -= v;
    pCurrentFieldScriptActor->mCC_scriptPC += 3;
    updateScriptActor3dRotation(id);
}

void OP_ROTATION_3D_PITCH_ADD(void) {
    s16 v = getImmediateOrVariableUnsigned(1);
    s32 id = currentFieldActorId;

    fieldSceneState.m290_actorArray[id].m50_modelRotation.vy += v;
    pCurrentFieldScriptActor->mCC_scriptPC += 3;
    updateScriptActor3dRotation(id);
}

void OP_ROTATION_3D_PITCH_SUBTRACT(void) {
    s16 v = getImmediateOrVariableUnsigned(1);
    s32 id = currentFieldActorId;

    fieldSceneState.m290_actorArray[id].m50_modelRotation.vy -= v;
    pCurrentFieldScriptActor->mCC_scriptPC += 3;
    updateScriptActor3dRotation(id);
}

void OP_ROTATION_3D_ROLL_ADD(void) {
    s16 v = getImmediateOrVariableUnsigned(1);
    s32 id = currentFieldActorId;

    fieldSceneState.m290_actorArray[id].m50_modelRotation.vz += v;
    pCurrentFieldScriptActor->mCC_scriptPC += 3;
    updateScriptActor3dRotation(id);
}

void OP_ROTATION_3D_ROLL_SUBTRACT(void) {
    s16 v = getImmediateOrVariableUnsigned(1);
    s32 id = currentFieldActorId;

    fieldSceneState.m290_actorArray[id].m50_modelRotation.vz -= v;
    pCurrentFieldScriptActor->mCC_scriptPC += 3;
    updateScriptActor3dRotation(id);
}

/* first free slot of inventory category 0, -1 if none */
s32 findFreeInventory0Slot(void) {
    s32 i;

    for (i = 0; i < 150; i++) {
        if (pKernelGameState->m1F90_inventory0Counts[i] == 0 || pKernelGameState->m2026_inventory0Ids[i] == 0) {
            return i;
        }
    }
    return -1;
}

/* first free slot of inventory category 1, -1 if none */
s32 findFreeInventory1Slot(void) {
    s32 i;

    for (i = 0; i < 100; i++) {
        if (pKernelGameState->m1D38_inventory1Counts[i] == 0 || pKernelGameState->m1D9C_inventory1Ids[i] == 0) {
            return i;
        }
    }
    return -1;
}

/* first free slot of inventory category 2, -1 if none */
s32 findFreeInventory2Slot(void) {
    s32 i;

    for (i = 0; i < 200; i++) {
        if (pKernelGameState->m1E00_inventory2Counts[i] == 0 || pKernelGameState->m1EC8_inventory2Ids[i] == 0) {
            return i;
        }
    }
    return -1;
}

/* first free slot of inventory category 3, -1 if none */
s32 findFreeInventory3Slot(void) {
    s32 i;

    for (i = 0; i < 100; i++) {
        if (pKernelGameState->m20BC_inventory3Counts[i] == 0 || pKernelGameState->m2120_inventory3Ids[i] == 0) {
            return i;
        }
    }
    return -1;
}

/* first free slot of inventory category 4, -1 if none */
s32 findFreeInventory4Slot(void) {
    s32 i;

    for (i = 0; i < 150; i++) {
        if (pKernelGameState->m2184_inventory4Counts[i] == 0 || pKernelGameState->m221A_inventory4Ids[i] == 0) {
            return i;
        }
    }
    return -1;
}

/* slot of item (index within category 0), -1 if not owned */
s32 findInventory0ItemSlot(s32 item) {
    s32 i;

    for (i = 0; i < 150; i++) {
        if (pKernelGameState->m2026_inventory0Ids[i] == item && pKernelGameState->m1F90_inventory0Counts[i] != 0) {
            return i;
        }
    }
    return -1;
}

/* slot of item (index within category 2), -1 if not owned */
s32 findInventory2ItemSlot(s32 item) {
    s32 i;

    for (i = 0; i < 200; i++) {
        if (pKernelGameState->m1EC8_inventory2Ids[i] == item && pKernelGameState->m1E00_inventory2Counts[i] != 0) {
            return i;
        }
    }
    return -1;
}

/* slot of item (index within category 1), -1 if not owned */
s32 findInventory1ItemSlot(s32 item) {
    s32 i;

    for (i = 0; i < 100; i++) {
        if (pKernelGameState->m1D9C_inventory1Ids[i] == item && pKernelGameState->m1D38_inventory1Counts[i] != 0) {
            return i;
        }
    }
    return -1;
}

/* slot of item (index within category 3), -1 if not owned */
s32 findInventory3ItemSlot(s32 item) {
    s32 i;

    for (i = 0; i < 100; i++) {
        if (pKernelGameState->m2120_inventory3Ids[i] == item && pKernelGameState->m20BC_inventory3Counts[i] != 0) {
            return i;
        }
    }
    return -1;
}

/* slot of item (index within category 4), -1 if not owned */
s32 findInventory4ItemSlot(s32 item) {
    s32 i;

    for (i = 0; i < 150; i++) {
        if (pKernelGameState->m221A_inventory4Ids[i] == item && pKernelGameState->m2184_inventory4Counts[i] != 0) {
            return i;
        }
    }
    return -1;
}

/* item-id array of the item's category */
u8* getInventoryItemIds(s32 item) {
    switch (item >> 8) {
    case 0:
        return pKernelGameState->m2026_inventory0Ids;
    case 1:
        return pKernelGameState->m1D9C_inventory1Ids;
    case 2:
        return pKernelGameState->m1EC8_inventory2Ids;
    case 3:
        return pKernelGameState->m2120_inventory3Ids;
    case 4:
        return pKernelGameState->m221A_inventory4Ids;
    }
    return NULL;
}

/* count array of the item's category */
u8* getInventoryItemCounts(s32 item) {
    switch (item >> 8) {
    case 0:
        return pKernelGameState->m1F90_inventory0Counts;
    case 1:
        return pKernelGameState->m1D38_inventory1Counts;
    case 2:
        return pKernelGameState->m1E00_inventory2Counts;
    case 3:
        return pKernelGameState->m20BC_inventory3Counts;
    case 4:
        return pKernelGameState->m2184_inventory4Counts;
    }
    return NULL;
}

/* slot holding the item, -1 if none */
s32 findInventoryItemSlot(s32 item) {
    switch (item >> 8) {
    case 0:
        return findInventory0ItemSlot(item);
    case 1:
        return findInventory1ItemSlot(item - 0x100);
    case 2:
        return findInventory2ItemSlot(item - 0x200);
    case 3:
        return findInventory3ItemSlot(item - 0x300);
    case 4:
        return findInventory4ItemSlot(item - 0x400);
    }
    return 0;
}

/* first free slot of the item's category, -1 if none */
s32 findFreeInventorySlot(s32 item) {
    switch (item >> 8) {
    case 0:
        return findFreeInventory0Slot();
    case 1:
        return findFreeInventory1Slot();
    case 2:
        return findFreeInventory2Slot();
    case 3:
        return findFreeInventory3Slot();
    case 4:
        return findFreeInventory4Slot();
    }
    return 0;
}

/* Noah OP_5A */
void OP_5A(void) {
    resetFieldScriptActor();
    pCurrentFieldScriptActor->mCC_scriptPC += 1;
}

/* Noah resetFieldScriptActor: stop the current actor */
void resetFieldScriptActor(void) {
    SpriteActorCore* sprite = fieldSceneState.m290_actorArray[currentFieldActorId].m4_pVramSpriteSheet;
    sFieldScriptEntity* se = pCurrentFieldScriptActor;

    g_breakCurrentScript = 1;
    se->m30_stepVector.vx = 0;
    se->m30_stepVector.vy = 0;
    se->m30_stepVector.vz = 0;
    se->m40_externalStep.vx = 0;
    se->m40_externalStep.vy = 0;
    se->m40_externalStep.vz = 0;
    se->m104_rotation = se->m106_currentRotation = se->m104_rotation | 0x8000;
    sprite->mC_step.vx = 0;
    sprite->mC_step.vz = 0;
    sprite->m18_moveSpeed = 0;
}

void OP_7F(void) {
    deltaMoveVar0[0] = getImmediateOrVariableUnsigned(1);
    pCurrentFieldScriptActor->mCC_scriptPC += 3;
}

/* Noah OP_CALL_IF_IN_TRIGGER: call u16 at pc+2 when the player is in trigger zone pc[1] */
void OP_CALL_IF_IN_TRIGGER(void) {
    sFieldScriptEntity* player;
    s32 idx, pos, c0, c1, c2, c3;

    player = fieldSceneState.m290_actorArray[g_playerControlledActor].m4C_scriptEntity;
    idx = pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 1];
    pos = (FP_INT(player->m20_position.vz) << 16) + FP_INT(player->m20_position.vx);
    c0 = (fieldTriggerData[idx].m0_corner0[2] << 16) + fieldTriggerData[idx].m0_corner0[0];
    c1 = (fieldTriggerData[idx].m6_corner1[2] << 16) + fieldTriggerData[idx].m6_corner1[0];
    c2 = (fieldTriggerData[idx].mC_corner2[2] << 16) + fieldTriggerData[idx].mC_corner2[0];
    c3 = (fieldTriggerData[idx].m12_corner3[2] << 16) + fieldTriggerData[idx].m12_corner3[0];
    if (NormalClip(c0, c1, pos) >= 0 && NormalClip(c1, c2, pos) >= 0 && NormalClip(c2, c3, pos) >= 0 &&
        NormalClip(c3, c0, pos) >= 0 && pCurrentFieldScriptActor->m12C_x6_stackDepth != 4) {
        pCurrentFieldScriptActor->m78_stack[pCurrentFieldScriptActor->m12C_x6_stackDepth] = pCurrentFieldScriptActor->mCC_scriptPC + 4;
        pCurrentFieldScriptActor->mCC_scriptPC = readU16FromScript(2);
        pCurrentFieldScriptActor->m12C_x6_stackDepth++;
        return;
    }
    fieldExectuteMaxCycles++;
    pCurrentFieldScriptActor->mCC_scriptPC += 4;
}

/* call u16 at pc+2 when the player is in trigger zone pc[1] (OP_CALL_IF_IN_TRIGGER with a height test) */
void OP_CC(void) {
    sFieldScriptEntity* player;
    s32 idx, y, pos, c0, c1, c2, c3;

    idx = pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 1];
    player = fieldSceneState.m290_actorArray[g_playerControlledActor].m4C_scriptEntity;
    y = FP_INT(player->m20_position.vy);
    if (fieldTriggerData[idx].m0_corner0[1] < y && y - (u16)player->m18_boundingVolume[1] < fieldTriggerData[idx].m0_corner0[1]) {
        pos = (FP_INT(player->m20_position.vz) << 16) + FP_INT(player->m20_position.vx);
        c0 = (fieldTriggerData[idx].m0_corner0[2] << 16) + fieldTriggerData[idx].m0_corner0[0];
        c1 = (fieldTriggerData[idx].m6_corner1[2] << 16) + fieldTriggerData[idx].m6_corner1[0];
        c2 = (fieldTriggerData[idx].mC_corner2[2] << 16) + fieldTriggerData[idx].mC_corner2[0];
        c3 = (fieldTriggerData[idx].m12_corner3[2] << 16) + fieldTriggerData[idx].m12_corner3[0];
        if (NormalClip(c0, c1, pos) >= 0 && NormalClip(c1, c2, pos) >= 0 && NormalClip(c2, c3, pos) >= 0 &&
            NormalClip(c3, c0, pos) >= 0 && pCurrentFieldScriptActor->m12C_x6_stackDepth != 4) {
            pCurrentFieldScriptActor->m78_stack[pCurrentFieldScriptActor->m12C_x6_stackDepth] = pCurrentFieldScriptActor->mCC_scriptPC + 4;
            pCurrentFieldScriptActor->mCC_scriptPC = readU16FromScript(2);
            pCurrentFieldScriptActor->m12C_x6_stackDepth++;
            return;
        }
    }
    fieldExectuteMaxCycles++;
    pCurrentFieldScriptActor->mCC_scriptPC += 4;
}

/* Noah OP_IF_PLAYER_IN_TRIGGER2: jump to u16 at pc+2 unless the player is in trigger zone pc[1] */
void OP_IF_PLAYER_IN_TRIGGER2(void) {
    sFieldScriptEntity* player;
    s32 idx, pos, c0, c1, c2, c3;

    player = fieldSceneState.m290_actorArray[g_playerControlledActor].m4C_scriptEntity;
    idx = pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 1];
    pos = (FP_INT(player->m20_position.vz) << 16) + FP_INT(player->m20_position.vx);
    c0 = (fieldTriggerData[idx].m0_corner0[2] << 16) + fieldTriggerData[idx].m0_corner0[0];
    c1 = (fieldTriggerData[idx].m6_corner1[2] << 16) + fieldTriggerData[idx].m6_corner1[0];
    c2 = (fieldTriggerData[idx].mC_corner2[2] << 16) + fieldTriggerData[idx].mC_corner2[0];
    c3 = (fieldTriggerData[idx].m12_corner3[2] << 16) + fieldTriggerData[idx].m12_corner3[0];
    if (NormalClip(c0, c1, pos) >= 0 && NormalClip(c1, c2, pos) >= 0 && NormalClip(c2, c3, pos) >= 0 &&
        NormalClip(c3, c0, pos) >= 0) {
        pCurrentFieldScriptActor->mCC_scriptPC += 4;
    } else {
        pCurrentFieldScriptActor->mCC_scriptPC = readU16FromScript(2);
        fieldExectuteMaxCycles++;
    }
}

/* Noah OP_IF_PLAYER_IN_TRIGGER: OP_IF_PLAYER_IN_TRIGGER2 with a height test */
void OP_IF_PLAYER_IN_TRIGGER(void) {
    sFieldScriptEntity* player;
    s32 idx, y, pos, c0, c1, c2, c3;

    idx = pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 1];
    player = fieldSceneState.m290_actorArray[g_playerControlledActor].m4C_scriptEntity;
    y = FP_INT(player->m20_position.vy);
    if (fieldTriggerData[idx].m0_corner0[1] < y && y - (u16)player->m18_boundingVolume[1] < fieldTriggerData[idx].m0_corner0[1]) {
        pos = (FP_INT(player->m20_position.vz) << 16) + FP_INT(player->m20_position.vx);
        c0 = (fieldTriggerData[idx].m0_corner0[2] << 16) + fieldTriggerData[idx].m0_corner0[0];
        c1 = (fieldTriggerData[idx].m6_corner1[2] << 16) + fieldTriggerData[idx].m6_corner1[0];
        c2 = (fieldTriggerData[idx].mC_corner2[2] << 16) + fieldTriggerData[idx].mC_corner2[0];
        c3 = (fieldTriggerData[idx].m12_corner3[2] << 16) + fieldTriggerData[idx].m12_corner3[0];
        if (NormalClip(c0, c1, pos) >= 0 && NormalClip(c1, c2, pos) >= 0 && NormalClip(c2, c3, pos) >= 0 &&
            NormalClip(c3, c0, pos) >= 0) {
            pCurrentFieldScriptActor->mCC_scriptPC += 4;
            return;
        }
    }
    pCurrentFieldScriptActor->mCC_scriptPC = readU16FromScript(2);
    fieldExectuteMaxCycles++;
}

/* Noah projectCharacterToScreen: screen position of the character of script byte 1 */
void projectCharacterToScreen(s32* x, s32* y) {
    SVECTOR v;
    MATRIX m;
    s32 sxy;
    s32 p;
    s32 flag;

    CompMatrix(&fieldSceneState.m1E4_projectionMatrix, &fieldSceneState.m290_actorArray[getCharacterToEntity(1)].m2C_matrixBackup, &m);
    v.vx = 0;
    v.vy = 0;
    v.vz = 0;
    SetRotMatrix(&m);
    SetTransMatrix(&m);
    RotTransPers(&v, &sxy, &p, &flag);
    *y = ((s16*)&sxy)[1];
    *x = ((s16*)&sxy)[0];
}

/* Noah OP_IF_CHARACTER_IN_VIEW */
void OP_IF_CHARACTER_IN_VIEW(void) {
    s32 x, y;

    projectCharacterToScreen(&x, &y);
    if (g_updateAllEntitiesSub2Var0 != 0) {
        pCurrentFieldScriptActor->mCC_scriptPC += 4;
    } else {
        if (y - 0x21U < 0x9F && x - 0x21U < 0xFF) {
            pCurrentFieldScriptActor->mCC_scriptPC += 4;
        } else {
            pCurrentFieldScriptActor->mCC_scriptPC = readU16FromScript(2);
        }
        g_breakCurrentScript = 1;
    }
}

/* jump to u16 at pc+2 unless the character of pc[1] projects inside the screen (1..0x13F, 1..0xDF) */
void OP_8A(void) {
    s32 x, y;

    projectCharacterToScreen(&x, &y);
    if (g_updateAllEntitiesSub2Var0 != 0) {
        pCurrentFieldScriptActor->mCC_scriptPC += 4;
    } else {
        if (y - 1U < 0xDF && x - 1U < 0x13F) {
            pCurrentFieldScriptActor->mCC_scriptPC += 4;
        } else {
            pCurrentFieldScriptActor->mCC_scriptPC = readU16FromScript(2);
        }
        g_breakCurrentScript = 1;
    }
}

/* jump to u16 at pc+4 unless the character of pc[1] is on walkmesh (imm/var at pc+2) */
void OPX_05(void) {
    if (readCharacter(1) != 0xFF) {
        sFieldScriptEntity* e = fieldSceneState.m290_actorArray[readCharacter(1)].m4C_scriptEntity;

        if (getImmediateOrVariableUnsigned(2) == e->m10_walkmeshId) {
            pCurrentFieldScriptActor->mCC_scriptPC += 6;
            return;
        }
    }
    pCurrentFieldScriptActor->mCC_scriptPC = readU16FromScript(4);
}

/* jump to u16 at pc+4 unless the walkmesh triangle material of the character of pc[1] is (imm/var at pc+2) */
void OPX_06(void) {
    if (readCharacter(1) != 0xFF) {
        sFieldScriptEntity* e = fieldSceneState.m290_actorArray[readCharacter(1)].m4C_scriptEntity;
        s32 mesh = e->m10_walkmeshId;
        s32 material = walkMeshTriangle[mesh][e->m8_currentWalkMeshTriangle[mesh]].mC_indexInWalkmeshData1;

        if (getImmediateOrVariableUnsigned(2) == material) {
            pCurrentFieldScriptActor->mCC_scriptPC += 6;
            return;
        }
    }
    pCurrentFieldScriptActor->mCC_scriptPC = readU16FromScript(4);
}

/* jump to u16 at pc+4 unless the character of pc[1] is closer than (imm/var at pc+2) */
void OP_89(void) {
    if (readCharacter(1) != 0xFF) {
        sFieldEntity* a = &fieldSceneState.m290_actorArray[readCharacter(1)];
        s32 dist = distance3d(FP_INT(pCurrentFieldEntity->m4C_scriptEntity->m20_position.vx) - FP_INT(a->m4C_scriptEntity->m20_position.vx),
                                 FP_INT(pCurrentFieldEntity->m4C_scriptEntity->m20_position.vy) - FP_INT(a->m4C_scriptEntity->m20_position.vy),
                                 FP_INT(pCurrentFieldEntity->m4C_scriptEntity->m20_position.vz) - FP_INT(a->m4C_scriptEntity->m20_position.vz));

        if (dist < getImmediateOrVariableUnsigned(2)) {
            pCurrentFieldScriptActor->mCC_scriptPC += 6;
            return;
        }
    }
    pCurrentFieldScriptActor->mCC_scriptPC = readU16FromScript(4);
}
