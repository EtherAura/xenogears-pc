#ifndef PORT_SYSTEM_WORLD_H
#define PORT_SYSTEM_WORLD_H

/*
 * A world of its own for the platform's interrupt side and the SPU, for game
 * code the extras run while the game is paused (port/src/extras: the jukebox
 * runs the game's sound driver in one, inside a code sandbox for the driver's
 * own state). The sound driver's set-up opens a BIOS event, programs a root
 * counter, sets LIBSPU callbacks and transfers samples; in a world all of that
 * is the world's, never the paused game's.
 *
 * A world has its own
 *   - clock and interrupt queue (hw.c): a stepped clock from 0, moved only by
 *     hwWorldRun or by game code in the world that waits (hwYield goes to the
 *     next event), with interrupt masking and delivery of its own;
 *   - BIOS events, root counters and critical section (libapi.c): the
 *     driver's 240 Hz tick is the world's root counter 2;
 *   - LIBSPU state (spu.c): the transfer address, mode and callback, the IRQ
 *     callback, the reverb attributes; transfers complete through the world's
 *     queue;
 *   - SPU: the core's state (RAM, voices, reverb), the register block the
 *     driver writes (portSpuRegisters) and the CD input (empty). Its samples
 *     go to the world's sink, never to the audio device.
 *
 * hwWorldEnter puts the game's aside and the world's in place, hwWorldLeave
 * swaps them back; one world is in at a time, entered and left on the same
 * call stack. The game's queue, clock, events and SPU are not touched: its
 * frames and sound after the extras are those of a run that never opened
 * them. Entering costs copies of the SPU state (about 530 KB each way).
 */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct HwWorld HwWorld;
typedef void (*HwWorldSink)(void* user, const int16_t* frames, uint32_t count);

/* A fresh world: time 0, nothing queued, the SPU as SpuInit leaves it (RAM
 * clear, voices parked, no sink). NULL if out of memory. */
HwWorld* hwWorldCreate(void);
void hwWorldDestroy(HwWorld* w);

void hwWorldEnter(HwWorld* w);
void hwWorldLeave(HwWorld* w);
HwWorld* hwWorldCurrent(void);

/* Inside: where the SPU's samples go (interleaved stereo s16, 44.1 kHz; NULL:
 * dropped), and whether the SPU runs at all (0: time passes, the register
 * writes are taken, nothing is mixed: the driver alone, fast). */
void hwWorldSetSink(HwWorldSink sink, void* user);
void hwWorldSetSpu(int on);

/* Inside: let `seconds` of the world's time pass. Each event is delivered at
 * its own time (the root counter's 240 Hz tick, transfer completions) with
 * the SPU run up to it. */
void hwWorldRun(double seconds);

/* Inside: the world's time in seconds (hwNow). */
double hwWorldTime(void);

#ifdef __cplusplus
}
#endif

#endif /* PORT_SYSTEM_WORLD_H */
