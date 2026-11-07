/*
 * Sound driver, first file (main executable 0x80037B88-0x8003A2D4): init and
 * shutdown, WDS/sequence lists, output mode, reverb, the sound arena, SPU RAM
 * blocks, music instances and the play/stop entry points. GCC 2.6.0, ASPSX
 * 2.34, -G8 (setCdMixVolume only matches with -G8); no read-only data.
 *
 * File boundaries (agent SOUNDB): the driver starts at initSoundSystem, after
 * battle_env.c's loader (the only code in 0x800379D8-0x8003F738 that calls
 * game code). It ends where the sound-effect entry points stop: they call
 * playSoundEffectSub through a prototype with short ch/vol/pan (they
 * sign-extend), while its definition in sequencer.c keeps them 32-bit, which
 * GCC 2.6.0 does not accept in one file. The driver's .data and small
 * globals belong to another object (nothing reaches them through $gp).
 */

#include "common.h"
#include "main/sound.h"
#include "psyq/libspu.h"

/* Sound arena block header (the 0x10 bytes in front of every allocation). */
typedef struct ArenaBlock {
    /* 0x0 */ u16 m0_flags;
    /* 0x2 */ u16 m2;
    /* 0x4 */ s32 m4;
    /* 0x8 */ u8* m8_end;
    /* 0xC */ struct ArenaBlock* mC_next;
} ArenaBlock;

/* The arena's block header: 0x10 bytes on the PS1, two pointers more on the PC. */
#define ARENA_HEADER ((s32)((sizeof(ArenaBlock) + 0xF) & ~0xF))

/* The arena: the PC's sound instances and WDS copies hold 8-byte pointers, so
 * it gets twice the PS1's 0x6300 bytes. */
static u8 pcSoundArena[0xC600] __attribute__((aligned(16)));
#define SOUND_ARENA pcSoundArena
#define SOUND_ARENA_SIZE ((s32)sizeof(pcSoundArena))

/* A loaded WDS keeps a copy of its header in the arena: the disc bytes on the
 * PS1, the native layout on the PC (include/main/sound.h). */
#define WDS_COPY_SIZE(wds) pcWdsSize(wds)
#define WDS_COPY(copy, wds) pcWdsConvert(copy, wds)

/* SPU RAM allocation block (Noah: structsLoadedSample), 12 of them. */
typedef struct SpuBlock {
    /* 0x0 */ u8 m0_flags;
    /* 0x1 */ u8 m1_tag;
    /* 0x2 */ s16 m2_next;
    /* 0x4 */ s32 m4_addr;
    /* 0x8 */ s32 m8_size;
    /* 0xC */ s32 mC;
} SpuBlock;

typedef struct {
    /* 0x00 */ u16 m0_flags;
    /* 0x02 */ u8 m2[0x10];
    /* 0x12 */ u16 m12_volume;
    /* 0x14 */ u8 m14[0x22];
    /* 0x36 */ s16 m36_voice0Dirty;
    /* 0x38 */ s16 m38_voice0VolumeLeft;
    /* 0x3A */ s16 m3A_voice0VolumeRight;
    /* 0x3C */ u8 m3C[0x26];
    /* 0x62 */ s16 m62_voice1Dirty;
    /* 0x64 */ s16 m64_voice1VolumeLeft;
    /* 0x66 */ s16 m66_voice1VolumeRight;
} Snd518;

/* libcd CdlATV */
typedef struct {
    u8 val0;
    u8 val1;
    u8 val2;
    u8 val3;
} SndCdAtv;

/* libspu SpuReverbAttr */
typedef struct {
    u32 mask;
    s32 mode;
    SndVolume depth;
    s32 delay;
    s32 feedback;
} SndReverbAttr;

/* SPU transfer queue entry (8 entries at loadWdsSub0SubVar1), 0x14 bytes. */
typedef struct SpuXfer {
    /* 0x00 */ u16 m0_mode;
    /* 0x02 */ u16 m2;
    /* 0x04 */ u8* m4_src;
    /* 0x08 */ s32 m8_spuAddr;
    /* 0x0C */ s32 mC_size;
    /* 0x10 */ void (*m10_callback)(void);
} SpuXfer;

extern s16 musicStatusFlag;   /* musicStatusFlag */
extern s32 audioTickEvent;   /* audio tick event */
extern s32 sendSamplesSpuAddr;   /* SPU transfer address */
extern s32 sendSamplesBytesLeft;   /* SPU bytes left */
extern SoundVoice* spuVoiceOwners[24];
extern SpuRegs* spuRegisters;  /* the SPU registers */
extern s16 soundErrorCode;
void EnterCriticalSection(void);
void ExitCriticalSection(void);
s32 OpenEvent(u32 desc, s32 spec, s32 mode, void* func);
void CloseEvent(s32);
void setSpuVoiceRelease(s32, s32, s32);
void setSpuKeyOff(s32);
void SpuSetIRQ(s32);
void* SpuSetTransferCallback(void*);
void* SpuSetIRQCallback(void*);
s32 StopRCnt(s32);
void SpuSetReverbModeDepth(s32, s32);
void SpuSetReverbModeType(s32);
void EnableEvent(s32);          /* EnableEvent */
void DisableEvent(s32);          /* DisableEvent */
void loadWdsSub0(s32, u8*, s32, void*);
extern u8 spuMallocTable[40]; /* SpuInitMalloc(4): 5 records of 8 bytes */
extern u8 soundArena[0x6300]; /* the arena (SOUND_ARENA_SIZE) */
extern SpuXfer* loadWdsSub0SubVar1; /* SPU transfer queue, 8 entries */
extern s32 lfoRandomSeed;
extern SoundInstance* pPlayingSoundsLinkedList; /* playing instances */
extern SoundInstance* pSoundEffectsInstances; /* sound effect instances */
extern Snd518* D_80059518;
extern u32 pendingKeyOn;
extern u32 pendingKeyOff2;
extern u32 pendingKeyOff;
extern s32 spuUpdateCounter;
extern s32 spuIrqCount;
extern s32 soundEffectVoiceCount;
extern s32 reverbWorkAreaBlock;
extern void* reverbClearBuffer;
extern u8 soundReverbMode;
s32 SpuInitMalloc(s32, u8*);
void clearSpuVoiceOwners(void);
void audioTick(void);
void spuDmaCompletionCallback(void);
void spuIrqCallback(void);
s32 SetRCnt(s32, s32, s32);
s32 StartRCnt(s32);
SoundInstance* initSoundEffectInstances(s32);
void SpuSetReverb(s32);
extern s32 numMaxSoundEffectInstances; /* sound effect track count */
extern s32 playSoundEffectVar0;
s16 checkHeader(void*, u32, s32);
extern SndVolume soundReverbDepthVolume;
extern SndCdAtv cdMixAttenuation;
extern u8 soundReverbDelay;
extern u8 soundReverbFeedback;
extern s32 reverbWorkAreaSizes[];
extern s32 reverbClearSpuAddr;
extern s32 reverbClearBytesLeft;
s32 CdMix(SndCdAtv*);
void SpuGetReverbModeType(s32*);
void SpuSetReverbModeDelayTime(s32);
void SpuSetReverbModeFeedback(s32);
s32 SpuSetCommonAttr(SpuVolumes*);
extern ArenaBlock* soundArenaHead;  /* sound arena head */
extern u8* soundArenaEnd;          /* sound arena end */
extern s32 soundArenaSize;          /* sound arena size */
extern SpuBlock spuAllocatedBlocks[12]; /* SPU RAM blocks */
s16 createMusicInstanceSub0(void*);
s32 getSoundInstanceSize(s32);
void initPercussions(SoundInstance*, void*);
void initSoundInstance(SoundInstance*);
void initSoundInstanceTracks(SoundInstance*);
void addActiveSoundInstance(SoundInstance*);
s32 removePlayingSound(SoundInstance*);
void freeSoundInstanceField4(SoundInstance*);
void startMusicInstanceSub0Sub(SoundInstance*);
s32 findFreeSoundEffectSlot(s32, s32);
void releaseSpuVoice(SoundVoice*, s32);

