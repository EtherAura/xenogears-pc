/* Battle debugger tools: CPU/GPU load meter, heap and VRAM viewers, free
 * camera, model editor. gcc-2.7.2-cdk, ASPSX 2.56. */
#include "common.h"
#include "main/sound.h"
#include "psyq.h"
#include "stack.h"
#include "main/task.h"
#include "main/memory.h"
#include "main/sprite.h"
#include "main/debug_font.h"
#include "battle/battle.h"
#include "battle_debug/battle_debug.h"
#include "pc_retail_heap.h"

void SetLineF2(void* p);
TaskHeader* allocateTaskPair(s32 size, TaskHeader* owner, void (*update)(TaskHeader*), void (*draw)(TaskHeader*), void (*del)(TaskHeader*));
void dumpHeapToPcFile(char* name);

extern u8* shapeTransfertTableCurrentEntry;
extern OTag* characterRenderingOT;
extern SVECTOR battleCameraEye;
extern SVECTOR battleCameraAt;

void createRotationMatrix(SVECTOR* rot, MATRIX* m);

/* Moves the battle camera eye with the pad (debug camera). */
void moveDebugCameraEye(s32 buttons);
void moveDebugCameraEye(s32 buttons) {
    VECTOR out;
    SVECTOR move;
    MATRIX m;
    s32 step;

    step = 32;
    if (buttons & 2) {
        step = 8;
    }
    if (buttons & 8) {
        step *= 4;
    }
    move.vx = 0;
    move.vy = 0;
    move.vz = 0;
    if (buttons & 0x1000) {
        move.vx = step;
    }
    if (buttons & 0x4000) {
        move.vx = -step;
    }
    if (buttons & 0x8000) {
        move.vz = step;
    }
    if (buttons & 0x2000) {
        move.vz = -step;
    }
    createRotationMatrix(&battleCameraBlock.m14_orientation, &m);
    ApplyMatrix(&m, &move, &out);
    battleCameraEye.vx += out.vx;
    battleCameraEye.vy += out.vy;
    battleCameraEye.vz += out.vz;
    if (buttons & 1) {
        battleCameraEye.vy += step;
    }
    if (buttons & 4) {
        battleCameraEye.vy -= step;
    }
}

/* Moves the battle camera target with the pad (debug camera; yaw only). */
void moveDebugCameraAt(s32 buttons);
void moveDebugCameraAt(s32 buttons) {
    VECTOR out;
    SVECTOR move;
    SVECTOR rot;
    MATRIX m;
    s32 step;
    SVECTOR* o;

    step = 32;
    if (buttons & 2) {
        step = 8;
    }
    if (buttons & 8) {
        step *= 4;
    }
    move.vx = 0;
    move.vy = 0;
    move.vz = 0;
    if (buttons & 0x1000) {
        move.vx = step;
    }
    if (buttons & 0x4000) {
        move.vx = -step;
    }
    if (buttons & 0x8000) {
        move.vz = step;
    }
    if (buttons & 0x2000) {
        move.vz = -step;
    }
    o = &battleCameraBlock.m14_orientation;
    rot.vx = o->vx;
    rot.vy = o->vy;
    rot.vz = o->vz;
    rot.vx = 0;
    createRotationMatrix(&rot, &m);
    ApplyMatrix(&m, &move, &out);
    battleCameraAt.vx += out.vx;
    battleCameraAt.vy += out.vy;
    battleCameraAt.vz += out.vz;
    if (buttons & 1) {
        battleCameraAt.vy += step;
    }
    if (buttons & 4) {
        battleCameraAt.vy -= step;
    }
}

extern s32 allocateSavePointMeshDataSub0_var1;
extern u8* shapeTransfertTableEnd;
extern SpriteVramData createSavePointMeshData_mode5;
extern s32 debugHeapViewerEnabled;
extern s32 debugPerfInfoEnabled;
extern s32 debugCameraViewEnabled;
extern s32 debugGeomOffsetToggle;
extern s32 debugCameraFrameCounter;
s32 updateDebugModelEditor(void);
void reinitDebugFontOnPadPress(void);
void updateDebugHeapViewer(void);
void setBattleCameraMode(s32);
SpriteTask* spawnEffectSprite(s32 animationId, SpriteVramData* vram, s16* position, s32 extraSize);

