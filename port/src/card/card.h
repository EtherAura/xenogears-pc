#ifndef PORT_CARD_H
#define PORT_CARD_H

/*
 * Memory cards (port/src/card).
 *
 * Two raw 128 KB card images, the format PCSX-Redux, Mednafen and DuckStation
 * read (.mcd/.mcr): 16 blocks of 8 KB, 64 frames of 128 bytes each; block 0 is
 * the directory (psx-spx "Memory Card Data Format").
 *
 *   card1, card2         (settings.ini; XG_CARD1, XG_CARD2 override) image of
 *                        slot 1 / 2, "none" for an empty slot, empty or unset
 *                        for the default image; the settings menu changes
 *                        them during the run (cardSetSlot: a card swap)
 *   XG_CARD_DIR          directory of the default images card1.mcd and
 *                        card2.mcd (default: SDL's per-user data directory,
 *                        e.g. ~/.local/share/xenogears/ on Linux,
 *                        %APPDATA%\xenogears\ on Windows)
 *
 * A missing image is a freshly formatted card, written to disk the first time
 * the game changes it. The game reaches the cards through LIBCARD and the
 * BIOS "bu" device (bu.c), which by default keeps the BIOS's timing: one slot served per
 * vblank, one sector per access (psx-spx "Memory Card Functions", measured
 * on the real BIOS in Beetle PSX).
 * The opt-in Speedup setting completes new card accesses immediately while
 * preserving their data, results and ordering (system/speedup.h).
 */

#include <stddef.h>
#include <stdint.h>

#define CARD_SIZE 0x20000
#define CARD_FRAME 0x80
#define CARD_BLOCK 0x2000
#define CARD_FRAMES (CARD_SIZE / CARD_FRAME)
#define CARD_BLOCKS 16
#define CARD_NAME_MAX 20 /* filename characters in a directory frame */

/* Directory frame (block 0, frames 1..15) allocation states. */
#define DIR_FIRST 0x51
#define DIR_MIDDLE 0x52
#define DIR_LAST 0x53
#define DIR_FREE 0xA0
#define DIR_DELETED_FIRST 0xA1
#define DIR_DELETED_MIDDLE 0xA2
#define DIR_DELETED_LAST 0xA3

typedef struct {
    uint8_t data[CARD_SIZE];
    int present;   /* a card is in the slot */
    int flagNew;   /* the card's FLAG bit 3: set on insertion, cleared by a write */
    int dirty;     /* changed since it was last written to its file */
    char path[1024];
} Card;

extern Card cards[2];
extern int cardQuiet; /* no slot messages (the command line) */

/* image.c */
void cardImagesInit(void);                 /* once: find and load both images */
void cardFormatImage(uint8_t* data);       /* a freshly formatted card, as emulators create it */
int cardIsFormatted(const uint8_t* data);  /* "MC" in the header frame */
void cardSetFrameChecksum(uint8_t* frame); /* byte 7Fh = XOR of 00h..7Eh */
int cardFrameChecksumOk(const uint8_t* frame);
int cardFlush(int slot);                   /* write the image if it changed; 0 on error */
int cardLoadFile(const char* path, uint8_t* data, size_t size); /* exactly size bytes */
int cardSaveFile(const char* path, const uint8_t* data, size_t size);
/* Each changed image keeps its previous disk contents in <path>.backups/.
 * History is limited to ten versions and follows the image, not the slot. */
#define CARD_BACKUPS 10
typedef struct {
    char name[32];
    int64_t modified; /* SDL_Time, nanoseconds since the Unix epoch */
} CardBackup;
int cardSaveImage(const char* path, const uint8_t* data);
int cardListBackups(int slot, CardBackup* out, int max);
int cardRestoreBackup(int slot, const char* name, char* err, size_t errSize);

/* Directory helpers on an image (fs.c). */
uint8_t* cardDirFrame(uint8_t* data, int block);  /* block 1..15 */
int cardFindFile(const uint8_t* data, const char* name); /* first block, or 0 */
int cardFileBlocks(const uint8_t* data, int first, int* blocks, int max); /* the chain; count or -1 */
int cardFreeBlocks(const uint8_t* data);
int cardCreateFile(uint8_t* data, const char* name, int nblocks, int* changedFrames); /* first block or 0 */
int cardDeleteFile(uint8_t* data, int first, int* changedFrames); /* frames changed */
int cardNameMatch(const char* pattern, const char* name);

/* mcs.c: single saves and the command line. */
int cardCommand(int argc, char** argv);
typedef struct {
    int block;                    /* first block */
    char name[CARD_NAME_MAX + 1]; /* e.g. BASLUS-00664... */
    int blocks;                   /* -1: a broken chain */
    char title[80];               /* the title frame's Shift-JIS title as ASCII */
} CardSave;
int cardListSaves(const uint8_t* data, CardSave* out, int max); /* count, -1 if not formatted */
/* One save as an .mcs file; 1 on success. err gets a message either way. */
int cardExportSave(const uint8_t* data, const char* name, const char* path, char* err, size_t errSize);
/* An .mcs (or 54-byte header) save into an image: 1 done, 0 failed, -1 a save
 * of that name is there and replace is 0. err gets a message either way. */
int cardImportSave(uint8_t* data, const char* path, int replace, char* err, size_t errSize);

/* The settings menu (port/src/ui), while the game is paused. */
int cardBusy(int slot);           /* bu.c: the game is in an access or has a file open on the slot */
/* image.c: put another card in a slot (path: an image, "" the default one,
 * "none" no card): the old one is written back, the new one loaded (a new
 * formatted card if the file is missing) and reads as newly inserted, and
 * card1/card2 is kept in settings.ini. 0 with a message in err on failure
 * (the slot is left as it was). */
int cardSetSlot(int slot, const char* path, char* err, size_t errSize);
/* image.c: the menu changed a card's contents (an import): written now, and
 * newly inserted for the game, which reads its directory again. */
int cardChangedByMenu(int slot); /* 0 if its image/backup could not be written */

#endif /* PORT_CARD_H */
