#ifndef MAIN_DIALOG_H
#define MAIN_DIALOG_H

/* Dialog text (Noah: kernel/messageWindow.cpp): the text part of a dialog
 * window, its per-line sprites, the font and the text bundle. */

#include "common.h"
#include "psyq.h"

/* Noah: sDialogWindow18_8C, a queued string (allocateHeap(8) node). */
typedef struct DialogQueue {
    /* 0x0 */ struct DialogQueue* m0_pNext;
    /* 0x4 */ u8* m4_dialogPointer;
} DialogQueue; /* 0x8 */

/* Noah: sDialogWindow18PerLineBufferEntry, one text line: its two sprite
 * halves (256 + rest pixels) per display buffer and its VRAM image. */
typedef struct {
    /* 0x00 */ SPRT m0_sprt[2][2]; /* [buffer][left/right half] */
    /* 0x50 */ RECT m50_rect;      /* VRAM area of the line image */
    /* 0x58 */ s16 m58_widthDiv4;  /* printed width in 4-pixel units */
    /* 0x5A */ u8 m5A_oddLine;     /* image in the odd (upper) CLUT bits */
    /* 0x5B */ u8 m5B_line;        /* image line number */
    /* 0x5C */ u8 m5C_vramY;       /* v of the line image */
    /* 0x5D */ u8 m5D;
    /* 0x5E */ u16 m5E_clut;
} DialogWindow18Line; /* 0x60 */

/* Noah: sDialogWindow18, the text part of a dialog window (at +0x18 in the
 * field's sDialogWindow). 0x90 bytes: Noah's m90_dialogPointer is the
 * containing window's +0xA8 here, since main's scratch window renderStringWindow
 * has its one line renderStringLine right at +0x90.
 * m10_flags as the code uses them: 0x1 lines scroll (m16_firstLine moves),
 * 0x2 no background tile, 0x4 printing a string, 0x8 stopped (reason in
 * m6B_stopReason), 0x20 clear the lines before printing, 0x40 clear once
 * resumed, 0x80 printing a sub-string (m20_savedString), 0x100 three times
 * m68 characters this step, 0x200 close after m84_delay. */
typedef struct {
    /* 0x00 */ s16 m0_cursorX;     /* Noah: m0.vx, in 4-pixel units */
    /* 0x02 */ s16 m2_cursorLine;  /* Noah: m0.vy */
    /* 0x04 */ s16 m4_x;           /* Noah: m4 (screen position) */
    /* 0x06 */ s16 m6_y;
    /* 0x08 */ s16 m8_widthTimes4;
    /* 0x0A */ s16 mA_width1;      /* width in 4-pixel units, made odd */
    /* 0x0C */ s16 mC_height;      /* lines */
    /* 0x0E */ s16 mE_vramY;       /* VRAM y of the line images */
    /* 0x10 */ u16 m10_flags;      /* Noah: bitfield m01..m200, see below */
    /* 0x12 */ s16 m12_widthPadded;
    /* 0x14 */ s16 m14_lineHeight; /* Noah: m14.vx */
    /* 0x16 */ s16 m16_firstLine;  /* Noah: m14.vy, top line in m28 */
    /* 0x18 */ s16 m18_lineCount;
    /* 0x1A */ s16 m1A;
    /* 0x1C */ u8* m1C_currentStringToPrint;
    /* 0x20 */ u8* m20_savedString; /* resume point after a sub-string */
    /* 0x24 */ s32 m24;
    /* 0x28 */ DialogWindow18Line* m28_perLineBuffer; /* mC_height lines */
    /* 0x2C */ u16* m2C_inRamDialogTextImage;         /* one line, 4bpp */
    /* 0x30 */ DR_MODE m30_textTileDrawMode[2];
    /* 0x48 */ TILE m48_textTile[2];  /* background, per display buffer */
    /* 0x68 */ u8 m68_charsPerStep;
    /* 0x69 */ u8 m69_charsThisStep;
    /* 0x6A */ u8 m6A_savedCharsPerStep;
    /* 0x6B */ u8 m6B_stopReason;  /* 1 end of text, 2 wait for button, 3 close */
    /* 0x6C */ u8 m6C_autoClose;
    /* 0x6D */ u8 m6D;             /* set by escape 0x0F 0x0B */
    /* 0x6E */ u8 m6E_selectedLineForMultiChoice; /* 0xFF: none */
    /* 0x6F */ u8 m6F;
    /* 0x70 */ s32 m70_vars[4];    /* numbers for escapes 0x0F 0x09/0A/0C */
    /* 0x80 */ s16 m80_stringRef;            /* name for escape 0x0F 0x04: bank << 8 | index */
    /* 0x82 */ s16 m82_queueCount;
    /* 0x84 */ s16 m84_delay;
    /* 0x86 */ s16 m86_currentDelayForNextCharacter;
    /* 0x88 */ s16 m88_delayBetweenCharacters;
    /* 0x8A */ s16 m8A;
    /* 0x8C */ DialogQueue* m8C_queue;
} DialogWindow18; /* 0x90 */

