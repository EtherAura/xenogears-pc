/*
 * Sound driver, second file (main executable 0x8003A2D4-0x8003F738): effect
 * volume/pan and slides, music controls, sound instance init,
 * playSoundEffectSub, the SPU transfer queue, the audio tick, the sequence
 * opcodes, LFOs, SPU voice allocation and registers, header checks and
 * setSoundError. GCC 2.6.0, ASPSX 2.34, -G8 (waitForMusic only matches with
 * -G8). Read-only data: startTransfert's jump table only (0x80018BDC, JP
 * 0x80018C70). See sound.c for the boundary evidence.
 */

#include "common.h"
#include "main/sound.h"
#include "psyq/libspu.h"

/* Sound control and sequencer types. Offsets from the retail code; names
 * from Noah (kernel/audio). */


/* Sequence position (getMusicInstancePlayTime). */
typedef struct SoundTime {
    /* 0x0 */ s32 m0_tickCount;
    /* 0x4 */ s16 m4_subSecond;
    /* 0x6 */ s16 m6_seconds;
    /* 0x8 */ s16 m8_minutes;
} SoundTime;

/* SPU transfer queue entry (8 entries at loadWdsSub0SubVar1), 0x14 bytes. */
typedef struct SpuXfer {
    /* 0x00 */ u16 m0_mode;
    /* 0x02 */ u16 m2;
    /* 0x04 */ u8* m4_src;
    /* 0x08 */ s32 m8_spuAddr;
    /* 0x0C */ s32 mC_size;
    /* 0x10 */ void (*m10_callback)(void);
} SpuXfer;

typedef u8* (*SeqOpcode)(u8* p, SoundInstance* inst, SoundTrack* ch);

typedef struct {
    /* 0x00 */ u32 m0_magic;
    /* 0x04 */ u32 m4;
    /* 0x08 */ u32 m8_size;
    /* 0x0C */ u16 mC_checksum;
} SeqHeader;

extern s16 musicStatusFlag;
extern s32 audioTickEvent;
extern SoundVoice* spuVoiceOwners[24];
extern SpuRegs* spuRegisters;
extern s16 soundErrorCode;
void EnterCriticalSection(void);
void ExitCriticalSection(void);
void SpuSetIRQ(s32);
void* SpuSetTransferCallback(void*);
void EnableEvent(s32);          /* EnableEvent */
void DisableEvent(s32);          /* DisableEvent */
void freeSamples(s32);
void copySoundMemory(void*, void*, s32);
Wds* findInWdsLinkedList(s32);
extern SpuXfer* loadWdsSub0SubVar1; /* SPU transfer queue, 8 entries */
extern s32 lfoRandomSeed;
extern SoundInstance* pPlayingSoundsLinkedList; /* playing instances */
extern SoundInstance* pSoundEffectsInstances; /* sound effect instances */
extern u32 pendingKeyOn;
extern u32 pendingKeyOff2;
extern u32 pendingKeyOff;
extern s32 spuUpdateCounter;
extern s32 spuIrqCount;            /* SPU IRQ count */
extern s32 soundEffectVoiceCount;
void* allocateFromSoundArena(s32);
void setupReverb(s32 mode, s32 depth, s32 delay, s32 feedback);
extern s32 numMaxSoundEffectInstances; /* sound effect track count */
extern s32 playSoundEffectVar0;
void freeBackToSoundArena(void*);
s32 SpuSetCommonAttr(SpuVolumes*);
void setVolumePairForOutputMode(s32 vol, SndVolume* out, s32 mode);
extern u16 spuDmaTransertEntry;            /* queue read index */
extern u16 loadWdsSub0SubVar0;            /* queue write index */
extern s16 spuDecodedDataHalf;
extern void (*spuIrqHook)(void);  /* SPU IRQ hook */
void SpuSetTransferMode(s32);
void SpuSetTransferStartAddr(s32);
s32 SpuReadDecodedData(void*, s32);
void SpuWrite(u8*, s32);
void SpuRead(u8*, s32);
void playSoundEffectInFreeSlotVolPan(s32 id, s32 volume, s32 pan);
void stopSoundEffectById(s32 id);
extern u16 spuIrqEnableRequest;
extern u32 audioTickTotalTime;
extern s32 audioTickCount;
u32 GetRCnt(u32 spec);
extern SeqOpcode seqOpcodes[0x80];  /* opcode handlers 0x80-0xFF */
extern u8 opcodeSizes[0x80];         /* opcode operand sizes */
extern u8 noteDeltaTimeTable[];             /* note length per data byte */
extern u8 noteKeyTable[];
extern s32 (*lfoWaveformFuncs[16])(SoundLfo*);
extern u8 pitchToOctaveSemitone[];
extern s16 pitchTable2[];
s32 SpuGetVoiceEnvelopeAttr(s32, s32*, s16*);
extern u8 errorSoundWds[];   /* built-in fallback WDS */
extern u8 errorSoundSed[];   /* built-in fallback SED (sequence) */
void loadSequence(void*);
void playSoundEffectInFreeSlot(s32);

void playBattleMusicSub1(SoundInstance* inst);
void freeMusicInstanceSnapshot(SoundInstance* inst);
void saveMusicInstanceSnapshot(SoundInstance* inst);
void restoreMusicInstanceSnapshot(SoundInstance* inst);
void playBattleMusicSub1Sub1(SoundInstance* inst);
void startMusicInstanceSub0Sub(SoundInstance* inst);
void initSoundEffectSoundInstance(SoundInstance* inst);
void initSoundInstanceDefaults(SoundInstance* inst);
void freeSoundInstanceField4(SoundInstance* inst);
void copySoundInstanceState(SoundInstance* inst, SoundInstance* src);
void addActiveSoundInstance(SoundInstance* inst);
s32 removePlayingSound(SoundInstance* inst);
s32 getSoundInstanceSize(s32 n);
void loadWdsSub0Sub(s32 addr, u8* src, s32 size, void* cb, u16 mode);
s32 loadWdsSub0SubSub(void);
void startTransfert(void);
void interpolateAudioParam(AudioParam* a);
void executeSequenceEvents2(SoundInstance* inst, SoundTrack* ch, s16 count);
void executeSequenceEvents(SoundInstance* inst, SoundTrack* chs, s16 count);
void processPercussions(SoundInstance* inst, SoundTrack* ch, u8 key);
s32 scaleLfoDepth(s32 depth, s16 rate, s16 type);
void resetLfoState(SoundLfo* lfo);
void setupAdsr(s16 index, SoundTrack* ch);
void playBattleMusicSub1Sub0(SoundInstance* inst, s32 flag);
void claimSpuVoice(SoundVoice* voice, u32 index);
void releaseSpuVoice(SoundVoice* voice, u32 index);
void keyOffSpuVoice(SoundVoice* voice, u32 index);
void sendAdsrToSpu(void);
void audioTickSub1(void);
void voicePostUpdate2(SoundInstance* inst, SoundTrack* channels, s16 count);
s16 computeSampleRate(s16 pitch);
void voicePostUpdate2Sub0(SoundVoice* voice, u32 index);
void voicePostUpdate2Sub1(SoundVoice* voice, u32 index);
void voicePostUpdate1(SoundInstance* inst, SoundTrack* ch, s16 count);
s32 stepLfoTriangle(SoundLfo* lfo);
s32 stepLfoTriangleBipolar(SoundLfo* lfo);
s32 getLfoRandom(void);
s16 createMusicInstanceSub0(SeqFile* seq);
s32 checkSeqChecksum(SeqHeader* seq);


void func_8003A2D4(void) {
}

void func_8003A2DC(void) {
}

/* Set the volume of every SFX track playing sound id. */
void setSoundEffectVolume(s32 id, s32 vol) {
    SoundTrack* t;
    s32 n;

    vol <<= 8;
    n = numMaxSoundEffectInstances;
    t = pSoundEffectsInstances->m94_tracks;
    do {
        if ((t->m0_flags & 1) && t->m8.m8_id == id) {
            t->m76_volume = vol;
            t->m2_update = 0x100;
        }
        t++;
        n--;
    } while (n != 0);
}

/* Set the volume of the SFX channel pair ch. */
void setSoundEffectChannelVolume(s32 ch, s32 vol) {
    SoundTrack* t;
    s32 n;

    ch &= 0xFE;
    ch ^= 8;
    n = 2;
    vol <<= 8;
    t = &pSoundEffectsInstances->m94_tracks[ch];
    do {
        if (t->m0_flags & 1) {
            t->m76_volume = vol;
            t->m2_update = 0x100;
        }
        t++;
        n--;
    } while (n != 0);
}

void fadeSoundEffectVolume(s32 id, s32 vol, s32 dur) {
    SoundTrack* t;
    s32 n;
    s32 d;

    vol <<= 8;
    n = numMaxSoundEffectInstances;
    t = pSoundEffectsInstances->m94_tracks;
    do {
        if ((t->m0_flags & 1) && t->m8.m8_id == id) {
            d = vol - t->m76_volume;
            if (d != 0) {
                if (dur == 0) {
                    dur = 1;
                }
                t->m8C_volumeStep = d / dur;
                t->m8E_volumeTarget = vol;
                t->m9A_volumeFade = dur;
                t->m4_flags |= 0x20;
            }
        }
        t++;
        n--;
    } while (n != 0);
}

void fadeSoundEffectChannelVolume(s32 ch, s32 vol, s32 dur) {
    SoundTrack* t;
    s32 n;
    s32 d;

    ch &= 0xFE;
    ch ^= 8;
    n = 2;
    vol <<= 8;
    t = &pSoundEffectsInstances->m94_tracks[ch];
    do {
        if (t->m0_flags & 1) {
            d = vol - t->m76_volume;
            if (d != 0) {
                if (dur == 0) {
                    dur = 1;
                }
                t->m8C_volumeStep = d / dur;
                t->m8E_volumeTarget = vol;
                t->m9A_volumeFade = dur;
                t->m4_flags |= 0x20;
            }
        }
        t++;
        n--;
    } while (n != 0);
}

void setSoundEffectPan(s32 id, s32 pan) {
    SoundTrack* t;
    s32 n;

    pan <<= 8;
    n = numMaxSoundEffectInstances;
    t = pSoundEffectsInstances->m94_tracks;
    do {
        if ((t->m0_flags & 1) && t->m8.m8_id == id) {
            t->m74_pan = pan;
            t->m2_update = 0x100;
        }
        t++;
        n--;
    } while (n != 0);
}

void setSoundEffectChannelPan(s32 ch, s32 pan) {
    SoundTrack* t;
    s32 n;

    ch &= 0xFE;
    ch ^= 8;
    n = 2;
    pan <<= 8;
    t = &pSoundEffectsInstances->m94_tracks[ch];
    do {
        if (t->m0_flags & 1) {
            t->m74_pan = pan;
            t->m2_update = 0x100;
        }
        t++;
        n--;
    } while (n != 0);
}

s32 getPlayingSoundEffectMask(s32 id) {
    SoundTrack* t;
    s32 n;
    s32 bit;
    s32 mask;

    bit = 1;
    t = pSoundEffectsInstances->m94_tracks;
    n = numMaxSoundEffectInstances;
    mask = 0;
    if (id == -1) {
        do {
            if (t->m0_flags & 1) {
                mask |= bit;
            }
            t++;
            n--;
            bit <<= 1;
        } while (n != 0);
    } else {
        do {
            if ((t->m0_flags & 1) && t->m8.m8_id == id) {
                mask |= bit;
            }
            t++;
            n--;
            bit <<= 1;
        } while (n != 0);
    }
    return mask;
}

s32 findFreeSoundEffectSlot(s32 id, s32 count) {
    SoundInstance* inst;
    SoundTrack* t;
    s32 n;
    u32 used;
    u32 idx;
    u32 lim;
    u32 full;
    u32 bits;
    u32 free;
    u32 bestTime;
    s32 best;
    s32 k;

    inst = pSoundEffectsInstances;
    n = numMaxSoundEffectInstances;
    used = 0;
    t = inst->m94_tracks;
    do {
        n--;
        if ((t->m0_flags & 1) && t->m8.m8_id == id) {
            t->m0_flags = 0;
            inst->m48_activeVoices &= ~(1 << t->m6_voiceIndex);
            releaseSpuVoice(&t->m30_voice, t->m27_voice);
        }
        t++;
    } while (n != 0);
    lim = inst->m14_trackCount - soundEffectVoiceCount;
    k = count + 2;
    idx = numMaxSoundEffectInstances - k;
    full = (u32)-1 >> (32 - count);
    bits = full << idx;
    inst = pSoundEffectsInstances;
    t = &inst->m94_tracks[idx];
    free = ~used & inst->m48_activeVoices;
    bestTime = -1;
    while (free & bits) {
        if (t->mC_time < bestTime && t->m7_priority < 0x21) {
            bestTime = t->mC_time;
            best = idx;
        }
        bits >>= count;
        if (bits < full || lim >= idx) {
            idx = best;
            break;
        }
        t -= count;
        idx -= count;
    }
    return idx;
}

u32 isMusicInstancePlaying(SoundInstance* inst) {
    return inst->m10_flags >> 15;
}

void setMusicInstanceTempo(SoundInstance* inst, s32 tempo, s32 dur) {
    s32 d;

    if (tempo == 0) {
        tempo = 0x100;
    }
    inst->m64_tempo.mA_target = tempo;
    if (dur == 0) {
        inst->m64_tempo.m8_counter = 0;
        inst->m64_tempo.m0_value = tempo << 16;
        inst->m54_tickRate = inst->m58.s.m5A_hi * tempo;
    } else {
        d = (tempo << 16) - inst->m64_tempo.m0_value;
        if (d != 0) {
            inst->m64_tempo.m8_counter = dur;
            inst->m64_tempo.m4_step = d / dur;
        }
    }
}

