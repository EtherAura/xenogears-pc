/*
 * The player's disc images and the game's file system on them (disc.h).
 */

#include "disc.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cd/cd_internal.h"
#include "common.h"
#include "main/filesystem.h"
#include "sandbox.h"

#define USER_OFFSET 24 /* Mode 2 Form 1: sync, header, subheader */
#define USER_SIZE 2048
#define TABLE_LBA 0x18
#define TABLE_SECTORS 16
#define DIRS_LBA 0x28
#define ENTRY_SIZE 7
#define MAX_ENTRIES (TABLE_SECTORS * USER_SIZE / ENTRY_SIZE)

struct XgDisc {
    char* path;
    CdDisc* image;
    uint8_t table[TABLE_SECTORS * USER_SIZE];
    uint16_t dirs[USER_SIZE / 2];
    int entries;
    char boot[32];
    const char* region;
};

uint64_t xgDiscFingerprint(const XgDisc* d) {
    uint64_t h = UINT64_C(14695981039346656037);
    int i;
    uint32_t sectors;
    if (d == NULL) return 0;
    for (i = 0; i < (int)sizeof(d->table); ++i) h = (h ^ d->table[i]) * UINT64_C(1099511628211);
    for (i = 0; i < (int)(sizeof(d->dirs) / sizeof(d->dirs[0])); ++i) {
        h = (h ^ (d->dirs[i] & 255)) * UINT64_C(1099511628211);
        h = (h ^ (d->dirs[i] >> 8)) * UINT64_C(1099511628211);
    }
    for (i = 0; d->boot[i]; ++i) h = (h ^ (uint8_t)d->boot[i]) * UINT64_C(1099511628211);
    sectors = (uint32_t)xgDiscSectors(d);
    for (i = 0; i < 4; ++i) h = (h ^ ((sectors >> (8 * i)) & 255)) * UINT64_C(1099511628211);
    return h;
}

/* The game's file system tables (src/main/filesystem.c). */
extern u8* fatFileTableBuffer;
extern u16* fatDirectoryTableBuffer;
extern sptr cdromFnd;

static int readUser(XgDisc* d, int lba, uint8_t* out, int sectors) {
    uint8_t raw[CD_RAW_SECTOR];
    for (int i = 0; i < sectors; i++) {
        if (!discRead(d->image, lba + i, raw)) {
            return 0;
        }
        memcpy(out + i * USER_SIZE, raw + USER_OFFSET, USER_SIZE);
    }
    return 1;
}

/* SYSTEM.CNF's boot file from the ISO 9660 root (primary volume descriptor
 * at sector 16; root directory record at +156). */
static void readBootName(XgDisc* d) {
    uint8_t pvd[USER_SIZE], dir[USER_SIZE];
    strcpy(d->boot, "?");
    d->region = "?";
    if (!readUser(d, 16, pvd, 1) || memcmp(pvd + 1, "CD001", 5) != 0) {
        return;
    }
    int rootLba = pvd[158] | pvd[159] << 8 | pvd[160] << 16 | pvd[161] << 24;
    if (!readUser(d, rootLba, dir, 1)) {
        return;
    }
    for (int pos = 0; pos < USER_SIZE && dir[pos] != 0; pos += dir[pos]) {
        int nameLen = dir[pos + 32];
        if (nameLen >= 10 && memcmp(dir + pos + 33, "SYSTEM.CNF", 10) == 0) {
            int lba = dir[pos + 2] | dir[pos + 3] << 8 | dir[pos + 4] << 16 | dir[pos + 5] << 24;
            uint8_t cnf[USER_SIZE + 1];
            if (!readUser(d, lba, cnf, 1)) {
                return;
            }
            cnf[USER_SIZE] = 0;
            const char* p = strstr((const char*)cnf, "cdrom:");
            if (p == NULL) {
                return;
            }
            p += 6;
            while (*p == '\\' || *p == '/') {
                p++;
            }
            size_t n = 0;
            while (p[n] != '\0' && p[n] != ';' && p[n] != '\r' && p[n] != '\n' && n + 1 < sizeof(d->boot)) {
                n++;
            }
            memcpy(d->boot, p, n);
            d->boot[n] = '\0';
            d->region = strncmp(d->boot, "SLUS", 4) == 0 || strncmp(d->boot, "SCUS", 4) == 0   ? "US"
                        : strncmp(d->boot, "SLPS", 4) == 0 || strncmp(d->boot, "SLPM", 4) == 0 ? "JP"
                        : strncmp(d->boot, "SLES", 4) == 0 || strncmp(d->boot, "SCES", 4) == 0 ? "EU"
                                                                                                : "?";
            return;
        }
    }
}

