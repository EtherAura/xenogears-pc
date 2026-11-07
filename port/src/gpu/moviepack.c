/*
 * Replacement movie frames (moviepack.h, xg-htn.35.10).
 *
 * The player's hooks say which stream plays and when a frame is complete in
 * VRAM (its rectangle, its number from the STR header). Worker threads read
 * that frame's picture and the next few ahead of time, from the pack's PNGs
 * (uncompressed ones without inflating, pngfast.c; others with SDL's
 * reader) or its AV1 video (av1video.c, one thread at a time, in order).
 *
 * 24-bit movies: the present pass asks for the frame the display shows and
 * gets the picture when it is ready, else the original frame shows (a
 * picture read late keeps the last one up to two frames): a slow disk costs
 * a replaced frame, never a late one. A frame is the display's only while
 * its VRAM is as the player left it: the tile generations (vram.c) of its
 * rectangle are recorded when it completes, and a later draw or load there
 * ends the replacement.
 *
 * 15-bit movies (the field's backgrounds: frames loaded into one buffer of
 * VRAM that the field draws from, as a direct-colour texture, while the next
 * frame is loaded over it slice by slice): each slice's part of the picture
 * goes into the hi-res copy of VRAM right after the slice's upload
 * (gvkHiPicture), where the hi-res draws take their direct-colour texels.
 * Without upscaling there is no copy, and a picture not ready at the upload
 * leaves the slice as decoded.
 */

#include "moviepack.h"

#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "av1video.h"
#include "gpu.h"
#include "gpu_internal.h"
#include "pngfast.h"
#include "system/settings.h"
#include "../system/savestate.h"

#define CACHE 16      /* pictures held */
#define AHEAD 8       /* frames read ahead of the last completed one */
#define HELD_BYTES (192u * 1024u * 1024u) /* beyond the next frame, no more read ahead than this */
#define THREADS 2     /* readers: a large PNG takes longer than a movie frame (66 ms) */
#define MAX_SIDE 4096
#define MAX_PIXELS (16u * 1024u * 1024u)
#define PNG_BYTES (128u * 1024u * 1024u)
#define STR_FPS 15 /* the streams' frames a second (double speed, 10 sectors a frame) */

enum { SLOT_EMPTY, SLOT_LOADING, SLOT_READY, SLOT_MISSING, SLOT_INVALID };
enum { SRC_NONE, SRC_PNG, SRC_AV1 };

typedef struct {
    int state, frame;
    uint32_t session; /* the stream's start it belongs to */
    int w, h;
    uint8_t* rgba;
    uint64_t used;
} Slot;

typedef struct {
    int valid, frame, rgb24;
    VRect r;          /* inclusive, VRAM halfwords */
    int w, h;         /* the frame in pixels */
    uint64_t gen;     /* the newest tile generation in r when it completed */
} Buffer;

static struct {
    SDL_Mutex* lock;
    SDL_Condition* wake;
    SDL_Thread* thread[THREADS];
    int quit;
    int settingsRead, enabled;
    /* the stream playing */
    uint32_t session;
    int active, kind, disc, stream;
    int playing; /* between the player's begin and end (with or without pictures in the pack) */
    char dir[3400];   /* SRC_PNG: its folder; SRC_AV1: the video file */
    int want; /* the last completed frame: the workers read want .. want + AHEAD */
    int w, h; /* the stream's picture size (the first picture's) */
    Buffer buf[2];
    Slot slot[CACHE];
    uint64_t clock;
    MoviePackStats stats;
    double decodeTotalMs;
    uint64_t decodes;
    char lastDumpDir[3400];
    uint64_t lastId; /* the last picture shown, held while the next one is read */
    int lastFrame;
    int countedFrame; /* the last frame counted as late or missing */
    uint64_t shownId;
    int lastHiFrame; /* the last 15-bit frame given its picture */
    /* SRC_AV1: the decoder (one worker at a time), for which session */
    MovieVideo* video;
    uint32_t videoSession;
    int videoBusy;
} mp;

