/*
 * The mouse pointing at a battle's targets (mouse.c, settings.ini
 * mouse_target), while the game shows its target arrows
 * (battleVar1->mC6_isTargetSelectionCursorVisible):
 *
 *   - choosing whom an ether, item or ability goes to (abilitySelectTarget):
 *     a click on a target moves the game's choice there with D-pad presses
 *     (a direction goes where selectNewSlotByDirection says, if that one is
 *     among abilityTargetBitmask) and confirms;
 *   - aiming an attack (the attack screen, battle menu 5: handleMenuSelectEnemy,
 *     battleVar2->m2E8_currentTarget, until the first attack button): a click
 *     on another target aims there (D-pad presses only: the attack is the
 *     player's choice of buttons); a click on the aimed one is the confirm
 *     button, as a click is without this setting (the attack the confirm
 *     button makes there).
 * A click beside the targets does nothing. The confirm goes only if the
 * game's choice is then the target clicked (mouseBattleCheck).
 *
 * Where the targets are on the screen: the battle hands over each sprite it
 * draws (battleSpriteRender: pcMouseBattleSprite) with the GTE's matrices set
 * for it, and it is projected here as the game projects it, its feet and its
 * head (m36_height above them, the height the game's spinning target marker
 * goes over); the hit box is that, 12 px wider on either side than a quarter
 * of its height, and a little below the feet. A target not drawn as a sprite
 * (a 3D model) is not clickable.
 */

#include "common.h"
#include "battle/battle.h"
#include "main/sprite.h"
#include "pc_wide.h"
#include "gte_pc.h"
#include "mouse.h"
#include "savestate.h"

extern u8 targetsPerPriority[12];
extern u16 abilityTargetBitmask;
extern u8 abilitySelectedTarget;
u8 selectNewSlotByDirection(u8 inputSlot, u8 direction);
u16 bitmaskCharacterCheck(u16 mask, u8 id);
u32 hwVSyncCount(void);

/* The D-pad bits of battleInputButton's directions 0..3 (Right, Down, Left, Up). */
static const unsigned short directionBits[4] = {0x2000, 0x4000, 0x8000, 0x1000};

static struct {
    const void* actor;
    short x, feet, head;
    u32 vblank;
} seen[16];

/* 1: aiming an attack; 2: choosing an ability's target; 0: neither. */
static int choosing(void) {
    if (pcWideGetMode() != PC_WIDE_BATTLE || battleVar1 == NULL || battleVar2 == NULL ||
        !battleVar1->mC6_isTargetSelectionCursorVisible) {
        return 0;
    }
    if (battleVar2->m2DD_currentActiveBattleMenu == 5 || battleVar2->m2DD_currentActiveBattleMenu == 100) {
        return battleVar2->m2E9_targetConfirmed ? 0 : 1;
    }
    return 2;
}

static int current(int mode) {
    return mode == 1 ? battleVar2->m2E8_currentTarget : abilitySelectedTarget;
}

/* The game's next target from s in direction d (abilitySelectTarget stays
 * where it is when that one is not among the targets). */
static int step(int mode, int s, int d) {
    int n = selectNewSlotByDirection((u8)s, (u8)d);
    if (mode == 2 && !bitmaskCharacterCheck(abilityTargetBitmask, (u8)n)) {
        return s;
    }
    return n;
}

/* The battle's sprite renderer draws an actor at v, the GTE's matrices set
 * for it (battleSpriteRender, src/battle/b_800B7C28.c PLATFORM_PC): where it
 * is on the screen, projected again here (its own projection keeps only the
 * depth cue) while the game asks for a target and the setting is on, with
 * the GTE's registers put back after (gteSave/gteLoad) and no safe point
 * (libgte's RotTransPers has one): the game goes on exactly as it would. */
