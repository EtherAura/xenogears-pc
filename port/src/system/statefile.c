/*
 * Save state files (statefile.h).
 *
 * The build id: a state holds return addresses, pointers to statics and the
 * stack, so it loads only in an executable whose code and data sit where they
 * sat. The id hashes the code itself and every allocated section's name,
 * address and size (identical code at the same address reaches every object
 * at the same address). A rebuild that changes neither keeps its states; any
 * other refuses them, with a message.
 *
 * The image id: the same at the image's link addresses, over what loads (no
 * debug sections), so that tools/state_schema.py gets it from the file, before
 * or after the debug information is stripped, and a state schema can say
 * which image it describes wherever that image is loaded.
 */

#ifndef _WIN32
#define _GNU_SOURCE 1 /* dl_iterate_phdr (link.h) */
#endif
#include "statefile.h"

#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "release.h"
#include "settings.h"
#include "statecodec.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <elf.h>
#include <link.h>
#endif

#define MAGIC "XGSTATE"
#define HEADER_SIZE 256

/* ---- the build id ---------------------------------------------------------------- */

static uint8_t buildId[16];
static char buildText[33];
static int buildKnown;
static uintptr_t imageBias;
static uint8_t imageId[16];
static char imageText[33];
static int imageKnown; /* 1 known, -1 unknown (the sections could not be read) */

static void idMix(uint64_t* a, uint64_t* b, const void* data, size_t n) {
    *a = stateHash(data, n, *a);
    *b = stateHash(data, n, *b ^ 0x5851F42D4C957F2Dull);
}

#ifdef _WIN32
static const IMAGE_NT_HEADERS* peHeaders(const unsigned char** base) {
    *base = (const unsigned char*)GetModuleHandleW(NULL);
    const IMAGE_DOS_HEADER* dos = (const IMAGE_DOS_HEADER*)*base;
    return (const IMAGE_NT_HEADERS*)(*base + dos->e_lfanew);
}

static int computeId(uint64_t* a, uint64_t* b) {
    const unsigned char* base;
    const IMAGE_NT_HEADERS* nt = peHeaders(&base);
    const IMAGE_SECTION_HEADER* sec = IMAGE_FIRST_SECTION(nt);
    uint64_t imageBase = (uint64_t)(uintptr_t)base;
    idMix(a, b, &imageBase, sizeof(imageBase));
    for (int i = 0; i < nt->FileHeader.NumberOfSections; i++) {
        uint32_t where[3] = {sec[i].VirtualAddress, sec[i].Misc.VirtualSize, sec[i].Characteristics};
        idMix(a, b, sec[i].Name, 8);
        idMix(a, b, where, sizeof(where));
        if (sec[i].Characteristics & IMAGE_SCN_CNT_CODE) {
            idMix(a, b, base + sec[i].VirtualAddress, sec[i].Misc.VirtualSize);
        }
    }
    return 1;
}

/* The image id (tools/state_schema.py pe_identity): the preferred image base and every section that
 * stays loaded, its place, size and flags (not its name: a long one is an offset into the symbols'
 * string table, which stripping rewrites), and the code. Debug sections are discardable and go
 * with the debug information. */
static int computeImageId(uint64_t* a, uint64_t* b) {
    const unsigned char* base;
    const IMAGE_NT_HEADERS* nt = peHeaders(&base);
    const IMAGE_SECTION_HEADER* sec = IMAGE_FIRST_SECTION(nt);
    uint64_t preferred = (uint64_t)nt->OptionalHeader.ImageBase;
    idMix(a, b, &preferred, sizeof(preferred));
    for (int i = 0; i < nt->FileHeader.NumberOfSections; i++) {
        if (sec[i].Characteristics & IMAGE_SCN_MEM_DISCARDABLE) {
            continue;
        }
        uint32_t where[3] = {sec[i].VirtualAddress, sec[i].Misc.VirtualSize, sec[i].Characteristics};
        idMix(a, b, where, sizeof(where));
        if (sec[i].Characteristics & IMAGE_SCN_CNT_CODE) {
            idMix(a, b, base + sec[i].VirtualAddress, sec[i].Misc.VirtualSize);
        }
    }
    return 1;
}

/* The bias, before main as on ELF (states record it while the game is parked on its own stacks): where
 * the executable sits against its preferred base (0: linked with --disable-dynamicbase, never moved). */
__attribute__((constructor)) static void imageBiasInit(void) {
    const unsigned char* base;
    const IMAGE_NT_HEADERS* nt = peHeaders(&base);
    imageBias = (uintptr_t)base - (uintptr_t)nt->OptionalHeader.ImageBase;
}

