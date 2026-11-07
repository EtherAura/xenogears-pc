/*
 * The model and animation viewers' game side (models.h): a scene of models
 * set up, animated and drawn by the game's own code in a sandbox.
 */

#include "models.h"

#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common.h"
#include "main/filesystem.h"
#include "field_mecha/mecha.h"
#include "main/memory.h"
#include "main/model.h"
#include "pc_convert.h"
#include "psyq.h"
#include "models_vm.h"
#include "sandbox.h"
#include "../game/battling/modelsview.h"

/* ---- the game code the scene runs ------------------------------------------------------------ */

/* src/main/model.c */
s32 fieldModelRelocation(Model* model);
void initModel1(ModelBlock* b, u8** start, u8** end);
void initModel2(ModelBlock* b, u8* prims, s32 mode);
s32 submitModelForRendering(ModelBlock* b, u8* prims, OTag* ot, s32 renderMode);
s32 uploadTextureToVram(void* data, s16 mode, s16 x, s16 y, s16 mode2, s16 clutX, s16 clutY);
extern s32 objectClippingMask;
extern s32 screenClippingX;
extern s32 screenClippingY;
extern s32 gDepthDivider;

/* src/main/filesystem.c: the PC-read path of loadImageFileToVram, one sector
 * at a time from the slot table at streamSectorSlots. */
void processImageFileSectorPc(s32 arg0, s32 arg1);
extern s32 streamSectorSlotCount;
extern u8* readDestination;
extern u16 streamNextDataSequence; /* the next sector's sequence number */
extern s32 readInProgress;

/* src/field_mecha (the Gear overlay) */
extern MATRIX* mechaFinalizeVar0; /* the colour matrix renderMechas sets (Noah: mechaFinalizeVar0) */
/* src/battle: the arena's renderer (the battle's environment, its bones as
 * the bone placements put them, billboards and the background layer). */
void renderBattleScenePolys(sMechaInitVar4* blocks, sMechaBone* bones, MATRIX* m, MATRIX* light, s32 arg4, OTag* ot,
                            s32 oddOrEven, s32 otSize);
/* port/src/game/field_mecha/modelsview.c: the overlay's and main's state
 * that the platform layer cannot name (data the port's link generates). */
s32 modelsViewSoundStubs(void);
s32 modelsViewTracksInUse(void);
/* port/src/game/battle/modelsview.c: the battle's copy of the Gear code. */
s32 modelsBattleInit(MATRIX* colour, s32 backR, s32 backG, s32 backB);
void* modelsBattleLoad(s32 slot, void* anim, void* model, s16 tpageX, s16 tpageY, s16 clutX, s16 clutY);
s32 battleFxViewTrailSegments(void);
void modelsBattleExtraFile(s32 operand, s32 param, s32* file);
s32 modelsBattleSetExtra(s32 slot, void* file);
void modelsBattlePlay(s32 slot, s32 animation);
void modelsBattleRender(MATRIX* view, MATRIX* light, OTag* ot, s32 oddOrEven, s32 step);
void* modelsBattleMecha(s32 slot);
u32 modelsBattleRigGeneration(s32 slot);
s32 modelsBattleTracksInUse(void);
sMechaDataTable2* pcBattleMechaData2(void* raw, s32 fresh); /* battle/battle.h */

/* What one OT entry holds: the render loops index it by z >> gDepthDivider (2). */
#define SCENE_OT 0x4000
#define MAX_STEPS 256
#define MAX_STATIC 256
#define MAX_MECHAS 10
#define TRACK_POOL 0x400
#define HEAP_BYTES (48u << 20)
#define MAX_PARTS 512
#define MAX_MORPHS 512

/* main/mime.c's native working structures (never disc-format data). */
typedef struct MorphData {
    s32 (*scale)(struct MorphData*);
    s32 target, current, step, reserved[4];
} MorphData;
typedef struct {
    ModelBlock* block;
    SVECTOR* vertices;
    SVECTOR* normals;
    s32 count;
    MorphData* data;
} Morph;
Morph* initModelDynamicVertices(ModelBlock* block, s32 flags);
void updateDynamicVertices(Morph* morph);
void dynamicVertices_updateVertices(SVECTOR* vertices, void* offsets, s32 count, s32 scale);

enum { STEP_PACKED, STEP_FIELD_TEXTURES, STEP_TIMS, STEP_TIM_BUNDLE, STEP_MECHA, STEP_MODEL, STEP_BATTLING };

typedef struct {
    int kind;
    uint8_t* data;
    int size;
    uint8_t* data2;
    int size2;
    int arg[3];
} Step;

typedef struct {
    ModelBlock* block;
    u8* prims[2];
    int renderMode;
    Morph* morph;
} Static;

struct XgScene {
    XgSandbox* sb;
    Step steps[MAX_STEPS];
    int stepCount;
    /* built: pointers into the sandbox's heap, valid while built */
    int built;
    int mechas;
    int battle;  /* the mechas are the battle's (its loader, VM and renderer) */
    void* battling; /* Battling's skeleton and isolated animation scheduler */
    sLoadedMechas* arena; /* an arena: set up by the Gear loader, drawn as the battle draws it */
    Static statics[MAX_STATIC];
    int staticCount;
    int morphCount;
    MorphData* morphs[MAX_MORPHS];
    int morphWeight[MAX_MORPHS], morphGoal[MAX_MORPHS], morphStep[MAX_MORPHS];
    OTag* ot;
    MATRIX* colour;
    int odd;
    int programs; /* the first mecha's animation programs */
    int neutralised; /* ops made harmless (parameters past the arrays they index) */
    uint8_t* extra; /* the extra animation file the next program gets (battle op 0x04's) */
    int extraSize;
    u8* animRaw;   /* the first mecha's animation file in the sandbox's heap */
    int animRawSize;
    const uint8_t* animFile; /* the same as the disc has it (the step's copy: before our patches) */
    u8* sentinel;  /* the keyframe the padded tables give for ids the file lacks */
    int sentinelSize;
    int usedSentinel; /* the program playing has read it */
    int anim, frame;
    int shadow;
    int trails; /* dedicated battle-effects viewer: native trail events */
    XgCamera camera; /* the last one drawn with (ticks use it too) */
    int haveCamera;
    /* Each part's primitives as set up (before any drawing writes screen
     * positions and lit colours into them), in xgSceneParts' order. */
    u8* setup[MAX_PARTS];
    void* setupBlock[MAX_PARTS];
    int setupCount;
};

static void renderInside(XgScene* s, const XgCamera* c, int flag);
static XgCamera defaultCamera(void);
static int partsInside(XgScene* s, XgScenePart* out, int max);

/* ---- the scene ------------------------------------------------------------------------------- */

XgScene* xgSceneCreate(void) {
    static const char* const modules[] = {"field_mecha", "battle", "battling", NULL};
    XgScene* s = calloc(1, sizeof(XgScene));
    if (s == NULL) {
        return NULL;
    }
    s->sb = xgSandboxCreate(modules, HEAP_BYTES);
    if (s->sb == NULL) {
        free(s);
        return NULL;
    }
    s->anim = -1;
    return s;
}

static void freeSetup(XgScene* s) {
    for (int i = 0; i < s->setupCount; i++) {
        free(s->setup[i]);
        s->setup[i] = NULL;
    }
    s->setupCount = 0;
}

void xgSceneClear(XgScene* s) {
    freeSetup(s);
    free(s->extra);
    s->extra = NULL;
    s->extraSize = 0;
    for (int i = 0; i < s->stepCount; i++) {
        free(s->steps[i].data);
        free(s->steps[i].data2);
    }
    memset(s->steps, 0, sizeof(s->steps));
    s->stepCount = 0;
    s->built = 0;
    s->anim = -1;
    s->frame = 0;
    memset(s->morphWeight, 0, sizeof(s->morphWeight));
    memset(s->morphGoal, 0, sizeof(s->morphGoal));
    memset(s->morphStep, 0, sizeof(s->morphStep));
    s->morphCount = 0;
}

void xgSceneDestroy(XgScene* s) {
    if (s != NULL) {
        xgSceneClear(s);
        xgSandboxDestroy(s->sb);
        free(s);
    }
}

static uint8_t* copyOf(const uint8_t* data, int size) {
    if (data == NULL || size <= 0) {
        return NULL;
    }
    uint8_t* p = malloc((size_t)size);
    if (p != NULL) {
        memcpy(p, data, (size_t)size);
    }
    return p;
}

static Step* addStep(XgScene* s, int kind, const uint8_t* data, int size) {
    if (s->stepCount >= MAX_STEPS || data == NULL || size <= 0) {
        return NULL;
    }
    Step* st = &s->steps[s->stepCount];
    st->data = copyOf(data, size);
    if (st->data == NULL) {
        return NULL;
    }
    st->kind = kind;
    st->size = size;
    s->stepCount++;
    s->built = 0;
    return st;
}

XgScene* xgSceneCopy(const XgScene* source) {
    XgScene* s = xgSceneCreate();
    if (s == NULL) return NULL;
    for (int i = 0; i < source->stepCount; i++) {
        const Step* from = &source->steps[i];
        Step* to = addStep(s, from->kind, from->data, from->size);
        if (to == NULL) { xgSceneDestroy(s); return NULL; }
        memcpy(to->arg, from->arg, sizeof(to->arg));
        if (from->size2 > 0) {
            to->data2 = copyOf(from->data2, from->size2);
            if (to->data2 == NULL) { xgSceneDestroy(s); return NULL; }
            to->size2 = from->size2;
        }
    }
    memcpy(s->morphWeight, source->morphWeight, sizeof(s->morphWeight));
    /* Only requested export clips advance the copy. */
    memcpy(s->morphGoal, source->morphWeight, sizeof(s->morphGoal));
    s->camera = source->camera;
    s->haveCamera = source->haveCamera;
    s->shadow = source->shadow;
    s->trails = source->trails;
    if (source->extraSize > 0) {
        s->extra = copyOf(source->extra, source->extraSize);
        if (s->extra == NULL) { xgSceneDestroy(s); return NULL; }
        s->extraSize = source->extraSize;
    }
    return s;
}