void playBattleMusic(SoundInstance* inst, s32 vol, s32 dur) {
    s32 d;

    inst->m70_volume.mA_target = vol << 8;
    if (dur == 0) {
        inst->m70_volume.m0_value = vol << 24;
        inst->m70_volume.m8_counter = 0;
        applyFlagToAllVoices(0x100, inst);
    } else {
        d = (vol << 16) - (inst->m70_volume.m0_value >> 8);
        if (d == 0) {
            return;
        }
        inst->m70_volume.m8_counter = dur;
        inst->m70_volume.m4_step = (d / dur) << 8;
    }
    if ((inst->m10_flags & 0x100) && vol != 0) {
        playBattleMusicSub1(inst);
    }
}

void setMusicInstancePitch(SoundInstance* inst, s32 val, s32 dur) {
    s32 d;

    inst->m7C_pitch.mA_target = val << 8;
    if (dur == 0) {
        inst->m7C_pitch.m0_value = val << 24;
        inst->m7C_pitch.m8_counter = 0;
        applyFlagToAllVoices(0x200, inst);
    } else {
        d = (val << 16) - (inst->m7C_pitch.m0_value >> 8);
        if (d != 0) {
            inst->m7C_pitch.m8_counter = dur;
            inst->m7C_pitch.m4_step = (d / dur) << 8;
        }
    }
}

void setMusicInstancePan(SoundInstance* inst, s32 val, s32 dur) {
    s32 d;

    inst->m88_pan.mA_target = val << 8;
    if (dur == 0) {
        inst->m88_pan.m0_value = val << 24;
        inst->m88_pan.m8_counter = 0;
        applyFlagToAllVoices(0x100, inst);
    } else {
        d = (val << 16) - (inst->m88_pan.m0_value >> 8);
        if (d != 0) {
            inst->m88_pan.m8_counter = dur;
            inst->m88_pan.m4_step = (d / dur) << 8;
        }
    }
}

void playBattleMusicSub1(SoundInstance* inst) {
    DisableEvent(audioTickEvent);
    if (musicStatusFlag & 0x1000) {
        setupReverb((u8)inst->m41_reverbMode, inst->m44_reverbDepth, inst->m42_reverbDelay, inst->m43_reverbFeedback);
    }
    playBattleMusicSub1Sub0(inst, 0xFFFF);
    playBattleMusicSub1Sub1(inst);
    inst->m10_flags = (inst->m10_flags & 0xFEFF) | 0x8000;
    EnableEvent(audioTickEvent);
}

void setMusicInstanceTrackMuteMask(SoundInstance* inst, u32 mask) {
    SoundTrack* t;
    s32 n;

    if (inst != NULL) {
        t = inst->m94_tracks;
        n = inst->m14_trackCount;
        inst->m4C_muteMask = mask;
        do {
            if (t->m0_flags != 0) {
                if (mask & 1) {
                    if (!(t->m0_flags & 0x20)) {
                        t->m0_flags |= 0x20;
                        if ((s16)inst->m10_flags & 0x8000) {
                            voicePostUpdate2Sub1(&t->m30_voice, t->m27_voice);
                        }
                    }
                } else {
                    if (t->m0_flags & 0x20) {
                        t->m0_flags &= ~0x20;
                        if ((*(u32*)t & 0x110) == 0x100) {
                            if ((s16)inst->m10_flags & 0x8000) {
                                voicePostUpdate2Sub0(&t->m30_voice, t->m27_voice);
                            }
                        }
                    }
                }
            }
            t++;
            n--;
            mask >>= 1;
        } while (n != 0);
    }
}

void setMusicLoopSelector(SoundInstance* inst, s32 v) {
    inst->m1B_loopSelector = v;
}

void getMusicInstancePlayTime(SoundInstance* inst, SoundTime* out) {
    u32 x;
    u32 y;

    x = (u32)inst->m28_elapsedTime >> 8;
    y = x / 240;
    out->m0_tickCount = inst->m24_tickCount;
    out->m4_subSecond = x % 240;
    out->m6_seconds = y % 60;
    out->m8_minutes = y / 60;
}

s16* getMusicInstanceLoopCount(SoundInstance* inst) {
    SoundTrack* t;
    u16 n;
    s16* out;
    u16 min;

    t = inst->m94_tracks;
    n = inst->m14_trackCount;
    out = &inst->m30_minLoopCount;
    min = 0xFFFF;
    do {
        if (t->m0_flags != 0 && (u16)t->m20_loopCount < min) {
            min = t->m20_loopCount;
        }
        t++;
        n--;
    } while (n != 0);
    if (min == 0xFFFF) {
        min = 0;
    }
    *out = min;
    return out;
}

u8* getSoundInstanceSeqName(SoundInstance* inst) {
    SoundInstance* p;

    p = pPlayingSoundsLinkedList;
    if (inst != NULL) {
        while (p != NULL) {
            if (p == inst) {
                break;
            }
            p = p->m0_next;
        }
    }
    if (p == NULL) {
        return NULL;
    }
    return (u8*)p->m8_seq + p->m8_seq->u1C.s.m1E;
}

void processMusicInstanceSnapshot(SoundInstance* inst, s32 mode) {
    switch (mode) {
    case 0:
        freeMusicInstanceSnapshot(inst);
        break;
    case 1:
        saveMusicInstanceSnapshot(inst);
        break;
    case 2:
        restoreMusicInstanceSnapshot(inst);
        break;
    }
}

void freeMusicInstanceSnapshot(SoundInstance* inst) {
    if (inst->m10_flags & 0x10) {
        inst->m10_flags &= ~0x10;
        freeSoundInstanceField4(inst);
    }
}

void saveMusicInstanceSnapshot(SoundInstance* inst) {
    s32 size;
    SoundInstance* copy;

    DisableEvent(audioTickEvent);
    size = getSoundInstanceSize(inst->m14_trackCount);
    if (inst->m4_snapshot == NULL) {
        copy = allocateFromSoundArena(size);
    }
    if (copy == NULL) {
        EnableEvent(audioTickEvent);
        return;
    }
    inst->m4_snapshot = copy;
    inst->m10_flags |= 0x10;
    copySoundMemory(copy, inst, size);
    copy->m0_next = NULL;
    copy->m4_snapshot = NULL;
    inst->m2C_loopEndTick = 0;
    EnableEvent(audioTickEvent);
}

void restoreMusicInstanceSnapshot(SoundInstance* inst) {
    s32 pos;

    if (inst->m4_snapshot != NULL && (inst->m10_flags & 0x10)) {
        DisableEvent(audioTickEvent);
        startMusicInstanceSub0Sub(inst);
        pos = inst->m24_tickCount;
        copySoundInstanceState(inst, inst->m4_snapshot);
        inst->m2C_loopEndTick = pos;
        playBattleMusicSub1Sub0(inst, 0xFFFF);
        playBattleMusicSub1Sub1(inst);
        EnableEvent(audioTickEvent);
    }
}

void seekMusicToMeasure(SoundInstance* inst, s32 v) {
    s32 tempo;

    inst->m1E_seekMeasure = v;
    inst->m10_flags |= 0x20;
    DisableEvent(audioTickEvent);
    startMusicInstanceSub0Sub(inst);
    EnableEvent(audioTickEvent);
    tempo = 0x7F00;
    inst->m64_tempo.mA_target = tempo;
    inst->m64_tempo.m8_counter = 0;
    inst->m64_tempo.m0_value = tempo << 16;
    inst->m54_tickRate = inst->m58.s.m5A_hi * tempo;
}

void playBattleMusicSub1Sub1(SoundInstance* inst) {
    SoundTrack* t;
    s32 n;

    n = inst->m14_trackCount;
    t = inst->m94_tracks;
    do {
        if ((*(u32*)t & 0x101) == 0x101 && !(t->m0_flags & 0x30)) {
            t->m2_update |= 1;
        }
        t++;
        n--;
    } while (n != 0);
}

void keyOffMusicInstance(SoundInstance* inst) {
    s32 n;
    SoundTrack* t;

    n = inst->m14_trackCount;
    t = inst->m94_tracks;
    do {
        if (t->m0_flags != 0) {
            keyOffSpuVoice(&t->m30_voice, t->m27_voice);
        }
        t++;
        n--;
    } while (n != 0);
}

void startMusicInstanceSub0Sub(SoundInstance* inst) {
    SoundTrack* t;
    s32 n;

    t = inst->m94_tracks;
    n = inst->m14_trackCount;
    do {
        releaseSpuVoice(&t->m30_voice, t->m27_voice);
        t++;
        n--;
    } while (n != 0);
}

void initPercussions(SoundInstance* inst, SeqFile* seq) {
    u8* perc;
    u8* p;
    s32 n;
    u32 v;

    perc = (u8*)inst + getSoundInstanceSize(seq->u14.s.m14_trackCount);
    inst->mC_percussion = perc;
    p = (u8*)seq + seq->m20_drumKitOffset;
    n = seq->u14.s.m15_percussionCount;
    do {
        v = p[1] | (p[2] << 8) | (p[3] << 16) | (p[4] << 24);
        *(u32*)(perc + (p[0] << 2)) = v;
        p += 5;
        n--;
    } while (n != 0);
}

SoundInstance* initSoundEffectInstances(s32 n) {
    SoundInstance* inst;
    SoundTrack* t;
    s32 i;
    s32 voice;
    s32 cnt;

    n &= ~1;
    numMaxSoundEffectInstances = n;
    inst = allocateFromSoundArena(getSoundInstanceSize(n));
    if (inst == NULL) {
        setSoundError(0x1E);
        return NULL;
    }
    initSoundEffectSoundInstance(inst);
    t = inst->m94_tracks;
    voice = 0x18 - n;
    cnt = n;
    i = 0;
    do {
        t->m0_flags = 0;
        t->m6_voiceIndex = i++;
        t->m27_voice = voice;
        t++;
        voice++;
    } while (--cnt);
    addActiveSoundInstance(inst);
    return inst;
}

void removeAndFreeSoundInstance(SoundInstance* inst) {
    removePlayingSound(inst);
    freeBackToSoundArena(inst);
}

void initSoundInstance(SoundInstance* inst) {
    SeqFile* seq;

    seq = inst->m8_seq;
    if (createMusicInstanceSub0(seq) != 0) {
        setSoundError(0xA);
        return;
    }
    inst->m10_flags |= 1;
    inst->m12_seqId = seq->m10_id;
    inst->m14_trackCount = seq->u14.s.m14_trackCount;
    inst->m16_sampleBank = seq->m16_sampleBank;
    inst->m18 = seq->m18;
    inst->m41_reverbMode = seq->m1A_reverbMode;
    inst->m44_reverbDepth = seq->m1B_reverbDepth << 8;
    inst->m42_reverbDelay = seq->u1C.s.m1C;
    inst->m43_reverbFeedback = seq->u1C.s.m1D;
    if (musicStatusFlag & 0x1000) {
        setupReverb(inst->m41_reverbMode, inst->m44_reverbDepth, inst->m42_reverbDelay, inst->m43_reverbFeedback);
    }
    initSoundInstanceDefaults(inst);
}

void initSoundEffectSoundInstance(SoundInstance* inst) {
    inst->m10_flags = 2;
    inst->m12_seqId = 0x7FFF;
    inst->m16_sampleBank = 0;
    inst->m18 = 0x7F;
    inst->m14_trackCount = numMaxSoundEffectInstances;
    initSoundInstanceDefaults(inst);
}

void initSoundInstanceDefaults(SoundInstance* inst) {
    freeSoundInstanceField4(inst);
    inst->m32_measure = 1;
    inst->m36_tick = 1;
    inst->m1A_instanceVar = 0;
    inst->m1B_loopSelector = 0;
    inst->m30_minLoopCount = 0;
    inst->m34_beat = 0;
    inst->m38_beatsPerMeasure = 4;
    inst->m3A_ticksPerBeat = 0x30;
    inst->m3C_timeSigUnit = 4;
    inst->m3E_timeSigBeats = 4;
    inst->m64_tempo.m0_value = 0x1000000;
    inst->m70_volume.m0_value = 0x7F000000;
    inst->m58.m58_tempo = 0x660000;
    inst->m54_tickRate = 0x6600;
    inst->m28_elapsedTime = 0;
    inst->m24_tickCount = 0;
    inst->m20_updateCount = 0;
    inst->m48_activeVoices = 0;
    inst->m7C_pitch.m0_value = 0;
    inst->m88_pan.m0_value = 0;
    inst->m64_tempo.m8_counter = 0;
    inst->m70_volume.m8_counter = 0;
    inst->m7C_pitch.m8_counter = 0;
    inst->m88_pan.m8_counter = 0;
    inst->m5C_tempoDelta = 0;
    inst->m60_tempoSlide = 0;
    inst->m50_tickAccum = 0x10000;
}

void initSoundInstanceTracks(SoundInstance* inst) {
    SeqFile* seq;
    u16* trk;
    void* wds;
    SoundTrack* t;
    s32 i;
    s32 voice;
    s32 n;
    u32 used;
    u32 bit;
    s32 j;
    u8* p;

    n = inst->m14_trackCount;
    t = inst->m94_tracks;
    if (n != 0) {
        i = 0;
        voice = -1;
        used = 0;
        seq = inst->m8_seq;
        trk = seq->m22_trackOffsets;
        wds = findInWdsLinkedList(inst->m16_sampleBank);
        if (wds == NULL) {
            wds = pLoadedWdsLinkedList;
        }
        do {
            if (*trk != 0) {
                bit = 1 << i;
                used |= bit;
                if (bit & inst->m4C_muteMask) {
                    t->m0_flags = 0x421;
                } else {
                    t->m0_flags = 0x401;
                }
                if (inst->m10_flags & 4) {
                    t->m0_flags |= 4;
                }
                t->m2_update = 0x170;
                t->m4_flags = 0;
                t->m8.m8_id = seq->m10_id;
                t->m7_priority = 0x10;
                t->m6_voiceIndex = i;
                p = (u8*)seq + *trk;
                t->m18_loopStart = 0;
                t->m66_octave = 0x3C;
                t->m62_gateRatio = 0xF;
                t->m72_loopDepth = -1;
                t->m76_volume = 0x6000;
                t->m78_volume = 0x7F000000;
                t->m1C = 0;
                t->m20_loopCount = 0;
                t->m22 = 0;
                t->m5C_deltaTime = 0;
                t->m60_lengthAdjust = 0;
                t->m6E_pitchBend = 0;
                t->m64_key = 0;
                t->m74_pan = 0x4000;
                t->m70_portamento = 0;
                t->mD0_pitchMod = 0;
                t->mD2_volumeMod = 0;
                t->mD4_panMod = 0;
                t->m30_voice.mC = 0;
                t->m30_voice.mE = 0;
                t->mCE_lfoActive = 0;
                t->m10_ptr = p;
                t->m14_pc = p;
                for (j = 3; j >= 0; j--) {
                    t->mD8_lfo[j].m1E_flags = 0;
                }
                t->m25_bank = inst->m16_sampleBank;
                t->m2C_wds = wds;
                if (wds != NULL) {
                    setupAdsr(0, t);
                }
                t->m27_voice = voice;
                t->m30_voice.m2_flags = 0;
                t->m30_voice.m4_priority = 0x100;
                claimSpuVoice(&t->m30_voice, voice);
            } else {
                t->m0_flags = 0;
            }
            trk++;
            t++;
            i++;
            voice++;
        } while (--n);
        inst->m48_activeVoices = used;
    }
}