/* Battle debugger main update: heap viewer, CPU/GPU stats, VRAM view with
 * the free camera. */
void updateBattleDebugger(void);
void updateBattleDebugger(void) {
    SVECTOR angle;
    SVECTOR pos;
    s32 flag;
    POLY_FT4* p;
    POLY_FT4* p2;
    TILE_1* t;
    s32 mode;

    updateDebugModelEditor();
    if (battleBssBlock.m8C58_pad.m6_pressed2 & 0x800) {
        debugHeapViewerEnabled = 1 - debugHeapViewerEnabled;
    }
    if (debugHeapViewerEnabled != 0) {
        updateDebugHeapViewer();
    }
    if (battleBssBlock.m8C58_pad.m6_pressed2 & 0x20) {
        debugPerfInfoEnabled = 1 - debugPerfInfoEnabled;
    }
    if (debugPerfInfoEnabled != 0) {
        logToScreen("CPU       %d\n", battleCameraBlock.m40_drawTiming0);
        logToScreen("GPU       %d\n", battleCameraBlock.m44_drawTiming1);
        logToScreen("tasks     %d\n", allocateSavePointMeshDataSub0_var1);
        logToScreen("polys     %d%%\n", (shapeTransfertTableEnd - shapeTransfertTableCurrentEntry) * 100 / 0x5000);
        logToScreen("frameRate %d\n", battleBssBlock.m8DAC_frameRate + 1);
    }
    if (0) {
        logToScreen("bitmap: %x\n");
    }
    if (battleBssBlock.m8C58_pad.m6_pressed2 & 0x100) {
        debugCameraViewEnabled = 1 - debugCameraViewEnabled;
        if (debugCameraViewEnabled == 0) {
            setBattleCameraMode(1);
        } else {
            setBattleCameraMode(4);
        }
    }
    if (debugCameraViewEnabled != 0) {
        p = (POLY_FT4*)shapeTransfertTableCurrentEntry;
        shapeTransfertTableCurrentEntry += sizeof(POLY_FT4);
        SetPolyFT4(p);
        SetShadeTex(p, 1);
        p->x0 = 0;
        p->y0 = -64;
        p->x1 = 64;
        p->y1 = -64;
        p->x2 = 0;
        p->y2 = 191;
        p->x3 = 64;
        p->y3 = 191;
        p->u0 = 0;
        p->v0 = 0;
        p->u1 = 127;
        p->v1 = 0;
        p->u2 = 0;
        p->v2 = 255;
        p->u3 = 127;
        p->v3 = 255;
        p->tpage = GetTPage(1, 0, 0x3C0, 0);
        p->clut = GetClut(0, 0x1CC);
        AddPrim(characterRenderingOT, p);
        p2 = (POLY_FT4*)shapeTransfertTableCurrentEntry;
        shapeTransfertTableCurrentEntry += sizeof(POLY_FT4);
        SetPolyFT4(p2);
        SetShadeTex(p2, 1);
        p2->x0 = 64;
        p2->y0 = -64;
        p2->x1 = 128;
        p2->y1 = -64;
        p2->x2 = 64;
        p2->y2 = 191;
        p2->x3 = 128;
        p2->y3 = 191;
        p2->u0 = 0;
        p2->v0 = 0;
        p2->u1 = 127;
        p2->v1 = 0;
        p2->u2 = 0;
        p2->v2 = 255;
        p2->u3 = 127;
        p2->v3 = 255;
        p2->tpage = GetTPage(1, 0, 0x340, 0x100);
        p2->clut = GetClut(0, 0x1CC);
        AddPrim(characterRenderingOT, p2);
        logToScreen("lenge:  %d\n", battleCameraBlock.m1C_distance);
        logToScreen("camera: %d,%d,%d\n", battleCameraEye.vx, battleCameraEye.vy, battleCameraEye.vz);
        logToScreen("watch:  %d,%d,%d\n", battleCameraAt.vx, battleCameraAt.vy, battleCameraAt.vz);
        angle.vx = (battleCameraBlock.m14_orientation.vx & 0xFFF) * 360 / 4096;
        angle.vy = (battleCameraBlock.m14_orientation.vy & 0xFFF) * 360 / 4096;
        logToScreen("angle:  %d,%d(%d)\n", angle.vx, angle.vy, (angle.vy + 90) % 360);
        debugCameraFrameCounter++;
        SetRotMatrix(&battleCameraBlock.m20_renderingMatrix);
        SetTransMatrix(&battleCameraBlock.m20_renderingMatrix);
        pos.vx = battleCameraAt.vx;
        pos.vy = battleCameraAt.vy;
        pos.vz = battleCameraAt.vz;
        t = (TILE_1*)shapeTransfertTableCurrentEntry;
        shapeTransfertTableCurrentEntry += sizeof(TILE_1);
        setlen(t, 2);
        setcode(t, 0x70);
        t->r0 = 0xFF;
        t->g0 = 0xFF;
        t->b0 = 0;
        RotTransPers(&pos, (s32*)&t->x0, &flag, &flag);
        t->x0 -= 4;
        t->y0 -= 4;
        AddPrim(characterRenderingOT, t);
        if ((debugCameraFrameCounter & 7) == 0) {
            spawnEffectSprite(2, &createSavePointMeshData_mode5, &pos.vx, PC_HEAP_BYTES(0));
        }
        if (battleBssBlock.m8C58_pad.m6_pressed2 & 0x80) {
            if (++debugGeomOffsetToggle & 1) {
                SetGeomOffset(160, 112);
            } else {
                SetGeomOffset(160, 165);
            }
        }
        if (battleBssBlock.m8C58_pad.m2_held2 & 0x40) {
            moveDebugCameraAt(battleBssBlock.m8C58_pad.m2_held2);
            moveDebugCameraEye(battleBssBlock.m8C58_pad.m2_held2);
        } else if (battleBssBlock.m8C58_pad.m2_held2 & 0x20) {
            moveDebugCameraAt(battleBssBlock.m8C58_pad.m2_held2);
        } else {
            moveDebugCameraEye(battleBssBlock.m8C58_pad.m2_held2);
        }
    }
    reinitDebugFontOnPadPress();
}