static void readSettings(void) {
    if (!mp.settingsRead) {
        mp.settingsRead = 1;
        settingsDescribe("movie_replacements", "movie_replacements: show a texture pack's movie pictures "
                                               "(movies/d<disc>s<stream>/<frame>.png or movies/d<disc>s<stream>.webm, "
                                               "AV1) in place of the movies' frames");
        mp.enabled = settingsBool("movie_replacements", 1);
    }
}

int moviePackEnabled(void) {
    readSettings();
    return mp.enabled;
}

void moviePackSetEnabled(int on) {
    readSettings();
    mp.enabled = on != 0;
    settingsSet("movie_replacements", mp.enabled ? "1" : "0");
}

int moviePackDumpStored(void) {
    return pngDumpStored();
}

void moviePackSetDumpStored(int on) {
    pngSetDumpStored(on);
}

/* A frame's PNG from the pack: RGBA bytes, or NULL (*missing: no file);
 * *stored: it was an uncompressed one. */
static uint8_t* readPng(const char* dir, int frame, int* w, int* h, int* missing, int* stored) {
    char path[3500];
    snprintf(path, sizeof(path), "%s/%05d.png", dir, frame);
    SDL_PathInfo info;
    *missing = *stored = 0;
    if (!SDL_GetPathInfo(path, &info)) {
        *missing = 1;
        return NULL;
    }
    if (info.type != SDL_PATHTYPE_FILE || info.size > PNG_BYTES) {
        return NULL;
    }
    size_t n = 0;
    uint8_t* file = SDL_LoadFile(path, &n);
    if (file == NULL) {
        return NULL;
    }
    int other = 0;
    uint8_t* out = pngDecodeStored(file, n, w, h, MAX_SIDE, MAX_PIXELS, &other);
    if (out != NULL) {
        *stored = 1;
        SDL_free(file);
        return out;
    }
    if (!other) {
        SDL_free(file); /* not a PNG, damaged, or too large */
        return NULL;
    }
    /* a compressed (or other kind of) PNG: SDL's reader */
    SDL_Surface* s = SDL_LoadPNG_IO(SDL_IOFromConstMem(file, n), true);
    SDL_free(file);
    if (s == NULL || s->w > MAX_SIDE || s->h > MAX_SIDE || (uint64_t)s->w * s->h > MAX_PIXELS) {
        SDL_DestroySurface(s);
        return NULL;
    }
    SDL_Surface* rgba = SDL_ConvertSurface(s, SDL_PIXELFORMAT_RGBA32);
    SDL_DestroySurface(s);
    if (rgba == NULL) {
        return NULL;
    }
    out = malloc((size_t)rgba->w * rgba->h * 4);
    if (out != NULL) {
        for (int y = 0; y < rgba->h; y++) {
            memcpy(out + (size_t)y * rgba->w * 4, (const uint8_t*)rgba->pixels + (size_t)y * rgba->pitch,
                   (size_t)rgba->w * 4);
        }
        *w = rgba->w;
        *h = rgba->h;
    }
    SDL_DestroySurface(rgba);
    return out;
}

static Slot* findSlot(int frame) {
    for (int i = 0; i < CACHE; i++) {
        if (mp.slot[i].state != SLOT_EMPTY && mp.slot[i].session == mp.session && mp.slot[i].frame == frame) {
            return &mp.slot[i];
        }
    }
    return NULL;
}

static void freeSlot(Slot* s) {
    free(s->rgba);
    memset(s, 0, sizeof(*s));
}

/* A slot for a new picture: an empty one, else the oldest not loading and
 * not wanted (frames before `want`, or another session's). */
static Slot* takeSlot(void) {
    Slot* best = NULL;
    for (int i = 0; i < CACHE; i++) {
        Slot* s = &mp.slot[i];
        if (s->state == SLOT_EMPTY) {
            return s;
        }
        if (s->state == SLOT_LOADING) {
            continue;
        }
        /* the two frames before `want` stay: one of them may be on screen */
        int stale = s->session != mp.session || s->frame < mp.want - 2 || s->frame > mp.want + AHEAD;
        if (stale && (best == NULL || s->used < best->used)) {
            best = s;
        }
    }
    if (best != NULL) {
        freeSlot(best);
    }
    return best;
}

