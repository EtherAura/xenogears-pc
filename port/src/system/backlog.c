/*
 * The dialogue backlog's capture (backlog.h; xg-htn.57).
 *
 * The printer draws a text a glyph at a time into a RAM image of the
 * window's current line pair (13 pixel lines, 4 bits a pixel: the even line
 * of the pair in bits 0-1, the odd one in bits 2-3), and uploads it to VRAM
 * after each step; earlier pages and lines exist nowhere once it moves on.
 * So the backlog copies each glyph's pixels as it is printed
 * (pcBacklogLine), into the entry of the window's text, a new row whenever
 * the printer moves to another line or back to its start: names and numbers
 * as they were substituted, a pack's or the other release's glyphs, the
 * pad's button glyphs, all as shown. The extras' dialogue history
 * (port/src/extras/observe.c) copies the same pixels for its own pane.
 *
 * An entry is made at its window's first glyph, so a text that prints
 * nothing (a window opened and closed) leaves none. When a window's next
 * text starts, its last one is finished: the same as the entry before it,
 * it is dropped (the world map's place names come again each time).
 */

#include "backlog.h"

#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common.h"
#include "main/dialog.h"
#include "pc_backlog.h"
#include "pc_wide.h"
#include "settings.h"
#include "system.h"

/* ---- the setting (host) ---- */

static int chosen, chosenValid;
static unsigned chosenRevision;

int backlogChosen(void) {
    if (!chosenValid || chosenRevision != settingsRevision()) {
        chosen = settingsInt("dialogue_backlog", 0) != 0;
        chosenRevision = settingsRevision();
        chosenValid = 1;
    }
    return chosen;
}

void backlogSetChosen(int on) {
    settingsDescribe("dialogue_backlog", "dialogue_backlog: 1 to keep the text shown so it can be read again (H, or "
                                         "Select+Square on the gamepad); 0 off (default)");
    settingsSetInt("dialogue_backlog", on != 0);
    settingsSave();
    chosenValid = 0;
    if (!on) {
        backlogClear();
    }
}

/* ---- the entries ---- */

typedef struct {
    BacklogEntry info;
    uint8_t* pixels; /* capacity rows of BACKLOG_LINE lines, info.stride bytes each */
    char* text;
    int capacity;
} Entry;

static Entry entries[BACKLOG_ENTRIES];
static uint32_t serial; /* the newest entry's (0: none yet) */
static int count;

static Entry* bySerial(uint32_t s) {
    if (s == 0 || s > serial || serial - s >= (uint32_t)count) {
        return NULL;
    }
    return &entries[(s - 1) % BACKLOG_ENTRIES];
}

static void release(Entry* e) {
    free(e->pixels);
    free(e->text);
    memset(e, 0, sizeof *e);
}

static Entry* newEntry(int kind) {
    Entry* e = &entries[serial % BACKLOG_ENTRIES];
    release(e); /* the oldest, once the ring is full */
    e->info.serial = ++serial;
    e->info.kind = kind;
    if (count < BACKLOG_ENTRIES) {
        count++;
    }
    return e;
}

static int same(const Entry* a, const Entry* b) {
    if (a->info.kind != b->info.kind || a->info.rows != b->info.rows || a->info.width != b->info.width) {
        return 0;
    }
    if ((a->text != NULL) != (b->text != NULL) || (a->text != NULL && strcmp(a->text, b->text) != 0)) {
        return 0;
    }
    for (int y = 0; y < a->info.rows * BACKLOG_LINE; y++) {
        if (memcmp(a->pixels + (size_t)y * a->info.stride, b->pixels + (size_t)y * b->info.stride,
                   (size_t)a->info.width) != 0) {
            return 0;
        }
    }
    return 1;
}

/* An entry is done: the newest the same as the one before it goes. */
static void finish(uint32_t s) {
    Entry* e = bySerial(s);
    if (e == NULL || s != serial || count < 2) {
        return;
    }
    Entry* before = bySerial(s - 1);
    if (before != NULL && same(e, before)) {
        release(e);
        serial--;
        count--;
    }
}

int backlogCount(void) {
    return count;
}

int backlogGet(int index, BacklogEntry* out) {
    if (out == NULL || index < 0 || index >= count) {
        return 0;
    }
    const Entry* e = bySerial(serial - (uint32_t)(count - 1 - index));
    if (e == NULL) {
        return 0;
    }
    *out = e->info;
    out->pixels = e->pixels;
    out->text = e->text;
    return 1;
}

/* ---- the windows being printed ---- */

#define TRACKS 8 /* the field's four, the battle's, the world map's two, one spare */

typedef struct {
    const void* window;
    int ignored;     /* the port's own prompt (pcBacklogIgnore) */
    int kind;        /* this text's, or -1: not kept */
    uint32_t entry;  /* its entry once a glyph came, else 0 */
    int line, x;     /* the last glyph's */
} Track;
static Track tracks[TRACKS];

static Track* track(const void* window, int add) {
    Track* free_ = NULL;
    for (int i = 0; i < TRACKS; i++) {
        if (tracks[i].window == window) return &tracks[i];
        if (tracks[i].window == NULL && free_ == NULL) free_ = &tracks[i];
    }
    if (!add) return NULL;
    if (free_ == NULL) {
        /* More windows than ever print at once: the one longest without an entry goes. */
        free_ = &tracks[0];
        for (int i = 1; i < TRACKS; i++) {
            if (!tracks[i].ignored && (free_->ignored || tracks[i].entry < free_->entry)) free_ = &tracks[i];
        }
        finish(free_->entry);
    }
    memset(free_, 0, sizeof *free_);
    free_->window = window;
    free_->kind = -1;
    return free_;
}

