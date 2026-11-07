/* Field script opcodes. gcc-2.7.2-psx -G0, ASPSX 2.34. */
#include "common.h"
#include "pc_retail_heap.h"
#include "pc_observe.h"
#include "psyq.h"
#include "main/memory.h"
#include "main/filesystem.h"
#include "main/game_state.h"
#include "main/sprite.h"
#include "main/debug_font.h"
#include "field/field.h"
#include "field/screen_effects.h"

/* Integer part of a 16.16 fixed-point word. */
#define FP_INT(x) (((s16*)&(x))[1])

extern sFieldScriptEntity* pCurrentFieldScriptActor;
extern u8* pCurrentFieldScriptFile;
extern s16 fieldVars[];
extern s32 fieldDebugDisable;
extern s32 currentFieldActorId;
extern s32 g_breakCurrentScript;
extern s32 currentScriptFinished;
extern s32 fieldExectuteMaxCycles;
extern s32 fieldExecuteVar1;
extern s32 fieldExecuteVar2;
extern s32 fieldExecuteVar3;
extern s32 fieldChangePrevented;
extern void (*fieldScriptOpcodes[])(void);
extern s32 onlyUpdateDirector;
extern s32 g_totalActors;
extern s32 playerCanRun;
extern s32 numDialogWindowsCreatedThisFrame;
extern sFieldEntity* pCurrentFieldEntity;
extern u8 noUpdatesToPartyMembers;
extern s32 partyFieldActorIds[];
extern s32 disableParticles;
extern s32 fieldScriptEntityAlreadyInitialized;
extern GameState* pKernelGameState;
extern s32 fieldMapNumber;
extern s32 currentlyPlayingMusic;
extern u16 battleNonDefendTurnCount;
extern u8 menuReturnState0;
extern s16 sceneDIP;
extern u8* fieldSaveBlockCursor;                 /* save block cursor */
extern u8 overlayStateSaveBlock[0x8000];          /* save block: the PC saves bigger structs */
extern s32 savedPartyIsOnGear[3];
extern s32 gearSwitchingEnabled;
extern u8 kernelAndFieldStatesSynced;
extern u16 padButtonForScripts2;
extern u16 padButtonForScripts;                 /* Noah: padButtonForScripts[0].m0_buttons */
extern s32 D_8004F2F4;
extern s32 fieldScriptTimerTicks;
extern s32 fieldScriptTimerMode;
extern u8 g_playTimeSeconds;                  /* play time: seconds */
extern u8 g_playTimeMinutes;                  /* play time: minutes */
extern u8 g_playTimeHours;                  /* play time: hours */
extern s32 fieldScriptInitVar0;
extern void* partyCharacterBuffers[];             /* party character sprite bundles */
extern s32 partyGearActorIds[];
extern u8 backgroundPoly[];                /* 0x38 bytes of field state (save block +0x4) */
extern u8 fieldSceneState[];                /* 0x1C8 bytes (save block +0x758) */
extern s32 g_playerControlledActor;
extern FieldBss_800B2184 op9DVar1;
extern struct {
    /* 0x00 */ s32 m0_objectOffset[3];  /* Noah: fieldObjectRenderingVar2 */
    /* 0x0C */ u8 mC[10];  /* 0x10: noUpdatesToPartyMembers */
    /* 0x16 */ u8 m16_objectMoveFlags;     /* Noah: fieldObjectRenderingVar3 */
} partyToFieldEntityArrayMapping;
extern s32 currentParty[];
u16 readU16FromScript(s32);
s16 readS16FromScript(s32);
u16 getScriptEntryPoint(s32 entityId, s32 scriptIndex);
void setVarsForCurrentParty(void);
void setVar(s32 varIndex, s32 value);
s32 getVariable(s32 varIndex);
s32 getBytesSign(s32 varIndex);
void flushFrame(void);
s32 executeFieldScript(s32 maxCycles);
void applyPartyGearStateChanges(void);
void restoreOnGearScriptFlags(void);
void spriteActorSetSpecialAnimation(SpriteActorCore* actor, void* animation);
void spriteActorRestoreState(SpriteActorCore* actor, void* state);
void spriteActorSaveState(SpriteActorCore* actor, void* state);
void restoreFieldStateFromSaveBlock(void);
void setupScreenDistortion(s32);
void updateScriptActor3dRotation(s32);
u16 getPlayerCharacterDir(void);
void savePartyMemberPositionVars(s32);
void loadFieldStateFromSaveBlock(void);
void resetFieldScriptEntityGraphicEntity(s32 actorId, s32 vramSlot, void* setup, s32 clutBank, s32 clutX, s32 m126, s32 m134_4);
u16 getCameraDirection(void);
/* Opcode 0x06 (not in Noah): OP_CALL with a 5-byte instruction. */
/* The field state block, addressed through one base in this file. */
extern sFieldState D_800AFA54;
void copyScreenToVram(s32 x, s32 y);

void OP_92(void) {
    s32 i;

    for (i = 0; i < 8; i++) {
        pCurrentFieldScriptActor->m8C_scriptSlots[i].m2_delay = 0;
        pCurrentFieldScriptActor->m8C_scriptSlots[i].m4_x10_status = 0;
        pCurrentFieldScriptActor->m8C_scriptSlots[i].m4_x12_priority = 0xF;
        pCurrentFieldScriptActor->m8C_scriptSlots[i].m0_scriptPC = 0xFFFF;
        pCurrentFieldScriptActor->m8C_scriptSlots[i].m4_x16 = 0;
        pCurrentFieldScriptActor->m8C_scriptSlots[i].m3_scriptIndex = 0xFF;
        pCurrentFieldScriptActor->m8C_scriptSlots[i].m4_x0_numSteps = 0xFFFF;
        pCurrentFieldScriptActor->m8C_scriptSlots[i].m4_x17_walkMode = 0;
    }
    pCurrentFieldScriptActor->mCE_currentScriptSlot = 0;
    pCurrentFieldScriptActor->mCF_scriptSlotWaitedOn = 0;
    g_breakCurrentScript = 1;
    pCurrentFieldScriptActor->m84_dialogWindowFlags = 0;
    pCurrentFieldScriptActor->m12C_x6_stackDepth = 0;
}

