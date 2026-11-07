/*
 * The settings menu's sounds (menusfx.h): one sound effect of a SEDS bank,
 * played as the game's sound driver plays it (src/main/sequencer.c:
 * playSoundEffectSub, audioTick, executeSequenceEvents and
 * executeSequenceEvents2, voicePostUpdate2, sendAdsrToSpu, audioTickSub1),
 * through the voices of this SPU, into PCM.
 *
 * The game's own driver cannot play it while the game is paused without
 * changing the game: its state is the game's. So this follows the driver's
 * steps for the two tracks of an effect, with the same tables and the same
 * register writes at the same 240 Hz ticks, and the SPU core's voices play
 * them. The SPU's whole state is put aside first and put back after, the
 * effect's samples are written into its RAM meanwhile, and only voices are
 * run (no reverb, no CD input, no capture): nothing of the game's sound is
 * touched.
 *
 * It follows what the game's menu sounds use (notes, rests and holds,
 * octave, volume and volume slides, instrument and ADSR changes, pitch bends
 * and slides, pitch modulation, legato, loops, note length changes, end).
 * Any other opcode stops the rendering (logged once).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "menusfx.h"
#include "spu_internal.h"

/* The game's sound driver (src/main): its tables and its pitch function. */
extern u8 noteDeltaTimeTable[];
extern u8 noteKeyTable[];
extern u8 opcodeSizes[0x80];
extern s16 musicStatusFlag;
s16 computeSampleRate(s16 pitch);

#define TICK_RATE 240 /* the driver's root counter: 33.8688 MHz / 8 / 0x44E8 */
#define FIRST_VOICE 22 /* playSoundEffect's tracks: the effect instance's last two of 16 */
#define TRACKS 2
#define MAIN_VOLUME 0x7FFE /* initSoundSystem: setMasterVolume(0x3FFF), a fixed volume of 2 x 0x3FFF */

/* A track (SoundTrack) and its voice's shadow (SoundVoice), as far as used. */
typedef struct {
    const u8* pc;
    u16 flags, update, f4;
    u16 delta, gate;
    u8 gateRatio, octave, key, prevKey, releaseBackup;
    s32 pitch, pitchDelta, volume78;
    s16 transpose, pitchBend, volume76;
    u16 pan, pitchSlide, volumeSlide;
    s32 volumeDelta;
    u8 lengthAdjust;
    int loopDepth; /* -1: no loop */
    struct {
        u8 count, octave, endOctave;
        const u8* start;
        const u8* end;
    } loops[4];
    int voice;
    int owned;
    u16 vflags, dirty, volL, volR, vpitch;
    u32 start, repeat;
    u8 attackMode, sustainMode, releaseMode, attack, decay, sustainRate, release, sustainLevel;
} Track;

typedef struct {
    const u8* seds;
    int sedsSize;
    const u8* wds;
    int wdsSize;
    u32 base; /* the samples' SPU address */
    Track t[TRACKS];
    u32 keyOn, keyOff, keyOff2;
    s32 tickAccum;
    SpuRegs regs;
    int stopped; /* an opcode not followed */
} Sfx;

static u32 rd32(const u8* p) {
    return (u32)p[0] | (u32)p[1] << 8 | (u32)p[2] << 16 | (u32)p[3] << 24;
}

static u16 rd16(const u8* p) {
    return (u16)(p[0] | p[1] << 8);
}

/* setupAdsr: the WDS instrument's addresses, envelope and transpose. */
static void setupAdsr(Sfx* s, Track* t, int index) {
    if (0x30 + (index + 1) * 16 > s->wdsSize) {
        s->stopped = 1;
        return;
    }
    const u8* ins = s->wds + 0x30 + index * 16;
    u32 offset = rd32(ins) << 3;
    u32 adsr = rd32(ins + 8);
    u16 modes = rd16(ins + 12);
    t->start = offset + s->base;
    t->repeat = offset + ((u32)rd16(ins + 4) << 3); /* as the driver has it: no base */
    t->attackMode = modes & 7;
    t->sustainMode = (modes >> 4) & 7;
    t->releaseMode = (modes >> 8) & 7;
    t->attack = adsr & 0x7F;
    t->decay = (adsr >> 8) & 0xF;
    t->sustainRate = (adsr >> 16) & 0x7F;
    t->sustainLevel = (adsr >> 12) & 0xF;
    t->release = t->releaseBackup = (adsr >> 24) & 0x1F;
    t->transpose = (s16)rd16(ins + 6);
    t->flags |= 0x8000;
}

