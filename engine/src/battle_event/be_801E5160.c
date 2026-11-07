/* The scripted-battle bytecode interpreter (Noah battleEvent.cpp): event
 * file loading, the update loop (76 opcodes, 4 per entity per frame), the
 * opcode handlers and the portrait dialog box. 0x801E5160-0x801E93E8 (JP
 * to 0x801E9420); gcc-2.6.3-psx, ASPSX 2.34. */
#include "common.h"
#include "main/kernel.h"
#include "psyq.h"
#include "battle/battle.h"
#include "pc_convert.h"
#include "main/game_state.h"
#include "main/memory.h"
#include "main/filesystem.h"
#include "main/dialog.h"
#include "battle_event/battle_event.h"
#include "pc_gameplay.h"
extern s32 g_playTimeInVsync; /* main: play time in vsyncs */

/* A little-endian u16 operand at bc[i] of the event bytecode. */
#define BC_U16(bc, i) (((bc)[(i) + 1] << 8) | (bc)[i])
/* The script variable whose byte offset is the operand at bc[i]. */
#define VAR(bc, i) battleEventVar0->m394_vars[BC_U16(bc, i) >> 1 & 0x7FFF]

extern u8 currentBattleMode;
extern u8 battleEventIsRunningFlag;
extern u8 battleEventEndBattleFlag;
extern u8 D_800D2C31[];
extern s32 battleDialogCursorSpriteIndex;
extern u16 battleEventDefaultDialogParams[];
extern u8 battleEventReturnMode;
extern u8 battleInitVar0;
extern u8 battleTransitionEffect;
extern u8 battleEventMenuEnabled;
extern u8 battleEventMoviePending;
extern u16 movieRequestEndFrame;
extern u8* battleEventFile3;
extern u8 battleMusic[];
extern u8 battleEventInitVar1[];
extern u8 isBattleAnEvent;
extern s32 battleDialogWindowX2;
extern s32 battleDialogWindowY2;
extern u8 battleInputButton;
extern u8 battleDialogPortraitsTable[];   /* Noah: battleDialogPortraitsTable ([portrait][flip]) */
extern DialogWindow18* battleEventVar1;
extern u8* battleEventVarDialogueBundle;
extern u8 battleRunningVar1;     /* BattleBss_800C3EB0.mA3A_battleRunningVar1 */

u16 bitmaskCharacterCheck(u16 mask, u8 i);
s32 battleSetupStringInPolyFT4Large(s32 character, POLY_FT4 (*polys)[2], s16 x, s16 y);
void battleEventDestroySpriteTaskCallback(void*);
void setupMechaForEvent(u16 slot, u16 target, s32 anim);
void waitBattleAnimationSpriteLoading(void);
void waitBattleAnimationSoundLoaded(void);
void startBattleCameraShake(SVECTOR* target, s32 duration);
void* battleEvent_createSpriteEntity(void* bundle, SVECTOR* pos);
void battleEvent_setSpriteAnimation(void* task, s32 anim);
void resetBattleAudioState(void);
void battleRenderDebugAndMain(void);
void openMusicBattleFolder(void);
void* allocateBattleHeap(s32 size, s32 flags);
void idleBattleDuringLoading(void);
void* realStartBattleMusic(void* seq, s32 vol, s32 b);
void playSoundEffectInFreeSlotVolPan(s32 id, s32 vol, s32 pan);
void setSoundEffectVolume(s32 id, s32 a);
void callGearRelocateSlot(u8);
void markEnemyDead(u8);
void performAttackSub2(void);
void clearBattleAnimationData(void);
void setupGearTurnHud(u8 entity);
void freeBattleRenderStruct2(u8 entity);
void playBattleSoundFileAndWait(s32 arg0, s32 arg1);
void stopSequence(void* seq);
void loadSequence(void* seq);
void setCurrentDir_20_0(void);
void unloadSequence(void* seq);
void battleEventResetSpriteDisposeState(void);

void battleEvent_loadArgs(u8* bc, u8 count, u8 mask, u8 mode);
u8 battleEvent_compareValues(s16 a, s16 b, u8 cmp);
u16 battleEvent_findFreeScriptSlot(sBattleRunningVar0Sub* p);
s32 battleEvent_startScriptSlot(s32 id, u8* bc);
s32 battleEvent_resolveEntityIndex(s32 arg);
void battleEventLoadMusicFile(s16 musicId, u8 volume);
void battleEventSetLoadedMusicVolume(s32 vol, s32 b);
u8 battleEvent_DisplayDialog(u16 msg, u8 portrait, u16 flags);
void setupBattleDialogCursorSpritePoly(s32 x, s32 y);
void battleDialogLoadPortrait(u8 portrait, u16 flags, u16 x, u16 y, u16 width);

/* The battle event code passes setupWindowSize2's coordinates as ints (no
 * sign extension), as the field code does (src/field/f_8007DCF8.c). */
#define SETUP_WINDOW_SIZE2 ((void (*)(DialogWindow18*, s32, s32, s32, s32, s32, s32))setupWindowSize2)
void battleEventDestroyScriptEntitySprite(s32 id, u8* bc);


void battleEventEntry(void) {
    LoadingBatchCommand cmds[3];
    u8* file2;
    sBattleEventData* data;
    s32 i;
    s32 j;

    battleRenderDebugAndMain();
    setCurrentDir_20_0();
    file2 = allocateBattleHeap(getFileSizeAligned(2), 1);
    cmds[0].m0_fileIndex = 2;
    cmds[0].m4_loadPtr = file2;
    battleEventFile3 = allocateBattleHeap(getFileSizeAligned(3), 0);
    cmds[1].m0_fileIndex = 3;
    cmds[1].m4_loadPtr = battleEventFile3;
    cmds[2].m0_fileIndex = 0;
    cmds[2].m4_loadPtr = NULL;
    batchStartLoadingFiles(cmds, 0, 0x80);
    idleBattleDuringLoading();
    /* Both tables stay as loaded; their entries are read once each. */
    data = (sBattleEventData*)mallocAndDecompress(PC_RELOC_ENTRY(file2, D_8006F9DF.m0_eventDataIndex * 2 + 1), 0);
    battleEventVarDialogueBundle = mallocAndDecompress(PC_RELOC_ENTRY(file2, D_8006F9DF.m0_eventDataIndex * 2 + 2), 0);
    freeHeap(file2);
    battleEventVar0 = allocateBattleHeap((s32)sizeof(sBattleRunningVar0), 0);
    bzero(battleEventVar0, (s32)sizeof(sBattleRunningVar0));
    battleEventVar1 = allocateBattleHeap((s32)sizeof(DialogWindow18) + 8, 0);
    bzero(battleEventVar1, (s32)offsetof(DialogWindow18, m70_vars[2]));
    battleEventVar2 = data;
    battleEventVar0->m390_scriptByteCode = (u8*)(data->m40_count * 16 + (uptr)data + 0x44);
    for (i = 0; i < 16; i++) {
        battleEventVar0->m794_executionOrder[i] = 0xFF;
    }
    for (i = 0; i < battleEventVar2->m40_count; i++) {
        for (j = 0; j < 8; j++) {
            battleEventVar0->m0_scriptEntities[i].m0_scriptBytecodeOffset[j] = 0xFFFF;
            battleEventVar0->m0_scriptEntities[i].m10_isScriptSlotActive[j] = 0xFF;
            battleEventVar0->m0_scriptEntities[i].m18_slotEntryPoints[j] = 0xFF;
        }
        battleEventVar0->m794_executionOrder[i] = i;
        battleEventVar0->m0_scriptEntities[i].m0_scriptBytecodeOffset[0] = battleEventVar2->m44_scriptBytecodeEntryPoints[i][0];
        battleEventVar0->m0_scriptEntities[i].m10_isScriptSlotActive[0] = 0;
        battleEventVar0->m0_scriptEntities[i].m22_priority = 0xFF;
        battleEventVar0->m0_scriptEntities[i].m18_slotEntryPoints[0] = 0;
        battleEventVar0->m0_scriptEntities[i].m20_currentActiveEntry = 0;
        battleEventVar0->m0_scriptEntities[i].m23_calledEntryPoint = 0xFF;
        battleEventVar0->m0_scriptEntities[i].m24_portrait = 0xFF;
    }
    battleEventVar0->m7F5 = 4;
    for (i = 0; i < 5; i++) {
        battleEventVar0->m7F6_dialogParams[i] = battleEventDefaultDialogParams[i];
    }
    for (i = 0; i < 2; i++) {
        SetPolyFT4(&battleEventVar0->m7A4_portraitPolys[i]);
        (battleEventVar0->m7A4_portraitPolys + i)->r0 = 0x80;
        (battleEventVar0->m7A4_portraitPolys + i)->g0 = 0x80;
        (battleEventVar0->m7A4_portraitPolys + i)->b0 = 0x80;
        SetSemiTrans(&battleEventVar0->m7A4_portraitPolys[i], 0);
        SetShadeTex(&battleEventVar0->m7A4_portraitPolys[i], 1);
        battleEventVar0->m7A4_portraitPolys[i].clut = GetClut(0, 0x1D0);
        battleEventVar0->m7A4_portraitPolys[i].tpage = GetTPage(1, 0, 0x3C0, 0x100);
    }
    battleVar1->mCA_battleEventPoly = 1;
    battleVar1->mCF_isWaitingForInput = 0;
    for (i = 0; i < 16; i++) {
        battleEventVar0->m804_isMoving[i] = 0;
        battleEventInitVar1[i] = 0;
    }
    battleEventVar0->m81F_hasLoadedMusic = 0;
#if VERSION_JP
    /* JP loads the event sound sequence as file 3 of the other directory. */
    openMusicBattleFolder();
    battleEventVar0->m818_eventSeq = allocateBattleHeap(getFileSizeAligned(3), 0);
    readFile(3, battleEventVar0->m818_eventSeq, 0, 0x80);
#else
    setCurrentDir_20_0();
    battleEventVar0->m818_eventSeq = allocateBattleHeap(getFileSizeAligned(4), 0);
    readFile(4, battleEventVar0->m818_eventSeq, 0, 0x80);
#endif
    idleBattleDuringLoading();
    loadSequence(battleEventVar0->m818_eventSeq);
    waitForMusic(0x10);
    battleEventVar0->m820_isSeqLoaded = 1;
    gCurrentBattleEventSeq = battleEventVar0->m818_eventSeq;
    waitBattleAnimationSoundLoaded();
}