XgDisc* xgDiscOpen(const char* path, const char** why) {
    const char* dummy;
    why = why != NULL ? why : &dummy;
    XgDisc* d = calloc(1, sizeof(XgDisc));
    if (d == NULL) {
        *why = "out of memory";
        return NULL;
    }
    d->path = strdup(path);
    d->image = discOpen(path);
    if (d->image == NULL) {
        *why = "not a readable disc image (.cue or raw .bin)";
        xgDiscClose(d);
        return NULL;
    }
    if (!readUser(d, TABLE_LBA, d->table, TABLE_SECTORS) || !readUser(d, DIRS_LBA, (uint8_t*)d->dirs, 1)) {
        *why = "the image is too short";
        xgDiscClose(d);
        return NULL;
    }
    for (d->entries = 0; d->entries < MAX_ENTRIES; d->entries++) {
        const uint8_t* e = d->table + d->entries * ENTRY_SIZE;
        if ((e[0] | e[1] << 8 | e[2] << 16) == 0xFFFFFF) {
            break;
        }
    }
    uint8_t label[USER_SIZE];
    if (d->entries == MAX_ENTRIES || d->dirs[XG_DISC_DIRS] < 1 || d->dirs[XG_DISC_DIRS] > 2 ||
        !readUser(d, 23, label, 1) || memcmp(label + 4, "_XENOGEARS", 10) != 0) {
        *why = "not a Xenogears disc (no file table)";
        xgDiscClose(d);
        return NULL;
    }
    readBootName(d);
    return d;
}

void xgDiscClose(XgDisc* d) {
    if (d != NULL) {
        if (d->image != NULL) {
            discClose(d->image);
        }
        free(d->path);
        free(d);
    }
}

const char* xgDiscPath(const XgDisc* d) {
    return d->path;
}

int xgDiscNumber(const XgDisc* d) {
    return d->dirs[XG_DISC_DIRS];
}

const char* xgDiscRegion(const XgDisc* d) {
    return d->region;
}

const char* xgDiscBootName(const XgDisc* d) {
    return d->boot;
}

int xgDiscSectors(const XgDisc* d) {
    return discSectorCount(d->image);
}

int xgDiscEntryCount(const XgDisc* d) {
    return d->entries;
}

XgEntry xgDiscEntry(const XgDisc* d, int index) {
    XgEntry e = {0, 0};
    if (index >= 0 && index < d->entries) {
        const uint8_t* p = d->table + index * ENTRY_SIZE;
        e.lba = p[0] | p[1] << 8 | p[2] << 16;
        e.size = (int32_t)((uint32_t)p[3] | (uint32_t)p[4] << 8 | (uint32_t)p[5] << 16 | (uint32_t)p[6] << 24);
    }
    return e;
}

int xgDiscDirFirst(const XgDisc* d, int dir) {
    if (dir < 0 || dir >= XG_DISC_DIRS || d->dirs[dir] == 0 || d->dirs[dir] == 0xFFFF ||
        d->dirs[dir] - 1 >= d->entries) {
        return -1;
    }
    return d->dirs[dir] - 1;
}

int xgDiscDirFiles(const XgDisc* d, int dir) {
    int first = xgDiscDirFirst(d, dir);
    if (first < 0) {
        return 0;
    }
    int next = d->entries;
    for (int k = 0; k < XG_DISC_DIRS; k++) {
        int s = xgDiscDirFirst(d, k);
        if (s > first && s < next) {
            next = s;
        }
    }
    return next - first;
}

