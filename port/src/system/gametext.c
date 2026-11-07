/*
 * The game's text from another source (gametext.h): the settings, the
 * sources' images, the lookup src/main/dialog.c makes (include/
 * pc_gametext.h), the fonts and the glyphs a source brings.
 */

#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common.h"
#include "main/dialog.h"
#include "main/game_state.h"
#include "pc_gametext.h"
#include "pc_glyph.h"

#include "cd/cd.h"
#include "cd/regional.h"
#include "cd/verify.h"
#include "extras/gallery.h"
#include "gametext.h"
#include "savestate.h"
#include "settings.h"

/* The game's: the current field's dialogue (field.c f_8006FDEC.c) and map. */
extern u8* rawFieldDialogBundle;
extern s32 fieldMapNumber;

/* The extras' field script walker (gallery_refs.c), when the build has it. */
uint8_t* xgFieldScriptWalk(const XgFieldScript* s) __attribute__((weak));

int pcGameTextOn;

#ifdef VERSION_JP
#define OTHER_REGION "US"
#else
#define OTHER_REGION "JP"
#endif

static uint16_t rd16(const uint8_t* p) { return (uint16_t)(p[0] | p[1] << 8); }

static const char* const categoryKeys[GT_CATEGORIES] = {"dialogue", "battle", "menus", "names"};

/* ---- settings ---------------------------------------------------------------------------- */

static struct {
    int init;
    int source[GT_CATEGORIES];
    char pack[GT_CATEGORIES][64];
    char status[GT_CATEGORIES][300];
    int given[GT_CATEGORIES], kept[GT_CATEGORIES];
    int pictures[GT_CATEGORIES]; /* text_<category>_pictures: its pictures with text too */
    unsigned generation;         /* any change of a source */
} cfg;

static void updateOn(void) {
    int on = 0;
    for (int c = 0; c < GT_CATEGORIES; c++) on |= cfg.source[c] != GT_SOURCE_DISC;
    pcGameTextOn = on;
}

static void parseSource(int c, const char* v) {
    cfg.source[c] = GT_SOURCE_DISC;
    cfg.pack[c][0] = '\0';
    if (v == NULL || v[0] == '\0' || !strcmp(v, "disc")) return;
    if (!strcmp(v, "other")) cfg.source[c] = GT_SOURCE_OTHER;
    else if (!strcmp(v, "image")) cfg.source[c] = GT_SOURCE_IMAGE;
    else if (!strncmp(v, "pack:", 5) && v[5] != '\0') {
        cfg.source[c] = GT_SOURCE_PACK;
        snprintf(cfg.pack[c], sizeof(cfg.pack[c]), "%s", v + 5);
    } else {
        fprintf(stderr, "gametext: text_%s=%s: no such source; the disc's text\n", categoryKeys[c], v);
    }
}

void gameTextInit(void) {
    if (cfg.init) return;
    cfg.init = 1;
    for (int c = 0; c < GT_CATEGORIES; c++) {
        char key[32], comment[200];
        snprintf(key, sizeof(key), "text_%s", categoryKeys[c]);
        snprintf(comment, sizeof(comment),
                 "%s: the game's %s text: disc (default), other (the other release's disc), pack:<id>", key,
                 categoryKeys[c]);
        settingsDescribe(key, comment);
        parseSource(c, settingsGet(key));
        if (gameTextHasPictures(c)) {
            snprintf(key, sizeof(key), "text_%s_pictures", categoryKeys[c]);
            snprintf(comment, sizeof(comment),
                     "%s: 1: the %s' pictures with text from the same source too (0: the disc's, default)", key,
                     categoryKeys[c]);
            settingsDescribe(key, comment);
            cfg.pictures[c] = settingsBool(key, 0);
        }
    }
    settingsDescribe("text_other_disc1", "text_other_disc1: the other release's Disc 1 image for its text (read-only)");
    settingsDescribe("text_other_disc2", "text_other_disc2: the other release's Disc 2 image for its text (read-only)");
    settingsDescribe("text_image1", "text_image1: a text image for Disc 1 (a translation patch on your own image; read-only)");
    settingsDescribe("text_image2", "text_image2: a text image for Disc 2 (read-only)");
    updateOn();
}

void pcGameTextInit(void) {
    gameTextInit();
    /* XG_TEXT_EXPORT=this, other or image: write that text pack now (headless runs, tests). */
    static int exported;
    const char* want = SDL_getenv("XG_TEXT_EXPORT");
    if (!exported && want != NULL && (!strcmp(want, "this") || !strcmp(want, "other") || !strcmp(want, "image"))) {
        exported = 1;
        char id[64], why[512];
        int ok = gameTextExportPack(!strcmp(want, "image") ? 2 : !strcmp(want, "other"), id, sizeof(id), why, sizeof(why));
        fprintf(stderr, "gametext: export %s: %s\n", ok ? "done" : "failed", why);
    }
}

int gameTextSource(int c) {
    gameTextInit();
    return c >= 0 && c < GT_CATEGORIES ? cfg.source[c] : GT_SOURCE_DISC;
}

const char* gameTextPack(int c) {
    gameTextInit();
    return c >= 0 && c < GT_CATEGORIES ? cfg.pack[c] : "";
}

const char* gameTextCategoryKey(int c) {
    return c >= 0 && c < GT_CATEGORIES ? categoryKeys[c] : "";
}

const char* gameTextOtherRegion(void) {
    return OTHER_REGION;
}

void gameTextSetSource(int c, int source, const char* pack) {
    gameTextInit();
    if (c < 0 || c >= GT_CATEGORIES) return;
    char key[32], value[80];
    snprintf(key, sizeof(key), "text_%s", categoryKeys[c]);
    switch (source) {
    case GT_SOURCE_OTHER: snprintf(value, sizeof(value), "other"); break;
    case GT_SOURCE_IMAGE: snprintf(value, sizeof(value), "image"); break;
    case GT_SOURCE_PACK: snprintf(value, sizeof(value), "pack:%s", pack ? pack : ""); break;
    default: snprintf(value, sizeof(value), "disc"); break;
    }
    parseSource(c, value);
    cfg.given[c] = cfg.kept[c] = 0;
    cfg.generation++;
    settingsSet(key, value);
    settingsSave();
    updateOn();
}

/* ---- pictures with text (gametext_picture.c) ---------------------------------------------- */

int gameTextHasPictures(int c) {
    return c == GT_MENUS || c == GT_BATTLE;
}

int gameTextPictures(int c) {
    gameTextInit();
    return c >= 0 && c < GT_CATEGORIES && gameTextHasPictures(c) && cfg.pictures[c];
}

void gameTextSetPictures(int c, int on) {
    gameTextInit();
    if (c < 0 || c >= GT_CATEGORIES || !gameTextHasPictures(c)) return;
    char key[40];
    snprintf(key, sizeof(key), "text_%s_pictures", categoryKeys[c]);
    cfg.pictures[c] = !!on;
    cfg.generation++;
    settingsSetInt(key, cfg.pictures[c]);
    settingsSave();
}

unsigned gtGeneration(void) {
    return cfg.generation;
}

void gameTextCounts(int c, int* given, int* kept) {
    *given = c >= 0 && c < GT_CATEGORIES ? cfg.given[c] : 0;
    *kept = c >= 0 && c < GT_CATEGORIES ? cfg.kept[c] : 0;
}

/* ---- images ----------------------------------------------------------------------------- */

static struct {
    GtImage* image[2];
    int tried[2];
    char why[2][300];
} retail, sources[2]; /* SRC_OTHER, SRC_IMAGE */

void gtForgetImage(void);

/* The sources that are images: the other release's discs, a text image (a
 * translation patch applied to the player's own image). */
enum { SRC_OTHER, SRC_IMAGE };
#define other sources[SRC_OTHER]

static int currentDisc(void) {
    int d = cdBootDiscNumber();
    return d == 1 || d == 2 ? d : 1;
}

