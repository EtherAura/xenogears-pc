#ifndef PORT_HW_H
#define PORT_HW_H

/*
 * The virtual PS1 hardware's clock and interrupts (port/src/system).
 *
 * The game runs on one thread. What the PS1 delivers as interrupts (vsync, CD
 * sector ready, the root counter that clocks the sound driver, DMA and drawing
 * completion) is queued here with the time it is due, and delivered as a
 * callback on the game thread at the next safe point: every platform function
 * the game calls starts with hwSafePoint(), and the game's few busy-wait loops
 * call portYield() (PORT_YIELD() in include/common.h). Nothing runs game code
 * concurrently, so the only interleavings are ones the PS1 could also produce.
 *
 * Interrupt masking (EnterCriticalSection, DisableEvent for the sound tick)
 * defers delivery until it is lifted, as on the PS1.
 */

#include <stdint.h>

/*
 * The PS1's clocks (psx-spx "GPU Timings", "Timers"). The CPU's, 44100 Hz *
 * 768, also clocks the root counters (the sound driver's 240 Hz tick), the
 * SPU (one sample per 768 cycles) and the CD drive (a sector per 451584
 * cycles at single speed). The GPU runs from a video crystal of its own: a
 * horizontal line is 3412.5 video clocks on NTSC (3405 on PAL) and a field
 * 263 lines (interlaced 262.5; PAL 314 and 312.5), so an NTSC console has a
 * vblank at 59.826 Hz in the 240-line modes and at 59.940 Hz interlaced.
 * Everything here is in seconds of one virtual clock (hwNow).
 */
#define HW_CPU_HZ 33868800.0
#define HW_VIDEO_HZ_NTSC 53693175.0
#define HW_VIDEO_HZ_PAL 53203425.0
#define HW_LINE_CLOCKS_NTSC 3412.5
#define HW_LINE_CLOCKS_PAL 3405.0
#define HW_FIELD_LINES_NTSC 263.0
#define HW_FIELD_LINES_NTSC_INTERLACED 262.5
#define HW_FIELD_LINES_PAL 314.0
#define HW_FIELD_LINES_PAL_INTERLACED 312.5

typedef void (*HwEvent)(void* arg);

/* Seconds since startup (the virtual clock follows real time). */
double hwNow(void);

/* Real time only: the virtual clock runs `rate` seconds per real second from
 * now on (1 by default; displaylock.c's lock of the vblank to the display
 * sets it within 0.4% of 1). The stepped clock ignores it. */
void hwSetClockRate(double rate);
/* ... that rate (the sound output plays at it, port/src/spu/output.c). */
double hwClockRate(void);

/* Battle speed (port/src/system/battlespeed.c): from now on the game's clock
 * runs `factor` (1 to HW_GAME_SPEED_MAX) game seconds per second of the audio
 * clock, which the sound driver's root counter (counter 2, libapi.c) and the
 * SPU's samples (spu.c) follow, so the music keeps its tempo and pitch. On the
 * real clock the audio clock keeps real time (the vblanks, the CD and every
 * other interrupt come `factor` times as often); on the stepped clock it is
 * the game's time divided by the factor. Until the factor first changes the
 * audio clock is the game's time itself, to the bit. Not in a world (world.h:
 * its own clock, at 1). */
#define HW_GAME_SPEED_MAX 4
void hwSetGameSpeed(int factor);
int hwGameSpeed(void);
/* Game code runs in a world of its own (world.h: the extras' sandbox). */
int hwInWorld(void);
/* The audio clock at the game's time t (hwNow() units; t at or after the last
 * change), and the game's time at which the audio clock reads a. */
double hwAudioTime(double t);
double hwAudioToGame(double a);

/* Seconds per vblank (per field) in the current video mode. */
double hwVSyncPeriod(void);

/* The GPU's display mode, as GP1(08h) sets it: vertical interlace (bit 5;
 * LIBGPU sets it for isinter and for the 480-line modes) and the video clocks
 * per pixel (10, 8, 7, 5 or 4 for 256 to 640 pixels: timer 0's dot clock).
 * The vblank after the next one comes at the new rate. */
void hwSetDisplayTiming(int interlaced, int dotClocks);

/* Queue fn(arg) to run on the game thread at the first safe point at or
 * after time `at` (hwNow() units). Events due at the same time run in the
 * order they were queued. */
void hwScheduleAt(double at, HwEvent fn, void* arg);

