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
#include "gte_pc.h"
#include "battle/pc_wide_fx.h"

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
        wideRippleDraw(e);
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
    Fx08Effect* e = allocateHeap(sizeof(Fx08Effect), 1);
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


/* Enhanced, 60 Hz battles (xg-hbc.19.2/.19.4): the screen ripple runs at the
 * native cadence, a frame every two vblanks (VSync(2)), the PS1 showing each
 * picture for two. Its state stays the Original's; between two frames the
 * picture halfway is drawn, as the shatter's (b_800B6F0C.c): inside the
 * VSync(2) wait, once its first vblank has come (libetc.c pcVSyncBetween,
 * PC-only work: no stepped-clock time, GTE put back), into the buffer on
 * display, which the frame after draws over whole. Shown during the wait is
 * the frame before last (double buffered, no clear: the mesh covers the
 * screen), so what each frame used is kept for three: the ripple's phase,
 * amplitude, brightness, depth and turn, put in halfway for one draw of
 * fxScreenRipple_draw into its own ordering table (the other buffer's
 * primitives, already drawn), then taken back. The effect never clears its
 * buffers: the native frame draws its mesh over the picture of two frames
 * ago, and where the mesh recedes that picture shows (the smear is part of
 * the look; the meshes turn, so they do not nest), so the buffer's picture
 * is kept before the picture between is drawn and put back after the wait
 * (StoreImage/LoadImage of the buffer, ripple60Restore, no stepped-clock
 * time) before the native frame draws: every native frame's pixels stay the
 * Original's. The buffer's wide picture (the margins and the shifted 2D,
 * which the 4:3 pair misses) is kept and put back the same way
 * (pcWideFrameKeep/PutBack, port/src/gpu/wide.c). */
int pcGameplayBattle60(void);
void pcVSyncBetween(void (*draw)(void));
void pcWideStash(int on);
int pcWideFrameKeep(const void* rect); /* pc_wide.h */
void pcWideFramePutBack(void);
void hwClockHold(int on); /* port/src/system/hw.c */
typedef struct {
    s32 phase;
    s32 amplitude;
    s32 brightness;
    s32 tz;
    s32 rz;
} Ripple60State;
static Ripple60State ripple60[3]; /* by frame, mod 3 */
static s32 ripple60Frames;        /* frames built since the effect began */
static TaskHeader* ripple60Task; /* the effect's task (its m4_data the Fx08Effect) */
static OTag ripple60OT[0x1000];
static s32 ripple60Drawn; /* a picture between was drawn into the buffer on display */
static u16 ripple60Kept[0x140 * 0xE0]; /* that buffer's picture before it */
static RECT ripple60KeptRect;

static void ripple60Record(Fx08Effect* e) {
    Ripple60State* r = &ripple60[ripple60Frames % 3];
    r->phase = e->m3C_phase;
    r->amplitude = e->m40_amplitude;
    r->brightness = e->m38_brightness;
    r->tz = e->m4C_translation.vz;
    r->rz = e->m5C_rotation.vz;
    ripple60Frames++;
}

static s32 ripple60Mid(s32 a, s32 b) {
    return a + (b - a) / 2;
}
static s16 ripple60Angle(s16 a, s16 b) {
    return (s16)(a + (s16)(b - a) / 2);
}

/* The mesh from a state, into the buffer on display (the one the frame
 * after the wait draws over), with the other buffer's primitives. */