/* The opcodes the menu sounds use (src/main/sequencer.c seqOP_*). */
static const u8* opcode(Sfx* s, Track* t, int op, const u8* p) {
    switch (op) {
    case 0x80: /* rest */
        t->delta = p[0];
        t->flags |= 0x400;
        t->update |= 2;
        return p + 1;
    case 0x81: /* hold */
        t->flags |= 0x100;
        t->delta = p[0];
        return p + 1;
    case 0x98: /* loop start */
        if (t->loopDepth >= 3) {
            break;
        }
        t->loopDepth++;
        t->loops[t->loopDepth].count = (u8)(p[0] - 1);
        t->loops[t->loopDepth].start = p + 1;
        t->loops[t->loopDepth].octave = t->octave;
        return p + 1;
    case 0x99: /* loop end */
        if (t->loopDepth < 0) {
            break;
        }
        if (--t->loops[t->loopDepth].count != 0xFF) {
            t->loops[t->loopDepth].end = p;
            t->loops[t->loopDepth].endOctave = t->octave;
            t->octave = t->loops[t->loopDepth].octave;
            return t->loops[t->loopDepth].start;
        }
        t->loopDepth--;
        return p;
    case 0x9A: /* loop break on the last pass */
        if (t->loopDepth < 0) {
            break;
        }
        if (t->loops[t->loopDepth].count == 0) {
            p = t->loops[t->loopDepth].end;
            t->octave = t->loops[t->loopDepth].endOctave;
            t->loopDepth--;
        }
        return p;
    case 0xAD: /* note length change */
        t->lengthAdjust = p[0] != 0 ? (u8)(t->lengthAdjust + p[0]) : 0;
        return p + 1;
    case 0x90: /* end (no loop point in an effect) */
        t->update &= 0xFFFC;
        if (t->owned) {
            t->owned = 0;
            s->keyOff |= 1u << t->voice;
            s->keyOn &= ~(1u << t->voice);
        }
        t->flags = 0;
        return p;
    case 0x94: /* octave */
        t->octave = (u8)(p[0] * 12);
        return p + 1;
    case 0x95:
        t->octave += 12;
        return p;
    case 0x96:
        t->octave -= 12;
        return p;
    case 0xB0: /* legato */
        t->flags |= 0x800;
        return p;
    case 0xB1:
        t->flags &= ~0x800;
        return p;
    case 0xB2: /* pitch modulation by the voice before (odd voices only) */
    case 0xB3:
        if (t->voice & 1) {
            t->dirty |= 0x1000;
            t->vflags = op == 0xB2 ? (t->vflags | 0x10) : (t->vflags & ~0x10);
        }
        return p;
    case 0xC2:
        t->attack = p[0];
        t->dirty |= 0x10;
        return p + 1;
    case 0xC3:
        t->decay = p[0];
        t->dirty |= 0x20;
        return p + 1;
    case 0xC4:
        t->sustainRate = p[0];
        t->dirty |= 0x40;
        return p + 1;
    case 0xC5:
        t->dirty |= 0x80;
        t->release = t->releaseBackup = p[0];
        return p + 1;
    case 0xC6:
        t->sustainLevel = p[0];
        t->dirty |= 0x100;
        return p + 1;
    case 0xD0: /* pitch bend: semitones / 8 (the next notes) */
        t->pitchBend = (s16)((s8)p[0] * 32);
        t->update |= 0x200;
        return p + 1;
    case 0xD1:
        t->pitchBend = (s16)(t->pitchBend + (s8)p[0] * 32);
        t->update |= 0x200;
        return p + 1;
    case 0xD2: /* fine: semitones / 32 */
        t->pitchBend = (s16)(t->pitchBend + (s8)p[0] * 8);
        t->update |= 0x200;
        return p + 1;
    case 0xD3:
        t->pitchBend = (s16)(t->pitchBend + p[1] + (s8)p[0] * 256);
        t->update |= 0x200;
        return p + 2;
    case 0xD4: { /* pitch slide */
        u16 len = p[0];
        s32 target = (s32)(s8)p[1] << 24;
        if (len != 0 && target != 0) {
            t->pitchSlide = len;
            t->f4 |= 1;
            t->pitchDelta = target / len;
        } else {
            t->f4 &= ~1;
        }
        return p + 2;
    }
    case 0xDC:
        t->f4 &= ~1;
        return p;
    case 0xE0: /* volume */
        t->volume78 = (s32)((u32)p[0] << 24);
        t->update |= 0x100;
        t->f4 &= 0xFEF7;
        return p + 1;
    case 0xE2: { /* volume slide */
        u16 len = p[0];
        s32 diff = (s32)((u32)(s8)p[1] << 24) - t->volume78;
        if (len != 0 && diff != 0) {
            t->volumeSlide = len;
            t->f4 = (u16)((t->f4 | 8) & 0xFEFF);
            t->volumeDelta = diff / len;
        }
        return p + 2;
    }
    case 0xFC: /* bank and instrument (one bank here) */
        setupAdsr(s, t, p[1]);
        return p + 2;
    case 0xFE: /* bank */
        return p + 1;
    default:
        break;
    }
    {
        static int told;
        if (!told) {
            told = 1;
            fprintf(stderr, "spu: menu sound: opcode %02X not followed\n", op);
        }
        s->stopped = 1;
        t->flags = 0;
        return p;
    }
}

