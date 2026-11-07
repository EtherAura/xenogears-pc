/*
 * The map viewer's kernel start (PC only; port/src/extras/mapfield.c and
 * mapworld.c, inside an extras sandbox and a world of its own, never the
 * game): what a game mode finds set up when kernelMain starts it.
 */
#include "common.h"
#include "psyq.h"
#include "main/filesystem.h"
#include "main/memory.h"
#include "main/sound.h"

void initKernelGlobals(void);
void initGameState(void);
void resetSpriteSystem(void);
void initFont(u8* font);
void setupPrintDialogTextVar(void* bundle);

/* kernelMain from the sound driver on: the driver and its resident sample
 * banks (01/02-05), the dialogue font (01/06) and text (01/07), the kernel's
 * globals, the game state from the disc (initGameState) and the sprite
 * system. The caller has entered a world of its own (system/world.h) for
 * the driver's events, root counter and SPU, and attached the disc's files
 * (extras/hostfiles.h). */
void mapsStartKernel(void) {
    void* buf;
    s32 i;
    Wds* banks[4];

    initSoundSystem(0);
    setCurrentDirectory(0, 1);
    for (i = 0; i < 4; i++) {
        banks[i] = (Wds*)allocateHeap(getFileSize(2 + i), 0);
        if (banks[i] != NULL) {
            readFile(2 + i, banks[i], 0, 0);
        }
    }
    for (i = 0; i < 4; i++) {
        if (banks[i] != NULL) {
            if (i == 1) {
                wdsFile3 = loadWds(banks[i], 0);
            } else if (i == 3) {
                wdsFile5 = loadWds(banks[i], 0);
            } else {
                loadWds(banks[i], 0);
            }
        }
    }
    buf = allocateHeap(getFileSize(6), 0);
    if (buf != NULL) {
        readFile(6, buf, 0, 0);
        initFont(mallocAndDecompress(buf, 1));
        freeHeap(buf);
    }
    buf = allocateHeap(getFileSize(7), 0);
    if (buf != NULL) {
        readFile(7, buf, 0, 0);
        setupPrintDialogTextVar(mallocAndDecompress(buf, 1));
        freeHeap(buf);
    }
    initKernelGlobals();
    initGameState();
    resetSpriteSystem();
}