void backlogClear(void) {
    for (int i = 0; i < BACKLOG_ENTRIES; i++) release(&entries[i]);
    count = 0; /* (serial goes on: an old serial never names a new entry) */
    for (int i = 0; i < TRACKS; i++) tracks[i].entry = 0;
}

void pcBacklogIgnore(const void* window) {
    Track* t = track(window, 1);
    if (t != NULL) {
        t->ignored = 1;
        t->kind = -1;
        t->entry = 0;
    }
}

/* The screen's kind of text, or -1 for a screen whose text is not kept. */
static int screenKind(void) {
    if (hwInWorld()) return -1; /* the extras' sandboxes: not the game the player plays */
    switch (pcWideGetMode()) {
    case PC_WIDE_FIELD: return BACKLOG_FIELD;
    case PC_WIDE_WORLDMAP: return BACKLOG_WORLDMAP;
    case PC_WIDE_BATTLE: return BACKLOG_BATTLE;
    default: return -1; /* menus, Battling, the kernel's screens */
    }
}

void pcBacklogText(const void* window) {
    Track* t = track(window, backlogChosen());
    if (t == NULL) return;
    finish(t->entry);
    t->entry = 0;
    t->line = t->x = -1;
    t->kind = t->ignored || !backlogChosen() ? -1 : screenKind();
}

static int grow(Entry* e, int rows) {
    if (rows <= e->capacity) return 1;
    int want = e->capacity > 0 ? e->capacity * 2 : 4;
    if (want < rows) want = rows;
    uint8_t* p = realloc(e->pixels, (size_t)want * BACKLOG_LINE * e->info.stride);
    if (p == NULL) return 0;
    memset(p + (size_t)e->capacity * BACKLOG_LINE * e->info.stride, 0,
           (size_t)(want - e->capacity) * BACKLOG_LINE * e->info.stride);
    e->pixels = p;
    e->capacity = want;
    return 1;
}

#define MAX_ROWS 256 /* a text's: past it, the rest is not kept */

void pcBacklogLine(const void* window, const void* image, int stride, int x, int width, int line, int odd) {
    if (image == NULL || stride <= 0 || stride > 256 || x < 0 || width <= 0 || x + width > stride) return;
    Track* t = track(window, 0);
    if (t == NULL || t->kind < 0 || !backlogChosen()) return;
    Entry* e = bySerial(t->entry);
    if (e == NULL || e->info.stride != stride * 4) {
        e = newEntry(t->kind);
        e->info.stride = stride * 4;
        e->info.body = fontPalettes[1];
        e->info.outline = fontPalettes[2];
        t->entry = e->info.serial;
        t->line = t->x = -1;
    }
    if (line != t->line || x <= t->x) {
        if (e->info.rows >= MAX_ROWS || !grow(e, e->info.rows + 1)) {
            t->kind = -1; /* this text: no more */
            return;
        }
        e->info.rows++;
    }
    t->line = line;
    t->x = x;
    /* This glyph's columns, and the one before it: the outline the printer
     * adds reaches a pixel to the left (printDialogCharacter). Never past
     * the glyph: the line image keeps an earlier text's pixels there. */
    int from = x * 4 > 0 ? x * 4 - 1 : 0, to = (x + width) * 4;
    uint8_t* row = e->pixels + (size_t)(e->info.rows - 1) * BACKLOG_LINE * e->info.stride;
    const uint16_t* words = image;
    for (int y = 0; y < BACKLOG_LINE; y++) {
        for (int p = from; p < to; p++) {
            unsigned v = words[y * stride + p / 4] >> ((p & 3) * 4);
            row[y * e->info.stride + p] = (uint8_t)((v >> (odd ? 2 : 0)) & 3);
        }
    }
    if (to > e->info.width) e->info.width = to;
}

/* ---- the controls (host) ---- */

/* port/src/ui/menu.cpp (weak: programs without the menu). */
extern void uiRequestBacklog(void) __attribute__((weak));

int backlogHoldsSelect(void) {
    return backlogChosen() && screenKind() >= 0;
}

/* pad.c, each vblank: port 1's buttons (live or scripted, before the game's
 * remapping). Select+Square, held together with one of them down this
 * vblank, opens the backlog (not inside the soft reset's Select+Start). */
#define PAD_SQUARE 0x0080u
#define PAD_SELECT 0x0100u
#define PAD_START 0x0800u
static unsigned lastButtons;
void backlogPad(unsigned buttons) {
    unsigned down = buttons & ~lastButtons, chord = PAD_SELECT | PAD_SQUARE;
    lastButtons = buttons;
    if ((buttons & chord) == chord && (down & chord) && !(buttons & PAD_START) && backlogChosen() &&
        uiRequestBacklog != NULL) {
        uiRequestBacklog();
    }
}

int backlogKey(int scancode, int mod) {
    (void)mod;
    if (scancode != SDL_SCANCODE_H || !backlogChosen() || uiRequestBacklog == NULL) {
        return 0;
    }
    uiRequestBacklog();
    return 1;
}

/* ---- captions ---- */

static char lastCaption[512];

void backlogCaption(const char* utf8) {
    if (utf8 == NULL || utf8[0] == '\0') {
        lastCaption[0] = '\0';
        return;
    }
    if (!backlogChosen() || strcmp(utf8, lastCaption) == 0) return;
    snprintf(lastCaption, sizeof lastCaption, "%s", utf8);
    if (hwInWorld()) return;
    Entry* e = newEntry(BACKLOG_MOVIE);
    size_t n = strlen(utf8) + 1;
    e->text = malloc(n);
    if (e->text != NULL) memcpy(e->text, utf8, n);
    finish(e->info.serial);
}
