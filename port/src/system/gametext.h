#ifndef PORT_SYSTEM_GAMETEXT_H
#define PORT_SYSTEM_GAMETEXT_H

/*
 * The game's text from another source than the disc in the drive, by
 * category (xg-htn.35.22; the user's choice: one per category):
 *
 *   dialogue      field dialogue (each field's section 7: texts and window
 *                 sizes) and the world map's place texts
 *   battle        enemy and attack names, the battle's item names and labels
 *   menus         the menu's labels and descriptions (10/xx)
 *   names         the kernel's names (01/07): items, weapons, accessories,
 *                 deathblows, chi, Gear parts
 *
 * Sources (settings.ini text_dialogue, text_battle, text_menus, text_names;
 * XG_TEXT_<CATEGORY> overrides):
 *   disc          the disc in the drive (default): nothing is looked up
 *   other         the other release's disc: Japanese text on the US build,
 *                 English on the JP build (text_other_disc1/2, else the
 *                 Japanese discs of F1 > Audio on the US build, else an image
 *                 beside the game's discs)
 *   image         a text image (text_image1/2): a translation patch (PPF,
 *                 xdelta...) the player applied to their own image. The game
 *                 runs from the verified image as always (a patched image fails
 *                 system/gamedata.c's check: patches change code modules too);
 *                 the text image is only read, as the other release's disc is:
 *                 its text tables, window sizes and font (the glyphs a patch
 *                 added come along). A patch's code (a variable-width font, a
 *                 longer window's code) does not apply: its text shows in the
 *                 game's own printer. Texts with codes the printer does not
 *                 know, and fields whose scripts the patch changed, keep the
 *                 disc's text.
 *   pack:<id>     a text pack in <save folder>/packs/<id>/ (gametext_pack.c)
 *
 * How a text is found (src/main/dialog.c asks: pcGameTextString, include/
 * pc_gametext.h): a table the game reads is the current field's dialogue
 * (field.c's rawFieldDialogBundle, checked against the retail field's), or
 * one of the disc's other text tables, known by a fingerprint (its header and
 * first entry) from the retail images. The same table of the source gives
 * the entry of the same index, for a field only where the two releases'
 * scripts open their dialogue windows with the same texts in the same order
 * (measured, US and JP: 923 of 935 fields; the others keep the disc's text). Its
 * glyphs become the running font's where that font has the same picture
 * (kana, Latin, signs), else codes FF C0..FE xx (no disc has them) that bring
 * the source font's glyph along (pcGameTextGlyph). Anything that does not
 * check out keeps the disc's text, never blank.
 */

#include <stddef.h>
#include <stdint.h>

enum { GT_DIALOGUE, GT_BATTLE, GT_MENUS, GT_NAMES, GT_CATEGORIES };
enum { GT_SOURCE_DISC, GT_SOURCE_OTHER, GT_SOURCE_IMAGE, GT_SOURCE_PACK };

/* ---- the settings (gametext.c) ---------------------------------------------------------- */

void gameTextInit(void);                   /* read the settings (main.c, or on first use) */
int gameTextSource(int category);          /* GT_SOURCE_* */
const char* gameTextPack(int category);    /* the pack's id for GT_SOURCE_PACK */
void gameTextSetSource(int category, int source, const char* pack); /* and settings.ini */
const char* gameTextCategoryKey(int category); /* "dialogue", ... */
const char* gameTextOtherRegion(void);     /* "JP" on the US build, "US" on the JP build */
/* A line for F1: where the source's text comes from, or why it cannot. */
const char* gameTextStatus(int category);
/* text_image1/2 changed (F1): read again. */
void gameTextImageChanged(void);
/* Counts since the source was chosen: texts given, texts that kept the disc's. */
void gameTextCounts(int category, int* given, int* kept);
/* Pictures with text (gametext_picture.c): the menus' and the battle's word
 * pictures from the category's source too (text_<category>_pictures, off by
 * default; the next time the game loads them: a menu opened, a battle). */
int gameTextHasPictures(int category);
int gameTextPictures(int category);
void gameTextSetPictures(int category, int on);
/* The category's source is a pack: its opt-ins (pack.ini: print=, windows=). */
typedef struct GtLayout GtLayout;
int gameTextPackLayout(int category, GtLayout* out);

