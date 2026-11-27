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

extern sBattleMechaInitData* battleMechaInitData;
extern u8 needBattleEventOverlay;
extern u8 battleEventMenuEnabled;
extern u8 battleEventMoviePending;
extern u8 battleInitVar0;

/* Some bosses were staged for one Gear and give every player slot the
 * same coordinates. Spread that party across the opening camera's view,
 * within the arena bounds. Change the spawn table so return moves use the
 * same positions as the loader. */
void pcGauntletSpaceGears(void) {
    sBattleMechaInitData* stage;
    Vec2s16* positions[3];
    s32 count = 0, overlap = 0, i, j;
    s32 alongZ, min, max, margin, gap, span, start;
    s32 x, z, dx, dz;

    if (pcGauntletState() != 2) return;
    stage = battleMechaInitData;
    for (i = 0; i < 3; i++) {
        sBattleVisualEntity* member = &battleBssBlock.m4_visualEntities[i];
        if ((member->m2_characterId & 0x7F) == 0x7F || member->m4_isGear == 0) continue;
        Vec2s16* pos = &stage->m100_gearPositions[i][0];
        for (j = 0; j < count; j++) {
            if (pos->vx == positions[j]->vx && pos->vy == positions[j]->vy) overlap = 1;
        }
        positions[count++] = pos;
    }
    if (!overlap) return;

    x = positions[0]->vx;
    z = positions[0]->vy;
    dx = stage->m47C_cameraPresets[0].m6_eye[0] - stage->m47C_cameraPresets[0].m0_at[0];
    dz = stage->m47C_cameraPresets[0].m6_eye[2] - stage->m47C_cameraPresets[0].m0_at[2];
    alongZ = (dx < 0 ? -dx : dx) > (dz < 0 ? -dz : dz);
    min = alongZ ? stage->m350_minZ : stage->m34E_minX;
    max = alongZ ? stage->m352_maxZ : stage->m34C_maxX;
    if (max <= min) {
        min = -32768;
        max = 32767;
    }
    margin = (max - min) / 8;
    if (margin > 128) margin = 128;
    min += margin;
    max -= margin;
    gap = 640 * stage->m344_mechaScale / 4096;
    if (gap <= 0) gap = 640;
    if (gap > (max - min) / (count - 1)) gap = (max - min) / (count - 1);
    span = gap * (count - 1);
    start = (alongZ ? z : x) - span / 2;
    if (start < min) start = min;
    if (start > max - span) start = max - span;
    for (i = 0; i < count; i++) {
        /* Lead in the centre of a three-Gear party. */
        s32 lane = count == 3 && i < 2 ? 1 - i : i;
        positions[i]->vx = alongZ ? x : start + lane * gap;
        positions[i]->vy = alongZ ? start + lane * gap : z;
    }
}

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
