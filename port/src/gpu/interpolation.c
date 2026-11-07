/* Optional display snapshots. Borrowed game/decoder pointers are resolved
 * while constructing a frame; presentation reads only owned immutable data. */
#include "interpolation.h"
#include "pc_interpolation.h"
#include "pc_wide.h"
#include "gte_pc.h"
#include "system/settings.h"
#include "system/speedup.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define OWNERS 2048
#define PACKETS 32768
#define TEXTURES INTERP_MAX_DRAWS
#define TEXTURE_SLOTS (TEXTURES * 2)
#define SAMPLE_SLOTS 1024
#define SAMPLE_WAYS 4
#define SAMPLE_BYTES (32u << 20)
/* Indexed replacements (texture, sprite and glyph packs, up to 8x) by identity: the pack
 * image's revision (a new one for every new content), the palette key and the request name
 * their pixels exactly, so a repeat in a later frame takes its stored encoding without the
 * palette decode, the content hash and the exact comparison the RGBA path needs. Those cost
 * up to 4M pixels per page a frame: a Cel 8x room seen from the free camera spent most of
 * its frame there (field 17). Only the encoding is kept. */
#define IDENTITY_SLOTS 1024
#define IDENTITY_BYTES (256u << 20)
typedef struct { const void* owner; const void* model; uint64_t identity; } Owner;
typedef struct { const void* address; const void* transitionOwner; uint64_t object, face; int discrete, kind, placement, capture, frozen, sourceX, sourceY, collapse, u0, u1, dx0, dx1; float anchor[3]; } Packet;
typedef struct { uint64_t revision; TextureRequest request; uint32_t offset, format; uint64_t material; int scale; uint32_t nextContent, words; } Texture;
/* Reusable encoding only: every frame still owns its immutable copy. The
 * key selects a candidate; exact RGBA equality is required before reuse. */
typedef struct {
    uint8_t* rgba;
    size_t count, words;
    uint32_t format;
    uint64_t key, material, touched;
} SampleEncoding;
typedef struct {
    uint32_t* words;
    size_t count, n;
    uint32_t format;
    uint64_t key, material, touched;
} IdentityEncoding;
static struct {
    int loaded, enabled, building, next, log, field, kind;
    uint64_t scene, serial, nextObject, object;
    const uint8_t* base;
    Owner owners[OWNERS];
    int ownerCount, capture, waveFrame, captureReady;
    unsigned captureGroup[32];
    int replayFrozen;
    Packet packets[PACKETS];
    unsigned packetUsed[PACKETS];
    int packetCount;
    const Packet* packet;
    const void* address;
    Texture textures[TEXTURES];
    uint32_t textureSlots[TEXTURE_SLOTS];
    uint32_t contentSlots[TEXTURE_SLOTS];
    int textureCount;
    SampleEncoding samples[SAMPLE_SLOTS];
    size_t sampleBytes;
    IdentityEncoding identities[IDENTITY_SLOTS];
    size_t identityBytes;
    uint64_t identityHits;
    uint64_t sampleClock, sampleHits, sampleMisses;
    uint8_t* rgba;
    size_t rgbaCapacity;
    InterpFrame frames[INTERP_FRAMES];
    InterpTimeline timeline;
    const char* status;
} ip;

static void sampleDrop(SampleEncoding* e) {
    if (e->rgba) ip.sampleBytes -= (e->count + e->words) * 4;
    free(e->rgba); memset(e, 0, sizeof(*e));
}
static void identityDrop(IdentityEncoding* e) {
    if (e->words) ip.identityBytes -= e->n * 4;
    free(e->words); memset(e, 0, sizeof(*e));
}
static void sampleClear(void) {
    for (int i = 0; i < SAMPLE_SLOTS; i++) sampleDrop(&ip.samples[i]);
    for (int i = 0; i < IDENTITY_SLOTS; i++) identityDrop(&ip.identities[i]);
}
static IdentityEncoding* identitySet(uint64_t key) {
    unsigned slot = (unsigned)((key ^ (key >> 29)) * UINT64_C(0xbf58476d1ce4e5b9) >> 40) & (IDENTITY_SLOTS / SAMPLE_WAYS - 1);
    return &ip.identities[slot * SAMPLE_WAYS];
}
static IdentityEncoding* identityFind(uint64_t key, size_t count) {
    IdentityEncoding* set = identitySet(key);
    for (int i = 0; i < SAMPLE_WAYS; i++)
        if (set[i].words && set[i].key == key && set[i].count == count) {
            set[i].touched = ++ip.sampleClock; ++ip.identityHits; return &set[i];
        }
    return NULL;
}
static void identityKeep(uint64_t key, size_t count, const uint32_t* words, size_t n, uint32_t format, uint64_t material) {
    size_t bytes = n * 4;
    if (bytes > IDENTITY_BYTES / 4) return;
    IdentityEncoding* set = identitySet(key), *e = &set[0];
    for (int i = 1; e->words && i < SAMPLE_WAYS; i++)
        if (!set[i].words || set[i].touched < e->touched) e = &set[i];
    identityDrop(e);
    while (ip.identityBytes + bytes > IDENTITY_BYTES) {
        IdentityEncoding* oldest = NULL;
        for (int i = 0; i < IDENTITY_SLOTS; i++)
            if (ip.identities[i].words && (!oldest || ip.identities[i].touched < oldest->touched)) oldest = &ip.identities[i];
        if (!oldest) return;
        identityDrop(oldest);
    }
    uint32_t* data = malloc(bytes);
    if (!data) return;
    memcpy(data, words, bytes);
    *e = (IdentityEncoding){data, count, n, format, key, material, ++ip.sampleClock};
    ip.identityBytes += bytes;
}
static SampleEncoding* sampleSlot(uint64_t key) {
    unsigned slot = (unsigned)((key ^ (key >> 32)) * UINT64_C(0x9e3779b97f4a7c15)) & (SAMPLE_SLOTS / SAMPLE_WAYS - 1);
    return &ip.samples[slot * SAMPLE_WAYS];
}
static SampleEncoding* sampleFind(uint64_t key, const uint8_t* rgba, size_t count) {
    SampleEncoding* set = sampleSlot(key);
    for (int i = 0; i < SAMPLE_WAYS; i++) {
        SampleEncoding* e = &set[i];
        if (e->rgba && e->key == key && e->count == count && !memcmp(e->rgba, rgba, count * 4)) {
            e->touched = ++ip.sampleClock; ++ip.sampleHits; return e;
        }
    }
    ++ip.sampleMisses; return NULL;
}
static void sampleKeep(uint64_t key, const uint8_t* rgba, size_t count, const uint32_t* words,
                       size_t n, uint32_t format, uint64_t material) {
    size_t bytes = (count + n) * 4;
    if (bytes > SAMPLE_BYTES) return;
    SampleEncoding* e = sampleSlot(key);
    for (int i = 1; e->rgba && i < SAMPLE_WAYS; i++) {
        SampleEncoding* candidate = &sampleSlot(key)[i];
        if (!candidate->rgba || candidate->touched < e->touched) e = candidate;
    }
    sampleDrop(e);
    while (ip.sampleBytes + bytes > SAMPLE_BYTES) {
        SampleEncoding* oldest = NULL;
        for (int i = 0; i < SAMPLE_SLOTS; i++)
            if (ip.samples[i].rgba && (!oldest || ip.samples[i].touched < oldest->touched)) oldest = &ip.samples[i];
        if (!oldest) return;
        sampleDrop(oldest);
    }
    uint8_t* data = malloc(bytes);
    if (!data) return; /* caching is optional, the frame already owns its data */
    memcpy(data, rgba, count * 4); memcpy(data + count * 4, words, n * 4);
    *e = (SampleEncoding){data, count, n, format, key, material, ++ip.sampleClock};
    ip.sampleBytes += bytes;
}