/* The running release's disc `d`: the image in the drive or its pair. */
static GtImage* retailImage(int d) {
    if (d < 1 || d > 2) return NULL;
    if (!retail.tried[d - 1]) {
        retail.tried[d - 1] = 1;
        for (int which = 0; which < 2 && retail.image[d - 1] == NULL; which++) {
            const char* p = cdSwapPath(which);
            GtImage* im = p && p[0] ? gtImageOpen(p, retail.why[d - 1], sizeof(retail.why[d - 1])) : NULL;
            if (im != NULL && gtImageDisc(im) == d) retail.image[d - 1] = im;
            else gtImageClose(im);
        }
    }
    return retail.image[d - 1];
}

/* Images beside the game's: its folder, the folder above and the folders in
 * that (a folder per disc), as the menu finds a Japanese font. */
static int searchBeside(const char* region, int d, char* out, size_t size) {
    const char* current = cdSwapPath(0);
    if (current == NULL || current[0] == '\0') return 0;
    char dir[4096];
    snprintf(dir, sizeof(dir), "%s", current);
    char* cut = strrchr(dir, '/');
#ifdef _WIN32
    char* cut2 = strrchr(dir, '\\');
    if (cut2 > cut) cut = cut2;
#endif
    if (cut == NULL) return 0;
    *cut = '\0';
    char parent[4096];
    snprintf(parent, sizeof(parent), "%s", dir);
    cut = strrchr(parent, '/');
#ifdef _WIN32
    cut2 = strrchr(parent, '\\');
    if (cut2 > cut) cut = cut2;
#endif
    if (cut != NULL) *cut = '\0';
    const char* roots[2] = {dir, parent};
    for (int r = 0; r < 2; r++) {
        int folders = 0;
        char** sub = SDL_GlobDirectory(roots[r], NULL, 0, &folders);
        for (int k = -1; k < folders; k++) {
            char where[4096];
            if (k < 0) snprintf(where, sizeof(where), "%s", roots[r]);
            else if (snprintf(where, sizeof(where), "%s/%s", roots[r], sub[k]) >= (int)sizeof(where)) continue;
            int files = 0;
            char** list = SDL_GlobDirectory(where, "*.cue", SDL_GLOB_CASEINSENSITIVE, &files);
            for (int f = 0; f < files; f++) {
                char path[4096];
                CdImageInfo info;
                char why[64];
                if (snprintf(path, sizeof(path), "%s/%s", where, list[f]) >= (int)sizeof(path)) continue;
                if (cdVerifyImage(path, &info, why, sizeof(why)) && !strcmp(info.region, region) && info.disc == d) {
                    snprintf(out, size, "%s", path);
                    SDL_free(list);
                    SDL_free(sub);
                    return 1;
                }
            }
            SDL_free(list);
        }
        SDL_free(sub);
    }
    return 0;
}

/* The other release's disc `d`: text_other_disc<d>, the Japanese discs of
 * F1 > Audio (the US build), or one beside the game's. */
static GtImage* otherImage(int d) {
    if (d < 1 || d > 2) return NULL;
    if (!other.tried[d - 1]) {
        other.tried[d - 1] = 1;
        char key[32], path[4096] = "";
        snprintf(key, sizeof(key), "text_other_disc%d", d);
        const char* set = settingsGet(key);
        if (set != NULL && set[0] != '\0') snprintf(path, sizeof(path), "%s", set);
#ifndef VERSION_JP
        if (path[0] == '\0' && regionalSourcePath(d) != NULL) snprintf(path, sizeof(path), "%s", regionalSourcePath(d));
#endif
        GtImage* im = path[0] ? gtImageOpen(path, other.why[d - 1], sizeof(other.why[d - 1])) : NULL;
        if (im != NULL && (strcmp(gtImageRegion(im), OTHER_REGION) || gtImageDisc(im) != d)) {
            snprintf(other.why[d - 1], sizeof(other.why[d - 1]), "%s is %s Disc %d, not %s Disc %d", path,
                     gtImageRegion(im), gtImageDisc(im), OTHER_REGION, d);
            gtImageClose(im);
            im = NULL;
        }
        if (im == NULL && searchBeside(OTHER_REGION, d, path, sizeof(path))) {
            im = gtImageOpen(path, other.why[d - 1], sizeof(other.why[d - 1]));
        }
        if (im == NULL && other.why[d - 1][0] == '\0') {
            snprintf(other.why[d - 1], sizeof(other.why[d - 1]), "No %s Disc %d image (text_other_disc%d)",
                     OTHER_REGION, d, d);
        }
        if (im != NULL) fprintf(stderr, "gametext: %s Disc %d text from %s\n", OTHER_REGION, d, gtImagePath(im));
        other.image[d - 1] = im;
    }
    return other.image[d - 1];
}

/* The text image for disc `d` (text_image<d>): any Xenogears image with that
 * disc's structure, a translation patch's on the player's own image most
 * likely. It is read as the other release's is, never booted: the game, its
 * modules and its scripts stay the verified image's; only its text tables
 * (and its font, for their glyphs) are read, each text checked. */
static GtImage* textImage(int d) {
    if (d < 1 || d > 2) return NULL;
    GtImage** slot = &sources[SRC_IMAGE].image[d - 1];
    char* why = sources[SRC_IMAGE].why[d - 1];
    size_t whySize = sizeof(sources[SRC_IMAGE].why[d - 1]);
    if (!sources[SRC_IMAGE].tried[d - 1]) {
        sources[SRC_IMAGE].tried[d - 1] = 1;
        char key[32];
        snprintf(key, sizeof(key), "text_image%d", d);
        const char* path = settingsGet(key);
        GtImage* im = path && path[0] ? gtImageOpen(path, why, whySize) : NULL;
        if (im != NULL && gtImageDisc(im) != d) {
            snprintf(why, whySize, "%s is Disc %d, not Disc %d", path, gtImageDisc(im), d);
            gtImageClose(im);
            im = NULL;
        }
        if (im == NULL && why[0] == '\0') snprintf(why, whySize, "No text image for Disc %d (text_image%d)", d, d);
        if (im != NULL) fprintf(stderr, "gametext: Disc %d text image %s (%s)\n", d, gtImagePath(im), gtImageRegion(im));
        *slot = im;
    }
    return *slot;
}

static GtImage* sourceImage(int src, int d) {
    return src == SRC_IMAGE ? textImage(d) : otherImage(d);
}

int gtCurrentDisc(void) {
    return currentDisc();
}

GtImage* gtRetailImage(int d) {
    return retailImage(d);
}

GtImage* gtCategoryImage(int c, int d) {
    if (c < 0 || c >= GT_CATEGORIES) return NULL;
    if (cfg.source[c] == GT_SOURCE_OTHER) return otherImage(d);
    if (cfg.source[c] == GT_SOURCE_IMAGE) return textImage(d);
    return NULL;
}

/* F1 or a test changed text_image<d>: read it again. */
void gameTextImageChanged(void) {
    cfg.generation++;
    for (int d = 0; d < 2; d++) {
        /* the old image stays open: tables read from it may still be shown */
        sources[SRC_IMAGE].image[d] = NULL;
        sources[SRC_IMAGE].tried[d] = 0;
        sources[SRC_IMAGE].why[d][0] = '\0';
    }
    gtForgetImage();
}

/* ---- the source's fonts ------------------------------------------------------------------ */

static struct {
    uint8_t* runningData; /* the running release's 01/06, from its image */
    int runningSize, runningTried;
    GtFont running;
    struct {
        uint8_t* data; /* the source's 01/06 */
        int size, tried;
        GtFont font;
        int16_t* equiv; /* its glyphs in the running font */
    } src[2];
} fonts;

/* The font the game prints with: its own disc's (read from the image, which
 * gives its size; the game's copy in memory must be the same). */
static const GtFont* runningFont(void) {
    if (!fonts.runningTried) {
        fonts.runningTried = 1;
        GtImage* r = retailImage(currentDisc());
        fonts.runningData = r ? gtImageFont(r, &fonts.runningSize) : NULL;
        const u8* game = getFontData();
        if (fonts.runningData == NULL || game == NULL || memcmp(game, fonts.runningData, 14) ||
            !gtFontParse(fonts.runningData, fonts.runningSize, &fonts.running)) {
            fprintf(stderr, "gametext: the game's font is not its disc's: no text from elsewhere\n");
            free(fonts.runningData);
            fonts.runningData = NULL;
        }
    }
    return fonts.runningData != NULL ? &fonts.running : NULL;
}