void pcMouseBattleSprite(const void* actor, SVECTOR* v) {
    if (!choosing() || !mouseTargetEnabled()) {
        return;
    }
    const SpriteActorCore* a = actor;
    /* The GTE's registers as they were after: the PS1 does not do this. */
    GteState saved;
    gteSave(&saved);
    gteMtc2(0, (u16)v->vx | (u32)(u16)v->vy << 16);
    gteMtc2(1, (u16)v->vz);
    gteCommand(GTE_RTPS);
    u32 feet = gteMfc2(14);
    gteMtc2(0, (u16)v->vx | (u32)(u16)(v->vy - a->m36_height) << 16);
    gteCommand(GTE_RTPS);
    u32 head = gteMfc2(14);
    gteLoad(&saved);
    int k = -1, oldest = 0;
    for (int i = 0; i < 16; i++) {
        if (seen[i].actor == actor) {
            k = i;
            break;
        }
        if (seen[i].vblank < seen[oldest].vblank) {
            oldest = i;
        }
    }
    if (k < 0) {
        k = oldest;
    }
    seen[k].actor = actor;
    seen[k].x = (short)(feet & 0xFFFF);
    seen[k].feet = (short)(feet >> 16);
    seen[k].head = (short)(head >> 16);
    seen[k].vblank = hwVSyncCount();
}

/* Where the target's sprite was drawn (in the last few vblanks). */
static int screenOf(int entity, int* x, int* feet, int* head) {
    if (entity < 0 || entity >= 11 || battleBssBlock.m8C8C_spriteActorCores[entity] == NULL) {
        return 0;
    }
    const void* actor = battleBssBlock.m8C8C_spriteActorCores[entity];
    u32 now = hwVSyncCount();
    for (int i = 0; i < 16; i++) {
        if (seen[i].actor == actor && now - seen[i].vblank <= 4) {
            *x = seen[i].x;
            *feet = seen[i].feet;
            *head = seen[i].head < seen[i].feet ? seen[i].head : seen[i].feet - 1;
            return 1;
        }
    }
    return 0;
}

int mouseBattlePoint(float x, float y, MousePlan* plan) {
    plan->count = 0;
    int mode = choosing();
    if (mode == 0) {
        return 0;
    }
    int best = -1;
    float bestD = 1e9f;
    for (int i = 0; i < 11; i++) {
        int t = targetsPerPriority[i], sx, feet, head;
        if (t == 0xFF || (mode == 2 && !bitmaskCharacterCheck(abilityTargetBitmask, (u8)t)) ||
            !screenOf(t, &sx, &feet, &head)) {
            continue;
        }
        int half = (feet - head) / 4 + 12, mid = (feet + head) / 2;
        mouseLog("mouse: target %d at %d, %d to %d", t, sx, head, feet);
        if (x >= sx - half && x <= sx + half && y >= head && y <= feet + 6) {
            float d = (x - sx) * (x - sx) + (y - mid) * (y - mid);
            if (d < bestD) {
                bestD = d;
                best = t;
            }
        }
    }
    if (best < 0) {
        return -1; /* beside the targets: nothing */
    }
    /* The shortest way there by the game's own directions (breadth first). */
    u8 from[11], how[11];
    int queue[11], head = 0, tail = 0, start = current(mode);
    if (start < 0 || start >= 11) {
        return -1;
    }
    for (int i = 0; i < 11; i++) {
        from[i] = 0xFF;
    }
    from[start] = (u8)start;
    queue[tail++] = start;
    while (head < tail && from[best] == 0xFF) {
        int s = queue[head++];
        for (int d = 0; d < 4; d++) {
            int n = step(mode, s, d);
            if (n >= 0 && n < 11 && from[n] == 0xFF) {
                from[n] = (u8)s;
                how[n] = (u8)d;
                queue[tail++] = n;
            }
        }
    }
    if (from[best] == 0xFF) {
        return -1; /* (not reachable from the current target) */
    }
    int path[11], n = 0;
    for (int s = best; s != start && n < 11; s = from[s]) {
        path[n++] = how[s];
    }
    while (n > 0 && plan->count < MOUSE_PLAN_MAX - 1) {
        plan->bits[plan->count++] = directionBits[path[--n]];
    }
    plan->confirm = mode == 2 || plan->count == 0;
    plan->check = MOUSE_CHECK_TARGET;
    plan->checkArg = best;
    return 1;
}

int mouseBattleCheck(int target) {
    int mode = choosing();
    return mode != 0 && current(mode) == target;
}

/* Save states (savestate.h): the actors seen are the old timeline's. */
void mouseBattleStateLoaded(void) {
    __builtin_memset(seen, 0, sizeof(seen));
}
