/* Pass a directory containing original-46.tim through original-a0.tim,
 * extracted from the player's retail disc, and portrait-mapping.bin (the
 * field overlay's 180-byte mapping). No game assets live in this test.
 * Exercise actual field caching, TIM parsing/uploads and mirrored geometry. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "field/field.h"
#include "main/filesystem.h"
#include "pc_gameplay.h"
#include "test/env.h"

static unsigned char originals[91][4640], expected[91][4640];
static int changes[91], checks;
#include "test/check.h"
static unsigned short color(const unsigned char* tim, int pixel) {
    int index = tim[544 + pixel];
    return tim[20 + index * 2] | tim[21 + index * 2] << 8;
}

/* Coordinates independently recorded from the retail pixel audit. */
static const struct { int file, x, y; } holes[] = {
    {0x5f,28,23}, {0x5f,29,23}, {0x5f,31,24}, {0x63,34,22},
    {0x81,16,48}, {0x81,18,49}, {0x81,29,49}, {0x81,35,50}, {0x81,38,51},
    {0x82,22,43}, {0x82,24,44}, {0x91,21,20}, {0x91,22,20}, {0x91,23,20},
    {0x93,23,23}, {0x93,24,23}, {0x93,25,24}, {0x94,28,26}, {0x95,31,50},
    {0x97,25,23}, {0x97,26,23}, {0x97,43,24}, {0x97,26,25}, {0x99,48,23}
};

static int testAssets(const char* dir) {
    unsigned char copy[4640];
    for (int file = 0x46; file <= 0xa0; ++file) {
        int n = file - 0x46;
        char path[4096];
        snprintf(path, sizeof(path), "%s/original-%02x.tim", dir, file);
        FILE* f = fopen(path, "rb");
        CHECK(f != NULL);
        CHECK(fread(originals[n], 1, 4640, f) == 4640);
        CHECK(fgetc(f) == EOF); fclose(f);
        memcpy(copy, originals[n], 4640);
        testSetEnv("XG_ORIGINAL_BUG_FIXES", "off");
        pcGameplayPortraitTim(copy, 4640, file);
        CHECK(!memcmp(copy, originals[n], 4640));
        testSetEnv("XG_ORIGINAL_BUG_FIXES", "on");
        pcGameplayPortraitTim(copy, 4640, file);
        memcpy(expected[n], copy, 4640);
        for (int p = 0; p < 4096; ++p) {
            int hole = 0;
            for (unsigned int h = 0; h < sizeof(holes)/sizeof(holes[0]); ++h)
                if (holes[h].file == file && holes[h].y * 64 + holes[h].x == p) hole = 1;
            if (file == 0x70 && p % 64 == 62 && p / 64 >= 45 && p / 64 <= 48) {
                static const unsigned short strayColors[] = {0x0400, 0x0c42, 0x0c42, 0x0421};
                CHECK(color(originals[n], p) == strayColors[p / 64 - 45]);
                CHECK(color(copy, p) == 0); ++changes[n];
            } else {
                CHECK(color(originals[n], p) == (hole ? 0 : color(copy, p)));
                if (hole) { CHECK(color(copy, p) == 0x8000); ++changes[n]; }
            }
        }
        /* Dimensions, UV origins, flags and every other header byte survive. */
        CHECK(!memcmp(copy, originals[n], 20));
        CHECK(!memcmp(copy + 532, originals[n] + 532, 12));
        if (file == 0x70) CHECK(!memcmp(copy + 20, originals[n] + 20, 512));
        if (!changes[n]) CHECK(!memcmp(copy, originals[n], 4640));
        pcGameplayPortraitTim(copy, 4640, file);
        CHECK(!memcmp(copy, expected[n], 4640));
        if (changes[n]) {
            /* Every single-byte asset modification must be left alone. */
            for (int b = 0; b < 4640; ++b) {
                memcpy(copy, originals[n], 4640); copy[b] ^= 1;
                unsigned char altered[4640]; memcpy(altered, copy, 4640);
                pcGameplayPortraitTim(copy, 4640, file);
                CHECK(!memcmp(copy, altered, 4640));
            }
            /* Short, oversized, wrong-ID and null inputs never get read. */
            unsigned char tiny = 0x10;
            for (unsigned int size = 0; size < 4640; ++size)
                pcGameplayPortraitTim(&tiny, size, file);
            pcGameplayPortraitTim(&tiny, 4641, file);
            pcGameplayPortraitTim(NULL, 4640, file);
            CHECK(tiny == 0x10);
            memcpy(copy, originals[n], 4640);
            pcGameplayPortraitTim(copy, 4640, file + 1);
            CHECK(!memcmp(copy, originals[n], 4640));
        }
    }
    int total = 0, faces = 0;
    for (int i = 0; i < 91; ++i) { total += changes[i]; faces += changes[i] != 0; }
    CHECK(total == 28 && faces == 11);
    return 0;
}

