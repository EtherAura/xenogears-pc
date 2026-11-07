/*
 * The game modules' initialised data from the player's disc (gamedata.h).
 *
 * tools/pc_link.py declares every data object the game C does not define
 * (the PS1 build's data assembly) with its PC size and layout, as zero
 * storage in the module's data section (xgdata_<module>), and writes per
 * module the copies that fill it: (object + offset, offset in the module's
 * file, size), plus where the file is on the disc, its size as the game
 * loads it and a hash of the bytes copied (build/.../gen/modules.c,
 * portModuleData). Pointers and the PC's widened fields are initialisers of
 * their own; only the bytes the PS1 and the PC lay out alike are copied.
 *
 * Here, at every launch and before anything reads the data (main.c: before
 * portModulesInit keeps the overlays' pristine copies and the extras keep
 * theirs), each module's file is read from the image through the image
 * reader (cd/disc.c, not the drive): the boot executable from the ISO root
 * (SYSTEM.CNF names it), the overlays from the game's own file table (16
 * sectors at LBA 0x18; directory table at LBA 0x28: file F of directory D is
 * entry dirs[D] - 1 + F - 1), LZSS-decompressed where the game decompresses
 * them. The game C's own initialised bytes that are the retail module's
 * (tables and strings as C initialisers) are zeroed in the module's object
 * when it is linked (tools/pc_srcdata.py) and put back from the same file
 * (portSrcData, with a hash of its own). The image must be the build's
 * release: the region (SYSTEM.CNF), every file's size and the hashes must
 * match, or nothing is written. Main's
 * embedded file and directory tables are each disc's own (cd/boot.c): they
 * are copied from the disc read and left out of the hash, so either disc of
 * the release can start the game.
 */

#include "gamedata.h"

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cd/cd_internal.h"
#include "cd/verify.h"

/* build/.../gen/modules.c (tools/pc_link.py): the same structs there. */
typedef struct {
    void* dst;
    unsigned int src;
    unsigned int size; /* | DISC_SPECIFIC */
} XgDataCopy;

typedef struct {
    const char* name;
    int dir, file, lzss; /* dir -1: the boot executable */
    unsigned int size;   /* the file's size as loaded */
    unsigned long long hash;
    const XgDataCopy* copies;
    int count;
} PortModuleData;

extern const PortModuleData portModuleData[];
extern const int portModuleCount;

/* build/.../gen/modules.c and <module>.src.c (tools/pc_srcdata.py): module i's game C bytes. */
typedef struct {
    const XgDataCopy* copies;
    const int* count;
    const unsigned long long* hash;
} PortSrcData;

extern const PortSrcData portSrcData[];
extern const char portDataRegion[];

#define DISC_SPECIFIC 0x80000000u
#define USER_OFFSET 24 /* Mode 2 Form 1: sync, header, subheader */
#define USER_SIZE 2048
#define TABLE_LBA 0x18
#define TABLE_SIZE 0x8000
#define DIRS_LBA 0x28
#define DIR_COUNT 0x3C

const char* portDataRegionName(void) {
    return portDataRegion;
}

static int fail(char* why, size_t size, const char* fmt, ...) __attribute__((format(printf, 3, 4)));
static int fail(char* why, size_t size, const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    if (size > 0) {
        vsnprintf(why, size, fmt, ap);
    }
    va_end(ap);
    return 0;
}