s32 battleEventFree(void) {
    s32 ret = 0;
    freeHeap(battleEventVar0);
    freeHeap(battleEventVar1);
    freeHeap(battleEventVar2);
    freeHeap(battleEventVarDialogueBundle);
    freeHeap(battleEventFile3);
    if (battleEventVar0->m81F_hasLoadedMusic) {
        ret = 1;
        startMusicInstanceSub0(pCurrentBattleMusic);
        battleRenderDebugAndMain();
        freeSoundInstance(pCurrentBattleMusic);
        battleRenderDebugAndMain();
    }
    if (battleEventVar0->m820_isSeqLoaded) {
        stopSequence(battleEventVar0->m818_eventSeq);
        unloadSequence(battleEventVar0->m818_eventSeq);
        battleRenderDebugAndMain();
        freeHeap(battleEventVar0->m818_eventSeq);
        battleEventVar0->m820_isSeqLoaded = 0;
    }
    return ret;
}

u16 battleEventGetBytecodeOffset(sBattleRunningVar0Sub* p) {
    s32 i;
    s32 found;
    u8 inactive = 0xFF;
    for (i = 0; i < 8; i++) {
        if (p->m10_isScriptSlotActive[i] < inactive) {
            found = i;
        }
    }
    p->m20_currentActiveEntry = found;
    p->m21_currentEntryPoint = p->m18_slotEntryPoints[found];
    return p->m0_scriptBytecodeOffset[found];
}

u16 battleEvent_findFreeScriptSlot(sBattleRunningVar0Sub* p) {
    s32 i;
    for (i = 1; i < 8; i++) {
        if (p->m10_isScriptSlotActive[i] == 0xFF) {
            break;
        }
    }
    return i;
}

/* Noah battleEvent_loadArgs: reads count operand words (at bc + 1 + 2i) into
 * m380_currentArgs. mode != 0: bit 15 set = literal (low 15 bits), else a
 * script variable (byte offset); mode == 0: bit (7 - i) of mask picks literal
 * or variable. The loop shape (ofs set in the mode branch, p = bc + i + i
 * after i++) reproduces retail's loop.c giv order. */
void battleEvent_loadArgs(u8* bc, u8 count, u8 mask, u8 mode) {
    s32 i;
    s32 ofs;
    u8* p;
    i = 0;
    if (i < count) {
        p = bc;
        do {
            if (mode != 0) {
                s16 v;
                ofs = i * 2;
                v = p[1] + (p[2] << 8);
                if (v & 0x8000) {
                    ((sBattleRunningVar0*)(ofs + (sptr)battleEventVar0))->m380_currentArgs[0] = v & 0x7FFF;
                } else {
                    ((sBattleRunningVar0*)(ofs + (sptr)battleEventVar0))->m380_currentArgs[0] = battleEventVar0->m394_vars[(s16)((s16)v / 2)];
                }
            } else if ((mask << i) & 0x80) {
                battleEventVar0->m380_currentArgs[i] = p[1] + (p[2] << 8);
            } else {
                battleEventVar0->m380_currentArgs[i] = VAR(p, 1);
            }
            i++;
            p = bc + i + i;
        } while (i < count);
    }
}

u8 battleEvent_compareValues(s16 a, s16 b, u8 cmp) {
    u8 ret = 0;
    switch (cmp & 0xF) {
    case 0:
        if (a == b) ret = 1;
        break;
    case 1:
        if (a != b) ret = 1;
        break;
    case 2:
        if (a > b) ret = 1;
        break;
    case 3:
        if (a < b) ret = 1;
        break;
    case 4:
        if (a >= b) ret = 1;
        break;
    case 5:
        if (a <= b) ret = 1;
        break;
    case 6:
        if ((s16)(a & b)) ret = 1;
        break;
    case 7:
        if (a != b) ret = 1;
        break;
    case 8:
        if ((s16)(a | b)) ret = 1;
        break;
    case 9:
        if (bitmaskCharacterCheck(a, b)) ret = 1;
        break;
    case 10:
        if (!bitmaskCharacterCheck(a, b)) ret = 1;
        break;
    }
    return ret;
}

s32 battleEvent_resolveEntityIndex(s32 arg) {
    s32 i;
    s32 ret = 0;
    u8 c = arg;
    if (c < 0x10) {
        for (i = 0; i < 3; i++) {
            if (battleCharacters[i] != 0xFF && battleCharacters[i] == c) {
                ret = i;
                break;
            }
        }
    } else {
        ret = arg - 0xD;
    }
    return (u8)ret;
}

void setupBattleDialogCursorSpritePoly(s32 x, s32 y) {
    s32 tx;
    s32 ty;
    battleDialogCursorSpriteIndex--;
    if (battleDialogCursorSpriteIndex < 0) {
        battleDialogCursorSpriteIndex = 4;
    }
    battleVar1->m100_countOf27C8 = battleSetupStringInPolyFT4Large(battleDialogCursorSpriteIndex + 0xE0, BATTLE_VAR0_M27C8(battleVar0), x, y);
    tx = BATTLE_VAR0_M27C8(battleVar0)[0][BATTLE_ODD_OR_EVEN].x1;
    ty = BATTLE_VAR0_M27C8(battleVar0)[0][BATTLE_ODD_OR_EVEN].y1;
    BATTLE_VAR0_M27C8(battleVar0)[0][BATTLE_ODD_OR_EVEN].x1 = BATTLE_VAR0_M27C8(battleVar0)[0][BATTLE_ODD_OR_EVEN].x2;
    BATTLE_VAR0_M27C8(battleVar0)[0][BATTLE_ODD_OR_EVEN].y1 = BATTLE_VAR0_M27C8(battleVar0)[0][BATTLE_ODD_OR_EVEN].y2;
    BATTLE_VAR0_M27C8(battleVar0)[0][BATTLE_ODD_OR_EVEN].x2 = tx;
    BATTLE_VAR0_M27C8(battleVar0)[0][BATTLE_ODD_OR_EVEN].y2 = ty;
    battleVar1->mA7_27C8OddOrEven = BATTLE_ODD_OR_EVEN;
    battleVar1->m9E_render27C8 = 1;
}

s32 battleEvent_OP0_endScript(s32 id) {
    battleEventVar0->m0_scriptEntities[id].m18_slotEntryPoints[battleEventVar0->m0_scriptEntities[id].m20_currentActiveEntry] = 0xFF;
    battleEventVar0->m0_scriptEntities[id].m10_isScriptSlotActive[battleEventVar0->m0_scriptEntities[id].m20_currentActiveEntry] = 0xFF;
    battleEventVar0->m0_scriptEntities[id].m0_scriptBytecodeOffset[battleEventVar0->m0_scriptEntities[id].m20_currentActiveEntry] = 0xFFFF;
    battleEventVar0->m0_scriptEntities[id].m18_slotEntryPoints[0] = 1;
    battleEventVar0->m0_scriptEntities[id].m20_currentActiveEntry = 0;
    battleEventVar0->m0_scriptEntities[id].m10_isScriptSlotActive[0] = 7;
    battleEventVar0->m0_scriptEntities[id].m0_scriptBytecodeOffset[0] = battleEventVar2->m44_scriptBytecodeEntryPoints[id][1];
    battleEventVar0->m0_scriptEntities[id].m23_calledEntryPoint = 0xFF;
    return 0;
}

