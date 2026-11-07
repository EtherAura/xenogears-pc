/*
 * Movies played by the game's own video player (gallery.h XgMovie): the
 * video_player overlay's STR player in a code sandbox, a world of its own
 * (clock, interrupts, SPU), a drive of its own with the disc image and an
 * MDEC of its own.
 *
 * The player is driven as the game's movie loops drive it: three updates
 * (updateStrPlayer) at the start of each vertical blank, then the blank's
 * time passes in the world, the drive delivering its sectors (video into the
 * player's ring, XA audio into the SPU's CD input) and the MDEC its slices
 * (the player's callback loads them into VRAM). The sound driver's tick is
 * not running here; its CD volume fade, which the player starts with
 * setCdVolume, is stepped as audioTick steps it (every second 240 Hz tick).
 */

#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gallery.h"

#include "cd/cd.h"
#include "common.h"
#include "main/filesystem.h"
#include "main/sound.h"
#include "mdec/mdec.h"
#include "psyq/libspu.h"
#include "sandbox.h"
#include "system/hw.h"
#include "system/settings.h"
#include "system/world.h"

/* src/video_player/vp_801D30C4.c (the overlay's STR player) */
s32 initStrPlayer(u16 width, u16 height, u16 depth, u16 sliceWidth, u16 ringSize, u16 vlcMode, u16 mode);
void startStrPlayback(s32 fileIndex, s32 offset, u16 startFrame, u16 endFrame, u16 channel, s32 flags, u16 audio,
                      u16 x0, u16 y0, u16 x1, u16 y1, u16 maxHeight, void (*callback)(u16, u16, u16));
void updateStrPlayer(void);
void stopStrPlayback(void);
void freeStrPlayer(void);
extern s8 strPlayerState; /* the player's completion state, generated module BSS */

/* src/main */
extern u8* fatFileTableBuffer;
extern u16* fatDirectoryTableBuffer;
extern sptr cdromFnd;
void interpolateAudioParam(AudioParam* a);
void SpuSetCommonAttr(SpuCommonAttr* attr);

#define FRAME_W 320
#define FRAME_H 240
/* The NTSC vertical blank of the movie's 240-line display (system/hw.h). */
#define VSYNC_PERIOD (HW_FIELD_LINES_NTSC * HW_LINE_CLOCKS_NTSC / HW_VIDEO_HZ_NTSC)
/* The two frame buffers the player alternates between (VRAM rows). */
#define BUFFER0_Y 0
#define BUFFER1_Y 256

struct XgMovie {
    XgSandbox* sb;
    HwWorld* world;
    CdWorld* cd;
    MdecWorld* mdec;
    int started;  /* the player has buffers (freeStrPlayer to do) */
    int playing;
    int last;
    int frame;    /* the newest whole frame and where it is */
    int frameX, frameY;
    int width, height;
    int sinceFrame; /* vertical blanks since the last frame came */
    int audible;
    int recording;
    int16_t* rec;
    size_t recFrames, recCap;
};

static XgMovie* current; /* the movie inside (its callbacks find it) */

static void enter(XgMovie* m) {
    xgSandboxEnter(m->sb);
    hwWorldEnter(m->world);
    cdWorldSwap(m->cd);
    mdecWorldSwap(m->mdec);
    current = m;
}

static void leave(XgMovie* m) {
    current = NULL;
    mdecWorldSwap(m->mdec);
    cdWorldSwap(m->cd);
    hwWorldLeave(m->world);
    xgSandboxLeave(m->sb);
}

/* ---- the movie's audio stream ---------------------------------------------------------------- */

static SDL_AudioStream* stream;
static int streamTried;

static SDL_AudioStream* openStream(void) {
    if (!streamTried) {
        streamTried = 1;
        SDL_AudioSpec spec = {SDL_AUDIO_S16, 2, XG_MOVIE_RATE};
        if (SDL_WasInit(SDL_INIT_AUDIO) || SDL_InitSubSystem(SDL_INIT_AUDIO)) {
            stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, NULL, NULL);
        }
        if (stream != NULL) {
            SDL_ResumeAudioStreamDevice(stream);
        } else {
            fprintf(stderr, "extras: gallery: no audio device: %s\n", SDL_GetError());
        }
    }
    return stream;
}

