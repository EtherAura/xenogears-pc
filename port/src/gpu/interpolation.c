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
typedef struct { const void* address; const void* transitionOwner; uint64_t object, face; int discrete, kind, placement, capture, frozen, sourceX, sourceY, collapse, u0, u1, dx0, dx1; float anchor[3]; unsigned gear, bone; } Packet;
typedef struct { uint64_t revision; TextureRequest request; uint32_t offset, format; uint64_t material; int scale; uint32_t nextContent, words; } Texture;
#define MIP_SLOTS 2048
typedef struct { uint64_t revision, palette; uint32_t origin, size; } MipKey;
typedef struct { MipKey key; uint32_t offset, format; } MipTexture;
#define PACK_PAGE_BYTES (96u << 20)
typedef struct {
    TextureSample sample;
    uint8_t* data;
    size_t bytes;
    uint64_t touched;
    uint8_t used[256];
} PackPage;
/* Every frame owns its encoding. A verified native decode identifies the
 * whole RGBA image for its cache lifetime. Other candidates compare pixels. */
typedef struct {
    uint8_t* rgba;
    size_t count, words;
    uint32_t format;
    uint64_t key, material, touched;
    uint64_t nativeRevision, scene;
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
    unsigned gear, bone;
    Texture textures[TEXTURES];
    uint32_t textureSlots[TEXTURE_SLOTS];
    uint32_t contentSlots[TEXTURE_SLOTS];
    int textureCount;
    MipTexture mips[MIP_SLOTS];
    PackPage pages[INTERP_PACK_PAGES];
    size_t pageBytes, pagePeak;
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
static void packPagesClear(void) {
    for (int i = 0; i < INTERP_PACK_PAGES; ++i) free(ip.pages[i].data);
    memset(ip.pages, 0, sizeof(ip.pages));
    ip.pageBytes = 0;
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
static SampleEncoding* sampleFind(uint64_t key, const uint8_t* rgba, size_t count, uint64_t nativeRevision) {
    SampleEncoding* set = sampleSlot(key);
    for (int i = 0; i < SAMPLE_WAYS; i++) {
        SampleEncoding* e = &set[i];
        if (!e->rgba || e->key != key || e->count != count) continue;
        int sameDecode = nativeRevision && e->nativeRevision == nativeRevision && e->scene == ip.scene;
        if (sameDecode || !memcmp(e->rgba, rgba, count * 4)) {
            e->nativeRevision = nativeRevision;
            e->scene = ip.scene;
            e->touched = ++ip.sampleClock;
            ++ip.sampleHits;
            return e;
        }
    }
    ++ip.sampleMisses; return NULL;
}
static void sampleKeep(uint64_t key, const uint8_t* rgba, size_t count, const uint32_t* words,
                       size_t n, uint32_t format, uint64_t material, uint64_t nativeRevision) {
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
    *e = (SampleEncoding){data, count, n, format, key, material, ++ip.sampleClock, nativeRevision, ip.scene};
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
int interpolationSetting(void) { load(); return ip.enabled && speedupEnabled(); }

/* The Enhanced engine's 60 Hz field (its 20 and 15 fps rooms included) and
 * world map complete a picture every vblank, and smoothing covers only those
 * two: at a display rate of 60 Hz or the native rate it would replay each one
 * a second time (the scene's draws and effects: the GPU's work about 1.4
 * times) a frame late, and add nothing. It stands aside there: the game's
 * own pictures, no delay; at 120/144/240 Hz it still adds pictures between
 * (xg-c4y7: measured in Lahan, late vblanks 6 -> 0 of 341, GPU
 * median 13.6 -> 9.5 ms). The setting itself stays as chosen. Weak: the
 * programs that link this file without the gameplay switches never stand
 * aside. */
extern int pcGameplayField60(void) __attribute__((weak));
extern int pcGameplayField60Slow(void) __attribute__((weak));
extern int pcGameplayWorldmap60(void) __attribute__((weak));
int interpolationStandsAside(void) {
#if XG_ENGINE_ENHANCED
    return presentationRate() <= 60 && pcGameplayField60 != NULL && pcGameplayField60Slow != NULL &&
           pcGameplayWorldmap60 != NULL && pcGameplayField60() && pcGameplayField60Slow() && pcGameplayWorldmap60();
#else
    return 0;
#endif
}
int interpolationEnabled(void) { return interpolationSetting() && !interpolationStandsAside(); }
int pcInterpolationHighRate(void) { return presentationRate() > 60 && interpolationSetting(); }
int interpolationCapturing(void) { return ip.loaded && ip.building >= 0 && ip.captureReady && ip.frames[ip.building].valid; }
int pcInterpolationBuilding(void) { return interpolationCapturing(); }
int pcInterpolationGearFrame(int field) {
    return interpolationCapturing() && pcInterpolationHighRate() &&
           (field ? ip.field >= 0 && ip.field < PC_INTERP_WORLDMAP : ip.field == PC_INTERP_BATTLE);
}
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
    /* Advancing the scene invalidates decode identities. The encodings
     * still own their pixels and can be reused after an exact comparison. */
    ip.ownerCount = ip.textureCount = 0;
    for (int i = 0; i < INTERP_FRAMES; i++) {
        ip.frames[i].serial = 0;
        ip.frames[i].packPages = 0;
    }
    interpTimelineReset(&ip.timeline);
    ip.status = interpolationEnabled() ? "Waiting for compatible field frames" : "Original presentation";
    hiresPrecisionTable();
}
void interpolationSetEnabled(int enabled) {
    load(); ip.enabled = enabled != 0;
    interpolationReset();
    if (!ip.enabled) packPagesClear();
    settingsSetInt("interpolation", ip.enabled); settingsSave();
}
void interpolationShutdown(void) {
    if (ip.log) gpuLog("interpolation: texture encoding reuse %llu hits, %llu misses, %zu bytes; %llu by identity, %zu bytes",
        (unsigned long long)ip.sampleHits, (unsigned long long)ip.sampleMisses, ip.sampleBytes,
        (unsigned long long)ip.identityHits, ip.identityBytes);
    sampleClear();
    if (ip.log && ip.pagePeak) gpuLog("interpolation: retained texture pack pages peak %zu bytes", ip.pagePeak);
    packPagesClear();
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
    f->scene = (ip.scene << 1) | (field == PC_INTERP_BATTLE); f->serial = 0;
    f->x = x; f->y = y; f->w = w; f->h = h;
    ip.captureReady = gvkInterpolationReady();
    if (!ip.captureReady) interpFrameReject(f, "capture-workspace-unavailable");
    ip.replayFrozen = -1;
    ip.textureCount = 0;
    memset(ip.textureSlots, 0, sizeof(ip.textureSlots));
    memset(ip.contentSlots, 0, sizeof(ip.contentSlots));
    memset(ip.mips, 0, sizeof(ip.mips));
}
/* Some battle arenas paint their own sky and ground in the farthest OT
 * slot instead of clearing through the draw environment. The replay starts
 * there and uses the same opaque backdrop draws. */
void pcInterpolationBattleBackdrop(void) {
    if (ip.field == PC_INTERP_BATTLE && interpolationCapturing()) {
        InterpFrame* f = &ip.frames[ip.building];
        f->background = 0;
        f->cleared = 1;
    }
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
/* scene.c: the registered scene ended at this point of the snapshot, lit and
 * blurred by `effects` (NULL: neither). A picture between replays the draws
 * up to here with their depth, does the same, and replays the rest (vk.c
 * interpolationRender). */
void interpolationSceneEnd(const SceneParams* effects) {
    if (ip.building < 0) return;
    InterpFrame* f = &ip.frames[ip.building];
    f->sceneEnd = f->vertexCount;
    f->sceneBlur = effects != NULL && (effects->dof.radius > 0 || effects->dof.debug || effects->dof.tilt > 0);
    f->sceneLight = effects != NULL && effects->light.on;
    if (effects != NULL) f->fx = *effects;
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
        if (f->gearCount)
            gpuLog("interpolation: Gear rigs %u, local vertices %u", f->gearCount, f->gearVertexCount);
    }
    interpolationReport("snapshot", f->valid ? "valid" : f->rejection ? f->rejection : "unknown-capture", -1, -1);
    if (f->valid) gvkInterpolationCapture(ip.building, f);
    ip.building = -1; ip.capture = 0; ip.object = 0; ip.base = NULL;
}
void pcInterpolationObject(const void* owner, const void* model) {
    ip.object = 0; ip.base = NULL;
    ip.kind = PC_INTERP_MODEL;
    ip.gear = 0;
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
unsigned pcInterpolationGear(const void* owner, const void* model, const void* pose, unsigned size,
                             InterpGearEval* evaluate) {
    if (!interpolationCapturing()) return INTERP_GEAR_HOLD;
    InterpFrame* f = &ip.frames[ip.building];
    if (f->gearCount == INTERP_GEAR_RIGS) return INTERP_GEAR_HOLD;
    pcInterpolationObject(owner, model);
    uint64_t identity = ip.object;
    pcInterpolationObject(NULL, NULL);
    if (!identity) return INTERP_GEAR_HOLD;
    InterpGearRig* rig = &f->gears[f->gearCount];
    if (rig->capacity < size) {
        void* owned = realloc(rig->pose, size);
        if (!owned) return INTERP_GEAR_HOLD;
        rig->pose = owned;
        rig->capacity = size;
    }
    memcpy(rig->pose, pose, size);
    rig->size = size;
    rig->identity = identity;
    rig->evaluate = evaluate;
    return ++f->gearCount;
}

void pcInterpolationGearObject(const void* bone, const void* model, unsigned rig, unsigned index) {
    pcInterpolationObjectType(bone, model, PC_INTERP_MECHA);
    ip.gear = rig > INTERP_GEAR_RIGS ? INTERP_GEAR_HOLD : rig;
    ip.bone = index;
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
    if (p) { p->object = ip.object; p->face = addr - base + 1; p->discrete = 0; p->kind = ip.kind; p->placement = 0; p->gear = ip.gear; p->bone = ip.bone; }
}
void pcInterpolationDiscretePrim(void* primitive) {
    if (ip.building < 0 || !ip.loaded || !primitive) return;
    Packet* p = packetSlot(primitive);
    if (p) { p->object = p->face = 0; p->discrete = 1; p->placement = 0; p->gear = 0; }
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
    unsigned savedGear = ip.gear;
    const uint8_t* base = ip.base;
    pcInterpolationObject(owner, resource);
    if (kind == PC_INTERP_WAVE) ip.waveFrame = 1;
    if (ip.object) {
        Packet* p = packetSlot(primitive);
        if (p) { p->object = ip.object; p->face = (uint64_t)part + 1; p->discrete = 0; p->kind = kind; p->placement = 0; p->gear = 0; }
    }
    ip.object = object; ip.base = base; ip.kind = savedKind; ip.gear = savedGear;
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
int interpolationGearPacket(void) { return ip.packet && ip.packet->gear && ip.packet->gear != INTERP_GEAR_HOLD; }
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

const TextureSample* interpolationPackPage(unsigned index) {
    return &ip.pages[index].sample;
}

/* Full-resolution pages are shared by snapshots, with exact ownership
 * even if the pack reloads. Only pages absent from every valid snapshot
 * can be reused for different artwork. */
static int keepPackPage(InterpFrame* f, const TextureSample* sample, size_t count) {
    int slot = -1;
    for (int i = 0; i < INTERP_PACK_PAGES; ++i) {
        if (ip.pages[i].data && ip.pages[i].sample.revision == sample->revision) {
            slot = i;
            break;
        }
    }
    if (slot < 0) {
        size_t bytes = count * (sample->indices ? 1 : 4);
        uint64_t inUse = 0;
        for (int i = 0; i < INTERP_FRAMES; ++i)
            if (ip.frames[i].valid) inUse |= ip.frames[i].packPages;
        do {
            slot = -1;
            int needBytes = ip.pageBytes + bytes > PACK_PAGE_BYTES;
            for (int i = 0; i < INTERP_PACK_PAGES; ++i) {
                if ((inUse & (UINT64_C(1) << i)) || (needBytes && !ip.pages[i].data)) continue;
                if (slot < 0 || ip.pages[i].touched < ip.pages[slot].touched) slot = i;
            }
            if (slot < 0) return -1;
            PackPage* old = &ip.pages[slot];
            ip.pageBytes -= old->bytes;
            free(old->data);
            *old = (PackPage){0};
        } while (ip.pageBytes + bytes > PACK_PAGE_BYTES);
        PackPage* page = &ip.pages[slot];
        page->data = malloc(bytes);
        if (!page->data) return -1;
        memcpy(page->data, sample->indices ? sample->indices : sample->rgba, bytes);
        page->sample = *sample;
        if (sample->indices) {
            page->sample.indices = page->data;
            page->sample.rgba = NULL;
            for (size_t i = 0; i < count; ++i) page->used[page->data[i]] = 1;
        } else {
            page->sample.rgba = page->data;
        }
        page->bytes = bytes;
        ip.pageBytes += bytes;
        if (ip.pageBytes > ip.pagePeak) ip.pagePeak = ip.pageBytes;
    }
    ip.pages[slot].touched = ++ip.sampleClock;
    f->packPages |= UINT64_C(1) << slot;
    return slot;
}

static int storePackTexture(InterpFrame* f, Texture* t, const TextureSample* sample,
                            const DecodedTexture* palette, size_t count) {
    int slot = keepPackPage(f, sample, count);
    if (slot < 0) return 0;
    t->format = INTERP_SAMPLE_RESIDENT;
    t->offset = (uint32_t)slot;
    t->material = sample->revision;
    if (!sample->indices) return 1;

    uint32_t words[257] = {(uint32_t)slot};
    uint16_t used[256] = {0};
    for (int i = 0; i < palette->paletteSize; ++i) {
        uint16_t c = palette->palette[i];
        if (ip.pages[slot].used[i]) used[i] = c;
        if (!c) continue;
        uint32_t rgba = (c & 0x8000 ? 128u : 255u) << 24;
        for (int k = 0; k < 3; ++k) {
            unsigned channel = (c >> (k * 5)) & 31;
            rgba |= ((channel << 3) | (channel >> 2)) << (k * 8);
        }
        words[1 + i] = rgba;
    }
    t->material ^= hashContent((const uint8_t*)used, (size_t)palette->paletteSize * sizeof(*used));
    t->format |= INTERP_SAMPLE_INDEXED | INTERP_SAMPLE_PALETTE | (uint32_t)(palette->paletteSize - 1) << 24;
    return interpFrameWords(f, words, 1 + (uint32_t)palette->paletteSize, &t->offset);
}

static MipTexture* replayMip(MipKey key, int* fresh) {
    uint64_t hash = key.revision * UINT64_C(0x9e3779b97f4a7c15) ^ key.palette;
    hash ^= ((uint64_t)key.origin << 32 | key.size) * UINT64_C(0xbf58476d1ce4e5b9);
    unsigned slot = (unsigned)((hash ^ (hash >> 32)) >> 12) & (MIP_SLOTS - 1);
    *fresh = 0;
    for (int i = 0; i < 8; ++i, slot = (slot + 1) & (MIP_SLOTS - 1)) {
        MipTexture* m = &ip.mips[slot];
        if (m->key.revision == key.revision && m->key.palette == key.palette &&
            m->key.origin == key.origin && m->key.size == key.size) return m;
        if (m->key.revision) continue;
        *m = (MipTexture){.key = key, .offset = INTERP_MIP_BASE_ONLY};
        *fresh = 1;
        return m;
    }
    return NULL;
}

static void storeMip(InterpFrame* f, MipTexture* m, const TextureSample* chain, int includesBase, size_t count) {
    if (!count) return;
    uint64_t key = chain->revision ^ UINT64_C(0x8e4c110d12875a91);
    IdentityEncoding* known = identityFind(key, count);
    size_t baseBytes = includesBase ? (size_t)chain->w * chain->h * chain->scale * chain->scale * 4 : 0;
    if (known) {
        if (interpFrameMipWords(f, known->words, (uint32_t)known->n, &m->offset)) m->format = known->format;
    } else if (interpFrameMipSample(f, chain->rgba + baseBytes, count, &m->offset, &m->format)) {
        identityKeep(key, count, f->mipWords + m->offset, f->mipCount - m->offset, m->format, 0);
    }
}

static MipTexture* packMip(InterpFrame* f, const TextureSample* sample, TextureRequest patch, const DecodedTexture* palette) {
    MipKey key = {.revision = sample->revision};
    int w = sample->w, h = sample->h;
    if (sample->indices) {
        key.palette = palette->paletteKey;
        key.origin = (uint32_t)(patch.u - sample->u) | (uint32_t)(patch.v - sample->v) << 16;
        w = patch.w; h = patch.h;
        key.size = (uint32_t)w | (uint32_t)h << 16;
    }
    int fresh;
    MipTexture* m = replayMip(key, &fresh);
    if (!fresh) return m;
    w *= sample->scale; h *= sample->scale;
    size_t count = textureMipmapPixels(w, h) - (size_t)w * h;
    /* Even a single-colour palette needs a byte per lower texel. Avoid
     * building a large tail that cannot fit, while reusing earlier tails. */
    if ((count + 3) / 4 > INTERP_MAX_MIP_WORDS - f->mipCount) return m;
    TextureSample chain;
    if (textureMipmapReplacement(sample, patch, palette, &chain)) storeMip(f, m, &chain, 0, count);
    return m;
}

static MipTexture* nativeMip(InterpFrame* f, TextureRequest patch) {
    TextureSample chain;
    if (!textureMipmapSample(textureCacheGet(textureRequestSnap(patch)), patch, &chain)) return NULL;
    int fresh;
    MipTexture* m = replayMip((MipKey){.revision = chain.revision}, &fresh);
    if (fresh) storeMip(f, m, &chain, 1, textureMipmapPixels(chain.w, chain.h) - (size_t)chain.w * chain.h);
    return m;
}

static void textureAnisotropic(InterpFrame* f, HiVertex* v, int count, const TextureSample* sample,
                               const DecodedTexture* decoded, int filter, int pack,
                               const InterpTextureFootprint* footprints) {
    int taps = hiresAnisotropyNow();
    for (int first = 0; first < count; first += 3) {
        const InterpTextureFootprint* fp = &footprints[first / 3];
        if (fp->footprint2 <= 0) continue;
        /* Keep the same authored bounds for this half across native
         * frames, even when it crosses the minification threshold. */
        for (int k = first; k < first + 3; ++k) {
            v[k].uvMin = fp->uvMin;
            v[k].uvMax = fp->uvMax;
        }
        if (fp->footprint2 * sample->scale * sample->scale < 1.0f || fp->uvMin == fp->uvMax) continue;
        int left = (int16_t)fp->uvMin, top = (int16_t)(fp->uvMin >> 16);
        int right = (int16_t)fp->uvMax + 1, bottom = (int16_t)(fp->uvMax >> 16) + 1;
        if (left < sample->u) left = sample->u;
        if (top < sample->v) top = sample->v;
        if (right > sample->u + sample->w) right = sample->u + sample->w;
        if (bottom > sample->v + sample->h) bottom = sample->v + sample->h;
        if (left >= right || top >= bottom) continue;
        TextureRequest patch = {.tpage = (uint16_t)v[first].tex, .clut = (uint16_t)(v[first].tex >> 16),
                                .window = v[first].texWindow, .u = left, .v = top,
                                .w = right - left, .h = bottom - top};
        const MipTexture* mip = pack ? packMip(f, sample, patch, decoded) : nativeMip(f, patch);
        unsigned sampling = TEXTURE_FILTER_ANISOTROPIC | taps |
            (filter == TEXTURE_FILTER_LINEAR ? TEXTURE_FILTER_ANISOTROPIC_LINEAR : 0) |
            (pack ? TEXTURE_FILTER_ANISOTROPIC_PACK : 0);
        for (int k = first; k < first + 3; ++k) {
            v[k].sample[3] = (v[k].sample[3] & ~255u) | (sampling + 1) | INTERP_SAMPLE_MIPS;
            v[k].wideEdge = (mip ? mip->offset : INTERP_MIP_BASE_ONLY) | (pack && !sample->indices ? INTERP_MIP_SHARED : 0);
            v[k].wideTex = mip ? mip->format : 0;
        }
    }
}

static int texture(InterpFrame* f, HiVertex* v, int count, int kind, uint64_t* material,
                   const InterpTextureFootprint* footprints) {
    uint32_t tex = v[0].tex;
    const DecodedTexture* d;
    TextureSample sample;
    int filter;
    int pack = 0, aniso = 0;
    uint64_t nativeRevision = 0;
    const DecodedTexture* packPalette = NULL;
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
    TextureRequest native = PC_TEX_IS_PORTRAIT(kind) ? patch : textureRequestSnap(patch);
    d = textureCacheGet(native);
    const char* reason = !d || !d->rgba ? "unavailable-texture" : gp0InterpolationTextureReason(d);
    if (reason) {
        interpolationReject(reason);
        if (ip.log) gpuLog("interpolation: unsupported texture %08x uv %d,%d %dx%d", tex, patch.u, patch.v, patch.w, patch.h);
        return 0;
    } /* rendered captures/feedback are unsupported */
    sample = (TextureSample){.rgba = d->rgba, .revision = d->revision, .u = d->request.u, .v = d->request.v,
                             .w = d->request.w, .h = d->request.h, .scale = 1};
    nativeRevision = d->revision;
    filter = gvkHiScale() ? textureFilter(kind) : TEXTURE_FILTER_NEAREST;
    if (gvkHiScale()) {
        const DecodedTexture* enhanced = d;
        if (kind == PC_TEX_3D) {
            TextureRequest stable = textureRequestSnapStable(patch);
            if (memcmp(&stable, &native, sizeof(stable))) enhanced = textureCacheGet(stable);
        }
        int sampled = PC_TEX_IS_PORTRAIT(kind) && !textureUsePack(kind) ? portraitPackClassSample(enhanced,patch,kind,&sample) :
            (textureUsePack(kind)?texturePackSample(enhanced,patch,filter,&sample):textureUseSpriteCel(kind)?textureSpriteCelSample(enhanced,patch,filter,&sample):0);
        pack = sampled && textureUsePack(kind);
        packPalette = enhanced;
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
        if (sampled) nativeRevision = 0;
        else if (enhanced && enhanced->rgba) {
            nativeRevision = enhanced->revision;
            sample=(TextureSample){enhanced->rgba,enhanced->revision,enhanced->request.u,enhanced->request.v,
                enhanced->request.w,enhanced->request.h,1,NULL,0};
        }
        aniso = (pack || !sampled) && footprints && kind == PC_TEX_3D && filter <= TEXTURE_FILTER_LINEAR &&
            (footprints[0].footprint2 > 0 || (count == 6 && footprints[1].footprint2 > 0));
        /* Replay evaluates Scale2x from the shared original texels. Keeping
         * a separate enlarged, edge-clamped image for every face duplicates
         * the same upload hundreds of times in complex Gear/story scenes. */
    }
sampled:
    if (sample.scale < 1 || sample.scale > 8 || sample.w < 1 || sample.h < 1 || sample.w > 256 || sample.h > 256) {
        interpolationReject("replacement-size"); return 0;
    }
    size_t pixels = (size_t)sample.w * sample.h * sample.scale * sample.scale;
    if (!sample.indices && !sample.rgba) { interpolationReject("unavailable-replacement"); return 0; }
    /* Share large, complete 3D pages with the live GPU cache. Sprites, HUD
     * and the smaller samples still use the compact snapshot encoding. */
    int resident = pack && kind == PC_TEX_3D && sample.keep && pixels >= 256u * 1024u;
    TextureRequest request = d->request;
    request.u = sample.u; request.v = sample.v; request.w = sample.w; request.h = sample.h;
    uint64_t revision = sample.revision ^ (sample.indices ? (resident ? packPalette : d)->paletteKey : 0);
    Texture* t = NULL;
    uint64_t requestKey = hashContent((const uint8_t*)&request, sizeof(request)) ^ (uint64_t)sample.scale;
    uint64_t key = requestKey ^ revision;
    unsigned slot = (unsigned)((key ^ (key >> 32)) * UINT64_C(0x9e3779b97f4a7c15)) & (TEXTURE_SLOTS - 1);
    while (ip.textureSlots[slot]) {
        Texture* p = &ip.textures[ip.textureSlots[slot] - 1];
        if (p->revision == revision && p->scale == sample.scale && !memcmp(&p->request, &request, sizeof(p->request))) { t = p; break; }
        slot = (slot + 1) & (TEXTURE_SLOTS - 1);
    }
    if (!t && resident) {
        if (ip.textureCount == TEXTURES) { interpolationReject("texture-count"); return 0; }
        t = &ip.textures[ip.textureCount];
        *t = (Texture){.revision = revision, .request = request, .scale = sample.scale};
        if (!storePackTexture(f, t, &sample, packPalette, pixels)) {
            interpolationReject("pack-page-capacity"); return 0;
        }
        ip.textureSlots[slot] = (uint32_t)++ip.textureCount;
    }
    if (!t && pixels > INTERP_MAX_PIXELS) { interpolationReject("texture-capacity"); return 0; }
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
        /* The exact fallback also finds matching pixels across different
         * CLUT revisions, such as when only an unused entry changed. */
        uint64_t sampleKey = requestKey;
        SampleEncoding* encoded = sampleFind(sampleKey, sample.rgba, pixels, nativeRevision);
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
        if (!encoded) sampleKeep(sampleKey, sample.rgba, pixels, f->pixels + offset, words, format, content, nativeRevision);
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
    if (aniso) textureAnisotropic(f, v, count, &sample, packPalette, filter, pack, footprints);
    return 1;
}
void interpolationDrawPolygons(const HiVertex* vertices, int count, int kind,
                               int wide, int shiftX, int shiftY, unsigned piece,
                               const InterpTextureFootprint* footprints, InterpGearVertex* gearVertices) {
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
    if (ip.field != PC_INTERP_BATTLE && !capture && !object && kind == PC_TEX_3D && !(v[0].mode & DRAW_FILL)) {
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
    if (!capture && !frozen && (v[0].mode & DRAW_TEXTURED) && !texture(f, v, count, kind, &material, footprints)) { interpFrameReject(f, "unsupported-texture"); return; }
    if (interpFrameDraw(f, v, count, object, face, material)) {
        f->draws[f->drawCount - 1].kind = ip.packet ? ip.packet->kind : PC_INTERP_MODEL;
        f->draws[f->drawCount - 1].collapse = ip.packet && ip.packet->collapse;
        if (ip.packet && ip.packet->gear) {
            if (gearVertices) for (int i = 0; i < count; i++) {
                gearVertices[i].center[0] -= f->viewX;
                gearVertices[i].center[1] -= f->viewY;
            }
            interpFrameGearDraw(f, ip.packet->gear, ip.packet->bone, gearVertices);
        }
        if (ip.packet && ip.packet->placement)
            interpFramePlacement(f, ip.packet->anchor[0] + f->x + shiftX - f->viewX,
                                 ip.packet->anchor[1] + f->y + shiftY - f->viewY, ip.packet->anchor[2]);
    }
}
void interpolationDraw(const HiVertex* vertices, int count, int kind,
                       int wide, int shiftX, int shiftY, unsigned piece) {
    interpolationDrawPolygons(vertices, count, kind, wide, shiftX, shiftY, piece, NULL, NULL);
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
        unsigned gears = f->gearCount;
        interpFrameClear(f);
        f->gearCount = gears;
        if (!ip.captureReady) interpFrameReject(f, "capture-workspace-unavailable");
        ip.textureCount = 0; memset(ip.textureSlots, 0, sizeof(ip.textureSlots));
        memset(ip.contentSlots, 0, sizeof(ip.contentSlots));
        memset(ip.mips, 0, sizeof(ip.mips));
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
    ip.status = "Interpolated geometry (one native frame delay)";
    return &ip.frames[prev];
}
const InterpFrame* interpolationPicture(const GpuDisplay* d, double vblank, int* before, int* after, double* alpha) {
    return picture(d, vblank, before, after, alpha, 0);
}
const InterpFrame* interpolationSample(const GpuDisplay* d, double vblank, int* before, int* after, double* alpha) {
    return picture(d, vblank, before, after, alpha, 1);
}
