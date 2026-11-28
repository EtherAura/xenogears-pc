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
#include "pc_interpolation.h"

/* The halfway pose's own work is the PC's: the game's GTE registers and
 * scratchpad as the game left them, and no time on the stepped clock (the
 * game's timing, its sound among it, stays the Original's; xg-hbc.19.2). */
static GteState gteKept;
static u8 scratchKept[0x400];
void hwClockHold(int on); /* system/hw.c: the PS1 does none of this */
static void keep(void) {
    hwClockHold(1);
    gteSave(&gteKept);
    memcpy(scratchKept, (void*)PSX_SCRATCH(0x1F800000), sizeof scratchKept);
}
static void putBack(void) {
    memcpy((void*)PSX_SCRATCH(0x1F800000), scratchKept, sizeof scratchKept);
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
#define GEAR_BONES INTERP_GEAR_BONES
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
    u32 nativeFrame, drawnFrame;
    BonePose native[GEAR_BONES];
    BonePose drawn[GEAR_BONES];
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
        if (free_ < 0 && (slots[i].mecha == NULL || (frame - slots[i].saved > 2 && frame - slots[i].built > 2 && frame - slots[i].nativeFrame > 2))) {
            free_ = i;
        }
    }
    if (!add || free_ < 0) return -1;
    slots[free_].mecha = m;
    slots[free_].saved = slots[free_].built = slots[free_].kept = 0;
    slots[free_].nativeFrame = slots[free_].drawnFrame = 0;
    return free_;
}

static BonePose bonePose(const sMechaBone* b) {
    BonePose p = {0};
    p.angles = b->m54_rotationAngles;
    memcpy(p.translation, b->m5C_translation, sizeof(p.translation));
    memcpy(p.scale, b->m4C_scale, sizeof(p.scale));
    return p;
}