void OP_4(void) {
    s32 i;

    for (i = 0; i < 8; i++) {
        if (pCurrentFieldScriptActor->m8C_scriptSlots[i].m4_x12_priority == 7) {
            pCurrentFieldScriptActor->m8C_scriptSlots[i].m0_scriptPC = getScriptEntryPoint(currentFieldActorId, 1);
        }
    }
    pCurrentFieldScriptActor->m8C_scriptSlots[pCurrentFieldScriptActor->mCE_currentScriptSlot].m4_x12_priority = 0xF;
    pCurrentFieldScriptActor->m8C_scriptSlots[pCurrentFieldScriptActor->mCE_currentScriptSlot].m3_scriptIndex = 0xFF;
    g_breakCurrentScript = 1;
}

void OP_STOP(void) {
    pCurrentFieldScriptActor->m8C_scriptSlots[pCurrentFieldScriptActor->mCE_currentScriptSlot].m4_x12_priority = 0xF;
    pCurrentFieldScriptActor->m8C_scriptSlots[pCurrentFieldScriptActor->mCE_currentScriptSlot].m3_scriptIndex = 0xFF;
    currentScriptFinished = 1;
    g_breakCurrentScript = 1;
}

/* The comparison cases are all "if (cond) result++; break;": GCC's
 * cross-jumping then folds the tails of cases 2, 3, 6, 8 and 9 into case 10's
 * test (so Noah/Ghidra see a shared "valueB != 0" test), while 6 and 9 stay
 * separate because they jump to the label cross-jumping created. */
void OP_JUMP_IF(void) {
    s32 valueB;
    s32 valueA;
    s32 result;

    valueB = 0;
    valueA = 0;
    switch (pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 5] & 0xF0) {
    case 0x00:
        valueA = getVariable(readU16FromScript(1));
        valueB = getVariable(readU16FromScript(3));
        if (getBytesSign(readU16FromScript(1)) != 0) {
            valueB &= 0xFFFF;
        } else {
            valueB = (s16)valueB;
        }
        break;
    case 0x40:
        valueA = getVariable(readU16FromScript(1));
        valueB = readS16FromScript(3);
        if (getBytesSign(readU16FromScript(1)) != 0) {
            valueB &= 0xFFFF;
        }
        break;
    case 0x80:
        valueA = readS16FromScript(1);
        valueB = getVariable(readU16FromScript(3));
        if (getBytesSign(readU16FromScript(3)) != 0) {
            valueA &= 0xFFFF;
        }
        break;
    case 0xC0:
        valueA = readS16FromScript(1);
        valueB = readS16FromScript(3);
        break;
    }

    result = 0;
    switch (pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 5] & 0xF) {
    case 0:
        if (valueA == valueB) {
            result++;
        }
        break;
    case 1:
        if (valueA != valueB) {
            result++;
        }
        break;
    case 2:
        if (valueA > valueB) {
            result++;
        }
        break;
    case 3:
        if (valueA < valueB) {
            result++;
        }
        break;
    case 4:
        if (valueA >= valueB) {
            result++;
        }
        break;
    case 5:
        if (valueA <= valueB) {
            result++;
        }
        break;
    case 6:
        if (valueA & valueB) {
            result++;
        }
        break;
    case 7:
        if (valueA != valueB) {
            result++;
        }
        break;
    case 8:
        if (valueA | valueB) {
            result++;
        }
        break;
    case 9:
        if (valueA & valueB) {
            result++;
        }
        break;
    case 10:
        if (~valueA & valueB) {
            result++;
        }
        break;
    }
    if (result == 1) {
        pCurrentFieldScriptActor->mCC_scriptPC += 8;
    } else {
        pCurrentFieldScriptActor->mCC_scriptPC = readU16FromScript(6);
    }
}

void OP_JUMP(void) {
    pCurrentFieldScriptActor->mCC_scriptPC = readU16FromScript(1);
}

void OP_INCREASE_FIELD_EXECUTION_MAX_CYCLES(void) {
    fieldExectuteMaxCycles += 0x20;
    pCurrentFieldScriptActor->mCC_scriptPC++;
}

/* Declared int but returns nothing: the return register counts as live at
 * the exit, which keeps GCC from stealing the loop head into the delay slot
 * of the loop branch. */
s32 executeFieldScript(s32 maxCycles) {
    s32 cycles;

    g_breakCurrentScript = 0;
    fieldExectuteMaxCycles = maxCycles;
    for (cycles = 0; cycles < fieldExectuteMaxCycles; cycles++) {
        if (cycles > 0x400) {
            if (fieldDebugDisable == 0) {
                sprintf_screen("EVENTLOOP ERROR ACT=%d\n", currentFieldActorId);
            }
            IMPLICIT_RETURN(0);
        }
        if (pcObserveOp) {
            s32 pc = pCurrentFieldScriptActor->mCC_scriptPC;
            s32 slot = pCurrentFieldScriptActor->mCE_currentScriptSlot & 7;
            s32 op = pCurrentFieldScriptFile[pc];
            pcObserveOp(fieldMapNumber, currentFieldActorId, slot,
                        pCurrentFieldScriptActor->m8C_scriptSlots[slot].m3_scriptIndex,
                        pc, op, op == 0xFE ? pCurrentFieldScriptFile[pc + 1] : -1);
        }
        fieldScriptOpcodes[pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC]]();
        if (currentScriptFinished == 0) {
            fieldExectuteMaxCycles = 0xFFFF;
        }
        if (fieldExecuteVar1 != 0) {
            if (fieldExecuteVar2 == 0) {
                IMPLICIT_RETURN(0);
            }
            if (fieldExecuteVar3 == 0) {
                IMPLICIT_RETURN(0);
            }
            if (fieldChangePrevented == 0) {
                IMPLICIT_RETURN(0);
            }
        }
        if (g_breakCurrentScript == 1 && currentScriptFinished == 1) {
            IMPLICIT_RETURN(0);
        }
    }
    FALLOFF_RETURN(0); /* result unused */
}

