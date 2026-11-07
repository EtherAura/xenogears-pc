#include "setup.h"
#include "verify.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int cdSetupPath(const char* path, char* out, size_t size) {
    if (!*path) {
        if (size)
            out[0] = 0;
        return size != 0;
    }
    int absolute = path[0] == '/';
#ifdef _WIN32
    absolute = (path[0] == '\\' && path[1] == '\\') ||
               (strlen(path) > 2 && path[1] == ':' && (path[2] == '/' || path[2] == '\\'));
    if (strlen(path) > 1 && path[1] == ':' && !absolute)
        return 0;
#endif
    char* cwd = absolute ? NULL : SDL_GetCurrentDirectory();
    if (!absolute && !cwd)
        return 0;
    int n;
#ifdef _WIN32
    if (!absolute && (path[0] == '/' || path[0] == '\\'))
        n = snprintf(out, size, "%.2s%s", cwd, path);
    else
#endif
        n = snprintf(out, size, "%s%s", cwd ? cwd : "", path);
    SDL_free(cwd);
    return n >= 0 && (size_t)n < size;
}

int cdSetupInteractive(void) {
    const char* script = SDL_getenv("XG_UI_SCRIPT");
    return SDL_getenv("XG_HEADLESS") == NULL || (script != NULL && script[0] != '\0');
}

/* ---- discovery ---- */

static int hasExtension(const char* name, const char* ext) {
    size_t n = strlen(name), e = strlen(ext);
    return n > e && name[n - e - 1] == '.' && SDL_strcasecmp(name + n - e, ext) == 0;
}

static int isDirectory(const char* path) {
    SDL_PathInfo info;
    return SDL_GetPathInfo(path, &info) && info.type == SDL_PATHTYPE_DIRECTORY;
}

static int isFile(const char* path, Sint64* size) {
    SDL_PathInfo info;
    if (!SDL_GetPathInfo(path, &info) || info.type != SDL_PATHTYPE_FILE) return 0;
    if (size) *size = (Sint64)info.size;
    return 1;
}

/* `dir` and a separator (if it has none at its end). */
static void joinPath(char* out, size_t size, const char* dir, const char* name) {
    size_t n = strlen(dir);
    int sep = n > 0 && (dir[n - 1] == '/' || dir[n - 1] == '\\');
    snprintf(out, size, "%s%s%s", dir, sep || n == 0 ? "" : "/", name);
}

typedef struct {
    char names[256][256];
    int count;
} Listing;

static SDL_EnumerationResult SDLCALL listOne(void* user, const char* dirname, const char* fname) {
    (void)dirname;
    Listing* l = user;
    if (fname[0] != '.' && l->count < 256 && strlen(fname) < 256) snprintf(l->names[l->count++], 256, "%s", fname);
    return SDL_ENUM_CONTINUE;
}

static int byName(const void* a, const void* b) {
    return SDL_strcasecmp((const char*)a, (const char*)b);
}

static Listing* list(const char* dir) {
    Listing* l = calloc(1, sizeof *l);
    if (l == NULL) return NULL;
    if (!SDL_EnumerateDirectory(dir, listOne, l)) l->count = 0;
    qsort(l->names, (size_t)l->count, sizeof l->names[0], byName);
    return l;
}

/* The images of one folder (no deeper). */
static int imagesIn(const char* dir, CdSetupImage* out, int count, int max) {
    Listing* l = list(dir);
    if (l == NULL) return count;
    int cues = 0;
    for (int i = 0; i < l->count; i++) cues += hasExtension(l->names[i], "cue");
    for (int i = 0; i < l->count && count < max; i++) {
        if (!hasExtension(l->names[i], cues ? "cue" : "bin")) continue;
        char path[4096], why[256];
        joinPath(path, sizeof path, dir, l->names[i]);
        if (!isFile(path, NULL) || !cdVerifyImage(path, &out[count].info, why, sizeof why)) continue;
        snprintf(out[count].path, sizeof out[count].path, "%s", path);
        count++;
    }
    free(l);
    return count;
}

static int byRelease(const void* a, const void* b) {
    const CdSetupImage *x = a, *y = b;
    int r = strcmp(x->info.region, y->info.region);
    return r != 0 ? -r : x->info.disc != y->info.disc ? x->info.disc - y->info.disc : strcmp(x->path, y->path);
}

int cdSetupFindImages(const char* dir, CdSetupImage* out, int max) {
    if (dir == NULL || !isDirectory(dir)) return 0;
    int count = imagesIn(dir, out, 0, max);
    Listing* l = list(dir);
    for (int i = 0; l != NULL && i < l->count && count < max; i++) {
        char sub[4096];
        joinPath(sub, sizeof sub, dir, l->names[i]);
        if (isDirectory(sub)) count = imagesIn(sub, out, count, max);
    }
    free(l);
    qsort(out, (size_t)count, sizeof *out, byRelease); /* US first, then JP; Disc 1 first */
    return count;
}

int cdSetupPick(const CdSetupImage* found, int count, const char* region, int* first, int* other) {
    *first = *other = -1;
    for (int want = 1; want <= 2 && *first < 0; want++)
        for (int i = 0; i < count && *first < 0; i++)
            if (found[i].info.disc == want && (region == NULL || !strcmp(found[i].info.region, region))) *first = i;
    if (*first < 0) return 0;
    for (int i = 0; i < count; i++)
        if (!strcmp(found[i].info.region, found[*first].info.region) && found[i].info.disc != found[*first].info.disc) {
            *other = i;
            break;
        }
    return 1;
}