void deinitDebugFont(void);

void reinitDebugFontOnPadPress(void) {
#if VERSION_JP
    u8 unused[0x38];                    /* JP's frame is 8 bytes larger */
#else
    u8 unused[0x30];
#endif

    if (battleBssBlock.m8C58_pad.m6_pressed2 != 0) {
        deinitDebugFont();
        initDebugFont(0x10, 0x10, 0x140, 0x100, 1000, 0, 0x340, 0, 0x340, 0x20, 0);
        initDebugFontClut(0x7FFF, 0x8000);
    }
}

extern s32 debugHeapViewerFlags;
extern s32 debugHeapViewerScroll;
extern s32 debugHeapViewerRepeatDelay;
extern s32 debugHeapViewerCount;
void printLoadedWdsList(void);
void dumpHeapBlocks(s32, s32, s32, s32);

/* Debug heap viewer: pad 2 toggles the column flags, scrolls and pages. */
void updateDebugHeapViewer(void) {
    s32 scroll;

    printLoadedWdsList();
    scroll = 0;
    if (battleBssBlock.m8C58_pad.m6_pressed2 & 2) {
        debugHeapViewerFlags ^= 0x10;
    }
    if (battleBssBlock.m8C58_pad.m6_pressed2 & 8) {
        debugHeapViewerFlags ^= 0x20;
    }
    if (battleBssBlock.m8C58_pad.m6_pressed2 & 0x10) {
        debugHeapViewerFlags ^= 0x40;
    }
    if (battleBssBlock.m8C58_pad.m6_pressed2 & 0x20) {
        debugHeapViewerFlags ^= 0x80;
    }
    if (battleBssBlock.m8C58_pad.m6_pressed2 & 0x80) {
        debugHeapViewerFlags ^= 2;
    }
    if (battleBssBlock.m8C58_pad.m6_pressed2 & 0x40) {
        debugHeapViewerFlags ^= 0x8000;
    }
    if (battleBssBlock.m8C58_pad.m6_pressed2 & 4) {
        debugHeapViewerCount++;
    }
    if (battleBssBlock.m8C58_pad.m6_pressed2 & 1) {
        if (--debugHeapViewerCount < 0) {
            debugHeapViewerCount = 0;
        }
    }
    logToScreen("\t\t\tdebug heap\n");
    if (battleBssBlock.m8C58_pad.m2_held2 & 0x5000) {
        if (++debugHeapViewerRepeatDelay > 8) {
            debugHeapViewerRepeatDelay = 8;
        }
    } else {
        debugHeapViewerRepeatDelay = 0;
    }
    if (battleBssBlock.m8C58_pad.m2_held2 & 0x1000) {
        if (debugHeapViewerRepeatDelay >= 8) {
            scroll--;
        }
    }
    if (battleBssBlock.m8C58_pad.m2_held2 & 0x4000) {
        if (debugHeapViewerRepeatDelay >= 8) {
            scroll++;
        }
    }
    if (battleBssBlock.m8C58_pad.m6_pressed2 & 0x1000) {
        scroll--;
    }
    if (battleBssBlock.m8C58_pad.m6_pressed2 & 0x4000) {
        scroll++;
    }
    if ((debugHeapViewerScroll += scroll) < 0) {
        debugHeapViewerScroll = 0;
    }
    dumpHeapBlocks(3, debugHeapViewerScroll, debugHeapViewerCount, debugHeapViewerFlags);
}

