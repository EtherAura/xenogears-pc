/*
 * Gears in battle at 60 Hz (xg-f8k.2; Smooth motion, port/src/gpu/interpolation.c).
 *
 * A battle draws every vertical blank, but renderMechasBattle steps the
 * Gears' animation every other one (its half-step accumulator), and each
 * frame draws the bone matrices updateMechAnim_battle built before that
 * frame's step: a step frame shows the pose before it, the next frame the
 * pose after it. On a step frame the pose after it is already known, so
 * the Gear can be drawn halfway between the two instead: a pose for every
 * vertical blank, none ahead of the game's own, without running the
 * animation again. Only the drawing of the Gear's own meshes takes these
 * matrices (submitMechaForRendering_battle); the bones, the shadow, the
 * cloth and every event keep the game's, so the game's state is the same
 * with smoothing on and off.
 *
 * The halfway matrices come from the game's own builder
 * (updateMechAnimSub1/2_battle) run on a copy of the bones with halfway
 * angles, translations and scales. A step that jumps (a new animation's
 * first pose) keeps the game's matrices for that Gear.
 */
#include "common.h"
#include "psyq.h"
#include "field_mecha/mecha.h"
#include "gte_pc.h"

/* The halfway pose's own work is the PC's: the game's GTE registers and
 * scratchpad as the game left them, and no time on the stepped clock (the
 * game's timing, its sound among it, stays the Original's; xg-hbc.19.2). */
static GteState gteKept;
static u8 scratchKept[0x400];
void hwClockHold(int on); /* system/hw.c: the PS1 does none of this */
static void keep(void) {
    hwClockHold(1);
    gteSave(&gteKept);
    memcpy(scratchKept, (void*)0x1F800000, sizeof scratchKept);
}
static void putBack(void) {
    memcpy((void*)0x1F800000, scratchKept, sizeof scratchKept);
    gteLoad(&gteKept);
    hwClockHold(0);
}

int interpolationEnabled(void); /* gpu/interpolation.c */
int pcGameplayBattle60(void);    /* system/gameplay.c: the Enhanced engine's 60 Hz battle (battle60) */
s32 updateMechAnimSub1_battle(sMechaBone* bones, s32 scale);
s32 updateMechAnimSub2_battle(sMechaBone* bones, s32 scale);
s32 updateMechAnimSub1(sMechaBone* bones, s32 scale); /* the field's builders (field_mecha) */
s32 updateMechAnimSub2(sMechaBone* bones, s32 scale);

/* The Enhanced engine's 60 Hz field (xg-hbc.19.3) uses the same halfway poses
 * for its Gears (engine/src/field_mecha/fm_801E34BC.c renderMechas, while the
 * field draws them: pcGearPoseField): the field's own builders, a pose saved
 * every frame (a Gear the scripts move a step a tick is drawn through the
 * tick even on frames its animation does not step: pcGearPoseFieldAfter's
 * root shift), and nothing of the battle's switches. */
static s32 fieldMode;
void pcGearPoseField(int on) {
    fieldMode = on;
}
int pcGearPoseFieldActive(void) {
    return fieldMode;
}

#define GEAR_SLOTS 32
#define GEAR_BONES 64
#define JUMP_ANGLE 0x200      /* an eighth of a turn in one step */
#define JUMP_TRANSLATION 0x800

typedef struct {
    SVECTOR angles;
    s32 translation[3];
    s16 scale[3];
} BonePose;

static struct {
    const sLoadedMechas* mecha;
    u32 saved;  /* the frame the pose before the step was saved in */
    u32 built;  /* the frame the halfway matrices are for */
    u32 kept;   /* the field: the frame that last kept the pose (a frame between keeps the tick's) */
    s32 n;
    BonePose before[GEAR_BONES];
    MATRIX root; /* the root's local matrix */
    MATRIX final[GEAR_BONES];
} slots[GEAR_SLOTS];

static u32 frame = 1;
static sMechaBone copy[GEAR_BONES];

void pcGearPoseFrame(void) {
    frame++;
}

static s32 slotFor(const sLoadedMechas* m, s32 add) {
    s32 i;
    s32 free_ = -1;

    for (i = 0; i < GEAR_SLOTS; i++) {
        if (slots[i].mecha == m) return i;
        if (free_ < 0 && (slots[i].mecha == NULL || (frame - slots[i].saved > 2 && frame - slots[i].built > 2))) {
            free_ = i;
        }
    }
    if (!add || free_ < 0) return -1;
    slots[free_].mecha = m;
    slots[free_].saved = slots[free_].built = 0;
    return free_;
}

