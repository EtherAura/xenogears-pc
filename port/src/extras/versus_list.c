/*
 * The Battle arena's versus formations (port/src/system/arena.h, the menu
 * block port/src/ui/arena.cpp): the battle debug selector's battle tables on
 * the disc in the drive (directory 0x20/3, file table + 7 for tables 0..252:
 * 16 configs of 0x20 bytes, include/battle_loader/battle_loader.h
 * sBattleConfig, then 16 weights), every config with an enemy and no battle
 * event (m1 & 0x20: its event would take the battle's outcome), once each
 * (the same enemy set and enemies are one formation, the first table holding
 * it), named after its enemies (the set's string table, bestiary_data.h).
 * Read a few tables at a time (xgVersusScanStep), as the menu draws.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bestiary_data.h"
#include "cd/cd.h"
#include "disc.h"
#include "versus_list.h"

#define TABLES 253
#define TABLE_DIR 0x23 /* (0x20, 3) */

struct XgVersusScan {
    XgDisc* disc;
    int table;
    XgVersusFormation* list;
    int count, cap;
    uint32_t* keys; /* set and enemies, for the duplicates */
    struct {
        int set, type;
        char name[48];
    } names[512];
    int nameCount;
};

static const char* typeName(XgVersusScan* s, int set, int type) {
    for (int i = 0; i < s->nameCount; i++) {
        if (s->names[i].set == set && s->names[i].type == type) return s->names[i].name;
    }
    if (s->nameCount >= (int)(sizeof(s->names) / sizeof(s->names[0]))) return "";
    int i = s->nameCount++;
    s->names[i].set = set;
    s->names[i].type = type;
    /* string type + 1 is the type's name (battleMonsterMapping) */
    if (!xgBeastSetString(s->disc, set, type + 1, s->names[i].name, sizeof(s->names[i].name)) ||
        s->names[i].name[0] == '\0') {
        snprintf(s->names[i].name, sizeof(s->names[i].name), "Enemy %d", type);
    }
    return s->names[i].name;
}

XgVersusScan* xgVersusScanBegin(void) {
    const char* path = cdSwapPath(0);
    XgDisc* d = path != NULL && *path ? xgDiscOpen(path, NULL) : NULL;
    if (d == NULL) return NULL;
    XgVersusScan* s = calloc(1, sizeof *s);
    if (s == NULL) {
        xgDiscClose(d);
        return NULL;
    }
    s->disc = d;
    return s;
}

static void addFormation(XgVersusScan* s, int table, int config, const uint8_t* c) {
    int types[8], count = 0;
    uint32_t key = c[0];
    for (int i = 0; i < 8; i++) {
        types[i] = c[8 + i];
        if ((types[i] & 0x7F) != 0x7F) count++; /* 0x7F or 0xFF: no enemy */
        key = key * 0x9E3779B1u + (uint32_t)types[i];
    }
    if (count == 0 || (c[1] & 0x20)) return;
    for (int i = 0; i < s->count; i++) {
        if (s->keys[i] == key && s->list[i].set == c[0]) return;
    }
    if (s->count == s->cap) {
        int cap = s->cap ? s->cap * 2 : 256;
        XgVersusFormation* l = realloc(s->list, sizeof(*l) * (size_t)cap);
        if (l == NULL) return;
        s->list = l;
        uint32_t* k = realloc(s->keys, sizeof(*k) * (size_t)cap);
        if (k == NULL) return;
        s->keys = k;
        s->cap = cap;
    }
    XgVersusFormation* f = &s->list[s->count];
    f->table = table;
    f->config = config;
    f->set = c[0];
    f->label[0] = '\0';
    /* names in slot order, a repeated one as "xN" */
    int done[8] = {0};
    size_t at = 0;
    for (int i = 0; i < 8; i++) {
        if ((types[i] & 0x7F) == 0x7F || done[i]) continue;
        int n = 0;
        for (int j = i; j < 8; j++) {
            if (types[j] == types[i]) {
                done[j] = 1;
                n++;
            }
        }
        const char* name = typeName(s, c[0], types[i] & 0x7F);
        int w = snprintf(f->label + at, sizeof(f->label) - at, "%s%s%s", at ? ", " : "", name,
                         types[i] & 0x80 ? " (Gear)" : "");
        if (w > 0 && at + (size_t)w < sizeof(f->label)) at += (size_t)w;
        if (n > 1) {
            w = snprintf(f->label + at, sizeof(f->label) - at, " x%d", n);
            if (w > 0 && at + (size_t)w < sizeof(f->label)) at += (size_t)w;
        }
    }
    s->keys[s->count] = key;
    s->count++;
}

/* `budget` tables more; 1 once every table is read. */
int xgVersusScanStep(XgVersusScan* s, int budget) {
    for (; budget > 0 && s->table < TABLES; budget--, s->table++) {
        int e = xgDiscFileEntry(s->disc, TABLE_DIR, s->table + 7), size = 0;
        if (e < 0) continue;
        uint8_t* b = xgDiscReadEntry(s->disc, e, &size);
        if (b == NULL) continue;
        for (int c = 0; c < 16 && (c + 1) * 0x20 <= size; c++) {
            addFormation(s, s->table, c, b + c * 0x20);
        }
        free(b);
    }
    return s->table >= TABLES;
}

double xgVersusScanProgress(const XgVersusScan* s) {
    return (double)s->table / TABLES;
}

int xgVersusScanDisc(const XgVersusScan* s) {
    return xgDiscNumber(s->disc);
}

/* The formations (malloc'd, the caller frees), their count. */
int xgVersusScanResult(XgVersusScan* s, XgVersusFormation** out) {
    *out = s->list;
    s->list = NULL;
    int n = s->count;
    s->count = s->cap = 0;
    return n;
}

void xgVersusScanFree(XgVersusScan* s) {
    if (s == NULL) return;
    xgDiscClose(s->disc);
    free(s->list);
    free(s->keys);
    free(s);
}