void updateDebugLoadMeter(TaskHeader* task);
void updateDebugLoadMeter(TaskHeader* task) {
    BattleDebugMeter* m = task->m4_data;

    m->m40_cpuSmooth += (battleCameraBlock.m40_drawTiming0 * 16 - m->m40_cpuSmooth) >> 3;
    m->m44_gpuSmooth += (battleCameraBlock.m44_drawTiming1 * 16 - m->m44_gpuSmooth) >> 3;
    m->m38_cpu = m->m40_cpuSmooth;
    m->m3C_gpu = m->m44_gpuSmooth;
    if (--m->m50_cpuPeakTimer == 0) {
        m->m48_cpuPeak = 0;
    }
    if (m->m38_cpu > m->m48_cpuPeak) {
        m->m48_cpuPeak = m->m38_cpu;
        m->m50_cpuPeakTimer = 80;
    }
    if (--m->m54_gpuPeakTimer == 0) {
        m->m4C_gpuPeak = 0;
    }
    if (m->m3C_gpu > m->m4C_gpuPeak) {
        m->m4C_gpuPeak = m->m3C_gpu;
        m->m54_gpuPeakTimer = 80;
    }
}

void drawDebugMeterNeedle(SVECTOR* v, u8 r, u8 g, u8 b);
void drawDebugMeterNeedle(SVECTOR* v, u8 r, u8 g, u8 b) {
    POLY_F3* p;
    SVECTOR s0;
    SVECTOR s1;
    SVECTOR s2;
    s32 flag;

    p = (POLY_F3*)shapeTransfertTableCurrentEntry;
    shapeTransfertTableCurrentEntry += sizeof(POLY_F3);
    SetPolyF3(p);
    p->r0 = r;
    p->g0 = g;
    p->b0 = b;
    RotTransSV(&v[0], &s0, &flag);
    RotTransSV(&v[1], &s1, &flag);
    RotTransSV(&v[2], &s2, &flag);
    p->x0 = s0.vx;
    p->y0 = s0.vy;
    p->x1 = s1.vx;
    p->y1 = s1.vy;
    p->x2 = s2.vx;
    p->y2 = s2.vy;
    AddPrim(characterRenderingOT, p);
}

