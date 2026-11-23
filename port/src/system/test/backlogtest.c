/* The dialogue backlog's capture (xg-htn.57; system/backlog.c), linked whole
 * with the settings, fed the printer's calls as src/main/dialog.c makes them
 * (pcBacklogText at a text's start, pcBacklogLine for each glyph drawn into
 * the window's line image: 13 rows of 4-bit pixels, the even line of a pair
 * in bits 0-1, the odd one in bits 2-3). No disc and no game.
 *
 *   - off (the default): nothing kept, no controls, Select the game's;
 *   - an entry from a text's first glyph: a row per printed line, the
 *     line's pixels as printed (its pair half), nothing past the glyphs;
 *   - the same text again right after itself is kept once; a window that
 *     prints nothing leaves nothing; two windows print side by side;
 *   - the screens: field, world map and battle kept, the menus, Battling and
 *     the extras' worlds not; the port's own prompts never;
 *   - captions: each new one once;
 *   - the newest BACKLOG_ENTRIES kept; off forgets them;
 *   - the controls: Select+Square (not with Start), H, and Select held as
 *     the port's chord button where text is kept.
 */
#include <SDL3/SDL.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pc_backlog.h"
#include "pc_wide.h"
#include "system/backlog.h"
#include "system/settings.h"

static int checks;
#include "test/check.h"

/* What backlog.c reads of the game and the port. */
uint16_t fontPalettes[32];
static int kind = PC_WIDE_FIELD, world;
int pcWideGetMode(void) { return kind; }
int hwInWorld(void) { return world; }
static int requests;
void uiRequestBacklog(void) { requests++; }

#define STRIDE 16 /* halfwords a row: 64 pixels */
typedef struct {
    uint16_t image[13 * STRIDE];
} Window;

/* A glyph `width` units (4 pixels each) wide at `x` on `line`: its pixels
 * value v (1 letter, 2 outline) in the pair half `odd`, the other half
 * left as it was. */
static void glyph(Window* w, int x, int width, int line, int odd, unsigned v) {
    for (int y = 0; y < 13; y++) {
        for (int p = x * 4; p < (x + width) * 4; p++) {
            uint16_t* word = &w->image[y * STRIDE + p / 4];
            int shift = (p & 3) * 4 + (odd ? 2 : 0);
            *word = (uint16_t)((*word & ~(3u << shift)) | ((((unsigned)p + (unsigned)y) % 2 ? v : 0) << shift));
        }
    }
    pcBacklogLine(w, w->image, STRIDE, x, width, line, odd);
}

static unsigned pixel(const BacklogEntry* e, int row, int y, int x) {
    return e->pixels[(size_t)(row * BACKLOG_LINE + y) * e->stride + x];
}

/* A text of `rows` lines of `glyphs` glyphs each, every glyph's value v. */
static void text(Window* w, int rows, int glyphs, unsigned v) {
    pcBacklogText(w);
    for (int r = 0; r < rows; r++) {
        for (int g = 0; g < glyphs; g++) glyph(w, g * 2, 2, r % 4, r % 2, v);
    }
}

static int newest(BacklogEntry* e) {
    return backlogGet(backlogCount() - 1, e);
}