void xgSceneAddPackedTextures(XgScene* s, const uint8_t* data, int size) {
    addStep(s, STEP_PACKED, data, size);
}

void xgSceneAddFieldTextures(XgScene* s, const uint8_t* data, int size) {
    addStep(s, STEP_FIELD_TEXTURES, data, size);
}

void xgSceneAddTims(XgScene* s, const uint8_t* data, int size) {
    addStep(s, STEP_TIMS, data, size);
}

void xgSceneAddTimBundle(XgScene* s, const uint8_t* data, int size) {
    addStep(s, STEP_TIM_BUNDLE, data, size);
}

void xgSceneAddMecha(XgScene* s, const uint8_t* model, int modelSize, const uint8_t* anim, int animSize, int how) {
    Step* st = addStep(s, STEP_MECHA, model, modelSize);
    if (st != NULL) {
        st->data2 = copyOf(anim, animSize);
        st->size2 = st->data2 != NULL ? animSize : 0;
        st->arg[0] = how;
    }
}

void xgSceneAddModel(XgScene* s, const uint8_t* data, int size, int block, int initMode, int renderMode) {
    Step* st = addStep(s, STEP_MODEL, data, size);
    if (st != NULL) {
        st->arg[0] = block;
        st->arg[1] = initMode;
        st->arg[2] = renderMode;
    }
}

void xgSceneAddBattling(XgScene* s, const uint8_t* data, int size) {
    addStep(s, STEP_BATTLING, data, size);
}

int xgSceneBattling(const XgScene* s) {
    return s->battling != NULL;
}

void xgSceneBattlingTiming(XgScene* s, int animation, XgBattlingTiming* timing) {
    memset(timing, 0, sizeof(*timing));
    if (!s->built || s->battling == NULL) return;
    xgSandboxEnter(s->sb);
    modelsBattlingTiming(s->battling, animation, timing);
    xgSandboxLeave(s->sb);
}

void xgSceneRepeatBattling(XgScene* s) {
    if (!s->built || s->battling == NULL || s->anim < 0) return;
    xgSandboxEnter(s->sb);
    modelsBattlingRepeat(s->battling, s->anim);
    xgSandboxLeave(s->sb);
    s->frame = 0;
}

int xgSceneMorphCount(const XgScene* s) {
    return s->built ? s->morphCount : 0;
}

int xgSceneMorphWeight(XgScene* s, int target) {
    return target >= 0 && target < s->morphCount ? s->morphWeight[target] : 0;
}

static Morph* partMorph(XgScene* s, void* block) {
    for (int i = 0; i < s->staticCount; i++) {
        if (s->statics[i].block == block) return s->statics[i].morph;
    }
    return NULL;
}

int xgScenePartMorphs(XgScene* s, void* block, int* targets, int max) {
    Morph* m = partMorph(s, block);
    if (m == NULL) return 0;
    if (max < m->count) return -1;
    for (int i = 0; i < m->count; i++) {
        targets[i] = -1;
        for (int k = 0; k < s->morphCount; k++) {
            if (s->morphs[k] == &m->data[i]) targets[i] = k;
        }
        if (targets[i] < 0) return -1;
    }
    return m->count;
}

int xgScenePartMorphPositions(XgScene* s, void* block, int target, float* xyz, int vertices) {
    Morph* m = partMorph(s, block);
    if (m == NULL || xyz == NULL || vertices != m->block->m2_numVertices || target < -1 || target >= m->count) return 0;
    SVECTOR* copy = malloc(sizeof(SVECTOR) * (size_t)vertices);
    if (copy == NULL) return 0;
    memcpy(copy, m->vertices, sizeof(SVECTOR) * (size_t)vertices);
    if (target >= 0) {
        ModelDynVertEntry* e = &m->block->m1C_dynamicVertices->m4_entries[target];
        dynamicVertices_updateVertices(copy, e->m4_positions, e->m0_count, 4096);
    }
    for (int i = 0; i < vertices; i++) {
        xyz[3 * i] = copy[i].vx;
        xyz[3 * i + 1] = copy[i].vy;
        xyz[3 * i + 2] = copy[i].vz;
    }
    free(copy);
    return 1;
}

int xgScenePartVertices(void* block, float* xyz, int max) {
    ModelBlock* b = block;
    if (xyz == NULL) return b->m2_numVertices;
    int count = b->m2_numVertices < max ? b->m2_numVertices : max;
    for (int i = 0; i < count; i++) {
        xyz[3 * i] = b->m8_vertices[i].vx;
        xyz[3 * i + 1] = b->m8_vertices[i].vy;
        xyz[3 * i + 2] = b->m8_vertices[i].vz;
    }
    return count;
}

void xgSceneSetMorph(XgScene* s, int target, int weight, int ticks) {
    if (!s->built || target < 0 || target >= s->morphCount) {
        return;
    }
    weight = weight < 0 ? 0 : weight > 4096 ? 4096 : weight;
    xgSandboxEnter(s->sb);
    MorphData* d = s->morphs[target];
    d->target = weight;
    d->step = ticks > 0 ? (abs(weight - d->current) + ticks - 1) / ticks : 0;
    if (ticks <= 0) {
        d->current = weight;
        s->morphWeight[target] = weight;
    }
    s->morphGoal[target] = d->target;
    s->morphStep[target] = d->step;
    for (int k = 0; k < s->morphCount; k++) {
        s->morphs[k]->step = 0;
    }
    for (int i = 0; i < s->staticCount; i++) {
        if (s->statics[i].morph != NULL) {
            updateDynamicVertices(s->statics[i].morph);
        }
    }
    for (int k = 0; k < s->morphCount; k++) {
        s->morphs[k]->step = s->morphStep[k];
    }
    xgSandboxLeave(s->sb);
}

int xgSceneBuilt(const XgScene* s) {
    return s->built;
}

void xgSceneEnter(XgScene* s) {
    xgSandboxEnter(s->sb);
}

void xgSceneLeave(XgScene* s) {
    xgSandboxLeave(s->sb);
}

XgVgpu* xgSceneGpu(XgScene* s) {
    return xgSandboxGpu(s->sb);
}

void xgSceneSetTrails(XgScene* s, int on) { s->trails = on != 0; }
int xgSceneTrailSegments(XgScene* s) {
    if (!s->built || !s->battle) return 0;
    xgSandboxEnter(s->sb);
    int n = battleFxViewTrailSegments();
    xgSandboxLeave(s->sb);
    return n;
}

void xgSceneSetShadow(XgScene* s, int on) {
    s->shadow = on;
}

/* ---- the steps ------------------------------------------------------------------------------- */

/* The field's texture file, a sector at a time through the loader's own
 * handler (its state starts as a fresh world leaves it: arg4..9 zero, the
 * positions the descriptors give, as the field asks for). Its slot table as
 * the CD read fills it: the handler keeps a unit's descriptor sector in its
 * slot (state 2, its row counts are read from there) while the data sectors
 * come through the others, and frees them all when the unit is done. */
#define FIELD_SLOTS 4
static int uploadFieldTextures(const uint8_t* data, int size) {
    SectorSlot* slots = xgSandboxAlloc(sizeof(*slots) * FIELD_SLOTS);
    u8* sectors = xgSandboxAlloc(0x800 * FIELD_SLOTS);
    if (slots == NULL || sectors == NULL) {
        return 0;
    }
    streamSectorSlots = slots;
    streamSectorSlotCount = FIELD_SLOTS;
    readDestination = sectors;
    streamNextDataSequence = 0;
    readInProgress = 1;
    for (int at = 0; at + 0x800 <= size && readInProgress > 0; at += 0x800) {
        int i = 0;
        while (i < FIELD_SLOTS && slots[i].m0_state != 0) {
            i++;
        }
        if (i == FIELD_SLOTS) {
            break; /* every slot held: not the format the handler takes */
        }
        memcpy(sectors + i * 0x800, data + at, 0x800);
        slots[i].m0_state = 1;
        slots[i].m2_sequence = streamNextDataSequence;
        processImageFileSectorPc(0, 0);
    }
    int done = readInProgress == 0;
    readInProgress = 0;
    streamSectorSlots = NULL;
    streamSectorSlotCount = 0;
    readDestination = NULL;
    return done;
}

/* TIMs back to back: CLUT and pixels LoadImage'd where each says (the field's
 * uploadFieldImages, whose OpenTIM/ReadTIM keep a cursor the sandbox does
 * not swap: the same uploads, read here). */
static int uploadTims(const uint8_t* data, int size) {
    int at = 0, count = 0;
    while (at + 8 <= size) {
        u32 id, flags;
        memcpy(&id, data + at, 4);
        memcpy(&flags, data + at + 4, 4);
        if (id != 0x10) {
            break;
        }
        int p = at + 8;
        for (int part = (flags & 8) ? 0 : 1; part < 2; part++) {
            u32 bytes;
            if (p + 12 > size) {
                return count;
            }
            memcpy(&bytes, data + p, 4);
            if (bytes < 12 || p + (int)bytes > size) {
                return count;
            }
            RECT r;
            memcpy(&r, data + p + 4, sizeof(RECT));
            void* pixels = xgSandboxCopy(data + p + 12, bytes - 12);
            if (pixels != NULL) {
                LoadImage(&r, pixels);
                freeHeap(pixels);
            }
            p += (int)bytes;
        }
        at = p;
        count++;
    }
    return count;
}

/* The sound driver's effect state, empty (see above), and the battle effect
 * sequence id op 0x3C reads. */
static void soundStubs(void) {
    modelsViewSoundStubs();
}

/* The animation VM takes keyframe ids without a bound (mechaGetKeyframe:
 * below 0x40 the file's table, above it the extra file's, which only the
 * battle loads), and some programs in the field's files name keyframes they
 * do not hold: programs the game never runs there. So that the viewer can
 * play every program, the tables are padded with a copy of keyframe 0 (a
 * pose, its tracks and events); a program that reads it is reported. */