/* Whether [lo, hi) lies in one readable section of this executable as loaded. */
int stateImageReadable(uint64_t lo, uint64_t hi) {
    const unsigned char* base;
    const IMAGE_NT_HEADERS* nt = peHeaders(&base);
    const IMAGE_SECTION_HEADER* sec = IMAGE_FIRST_SECTION(nt);
    if (hi < lo) return 0;
    for (int i = 0; i < nt->FileHeader.NumberOfSections; i++) {
        uint64_t at = (uint64_t)(uintptr_t)base + sec[i].VirtualAddress;
        if ((sec[i].Characteristics & IMAGE_SCN_MEM_READ) && lo >= at && hi <= at + sec[i].Misc.VirtualSize) {
            return 1;
        }
    }
    return 0;
}

const char* stateImagePath(void) {
    static char path[1100];
    static int known;
    if (!known) {
        wchar_t wide[1024];
        DWORD n = GetModuleFileNameW(NULL, wide, (DWORD)(sizeof wide / sizeof wide[0]));
        known = n > 0 && n < sizeof wide / sizeof wide[0] &&
                        WideCharToMultiByte(CP_UTF8, 0, wide, -1, path, (int)sizeof path, NULL, NULL) > 0
                    ? 1
                    : -1;
    }
    return known == 1 ? path : NULL;
}
#else
/* The ELF object this code is in (the executable, or the library an Android
 * app loads) and where it sits: its load bias (0 for a non-PIE executable)
 * and the file to read its sections from. */
typedef struct {
    uintptr_t inside, bias;
    const char* path;
    int found;
} SelfObject;

static int findSelf(struct dl_phdr_info* info, size_t size, void* data) {
    (void)size;
    SelfObject* self = data;
    for (int i = 0; i < info->dlpi_phnum; i++) {
        const ElfW(Phdr)* ph = &info->dlpi_phdr[i];
        uintptr_t lo = info->dlpi_addr + ph->p_vaddr;
        if (ph->p_type == PT_LOAD && self->inside >= lo && self->inside < lo + ph->p_memsz) {
            self->bias = info->dlpi_addr;
            self->path = info->dlpi_name != NULL && info->dlpi_name[0] != '\0' ? info->dlpi_name : "/proc/self/exe";
            self->found = 1;
            return 1;
        }
    }
    return 0;
}

static const char* imagePath;

/* The bias, before main (or at the library's load): states record it while
 * the game is parked, on the game's stacks, where a call into the C library
 * would leave this run's host values below the game's frames. */
__attribute__((constructor)) static void imageBiasInit(void) {
    SelfObject self = {.inside = (uintptr_t)&imageBiasInit};
    dl_iterate_phdr(findSelf, &self);
    imageBias = self.found ? self.bias : 0;
    imagePath = self.found ? self.path : NULL; /* the loader's name for it: kept as long as it is loaded */
}

typedef struct {
    uintptr_t inside, lo, hi;
    int readable;
} ReadableQuery;

/* Whether [lo, hi) is in one of this object's PT_LOAD segments that can be read. */
static int findReadable(struct dl_phdr_info* info, size_t size, void* data) {
    (void)size;
    ReadableQuery* q = data;
    int self = 0;
    for (int i = 0; i < info->dlpi_phnum; i++) {
        const ElfW(Phdr)* ph = &info->dlpi_phdr[i];
        uintptr_t lo = info->dlpi_addr + ph->p_vaddr;
        if (ph->p_type == PT_LOAD && q->inside >= lo && q->inside < lo + ph->p_memsz) self = 1;
    }
    if (!self) return 0;
    for (int i = 0; i < info->dlpi_phnum; i++) {
        const ElfW(Phdr)* ph = &info->dlpi_phdr[i];
        uintptr_t lo = info->dlpi_addr + ph->p_vaddr;
        if (ph->p_type == PT_LOAD && (ph->p_flags & PF_R) && q->lo >= lo && q->hi <= lo + ph->p_memsz) q->readable = 1;
    }
    return 1;
}

int stateImageReadable(uint64_t lo, uint64_t hi) {
    ReadableQuery q = {.inside = (uintptr_t)&findReadable, .lo = (uintptr_t)lo, .hi = (uintptr_t)hi};
    if (hi < lo) return 0;
    dl_iterate_phdr(findReadable, &q);
    return q.readable;
}

const char* stateImagePath(void) {
    return imagePath;
}

