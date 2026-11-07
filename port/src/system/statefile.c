/*
 * Save state files (statefile.h).
 *
 * The build id: a state holds return addresses, pointers to statics and the
 * stack, so it loads only in an executable whose code and data sit where they
 * sat. The id hashes the code itself and every allocated section's name,
 * address and size (identical code at the same address reaches every object
 * at the same address). A rebuild that changes neither keeps its states; any
 * other refuses them, with a message.
 */

#ifndef _WIN32
#define _GNU_SOURCE 1 /* dl_iterate_phdr (link.h) */
#endif
#include "statefile.h"

#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

static void idMix(uint64_t* a, uint64_t* b, const void* data, size_t n) {
    *a = stateHash(data, n, *a);
    *b = stateHash(data, n, *b ^ 0x5851F42D4C957F2Dull);
}

#ifdef _WIN32
static int computeId(uint64_t* a, uint64_t* b) {
    const unsigned char* base = (const unsigned char*)GetModuleHandleW(NULL);
    const IMAGE_DOS_HEADER* dos = (const IMAGE_DOS_HEADER*)base;
    const IMAGE_NT_HEADERS* nt = (const IMAGE_NT_HEADERS*)(base + dos->e_lfanew);
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

/* The bias, before main (or at the library's load): states record it while
 * the game is parked, on the game's stacks, where a call into the C library
 * would leave this run's host values below the game's frames. */
__attribute__((constructor)) static void imageBiasInit(void) {
    SelfObject self = {.inside = (uintptr_t)&imageBiasInit};
    dl_iterate_phdr(findSelf, &self);
    imageBias = self.found ? self.bias : 0;
}

static int computeId(uint64_t* a, uint64_t* b) {
    SelfObject self = {.inside = (uintptr_t)&computeId};
    dl_iterate_phdr(findSelf, &self);
    if (!self.found) {
        return 0;
    }
    /* Position-independent code at another address holds other addresses: a
     * non-zero load bias is part of the id (as the image base is on Windows). */
    if (self.bias != 0) {
        uint64_t bias = self.bias;
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
            /* The code as loaded. */
            idMix(a, b, (const void*)(uintptr_t)(self.bias + sh[i].sh_addr), sh[i].sh_size);
        }
    }
    free(names);
    free(sh);
    fclose(f);
    return ok;
}
#endif

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
    H_IMAGE = 232
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
    if (h->thumbW > 1024 || h->thumbH > 1024 || h->bodySize > (1ull << 31)) {
        return 0;
    }
    return 1;
}

/* ---- files ------------------------------------------------------------------------ */

int stateFileWrite(const char* path, const StateHeader* h, const uint8_t* thumb, const uint8_t* body, char* why,
                   size_t whySize) {
    size_t thumbRaw = thumb != NULL ? (size_t)h->thumbW * h->thumbH * 3 : 0;
    uint8_t* packedThumb = malloc(statePackBound(thumbRaw));
    uint8_t* packedBody = malloc(statePackBound((size_t)h->bodySize));
    char tmp[1100];
    int ok = 0;

    snprintf(tmp, sizeof tmp, "%s.tmp", path);
    if (packedThumb != NULL && packedBody != NULL) {
        size_t tp = thumbRaw ? statePack(thumb, thumbRaw, packedThumb) : 0;
        size_t bp = statePack(body, (size_t)h->bodySize, packedBody);
        StateHeader copy = *h;
        if (thumb == NULL) {
            copy.thumbW = copy.thumbH = 0;
        }
        uint8_t head[HEADER_SIZE];
        headerPack(&copy, (uint32_t)tp, bp, head);
        FILE* f = fopen(tmp, "wb");
        ok = f != NULL && fwrite(head, 1, HEADER_SIZE, f) == HEADER_SIZE && fwrite(packedThumb, 1, tp, f) == tp &&
             fwrite(packedBody, 1, bp, f) == bp;
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
