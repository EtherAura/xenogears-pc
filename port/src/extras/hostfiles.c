/*
 * The game's file reads inside a sandbox, served from a disc image
 * (hostfiles.h).
 */

#include "hostfiles.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common.h"
#include "system/system.h"

#define TABLE_LBA 0x18
#define TABLE_SECTORS 16
#define DIRS_LBA 0x28
#define SECTOR 2048
#define ENTRY_SIZE 7
#define MAX_ENTRIES (TABLE_SECTORS * SECTOR / ENTRY_SIZE)
#define NAME_SIZE 64 /* the game's stride through cdromFnd */
#define MAX_OPEN 32
#define CACHED 8 /* files kept read (the world map reads its terrain file chunk by chunk) */

/* The game's file system (src/main/filesystem.c). */
extern u8* fatFileTableBuffer;
extern u16* fatDirectoryTableBuffer;
extern sptr cdromFnd;
extern s32 startOfDirectoryFileIndex;
extern s32 readInProgress;
extern s32 remainingReadSize;
extern s32 cdCommandState;
extern s32 pcStreamFd;

typedef struct {
    int used;
    unsigned age; /* when it was opened (the oldest is reused when all are open) */
    int entry;
    const uint8_t* data; /* whole sectors, in the cache (NULL: an empty or unknown file, zeros) */
    int size;            /* the entry's size (a group: its members' extent) */
    int pos;
} Open;

typedef struct {
    int entry;      /* -1: free */
    uint8_t* data;
    int size;
    unsigned age;
} Cached;

struct XgHostFiles {
    XgDisc* disc;
    uint8_t* table;
    uint16_t* dirs;
    char* names;
    char* namesBlock;
    Open open[MAX_OPEN];
    Cached cache[CACHED];
    PcHostFiles calls;
    int opened, lastEntry;
    uint8_t visited[MAX_ENTRIES];
    unsigned clock;
};

static int32_t hostOpen(void* user, const char* name) {
    XgHostFiles* h = user;
    int entry = name != NULL ? atoi(name) : -1;
    /* A free slot, else the oldest open file's (a stream the game never
     * closed): an open must not fail, the game's error screen loops forever. */
    int fd = 0;
    for (int i = 0; i < MAX_OPEN; i++) {
        if (!h->open[i].used) {
            fd = i;
            break;
        }
        if (h->open[i].age < h->open[fd].age) {
            fd = i;
        }
    }
    Open* o = &h->open[fd];
    memset(o, 0, sizeof(*o));
    o->used = 1;
    o->age = ++h->clock;
    o->entry = entry;
    h->opened++;
    h->lastEntry = entry;
    if (entry >= 0 && entry < xgDiscEntryCount(h->disc)) {
        XgEntry e = xgDiscEntry(h->disc, entry);
        Cached* c = NULL;
        for (int i = 0; i < CACHED && e.size != 0; i++) {
            if (h->cache[i].entry == entry) {
                c = &h->cache[i];
            }
        }
        if (c == NULL && e.size != 0) {
            /* the least recently used slot */
            c = &h->cache[0];
            for (int i = 1; i < CACHED; i++) {
                if (h->cache[i].age < c->age) {
                    c = &h->cache[i];
                }
            }
            /* nothing open may point into it */
            for (int i = 0; i < MAX_OPEN; i++) {
                if (h->open[i].used && h->open[i].data == c->data && c->data != NULL) {
                    h->open[i].data = NULL;
                }
            }
            free(c->data);
            c->data = xgDiscReadEntry(h->disc, entry, &c->size);
            c->entry = c->data != NULL ? entry : -1;
        }
        if (c != NULL && c->entry == entry) {
            c->age = h->clock;
            o->data = c->data;
            o->size = e.size < 0 ? e.size : c->size; /* a group's size stays negative, as in the table */
        }
    }
    if (entry >= 0 && entry < MAX_ENTRIES && o->data != NULL) h->visited[entry] = 1;
    return fd;
}

static Open* lookup(XgHostFiles* h, int32_t fd) {
    return fd >= 0 && fd < MAX_OPEN && h->open[fd].used ? &h->open[fd] : NULL;
}

static int32_t hostSeek(void* user, int32_t fd, int32_t offset, int32_t mode) {
    Open* o = lookup(user, fd);
    if (o == NULL) {
        return -1;
    }
    int base = mode == 0 ? 0 : mode == 1 ? o->pos : o->size;
    o->pos = base + offset;
    return o->pos;
}

