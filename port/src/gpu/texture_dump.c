/* Opt-in texture discovery. Native pixels are observed before the current
 * primitive is queued; neither the packets nor either draw path is changed. */
#include <SDL3/SDL.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pngfast.h"
#include "texture.h"
#include "system/settings.h"

/* Distinct observations per run (a hash set of DUMP_SLOTS keeps the check
 * cheap); stable regions make a field one texture per page, but a long tour
 * still meets many sprite frames and dialogue lines. */
#define DUMP_LIMIT 65536
#define DUMP_SLOTS (DUMP_LIMIT * 2)
#define GLYPH_LIMIT 4096
typedef struct { uint64_t texel, palette; TextureRequest request; } Seen;
typedef struct { uint64_t key; int limit; } GlyphSeen;
/* A field page's sampled rectangles (samples.jsonl): where its polygons read
 * it, so a repaint can tell the atlas's tiles apart. */
#define SAMPLE_SLOTS 131072
typedef struct { uint64_t texel, palette; int16_t u, v, w, h; } SampleSeen;
static struct {
    char* dir;
    SDL_IOStream* log;
    SDL_IOStream* sampleLog;
    SampleSeen* samples; /* SAMPLE_SLOTS, texel 0 empty */
    SDL_IOStream* glyphLog;
    Seen* seen;      /* DUMP_LIMIT, allocated while dumping */
    int32_t* slots;  /* DUMP_SLOTS indexes into seen, -1 empty */
    GlyphSeen glyphSeen[GLYPH_LIMIT];
    int glyphCount;
    int count, mode;
    uint32_t vblank, from;
} dump;
static char selected[3072], status[256];
static int selectedLoaded;

/* The game supplies its field ID; standalone GPU tests have no field. */
__attribute__((weak)) int gpuTextureField(void) { return -1; }

static void stop(const char* reason) {
    snprintf(status,sizeof(status),"%s%s%.160s",reason,SDL_GetError()[0]?": ":"",SDL_GetError());
    gpuLog("texture dump: %s%s%s", reason, SDL_GetError()[0] ? ": " : "", SDL_GetError());
    if (dump.log) SDL_CloseIO(dump.log);
    if (dump.glyphLog) SDL_CloseIO(dump.glyphLog);
    if (dump.sampleLog) SDL_CloseIO(dump.sampleLog);
    dump.glyphLog=NULL;
    dump.sampleLog=NULL;
    dump.log = NULL;
}

void textureDumpShutdown(void) {
    if (dump.dir) {
        TextureStats stats;
        textureCacheStats(&stats);
        gpuLog("texture dump: %d observations, %" PRIu64 " decodes, %" PRIu64 " hits, %" PRIu64
               " evictions, %" PRIu64 " rendered skips, %zu decoded bytes", dump.count,
               stats.decodes, stats.hits, stats.evictions, stats.renderedSkips, stats.bytes);
    }
    if (dump.log) SDL_CloseIO(dump.log);
    if (dump.glyphLog) SDL_CloseIO(dump.glyphLog);
    if (dump.sampleLog) SDL_CloseIO(dump.sampleLog);
    free(dump.dir);
    free(dump.samples);
    free(dump.seen);
    free(dump.slots);
    memset(&dump, 0, sizeof(dump));
}

void textureDumpInit(void) {
    int mode=dump.mode; uint32_t vblank=dump.vblank;
    textureDumpShutdown();
    dump.mode=mode; dump.vblank=vblank;
    const char* base=textureDumpRoot();
    snprintf(status,sizeof(status),"Disabled");
    if (!base || !*base || !SDL_strcasecmp(base,"none")) return;
    settingsDescribe("texture_dump_from","Texture discovery records nothing before this vblank (0, the default: from the start)");
    const char* from=settingsGet("texture_dump_from");
    dump.from=from?(uint32_t)strtoul(from,NULL,10):0;
    dump.seen=malloc(sizeof(Seen)*DUMP_LIMIT);
    dump.slots=malloc(sizeof(int32_t)*DUMP_SLOTS);
    if (!dump.seen || !dump.slots) { SDL_ClearError(); stop("cannot allocate the observation table"); return; }
    memset(dump.slots,0xff,sizeof(int32_t)*DUMP_SLOTS);
#if defined(VERSION_JP)
    const char* boot = "SLPS-01160";
#else
    const char* boot = "SLUS-00664";
#endif
    if (strlen(base) > 3000) { SDL_ClearError(); stop("directory path too long"); return; }
    dump.dir = malloc(strlen(base) + 32);
    if (!dump.dir) return;
    sprintf(dump.dir, "%s/%s", base, boot);
    if (!SDL_CreateDirectory(dump.dir)) { stop("cannot create directory"); return; }
    char path[3100];
    snprintf(path, sizeof(path), "%s/observations.jsonl", dump.dir);
    dump.log = SDL_IOFromFile(path, "ab");
    if (!dump.log) { stop("cannot open observations"); return; }
    snprintf(path, sizeof(path), "%s/samples.jsonl", dump.dir);
    dump.samples = calloc(SAMPLE_SLOTS, sizeof(SampleSeen));
    dump.sampleLog = dump.samples ? SDL_IOFromFile(path, "ab") : NULL; /* optional: observations go on without it */
    if (dump.from) gpuLog("texture dump: %s (at most %d distinct observations per run, from vblank %u)", dump.dir, DUMP_LIMIT, dump.from);
    else gpuLog("texture dump: %s (at most %d distinct observations per run)", dump.dir, DUMP_LIMIT);
}