s32 battleEvent_OP1_jump(s32 id, u8* bc) {
    battleEventVar0->m0_scriptEntities[id].m0_scriptBytecodeOffset[battleEventVar0->m0_scriptEntities[id].m20_currentActiveEntry] = bc[1] + (bc[2] << 8);
    return 0;
}

s32 battleEvent_OP2_jumpIf(s32 id, u8* bc) {
    battleEvent_loadArgs(bc, 2, bc[5], 0);
    if (battleEvent_compareValues(battleEventVar0->m380_currentArgs[0], battleEventVar0->m380_currentArgs[1], bc[5]) == 0) {
        battleEventVar0->m0_scriptEntities[id].m0_scriptBytecodeOffset[battleEventVar0->m0_scriptEntities[id].m20_currentActiveEntry] = bc[6] + (bc[7] << 8);
        return 0;
    }
    return 8;
}

s32 battleEvent_startScriptSlot(s32 id, u8* bc) {
    s32 ret = 0;
    u8 slot = battleEvent_findFreeScriptSlot(&battleEventVar0->m0_scriptEntities[bc[1]]);
    if (slot != 8) {
        battleEventVar0->m0_scriptEntities[id].m23_calledEntryPoint = bc[2] & 0x1F;
        battleEventVar0->m0_scriptEntities[bc[1]].m10_isScriptSlotActive[slot] = bc[2] >> 5;
        battleEventVar0->m0_scriptEntities[bc[1]].m0_scriptBytecodeOffset[slot] = battleEventVar2->m44_scriptBytecodeEntryPoints[bc[1]][battleEventVar0->m0_scriptEntities[id].m23_calledEntryPoint];
        battleEventVar0->m0_scriptEntities[bc[1]].m18_slotEntryPoints[slot] = battleEventVar0->m0_scriptEntities[id].m23_calledEntryPoint;
        ret = 3;
    }
    return ret;
}

s32 battleEvent_OP4_waitScriptDone(s32 id, u8* bc) {
    s32 ret = 0;
    if (battleEventVar0->m0_scriptEntities[id].m23_calledEntryPoint != (bc[2] & 0x1F)) {
        battleEvent_startScriptSlot(id, bc);
    } else if (battleEventVar0->m0_scriptEntities[id].m23_calledEntryPoint == battleEventVar0->m0_scriptEntities[bc[1]].m21_currentEntryPoint) {
        battleEventVar0->m0_scriptEntities[id].m23_calledEntryPoint = 0xFF;
        ret = 3;
    }
    return ret;
}

s32 battleEvent_OP5_waitScript(s32 id, u8* bc) {
    s32 ret = 0;
    u8 flag = 1;
    s32 i;
    if (battleEventVar0->m0_scriptEntities[id].m23_calledEntryPoint != (bc[2] & 0x1F)) {
        battleEvent_startScriptSlot(id, bc);
    } else {
        for (i = 0; i < 8; i++) {
            if (battleEventVar0->m0_scriptEntities[bc[1]].m18_slotEntryPoints[i] == battleEventVar0->m0_scriptEntities[id].m23_calledEntryPoint) {
                flag = 0;
                break;
            }
        }
        if (flag) {
            if (battleEventVar0->m0_scriptEntities[id].m23_calledEntryPoint != battleEventVar0->m0_scriptEntities[bc[1]].m21_currentEntryPoint) {
                ret = 3;
                battleEventVar0->m0_scriptEntities[id].m23_calledEntryPoint = 0xFF;
            }
        }
    }
    return ret;
}

s32 battleEvent_OP6_setVar(s32 id, u8* bc) {
    battleEvent_loadArgs(bc, 2, bc[5], 0);
    VAR(bc, 1) = battleEventVar0->m380_currentArgs[1];
    return 6;
}

s32 battleEvent_OP7_setVarTrue(s32 id, u8* bc) {
    VAR(bc, 1) = 1;
    return 3;
}

s32 battleEvent_OP8_setVarFalse(s32 id, u8* bc) {
    VAR(bc, 1) = 0;
    return 3;
}

s32 battleEvent_OP9_addVar(s32 id, u8* bc) {
    battleEvent_loadArgs(bc, 2, bc[5], 0);
    VAR(bc, 1) += battleEventVar0->m380_currentArgs[1];
    return 6;
}

s32 battleEvent_OPA_subVar(s32 id, u8* bc) {
    battleEvent_loadArgs(bc, 2, bc[5], 0);
    VAR(bc, 1) -= battleEventVar0->m380_currentArgs[1];
    return 6;
}

s32 battleEvent_OPB_orVar(s32 id, u8* bc) {
    battleEvent_loadArgs(bc, 2, bc[5], 0);
    VAR(bc, 1) |= battleEventVar0->m380_currentArgs[1];
    return 6;
}

s32 battleEvent_OPC_clearBitsVar(s32 id, u8* bc) {
    battleEvent_loadArgs(bc, 2, bc[5], 0);
    VAR(bc, 1) &= ~battleEventVar0->m380_currentArgs[1];
    return 6;
}

s32 battleEvent_OPD_incVar(s32 id, u8* bc) {
    VAR(bc, 1)++;
    return 3;
}

s32 battleEvent_OPE_decVar(s32 id, u8* bc) {
    VAR(bc, 1)--;
    return 3;
}

s32 battleEvent_OPF_andVar(s32 id, u8* bc) {
    battleEvent_loadArgs(bc, 2, bc[5], 0);
    VAR(bc, 1) &= battleEventVar0->m380_currentArgs[1];
    return 6;
}

s32 battleEvent_OP10_orVar2(s32 id, u8* bc) {
    battleEvent_loadArgs(bc, 2, bc[5], 0);
    VAR(bc, 1) |= battleEventVar0->m380_currentArgs[1];
    return 6;
}

s32 battleEvent_OP11_xorVar(s32 id, u8* bc) {
    battleEvent_loadArgs(bc, 2, bc[5], 0);
    VAR(bc, 1) ^= battleEventVar0->m380_currentArgs[1];
    return 6;
}

s32 battleEvent_OP12_shlVar(s32 id, u8* bc) {
    s32 i = BC_U16(bc, 1) >> 1;
    battleEvent_loadArgs(bc, 2, 0, 0);
    ((u16*)battleEventVar0->m394_vars)[i] <<= (u16)battleEventVar0->m380_currentArgs[1];
    return 5;
}

s32 battleEvent_OP13_shrVar(s32 id, u8* bc) {
    s32 i = BC_U16(bc, 1) >> 1;
    battleEvent_loadArgs(bc, 2, 0, 0);
    ((u16*)battleEventVar0->m394_vars)[i] >>= (u16)battleEventVar0->m380_currentArgs[1];
    return 5;
}

s32 battleEvent_OP14_randomVar(s32 id, u8* bc) {
    VAR(bc, 1) = getRandomValueInRange(0, 0x7FFF);
    return 3;
}

s32 battleEvent_OP15_randomRangeVar(s32 id, u8* bc) {
    VAR(bc, 3) = getRandomValueInRange(0, bc[1] | (bc[2] << 8));
    return 5;
}

s32 battleEvent_OP16_mulVar(s32 id, u8* bc) {
    battleEvent_loadArgs(bc, 2, bc[5], 0);
    VAR(bc, 1) = battleEventVar0->m380_currentArgs[0] * battleEventVar0->m380_currentArgs[1];
    return 6;
}

s32 battleEvent_OP17_divVar(s32 id, u8* bc) {
    battleEvent_loadArgs(bc, 2, bc[5], 0);
    VAR(bc, 1) = R3000_DIV((s32)battleEventVar0->m380_currentArgs[0], battleEventVar0->m380_currentArgs[1]);
    return 6;
}

