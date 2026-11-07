/*
 * Game code run outside the game (sandbox.h): a world of its own for the
 * modules' data and BSS, the heap, the scratchpad, the GTE and the GPU,
 * swapped with the game's around each use.
 */

#include "sandbox.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common.h"
#include "gte_pc.h"
#include "main/memory.h"
#include "pc_wide.h"
#include "system/hw.h"

/* build/.../gen/modules.c (port/src/system/memory.c has the same view). */
typedef struct {
    const char* name;
    char* dataStart;
    char* dataEnd;
    char* bssStart;
    char* bssEnd;
} PortModule;

extern PortModule portModules[];
extern const int portModuleCount;

/* port/src/system/libc.c: the game's rand() seed lives in the platform layer,
 * not in a module's data, so the sandbox swaps it by hand (the sprite VM's
 * random ops would otherwise move the paused game's sequence). */
u32 portRandSeed(void);
void srand(u32 newSeed);

extern unsigned char pcScratchpad[]; /* system/memory.c: the PS1 scratchpad (include/common.h PSX_SCRATCH) */
#define SCRATCHPAD pcScratchpad
#define SCRATCHPAD_SIZE 0x400
#define DEFAULT_HEAP (16u << 20)

typedef struct {
    char* start;
    size_t size;
    const unsigned char* pristine; /* NULL: BSS (zero) */
    unsigned char* mine;           /* the sandbox's copy while outside */
    unsigned char* theirs;         /* the game's while inside */
} Region;

struct XgSandbox {
    Region* regions;
    int regionCount;
    unsigned char* arena;
    size_t arenaSize;
    unsigned char scratch[SCRATCHPAD_SIZE], scratchGame[SCRATCHPAD_SIZE];
    GteState gte, gteGame;
    int wideGame[3];
    u32 seed, seedGame; /* rand()'s seed: the sandbox's, the game's while inside */
    int heapReady; /* initHeap has run in this world */
    XgVgpu* gpu;
};

static unsigned char** pristine; /* each module's data as loaded */
static GteState gteAtStart;
static XgSandbox* current;

/* ---- the modules as loaded ----------------------------------------------------------- */

void xgSandboxInit(void) {
    if (pristine != NULL) {
        return;
    }
    pristine = calloc((size_t)portModuleCount, sizeof(*pristine));
    for (int i = 0; pristine != NULL && i < portModuleCount; i++) {
        PortModule* m = &portModules[i];
        if (m->dataStart != NULL && m->dataEnd > m->dataStart) {
            size_t size = (size_t)(m->dataEnd - m->dataStart);
            pristine[i] = malloc(size);
            if (pristine[i] != NULL) {
                memcpy(pristine[i], m->dataStart, size);
            }
        }
    }
    gteSave(&gteAtStart);
}

static int moduleIndex(const char* name) {
    for (int i = 0; i < portModuleCount; i++) {
        if (strcmp(portModules[i].name, name) == 0) {
            return i;
        }
    }
    return -1;
}

const void* xgModulePristineData(const char* name, size_t* size) {
    int i = moduleIndex(name);
    if (i < 0 || pristine == NULL || pristine[i] == NULL) {
        return NULL;
    }
    *size = (size_t)(portModules[i].dataEnd - portModules[i].dataStart);
    return pristine[i];
}

/* ---- a sandbox ------------------------------------------------------------------------- */

static int addRegion(XgSandbox* sb, char* start, char* end, const unsigned char* data) {
    if (start == NULL || end <= start) {
        return 1;
    }
    Region* r = &sb->regions[sb->regionCount];
    r->start = start;
    r->size = (size_t)(end - start);
    r->pristine = data;
    r->mine = malloc(r->size);
    r->theirs = malloc(r->size);
    if (r->mine == NULL || r->theirs == NULL) {
        free(r->mine);
        free(r->theirs);
        return 0;
    }
    sb->regionCount++;
    return 1;
}

static int addModule(XgSandbox* sb, const char* name) {
    int i = moduleIndex(name);
    if (i < 0) {
        fprintf(stderr, "extras: sandbox: no module %s\n", name);
        return 1;
    }
    for (int k = 0; k < sb->regionCount; k++) {
        if (sb->regions[k].start == portModules[i].dataStart || sb->regions[k].start == portModules[i].bssStart) {
            return 1; /* named twice */
        }
    }
    PortModule* m = &portModules[i];
    return addRegion(sb, m->dataStart, m->dataEnd, pristine != NULL ? pristine[i] : NULL) &&
           addRegion(sb, m->bssStart, m->bssEnd, NULL);
}

/* The world as the modules were loaded: their data, zero BSS, an empty
 * scratchpad, the GTE as at startup, no heap yet. */
static void freshWorld(XgSandbox* sb) {
    for (int k = 0; k < sb->regionCount; k++) {
        Region* r = &sb->regions[k];
        if (r->pristine != NULL) {
            memcpy(r->mine, r->pristine, r->size);
        } else {
            memset(r->mine, 0, r->size);
        }
    }
    memset(sb->scratch, 0, sizeof(sb->scratch));
    sb->gte = gteAtStart;
    sb->seed = 0; /* as at power-on: the game never reseeds */
    sb->heapReady = 0;
}