static uint32_t u32(const uint8_t* p) {
    return p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

/* `bytes` from LBA `lba` on, whole sectors (an LZSS stream may run into the
 * last one's padding); malloc'd, NULL on a read error. */
static uint8_t* readUser(CdDisc* d, int lba, uint32_t bytes, uint32_t* got) {
    uint32_t sectors = (bytes + USER_SIZE - 1) / USER_SIZE;
    uint8_t raw[CD_RAW_SECTOR];
    uint8_t* out = malloc((size_t)sectors * USER_SIZE + 1);
    if (out == NULL || lba < 0 || (uint64_t)lba + sectors > (uint64_t)discSectorCount(d)) {
        free(out);
        return NULL;
    }
    for (uint32_t i = 0; i < sectors; i++) {
        if (!discRead(d, lba + (int)i, raw) || raw[15] != 2) {
            free(out);
            return NULL;
        }
        memcpy(out + (size_t)i * USER_SIZE, raw + USER_OFFSET, USER_SIZE);
    }
    *got = sectors * USER_SIZE;
    return out;
}

/* A file of the ISO root directory (the boot executable). */
static int isoFind(CdDisc* d, const char* name, int* lba, uint32_t* size) {
    uint32_t n;
    uint8_t* pvd = readUser(d, 16, USER_SIZE, &n);
    if (pvd == NULL || pvd[0] != 1 || memcmp(pvd + 1, "CD001", 5) != 0) {
        free(pvd);
        return 0;
    }
    uint32_t dirLba = u32(pvd + 158), dirBytes = u32(pvd + 166);
    free(pvd);
    if (dirBytes == 0 || dirBytes > 128 * 1024) {
        return 0;
    }
    uint8_t* dir = readUser(d, (int)dirLba, dirBytes, &n);
    if (dir == NULL) {
        return 0;
    }
    size_t len = strlen(name);
    int found = 0;
    for (uint32_t at = 0; at < dirBytes && !found;) {
        uint32_t rec = dir[at];
        if (rec == 0) { /* records never straddle sectors */
            at = (at / USER_SIZE + 1) * USER_SIZE;
            continue;
        }
        if (rec < 34 || at + rec > dirBytes || 33u + dir[at + 32] > rec) {
            break;
        }
        uint32_t nameLen = dir[at + 32];
        const char* recName = (const char*)dir + at + 33;
        if (nameLen >= len && memcmp(recName, name, len) == 0 && (nameLen == len || recName[len] == ';')) {
            *lba = (int)u32(dir + at + 2);
            *size = u32(dir + at + 10);
            found = 1;
        }
        at += rec;
    }
    free(dir);
    return found;
}

/* The game's LZSS: u32 output size, then groups of 8 items after a flag byte
 * (bit 0 first): 0 a literal byte, 1 a u16 reference (distance: low 12 bits,
 * length: high 4 bits + 3). NULL if the stream is not one. */
static uint8_t* lzss(const uint8_t* in, uint32_t inSize, uint32_t* outSize) {
    if (inSize < 4) {
        return NULL;
    }
    uint32_t size = u32(in), pos = 4, done = 0;
    if (size == 0 || size > 16 * 1024 * 1024) {
        return NULL;
    }
    uint8_t* out = malloc(size);
    if (out == NULL) {
        return NULL;
    }
    while (done < size && pos < inSize) {
        uint8_t flags = in[pos++];
        for (int bit = 0; bit < 8 && done < size; bit++, flags >>= 1) {
            if (flags & 1) {
                if (pos + 2 > inSize) {
                    free(out);
                    return NULL;
                }
                uint32_t ref = in[pos] | (uint32_t)in[pos + 1] << 8;
                uint32_t dist = ref & 0xFFF, count = (ref >> 12) + 3;
                pos += 2;
                if (dist == 0 || dist > done) {
                    free(out);
                    return NULL;
                }
                for (uint32_t k = 0; k < count && done < size; k++, done++) {
                    out[done] = out[done - dist]; /* may overlap the bytes being written */
                }
            } else {
                if (pos >= inSize) {
                    free(out);
                    return NULL;
                }
                out[done++] = in[pos++];
            }
        }
    }
    if (done != size) {
        free(out);
        return NULL;
    }
    *outSize = size;
    return out;
}

static uint64_t fnv1a64(uint64_t h, const uint8_t* p, size_t n) {
    for (size_t i = 0; i < n; i++) {
        h = (h ^ p[i]) * 0x100000001B3ull;
    }
    return h;
}

/* Module i's file as the game loads it; malloc'd, NULL with *why on failure. */
static uint8_t* readModule(CdDisc* d, const CdImageInfo* info, const uint8_t* table, const uint8_t* dirs,
                           const PortModuleData* m, char* why, size_t size) {
    int lba;
    uint32_t bytes, got;
    if (m->dir < 0) {
        if (!isoFind(d, info->boot, &lba, &bytes)) {
            fail(why, size, "Cannot find the boot executable %s on the disc.", info->boot);
            return NULL;
        }
    } else {
        unsigned first = m->dir < DIR_COUNT ? (unsigned)(dirs[2 * m->dir] | dirs[2 * m->dir + 1] << 8) : 0xFFFF;
        unsigned entry = first - 1 + (unsigned)m->file - 1;
        if (first == 0 || first == 0xFFFF || entry >= TABLE_SIZE / 7) {
            fail(why, size, "The disc's file table has no file %X of directory %X (%s).", m->file, m->dir, m->name);
            return NULL;
        }
        const uint8_t* e = table + 7 * entry;
        lba = e[0] | e[1] << 8 | e[2] << 16;
        int32_t n = (int32_t)u32(e + 3);
        if (n <= 0) {
            fail(why, size, "The disc's file %X of directory %X (%s) is empty.", m->file, m->dir, m->name);
            return NULL;
        }
        bytes = (uint32_t)n;
    }
    uint8_t* file = readUser(d, lba, bytes, &got);
    if (file == NULL) {
        fail(why, size, "Cannot read %s from the disc (LBA %d, %u bytes).", m->name, lba, (unsigned)bytes);
        return NULL;
    }
    if (m->lzss) {
        uint32_t outSize = 0;
        uint8_t* image = lzss(file, got, &outSize);
        free(file);
        if (image == NULL) {
            fail(why, size, "The disc's %s is not the compressed file this build expects.", m->name);
            return NULL;
        }
        file = image;
        bytes = outSize;
    }
    if (bytes != m->size) {
        free(file);
        fail(why, size, "The disc's %s is %u bytes, this build's is %u: a different release or a modified image.",
             m->name, (unsigned)bytes, m->size);
        return NULL;
    }
    return file;
}

/* gameModes in both retail boot headers (matched boot_header.rodata.s).
 * Keep layout offsets here; load the actual addresses from the player's image. */
static uint32_t retailHeapStarts[7];
uint32_t portRetailHeapStart(int mode) {
    return mode >= 0 && mode < 7 ? retailHeapStarts[mode] : 0;
}

static int load(const char* image, int apply, char* why, size_t size) {
    CdImageInfo info;
    if (size > 0) {
        why[0] = '\0';
    }
    if (image == NULL || !cdVerifyImage(image, &info, why, size)) {
        return 0;
    }
    if (strcmp(info.region, portDataRegion) != 0) {
        return fail(why, size, "This is a %s disc; this game build needs the %s release.", info.region, portDataRegion);
    }
    CdDisc* d = discOpen(image);
    if (d == NULL) {
        return fail(why, size, "%s", discLastError());
    }
    uint32_t got;
    uint8_t* table = readUser(d, TABLE_LBA, TABLE_SIZE, &got);
    uint8_t* dirs = readUser(d, DIRS_LBA, USER_SIZE, &got);
    uint8_t** files = calloc((size_t)portModuleCount, sizeof(*files));
    int ok = table != NULL && dirs != NULL && files != NULL;
    if (!ok) {
        fail(why, size, "Cannot read the disc's file table.");
    }
    size_t total = 0;
    for (int i = 0; ok && i < portModuleCount; i++) {
        const PortModuleData* m = &portModuleData[i];
        const PortSrcData* src = &portSrcData[i];
        if (m->count == 0 && *src->count == 0) {
            continue;
        }
        files[i] = readModule(d, &info, table, dirs, m, why, size);
        if (files[i] == NULL) {
            ok = 0;
            break;
        }
        uint64_t h = 0xCBF29CE484222325ull;
        for (int k = 0; k < m->count; k++) {
            const XgDataCopy* c = &m->copies[k];
            uint32_t n = c->size & ~DISC_SPECIFIC;
            if ((uint64_t)c->src + n > m->size) {
                ok = fail(why, size, "This build's copy list for %s runs past its file.", m->name);
                break;
            }
            if (!(c->size & DISC_SPECIFIC)) {
                h = fnv1a64(h, files[i] + c->src, n);
            }
            total += n;
        }
        uint64_t hs = 0xCBF29CE484222325ull;
        for (int k = 0; ok && k < *src->count; k++) {
            const XgDataCopy* c = &src->copies[k];
            if ((uint64_t)c->src + c->size > m->size) {
                ok = fail(why, size, "This build's copy list for %s's game C runs past its file.", m->name);
                break;
            }
            hs = fnv1a64(hs, files[i] + c->src, c->size);
            total += c->size;
        }
        if (ok && (h != m->hash || hs != *src->hash)) {
            ok = fail(why, size,
                      "The disc's %s data differs from what this build expects: a different release, revision "
                      "or a modified image.",
                      m->name);
        }
    }
    if (ok && apply) {
        for (int i = 0; i < portModuleCount; i++) {
            if (strcmp(portModuleData[i].name, "main") != 0 || !files[i]) continue;
            for (int mode = 0; mode < 7; mode++) {
                uint32_t offset = 0x888C + (uint32_t)mode * 16 + 8;
                uint32_t end = offset + 4 <= portModuleData[i].size ? u32(files[i] + offset) : 0;
                retailHeapStarts[mode] = end >= 0x80000000u && end < 0x801FBFF0u && !(end & 3) ? end + 4 : 0;
            }
        }
        for (int i = 0; i < portModuleCount; i++) {
            const PortModuleData* m = &portModuleData[i];
            for (int k = 0; k < m->count; k++) {
                const XgDataCopy* c = &m->copies[k];
                memcpy(c->dst, files[i] + c->src, c->size & ~DISC_SPECIFIC);
            }
            for (int k = 0; files[i] != NULL && k < *portSrcData[i].count; k++) {
                const XgDataCopy* c = &portSrcData[i].copies[k];
                memcpy(c->dst, files[i] + c->src, c->size);
            }
        }
        fprintf(stderr, "gamedata: %zu bytes of module data from %s Disc %d (%s)\n", total, info.region, info.disc,
                info.boot);
    }
    for (int i = 0; files != NULL && i < portModuleCount; i++) {
        free(files[i]);
    }
    free(files);
    free(table);
    free(dirs);
    discClose(d);
    return ok;
}

int portDataLoad(const char* image, char* why, size_t size) {
    return load(image, 1, why, size);
}

int portDataCheck(const char* image, char* why, size_t size) {
    return load(image, 0, why, size);
}
