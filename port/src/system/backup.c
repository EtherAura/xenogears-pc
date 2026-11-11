/*
 * Saves backup (backup.h; xg-mrj.3.7). The archive is a plain .zip (stored
 * entries, no compression: card images and states are small, and any unzip
 * tool opens it), written from the format's published description (PKWARE's
 * APPNOTE.TXT: local headers, the central directory, its end record).
 * Restoring reads every entry and checks its CRC before it writes any file,
 * so a damaged or foreign archive changes nothing; names that would leave
 * the save folder (absolute, "..") are refused.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "backup.h"

#define MAX_ENTRY (1024u * 1024u * 1024u) /* 1 GB: far above any save file */
#define MAX_FILES 4096

/* ---- the save folder's files --------------------------------------------------------- */

typedef struct {
    char* names[MAX_FILES];
    int count;
    const char* root;
    size_t rootLen;
} FileList;

static SDL_EnumerationResult SDLCALL listOne(void* user, const char* dirname, const char* fname) {
    FileList* l = user;
    char path[4096];
    snprintf(path, sizeof path, "%s%s", dirname, fname);
    SDL_PathInfo info;
    if (!SDL_GetPathInfo(path, &info)) return SDL_ENUM_CONTINUE;
    if (info.type == SDL_PATHTYPE_DIRECTORY) {
        size_t n = strlen(path);
        if (n + 1 < sizeof path) {
            path[n] = '/';
            path[n + 1] = '\0';
            SDL_EnumerateDirectory(path, listOne, l);
        }
    } else if (info.type == SDL_PATHTYPE_FILE && l->count < MAX_FILES && strlen(path) > l->rootLen) {
        l->names[l->count++] = SDL_strdup(path + l->rootLen);
    }
    return SDL_ENUM_CONTINUE;
}

static int byName(const void* a, const void* b) {
    return strcmp(*(char* const*)a, *(char* const*)b);
}

/* ---- little-endian fields ----------------------------------------------------------- */

static void put16(Uint8* p, unsigned v) {
    p[0] = (Uint8)v;
    p[1] = (Uint8)(v >> 8);
}
static void put32(Uint8* p, Uint32 v) {
    put16(p, v & 0xFFFF);
    put16(p + 2, v >> 16);
}
static unsigned get16(const Uint8* p) {
    return p[0] | p[1] << 8;
}
static Uint32 get32(const Uint8* p) {
    return get16(p) | (Uint32)get16(p + 2) << 16;
}

static int fail(char* why, size_t size, const char* fmt, const char* what) {
    snprintf(why, size, fmt, what != NULL ? what : "");
    return 0;
}

/* ---- writing ----------------------------------------------------------------------- */

typedef struct {
    Uint32 crc, size, offset;
    unsigned time, date;
} Written;