/* Bytes of this stream's pictures held or being read. */
static size_t heldBytes(void) {
    size_t n = 0;
    for (int i = 0; i < CACHE; i++) {
        if (mp.slot[i].state != SLOT_EMPTY && mp.slot[i].session == mp.session) {
            n += (size_t)(mp.w > 0 ? mp.w : 1) * (mp.h > 0 ? mp.h : 1) * 4;
        }
    }
    return n;
}

/* STR frame `frame`'s picture from the video (the worker holds videoBusy,
 * not the lock). */
static uint8_t* readVideo(int frame, int* w, int* h, int* missing, uint32_t session, const char* path) {
    char err[256];
    *missing = 0;
    if (mp.video == NULL || mp.videoSession != session) {
        movieVideoClose(mp.video);
        mp.video = movieVideoOpen(path, err, sizeof(err));
        mp.videoSession = session;
        if (mp.video == NULL) {
            fprintf(stderr, "movie pack: %s\n", err);
            return NULL;
        }
    }
    /* the frame shown at (frame - 1) / 15 s; a millisecond of slack for
     * timestamps rounded to milliseconds */
    int64_t t = (int64_t)(frame - 1) * 1000000000 / STR_FPS + 1000000;
    int i = movieVideoFrameAt(mp.video, t);
    if (i < 0 || (i == movieVideoFrames(mp.video) - 1 && t > movieVideoTime(mp.video, i) + 1000000000 / 2)) {
        *missing = 1; /* before the video, or well past its end */
        return NULL;
    }
    uint8_t* rgba = movieVideoDecode(mp.video, i, w, h, err, sizeof(err));
    if (rgba == NULL) {
        fprintf(stderr, "movie pack: %s: %s\n", path, err);
    }
    return rgba;
}

static int SDLCALL worker(void* user) {
    (void)user;
    SDL_LockMutex(mp.lock);
    while (!mp.quit) {
        int frame = -1;
        int video = mp.kind == SRC_AV1;
        if (mp.active && mp.want > 0 && !(video && mp.videoBusy)) {
            for (int f = mp.want; f <= mp.want + AHEAD; f++) {
                if (f > mp.want + 1 && heldBytes() > HELD_BYTES) {
                    break; /* large pictures: read less far ahead */
                }
                if (findSlot(f) == NULL) {
                    frame = f;
                    break;
                }
            }
        }
        Slot* s = frame > 0 ? takeSlot() : NULL;
        if (s == NULL) {
            SDL_WaitCondition(mp.wake, mp.lock);
            continue;
        }
        s->state = SLOT_LOADING;
        s->frame = frame;
        s->session = mp.session;
        s->used = ++mp.clock;
        char dir[sizeof(mp.dir)];
        memcpy(dir, mp.dir, sizeof(dir));
        uint32_t session = mp.session;
        if (video) {
            mp.videoBusy = 1;
        }
        SDL_UnlockMutex(mp.lock);
        int w = 0, h = 0, missing = 0, stored = 0;
        uint64_t t0 = SDL_GetTicksNS();
        uint8_t* rgba = video ? readVideo(frame, &w, &h, &missing, session, dir)
                              : readPng(dir, frame, &w, &h, &missing, &stored);
        double ms = (double)(SDL_GetTicksNS() - t0) / 1e6;
        SDL_LockMutex(mp.lock);
        if (video) {
            mp.videoBusy = 0;
            SDL_BroadcastCondition(mp.wake);
        }
        if (rgba != NULL && session == mp.session) {
            /* any size: each picture covers its frame (a size change costs
             * the present pass a new image, so packs keep one size) */
            mp.w = mp.stats.w = w;
            mp.h = mp.stats.h = h;
            {
                mp.decodes++;
                mp.decodeTotalMs += ms;
                mp.stats.decodeMs = mp.decodeTotalMs / (double)mp.decodes;
                mp.stats.decodeMsMax = ms > mp.stats.decodeMsMax ? ms : mp.stats.decodeMsMax;
                if (!video) {
                    mp.stats.storedPngs += stored;
                    mp.stats.compressedPngs += !stored;
                    snprintf(mp.stats.format, sizeof(mp.stats.format), "%s",
                             mp.stats.compressedPngs == 0 ? "PNG, uncompressed"
                             : mp.stats.storedPngs == 0   ? "PNG, compressed"
                                                          : "PNG, mixed");
                }
            }
        }
        s->rgba = rgba;
        s->w = w;
        s->h = h;
        s->state = rgba != NULL ? SLOT_READY : missing ? SLOT_MISSING : SLOT_INVALID;
        if (s->state == SLOT_INVALID && session == mp.session) {
            mp.stats.invalid++;
            fprintf(stderr, "movie pack: %s frame %d: not a usable picture\n", dir, frame);
        }
    }
    SDL_UnlockMutex(mp.lock);
    return 0;
}