s32 getSamplePtr(Wds* wds, s32 addr);
void setupSamplesPtr(s32 addr, s32 size);
s32 sendSamples2(u8* src, s32 size);
Wds* findInWdsLinkedList(s32 bank);
void changeSoundOption(s32 mode);
s32 processSoundMenuSub0(void);
void setCdMixVolume(s32 vol);
void setupReverb(s32 mode, s32 depth, s32 delay, s32 feedback);
void startReverbAreaClear(s32 addr, s32 size);
void clearNextReverbChunk(void);
void setMasterVolume(s32 vol, s32 time);
void setCdVolume(s32 vol, s32 time);
void setCdReverbAndMix(s32 reverb, s32 mix);
void applyOutputModeVolumes(void);
void setVolumePairForOutputMode(s32 vol, SndVolume* v, s32 which);
void initSoundArena(u8* buf, s32 size);
void* allocateFromSoundArena(u32 size);
void* allocateFromSoundArenaTop(s32 size);
void freeBackToSoundArena(void* data);
void copySoundMemory(void* d, void* s, s32 size);
void clearSoundMemory(void* p, s32 size);
void initLoadedSampleTable(void);
s32 allocateSpuRam(s32 size, s32 tag);
s32 allocateSPUSpaceForSamples(s32 size, s32 addr, s32 tag);
s32 freeSamples(s32 addr);
s32 findFreeSlotInLoadedSamples(void);
SpuBlock* findSpuBlock(s32 addr);
SoundInstance* createMusicInstance(void* data);
void startMusicInstance(SoundInstance* inst, s32 a, s32 b);
void stopAllMusicInstances(void);
void stopAllSounds(void);
void stopSequence(void* data);


/* Noah: initSoundSystem. */
void initSoundSystem(u16 mode) {
    if (musicStatusFlag < 0) {
        setSoundError(0x28);
        return;
    }
    musicStatusFlag = mode | 0xB801;
    SpuInitMalloc(4, spuMallocTable);
    initSoundArena(SOUND_ARENA, SOUND_ARENA_SIZE);
    initLoadedSampleTable();
    loadWdsSub0SubVar1 = allocateFromSoundArena(8 * sizeof(SpuXfer));
    clearSpuVoiceOwners();
    lfoRandomSeed = 0x12345678;
    pPlayingSoundsLinkedList = 0;
    pSoundEffectsInstances = 0;
    pLoadedSequencesLinkedList = NULL;
    pLoadedWdsLinkedList = NULL;
    D_80059518 = 0;
    pendingKeyOn = 0;
    pendingKeyOff2 = 0;
    pendingKeyOff = 0;
    g_spuVolumes.m8_mvolmode.left = 0;
    g_spuVolumes.m8_mvolmode.right = 0;
    g_spuVolumes.m0_mask = 0xC;
    EnterCriticalSection();
    audioTickEvent = OpenEvent(0xF2000002, 2, 0x1000, audioTick);
    SetRCnt(0xF2000002, 0x44E8, 0x1000);
    StartRCnt(0xF2000002);
    SpuSetTransferCallback(spuDmaCompletionCallback);
    SpuSetIRQCallback(spuIrqCallback);
    SpuSetIRQ(0);
    spuUpdateCounter = 0;
    spuIrqCount = 0;
    ExitCriticalSection();
    allocateSPUSpaceForSamples(0x2000, 0x10000, 4);
    changeSoundOption(1);
    setCdReverbAndMix(0, 1);
    setMasterVolume(0x3FFF, 0);
    setCdVolume(0x7FFF, 0);
    if (musicStatusFlag & 0x4000) {
        setCdMixVolume(0x80);
    }
    pSoundEffectsInstances = initSoundEffectInstances(0x10);
    soundEffectVoiceCount = 8;
    reverbWorkAreaBlock = -1;
    reverbClearBuffer = 0;
    soundReverbMode = 0xFF;
    setupReverb(4, 0, 0, 0);
    SpuSetReverb(1);
    soundErrorCode = 0;
}

/* Noah: sound system shutdown. */
void shutdownSoundSystem(void) {
    s32 i;

    if (musicStatusFlag == 0) {
        setSoundError(0x29);
        return;
    }
    EnterCriticalSection();
    musicStatusFlag = 0;
    SpuSetIRQ(0);
    SpuSetTransferCallback(NULL);
    SpuSetIRQCallback(NULL);
    StopRCnt(0xF2000002);
    CloseEvent(audioTickEvent);
    ExitCriticalSection();
    for (i = 0; i < 24; i++) {
        setSpuVoiceRelease(i, 6, 3);
    }
    setSpuKeyOff(0xFFFFFF);
    SpuSetReverbModeDepth(0, 0);
    SpuSetReverbModeType(0);
    soundErrorCode = 0;
}

void resumeMusic(void) {
    s32 i;
    void** p = spuVoiceOwners;

    for (i = 0; i < 24; p++) {
        void* q = *p;
        i++;
        if (q != NULL) {
            *(u16*)((u8*)q + 6) |= 0x1F5;
        }
    }
    musicStatusFlag &= ~0x40;
}

