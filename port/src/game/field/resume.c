/*
 * The field main loop's safe point (include/pc_state.h): fieldEntryPoint's
 * loop keeps its locals here at the top of each frame, where a canonical
 * state is saved, and takes them back when a stackless load starts the game
 * again there. Part of the field module: saved with its BSS.
 */
#include "common.h"
#include "pc_state.h"

static struct {
    s32 firstFrame;
    s32 comboHeld;
} fieldLoop;

void pcFieldSafePoint(s32 firstFrame, s32 comboHeld) {
    fieldLoop.firstFrame = firstFrame;
    fieldLoop.comboHeld = comboHeld;
    pcSafePoint(PC_SAFE_FIELD);
}

/* 1: the loop's top; 2: its call of openFieldMenu (a menu loop is the safe point) */
s32 pcFieldResume(s32* firstFrame, s32* comboHeld) {
    if (PC_SAFE_MODE(pcResumeAt()) != PC_SAFE_MODE(PC_SAFE_FIELD)) {
        return 0;
    }
    *firstFrame = fieldLoop.firstFrame;
    *comboHeld = fieldLoop.comboHeld;
    if (pcResumeMenu()) {
        return 2;
    }
    pcResumeDone();
    return 1;
}

/* openFieldMenu's locals that outlive the menu (src/field/f_80078B5C.c) */
static struct {
    void* menuBuffer;
    uptr heapTop;
    void* walkMeshCopy;
    void* mechaState;
    u32 sfxLoops;
    s32 wideKind;
} fieldMenu;

void pcFieldMenuKeep(void* menuBuffer, uptr heapTop, void* walkMeshCopy, void* mechaState, u32 sfxLoops, s32 wideKind) {
    fieldMenu.menuBuffer = menuBuffer;
    fieldMenu.heapTop = heapTop;
    fieldMenu.walkMeshCopy = walkMeshCopy;
    fieldMenu.mechaState = mechaState;
    fieldMenu.sfxLoops = sfxLoops;
    fieldMenu.wideKind = wideKind;
}

s32 pcFieldMenuTake(void** menuBuffer, uptr* heapTop, void** walkMeshCopy, void** mechaState, u32* sfxLoops, s32* wideKind) {
    if (PC_SAFE_MODE(pcResumeAt()) != PC_SAFE_MODE(PC_SAFE_FIELD) || !pcResumeMenu()) {
        return 0;
    }
    *menuBuffer = fieldMenu.menuBuffer;
    *heapTop = fieldMenu.heapTop;
    *walkMeshCopy = fieldMenu.walkMeshCopy;
    *mechaState = fieldMenu.mechaState;
    *sfxLoops = fieldMenu.sfxLoops;
    *wideKind = fieldMenu.wideKind;
    return 1;
}