/* Inside the text system (gametext.c), for gametext_picture.c. */
typedef struct GtImage GtImage;
typedef struct GtPack GtPack;
unsigned gtGeneration(void);            /* changes with any source */
int gtCurrentDisc(void);                /* 1 or 2 */
GtImage* gtRetailImage(int disc);       /* the running release's */
GtImage* gtCategoryImage(int category, int disc); /* its source's image (other, text image), or NULL */
GtPack* gtCategoryPack(int category);   /* its source's pack, or NULL */

/* ---- images (gametext_disc.c): read-only, never the drive's -------------------------------- */

GtImage* gtImageOpen(const char* path, char* why, size_t size);
void gtImageClose(GtImage* image);
const char* gtImagePath(const GtImage* image);
const char* gtImageRegion(const GtImage* image); /* "US" or "JP" */
int gtImageDisc(const GtImage* image);           /* 1 or 2 */
uint8_t* gtImageFile(GtImage* image, int dir, int file, int* size); /* malloc'd; NULL: none */
uint8_t* gtLzss(const uint8_t* src, int size, int* outSize);       /* malloc'd; NULL: no stream */
uint8_t* gtFieldSection(GtImage* image, int field, int section, int* size);
uint8_t* gtImageFont(GtImage* image, int* size); /* 01/06, decompressed */
/* Member `member` (from 1) of an offset bundle file, as stored (malloc'd),
 * its size in *size; the GT_MEMBER_SLACK bytes after it in the file follow
 * (zeros past the file's end): an LZSS member may read into them. */
#define GT_MEMBER_SLACK 64
uint8_t* gtImageMember(GtImage* image, int dir, int file, int member, int* size);

/* A text table: u32 header, u16 offsets from +4, the texts. */
typedef struct {
    const uint8_t* base;
    int size, count;
} GtTable;
int gtTableParse(const uint8_t* base, int size, GtTable* out); /* 1: a valid table */
const uint8_t* gtTableEntry(const GtTable* table, int index, int* length);
/* Its fingerprint: the header and offsets (to 132 bytes) and the first
 * entry up to its end code (256 bytes at most). size < 0: unknown (a
 * table in the game's memory). */
uint64_t gtFingerprint(const uint8_t* base, int size);

/* The disc's text tables but the fields', by key. */
enum { GT_KIND_KERNEL, GT_KIND_MENU, GT_KIND_BATTLE_COMMON, GT_KIND_BATTLE_SET, GT_KIND_WORLD, GT_KINDS };
typedef struct {
    int kind, file, member; /* the file in its kind's directory; the bundle member (from 1) or 0 */
} GtKey;
int gtKeyCategory(const GtKey* key); /* GT_* or -1 (a table no category takes: the code table, buttons) */
typedef void (*GtVisit)(void* user, const GtKey* key, const uint8_t* table, int size);
int gtImageTables(GtImage* image, GtVisit visit, void* user); /* the number visited */
uint8_t* gtImageTable(GtImage* image, const GtKey* key, int* size); /* malloc'd from the table's start */

/* ---- fonts and text (gametext.c) --------------------------------------------------------- */

typedef struct {
    const uint8_t* data;
    int size;
    int lead, first, narrow, narrow2, two, count; /* header +4, +12, +8, +10, +6/22; glyphs */
    const uint8_t* bits;                          /* the glyphs, 22 bytes each */
} GtFont;
int gtFontParse(const uint8_t* data, int size, GtFont* out);
int gtFontDecode(const GtFont* font, const uint8_t* s, int* length); /* glyph index; -1 not a glyph */
int gtFontEncode(const GtFont* font, int index, uint8_t out[2]);     /* the code's length */
int gtFontWide(const GtFont* font, int index);

/* Source font slots: 0 the other release's, 1 a text image's, 2.. packs'.
 * Encoded text keeps immutable glyph snapshots across slot changes. */