int xgDiscFileEntry(const XgDisc* d, int dir, int file) {
    int first = xgDiscDirFirst(d, dir);
    if (first < 0 || file < 1) {
        return -1;
    }
    int index = first + file - 1;
    return index < d->entries ? index : -1;
}

int xgDiscEntryDir(const XgDisc* d, int index, int* dir, int* file) {
    for (int k = 0; k < XG_DISC_DIRS; k++) {
        int first = xgDiscDirFirst(d, k);
        if (first >= 0 && index >= first && index < first + xgDiscDirFiles(d, k)) {
            *dir = k;
            *file = index - first + 1;
            return 1;
        }
    }
    return 0;
}

int xgDiscReadRaw(XgDisc* d, int lba, uint8_t* out) {
    return discRead(d->image, lba, out);
}

uint8_t* xgDiscReadSectors(XgDisc* d, int lba, int count) {
    if (count <= 0 || count > 0x10000) {
        return NULL;
    }
    uint8_t* out = malloc((size_t)count * USER_SIZE);
    if (out != NULL && !readUser(d, lba, out, count)) {
        free(out);
        out = NULL;
    }
    return out;
}

uint8_t* xgDiscReadEntry(XgDisc* d, int index, int* size) {
    XgEntry e = xgDiscEntry(d, index);
    int bytes = e.size;
    if (e.size < 0) {
        /* A group: from its LBA to the end of its last member. */
        bytes = 0;
        for (int k = 1; k <= -e.size && index + k < d->entries; k++) {
            XgEntry m = xgDiscEntry(d, index + k);
            if (m.size > 0) {
                int end = (m.lba - e.lba) * USER_SIZE + m.size;
                bytes = end > bytes ? end : bytes;
            }
        }
    }
    if (bytes <= 0 || bytes > 0x8000000) {
        return NULL;
    }
    *size = bytes;
    return xgDiscReadSectors(d, e.lba, (bytes + USER_SIZE - 1) / USER_SIZE);
}

static XgSandbox* lookups;

int xgDiscGameLookup(XgDisc* d, int dir, int file, int* lba, int* size, int* groupCount) {
    if (lookups == NULL && (lookups = xgSandboxCreate(NULL, 1u << 20)) == NULL) {
        return 0;
    }
    xgSandboxReset(lookups, 0);
    xgSandboxEnter(lookups);
    fatFileTableBuffer = xgSandboxCopy(d->table, sizeof(d->table));
    fatDirectoryTableBuffer = xgSandboxCopy(d->dirs, sizeof(d->dirs));
    cdromFnd = 0;
    int ok = fatFileTableBuffer != NULL && fatDirectoryTableBuffer != NULL && setCurrentDirectory(dir, 0) >= 0;
    if (ok) {
        *lba = getFileStartSector(file);
        *size = getFileSize(file);
        *groupCount = getNegativeFileSize(file);
    }
    xgSandboxLeave(lookups);
    return ok;
}

int xgDiscCheckGameLookups(XgDisc* d) {
    XgSandbox* sb = xgSandboxCreate(NULL, 1u << 20);
    if (sb == NULL) {
        return -1;
    }
    int differences = 0;
    xgSandboxEnter(sb);
    /* The tables as initCDAndFileSystem leaves them, without its CD reads. */
    fatFileTableBuffer = xgSandboxCopy(d->table, sizeof(d->table));
    fatDirectoryTableBuffer = xgSandboxCopy(d->dirs, sizeof(d->dirs));
    cdromFnd = 0;
    for (int dir = 0; fatFileTableBuffer != NULL && fatDirectoryTableBuffer != NULL && dir < XG_DISC_DIRS; dir++) {
        int files = xgDiscDirFiles(d, dir);
        if (files == 0 || setCurrentDirectory(dir, 0) != xgDiscDirFirst(d, dir)) {
            differences += files != 0;
            continue;
        }
        for (int f = 1; f <= files; f++) {
            XgEntry e = xgDiscEntry(d, xgDiscFileEntry(d, dir, f));
            differences += getFileStartSector(f) != e.lba || getFileSize(f) != e.size;
        }
    }
    xgSandboxLeave(sb);
    xgSandboxDestroy(sb);
    return differences;
}
