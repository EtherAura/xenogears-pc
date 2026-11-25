#ifndef PORT_EXTRAS_AUDIOTAP_H
#define PORT_EXTRAS_AUDIOTAP_H

/*
 * The extras' audio streams (audiotap.c): the jukebox's, the gallery movie's
 * and the sound previews' (the bestiary's and the battle preview's) are each
 * opened here, not on the device directly.
 *
 * Normally each is SDL's stream on the default playback device, as before.
 * With XG_EXTRAS_AUDIO_DUMP set (a recording: tools/publish/reel.py's extras
 * clips with extras_sound) none reaches a device. Each is an SDL stream bound
 * to nothing, and the shell's loop plays them on the extras' own clock, one
 * frame's time a frame (xgTapFrame), mixing what they hold into that file as
 * raw s16 stereo at 44.1 kHz, from the first frame on (silence while nothing
 * plays). A device would drain the streams by the wall clock, and a 4K capture
 * runs far slower than real time; on the frame clock, an XG_EXTRAS_SCRIPT run
 * (whose viewers do the same work on every frame: the movie runs one vblank a
 * frame) gives the same sound on every run, in step with its pictures.
 */

#include <SDL3/SDL.h>

#ifdef __cplusplus
extern "C" {
#endif

/* A playback stream of `spec`, the device's (resumed) or a recording's; NULL
 * without either. Initialises SDL's audio when it is not yet. */
SDL_AudioStream* xgTapOpen(const SDL_AudioSpec* spec);
void xgTapClose(SDL_AudioStream* s);
/* The shell, once a frame: a recording plays `seconds` of every stream. */
void xgTapFrame(double seconds);

#ifdef __cplusplus
}
#endif

#endif
