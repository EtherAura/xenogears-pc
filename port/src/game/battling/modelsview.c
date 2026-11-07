/* The viewer runs the game's Battling skeleton loader and animation scheduler.
 * Fight events (projectiles, damage and opponents) belong to a fight world
 * and are not run by this isolated skeleton view. */
#include "modelsview.h"
#include "battling/battling.h"
#include "main/memory.h"

BattlingDataFile* relocateBattlingDataFile(BattlingDataFile* file);
BattlingNode* loadBattlingSkeleton(BattlingDataFile* file);
void setBattlingTextureClutPos(s16 x, s16 y, s16 clutX, s16 clutY);
void resetBattlingAnim(BattlingAnim* anim);
s32 stepBattlingAnim(BattlingAnim* anim, s32 frames);
void updateBattlingGearAnimation(BattlingGear* gear);
MATRIX* createRotationMatrix(SVECTOR* rotation, MATRIX* out);
MATRIX* MulMatrix0(MATRIX* a, MATRIX* b, MATRIX* out);
s32 submitModelForRendering(ModelBlock* block, u8* prims, OTag* ot, s32 mode);

typedef struct {
    BattlingNode* root;
    BattlingGear gear;
    s32 native;
} Playback;

static BattlingNode* skeleton(void* playback) {
    return ((Playback*)playback)->root;
}

void* modelsBattlingLoad(void* data, s32 size) {
    Playback* p;
    BattlingDataFile* file = data;
    u32 params, rates;
    if (size < (s32)sizeof(*file)) return NULL;
    p = allocateHeap((s32)sizeof(*p), 0);
    if (p == NULL) return NULL;
    memset(p, 0, sizeof(*p));
    setBattlingTextureClutPos(0x3C0, 0, 0, 0x1FF);
    relocateBattlingDataFile(data);
    p->root = loadBattlingSkeleton(data);
    if (p->root == NULL) return NULL;
    /* Some directory 31 skeletons have no fight parameters. Keep those raw;
     * never borrow speed data from the following section or another fighter. */
    params = file->m10_params - file->m1C_base;
    rates = file->m14_animParams - file->m1C_base;
    p->native = file->m10_params != 0 && file->m14_animParams != 0 &&
                params >= sizeof(*file) && params <= (u32)size && (u32)size - params >= 14 &&
                rates >= sizeof(*file) && rates <= (u32)size && (u32)size - rates >= 36 * 2;
    p->gear.m5C_model = p->root;
    p->gear.m909_gearId = 0xFF; /* preview all parts; no gear-specific fight visibility */
    p->gear.m15CC_replayCur = &p->gear.m9CC_replay[0];
    if (p->native) {
        p->gear.m7C_animParams = BL_FILE(file, m14_animParams);
        /* startBattlingRound's base scale, normal menu speed (0x100), no
         * movement bonus. updateBattlingGearAnimation applies per-clip rates
         * and blend steps, including fractional frames and zero-speed poses. */
        p->gear.m15F2_speedScale = ((BattlingGearParams*)BL_FILE(file, m10_params))->mC_speedScale;
    }
    return p;
}

s32 modelsBattlingBoneCount(void* root) {
    return root != NULL ? NODE_SKELETON(skeleton(root))->m0_boneCount + 1 : 0;
}

s32 modelsBattlingAnimationCount(void* root) {
    return root != NULL ? NODE_SKELETON(skeleton(root))->m2_animCount : 0;
}

static BattlingAnim* clip(void* root, s32 animation) {
    BattlingSkeleton* s = NODE_SKELETON(skeleton(root));
    return animation >= 0 && animation < s->m2_animCount ? &s->m8_anims[animation] : NULL;
}

void modelsBattlingPlay(void* root, s32 animation) {
    Playback* p = root;
    BattlingAnim* a = clip(root, animation);
    p->gear.m4C_animation = animation;
    p->gear.m4E_lastAnimation = 0xFF;
    p->gear.mD0_status.flags = 0;
    if (a != NULL && a->m0_data != NULL) {
        resetBattlingAnim(a);
    }
}

void modelsBattlingRepeat(void* root, s32 animation) {
    Playback* p = root;
    /* A native looping clip has already requested its restart. Retain its
     * final pose and blend countdown, as the fight's next update does. */
    if (p->native && animation < 36 && (p->gear.mD0_status.flags & 0x10000000)) {
        p->gear.m4C_animation = animation;
        p->gear.mD0_status.bits.b11_animEnd = 0;
    } else {
        modelsBattlingPlay(root, animation);
    }
}

s32 modelsBattlingStep(void* root, s32 animation) {
    Playback* p = root;
    BattlingAnim* a = clip(root, animation);
    if (a == NULL || a->m0_data == NULL) return 1;
    if (!p->native || animation >= 36) return stepBattlingAnim(a, 1);
    /* Stop at this clip's native transition. The viewer's Loop control may
     * replay it; an isolated clip must not switch to the scheduler's next id. */
    if (p->gear.mD0_status.bits.b11_animEnd) return 1;
    updateBattlingGearAnimation(&p->gear);
    return p->gear.mD0_status.bits.b11_animEnd;
}