/* Before the frame's steps (iterations > 0): the pose the frame draws. */
void pcGearPoseBefore(const sLoadedMechas* m, s32 iterations) {
    const sMechaBone* b = m->m4_bones;
    s32 s;
    s32 i;
    s32 n;

    int replay = pcInterpolationHighRate() && pcInterpolationBuilding();
    int halfway = fieldMode || (iterations > 0 && (interpolationEnabled() || pcGameplayBattle60()));

    if (b == NULL || (!halfway && !replay)) return;
    n = b->mA_numBones;
    if (n <= 0 || n > GEAR_BONES || (s = slotFor(m, 1)) < 0) return;
    if (replay) {
        for (i = 0; i < n; i++) slots[s].native[i] = bonePose(&b[i]);
        slots[s].nativeFrame = frame;
    }
    if (!halfway) return;
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

/* The Original field has no native half-step. Capture only the actual bone
 * channels the game just supplied to the matrix builder. */
void pcGearPoseReplayBefore(const sLoadedMechas* m) {
    if (!pcInterpolationHighRate() || !pcInterpolationBuilding()) return;
    const sMechaBone* b = m->m4_bones;
    if (!b || b->mA_numBones == 0 || b->mA_numBones > GEAR_BONES) return;
    s32 s = slotFor(m, 1);
    int i;
    if (s < 0) return;
    for (i = 0; i < b->mA_numBones; i++) slots[s].native[i] = bonePose(&b[i]);
    slots[s].nativeFrame = frame;
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
    if (slots[s].nativeFrame == frame) {
        for (i = 0; i < n; i++) slots[s].drawn[i] = bonePose(&copy[i]);
        slots[s].drawnFrame = frame;
    }
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

/* Recipe for a displayed endpoint. No game pointers cross into the frame.
 * Root offsets from field scripts are already included in the drawn channels. */
typedef struct {
    BonePose channels;
    signed char parent;
    u8 yxz, billboard;
} ReplayBone;

typedef struct {
    s32 scale;
    u16 first;
    u8 count, useBoneScale, attachBone, attachRotation, attachSlot;
    s16 attachOffset[3];
} ReplaySkeleton;

typedef struct {
    MATRIX view;
    float viewOffset[3];
    u8 field, depth;
    ReplaySkeleton skeletons[GEAR_SLOTS]; /* from the drawn Gear up to its root attachment */
    ReplayBone bones[GEAR_SLOTS * GEAR_BONES];
} ReplayPose;

#define REPLAY_PARTS 65536

static int blendSkeleton(const ReplayPose* a, const ReplayPose* b, int level, int k) {
    const ReplaySkeleton* sa = &a->skeletons[level];
    const ReplaySkeleton* sb = &b->skeletons[level];
    int i, axis;
    if (sa->count != sb->count || sa->useBoneScale != sb->useBoneScale ||
        sa->attachBone != sb->attachBone || sa->attachRotation != sb->attachRotation ||
        sa->attachSlot != sb->attachSlot) return 0;
    memset(copy, 0, sa->count * sizeof(*copy));
    copy[0].mA_numBones = sa->count;
    for (i = 0; i < sa->count; i++) {
        const ReplayBone* from = &a->bones[sa->first + i];
        const ReplayBone* to = &b->bones[sb->first + i];
        sMechaBone* bone = &copy[i];
        if (from->parent != to->parent || from->yxz != to->yxz || from->billboard != to->billboard) return 0;
        if (from->parent >= 0) bone->m0_parentBone = &copy[(unsigned)from->parent];
        bone->m6_useYXZRotation = from->yxz;
        bone->m4_needTranslationUpdate = bone->m5_needRotationUpdate = 1;
        if (partAngle(from->channels.angles.vx, to->channels.angles.vx, &bone->m54_rotationAngles.vx, k, REPLAY_PARTS) ||
            partAngle(from->channels.angles.vy, to->channels.angles.vy, &bone->m54_rotationAngles.vy, k, REPLAY_PARTS) ||
            partAngle(from->channels.angles.vz, to->channels.angles.vz, &bone->m54_rotationAngles.vz, k, REPLAY_PARTS)) return 0;
        for (axis = 0; axis < 3; axis++) {
            s64 delta = (s64)to->channels.translation[axis] - from->channels.translation[axis];
            if (delta > JUMP_TRANSLATION || delta < -JUMP_TRANSLATION) return 0;
            bone->m5C_translation[axis] = from->channels.translation[axis] + (s32)(delta * k / REPLAY_PARTS);
            s32 ds = to->channels.scale[axis] - from->channels.scale[axis];
            bone->m4C_scale[axis] = (s16)(from->channels.scale[axis] + (s64)ds * k / REPLAY_PARTS);
            if (sa->useBoneScale && bone->m4C_scale[axis] == 0) return 0;
        }
    }
    return 1;
}

/* Replay the same binding as pcGearPoseAttach, using the parent's interpolated
 * root and socket. Offsets are local to that socket. */
static void replayAttach(const ReplaySkeleton* a, const ReplaySkeleton* b, int k,
                         const MATRIX* parentRoot, const MATRIX* parentRotation, MATRIX* socket) {
    MATRIX placement = *parentRoot;
    if (a->attachRotation) {
        MATRIX rotation = *parentRotation;
        if (a->attachBone) MulMatrix0(&rotation, socket, &rotation);
        MulMatrix2(&rotation, &copy[0].mC_localMatrix);
        MulMatrix2(&rotation, &copy[0].m2C_boneFinalMatrix);
    }
    if (a->attachBone) CompMatrix(&placement, socket, &placement);
    int axis, col;
    for (axis = 0; axis < 3; axis++) {
        s64 delta = 0;
        for (col = 0; col < 3; col++) {
            s32 offset = a->attachOffset[col] + (s32)(((s64)b->attachOffset[col] - a->attachOffset[col]) * k / REPLAY_PARTS);
            delta += (s64)placement.m[axis][col] * offset;
        }
        copy[0].mC_localMatrix.t[axis] = placement.t[axis] + (s32)(delta >> 12);
    }
}

static int buildReplayPose(const void* before, const void* after, double alpha, InterpGearMatrix* out) {
    const ReplayPose* a = before;
    const ReplayPose* b = after;
    if (a->depth != b->depth || a->field != b->field) return 0;
    const int k = (int)(alpha * REPLAY_PARTS + 0.5);
    MATRIX view = a->view, parentRoot, parentRotation, socket, root;
    s32 scale = 0;
    int axis, col, i, level;
    for (axis = 0; axis < 3; axis++) {
        s64 d = (s64)b->view.t[axis] - a->view.t[axis];
        view.t[axis] += (s32)(d * k / REPLAY_PARTS);
        for (col = 0; col < 3; col++)
            view.m[axis][col] += (s16)((s64)(b->view.m[axis][col] - a->view.m[axis][col]) * k / REPLAY_PARTS);
    }
    keep();
    for (level = a->depth - 1; level >= 0; level--) {
        const ReplaySkeleton* sa = &a->skeletons[level];
        const ReplaySkeleton* sb = &b->skeletons[level];
        if (!blendSkeleton(a, b, level, k)) {
            putBack();
            return 0;
        }
        scale = sa->scale + (s32)(((s64)sb->scale - sa->scale) * k / REPLAY_PARTS);
        if (sa->useBoneScale) {
            (a->field ? updateMechAnimSub2 : updateMechAnimSub2_battle)(copy, scale);
        } else {
            (a->field ? updateMechAnimSub1 : updateMechAnimSub1_battle)(copy, scale);
        }
        if (level < a->depth - 1) replayAttach(sa, sb, k, &parentRoot, &parentRotation, &socket);
        if (level > 0) {
            parentRoot = copy[0].mC_localMatrix;
            parentRotation = copy[0].m2C_boneFinalMatrix;
            socket = copy[a->skeletons[level - 1].attachBone].m2C_boneFinalMatrix;
        }
    }
    CompMatrix(&view, &copy[0].mC_localMatrix, &root);
    for (i = 1; i < a->skeletons[0].count; i++) {
        MATRIX m;
        CompMatrix(&root, &copy[i].m2C_boneFinalMatrix, &m);
        int billboard = a->bones[i].billboard;
        for (axis = 0; axis < 3; axis++) {
            for (col = 0; col < 3; col++) {
                s32 rotation = m.m[axis][col];
                if (billboard) rotation = col == 1 && billboard == 1 ? root.m[axis][col] : axis == col ? scale : 0;
                out[i].m[axis][col] = rotation / 4096.0f;
            }
            out[i].t[axis] = m.t[axis] + a->viewOffset[axis] +
                            (float)(alpha * (b->viewOffset[axis] - a->viewOffset[axis]));
        }
    }
    putBack();
    return 1;
}

static int captureSkeleton(ReplayPose* recipe, const sLoadedMechas* m, s32 pose, unsigned first) {
    s32 s = slotFor(m, 0);
    const sMechaBone* bones = m->m4_bones;
    if (s < 0 || slots[s].nativeFrame != frame || !bones || bones->mA_numBones > GEAR_BONES) return 0;
    const BonePose* channels = pose >= 0 && slots[s].drawnFrame == frame ? slots[s].drawn : slots[s].native;
    ReplaySkeleton* skel = &recipe->skeletons[recipe->depth++];
    skel->scale = m->m1C_scale;
    skel->useBoneScale = m->m37_useBoneScale != 0;
    skel->count = bones->mA_numBones;
    skel->first = first;
    skel->attachSlot = m->m5C_attachMecha;
    if (skel->attachSlot != 0xFF) {
        skel->attachBone = m->m5E_attachBone;
        skel->attachRotation = m->m5D_attachRotation;
        memcpy(skel->attachOffset, m->m6A_attachOffset, sizeof(skel->attachOffset));
    }
    int i;
    for (i = 0; i < skel->count; i++) {
        ReplayBone* dst = &recipe->bones[first + i];
        const sMechaBone* bone = &bones[i];
        memset(dst, 0, sizeof(*dst));
        dst->channels = channels[i];
        dst->yxz = bone->m6_useYXZRotation;
        dst->billboard = bone->m52_billboardMode;
        dst->parent = bone->m0_parentBone ? bone->m0_parentBone - bones : -1;
    }
    return skel->count;
}

unsigned pcGearPoseDraw(const sLoadedMechas* m, const MATRIX* view, s32 pose, int field,
                       sLoadedMechas* const* mechas, unsigned count) {
    if (!pcInterpolationGearFrame(field)) return 0;
    ReplayPose recipe;
    int axis;
    memset(&recipe, 0, offsetof(ReplayPose, bones));
    memcpy(recipe.view.m, view->m, sizeof(view->m));
    memcpy(recipe.view.t, view->t, sizeof(view->t));
    double offset[3] = {0};
    gtePrecisionViewOffsetGet(offset);
    for (axis = 0; axis < 3; axis++) recipe.viewOffset[axis] = (float)offset[axis];
    recipe.field = field;

    u64 visited = 0;
    unsigned total = 0;
    const sLoadedMechas* next = m;
    int half = pose >= 0;
    for (;;) {
        int bones = captureSkeleton(&recipe, next, pose, total);
        if (!bones) return (unsigned)-1;
        total += bones;
        unsigned parent = next->m5C_attachMecha;
        if (parent == 0xFF) break;
        unsigned socket = next->m5E_attachBone;
        if (parent >= count || parent >= 64 || (visited & (1ULL << parent)) || recipe.depth >= GEAR_SLOTS) return (unsigned)-1;
        visited |= 1ULL << parent;
        next = mechas[parent];
        if (!next || !next->m4_bones || socket >= next->m4_bones->mA_numBones) return (unsigned)-1;
        /* A drawn half-step attachment only uses half-step ancestors. */
        pose = half ? pcGearPoseFor(next) : -1;
        if (half && pose < 0) return (unsigned)-1;
    }
    return pcInterpolationGear(m, m->m0_model, &recipe,
        offsetof(ReplayPose, bones) + total * sizeof(ReplayBone), buildReplayPose);
}