static void load(void) {
    if (ip.loaded) return;
    ip.loaded = 1;
    ip.building = -1;
    ip.field = -1;
    ip.scene = 1;
    ip.status = "Original presentation";
    interpTimelineReset(&ip.timeline);
    settingsDescribe("interpolation", "interpolation: field geometry smoothing with one native frame of latency while Speedup is enabled; default on with Speedup");
    ip.enabled = settingsBool("interpolation", 1);
    ip.log = settingsBool("interpolation_log", 0);
}
/* Called per packet and per presented frame: speedup.c caches the setting
 * (refreshed on every settings change, so the menu's toggle and
 * speedupSetEnabled's gpuHostReset see the new value). */
int interpolationEnabled(void) { load(); return ip.enabled && speedupEnabled(); }
int interpolationCapturing(void) { return ip.loaded && ip.building >= 0 && ip.captureReady && ip.frames[ip.building].valid; }
int pcInterpolationBuilding(void) { return interpolationCapturing(); }
const char* interpolationStatus(void) { load(); return ip.status; }
void interpolationReport(const char* event, const char* reason, double vblank, double alpha) {
    if (ip.log && ip.enabled) gpuLog("interpolation-coverage: field=%d event=%s reason=%s vblank=%.3f alpha=%.6f",
                                   ip.field, event, reason, vblank, alpha);
}
void interpolationReset(void) {
    load();
    gpuHostInvalidate();
    if (ip.log) gpuLog("interpolation: reset scene %llu building %d", (unsigned long long)ip.scene, ip.building);
    ++ip.scene;
    ip.building = -1;
    ip.capture = ip.waveFrame = 0;
    ip.object = 0;
    ip.base = NULL;
    ip.packet = NULL;
    interpolationPacketsDone();
    /* Encodings own their pixels and are rechecked byte for byte on reuse.
     * A scene reset invalidates identities, not these bounded candidates. */
    ip.ownerCount = ip.textureCount = 0;
    for (int i = 0; i < INTERP_FRAMES; i++) ip.frames[i].serial = 0;
    interpTimelineReset(&ip.timeline);
    ip.status = interpolationEnabled() ? "Waiting for compatible field frames" : "Original presentation";
    gtePrecisionEnable(interpolationEnabled() || hiresPrecision());
}
void interpolationSetEnabled(int enabled) {
    load(); ip.enabled = enabled != 0;
    interpolationReset();
    settingsSetInt("interpolation", ip.enabled); settingsSave();
}
void interpolationShutdown(void) {
    if (ip.log) gpuLog("interpolation: texture encoding reuse %llu hits, %llu misses, %zu bytes; %llu by identity, %zu bytes",
        (unsigned long long)ip.sampleHits, (unsigned long long)ip.sampleMisses, ip.sampleBytes,
        (unsigned long long)ip.identityHits, ip.identityBytes);
    sampleClear();
    for (int i = 0; i < INTERP_FRAMES; i++) interpFrameFree(&ip.frames[i]);
    free(ip.rgba);
    memset(&ip, 0, sizeof(ip));
}
void pcInterpolationScene(void) { interpolationReset(); }
/* A transition can begin constructing the next back buffer before VSync
 * exposes the last completed one. Keep the newest pending snapshot of each
 * native rectangle, in addition to the two active presentation endpoints. */
