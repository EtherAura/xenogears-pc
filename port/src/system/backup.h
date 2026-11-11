/*
 * Saves backup (system/backup.c; xg-mrj.3.7): the save folder (SDL_GetPrefPath:
 * settings.ini, the memory card images and their history, the save states)
 * as one .zip file the player puts where they like (the system's file
 * dialog), and back. Android keeps an app's own folder only as long as the
 * app is installed; a backup outlives it.
 */
#ifndef XG_SYSTEM_BACKUP_H
#define XG_SYSTEM_BACKUP_H

#include <SDL3/SDL.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Ask where to write the backup, then write it (on a thread of its own). */
void backupExport(SDL_Window* window);
/* Ask for a backup, then restore it over the save folder (the game reads it at its next start). */
void backupImport(SDL_Window* window);
/* The last result ("" while nothing happened); error: it failed. 1 while one runs. */
int backupStatus(char* text, size_t size, int* error);

/* The work itself, for tests: the folder `dir` into/from the zip at `zip`. 0 and *why on failure. */
int backupWrite(const char* dir, const char* zip, int* files, char* why, size_t size);
int backupRead(const char* zip, const char* dir, int* files, char* why, size_t size);

#ifdef __cplusplus
}
#endif

#endif
