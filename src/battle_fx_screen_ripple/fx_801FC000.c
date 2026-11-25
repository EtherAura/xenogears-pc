/* Screen shatter/ripple over 2x14x20 triangles with its own frame loop on a
 * scratch stack (a variant of battle's start effect).
 * gcc-2.7.2-cdk, ASPSX 2.56. */
#include "common.h"
#include "psyq.h"
#include "main/task.h"
#include "main/memory.h"
#include "stack.h"
#include "battle/battle.h"
#include "battle_fx_screen_ripple/battle_fx_screen_ripple.h"
#ifdef PLATFORM_PC
#include "gte_pc.h"
#include "battle/pc_wide_fx.h"
#endif

/* main */
TaskHeader* allocateTaskPair(s32 size, TaskHeader* owner, void (*update)(TaskHeader*), void (*draw)(TaskHeader*), void (*del)(TaskHeader*));
void registerSpriteCallback2Sub0(TaskHeader* task);
void allocateSavePointMeshDataSub0_callback(TaskHeader* task);
void clearShapeTransferEntry(void* data);
void createRotationMatrix(SVECTOR* rot, MATRIX* m);
s32 getAngleSin(s32); /* sine */
s32 getAngleCos(s32); /* cosine */
s32 addClamp255(s32 value, s32 delta);
void SetPolyGT3(void* p);
extern s16 battleMechaPartyTextureLocations[6];
extern u8* shapeTransfertTableCurrentEntry;
/* this overlay */
extern u8 fxScreenRippleAnimatePhase;           /* 1: the "fly off" variant */
extern SVECTOR fxScreenRippleTriVertsTopLeft[3];
extern SVECTOR fxScreenRippleTriVertsBottomRight[3];
extern OTag* fxScreenRippleOT;        /* ordering table */
Fx08Effect* initFxScreenRipple(Fx08Effect* e);
void fxScreenRipple_update(TaskHeader* task);
void fxScreenRipple_draw(TaskHeader* task);
void freeFxScreenRipple(void* p);
Fx08Effect* createBattleStartEffect(void);
void runFxScreenRippleLoop(void);

#ifdef PLATFORM_PC
/*
 * The wide picture (include/pc_wide.h): the ripple covers the margins too, as
 * the battle loader's wave transition does (bld_801E7F4C.c): the capture
 * keeps the wide picture's margins (pcWideCaptureMask), the 4:3 triangles
 * sample it as before (PC_WIDE_HANDLED), and extra columns of triangles cut
 * from the margins ripple with them (each vertex's phase follows its
 * distance from the centre). The frames are not cleared (isbg 0: the
 * triangles leave trails), so the margins are kept from frame to frame
 * (pcWideKeepMargins) as VRAM keeps the 4:3 part. The extras are the PC's
 * own (no game heap), drawn only into the wide picture, and the GTE is driven
 * directly (battle/pc_wide_fx.h) and put back as the PS1 leaves it. A capture without margins leaves the frames 4:3.
 */
#define WIDE_RIPPLE_COLS 24 /* per side, 16 pixels each: the widest margin */

static struct {
    s32 cols; /* per side; 0: none */
    Fx08Triangle t[2][14][2 * WIDE_RIPPLE_COLS];
} wideRipple;

/* The x of margin column c: left of the screen, then right. */
static s32 wideRippleX(s32 c) {
    return c < wideRipple.cols ? (c - wideRipple.cols) * 16 : 0x140 + (c - wideRipple.cols) * 16;
}