void battleDialogLoadPortrait(u8 portrait, u16 flags, u16 x, u16 y, u16 width) {
    s32 file;
    u8* buf;
    TIM_IMAGE tim;
    u32 flip = flags & 1;
    file = battleDialogPortraitsTable[portrait * 2 + flip] + 0x46;
    setCurrentDirectory(4, 0);
    buf = allocateBattleHeap(getFileSizeAligned(file), 1);
    readFile(file, buf, 0, 0x80);
    idleBattleDuringLoading();
    OpenTIM((u32*)buf);
    ReadTIM(&tim);
    tim.crect->x = 0;
    tim.crect->y = 0x1D0;
    tim.prect->x = 0x3C0;
    tim.prect->y = 0x100;
    LoadImage(tim.crect, tim.caddr);
    LoadImage(tim.prect, tim.paddr);
    DrawSync(0);
    freeHeap(buf);
    if (flip) {
        (battleEventVar0->m7A4_portraitPolys + BATTLE_ODD_OR_EVEN)->x0 = x + width - 4;
        (battleEventVar0->m7A4_portraitPolys + BATTLE_ODD_OR_EVEN)->y0 = y + 4;
        (battleEventVar0->m7A4_portraitPolys + BATTLE_ODD_OR_EVEN)->x1 = x + width - 0x44;
        (battleEventVar0->m7A4_portraitPolys + BATTLE_ODD_OR_EVEN)->y1 = y + 4;
        (battleEventVar0->m7A4_portraitPolys + BATTLE_ODD_OR_EVEN)->x2 = x + width - 4;
        (battleEventVar0->m7A4_portraitPolys + BATTLE_ODD_OR_EVEN)->y2 = y + 0x44;
        (battleEventVar0->m7A4_portraitPolys + BATTLE_ODD_OR_EVEN)->x3 = x + width - 0x44;
        (battleEventVar0->m7A4_portraitPolys + BATTLE_ODD_OR_EVEN)->y3 = y + 0x44;
        (battleEventVar0->m7A4_portraitPolys + BATTLE_ODD_OR_EVEN)->u0 = 0;
        (battleEventVar0->m7A4_portraitPolys + BATTLE_ODD_OR_EVEN)->v0 = 0;
        (battleEventVar0->m7A4_portraitPolys + BATTLE_ODD_OR_EVEN)->u1 = 0x3F;
        (battleEventVar0->m7A4_portraitPolys + BATTLE_ODD_OR_EVEN)->v1 = 0;
        (battleEventVar0->m7A4_portraitPolys + BATTLE_ODD_OR_EVEN)->u2 = 0;
        (battleEventVar0->m7A4_portraitPolys + BATTLE_ODD_OR_EVEN)->v2 = 0x40;
        (battleEventVar0->m7A4_portraitPolys + BATTLE_ODD_OR_EVEN)->u3 = 0x3F;
        (battleEventVar0->m7A4_portraitPolys + BATTLE_ODD_OR_EVEN)->v3 = 0x40;
    } else {
        (battleEventVar0->m7A4_portraitPolys + BATTLE_ODD_OR_EVEN)->x0 = x + 4;
        (battleEventVar0->m7A4_portraitPolys + BATTLE_ODD_OR_EVEN)->y0 = y + 4;
        (battleEventVar0->m7A4_portraitPolys + BATTLE_ODD_OR_EVEN)->x1 = x + 0x44;
        (battleEventVar0->m7A4_portraitPolys + BATTLE_ODD_OR_EVEN)->y1 = y + 4;
        (battleEventVar0->m7A4_portraitPolys + BATTLE_ODD_OR_EVEN)->x2 = x + 4;
        (battleEventVar0->m7A4_portraitPolys + BATTLE_ODD_OR_EVEN)->y2 = y + 0x44;
        (battleEventVar0->m7A4_portraitPolys + BATTLE_ODD_OR_EVEN)->x3 = x + 0x44;
        (battleEventVar0->m7A4_portraitPolys + BATTLE_ODD_OR_EVEN)->y3 = y + 0x44;
        (battleEventVar0->m7A4_portraitPolys + BATTLE_ODD_OR_EVEN)->u0 = 0;
        (battleEventVar0->m7A4_portraitPolys + BATTLE_ODD_OR_EVEN)->v0 = 0;
        (battleEventVar0->m7A4_portraitPolys + BATTLE_ODD_OR_EVEN)->u1 = 0x40;
        (battleEventVar0->m7A4_portraitPolys + BATTLE_ODD_OR_EVEN)->v1 = 0;
        (battleEventVar0->m7A4_portraitPolys + BATTLE_ODD_OR_EVEN)->u2 = 0;
        (battleEventVar0->m7A4_portraitPolys + BATTLE_ODD_OR_EVEN)->v2 = 0x40;
        (battleEventVar0->m7A4_portraitPolys + BATTLE_ODD_OR_EVEN)->u3 = 0x40;
        (battleEventVar0->m7A4_portraitPolys + BATTLE_ODD_OR_EVEN)->v3 = 0x40;
    }
    battleEventVar0->m7F4_battleDialogPortraitOddOrEven = BATTLE_ODD_OR_EVEN;
}

u8 battleEvent_DisplayDialog(u16 msg, u8 portrait, u16 flags) {
    u16 x;
    u16 y;
    u16 width;
    u16 height;
    u8 showPortrait = 0;
    u8 ret = 0;
    s32 i;

    x = battleEventVar0->m7F6_dialogParams[0];
    y = battleEventVar0->m7F6_dialogParams[1];
    width = battleEventVar0->m7F6_dialogParams[2] * 12 + 0x18;
    if ((u16)flags == 0) {
        flags = battleEventVar0->m7F6_dialogParams[4];
    }
    if (battleEventVar0->m802_dialogBoxInitialized == 0) {
        battleDialogCursorSpriteIndex = 4;
        if (battleEventVar0->m7F6_dialogParams[1] == 0x7FFF) {
            y = 0x10;
            if (flags & 4) {
                y = 0x8C;
            }
        }
        if (battleEventVar0->m7F6_dialogParams[3] >= 5) {
            height = 4;
        } else {
            height = battleEventVar0->m7F6_dialogParams[3];
        }
        height = height * 13 + 0x14;
        if (battleEventVar0->m7F6_dialogParams[0] == 0x7FFF) {
            x = 0xA0 - (width >> 1);
        }
        if (!(flags & 8)) {
            if (!(flags & 2) && portrait != 0xFF) {
                width += 0x40;
                showPortrait = 1;
            }
            if (battleEventVar0->m7F6_dialogParams[0] == 0x7FFF) {
                x = 0xA0 - (width >> 1);
            }
            if (!showPortrait) {
                allocateAndSetupBattleDialogWindow(0, x, y, width, height, ((u16)flags >> 4 ^ 1) & 1, 1);
                while (!battleVar1->mBF_isDialogWindowInitialized3[0]) {
                    battleRenderDebugAndMain();
                }
            } else {
                battleDialogLoadPortrait(portrait, flags, x, y, width);
                allocateAndSetupBattleDialogWindow(0, x, y, width, height, ((u16)flags >> 4 ^ 1) & 1, 1);
                while (!battleVar1->mBF_isDialogWindowInitialized3[0]) {
                    battleRenderDebugAndMain();
                }
                battleVar1->mC8_dialogPortrait = 1;
                if (!(flags & 1)) {
                    x += 0x40;
                }
            }
        }
        battleDialogWindowY2 = y + 8;
        battleDialogWindowX2 = x + 0xC;
        SETUP_WINDOW_SIZE2(battleEventVar1, 0x380, 0x100, battleDialogWindowX2, battleDialogWindowY2, battleEventVar0->m7F6_dialogParams[2] * 3, battleEventVar0->m7F6_dialogParams[3]);
        ((u8*)&battleEventVar1->m48_textTile[1])[0] = 4;
        battleEventVar1->m10_flags |= 2;
        resetDialogWindow18(battleEventVar1);
        addDialogWindowsToOTSub2(battleEventVar1, getDialogParamPointer(battleEventVarDialogueBundle, msg));
        battleVar1->mC9_dialog = 1;
        battleEventVar0->m802_dialogBoxInitialized = 1;
        battleRenderDebugAndMain();
    }
    if (battleEventVar1->m10_flags & 8) {
        if (!(flags & 8)) {
            setupBattleDialogCursorSpritePoly(battleEventVar1->m0_cursorX * 4 + battleDialogWindowX2 + 2, battleEventVar1->m2_cursorLine * 14 + battleDialogWindowY2 + 5);
        }
        battleVar1->mCF_isWaitingForInput = 1;
        /* The auto-advance setting (off by default) presses for the page. */
        if ((battleInputButton == 4) | pcGameplayDialogueAuto(1, 1, g_playTimeInVsync)) {
            addDialogWindowsToOTSub1(battleEventVar1);
            battleVar1->mCF_isWaitingForInput = 0;
            battleVar1->m9E_render27C8 = 0;
        }
    }
    else {
        pcGameplayDialogueAuto(1, 0, g_playTimeInVsync);
    }
    if (!(battleEventVar1->m10_flags & 4)) {
        battleVar1->mC9_dialog = 0;
        dealocateDialogWindow18Memory(battleEventVar1);
        battleRenderDebugAndMain();
        battleVar1->mC8_dialogPortrait = 0;
        if (!(flags & 8)) {
            clearBattleDialogWindow(0);
        }
        ret = 1;
        battleEventVar0->m802_dialogBoxInitialized = 0;
        for (i = 0; i < 5; i++) {
            battleEventVar0->m7F6_dialogParams[i] = battleEventDefaultDialogParams[i];
        }
    }
    return ret;
}

s32 battleEvent_OP18_showDialog(s32 id, u8* bc) {
    return (battleEvent_DisplayDialog(bc[1] | (bc[2] << 8), battleEventVar0->m0_scriptEntities[id].m24_portrait, bc[3]) != 0) << 2;
}