#define KEYFRAME_IDS 0x40
#define EXTRA_IDS 0xC0

static void* paddedTable(void* const* real, int count, int slots, u8* sentinel) {
    void** t = xgSandboxAlloc(sizeof(void*) * (size_t)(1 + slots));
    if (t != NULL) {
        t[0] = (void*)(uintptr_t)count;
        for (int i = 0; i < slots; i++) {
            t[1 + i] = i < count && real != NULL ? real[i] : sentinel;
        }
    }
    return t;
}

static void padKeyframes(XgScene* s, sMechaDataTable2* data2, const u8* raw, int rawSize) {
    sMechaDataTable2_4_4_array* kf = data2->m4_anim->m4_keyframes;
    int count = kf->m0_count;
    if (count <= 0 || count > KEYFRAME_IDS) {
        return;
    }
    u8* first = (u8*)kf->m4_keyframes[0];
    u8* end = (u8*)raw + rawSize;
    u8* next = count > 1 ? (u8*)kf->m4_keyframes[1] : end;
    if (first < raw || first >= end || next <= first || next > end) {
        next = end;
    }
    s->sentinelSize = (int)(next - first);
    s->sentinel = xgSandboxCopy(first, (size_t)s->sentinelSize);
    if (s->sentinel == NULL) {
        return;
    }
    /* A missing-keyframe substitute supplies the pose/tracks only. Its
     * original event offsets need not lie in this keyframe's extent. */
    ((sMechaDataTable2_4_4*)s->sentinel)->m12_numEvents = 0;
    ((sMechaDataTable2_4_4*)s->sentinel)->m14_eventsOffset = 0;
    void* t = paddedTable((void* const*)kf->m4_keyframes, count, KEYFRAME_IDS, s->sentinel);
    if (t != NULL) {
        data2->m4_anim->m4_keyframes = t;
    }
}

/* The scene's mecha k: the field's (loadedMechas) or the battle's. */
static sLoadedMechas* mechaAt(const XgScene* s, int k) {
    if (k < 0 || k >= (s->battle ? 32 : MAX_MECHAS)) {
        return NULL;
    }
    return s->battle ? (sLoadedMechas*)modelsBattleMecha(k) : loadedMechas[k];
}

/* Ops the battle's copy would run against the hardware or wait on forever in
 * a sandbox, in the sandbox's copy of the program:
 *   0x04 (read an extra animation file from the CD): op 0x30 (zero a counter
 *        word: its own operand), the file given before the program plays;
 *   0x06 (free the extra file): op 0x02, so the given file stays;
 *   0x07 (stop the CD read), 0x72 (wait for an effect's set-up): op 0x02 (a
 *        signal to the action queue, which is empty here). */
static void patchInlineEvents(u16* op, const u8* file, int size, const sLoadedMechas* m, int trails);

static void patchBattleProgram(u16* program, int words, const sLoadedMechas* m, const u8* file, int size,
                               int* neutralised, int trails) {
    int bones = m->m4_bones->mA_numBones;
    int at = 0;
    for (int ops = 0; ops < 4096 && at < words; ops++) {
        int n = xgMechaOpLength(program + at, words - at, 1);
        if (n <= 0) {
            break;
        }
        int op = program[at] & 0xFF;
        patchInlineEvents(program + at, file, size, m, trails);
        if (!xgMechaOpSafe(program + at, words - at, 1, bones)) {
            xgMechaOpNeutralise(program + at, n);
            (*neutralised)++;
        } else if (op == 0x04) {
            program[at] = (u16)((program[at] & 0xFF00) | 0x30);
        } else if (op == 0x3C) {
            /* The silent viewer has no loaded sound banks; source 2 can
             * otherwise dereference an absent extra-file sequence. */
            xgMechaOpNeutralise(program + at, n);
        } else if (op == 0x06 || op == 0x07 || op == 0x72) {
            program[at] = (u16)((program[at] & 0xFF00) | 0x02);
        }
        at += n;
    }
}

/* A keyframe's event stream (sMechaAnimEvent records, types 1-9), made
 * safe in the sandbox's copy where the interpreters index without a bound or
 * spawn what this world lacks:
 *   - type 1 (battle: an effect sprite from banks the battle loads,
 *     createSavePointMeshData_mode5 and createSavePointMeshData_mode6): damage mask bit 0, so the game's own gate
 *     (isMechaEventAllowedForHitResult) passes over it on this world's plain
 *     hit;
 *   - type 5: an inert sound-bank selector (the viewer has no sound banks);
 *   - types 3/4: bounded native trail events are retained in effects scenes;
 *     other scenes omit them. Split-off rigs have no emitter pool (the PC
 *     decoder checks it); type 9 stops have no colour-animation pool;
 *   - invalid type 2/7 light/bone indices: a harmless root visibility event.
 * Record lengths stay unchanged, so the native event decoder keeps time. */
static void patchEventStream(u8* e, int events, const u8* file, int size, const sLoadedMechas* m, int trails) {
    int bones = m->m4_bones->mA_numBones;
    for (int i = 0; i < events && e >= file && e + 6 <= file + size; i++) {
        int type = e[2], index = e[3], on = e[4] != 0;
        int n = type == 1 ? 0x14 : type == 2 ? (on ? 0x12 : 6) : type == 3 || type == 4 || type == 9 ? (on ? 0x1C : 6)
              : type == 5 ? 8 : type == 6 ? 4 : type == 7 ? 6 : type == 8 ? 0xA : 0;
        if (n == 0 || e + n > file + size) {
            break;
        }
        if ((type == 3 || type == 4) && trails && index < m->m10C_num110 && m->m110_trails != NULL &&
            (!on || (e[5] < bones && e[6] > 0 && e[6] <= 32 && e[7] > 0))) {
            e += n; /* original decoder, emitter and segment pool */
            continue;
        }
        if (type == 1) {
            if (trails < 2) e[4] |= 1;
            e[4] &= ~0x80; /* no gear-specific effect-id lookup */
            if (trails < 2 || e[5] >= bones) e[5] = 0;
        } else if (type == 5) {
            /* Bank 4 is the native decoder's inert fallback. Keep the
             * eight-byte record without looking up a missing bank. */
            e[5] = 4;
            e[7] = 0;
        } else if ((type == 2 && !on && index >= 2) || ((type == 3 || type == 4 || type == 9) && !on) ||
                   (type == 7 && e[4] >= bones)) {
            e[2] = 7;
            e[4] = 0;
        } else if ((type == 3 || type == 4) && on) {
            /* Split-off bone models have no emitter pool, even when
             * their source has one. A skipped colour start preserves
             * this record's length in both interpreters. */
            e[2] = 9;
            e[3] = 0xFF;
        } else if (type == 2 && on && e[6] >= bones) {
            e[6] = 0;
        }
        e += n;
    }
}

/* Every keyframe's stream (and so the extra file's). */
static void patchEvents(sMechaDataTable2_4_4_array* keyframes, int count, const u8* file, int size,
                        const sLoadedMechas* m, int trails) {
    for (int k = 0; keyframes != NULL && k < count; k++) {
        u8* kf = (u8*)keyframes->m4_keyframes[k];
        if (kf >= file && kf + 0x18 <= file + size) {
            sMechaDataTable2_4_4* h = (sMechaDataTable2_4_4*)kf;
            if (h->m14_eventsOffset != 0) {
                patchEventStream(kf + h->m14_eventsOffset, h->m12_numEvents, file, size, m, trails);
            }
        }
    }
}

/* The streams op 0x63 carries in a program (its parameter: the count). */
static void patchInlineEvents(u16* op, const u8* file, int size, const sLoadedMechas* m, int trails) {
    if ((op[0] & 0xFF) == 0x63) {
        patchEventStream((u8*)(op + 2), op[0] >> 8, file, size, m, trails);
    }
}

/* Each program, bounded by the next distinct program or the file end. */
static int programWords(void** programs, int count, int k, const u8* file, int size) {
    const u8* start = programs[k];
    const u8* end = file + size;
    for (int i = 0; i < count; i++) {
        const u8* next = programs[i];
        if (next > start && next < end) {
            end = next;
        }
    }
    return (int)((end - start) / 2);
}

static int patchBattlePrograms(void** programs, int count, const u8* file, int size, const sLoadedMechas* m, int trails) {
    int neutralised = 0;
    for (int k = 0; programs != NULL && k < count; k++) {
        const u8* p = programs[k];
        if (p != NULL && p >= file && p < file + size) {
            patchBattleProgram(programs[k], programWords(programs, count, k, file, size), m, file, size, &neutralised, trails);
        }
    }
    return neutralised;
}

/* A program ending in rejoin is normally started on an op 0x15 split-off
 * model. The selector also lets it run directly on the source model, which
 * has no sourceEntryId and cannot rejoin/free itself. Stop that selected
 * program at the return; other programs still rejoin their split models. */
static void patchStandaloneProgram(XgScene* s, sLoadedMechas* m, int anim) {
    u16* p = m->m8_bytecodeTable[anim];
    if ((u8*)p < s->animRaw || (u8*)p >= s->animRaw + s->animRawSize) {
        return;
    }
    int words = programWords((void**)m->m8_bytecodeTable, s->programs, anim, s->animRaw, s->animRawSize);
    for (int at = 0, ops = 0; at < words && ops < 4096; ops++) {
        int n = xgMechaOpLength(p + at, words - at, 1);
        if (n <= 0) {
            break;
        }
        if ((p[at] & 0xFF) == 0x16) {
            p[at] = 0;
            s->neutralised++;
        } else if ((p[at] & 0xFF) == 0x14 && n == 2 && (p[at + 1] >> 8) == anim) {
            int target = p[at] >> 8;
            if (target == 0 || target >= 0xFD) {
                /* With only this rig present, a broadcast to the attack
                 * target can resolve to the source and recurse forever.
                 * A same-program self-call has no isolated-model motion. */
                xgMechaOpNeutralise(p + at, n);
                s->neutralised++;
            }
        }
        at += n;
    }
}

