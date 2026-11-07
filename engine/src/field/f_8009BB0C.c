#include "common.h"
#include "pc_retail_heap.h"
#include "psyq.h"
#include "main/game_state.h"
#include "field/field.h"
#include "main/filesystem.h"
#include "main/memory.h"
#include "pc_gameplay.h"
#include "pc_gametext.h"
extern FieldBss_800AF880 fieldSceneState;

/* Sizes belong to the same two temporary buffers as the native batch. */
static s32 pcPortraitBytes[2];

#define ADD_DIALOG_SUB0(p) (((s32 (*)(DialogWindow18*))addDialogWindowsToOTSub0)(p))

extern s32 g_breakCurrentScript;
extern DialogWindow gDialogWindows[4];
extern u16 windowOpenBF[];
u16 getCameraDirection(void);
s32 showDialogWindowForActor(s32 actorId, s32 mode);
void OP_SHOW_DIALOG_WINDOW_FOR_OTHER_ACTOR_MODE0(void);
void OP_STOP(void);
s32 findDialogWindowForCurrentActor(s32* outputId);

/* Noah: portraitLoadingStatus entry (same view as src/field/f_8006FDEC.c) */
typedef struct {
    /* 0x0 */ s16 m0_id;
    /* 0x2 */ s16 m2_status;
    /* 0x4 */ s16 m4_isAsymmetric;
} PortraitLoadingStatus;
extern PortraitLoadingStatus portraitLoadingStatus[3];
extern s32 currentPortraitLoadingSlot;
extern u32* D_800ADB10;                     /* Noah: partyPortraitPointers[0] */
extern u32* D_800ADB14;                     /* Noah: partyPortraitPointers[1] */
extern RECT partyPortraitsRects[3][2];
extern u8 portraitMapping[];                     /* Noah: portraitMapping [][2] */
extern LoadingBatchCommand portraitsLoadingCommands[3];
void loadTimToVram(u32* addr, s16 px, s16 py, s16 cx, s16 cy, s16 cw, s16 ch);
s32 isPortraitUsed(s32 portrait);
s32 isDialogAvatarLoaded(s32 portraitId);
extern s32 loadCompleted;                      /* Noah: fieldTransitionInProgress? */
extern s32 fieldTransitionInProgress;
extern s32 numDialogWindowsCreatedThisFrame;
extern s32 g_menuIdToOpen;
extern s32 iRam800adb70;
extern u8* rawFieldDialogBundle;
s32 isLoadCompleted(void);
s32 findFreeDialogWindow(void);
s32 getWindowWithLowestPriority(void);
s32 allocateDialogWindow(void);
void projectActorToScreen(s32 actor, s32* outX, s32* outY, s32 offsetY);
void flagWindowOpenBF(s32 bit);
void pcFieldDialogWide(s32 window, s32 ps1X, s32 wantX, s32 width);

void OP_WAIT_DIALOG(void) {
    s32 windowIndex;

    if (findDialogWindowForCurrentActor(&windowIndex) == -1) {
        fieldExectuteMaxCycles += 8;
        setVar(0x14, pCurrentFieldScriptActor->m81_selectedMultichoiceLine);
        pCurrentFieldScriptActor->mCC_scriptPC += 1;
        return;
    }
    if (fieldSceneState.m290_actorArray[gDialogWindows[windowIndex].m418_actorId].m4C_scriptEntity->m4_flags & 0x200) {
        u32 raw = pCurrentFieldScriptActor->m84_dialogWindowFlags;
        s32 flags;
        if (raw >> 16) {
            flags = (u16)(raw >> 16);
        } else {
            flags = (u16)raw;
        }
        if (!(flags & 1)) {
            if (pCurrentFieldScriptActor->m8C_scriptSlots[pCurrentFieldScriptActor->mCE_currentScriptSlot].m4_x12_priority != 7) {
                OP_STOP();
            }
            gDialogWindows[windowIndex].m414_keepOpen = 0;
        }
    }
    g_breakCurrentScript = 1;
}