s32 battleEvent_OP19_showDialogAlt(s32 id, u8* bc) {
    return battleEvent_DisplayDialog(bc[2] | (bc[3] << 8), bc[1], bc[4]) != 0 ? 5 : 0;
}

s32 battleEvent_OP1A_setDialogParams(s32 id, u8* bc) {
    s32 i;
    battleEvent_loadArgs(bc, 5, 0, 1);
    for (i = 0; i < 4; i++) {
        if (battleEventVar0->m380_currentArgs[i] != 0) {
            battleEventVar0->m7F6_dialogParams[i] = battleEventVar0->m380_currentArgs[i];
        } else {
            battleEventVar0->m7F6_dialogParams[i] = battleEventDefaultDialogParams[i];
        }
    }
    battleEventVar0->m7F6_dialogParams[4] = battleEventVar0->m380_currentArgs[4];
    return 0xB;
}

s32 battleEvent_OP1B_setPortrait(s32 id, u8* bc) {
    u8 a = bc[1];
    if (a >= 0xF3) {
        a = D_800D2C31[a];
    }
    battleEventVar0->m0_scriptEntities[id].m24_portrait = a;
    return 2;
}

s32 battleEvent_OP1C_setCameraMode2(s32 id, u8* bc) {
    currentBattleMode = 2;
    return 1;
}

s32 battleEvent_OP1D_setCameraMode1(s32 id, u8* bc) {
    currentBattleMode = 1;
    return 1;
}

s32 battleEvent_OP1E_fadeOut(s32 id, u8* bc) {
    battleEvent_loadArgs(bc, 1, 0, 1);
    battleCreateFader(battleEventVar0->m380_currentArgs[0], 2, 0xFF, 0xFF, 0xFF);
    return 3;
}

s32 battleEvent_OP1F_fadeIn(s32 id, u8* bc) {
    battleEvent_loadArgs(bc, 1, 0, 1);
    battleCreateFader(battleEventVar0->m380_currentArgs[0], 2, 0, 0, 0);
    return 3;
}

s32 battleEvent_OP49_setReturnMode(s32 id, u8* bc) {
    battleEvent_loadArgs(bc, 1, 0, 1);
    battleEventReturnMode = battleEventVar0->m380_currentArgs[0];
    return 3;
}

s32 battleEvent_OP20_endBattle(s32 id, u8* bc) {
    battleEventEndBattleFlag = 1;
    battleEventVar0->m800_isEventDone = 1;
    return 1;
}

s32 battleEvent_OP21_setBattleRunning(s32 id, u8* bc) {
    battleEventIsRunningFlag = 1;
    return 1;
}

s32 battleEvent_OP22_pauseEvent(s32 id, u8* bc) {
    battleEventVar0->m801_pauseCountdown = 2;
    return 1;
}

s32 battleEvent_OP37_startBattle(s32 id, u8* bc) {
    isBattleAnEvent = 1;
    battleEventVar0->m800_isEventDone = 1;
    battleRunningVar1 = 1;
    return 1;
}

s32 battleEvent_OP23_setupMecha(s32 id, u8* bc) {
    s32 ret = 0;
    battleEvent_loadArgs(bc, 3, 0, 1);
    switch (battleEventVar0->m0_scriptEntities[battleEventVar0->m380_currentArgs[0] - 0xF3].m34_mechaSetupState) {
    case 0:
        battleEventVar0->m0_scriptEntities[battleEventVar0->m380_currentArgs[0] - 0xF3].m34_mechaSetupState = 2;
        setupMechaForEvent(battleEventVar0->m380_currentArgs[0] - 0xF3, characterIdToTargetBitmask(battleEventVar0->m380_currentArgs[1] + 0xD), battleEventVar0->m380_currentArgs[2]);
        break;
    case 1:
        ret = 7;
        waitBattleAnimationSpriteLoading();
        waitBattleAnimationSoundLoaded();
        battleEventVar0->m0_scriptEntities[battleEventVar0->m380_currentArgs[0] - 0xF3].m34_mechaSetupState = 0;
        break;
    }
    return ret;
}

s32 battleEvent_OP38_playMechaAnim(s32 id, u8* bc) {
    battleEvent_loadArgs(bc, 3, 0, 1);
    mechaPlayAnimation_battle(battleEventVar0->m380_currentArgs[0] - 0xF3, characterIdToTargetBitmask(battleEventVar0->m380_currentArgs[1] + 0xD), battleEventVar0->m380_currentArgs[2]);
    return 7;
}

s32 battleEvent_OP4A_disablePlayerControl(s32 id, u8* bc) {
    startGearInfinityMode(0);
    return 1;
}

s32 battleEvent_OP4B_markCharacterDead(s32 id, u8* bc) {
    battleEvent_loadArgs(bc, 1, 0, 1);
    D_800CCD1E[(u8)battleEvent_resolveEntityIndex((u8)battleEventVar0->m380_currentArgs[0])].m0_m36 |= 1;
    return 3;
}

s32 battleEvent_OP24_setBattleTransition(s32 id, u8* bc) {
    battleEvent_loadArgs(bc, 2, 0, 1);
    battleInitVar0 = battleEventVar0->m380_currentArgs[0] + 1;
    battleTransitionEffect = battleEventVar0->m380_currentArgs[1];
    return 5;
}

s32 battleEvent_OP25_enableBattleMenu(s32 id, u8* bc) {
    battleEventMenuEnabled = 1;
    return 1;
}

s32 battleEvent_OP26_setFieldReturn(s32 id, u8* bc) {
    battleEvent_loadArgs(bc, 4, 0, 1);
    gameState.m231A_fieldID = battleEventVar0->m380_currentArgs[0];
    gameState.m231C_cameraYaw = battleEventVar0->m380_currentArgs[1];
    gameState.m231E_worldmapInitialPositionIndex = battleEventVar0->m380_currentArgs[2];
    gameState.m2320_worldmapMode = battleEventVar0->m380_currentArgs[3];
    resetFieldScriptEntityInit();
    return 9;
}

s32 battleEvent_OP27_playMovie(s32 id, u8* bc) {
    battleEvent_loadArgs(bc, 4, 0, 1);
    movieType.m0_type = battleEventVar0->m380_currentArgs[0] | 0x80;
    movieType.m1_number = battleEventVar0->m380_currentArgs[1];
    movieType.m2_returnMode = 1;
    movieType.m3_fadeParam = battleEventVar0->m380_currentArgs[2];
    battleEventMoviePending = 1;
    movieRequestEndFrame = battleEventVar0->m380_currentArgs[3];
    return 9;
}

s32 battleEvent_OP28_playSound(s32 id, u8* bc) {
    battleCreateFader(bc[5], bc[1], bc[2], bc[3], bc[4]);
    return 6;
}

s32 battleEvent_OP29_setCameraTarget(s32 id, u8* bc) {
    SVECTOR sv;
    battleEvent_loadArgs(bc, 4, 0, 1);
    sv.vx = battleEventVar0->m380_currentArgs[0];
    sv.vy = battleEventVar0->m380_currentArgs[1];
    sv.vz = battleEventVar0->m380_currentArgs[2];
    startBattleCameraShake(&sv, battleEventVar0->m380_currentArgs[3]);
    return 9;
}

s32 battleEvent_OP35_createSprite(s32 id, u8* bc) {
    SVECTOR pos;
    s32 idx;
    /* pos is never set: the PS1 passes its stack residue there, the s4/s5
     * that updateCharacterBlinkingTask saved (battleEvent_update's callers'
     * registers): 0 and 1 in the opening gear battle (US, Redux). The PC
     * stack holds host addresses there, which put the sprite far away and
     * flipped the battle's timing between builds; it gives the PS1's. */
    pos.vx = 0;
    pos.vy = 0;
    pos.vz = 1;
    pos.pad = 0;
    battleEvent_loadArgs(bc, 2, 0, 1);
    idx = (u8)(battleEventVar0->m380_currentArgs[0] + 0xD);
    if (battleEventVar0->m0_scriptEntities[idx].m35_hasSprite == 0) {
        void* p = mallocAndDecompress(PC_RELOC_ENTRY(battleEventFile3, battleEventVar0->m380_currentArgs[1] + 1), 0);
        battleEventVar0->m0_scriptEntities[idx].m2C_animationBundle = p;
        battleEventVar0->m0_scriptEntities[idx].m30_spriteTask = battleEvent_createSpriteEntity(p, &pos);
        battleEventVar0->m0_scriptEntities[idx].m35_hasSprite = 1;
    }
    return 5;
}