enum { GT_BANK_OTHER, GT_BANK_IMAGE, GT_BANK_PACK, GT_BANK_FONTS = 8 };
void gtBankSetFont(int slot, const GtFont* font);
/* Encode a copied glyph and width; 0 if the bounded snapshot store is full. */
int gtBankEncodeGlyph(int slot, const GtFont* font, int index, uint8_t out[3]);
/* For each glyph of `from`, the same picture in `to` (-1: none); malloc'd. */
int16_t* gtFontEquivalents(const GtFont* from, const GtFont* to);

/* A text in `from`'s codes as `to`'s: control codes copied, glyphs mapped
 * (equiv) or brought along from bank slot `bank`. Returns the length with
 * its end code, 0 when the text does not check out (an unknown code). */
int gtTranscode(const uint8_t* src, int length, const GtFont* from, const int16_t* equiv, int bank,
                const GtFont* to, uint8_t* out, int cap);

/* The proportional print (print=proportional): a pen in pixels along a
 * line of the printer's 4-bit line image (two planes: even and odd lines). */
typedef struct {
    int px;  /* the next glyph's left outline */
    int end; /* one past the last pixel used */
    int ink; /* a glyph has ink on this line (blanks before it keep their width) */
} GtPen;
int gtGlyphInk(const uint16_t* glyph, int* first, int* last); /* its inked columns; 0: blank */
/* Move the pen over a glyph of `units` (the font's 2 or 3; NULL: a blank):
 * its left outline's pixel, -1 for a blank. */
int gtPenGlyph(GtPen* pen, const uint16_t* glyph, int units, int spacing, int space, int* first, int* last);
void gtDrawGlyph(uint16_t* image, int stride, int odd, int x, const uint16_t* glyph, int first, int last,
                 uint8_t* indices);
void gtClearColumns(uint16_t* image, int stride, int odd, int from, int to); /* pixels from..to, 13 rows */

/* ---- packs (gametext_pack.c) ------------------------------------------------------------- */

/* pack.ini's opt-ins (gametext_pack.c's header): the game's look without them. */
enum { GT_WINDOWS_NATIVE, GT_WINDOWS_PACK, GT_WINDOWS_GROW, GT_WINDOWS_SOURCE };
struct GtLayout {
    int proportional;   /* print=proportional */
    int windows;        /* GT_WINDOWS_* */
    int spacing, space; /* pixels between glyphs (1..3), a blank's (2..12) */
};
void gtLayoutParse(const char* ini, GtLayout* out);

/* Write the text of the images (a release's two discs; either may be NULL)
 * as a pack in `dir`: pack.ini and one UTF-8 file per kind; fieldOk (or
 * NULL: all) says which fields' dialogue goes in. */
int gameTextExport(GtImage* disc1, GtImage* disc2, const char* dir, const char* name,
                   int (*fieldOk)(int disc, int field, void* user), void* user, char* why, size_t size);
/* F1: the running release's text (which 0) or the other release's (1, only
 * the fields whose scripts fit this release's) as a pack in the pack folder:
 * <region>-<language>; its id in `id`. which 2: the text images' (a
 * translation patch) as image-<file name>, with their own font: a private
 * pack to edit, the image no longer needed. */
int gameTextExportPack(int which, char* id, size_t idSize, char* why, size_t size);
int gtSameDialogue(GtImage* a, GtImage* b, int field);
void gtKeyName(const GtKey* key, int disc, char* out, size_t size); /* "kernel/1/23" ... */

/* <save folder>/packs (XG_PACK_DIR overrides; "none": no packs). */
const char* gtPackDir(void);
int gtPackList(char ids[][64], char names[][128], int max);
GtPack* gtPackLoad(const char* id, char* why, size_t size);
const char* gtPackFind(const GtPack* pack, const char* key, int* w, int* h); /* its text, or NULL */
const char* gtPackName(const GtPack* pack);
const char* gtPackId(const GtPack* pack);
const char* gtPackRegion(const GtPack* pack);   /* whose tables its keys are: "US", "JP" */
const char* gtPackFontName(const GtPack* pack); /* the font of its <g:N>: "us", "jp", "pack" */
/* font=pack: its own font and its glyphs' characters (font.map); NULL: none. */
const GtFont* gtPackOwnFont(const GtPack* pack, const uint16_t** uni);
void gtPackLayout(const GtPack* pack, GtLayout* out);
const uint16_t* gtFontUnicode(const char* region, int* count); /* glyph -> code point (0 unknown) */
/* A retail release's ("US", "JP") text as UTF-8 (a new line as a space, the
 * crossed box as U+25A1): 0 when it holds a control code or a glyph with no
 * character (the extras' names; no image needed). */