static void start(void) {
    if (mp.thread[0] == NULL) {
        mp.quit = 0;
        for (int i = 0; i < THREADS; i++) {
            mp.thread[i] = SDL_CreateThread(worker, "moviepack", NULL);
        }
    }
}

/* ---- the player's hooks ---- */

void moviePackBegin(int disc, int stream, int first) {
    readSettings();
    if (mp.lock == NULL) {
        mp.lock = SDL_CreateMutex();
        mp.wake = SDL_CreateCondition();
    }
    SDL_LockMutex(mp.lock);
    mp.playing = 1;
    memset(mp.buf, 0, sizeof(mp.buf));
    if (mp.active && disc == mp.disc && stream == mp.stream) {
        /* the same stream again (a loop, a read retried): its pictures stay */
        mp.want = first;
        SDL_BroadcastCondition(mp.wake);
        SDL_UnlockMutex(mp.lock);
        return;
    }
    mp.session++;
    mp.countedFrame = 0;
    mp.disc = mp.stats.disc = disc;
    mp.stream = mp.stats.stream = stream;
    mp.want = first; /* read ahead from the first frame */
    mp.w = mp.h = mp.stats.w = mp.stats.h = 0;
    mp.active = 0;
    mp.kind = SRC_NONE;
    /* the counters are this stream's */
    mp.stats.format[0] = '\0';
    mp.stats.shown = mp.stats.missing = mp.stats.late = mp.stats.invalid = 0;
    mp.stats.storedPngs = mp.stats.compressedPngs = 0;
    mp.stats.decodeMs = mp.stats.decodeMsMax = 0;
    mp.decodes = 0;
    mp.decodeTotalMs = 0;
    mp.lastHiFrame = 0;
    const char* root = texturePackRoot();
    if (mp.enabled && root != NULL && root[0] != '\0' && SDL_strcasecmp(root, "none") != 0) {
        SDL_PathInfo info;
        snprintf(mp.dir, sizeof(mp.dir), "%s/%s/movies/d%ds%02d", root, texturePackVersion(), disc, stream);
        if (SDL_GetPathInfo(mp.dir, &info) && info.type == SDL_PATHTYPE_DIRECTORY) {
            mp.kind = SRC_PNG;
            snprintf(mp.stats.format, sizeof(mp.stats.format), "PNG");
        } else {
            static const char* const exts[2] = {"webm", "mkv"};
            for (int i = 0; i < 2 && mp.kind == SRC_NONE; i++) {
                snprintf(mp.dir, sizeof(mp.dir), "%s/%s/movies/d%ds%02d.%s", root, texturePackVersion(), disc, stream,
                         exts[i]);
                if (SDL_GetPathInfo(mp.dir, &info) && info.type == SDL_PATHTYPE_FILE) {
                    mp.kind = movieVideoAvailable() ? SRC_AV1 : SRC_NONE;
                    snprintf(mp.stats.format, sizeof(mp.stats.format), "%s",
                             mp.kind == SRC_AV1 ? "AV1 video" : "AV1 video (no decoder in this build)");
                }
            }
        }
        mp.active = mp.kind != SRC_NONE;
    }
    mp.stats.active = mp.active;
    snprintf(mp.stats.message, sizeof(mp.stats.message), "disc %d stream %d: %s", disc, stream,
             mp.active ? "pictures from the pack" : "no pictures in the pack");
    if (mp.active) {
        fprintf(stderr, "movie pack: %s\n", mp.dir);
    }
    SDL_UnlockMutex(mp.lock);
    if (mp.active) {
        start();
        SDL_LockMutex(mp.lock);
        SDL_BroadcastCondition(mp.wake);
        SDL_UnlockMutex(mp.lock);
    }
}