/* The field's copy: only the parameters (it neither reads the CD nor spawns
 * effects). */
static int patchFieldPrograms(void** programs, int count, const u8* file, int size, const sLoadedMechas* m) {
    int bones = m->m4_bones->mA_numBones;
    int neutralised = 0;
    for (int k = 0; programs != NULL && k < count; k++) {
        u16* p = programs[k];
        if (p == NULL || (u8*)p < file || (u8*)p >= file + size) {
            continue;
        }
        int words = programWords(programs, count, k, file, size), at = 0;
        for (int ops = 0; ops < 4096 && at < words; ops++) {
            int n = xgMechaOpLength(p + at, words - at, 0);
            if (n <= 0) {
                break;
            }
            patchInlineEvents(p + at, file, size, m, 0);
            if (!xgMechaOpSafe(p + at, words - at, 0, bones)) {
                xgMechaOpNeutralise(p + at, n);
                neutralised++;
            }
            at += n;
        }
    }
    return neutralised;
}

static int inSentinel(const XgScene* s, const void* p) {
    return s->sentinel != NULL && (const u8*)p >= s->sentinel && (const u8*)p < s->sentinel + s->sentinelSize;
}

/* Inside: whether the first mecha's tracks or events read the sentinel. */
static int readsSentinel(XgScene* s) {
    sLoadedMechas* m = s->mechas > 0 ? mechaAt(s, 0) : NULL;
    if (m == NULL || m->m4_bones == NULL || s->sentinel == NULL) {
        return 0;
    }
    if (m->m98_eventFrame >= 0 && inSentinel(s, m->mA4_eventStream)) {
        return 1;
    }
    for (int i = 0; i < m->m4_bones->mA_numBones; i++) {
        for (int k = 0; k < 3; k++) {
            sMechaInitVar2Sub* t = m->m4_bones[i].m70_animTracks[k];
            if (t != NULL && t->m0_inUse && (t->m2_type & 0xF) <= 2 && inSentinel(s, t->m4.stream.m4_start)) {
                return 1;
            }
        }
    }
    return 0;
}

#define BACK_COLOUR 0x3C

/* The extra keyframes (ids 0x40 up) of a battle mecha: its extra file's,
 * padded with the sentinel. */
static void padExtraKeyframes(XgScene* s, sLoadedMechas* m) {
    if (s->sentinel == NULL) {
        return;
    }
    sMechaDataTable2_4_4_array* t = m->m18_extraKeyframes;
    int count = t != NULL ? t->m0_count : 0;
    count = count < 0 || count > EXTRA_IDS ? 0 : count;
    void* padded = paddedTable(t != NULL ? (void* const*)t->m4_keyframes : NULL, count, EXTRA_IDS, s->sentinel);
    if (padded != NULL) {
        m->m18_extraKeyframes = padded;
    }
}

void* xgModelsBattleEffectLoad(int slot, const uint8_t* model, int modelSize, const uint8_t* anim, int animSize) {
    XgScene s = {0};
    if (modelSize < 24 || animSize < 16) return NULL;
    u8* data1 = xgSandboxCopy(model, (size_t)modelSize);
    u8* raw = xgSandboxCopy(anim, (size_t)animSize);
    if (!data1 || !raw) return NULL;
    sMechaDataTable2* data2 = pcBattleMechaData2(raw, 1);
    padKeyframes(&s, data2, raw, animSize);
    sLoadedMechas* m = modelsBattleLoad(slot, raw, data1, 0x300, 0x100, 0, 0x1DB);
    if (!m || !m->m4_bones) return NULL;
    int programs = data2->m4_anim->m0_count - 1;
    patchBattlePrograms((void**)m->m8_bytecodeTable, programs, raw, animSize, m, 2);
    sMechaDataTable2_4_4_array* kf = data2->m4_anim->m4_keyframes;
    patchEvents(kf, kf->m0_count < KEYFRAME_IDS ? kf->m0_count : KEYFRAME_IDS, raw, animSize, m, 2);
    padExtraKeyframes(&s, m);
    m->m38_syncFlag = m->m22_isBattleGear = 1;
    return m;
}

int xgModelsBattleEffectExtra(int slot, const uint8_t* extra, int size) {
    XgScene s = {0};
    sLoadedMechas* m = modelsBattleMecha(slot);
    if (!m || size < 16) return 0;
    /* Retain the model's padded fallback before replacing its extra table. */
    if (m->m18_extraKeyframes && m->m18_extraKeyframes->m0_count < EXTRA_IDS)
        s.sentinel = (u8*)m->m18_extraKeyframes->m4_keyframes[EXTRA_IDS - 1];
    u8* raw = xgSandboxCopy(extra, (size_t)size);
    if (!raw || !modelsBattleSetExtra(slot, raw)) return 0;
    sMechaExtraAnim* x = m->mC_extraAnim;
    int count = (int)(uintptr_t)x->m4_anim[0];
    patchBattlePrograms((void**)&x->m4_anim[2], count > 1 && count < 256 ? count - 1 : 0, raw, size, m, 2);
    sMechaDataTable2_4_4_array* kf = m->m18_extraKeyframes;
    if (kf) patchEvents(kf, kf->m0_count < EXTRA_IDS ? kf->m0_count : EXTRA_IDS, raw, size, m, 2);
    /* These retail clips contain every referenced extra keyframe. Keep a
     * bounded table even when an unused program names an absent one. */
    padExtraKeyframes(&s, m);
    return 1;
}

/* A battle Gear or enemy through the battle's own copy of the Gear code. */
static int addBattleMecha(XgScene* s, Step* st, char* why, int whySize) {
    if (s->mechas > 0 && !s->battle) {
        snprintf(why, (size_t)whySize, "a battle Gear with field ones");
        return 0;
    }
    if (st->data2 == NULL) {
        snprintf(why, (size_t)whySize, "no animation file");
        return 0;
    }
    if (!s->battle) {
        if (!modelsBattleInit(s->colour, BACK_COLOUR, BACK_COLOUR, BACK_COLOUR)) {
            snprintf(why, (size_t)whySize, "the sandbox's heap is full");
            return 0;
        }
        s->battle = 1;
    }
    u8* data1 = xgSandboxCopy(st->data, (size_t)st->size);
    u8* raw = xgSandboxCopy(st->data2, (size_t)st->size2);
    if (data1 == NULL || raw == NULL || *(u32*)raw < 2) {
        snprintf(why, (size_t)whySize, data1 == NULL || raw == NULL ? "the sandbox's heap is full" : "not an animation file");
        return 0;
    }
    /* Converted first (the loader's flag 4 finds the same tables), so that
     * the keyframes can be padded as for the field. */
    sMechaDataTable2* data2 = pcBattleMechaData2(raw, 1);
    padKeyframes(s, data2, raw, st->size2);
    int slot = s->mechas;
    if (slot == 0) {
        s->programs = data2->m4_anim->m0_count > 1 ? data2->m4_anim->m0_count - 1 : 0;
        s->animRaw = raw;
        s->animRawSize = st->size2;
        s->animFile = st->data2;
    }
    sLoadedMechas* m = modelsBattleLoad(slot, raw, data1, (s16)(0x240 - slot * 0x40), 0x100, 0, (s16)(0xFC + slot));
    if (m == NULL || m->m4_bones == NULL) {
        snprintf(why, (size_t)whySize, "the battle's Gear loader gave no skeleton");
        return 0;
    }
    s->neutralised += patchBattlePrograms((void**)m->m8_bytecodeTable, s->programs, raw, st->size2, m, s->trails);
    sMechaDataTable2_4_4_array* kf = data2->m4_anim->m4_keyframes;
    patchEvents(kf, kf->m0_count < KEYFRAME_IDS ? kf->m0_count : KEYFRAME_IDS, raw, st->size2, m, s->trails);
    padExtraKeyframes(s, m);
    s->mechas++;
    return 1;
}