#ifndef _WIN32
typedef struct { s16 m0_id, m2_status, m4_isAsymmetric; } PortraitLoadingStatus;
PortraitLoadingStatus portraitLoadingStatus[3];
s32 currentPortraitLoadingSlot;
u32 *D_800ADB10, *D_800ADB14;
RECT partyPortraitsRects[3][2];
u8 portraitMapping[180];
LoadingBatchCommand portraitsLoadingCommands[3];
DialogWindow gDialogWindows[4];
static sFieldScriptEntity actor;
sFieldScriptEntity* pCurrentFieldScriptActor = &actor;
static int busy, allocations, frees, uploads;
static unsigned char captured[4][4096];
static RECT uploadRects[4];
s32 isDialogAvatarLoaded(s32);
void setupFacePolyPosition(POLY_FT4*, s32, s32, s32, s32, s32);
s32 setCurrentDirectory(s32 directory, s32 offset) {
    if (directory != 4 || offset) abort();
    return 0;
}
s32 getFileSizeAligned(s32 file) {
    if (file < 0x46 || file > 0xa0) abort();
    return 4640;
}
void* allocateHeap(s32 size, s32 flags) {
    if (size != 4640 || flags) abort();
    ++allocations;
    return malloc(size);
}
void freeHeap(void* p) { ++frees; free(p); }
s32 batchStartLoadingFiles(LoadingBatchCommand* commands, s32 a, s32 b) {
    if (a || b) abort();
    for (int i = 0; commands[i].m0_fileIndex; ++i)
        memcpy(commands[i].m4_loadPtr, originals[commands[i].m0_fileIndex - 0x46], 4640);
    return 0;
}
s32 waitReadCompletion(s32 mode) { if (mode != 1) abort(); return busy; }
void LoadImage(RECT* rect, void* pixels) {
    int bytes = rect->w * rect->h * 2;
    if (uploads >= 4 || (bytes != 512 && bytes != 4096)) abort();
    uploadRects[uploads] = *rect;
    memcpy(captured[uploads++], pixels, bytes);
}

