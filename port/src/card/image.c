/*
 * Card images on disk (card.h): where they are, loading, writing back, and
 * the layout of a freshly formatted card.
 */

#include <SDL3/SDL.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "card.h"
#include "../system/savestate.h"
#include "system/settings.h"

Card cards[2];
int cardQuiet; /* the command line: no slot messages */

/* XOR of the frame's first CARD_FRAME - 1 bytes: what its last byte holds. */
static uint8_t frameChecksum(const uint8_t* frame) {
    uint8_t x = 0;
    for (int i = 0; i < CARD_FRAME - 1; i++) {
        x ^= frame[i];
    }
    return x;
}

void cardSetFrameChecksum(uint8_t* frame) {
    frame[CARD_FRAME - 1] = frameChecksum(frame);
}

int cardFrameChecksumOk(const uint8_t* frame) {
    return frame[CARD_FRAME - 1] == frameChecksum(frame);
}

int cardIsFormatted(const uint8_t* data) {
    return data[0] == 'M' && data[1] == 'C';
}

/* The layout PCSX-Redux gives a new card (and the BIOS's format leaves the
 * directory in): header "MC", 15 free directory frames, 20 empty broken
 * sector entries, zeroed replacement and unused frames, and frame 63 (the
 * write test frame) a copy of the header. */
void cardFormatImage(uint8_t* data) {
    memset(data, 0, CARD_SIZE);
    data[0] = 'M';
    data[1] = 'C';
    cardSetFrameChecksum(data);
    for (int i = 1; i < 16; i++) {
        uint8_t* f = data + i * CARD_FRAME;
        f[0] = DIR_FREE;
        f[8] = 0xFF;
        f[9] = 0xFF;
        cardSetFrameChecksum(f);
    }
    for (int i = 16; i < 36; i++) {
        uint8_t* f = data + i * CARD_FRAME;
        memset(f, 0xFF, 4);
        f[8] = 0xFF;
        f[9] = 0xFF;
        cardSetFrameChecksum(f);
    }
    memcpy(data + 63 * CARD_FRAME, data, CARD_FRAME);
}

int cardLoadFile(const char* path, uint8_t* data, size_t size) {
    FILE* f = fopen(path, "rb");
    if (f == NULL) {
        return 0;
    }
    size_t n = fread(data, 1, size, f);
    int extra = fgetc(f) != EOF;
    fclose(f);
    return n == size && !extra;
}

/* Written to a temporary file first, so a crash never leaves half a card. */
int cardSaveFile(const char* path, const uint8_t* data, size_t size) {
    char tmp[1200];
    if (snprintf(tmp, sizeof(tmp), "%s.tmp", path) >= (int)sizeof(tmp)) {
        return 0;
    }
    FILE* f = fopen(tmp, "wb");
    if (f == NULL) {
        fprintf(stderr, "card: cannot write %s\n", tmp);
        return 0;
    }
    int ok = fwrite(data, 1, size, f) == size;
    ok &= fclose(f) == 0;
    if (!ok || !SDL_RenamePath(tmp, path)) {
        fprintf(stderr, "card: cannot write %s\n", path);
        remove(tmp);
        return 0;
    }
    return 1;
}

/* Timestamp filenames avoid rotating files before the newest backup is safe.
 * Only our names are considered for retention or accepted by Restore. */
static int backupName(const char* name) {
    if (strlen(name) != 24 || strcmp(name + 20, ".mcd") != 0) {
        return 0;
    }
    for (int i = 0; i < 20; i++) {
        if (name[i] < '0' || name[i] > '9') {
            return 0;
        }
    }
    return 1;
}

typedef struct {
    CardBackup* items;
    int count, capacity;
} BackupList;

