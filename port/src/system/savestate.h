#ifndef PORT_SYSTEM_SAVESTATE_H
#define PORT_SYSTEM_SAVESTATE_H

/*
 * Save states (savestate.c, statefile.c, statecodec.c, gamestack.c).
 *
 * A state is the virtual PS1 as the game code sees it, taken with the game
 * parked in its VSync wait (hw.c hwWaitVSync calls stateVBlankPoint right
 * after a vertical blank; gamestack.h parks it; with Speedup, battle's and
 * the field's main flip park later in the frame when the vblank came while
 * they drew, after its handlers: hwWaitVSyncSince): every game module's data
 * and BSS, the heap arena, the game's own stack, the scratchpad, and each
 * platform file's game-visible statics, which that file lists in its
 * xxxStateIo function. All of it sits at fixed addresses in the executable,
 * so a load puts the bytes back in place and every pointer in them holds,
 * in the same run or after a restart of the same build. Host things (SDL,
 * Vulkan, the menu, settings, card files on disk) are never in a state; the
 * xxxStateLoaded hooks rebuild the host side from the restored state.
 * savestate_inventory.txt classifies every platform static (a gate).
 *
 * Keys (pad.c): F7 save to the selected slot, F8 load it, F9 / Shift+F9 the
 * next / previous slot, Shift+F8 undo the last load (the game as it was
 * before it). F1 > Save states lists the slots. `xenogears --state FILE`
 * starts from a state. Rewind (rewind.h, off by default) keeps the last
 * seconds as captures of the same kind: hold R or Select+L2 to go back. Linux x86-64 only for now (Windows: the game stack
 * and Battling's fibers need their own switch first).
 *
 * Harness (headless tests, tools/statecheck.py):
 *   XG_STATE_SAVE=<vblank>:<file>[,<vblank>:<file>...]  save at the first
 *                     park at or after that vblank
 *   XG_STATE_LOAD=<file>   load at the first park (the boot's first VSync)
 *   XG_STATE_EVERY=<n>:<file>  save every n vblanks to that file (over the
 *                     last: a run that saves must equal one that does not)
 *   XG_STATE_RELOAD=1      load each harness save right back (in the same run)
 *   XG_STATE_LOG=1         a line per park served
 */

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- the files' part: xxxStateIo(XgState*) ------------------------------ */

typedef struct XgState XgState;

/* Weak where the files declare them: unit tests link single platform files
 * without savestate.c (their StateIo functions are never called there). */
#if defined(__GNUC__)
#define XG_STATE_WEAK __attribute__((weak))
#else
#define XG_STATE_WEAK
#endif

/* Whether this pass restores, or saves (a load first checks the whole state
 * in a pass that does neither: side effects belong under xgStateLoading). */
int xgStateLoading(const XgState* s) XG_STATE_WEAK;
int xgStateSaving(const XgState* s) XG_STATE_WEAK;
/* n bytes at p, tagged: copied into the state, or back from it. */
void xgStateIo(XgState* s, const char* tag, void* p, size_t n) XG_STATE_WEAK;
/* The same for a copy the module made aside, kept until its next save or
 * capture (vram.c's snapshot): a rewind capture lists it as stable (rewind's
 * worker compares it later, the game going on) rather than copying it. */
void xgStateIoAside(XgState* s, const char* tag, void* p, size_t n) XG_STATE_WEAK;
/* A length (or any value) that sizes what follows: saved as given, returned
 * as saved (in the checking pass too). */
size_t xgStateLen(XgState* s, const char* tag, size_t n) XG_STATE_WEAK;
/* Saving pixels the device holds (VRAM wide 0, the wide target wide 1) into
 * dst, pitch in pixels, before dst is saved: in a rewind capture the copy is
 * queued behind the frame and lands later (1); otherwise 0: read them now. */
int xgStateLate(XgState* s, int wide, int x, int y, int w, int h, uint16_t* dst, int pitch) XG_STATE_WEAK;
#define XG_STATE_VAR(s, v) xgStateIo((s), #v, (void*)&(v), sizeof(v))

