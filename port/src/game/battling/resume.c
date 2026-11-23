/*
 * The Battling arena's safe point (include/pc_state.h). The arena's game runs
 * in a coroutine (thread.c): battlingMain's loop resumes battlingMainTask once
 * a frame, and the task yields at the end of its frame. Two of its yields are
 * where players wait: the main menu and gear select loop, and the fight loop
 * (fights, the pause menu, replays, round results, the victory screen). Each
 * keeps the task's locals here just before it yields; every other yield
 * clears the mark when the task goes on (yieldBattlingThread), so
 * battlingMain's safe point parks only while the task waits at one of those
 * two. A stackless load restarts the coroutine in its own BattlingThread, and
 * the task takes up its loop after that yield. Part of the battling module:
 * saved with its BSS.
 */
#include "common.h"
#include "pc_state.h"
#include "battling/battling.h"

void pcBattlingThreadRestart(BattlingThread* t); /* thread.c */

static struct {
    s32 site; /* 1 the menu loop's yield, 2 the fight loop's; 0 any other */
    BattlingScene* task;
    s32 timer;
    s32 state;
    s32 hold;
    BattlingThread* thread; /* battlingMain's */
    u32 prev;               /* battlingMain's last frame's play time (its frame rate readout) */
} arena;

void pcBattlingKeep(s32 site, BattlingScene* task, s32 timer, s32 state, s32 hold) {
    arena.site = site;
    arena.task = task;
    arena.timer = timer;
    arena.state = state;
    arena.hold = hold;
}

void pcBattlingYielded(void) {
    arena.site = 0;
}

/* battlingMainTask's entry: which yield to go on after (0: none, a fresh start) */
s32 pcBattlingResume(BattlingScene** task, s32* timer, s32* state, s32* hold) {
    if (PC_SAFE_MODE(pcResumeAt()) != PC_SAFE_MODE(PC_SAFE_BATTLING)) {
        return 0;
    }
    *task = arena.task;
    *timer = arena.timer;
    *state = arena.state;
    *hold = arena.hold;
    pcResumeDone();
    return arena.site;
}

void pcBattlingSafePoint(BattlingThread* thread, u32 prev) {
    arena.thread = thread;
    arena.prev = prev;
    pcSafePoint(arena.site ? PC_SAFE_BATTLING : PC_SAFE_NONE);
}

/* battlingMain's entry after a stackless load: its coroutine, to start again */
BattlingThread* pcBattlingRestart(u32* prev) {
    *prev = arena.prev;
    pcBattlingThreadRestart(arena.thread);
    return arena.thread;
}
