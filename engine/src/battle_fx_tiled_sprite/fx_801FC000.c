/* Tiled sprite: pins an actor, accumulates its movement and draws it tiled
 * across the screen.
 * gcc-2.7.2-cdk, ASPSX 2.56. */
#include "common.h"
#include "psyq.h"
#include "main/task.h"
#include "main/sprite.h"
#include "battle_fx_tiled_sprite/battle_fx_tiled_sprite.h"
#include "gte_pc.h"
#include "battle/pc_wide_fx.h"

/* main */
extern MATRIX currentRenderingMatrix;
extern s32 gDepthDivider;          /* OT depth shift */
extern OTag* characterRenderingOT;
extern u8* shapeTransfertTableEnd;
extern u8* shapeTransfertTableCurrentEntry;
TaskHeader* allocateTaskPair(s32 size, TaskHeader* owner, void (*update)(TaskHeader*), void (*draw)(TaskHeader*), void (*del)(TaskHeader*));
void spriteCallback_render2_updateMatrix(SpriteActorCore* actor);
void setupSpriteActorTransform(SpriteActorCore* actor);
void* memset(void* dst, s32 c, s32 n);
/* this overlay */
s32 getFxTiledSpriteBounds(SpriteActorCore* actor, s32* width, s32* height, Fx06Bounds* b);
void drawFxTiledSpriteCopy(SubSprite* sub, s32 count, s32 ox, s32 oy, s32 otIndex, SpriteActorCore* actor);
void fxTiledSprite_draw(TaskHeader* task);

s32 getFxTiledSpriteBounds(SpriteActorCore* actor, s32* width, s32* height, Fx06Bounds* b) {
    SubSprite* sub;
    s32 count;
    s32 i;
    s32 x, y;
    s32 ty;
    s32 x1, y1;
    s32 w, h;

    b->m0_minY = 0x400;
    b->m4_maxY = -0x400;
    b->m2_minX = 0x400;
    b->m6_maxX = -0x400;
    count = actor->m40_draw.bits.x2_subSpriteCount;
    sub = actor->m20_object->m30_subSprites;
    for (i = 0; i != count; i++, sub++) {
        w = sub->m6_width;
        h = sub->m7_height;
        ty = sub->m2_offsetY;
        y = ty;
        x = sub->m0_offsetX;
        y1 = y + h;
        x1 = x + w;
        if (x < b->m2_minX) {
            b->m2_minX = x;
        }
        if (y < b->m0_minY) {
            b->m0_minY = y;
        }
        if (b->m6_maxX < x1) {
            b->m6_maxX = x1;
        }
        if (b->m4_maxY < y1) {
            b->m4_maxY = y1;
        }
    }
    *width = b->m6_maxX - b->m2_minX;
    *height = b->m4_maxY - b->m0_minY;
    return count;
}

void fxTiledSprite_update(TaskHeader* task) {
    Fx06Effect* e = (Fx06Effect*)task->m4_data;
    SpriteActorCore* actor = e->m48_actor;

    e->m38_scroll.vx += actor->mC_step.vx;
    e->m38_scroll.vy += actor->mC_step.vy;
    e->m38_scroll.vz += actor->mC_step.vz;
    actor->m0_position.vx -= actor->mC_step.vx;
    actor->m0_position.vy -= actor->mC_step.vy;
    actor->m0_position.vz -= actor->mC_step.vz;
}

void drawFxTiledSpriteCopy(SubSprite* sub, s32 count, s32 ox, s32 oy, s32 otIndex, SpriteActorCore* actor) {
#if VERSION_JP
    SVECTOR v[4] = { 0 };
#else
    SVECTOR v[4];
#endif
    s32 p, flag;
    POLY_FT4* poly;
    s32 i;
    s16 tu, tv, uw, vh;
    s16 x, y;
    s32 w, h;

#if !VERSION_JP
    memset(v, 0, sizeof(v));
#endif
    if (shapeTransfertTableCurrentEntry + count * sizeof(POLY_FT4) < shapeTransfertTableEnd) {
        for (i = 0; i != count; i++, sub++) {
            poly = (POLY_FT4*)shapeTransfertTableCurrentEntry;
            shapeTransfertTableCurrentEntry += sizeof(POLY_FT4);
            setlen(poly, 9);
            *(u32*)&poly->r0 = sub->m10_colorAndCode;
            poly->tpage = sub->mA_tpage;
            poly->clut = sub->mC_clut;
            w = sub->m6_width;
            h = sub->m7_height;
            x = sub->m0_offsetX;
            y = sub->m2_offsetY;
            if (!sub->m14_attrs.bits.x4_tileFlipX) {
                v[0].vx = x;
                v[1].vx = x + w;
                v[2].vx = x + w;
                v[3].vx = x;
            } else {
                v[0].vx = x + w;
                v[1].vx = x;
                v[2].vx = x;
                v[3].vx = x + w;
            }
            if (!sub->m14_attrs.bits.x5_tileFlipY) {
                v[0].vy = y;
                v[1].vy = y;
                v[2].vy = y + h;
                v[3].vy = y + h;
            } else {
                v[0].vy = y + h;
                v[1].vy = y + h;
                v[2].vy = y;
                v[3].vy = y;
            }
            v[0].vx += ox;
            v[1].vx += ox;
            v[2].vx += ox;
            v[3].vx += ox;
            v[0].vy += oy;
            v[1].vy += oy;
            v[2].vy += oy;
            v[3].vy += oy;
            v[0].vx <<= actor->m40_draw.bits.x8;
            v[1].vx <<= actor->m40_draw.bits.x8;
            v[2].vx <<= actor->m40_draw.bits.x8;
            v[3].vx <<= actor->m40_draw.bits.x8;
            v[0].vy <<= actor->m40_draw.bits.x8;
            v[1].vy <<= actor->m40_draw.bits.x8;
            v[2].vy <<= actor->m40_draw.bits.x8;
            v[3].vy <<= actor->m40_draw.bits.x8;
            RotAverage4(&v[0], &v[1], &v[2], &v[3], (s32*)&poly->x0, (s32*)&poly->x1, (s32*)&poly->x3, (s32*)&poly->x2, &p, &flag);
            tu = sub->m4_texcoordX;
            tv = sub->m5_texcoordY;
            uw = sub->m6_width - 1;
            vh = sub->m7_height - 1;
            poly->u0 = tu;
            poly->v0 = tv;
            poly->u1 = tu + uw;
            poly->v1 = tv;
            poly->u2 = tu;
            poly->v2 = tv + vh;
            poly->u3 = tu + uw;
            poly->v3 = tv + vh;
            addPrim(&characterRenderingOT[otIndex], poly);
        }
    }
}