/* After the capture's LoadImage. */
static void wideRippleInit(void) {
    RECT r = {0, 0, 0x140, 0xE0};
    s32 i, j, c, l, k, m;

    m = pcWideCaptureMask(&r, 0x2C0, 0x100);
    wideRipple.cols = m > 0 ? (m + 15) / 16 : 0;
    if (wideRipple.cols > WIDE_RIPPLE_COLS) {
        wideRipple.cols = WIDE_RIPPLE_COLS;
    }
    for (i = 0; i != 2; i++) {
        for (j = 0; j != 14; j++) {
            s32 y = j << 4;

            for (c = 0; c < 2 * wideRipple.cols; c++) {
                Fx08Triangle* t = &wideRipple.t[i][j][c];
                SVECTOR* src = i == 0 ? fxScreenRippleTriVertsTopLeft : fxScreenRippleTriVertsBottomRight;

                k = wideRippleX(c) / 16;
                for (l = 0; l != 3; l++) {
                    SVECTOR* v = &t->m54_vertices[l];
                    s32 d;

                    v->vx = src[l].vx + (k * 16 - (i == 0 ? 155 : 149)) * 16;
                    v->vy = src[l].vy + (y - (i == 0 ? 107 : 101)) * 16;
                    v->vz = src[l].vz;
                    d = pcWideSqrt(v->vx * v->vx + v->vy * v->vy);
                    t->m6C_phase[l] = fxScreenRippleAnimatePhase ? d * 2 : d * 3 / 5;
                }
                for (l = 0; l != 2; l++) {
                    POLY_GT3* poly = &t->m4_poly[l];

                    SetPolyGT3(poly);
                    SetShadeTex(poly, 0);
                    setRGB0(poly, 0x80, 0x80, 0x80);
                    setRGB1(poly, 0x80, 0x80, 0x80);
                    setRGB2(poly, 0x80, 0x80, 0x80);
                    poly->code |= 2;
                    poly->tpage = GetTPage(2, 0, 0x2C0, 0x100);
                    poly->u0 = i == 0 ? 0 : 16;
                    poly->v0 = y;
                    poly->u1 = 16;
                    poly->v1 = i == 0 ? y : y + 16;
                    poly->u2 = 0;
                    poly->v2 = y + 16;
                }
            }
        }
    }
    pcWideKeepMargins(wideRipple.cols > 0);
}

/* Inside fxScreenRipple_draw, after its loop: its matrices still set. */
static void wideRippleDraw(Fx08Effect* e) {
    GteState gte;
    s32 i, j, k, c, l;

    if (wideRipple.cols == 0 || pcWideView == 0) {
        return;
    }
    for (i = 0; i != 2; i++) {
        for (j = 0; j != 14; j++) {
            for (k = 0; k != 20; k++) {
                pcWidePrim(&e->m64_triangles[i][j][k].m4_poly[battleBssBlock.m8C84_oddOrEven], 0, PC_WIDE_HANDLED);
            }
        }
    }
    gteSave(&gte);
    for (i = 0; i != 2; i++) {
        for (j = 0; j != 14; j++) {
            for (c = 0; c < 2 * wideRipple.cols; c++) {
                Fx08Triangle* t = &wideRipple.t[i][j][c];
                SVECTOR* v = t->m54_vertices;
                POLY_GT3* poly = &t->m4_poly[battleBssBlock.m8C84_oddOrEven];
                s32 flag, z;

                for (l = 0; l != 3; l++) {
                    s32 q, r;

                    if (fxScreenRippleAnimatePhase) {
                        q = getAngleCos(e->m3C_phase + t->m6C_phase[l]);
                    } else {
                        q = getAngleSin(t->m6C_phase[l]);
                    }
                    r = q * e->m40_amplitude / 4096;
                    v[l].vz = r >> 2;
                    r = (r >> 7) + e->m38_brightness;
                    r = r < 0 ? 0 : r >= 0x100 ? 0xFF : r;
                    if (l == 0) {
                        setRGB0(poly, r, r, r);
                    } else if (l == 1) {
                        setRGB1(poly, r, r, r);
                    } else {
                        setRGB2(poly, r, r, r);
                    }
                }
                z = pcWideRotTransPers3(&v[0], &v[1], &v[2], &poly->x0, &poly->x1, &poly->x2, &flag);
                if (!(flag & 0x8000)) {
                    AddPrim(&fxScreenRippleOT[z >> 6], poly);
                    pcWidePrimTex(poly, 0, PC_WIDE_ONLY, wideRippleX(c));
                }
            }
        }
    }
    gteLoad(&gte);
}
#endif

