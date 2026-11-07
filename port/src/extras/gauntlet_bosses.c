/*
 * The gauntlets' boss list (port/src/system/gauntlet.h), read from the
 * player's discs with the bestiary's readers (bestiary_data.h):
 *
 *   - every battle a field's scripts start (ops 71 and FE 84 with an
 *     immediate config, xgBeastFieldPlaces), on both discs, fields 0 and
 *     720-729 (the developers' and planners' rooms) left out;
 *   - a boss: one of its enemies (a record with HP, not the debug set 0) is
 *     in no random battle of either disc (a field's table or a world map
 *     zone), as the bestiary's Bosses tab counts them;
 *   - a Gear battle: an enemy slot is a Gear (m4 & 0x80) or the config
 *     puts the party in Gears (m1 & 0x10);
 *   - its field's whole table (archive 04/B8+2n section 6: the 16 configs):
 *     a battle event may chain into another of them (op 0x24);
 *   - the story: the lowest story progress its field's scripts set (op 87),
 *     else the lowest of the fields up to three exits away (as the bestiary
 *     estimates it: a lower bound);
 *   - the same config bytes on several fields or discs: one boss, its
 *     earliest place.
 * Pure data from the discs: no game state.
 */
#include "system/gauntlet.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bestiary_data.h"
#include "cd/cd.h"
#include "disc.h"
#include "files.h"
#include "gallery.h"
#include "tables.h"
#include "tables_int.h"

#define FIELDS 730
#define WORLDS 17
#define PLACES 64

typedef struct {
    int disc, field, config;
    int story; /* from the scripts around its battle op, -1 none */
    int music; /* the theme its routine plays before it, -1 none */
    unsigned char cfg[0x20];
    unsigned char* table; /* the field's 0x200 bytes (shared by its places) */
} Scripted;

struct XgGauntletScan {
    XgDisc* discs[2];
    int disc, field, world, beasts, done;
    int fieldStory[2][FIELDS];
    int ownStory[2][FIELDS];      /* the lowest progress its own scripts set, before the estimates */
    Scripted* scripted;
    int scriptedCount, scriptedCap;
    uint64_t* random; /* enemies of random battles: (disc, set, type) keys, then their identities */
    int randomCount, randomCap;
    unsigned char** tables;
    int tableCount, tableCap;
    XgBeast* beastList;
    int beastCount;
    int identified; /* random's keys are identities now */
};

