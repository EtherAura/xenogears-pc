/*
 * JV3 STATUS (round 21, JP variants). US: preset (gcc-2.7.2-cdk, ASPSX 2.56).
 * JP: gcc-2.6.3-psx, ASPSX 2.34, -G0, no divide checks (2.6.0 matches every
 * function except advanceCustomPolyPacket; 2.7.2-psx/cdk do not). Check with
 * work/agents/JV3/t.sh <file> gcc-2.6.3-psx (US via --as, JP explicit flags,
 * -I work/agents/JV3/include, JP symbols from jp_extra_symbols.txt).
 * Every fallback below is now ONE shared body (no #if VERSION_JP needed):
 *   updateBattleStartEffect OK us/jp   drawBattleStartEffectTask OK us/jp   renderBattleStartEffect OK us/jp
 *   createBattleStartEffectTask OK us/jp   createBattleStartEffect OK us/jp   battleStartEffect OK us/jp
 * JP symbols needed: see jp_extra_symbols.txt (pBattleStartEffectOT, battleBssBlock,
 * battleStartShardVertsTopLeft, battleStartShardVertsBottomRight).
 */
/*
 * Battle start effect (Noah battleStartEffect.cpp). 0x800B6F0C-0x800B7C28;
 * gcc-2.7.2-cdk, ASPSX 2.56.
 */
#include "common.h"
#include "psyq.h"
#include "gte.h"
#include "main/memory.h"
#include "main/task.h"
#include "main/filesystem.h"
#include "stack.h"
#include "battle/battle.h"
#ifdef PLATFORM_PC
#include "gte_pc.h"
#include "battle/pc_wide_fx.h"
#endif

/* main */
extern OTag* characterRenderingOT;
s32 addClamp255(s32 value, s32 delta);
TaskHeader* allocateTaskPair(s32 size, TaskHeader* owner, void (*update)(TaskHeader*), void (*draw)(TaskHeader*), void (*del)(TaskHeader*));
void registerSpriteCallback2Sub0(TaskHeader* task);
void allocateSavePointMeshDataSub0_callback(TaskHeader* task);
void clearShapeTransferEntry(void* data);
void resetSpriteCallbacks(void);
void checkSoftReboot(void);
void startBattleLoadingDuringEffect2(void);
void battleLoaderTick(u8 step);
MATRIX* RotMatrixZYX(SVECTOR* r, MATRIX* m); /* battle loader overlay; Noah: battleLoaderTick */

/* battle */
extern SVECTOR battleStartShardVertsTopLeft[3];
extern SVECTOR battleStartShardVertsBottomRight[3];
extern VECTOR effectGravity;
extern OTag* pBattleStartEffectOT;
sBattleStartEffect* initBattleStartEffect(sBattleStartEffect* e);
void renderBattleStartEffect(TaskHeader* task);
void updateBattleStartEffect(TaskHeader* task);
sBattleStartEffect* createBattleStartEffect(void);
void freeBattleStartEffect(void* p);

#ifdef PLATFORM_PC
/*
 * The wide picture (include/pc_wide.h): the screen shatters across the
 * margins too. The capture keeps the wide picture's margins
 * (pcWideCaptureMask at the battle start, pcWideCapture after opcode 0x18's
 * MoveImage), the 4:3 triangles sample it as before (PC_WIDE_HANDLED: their
 * wide copies read the capture's centre), and extra 16-pixel columns cut
 * from the margins break away the same way. Those are the PC's own: their
 * random numbers come from a local generator (the game's rand() sequence
 * stays the PS1's), they live outside the game heap, are drawn only into the
 * wide picture, and the GTE is driven directly (battle/pc_wide_fx.h) and put
 * back as the PS1 leaves it. A capture without margins (the screen before was
 * 4:3) leaves the frames 4:3.
 */
#define WIDE_SHATTER_COLS 24 /* per side, 16 pixels each: the widest margin */

typedef struct {
    void* owner;
    s32 cols; /* per side; 0: none */
    sBattleStartEffectSub t[2][14][2 * WIDE_SHATTER_COLS];
} WideShatter;

static WideShatter wideShatter[2];
static s32 wideShatterMargin; /* the next effect's capture */
static u32 wideShatterSeed;

/* The capture of opcode 0x18 (b_800B3E04.c), or of the battle start. */
void pcWideShatterCapture(s32 margin) {
    wideShatterMargin = margin;
}