void OP_SETUP_MULTICHOICE(void) {
    s32 windowIndex;
    s32 start;
    s32 last;
    DialogWindow18* w;

    if (findDialogWindowForCurrentActor(&windowIndex) == 0) {
        fieldExectuteMaxCycles += 8;
        if (ADD_DIALOG_SUB0(&gDialogWindows[windowIndex].m18_text) != 1) {
            w = &gDialogWindows[windowIndex].m18_text;
            if (w->m84_delay == 0) {
                goto end;
            }
            if (w->m6C_autoClose == 0) {
                goto end;
            }
        }
        gDialogWindows[windowIndex].m37C_cursor.m0_state = 0;
        pCurrentFieldScriptActor->m81_selectedMultichoiceLine = 0xFF;
        start = pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 1] >> 4;
        gDialogWindows[windowIndex].m37C_cursor.m2_startOfMultiChoice = start;
        last = pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 1];
        gDialogWindows[windowIndex].m37C_cursor.m6_currentMultiChoiceSelection = 0;
        gDialogWindows[windowIndex].m37C_cursor.m4_numMaxMultiChoiceSelection = (last & 0xF) - start + 1;
        setupMultiChoiceColor(&gDialogWindows[windowIndex].m18_text, 0xEF, 0x1E, 0xF0);
    }
    pCurrentFieldScriptActor->mCC_scriptPC += 2;
end:
    g_breakCurrentScript = 1;
}

s32 func_8009BE58(void) {
    return ((pCurrentFieldScriptActor->m12C_x9 - getCameraDirection()) & 7) < 5;
}

void OP_CLOSE_CURRENT_ACTOR_DIALOG(void) {
    sFieldScriptEntity* p = pCurrentFieldScriptActor;
    s32 windowIndex;

    if (pCurrentFieldScriptFile[p->mCC_scriptPC + 1] == 0) {
        if (findDialogWindowForCurrentActor(&windowIndex) == 0) {
            gDialogWindows[windowIndex].m414_keepOpen = 0;
            pCurrentFieldScriptActor->mCC_scriptPC += 2;
        } else {
            pCurrentFieldScriptActor->mCC_scriptPC += 2;
        }
    } else {
        p->m82_dialogWindowSize[0] = 0;
        p->m88_dialogWindowPosition[0] = 0;
        p->m88_dialogWindowPosition[1] = 0;
        pCurrentFieldScriptActor->m82_dialogWindowSize[1] = 0;
        pCurrentFieldScriptActor->m84_dialogWindowFlags = 0;
        pCurrentFieldScriptActor->mCC_scriptPC += 2;
    }
    g_breakCurrentScript = 1;
}

void OP_FC(void) {
    if (readCharacter(1) != 0xFF) {
        pCurrentFieldScriptActor->m80_dialogPortrait = fieldSceneState.m290_actorArray[readCharacter(1)].m4C_scriptEntity->m80_dialogPortrait;
        OP_SHOW_DIALOG_WINDOW_FOR_OTHER_ACTOR_MODE0();
    } else {
        pCurrentFieldScriptActor->mCC_scriptPC += 6;
    }
}

void OP_SHOW_DIALOG_WINDOW_FOR_OTHER_ACTOR_MODE0(void) {
    if (readCharacter(1) != 0xFF) {
        s32 actor = readCharacter(1);
        pCurrentFieldScriptActor->mCC_scriptPC++;
        if (showDialogWindowForActor(actor, 0) == -1) {
            pCurrentFieldScriptActor->mCC_scriptPC--;
        }
    } else {
        pCurrentFieldScriptActor->mCC_scriptPC += 6;
    }
}

void OP_SHOW_DIALOG_WINDOW_MODE0(void) {
    showDialogWindowForActor(currentFieldActorId, 0);
}

void OP_D3(void) {
    showDialogWindowForActor(currentFieldActorId, 1);
}

void OP_03(void) {
    showDialogWindowForActor(currentFieldActorId, 2);
}

