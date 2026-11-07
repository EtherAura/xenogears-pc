#ifndef PORT_GAME_BATTLE_MODELSVIEW_H
#define PORT_GAME_BATTLE_MODELSVIEW_H

/*
 * The model viewer's way into the battle's copy of the Gear code
 * (modelsview.c, PC only): compiled into the battle module, for
 * port/src/extras/models.c, which calls it inside an extras sandbox whose
 * world holds the battle module. Never called by the game.
 *
 * A battle world of the least the battle's Gear code reads: a battle config
 * (main's battleEnvMechaInitData, which a battle takes from its arena's file) of zeros
 * with the Gear scale, the pool sizes and the colours set, the colour matrix,
 * the track and trail pools (mechaInitForBattle), no terrain, no camera
 * script, no damage, the sound driver off (its effects ask musicStatusFlag).
 */

#include "common.h"
#include "psyq.h"

/* The world; 0 if the heap is full. */
s32 modelsBattleInit(MATRIX* colour, s32 backR, s32 backG, s32 backB);

/* A Gear or enemy into battle slot `slot` (mechaInitEnvironmentMechaMesh,
 * flags 4: the animation file converted without loading its sound sequence
 * into the driver). Its sLoadedMechas, NULL on failure. */
void* modelsBattleLoad(s32 slot, void* anim, void* model, s16 tpageX, s16 tpageY, s16 clutX, s16 clutY);

/* The extra animation file op 0x04 would read for (operand, parameter):
 * file `*file` of directory 0x2A (the battle's own table). */
void modelsBattleExtraFile(s32 operand, s32 param, s32* file);
/* Give the mecha in `slot` an extra animation file as ops 0x04 and 0x05
 * would (the file kept, its tables converted, its textures and sprite
 * frames uploaded), without loading its sound sequence. 1 on success. */
s32 modelsBattleSetExtra(s32 slot, void* file);

/* mechaPlayAnimation_battle. */
void modelsBattlePlay(s32 slot, s32 animation);

/* The battle's renderMechasBattle into ot: a VM step for every mecha with
 * step 1 (as a frame of two vblanks), the current pose with step 0. */
void modelsBattleRender(MATRIX* view, MATRIX* light, OTag* ot, s32 oddOrEven, s32 step);

/* The mecha in slot (sLoadedMechas*), NULL if none; the track pool's tracks
 * in use. */
void* modelsBattleMecha(s32 slot);
/* Native split opcode calls Created even when heap addresses are reused. */
void modelsBattleRigCreated(s32 slot);
u32 modelsBattleRigGeneration(s32 slot);
s32 modelsBattleTracksInUse(void);

#endif /* PORT_GAME_BATTLE_MODELSVIEW_H */
