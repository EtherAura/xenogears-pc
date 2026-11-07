/* Exercise the host queue through a real SDL conversion stream, with device
 * pause/resume observed locally. Physical-device scheduling is covered by
 * the real-time opening-crawl run; this test must not play sound. */
#define _POSIX_C_SOURCE 200809L
#include <SDL3/SDL.h>
static int devicePaused, resumes;
static float playbackRatio;
static bool testPause(SDL_AudioStream* s) { (void)s; devicePaused = 1; return true; }
static bool testResume(SDL_AudioStream* s) { (void)s; devicePaused = 0; resumes++; return true; }
/* Inspect queued PCM before the device's intentional drift resampling. */
static bool testRatio(SDL_AudioStream* s, float ratio) { (void)s; playbackRatio = ratio; return true; }
#define SDL_PauseAudioStreamDevice testPause
#define SDL_ResumeAudioStreamDevice testResume
#define SDL_SetAudioStreamFrequencyRatio testRatio
#include "../output.c"
#undef SDL_PauseAudioStreamDevice
#undef SDL_ResumeAudioStreamDevice
#undef SDL_SetAudioStreamFrequencyRatio
#include "test/check.h"
static int checks;
/* hw.c's virtual clock rate (the display lock's), which the stream follows. */
static double testClockRate = 1;
double hwClockRate(void) { return testClockRate; }