/* A Gear-like model through the Gear overlay's own loader. */
static int addMecha(XgScene* s, Step* st, char* why, int whySize) {
    if (s->mechas >= MAX_MECHAS) {
        snprintf(why, (size_t)whySize, "too many models");
        return 0;
    }
    if (st->size < 24 || *(const u32*)st->data < 4 || *(const u32*)st->data > 8) {
        snprintf(why, (size_t)whySize, "not a model bundle");
        return 0;
    }
    if (st->arg[0] == XG_MECHA_BATTLE) {
        return addBattleMecha(s, st, why, whySize);
    }
    if (s->battle) {
        snprintf(why, (size_t)whySize, "a field Gear with battle ones");
        return 0;
    }
    if (s->mechas == 0) {
        mechaInitOverlay(TRACK_POOL);
        mechaFinalizeVar0 = s->colour;
    }
    u8* data1 = xgSandboxCopy(st->data, (size_t)st->size);
    sMechaDataTable2* data2 = NULL;
    u16 flags;
    if (st->arg[0] == XG_MECHA_ARENA || st->data2 == NULL) {
        /* As the battle loader sets up an arena: no animation, the textures
         * at their own places, the primitives as the archive has them. */
        flags = 0xC4;
    } else {
        u8* raw = xgSandboxCopy(st->data2, (size_t)st->size2);
        if (raw == NULL || *(u32*)raw < 2) {
            snprintf(why, (size_t)whySize, "not an animation file");
            return 0;
        }
        /* Converted here so that the loader neither converts it nor loads its
         * sound sequence into the driver (loadSequence touches BIOS events):
         * flag 4, as for a mecha sharing an earlier one's file. */
        data2 = pcConvertMechaData2(raw);
        flags = 4;
        padKeyframes(s, data2, raw, st->size2);
        if (s->mechas == 0) {
            s->animRaw = raw;
            s->animRawSize = st->size2;
            s->animFile = st->data2;
            /* The programs: the animation table's count less its keyframe slot. */
            s->programs = data2->m4_anim->m0_count > 1 ? data2->m4_anim->m0_count - 1 : 0;
        }
    }
    if (data1 == NULL) {
        snprintf(why, (size_t)whySize, "the sandbox's heap is full");
        return 0;
    }
    int slot = s->mechas;
    /* The field's places for its Gears' textures (loadMechaOverlay_finalize). */
    SVECTOR pos = {0, 0, 0, 0};
    mechaInitNewMecha(slot, flags, data2, (sMechaDataTable1*)data1, (s16)(0x240 - slot * 0x40), 0x100, 0,
                      (s16)(0xFC + slot), &pos);
    sLoadedMechas* m = loadedMechas[slot];
    if (m == NULL || m->m4_bones == NULL) {
        snprintf(why, (size_t)whySize, "the Gear loader gave no skeleton");
        return 0;
    }
    if (data2 != NULL && s->sentinel != NULL) {
        /* The extra keyframes (ids 0x40 up): the padding alone. */
        void* t = paddedTable(NULL, 0, EXTRA_IDS, s->sentinel);
        if (t != NULL) {
            m->m18_extraKeyframes = t; /* entry id - 0x40 */
        }
    }
    if (data2 != NULL) {
        int programs = data2->m4_anim->m0_count > 1 ? data2->m4_anim->m0_count - 1 : 0;
        s->neutralised += patchFieldPrograms((void**)data2->m4_anim->m8_bytecode, programs, s->animRaw,
                                             s->animRawSize, m);
        sMechaDataTable2_4_4_array* kf = data2->m4_anim->m4_keyframes;
        patchEvents(kf, kf->m0_count < KEYFRAME_IDS ? kf->m0_count : KEYFRAME_IDS, s->animRaw, s->animRawSize, m, 0);
    }
    if (st->arg[0] == XG_MECHA_ARENA) {
        /* As the battle keeps its environment: out of the mecha list (the
         * loader leaves the animation state unset without an animation),
         * drawn by the battle's own environment renderer. */
        initMechaTransforms1(m, NULL, NULL, NULL);
        loadedMechas[slot] = NULL;
        s->arena = m;
        /* The battle loader's bone placements: member 4, (x, y, z, billboard) per bone. */
        u32* t1 = (u32*)data1;
        if (t1[0] >= 5) {
            s16* src = PC_RELOC_ENTRY(t1, 5);
            sMechaBone* bones = m->m4_bones;
            for (int i = 1; i < bones->mA_numBones; i++, src += 4) {
                bones[i].m5C_translation[0] = src[0];
                bones[i].m5C_translation[1] = src[1];
                bones[i].m5C_translation[2] = src[2];
                bones[i].m52_billboardMode = src[3];
            }
        }
        return 1;
    }
    s->mechas++;
    return 1;
}

static int addModel(XgScene* s, Step* st, char* why, int whySize) {
    int parts = st->size >= 16 ? *(const s32*)st->data : 0;
    if (parts <= 0 || parts > 4096) {
        snprintf(why, (size_t)whySize, "not a model archive");
        return 0;
    }
    u8* raw = xgSandboxCopy(st->data, (size_t)st->size);
    if (raw == NULL) {
        snprintf(why, (size_t)whySize, "the sandbox's heap is full");
        return 0;
    }
    fieldModelRelocation((Model*)raw);
    ModelBlock* blocks = pcModelBlocks(raw);
    int from = st->arg[0] < 0 ? 0 : st->arg[0], to = st->arg[0] < 0 ? parts : st->arg[0] + 1;
    if (to > parts) {
        snprintf(why, (size_t)whySize, "no block %d", st->arg[0]);
        return 0;
    }
    for (int i = from; i < to && s->staticCount < MAX_STATIC; i++) {
        Static* m = &s->statics[s->staticCount];
        m->block = &blocks[i];
        m->morph = initModelDynamicVertices(m->block, 0);
        if (m->morph != NULL) {
            int first = s->morphCount;
            for (int k = 0; k < m->morph->count && s->morphCount < MAX_MORPHS; k++) {
                int at = s->morphCount++;
                MorphData* d = &m->morph->data[k];
                s->morphs[at] = d;
                d->current = s->morphWeight[at];
                d->target = s->morphWeight[at];
                d->step = 0;
            }
            updateDynamicVertices(m->morph);
            for (int k = 0; k < m->morph->count && first + k < MAX_MORPHS; k++) {
                m->morph->data[k].target = s->morphGoal[first + k];
                m->morph->data[k].step = s->morphStep[first + k];
            }
        }
        initModel1(m->block, &m->prims[0], &m->prims[1]);
        if (m->prims[0] == NULL) {
            snprintf(why, (size_t)whySize, "the sandbox's heap is full");
            return 0;
        }
        initModel2(m->block, m->prims[0], st->arg[1]);
        memcpy(m->prims[1], m->prims[0], (size_t)m->block->m34_primBufferSize);
        m->renderMode = st->arg[2];
        s->staticCount++;
    }
    return 1;
}

/* The light the scene is set up and drawn with: the gear shop's
 * (initGearShopView): one white light from above, some ambient. */
static void sceneLight(MATRIX* light, MATRIX* colour) {
    memset(light, 0, sizeof(*light));
    memset(colour, 0, sizeof(*colour));
    light->m[0][0] = 0x546;
    light->m[0][1] = -0xE39;
    light->m[0][2] = 0x546;
    colour->m[0][0] = 0x600;
    colour->m[1][0] = 0x600;
    colour->m[2][0] = 0x600;
}

int xgSceneBuild(XgScene* s, char* why, int whySize) {
    why[0] = '\0';
    xgSandboxReset(s->sb, 1);
    s->built = 0;
    s->mechas = 0;
    s->battle = 0;
    s->battling = NULL;
    s->arena = NULL;
    s->programs = 0;
    s->neutralised = 0;
    s->sentinel = NULL;
    s->sentinelSize = 0;
    s->usedSentinel = 0;
    s->animRaw = NULL;
    s->animRawSize = 0;
    s->animFile = NULL;
    s->staticCount = 0;
    s->morphCount = 0;
    s->anim = -1;
    s->frame = 0;
    s->odd = 0;
    xgSandboxEnter(s->sb);
    soundStubs();
    s->ot = xgSandboxAlloc(SCENE_OT * sizeof(OTag));
    s->colour = xgSandboxAlloc(sizeof(MATRIX));
    MATRIX light;
    int ok = s->ot != NULL && s->colour != NULL;
    if (ok) {
        sceneLight(&light, s->colour);
        SetLightMatrix(&light);
        SetColorMatrix(s->colour);
        SetBackColor(BACK_COLOUR, BACK_COLOUR, BACK_COLOUR);
        gDepthDivider = 2;
    } else {
        snprintf(why, (size_t)whySize, "the sandbox's heap is full");
    }
    for (int i = 0; ok && i < s->stepCount; i++) {
        Step* st = &s->steps[i];
        switch (st->kind) {
        case STEP_PACKED: {
            void* t = xgSandboxCopy(st->data, (size_t)st->size);
            if (t != NULL) {
                uploadTextureToVram(t, 0, 0, 0, 0, 0, 0);
            }
            break;
        }
        case STEP_FIELD_TEXTURES:
            if (!uploadFieldTextures(st->data, st->size)) {
                fprintf(stderr, "extras: models: the field texture file did not upload whole\n");
            }
            break;
        case STEP_TIMS:
            uploadTims(st->data, st->size);
            break;
        case STEP_TIM_BUNDLE: {
            /* [count][offsets from its start], a TIM at each (loadMenuImageBundle,
             * uploadFieldImages: the same uploads, read here). */
            u32 n = st->size >= 4 ? *(const u32*)st->data : 0;
            for (u32 k = 0; n < 4096 && k < n && 4 + 4 * (int)k + 4 <= st->size; k++) {
                u32 at = ((const u32*)st->data)[1 + k];
                if (at < (u32)st->size) {
                    uploadTims(st->data + at, st->size - (int)at);
                }
            }
            break;
        }
        case STEP_MECHA:
            ok = addMecha(s, st, why, whySize);
            break;
        case STEP_MODEL:
            ok = addModel(s, st, why, whySize);
            break;
        case STEP_BATTLING: {
            void* raw = xgSandboxCopy(st->data, (size_t)st->size);
            s->battling = raw != NULL ? modelsBattlingLoad(raw, st->size) : NULL;
            ok = s->battling != NULL;
            if (ok) {
                s->programs = modelsBattlingAnimationCount(s->battling);
            } else {
                snprintf(why, (size_t)whySize, "the sandbox's heap is full");
            }
            break;
        }
        }
    }
    freeSetup(s);
    if (ok) {
        /* The primitives as set up, then a first pose (the bones' matrices). */
        XgScenePart* parts = malloc(sizeof(XgScenePart) * MAX_PARTS);
        int n = parts != NULL ? partsInside(s, parts, MAX_PARTS) : 0;
        for (int i = 0; i < n; i++) {
            size_t size = (size_t)((ModelBlock*)parts[i].block)->m34_primBufferSize;
            s->setup[i] = malloc(size);
            if (s->setup[i] != NULL) {
                memcpy(s->setup[i], parts[i].prims, size);
            }
            s->setupBlock[i] = parts[i].block;
        }
        s->setupCount = n;
        free(parts);
        XgCamera c = defaultCamera();
        renderInside(s, &c, -1);
    }
    xgSandboxLeave(s->sb);
    s->built = ok && (s->mechas > 0 || s->arena != NULL || s->battling != NULL || s->staticCount > 0);
    if (ok && !s->built) {
        snprintf(why, (size_t)whySize, "nothing to show");
    }
    return s->built;
}

/* ---- matrices -------------------------------------------------------------------------------- */

typedef struct {
    float m[3][3], t[3];
} Mat;

static Mat fromGte(const MATRIX* g) {
    Mat r;
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) {
            r.m[i][j] = g->m[i][j] / 4096.0f;
        }
        r.t[i] = (float)g->t[i];
    }
    return r;
}