void pauseMusic(void) {
    s32 i;
    SpuVoiceRegs* p = spuRegisters->m0_voice;

    musicStatusFlag |= 0x40;
    for (i = 0; i < 24; i++) {
        p->m0_volL = 0;
        p->m2_volR = 0;
        p->m4_pitch = 0;
        p->m8_adsr1 = (u8)p->m8_adsr1 + 0x7F00;
        p->mA_adsr2 = 0x1FDF;
        p++;
    }
}

void enableAudioTick(void) {
    if (!(musicStatusFlag & 1)) {
        musicStatusFlag |= 1;
        EnableEvent(audioTickEvent);
    }
}

void disableAudioTick(void) {
    if (musicStatusFlag & 1) {
        DisableEvent(audioTickEvent);
        musicStatusFlag &= ~1;
    }
}

/* Noah: loadWds. */
Wds* loadWds(Wds* wds, s32 addr) {
    s32 spu;
    Wds* copy;
    Wds** pp;
    Wds* w;
    Wds* t;

    spu = getSamplePtr(wds, addr);
    if (spu == 0) {
        setSoundError(0x1F);
        return NULL;
    }
    loadWdsSub0(spu, (u8*)wds + wds->m18_headerSize2, wds->m14_adpcmSize, 0);
    copy = allocateFromSoundArenaTop(WDS_COPY_SIZE(wds));
    if (copy == NULL) {
        freeSamples(spu);
        setSoundError(0x1E);
        return NULL;
    }
    WDS_COPY(copy, wds);
    copy->m28_adpcmAddress = spu;
    DisableEvent(audioTickEvent);
    pp = &pLoadedWdsLinkedList;
    if (pLoadedWdsLinkedList != NULL) {
        do {
            t = *pp;
            pp = &t->m2C_next;
        } while (t->m2C_next != NULL);
    }
    *pp = copy;
    copy->m2C_next = NULL;
    EnableEvent(audioTickEvent);
    return copy;
}

/* Noah: playMusic2 (load a WDS bank streamed in pieces). */
Wds* playMusic2(Wds* wds, s32 size, s32 addr) {
    s32 spu;
    Wds* copy;
    Wds** pp;
    Wds* t;

    if (findInWdsLinkedList(wds->m20_bankNumber) != NULL) {
        setSoundError(0x16);
        return NULL;
    }
    spu = getSamplePtr(wds, addr);
    if (spu == 0) {
        setSoundError(0x1F);
        return NULL;
    }
    setupSamplesPtr(spu, wds->m14_adpcmSize);
    sendSamples2((u8*)wds + wds->m18_headerSize2, size - wds->m10_headerSize1);
    copy = allocateFromSoundArenaTop(WDS_COPY_SIZE(wds));
    if (copy == NULL) {
        freeSamples(spu);
        setSoundError(0x1E);
        return NULL;
    }
    WDS_COPY(copy, wds);
    copy->m28_adpcmAddress = spu;
    DisableEvent(audioTickEvent);
    pp = &pLoadedWdsLinkedList;
    if (pLoadedWdsLinkedList != NULL) {
        do {
            t = *pp;
            pp = &t->m2C_next;
        } while (t->m2C_next != NULL);
    }
    *pp = copy;
    copy->m2C_next = NULL;
    EnableEvent(audioTickEvent);
    return copy;
}

/* Noah: getSamplePtr (no return statement: callers use the callee's v0). */
s32 getSamplePtr(Wds* wds, s32 addr) {
    s32 r;

    if (addr == 0) {
        addr = wds->m28_adpcmAddress;
    } else if (addr == -1) {
        addr = 0;
    }
    if (addr == 0) {
        r = allocateSpuRam(wds->m14_adpcmSize, wds->m1E_spuBlockTag);
    } else {
        r = allocateSPUSpaceForSamples(wds->m14_adpcmSize, wds->m28_adpcmAddress, wds->m1E_spuBlockTag);
    }
    return r;
}

void setupSamplesPtr(s32 addr, s32 size) {
    sendSamplesSpuAddr = addr;
    sendSamplesBytesLeft = size;
}

s32 sendSamples2(u8* src, s32 size) {
    s32 left = sendSamplesBytesLeft;
    s32 addr;

    if (left == 0) {
        return 0;
    }
    size = (left <= size) ? left : size;
    addr = sendSamplesSpuAddr;
    loadWdsSub0(addr, src, size, 0);
    sendSamplesSpuAddr = addr + size;
    return sendSamplesBytesLeft = left - size;
}


void unloadWds(Wds* wds) {
    Wds* cur;
    Wds* prev;

    prev = NULL;
    for (cur = pLoadedWdsLinkedList; cur != NULL; prev = cur, cur = cur->m2C_next) {
        if (cur == wds) {
            break;
        }
    }
    if (cur == NULL) {
        setSoundError(0x11);
        return;
    }
    DisableEvent(audioTickEvent);
    if (prev != NULL) {
        prev->m2C_next = wds->m2C_next;
    } else {
        pLoadedWdsLinkedList = wds->m2C_next;
    }
    EnableEvent(audioTickEvent);
    if (wds->m28_adpcmAddress != freeSamples(wds->m28_adpcmAddress)) {
        setSoundError(0x24);
    }
    freeBackToSoundArena(wds);
}

/* Noah: findInWdsLinkedList. */
Wds* findInWdsLinkedList(s32 bank) {
    Wds* w;

    w = pLoadedWdsLinkedList;
    if (w != NULL) {
        do {
            if (w->m20_bankNumber == bank) {
                return w;
            }
            w = w->m2C_next;
        } while (w != NULL);
    }
    return w;
}

void loadSequence(void* data) {
    SeqFile* seq;
    SeqFile* s;
    SeqFile** pp;
    SeqFile* t;
    s16 err;

    seq = data;
    if (!(musicStatusFlag & 0x80)) {
        for (s = pLoadedSequencesLinkedList; s != NULL; s = SEQ_NEXT(s)) {
            if (seq->u14.m14_id == s->u14.m14_id) {
                setSoundError(0x15);
                return;
            }
        }
    }
    err = checkHeader(data, 0x73646573, 0x101);
    if (err != 0) {
        setSoundError(err);
        return;
    }
    DisableEvent(audioTickEvent);
    if (pLoadedSequencesLinkedList == NULL) {
        pLoadedSequencesLinkedList = seq;
    } else {
        for (t = pLoadedSequencesLinkedList; SEQ_NEXT(t) != NULL; t = SEQ_NEXT(t)) {
        }
        SEQ_SET_NEXT(t, seq);
    }
    SEQ_SET_NEXT(seq, NULL);
    EnableEvent(audioTickEvent);
}