const char* textureDumpRoot(void) {
    if (!selectedLoaded) {
        selectedLoaded=1;
        settingsDescribe("texture_dump","Texture discovery root; none disables; XG_TEXDUMP is also accepted");
        const char* s=settingsGet("texture_dump");
        /* Keep the original UTF-8 environment switch as a compatibility alias. */
        if (!s) s=SDL_getenv("XG_TEXDUMP");
        if (s && strlen(s)<sizeof(selected)) snprintf(selected,sizeof(selected),"%s",s);
        else if (s) snprintf(status,sizeof(status),"Directory path too long");
    }
    return selected;
}
void textureDumpSetRoot(const char* s) {
    textureDumpRoot(); if (!s) s="";
    if (strlen(s)>3000) { snprintf(status,sizeof(status),"Directory path too long; previous selection kept"); return; }
    memmove(selected,s,strlen(s)+1);
    settingsSet("texture_dump",selected[0]?selected:"none"); settingsSave(); textureDumpInit();
}
const char* textureDumpStatus(void) {
    if (dump.log && dump.vblank < dump.from)
        snprintf(status,sizeof(status),"Waiting for vblank %u (texture_dump_from)",dump.from);
    else if (dump.log) snprintf(status,sizeof(status),"Dumping: %d texture observations, %d glyphs (limits %d/%d)",dump.count,dump.glyphCount,DUMP_LIMIT,GLYPH_LIMIT);
    return status;
}

int textureDumpEnabled(void) { return dump.log != NULL; }
void textureDumpMode(int mode) { dump.mode = mode; }
void textureDumpVBlank(uint32_t vblank) {
    dump.vblank = vblank;
    if (dump.log && !SDL_FlushIO(dump.log)) stop("cannot flush observations");
    if (dump.glyphLog && !SDL_FlushIO(dump.glyphLog)) stop("cannot flush glyph observations");
}

static int exists(const char* path) {
    SDL_PathInfo info;
    return SDL_GetPathInfo(path, &info) && info.type == SDL_PATHTYPE_FILE;
}

static int binary(const char* name, const void* data, size_t bytes) {
    char path[3100], tmp[3110];
    snprintf(path, sizeof(path), "%s/%s", dump.dir, name);
    if (exists(path)) return 1;
    snprintf(tmp, sizeof(tmp), "%s.tmp", path);
    SDL_IOStream* io = SDL_IOFromFile(tmp, "wb");
    if (!io) return 0;
    int ok = SDL_WriteIO(io, data, bytes) == bytes;
    if (!SDL_CloseIO(io)) ok = 0;
    if (ok) ok = SDL_RenamePath(tmp, path);
    if (!ok) SDL_RemovePath(tmp);
    return ok;
}

static int png(const char* name, const uint8_t* pixels, int w, int h) {
    char path[3100], tmp[3110];
    snprintf(path, sizeof(path), "%s/%s", dump.dir, name);
    if (exists(path)) return 1;
#if SDL_VERSION_ATLEAST(3, 4, 0)
    snprintf(tmp, sizeof(tmp), "%s.tmp", path);
    int ok;
    if (pngDumpStored()) {
        ok = pngWriteStored(tmp, w, h, 4, pixels, (size_t)w * 4); /* uncompressed (pngfast.c) */
    } else {
        SDL_Surface* surface = SDL_CreateSurfaceFrom(w, h, SDL_PIXELFORMAT_RGBA32, (void*)pixels, w * 4);
        if (!surface) return 0;
        ok = SDL_SavePNG(surface, tmp);
        SDL_DestroySurface(surface);
    }
    if (ok) ok = SDL_RenamePath(tmp, path);
    if (!ok) SDL_RemovePath(tmp);
    return ok;
#else
    (void)tmp;
    SDL_SetError("texture dumps require SDL 3.4 PNG support");
    return 0;
#endif
}

static int same(TextureRequest a, TextureRequest b) {
    return a.tpage == b.tpage && a.clut == b.clut && a.window == b.window &&
           a.u == b.u && a.v == b.v && a.w == b.w && a.h == b.h &&
           a.stableFrom == b.stableFrom && a.stableTo == b.stableTo;
}

