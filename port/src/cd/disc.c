/*
 * Disc images: a .cue sheet and its BINARY files of raw 2352-byte sectors
 * (redump style, any number of files and tracks), or a raw .bin alone.
 *
 * Positions are kept in "cue time": sectors from the start of the first file,
 * with virtual PREGAP/POSTGAP sectors (not stored in any file) inserted where
 * the sheet puts them. LBA 0 is the first track's INDEX 01, which the drive
 * reports as MSF 00:02:00.
 */

#define _FILE_OFFSET_BITS 64
#define _POSIX_C_SOURCE 200809L

#include "cd_internal.h"

#include <SDL3/SDL.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <errno.h>
#include <limits.h>

#ifdef _WIN32
#define fseek64 _fseeki64
#define ftell64 _ftelli64
#else
#define fseek64 fseeko
#define ftell64 ftello
#endif

static char lastError[1024];
const char* discLastError(void) { return lastError; }
static void imageError(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(lastError, sizeof(lastError), fmt, ap);
    va_end(ap);
    size_t n = strlen(lastError);
    if (n && lastError[n - 1] == '\n') lastError[n - 1] = 0;
    fprintf(stderr, "%s\n", lastError);
}

#define MAX_FILES 99
#define MAX_TRACKS 99
#define MAX_EXTENTS (MAX_TRACKS * 3 + MAX_FILES)

typedef struct {
    int start;  /* cue time of the extent's first sector */
    int length;
    int file;   /* index into files, or -1 for a virtual gap */
    int offset; /* first sector within the file */
} Extent;

typedef struct {
    int number;
    int audio;
    int index0; /* cue time of INDEX 00 (pregap start); == index1 if none */
    int index1; /* cue time of INDEX 01 */
} Track;

struct CdDisc {
    FILE* files[MAX_FILES];
    char* paths[MAX_FILES]; /* the files' paths (discWarm) */
    int fileCount;
    Extent extents[MAX_EXTENTS];
    int extentCount;
    Track tracks[MAX_TRACKS];
    int trackCount;
    int origin; /* cue time of LBA 0 */
    int end;    /* cue time of the lead-out */
};

void discClose(CdDisc* disc) {
    if (disc == NULL) {
        return;
    }
    for (int i = 0; i < disc->fileCount; i++) {
        if (disc->files[i] != NULL) {
            fclose(disc->files[i]);
        }
        free(disc->paths[i]);
    }
    free(disc);
}

static int addFile(CdDisc* disc, const char* path, int64_t* sectors) {
    if (disc->fileCount >= MAX_FILES) {
        imageError("cd: too many FILEs in the cue sheet\n");
        return -1;
    }
    FILE* fp = fopen(path, "rb");
    if (fp == NULL) {
        imageError("cd: cannot open %s: %s\n", path, strerror(errno));
        return -1;
    }
    int64_t size = 0;
    if (fseek64(fp, 0, SEEK_END) == 0) {
        size = ftell64(fp);
    }
    if (size <= 0 || size % CD_RAW_SECTOR != 0 || size / CD_RAW_SECTOR > INT_MAX) {
        imageError("cd: %s is not a raw image of 2352-byte sectors\n", path);
        fclose(fp);
        return -1;
    }
    *sectors = size / CD_RAW_SECTOR;
    disc->files[disc->fileCount] = fp;
    disc->paths[disc->fileCount] = strdup(path);
    return disc->fileCount++;
}

static int addExtent(CdDisc* disc, int start, int length, int file, int offset) {
    if (start < 0 || length < 0 || offset < 0 || length > INT_MAX - start) {
        imageError("cd: cue sector range is invalid or too large");
        return 0;
    }
    if (length <= 0) {
        return 1;
    }
    if (disc->extentCount >= MAX_EXTENTS) {
        imageError("cd: cue sheet too complex\n");
        return 0;
    }
    disc->extents[disc->extentCount++] = (Extent){ start, length, file, offset };
    return 1;
}

/* Next whitespace-separated token of a cue line, honouring double quotes. */
static char* nextToken(char** cursor) {
    char* p = *cursor;
    while (*p != '\0' && isspace((unsigned char)*p)) {
        p++;
    }
    if (*p == '\0') {
        *cursor = p;
        return NULL;
    }
    char* tok;
    if (*p == '"') {
        tok = ++p;
        while (*p != '\0' && *p != '"') {
            p++;
        }
    } else {
        tok = p;
        while (*p != '\0' && !isspace((unsigned char)*p)) {
            p++;
        }
    }
    if (*p != '\0') {
        *p++ = '\0';
    }
    *cursor = p;
    return tok;
}

