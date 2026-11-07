/*
 * A world of its own for the platform's interrupt side and the SPU (world.h).
 * Each file keeps its part of a world (system.h: hw.c, libapi.c, spu.c, and
 * the SPU core's CD input); this file holds the parts and the SPU's state and
 * swaps them all.
 */

#include "world.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "spu/spu_internal.h"
#include "system.h"

extern SpuRegs portSpuRegisters; /* spu.c */

struct HwWorld {
    void* hw;
    void* api;
    void* libspu;
    void* cdin;
    SpuState* spu; /* the core's state (RAM, voices, reverb) */
    SpuRegs regs;  /* the register block the driver writes */
};

static HwWorld* current;

/* Exchange n bytes, a chunk at a time. */
static void swapBytes(void* a, void* b, size_t n) {
    unsigned char tmp[4096];
    unsigned char* x = a;
    unsigned char* y = b;
    while (n > 0) {
        size_t k = n < sizeof(tmp) ? n : sizeof(tmp);
        memcpy(tmp, x, k);
        memcpy(x, y, k);
        memcpy(y, tmp, k);
        x += k;
        y += k;
        n -= k;
    }
}

static void swapAll(HwWorld* w) {
    hwWorldPartSwap(w->hw);
    apiWorldPartSwap(w->api);
    spuWorldPartSwap(w->libspu);
    spuCdStateSwap(w->cdin);
    swapBytes(&spu, w->spu, sizeof(spu));
    swapBytes(&portSpuRegisters, &w->regs, sizeof(portSpuRegisters));
}

HwWorld* hwWorldCreate(void) {
    HwWorld* w = calloc(1, sizeof(HwWorld));
    if (w == NULL) {
        return NULL;
    }
    w->hw = malloc(hwWorldPartSize());
    w->api = malloc(apiWorldPartSize());
    w->libspu = malloc(spuWorldPartSize());
    w->cdin = calloc(1, spuCdStateSize()); /* zero: an empty input */
    w->spu = calloc(1, sizeof(SpuState));
    if (w->hw == NULL || w->api == NULL || w->libspu == NULL || w->cdin == NULL || w->spu == NULL) {
        hwWorldDestroy(w);
        return NULL;
    }
    hwWorldPartInit(w->hw);
    apiWorldPartInit(w->api);
    spuWorldPartInit(w->libspu);
    if (current != NULL) {
        fprintf(stderr, "world: created inside another (not set up)\n");
        return w;
    }
    hwWorldEnter(w);
    spuWorldPowerOn();
    hwWorldLeave(w);
    return w;
}

void hwWorldDestroy(HwWorld* w) {
    if (w == NULL) {
        return;
    }
    if (current == w) {
        hwWorldLeave(w);
    }
    if (w->hw != NULL) {
        hwWorldPartFree(w->hw);
    }
    free(w->hw);
    free(w->api);
    free(w->libspu);
    free(w->cdin);
    free(w->spu);
    free(w);
}

void hwWorldEnter(HwWorld* w) {
    if (current != NULL) {
        fprintf(stderr, "world: entered while inside one (ignored)\n");
        return;
    }
    swapAll(w);
    current = w;
}

void hwWorldLeave(HwWorld* w) {
    if (current != w) {
        fprintf(stderr, "world: left without being entered (ignored)\n");
        return;
    }
    current = NULL;
    swapAll(w);
}

HwWorld* hwWorldCurrent(void) {
    return current;
}

void hwWorldSetSink(HwWorldSink sink, void* user) {
    if (current != NULL) {
        spuWorldSetSink(sink, user);
    }
}

void hwWorldSetSpu(int on) {
    if (current != NULL) {
        spuWorldSetRun(on);
    }
}

double hwWorldTime(void) {
    return current != NULL ? hwNow() : 0;
}
