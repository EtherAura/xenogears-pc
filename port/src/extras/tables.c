/*
 * The recovered tables (tables.h): names and playback rates. The music and
 * sound tables are in tables_sound.c. docs/ASSETS.md has the facts each one
 * rests on; everything here is read from the player's disc, decoded by the
 * game's own LZSS decoder where it is compressed.
 *
 * Names:
 *   - Story chapters: file 11/01, one Shift-JIS line per story progress value
 *     (field variable 0), which the menu's loadSaveLocationName copies into
 *     the memory card save's title.
 *   - World map places: each world map configuration's archive (24/2B+0B*c
 *     + 1, LZSS) holds a text bank (header +0x1C) and four exit groups
 *     (header +4: initial positions, then the groups); an exit is a 16-byte
 *     record (rectangle, destination field, return mode, text, type), and
 *     the world map shows the text when the player stands on it
 *     (setupWorldmapExits). Type 4 exits are messages, not places.
 *   - Fields: the place whose exit enters a field names it; other fields
 *     take the name of the nearest such field along the fields' own exits
 *     (the scripts' change-field ops), at most three exits away, and no
 *     name when places of different names are as near. Checked against a
 *     community list of field descriptions (US disc 1): about 85% of the
 *     names given agree; the misses are fields scenes reach (the
 *     Yggdrasil's rooms, cutscenes), which is why the walk stops early.
 */

#include "tables.h"

#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#include "cd/cd.h"
#include "files.h"
#include "system/gametext.h"
#include "system/hw.h"
#include "tables_int.h"

/* ---- discs and kept strings ------------------------------------------------------------------- */

static struct {
    int opened;
    char* override; /* xgTablesSetDisc */
    XgDisc* discs[2];
    int count;
    char** kept;
    int keptCount, keptCap;
} T;

