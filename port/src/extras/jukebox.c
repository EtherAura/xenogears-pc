/*
 * The jukebox's engine (jukebox.h): the game's sound driver in a sandbox and
 * a world of its own.
 */

#include "jukebox.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common.h"
#include "main/memory.h"
#include "main/sound.h"
#include "sandbox.h"
#include "cd/cd_internal.h"
#include "spu/spu_internal.h"
#include "system/world.h"

/* The game's sound driver (src/main/sound.c, sequencer.c, sound_data.c). */
extern s16 musicStatusFlag; /* bit 3: setSoundError has run */
extern u8 opcodeSizes[0x80];
extern u8 noteDeltaTimeTable[];
void shutdownSoundSystem(void);
s32 getPlayingSoundEffectMask(s32 id);
void loadSequence(void* data);
void unloadSequence(void* data);
SoundInstance* createMusicInstance(void* data);
void startMusicInstance(SoundInstance* inst, s32 a, s32 b);
void stopAllSounds(void);
void playSoundEffect(s32 id);
void setMusicInstanceTrackMuteMask(SoundInstance* inst, u32 mask);
s16* getMusicInstanceLoopCount(SoundInstance* inst);

#define HEAP_BYTES (4u << 20) /* scores, effect banks, and a bank being loaded */
#define MAX_SEDS 8

typedef struct {
    void* data; /* in the sandbox's heap, linked into the driver's list */
    int size;
    int id;
} Seds;

struct XgJbEngine {
    XgSandbox* sb;
    HwWorld* world;
    SoundInstance* music;
    void* score;      /* the score's copy in the sandbox's heap */
    double started;   /* world time the score started */
    Seds seds[MAX_SEDS];
    /* xgJbRender's sink: the frames asked for, then a spill for the next call */
    int16_t* out;
    int want, have;
    int16_t* spill;
    int spillCount, spillCap;
    void* xa; /* the XA decoder's state (cd/xa.c), this engine's own */
};