int backupWrite(const char* dir, const char* zip, int* files, char* why, size_t size) {
    FileList* l = calloc(1, sizeof *l);
    Written* w = calloc(MAX_FILES, sizeof *w);
    if (l == NULL || w == NULL) {
        free(l);
        free(w);
        return fail(why, size, "out of memory", NULL);
    }
    char root[4096];
    snprintf(root, sizeof root, "%s%s", dir, dir[0] != '\0' && dir[strlen(dir) - 1] != '/' ? "/" : "");
    l->root = root;
    l->rootLen = strlen(root);
    SDL_EnumerateDirectory(root, listOne, l);
    qsort(l->names, (size_t)l->count, sizeof l->names[0], byName);
    SDL_IOStream* out = SDL_IOFromFile(zip, "wb");
    int ok = out != NULL;
    if (!ok) fail(why, size, "cannot write the backup: %s", SDL_GetError());
    Uint64 at = 0;
    int written = 0;
    for (int i = 0; ok && i < l->count; i++) {
        char path[4096];
        snprintf(path, sizeof path, "%s%s", root, l->names[i]);
        size_t n = 0;
        void* data = SDL_LoadFile(path, &n);
        if (data == NULL || n > MAX_ENTRY) {
            SDL_free(data);
            continue; /* gone meanwhile, or not a save */
        }
        SDL_PathInfo info;
        SDL_DateTime t;
        unsigned time = 0, date = 0x21; /* 1980-01-01 */
        if (SDL_GetPathInfo(path, &info) && SDL_TimeToDateTime(info.modify_time, &t, true) && t.year >= 1980) {
            time = (unsigned)(t.hour << 11 | t.minute << 5 | t.second / 2);
            date = (unsigned)((t.year - 1980) << 9 | t.month << 5 | t.day);
        }
        size_t nameLen = strlen(l->names[i]);
        Uint8 h[30];
        Written* e = &w[written];
        e->crc = SDL_crc32(0, data, n);
        e->size = (Uint32)n;
        e->offset = (Uint32)at;
        e->time = time;
        e->date = date;
        put32(h, 0x04034b50);
        put16(h + 4, 20);      /* version needed: 2.0 */
        put16(h + 6, 0x0800);  /* names in UTF-8 */
        put16(h + 8, 0);       /* stored */
        put16(h + 10, time);
        put16(h + 12, date);
        put32(h + 14, e->crc);
        put32(h + 18, e->size);
        put32(h + 22, e->size);
        put16(h + 26, (unsigned)nameLen);
        put16(h + 28, 0);
        ok = SDL_WriteIO(out, h, sizeof h) == sizeof h && SDL_WriteIO(out, l->names[i], nameLen) == nameLen &&
             SDL_WriteIO(out, data, n) == n;
        SDL_free(data);
        at += sizeof h + nameLen + n;
        if (at >= 0xFFFFFFFFull) {
            ok = fail(why, size, "the save folder is too large for one backup (4 GB)", NULL);
        } else if (!ok) {
            fail(why, size, "cannot write the backup: %s", SDL_GetError());
        } else {
            l->names[written] = l->names[i];
            if (written != i) l->names[i] = NULL;
            written++;
        }
    }
    Uint64 directory = at;
    for (int i = 0; ok && i < written; i++) {
        size_t nameLen = strlen(l->names[i]);
        Uint8 h[46];
        memset(h, 0, sizeof h);
        put32(h, 0x02014b50);
        put16(h + 4, 0x031E);  /* made by: Unix, 3.0 */
        put16(h + 6, 20);
        put16(h + 8, 0x0800);
        put16(h + 12, w[i].time);
        put16(h + 14, w[i].date);
        put32(h + 16, w[i].crc);
        put32(h + 20, w[i].size);
        put32(h + 24, w[i].size);
        put16(h + 28, (unsigned)nameLen);
        put32(h + 38, 0100644u << 16); /* rw-r--r-- */
        put32(h + 42, w[i].offset);
        ok = SDL_WriteIO(out, h, sizeof h) == sizeof h && SDL_WriteIO(out, l->names[i], nameLen) == nameLen;
        at += sizeof h + nameLen;
    }
    if (ok) {
        Uint8 end[22];
        memset(end, 0, sizeof end);
        put32(end, 0x06054b50);
        put16(end + 8, (unsigned)written);
        put16(end + 10, (unsigned)written);
        put32(end + 12, (Uint32)(at - directory));
        put32(end + 16, (Uint32)directory);
        ok = SDL_WriteIO(out, end, sizeof end) == sizeof end;
        if (!ok) fail(why, size, "cannot write the backup: %s", SDL_GetError());
    }
    if (out != NULL && !SDL_CloseIO(out) && ok) ok = fail(why, size, "cannot write the backup: %s", SDL_GetError());
    for (int i = 0; i < l->count; i++) SDL_free(l->names[i]);
    free(l);
    free(w);
    if (ok && files != NULL) *files = written;
    return ok;
}

/* ---- reading ----------------------------------------------------------------------- */

/* A name that stays inside the save folder. */
static int safeName(const char* name) {
    if (name[0] == '\0' || name[0] == '/' || strchr(name, '\\') != NULL || strchr(name, ':') != NULL) return 0;
    for (const char* p = name; *p != '\0';) {
        const char* slash = strchr(p, '/');
        size_t n = slash != NULL ? (size_t)(slash - p) : strlen(p);
        if (n == 0 || (n == 2 && p[0] == '.' && p[1] == '.') || (n == 1 && p[0] == '.')) return 0;
        p += n + (slash != NULL);
    }
    return name[strlen(name) - 1] != '/';
}

