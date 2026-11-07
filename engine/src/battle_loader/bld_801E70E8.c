/* Battle loader: the battle environment (background layers, horizon,
 * terrain). 0x801E70E8-0x801E7F4C; gcc-2.7.2-psx, ASPSX 2.34. */
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
#include "pc_convert.h"

/* main */
extern u8 battleScriptLoaderTaskCreationInProgress;                   /* task allocation heap flag (Noah: battleScriptLoaderTaskCreationInProgress) */
TaskHeader* allocateUpdateTask(TaskHeader* owner, s32 size);
void freeUpdateTask(TaskHeader* task);
void registerSpriteCallback2Sub0(TaskHeader* task);
void allocateSavePointMeshDataSub0_callback(TaskHeader* task);
void clearShapeTransferEntry(void* data);
s32 uploadTextureToVram(s32* data, s16 mode, s16 x, s16 y, s16 mode2, s16 clutX, s16 clutY);
extern OTag* characterRenderingOT;
extern SpriteVramData createSavePointMeshData_mode5;
void initFieldEntitySub4Sub5Sub0(SpriteVramData* vram, void* bundle, Vec2s16 vramLocation, Vec2s16 clut, s32 unused);
void loadSequence(void* data);
u32 doPointerRelocation(u32* data);
s32 getSpriteNumVerticalStrides(s32* p);
void spriteActorSetPlayingAnimation(SpriteActorCore* actor, s32 animation);
void spriteActorSetDirection(SpriteActorCore* actor, s32 direction);
void setSpriteActorAngle(SpriteActorCore* actor, s16 angle);
void setFixedXZ(FP_VEC3* v, s32 x, s32 z);

/* battle */
extern s32 mainBattleSpriteCallback_phase5Var;
void createMechaLoadingTask(s32 entity);
void setBattleMechaVar2(void);
extern void* battleSpriteLoadTempBuffer;                /* battle sprite data (copy of config file 3's sprite data) */
void mechaInitEnvironmentMechaMesh(s32 entryId, u16 flags, void* pData2, void* pData1, s16 tpageX, s16 tpageY, s16 clutX, s16 clutY, SVECTOR* pPosition);
void initMechaForBattle(u32 entity);
extern s16 battleEnvTextureRectX;                  /* battle env texture rect x (Noah: battleEnvTextureRect*) */
extern s16 battleEnvTextureRectY;                  /* ... y */
extern s16 battleEnvTextureRectW;                  /* ... w */
extern s16 battleEnvTextureRectH;                  /* ... h */
void sampleBattleTerrainHeight(SpriteActorCore* core);
extern u8 currentBattleLoaderTransitionEffect;


/* RR3: battle environment (loadBattleEnvironment) */
extern void* battleEnvBackgroundPolys[2];
extern sBattleImageMoveCommand battleMoveCommands[2];
extern sMechaBone* environmentModelConfigs;
extern sMechaInitVar4* environmentModelBlocks;
extern sMechaInitVar2 battleMechaInitVar2;
extern sLoadedMechas_118 battleEnvColorAnim; /* environment CLUT animation */
extern MATRIX* battle800d2fc0;                 /* Noah: battle800d2fc0 (colour matrix) */
extern s16 battleEnvColliderCount;                     /* collider count */
extern s16* battleEnvColliders;                    /* colliders: {x, z, radius} */
void* createBackgroundPoly(s32 arg0, s32 arg1, s32 arg2, s32 arg3, s32 clutX, s32 clutY, s32 arg6, s32 arg7,
                           VECTOR* vec, u8* colors, s16 arg10, s16 arg11, s16 arg12);
void* setupImageMoveCommand(sBattleImageMoveCommand* cmd, s16 x, s16 y, s16 width, s16 a4, s16 count, s16 a6, s16 a7, u8* speeds);
void mechaInitEnvironmentMechaMesh(s32 entryId, u16 flags, void* pData2, void* pData1, s16 tpageX, s16 tpageY, s16 clutX, s16 clutY, SVECTOR* pPosition);
void initMechaTransforms1_battle(sLoadedMechas* arg0, sMechaInitVar2* arg1, void* arg2, void* arg3);
void initMechaAnimation_battle(sLoadedMechas* arg0, sLoadedMechas* arg1, sMechaInitVar2* arg2, s32 animationId);
s32 updateMechAnimSub1_battle(sMechaBone* bones, s32 scale);
void SetColorMatrix(MATRIX* m);
void SetBackColor(s32 r, s32 g, s32 b);
s32 processBattleEnvTextures(sptr* textures);
sBattleHorizon* createBattleHorizon(s16 tpX, s16 tpY, s16 arg2, s16 arg3, s16 texH, s16 gridStep, s16 arg6, s16 clutX, s16 clutY,
                              s16 arg9, s16 arg10, VECTOR* vec, CVECTOR* colors, s16 arg13, s16 arg14);
