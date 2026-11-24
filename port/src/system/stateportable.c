/*
 * Portable slots (stateportable.h): this build's schema, a slot's trailer at
 * the save, another build's slot converted at the load.
 */

#include "stateportable.h"

#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gamestack.h"
#include "savestate.h"
#include "statecodec.h"
#include "stateschema.h"

#ifdef VERSION_JP
#define SCHEMA_VERSION "jp"
#else
#define SCHEMA_VERSION "us"
#endif

/* gen/modules.c (savestate.c has the same view). */
typedef struct {
    const char* name;
    char* dataStart;
    char* dataEnd;
    char* bssStart;
    char* bssEnd;
} PortModule;
extern PortModule portModules[];
extern const int portModuleCount;
unsigned char* portHeapStart(void);
unsigned char* portHeapEnd(void);
extern unsigned char pcScratchpad[]; /* system/memory.c */
void pcResumeGame(void);             /* port/src/game/main/statetypes.c */

/* This build's schema: read once (the first portable save or conversion), then kept. Written on a
 * save's worker thread or on the main thread with no worker running (savestate.c finishes the write
 * before a conversion), so never by two at once. */
static struct {
    int known;           /* path worked out (statePortableOn, on the main thread) */
    char path[1100];     /* "": none (XG_STATE_SCHEMA=none, no base path) */
    int tried;
    StateSchema* schema; /* NULL: none or unusable (why) */
    uint64_t hash;       /* stateHash of the file's bytes */
    int filed;           /* filed in the save folder (or found there) */
    char why[300];
} own;

static const char* ownPath(void) {
    if (!own.known) {
        own.known = 1;
        const char* env = getenv("XG_STATE_SCHEMA");
        if (env != NULL && env[0] != '\0') {
            snprintf(own.path, sizeof own.path, "%s", strcmp(env, "none") == 0 ? "" : env);
        } else {
            const char* base = SDL_GetBasePath(); /* the executable's folder, with its separator */
            if (base != NULL) {
                snprintf(own.path, sizeof own.path, "%sstate_schema.json", base);
            }
        }
    }
    return own.path;
}

int statePortableOn(void) {
    const char* path = ownPath();
    return stateImageBias() == 0 && path[0] != '\0' && SDL_GetPathInfo(path, NULL);
}

static uint8_t* readAll(const char* path, size_t* n) {
    size_t size = 0;
    void* data = SDL_LoadFile(path, &size);
    if (data == NULL) {
        return NULL;
    }
    uint8_t* copy = malloc(size + 1); /* (SDL's memory freed by SDL: a copy of ours) */
    if (copy != NULL) {
        memcpy(copy, data, size);
        copy[size] = 0;
    }
    SDL_free(data);
    *n = size;
    return copy;
}

/* Whether the schema describes this executable as it runs: its version, its read-only ranges in this
 * image, every game module's sections, the heap, the game stack and the scratchpad where they are,
 * two functions (a game module's, the platform's) where they are; not older than the executable (a
 * rebuild moves what it describes). 0 with why. */
static int describesThis(const StateSchema* s, const char* path, char* why, size_t size) {
    if (strcmp(stateSchemaVersion(s), SCHEMA_VERSION) != 0) {
        snprintf(why, size, "it is the %s version's", stateSchemaVersion(s));
        return 0;
    }
    SDL_PathInfo si, ei;
    const char* exe = stateImagePath();
    if (exe != NULL && SDL_GetPathInfo(path, &si) && SDL_GetPathInfo(exe, &ei) && si.modify_time < ei.modify_time) {
        snprintf(why, size, "it is older than the executable (tools/state_schema.py writes it again after a build)");
        return 0;
    }
    for (int32_t i = 0; i < stateSchemaReadOnlyCount(s); i++) {
        uint64_t lo, hi, fo;
        stateSchemaReadOnlyAt(s, i, &lo, &hi, &fo);
        if (!stateImageReadable(lo, hi)) {
            snprintf(why, size, "its read-only range %llx-%llx is not in this executable", (unsigned long long)lo,
                     (unsigned long long)hi);
            return 0;
        }
    }
    int sections = 0;
    for (int i = 0; i < portModuleCount; i++) {
        const PortModule* m = &portModules[i];
        for (int k = 0; k < 2; k++) {
            char name[96];
            snprintf(name, sizeof name, "%s.%s", m->name, k ? "bss" : "data");
            StRange r = stateSchemaSection(s, name);
            uintptr_t lo = (uintptr_t)(k ? m->bssStart : m->dataStart), hi = (uintptr_t)(k ? m->bssEnd : m->dataEnd);
            if (r.name == NULL) {
                continue; /* (an empty section has none) */
            }
            sections++;
            if (r.lo != lo || r.hi != hi) {
                snprintf(why, size, "its section %s is not where this executable has it", name);
                return 0;
            }
        }
    }
    StRange heap = stateSchemaRegion(s, "heapArena"), stack = stateSchemaRegion(s, "gameStack");
    StRange pad = stateSchemaRegion(s, "pcScratchpad");
    if (sections == 0 || heap.lo != (uintptr_t)portHeapStart() || heap.hi != (uintptr_t)portHeapEnd() ||
        stack.lo != (uintptr_t)gameStackBase() || stack.hi != (uintptr_t)gameStackBase() + gameStackSize() ||
        pad.lo != (uintptr_t)pcScratchpad) {
        snprintf(why, size, "its memory regions are not where this executable has them");
        return 0;
    }
    uint64_t a = 0, b = 0;
    if (!stateSchemaSymbolFind(s, "pcResumeGame", &a) || a != (uintptr_t)&pcResumeGame ||
        !stateSchemaSymbolFind(s, "stateServe", &b) || b != (uintptr_t)&stateServe) {
        snprintf(why, size, "its functions are not where this executable has them");
        return 0;
    }
    return 1;
}

