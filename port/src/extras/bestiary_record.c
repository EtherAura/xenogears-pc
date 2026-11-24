/* The bestiary's progress per game (bestiary_record.h): the battle code's
 * reports (pcBestiaryNote, include/pc_bestiary.h) into the running game's
 * record, a chosen record for the bestiary to show, and the carry-over. */

#include "bestiary_record.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "card/card.h"
#include "card/saves.h"
#include "cd/cd.h"
#include "common.h"
#include "main/game_state.h"
#include "pc_bestiary.h"
#include "system/gameid.h"
#include "system/settings.h"

int getCurrentDiscNumber(void); /* src/main/filesystem.c: the loaded directory table's disc (1, 2) */

typedef struct {
    char region;
    unsigned char disc, set, type;
    int flags;
} Key;

typedef struct {
    int loaded;
    Key* keys;
    int count, capacity;
    unsigned id;
    int fresh;       /* a fresh game before its first save: in memory only */
    char path[1024]; /* empty: nothing on disk */
} Record;

static Record game = {0, NULL, 0, 0, 0, 1, ""};
static Record view;
static Record* active = &game;

/* bestiary-<id>.txt, or bestiary.txt for id 0, beside settings.ini. */
static void pathOf(char* out, size_t size, unsigned id) {
    const char* settings = settingsPath();
    out[0] = '\0';
    if (settings == NULL || settings[0] == '\0') return;
    snprintf(out, size, "%s", settings);
    char* slash = strrchr(out, '/');
#ifdef _WIN32
    char* back = strrchr(out, '\\');
    if (back != NULL && (slash == NULL || back > slash)) slash = back;
#endif
    size_t at = slash != NULL ? (size_t)(slash - out + 1) : 0;
    if (id != 0) {
        snprintf(out + at, size - at, "bestiary-%08x.txt", id);
    } else {
        snprintf(out + at, size - at, "bestiary.txt");
    }
}

static Key* find(Record* r, int region, int disc, int set, int type, int add) {
    for (int i = 0; i < r->count; i++) {
        Key* k = &r->keys[i];
        if (k->region == region && k->disc == disc && k->set == set && k->type == type) return k;
    }
    if (!add) return NULL;
    if (r->count == r->capacity) {
        int capacity = r->capacity ? r->capacity * 2 : 256;
        Key* grown = realloc(r->keys, sizeof(Key) * (size_t)capacity);
        if (grown == NULL) return NULL;
        r->keys = grown;
        r->capacity = capacity;
    }
    Key* k = &r->keys[r->count++];
    k->region = (char)region;
    k->disc = (unsigned char)disc;
    k->set = (unsigned char)set;
    k->type = (unsigned char)type;
    k->flags = 0;
    return k;
}

/* The keys of a record file merged into r. */
static void readInto(Record* r, const char* path) {
    FILE* f = path[0] ? fopen(path, "r") : NULL;
    if (f == NULL) return;
    char line[128];
    while (fgets(line, sizeof(line), f)) {
        char region;
        int disc, set, type, flags;
        if (line[0] == '#' || sscanf(line, " %c %d %d %d %d", &region, &disc, &set, &type, &flags) != 5) continue;
        if ((region != 'U' && region != 'J') || disc < 1 || disc > 2 || set < 0 || set > 255 || type < 0 || type > 127) continue;
        Key* k = find(r, region, disc, set, type, 1);
        if (k) k->flags |= flags & 7;
    }
    fclose(f);
}

unsigned xgBestiaryCarryPending(void) {
    const char* v = settingsGet("bestiary_carry");
    if (v == NULL || v[0] == '\0' || strcmp(v, "none") == 0) return XG_BESTIARY_CARRY_NONE;
    return (unsigned)strtoul(v, NULL, 16);
}

static void load(Record* r) {
    if (r->loaded) return;
    r->loaded = 1;
    r->count = 0;
    if (r->fresh && r->id == 0) {
        /* A fresh game: nothing on disk yet; the carry-over seeds it. */
        unsigned carry = xgBestiaryCarryPending();
        r->path[0] = '\0';
        if (carry != XG_BESTIARY_CARRY_NONE) {
            char path[1024];
            pathOf(path, sizeof(path), carry);
            readInto(r, path);
        }
        return;
    }
    pathOf(r->path, sizeof(r->path), r->id);
    readInto(r, r->path);
}

static void save(Record* r) {
    load(r);
    if (!r->path[0]) return;
    char temp[1040];
    snprintf(temp, sizeof(temp), "%s.new", r->path);
    FILE* f = fopen(temp, "w");
    if (f == NULL) return;
    fprintf(f, "# Xenogears bestiary (the extras): enemy types met in battle (1), defeated (2), caught up from a save (4)\n");
    if (r->id != 0) fprintf(f, "# game %08x\n", r->id);
    fprintf(f, "# region disc set type flags\n");
    for (int i = 0; i < r->count; i++) {
        const Key* k = &r->keys[i];
        fprintf(f, "%c %d %d %d %d\n", k->region, k->disc, k->set, k->type, k->flags);
    }
    if (fclose(f) != 0) {
        remove(temp);
        return;
    }
#ifdef _WIN32
    remove(r->path);
#endif
    rename(temp, r->path);
}

