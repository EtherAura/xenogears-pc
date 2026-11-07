/*
 * The field's per-frame state for content sweeps (XG_SWEEP_LOG, written by
 * port/src/system/sweeplog.c). updateAndRenderField calls this first, every
 * field frame (the main loop's and the transitions'); the Redux side reads
 * the same values from RAM at the same point (US 0x8007554C, an exec
 * breakpoint; work/agents/PORT_M6/fields/fsweep.py).
 *
 * One line "F <vblank> <values>": frame number, field, actor count, the
 * random seed, the camera eye and target, a hash of the 0x800 bytes of
 * script variables but the play time at 0xC (h = h * 31 + byte, from 0),
 * then per actor: script position x y z, script PC, rotation, animation and
 * the script flags. When the hash changes, a "V" line has the variables.
 */
#include "common.h"
#include "psyq.h"
#include "field/field.h"

#define SWEEP_MAX_ACTORS 64
#define SWEEP_HEADER 11
#define SWEEP_PER_ACTOR 7

int portSweepEnabled(void);
void portSweepLog(const char* tag, const int* values, int count);
u32 portRandSeed(void);
extern FieldBss_800AF880 fieldSceneState;
extern s32 fieldMapNumber;

static s32 sweepFrame;
static u32 sweepVarsHash;

void portFieldSweepFrame(void) {
    s32 v[SWEEP_HEADER + SWEEP_MAX_ACTORS * SWEEP_PER_ACTOR];
    s32 n = 0;
    s32 i;
    s32 count;
    u32 hash = 0;
    u8* vars = (u8*)fieldVars;

    if (!portSweepEnabled()) {
        return;
    }
    for (i = 0; i < 0x800; i++) {
        if (i != 12 && i != 13) { /* variable 0xC: the play time (setVar in syncKernelAndFieldStates) */
            hash = hash * 31 + vars[i];
        }
    }
    count = g_totalActors;
    if (count > SWEEP_MAX_ACTORS) {
        count = SWEEP_MAX_ACTORS;
    }
    if (count < 0) {
        count = 0;
    }
    v[n++] = sweepFrame++;
    v[n++] = fieldMapNumber;
    v[n++] = g_totalActors;
    v[n++] = portRandSeed();
    v[n++] = fieldSceneState.m0_cameraEye.vx;
    v[n++] = fieldSceneState.m0_cameraEye.vy;
    v[n++] = fieldSceneState.m0_cameraEye.vz;
    v[n++] = fieldSceneState.m10_cameraAt.vx;
    v[n++] = fieldSceneState.m10_cameraAt.vy;
    v[n++] = fieldSceneState.m10_cameraAt.vz;
    v[n++] = hash;
    for (i = 0; i < count; i++) {
        sFieldScriptEntity* e = fieldSceneState.m290_actorArray != NULL ? fieldSceneState.m290_actorArray[i].m4C_scriptEntity : NULL;
        if (e == NULL) {
            s32 k;
            for (k = 0; k < SWEEP_PER_ACTOR; k++) {
                v[n++] = 0;
            }
            continue;
        }
        v[n++] = e->m20_position.vx;
        v[n++] = e->m20_position.vy;
        v[n++] = e->m20_position.vz;
        v[n++] = e->mCC_scriptPC;
        v[n++] = e->m106_currentRotation;
        v[n++] = e->mE8_currentAnimationId;
        v[n++] = e->m0_fieldScriptFlags;
    }
    portSweepLog("F", v, n);
    if (sweepFrame == 1 || hash != sweepVarsHash) {
        /* the variables themselves when they change: "V <vblank> <frame> <0x400 values>" */
        static s32 vars16[1 + 0x400];
        vars16[0] = sweepFrame - 1;
        for (i = 0; i < 0x400; i++) {
            vars16[1 + i] = fieldVars[i];
        }
        portSweepLog("V", vars16, 1 + 0x400);
        sweepVarsHash = hash;
    }
}

/* The Enhanced engine's 60 Hz field oracle (xg-hbc.19.3, work/field60b): what a
 * field frame's picture shows, one "FP <vblank> <values>" line a frame when
 * XG_SWEEP_PICTURE=1 is set beside XG_SWEEP_LOG (portSweepPictureEnabled,
 * system/sweeplog.c). The engine fills the values (engine/src/field/
 * f_80072140.c pcFieldPictureLog): the frame's index and tick, the schedule,
 * the projection, the compass, hashes of the faders, particles, dialogue
 * windows, distortion, Gears and transition strips as drawn, the scale, DIP
 * and SCRZ, the scene objects, then per actor its drawn position, sprite
 * frame and facing. The Original never calls this. */
int portSweepPictureEnabled(void);
int portFieldPictureEnabled(void) {
    return portSweepPictureEnabled();
}
void portFieldSweepPicture(const s32* v, s32 n) {
    if (portSweepPictureEnabled()) {
        portSweepLog("FP", v, n);
    }
}