/* Each file's game-visible statics (savestate.c's parts[] has the order). */
void hwStateIo(XgState* s);
void apiStateIo(XgState* s);
void etcStateIo(XgState* s);
void libcStateIo(XgState* s);
void padStateIo(XgState* s);
void memoryStateIo(XgState* s);
void gameplayStateIo(XgState* s);
void gauntletStateIo(XgState* s);
void gameIdStateIo(XgState* s);
void arenaStateIo(XgState* s);
void optionsStateIo(XgState* s);
void gteStateIo(XgState* s);
void libgteStateIo(XgState* s);
void cardStateIo(XgState* s);
void cardImageStateIo(XgState* s);
void spuStateIo(XgState* s);
void spuCoreStateIo(XgState* s);
void spuCdStateIo(XgState* s);
void driveStateIo(XgState* s);
void libcdStateIo(XgState* s);
void streamStateIo(XgState* s);
void swapStateIo(XgState* s);
void xaStateIo(XgState* s);
void bootStateIo(XgState* s);
void regionalStateIo(XgState* s);
void pressStateIo(XgState* s);
void mdecStateIo(XgState* s);
void vlcStateIo(XgState* s);
void gpuStateIo(XgState* s);
void vramStateIo(XgState* s);
void gp0StateIo(XgState* s);
void wideStateIo(XgState* s);
void primStateIo(XgState* s);
void quitAtStateIo(XgState* s);
void gametextStateIo(XgState* s);
void gametextFontStateIo(XgState* s);
void moviePackStateIo(XgState* s);
void battleSpeedStateIo(XgState* s);

/* Before a save: the device's drawing read back into the CPU copy of VRAM,
 * owed texture-cache walks done (gp0.c; output-identical). */
void gp0StateFlush(void);
/* After a load, in this order (savestate.c loadedHooks). */
void precisionStateLoaded(void);
void mouseStateLoaded(void);
void mouseBattleStateLoaded(void);
void rumbleGameStateLoaded(void);
void spuOutputStateLoaded(uint64_t dumpFrames);
void gpuStateLoaded(void);
void dumpStateLoaded(void);
/* spu/output.c: frames written to XG_AUDIO_DUMP so far (a state keeps it). */
uint64_t spuOutputDumpFrames(void);

/* ---- the park point and the host side ---------------------------------- */

/* hw.c hwWaitVSync, after the vblank: parks the game when a request or a
 * rewind capture is due (one flag test otherwise). */
void stateVBlankPoint(void);
/* gamestack.c, on the host side of a park. */
void stateServe(void);

/* ---- requests (keys, F1 > States, the harness) ------------------------- */

#define STATE_SLOTS 10
#define STATE_UNDO 0 /* the slot "Before the last load" */

void stateRequestSave(int slot) XG_STATE_WEAK;         /* 1..STATE_SLOTS */
void stateRequestLoad(int slot) XG_STATE_WEAK;         /* 1..STATE_SLOTS, or STATE_UNDO */
void stateRequestLoadFile(const char* path) XG_STATE_WEAK;
int stateSelectedSlot(void) XG_STATE_WEAK;
void stateSelectSlot(int slot) XG_STATE_WEAK;          /* wraps around (F9 past 10 is 1) */
void stateNoteSlot(void) XG_STATE_WEAK;                /* a corner note: the selected slot's contents */
/* Whether states can be used at all; else why not (a sentence). */
int stateAvailable(const char** why);
/* A save or load waits for the game's next VSync wait. */
int stateBusy(void);

/* A slot as F1 > States shows it. thumb: RGB888 thumbW x thumbH, malloc'd. */
typedef struct {
    int exists;        /* a file is there */
    int usable;        /* this build can load it */
    char why[160];     /* !usable: why not */
    int64_t savedAt;   /* Unix seconds */
    int disc;
    uint32_t vblank;
    uint32_t playTime; /* seconds, the game's own clock */
    char label[96];
    int thumbW, thumbH;
    unsigned char* thumb;
} XgStateInfo;
int stateSlotInfo(int slot, XgStateInfo* out); /* 1 if a file is there */
void stateInfoFree(XgStateInfo* info);
int stateDeleteSlot(int slot);
/* Changes whenever a slot's file changed (the menu reads the slots again). */
int stateSerial(void);

#ifdef __cplusplus
}
#endif

#endif /* PORT_SYSTEM_SAVESTATE_H */