void initBattleTerrain(SVECTOR* vertices, sBattleTerrainTriangle* triangles, s32 count);

/* this overlay */
extern s32* battleLoaderTexture;
extern OTag* battleTileEffectOT;
extern s32 battlePartyFileMapping[14][2];
extern u8 isBattleTileEffectMode4;
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
void initBattleTileEffect(void* p);
void battleLoaderUpdateMode1(void);
void initBattleWaveEffect(void* p);

s32 processBattleEnvTextures(sptr* textures) {
    s32 i;
    s32 n;
    s32 minX;
    s32 minY;
    s32 maxX;
    s32 maxY;
    s32 x;
    s32 y;

    textures = (sptr*)pcRelocTable((u32*)textures);
    minX = 0x800;
    minY = 0x800;
    maxX = -0x800;
    maxY = -0x800;
    n = (s32)textures[0];
    for (i = 0; i < n; i++) {
        u16* p = (u16*)textures[i + 1];
        if (*p == 0x1101) {
            p += 2;
            x = *p++;
            y = *p++;
            x += *p++;
            y += *p++;
            if (x < minX) {
                minX = x;
            }
            if (y < minY) {
                minY = y;
            }
            x += p[0];
            y += p[1];
            if (maxX < x) {
                maxX = x;
            }
            if (maxY < y) {
                maxY = y;
            }
        }
    }
    battleEnvTextureRectX = minX;
    battleEnvTextureRectY = minY;
    battleEnvTextureRectW = maxX - minX;
    battleEnvTextureRectH = maxY - minY;
    freeHeap(textures);
    return battleEnvTextureRectW * battleEnvTextureRectH;
}

