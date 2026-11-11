/*
 * The Android app's platform parts (system/android.c; xg-mrj.3.7): the packs
 * folder and the activity's own actions (port/android XgActivity: the folder
 * pickers, which the system shows, and copying packs in).
 */
#ifndef XG_SYSTEM_ANDROID_H
#define XG_SYSTEM_ANDROID_H

#include <stddef.h>

#include <SDL3/SDL.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifdef __ANDROID__
/* The folder that holds the pack folders (texture-packs/, ui-packs/...). */
const char* androidPacksRoot(void);
/* XgActivity's method() (no arguments, no result): e.g. "chooseDiscFolder". */
void androidActivityAction(const char* method);
/* XgActivity's method(String arg), no result: e.g. "copyPack", "texture". */
void androidActivityActionText(const char* method, const char* arg);
/* The packs copied into the app's packs folder: each library's folders. */
typedef struct {
    char library[16]; /* texture-packs, ui-packs, portrait-packs, sprite-packs, glyph-packs */
    char name[128];   /* SLUS-00664, cel-4x, a portrait set's name... */
    unsigned long long bytes;
} AndroidPack;
int androidListPacks(AndroidPack* out, int max);
/* Removes <packs folder>/<library>/<name> (a library above, a name with no "/"): 1 when gone. */
int androidRemovePack(const char* library, const char* name);
/* At the start: when the app's version changed since the last start and the earlier version's
 * save states are there (this one cannot load them), says so once (a message box). */
void androidUpdateCheck(SDL_Window* window);
/* XgActivity's String method() into out ("" when there is none). */
const char* androidActivityText(const char* method, char* out, size_t size);
#endif

#ifdef __cplusplus
}
#endif

#endif