void unloadSequence(void* data) {
    SeqFile* seq;
    SeqFile* cur;
    SeqFile* prev;

    seq = data;
    prev = NULL;
    for (cur = pLoadedSequencesLinkedList; cur != NULL; prev = cur, cur = SEQ_NEXT(cur)) {
        if (cur == seq) {
            break;
        }
    }
    if (cur == NULL) {
        setSoundError(0x10);
        return;
    }
    stopSequence(data);
    DisableEvent(audioTickEvent);
    if (prev != NULL) {
        SEQ_SET_NEXT(prev, SEQ_NEXT(seq));
    } else {
        pLoadedSequencesLinkedList = SEQ_NEXT(seq);
    }
    SEQ_SET_NEXT(seq, NULL);
    if (checkHeader(data, 0x73646573, 0x101)) {
        setSoundError(0xB);
        return;
    }
    EnableEvent(audioTickEvent);
}

void clearLoadedSequences(void) {
    stopAllSounds();
    pLoadedSequencesLinkedList = NULL;
}

SeqFile* findSequenceInLoadedList(SeqFile* seq, s16 id) {
    SeqFile* s;

    if (seq != NULL) {
        id = seq->u14.m14_id;
    }
    s = pLoadedSequencesLinkedList;
    if (s != NULL) {
        do {
            if (id == s->u14.m14_id) {
                return s;
            }
            s = SEQ_NEXT(s);
        } while (s != NULL);
    }
    return s;
}

void stopAllMusicAndSounds(void) {
    stopAllMusicInstances();
    stopAllSounds();
}

void changeSoundOption(s32 mode) {
    SoundInstance* p;
    Snd518* q;
    s32 v;

    musicStatusFlag &= 0xF8FF;
    switch (mode) {
    case 0:
        break;
    case 1:
        musicStatusFlag |= 0x100;
        break;
    case 2:
        musicStatusFlag |= 0x300;
        break;
    case 3:
        musicStatusFlag |= 0x500;
        break;
    }
    applyOutputModeVolumes();
    SpuSetReverbModeDepth(soundReverbDepthVolume.left, soundReverbDepthVolume.right);
    for (p = pPlayingSoundsLinkedList; p != NULL; p = p->m0_next) {
        applyFlagToAllVoices(0x100, p);
    }
    if (musicStatusFlag & 0x4000) {
        setCdMixVolume(g_spuVolumes.m2E_cdMixVolume);
    }
    q = D_80059518;
    if (q != NULL && (q->m0_flags & 1)) {
        v = q->m12_volume;
        if (processSoundMenuSub0()) {
            v <<= 7;
            q->m38_voice0VolumeLeft = v;
            q->m3A_voice0VolumeRight = 0;
            q->m64_voice1VolumeLeft = 0;
            q->m66_voice1VolumeRight = v;
        } else {
            v <<= 6;
            q->m38_voice0VolumeLeft = v;
            q->m3A_voice0VolumeRight = v;
            q->m64_voice1VolumeLeft = v;
            q->m66_voice1VolumeRight = v;
        }
        q->m36_voice0Dirty = 1;
        q->m62_voice1Dirty = 1;
    }
}

/* Noah: processSoundMenuSub0. */
s32 processSoundMenuSub0(void) {
    s32 r;

    if (musicStatusFlag & 0x700) {
        r = 1;
        if (musicStatusFlag & 0x600) {
            r = 2;
        }
    } else {
        r = 0;
    }
    return r;
}

/* Set the CD audio mix volume (stereo: straight through, mono: halved to both). */
void setCdMixVolume(s32 vol) {
    s32 a;
    s32 b;

    g_spuVolumes.m2E_cdMixVolume = vol;
    if (musicStatusFlag & 0x700) {
        a = vol;
        b = 0;
    } else {
        a = b = vol >> 1;
    }
    cdMixAttenuation.val0 = cdMixAttenuation.val2 = a;
    cdMixAttenuation.val1 = cdMixAttenuation.val3 = b;
    CdMix(&cdMixAttenuation);
}

void setSequenceReverbEnabled(s32 on) {
    if (on) {
        musicStatusFlag |= 0x1000;
    } else {
        musicStatusFlag &= ~0x1000;
    }
}

void setMusicInstanceStopExempt(SoundInstance* inst, s32 on) {
    if (on) {
        inst->m10_flags &= ~1;
    } else {
        inst->m10_flags |= 1;
    }
}

/* Noah: setupReverb. */
void setupReverb(s32 mode, s32 depth, s32 delay, s32 feedback) {
    SndReverbAttr attr;
    s32 cur;
    s32 size;
    s32 addr;
    s32 realloc;

    realloc = 0;
    if (mode == -2) {
        return;
    }
    if (mode == 0) {
        feedback = 0;
        delay = 0;
        depth = 0;
    } else if (mode == -1) {
        mode = soundReverbMode;
    }
    SpuGetReverbModeType(&cur);
    if (cur != mode || mode == 0) {
        if (reverbWorkAreaBlock != -1) {
            freeSamples(reverbWorkAreaBlock);
        }
        size = reverbWorkAreaSizes[mode];
        addr = 0x80000 - size;
        reverbWorkAreaBlock = allocateSPUSpaceForSamples(size, addr, 5);
        if (reverbWorkAreaBlock == 0) {
            setSoundError(0x20);
            mode = 0;
            feedback = 0;
            delay = 0;
            depth = 0;
        }
        realloc = 1;
    }
    soundReverbMode = mode;
    g_spuVolumes.m2C_reverbDepth = depth;
    soundReverbDelay = delay;
    soundReverbFeedback = feedback;
    applyOutputModeVolumes();
    if (realloc) {
        SpuSetReverbModeDepth(0, 0);
        SpuSetReverbModeType(mode);
        startReverbAreaClear(addr, size);
    } else {
        SpuSetReverbModeDepth(soundReverbDepthVolume.left, soundReverbDepthVolume.right);
        SpuSetReverbModeDelayTime(delay);
        SpuSetReverbModeFeedback(feedback);
    }
}

/* Start clearing the reverb work area (addr, size) in SPU RAM. */
void startReverbAreaClear(s32 addr, s32 size) {
    reverbClearSpuAddr = addr;
    reverbClearBytesLeft = size;
    if (reverbClearBuffer == NULL) {
        reverbClearBuffer = allocateFromSoundArena(0x840);
        if (reverbClearBuffer == NULL) {
            setSoundError(0x1E);
        }
    }
    musicStatusFlag |= 0x20;
    clearNextReverbChunk();
}