int gtTextToUtf8(const char* region, const uint8_t* text, int max, char* out, int size);
/* The glyph of a release's font with that character (its first), or -1;
 * *wide: 12 pixels (else 8). */
int gtUnicodeGlyph(const char* region, unsigned codepoint, int* wide);
int gtGlyphWide(const char* region, int glyph); /* 12 pixels wide (else 8) */

typedef struct {
    const GtFont* to;          /* the running font */
    const uint16_t* toUni;
    int toUniCount;
    const GtFont* bankFont;    /* a font whose glyphs come along (or NULL) */
    const uint16_t* bankUni;
    int bankUniCount, bankSlot;
    int gIsTo;                 /* <g:N> are the running font's glyphs (else the bank font's) */
    int bankFirst;             /* a character the bank font has comes from it (a pack's own font) */
    const int16_t* bankEquiv;  /* a bank glyph drawn as the running font's (the same picture), or NULL */
} GtEncode;
/* A pack's text as codes; 0 when it does not check out. */
int gtPackEncode(const char* text, const GtEncode* enc, uint8_t* out, int cap);

/* ---- pictures with text (gametext_picture.c) ----------------------------------------------- */

/* Write the release's pictures with text as indexed PNGs in <dir>/pictures
 * (disc 2's where they differ), plus commented sprite-placement templates;
 * returns the number of PNGs written. */
int gtPictureExport(GtImage* disc1, GtImage* disc2, const char* dir, int sourceMembers);
enum { GT_PICTURE_TIMS, GT_PICTURE_TEXTURES, GT_PICTURE_SPRITES };
#define GT_PICTURE_MEMBER_MAX (2u * 1024u * 1024u)
/* Private image packs retain picture geometry/palettes as well as PNGs.
 * Snapshots bind to the running disc member and validate the whole payload. */
uint8_t* gtPictureSnapshot(const uint8_t* base, int baseSize, const uint8_t* data, int size,
                           int kind, int* outSize);
uint8_t* gtPictureRestore(const uint8_t* base, int baseSize, const uint8_t* file, int size,
                          int kind, int* outSize);
/* Optional sprite placement (gametext_sprite.c). The table is the running
 * disc's decompressed font/sprite table. Layout rows change only draw x/y.
 * A NULL result with empty why means no active overrides; any invalid row
 * rejects the whole layout. Returned data/text is malloc'd. */
#define GT_SPRITE_LAYOUT_MAX (256u * 1024u)
uint64_t gtSpriteFingerprint(const uint8_t* table, int size);
uint8_t* gtSpriteLayoutApply(const uint8_t* table, int size, const char* text, size_t length,
                            int* outSize, char* why, size_t whySize);
char* gtSpriteLayoutTemplate(const uint8_t* table, int size, int* length);
/* An 8-bit indexed PNG (RGBA palette); a font's glyphs as a sheet in dir/name. */
int gtPngWrite(const char* path, int w, int h, const uint8_t* indices, const uint8_t (*palette)[4], int colors);
int gtFontSheet(const GtFont* font, const char* dir, const char* name);

/* ---- a text image's pack (gametext_pack.c) ---------------------------------------------- */

/* A font's glyphs as characters by `base`'s: the same picture, or redrawn in
 * the same place (malloc'd, font->count entries, 0 unknown). */
uint16_t* gtDerivedUnicode(const GtFont* font, const GtFont* base, const uint16_t* baseUni, int baseUniCount);
/* The text of a text image (a translation patch) as a pack with its own font
 * (font.bin, font.map, font.png; pack.ini font=pack, windows=source):
 * characters known by the base font's pictures, the rest <g:N> of the pack's
 * font. Source window sizes retain the game's script precedence. */
int gameTextExportImage(GtImage* disc1, GtImage* disc2, const GtFont* base, const uint16_t* baseUni, int baseUniCount,
                        const char* dir, const char* name, int (*fieldOk)(int disc, int field, void* user), void* user,
                        char* why, size_t size);

#endif