/* a then b: a . b (b applied first). */
static Mat compose(const Mat* a, const Mat* b) {
    Mat r;
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) {
            r.m[i][j] = a->m[i][0] * b->m[0][j] + a->m[i][1] * b->m[1][j] + a->m[i][2] * b->m[2][j];
        }
        r.t[i] = a->m[i][0] * b->t[0] + a->m[i][1] * b->t[1] + a->m[i][2] * b->t[2] + a->t[i];
    }
    return r;
}

static void toGte(const Mat* a, MATRIX* g) {
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) {
            float v = a->m[i][j] * 4096.0f;
            v = v > 32767.0f ? 32767.0f : v < -32768.0f ? -32768.0f : v;
            g->m[i][j] = (s16)lrintf(v);
        }
        g->t[i] = (s32)lrintf(a->t[i]);
    }
}

/* The view: the world turned by yaw about y, then by pitch about x, the
 * target `distance` in front of the eye. */
static Mat viewMatrix(const XgCamera* c) {
    const float pi = 3.14159265358979f;
    float y = c->yaw * pi / 2048.0f, p = c->pitch * pi / 2048.0f;
    float cy = cosf(y), sy = sinf(y), cp = cosf(p), sp = sinf(p);
    Mat r;
    /* Rx(p) . Ry(y) */
    r.m[0][0] = cy;
    r.m[0][1] = 0;
    r.m[0][2] = sy;
    r.m[1][0] = sp * sy;
    r.m[1][1] = cp;
    r.m[1][2] = -sp * cy;
    r.m[2][0] = -cp * sy;
    r.m[2][1] = sp;
    r.m[2][2] = cp * cy;
    for (int i = 0; i < 3; i++) {
        r.t[i] = -(r.m[i][0] * c->target[0] + r.m[i][1] * c->target[1] + r.m[i][2] * c->target[2]);
    }
    r.t[2] += c->distance;
    return r;
}

/* ---- parts and bones ------------------------------------------------------------------------- */


/* The skeleton the bones, animation and export follow: the first mecha's,
 * else the arena's. */
static sLoadedMechas* firstSkeleton(XgScene* s) {
    return s->mechas > 0 ? mechaAt(s, 0) : s->arena;
}

/* Inside: every part the scene draws, where it draws it. */
static int partsInside(XgScene* s, XgScenePart* out, int max) {
    int n = s->battling != NULL ? modelsBattlingParts(s->battling, out, max) : 0;
    for (int k = 0; k < (s->battle ? 32 : s->mechas); k++) {
        sLoadedMechas* m = mechaAt(s, k);
        if (m == NULL || m->m4_bones == NULL || m->m0_model == NULL) {
            continue;
        }
        sMechaBone* bones = m->m4_bones;
        Mat root = fromGte(&bones[0].mC_localMatrix);
        for (int i = 1; i < bones->mA_numBones && n < max; i++) {
            sMechaBone* b = &bones[i];
            if (b->m8_geometryId == 0xFFFF || b->m8_geometryId >= m->m0_model->m4_count) {
                continue;
            }
            Mat local = fromGte(&b->m2C_boneFinalMatrix);
            Mat w = compose(&root, &local);
            XgScenePart* p = &out[n++];
            p->block = m->m0_model->m0_blocks[b->m8_geometryId];
            p->prims = b->m68_prims[0];
            p->mecha = 1;
            memcpy(p->m, w.m, sizeof(p->m));
            memcpy(p->t, w.t, sizeof(p->t));
            p->bone = i;
            p->rig = k;
            p->generation = s->battle ? modelsBattleRigGeneration(k) : 0;
            p->parent = b->m0_parentBone != NULL ? (int)(b->m0_parentBone - bones) : 0;
            p->enabled = b->m7_isEnabled != 0 && m->m34_visible != 0;
        }
    }
    if (s->arena != NULL && s->arena->m4_bones != NULL) {
        /* The battle's renderer places each bone by its final matrix alone. */
        sMechaBone* bones = s->arena->m4_bones;
        for (int i = 1; i < bones->mA_numBones && n < max; i++) {
            sMechaBone* b = &bones[i];
            if (b->m8_geometryId == 0xFFFF || b->m8_geometryId >= s->arena->m0_model->m4_count) {
                continue;
            }
            Mat w = fromGte(&b->m2C_boneFinalMatrix);
            XgScenePart* p = &out[n++];
            p->block = s->arena->m0_model->m0_blocks[b->m8_geometryId];
            p->prims = b->m68_prims[0];
            p->mecha = 1;
            memcpy(p->m, w.m, sizeof(p->m));
            memcpy(p->t, w.t, sizeof(p->t));
            p->bone = i;
            p->rig = XG_RIG_ARENA;
            p->generation = 0;
            p->parent = b->m0_parentBone != NULL ? (int)(b->m0_parentBone - bones) : 0;
            p->enabled = b->m7_isEnabled != 0;
        }
    }
    for (int i = 0; i < s->staticCount && n < max; i++) {
        XgScenePart* p = &out[n++];
        memset(p, 0, sizeof(*p));
        p->block = s->statics[i].block;
        p->prims = s->statics[i].prims[0];
        p->m[0][0] = p->m[1][1] = p->m[2][2] = 1;
        p->bone = -1;
        p->rig = -1;
        p->parent = -1;
        p->enabled = 1;
    }
    for (int i = 0; i < n; i++) {
        if (i < s->setupCount && s->setupBlock[i] == out[i].block && s->setup[i] != NULL) {
            out[i].prims = s->setup[i];
        }
    }
    return n;
}

int xgSceneParts(XgScene* s, XgScenePart* out, int max) {
    if (!s->built) {
        return 0;
    }
    xgSandboxEnter(s->sb);
    int n = partsInside(s, out, max);
    xgSandboxLeave(s->sb);
    return n;
}

int xgModelsFieldMechaParts(XgScenePart* out, int max) {
    XgScene s;
    memset(&s, 0, sizeof(s));
    s.mechas = MAX_MECHAS;
    return partsInside(&s, out, max);
}

static int mechaBones(XgScene* s, sLoadedMechas* m, XgSceneBone* out, int max) {
    int n = 0;
    if (m != NULL && m->m4_bones != NULL) {
        sMechaBone* bones = m->m4_bones;
        for (int i = 0; i < bones->mA_numBones && n < max; i++, n++) {
            sMechaBone* b = &bones[i];
            Mat l = fromGte(&b->mC_localMatrix);
            if (i == 0 && m == s->arena) {
                memset(&l, 0, sizeof(l));
                l.m[0][0] = l.m[1][1] = l.m[2][2] = 1;
            }
            memcpy(out[n].m, l.m, sizeof(l.m));
            memcpy(out[n].t, l.t, sizeof(l.t));
            out[n].parent = i == 0 ? -1 : b->m0_parentBone != NULL ? (int)(b->m0_parentBone - bones) : 0;
            out[n].geometry = i > 0 && b->m8_geometryId != 0xFFFF ? b->m8_geometryId : -1;
            out[n].enabled = i == 0 ? m->m34_visible != 0 : b->m7_isEnabled != 0;
        }
    }
    return n;
}

int xgSceneBones(XgScene* s, XgSceneBone* out, int max) {
    if (!s->built) {
        return 0;
    }
    xgSandboxEnter(s->sb);
    XgCamera c = s->haveCamera ? s->camera : defaultCamera();
    renderInside(s, &c, -1); /* the matrices of the current state */
    int n = s->battling != NULL ? modelsBattlingBones(s->battling, out, max) : 0;
    if (n == 0) n = mechaBones(s, firstSkeleton(s), out, max);
    xgSandboxLeave(s->sb);
    return n;
}

int xgSceneRigs(XgScene* s, XgSceneRig* out, int max) {
    if (!s->built) return 0;
    xgSandboxEnter(s->sb);
    XgCamera c = s->haveCamera ? s->camera : defaultCamera();
    renderInside(s, &c, -1);
    XgSceneRig rigs[XG_SCENE_MAX_RIGS];
    int n = 0;
    if (s->battling != NULL) rigs[n++] = (XgSceneRig){XG_RIG_BATTLING, -1, modelsBattlingBoneCount(s->battling), 0};
    for (int k = 0; k < (s->battle ? 32 : s->mechas); k++) {
        sLoadedMechas* m = mechaAt(s, k);
        if (m == NULL || m->m4_bones == NULL) continue;
        rigs[n++] = (XgSceneRig){k, s->battle ? m->m21_sourceEntryId : -1, m->m4_bones->mA_numBones,
                                 s->battle ? modelsBattleRigGeneration(k) : 0};
    }
    if (s->arena != NULL && s->arena->m4_bones != NULL) {
        rigs[n++] = (XgSceneRig){XG_RIG_ARENA, -1, s->arena->m4_bones->mA_numBones, 0};
    }
    if (n <= max) memcpy(out, rigs, sizeof(*out) * (size_t)n);
    xgSandboxLeave(s->sb);
    return n <= max ? n : -1;
}

int xgSceneRigBones(XgScene* s, const XgSceneRig* rig, XgSceneBone* out, int max) {
    if (!s->built) return 0;
    if (max < rig->bones) return -1;
    xgSandboxEnter(s->sb);
    int n = 0;
    if (rig->slot == XG_RIG_BATTLING && s->battling != NULL) {
        n = modelsBattlingBones(s->battling, out, max);
    } else {
        sLoadedMechas* m = rig->slot == XG_RIG_ARENA ? s->arena : mechaAt(s, rig->slot);
        unsigned int generation = s->battle && rig->slot < 32 ? modelsBattleRigGeneration(rig->slot) : 0;
        if (m != NULL && generation == rig->generation) n = mechaBones(s, m, out, max);
    }
    xgSandboxLeave(s->sb);
    return n;
}