/* SPU transfer callback: clear the next reverb chunk, or finish. */
void clearNextReverbChunk(void) {
    s32 left;
    s32 n;
    s32 addr;

    left = reverbClearBytesLeft;
    if (left == 0) {
        freeBackToSoundArena(reverbClearBuffer);
        reverbClearBuffer = NULL;
        SpuSetReverbModeDepth(soundReverbDepthVolume.left, soundReverbDepthVolume.right);
        SpuSetReverbModeDelayTime(soundReverbDelay);
        SpuSetReverbModeFeedback(soundReverbFeedback);
        musicStatusFlag &= ~0x20;
    } else {
        n = 0x800;
        if (left <= 0x840) {
            n = left;
        }
        addr = reverbClearSpuAddr;
        reverbClearBytesLeft = left - n;
        reverbClearSpuAddr = addr + n;
        loadWdsSub0(addr, reverbClearBuffer, n, clearNextReverbChunk);
        if (!(musicStatusFlag & 0x10)) {
            loadWdsSub0(addr, reverbClearBuffer, n, NULL);
        }
    }
}

/* Master volume, optionally ramped over time ticks. */
void setMasterVolume(s32 vol, s32 time) {
    s32 d;

    g_spuVolumes.m30_master.mA_target = vol;
    if (time == 0) {
        g_spuVolumes.m30_master.m0_value = vol << 16;
        g_spuVolumes.m30_master.m8_counter = 0;
        g_spuVolumes.m28_masterVolume = vol;
        setVolumePairForOutputMode(g_spuVolumes.m30_master.m0_value >> 16, &g_spuVolumes.m4_mvol, 0);
        g_spuVolumes.m0_mask |= 3;
    } else {
        d = (vol << 8) - (g_spuVolumes.m30_master.m0_value >> 8);
        if (d != 0) {
            g_spuVolumes.m30_master.m8_counter = time;
            g_spuVolumes.m30_master.m4_step = (d / time) << 8;
        }
    }
}

/* CD volume, optionally ramped over time ticks. */
void setCdVolume(s32 vol, s32 time) {
    s32 d;

    g_spuVolumes.m3C_cd.mA_target = vol;
    if (time == 0) {
        g_spuVolumes.m3C_cd.m0_value = vol << 16;
        g_spuVolumes.m3C_cd.m8_counter = 0;
        g_spuVolumes.m10_cdVolume.left = g_spuVolumes.m10_cdVolume.right = g_spuVolumes.m2A_cdVolume = vol;
        g_spuVolumes.m0_mask |= 0xC0;
    } else {
        d = (vol << 8) - (g_spuVolumes.m3C_cd.m0_value >> 8);
        if (d != 0) {
            g_spuVolumes.m3C_cd.m8_counter = time;
            g_spuVolumes.m3C_cd.m4_step = (d / time) << 8;
        }
    }
}

void setCdReverbAndMix(s32 reverb, s32 mix) {
    g_spuVolumes.m14_cdReverb = reverb;
    g_spuVolumes.m18_cdMix = mix;
    g_spuVolumes.m0_mask |= 0x300;
    SpuSetCommonAttr(&g_spuVolumes);
}

void applyOutputModeVolumes(void) {
    setVolumePairForOutputMode(g_spuVolumes.m28_masterVolume, &g_spuVolumes.m4_mvol, 0);
    g_spuVolumes.m10_cdVolume.left = g_spuVolumes.m10_cdVolume.right = g_spuVolumes.m2A_cdVolume;
    setVolumePairForOutputMode(g_spuVolumes.m2C_reverbDepth, &soundReverbDepthVolume, 1);
    g_spuVolumes.m0_mask |= 0xC3;
}

/* Fill a volume pair; in surround modes one side is phase inverted. */
void setVolumePairForOutputMode(s32 vol, SndVolume* v, s32 which) {
    u16 flags;

    flags = musicStatusFlag;
    v->right = vol;
    v->left = vol;
    if (flags & 0x600) {
        which &= 0xFF;
        if (!(flags & 0x200)) {
            if ((which ^ 1) != 0) {
                v->left = -vol;
            } else {
                v->right = -vol;
            }
        } else {
            if (which) {
                v->left = -vol;
            } else {
                v->right = -vol;
            }
        }
    }
}

/* Sound arena init (buffer, size). */
void initSoundArena(u8* buf, s32 size) {
    ArenaBlock* b;

    size &= ~0xF;
    if ((sptr)buf & 0xF) {
        size -= 0x10;
        buf = (u8*)(((sptr)buf + 0xF) & ~0xF);
    }
    soundArenaEnd = buf + size;
    b = (ArenaBlock*)buf;
    b->m0_flags = 0x8000;
    soundArenaHead = b;
    soundArenaSize = size;
    b->m2 = 0;
    b->m4 = 0;
    b->m8_end = buf + ARENA_HEADER;
    b->mC_next = NULL;
}

/* Sound arena allocation, first fit, zeroed. */
void* allocateFromSoundArena(u32 size) {
    ArenaBlock* p;
    ArenaBlock* blk;
    u32 need;
    u8* data;
    u8* end;

    DisableEvent(audioTickEvent);
    need = ((size + 0xF) & ~0xF) + ARENA_HEADER;
    p = soundArenaHead;
    while (p->mC_next != NULL) {
        end = (u8*)p->mC_next;
        if (end - p->m8_end >= need) {
            goto found;
        }
        p = p->mC_next;
    }
    end = soundArenaEnd;
    if (end - p->m8_end >= need) {
    found:
        blk = (ArenaBlock*)(((sptr)p->m8_end + 0xF) & ~0xF);
        data = (u8*)blk + ARENA_HEADER;
        blk->m8_end = data + size;
        blk->mC_next = NULL;
        blk->m4 = 0;
        blk->m0_flags = 2;
        blk->m2 = 0;
        blk->mC_next = p->mC_next;
        p->mC_next = blk;
        EnableEvent(audioTickEvent);
        clearSoundMemory(data, size);
        return data;
    }
    return NULL;
}

