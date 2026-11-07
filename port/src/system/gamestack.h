#ifndef PORT_SYSTEM_GAMESTACK_H
#define PORT_SYSTEM_GAMESTACK_H

/*
 * The game's own stack (gamestack.c).
 *
 * kernelMain runs on a stack of its own, a static array, so it sits at the
 * same address in every run of a build (the executable is not
 * position-independent): a save state (savestate.c) holds the stack's bytes
 * and the game's stack pointer, and a load puts them back, in the same run or
 * in a later one. The main thread's own stack moves from run to run (the
 * system randomises it) and holds the host's frames under main().
 *
 * The game parks (gameStackPark) at a safe point: the callee-saved registers
 * go onto its stack, and the host side (gameStackRun's loop, on the main
 * thread's stack) serves what was asked for (stateServe: save, load) and
 * switches back, to the same place or, after a load, to the place the state
 * was saved at. Everything still runs on the game thread, one side at a time.
 */

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* main.c: run entry() on the game stack until it returns (kernelMain does
 * not; the soft reset leaves by longjmp and calls this again). */
void gameStackRun(void (*entry)(void));

/* On the game stack: let the host side serve a request, then go on. */
void gameStackPark(void);

/* main.c's startKernel, about to longjmp from the game's stacks back to
 * main() (the soft reset): the game is off its stack, and on Windows the
 * thread's block names the host's stack again. */
void gameStackAbandon(void);

/* kernel.c, just before bootGame's frame is made (it lasts the whole game)
 * and before each mode's entry: zero the stack below the caller. The host
 * libraries set up before bootGame (the Vulkan driver, SDL) leave their
 * addresses there, different in every process, and frames have slots
 * nothing writes; a rewind capture's park leaves its own frames there too,
 * which a run without captures would not have. */
void gameStackScrub(void);

/* Whether the game runs on its stack now (0 before kernelMain and in the
 * extras' own loop). */
int gameStackActive(void);

/* The stack: its lowest address and size; while parked, the game's stack
 * pointer (the bytes from there to the top are the game's frames). */
unsigned char* gameStackBase(void);
size_t gameStackSize(void);
void* gameStackParkedSp(void);
/* A load: the game goes on from this stack pointer (one gameStackParkedSp
 * gave, with the stack's bytes put back) when the host switches back. */
void gameStackSetParkedSp(void* sp);
/* The lowest address the game's frames have reached (its pages below are
 * still zero: never written). */
unsigned char* gameStackLowWater(void);
void gameStackSetLowWater(unsigned char* low); /* a load put a state's stack back */

#ifdef __cplusplus
}
#endif

#endif /* PORT_SYSTEM_GAMESTACK_H */