uint32_t tablesRd32(const uint8_t* p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

uint16_t tablesRd16(const uint8_t* p) {
    return (uint16_t)(p[0] | p[1] << 8);
}

static void addDisc(const char* path) {
    if (path == NULL || path[0] == '\0' || T.count == 2) {
        return;
    }
    XgDisc* d = xgDiscOpen(path, NULL);
    if (d != NULL) {
        T.discs[T.count++] = d;
    }
}

static void openDiscs(void) {
    if (T.opened) {
        return;
    }
    T.opened = 1;
    if (T.override != NULL) {
        addDisc(T.override);
    } else {
        addDisc(cdSwapPath(0));
        addDisc(cdSwapPath(1));
    }
}

XgDisc* tablesDisc(int number) {
    openDiscs();
    for (int i = 0; i < T.count; i++) {
        if (xgDiscNumber(T.discs[i]) == number) {
            return T.discs[i];
        }
    }
    return T.count > 0 ? T.discs[0] : NULL;
}

int tablesRegionOf(XgDisc* d) {
    const char* r = d != NULL ? xgDiscRegion(d) : "";
    return strcmp(r, "US") == 0 ? 'U' : strcmp(r, "JP") == 0 ? 'J' : 0;
}

uint8_t* tablesRead(XgDisc* d, int dir, int file, int* size) {
    int entry = d != NULL ? xgDiscFileEntry(d, dir, file) : -1;
    if (entry < 0) {
        return NULL;
    }
    uint8_t* data = xgDiscReadEntry(d, entry, size);
    if (data != NULL && *size <= 0) {
        free(data);
        data = NULL;
    }
    return data;
}

uint8_t* tablesReadLzss(XgDisc* d, int dir, int file, int* size) {
    int raw;
    uint8_t* data = tablesRead(d, dir, file, &raw);
    if (data == NULL) {
        return NULL;
    }
    uint8_t* out = xgLzssDecompress(data, raw, size);
    free(data);
    return out;
}

const char* tablesKeep(const char* s) {
    if (s == NULL) {
        return "";
    }
    if (T.keptCount == T.keptCap) {
        int cap = T.keptCap ? T.keptCap * 2 : 256;
        char** k = realloc(T.kept, (size_t)cap * sizeof(*k));
        if (k == NULL) {
            return "";
        }
        T.kept = k;
        T.keptCap = cap;
    }
    char* copy = strdup(s);
    if (copy == NULL) {
        return "";
    }
    T.kept[T.keptCount++] = copy;
    return copy;
}

/* ---- text ---------------------------------------------------------------------------------- */

/* A text of the game as UTF-8 (system/gametext.h: every glyph of both
 * releases' fonts has its character): a new line a space, spaces collapsed
 * and trimmed; 0 when the text holds a code that is not a glyph. */
int tablesGameText(const uint8_t* t, int max, int region, char* out, int outSize) {
    char raw[512];
    out[0] = '\0';
    if (!gtTextToUtf8(region == 'J' ? "JP" : "US", t, max, raw, sizeof(raw)) ||
        strstr(raw, "\xE2\x96\xA1") != NULL) {
        return 0; /* a code that is no glyph, or the box the font prints for a missing one: no name */
    }
    if (region != 'J') {
        /* the US names are the font's narrow Latin set: other glyphs there spell the JP
         * leftovers some US tables keep (in the US font's codes, nonsense) */
        for (const char* r = raw; *r != '\0'; r++) {
            if ((unsigned char)*r >= 0x80) {
                return 0;
            }
        }
    }
    int n = 0, cut = 0;
    for (const char* r = raw; *r != '\0'; r++) {
        char ch = *r;
        if ((unsigned char)r[0] == 0xE3 && (unsigned char)r[1] == 0x80 && (unsigned char)r[2] == 0x80) {
            ch = ' '; /* the ideographic space (a JP blank name: no name) */
            r += 2;
        }
        if (n + 1 >= outSize) {
            cut = 1;
            break;
        }
        if (ch == ' ' && (n == 0 || out[n - 1] == ' ')) {
            continue;
        }
        out[n++] = ch;
    }
    if (cut) {
        /* cut in the middle of a character: no part of it */
        int lead = n;
        while (lead > 0 && ((unsigned char)out[lead - 1] & 0xC0) == 0x80) {
            lead--;
        }
        if (lead > 0 && ((unsigned char)out[lead - 1] & 0xC0) == 0xC0) {
            unsigned char b = (unsigned char)out[lead - 1];
            int want = b >= 0xF0 ? 4 : b >= 0xE0 ? 3 : 2;
            if (n - (lead - 1) < want) {
                n = lead - 1;
            }
        }
    }
    while (n > 0 && out[n - 1] == ' ') {
        n--;
    }
    out[n] = '\0';
    return 1;
}

/* Full-width forms (U+FF01-U+FF5E) and the ideographic space as ASCII, and
 * the two typographic signs the US lines use; then trailing spaces cut. */
static void foldWide(char* s) {
    unsigned char* r = (unsigned char*)s;
    char* w = s;
    while (*r != 0) {
        unsigned cp = 0;
        int len = 1;
        if (r[0] == 0xE3 && r[1] == 0x80 && r[2] == 0x80) {
            cp = ' ';
            len = 3;
        } else if (r[0] == 0xEF && (r[1] == 0xBC || r[1] == 0xBD) && r[2] >= 0x80 && r[2] <= 0xBF) {
            unsigned u = 0xF000 | (unsigned)(r[1] & 0x3F) << 6 | (unsigned)(r[2] & 0x3F);
            if (u >= 0xFF01 && u <= 0xFF5E) {
                cp = u - 0xFF01 + 0x21;
                len = 3;
            }
        } else if (r[0] == 0xE2 && r[1] == 0x80 && r[2] == 0x99) {
            cp = '\''; /* U+2019 */
            len = 3;
        } else if (r[0] == 0xE2 && r[1] == 0x88 && r[2] == 0x92) {
            cp = '-'; /* U+2212 */
            len = 3;
        }
        if (cp != 0) {
            *w++ = (char)cp;
            r += len;
        } else {
            *w++ = (char)*r++;
        }
    }
    *w = '\0';
    while (w > s && (w[-1] == ' ' || w[-1] == '\r' || w[-1] == '\n')) {
        *--w = '\0';
    }
}

char* tablesSjisToUtf8(const uint8_t* s, int n) {
    char* out = NULL;
#ifdef _WIN32
    int wide = MultiByteToWideChar(932, 0, (const char*)s, n, NULL, 0);
    if (wide > 0) {
        WCHAR* w = malloc((size_t)wide * sizeof(WCHAR));
        if (w != NULL && MultiByteToWideChar(932, 0, (const char*)s, n, w, wide) == wide) {
            int bytes = WideCharToMultiByte(CP_UTF8, 0, w, wide, NULL, 0, NULL, NULL);
            out = malloc((size_t)bytes + 1);
            if (out != NULL) {
                WideCharToMultiByte(CP_UTF8, 0, w, wide, out, bytes, NULL, NULL);
                out[bytes] = '\0';
            }
        }
        free(w);
    }
#else
    char* conv = SDL_iconv_string("UTF-8", "SHIFT-JIS", (const char*)s, (size_t)n);
    if (conv != NULL) {
        out = strdup(conv);
        SDL_free(conv);
    }
#endif
    if (out == NULL) {
        /* No converter: the full-width Latin row by its code arithmetic
         * (0x8140 space, 0x824F digits, 0x8260 capitals, 0x8281 small
         * letters), the rest as '?'. */
        out = malloc((size_t)n + 1);
        if (out == NULL) {
            return NULL;
        }
        int k = 0;
        for (int i = 0; i < n; i++) {
            if (s[i] < 0x80) {
                out[k++] = (char)s[i];
                continue;
            }
            unsigned c = i + 1 < n ? (unsigned)(s[i] << 8 | s[i + 1]) : 0;
            i++;
            out[k++] = c == 0x8140                    ? ' '
                       : c >= 0x824F && c <= 0x8258   ? (char)('0' + c - 0x824F)
                       : c >= 0x8260 && c <= 0x8279   ? (char)('A' + c - 0x8260)
                       : c >= 0x8281 && c <= 0x829A   ? (char)('a' + c - 0x8281)
                       : c == 0x8166                  ? '\''
                       : c == 0x8144                  ? '.'
                       : c == 0x8149                  ? '!'
                       : c == 0x817C || c == 0x815D   ? '-'
                                                      : '?';
        }
        out[k] = '\0';
    }
    foldWide(out);
    return out;
}

/* ---- story chapters (11/01) ------------------------------------------------------------------- */

static struct {
    int loaded;
    int count;
    const char** lines;
    int chapters;
    int* chapterStart;
} S;

static void loadStory(void) {
    if (S.loaded) {
        return;
    }
    S.loaded = 1;
    XgDisc* d = tablesDisc(1);
    int size;
    uint8_t* f = tablesRead(d, 0x11, 1, &size);
    if (f == NULL) {
        return;
    }
    /* loadSaveLocationName: lines end at '\n'; a byte from 0x80 starts a
     * two-byte character; the name is the line's first 30 bytes. */
    int cap = 0;
    for (int i = 0; i < size;) {
        int start = i;
        while (i < size && f[i] != '\n') {
            i += f[i] >= 0x80 ? 2 : 1;
        }
        int end = i < size ? i : size;
        i++;
        if (end > start && f[end - 1] == '\r') {
            end--;
        }
        if (end - start > 30) {
            end = start + 30;
        }
        if (end <= start && i >= size) {
            break;
        }
        if (S.count == cap) {
            cap = cap ? cap * 2 : 512;
            const char** l = realloc(S.lines, (size_t)cap * sizeof(*l));
            if (l == NULL) {
                break;
            }
            S.lines = l;
        }
        char* u = tablesSjisToUtf8(f + start, end - start);
        S.lines[S.count++] = tablesKeep(u != NULL ? u : "");
        free(u);
    }
    free(f);
    S.chapterStart = malloc((size_t)(S.count > 0 ? S.count : 1) * sizeof(int));
    for (int i = 0; S.chapterStart != NULL && i < S.count; i++) {
        if (i == 0 || strcmp(S.lines[i], S.lines[i - 1]) != 0) {
            S.chapterStart[S.chapters++] = i;
        }
    }
}

int xgStoryCount(void) {
    loadStory();
    return S.count;
}

const char* xgStoryName(int progress) {
    loadStory();
    return progress >= 0 && progress < S.count ? S.lines[progress] : "";
}

int xgStoryChapterCount(void) {
    loadStory();
    return S.chapters;
}

const char* xgStoryChapterName(int chapter) {
    loadStory();
    return chapter >= 0 && chapter < S.chapters ? S.lines[S.chapterStart[chapter]] : "";
}

int xgStoryChapterStart(int chapter) {
    loadStory();
    return chapter >= 0 && chapter < S.chapters ? S.chapterStart[chapter] : -1;
}

/* ---- world map places --------------------------------------------------------------------- */

#define WORLD_DIR 0x24
#define WORLD_CONFIGS 17
#define WORLD_FIRST 0x2B
#define WORLD_STEP 0x0B
#define MAX_PLACES 256

typedef struct {
    const char* name;
    int field;
    int config; /* the first configuration that has it */
} Place;

static struct {
    int loaded;
    int count;
    Place places[MAX_PLACES];
} W;

/* A place's name has two letters at least: some configurations' banks
 * hold a blank or a stray letter where an exit leads nowhere named (text 8:
 * " ", or "s" from configuration 9 on). */
static int isName(const char* s) {
    int letters = 0;
    for (; *s != '\0'; s++) {
        /* a Latin letter, or a character past ASCII (its first byte: kana, kanji) */
        letters += (*s >= 'A' && *s <= 'Z') || (*s >= 'a' && *s <= 'z') || ((unsigned char)*s & 0xC0) == 0xC0;
    }
    return letters >= 2;
}

/* One configuration's archive: its exits' places. */
static void readWorldConfig(XgDisc* d, int config, int region) {
    int size;
    uint8_t* a = tablesReadLzss(d, WORLD_DIR, WORLD_FIRST + WORLD_STEP * config + 1, &size);
    if (a == NULL || size < 0x30) {
        free(a);
        return;
    }
    uint32_t locations = tablesRd32(a + 4);
    uint32_t text = tablesRd32(a + 0x1C);
    if (locations + 8 > (uint32_t)size || text + 8 > (uint32_t)size) {
        free(a);
        return;
    }
    /* The text bank: a u16 count less one, then u16 offsets from its start
     * (getDialogParamPointer reads entry i at +4 + 2i). */
    int texts = tablesRd16(a + text) + 1;
    const uint8_t* loc = a + locations;
    uint32_t groups = tablesRd32(loc + 4);
    for (int g = 0; g < 4 && locations + groups + 16 <= (uint32_t)size; g++) {
        uint32_t at = locations + tablesRd32(loc + groups + 4 * g);
        for (; at + 16 <= (uint32_t)size; at += 16) {
            int16_t field = (int16_t)tablesRd16(a + at + 8);
            int textIndex = (int16_t)tablesRd16(a + at + 0xC);
            int type = (int16_t)tablesRd16(a + at + 0xE);
            if (field == -1) {
                break;
            }
            if (type == 4 || textIndex < 0 || textIndex >= texts || text + 4 + 2 * textIndex + 2 > (uint32_t)size) {
                continue;
            }
            uint32_t t = text + tablesRd16(a + text + 4 + 2 * textIndex);
            char name[64];
            if (t >= (uint32_t)size || !tablesGameText(a + t, size - (int)t, region, name, sizeof(name)) ||
                !isName(name)) {
                continue;
            }
            int known = 0;
            for (int i = 0; i < W.count; i++) {
                if (W.places[i].field == field && strcmp(W.places[i].name, name) == 0) {
                    known = 1;
                    break;
                }
            }
            if (!known && W.count < MAX_PLACES) {
                W.places[W.count].name = tablesKeep(name);
                W.places[W.count].field = field;
                W.places[W.count].config = config;
                W.count++;
            }
        }
    }
    free(a);
}

static void loadWorld(void) {
    if (W.loaded) {
        return;
    }
    W.loaded = 1;
    XgDisc* d = tablesDisc(1);
    int region = tablesRegionOf(d);
    for (int c = 0; d != NULL && c < WORLD_CONFIGS; c++) {
        readWorldConfig(d, c, region);
    }
}

int xgWorldPlaceCount(void) {
    loadWorld();
    return W.count;
}

const char* xgWorldPlaceName(int place) {
    loadWorld();
    return place >= 0 && place < W.count ? W.places[place].name : "";
}

int xgWorldPlaceField(int place) {
    loadWorld();
    return place >= 0 && place < W.count ? W.places[place].field : -1;
}

/* ---- fields ------------------------------------------------------------------------------------ */

#define FIELD_DIR 4
#define FIELD_FIRST 0xB8
#define FIELD_SLOTS ((0x6BA - FIELD_FIRST) / 2) /* the Gear bundles start at 04/6BA */
#define SCRIPT_SECTION 5
/* Field 0 is the developers' room: its menus jump to every field (its own
 * texts: "dokodemo door", "Jump to planner's map"), and the planners' maps
 * (720-729) it leads to do the same; names never pass through it. */
#define DEBUG_ROOM 0
#define HUB_EXITS 16
#define MAX_HOPS 3

static struct {
    int loaded;
    int count;
    int* place;  /* per field: the place naming it, -1 */
    int* hops;   /* per field: exits from that place's field */
    int** exits; /* per field: the fields its script changes to, -1 ended */
} F;

/* A field archive's header (docs/ASSETS.md "Field archive"): nine section
 * offsets at 0x130, in order, inside the file. */
static int fieldHeaderOk(const uint8_t* h, int size) {
    uint32_t prev = 0x154;
    for (int i = 0; i < 9; i++) {
        uint32_t o = tablesRd32(h + 0x130 + 4 * i);
        if (o < prev || o > (uint32_t)size) {
            return 0;
        }
        prev = o;
    }
    return 1;
}

/* The fields a script's bytecode changes to: its change-field ops with an
 * immediate field (bit 15) below `fields` and an immediate entry point
 * (field variable 2) below 0x100:
 *   0x98 field, entry          OP_CHANGE_FIELD_WHEN_READY2
 *   0x12 field, entry, ...     OP_CUSTOM_FIELD_TRANSITION
 *   0x47 b, field, entry       OP_WALK_AND_CHANGE_FIELD
 *   FE CF field, entry         (the change that opens a menu)
 *   FE 84 b, b, b, b, field, entry   (battle, then the field)
 * The bytecode is scanned, not walked, so the immediate bits guard it. */
static int addExit(int* list, int n, int max, const uint8_t* p, int fields) {
    uint16_t f = tablesRd16(p);
    uint16_t e = tablesRd16(p + 2);
    if (!(f & 0x8000) || !(e & 0x8000) || (f & 0x7FFF) >= fields || (e & 0x7FFF) >= 0x100) {
        return n;
    }
    for (int i = 0; i < n; i++) {
        if (list[i] == (f & 0x7FFF)) {
            return n;
        }
    }
    if (n < max) {
        list[n++] = f & 0x7FFF;
    }
    return n;
}

static int* scanExits(const uint8_t* code, int size, int fields) {
    int list[64];
    int n = 0;
    for (int i = 0; i + 6 <= size; i++) {
        switch (code[i]) {
        case 0x98:
        case 0x12:
            n = addExit(list, n, 64, code + i + 1, fields);
            break;
        case 0x47:
            n = addExit(list, n, 64, code + i + 2, fields);
            break;
        case 0xFE:
            if (code[i + 1] == 0xCF) {
                n = addExit(list, n, 64, code + i + 2, fields);
            } else if (code[i + 1] == 0x84 && i + 10 <= size) {
                n = addExit(list, n, 64, code + i + 6, fields);
            }
            break;
        }
    }
    int* out = malloc((size_t)(n + 1) * sizeof(int));
    if (out != NULL) {
        memcpy(out, list, (size_t)n * sizeof(int));
        out[n] = -1;
    }
    return out;
}

static void loadFields(void) {
    if (F.loaded) {
        return;
    }
    F.loaded = 1;
    XgDisc* d = tablesDisc(1);
    if (d == NULL) {
        return;
    }
    Uint64 start = SDL_GetTicks();
    /* The archives: the slots up to the Gear bundles that hold one. */
    uint8_t* sections[FIELD_SLOTS] = {0};
    int sizes[FIELD_SLOTS] = {0};
    for (int n = 0; n < FIELD_SLOTS; n++) {
        int entry = xgDiscFileEntry(d, FIELD_DIR, FIELD_FIRST + 2 * n);
        XgEntry e = entry >= 0 ? xgDiscEntry(d, entry) : (XgEntry){0, 0};
        if (e.size < 0x200) {
            continue;
        }
        uint8_t* head = xgDiscReadSectors(d, e.lba, 1);
        if (head == NULL) {
            continue;
        }
        if (fieldHeaderOk(head, e.size)) {
            /* Section 5 (the scripts), up to section 6. */
            uint32_t from = tablesRd32(head + 0x130 + 4 * SCRIPT_SECTION);
            uint32_t to = tablesRd32(head + 0x130 + 4 * (SCRIPT_SECTION + 1));
            int first = (int)(from / 2048);
            int last = (int)((to + 2047) / 2048);
            uint8_t* raw = to > from ? xgDiscReadSectors(d, e.lba + first, last - first) : NULL;
            if (raw != NULL) {
                sections[n] = xgLzssDecompress(raw + (from - (uint32_t)first * 2048), (int)(to - from), &sizes[n]);
                free(raw);
            }
            F.count = n + 1;
        }
        free(head);
    }
    F.place = malloc((size_t)(F.count + 1) * sizeof(int));
    F.hops = malloc((size_t)(F.count + 1) * sizeof(int));
    F.exits = calloc((size_t)F.count + 1, sizeof(int*));
    int* queue = malloc((size_t)(F.count + 1) * sizeof(int));
    if (F.place == NULL || F.hops == NULL || F.exits == NULL || queue == NULL) {
        F.count = 0;
    }
    for (int n = 0; n < F.count; n++) {
        F.place[n] = -1;
        F.hops[n] = -1;
        /* Section 5: 0x80 bytes of variable signs, a u32 row count, rows of
         * 32 u16 routine offsets, then the bytecode. */
        const uint8_t* s = sections[n];
        if (s != NULL && sizes[n] > 0x84) {
            uint32_t rows = tablesRd32(s + 0x80);
            uint32_t code = 0x84 + rows * 64;
            if (rows < 1024 && code < (uint32_t)sizes[n]) {
                F.exits[n] = scanExits(s + code, sizes[n] - (int)code, F.count);
            }
        }
    }
    for (int n = 0; n < FIELD_SLOTS; n++) {
        free(sections[n]);
    }
    /* Debug fields pass no names on: the developers' room, and the fields
     * whose scripts lead to more than HUB_EXITS others (its "planner's
     * maps", 720-729 on the US disc). */
    char* debug = calloc((size_t)F.count + 1, 1);
    int* hops = malloc((size_t)(F.count + 1) * sizeof(int));
    int* ambiguous = calloc((size_t)F.count + 1, sizeof(int));
    if (debug == NULL || hops == NULL || ambiguous == NULL) {
        F.count = 0;
    }
    for (int n = 0; n < F.count; n++) {
        int exits = 0;
        for (int* e = F.exits[n]; e != NULL && *e >= 0; e++) {
            exits++;
        }
        debug[n] = n == DEBUG_ROOM || exits > HUB_EXITS;
    }
    /* From each place's field, a breadth-first walk up to MAX_HOPS exits:
     * a field takes the name of the nearest place, and none when places of
     * other names are as near (a ship or a scene many places lead to). */
    loadWorld();
    for (int p = 0; F.count > 0 && p < W.count; p++) {
        int f0 = W.places[p].field;
        if (f0 < 0 || f0 >= F.count || debug[f0]) {
            continue;
        }
        for (int n = 0; n < F.count; n++) {
            hops[n] = -1;
        }
        int head = 0, tail = 0;
        hops[f0] = 0;
        queue[tail++] = f0;
        while (head < tail) {
            int f = queue[head++];
            if (F.hops[f] < 0 || hops[f] < F.hops[f]) {
                F.hops[f] = hops[f];
                F.place[f] = p;
                ambiguous[f] = 0;
            } else if (hops[f] == F.hops[f] && strcmp(W.places[F.place[f]].name, W.places[p].name) != 0) {
                ambiguous[f] = 1;
            }
            if (debug[f] || hops[f] == MAX_HOPS) {
                continue;
            }
            for (int* e = F.exits[f]; e != NULL && *e >= 0; e++) {
                if (hops[*e] < 0) {
                    hops[*e] = hops[f] + 1;
                    queue[tail++] = *e;
                }
            }
        }
    }
    int named = 0;
    for (int n = 0; n < F.count; n++) {
        if (debug[n] || ambiguous[n]) {
            F.place[n] = -1;
            F.hops[n] = -1;
        }
        named += F.place[n] >= 0;
    }
    free(debug);
    free(hops);
    free(ambiguous);
    free(queue);
    fprintf(stderr, "tables: %d fields, %d named from %d world map places (%u ms)\n", F.count, named, W.count,
            (unsigned)(SDL_GetTicks() - start));
}

int xgFieldCount(void) {
    loadFields();
    return F.count;
}

const char* xgFieldName(int field) {
    loadFields();
    return field >= 0 && field < F.count && F.place[field] >= 0 ? W.places[F.place[field]].name : "";
}

int xgFieldPlace(int field) {
    loadFields();
    return field >= 0 && field < F.count ? F.place[field] : -1;
}

/* For the Tables extra: a field's exits and its distance from its place. */
const int* xgTablesFieldExits(int field) {
    loadFields();
    static const int none[1] = {-1};
    return field >= 0 && field < F.count && F.exits[field] != NULL ? F.exits[field] : none;
}

int xgTablesFieldHops(int field) {
    loadFields();
    return field >= 0 && field < F.count ? F.hops[field] : -1;
}

/* ---- the disc and starting over ------------------------------------------------------------------ */

static void resetAll(void) {
    for (int i = 0; i < T.count; i++) {
        xgDiscClose(T.discs[i]);
    }
    T.count = 0;
    T.opened = 0;
    free(S.lines);
    free(S.chapterStart);
    memset(&S, 0, sizeof(S));
    memset(&W, 0, sizeof(W));
    for (int n = 0; n < F.count; n++) {
        free(F.exits[n]);
    }
    free(F.exits);
    free(F.place);
    free(F.hops);
    memset(&F, 0, sizeof(F));
    tablesSoundReset();
    tablesBattleReset();
    for (int i = 0; i < T.keptCount; i++) {
        free(T.kept[i]);
    }
    T.keptCount = 0;
}

int xgTablesSetDisc(const char* path) {
    if (path != NULL) {
        XgDisc* d = xgDiscOpen(path, NULL);
        if (d == NULL) {
            return 0;
        }
        xgDiscClose(d);
    }
    resetAll();
    free(T.override);
    T.override = path != NULL ? strdup(path) : NULL;
    return 1;
}

XgDisc* xgTablesDisc(void) {
    return tablesDisc(1);
}

const char* xgTablesRegion(void) {
    XgDisc* d = tablesDisc(1);
    int r = tablesRegionOf(d);
    return r == 'U' ? "US" : r == 'J' ? "JP" : "";
}

/* ---- playback rates ------------------------------------------------------------------------- */

/* Progressive NTSC animation uses the runtime clock's timing, including
 * its half video clock per line. */
double xgVsyncRate(void) {
    return HW_VIDEO_HZ_NTSC / (HW_LINE_CLOCKS_NTSC * HW_FIELD_LINES_NTSC);
}

/*
 * What the game's code does (docs/ASSETS.md "Playback rates"):
 *   - field (updateAndRenderField): a frame waits for 2 + dialogWindowFlag2
 *     vblanks after its start (op D6 sets the flag: 0, 1 or 2); each frame
 *     runs each sprite's wait countdown fieldDrawEnvsInitialized + 1 times
 *     (OP_INIT_ENTITY_SCRIPT_sub0Sub9), and initFieldDrawEnvs sets that to
 *     1; renderMechas adds 1 + 1 to a half-step counter and runs a Gear
 *     animation step per 2, so one step a frame;
 *   - world map: VSync(2) a frame, the same sprite and Gear code (its init
 *     sets fieldDrawEnvsInitialized to 1, renderMechas gets 1);
 *   - battle (battleRender): VSync(0), one vblank at least; the vblanks the
 *     frame took beyond it (capped at 4) run the sprites' callbacks that
 *     many more times, and renderMechasBattle adds 1 + them to its
 *     half-step counter: sprites tick once per vblank and Gears step once
 *     per two, whatever the frame rate.
 */
int xgAnimRates(int mode, int slow, XgRates* out) {
    double vsync = xgVsyncRate();
    switch (mode) {
    case 1:
    case 3:
        if (mode != 1 || slow < 0 || slow > 2) {
            slow = 0;
        }
        out->vsyncsPerFrame = 2 + slow;
        out->frameRate = vsync / out->vsyncsPerFrame;
        out->catchUp = 0;
        out->spriteTickRate = out->frameRate * 2;
        out->gearStepRate = out->frameRate;
        return 1;
    case 2:
        out->vsyncsPerFrame = 1;
        out->frameRate = vsync;
        out->catchUp = 1;
        out->spriteTickRate = vsync;
        out->gearStepRate = vsync / 2;
        return 1;
    default:
        return 0;
    }
}

double xgSpriteTickRate(int mode) {
    XgRates r;
    return xgAnimRates(mode, 0, &r) ? r.spriteTickRate : 0.0;
}

double xgGearStepRate(int mode) {
    XgRates r;
    return xgAnimRates(mode, 0, &r) ? r.gearStepRate : 0.0;
}
