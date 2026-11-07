/*
 * Memory the game manages itself: the heap arena, and the overlay data that a
 * mode switch resets.
 *
 * The PS1 heap is the RAM between the end of main's BSS (where the game-mode
 * overlays load) and 0x801FC000, about 1.55 MB. Structs with pointers or
 * ordering-table tags are larger on the PC, so the PC arena is bigger.
 *
 * The PS1 reloads an overlay's image from disc and clears its BSS each time it
 * is loaded, so its initialised data starts from the same values every time.
 * On the PC every overlay is linked in once: each overlay's data and BSS sit in
 * their own sections (tools/pc_link.py, port/CMakeLists.txt), a copy of the
 * data is kept from startup, and portResetModule puts both back.
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <sys/mman.h>
#endif

#include "savestate.h"
#include "system.h"

#define PORT_HEAP_SIZE (8 * 1024 * 1024)

/* Page-aligned: BattleBlinkTask.m20_phase (src/battle/b_800B7C28.c,
 * updateCharacterBlinkColor) is never initialised by retail and holds a leftover
 * host heap pointer whose low bits become the blink phase; with the arena on a
 * page boundary heap pointers keep the same low 12 bits on every build (Linux,
 * Windows, any link layout), so that phase is the same on all of them. */
static _Alignas(4096) unsigned char heapArena[PORT_HEAP_SIZE];

unsigned char* portHeapStart(void) {
    return heapArena;
}

unsigned char* portHeapEnd(void) {
    return heapArena + PORT_HEAP_SIZE;
}

/* The PS1's 1 KB scratchpad. The game reaches it by its raw address, so the PC
 * maps a page there; the executable is not position-independent, so nothing
 * else lives that low. (Windows: VirtualAlloc at the same address.) */
#define SCRATCHPAD_ADDR 0x1F800000UL
#define SCRATCHPAD_MAP 0x1000

