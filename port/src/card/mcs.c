/*
 * Single saves and the card command line:
 *
 *   xenogears --card list   CARD
 *   xenogears --card export CARD NAME OUT.mcs
 *   xenogears --card import CARD FILE [--replace]
 *   xenogears --card delete CARD NAME
 *   xenogears --card format CARD
 *
 * CARD is 1 or 2 (the images the game uses, card.h) or the path of a raw
 * 128 KB image (.mcd/.mcr). NAME is a file name on the card, as `list` shows
 * it (e.g. BASLUS-006640).
 *
 * The settings menu (port/src/ui) lists, exports and imports saves on the
 * cards in the slots through the same functions (cardListSaves,
 * cardExportSave, cardImportSave).
 *
 * Formats (psx-spx "Memory Card Images"):
 *   .mcs      the first block's 128-byte directory frame, then the save's
 *             blocks in order (import reads only the name and size from the
 *             frame, as psx-spx recommends);
 *   54-byte header (Xploder, Action Replay): 21 bytes of name, 33 of title,
 *             then the blocks; import only, recognised by its size.
 */

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "card.h"

static uint8_t image[CARD_SIZE];
static char imagePath[1024];

static int openCard(const char* which) {
    if (strcmp(which, "1") == 0 || strcmp(which, "2") == 0) {
        int slot = which[0] - '1';
        cardImagesInit();
        if (!cards[slot].present) {
            fprintf(stderr, "card %s: the slot is empty (XG_CARD%s=none)\n", which, which);
            return 0;
        }
        snprintf(imagePath, sizeof(imagePath), "%s", cards[slot].path);
        memcpy(image, cards[slot].data, CARD_SIZE);
        cards[slot].dirty = 0; /* this command writes the image itself */
        return 1;
    }
    snprintf(imagePath, sizeof(imagePath), "%s", which);
    if (!cardLoadFile(which, image, CARD_SIZE)) {
        FILE* f = fopen(which, "rb");
        if (f != NULL) {
            fclose(f);
            fprintf(stderr, "%s: not a 128 KB memory card image\n", which);
            return 0;
        }
        cardFormatImage(image); /* a new image */
    }
    return 1;
}

static int saveCard(void) {
    if (!cardSaveImage(imagePath, image)) {
        return 0;
    }
    printf("wrote %s\n", imagePath);
    return 1;
}

/* The title frame's Shift-JIS title as ASCII (the letters, digits and
 * punctuation saves use; anything else becomes '?'). */
static char sjisPunct(int b) {
    static const struct {
        uint8_t code;
        char ch;
    } table[] = {{0x40, ' '}, {0x43, ','}, {0x44, '.'}, {0x46, ':'}, {0x47, ';'}, {0x48, '?'}, {0x49, '!'},
                 {0x51, '_'}, {0x5E, '/'}, {0x5F, '\\'}, {0x60, '~'}, {0x69, '('}, {0x6A, ')'}, {0x6D, '['},
                 {0x6E, ']'}, {0x7B, '+'}, {0x7C, '-'}, {0x81, '='}, {0x93, '%'}, {0x94, '#'}, {0x95, '&'},
                 {0x96, '*'}, {0x97, '@'}};
    for (size_t i = 0; i < sizeof(table) / sizeof(table[0]); i++) {
        if (table[i].code == b) {
            return table[i].ch;
        }
    }
    return '?';
}

static void titleAscii(const uint8_t* title, char* out, size_t size) {
    size_t o = 0;
    for (int i = 0; i < 64 && title[i] != 0 && o + 1 < size;) {
        uint8_t a = title[i];
        if (a < 0x80) {
            out[o++] = (char)a;
            i++;
            continue;
        }
        uint8_t b = i + 1 < 64 ? title[i + 1] : 0;
        char ch = '?';
        if (a == 0x82 && b >= 0x4F && b <= 0x58) {
            ch = (char)('0' + b - 0x4F);
        } else if (a == 0x82 && b >= 0x60 && b <= 0x79) {
            ch = (char)('A' + b - 0x60);
        } else if (a == 0x82 && b >= 0x81 && b <= 0x9A) {
            ch = (char)('a' + b - 0x81);
        } else if (a == 0x81) {
            ch = sjisPunct(b);
        }
        out[o++] = ch;
        i += 2;
    }
    out[o] = '\0';
}

static int fileSizeOf(const uint8_t* frame) {
    return frame[4] | frame[5] << 8 | frame[6] << 16;
}

static void say(char* err, size_t size, const char* fmt, ...) __attribute__((format(printf, 3, 4)));
static void say(char* err, size_t size, const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    if (err != NULL && size > 0) {
        vsnprintf(err, size, fmt, ap);
    }
    va_end(ap);
}