/* executeSequenceEvents2: slides, and the counts to the note's end. */
static void countDown(Track* t) {
    if (t->flags == 0 || t->delta == 0) {
        return;
    }
    if (t->f4 & 8) {
        t->update |= 0x100;
        if (--t->volumeSlide == 0) {
            t->f4 &= ~8;
        }
        t->volume78 += t->volumeDelta;
    }
    if (t->f4 & 1) {
        t->update |= 0x200;
        if (!(t->f4 & 2) && --t->pitchSlide == 0) {
            t->f4 &= ~1;
        }
        t->pitch += t->pitchDelta;
    }
    t->delta--;
    t->gate--;
    if (t->delta == 1 && (t->flags & 0x1000)) {
        t->release = 6;
        t->dirty |= 0x80;
    }
    if (t->gate == 0) {
        t->flags |= 0x400;
        t->update |= 2;
    }
}

/* executeSequenceEvents: the track's next events, once its note is over. */
static void events(Sfx* s, Track* t) {
    if (t->flags == 0 || t->delta != 0) {
        return;
    }
    int post = 0;
    u16 orig = t->flags;
    const u8* p = t->pc;
    const u8* end = s->seds + s->sedsSize;
    t->flags = orig & 0xF8FF;
    do {
        if (p + 3 > end) {
            s->stopped = 1;
            t->flags = 0;
            return;
        }
        int op = *p++;
        if (op < 0x80) {
            int data = *p++;
            int time;
            t->update |= 0x100;
            if (!(t->flags & 8)) {
                t->volume76 = (s16)(op << 8);
            }
            t->key = (u8)(t->octave + noteKeyTable[data]);
            time = noteDeltaTimeTable[data];
            if (time == 0) {
                time = *p++;
            }
            t->delta = (u16)time;
            t->release = t->releaseBackup;
            t->dirty |= 0x80;
            t->pitch = ((t->key << 8) + t->pitchBend + t->transpose) << 16;
            t->update |= 0x200;
            t->flags |= 0x180;
            post = 1;
            if (orig & 0x400) {
                t->update |= 1;
            }
            if (t->flags & 0x8000) {
                t->flags &= 0x7FFF;
                t->dirty = 0xFFFF;
                t->update |= 0x300;
            }
        } else {
            p = opcode(s, t, op, p);
            if (t->flags == 0) {
                return;
            }
        }
    } while (!(t->flags & 0x500));
    t->pc = p;
    if (t->flags & 0x800) {
        t->flags |= 0x200;
    }
    /* What comes next, through loop ends still to repeat and breaks: a rest
     * or a legato change ends the legato, a hold keeps it, a note keeps the
     * release short at the end of this one. An effect has no loop point, so
     * an end ends the look. */
    int depth = t->loopDepth;
    int op = *p;
    while (op >= 0x80) {
        if (op == 0x90) {
            break;
        }
        if (op == 0x80 || op == 0xB0 || op == 0xB1) {
            t->flags &= ~0x200;
            break;
        }
        if (op == 0x81) {
            t->flags |= 0x200;
            break;
        }
        if (op == 0x99 && depth >= 0) {
            if (t->loops[depth].count != 0) {
                p = t->loops[depth].start;
                op = p < end ? *p : 0x90;
                continue;
            }
            depth--;
        }
        if (op == 0x9A && depth >= 0 && t->loops[depth].count == 0) {
            p = t->loops[depth].end;
            depth--;
        } else if (opcodeSizes[op - 0x80] != 0) {
            p += opcodeSizes[op - 0x80];
        } else {
            break; /* unused opcodes: the driver would not get past them either */
        }
        op = p < end ? *p : 0x90;
    }
    if (op < 0x80) {
        t->flags |= 0x1000;
    } else {
        t->flags &= ~0x1000;
    }
    /* The note length change, kept from making a note end before it starts. */
    u8 adjust = t->lengthAdjust;
    s16 time = (s16)((s8)adjust + t->delta);
    if (time <= 0) {
        t->lengthAdjust = (u8)(adjust + (u8)t->delta);
        time = (s16)(t->delta + time);
    }
    u16 gate = 0x7FFF;
    if (!(t->flags & 0x600)) {
        if (t->gateRatio == 16) {
            gate = (u16)time;
        } else {
            gate = t->gateRatio == 15 ? (u16)(time - 1) : (u16)((time * t->gateRatio) >> 4);
            if (gate == 0) {
                gate = 1;
            }
        }
    }
    t->delta = (u16)time;
    t->gate = gate;
    if (post) {
        t->prevKey = t->key;
    }
}