/* Entry i's data (a malloc'd copy), checked against its CRC. */
static Uint8* entryData(SDL_IOStream* in, const Uint8* central, Uint32* n, char* why, size_t size) {
    Uint8 h[30];
    Uint32 offset = get32(central + 42);
    *n = get32(central + 24);
    if (get16(central + 10) != 0 || get32(central + 20) != *n) {
        fail(why, size, "the backup holds a compressed entry (not one of this game's backups)", NULL);
        return NULL;
    }
    if (*n > MAX_ENTRY || SDL_SeekIO(in, offset, SDL_IO_SEEK_SET) < 0 || SDL_ReadIO(in, h, sizeof h) != sizeof h ||
        get32(h) != 0x04034b50 || SDL_SeekIO(in, get16(h + 26) + get16(h + 28), SDL_IO_SEEK_CUR) < 0) {
        fail(why, size, "the backup is damaged (a file's header)", NULL);
        return NULL;
    }
    Uint8* data = malloc(*n ? *n : 1);
    if (data == NULL || SDL_ReadIO(in, data, *n) != *n || SDL_crc32(0, data, *n) != get32(central + 16)) {
        free(data);
        fail(why, size, "the backup is damaged (a file's contents)", NULL);
        return NULL;
    }
    return data;
}

int backupRead(const char* zip, const char* dir, int* files, char* why, size_t size) {
    SDL_IOStream* in = SDL_IOFromFile(zip, "rb");
    if (in == NULL) return fail(why, size, "cannot read the backup: %s", SDL_GetError());
    Sint64 total = SDL_GetIOSize(in);
    Uint8 tail[22 + 65535];
    Sint64 tailLen = total < (Sint64)sizeof tail ? total : (Sint64)sizeof tail;
    const Uint8* end = NULL;
    if (tailLen >= 22 && SDL_SeekIO(in, total - tailLen, SDL_IO_SEEK_SET) >= 0 &&
        SDL_ReadIO(in, tail, (size_t)tailLen) == (size_t)tailLen) {
        for (Sint64 i = tailLen - 22; i >= 0 && end == NULL; i--) {
            if (get32(tail + i) == 0x06054b50) end = tail + i;
        }
    }
    if (end == NULL) {
        SDL_CloseIO(in);
        return fail(why, size, "not a .zip backup", NULL);
    }
    unsigned count = get16(end + 10);
    Uint32 dirSize = get32(end + 12), dirAt = get32(end + 16);
    Uint8* central = dirSize <= 64u * 1024 * 1024 ? malloc(dirSize ? dirSize : 1) : NULL;
    int ok = central != NULL && (Sint64)dirAt + dirSize <= total && SDL_SeekIO(in, dirAt, SDL_IO_SEEK_SET) >= 0 &&
             SDL_ReadIO(in, central, dirSize) == dirSize;
    if (!ok) fail(why, size, "the backup is damaged (its directory)", NULL);
    /* Every entry first: its name, its header, its CRC. */
    int settings = 0;
    for (int pass = 0; ok && pass < 2; pass++) {
        Uint32 at = 0;
        for (unsigned i = 0; ok && i < count; i++) {
            if (at + 46 > dirSize || get32(central + at) != 0x02014b50) {
                ok = fail(why, size, "the backup is damaged (its directory)", NULL);
                break;
            }
            const Uint8* c = central + at;
            unsigned nameLen = get16(c + 28);
            Uint32 next = at + 46 + nameLen + get16(c + 30) + get16(c + 32);
            if (next > dirSize) {
                ok = fail(why, size, "the backup is damaged (its directory)", NULL);
                break;
            }
            char name[1024];
            snprintf(name, sizeof name, "%.*s", (int)nameLen, (const char*)c + 46);
            at = next;
            if (nameLen > 0 && name[nameLen - 1] == '/') continue; /* a folder's own entry */
            if (strlen(name) != nameLen || !safeName(name)) {
                ok = fail(why, size, "the backup names a file outside the save folder: %s", name);
                break;
            }
            settings |= strcmp(name, "settings.ini") == 0;
            Uint32 n;
            Uint8* data = entryData(in, c, &n, why, size);
            if (data == NULL) {
                ok = 0;
                break;
            }
            if (pass == 1) {
                char path[4096], temp[4100];
                snprintf(path, sizeof path, "%s/%s", dir, name);
                snprintf(temp, sizeof temp, "%s.new", path);
                char* slash = strrchr(path, '/');
                *slash = '\0';
                SDL_CreateDirectory(path);
                *slash = '/';
                SDL_IOStream* out = SDL_IOFromFile(temp, "wb");
                ok = out != NULL && SDL_WriteIO(out, data, n) == n;
                ok = out != NULL && SDL_CloseIO(out) && ok && SDL_RenamePath(temp, path);
                if (!ok) fail(why, size, "cannot write %s", path);
                else if (files != NULL) (*files)++;
            }
            free(data);
        }
        if (ok && pass == 0 && !settings) ok = fail(why, size, "not a backup of this game's saves (no settings.ini)", NULL);
        if (pass == 0 && files != NULL) *files = 0;
    }
    free(central);
    SDL_CloseIO(in);
    return ok;
}