/* A source's font, its glyphs in its bank slot (GT_BANK_OTHER or
 * GT_BANK_IMAGE) and their equivalents in the running font. */
static int sourceFontReady(int src) {
    const GtFont* to = runningFont();
    if (to == NULL) return 0;
    if (!fonts.src[src].tried) {
        fonts.src[src].tried = 1;
        GtImage* im = sourceImage(src, currentDisc());
        if (im == NULL) im = sourceImage(src, 3 - currentDisc());
        fonts.src[src].data = im ? gtImageFont(im, &fonts.src[src].size) : NULL;
        if (fonts.src[src].data == NULL || !gtFontParse(fonts.src[src].data, fonts.src[src].size, &fonts.src[src].font)) {
            free(fonts.src[src].data);
            fonts.src[src].data = NULL;
            return 0;
        }
        gtBankSetFont(src == SRC_IMAGE ? GT_BANK_IMAGE : GT_BANK_OTHER, &fonts.src[src].font);
    }
    if (fonts.src[src].data == NULL) return 0;
    if (fonts.src[src].equiv == NULL) fonts.src[src].equiv = gtFontEquivalents(&fonts.src[src].font, to);
    return fonts.src[src].equiv != NULL;
}
static int otherFontReady(void) {
    return sourceFontReady(SRC_OTHER);
}

/* ---- tables of a source: their texts in the running font --------------------------------- */

typedef struct {
    int src;       /* SRC_OTHER, SRC_IMAGE: whose font its codes are */
    uint8_t* blob; /* the source's table (owned) */
    int size;
    GtTable table;
    uint64_t id;    /* the arena's key for its entries (its source and bytes) */
    uint8_t* wh;    /* a field's window sizes (in blob), else NULL */
} SourceTable;

static int logOn = -1; /* XG_TEXT_LOG: each table asked about, each text made */

/* ---- the texts the game is given (a save state keeps them) ------------------------------- */

/* Every text given to the game is here: the game keeps pointers to them (its
 * windows' queues, its tables), so they sit at fixed addresses a save state
 * restores (system/savestate.h; gametextStateIo), in this run or the next.
 * Entries by key (where the text came from); a text that did not check out
 * is an entry of no bytes (the disc's is used). Kept for the run: a window
 * may still show a text after the source changed. */
#define ARENA_BYTES (16u << 20)
#define ARENA_TEXTS (1u << 17)
typedef struct {
    uint64_t key;
    uint32_t at, length;            /* length 0: the disc's text */
    uint8_t proportional, spacing, space, wrap;
} ArenaText;
static uint8_t arenaBytes[ARENA_BYTES];
static ArenaText arenaTexts[ARENA_TEXTS];
static uint32_t arenaUsed, arenaCount;
static uint32_t arenaHash[ARENA_TEXTS * 2]; /* index + 1 by key (rebuilt after a load) */
static int arenaFull;

static uint64_t fnv(uint64_t h, const void* p, size_t n) {
    const uint8_t* b = p;
    while (n--) h = (h ^ *b++) * UINT64_C(1099511628211);
    return h;
}
#define FNV_START UINT64_C(14695981039346656037)

static void arenaHashAdd(uint32_t i) {
    uint32_t h = (uint32_t)(arenaTexts[i].key ^ arenaTexts[i].key >> 32) & (ARENA_TEXTS * 2 - 1);
    while (arenaHash[h] != 0) h = (h + 1) & (ARENA_TEXTS * 2 - 1);
    arenaHash[h] = i + 1;
}

/* The entry for key: its text, KEPT_TEXT for one of no bytes, NULL none yet. */
#define KEPT_TEXT ((u8*)1)
static u8* arenaFind(uint64_t key) {
    uint32_t h = (uint32_t)(key ^ key >> 32) & (ARENA_TEXTS * 2 - 1);
    for (; arenaHash[h] != 0; h = (h + 1) & (ARENA_TEXTS * 2 - 1)) {
        const ArenaText* t = &arenaTexts[arenaHash[h] - 1];
        if (t->key == key) return t->length ? arenaBytes + t->at : KEPT_TEXT;
    }
    return NULL;
}

/* Add the text (n bytes; 0: the disc's): its place, KEPT_TEXT for none. */
static u8* arenaAdd(uint64_t key, const uint8_t* text, int n, const GtLayout* layout) {
    if (arenaCount >= ARENA_TEXTS || arenaUsed + (uint32_t)(n > 0 ? n : 0) > ARENA_BYTES) {
        if (!arenaFull) fprintf(stderr, "gametext: %u texts, %u bytes: no room for more (the disc's text)\n",
                                arenaCount, arenaUsed);
        arenaFull = 1;
        return KEPT_TEXT;
    }
    ArenaText* t = &arenaTexts[arenaCount];
    memset(t, 0, sizeof(*t));
    t->key = key;
    t->at = arenaUsed;
    t->length = n > 0 ? (uint32_t)n : 0;
    if (n > 0) memcpy(arenaBytes + arenaUsed, text, (size_t)n);
    arenaUsed += t->length;
    if (n > 0 && layout != NULL && layout->proportional) {
        t->proportional = 1;
        t->spacing = (uint8_t)layout->spacing;
        t->space = (uint8_t)layout->space;
        pcGameTextProportionalOn = 1;
    }
    if (n > 0 && layout != NULL && layout->windows == GT_WINDOWS_GROW) {
        t->wrap = 1;
        pcGameTextWrapOn = 1;
    }
    arenaHashAdd(arenaCount++);
    return t->length ? arenaBytes + t->at : KEPT_TEXT;
}

/* The entry holding the text at p (the game's pointer may be inside one). */
static const ArenaText* arenaAt(const u8* p) {
    if (p < arenaBytes || p >= arenaBytes + arenaUsed) return NULL;
    uint32_t off = (uint32_t)(p - arenaBytes);
    int lo = 0, hi = (int)arenaCount - 1, found = -1;
    while (lo <= hi) { /* the last entry starting at or before p (they are in order) */
        int mid = (lo + hi) / 2;
        if (arenaTexts[mid].at <= off) {
            found = mid;
            lo = mid + 1;
        } else {
            hi = mid - 1;
        }
    }
    while (found >= 0 && arenaTexts[found].length == 0) found--; /* an empty entry shares its place */
    return found >= 0 && off < arenaTexts[found].at + arenaTexts[found].length ? &arenaTexts[found] : NULL;
}

static void sourceTableSet(SourceTable* s, int src, uint8_t* blob, int size) {
    s->src = src;
    s->blob = blob;
    s->size = size;
    if (blob == NULL || !gtTableParse(blob, size, &s->table)) {
        s->table.count = 0;
        return;
    }
    s->id = fnv(fnv(FNV_START, "source", 6), &src, sizeof(src));
    s->id = fnv(s->id, blob, (size_t)size);
}

static u8* sourceText(SourceTable* s, int index, int category) {
    if (index < 0 || index >= s->table.count) {
        if (category >= 0) cfg.kept[category]++;
        return NULL;
    }
    uint64_t key = fnv(s->id, &index, sizeof(index));
    u8* text = arenaFind(key);
    if (text == NULL) {
        int length;
        const uint8_t* src = gtTableEntry(&s->table, index, &length);
        uint8_t buf[4096];
        int k = s->src;
        int n = src && sourceFontReady(k) ? gtTranscode(src, length, &fonts.src[k].font, fonts.src[k].equiv,
                                                       k == SRC_IMAGE ? GT_BANK_IMAGE : GT_BANK_OTHER,
                                                       &fonts.running, buf, sizeof(buf))
                                         : 0;
        text = arenaAdd(key, buf, n, NULL);
        if (logOn > 0) {
            fprintf(stderr, "gametext: entry %d: %d bytes ->", index, n);
            for (int k = 0; k < n && k < 48; k++) fprintf(stderr, " %02x", buf[k]);
            fprintf(stderr, "\n");
        }
    }
    if (text == KEPT_TEXT) {
        if (category >= 0) cfg.kept[category]++;
        return NULL;
    }
    if (category >= 0) cfg.given[category]++;
    return text;
}

/* ---- the fields ------------------------------------------------------------------------- */

typedef struct {
    int disc, field;
    int known;          /* 1: the retail field has dialogue (its fingerprint checks the game's table) */
    uint64_t retail;
    int state[2];          /* per source: 0 not looked at, 1 its text fits, -1 not */
    SourceTable source[2]; /* the sources' section 7 */
} FieldText;