/* voicePostUpdate2: volume, pitch, key on and off from what changed. */
static void postUpdate(Sfx* s, Track* t) {
    if (t->flags == 0) {
        return;
    }
    if (t->update & 0x100) {
        s32 volume = (s16)(t->volume78 >> 16);
        s32 pan = t->pan;
        volume = volume < 0 ? 0 : volume > 0x7FFF ? 0x7FFF : volume;
        volume = (t->volume76 * volume) >> 15;
        volume = (0x7F00 * volume) >> 16; /* the instance's volume, 0x7F000000 */
        pan = pan < 0 ? 0 : pan > 0x7F00 ? 0x7F00 : pan;
        s32 left, right;
        if (musicStatusFlag & 0x100) { /* stereo (the game's sound option) */
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
            right = left = (volume * 0x5A00) >> 15;
        }
        t->volL = (u16)left;
        t->volR = (u16)right;
        t->dirty |= 1;
    }
    if (t->update & 0x200) {
        t->vpitch = (u16)(computeSampleRate((s16)(t->pitch >> 16)) & 0x3FFF);
        t->dirty |= 4;
    }
    if ((t->update & 1) && !(t->flags & 0x20) && t->owned) {
        s->keyOn |= 1u << t->voice;
    }
    if ((t->update & 2) && t->owned) {
        s->keyOff2 |= 1u << t->voice;
    }
    t->update = 0;
}