void drawDebugMeterTick(u8 r, u8 g, u8 b);
void drawDebugMeterTick(u8 r, u8 g, u8 b) {
    LINE_F3* p;
    SVECTOR v0;
    SVECTOR v1;
    SVECTOR s0;
    SVECTOR s1;
    s32 flag;

    p = (LINE_F3*)shapeTransfertTableCurrentEntry;
    shapeTransfertTableCurrentEntry += sizeof(LINE_F3);
    SetLineF2(p);
    v0.vx = 30;
    v0.vy = 0;
    v0.vz = 0;
    v1.vx = 35;
    v1.vy = 0;
    v1.vz = 0;
    p->r0 = r;
    p->g0 = g;
    p->b0 = b;
    RotTransSV(&v0, &s0, &flag);
    RotTransSV(&v1, &s1, &flag);
    p->x0 = s0.vx;
    p->y0 = s0.vy;
    p->x1 = s1.vx;
    p->y1 = s1.vy;
    AddPrim(characterRenderingOT, p);
}

void drawDebugMeterPeakLine(s32 x, u8 r, u8 g, u8 b);
void drawDebugMeterPeakLine(s32 x, u8 r, u8 g, u8 b) {
    LINE_F3* p;
    SVECTOR v;
    SVECTOR s;
    s32 flag;

    p = (LINE_F3*)shapeTransfertTableCurrentEntry;
    shapeTransfertTableCurrentEntry += sizeof(LINE_F3);
    SetLineF2(p);
    v.vx = x;
    v.vy = 0;
    v.vz = 0;
    p->r0 = r;
    p->g0 = g;
    p->b0 = b;
    RotTransSV(&v, &s, &flag);
    p->x0 = 280;
    p->y0 = 200;
    p->x1 = s.vx;
    p->y1 = s.vy;
    AddPrim(characterRenderingOT, p);
}

extern SVECTOR debugLoadMeterNeedleVerts[6];

/* Draws the CPU (red) and GPU (blue) load meter: needles, peak marks and a
 * tick per 0x400 of load. */
void drawDebugLoadMeter(TaskHeader* task);
void drawDebugLoadMeter(TaskHeader* task) {
    BattleDebugMeter* m;
    MATRIX mat;
    SVECTOR rot;
    VECTOR pos;
    s32 n;
    s32 angle;

    m = task->m4_data;
    pos.vx = 280;
    pos.vy = 200;
    pos.vz = 0;
    TransMatrix(&mat, &pos);
    SetTransMatrix(&mat);
    rot.vx = 0;
    rot.vy = 0;
    rot.vz = m->m3C_gpu - 0x400;
    createRotationMatrix(&rot, &mat);
    SetRotMatrix(&mat);
    drawDebugMeterNeedle(&debugLoadMeterNeedleVerts[0], 0, 0, 0xFF);
    rot.vx = 0;
    rot.vy = 0;
    rot.vz = m->m4C_gpuPeak - 0x400;
    createRotationMatrix(&rot, &mat);
    SetRotMatrix(&mat);
    drawDebugMeterPeakLine(20, 0, 0, 0xFF);
    rot.vx = 0;
    rot.vy = 0;
    rot.vz = m->m38_cpu - 0x400;
    createRotationMatrix(&rot, &mat);
    SetRotMatrix(&mat);
    drawDebugMeterNeedle(&debugLoadMeterNeedleVerts[3], 0xFF, 0, 0);
    rot.vx = 0;
    rot.vy = 0;
    rot.vz = m->m48_cpuPeak - 0x400;
    createRotationMatrix(&rot, &mat);
    SetRotMatrix(&mat);
    drawDebugMeterPeakLine(30, 0xFF, 0, 0);
    for (n = m->m38_cpu / 1024 + 1, angle = -0x400; n != 0; n--, angle += 0x100) {
        rot.vx = 0;
        rot.vy = 0;
        rot.vz = angle;
        createRotationMatrix(&rot, &mat);
        SetRotMatrix(&mat);
        drawDebugMeterTick(0xFF, 0, 0);
    }
    for (n = m->m3C_gpu / 1024 + 1, angle = 0x400; n != 0; n--, angle += 0x100) {
        rot.vx = 0;
        rot.vy = 0;
        rot.vz = angle;
        createRotationMatrix(&rot, &mat);
        SetRotMatrix(&mat);
        drawDebugMeterTick(0, 0, 0xFF);
    }
}

void drawDebugLoadMeter(TaskHeader* task);