static FieldText* fields;
static int fieldCount, fieldCap;

/* The dialogue ops' texts in the order the script reaches them (the field
 * VM: D2, D3, 03, F5 take the text at +1, D4 and FC at +2). */
static int dialogueOps(const uint8_t* section5, int size, int* out, int cap) {
    if (xgFieldScriptWalk == NULL || section5 == NULL || size < 0x84) return -1;
    uint32_t actors = section5[0x80] | section5[0x81] << 8 | section5[0x82] << 16 | (uint32_t)section5[0x83] << 24;
    uint32_t at = 0x84 + actors * 64;
    if (actors == 0 || actors > 256 || at >= (uint32_t)size) return -1;
    XgFieldScript s;
    s.actors = (int)actors;
    s.codeSize = size - (int)at;
    s.code = (uint8_t*)section5 + at;
    s.routines = malloc(sizeof(*s.routines) * actors);
    if (s.routines == NULL) return -1;
    for (uint32_t a = 0; a < actors; a++)
        for (int k = 0; k < 32; k++) s.routines[a][k] = rd16(section5 + 0x84 + a * 64 + k * 2);
    uint8_t* marks = xgFieldScriptWalk(&s);
    free(s.routines);
    if (marks == NULL) return -1;
    int n = 0;
    for (int pc = 0; pc < s.codeSize; pc++) {
        if (marks[pc] == 0) continue;
        int op = s.code[pc], arg = op == 0xD2 || op == 0xD3 || op == 0x03 || op == 0xF5 ? 1
                                   : op == 0xD4 || op == 0xFC                            ? 2
                                                                                          : 0;
        if (arg && marks[pc] >= arg + 2 && n < cap) {
            out[n++] = (op << 16) | rd16(s.code + pc + arg);
        }
    }
    free(marks);
    return n;
}

/* A source's dialogue fits this field's scripts: the same scripts, or
 * scripts that open the same texts in the same order (measured, US and JP:
 * 923 of the 935 fields with dialogue). */
int gtSameDialogue(GtImage* a, GtImage* b, int field) {
    int na, nb;
    uint8_t* sa = gtFieldSection(a, field, 5, &na);
    uint8_t* sb = gtFieldSection(b, field, 5, &nb);
    int same = sa != NULL && sb != NULL && na == nb && !memcmp(sa, sb, (size_t)na);
    if (!same && sa != NULL && sb != NULL) {
        static int oa[4096], ob[4096];
        int ca = dialogueOps(sa, na, oa, 4096), cb = dialogueOps(sb, nb, ob, 4096);
        same = ca > 0 && ca == cb && !memcmp(oa, ob, sizeof(int) * (size_t)ca);
    }
    free(sa);
    free(sb);
    return same;
}

static FieldText* fieldText(int disc, int field) {
    for (int i = 0; i < fieldCount; i++) {
        if (fields[i].disc == disc && fields[i].field == field) return &fields[i];
    }
    if (fieldCount == fieldCap) {
        int cap = fieldCap ? fieldCap * 2 : 32;
        FieldText* grown = realloc(fields, sizeof(*fields) * (size_t)cap);
        if (grown == NULL) return NULL;
        fields = grown;
        fieldCap = cap;
    }
    FieldText* f = &fields[fieldCount++];
    memset(f, 0, sizeof(*f));
    f->disc = disc;
    f->field = field;
    GtImage* r = retailImage(disc);
    int size;
    uint8_t* blob = r ? gtFieldSection(r, field, 7, &size) : NULL;
    if (blob != NULL) {
        f->retail = gtFingerprint(blob, size);
        f->known = 1;
    }
    free(blob);
    return f;
}

/* A source's text for this field, when its scripts fit this release's. */
static SourceTable* fieldSource(FieldText* f, int src) {
    if (f->state[src] == 0) {
        f->state[src] = -1;
        GtImage* r = retailImage(f->disc);
        GtImage* o = sourceImage(src, f->disc);
        int size;
        uint8_t* blob = r && o ? gtFieldSection(o, f->field, 7, &size) : NULL;
        if (blob == NULL || !gtSameDialogue(r, o, f->field)) {
            if (blob != NULL)
                fprintf(stderr, "gametext: field %d (disc %d): the disc's dialogue (the %s's scripts differ)\n",
                        f->field, f->disc, src == SRC_IMAGE ? "text image" : "other release");
            free(blob);
            return NULL;
        }
        sourceTableSet(&f->source[src], src, blob, size);
        if (f->source[src].table.count > 0) {
            f->source[src].wh = blob + 4 + 2 * f->source[src].table.count;
            f->state[src] = 1;
        }
    }
    return f->state[src] == 1 ? &f->source[src] : NULL;
}

/* The game's table is the current field's (the retail one's fingerprint). */
static FieldText* currentField(const u8* table) {
    if (logOn < 0) logOn = SDL_getenv("XG_TEXT_LOG") != NULL;
    if (table == NULL || table != rawFieldDialogBundle) return NULL;
    FieldText* f = fieldText(currentDisc(), (int)(fieldMapNumber & 0x3FFF));
    if (f == NULL || !f->known || f->retail != gtFingerprint(table, -1)) return NULL;
    return f;
}

/* ---- packs as a source ------------------------------------------------------------------ */

static struct {
    GtPack* pack;
    char id[64];
    char why[300];
    int16_t* equiv;       /* its own font's glyphs in the running font (packFontReady) */
    const GtPack* equivFor;
} packs[GT_CATEGORIES];

static GtPack* categoryPack(int c);

/* A pack's own font in the category's bank slot (GT_BANK_PACK + c) and its
 * glyphs' equivalents in the running font; 0: the pack has none. */
static int packFontReady(int c) {
    GtPack* p = categoryPack(c);
    const GtFont* own = p ? gtPackOwnFont(p, NULL) : NULL;
    const GtFont* to = runningFont();
    if (own == NULL || to == NULL) return 0;
    if (packs[c].equivFor != p) {
        free(packs[c].equiv);
        packs[c].equiv = gtFontEquivalents(own, to);
        packs[c].equivFor = p;
        gtBankSetFont(GT_BANK_PACK + c, own);
    }
    return packs[c].equiv != NULL;
}

static GtPack* categoryPack(int c) {
    if (strcmp(packs[c].id, cfg.pack[c])) {
        /* another pack: the old one's texts stay (a window may still show one) */
        packs[c].pack = NULL;
        snprintf(packs[c].id, sizeof(packs[c].id), "%s", cfg.pack[c]);
        for (int o = 0; o < GT_CATEGORIES; o++) {
            if (o != c && packs[o].pack != NULL && !strcmp(packs[o].id, cfg.pack[c])) packs[c].pack = packs[o].pack;
        }
        if (packs[c].pack == NULL) {
            packs[c].pack = gtPackLoad(cfg.pack[c], packs[c].why, sizeof(packs[c].why));
            fprintf(stderr, "gametext: pack %s: %s\n", cfg.pack[c], packs[c].why);
        } else {
            snprintf(packs[c].why, sizeof(packs[c].why), "%s", gtPackName(packs[c].pack));
        }
    }
    return packs[c].pack;
}

/* ---- the proportional print (a pack's print=proportional) -------------------------------- */

int pcGameTextProportionalOn;
int pcGameTextWrapOn;

/* The windows printing such a text: the pen, which the game's cursor (in
 * 4-pixel units) follows; a cursor the game moved (a new line, a new text)
 * puts the pen there. */
static struct {
    const void* window;
    int on, wrap, fieldWidth, wrapCursor, wrapInk, cursor, cleared, spacing, space;
    GtPen pen;
    uint32_t age;
} printing[16];
static uint32_t printingAge;

