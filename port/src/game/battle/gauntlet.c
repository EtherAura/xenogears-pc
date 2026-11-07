/*
 * Gauntlets: a battle's outcome, before finishBattle (battleMain), for
 * port/src/game/main/gauntlet.c: how it ended (battleRunningVar1), a wipe
 * a battle event turns into the story's outcome, a chain the battle event
 * asked for (battle event op 0x24 sets battleInitVar0), and who was knocked
 * out. A gauntlet never leaves a battle for its battle event's movie: the
 * fight's story stays where it was.
 */
#include "common.h"
#include "battle/battle.h"
#include "pc_gameplay.h"

extern u8 needBattleEventOverlay;
extern u8 battleEventMenuEnabled;
extern u8 battleEventMoviePending;
extern u8 battleInitVar0;

void pcGauntletBattleOverHook(void) {
    u32 knocked = 0;
    s32 i;

    if (pcGauntletState() != 2) {
        return;
    }
    for (i = 0; i < 3; i++) {
        if (battleCharacters[i] < 11 && (battleEntities.m0_entities[i].m0_base.m7C_statusFlags[0] & 0xC000)) {
            knocked |= 1 << battleCharacters[i];
        }
    }
    if (pcGauntletOver(performAttackSub3_var0.m2_battleRunningVar1, needBattleEventOverlay && battleEventMenuEnabled,
                       battleInitVar0 != 0, knocked)) {
        battleEventMoviePending = 0;
    }
}