void createDebugLoadMeter(void);
void createDebugLoadMeter(void) {
    BattleDebugMeter* m;

    m = (BattleDebugMeter*)allocateTaskPair(sizeof(BattleDebugMeter), NULL, updateDebugLoadMeter, drawDebugLoadMeter, NULL);
    m->m54_gpuPeakTimer = 1;
    m->m50_cpuPeakTimer = 1;
    m->m4C_gpuPeak = 0;
    m->m48_cpuPeak = 0;
    m->m3C_gpu = 0;
    m->m38_cpu = 0;
    m->m44_gpuSmooth = 0;
    m->m40_cpuSmooth = 0;
}

void printLoadedWdsList(void) {
    Wds* w;
    s32 i;

    w = pLoadedWdsLinkedList;
    i = 0;
    while (1) {
        if (w->m2C_next == NULL) {
            break;
        }
        sprintf_screen("%d W=%x\n", i, w->m2C_next);
        w = w->m2C_next;
        i++;
    }
}

extern SpriteActorCore* debugModelEditorActor;
extern u8 debugModelEditorShift;
extern u8 debugModelEditorControlMode;
extern char* debugModelEditorControlModeNames[5];

/* Model debugger: shows the selected actor's transform and edits it with
 * pad 2 (control mode: pos, rot, scale, light angle, light colour).
 * Declared int with no return value: v0 is then live at the exit, which
 * keeps reorg from putting the jump-table lui in the dispatch delay slot. */
