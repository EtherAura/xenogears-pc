/*
 * The SPU's output to the host: an SDL3 audio stream the game thread fills as
 * the SPU follows the virtual clock (port/src/system/spu.c), and the device
 * drains at its own clock.
 *
 * The stream plays at the virtual clock's rate (hwClockRate: the display lock
 * runs it up to 0.4% fast or slow, displaylock.c), so the queue holds its
 * target without the drift correction's help. The two clocks still drift
 * apart (and the virtual one stalls with the game), so the stream's queue is
 * kept near a target latency by playing very slightly faster or slower on top
 * (SDL_SetAudioStreamFrequencyRatio, at most 0.5%, which is inaudible); a
 * queue far over the target (a burst after a stall, or a stepped clock running
 * ahead of real time) is cut back, and playback is lossy there: an arrival
 * over the limit keeps only its last target's worth of frames before it is
 * batched, a queue over it is dropped and its reserve filled again.
 * XG_AUDIO_DUMP still receives every frame.
 *
 * A game thread stalled for longer than the queue (a hi-res canvas trim, a
 * long load) leaves the device short: SDL pads the request with silence. The
 * game then catches the stall up at once (hw.c), so the frames that come are
 * as late as that silence was long: as many are skipped (spuOutputPush), and
 * what is left fades in from the silence, in step with the picture again.
 * Kept, they would hold the sound that far behind the picture until the drift
 * correction drained them: a minute and more after a 200 ms stall. A stall
 * hw.c takes out of the clock instead is not caught up (nothing is late): the
 * queue the device played through it is put back in silence
 * (spuOutputClockGap), so the sound stays as far behind the picture as before.
 * Small arrivals are batched before entering SDL, and playback starts only
 * once its reserve is filled. The game's busy waits can generate individual
 * samples: locking the stream and changing its ratio for each one competes
 * with the audio thread without improving latency.
 *
 * While the settings menu pauses the game (port/src/ui) the SPU produces
 * nothing: what is still queued is faded out (it is our own input, kept in a
 * ring, so it can be put back faded), and the first frames after the pause
 * fade in, so neither end clicks. A pause shorter than that queue resumes
 * with the device still playing the faded tail: it is not paused under it.
 *
 * A device underrun means the queue was shorter than a stall: the target
 * then grows (audio_latency_max caps it) so the next stall of that size is
 * covered, and shrinks back a little every half minute without one, the way
 * frontends raise their audio latency when the host cannot keep up. The
 * drift correction builds the extra queue over a few seconds (at most 0.5%
 * slower), without a gap.
 *
 * Settings (settings.ini; XG_<KEY> in the environment overrides):
 *   audio_latency     target queue in milliseconds (default 40)
 *   audio_latency_max the target's ceiling when underruns grow it (default 150; the
 *                     base value disables the growth)
 *   volume            master volume in percent, 0-100 (default 100)
 * Environment:
 *   XG_AUDIO_DUMP     file that receives every frame the SPU produces, raw s16
 *                     stereo at 44.1 kHz, independent of the device and the
 *                     volume: with XG_CLOCK=step it is the same on every run
 *   SDL_AUDIO_DRIVER  dummy (no sound) and disk (SDL's own capture) work too
 */

/* fseeko, ftruncate and fileno (the dump seeks on a state load) under -std=c11 too. */
#define _POSIX_C_SOURCE 200809L
#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "output.h"
#include "spu.h"
#include "system/settings.h"
#include "../system/savestate.h"
#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#endif

/* port/src/system/hw.c: the virtual clock's rate (weak: the output's unit
 * test links this file alone; it plays at 1). */
extern double hwClockRate(void) __attribute__((weak));

#define MAX_RATIO_OFFSET 0.005
#define OVERFLOW_FACTOR 4 /* cut back above this many times the target */
#define RING_FRAMES 32768 /* the last frames put into the stream (0.74 s) */
#define FADE_IN_FRAMES 441 /* 10 ms */
#define OUTPUT_FRAMES 128 /* batch busy-wait safe points: less than 3 ms */