void moviePackEnd(void) {
    mp.playing = 0;
    if (mp.lock == NULL) {
        return;
    }
    SDL_LockMutex(mp.lock);
    mp.session++;
    mp.active = 0;
    mp.want = 0;
    memset(mp.buf, 0, sizeof(mp.buf));
    for (int i = 0; i < CACHE; i++) {
        if (mp.slot[i].state != SLOT_LOADING) {
            freeSlot(&mp.slot[i]);
        }
    }
    SDL_UnlockMutex(mp.lock);
}

static uint64_t newestGeneration(VRect r) {
    uint64_t g = 0;
    for (int y = r.y0 >> TILE_SHIFT; y <= r.y1 >> TILE_SHIFT; y++) {
        for (int x = r.x0 >> TILE_SHIFT; x <= r.x1 >> TILE_SHIFT; x++) {
            uint64_t t = vramTileGeneration(x, y);
            g = t > g ? t : g;
        }
    }
    return g;
}

/* Texture discovery (texture_dump): the decoded frame, for upscaling. */
static void dumpFrame(int frame, VRect r, int w, int h, int rgb24) {
    const char* root = textureDumpEnabled() ? textureDumpRoot() : NULL;
    if (root == NULL || root[0] == '\0') {
        return;
    }
    char dir[3400], path[3500];
    snprintf(dir, sizeof(dir), "%s/%s/movies/d%ds%02d", root, texturePackVersion(), mp.disc, mp.stream);
    if (strcmp(dir, mp.lastDumpDir) != 0) {
        SDL_CreateDirectory(dir);
        snprintf(mp.lastDumpDir, sizeof(mp.lastDumpDir), "%s", dir);
    }
    snprintf(path, sizeof(path), "%s/%05d.png", dir, frame);
    SDL_PathInfo info;
    if (SDL_GetPathInfo(path, &info)) {
        return;
    }
    GpuDisplay d = {0};
    d.x = r.x0;
    d.y = r.y0;
    d.w = w;
    d.h = h;
    d.rgb24 = rgb24;
    d.enabled = 1;
    uint8_t* rgb = malloc((size_t)w * h * 3);
    if (rgb != NULL) {
        displayToRgb(&d, rgb); /* uploads keep the CPU copy exact */
        int ok = pngDumpStored() ? pngWriteStored(path, w, h, 3, rgb, (size_t)w * 3) : pngWrite(path, w, h, rgb);
        if (ok) {
            SDL_LockMutex(mp.lock);
            mp.stats.dumped++;
            SDL_UnlockMutex(mp.lock);
        }
        free(rgb);
    }
}

void moviePackFrame(int frame, int x0, int y0, int x1, int y1, int rgb24) {
    if (mp.lock == NULL || x1 <= x0 || y1 <= y0) {
        return;
    }
    VRect r = {x0 & (VRAM_W - 1), y0 & (VRAM_H - 1), (x1 - 1) & (VRAM_W - 1), (y1 - 1) & (VRAM_H - 1)};
    if (r.x1 < r.x0 || r.y1 < r.y0) {
        return; /* wraps: not a movie frame we can match */
    }
    int w = rgb24 ? (x1 - x0) * 2 / 3 : x1 - x0, h = y1 - y0;
    dumpFrame(frame, r, w, h, rgb24);
    SDL_LockMutex(mp.lock);
    if (mp.active && frame < mp.want - 1 && mp.want - frame > AHEAD) {
        /* numbers going back within one start: not on the retail discs (each
         * stream counts from 1); noted, the pictures follow the numbers */
        fprintf(stderr, "movie pack: disc %d stream %d: frame %d after %d\n", mp.disc, mp.stream, frame, mp.want);
    }
    Buffer* b = &mp.buf[0];
    if (mp.buf[0].valid && (mp.buf[0].r.x0 != r.x0 || mp.buf[0].r.y0 != r.y0)) {
        b = &mp.buf[1];
        if (mp.buf[1].valid && (mp.buf[1].r.x0 != r.x0 || mp.buf[1].r.y0 != r.y0)) {
            b = mp.buf[0].frame < mp.buf[1].frame ? &mp.buf[0] : &mp.buf[1];
        }
    }
    b->valid = 1;
    b->frame = frame;
    b->rgb24 = rgb24;
    b->r = r;
    b->w = w;
    b->h = h;
    b->gen = newestGeneration(r);
    if (mp.active) {
        mp.want = frame;
        SDL_BroadcastCondition(mp.wake);
    }
    SDL_UnlockMutex(mp.lock);
}

