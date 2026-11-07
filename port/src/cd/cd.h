#ifndef PORT_CD_H
#define PORT_CD_H

/*
 * The CD-ROM drive (port/src/cd): the player's disc image, a drive that
 * answers controller commands with the PS1's interrupt timing, and LIBCD on
 * top of it as the game uses it.
 * The optional Speedup setting accelerates ordinary data loads to 8x and
 * preserves streamed/movie/audio timing. It is off by default.
 *
 * Settings (environment):
 *   XG_CD_SPEED   multiplier for data loads (ReadN, seeks, Pause/Stop/spin-up
 *                 times); default 1 = the real drive. Streamed reads (ReadS,
 *                 XA-ADPCM) always run at the real rate. Speedup raises the
 *                 ordinary data-load multiplier to at least 8.
 *   XG_CD_LOG     1: log commands and interrupts to stderr; 2: sectors too.
 *   XG_DISC2      the second disc's image (default: beside the first, swap.c);
 *                 settings.ini disc2, which the settings menu sets
 *   XG_DISC_SWAP  auto (default): swap discs when the game asks; manual: F2
 *                 (or the menu) only; settings.ini disc_swap
 */

#include <stdint.h>

#define CD_RAW_SECTOR 2352

/* Open the disc image (.cue, or a raw 2352-byte-sector .bin) as the disc in
 * the drive. Returns 1 on success. Called by main.c at startup. */
int cdOpenDisc(const char* cuePath);

/* Swap discs: the shell opens, the new image goes in, and the drive reports
 * it as the PS1 does (stat bit 4 until a Nop). Returns 1 on success. */
int cdChangeDisc(const char* cuePath);

/* swap.c: the game's second disc. cdOpenDisc records the first disc and
 * finds the other one (XG_DISC2, or the "Disc 2" image beside it); the game
 * gets it when it waits for the shell to open (XG_DISC_SWAP=auto, default),
 * or cdSwapDisc puts it in at any time (the F2 key). */
void cdSwapInit(const char* firstDisc);
int cdSwapDisc(void);                  /* open the shell, put in the other disc; 1 on success */
int cdSwapWanted(void);                /* the game waits for a disc: it stopped the drive and polls the shell */
int cdSwapRequest(void);               /* F2 and the menu: cdSwapDisc only while wanted; -1 when not wanted */
/* The other disc put in for a reset (the extras' cutscene replay starts the
 * game on the disc its scene wants: system/options.c portStartPrepare): no
 * shell event, as if swapped while the console was off (cdInsertDisc). */
int cdSwapForReset(void);
int cdInsertDisc(const char* cuePath);
void cdSwapNoteCommand(uint8_t com);   /* drive.c: each controller command, for the automatic swap */
/* The settings menu (port/src/ui): the image in the drive (0) and the other
 * one (1, NULL if none); another other disc ("" for the default beside the
 * first; 0 if unreadable or an incompatible disc pair), kept as disc2;
 * a rejected selection leaves the existing path/settings intact.
 * Automatic swaps on or off. */
const char* cdSwapPath(int which);
int cdSwapSetOther(const char* path);
int cdSwapAutomatic(void);
void cdSwapSetAutomatic(int on);

/* boot.c: a run that starts on disc 2 uses that disc's file tables, as its
 * own executable would (kernelMain passes main's embedded tables; only the
 * first call, at power-on, acts). cdBootDiscNumber: the disc in the drive
 * (1, 2), 0 if not a Xenogears disc. */
void cdBootTables(uint8_t* fileTable, uint16_t* directoryTable);
int cdBootDiscNumber(void);
/* The next cdBootTables (a soft reset after cdSwapForReset) reads the tables
 * of the disc in the drive, whichever it is. */
void cdBootTablesReload(void);

/* Number of sectors on the disc (LBA 0 = 00:02:00), 0 without a disc. */
int cdDiscSectorCount(void);
int cdDiscIsJapanese(void); /* mounted image, including private preview drives */

/* Read the raw 2352-byte sector at `lba`. Returns 1 on success. */
int cdReadRawSector(int lba, uint8_t out[CD_RAW_SECTOR]);

/* world.c: a drive of a world of its own, for the extras (the gallery plays
 * movies from a disc image of its own with the game's own video player). It
 * has its own disc, LIBCD state (callbacks, interrupts, the CD audio mix),
 * streaming ring and XA decoder history; its commands are not seen by the
 * automatic disc swap or the slowdown replay. Swap it in only inside a world
 * of its own (port/src/system/world.h): its events wait on that world's queue
 * and its XA audio goes to that world's SPU (cdWorldSpuInput turns the SPU's
 * CD input on there, as CdInit does). The drive the game uses, and its disc,
 * are not touched. cdWorldSwap puts it in place and, called again, back. */
typedef struct CdWorld CdWorld;
CdWorld* cdWorldCreate(const char* discPath); /* NULL: the image cannot be read */
void cdWorldDestroy(CdWorld* w);
void cdWorldSwap(CdWorld* w);
void cdWorldSpuInput(void);

/* The CD audio mix of CdMix (L->L, L->R, R->R, R->L; 0x80 = 1.0), for the
 * SPU's CD input. */
void cdGetAudioMix(uint8_t atv[4]);

#endif /* PORT_CD_H */
