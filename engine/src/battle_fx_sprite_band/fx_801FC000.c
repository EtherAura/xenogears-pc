/* Sprite band: 16 copies of an actor scrolled by its move speed.
 * gcc-2.7.2-cdk, ASPSX 2.56. */
#include "common.h"
#include "psyq.h"
#include "main/task.h"
#include "main/sprite.h"
#include "battle_fx_sprite_band/battle_fx_sprite_band.h"

/* main */
extern MATRIX currentRenderingMatrix;
extern s32 gDepthDivider;          /* OT depth shift */
extern OTag* characterRenderingOT;
extern u8* shapeTransfertTableEnd;
extern u8* shapeTransfertTableCurrentEntry;
TaskHeader* allocateTaskPair(s32 size, TaskHeader* owner, void (*update)(TaskHeader*), void (*draw)(TaskHeader*), void (*del)(TaskHeader*));
void spriteCallback_render2_updateMatrix(SpriteActorCore* actor);
void* memset(void* dst, s32 c, s32 n);
/* this overlay */
s32 getFxSpriteBandBounds(SpriteActorCore* actor, s32* width, s32* height, Fx07Bounds* b);
u8 drawFxSpriteBandCopy(SubSprite* sub, s32 count, s32 ox, s32 oy, SpriteActorCore* actor);
void fxSpriteBand_draw(TaskHeader* task);

s32 getFxSpriteBandBounds(SpriteActorCore* actor, s32* width, s32* height, Fx07Bounds* b) {
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

void fxSpriteBand_update(TaskHeader* task) {
    Fx07Effect* e = (Fx07Effect*)task->m4_data;

    e->m38_scroll.vx -= e->m58_actor->m18_moveSpeed;
}

u8 drawFxSpriteBandCopy(SubSprite* sub, s32 count, s32 ox, s32 oy, SpriteActorCore* actor) {
    s32 visible = 0;
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
    s32 otIndex;

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
            if (v[1].vx < 0) {
                continue;
            }
            if (v[0].vx < 0) {
                v[0].vx = 0;
                v[3].vx = 0;
            }
            otIndex = RotAverage4(&v[0], &v[1], &v[2], &v[3], (s32*)&poly->x0, (s32*)&poly->x1, (s32*)&poly->x3, (s32*)&poly->x2, &p, &flag) >> gDepthDivider;
            otIndex += actor->m30_otOffset;
            if (flag & 0x8000) {
                continue;
            }
            if ((u32)(otIndex - 1) >= 0xFFF) {
                continue;
            }
            tu = sub->m4_texcoordX;
            uw = sub->m6_width - 1;
            tv = sub->m5_texcoordY;
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
            if (((poly->x0 > 0 && poly->x0 < 320) && (poly->y0 > 0 && poly->y0 < 224))
                || ((poly->x1 > 0 && poly->x1 < 320) && (poly->y1 > 0 && poly->y1 < 224))
                || ((poly->x2 > 0 && poly->x2 < 320) && (poly->y2 > 0 && poly->y2 < 224))
                || ((poly->x3 > 0 && poly->x3 < 320) && (poly->y3 > 0 && poly->y3 < 224))) {
                visible++;
            }
        }
        return visible;
    }
    FALLOFF_RETURN(0); /* result unused */
}

void fxSpriteBand_draw(TaskHeader* task) {
    Fx07Effect* e = (Fx07Effect*)task->m4_data;
    SpriteActorCore* actor = e->m58_actor;
    SVECTOR pos; /* unused */
    Fx07Bounds b;
    MATRIX m;
    VECTOR trans;
    s32 w, h;
    s32 count;
    SubSprite* sub;
    s32 x;
    s32 i;

    actor->m0_position.vx = e->m48_position.vx;
    actor->m0_position.vy = e->m48_position.vy;
    actor->m0_position.vz = e->m48_position.vz;
    count = getFxSpriteBandBounds(actor, &w, &h, &b);
    spriteCallback_render2_updateMatrix(actor);
    trans.vx = actor->m0_position.vx >> 16;
    trans.vy = actor->m0_position.vy >> 16;
    trans.vz = actor->m0_position.vz >> 16;
    TransMatrix(&actor->m20_object->mC_matrix, &trans);
    CompMatrix(&currentRenderingMatrix, &actor->m20_object->mC_matrix, &m);
    SetRotMatrix(&m);
    SetTransMatrix(&m);
    x = R3000_MOD(e->m38_scroll.vx >> 16, w) - w;
    sub = actor->m20_object->m30_subSprites;
    for (i = 0; i != 16; i++) {
        drawFxSpriteBandCopy(sub, count, x, 0, actor);
        x += w;
    }
}

void createFxSpriteBand(SpriteActorCore* actor) {
    Fx07Effect* e = (Fx07Effect*)allocateTaskPair(sizeof(Fx07Effect), actor->m6C_owner, fxSpriteBand_update, fxSpriteBand_draw, NULL);

    e->m58_actor = actor;
    actor->mAC_x5 = 1;
    e->m38_scroll.vx = 0;
    e->m38_scroll.vy = 0;
    e->m38_scroll.vz = 0;
    e->m48_position.vx = actor->m0_position.vx;
    e->m48_position.vy = actor->m0_position.vy;
    e->m48_position.vz = actor->m0_position.vz;
}