static SDL_EnumerationResult SDLCALL findBackup(void* user, const char* dir, const char* name) {
    BackupList* list = user;
    if (!backupName(name)) {
        return SDL_ENUM_CONTINUE;
    }
    char path[1200];
    SDL_PathInfo info;
    if (snprintf(path, sizeof(path), "%s%s", dir, name) >= (int)sizeof(path) ||
        !SDL_GetPathInfo(path, &info) || info.type != SDL_PATHTYPE_FILE) {
        return SDL_ENUM_CONTINUE;
    }
    if (list->count == list->capacity) {
        int capacity = list->capacity ? list->capacity * 2 : 16;
        CardBackup* items = realloc(list->items, (size_t)capacity * sizeof(*items));
        if (items == NULL) {
            return SDL_ENUM_FAILURE;
        }
        list->items = items;
        list->capacity = capacity;
    }
    CardBackup* item = &list->items[list->count++];
    snprintf(item->name, sizeof(item->name), "%s", name);
    item->modified = info.modify_time;
    return SDL_ENUM_CONTINUE;
}

static int newestBackup(const void* a, const void* b) {
    const CardBackup *left = a, *right = b;
    return strcmp(right->name, left->name);
}

static int backupDirectory(const char* image, char* out, size_t size) {
    return snprintf(out, size, "%s.backups", image) < (int)size;
}

static int readBackups(const char* dir, BackupList* list) {
    SDL_PathInfo info;
    if (!SDL_GetPathInfo(dir, &info)) {
        return 1; /* no history yet */
    }
    if (info.type != SDL_PATHTYPE_DIRECTORY || !SDL_EnumerateDirectory(dir, findBackup, list)) {
        return 0;
    }
    if (list->count > 1) {
        qsort(list->items, (size_t)list->count, sizeof(*list->items), newestBackup);
    }
    return 1;
}

static int preserveImage(const char* image, const uint8_t* data) {
    char dir[1100], path[1200];
    SDL_Time now;
    if (!backupDirectory(image, dir, sizeof(dir)) || !SDL_CreateDirectory(dir) || !SDL_GetCurrentTime(&now)) {
        return 0;
    }
    /* A fast second write, or a restored system clock, must not overwrite an
     * existing backup. The timestamp is also a deterministic tie breaker. */
    BackupList list = {0};
    if (!readBackups(dir, &list)) {
        free(list.items);
        return 0;
    }
    Uint64 stamp = (Uint64)now;
    if (list.count > 0) {
        errno = 0;
        Uint64 latest = strtoull(list.items[0].name, NULL, 10);
        if (errno || latest == UINT64_MAX) {
            free(list.items);
            return 0;
        }
        if (latest >= stamp) {
            stamp = latest + 1;
        }
    }
    free(list.items);
    memset(&list, 0, sizeof(list));
    SDL_PathInfo info;
    do {
        snprintf(path, sizeof(path), "%s/%020llu.mcd", dir, (unsigned long long)stamp++);
    } while (SDL_GetPathInfo(path, &info));
    if (!cardSaveFile(path, data, CARD_SIZE)) {
        return 0;
    }
    if (readBackups(dir, &list)) {
        for (int i = CARD_BACKUPS; i < list.count; i++) {
            snprintf(path, sizeof(path), "%s/%s", dir, list.items[i].name);
            if (!SDL_RemovePath(path)) {
                fprintf(stderr, "card: cannot remove old backup %s\n", path);
            }
        }
    }
    free(list.items);
    return 1;
}

int cardSaveImage(const char* path, const uint8_t* data) {
    static uint8_t previous[CARD_SIZE];
    FILE* f = fopen(path, "rb");
    if (f != NULL) {
        size_t n = fread(previous, 1, CARD_SIZE, f);
        int valid = n == CARD_SIZE && fgetc(f) == EOF && !ferror(f);
        fclose(f);
        if (!valid) {
            fprintf(stderr, "card: cannot preserve invalid image %s\n", path);
            return 0;
        }
        if (memcmp(previous, data, CARD_SIZE) == 0) {
            return 1;
        }
        if (!preserveImage(path, previous)) {
            fprintf(stderr, "card: cannot back up %s; previous image kept\n", path);
            return 0;
        }
    } else if (errno != ENOENT) {
        fprintf(stderr, "card: cannot read %s before writing\n", path);
        return 0;
    }
    return cardSaveFile(path, data, CARD_SIZE);
}