static int parseMsf(const char* s) {
    const int limits[3] = {99, 59, 74};
    int parts[3];
    if (s == NULL) return -1;
    for (int i = 0; i < 3; i++) {
        char* end;
        errno = 0;
        long n = strtol(s, &end, 10);
        if (errno == ERANGE || end == s || n < 0 || n > limits[i] || *end != (i == 2 ? '\0' : ':')) return -1;
        parts[i] = (int)n;
        s = end + (i != 2);
    }
    return (parts[0] * 60 + parts[1]) * 75 + parts[2];
}

/* State of the sheet's current FILE while extents are emitted. */
typedef struct {
    int file;
    int64_t sectors;
    int consumed; /* file sectors already covered by extents */
    int time;     /* cue time where the next extent starts */
    int pregap;   /* pending PREGAP of the current track */
    int postgap;  /* pending POSTGAP of the previous track */
    int pendingIndex0;
} Sheet;

/* Cover the current file up to file sector `pos`, then a pending gap. */
static int advanceTo(CdDisc* disc, Sheet* s, int pos, int gap) {
    if (pos > s->consumed) {
        if (!addExtent(disc, s->time, pos - s->consumed, s->file, s->consumed)) {
            return 0;
        }
        s->time += pos - s->consumed;
        s->consumed = pos;
    }
    if (gap > 0) {
        if (!addExtent(disc, s->time, gap, -1, 0)) {
            return 0;
        }
        s->time += gap;
    }
    return 1;
}

static int finishFile(CdDisc* disc, Sheet* s) {
    if (s->file < 0) {
        return 1;
    }
    int ok = advanceTo(disc, s, (int)s->sectors, s->postgap);
    s->postgap = 0;
    s->file = -1;
    return ok;
}