void pcGameTextBegin(const void* window, const u8* text) {
    if (!pcGameTextProportionalOn && !pcGameTextWrapOn) return;
    const ArenaText* k = arenaAt(text);
    int slot = -1;
    if (k != NULL && !k->proportional && !k->wrap) k = NULL;
    for (int i = 0; i < 16 && slot < 0; i++) {
        if (printing[i].window == window) slot = i;
    }
    if (slot < 0) {
        if (k == NULL) return;
        slot = 0;
        for (int i = 1; i < 16; i++) {
            if (printing[i].age < printing[slot].age) slot = i;
        }
        printing[slot].window = window;
        printing[slot].fieldWidth = 0;
    }
    printing[slot].on = k != NULL && k->proportional;
    printing[slot].wrap = k != NULL && k->wrap;
    printing[slot].wrapCursor = -1;
    printing[slot].wrapInk = 0;
    printing[slot].cursor = -1;
    printing[slot].age = ++printingAge;
    if (k != NULL) {
        printing[slot].spacing = k->spacing;
        printing[slot].space = k->space;
    }
}

void pcGameTextFieldWindow(const void* window, s32 width) {
    int slot = -1;
    for (int i = 0; i < 16; i++) if (printing[i].window == window) { slot = i; break; }
    if (slot < 0) {
        slot = 0;
        for (int i = 1; i < 16; i++) if (printing[i].age < printing[slot].age) slot = i;
        memset(&printing[slot], 0, sizeof(printing[slot]));
        printing[slot].window = window;
    }
    printing[slot].age = ++printingAge;
    printing[slot].fieldWidth = width > 0 ? width : 0;
}

s32 pcGameTextPrintWidth(const void* window, s32 width) {
    for (int i = 0; i < 16; i++) {
        if (printing[i].window == window && printing[i].wrap && printing[i].fieldWidth > 0)
            return SDL_min(width, printing[i].fieldWidth);
    }
    return width;
}

s32 pcGameTextPrint(const void* window, const u16* glyph, s32 units, u16* image, s32 stride, s32 odd, s16* cursorX,
                    s32 width) {
    int slot = -1;
    for (int i = 0; i < 16 && slot < 0; i++) {
        if (printing[i].window == window) slot = i;
    }
    if (slot < 0 || !printing[slot].on) return 0;
    if (*cursorX != printing[slot].cursor) {
        printing[slot].pen.px = printing[slot].pen.end = *cursorX * 4;
        printing[slot].pen.ink = 0;
        printing[slot].cleared = printing[slot].pen.px;
    }
    GtPen pen = printing[slot].pen;
    if (glyph == NULL) {
        /* the game's own glyph (a button's): it prints at the next whole unit */
        *cursorX = (s16)((SDL_max(pen.px, pen.end) + 3) / 4);
        printing[slot].cursor = -1;
        return 0;
    }
    int first, last;
    int x = gtPenGlyph(&pen, glyph, units, printing[slot].spacing, printing[slot].space, &first, &last);
    int cover = (pen.end + 3) / 4;
    if (cover > width || cover >= stride) {
        *cursorX = (s16)(width + 1);
        printing[slot].cursor = -1;
        return -1;
    }
    if (printing[slot].cleared < cover * 4) {
        /* the line image is the window's for every line: what the sprite shows is cleared */
        pcGlyphClearColumns(image, stride, odd & 1, printing[slot].cleared, cover * 4 - 1);
        gtClearColumns(image, stride, odd & 1, printing[slot].cleared, cover * 4 - 1);
        printing[slot].cleared = cover * 4;
    }
    if (x >= 0) {
        uint8_t indices[14 * 13];
        gtDrawGlyph(image, stride, odd & 1, x, glyph, first, last, indices);
        pcGlyphProportional(image, stride, x, last - first + 3, odd & 1, indices);
    }
    printing[slot].pen = pen;
    printing[slot].cursor = *cursorX = (s16)cover;
    return 1;
}

/* A pack's text as the running font's codes (cached; NULL keeps the disc's). */
static u8* packEncoded(int c, const char* text, int fieldWindow) {
    GtLayout layout;
    gtPackLayout(packs[c].pack, &layout);
    uint64_t key = fnv(FNV_START, "pack", 4);
    key = fnv(key, packs[c].id, strlen(packs[c].id) + 1);
    key = fnv(key, &layout, sizeof(layout));
    key = fnv(key, &c, sizeof(c)); /* a pack's own font sits in the category's bank slot */
    key = fnv(key, &fieldWindow, sizeof(fieldWindow));
    key = fnv(key, text, strlen(text));
    u8* found = arenaFind(key);
    if (found != NULL) return found == KEPT_TEXT ? NULL : found;
    const GtFont* to = runningFont();
    if (to == NULL) return NULL;
    GtEncode enc;
    memset(&enc, 0, sizeof(enc));
    enc.to = to;
#ifdef VERSION_JP
    enc.toUni = gtFontUnicode("JP", &enc.toUniCount);
    const char* bankRegion = "US";
#else
    enc.toUni = gtFontUnicode("US", &enc.toUniCount);
    const char* bankRegion = "JP";
#endif
    if (otherFontReady()) {
        enc.bankFont = &fonts.src[SRC_OTHER].font;
        enc.bankUni = gtFontUnicode(bankRegion, &enc.bankUniCount);
        enc.bankSlot = GT_BANK_OTHER;
    }
#ifdef VERSION_JP
    const char* runningName = "jp";
#else
    const char* runningName = "us";
#endif
    const char* fontName = gtPackFontName(packs[c].pack);
    enc.gIsTo = fontName[0] == '\0' || !SDL_strcasecmp(fontName, runningName); /* else the other release's font */
    if (packFontReady(c)) {
        /* font=pack (a text image's pack): its own glyphs, the running font's where they are the same picture */
        const uint16_t* uni = NULL;
        enc.bankFont = gtPackOwnFont(packs[c].pack, &uni);
        enc.bankUni = uni;
        enc.bankUniCount = enc.bankFont->count;
        enc.bankSlot = GT_BANK_PACK + c;
        enc.bankFirst = 1;
        enc.bankEquiv = packs[c].equiv;
        enc.gIsTo = 0;
    }
    uint8_t buf[4096];
    int n = gtPackEncode(text, &enc, buf, sizeof(buf));
    if (!fieldWindow) layout.windows = GT_WINDOWS_NATIVE;
    u8* out = arenaAdd(key, buf, n, &layout);
    if (out == KEPT_TEXT) {
        if (logOn > 0) fprintf(stderr, "gametext: pack text does not check out: %.60s\n", text);
        return NULL;
    }
    return out;
}

/* ---- save states (system/savestate.h) ------------------------------------------------------ */

static int sourceOf(int category);

/* The bank fonts the chosen sources' texts use (a state loaded in a new run
 * may show such a text before any is looked up). */
static void gtFontsReady(void) {
    for (int c = 0; c < GT_CATEGORIES; c++) {
        if (cfg.source[c] == GT_SOURCE_OTHER || cfg.source[c] == GT_SOURCE_IMAGE) sourceFontReady(sourceOf(c));
        if (cfg.source[c] == GT_SOURCE_PACK) {
            packFontReady(c);
            otherFontReady();
        }
    }
}

/* What the game holds of this file: the texts it was given and the
 * proportional print's pens. The sources, packs and caches are the host's. */
void gametextStateIo(XgState* s) {
    uint32_t used = (uint32_t)xgStateLen(s, "used", arenaUsed);
    uint32_t count = (uint32_t)xgStateLen(s, "count", arenaCount);
    if (used > ARENA_BYTES || count > ARENA_TEXTS) used = count = 0;
    xgStateIo(s, "bytes", arenaBytes, used);
    xgStateIo(s, "texts", arenaTexts, count * sizeof(arenaTexts[0]));
    XG_STATE_VAR(s, printing);
    XG_STATE_VAR(s, printingAge);
    if (xgStateLoading(s)) {
        /* the game may never set its text up again after a load (pcGameTextInit) */
        gameTextInit();
        gtFontsReady();
        arenaUsed = used;
        arenaCount = count;
        arenaFull = 0;
        memset(arenaHash, 0, sizeof(arenaHash));
        int proportionalText = 0;
        int wrappedText = 0;
        for (uint32_t i = 0; i < count; i++) {
            arenaHashAdd(i);
            proportionalText |= arenaTexts[i].proportional;
            wrappedText |= arenaTexts[i].wrap;
        }
        pcGameTextProportionalOn = proportionalText;
        pcGameTextWrapOn = wrappedText;
    }
}

