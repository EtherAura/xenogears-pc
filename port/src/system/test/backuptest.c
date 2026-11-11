/* Saves backup (system/backup.c): a save folder into a .zip and back, byte
 * for byte, its subfolders too; a damaged archive, one without settings.ini
 * and one naming a file outside the folder restore nothing. */
#include <stdio.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "system/backup.h"

static int failures;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)

static void put(const char* path, const void* data, size_t n) {
    SDL_IOStream* io = SDL_IOFromFile(path, "wb");
    CHECK(io != NULL && SDL_WriteIO(io, data, n) == n);
    if (io != NULL) SDL_CloseIO(io);
}

static int same(const char* a, const char* b) {
    size_t na = 0, nb = 0;
    void* da = SDL_LoadFile(a, &na);
    void* db = SDL_LoadFile(b, &nb);
    int r = da != NULL && db != NULL && na == nb && memcmp(da, db, na) == 0;
    SDL_free(da);
    SDL_free(db);
    return r;
}

static SDL_EnumerationResult SDLCALL removeOne(void* user, const char* dirname, const char* fname) {
    (void)user;
    char path[1400];
    snprintf(path, sizeof path, "%s%s", dirname, fname);
    SDL_PathInfo info;
    if (SDL_GetPathInfo(path, &info) && info.type == SDL_PATHTYPE_DIRECTORY) SDL_EnumerateDirectory(path, removeOne, NULL);
    SDL_RemovePath(path);
    return SDL_ENUM_CONTINUE;
}

static void removeAll(const char* dir) {
    SDL_EnumerateDirectory(dir, removeOne, NULL);
    SDL_RemovePath(dir);
}

int main(int argc, char** argv) {
    char base[1024], from[1100], to[1100], zip[1100], path[1200], other[1200], why[1200];
    snprintf(base, sizeof base, "%s/xg-backuptest-%u", argc > 1 ? argv[1] : SDL_GetCurrentDirectory(),
             (unsigned)SDL_GetTicksNS());
    snprintf(from, sizeof from, "%s/from", base);
    snprintf(to, sizeof to, "%s/to", base);
    snprintf(zip, sizeof zip, "%s/saves.zip", base);
    snprintf(path, sizeof path, "%s/states/US", from);
    CHECK(SDL_CreateDirectory(path) && SDL_CreateDirectory(to));

    static unsigned char card[131072], state[300000];
    for (size_t i = 0; i < sizeof card; i++) card[i] = (unsigned char)(i * 7 + (i >> 9));
    for (size_t i = 0; i < sizeof state; i++) state[i] = (unsigned char)(i * 13 ^ (i >> 5));
    const char ini[] = "speedup=0\ncard1=\n";
    snprintf(path, sizeof path, "%s/settings.ini", from);
    put(path, ini, sizeof ini - 1);
    snprintf(path, sizeof path, "%s/card1.mcd", from);
    put(path, card, sizeof card);
    snprintf(path, sizeof path, "%s/states/US/slot1.xgs", from);
    put(path, state, sizeof state);
    snprintf(path, sizeof path, "%s/empty.txt", from);
    put(path, "", 0);

    int files = -1;
    CHECK(backupWrite(from, zip, &files, why, sizeof why));
    CHECK(files == 4);
    files = -1;
    CHECK(backupRead(zip, to, &files, why, sizeof why));
    CHECK(files == 4);
    static const char* const names[] = {"settings.ini", "card1.mcd", "states/US/slot1.xgs", "empty.txt"};
    for (int i = 0; i < 4; i++) {
        snprintf(path, sizeof path, "%s/%s", from, names[i]);
        snprintf(other, sizeof other, "%s/%s", to, names[i]);
        CHECK(same(path, other));
    }

    /* A damaged copy: one byte of the card changed. Nothing is written. */
    size_t n = 0;
    unsigned char* z = SDL_LoadFile(zip, &n);
    CHECK(z != NULL && n > sizeof card);
    char bad[1200];
    snprintf(bad, sizeof bad, "%s/bad.zip", base);
    size_t flipped = 0;
    for (size_t i = 0; z != NULL && i + 9 < n; i++) {
        if (memcmp(z + i, "card1.mcd", 9) == 0) {
            flipped = i + 9 + 1000;
            z[flipped] ^= 1;
            break;
        }
    }
    put(bad, z, n);
    if (z != NULL) z[flipped] ^= 1;
    snprintf(other, sizeof other, "%s/again", base);
    CHECK(SDL_CreateDirectory(other));
    CHECK(!backupRead(bad, other, &files, why, sizeof why) && strstr(why, "damaged") != NULL);
    snprintf(path, sizeof path, "%s/again/settings.ini", base);
    SDL_PathInfo info;
    CHECK(!SDL_GetPathInfo(path, &info)); /* the check comes before any file is written */

    /* A name that leaves the folder: refused. */
    for (size_t i = 0; z != NULL && i + 12 < n; i++) {
        if (memcmp(z + i, "settings.ini", 12) == 0) memcpy(z + i, "../ettings.i", 12);
    }
    put(bad, z, n);
    CHECK(!backupRead(bad, other, &files, why, sizeof why) && strstr(why, "outside") != NULL);
    SDL_free(z);

    /* Not a zip. */
    put(bad, ini, sizeof ini - 1);
    CHECK(!backupRead(bad, other, &files, why, sizeof why) && strstr(why, "not a .zip") != NULL);

    removeAll(base);
    printf(failures ? "backuptest: %d failures\n" : "backuptest: ok\n", failures);
    return failures != 0;
}