void fxScreenRipple_update(TaskHeader* task) {
    Fx08Effect* e = (Fx08Effect*)task->m4_data;
    s32 i, j, k;
    SVECTOR v;
    s32 phase;

    if (fxScreenRippleAnimatePhase) {
        e->m48_speed++;
        e->m3C_phase = phase = e->m3C_phase + 0x80;
        e->m40_amplitude += 0x40;
        e->m44_frameCounter++;
        e->m4C_translation.vz -= 30;
#if VERSION_JP
        e->m3C_phase = (e->m48_speed >> 2) + phase;
#else
        e->m3C_phase = phase + (e->m48_speed >> 2);
#endif
        if (e->m44_frameCounter > 100) {
            e->m38_brightness -= 4;
        }
    } else {
        e->m48_speed += 10;
        e->m40_amplitude += 0x600;
        e->m44_frameCounter++;
        e->m5C_rotation.vz += 10;
        e->m4C_translation.vz -= e->m48_speed;
        if (e->m44_frameCounter > 24) {
            e->m38_brightness -= 20;
        }
    }
    for (i = 0; i != 2; i++) {
        for (j = 0; j != 14; j++) {
            for (k = 0; k != 20; k++) {
            }
        }
    }
}

void fxScreenRipple_draw(TaskHeader* task) {
    Fx08Effect* e = (Fx08Effect*)task->m4_data;
    s32 ofx, ofy;
    s32 h;
    s32 j, i, k, l;
    s32 r, z;
    s32 amp;
    s32 q;

    ReadGeomOffset(&ofx, &ofy);
    h = ReadGeomScreen();
    SetGeomOffset(0xA0, 0x70);
    SetGeomScreen(0x200);
    {
        MATRIX m;
        s32 p, flag;

        createRotationMatrix(&e->m5C_rotation, &m);
        TransMatrix(&m, &e->m4C_translation);
        SetRotMatrix(&m);
        SetTransMatrix(&m);
        for (i = 0; i != 2; i++) {
            for (j = 0; j != 14; j++) {
                for (k = 0; k != 20; k++) {
                    Fx08Triangle* t = &e->m64_triangles[i][j][k];
                    SVECTOR* v = t->m54_vertices;
                    POLY_GT3* poly = &t->m4_poly[battleBssBlock.m8C84_oddOrEven];
                    for (l = 0; l != 3; l++) {
                        if (fxScreenRippleAnimatePhase) {
                            amp = e->m40_amplitude;
                            q = getAngleCos(e->m3C_phase + t->m6C_phase[l]);
                        } else {
                            amp = e->m40_amplitude;
                            q = getAngleSin(t->m6C_phase[l]);
                        }
                        q *= amp;
                        r = q / 4096;
                        {
                            s32 vz = r >> 2;
                            v[l].vz = vz;
                        }
                        r >>= 7;
                        r = r + e->m38_brightness;
                        if (r < 0) {
                            r = 0;
                        }
                        if (r >= 0x100) {
                            r = 0xFF;
                        }
                        switch (l) {
                        case 0:
                            poly->r0 = poly->g0 = poly->b0 = r;
                            break;
                        case 1:
                            poly->r1 = poly->g1 = poly->b1 = r;
                            break;
                        case 2:
                            poly->r2 = poly->g2 = poly->b2 = r;
                            break;
                        }
                    }
                    z = RotTransPers3(&v[0], &v[1], &v[2],
                                      (s32*)&poly->x0, (s32*)&poly->x1, (s32*)&poly->x2, &p, &flag);
                    z >>= 6;
                    if (!(flag & 0x8000)) {
                        AddPrim(&fxScreenRippleOT[z], poly);
                    }
                }
            }
        }
#ifdef PLATFORM_PC
        wideRippleDraw(e);
#endif
    }
    SetGeomOffset(ofx, ofy);
    SetGeomScreen(h);
}

void freeFxScreenRipple(void* p) {
    DrawSync(0);
    freeHeap(p);
}

void fxScreenRipple_delete(TaskHeader* task) {
    registerSpriteCallback2Sub0(task + 1);
    allocateSavePointMeshDataSub0_callback(task);
    clearShapeTransferEntry(task);
}

Fx08Effect* createBattleStartEffect(void) {
    Fx08Effect* e = allocateHeap(PC_HEAP_OF(Fx08Effect, sizeof(Fx08Effect)), 1);
    e->m0_updateTask.m4_data = e;
    e->m1C_drawTask.m4_data = e;
    return initFxScreenRipple(e);
}