/* Start SFX id (seq id << 16 | sound) on SFX tracks ch & 0xFF (priority
 * ch >> 8) with volume vol and pan (Noah: playSoundEffectSub). */
void playSoundEffectSub(u32 ch, s32 id, u32 vol, u16 pan) {
    SeqFile* seq;
    SoundInstance* inst;
    void* wds;
    u16* trk;
    SoundTrack* t;
    u8 prio;
    s32 n;
    s32 j;
    u8* p;
    u32 c;
    u32 tmp;
    u8* bp;

    c = ch;
    seq = pLoadedSequencesLinkedList;
    inst = pSoundEffectsInstances;
    while (seq->u14.m14_id != (id >> 16)) {
        seq = SEQ_NEXT(seq);
        if (seq == NULL) {
            return;
        }
    }
    wds = findInWdsLinkedList(seq->m16_sampleBank);
    if (wds == NULL) {
        wds = pLoadedWdsLinkedList;
    }
    bp = (u8*)seq + seq->m18;
    tmp = (s16)vol * bp[id & 0xFFFF];
    tmp >>= 7;
    vol = tmp;
    if ((vol >> 15) & 1) {
        vol = 0x7FFF;
    }
    trk = (u16*)((u8*)seq + ((id & 0xFFFF) * 4 + 0x20));
    prio = c >> 8;
    t = &inst->m94_tracks[c & 0xFF];
    n = playSoundEffectVar0;
    DisableEvent(audioTickEvent);
    do {
        t->m8.m8_id = id;
        t->mC_time = spuUpdateCounter;
        t->m7_priority = prio;
        if (*trk != 0) {
            inst->m48_activeVoices |= 1 << t->m6_voiceIndex;
            t->m0_flags = 0x409;
            if (seq->m10_id & 1) {
                t->m0_flags = 0x40B;
            }
            t->m2_update = 0x170;
            t->m4_flags = 0;
            p = (u8*)seq + *trk;
            t->m66_octave = 0x3C;
            t->m62_gateRatio = 0xF;
            t->m72_loopDepth = -1;
            t->m18_loopStart = 0;
            t->m1C = 0;
            t->m20_loopCount = 0;
            t->m22 = 0;
            t->m5C_deltaTime = 0;
            t->m60_lengthAdjust = 0;
            t->m6E_pitchBend = 0;
            t->m64_key = 0;
            t->m76_volume = vol;
            t->m78_volume = 0x7F000000;
            t->m74_pan = pan;
            t->m70_portamento = 0;
            t->mD0_pitchMod = 0;
            t->mD2_volumeMod = 0;
            t->mD4_panMod = 0;
            t->m30_voice.mC = 0;
            t->m30_voice.mE = 0;
            t->mCE_lfoActive = 0;
            t->m10_ptr = p;
            t->m14_pc = p;
            for (j = 3; j >= 0; j--) {
                t->mD8_lfo[j].m1E_flags = 0;
            }
            t->m25_bank = seq->m16_sampleBank;
            t->m2C_wds = wds;
            if (wds != NULL) {
                setupAdsr(0, t);
            }
            t->m30_voice.m2_flags = 0;
            t->m30_voice.m4_priority = 0x200;
            claimSpuVoice(&t->m30_voice, t->m27_voice);
        } else {
            inst->m48_activeVoices &= ~(1 << t->m6_voiceIndex);
            t->m0_flags = 0;
            releaseSpuVoice(&t->m30_voice, t->m27_voice);
        }
        trk++;
        t++;
        n--;
    } while (n != 0);
    inst->m10_flags |= 0x8000;
    EnableEvent(audioTickEvent);
}

void freeSoundInstanceField4(SoundInstance* inst) {
    SoundInstance* p;

    p = inst->m4_snapshot;
    if (p != NULL) {
        inst->m4_snapshot = NULL;
        do {
            inst = p->m4_snapshot;
            freeBackToSoundArena(p);
            p = inst;
        } while (inst != NULL);
    }
}

void copySoundInstanceState(SoundInstance* inst, SoundInstance* src) {
    SoundInstance* next;
    void* saved;

    next = inst->m0_next;
    saved = inst->m4_snapshot;
    copySoundMemory(inst, src, getSoundInstanceSize(inst->m14_trackCount));
    inst->m0_next = next;
    inst->m4_snapshot = saved;
}

void addActiveSoundInstance(SoundInstance* inst) {
    DisableEvent(audioTickEvent);
    inst->m0_next = pPlayingSoundsLinkedList;
    pPlayingSoundsLinkedList = inst;
    EnableEvent(audioTickEvent);
}

s32 removePlayingSound(SoundInstance* inst) {
    SoundInstance* p;
    SoundInstance* prev;

    p = pPlayingSoundsLinkedList;
    prev = NULL;
    while (p != NULL) {
        if (p == inst) {
            break;
        }
        prev = p;
        p = p->m0_next;
    }
    if (p == NULL) {
        setSoundError(0xF);
        return -1;
    }
    if ((s16)inst->m10_flags & 0x8000) {
        if (inst == NULL) {
            setSoundError(5);
        } else {
            inst->m10_flags &= 0x7FFF;
            startMusicInstanceSub0Sub(inst);
        }
    }
    if (prev != NULL) {
        prev->m0_next = inst->m0_next;
    } else {
        pPlayingSoundsLinkedList = inst->m0_next;
    }
    return 0;
}

void applyFlagToActiveTracks(s32 flag, SoundInstance* inst) {
    SoundTrack* t;
    s32 n;

    t = inst->m94_tracks;
    n = inst->m14_trackCount;
    do {
        if (t->m0_flags != 0) {
            t->m0_flags = flag | t->m0_flags;
        }
        n--;
        t++;
    } while (n != 0);
}

s32 getSoundInstanceSize(s32 n) {
    /* 0x158 and 0x94 on the PS1; the PC's structs hold 64-bit pointers. */
    return n * (s32)sizeof(SoundTrack) + (s32)(sptr)(&((SoundInstance*)0)->m94_tracks);
}

void spuDmaCompletionCallback(void) {
    void (*cb)(void);

    cb = loadWdsSub0SubVar1[spuDmaTransertEntry].m10_callback;
    musicStatusFlag |= 4;
    if (cb != NULL) {
        cb();
    }
    musicStatusFlag &= ~0x10;
    if (spuDmaTransertEntry != loadWdsSub0SubVar0) {
        startTransfert();
    }
    musicStatusFlag &= ~4;
}

void loadWdsSub0(s32 addr, u8* src, s32 size, void* cb) {
    loadWdsSub0Sub(addr, src, size, cb, 1);
}

void queueSpuRead(s32 addr, u8* src, s32 size, void* cb) {
    loadWdsSub0Sub(addr, src, size, cb, 2);
}

void queueSpuReadDecodedData(s32 addr, u8* src, s32 size, void* cb) {
    loadWdsSub0Sub(addr, src, size, cb, 3);
}

void queueSpuReadDecodedCdData(s32 addr, u8* src, s32 size, void* cb) {
    loadWdsSub0Sub(addr, src, size, cb, 4);
}

void loadWdsSub0Sub(s32 addr, u8* src, s32 size, void* cb, u16 mode) {
    u16 flags;
    u16 idx;
    SpuXfer* e;

    flags = musicStatusFlag;
    if (!(flags & 4)) {
        while (loadWdsSub0SubSub() != 0) {
            PORT_YIELD();
        }
        EnterCriticalSection();
    }
    idx = loadWdsSub0SubVar0 + 1;
    if (idx >= 8) {
        idx = 0;
    }
    loadWdsSub0SubVar0 = idx;
    e = &loadWdsSub0SubVar1[idx];
    e->m0_mode = mode & 0xF;
    e->m2 = 0;
    e->m4_src = src;
    e->m8_spuAddr = addr & 0x7FFF8;
    e->mC_size = size;
    e->m10_callback = cb;
    if (!(musicStatusFlag & 0x10)) {
        startTransfert();
    }
    if (!(flags & 4)) {
        ExitCriticalSection();
    }
}

s32 loadWdsSub0SubSub(void) {
    u16 w;
    u16 r;

    w = loadWdsSub0SubVar0;
    r = spuDmaTransertEntry;
    if (w < r) {
        w += 8;
    }
    return w - r >= 6;
}

void func_8003BDF4(void) {
}

s32 waitForMusic(s32 wait) {
    if (wait & 0x10) {
        while (*(volatile s16*)&musicStatusFlag & 0x10) {
            PORT_YIELD();
        }
    }
    if (musicStatusFlag & 0x10) {
        return (s16)loadWdsSub0SubVar1[spuDmaTransertEntry].m0_mode;
    }
    return 0;
}

void startTransfert(void) {
    u16 idx;
    SpuXfer* e;
    void* old;

    idx = spuDmaTransertEntry + 1;
    if (idx >= 8) {
        idx = 0;
    }
    spuDmaTransertEntry = idx;
    musicStatusFlag |= 0x10;
    e = &loadWdsSub0SubVar1[idx];
    old = SpuSetTransferCallback(spuDmaCompletionCallback);
    SpuSetTransferMode(0);
    SpuSetTransferStartAddr(e->m8_spuAddr);
    switch (e->m0_mode) {
    case 0:
        break;
    case 1:
        SpuWrite(e->m4_src, e->mC_size);
        break;
    case 2:
        SpuRead(e->m4_src, e->mC_size);
        break;
    case 3:
        spuDecodedDataHalf = SpuReadDecodedData(e->m4_src, 0);
        break;
    case 4:
        spuDecodedDataHalf = SpuReadDecodedData(e->m4_src, 5);
        break;
    }
    if (old != spuDmaCompletionCallback) {
        setSoundError(0x26);
    }
}

void spuIrqCallback(void) {
    void (*cb)(void);

    cb = spuIrqHook;
    musicStatusFlag |= 4;
    spuIrqCount++;
    if (cb != NULL) {
        cb();
    }
    musicStatusFlag &= ~4;
}

void setSpuIrqHook(void (*cb)(void)) {
    spuIrqHook = cb;
}


/* Root counter 2 tick (Noah: audioTick). */
s32 audioTick(void) {
    SoundInstance* inst;
    u32 start;
    u32 now;
    s16 count;
    SoundTrack* chs;

    if (musicStatusFlag & 0x40) {
        return 0;
    }
    start = GetRCnt(0xF2000002);
    if (spuUpdateCounter++ & 1) {
        if (g_spuVolumes.m30_master.m8_counter != 0) {
            interpolateAudioParam(&g_spuVolumes.m30_master);
            g_spuVolumes.m28_masterVolume = g_spuVolumes.m30_master.m0_value >> 16;
            setVolumePairForOutputMode(g_spuVolumes.m28_masterVolume, &g_spuVolumes.m4_mvol, 0);
            do {
                g_spuVolumes.m0_mask |= 3;
            } while (0);
        }
        if (g_spuVolumes.m3C_cd.m8_counter != 0) {
            interpolateAudioParam(&g_spuVolumes.m3C_cd);
            g_spuVolumes.m10_cdVolume.left = g_spuVolumes.m10_cdVolume.right = g_spuVolumes.m2A_cdVolume = g_spuVolumes.m3C_cd.m0_value >> 16;
            /* do/while (0) (a macro in the original?): this mask update then
             * counts as a loop, which gives it local-alloc priority ($v0). */
            do {
                g_spuVolumes.m0_mask |= 0xC0;
            } while (0);
        }
        if (g_spuVolumes.m0_mask != 0) {
            SpuSetCommonAttr(&g_spuVolumes);
            g_spuVolumes.m0_mask = 0;
        }
    }
    sendAdsrToSpu();
    for (inst = pPlayingSoundsLinkedList; inst != NULL; inst = inst->m0_next) {
        if ((s16)inst->m10_flags >= 0) {
            continue;
        }
        if (inst->m2C_loopEndTick != 0 && inst->m24_tickCount >= inst->m2C_loopEndTick) {
            restoreMusicInstanceSnapshot(inst);
        }
        if (inst->m64_tempo.m8_counter != 0) {
            interpolateAudioParam(&inst->m64_tempo);
            inst->m54_tickRate = (s16)(inst->m58.m58_tempo >> 16) * (s16)(inst->m64_tempo.m0_value >> 16);
        }
        if (inst->m70_volume.m8_counter != 0) {
            interpolateAudioParam(&inst->m70_volume);
            applyFlagToAllVoices(0x100, inst);
        }
        if (inst->m7C_pitch.m8_counter != 0) {
            interpolateAudioParam(&inst->m7C_pitch);
            applyFlagToAllVoices(0x200, inst);
        }
        if (inst->m88_pan.m8_counter != 0) {
            interpolateAudioParam(&inst->m88_pan);
            applyFlagToAllVoices(0x100, inst);
        }
        inst->m20_updateCount++;
        inst->m28_elapsedTime += (s16)(inst->m64_tempo.m0_value >> 16);
        inst->m50_tickAccum -= inst->m54_tickRate;
        while (inst->m50_tickAccum < 0) {
            inst->m50_tickAccum += 0x10000;
            if (--inst->m36_tick == 0) {
                inst->m36_tick = inst->m3A_ticksPerBeat;
                if (++inst->m34_beat > inst->m38_beatsPerMeasure) {
                    inst->m34_beat = 1;
                    inst->m32_measure++;
                }
            }
            count = inst->m14_trackCount;
            if (count != 0) {
                chs = inst->m94_tracks;
                executeSequenceEvents2(inst, chs, count);
                executeSequenceEvents(inst, chs, count);
            }
            if (inst->m48_activeVoices != 0) {
                inst->m24_tickCount++;
                if (inst->m70_volume.m0_value == 0) {
                    startMusicInstanceSub0(inst);
                    inst->m10_flags |= 0x100;
                }
                if (inst->m32_measure == inst->m1E_seekMeasure) {
                    inst->m10_flags &= ~0x20;
                    setMusicInstanceTempo(inst, 0, 0);
                    inst->m1E_seekMeasure = 0;
                }
            } else {
                inst->m10_flags &= 0x7FFF;
                break;
            }
        }
    }
    for (inst = pPlayingSoundsLinkedList; inst != NULL; inst = inst->m0_next) {
        if ((s16)inst->m10_flags < 0) {
            count = inst->m14_trackCount;
            if (count != 0) {
                chs = inst->m94_tracks;
                voicePostUpdate1(inst, chs, count);
                voicePostUpdate2(inst, chs, count);
            }
        }
    }
    audioTickSub1();
    if (spuIrqEnableRequest & 1) {
        spuIrqEnableRequest &= ~1;
        SpuSetIRQ(1);
    }
    now = GetRCnt(0xF2000002);
    if (now >= start) {
        audioTickTotalTime += now - start;
        audioTickCount++;
    }
    return 0;
}

