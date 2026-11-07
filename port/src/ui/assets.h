// The settings menu's look, read from the player's disc when the menu first
// opens (assets.cpp): nothing of the game is in this program's own files.
//
//   - The menu resource file (directory 0x10, file 1): its TIM images go into
//     a private copy of VRAM, and its sprite table (the game's "menu font":
//     window frame pieces, cursors, buttons, the menu's own letters) says
//     which piece of VRAM each sprite shows, through which CLUT.
//   - The dialogue font (directory 1, file 6): 11-row 1-bit glyphs that the
//     game prints with an outline, 8 or 12 pixels apart (src/main/dialog.c).
//
// The disc is read through the drive's image (cd.h cdReadRawSector) with its
// own file tables, so the other disc in the drive works the same.

#ifndef PORT_UI_ASSETS_H
#define PORT_UI_ASSETS_H

#include <stdint.h>

namespace assets {

// One picture: RGBA, alpha 0 where the PS1 draws nothing (texel 0), 255 for
// an opaque texel, 128 for a semi-transparent one (STP set, drawn at 50%).
struct Image {
    int w = 0, h = 0;
    int ox = 0, oy = 0; // where the sprite's origin is inside the picture (the game places sprites by it)
    uint32_t* rgba = nullptr;
};

// Reads everything once; true when the disc gave it all.
bool load();
bool loaded();

// Sprite `id` of the menu sprite table as one picture (its pieces placed as
// the game places them), or nullptr. semi: STP texels at half alpha (the
// game draws the sprite semi-transparent), else opaque.
const Image* sprite(int id, bool semi);

// A rectangle of the menu's VRAM through a CLUT: depth 0 4-bit, 1 8-bit;
// tpage the texture page's VRAM x, y; u, v inside the page.
Image vramImage(int depth, int tpageX, int tpageY, int clutX, int clutY, int u, int v, int w, int h, bool semi);

// The dialogue font: the game's character code for a Unicode code point
// (-1: none), and that character as the game prints it: a 14 x 13 picture of
// palette indices (0 nothing, 1 body, 2 outline), whose pen advances
// `advance` pixels (8 or 12). The picture starts one pixel left of the pen.
// Japanese OST characters use the disc's native kana/kanji (a nearby JP
// image supplies US-disc kanji), with an outlined pixel fallback for absent
// glyphs. menu_jp_disc can name that image explicitly or disable discovery.
int fontCode(uint32_t codepoint);
bool fontGlyph(int code, uint8_t pixels[13][14], int* advance);

// The text colours (fontPalettes, main's data): body and outline, 15-bit.
void fontColors(uint16_t* body, uint16_t* outline);

// The game's 15-bit colour as RGBA (alpha as for Image).
uint32_t rgba(uint16_t c, bool semi);

// File `file` (from 1) of directory `dir` of the disc in the drive, whole
// sectors; nullptr if there is none. Freed with free().
uint8_t* readDiscFile(int dir, int file, int* size);

} // namespace assets

#endif