static s32 wideShatterRand(void) {
    wideShatterSeed = wideShatterSeed * 1103515245u + 12345u;
    return (s32)((wideShatterSeed >> 16) & 0x7FFF);
}

static WideShatter* wideShatterOf(void* owner) {
    s32 i;

    for (i = 0; i < 2; i++) {
        if (wideShatter[i].owner == owner && wideShatter[i].cols > 0) {
            return &wideShatter[i];
        }
    }
    return NULL;
}

static void wideShatterFree(void* owner) {
    WideShatter* w = wideShatterOf(owner);

    if (w != NULL) {
        w->owner = NULL;
        w->cols = 0;
    }
}

/* The x of margin column c: left of the screen first, then right of it. */
static s32 wideShatterX(WideShatter* w, s32 c) {
    return c < w->cols ? (c - w->cols) * 16 : 0x140 + (c - w->cols) * 16;
}

/* initBattleStartEffect for the margin columns. */
static void wideShatterInit(sBattleStartEffect* e) {
    WideShatter* w;
    s32 base, i, j, c, l, kx, jx, gz;
    SVECTOR rot;
    MATRIX m;

    wideShatterFree(e);
    if (wideShatterMargin <= 0) {
        return;
    }
    w = wideShatter[0].cols == 0 ? &wideShatter[0] : &wideShatter[1];
    w->owner = e;
    w->cols = (wideShatterMargin + 15) / 16;
    if (w->cols > WIDE_SHATTER_COLS) {
        w->cols = WIDE_SHATTER_COLS;
    }
    wideShatterMargin = 0;
    wideShatterSeed = 0x5EED;
    base = pcWideSqrt(0x9500) << 10;
    for (i = 0; i != 2; i++) {
        for (j = 0; j != 14; j++) {
            for (c = 0; c < 2 * w->cols; c++) {
                sBattleStartEffectSub* p = &w->t[i][j][c];
                s32 dist, len, angle;

                kx = wideShatterX(w, c);
                jx = j << 4;
                p->m0_rotation.vx = p->m0_rotation.vy = p->m0_rotation.vz = 0;
                p->m10_position.vx = (kx - (i == 0 ? 0x9B : 0x95)) * 32;
                p->m10_position.vy = j * 0x200 - (i == 0 ? 0xD60 : 0xCA0);
                p->m10_position.vz = 0x4000;
                gz = -0x1F40000 - ((wideShatterRand() % 1000) << 16);
                dist = pcWideSqrt(p->m10_position.vx * p->m10_position.vx + p->m10_position.vy * p->m10_position.vy);
                p->m64_delay = dist / 1024;
                angle = pcWideAtan2(p->m10_position.vy, p->m10_position.vx) + 0x600;
                len = (dist << 11) / base - 0x20 + wideShatterRand() % 64;
                rot.vx = 0;
                rot.vy = len;
                rot.vz = angle + wideShatterRand() % 1024;
                /* The PS1's gravity (0, 0, gz) turned by rot: the matrix's
                 * third column times gz. */
                createRotationMatrix(&rot, &m);
                p->m68_velocity.vx = (s32)(((s64)m.m[0][2] * gz) >> 12);
                p->m68_velocity.vy = (s32)(((s64)m.m[1][2] * gz) >> 12);
                p->m68_velocity.vz = (s32)(((s64)m.m[2][2] * gz) >> 12);
                p->m78_gravity = 0x70800 - (wideShatterRand() % 1600) * 256;
                p->m8_rotationStep.vx = (wideShatterRand() & 0xFF) - 0x7F;
                p->m8_rotationStep.vy = (wideShatterRand() & 0xFF) - 0x7F;
                p->m8_rotationStep.vz = (wideShatterRand() & 0x1FF) - 0xFF;
                /* Texels from the capture's left edge, moved to the column in
                 * the wide pass (pcWidePrimTex). */
                for (l = 0; l != 2; l++) {
                    POLY_FT3* poly = &p->m24_poly[l];

                    SetPolyFT3(poly);
                    SetShadeTex(poly, 0);
                    poly->r0 = poly->g0 = poly->b0 = 0xFF;
                    poly->code &= ~2;
                    poly->tpage = GetTPage(2, 1, 0x2C0, 0x100);
                    poly->u0 = i == 0 ? 0 : 16;
                    poly->v0 = jx;
                    poly->u1 = 16;
                    poly->v1 = i == 0 ? jx : jx + 16;
                    poly->u2 = 0;
                    poly->v2 = jx + 16;
                }
            }
        }
    }
}

