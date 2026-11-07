/* Battle loader transitions: the tile (mode 1) and wave (modes 2 and 4)
 * effects (twins in battle and battle_fx_screen_ripple). 0x801E7F4C-0x801E91E8;
 * gcc-2.7.2-cdk, ASPSX 2.56. */
#include "common.h"
#include "psyq.h"
#include "main/memory.h"
#include "main/task.h"
#include "main/filesystem.h"
#include "main/sprite.h"
#include "stack.h"
/* field_mecha/mecha.h (via battle/battle.h) declares the gear overlay's own
 * mainBattleSpriteCallback_phase3; this overlay has a different function at that address. */
#define mainBattleSpriteCallback_phase3 fieldMecha_func_801E6E48
#include "battle/battle.h"
#undef mainBattleSpriteCallback_phase3
#include "battle_loader/battle_loader.h"
#ifdef PLATFORM_PC
#include "gte_pc.h"
#include "battle/pc_wide_fx.h"
#endif

/* main */
extern u8 battleScriptLoaderTaskCreationInProgress;                   /* task allocation heap flag (Noah: battleScriptLoaderTaskCreationInProgress) */
TaskHeader* allocateUpdateTask(TaskHeader* owner, s32 size);
void freeUpdateTask(TaskHeader* task);
void registerSpriteCallback2Sub0(TaskHeader* task);
void allocateSavePointMeshDataSub0_callback(TaskHeader* task);
void clearShapeTransferEntry(void* data);
s32 addClamp255(s32 value, s32 delta);
void resetSpriteCallbacks(void);
void checkSoftReboot(void);
void startBattleLoadingDuringEffect2(void);
s32 waitReadCompletion(s32 mode);
void createRotationMatrix(SVECTOR* rot, MATRIX* m);
s32 getAngleCos(s32 angle);           /* cos */
s32 getAngleSin(s32 angle);           /* sin */
s32 uploadTextureToVram(s32* data, s16 mode, s16 x, s16 y, s16 mode2, s16 clutX, s16 clutY);
extern OTag* characterRenderingOT;
extern SpriteVramData createSavePointMeshData_mode5;
void initFieldEntitySub4Sub5Sub0(SpriteVramData* vram, void* bundle, Vec2s16 vramLocation, Vec2s16 clut, s32 unused);
void loadSequence(void* data);
u32 doPointerRelocation(u32* data);
s32 getSpriteNumVerticalStrides(s32* p);
#ifndef PLATFORM_PC
void uploadNpcSpriteSheet(s32* data, s32 x, s16 y);
#endif
void spriteActorSetPlayingAnimation(SpriteActorCore* actor, s32 animation);
void spriteActorSetDirection(SpriteActorCore* actor, s32 direction);
void setSpriteActorAngle(SpriteActorCore* actor, s16 angle);
void setFixedXZ(FP_VEC3* v, s32 x, s32 z);

/* battle */
extern s32 mainBattleSpriteCallback_phase5Var;
void createMechaLoadingTask(s32 entity);
void setBattleMechaVar2(void);
void battleLoaderTick(u8 step);            /* battle loader: Noah battleLoaderTick */
extern void* battleSpriteLoadTempBuffer;                /* battle sprite data (copy of config file 3's sprite data) */
void mechaInitEnvironmentMechaMesh(s32 entryId, u16 flags, void* pData2, void* pData1, s16 tpageX, s16 tpageY, s16 clutX, s16 clutY, SVECTOR* pPosition);
void initMechaForBattle(u32 entity);
extern s16 battleEnvTextureRectX;                  /* battle env texture rect x (Noah: battleEnvTextureRect*) */
extern s16 battleEnvTextureRectY;                  /* ... y */
extern s16 battleEnvTextureRectW;                  /* ... w */
extern s16 battleEnvTextureRectH;                  /* ... h */
void sampleBattleTerrainHeight(SpriteActorCore* core);
extern u8 currentBattleLoaderTransitionEffect;

