/*
 * Battling's per-frame state for the 60 Hz oracle (XG_SWEEP_LOG, written by
 * port/src/system/sweeplog.c; work/battling60): battlingMain calls this once
 * a frame, after the coroutine's step. A BL line: the frame counter, the
 * scene mode and the next one, the game mode, the round winner, then per
 * Gear its position, turn, HP, bar HP, gauge, action, move state, animation
 * and animation frame, and the random seed. Two runs of the same match, with
 * and without the pictures between, must log the same BL lines.
 */
#include "common.h"
#include "psyq.h"
#include "battling/battling.h"

extern BattlingGear battlingGear1;
extern BattlingGear battlingGear2;
extern s32 battlingSceneMode;
extern s32 battlingNextSceneMode;
extern s32 battlingGameMode;
extern s32 battlingRoundWinner;
extern s32 battlingFrameCounter;
extern s16 battlingVsyncInterval;
int portSweepEnabled(void);
void portSweepLog(const char* tag, const int* values, int count);
u32 portRandSeed(void);

void portBattlingSweepFrame(void) {
    int v[6 + 2 * 12 + 1];
    int n = 0;
    int g;
    BattlingGear* gears[2];

    if (!portSweepEnabled()) {
        return;
    }
    gears[0] = &battlingGear1;
    gears[1] = &battlingGear2;
    v[n++] = battlingFrameCounter;
    v[n++] = battlingSceneMode;
    v[n++] = battlingNextSceneMode;
    v[n++] = battlingGameMode;
    v[n++] = battlingRoundWinner;
    v[n++] = battlingVsyncInterval;
    for (g = 0; g < 2; g++) {
        BattlingGear* p = gears[g];
        v[n++] = p->m0_position.vx;
        v[n++] = p->m0_position.vy;
        v[n++] = p->m0_position.vz;
        v[n++] = p->m54_rotY;
        v[n++] = p->mB4_hp;
        v[n++] = p->mB8_barHp;
        v[n++] = p->mB6_gauge;
        v[n++] = p->mC5_action;
        v[n++] = p->mC4_moveState;
        v[n++] = p->m4C_animation;
        v[n++] = p->m50_animFrame;
        v[n++] = (int)p->mD0_status.flags;
    }
    v[n++] = (int)portRandSeed();
    portSweepLog("BL", v, n);
}