s32 battleEvent_OP2A_playAnimation(s32 id, u8* bc) {
    s32 idx;
    battleEvent_loadArgs(bc, 2, 0, 1);
    idx = (u8)(battleEventVar0->m380_currentArgs[0] + 0xD);
    if (battleEventVar0->m0_scriptEntities[idx].m35_hasSprite != 0) {
        battleEvent_setSpriteAnimation(battleEventVar0->m0_scriptEntities[idx].m30_spriteTask, battleEventVar0->m380_currentArgs[1]);
    }
    return 5;
}

void battleEventDestroyScriptEntitySprite(s32 id, u8* bc) {
    s32 idx;
    battleEvent_loadArgs(bc, 1, 0, 1);
    idx = (u8)(battleEventVar0->m380_currentArgs[0] + 0xD);
    if (battleEventVar0->m0_scriptEntities[idx].m35_hasSprite != 0) {
        battleEventDestroySpriteTaskCallback(battleEventVar0->m0_scriptEntities[idx].m30_spriteTask);
        freeHeap(battleEventVar0->m0_scriptEntities[idx].m2C_animationBundle);
        battleEventVar0->m0_scriptEntities[idx].m35_hasSprite = 0;
    }
}

s32 battleEvent_OP36_destroySprite(s32 id, u8* bc) {
    battleEventDestroyScriptEntitySprite(id, bc);
    return 3;
}

s32 battleEvent_OP40_destroySpriteAndReset(s32 id, u8* bc) {
    battleEventDestroyScriptEntitySprite(id, bc);
    battleEventResetSpriteDisposeState();
    return 3;
}

s32 battleEvent_OP2B_wait(s32 id, u8* bc) {
    s32 ret = 0;
    if (battleEventVar0->m0_scriptEntities[id].m28_isWaiting == 0) {
        battleEvent_loadArgs(bc, 1, 0, 1);
        battleEventVar0->m0_scriptEntities[id].m26_waitTimer = battleEventVar0->m380_currentArgs[0] << 1;
        battleEventVar0->m0_scriptEntities[id].m28_isWaiting = 1;
    }
    if (battleEventVar0->m0_scriptEntities[id].m26_waitTimer == 0) {
        battleEventVar0->m0_scriptEntities[id].m28_isWaiting = 0;
        ret = 3;
    }
    return ret;
}

s32 battleEvent_OP2C_setPriority(s32 id, u8* bc) {
    u8 backup[16];
    s32 i;
    s32 dst;
    battleEventVar0->m0_scriptEntities[id].m22_priority = bc[1];
    if (bc[1] == 0xFE) {
        for (i = 0; i < 16; i++) {
            backup[i] = battleEventVar0->m794_executionOrder[i];
        }
        battleEventVar0->m794_executionOrder[0] = id;
        dst = 1;
        for (i = 0; i < 16; i++) {
            if (backup[i] != id) {
                battleEventVar0->m794_executionOrder[dst++] = backup[i];
            }
        }
    }
    return 3;
}

void battleEventLoadMusicFile(s16 musicId, u8 volume) {
    s32 size;
    resetBattleAudioState();
    if (battleEventVar0->m81F_hasLoadedMusic) {
        freeSoundInstance(pCurrentBattleMusic);
        battleRenderDebugAndMain();
    }
    openMusicBattleFolder();
    size = getFileSizeAligned(musicId + 4);
    battleEventVar0->m814_pLoadedMusic = battleMusic;
    readFile(musicId + 4, battleMusic, 0, 0x80);
    idleBattleDuringLoading();
    memmove(battleMusic, battleEventVar0->m814_pLoadedMusic, size);
    battleEventVar0->m81F_hasLoadedMusic = 1;
    battleEventVar0->m81C_loadedMusicId = musicId;
    battleEventVar0->m81E_loadedMusicVol = volume;
    pCurrentBattleMusic = realStartBattleMusic(battleMusic, volume, 0);
}

void battleEventSetLoadedMusicVolume(s32 vol, s32 b) {
    playBattleMusic(pCurrentBattleMusic, vol, b);
}

s32 battleEvent_OP2D_loadMusic(s32 id, u8* bc) {
    battleEvent_loadArgs(bc, 1, 0, 1);
    battleEventLoadMusicFile(battleEventVar0->m380_currentArgs[0], 0x7F);
    return 3;
}

s32 battleEvent_OP2E_loadMusicQuiet(s32 id, u8* bc) {
    battleEvent_loadArgs(bc, 1, 0, 1);
    battleEventLoadMusicFile(battleEventVar0->m380_currentArgs[0], 0);
    return 3;
}

s32 battleEvent_OP2F_playLoadedMusic(s32 id, u8* bc) {
    battleEvent_loadArgs(bc, 2, 0, 1);
    battleEventVar0->m81E_loadedMusicVol = battleEventVar0->m380_currentArgs[0];
    battleEventSetLoadedMusicVolume(battleEventVar0->m380_currentArgs[0], battleEventVar0->m380_currentArgs[1]);
    return 5;
}

s32 battleEvent_OP30_stopMusic(s32 id, u8* bc) {
    u8 vol = 0;
    battleEvent_loadArgs(bc, 1, 0, 1);
    if (battleEventVar0->m380_currentArgs[0] == 0) {
        vol = battleEventVar0->m81E_loadedMusicVol;
    }
    battleEventSetLoadedMusicVolume(vol, 0);
    return 3;
}

s32 battleEvent_OP31_playSoundEffect(s32 id, u8* bc) {
    SeqFile* seq;
    battleEvent_loadArgs(bc, 4, 0, 1);
    if (battleEventVar0->m380_currentArgs[3] == 0) {
        seq = battleEventVar0->m818_eventSeq;
    } else {
        seq = gBattleCurrentSoundEffectSeq;
    }
    playSoundEffectInFreeSlotVolPan((seq->u14.m14_id << 16) | battleEventVar0->m380_currentArgs[0], (s16)battleEventVar0->m380_currentArgs[1], (s16)battleEventVar0->m380_currentArgs[2]);
    return 9;
}

s32 battleEvent_OP41_playSong(s32 id, u8* bc) {
    SeqFile* seq;
    battleEvent_loadArgs(bc, 3, 0, 1);
    if (battleEventVar0->m380_currentArgs[2] == 0) {
        seq = battleEventVar0->m818_eventSeq;
    } else {
        seq = gBattleCurrentSoundEffectSeq;
    }
    setSoundEffectVolume((seq->u14.m14_id << 16) | battleEventVar0->m380_currentArgs[0], battleEventVar0->m380_currentArgs[1]);
    return 7;
}

s32 battleEvent_OP32_nop(s32 id, u8* bc) { return 1; }

s32 battleEvent_OP33_stopBattleMusic(s32 id, u8* bc) {
    if (battleEventVar0->m81F_hasLoadedMusic != 0) {
        startMusicInstanceSub0(pCurrentBattleMusic);
        battleRenderDebugAndMain();
        freeSoundInstance(pCurrentBattleMusic);
        battleEventVar0->m81F_hasLoadedMusic = 0;
    }
    return 1;
}

s32 battleEvent_OP34_nop2(s32 id, u8* bc) { return 1; }

s32 battleEvent_OP39_transformToGear(s32 id, u8* bc) {
    battleEntities.m0_entities[0].m0_base.mA0_gearNum = 0;
    gameState.m26C_party[0].mA0_gearNum = 0;
    callGearRelocateSlot(0);
    callGearSwapSprite(0);
    battleVar2->m2EB_hpChanged[0] = 1;
    battleEntities.m0_entities[0].m15A_flags |= 0x80;
    markEnemyDead(0);
    apConfigArray[0].m1_gearState = 2;
    BATTLE_VAR0_M835C(battleVar0)[0].m1E1_barMode = 2;
    battleBssBlock.m4_visualEntities[0].m4_isGear = 1;
    return 1;
}

s32 battleEvent_OP3A_setCameraForEntity(s32 id, u8* bc) {
    battleEvent_loadArgs(bc, 2, 0, 1);
    battleEventSetSpriteCameraAnim(battleEvent_resolveEntityIndex((u8)battleEventVar0->m380_currentArgs[0]), (s16)battleEventVar0->m380_currentArgs[1]);
    return 5;
}

s32 battleEvent_OP3B_hideEntity(s32 id, u8* bc) {
    battleEvent_loadArgs(bc, 1, 0, 1);
    battleEventHideSpriteEntity(battleEvent_resolveEntityIndex((u8)battleEventVar0->m380_currentArgs[0]));
    return 3;
}

s32 battleEvent_OP3C_showEntity(s32 id, u8* bc) {
    battleEvent_loadArgs(bc, 1, 0, 1);
    battleEventShowSpriteEntity(battleEvent_resolveEntityIndex((u8)battleEventVar0->m380_currentArgs[0]));
    return 3;
}

s32 battleEvent_OP3D_setEntityFlip(s32 id, u8* bc) {
    battleEvent_loadArgs(bc, 1, 0, 1);
    battleEventSetSpriteFlip(battleEvent_resolveEntityIndex((u8)battleEventVar0->m380_currentArgs[0]));
    return 3;
}

