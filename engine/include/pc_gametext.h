#ifndef PC_GAMETEXT_H
#define PC_GAMETEXT_H

/*
 * The game's text from another source (port/src/system/gametext.h), as the
 * game C sees it: src/main/dialog.c asks here before it takes a string, a
 * window size or a glyph from its own data. Nothing is asked while every
 * category's text is the disc's (pcGameTextOn 0): default play is untouched.
 */

#include "common.h"

extern int pcGameTextOn;

/* Read the player's choices (the game's text bundle is in: setupPrintDialogTextVar). */
void pcGameTextInit(void);

/* Entry `index` of the text table at `table` from the chosen source, in the
 * running font's codes; NULL: the table's own. */
u8* pcGameTextString(u8* table, s32 index);

/* A field dialogue's window size from the chosen source (which 0: width,
 * 1: height); -1: the table's own. */
s32 pcGameTextWindow(u8* table, s32 index, s32 which);

/* A successfully supplied pack entry with windows=pack/grow owns its window
 * size, ahead of field-script overrides. Native layout keeps the script's. */
s32 pcGameTextWindowOverridesScript(u8* table, s32 index);

/* At `s` (FF C0..FE xx, codes no disc uses): a glyph the source brought (the
 * other release's font, a pack's): its 11 rows of the dialogue font's format,
 * and its width in 4-pixel units; NULL when `s` is not one. */
const u16* pcGameTextGlyph(const u8* s, u16* width);

/* A member the game decompresses (mallocAndDecompress): the menus' or the
 * battle's word pictures (or their sprite table) from the chosen source when
 * the player opted in, in a heap block of `flags`; NULL: the disc's. */
u8* pcGameTextMember(void* src, s32 flags);

/* A pack that prints proportionally (its pack.ini's opt-in) gave a text:
 * the printer asks pcGameTextPrint for each glyph. 0 until then. */
extern int pcGameTextProportionalOn;
extern int pcGameTextWrapOn;

/* A window (DialogWindow18) starts printing `text` (the queue's next text,
 * renderString's): proportional when it is such a pack's. */
void pcGameTextBegin(const void* window, const u8* text);

/* A grown pack window: move a fitting word to the next line at whitespace.
 * Updates text/cursor and returns 1 when the printer should advance its line. */
s32 pcGameTextWrap(const void* window, u8** text, s16* cursorX, s32 width);
/* Available field text columns after reserving a portrait's area. Applied
 * only to opted-in grown windows; other text keeps its native width. */
void pcGameTextFieldWindow(const void* window, s32 width);
s32 pcGameTextPrintWidth(const void* window, s32 width);

/* Print a glyph (11 rows; NULL: one the game draws itself, as a button's, at
 * a whole unit) of `units` (2 or 3) proportionally into the window's line
 * image (stride in words, plane odd), moving *cursorX (4-pixel units: what
 * the game measures with) past it. 1 printed, 0 not this window's (the
 * game prints it), -1 past `width` (the cursor moved past it: the next line
 * takes the glyph, as with the game's own print). */
s32 pcGameTextPrint(const void* window, const u16* glyph, s32 units, u16* image, s32 stride, s32 odd, s16* cursorX,
                    s32 width);

#endif