/* this overlay */
extern s32* battleLoaderTexture;
extern OTag* battleTileEffectOT;                /* tile transition OT */
extern OTag* battleWaveEffectOT;                /* wave transition OT */
extern u8 isBattleWaveEffectMode3;
extern SVECTOR battleWaveTriVertsTopLeft[3];           /* wave triangle, first half */
extern SVECTOR battleWaveTriVertsBottomRight[3];           /* wave triangle, second half */
extern s32 battlePartyFileMapping[14][2];
extern u8 isBattleTileEffectMode4;
extern SVECTOR battleTileShardVertsTopLeft[3];           /* tile triangle, first half */
extern SVECTOR battleTileShardVertsBottomRight[3];           /* tile triangle, second half */
extern s16 battleSpriteX;
extern u8 battleSpriteWidthPerCharacter[12];

void loadBattleSpriteVram(sBattleSpriteConfigs* config);
void createEnemiesBattleSprites(void* config);
void createBattleSpriteActor(u32 entity, s32 visualBufferIndex, s32 animationId);
void setupPlayerSpriteLoadingCommands(LoadingBatchCommand* commands);
void startPlayableCharactersJumpToPosition(void);
void createBattlePlayerSpriteActors(void);
void mainBattleSpriteCallback_phase6(TaskHeader* task);
void mainBattleSpriteCallback_phase5(TaskHeader* task);
void mainBattleSpriteCallback_phase4(TaskHeader* task);
void uploadBattleLoaderTexture(void);
void mainBattleSpriteCallback_phase3(TaskHeader* task);
void mainBattleSpriteCallback_phase2(TaskHeader* task);
void mainBattleSpriteCallback_phase1(TaskHeader* task);
void createBattleSpriteLoadingTaskSub(void* config);
void renderBattleTileEffect(TaskHeader* task);
sBattleTileEffect* initBattleTileEffect(sBattleTileEffect* e);
void battleLoaderUpdateMode1(void);
sBattleTileEffect* createBattleTileEffect(void);
void freeBattleTileEffect(void* p);
void updateBattleTileEffect(TaskHeader* task);
sBattleWaveEffect* initBattleWaveEffect(sBattleWaveEffect* e);

#ifdef PLATFORM_PC
/*
 * The wide picture (include/pc_wide.h): the tile (modes 1 and 4) and wave
 * (modes 2 and 3) transitions cover the margins too. The capture keeps the
 * wide picture's margins (pcWideCaptureMask: the CPU sets each pixel's mask
 * bit, so every texel blends), the 4:3 pieces sample it as before
 * (PC_WIDE_HANDLED), and extra columns of the same pieces cut from the
 * margins do what their neighbours do (the wave's phase follows each
 * vertex's distance from the centre, so it rolls on outwards). The extras
 * are the PC's own (no game heap), drawn only into the wide picture, and the
 * GTE is driven directly (battle/pc_wide_fx.h) and put back as the PS1 leaves
 * it. A capture without margins (the screen before was 4:3) leaves the frames
 * 4:3.
 */
#define WIDE_TILE_COLS 12 /* per side, 32 pixels each: the widest margin */
#define WIDE_WAVE_COLS 24 /* per side, 16 pixels each */

static struct {
    s32 margin; /* the capture's */
    s32 tileCols, waveCols;
    sBattleStartEffectSub tiles[2][7][2 * WIDE_TILE_COLS];
    sBattleWaveTriangle wave[2][14][2 * WIDE_WAVE_COLS];
} wideLoader;

/* After the transition's LoadImage of the capture (modes 1 to 4). */
void pcWideLoaderCapture(void) {
    RECT r = {0, 0, 0x140, 0xE0};

    wideLoader.margin = pcWideCaptureMask(&r, 0x2C0, 0x100);
    wideLoader.tileCols = wideLoader.waveCols = 0;
}