void moviePackSlice(int frame, int x0, int y0, int x1, int y1, int fx0, int fy0, int fx1, int fy1, int rgb24) {
    if (mp.lock == NULL || rgb24 || !mp.active || !mp.enabled || gvkHiScale() == 0 || x1 <= x0 || y1 <= y0 ||
        fx1 <= fx0 || fy1 <= fy0 || x0 < 0 || y0 < 0 || x1 > VRAM_W || y1 > VRAM_H) {
        return;
    }
    SDL_LockMutex(mp.lock);
    Slot* s = findSlot(frame);
    if (s != NULL && s->state == SLOT_READY) {
        int mask = (vramShadow[y0][x0] & 0x8000) != 0;
        uint64_t id = (uint64_t)mp.session << 32 | (uint32_t)frame;
        if (gvkHiPicture(x0, y0, x1 - 1, y1 - 1, s->rgba, s->w, s->h, id, fx0, fy0, fx1 - fx0, fy1 - fy0, mask) &&
            frame != mp.lastHiFrame) {
            mp.lastHiFrame = frame;
            mp.stats.shown++;
        }
    } else if (frame != mp.countedFrame) {
        mp.countedFrame = frame;
        if (s == NULL || s->state == SLOT_LOADING) {
            mp.stats.late++;
        } else {
            mp.stats.missing += s->state == SLOT_MISSING;
        }
    }
    SDL_UnlockMutex(mp.lock);
}

/* ---- the present pass ---- */

int moviePackPicture(int x, int y, int w, int h, MoviePicture* out) {
    if (mp.lock == NULL || !mp.active || !mp.enabled) {
        return 0;
    }
    int found = 0;
    SDL_LockMutex(mp.lock);
    for (int i = 0; i < 2 && !found; i++) {
        Buffer* b = &mp.buf[i];
        /* the frame where the display area shows it (the area may crop it:
         * the movies are 224 lines, the display 216), at a whole pixel */
        int dx = b->r.x0 - x, dy = b->r.y0 - y;
        if (!b->valid || !b->rgb24 || dx * 2 % 3 != 0 || dx * 2 / 3 >= w || dy >= h || dx * 2 / 3 + b->w <= 0 ||
            dy + b->h <= 0) {
            continue;
        }
        if (newestGeneration(b->r) != b->gen) {
            continue; /* drawn or loaded over since */
        }
        Slot* s = findSlot(b->frame);
        if (s == NULL || s->state == SLOT_LOADING) {
            /* not read yet: the last picture stays a frame or two rather
             * than the original flashing in between */
            if (b->frame != mp.countedFrame) {
                mp.countedFrame = b->frame;
                mp.stats.late++;
            }
            Slot* last = (uint32_t)(mp.lastId >> 32) == mp.session && b->frame - mp.lastFrame <= 2 &&
                                 b->frame > mp.lastFrame
                             ? findSlot(mp.lastFrame)
                             : NULL;
            if (last == NULL || last->state != SLOT_READY) {
                found = -1;
                break;
            }
            s = last;
        } else if (s->state != SLOT_READY) {
            if (b->frame != mp.countedFrame) {
                mp.countedFrame = b->frame;
                mp.stats.missing += s->state == SLOT_MISSING;
            }
            found = -1;
            break;
        }
        s->used = ++mp.clock;
        out->id = (uint64_t)mp.session << 32 | (uint32_t)s->frame;
        if (s->frame == b->frame) {
            mp.lastId = out->id;
            mp.lastFrame = s->frame;
        }
        out->w = s->w;
        out->h = s->h;
        out->rect[0] = dx * 2 / 3;
        out->rect[1] = dy;
        out->rect[2] = b->w;
        out->rect[3] = b->h;
        if (out->id != mp.shownId) {
            mp.shownId = out->id;
            mp.stats.shown++;
        }
        found = 1;
    }
    SDL_UnlockMutex(mp.lock);
    return found > 0;
}

