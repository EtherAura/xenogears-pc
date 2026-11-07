/*
 * What the gallery lists, recovered from the player's disc when it opens
 * (gallery.h): the movies, where the game plays them, and the scenes that
 * move the story on. Nothing here is a table of the game's: it is all read
 * from the disc, with what our decompilation tells about the formats.
 *
 * Movies. Directory 0x18/1 holds the movie streams (the movie mode and the
 * field play movie n from its file n + 2; the game's own lookups find them,
 * disc.h xgDiscGameLookup). A stream holds one movie or several, each with
 * its frames numbered from 1 in the 32-byte STR header of its video sectors.
 * The streams are sampled every few sectors and the places where the frame
 * numbers start again are found by binary search.
 *
 * Where the game plays them. No table maps a movie to its stream and start:
 * the field scripts' movie ops carry them (src/field/f_80086D8C.c: extended
 * op 0xA0 with its sound sequence, 0x60 OP_SET_2D_BACKGROUND, 0x67 a movie
 * as the field's background), with the stream, a sector offset in it, the
 * first and last frame. Each op found is checked against the streams (the
 * frame at its sector, the movie's length), which also tells its disc. The
 * kernel's opening (kernelMain sets the movie mode's request: movie 0x10 on
 * disc 1, 7 on disc 2, frames 1 to 233) is the one played from code.
 * Battle events are read from the script/dialogue pairs in 20/02; their
 * reachable OP27 requests are checked against the same stream headers.
 *
 * Scenes. A field script's SET_GAMEPROGRESS (op 0x87) sets the story
 * progress (field variable 0) as a scene starts. Each one found is a scene:
 * its field, started with the story's previous progress value and the
 * entrance its actor's set-up tests for (a JUMP_IF on variable 2, which the
 * field takes from gameState 0x2320), from the disc the field's scenes play
 * on (disc 2 once the story is past every scene of the fields disc 2 does not
 * hold).
 *
 * The scripts are walked as the field VM runs them: from each actor's 32
 * routine entry points, along jumps, branches and calls, an instruction at a
 * time. The instruction lengths are the PC steps of the VM's handlers (src/
 * field: mCC_scriptPC += n), with the few that depend on an argument byte
 * worked out below; this reaches nearly all of the code (on US disc 1, all
 * but 36 places, every one an op whose handler does nothing).
 *
 * XG_GALLERY_LOG=1 puts each stream's lookup on stderr.
 */

#include "gallery.h"
#include "gallery_battle.h"

#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cd/cd_internal.h"
#include "files.h"

#define USER_SIZE 2048
#define FORM2_SIZE 2336 /* a stream's size counts its sectors so */
#define FIELD_DIR 4
#define FIELD_FILE(n) (0xB8 + 2 * (n))
#define FIELD_MIN_SIZE 0x190 /* placeholders (disc 2's "CDMAKE Dummy") are smaller */
#define MOVIE_DIR 0x19       /* (0x18, 1): the game names directories by base and sub */
#define SAMPLE_STEP 64

static uint16_t rd16(const uint8_t* p) {
    return (uint16_t)(p[0] | p[1] << 8);
}

static uint32_t rd32(const uint8_t* p) {
    return (uint32_t)(p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24);
}

/* ---- field scripts ------------------------------------------------------------------------------ */