static int32_t hostRead(void* user, int32_t fd, void* buf, uint32_t len) {
    Open* o = lookup(user, fd);
    if (o == NULL || buf == NULL) {
        return -1;
    }
    /* Whole sectors are there; past them, zeros. Always the length asked
     * for: the game takes 0 for a failed read and retries into its error
     * screen. */
    int have = o->size < 0 ? 0 : ((o->size + SECTOR - 1) / SECTOR) * SECTOR;
    int from = o->pos;
    uint32_t n = 0;
    if (o->data != NULL && from >= 0 && from < have) {
        n = (uint32_t)(have - from) < len ? (uint32_t)(have - from) : len;
        memcpy(buf, o->data + from, n);
    }
    memset((uint8_t*)buf + n, 0, len - n);
    o->pos += (int)len;
    return len != 0 ? (int32_t)len : 1;
}

static int32_t hostClose(void* user, int32_t fd) {
    Open* o = lookup(user, fd);
    if (o == NULL) {
        return -1;
    }
    memset(o, 0, sizeof(*o));
    return 0;
}

XgHostFiles* xgHostFilesCreate(XgDisc* disc) {
    XgHostFiles* h = calloc(1, sizeof(XgHostFiles));
    if (h == NULL) {
        return NULL;
    }
    h->disc = disc;
    h->table = xgDiscReadSectors(disc, TABLE_LBA, TABLE_SECTORS);
    h->dirs = (uint16_t*)xgDiscReadSectors(disc, DIRS_LBA, 1);
    /* one slot more: the table may start a slot in, so that the low 32 bits
     * of its address (getCdromFnd returns them as an s32) are never 0 or -1 */
    h->namesBlock = calloc(MAX_ENTRIES + 2, NAME_SIZE);
    h->names = h->namesBlock;
    if (h->names != NULL && ((uint32_t)(uintptr_t)h->names == 0 || (uint32_t)(uintptr_t)h->names == 0xFFFFFFFFu)) {
        h->names += NAME_SIZE;
    }
    if (h->table == NULL || h->dirs == NULL || h->names == NULL) {
        xgHostFilesDestroy(h);
        return NULL;
    }
    for (int i = 0; i <= MAX_ENTRIES; i++) {
        snprintf(h->names + (size_t)i * NAME_SIZE, NAME_SIZE, "%d", i);
    }
    h->calls.user = h;
    h->calls.open = hostOpen;
    h->calls.seek = hostSeek;
    h->calls.read = hostRead;
    h->calls.close = hostClose;
    h->lastEntry = -1;
    for (int i = 0; i < CACHED; i++) {
        h->cache[i].entry = -1;
    }
    return h;
}

void xgHostFilesDestroy(XgHostFiles* h) {
    if (h == NULL) {
        return;
    }
    for (int i = 0; i < CACHED; i++) {
        free(h->cache[i].data);
    }
    free(h->table);
    free(h->dirs);
    free(h->namesBlock);
    free(h);
}

void xgHostFilesAttach(XgHostFiles* h) {
    /* initCDAndFileSystem's host mode (its `mode` is the name table, an s32
     * on the PS1: too narrow for a PC pointer, so set here). */
    cdromFnd = (sptr)h->names;
    fatFileTableBuffer = h->table;
    fatDirectoryTableBuffer = h->dirs;
    startOfDirectoryFileIndex = 0;
    readInProgress = 0;
    remainingReadSize = 0;
    cdCommandState = 0;
    pcStreamFd = -1;
    biosSetHostFiles(&h->calls);
}

void xgHostFilesCloseAll(XgHostFiles* h) {
    for (int fd = 0; fd < MAX_OPEN; fd++) {
        memset(&h->open[fd], 0, sizeof(h->open[fd]));
    }
}

void xgHostFilesDetach(XgHostFiles* h) {
    (void)h;
    biosSetHostFiles(NULL);
}

int xgHostFilesOpened(const XgHostFiles* h, int* lastEntry) {
    if (lastEntry != NULL) {
        *lastEntry = h->lastEntry;
    }
    return h->opened;
}

int xgHostFilesEntries(const XgHostFiles* h, int* entries, int capacity) {
    int count = 0;
    if (!h) return 0;
    for (int i = 0; i < MAX_ENTRIES; ++i) if (h->visited[i]) {
        if (entries && count < capacity) entries[count] = i;
        ++count;
    }
    return count;
}