static int opened;
static SDL_AudioStream* stream;
static FILE* dump;
static uint64_t dumped; /* frames written to the dump */
static uint32_t unflushed;
static int targetFrames;   /* the queue the drift correction steers to: base + boost */
static int baseFrames;     /* audio_latency */
static int maxFrames;      /* audio_latency_max */
static int boostFrames;    /* grown by underruns, decayed in quiet */
static int seenStarved;    /* the starved count the growth last acted on */
static uint64_t quietSince; /* pushed frames when the boost last changed */
static double smoothed;
static int logStats;
static uint64_t pushed;
static uint64_t nextReport;
static int dryPushes; /* pushes that found the queue empty: the device may have run dry */
static int cuts;      /* the lossy paths: a queue over the limit dropped, or an arrival trimmed (audible skips) */
static int16_t ring[RING_FRAMES][2];
static uint32_t ringPos; /* frames ever put into the stream, mod RING_FRAMES */
static int fadeIn;       /* frames of the fade-in still to come */
static int paused;
static int16_t pending[OUTPUT_FRAMES][2];
static uint32_t pendingFrames;
static int prefilling;
/* Device requests the queue could not fill (SDL's get callback: more wanted than queued), outside
 * pauses and the prefill: an underrun the device pads with silence, heard as a crackle. The
 * push-time dry count cannot see one that a later push refills. */
static SDL_AtomicInt starved;
/* ... and the frames of silence it padded them with (SDL's shortfall, in our
 * frames): the frames still to come are that late, owed back (payOwed). */
static SDL_AtomicInt owed;
static void SDLCALL onDeviceGet(void* user, SDL_AudioStream* s, int additional, int total) {
    (void)user; (void)s; (void)total;
    if (additional > 0 && !prefilling && !paused) {
        SDL_AddAtomicInt(&starved, 1);
        SDL_AddAtomicInt(&owed, additional / (int)(sizeof(int16_t) * 2));
    }
}
static int deviceFrames;

static void flushPending(void);

/* Start with the requested reserve, including after a state load. Starting
 * an empty device lets it consume each small arrival immediately; the drift
 * correction alone takes seconds to build a useful queue. */
static void prefillOutput(void) {
    SDL_PauseAudioStreamDevice(stream);
    prefilling = 1;
    smoothed = targetFrames;
    SDL_SetAtomicInt(&owed, 0); /* the reserve starts the sound's time again */
}

/* audio_latency and volume, from the settings. */
void spuOutputApplySettings(void) {
    settingsDescribe("audio_latency", "audio_latency: the sound output's queue in milliseconds (default 40)");
    settingsDescribe("volume", "volume: master volume in percent, 0 to 100");
    settingsDescribe("audio_latency_max", "audio_latency_max: how far underruns may grow the queue, in milliseconds (default 150)");
    int ms = settingsInt("audio_latency", 40);
    baseFrames = SPU_RATE * (ms < 1 ? 1 : ms > 1000 ? 1000 : ms) / 1000;
    if (baseFrames < 256) {
        baseFrames = 256;
    }
    /* The device must be able to take one full block without exhausting
     * the stream, including when a requested latency is below its block. */
    if (baseFrames < deviceFrames) baseFrames = deviceFrames;
    int maxMs = settingsInt("audio_latency_max", 150);
    maxFrames = SPU_RATE * (maxMs < 1 ? 1 : maxMs > 1000 ? 1000 : maxMs) / 1000;
    if (maxFrames < baseFrames) maxFrames = baseFrames;
    if (boostFrames > maxFrames - baseFrames) boostFrames = maxFrames - baseFrames;
    targetFrames = baseFrames + boostFrames;
    smoothed = targetFrames;
    int volume = settingsInt("volume", 100);
    volume = volume < 0 ? 0 : volume > 100 ? 100 : volume;
    if (stream != NULL) {
        /* Perceived loudness: the percentage squared. */
        SDL_SetAudioStreamGain(stream, (float)(volume * volume) / 10000.0f);
    }
}

static void openOutput(void) {
    const char* path = getenv("XG_AUDIO_DUMP");
    SDL_AudioSpec spec = {SDL_AUDIO_S16, 2, SPU_RATE};

    opened = 1;
    logStats = getenv("XG_SPU_LOG") != NULL && atoi(getenv("XG_SPU_LOG")) > 0;
    spuOutputApplySettings();
    if (path != NULL && path[0] != '\0') {
        dump = fopen(path, "wb");
        if (dump == NULL) {
            fprintf(stderr, "spu: cannot write XG_AUDIO_DUMP %s\n", path);
        }
    }
    if (!SDL_WasInit(SDL_INIT_AUDIO) && !SDL_InitSubSystem(SDL_INIT_AUDIO)) {
        fprintf(stderr, "spu: no audio: %s\n", SDL_GetError());
        return;
    }
    stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, NULL, NULL);
    if (stream == NULL) {
        fprintf(stderr, "spu: no audio device: %s\n", SDL_GetError());
        return;
    }
    SDL_SetAudioStreamGetCallback(stream, onDeviceGet, NULL);
    SDL_AudioSpec dev = {0};
    int frames = 0;
    if (SDL_GetAudioDeviceFormat(SDL_GetAudioStreamDevice(stream), &dev, &frames) && dev.freq > 0) {
        deviceFrames = (int)(((int64_t)frames * SPU_RATE + dev.freq - 1) / dev.freq) + OUTPUT_FRAMES;
    }
    spuOutputApplySettings();
    prefillOutput();
    if (logStats) {
        fprintf(stderr, "spu: audio out via %s, device %d Hz, %d-frame buffer, target queue %d frames\n",
                SDL_GetCurrentAudioDriver(), dev.freq, frames, targetFrames);
    }
}