/* The x of margin column c of width w: left of the screen, then right. */
static s32 wideLoaderX(s32 cols, s32 c, s32 w) {
    return c < cols ? (c - cols) * w : 0x140 + (c - cols) * w;
}

static void wideTileInit(void) {
    s32 i, j, c, l, k, cols;

    cols = wideLoader.margin > 0 ? (wideLoader.margin + 31) / 32 : 0;
    wideLoader.tileCols = cols < WIDE_TILE_COLS ? cols : WIDE_TILE_COLS;
    for (i = 0; i != 2; i++) {
        for (j = 0; j != 7; j++) {
            for (c = 0; c < 2 * wideLoader.tileCols; c++) {
                sBattleStartEffectSub* p = &wideLoader.tiles[i][j][c];
                s32 v = j << 5;

                k = wideLoaderX(wideLoader.tileCols, c, 32) / 32;
                p->m0_rotation.vx = p->m0_rotation.vy = p->m0_rotation.vz = 0;
                p->m10_position.vx = (k * 0x10 - (i == 0 ? 0x4B : 0x45)) * 0x20;
                p->m10_position.vy = j * 0x200 - (i == 0 ? 0x660 : 0x5A0);
                p->m10_position.vz = 0x2000;
                /* Texels from the capture's left edge, moved to the column in
                 * the wide pass (pcWidePrimTex). */
                for (l = 0; l != 2; l++) {
                    POLY_FT3* poly = &p->m24_poly[l];

                    SetPolyFT3(poly);
                    SetShadeTex(poly, 0);
                    poly->r0 = poly->g0 = poly->b0 = 0x80;
                    poly->code |= 2;
                    poly->tpage = GetTPage(2, 1, 0x2C0, 0x100);
                    poly->u0 = i == 0 ? 0 : 0x20;
                    poly->v0 = v;
                    poly->u1 = 0x20;
                    poly->v1 = i == 0 ? v : v + 0x20;
                    poly->u2 = 0;
                    poly->v2 = v + 0x20;
                }
            }
        }
    }
}

static void wideTileUpdate(void) {
    s32 i, j, c;

    for (i = 0; i != 2; i++) {
        for (j = 0; j != 7; j++) {
            for (c = 0; c < 2 * wideLoader.tileCols; c++) {
                sBattleStartEffectSub* p = &wideLoader.tiles[i][j][c];
                POLY_FT3* poly = &p->m24_poly[battleBssBlock.m8C84_oddOrEven];

                poly->r0 = addClamp255(poly->r0, -3);
                poly->g0 = addClamp255(poly->g0, -3);
                poly->b0 = addClamp255(poly->b0, -3);
                if (!isBattleTileEffectMode4) {
                    p->m10_position.vz -= 0x40;
                }
            }
        }
    }
}

/* After renderBattleTileEffect's loop, its geometry offset and screen set. */
static void wideTileRender(sBattleTileEffect* e) {
    GteState gte;
    s32 i, j, k, c;

    if (wideLoader.tileCols == 0 || pcWideView == 0) {
        return;
    }
    for (i = 0; i != 2; i++) {
        for (j = 0; j != 7; j++) {
            for (k = 0; k != 10; k++) {
                if (e->m3C_tiles[i][j][k].m10_position.vz > 0x3F) {
                    pcWidePrim(&e->m3C_tiles[i][j][k].m24_poly[battleBssBlock.m8C84_oddOrEven], 0, PC_WIDE_HANDLED);
                }
            }
        }
    }
    gteSave(&gte);
    for (i = 0; i != 2; i++) {
        for (j = 0; j != 7; j++) {
            for (c = 0; c < 2 * wideLoader.tileCols; c++) {
                sBattleStartEffectSub* s = &wideLoader.tiles[i][j][c];
                POLY_FT3* poly = &s->m24_poly[battleBssBlock.m8C84_oddOrEven];

                if (s->m10_position.vz > 0x3F) {
                    MATRIX m;
                    SVECTOR* v = i == 0 ? battleTileShardVertsTopLeft : battleTileShardVertsBottomRight;
                    s32 flag, depth;

                    createRotationMatrix(&s->m0_rotation, &m);
                    pcWideSetMatrix(&m, &s->m10_position);
                    depth = pcWideRotTransPers3(&v[0], &v[1], &v[2], &poly->x0, &poly->x1, &poly->x2, &flag) >> 6;
                    AddPrim(&battleTileEffectOT[depth], poly);
                    pcWidePrimTex(poly, 0, PC_WIDE_ONLY, wideLoaderX(wideLoader.tileCols, c, 32));
                }
            }
        }
    }
    gteLoad(&gte);
}