/* Sound arena allocation from the top (last gap that fits), zeroed. */
void* allocateFromSoundArenaTop(s32 size) {
    ArenaBlock* p;
    ArenaBlock* next;
    ArenaBlock* best;
    ArenaBlock* blk;
    u8* end;
    u8* bestEnd;
    s32 need;
    u8* data;

    DisableEvent(audioTickEvent);
    need = ((size + 0xF) & ~0xF) + ARENA_HEADER;
    best = NULL;
    bestEnd = NULL;
    p = soundArenaHead;
    end = soundArenaEnd;
    for (;;) {
        next = p->mC_next;
        if (next == NULL) {
            if (end - p->m8_end >= need) {
                best = p;
                bestEnd = end;
            }
            break;
        }
        if ((u8*)next - p->m8_end >= need) {
            best = p;
            bestEnd = (u8*)next;
        }
        p = next;
    }
    if (best == NULL) {
        return NULL;
    }
    bestEnd -= need;
    blk = (ArenaBlock*)(((sptr)bestEnd + 0xF) & ~0xF);
    data = (u8*)blk + ARENA_HEADER;
    blk->m8_end = data + size;
    blk->mC_next = NULL;
    blk->m4 = 0;
    blk->m0_flags = 2;
    blk->m2 = 0;
    blk->mC_next = best->mC_next;
    best->mC_next = blk;
    EnableEvent(audioTickEvent);
    clearSoundMemory(data, size);
    return data;
}

/* Sound arena free. */
void freeBackToSoundArena(void* data) {
    ArenaBlock* head = soundArenaHead;
    ArenaBlock* blk;
    ArenaBlock* p;
    ArenaBlock* prev;

    DisableEvent(audioTickEvent);
    blk = (ArenaBlock*)((u8*)data - ARENA_HEADER);
    p = head;
    prev = NULL;
    while (p != blk) {
        prev = p;
        p = p->mC_next;
    }
    if (prev != NULL) {
        prev->mC_next = blk->mC_next;
    } else {
        head = blk->mC_next;
    }
    EnableEvent(audioTickEvent);
}

/* Largest free gap in the sound arena. */
s32 getLargestSoundArenaGap(void) {
    ArenaBlock* p;
    ArenaBlock* next;
    s32 max;
    s32 gap;

    p = soundArenaHead;
    max = 0;
    while (p->mC_next != NULL) {
        next = p->mC_next;
        gap = (s32)((u8*)next - p->m8_end);
        if (max < gap) {
            max = gap;
        }
        p = next;
    }
    gap = (s32)(soundArenaEnd - p->m8_end);
    if (max < gap) {
        max = gap;
    }
    return max & ~0xF;
}

void copySoundMemory(void* d, void* s, s32 size) {
    u32* dst = d;
    u32* src = s;
    u32 a;
    u32 b;
    u32 c;
    u32 e;
    s32 n;

    for (n = size >> 4; n != 0; n--) {
        a = src[0];
        b = src[1];
        c = src[2];
        e = src[3];
        dst[0] = a;
        dst[1] = b;
        dst[2] = c;
        dst[3] = e;
        src += 4;
        dst += 4;
    }
    for (n = (size >> 2) & 3; n != 0; n--) {
        *dst++ = *src++;
    }
    for (n = size & 3; n != 0; n--) {
        *(u8*)dst = *(u8*)src;
        dst = (u32*)((u8*)dst + 1);
        src = (u32*)((u8*)src + 1);
    }
}

void clearSoundMemory(void* p, s32 size) {
    u32* dst = p;
    s32 n;

    for (n = size >> 4; n != 0; n--) {
        dst[3] = 0;
        dst[2] = 0;
        dst[1] = 0;
        dst[0] = 0;
        dst += 4;
    }
    for (n = (size >> 2) & 3; n != 0; n--) {
        *dst++ = 0;
    }
    for (n = size & 3; n != 0; n--) {
        *(u8*)dst = 0;
        dst = (u32*)((u8*)dst + 1);
    }
}

/* Noah: initLoadedSampleTable. */
void initLoadedSampleTable(void) {
    s32 i;

    for (i = 0; i < 12; i++) {
        spuAllocatedBlocks[i].m0_flags = 0;
    }
    spuAllocatedBlocks[0].m0_flags = 0x81;
    spuAllocatedBlocks[0].m1_tag = 5;
    spuAllocatedBlocks[0].m4_addr = 0;
    spuAllocatedBlocks[0].m8_size = 0x1010;
    spuAllocatedBlocks[0].m2_next = 0;
}

/* Allocate size bytes of SPU RAM, first fit. */
s32 allocateSpuRam(s32 size, s32 tag) {
    SpuBlock* p;
    SpuBlock* next;
    s32 addr;
    s32 i;

    p = spuAllocatedBlocks;
    addr = spuAllocatedBlocks[0].m4_addr + spuAllocatedBlocks[0].m8_size;
    if (p->m2_next == 0) {
        goto tail;
    }
loop:
    next = &spuAllocatedBlocks[p->m2_next];
    if (next->m4_addr - addr >= size) {
        goto found;
    }
    p = next;
    addr = next->m4_addr + next->m8_size;
    if (p->m2_next != 0) {
        goto loop;
    }
tail:
    if (0x80000 - addr < size) {
        return 0;
    }
found:
    i = findFreeSlotInLoadedSamples();
    if (i < 0) {
        return 0;
    }
    next = &spuAllocatedBlocks[i];
    next->m0_flags = 0x80;
    next->m1_tag = 0;
    next->m4_addr = addr;
    next->m8_size = size;
    next->m2_next = p->m2_next;
    p->m2_next = i;
    return addr;
}

/* Allocate size bytes of SPU RAM from the top (last gap that fits). */
s32 allocateSpuRamFromTop(s32 size) {
    SpuBlock* p;
    SpuBlock* next;
    SpuBlock* found;
    s32 end;
    s32 addr;
    s32 i;

    p = spuAllocatedBlocks;
    found = NULL;
    for (;;) {
        end = p->m4_addr + p->m8_size;
        if (p->m2_next == 0) {
            if (0x80000 - end >= size) {
                found = p;
                addr = 0x80000 - size;
            }
            break;
        }
        next = &spuAllocatedBlocks[p->m2_next];
        if (next->m4_addr - end >= size) {
            found = p;
            addr = next->m4_addr - size;
        }
        p = next;
    }
    if (found == NULL) {
        return 0;
    }
    i = findFreeSlotInLoadedSamples();
    if (i < 0) {
        return 0;
    }
    next = &spuAllocatedBlocks[i];
    next->m0_flags = 0x80;
    next->m1_tag = 0;
    next->m4_addr = addr;
    next->m8_size = size;
    next->m2_next = p->m2_next;
    p->m2_next = i;
    return addr;
}

/* Noah: allocateSPUSpaceForSamples. First-fit search of the SPU RAM block
 * list (sorted by address) for `size` bytes at `addr`; links a new block in
 * after the one that precedes it and returns addr, or 0 if it does not fit. */
