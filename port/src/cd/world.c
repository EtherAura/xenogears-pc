/*
 * A world of its own for the drive (cd.h cdWorldCreate): a drive with a disc
 * of its own, LIBCD's state, the streaming ring and the XA decoder, each
 * file's part (cd_internal.h) kept here and swapped with the file's state.
 */

#include "cd/cd.h"
#include "cd_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct CdWorld {
    void* drive;
    void* libcd;
    void* stream;
    void* xa;
    void* regional;
};

void cdSwapBytes(void* a, void* b, size_t n) {
    unsigned char tmp[512];
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

CdWorld* cdWorldCreate(const char* discPath) {
    CdDisc* d = discOpen(discPath);
    if (d == NULL) {
        return NULL;
    }
    CdWorld* w = calloc(1, sizeof(CdWorld));
    if (w != NULL) {
        w->drive = malloc(drivePartSize());
        w->libcd = malloc(libcdPartSize());
        w->stream = malloc(streamPartSize());
        w->xa = malloc(xaPartSize());
        w->regional = malloc(regionalPartSize());
    }
    if (w == NULL || w->drive == NULL || w->libcd == NULL || w->stream == NULL || w->xa == NULL || w->regional == NULL) {
        if (w != NULL) {
            free(w->drive);
            free(w->libcd);
            free(w->stream);
            free(w->xa);
            free(w->regional);
            free(w);
        }
        discClose(d);
        return NULL;
    }
    drivePartInit(w->drive, d);
    libcdPartInit(w->libcd);
    streamPartInit(w->stream);
    xaPartInit(w->xa);
    regionalPartInit(w->regional);
    return w;
}

void cdWorldDestroy(CdWorld* w) {
    if (w == NULL) {
        return;
    }
    drivePartFree(w->drive);
    free(w->drive);
    free(w->libcd);
    free(w->stream);
    free(w->xa);
    free(w->regional);
    free(w);
}

void cdWorldSwap(CdWorld* w) {
    drivePartSwap(w->drive);
    libcdPartSwap(w->libcd);
    streamPartSwap(w->stream);
    xaPartSwap(w->xa);
    regionalPartSwap(w->regional);
}