void modelsBattlingState(void* root, s32 animation, XgVmState* state) {
    BattlingAnim* a = clip(root, animation);
    Playback* p = root;
    state->program = animation;
    state->visibleBones = modelsBattlingBoneCount(root) - 1;
    state->stopped = a == NULL || a->m0_data == NULL ||
                     (p->native && animation < 36 ? p->gear.mD0_status.bits.b11_animEnd
                                                  : a->m12_frame >= a->m0_data->m0_frameCount);
    state->eventFrame = a != NULL ? a->m12_frame : -1;
    state->tracks = a != NULL && a->m0_data != NULL && !state->stopped ? a->m0_data->m6_trackCount : 0;
}

void modelsBattlingTiming(void* root, s32 animation, XgBattlingTiming* timing) {
    Playback* p = root;
    BattlingAnim* a = clip(root, animation);
    memset(timing, 0, sizeof(*timing));
    if (a == NULL || a->m0_data == NULL) return;
    timing->frames = a->m0_data->m0_frameCount;
    timing->native = p->native && animation < 36;
    timing->speed = timing->native ? (u8)(p->gear.m7C_animParams[animation * 2 + 1] *
                                         p->gear.m15F2_speedScale / 256) : 16;
    timing->blend = timing->native ? p->gear.m7C_animParams[animation * 2] : 1;
}

/* The same local transforms as renderBattlingNodeTree, retained without a
 * view matrix so bounds and GLB nodes see the skeleton in model space. */
static void matrices(BattlingNode* root) {
    BattlingSkeleton* s = NODE_SKELETON(root);
    BattlingNode* node;
    VECTOR t;
    s32 i;
    createRotationMatrix(&root->m44_rotation, &root->m4C_localMatrix);
    for (i = 0; i < s->m0_boneCount; i++) {
        node = s->m4_bones[i];
        createRotationMatrix(&node->m44_rotation, &node->mC_worldMatrix);
        t.vx = node->m2C_position.vx;
        t.vy = node->m2C_position.vy;
        t.vz = node->m2C_position.vz;
        TransMatrix(&node->mC_worldMatrix, &t);
        CompMatrix(&node->m8C_parent->m4C_localMatrix, &node->mC_worldMatrix, &node->m4C_localMatrix);
    }
}

static void copyMatrix(MATRIX* m, float out[3][3], float t[3]) {
    s32 i, j;
    for (i = 0; i < 3; i++) {
        for (j = 0; j < 3; j++) {
            out[i][j] = m->m[i][j] / 4096.0f;
        }
        t[i] = m->t[i];
    }
}

s32 modelsBattlingParts(void* root, XgScenePart* out, s32 max) {
    BattlingNode* nodeRoot = skeleton(root);
    BattlingSkeleton* s = NODE_SKELETON(nodeRoot);
    BattlingBoneData* data = s->m18_boneData;
    BattlingNode* node;
    BattlingModel* model;
    XgScenePart* part;
    s32 i, n = 0;
    matrices(nodeRoot);
    for (i = 0; i < s->m0_boneCount && n < max; i++) {
        node = s->m4_bones[i];
        if (node->m0_type != 1) {
            continue;
        }
        model = NODE_MODEL(node);
        part = &out[n++];
        memset(part, 0, sizeof(*part));
        part->block = model->m14_modelBlock;
        part->prims = model->m8_prims[0];
        copyMatrix(&node->m4C_localMatrix, part->m, part->t);
        part->bone = i + 1;
        part->parent = data[i].m0_parent + 1;
        part->enabled = !(model->m0_flags & 1);
        part->mecha = 1;
        part->rig = XG_RIG_BATTLING;
    }
    return n;
}

s32 modelsBattlingBones(void* root, XgSceneBone* out, s32 max) {
    BattlingNode* nodeRoot = skeleton(root);
    BattlingSkeleton* s = NODE_SKELETON(nodeRoot);
    BattlingBoneData* data = s->m18_boneData;
    s32 i, n = s->m0_boneCount + 1;
    if (n > max) {
        n = max;
    }
    matrices(nodeRoot);
    for (i = 0; i < n; i++) {
        memset(&out[i], 0, sizeof(out[i]));
        out[i].enabled = 1;
        if (i == 0) {
            copyMatrix(&nodeRoot->m4C_localMatrix, out[i].m, out[i].t);
            out[i].parent = -1;
            out[i].geometry = -1;
        } else {
            copyMatrix(&s->m4_bones[i - 1]->mC_worldMatrix, out[i].m, out[i].t);
            out[i].parent = data[i - 1].m0_parent + 1;
            out[i].geometry = data[i - 1].m2_model;
        }
    }
    return n;
}

void modelsBattlingRender(void* root, MATRIX* view, MATRIX* light, OTag* ot, s32 odd) {
    BattlingNode* nodeRoot = skeleton(root);
    BattlingSkeleton* s = NODE_SKELETON(nodeRoot);
    BattlingNode* node;
    BattlingModel* model;
    MATRIX pose, lit;
    s32 i;
    matrices(nodeRoot);
    for (i = 0; i < s->m0_boneCount; i++) {
        node = s->m4_bones[i];
        if (node->m0_type != 1) {
            continue;
        }
        model = NODE_MODEL(node);
        if (model->m0_flags & 1) {
            continue;
        }
        CompMatrix(view, &node->m4C_localMatrix, &pose);
        MulMatrix0(light, &node->m4C_localMatrix, &lit);
        SetRotMatrix(&pose);
        SetTransMatrix(&pose);
        SetLightMatrix(&lit);
        submitModelForRendering(model->m14_modelBlock, model->m8_prims[odd], ot, model->m4_renderMode);
    }
}