static void wideShatterUpdate(sBattleStartEffect* e) {
    WideShatter* w = wideShatterOf(e);
    s32 i, j, c;

    for (i = 0; w != NULL && i != 2; i++) {
        for (j = 0; j != 14; j++) {
            for (c = 0; c < 2 * w->cols; c++) {
                sBattleStartEffectSub* p = &w->t[i][j][c];

                if (p->m64_delay != 0) {
                    p->m64_delay--;
                } else {
                    POLY_FT3* poly = &p->m24_poly[battleBssBlock.m8C84_oddOrEven];
                    poly->r0 = addClamp255(poly->r0, -6);
                    poly->g0 = addClamp255(poly->g0, -6);
                    poly->b0 = addClamp255(poly->b0, -6);
                    p->m10_position.vx += p->m68_velocity.vx >> 16;
                    p->m10_position.vy += p->m68_velocity.vy >> 16;
                    p->m10_position.vz += p->m68_velocity.vz >> 16;
                    p->m0_rotation.vx += p->m8_rotationStep.vx;
                    p->m0_rotation.vy += p->m8_rotationStep.vy;
                    p->m0_rotation.vz += p->m8_rotationStep.vz;
                    p->m68_velocity.vy -= p->m68_velocity.vy / 16;
                    p->m68_velocity.vx -= p->m68_velocity.vx / 16;
                    p->m68_velocity.vz -= p->m68_velocity.vz / 16;
                    p->m68_velocity.vy += p->m78_gravity;
                }
            }
        }
    }
}

/* After renderBattleStartEffect's loop, its geometry offset and screen set. */
static void wideShatterRender(sBattleStartEffect* e) {
    WideShatter* w = wideShatterOf(e);
    GteState gte;
    s32 i, j, k, c;

    if (w == NULL || pcWideView == 0) {
        return;
    }
    for (i = 0; i != 2; i++) {
        for (j = 0; j != 14; j++) {
            for (k = 0; k != 20; k++) {
                sBattleStartEffectSub* s = &e->m3C_triangleData[i][j][k];
                if (s->m10_position.vz > 0x3F) {
                    pcWidePrim(&s->m24_poly[battleBssBlock.m8C84_oddOrEven], 0, PC_WIDE_HANDLED);
                }
            }
        }
    }
    gteSave(&gte);
    for (i = 0; i != 2; i++) {
        for (j = 0; j != 14; j++) {
            for (c = 0; c < 2 * w->cols; c++) {
                sBattleStartEffectSub* s = &w->t[i][j][c];
                POLY_FT3* poly = &s->m24_poly[battleBssBlock.m8C84_oddOrEven];
                if (s->m10_position.vz > 0x3F) {
                    MATRIX m;
                    SVECTOR* v = i == 0 ? battleStartShardVertsTopLeft : battleStartShardVertsBottomRight;
                    s32 flag, depth;

                    createRotationMatrix(&s->m0_rotation, &m);
                    pcWideSetMatrix(&m, &s->m10_position);
                    depth = pcWideRotTransPers3(&v[0], &v[1], &v[2], &poly->x0, &poly->x1, &poly->x2, &flag) >> 6;
                    AddPrim(&pBattleStartEffectOT[depth], poly);
                    pcWidePrimTex(poly, 0, PC_WIDE_ONLY, wideShatterX(w, c));
                }
            }
        }
    }
    gteLoad(&gte);
}
#endif