int moviePackCopy(uint64_t id, uint8_t* dst, size_t pitch) {
    if (mp.lock == NULL) {
        return 0;
    }
    int ok = 0;
    SDL_LockMutex(mp.lock);
    Slot* s = (uint32_t)(id >> 32) == mp.session ? findSlot((int)(uint32_t)id) : NULL;
    if (s != NULL && s->state == SLOT_READY) {
        for (int y = 0; y < s->h; y++) {
            memcpy(dst + (size_t)y * pitch, s->rgba + (size_t)y * s->w * 4, (size_t)s->w * 4);
        }
        ok = 1;
    }
    SDL_UnlockMutex(mp.lock);
    return ok;
}

void moviePackShutdown(void) {
    if (mp.lock == NULL) {
        return;
    }
    if (mp.thread[0] != NULL) {
        SDL_LockMutex(mp.lock);
        mp.quit = 1;
        SDL_BroadcastCondition(mp.wake);
        SDL_UnlockMutex(mp.lock);
        for (int i = 0; i < THREADS; i++) {
            SDL_WaitThread(mp.thread[i], NULL);
        }
    }
    if (mp.stats.shown > 0) {
        fprintf(stderr, "movie pack: %llu pictures shown (%s), %llu read late, %llu frames without one; "
                        "%.1f ms a picture to read (worst %.1f)\n",
                (unsigned long long)mp.stats.shown, mp.stats.format, (unsigned long long)mp.stats.late,
                (unsigned long long)mp.stats.missing, mp.stats.decodeMs, mp.stats.decodeMsMax);
    }
    movieVideoClose(mp.video);
    for (int i = 0; i < CACHE; i++) {
        freeSlot(&mp.slot[i]);
    }
    SDL_DestroyCondition(mp.wake);
    SDL_DestroyMutex(mp.lock);
    int enabled = mp.enabled, read = mp.settingsRead;
    memset(&mp, 0, sizeof(mp));
    mp.enabled = enabled;
    mp.settingsRead = read;
}

void moviePackStats(MoviePackStats* out) {
    readSettings();
    if (mp.lock != NULL) {
        SDL_LockMutex(mp.lock);
    }
    *out = mp.stats;
    out->cacheBytes = 0;
    out->cacheFrames = out->loadingFrames = 0;
    for (int i = 0; i < CACHE; i++) {
        const Slot* s = &mp.slot[i];
        if (s->rgba != NULL) {
            out->cacheBytes += (uint64_t)s->w * s->h * 4;
            out->cacheFrames++;
        }
        out->loadingFrames += s->state == SLOT_LOADING;
    }
    if (mp.lock != NULL) {
        SDL_UnlockMutex(mp.lock);
    }
}

/* ---- save states (system/savestate.h) ---- */

/* The stream the movie player is in and its frame: a state loaded (or
 * rewound) mid-movie takes the pack's pictures up again from there, as the
 * player's own begin does (the same stream keeps the pictures read). */
void moviePackStateIo(XgState* s) {
    int playing = mp.playing; /* between movies the rest is stale: saved as zeros */
    int disc = playing ? mp.disc : 0, stream = playing ? mp.stream : 0, want = playing ? mp.want : 0;
    XG_STATE_VAR(s, playing);
    XG_STATE_VAR(s, disc);
    XG_STATE_VAR(s, stream);
    XG_STATE_VAR(s, want);
    if (!xgStateLoading(s)) {
        return;
    }
    if (playing) {
        moviePackBegin(disc, stream, want);
    } else {
        moviePackEnd();
    }
}