/* Into the stream, and into the ring (what may have to be faded out). */
static void putFrames(const int16_t* frames, uint32_t count) {
    if (count == 0) {
        return;
    }
    SDL_PutAudioStreamData(stream, frames, (int)(count * sizeof(int16_t) * 2));
    for (uint32_t i = 0; i < count; i++) {
        ring[ringPos][0] = frames[i * 2];
        ring[ringPos][1] = frames[i * 2 + 1];
        ringPos = (ringPos + 1) % RING_FRAMES;
    }
}

void spuOutputPause(int on) {
    if (on == paused) {
        return;
    }
    if (on) flushPending();
    paused = on;
    SDL_SetAtomicInt(&owed, 0); /* the pause's fades replace any skip */
    if (stream == NULL) {
        return;
    }
    if (!on) {
        fadeIn = FADE_IN_FRAMES;
        int queued = prefilling ? 0 : SDL_GetAudioStreamQueued(stream) / (int)(sizeof(int16_t) * 2);
        if (queued <= 0) {
            prefillOutput(); /* the tail has played: the device is silent */
            return;
        }
        /* Within the queue's length of the pause the device is still playing
         * the faded tail; pausing it now would stop it on a non-zero sample
         * (a click). Let the tail drain, with the reserve filled behind it
         * in silence, which the fade-in then follows. */
        static const int16_t silence[OUTPUT_FRAMES][2];
        int startFrames = targetFrames > deviceFrames ? targetFrames : deviceFrames;
        for (int n = startFrames - queued; n > 0; n -= OUTPUT_FRAMES) {
            putFrames(&silence[0][0], (uint32_t)(n < OUTPUT_FRAMES ? n : OUTPUT_FRAMES));
        }
        smoothed = targetFrames;
        return;
    }
    if (prefilling) {
        SDL_ClearAudioStream(stream);
        return; /* no sound has reached the device yet */
    }
    /* What the device has not taken yet is the end of the ring: put it back
     * faded to silence. */
    SDL_LockAudioStream(stream);
    int queued = SDL_GetAudioStreamQueued(stream) / (int)(sizeof(int16_t) * 2);
    if (queued > RING_FRAMES) {
        queued = RING_FRAMES;
    }
    SDL_ClearAudioStream(stream);
    static int16_t tail[RING_FRAMES][2];
    for (int i = 0; i < queued; i++) {
        uint32_t at = (ringPos + RING_FRAMES - (uint32_t)queued + (uint32_t)i) % RING_FRAMES;
        float g = 1.0f - (float)(i + 1) / (float)queued;
        tail[i][0] = (int16_t)(ring[at][0] * g);
        tail[i][1] = (int16_t)(ring[at][1] * g);
        /* The ring keeps what the stream holds: a pause soon after this
         * one fades what actually plays. */
        ring[at][0] = tail[i][0];
        ring[at][1] = tail[i][1];
    }
    if (queued > 0) {
        SDL_PutAudioStreamData(stream, tail, queued * (int)(sizeof(int16_t) * 2));
    }
    SDL_UnlockAudioStream(stream);
}

/* Lossy cuts so far: a queue over the limit dropped, or an arrival trimmed (XG_CPU_TIMING). */
int spuOutputCuts(void) {
    return cuts;
}

/* Device requests the queue could not fill so far (XG_CPU_TIMING). */
int spuOutputStarved(void) {
    return SDL_GetAtomicInt(&starved);
}