int main(void) {
    SDL_setenv_unsafe("XG_SETTINGS", "none", 1);
    SDL_unsetenv_unsafe("XG_DIALOGUE_BACKLOG");
    fontPalettes[1] = 0x7FFF;
    fontPalettes[2] = 0x2108;
    static Window a, b, c;
    BacklogEntry e;

    /* Off: nothing kept, no controls. */
    CHECK(backlogChosen() == 0);
    text(&a, 2, 3, 1);
    CHECK(backlogCount() == 0);
    backlogCaption("A caption");
    CHECK(backlogCount() == 0);
    backlogPad(0x0180);
    backlogPad(0);
    CHECK(requests == 0 && backlogKey(SDL_SCANCODE_H, 0) == 0 && requests == 0);
    CHECK(backlogHoldsSelect() == 0);

    /* On: a text's rows, as printed. */
    backlogSetChosen(1);
    CHECK(backlogChosen() == 1 && backlogHoldsSelect() == 1);
    pcBacklogText(&a);
    CHECK(backlogCount() == 0); /* nothing printed yet */
    glyph(&a, 0, 2, 0, 0, 1);
    glyph(&a, 2, 3, 0, 0, 2);
    glyph(&a, 0, 1, 1, 1, 1); /* the next line: its pair's odd half */
    a.image[5 * STRIDE + 10] = 0xFFFF; /* stale pixels past the glyphs (an earlier text's) */
    glyph(&a, 1, 1, 1, 1, 1);
    CHECK(backlogCount() == 1 && newest(&e));
    CHECK(e.kind == BACKLOG_FIELD && e.rows == 2 && e.width == 20 && e.stride == STRIDE * 4 && e.text == NULL);
    CHECK(e.body == 0x7FFF && e.outline == 0x2108);
    CHECK(pixel(&e, 0, 0, 1) == 1 && pixel(&e, 0, 0, 0) == 0 && pixel(&e, 0, 1, 0) == 1); /* (x + y) odd: the value */
    CHECK(pixel(&e, 0, 0, 9) == 2 && pixel(&e, 0, 1, 9) == 0);
    CHECK(pixel(&e, 1, 0, 1) == 1 && pixel(&e, 1, 0, 2) == 0 && pixel(&e, 1, 1, 2) == 1); /* the odd half */
    for (int x = 8; x < 64; x++) CHECK(pixel(&e, 1, 5, x) == 0); /* nothing past the glyphs */
    CHECK(pixel(&e, 0, 5, 40) == 0 && pixel(&e, 1, 12, 8) == 0);

    /* The same text again: kept once. Another: kept. A window that prints
     * nothing: nothing. */
    text(&a, 2, 3, 1);
    text(&a, 2, 3, 1);
    pcBacklogText(&a);
    int n = backlogCount();
    CHECK(n == 2); /* the first entry differs (its glyphs' widths and values) */
    text(&a, 3, 2, 2);
    pcBacklogText(&a);
    CHECK(backlogCount() == 3 && newest(&e) && e.rows == 3);
    pcBacklogText(&b);
    pcBacklogText(&b);
    CHECK(backlogCount() == 3);
    /* A text said again later, after another, is kept again. */
    text(&a, 2, 3, 1);
    pcBacklogText(&a);
    CHECK(backlogCount() == 4);

    /* Two windows side by side: each its own entry and rows. */
    pcBacklogText(&a);
    pcBacklogText(&b);
    glyph(&a, 0, 2, 0, 0, 1);
    glyph(&b, 0, 2, 0, 0, 2);
    glyph(&a, 0, 2, 1, 1, 1);
    glyph(&b, 2, 2, 0, 0, 2);
    CHECK(backlogCount() == 6);
    BacklogEntry ea, eb;
    CHECK(backlogGet(4, &ea) && backlogGet(5, &eb));
    CHECK(ea.rows == 2 && ea.width == 8 && eb.rows == 1 && eb.width == 16);

    /* The screens. */
    static const int keptKinds[] = {PC_WIDE_FIELD, PC_WIDE_WORLDMAP, PC_WIDE_BATTLE};
    static const int keptAs[] = {BACKLOG_FIELD, BACKLOG_WORLDMAP, BACKLOG_BATTLE};
    for (int i = 0; i < 3; i++) {
        kind = keptKinds[i];
        n = backlogCount();
        text(&c, 1, 1 + i, 1);
        CHECK(backlogCount() == n + 1 && newest(&e) && e.kind == keptAs[i]);
        CHECK(backlogHoldsSelect() == 1);
    }
    static const int notKept[] = {PC_WIDE_KERNEL, PC_WIDE_MENU, PC_WIDE_BATTLING, PC_WIDE_MOVIE};
    for (int i = 0; i < 4; i++) {
        kind = notKept[i];
        n = backlogCount();
        text(&c, 1, 5, 2);
        CHECK(backlogCount() == n && backlogHoldsSelect() == 0);
    }
    kind = PC_WIDE_FIELD;
    world = 1; /* an extras sandbox */
    n = backlogCount();
    text(&c, 1, 5, 2);
    CHECK(backlogCount() == n && backlogHoldsSelect() == 0);
    world = 0;
    pcBacklogIgnore(&b); /* the port's prompt */
    text(&b, 2, 2, 2);
    text(&b, 1, 4, 1);
    CHECK(backlogCount() == n);
    text(&c, 1, 6, 2);
    CHECK(backlogCount() == n + 1);

    /* Captions: each new one once. */
    n = backlogCount();
    backlogCaption("We'll meet again.");
    backlogCaption("We'll meet again.");
    CHECK(backlogCount() == n + 1 && newest(&e) && e.kind == BACKLOG_MOVIE && e.pixels == NULL);
    CHECK(strcmp(e.text, "We'll meet again.") == 0);
    backlogCaption(NULL);
    backlogCaption("We'll meet again."); /* the same one again right after: once */
    CHECK(backlogCount() == n + 1);
    backlogCaption("");
    backlogCaption("Fei!");
    CHECK(backlogCount() == n + 2 && newest(&e) && strcmp(e.text, "Fei!") == 0);

    /* The newest BACKLOG_ENTRIES. */
    for (int i = 0; i < BACKLOG_ENTRIES + 40; i++) {
        pcBacklogText(&c);
        for (int g = 0; g <= i % 14; g++) glyph(&c, g, 1, 0, 0, 1 + (unsigned)(i / 14) % 2);
    }
    pcBacklogText(&c);
    CHECK(backlogCount() == BACKLOG_ENTRIES);
    BacklogEntry first, last;
    CHECK(backlogGet(0, &first) && newest(&last) && last.serial - first.serial == BACKLOG_ENTRIES - 1);
    CHECK(!backlogGet(BACKLOG_ENTRIES, &e) && !backlogGet(-1, &e));

    /* The controls. */
    backlogPad(0);
    backlogPad(0x0100); /* Select */
    CHECK(requests == 0);
    backlogPad(0x0180); /* ... and Square */
    CHECK(requests == 1);
    backlogPad(0x0180); /* held: once */
    backlogPad(0);
    backlogPad(0x0980); /* with Start: the soft reset's buttons */
    CHECK(requests == 1);
    backlogPad(0);
    CHECK(backlogKey(SDL_SCANCODE_H, 0) == 1 && requests == 2);
    CHECK(backlogKey(SDL_SCANCODE_J, 0) == 0 && requests == 2);

    /* Off forgets. */
    backlogSetChosen(0);
    CHECK(backlogCount() == 0 && backlogChosen() == 0 && backlogHoldsSelect() == 0);
    text(&a, 1, 2, 1);
    CHECK(backlogCount() == 0);
    CHECK(backlogKey(SDL_SCANCODE_H, 0) == 0 && requests == 2);

    printf("backlogtest: %d checks passed\n", checks);
    return 0;
}
