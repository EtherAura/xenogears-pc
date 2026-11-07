#ifndef PORT_EXTRAS_TABLES_INT_H
#define PORT_EXTRAS_TABLES_INT_H

/*
 * The tables' own pieces shared by tables*.c (tables.h is the API).
 */

#include <stdint.h>

#include "disc.h"

/* The disc to read: disc `number` (1 or 2) when the tables have it, else
 * the other; NULL with no disc. */
XgDisc* tablesDisc(int number);
/* The disc's region: 'U' (US), 'J' (JP) or 0. */
int tablesRegionOf(XgDisc* d);

/* File `file` of directory `dir` (whole sectors read, `*size` the file's
 * own size), malloc'd; NULL if none. */
uint8_t* tablesRead(XgDisc* d, int dir, int file, int* size);
/* The same, LZSS-decompressed by the game's own decoder (files.h). */
uint8_t* tablesReadLzss(XgDisc* d, int dir, int file, int* size);

/* A string kept for the tables' lifetime (until xgTablesSetDisc). */
const char* tablesKeep(const char* s);

/* The game's dialogue text (its own character codes: the US font's narrow
 * glyphs) as UTF-8 into out; 0 when it holds a code we cannot spell (the
 * JP font's kana and kanji), with out "". */
int tablesGameText(const uint8_t* text, int max, int region, char* out, int outSize);
/* Shift-JIS as UTF-8 (the platform's converter; full-width Latin letters,
 * digits and signs become plain ASCII), trailing spaces cut. malloc'd. */
char* tablesSjisToUtf8(const uint8_t* s, int n);

/* Called by xgTablesSetDisc: drop what tables_sound.c and tables_battle.c keep. */
void tablesSoundReset(void);
void tablesBattleReset(void);

uint32_t tablesRd32(const uint8_t* p);
uint16_t tablesRd16(const uint8_t* p);

#endif