void spuOutputLevels(SpuOutputLevels* out) {
    memset(out, 0, sizeof(*out));
    if (stream == NULL) return;
    out->open = 1;
    out->queued = SDL_GetAudioStreamQueued(stream) / (int)(sizeof(int16_t) * 2) + (int)pendingFrames;
    out->target = targetFrames;
    out->base = baseFrames;
    out->max = maxFrames;
    out->prefilling = prefilling;
    out->average = smoothed;
}

/* Pushes so far that found the device's queue empty (XG_CPU_TIMING). */
int spuOutputDryPushes(void) {
    return dryPushes;
}

static void flushPending(void) {
    const int16_t* frames = &pending[0][0];
    uint32_t count = pendingFrames;
    if (stream == NULL || count == 0) return;
    pendingFrames = 0;
    int queued = SDL_GetAudioStreamQueued(stream) / (int)(sizeof(int16_t) * 2);
    dryPushes += queued == 0 && !prefilling;
    if (queued > targetFrames * OVERFLOW_FACTOR) {
        SDL_ClearAudioStream(stream);
        ++cuts;
        queued = 0;
        prefillOutput();
    }
    static int16_t faded[FADE_IN_FRAMES][2];
    uint32_t done = 0;
    while (fadeIn > 0 && done < count) {
        /* After a pause: from silence up to the signal. */
        int n = 0;
        for (; fadeIn > 0 && done < count && n < FADE_IN_FRAMES; n++, done++, fadeIn--) {
            float g = 1.0f - (float)fadeIn / FADE_IN_FRAMES;
            faded[n][0] = (int16_t)(frames[done * 2] * g);
            faded[n][1] = (int16_t)(frames[done * 2 + 1] * g);
        }
        putFrames(&faded[0][0], (uint32_t)n);
    }
    putFrames(frames + done * 2, count - done);

    /* Underruns grow the target (the file's comment): by 20 ms each, up to
     * the ceiling; half a minute without one gives 10 ms back. */
    int starvedNow = SDL_GetAtomicInt(&starved);
    if (baseFrames > 0) { /* settings applied (the unit test sets targetFrames itself) */
        if (starvedNow != seenStarved) {
            seenStarved = starvedNow;
            int grown = boostFrames + SPU_RATE * 20 / 1000;
            boostFrames = grown > maxFrames - baseFrames ? maxFrames - baseFrames : grown;
            quietSince = pushed;
        } else if (boostFrames > 0 && pushed - quietSince > (uint64_t)SPU_RATE * 30) {
            boostFrames -= SPU_RATE * 10 / 1000;
            if (boostFrames < 0) boostFrames = 0;
            quietSince = pushed;
        }
        targetFrames = baseFrames + boostFrames;
    }
    /* Follow the queue's average (about a quarter second) and steer it back
     * to the target, from the clock's own rate (the file's comment). */
    double weight = count / (SPU_RATE * 0.25);
    smoothed += (queued + (double)count - smoothed) * (weight > 1 ? 1 : weight);
    double offset = (smoothed - targetFrames) / targetFrames * 0.01;
    if (offset > MAX_RATIO_OFFSET) {
        offset = MAX_RATIO_OFFSET;
    } else if (offset < -MAX_RATIO_OFFSET) {
        offset = -MAX_RATIO_OFFSET;
    }
    double ratio = (hwClockRate != NULL ? hwClockRate() : 1.0) * (1.0 + offset);
    SDL_SetAudioStreamFrequencyRatio(stream, (float)ratio);
    int startFrames = targetFrames > deviceFrames ? targetFrames : deviceFrames;
    if (prefilling && queued + (int)count >= startFrames) {
        prefilling = 0;
        SDL_ResumeAudioStreamDevice(stream);
    }
    pushed += count;
    if (logStats && pushed >= nextReport) {
        nextReport = pushed + 5 * SPU_RATE;
        fprintf(stderr, "spu: out queue %d frames (average %.0f, target %d%s), speed %+.3f%% (drift %+.3f%%), %d dry, %d cut, %d starved\n",
                queued, smoothed, targetFrames, boostFrames ? " grown" : "", (ratio - 1) * 100, offset * 100, dryPushes, cuts, SDL_GetAtomicInt(&starved));
    }
}

/* Skip what an underrun made late (owed; the file's comment): the oldest
 * first, the batch not yet in the stream, then the arrival's. What is kept
 * comes back up from the device's silence. */
