/* Field script opcodes. gcc-2.7.2-psx -G0, ASPSX 2.34. */
#include "common.h"
#include "psyq.h"
#include "field/field.h"
#include "main/memory.h"
#include "main/filesystem.h"
#include "main/game_state.h"
#include "main/debug_font.h"

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
extern u8 overlayStateSaveBlock[];                /* save block */
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
extern struct {
    /* 0x00 */ s32 m0_objectOffset[3];  /* Noah: fieldObjectRenderingVar2 */
    /* 0x0C */ u8 mC[10];  /* 0x10: noUpdatesToPartyMembers */
    /* 0x16 */ u8 m16_objectMoveFlags;     /* Noah: fieldObjectRenderingVar3 */
} partyToFieldEntityArrayMapping;
extern s32 currentParty[];

u16 readU16FromScript(s32);
s16 readS16FromScript(s32);
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
void OP_06(void) {
    if (pCurrentFieldScriptActor->m12C_x6_stackDepth != 4) {
        pCurrentFieldScriptActor->m78_stack[pCurrentFieldScriptActor->m12C_x6_stackDepth] = pCurrentFieldScriptActor->mCC_scriptPC + 5;
        pCurrentFieldScriptActor->mCC_scriptPC = readU16FromScript(1);
        pCurrentFieldScriptActor->m12C_x6_stackDepth++;
    } else {
        if (fieldDebugDisable == 0) {
            sprintf_screen("STACKERR ACT=%d\n", currentFieldActorId);
        }
        g_breakCurrentScript = 1;
    }
}

void OP_CALL(void) {
    if (pCurrentFieldScriptActor->m12C_x6_stackDepth != 4) {
        pCurrentFieldScriptActor->m78_stack[pCurrentFieldScriptActor->m12C_x6_stackDepth] = pCurrentFieldScriptActor->mCC_scriptPC + 3;
        pCurrentFieldScriptActor->mCC_scriptPC = readU16FromScript(1);
        pCurrentFieldScriptActor->m12C_x6_stackDepth++;
    } else {
        if (fieldDebugDisable == 0) {
            sprintf_screen("STACKERR ACT=%d\n", currentFieldActorId);
        }
        g_breakCurrentScript = 1;
    }
}

void OP_RETURN(void) {
    if (pCurrentFieldScriptActor->m12C_x6_stackDepth == 0) {
        if (fieldDebugDisable == 0) {
            sprintf_screen("STACKERR ACT=%d\n", currentFieldActorId);
        }
        pCurrentFieldScriptActor->m8C_scriptSlots[pCurrentFieldScriptActor->mCE_currentScriptSlot].m4_x12_priority = 0xF;
        pCurrentFieldScriptActor->m8C_scriptSlots[pCurrentFieldScriptActor->mCE_currentScriptSlot].m3_scriptIndex = 0xFF;
        currentScriptFinished = 1;
        g_breakCurrentScript = 1;
    } else {
        pCurrentFieldScriptActor->m12C_x6_stackDepth--;
        pCurrentFieldScriptActor->mCC_scriptPC = pCurrentFieldScriptActor->m78_stack[pCurrentFieldScriptActor->m12C_x6_stackDepth];
    }
}

