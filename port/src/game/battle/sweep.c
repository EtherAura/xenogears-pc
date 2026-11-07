/*
 * The battle's per-frame state for content sweeps (XG_SWEEP_LOG, written by
 * port/src/system/sweeplog.c). battleRender calls this first thing each
 * battle frame; the Redux side reads the same values from RAM at battleRender's
 * entry (US 0x800BE790; work/agents/PORT_M6/battles/bcap.py).
 *
 * One line "B <vblank> <values>": the log's vblank while the slowdown replay
 * is lined up (else -1), the random seed, the acting entity, the current
 * turn, AP and menu (battleVar2), a hash of the 11 battle entities (their
 * layout is the PS1's), then per entity: HP, gear HP, gear fuel and the first
 * status word; then the catch-up tick count (spriteCallback2Var4) the frame starts with and the
 * camera: eye, at, their targets (x y z each), battleIR0, the camera mode and its interpolation;
 * last finalizeBattleMechaLoadingVar0 (a gear load waiting for the CD to go idle).
 *
 * XG_BATTLE_POKE=<vblank>[,<who>]: at the first battle frame at or after that
 * log vblank, set the HP (and gear HP) of the enemies (who 0, the default) or
 * of the party (who 1) to 1 where it is higher, so sweep battles reach the
 * result screen or the game over; Redux pokes the same frame (bcap.py --bpoke).
 */
#include "common.h"
#include "battle/battle.h"

#define SWEEP_HEADER 7
#define SWEEP_PER_ENTITY 4

extern s16 spriteCallback2Var4;
extern SVECTOR battleCameraEye;
extern SVECTOR battleCameraAt;
extern s32 battleIR0;
extern s32 battleCameraMode;
extern s32 battleCameraModeInterpolation;
extern u8 finalizeBattleMechaLoadingVar0;

int portSweepEnabled(void);
void portSweepLog(const char* tag, const int* values, int count);
const char* portOption(const char* name);
u32 portRandSeed(void);
int slowdownLogVBlank(s64* vblank);

static s32 pokeAt = -2; /* -2: not read yet, -1: none or done */
static s32 pokeWho;

static const char* readNumber(const char* s, s32* out) {
    s32 v = 0;

    while (*s >= '0' && *s <= '9') {
        v = v * 10 + (*s++ - '0');
    }
    *out = v;
    return s;
}

static void poke(s64 logNow) {
    s32 i;
    const char* s;

    if (pokeAt == -2) {
        pokeAt = -1;
        s = portOption("XG_BATTLE_POKE");
        if (s != NULL) {
            s = readNumber(s, &pokeAt);
            if (*s == ',') {
                readNumber(s + 1, &pokeWho);
            }
        }
    }
    /* Only near that vblank: a chained battle resets this module's data
     * (portResetModule), and Redux pokes once. */
    if (pokeAt < 0 || logNow < pokeAt || logNow > pokeAt + 120) {
        return;
    }
    pokeAt = -1;
    for (i = pokeWho ? 0 : 3; i < (pokeWho ? 3 : 11); i++) {
        sBattleEntity* e = &battleEntities.m0_entities[i];
        if (e->m0_base.m4C_HP > 1) {
            e->m0_base.m4C_HP = 1;
        }
        if (e->mA4_gear.m60_hp > 1) {
            e->mA4_gear.m60_hp = 1;
        }
    }
}

void portBattleSweepFrame(void) {
    s32 v[SWEEP_HEADER + 11 * SWEEP_PER_ENTITY + 17];
    s32 n = 0;
    s32 i;
    s64 logNow;
    u32 hash = 0;
    u32* words = (u32*)battleEntities.m0_entities;

    if (!portSweepEnabled()) {
        return;
    }
    if (!slowdownLogVBlank(&logNow)) {
        logNow = -1;
    } else {
        poke(logNow);
    }
    for (i = 0; i < (s32)(sizeof(battleEntities.m0_entities) / 4); i++) {
        hash = hash * 31u + words[i];
    }
    v[n++] = (s32)logNow;
    v[n++] = portRandSeed();
    v[n++] = battleBssBlock.mA72_currentEntity;
    v[n++] = battleVar2 != NULL ? battleVar2->m2D3_currentEntityTurn : -1;
    v[n++] = battleVar2 != NULL ? battleVar2->m2D4_remainingAP : -1;
    v[n++] = battleVar2 != NULL ? battleVar2->m2DD_currentActiveBattleMenu : -1;
    v[n++] = hash;
    for (i = 0; i < 11; i++) {
        sBattleEntity* e = &battleEntities.m0_entities[i];
        v[n++] = e->m0_base.m4C_HP;
        v[n++] = e->mA4_gear.m60_hp;
        v[n++] = e->mA4_gear.m38_fuel;
        v[n++] = e->m0_base.m7C_statusFlags[0];
    }
    v[n++] = spriteCallback2Var4;
    v[n++] = battleCameraEye.vx;
    v[n++] = battleCameraEye.vy;
    v[n++] = battleCameraEye.vz;
    v[n++] = battleCameraAt.vx;
    v[n++] = battleCameraAt.vy;
    v[n++] = battleCameraAt.vz;
    v[n++] = battleCameraBlock.m4_eyeTarget.vx;
    v[n++] = battleCameraBlock.m4_eyeTarget.vy;
    v[n++] = battleCameraBlock.m4_eyeTarget.vz;
    v[n++] = battleCameraBlock.mC_atTarget.vx;
    v[n++] = battleCameraBlock.mC_atTarget.vy;
    v[n++] = battleCameraBlock.mC_atTarget.vz;
    v[n++] = battleIR0;
    v[n++] = battleCameraMode;
    v[n++] = battleCameraModeInterpolation;
    v[n++] = finalizeBattleMechaLoadingVar0;
    portSweepLog("B", v, n);
}

/* The Enhanced engine's 60 Hz battle oracle (xg-hbc.19.2, work/battle60): what a
 * battle frame draws, "BR <vblank> eye(x y z) at(x y z)" then per battle entity's
 * sprite actor (11) x y z (whole units) and its sprite frame (-1: none), then what the
 * engine adds. */
void portBattleSweepPicture(const s32* extra, s32 count) {
    s32 v[6 + 11 * 4 + 16];
    s32 n = 0;
    s32 i;

    if (!portSweepEnabled()) {
        return;
    }
    v[n++] = battleCameraEye.vx;
    v[n++] = battleCameraEye.vy;
    v[n++] = battleCameraEye.vz;
    v[n++] = battleCameraAt.vx;
    v[n++] = battleCameraAt.vy;
    v[n++] = battleCameraAt.vz;
    for (i = 0; i < 11; i++) {
        SpriteActorCore* a = battleBssBlock.m8C8C_spriteActorCores[i];
        v[n++] = a != NULL ? a->m0_position.vx >> 16 : 0;
        v[n++] = a != NULL ? a->m0_position.vy >> 16 : 0;
        v[n++] = a != NULL ? a->m0_position.vz >> 16 : 0;
        v[n++] = a != NULL ? (s32)a->m34_currentSpriteFrame : -1;
    }
    for (i = 0; i < count && i < 16; i++) {
        v[n++] = extra[i];
    }
    portSweepLog("BR", v, n);
}