s32 battleEvent_OP3E_moveEntity(s32 id, u8* bc) {
    s32 ret = 0;
    u8 slot;
    battleEvent_loadArgs(bc, 4, 0, 1);
    slot = battleEvent_resolveEntityIndex((u8)battleEventVar0->m380_currentArgs[0]);
    if (battleEventInitVar1[slot] == 0) {
        battleEventVar0->m804_isMoving[slot] = 1;
        battleEventInitVar1[slot] = 1;
        battleEventStartSpriteMove(slot, (s16)battleEventVar0->m380_currentArgs[1], (s16)battleEventVar0->m380_currentArgs[2], (s16)battleEventVar0->m380_currentArgs[3]);
    } else if (battleEventVar0->m804_isMoving[slot] == 0) {
        battleEventInitVar1[slot] = 0;
        ret = 9;
    }
    return ret;
}

s32 battleEvent_OP3F_moveEntityAlt(s32 id, u8* bc) {
    s32 ret = 0;
    u8 slot;
    battleEvent_loadArgs(bc, 4, 0, 1);
    slot = battleEvent_resolveEntityIndex((u8)battleEventVar0->m380_currentArgs[0]);
    if (battleEventInitVar1[slot] == 0) {
        battleEventVar0->m804_isMoving[slot] = 1;
        battleEventInitVar1[slot] = 1;
        battleEventStartSpriteMoveAlt(slot, (s16)battleEventVar0->m380_currentArgs[1], (s16)battleEventVar0->m380_currentArgs[2], (s16)battleEventVar0->m380_currentArgs[3]);
    } else if (battleEventVar0->m804_isMoving[slot] == 0) {
        battleEventInitVar1[slot] = 0;
        ret = 9;
    }
    return ret;
}

s32 battleEvent_OP45_loadCharacterSprite(s32 id, u8* bc) {
    s32 ret = 0;
    s32 dst;
    s32 src;
    battleEvent_loadArgs(bc, 4, 0, 1);
    dst = battleEvent_resolveEntityIndex((u8)battleEventVar0->m380_currentArgs[0]);
    src = battleEvent_resolveEntityIndex((u8)battleEventVar0->m380_currentArgs[1]);
    battleVar2->m2DA_indexInBattleVar48 = 0;
    performAttackSub2();
    battleCurrentDamages[0].m18_damageType[src & 0xFF] = battleEventVar0->m380_currentArgs[3];
    if (battleEventInitVar1[(u8)dst] == 0) {
        battleEventVar0->m804_isMoving[(u8)dst] = 1;
        battleEventInitVar1[(u8)dst] = 1;
        battleEventStartCharacterSpriteLoad(dst, src);
        while (isCDBusy()) {
            battleRenderDebugAndMain();
        }
        battleEventSetSpriteCameraAnim(dst, (s16)battleEventVar0->m380_currentArgs[2]);
    } else if (battleEventVar0->m804_isMoving[(u8)dst] == 0) {
        battleEventInitVar1[(u8)dst] = 0;
        ret = 9;
    }
    return ret;
}

s32 battleEvent_OP46_swapCharacterSprite(s32 id, u8* bc) {
    u8 dst;
    u8 src;
    battleVar2->m2DA_indexInBattleVar48 = 0;
    performAttackSub2();
    battleEvent_loadArgs(bc, 3, 0, 1);
    dst = battleEvent_resolveEntityIndex((u8)battleEventVar0->m380_currentArgs[0]);
    src = battleEvent_resolveEntityIndex((u8)battleEventVar0->m380_currentArgs[1]);
    battleEventSwapSpritePrepare(dst, battleEventVar0->m380_currentArgs[2]);
    battleEventSwapSpriteCommit(dst, src);
    return 7;
}

s32 battleEvent_OP47_resetInputState(s32 id, u8* bc) {
    clearBattleAnimationData();
    return 1;
}

s32 battleEvent_OP42_disableBattleRendering(s32 id, u8* bc) {
    setupGearTurnHud(0);
    return 1;
}

s32 battleEvent_OP43_enableBattleRendering(s32 id, u8* bc) {
    freeBattleRenderStruct2(0);
    return 1;
}

s32 battleEvent_OP44_resetAllSprites(s32 id, u8* bc) {
    s32 i;
    for (i = 0; i < 11; i++) {
        if (battleMechas[i] != NULL) {
            ((sBattleRunningVar0Sub*)battleMechas[i])->m35_hasSprite = 0;
        }
    }
    return 1;
}

s32 battleEvent_OP48_setMusicVolume(s32 id, u8* bc) {
    battleEvent_loadArgs(bc, 2, 0, 1);
    playBattleSoundFileAndWait(battleEventVar0->m380_currentArgs[0], battleEventVar0->m380_currentArgs[1]);
    return 5;
}

