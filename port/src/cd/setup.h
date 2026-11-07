#ifndef PORT_CD_SETUP_H
#define PORT_CD_SETUP_H
#include <SDL3/SDL.h>
#include "verify.h"
/* Before the game/GPU: the starting image `path` checked (region NULL: any).
 *   CD_SETUP_OK       it is valid: out is its complete path
 *   CD_SETUP_FOUND    it is not, but another valid image was found where
 *                     images usually are (cdSetupDiscover): out; the setup
 *                     screen is to be shown, starting from it
 *   CD_SETUP_NONE     no valid image anywhere: the setup screen without a
 *                     disc (port/src/ui: uiDiscSetup with UI_SETUP_NO_DISC)
 *   0                 an unattended run (XG_HEADLESS without XG_UI_SCRIPT):
 *                     the reason on stderr, nothing waits for anyone */
enum { CD_SETUP_OK = 1, CD_SETUP_FOUND = 2, CD_SETUP_NONE = 3 };
int cdSetupFirst(SDL_Window* window, const char* path, const char* region, char out[4096]);
/* A run that can show the setup screen: not XG_HEADLESS, or one driven by
 * XG_UI_SCRIPT. */
int cdSetupInteractive(void);

/* Discovery (the setup screen's folder search, and cdSetupFirst):
 * the valid Xenogears images in `dir` and in each folder directly in it
 * (the usual layout: a folder per disc), at most `max`: the .cue sheets of
 * each folder, or its raw .bin images where it has no .cue. Sorted by
 * region, then disc. Returns how many. */
typedef struct {
    char path[4096];
    CdImageInfo info;
} CdSetupImage;
int cdSetupFindImages(const char* dir, CdSetupImage* out, int max);
/* Of those, the starting image (the region's Disc 1, else its Disc 2; region
 * NULL: any) and the other disc of the same release (-1: none). 0: none. */
int cdSetupPick(const CdSetupImage* found, int count, const char* region, int* first, int* other);
/* A valid image of the region in the folders near the given paths (each
 * path's folder, the folder above it, and the folders directly in those),
 * then beside the executable and in the current folder. */
int cdSetupDiscover(const char* region, const char* const* near, int count, char out[4096]);
/* A PS1 BIOS image in `dir` (the usual file names, as port/src/system/krom.c
 * looks for them beside the disc; 512 KB), or `dir` itself when it is one.
 * 0: none. */
int cdSetupFindBios(const char* dir, char* out, size_t size);
int cdSetupIsBios(const char* path);
int cdVerifyCommand(int argc, char** argv, const char* region);
/* Store paths independently of the working directory on the next launch. */
int cdSetupPath(const char* path, char* out, size_t size);
#endif
