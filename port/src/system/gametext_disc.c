/*
 * Text from a Xenogears image other than the drive's (gametext.h): its file
 * table, its files, its LZSS streams, and where the game keeps its text
 * tables. Images are opened read-only with their own reader (cd/disc.c);
 * the drive, its pair and the game's state are never touched.
 *
 * The text tables (each the format getDialogParamPointer reads: a u32
 * header, u16 offsets from +4, the texts), as the game loads them:
 *   kernel        01/07, LZSS, an offset bundle (u32 n, u32 offsets): each
 *                 member a table (setupPrintDialogTextVar; member 27 is the
 *                 name entry's code table)
 *   menu          10/01 and 10/02: offset bundles of LZSS members
 *                 (m_801C65F4.c: the labels are 10/01's 4th, the
 *                 descriptions 10/02's)
 *   battle common 0C/03: an offset bundle of LZSS members (the battle's item
 *                 and attack names: its 16th and 38th; bld_801E4048.c)
 *   battle set    0D/2+2n: the enemy set's strings at the u16 at +0x30
 *   world         24/2C+0Bc: LZSS, its place texts at the u32 at +0x1C
 *                 (finalizeWorldMapFileLoading)
 *   field         04/B8+2n: section 7 (dialogue), section 5 (scripts)
 * Measured on the four retail images: every one of these parses as a table.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cd/cd_internal.h"
#include "cd/verify.h"
#include "gametext.h"

#define RAW_SECTOR 2352
#define USER_OFFSET 24
#define USER_SIZE 2048
#define TABLE_LBA 0x18
#define TABLE_SIZE 0x8000
#define DIRS_LBA 0x28
#define FILE_MAX (16 << 20)

struct GtImage {
    CdDisc* disc;
    char path[4096];
    CdImageInfo info;
    uint8_t table[TABLE_SIZE];
    uint8_t dirs[USER_SIZE];
};

static uint32_t le32(const uint8_t* p) {
    return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24;
}
static uint16_t le16(const uint8_t* p) {
    return (uint16_t)(p[0] | p[1] << 8);
}

static int readUser(GtImage* im, int lba, int sectors, uint8_t* out) {
    uint8_t raw[RAW_SECTOR];
    for (int i = 0; i < sectors; i++) {
        if (!discRead(im->disc, lba + i, raw)) return 0;
        memcpy(out + i * USER_SIZE, raw + USER_OFFSET, USER_SIZE);
    }
    return 1;
}

GtImage* gtImageOpen(const char* path, char* why, size_t size) {
    GtImage* im = calloc(1, sizeof(*im));
    if (im == NULL) return NULL;
    if (path == NULL || !cdVerifyImage(path, &im->info, why, size)) {
        free(im);
        return NULL;
    }
    snprintf(im->path, sizeof(im->path), "%s", path);
    im->disc = discOpen(path);
    if (im->disc == NULL || !readUser(im, TABLE_LBA, TABLE_SIZE / USER_SIZE, im->table) ||
        !readUser(im, DIRS_LBA, 1, im->dirs)) {
        if (size) snprintf(why, size, "Cannot read %s's file table.", path);
        gtImageClose(im);
        return NULL;
    }
    return im;
}

void gtImageClose(GtImage* im) {
    if (im == NULL) return;
    if (im->disc != NULL) discClose(im->disc);
    free(im);
}

const char* gtImagePath(const GtImage* im) { return im ? im->path : ""; }
const char* gtImageRegion(const GtImage* im) { return im ? im->info.region : ""; }
int gtImageDisc(const GtImage* im) { return im ? im->info.disc : 0; }

/* File `file` (from 1) of directory `dir`, whole sectors (an LZSS stream may
 * run into the padding, as the game's loader reads it). */
uint8_t* gtImageFile(GtImage* im, int dir, int file, int* size) {
    *size = 0;
    if (im == NULL || dir < 0 || dir >= USER_SIZE / 2 || file < 1) return NULL;
    unsigned base = le16(im->dirs + 2 * dir);
    if (base == 0 || base == 0xFFFF) return NULL;
    unsigned entry = base - 1 + (unsigned)file - 1;
    if (entry >= TABLE_SIZE / 7) return NULL;
    const uint8_t* e = im->table + entry * 7;
    unsigned lba = e[0] | e[1] << 8 | e[2] << 16;
    int32_t bytes = (int32_t)le32(e + 3);
    if (lba == 0xFFFFFF || bytes <= 0 || bytes > FILE_MAX) return NULL;
    int sectors = (bytes + USER_SIZE - 1) / USER_SIZE;
    uint8_t* out = malloc((size_t)sectors * USER_SIZE + 16);
    if (out == NULL || !readUser(im, (int)lba, sectors, out)) {
        free(out);
        return NULL;
    }
    memset(out + sectors * USER_SIZE, 0, 16);
    *size = sectors * USER_SIZE;
    return out;
}

/* The game's LZSS (tools/xenofs.py describes it): u32 size, then groups of
 * eight items under a flag byte (LSB first): 0 a literal, 1 a u16 back
 * reference (12-bit distance, length + 3 in the top 4 bits). */