static unsigned seenHash(uint64_t texel, uint64_t palette, TextureRequest r) {
    uint64_t h = texel ^ (palette * UINT64_C(0x9E3779B97F4A7C15));
    h ^= (uint64_t)r.tpage << 48 ^ (uint64_t)r.clut << 32 ^ (uint64_t)(uint32_t)(r.u | r.v << 8 | r.w << 16) ^
         (uint64_t)r.h << 24 ^ (uint64_t)r.window << 40 ^ r.stableFrom;
    h *= UINT64_C(0xBF58476D1CE4E5B9);
    return (unsigned)(h >> 32) & (DUMP_SLOTS - 1);
}

void textureDumpGlyph(uint64_t key, const uint8_t* indices, int w, int h, int limit) {
    if (!dump.log || dump.vblank < dump.from || !indices || w<1 || w>256 || h<1 || h>64 || limit<1 || limit>256) return;
    for (int i=0;i<dump.glyphCount;++i)
        if (dump.glyphSeen[i].key==key && dump.glyphSeen[i].limit==limit) return;
    if (dump.glyphCount==GLYPH_LIMIT) return;
    if (!dump.glyphLog) {
        char path[3100]; snprintf(path,sizeof(path),"%s/glyphs",dump.dir);
        if (!SDL_CreateDirectory(path)) { stop("cannot create glyph directory"); return; }
        snprintf(path,sizeof(path),"%s/glyphs/observations.jsonl",dump.dir);
        dump.glyphLog=SDL_IOFromFile(path,"ab");
        if (!dump.glyphLog) { stop("cannot open glyph observations"); return; }
    }
    size_t count=(size_t)w*h;
    uint8_t* grey=malloc(count*4); if (!grey) { SDL_ClearError(); stop("cannot allocate glyph dump"); return; }
    for (size_t i=0;i<count;++i) {
        if (indices[i]>=limit) { free(grey); return; }
        grey[i*4]=grey[i*4+1]=grey[i*4+2]=indices[i]; grey[i*4+3]=255;
    }
    char name[80]; snprintf(name,sizeof(name),"glyphs/%016" PRIx64 ".indices.png",key);
    int ok=png(name,grey,w,h); free(grey);
    if (!ok) { stop("cannot write glyph PNG"); return; }
    char record[256];
    int n=snprintf(record,sizeof(record),"{\"schema\":\"xg-glyph-dump-1\",\"key\":\"%016" PRIx64 "\","
        "\"width\":%d,\"height\":%d,\"depth\":%d,\"index_limit\":%d,\"vblank\":%u}\n",key,w,h,limit==256?8:4,limit,dump.vblank);
    if (n<0 || n>=(int)sizeof(record) || SDL_WriteIO(dump.glyphLog,record,(size_t)n)!=(size_t)n) {
        stop("cannot write glyph observation"); return;
    }
    /* The same shape may be a full atlas glyph and a packed dialog plane:
     * export both limits so authors can retain the stricter shared range. */
    dump.glyphSeen[dump.glyphCount++]=(GlyphSeen){key,limit};
}

static void sampleSeen(uint64_t texel, uint64_t palette, int u, int v, int w, int h) {
    unsigned slot = (unsigned)((texel ^ palette * 31 ^ (uint64_t)(u * 73856093) ^ (uint64_t)(v * 19349663) ^ (uint64_t)(w * 83492791) ^ (uint64_t)h) * 0x9E3779B97F4A7C15ull >> 47) & (SAMPLE_SLOTS - 1);
    for (int n = 0; n < SAMPLE_SLOTS; ++n, slot = (slot + 1) & (SAMPLE_SLOTS - 1)) {
        SampleSeen* e = &dump.samples[slot];
        if (e->texel == texel && e->palette == palette && e->u == u && e->v == v && e->w == w && e->h == h) return;
        if (e->texel) continue;
        *e = (SampleSeen){texel, palette, (int16_t)u, (int16_t)v, (int16_t)w, (int16_t)h};
        char line[200];
        int k = snprintf(line, sizeof(line), "{\"texel_key\":\"%016" PRIx64 "\",\"palette_key\":\"%016" PRIx64 "\",\"u\":%d,\"v\":%d,\"w\":%d,\"h\":%d,\"field\":%d}\n",
                         texel, palette, u, v, w, h, dump.mode == 1 ? gpuTextureField() : -1);
        if (k > 0 && k < (int)sizeof(line)) SDL_WriteIO(dump.sampleLog, line, (size_t)k);
        return;
    }
}

