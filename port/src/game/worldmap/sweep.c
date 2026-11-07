/*
 * The world map's per-frame state for the 60 Hz oracle (XG_SWEEP_LOG, written
 * by port/src/system/sweeplog.c; docs/ENGINE_PLAN.md "60 Hz world map"). The
 * Enhanced engine's worldmapMainLoop (engine/src/worldmap/w_80070CFC.c) calls
 * portWorldmapSweepFrame on every frame once the frame's input is collected,
 * before its tasks run, and the ground task calls portWorldmapSweepRender
 * once the frame's picture is set up. The Original never calls either.
 *
 *   W <vblank> frame tick mode gearMode seed in1 in2 in3
 *     radar(x y z) radar3(x y z) rot(x y z) dist eye(x y z)
 *     encTimer encCount0 encPeriod encTriggered wave followIndex
 *     grid(x z) gameStateHash clock(us) 0 0 0, then per task entry 0..19:
 *     state subState x y z direction m5C hidden animation spriteFrame wait
 *   R <vblank> frame tick eye(x y z) rot(x y z) radar3(x y z) 0, then entries
 *     1..8 as drawn: x y z (0 0 0 when hidden), then model 0 (the Yggdrasil) as drawn: x y z
 *   M <vblank> frame tick count, then per model (up to 96) as drawn: hidden x y z
 *     m[0][0] m[0][1] m[2][2] (rotation and scale) and the drawn buffer's first
 *     primitive's red (its colour), for the event modes' evenness
 *     (work/worldmap60/evenmodels.py)
 *
 * tick is 1 on the frames that run the game's logic (every frame of the
 * native 30 Hz loop), 0 on the 60 Hz frames between. Comparing a native run
 * with a 60 Hz run: the native run's k-th W line against the 60 Hz run's k-th
 * tick line (work/worldmap60/compare.py).
 */
#include "common.h"
#include "psyq.h"
#include "main/sprite.h"
#include "main/game_state.h"
#include "worldmap/worldmap.h"

#define SWEEP_ENTRIES 20
#define SWEEP_HEADER 34
#define SWEEP_PER_ENTRY 11

int portSweepEnabled(void);
void portSweepLog(const char* tag, const int* values, int count);
u32 portRandSeed(void);
double hwNow(void); /* system/hw.c */

extern sWorldmapState* gWorldmapState;
extern sWorldmapModel* worldmapModels;
extern s16 worldmapNumModels;
extern s32 worldmapOddOrEven;
extern VECTOR worldmapRadarPosition;
extern VECTOR worldmapRadarPosition3;
extern SVECTOR worldmapRotation;
extern s32 worldmapCameraVar0;
extern sWorldmapCameraState worldmapCameraVector;
extern s32 worldmapEncounterTimer;
extern s16 worldmapEncounterCountdowns[16];
extern s32 worldmapEncounterPeriod;
extern s32 worldmapEncounterTriggered;
extern s32 worldmapWaterWave;
extern s16 worldmapFollowLeaderDataIndex;
extern VECTOR worldmapGridInputPosition;
extern s32 worldMapGearMode;
extern s32 currentWorldmapMode;
extern u16 worldmapInput1_0;
extern u16 worldmapInput2_0;
extern u16 worldmapInput3_0;