void updateBattleStartEffect(TaskHeader* task) {
    sBattleStartEffect* e = (sBattleStartEffect*)task->m4_data;
    s32 i, j, k;
    SVECTOR v;

    e->m38_frameCounter++;
    for (i = 0; i != 2; i++) {
        for (j = 0; j != 14; j++) {
            for (k = 0; k != 20; k++) {
                sBattleStartEffectSub* p = &e->m3C_triangleData[i][j][k];
                if (p->m64_delay != 0) {
                    p->m64_delay--;
                } else {
                    POLY_FT3* poly = &p->m24_poly[battleBssBlock.m8C84_oddOrEven];
                    poly->r0 = addClamp255(poly->r0, -6);
                    poly->g0 = addClamp255(poly->g0, -6);
                    poly->b0 = addClamp255(poly->b0, -6);
                    v.vx = p->m68_velocity.vx >> 16;
                    v.vy = p->m68_velocity.vy >> 16;
                    v.vz = p->m68_velocity.vz >> 16;
                    p->m10_position.vx += v.vx;
                    p->m10_position.vy += v.vy;
                    p->m10_position.vz += v.vz;
                    p->m0_rotation.vx += p->m8_rotationStep.vx;
                    p->m0_rotation.vy += p->m8_rotationStep.vy;
                    p->m0_rotation.vz += p->m8_rotationStep.vz;
                    p->m68_velocity.vy -= p->m68_velocity.vy / 16;
                    p->m68_velocity.vx -= p->m68_velocity.vx / 16;
                    p->m68_velocity.vz -= p->m68_velocity.vz / 16;
                    p->m68_velocity.vy += p->m78_gravity;
                }
            }
        }
    }
#ifdef PLATFORM_PC
    wideShatterUpdate(e);
#endif
}

void drawBattleStartEffectTask(TaskHeader* task);
void drawBattleStartEffectTask(TaskHeader* task) {
    pBattleStartEffectOT = characterRenderingOT;
    renderBattleStartEffect(task);
}

void renderBattleStartEffect(TaskHeader* task) {
    sBattleStartEffect* e = (sBattleStartEffect*)task->m4_data;
    s32 ofx, ofy;
    s32 h;
    s32 i, j, k;

    ReadGeomOffset(&ofx, &ofy);
    h = ReadGeomScreen();
    SetGeomOffset(0xA0, 0x70);
    SetGeomScreen(0x200);
    for (i = 0; i != 2; i++) {
        for (j = 0; j != 14; j++) {
            for (k = 0; k != 20; k++) {
                sBattleStartEffectSub* s = &e->m3C_triangleData[i][j][k];
                POLY_FT3* poly = &s->m24_poly[battleBssBlock.m8C84_oddOrEven];
                if (s->m10_position.vz > 0x3F) {
                    MATRIX m;
                    SVECTOR* v;
                    s32 p, flag;
                    createRotationMatrix(&s->m0_rotation, &m);
                    TransMatrix(&m, &s->m10_position);
                    SetRotMatrix(&m);
                    SetTransMatrix(&m);
                    if (i == 0) {
                        v = battleStartShardVertsTopLeft;
                    } else {
                        v = battleStartShardVertsBottomRight;
                    }
                    {
                        s32 depth = RotTransPers3(&v[0], &v[1], &v[2], (s32*)&poly->x0, (s32*)&poly->x1, (s32*)&poly->x2, &p, &flag) >> 6;
                        AddPrim(&pBattleStartEffectOT[depth], poly);
                    }
                }
            }
        }
    }
#ifdef PLATFORM_PC
    wideShatterRender(e);
#endif
    SetGeomOffset(ofx, ofy);
    SetGeomScreen(h);
}

void freeBattleStartEffect(void* p) {
    DrawSync(0);
#ifdef PLATFORM_PC
    wideShatterFree(p);
#endif
    freeHeap(p);
}

void deleteBattleStartEffectTask(TaskHeader* task) {
#ifdef PLATFORM_PC
    wideShatterFree(task);
#endif
    registerSpriteCallback2Sub0(task + 1);
    allocateSavePointMeshDataSub0_callback(task);
    clearShapeTransferEntry(task);
}

sBattleStartEffect* createBattleStartEffectTask(void);
sBattleStartEffect* createBattleStartEffectTask(void) {
    return initBattleStartEffect((sBattleStartEffect*)allocateTaskPair(sizeof(sBattleStartEffect), NULL, updateBattleStartEffect, drawBattleStartEffectTask, deleteBattleStartEffectTask));
}

sBattleStartEffect* createBattleStartEffect(void) {
    sBattleStartEffect* e = allocateHeap(sizeof(sBattleStartEffect), 1);
    e->m0_updateTask.m4_data = e;
    e->m1C_drawTask.m4_data = e;
    return initBattleStartEffect(e);
}