int xgFieldScriptRead(XgDisc* disc, int field, XgFieldScript* out) {
    memset(out, 0, sizeof(*out));
    if (!disc || field < 0 || field > 729) return 0;
    int index = xgDiscFileEntry(disc, FIELD_DIR, FIELD_FILE(field));
    XgEntry e = index >= 0 ? xgDiscEntry(disc, index) : (XgEntry){0, 0};
    if (e.size < FIELD_MIN_SIZE) {
        return 0;
    }
    uint8_t* head = xgDiscReadSectors(disc, e.lba, 1);
    if (head == NULL) {
        return 0;
    }
    /* Section 5's decompressed size and offset (docs/ASSETS.md "Field archive"). */
    uint32_t want = rd32(head + 0x10C + 4 * 5);
    uint32_t at = rd32(head + 0x130 + 4 * 5);
    uint32_t end = (uint32_t)e.size;
    for (int k = 0; k < 9; k++) {
        uint32_t o = rd32(head + 0x130 + 4 * k);
        if (o > at && o < end) {
            end = o;
        }
    }
    free(head);
    if (at >= (uint32_t)e.size || end <= at || want < 0x84 || want > (8u << 20)) {
        return 0;
    }
    int first = (int)(at / USER_SIZE), count = (int)((end + 16 + USER_SIZE - 1) / USER_SIZE) - first;
    uint8_t* raw = xgDiscReadSectors(disc, e.lba + first, count);
    if (raw == NULL) {
        return 0;
    }
    int size = 0;
    uint8_t* code = xgLzssDecompress(raw + (at - (uint32_t)first * USER_SIZE), (int)(end - at), &size);
    free(raw);
    if (code == NULL || size < (int)want) {
        free(code);
        return 0;
    }
    uint32_t actors = rd32(code + 0x80);
    uint32_t codeAt = 0x84 + actors * 64;
    if (actors == 0 || actors > 256 || codeAt >= want) {
        free(code);
        return 0;
    }
    out->actors = (int)actors;
    out->routines = calloc(actors, sizeof(*out->routines));
    out->codeSize = (int)(want - codeAt);
    out->code = malloc((size_t)out->codeSize + 16);
    if (out->routines == NULL || out->code == NULL) {
        free(code);
        xgFieldScriptFree(out);
        return 0;
    }
    for (uint32_t a = 0; a < actors; a++) {
        for (int k = 0; k < 32; k++) {
            out->routines[a][k] = rd16(code + 0x84 + a * 64 + k * 2);
        }
    }
    memcpy(out->code, code + codeAt, (size_t)out->codeSize);
    memset(out->code + out->codeSize, 0, 16);
    free(code);
    return 1;
}

void xgFieldScriptFree(XgFieldScript* s) {
    free(s->code);
    free(s->routines);
    memset(s, 0, sizeof(*s));
}

int xgFieldMediaRead(XgDisc* disc, int field, XgFieldMedia** out) {
    *out = NULL;
    XgFieldScript fs;
    if (!xgFieldScriptRead(disc, field, &fs)) return -1;
    int count = xgFieldMediaDecode(&fs, field, out);
    xgFieldScriptFree(&fs);
    return count;
}

/* ---- the scan ------------------------------------------------------------------------------------ */

typedef struct {
    XgDisc* disc;
    CdDisc* image;  /* raw sectors (the subheader tells video from audio) */
    int streams;    /* movie streams */
    int fields;     /* field archives */
    uint8_t* real;  /* per field: not a placeholder */
    uint8_t* events;
    int eventBytes, eventCount;
    XgBundle eventBundle;
} Disc;

typedef struct {
    int field, setsTo, pc, entry, progressHint;
    int actor, slot, start;
} SetOp;

enum { PHASE_STREAMS, PHASE_FIELDS, PHASE_BATTLE, PHASE_DONE };

struct XgGalleryScan {
    Disc discs[2];
    int phase;
    int disc, item; /* where the phase is */
    XgMovieEntry* movies;
    int movieCount, movieCap;
    XgMovieUse* uses;
    int useCount, useCap;
    SetOp* sets;
    int setCount, setCap;
    XgScene* scenes;
    int sceneCount;
    int pictureField[256]; /* the first field showing event picture n (+1; 0 none) */
    char doing[96];
};

#define GROW(arr, count, cap)                                                                                  \
    ((count) < (cap) || ((cap) = (cap) ? (cap) * 2 : 64, (arr) = realloc((arr), sizeof(*(arr)) * (size_t)(cap))) != NULL)

/* A video sector's STR header (0: not a video sector, or unreadable). */
typedef struct {
    int frame, sector, sectors, width, height;
} StrHead;

static int strHead(CdDisc* image, int lba, StrHead* h) {
    uint8_t raw[CD_RAW_SECTOR];
    if (!discRead(image, lba, raw)) {
        return 0;
    }
    const uint8_t* sub = raw + 16;
    const uint8_t* u = raw + 24;
    if (raw[15] != 2 || (sub[2] & 0x04) || rd16(u) != 0x0160 || rd16(u + 2) != 0x8001) {
        return 0; /* not Mode 2, an audio sector, or not an STR video chunk */
    }
    h->sector = rd16(u + 4);
    h->sectors = rd16(u + 6);
    h->frame = (int)rd32(u + 8);
    h->width = rd16(u + 16);
    h->height = rd16(u + 18);
    return 1;
}

