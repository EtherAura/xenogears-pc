#ifndef PORT_BACKLOG_H
#define PORT_BACKLOG_H

/*
 * The dialogue backlog (backlog.c; xg-htn.57): the text the game has shown,
 * kept so it can be read again (port/src/ui/backlog.cpp draws it). Off by
 * default; dialogue_backlog in settings.ini, F1 > Gameplay > Dialogue
 * backlog.
 *
 * What is kept: every text the game's printer puts in a window on the field,
 * on the world map and in a battle's event dialogue (src/main/dialog.c, both
 * engines: include/pc_backlog.h), as the printed pixels of its lines (names,
 * numbers, Japanese, text packs and translations as they were shown), and a
 * movie's captions as text. Menus and the port's own prompts are not kept;
 * nor are the extras' sandboxes. A text the same as the one before it (the
 * world map's place names, a line said again) is kept once.
 *
 * Host state: what the player saw, never in a save state. The game thread
 * only (the printer, the movie's frames, and the menu that shows it, which
 * runs inside the vblank).
 */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BACKLOG_ENTRIES 256 /* the newest kept */
#define BACKLOG_LINE 13     /* a text row's pixel lines: the printer's line image (11 of glyph and the outline) */

enum { BACKLOG_FIELD, BACKLOG_WORLDMAP, BACKLOG_BATTLE, BACKLOG_MOVIE };

typedef struct {
    uint32_t serial;        /* 1, 2, ... in the order the texts came */
    int kind;               /* BACKLOG_* */
    int rows;               /* text rows */
    int width;              /* pixels: the widest row's printed width */
    int stride;             /* bytes from one pixel line to the next */
    const uint8_t* pixels;  /* rows * BACKLOG_LINE pixel lines: 0 clear, odd the letters, even the outline */
    uint16_t body, outline; /* the printer's colours then (fontPalettes, 15-bit) */
    const char* text;       /* a caption (BACKLOG_MOVIE: UTF-8; no pixels) */
} BacklogEntry;

/* The setting: 1 on, 0 off (the default; nothing is kept). */
int backlogChosen(void);
void backlogSetChosen(int on); /* saved; off also forgets what was kept */

int backlogCount(void);
/* index 0 the oldest kept, backlogCount() - 1 the newest; 0 past the end.
 * The pointers stay good until the next text comes or the backlog clears. */
int backlogGet(int index, BacklogEntry* out);
void backlogClear(void);

/* port/src/cd/regional.c, at each movie frame: the caption shown now (NULL
 * or "" for none). */
void backlogCaption(const char* utf8);

/* The controls that open it (port/src/ui/menu.cpp uiRequestBacklog), with
 * the option on: pad.c's key events (H, unmodified, when no PS1 button has
 * it: 1 when it took the key) and each vblank's buttons (Select+Square). */
int backlogKey(int scancode, int mod);
void backlogPad(unsigned buttons);
/* selectchord.c: on the screens whose text is kept, with the option on,
 * Select is the port's chord button (Select+Square is not the game's). */
int backlogHoldsSelect(void);

#ifdef __cplusplus
}
#endif

#endif /* PORT_BACKLOG_H */