/* The id of this image loaded at bias (its own: the build id; 0: the image id, as at its link addresses). */
static int elfId(uint64_t bias, uint64_t* a, uint64_t* b) {
    SelfObject self = {.inside = (uintptr_t)&elfId};
    dl_iterate_phdr(findSelf, &self);
    if (!self.found) {
        return 0;
    }
    /* Position-independent code at another address holds other addresses: a
     * non-zero load bias is part of the id (as the image base is on Windows). */
    if (bias != 0) {
        idMix(a, b, &bias, sizeof(bias));
    }
    FILE* f = fopen(self.path, "rb");
    Elf64_Ehdr eh;
    if (f == NULL || fread(&eh, sizeof(eh), 1, f) != 1 || memcmp(eh.e_ident, ELFMAG, SELFMAG) != 0 ||
        eh.e_shentsize != sizeof(Elf64_Shdr) || eh.e_shnum == 0) {
        if (f != NULL) {
            fclose(f);
        }
        return 0;
    }
    Elf64_Shdr* sh = calloc(eh.e_shnum, sizeof(Elf64_Shdr));
    char* names = NULL;
    int ok = sh != NULL && fseek(f, (long)eh.e_shoff, SEEK_SET) == 0 &&
             fread(sh, sizeof(Elf64_Shdr), eh.e_shnum, f) == eh.e_shnum && eh.e_shstrndx < eh.e_shnum;
    if (ok) {
        Elf64_Shdr* strs = &sh[eh.e_shstrndx];
        names = calloc(1, strs->sh_size + 1);
        ok = names != NULL && fseek(f, (long)strs->sh_offset, SEEK_SET) == 0 &&
             fread(names, 1, strs->sh_size, f) == strs->sh_size;
    }
    for (int i = 0; ok && i < eh.e_shnum; i++) {
        if (!(sh[i].sh_flags & SHF_ALLOC)) {
            continue;
        }
        const char* name = sh[i].sh_name < sh[eh.e_shstrndx].sh_size ? names + sh[i].sh_name : "";
        uint64_t where[4] = {sh[i].sh_addr, sh[i].sh_size, sh[i].sh_type, sh[i].sh_flags};
        idMix(a, b, name, strlen(name));
        idMix(a, b, where, sizeof(where));
        if ((sh[i].sh_flags & SHF_EXECINSTR) && sh[i].sh_type == SHT_PROGBITS) {
            /* The code as loaded (position-independent: the file's bytes wherever it is). */
            idMix(a, b, (const void*)(uintptr_t)(self.bias + sh[i].sh_addr), sh[i].sh_size);
        }
    }
    free(names);
    free(sh);
    fclose(f);
    return ok;
}

static int computeId(uint64_t* a, uint64_t* b) {
    return elfId(imageBias, a, b);
}

/* The image id (tools/state_schema.py elf_identity): the build id at the link addresses. Stripping
 * keeps every allocated section's name, place, size, type and flags, and the code. */
static int computeImageId(uint64_t* a, uint64_t* b) {
    return elfId(0, a, b);
}
#endif

const char* stateAppVersion(void) {
    static char version[16];
    static int known;
    if (!known) {
        known = 1;
        char full[128];
        const char* v = getenv("XG_APP_VERSION");
        snprintf(full, sizeof full, "%s", v != NULL ? v : "");
        char* dot = strstr(full, ".changes"); /* a development build's mark: no room for it */
        if (dot != NULL) *dot = '\0';
        /* Elsewhere the release file's version (release.h; "" for a development build). */
        const char* chosen = full[0] != '\0' ? full : releaseVersion();
        size_t n = strlen(chosen);
        if (n >= sizeof version) n = sizeof version - 1; /* as the header holds it */
        memcpy(version, chosen, n);
        version[n] = '\0';
    }
    return version;
}

const uint8_t* stateBuildId(void) {
    if (!buildKnown) {
        uint64_t a = 0x243F6A8885A308D3ull, b = 0x13198A2E03707344ull;
        if (!computeId(&a, &b)) {
            fprintf(stderr, "state: cannot read this executable's sections: states get a random id\n");
            a ^= SDL_GetPerformanceCounter();
            b ^= (uint64_t)(uintptr_t)&a;
        }
        for (int i = 0; i < 8; i++) {
            buildId[i] = (uint8_t)(a >> (8 * i));
            buildId[8 + i] = (uint8_t)(b >> (8 * i));
        }
        for (int i = 0; i < 16; i++) {
            snprintf(buildText + 2 * i, 3, "%02x", buildId[i]);
        }
        buildKnown = 1;
    }
    return buildId;
}

uintptr_t stateImageBias(void) {
    return imageBias;
}

