#ifndef PORT_EXTRAS_BATTLE_PREVIEW_H
#define PORT_EXTRAS_BATTLE_PREVIEW_H
#include "disc.h"
#include "vgpu.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct XgBattlePreview XgBattlePreview;
XgBattlePreview *xgBattlePreviewCreate(XgDisc *disc, int character, int arena, int enemySet, int gear,
                                       int enemy);
// kind: pose (0), party special (1), ability (2), Gear program (3), enemy
// animation (4), command entry (5: a party slot's attack, deathblow or Ether,
// a Gear's attack, deathblow, Infinity, Ether or special, as the battle picks
// its animation and approach).
int xgBattlePreviewPresentation(XgBattlePreview *, int slot, int full);
int xgBattlePreviewPlay(XgBattlePreview *preview, int slot, int target, int kind, int id, int program,
                        int full);
// An enemy turn: `count` battleScriptContext actions (8 bytes each, the target
// mask m6 filled in) run through executeBattleCode as the enemy's turn does.
int xgBattlePreviewTurn(XgBattlePreview *preview, int slot, int target, const unsigned char *actions,
                        int count, int full);
// out: character id, Gear, animation count, party special file, ability range
// (2), command loadout, Gear number (party slots; -1 otherwise).
int xgBattlePreviewInfo(XgBattlePreview *preview, int slot, int out[8]);
// A party slot's loaded command entries (38 on foot, 41 on a Gear): attack
// type and attack flags; the count, 0 for no party slot.
int xgBattlePreviewCommands(XgBattlePreview *preview, int slot, int types[41], int flags[41]);
void xgBattlePreviewDestroy(XgBattlePreview *preview);
int xgBattlePreviewStep(XgBattlePreview *preview, int ticks);
XgVgpu *xgBattlePreviewGpu(XgBattlePreview *preview);
int xgBattlePreviewScreenY(const XgBattlePreview *preview);
int xgBattlePreviewFailed(const XgBattlePreview *);
// The move's native sequence has not ended yet (its frames may still be queued).
int xgBattlePreviewActive(XgBattlePreview *);
void xgBattlePreviewDump(XgBattlePreview *);
void xgBattlePreviewAudio(XgBattlePreview *preview, int audible);
#ifdef __cplusplus
}
#endif
#endif