u8 loadBattleEnvironment(u8** envFile, void* unused, sBattleEnvMechaData* mecha, MATRIX* light, MATRIX* color, u8* backColor) {
    sBattleMechaInitData* file = (sBattleMechaInitData*)*envFile;
    u8* src;
    s32 off;
    sBattleEnvConfig* cfg;
    s32* anim;
    s32* count;
    SVECTOR* vertices;
    sBattleTerrainTriangle* triangles;
    u8* ptrs[4];
    s32 i;
    s32 nBg;
    s32 nMove;
    u8 useBackColor;

    if (mecha == NULL || envFile == NULL || file == NULL) {
        return 0;
    }
    resetMemoryAllocStats(4, 0);
    environmentModelConfigs = NULL;
    battle800c3ea0 = NULL;
    battleTerrainVertices = NULL;
    battleTerrainTriangles = NULL;
    battleTerrainTriangleCount = 0;
    for (i = 0; i < 2; i++) {
        battleEnvBackgroundPolys[i] = NULL;
    }
    for (i = 0; i < 2; i++) {
        battleMoveCommands[i].m14_offsets = NULL;
    }
    battleEnvColorAnim.m1A_isActive = 0;
    if (mecha != NULL) {
        sMechaBone* bones;
        s16* src;
        unflagAllocation(mecha);
        mechaInitEnvironmentMechaMesh(0x1F, 0xC4, mecha, mecha, 0, 0, 0, 0, NULL);
        {
            /* The PS1 reads the table mechaInitEnvironmentMechaMesh relocated. */
            sBattleEnvMechaData* table = (sBattleEnvMechaData*)pcConvertMechaData1(mecha);
            processBattleEnvTextures(table->m4_textures);
            src = table->m14_boneInit;
            freeHeap(table);
        }
        bones = battleMechas[0x1F]->m4_bones;
        environmentModelConfigs = bones;
        environmentModelBlocks = battleMechas[0x1F]->m0_model;
        for (i = 1; i < bones->mA_numBones; i++, src += 4) {
            bones[i].m5C_translation[0] = src[0];
            bones[i].m5C_translation[1] = src[1];
            bones[i].m5C_translation[2] = src[2];
            bones[i].m52_billboardMode = src[3];
        }
    }
    src = (u8*)file;
    off = ((s32*)src)[-1];
    file = allocateHeap(off, 0);
    battleEnvMechaInitData = file;
    if (file != NULL) {
        memcpy(file, src, off);
        unflagAllocation(src - 4);
        freeHeap(src - 4);
        nBg = 0;
        nMove = 0;
        off = file->m50C_terrainVertices;
        anim = (s32*)((u8*)file + off);
        vertices = off ? (SVECTOR*)anim : NULL;
        off = file->m510_terrainTriangles;
        anim = (s32*)((u8*)file + off) + 1;
        count = anim - 1;
        triangles = off ? (sBattleTerrainTriangle*)anim : NULL;
        off = file->m514_animation;
        anim = (s32*)((u8*)file + off);
        anim = (s32*)pcRelocTable((u32*)anim); /* kept: the animation keeps &table[2] */
        src = (u8*)((sptr*)anim)[1];
        cfg = (sBattleEnvConfig*)file->m340_terrainAgility;
        if (mecha != NULL) {
            initMechaTransforms1_battle(battleMechas[0x1F], &battleMechaInitVar2, (void*)((sptr*)anim + 2), NULL);
            initMechaAnimation_battle(battleMechas[0x1F], battleMechas[0x1F], &battleMechaInitVar2, 0);
            updateMechAnimSub1_battle(environmentModelConfigs, battleMechas[0x1F]->m1C_scale);
        }
        for (i = 0; i < 4; i++) {
            off = *(s32*)src;
            anim = (s32*)(src + off);
            ptrs[i] = (u8*)anim;
        }
        battleEnvColliderCount = *(s16*)ptrs[0];
        battleEnvColliders = (s16*)ptrs[0] + 1;
        light->m[0][0] = file->m464_light[0];
        light->m[0][1] = file->m464_light[1];
        light->m[0][2] = file->m464_light[2];
        light->m[1][0] = 0;
        light->m[1][1] = 0;
        light->m[1][2] = 0;
        light->m[2][0] = 0;
        light->m[2][1] = 0;
        light->m[2][2] = 0;
        color->m[0][0] = file->m46C_color[0];
        color->m[0][1] = 0;
        color->m[0][2] = 0;
        color->m[1][0] = file->m46C_color[1];
        color->m[1][1] = 0;
        color->m[1][2] = 0;
        battle800d2fc0 = color;
        color->m[2][0] = file->m46C_color[2];
        color->m[2][1] = 0;
        color->m[2][2] = 0;
        SetColorMatrix(color);
        SetBackColor(file->m474_backR, file->m475_backG, file->m476_backB);
        for (i = 0; i < 6; i++) {
            sBattleEnvLayer* l = &cfg->m20_layers[i];
            switch (l->m18_type) {
            case 0:
                break;
            case 1:
                if (battleEnvBackgroundPolys[nBg] == NULL && nBg < 2) {
                    battleEnvBackgroundPolys[nBg] = createBackgroundPoly(l->m10_vramX, l->m12_vramY, l->m14_width, l->m16_height, l->m1A_clutX, l->m1C_clutY, l->m1E, l->m20, &l->m0_horizonPos, NULL, 0, l->m24_zoomDivisor, l->m26_zoomStartDistance);
                }
                nBg++;
                break;
            case 2:
                if (battleEnvBackgroundPolys[nBg] == NULL && nBg < 2) {
                    battleEnvBackgroundPolys[nBg] = createBackgroundPoly(l->m10_vramX, l->m12_vramY, l->m14_width, l->m16_height, l->m1A_clutX, l->m1C_clutY, l->m1E, l->m20, &l->m0_horizonPos, (u8*)cfg->m118_colors, l->m22_groundBandScale, l->m24_zoomDivisor, l->m26_zoomStartDistance);
                }
                nBg++;
                break;
            case 3:
                if (battle800c3ea0 == NULL) {
                    battle800c3ea0 = createBattleHorizon(l->m10_vramX, l->m12_vramY, cfg->m114_horizonScrollU, cfg->m116_horizonScrollV, l->m14_width, l->m1E, l->m16_height, l->m1A_clutX, l->m1C_clutY, cfg->m110_horizonGeomScreen, cfg->m112_horizonTiltScale, &l->m0_horizonPos, cfg->m118_colors, l->m20, l->m22_groundBandScale);
                }
                break;
            case 5:
                cfg->m1E_useBackColor = 1;
                break;
            case 7:
                if (battleMoveCommands[nMove].m14_offsets == NULL && nMove < 2 && i + 1 < 4) {
                    setupImageMoveCommand(&battleMoveCommands[nMove], l->m10_vramX, l->m12_vramY, l->m14_width, l->m16_height, l->m20, l->m1A_clutX, l->m1C_clutY, ptrs[i + 1]);
                }
                nMove++;
                break;
            }
        }
        useBackColor = cfg->m1E_useBackColor;
        if (useBackColor != 0 && backColor != NULL) {
            backColor[0] = cfg->m118_colors[0].r;
            backColor[1] = cfg->m118_colors[0].g;
            backColor[2] = cfg->m118_colors[0].b;
        }
        if (vertices != NULL && triangles != NULL) {
            initBattleTerrain(vertices, triangles, *count);
        }
        for (i = 0; i < 4; i++) {
            battleEntities.m6028_terrainAgility[i] = battleEnvMechaInitData->m340_terrainAgility[i];
        }
        DrawSync(0);
        freeHeap(mecha);
        *envFile = (u8*)file;
        return useBackColor;
    }
    FALLOFF_RETURN(0); /* the copy's allocation failed: $v0 is its NULL */
}