void exectueEntitiesUpdateFunction(void) {
    s32 numEntities;
    s32 i;
    s32 j;
    s32 maxPriority;
    sFieldEntity* e;
    sFieldScriptEntity* script;

    if (onlyUpdateDirector == 1) {
        numEntities = 1;
    } else {
        numEntities = g_totalActors;
    }
    playerCanRun = 0;
    numDialogWindowsCreatedThisFrame = 0;
    for (i = 0; i < numEntities; i++) {
        if ((D_800AFA54.mBC_actorArray[i].m58_flags & 0xF00) && !(D_800AFA54.mBC_actorArray[i].m4C_scriptEntity->m4_flags & 0x100000)) {
            e = &D_800AFA54.mBC_actorArray[i];
            if (fieldExecuteVar1 != 0) {
                if (fieldExecuteVar2 == 0) {
                    return;
                }
                if (fieldExecuteVar3 == 0) {
                    return;
                }
                if (fieldChangePrevented == 0) {
                    return;
                }
            }
            /* Matching hacks (no plain-C form found, see the near miss below):
             * retail reloads m4C here, but GCC 2.7.2 CSE (skip-blocks, after
             * loop) carries the load from the entity test across the checks
             * above, so the read is volatile; the flags are cleared through a
             * u32 (scalar) pointer so sched keeps that store ahead of the
             * global stores (as a struct field it may pass them). */
            script = *(sFieldScriptEntity* volatile*)&e->m4C_scriptEntity;
            *(u32*)script &= ~0x1000000;
            pCurrentFieldEntity = e;
            currentFieldActorId = i;
            pCurrentFieldScriptActor = script;
            maxPriority = 0xF;
            if (noUpdatesToPartyMembers != 0) {
                for (j = 0; j < 3; j++) {
                    if (partyFieldActorIds[j] != 0xFF && partyFieldActorIds[j] == i) {
                        goto next;
                    }
                }
            }
            for (j = 0; j < 8; j++) {
                if (pCurrentFieldScriptActor->m8C_scriptSlots[j].m4_x12_priority <= maxPriority) {
                    maxPriority = pCurrentFieldScriptActor->m8C_scriptSlots[j].m4_x12_priority;
                    pCurrentFieldScriptActor->mCE_currentScriptSlot = j;
                }
            }
            if (maxPriority == 0xF) {
                pCurrentFieldScriptActor->m8C_scriptSlots[0].m0_scriptPC = getScriptEntryPoint(i, 1);
                pCurrentFieldScriptActor->m8C_scriptSlots[0].m4_x12_priority = 7;
                pCurrentFieldScriptActor->mCE_currentScriptSlot = 0;
            }
            pCurrentFieldScriptActor->mCC_scriptPC = pCurrentFieldScriptActor->m8C_scriptSlots[pCurrentFieldScriptActor->mCE_currentScriptSlot].m0_scriptPC;
            currentScriptFinished = 1;
            if (!(pCurrentFieldScriptActor->m0_fieldScriptFlags & 1)) {
                /* executeFieldScript saves its $ra (the return address here) in
                 * the stack word under the field's wall-slide edge buffer
                 * (f_8008083C.c pcSlideEdgeStack). */
#if VERSION_JP
                pcSlideEdgeStack[0] = 0x800A17E8;
#else
                pcSlideEdgeStack[0] = 0x800A2258;
#endif
                executeFieldScript(8);
            }
            pCurrentFieldScriptActor->m8C_scriptSlots[pCurrentFieldScriptActor->mCE_currentScriptSlot].m0_scriptPC = pCurrentFieldScriptActor->mCC_scriptPC;
        }
    next:;
    }
}

/* Near miss: with these statements (store pCurrentFieldEntity first, so CSE
 * drops the m4C load of the entity test and reloads it after the checks, as
 * retail does) only the block after the field-change checks differs:
 * register choice (entity $a0 vs $a1, script $v1 vs $a0) and the order sched
 * gives the mask constant, the flag store and the global stores. */
#if 0

void exectueEntitiesUpdateFunction(void) {
    s32 numEntities;
    s32 i;
    s32 j;
    s32 maxPriority;

    if (onlyUpdateDirector == 1) {
        numEntities = 1;
    } else {
        numEntities = g_totalActors;
    }
    playerCanRun = 0;
    numDialogWindowsCreatedThisFrame = 0;
    for (i = 0; i < numEntities; i++) {
        if ((D_800AFA54.mBC_actorArray[i].m58_flags & 0xF00) && !(D_800AFA54.mBC_actorArray[i].m4C_scriptEntity->m4_flags & 0x100000)) {
        if (fieldExecuteVar1 != 0) {
            if (fieldExecuteVar2 == 0) {
                return;
            }
            if (fieldExecuteVar3 == 0) {
                return;
            }
            if (fieldChangePrevented == 0) {
                return;
            }
        }
        pCurrentFieldEntity = &D_800AFA54.mBC_actorArray[i];
        pCurrentFieldScriptActor = pCurrentFieldEntity->m4C_scriptEntity;
        currentFieldActorId = i;
        pCurrentFieldScriptActor->m0_fieldScriptFlags &= ~0x1000000;
        maxPriority = 0xF;
        if (noUpdatesToPartyMembers != 0) {
            for (j = 0; j < 3; j++) {
                if (partyFieldActorIds[j] != 0xFF && partyFieldActorIds[j] == i) {
                    goto next;
                }
            }
        }
        for (j = 0; j < 8; j++) {
            if (pCurrentFieldScriptActor->m8C_scriptSlots[j].m4_x12_priority <= maxPriority) {
                maxPriority = pCurrentFieldScriptActor->m8C_scriptSlots[j].m4_x12_priority;
                pCurrentFieldScriptActor->mCE_currentScriptSlot = j;
            }
        }
        if (maxPriority == 0xF) {
            pCurrentFieldScriptActor->m8C_scriptSlots[0].m0_scriptPC = getScriptEntryPoint(i, 1);
            pCurrentFieldScriptActor->m8C_scriptSlots[0].m4_x12_priority = 7;
            pCurrentFieldScriptActor->mCE_currentScriptSlot = 0;
        }
        pCurrentFieldScriptActor->mCC_scriptPC = pCurrentFieldScriptActor->m8C_scriptSlots[pCurrentFieldScriptActor->mCE_currentScriptSlot].m0_scriptPC;
        currentScriptFinished = 1;
        if (!(pCurrentFieldScriptActor->m0_fieldScriptFlags & 1)) {
            executeFieldScript(8);
        }
        pCurrentFieldScriptActor->m8C_scriptSlots[pCurrentFieldScriptActor->mCE_currentScriptSlot].m0_scriptPC = pCurrentFieldScriptActor->mCC_scriptPC;
        }
    next:;
    }
}

#endif

/* Runs script scriptIndex of actor 0 to completion on a scratch copy of its
 * script state, then restores the state. */
