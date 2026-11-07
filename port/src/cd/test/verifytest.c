#include "cd/verify.h"
#include "cd/cd.h"
#include "system/settings.h"
#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

static int checks, driveCalls;
#include "test/check.h"
int cdChangeDisc(const char* p) {
    (void)p;
    driveCalls++;
    return 1;
}
int cdInsertDisc(const char* p) {
    (void)p;
    driveCalls++;
    return 1;
}
double hwNow(void) {
    return 0;
}
void hwScheduleAt(double when, void (*callback)(void*), void* arg) {
    (void)when;
    (void)callback;
    (void)arg;
}
static uint8_t image[80 * 2352];
static uint8_t* sector(int i) {
    return image + i * 2352 + 24;
}
static void put32(uint8_t* p, unsigned n) {
    for (int i = 0; i < 4; i++)
        p[i] = (uint8_t)(n >> (8 * i));
}
static void fixture(int disc, int jp) {
    memset(image, 0, sizeof image);
    for (int i = 0; i < 80; i++)
        image[i * 2352 + 15] = 2;
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
static int save(const char* path) {
    FILE* f = fopen(path, "wb");
    if (!f)
        return 0;
    int ok = fwrite(image, 1, sizeof image, f) == sizeof image;
    return fclose(f) == 0 && ok;
}
int main(int argc, char** argv) {
    const char* dir = argc > 1 ? argv[1] : "build/pc/discverify-fixtures";
    CHECK(SDL_CreateDirectory(dir));
    char paths[4][1024], bad[1024], cue[1024], why[1024];
    CdImageInfo info[2];
    for (int i = 0; i < 4; i++) {
        snprintf(paths[i], sizeof paths[i], "%s/%s%d.bin", dir, i < 2 ? "us" : "jp", 1 + (i & 1));
        fixture(1 + (i & 1), i >= 2);
        CHECK(save(paths[i]));
        CHECK(cdVerifyImage(paths[i], &info[0], why, sizeof why));
        CHECK(info[0].disc == 1 + (i & 1) && info[0].entries == 1 && info[0].sectors == 80);
        CHECK(!strcmp(info[0].region, i < 2 ? "US" : "JP"));
    }
    CHECK(cdVerifyPair(paths[0], paths[1], "US", info, why, sizeof why));
    CHECK(cdVerifyPair(paths[1], paths[0], "US", info, why, sizeof why));
    CHECK(cdVerifyPair(paths[2], paths[3], "JP", info, why, sizeof why));
    CHECK(cdVerifyPair(paths[0], NULL, "US", info, why, sizeof why));
    CHECK(!cdVerifyPair(paths[2], NULL, "US", info, why, sizeof why) && strstr(why, "requires US"));
    CHECK(!cdVerifyPair(paths[0], paths[0], NULL, info, why, sizeof why) && strstr(why, "same disc"));
    CHECK(!cdVerifyPair(paths[0], paths[3], NULL, info, why, sizeof why) && strstr(why, "different regions"));
    CHECK(!cdVerifyImage("", &info[0], why, sizeof why));
    snprintf(bad, sizeof bad, "%s/bad.bin", dir);
    snprintf(cue, sizeof cue, "%s/bad.cue", dir);
    CHECK(!cdVerifyImage(bad, &info[0], why, sizeof why));
    FILE* f = fopen(cue, "wb");
    CHECK(f != NULL);
    fprintf(f, "FILE \"missing.bin\" BINARY\nTRACK 01 MODE2/2352\nINDEX 01 00:00:00\n");
    CHECK(fclose(f) == 0);
    CHECK(!cdVerifyImage(cue, &info[0], why, sizeof why) && strstr(why, "missing.bin"));
    f = fopen(cue, "wb"); CHECK(f != NULL);
    fprintf(f, "FILE \"us1.bin\" BINARY\nTRACK 01 MODE2/2352\nINDEX 01 999999999999999999999:00:00\n");
    CHECK(fclose(f) == 0);
    CHECK(!cdVerifyImage(cue, &info[0], why, sizeof why) && strstr(why, "bad INDEX"));
    f = fopen(cue, "wb"); CHECK(f != NULL);
    fprintf(f, "FILE \"us1.bin\" BINARY\nTRACK 01 MODE2/2352\nINDEX 01 00:99:00\n");
    CHECK(fclose(f) == 0);
    CHECK(!cdVerifyImage(cue, &info[0], why, sizeof why) && strstr(why, "bad INDEX"));
    fixture(1, 0);
    image[23 * 2352 + 15] = 1;
    CHECK(save(bad));
    CHECK(!cdVerifyImage(bad, &info[0], why, sizeof why) && strstr(why, "identity"));
    fixture(1, 0);
    sector(44)[0] = 4;
    CHECK(save(bad));
    CHECK(!cdVerifyImage(bad, &info[0], why, sizeof why) && strstr(why, "SYSTEM.CNF"));
    fixture(1, 0);
    sector(44)[32] = 200;
    CHECK(save(bad));
    CHECK(!cdVerifyImage(bad, &info[0], why, sizeof why));
    fixture(1, 0);
    put32(sector(24) + 3, 0x7FFFFFFF);
    CHECK(save(bad));
    CHECK(!cdVerifyImage(bad, &info[0], why, sizeof why) && strstr(why, "beyond"));
    fixture(1, 0);
    sector(40)[120] = 2;
    CHECK(save(bad));
    CHECK(!cdVerifyImage(bad, &info[0], why, sizeof why) && strstr(why, "mismatch"));
    fixture(1, 0);
    memset(sector(40), 255, 120);
    CHECK(save(bad));
    CHECK(!cdVerifyImage(bad, &info[0], why, sizeof why));
    fixture(1, 0);
    sector(40)[0] = 2;
    CHECK(save(bad));
    CHECK(!cdVerifyImage(bad, &info[0], why, sizeof why) && strstr(why, "directory"));
    CHECK(cdImageSibling("/Disc 1/x Disc 1.cue", bad, sizeof bad) && !strcmp(bad, "/Disc 2/x Disc 2.cue"));
    CHECK(!cdImageSibling("x.cue", bad, sizeof bad));
    CHECK(!cdImageSibling("Disc 1.cue", bad, 2));
    SDL_SetEnvironmentVariable(SDL_GetEnvironment(), "XG_SETTINGS", "none", true);
    SDL_SetEnvironmentVariable(SDL_GetEnvironment(), "XG_DISC2", paths[1], true);
    cdSwapInit(paths[0]);
    CHECK(!strcmp(cdSwapPath(1), paths[1]));
    CHECK(!cdSwapSetOther(paths[0]) && !strcmp(cdSwapPath(1), paths[1]));
    CHECK(!cdSwapSetOther(paths[3]) && !strcmp(cdSwapPath(1), paths[1]));
    CHECK(settingsOverride("disc2") != NULL); /* rejection did not change settings */
    char automatic[2][1024];
    snprintf(automatic[0], sizeof automatic[0], "%s/Disc 1.bin", dir);
    snprintf(automatic[1], sizeof automatic[1], "%s/Disc 2.bin", dir);
    fixture(1, 0);
    CHECK(save(automatic[0]));
    fixture(2, 1);
    CHECK(save(automatic[1]));
    SDL_SetEnvironmentVariable(SDL_GetEnvironment(), "XG_DISC2", "", true);
    cdSwapInit(automatic[0]);
    CHECK(cdSwapPath(1) == NULL);
    CHECK(cdSwapSetOther("")); /* invalid automatically found images do not block one-disc use */
    CHECK(cdSwapPath(1) == NULL);
    CHECK(driveCalls == 0);
    printf("Disc verification: %d checks passed\n", checks);
    return 0;
}