s32 allocateSPUSpaceForSamples(s32 size, s32 addr, s32 tag) {
    SpuBlock* p;
    SpuBlock* next;
    s32 gap;
    s32 end;
    s32 a;
    s32 i;
    s32 lim;

    p = spuAllocatedBlocks;
    gap = 0;
    lim = addr + size;
    end = spuAllocatedBlocks[0].m4_addr + spuAllocatedBlocks[0].m8_size;
    if (spuAllocatedBlocks[0].m4_addr < addr) {
        do {
            /* The do/while (0) (a macro in the original?) weights p's refs by
             * one more loop level, which gives p $s0 and addr $s1 as retail. */
            do {
                if (p->m2_next == 0) {
                    goto top;
                }
            } while (0);
            next = &spuAllocatedBlocks[p->m2_next];
            a = next->m4_addr;
            if (a >= lim) {
                goto mid;
            }
            end = a + next->m8_size;
            p = next;
        } while (a < addr);
    }
check:
    if (gap < size) {
        return 0;
    }
    if (addr >= end) {
        goto alloc;
    }
    return 0;
mid:
    gap = a - end;
    goto check;
top:
    gap = 0x80000 - end;
    goto check;
alloc:
    i = findFreeSlotInLoadedSamples();
    if (i < 0) {
        return 0;
    }
    next = &spuAllocatedBlocks[i];
    next->m0_flags = 0x80;
    next->m1_tag = 0;
    next->m4_addr = addr;
    next->m8_size = size;
    next->m2_next = p->m2_next;
    p->m2_next = i;
    return addr;
}

/* Noah: freeSamples. */
s32 freeSamples(s32 addr) {
    SpuBlock* p;
    SpuBlock* prev;

    p = spuAllocatedBlocks;
    prev = NULL;
    for (;;) {
        if (p->m4_addr == addr) {
            prev->m2_next = p->m2_next;
            p->m0_flags = 0;
            p->m1_tag = 0;
            p->m4_addr = 0;
            p->m2_next = 0;
            return addr;
        }
        if (p->m2_next == 0) {
            break;
        }
        prev = p;
        p = &spuAllocatedBlocks[p->m2_next];
    }
    return 0;
}

void setSpuBlockTag(s32 addr, s32 tag) {
    SpuBlock* b;

    b = findSpuBlock(addr);
    if (b != NULL) {
        b->m1_tag = tag;
    }
}

s32 func_8003977C(void) {
    return 0;
}

/* Noah: findFreeSlotInLoadedSamples. */
s32 findFreeSlotInLoadedSamples(void) {
    s32 i;

    for (i = 0; i < 12; i++) {
        if (spuAllocatedBlocks[i].m0_flags == 0) {
            return i;
        }
    }
    return 0;
}

/* Find the SPU block at addr (retail only ever checks the first block). */
SpuBlock* findSpuBlock(s32 addr) {
    SpuBlock* p = spuAllocatedBlocks;

    for (;;) {
        if (p->m4_addr == addr) {
            return p;
        }
        if (p->m2_next != 0) {
            return NULL;
        }
        p = &spuAllocatedBlocks[p->m2_next];
    }
}

SoundInstance* realStartBattleMusic(void* seq, s32 a, s32 b) {
    SoundInstance* inst;

    inst = createMusicInstance(seq);
    startMusicInstance(inst, a, b);
    return inst;
}

/* Noah: createMusicInstance. */
SoundInstance* createMusicInstance(void* data) {
    SeqFile* seq;
    SoundInstance* inst;
    s32 err;
    s32 size;

    seq = data;
    err = createMusicInstanceSub0(data);
    if (err != 0) {
        goto error;
    }
    size = getSoundInstanceSize(((SeqFile*)data)->u14.s.m14_trackCount);
    if (((SeqFile*)data)->u14.s.m15_percussionCount) {
        size += 0x180;
    }
    inst = allocateFromSoundArena(size);
    if (inst == NULL) {
        err = 0x1E;
    error:
        setSoundError(err);
        return NULL;
    }
    inst->m8_seq = (SeqFile*)seq;
    if (seq->u14.s.m15_percussionCount) {
        initPercussions(inst, seq);
    }
    initSoundInstance(inst);
    initSoundInstanceTracks(inst);
    inst->m4C_muteMask = 0;
    addActiveSoundInstance(inst);
    return inst;
}

/* createMusicInstance into a caller-provided buffer. */
SoundInstance* createMusicInstanceInBuffer(void* data, SoundInstance* inst) {
    SeqFile* seq = data;
    s16 err;
    s32 size;

    err = createMusicInstanceSub0(data);
    if (err != 0) {
        setSoundError(err);
        return NULL;
    }
    size = getSoundInstanceSize(seq->u14.s.m14_trackCount);
    if (seq->u14.s.m15_percussionCount) {
        size += 0x180;
    }
    clearSoundMemory(inst, size);
    inst->m8_seq = (SeqFile*)seq;
    if (seq->u14.s.m15_percussionCount) {
        initPercussions(inst, seq);
    }
    initSoundInstance(inst);
    initSoundInstanceTracks(inst);
    inst->m4C_muteMask = 0;
    addActiveSoundInstance(inst);
    inst->m10_flags |= 0x4000;
    return inst;
}

/* Noah: freeSoundInstance. */
void freeSoundInstance(SoundInstance* inst) {
    if ((s16)inst->m10_flags & 0x8000) {
        startMusicInstanceSub0(inst);
    }
    if (createMusicInstanceSub0(inst->m8_seq) != 0) {
        setSoundError(0xA);
        return;
    }
    if (removePlayingSound(inst) != 0) {
        setSoundError(5);
        return;
    }
    freeSoundInstanceField4(inst);
    if (!(inst->m10_flags & 0x4000)) {
        freeBackToSoundArena(inst);
    }
}

/* Noah: startMusicInstance. */
void startMusicInstance(SoundInstance* inst, s32 a, s32 b) {
    if (inst == NULL) {
        setSoundError(5);
        return;
    }
    inst->m10_flags &= 0x7FFF;
    if (createMusicInstanceSub0(inst->m8_seq) != 0) {
        setSoundError(0xA);
        return;
    }
    if ((s16)inst->m10_flags & 0x8000) {
        startMusicInstanceSub0(inst);
    }
    DisableEvent(audioTickEvent);
    initSoundInstance(inst);
    initSoundInstanceTracks(inst);
    inst->m70_volume.m0_value = 0;
    playBattleMusic(inst, a, b);
    inst->m10_flags |= 0x8000;
    EnableEvent(audioTickEvent);
}