void OP_SHOW_DIALOG_WINDOW_FOR_CURRENT_ACTOR_MODE3(void) {
    showDialogWindowForActor(currentFieldActorId, 3);
}

/* The mapping temporary holds an address, then its byte value. */
s32 isDialogAvatarLoaded(s32 portraitId) {
    s32 i;
    s32 found;
    uptr portraitMappingValue;

    for (i = 0; i < 3; i++) {
        if (portraitLoadingStatus[i].m2_status == 1) {
            if (waitReadCompletion(1) == 0) {
                portraitLoadingStatus[i].m2_status = 2;
                pcGameplayPortraitTim(D_800ADB10, pcPortraitBytes[0], portraitsLoadingCommands[0].m0_fileIndex);
                if (portraitLoadingStatus[i].m4_isAsymmetric)
                    pcGameplayPortraitTim(D_800ADB14, pcPortraitBytes[1], portraitsLoadingCommands[1].m0_fileIndex);
                loadTimToVram(D_800ADB10, ((s16*)partyPortraitsRects)[i * 8 + 0], ((s16*)partyPortraitsRects)[i * 8 + 1], ((s16*)partyPortraitsRects)[i * 8 + 2], ((s16*)partyPortraitsRects)[i * 8 + 3], 0x100, 1);
                if (portraitLoadingStatus[i].m4_isAsymmetric == 0) {
                    loadTimToVram(D_800ADB10, ((s16*)partyPortraitsRects)[i * 8 + 4], ((s16*)partyPortraitsRects)[i * 8 + 5], ((s16*)partyPortraitsRects)[i * 8 + 6], ((s16*)partyPortraitsRects)[i * 8 + 7], 0x100, 1);
                } else {
                    loadTimToVram(D_800ADB14, ((s16*)partyPortraitsRects)[i * 8 + 4], ((s16*)partyPortraitsRects)[i * 8 + 5], ((s16*)partyPortraitsRects)[i * 8 + 6], ((s16*)partyPortraitsRects)[i * 8 + 7], 0x100, 1);
                }
            }
            return -1;
        }
        if (portraitLoadingStatus[i].m2_status == 2) {
            portraitLoadingStatus[i].m2_status = 0;
            freeHeap(D_800ADB10);
            if (portraitLoadingStatus[i].m4_isAsymmetric == 1) {
                freeHeap(D_800ADB14);
            }
            return -1;
        }
    }
    for (i = 0; i < 3; i++) {
        if (portraitLoadingStatus[i].m0_id == portraitId) {
            pCurrentFieldScriptActor->m12C_x2 = i;
            return 0;
        }
    }
    found = 0;
    i = 0;
    while (1) {
        currentPortraitLoadingSlot++;
        if (currentPortraitLoadingSlot >= 3) {
            currentPortraitLoadingSlot = 0;
        }
        if (isPortraitUsed(portraitLoadingStatus[currentPortraitLoadingSlot].m0_id) == 0) {
            i++;
            found++;
            break;
        }
        i++;
        if (i >= 3) {
            break;
        }
    }
    if (found == 0) {
        return -1;
    }
    pCurrentFieldScriptActor->m12C_x2 = currentPortraitLoadingSlot;
    setCurrentDirectory(4, 0);
    portraitLoadingStatus[currentPortraitLoadingSlot].m0_id = portraitId;
    portraitLoadingStatus[currentPortraitLoadingSlot].m2_status = 1;
    portraitLoadingStatus[currentPortraitLoadingSlot].m4_isAsymmetric = 0;
    i = 0;
    portraitsLoadingCommands[i].m0_fileIndex = portraitMapping[portraitId * 2] + 0x46;
    pcPortraitBytes[0] = getFileSizeAligned(portraitsLoadingCommands[i].m0_fileIndex);
    portraitsLoadingCommands[i].m4_loadPtr = D_800ADB10 = allocateHeap(PC_HEAP_BYTES(pcPortraitBytes[0]), 0);
    i = 1;
    /* Reuse the 32-bit temporary for the mapping address, then its byte value. */
    portraitMappingValue = (uptr)&portraitMapping[portraitId * 2 + 1];
    if (*((u8*)portraitMappingValue) != portraitMapping[portraitId * 2]) {
        portraitLoadingStatus[currentPortraitLoadingSlot].m4_isAsymmetric = 1;
        portraitMappingValue = *((u8*)portraitMappingValue);
        portraitsLoadingCommands[1].m0_fileIndex = portraitMappingValue + 0x46;
        i = 2;
        pcPortraitBytes[1] = getFileSizeAligned((s32)(portraitMappingValue + 0x46));
        portraitsLoadingCommands[1].m4_loadPtr = D_800ADB14 = allocateHeap(PC_HEAP_BYTES(pcPortraitBytes[1]), 0);
    }
    portraitsLoadingCommands[i].m0_fileIndex = 0;
    portraitsLoadingCommands[i].m4_loadPtr = 0;
    batchStartLoadingFiles(portraitsLoadingCommands, 0, 0);
    return -1;
}