uint8_t* gtLzss(const uint8_t* src, int size, int* outSize) {
    *outSize = 0;
    if (src == NULL || size < 4) return NULL;
    uint32_t n = le32(src);
    if (n == 0 || n > FILE_MAX) return NULL;
    uint8_t* out = malloc(n + 16);
    if (out == NULL) return NULL;
    uint32_t o = 0;
    int i = 4;
    while (o < n && i < size) {
        int flags = src[i++];
        for (int k = 0; k < 8 && o < n; k++, flags >>= 1) {
            if (flags & 1) {
                if (i + 1 >= size) goto bad;
                int ref = src[i] | src[i + 1] << 8;
                i += 2;
                uint32_t dist = (uint32_t)(ref & 0xFFF), len = (uint32_t)(ref >> 12) + 3;
                if (dist == 0 || dist > o) goto bad;
                for (uint32_t j = 0; j < len && o < n; j++, o++) out[o] = out[o - dist];
            } else {
                if (i >= size) goto bad;
                out[o++] = src[i++];
            }
        }
    }
    if (o != n) goto bad;
    memset(out + n, 0, 16);
    *outSize = (int)n;
    return out;
bad:
    free(out);
    return NULL;
}

uint8_t* gtFieldSection(GtImage* im, int field, int section, int* size) {
    *size = 0;
    if (field < 0 || field >= 730 || section < 0 || section >= 9) return NULL;
    int bytes;
    uint8_t* arch = gtImageFile(im, 4, 0xB8 + 2 * field, &bytes);
    if (arch == NULL || bytes < 0x154) {
        free(arch);
        return NULL;
    }
    uint32_t want = le32(arch + 0x10C + 4 * section), at = le32(arch + 0x130 + 4 * section);
    uint8_t* out = NULL;
    if (want != 0 && at + 4 < (uint32_t)bytes) out = gtLzss(arch + at, bytes - (int)at, size);
    free(arch);
    return out;
}

uint8_t* gtImageFont(GtImage* im, int* size) {
    int bytes;
    uint8_t* file = gtImageFile(im, 1, 6, &bytes);
    uint8_t* out = file ? gtLzss(file, bytes, size) : NULL;
    free(file);
    return out;
}

/* ---- text tables ------------------------------------------------------------------------- */

/* Entries: the header's u16 + 1 when the offsets hold that many, else as
 * many as fit before the first text. Offsets may repeat (an empty entry,
 * or one text for two) but never go back or out of the table. */
int gtTableParse(const uint8_t* base, int size, GtTable* t) {
    memset(t, 0, sizeof(*t));
    if (base == NULL || size < 7) return 0;
    int first = le16(base + 4);
    if (first < 6 || first >= size) return 0;
    int fit = (first - 4) / 2, count = le16(base) + 1;
    if (count > fit) count = fit;
    int prev = first;
    for (int i = 0; i < count; i++) {
        int o = le16(base + 4 + 2 * i);
        if (o < prev || o >= size || o < first) return 0;
        prev = o;
    }
    t->base = base;
    t->size = size;
    t->count = count;
    return 1;
}

/* Entry `index`: from its offset to the table's end (4 KB at most); where
 * it ends is for a reader of its codes to find (an escape's operand or a
 * two-byte character's second byte may be 0, the end code). */
const uint8_t* gtTableEntry(const GtTable* t, int index, int* length) {
    *length = 0;
    if (t == NULL || t->base == NULL || index < 0 || index >= t->count) return NULL;
    int o = le16(t->base + 4 + 2 * index);
    *length = t->size - o < 4096 ? t->size - o : 4096;
    return t->base + o;
}

uint64_t gtFingerprint(const uint8_t* base, int size) {
    uint64_t h = 0xCBF29CE484222325ull;
    int first = le16(base + 4);
    int head = first < 132 ? first : 132;
    if (size >= 0 && head > size) head = size;
    for (int i = 0; i < head; i++) h = (h ^ base[i]) * 0x100000001B3ull;
    for (int i = 0; i < 256 && (size < 0 || first + i < size); i++) {
        uint8_t c = base[first + i];
        h = (h ^ c) * 0x100000001B3ull;
        if (c == 0) break;
    }
    return h;
}

int gtKeyCategory(const GtKey* k) {
    switch (k->kind) {
    case GT_KIND_KERNEL:
        if (k->member == 28 || k->member == 50) return -1; /* the code table (27), the buttons (49) */
        return k->member == 19 ? GT_BATTLE : GT_NAMES;     /* 18: the battle's labels */
    case GT_KIND_MENU:
        return GT_MENUS;
    case GT_KIND_BATTLE_COMMON:
    case GT_KIND_BATTLE_SET:
        return GT_BATTLE;
    case GT_KIND_WORLD:
        return GT_DIALOGUE;
    default:
        return -1;
    }
}