/* Bind every track to its WDS sample and start the instance. */
void resumeMusicInstance(SoundInstance* inst, s32 a, s32 b) {
    SoundTrack* t;
    Wds* wds;
    s32 n;
    s32 start;
    WdsInstrument* smp;

    if (inst == NULL) {
        setSoundError(5);
        return;
    }
    n = inst->m14_trackCount;
    t = inst->m94_tracks;
    do {
        wds = findInWdsLinkedList(t->m25_bank);
        n--;
        t->m2C_wds = wds;
        smp = &wds->m30_instruments[t->m26_instrument];
        start = smp->m0_start << 3;
        t->m30_voice.m1C_startAddress = start + wds->m28_adpcmAddress;
        t->m30_voice.m20_repeatAddress = start + (smp->m4_loopOffset << 3);
        t->m30_voice.m6_dirty = 0xFFFF;
        t++;
    } while (n != 0);
    inst->m70_volume.m0_value = 0;
    inst->m10_flags |= 0x100;
    playBattleMusic(inst, a, b);
}

/* Noah: startMusicInstanceSub0. */
void startMusicInstanceSub0(SoundInstance* inst) {
    if (inst == NULL) {
        setSoundError(5);
        return;
    }
    inst->m10_flags &= 0x7FFF;
    startMusicInstanceSub0Sub(inst);
}

void fadeOutMusicInstance(SoundInstance* inst, s32 b) {
    if (inst == NULL) {
        setSoundError(5);
        return;
    }
    playBattleMusic(inst, 0, b);
}

/* Stop every playing instance that has flag 1. */
void stopAllMusicInstances(void) {
    SoundInstance* p;

    for (p = pPlayingSoundsLinkedList; p != NULL; p = p->m0_next) {
        if (p->m10_flags & 1) {
            p->m10_flags &= 0x7FFF;
            startMusicInstanceSub0Sub(p);
        }
    }
}

void func_80039D24(void) {
}

/* Enable/disable sound effects. */
void enableSoundEffects(s32 on) {
    if (on) {
        musicStatusFlag |= 0x800;
    } else {
        stopAllSounds();
        musicStatusFlag &= ~0x800;
    }
}

s32 setSoundEffectVoiceCount(s32 n) {
    if (n != 0) {
        if (n > 16) {
            n = 16;
        }
        if (n < 4) {
            n = 4;
        }
        soundEffectVoiceCount = n & 0xFE;
    }
    return soundEffectVoiceCount;
}

/* Noah: playSoundEffect. */
void playSoundEffect(s32 id) {
    if (musicStatusFlag & 0x800) {
        /* A separate statement keeps retail's full-word load: written inline
         * in the s16 argument, the front end narrows the read to lhu. */
        s32 ch = numMaxSoundEffectInstances - 2;
        playSoundEffectVar0 = 2;
        playSoundEffectSub(ch | 0x8000, id, 0x6000, 0x4000);
    }
}

void playSoundEffectFixedSlot(s32 id) {
    if (musicStatusFlag & 0x800) {
        playSoundEffectVar0 = 2;
        playSoundEffectSub(0x600C, id, 0x6000, 0x4000);
    }
}

void playSoundEffectInFreeSlot(s32 id) {
    if (musicStatusFlag & 0x800) {
        s32 ch = findFreeSoundEffectSlot(id, 2);
        playSoundEffectVar0 = 2;
        playSoundEffectSub(ch | 0x2000, id, 0x6000, 0x4000);
    }
}

void playSoundEffectOnChannel(s32 id, s32 ch) {
    if (musicStatusFlag & 0x800) {
        playSoundEffectVar0 = 2;
        playSoundEffectSub((ch & 0xFE ^ 8) | 0x2000, id, 0x6000, 0x4000);
    }
}

void playSoundEffectInFreeSlotVolPan(s32 id, s32 vol, s32 pan) {
    if (musicStatusFlag & 0x800) {
        s32 ch = findFreeSoundEffectSlot(id, 2);
        playSoundEffectVar0 = 2;
        playSoundEffectSub(ch | 0x2000, id, vol << 8, pan << 8);
    }
}

/* Noah: PlaySoundEffect2. */
void PlaySoundEffect2(s32 id, s32 ch, s32 vol, s32 pan) {
    if (musicStatusFlag & 0x800) {
        playSoundEffectVar0 = 2;
        playSoundEffectSub((ch & 0xFE ^ 8) | 0x2000, id, vol << 8, pan << 8);
    }
}

/* Noah: stopAllSounds. */
void stopAllSounds(void) {
    s32 n = numMaxSoundEffectInstances;
    SoundInstance* inst = pSoundEffectsInstances;
    SoundTrack* t = inst->m94_tracks;

    DisableEvent(audioTickEvent);
    do {
        n--;
        if (t->m0_flags & 1) {
            t->m0_flags = 0;
            releaseSpuVoice(&t->m30_voice, t->m27_voice);
        }
        t++;
    } while (n != 0);
    inst->m48_activeVoices = 0;
    EnableEvent(audioTickEvent);
}

/* Noah: stopSequence. */
void stopSequence(void* data) {
    s32 n = numMaxSoundEffectInstances;
    SoundInstance* inst = pSoundEffectsInstances;
    SoundTrack* t;
    s16 id;

    id = ((SeqFile*)data)->u14.m14_id;
    t = inst->m94_tracks;
    do {
        n--;
        if ((t->m0_flags & 1) && t->m8.s.mA_seqId == id) {
            t->m0_flags = 0;
            inst->m48_activeVoices &= ~(1 << t->m6_voiceIndex);
            releaseSpuVoice(&t->m30_voice, t->m27_voice);
        }
        t++;
    } while (n != 0);
}

/* Stop the sound effects started with owner key. */
void stopSoundEffectById(s32 key) {
    SoundInstance* inst = pSoundEffectsInstances;
    s32 n = numMaxSoundEffectInstances;
    SoundTrack* t;

    t = inst->m94_tracks;
    do {
        n--;
        if ((t->m0_flags & 1) && t->m8.m8_id == key) {
            t->m0_flags = 0;
            inst->m48_activeVoices &= ~(1 << t->m6_voiceIndex);
            releaseSpuVoice(&t->m30_voice, t->m27_voice);
        }
        t++;
    } while (n != 0);
}

void StopSoundEffect(s32 ch) {
    SoundInstance* inst;
    SoundTrack* t;
    s32 n;

    ch &= 0xFE;
    ch ^= 8;
    n = 2;
    inst = pSoundEffectsInstances;
    t = &inst->m94_tracks[ch];
    do {
        n--;
        if (t->m0_flags & 1) {
            t->m0_flags = 0;
            inst->m48_activeVoices &= ~(1 << t->m6_voiceIndex);
            releaseSpuVoice(&t->m30_voice, t->m27_voice);
        }
        t++;
    } while (n != 0);
}