const uint8_t* stateImageId(void) {
    if (!imageKnown) {
        uint64_t a = 0x243F6A8885A308D3ull, b = 0x13198A2E03707344ull; /* the build id's seeds */
        imageKnown = computeImageId(&a, &b) ? 1 : -1;
        for (int i = 0; i < 8; i++) {
            imageId[i] = (uint8_t)(a >> (8 * i));
            imageId[8 + i] = (uint8_t)(b >> (8 * i));
        }
        for (int i = 0; i < 16; i++) {
            snprintf(imageText + 2 * i, 3, "%02x", imageId[i]);
        }
        if (imageKnown < 0) {
            imageText[0] = '\0';
        }
    }
    return imageKnown > 0 ? imageId : NULL;
}

const char* stateImageIdText(void) {
    stateImageId();
    return imageText;
}

const char* stateBuildText(void) {
    stateBuildId();
    return buildText;
}

/* ---- the header ------------------------------------------------------------------- */

static void put32(uint8_t* p, uint32_t v) {
    for (int i = 0; i < 4; i++) p[i] = (uint8_t)(v >> (8 * i));
}
static void put64(uint8_t* p, uint64_t v) {
    for (int i = 0; i < 8; i++) p[i] = (uint8_t)(v >> (8 * i));
}
static uint32_t get32(const uint8_t* p) {
    uint32_t v = 0;
    for (int i = 0; i < 4; i++) v |= (uint32_t)p[i] << (8 * i);
    return v;
}
static uint64_t get64(const uint8_t* p) {
    uint64_t v = 0;
    for (int i = 0; i < 8; i++) v |= (uint64_t)p[i] << (8 * i);
    return v;
}

/* Offsets in the header block (after magic, version and size: 16 bytes). */
enum {
    H_BUILD = 16, H_REGION = 32, H_DISC = 36, H_SERIAL = 40, H_SECTORS = 56, H_VBLANK = 60, H_SAVED = 64,
    H_PLAY = 72, H_MODE = 76, H_PLACE = 80, H_LABEL = 84, H_FLAGS = 180, H_DUMP = 184, H_THUMBW = 192,
    H_THUMBH = 196, H_BODY = 200, H_HASH = 208, H_THUMBPACKED = 216, H_ENGINE = 220, H_BODYPACKED = 224,
    H_IMAGE = 232, H_APPVERSION = 240 /* 16 bytes, to the header's end */
};

static void headerPack(const StateHeader* h, uint32_t thumbPacked, uint64_t bodyPacked, uint8_t out[HEADER_SIZE]) {
    memset(out, 0, HEADER_SIZE);
    memcpy(out, MAGIC, 8);
    put32(out + 8, STATE_FORMAT);
    put32(out + 12, HEADER_SIZE);
    memcpy(out + H_BUILD, h->buildId, 16);
    memcpy(out + H_REGION, h->region, 4);
    put32(out + H_DISC, h->disc);
    memcpy(out + H_SERIAL, h->serial, 16);
    put32(out + H_SECTORS, h->sectors);
    put32(out + H_VBLANK, h->vblank);
    put64(out + H_SAVED, (uint64_t)h->savedAt);
    put32(out + H_PLAY, h->playTime);
    put32(out + H_MODE, (uint32_t)h->mode);
    put32(out + H_PLACE, (uint32_t)h->place);
    memcpy(out + H_LABEL, h->label, sizeof(h->label));
    out[H_LABEL + sizeof(h->label) - 1] = 0;
    put32(out + H_FLAGS, h->flags);
    put64(out + H_DUMP, h->dumpFrames);
    put32(out + H_THUMBW, h->thumbW);
    put32(out + H_THUMBH, h->thumbH);
    put64(out + H_BODY, h->bodySize);
    put64(out + H_HASH, h->bodyHash);
    put32(out + H_THUMBPACKED, thumbPacked);
    put32(out + H_ENGINE, h->engine); /* system/engine.h; 0 in files from before it: the Original */
    put64(out + H_BODYPACKED, bodyPacked);
    /* For tools (statecheck.py): the image's load bias, which the build id
     * holds too (a PIE or a library at other addresses is another build to a
     * state); 0 in files from before it. */
    put64(out + H_IMAGE, (uint64_t)stateImageBias());
    /* The app's version (stateAppVersion); zeros in files from before it. */
    memcpy(out + H_APPVERSION, h->appVersion, sizeof(h->appVersion));
    out[H_APPVERSION + sizeof(h->appVersion) - 1] = 0;
}