void runDirectorScriptImmediately(s32 scriptIndex) {
    sFieldScriptEntity* backup;
    s32 i;

    pCurrentFieldEntity = &D_800AFA54.mBC_actorArray[0];
    pCurrentFieldScriptActor = pCurrentFieldEntity->m4C_scriptEntity;
    backup = allocateHeap(PC_HEAP_SIZED(sizeof(sFieldScriptEntity), 0x138), 1);
    *backup = *pCurrentFieldEntity->m4C_scriptEntity;
    for (i = 0; i < 8; i++) {
        pCurrentFieldScriptActor->m8C_scriptSlots[i].m2_delay = 0;
        pCurrentFieldScriptActor->m8C_scriptSlots[i].m4_x10_status = 0;
        pCurrentFieldScriptActor->m8C_scriptSlots[i].m4_x12_priority = 0xF;
        pCurrentFieldScriptActor->m8C_scriptSlots[i].m0_scriptPC = 0xFFFF;
        pCurrentFieldScriptActor->m8C_scriptSlots[i].m4_x16 = 0;
        pCurrentFieldScriptActor->m8C_scriptSlots[i].m3_scriptIndex = 0xFF;
        pCurrentFieldScriptActor->m8C_scriptSlots[i].m4_x0_numSteps = 0xFFFF;
        pCurrentFieldScriptActor->m8C_scriptSlots[i].m4_x17_walkMode = 0;
    }
    currentFieldActorId = 0;
    fieldExecuteVar1 = 0;
    currentScriptFinished = 0;
    pCurrentFieldScriptActor->mCC_scriptPC = getScriptEntryPoint(0, scriptIndex);
    executeFieldScript(0xFFFF);
    fieldExecuteVar1 = 1;
    *pCurrentFieldEntity->m4C_scriptEntity = *backup;
    freeHeap(backup);
}

void runMenuReturnScript(void) {
    disableParticles = 1;
    runDirectorScriptImmediately(3);
    applyPartyGearStateChanges();
    disableParticles = 0;
}

void runFieldResumeScript(void) {
    s32 i;
    sFieldScriptEntity* pScript;
    VECTOR unused; /* only reserves stack */

    if (fieldScriptEntityAlreadyInitialized != 0) {
        runDirectorScriptImmediately(2);
        restoreOnGearScriptFlags();
        for (i = 0; i < op9DVar1.mE0_numMechas; i++) {
            mechaPlayAnimation(i, 0, op9DVar1.m60_mechaCurrentAnimation[i]);
        }
        pScript = D_800AFA54.mBC_actorArray[g_playerControlledActor].m4C_scriptEntity;
        i = pScript->m74_touchedActor;
        if (i != 0xFF) {
            pScript->m20_position.vy -= 8;
        }
        for (i = 0; i < D_800AFA54.mB8_numObjects; i++) {
            if (i < g_totalActors) {
                if (D_800AFA54.mBC_actorArray[i].m4C_scriptEntity->m12C_x0) {
                    continue;
                }
            } else if ((partyToFieldEntityArrayMapping.m16_objectMoveFlags & 0x7F) == 0) {
                D_800AFA54.mBC_actorArray[i].mC_matrix.t[0] += partyToFieldEntityArrayMapping.m0_objectOffset[0];
                D_800AFA54.mBC_actorArray[i].mC_matrix.t[1] += partyToFieldEntityArrayMapping.m0_objectOffset[1];
                D_800AFA54.mBC_actorArray[i].mC_matrix.t[2] += partyToFieldEntityArrayMapping.m0_objectOffset[2];
            }
            if ((partyToFieldEntityArrayMapping.m16_objectMoveFlags & 0x7F) == 1) {
                D_800AFA54.mBC_actorArray[i].mC_matrix.t[0] += partyToFieldEntityArrayMapping.m0_objectOffset[0];
                D_800AFA54.mBC_actorArray[i].mC_matrix.t[1] += partyToFieldEntityArrayMapping.m0_objectOffset[1];
                D_800AFA54.mBC_actorArray[i].mC_matrix.t[2] += partyToFieldEntityArrayMapping.m0_objectOffset[2];
            }
        }
    }
}

void reloadFieldActorsFromSaveBlock(void) {
    s32 i;
    sFieldScriptEntity* pScript;

    if (fieldScriptEntityAlreadyInitialized != 0) {
        for (i = 0; i < g_totalActors; i++) {
            setCurrentDirectory(4, 0);
            pScript = D_800AFA54.mBC_actorArray[i].m4C_scriptEntity;
            if (pScript->m124_special2dAnimationId != -1) {
                pCurrentFieldScriptActor = pScript;
                pCurrentFieldScriptActor->m120_special2dAnimation =
                    allocateHeap(PC_HEAP_BYTES(getFileSizeAligned(pCurrentFieldScriptActor->m124_special2dAnimationId) + 8), 0);
                readFile(pCurrentFieldScriptActor->m124_special2dAnimationId, pCurrentFieldScriptActor->m120_special2dAnimation, 0, 0x80);
                waitReadCompletion(0);
            }
        }
        for (i = 0; i < g_totalActors; i++) {
            if (D_800AFA54.mBC_actorArray[i].m4C_scriptEntity->m124_special2dAnimationId != -1) {
                spriteActorSetSpecialAnimation(D_800AFA54.mBC_actorArray[i].m4_pVramSpriteSheet,
                                               D_800AFA54.mBC_actorArray[i].m4C_scriptEntity->m120_special2dAnimation);
            }
        }
        restoreFieldStateFromSaveBlock();
        if (fieldScreenEffects.m0_screenDistortionRunning != 0) {
            setupScreenDistortion(1);
        }
        setVar(0x10, 0);
        setVarsForCurrentParty();
        for (i = 0; i < g_totalActors; i++) {
            updateScriptActor3dRotation(i);
        }
    }
}

/* Noah: startAllEntityScripts (the fieldScriptEntityAlreadyInitialized == 0 branch); the other branch
 * rebuilds the actors' graphics after loading a save. */