/* Globals (bss outside dialog.c; Noah names in comments). */
extern u16 textSpriteMode0;          /* textSpriteMode0 (CLUT for even lines) */
extern u16 textSpriteMode1;          /* textSpriteMode1 (CLUT for odd lines) */
/* dialog.c's .data */
extern u16 fontPalettes[];        /* fontPalettes (two 16-colour CLUTs) */
extern u16 crossedBoxGlyph[];        /* glyph for character 0xFF 0xFF */

/* Line sprites and VRAM images for a width x height window at (x, y). */
void setupWindowSize2(DialogWindow18* p, s16 x1, s16 y1, s16 x, s16 y, s16 width, s16 height);

/* Font and text bundle */
u32 doPointerRelocation(u32* data);
void relocateOffsetTable(u32* data);
u8* getFontData(void);          /* font data */
u32* getTextBundle(void);         /* text bundle */
void freeFontData(void);         /* free the font */
void freeTextBundle(void);         /* free the text bundle */
void initFont(u8* font);
void setupPrintDialogTextVar(void* bundle);
void initFontAndTextBundle(u8* font, u32* bundle);
void initFontPalettes(s16 x, s16 y);

/* Strings in the text bundle */
u8* getDialogParamPointer(u8* buf, s32 idx);
u8 getDialogParam0(u16* p, s32 i);
u8 getDialogParam1(u8* p, s32 i);
u8* getDeathblowName(s32 bank, s32 idx);
u8* getTextBank16String(s32 idx);
u8* getAccessoryName(s32 idx);
u8* getUsableItemName(s32 idx);
u8* getWeaponName(s32 idx);
u8* getTextBank24String(s32 idx);
u8* getTextBank25String(s32 idx);
u8* getItemLabelString(s32 idx);
u8* drawCircleMenuChi_updateSub0Sub1(s32 idx);
u8* getTextBank19String(s32 idx);
u8* getTextBank21String(s32 idx);
u8* getTextBank27String(s32 idx);
u8* getGearDeathblowName(s32 bank, s32 idx);
u8* drawCircleMenuChi_updateSub0Sub2(s32 idx);
u8* getGearAccessoryName(s32 idx);
u8* getGearWeaponName(s32 idx);
u8* getGearFuelAbilityName(s32 idx);

/* Character codes <-> byte strings (code table in text bank 27) */
void dialogSpecialCase9AndASub0(u16* codes);
void decodeCharacterCodes(u16* codes, u8* out, u32 n);
s32 findCharacterCode(u8 a, u8 b);
s32 encodeStringToCharCodes(u8* s, u16* out);

/* Text printer */
u8 addDialogWindowsToOTSub0(DialogWindow18* p);
void dialogSpecialCase9AndA(s32 value, s32 base, s32 sign);
void dialogSpecialCase9AndA_2(DialogWindow18* p, u8* str);
void updateDialogTextImage(DialogWindow18* p);

/* Window */
void addDialogWindowsToOTSub1(DialogWindow18* w);
void resetDialogWindow18(DialogWindow18* w);
void dealocateDialogWindow18MemorySubSub(DialogWindow18* w);
void dealocateDialogWindow18MemorySub(DialogWindow18* w);
void dealocateDialogWindow18Memory(DialogWindow18* w);
s32 addDialogWindowsToOTSub2(DialogWindow18* w, u8* text);
s32 getWindowSubStructX(DialogWindow18* w);
s32 getWindowSubStructY(DialogWindow18* w);
void setupMultiChoiceColor(DialogWindow18* w, u8 r, u8 g, u8 b);
void setSelectedLineForMultiChoice(DialogWindow18* w, u8 line);
void clearSelectedLineForMultiChoice(DialogWindow18* w);
void updateAndRenderTextForDialogWindow(DialogWindow18* w, void* ot, s32 buf);
s32 renderString(u8* text, u16* image, s16 width, s32 odd);

/* Font glyphs */
u16 getDialogFontCharacterWidth(u16 type, u16 id);
void printDialogCharacter(u16 type, u16 id, u16* out, s16 stride, s32 odd);

#endif /* MAIN_DIALOG_H */