/* Before the frame's steps (iterations > 0): the pose the frame draws. */
void pcGearPoseBefore(const sLoadedMechas* m, s32 iterations) {
    const sMechaBone* b = m->m4_bones;
    s32 s;
    s32 i;
    s32 n;

    if (b == NULL || (!fieldMode && (iterations <= 0 || !(interpolationEnabled() || pcGameplayBattle60())))) return;
    n = b->mA_numBones;
    if (n <= 0 || n > GEAR_BONES || (s = slotFor(m, 1)) < 0) return;
    if (fieldMode && iterations <= 0 && slots[s].saved != 0 && frame - slots[s].saved < 8) {
        /* the field, a frame between: the pose before the tick's step stays (drawn through it) */
        slots[s].kept = frame;
        return;
    }
    slots[s].kept = frame;
    for (i = 0; i < n; i++) {
        slots[s].before[i].angles = b[i].m54_rotationAngles;
        slots[s].before[i].translation[0] = b[i].m5C_translation[0];
        slots[s].before[i].translation[1] = b[i].m5C_translation[1];
        slots[s].before[i].translation[2] = b[i].m5C_translation[2];
        slots[s].before[i].scale[0] = b[i].m4C_scale[0];
        slots[s].before[i].scale[1] = b[i].m4C_scale[1];
        slots[s].before[i].scale[2] = b[i].m4C_scale[2];
    }
    slots[s].n = n;
    slots[s].saved = frame;
}

static s32 halfAngle(s16 from, s16 to, s16* out) {
    s32 d = ((to - from + 0x800) & 0xFFF) - 0x800;

    *out = (s16)(from + d / 2);
    return d > JUMP_ANGLE || d < -JUMP_ANGLE;
}

static s32 halfTranslation(s32 from, s32 to, s32* out) {
    s32 d = to - from;

    *out = from + d / 2;
    return d > JUMP_TRANSLATION || d < -JUMP_TRANSLATION;
}

static void build(const sLoadedMechas* m, s32 s, s32 shiftX, s32 shiftZ, s32 k, s32 parts);
/* After the frame's steps: the halfway matrices. */
void pcGearPoseAfter(const sLoadedMechas* m) {
    build(m, slotFor(m, 0), 0, 0, 1, 2);
}
/* The field: after the frame's steps, the matrices k/parts of the way from
 * the pose before the native frame's step (taken on its tick) to the game's,
 * with the root moved by the part of a scripted step the frame has not
 * reached (shiftX/Z, whole units; engine/src/field). At k == parts without a
 * shift: the game's matrices. */