static int headerUnpack(const uint8_t in[HEADER_SIZE], StateHeader* h, uint32_t* thumbPacked, uint64_t* bodyPacked) {
    if (memcmp(in, MAGIC, 8) != 0 || get32(in + 8) != STATE_FORMAT || get32(in + 12) != HEADER_SIZE) {
        return 0;
    }
    memset(h, 0, sizeof(*h));
    memcpy(h->buildId, in + H_BUILD, 16);
    memcpy(h->region, in + H_REGION, 4);
    h->region[3] = 0;
    h->disc = get32(in + H_DISC);
    memcpy(h->serial, in + H_SERIAL, 16);
    h->serial[15] = 0;
    h->sectors = get32(in + H_SECTORS);
    h->vblank = get32(in + H_VBLANK);
    h->savedAt = (int64_t)get64(in + H_SAVED);
    h->playTime = get32(in + H_PLAY);
    h->mode = (int32_t)get32(in + H_MODE);
    h->place = (int32_t)get32(in + H_PLACE);
    memcpy(h->label, in + H_LABEL, sizeof(h->label));
    h->label[sizeof(h->label) - 1] = 0;
    h->flags = get32(in + H_FLAGS);
    h->dumpFrames = get64(in + H_DUMP);
    h->thumbW = get32(in + H_THUMBW);
    h->thumbH = get32(in + H_THUMBH);
    h->bodySize = get64(in + H_BODY);
    h->bodyHash = get64(in + H_HASH);
    *thumbPacked = get32(in + H_THUMBPACKED);
    h->engine = get32(in + H_ENGINE);
    *bodyPacked = get64(in + H_BODYPACKED);
    memcpy(h->appVersion, in + H_APPVERSION, sizeof(h->appVersion));
    h->appVersion[sizeof(h->appVersion) - 1] = 0;
    h->imageBias = get64(in + H_IMAGE);
    if (h->thumbW > 1024 || h->thumbH > 1024 || h->bodySize > (1ull << 31)) {
        return 0;
    }
    return 1;
}

/* ---- the trailer of a portable slot ----------------------------------------------- */

#define TRAILER_MAGIC "XGCANON"
#define TRAILER_VERSION 1u
#define TRAILER_HEAD 24 /* magic, version, 0, the size of what follows */

int statePortableString(StatePortable* p, uint64_t addr, const uint8_t* text, size_t len) {
    if (len > STATE_STRING_MAX) {
        return 0;
    }
    if (p->stringCount == p->stringCap) {
        uint32_t cap = p->stringCap ? p->stringCap * 2 : 64;
        StateString* n = realloc(p->strings, (size_t)cap * sizeof(*n));
        if (n == NULL) {
            return 0;
        }
        p->strings = n;
        p->stringCap = cap;
    }
    if (p->poolSize + len + 1 > p->poolCap) {
        size_t cap = p->poolCap ? p->poolCap : 4096;
        while (cap < p->poolSize + len + 1) {
            cap *= 2;
        }
        uint8_t* n = realloc(p->pool, cap);
        if (n == NULL) {
            return 0;
        }
        p->pool = n;
        p->poolCap = cap;
    }
    StateString* s = &p->strings[p->stringCount++];
    s->addr = addr;
    s->len = (uint32_t)len;
    s->off = (uint32_t)p->poolSize;
    if (len > 0) {
        memcpy(p->pool + p->poolSize, text, len);
    }
    p->pool[p->poolSize + len] = 0;
    p->poolSize += len + 1;
    return 1;
}

static int byAddress(const void* a, const void* b) {
    const StateString* x = a;
    const StateString* y = b;
    if (x->addr != y->addr) {
        return x->addr < y->addr ? -1 : 1;
    }
    return x->off < y->off ? -1 : x->off > y->off; /* the first added first: stable */
}

void statePortableSortStrings(StatePortable* p) {
    if (p->stringCount == 0) {
        return;
    }
    qsort(p->strings, p->stringCount, sizeof(*p->strings), byAddress);
    uint32_t n = 1;
    for (uint32_t i = 1; i < p->stringCount; i++) {
        if (p->strings[i].addr != p->strings[n - 1].addr) {
            p->strings[n++] = p->strings[i];
        }
    }
    p->stringCount = n;
}

