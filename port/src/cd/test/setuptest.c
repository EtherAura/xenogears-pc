/* The setup screen's discovery (cd/setup.c) on synthetic images in scratch
 * folders: images in a folder and in the folders directly in it, .cue before
 * .bin, region and disc order, the starting/other pick, the BIOS search by
 * name and size, and the unattended rule. */
#include "cd/setup.h"
#include "cd/cd.h"
#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#ifdef _WIN32
#include <direct.h>
#define chdir _chdir
#else
#include <unistd.h>
#endif

static int checks;
#include "test/check.h"

/* A minimal raw image that cdVerifyImage accepts (as verifytest.c's). */
static uint8_t image[80 * 2352];
static uint8_t* sector(int i) {
    return image + i * 2352 + 24;
}
static void put32(uint8_t* p, unsigned n) {
    for (int i = 0; i < 4; i++) p[i] = (uint8_t)(n >> (8 * i));
}
static void fixture(int disc, int jp) {
    memset(image, 0, sizeof image);
    for (int i = 0; i < 80; i++) image[i * 2352 + 15] = 2;
    memcpy(sector(23), disc == 1 ? "DS01_XENOGEARS" : "DS02_XENOGEARS", 14);
    uint8_t* p = sector(16);
    p[0] = 1;
    memcpy(p + 1, "CD001", 5);
    put32(p + 158, 44);
    put32(p + 166, 2048);
    p = sector(44);
    p[0] = 46;
    put32(p + 2, 43);
    put32(p + 10, 100);
    p[32] = 12;
    memcpy(p + 33, "SYSTEM.CNF;1", 12);
    const char* boot = jp ? disc == 1 ? "SLPS_011.60" : "SLPS_011.61" : disc == 1 ? "SLUS_006.64" : "SLUS_006.69";
    snprintf((char*)sector(43), 2048, "BOOT = cdrom:\\%s;1\r\n", boot);
    p = sector(24);
    p[0] = 45;
    put32(p + 3, 2048);
    memset(p + 7, 255, 3);
    p = sector(40);
    memset(p, 255, 120);
    p[0] = 1;
    p[1] = 0;
    p[120] = (uint8_t)disc;
}
static int save(const char* path, size_t size) {
    FILE* f = fopen(path, "wb");
    if (!f) return 0;
    int ok = fwrite(image, 1, size, f) == size;
    return fclose(f) == 0 && ok;
}
static int text(const char* path, const char* body) {
    FILE* f = fopen(path, "wb");
    if (!f) return 0;
    fputs(body, f);
    return fclose(f) == 0;
}

/* The drive's functions swap.c would want (not used here). */
int cdChangeDisc(const char* p) { (void)p; return 1; }
int cdInsertDisc(const char* p) { (void)p; return 1; }

