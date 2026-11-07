/*
 * Sound previews on an SPU of their own (preview.h).
 */

#include "preview.h"

#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "spu/menusfx.h"
#include "spu/spu_internal.h"
#include "system/settings.h"

extern SpuRegs portSpuRegisters; /* port/src/system/spu.c: the block the game writes */
s16 computeSampleRate(s16 pitch); /* the game's sound driver: Q8 note to SPU pitch */

#define MAIN_VOLUME 0x3FFF

struct XgSpuBox {
    SpuState* state;
    SpuState* saved;
    SpuRegs regs, savedRegs;
};

static XgSpuBox* inside;

static uint32_t rd32(const uint8_t* p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static uint16_t rd16(const uint8_t* p) {
    return (uint16_t)(p[0] | p[1] << 8);
}

/* ---- an SPU of its own ------------------------------------------------------------------- */

void xgSpuBoxEnter(XgSpuBox* b) {
    if (inside != NULL) {
        fprintf(stderr, "extras: SPU box entered while inside one (ignored)\n");
        return;
    }
    memcpy(b->saved, &spu, sizeof(spu));
    memcpy(&spu, b->state, sizeof(spu));
    b->savedRegs = portSpuRegisters;
    portSpuRegisters = b->regs;
    inside = b;
}

void xgSpuBoxLeave(XgSpuBox* b) {
    if (inside != b) {
        return;
    }
    memcpy(b->state, &spu, sizeof(spu));
    memcpy(&spu, b->saved, sizeof(spu));
    b->regs = portSpuRegisters;
    portSpuRegisters = b->savedRegs;
    inside = NULL;
}

XgSpuBox* xgSpuBoxCreate(void) {
    XgSpuBox* b = calloc(1, sizeof(XgSpuBox));
    if (b == NULL || (b->state = calloc(1, sizeof(SpuState))) == NULL ||
        (b->saved = malloc(sizeof(SpuState))) == NULL) {
        xgSpuBoxDestroy(b);
        return NULL;
    }
    xgSpuBoxEnter(b);
    spuReset(&portSpuRegisters);
    portSpuRegisters.m1AA_control = 0xC000; /* SPU on, not muted */
    portSpuRegisters.m180_mainVolL = portSpuRegisters.m182_mainVolR = MAIN_VOLUME;
    spuLatch(&portSpuRegisters);
    xgSpuBoxLeave(b);
    return b;
}

void xgSpuBoxDestroy(XgSpuBox* b) {
    if (b != NULL) {
        if (inside == b) {
            xgSpuBoxLeave(b);
        }
        free(b->state);
        free(b->saved);
        free(b);
    }
}

void xgSpuBoxWrite(uint32_t addr, const void* data, uint32_t size) {
    spuRamWrite(addr, data, size);
}

int xgSpuBoxMix(int16_t* out, int frames) {
    spuLatchDriver(&portSpuRegisters);
    /* The SPU's mix without the CD input and the capture buffers, which are
     * the paused game's. */
    if (frames > 0) {
        spuRunBox(out, (uint32_t)frames);
    }
    for (int i = 0; i < 24; i++) {
        if (spu.voices[i].level != 0) {
            return 1;
        }
    }
    return 0;
}

/* ---- a sample ------------------------------------------------------------------------------- */

int xgPreviewSample(const uint8_t* wds, int wdsSize, int preset, int key, int holdFrames, int16_t* out,
                    int maxFrames) {
    if (wdsSize < 0x30 || memcmp(wds, "wds ", 4) != 0) {
        return 0;
    }
    int presets = (int)rd32(wds + 0x1C) + 1;
    if (preset < 0 || preset >= presets || 0x30 + (preset + 1) * 16 > wdsSize) {
        return 0;
    }
    XgSpuBox* b = xgSpuBoxCreate();
    if (b == NULL) {
        return 0;
    }
    /* The bank where the game loads it, the preset as setupAdsr and
     * sendAdsrToSpu set a voice up (port/src/spu/menusfx.c). */
    uint32_t base = rd32(wds + 0x28) != 0 ? rd32(wds + 0x28) : 0x1010;
    uint32_t at = rd32(wds + 0x18), adpcm = rd32(wds + 0x14);
    const uint8_t* ins = wds + 0x30 + preset * 16;
    uint32_t offset = rd32(ins) << 3, adsr = rd32(ins + 8);
    uint16_t modes = rd16(ins + 12);
    int16_t tuning = (int16_t)rd16(ins + 6);
    xgSpuBoxEnter(b);
    if (at < (uint32_t)wdsSize) {
        xgSpuBoxWrite(base, wds + at, adpcm < (uint32_t)wdsSize - at ? adpcm : (uint32_t)wdsSize - at);
    }
    SpuVoiceRegs* v = &portSpuRegisters.m0_voice[0];
    v->m0_volL = v->m2_volR = 0x3FFF;
    v->m4_pitch = (uint16_t)(computeSampleRate((s16)((key << 8) + tuning)) & 0x3FFF);
    v->m6_addr = (uint16_t)((offset + base) >> 3);
    v->mE_loopAddr = (uint16_t)((offset + ((uint32_t)rd16(ins + 4) << 3)) >> 3);
    v->m8_adsr1 = (uint16_t)(((modes & 7) >> 2) << 15 | (adsr & 0x7F) << 8 | ((adsr >> 8) & 0xF) << 4 |
                             ((adsr >> 12) & 0xF));
    v->mA_adsr2 = (uint16_t)((((modes >> 4) & 7) >> 1) << 14 | ((adsr >> 16) & 0x7F) << 6 |
                             (((modes >> 8) & 7) >> 2) << 5 | ((adsr >> 24) & 0x1F));
    portSpuRegisters.m188_keyOn[0] = 1;
    int frames = 0;
    while (frames < maxFrames) {
        int n = maxFrames - frames < 512 ? maxFrames - frames : 512;
        if (frames < holdFrames && holdFrames - frames < n) {
            n = holdFrames - frames; /* a chunk ends where the key goes off */
        }
        if (frames == holdFrames) {
            portSpuRegisters.m18C_keyOff[0] = 1;
        }
        int sounding = xgSpuBoxMix(out + frames * 2, n);
        frames += n;
        if (!sounding && frames > 64) {
            break;
        }
    }
    xgSpuBoxLeave(b);
    xgSpuBoxDestroy(b);
    while (frames > 0 && out[frames * 2 - 2] == 0 && out[frames * 2 - 1] == 0) {
        frames--;
    }
    return frames;
}

int xgPreviewEffect(const uint8_t* seds, int sedsSize, const uint8_t* wds, int wdsSize, int effect, int16_t* out,
                    int maxFrames) {
    /* menusfx.c puts the SPU aside itself, game's or box's. */
    return menuSfxRender(seds, sedsSize, wds, wdsSize, effect, out, maxFrames);
}

/* ---- the preview's audio stream --------------------------------------------------------------- */

static SDL_AudioStream* stream;
static int streamTried;

static SDL_AudioStream* openStream(void) {
    if (!streamTried) {
        streamTried = 1;
        SDL_AudioSpec spec = {SDL_AUDIO_S16, 2, XG_PREVIEW_RATE};
        if (SDL_WasInit(SDL_INIT_AUDIO) || SDL_InitSubSystem(SDL_INIT_AUDIO)) {
            stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, NULL, NULL);
        }
        if (stream != NULL) {
            SDL_ResumeAudioStreamDevice(stream);
        } else {
            fprintf(stderr, "extras: no audio device for previews: %s\n", SDL_GetError());
        }
    }
    return stream;
}

void xgPreviewPlay(const int16_t* frames, int count) {
    xgPreviewStop();
    xgPreviewQueue(frames, count);
}

void xgPreviewQueue(const int16_t* frames, int count) {
    SDL_AudioStream* s = openStream();
    if (s == NULL || count <= 0) {
        return;
    }
    int volume = settingsInt("volume", 100);
    volume = volume < 0 ? 0 : volume > 100 ? 100 : volume;
    SDL_SetAudioStreamGain(s, (float)(volume * volume) / 10000.0f);
    SDL_PutAudioStreamData(s, frames, count * 4);
}

void xgPreviewStop(void) {
    if (stream != NULL) {
        SDL_ClearAudioStream(stream);
    }
}

int xgPreviewPlaying(void) {
    return stream != NULL && SDL_GetAudioStreamQueued(stream) > 0;
}