double xgMovieAudioQueued(void) {
    return stream != NULL ? SDL_GetAudioStreamQueued(stream) / (4.0 * XG_MOVIE_RATE) : -1.0;
}

void xgMovieAudioStop(void) {
    if (stream != NULL) {
        SDL_ClearAudioStream(stream);
    }
}

/* The world's SPU output (44.1 kHz stereo). */
static void sink(void* user, const int16_t* frames, uint32_t count) {
    XgMovie* m = user;
    if (m->audible && openStream() != NULL) {
        int volume = settingsInt("volume", 100);
        volume = volume < 0 ? 0 : volume > 100 ? 100 : volume;
        SDL_SetAudioStreamGain(stream, (float)(volume * volume) / 10000.0f);
        SDL_PutAudioStreamData(stream, frames, (int)count * 4);
    }
    if (m->recording) {
        if (m->recFrames + count > m->recCap) {
            size_t cap = (m->recFrames + count) * 2;
            int16_t* p = realloc(m->rec, cap * 4);
            if (p == NULL) {
                m->recording = 0;
                return;
            }
            m->rec = p;
            m->recCap = cap;
        }
        memcpy(m->rec + m->recFrames * 2, frames, (size_t)count * 4);
        m->recFrames += count;
    }
}

/* ---- the player -------------------------------------------------------------------------------- */

/* The player's callback when a whole frame is in VRAM (handleMovieFrame's
 * place in the game): its number, x (pixels) and y. */
static void onFrame(u16 frame, u16 x, u16 y) {
    XgMovie* m = current;
    if (m == NULL) {
        return;
    }
    m->frame = frame;
    m->frameX = x;
    m->frameY = y;
    m->sinceFrame = 0;
}

XgMovie* xgMovieCreate(XgDisc* disc, char* why, int whySize) {
    static const char* const modules[] = {"video_player", NULL};
    XgMovie* m = calloc(1, sizeof(XgMovie));
    uint8_t* table = xgDiscReadSectors(disc, 0x18, 16);
    uint8_t* dirs = xgDiscReadSectors(disc, 0x28, 1);
    if (m != NULL) {
        m->sb = xgSandboxCreate(modules, 8u << 20);
        m->world = hwWorldCreate();
        m->cd = cdWorldCreate(xgDiscPath(disc));
        m->mdec = mdecWorldCreate();
    }
    if (m == NULL || m->sb == NULL || m->world == NULL || m->cd == NULL || m->mdec == NULL || table == NULL ||
        dirs == NULL) {
        snprintf(why, (size_t)whySize, m != NULL && m->cd == NULL ? "cannot read the disc image" : "out of memory");
        free(table);
        free(dirs);
        xgMovieDestroy(m);
        return NULL;
    }
    enter(m);
    /* The file system as initCDAndFileSystem leaves it (without its reads):
     * the player finds the streams with the game's own lookups. */
    fatFileTableBuffer = xgSandboxCopy(table, 16 * 2048);
    fatDirectoryTableBuffer = xgSandboxCopy(dirs, 2048);
    cdromFnd = 0;
    cdWorldSpuInput(); /* the SPU's CD input, as CdInit sets it */
    leave(m);
    free(table);
    free(dirs);
    return m;
}

void xgMovieDestroy(XgMovie* m) {
    if (m == NULL) {
        return;
    }
    if (m->sb != NULL && m->world != NULL && m->cd != NULL && m->mdec != NULL && m->started) {
        enter(m);
        freeStrPlayer();
        leave(m);
    }
    mdecWorldDestroy(m->mdec);
    cdWorldDestroy(m->cd);
    hwWorldDestroy(m->world);
    xgSandboxDestroy(m->sb);
    free(m->rec);
    free(m);
}