static void ripple60DrawState(Fx08Effect* e, const Ripple60State* s) {
    Ripple60State keep;
    GteState gte;
    OTag* ot;
    s32 cur;

    keep.phase = e->m3C_phase;
    keep.amplitude = e->m40_amplitude;
    keep.brightness = e->m38_brightness;
    keep.tz = e->m4C_translation.vz;
    keep.rz = e->m5C_rotation.vz;
    e->m3C_phase = s->phase;
    e->m40_amplitude = s->amplitude;
    e->m38_brightness = s->brightness;
    e->m4C_translation.vz = s->tz;
    e->m5C_rotation.vz = s->rz;
    gteSave(&gte);
    pcWideStash(1);
    ClearOTagR(ripple60OT, 0x1000);
    ot = fxScreenRippleOT;
    fxScreenRippleOT = ripple60OT;
    cur = battleBssBlock.m8C84_oddOrEven;
    battleBssBlock.m8C84_oddOrEven = 1 - cur; /* the primitives the frame before drew with */
    fxScreenRipple_draw(ripple60Task);
    battleBssBlock.m8C84_oddOrEven = cur;
    fxScreenRippleOT = ot;
    /* the buffer on display: the one this frame draws over after the wait */
    PutDrawEnv(&battleBssBlock.mB70_renderStructs[cur].m0_drawEnv);
    DrawOTag(&ripple60OT[0xFFF]);
    pcWideStash(0);
    gteLoad(&gte);
    e->m3C_phase = keep.phase;
    e->m40_amplitude = keep.amplitude;
    e->m38_brightness = keep.brightness;
    e->m4C_translation.vz = keep.tz;
    e->m5C_rotation.vz = keep.rz;
}

/* Inside the frame's VSync(2), at its first vblank: the picture halfway
 * between the two frames before it, into the buffer on display. */
static void ripple60Between(void) {
    Fx08Effect* e = ripple60Task != NULL ? (Fx08Effect*)ripple60Task->m4_data : NULL;
    s32 f = ripple60Frames; /* built: frames 0..f-1; f-2 on display, f-1 next */
    Ripple60State* a;
    Ripple60State* b;
    Ripple60State mid;

    if (e == NULL || f < 2) {
        return;
    }
    a = &ripple60[(f - 2) % 3];
    b = &ripple60[(f - 1) % 3];
    mid.phase = ripple60Angle((s16)a->phase, (s16)b->phase);
    mid.amplitude = ripple60Mid(a->amplitude, b->amplitude);
    mid.brightness = ripple60Mid(a->brightness, b->brightness);
    mid.tz = ripple60Mid(a->tz, b->tz);
    mid.rz = ripple60Angle((s16)a->rz, (s16)b->rz);
    ripple60KeptRect = battleBssBlock.mB70_renderStructs[battleBssBlock.m8C84_oddOrEven].m0_drawEnv.clip;
    if (ripple60KeptRect.w > 0x140) ripple60KeptRect.w = 0x140;
    if (ripple60KeptRect.h > 0xE0) ripple60KeptRect.h = 0xE0;
    StoreImage(&ripple60KeptRect, (u32*)ripple60Kept);
    DrawSync(0);
    pcWideFrameKeep(&ripple60KeptRect);
    ripple60DrawState(e, &mid);
    ripple60Drawn = 1;
}

/* After the wait: the buffer's picture back where the picture between was. */
static void ripple60Restore(void) {
    if (!ripple60Drawn) {
        return;
    }
    ripple60Drawn = 0;
    hwClockHold(1);
    LoadImage(&ripple60KeptRect, (u32*)ripple60Kept);
    DrawSync(0);
    pcWideFramePutBack();
    hwClockHold(0);
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
    wideRippleInit();
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
    ripple60Task = (TaskHeader*)e; /* Enhanced, 60 Hz (above) */
    ripple60Frames = 0;
    ripple60Drawn = 0;
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
        pcVSyncBetween(pcGameplayBattle60() ? ripple60Between : NULL); /* Enhanced, 60 Hz (above) */
        VSync(2);
        pcVSyncBetween(NULL);
        ripple60Restore();
        fxScreenRipple_update((TaskHeader*)e);
        fxScreenRipple_draw((TaskHeader*)e);
        ripple60Record((Fx08Effect*)((TaskHeader*)e)->m4_data);
        PutDispEnv(&battleBssBlock.m8C50_currentRenderStruct->m5C_dispEnv);
        PutDrawEnv(&battleBssBlock.m8C50_currentRenderStruct->m0_drawEnv);
        DrawOTag(&battleBssBlock.m8C50_currentRenderStruct->m70_ot[0xFFF]);
    }
    ripple60Task = NULL; /* Enhanced, 60 Hz: the effect ends (the waits below show its last frames) */
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
    pcWideKeepMargins(0);
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