int cardListBackups(int slot, CardBackup* out, int max) {
    char dir[1100];
    BackupList list = {0};
    if (slot < 0 || slot > 1 || !cards[slot].present || max < 0 ||
        !backupDirectory(cards[slot].path, dir, sizeof(dir))) {
        return 0;
    }
    int ok = readBackups(dir, &list);
    int count = ok ? (list.count < max ? list.count : max) : -1;
    if (count > 0) {
        memcpy(out, list.items, (size_t)count * sizeof(*out));
    }
    free(list.items);
    return count;
}

/* system/netplay.c (weak: tests link this file without it). */
int netplayRefuses(const char* what) __attribute__((weak)); /* system/netplay.c: not in an online session */

int cardRestoreBackup(int slot, const char* name, char* err, size_t errSize) {
    static uint8_t restored[CARD_SIZE];
    char dir[1100], path[1200];
    if (netplayRefuses != NULL && netplayRefuses("a memory card restore")) {
        snprintf(err, errSize, "not during an online session");
        return 0;
    }
    if (slot < 0 || slot > 1 || !cards[slot].present || !backupName(name)) {
        snprintf(err, errSize, "no card or invalid backup selection");
        return 0;
    }
    if (cardBusy(slot)) {
        snprintf(err, errSize, "the game is using the card in slot %d; try again in a moment", slot + 1);
        return 0;
    }
    Card* c = &cards[slot];
    if (!backupDirectory(c->path, dir, sizeof(dir))) {
        return 0;
    }
    snprintf(path, sizeof(path), "%s/%s", dir, name);
    int valid = cardLoadFile(path, restored, CARD_SIZE) && cardIsFormatted(restored) && cardFrameChecksumOk(restored);
    for (int i = 1; valid && i < CARD_BLOCKS; i++) {
        valid = cardFrameChecksumOk(restored + i * CARD_FRAME);
    }
    if (!valid) {
        snprintf(err, errSize, "backup is missing or is not a formatted 128 KB card");
        return 0;
    }
    /* Flush any pending changes first: Restore preserves the current version
     * as well as the prior disk image, and commits memory only after disk. */
    if (!cardFlush(slot) || !cardSaveImage(c->path, restored)) {
        snprintf(err, errSize, "cannot preserve and restore %s; current card kept", c->path);
        return 0;
    }
    memcpy(c->data, restored, CARD_SIZE);
    c->flagNew = 1;
    c->dirty = 0;
    snprintf(err, errSize, "Restored slot %d; its previous contents are in the backup history", slot + 1);
    return 1;
}

int cardFlush(int slot) {
    Card* c = &cards[slot];
    if (!c->dirty || !c->present || c->path[0] == '\0') {
        return 1;
    }
    if (!cardSaveImage(c->path, c->data)) {
        return 0;
    }
    c->dirty = 0;
    return 1;
}

static void flushAll(void) {
    cardFlush(0);
    cardFlush(1);
}

static void defaultPath(int slot, char* out, size_t size) {
    const char* dir = getenv("XG_CARD_DIR");
    char* pref = NULL;
    if (dir == NULL || dir[0] == '\0') {
        pref = SDL_GetPrefPath(NULL, "xenogears");
        dir = pref != NULL ? pref : ".";
    }
    size_t len = strlen(dir);
    const char* sep = len > 0 && (dir[len - 1] == '/' || dir[len - 1] == '\\') ? "" : "/";
    snprintf(out, size, "%s%scard%d.mcd", dir, sep, slot + 1);
    SDL_free(pref);
}

/* Slot `slot` from a card1/card2 value ("none", "" for the default image, or
 * a path) into *c. 0 (and a message) when the file is there but no card. */