static void payOwed(const int16_t** frames, uint32_t* count) {
    int owe = SDL_GetAtomicInt(&owed);
    if (owe <= 0) {
        return;
    }
    uint32_t fromPending = (uint32_t)owe < pendingFrames ? (uint32_t)owe : pendingFrames;
    memmove(pending[0], pending[fromPending], (pendingFrames - fromPending) * sizeof(pending[0]));
    pendingFrames -= fromPending;
    uint32_t fromArrival = (uint32_t)owe - fromPending < *count ? (uint32_t)owe - fromPending : *count;
    *frames += 2 * fromArrival;
    *count -= fromArrival;
    SDL_AddAtomicInt(&owed, -(int)(fromPending + fromArrival));
    fadeIn = FADE_IN_FRAMES;
}

/* hw.c took a stall out of the clock: the game does not catch it up, so
 * nothing that comes is late. The device played the queue through the stall
 * (then silence): it is put back in silence, the sound fading in behind it,
 * as far behind the picture as before (the file's comment). */
void spuOutputClockGap(void) {
    SDL_SetAtomicInt(&owed, 0);
    if (stream == NULL || paused || prefilling) {
        return;
    }
    int queued = SDL_GetAudioStreamQueued(stream) / (int)(sizeof(int16_t) * 2);
    if (queued > 0) {
        flushPending(); /* still playing: the batch follows it */
    } else {
        pendingFrames = 0; /* their time passed in the device's silence */
    }
    queued = SDL_GetAudioStreamQueued(stream) / (int)(sizeof(int16_t) * 2);
    static const int16_t silence[OUTPUT_FRAMES][2];
    int startFrames = targetFrames > deviceFrames ? targetFrames : deviceFrames;
    for (int n = startFrames - queued; n > 0; n -= OUTPUT_FRAMES) {
        putFrames(&silence[0][0], (uint32_t)(n < OUTPUT_FRAMES ? n : OUTPUT_FRAMES));
    }
    fadeIn = FADE_IN_FRAMES;
    smoothed = targetFrames;
}

void spuOutputPush(const int16_t* frames, uint32_t count) {
    if (!opened) openOutput();
    if (dump != NULL) {
        /* Native PCM remains independent of host batching and playback. */
        fwrite(frames, sizeof(int16_t) * 2, count, dump);
        dumped += count;
        unflushed += count;
        if (unflushed >= 4096) {
            fflush(dump);
            unflushed = 0;
        }
    }
    if (stream == NULL || count == 0) return;
    payOwed(&frames, &count);
    if (count > (uint32_t)targetFrames * OVERFLOW_FACTOR) {
        /* Lossy for the host only (the file's comment): the dump above has
         * every frame. */
        frames += 2 * (count - (uint32_t)targetFrames);
        count = (uint32_t)targetFrames;
        ++cuts;
    }
    while (count > 0) {
        uint32_t n = OUTPUT_FRAMES - pendingFrames;
        if (n > count) n = count;
        memcpy(pending[pendingFrames], frames, n * sizeof(pending[0]));
        pendingFrames += n;
        frames += n * 2;
        count -= n;
        if (pendingFrames == OUTPUT_FRAMES) flushPending();
    }
}

/* ---- save states (system/savestate.h) ---- */

/* The dump's length in frames: a state records it. */
uint64_t spuOutputDumpFrames(void) {
    return dumped;
}

/* After a load: what the device has queued belongs to the timeline left
 * behind (dropped; the sound comes up from silence). The dump goes on from
 * where it stood when the state was saved, so a run that loads and one
 * that never did have the same dump from there on (a later timeline is cut
 * off; after a restart the gap before it reads as silence). */
void spuOutputStateLoaded(uint64_t dumpFrames) {
    if (!opened) {
        openOutput();
    }
    if (stream != NULL) {
        pendingFrames = 0;
        prefillOutput();
        SDL_ClearAudioStream(stream);
        fadeIn = FADE_IN_FRAMES;
        smoothed = targetFrames;
    }
    if (dump != NULL) {
        long long at = (long long)dumpFrames * (long long)(sizeof(int16_t) * 2);
        fflush(dump);
#ifdef _WIN32
        if (dumpFrames < dumped) {
            _chsize_s(_fileno(dump), at);
        }
        _fseeki64(dump, at, SEEK_SET);
#else
        if (dumpFrames < dumped && ftruncate(fileno(dump), (off_t)at) != 0) {
            fprintf(stderr, "spu: cannot cut XG_AUDIO_DUMP back\n");
        }
        fseeko(dump, (off_t)at, SEEK_SET);
#endif
        dumped = dumpFrames;
        unflushed = 0;
    }
}