/* Noah: initBattleStartEffect (the screen-shatter triangles). Matching
 * notes: tx is declared in a block around the poly loop (its spill slot must
 * come after the loop-hoisted row values), the SquareRoot0 result gets its
 * own variable, angle += 0x600 is written inside the sum, len - 0x20 goes
 * through a block temp, and l = 0 is set ahead of the u/v values (dbr fills
 * rand's delay slot with it). */
sBattleStartEffect* initBattleStartEffect(sBattleStartEffect* e) {
    s32 base;
    s32 j;
    s32 i;
    s32 k;
    s32 l;
    s32 kx;
    VECTOR sq;
    SVECTOR rot;
    MATRIX m;

    e->m38_frameCounter = 0;
    base = SquareRoot0(0x9500) << 10;
    for (i = 0; i != 2; i++) {
        for (j = 0; j != 14; j++) {
            for (k = 0, kx = 0; k != 20; k++, kx += 16) {
                sBattleStartEffectSub* p = &e->m3C_triangleData[i][j][k];
                s32 jx = j << 4;
                s32 dist, len, angle, angle2, r, r2, r3;
                s32 u, u2, v2;

                p->m0_rotation.vx = 0;
                p->m0_rotation.vy = 0;
                p->m0_rotation.vz = 0;
                if (i == 0) {
                    p->m10_position.vx = (kx - 0x9B) * 32;
                    p->m10_position.vy = j * 0x200 - 0xD60;
                    p->m10_position.vz = 0x4000;
                } else {
                    p->m10_position.vx = (kx - 0x95) * 32;
                    p->m10_position.vy = j * 0x200 - 0xCA0;
                    p->m10_position.vz = 0x4000;
                }
                effectGravity.vz = -0x1F40000;
                {
                    s32 rv = rand();
                    effectGravity.vz += ((rv / 1000) * 1000 - rv) << 16;
                }
                Square0(&p->m10_position, &sq);
                dist = SquareRoot0(sq.vx + sq.vy);
                p->m64_delay = (base / 32 - dist) / 2048;
                p->m64_delay = dist / 1024;
                angle = ratan2(p->m10_position.vy, p->m10_position.vx);
                r = rand();
                angle2 = (angle += 0x600) + r % 1024;
                len = (dist << 11) / base;
                r2 = rand();
                {
                    s32 t = len - 0x20;
                    len = t + r2 % 64;
                }
                rot.vx = 0;
                rot.vy = len;
                rot.vz = angle2;
                RotMatrixZYX(&rot, &m);
                ApplyMatrixLV(&m, &effectGravity, &p->m68_velocity);
                r3 = rand();
                p->m78_gravity = 0x70800 - (r3 % 1600) * 256;
                p->m8_rotationStep.vx = (rand() & 0xFF) - 0x7F;
                p->m8_rotationStep.vy = (rand() & 0xFF) - 0x7F;
                p->m8_rotationStep.vz = (rand() & 0x1FF) - 0xFF;
                {
                    s32 tx = kx;

                    l = 0;
                    u = kx & 0x3F;
                    u2 = u + 0x10;
                    v2 = jx + 0x10;
                    for (; l != 2; l++) {
                        POLY_FT3* poly = &p->m24_poly[l];
                        SetPolyFT3(poly);
                        SetShadeTex(poly, 0);
                        poly->r0 = 0xFF;
                        poly->g0 = 0xFF;
                        poly->b0 = 0xFF;
                        poly->code &= ~2;
                        poly->tpage = GetTPage(2, 1, tx + 0x2C0, 0x100);
                        if (i == 0) {
                            poly->u0 = u;
                            poly->v0 = jx;
                            poly->u1 = u2;
                            poly->v1 = jx;
                            poly->u2 = u;
                            poly->v2 = v2;
                        } else {
                            poly->u0 = u2;
                            poly->v0 = jx;
                            poly->u1 = u2;
                            poly->v1 = v2;
                            poly->u2 = u;
                            poly->v2 = v2;
                        }
                    }
                }
            }
        }
    }
#ifdef PLATFORM_PC
    wideShatterInit(e);
#endif
    return e;
}