static int testLoader(const char* dir) {
    /* Mapping values from the two retail overlays; Bart and Sigurd use
     * distinct assets for the two directions. All other pairs are equal. */
    char path[4096];
    snprintf(path, sizeof(path), "%s/portrait-mapping.bin", dir);
    FILE* f = fopen(path, "rb");
    CHECK(f != NULL);
    CHECK(fread(portraitMapping, 1, 180, f) == 180);
    CHECK(fgetc(f) == EOF); fclose(f);
    CHECK(portraitMapping[6] == 0x59 - 0x46 && portraitMapping[7] == 0x5a - 0x46);
    CHECK(portraitMapping[54] == 0x78 - 0x46 && portraitMapping[55] == 0x79 - 0x46);
    for (int enabled = 0; enabled < 2; ++enabled) {
        testSetEnv("XG_ORIGINAL_BUG_FIXES", enabled ? "on" : "off");
        for (int id = 0; id < 90; ++id) {
            int file = portraitMapping[id * 2] + 0x46;
            int otherFile = portraitMapping[id * 2 + 1] + 0x46;
            int asymmetric = file != otherFile;
            CHECK(file >= 0x46 && file <= 0xa0 && otherFile >= 0x46 && otherFile <= 0xa0);
            CHECK(asymmetric == (id == 3 || id == 27));
            for (int slot = 0; slot < 3; ++slot) {
                portraitLoadingStatus[slot] = (PortraitLoadingStatus){-1, 0, 0};
                partyPortraitsRects[slot][0] = (RECT){704, 256 + 64 * slot, 0, 224 + slot * 2};
                partyPortraitsRects[slot][1] = (RECT){736, 256 + 64 * slot, 0, 225 + slot * 2};
            }
            currentPortraitLoadingSlot = 2;
            allocations = frees = uploads = busy = 0;
            CHECK(isDialogAvatarLoaded(id) == -1);
            CHECK(allocations == 1 + asymmetric && uploads == 0 && frees == 0);
            CHECK(portraitLoadingStatus[0].m2_status == 1);
            busy = 1;
            CHECK(isDialogAvatarLoaded(id) == -1 && uploads == 0);
            busy = 0;
            CHECK(isDialogAvatarLoaded(id) == -1 && uploads == 4);
            CHECK(portraitLoadingStatus[0].m2_status == 2 && frees == 0);
            for (int side = 0; side < 2; ++side) {
                int n = (side ? otherFile : file) - 0x46;
                const unsigned char* tim = enabled ? expected[n] : originals[n];
                CHECK(!memcmp(captured[side * 2], tim + 20, 512));
                CHECK(!memcmp(captured[side * 2 + 1], tim + 544, 4096));
                CHECK(uploadRects[side * 2].x == 0 && uploadRects[side * 2].y == 224 + side);
                CHECK(uploadRects[side * 2 + 1].x == 704 + side * 32);
                CHECK(uploadRects[side * 2 + 1].y == 256);
            }
            CHECK(isDialogAvatarLoaded(id) == -1 && frees == allocations);
            CHECK(isDialogAvatarLoaded(id) == 0 && uploads == 4 && allocations == frees);
            CHECK(actor.m12C_x2 == 0);
            /* Cached faces stay stable; a subsequent load uses the setting.
             * Reusing all slots also verifies lifetime of both batch buffers. */
            for (int reload = 0; reload < 3; ++reload) {
                testSetEnv("XG_ORIGINAL_BUG_FIXES", reload == 1 ? "on" : "off");
                CHECK(isDialogAvatarLoaded(id) == 0 && uploads == 4);
                portraitLoadingStatus[currentPortraitLoadingSlot].m0_id = -1;
                uploads = 0;
                CHECK(isDialogAvatarLoaded(id) == -1);
                CHECK(isDialogAvatarLoaded(id) == -1 && uploads == 4);
                CHECK(!memcmp(captured[1], (reload == 1 ? expected : originals)[file - 0x46] + 544, 4096));
                CHECK(isDialogAvatarLoaded(id) == -1);
                CHECK(isDialogAvatarLoaded(id) == 0 && allocations == frees);
            }
            testSetEnv("XG_ORIGINAL_BUG_FIXES", enabled ? "on" : "off");
        }
    }
    for (int side = 0; side < 2; ++side) {
        POLY_FT4 quad; memset(&quad, 0x55, sizeof(quad));
        setupFacePolyPosition(&quad, 10, 20, 64, 64, side ? 0x20 : 0);
        CHECK(quad.x0 == (side ? 74 : 9) && quad.x1 == (side ? 10 : 73));
        CHECK(quad.x2 == quad.x0 && quad.x3 == quad.x1);
        CHECK(quad.y0 == 20 && quad.y1 == 20 && quad.y2 == 84 && quad.y3 == 84);
        CHECK(quad.u0 == 0x55 && quad.v0 == 0x55 && quad.clut == 0x5555 && quad.tpage == 0x5555);
    }
    return 0;
}
#endif

int main(int argc, char** argv) {
    if (argc != 2) { fprintf(stderr, "usage: %s retail-portrait-directory\n", argv[0]); return 2; }
    testSetEnv("XG_SETTINGS", "none");
    CHECK(testAssets(argv[1]) == 0);
#ifndef _WIN32
    CHECK(testLoader(argv[1]) == 0);
#endif
    printf("retail portraits: %d checks passed\n", checks);
    return 0;
}
