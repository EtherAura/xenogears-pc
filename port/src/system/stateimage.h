#ifndef PORT_SYSTEM_STATEIMAGE_H
#define PORT_SYSTEM_STATEIMAGE_H

/*
 * A state as a list of chunks in memory, between savestate.c and rewind.c
 * (internal). A rewind capture lists the parked game's chunks without
 * copying the large ones (they are the game's own static memory), compares
 * them with the last capture and keeps what changed; going back restores a
 * list of chunks as a load restores a file.
 */

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const char* tag; /* "<part>.<name>", tagLen bytes (not terminated) */
    int tagLen;
    const uint8_t* data; /* when late, valid only after stateLateFinish (may be NULL before it) */
    size_t size;
    int late; /* listed with late: pixels the device still owes (stateLateFinish) */
    int stable; /* data stays as listed until the list after next (a copy the listing made, the
                 * device's late mapping, a module's own snapshot made aside): rewind's worker
                 * may compare it while the game goes on; else the game's live memory, in
                 * place, only while the game is parked */
} StateChunkRef;

/* On the host side of a park: the game's chunks now. Large chunks point at
 * the game's static memory (modules, heap, stack, VRAM, SPU), the others at
 * copies; valid until the game goes on or the next call. 0 if states cannot
 * be taken now. With late, the device's pixels (VRAM drawn since its last
 * readback, the wide target) are copied behind the frame instead of waited
 * for: the chunks marked late are complete once stateLateFinish returns,
 * which the next list, or any save, calls first. A late chunk's pixels then
 * stay where they are until the list after next (the copies are made by
 * turns), so rewind's pack worker compares them while the game goes on. */
int stateListChunks(const StateChunkRef** chunks, int* count, int late);
void stateLateFinish(void);

/* On the host side of a park: the game put back from chunks (a list taken
 * by stateListChunks in this run), checked first; the disc it was on put
 * in; the host side rebuilt (audio dump at dumpFrames). 0 with why if the
 * chunks do not fit (nothing touched then). */
int stateRestoreChunks(const StateChunkRef* chunks, int count, int disc, uint64_t dumpFrames, char* why,
                       size_t whySize);

/* rewind.c, from savestate.c */
void rewindReset(void);                 /* a load or a reset: the history is another timeline's */
int rewindWanted(uint32_t vblank);      /* at a park point: REWIND_CAPTURE, REWIND_BACK or 0 */
void rewindServe(int what);             /* on the host side of that park */
void rewindSettle(void);                /* a capture still owed device pixels: completed now */
enum { REWIND_CAPTURE = 1, REWIND_BACK = 2 };

#ifdef __cplusplus
}
#endif

#endif /* PORT_SYSTEM_STATEIMAGE_H */