/* sendAdsrToSpu: the changed voice registers, then the key ons. */
static void sendRegisters(Sfx* s) {
    u32 fm = 0;
    u16 changed = 0;
    for (int i = 0; i < TRACKS; i++) {
        Track* t = &s->t[i];
        SpuVoiceRegs* r = &s->regs.m0_voice[t->voice];
        if (!t->owned) {
            continue;
        }
        u16 d = t->dirty;
        if (d & 1) {
            r->m0_volL = t->volL;
            r->m2_volR = t->volR;
        }
        if (d & 4) {
            r->m4_pitch = t->vpitch;
        }
        if (d & 8) {
            r->m6_addr = (u16)(t->start >> 3);
            r->mE_loopAddr = (u16)(t->repeat >> 3);
        }
        if (d & 0x10) {
            r->m8_adsr1 = (u16)((u8)r->m8_adsr1 + (t->attack << 8) + ((t->attackMode >> 2) << 15));
        }
        if (d & 0x20) {
            r->m8_adsr1 = (u16)((r->m8_adsr1 & 0xFF0F) + (t->decay << 4));
        }
        if (d & 0x40) {
            r->mA_adsr2 = (u16)((r->mA_adsr2 & 0x3F) + (t->sustainRate << 6) + ((t->sustainMode >> 1) << 14));
        }
        if (d & 0x80) {
            r->mA_adsr2 = (u16)((r->mA_adsr2 & 0xFFC0) + (t->release + ((t->releaseMode >> 2) << 5)));
        }
        if (d & 0x100) {
            r->m8_adsr1 = (u16)(t->sustainLevel + (r->m8_adsr1 & 0xFFF0));
        }
        changed |= d & 0x7000;
        t->dirty = 0;
        fm |= (u32)((t->vflags >> 4) & 1) << t->voice;
    }
    if (changed & 0x1000) {
        s->regs.m190_fm[0] = (u16)fm;
        s->regs.m190_fm[1] = (u16)(fm >> 16);
    }
    if (s->keyOn != 0) {
        s->regs.m188_keyOn[0] = (u16)s->keyOn;
        s->regs.m188_keyOn[1] = (u16)(s->keyOn >> 16);
        s->keyOn = 0;
    }
}

/* audioTickSub1: the key offs, with the quick release the driver gives them. */
static void sendKeyOffs(Sfx* s) {
    for (int v = 0; v < 24; v++) {
        if (s->keyOff & (1u << v)) {
            s->regs.m0_voice[v].mA_adsr2 = (u16)((s->regs.m0_voice[v].mA_adsr2 & 0xFFC0) | 6);
        }
    }
    u32 off = s->keyOff | s->keyOff2;
    if (off != 0) {
        s->regs.m18C_keyOff[0] = (u16)off;
        s->regs.m18C_keyOff[1] = (u16)(off >> 16);
        s->keyOff = s->keyOff2 = 0;
    }
}

/* One audio tick (audioTick) for the effect; 0 once its tracks are done. */
static int tick(Sfx* s) {
    sendRegisters(s);
    s->tickAccum -= 0x6600; /* the effect instance's tick rate: tempo 0x66 */
    while (s->tickAccum < 0) {
        s->tickAccum += 0x10000;
        for (int i = 0; i < TRACKS; i++) {
            countDown(&s->t[i]);
        }
        for (int i = 0; i < TRACKS; i++) {
            events(s, &s->t[i]);
        }
    }
    int active = 0;
    for (int i = 0; i < TRACKS; i++) {
        postUpdate(s, &s->t[i]);
        active |= s->t[i].flags != 0;
    }
    sendKeyOffs(s);
    return active && !s->stopped;
}

/* playSoundEffectSub for one effect: its two tracks on voices 22 and 23. */
static int start(Sfx* s, int effect) {
    if (s->sedsSize < 0x20 || memcmp(s->seds, "seds", 4) != 0 || s->wdsSize < 0x30 ||
        memcmp(s->wds, "wds ", 4) != 0) {
        return 0;
    }
    int count = rd16(s->seds + 0x12);
    int volumes = rd16(s->seds + 0x18);
    if (effect < 0 || effect >= count || 0x20 + (effect + 1) * 4 > s->sedsSize || volumes + effect >= s->sedsSize) {
        return 0;
    }
    u32 vol = (u32)((0x6000 * s->seds[volumes + effect]) >> 7);
    vol = (vol >> 15) & 1 ? 0x7FFF : vol;
    s->base = rd32(s->wds + 0x28) != 0 ? rd32(s->wds + 0x28) : 0x1010;
    int any = 0;
    for (int i = 0; i < TRACKS; i++) {
        Track* t = &s->t[i];
        u16 at = rd16(s->seds + 0x20 + effect * 4 + i * 2);
        memset(t, 0, sizeof(*t));
        t->voice = FIRST_VOICE + i;
        if (at == 0 || at >= s->sedsSize) {
            continue;
        }
        any = 1;
        t->flags = (rd16(s->seds + 0x10) & 1) ? 0x40B : 0x409;
        t->update = 0x170;
        t->pc = s->seds + at;
        t->octave = 0x3C;
        t->gateRatio = 0xF;
        t->volume76 = (s16)vol;
        t->volume78 = 0x7F000000;
        t->loopDepth = -1;
        t->pan = 0x4000;
        setupAdsr(s, t, 0);
        /* claimSpuVoice */
        t->owned = 1;
        t->dirty = 0xFFFF;
        s->keyOff |= 1u << t->voice;
        s->keyOn &= ~(1u << t->voice);
    }
    return any;
}