/* Battle environment background (a 9x9 grid of textured quads plus sky
 * quads; called from loadBattleEnvironment) */
sBattleHorizon* createBattleHorizon(s16 tpX, s16 tpY, s16 arg2, s16 arg3, s16 texH, s16 gridStep, s16 arg6, s16 clutX, s16 clutY,
                                  s16 arg9, s16 arg10, VECTOR* vec, CVECTOR* colors, s16 arg13, s16 arg14) {
    DRAWENV env;
    RECT r;
    sBattleHorizon* p;
    s32 i, j, k;

    resetMemoryAllocStats(4, 0);
    p = allocateHeap(sizeof(sBattleHorizon), 0);
    if (p != NULL) {
        GetDrawEnv(&env);
        p->m0_u = 0;
        p->m2_v = 0;
        p->m4_du = arg2;
        p->m6_dv = arg3;
        p->m8_texSize = texH;
        p->mA_height = arg6;
        p->mC_skyBandScale = arg13;
        p->mE_groundBandScale = arg14;
        p->m10_geomScreen = arg9;
        p->m12_tiltScale = arg10;
        p->m14_horizonX = *(s16*)&vec->vx;
        p->m16_horizonY = *(s16*)&vec->vy;
        p->m18_horizonDistance = *(s16*)&vec->vz;
        for (i = 0; i < 2; i++) {
            p->m1C_colors[i].r = colors[i].r;
            p->m1C_colors[i].g = colors[i].g;
            p->m1C_colors[i].b = colors[i].b;
        }
        k = 0;
        for (i = 0; i < 9; i++) {
            for (j = 0; j < 9; j++) {
                p->m54_grid[k].vx = j * gridStep - gridStep * 4;
                p->m54_grid[k].vy = 0;
                p->m54_grid[k].vz = i * gridStep - gridStep * 4;
                k++;
            }
        }
        for (i = 0; i < 2; i++) {
            SetPolyF4(&p->m16DC_flat[i]);
            p->m16DC_flat[i].r0 = colors[0].r;
            p->m16DC_flat[i].g0 = colors[0].g;
            p->m16DC_flat[i].b0 = colors[0].b;
            p->m16DC_flat[i].x0 = 0;
            p->m16DC_flat[i].y0 = 0;
            p->m16DC_flat[i].x1 = 0x140;
            p->m16DC_flat[i].y1 = 0;
            p->m16DC_flat[i].x2 = 0;
            p->m16DC_flat[i].x3 = 0x140;
        }
        for (i = 0; i < 2; i++) {
            SetPolyG4(&p->m173C_gradient[i]);
            SetSemiTrans(&p->m173C_gradient[i], 1);
            p->m173C_gradient[i].r2 = 0;
            p->m173C_gradient[i].g2 = 0;
            p->m173C_gradient[i].b2 = 0;
            p->m173C_gradient[i].r3 = 0;
            p->m173C_gradient[i].g3 = 0;
            p->m173C_gradient[i].b3 = 0;
            p->m173C_gradient[i].r0 = 0xFF - colors[0].r;
            p->m173C_gradient[i].g0 = 0xFF - colors[0].g;
            p->m173C_gradient[i].b0 = 0xFF - colors[0].b;
            p->m173C_gradient[i].r1 = 0xFF - colors[0].r;
            p->m173C_gradient[i].g1 = 0xFF - colors[0].g;
            p->m173C_gradient[i].b1 = 0xFF - colors[0].b;
            p->m173C_gradient[i].x0 = 0;
            p->m173C_gradient[i].x1 = 0x140;
            p->m173C_gradient[i].x2 = 0;
            p->m173C_gradient[i].x3 = 0x140;
        }
        colors++;
        for (i = 2; i < 4; i++) {
            SetPolyG4(&p->m173C_gradient[i]);
            p->m173C_gradient[i].r0 = colors->r;
            p->m173C_gradient[i].g0 = colors->g;
            p->m173C_gradient[i].b0 = colors->b;
            p->m173C_gradient[i].r1 = colors->r;
            p->m173C_gradient[i].g1 = colors->g;
            p->m173C_gradient[i].b1 = colors->b;
            colors++;
            p->m173C_gradient[i].r2 = colors->r;
            p->m173C_gradient[i].g2 = colors->g;
            p->m173C_gradient[i].b2 = colors->b;
            p->m173C_gradient[i].r3 = colors->r;
            p->m173C_gradient[i].g3 = colors->g;
            p->m173C_gradient[i].b3 = colors->b;
            colors--;
            p->m173C_gradient[i].x0 = 0;
            p->m173C_gradient[i].x1 = 0x140;
            p->m173C_gradient[i].x2 = 0;
            p->m173C_gradient[i].x3 = 0x140;
        }
        colors++;
        for (i = 2; i < 4; i++) {
            SetPolyF4(&p->m16DC_flat[i]);
            p->m16DC_flat[i].r0 = colors->r;
            p->m16DC_flat[i].g0 = colors->g;
            p->m16DC_flat[i].b0 = colors->b;
            p->m16DC_flat[i].x0 = 0;
            p->m16DC_flat[i].x1 = 0x140;
            p->m16DC_flat[i].x2 = 0;
            p->m16DC_flat[i].y2 = 0xF0;
            p->m16DC_flat[i].x3 = 0x140;
            p->m16DC_flat[i].y3 = 0xF0;
        }
        for (i = 0; i < 128; i++) {
            SetPolyFT4(&p->m2DC_quads[i]);
            SetShadeTex(&p->m2DC_quads[i], 1);
            p->m2DC_quads[i].clut = GetClut(clutX, clutY);
            p->m2DC_quads[i].tpage = GetTPage(0, 1, (tpX / 64) * 64, (tpY / 256) * 256);
        }
        for (i = 0; i < 2; i++) {
            r.x = tpX % 64;
            r.y = tpY % 256;
            r.w = texH;
            r.h = texH;
            SetDrawMode(&p->m24_drawModes[i], env.dfe, env.dtd, GetTPage(0, 1, 0, 0), &r);
        }
        for (i = 2; i < 4; i++) {
            SetDrawMode(&p->m3C_drawModes[i - 2], env.dfe, env.dtd, GetTPage(0, 1, 0, 0), &env.tw);
        }
    }
    return p;
}

void initBattleTerrain(SVECTOR* vertices, sBattleTerrainTriangle* triangles, s32 count) {
    s32 i;

    battleTerrainVertices = vertices;
    battleTerrainTriangles = triangles;
    battleTerrainTriangleCount = count;
    battleTerrainSearchGeneration = 1;
    if (triangles != NULL) {
        for (i = 0; i < battleTerrainTriangleCount; i++) {
            battleTerrainTriangles[i].mD_visited = 0;
        }
    }
    if (count == 0) {
        battleTerrainVertices = NULL;
        battleTerrainTriangles = NULL;
    }
}
