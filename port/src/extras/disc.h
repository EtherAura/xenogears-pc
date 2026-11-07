#ifndef PORT_EXTRAS_DISC_H
#define PORT_EXTRAS_DISC_H

/*
 * The player's disc images for the extras (disc.c): any Xenogears disc, read
 * through the CD layer's image reader (port/src/cd/disc.c, a reader of its
 * own: the drive and the paused game's reads are untouched), and the game's
 * own file system on it (docs/ASSETS.md "Disc and file table"; tools/xenofs.py):
 *
 *   - the file table: 16 sectors from LBA 0x18, 7-byte entries (u24 LBA,
 *     s32 size; 0 empty; negative: a group of -size entries that follow it,
 *     read as one; LBA 0xFFFFFF ends it);
 *   - the directory table at LBA 0x28: u16 one-based first entries of 60
 *     directories (0xFFFF none), the disc number at index 0x3C. The game
 *     names a directory by (base, sub) and reads dirs[base + sub]; file F
 *     (from 1) of a directory is table entry dirs[d] - 1 + F - 1. A directory
 *     has no end of its own: here it runs to the next directory's start.
 *
 * File sizes and sectors come from the game's own lookups (filesystem.c's
 * setCurrentDirectory, getFileSize, getFileStartSector), run in a sandbox
 * (sandbox.h) over this disc's tables: they agree with the table read here
 * (xgDiscCheckGameLookups says so in the log).
 */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define XG_DISC_DIRS 0x3C /* directory slots; the disc number follows */

typedef struct XgDisc XgDisc;

typedef struct {
    int lba;
    int32_t size; /* bytes; 0 empty; < 0 a group of -size entries */
} XgEntry;

/* A disc image (.cue, or a raw 2352-byte-sector .bin); NULL when it is not
 * readable or has no Xenogears file table. *why gets the reason. */
XgDisc* xgDiscOpen(const char* path, const char** why);
void xgDiscClose(XgDisc* d);

const char* xgDiscPath(const XgDisc* d);
int xgDiscNumber(const XgDisc* d);       /* 1 or 2 (directory table 0x3C) */
const char* xgDiscRegion(const XgDisc* d); /* "US", "JP", "EU" or "?" (SYSTEM.CNF's boot file) */
const char* xgDiscBootName(const XgDisc* d); /* e.g. "SLUS_006.64" */
int xgDiscSectors(const XgDisc* d);
/* Stable locator identity: FNV-1a of the boot name, sector count and file /
 * directory tables. Same cue/bin image has the same identity. This is not
 * a cryptographic checksum of every asset byte. */
uint64_t xgDiscFingerprint(const XgDisc* d);

/* Table entries before the end marker. */
int xgDiscEntryCount(const XgDisc* d);
XgEntry xgDiscEntry(const XgDisc* d, int index);

/* Directory d's first table entry (0-based), -1 if it has none; its files
 * (up to the next directory's start); the table entry of file f (from 1). */
int xgDiscDirFirst(const XgDisc* d, int dir);
int xgDiscDirFiles(const XgDisc* d, int dir);
int xgDiscFileEntry(const XgDisc* d, int dir, int file);
/* The directory and file a table entry belongs to (the first directory that
 * holds it); 0 if none. */
int xgDiscEntryDir(const XgDisc* d, int index, int* dir, int* file);

/* Read a table entry's bytes, whole sectors (LZSS streams may run into the
 * padding); a group: all its members' extent. *size gets the entry's size
 * (the group's extent for a group). malloc'd; NULL on a read error or an
 * empty entry. */
uint8_t* xgDiscReadEntry(XgDisc* d, int index, int* size);
/* Raw sectors' user data (2048 bytes each), malloc'd. */
uint8_t* xgDiscReadSectors(XgDisc* d, int lba, int count);
/* One raw 2352-byte sector (sync, header, subheader, data): XA audio is
 * Mode 2 Form 2. 1 on success. */
int xgDiscReadRaw(XgDisc* d, int lba, uint8_t* out);

/* The game's own lookup of file `file` of directory `dir` (filesystem.c's
 * setCurrentDirectory, getFileStartSector, getFileSize, getNegativeFileSize
 * on this disc's tables, in a sandbox). 0 if the sandbox could not be made. */
int xgDiscGameLookup(XgDisc* d, int dir, int file, int* lba, int* size, int* groupCount);

/* The game's own lookups (filesystem.c in a sandbox) for every file of every
 * directory, compared with the table: the number of differences (0: they
 * agree), -1 if the sandbox could not be made. */
int xgDiscCheckGameLookups(XgDisc* d);

#ifdef __cplusplus
}
#endif

#endif /* PORT_EXTRAS_DISC_H */
