/* Disposable-card integration checks. Pass a fresh output directory. */
#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "card/card.h"
#include "test/env.h"

static int busy;
int cardBusy(int slot) { return busy; }
static int checks;
#define CHECK_DETAIL SDL_GetError()
#include "test/check.h"
static SDL_EnumerationResult SDLCALL countImages(void* user, const char* dir, const char* name) {
    size_t n = strlen(name);
    if (n > 4 && strcmp(name + n - 4, ".mcd") == 0) (*(int*)user)++;
    return SDL_ENUM_CONTINUE;
}

int main(int argc, char** argv) {
    if (argc < 2 || argc > 3) return 2;
    testSetEnv("XG_SETTINGS", "none");
    testSetEnv("XG_CARD1", "none");
    testSetEnv("XG_CARD2", "none");
    cardImagesInit();
    CHECK(SDL_CreateDirectory(argv[1]));
    Card* c = &cards[0];
    snprintf(c->path, sizeof(c->path), "%s/card.mcd", argv[1]);
    c->present = 1;
    CardBackup history[CARD_BACKUPS];
    if (argc == 3) {
        CHECK(cardLoadFile(c->path, c->data, CARD_SIZE));
        CHECK(c->data[CARD_BLOCK] == 99);
        CHECK(cardListBackups(0, history, CARD_BACKUPS) == CARD_BACKUPS);
        printf("card history restart: %d checks passed\n", checks);
        return 0;
    }
    cardFormatImage(c->data);
    CHECK(cardSaveImage(c->path, c->data));
    CHECK(cardListBackups(0, history, CARD_BACKUPS) == 0);
    for (int version = 1; version <= CARD_BACKUPS + 3; version++) {
        c->data[CARD_BLOCK] = (uint8_t)version;
        c->dirty = 1;
        CHECK(cardFlush(0));
        CHECK(!c->dirty);
    }
    CHECK(cardListBackups(0, history, CARD_BACKUPS) == CARD_BACKUPS);
    char dir[1100];
    snprintf(dir, sizeof(dir), "%s.backups", c->path);
    int files = 0;
    CHECK(SDL_EnumerateDirectory(dir, countImages, &files) && files == CARD_BACKUPS);
    char newest[32];
    snprintf(newest, sizeof(newest), "%s", history[0].name);
    CHECK(cardSaveImage(c->path, c->data));
    CHECK(cardListBackups(0, history, CARD_BACKUPS) == CARD_BACKUPS);
    CHECK(strcmp(history[0].name, newest) == 0); /* unchanged writes don't rotate */
    char err[1200];
    busy = 1;
    CHECK(!cardRestoreBackup(0, newest, err, sizeof(err)));
    CHECK(c->data[CARD_BLOCK] == CARD_BACKUPS + 3);
    busy = 0;
    CHECK(!cardRestoreBackup(0, "../card.mcd", err, sizeof(err)));
    CHECK(cardRestoreBackup(0, newest, err, sizeof(err)));
    CHECK(c->data[CARD_BLOCK] == CARD_BACKUPS + 2 && c->flagNew && !c->dirty);
    static uint8_t disk[CARD_SIZE];
    CHECK(cardLoadFile(c->path, disk, CARD_SIZE));
    CHECK(memcmp(disk, c->data, CARD_SIZE) == 0);
    CHECK(cardListBackups(0, history, CARD_BACKUPS) == CARD_BACKUPS);
    /* Restore's first backup contains the state we just replaced. */
    CHECK(cardRestoreBackup(0, history[0].name, err, sizeof(err)));
    CHECK(c->data[CARD_BLOCK] == CARD_BACKUPS + 3);
    c->dirty = 1;
    c->data[CARD_BLOCK] = 99;
    CHECK(cardRestoreBackup(0, newest, err, sizeof(err))); /* pending memory also preserved */
    CHECK(cardListBackups(0, history, CARD_BACKUPS) == CARD_BACKUPS);
    CHECK(cardRestoreBackup(0, history[0].name, err, sizeof(err)));
    CHECK(c->data[CARD_BLOCK] == 99);
    /* History is discovered from disk, independent of slot or process state. */
    cards[1] = *c;
    CHECK(cardListBackups(1, history, CARD_BACKUPS) == CARD_BACKUPS);
    CHECK(cardListBackups(-1, history, CARD_BACKUPS) == 0);
    char bad[1200];
    snprintf(bad, sizeof(bad), "%s.backups/%s", c->path, history[0].name);
    FILE* f = fopen(bad, "wb");
    CHECK(f != NULL);
    CHECK(fwrite("bad", 1, 3, f) == 3);
    CHECK(fclose(f) == 0);
    CHECK(!cardRestoreBackup(0, history[0].name, err, sizeof(err)));
    CHECK(c->data[CARD_BLOCK] == 99);
    memcpy(disk, c->data, CARD_SIZE);
    disk[CARD_FRAME + 40] ^= 1; /* valid header, corrupt directory checksum */
    CHECK(cardSaveFile(bad, disk, CARD_SIZE));
    CHECK(!cardRestoreBackup(0, history[0].name, err, sizeof(err)));
    CHECK(c->data[CARD_BLOCK] == 99);
    /* A blocked output must keep both live memory and the existing image. */
    snprintf(bad, sizeof(bad), "%s.tmp", c->path);
    CHECK(SDL_CreateDirectory(bad));
    CHECK(cardListBackups(0, history, CARD_BACKUPS) == CARD_BACKUPS);
    CHECK(!cardRestoreBackup(0, history[2].name, err, sizeof(err)));
    CHECK(c->data[CARD_BLOCK] == 99);
    CHECK(cardLoadFile(c->path, disk, CARD_SIZE) && disk[CARD_BLOCK] == 99);
    CHECK(SDL_RemovePath(bad));
    /* An inaccessible history location must not destroy the prior card. */
    char other[1024];
    snprintf(other, sizeof(other), "%s/blocked.mcd", argv[1]);
    CHECK(cardSaveImage(other, disk));
    snprintf(bad, sizeof(bad), "%s.backups", other);
    f = fopen(bad, "wb");
    CHECK(f != NULL && fclose(f) == 0);
    disk[CARD_BLOCK] = 100;
    CHECK(!cardSaveImage(other, disk));
    CHECK(cardLoadFile(other, disk, CARD_SIZE) && disk[CARD_BLOCK] == 99);
    printf("card history: %d checks passed\n", checks);
    return 0;
}
