#include "bestiary_data.h"

#include <stdlib.h>
#include <string.h>

#include "files.h"
#include "gallery.h"
#include "sprites_enemy.h"
#include "tables_int.h"

#include "pc_gameplay.h" /* port/src/system/gameplay.c */

static uint32_t rd32(const uint8_t* p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static int rd16(const uint8_t* p) { return p[0] | p[1] << 8; }

static uint8_t* readFile(XgDisc* d, int dir, int file, int* size) {
    int e = xgDiscFileEntry(d, dir, file);
    *size = 0;
    if (e < 0 || xgDiscEntry(d, e).size <= 0 || xgDiscEntry(d, e).size > (8 << 20)) return NULL;
    return xgDiscReadEntry(d, e, size);
}

static uint64_t fnv(uint64_t h, const uint8_t* p, size_t n) {
    for (size_t i = 0; i < n; i++) h = (h ^ p[i]) * 1099511628211ull;
    return h;
}

/* A u32-count, u16-offset string table (getDialogParamPointer) entry. */
static int tableString(const uint8_t* b, int size, int table, int i, int region, char* out, int outSize) {
    out[0] = '\0';
    if (table < 0 || table + 4 > size || i < 0 || (uint32_t)i >= rd32(b + table) || table + 6 + 2 * i > size) return 0;
    int at = table + rd16(b + table + 4 + 2 * i);
    return at < size && tablesGameText(b + at, size - at, region, out, outSize);
}

/* The set file's type count: the leading u16 script-block offsets, each
 * past the records (loadEnemies copies record k for type id k). */
static int typeCount(const uint8_t* b, int size) {
    if (size < 0x32) return 0;
    int n = 0;
    while (n < 24 && rd16(b + 2 * n) != 0) n++;
    int strings = rd16(b + 0x30);
    while (n > 0 && 0x32 + n * XG_BEAST_RECORD > (strings < size ? strings : size)) n--;
    return n;
}

static uint64_t visualHash(const uint8_t* v, int size, int type, int* kind) {
    *kind = 0;
    int count = xgEnemySpriteCount(v, size);
    if (type >= count) return 0;
    XgEnemySprite s;
    int k = xgEnemySpriteGet(v, size, type, &s);
    if (k == 1) {
        *kind = 1;
        uint64_t h = fnv(14695981039346656037ull, v + s.bundleOffset, (size_t)s.bundleSize);
        return fnv(h, v + s.sheetOffset, (size_t)s.sheetSize);
    }
    if (k == -1) {
        /* A model: its container up to the next entry's data, or the file's end. */
        *kind = -1;
        uint32_t at = rd32(v + 8 + 12 * type + 4), end = (uint32_t)size;
        if (at < 8) return at; /* shares an earlier entry's model */
        for (int j = 0; j < count; j++) {
            for (int f = 0; f < 2; f++) {
                uint32_t o = rd32(v + 8 + 12 * j + 4 * f);
                if (o > at && o < end) end = o;
            }
        }
        return at < end ? fnv(14695981039346656037ull, v + at, end - at) : 0;
    }
    return 0;
}

int xgBeastItemName(XgDisc* d, int category, int item, char* out, int outSize) {
    /* The text bundle's banks (setupPrintDialogTextVar: u32 count, then the
     * banks' offsets): weapons 23, accessories 17, items 22, Gear weapons
     * 51, Gear accessories 50 (dialog.c getWeaponName ... getGearWeaponName). */
    static const int banks[5] = {0x5C / 4, 0x44 / 4, 0x58 / 4, 0xCC / 4, 0xC8 / 4};
    static XgDisc* cachedDisc;
    static uint8_t* text;
    static int textSize;
    out[0] = '\0';
    if (category < 0 || category > 4 || d == NULL) return 0;
    if (cachedDisc != d) {
        free(text);
        text = NULL;
        textSize = 0;
        int size = 0;
        uint8_t* raw = readFile(d, 1, 7, &size);
        if (raw) text = xgLzssDecompress(raw, size, &textSize);
        free(raw);
        cachedDisc = d;
    }
    if (!text || 8 + 4 * banks[category] > textSize) return 0;
    int bank = (int)rd32(text + 4 + 4 * banks[category]);
    return tableString(text, textSize, bank, item, tablesRegionOf(d), out, outSize);
}

int xgBeastSetString(XgDisc* d, int set, int i, char* out, int outSize) {
    int size = 0;
    out[0] = '\0';
    uint8_t* b = readFile(d, 0x0D, 2 + 2 * set, &size);
    int ok = b && size >= 0x32 && tableString(b, size, rd16(b + 0x30), i, tablesRegionOf(d), out, outSize);
    free(b);
    return ok;
}

static void fill(XgBeast* e, const uint8_t* r) {
    memcpy(e->raw, r, XG_BEAST_RECORD);
    int baseMax = rd16(r + 0x4E);
    int gearMax = (int)rd32(r + 0xA4 + 0x64);
    e->gear = baseMax == 0 && gearMax > 0;
    if (e->gear) {
        e->hp = gearMax;
        e->attack = r[0xA4 + 0x3F]; /* m3F_attackMultiplier (gameplay.c scales it as the attack) */
        e->defence = rd16(r + 0xA4 + 0x70);
        e->etherDefence = rd16(r + 0xA4 + 0x72);
    } else {
        e->hp = baseMax;
        e->attack = r[0x58];
        e->defence = r[0x59];
        e->etherDefence = r[0x5C];
    }
    e->agility = r[0x5A]; /* enemy ATB uses the base agility, Gears too (gameplay.c) */
    e->ether = r[0x5B];
    e->hit = r[0x5E];
    e->evade = r[0x5F];
    e->level = r[0x62];
    e->exp = (int)rd32(r + 0x14C);
    e->gold = rd16(r + 0x156);
    for (int i = 0; i < 2; i++) {
        e->drops[i].chance = r[0x150 + i];
        e->drops[i].item = r[0x152 + i];
        e->drops[i].category = r[0x154 + i];
    }
    e->monsterFlags = rd16(r + 0x34);
    e->monsterFlags2 = rd16(r + 0x36);
    e->weakness = rd16(r + 0x38);
    e->resist[0] = rd16(r + 0x84 + 8);
    e->resist[1] = rd16(r + 0x84 + 10);
    e->gearResist = rd16(r + 0xA4 + 0x6E);
}

int xgBeastsRead(XgDisc* d, XgBeast** out, int count) {
    if (d == NULL) return count;
    int region = tablesRegionOf(d), number = xgDiscNumber(d);
    int files = xgDiscDirFiles(d, 0x0D);
    for (int set = 0; 3 + 2 * set < files; set++) {
        int size = 0, vsize = 0;
        uint8_t* b = readFile(d, 0x0D, 2 + 2 * set, &size);
        uint8_t* v = readFile(d, 0x0D, 3 + 2 * set, &vsize);
        int types = b ? typeCount(b, size) : 0;
        if (types > 0) {
            XgBeast* grown = realloc(*out, sizeof(XgBeast) * (size_t)(count + types));
            if (grown == NULL) {
                free(b);
                free(v);
                return -1;
            }
            *out = grown;
        }
        for (int k = 0; k < types; k++) {
            XgBeast* e = &(*out)[count++];
            memset(e, 0, sizeof(*e));
            e->disc = number;
            e->set = set;
            e->type = k;
            fill(e, b + 0x32 + k * XG_BEAST_RECORD);
            tableString(b, size, rd16(b + 0x30), k + 1, region, e->name, sizeof(e->name));
            for (int i = 0; i < 2; i++)
                if (e->drops[i].chance > 0)
                    xgBeastItemName(d, e->drops[i].category, e->drops[i].item, e->drops[i].name, sizeof(e->drops[i].name));
            uint64_t h = fnv(14695981039346656037ull, e->raw, XG_BEAST_RECORD);
            h = fnv(h, (const uint8_t*)e->name, strlen(e->name));
            uint64_t vh = v ? visualHash(v, vsize, k, &e->visual) : 0;
            e->identity = fnv(h, (const uint8_t*)&vh, sizeof(vh));
        }
        free(b);
        free(v);
    }
    return count;
}

void xgBeastWithOptions(const XgBeast* in, XgBeast* out) {
    uint8_t r[XG_BEAST_RECORD];
    memcpy(r, in->raw, sizeof(r));
    pcGameplayEnemyStatsForType(r, in->set, in->type);
    *out = *in;
    fill(out, r);
    out->exp = (int)pcGameplayExperience((unsigned)in->exp);
    out->gold = (int)pcGameplayGold((unsigned)in->gold);
}

/* ---- attacks -------------------------------------------------------------------------------- */

/* 0C/03 member 3 (0-based: the loader's reloc[4], bld_801E4048.c): the
 * enemies' 200 attack records of 0x28 bytes, LZSS. */
static uint8_t* attackTable(XgDisc* d, int* size) {
    static XgDisc* cachedDisc;
    static uint8_t* table;
    static int tableSize;
    if (cachedDisc != d) {
        free(table);
        table = NULL;
        tableSize = 0;
        int n = 0;
        uint8_t* b = readFile(d, 0x0C, 3, &n);
        if (b && n >= 20 && rd32(b) > 4) {
            uint32_t at = rd32(b + 4 + 4 * 3), end = rd32(b + 4 + 4 * 4);
            if (at < (uint32_t)n && end <= (uint32_t)n && end > at)
                table = xgLzssDecompress(b + at, (int)(end - at), &tableSize);
        }
        free(b);
        cachedDisc = d;
    }
    *size = tableSize;
    return table;
}

static void attackFields(XgDisc* d, XgBeastAttack* a) {
    int size = 0;
    const uint8_t* t = attackTable(d, &size);
    if (a->kind != 1 || a->attack < 0 || !t || (a->attack + 1) * 0x28 > size) return;
    const uint8_t* r = t + a->attack * 0x28;
    a->flags = rd16(r + 0x0A);
    a->power = r[0x11];
    a->hitBonus = (int8_t)r[0x15];
    a->statusChance = r[0x1C];
    a->statusType = r[0x1D];
    a->statusMask = rd16(r + 0x1E);
    a->elements = r[0x22];
}

int xgBeastAttacks(XgDisc* d, int set, int type, XgBeastAttack* out, int max) {
    int size = 0, n = 0;
    uint8_t* b = readFile(d, 0x0D, 2 + 2 * set, &size);
    if (!b || type < 0 || type >= typeCount(b, size)) {
        free(b);
        return 0;
    }
    int region = tablesRegionOf(d), strings = rd16(b + 0x30);
    int block = rd16(b + 2 * type);
    for (int script = 0; script < 4 && block + 8 <= size; script++) {
        if (script == 1) continue; /* loaded, never run (b_80079674.c) */
        int pc = block + rd16(b + block + 2 * script), end = strings < size ? strings : size;
        int ctx[8], variable[8], inBlock = 0, pendingName = -1;
        for (int i = 0; i < 8; i++) ctx[i] = variable[i] = 0;
        for (int steps = 0; pc + 4 <= end && steps < 4096; steps++, pc += 4) {
            int op = b[pc], x = b[pc + 1], y = b[pc + 2], z = b[pc + 3];
            if (op == 0xFF || op == 0xFD) {
                /* a guarded block's return; unguarded, the script's end */
                if (!inBlock) break;
                inBlock = 0;
                pendingName = -1;
                continue;
            }
            if (op >= 0x81 && op <= 0xEF && op != 0x9A) inBlock = 1;
            if ((op == 0x01 || op == 0x02 || op == 0x52) && x < 8) {
                ctx[x] = op == 0x01 ? y : 0;
                variable[x] = op != 0x01;
                if (x != 0) continue;
                /* m0 written: the action is complete (and the next begins) */
                int kind = ctx[0];
                if (!variable[0] && kind == 12 && !variable[3]) {
                    /* "show name string m3": the next action's name, or a
                     * line the enemy says (the set's strings quote speech) */
                    char text[64];
                    tableString(b, size, strings, ctx[3], region, text, sizeof(text));
                    if (strchr(text, '"') == NULL) {
                        pendingName = ctx[3];
                    } else if (n < max) {
                        int seen = 0;
                        for (int i = 0; i < n && !seen; i++) seen = out[i].kind == 12 && out[i].nameIndex == ctx[3];
                        if (!seen) {
                            XgBeastAttack* line = &out[n++];
                            memset(line, 0, sizeof(*line));
                            line->kind = 12;
                            line->script = script;
                            line->attack = line->animation = -1;
                            line->nameIndex = ctx[3];
                            memcpy(line->name, text, sizeof(line->name));
                        }
                    }
                }
                if (!variable[0] && (kind == 1 || kind == 13) && n < max) {
                    XgBeastAttack a;
                    memset(&a, 0, sizeof(a));
                    a.kind = kind;
                    a.script = script;
                    a.attack = kind == 1 && !variable[1] ? ctx[1] : -1;
                    a.animation = kind == 1 && !variable[2] ? ctx[2] : -1;
                    a.nameIndex = !variable[3] && ctx[3] != 0 ? ctx[3] : pendingName;
                    pendingName = -1;
                    a.value = kind == 13 ? (variable[4] || variable[5] ? -1 : ctx[4] | ctx[5] << 8) : 0;
                    int seen = 0;
                    for (int i = 0; i < n && !seen; i++)
                        seen = out[i].kind == a.kind && out[i].attack == a.attack && out[i].animation == a.animation &&
                               out[i].nameIndex == a.nameIndex && out[i].value == a.value;
                    if (!seen) {
                        if (a.nameIndex >= 0) tableString(b, size, strings, a.nameIndex, region, a.name, sizeof(a.name));
                        attackFields(d, &a);
                        out[n++] = a;
                    }
                }
                for (int i = 0; i < 8; i++) ctx[i] = variable[i] = 0;
            } else if (op == 0x3D && x < 7) {
                ctx[x] = y;
                ctx[x + 1] = z;
                variable[x] = variable[x + 1] = 0;
            }
        }
    }
    free(b);
    return n;
}

/* ---- where battles happen -------------------------------------------------------------------- */

static void placeFrom(XgBeastPlace* p, const uint8_t* c) {
    p->set = c[0];
    p->flags = c[1];
    p->arena = c[2];
    p->event = c[3];
    for (int i = 0; i < 8; i++) p->types[i] = c[8 + i];
}

static int hasEnemies(const uint8_t* c) {
    for (int i = 0; i < 8; i++)
        if ((c[8 + i] & 0x7F) != 0x7F) return 1;
    return 0;
}

/* Field section k (archive 04/B8+2n: sizes at +0x10C, offsets at +0x130,
 * each section its own LZSS stream), malloc'd. */
static uint8_t* fieldSection(const uint8_t* a, int size, int k, int* out) {
    *out = 0;
    if (size < 0x190) return NULL;
    uint32_t at = rd32(a + 0x130 + 4 * k), want = rd32(a + 0x10C + 4 * k), end = (uint32_t)size;
    for (int j = 0; j < 9; j++) {
        uint32_t o = rd32(a + 0x130 + 4 * j);
        if (o > at && o < end) end = o;
    }
    if (at == 0 || at >= (uint32_t)size || want == 0 || want > (1u << 20)) return NULL;
    int n = 0;
    uint8_t* b = xgLzssDecompress(a + at, (int)(end - at), &n);
    if (b == NULL || n < (int)want) {
        free(b);
        return NULL;
    }
    *out = n;
    return b;
}

int xgBeastFieldPlaces(XgDisc* d, int field, XgBeastPlace* out, int max, int* story) {
    int n = 0, size = 0, number = xgDiscNumber(d);
    *story = -1;
    uint8_t* a = readFile(d, 4, 0xB8 + 2 * field, &size);
    if (a == NULL || size < 0x190) {
        free(a);
        return 0;
    }
    int tsize = 0;
    uint8_t* table = fieldSection(a, size, 6, &tsize);
    free(a);
    /* The scripts: the encounter op, the scripted battles, the story. */
    int random = 0, maybe = 0;
    int scripted[64], scriptedCount = 0;
    XgFieldScript fs;
    if (xgFieldScriptRead(d, field, &fs)) {
        uint8_t* marks = xgFieldScriptWalk(&fs);
        for (int pc = 0; marks && pc < fs.codeSize; pc++) {
            if (!marks[pc]) continue;
            const uint8_t* c = fs.code + pc;
            int len = marks[pc];
            if (c[0] == 0xF7 && len >= 5) {
                int timer = rd16(c + 1), count = rd16(c + 3);
                if ((timer & 0x8000) && (count & 0x8000)) random |= (timer & 0x7FFF) && (count & 0x7FFF);
                else maybe = 1;
            } else if (c[0] == 0x71 && len >= 3 && (rd16(c + 1) & 0x8000) && scriptedCount < 64) {
                scripted[scriptedCount++] = rd16(c + 1) & 0x7FFF;
            } else if (c[0] == 0xFE && len >= 4 && c[1] == 0x84 && (rd16(c + 2) & 0x8000) && scriptedCount < 64) {
                scripted[scriptedCount++] = rd16(c + 2) & 0x7FFF;
            } else if (c[0] == 0x87 && len >= 3 && (rd16(c + 1) & 0x8000)) {
                int v = rd16(c + 1) & 0x7FFF;
                if (*story < 0 || v < *story) *story = v;
            }
        }
        free(marks);
        xgFieldScriptFree(&fs);
    }
    if (table != NULL && tsize >= 0x210) {
        int total = 0;
        for (int k = 0; k < 16; k++) total += table[0x200 + k];
        for (int k = 0; k < 16 && (random || maybe) && n < max; k++) {
            if (!table[0x200 + k] || !hasEnemies(table + 0x20 * k)) continue;
            XgBeastPlace* p = &out[n++];
            memset(p, 0, sizeof(*p));
            p->disc = number;
            p->kind = XG_PLACE_FIELD_RANDOM;
            p->field = field;
            p->config = k;
            p->weight = table[0x200 + k];
            p->total = total + 1; /* r = rand()*(total+1) >> 15: 0 is no battle */
            p->randomMaybe = !random;
            placeFrom(p, table + 0x20 * k);
        }
        for (int i = 0; i < scriptedCount && n < max; i++) {
            int k = scripted[i];
            int dup = 0;
            for (int j = 0; j < i; j++) dup |= scripted[j] == k;
            if (dup || k >= 16 || !hasEnemies(table + 0x20 * k)) continue;
            XgBeastPlace* p = &out[n++];
            memset(p, 0, sizeof(*p));
            p->disc = number;
            p->kind = XG_PLACE_FIELD_SCRIPTED;
            p->field = field;
            p->config = k;
            placeFrom(p, table + 0x20 * k);
        }
    }
    free(table);
    return n;
}

int xgBeastWorldPlaces(XgDisc* d, int world, XgBeastPlace* out, int max) {
    int n = 0, size = 0, number = xgDiscNumber(d);
    uint8_t* raw = readFile(d, 0x24, 0x2B + 0x0B * world + 1, &size);
    int asize = 0;
    uint8_t* a = raw ? xgLzssDecompress(raw, size, &asize) : NULL;
    free(raw);
    if (a == NULL || asize < 0x6C) {
        free(a);
        return 0;
    }
    for (int zone = 0; zone < 16; zone++) {
        int32_t at = (int32_t)rd32(a + 0x2C + 4 * zone);
        if (at <= 0 || at + 0x240 > asize) continue;
        const uint8_t* t = a + at;
        for (int range = 0; range < 4; range++) {
            int total = 0;
            for (int k = 0; k < 16; k++) total += t[0x200 + 16 * range + k];
            for (int k = 0; k < 16 && n < max; k++) {
                if (!t[0x200 + 16 * range + k] || !hasEnemies(t + 0x20 * k)) continue;
                XgBeastPlace* p = &out[n++];
                memset(p, 0, sizeof(*p));
                p->disc = number;
                p->kind = XG_PLACE_WORLD;
                p->world = world;
                p->zone = zone;
                p->range = range;
                p->config = k;
                p->weight = t[0x200 + 16 * range + k];
                p->total = total;
                placeFrom(p, t + 0x20 * k);
            }
        }
    }
    free(a);
    return n;
}

int xgBeastWorldStory(int world) {
    /* initWorldMap (w_80070CFC.c): below mode 8, configuration c is the one
     * from story 0x18, 0x36, 0x87, 0x95, 0xBA, 0xC6, 0xCC, 0xED on. */
    static const int from[9] = {0, 0x18, 0x36, 0x87, 0x95, 0xBA, 0xC6, 0xCC, 0xED};
    return world >= 0 && world < 9 ? from[world] : -1;
}

int xgBeastRangeStory(int range) {
    static const int from[4] = {0, 0x36, 0xC9, 0x154};
    return range >= 0 && range < 4 ? from[range] : -1;
}