void xgSceneInfo(XgScene* s, XgSceneInfo* out) {
    memset(out, 0, sizeof(*out));
    if (!s->built) {
        return;
    }
    XgScenePart* parts = malloc(sizeof(XgScenePart) * MAX_PARTS);
    if (parts == NULL) {
        return;
    }
    xgSandboxEnter(s->sb);
    int n = partsInside(s, parts, MAX_PARTS);
    out->parts = n;
    float mn[3] = {1e30f, 1e30f, 1e30f}, mx[3] = {-1e30f, -1e30f, -1e30f};
    for (int i = 0; i < n; i++) {
        ModelBlock* b = parts[i].block;
        out->polygons += b->m4_numPrims;
        for (int v = 0; v < b->m2_numVertices; v++) {
            SVECTOR* sv = &b->m8_vertices[v];
            for (int k = 0; k < 3; k++) {
                float w = parts[i].m[k][0] * sv->vx + parts[i].m[k][1] * sv->vy + parts[i].m[k][2] * sv->vz +
                          parts[i].t[k];
                mn[k] = w < mn[k] ? w : mn[k];
                mx[k] = w > mx[k] ? w : mx[k];
            }
        }
    }
    if (n == 0 || mn[0] > mx[0]) {
        memset(mn, 0, sizeof(mn));
        memset(mx, 0, sizeof(mx));
    }
    memcpy(out->min, mn, sizeof(mn));
    memcpy(out->max, mx, sizeof(mx));
    sLoadedMechas* m = firstSkeleton(s);
    if (m != NULL && m->m4_bones != NULL) {
        out->bones = m->m4_bones->mA_numBones;
        out->animations = s->programs;
    }
    if (s->battling != NULL) {
        out->bones = modelsBattlingBoneCount(s->battling);
        out->animations = s->programs;
    }
    xgSandboxLeave(s->sb);
    free(parts);
}

/* ---- drawing and ticks ----------------------------------------------------------------------- */

/* Inside: the GTE's screen and the render loops' tests for the camera, the
 * view matrix, the light matrix. */
static void setupCamera(const XgCamera* c, MATRIX* view, MATRIX* light) {
    int w = c->w < 16 ? 16 : c->w > 1024 ? 1024 : c->w;
    int h = c->h < 16 ? 16 : c->h > 1024 ? 1024 : c->h;
    objectClippingMask = 0;
    screenClippingX = w;
    screenClippingY = (h - 1) << 16;
    gDepthDivider = 2;
    SetGeomOffset(w / 2, h / 2);
    float zoom = c->zoom > 0.01f ? c->zoom : 1.0f;
    SetGeomScreen((s32)lrintf(fminf(h * zoom, 60000.0f)));
    Mat v = viewMatrix(c);
    toGte(&v, view);
    MATRIX colour;
    sceneLight(light, &colour);
    if (c->lightFollows) {
        /* The light's directions are in the world: turned back by the view's
         * rotation they stay where they are on the screen. */
        Mat l = fromGte(light), r = v;
        memset(r.t, 0, sizeof(r.t));
        Mat rl = compose(&l, &r);
        memset(rl.t, 0, sizeof(rl.t));
        toGte(&rl, light);
    }
}

static void hideShadows(XgScene* s) {
    for (int k = 0; k < (s->battle ? 32 : s->mechas); k++) {
        sLoadedMechas* m = mechaAt(s, k);
        if (m != NULL) {
            if (s->shadow) {
                m->m4A_flags &= ~1;
            } else {
                m->m4A_flags |= 1;
            }
        }
    }
}

/* Inside: the scene into the OT; flag 1 advances the mechas one tick (the
 * field's call), -1 not. */
static void renderInside(XgScene* s, const XgCamera* c, int flag) {
    MATRIX view, light;
    setupCamera(c, &view, &light);
    ClearOTagR(s->ot, SCENE_OT);
    SetBackColor(BACK_COLOUR, BACK_COLOUR, BACK_COLOUR);
    if (s->battling != NULL) {
        if (flag > 0 && s->anim >= 0) {
            modelsBattlingStep(s->battling, s->anim);
        }
        modelsBattlingRender(s->battling, &view, &light, s->ot, s->odd);
    }
    if (s->mechas > 0 && s->battle) {
        /* The battle's frame is a vblank: a step is two of them, the first
         * drawing nothing that is kept. */
        hideShadows(s);
        if (flag > 0) {
            modelsBattleRender(&view, &light, s->ot, s->odd, 1);
            ClearOTagR(s->ot, SCENE_OT);
            s->odd ^= 1;
        }
        modelsBattleRender(&view, &light, s->ot, s->odd, flag > 0);
    } else if (s->mechas > 0) {
        hideShadows(s);
        renderMechas(&view, &light, s->ot, s->odd, flag);
    }
    if (s->arena != NULL) {
        updateMechAnimSub1(s->arena->m4_bones, s->arena->m1C_scale);
        renderBattleScenePolys(s->arena->m0_model, s->arena->m4_bones, &view, &light, 0, s->ot, s->odd, SCENE_OT);
        gDepthDivider = 2;
    }
    Mat lm = fromGte(&light);
    for (int i = 0; i < s->staticCount; i++) {
        Static* m = &s->statics[i];
        if (flag > 0 && m->morph != NULL) {
            updateDynamicVertices(m->morph);
        }
        MATRIX g;
        SetRotMatrix(&view);
        SetTransMatrix(&view);
        lm.t[0] = lm.t[1] = lm.t[2] = 0;
        toGte(&lm, &g);
        SetLightMatrix(&g);
        submitModelForRendering(m->block, m->prims[s->odd], s->ot, m->renderMode);
    }
    s->odd ^= 1;
}

static XgCamera defaultCamera(void) {
    XgCamera c;
    memset(&c, 0, sizeof(c));
    c.distance = 4000;
    c.zoom = 1;
    c.w = 640;
    c.h = 480;
    return c;
}

int xgSceneDraw(XgScene* s, const XgCamera* c) {
    XgVgpu* gpu = xgSandboxGpu(s->sb);
    if (!s->built) {
        xgVgpuClearPrims(gpu);
        return 0;
    }
    s->camera = *c;
    s->haveCamera = 1;
    xgSandboxEnter(s->sb);
    xgVgpuClearPrims(gpu);
    int w = c->w < 16 ? 16 : c->w > 1024 ? 1024 : c->w;
    int h = c->h < 16 ? 16 : c->h > 1024 ? 1024 : c->h;
    xgVgpuSetArea(gpu, 0, 0, w - 1, h - 1, 0, 0);
    renderInside(s, c, -1);
    DrawOTag(&s->ot[SCENE_OT - 1]);
    xgSandboxLeave(s->sb);
    return xgVgpuPrimCount(gpu);
}

void xgSceneTick(XgScene* s, int ticks) {
    if (!s->built || (s->mechas == 0 && s->battling == NULL && s->morphCount == 0) || ticks <= 0) {
        return;
    }
    XgCamera c = s->haveCamera ? s->camera : defaultCamera();
    xgSandboxEnter(s->sb);
    for (int i = 0; i < ticks; i++) {
        if (s->battling != NULL && s->anim >= 0) {
            modelsBattlingStep(s->battling, s->anim);
        } else {
            renderInside(s, &c, 1);
        }
        s->frame++;
        s->usedSentinel |= readsSentinel(s);
        for (int k = 0; k < s->morphCount; k++) {
            s->morphWeight[k] = s->morphs[k]->current;
        }
    }
    xgSandboxLeave(s->sb);
}

int xgSceneFrame(const XgScene* s) {
    return s->frame;
}

int xgSceneAnimation(const XgScene* s) {
    return s->anim;
}

int xgScenePlay(XgScene* s, int anim, char* why, int whySize) {
    if (!xgSceneBuild(s, why, whySize)) {
        return 0;
    }
    if ((s->mechas == 0 && s->battling == NULL) || anim < 0) {
        return 1;
    }
    if (anim >= s->programs) {
        snprintf(why, (size_t)whySize, "no animation %d", anim);
        return 0;
    }
    xgSandboxEnter(s->sb);
    if (s->battling != NULL) {
        modelsBattlingPlay(s->battling, anim);
    } else if (s->battle) {
        sLoadedMechas* m = mechaAt(s, 0);
        if (s->extra != NULL && m != NULL) {
            /* The extra file op 0x04 would read, as ops 0x04 and 0x05 leave it. */
            void* file = xgSandboxCopy(s->extra, (size_t)s->extraSize);
            if (file != NULL && modelsBattleSetExtra(0, file)) {
                sMechaExtraAnim* x = m->mC_extraAnim;
                int count = (int)(uintptr_t)x->m4_anim[0];
                s->neutralised += patchBattlePrograms((void**)&x->m4_anim[2], count > 1 && count < 256 ? count - 1 : 0,
                                                      file, s->extraSize, m, s->trails);
                sMechaDataTable2_4_4_array* ekf = m->m18_extraKeyframes;
                if (ekf != NULL) {
                    patchEvents(ekf, ekf->m0_count < EXTRA_IDS ? ekf->m0_count : EXTRA_IDS, file, s->extraSize, m, s->trails);
                }
                padExtraKeyframes(s, m);
            }
        }
        if (m != NULL) {
            patchStandaloneProgram(s, m, anim);
        }
        modelsBattlePlay(0, anim);
    } else {
        mechaPlayAnimation(0, 0, anim);
    }
    s->usedSentinel = readsSentinel(s);
    xgSandboxLeave(s->sb);
    s->anim = anim;
    s->frame = 0;
    return 1;
}

void xgSceneSeek(XgScene* s, int frame) {
    if (!s->built || (s->mechas == 0 && s->battling == NULL)) {
        return;
    }
    if (frame < s->frame) {
        char why[128];
        int anim = s->anim;
        if (!xgScenePlay(s, anim, why, sizeof(why))) {
            return;
        }
    }
    xgSceneTick(s, frame - s->frame);
}