static void wideWaveInit(void) {
    s32 i, j, c, l, k, cols;

    cols = wideLoader.margin > 0 ? (wideLoader.margin + 15) / 16 : 0;
    wideLoader.waveCols = cols < WIDE_WAVE_COLS ? cols : WIDE_WAVE_COLS;
    for (i = 0; i != 2; i++) {
        for (j = 0; j != 14; j++) {
            s32 y = j << 4;

            for (c = 0; c < 2 * wideLoader.waveCols; c++) {
                sBattleWaveTriangle* t = &wideLoader.wave[i][j][c];
                SVECTOR* src = i == 0 ? battleWaveTriVertsTopLeft : battleWaveTriVertsBottomRight;

                k = wideLoaderX(wideLoader.waveCols, c, 16) / 16;
                for (l = 0; l != 3; l++) {
                    SVECTOR* v = &t->m54_vertices[l];
                    s32 d;

                    v->vx = src[l].vx + (k * 16 - (i == 0 ? 155 : 149)) * 16;
                    v->vy = src[l].vy + (y - (i == 0 ? 107 : 101)) * 16;
                    v->vz = src[l].vz;
                    d = pcWideSqrt(v->vx * v->vx + v->vy * v->vy);
                    t->m6C_distance[l] = isBattleWaveEffectMode3 ? d * 2 : d * 3 / 5;
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
}

/* Inside renderBattleWaveEffect, after its loop: its matrices still set. */
static void wideWaveRender(sBattleWaveEffect* e) {
    GteState gte;
    s32 i, j, k, c, l;

    if (wideLoader.waveCols == 0 || pcWideView == 0) {
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
            for (c = 0; c < 2 * wideLoader.waveCols; c++) {
                sBattleWaveTriangle* t = &wideLoader.wave[i][j][c];
                SVECTOR* v = t->m54_vertices;
                POLY_GT3* poly = &t->m4_poly[battleBssBlock.m8C84_oddOrEven];
                s32 flag, z;

                for (l = 0; l != 3; l++) {
                    s32 q, r;

                    if (isBattleWaveEffectMode3) {
                        q = getAngleCos(e->m3C_phase + t->m6C_distance[l]);
                    } else {
                        q = getAngleSin(t->m6C_distance[l]);
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
                    AddPrim(&battleWaveEffectOT[z >> 6], poly);
                    pcWidePrimTex(poly, 0, PC_WIDE_ONLY, wideLoaderX(wideLoader.waveCols, c, 16));
                }
            }
        }
    }
    gteLoad(&gte);
}
#endif

/* Tile transition update (Noah: MissingCode "tile animation" in battleLoaderUpdateMode1) */
void updateBattleTileEffect(TaskHeader* task) {
    sBattleTileEffect* e = (sBattleTileEffect*)task->m4_data;
    s32 i, j, k;
    SVECTOR unused; /* retail's frame has an unused 8-byte slot */

    e->m38_frameCounter++;
    for (i = 0; i != 2; i++) {
        for (j = 0; j != 7; j++) {
            for (k = 0; k != 10; k++) {
                sBattleStartEffectSub* p = &e->m3C_tiles[i][j][k];
                POLY_FT3* poly = &p->m24_poly[battleBssBlock.m8C84_oddOrEven];
                poly->r0 = addClamp255(poly->r0, -3);
                poly->g0 = addClamp255(poly->g0, -3);
                poly->b0 = addClamp255(poly->b0, -3);
                if (isBattleTileEffectMode4 == 0) {
                    p->m10_position.vz -= 0x40;
                }
            }
        }
    }
#ifdef PLATFORM_PC
    wideTileUpdate();
#endif
}

void drawBattleTileEffectTask(TaskHeader* task);
void drawBattleTileEffectTask(TaskHeader* task) {
    battleTileEffectOT = characterRenderingOT;
    renderBattleTileEffect(task);
}

/* Tile transition render (Noah: MissingCode "tile rendering" in battleLoaderUpdateMode1) */
void renderBattleTileEffect(TaskHeader* task) {
    sBattleTileEffect* e = (sBattleTileEffect*)task->m4_data;
    s32 ofx, ofy;
    s32 h;
    s32 i, j, k;

    ReadGeomOffset(&ofx, &ofy);
    h = ReadGeomScreen();
    SetGeomOffset(0xA0, 0x70);
    SetGeomScreen(0x200);
    for (i = 0; i != 2; i++) {
        for (j = 0; j != 7; j++) {
            for (k = 0; k != 10; k++) {
                sBattleStartEffectSub* s = &e->m3C_tiles[i][j][k];
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
                        v = battleTileShardVertsTopLeft;
                    } else {
                        v = battleTileShardVertsBottomRight;
                    }
                    {
                        s32 depth = RotTransPers3(&v[0], &v[1], &v[2], (s32*)&poly->x0, (s32*)&poly->x1, (s32*)&poly->x2, &p, &flag) >> 6;
                        AddPrim(&battleTileEffectOT[depth], poly);
                    }
                }
            }
        }
    }
#ifdef PLATFORM_PC
    wideTileRender(e);
#endif
    SetGeomOffset(ofx, ofy);
    SetGeomScreen(h);
}

void freeBattleTileEffect(void* p) {
    DrawSync(0);
    freeHeap(p);
}

void deleteBattleTileEffectTask(TaskHeader* task) {
    registerSpriteCallback2Sub0(task + 1);
    allocateSavePointMeshDataSub0_callback(task);
    clearShapeTransferEntry(task);
}

sBattleTileEffect* createBattleTileEffect(void) {
    sBattleTileEffect* e = allocateHeap(sizeof(sBattleTileEffect), 1);
    e->m0_updateTask.m4_data = e;
    e->m1C_drawTask.m4_data = e;
#ifdef PLATFORM_PC
    wideTileInit();
#endif
    return initBattleTileEffect(e);
}

#if VERSION_JP
sBattleTileEffect* initBattleTileEffect(sBattleTileEffect* e) {
    s32 i, j, k, l;
    s32 textureRow;
    s32 u;
    VECTOR unused0;
    SVECTOR unused1;
    MATRIX unused2;

    e->m38_frameCounter = 0;
    SquareRoot0(0x9500);
    for (i = 0; i != 2; i++) {
        for (j = 0; j != 7; j++) {
            textureRow = j;
            for (k = 0; k != 10; k++) {
                sBattleStartEffectSub* p = &e->m3C_tiles[i][j][k];

                p->m0_rotation.vx = 0;
                p->m0_rotation.vy = 0;
                p->m0_rotation.vz = 0;
                if (i == 0) {
                    p->m10_position.vx = (k * 0x10 - 0x4B) * 0x20;
                    p->m10_position.vy = (j * 0x10 - 0x33) * 0x20;
                    p->m10_position.vz = 0x2000;
                } else {
                    p->m10_position.vx = (k * 0x10 - 0x45) * 0x20;
                    p->m10_position.vy = (j * 0x10 - 0x2D) * 0x20;
                    p->m10_position.vz = 0x2000;
                }
                for (l = 0; l != 2; l++) {
                    POLY_FT3* poly = &p->m24_poly[l];
                    u = (k * 0x20) & 0x3F;
                    SetPolyFT3(poly);
                    SetShadeTex(poly, 0);
                    poly->r0 = 0x80;
                    poly->g0 = 0x80;
                    poly->b0 = 0x80;
                    poly->code |= 2;
                    poly->tpage = GetTPage(2, 1, k * 0x20 + 0x2C0, 0x100);
                    if (i == 0) {
                        poly->u0 = u;
                        poly->v0 = textureRow * 0x20;
                        poly->u1 = u + 0x20;
                        poly->v1 = textureRow * 0x20;
                        poly->u2 = u;
                        poly->v2 = textureRow * 0x20 + 0x20;
                    } else {
                        poly->u0 = u + 0x20;
                        poly->v0 = textureRow * 0x20;
                        poly->u1 = u + 0x20;
                        poly->v1 = textureRow * 0x20 + 0x20;
                        poly->u2 = u;
                        poly->v2 = textureRow * 0x20 + 0x20;
                    }
                }
            }
        }
    }
    return e;
}
#else
sBattleTileEffect* initBattleTileEffect(sBattleTileEffect* e) {
    s32 i, j, k, l;
    s32 u, v, u2, v2;
    VECTOR unused0;
    SVECTOR unused1;
    MATRIX unused2;

    e->m38_frameCounter = 0;
    SquareRoot0(0x9500);
    for (i = 0; i != 2; i++) {
        for (j = 0; j != 7; j++) {
            for (k = 0, v = j << 5; k != 10; k++) {
                sBattleStartEffectSub* p = &e->m3C_tiles[i][j][k];

                p->m0_rotation.vx = 0;
                p->m0_rotation.vy = 0;
                p->m0_rotation.vz = 0;
                if (i == 0) {
                    s32 y = j * 0x200;
                    p->m10_position.vx = (k * 0x10 - 0x4B) * 0x20;
                    p->m10_position.vy = y - 0x660;
                    p->m10_position.vz = 0x2000;
                } else {
                    s32 y = j * 0x200;
                    p->m10_position.vx = (k * 0x10 - 0x45) * 0x20;
                    p->m10_position.vy = y - 0x5A0;
                    p->m10_position.vz = 0x2000;
                }
                for (l = 0; l != 2; l++) {
                    POLY_FT3* poly = &p->m24_poly[l];
                    u = (k * 0x20) & 0x3F;
                    u2 = u + 0x20;
                    v2 = v + 0x20;
                    SetPolyFT3(poly);
                    SetShadeTex(poly, 0);
                    poly->r0 = 0x80;
                    poly->g0 = 0x80;
                    poly->b0 = 0x80;
                    poly->code |= 2;
                    poly->tpage = GetTPage(2, 1, k * 0x20 + 0x2C0, 0x100);
                    if (i == 0) {
                        poly->u0 = u;
                        poly->v0 = v;
                        poly->u1 = u2;
                        poly->v1 = v;
                        poly->u2 = u;
                        poly->v2 = v2;
                    } else {
                        poly->u0 = u2;
                        poly->v0 = v;
                        poly->u1 = u2;
                        poly->v1 = v2;
                        poly->u2 = u;
                        poly->v2 = v2;
                    }
                }
            }
        }
    }
    return e;
}
#endif

/* Noah: battleLoaderUpdateMode1 (tile transition while the battle loads) */
void battleLoaderUpdateMode1(void) {
    s32 counter;
    s32 state;
    s32 n;
    u16* buf;
    u16* p;
    RECT r;
    s32 i;
    sBattleTileEffect* e;

    counter = 0x52;
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
    pcWideLoaderCapture();
#endif
    freeHeap(buf);
    battleBssBlock.m8C50_currentRenderStruct = (battleBssBlock.m8C50_currentRenderStruct == &battleBssBlock.mB70_renderStructs[0]) ? &battleBssBlock.mB70_renderStructs[1] : &battleBssBlock.mB70_renderStructs[0];
    battleBssBlock.m8C54_currentOT = battleBssBlock.m8C50_currentRenderStruct->m70_ot;
    ClearOTagR(battleBssBlock.m8C54_currentOT, 0x1000);
    battleBssBlock.m8C84_oddOrEven = 0;
    battleBssBlock.m8C50_currentRenderStruct = &battleBssBlock.mB70_renderStructs[0];
    battleBssBlock.mB70_renderStructs[0].m0_drawEnv.isbg = 1;
    battleBssBlock.mB70_renderStructs[1].m0_drawEnv.isbg = 1;
    battleBssBlock.mB70_renderStructs[0].m0_drawEnv.r0 = 0;
    battleBssBlock.mB70_renderStructs[1].m0_drawEnv.r0 = 0;
    battleBssBlock.mB70_renderStructs[0].m0_drawEnv.g0 = 0;
    battleBssBlock.mB70_renderStructs[1].m0_drawEnv.g0 = 0;
    battleBssBlock.mB70_renderStructs[0].m0_drawEnv.b0 = 0;
    battleBssBlock.mB70_renderStructs[1].m0_drawEnv.b0 = 0;
    e = createBattleTileEffect();
    while (counter != 0 || state != 5) {
        if (counter > 0) {
            counter--;
        }
        battleBssBlock.m8C50_currentRenderStruct = (battleBssBlock.m8C50_currentRenderStruct == &battleBssBlock.mB70_renderStructs[0]) ? &battleBssBlock.mB70_renderStructs[1] : &battleBssBlock.mB70_renderStructs[0];
        battleBssBlock.m8C54_currentOT = battleBssBlock.m8C50_currentRenderStruct->m70_ot;
        ClearOTagR(battleBssBlock.m8C54_currentOT, 0x1000);
        battleBssBlock.m8C84_oddOrEven = 1 - battleBssBlock.m8C84_oddOrEven;
        battleTileEffectOT = battleBssBlock.m8C54_currentOT;
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
        updateBattleTileEffect((TaskHeader*)e);
        renderBattleTileEffect((TaskHeader*)e);
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
    freeBattleTileEffect(e);
    SetDispMask(0);
    waitReadCompletion(0);
    battleLoaderTick(3);
}


void battleLoaderUpdateMode4(void);
void battleLoaderUpdateMode4(void) {
    isBattleTileEffectMode4 = 1;
    battleLoaderUpdateMode1();
}

/* Wave transition update (updateBattleWaveEffect) */
void updateBattleWaveEffect(TaskHeader* task);
void updateBattleWaveEffect(TaskHeader* task) {
    sBattleWaveEffect* e = (sBattleWaveEffect*)task->m4_data;
    s32 i, j, k;
    SVECTOR unused; /* retail's frame has an unused 8-byte slot */

    if (isBattleWaveEffectMode3 != 0) {
        e->m48_speed++;
        e->m3C_phase += 0xA0;
        e->m44_frameCounter++;
        e->m4C_position.vz -= 0x3C;
        if (e->m44_frameCounter > 0x42) {
            e->m38_brightness -= 0x18;
        } else {
            e->m40_amplitude += 0x80;
        }
    } else {
        e->m48_speed += 0xA;
        e->m40_amplitude += 0x600;
        e->m44_frameCounter++;
        e->m4C_position.vz -= e->m48_speed;
        if (e->m44_frameCounter > 0x18) {
            e->m38_brightness -= 0x14;
        }
    }
    for (i = 0; i != 2; i++) {
        for (j = 0; j != 14; j++) {
            for (k = 0; k != 20; k++) {
            }
        }
    }
}

void renderBattleWaveEffect(TaskHeader* task) {
    sBattleWaveEffect* e = (sBattleWaveEffect*)task->m4_data;
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
        TransMatrix(&m, &e->m4C_position);
        SetRotMatrix(&m);
        SetTransMatrix(&m);
        for (i = 0; i != 2; i++) {
            for (j = 0; j != 14; j++) {
                for (k = 0; k != 20; k++) {
                    sBattleWaveTriangle* t = &e->m64_triangles[i][j][k];
                    SVECTOR* v = t->m54_vertices;
                    POLY_GT3* poly = &t->m4_poly[battleBssBlock.m8C84_oddOrEven];
                    for (l = 0; l != 3; l++) {
                        if (isBattleWaveEffectMode3) {
                            amp = e->m40_amplitude;
                            q = getAngleCos(e->m3C_phase + t->m6C_distance[l]);
                        } else {
                            amp = e->m40_amplitude;
                            q = getAngleSin(t->m6C_distance[l]);
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
                        AddPrim(&battleWaveEffectOT[z], poly);
                    }
                }
            }
        }
#ifdef PLATFORM_PC
        wideWaveRender(e);
#endif
    }
    SetGeomOffset(ofx, ofy);
    SetGeomScreen(h);
}

void freeBattleWaveEffect(void* p) {
    DrawSync(0);
    freeHeap(p);
}

void deleteBattleWaveEffectTask(TaskHeader* task) {
    registerSpriteCallback2Sub0(task + 1);
    allocateSavePointMeshDataSub0_callback(task);
    clearShapeTransferEntry(task);
}

sBattleWaveEffect* createBattleStartEffect(void);
sBattleWaveEffect* createBattleStartEffect(void) {
    sBattleWaveEffect* e = allocateHeap(sizeof(sBattleWaveEffect), 1);
    e->m0_updateTask.m4_data = e;
    e->m1C_drawTask.m4_data = e;
#ifdef PLATFORM_PC
    wideWaveInit();
#endif
    return initBattleWaveEffect(e);
}

sBattleWaveEffect* initBattleWaveEffect(sBattleWaveEffect* e) {
    s32 j;
    s32 i;
    s32 l;
    s32 k;
    VECTOR sq;

    if (isBattleWaveEffectMode3 != 0) {
        e->m44_frameCounter = 0;
        e->m40_amplitude = 0x400;
        e->m38_brightness = 0x80;
        e->m48_speed = 0x40;
        e->m3C_phase = 0x700;
    } else {
        e->m38_brightness = 0x80;
        e->m44_frameCounter = 0;
        e->m40_amplitude = 0;
        e->m48_speed = 0x40;
        e->m3C_phase = 0;
    }
    SquareRoot0(0x9500);
    e->m4C_position.vx = 0;
    e->m4C_position.vy = 0;
    e->m4C_position.vz = 0x2000;
    e->m5C_rotation.vx = 0;
    e->m5C_rotation.vy = 0;
    e->m5C_rotation.vz = 0;
    for (i = 0; i != 2; i++) {
        for (j = 0; j != 14; j++) {
            s32 y = j << 4;

            for (k = 0; k != 20; k++) {
                sBattleWaveTriangle* t = &e->m64_triangles[i][j][k];
                SVECTOR* src;
                s32 v2;

                if (i == 0) {
                    src = battleWaveTriVertsTopLeft;
                } else {
                    src = battleWaveTriVertsBottomRight;
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
                    if (isBattleWaveEffectMode3) {
                        t->m6C_distance[l] = SquareRoot0(sq.vx + sq.vy) * 2;
                    } else {
                        t->m6C_distance[l] = SquareRoot0(sq.vx + sq.vy) * 3 / 5;
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

