#ifndef PORT_EXTRAS_MODELS_TYPES_H
#define PORT_EXTRAS_MODELS_TYPES_H

/* Shared ABI for platform and -nostdinc game-module viewer hooks. */

typedef struct {
    int program;    /* -1: none */
    int pc;         /* in words from the program's start; -1 when it runs elsewhere */
    unsigned short words[4]; /* the bytecode at pc */
    int wait;       /* m40: ticks waited at a wait op */
    int loops;      /* m42 */
    int tracks;     /* animation tracks running in the pool */
    int stopped;    /* at an end op (0x00) with no track running */
    int eventFrame; /* the keyframe's event stream frame, -1 none */
    int visibleBones;
    int missingKeyframes; /* the program names keyframes its file lacks (one the game never runs there) */
    int suppressedOps; /* unsafe references omitted from the sandbox's file copy */
} XgVmState;

typedef struct {
    int native; /* normal fight speed, stationary; 0: raw clip without fight settings */
    int frames, speed, blend;
} XgBattlingTiming;

typedef struct {
    void* block;
    unsigned char* prims; /* as set up (initModel2), before any drawing */
    float m[3][3], t[3];
    int bone, parent;
    int enabled;
    int mecha; /* a skeleton's part (set up per bone, lit as it moves) */
    int rig; /* slot, XG_RIG_BATTLING/ARENA, or -1 for a static part */
    unsigned int generation; /* a battle slot's lifetime, stable within a replay */
} XgScenePart;

enum { XG_RIG_BATTLING = 32, XG_RIG_ARENA = 33, XG_SCENE_MAX_RIGS = 34 };
typedef struct {
    int slot, source, bones;
    unsigned int generation;
} XgSceneRig;

typedef struct {
    float m[3][3], t[3];
    int parent; /* -1: the root (bone 0's own is -1 too) */
    int geometry; /* model block, -1 none */
    int enabled;
} XgSceneBone;

#endif
