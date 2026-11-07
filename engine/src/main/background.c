/*
 * Field background polygons (Noah: field.cpp, sBackgroundPoly): allocation
 * and setup of the panorama strips and gradient/fill polys.
 *
 * File 0x8002709C-0x800273C4 (main executable): GCC 2.7.2 (PSY-Q) and ASPSX
 * < 2.50, between the CDK-built font.c and the GCC 2.6.0 code after it.
 */
#include "common.h"
#include "pc_retail_heap.h"
#include "psyq.h"
#include "main/memory.h"

/* Noah: sBackgroundPoly (0x34C bytes) */
typedef struct {
    /* 0x000 */ POLY_FT4 m0_ft4[16];
    /* 0x280 */ POLY_F4 m280_f4[4];
    /* 0x2E0 */ POLY_G4 m2E0_g4[2];
    /* 0x328 */ s32 m328_textureWidth;
    /* 0x32C */ s32 m32C_textureHeight;
    /* 0x330 */ s32 m330_scrollScale;
    /* 0x334 */ s16 m334_vramX;
    /* 0x336 */ s16 m336_vramY;
    /* 0x338 */ s16 m338_colorMode;
    /* 0x33A */ s16 m33A_texV;
    /* 0x33C */ s16 m33C;
    /* 0x33E */ s16 m33E_horizonY;
    /* 0x340 */ s16 m340_horizonDistance;
    /* 0x342 */ s16 m342;
    /* 0x344 */ s16 m344_hasFills;
    /* 0x346 */ s16 m346_gradientScale;
    /* 0x348 */ s16 m348_zoomDivisor;
    /* 0x34A */ s16 m34A_zoomStartDistance;
} BackgroundPoly;

BackgroundPoly* createBackgroundPoly(s32 arg0, s32 arg1, s32 arg2, s32 arg3, s32 clutX, s32 clutY, s32 arg6, s32 arg7,
                             VECTOR* vec, u8* colors, s16 arg10, s16 arg11, s16 arg12) {
    DRAWENV env;
    BackgroundPoly* p;
    s32 i;

    resetMemoryAllocStats(4, 0);
    p = allocateHeap(PC_HEAP_SIZED(sizeof(BackgroundPoly), 0x34c), 0);
    if (p == NULL) {
        return NULL;
    }
    GetDrawEnv(&env);
    p->m328_textureWidth = arg2;
    p->m32C_textureHeight = arg3;
    p->m33C = *(s16*)&vec->vx;
    p->m33E_horizonY = *(s16*)&vec->vy;
    p->m340_horizonDistance = *(s16*)&vec->vz;
    p->m346_gradientScale = arg10;
    p->m348_zoomDivisor = arg11;
    p->m34A_zoomStartDistance = arg12;
    if (vec->vz >= 0) {
        p->m330_scrollScale = arg7;
    } else {
        p->m330_scrollScale = -arg7;
    }
    p->m334_vramX = arg0;
    p->m336_vramY = arg1;
    p->m338_colorMode = arg6;
    p->m33A_texV = arg1 % 256;
    for (i = 0; i < 16; i++) {
        SetPolyFT4(&p->m0_ft4[i]);
        SetShadeTex(&p->m0_ft4[i], 1);
        p->m0_ft4[i].clut = GetClut(clutX, clutY);
    }
    if (colors != NULL) {
        p->m344_hasFills = 1;
        for (i = 0; i < 2; i++) {
            SetPolyF4(&p->m280_f4[i]);
            p->m280_f4[i].r0 = colors[0];
            p->m280_f4[i].g0 = colors[1];
            p->m280_f4[i].b0 = colors[2];
            p->m280_f4[i].x0 = 0;
            p->m280_f4[i].y0 = 0;
            p->m280_f4[i].x1 = 320;
            p->m280_f4[i].y1 = 0;
            p->m280_f4[i].x2 = 0;
            p->m280_f4[i].x3 = 320;
        }
        colors += 4;
        for (i = 0; i < 2; i++) {
            p->m2E0_g4[i].r0 = colors[0];
            p->m2E0_g4[i].g0 = colors[1];
            p->m2E0_g4[i].b0 = colors[2];
            p->m2E0_g4[i].r1 = colors[0];
            p->m2E0_g4[i].g1 = colors[1];
            p->m2E0_g4[i].b1 = colors[2];
            colors += 4;
            p->m2E0_g4[i].r2 = colors[0];
            p->m2E0_g4[i].g2 = colors[1];
            p->m2E0_g4[i].b2 = colors[2];
            p->m2E0_g4[i].r3 = colors[0];
            p->m2E0_g4[i].g3 = colors[1];
            p->m2E0_g4[i].b3 = colors[2];
            colors -= 4;
            SetPolyG4(&p->m2E0_g4[i]);
            p->m2E0_g4[i].x0 = 0;
            p->m2E0_g4[i].x1 = 320;
            p->m2E0_g4[i].x2 = 0;
            p->m2E0_g4[i].x3 = 320;
        }
        colors += 4;
        for (i = 2; i < 4; i++) {
            SetPolyF4(&p->m280_f4[i]);
            p->m280_f4[i].r0 = colors[0];
            p->m280_f4[i].g0 = colors[1];
            p->m280_f4[i].b0 = colors[2];
            p->m280_f4[i].x0 = 0;
            p->m280_f4[i].x1 = 320;
            p->m280_f4[i].x2 = 0;
            p->m280_f4[i].y2 = 240;
            p->m280_f4[i].x3 = 320;
            p->m280_f4[i].y3 = 240;
        }
    } else {
        p->m344_hasFills = 0;
    }
    return p;
}