void startAllEntityScripts(void) {
    s32 i;

    if (fieldScriptEntityAlreadyInitialized != 0) {
        loadFieldStateFromSaveBlock();
        for (i = 0; i < g_totalActors; i++) {
            if (!(D_800AFA54.mBC_actorArray[i].m4C_scriptEntity->m126_spriteSource & 0x80)) {
                resetFieldScriptEntityGraphicEntity(i, D_800AFA54.mBC_actorArray[i].m4C_scriptEntity->m127_vramSlot,
                              partyCharacterBuffers[D_800AFA54.mBC_actorArray[i].m4C_scriptEntity->m126_spriteSource],
                              D_800AFA54.mBC_actorArray[i].m4C_scriptEntity->m130_x1C,
                              D_800AFA54.mBC_actorArray[i].m4C_scriptEntity->m134_x0,
                              D_800AFA54.mBC_actorArray[i].m4C_scriptEntity->m126_spriteSource,
                              D_800AFA54.mBC_actorArray[i].m4C_scriptEntity->m134_x4);
            } else {
                resetFieldScriptEntityGraphicEntity(i, D_800AFA54.mBC_actorArray[i].m4C_scriptEntity->m127_vramSlot,
                              (void*)(((s32*)D_800AFA54.mC8_actorSetupParams)[(D_800AFA54.mBC_actorArray[i].m4C_scriptEntity->m126_spriteSource & 0x7F) + 1] +
                                      (uptr)D_800AFA54.mC8_actorSetupParams),
                              D_800AFA54.mBC_actorArray[i].m4C_scriptEntity->m130_x1C,
                              D_800AFA54.mBC_actorArray[i].m4C_scriptEntity->m134_x0,
                              D_800AFA54.mBC_actorArray[i].m4C_scriptEntity->m126_spriteSource,
                              D_800AFA54.mBC_actorArray[i].m4C_scriptEntity->m134_x4);
                switch (D_800AFA54.mBC_actorArray[i].m4C_scriptEntity->m12C_x10) {
                case 1:
                    allocateSpriteVramLocations(D_800AFA54.mBC_actorArray[i].m4_pVramSpriteSheet, 2, 0);
                    D_800AFA54.mBC_actorArray[i].m4_pVramSpriteSheet->m7C_extra->m18_vramLocations[2] = D_800AFA54.mBC_actorArray[i].m4C_scriptEntity->m12C_x12;
                    D_800AFA54.mBC_actorArray[i].m4_pVramSpriteSheet->m7C_extra->m18_vramLocations[3] = D_800AFA54.mBC_actorArray[i].m4C_scriptEntity->m130_x0;
                    break;
                case 2:
                    allocateSpriteVramLocations(D_800AFA54.mBC_actorArray[i].m4_pVramSpriteSheet, 3, 0);
                    D_800AFA54.mBC_actorArray[i].m4_pVramSpriteSheet->m7C_extra->m18_vramLocations[2] = D_800AFA54.mBC_actorArray[i].m4C_scriptEntity->m12C_x12;
                    D_800AFA54.mBC_actorArray[i].m4_pVramSpriteSheet->m7C_extra->m18_vramLocations[3] = D_800AFA54.mBC_actorArray[i].m4C_scriptEntity->m130_x0;
                    D_800AFA54.mBC_actorArray[i].m4_pVramSpriteSheet->m7C_extra->m18_vramLocations[4] = D_800AFA54.mBC_actorArray[i].m4C_scriptEntity->m130_x9;
                    D_800AFA54.mBC_actorArray[i].m4_pVramSpriteSheet->m7C_extra->m18_vramLocations[5] = D_800AFA54.mBC_actorArray[i].m4C_scriptEntity->m130_x13;
                    break;
                }
            }
        }
        if (gearSwitchingEnabled != 0) {
            for (i = 0; i < 3; i++) {
                if (partyFieldActorIds[i] != 0xFF) {
                    if (pKernelGameState->m22B1_isOnGear[i] != 0) {
                        void* sprite;
                        sprite = D_800AFA54.mBC_actorArray[partyFieldActorIds[i]].m4_pVramSpriteSheet;
                        D_800AFA54.mBC_actorArray[partyFieldActorIds[i]].m4_pVramSpriteSheet = D_800AFA54.mBC_actorArray[partyGearActorIds[i]].m4_pVramSpriteSheet;
                        D_800AFA54.mBC_actorArray[partyGearActorIds[i]].m4_pVramSpriteSheet = sprite;
                        D_800AFA54.mBC_actorArray[partyGearActorIds[i]].m4C_scriptEntity->m0_fieldScriptFlags |= 0x200;
                        D_800AFA54.mBC_actorArray[partyGearActorIds[i]].m4C_scriptEntity->m0_fieldScriptFlags &= ~0x500;
                        D_800AFA54.mBC_actorArray[partyGearActorIds[i]].m58_flags |= 0x20;
                    } else {
                        D_800AFA54.mBC_actorArray[partyGearActorIds[i]].m4C_scriptEntity->m0_fieldScriptFlags |= 0x400;
                        D_800AFA54.mBC_actorArray[partyGearActorIds[i]].m4C_scriptEntity->m0_fieldScriptFlags &= ~0x300;
                    }
                }
            }
        }
    } else {
        setVar(0x10, 0);
        setVarsForCurrentParty();
        for (i = 0; i < g_totalActors; i++) {
            pCurrentFieldEntity = &D_800AFA54.mBC_actorArray[i];
            pCurrentFieldScriptActor = pCurrentFieldEntity->m4C_scriptEntity;
            currentFieldActorId = i;
            pCurrentFieldScriptActor->mCC_scriptPC = getScriptEntryPoint(i, 2);
            if (pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC] == 0) {
                pCurrentFieldScriptActor->m4_flags |= 0x4000000;
            }
            pCurrentFieldEntity = &D_800AFA54.mBC_actorArray[i];
            pCurrentFieldScriptActor = pCurrentFieldEntity->m4C_scriptEntity;
            currentFieldActorId = i;
            pCurrentFieldScriptActor->mCC_scriptPC = getScriptEntryPoint(i, 0);
        }
        for (i = 0; i < g_totalActors; i++) {
            pCurrentFieldEntity = &D_800AFA54.mBC_actorArray[i];
            pCurrentFieldScriptActor = pCurrentFieldEntity->m4C_scriptEntity;
            currentFieldActorId = i;
            fieldScriptInitVar0 = 0;
            currentScriptFinished = 0;
            executeFieldScript(0xFFFF);
            if (fieldScriptInitVar0 == 0) {
                u8* setup = D_800AFA54.mC8_actorSetupParams;
                resetFieldScriptEntityGraphicEntity(i, 0, (void*)(((s32*)setup)[1] + (uptr)setup), 0, 0, 0x80, 0);
                pCurrentFieldScriptActor->m4_flags |= 0x800;
            }
        }
    }
}