/* Each kind's files (in its directory). */
static const struct {
    int dir, first, step, count, compressed, bundle;
} kinds[GT_KINDS] = {
    [GT_KIND_KERNEL] = {0x01, 7, 1, 1, 1, 1},
    [GT_KIND_MENU] = {0x10, 1, 1, 2, 0, 1},
    [GT_KIND_BATTLE_COMMON] = {0x0C, 3, 1, 1, 0, 1},
    [GT_KIND_BATTLE_SET] = {0x0D, 2, 2, 76, 0, 0},
    [GT_KIND_WORLD] = {0x24, 0x2C, 0x0B, 17, 1, 0},
};

/* The file's tables: visit(user, key, table, size), each member's own copy. */
static int fileTables(GtImage* im, int kind, int file, int onlyMember, GtVisit visit, void* user) {
    int bytes, visited = 0;
    uint8_t* raw = gtImageFile(im, kinds[kind].dir, file, &bytes);
    if (raw == NULL) return 0;
    uint8_t* data = raw;
    int size = bytes;
    if (kinds[kind].compressed) {
        data = gtLzss(raw, bytes, &size);
        free(raw);
        raw = NULL;
        if (data == NULL) return 0;
    }
    GtKey key = {kind, file, 0};
    GtTable t;
    if (kinds[kind].bundle) {
        uint32_t n = size >= 8 ? le32(data) : 0;
        if (n == 0 || n > 512 || 4 + 4 * n > (uint32_t)size) n = 0;
        for (uint32_t m = 1; m <= n; m++) {
            if (onlyMember && (int)m != onlyMember) continue;
            uint32_t at = le32(data + 4 * m);
            if (at >= (uint32_t)size) continue;
            uint32_t end = m < n ? le32(data + 4 * (m + 1)) : (uint32_t)size;
            if (end < at || end > (uint32_t)size) end = (uint32_t)size;
            key.member = (int)m;
            if (kinds[kind].compressed) {
                /* members of a decompressed bundle are the tables themselves */
                if (gtTableParse(data + at, (int)(end - at), &t)) {
                    visit(user, &key, data + at, (int)(end - at));
                    visited++;
                }
            } else {
                int n2;
                uint8_t* member = gtLzss(data + at, size - (int)at, &n2);
                if (member != NULL && gtTableParse(member, n2, &t)) {
                    visit(user, &key, member, n2);
                    visited++;
                }
                free(member);
            }
        }
    } else {
        uint32_t at = kind == GT_KIND_BATTLE_SET ? (size > 0x32 ? le16(data + 0x30) : 0)
                                                 : (size > 0x20 ? le32(data + 0x1C) : 0);
        if (at > 0 && at < (uint32_t)size && gtTableParse(data + at, size - (int)at, &t)) {
            visit(user, &key, data + at, size - (int)at);
            visited++;
        }
    }
    free(data);
    return visited;
}

uint8_t* gtImageMember(GtImage* im, int dir, int file, int member, int* size) {
    int bytes;
    *size = 0;
    uint8_t* data = gtImageFile(im, dir, file, &bytes);
    if (data == NULL) return NULL;
    uint32_t n = bytes >= 8 ? le32(data) : 0;
    uint8_t* out = NULL;
    if (member >= 1 && (uint32_t)member <= n && n <= 512 && 4 + 4 * n <= (uint32_t)bytes) {
        uint32_t at = le32(data + 4 * member);
        uint32_t end = (uint32_t)member < n ? le32(data + 4 * (member + 1)) : (uint32_t)bytes;
        /* with what follows it in the file: an LZSS stream may end in the
         * middle of a group, its last items read from the next member's
         * bytes (the game's decoder stops at the size it wrote) */
        uint32_t keep = (uint32_t)bytes - at < (end - at) + GT_MEMBER_SLACK ? (uint32_t)bytes - at
                                                                             : (end - at) + GT_MEMBER_SLACK;
        if (at < end && end <= (uint32_t)bytes && (out = calloc(1, (end - at) + GT_MEMBER_SLACK)) != NULL) {
            memcpy(out, data + at, keep);
            *size = (int)(end - at);
        }
    }
    free(data);
    return out;
}

int gtImageTables(GtImage* im, GtVisit visit, void* user) {
    int n = 0;
    for (int kind = 0; kind < GT_KINDS; kind++) {
        for (int i = 0; i < kinds[kind].count; i++) {
            n += fileTables(im, kind, kinds[kind].first + i * kinds[kind].step, 0, visit, user);
        }
    }
    return n;
}

typedef struct {
    uint8_t* copy;
    int size;
} OneTable;

static void keepOne(void* user, const GtKey* key, const uint8_t* table, int size) {
    (void)key;
    OneTable* one = user;
    if (one->copy != NULL) return;
    one->copy = malloc((size_t)size + 16);
    if (one->copy == NULL) return;
    memcpy(one->copy, table, (size_t)size);
    memset(one->copy + size, 0, 16);
    one->size = size;
}

uint8_t* gtImageTable(GtImage* im, const GtKey* key, int* size) {
    OneTable one = {NULL, 0};
    *size = 0;
    if (key == NULL || key->kind < 0 || key->kind >= GT_KINDS) return NULL;
    if (kinds[key->kind].bundle && key->member <= 0) return NULL;
    fileTables(im, key->kind, key->file, key->member, keepOne, &one);
    *size = one.size;
    return one.copy;
}