GtPack* gtCategoryPack(int c) {
    if (c < 0 || c >= GT_CATEGORIES || cfg.source[c] != GT_SOURCE_PACK) return NULL;
    return categoryPack(c);
}

static const char* packFind(int c, const char* key, int* w, int* h) {
    GtPack* p = categoryPack(c);
    return p != NULL ? gtPackFind(p, key, w, h) : NULL;
}

/* ---- the other tables ------------------------------------------------------------------- */

typedef struct {
    uint64_t fingerprint;
    GtKey key;
    int category;
    int disc;
    SourceTable* source[2]; /* per source; NULL until first asked */
    int tried[2];
} KnownTable;

static KnownTable* known;
static int knownCount, knownCap, knownBuilt;

static void addKnown(void* user, const GtKey* key, const uint8_t* table, int size) {
    int disc = *(int*)user;
    int category = gtKeyCategory(key);
    if (category < 0) return;
    if (knownCount == knownCap) {
        int cap = knownCap ? knownCap * 2 : 128;
        KnownTable* grown = realloc(known, sizeof(*known) * (size_t)cap);
        if (grown == NULL) return;
        known = grown;
        knownCap = cap;
    }
    KnownTable* k = &known[knownCount++];
    memset(k, 0, sizeof(*k));
    k->fingerprint = gtFingerprint(table, size);
    k->key = *key;
    k->category = category;
    k->disc = disc;
}

static void buildKnown(void) {
    if (knownBuilt) return;
    knownBuilt = 1;
    for (int d = 1; d <= 2; d++) {
        GtImage* r = retailImage(d);
        if (r != NULL) {
            int n = gtImageTables(r, addKnown, &d);
            fprintf(stderr, "gametext: disc %d: %d text tables\n", d, n);
        }
    }
}

/* Tables the game asked about, by fingerprint (its tables come and go on
 * the heap: a pointer says nothing; the fingerprint is cheap, 400 bytes). */
#define SEEN 64
static struct {
    uint64_t fingerprint;
    int disc, known; /* index in known, -1 none */
} seen[SEEN];
static int seenNext, seenUsed;

static KnownTable* knownTable(const u8* table) {
    uint64_t fp = gtFingerprint(table, -1);
    int disc = currentDisc();
    for (int i = 0; i < seenUsed; i++) {
        if (seen[i].fingerprint == fp && seen[i].disc == disc) return seen[i].known >= 0 ? &known[seen[i].known] : NULL;
    }
    buildKnown();
    int found = -1;
    for (int i = 0; i < knownCount; i++) {
        if (known[i].fingerprint == fp && (found < 0 || known[i].disc == disc)) found = i;
    }
    seen[seenNext].fingerprint = fp;
    seen[seenNext].disc = disc;
    seen[seenNext].known = found;
    seenNext = (seenNext + 1) % SEEN;
    if (seenUsed < SEEN) seenUsed++;
    return found >= 0 ? &known[found] : NULL;
}

static SourceTable* knownSource(KnownTable* k, int src) {
    if (!k->tried[src]) {
        k->tried[src] = 1;
        GtImage* o = sourceImage(src, k->disc);
        if (o == NULL) o = sourceImage(src, 3 - k->disc);
        int size;
        uint8_t* blob = o ? gtImageTable(o, &k->key, &size) : NULL;
        k->source[src] = calloc(1, sizeof(*k->source[src]));
        if (k->source[src] != NULL) sourceTableSet(k->source[src], src, blob, size);
        else free(blob);
    }
    return k->source[src];
}

/* The text image was changed: what was read from the old one is not asked again. */
void gtForgetImage(void) {
    for (int i = 0; i < fieldCount; i++) fields[i].state[SRC_IMAGE] = 0;
    for (int i = 0; i < knownCount; i++) known[i].tried[SRC_IMAGE] = 0;
    fonts.src[SRC_IMAGE].tried = 0;
    fonts.src[SRC_IMAGE].data = NULL; /* kept allocated: brought glyphs may still be on screen */
    free(fonts.src[SRC_IMAGE].equiv);
    fonts.src[SRC_IMAGE].equiv = NULL;
}

static int sourceOf(int category) {
    return cfg.source[category] == GT_SOURCE_OTHER ? SRC_OTHER : cfg.source[category] == GT_SOURCE_IMAGE ? SRC_IMAGE : -1;
}

/* ---- what a text needs in a window (a pack's windows=grow) -------------------------------- */

typedef struct {
    int units, lines, line, page, prop, spacing, space;
    GtPen pen;
} Measure;

static void measureGlyph(Measure* m, const u16* glyph, int units) {
    if (m->prop) {
        int a, b;
        gtPenGlyph(&m->pen, glyph, units, m->spacing, m->space, &a, &b);
        m->line = (m->pen.end + 3) / 4;
    } else {
        m->line += units;
    }
    if (m->line > m->units) m->units = m->line;
}

static void measureBlank(Measure* m, int units) { /* a substring not known yet: as many narrow glyphs */
    if (m->prop) {
        m->pen.px += units * 4;
        m->pen.end = SDL_max(m->pen.end, m->pen.px);
        m->pen.ink = 1;
        m->line = (m->pen.end + 3) / 4;
    } else {
        m->line += units;
    }
    if (m->line > m->units) m->units = m->line;
}

static void measureBreak(Measure* m, int page) {
    m->line = 0;
    memset(&m->pen, 0, sizeof(m->pen));
    m->page = page ? 1 : m->page + 1;
    if (m->page > m->lines) m->lines = m->page;
}

/* As updateDialogTextImage reads it: a substring as the game would print it
 * now (the names, the banks), a number as six digits, a reference as eight
 * letters. */
static void measureText(Measure* m, const u8* s, int depth) {
    extern u16 crossedBoxGlyph[12];
    static const int8_t escape[16] = {3, 3, 3, 4, 2, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3};
    const GtFont* f = runningFont();
    for (int guard = 0; s != NULL && *s != 0 && guard < 4096; guard++) {
        int c = s[0];
        if (c == 1) {
            measureBreak(m, 0);
            s++;
        } else if (c == 2) {
            measureBreak(m, 1);
            s += s[1] == 1 ? 2 : 1;
        } else if (c == 3) {
            return;
        } else if (c == 0x0F) {
            int k = s[1] & 15;
            const u8* sub = NULL;
            switch (k) {
            case 3: sub = getDeathblowName(s[2], s[3]); break;
            case 4: measureBlank(m, 16); break;
            case 5: {
                int member = s[2];
                if (member >= 0x80) member = member - 0x80 < 3 ? gameState.m1D34_currentParty[member - 0x80] : 0xFF;
                sub = member < 31 ? gameState.m0_names[member] : getDeathblowName(0x68 / 4, 0);
                break;
            }
            case 6: sub = getWeaponName(s[2]); break;
            case 7: sub = getTextBank24String(s[2]); break;
            case 8: sub = getTextBank25String(s[2]); break;
            case 9: case 10: case 12: measureBlank(m, 12); break;
            case 15: measureBlank(m, 3); break;
            case 2: case 13: measureBreak(m, 1); break; /* it closes after a while */
            default: break;
            }
            if (sub != NULL && depth < 2) measureText(m, sub, depth + 1);
            s += escape[k];
        } else if (c == 0xFF && s[1] == 0xFF) {
            measureGlyph(m, crossedBoxGlyph, 3);
            s += 2;
        } else if (c == 0xFF && s[1] >= 0xC0) {
            u16 units = 2;
            const u16* g = pcGameTextGlyph(s, &units);
            if (g == NULL) return;
            measureGlyph(m, g, units);
            s += 3;
        } else {
            int n, i = f != NULL ? gtFontDecode(f, s, &n) : -1;
            if (i < 0) return;
            measureGlyph(m, (const u16*)(const void*)(f->bits + i * 22), gtFontWide(f, i) ? 3 : 2);
            s += n;
        }
    }
}

/* Read one printable glyph without changing the game's printer globals. */
static const u16* textGlyph(const u8* s, int* len) {
    if (*s < 0x10) return NULL;
    if (s[0] == 0xFF && s[1] >= 0xC0 && s[1] < 0xFF) {
        u16 units;
        *len = 3;
        return pcGameTextGlyph(s, &units);
    }
    const GtFont* f = runningFont();
    int i = f != NULL ? gtFontDecode(f, s, len) : -1;
    return i >= 0 ? (const u16*)(const void*)(f->bits + i * 22) : NULL;
}