/* The first video sector at or after `at` (within `limit` sectors), -1. */
static int nextVideo(CdDisc* image, int lba, int at, int end, StrHead* h) {
    for (int o = at; o < end && o < at + 32; o++) {
        if (strHead(image, lba + o, h)) {
            return o;
        }
    }
    return -1;
}

/* The frame of the last video sector before `at` (back to `from`), -1. A
 * stream can end with some seconds of sound alone. */
static int lastFrameBefore(CdDisc* image, int lba, int from, int at) {
    StrHead h;
    for (int q = at - 1; q >= from && q >= at - 2048; q--) {
        if (strHead(image, lba + q, &h)) {
            return h.frame;
        }
    }
    return -1;
}

/* The movies of stream s: sampled every SAMPLE_STEP sectors; where the frame
 * number drops, a binary search finds the first sector of the next movie
 * (the first sector whose next video sector has a lower frame than the
 * last sample's: true from the boundary on, false before it). */
static void scanStream(XgGalleryScan* sc, int discIndex, int s) {
    Disc* d = &sc->discs[discIndex];
    int lba = 0, size = 0, group = 0;
    int found = xgDiscGameLookup(d->disc, MOVIE_DIR, s + 2, &lba, &size, &group);
    if (getenv("XG_GALLERY_LOG") != NULL) {
        fprintf(stderr, "gallery: disc %d stream %d: lookup %d lba %d size %d\n", discIndex + 1, s, found, lba, size);
    }
    if (!found || size <= 0) {
        return;
    }
    int end = size / FORM2_SIZE;
    StrHead h;
    int start = nextVideo(d->image, lba, 0, end, &h);
    if (start < 0) {
        return;
    }
    int width = h.width, height = h.height;
    int prevAt = start, prevFrame = h.frame;
    while (start >= 0) {
        int boundary = -1;
        for (int o = prevAt + SAMPLE_STEP; o < end; o += SAMPLE_STEP) {
            int v = nextVideo(d->image, lba, o, end, &h);
            if (v < 0) {
                break;
            }
            if (h.frame >= prevFrame) {
                prevAt = v;
                prevFrame = h.frame;
                continue;
            }
            int lo = prevAt, hi = v;
            while (hi - lo > 1) {
                int mid = lo + (hi - lo) / 2;
                StrHead m;
                int mv = nextVideo(d->image, lba, mid, end, &m);
                if (mv >= 0 && m.frame < prevFrame) {
                    hi = mid;
                } else {
                    lo = mid;
                }
            }
            boundary = nextVideo(d->image, lba, hi, end, &h);
            break;
        }
        int stop = boundary >= 0 ? boundary : end;
        int last = lastFrameBefore(d->image, lba, start, stop);
        if (last > 0 && GROW(sc->movies, sc->movieCount, sc->movieCap)) {
            XgMovieEntry* m = &sc->movies[sc->movieCount++];
            m->disc = discIndex + 1;
            m->stream = s;
            m->lba = lba;
            m->offset = start;
            m->sectors = stop - start;
            m->frames = last;
            m->width = width;
            m->height = height;
        }
        if (boundary < 0) {
            break;
        }
        start = prevAt = boundary;
        prevFrame = h.frame;
        width = h.width;
        height = h.height;
    }
}

/* The movie of a disc's stream holding sector `offset`, -1. */
static int movieAt(const XgGalleryScan* sc, int disc, int stream, int offset) {
    for (int i = 0; i < sc->movieCount; i++) {
        const XgMovieEntry* m = &sc->movies[i];
        if (m->disc == disc && m->stream == stream && offset >= m->offset && offset < m->offset + m->sectors) {
            return i;
        }
    }
    return -1;
}

/* Does the op's movie play from this disc: a movie holds its first frame at
 * its sector, and its last frame. */
static int checkUse(XgGalleryScan* sc, int disc, const XgMovieUse* u) {
    if (sc->discs[disc - 1].disc == NULL) {
        return -1;
    }
    int i = movieAt(sc, disc, u->stream, u->offset);
    if (i < 0) {
        return -1;
    }
    const XgMovieEntry* m = &sc->movies[i];
    StrHead h;
    if (!strHead(sc->discs[disc - 1].image, m->lba + u->offset, &h) || h.frame != u->first || h.sector != 0 ||
        u->last > m->frames || u->last < u->first) {
        return -1;
    }
    return i;
}

/* Validate only the requested stream on the exact image, without indexing
 * every field or choosing the other disc when a reference fails here. */
