/*
 * BIOS B(51h) Krom2RawAdd: the Shift-JIS font in the BIOS ROM (psx-spx
 * "BIOS Character Sets"). The game draws a save's title on the memory card
 * screens (menu renderSaveTitleImage) and the text that scrolls over a movie
 * in the field (drawScrollingTextLineToVram, script opcode EX BEh) with it.
 *
 * The font is the player's own BIOS image, read at startup (or when the
 * settings menu picks another) and never copied anywhere:
 *   bios      (settings.ini; XG_BIOS overrides) the BIOS image (512 KB), or
 *             "none" for no font
 *   default   the first of the usual BIOS file names (scph5501.bin, ...,
 *             bios.bin) found beside the disc image, in the folder above
 *             it, or in the save folder (XG_CARD_DIR's default,
 *             SDL_GetPrefPath).
 * Without one, every character the BIOS has is a blank glyph.
 *
 * Two 16x15 charsets, 30 bytes a glyph (15 rows of 2 bytes, the left byte
 * first, bit 7 the leftmost pixel), back to back in the ROM:
 *   charset 2 at BFC66000h: 8140h..84BEh (symbols, digits, latin, kana,
 *             greek, cyrillic, box drawing) in 19 ranges, 524 glyphs;
 *   charset 3 at BFC69D68h: 889Fh..9872h (JIS level 1 kanji), 94 glyphs
 *             a kuten row.
 * Codes outside these two ranges are -1, as in the BIOS.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "common.h"
#include "settings.h"
#include "system.h"

#define BIOS_SIZE 0x80000
#define CHARSET2 0x66000 /* ROM offsets (BFC00000h + offset) */
#define CHARSET3 0x69D68
#define GLYPH_SIZE 30
#define MAX_PATH_LEN 4096

/* Charset 2's code ranges [first, end), in glyph order; the codes between
 * them are unassigned. */
static const struct {
    u16 first;
    u16 end;
} charset2[] = {
    {0x8140, 0x817F}, {0x8180, 0x81AD}, {0x81B8, 0x81C0}, {0x81C8, 0x81CF}, {0x81DA, 0x81E9},
    {0x81F0, 0x81F8}, {0x81FC, 0x81FD}, {0x824F, 0x8259}, {0x8260, 0x827A}, {0x8281, 0x829B},
    {0x829F, 0x82F2}, {0x8340, 0x837F}, {0x8380, 0x8397}, {0x839F, 0x83B7}, {0x83BF, 0x83D7},
    {0x8440, 0x8461}, {0x8470, 0x847F}, {0x8480, 0x8492}, {0x849F, 0x84BF},
};

static u8* rom; /* the player's BIOS image; NULL: none */
static char romPath[MAX_PATH_LEN]; /* where it came from; "": no glyphs */
static char discDir[MAX_PATH_LEN]; /* kromInit's disc path, for the default search */

static const char* const biosNames[] = {
#ifdef VERSION_JP
    "scph5500.bin", "scph7000.bin", "scph3500.bin", "scph3000.bin", "scph1000.bin",
    "scph5501.bin", "scph7001.bin", "scph1001.bin",
#else
    "scph5501.bin", "scph7001.bin", "scph1001.bin",
    "scph5500.bin", "scph7000.bin", "scph3500.bin", "scph3000.bin", "scph1000.bin",
#endif
    "scph5502.bin", "scph7502.bin", "scph1002.bin", "bios.bin",
};

/* Read a 512 KB BIOS image. Returns 1 on success. A second image goes into
 * the first one's memory: a glyph pointer the game holds stays valid. */
static int loadBios(const char* path, int quiet) {
    FILE* f = fopen(path, "rb");
    if (f == NULL) {
        if (!quiet) {
            fprintf(stderr, "bios: can't open %s\n", path);
        }
        return 0;
    }
    u8* data = malloc(BIOS_SIZE);
    size_t n = data != NULL ? fread(data, 1, BIOS_SIZE, f) : 0;
    int extra = fgetc(f) != EOF;
    fclose(f);
    if (n != BIOS_SIZE || extra) {
        fprintf(stderr, "bios: %s is not a 512 KB PS1 BIOS image; no kanji font\n", path);
        free(data);
        return 0;
    }
    if (rom != NULL) {
        memcpy(rom, data, BIOS_SIZE);
        free(data);
    } else {
        rom = data;
    }
    snprintf(romPath, sizeof romPath, "%s", path);
    fprintf(stderr, "bios: kanji font from %s\n", path);
    return 1;
}

/* Look for the usual BIOS names in `dir` (a path ending in a separator). */
static int findIn(const char* dir) {
    char path[MAX_PATH_LEN];
    for (size_t i = 0; i < sizeof biosNames / sizeof biosNames[0]; i++) {
        snprintf(path, sizeof path, "%s%s", dir, biosNames[i]);
        if (loadBios(path, 1)) {
            return 1;
        }
    }
    return 0;
}

