/*
 * The extras' audio streams, and their recording (audiotap.h).
 */

#include "audiotap.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TAP_RATE 44100
#define TAP_STREAMS 8

static struct {
    int checked;
    FILE* dump;
    SDL_AudioStream* streams[TAP_STREAMS];
    int count;
    double owed; /* the clock's fraction of a sample frame not yet played */
    int32_t* mix;
    int16_t* buf;
    int cap; /* frames */
} tap;

static FILE* recording(void) {
    if (!tap.checked) {
        tap.checked = 1;
        const char* path = getenv("XG_EXTRAS_AUDIO_DUMP");
        if (path != NULL && path[0] != '\0') {
            tap.dump = fopen(path, "wb");
            if (tap.dump == NULL) {
                fprintf(stderr, "extras: cannot write XG_EXTRAS_AUDIO_DUMP %s\n", path);
            } else {
                fprintf(stderr, "extras: sound recorded on the frame clock into %s\n", path);
            }
        }
    }
    return tap.dump;
}

SDL_AudioStream* xgTapOpen(const SDL_AudioSpec* spec) {
    if (!SDL_WasInit(SDL_INIT_AUDIO) && !SDL_InitSubSystem(SDL_INIT_AUDIO)) {
        return NULL;
    }
    if (recording() == NULL) {
        SDL_AudioStream* s = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, spec, NULL, NULL);
        if (s != NULL) {
            SDL_ResumeAudioStreamDevice(s);
        }
        return s;
    }
    if (tap.count == TAP_STREAMS) {
        SDL_SetError("extras: more than %d recorded streams", TAP_STREAMS);
        return NULL;
    }
    const SDL_AudioSpec out = {SDL_AUDIO_S16, 2, TAP_RATE};
    SDL_AudioStream* s = SDL_CreateAudioStream(spec, &out);
    if (s != NULL) {
        tap.streams[tap.count++] = s;
    }
    return s;
}

void xgTapClose(SDL_AudioStream* s) {
    if (s == NULL) {
        return;
    }
    for (int i = 0; i < tap.count; i++) {
        if (tap.streams[i] == s) {
            tap.streams[i] = tap.streams[--tap.count];
            break;
        }
    }
    SDL_DestroyAudioStream(s);
}

void xgTapFrame(double seconds) {
    FILE* f = recording();
    if (f == NULL) {
        return;
    }
    tap.owed += seconds * TAP_RATE;
    int n = (int)tap.owed;
    tap.owed -= n;
    if (n <= 0) {
        return;
    }
    if (n > tap.cap) {
        int32_t* mix = realloc(tap.mix, (size_t)n * 2 * sizeof *mix);
        int16_t* buf = realloc(tap.buf, (size_t)n * 2 * sizeof *buf);
        if (mix != NULL) {
            tap.mix = mix;
        }
        if (buf != NULL) {
            tap.buf = buf;
        }
        if (mix == NULL || buf == NULL) {
            return;
        }
        tap.cap = n;
    }
    memset(tap.mix, 0, (size_t)n * 2 * sizeof *tap.mix);
    for (int i = 0; i < tap.count; i++) {
        /* What a stream holds short of the frame's time is silence, as a device's underrun. */
        int got = SDL_GetAudioStreamData(tap.streams[i], tap.buf, n * 4);
        for (int k = 0; k < (got > 0 ? got / 2 : 0); k++) {
            tap.mix[k] += tap.buf[k];
        }
    }
    for (int k = 0; k < n * 2; k++) {
        int32_t v = tap.mix[k];
        tap.buf[k] = (int16_t)(v > 32767 ? 32767 : v < -32768 ? -32768 : v);
    }
    fwrite(tap.buf, 4, (size_t)n, f);
    fflush(f);
}