void textureDumpObserve(uint32_t tex, uint32_t window, uint32_t uvMin, uint32_t uvMax, int stable) {
    if (!dump.log || dump.vblank < dump.from) return;
    int u0 = (int16_t)uvMin, v0 = (int16_t)(uvMin >> 16);
    int u1 = (int16_t)uvMax, v1 = (int16_t)(uvMax >> 16);
    if (u1 < u0 || v1 < v0) return;
    /* More than one page's UV cycle has no larger distinct source patch. */
    if (u1 - u0 >= 256) { u0 = 0; u1 = 255; }
    if (v1 - v0 >= 256) { v0 = 0; v1 = 255; }
    TextureRequest r = {(uint16_t)tex, (uint16_t)(tex >> 16), window, u0, v0, u1 - u0 + 1, v1 - v0 + 1, 0, 0};
    TextureRequest sampled = r;
    const DecodedTexture* d = textureCacheGet(stable ? textureRequestSnapStable(r) : textureRequestSnap(r));
    if (!d) return;
    r = d->request;
    if (stable && dump.sampleLog && r.stableTo) sampleSeen(d->texelKey, d->paletteKey, sampled.u - r.u, sampled.v - r.v, sampled.w, sampled.h);
    unsigned slot = seenHash(d->texelKey, d->paletteKey, r);
    for (; dump.slots[slot] >= 0; slot = (slot + 1) & (DUMP_SLOTS - 1)) {
        const Seen* o = &dump.seen[dump.slots[slot]];
        if (o->texel == d->texelKey && o->palette == d->paletteKey && same(o->request, r)) return;
    }
    if (dump.count == DUMP_LIMIT) { SDL_ClearError(); stop("observation limit reached"); return; }
    char rgbaName[80], texelName[80], paletteName[80], indexName[80];
    snprintf(rgbaName, sizeof(rgbaName), "%016" PRIx64 "-%016" PRIx64 ".png", d->texelKey, d->paletteKey);
    snprintf(texelName, sizeof(texelName), "%016" PRIx64 ".%s.bin", d->texelKey, d->paletteSize ? "indices" : "words");
    snprintf(paletteName, sizeof(paletteName), "%016" PRIx64 ".clut.bin", d->paletteKey);
    snprintf(indexName, sizeof(indexName), "%016" PRIx64 ".indices.png", d->texelKey);
    size_t pixels = (size_t)r.w * r.h;
    int ok = png(rgbaName, d->rgba, r.w, r.h) &&
             binary(texelName, d->texels, pixels * (d->paletteSize ? 1 : 2));
    if (ok && d->paletteSize) {
        uint8_t palette[512];
        for (int i = 0; i < d->paletteSize; ++i) {
            palette[i * 2] = (uint8_t)d->palette[i];
            palette[i * 2 + 1] = (uint8_t)(d->palette[i] >> 8);
        }
        ok = binary(paletteName, palette, (size_t)d->paletteSize * 2);
        uint8_t* grey = malloc(pixels * 4);
        if (!grey) ok = 0;
        if (grey) {
            for (size_t i = 0; i < pixels; ++i) {
                grey[i * 4] = grey[i * 4 + 1] = grey[i * 4 + 2] = d->texels[i];
                grey[i * 4 + 3] = 255;
            }
            if (ok) ok = png(indexName, grey, r.w, r.h);
            free(grey);
        }
    }
    if (!ok) { stop("cannot write texture files"); return; }
    char record[1000];
    int n = snprintf(record, sizeof(record),
        "{\"schema\":\"xg-texture-dump-1\",\"texel_key\":\"%016" PRIx64 "\",\"palette_key\":\"%016" PRIx64
        "\",\"depth\":%d,\"width\":%d,\"height\":%d,\"rgba\":\"%s\",\"texels\":\"%s\","
        "\"palette\":\"%s\",\"indices\":\"%s\",\"vblank\":%u,\"mode\":%d,\"field\":%d,"
        "\"tpage\":%u,\"clut\":%u,\"window\":%u,\"u\":%d,\"v\":%d,"
        "\"sample_u\":%d,\"sample_v\":%d,\"sample_width\":%d,\"sample_height\":%d,"
        "\"stable_from\":%u,\"stable_to\":%u}\n",
        d->texelKey, d->paletteKey, d->depth, r.w, r.h, rgbaName, texelName,
        d->paletteSize ? paletteName : "", d->paletteSize ? indexName : "",
        dump.vblank, dump.mode, dump.mode == 1 ? gpuTextureField() : -1, r.tpage, r.clut, r.window, r.u, r.v,
        sampled.u, sampled.v, sampled.w, sampled.h, r.stableFrom, r.stableTo);
    if (n < 0 || n >= (int)sizeof(record) || SDL_WriteIO(dump.log, record, (size_t)n) != (size_t)n) {
        stop("cannot write observation"); return;
    }
    dump.slots[slot] = dump.count;
    dump.seen[dump.count++] = (Seen){d->texelKey, d->paletteKey, r};
}