/* `path` cut after its last separator, `up` more folders removed ("" for the
 * current folder); 0 if there is no such folder. */
static int folderOf(const char* path, int up, char* out, size_t size) {
    snprintf(out, size, "%s", path);
    for (int i = 0; i <= up; i++) {
        char* sep = NULL;
        for (char* p = out; *p != '\0'; p++) {
            if (*p == '/' || *p == '\\') {
                sep = p;
            }
        }
        if (sep == NULL) {
            out[0] = '\0';
            return i == 0;
        }
        if (i < up) {
            *sep = '\0';
        } else {
            sep[1] = '\0';
        }
    }
    return 1;
}

/* The default: the usual names beside the disc image, in the folder above
 * it, then in the save folder. */
static int searchDefault(void) {
    char dir[MAX_PATH_LEN];
    int found = 0;
    for (int up = 0; up < 2 && !found; up++) {
        found = folderOf(discDir, up, dir, sizeof dir) && findIn(dir);
    }
    if (!found) {
        char* pref = SDL_GetPrefPath(NULL, "xenogears");
        found = pref != NULL && findIn(pref);
        SDL_free(pref);
    }
    return found;
}

/* main.c: find and read the BIOS image, beside `discPath` by default. */
void kromInit(const char* discPath) {
    settingsDescribe("bios", "bios: the PS1 BIOS image for the kanji glyphs (512 KB; read where it is, never "
                             "copied), none, or empty to look beside the disc image");
    const char* env = settingsGet("bios");
    snprintf(discDir, sizeof discDir, "%s", discPath);
    if (env != NULL && env[0] != '\0') {
        if (strcmp(env, "none") != 0) {
            loadBios(env, 0);
        }
    } else {
        searchDefault();
    }
    if (rom == NULL) {
        fprintf(stderr, "bios: no BIOS image (XG_BIOS): kanji glyphs are blank\n");
    }
}

const char* kromPath(void) {
    return romPath[0] != '\0' ? romPath : NULL;
}

int kromSetBios(const char* path) {
    if (strcmp(path, "none") == 0) {
        if (rom != NULL) {
            memset(rom, 0, BIOS_SIZE); /* blank glyphs, at the same address */
        }
        romPath[0] = '\0';
    } else if (path[0] == '\0') {
        if (!searchDefault()) {
            return 0;
        }
    } else if (!loadBios(path, 0)) {
        return 0;
    }
    settingsSet("bios", path);
    settingsSave();
    return 1;
}

/* The glyph's offset in the ROM, or 0 for a code the BIOS has no glyph for. */
static u32 glyphOffset(u32 code) {
    if (code >= 0x8140 && code < 0x84BF) {
        u32 glyph = 0;
        for (size_t i = 0; i < sizeof charset2 / sizeof charset2[0]; i++) {
            if (code < charset2[i].first) {
                /* A gap: the BIOS (B(53h) Krom2Offset) indexes its range
                 * table with a stale stack byte here, so its answer
                 * depends on what ran before. No glyph. */
                return 0;
            }
            if (code < charset2[i].end) {
                return CHARSET2 + (glyph + code - charset2[i].first) * GLYPH_SIZE;
            }
            glyph += charset2[i].end - charset2[i].first;
        }
        return 0;
    }
    if (code >= 0x889F && code < 0x9873) {
        /* Two kuten rows a lead byte: trail bytes 40h..7Eh and 80h..9Eh (no
         * 7Fh), then 9Fh..FCh. The BIOS counts from 40h below 7Fh and from
         * 9Fh from 7Fh up, so 80h..9Eh land right after 7Eh, 7Fh shows 7Eh's
         * glyph and the other bad trail bytes a neighbouring row's. */
        s32 lead = code >> 8;
        s32 trail = code & 0xFF;
        s32 second = trail >= 0x7F;
        s32 row = 2 * (lead - 0x88) - 1 + second;
        return CHARSET3 + (u32)(row * 94 + trail - (second ? 0x9F : 0x40)) * GLYPH_SIZE;
    }
    return 0;
}

/* B(51h) Krom2RawAdd: the address of a Shift-JIS character's glyph, -1 if the
 * BIOS has none. The callers read 16 rows: the 16th is the next glyph's first,
 * as on the PS1 (the blank glyph has one too). */
u16* func_800405C4(s32 sjis) {
    static u16 blankGlyph[16];

    hwSafePoint();
    u32 offset = glyphOffset((u32)sjis & 0xFFFF);
    if (offset == 0) {
        return (u16*)-1;
    }
    return rom != NULL && romPath[0] != '\0' ? (u16*)(rom + offset) : blankGlyph;
}