static int textBlank(const u8* s, int* len) {
    int a, b;
    const u16* glyph = textGlyph(s, len);
    return glyph != NULL && !gtGlyphInk(glyph, &a, &b);
}

s32 pcGameTextWrap(const void* window, u8** text, s16* cursorX, s32 width) {
    s32 lineWidth = width;
    width = pcGameTextPrintWidth(window, width);
    int slot = -1;
    for (int i = 0; i < 16; i++) if (printing[i].window == window) { slot = i; break; }
    if (slot < 0 || !printing[slot].wrap) return 0;
    if (*cursorX < printing[slot].wrapCursor || *cursorX == 0) printing[slot].wrapInk = 0;
    printing[slot].wrapCursor = *cursorX;
    const u8* start = *text;
    const u8* word = start;
    int len = 0;
    if (!textBlank(word, &len)) { printing[slot].wrapInk = 1; return 0; }
    if (!printing[slot].wrapInk || *cursorX <= 0 || *cursorX > width) return 0;
    /* Keep leading indentation. At an automatic break, discard only the
     * separator blanks; explicit newlines/waits and escaped controls stay. */
    do { word += len; } while (word - start < 4096 && textBlank(word, &len));
    if (word - start >= 4096) return 0;
    const u8* end = word;
    static const uint8_t escape[16] = {3, 3, 3, 4, 2, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3};
    while (*end && end - start < 4096) {
        if (*end == 0x0F && end[1] < 16) {
            end += escape[end[1]];
        } else {
            if (*end < 0x10 || textBlank(end, &len)) break;
            if (!textGlyph(end, &len)) break;
            end += len;
        }
    }
    if (end == word || end - start >= 4096) return 0;
    u8 codes[4096];
    memcpy(codes, word, (size_t)(end - word)); codes[end - word] = 0;
    Measure m;
    memset(&m, 0, sizeof(m));
    m.prop = printing[slot].on;
    m.spacing = printing[slot].spacing;
    m.space = printing[slot].space;
    measureText(&m, codes, 0);
    if (m.units > width) return 0; /* a long word keeps native character wrapping */
    memset(&m.pen, 0, sizeof(m.pen));
    m.units = 0;
    m.line = *cursorX;
    if (m.prop) {
        if (*cursorX == printing[slot].cursor) m.pen = printing[slot].pen;
        else m.pen.px = m.pen.end = *cursorX * 4;
    }
    memcpy(codes, start, (size_t)(end - start)); codes[end - start] = 0;
    measureText(&m, codes, 0);
    if (m.units <= width) return 0;
    if (logOn > 0) fprintf(stderr, "gametext: word wrap at %d/%d units\n", *cursorX, (int)width);
    *text = (u8*)word;
    *cursorX = (s16)(lineWidth + 1);
    printing[slot].cursor = -1;
    return 1;
}

static Measure* measured(const u8* codes, const GtLayout* layout) {
    static Measure m;
    static const u8* last;
    static GtLayout lastLayout;
    if (codes != last || memcmp(layout, &lastLayout, sizeof(*layout))) {
        memset(&m, 0, sizeof(m));
        m.prop = layout->proportional;
        m.spacing = layout->spacing;
        m.space = layout->space;
        m.page = m.lines = 1;
        measureText(&m, codes, 0);
        last = codes;
        lastLayout = *layout;
    }
    return &m;
}
static int measuredWidth(const u8* codes, const GtLayout* layout) { return measured(codes, layout)->units; }
static int measuredLines(const u8* codes, const GtLayout* layout) { return measured(codes, layout)->lines; }

/* ---- the lookup (include/pc_gametext.h) -------------------------------------------------- */

static u8* counted(int c, u8* text) {
    if (logOn > 0) fprintf(stderr, "gametext: %s %s\n", c >= 0 ? categoryKeys[c] : "-", text ? "given" : "kept");
    if (c >= 0) {
        if (text != NULL) cfg.given[c]++;
        else cfg.kept[c]++;
    }
    return text;
}

u8* pcGameTextString(u8* table, s32 index) {
    if (table == NULL) return NULL;
    if (logOn > 0) fprintf(stderr, "gametext: table %p entry %d\n", (void*)table, (int)index);
    FieldText* f = currentField(table);
    if (f != NULL) {
        switch (cfg.source[GT_DIALOGUE]) {
        case GT_SOURCE_OTHER:
        case GT_SOURCE_IMAGE: {
            SourceTable* s = fieldSource(f, sourceOf(GT_DIALOGUE));
            return s != NULL ? sourceText(s, index, GT_DIALOGUE) : counted(GT_DIALOGUE, NULL);
        }
        case GT_SOURCE_PACK: {
            char key[64];
            snprintf(key, sizeof(key), "field/%d/%d/%d", f->disc, f->field, (int)index);
            const char* text = packFind(GT_DIALOGUE, key, NULL, NULL);
            return counted(GT_DIALOGUE, text ? packEncoded(GT_DIALOGUE, text, 1) : NULL);
        }
        default:
            return NULL;
        }
    }
    if (table == rawFieldDialogBundle) return NULL; /* a field the retail images do not have */
    KnownTable* k = knownTable(table);
    if (k == NULL) return NULL;
    switch (cfg.source[k->category]) {
    case GT_SOURCE_OTHER:
    case GT_SOURCE_IMAGE: {
        SourceTable* s = knownSource(k, sourceOf(k->category));
        return s != NULL ? sourceText(s, index, k->category) : counted(k->category, NULL);
    }
    case GT_SOURCE_PACK: {
        char name[64], key[80];
        gtKeyName(&k->key, k->disc, name, sizeof(name));
        snprintf(key, sizeof(key), "%s/%d", name, (int)index);
        const char* text = packFind(k->category, key, NULL, NULL);
        if (text == NULL && k->disc != 1) {
            gtKeyName(&k->key, 1, name, sizeof(name));
            snprintf(key, sizeof(key), "%s/%d", name, (int)index);
            text = packFind(k->category, key, NULL, NULL);
        }
        return counted(k->category, text ? packEncoded(k->category, text, 0) : NULL);
    }
    default:
        return NULL;
    }
}

s32 pcGameTextWindowOverridesScript(u8* table, s32 index) {
    if (cfg.source[GT_DIALOGUE] != GT_SOURCE_PACK) return 0;
    GtPack* pack = categoryPack(GT_DIALOGUE);
    if (pack == NULL) return 0;
    GtLayout layout;
    gtPackLayout(pack, &layout);
    return (layout.windows == GT_WINDOWS_PACK || layout.windows == GT_WINDOWS_GROW) &&
           pcGameTextWindow(table, index, 0) >= 0;
}

s32 pcGameTextWindow(u8* table, s32 index, s32 which) {
    if (cfg.source[GT_DIALOGUE] == GT_SOURCE_DISC) return -1;
    FieldText* f = currentField(table);
    if (f == NULL) return -1;
    if (cfg.source[GT_DIALOGUE] == GT_SOURCE_OTHER || cfg.source[GT_DIALOGUE] == GT_SOURCE_IMAGE) {
        SourceTable* s = fieldSource(f, sourceOf(GT_DIALOGUE));
        if (s == NULL || s->wh == NULL || index < 0 || index >= s->table.count) return -1;
        /* only with its text: a text the source could not give keeps its size */
        if (sourceText(s, index, -1) == NULL) return -1;
        return s->wh[index * 2 + (which ? 1 : 0)];
    }
    if (cfg.source[GT_DIALOGUE] == GT_SOURCE_PACK) {
        char key[64];
        int w = 0, h = 0;
        snprintf(key, sizeof(key), "field/%d/%d/%d", f->disc, f->field, (int)index);
        const char* text = packFind(GT_DIALOGUE, key, &w, &h);
        u8* codes = text != NULL ? packEncoded(GT_DIALOGUE, text, 1) : NULL;
        if (codes == NULL || index < 0 || index > rd16(table)) return -1;
        GtPack* pack = categoryPack(GT_DIALOGUE);
        GtLayout layout;
        gtPackLayout(pack, &layout);
        int disc = table[rd16(table) * 2 + 6 + index * 2 + (which ? 1 : 0)];
        int v = which ? h : w;
        if (v < 0 || v > 255) v = disc;
        if (layout.windows == GT_WINDOWS_SOURCE) return v;
        if (layout.windows == GT_WINDOWS_NATIVE) {
            /* the disc's size; a pack of the other release: that release's (as its disc gives) */
            return !SDL_strcasecmp(gtPackRegion(pack), OTHER_REGION) ? v : -1;
        }
        if (layout.windows == GT_WINDOWS_GROW && v > 0) {
            int need = which ? measuredLines(codes, &layout) : measuredWidth(codes, &layout);
            if (need > v) v = need;
        }
        /* within the screen (55: the field places a window with a portrait,
         * 17 units more, on screen) and the window's text memory (its lines'
         * VRAM band: 7 lines); never below the disc's */
        int limit = SDL_max(disc, which ? 7 : 55);
        v = SDL_min(v, limit);
        if (logOn > 0) fprintf(stderr, "gametext: window %s %d (disc %d)\n", which ? "height" : "width", v, disc);
        return v;
    }
    return -1;
}