static const StateSchema* ownSchema(char* why, size_t size) {
    if (!own.tried) {
        own.tried = 1;
        const char* path = ownPath();
        size_t n = 0;
        uint8_t* json = path[0] != '\0' ? readAll(path, &n) : NULL;
        char err[300] = "";
        if (stateImageBias() != 0) {
            snprintf(own.why, sizeof own.why,
                     "this build runs away from its link addresses (position-independent): its schema's addresses "
                     "do not apply");
        } else if (json == NULL) {
            snprintf(own.why, sizeof own.why, "this build has no state schema (%.200s)",
                     path[0] != '\0' ? path : "XG_STATE_SCHEMA=none");
        } else {
            uint64_t t0 = SDL_GetTicksNS();
            own.hash = stateHash(json, n, 0);
            own.schema = stateSchemaParse((const char*)json, n, err, sizeof err);
            if (own.schema == NULL) {
                snprintf(own.why, sizeof own.why, "this build's state schema cannot be read: %s", err);
            } else if (!describesThis(own.schema, path, err, sizeof err)) {
                snprintf(own.why, sizeof own.why, "this build's state schema (%.100s) does not describe it: %.140s",
                         path, err);
                stateSchemaFree(own.schema);
                own.schema = NULL;
            } else {
                fprintf(stderr, "state: schema %016llx (%s) read in %.0f ms\n", (unsigned long long)own.hash, path,
                        (double)(SDL_GetTicksNS() - t0) * 1e-6);
            }
        }
        free(json);
        if (own.schema == NULL) {
            fprintf(stderr, "state: portable slots off: %s\n", own.why);
        }
    }
    if (own.schema == NULL) {
        snprintf(why, size, "%s", own.why);
    }
    return own.schema;
}

/* The read-only ranges as this process has them (link addresses: no bias). */
static StateRoStrings* liveStrings(const StateSchema* s, StateRoSegment** segments) {
    int32_t n = stateSchemaReadOnlyCount(s);
    StateRoSegment* seg = calloc((size_t)n + 1, sizeof(*seg));
    if (seg == NULL) {
        return NULL;
    }
    for (int32_t i = 0; i < n; i++) {
        uint64_t lo, hi, fo;
        stateSchemaReadOnlyAt(s, i, &lo, &hi, &fo);
        seg[i].lo = lo;
        seg[i].hi = hi;
        seg[i].bytes = (const uint8_t*)(uintptr_t)lo; /* (describesThis: in this image) */
        seg[i].size = (size_t)(hi - lo);
    }
    *segments = seg;
    StateRoStrings* r = stateRoStringsNew(seg, n);
    if (r == NULL) {
        free(seg);
    }
    return r;
}

/* ---- the save -------------------------------------------------------------------------- */

typedef struct {
    StateStrings live;
    StatePortable* table;
    int full; /* out of memory */
} Recorder;

/* The writer's `at` at a save: this process's string, kept in the table. */
static int recordAt(void* user, uint64_t addr, const uint8_t** text, size_t* len) {
    Recorder* r = user;
    if (!r->live.at(r->live.user, addr, text, len)) {
        return 0;
    }
    if (!statePortableString(r->table, addr, *text, *len)) {
        r->full = 1;
    }
    return 1;
}