void OP_FF(void) {
    pCurrentFieldScriptActor->mCC_scriptPC++;
}

/* A word of the script header's sign mask. Before the first field loads,
 * the PS1 reads it through NULL: low BIOS RAM, whose bit the boot tests
 * (variable 0x50) is clear (PCSX-Redux). The PC has no page at 0. */
#define VAR_SIGN_MASK(off) (rawFieldScriptData != NULL ? *(u32*)(rawFieldScriptData->m0_unsignedVarMask + (off)) : 0)

/* Scripts can name variables past the 0x400 halfwords (field 489's movie
 * opcode, OPX_A0, takes 0xFE00 and 0xFE61 as variables): the PS1 reads and
 * writes the RAM after fieldVars (Redux reads 0 and 0x7777 there). The PC
 * reads 0 and drops the writes instead of leaving the array. */
static s16 pcFieldVarOutside;
#define FIELD_VAR(i) (*((u32)(i) < 0x400 ? &fieldVars[i] : (pcFieldVarOutside = 0, &pcFieldVarOutside)))

s32 getBytesSign(s32 arg0) {
    return -((VAR_SIGN_MASK((arg0 >> 6) * 4) & (1 << ((arg0 >> 1) & 0x1F))) != 0);
}

/* Noah: getVariable. A variable is unsigned when its bit in the script
 * header's sign mask is set. */
s32 getVariable(s32 varIndex) {
    s32 result;
    if (VAR_SIGN_MASK((varIndex >> 6) * 4) & (1 << ((varIndex >> 1) & 0x1F))) {
        result = (u16)FIELD_VAR(varIndex >> 1);
    } else {
        result = FIELD_VAR(varIndex >> 1);
    }
    return result;
}

/* Noah: setVar. Both arms store the same halfword; GCC's cross-jumping
 * merges them after register allocation, which is why the index sits in
 * $a2 (the sign-mask test that got deleted had used $v0-$a0). */
void setVar(s32 varIndex, s32 value) {
    if (VAR_SIGN_MASK((varIndex >> 6) * 4) & (1 << ((varIndex >> 1) & 0x1F))) {
        FIELD_VAR(varIndex >> 1) = (u16)value;
    } else {
        FIELD_VAR(varIndex >> 1) = value;
    }
}

u16 getScriptEntryPoint(s32 entityId, s32 scriptIndex) {
    u16* entryPoints = (u16*)rawFieldScriptData->m84_actors;
    return entryPoints[entityId * 0x20 + scriptIndex];
}

void setVarsForCurrentParty(void) {
    setVar(0x3E, currentParty[0]);
    setVar(0x40, currentParty[1]);
    setVar(0x42, currentParty[2]);
}

/* Noah: saveStateToKernel. */
void saveStateToKernel(void) {
    s32 i;

    pKernelGameState->m231A_fieldID = fieldMapNumber;
    pKernelGameState->m2322_previouslyPlayingMusic = currentlyPlayingMusic;
    pKernelGameState->m2320_worldmapMode = pKernelGameState->m1930_fieldVarsBackup[1];
    pKernelGameState->m231C_cameraYaw = pKernelGameState->m1930_fieldVarsBackup[4] << 9;
    setVar(0x44, battleNonDefendTurnCount);
    setVar(0x46, menuReturnState0);
    setVar(6, getPlayerCharacterDir());
    setVar(8, getCameraDirection());
    setVar(0x24, sceneDIP);
    setVar(0x3C, fieldMapNumber);
    setVarsForCurrentParty();
    for (i = 0; i < 0x200; i++) {
        pKernelGameState->m1930_fieldVarsBackup[i] = fieldVars[i];
    }
}

/* Noah: syncKernelAndFieldStates. */
s32 pcFieldOffTick(void); /* f_80072140.c */
void syncKernelAndFieldStates(void) {
    s32 i;
    s32 time;
    s32 seconds;
    s32 minutes;

    if (kernelAndFieldStatesSynced != 1) {
        padButtonForScripts2 |= padButtonForScripts;
        for (i = 0; i < 3; i++) {
            pKernelGameState->m1D34_currentParty[i] = currentParty[i];
        }
        saveStateToKernel();
        D_8004F2F4 = 0;
        if (!pcFieldOffTick()) { /* Enhanced (xg-hbc.19.3): the scripts' timer (var 0xA) counts native frames;
                                  * at 60 Hz it ran twice as fast */
            fieldScriptTimerTicks++;
        }
        for (i = 0; i < 3; i++) {
            if (pKernelGameState->m22B1_isOnGear[i] == 1) {
                savePartyMemberPositionVars(i);
            }
        }
        if (fieldScriptTimerTicks >= 0x1F) {
            fieldScriptTimerTicks = 0;
            if (!(fieldScriptTimerMode & 0x80)) {
                time = getVariable(0xA);
                seconds = time & 0xFF;
                minutes = (time >> 8) & 0xFF;
                if (!(fieldScriptTimerMode & 4)) {
                    if (seconds != 0xFF3B) {
                        seconds++;
                        if (seconds >= 0x3D) {
                            seconds = 0;
                            minutes++;
                        }
                    }
                } else if (seconds == 0) {
                    if (minutes != 0) {
                        seconds = 0x3B;
                        minutes--;
                    }
                } else {
                    seconds--;
                }
                setVar(0xA, (minutes << 8) | (seconds & 0xFF));
            }
        }
        setVar(0xC, g_playTimeSeconds | (g_playTimeMinutes << 8));
        setVar(0xE, g_playTimeHours);
        setVar(0x1E, FP_INT(D_800AFA54.mBC_actorArray[g_playerControlledActor].m4C_scriptEntity->m20_position.vx));
        setVar(0x20, FP_INT(D_800AFA54.mBC_actorArray[g_playerControlledActor].m4C_scriptEntity->m20_position.vz));
        setVar(0x22, FP_INT(D_800AFA54.mBC_actorArray[g_playerControlledActor].m4C_scriptEntity->m20_position.vy));
    }
}

/* Loads the field state from the save block overlayStateSaveBlock (cursor fieldSaveBlockCursor).
 * The copies are memcpy with a constant size, which GCC expands inline
 * (psyq.h's memcpy prototype is not the builtin's, hence __builtin_memcpy). */