int xgMovieStart(XgMovie* m, int streamNo, int offset, int first, int last, int width, int height) {
    int ok = 1;
    enter(m);
    if (m->started) {
        freeStrPlayer(); /* stops the drive and the MDEC too */
        m->started = 0;
    }
    m->frame = 0;
    m->playing = 0;
    m->sinceFrame = 0;
    m->width = width > 0 && width <= FRAME_W ? width : FRAME_W;
    m->height = height > 0 && height <= FRAME_H ? height : 224;
    /* As the movie mode sets it up (moviePlaybackLoop): whole slices (the
     * overlay's strSingleSliceMode stays 0, as loaded), 24-bit. */
    if (initStrPlayer(FRAME_W, FRAME_H, 0x80, 0x10, 0x20, 0x800, 1) != 0) {
        ok = 0;
    } else {
        m->started = 1;
        setCurrentDirectory(0x18, 1);
        /* XA on (flags 1) from file 1 channel 1; played once; the frames
         * alternate between two buffers. */
        startStrPlayback(streamNo + 2, offset, (u16)first, (u16)last, 1, 1, 0, 0, BUFFER0_Y, 0, BUFFER1_Y, 0xFFFF,
                         onFrame);
        m->playing = 1;
        m->last = last;
    }
    leave(m);
    return ok && m->playing;
}

void xgMovieStop(XgMovie* m) {
    if (m->started) {
        enter(m);
        stopStrPlayback();
        leave(m);
    }
    m->playing = 0;
}

int xgMoviePlaying(const XgMovie* m) {
    return m->playing;
}

/* What audioTick does for the CD volume on its every second tick. */
static void volumeTick(void) {
    if (g_spuVolumes.m3C_cd.m8_counter != 0) {
        interpolateAudioParam(&g_spuVolumes.m3C_cd);
        g_spuVolumes.m10_cdVolume.left = g_spuVolumes.m10_cdVolume.right = g_spuVolumes.m2A_cdVolume =
            g_spuVolumes.m3C_cd.m0_value >> 16;
        g_spuVolumes.m0_mask |= 0xC0;
    }
    if (g_spuVolumes.m0_mask != 0) {
        SpuSetCommonAttr((SpuCommonAttr*)&g_spuVolumes);
        g_spuVolumes.m0_mask = 0;
    }
}

void xgMovieRun(XgMovie* m, int vsyncs, int audible) {
    if (!m->playing || vsyncs <= 0) {
        return;
    }
    m->audible = audible;
    enter(m);
    hwWorldSetSink(sink, m);
    for (int v = 0; v < vsyncs && m->playing; v++) {
        for (int k = 0; k < 3; k++) {
            updateStrPlayer();
        }
        for (int half = 0; half < 2; half++) {
            volumeTick();
            hwWorldRun(VSYNC_PERIOD / 2);
        }
        /* The player stops itself after the last frame; a stream that ends
         * first (or a read that fails) brings no more frames. */
        if (strPlayerState < 0 || m->frame >= m->last || ++m->sinceFrame > 3 * 60) {
            m->playing = 0;
        }
    }
    hwWorldSetSink(NULL, NULL);
    leave(m);
}

int xgMovieFrame(XgMovie* m, uint32_t* rgba, int* w, int* h) {
    if (m->frame == 0) {
        return 0;
    }
    const uint16_t* vram = xgVgpuVram(xgSandboxGpu(m->sb));
    int x0 = m->frameX * 3 / 2; /* 24-bit: three bytes a pixel */
    for (int y = 0; y < m->height; y++) {
        const uint8_t* row = (const uint8_t*)(vram + (size_t)((m->frameY + y) & 511) * XG_VRAM_W + x0);
        uint32_t* out = rgba + (size_t)y * m->width;
        for (int x = 0; x < m->width; x++) {
            const uint8_t* p = row + x * 3;
            out[x] = (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | 0xFF000000u;
        }
    }
    *w = m->width;
    *h = m->height;
    return m->frame;
}

void xgMovieRecord(XgMovie* m, int on) {
    m->recording = on;
    if (on) {
        m->recFrames = 0;
    }
}

const int16_t* xgMovieRecorded(const XgMovie* m, int* frames) {
    *frames = (int)m->recFrames;
    return m->rec;
}
