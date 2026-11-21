#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "module.h"

void portModulesInit(void);
void portResetModule(const char *name);
void portClearModuleBss(const char *name);

typedef struct {
    const char *name;
    char *dataStart, *dataEnd, *bssStart, *bssEnd;
} PortModule;

#define BOUNDS(id, section) \
    extern char id##Start[] __asm("section$start$__DATA$" #section); \
    extern char id##End[] __asm("section$end$__DATA$" #section)
BOUNDS(fieldData, xgd01);
BOUNDS(fieldBss, xgb01);
BOUNDS(battleData, xgd02);
BOUNDS(battleBss, xgb02);

PortModule portModules[] = {
    {"field", fieldDataStart, fieldDataEnd, fieldBssStart, fieldBssEnd},
    {"battle", battleDataStart, battleDataEnd, battleBssStart, battleBssEnd},
};
const int portModuleCount = 2;

static void check(int ok, const char *what) {
    if (!ok) {
        fprintf(stderr, "FAIL: %s\n", what);
        exit(1);
    }
}

static int within(const void *p, size_t n, const char *start, const char *end) {
    uintptr_t addr = (uintptr_t)p;
    return addr >= (uintptr_t)start && addr <= (uintptr_t)end && n <= (uintptr_t)end - addr;
}

enum { IMAGE_SIZE = 56 };
_Static_assert(offsetof(Data, target) == 24 && offsetof(Data, after) == 32, "arm64 layout");

static const struct {
    size_t module, dest, source, length;
} copies[] = {
#include "copies.inc"
};

static void checkFresh(View v, const unsigned char *image, int seed) {
    check(!memcmp(v.data->before, image, 24) && !memcmp(v.data->after, image + 32, 24),
          "both source byte ranges restored");
    check(v.data->seed == seed && v.data->target == &v.common[1], "seed and relocated pointer restored");
    check(v.common[0] == 0 && v.common[1] == 0 && *v.bss == 0, "common and BSS cleared");
}

static void dirty(View v, int *other) {
    memset(v.data->before, 0xa5, sizeof(v.data->before));
    memset(v.data->after, 0x5a, sizeof(v.data->after));
    v.data->seed = -1;
    v.data->target = other;
    v.common[0] = 83;
    v.common[1] = 84;
    *v.bss = 85;
}

static void checkDirty(View v, int *other) {
    for (size_t i = 0; i < sizeof(v.data->before); ++i) {
        check(v.data->before[i] == 0xa5 && v.data->after[i] == 0x5a, "peer payload retained");
    }
    check(v.data->seed == -1 && v.data->target == other, "peer data and pointer retained");
    check(v.common[0] == 83 && v.common[1] == 84 && *v.bss == 85, "peer common and BSS retained");
}

int main(int argc, char **argv) {
    check(argc == 2, "expected synthetic source file");
    unsigned char image[2][IMAGE_SIZE];
    FILE *f = fopen(argv[1], "rb");
    check(f && fread(image, 1, sizeof(image), f) == sizeof(image) && fgetc(f) == EOF, "read source file");
    fclose(f);

    View v[] = {fieldView(), battleView()};
    for (int i = 0; i < 2; ++i) {
        PortModule m = portModules[i];
        check(v[i].peer().data == v[1-i].data, "cross-module export resolved");
        check(within(v[i].data, sizeof(Data), m.dataStart, m.dataEnd), "data inside own linker bounds");
        check(within(v[i].common, sizeof(int[2]), m.bssStart, m.bssEnd), "common inside own BSS bounds");
        check(within(v[i].bss, sizeof(int), m.bssStart, m.bssEnd), "static inside own BSS bounds");
        check(v[i].data->target == &v[i].common[1], "dyld relocated the pointer with its addend");
        const unsigned char zeros[24] = {0};
        check(!memcmp(v[i].data->before, zeros, 24) && !memcmp(v[i].data->after, zeros, 24),
              "source bytes stripped on both sides of pointer");
    }
    puts("PASS: linker bounds, duplicate private names, cross-module exports and rebased pointers");
    for (size_t i = 0; i < sizeof(copies) / sizeof(copies[0]); ++i) {
        memcpy((char *)v[copies[i].module].data + copies[i].dest,
               image[copies[i].module] + copies[i].source, copies[i].length);
    }
    for (int i = 0; i < 2; ++i) {
        checkFresh(v[i], image[i], i + 1);
    }
    portModulesInit();
    for (int i = 0; i < 2; ++i) {
        int peer = 1 - i;
        dirty(v[i], v[peer].common);
        dirty(v[peer], v[i].common);
        portResetModule(portModules[i].name);
        checkFresh(v[i], image[i], i + 1);
        checkDirty(v[peer], v[i].common);
        printf("PASS: %s reset restored data, source bytes and pointer, cleared common/BSS, and preserved %s\n",
               portModules[i].name, portModules[peer].name);
    }
    portClearModuleBss("field");
    check(*v[0].bss == 0 && !v[0].common[0] && !v[0].common[1], "BSS-only clear");
    check(v[0].data->seed == -1 && v[0].data->target == v[1].common, "BSS-only clear preserved data");
    puts("PASS: BSS-only clear preserved initialized data and pointer");
    return 0;
}