/* Step an interpolated value (Noah: interpolateAudioParam). */
void interpolateAudioParam(AudioParam* a) {
    if (--a->m8_counter != 0) {
        a->m0_value += a->m4_step;
    } else {
        a->m0_value = a->mA_target << 16;
    }
}

/* Noah: executeSequenceEvents2. */
void executeSequenceEvents2(SoundInstance* inst, SoundTrack* ch, s16 count) {
    u16 n;

    n = inst->m60_tempoSlide;
    if (n != 0) {
        n--;
        if (n != 0) {
            inst->m58.m58_tempo += inst->m5C_tempoDelta;
        } else {
            inst->m58.m58_tempo = inst->m62_tempoTarget << 16;
        }
        inst->m60_tempoSlide = n;
        inst->m54_tickRate = (s16)(inst->m58.m58_tempo >> 16) * (s16)(inst->m64_tempo.m0_value >> 16);
    }
    do {
        u16 flags0 = ch->m0_flags;

        if (flags0 != 0) {
            u32 w = *(u32*)&ch->m5C_deltaTime;
            u16 upd = ch->m2_update;
            u32 delta = w & 0xFFFF;
            u32 gate = w >> 16;

            if (delta != 0) {
                u16 f4 = ch->m4_flags;

                if (f4 & 8) {
                    upd |= 0x100;
                    if (--ch->m96_volumeSlide == 0) {
                        f4 &= ~8;
                    }
                    ch->m78_volume += ch->m88_volumeDelta;
                }
                if (f4 & 1) {
                    upd |= 0x200;
                    if (!(f4 & 2)) {
                        if (--ch->m94_pitchSlide == 0) {
                            f4 &= ~1;
                        }
                    }
                    ch->m68_pitch += ch->m84_pitchDelta;
                }
                if (f4 & 0x10) {
                    if (--ch->m98_panSlide == 0) {
                        ch->m74_pan = ch->m92_panTarget;
                        f4 &= ~0x10;
                    } else {
                        ch->m74_pan += ch->m90_panStep;
                    }
                    upd |= 0x100;
                }
                if (f4 & 0x20) {
                    if (--ch->m9A_volumeFade == 0) {
                        ch->m76_volume = ch->m8E_volumeTarget;
                        f4 &= ~0x20;
                    } else {
                        ch->m76_volume += ch->m8C_volumeStep;
                    }
                    upd |= 0x100;
                }
                ch->m4_flags = f4;
                delta--;
                gate--;
                if (delta == 1 && (flags0 & 0x1000)) {
                    ch->m30_voice.m2A_release = 6;
                    ch->m30_voice.m6_dirty |= 0x80;
                }
                if (gate == 0) {
                    ch->m0_flags |= 0x400;
                    upd |= 2;
                }
                *(u32*)&ch->m5C_deltaTime = delta + (gate << 16);
            }
            ch->m2_update = upd;
        }
        ch++;
    } while (--count);
}

void executeSequenceEvents(SoundInstance* inst, SoundTrack* chs, s16 count) {
    SoundTrack* ch = chs;
    u8* p;
    u16 orig;
    s32 needPost;
    s16 op;
    s32 i;
    s16 time;


    do {
        if (ch->m0_flags != 0 && ch->m5C_deltaTime == 0) {
            needPost = 0;
            orig = ch->m0_flags;
            p = ch->m14_pc;
            ch->m0_flags = orig & 0xF8FF;
            do {
                op = *p++;
                if (op < 0x80) {
                    s32 key;

                    if (!(ch->m0_flags & 8)) {
                        ch->m76_volume = op << 8;
                    }
                    ch->m2_update |= 0x100;
                    time = *p++;
                    key = ch->m65_key = ch->m66_octave + noteKeyTable[time];
                    time = noteDeltaTimeTable[time];
                    if (time == 0) {
                        time = *p++;
                    }
                    ch->m5C_deltaTime = time;
                    ch->m30_voice.m2A_release = ch->m28_releaseBackup;
                    ch->m30_voice.m6_dirty |= 0x80;
                    if (ch->m0_flags & 0x10) {
                        processPercussions(inst, ch, key);
                    } else {
                        ch->m68_pitch = ((key << 8) + ch->m6E_pitchBend + ch->m6C_transpose) << 16;
                    }
                    ch->m2_update |= 0x200;
                    ch->m0_flags |= 0x180;
                    needPost = 1;
                    if (orig & 0x400) {
                        ch->m2_update |= 1;
                    }
                    if (ch->m0_flags & 0x8000) {
                        ch->m0_flags &= 0x7FFF;
                        ch->m30_voice.m6_dirty = 0xFFFF;
                        ch->m2_update |= 0x300;
                    }
                } else {
                    p = seqOpcodes[(s16)(op - 0x80)](p, inst, ch);
                    if (ch->m0_flags == 0) {
                        inst->m48_activeVoices &= ~(1 << ch->m6_voiceIndex);
                        break;
                    }
                }
            } while (!(ch->m0_flags & 0x500));
            ch->m14_pc = p;
            if (ch->m0_flags == 0) {
                ch++;
                continue;
            }
            {
                SoundLoop* loop;
                u8 adj;
                u16 gate;

                if (ch->m0_flags & 0x800) {
                    ch->m0_flags |= 0x200;
                }
                loop = &ch->m9C_loops[ch->m72_loopDepth];
                op = *p;
                while (op >= 0x80) {
                    if ((s16)op == 0x90) {
                        p = ch->m18_loopStart;
                        if (p != NULL) {
                            goto next;
                        }
                        break;
                    } else {
                        if ((s16)op == 0x80) {
                            ch->m0_flags &= ~0x200;
                            break;
                        }
                        if ((s16)op == 0x81) {
                            ch->m0_flags |= 0x200;
                            break;
                        }
                        if (op >= 0xB0 && op <= 0xB1) {
                            ch->m0_flags &= ~0x200;
                            break;
                        }
                        if ((s16)op == 0x99) {
                            if (loop->m0_count != 0) {
                                p = loop->m4_start;
                                goto next;
                            }
                            loop--;
                        }
                        if (op == 0x9A && loop->m0_count == 0) {
                            p = loop->m8_end;
                            loop--;
                        } else {
                            p += opcodeSizes[(s16)(op - 0x80)];
                        }
                    }
                next:
                    op = *p;
                }
                i = 0;
                if (op < 0x80) {
                    ch->m0_flags |= 0x1000;
                } else {
                    ch->m0_flags &= ~0x1000;
                }
                adj = ch->m60_lengthAdjust;
                time = (s8)adj + ch->m5C_deltaTime;
                if ((s16)time <= 0) {
                    ch->m60_lengthAdjust = adj + (u8)ch->m5C_deltaTime;
                    time = ch->m5C_deltaTime + time;
                }
                gate = 0x7FFF;
                if (!(ch->m0_flags & 0x600)) {
                    switch (ch->m62_gateRatio) {
                    case 16:
                        gate = time;
                        goto store;
                    case 15:
                        gate = time - 1;
                        break;
                    default:
                        gate = ((s16)time * ch->m62_gateRatio) >> 4;
                        break;
                    }
                    if ((gate & 0xFFFF) == 0) {
                        gate = 1;
                    }
                }
            store:
                *(s32*)&ch->m5C_deltaTime = (s16)time + (gate << 16);
                if (needPost) {
                    if (ch->m4_flags & 4) {
                        s32 diff = (ch->m65_key - ch->m64_key) << 24;

                        if (diff != 0) {
                            ch->m84_pitchDelta = R3000_DIV(diff, ch->m70_portamento);
                            ch->m94_pitchSlide = ch->m70_portamento;
                            ch->m4_flags |= 1;
                            ch->m68_pitch = ((ch->m64_key << 8) + ch->m6E_pitchBend + ch->m6C_transpose) << 16;
                        }
                    }
                    ch->m64_key = ch->m65_key;
                    if (ch->m4_flags & 0x100) {
                        ch->m96_volumeSlide = ch->m80_noteVolumeSlideLength;
                        ch->m88_volumeDelta = ch->m7C_noteVolumeDelta;
                        ch->m78_volume = ch->m82_noteVolumeStart << 16;
                        ch->m4_flags |= 8;
                    }
                    {
                        SoundLfo* lfo = ch->mD8_lfo;
                        for (i = 4; i != 0; i--, lfo++) {
                            u16 f = lfo->m1E_flags;
                            if ((f & 3) == 3) {
                                lfo->m4_value = 0;
                                lfo->m10_counter = 1;
                                lfo->m14_delay = lfo->m16_delayReload;
                                lfo->m18_fade = lfo->m1A_fadeStep;
                                ch->m2_update |= 0x100;
                                lfo->m1E_flags = f & ~0xC;
                            }
                        }
                    }
                }
            }
        }
        ch++;
    } while (--count);
}

/* Noah: processPercussions. */
void processPercussions(SoundInstance* inst, SoundTrack* ch, u8 key) {
    u8* perc = &inst->mC_percussion[key * 4];

    setupAdsr(perc[0], ch);
    ch->m68_pitch = ((perc[1] << 8) + ch->m6E_pitchBend + ch->m6C_transpose) << 16;
    ch->m74_pan = perc[3] << 8;
    ch->m2_update |= 0x100;
}

/* Unused opcodes. */
u8* seqOP_unused(u8* p, SoundInstance* inst, SoundTrack* ch) {
    return p;
}

/* 0x80 (Noah: seqOP_0_restAndNoteOff). */
u8* seqOP_0_restAndNoteOff(u8* p, SoundInstance* inst, SoundTrack* ch) {
    ch->m5C_deltaTime = *p;
    ch->m0_flags |= 0x400;
    ch->m2_update |= 2;
    return p + 1;
}

/* 0x81 (Noah: seqOP_1_hold). */
u8* seqOP_1_hold(u8* p, SoundInstance* inst, SoundTrack* ch) {
    u8 v = *p;

    ch->m0_flags |= 0x100;
    ch->m5C_deltaTime = v;
    return p + 1;
}

/* 0x8A */
u8* seqOP_A_nop(u8* p, SoundInstance* inst, SoundTrack* ch) {
    return p;
}

/* 0x8D: loop point taken when the byte matches the instance's m1B. */
u8* seqOP_D_selectLoopPoint(u8* p, SoundInstance* inst, SoundTrack* ch) {
    if (*p++ == inst->m1B_loopSelector) {
        ch->m18_loopStart = p;
        ch->m23_loopOctave = ch->m66_octave;
    }
    return p;
}

/* 0x8E */
u8* seqOP_E_skip3(u8* p, SoundInstance* inst, SoundTrack* ch) {
    return p + 3;
}

/* 0x8F */
u8* seqOP_F_nop(u8* p, SoundInstance* inst, SoundTrack* ch) {
    return p;
}

/* 0x90 (Noah: seqOP_10_end). */
u8* seqOP_10_end(u8* p, SoundInstance* inst, SoundTrack* ch) {
    if (ch->m18_loopStart != NULL) {
        p = ch->m18_loopStart;
        ch->m20_loopCount++;
        ch->m66_octave = ch->m23_loopOctave;
    } else {
        ch->m2_update &= 0xFFFC;
        releaseSpuVoice(&ch->m30_voice, ch->m27_voice);
        ch->m0_flags = 0;
    }
    return p;
}

/* 0x91 (Noah: seqOP_11_infiniteLoop). */
u8* seqOP_11_infiniteLoop(u8* p, SoundInstance* inst, SoundTrack* ch) {
    ch->m18_loopStart = p;
    ch->m23_loopOctave = ch->m66_octave;
    return p;
}

/* 0x94 (Noah: seqOP_14_setOctave). */
u8* seqOP_14_setOctave(u8* p, SoundInstance* inst, SoundTrack* ch) {
    ch->m66_octave = p[0] * 12;
    return p + 1;
}