int statePortableStringAt(void* user, uint64_t addr, const uint8_t** text, size_t* len) {
    const StatePortable* p = user;
    uint32_t lo = 0, hi = p->stringCount;
    while (lo < hi) {
        uint32_t mid = lo + (hi - lo) / 2;
        if (p->strings[mid].addr < addr) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
    if (lo == p->stringCount || p->strings[lo].addr != addr) {
        return 0;
    }
    *text = p->pool + p->strings[lo].off;
    *len = p->strings[lo].len;
    return 1;
}

void statePortableFree(StatePortable* p) {
    free(p->strings);
    free(p->pool);
    memset(p, 0, sizeof(*p));
}

/* The trailer's bytes, malloc'd (the table sorted). */
static uint8_t* trailerPack(const StatePortable* p, size_t* size) {
    size_t n = TRAILER_HEAD + 8 + 4 + 4 + 8;
    for (uint32_t i = 0; i < p->stringCount; i++) {
        n += 12 + p->strings[i].len;
    }
    uint8_t* out = malloc(n);
    if (out == NULL) {
        return NULL;
    }
    memcpy(out, TRAILER_MAGIC, 8);
    put32(out + 8, TRAILER_VERSION);
    put32(out + 12, 0);
    put64(out + 16, n - TRAILER_HEAD);
    put64(out + 24, p->schemaHash);
    put32(out + 32, (uint32_t)p->safePoint);
    put32(out + 36, p->stringCount);
    size_t at = 40;
    for (uint32_t i = 0; i < p->stringCount; i++) {
        put64(out + at, p->strings[i].addr);
        put32(out + at + 8, p->strings[i].len);
        memcpy(out + at + 12, p->pool + p->strings[i].off, p->strings[i].len);
        at += 12 + p->strings[i].len;
    }
    put64(out + at, stateHash(out, at, 0));
    *size = n;
    return out;
}

/* A trailer read back (n bytes from its magic to the file's end); 0 if damaged. */
static int trailerUnpack(const uint8_t* in, size_t n, StatePortable* p) {
    memset(p, 0, sizeof(*p));
    if (n < TRAILER_HEAD + 24 || memcmp(in, TRAILER_MAGIC, 8) != 0 || get32(in + 8) != TRAILER_VERSION ||
        get64(in + 16) != n - TRAILER_HEAD || get64(in + n - 8) != stateHash(in, n - 8, 0)) {
        return 0;
    }
    p->schemaHash = get64(in + 24);
    p->safePoint = (int32_t)get32(in + 32);
    uint32_t count = get32(in + 36);
    size_t at = 40;
    uint64_t last = 0;
    for (uint32_t i = 0; i < count; i++) {
        if (at + 12 > n - 8) {
            statePortableFree(p);
            return 0;
        }
        uint64_t addr = get64(in + at);
        uint32_t len = get32(in + at + 8);
        if (len > STATE_STRING_MAX || at + 12 + len > n - 8 || (i > 0 && addr <= last) ||
            !statePortableString(p, addr, in + at + 12, len)) {
            statePortableFree(p);
            return 0;
        }
        last = addr;
        at += 12 + len;
    }
    if (at != n - 8) {
        statePortableFree(p);
        return 0;
    }
    return 1;
}

/* ---- files ------------------------------------------------------------------------ */

int stateFileWrite(const char* path, const StateHeader* h, const uint8_t* thumb, const uint8_t* body, char* why,
                   size_t whySize) {
    return stateFileWritePortable(path, h, thumb, body, NULL, why, whySize);
}

int stateFileWritePortable(const char* path, const StateHeader* h, const uint8_t* thumb, const uint8_t* body,
                           const StatePortable* portable, char* why, size_t whySize) {
    size_t thumbRaw = thumb != NULL ? (size_t)h->thumbW * h->thumbH * 3 : 0;
    uint8_t* packedThumb = malloc(statePackBound(thumbRaw));
    uint8_t* packedBody = malloc(statePackBound((size_t)h->bodySize));
    size_t trailerSize = 0;
    uint8_t* trailer = portable != NULL ? trailerPack(portable, &trailerSize) : NULL;
    char tmp[1100];
    int ok = 0;

    snprintf(tmp, sizeof tmp, "%s.tmp", path);
    if (packedThumb != NULL && packedBody != NULL && (portable == NULL || trailer != NULL)) {
        size_t tp = thumbRaw ? statePack(thumb, thumbRaw, packedThumb) : 0;
        size_t bp = statePack(body, (size_t)h->bodySize, packedBody);
        StateHeader copy = *h;
        if (thumb == NULL) {
            copy.thumbW = copy.thumbH = 0;
        }
        copy.flags = portable != NULL ? copy.flags | STATE_FLAG_PORTABLE : copy.flags & ~STATE_FLAG_PORTABLE;
        uint8_t head[HEADER_SIZE];
        headerPack(&copy, (uint32_t)tp, bp, head);
        FILE* f = fopen(tmp, "wb");
        ok = f != NULL && fwrite(head, 1, HEADER_SIZE, f) == HEADER_SIZE && fwrite(packedThumb, 1, tp, f) == tp &&
             fwrite(packedBody, 1, bp, f) == bp &&
             (trailer == NULL || fwrite(trailer, 1, trailerSize, f) == trailerSize);
        if (f != NULL && fclose(f) != 0) {
            ok = 0;
        }
        if (!ok) {
            snprintf(why, whySize, "cannot write %s", tmp);
        } else if (!SDL_RenamePath(tmp, path)) {
            snprintf(why, whySize, "cannot replace %s: %s", path, SDL_GetError());
            ok = 0;
        }
        if (!ok) {
            SDL_RemovePath(tmp);
        }
    } else {
        snprintf(why, whySize, "out of memory");
    }
    free(packedThumb);
    free(packedBody);
    free(trailer);
    return ok;
}

static FILE* openHeader(const char* path, StateHeader* h, uint32_t* thumbPacked, uint64_t* bodyPacked) {
    uint8_t head[HEADER_SIZE];
    FILE* f = fopen(path, "rb");
    if (f == NULL) {
        return NULL;
    }
    if (fread(head, 1, HEADER_SIZE, f) != HEADER_SIZE || !headerUnpack(head, h, thumbPacked, bodyPacked)) {
        fclose(f);
        return NULL;
    }
    return f;
}

int stateFileReadHeader(const char* path, StateHeader* h, uint8_t** thumb) {
    uint32_t tp;
    uint64_t bp;
    FILE* f = openHeader(path, h, &tp, &bp);
    if (f == NULL) {
        return 0;
    }
    if (thumb != NULL) {
        size_t raw = (size_t)h->thumbW * h->thumbH * 3;
        uint8_t* packed = malloc(tp ? tp : 1);
        *thumb = raw ? malloc(raw) : NULL;
        if (*thumb != NULL && (packed == NULL || fread(packed, 1, tp, f) != tp || !stateUnpack(packed, tp, *thumb, raw))) {
            free(*thumb);
            *thumb = NULL;
        }
        free(packed);
    }
    fclose(f);
    return 1;
}

int stateFileRead(const char* path, StateHeader* h, uint8_t** body, char* why, size_t whySize) {
    uint32_t tp;
    uint64_t bp;
    FILE* f = openHeader(path, h, &tp, &bp);
    *body = NULL;
    if (f == NULL) {
        FILE* any = fopen(path, "rb");
        snprintf(why, whySize, any != NULL ? "it is no save state of this format" : "there is no such file");
        if (any != NULL) {
            fclose(any);
        }
        return 0;
    }
    uint8_t* packed = malloc(bp ? (size_t)bp : 1);
    uint8_t* raw = malloc(h->bodySize ? (size_t)h->bodySize : 1);
    int ok = packed != NULL && raw != NULL && fseek(f, (long)(HEADER_SIZE + tp), SEEK_SET) == 0 &&
             fread(packed, 1, (size_t)bp, f) == bp;
    fclose(f);
    if (!ok) {
        snprintf(why, whySize, "the file is cut short");
    } else if (!stateUnpack(packed, (size_t)bp, raw, (size_t)h->bodySize) ||
               stateHash(raw, (size_t)h->bodySize, 0) != h->bodyHash) {
        snprintf(why, whySize, "the file is damaged");
        ok = 0;
    }
    free(packed);
    if (!ok) {
        free(raw);
        return 0;
    }
    *body = raw;
    return 1;
}

int stateFileReadPortable(const char* path, StatePortable* out, char* why, size_t whySize) {
    StateHeader h;
    uint32_t tp;
    uint64_t bp;
    memset(out, 0, sizeof(*out));
    FILE* f = openHeader(path, &h, &tp, &bp);
    if (f == NULL) {
        snprintf(why, whySize, "it is no save state of this format");
        return 0;
    }
    if (!(h.flags & STATE_FLAG_PORTABLE)) {
        fclose(f);
        snprintf(why, whySize, "it has no trailer");
        return 0;
    }
    long at = (long)(HEADER_SIZE + tp + bp);
    long end = fseek(f, 0, SEEK_END) == 0 ? ftell(f) : -1;
    uint8_t* data = end > at && end - at < (1l << 26) ? malloc((size_t)(end - at)) : NULL;
    size_t n = end > at ? (size_t)(end - at) : 0;
    int ok = data != NULL && fseek(f, at, SEEK_SET) == 0 && fread(data, 1, n, f) == n && trailerUnpack(data, n, out);
    fclose(f);
    free(data);
    if (!ok) {
        snprintf(why, whySize, "its trailer is damaged");
    }
    return ok;
}

/* ---- slots ------------------------------------------------------------------------ */

#ifdef VERSION_JP
#define REGION "JP"
#else
#define REGION "US"
#endif

const char* stateDir(void) {
    static char dir[1100];
    static int known;
    if (!known) {
        known = 1;
        const char* env = getenv("XG_STATE_DIR");
        const char* settings = settingsPath();
        if (env != NULL && env[0] != '\0') {
            snprintf(dir, sizeof dir, "%s", env);
        } else if (settings != NULL && settings[0] != '\0') {
            snprintf(dir, sizeof dir, "%s", settings);
            char* slash = strrchr(dir, '/');
#ifdef _WIN32
            char* back = strrchr(dir, '\\');
            if (back != NULL && (slash == NULL || back > slash)) {
                slash = back;
            }
#endif
            size_t at = slash != NULL ? (size_t)(slash - dir + 1) : 0;
            snprintf(dir + at, sizeof dir - at, "states/%s", REGION);
        } else {
            dir[0] = 0;
        }
        if (dir[0] != 0 && !SDL_CreateDirectory(dir)) {
            fprintf(stderr, "state: cannot make %s: %s\n", dir, SDL_GetError());
        }
    }
    return dir[0] != 0 ? dir : NULL;
}

int stateSlotPath(int slot, char* out, size_t size) {
    const char* dir = stateDir();
    if (dir == NULL) {
        return 0;
    }
    if (slot == 0) {
        snprintf(out, size, "%s/undo.xgs", dir);
    } else {
        snprintf(out, size, "%s/slot%d.xgs", dir, slot);
    }
    return 1;
}

/* ---- the schemas of portable slots ------------------------------------------------ */

#define SCHEMA_MAGIC "XGSCHEMA"

int stateSchemaFilePath(uint64_t hash, char* out, size_t size) {
    const char* dir = stateDir();
    if (dir == NULL) {
        return 0;
    }
    snprintf(out, size, "%s/schemas/%016llx.xgschema", dir, (unsigned long long)hash);
    return 1;
}

int stateSchemaFileWrite(const char* path, const uint8_t* json, size_t n, char* why, size_t whySize) {
    char dir[1100], tmp[1100];
    snprintf(dir, sizeof dir, "%s", path);
    char* slash = strrchr(dir, '/');
    if (slash != NULL) {
        *slash = 0;
        SDL_CreateDirectory(dir); /* (fails when it is there) */
    }
    uint8_t* packed = malloc(16 + statePackBound(n));
    if (packed == NULL) {
        snprintf(why, whySize, "out of memory");
        return 0;
    }
    memcpy(packed, SCHEMA_MAGIC, 8);
    put64(packed + 8, n);
    size_t size = 16 + statePack(json, n, packed + 16);
    snprintf(tmp, sizeof tmp, "%s.tmp", path);
    FILE* f = fopen(tmp, "wb");
    int ok = f != NULL && fwrite(packed, 1, size, f) == size;
    if (f != NULL && fclose(f) != 0) {
        ok = 0;
    }
    free(packed);
    if (!ok) {
        snprintf(why, whySize, "cannot write %s", tmp);
    } else if (!SDL_RenamePath(tmp, path)) {
        snprintf(why, whySize, "cannot replace %s: %s", path, SDL_GetError());
        ok = 0;
    }
    if (!ok) {
        SDL_RemovePath(tmp);
    }
    return ok;
}

int stateSchemaFileRead(const char* path, uint8_t** json, size_t* n, char* why, size_t whySize) {
    *json = NULL;
    *n = 0;
    FILE* f = fopen(path, "rb");
    if (f == NULL) {
        snprintf(why, whySize, "there is no such file");
        return 0;
    }
    long size = fseek(f, 0, SEEK_END) == 0 ? ftell(f) : -1;
    uint8_t* data = size >= 16 && size < (1l << 30) ? malloc((size_t)size) : NULL;
    int ok = data != NULL && fseek(f, 0, SEEK_SET) == 0 && fread(data, 1, (size_t)size, f) == (size_t)size &&
             memcmp(data, SCHEMA_MAGIC, 8) == 0;
    fclose(f);
    uint64_t raw = ok ? get64(data + 8) : 0;
    uint8_t* text = ok && raw < (1ull << 31) ? malloc((size_t)raw + 1) : NULL;
    ok = text != NULL && stateUnpack(data + 16, (size_t)size - 16, text, (size_t)raw);
    free(data);
    if (!ok) {
        free(text);
        snprintf(why, whySize, "it is damaged");
        return 0;
    }
    text[raw] = 0;
    *json = text;
    *n = (size_t)raw;
    return 1;
}