Fx08Effect* initFxScreenRipple(Fx08Effect* e) {
    s32 j;
    s32 i;
    s32 k;
    s32 l;
    VECTOR sq;

    if (fxScreenRippleAnimatePhase) {
        e->m44_frameCounter = 0;
        e->m38_brightness = 0x80;
        e->m40_amplitude = 0;
        e->m48_speed = 0x40;
    } else {
        e->m38_brightness = 0x80;
        e->m44_frameCounter = 0;
        e->m40_amplitude = 0;
        e->m48_speed = 0x40;
    }
    e->m3C_phase = 0;
    SquareRoot0(0x9500);
    e->m4C_translation.vx = 0;
    e->m4C_translation.vy = 0;
    e->m4C_translation.vz = 0x2000;
    e->m5C_rotation.vx = 0;
    e->m5C_rotation.vy = 0;
    e->m5C_rotation.vz = 0;
    for (i = 0; i != 2; i++) {
        for (j = 0; j != 14; j++) {
            s32 y = j << 4;

            for (k = 0; k != 20; k++) {
                Fx08Triangle* t = &e->m64_triangles[i][j][k];
                SVECTOR* src;
                s32 v2;

                if (i == 0) {
                    src = fxScreenRippleTriVertsTopLeft;
                } else {
                    src = fxScreenRippleTriVertsBottomRight;
                }
                for (l = 0, v2 = (y - 101) * 16; l != 3; l++) {
                    SVECTOR* v = WAVE_VERTEX(t, l);

                    v->vx = src[l].vx;
                    v->vy = src[l].vy;
                    v->vz = src[l].vz;
                    if (i == 0) {
                        t->m54_vertices[l].vx += (k * 16 - 155) * 16;
                        t->m54_vertices[l].vy += (y - 107) * 16;
                    } else {
                        t->m54_vertices[l].vx += (k * 16 - 149) * 16;
                        t->m54_vertices[l].vy += v2;
                    }
                    v = WAVE_VERTEX(t, l);
                    sq.vx = v->vx;
                    sq.vy = v->vy;
                    sq.vz = v->vz;
                    Square0(&sq, &sq);
                    if (fxScreenRippleAnimatePhase) {
                        t->m6C_phase[l] = SquareRoot0(sq.vx + sq.vy) * 2;
                    } else {
                        t->m6C_phase[l] = SquareRoot0(sq.vx + sq.vy) * 3 / 5;
                    }
                }
                {
                    s32 u, u2;

                    for (l = 0; l != 2; l++) {
                        POLY_GT3* poly = &t->m4_poly[l];
                        u = (k * 16) & 0x3F;
                        u2 = u + 16;
                        v2 = y + 16;

                        SetPolyGT3(poly);
                        SetShadeTex(poly, 0);
                        poly->r0 = 0x80;
                        poly->g0 = 0x80;
                        poly->b0 = 0x80;
                        poly->r1 = 0x80;
                        poly->g1 = 0x80;
                        poly->b1 = 0x80;
                        poly->r2 = 0x80;
                        poly->g2 = 0x80;
                        poly->b2 = 0x80;
                        poly->code |= 2;
                        poly->tpage = GetTPage(2, 0, k * 16 + 0x2C0, 0x100);
                        if (i == 0) {
                            poly->u0 = u;
                            poly->v0 = y;
                            poly->u1 = u2;
                            poly->v1 = y;
                            poly->u2 = u;
                            /* The nested do/while (0) only weights v2 for the register
                             * allocator (flow counts refs by loop depth): retail gives v2 s3,
                             * ahead of u2 (s4) and l (s5). Cross-jumping merges both copies. */
                            do { do { do { do { do { poly->v2 = v2; } while (0); } while (0); } while (0); } while (0); } while (0);
                        } else {
                            poly->u0 = u2;
                            poly->v0 = y;
                            poly->u1 = u2;
                            poly->v1 = v2;
                            poly->u2 = u;
                            do { do { do { do { do { poly->v2 = v2; } while (0); } while (0); } while (0); } while (0); } while (0);
                        }
                    }
                }
            }
        }
    }
    return e;
}