/* 0x95 (Noah: seqOP_15_incOctave). */
u8* seqOP_15_incOctave(u8* p, SoundInstance* inst, SoundTrack* ch) {
    ch->m66_octave += 12;
    return p;
}

/* 0x96 (Noah: seqOP_16_decOctave). */
u8* seqOP_16_decOctave(u8* p, SoundInstance* inst, SoundTrack* ch) {
    ch->m66_octave -= 12;
    return p;
}

/* 0x97 (Noah: seqOP_17_timeSignature). */
u8* seqOP_17_timeSignature(u8* p, SoundInstance* inst, SoundTrack* ch) {
    u16 beats = p[0];
    u16 unit = p[1];

    inst->m3A_ticksPerBeat = R3000_DIV(192, unit);
    inst->m3C_timeSigUnit = unit;
    inst->m38_beatsPerMeasure = beats;
    inst->m3E_timeSigBeats = beats;
    inst->m36_tick = inst->m3A_ticksPerBeat;
    return p + 2;
}

/* 0xF9: set measure and beat. */
u8* seqOP_79_setMeasure(u8* p, SoundInstance* inst, SoundTrack* ch) {
    inst->m32_measure = p[0];
    inst->m34_beat = p[1];
    inst->m36_tick = inst->m3A_ticksPerBeat;
    return p + 2;
}

/* 0xA4 */
u8* seqOP_24_setInstanceVar(u8* p, SoundInstance* inst, SoundTrack* ch) {
    inst->m1A_instanceVar = p[0];
    return p + 1;
}

/* 0xA5 */
u8* seqOP_25_adjustInstanceVar(u8* p, SoundInstance* inst, SoundTrack* ch) {
    inst->m1A_instanceVar += p[0];
    return p + 1;
}

/* 0x98 (Noah: seqOP_18_loopStart). */
u8* seqOP_18_loopStart(u8* p, SoundInstance* inst, SoundTrack* ch) {
    SoundLoop* loop;

    ch->m72_loopDepth++;
    loop = &ch->m9C_loops[ch->m72_loopDepth];
    loop->m0_count = *p++ - 1;
    loop->m4_start = p;
    loop->m2_octave = ch->m66_octave;
    return p;
}

/* 0x99 (Noah: seqOP_19_loopEnd). */
u8* seqOP_19_loopEnd(u8* p, SoundInstance* inst, SoundTrack* ch) {
    SoundLoop* loop = &ch->m9C_loops[ch->m72_loopDepth];

    if (--loop->m0_count != 0xFF) {
        loop->m8_end = p;
        p = loop->m4_start;
        loop->m3_octave = ch->m66_octave;
        ch->m66_octave = loop->m2_octave;
    } else {
        ch->m72_loopDepth--;
    }
    return p;
}

/* 0x9A (Noah: seqOP_1A_loopBreak). */
u8* seqOP_1A_loopBreak(u8* p, SoundInstance* inst, SoundTrack* ch) {
    SoundLoop* loop = &ch->m9C_loops[ch->m72_loopDepth];

    if (loop->m0_count == 0) {
        p = loop->m8_end;
        ch->m66_octave = loop->m3_octave;
        ch->m72_loopDepth--;
    }
    return p;
}

/* 0x9C (Noah: seqOP_1C_playSoundEffect). */
u8* seqOP_1C_playSoundEffect(u8* p, SoundInstance* inst, SoundTrack* ch) {
    playSoundEffectInFreeSlotVolPan(p[0] | (p[1] << 8), 0x7F, 0x40);
    return p + 3;
}

/* 0x9D (Noah: seqOP_1D_stopSoundEffect). */
u8* seqOP_1D_stopSoundEffect(u8* p, SoundInstance* inst, SoundTrack* ch) {
    stopSoundEffectById(p[0] | (p[1] << 8));
    return p + 2;
}

/* 0x9E (Noah: seqOP_1E_jumpToTrack). */
u8* seqOP_1E_jumpToTrack(u8* p, SoundInstance* inst, SoundTrack* ch) {
    SeqFile* seq = pLoadedSequencesLinkedList;
    s16 track = p[0] | (p[1] << 8);
    u8 sub = p[2];

    if (ch->m8.s.mA_seqId != 0) {
        while (seq->u14.m14_id != ch->m8.s.mA_seqId) {
            seq = SEQ_NEXT(seq);
            if (seq == NULL) {
                return p;
            }
        }
    }
    p = (u8*)seq + (&seq->m20_drumKitOffset)[sub + track * 2];
    return p + 3;
}

/* 0xA0 (Noah: seqOP_20_setTempo). The post-increment is what gives retail's
 * second zero-extension of the byte. */
u8* seqOP_20_setTempo(u8* p, SoundInstance* inst, SoundTrack* ch) {
    u8 t = *p++;

    inst->m58.m58_tempo = t << 16;
    inst->m54_tickRate = t * (s16)(inst->m64_tempo.m0_value >> 16);
    return p;
}

/* 0xA1: adjust tempo. */
u8* seqOP_21_adjustTempo(u8* p, SoundInstance* inst, SoundTrack* ch) {
    s32 unused[2];

    inst->m58.m58_tempo += (s8)p[0] << 16;
    inst->m54_tickRate = 0;
    return p + 1;
}

/* 0xA2 (Noah: seqOP_22_tempoSlide). */
u8* seqOP_22_tempoSlide(u8* p, SoundInstance* inst, SoundTrack* ch) {
    s16 len = p[0];
    s32 target = p[1];
    s32 delta;

    inst->m62_tempoTarget = target;
    delta = (target << 16) - inst->m58.m58_tempo;
    if (len != 0 && delta != 0) {
        inst->m60_tempoSlide = len;
        inst->m5C_tempoDelta = delta / len;
    }
    return p + 2;
}

/* 0xA6 (Noah: seqOP_26_setMasterVolume). */
u8* seqOP_26_setMasterVolume(u8* p, SoundInstance* inst, SoundTrack* ch) {
    inst->m70_volume.m0_value = *p++ << 24;
    applyFlagToAllVoices(0x100, inst);
    return p;
}

/* 0xA7: master volume slide. */
u8* seqOP_27_masterVolumeSlide(u8* p, SoundInstance* inst, SoundTrack* ch) {
    u8 len = p[0];
    u8 target = p[1];
    s16 count = len << 5;
    s32 delta = (target << 24) - inst->m70_volume.m0_value;

    if (count != 0 && delta != 0) {
        inst->m70_volume.m8_counter = count;
        inst->m70_volume.mA_target = target << 8;
        inst->m70_volume.m4_step = delta / count;
    }
    return p + 2;
}

/* 0xA9 (Noah: seqOP_29_setTranspose). */
u8* seqOP_29_setTranspose(u8* p, SoundInstance* inst, SoundTrack* ch) {
    ch->m62_gateRatio = p[0];
    return p + 1;
}

/* 0xAA (Noah: seqOP_2A_changeVoiceChannel). */
u8* seqOP_2A_changeVoiceChannel(u8* p, SoundInstance* inst, SoundTrack* ch) {
    s32 voice = *p++;

    if (voice < 25) {
        releaseSpuVoice(&ch->m30_voice, ch->m27_voice);
        claimSpuVoice(&ch->m30_voice, ch->m27_voice = voice);
    }
    return p;
}

/* 0xAC (Noah: seqOP_2C_setAdsr). */
u8* seqOP_2C_setAdsr(u8* p, SoundInstance* inst, SoundTrack* ch) {
    setupAdsr(*p++, ch);
    return p;
}

/* 0xAD (Noah: seqOP_2D_adjustNoteLength). */
u8* seqOP_2D_adjustNoteLength(u8* p, SoundInstance* inst, SoundTrack* ch) {
    u8 v = *p++;

    if (v != 0) {
        ch->m60_lengthAdjust += v;
    } else {
        ch->m60_lengthAdjust = 0;
    }
    return p;
}

/* 0xAE (Noah: seqOP_2E_percussionOn). */
u8* seqOP_2E_percussionOn(u8* p, SoundInstance* inst, SoundTrack* ch) {
    if (inst->mC_percussion != NULL) {
        ch->m0_flags |= 0x10;
    }
    return p;
}

/* 0xAF (Noah: seqOP_2F_percussionOff). */
u8* seqOP_2F_percussionOff(u8* p, SoundInstance* inst, SoundTrack* ch) {
    ch->m0_flags &= ~0x10;
    return p;
}

/* 0xB0 (Noah: seqOP_30_legatoOn). */
u8* seqOP_30_legatoOn(u8* p, SoundInstance* inst, SoundTrack* ch) {
    ch->m0_flags |= 0x800;
    return p;
}

/* 0xB1 (Noah: seqOP_31_legatoOff). */
u8* seqOP_31_legatoOff(u8* p, SoundInstance* inst, SoundTrack* ch) {
    ch->m0_flags &= ~0x800;
    return p;
}

/* 0xB2: pitch modulation on (Noah calls it seqOP_32_noiseOn, but voice flag
 * 0x10 drives the SPU pitch-modulation register, see sendAdsrToSpu). */
u8* seqOP_32_pitchModOn(u8* p, SoundInstance* inst, SoundTrack* ch) {
    if (ch->m27_voice & 1) {
        ch->m30_voice.m6_dirty |= 0x1000;
        ch->m30_voice.m2_flags |= 0x10;
    }
    return p;
}

/* 0xB3: pitch modulation off. */
u8* seqOP_33_pitchModOff(u8* p, SoundInstance* inst, SoundTrack* ch) {
    if (ch->m27_voice & 1) {
        ch->m30_voice.m6_dirty |= 0x1000;
        ch->m30_voice.m2_flags &= ~0x10;
    }
    return p;
}

/* 0xB4 (Noah: seqOP_34_setNoiseClock). */
u8* seqOP_34_setNoiseClock(u8* p, SoundInstance* inst, SoundTrack* ch) {
    inst->m1C_noiseClock = *p++;
    SpuSetNoiseClock(inst->m1C_noiseClock);
    ch->m30_voice.m6_dirty |= 0x2000;
    ch->m30_voice.m2_flags |= 0x20;
    return p;
}

/* 0xB5 (Noah: seqOP_35_adjustNoiseClock). */
u8* seqOP_35_adjustNoiseClock(u8* p, SoundInstance* inst, SoundTrack* ch) {
    inst->m1C_noiseClock = (*p++ + inst->m1C_noiseClock) & 0x3F;
    SpuSetNoiseClock(inst->m1C_noiseClock);
    ch->m30_voice.m6_dirty |= 0x2000;
    ch->m30_voice.m2_flags |= 0x20;
    return p;
}

/* 0xB6: noise on (voice flag 0x20 drives the SPU noise register). */
u8* seqOP_36_noiseModeOn(u8* p, SoundInstance* inst, SoundTrack* ch) {
    ch->m30_voice.m6_dirty |= 0x2000;
    ch->m30_voice.m2_flags |= 0x20;
    return p;
}

/* 0xB7: noise off. */
u8* seqOP_37_noiseModeOff(u8* p, SoundInstance* inst, SoundTrack* ch) {
    ch->m30_voice.m6_dirty |= 0x2000;
    ch->m30_voice.m2_flags &= ~0x20;
    return p;
}

/* 0xB8 (Noah: seqOP_38_setupReverb). */
u8* seqOP_38_setupReverb(u8* p, SoundInstance* inst, SoundTrack* ch) {
    u8 depth = p[0];
    s32 delay;
    s32 feedback;

    inst->m44_reverbDepth = depth << 8;
    inst->m42_reverbDelay = delay = (s8)p[1];
    inst->m43_reverbFeedback = feedback = (s8)p[2];
    setupReverb(-1, (s16)(depth << 8), delay, feedback);
    return p + 3;
}

/* 0xBA (Noah: seqOP_3A_reverbOn). */
u8* seqOP_3A_reverbOn(u8* p, SoundInstance* inst, SoundTrack* ch) {
    if (!(inst->m10_flags & 6) || ((musicStatusFlag & 0x2000) && !(ch->m0_flags & 2))) {
        ch->m30_voice.m6_dirty |= 0x4000;
        ch->m30_voice.m2_flags |= 0x40;
        return p;
    }
    return p;
}

/* 0xBB: reverb off. */
u8* seqOP_3B_reverbOff(u8* p, SoundInstance* inst, SoundTrack* ch) {
    ch->m30_voice.m6_dirty |= 0x4000;
    ch->m30_voice.m2_flags &= ~0x40;
    return p;
}

/* 0xBC */
u8* seqOP_3C_skip3(u8* p, SoundInstance* inst, SoundTrack* ch) {
    return p + 3;
}

/* 0xBD */
u8* seqOP_3D_nop(u8* p, SoundInstance* inst, SoundTrack* ch) {
    return p;
}

/* 0xBE */
u8* seqOP_3E_nop(u8* p, SoundInstance* inst, SoundTrack* ch) {
    return p;
}

/* 0xC0 (Noah: seqOP_40_ADSRReset). */
u8* seqOP_40_ADSRReset(u8* p, SoundInstance* inst, SoundTrack* ch) {
    setupAdsr(ch->m26_instrument, ch);
    return p;
}

/* 0xC1: set the three ADSR modes. */
u8* seqOP_41_setAdsrModes(u8* p, SoundInstance* inst, SoundTrack* ch) {
    ch->m30_voice.m24_attackMode = p[0];
    ch->m30_voice.m25_sustainMode = p[1];
    ch->m30_voice.m26_releaseMode = p[2];
    ch->m30_voice.m6_dirty |= 0x1F0;
    return p + 3;
}

/* 0xC2 (Noah: seqOP_42_setAttack). */
u8* seqOP_42_setAttack(u8* p, SoundInstance* inst, SoundTrack* ch) {
    ch->m30_voice.m27_attack = *p;
    ch->m30_voice.m6_dirty |= 0x10;
    return p + 1;
}

/* 0xC3: set decay. */
u8* seqOP_43_setDecay(u8* p, SoundInstance* inst, SoundTrack* ch) {
    ch->m30_voice.m28_decay = *p;
    ch->m30_voice.m6_dirty |= 0x20;
    return p + 1;
}

