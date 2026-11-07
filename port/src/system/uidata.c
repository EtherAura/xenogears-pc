/*
 * The game's own data that the settings menu (port/src/ui, C++) shows: read
 * here, in the platform layer, so that the link exports it from main
 * (tools/pc_link.py --platform; the menu's objects are not looked at).
 */

#include <stdint.h>

#include "common.h"

extern u16 fontPalettes[32]; /* src/main/dialog.c: the two text CLUTs */

/* The dialogue text's body and outline colours (index 1 and 2 of the CLUT
 * the game prints with, initFontPalettes). */
void uiGameFontColors(uint16_t* body, uint16_t* outline) {
    *body = fontPalettes[1];
    *outline = fontPalettes[2];
}