static int pendingFrame(int slot) {
    const InterpFrame* f = &ip.frames[slot];
    if (!f->serial || f->serial <= ip.timeline.serial) return 0;
    for (int i = 0; i < INTERP_FRAMES; i++) {
        const InterpFrame* other = &ip.frames[i];
        if (other->serial > f->serial && other->x == f->x && other->y == f->y &&
            other->w == f->w && other->h == f->h) return 0;
    }
    return 1;
}
void pcInterpolationFrameBegin(int field, int x, int y, int w, int h) {
    load();
    ip.field = field; ip.waveFrame = 0;
    if (!interpolationEnabled() || gpuCapturing()) return;
    if (ip.building >= 0) interpolationReset();
    if (x < 0 || y < 0 || w < 1 || h < 1 || x + w > VRAM_W || y + h > VRAM_H) return;
    for (int i = 0; i < INTERP_FRAMES; i++) {
        int slot = (ip.next + i) % INTERP_FRAMES;
        if (slot == ip.timeline.current || slot == ip.timeline.previous || pendingFrame(slot)) continue;
        ip.building = slot; ip.next = (slot + 1) % INTERP_FRAMES; break;
    }
    if (ip.building < 0) return;
    InterpFrame* f = &ip.frames[ip.building];
    interpFrameClear(f);
    f->scene = ip.scene; f->serial = 0;
    f->x = x; f->y = y; f->w = w; f->h = h;
    ip.captureReady = gvkInterpolationReady();
    if (!ip.captureReady) interpFrameReject(f, "capture-workspace-unavailable");
    ip.replayFrozen = -1;
    ip.textureCount = 0;
    memset(ip.textureSlots, 0, sizeof(ip.textureSlots));
    memset(ip.contentSlots, 0, sizeof(ip.contentSlots));
}
static int viewport(InterpFrame* f) {
    WideView view;
    int wide = wideFrameView((VRect){f->x, f->y, f->x + f->w - 1, f->y + f->h - 1}, &view);
    if (wide < 0) { interpFrameReject(f, "expanded-frame-unsafe"); return 0; }
    int x = wide ? view.x : f->x, y = wide ? view.y : f->y;
    int w = wide ? view.w : f->w, h = wide ? view.h : f->h;
    int ox = wide ? view.m : 0, oy = wide ? view.top : 0;
    int aw, ah, aspect = presentAspect(&aw, &ah), expanded = presentExpandedVertical();
    if (f->viewSet && (f->wide != wide || f->viewX != x || f->viewY != y ||
        f->viewW != w || f->viewH != h || f->originX != ox || f->originY != oy ||
        f->aspect != aspect || f->aspectW != aw || f->aspectH != ah || f->expanded != expanded)) {
        interpFrameReject(f, "viewport-change"); return 0;
    }
    f->viewSet = 1; f->wide = wide; f->viewX = x; f->viewY = y;
    f->viewW = w; f->viewH = h; f->originX = ox; f->originY = oy;
    f->aspect = aspect; f->aspectW = aw; f->aspectH = ah; f->expanded = expanded;
    return 1;
}
int interpolationRecording(int wide) {
    if (!interpolationCapturing()) return 0;
    InterpFrame* f = &ip.frames[ip.building];
    return f->cleared && viewport(f) && f->wide == wide;
}
void pcInterpolationFrameEnd(void) {
    if (ip.building < 0) return;
    InterpFrame* f = &ip.frames[ip.building];
    f->serial = ++ip.serial;
    if (ip.capture) interpFrameReject(f,"incomplete-capture");
    if (!f->cleared) interpFrameReject(f, "missing-clear");
    if (!f->vertexCount) interpFrameReject(f, "empty-snapshot");
    if (f->valid) viewport(f);
    if (ip.log) gpuLog("interpolation: snapshot %llu valid %d clear %d rect %d,%d %dx%d draws %u vertices %u pixels %u",
                      (unsigned long long)f->serial, f->valid, f->cleared, f->x, f->y, f->w, f->h,
                      f->drawCount, f->vertexCount, f->pixelCount);
    if (ip.log && f->valid) {
        unsigned kinds[7] = {0};
        for (uint32_t i = 0; i < f->drawCount; i++)
            if (f->draws[i].object && (unsigned)f->draws[i].kind < 7) kinds[f->draws[i].kind]++;
        gpuLog("interpolation: families model=%u mecha=%u particle=%u sprite=%u shadow=%u transition=%u wave=%u",
               kinds[0], kinds[1], kinds[2], kinds[3], kinds[4], kinds[5], kinds[6]);
    }
    interpolationReport("snapshot", f->valid ? "valid" : f->rejection ? f->rejection : "unknown-capture", -1, -1);
    if (f->valid) gvkInterpolationCapture(ip.building, f);
    ip.building = -1; ip.capture = 0; ip.object = 0; ip.base = NULL;
}
void pcInterpolationObject(const void* owner, const void* model) {
    ip.object = 0; ip.base = NULL;
    ip.kind = PC_INTERP_MODEL;
    if (ip.building < 0 || !owner || !model) return;
    for (int i = 0; i < ip.ownerCount; i++) {
        Owner* p = &ip.owners[i];
        if (p->owner != owner) continue;
        if (p->model != model) { p->model = model; p->identity = ++ip.nextObject; }
        ip.object = p->identity; return;
    }
    if (ip.ownerCount == OWNERS) { interpolationReject("owner-capacity"); return; }
    ip.object = ++ip.nextObject;
    ip.owners[ip.ownerCount++] = (Owner){owner, model, ip.object};
}
void pcInterpolationObjectType(const void* owner, const void* model, int kind) {
    pcInterpolationObject(owner, model); ip.kind = kind;
}
void pcInterpolationModel(const void* primitives) { if (ip.object) ip.base = primitives; }
static unsigned packetHash(const void* p) {
    uintptr_t h = (uintptr_t)p >> 3;
    h ^= h >> 21; h *= UINT64_C(0x9e3779b97f4a7c15);
    return (unsigned)(h >> 32) & (PACKETS - 1);
}
static Packet* packetSlot(void* address) {
    unsigned h = packetHash(address);
    for (int i = 0; i < 64; i++, h = (h + 1) & (PACKETS - 1)) {
        if (ip.packets[h].address == address) return &ip.packets[h];
        if (!ip.packets[h].address) {
            if (ip.packetCount >= PACKETS / 2) break;
            ip.packetUsed[ip.packetCount++] = h;
            ip.packets[h] = (Packet){.address = address};
            return &ip.packets[h];
        }
    }
    interpolationReject("packet-capacity"); return NULL;
}
void pcInterpolationPrim(void* primitive) {
    if (!ip.object || !ip.base || ip.building < 0) return;
    uintptr_t addr = (uintptr_t)primitive, base = (uintptr_t)ip.base;
    if (addr < base || addr - base > UINT32_MAX) { interpolationReject("primitive-offset"); return; }
    Packet* p = packetSlot(primitive);
    if (p) { p->object = ip.object; p->face = addr - base + 1; p->discrete = 0; p->kind = ip.kind; p->placement = 0; }
}
void pcInterpolationDiscretePrim(void* primitive) {
    if (ip.building < 0 || !ip.loaded || !primitive) return;
    Packet* p = packetSlot(primitive);
    if (p) { p->object = p->face = 0; p->discrete = 1; p->placement = 0; }
}
void pcInterpolationHeapFree(const void* start, int bytes) {
    if (!ip.enabled || bytes <= 0) return;
    uintptr_t lo = (uintptr_t)start, hi = lo + (unsigned)bytes;
    for (int i = 0; i < ip.ownerCount; i++) {
        uintptr_t a = (uintptr_t)ip.owners[i].owner, b = (uintptr_t)ip.owners[i].model;
        if ((a >= lo && a < hi) || (b >= lo && b < hi)) { interpolationReset(); return; }
    }
}
void pcInterpolationLifetime(const void* owner) {
    if (!ip.enabled || !owner) return;
    for (int i = 0; i < ip.ownerCount; i++)
        if (ip.owners[i].owner == owner) { ip.owners[i].identity = ++ip.nextObject; return; }
}
void pcInterpolationKeyType(const void* owner, const void* resource, unsigned part, void* primitive, int kind) {
    if (ip.building < 0 || !primitive) return;
    uint64_t object = ip.object;
    int savedKind = ip.kind;
    const uint8_t* base = ip.base;
    pcInterpolationObject(owner, resource);
    if (kind == PC_INTERP_WAVE) ip.waveFrame = 1;
    if (ip.object) {
        Packet* p = packetSlot(primitive);
        if (p) { p->object = ip.object; p->face = (uint64_t)part + 1; p->discrete = 0; p->kind = kind; p->placement = 0; }
    }
    ip.object = object; ip.base = base; ip.kind = savedKind;
}
void pcInterpolationKey(const void* owner, const void* resource, unsigned part, void* primitive) {
    pcInterpolationKeyType(owner, resource, part, primitive, PC_INTERP_SPRITE);
}
void pcInterpolationPlacement(const void* owner, const void* resource, unsigned part, void* primitive, float x, float y, float z) {
    pcInterpolationKey(owner, resource, part, primitive);
    if (ip.building < 0 || !primitive) return;
    Packet* p = packetSlot(primitive);
    if (p && p->object) { p->placement = 1; p->anchor[0] = x; p->anchor[1] = y; p->anchor[2] = z; }
}
void pcInterpolationCapturePrim(void* primitive, int op, int sourceX, int sourceY) {
    if (ip.building < 0 || !primitive) return;
    Packet* p = packetSlot(primitive);
    if (p) { p->capture = op; p->sourceX = sourceX; p->sourceY = sourceY; }
}
void pcInterpolationWaveEdgePrim(void* primitive, int u0, int u1, int dx0, int dx1) {
    if (ip.building < 0 || !primitive) return;
    Packet* p=packetSlot(primitive);
    if (p) { p->collapse=1; p->u0=u0; p->u1=u1; p->dx0=dx0; p->dx1=dx1; }
}
void pcInterpolationTransitionPrim(const void* owner, unsigned part, void* primitive) {
    load();
    if (!interpolationEnabled() || !owner || !primitive) return;
    Packet* p = packetSlot(primitive);
    if (p) { p->frozen = 1; p->transitionOwner = owner; p->face = (uint64_t)part+1; p->kind = PC_INTERP_TRANSITION; }
}
static void captureBegin(void) {
    InterpFrame* f = &ip.frames[ip.building];
    /* Capture tags also govern native feedback bookkeeping. Preserve their
     * nesting while display workspace is unavailable; only storage stops. */
    if (!f->cleared || !viewport(f)) return;
    if (f->w != 320 || f->h != 224 || f->viewY < 0 || f->viewY + f->viewH > VRAM_H) { interpFrameReject(f, "capture-viewport"); return; }
    if (ip.capture == 32) { interpFrameReject(f,"capture-depth-capacity"); return; }
    ip.captureGroup[ip.capture++] = ++f->capture;
    if (f->valid && !f->wide && ip.capture == 1) {
        uint32_t words[1280], offset;
        gp0InterpolationCache(words);
        if (interpFrameWords(f,words,1280,&offset))
            interpFrameOp(f,(InterpOp){.kind=INTERP_OP_CACHE,.data=offset,.w=1280});
    }
}
void interpolationPacket(const void* address) {
    if (ip.capture && ip.packet && (ip.packet->capture & PC_INTERP_CAPTURE_END)) --ip.capture;
    ip.packet = NULL;
    ip.address = address;
    if (ip.building < 0) return;
    if (!address) return;
    unsigned h = packetHash(address);
    for (int i = 0; i < 64; i++, h = (h + 1) & (PACKETS - 1)) {
        if (!ip.packets[h].address) break;
        if (ip.packets[h].address == address) { ip.packet = &ip.packets[h]; break; }
    }
    if (ip.packet && ip.packet->transitionOwner) {
        uint64_t saved = ip.object;
        const uint8_t* base = ip.base; int kind = ip.kind;
        pcInterpolationObject(ip.packet->transitionOwner, ip.packet->transitionOwner);
        ((Packet*)ip.packet)->object = ip.object;
        ip.object = saved; ip.base = base; ip.kind = kind;
    }
    if (ip.packet && ip.packet->capture == PC_INTERP_CAPTURE_BEGIN) {
        captureBegin();
    } else if (ip.capture && (!ip.packet || (!ip.packet->capture && !ip.packet->frozen))) {
        interpFrameReject(&ip.frames[ip.building], "untagged-capture-packet");
    }
}
int interpolationCaptureSample(void) {
    int op = ip.capture && ip.packet ? ip.packet->capture & 15 : 0;
    return (ip.packet && ip.packet->frozen) || op == PC_INTERP_CAPTURE_BODY || op == PC_INTERP_CAPTURE_SAMPLE || op == PC_INTERP_CAPTURE_STASH;
}
int interpolationWaveFrame(void) { return ip.waveFrame; }
int interpolationWavePacket(void) { return ip.packet && ip.packet->kind == PC_INTERP_WAVE && ip.packet->capture; }
int interpolationGeometryPacket(void) { return ip.packet && ip.packet->object != 0; }
void interpolationPacketsDone(void) {
    if (ip.capture && ip.packet && (ip.packet->capture & PC_INTERP_CAPTURE_END)) --ip.capture;
    for (int i = 0; i < ip.packetCount; i++) ip.packets[ip.packetUsed[i]].address = NULL;
    ip.packetCount = 0; ip.packet = NULL; ip.waveFrame = 0;
}
void interpolationReject(const char* reason) {
    /* Only this explicitly authored prefix transform is independent of the
     * native cache walk. All other feedback/cache rejection remains intact. */
    if (ip.packet && (ip.packet->frozen || (ip.capture && ip.packet->capture)) &&
        (!strcmp(reason,"texture-feedback") || !strcmp(reason,"stale-cached-texels"))) return;
    if (ip.building >= 0) interpFrameReject(&ip.frames[ip.building], reason);
}
void interpolationUnsupported(void) { interpolationReject("unsupported-draw"); }