s32 isPortraitUsed(s32 portrait) {
    s32 i;

    for (i = 0; i < 4; i++) {
        if (gDialogWindows[i].m40E_state == 0 && gDialogWindows[i].m494_hasPortrait == 1 && gDialogWindows[i].m495_portrait == portrait) {
            return -1;
        }
    }
    return 0;
}

s32 showDialogWindowForActor(s32 actorId, s32 mode) {
    s32 windowIndex;
    s32 projX;
    s32 projY;
    s32 dialogWindow;
    u16 dialogIndex;
    s32 numActive;
    s32 activeFlags;
    s32 i;
    s32 width;
    s32 height;
    u32 flagFromScript;
    s32 boxX;
    s32 boxY;
    s32 flags;
    s32 x2;
    s32 y2;
    s32 wideWantX;
    s32 wideWidth;
    s32 pcPackWindow;

    fieldExectuteMaxCycles += 0x20;
    if (loadCompleted != 0 || fieldTransitionInProgress != 0 || numDialogWindowsCreatedThisFrame != 0 || g_menuIdToOpen != 0xFF ||
        (iRam800adb70 == 0 && isLoadCompleted() != 0)) {
        g_breakCurrentScript = 1;
        return -1;
    }
    if (pCurrentFieldScriptActor->m80_dialogPortrait != 0xFF && isDialogAvatarLoaded(pCurrentFieldScriptActor->m80_dialogPortrait) == -1) {
        g_breakCurrentScript = 1;
        return -1;
    }
    numDialogWindowsCreatedThisFrame++;
    if (findDialogWindowForCurrentActor(&dialogWindow) != -1) {
        goto found;
    }
    fieldExectuteMaxCycles += 8;
    dialogIndex = readU16FromScript(1);
    if (findFreeDialogWindow() != 0) {
        windowIndex = getWindowWithLowestPriority();
        if (windowIndex != 0xFFFF) {
            gDialogWindows[windowIndex].m414_keepOpen = 0;
            g_breakCurrentScript = 1;
            return -1;
        }
    } else {
        windowIndex = allocateDialogWindow();
    }
    i = 0;
    numActive = 0;
    activeFlags = 0;
    for (; i < 4; i++) {
        if (gDialogWindows[i].m40E_state == 0) {
            numActive++;
            activeFlags |= (s16)gDialogWindows[i].m40C_flags;
        }
    }
    width = ((s32 (*)(u8*, s32))getDialogParam0)(rawFieldDialogBundle, dialogIndex);
    height = ((s32 (*)(u8*, s32))getDialogParam1)(rawFieldDialogBundle, dialogIndex);
    pcPackWindow = pcGameTextOn && pcGameTextWindowOverridesScript(rawFieldDialogBundle, dialogIndex);
#if !VERSION_JP
    /* US only: the script's window size overrides the text size here too */
    if (mode == 0 || mode == 3) {
        if (!pcPackWindow) {
        if (pCurrentFieldScriptActor->m82_dialogWindowSize[0] != 0) {
            width = pCurrentFieldScriptActor->m82_dialogWindowSize[0];
        }
        if (pCurrentFieldScriptActor->m82_dialogWindowSize[1] != 0) {
            height = pCurrentFieldScriptActor->m82_dialogWindowSize[1];
        }
        }
    }
#endif
    {
        u32 old;
        s32 low;
        sFieldScriptEntity* p = pCurrentFieldScriptActor;

        old = p->m84_dialogWindowFlags;
        low = old & 0xFFFF;
        p->m84_dialogWindowFlags = low;
        flagFromScript = low;
        if (pCurrentFieldScriptFile[p->mCC_scriptPC + 3] != 0) {
            flagFromScript = (old & 0xFF00) | pCurrentFieldScriptFile[p->mCC_scriptPC + 3];
            p->m84_dialogWindowFlags = low | (flagFromScript << 16);
        }
    }
    boxY = 0x10;
    switch ((flagFromScript >> 4) & 3) {
    case 0:
        if (((pCurrentFieldScriptActor->m12C_x9 - getCameraDirection()) & 7) >= 5) {
            if (activeFlags & 0x80) {
                goto mode2;
            }
            if (numActive == 0) {
                goto mode1;
            }
            goto mode2;
        } else {
            if (!(activeFlags & 0x80)) {
                goto mode2;
            }
        }
    mode1:
    case 1:
        gDialogWindows[windowIndex].m40C_flags = 1;
        if (mode == 0 || mode == 3) {
            projectActorToScreen(actorId, &projX, &projY, -0x40);
            boxY = 0x14;
            if (mode == 0) {
                { s32 y = projY; boxY = y - height * 14 - 0x24; }
            } else {
                projX = 0xA0;
            }
            if (pCurrentFieldScriptActor->m80_dialogPortrait != 0xFF && !(flagFromScript & 2)) {
                height = 4;
                if (width < 0x18) {
                    width = 0x18;
                }
                width += 0x11;
                boxY = 0x10;
            }
            break;
        }
        width = 0x48;
        height = 4;
        boxY = 0x10;
        projX = 0xA0;
        break;
    case 2:
    mode2:
        gDialogWindows[windowIndex].m40C_flags = 0x81;
        if (mode == 0 || (boxY = 0x94, mode == 3)) {
            projectActorToScreen(actorId, &projX, &projY, -0x40);
            boxY = 0x94;
            if (mode == 0) {
                boxY = projY + 0x30;
            } else {
                projX = 0xA0;
            }
            if (pCurrentFieldScriptActor->m80_dialogPortrait != 0xFF && !(flagFromScript & 2)) {
                if (width < 0x18) {
                    width = 0x18;
                }
                width += 0x11;
                height = 4;
                boxY = 0x94;
            }
            break;
        }
        width = 0x48;
        height = 4;
        projX = 0xA0;
        break;
    }
    boxX = projX - (width * 2 + 8);
    wideWantX = boxX; /* before the clamps (port/src/game/field/wide.c) */
    wideWidth = width;
    if (boxX < 0xC) {
        boxX = 0xC;
    }
    x2 = boxX + 0x10;
    if (x2 + width * 4 >= 0x135) {
        boxX = 0x124 - width * 4;
    }
    if (boxY < 0x10) {
        boxY = 0x10;
    }
    y2 = boxY + 8;
    if (y2 + height * 14 >= 0xD5) {
        boxY = 0xCC - height * 14;
    }
    if (mode == 0 || mode == 3) {
        sFieldScriptEntity* p = pCurrentFieldScriptActor;

        if (p->m88_dialogWindowPosition[0] != 0) {
            boxX = p->m88_dialogWindowPosition[0];
            wideWantX = boxX; /* placed by the script: it stays */
        }
        if (p->m88_dialogWindowPosition[1] != 0) {
            boxY = p->m88_dialogWindowPosition[1];
        }
        if (!pcPackWindow) {
        if (p->m82_dialogWindowSize[0] != 0) {
            width = p->m82_dialogWindowSize[0];
        }
        if (p->m82_dialogWindowSize[1] != 0) {
            height = p->m82_dialogWindowSize[1];
        }
        }
        if (p->m80_dialogPortrait != 0xFF && !(flagFromScript & 2)) {
            height = 4;
        }
    }
    if (flagFromScript & 0x40) {
        gDialogWindows[windowIndex].m40C_flags |= 0x40;
    }
    flags = 0;
    if (!(flagFromScript & 0xC)) {
        flags = (((((s16)fieldSceneState.m290_actorArray[actorId].m4C_scriptEntity->m106_currentRotation >> 9) - getCameraDirection() + 1) & 7) >= 4) << 10;
    } else if (flagFromScript & 4) {
        flags = 0x400;
    }
    createDialogWindow(boxX, boxY, dialogIndex, windowIndex, width, height, currentFieldActorId, actorId, mode, flags, flagFromScript);
    if (pcGameTextWrapOn) {
        pcGameTextFieldWindow(&gDialogWindows[windowIndex].m18_text,
                             width - (gDialogWindows[windowIndex].m494_hasPortrait ? 0x11 : 0));
    }
    pcFieldDialogWide(windowIndex, boxX, wideWantX, wideWidth);
    flagWindowOpenBF(windowIndex);
    pCurrentFieldScriptActor->m104_rotation |= 0x8000;
    pCurrentFieldScriptActor->mCC_scriptPC += 4;
    return 0;
found:
    g_breakCurrentScript = 1;
    gDialogWindows[dialogWindow].m414_keepOpen = 0;
    return -1;
}

