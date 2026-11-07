/* The BIOS must report a save failure when its disk backup cannot be made. */
#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "common.h"
#include "card/card.h"
#include "system/system.h"
#include "test/env.h"

void hwSafePoint(void) {}
int slowdownBiosMissed(void) { return 0; }
void hwYield(void) { exit(3); }
double hwNow(void) { return 0; }
double hwVSyncPeriod(void) { return 1.0 / 60; }
uint32_t hwVSyncCount(void) { return 0; }
void hwScheduleAt(double at, HwEvent fn, void* arg) { fn(arg); }
void DeliverEvent(uint32_t ev, int32_t spec) {}
void InitCARD(s32 padEnable);
s32 func_80040534(char* path, s32 mode);
s32 func_80040554(s32 fd, void* buf, s32 n);
s32 func_80040564(s32 fd);
s32 func_80040574(char* path);
s32 func_800405A4(char* oldPath, char* newPath);
s32 func_800405B4(char* path);

static int checks;
#define CHECK_DETAIL SDL_GetError()
#include "test/check.h"

int main(int argc, char** argv) {
    if (argc != 2) return 2;
    testSetEnv("XG_SETTINGS", "none");
    testSetEnv("XG_CARD1", "none");
    testSetEnv("XG_CARD2", "none");
    InitCARD(0); /* accesses complete synchronously without StartCARD */
    Card* c = &cards[0];
    snprintf(c->path, sizeof(c->path), "%s/bios.mcd", argv[1]);
    c->present = 1;
    cardFormatImage(c->data);
    int changed[CARD_BLOCKS];
    CHECK(cardCreateFile(c->data, "TEST", 1, changed) != 0);
    CHECK(cardSaveImage(c->path, c->data));
    static uint8_t original[CARD_SIZE], disk[CARD_SIZE];
    memcpy(original, c->data, CARD_SIZE);
    char history[1100];
    snprintf(history, sizeof(history), "%s.backups", c->path);
    FILE* f = fopen(history, "wb");
    CHECK(f != NULL && fclose(f) == 0); /* directory creation will fail */
    uint8_t data[CARD_FRAME];
    memset(data, 42, sizeof(data));
    int fd = func_80040534("bu00:TEST", 0);
    CHECK(fd >= 2);
    CHECK(func_80040554(fd, data, sizeof(data)) == -1);
    CHECK(c->dirty && cardLoadFile(c->path, disk, CARD_SIZE) && memcmp(disk, original, CARD_SIZE) == 0);
    CHECK(func_80040564(fd) == fd);
    memcpy(c->data, original, CARD_SIZE); c->dirty = 0;
    CHECK(func_80040534("bu00:NEW", 0x10200) == -1);
    CHECK(c->dirty && cardLoadFile(c->path, disk, CARD_SIZE) && memcmp(disk, original, CARD_SIZE) == 0);
    memcpy(c->data, original, CARD_SIZE); c->dirty = 0;
    CHECK(!func_800405A4("bu00:TEST", "bu00:RENAMED"));
    CHECK(c->dirty && cardLoadFile(c->path, disk, CARD_SIZE) && memcmp(disk, original, CARD_SIZE) == 0);
    memcpy(c->data, original, CARD_SIZE); c->dirty = 0;
    CHECK(!func_800405B4("bu00:TEST"));
    CHECK(c->dirty && cardLoadFile(c->path, disk, CARD_SIZE) && memcmp(disk, original, CARD_SIZE) == 0);
    memcpy(c->data, original, CARD_SIZE); c->dirty = 0;
    CHECK(!func_80040574("bu00:"));
    CHECK(c->dirty && cardLoadFile(c->path, disk, CARD_SIZE) && memcmp(disk, original, CARD_SIZE) == 0);
    CHECK(SDL_RemovePath(history));
    memcpy(c->data, original, CARD_SIZE); c->dirty = 0;
    fd = func_80040534("bu00:TEST", 0);
    CHECK(fd >= 2 && func_80040554(fd, data, sizeof(data)) == sizeof(data));
    CHECK(!c->dirty && cardLoadFile(c->path, disk, CARD_SIZE) && disk[CARD_BLOCK] == 42);
    CHECK(func_80040564(fd) == fd);
    printf("card BIOS persistence errors: %d checks passed\n", checks);
    return 0;
}