int xgFieldMediaMovie(XgDisc* disc, const XgFieldMedia* r, XgMovieEntry* out) {
    if (!disc || !r || r->kind != XG_MEDIA_MOVIE || r->problem || r->stream < 0 || r->offset < 0) return 0;
    int n = xgDiscNumber(disc);
    if (n < 1 || n > 2 || xgDiscFileEntry(disc, MOVIE_DIR, r->stream + 2) < 0) return 0;
    XgGalleryScan sc = {0};
    sc.discs[n - 1].disc = disc;
    sc.discs[n - 1].image = discOpen(xgDiscPath(disc));
    if (!sc.discs[n - 1].image) return 0;
    scanStream(&sc, n - 1, r->stream);
    XgMovieUse u = {.stream = r->stream, .offset = r->offset, .first = r->first, .last = r->last};
    int i = checkUse(&sc, n, &u);
    if (i >= 0) *out = sc.movies[i];
    discClose(sc.discs[n - 1].image); free(sc.movies);
    return i >= 0;
}

static void addUse(XgGalleryScan* sc, XgMovieUse u, int realOn2) {
    int on1 = checkUse(sc, 1, &u), on2 = checkUse(sc, 2, &u);
    if (on1 < 0 && on2 < 0) {
        return;
    }
    /* Both: a field disc 2 holds plays disc 2's. */
    int use2 = on2 >= 0 && (on1 < 0 || realOn2);
    u.disc = use2 ? 2 : 1;
    u.movie = use2 ? on2 : on1;
    for (int i = 0; i < sc->useCount; i++) {
        XgMovieUse* o = &sc->uses[i];
        if (o->disc == u.disc && o->field == u.field && o->event == u.event && o->stream == u.stream && o->offset == u.offset &&
            o->first == u.first && o->last == u.last && o->pc == u.pc) {
            return; /* the same op in another routine */
        }
    }
    if (GROW(sc->uses, sc->useCount, sc->useCap)) {
        sc->uses[sc->useCount++] = u;
    }
}

typedef struct {
    XgGalleryScan* scan;
    int disc, event;
} BattleUse;

static void addBattleUse(void* context, int pc, const int args[4]) {
    BattleUse* b = context;
    /* OP27 assigns byte-sized type/stream/fade, starts at sector0/frame1,
     * and supplies the requested last frame to the movie-mode player. */
    XgMovieUse u = {.disc = b->disc + 1, .stream = args[1] & 255, .first = 1,
                   .last = args[3], .field = -2, .op = 0x27, .mode = (args[0] & 255) | 0x80,
                   .movie = -1, .event = b->event, .pc = pc};
    if ((u.mode & 0x7f) > 2) return;
    u.movie = checkUse(b->scan, u.disc, &u);
    if (u.movie >= 0 && GROW(b->scan->uses, b->scan->useCount, b->scan->useCap)) {
        b->scan->uses[b->scan->useCount++] = u;
    }
}

static void scanBattle(XgGalleryScan* sc, int disc, int event) {
    Disc* d = &sc->discs[disc];
    int memberSize;
    int at = xgBundleMember(d->events, d->eventBytes, &d->eventBundle, event * 2, &memberSize);
    if (at < 0 || memberSize < 4) return;
    /* The game's decoder reads through a member boundary for some scripts
     * (US event0 is one). Retain the next 16 loaded bytes and pad the local
     * buffer for the decoder's final group. Its stream check also needs a
     * member-sized input, rather than all subsequent bundle members. */
    int inputSize = memberSize + 16;
    if (inputSize > d->eventBytes - at) inputSize = d->eventBytes - at;
    uint8_t* input = calloc((size_t)inputSize + 16, 1);
    if (input == NULL) return;
    memcpy(input, d->events + at, (size_t)inputSize);
    int size = 0;
    uint8_t* script = xgLzssDecompress(input, inputSize, &size);
    free(input);
    if (script != NULL) {
        BattleUse b = {sc, disc, event};
        xgBattleMovies(script, size, addBattleUse, &b);
        free(script);
    }
}

/* An immediate u16 argument (bit 15 set; else a variable), -1. */
static int immediate(const uint8_t* p) {
    int v = rd16(p);
    return v & 0x8000 ? v & 0x7FFF : -1;
}