void battleEvent_update(void) {
    u8 cont = 1;
    u32 i;
    u8 id;
    u8 n;
    u16 offset;
    s32 size;

    waitBattleAnimationSoundLoaded();
    updateCharacterBlinkingTask(0);
    if (battleEventVar0->m800_isEventDone == 0) {
        do {
            for (i = 0; i < battleEventVar2->m40_count; i++) {
                id = battleEventVar0->m794_executionOrder[i];
                battleRenderDebugAndMain();
                n = 4;
                do {
                    offset = battleEventGetBytecodeOffset(&battleEventVar0->m0_scriptEntities[id]);
                    switch (battleEventVar0->m390_scriptByteCode[offset]) {
                case 0x00:
                    size = battleEvent_OP0_endScript(id);
                    n = 1;
                    break;
                case 0x01:
                    size = battleEvent_OP1_jump(id, battleEventVar0->m390_scriptByteCode + offset);
                    break;
                case 0x02:
                    size = battleEvent_OP2_jumpIf(id, battleEventVar0->m390_scriptByteCode + offset);
                    break;
                case 0x03:
                    size = battleEvent_startScriptSlot(id, battleEventVar0->m390_scriptByteCode + offset);
                    break;
                case 0x04:
                    size = battleEvent_OP4_waitScriptDone(id, battleEventVar0->m390_scriptByteCode + offset);
                    break;
                case 0x05:
                    size = battleEvent_OP5_waitScript(id, battleEventVar0->m390_scriptByteCode + offset);
                    break;
                case 0x06:
                    size = battleEvent_OP6_setVar(id, battleEventVar0->m390_scriptByteCode + offset);
                    break;
                case 0x07:
                    size = battleEvent_OP7_setVarTrue(id, battleEventVar0->m390_scriptByteCode + offset);
                    break;
                case 0x08:
                    size = battleEvent_OP8_setVarFalse(id, battleEventVar0->m390_scriptByteCode + offset);
                    break;
                case 0x09:
                    size = battleEvent_OP9_addVar(id, battleEventVar0->m390_scriptByteCode + offset);
                    break;
                case 0x0A:
                    size = battleEvent_OPA_subVar(id, battleEventVar0->m390_scriptByteCode + offset);
                    break;
                case 0x0B:
                    size = battleEvent_OPB_orVar(id, battleEventVar0->m390_scriptByteCode + offset);
                    break;
                case 0x0C:
                    size = battleEvent_OPC_clearBitsVar(id, battleEventVar0->m390_scriptByteCode + offset);
                    break;
                case 0x0D:
                    size = battleEvent_OPD_incVar(id, battleEventVar0->m390_scriptByteCode + offset);
                    break;
                case 0x0E:
                    size = battleEvent_OPE_decVar(id, battleEventVar0->m390_scriptByteCode + offset);
                    break;
                case 0x0F:
                    size = battleEvent_OPF_andVar(id, battleEventVar0->m390_scriptByteCode + offset);
                    break;
                case 0x10:
                    size = battleEvent_OP10_orVar2(id, battleEventVar0->m390_scriptByteCode + offset);
                    break;
                case 0x11:
                    size = battleEvent_OP11_xorVar(id, battleEventVar0->m390_scriptByteCode + offset);
                    break;
                case 0x12:
                    size = battleEvent_OP12_shlVar(id, battleEventVar0->m390_scriptByteCode + offset);
                    break;
                case 0x13:
                    size = battleEvent_OP13_shrVar(id, battleEventVar0->m390_scriptByteCode + offset);
                    break;
                case 0x14:
                    size = battleEvent_OP14_randomVar(id, battleEventVar0->m390_scriptByteCode + offset);
                    break;
                case 0x15:
                    size = battleEvent_OP15_randomRangeVar(id, battleEventVar0->m390_scriptByteCode + offset);
                    break;
                case 0x16:
                    size = battleEvent_OP16_mulVar(id, battleEventVar0->m390_scriptByteCode + offset);
                    break;
                case 0x17:
                    size = battleEvent_OP17_divVar(id, battleEventVar0->m390_scriptByteCode + offset);
                    break;
                case 0x18:
                    size = battleEvent_OP18_showDialog(id, battleEventVar0->m390_scriptByteCode + offset);
                    break;
                case 0x19:
                    size = battleEvent_OP19_showDialogAlt(id, battleEventVar0->m390_scriptByteCode + offset);
                    break;
                case 0x1A:
                    size = battleEvent_OP1A_setDialogParams(id, battleEventVar0->m390_scriptByteCode + offset);
                    break;
                case 0x1B:
                    size = battleEvent_OP1B_setPortrait(id, battleEventVar0->m390_scriptByteCode + offset);
                    break;
                case 0x1C:
                    size = battleEvent_OP1C_setCameraMode2(id, battleEventVar0->m390_scriptByteCode + offset);
                    break;
                case 0x1D:
                    size = battleEvent_OP1D_setCameraMode1(id, battleEventVar0->m390_scriptByteCode + offset);
                    break;
                case 0x1E:
                    size = battleEvent_OP1E_fadeOut(id, battleEventVar0->m390_scriptByteCode + offset);
                    break;
                case 0x1F:
                    size = battleEvent_OP1F_fadeIn(id, battleEventVar0->m390_scriptByteCode + offset);
                    break;
                case 0x20:
                    size = battleEvent_OP20_endBattle(id, battleEventVar0->m390_scriptByteCode + offset);
                    break;
                case 0x21:
                    size = battleEvent_OP21_setBattleRunning(id, battleEventVar0->m390_scriptByteCode + offset);
                    break;
                case 0x22:
                    size = battleEvent_OP22_pauseEvent(id, battleEventVar0->m390_scriptByteCode + offset);
                    break;
                case 0x23:
                    size = battleEvent_OP23_setupMecha(id, battleEventVar0->m390_scriptByteCode + offset);
                    break;
                case 0x24:
                    size = battleEvent_OP24_setBattleTransition(id, battleEventVar0->m390_scriptByteCode + offset);
                    break;
                case 0x25:
                    size = battleEvent_OP25_enableBattleMenu(id, battleEventVar0->m390_scriptByteCode + offset);
                    break;
                case 0x26:
                    size = battleEvent_OP26_setFieldReturn(id, battleEventVar0->m390_scriptByteCode + offset);
                    break;
                case 0x27:
                    size = battleEvent_OP27_playMovie(id, battleEventVar0->m390_scriptByteCode + offset);
                    break;
                case 0x28:
                    size = battleEvent_OP28_playSound(id, battleEventVar0->m390_scriptByteCode + offset);
                    break;
                case 0x29:
                    size = battleEvent_OP29_setCameraTarget(id, battleEventVar0->m390_scriptByteCode + offset);
                    break;
                case 0x2A:
                    size = battleEvent_OP2A_playAnimation(id, battleEventVar0->m390_scriptByteCode + offset);
                    break;
                case 0x2B:
                    size = battleEvent_OP2B_wait(id, battleEventVar0->m390_scriptByteCode + offset);
                    break;
                case 0x2C:
                    size = battleEvent_OP2C_setPriority(id, battleEventVar0->m390_scriptByteCode + offset);
                    break;
                case 0x2D:
                    size = battleEvent_OP2D_loadMusic(id, battleEventVar0->m390_scriptByteCode + offset);
                    break;
                case 0x2E:
                    size = battleEvent_OP2E_loadMusicQuiet(id, battleEventVar0->m390_scriptByteCode + offset);
                    break;
                case 0x2F:
                    size = battleEvent_OP2F_playLoadedMusic(id, battleEventVar0->m390_scriptByteCode + offset);
                    break;
                case 0x30:
                    size = battleEvent_OP30_stopMusic(id, battleEventVar0->m390_scriptByteCode + offset);
                    break;
                case 0x31:
                    size = battleEvent_OP31_playSoundEffect(id, battleEventVar0->m390_scriptByteCode + offset);
                    break;
                case 0x32:
                    size = battleEvent_OP32_nop(id, battleEventVar0->m390_scriptByteCode + offset);
                    break;
                case 0x33:
                    size = battleEvent_OP33_stopBattleMusic(id, battleEventVar0->m390_scriptByteCode + offset);
                    break;
                case 0x34:
                    size = battleEvent_OP34_nop2(id, battleEventVar0->m390_scriptByteCode + offset);
                    break;
                case 0x35:
                    size = battleEvent_OP35_createSprite(id, battleEventVar0->m390_scriptByteCode + offset);
                    break;
                case 0x36:
                    size = battleEvent_OP36_destroySprite(id, battleEventVar0->m390_scriptByteCode + offset);
                    break;
                case 0x37:
                    size = battleEvent_OP37_startBattle(id, battleEventVar0->m390_scriptByteCode + offset);
                    break;
                case 0x38:
                    size = battleEvent_OP38_playMechaAnim(id, battleEventVar0->m390_scriptByteCode + offset);
                    break;
                case 0x39:
                    size = battleEvent_OP39_transformToGear(id, battleEventVar0->m390_scriptByteCode + offset);
                    break;
                case 0x3A:
                    size = battleEvent_OP3A_setCameraForEntity(id, battleEventVar0->m390_scriptByteCode + offset);
                    break;
                case 0x3B:
                    size = battleEvent_OP3B_hideEntity(id, battleEventVar0->m390_scriptByteCode + offset);
                    break;
                case 0x3C:
                    size = battleEvent_OP3C_showEntity(id, battleEventVar0->m390_scriptByteCode + offset);
                    break;
                case 0x3D:
                    size = battleEvent_OP3D_setEntityFlip(id, battleEventVar0->m390_scriptByteCode + offset);
                    break;
                case 0x3E:
                    size = battleEvent_OP3E_moveEntity(id, battleEventVar0->m390_scriptByteCode + offset);
                    break;
                case 0x3F:
                    size = battleEvent_OP3F_moveEntityAlt(id, battleEventVar0->m390_scriptByteCode + offset);
                    break;
                case 0x40:
                    size = battleEvent_OP40_destroySpriteAndReset(id, battleEventVar0->m390_scriptByteCode + offset);
                    break;
                case 0x41:
                    size = battleEvent_OP41_playSong(id, battleEventVar0->m390_scriptByteCode + offset);
                    break;
                case 0x42:
                    size = battleEvent_OP42_disableBattleRendering(id, battleEventVar0->m390_scriptByteCode + offset);
                    break;
                case 0x43:
                    size = battleEvent_OP43_enableBattleRendering(id, battleEventVar0->m390_scriptByteCode + offset);
                    break;
                case 0x44:
                    size = battleEvent_OP44_resetAllSprites(id, battleEventVar0->m390_scriptByteCode + offset);
                    break;
                case 0x45:
                    size = battleEvent_OP45_loadCharacterSprite(id, battleEventVar0->m390_scriptByteCode + offset);
                    break;
                case 0x46:
                    size = battleEvent_OP46_swapCharacterSprite(id, battleEventVar0->m390_scriptByteCode + offset);
                    break;
                case 0x47:
                    size = battleEvent_OP47_resetInputState(id, battleEventVar0->m390_scriptByteCode + offset);
                    break;
                case 0x48:
                    size = battleEvent_OP48_setMusicVolume(id, battleEventVar0->m390_scriptByteCode + offset);
                    break;
                case 0x49:
                    size = battleEvent_OP49_setReturnMode(id, battleEventVar0->m390_scriptByteCode + offset);
                    break;
                case 0x4A:
                    size = battleEvent_OP4A_disablePlayerControl(id, battleEventVar0->m390_scriptByteCode + offset);
                    break;
                case 0x4B:
                    size = battleEvent_OP4B_markCharacterDead(id, battleEventVar0->m390_scriptByteCode + offset);
                    break;
                    }
                    battleEventVar0->m0_scriptEntities[id].m0_scriptBytecodeOffset[battleEventVar0->m0_scriptEntities[id].m20_currentActiveEntry] =
                        size + battleEventVar0->m0_scriptEntities[id].m0_scriptBytecodeOffset[battleEventVar0->m0_scriptEntities[id].m20_currentActiveEntry];
                } while (--n);
            }
            if (battleEventVar0->m801_pauseCountdown != 0 && --battleEventVar0->m801_pauseCountdown == 1) {
                cont = 0;
            }
        } while (cont);
    }
}


#if VERSION_JP
/* JP builds the whole overlay with gcc-2.6.3 and has a plain 4-bit slot
 * field (main/sprite.h mAC_x0); US compiles this one with the tail
 * (gcc-2.7.2-cdk) at the start of be_801E93E8.c. */
void battleEventSpriteMoveDoneCallback(SpriteActorCore* actor) {
    battleEventVar0->m804_isMoving[actor->mAC_x0] = 0;
    spriteActorSetCallback(actor, NULL);
}
#endif