int cardListSaves(const uint8_t* data, CardSave* out, int max) {
    if (!cardIsFormatted(data)) {
        return -1;
    }
    int count = 0;
    for (int b = 1; b < CARD_BLOCKS && count < max; b++) {
        const uint8_t* f = data + b * CARD_FRAME;
        if (f[0] != DIR_FIRST) {
            continue;
        }
        CardSave* s = &out[count++];
        int blocks[CARD_BLOCKS];
        s->block = b;
        memcpy(s->name, f + 0x0A, CARD_NAME_MAX);
        s->name[CARD_NAME_MAX] = '\0';
        s->blocks = cardFileBlocks(data, b, blocks, CARD_BLOCKS);
        titleAscii(data + b * CARD_BLOCK + 4, s->title, sizeof(s->title));
    }
    return count;
}

static int cmdList(void) {
    CardSave saves[CARD_BLOCKS];
    int n = cardListSaves(image, saves, CARD_BLOCKS);
    if (n < 0) {
        printf("%s: not formatted\n", imagePath);
        return 0;
    }
    printf("%s: %d free blocks\n", imagePath, cardFreeBlocks(image));
    for (int i = 0; i < n; i++) {
        printf("  block %2d  %-20s  %2d block%s  %s\n", saves[i].block, saves[i].name, saves[i].blocks,
               saves[i].blocks == 1 ? " " : "s", saves[i].title);
    }
    return 1;
}

int cardExportSave(const uint8_t* data, const char* name, const char* out, char* err, size_t errSize) {
    int first = cardFindFile(data, name);
    if (first == 0 || strchr(name, '?') != NULL || strchr(name, '*') != NULL) {
        say(err, errSize, "no file %s on the card", name);
        return 0;
    }
    const char* dot = strrchr(out, '.');
    if (dot == NULL || (strcmp(dot, ".mcs") != 0 && strcmp(dot, ".MCS") != 0)) {
        say(err, errSize, "%s: export writes .mcs files", out);
        return 0;
    }
    int blocks[CARD_BLOCKS];
    int n = cardFileBlocks(data, first, blocks, CARD_BLOCKS);
    if (n < 0) {
        say(err, errSize, "%s has a broken block chain", name);
        return 0;
    }
    size_t size = CARD_FRAME + (size_t)n * CARD_BLOCK;
    uint8_t* buf = malloc(size);
    memcpy(buf, data + first * CARD_FRAME, CARD_FRAME);
    for (int i = 0; i < n; i++) {
        memcpy(buf + CARD_FRAME + (size_t)i * CARD_BLOCK, data + blocks[i] * CARD_BLOCK, CARD_BLOCK);
    }
    int ok = cardSaveFile(out, buf, size);
    free(buf);
    if (!ok) {
        say(err, errSize, "cannot write %s", out);
        return 0;
    }
    say(err, errSize, "exported %s (%d block%s) to %s", name, n, n == 1 ? "" : "s", out);
    return 1;
}

static int findOrComplain(const char* name) {
    int first = cardFindFile(image, name);
    if (first == 0 || strchr(name, '?') != NULL || strchr(name, '*') != NULL) {
        fprintf(stderr, "%s: no file %s\n", imagePath, name);
        return 0;
    }
    return first;
}

static int cmdExport(const char* name, const char* out) {
    char msg[1200];
    int ok = cardExportSave(image, name, out, msg, sizeof(msg));
    if (ok) {
        printf("%s\n", msg);
    } else {
        fprintf(stderr, "%s: %s\n", imagePath, msg);
    }
    return ok;
}

static uint8_t* readAll(const char* path, size_t* size) {
    FILE* f = fopen(path, "rb");
    if (f == NULL) {
        return NULL;
    }
    size_t cap = 0x40000, n = 0;
    uint8_t* buf = malloc(cap);
    size_t got;
    while ((got = fread(buf + n, 1, cap - n, f)) > 0) {
        n += got;
        if (n == cap) {
            cap *= 2;
            buf = realloc(buf, cap);
        }
    }
    fclose(f);
    *size = n;
    return buf;
}

