/* Sprite wobble: redraws an actor's first sub-sprite as 4-pixel rows
 * displaced by a cosine wave.
 * gcc-2.7.2-cdk, ASPSX 2.56. */
#include "common.h"
#include "psyq.h"
#include "main/task.h"
#include "main/sprite.h"
#include "battle_fx_sprite_wobble/battle_fx_sprite_wobble.h"

/* main */
extern MATRIX currentRenderingMatrix;
extern SVECTOR currentSpriteCharacterSize[4];
extern s32 gDepthDivider;          /* OT depth shift */
extern OTag* characterRenderingOT;
extern u8* shapeTransfertTableEnd;
extern u8* shapeTransfertTableCurrentEntry;
TaskHeader* allocateTaskPair(s32 size, TaskHeader* owner, void (*update)(TaskHeader*), void (*draw)(TaskHeader*), void (*del)(TaskHeader*));
void spriteCallback_render2_updateMatrix(SpriteActorCore* actor);
void setupSpriteActorTransform(SpriteActorCore* actor);
s32 getAngleCos(s32); /* cosine */

void fxSpriteWobble_update(TaskHeader* task) {
    Fx04Effect* e = (Fx04Effect*)task->m4_data;

    e->m4C_phase += e->m50_phaseSpeed;
}

void fxSpriteWobble_draw(TaskHeader* task);

void fxSpriteWobble_draw(TaskHeader* task) {
    Fx04Effect* e = (Fx04Effect*)task->m4_data;
    SpriteActorCore* actor = e->m38_actor;
    SVECTOR pos;
    VECTOR trans;
    s32 sxy;
    s32 flag;
    s32 otIndex;
    SubSprite* sub;
    POLY_FT4* p;
    s32 phase;
    s32 x0, x1;
    s32 y;
    s32 phaseStep, amp, top;
    u8 u, w;
    s16 ax, bx, cx, dx, ay, by;

    pos.vx = actor->m0_position.vx >> 16;
    pos.vy = actor->m0_position.vy >> 16;
    pos.vz = actor->m0_position.vz >> 16;
    SetRotMatrix(&currentRenderingMatrix);
    SetTransMatrix(&currentRenderingMatrix);
    otIndex = (RotTransPers(&pos, &sxy, &sxy, &flag) >> gDepthDivider) + actor->m30_otOffset;
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
            sub = (SubSprite*)((SpriteActorCore*)sub)->m70_parent;
            otIndex = ((SpriteActorCore*)sub)->m2E_otIndex;
        }
        if ((u32)(otIndex - 1) >= 0xFFF) {
            return;
        }
        setupSpriteActorTransform(actor);
    }
    phase = e->m4C_phase;
    x0 = getAngleCos(phase) * e->m3C_amplitude / 4096;
    amp = e->m3C_amplitude << 8;
    sub = actor->m20_object->m30_subSprites;
    phaseStep = e->m44_phaseStep << 8;
    y = sub->m2_offsetY + sub->m7_height - 4;
    top = sub->m2_offsetY;
    u = sub->m4_texcoordX;
    w = sub->m6_width;
    if (top < y) {
        do {
            x1 = getAngleCos(phase) * (amp >> 8) / 4096;
            p = (POLY_FT4*)shapeTransfertTableCurrentEntry;
            if ((u8*)(p + 1) >= shapeTransfertTableEnd) {
                return;
            }
            shapeTransfertTableCurrentEntry = (u8*)(p + 1);
            setlen(p, 9);
            *(u32*)&p->r0 = sub->m10_colorAndCode;
            p->tpage = sub->mA_tpage;
            p->clut = sub->mC_clut;
            ax = sub->m0_offsetX + x1;
            {
                s32 mw = w;

                bx = sub->m0_offsetX + mw + x1;
                cx = sub->m0_offsetX + mw + x0;
            }
            dx = sub->m0_offsetX + x0;
            currentSpriteCharacterSize[0].vx = ax;
            currentSpriteCharacterSize[1].vx = bx;
            currentSpriteCharacterSize[2].vx = cx;
            currentSpriteCharacterSize[3].vx = dx;
            ay = sub->m2_offsetY + sub->m7_height + y;
            currentSpriteCharacterSize[0].vy = ay;
            currentSpriteCharacterSize[1].vy = ay;
            currentSpriteCharacterSize[2].vy = ay + 4;
            currentSpriteCharacterSize[3].vy = ay + 4;
            by = ay + 4;
            currentSpriteCharacterSize[0].vx = ax << actor->m40_draw.bits.x8;
            currentSpriteCharacterSize[1].vx = bx << actor->m40_draw.bits.x8;
            currentSpriteCharacterSize[2].vx = cx << actor->m40_draw.bits.x8;
            currentSpriteCharacterSize[3].vx = dx << actor->m40_draw.bits.x8;
            currentSpriteCharacterSize[0].vy = ay << actor->m40_draw.bits.x8;
            currentSpriteCharacterSize[1].vy = ay << actor->m40_draw.bits.x8;
            currentSpriteCharacterSize[2].vy = by << actor->m40_draw.bits.x8;
            currentSpriteCharacterSize[3].vy = by << actor->m40_draw.bits.x8;
            RotTransPers4(&currentSpriteCharacterSize[0], &currentSpriteCharacterSize[1], &currentSpriteCharacterSize[2], &currentSpriteCharacterSize[3],
                          (s32*)&p->x0, (s32*)&p->x1, (s32*)&p->x3, (s32*)&p->x2, &sxy, &flag);
            p->u0 = u;
            p->v0 = sub->m5_texcoordY + sub->m7_height + y;
            p->u1 = u + w;
            p->v1 = sub->m5_texcoordY + sub->m7_height + y;
            p->u2 = u;
            p->v2 = sub->m5_texcoordY + sub->m7_height + y + 4;
            p->u3 = u + w;
            p->v3 = sub->m5_texcoordY + sub->m7_height + y + 4;
            x0 = x1;
            if (!(flag & 0x8000) && otIndex < 0x1000) {
                AddPrim(&characterRenderingOT[otIndex], p);
            }
            y -= 4;
            phase += phaseStep >> 8;
            phaseStep += e->m48_phaseStepStep;
            amp += e->m40_amplitudeStep;
        } while (top < y);
    }
}

void createFxSpriteWobble(SpriteActorCore* actor, s32 phase, s32 amplitude, s32 amplitudeStep, s32 phaseStep, s32 phaseStepStep, s32 phaseSpeed) {
    Fx04Effect* e = (Fx04Effect*)allocateTaskPair(sizeof(Fx04Effect), actor->m6C_owner, fxSpriteWobble_update, fxSpriteWobble_draw, NULL);

    e->m38_actor = actor;
    e->m3C_amplitude = amplitude;
    e->m40_amplitudeStep = amplitudeStep;
    e->m44_phaseStep = phaseStep;
    e->m48_phaseStepStep = phaseStepStep;
    e->m4C_phase = phase;
    e->m50_phaseSpeed = phaseSpeed;
}