int main(int argc, char** argv) {
    char root[1024], dir[1024], path[1024], out[4096];
    const char* given = argc > 1 ? argv[1] : "build/pc/setup-fixtures";
    if (given[0] == '/' || (given[0] && given[1] == ':')) {
        snprintf(root, sizeof root, "%s", given);
    } else {
        char* cwd = SDL_GetCurrentDirectory();
        snprintf(root, sizeof root, "%s%s", cwd ? cwd : "", given);
        SDL_free(cwd);
    }
    /* root/images/us1/US 1.cue + .bin, root/images/us2/u2.bin (no cue: the .bin),
     * root/images/jp1.cue + .bin, root/images/notes.cue (not an image),
     * root/images/deep/deeper/x.bin (too deep), root/bios/{SCPH5501.BIN 512 KB, bios.bin small} */
    snprintf(path, sizeof path, "%s/bios/SCPH5501.BIN", root);
    SDL_RemovePath(path); /* (from an earlier run: made below, after the check without it) */
    snprintf(dir, sizeof dir, "%s/images/us1", root); CHECK(SDL_CreateDirectory(dir));
    snprintf(dir, sizeof dir, "%s/images/us2", root); CHECK(SDL_CreateDirectory(dir));
    snprintf(dir, sizeof dir, "%s/images/deep/deeper", root); CHECK(SDL_CreateDirectory(dir));
    snprintf(dir, sizeof dir, "%s/bios", root); CHECK(SDL_CreateDirectory(dir));
    snprintf(dir, sizeof dir, "%s/empty", root); CHECK(SDL_CreateDirectory(dir));
    fixture(1, 0);
    snprintf(path, sizeof path, "%s/images/us1/US 1.bin", root); CHECK(save(path, sizeof image));
    snprintf(path, sizeof path, "%s/images/us1/US 1.cue", root);
    CHECK(text(path, "FILE \"US 1.bin\" BINARY\n  TRACK 01 MODE2/2352\n    INDEX 01 00:00:00\n"));
    fixture(2, 0);
    snprintf(path, sizeof path, "%s/images/us2/u2.bin", root); CHECK(save(path, sizeof image));
    fixture(1, 1);
    snprintf(path, sizeof path, "%s/images/jp1.bin", root); CHECK(save(path, sizeof image));
    snprintf(path, sizeof path, "%s/images/jp1.cue", root);
    CHECK(text(path, "FILE \"jp1.bin\" BINARY\n  TRACK 01 MODE2/2352\n    INDEX 01 00:00:00\n"));
    snprintf(path, sizeof path, "%s/images/notes.cue", root); CHECK(text(path, "not a sheet\n"));
    fixture(1, 0);
    snprintf(path, sizeof path, "%s/images/deep/deeper/x.bin", root); CHECK(save(path, sizeof image));

    CdSetupImage found[8];
    snprintf(dir, sizeof dir, "%s/images", root);
    int n = cdSetupFindImages(dir, found, 8);
    CHECK(n == 3); /* jp1.cue, us1/US 1.cue, us2/u2.bin: not notes.cue, not deep/deeper */
    CHECK(!strcmp(found[0].info.region, "US") && found[0].info.disc == 1 && strstr(found[0].path, "us1/US 1.cue"));
    CHECK(!strcmp(found[1].info.region, "US") && found[1].info.disc == 2 && strstr(found[1].path, "us2/u2.bin"));
    CHECK(!strcmp(found[2].info.region, "JP") && found[2].info.disc == 1 && strstr(found[2].path, "jp1.cue"));
    int first, other;
    CHECK(cdSetupPick(found, n, "US", &first, &other) && first == 0 && other == 1);
    CHECK(cdSetupPick(found, n, "JP", &first, &other) && first == 2 && other == -1);
    CHECK(cdSetupPick(found, n, NULL, &first, &other) && first == 0 && other == 1);
    CHECK(cdSetupPick(found + 1, 1, "US", &first, &other) && first == 0 && other == -1); /* Disc 2 alone */
    CHECK(!cdSetupPick(found + 2, 1, "US", &first, &other));
    CHECK(cdSetupFindImages(dir, found, 1) == 1);
    snprintf(dir, sizeof dir, "%s/empty", root);
    CHECK(cdSetupFindImages(dir, found, 8) == 0);
    CHECK(cdSetupFindImages("/no/such/folder", found, 8) == 0);

    /* Discovery near a path: its folder, the one above it, the folders in them. */
    snprintf(path, sizeof path, "%s/images/us1/gone.cue", root);
    const char* near[] = {path};
    CHECK(cdSetupDiscover("US", near, 1, out) && strstr(out, "us1/US 1.cue"));
    CHECK(cdSetupDiscover("JP", near, 1, out) && strstr(out, "jp1.cue"));
    /* (the current folder is searched too: an empty one) */
    snprintf(dir, sizeof dir, "%s/empty", root);
    CHECK(chdir(dir) == 0);
    snprintf(path, sizeof path, "%s/empty/gone.cue", root);
    /* the folder above (root) and the folders in it (root/images): not root/images/us1 */
    CHECK(cdSetupDiscover("JP", near, 1, out) && strstr(out, "jp1.cue"));
    CHECK(!cdSetupDiscover("US", near, 1, out));

    /* BIOS: by name (any case), 512 KB only; a file given is taken as it is. */
    memset(image, 0, sizeof image);
    snprintf(path, sizeof path, "%s/bios/bios.bin", root); CHECK(save(path, 1000));
    snprintf(dir, sizeof dir, "%s/bios", root);
    CHECK(!cdSetupFindBios(dir, out, sizeof out));
    FILE* f;
    snprintf(path, sizeof path, "%s/bios/SCPH5501.BIN", root);
    f = fopen(path, "wb"); CHECK(f != NULL);
    for (int i = 0; i < 0x80000 / 1024; i++) fwrite(image, 1, 1024, f);
    CHECK(fclose(f) == 0);
    CHECK(cdSetupFindBios(dir, out, sizeof out) && strstr(out, "SCPH5501.BIN"));
    CHECK(cdSetupIsBios(path) && cdSetupFindBios(path, out, sizeof out) && !strcmp(out, path));
    snprintf(path, sizeof path, "%s/bios/bios.bin", root);
    CHECK(!cdSetupIsBios(path) && !cdSetupIsBios("") && !cdSetupIsBios(NULL));
    CHECK(!cdSetupFindBios("/no/such/folder", out, sizeof out));

    /* Unattended runs never wait for the setup screen. */
    SDL_SetEnvironmentVariable(SDL_GetEnvironment(), "XG_HEADLESS", "1", true);
    SDL_UnsetEnvironmentVariable(SDL_GetEnvironment(), "XG_UI_SCRIPT");
    CHECK(!cdSetupInteractive());
    CHECK(cdSetupFirst(NULL, "/no/such/image.cue", "US", out) == 0);
    snprintf(path, sizeof path, "%s/images/us1/US 1.cue", root);
    CHECK(cdSetupFirst(NULL, path, "US", out) == CD_SETUP_OK);
    CHECK(cdSetupFirst(NULL, path, "JP", out) == 0); /* the wrong release */
    SDL_SetEnvironmentVariable(SDL_GetEnvironment(), "XG_UI_SCRIPT", "x", true);
    CHECK(cdSetupInteractive());
    snprintf(path, sizeof path, "%s/images/us1/gone.cue", root);
    CHECK(cdSetupFirst(NULL, path, "US", out) == CD_SETUP_FOUND && strstr(out, "us1/US 1.cue"));
    snprintf(path, sizeof path, "%s/nowhere/gone.cue", root);
    CHECK(cdSetupFirst(NULL, path, "JP", out) == CD_SETUP_FOUND); /* root/images is a folder in root */
    CHECK(cdSetupFirst(NULL, "/no/such/x/image.cue", "US", out) == CD_SETUP_NONE && out[0] == 0);
    printf("disc setup: %d checks passed\n", checks);
    return 0;
}