/* 0xC4 (Noah: seqOP_44_setSustain). */
u8* seqOP_44_setSustain(u8* p, SoundInstance* inst, SoundTrack* ch) {
    ch->m30_voice.m29_sustainRate = *p;
    ch->m30_voice.m6_dirty |= 0x40;
    return p + 1;
}

/* 0xC5 (Noah: seqOP_45_setRelease). */
u8* seqOP_45_setRelease(u8* p, SoundInstance* inst, SoundTrack* ch) {
    u8 v = *p;

    ch->m30_voice.m6_dirty |= 0x80;
    ch->m28_releaseBackup = v;
    ch->m30_voice.m2A_release = v;
    return p + 1;
}

/* 0xC6: set sustain rate. */
u8* seqOP_46_setSustainLevel(u8* p, SoundInstance* inst, SoundTrack* ch) {
    ch->m30_voice.m2B_sustainLevel = *p;
    ch->m30_voice.m6_dirty |= 0x100;
    return p + 1;
}

/* 0xC7: set decay and sustain rate. */
u8* seqOP_47_setDecayAndSustainLevel(u8* p, SoundInstance* inst, SoundTrack* ch) {
    ch->m30_voice.m28_decay = p[0];
    ch->m30_voice.m2B_sustainLevel = p[1];
    ch->m30_voice.m6_dirty |= 0x120;
    return p + 2;
}

/* 0xC8: set attack mode. */
u8* seqOP_48_setAttackMode(u8* p, SoundInstance* inst, SoundTrack* ch) {
    ch->m30_voice.m24_attackMode = *p;
    ch->m30_voice.m6_dirty |= 0x10;
    return p + 1;
}

/* 0xC9 (Noah: seqOP_49_setSustainMode). */
u8* seqOP_49_setSustainMode(u8* p, SoundInstance* inst, SoundTrack* ch) {
    ch->m30_voice.m25_sustainMode = *p;
    ch->m30_voice.m6_dirty |= 0x40;
    return p + 1;
}

/* 0xCA (Noah: seqOP_4A_setReleaseMode). */
u8* seqOP_4A_setReleaseMode(u8* p, SoundInstance* inst, SoundTrack* ch) {
    ch->m30_voice.m26_releaseMode = *p;
    ch->m30_voice.m6_dirty |= 0x80;
    return p + 1;
}

/* 0xD0 (Noah: seqOP_50_setPitchBend). */
u8* seqOP_50_setPitchBend(u8* p, SoundInstance* inst, SoundTrack* ch) {
    ch->m6E_pitchBend = (p[0] << 24) >> 19;
    ch->m2_update |= 0x200;
    return p + 1;
}

/* 0xD1 (Noah: seqOP_51_adjustPitchBend). */
u8* seqOP_51_adjustPitchBend(u8* p, SoundInstance* inst, SoundTrack* ch) {
    ch->m6E_pitchBend += (p[0] << 24) >> 19;
    ch->m2_update |= 0x200;
    return p + 1;
}

/* 0xD2: fine pitch bend adjust. */
u8* seqOP_52_adjustPitchBendFine(u8* p, SoundInstance* inst, SoundTrack* ch) {
    ch->m6E_pitchBend += (p[0] << 24) >> 21;
    ch->m2_update |= 0x200;
    return p + 1;
}

/* 0xD3 */
u8* seqOP_53_adjustPitchBend16(u8* p, SoundInstance* inst, SoundTrack* ch) {
    ch->m6E_pitchBend += p[1] + ((p[0] << 24) >> 16);
    ch->m2_update |= 0x200;
    return p + 2;
}

/* 0xD4 (Noah: seqOP_54_pitchSlide). */
u8* seqOP_54_pitchSlide(u8* p, SoundInstance* inst, SoundTrack* ch) {
    u16 len = p[0];
    s32 target = (s8)p[1] << 24;

    if (len != 0 && target != 0) {
        ch->m94_pitchSlide = len;
        ch->m4_flags |= 1;
        ch->m84_pitchDelta = target / len;
    } else {
        ch->m4_flags &= ~1;
    }
    return p + 2;
}

/* 0xD5 */
u8* seqOP_55_toggleEndlessPitchSlide(u8* p, SoundInstance* inst, SoundTrack* ch) {
    ch->m4_flags ^= 2;
    return p;
}

/* 0xDC */
u8* seqOP_5C_pitchSlideOff(u8* p, SoundInstance* inst, SoundTrack* ch) {
    ch->m4_flags &= ~1;
    return p;
}

/* 0xD6: portamento. */
u8* seqOP_56_portamento(u8* p, SoundInstance* inst, SoundTrack* ch) {
    u8 v = *p++;

    ch->m70_portamento = v;
    if (v != 0) {
        ch->m4_flags |= 4;
    } else {
        ch->m4_flags &= ~4;
    }
    return p;
}


u8* seqOP_58_pitchLFO(u8* p, SoundInstance* inst, SoundTrack* ch) {
    s32 depth = (s8)p[1];
    s16 rate = p[0];

    if (depth != 0 && rate != 0) {
        if (depth < 0) {
            depth = depth * -depth;
        } else {
            depth = depth * depth;
        }
        rate += rate * rate / 64;
        ch->mD8_lfo[0].mC_depth = scaleLfoDepth(depth << 14, rate, 3);
        ch->mD8_lfo[0].m12_period = rate;
        ch->mD8_lfo[0].m16_delayReload = p[2] << 2;
        ch->mD8_lfo[0].m1A_fadeStep = 0x400;
        ch->mD8_lfo[0].m0_func = stepLfoTriangleBipolar;
        ch->mD8_lfo[0].m1D_waveform = 3;
        ch->mD8_lfo[0].m1E_flags = 3;
        ch->mD8_lfo[0].m1C_target = 0;
        ch->mCE_lfoActive |= 1;
        resetLfoState(&ch->mD8_lfo[0]);
    }
    return p + 3;
}

u8* seqOP_59_pitchLFOWaveform(u8* p, SoundInstance* inst, SoundTrack* ch) {
    s16 rate = p[0];
    s32 depth = (s8)p[1];
    u16 flags = p[2];
    s32 mode;
    SoundLfo* lfo;

    if (depth != 0 && rate != 0) {
        if (depth < 0) {
            depth = depth * -depth;
        } else {
            depth = depth * depth;
        }
        depth <<= 14;
        rate += rate * rate / 64;
        mode = ((flags & 0x10) == 0) * 2;
        flags &= 0xF;
        lfo = &ch->mD8_lfo[0];
        lfo->mC_depth = scaleLfoDepth(depth, rate, flags & 0xF);
        lfo->m1A_fadeStep = 0x400;
        lfo->m12_period = rate;
        lfo->m16_delayReload = 0;
        lfo->m0_func = lfoWaveformFuncs[flags];
        lfo->m1D_waveform = flags;
        lfo->m1C_target = 0;
        lfo->m1E_flags = mode + 1;
        ch->mCE_lfoActive |= 1;
        resetLfoState(lfo);
    }
    return p + 3;
}

u8* seqOP_57_pitchLFOSpeed(u8* p, SoundInstance* inst, SoundTrack* ch) {
    s32 speed = (*p++ + 1) & 0xFF;

    if (speed != 0) {
        ch->mD8_lfo[0].m18_fade = ch->mD8_lfo[0].m1A_fadeStep = 0x400 / (speed << 2);
    }
    return p;
}

u8* seqOP_5A_pitchLFOOn(u8* p, SoundInstance* inst, SoundTrack* ch) {
    ch->mCE_lfoActive |= 1;
    ch->mD8_lfo[0].m1E_flags |= 1;
    return p;
}

u8* seqOP_5B_pitchLFOOff(u8* p, SoundInstance* inst, SoundTrack* ch) {
    ch->mCE_lfoActive &= ~1;
    ch->mD8_lfo[0].m1E_flags &= ~1;
    return p;
}

u8* seqOP_60_setVolume(u8* p, SoundInstance* inst, SoundTrack* ch) {
    ch->m78_volume = p[0] << 24;
    ch->m2_update |= 0x100;
    ch->m4_flags &= 0xFEF7;
    return p + 1;
}

u8* seqOP_61_adjustVolume(u8* p, SoundInstance* inst, SoundTrack* ch) {
    ch->m78_volume = ((s8)p[0] << 24) + ch->m78_volume & 0x7FFFFFFF;
    ch->m2_update |= 0x100;
    ch->m4_flags &= 0xFEF7;
    return p + 1;
}

u8* seqOP_62_volumeSlide(u8* p, SoundInstance* inst, SoundTrack* ch) {
    u16 length = p[0];
    s32 diff = ((s8)p[1] << 24) - ch->m78_volume;

    if (length != 0 && diff != 0) {
        ch->m96_volumeSlide = length;
        ch->m4_flags = (ch->m4_flags | 8) & 0xFEFF;
        ch->m88_volumeDelta = diff / length;
    }
    return p + 2;
}

u8* seqOP_78_volumeSlideMulti(u8* p, SoundInstance* inst, SoundTrack* ch) {
    s32 start = p[0] << 24;
    s32 diff = (p[2] << 24) - start;
    s16 length = p[1];

    if (diff != 0 && length != 0) {
        ch->m82_noteVolumeStart = start >> 16;
        ch->m80_noteVolumeSlideLength = length;
        ch->m4_flags = (ch->m4_flags | 0x100) & 0xFFF7;
        ch->m7C_noteVolumeDelta = diff / length;
    } else {
        ch->m4_flags &= 0xFEFF;
    }
    return p + 3;
}

u8* seqOP_64_volumeLFO(u8* p, SoundInstance* inst, SoundTrack* ch) {
    s32 depth = (s8)p[1];
    s16 rate = p[0];

    if (depth != 0 && rate != 0) {
        rate += rate * rate / 64;
        ch->mD8_lfo[1].mC_depth = scaleLfoDepth(depth << 24, rate, 2);
        ch->mD8_lfo[1].m12_period = rate;
        ch->mD8_lfo[1].m16_delayReload = p[2] << 2;
        ch->mD8_lfo[1].m1A_fadeStep = 0x400;
        ch->mD8_lfo[1].m0_func = stepLfoTriangle;
        ch->mD8_lfo[1].m1D_waveform = 2;
        ch->mD8_lfo[1].m1C_target = 1;
        ch->mD8_lfo[1].m1E_flags = 3;
        ch->mCE_lfoActive |= 2;
        resetLfoState(&ch->mD8_lfo[1]);
    }
    return p + 3;
}

u8* seqOP_65_volumeLFOWaveform(u8* p, SoundInstance* inst, SoundTrack* ch) {
    s16 rate = p[0];
    s32 depth = (s8)p[1];
    u16 flags = p[2];
    s32 mode;
    SoundLfo* lfo;

    if (depth != 0 && rate != 0) {
        depth <<= 24;
        do {
            rate += rate * rate / 64;
        } while (0);
        mode = ((flags & 0x10) == 0) * 2;
        flags &= 0xF;
        lfo = &ch->mD8_lfo[1];
        lfo->mC_depth = scaleLfoDepth(depth, rate, flags & 0xF);
        lfo->m1A_fadeStep = 0x400;
        lfo->m12_period = rate;
        lfo->m16_delayReload = 0;
        lfo->m0_func = lfoWaveformFuncs[flags];
        lfo->m1D_waveform = flags;
        lfo->m1C_target = 1;
        lfo->m1E_flags = mode + 1;
        ch->mCE_lfoActive |= 2;
        resetLfoState(lfo);
    }
    return p + 3;
}

u8* seqOP_63_volumeLFOSpeed(u8* p, SoundInstance* inst, SoundTrack* ch) {
    s32 speed = (*p++ + 1) & 0xFF;

    if (speed != 0) {
        ch->mD8_lfo[1].m18_fade = ch->mD8_lfo[1].m1A_fadeStep = 0x400 / (speed << 2);
    }
    return p;
}

u8* seqOP_66_volumeLFOOn(u8* p, SoundInstance* inst, SoundTrack* ch) {
    ch->mCE_lfoActive |= 2;
    ch->mD8_lfo[1].m1E_flags |= 1;
    return p;
}

u8* seqOP_67_volumeLFOOff(u8* p, SoundInstance* inst, SoundTrack* ch) {
    ch->mCE_lfoActive &= ~2;
    ch->mD8_lfo[1].m1E_flags &= ~1;
    return p;
}

u8* seqOP_68_setPan(u8* p, SoundInstance* inst, SoundTrack* ch) {
    ch->m74_pan = p[0] << 8;
    ch->m2_update |= 0x100;
    return p + 1;
}

u8* seqOP_69_adjustPan(u8* p, SoundInstance* inst, SoundTrack* ch) {
    ch->m74_pan = (ch->m74_pan + ((s8)p[0] << 8)) & 0x7FFF;
    ch->m2_update |= 0x100;
    return p + 1;
}

u8* seqOP_6A_panSlide(u8* p, SoundInstance* inst, SoundTrack* ch) {
    u16 length = p[0];
    s32 diff = (s8)p[1] - ((s16)ch->m74_pan >> 8);
    if (length != 0 && diff != 0) {
        ch->m92_panTarget = diff * 256;
        ch->m98_panSlide = length;
        ch->m4_flags |= 0x10;
        ch->m90_panStep = (diff * 256) / length;
    }
    return p + 2;
}

u8* seqOP_6B_panLFOSpeed(u8* p, SoundInstance* inst, SoundTrack* ch) {
    s32 speed = (*p++ + 1) & 0xFF;

    if (speed != 0) {
        ch->mD8_lfo[2].m18_fade = ch->mD8_lfo[2].m1A_fadeStep = 0x400 / (speed << 2);
    }
    return p;
}