/* The routine (actor, slot) whose entry point is the last one at or before pc. */
static void routineOf(const XgFieldScript* fs, int pc, int* actor, int* slot) {
    int best = -1;
    *actor = *slot = -1;
    for (int a = 0; a < fs->actors; a++) {
        for (int k = 0; k < 32; k++) {
            int e = fs->routines[a][k];
            if (e <= pc && e > best && e < fs->codeSize) {
                best = e;
                *actor = a;
                *slot = k;
            }
        }
    }
}

/* The code a routine runs from `from` (along its branches, a few hundred
 * instructions at most, not past `to`): a JUMP_IF testing variable 2 (the
 * entrance) for equality with a value gives *entry, an IF_GAMEPROGRESS_EQUAL
 * gives *progress. */
static void guards(const XgFieldScript* fs, const uint8_t* marks, int from, int to, int* entry, int* progress) {
    int todo[32], pending = 0, visited[256], nv = 0;
    todo[pending++] = from;
    while (pending > 0) {
        int pc = todo[--pending];
        while (pc >= 0 && pc < fs->codeSize && pc < to && marks[pc] != 0 && nv < 256) {
            int again = 0;
            for (int k = 0; k < nv && !again; k++) {
                again = visited[k] == pc;
            }
            if (again) {
                break;
            }
            visited[nv++] = pc;
            const uint8_t* c = fs->code + pc;
            if (c[0] == 0x02 && rd16(c + 1) == 2 && (c[5] & 0xF0) == 0x40 && (c[5] & 0x0F) == 0 && *entry < 0) {
                *entry = (int16_t)rd16(c + 3);
            } else if (c[0] == 0x86 && *progress < 0) {
                *progress = immediate(c + 1);
            }
            int ext = c[0] == 0xFE;
            int at = xgFieldJumpOffset(ext ? c[1] : c[0], ext);
            if (!ext && c[0] == 0x01) {
                pc = rd16(c + 1);
                continue;
            }
            if (at != 0 && (ext || c[0] != 0x05) && pending < 32) {
                todo[pending++] = rd16(c + at); /* a branch's other way */
            }
            if (!ext && xgFieldEndsFlow(c[0])) {
                break;
            }
            pc += marks[pc];
        }
    }
}

static void scanField(XgGalleryScan* sc, int discIndex, int field) {
    Disc* d = &sc->discs[discIndex];
    XgFieldScript fs;
    if (!xgFieldScriptRead(d->disc, field, &fs)) {
        return;
    }
    uint8_t* marks = xgFieldScriptWalk(&fs);
    int realOn2 = sc->discs[1].real != NULL && field < sc->discs[1].fields && sc->discs[1].real[field];
    XgFieldMedia* refs = NULL;
    int count = xgFieldMediaDecode(&fs, field, &refs);
    for (int k = 0; k < count; ++k) {
        const XgFieldMedia* r = refs + k;
        if (r->problem) continue;
        if (r->kind == XG_MEDIA_MOVIE) {
            XgMovieUse u = {.stream = r->stream, .offset = r->offset, .first = r->first, .last = r->last,
                .field = field, .op = r->op, .mode = r->mode, .movie = -1, .event = -1, .pc = r->pc};
            addUse(sc, u, realOn2);
        } else if (sc->pictureField[r->picture] == 0) sc->pictureField[r->picture] = field + 1;
    }
    free(refs);
    for (int pc = 0; marks != NULL && pc < fs.codeSize; pc++) {
        if (marks[pc] == 0) continue;
        const uint8_t* c = fs.code + pc;
        if (c[0] == 0x87 && discIndex == 0) {
            /* The scenes come from disc 1, which holds every field. */
            int v = immediate(c + 1);
            if (v < 0 || !GROW(sc->sets, sc->setCount, sc->setCap)) {
                continue;
            }
            SetOp* s = &sc->sets[sc->setCount++];
            s->field = field;
            s->setsTo = v;
            s->pc = pc;
            s->entry = -1;
            s->progressHint = -1;
            int actor, slot;
            routineOf(&fs, pc, &actor, &slot);
            s->actor = actor;
            s->slot = slot;
            s->start = actor >= 0 ? fs.routines[actor][slot] : -1;
            if (actor >= 0) {
                guards(&fs, marks, fs.routines[actor][slot], pc, &s->entry, &s->progressHint);
                if (slot != 0) {
                    guards(&fs, marks, fs.routines[actor][0], fs.codeSize, &s->entry, &s->progressHint);
                }
            }
        }
    }
    free(marks);
    xgFieldScriptFree(&fs);
}