static int parseCue(CdDisc* disc, const char* cuePath) {
    FILE* fp = fopen(cuePath, "r");
    if (fp == NULL) {
        imageError("cd: cannot open %s: %s\n", cuePath, strerror(errno));
        return 0;
    }
    char dir[1024];
    if (strlen(cuePath) >= sizeof dir) {
        imageError("cd: cue path is longer than 1023 bytes");
        fclose(fp);
        return 0;
    }
    snprintf(dir, sizeof dir, "%s", cuePath);
    char* slash = strrchr(dir, '/');
#ifdef _WIN32
    char* bslash = strrchr(dir, '\\');
    if (bslash != NULL && (slash == NULL || bslash > slash)) {
        slash = bslash;
    }
#endif
    if (slash != NULL) {
        slash[1] = '\0';
    } else {
        dir[0] = '\0';
    }

    Sheet s = { .file = -1 };
    Track* track = NULL;
    char line[1024];
    int ok = 1;
    int lineNo = 0;
    while (ok && fgets(line, sizeof line, fp) != NULL) {
        lineNo++;
        char* cursor = line;
        char* cmd = nextToken(&cursor);
        if (cmd == NULL) {
            continue;
        }
        if (strcmp(cmd, "FILE") == 0) {
            char* name = nextToken(&cursor);
            char* kind = nextToken(&cursor);
            if (name == NULL || (kind != NULL && strcmp(kind, "BINARY") != 0)) {
                imageError("cd: %s:%d: only BINARY files are supported\n", cuePath, lineNo);
                ok = 0;
                break;
            }
            ok = finishFile(disc, &s);
            char path[2048];
            int absolute = name[0] == '/';
#ifdef _WIN32
            absolute |= name[0] == '\\' || (isalpha((unsigned char)name[0]) && name[1] == ':');
#endif
            if (absolute) {
                snprintf(path, sizeof path, "%s", name);
            } else {
                snprintf(path, sizeof path, "%s%s", dir, name);
            }
            int64_t sectors = 0;
            s.file = addFile(disc, path, &sectors);
            s.sectors = sectors;
            s.consumed = 0;
            ok = ok && s.file >= 0;
        } else if (strcmp(cmd, "TRACK") == 0) {
            char* num = nextToken(&cursor);
            char* type = nextToken(&cursor);
            if (num == NULL || type == NULL || disc->trackCount >= MAX_TRACKS || s.file < 0) {
                imageError("cd: %s:%d: bad TRACK\n", cuePath, lineNo);
                ok = 0;
                break;
            }
            int audio = strcmp(type, "AUDIO") == 0;
            if (!audio && strcmp(type, "MODE2/2352") != 0 && strcmp(type, "MODE1/2352") != 0) {
                imageError("cd: %s:%d: track type %s not supported (raw 2352-byte sectors only)\n", cuePath,
                        lineNo, type);
                ok = 0;
                break;
            }
            track = &disc->tracks[disc->trackCount++];
            *track = (Track){ atoi(num), audio, -1, -1 };
            s.pregap = 0;
            s.pendingIndex0 = -1;
        } else if (strcmp(cmd, "PREGAP") == 0) {
            int len = parseMsf(nextToken(&cursor));
            if (track == NULL || len < 0) {
                ok = 0;
                break;
            }
            s.pregap = len;
        } else if (strcmp(cmd, "POSTGAP") == 0) {
            int len = parseMsf(nextToken(&cursor));
            if (track == NULL || len < 0) {
                ok = 0;
                break;
            }
            s.postgap = len;
        } else if (strcmp(cmd, "INDEX") == 0) {
            char* num = nextToken(&cursor);
            int pos = parseMsf(nextToken(&cursor));
            if (track == NULL || num == NULL || pos < 0 || pos > s.sectors) {
                imageError("cd: %s:%d: bad INDEX\n", cuePath, lineNo);
                ok = 0;
                break;
            }
            int index = atoi(num);
            if (index == 0 || (index == 1 && track->index1 < 0)) {
                /* The first INDEX of a track: the previous track's postgap
                 * and this track's pregap go in front of it. */
                if (track->index0 < 0) {
                    ok = advanceTo(disc, &s, pos, s.postgap);
                    s.postgap = 0;
                    int gapStart = s.time;
                    ok = ok && advanceTo(disc, &s, pos, s.pregap);
                    track->index0 = s.pregap > 0 ? gapStart : s.time;
                    s.pregap = 0;
                } else {
                    ok = advanceTo(disc, &s, pos, 0);
                }
                if (index == 1) {
                    track->index1 = s.time;
                }
            }
        }
        /* REM, CATALOG, FLAGS, ISRC, PERFORMER, TITLE...: not needed. */
    }
    fclose(fp);
    ok = ok && finishFile(disc, &s);
    if (ok && disc->trackCount == 0) {
        imageError("cd: %s has no tracks\n", cuePath);
        ok = 0;
    }
    for (int i = 0; ok && i < disc->trackCount; i++) {
        if (disc->tracks[i].index1 < 0) {
            imageError("cd: %s: track %d has no INDEX 01\n", cuePath, disc->tracks[i].number);
            ok = 0;
        }
    }
    if (ok) {
        disc->origin = disc->tracks[0].index1;
        disc->end = s.time;
    }
    return ok;
}

static int endsWith(const char* s, const char* suffix) {
    size_t n = strlen(s), m = strlen(suffix);
    if (n < m) {
        return 0;
    }
    for (size_t i = 0; i < m; i++) {
        if (tolower((unsigned char)s[n - m + i]) != suffix[i]) {
            return 0;
        }
    }
    return 1;
}

CdDisc* discOpen(const char* path) {
    snprintf(lastError, sizeof(lastError), "Cannot open disc image");
    if (!path || !*path) { imageError("Choose a .cue or raw .bin disc image"); return NULL; }
    CdDisc* disc = calloc(1, sizeof *disc);
    if (disc == NULL) {
        imageError("Out of memory opening the disc image");
        return NULL;
    }
    int ok;
    if (endsWith(path, ".cue")) {
        ok = parseCue(disc, path);
    } else {
        /* A raw image alone: one data track. */
        int64_t sectors = 0;
        int file = addFile(disc, path, &sectors);
        ok = file >= 0 && addExtent(disc, 0, (int)sectors, file, 0);
        if (ok) {
            disc->tracks[0] = (Track){ 1, 0, 0, 0 };
            disc->trackCount = 1;
            disc->origin = 0;
            disc->end = (int)sectors;
        }
    }
    if (!ok) {
        discClose(disc);
        return NULL;
    }
    return disc;
}

/* Each image file read once on a low-priority thread of its own, into the
 * system's file cache (xg-2fy.2): the drive's reads then never wait for the
 * disk. Most of an image is read only now and then, so it was not in the
 * cache, and a read that found the SSD in a deep power-saving state blocked
 * the game thread for 60-85 ms (in readSector at a room change, at a
 * cutscene's first Japanese audio), each a gap in the sound. Once per path
 * for the process; the thread opens the file itself and keeps nothing. */