u8* seqOP_6C_panLFO(u8* p, SoundInstance* inst, SoundTrack* ch) {
    s32 depth = (s8)p[1];
    s16 rate = p[0];

    if (depth != 0 && rate != 0) {
        rate += rate * rate / 64;
        ch->mD8_lfo[2].mC_depth = scaleLfoDepth(depth << 24, rate, 3);
        ch->mD8_lfo[2].m12_period = rate;
        ch->mD8_lfo[2].m16_delayReload = p[2] << 2;
        ch->mD8_lfo[2].m1A_fadeStep = 0x400;
        ch->mD8_lfo[2].m0_func = stepLfoTriangleBipolar;
        ch->mD8_lfo[2].m1D_waveform = 3;
        ch->mD8_lfo[2].m1C_target = 2;
        ch->mD8_lfo[2].m1E_flags = 3;
        ch->mCE_lfoActive |= 4;
        resetLfoState(&ch->mD8_lfo[2]);
    }
    return p + 3;
}

u8* seqOP_6D_panLFOWaveform(u8* p, SoundInstance* inst, SoundTrack* ch) {
    s16 rate = p[0];
    s32 depth = (s8)p[1];
    u16 flags = p[2];
    s32 mode;
    SoundLfo* lfo;

    if (depth != 0 && rate != 0) {
        depth <<= 24;
        do {
            rate += rate * rate / 64;
        } while (0);
        mode = ((flags & 0x10) == 0) * 2;
        flags &= 0xF;
        lfo = &ch->mD8_lfo[2];
        lfo->mC_depth = scaleLfoDepth(depth, rate, flags & 0xF);
        lfo->m1A_fadeStep = 0x400;
        lfo->m12_period = rate;
        lfo->m16_delayReload = 0;
        lfo->m0_func = lfoWaveformFuncs[flags];
        lfo->m1D_waveform = flags;
        lfo->m1C_target = 2;
        lfo->m1E_flags = mode + 1;
        ch->mCE_lfoActive |= 4;
        resetLfoState(lfo);
    }
    return p + 3;
}

u8* seqOP_6E_panLFOOn(u8* p, SoundInstance* inst, SoundTrack* ch) {
    ch->mCE_lfoActive |= 4;
    ch->mD8_lfo[2].m1E_flags |= 1;
    return p;
}

u8* seqOP_6F_panLFOOff(u8* p, SoundInstance* inst, SoundTrack* ch) {
    ch->mCE_lfoActive &= ~4;
    ch->mD8_lfo[2].m1E_flags &= ~1;
    return p;
}

u8* seqOP_70_LFOSelect(u8* p, SoundInstance* inst, SoundTrack* ch) {
    SoundLfo* lfo;
    u8 b;

    ch->mCC_lfoIndex = p[0];
    lfo = &ch->mD8_lfo[ch->mCC_lfoIndex];
    b = p[1];
    lfo->m1D_waveform = b & 0xF;
    lfo->m0_func = lfoWaveformFuncs[lfo->m1D_waveform];
    if (!(b & 0x10)) {
        lfo->m1E_flags = 2;
    } else {
        lfo->m1E_flags = 0;
    }
    lfo->m1C_target = p[2];
    lfo->m1A_fadeStep = 0x400;
    lfo->m16_delayReload = 0;
    return p + 3;
}

/* The u16 copies matter: p[0] is reloaded after the division (the first
 * load goes through a u16), and the period survives the call in its own
 * register while the int rate is passed unextended. */
u8* seqOP_71_LFOUpdate(u8* p, SoundInstance* inst, SoundTrack* ch) {
    SoundLfo* lfo = &ch->mD8_lfo[ch->mCC_lfoIndex];
    u16 x = p[0];
    s32 q;
    s32 rate;
    u16 period;

    q = x * x / 64;
    rate = p[0] + q;
    period = rate;
    lfo->mC_depth = scaleLfoDepth(((s8)p[1] << 24) | (p[2] << 16), rate, lfo->m1D_waveform);
    lfo->m12_period = period;
    return p + 3;
}

/* Scale an LFO depth by its rate for the given waveform. Callers above use
 * it without a prototype (seqOP_71_LFOUpdate passes an unextended int). */
s32 scaleLfoDepth(s32 depth, s16 rate, s16 type) {
    if (depth != 0 && rate != 0) {
        switch (type) {
        case 2:
        case 3:
            depth /= rate;
            break;
        case 4:
            if (rate != 1) {
                depth /= rate - 1;
            }
            break;
        }
    }
    return depth;
}

u8* seqOP_72_LFODelayAndFade(u8* p, SoundInstance* inst, SoundTrack* ch) {
    SoundLfo* lfo = &ch->mD8_lfo[ch->mCC_lfoIndex];
    u8 speed = p[1] + 1;
    if (speed != 0) {
        u16 step = 0x400 / (speed << 2);
        lfo->m16_delayReload = p[0] << 2;
        lfo->m1A_fadeStep = step;
        lfo->m18_fade = step;
    }
    return p + 2;
}

u8* seqOP_75_nop(u8* p, SoundInstance* inst, SoundTrack* ch) {
    return p;
}

u8* seqOP_76_LFORestart(u8* p, SoundInstance* inst, SoundTrack* ch) {
    s32 i = *p++;
    SoundLfo* lfo = &ch->mD8_lfo[i];

    resetLfoState(lfo);
    lfo->m1E_flags |= 1;
    ch->mCE_lfoActive |= 1 << i;
    return p;
}

void resetLfoState(SoundLfo* lfo) {
    lfo->m10_counter = 1;
    lfo->m4_value = 0;
    lfo->m1E_flags &= 0xFFF3;
    lfo->m14_delay = lfo->m16_delayReload;
    lfo->m18_fade = lfo->m1A_fadeStep;
}

u8* seqOP_77_LFOOff(u8* p, SoundInstance* inst, SoundTrack* ch) {
    u8 i = p[0];

    ch->mD8_lfo[i].m1E_flags &= ~1;
    ch->mCE_lfoActive &= ~(1 << i);
    return p + 1;
}

u8* seqOP_7C_changeWdsAndADSR(u8* p, SoundInstance* inst, SoundTrack* ch) {
    Wds* wds;
    u8 bank = p[0];
    u8 adsr = p[1];

    ch->m25_bank = bank;
    wds = findInWdsLinkedList(bank);
    if (wds == NULL) {
        wds = pLoadedWdsLinkedList;
    }
    ch->m2C_wds = wds;
    setupAdsr(adsr, ch);
    return p + 2;
}

u8* seqOP_7D_setTempoScale(u8* p, SoundInstance* inst, SoundTrack* ch) {
    s32 t = *p++;

    if (t != 0) {
        inst->m64_tempo.m0_value = t << 24;
        inst->m54_tickRate = (s16)(inst->m58.m58_tempo >> 16) * (t << 8);
    }
    return p;
}

u8* seqOP_7E_changeBank(u8* p, SoundInstance* inst, SoundTrack* ch) {
    Wds* wds;
    u8 bank = *p++;

    ch->m25_bank = bank;
    wds = findInWdsLinkedList(bank);
    if (wds == NULL) {
        wds = pLoadedWdsLinkedList;
    }
    ch->m2C_wds = wds;
    return p;
}

u8* seqOP_7F_waitRelease(u8* p, SoundInstance* inst, SoundTrack* ch) {
    s32 keyStatus;
    s16 envelope;

    SpuGetVoiceEnvelopeAttr(ch->m27_voice, &keyStatus, &envelope);
    if (envelope == 0) {
        ch->m2_update &= ~3;
        releaseSpuVoice(&ch->m30_voice, ch->m27_voice);
        ch->m0_flags = 0;
    }
    return p;
}

void setupAdsr(s16 index, SoundTrack* ch) {
    WdsInstrument* ins;
    Wds* w;
    u32 offset;
    u32 v;

    ch->m26_instrument = index;
    w = ch->m2C_wds;
    ins = &w->m30_instruments[index];
    offset = ins->m0_start << 3;
    ch->m30_voice.m1C_startAddress = offset + ch->m2C_wds->m28_adpcmAddress;
    ch->m30_voice.m20_repeatAddress = offset + (ins->m4_loopOffset << 3);
    v = ins->mC_adsrModes;
    ch->m30_voice.m24_attackMode = v & 7;
    ch->m30_voice.m25_sustainMode = (v >> 4) & 7;
    ch->m30_voice.m26_releaseMode = (v >> 8) & 7;
    v = ins->m8_adsr;
    ch->m30_voice.m27_attack = v & 0x7F;
    ch->m30_voice.m28_decay = (v >> 8) & 0xF;
    ch->m30_voice.m29_sustainRate = (v >> 16) & 0x7F;
    ch->m30_voice.m2B_sustainLevel = (v >> 12) & 0xF;
    ch->m30_voice.m2A_release = ch->m28_releaseBackup = (v >> 24) & 0x1F;
    ch->m6C_transpose = ins->m6_transpose;
    ch->m0_flags |= 0x8000;
}

void applyFlagToAllVoices(u16 flag, SoundInstance* inst) {
    SoundTrack* ch = inst->m94_tracks;
    s32 n = inst->m14_trackCount;

    do {
        if (ch->m0_flags != 0) {
            ch->m2_update |= flag;
        }
        ch++;
    } while (--n != 0);
}

void playBattleMusicSub1Sub0(SoundInstance* inst, s32 flag) {
    SoundTrack* ch = inst->m94_tracks;
    s32 n = inst->m14_trackCount;

    do {
        if (ch->m0_flags != 0) {
            ch->m30_voice.m6_dirty |= flag;
        }
        ch++;
    } while (--n != 0);
}

void clearSpuVoiceOwners(void) {
    s32 i;

    for (i = 23; i >= 0; i--) {
        spuVoiceOwners[i] = NULL;
    }
}

void claimSpuVoice(SoundVoice* voice, u32 index) {
    SoundVoice** owner = &spuVoiceOwners[index];

    if (index < 24) {
        if (*owner == voice) {
            pendingKeyOff |= 1 << index;
        } else if (*owner == NULL || (*owner)->m4_priority <= voice->m4_priority) {
            voice->m6_dirty = 0xFFFF;
            voice->m0_voice = index;
            spuVoiceOwners[index] = voice;
            pendingKeyOff |= 1 << index;
            pendingKeyOn &= ~(1 << index);
        }
    }
}

void assignSpuVoiceOwner(SoundVoice* voice, u32 index) {
    SoundVoice** owner = &spuVoiceOwners[index];

    if (index < 24) {
        if (*owner != voice) {
            if (*owner == NULL || (*owner)->m4_priority <= voice->m4_priority) {
                voice->m0_voice = index;
                *owner = voice;
            }
        }
    }
}

void releaseSpuVoice(SoundVoice* voice, u32 index) {
    SoundVoice** owner = &spuVoiceOwners[index];

    if (index < 24 && *owner == voice) {
        *owner = NULL;
        pendingKeyOff |= 1 << index;
        pendingKeyOn &= ~(1 << index);
    }
}

void keyOffSpuVoice(SoundVoice* voice, u32 index) {
    if (index < 24 && spuVoiceOwners[index] == voice) {
        pendingKeyOff |= 1 << index;
        pendingKeyOn &= ~(1 << index);
    }
}

/* Flush the dirty voice shadows to the SPU; the voice pointer is reused
 * for the global key-on / FM / noise / reverb registers after the loop. */
void sendAdsrToSpu(void) {
    SoundVoice** owner = spuVoiceOwners;
    u32 reverb = 0;
    u32 noise = 0;
    u32 fm = 0;
    u16 update = 0;
    SpuVoiceRegs* regs = (SpuVoiceRegs*)spuRegisters;
    s32 i;
    s32 r;
    u32 keyOn;

    for (i = 0; i < 24; regs++, i++, owner++) {
        SoundVoice* v = *owner;
        u16 dirty;

        if (v != NULL) {
            dirty = v->m6_dirty;
            if (dirty != 0) {
                if (dirty & 1) {
                    regs->m0_volL = v->m8_volumeLeft;
                    regs->m2_volR = v->mA_volumeRight;
                }
                if (dirty & 4) {
                    regs->m4_pitch = v->m14_pitch;
                }
                if (dirty & 8) {
                    regs->m6_addr = v->m1C_startAddress >> 3;
                    regs->mE_loopAddr = v->m20_repeatAddress >> 3;
                }
                if (dirty & 0x10) {
                    regs->m8_adsr1 = (u8)regs->m8_adsr1 + (v->m27_attack << 8) + ((v->m24_attackMode >> 2) << 15);
                }
                if (dirty & 0x20) {
                    r = regs->m8_adsr1;
                    r = (r & 0xFF0F) + (v->m28_decay << 4);
                    regs->m8_adsr1 = r;
                }
                if (dirty & 0x40) {
                    r = regs->mA_adsr2;
                    regs->mA_adsr2 = (r & 0x3F) + (v->m29_sustainRate << 6) + ((v->m25_sustainMode >> 1) << 14);
                }
                if (dirty & 0x80) {
                    r = regs->mA_adsr2;
                    regs->mA_adsr2 = (r & 0xFFC0) + (v->m2A_release + ((v->m26_releaseMode >> 2) << 5));
                }
                if (dirty & 0x100) {
                    r = regs->m8_adsr1;
                    r = v->m2B_sustainLevel + (r & 0xFFF0);
                    regs->m8_adsr1 = r;
                }
                update |= dirty & 0x7000;
                v->m6_dirty = 0;
            }
            fm |= ((v->m2_flags >> 4) & 1) << i;
            noise |= ((v->m2_flags >> 5) & 1) << i;
            reverb |= ((v->m2_flags >> 6) & 1) << i;
        }
    }
    regs = (SpuVoiceRegs*)spuRegisters;
    if (update != 0) {
        if (update & 0x1000) {
            ((SpuRegs*)regs)->m190_fm[0] = fm;
            ((SpuRegs*)regs)->m190_fm[1] = fm >> 16;
        }
        if (update & 0x2000) {
            ((SpuRegs*)regs)->m194_noise[0] = noise;
            ((SpuRegs*)regs)->m194_noise[1] = noise >> 16;
        }
        if (update & 0x4000) {
            ((SpuRegs*)regs)->m198_reverbOn[0] = reverb;
            ((SpuRegs*)regs)->m198_reverbOn[1] = reverb >> 16;
        }
    }
    keyOn = pendingKeyOn;
    if (keyOn != 0) {
        ((SpuRegs*)regs)->m188_keyOn[0] = keyOn;
        ((SpuRegs*)regs)->m188_keyOn[1] = keyOn >> 16;
        pendingKeyOn = 0;
    }
}