XgSandbox* xgSandboxCreate(const char* const* modules, size_t heapBytes) {
    xgSandboxInit();
    XgSandbox* sb = calloc(1, sizeof(XgSandbox));
    if (sb == NULL) {
        return NULL;
    }
    int count = 1;
    for (int i = 0; modules != NULL && modules[i] != NULL; i++) {
        count++;
    }
    sb->regions = calloc((size_t)count * 2, sizeof(Region));
    sb->arenaSize = heapBytes != 0 ? (heapBytes + 15) & ~(size_t)15 : DEFAULT_HEAP;
    sb->arena = malloc(sb->arenaSize);
    sb->gpu = xgVgpuCreate();
    int ok = sb->regions != NULL && sb->arena != NULL && sb->gpu != NULL && addModule(sb, "main");
    for (int i = 0; ok && modules != NULL && modules[i] != NULL; i++) {
        ok = addModule(sb, modules[i]);
    }
    if (!ok) {
        xgSandboxDestroy(sb);
        return NULL;
    }
    freshWorld(sb);
    return sb;
}

void xgSandboxDestroy(XgSandbox* sb) {
    if (sb == NULL) {
        return;
    }
    if (current == sb) {
        xgSandboxLeave(sb);
    }
    for (int k = 0; k < sb->regionCount; k++) {
        free(sb->regions[k].mine);
        free(sb->regions[k].theirs);
    }
    free(sb->regions);
    free(sb->arena);
    xgVgpuDestroy(sb->gpu);
    free(sb);
}

void xgSandboxEnter(XgSandbox* sb) {
    if (current != NULL) {
        fprintf(stderr, "extras: sandbox entered while inside one (ignored)\n");
        return;
    }
    hwMaskInterrupts();
    for (int k = 0; k < sb->regionCount; k++) {
        Region* r = &sb->regions[k];
        memcpy(r->theirs, r->start, r->size);
        memcpy(r->start, r->mine, r->size);
    }
    memcpy(sb->scratchGame, SCRATCHPAD, SCRATCHPAD_SIZE);
    memcpy(SCRATCHPAD, sb->scratch, SCRATCHPAD_SIZE);
    gteSave(&sb->gteGame);
    gteLoad(&sb->gte);
    sb->seedGame = portRandSeed();
    srand(sb->seed);
    sb->wideGame[0] = pcWideView;
    sb->wideGame[1] = pcWideCull;
    sb->wideGame[2] = pcWideOnlyScope;
    pcWideView = pcWideCull = pcWideOnlyScope = 0;
    gpuSetCapture(xgVgpuCapture(sb->gpu));
    current = sb;
    if (!sb->heapReady) {
        /* The game's heap, on the sandbox's arena; a full heap returns NULL
         * rather than the game's fatal error screen (bootGame). */
        sb->heapReady = 1;
        initHeap((HeapBlock*)sb->arena, (HeapBlock*)(sb->arena + sb->arenaSize));
        setHeapAllowFailure(1);
    }
}

void xgSandboxLeave(XgSandbox* sb) {
    if (current != sb) {
        fprintf(stderr, "extras: sandbox left without being entered (ignored)\n");
        return;
    }
    current = NULL;
    gpuSetCapture(NULL);
    pcWideView = sb->wideGame[0];
    pcWideCull = sb->wideGame[1];
    pcWideOnlyScope = sb->wideGame[2];
    sb->seed = portRandSeed();
    srand(sb->seedGame);
    gteSave(&sb->gte);
    gteLoad(&sb->gteGame);
    memcpy(sb->scratch, SCRATCHPAD, SCRATCHPAD_SIZE);
    memcpy(SCRATCHPAD, sb->scratchGame, SCRATCHPAD_SIZE);
    for (int k = 0; k < sb->regionCount; k++) {
        Region* r = &sb->regions[k];
        memcpy(r->mine, r->start, r->size);
        memcpy(r->start, r->theirs, r->size);
    }
    hwUnmaskInterrupts();
}

XgSandbox* xgSandboxCurrent(void) {
    return current;
}

void xgSandboxReset(XgSandbox* sb, int clearVram) {
    int inside = current == sb;
    if (inside) {
        xgSandboxLeave(sb);
    }
    freshWorld(sb);
    xgVgpuClearPrims(sb->gpu);
    xgVgpuSetArea(sb->gpu, 0, 0, XG_VRAM_W - 1, XG_VRAM_H - 1, 0, 0);
    if (clearVram) {
        xgVgpuClearVram(sb->gpu);
    }
    if (inside) {
        xgSandboxEnter(sb);
    }
}

void xgSandboxZeroHeap(XgSandbox* sb) {
    if (current == NULL && !sb->heapReady) memset(sb->arena, 0, sb->arenaSize);
}

XgVgpu* xgSandboxGpu(XgSandbox* sb) {
    return sb->gpu;
}

void* xgSandboxAlloc(size_t size) {
    if (current == NULL || size > 0x7FFFFFF0u) {
        return NULL;
    }
    void* p = allocateHeap((s32)size, 0);
    if (p != NULL) {
        memset(p, 0, size);
    }
    return p;
}

void* xgSandboxCopy(const void* data, size_t size) {
    unsigned char* p = xgSandboxAlloc(size + 16);
    if (p != NULL) {
        memcpy(p, data, size);
    }
    return p;
}

int xgSandboxOwns(const XgSandbox* sb, const void* p) {
    const unsigned char* c = p;
    return c >= sb->arena && c < sb->arena + sb->arenaSize;
}