int cardImportSave(uint8_t* data, const char* path, int replace, char* err, size_t errSize) {
    size_t size;
    uint8_t* buf = readAll(path, &size);
    if (buf == NULL) {
        say(err, errSize, "%s: cannot read", path);
        return 0;
    }
    char name[CARD_NAME_MAX + 1] = {0};
    const uint8_t* blocks = NULL;
    int nblocks = 0;
    if (size > CARD_FRAME && (size - CARD_FRAME) % CARD_BLOCK == 0) {
        /* .mcs: the name and size from the directory frame */
        memcpy(name, buf + 0x0A, CARD_NAME_MAX);
        nblocks = fileSizeOf(buf) / CARD_BLOCK;
        blocks = buf + CARD_FRAME;
        if (nblocks != (int)((size - CARD_FRAME) / CARD_BLOCK)) {
            nblocks = 0;
        }
    } else if (size > 54 && (size - 54) % CARD_BLOCK == 0) {
        /* Xploder / Action Replay */
        memcpy(name, buf, CARD_NAME_MAX);
        nblocks = (int)((size - 54) / CARD_BLOCK);
        blocks = buf + 54;
    }
    if (nblocks < 1 || nblocks > CARD_BLOCKS - 1 || name[0] == '\0' || blocks[0] != 'S' || blocks[1] != 'C') {
        say(err, errSize, "%s: not a single save (.mcs, or a 54-byte header and whole blocks)", path);
        free(buf);
        return 0;
    }
    if (!cardIsFormatted(data)) {
        say(err, errSize, "the card is not formatted");
        free(buf);
        return 0;
    }
    int old = cardFindFile(data, name);
    if (old != 0 && !replace) {
        say(err, errSize, "%s is already there", name);
        free(buf);
        return -1;
    }
    /* Room first, so a failed replace leaves the old save. */
    int oldBlocks[CARD_BLOCKS];
    int have = old != 0 ? cardFileBlocks(data, old, oldBlocks, CARD_BLOCKS) : 0;
    if (cardFreeBlocks(data) + (have > 0 ? have : 0) < nblocks) {
        say(err, errSize, "%d free blocks, %s needs %d", cardFreeBlocks(data) + (have > 0 ? have : 0), name, nblocks);
        free(buf);
        return 0;
    }
    if (old != 0) {
        cardDeleteFile(data, old, NULL);
    }
    int first = cardCreateFile(data, name, nblocks, NULL);
    if (first == 0) {
        say(err, errSize, "%d free blocks, %s needs %d", cardFreeBlocks(data), name, nblocks);
        free(buf);
        return 0;
    }
    int chain[CARD_BLOCKS];
    cardFileBlocks(data, first, chain, CARD_BLOCKS);
    for (int i = 0; i < nblocks; i++) {
        memcpy(data + chain[i] * CARD_BLOCK, blocks + (size_t)i * CARD_BLOCK, CARD_BLOCK);
    }
    free(buf);
    say(err, errSize, "imported %s (%d block%s) at block %d", name, nblocks, nblocks == 1 ? "" : "s", first);
    return 1;
}

static int cmdImport(const char* path, int replace) {
    char msg[1200];
    int ok = cardImportSave(image, path, replace, msg, sizeof(msg));
    if (ok <= 0) {
        fprintf(stderr, "%s: %s%s\n", imagePath, msg, ok < 0 ? " (--replace overwrites it)" : "");
        return 0;
    }
    printf("%s\n", msg);
    return saveCard();
}

static int cmdDelete(const char* name) {
    int first = findOrComplain(name);
    if (first == 0) {
        return 0;
    }
    cardDeleteFile(image, first, NULL);
    printf("deleted %s\n", name);
    return saveCard();
}

static int usage(void) {
    fprintf(stderr, "usage: xenogears --card list CARD\n"
                    "       xenogears --card export CARD NAME OUT.mcs\n"
                    "       xenogears --card import CARD FILE [--replace]\n"
                    "       xenogears --card delete CARD NAME\n"
                    "       xenogears --card format CARD\n"
                    "CARD: 1 or 2 (the game's cards: XG_CARD1/XG_CARD2, XG_CARD_DIR) or an image file\n");
    return 2;
}

/* argv[0] is the command. Returns the process exit status. */
int cardCommand(int argc, char** argv) {
    if (argc < 2) {
        return usage();
    }
    const char* cmd = argv[0];
    cardQuiet = 1;
    if (!openCard(argv[1])) {
        return 1;
    }
    int ok;
    if (strcmp(cmd, "list") == 0 && argc == 2) {
        ok = cmdList();
    } else if (strcmp(cmd, "export") == 0 && argc == 4) {
        ok = cmdExport(argv[2], argv[3]);
    } else if (strcmp(cmd, "import") == 0 && (argc == 3 || (argc == 4 && strcmp(argv[3], "--replace") == 0))) {
        ok = cmdImport(argv[2], argc == 4);
    } else if (strcmp(cmd, "delete") == 0 && argc == 3) {
        ok = cmdDelete(argv[2]);
    } else if (strcmp(cmd, "format") == 0 && argc == 2) {
        cardFormatImage(image);
        ok = saveCard();
    } else {
        return usage();
    }
    return ok ? 0 : 1;
}