void audioTickSub1(void) {
    u32 keyOff = pendingKeyOff;
    SpuRegs* regs = spuRegisters;
    s32 i;
    SpuVoiceRegs* v;
    if (keyOff != 0) {
        v = regs->m0_voice;
        for (i = 0; i < 24; i++, v++) {
            if (keyOff & (1 << i)) {
                v->mA_adsr2 = (v->mA_adsr2 & 0xFFC0) | 6;
            }
        }
    }
    keyOff = pendingKeyOff2 | pendingKeyOff;
    if (keyOff != 0) {
        regs->m18C_keyOff[0] = keyOff;
        regs->m18C_keyOff[1] = keyOff >> 16;
        pendingKeyOff = 0;
        pendingKeyOff2 = 0;
    }
}

void voicePostUpdate2(SoundInstance* inst, SoundTrack* channels, s16 count) {
    SoundTrack* ch;
    u16 flags;
    s32 volume;
    s32 pan;
    s32 left;
    s32 right;
    s32 pitch;

    if (!(inst->m10_flags & 0x20)) {
        ch = channels;
        do {
            if (ch->m0_flags != 0) {
                flags = ch->m2_update;
                if (flags & 0x100) {
                    volume = (s16)(ch->m78_volume >> 16) - (((s16)(ch->m78_volume >> 16) * ch->mD2_volumeMod) >> 15);
                    if (volume > 0x7FFF) {
                        volume = 0x7FFF;
                    }
                    if (volume < 0) {
                        volume = 0;
                    }
                    pan = (s16)ch->m74_pan + ch->mD4_panMod + (s16)(inst->m88_pan.m0_value >> 16);
                    volume = (ch->m76_volume * volume) >> 15;
                    volume = ((s16)(inst->m70_volume.m0_value >> 16) * volume) >> 16;
                    if (pan > 0x7F00) {
                        pan = 0x7F00;
                    }
                    if (pan < 0) {
                        pan = 0;
                    }
                    if (musicStatusFlag & 0x100) {
                        if (pan < 0x4000) {
                            right = (pan * 0x5A00) >> 14;
                            left = 0x7F00 - ((pan * 0x2500) >> 14);
                        } else {
                            pan = 0x8000 - pan;
                            left = (pan * 0x5A00) >> 14;
                            right = 0x7F00 - ((pan * 0x2500) >> 14);
                        }
                        left = (left * volume) >> 15;
                        right = (right * volume) >> 15;
                    } else {
                        right = (volume * 0x5A00) >> 15;
                        left = right;
                    }
                    ch->m30_voice.m8_volumeLeft = left;
                    ch->m30_voice.mA_volumeRight = right;
                    ch->m30_voice.m6_dirty |= 1;
                }
                if (flags & 0x200) {
                    pitch = (s16)(ch->m68_pitch >> 16) + ch->mD0_pitchMod + (s16)(inst->m7C_pitch.m0_value >> 16);
                    ch->m30_voice.m14_pitch = computeSampleRate(pitch) & 0x3FFF;
                    ch->m30_voice.m6_dirty |= 4;
                }
                if ((flags & 1) && !(ch->m0_flags & 0x20)) {
                    voicePostUpdate2Sub0(&ch->m30_voice, ch->m27_voice);
                }
                if (flags & 2) {
                    voicePostUpdate2Sub1(&ch->m30_voice, ch->m27_voice);
                }
                ch->m2_update = 0;
            }
            ch++;
        } while (--count != 0);
    }
}

/* A pitch below 0 (an LFO pulling a low note down) indexes the octave table
 * past its 117 notes, up to 127: the PS1 reads on into 3 zero bytes of
 * padding and the first bytes of pitchTable2 (0x80050BF0), its next data. */
static s32 octaveSemitonePS1(s32 note) {
    if (note < 117) {
        return pitchToOctaveSemitone[note];
    }
    return note < 120 ? 0 : ((u8*)pitchTable2)[note - 120];
}

s16 computeSampleRate(s16 pitch) {
    s32 e = octaveSemitonePS1((pitch & 0x7FFF) >> 8);
    s32 shift = 6 - (e >> 4);
    s32 rate = pitchTable2[(pitch & 0xFF) + ((e & 0xF) << 8)];

    if (shift < 0) {
        rate <<= -shift;
    } else {
        rate >>= shift;
    }
    return rate;
}

void voicePostUpdate2Sub0(SoundVoice* voice, u32 index) {
    SoundVoice** owner = &spuVoiceOwners[index];

    if (index < 24) {
        if (*owner != voice) {
            if (*owner == NULL || (*owner)->m4_priority <= voice->m4_priority) {
                voice->m6_dirty = 0xFFFF;
                voice->m0_voice = index;
                spuVoiceOwners[index] = voice;
                pendingKeyOff |= 1 << index;
            } else {
                return;
            }
        }
        pendingKeyOn |= 1 << index;
    }
}

void voicePostUpdate2Sub1(SoundVoice* voice, u32 index) {
    if (index < 24 && spuVoiceOwners[index] == voice) {
        pendingKeyOff2 |= 1 << index;
    }
}

void voicePostUpdate1(SoundInstance* inst, SoundTrack* ch, s16 count) {
    SoundLfo* lfo;
    u16 flags;
    s32 n;
    s32 value;

    do {
        if (ch->m0_flags != 0) {
            ch->mD4_panMod = 0;
            ch->mD2_volumeMod = 0;
            ch->mD0_pitchMod = 0;
            if (ch->mCE_lfoActive != 0) {
                n = 4;
                lfo = ch->mD8_lfo;
                flags = ch->m2_update;
                for (; n != 0; lfo++, n--) {
                    if (lfo->m1E_flags & 1) {
                        if (lfo->m14_delay != 0) {
                            lfo->m14_delay--;
                        } else {
                            value = lfo->m0_func(lfo);
                            if (lfo->m18_fade < 0x400) {
                                value = (value >> 10) * lfo->m18_fade;
                                lfo->m18_fade += lfo->m1A_fadeStep;
                            }
                            value >>= 16;
                            switch (lfo->m1C_target) {
                            case 0:
                                ch->mD0_pitchMod += value;
                                flags |= 0x200;
                                break;
                            case 1:
                                ch->mD2_volumeMod += value;
                                flags |= 0x100;
                                break;
                            case 2:
                                ch->mD4_panMod += value;
                                flags |= 0x100;
                                break;
                            }
                        }
                    }
                }
                ch->m2_update = flags;
            }
        }
        ch++;
    } while (--count != 0);
}

s32 stepLfoDisabled(SoundLfo* lfo) {
    lfo->m1E_flags &= ~1;
    /* voicePostUpdate1 uses $v0, which holds the new flags (lhu, andi, sh) */
    FALLOFF_RETURN(lfo->m1E_flags);
}

s32 stepLfoSquare(SoundLfo* lfo) {
    if (--lfo->m10_counter == 0) {
        lfo->m10_counter = lfo->m12_period;
        lfo->m4_value = (lfo->m4_value == 0) ? lfo->mC_depth : 0;
    }
    return lfo->m4_value;
}

s32 stepLfoSquareBipolar(SoundLfo* lfo) {
    s32 depth;

    if (--lfo->m10_counter == 0) {
        depth = lfo->mC_depth;
        lfo->m10_counter = lfo->m12_period;
        if (lfo->m1E_flags & 8) {
            depth = -depth;
        }
        lfo->m4_value = depth;
        lfo->m1E_flags ^= 8;
    }
    return lfo->m4_value;
}

s32 stepLfoTriangle(SoundLfo* lfo) {
    s32 depth;

    if (--lfo->m10_counter == 0) {
        depth = lfo->mC_depth;
        lfo->m10_counter = lfo->m12_period;
        if (lfo->m1E_flags & 8) {
            depth = -depth;
        }
        lfo->m8_step = depth;
        lfo->m1E_flags ^= 8;
    }
    return lfo->m4_value += lfo->m8_step;
}

s32 stepLfoTriangleBipolar(SoundLfo* lfo) {
    u16 flags;
    s32 depth;
    s32 counter = lfo->m10_counter;

    if (--counter == 0) {
        flags = lfo->m1E_flags;
        counter = lfo->m12_period;
        if (flags & 4) {
            counter <<= 1;
        }
        depth = lfo->mC_depth;
        lfo->m8_step = depth;
        if (flags & 8) {
            lfo->m8_step = -depth;
        }
        flags = (flags | 4) ^ 8;
        lfo->m1E_flags = flags;
    }
    lfo->m10_counter = counter;
    return lfo->m4_value += lfo->m8_step;
}

s32 stepLfoSawtooth(SoundLfo* lfo) {
    if (--lfo->m10_counter == 0) {
        lfo->m10_counter = lfo->m12_period;
        lfo->m4_value = 0;
    } else {
        lfo->m4_value += lfo->mC_depth;
    }
    return lfo->m4_value;
}

s32 stepLfoRandom(SoundLfo* lfo) {
    getLfoRandom();
    if (--lfo->m10_counter == 0) {
        lfo->m10_counter = lfo->m12_period;
        lfo->m4_value = (lfo->mC_depth >> 15) * getLfoRandom();
    }
    return lfo->m4_value;
}

s32 stepLfoRandomBipolar(SoundLfo* lfo) {
    if (--lfo->m10_counter == 0) {
        lfo->m10_counter = lfo->m12_period;
        lfo->m4_value = (lfo->mC_depth >> 14) * getLfoRandom() - lfo->mC_depth;
    }
    return lfo->m4_value;
}

void setLfoRandomSeed(s32 seed) {
    lfoRandomSeed = seed;
}

s32 getLfoRandom(void) {
    s32 x = lfoRandomSeed;

    x ^= x << 17;
    x ^= x >> 15;
    lfoRandomSeed = x;
    return x & 0x7FFF;
}

void setSpuKeyOn(u32 mask) {
    spuRegisters->m188_keyOn[0] = mask;
    spuRegisters->m188_keyOn[1] = mask >> 16;
}

void setSpuKeyOff(u32 mask) {
    spuRegisters->m18C_keyOff[0] = mask;
    spuRegisters->m18C_keyOff[1] = mask >> 16;
}

void setSpuReverbVoices(u32 mask) {
    spuRegisters->m198_reverbOn[0] = mask;
    spuRegisters->m198_reverbOn[1] = mask >> 16;
}

void func_8003F4BC(void) {
}

void setSpuVoiceStartAddr(s32 voice, s32 addr) {
    SpuVoiceRegs* v = &spuRegisters->m0_voice[voice];

    v->m6_addr = addr >> 3;
}

void setSpuVoiceLoopAddr(s32 voice, s32 addr) {
    SpuVoiceRegs* v = &spuRegisters->m0_voice[voice];

    v->mE_loopAddr = addr >> 3;
}

void setSpuVoiceVolume(s32 voice, s16 left, s16 right) {
    SpuVoiceRegs* v = &spuRegisters->m0_voice[voice];

    v->m0_volL = left;
    v->m2_volR = right;
}

void setSpuVoicePitch(s32 voice, u16 pitch) {
    SpuVoiceRegs* v = &spuRegisters->m0_voice[voice];

    v->m4_pitch = pitch;
}

void setSpuVoiceAttack(s32 voice, s32 rate, s32 mode) {
    SpuVoiceRegs* v = &spuRegisters->m0_voice[voice];

    v->m8_adsr1 = (u8)v->m8_adsr1 + (rate << 8) + ((mode >> 2) << 15);
}

void setSpuVoiceDecay(s32 voice, s32 rate) {
    SpuVoiceRegs* v = &spuRegisters->m0_voice[voice];

    v->m8_adsr1 = (v->m8_adsr1 & 0xFF0F) + (rate << 4);
}

void setSpuVoiceSustain(s32 voice, s32 rate, s32 mode) {
    SpuVoiceRegs* v = &spuRegisters->m0_voice[voice];

    v->mA_adsr2 = (v->mA_adsr2 & 0x3F) + (rate << 6) + ((mode >> 1) << 14);
}

void setSpuVoiceRelease(s32 voice, s32 rate, s32 mode) {
    SpuVoiceRegs* v = &spuRegisters->m0_voice[voice];

    v->mA_adsr2 = (v->mA_adsr2 & 0xFFC0) + (rate + ((mode >> 2) << 5));
}

void setSpuVoiceSustainLevel(s32 voice, s32 level) {
    SpuVoiceRegs* v = &spuRegisters->m0_voice[voice];

    v->m8_adsr1 = (v->m8_adsr1 & 0xFFF0) + level;
}

s16 checkHeader(SeqHeader* seq, u32 magic, s32 checksum) {
    if (seq->m0_magic != magic) {
        return 1;
    }
    if (checkSeqChecksum(seq) != 0) {
        return 2;
    }
    return (seq->mC_checksum != (u16)checksum) << 2;
}

s16 createMusicInstanceSub0(SeqFile* seq) {
    return 0;
}

s32 checkSeqChecksum(SeqHeader* seq) {
    s32 sum = 0;
    s32* p = (s32*)seq;
    u32 n = (seq->m8_size + 3) >> 2;

    do {
        sum += *p++;
    } while (--n != 0);
    return sum;
}

void setSoundError(s16 code) {
    if (!(musicStatusFlag & 0x88)) {
        musicStatusFlag |= 8;
        soundErrorCode = code;
        freeSamples(0x10000);
        loadWds((Wds*)errorSoundWds, 0);
        loadSequence(errorSoundSed);
        waitForMusic(0x10);
        playSoundEffectInFreeSlot((errorSoundSedBody[0] << 16) | 1);
    }
}