static int compareScenes(const void* a, const void* b) {
    const XgScene* x = a;
    const XgScene* y = b;
    if (x->setsTo != y->setsTo) {
        return x->setsTo - y->setsTo;
    }
    return x->field - y->field;
}

static int compareInts(const void* a, const void* b) {
    return *(const int*)a - *(const int*)b;
}

/* The debug rooms (US 720-728) set the story to many values, one after
 * another at the player's choice: a field that sets eight or more is one. */
#define DEBUG_ROOM_SETS 8

static int distinctSets(const XgGalleryScan* sc, int field) {
    int n = 0;
    for (int i = 0; i < sc->setCount; i++) {
        if (sc->sets[i].field != field) {
            continue;
        }
        int seen = 0;
        for (int k = 0; k < i && !seen; k++) {
            seen = sc->sets[k].field == field && sc->sets[k].setsTo == sc->sets[i].setsTo;
        }
        n += !seen;
    }
    return n;
}

static void buildScenes(XgGalleryScan* sc) {
    /* Leave out the debug rooms. */
    uint8_t* debug = calloc((size_t)sc->setCount + 1, 1);
    for (int i = 0; debug != NULL && i < sc->setCount; i++) {
        debug[i] = distinctSets(sc, sc->sets[i].field) >= DEBUG_ROOM_SETS;
    }
    int kept = 0;
    for (int i = 0; debug != NULL && i < sc->setCount; i++) {
        if (!debug[i]) {
            sc->sets[kept++] = sc->sets[i];
        }
    }
    sc->setCount = debug != NULL ? kept : sc->setCount;
    free(debug);
    /* The story's progress values, in order. */
    int* values = malloc(sizeof(int) * (size_t)(sc->setCount + 1));
    int nv = 0;
    for (int i = 0; values != NULL && i < sc->setCount; i++) {
        values[nv++] = sc->sets[i].setsTo;
    }
    if (values != NULL) {
        qsort(values, (size_t)nv, sizeof(int), compareInts);
    }
    /* The last value set in a field disc 2 does not hold: past it, the
     * story is on disc 2. */
    int disc2From = 0x7FFF;
    if (sc->discs[1].real != NULL) {
        disc2From = -1;
        for (int i = 0; i < sc->setCount; i++) {
            int f = sc->sets[i].field;
            if ((f >= sc->discs[1].fields || !sc->discs[1].real[f]) && sc->sets[i].setsTo > disc2From) {
                disc2From = sc->sets[i].setsTo;
            }
        }
    }
    sc->scenes = calloc((size_t)sc->setCount + 1, sizeof(XgScene));
    sc->sceneCount = 0;
    for (int i = 0; sc->scenes != NULL && i < sc->setCount; i++) {
        const SetOp* s = &sc->sets[i];
        int dup = 0;
        for (int k = 0; k < sc->sceneCount && !dup; k++) {
            dup = sc->scenes[k].field == s->field && sc->scenes[k].setsTo == s->setsTo;
        }
        if (dup) {
            continue;
        }
        XgScene* o = &sc->scenes[sc->sceneCount++];
        o->field = s->field;
        o->setsTo = s->setsTo;
        int prev = s->setsTo > 0 ? s->setsTo - 1 : 0;
        for (int k = nv - 1; k >= 0; k--) {
            if (values[k] < s->setsTo) {
                prev = values[k];
                break;
            }
        }
        o->progress = s->progressHint >= 0 && s->progressHint < s->setsTo ? s->progressHint : prev;
        o->entry = s->entry >= 0 ? s->entry : 0;
        o->disc = sc->discs[1].real != NULL && s->field < sc->discs[1].fields && sc->discs[1].real[s->field] &&
                          s->setsTo > disc2From
                      ? 2
                      : 1;
        o->actor = s->actor;
        o->slot = s->slot;
        o->start = s->start;
        o->pc = s->pc;
        o->use = -1;
        for (int u = 0; u < sc->useCount; u++) {
            if (sc->uses[u].field == s->field && sc->uses[u].disc == o->disc) {
                o->use = u;
                break;
            }
        }
    }
    if (sc->scenes != NULL) {
        qsort(sc->scenes, (size_t)sc->sceneCount, sizeof(XgScene), compareScenes);
    }
    free(values);
}