static uint64_t hashPixels(const uint8_t* p, size_t bytes) {
    uint64_t h = UINT64_C(14695981039346656037);
    for (size_t i = 0; i < bytes; i++) h = (h ^ p[i]) * UINT64_C(1099511628211);
    return h;
}
/* Content candidates are still verified pixel for pixel before storage is
 * shared. Hash whole words here: serial byte hashing of the large textures
 * in the opening flashback otherwise dominates snapshot construction.
 * Material equality only needs the same pixels to produce the same key. */
static uint64_t hashContent(const uint8_t* p, size_t bytes) {
    uint64_t h = UINT64_C(14695981039346656037) ^ bytes;
    while (bytes >= 8) {
        uint64_t word;
        memcpy(&word, p, sizeof(word));
        h ^= word;
        h = (h << 21) | (h >> 43);
        h *= UINT64_C(1099511628211);
        p += 8; bytes -= 8;
    }
    while (bytes--) h = (h ^ *p++) * UINT64_C(1099511628211);
    h ^= h >> 32;
    h *= UINT64_C(1099511628211);
    return h ^ (h >> 29);
}
/* interpolationPoseDraw: the sample texture() takes instead of a lookup. */
static struct { int on; const DecodedTexture* d; TextureSample s; } poseOverride;
static int texture(InterpFrame* f, HiVertex* v, int count, int kind, uint64_t* material) {
    uint32_t tex = v[0].tex;
    const DecodedTexture* d;
    TextureSample sample;
    int filter;
    if (poseOverride.on) {
        d = poseOverride.d; sample = poseOverride.s;
        filter = gvkHiScale() ? textureFilter(kind) : TEXTURE_FILTER_NEAREST;
        goto sampled;
    }
    int u = (int16_t)v[0].uvMin, vv = (int16_t)(v[0].uvMin >> 16);
    TextureRequest patch = {(uint16_t)tex, (uint16_t)(tex >> 16), v[0].texWindow,
                            u, vv, (int16_t)v[0].uvMax - u + 1, (int16_t)(v[0].uvMax >> 16) - vv + 1, 0, 0};
    if (patch.w < 1 || patch.h < 1 || patch.w > 256 || patch.h > 256) {
        interpolationReject("texture-region"); return 0;
    }
    /* Tagged face snapshots already carry the complete canvas. A later
     * upload may cover multiple faces, or evict their upload provenance. */
    d = textureCacheGet(PC_TEX_IS_PORTRAIT(kind) ? patch : textureRequestSnap(patch));
    const char* reason = !d || !d->rgba ? "unavailable-texture" : gp0InterpolationTextureReason(d);
    if (reason) {
        interpolationReject(reason);
        if (ip.log) gpuLog("interpolation: unsupported texture %08x uv %d,%d %dx%d", tex, patch.u, patch.v, patch.w, patch.h);
        return 0;
    } /* rendered captures/feedback are unsupported */
    sample = (TextureSample){.rgba = d->rgba, .revision = d->revision, .u = d->request.u, .v = d->request.v,
                             .w = d->request.w, .h = d->request.h, .scale = 1};
    filter = gvkHiScale() ? textureFilter(kind) : TEXTURE_FILTER_NEAREST;
    if (gvkHiScale()) {
        const DecodedTexture* enhanced = PC_TEX_IS_PORTRAIT(kind) ? d :
            textureCacheGet(kind == PC_TEX_3D ? textureRequestSnapStable(patch) : textureRequestSnap(patch));
        int sampled = PC_TEX_IS_PORTRAIT(kind) && !textureUsePack(kind) ? portraitPackClassSample(enhanced,patch,kind,&sample) :
            (textureUsePack(kind)?texturePackSample(enhanced,patch,filter,&sample):textureUseSpriteCel(kind)?textureSpriteCelSample(enhanced,patch,filter,&sample):0);
        int hudScale=textureHudScale(kind);
        int celScale=textureCelScale(kind);
        /* Unmatched Cel graphics take the native decode below: the same
         * sample a nearest textureSampleGet gives, at any depth. */
        if(!sampled && celScale) sampled=textureCelSample(enhanced,patch,celScale,&sample);
        /* Match direct drawing: composing a whole uploaded font atlas can
         * hit the canvas cap and discard the glyph's authored 8x detail. */
        if (!sampled && textureUseGlyphs(kind)) {
            sampled = textureGlyphCompose(textureCacheGet(patch), &sample);
            if (sampled && textureGlyphSmooth()) filter = TEXTURE_FILTER_TEXT_SMOOTH;
        }
        if (!sampled && hudScale) sampled=textureUpscaleSample(enhanced,patch,hudScale,&sample);
        if (!sampled && enhanced && enhanced->rgba) {
            sample=(TextureSample){enhanced->rgba,enhanced->revision,enhanced->request.u,enhanced->request.v,
                enhanced->request.w,enhanced->request.h,1,NULL,0};
        }
        /* Replay evaluates Scale2x from the shared original texels. Keeping
         * a separate enlarged, edge-clamped image for every face duplicates
         * the same upload hundreds of times in complex Gear/story scenes. */
    }
sampled:
    if (sample.scale < 1 || sample.scale > 8 || sample.w < 1 || sample.h < 1 || sample.w > 256 || sample.h > 256) {
        interpolationReject("replacement-size"); return 0;
    }
    size_t pixels = (size_t)sample.w * sample.h * sample.scale * sample.scale;
    if (pixels > INTERP_MAX_PIXELS) { interpolationReject("texture-capacity"); return 0; }
    if (!sample.indices && !sample.rgba) { interpolationReject("unavailable-replacement"); return 0; }
    TextureRequest request = d->request;
    request.u = sample.u; request.v = sample.v; request.w = sample.w; request.h = sample.h;
    uint64_t revision = sample.revision ^ (sample.indices ? d->paletteKey : 0);
    Texture* t = NULL;
    uint64_t requestKey = hashPixels((const uint8_t*)&request, sizeof(request)) ^ (uint64_t)sample.scale;
    uint64_t key = requestKey ^ revision;
    unsigned slot = (unsigned)((key ^ (key >> 32)) * UINT64_C(0x9e3779b97f4a7c15)) & (TEXTURE_SLOTS - 1);
    while (ip.textureSlots[slot]) {
        Texture* p = &ip.textures[ip.textureSlots[slot] - 1];
        if (p->revision == revision && p->scale == sample.scale && !memcmp(&p->request, &request, sizeof(p->request))) { t = p; break; }
        slot = (slot + 1) & (TEXTURE_SLOTS - 1);
    }
    IdentityEncoding* known = !t && sample.indices ? identityFind(key, pixels) : NULL;
    if (known) {   /* an indexed replacement seen in an earlier frame: its encoding as kept */
        if (ip.textureCount == TEXTURES) { interpolationReject("texture-count"); return 0; }
        uint32_t offset;
        if (!interpFrameWords(f, known->words, (uint32_t)known->n, &offset)) return 0;
        t = &ip.textures[ip.textureCount++];
        *t = (Texture){revision, request, offset, known->format, known->material, sample.scale, 0, (uint32_t)known->n};
        ip.textureSlots[slot] = (uint32_t)ip.textureCount;
    }
    if (!t) {
        if (ip.textureCount == TEXTURES) { interpolationReject("texture-count"); return 0; }
        /* Indexed samples (glyph, Cel and pack replacements, up to 8x) are
         * decoded through the palette only for a texture new to this frame:
         * a table hit already trusts the request, revision (the palette key
         * included) and scale to name the same pixels. Decoding w*h*64
         * pixels for every repeated glyph and HUD quad was the game thread's
         * largest cost in fields (xg-ody). */
        if (sample.indices) {
            if (ip.rgbaCapacity < pixels * 4) {
                uint8_t* next = realloc(ip.rgba, pixels * 4);
                if (!next) { interpolationReject("texture-capacity"); return 0; }
                ip.rgba = next; ip.rgbaCapacity = pixels * 4;
            }
            for (size_t i = 0; i < pixels; i++) {
                unsigned index = sample.indices[i];
                if (index >= (unsigned)d->paletteSize) { interpolationReject("replacement-index"); return 0; }
                uint16_t c = d->palette[index];
                for (int k = 0; k < 3; k++) { int channel = (c >> (5 * k)) & 31; ip.rgba[i * 4 + k] = (uint8_t)((channel << 3) | (channel >> 2)); }
                ip.rgba[i * 4 + 3] = c == 0 ? 0 : c & 0x8000 ? 128 : 255;
            }
            sample.rgba = ip.rgba;
        }
        /* Location selects candidates across decode/CLUT revisions; exact
         * pixels decide reuse. Unused palette changes no longer force a
         * large rehash, while different used colours still miss at once. */
        uint64_t sampleKey = requestKey;
        SampleEncoding* encoded = sampleFind(sampleKey, sample.rgba, pixels);
        uint64_t content = encoded ? encoded->material : hashContent(sample.rgba, pixels * 4);
        uint32_t offset = f->pixelCount, format = 0, words = 0;
        /* Different palettes, page locations and decode revisions can yield
         * identical immutable pixels. Keep their sampling coordinates but
         * share storage only after an exact comparison, never on a hash alone. */
        unsigned contentSlot = (unsigned)(content ^ (content >> 32) ^ pixels) & (TEXTURE_SLOTS - 1);
        for (uint32_t i = ip.contentSlots[contentSlot]; i; i = ip.textures[i - 1].nextContent) {
            const Texture* p = &ip.textures[i - 1];
            size_t n = (size_t)p->request.w * p->request.h * p->scale * p->scale;
            if (p->material == content && n == pixels && interpSampleEqual(f, p->offset, p->format, sample.rgba, pixels)) {
                offset = p->offset; format = p->format; words = p->words; break;
            }
        }
        if (offset == f->pixelCount) {
            if (encoded) {
                format = encoded->format;
                if (!interpFrameWords(f, (const uint32_t*)(encoded->rgba + pixels * 4), (uint32_t)encoded->words, &offset)) return 0;
                words = (uint32_t)encoded->words;
            } else {
                if (!interpFrameSample(f, sample.rgba, pixels, &offset, &format)) return 0;
                words = f->pixelCount - offset;
            }
        }
        /* An alias that shared this frame's storage still needs its own
         * candidate next frame, otherwise it rehashes the same pixels. */
        if (!encoded) sampleKeep(sampleKey, sample.rgba, pixels, f->pixels + offset, words, format, content);
        if (sample.indices) identityKeep(key, pixels, f->pixels + offset, words, format, content);
        t = &ip.textures[ip.textureCount++];
        *t = (Texture){revision, request, offset, format, content, sample.scale, ip.contentSlots[contentSlot], words};
        ip.contentSlots[contentSlot] = (uint32_t)ip.textureCount;
        ip.textureSlots[slot] = (uint32_t)ip.textureCount;
    }
    *material = t->material;
    for (int i = 0; i < count; i++) {
        v[i].sample[0] = t->offset;
        v[i].sample[1] = ((uint32_t)t->request.u & 65535) | (uint32_t)t->request.v << 16;
        v[i].sample[2] = (uint32_t)t->request.w | (uint32_t)t->request.h << 16;
        v[i].sample[3] = t->format | (uint32_t)(filter + 1) | (uint32_t)t->scale << 8;
    }
    return 1;
}
void interpolationDraw(const HiVertex* vertices, int count, int kind,
                       int wide, int shiftX, int shiftY, unsigned piece) {
    if (ip.building < 0 || count < 1 || count > 6) return;
    InterpFrame* f = &ip.frames[ip.building];
    if (!f->valid) return;
    int capture = ip.capture && ip.packet ? ip.packet->capture & 15 : 0;
    if ((capture == PC_INTERP_CAPTURE_SKIP && (vertices[0].mode & DRAW_TEXTURED)) || capture == PC_INTERP_CAPTURE_BEGIN || capture == PC_INTERP_CAPTURE_MOVE || capture == PC_INTERP_CAPTURE_FREEZE) return;
    HiVertex v[6];
    memcpy(v, vertices, (size_t)count * sizeof(*v));
    if (ip.packet && ip.packet->collapse) for (int i=0;i<count;i++) {
        if (v[i].u==ip.packet->u0) v[i].x += ip.packet->dx0;
        else if (v[i].u==ip.packet->u1) v[i].x += ip.packet->dx1;
    }
    /* A pose is one face of its actor whatever part it is drawn with. */
    uint64_t material = 0, object = ip.packet ? ip.packet->object : 0, face = poseOverride.on ? 0xFFFu : ip.packet ? ip.packet->face : 0;
    int frozen = ip.packet && ip.packet->frozen;
    if (object) {
        face = (face << 4) | piece;
        if (capture) face |= (uint64_t)ip.captureGroup[ip.capture-1] << 48;
    }
    if (ip.packet && ip.packet->discrete) kind = PC_TEX_SPRITE;
    float xmin = v[0].x, xmax = xmin, ymin = v[0].y, ymax = ymin;
    for (int i = 1; i < count; i++) {
        if (v[i].x < xmin) xmin = v[i].x;
        if (v[i].x > xmax) xmax = v[i].x;
        if (v[i].y < ymin) ymin = v[i].y;
        if (v[i].y > ymax) ymax = v[i].y;
    }
    if (f->frozen && wide == f->wide) {
        float left=(float)(v[0].clipMin&65535), top=(float)(v[0].clipMin>>16);
        float right=(float)((v[0].clipMax&65535)+1), bottom=(float)((v[0].clipMax>>16)+1);
        if (left<xmin) left=xmin;
        if (top<ymin) top=ymin;
        if (right>xmax) right=xmax;
        if (bottom>ymax) bottom=ymax;
        if(left<right && top<bottom && right>f->frozenRect.x0 && left<f->frozenRect.x1+1 && bottom>f->frozenRect.y0 && top<f->frozenRect.y1+1) {
            f->frozenDirty=f->frozenWrites=1;
        }
    }
    int limitW = capture ? WIDE_W : f->viewW;
    int limitH = capture ? VRAM_H-f->viewY : f->viewH;
    /* Authored stash packets occupy the private atlas beside the view. */
    if (wide != f->wide) return;
    if (!interpolationWavePacket() && (xmax < f->viewX || xmin >= f->viewX + limitW ||
        ymax < f->viewY || ymin >= f->viewY + f->viewH)) return;
    if (!capture && !frozen && v[0].wideTex) { interpFrameReject(f, "texture-feedback"); return; }
    /* Untagged projected geometry is not safely identifiable. Screen-aligned
     * quads, sprite/HUD/text layers and line blocks remain discrete. */
    if (!capture && !object && kind == PC_TEX_3D && !(v[0].mode & DRAW_FILL)) {
        int flat = count == 6 && v[0].x == v[2].x && v[1].x == v[5].x && v[0].y == v[1].y && v[2].y == v[5].y;
        if (!flat) {
            if (ip.log) gpuLog("interpolation: unidentified geometry count %d mode %x at %.1f,%.1f packet %p", count, v[0].mode, v[0].x, v[0].y, ip.address);
            interpFrameReject(f, "unidentified-geometry"); return;
        }
    }
    int sourceX = 0, sourceY = 0, version = -1;
    if (frozen) {
        if (capture || ((v[0].tex >> 7) & 3) != 2) {
            interpFrameReject(f,"unsafe-transition-capture"); return;
        }
        VRect rect = {704,256,1023,511};
        if (wide && (!v[0].wideTex || !wideTransitionCaptureRect(&rect))) {
            interpFrameReject(f,"missing-transition-capture"); return;
        }
        sourceX = (v[0].tex & 15)*64 + (wide ? (int16_t)v[0].wideTex : 0) - rect.x0;
        sourceY = ((v[0].tex >> 4) & 1)*256 + (wide ? (int32_t)(v[0].wideTex << 1) >> 17 : 0) - rect.y0;
        HiVertex check=v[0];VRect sampled;
        check.sample[0]=(uint32_t)(uint16_t)sourceX|(uint32_t)(uint16_t)sourceY<<16;
        if(!interpCaptureRect(&check,&sampled) || sampled.x0<0 || sampled.y0<0 || sampled.x1>rect.x1-rect.x0 || sampled.y1>rect.y1-rect.y0) {
            interpFrameReject(f,"transition-capture-bounds"); return;
        }
        if (!f->wide && !gp0InterpolationCaptureSafe()) f->frozenWrites=1;
        if (!f->frozenCount || f->frozenDirty || memcmp(&rect,&f->frozenRect,sizeof(rect))) {
            if (f->frozenCount==INTERP_FREEZES) { interpFrameReject(f,"capture-version-capacity"); return; }
            version=f->frozenCount++;
            f->frozenRects[version]=rect;
            if (!gvkInterpolationFreeze(ip.building,version,rect,wide)) { interpFrameReject(f,"capture-allocation"); return; }
            if (!f->frozen && !wide && !ip.capture) {
                uint32_t words[1280], offset;
                gp0InterpolationCache(words);
                if (interpFrameWords(f,words,1280,&offset)) interpFrameOp(f,(InterpOp){.kind=INTERP_OP_CACHE,.data=offset,.w=1280});
            }
            f->frozenDirty=0;
        } else version=f->frozenCount-1;
        if (ip.replayFrozen!=version) {
            interpFrameOp(f,(InterpOp){.kind=INTERP_OP_FROZEN,.data=(uint32_t)version}); ip.replayFrozen=version;
        }
        f->frozen=1; f->frozenRect=rect;
    }
    for (int i = 0; i < count; i++) {
        v[i].x -= f->viewX; v[i].y -= f->viewY;
        int x0 = (int)(v[i].clipMin & 65535) - f->viewX, y0 = (int)(v[i].clipMin >> 16) - f->viewY;
        int x1 = (int)(v[i].clipMax & 65535) - f->viewX, y1 = (int)(v[i].clipMax >> 16) - f->viewY;
        if (x0 < 0) x0 = 0;
        if (y0 < 0) y0 = 0;
        if (x1 >= limitW) x1 = limitW - 1;
        if (y1 >= limitH) y1 = limitH - 1;
        if (x1 < x0 || y1 < y0) return;
        v[i].clipMin = (uint32_t)x0 | (uint32_t)y0 << 16;
        v[i].clipMax = (uint32_t)x1 | (uint32_t)y1 << 16;
        v[i].wideTex = v[i].wideEdge = 0;
        memset(v[i].sample, 0, sizeof(v[i].sample));
        v[i].mode &= ~(DRAW_CACHED | DRAW_FRINGE);
    }
    if (capture) ip.replayFrozen=-1;
    if (capture && (v[0].mode & DRAW_TEXTURED)) {
        if (((vertices[0].tex >> 7) & 3) != 2) { interpFrameReject(f,"capture-depth"); return; }
        int tx = (vertices[0].tex & 15)*64, ty = ((vertices[0].tex >> 4)&1)*256;
        /* The native saved strips occupy their own rows in the private replay.
         * Pack their 64 columns at x=0 instead of inheriting VRAM x=960. */
        if (wide && tx == 960 && capture == PC_INTERP_CAPTURE_SAMPLE) { tx = 0; ty += VRAM_H; }
        else if (wide && vertices[0].wideTex) {
            tx += (int16_t)vertices[0].wideTex;
            ty += (int32_t)(vertices[0].wideTex << 1) >> 17;
        } else if (wide) ty += VRAM_H;
        for (int i = 0; i < count; i++) {
            v[i].sample[0] = (uint32_t)(uint16_t)tx | (uint32_t)(uint16_t)ty << 16;
            v[i].sample[3] = INTERP_SAMPLE_CAPTURE | INTERP_SAMPLE_EVOLVING | (wide ? INTERP_SAMPLE_UNCACHED : 0);
            v[i].tex &= ~16u; /* framebuffer parity is not a material change */
        }
    }
    if (frozen) for (int i = 0; i < count; i++) {
        v[i].sample[0] = (uint32_t)(uint16_t)sourceX | (uint32_t)(uint16_t)sourceY << 16;
        v[i].sample[3] = INTERP_SAMPLE_CAPTURE | (uint32_t)version<<24;
    }
    if (!capture && !frozen && (v[0].mode & DRAW_TEXTURED) && !texture(f, v, count, kind, &material)) { interpFrameReject(f, "unsupported-texture"); return; }
    if (interpFrameDraw(f, v, count, object, face, material)) {
        f->draws[f->drawCount - 1].kind = ip.packet ? ip.packet->kind : PC_INTERP_MODEL;
        f->draws[f->drawCount - 1].collapse = ip.packet && ip.packet->collapse;
        if (ip.packet && ip.packet->placement)
            interpFramePlacement(f, ip.packet->anchor[0] + f->x + shiftX - f->viewX,
                                 ip.packet->anchor[1] + f->y + shiftY - f->viewY, ip.packet->anchor[2]);
    }
}
void interpolationPoseDraw(const HiVertex* vertices, int count, int wide, int shiftX, int shiftY,
                           const DecodedTexture* palette, const TextureSample* sample) {
    if (!palette || !sample) return;
    poseOverride.on = 1; poseOverride.d = palette; poseOverride.s = *sample;
    interpolationDraw(vertices, count, PC_TEX_SPRITE, wide, shiftX, shiftY, 15);
    poseOverride.on = 0;
}
void interpolationClear(VRect r, uint32_t rgb) {
    interpolationSourceChanging(r);
    if (ip.building < 0) return;
    InterpFrame* f = &ip.frames[ip.building];
    if (r.x0 <= f->x && r.y0 <= f->y && r.x1 >= f->x + f->w - 1 && r.y1 >= f->y + f->h - 1) {
        interpFrameClear(f);
        if (!ip.captureReady) interpFrameReject(f, "capture-workspace-unavailable");
        ip.textureCount = 0; memset(ip.textureSlots, 0, sizeof(ip.textureSlots));
        memset(ip.contentSlots, 0, sizeof(ip.contentSlots));
        f->background = rgb; f->cleared = 1; ip.replayFrozen=-1;
    } else if (r.x0 < f->x + f->w && r.x1 >= f->x && r.y0 < f->y + f->h && r.y1 >= f->y) {
        interpFrameReject(f, "partial-clear");
    }
}
void interpolationCacheClear(void) {
    if (ip.building < 0) return;
    InterpFrame* f = &ip.frames[ip.building];
    if ((ip.capture || f->frozen) && !f->wide) interpFrameOp(f,(InterpOp){.kind=INTERP_OP_CACHE});
}
void interpolationCopy(int wide, int sx, int sy, int dx, int dy, int w, int h) {
    if (!wide) interpolationSourceChanging((VRect){dx,dy,dx+w-1,dy+h-1});
    if (ip.building < 0 || !ip.capture) return;
    InterpFrame* f = &ip.frames[ip.building];
    if (!f->valid || !viewport(f)) return;
    if (wide && w == 64 && h == 16 && dy >= WIDE_SLOT_Y) return; /* native strips own their deterministic atlas */
    if (wide != f->wide) {
        /* Native saved strips have no wide slot. Their centre source comes
         * from the private moving picture; keep their atlas on separate rows. */
        if (wide || !f->wide || dx != 960 || w != 64 || h != 16) return;
        sx += f->viewX + f->originX-f->x; sy += f->viewY + f->originY-f->y;
        dx = 0; dy += VRAM_H;
    }
    unsigned flags=(sx>=f->viewX && sx+w<=f->viewX+f->viewW && sy>=f->viewY && sy+h<=f->viewY+f->viewH ? 1u : 0u) |
        (dx>=f->viewX && dx+w<=f->viewX+f->viewW && dy>=f->viewY && dy+h<=f->viewY+f->viewH ? 2u : 0u);
    interpFrameOp(f,(InterpOp){.kind=INTERP_OP_COPY,.data=flags,.sx=sx,.sy=sy,.dx=dx,.dy=dy,.w=w,.h=h});
}
void interpolationSourceChanging(VRect r) {
    if (ip.building < 0) return;
    InterpFrame* f=&ip.frames[ip.building];
    if (f->frozen && r.x0<=1023 && r.x1>=704 && r.y0<=511 && r.y1>=256)
        f->frozenDirty=f->frozenWrites=1;
}
void interpolationTransfer(VRect r) {
    if (!ip.enabled) return;
    if (ip.capture && ip.packet &&
        ((ip.packet->capture & 15) == PC_INTERP_CAPTURE_SKIP || (ip.packet->capture & 15) == PC_INTERP_CAPTURE_MOVE || (ip.packet->capture & 15) == PC_INTERP_CAPTURE_FREEZE)) return;
    interpolationSourceChanging(r);
    for (int i = 0; i < INTERP_FRAMES; i++) {
        InterpFrame* f = &ip.frames[i];
        if (f->w && r.x0 < f->x + f->w && r.x1 >= f->x && r.y0 < f->y + f->h && r.y1 >= f->y) {
            if (ip.log) gpuLog("interpolation: framebuffer transfer %d,%d..%d,%d", r.x0, r.y0, r.x1, r.y1);
            interpolationReport("reset", "framebuffer-transfer", -1, -1);
            interpolationReset(); return;
        }
    }
}
const InterpFrame* interpolationFrame(int index) { return index >= 0 && index < INTERP_FRAMES ? &ip.frames[index] : NULL; }
static const InterpFrame* picture(const GpuDisplay* d, double vblank, int* before, int* after, double* alpha, int sample) {
    load();
    if (!interpolationEnabled()) return NULL;
    if (!d->enabled || d->rgb24) {
        interpolationReport("native", d->rgb24 ? "rgb24-display" : "display-disabled", vblank, -1); return NULL;
    }
    int slot = -1;
    for (int i = 0; i < INTERP_FRAMES; i++) {
        if (sample && i != ip.timeline.current) continue;
        const InterpFrame* f = &ip.frames[i];
        if (!f->serial || d->x < f->x || d->y < f->y || d->x + d->w > f->x + f->w || d->y + d->h > f->y + f->h) continue;
        if (slot < 0 || f->serial > ip.frames[slot].serial) slot = i;
    }
    if (slot < 0 || !ip.frames[slot].valid) {
        interpolationReport("native", slot < 0 ? "missing-snapshot" : ip.frames[slot].rejection ? ip.frames[slot].rejection : "invalid-snapshot", vblank, -1);
        ip.status = "Native: unsupported field frame"; interpTimelineReset(&ip.timeline); return NULL;
    }
    int aw, ah, aspect = presentAspect(&aw, &ah);
    const InterpFrame* latest = &ip.frames[slot];
    if (latest->aspect != aspect || latest->aspectW != aw || latest->aspectH != ah ||
        latest->expanded != presentExpandedVertical()) {
        interpolationReport("reset", "framing-change", vblank, -1);
        interpolationReset(); return NULL;
    }
    if (latest->serial < ip.timeline.serial) {
        interpolationReport("native", "stale-snapshot", vblank, -1);
        ip.status = "Native: waiting for a newer field frame"; return NULL;
    }
    if (!sample) interpTimelineActivate(&ip.timeline, ip.frames[slot].serial, slot, vblank);
    int prev = ip.timeline.previous;
    const char* reason = prev < 0 ? "timeline-anchor" : interpFramesMismatch(&ip.frames[prev], &ip.frames[slot]);
    if (reason) {
        interpolationReport("native", reason, vblank, -1);
        ip.status = "Native: waiting for compatible geometry"; return NULL;
    }
    *before = prev; *after = slot; *alpha = interpTimelineAlpha(&ip.timeline, vblank);
    ip.status = "Interpolated field geometry (one native frame delay)";
    return &ip.frames[prev];
}
const InterpFrame* interpolationPicture(const GpuDisplay* d, double vblank, int* before, int* after, double* alpha) {
    return picture(d, vblank, before, after, alpha, 0);
}
const InterpFrame* interpolationSample(const GpuDisplay* d, double vblank, int* before, int* after, double* alpha) {
    return picture(d, vblank, before, after, alpha, 1);
}
