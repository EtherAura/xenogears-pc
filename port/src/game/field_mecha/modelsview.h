#ifndef PORT_GAME_FIELD_MECHA_MODELSVIEW_H
#define PORT_GAME_FIELD_MECHA_MODELSVIEW_H

/*
 * The model viewer's way into the Gear overlay (modelsview.c, PC only):
 * compiled into the field_mecha module, so it reaches the overlay's own state
 * and main's, for port/src/extras/models.c, which calls it inside an extras
 * sandbox (sandbox.h) whose world holds field_mecha. Never called by the game.
 */

#include "common.h"

/* The sound driver's state as the Gear VM reads it (op 0x3C
 * fadeSoundEffectVolume walks the effect instances; getMechaSoundEffectIdBase
 * reads the battle effect sequence): empty instances and a sequence of id 0,
 * since the sandbox never runs initSoundSystem (BIOS events, root counters). */
s32 modelsViewSoundStubs(void); /* 0: the heap is full */

/* Animation tracks in use in the overlay's pool (mechaInitVar2). */
s32 modelsViewTracksInUse(void);

#endif /* PORT_GAME_FIELD_MECHA_MODELSVIEW_H */