XgGalleryScan* xgScanBegin(XgDisc* disc1, XgDisc* disc2) {
    XgGalleryScan* sc = calloc(1, sizeof(XgGalleryScan));
    if (sc == NULL) {
        return NULL;
    }
    XgDisc* in[2] = {disc1, disc2};
    for (int k = 0; k < 2; k++) {
        Disc* d = &sc->discs[k];
        d->disc = in[k];
        if (d->disc == NULL) {
            continue;
        }
        d->image = discOpen(xgDiscPath(d->disc));
        int lba, size;
        if (!xgDiscGameLookup(d->disc, MOVIE_DIR, 1, &lba, &size, &d->streams)) {
            d->streams = 0;
        }
        while (1) {
            int index = xgDiscFileEntry(d->disc, FIELD_DIR, FIELD_FILE(d->fields));
            if (index < 0 || xgDiscEntry(d->disc, index).size <= 0) {
                break;
            }
            d->fields++;
        }
        d->real = calloc((size_t)d->fields + 1, 1);
        for (int f = 0; d->real != NULL && f < d->fields; f++) {
            XgEntry e = xgDiscEntry(d->disc, xgDiscFileEntry(d->disc, FIELD_DIR, FIELD_FILE(f)));
            d->real[f] = e.size >= FIELD_MIN_SIZE;
        }
        if (d->image == NULL) {
            d->streams = 0;
        }
        int eventFile = xgDiscFileEntry(d->disc, 0x20, 2);
        if (eventFile >= 0) d->events = xgDiscReadEntry(d->disc, eventFile, &d->eventBytes);
        if (d->events != NULL && xgBundleParse(d->events, d->eventBytes, &d->eventBundle) &&
            (d->eventBundle.count & 1) == 0) {
            d->eventCount = d->eventBundle.count / 2;
        }
    }
    /* The kernel's opening: the movie mode's first request (kernelMain). */
    sc->phase = PHASE_STREAMS;
    return sc;
}

void xgScanFree(XgGalleryScan* sc) {
    if (sc == NULL) {
        return;
    }
    for (int k = 0; k < 2; k++) {
        discClose(sc->discs[k].image);
        free(sc->discs[k].real);
        free(sc->discs[k].events);
    }
    free(sc->movies);
    free(sc->uses);
    free(sc->sets);
    free(sc->scenes);
    free(sc);
}

static void addKernelUses(XgGalleryScan* sc) {
    /* kernelMain's request: movie 0x10 on disc 1, 7 on disc 2, type 1 (frames
     * 1 to 233: moviePlaybackLoop's end frame without bit 7, 0xE9). */
    static const int kernelMovie[2] = {0x10, 7};
    for (int k = 0; k < 2; k++) {
        XgMovieUse u = {k + 1, kernelMovie[k], 0, 1, 0xE9, -1, 0, 1, -1, -1, -1};
        u.movie = checkUse(sc, k + 1, &u);
        if (u.movie >= 0 && GROW(sc->uses, sc->useCount, sc->useCap)) {
            sc->uses[sc->useCount++] = u;
        }
    }
}

int xgScanStep(XgGalleryScan* sc, int budget) {
    uint64_t until = SDL_GetTicks() + (uint64_t)(budget > 0 ? budget : 1);
    while (sc->phase != PHASE_DONE && SDL_GetTicks() < until) {
        Disc* d = &sc->discs[sc->disc];
        if (sc->phase == PHASE_STREAMS) {
            if (d->disc != NULL && sc->item < d->streams) {
                snprintf(sc->doing, sizeof(sc->doing), "Disc %d: movie stream %d", sc->disc + 1, sc->item);
                scanStream(sc, sc->disc, sc->item++);
                continue;
            }
            sc->item = 0;
            if (++sc->disc == 2) {
                sc->disc = 0;
                sc->phase = PHASE_FIELDS;
                addKernelUses(sc);
            }
            continue;
        }
        if (sc->phase == PHASE_BATTLE) {
            if (d->disc != NULL && sc->item < d->eventCount) {
                snprintf(sc->doing, sizeof(sc->doing), "Disc %d: battle event %d", sc->disc + 1, sc->item);
                scanBattle(sc, sc->disc, sc->item++);
                continue;
            }
            sc->item = 0;
            if (++sc->disc == 2) {
                buildScenes(sc);
                sc->phase = PHASE_DONE;
                snprintf(sc->doing, sizeof(sc->doing), "Done");
            }
            continue;
        }
        if (d->disc != NULL && sc->item < d->fields) {
            int f = sc->item++;
            /* Disc 1 holds every field; disc 2's own add only its movies. */
            if (d->real != NULL && d->real[f] && (sc->disc == 0 || sc->discs[0].disc == NULL)) {
                snprintf(sc->doing, sizeof(sc->doing), "Disc %d: field %d's script", sc->disc + 1, f);
                scanField(sc, sc->disc, f);
            }
            continue;
        }
        sc->item = 0;
        if (++sc->disc == 2) {
            sc->disc = 0;
            sc->phase = PHASE_BATTLE;
        }
    }
    return sc->phase == PHASE_DONE;
}