void loadFieldStateFromSaveBlock(void) {
    s32 i;
    s32 flags;
    u8* setup;
    sFieldEntity* pEntity;

    fieldSaveBlockCursor = overlayStateSaveBlock;
    D_800AFA54.mB8_numObjects = *fieldSaveBlockCursor;
    fieldSaveBlockCursor += 4;
    fieldSaveBlockCursor = pcFieldSaveRange(fieldSaveBlockCursor, FIELD_SAVE_BACKGROUND, 1);
    __builtin_memcpy(&D_800AFA54, fieldSaveBlockCursor, sizeof(sFieldStateSaved));
    fieldSaveBlockCursor += 0x74;
    __builtin_memcpy(walkMeshVar1, fieldSaveBlockCursor, 0x400);
    fieldSaveBlockCursor += 0x400;
    fieldSaveBlockCursor = pcFieldSaveRange(fieldSaveBlockCursor, FIELD_SAVE_EFFECTS, 1);
    __builtin_memcpy(fieldSceneState, fieldSaveBlockCursor, 0x1C8);
    fieldSaveBlockCursor += 0x1C8;
    for (i = 0; i < g_totalActors; i++) {
        pEntity = &D_800AFA54.mBC_actorArray[i];
        __builtin_memcpy(&pEntity->m50_modelRotation, fieldSaveBlockCursor, 8);
        fieldSaveBlockCursor += 8;
        __builtin_memcpy(&flags, fieldSaveBlockCursor, 4);
        D_800AFA54.mBC_actorArray[i].m58_flags = flags;
        fieldSaveBlockCursor += 0x34;
        setup = (u8*)D_800AFA54.mBC_actorArray[i].m4C_scriptEntity->m118_deformationStrength;
        __builtin_memcpy(D_800AFA54.mBC_actorArray[i].m4C_scriptEntity, fieldSaveBlockCursor, sizeof(sFieldScriptEntity));
        D_800AFA54.mBC_actorArray[i].m4C_scriptEntity->m118_deformationStrength = (s32*)setup;
        fieldSaveBlockCursor += sizeof(sFieldScriptEntity);
        if (D_800AFA54.mBC_actorArray[i].m4C_scriptEntity->m134_x7) {
            D_800AFA54.mBC_actorArray[i].m4C_scriptEntity->m110_modelRotation2 = allocateHeap(PC_HEAP_BYTES(0xC), 0);
            __builtin_memcpy(D_800AFA54.mBC_actorArray[i].m4C_scriptEntity->m110_modelRotation2, fieldSaveBlockCursor, 0xC);
            fieldSaveBlockCursor += 0xC;
        }
        if (D_800AFA54.mBC_actorArray[i].m4C_scriptEntity->m12C_xC) {
            D_800AFA54.mBC_actorArray[i].m4C_scriptEntity->m114_movementBoundingZone = allocateHeap(PC_HEAP_BYTES(0x10), 0);
            __builtin_memcpy(D_800AFA54.mBC_actorArray[i].m4C_scriptEntity->m114_movementBoundingZone, fieldSaveBlockCursor, 0x10);
            fieldSaveBlockCursor += 0x10;
        }
    }
    __builtin_memcpy(fieldVars, fieldSaveBlockCursor, 0x800);
    fieldSaveBlockCursor += 0x800;
}

void restoreFieldStateFromSaveBlock(void) {
    s32 i;
    s32 partyChanged;
    sFieldScriptEntity* pScript;
    sFieldScriptEntity* pScript2;

    /* The saved field state (D_800AFA54 = 0x800AF880 + 0x1D4) and the object
     * count (+0x28C) are addressed from the FieldBss_800AF880 block base: a
     * destination with a nonzero offset from its symbol keeps its known
     * constant in CSE, so the block copy's alignment test stays
     * `or src,dest` as in retail (with &D_800AFA54 CSE swaps the operands). */
    fieldSaveBlockCursor = overlayStateSaveBlock;
    *(s32*)(fieldSceneState + 0x28C) = *fieldSaveBlockCursor;
    fieldSaveBlockCursor += 4 + pcFieldSaveRangeSize(FIELD_SAVE_BACKGROUND);
    __builtin_memcpy(fieldSceneState + 0x1D4, fieldSaveBlockCursor, 0x74);
    partyChanged = 0;
    fieldSaveBlockCursor += 0x74 + 0x400 + pcFieldSaveRangeSize(FIELD_SAVE_EFFECTS) + 0x1C8;
#if !VERSION_JP
    for (i = 0; i < 3; i++) {
        if (savedPartyIsOnGear[i] != pKernelGameState->m22B1_isOnGear[i]) {
            partyChanged++;
        }
    }
#endif
    for (i = 0; i < g_totalActors; i++) {
        pScript = D_800AFA54.mBC_actorArray[i].m4C_scriptEntity;
        fieldSaveBlockCursor += 0xC;
        if (pScript->m124_special2dAnimationId != -1 && pScript->mEA_forcedAnimation != 0xFF) {
            *(s16*)(fieldSaveBlockCursor + 0x14) = pScript->mEA_forcedAnimation;
        }
        pScript = (sFieldScriptEntity*)(i * sizeof(sFieldEntity) + (uptr)D_800AFA54.mBC_actorArray);
        pScript2 = ((sFieldEntity*)pScript)->m4C_scriptEntity;
        if (!(pScript2->m4_flags & 0x1000000)) {
#if VERSION_JP
            spriteActorRestoreState(((sFieldEntity*)pScript)->m4_pVramSpriteSheet, fieldSaveBlockCursor);
#else
            if (gearSwitchingEnabled == 0 || !(pScript2->m0_fieldScriptFlags & 0x600) || partyChanged == 0) {
                spriteActorRestoreState(((sFieldEntity*)pScript)->m4_pVramSpriteSheet, fieldSaveBlockCursor);
            }
#endif
        }
        fieldSaveBlockCursor += 0x30 + sizeof(sFieldScriptEntity); /* the sprite state and the script entity */
        if (D_800AFA54.mBC_actorArray[i].m4C_scriptEntity->m134_x7) {
            fieldSaveBlockCursor += 0xC;
        }
        if (D_800AFA54.mBC_actorArray[i].m4C_scriptEntity->m12C_xC) {
            fieldSaveBlockCursor += 0x10;
        }
    }
}