void playFxScreenRipple(void) {
    u8* buf = allocateHeap(0x2000, 0);

    STACK_SWITCH(buf + 0x1F00);
    runFxScreenRippleLoop();
    STACK_RESTORE();
    freeHeap(buf);
}

void runFxScreenRippleLoop(void) {
    s32 counter;
    u8 rgb[3];
    s32 x;
    u16* saved0;
    u16* saved1;
    u16* buf;
    u16* p;
    s32 i;
    u8 isbg;
    Fx08Effect* e;
    POLY_F4* poly;

    /* retail keeps x, saved0 and saved1 in stack slots allocated before r */
    (void)&x;
    (void)&saved0;
    (void)&saved1;
    {
    RECT r;
    counter = 0xA4;
    saved0 = allocateHeap(0x8000, 1);
    saved1 = allocateHeap(0x8000, 1);
    r.x = battleMechaPartyTextureLocations[2];
    r.y = battleMechaPartyTextureLocations[3];
    r.w = 0x40;
    r.h = 0x100;
    StoreImage(&r, (u32*)saved0);
    r.x = battleMechaPartyTextureLocations[4];
    r.y = battleMechaPartyTextureLocations[5];
    r.w = 0x40;
    r.h = 0x100;
    StoreImage(&r, (u32*)saved1);
    DrawSync(0);
    buf = allocateHeap(0x30000, 1);
    p = buf;
    r.x = 0;
    r.y = 0;
    r.w = 0x140;
    r.h = 0xE0;
    StoreImage(&r, (u32*)buf);
    DrawSync(0);
    for (i = 0; i != 0x14000; i++) {
        *p++ |= 0x8000;
    }
    r.x = 0x2C0;
    r.y = 0x100;
    r.w = 0x140;
    r.h = 0xE0;
    LoadImage(&r, buf);
    DrawSync(0);
#ifdef PLATFORM_PC
    wideRippleInit();
#endif
    freeHeap(buf);
    battleBssBlock.m8C50_currentRenderStruct = (battleBssBlock.m8C50_currentRenderStruct == &battleBssBlock.mB70_renderStructs[0]) ? &battleBssBlock.mB70_renderStructs[1] : &battleBssBlock.mB70_renderStructs[0];
    battleBssBlock.m8C54_currentOT = battleBssBlock.m8C50_currentRenderStruct->m70_ot;
    ClearOTagR(battleBssBlock.m8C54_currentOT, 0x1000);
    rgb[0] = battleBssBlock.mB70_renderStructs[1].m0_drawEnv.r0;
    rgb[1] = battleBssBlock.mB70_renderStructs[1].m0_drawEnv.g0;
    rgb[2] = battleBssBlock.mB70_renderStructs[1].m0_drawEnv.b0;
    isbg = battleBssBlock.mB70_renderStructs[0].m0_drawEnv.isbg;
    battleBssBlock.m8C84_oddOrEven = 0;
    battleBssBlock.m8C50_currentRenderStruct = &battleBssBlock.mB70_renderStructs[0];
    battleBssBlock.mB70_renderStructs[0].m0_drawEnv.isbg = 0;
    battleBssBlock.mB70_renderStructs[1].m0_drawEnv.isbg = 0;
    battleBssBlock.mB70_renderStructs[0].m0_drawEnv.r0 = 0;
    battleBssBlock.mB70_renderStructs[1].m0_drawEnv.r0 = 0;
    battleBssBlock.mB70_renderStructs[0].m0_drawEnv.g0 = 0;
    battleBssBlock.mB70_renderStructs[1].m0_drawEnv.g0 = 0;
    battleBssBlock.mB70_renderStructs[0].m0_drawEnv.b0 = 0;
    battleBssBlock.mB70_renderStructs[1].m0_drawEnv.b0 = 0;
    e = createBattleStartEffect();
    while (counter != 0) {
        if (counter > 0) {
            counter--;
        }
        battleBssBlock.mB70_renderStructs[battleBssBlock.m8C84_oddOrEven].m0_drawEnv.r0 = addClamp255(battleBssBlock.mB70_renderStructs[battleBssBlock.m8C84_oddOrEven].m0_drawEnv.r0, -0xC);
        battleBssBlock.mB70_renderStructs[battleBssBlock.m8C84_oddOrEven].m0_drawEnv.g0 = addClamp255(battleBssBlock.mB70_renderStructs[battleBssBlock.m8C84_oddOrEven].m0_drawEnv.g0, -0xC);
        battleBssBlock.mB70_renderStructs[battleBssBlock.m8C84_oddOrEven].m0_drawEnv.b0 = addClamp255(battleBssBlock.mB70_renderStructs[battleBssBlock.m8C84_oddOrEven].m0_drawEnv.b0, -0xC);
        battleBssBlock.m8C50_currentRenderStruct = (battleBssBlock.m8C50_currentRenderStruct == &battleBssBlock.mB70_renderStructs[0]) ? &battleBssBlock.mB70_renderStructs[1] : &battleBssBlock.mB70_renderStructs[0];
        battleBssBlock.m8C54_currentOT = battleBssBlock.m8C50_currentRenderStruct->m70_ot;
        ClearOTagR(battleBssBlock.m8C54_currentOT, 0x1000);
        battleBssBlock.m8C84_oddOrEven = 1 - battleBssBlock.m8C84_oddOrEven;
        fxScreenRippleOT = battleBssBlock.m8C54_currentOT;
        DrawSync(0);
        VSync(2);
        fxScreenRipple_update((TaskHeader*)e);
        fxScreenRipple_draw((TaskHeader*)e);
        PutDispEnv(&battleBssBlock.m8C50_currentRenderStruct->m5C_dispEnv);
        PutDrawEnv(&battleBssBlock.m8C50_currentRenderStruct->m0_drawEnv);
        DrawOTag(&battleBssBlock.m8C50_currentRenderStruct->m70_ot[0xFFF]);
    }
    DrawSync(0);
    VSync(2);
    ClearOTagR(battleBssBlock.m8C54_currentOT, 0x1000);
    DrawSync(0);
    VSync(2);
    r.x = battleMechaPartyTextureLocations[2];
    r.y = battleMechaPartyTextureLocations[3];
    r.w = 0x40;
    r.h = 0x100;
    LoadImage(&r, saved0);
    r.x = battleMechaPartyTextureLocations[4];
    r.y = battleMechaPartyTextureLocations[5];
    r.w = 0x40;
    r.h = 0x100;
    LoadImage(&r, saved1);
    freeHeap(saved0);
    freeHeap(saved1);
    DrawSync(0);
#ifdef PLATFORM_PC
    pcWideKeepMargins(0);
#endif
    r.x = 0;
    r.y = 0;
    r.w = 0x140;
    r.h = 0x1C0;
    ClearImage(&r, 0, 0, 0);
    DrawSync(0);
    VSync(2);
    poly = (POLY_F4*)shapeTransfertTableCurrentEntry;
    shapeTransfertTableCurrentEntry += sizeof(POLY_F4);
    SetPolyF4(poly);
    poly->r0 = 0;
    poly->g0 = 0;
    poly->b0 = 0;
    poly->x0 = 0;
    poly->y0 = 0;
    poly->x1 = 0x140;
    poly->y1 = 0;
    poly->x2 = 0;
    poly->y2 = 0xF0;
    poly->x3 = 0x140;
    poly->y3 = 0xF0;
    AddPrim(&battleBssBlock.m8C54_currentOT[0xFFE], poly);
    battleBssBlock.mB70_renderStructs[1].m0_drawEnv.isbg = isbg;
    battleBssBlock.mB70_renderStructs[0].m0_drawEnv.isbg = isbg;
    battleBssBlock.mB70_renderStructs[1].m0_drawEnv.r0 = rgb[0];
    battleBssBlock.mB70_renderStructs[0].m0_drawEnv.r0 = rgb[0];
    battleBssBlock.mB70_renderStructs[1].m0_drawEnv.g0 = rgb[1];
    battleBssBlock.mB70_renderStructs[0].m0_drawEnv.g0 = rgb[1];
    battleBssBlock.mB70_renderStructs[1].m0_drawEnv.b0 = rgb[2];
    battleBssBlock.mB70_renderStructs[0].m0_drawEnv.b0 = rgb[2];
    freeFxScreenRipple(e);
    }
}
