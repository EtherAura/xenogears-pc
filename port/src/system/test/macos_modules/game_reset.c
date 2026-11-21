/* Reset the linked game objects. Only the field and battle overlays are present. */
#include <inttypes.h>
#include <mach-o/getsect.h>
#include <mach-o/ldsyms.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    const char *name;
    unsigned char *data, *dataEnd, *bss, *bssEnd;
} Module;
typedef struct { unsigned char *dst; unsigned src, size; } Copy;
extern Module portModules[];
extern const int portModuleCount;
extern const Copy xgSrcCopies_field[], xgSrcCopies_battle[];
extern const int xgSrcCount_field, xgSrcCount_battle;
extern const unsigned long long xgSrcHash_field, xgSrcHash_battle;
extern char fieldSceneState[], battleEntities[];
void portModulesInit(void);
void portResetModule(const char *);
void slowdownModeEntry(int mode) { (void)mode; abort(); }
void xgStateIo(void *s, const char *tag, void *p, size_t n) {
    (void)s; (void)tag; (void)p; (void)n; abort();
}

static void require(int ok, const char *message) {
    if (!ok) { fprintf(stderr, "FAIL: %s\n", message); exit(1); }
}

static int contains(const unsigned char *lo, const unsigned char *hi, uintptr_t p, size_t len) {
    return (uintptr_t)lo <= p && p <= (uintptr_t)hi && len <= (uintptr_t)hi - p;
}

static void restore(const Module *m, const Copy *copies, int n, uint64_t expected, const char *filename) {
    FILE *f = fopen(filename, "rb");
    require(f && n > 0, "missing source evidence");
    uint64_t hash = UINT64_C(0xcbf29ce484222325);
    size_t total = 0;
    for (int i = 0; i < n; ++i) {
        Copy c = copies[i];
        require(contains(m->data, m->dataEnd, (uintptr_t)c.dst, c.size), "copy outside module data");
        require(!fseek(f, c.src, SEEK_SET), "bad source offset");
        for (unsigned j = 0; j < c.size; ++j) {
            int v = fgetc(f);
            require(!c.dst[j] && v > 0, "expected a stripped and restorable byte");
            c.dst[j] = (unsigned char)v;
            hash = (hash ^ c.dst[j]) * UINT64_C(0x100000001b3);
        }
        total += c.size;
    }
    fclose(f);
    require(hash == expected, "restored bytes failed the game build's hash");
    printf("%s restored %zu bytes in %d copies, FNV-1a %016" PRIx64 "\n", m->name, total, n, hash);
}

static int filled(const unsigned char *p, size_t n, int value) {
    for (size_t i = 0; i < n; ++i) if (p[i] != value) return 0;
    return 1;
}

int main(int argc, char **argv) {
    require(argc == 3 && portModuleCount == 2, "expected field and battle inputs");
    const Copy *copies[] = {xgSrcCopies_field, xgSrcCopies_battle};
    const int counts[] = {xgSrcCount_field, xgSrcCount_battle};
    const uint64_t hashes[] = {xgSrcHash_field, xgSrcHash_battle};
    const char *names[] = {"field", "battle"};
    const void *exports[] = {fieldSceneState, battleEntities};
    unsigned char *initial[2];
    size_t data[2], bss[2];
    /* Field's data points only at its code (the opcode tables), battle's at its own data too. */
    unsigned long textSize = 0;
    const unsigned char *text = getsegmentdata(&_mh_execute_header, "__TEXT", &textSize);
    require(text != NULL && textSize > 0, "the executable's code range");
    for (int i = 0; i < 2; ++i) {
        Module m = portModules[i];
        require(!strcmp(m.name, names[i]) && m.dataEnd > m.data && m.bssEnd > m.bss, "generated bounds");
        require(contains(m.data, m.dataEnd, (uintptr_t)exports[i], 1) ||
                contains(m.bss, m.bssEnd, (uintptr_t)exports[i], 1), "game export outside its module");
        restore(&m, copies[i], counts[i], hashes[i], argv[i + 1]);
        data[i] = (uintptr_t)m.dataEnd - (uintptr_t)m.data;
        bss[i] = (uintptr_t)m.bssEnd - (uintptr_t)m.bss;
        initial[i] = malloc(data[i]);
        require(initial[i] != NULL, "allocate comparison image");
        memcpy(initial[i], m.data, data[i]);
        size_t own = 0, code = 0;
        for (size_t j = 0; j + sizeof(uintptr_t) <= data[i]; j += sizeof(uintptr_t)) {
            uintptr_t p;
            memcpy(&p, m.data + j, sizeof(p));
            if (contains(m.data, m.dataEnd, p, 1) || contains(m.bss, m.bssEnd, p, 1)) ++own;
            else if (contains(text, text + textSize, p, 1)) ++code;
        }
        require(own + code > 0, "expected live pointers in the module's data");
        printf("%s has %zu data bytes, %zu BSS bytes, pointer slots: %zu into its data and BSS, %zu into code\n",
               m.name, data[i], bss[i], own, code);
    }
    portModulesInit();
    for (int i = 0; i < 2; ++i) {
        int peer = 1 - i;
        for (int j = 0; j < 2; ++j) {
            memset(portModules[j].data, 0x71 + j, data[j]);
            memset(portModules[j].bss, 0x41 + j, bss[j]);
        }
        portResetModule(names[i]);
        require(!memcmp(portModules[i].data, initial[i], data[i]), "initialized data or live pointer changed");
        require(filled(portModules[i].bss, bss[i], 0), "BSS failed to clear");
        require(filled(portModules[peer].data, data[peer], 0x71 + peer) &&
                filled(portModules[peer].bss, bss[peer], 0x41 + peer), "peer changed during reset");
        printf("PASS: US Original %s reset restored all data and pointers, cleared BSS and preserved %s\n",
               names[i], names[peer]);
    }
    free(initial[0]); free(initial[1]);
    return 0;
}