void pcGearPoseFieldAfter(const sLoadedMechas* m, s32 shiftX, s32 shiftZ, s32 k, s32 parts) {
    if (!fieldMode || (k >= parts && shiftX == 0 && shiftZ == 0)) return;
    build(m, slotFor(m, 0), shiftX, shiftZ, k, parts);
}
static s32 partAngle(s16 from, s16 to, s16* out, s32 k, s32 parts) {
    s32 d = ((to - from + 0x800) & 0xFFF) - 0x800;

    *out = (s16)(from + d * k / parts);
    return d > JUMP_ANGLE || d < -JUMP_ANGLE;
}
static s32 partTranslation(s32 from, s32 to, s32* out, s32 k, s32 parts) {
    s32 d = to - from;

    *out = from + (s32)(((long long)d * k) / parts);
    return d > JUMP_TRANSLATION || d < -JUMP_TRANSLATION;
}
static void build(const sLoadedMechas* m, s32 s, s32 shiftX, s32 shiftZ, s32 k, s32 parts) {
    const sMechaBone* b = m->m4_bones;
    s32 jump = 0;
    s32 i;
    s32 n;
    s32 tick;

    if (s < 0 || b == NULL || b->mA_numBones != slots[s].n) return;
    if (fieldMode ? slots[s].kept != frame : slots[s].saved != frame) return;
    tick = slots[s].saved == frame;
    n = slots[s].n;
    for (i = 0; i < n; i++) {
        const BonePose* p = &slots[s].before[i];

        copy[i] = b[i];
        if (b[i].m0_parentBone != NULL) {
            copy[i].m0_parentBone = &copy[b[i].m0_parentBone - b];
        }
        if (!fieldMode) {
            jump |= halfAngle(p->angles.vx, b[i].m54_rotationAngles.vx, &copy[i].m54_rotationAngles.vx);
            jump |= halfAngle(p->angles.vy, b[i].m54_rotationAngles.vy, &copy[i].m54_rotationAngles.vy);
            jump |= halfAngle(p->angles.vz, b[i].m54_rotationAngles.vz, &copy[i].m54_rotationAngles.vz);
            jump |= halfTranslation(p->translation[0], b[i].m5C_translation[0], &copy[i].m5C_translation[0]);
            jump |= halfTranslation(p->translation[1], b[i].m5C_translation[1], &copy[i].m5C_translation[1]);
            jump |= halfTranslation(p->translation[2], b[i].m5C_translation[2], &copy[i].m5C_translation[2]);
            copy[i].m4C_scale[0] = (s16)((p->scale[0] + b[i].m4C_scale[0]) / 2);
            copy[i].m4C_scale[1] = (s16)((p->scale[1] + b[i].m4C_scale[1]) / 2);
            copy[i].m4C_scale[2] = (s16)((p->scale[2] + b[i].m4C_scale[2]) / 2);
        } else if (i > 0 || tick) {
            /* the field's root follows its actor every frame (place, facing: renderFieldCharacterSprites):
             * on a frame between it is the game's, the bones the VM moves are drawn through the step */
            jump |= partAngle(p->angles.vx, b[i].m54_rotationAngles.vx, &copy[i].m54_rotationAngles.vx, k, parts);
            jump |= partAngle(p->angles.vy, b[i].m54_rotationAngles.vy, &copy[i].m54_rotationAngles.vy, k, parts);
            jump |= partAngle(p->angles.vz, b[i].m54_rotationAngles.vz, &copy[i].m54_rotationAngles.vz, k, parts);
            jump |= partTranslation(p->translation[0], b[i].m5C_translation[0], &copy[i].m5C_translation[0], k, parts);
            jump |= partTranslation(p->translation[1], b[i].m5C_translation[1], &copy[i].m5C_translation[1], k, parts);
            jump |= partTranslation(p->translation[2], b[i].m5C_translation[2], &copy[i].m5C_translation[2], k, parts);
            copy[i].m4C_scale[0] = (s16)(p->scale[0] + (b[i].m4C_scale[0] - p->scale[0]) * k / parts);
            copy[i].m4C_scale[1] = (s16)(p->scale[1] + (b[i].m4C_scale[1] - p->scale[1]) * k / parts);
            copy[i].m4C_scale[2] = (s16)(p->scale[2] + (b[i].m4C_scale[2] - p->scale[2]) * k / parts);
        }
        copy[i].m4_needTranslationUpdate = 1;
        copy[i].m5_needRotationUpdate = 1;
    }
    if (jump) return;
    copy[0].m5C_translation[0] += shiftX;
    copy[0].m5C_translation[2] += shiftZ;
    keep();
    if (m->m37_useBoneScale != 0) {
        (fieldMode ? updateMechAnimSub2 : updateMechAnimSub2_battle)(copy, m->m1C_scale);
    } else {
        (fieldMode ? updateMechAnimSub1 : updateMechAnimSub1_battle)(copy, m->m1C_scale);
    }
    putBack();
    slots[s].root = copy[0].mC_localMatrix;
    for (i = 0; i < n; i++) {
        slots[s].final[i] = copy[i].m2C_boneFinalMatrix;
    }
    slots[s].built = frame;
}

/* The Gear's halfway matrices for this frame's drawing: its slot, or -1. */
s32 pcGearPoseFor(const sLoadedMechas* m) {
    s32 s = slotFor(m, 0);

    return s >= 0 && slots[s].built == frame ? s : -1;
}

MATRIX* pcGearPoseRoot(s32 s, MATRIX* game) {
    return s >= 0 ? &slots[s].root : game;
}

MATRIX* pcGearPoseFinal(s32 s, s32 bone, MATRIX* game) {
    return s >= 0 && bone < slots[s].n ? &slots[s].final[bone] : game;
}

/* An attached mecha (a part on a Gear's bone): placed again from its
 * parent's halfway matrices, as updateMechasBaseMatrixIsAttached_battle
 * places it from the parent's. Either without a halfway pose this frame:
 * the game's matrices for it. */
void pcGearPoseAttach(const sLoadedMechas* m, const sLoadedMechas* parent) {
    s32 s = slotFor(m, 0);
    s32 p = parent != NULL ? slotFor(parent, 0) : -1;
    s32 bone = m->m5E_attachBone;
    MATRIX rot;
    MATRIX at;
    s32 k;

    if (s < 0 || slots[s].built != frame) return;
    if (p < 0 || slots[p].built != frame || bone >= slots[p].n || m->m34_visible == 0) {
        slots[s].built = 0;
        return;
    }
    keep();
    if (m->m5D_attachRotation != 0) {
        if (bone != 0) {
            MulMatrix0(&slots[p].final[0], &slots[p].final[bone], &rot);
        } else {
            rot = slots[p].final[0];
        }
        MulMatrix2(&rot, &slots[s].root);
        MulMatrix2(&rot, &slots[s].final[0]);
    }
    if (bone != 0) {
        CompMatrix(&slots[p].root, &slots[p].final[bone], &at);
    } else {
        at = slots[p].root;
    }
    for (k = 0; k < 3; k++) {
        slots[s].root.t[k] = ((at.m[k][0] * m->m6A_attachOffset[0] + at.m[k][1] * m->m6A_attachOffset[1] +
                               at.m[k][2] * m->m6A_attachOffset[2]) >> 12) + at.t[k];
    }
    putBack();
}