/* ---- the viewed record ------------------------------------------------- */

int xgBestiaryState(int region, int disc, int set, int type) {
    load(active);
    const Key* k = find(active, region, disc, set, type, 0);
    return k ? k->flags : 0;
}

int xgBestiaryCatchUp(int region, int disc, int set, int type) {
    load(active);
    Key* k = find(active, region, disc, set, type, 1);
    if (k == NULL || k->flags & (XG_BESTIARY_SEEN | XG_BESTIARY_CAUGHT_UP)) return 0;
    k->flags |= XG_BESTIARY_CAUGHT_UP;
    return 1;
}

void xgBestiarySave(void) { save(active); }

int xgBestiaryCount(void) {
    load(active);
    return active->count;
}

void xgBestiaryReset(void) {
    load(active);
    active->count = 0;
    if (active->path[0]) remove(active->path);
}

/* ---- which record ------------------------------------------------------ */

void xgBestiaryView(unsigned id) {
    load(&game);
    if (!game.fresh && id == game.id) {
        active = &game;
        return;
    }
    if (active == &view && view.id == id) return;
    view.loaded = 0;
    view.id = id;
    view.fresh = 0;
    active = &view;
}

void xgBestiaryViewGame(void) { active = &game; }
int xgBestiaryViewingGame(void) { return active == &game; }
unsigned xgBestiaryGameId(void) { return game.id; }
int xgBestiaryGameFresh(void) { return game.fresh; }

/* gameid.c: the running game changed, or was just given its id. */
void pcBestiaryGameChanged(unsigned id, int fresh, int assigned) {
    if (assigned) {
        /* The record kept so far (in memory for a fresh game; the shared
         * file's keys for a game saved before ids) goes on under the id. */
        load(&game);
        game.id = id;
        game.fresh = 0;
        pathOf(game.path, sizeof(game.path), id);
        save(&game);
        if (fresh && xgBestiaryCarryPending() != XG_BESTIARY_CARRY_NONE) {
            settingsSet("bestiary_carry", "none"); /* consumed */
            settingsSave();
        }
        return;
    }
    if (game.loaded && game.id == id && game.fresh == fresh && !(fresh && id == 0)) return;
    game.loaded = 0;
    game.id = id;
    game.fresh = fresh;
    if (active == &view && !fresh && view.id == id) active = &game;
}

/* ---- carry-over --------------------------------------------------------- */

void xgBestiaryCarry(unsigned id) {
    char text[16];
    settingsDescribe("bestiary_carry", "bestiary_carry: the bestiary record (a game id in hex, 0 the shared one) the next new game starts with; none");
    if (id == XG_BESTIARY_CARRY_NONE) {
        settingsSet("bestiary_carry", "none");
    } else {
        snprintf(text, sizeof(text), "%08x", id);
        settingsSet("bestiary_carry", text);
    }
    settingsSave();
    if (game.fresh && game.id == 0) game.loaded = 0; /* the fresh game's seed changed */
}

/* ---- the battle's reports ---------------------------------------------- */

void pcBestiaryNote(int set, int type, int defeated) {
    int bit = defeated ? XG_BESTIARY_DEFEATED : XG_BESTIARY_SEEN;
    load(&game);
    int disc = getCurrentDiscNumber();
    if (disc < 1 || disc > 2 || set < 0 || set > 255 || type < 0 || type > 127) return;
    Key* k = find(&game, cdDiscIsJapanese() ? 'J' : 'U', disc, set, type, 1);
    if (k == NULL || (k->flags & bit)) return;
    k->flags |= bit | (defeated ? XG_BESTIARY_SEEN : 0);
    save(&game);
}

int pcBestiaryKnown(int set, int type) {
    int disc = getCurrentDiscNumber();
    if (disc < 1 || disc > 2) return 0;
    load(&game);
    const Key* k = find(&game, cdDiscIsJapanese() ? 'J' : 'U', disc, set, type, 0);
    return k != NULL && (k->flags & XG_BESTIARY_DEFEATED) != 0;
}

/* ---- the game and its saves -------------------------------------------- */

int xgBestiaryGameProgress(void) {
    /* The bestiary offers it only when the extras opened over the paused
     * game (viewer.h fromGame); on their own there is no game. */
    return gameState.m1930_fieldVarsBackup[0];
}

/* Both regions' saves (the card module's reader, card/saves.h). */
int xgBestiarySaves(XgBestiarySave* out, int max) {
    CardGameSave saves[2 * CARD_BLOCKS];
    int n = cardGameSaves(saves, max < 2 * CARD_BLOCKS ? max : 2 * CARD_BLOCKS, 0);
    for (int i = 0; i < n; i++) {
        XgBestiarySave* s = &out[i];
        s->slot = saves[i].card;
        s->block = saves[i].block;
        s->region = saves[i].region;
        s->progress = saves[i].progress;
        s->disc = saves[i].disc;
        s->id = saves[i].id;
        snprintf(s->title, sizeof(s->title), "%s", saves[i].title);
    }
    return n;
}
