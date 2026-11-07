/* Circle warp: bends a 128x128 screen area of 2x16x16 triangles into a circle.
 * gcc-2.7.2-cdk, ASPSX 2.56. */
#include "common.h"
#include "psyq.h"
#include "main/task.h"
#include "battle/battle.h"
#include "battle_fx_circle_warp/battle_fx_circle_warp.h"

/* main */
extern OTag* characterRenderingOT;
extern s32 gDepthDivider;  /* OT depth shift */
TaskHeader* allocateTaskPair(s32 size, TaskHeader* owner, void (*update)(TaskHeader*), void (*draw)(TaskHeader*), void (*del)(TaskHeader*));
void registerSpriteCallback2Sub0(TaskHeader* task);
void allocateSavePointMeshDataSub0_callback(TaskHeader* task);
void clearShapeTransferEntry(void* data);
s32 getAngleSin(s32); /* sine */
s32 getAngleCos(s32); /* cosine */

void fxCircleWarp_update(TaskHeader* task) {
    Fx02Effect* e = (Fx02Effect*)task->m4_data;

    e->m38_frameCounter++;
    if (battleBssBlock.m8C58_pad.m6_pressed2 & 0x100) {
        task->mC_deleteCallback(task);
    }
}

void fxCircleWarp_draw(TaskHeader* task) {
    Fx02Effect* e = (Fx02Effect*)task->m4_data;
    s32 ofx, ofy;
    s32 h;
    s32 i, j, k;

    ReadGeomOffset(&ofx, &ofy);
    h = ReadGeomScreen();
    SetGeomOffset(0x40, 0x40);
    SetGeomScreen(0x200);
    {
        SVECTOR off[6];

        off[0].vx = 0x40;
        off[0].vy = 0x40;
        for (i = 0; i != 2; i++) {
            for (j = 0; j < 16; j++) {
                for (k = 0; k < 16; k++) {
                    Fx02Tile* t = &e->m40_tiles[i][j][k];
                    POLY_FT3* p = &t->m4_poly[battleBssBlock.m8C84_oddOrEven];
                    p->x0 = t->m44_vertices[0].vx + off[0].vx;
                    p->y0 = t->m44_vertices[0].vy + off[0].vy;
                    p->x1 = t->m44_vertices[1].vx + off[0].vx;
                    p->y1 = t->m44_vertices[1].vy + off[0].vy;
                    p->x2 = t->m44_vertices[2].vx + off[0].vx;
                    p->y2 = t->m44_vertices[2].vy + off[0].vy;
                    p->x0 = t->m5C_screen[0].vx + off[0].vx;
                    p->y0 = t->m5C_screen[0].vy + off[0].vy;
                    p->x1 = t->m5C_screen[1].vx + off[0].vx;
                    p->y1 = t->m5C_screen[1].vy + off[0].vy;
                    p->x2 = t->m5C_screen[2].vx + off[0].vx;
                    p->y2 = t->m5C_screen[2].vy + off[0].vy;
                    AddPrim(characterRenderingOT, p);
                    /* The wide picture (include/pc_wide.h): the warp samples
                     * the screen's top-left corner, which the wide copy of the
                     * frame has too; the margins show the scene beside it. */
                    pcWidePrim(p, 0, PC_WIDE_HANDLED);
                }
            }
        }
    }
    SetGeomOffset(ofx, ofy);
    SetGeomScreen(h);
}

void fxCircleWarp_delete(TaskHeader* task) {
    registerSpriteCallback2Sub0(task + 1);
    allocateSavePointMeshDataSub0_callback(task);
    clearShapeTransferEntry(task);
    gDepthDivider = 4;
}

#define ABS(x) ((x) >= 0 ? (x) : -(x))

void createFxCircleWarp(void) {
    Fx02Effect* e;
    s32 j, i, k, l;
    s32 r, a;

    gDepthDivider = 0;
    e = (Fx02Effect*)allocateTaskPair(sizeof(Fx02Effect), NULL, fxCircleWarp_update, fxCircleWarp_draw, fxCircleWarp_delete);
    e->m38_frameCounter = 0;
    for (i = 0; i != 2; i++) {
        for (j = 0; j < 16; j++) {
            for (k = 0; k < 16; k++) {
                Fx02Tile* t = &e->m40_tiles[i][j][k];
                if (i == 0) {
                    t->m44_vertices[0].vx = k * 8 - 0x40;
                    t->m44_vertices[0].vy = j * 8 - 0x40;
                    t->m44_vertices[0].vz = 0x200;
                    t->m44_vertices[1].vx = k * 8 - 0x38;
                    t->m44_vertices[1].vy = j * 8 - 0x40;
                    t->m44_vertices[1].vz = 0x200;
                    t->m44_vertices[2].vx = k * 8 - 0x40;
                    t->m44_vertices[2].vy = (j - 7) * 8;
                    t->m44_vertices[2].vz = 0x200;
                } else {
                    t->m44_vertices[0].vx = k * 8 - 0x38;
                    t->m44_vertices[0].vy = j * 8 - 0x40;
                    t->m44_vertices[0].vz = 0x200;
                    t->m44_vertices[2].vx = k * 8 - 0x40;
                    t->m44_vertices[2].vy = (j - 7) * 8;
                    t->m44_vertices[2].vz = 0x200;
                    t->m44_vertices[1].vx = k * 8 - 0x38;
                    t->m44_vertices[1].vy = (j - 7) * 8;
                    t->m44_vertices[1].vz = 0x200;
                }
                for (l = 0; l != 3; l++) {
                    r = ABS(ABS(t->m44_vertices[l].vx) > ABS(t->m44_vertices[l].vy) ? t->m44_vertices[l].vx : t->m44_vertices[l].vy);
                    a = ratan2(t->m44_vertices[l].vy, t->m44_vertices[l].vx);
                    t->m5C_screen[l].vx = getAngleSin(a) * r / 4096;
                    t->m5C_screen[l].vy = getAngleCos(a) * r / 4096;
                }
                for (l = 0; l != 2; l++) {
                    POLY_FT3* p = &t->m4_poly[l];
                    SetPolyFT3(p);
                    if (l != 0) {
                        p->tpage = GetTPage(2, 0, k * 8, 0);
                    } else {
                        p->tpage = GetTPage(2, 0, k * 8, 0xE0);
                    }
                    p->r0 = 0x80;
                    p->g0 = 0xA0;
                    p->b0 = 0x80;
                    p->tpage = GetTPage(2, 0, k * 8, (1 - battleBssBlock.m8C84_oddOrEven) * 224);
                    if (i == 0) {
                        p->u0 = (k * 8) & 0x3F;
                        p->v0 = j * 8;
                        p->u1 = ((k * 8) & 0x3F) + 8;
                        p->v1 = j * 8;
                        p->u2 = (k * 8) & 0x3F;
                        p->v2 = j * 8 + 8;
                    } else {
                        p->u0 = ((k * 8) & 0x3F) + 8;
                        p->v0 = j * 8;
                        p->u1 = ((k * 8) & 0x3F) + 8;
                        p->v1 = j * 8 + 8;
                        p->u2 = (k * 8) & 0x3F;
                        p->v2 = j * 8 + 8;
                    }
                }
            }
        }
    }
}