void flagWindowOpenBF(s32 bit) {
    windowOpenBF[0] |= 1 << bit;
}

s32 findDialogWindowForCurrentActor(s32* outputId) {
    s32 i;

    for (i = 0; i < 4; i++) {
        if (gDialogWindows[i].m416_fieldActorId == currentFieldActorId && gDialogWindows[i].m40E_state == 0) {
            *outputId = i;
            return 0;
        }
    }
    return -1;
}

s32 getCharacterToEntity(s32 offset) {
    s32 actor = readCharacter(offset);
    if (actor == 0xFF) {
        return partyFieldActorIds[0];
    }
    return actor;
}

s32 readCharacter(s32 offset) {
    s32 value = pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + offset];

    if (value == 0xFF) {
        value = partyFieldActorIds[0];
    } else if (value == 0xFE) {
        value = partyFieldActorIds[1];
    } else if (value == 0xFD) {
        value = partyFieldActorIds[2];
    } else if (value == 0xFB) {
        value = currentFieldActorId;
    }
    return value;
}

void OP_CF(void) {
    pCurrentFieldScriptActor->m88_dialogWindowPosition[0] = pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 1] * 2;
    pCurrentFieldScriptActor->m88_dialogWindowPosition[1] = pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 2];
    pCurrentFieldScriptActor->m82_dialogWindowSize[0] = pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 3] * 3;
    pCurrentFieldScriptActor->m82_dialogWindowSize[1] = pCurrentFieldScriptFile[pCurrentFieldScriptActor->mCC_scriptPC + 4];
    pCurrentFieldScriptActor->mCC_scriptPC += 5;
}

void OP_SET_DIALOG_WINDOW_PARAM(void) {
    pCurrentFieldScriptActor->m88_dialogWindowPosition[0] = getImmediateOrVariableUnsigned(1);
    pCurrentFieldScriptActor->m88_dialogWindowPosition[1] = getImmediateOrVariableUnsigned(3);
    pCurrentFieldScriptActor->m82_dialogWindowSize[0] = getImmediateOrVariableUnsigned(5) * 3;
    pCurrentFieldScriptActor->m82_dialogWindowSize[1] = getImmediateOrVariableUnsigned(7);
    pCurrentFieldScriptActor->m84_dialogWindowFlags = getImmediateOrVariableUnsigned(9);
    pCurrentFieldScriptActor->mCC_scriptPC += 11;
}