static int loadSlot(int slot, const char* value, Card* c, char* err, size_t errSize) {
    memset(c, 0, sizeof(*c));
    if (value != NULL && strcmp(value, "none") == 0) {
        snprintf(err, errSize, "slot %d empty", slot + 1);
        return 1;
    }
    if (value != NULL && value[0] != '\0') {
        snprintf(c->path, sizeof(c->path), "%s", value);
    } else {
        defaultPath(slot, c->path, sizeof(c->path));
    }
    c->present = 1;
    c->flagNew = 1; /* a card reads as newly inserted after power-on or a swap */
    if (cardLoadFile(c->path, c->data, CARD_SIZE)) {
        snprintf(err, errSize, "slot %d: %s", slot + 1, c->path);
        return 1;
    }
    FILE* f = fopen(c->path, "rb");
    if (f != NULL) {
        /* There is a file, but not a 128 KB card: never overwrite it. */
        fclose(f);
        snprintf(err, errSize, "slot %d: %s is not a 128 KB card image", slot + 1, c->path);
        c->present = 0;
        return 0;
    }
    cardFormatImage(c->data);
    snprintf(err, errSize, "slot %d: %s (new, formatted)", slot + 1, c->path);
    return 1;
}

void cardImagesInit(void) {
    static int done;
    if (done) {
        return;
    }
    done = 1;
    settingsDescribe("card1", "card1, card2: the memory card images in slots 1 and 2 (none: no card; empty: "
                              "card1.mcd/card2.mcd in this folder)");
    for (int slot = 0; slot < 2; slot++) {
        char msg[1200];
        int ok = loadSlot(slot, settingsGet(slot == 0 ? "card1" : "card2"), &cards[slot], msg, sizeof(msg));
        if (!ok) {
            fprintf(stderr, "card: %s; slot left empty\n", msg);
        } else if (!cardQuiet) {
            fprintf(stderr, "card: %s\n", msg);
        }
    }
    atexit(flushAll);
}

int cardSetSlot(int slot, const char* path, char* err, size_t errSize) {
    static Card next; /* 128 KB: not on the stack */
    if (netplayRefuses != NULL && netplayRefuses("a memory card change")) { /* the game would see it on one side */
        snprintf(err, errSize, "not during an online session");
        return 0;
    }
    cardImagesInit();
    if (cardBusy(slot)) {
        snprintf(err, errSize, "the game is using the card in slot %d; try again in a moment", slot + 1);
        return 0;
    }
    if (!loadSlot(slot, path, &next, err, errSize)) {
        return 0;
    }
    if (!cardFlush(slot)) {
        snprintf(err, errSize, "cannot write the card in slot %d back to %s", slot + 1, cards[slot].path);
        return 0;
    }
    cards[slot] = next;
    settingsSet(slot == 0 ? "card1" : "card2", path);
    settingsSave();
    fprintf(stderr, "card: %s (inserted from the settings menu)\n", err);
    return 1;
}

int cardChangedByMenu(int slot) {
    Card* c = &cards[slot];
    c->dirty = 1;
    c->flagNew = 1;
    return cardFlush(slot);
}

/* ---- save states (system/savestate.h) ---- */

static uint64_t imageHash(const Card* c) {
    uint64_t h = 0xcbf29ce484222325ull;
    const uint64_t* w = (const uint64_t*)c->data;
    for (size_t i = 0; i < CARD_SIZE / 8; i++) {
        h = (h ^ w[i]) * 0x100000001b3ull;
    }
    return c->present ? h : 0;
}

/* A card's FLAG "new card" bit as the state's game saw it, and what was on
 * each card then. A load writes pending changes to disk first; a card whose
 * contents changed since (a game saved on it later, another image chosen)
 * comes back as a new card, so the game reads its directory again, as after
 * a swap. The images themselves are never rolled back. */
void cardImageStateIo(XgState* s) {
    uint64_t hash[2] = {imageHash(&cards[0]), imageHash(&cards[1])};
    int flagNew[2] = {cards[0].flagNew, cards[1].flagNew};
    XG_STATE_VAR(s, hash);
    XG_STATE_VAR(s, flagNew);
    if (!xgStateLoading(s)) {
        return;
    }
    cardImagesInit();
    for (int slot = 0; slot < 2; slot++) {
        cardFlush(slot);
        if (cards[slot].present) {
            cards[slot].flagNew = imageHash(&cards[slot]) == hash[slot] ? flagNew[slot] : 1;
        }
    }
}