/* Near miss (6 words): restores the field state and the actors' sprite
 * states from the save block overlayStateSaveBlock, walking it with the cursor
 * fieldSaveBlockCursor. Differs only in register choice: the block copy's run-time
 * alignment test is "or src,dest" in retail and "or dest,src" here (CSE's
 * operand order), and the entity/script pointers swap $a0/$a1. */
#if 0

void restoreFieldStateFromSaveBlock(void) {
    s32 i;
    s32 partyChanged;
    sFieldScriptEntity* pScript;
    sFieldEntity* pEntity;

    fieldSaveBlockCursor = overlayStateSaveBlock;
    D_800AFA54.mB8_numObjects = *fieldSaveBlockCursor;
    fieldSaveBlockCursor += 0x3C;
    __builtin_memcpy(&D_800AFA54, fieldSaveBlockCursor, sizeof(sFieldStateSaved));
    partyChanged = 0;
    fieldSaveBlockCursor += 0x920;
    for (i = 0; i < 3; i++) {
        if (savedPartyIsOnGear[i] != pKernelGameState->m22B1[i]) {
            partyChanged++;
        }
    }
    for (i = 0; i < g_totalActors; i++) {
        pScript = D_800AFA54.mBC_actorArray[i].m4C_scriptEntity;
        fieldSaveBlockCursor += 0xC;
        if (pScript->m124_special2dAnimationId != -1 && pScript->mEA_forcedAnimation != 0xFF) {
            *(s16*)(fieldSaveBlockCursor + 0x14) = pScript->mEA_forcedAnimation;
        }
        pEntity = (sFieldEntity*)(i * sizeof(sFieldEntity) + (u32)D_800AFA54.mBC_actorArray);
        pScript = pEntity->m4C_scriptEntity;
        if (!(pScript->m4_flags & 0x1000000)) {
            if (gearSwitchingEnabled == 0 || !(pScript->m0_fieldScriptFlags & 0x600) || partyChanged == 0) {
                spriteActorRestoreState(pEntity->m4_pVramSpriteSheet, fieldSaveBlockCursor);
            }
        }
        fieldSaveBlockCursor += 0x30 + sizeof(sFieldScriptEntity); /* the sprite state and the script entity */
        if (D_800AFA54.mBC_actorArray[i].m4C_scriptEntity->m134_x7) {
            fieldSaveBlockCursor += 0xC;
        }
        if (D_800AFA54.mBC_actorArray[i].m4C_scriptEntity->m12C_xC) {
            fieldSaveBlockCursor += 0x10;
        }
    }
}

#endif

/* Saves the field state into the save block overlayStateSaveBlock (the inverse of
 * loadFieldStateFromSaveBlock). */
void saveFieldStateToSaveBlock(void) {
    s32 i;
    s32 flags;
    s32 size;
    sFieldEntity* pEntity;

    fieldSaveBlockCursor = overlayStateSaveBlock;
    *fieldSaveBlockCursor = D_800AFA54.mB8_numObjects;
    fieldSaveBlockCursor += 4;
    fieldSaveBlockCursor = pcFieldSaveRange(fieldSaveBlockCursor, FIELD_SAVE_BACKGROUND, 0);
    __builtin_memcpy(fieldSaveBlockCursor, &D_800AFA54, sizeof(sFieldStateSaved));
    fieldSaveBlockCursor += 0x74;
    __builtin_memcpy(fieldSaveBlockCursor, walkMeshVar1, 0x400);
    fieldSaveBlockCursor += 0x400;
    fieldSaveBlockCursor = pcFieldSaveRange(fieldSaveBlockCursor, FIELD_SAVE_EFFECTS, 0);
    __builtin_memcpy(fieldSaveBlockCursor, fieldSceneState, 0x1C8);
    fieldSaveBlockCursor += 0x1C8;
    for (i = 0; i < g_totalActors; i++) {
        pEntity = &D_800AFA54.mBC_actorArray[i];
        __builtin_memcpy(fieldSaveBlockCursor, &pEntity->m50_modelRotation, 8);
        flags = D_800AFA54.mBC_actorArray[i].m58_flags;
        fieldSaveBlockCursor += 8;
        __builtin_memcpy(fieldSaveBlockCursor, &flags, 4);
        fieldSaveBlockCursor += 4;
        spriteActorSaveState(D_800AFA54.mBC_actorArray[i].m4_pVramSpriteSheet, fieldSaveBlockCursor);
        fieldSaveBlockCursor += 0x30;
        __builtin_memcpy(fieldSaveBlockCursor, D_800AFA54.mBC_actorArray[i].m4C_scriptEntity, sizeof(sFieldScriptEntity));
        fieldSaveBlockCursor += sizeof(sFieldScriptEntity);
        if (D_800AFA54.mBC_actorArray[i].m4C_scriptEntity->m134_x7) {
            __builtin_memcpy(fieldSaveBlockCursor, D_800AFA54.mBC_actorArray[i].m4C_scriptEntity->m110_modelRotation2, 0xC);
            fieldSaveBlockCursor += 0xC;
        }
        if (D_800AFA54.mBC_actorArray[i].m4C_scriptEntity->m12C_xC) {
            __builtin_memcpy(fieldSaveBlockCursor, D_800AFA54.mBC_actorArray[i].m4C_scriptEntity->m114_movementBoundingZone, 0x10);
            fieldSaveBlockCursor += 0x10;
        }
    }
    __builtin_memcpy(fieldSaveBlockCursor, fieldVars, 0x800);
    fieldSaveBlockCursor += 0x800;
#if !VERSION_JP
    /* US only */
    for (i = 0; i < 3; i++) {
        savedPartyIsOnGear[i] = pKernelGameState->m22B1_isOnGear[i];
    }
#endif
    /* the base lands in $s1: it is the loop counter's variable (32 bits
     * suffice: only the difference, the printed size, is used) */
    i = (s32)(sptr)overlayStateSaveBlock;
    size = (s32)(sptr)fieldSaveBlockCursor;
    if (fieldDebugDisable == 0) {
        size -= i;
        sprintf_screen("SAVESIZE=%d %x\n", size, size);
    }
}

void copyScreenToTransitionStrips(void) {
    copyScreenToVram(0x2C0, 0x100);
}

void copyScreenToVram(s32 x, s32 y) {
    RECT rect;
    rect.y = 0;
    rect.x = 0;
    rect.w = 0x140;
    rect.h = 0xE0;
    SetGeomScreen(0x200);
    MoveImage(&rect, x, y);
    flushFrame();
}