double xgScanProgress(const XgGalleryScan* sc) {
    double total = 0, done = 0;
    for (int k = 0; k < 2; k++) {
        const Disc* d = &sc->discs[k];
        total += d->streams * 8.0 + (k == 0 || sc->discs[0].disc == NULL ? d->fields : 0) + d->eventCount;
    }
    for (int k = 0; k < 2; k++) {
        const Disc* d = &sc->discs[k];
        int fields = k == 0 || sc->discs[0].disc == NULL ? d->fields : 0;
        if (sc->phase >= PHASE_BATTLE || (sc->phase == PHASE_FIELDS && k < sc->disc)) {
            done += d->streams * 8.0 + fields;
        } else if (sc->phase == PHASE_FIELDS && k == sc->disc) {
            done += d->streams * 8.0 + (fields > 0 ? sc->item : 0);
        } else if (sc->phase == PHASE_FIELDS) {
            done += d->streams * 8.0;
        } else if (k < sc->disc) {
            done += d->streams * 8.0;
        } else if (k == sc->disc) {
            done += sc->item * 8.0;
        }
        if (sc->phase == PHASE_DONE || (sc->phase == PHASE_BATTLE && k < sc->disc)) done += d->eventCount;
        else if (sc->phase == PHASE_BATTLE && k == sc->disc) done += sc->item;
    }
    return total > 0 ? done / total : 1.0;
}

const char* xgScanDoing(const XgGalleryScan* sc) {
    return sc->doing;
}

int xgScanMovieCount(const XgGalleryScan* sc) {
    return sc->movieCount;
}

const XgMovieEntry* xgScanMovie(const XgGalleryScan* sc, int i) {
    return i >= 0 && i < sc->movieCount ? &sc->movies[i] : NULL;
}

int xgScanUseCount(const XgGalleryScan* sc) {
    return sc->useCount;
}

const XgMovieUse* xgScanUse(const XgGalleryScan* sc, int i) {
    return i >= 0 && i < sc->useCount ? &sc->uses[i] : NULL;
}

int xgScanSceneCount(const XgGalleryScan* sc) {
    return sc->sceneCount;
}

const XgScene* xgScanScene(const XgGalleryScan* sc, int i) {
    return i >= 0 && i < sc->sceneCount ? &sc->scenes[i] : NULL;
}

int xgScanFieldCount(const XgGalleryScan* sc, int disc) {
    return disc >= 1 && disc <= 2 ? sc->discs[disc - 1].fields : 0;
}

int xgScanFieldReal(const XgGalleryScan* sc, int disc, int field) {
    if (disc < 1 || disc > 2) {
        return 0;
    }
    const Disc* d = &sc->discs[disc - 1];
    return d->real != NULL && field >= 0 && field < d->fields && d->real[field];
}

int xgScanPictureField(const XgGalleryScan* sc, int picture) {
    return picture >= 0 && picture < 256 ? sc->pictureField[picture] - 1 : -1;
}

int xgScanSeek(XgGalleryScan* sc, int movie, int frame) {
    const XgMovieEntry* m = xgScanMovie(sc, movie);
    if (m == NULL || frame < 1 || frame > m->frames) {
        return -1;
    }
    CdDisc* image = sc->discs[m->disc - 1].image;
    /* The first video sector whose frame is `frame` or later. */
    int lo = m->offset, hi = m->offset + m->sectors;
    while (lo < hi) {
        int mid = (lo + hi) / 2;
        StrHead h;
        int v = nextVideo(image, m->lba, mid, m->offset + m->sectors, &h);
        if (v < 0 || h.frame >= frame) {
            hi = mid;
        } else {
            lo = v + 1;
        }
    }
    StrHead h;
    int v = nextVideo(image, m->lba, lo, m->offset + m->sectors, &h);
    return v >= 0 && h.frame == frame && h.sector == 0 ? v : -1;
}