int main(void) {
    SDL_setenv_unsafe("XG_SETTINGS", "none", 1);
    SDL_AudioSpec spec = {SDL_AUDIO_S16, 2, SPU_RATE};
    stream = SDL_CreateAudioStream(&spec, &spec);
    CHECK(stream);
    opened = 1;
    targetFrames = 512;
    deviceFrames = 256;
    dump = tmpfile();
    CHECK(dump);
    prefillOutput();
    int16_t source[1152 * 2], got[1152 * 2];
    for (int i = 0; i < 1152 * 2; i++) source[i] = (int16_t)(i * 17 - 20000);
    for (int i = 0; i < 511; i++) spuOutputPush(source + 2 * i, 1);
    CHECK(devicePaused && !resumes && prefilling);
    CHECK(SDL_GetAudioStreamQueued(stream) == 384 * 4);
    spuOutputPush(source + 1022, 1);
    CHECK(!devicePaused && resumes == 1 && !prefilling);
    CHECK(SDL_GetAudioStreamData(stream, got, 512 * 4) == 512 * 4);
    CHECK(!memcmp(source, got, 512 * 4));

    /* A large arrival and its small tail preserve order and every sample. */
    spuOutputPush(source + 1024, 549);
    CHECK(SDL_GetAudioStreamQueued(stream) == 512 * 4 && pendingFrames == 37);
    spuOutputPush(source + 2122, 91);
    CHECK(SDL_GetAudioStreamData(stream, got, 640 * 4) == 640 * 4);
    CHECK(!memcmp(source + 1024, got, 640 * 4));
    CHECK(dumped == 1152 && !pendingFrames);
    CHECK(playbackRatio >= 0.995f && playbackRatio <= 1.005f);

    /* Loading drops an incomplete host block and cuts the raw dump at the
     * saved native sample, then fades only playback, never the dump. */
    spuOutputPush(source, 47);
    CHECK(pendingFrames == 47);
    spuOutputStateLoaded(600);
    CHECK(!pendingFrames && !SDL_GetAudioStreamQueued(stream));
    CHECK(devicePaused && prefilling && dumped == 600);
    int16_t steady[512 * 2];
    for (int i = 0; i < 512 * 2; i++) steady[i] = 10000;
    spuOutputPush(steady, 512);
    CHECK(SDL_GetAudioStreamData(stream, got, 512 * 4) == 512 * 4);
    CHECK(got[0] == 0 && got[880] > 9900 && got[882] == 10000 && got[1023] == 10000);
    fflush(dump);
    CHECK(ftell(dump) == 1112 * 4);
    rewind(dump);
    CHECK(fread(got, 4, 600, dump) == 600 && !memcmp(got, source, 600 * 4));
    CHECK(fread(got, 4, 512, dump) == 512 && !memcmp(got, steady, 512 * 4));
    CHECK(fgetc(dump) == EOF);
    fseek(dump, 0, SEEK_END);

    /* A pause includes the unsubmitted tail in its fade, and resume fills
     * the reserve again rather than playing each tiny arrival immediately. */
    spuOutputPush(steady, 17);
    spuOutputPause(1);
    CHECK(!pendingFrames && paused);
    CHECK(SDL_GetAudioStreamData(stream, got, 17 * 4) == 17 * 4);
    CHECK(got[0] > got[16] && got[16] > got[32] && got[32] == 0);
    spuOutputPause(0);
    CHECK(devicePaused && prefilling && fadeIn == FADE_IN_FRAMES);
    spuOutputPush(steady, 127);
    CHECK(devicePaused && pendingFrames == 127 && !SDL_GetAudioStreamQueued(stream));
    spuOutputStateLoaded(600);
    CHECK(!pendingFrames && dumped == 600);

    /* A latency shorter than the device's block still reserves that block.
     * Pausing during prefill must not leave old sound to play on resume. */
    deviceFrames = 1024;
    settingsSetInt("audio_latency", 1);
    settingsSetInt("audio_latency_max", 1); /* no growth in these scenarios */
    spuOutputApplySettings();
    CHECK(targetFrames == 1024);
    spuOutputPush(steady, 512);
    CHECK(devicePaused && prefilling);
    spuOutputPush(steady, 512);
    CHECK(!devicePaused && !prefilling);
    spuOutputStateLoaded(600);
    spuOutputPush(steady, 17);
    spuOutputPause(1);
    CHECK(!pendingFrames && !SDL_GetAudioStreamQueued(stream) && devicePaused);
    spuOutputPause(0);
    CHECK(prefilling && !paused);

    /* A pause shorter than the queue: the device is still playing the faded
     * tail at resume. It must not be paused under it (a click); the reserve
     * is silence behind the tail, then the fade-in, all in order. */
    static int16_t played[2048 * 2];
    spuOutputPush(steady, 512); spuOutputPush(steady, 512);
    CHECK(!devicePaused && !prefilling && SDL_GetAudioStreamQueued(stream) == 1024 * 4);
    CHECK(SDL_GetAudioStreamData(stream, played, 724 * 4) == 724 * 4); /* the device took most */
    int pausedEarly = resumes;
    spuOutputPause(1);
    CHECK(SDL_GetAudioStreamQueued(stream) == 300 * 4);
    CHECK(SDL_GetAudioStreamData(stream, played, 100 * 4) == 100 * 4); /* 100 of the tail played */
    spuOutputPause(0);
    CHECK(!devicePaused && !prefilling && !paused && fadeIn == FADE_IN_FRAMES && resumes == pausedEarly);
    CHECK(SDL_GetAudioStreamQueued(stream) == 1024 * 4); /* tail + silence: the full reserve */
    spuOutputPush(steady, 512);
    CHECK(!devicePaused && !pendingFrames && SDL_GetAudioStreamQueued(stream) == 1536 * 4);
    CHECK(SDL_GetAudioStreamData(stream, played, 100 * 4) == 100 * 4);
    for (int i = 1; i < 100; i++) CHECK(played[i * 2] < played[i * 2 - 2]); /* still fading */
    CHECK(played[99 * 2] > 0);
    /* A second pause right away fades what the stream holds (the faded tail
     * and the silence), continuing from the sample that played last. */
    int16_t last = played[99 * 2];
    spuOutputPause(1);
    int left = SDL_GetAudioStreamQueued(stream) / 4;
    CHECK(left == 1436 && SDL_GetAudioStreamData(stream, played, left * 4) == left * 4);
    CHECK(played[0] < last && last - played[0] < 100);
    for (int i = 100; i < 924; i++) CHECK(played[i * 2] == 0 && played[i * 2 + 1] == 0);
    CHECK(played[(left - 1) * 2] == 0);
    spuOutputPause(0);
    CHECK(devicePaused && prefilling); /* drained: an ordinary refill */
    /* Undisturbed, the same resume plays the whole tail, silence, then the
     * fade-in: none of the frames is lost or reordered. */
    spuOutputStateLoaded(600);
    spuOutputPush(steady, 512); spuOutputPush(steady, 512);
    CHECK(SDL_GetAudioStreamData(stream, played, 724 * 4) == 724 * 4);
    spuOutputPause(1); spuOutputPause(0);
    spuOutputPush(steady, 512);
    CHECK(SDL_GetAudioStreamData(stream, played, 1536 * 4) == 1536 * 4);
    for (int i = 1; i < 300; i++) CHECK(played[i * 2] <= played[i * 2 - 2]);
    CHECK(played[299 * 2] == 0);
    for (int i = 300; i < 1024; i++) CHECK(played[i * 2] == 0);
    CHECK(played[1024 * 2] == 0 && played[(1024 + FADE_IN_FRAMES) * 2] == 10000);

    /* An underrun: the device asked for 300 frames more than the stream had
     * (SDL pads them with silence). The next 300 frames are that late and
     * skipped, the oldest first (the batch not yet in the stream), and what
     * is kept fades in from the silence. */
    SDL_SetAudioStreamGetCallback(stream, onDeviceGet, NULL);
    spuOutputStateLoaded(600);
    spuOutputPush(steady, 512); spuOutputPush(steady, 512);
    CHECK(!prefilling && SDL_GetAudioStreamQueued(stream) == 1024 * 4 && !SDL_GetAtomicInt(&owed));
    spuOutputPush(steady, 50);
    CHECK(pendingFrames == 50);
    int starvedBefore = SDL_GetAtomicInt(&starved);
    CHECK(SDL_GetAudioStreamData(stream, played, 1324 * 4) == 1024 * 4);
    CHECK(SDL_GetAtomicInt(&starved) == starvedBefore + 1 && SDL_GetAtomicInt(&owed) == 300);
    static int16_t ramp[512 * 2];
    for (int i = 0; i < 512; i++) ramp[i * 2] = ramp[i * 2 + 1] = (int16_t)(i + 1);
    spuOutputPush(ramp, 512);
    CHECK(!SDL_GetAtomicInt(&owed) && pendingFrames == 6 && SDL_GetAudioStreamQueued(stream) == 256 * 4);
    CHECK(SDL_GetAudioStreamData(stream, played, 256 * 4) == 256 * 4);
    CHECK(played[0] == 0 && played[1] == 0);
    for (int i = 1; i < 256; i++) {
        float g = 1.0f - (float)(FADE_IN_FRAMES - i) / FADE_IN_FRAMES;
        CHECK(played[i * 2] == (int16_t)((int16_t)(251 + i) * g)); /* frames 251.. of the ramp */
    }
    /* Neither a prefill nor a pause carries a debt: their reserve starts the
     * sound's time again. */
    CHECK(SDL_GetAudioStreamData(stream, played, 100 * 4) == 0 && SDL_GetAtomicInt(&owed) == 100);
    spuOutputPause(1);
    CHECK(!SDL_GetAtomicInt(&owed));
    spuOutputPause(0);
    spuOutputStateLoaded(600);

    /* A stall hw.c took out of the clock: nothing comes late, and the queue
     * the device played through it is put back in silence before the sound
     * fades in again. */
    spuOutputPush(steady, 512); spuOutputPush(steady, 512); spuOutputPush(steady, 50);
    CHECK(SDL_GetAudioStreamData(stream, played, 1100 * 4) == 1024 * 4 && SDL_GetAtomicInt(&owed) == 76);
    CHECK(pendingFrames == 50);
    spuOutputClockGap();
    CHECK(!SDL_GetAtomicInt(&owed) && !pendingFrames && fadeIn == FADE_IN_FRAMES);
    CHECK(SDL_GetAudioStreamQueued(stream) == 1024 * 4);
    spuOutputPush(steady, 128);
    CHECK(SDL_GetAudioStreamData(stream, played, 1152 * 4) == 1152 * 4);
    for (int i = 0; i < 1024; i++) CHECK(played[i * 2] == 0);
    CHECK(played[1025 * 2] > 0 && played[1151 * 2] < 10000);

    /* The stream plays at the virtual clock's rate (the display lock's), the
     * drift correction on top: at the target it adds nothing. */
    testClockRate = 1.00291;
    spuOutputStateLoaded(600);
    spuOutputPush(steady, 896);
    smoothed = targetFrames;
    spuOutputPush(steady, 128);
    CHECK(playbackRatio == (float)1.00291);
    testClockRate = 1;

    /* Underruns grow the target by 20 ms each up to audio_latency_max; half a
     * minute without one gives 10 ms back. */
    settingsSetInt("audio_latency_max", 100);
    spuOutputApplySettings();
    CHECK(targetFrames == 1024 && maxFrames == SPU_RATE / 10);
    spuOutputStateLoaded(600);
    int base = targetFrames;
    SDL_AddAtomicInt(&starved, 1); spuOutputPush(steady, 128);
    CHECK(targetFrames == base + SPU_RATE * 20 / 1000);
    SDL_AddAtomicInt(&starved, 1); spuOutputPush(steady, 128);
    CHECK(targetFrames == base + SPU_RATE * 40 / 1000);
    quietSince = pushed - (uint64_t)SPU_RATE * 31; spuOutputPush(steady, 128);
    CHECK(targetFrames == base + SPU_RATE * 30 / 1000);
    settingsSetInt("audio_latency_max", 1);
    spuOutputApplySettings();
    CHECK(targetFrames == 1024);

    fclose(dump);
    SDL_DestroyAudioStream(stream);
    SDL_Quit();
    printf("spu_outputtest: %d checks, 0 failures\n", checks);
    return 0;
}
