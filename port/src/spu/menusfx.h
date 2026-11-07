#ifndef PORT_SPU_MENUSFX_H
#define PORT_SPU_MENUSFX_H

/*
 * The settings menu's sounds (port/src/ui/sfx.cpp), rendered by menusfx.c:
 * sound effect `effect` of a SEDS bank as the game's sound driver plays it
 * (playSoundEffect), its samples from the WDS bank `wds`, through this SPU's
 * voices, into `out` (interleaved stereo s16 at 44.1 kHz, at most maxFrames).
 * Returns the frames written, 0 if the effect uses what this does not follow.
 *
 * The game's SPU and sound driver are not touched: the SPU's state is put
 * aside for the rendering and put back after it.
 */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int menuSfxRender(const uint8_t* seds, int sedsSize, const uint8_t* wds, int wdsSize, int effect, int16_t* out,
                  int maxFrames);

#ifdef __cplusplus
}
#endif

#endif /* PORT_SPU_MENUSFX_H */