/* This build's schema in the save folder, once: its bytes read again and checked against the hash. */
static int fileSchema(char* why, size_t size) {
    char path[1100];
    if (own.filed) {
        return 1;
    }
    if (!stateSchemaFilePath(own.hash, path, sizeof path)) {
        snprintf(why, size, "no folder for save states");
        return 0;
    }
    if (SDL_GetPathInfo(path, NULL)) {
        own.filed = 1;
        return 1;
    }
    size_t n = 0;
    uint8_t* json = readAll(ownPath(), &n);
    int ok = json != NULL && stateHash(json, n, 0) == own.hash;
    if (!ok) {
        snprintf(why, size, "%s changed since it was read", ownPath());
    } else {
        ok = stateSchemaFileWrite(path, json, n, why, size);
    }
    free(json);
    if (ok) {
        own.filed = 1;
        fprintf(stderr, "state: schema filed as %s\n", path);
    }
    return ok;
}

int statePortableMake(const uint8_t* body, size_t size, StatePortable* out, char* why, size_t whySize) {
    memset(out, 0, sizeof(*out));
    const StateSchema* s = ownSchema(why, whySize);
    if (s == NULL) {
        return 0;
    }
    StateRoSegment* seg = NULL;
    StateRoStrings* live = liveStrings(s, &seg);
    if (live == NULL) {
        snprintf(why, whySize, "out of memory");
        return 0;
    }
    uint64_t t0 = SDL_GetTicksNS();
    Recorder rec = {stateRoStrings(live), out, 0};
    StateConvertSide w = {body, size, s, {&rec, recordAt, NULL}};
    StateConvertResult r;
    char err[300];
    int ok = stateConvertEncode(&w, &r, err, sizeof err);
    stateRoStringsFree(live);
    free(seg);
    if (!ok) {
        snprintf(why, whySize, "its encoding failed: %s", err);
    } else if (rec.full) {
        snprintf(why, whySize, "out of memory");
        ok = 0;
    } else if (r.safePoint == 0) {
        snprintf(why, whySize, "it was not saved at a safe point");
        ok = 0;
    }
    for (int32_t i = 0; ok && i < r.encodeWarningCount; i++) {
        if (strstr(r.encodeWarnings[i].text, "not canonical") != NULL) { /* (pcStateRanges full) */
            snprintf(why, whySize, "%s", r.encodeWarnings[i].text);
            ok = 0;
        }
    }
    if (ok) {
        statePortableSortStrings(out);
        out->schemaHash = own.hash;
        out->safePoint = (int32_t)r.safePoint;
        ok = fileSchema(why, whySize);
    }
    if (ok) {
        size_t bytes = 0;
        for (uint32_t i = 0; i < out->stringCount; i++) bytes += out->strings[i].len;
        fprintf(stderr, "state: portable: safe point 0x%x, schema %016llx, %u strings (%zu bytes), %.0f ms\n",
                (unsigned)out->safePoint, (unsigned long long)out->schemaHash, out->stringCount, bytes,
                (double)(SDL_GetTicksNS() - t0) * 1e-6);
    } else {
        statePortableFree(out);
    }
    stateConvertResultFree(&r);
    return ok;
}

/* ---- the load -------------------------------------------------------------------------- */

void statePortableWriter(const StateHeader* h, char* out, size_t size) {
    if (h->appVersion[0] != '\0' && stateAppVersion()[0] != '\0' && strcmp(h->appVersion, stateAppVersion()) != 0) {
        snprintf(out, size, "version %s", h->appVersion);
    } else {
        snprintf(out, size, "another build of the port");
    }
}

int statePortableCanLoad(const StateHeader* h, const char* path, char* why, size_t whySize) {
    char writer[64], err[200], schema[1100];
    statePortableWriter(h, writer, sizeof writer);
    StatePortable p;
    if (!(h->flags & STATE_FLAG_PORTABLE)) {
        snprintf(why, whySize, "saved by %s, which this one cannot read: it was saved during a movie, a prompt or a "
                 "card access, or by a build that cannot convert", writer);
        return 0;
    }
    if (!stateFileReadPortable(path, &p, err, sizeof err)) {
        snprintf(why, whySize, "saved by %s, which this one cannot read: %s", writer, err);
        return 0;
    }
    uint64_t hash = p.schemaHash;
    statePortableFree(&p);
    if (h->imageBias != 0 || stateImageBias() != 0) {
        snprintf(why, whySize, "saved by %s: converting needs both builds at their link addresses (a "
                 "position-independent build cannot convert yet)", writer);
        return 0;
    }
    if (!statePortableOn()) {
        snprintf(why, whySize, "saved by %s: this build has no state schema, so it cannot convert states", writer);
        return 0;
    }
    if (!stateSchemaFilePath(hash, schema, sizeof schema) || !SDL_GetPathInfo(schema, NULL)) {
        snprintf(why, whySize, "saved by %s: the schema of that version is missing from the save folder "
                 "(schemas/%016llx.xgschema)", writer, (unsigned long long)hash);
        return 0;
    }
    return 1;
}