/* ---- export ------------------------------------------------------------------------------ */

static int otherFieldFits(int disc, int field, void* user) {
    (void)user;
    GtImage* r = retailImage(disc);
    GtImage* o = otherImage(disc);
    return r != NULL && o != NULL && gtSameDialogue(r, o, field);
}

static int imageFieldFits(int disc, int field, void* user) {
    (void)user;
    GtImage* r = retailImage(disc);
    GtImage* t = textImage(disc);
    return r != NULL && t != NULL && gtSameDialogue(r, t, field);
}

/* The text images' text as a private pack with their own font (gametext.h). */
static int exportTextImage(char* id, size_t idSize, char* why, size_t size) {
    GtImage* d1 = textImage(1);
    GtImage* d2 = textImage(2);
    GtImage* any = d1 ? d1 : d2;
    const char* dir = gtPackDir();
    if (dir == NULL) {
        snprintf(why, size, "No pack folder (XG_PACK_DIR=none, or no save folder).");
        return 0;
    }
    if (any == NULL) {
        snprintf(why, size, "%s", sources[SRC_IMAGE].why[0][0] ? sources[SRC_IMAGE].why[0] : "No text image (text_image1).");
        return 0;
    }
    /* the characters by the original font of the image's release: the
     * glyphs the patch kept or redrew in place are known, its new ones <g:N> */
    const char* region = gtImageRegion(any);
    const GtFont* base = NULL;
    const uint16_t* baseUni = NULL;
    int baseCount = 0;
    if (!strcmp(region, OTHER_REGION)) {
        if (otherFontReady()) base = &fonts.src[SRC_OTHER].font;
    } else {
        base = runningFont();
    }
    baseUni = gtFontUnicode(region, &baseCount);
    GtFont own;
    int ownSize = 0;
    uint8_t* ownData = NULL;
    if (base == NULL) { /* no original to compare with: the image's font by its own places */
        ownData = gtImageFont(any, &ownSize);
        if (ownData != NULL && gtFontParse(ownData, ownSize, &own)) base = &own;
    }
    if (base == NULL) {
        free(ownData);
        snprintf(why, size, "Cannot read a font to compare %s's with.", gtImagePath(any));
        return 0;
    }
    /* the pack's id: "image-" and the image's file name */
    const char* path = gtImagePath(any);
    const char* file = strrchr(path, '/');
    file = file ? file + 1 : path;
    char stem[48];
    int n = 0;
    for (const char* p = file; *p && *p != '.' && n < (int)sizeof(stem) - 1; p++) {
        stem[n++] = (char)(SDL_isalnum((unsigned char)*p) ? SDL_tolower((unsigned char)*p) : '-');
    }
    stem[n] = '\0';
    snprintf(id, idSize, "image-%s", n ? stem : "patch");
    char packPath[4096], name[160];
    SDL_CreateDirectory(dir);
    snprintf(packPath, sizeof(packPath), "%s/%s", dir, id);
    snprintf(name, sizeof(name), "Text image %s", file);
    int ok = gameTextExportImage(d1, d2, base, baseUni, baseCount, packPath, name, imageFieldFits, NULL, why, size);
    free(ownData);
    return ok;
}

int gameTextExportPack(int which, char* id, size_t idSize, char* why, size_t size) {
    gameTextInit();
    if (which == 2) {
        if (!exportTextImage(id, idSize, why, size)) return 0;
        char packPath[4096];
        snprintf(packPath, sizeof(packPath), "%s/%s", gtPackDir(), id);
        int pictures = gtPictureExport(textImage(1), textImage(2), packPath, 1);
        if (pictures < 0) {
            snprintf(why, size, "Cannot export the text image's picture geometry and palettes.");
            return 0;
        }
        if (pictures > 0) {
            size_t n = strlen(why);
            if (n > 0 && why[n - 1] == '.') why[n - 1] = '\0';
            snprintf(why + strlen(why), size - strlen(why), "; %d pictures.", pictures);
        }
        return 1;
    }
    GtImage* d1 = which ? otherImage(1) : retailImage(1);
    GtImage* d2 = which ? otherImage(2) : retailImage(2);
    const char* dir = gtPackDir();
    if (dir == NULL) {
        snprintf(why, size, "No pack folder (XG_PACK_DIR=none, or no save folder).");
        return 0;
    }
    GtImage* any = d1 ? d1 : d2;
    if (any == NULL) {
        snprintf(why, size, "%s", which ? other.why[0] : "No disc image of this release to read.");
        return 0;
    }
    const char* region = gtImageRegion(any);
    int jp = !strcmp(region, "JP");
    snprintf(id, idSize, "%s-%s", jp ? "jp" : "us", jp ? "ja" : "en");
    char path[4096];
    SDL_CreateDirectory(dir);
    snprintf(path, sizeof(path), "%s/%s", dir, id);
    const char* name = jp ? "JP text" : "US text";
    if (!gameTextExport(d1, d2, path, name, which ? otherFieldFits : NULL, NULL, why, size)) return 0;
    /* this release's pictures with text too (the other release's are cut
     * by its own sprite tables: they would not fit this one's) */
    int pictures = which ? 0 : gtPictureExport(d1, d2, path, 0);
    if (pictures > 0) {
        size_t n = strlen(why);
        if (n > 0 && why[n - 1] == '.') why[n - 1] = '\0';
        snprintf(why + strlen(why), size - strlen(why), "; %d pictures.", pictures);
    }
    return 1;
}

int gameTextPackLayout(int c, GtLayout* out) {
    gameTextInit();
    if (c < 0 || c >= GT_CATEGORIES || cfg.source[c] != GT_SOURCE_PACK) return 0;
    GtPack* p = categoryPack(c);
    if (p == NULL) return 0;
    gtPackLayout(p, out);
    return 1;
}

const char* gameTextStatus(int c) {
    gameTextInit();
    if (c < 0 || c >= GT_CATEGORIES) return "";
    char* s = cfg.status[c];
    switch (cfg.source[c]) {
    case GT_SOURCE_DISC:
        snprintf(s, sizeof(cfg.status[c]), "The disc's text.");
        break;
    case GT_SOURCE_OTHER: {
        int d = currentDisc();
        GtImage* o = otherImage(d);
        if (o == NULL) snprintf(s, sizeof(cfg.status[c]), "%s", other.why[d - 1]);
        else snprintf(s, sizeof(cfg.status[c]), "%s Disc %d: %s", OTHER_REGION, d, gtImagePath(o));
        break;
    }
    case GT_SOURCE_IMAGE: {
        int d = currentDisc();
        GtImage* t = textImage(d);
        if (t == NULL) snprintf(s, sizeof(cfg.status[c]), "%s", sources[SRC_IMAGE].why[d - 1]);
        else snprintf(s, sizeof(cfg.status[c]), "Text image (%s Disc %d): %s", gtImageRegion(t), d, gtImagePath(t));
        break;
    }
    case GT_SOURCE_PACK:
        categoryPack(c);
        snprintf(s, sizeof(cfg.status[c]), "%s", packs[c].why);
        break;
    default:
        snprintf(s, sizeof(cfg.status[c]), "Not available yet: the disc's text.");
        break;
    }
    return s;
}