int portScratchpadInit(void) {
    void* want = (void*)(size_t)SCRATCHPAD_ADDR;
#ifdef _WIN32
    /* Reserves the 64 KB allocation granule at 0x1F800000, commits one page. */
    void* p = VirtualAlloc(want, SCRATCHPAD_MAP, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if (p != want) {
        fprintf(stderr, "scratchpad: VirtualAlloc at 0x1F800000 failed (error %lu)\n", GetLastError());
        return 0;
    }
#else
    void* p = mmap(want, SCRATCHPAD_MAP, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE,
                   -1, 0);
    if (p != want) {
        int err = errno;
        if (p != MAP_FAILED) munmap(p, SCRATCHPAD_MAP); /* a kernel that took the address as a hint */
        fprintf(stderr, "scratchpad: mmap at 0x1F800000 gave %p (errno %d, %s)\n", p, err, strerror(err));
        /* What is in the way: the mappings of this process that overlap the range. */
        FILE* maps = fopen("/proc/self/maps", "r");
        if (maps) {
            char line[512];
            while (fgets(line, sizeof(line), maps)) {
                unsigned long lo = 0, hi = 0;
                if (sscanf(line, "%lx-%lx", &lo, &hi) == 2 && lo < SCRATCHPAD_ADDR + SCRATCHPAD_MAP && hi > SCRATCHPAD_ADDR)
                    fprintf(stderr, "scratchpad: in the way: %s", line);
            }
            fclose(maps);
        }
        return 0;
    }
#endif
    return 1;
}

/* build/pc/cmake/gen/modules.c */
typedef struct {
    const char* name;
    char* dataStart;
    char* dataEnd;
    char* bssStart;
    char* bssEnd;
} PortModule;

extern PortModule portModules[];
extern const int portModuleCount;

static unsigned char** pristineData;

void portModulesInit(void) {
    pristineData = calloc((size_t)portModuleCount, sizeof(*pristineData));
    for (int i = 0; i < portModuleCount; i++) {
        PortModule* m = &portModules[i];
        if (m->dataStart != NULL && m->dataEnd > m->dataStart) {
            size_t size = (size_t)(m->dataEnd - m->dataStart);
            pristineData[i] = malloc(size);
            memcpy(pristineData[i], m->dataStart, size);
        }
    }
}

/* Host memory for the 60 Hz Battling's sandbox copies (between.c; game-side
 * code has no libc). */
void* portSandboxAlloc(size_t n) {
    return malloc(n);
}

/* A module's data and BSS ranges (the 60 Hz Battling's memory sandbox,
 * port/src/game/battling/between.c). 0 when there is no such module. */
int portModuleRange(const char* name, void** dataStart, void** dataEnd, void** bssStart, void** bssEnd) {
    for (int i = 0; i < portModuleCount; i++) {
        PortModule* m = &portModules[i];
        if (strcmp(m->name, name) != 0) {
            continue;
        }
        *dataStart = m->dataStart;
        *dataEnd = m->dataEnd;
        *bssStart = m->bssStart;
        *bssEnd = m->bssEnd;
        return 1;
    }
    return 0;
}

/* Where the PS1 loads an overlay: its data and BSS as a fresh load leaves them. */
void portResetModule(const char* name) {
    for (int i = 0; i < portModuleCount; i++) {
        PortModule* m = &portModules[i];
        if (strcmp(m->name, name) != 0) {
            continue;
        }
        if (pristineData[i] != NULL) {
            memcpy(m->dataStart, pristineData[i], (size_t)(m->dataEnd - m->dataStart));
        }
        if (m->bssStart != NULL && m->bssEnd > m->bssStart) {
            memset(m->bssStart, 0, (size_t)(m->bssEnd - m->bssStart));
        }
        return;
    }
    fprintf(stderr, "portResetModule: no module %s\n", name);
}

/* A module's BSS cleared, its data kept (main's at the soft reset). */
void portClearModuleBss(const char* name) {
    for (int i = 0; i < portModuleCount; i++) {
        PortModule* m = &portModules[i];
        if (strcmp(m->name, name) == 0) {
            if (m->bssStart != NULL && m->bssEnd > m->bssStart) {
                memset(m->bssStart, 0, (size_t)(m->bssEnd - m->bssStart));
            }
            return;
        }
    }
    fprintf(stderr, "portClearModuleBss: no module %s\n", name);
}

/* The overlay each game mode loads (kernel.c's gameModes; modes 0 and 5 are in main). */
static const char* const gameModeOverlays[7] = {
    NULL, "field", "battle", "worldmap", "battling", NULL, "movie",
};

/* bootGame calls this for every mode right before the mode's entry point (the
 * PS1's `jalr` there is where Beetle and Redux logs mark a mode start). */
static int gameModeNow = -1; /* the mode bootGame started last (a save state's label) */

void portResetGameMode(int mode) {
    gameModeNow = mode;
    slowdownModeEntry(mode);
    if (mode >= 0 && mode < 7 && gameModeOverlays[mode] != NULL) {
        portResetModule(gameModeOverlays[mode]);
    }
}

/* A copy of a module's data and BSS, and putting it back: the PS1 copies an
 * overlay's whole image aside and back, as the field does with field_mecha
 * around the gear shop, which loads its own fresh copy. One copy at a time,
 * in a static buffer: the game keeps the pointer in a local across the
 * whole shop, so a save state taken there holds it (savestate.h). A module
 * too large for it gets a host copy, and save states wait until it is back. */
#define SAVED_MODULE_MAX (64 * 1024)
static _Alignas(16) unsigned char savedModule[SAVED_MODULE_MAX];
static int savedModuleOnHeap;

void* portSaveModule(const char* name) {
    for (int i = 0; i < portModuleCount; i++) {
        PortModule* m = &portModules[i];
        if (strcmp(m->name, name) != 0) {
            continue;
        }
        size_t data = m->dataEnd > m->dataStart ? (size_t)(m->dataEnd - m->dataStart) : 0;
        size_t bss = m->bssEnd > m->bssStart ? (size_t)(m->bssEnd - m->bssStart) : 0;
        unsigned char* copy = savedModule;
        if (data + bss > sizeof(savedModule)) {
            copy = malloc(data + bss + 1);
            savedModuleOnHeap = 1;
        }
        if (data > 0) {
            memcpy(copy, m->dataStart, data);
        }
        if (bss > 0) {
            memcpy(copy + data, m->bssStart, bss);
        }
        return copy;
    }
    fprintf(stderr, "portSaveModule: no module %s\n", name);
    return NULL;
}

void portRestoreModule(const char* name, void* copy) {
    for (int i = 0; copy != NULL && i < portModuleCount; i++) {
        PortModule* m = &portModules[i];
        if (strcmp(m->name, name) != 0) {
            continue;
        }
        size_t data = m->dataEnd > m->dataStart ? (size_t)(m->dataEnd - m->dataStart) : 0;
        size_t bss = m->bssEnd > m->bssStart ? (size_t)(m->bssEnd - m->bssStart) : 0;
        if (data > 0) {
            memcpy(m->dataStart, copy, data);
        }
        if (bss > 0) {
            memcpy(m->bssStart, (unsigned char*)copy + data, bss);
        }
        break;
    }
    if (copy != savedModule) {
        free(copy);
        savedModuleOnHeap = 0;
    }
}

/* ---- save states (savestate.h) ---- */

int portSavedModuleOnHeap(void) {
    return savedModuleOnHeap;
}

int portGameMode(void) {
    return gameModeNow;
}

/* The module copy the field may be holding (the heap arena and the modules'
 * sections are savestate.c's regions). */
void memoryStateIo(XgState* s) {
    XG_STATE_VAR(s, savedModule);
    XG_STATE_VAR(s, gameModeNow);
}