/* `path` cut to its folder (`up` folders more), "" if it has none. */
static void folderOf(const char* path, int up, char* out, size_t size) {
    snprintf(out, size, "%s", path);
    for (int i = 0; i <= up; i++) {
        char* sep = NULL;
        for (char* p = out; *p; p++)
            if (*p == '/' || *p == '\\') sep = p;
        if (sep == NULL) {
            out[0] = 0;
            return;
        }
        *sep = 0;
        if (sep == out) { /* the root */
            snprintf(out, size, "/");
            return;
        }
    }
}

int cdSetupDiscover(const char* region, const char* const* near, int count, char out[4096]) {
    enum { MAX = 16 };
    CdSetupImage* found = calloc(MAX, sizeof *found);
    if (found == NULL) return 0;
    int ok = 0;
    for (int k = 0; k < count + 2 && !ok; k++) {
        char dir[4096];
        for (int up = 0; up < 2 && !ok; up++) {
            if (k < count) {
                if (near[k] == NULL || near[k][0] == 0) break;
                folderOf(near[k], up, dir, sizeof dir);
            } else if (k == count) {
                const char* base = SDL_GetBasePath();
                if (base == NULL || up > 0) break;
                snprintf(dir, sizeof dir, "%s", base);
            } else {
                char* cwd = SDL_GetCurrentDirectory();
                snprintf(dir, sizeof dir, "%s", cwd ? cwd : "");
                SDL_free(cwd);
                if (up > 0) break;
            }
            /* Not a whole drive or the root: only folders near the images. */
            size_t len = strlen(dir);
            if (dir[0] == 0 || (len == 1 && (dir[0] == '/' || dir[0] == '\\')) ||
                (len <= 3 && len >= 2 && dir[1] == ':'))
                continue;
            int n = cdSetupFindImages(dir, found, MAX), first, other;
            if (cdSetupPick(found, n, region, &first, &other)) {
                snprintf(out, 4096, "%s", found[first].path);
                ok = 1;
            }
        }
    }
    free(found);
    return ok;
}

/* port/src/system/krom.c's names, in its order for the build's version. */
static const char* const biosNames[] = {
#ifdef VERSION_JP
    "scph5500.bin", "scph7000.bin", "scph3500.bin", "scph3000.bin", "scph1000.bin",
    "scph5501.bin", "scph7001.bin", "scph1001.bin",
#else
    "scph5501.bin", "scph7001.bin", "scph1001.bin",
    "scph5500.bin", "scph7000.bin", "scph3500.bin", "scph3000.bin", "scph1000.bin",
#endif
    "scph5502.bin", "scph7502.bin", "scph1002.bin", "bios.bin",
};

int cdSetupIsBios(const char* path) {
    Sint64 size = 0;
    return path != NULL && path[0] != 0 && isFile(path, &size) && size == 0x80000;
}

int cdSetupFindBios(const char* dir, char* out, size_t size) {
    if (cdSetupIsBios(dir)) {
        snprintf(out, size, "%s", dir);
        return 1;
    }
    if (dir == NULL || !isDirectory(dir)) return 0;
    Listing* l = list(dir);
    int ok = 0;
    /* The names case-blind (a folder copied from another system). */
    for (size_t n = 0; l != NULL && n < sizeof biosNames / sizeof biosNames[0] && !ok; n++)
        for (int i = 0; i < l->count && !ok; i++) {
            char path[4096];
            joinPath(path, sizeof path, dir, l->names[i]);
            if (SDL_strcasecmp(l->names[i], biosNames[n]) == 0 && cdSetupIsBios(path)) {
                snprintf(out, size, "%s", path);
                ok = 1;
            }
        }
    free(l);
    return ok;
}

/* ---- the first check ---- */

int cdSetupFirst(SDL_Window* window, const char* path, const char* region, char out[4096]) {
    (void)window;
    CdImageInfo info[2];
    char why[1024];
    if (!cdSetupPath(path, out, 4096)) {
        fprintf(stderr, "disc setup: cannot resolve the image path\n");
        if (!cdSetupInteractive()) return 0;
        out[0] = 0;
    }
    if (out[0] && cdVerifyPair(out, NULL, region, info, why, sizeof why)) {
        fprintf(stderr, "disc setup: %s Disc %d (%s), %d file entries, %d sectors\n", info[0].region, info[0].disc,
                info[0].boot, info[0].entries, info[0].sectors);
        return CD_SETUP_OK;
    }
    fprintf(stderr, "disc setup: %s\n", out[0] ? why : "no image");
    if (!cdSetupInteractive()) return 0;
    /* Where it was, then the usual places: the setup screen starts from what
     * is found (the game's look needs an image: it is read from it). */
    const char* near[] = {out, path};
    char found[4096];
    if (cdSetupDiscover(region, near, 2, found)) {
        fprintf(stderr, "disc setup: found %s: the setup screen starts from it\n", found);
        snprintf(out, 4096, "%s", found);
        return CD_SETUP_FOUND;
    }
    fprintf(stderr, "disc setup: no valid image found: the setup screen without one\n");
    out[0] = 0;
    return CD_SETUP_NONE;
}
int cdVerifyCommand(int argc, char** argv, const char* region) {
    if (argc < 1 || argc > 2) {
        fprintf(stderr, "Usage: xenogears --verify-discs FIRST.cue [OTHER.cue]\n");
        return 2;
    }
    CdImageInfo info[2];
    char why[1024];
    if (!cdVerifyPair(argv[0], argc == 2 ? argv[1] : NULL, region, info, why, sizeof why)) {
        fprintf(stderr, "disc verification: %s\n", why);
        return 1;
    }
    for (int i = 0; i < argc; i++)
        printf("%s: %s Disc %d, %s, %d sectors, %d file entries; identity and file-table checks passed\n", argv[i],
               info[i].region, info[i].disc, info[i].boot, info[i].sectors, info[i].entries);
    return 0;
}
