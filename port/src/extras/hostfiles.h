#ifndef PORT_EXTRAS_HOSTFILES_H
#define PORT_EXTRAS_HOSTFILES_H

/*
 * The game's file reads inside a sandbox, served from a disc image
 * (hostfiles.c).
 *
 * The game's file system (src/main/filesystem.c) has a second path besides
 * the drive: the SN debugger's host files. When `cdromFnd` points to a table
 * of 64-byte file names, one per file table entry, readFile, batch loads
 * (batchStartLoadingFiles) and image files (loadImageFileToVram) open the
 * file with PCopen and read it with PCread, and each read is done when the
 * call returns. xgHostFilesAttach, inside a sandbox (sandbox.h), points the
 * sandbox's copy of the file system at a disc's tables and at that path, and
 * serves PCopen/PCread from the disc image (port/src/system/bios.c
 * biosSetHostFiles). Game code that loads files then works in the sandbox
 * as it does in the game, without the drive (the paused game's) and without
 * interrupts (masked there).
 *
 *   XgHostFiles* files = xgHostFilesCreate(disc);   // once
 *   xgSandboxEnter(sb);
 *   xgHostFilesAttach(files);   // after each xgSandboxReset too: it resets main's data
 *   ... setCurrentDirectory(4, 0); readFile(...); ...
 *   xgHostFilesDetach(files);   // before leaving
 *   xgSandboxLeave(sb);
 *
 * Every open succeeds (a file the table does not have reads as zeros; with
 * every slot taken, the oldest open file's is reused): a failed host read
 * ends in the game's read-error screen, which loops forever.
 */

#include "disc.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct XgHostFiles XgHostFiles;

/* The disc's tables are read here; NULL if they cannot be. */
XgHostFiles* xgHostFilesCreate(XgDisc* disc);
void xgHostFilesDestroy(XgHostFiles* h);

/* Inside a sandbox: the file system's tables (fatFileTableBuffer,
 * fatDirectoryTableBuffer), its state as initCDAndFileSystem leaves it in
 * host mode, cdromFnd, and the PC calls. */
void xgHostFilesAttach(XgHostFiles* h);
/* Before leaving the sandbox: the PC calls fail again. */
void xgHostFilesDetach(XgHostFiles* h);

/* Every file open now closed (after xgSandboxReset: the files the old
 * world left open, streams it never stopped, are not the new world's). */
void xgHostFilesCloseAll(XgHostFiles* h);

/* Files opened since it was made, and the last one's table entry (tests). */
/* Distinct successfully served table entries, including streamed terrain.
 * Return the full count; write at most capacity entries when non-NULL. */
int xgHostFilesEntries(const XgHostFiles* h, int* entries, int capacity);
int xgHostFilesOpened(const XgHostFiles* h, int* lastEntry);

#ifdef __cplusplus
}
#endif

#endif /* PORT_EXTRAS_HOSTFILES_H */