static uint32_t rd32(const uint8_t* p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static uint16_t rd16(const uint8_t* p) {
    return (uint16_t)(p[0] | p[1] << 8);
}

static void enter(XgJbEngine* e) {
    xgSandboxEnter(e->sb);
    hwWorldEnter(e->world);
}

static void leave(XgJbEngine* e) {
    hwWorldLeave(e->world);
    xgSandboxLeave(e->sb);
}

XgJbEngine* xgJbCreate(void) {
    XgJbEngine* e = calloc(1, sizeof(XgJbEngine));
    if (e == NULL) {
        return NULL;
    }
    e->sb = xgSandboxCreate(NULL, HEAP_BYTES);
    e->world = hwWorldCreate();
    e->xa = malloc(xaPartSize());
    if (e->sb == NULL || e->world == NULL || e->xa == NULL) {
        xgJbDestroy(e);
        return NULL;
    }
    xaPartInit(e->xa);
    /* kernelMain's set-up (SpuInit is the world's own: hwWorldCreate). */
    enter(e);
    initSoundSystem(0);
    leave(e);
    return e;
}

void xgJbDestroy(XgJbEngine* e) {
    if (e == NULL) {
        return;
    }
    if (e->sb != NULL && e->world != NULL) {
        /* The driver's own shutdown: its event closed, its counter stopped,
         * every voice released (all of it the world's). */
        enter(e);
        shutdownSoundSystem();
        leave(e);
    }
    hwWorldDestroy(e->world);
    xgSandboxDestroy(e->sb);
    free(e->spill);
    free(e->xa);
    free(e);
}

/* ---- sample banks ------------------------------------------------------------------------------ */

static void unloadBank(Wds* w) {
    unloadWds(w);
}

int xgJbLoadBank(XgJbEngine* e, const uint8_t* wds, int size) {
    if (size < 0x30 || memcmp(wds, "wds ", 4) != 0) {
        return 0;
    }
    int bank = rd16(wds + 0x20);
    uint32_t at = rd32(wds + 0x28), bytes = rd32(wds + 0x14);
    uint32_t head = rd32(wds + 0x10), adpcm = rd32(wds + 0x18);
    if (head < 0x30 || adpcm > (uint32_t)size || bytes > (uint32_t)size - adpcm) {
        return 0; /* a header that would read past the file */
    }
    int ok = 0;
    enter(e);
    for (Wds* w = pLoadedWdsLinkedList; w != NULL;) {
        Wds* next = w->m2C_next;
        uint32_t wAt = (uint32_t)w->m28_adpcmAddress, wEnd = wAt + (uint32_t)w->m14_adpcmSize;
        if (w->m20_bankNumber == bank && wAt == at && (uint32_t)w->m14_adpcmSize == bytes) {
            ok = 1; /* already there */
            break;
        }
        if (w->m20_bankNumber == bank || (at < wEnd && wAt < at + bytes)) {
            unloadBank(w);
        }
        w = next;
    }
    if (!ok) {
        void* copy = xgSandboxCopy(wds, (size_t)size);
        if (copy != NULL) {
            ok = loadWds((Wds*)copy, 0) != NULL;
            waitForMusic(0x10); /* the transfers done (the world's time passes) */
            freeHeap(copy);
        }
    }
    leave(e);
    return ok;
}

int xgJbHasBank(XgJbEngine* e, int bank, uint32_t addr, uint32_t bytes) {
    int has = 0;
    enter(e);
    for (Wds* w = pLoadedWdsLinkedList; w != NULL && !has; w = w->m2C_next) {
        has = w->m20_bankNumber == bank && (uint32_t)w->m28_adpcmAddress == addr && (uint32_t)w->m14_adpcmSize == bytes;
    }
    leave(e);
    return has;
}

int xgJbBanks(XgJbEngine* e, int* banks, int max) {
    int n = 0;
    enter(e);
    for (Wds* w = pLoadedWdsLinkedList; w != NULL; w = w->m2C_next) {
        if (n < max) {
            banks[n] = w->m20_bankNumber;
        }
        n++;
    }
    leave(e);
    return n < max ? n : max;
}

/* ---- music ----------------------------------------------------------------------------------------- */

/* Inside: the score stopped and freed. */
static void dropScore(XgJbEngine* e) {
    if (e->music != NULL) {
        freeSoundInstance(e->music);
        e->music = NULL;
    }
    if (e->score != NULL) {
        freeHeap(e->score);
        e->score = NULL;
    }
}

int xgJbPlay(XgJbEngine* e, const uint8_t* smds, int size) {
    if (size < 0x24 || memcmp(smds, "smds", 4) != 0 || smds[0x14] == 0 ||
        0x22 + 2 * smds[0x14] > size) {
        return 0;
    }
    enter(e);
    dropScore(e);
    e->score = xgSandboxCopy(smds, (size_t)size);
    if (e->score != NULL) {
        e->music = createMusicInstance(e->score);
        if (e->music != NULL) {
            startMusicInstance(e->music, 0x7F, 0);
            e->started = hwWorldTime();
        } else {
            freeHeap(e->score);
            e->score = NULL;
        }
    }
    int ok = e->music != NULL;
    leave(e);
    return ok;
}

void xgJbStop(XgJbEngine* e) {
    enter(e);
    dropScore(e);
    stopAllSounds();
    leave(e);
}

void xgJbFade(XgJbEngine* e, double seconds) {
    enter(e);
    if (e->music != NULL) {
        int ticks = (int)(seconds * 240 + 0.5);
        playBattleMusic(e->music, 0, ticks > 0 ? ticks : 1);
    }
    leave(e);
}

void xgJbMute(XgJbEngine* e, uint32_t mask) {
    enter(e);
    if (e->music != NULL) {
        setMusicInstanceTrackMuteMask(e->music, mask);
    }
    leave(e);
}

/* ---- effects ----------------------------------------------------------------------------------------- */

/* Inside: the bank loaded (once; another with its id goes first). */
static Seds* loadSeds(XgJbEngine* e, const uint8_t* seds, int size) {
    int id = rd16(seds + 0x14);
    Seds* slot = NULL;
    for (int i = 0; i < MAX_SEDS; i++) {
        Seds* s = &e->seds[i];
        if (s->data != NULL && s->id == id && (s->size != size || memcmp(s->data, seds, (size_t)size) != 0)) {
            /* Another bank with this id: the driver keeps one per id. */
            stopAllSounds();
            unloadSequence(s->data);
            freeHeap(s->data);
            s->data = NULL;
        }
        if (s->data != NULL && s->id == id) {
            slot = s;
        }
    }
    if (slot != NULL) {
        return slot;
    }
    for (int i = 0; i < MAX_SEDS && slot == NULL; i++) {
        if (e->seds[i].data == NULL) {
            slot = &e->seds[i];
        }
    }
    if (slot == NULL) { /* full: the oldest goes (never the first, the field's) */
        slot = &e->seds[1];
        stopAllSounds();
        unloadSequence(slot->data);
        freeHeap(slot->data);
        memmove(&e->seds[1], &e->seds[2], sizeof(Seds) * (MAX_SEDS - 2));
        slot = &e->seds[MAX_SEDS - 1];
        slot->data = NULL;
    }
    slot->data = xgSandboxCopy(seds, (size_t)size);
    if (slot->data == NULL) {
        return NULL;
    }
    slot->size = size;
    slot->id = id;
    /* setSoundError reports an error once (bit 3): cleared, it tells whether
     * this load was refused (a bad header, a checksum). */
    musicStatusFlag &= ~8;
    loadSequence(slot->data);
    waitForMusic(0x10);
    if (musicStatusFlag & 8) {
        freeHeap(slot->data);
        slot->data = NULL;
        return NULL;
    }
    return slot;
}

static int sedsOk(const uint8_t* seds, int size) {
    return size >= 0x20 && memcmp(seds, "seds", 4) == 0 && rd32(seds + 8) <= (uint32_t)size;
}

int xgJbLoadEffects(XgJbEngine* e, const uint8_t* seds, int size) {
    if (!sedsOk(seds, size)) {
        return 0;
    }
    enter(e);
    int ok = loadSeds(e, seds, size) != NULL;
    leave(e);
    return ok;
}

int xgJbEffect(XgJbEngine* e, const uint8_t* seds, int size, int effect) {
    if (!sedsOk(seds, size) || effect < 0 || effect >= rd16(seds + 0x12) || 0x20 + 4 * (effect + 1) > size) {
        return 0;
    }
    enter(e);
    int ok = loadSeds(e, seds, size) != NULL;
    if (ok) {
        playSoundEffect((s32)((uint32_t)rd16(seds + 0x14) << 16 | (uint32_t)effect));
    }
    leave(e);
    return ok;
}

/* ---- time ------------------------------------------------------------------------------------------------ */

static void sink(void* user, const int16_t* frames, uint32_t count) {
    XgJbEngine* e = user;
    uint32_t take = 0;
    if (e->have < e->want) {
        take = (uint32_t)(e->want - e->have);
        take = take < count ? take : count;
        if (e->out != NULL) {
            memcpy(e->out + e->have * 2, frames, take * 4);
        }
        e->have += (int)take;
    }
    if (take < count) {
        int extra = (int)(count - take);
        if (e->spillCount + extra > e->spillCap) {
            int cap = (e->spillCount + extra) * 2;
            int16_t* p = realloc(e->spill, (size_t)cap * 4);
            if (p == NULL) {
                return;
            }
            e->spill = p;
            e->spillCap = cap;
        }
        memcpy(e->spill + e->spillCount * 2, frames + take * 2, (size_t)extra * 4);
        e->spillCount += extra;
    }
}

/* Inside: one tick's time (or less) passed. */
static void tick(XgJbEngine* e, double left) {
    double step = 1.0 / 240;
    hwWorldRun(left < step ? left : step);
}

static int scoreEnded(XgJbEngine* e) {
    return e->music == NULL || !(e->music->m10_flags & 0x8000);
}

int xgJbRender(XgJbEngine* e, int16_t* out, int frames, int untilLoops, int untilEnd) {
    e->out = out;
    e->want = frames;
    e->have = 0;
    if (e->spillCount > 0) {
        int take = e->spillCount < frames ? e->spillCount : frames;
        if (out != NULL) {
            memcpy(out, e->spill, (size_t)take * 4);
        }
        memmove(e->spill, e->spill + take * 2, (size_t)(e->spillCount - take) * 4);
        e->spillCount -= take;
        e->have = take;
    }
    enter(e);
    hwWorldSetSink(sink, e);
    /* The SPU's samples are whole sample clocks of the world's time: a run
     * can fall a frame short of its time, the next tick makes it up. */
    for (int guard = 0; e->have < frames && guard < frames + 16; guard++) {
        tick(e, (frames - e->have + 0.5) / XG_JB_RATE);
        if ((untilLoops > 0 && e->music != NULL && *getMusicInstanceLoopCount(e->music) >= untilLoops) ||
            (untilEnd && scoreEnded(e))) {
            break;
        }
    }
    hwWorldSetSink(NULL, NULL);
    leave(e);
    e->out = NULL;
    return e->have;
}

int xgJbMeasure(XgJbEngine* e, double seconds, XgJbMarks* m) {
    int done = 0;
    enter(e);
    hwWorldSetSpu(0);
    double end = hwWorldTime() + seconds;
    while (!done && hwWorldTime() < end) {
        tick(e, end - hwWorldTime());
        double t = hwWorldTime() - e->started;
        int loops = e->music != NULL ? *getMusicInstanceLoopCount(e->music) : 0;
        for (int i = 0; i < 2; i++) {
            if (loops >= i + 1 && m->loop[i] < 0) {
                m->loop[i] = t;
            }
        }
        if (scoreEnded(e) && m->end < 0) {
            m->end = t;
        }
        done = m->loop[1] >= 0 || m->end >= 0;
    }
    hwWorldSetSpu(1);
    leave(e);
    return done;
}

void xgJbStatus(XgJbEngine* e, XgJbStatus* s) {
    memset(s, 0, sizeof(*s));
    enter(e);
    SoundInstance* m = e->music;
    if (m != NULL) {
        s->music = (m->m10_flags & 0x8000) != 0;
        s->time = hwWorldTime() - e->started;
        s->loops = *getMusicInstanceLoopCount(m);
        s->measure = m->m32_measure;
        s->beat = m->m34_beat;
        s->tempo = (int)(m->m58.m58_tempo >> 16);
        /* Ticks a second: 240 audio ticks, each tempo x scale / 0x10000 of a
         * score tick (scale 0x100 = 1); 48 score ticks a quarter note. */
        s->bpm = s->tempo * ((m->m64_tempo.m0_value >> 16) / 256.0) * 240.0 / 256.0 / 48.0 * 60.0;
        s->volume = (int)(m->m70_volume.m0_value >> 24);
        s->tracks = m->m14_trackCount < XG_JB_TRACKS ? m->m14_trackCount : XG_JB_TRACKS;
        for (int i = 0; i < s->tracks; i++) {
            SoundTrack* t = &m->m94_tracks[i];
            int v = t->m27_voice;
            s->track[i].active = t->m0_flags != 0;
            s->track[i].muted = (m->m4C_muteMask >> i) & 1;
            s->track[i].voice = v < 24 ? v : -1;
            s->track[i].bank = t->m25_bank;
            s->track[i].instrument = t->m26_instrument;
            s->track[i].key = t->m64_key;
            if (v < 24) {
                s->track[i].level = spu.voices[v].level;
                s->track[i].heard = t->m0_flags != 0 && !(t->m0_flags & 0x20) && spu.voices[v].level > 0;
            }
        }
    }
    s->effects = getPlayingSoundEffectMask(-1) != 0;
    for (int v = 0; v < 24; v++) {
        s->voiceLevel[v] = spu.voices[v].level;
    }
    s->error = (musicStatusFlag & 8) != 0;
    leave(e);
}

/* ---- scores, read --------------------------------------------------------------------------------------- */

static void addBank(int* banks, int* n, int max, int bank) {
    for (int i = 0; i < *n && i < max; i++) {
        if (banks[i] == bank) {
            return;
        }
    }
    if (*n < max) {
        banks[*n] = bank;
    }
    (*n)++;
}

/* One track's events, from `at` to its end (op 0x90): the banks it switches
 * to. The operand sizes are the driver's own (opcodeSizes, op byte
 * included); a note is a velocity, a length-and-key byte and, for the
 * lengths the table has as 0, a byte of its own. */
static void walkTrack(const uint8_t* d, int size, int at, int* banks, int* n, int max) {
    int p = at;
    for (int steps = 0; p < size && steps < 100000; steps++) {
        int op = d[p];
        if (op < 0x80) {
            if (p + 1 >= size) {
                return;
            }
            p += noteDeltaTimeTable[d[p + 1]] == 0 ? 3 : 2;
            continue;
        }
        if (op == 0x90) {
            return;
        }
        if ((op == 0xFC || op == 0xFE) && p + 1 < size) {
            addBank(banks, n, max, d[p + 1]);
        }
        int len = opcodeSizes[op - 0x80];
        p += len > 0 ? len : 1;
    }
}

int xgJbScoreBanks(const uint8_t* d, int size, int* banks, int max) {
    int n = 0;
    if (size >= 0x24 && memcmp(d, "smds", 4) == 0) {
        addBank(banks, &n, max, rd16(d + 0x16));
        int tracks = d[0x14];
        for (int t = 0; t < tracks && 0x22 + 2 * t + 2 <= size; t++) {
            int at = rd16(d + 0x22 + 2 * t);
            if (at != 0) {
                walkTrack(d, size, at, banks, &n, max);
            }
        }
    } else if (size >= 0x20 && memcmp(d, "seds", 4) == 0) {
        addBank(banks, &n, max, rd16(d + 0x16));
        int effects = rd16(d + 0x12);
        for (int i = 0; i < effects * 2 && 0x20 + 2 * i + 2 <= size; i++) {
            int at = rd16(d + 0x20 + 2 * i);
            if (at != 0) {
                walkTrack(d, size, at, banks, &n, max);
            }
        }
    }
    return n < max ? n : max;
}

int xgJbEffectBanks(const uint8_t* d, int size, int effect, int* banks, int max) {
    int n = 0;
    if (size < 0x20 || memcmp(d, "seds", 4) != 0 || effect < 0 || effect >= rd16(d + 0x12)) {
        return 0;
    }
    addBank(banks, &n, max, rd16(d + 0x16));
    for (int i = 0; i < 2 && 0x20 + 4 * effect + 2 * i + 2 <= size; i++) {
        int at = rd16(d + 0x20 + 4 * effect + 2 * i);
        if (at != 0) {
            walkTrack(d, size, at, banks, &n, max);
        }
    }
    return n < max ? n : max;
}

/* ---- CD audio ------------------------------------------------------------------------------------- */

void xgJbXa(XgJbEngine* e, const uint8_t* raw, int count) {
    hwWorldEnter(e->world);
    xaPartSwap(e->xa);
    for (int i = 0; i < count; i++) {
        xaSubmitSector(raw + (size_t)i * CD_RAW_SECTOR);
    }
    xaPartSwap(e->xa);
    hwWorldLeave(e->world);
}

int xgJbCdQueued(XgJbEngine* e) {
    hwWorldEnter(e->world);
    int n = (int)spuCdQueued();
    hwWorldLeave(e->world);
    return n;
}