/*
 * The wide picture (include/pc_wide.h): the copies past the 4:3 edges, as far
 * as the cull margin reaches. The PS1's loops stop at the first copy off its
 * screen; these continue from there. The extra copies come from a PC pool
 * (the shape transfer table they would use is shared by everything this
 * frame), are drawn only into the wide picture, and the GTE is driven
 * directly (battle/pc_wide_fx.h: no safe points) and put back as the PS1
 * leaves it. dir 0: rightwards from minX, 1: leftwards from maxX.
 */
/* drawFxTiledSpriteCopy for a copy only the wide picture has. */
static void wideFxTiledSpriteCopy(SubSprite* sub, s32 count, s32 ox, s32 oy, s32 otIndex, SpriteActorCore* actor) {
    SVECTOR v[4];
    s32 flag, i, k, x, y, w, h;
    POLY_FT4* poly;

    memset(v, 0, sizeof(v));
    if (shapeTransfertTableCurrentEntry + count * sizeof(POLY_FT4) >= shapeTransfertTableEnd) {
        return;
    }
    for (i = 0; i != count; i++, sub++) {
        poly = (POLY_FT4*)shapeTransfertTableCurrentEntry;
        shapeTransfertTableCurrentEntry += sizeof(POLY_FT4);
        setlen(poly, 9);
        *(u32*)&poly->r0 = sub->m10_colorAndCode;
        poly->tpage = sub->mA_tpage;
        poly->clut = sub->mC_clut;
        w = sub->m6_width;
        h = sub->m7_height;
        x = sub->m0_offsetX;
        y = sub->m2_offsetY;
        v[0].vx = v[3].vx = sub->m14_attrs.bits.x4_tileFlipX ? x + w : x;
        v[1].vx = v[2].vx = sub->m14_attrs.bits.x4_tileFlipX ? x : x + w;
        v[0].vy = v[1].vy = sub->m14_attrs.bits.x5_tileFlipY ? y + h : y;
        v[2].vy = v[3].vy = sub->m14_attrs.bits.x5_tileFlipY ? y : y + h;
        for (k = 0; k < 4; k++) {
            v[k].vx = (s16)(v[k].vx + ox) << actor->m40_draw.bits.x8;
            v[k].vy = (s16)(v[k].vy + oy) << actor->m40_draw.bits.x8;
        }
        pcWideRotAverage4(&v[0], &v[1], &v[2], &v[3], &poly->x0, &poly->x1, &poly->x3, &poly->x2, &flag);
        poly->u0 = poly->u2 = sub->m4_texcoordX;
        poly->u1 = poly->u3 = sub->m4_texcoordX + sub->m6_width - 1;
        poly->v0 = poly->v1 = sub->m5_texcoordY;
        poly->v2 = poly->v3 = sub->m5_texcoordY + sub->m7_height - 1;
        addPrim(&characterRenderingOT[otIndex], poly);
    }
}

