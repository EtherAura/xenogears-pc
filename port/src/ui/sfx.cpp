// The settings menu's sounds (sfx.h): the game's menu sounds, rendered from
// its sound effect bank and samples on the player's disc by the SPU's voices
// (port/src/spu/menusfx.c) when the menu first opens, and played on an audio
// stream of their own. The game's sound stays paused and untouched: the
// sounds are not the game's (XG_AUDIO_DUMP has none of them).

#include "sfx.h"

#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>

#include "assets.h"

extern "C" {
#include "spu/menusfx.h"
#include "system/settings.h"
}

namespace sfx {

namespace {

#define RATE 44100
#define MAX_FRAMES (RATE * 2)

struct Pcm {
    int16_t* frames = nullptr;
    int count = 0;
};

const Sound kSounds[] = {MOVE, CHOOSE, BACK, BUZZ, OPEN, CLOSE};
#define SOUND_COUNT (int)(sizeof(kSounds) / sizeof(kSounds[0]))

struct State {
    bool tried;
    unsigned plays;
    SDL_AudioStream* stream;
    Pcm sounds[SOUND_COUNT];
};

State st;

int slot(Sound s) {
    for (int i = 0; i < SOUND_COUNT; i++) {
        if (kSounds[i] == s) return i;
    }
    return -1;
}

// The field's sound effect bank (directory 4, file 0xA8, as the field loads
// it into fieldSeq for its menu) and the resident sample bank it plays from
// (directory 1, file 2: bank 0, at 0x1010 in SPU RAM).
void prepare() {
    st.tried = true;
    int sedsSize = 0, wdsSize = 0;
    uint8_t* seds = assets::readDiscFile(4, 0xA8, &sedsSize);
    uint8_t* wds = assets::readDiscFile(1, 2, &wdsSize);
    int made = 0;
    for (int i = 0; seds != nullptr && wds != nullptr && i < SOUND_COUNT; i++) {
        int s = kSounds[i];
        Pcm& p = st.sounds[i];
        p.frames = (int16_t*)malloc(MAX_FRAMES * 4);
        p.count = p.frames != nullptr ? menuSfxRender(seds, sedsSize, wds, wdsSize, s, p.frames, MAX_FRAMES) : 0;
        made += p.count > 0;
        const char* dir = getenv("XG_UI_SOUND_DUMP"); // the rendered sounds, raw s16 stereo 44.1 kHz
        if (dir != nullptr && p.count > 0) {
            char path[1024];
            snprintf(path, sizeof(path), "%s/menu_sound_%d.raw", dir, s);
            FILE* f = fopen(path, "wb");
            if (f != nullptr) {
                fwrite(p.frames, 4, (size_t)p.count, f);
                fclose(f);
            }
        }
    }
    free(seds);
    free(wds);
    if (made > 0) {
        SDL_AudioSpec spec = {SDL_AUDIO_S16, 2, RATE};
        if (SDL_WasInit(SDL_INIT_AUDIO) || SDL_InitSubSystem(SDL_INIT_AUDIO)) {
            st.stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, NULL, NULL);
        }
        if (st.stream != nullptr) {
            SDL_ResumeAudioStreamDevice(st.stream);
        }
    }
    fprintf(stderr, "ui: the menu's sounds: %d of %d from the disc%s\n", made, SOUND_COUNT,
            made > 0 && st.stream == nullptr ? ", but no audio device" : "");
}

} // namespace

void opened() {
    bool none = true;
    for (const Pcm& p : st.sounds) none &= p.count == 0;
    if (!st.tried || (none && st.stream == nullptr)) {
        prepare(); // (again: the first opening had no disc in the drive, the setup screen)
    }
    if (st.stream != nullptr) {
        // The game's volume setting, as its own output takes it (spu/output.c).
        int volume = settingsInt("volume", 100);
        volume = volume < 0 ? 0 : volume > 100 ? 100 : volume;
        SDL_SetAudioStreamGain(st.stream, (float)(volume * volume) / 10000.0f);
    }
}

unsigned plays() {
    return st.plays;
}

void play(Sound s) {
    st.plays++;
    if (getenv("XG_UI_SOUND_LOG") != nullptr) {
        fprintf(stderr, "ui: sound %#x\n", (unsigned)s); // (with XG_UI_SCRIPT shots: which frame)
    }
    int i = slot(s);
    if (s == CLOSE && i >= 0 && st.sounds[i].count == 0) {
        i = slot(BACK); // what the button the game goes back with plays first (updateMenuInput)
    }
    if (st.stream == nullptr || i < 0 || st.sounds[i].count == 0) {
        return;
    }
    // One sound at a time, from its start, as the game's menu plays them on
    // one pair of voices.
    SDL_ClearAudioStream(st.stream);
    SDL_PutAudioStreamData(st.stream, st.sounds[i].frames, st.sounds[i].count * 4);
}

} // namespace sfx