/* Deliver every event that is due. Every platform entry point the game calls
 * starts with this. Re-entrant calls (a callback calling a platform function)
 * do not deliver again. */
void hwSafePoint(void);
/* Work the PS1 does not do (the Enhanced engine's extra pictures): between
 * hwClockHold(1) and hwClockHold(0) safe points take no time on the stepped
 * clock nor in the slowdown replay and deliver nothing. Nests. */
void hwClockHold(int on);
/* Work the PS1 does later in its frame than the Enhanced engine (the 60 Hz
 * world map's tick, xg-hbc.19.1): between hwClockDefer(1) and (0) safe points
 * deliver nothing and their stepped-clock time is kept for hwClockSettle,
 * which spends it (and delivers what came due). Nests. */
void hwClockDefer(int on);
void hwClockSettle(void);
/* Optional display opportunity in a game busy wait for the next vblank
 * (VSync(-1) polled until the count changes): the host may present until
 * that vblank. No clock or event changes, and no work during stepped
 * playback or interrupts. */
void hwPresentPoll(void);
/* A game busy wait on VSync(-1) (libetc.c; the field's frame cap) on the real
 * clock: the host presents, then the thread sleeps until the next event (the
 * vblank is still delivered at its time, the game sees the same counts)
 * instead of polling on at a safe point's cost. 0 on the stepped clock, in a
 * world or paused: the caller polls as before (there every poll is a safe
 * point whose cost is part of the modelled time). */
int hwVBlankIdle(void);
/* vsync_wait: real-time waits spin instead of sleeping (a steady CPU clock,
 * vblanks on time, one core busy). hwSetSpinWait saves the setting. */
int hwSpinWait(void);
void hwSetSpinWait(int on);

/* A safe point inside a game busy-wait loop: delivers what is due and, when
 * nothing is, sleeps until the next event so the loop does not spin hot. */
void hwYield(void);

/* The PS1's CPU would be busy here for `seconds` with work the PC has just
 * done at once (a measured cost: LIBPRESS's DecDCTvlc, port/src/mdec/vlc.c).
 * The clock moves on by that much and the events that come due meanwhile are
 * delivered at their times, as their interrupts would break into the PS1's
 * computation; on the real-time clock the thread waits it out. Does nothing
 * inside a callback, with interrupts masked, or in a world of its own
 * (world.h: its clock moves only when it runs or waits). */
void hwBusy(double seconds);
void hwVSyncResume(void); /* a wait for the vblank returns: the time the PS1's vblank handlers take */

/* Block until the next vertical blank (hwVSyncPeriod), delivering events
 * meanwhile; pumps the window's events. Returns the vsync count. */
uint32_t hwWaitVSync(void);
/* Speedup battle/field pacing: use a vblank already delivered since
 * `previous`, otherwise wait normally. Real game clock only; worlds/stepped
 * clocks keep their existing frame boundaries. Parks for a save state as
 * hwWaitVSync does, later in the frame when the vblank came during drawing
 * (savestate.h). */
uint32_t hwWaitVSyncSince(uint32_t previous);

/* Number of vertical blanks since startup. */
uint32_t hwVSyncCount(void);

/* Slowdown replay (slowdown.c, XG_SLOWDOWN): the drive reports each read
 * command (ReadN/ReadS, the Setloc sector or -1), asks when a data sector due
 * at `at` may reach the CPU (the PS1's time for the same one, or `at`), and
 * reports each one it delivers to a ready callback. */
void slowdownCdRead(int command, int32_t lba);
/* The PS1's time for the first sector of the read just reported, or < 0. */
double slowdownCdFirst(void);
double slowdownCdHold(double at, int32_t lba);
void slowdownCdDelivered(void);
/* Each command written to the drive: seconds the PS1 took to complete it (from
 * the write to the interrupt that completes it), or < 0 to use the drive's
 * own timing; and each completion LIBCD sees (for XG_SLOWDOWN_LOG). */
double slowdownCdCommand(int command);
void slowdownCdComplete(int status);

/* Interrupt masking: while masked, hwSafePoint delivers nothing. Nests. */
void hwMaskInterrupts(void);
void hwUnmaskInterrupts(void);

/* The settings menu (port/src/ui) pauses the game: between hwPause(1) and
 * hwPause(0) the virtual clock stands still (the real time spent is taken
 * out of it). hwPausedFrame: once per menu frame (the stall watchdog sees the
 * thread alive; a quit request is acted on). */
void hwPause(int on);
void hwPausedFrame(void);

#endif /* PORT_HW_H */