/* ---- the menu's actions ------------------------------------------------------------ */

static SDL_SpinLock lock;
static char status[1200];
static int statusError, running;
static int exporting;

static void setStatus(int error, const char* text) {
    SDL_LockSpinlock(&lock);
    snprintf(status, sizeof status, "%s", text);
    statusError = error;
    running = 0;
    SDL_UnlockSpinlock(&lock);
    fprintf(stderr, "backup: %s\n", text);
}

static int SDLCALL work(void* arg) {
    char* zip = arg;
    char* pref = SDL_GetPrefPath(NULL, "xenogears");
    char why[1100], text[1200];
    int files = 0, ok = 0;
    if (pref == NULL) {
        snprintf(text, sizeof text, "No save folder: %s", SDL_GetError());
    } else if (exporting) {
        ok = backupWrite(pref, zip, &files, why, sizeof why);
        if (ok) snprintf(text, sizeof text, "Backed up %d files", files);
        else snprintf(text, sizeof text, "Backup failed: %s", why);
    } else {
        ok = backupRead(zip, pref, &files, why, sizeof why);
        if (ok) snprintf(text, sizeof text, "Restored %d files: quit and start the game again to use them", files);
        else snprintf(text, sizeof text, "Nothing restored: %s", why);
    }
    setStatus(!ok, text);
    SDL_free(pref);
    SDL_free(zip);
    return 0;
}

static void SDLCALL chosen(void* user, const char* const* list, int filter) {
    (void)user;
    (void)filter;
    char text[1200];
    if (list == NULL || list[0] == NULL) {
        if (list == NULL) snprintf(text, sizeof text, "No file dialog: %s", SDL_GetError());
        else text[0] = '\0'; /* cancelled */
        setStatus(list == NULL, text);
        return;
    }
    SDL_Thread* t = SDL_CreateThread(work, "backup", SDL_strdup(list[0]));
    if (t == NULL) {
        snprintf(text, sizeof text, "No backup thread: %s", SDL_GetError());
        setStatus(1, text);
    } else {
        SDL_DetachThread(t);
    }
}

static const SDL_DialogFileFilter zipFilter[] = {{"Zip archive", "zip"}};

static int start(int write) {
    SDL_LockSpinlock(&lock);
    int busy = running;
    if (!busy) {
        running = 1;
        exporting = write;
        status[0] = '\0';
    }
    SDL_UnlockSpinlock(&lock);
    return !busy;
}

void backupExport(SDL_Window* window) {
    if (!start(1)) return;
    SDL_ShowSaveFileDialog(chosen, NULL, window, zipFilter, 1, "xenogears-saves.zip");
}

void backupImport(SDL_Window* window) {
    if (!start(0)) return;
    SDL_ShowOpenFileDialog(chosen, NULL, window, zipFilter, 1, NULL, false);
}

int backupStatus(char* text, size_t size, int* error) {
    SDL_LockSpinlock(&lock);
    snprintf(text, size, "%s", status);
    *error = statusError;
    int r = running;
    SDL_UnlockSpinlock(&lock);
    return r;
}