static void fxTiledSpriteWide(SubSprite* sub, s32 count, s32 w, Fx06Bounds* b, Fx06Effect* e, s32 otIndex,
                              SpriteActorCore* actor) {
    GteState gte;
    SVECTOR pos;
    Vec2s16 sxy;
    s32 p, x, y, dir, i, size, n;
    s32 past;
    u8* entry = shapeTransfertTableCurrentEntry;
    u8* end = shapeTransfertTableEnd;
    u8* pool;

    gteSave(&gte);
    size = count * sizeof(POLY_FT4);
    for (dir = 0; dir < 2; dir++) {
        pos.vz = 0;
        pos.vy = b->m0_minY;
        y = e->m38_scroll.vy >> 16;
        x = e->m38_scroll.vx >> 16;
        past = 0;
        for (n = 0; n < 64; n++) {
            pos.vx = (dir == 0 ? b->m2_minX : b->m6_maxX) + x;
            pcWideRotTransPers(&pos, &sxy, &p);
            if (!past) {
                past = dir == 0 ? sxy.vx >= 0x141 : sxy.vx < 0;
            }
            if (dir == 0 ? sxy.vx >= PC_WIDE_X1(0x141) : sxy.vx < PC_WIDE_X0(0)) {
                break;
            }
            if (past) {
                pool = pcWideAlloc(size + 16);
                if (pool == NULL) {
                    break;
                }
                shapeTransfertTableCurrentEntry = pool;
                shapeTransfertTableEnd = pool + size + 16;
                wideFxTiledSpriteCopy(sub, count, x, y, otIndex, actor);
                for (i = 0; i < count; i++) {
                    pcWideOnly(pool + i * sizeof(POLY_FT4));
                }
            }
            x += dir == 0 ? w : -w;
        }
    }
    shapeTransfertTableCurrentEntry = entry;
    shapeTransfertTableEnd = end;
    gteLoad(&gte);
}

void fxTiledSprite_draw(TaskHeader* task) {
    Fx06Effect* e = (Fx06Effect*)task->m4_data;
    SpriteActorCore* actor = e->m48_actor;
    SpriteActorCore* parent;
    SVECTOR pos;
    VECTOR trans;
    Vec2s16 sxy;
    Fx06Bounds b;
    s32 p, flag;
    s32 w, h;
    s32 otIndex;
    s32 count;
    SubSprite* sub;
    s32 x, y;

    pos.vx = actor->m0_position.vx >> 16;
    pos.vy = actor->m0_position.vy >> 16;
    pos.vz = actor->m0_position.vz >> 16;
    SetRotMatrix(&currentRenderingMatrix);
    SetTransMatrix(&currentRenderingMatrix);
    otIndex = (RotTransPers(&pos, &p, &p, &flag) >> gDepthDivider) + actor->m30_otOffset;
    if (flag & 0x8000) {
        otIndex = 0;
    }
    actor->m2E_otIndex = otIndex;
    if (actor->m3C_x18) {
        spriteCallback_render2_updateMatrix(actor);
        trans.vx = actor->m0_position.vx >> 16;
        trans.vy = actor->m0_position.vy >> 16;
        trans.vz = actor->m0_position.vz >> 16;
        TransMatrix(&actor->m20_object->mC_matrix, &trans);
        SetRotMatrix(&actor->m20_object->mC_matrix);
        SetTransMatrix(&actor->m20_object->mC_matrix);
        if (actor->m3C_x19) {
            otIndex = 0xFFF;
        } else {
            otIndex = actor->m30_otOffset;
        }
        if ((u32)(otIndex - 1) >= 0xFFF) {
            return;
        }
    } else {
        if (actor->m3C_x1D) {
            /* reads through an uninitialised pointer in retail */
            parent = parent->m70_parent;
            otIndex = parent->m2E_otIndex;
        }
        if ((u32)(otIndex - 1) >= 0xFFF) {
            return;
        }
        setupSpriteActorTransform(actor);
    }
    count = getFxTiledSpriteBounds(actor, &w, &h, &b);
    sub = actor->m20_object->m30_subSprites;
    pos.vz = 0;
    pos.vy = b.m0_minY;
    y = e->m38_scroll.vy >> 16;
    x = e->m38_scroll.vx >> 16;
    while (pos.vx = b.m2_minX + x, RotTransPers(&pos, (s32*)&sxy, &p, &p), sxy.vx < 0x141) {
        drawFxTiledSpriteCopy(sub, count, x, y, otIndex, actor);
        x += w;
    }
    pos.vy = b.m0_minY;
    y = e->m38_scroll.vy >> 16;
    x = e->m38_scroll.vx >> 16;
    while (pos.vx = b.m6_maxX + x, RotTransPers(&pos, (s32*)&sxy, &p, &p), sxy.vx >= 0) {
        drawFxTiledSpriteCopy(sub, count, x, y, otIndex, actor);
        x -= w;
    }
    if (pcWideCull != 0 && w > 0) {
        fxTiledSpriteWide(sub, count, w, &b, e, otIndex, actor);
    }
}

void createFxTiledSprite(SpriteActorCore* actor) {
    Fx06Effect* e = (Fx06Effect*)allocateTaskPair(sizeof(Fx06Effect), actor->m6C_owner, fxTiledSprite_update, fxTiledSprite_draw, NULL);

    e->m48_actor = actor;
    actor->mAC_x5 = 1;
    e->m38_scroll.vx = 0;
    e->m38_scroll.vy = 0;
    e->m38_scroll.vz = 0;
}
