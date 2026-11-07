#ifndef PORT_EXTRAS_SANDBOX_H
#define PORT_EXTRAS_SANDBOX_H

/*
 * Game code run outside the game (sandbox.c): the extras' viewers call the
 * game's own decoders (relocation, model and sprite setup, texture uploads,
 * the file system's table lookups...) on data from the disc, while the game
 * is paused, without changing anything of it.
 *
 * Decision: a world of its own, swapped in and out, rather than
 * a snapshot of the game restored afterwards. A sandbox has its own copy of
 *   - the data and BSS of the modules it names (main always: the heap's own
 *     state, the GTE and model globals live there), starting as the modules
 *     were loaded (extrasInit keeps their data; BSS zero), not as the paused
 *     game has them;
 *   - a heap arena (malloc'd, default 16 MB) that the game's allocateHeap
 *     works in: initHeap runs on it the first time, with failures returning
 *     NULL (setHeapAllowFailure) instead of the game's fatal error screen;
 *   - the scratchpad (1 KB at 0x1F800000) and the GTE's registers;
 *   - rand()'s seed (port/src/system/libc.c: 0 at first, as at power-on);
 *   - a GPU (vgpu.h): LoadImage, DrawOTag and the rest go there (gpu.h
 *     gpuSetCapture), not to VRAM or the picture.
 * xgSandboxEnter puts the game's copies of all that aside and the sandbox's
 * in place; xgSandboxLeave swaps them back. Inside, interrupts are masked
 * (hwMaskInterrupts: no vblank, CD or sound callback runs game code against
 * the wrong world), the wide picture's hooks are off (pcWideView/Cull 0), and
 * the clock stands still (the extras run while the game is paused: hwPause).
 * The game's heap, VRAM and module state are never written, so its frames and
 * sound after the extras are those of a run that never opened them.
 * Swapping costs a copy of the named modules (main: about 320 KB each way).
 *
 * Rules for code run inside:
 *   - it must not wait for the hardware: no VSync, no CD reads, no DrawSync
 *     waits that need a callback (interrupts are masked);
 *   - it works on data in the sandbox's heap (xgSandboxCopy): pointers into
 *     the game's heap must not be followed;
 *   - one sandbox is entered at a time, and enter/leave pair up on the same
 *     call stack (a viewer typically enters, calls, and leaves in one frame).
 */

#include <stddef.h>
#include <stdint.h>

#include "vgpu.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct XgSandbox XgSandbox;

/* modules: NULL-terminated names of the modules besides main whose data the
 * code uses (as in build/.../gen/modules.c: "field", "battle"...), or NULL.
 * heapBytes: 0 for 16 MB. NULL if out of memory. */
XgSandbox* xgSandboxCreate(const char* const* modules, size_t heapBytes);
void xgSandboxDestroy(XgSandbox* sb);

void xgSandboxEnter(XgSandbox* sb);
void xgSandboxLeave(XgSandbox* sb);
XgSandbox* xgSandboxCurrent(void); /* the sandbox inside, or NULL */

/* A fresh world: the modules as loaded, an empty heap, the GPU cleared
 * (VRAM too if clearVram). Inside or outside. */
void xgSandboxReset(XgSandbox* sb, int clearVram);
/* Outside, after Reset and before Enter: deterministic unused native fields.
 * Refused once the heap is in use; never clears the paused game's heap. */
void xgSandboxZeroHeap(XgSandbox* sb);

/* Its GPU (valid inside and outside). */
XgVgpu* xgSandboxGpu(XgSandbox* sb);

/* Inside: `size` bytes copied into a new heap block (16 more, zeroed: LZSS
 * streams and some readers run a little past the end); NULL if full. */
void* xgSandboxCopy(const void* data, size_t size);
/* Inside: a heap block (allocateHeap mode 0), zeroed. */
void* xgSandboxAlloc(size_t size);
/* The arena's bounds (a pointer in the sandbox's heap is between them). */
int xgSandboxOwns(const XgSandbox* sb, const void* p);

/* The modules' data as loaded (extrasInit), by name; NULL if unknown. */
const void* xgModulePristineData(const char* name, size_t* size);

/* extras.h extrasInit's work: keeps every module's data as it is now. */
void xgSandboxInit(void);

#ifdef __cplusplus
}
#endif

#endif /* PORT_EXTRAS_SANDBOX_H */