void battleStartEffect(void);
void battleStartEffect(void) {
    s32 counter;
    s32 state;
    s32 n;
    u16* buf;
    u16* p;
    RECT r;
    s32 i;
    sBattleStartEffect* e;

    counter = 0x56;
    resetSpriteCallbacks();
    state = 1;
    n = 0;
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
    {
        RECT s = {0, 0, 0x140, 0xE0};
        pcWideShatterCapture(pcWideCaptureMask(&s, 0x2C0, 0x100));
    }
#endif
    freeHeap(buf);
    battleBssBlock.m8C50_currentRenderStruct = (battleBssBlock.m8C50_currentRenderStruct == &battleBssBlock.mB70_renderStructs[0]) ? &battleBssBlock.mB70_renderStructs[1] : &battleBssBlock.mB70_renderStructs[0];
    battleBssBlock.m8C54_currentOT = battleBssBlock.m8C50_currentRenderStruct->m70_ot;
    ClearOTagR(battleBssBlock.m8C54_currentOT, 0x1000);
    battleBssBlock.m8C84_oddOrEven = 0;
    battleBssBlock.m8C50_currentRenderStruct = &battleBssBlock.mB70_renderStructs[0];
    battleBssBlock.mB70_renderStructs[0].m0_drawEnv.isbg = 1;
    battleBssBlock.mB70_renderStructs[1].m0_drawEnv.isbg = 1;
    battleBssBlock.mB70_renderStructs[0].m0_drawEnv.r0 = 0xFF;
    battleBssBlock.mB70_renderStructs[1].m0_drawEnv.r0 = 0xFF;
    battleBssBlock.mB70_renderStructs[0].m0_drawEnv.g0 = 0xFF;
    battleBssBlock.mB70_renderStructs[1].m0_drawEnv.g0 = 0xFF;
    battleBssBlock.mB70_renderStructs[0].m0_drawEnv.b0 = 0xFF;
    battleBssBlock.mB70_renderStructs[1].m0_drawEnv.b0 = 0xFF;
    e = createBattleStartEffect();
    while (counter != 0 || state != 5) {
        if (counter > 0) {
            counter--;
        }
        battleBssBlock.m8C50_currentRenderStruct = (battleBssBlock.m8C50_currentRenderStruct == &battleBssBlock.mB70_renderStructs[0]) ? &battleBssBlock.mB70_renderStructs[1] : &battleBssBlock.mB70_renderStructs[0];
        battleBssBlock.m8C54_currentOT = battleBssBlock.m8C50_currentRenderStruct->m70_ot;
        ClearOTagR(battleBssBlock.m8C54_currentOT, 0x1000);
        battleBssBlock.m8C84_oddOrEven = 1 - battleBssBlock.m8C84_oddOrEven;
        pBattleStartEffectOT = battleBssBlock.m8C54_currentOT;
        if (isCDBusy() == 0) {
            switch (state) {
            case 0:
                break;
            case 2:
                startBattleLoadingDuringEffect2();
                state++;
                break;
            case 1:
            case 3:
            case 4:
                battleLoaderTick(n);
                n++;
                state++;
                break;
            }
        }
        checkSoftReboot();
        STACK_SWITCH(PSX_SCRATCH(0x1F8003FC));
        updateBattleStartEffect((TaskHeader*)e);
        renderBattleStartEffect((TaskHeader*)e);
        STACK_RESTORE();
        DrawSync(0);
        VSync(2);
        battleBssBlock.mB70_renderStructs[battleBssBlock.m8C84_oddOrEven].m0_drawEnv.r0 = addClamp255(battleBssBlock.mB70_renderStructs[battleBssBlock.m8C84_oddOrEven].m0_drawEnv.r0, -0xC);
        battleBssBlock.mB70_renderStructs[battleBssBlock.m8C84_oddOrEven].m0_drawEnv.g0 = addClamp255(battleBssBlock.mB70_renderStructs[battleBssBlock.m8C84_oddOrEven].m0_drawEnv.g0, -0xC);
        battleBssBlock.mB70_renderStructs[battleBssBlock.m8C84_oddOrEven].m0_drawEnv.b0 = addClamp255(battleBssBlock.mB70_renderStructs[battleBssBlock.m8C84_oddOrEven].m0_drawEnv.b0, -0xC);
        PutDispEnv(&battleBssBlock.m8C50_currentRenderStruct->m5C_dispEnv);
        PutDrawEnv(&battleBssBlock.m8C50_currentRenderStruct->m0_drawEnv);
        DrawOTag(&battleBssBlock.m8C50_currentRenderStruct->m70_ot[0xFFF]);
    }
    freeBattleStartEffect(e);
    SetDispMask(0);
    waitReadCompletion(0);
    battleLoaderTick(3);
}