void portWorldmapSweepFrame(s32 frame, s32 tick) {
    s32 v[SWEEP_HEADER + SWEEP_ENTRIES * SWEEP_PER_ENTRY];
    s32 n = 0;
    s32 i;
    u32 h = 0;
    u8* g = (u8*)&gameState;

    if (!portSweepEnabled() || gWorldmapState == NULL) {
        return;
    }
    for (i = 0; i < (s32)sizeof(gameState); i++) {
        h = h * 31 + g[i];
    }
    v[n++] = frame;
    v[n++] = tick;
    v[n++] = currentWorldmapMode;
    v[n++] = worldMapGearMode;
    v[n++] = portRandSeed();
    v[n++] = worldmapInput1_0;
    v[n++] = worldmapInput2_0;
    v[n++] = worldmapInput3_0;
    v[n++] = worldmapRadarPosition.vx;
    v[n++] = worldmapRadarPosition.vy;
    v[n++] = worldmapRadarPosition.vz;
    v[n++] = worldmapRadarPosition3.vx;
    v[n++] = worldmapRadarPosition3.vy;
    v[n++] = worldmapRadarPosition3.vz;
    v[n++] = worldmapRotation.vx;
    v[n++] = worldmapRotation.vy;
    v[n++] = worldmapRotation.vz;
    v[n++] = worldmapCameraVar0;
    v[n++] = worldmapCameraVector.m0_eye.vx;
    v[n++] = worldmapCameraVector.m0_eye.vy;
    v[n++] = worldmapCameraVector.m0_eye.vz;
    v[n++] = worldmapEncounterTimer;
    v[n++] = worldmapEncounterCountdowns[0];
    v[n++] = worldmapEncounterPeriod;
    v[n++] = worldmapEncounterTriggered;
    v[n++] = worldmapWaterWave;
    v[n++] = worldmapFollowLeaderDataIndex;
    v[n++] = worldmapGridInputPosition.vx;
    v[n++] = worldmapGridInputPosition.vz;
    v[n++] = (s32)h;
    v[n++] = (s32)(hwNow() * 1e6); /* the clock (microseconds): when the frame's logic starts */
    v[n++] = 0;
    v[n++] = 0;
    v[n++] = 0;
    for (i = 0; i < SWEEP_ENTRIES; i++) {
        sWorldmapStateEntry* e = &gWorldmapState->m0_entries[i];
        v[n++] = e->m1C_update != NULL ? e->m0_state : -1;
        v[n++] = e->m20_subState;
        v[n++] = e->m28_position.vx;
        v[n++] = e->m28_position.vy;
        v[n++] = e->m28_position.vz;
        v[n++] = e->m48_direction;
        v[n++] = e->m5C;
        v[n++] = e->m24_hidden;
        v[n++] = e->m4C_sprite != NULL ? (s8)e->m4C_sprite->mAF_animationId : -1;
        v[n++] = e->m4C_sprite != NULL ? e->m4C_sprite->m34_currentSpriteFrame : -1;
        v[n++] = e->m4C_sprite != NULL ? e->m4C_sprite->m9E_wait : -1;
    }
    portSweepLog("W", v, n);
}

void portWorldmapSweepRender(s32 frame, s32 tick) {
    s32 v[12 + 24 + 3];
    s32 n = 0;
    s32 i;

    if (!portSweepEnabled() || gWorldmapState == NULL) {
        return;
    }
    v[n++] = frame;
    v[n++] = tick;
    v[n++] = worldmapCameraVector.m0_eye.vx;
    v[n++] = worldmapCameraVector.m0_eye.vy;
    v[n++] = worldmapCameraVector.m0_eye.vz;
    v[n++] = worldmapRotation.vx;
    v[n++] = worldmapRotation.vy;
    v[n++] = worldmapRotation.vz;
    v[n++] = worldmapRadarPosition3.vx;
    v[n++] = worldmapRadarPosition3.vy;
    v[n++] = worldmapRadarPosition3.vz;
    v[n++] = 0;
    for (i = 1; i <= 8; i++) {
        sWorldmapStateEntry* e = &gWorldmapState->m0_entries[i];
        s32 shown = e->m24_hidden == 0 && e->m1C_update != NULL; /* hidden: 0 0 0 */
        v[n++] = shown ? e->m28_position.vx : 0;
        v[n++] = shown ? e->m28_position.vy : 0;
        v[n++] = shown ? e->m28_position.vz : 0;
    }
    v[n++] = worldmapModels != NULL && worldmapNumModels > 0 ? worldmapModels[0].m8_position.vx : 0;
    v[n++] = worldmapModels != NULL && worldmapNumModels > 0 ? worldmapModels[0].m8_position.vy : 0;
    v[n++] = worldmapModels != NULL && worldmapNumModels > 0 ? worldmapModels[0].m8_position.vz : 0;
    portSweepLog("R", v, n);
    if (worldmapModels != NULL) {
        static s32 m[4 + 96 * 8];
        s32 k = worldmapNumModels < 96 ? worldmapNumModels : 96;
        n = 0;
        m[n++] = frame;
        m[n++] = tick;
        m[n++] = k;
        for (i = 0; i < k; i++) {
            sWorldmapModel* p = &worldmapModels[i];
            u8* prim = p->m48_primBuffers[worldmapOddOrEven];
            m[n++] = p->m0_hidden;
            m[n++] = p->m8_position.vx;
            m[n++] = p->m8_position.vy;
            m[n++] = p->m8_position.vz;
            m[n++] = p->m20_rotationMatrix.m[0][0];
            m[n++] = p->m20_rotationMatrix.m[0][1];
            m[n++] = p->m20_rotationMatrix.m[2][2];
            m[n++] = prim != NULL ? ((POLY_FT3*)prim)->r0 : 0;
        }
        portSweepLog("M", m, n);
    }
}