void xgSceneVm(XgScene* s, XgVmState* out) {
    memset(out, 0, sizeof(*out));
    out->program = -1;
    out->pc = -1;
    out->eventFrame = -1;
    if (!s->built || (s->mechas == 0 && s->battling == NULL)) {
        return;
    }
    xgSandboxEnter(s->sb);
    if (s->battling != NULL) {
        modelsBattlingState(s->battling, s->anim, out);
    }
    sLoadedMechas* m = mechaAt(s, 0);
    if (m != NULL) {
        out->program = s->anim;
        sMechaDataTable2_4_8* pc = m->m10_bytecode0;
        if (pc != NULL && s->anim >= 0 && m->m8_bytecodeTable != NULL) {
            sMechaDataTable2_4_8* start = m->m8_bytecodeTable[s->anim];
            long d = pc - start;
            out->pc = d >= 0 && d < 0x8000 ? (int)d : -1;
        }
        for (int i = 0; pc != NULL && i < 4; i++) {
            out->words[i] = pc[i];
        }
        out->wait = m->m40_waitTimer;
        out->loops = m->m42_loopCount;
        out->eventFrame = m->m98_eventFrame;
        int running = s->battle ? modelsBattleTracksInUse() : modelsViewTracksInUse();
        out->tracks = running;
        out->stopped = pc != NULL && (pc[0] & 0xFF) == 0 && running == 0;
        for (int i = 1; m->m4_bones != NULL && i < m->m4_bones->mA_numBones; i++) {
            out->visibleBones += m->m4_bones[i].m7_isEnabled != 0;
        }
        out->missingKeyframes = s->usedSentinel;
        out->suppressedOps = s->neutralised;
    }
    xgSandboxLeave(s->sb);
}

int xgSceneProgram(XgScene* s, int anim, uint16_t* out, int max) {
    if (!s->built || s->mechas == 0 || anim < 0 || anim >= s->programs || s->animRaw == NULL) {
        return 0;
    }
    xgSandboxEnter(s->sb);
    int n = 0;
    sLoadedMechas* m = mechaAt(s, 0);
    if (m != NULL && m->m8_bytecodeTable != NULL) {
        /* From its start to the next program's (in the file's order), or the
         * file's end. */
        u8* start = (u8*)m->m8_bytecodeTable[anim];
        u8* end = s->animRaw + s->animRawSize;
        for (int k = 0; k < s->programs; k++) {
            u8* p = (u8*)m->m8_bytecodeTable[k];
            if (p > start && p < end) {
                end = p;
            }
        }
        if (start >= s->animRaw && start < end && s->animFile != NULL) {
            /* The program as the disc has it (the live one may carry our
             * patches, and ops 0x30/0x31 keep counters in it). */
            n = (int)((end - start) / 2);
            n = n < max ? n : max;
            memcpy(out, s->animFile + (start - s->animRaw), (size_t)n * 2);
        }
    }
    xgSandboxLeave(s->sb);
    return n;
}

typedef struct {
    int operand, param, found;
} ExtraScan;

static int findLoadExtra(const uint16_t* op, int at, void* user) {
    ExtraScan* e = user;
    (void)at;
    if ((op[0] & 0xFF) == 0x04 && !e->found) {
        e->operand = op[1];
        e->param = op[0] >> 8;
        e->found = 1;
    }
    return 1;
}

int xgSceneProgramExtraFile(XgScene* s, int anim) {
    static uint16_t words[0x4000];
    if (!s->battle) {
        return -1;
    }
    int n = xgSceneProgram(s, anim, words, 0x4000);
    ExtraScan e = {0, 0, 0};
    xgMechaScan(words, n, 1, findLoadExtra, &e);
    if (!e.found) {
        return -1;
    }
    s32 file = -1;
    xgSandboxEnter(s->sb);
    modelsBattleExtraFile(e.operand, e.param, &file);
    xgSandboxLeave(s->sb);
    return file;
}

void xgSceneSetExtraFile(XgScene* s, const uint8_t* data, int size) {
    free(s->extra);
    s->extra = copyOf(data, size);
    s->extraSize = s->extra != NULL ? size : 0;
}

int xgSceneBattle(const XgScene* s) {
    return s->battle;
}

/* ---- texture pages --------------------------------------------------------------------------- */

/* The GP0 polygon packet after its tag (PS1 words): the texture's page, CLUT
 * and the texels its vertices use. */
typedef struct {
    int textured, semi, raw, quad, gouraud, n;
    uint8_t code;
    uint16_t tpage, clut;
    uint8_t u[4], v[4];
    uint8_t r[4], g[4], b[4];
} Packet;

static int readPacket(const u8* prim, Packet* p) {
    const u32* w = (const u32*)(prim + PRIM_OFS(4));
    uint8_t code = (uint8_t)(w[0] >> 24);
    memset(p, 0, sizeof(*p));
    if ((code & 0xE0) != 0x20) {
        return 0;
    }
    p->code = code;
    p->gouraud = (code & 0x10) != 0;
    p->quad = (code & 0x08) != 0;
    p->textured = (code & 0x04) != 0;
    p->semi = (code & 0x02) != 0;
    p->raw = (code & 0x01) != 0;
    p->n = p->quad ? 4 : 3;
    int at = 0;
    for (int k = 0; k < p->n; k++) {
        u32 c = k == 0 || p->gouraud ? w[at++] : w[0];
        p->r[k] = (uint8_t)c;
        p->g[k] = (uint8_t)(c >> 8);
        p->b[k] = (uint8_t)(c >> 16);
        at++; /* xy */
        if (p->textured) {
            u32 t = w[at++];
            p->u[k] = (uint8_t)t;
            p->v[k] = (uint8_t)(t >> 8);
            if (k == 0) {
                p->clut = (uint16_t)(t >> 16);
            } else if (k == 1) {
                p->tpage = (uint16_t)(t >> 16);
            }
        }
    }
    return 1;
}

/* The polygon definitions of main's model code (model.c polyRenderDefs):
 * the primitive's size for each mesh block family. */
typedef struct {
    void* render[6];
    void* init;
    s32 indexSize, displayListSize, primSize;
} PolyDef;
extern PolyDef polyRenderDefs[17];

int xgScenePages(XgScene* s, XgScenePage* out, int max) {
    if (!s->built) {
        return 0;
    }
    XgScenePart* parts = malloc(sizeof(XgScenePart) * MAX_PARTS);
    if (parts == NULL) {
        return 0;
    }
    int count = 0;
    xgSandboxEnter(s->sb);
    int n = partsInside(s, parts, MAX_PARTS);
    for (int i = 0; i < n; i++) {
        ModelBlock* b = parts[i].block;
        u8* mb = b->m10_meshBlocks;
        u8* prim = parts[i].prims;
        for (int g = 0; g < b->m6_numMeshBlocks; g++) {
            int type = mb[0], polys = *(u16*)(mb + 2);
            if (type > 16) {
                break;
            }
            for (int k = 0; k < polys; k++, prim += polyRenderDefs[type].primSize) {
                Packet p;
                if (!readPacket(prim, &p) || !p.textured) {
                    continue;
                }
                uint16_t clut = (p.tpage >> 7 & 3) == 2 ? 0 : p.clut;
                uint16_t page = p.tpage & 0x19F; /* page, depth (blend mode kept apart) */
                int at = -1;
                for (int j = 0; j < count; j++) {
                    if ((out[j].tpage & 0x19F) == page && out[j].clut == clut) {
                        at = j;
                    }
                }
                if (at < 0 && count < max) {
                    at = count++;
                    memset(&out[at], 0, sizeof(out[at]));
                    out[at].tpage = p.tpage;
                    out[at].clut = clut;
                    out[at].u0 = out[at].v0 = 255;
                }
                if (at < 0) {
                    continue;
                }
                XgScenePage* pg = &out[at];
                pg->polygons++;
                if (p.semi) {
                    pg->semi = 1;
                    pg->tpage = p.tpage;
                }
                for (int v = 0; v < p.n; v++) {
                    pg->u0 = p.u[v] < pg->u0 ? p.u[v] : pg->u0;
                    pg->v0 = p.v[v] < pg->v0 ? p.v[v] : pg->v0;
                    pg->u1 = p.u[v] > pg->u1 ? p.u[v] : pg->u1;
                    pg->v1 = p.v[v] > pg->v1 ? p.v[v] : pg->v1;
                }
            }
            mb += 4 + polys * polyRenderDefs[type].indexSize;
        }
    }
    xgSandboxLeave(s->sb);
    free(parts);
    return count;
}

static uint32_t rgba15(uint16_t c, int semiTexel) {
    uint32_t r = (c & 31) << 3 | (c & 31) >> 2;
    uint32_t g = (c >> 5 & 31) << 3 | (c >> 5 & 31) >> 2;
    uint32_t b = (c >> 10 & 31) << 3 | (c >> 10 & 31) >> 2;
    uint32_t a = c == 0 ? 0 : semiTexel && (c & 0x8000) ? 128 : 255;
    return r | g << 8 | b << 16 | a << 24;
}

void xgSceneBakePage(XgScene* s, uint16_t tpage, uint16_t clut, int semi, uint32_t* rgba) {
    const uint16_t* vram = xgVgpuVram(xgSandboxGpu(s->sb));
    int px = (tpage & 15) * 64, py = (tpage >> 4 & 1) * 256, depth = tpage >> 7 & 3;
    int cx = (clut & 63) * 16, cy = clut >> 6 & 511;
    for (int y = 0; y < 256; y++) {
        const uint16_t* row = vram + ((py + y) & 511) * XG_VRAM_W;
        for (int x = 0; x < 256; x++) {
            uint16_t c;
            if (depth == 0) {
                uint16_t w = row[(px + x / 4) & 1023];
                c = vram[cy * XG_VRAM_W + ((cx + (w >> (x % 4 * 4) & 15)) & 1023)];
            } else if (depth == 1) {
                uint16_t w = row[(px + x / 2) & 1023];
                c = vram[cy * XG_VRAM_W + ((cx + (w >> (x % 2 * 8) & 255)) & 1023)];
            } else {
                c = row[(px + x) & 1023];
            }
            rgba[y * 256 + x] = rgba15(c, semi);
        }
    }
}