static void logWarnings(const char* side, const StateConvertWarning* w, int32_t n) {
    for (int32_t i = 0; i < n; i++) {
        fprintf(stderr, "state:   %s %lld %s\n", side, (long long)w[i].count, w[i].text);
    }
}

int statePortableConvert(const StateHeader* h, const uint8_t* body, const char* path, const uint8_t* now,
                         size_t nowSize, StateConvertResult* out, char* why, size_t whySize) {
    char writer[64], err[300], schemaPath[1100];
    memset(out, 0, sizeof(*out));
    if (!statePortableCanLoad(h, path, why, whySize)) {
        return 0;
    }
    statePortableWriter(h, writer, sizeof writer);
    const StateSchema* mine = ownSchema(err, sizeof err);
    if (mine == NULL) {
        snprintf(why, whySize, "saved by %s: %s", writer, err);
        return 0;
    }
    uint64_t t0 = SDL_GetTicksNS();
    StatePortable p;
    uint8_t* json = NULL;
    size_t n = 0;
    StateSchema* theirs = NULL;
    if (!stateFileReadPortable(path, &p, err, sizeof err)) {
        snprintf(why, whySize, "saved by %s, which this one cannot read: %s", writer, err);
        return 0;
    }
    stateSchemaFilePath(p.schemaHash, schemaPath, sizeof schemaPath);
    if (!stateSchemaFileRead(schemaPath, &json, &n, err, sizeof err) || stateHash(json, n, 0) != p.schemaHash) {
        snprintf(why, whySize, "saved by %s: its schema in the save folder (%s) is damaged", writer, schemaPath);
    } else if ((theirs = stateSchemaParse((const char*)json, n, err, sizeof err)) == NULL) {
        snprintf(why, whySize, "saved by %s: its schema cannot be read: %s", writer, err);
    } else if (strcmp(stateSchemaVersion(theirs), SCHEMA_VERSION) != 0) {
        snprintf(why, whySize, "saved by %s: its schema is the %s version's", writer, stateSchemaVersion(theirs));
        stateSchemaFree(theirs);
        theirs = NULL;
    }
    free(json);
    uint8_t* tmpl = NULL;
    size_t tmplSize = 0;
    StateRoSegment* seg = NULL;
    StateRoStrings* live = NULL;
    int ok = 0;
    if (theirs != NULL && !stateConvertTemplate(now, nowSize, mine, body, (size_t)h->bodySize, &tmpl, &tmplSize, err,
                                                 sizeof err)) {
        snprintf(why, whySize, "saved by %s, and this build's own state is damaged: %s", writer, err);
    } else if (theirs != NULL && (live = liveStrings(mine, &seg)) == NULL) {
        snprintf(why, whySize, "out of memory");
    } else if (theirs != NULL) {
        StateConvertSide w = {body, (size_t)h->bodySize, theirs, {&p, statePortableStringAt, NULL}};
        StateConvertSide r = {tmpl, tmplSize, mine, stateRoStrings(live)};
        ok = stateConvert(&w, &r, 1, out, err, sizeof err);
        if (!ok) {
            snprintf(why, whySize, "saved by %s, and converting it failed: %s", writer, err);
        }
    }
    if (ok) {
        fprintf(stderr,
                "state: converting %s: saved by build %02x%02x%02x%02x... (schema %016llx, safe point 0x%x, "
                "%u strings) in %.0f ms: %d encode and %d decode warnings\n",
                path, h->buildId[0], h->buildId[1], h->buildId[2], h->buildId[3], (unsigned long long)p.schemaHash,
                (unsigned)p.safePoint, p.stringCount, (double)(SDL_GetTicksNS() - t0) * 1e-6,
                (int)out->encodeWarningCount, (int)out->decodeWarningCount);
        logWarnings("encode", out->encodeWarnings, out->encodeWarningCount);
        logWarnings("decode", out->decodeWarnings, out->decodeWarningCount);
    }
    stateRoStringsFree(live);
    free(seg);
    free(tmpl);
    stateSchemaFree(theirs);
    statePortableFree(&p);
    return ok;
}