s32 updateDebugModelEditor(void) {
    SVECTOR v;
    VECTOR d;
    s32 sxy;
    s32 p;
    s32 flag;
    s32 otz;
    SpriteActorCore* a;

    a = debugModelEditorActor;
    if (a == NULL) {
        IMPLICIT_RETURN(0);
    }
    if (battleBssBlock.m8C58_pad.m6_pressed2 & 8) {
        if (++debugModelEditorShift >= 7) {
            debugModelEditorShift = 0;
        }
    }
    logToScreen("shifts mode: %x\n", debugModelEditorShift);
    if (battleBssBlock.m8C58_pad.m6_pressed2 & 2) {
        if (++debugModelEditorControlMode >= 5) {
            debugModelEditorControlMode = 0;
        }
    }
    logToScreen("control mode: %s\n", debugModelEditorControlModeNames[debugModelEditorControlMode]);
    v.vx = a->m0_position.vx >> 16;
    v.vy = a->m0_position.vy >> 16;
    v.vz = a->m0_position.vz >> 16;
    SetRotMatrix(&battleCameraBlock.m20_renderingMatrix);
    SetTransMatrix(&battleCameraBlock.m20_renderingMatrix);
    otz = RotTransPers(&v, &sxy, &p, &flag);
    logToScreen("shapeno %x\n", (u16)a->m34_currentSpriteFrame);
    logToScreen("otz     %x\n", otz);
    logToScreen("pos xyz %d,%d,%d\n", a->m0_position.vx >> 17, a->m0_position.vy >> 17, a->m0_position.vz >> 17);
    logToScreen("vec xyz %x,%x,%x\n", a->mC_step.vx >> 1, a->mC_step.vy >> 1, a->mC_step.vz >> 1);
    v.vz = (((ModelObject*)a->m20_object)->m0_rotation[2] & 0xFFF) * 360 / 4096;
    v.vx = (((ModelObject*)a->m20_object)->m0_rotation[0] & 0xFFF) * 360 / 4096;
    v.vy = (((ModelObject*)a->m20_object)->m0_rotation[1] & 0xFFF) * 360 / 4096;
    logToScreen("rot:    %d,%d,%d\n", v.vx, v.vy, v.vz);
    logToScreen("scale:  %d,%d,%d\n", ((ModelObject*)a->m20_object)->m6_scale[0] >> 1,
                ((ModelObject*)a->m20_object)->m6_scale[1] >> 1, ((ModelObject*)a->m20_object)->m6_scale[2] >> 1);
    if (a->m3C_x0_type == 2) {
        logToScreen("lgtang: %d,%d,%d\n", ((ModelObject*)a->m20_object)->m44_lightRotation.vx,
                    ((ModelObject*)a->m20_object)->m44_lightRotation.vy, ((ModelObject*)a->m20_object)->m44_lightRotation.vz);
        logToScreen("lgtcol: %d,%d,%d\n", (s16)((ModelObject*)a->m20_object)->m4C_lightColor[0],
                    (s16)((ModelObject*)a->m20_object)->m4C_lightColor[1], (s16)((ModelObject*)a->m20_object)->m4C_lightColor[2]);
    }
    logToScreen("gravity %x\n", a->m1C_gravity);
    if (a->m40_draw.bits.xD_mode == 0xF) {
        logToScreen("polys %d\n", ((s32*)((ModelObject*)a->m20_object)->m34_modelBlock)[5]);
    }
    d.vx = 0;
    d.vy = 0;
    d.vz = 0;
    if (battleBssBlock.m8C58_pad.m2_held2 & 1) {
        d.vz = -1;
    }
    if (battleBssBlock.m8C58_pad.m2_held2 & 4) {
        d.vz++;
    }
    if (battleBssBlock.m8C58_pad.m2_held2 & 0x1000) {
        d.vy = -1;
    }
    if (battleBssBlock.m8C58_pad.m2_held2 & 0x4000) {
        d.vy++;
    }
    if (battleBssBlock.m8C58_pad.m2_held2 & 0x8000) {
        d.vx = -1;
    }
    if (battleBssBlock.m8C58_pad.m2_held2 & 0x2000) {
        d.vx++;
    }
    d.vx <<= debugModelEditorShift;
    d.vy <<= debugModelEditorShift;
    d.vz <<= debugModelEditorShift;
    switch (debugModelEditorControlMode) {
    case 0:
        d.vx <<= 17;
        d.vy <<= 17;
        d.vz <<= 17;
        a->m0_position.vx += d.vx;
        a->m0_position.vy += d.vy;
        a->m0_position.vz += d.vz;
        break;
    case 2:
        d.vx <<= 4;
        d.vy <<= 4;
        d.vz <<= 4;
        ((ModelObject*)a->m20_object)->m6_scale[0] += d.vx;
        ((ModelObject*)a->m20_object)->m6_scale[1] += d.vy;
        ((ModelObject*)a->m20_object)->m6_scale[2] += d.vz;
        a->m3C_x1C_matrixDirty = 1;
        break;
    case 1:
        ((ModelObject*)a->m20_object)->m0_rotation[0] += d.vy;
        ((ModelObject*)a->m20_object)->m0_rotation[1] += d.vx;
        ((ModelObject*)a->m20_object)->m0_rotation[2] += d.vz;
        a->m3C_x1C_matrixDirty = 1;
        break;
    case 3:
        if (a->m3C_x0_type == 2) {
            ((ModelObject*)a->m20_object)->m44_lightRotation.vx += d.vx;
            ((ModelObject*)a->m20_object)->m44_lightRotation.vy += d.vy;
            ((ModelObject*)a->m20_object)->m44_lightRotation.vz += d.vz;
        }
        break;
    case 4:
        if (a->m3C_x0_type == 2) {
            ((ModelObject*)a->m20_object)->m4C_lightColor[0] += d.vx;
            ((ModelObject*)a->m20_object)->m4C_lightColor[1] += d.vy;
            ((ModelObject*)a->m20_object)->m4C_lightColor[2] += d.vz;
        }
        break;
    }
    FALLOFF_RETURN(0); /* result unused */
}

extern s32 battleHeapDumpCount;
extern char strBattleHeapDumpPath[20]; /* the dump file name, c:\btlmem\mem_00: JP splits it at [15] */

void writeBattleHeapDump(void);
void writeBattleHeapDump(void) {
    battleHeapDumpCount++;
    strBattleHeapDumpPath[15] = battleHeapDumpCount + '0';
    dumpHeapToPcFile(strBattleHeapDumpPath);
}

void dumpBattleHeap(void);
void dumpBattleHeap(void) {
    u8* stack;

    stack = allocateHeap(0x4000, 1);
    STACK_SWITCH(stack + 0x3FC0);
    writeBattleHeapDump();
    STACK_RESTORE();
    freeHeap(stack);
}