static uint32_t rd32(const uint8_t* p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static int debugField(int field) {
    return field == 0 || field >= 720;
}

/* The field archive's section 6 (16 configs, then weights), malloc'd. */
static unsigned char* fieldTable(XgDisc* d, int field) {
    int e = xgDiscFileEntry(d, 4, 0xB8 + 2 * field), size = 0;
    if (e < 0 || xgDiscEntry(d, e).size < 0x190) return NULL;
    uint8_t* a = xgDiscReadEntry(d, e, &size);
    if (a == NULL || size < 0x190) {
        free(a);
        return NULL;
    }
    uint32_t at = rd32(a + 0x130 + 4 * 6), want = rd32(a + 0x10C + 4 * 6), end = (uint32_t)size;
    for (int j = 0; j < 9; j++) {
        uint32_t o = rd32(a + 0x130 + 4 * j);
        if (o > at && o < end) end = o;
    }
    unsigned char* out = NULL;
    if (at != 0 && at < (uint32_t)size && want >= PC_GAUNTLET_TABLE && want <= 0x1000) {
        int n = 0;
        uint8_t* b = xgLzssDecompress(a + at, (int)(end - at), &n);
        if (b != NULL && n >= PC_GAUNTLET_TABLE) {
            out = b;
        } else {
            free(b);
        }
    }
    free(a);
    return out;
}

static int immediate(const uint8_t* p) {
    int v = p[0] | p[1] << 8;
    return v & 0x8000 ? v & 0x7FFF : -1;
}

/* The story progress a test op implies where it passes: EQUAL x (op 86) x,
 * LESS x (op 84) x - 1, GREATER x (op 85) x + 1; -1 none. */
static int testedStory(const uint8_t* c) {
    int v = immediate(c + 1);
    if (v < 0) return -1;
    return c[0] == 0x86 ? v : c[0] == 0x84 ? (v > 0 ? v - 1 : 0) : v + 1;
}

/* The story of the battles a field's scripts start with config k: what the
 * nearest progress test before the battle op in its routine implies, else
 * the progress the flow after it sets next (op 87) less one; the lowest over
 * its battle ops, -1 none. */
static int routineStart(const XgFieldScript* fs, int pc) {
    int start = 0;
    for (int a = 0; a < fs->actors; a++) {
        for (int r = 0; r < 32; r++) {
            int e = fs->routines[a][r];
            if (e <= pc && e > start && e < fs->codeSize) start = e;
        }
    }
    return start;
}

static int battleStory(const XgFieldScript* fs, const uint8_t* marks, int config, int* music) {
    int best = -1;
    *music = -1;
    for (int pc = 0; pc < fs->codeSize; pc++) {
        if (!marks[pc]) continue;
        const uint8_t* c = fs->code + pc;
        int k = -1;
        if (c[0] == 0x71 && marks[pc] >= 3) {
            k = immediate(c + 1);
        } else if (c[0] == 0xFE && c[1] == 0x84 && marks[pc] >= 4) {
            k = immediate(c + 2);
        }
        if (k != config) continue;
        int start = routineStart(fs, pc);
        int story = -1;
        for (int q = start; q < pc; q++) {
            if (!marks[q] || marks[q] < 3) continue;
            if (fs->code[q] >= 0x84 && fs->code[q] <= 0x86) {
                int t = testedStory(fs->code + q);
                if (t >= 0) story = t; /* the routine's own, the nearest */
            } else if ((fs->code[q] == 0x72 || fs->code[q] == 0x75) && immediate(fs->code + q + 1) >= 0 &&
                       immediate(fs->code + q + 1) < 0xF0 && *music < 0) {
                /* OP_72 / OP_PLAY_MUSIC: the theme the routine plays before the battle (its last one) */
                int m = immediate(fs->code + q + 1);
                for (int r = q + marks[q]; r < pc; r += marks[r] ? marks[r] : 1) {
                    if (marks[r] >= 3 && (fs->code[r] == 0x72 || fs->code[r] == 0x75) && immediate(fs->code + r + 1) >= 0 &&
                        immediate(fs->code + r + 1) < 0xF0) { /* 0xFF: the music stops */
                        m = immediate(fs->code + r + 1);
                    }
                }
                *music = m;
            }
        }
        for (int q = pc + marks[pc], steps = 0; story < 0 && q < fs->codeSize && marks[q] && steps < 400; steps++) {
            const uint8_t* d = fs->code + q;
            if (d[0] == 0x87 && marks[q] >= 3 && immediate(d + 1) >= 0) {
                story = immediate(d + 1) > 0 ? immediate(d + 1) - 1 : 0;
            } else if (d[0] == 0x01 && marks[q] >= 3) {
                q = d[1] | d[2] << 8;
                continue;
            } else if (xgFieldEndsFlow(d[0])) {
                break;
            }
            q += marks[q];
        }
        if (story >= 0 && (best < 0 || story < best)) best = story;
    }
    return best;
}

#define GROW(arr, count, cap)                                                                                    \
    ((count) < (cap) || ((cap) = (cap) ? (cap) * 2 : 64,                                                         \
                         (arr) = realloc((arr), sizeof(*(arr)) * (size_t)(cap))) != NULL)

static void addRandom(XgGauntletScan* s, const XgBeastPlace* p) {
    for (int i = 0; i < 8; i++) {
        int t = p->types[i] & 0x7F;
        if (t == 0x7F) continue;
        if (!GROW(s->random, s->randomCount, s->randomCap)) return;
        s->random[s->randomCount++] = (uint64_t)p->disc << 16 | (uint64_t)p->set << 8 | (uint64_t)t;
    }
}

XgGauntletScan* xgGauntletScanBegin(void) {
    XgGauntletScan* s = calloc(1, sizeof *s);
    if (s == NULL) return NULL;
    for (int i = 0; i < 2; i++) {
        const char* p = cdSwapPath(i);
        XgDisc* d = p != NULL && *p ? xgDiscOpen(p, NULL) : NULL;
        int n = d != NULL ? xgDiscNumber(d) : 0;
        if (n >= 1 && n <= 2 && s->discs[n - 1] == NULL) {
            s->discs[n - 1] = d;
        } else {
            xgDiscClose(d);
        }
    }
    if (s->discs[0] == NULL && s->discs[1] == NULL) {
        free(s);
        return NULL;
    }
    for (int d = 0; d < 2; d++) {
        for (int f = 0; f < FIELDS; f++) s->fieldStory[d][f] = -1;
    }
    return s;
}

int xgGauntletScanDiscs(const XgGauntletScan* s) {
    return (s->discs[0] != NULL) | (s->discs[1] != NULL) << 1;
}

double xgGauntletScanProgress(const XgGauntletScan* s) {
    double per = FIELDS + WORLDS + 1;
    double at = s->disc * per + s->field + s->world + s->beasts;
    return s->done ? 1.0 : at / (2 * per);
}

int xgGauntletScanStep(XgGauntletScan* s, int budget) {
    XgBeastPlace buf[PLACES];
    while (!s->done && budget-- > 0) {
        if (s->disc >= 2) {
            s->done = 1;
            break;
        }
        XgDisc* d = s->discs[s->disc];
        if (d == NULL) {
            s->disc++;
            continue;
        }
        if (s->field < FIELDS) {
            int field = s->field++, story = -1;
            int n = debugField(field) ? 0 : xgBeastFieldPlaces(d, field, buf, PLACES, &story);
            s->fieldStory[s->disc][field] = story;
            unsigned char* table = NULL;
            XgFieldScript fs;
            uint8_t* marks = NULL;
            int haveScript = 0;
            for (int i = 0; i < n; i++) {
                if (buf[i].kind != XG_PLACE_FIELD_SCRIPTED) {
                    addRandom(s, &buf[i]);
                    continue;
                }
                if (table == NULL) {
                    table = fieldTable(d, field);
                    if (table == NULL || !GROW(s->tables, s->tableCount, s->tableCap)) {
                        free(table);
                        table = NULL;
                        break;
                    }
                    s->tables[s->tableCount++] = table;
                }
                if (!GROW(s->scripted, s->scriptedCount, s->scriptedCap)) break;
                Scripted* sc = &s->scripted[s->scriptedCount++];
                sc->disc = buf[i].disc;
                sc->field = field;
                sc->config = buf[i].config;
                sc->table = table;
                memcpy(sc->cfg, table + 0x20 * buf[i].config, sizeof sc->cfg);
                if (!haveScript) {
                    haveScript = xgFieldScriptRead(d, field, &fs) ? 1 : -1;
                    marks = haveScript > 0 ? xgFieldScriptWalk(&fs) : NULL;
                }
                sc->music = -1;
                sc->story = marks != NULL ? battleStory(&fs, marks, sc->config, &sc->music) : -1;
            }
            free(marks);
            if (haveScript > 0) xgFieldScriptFree(&fs);
        } else if (s->world < WORLDS) {
            int n = xgBeastWorldPlaces(d, s->world++, buf, PLACES);
            for (int i = 0; i < n; i++) addRandom(s, &buf[i]);
        } else if (!s->beasts) {
            s->beasts = 1;
            int n = xgBeastsRead(d, &s->beastList, s->beastCount);
            if (n >= 0) s->beastCount = n;
        } else {
            s->disc++;
            s->field = s->world = s->beasts = 0;
        }
    }
    return s->done;
}

static const XgBeast* beastOf(const XgGauntletScan* s, int disc, int set, int type) {
    for (int i = 0; i < s->beastCount; i++) {
        const XgBeast* b = &s->beastList[i];
        if (b->disc == disc && b->set == set && b->type == type) return b;
    }
    return NULL;
}

/* The identities of the enemies of random battles (their records'). */
static void randomIdentities(XgGauntletScan* s) {
    int n = 0;
    s->identified = 1;
    for (int i = 0; i < s->randomCount; i++) {
        uint64_t k = s->random[i];
        const XgBeast* r = beastOf(s, (int)(k >> 16), (int)(k >> 8 & 0xFF), (int)(k & 0xFF));
        if (r == NULL) continue;
        int dup = 0;
        for (int j = 0; j < n && !dup; j++) dup = s->random[j] == r->identity;
        if (!dup) s->random[n++] = r->identity;
    }
    s->randomCount = n;
}

static int inRandom(const XgGauntletScan* s, const XgBeast* b) {
    for (int i = 0; i < s->randomCount; i++) {
        if (s->random[i] == b->identity) return 1;
    }
    return 0;
}

static int compareBosses(const void* a, const void* b) {
    const PcGauntletBoss* x = a;
    const PcGauntletBoss* y = b;
    int sx = x->story < 0 ? 1 << 30 : x->story, sy = y->story < 0 ? 1 << 30 : y->story;
    if (sx != sy) return sx < sy ? -1 : 1;
    if (x->disc != y->disc) return x->disc - y->disc;
    if (x->field != y->field) return x->field - y->field;
    return x->config - y->config;
}

static void storyEstimates(XgGauntletScan* s) {
    memcpy(s->ownStory, s->fieldStory, sizeof s->ownStory);
    for (int disc = 0; disc < 2; disc++) {
        for (int round = 0; round < 3; round++) {
            int next[FIELDS];
            for (int f = 0; f < FIELDS; f++) {
                next[f] = s->fieldStory[disc][f];
                if (next[f] >= 0 || debugField(f)) continue;
                for (const int* x = xgTablesFieldExits(f); x != NULL && *x >= 0; x++) {
                    int st = *x < FIELDS && !debugField(*x) ? s->fieldStory[disc][*x] : -1;
                    if (st >= 0 && (next[f] < 0 || st < next[f])) next[f] = st;
                }
            }
            /* the links both ways: a field another one exits to */
            for (int f = 0; f < FIELDS; f++) {
                if (debugField(f) || s->fieldStory[disc][f] < 0) continue;
                for (const int* x = xgTablesFieldExits(f); x != NULL && *x >= 0; x++) {
                    if (*x >= FIELDS || debugField(*x) || s->fieldStory[disc][*x] >= 0) continue;
                    int st = s->fieldStory[disc][f];
                    if (next[*x] < 0 || st < next[*x]) next[*x] = st;
                }
            }
            memcpy(s->fieldStory[disc], next, sizeof next);
        }
    }
}

int xgGauntletScanResult(XgGauntletScan* s, PcGauntletBoss** out) {
    *out = NULL;
    while (!s->done) xgGauntletScanStep(s, 1 << 20);
    if (!s->identified) { /* once */
        storyEstimates(s);
        randomIdentities(s);
    }
    PcGauntletBoss* list = calloc((size_t)(s->scriptedCount ? s->scriptedCount : 1), sizeof *list);
    int* exactOf = calloc((size_t)(s->scriptedCount ? s->scriptedCount : 1), sizeof *exactOf);
    if (list == NULL || exactOf == NULL) {
        free(list);
        free(exactOf);
        return 0;
    }
    int n = 0;
    for (int i = 0; i < s->scriptedCount; i++) {
        const Scripted* sc = &s->scripted[i];
        int set = sc->cfg[0], boss = 0, gear = (sc->cfg[1] & 0x10) != 0, named = 0;

        char name[160] = "";
        if (set == 0) continue;
        for (int k = 0; k < 8; k++) {
            int t = sc->cfg[8 + k];
            if ((t & 0x7F) == 0x7F) continue;
            gear |= (t & 0x80) != 0;
            const XgBeast* b = beastOf(s, sc->disc, set, t & 0x7F);
            if (b == NULL || b->hp == 0) continue;
            boss |= !inRandom(s, b);
            if (b->name[0] && strstr(name, b->name) == NULL && strlen(name) + strlen(b->name) + 3 < sizeof name) {
                if (named++) strcat(name, ", ");
                strcat(name, b->name);
            }
        }
        if (!boss) continue;
        /* How it is dated: 2 its battle's own routine, 1 its field's own
         * progress sets (their lowest), 0 an estimate from nearby fields or
         * its world map place. (Where other fields' scripts lead into its
         * field is no better: the first visit is rarely next to a progress
         * op, so the earliest found there is often a later visit.) */
        int disc = sc->disc - 1, tier = 0;
        int story = s->fieldStory[disc][sc->field];
        if (sc->story >= 0) {
            story = sc->story, tier = 2;
        } else if (s->ownStory[disc][sc->field] >= 0) {
            tier = 1;
        }
        if (story < 0 && xgFieldPlace(sc->field) >= 0) {
            /* the world map place the field is named after: its fields' earliest */
            for (int f = 0; f < FIELDS; f++) {
                int st = s->fieldStory[disc][f];
                if (!debugField(f) && st >= 0 && xgFieldPlace(f) == xgFieldPlace(sc->field) && (story < 0 || st < story)) {
                    story = st;
                }
            }
        }
        int exact = tier;
        int dup = -1;
        for (int k = 0; k < n && dup < 0; k++) {
            if (memcmp(list[k].table + 0x20 * list[k].config, sc->cfg, sizeof sc->cfg) == 0) dup = k;
        }
        if (dup >= 0) {
            /* its earliest place: one its own scripts date first, then the lowest estimate */
            int was = list[dup].story, wasExact = exactOf[dup];
            if (wasExact > exact || (wasExact == exact && (story < 0 || (was >= 0 && was <= story)))) continue;
        }
        exactOf[dup >= 0 ? dup : n] = exact;
        PcGauntletBoss* b = dup >= 0 ? &list[dup] : &list[n++];
        memset(b, 0, sizeof *b);
        b->disc = sc->disc;
        b->field = sc->field;
        b->config = sc->config;
        b->story = story;
        b->gear = gear;
        b->set = set;
        b->music = sc->music;
        memcpy(b->table, sc->table, PC_GAUNTLET_TABLE);
        if (name[0]) {
            snprintf(b->name, sizeof b->name, "%s", name);
        } else { /* no spelling of its names here (a JP disc's kana and kanji) */
            snprintf(b->name, sizeof b->name, "Field %d, battle %d", sc->field, sc->config);
        }
    }
    /* Event blocks: the game numbers one event's fields together; a boss
     * the scripts do not date takes the latest estimate of its block's
     * bosses (each a lower bound). Blocks checked against the scripts' own
     * dates: Aveh Castle 150-171 (the tournament, Margie, Ramsus at 70),
     * the Assault on Aveh 172-193 (Elly's unit and the Dora's flagship at
     * 90; its flagship chase and Id's fields set no progress of their own). */
    static const int blocks[][2] = {{150, 171}, {172, 193}};
    for (size_t k = 0; k < sizeof blocks / sizeof blocks[0]; k++) {
        int latest = -1;
        for (int i = 0; i < n; i++) {
            if (list[i].field >= blocks[k][0] && list[i].field <= blocks[k][1] && list[i].story > latest) {
                latest = list[i].story;
            }
        }
        for (int i = 0; i < n; i++) {
            if (exactOf[i] < 2 && list[i].field >= blocks[k][0] && list[i].field <= blocks[k][1]) list[i].story = latest;
        }
    }
    /* A boss only estimated: not before a dated boss of the same event's
     * fields (the game numbers them together, within 8 numbers); with no
     * estimate at all, the dated boss's field nearest. */
    for (int i = 0; i < n; i++) {
        if (exactOf[i] >= 1) continue;
        int near = -1;
        for (int k = 0; k < n; k++) {
            if (exactOf[k] < 1 || list[k].disc != list[i].disc) continue;
            int dk = abs(list[k].field - list[i].field);
            if (near < 0 || dk < abs(list[near].field - list[i].field) ||
                (dk == abs(list[near].field - list[i].field) && list[k].story < list[near].story)) {
                near = k;
            }
        }
        if (near < 0) continue;
        if (list[i].story < 0 || (abs(list[near].field - list[i].field) <= 8 && list[near].story > list[i].story)) {
            list[i].story = list[near].story;
        }
    }
    for (int i = 0; i < n; i++) list[i].dated = exactOf[i] == 2;
    free(exactOf);
    qsort(list, (size_t)n, sizeof *list, compareBosses);
    *out = list;
    return n;
}

void xgGauntletScanFree(XgGauntletScan* s) {
    if (s == NULL) return;
    for (int i = 0; i < 2; i++) xgDiscClose(s->discs[i]);
    for (int i = 0; i < s->tableCount; i++) free(s->tables[i]);
    free(s->tables);
    free(s->scripted);
    free(s->random);
    free(s->beastList);
    free(s);
}

int xgGauntletName(const unsigned char* text, int size, char* out, int outSize) {
    return tablesGameText(text, size, cdDiscIsJapanese() ? 'J' : 'U', out, outSize);
}
