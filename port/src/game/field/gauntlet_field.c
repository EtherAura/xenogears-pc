/*
 * Gauntlets: the first fight from a field (port/src/game/main/gauntlet.c).
 * fieldEntryPoint calls this where the player could open the menu (no menu
 * pending, the menu allowed, the player free to move); it starts the battle
 * as the field's random battles do (checkForRandomEncounter): the battle's
 * theme (op9DVar1.m10C_battleMusicId: the theme the boss's own field script
 * plays before its battle op, else the field's default battle theme 0x1D),
 * the battle overlay, then the field leaves for the battle on its next odd
 * frame. Later fights keep that theme (a chained battle keeps the music).
 */
#include "common.h"
#include "field/field.h"
#include "pc_gameplay.h"

extern FieldBss_800B2184 op9DVar1;
extern s32 playMusicAuthorized;
extern s32 bBattleSequenceInitialized;
extern s32 loadCompleted;
extern s32 fieldMusicLoadPending;
extern u8 fieldTransitionCompleted;
extern s32 mechaOverlayLoadingMode;
void loadGameModeOverlay(s32 mode);

void pcGauntletFieldTick(void) {
    s32 music;

    if (pcGauntletState() != 1 || playMusicAuthorized == 0 || loadCompleted == 1 || fieldMusicLoadPending == -1 ||
        fieldTransitionCompleted == 0 || bBattleSequenceInitialized) {
        return;
    }
    if (!pcGauntletStartBattle()) {
        return;
    }
    music = pcGauntletFightMusic(0);
    op9DVar1.m10C_battleMusicId = music >= 0 ? music : 0x1D; /* the boss theme the story plays, else the battle theme */
    if (mechaOverlayLoadingMode == 0) {
        loadGameModeOverlay(2);
    }
    playMusicAuthorized = 0;
    bBattleSequenceInitialized = 1;
}