static int SDLCALL warmRun(void* arg) {
    char* path = arg;
    SDL_SetCurrentThreadPriority(SDL_THREAD_PRIORITY_LOW);
    FILE* fp = fopen(path, "rb");
    char* buffer = malloc(1 << 20);
    uint64_t began = SDL_GetTicksNS(), bytes = 0;
    size_t n;
    while (fp != NULL && buffer != NULL && (n = fread(buffer, 1, 1 << 20, fp)) > 0) {
        bytes += n;
    }
    if (fp != NULL) {
        const char* name = path;
        for (const char* c = path; *c != '\0'; c++) {
            if (*c == '/' || *c == '\\') name = c + 1;
        }
        fprintf(stderr, "cd: %s in the file cache (%.0f MB in %.1f s)\n", name,
                (double)bytes / (1 << 20), (double)(SDL_GetTicksNS() - began) * 1e-9);
        fclose(fp);
    }
    free(buffer);
    free(path);
    return 0;
}

void discWarm(const CdDisc* disc) {
    static char* warmed[16];
    static int warmedCount;
    for (int i = 0; disc != NULL && i < disc->fileCount; i++) {
        const char* path = disc->paths[i];
        int seen = path == NULL;
        for (int k = 0; !seen && k < warmedCount; k++) {
            seen = strcmp(warmed[k], path) == 0;
        }
        if (seen || warmedCount == (int)(sizeof(warmed) / sizeof(warmed[0]))) {
            continue;
        }
        char* copy = strdup(path);
        SDL_Thread* t = copy != NULL ? SDL_CreateThread(warmRun, "disc cache", copy) : NULL;
        if (t == NULL) {
            free(copy);
            continue;
        }
        SDL_DetachThread(t);
        warmed[warmedCount++] = strdup(path);
        if (warmed[warmedCount - 1] == NULL) warmedCount--;
    }
}

int discSectorCount(const CdDisc* disc) {
    return disc->end - disc->origin;
}

int discTrackCount(const CdDisc* disc) {
    return disc->trackCount;
}

int discTrackNumber(const CdDisc* disc, int i) {
    return disc->tracks[i].number;
}

int discTrackStart(const CdDisc* disc, int i) {
    return disc->tracks[i].index1 - disc->origin;
}

int discTrackIsAudio(const CdDisc* disc, int i) {
    return disc->tracks[i].audio;
}

int discTrackAt(const CdDisc* disc, int lba) {
    int t = lba + disc->origin;
    int found = 0;
    for (int i = 0; i < disc->trackCount; i++) {
        if (disc->tracks[i].index0 <= t) {
            found = i;
        }
    }
    return found;
}

static uint8_t toBcd(int v) {
    return (uint8_t)((v / 10) * 16 + v % 10);
}

void discWriteHeader(uint8_t* raw, int lba, int mode) {
    static const uint8_t sync[12] = { 0, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0 };
    int a = lba + 150;
    memcpy(raw, sync, sizeof sync);
    raw[12] = toBcd(a / 4500);
    raw[13] = toBcd(a / 75 % 60);
    raw[14] = toBcd(a % 75);
    raw[15] = (uint8_t)mode;
}

int discRead(CdDisc* disc, int lba, uint8_t* out) {
    int t = lba + disc->origin;
    if (t < 0 || t >= disc->end) {
        return 0;
    }
    for (int i = 0; i < disc->extentCount; i++) {
        const Extent* e = &disc->extents[i];
        if (t < e->start || t >= e->start + e->length) {
            continue;
        }
        if (e->file >= 0) {
            FILE* fp = disc->files[e->file];
            int64_t at = (int64_t)(e->offset + (t - e->start)) * CD_RAW_SECTOR;
            if (fseek64(fp, at, SEEK_SET) != 0 || fread(out, CD_RAW_SECTOR, 1, fp) != 1) {
                return 0;
            }
            return 1;
        }
        break;
    }
    /* A virtual gap: silence, or an empty data sector with a valid header. */
    memset(out, 0, CD_RAW_SECTOR);
    const Track* track = &disc->tracks[discTrackAt(disc, lba)];
    if (!track->audio) {
        discWriteHeader(out, lba, 2);
    }
    return 1;
}