int menuSfxRender(const uint8_t* seds, int sedsSize, const uint8_t* wds, int wdsSize, int effect, int16_t* out,
                  int maxFrames) {
    static SpuState* saved;
    Sfx* s = (Sfx*)calloc(1, sizeof(Sfx));
    if (s == NULL || (saved == NULL && (saved = (SpuState*)malloc(sizeof(SpuState))) == NULL)) {
        free(s);
        return 0;
    }
    s->seds = seds;
    s->sedsSize = sedsSize;
    s->wds = wds;
    s->wdsSize = wdsSize;
    /* initSoundInstanceDefaults: keep the driver's tempo phase.
     * Forcing events onto the first audio tick with 0x65FF
     * leaves a full sequence tick in the accumulator afterwards: the next
     * 8-tick legato note starts one audio tick late (Menu: choose). */
    s->tickAccum = 0x10000;
    if (!start(s, effect)) {
        free(s);
        return 0;
    }

    /* The game's SPU aside; a reset one with the bank's samples where the
     * game loads them. */
    memcpy(saved, &spu, sizeof(spu));
    spuReset(&s->regs);
    u32 headerSize = rd32(wds + 0x18), adpcmSize = rd32(wds + 0x14);
    if (headerSize < (u32)wdsSize) {
        spuRamWrite(s->base, wds + headerSize,
                    adpcmSize < (u32)wdsSize - headerSize ? adpcmSize : (u32)wdsSize - headerSize);
    }
    s->regs.m1AA_control = 0xC000;
    spuLatch(&s->regs);

    /* The first two driver ticks have no sequence events. Omit their
     * silent preroll, keeping the menu's onset and sample-clock phase. */
    for (int i = 0; i < 2; i++) {
        tick(s);
        spuLatchDriver(&s->regs);
    }

    int frames = 0, ticks = 0, playing = 1;
    while (frames < maxFrames) {
        if (playing) {
            playing = tick(s);
        }
        spuLatchDriver(&s->regs);
        /* 44100 / 240 = 183.75 samples a tick. */
        int n = (ticks & 3) == 3 ? 183 : 184;
        ticks++;
        int heard = 0;
        for (int i = 0; i < n && frames < maxFrames; i++, frames++) {
            int32_t mix[2] = {0, 0};
            for (int v = FIRST_VOICE; v < FIRST_VOICE + TRACKS; v++) {
                int32_t x = spuVoiceSample(v);
                mix[0] += (x * spu.voices[v].volume[0].level) >> 15;
                mix[1] += (x * spu.voices[v].volume[1].level) >> 15;
                spuVoiceAdvance(v);
            }
            for (int lr = 0; lr < 2; lr++) {
                out[frames * 2 + lr] = (int16_t)spuClamp16((spuClamp16(mix[lr]) * MAIN_VOLUME) >> 15);
            }
        }
        for (int v = FIRST_VOICE; v < FIRST_VOICE + TRACKS; v++) {
            heard |= spu.voices[v].level != 0;
        }
        if (!playing && !heard) {
            break; /* the tracks are over and their release has faded */
        }
    }
    memcpy(&spu, saved, sizeof(spu));
    int stopped = s->stopped;
    free(s);
    /* A voice heard at no volume (a pitch modulator) may outlast the sound. */
    while (frames > 0 && out[frames * 2 - 2] == 0 && out[frames * 2 - 1] == 0) {
        frames--;
    }
    return stopped ? 0 : frames;
}
