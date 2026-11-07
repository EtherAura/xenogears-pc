/*
 * The invariant log (XG_INVARIANT_LOG=file): what the game's state looks
 * like at every vblank, written as text so two runs can be compared line by
 * line (tools/engine_diff.py). It is the oracle of the Enhanced engine
 * (docs/ENGINE_PLAN.md): that engine changes pictures and timing on purpose
 * and cannot be held to the PS1's frames, so it is held to the Original's
 * game state instead, replaying the same scripted input on both.
 *
 * One line per vblank, at the point a save state is taken (hwWaitVSync, after
 * the vblank's handlers; not inside the extras' worlds):
 *
 *   v <vblank> m <mode> gs <hash> ...
 *     mode   the screen kind (pc_wide.h: 0 kernel, 1 field, 2 battle,
 *            3 world map, 4 Battling, 5 menu, 6 movie)
 *     gs     FNV-1a 64 of gameState (0x2358 bytes, no pointers: party,
 *            gears, inventory, gold, flags, field variable backup, field id)
 *   field:   fv <hash of the 1024 live field variables>
 *            p <actor> <x> <y> <z>   the party leader's position (its
 *                                    script entity's m20_position), when
 *                                    the actor array holds it
 *   world:   w <x> <y> <z> <hash>   entry 0's position, and a hash of
 *                                    every entry's state, sub-state and
 *                                    position (pointers left out)
 *
 * and, when the game writes a memory-card frame:
 *
 *   c <vblank> <slot> <sector> <hash of the 128 bytes>
 *
 * Nothing here is read back by the game; the log is host-side only and the
 * file is closed by exit. Host pointers never enter a line: two different
 * executables lay memory out differently and the log must agree between them.
 */
#include "pc_retail_heap.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common.h"
#include "field/field.h"
#include "main/game_state.h"
#include "pc_wide.h"
#include "worldmap/worldmap.h"

#include "hw.h"
#include "invariants.h"
#include "system.h"

extern sWorldmapState* gWorldmapState;

static FILE* logFile;
static int opened;

static uint64_t fnv(uint64_t h, const void* data, size_t n) {
    const unsigned char* p = (const unsigned char*)data;
    for (size_t i = 0; i < n; i++) {
        h = (h ^ p[i]) * 0x100000001B3ull;
    }
    return h;
}

#define FNV_INIT 0xCBF29CE484222325ull

static FILE* file(void) {
    if (!opened) {
        const char* path = getenv("XG_INVARIANT_LOG");
        opened = 1;
        if (path != NULL && path[0] != '\0') {
            logFile = fopen(path, "w");
            if (logFile == NULL) {
                fprintf(stderr, "invariants: cannot write XG_INVARIANT_LOG %s\n", path);
            }
        }
    }
    return logFile;
}

int invariantLogging(void) {
    return file() != NULL;
}

static void fieldLine(FILE* f) {
    fprintf(f, " fv %016llx", (unsigned long long)fnv(FNV_INIT, fieldVars, 0x400 * sizeof(s16)));
    s32 actor = partyFieldActorIds[0];
    /* A field transition frees the actors' script entities before it clears
     * the actor array (transitionFields' capture waits on VSync in between):
     * only a live heap block is read (pcHeapAddress is 0 for a freed one). */
    if (D_800AFB10 != NULL && actor >= 0 && actor < g_totalActors && D_800AFB10[actor].m4C_scriptEntity != NULL &&
        pcHeapAddress(D_800AFB10[actor].m4C_scriptEntity) != 0) {
        const VECTOR* p = &D_800AFB10[actor].m4C_scriptEntity->m20_position;
        fprintf(f, " p %d %d %d %d", (int)actor, (int)p->vx, (int)p->vy, (int)p->vz);
    }
}

static void worldLine(FILE* f) {
    if (gWorldmapState == NULL) {
        return;
    }
    uint64_t h = FNV_INIT;
    for (int i = 0; i < 64; i++) {
        const sWorldmapStateEntry* e = &gWorldmapState->m0_entries[i];
        s32 v[5] = {e->m0_state, e->m20_subState, e->m28_position.vx, e->m28_position.vy, e->m28_position.vz};
        h = fnv(h, v, sizeof v);
    }
    const VECTOR* p = &gWorldmapState->m0_entries[0].m28_position;
    fprintf(f, " w %d %d %d %016llx", (int)p->vx, (int)p->vy, (int)p->vz, (unsigned long long)h);
}

void invariantVBlank(void) {
    FILE* f = file();
    if (f == NULL) {
        return;
    }
    int mode = pcWideGetMode();
    fprintf(f, "v %u m %d gs %016llx", (unsigned)hwVSyncCount(), mode,
            (unsigned long long)fnv(FNV_INIT, &gameState, sizeof gameState));
    if (mode == PC_WIDE_FIELD) {
        fieldLine(f);
    } else if (mode == PC_WIDE_WORLDMAP) {
        worldLine(f);
    }
    fputc('\n', f);
    fflush(f);
}

void invariantCardWrite(int slot, int sector, const uint8_t* frame) {
    FILE* f = file();
    if (f == NULL) {
        return;
    }
    fprintf(f, "c %u %d %d %016llx\n", (unsigned)hwVSyncCount(), slot, sector,
            (unsigned long long)fnv(FNV_INIT, frame, 128));
    fflush(f);
}
