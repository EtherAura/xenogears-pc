/*
 * VRAM shape-transfer queue (double-buffered LoadImage/ClearImage and
 * deferred frees; Noah: field.cpp shapeTransfert*) and the sprite/model
 * draw callbacks (Noah: spriteCallback_render*).
 *
 * Provisional file 0x80024F20-0x80025FA8 (main executable). Separate from
 * sprite.c: it defines shapeTransfertTableEnd/shapeTransfertTableCurrentEntry ($gp access) while sprite.c
 * reaches them with %hi/%lo. Built with GCC 2.7.2-cdk.
 */
#include "common.h"
#include "pc_retail_heap.h"
#include "pc_interpolation.h"
#include "psyq.h"
#include "main/memory.h"
#include "main/task.h"
#include "main/sprite.h"
#include "gte.h"

typedef struct {
    u16 m0;
    u16 m2;
    VECTOR m4;
    VECTOR m14;
} PaletteScratch;

/* ---- types ---------------------------------------------------------------- */

/* Deferred LoadImage/ClearImage (Noah: sShapeTransfert), 0x10 bytes. */
typedef struct ShapeTransfert {
    /* 0x00 */ RECT m0_rect;
    /* 0x08 */ void* m8_data; /* NULL: ClearImage */
    /* 0x0C */ struct ShapeTransfert* mC_pNext;
} ShapeTransfert;

/* Heap block freed when its arena half is reused (Noah: sShapeTransfertTempBuffer). */
typedef struct ShapeTransfertTempBuffer {
    /* 0x00 */ void* m0_data;
    /* 0x04 */ struct ShapeTransfertTempBuffer* m4_pNext;
} ShapeTransfertTempBuffer;

/* ---- globals -------------------------------------------------------------- */

/* Defined here (small, $gp-relative). */
static s32 shapeTransfertDoubleBufferIndex;
static s32 shapeTransfertTableSize;
/* Noah: shapeTransfertTemporaryBuffersLinkedLists (file-local in retail too) */
static ShapeTransfertTempBuffer* shapeTransfertTemporaryBuffersLinkedLists[2];
u8* shapeTransfertBuffer[2];
ShapeTransfert* shapeTransfertTable[2];
u8* shapeTransfertTableStart;
u8* shapeTransfertTableEnd;
u8* shapeTransfertTableCurrentEntry;
extern u8 isBattleOverlayLoaded;
extern u8 isWorldMapOverlayLoaded;
extern s32 battleDefaultEntityScale;
extern MATRIX currentRenderingMatrix;
extern s32 gDepthDivider; /* Noah: gDepthDivider (OT z shift) */
extern OTag* characterRenderingOT;
extern u8 battleSpritesDisabled; /* Noah: battleSpritesDisabled */
/* ---- prototypes ----------------------------------------------------------- */

void resetSpriteCallbacks(void);
void resetSpriteTransfertList(void);
void clearSpriteTransfertList(void);
void spriteCallback_render2_updateMatrix(SpriteActorCore* actor);
void submitSpriteActorToRendering(SpriteActorCore* actor, OTag* ot);
void renderSpriteActor(SpriteActorCore* actor, OTag* ot);
void submitModelForRendering(void* modelBlock, void* primBuffer, OTag* ot, s32 mode);
MATRIX* createRotationMatrix(SVECTOR* r, MATRIX* m);
void renderCustomPolyBuffer(void* modelBlock, void* primBuffer, OTag* ot, s32 a3, s32 depth, s32 blend);
void spriteCallback_render0(TaskHeader* task);
void spriteCallback_render1(TaskHeader* task);
void spriteCallback_render2(TaskHeader* task);
void spriteCallback_render8(TaskHeader* task);
void spriteCallback_render9(TaskHeader* task);
void spriteCallback_render15(TaskHeader* task);

/* ---- .data (0x8004FD40) --------------------------------------------------- */

/* Draw callback per render mode (Noah: spriteBytecode2ExtendedE0_Sub0Sub0_callback). */
void (*spriteBytecode2ExtendedE0_Sub0Sub0_callback[16])(TaskHeader*) = {
    spriteCallback_render0, spriteCallback_render1, spriteCallback_render2, NULL,
    NULL, spriteCallback_render0, spriteCallback_render0, spriteCallback_render2,
    spriteCallback_render8, spriteCallback_render9, NULL, NULL,
    NULL, NULL, spriteCallback_render0, spriteCallback_render15,
};
/* Light colour matrix: column 0 is set per model (spriteCallback_render15). */
MATRIX spriteModelLightColors = {{{0xC00, 0, 0}, {0xC00, 0, 0}, {0xC00, 0, 0}}, {0, 0, 0}};
/* Light direction matrix. */
MATRIX spriteModelLightDirections = {{{0x1000, 0x1000, 0x1000}, {0, 0, 0}, {0, 0, 0}}, {0, 0, 0}};

/* ---- 0x80024F20 ----------------------------------------------------------- */

/* Boot-time reset of the sprite system (called from main after initGameState). */
void resetSpriteSystem(void) {
    isBattleOverlayLoaded = 0;
    isWorldMapOverlayLoaded = 0;
    battleDefaultEntityScale = 0x2000;
    resetSpriteCallbacks();
    resetSpriteTransfertList();
}

void allocateShapeTransfert(s32 size, s32 flags) {
    /* The arena holds primitives and transfer entries, up to twice their PS1
     * size on the PC (8-byte tags and pointers): the PS1 size would fill early
     * and drop sprites. */
    s32 retailBytes = size * 2;
    size *= 2;
    shapeTransfertTableSize = size;
    shapeTransfertBuffer[0] = allocateHeap(PC_HEAP_SIZED(size * 2, retailBytes), flags);
    shapeTransfertBuffer[1] = shapeTransfertBuffer[0] + size;
    shapeTransfertTemporaryBuffersLinkedLists[1] = NULL;
    shapeTransfertTemporaryBuffersLinkedLists[0] = NULL;
    shapeTransfertTable[0] = NULL;
    resetSpriteTransfertList();
}


/* Noah: none (frees the shape-transfer arena). */
void freeShapeTransfert(void) {
    freeHeap(shapeTransfertBuffer[0]);
    clearSpriteTransfertList();
}

void setCharacterRenderingOT(OTag* ot) {
    characterRenderingOT = ot;
}

void setCurrentRenderingMatrix(MATRIX* m) {
    currentRenderingMatrix = *m;
}

/* Noah: shapeTransfert (run this frame's queued VRAM uploads/clears). */
void shapeTransfert(void) {
    ShapeTransfert* p = shapeTransfertTable[shapeTransfertDoubleBufferIndex];
    while (p != NULL) {
        void* data = p->m8_data;
        if (data != NULL) {
            LoadImage(&p->m0_rect, data);
        } else {
            ClearImage(&p->m0_rect, 0, 0, 0);
        }
        p = p->mC_pNext;
    }
    shapeTransfertTable[shapeTransfertDoubleBufferIndex] = NULL;
}

/* Noah: clearShapeTransfertTableEntry (switch arena half, free its deferred blocks). */
void clearShapeTransfertTableEntry(s32 index) {
    ShapeTransfertTempBuffer* p;
    shapeTransfertDoubleBufferIndex = index;
    shapeTransfertTableStart = shapeTransfertTableCurrentEntry = shapeTransfertBuffer[index];
    shapeTransfertTableEnd = shapeTransfertTableStart + shapeTransfertTableSize;
    for (p = shapeTransfertTemporaryBuffersLinkedLists[index]; p != NULL; p = p->m4_pNext) {
        freeHeap(p->m0_data);
    }
    shapeTransfertTemporaryBuffersLinkedLists[index] = NULL;
}

/* Noah: clearShapeTransferEntry (free `data` when this arena half is next reused). */
void clearShapeTransferEntry(void* data) {
    ShapeTransfertTempBuffer* p = (ShapeTransfertTempBuffer*)shapeTransfertTableCurrentEntry;
    shapeTransfertTableCurrentEntry += sizeof(ShapeTransfertTempBuffer);
    if (p != NULL) {
        p->m0_data = data;
        p->m4_pNext = shapeTransfertTemporaryBuffersLinkedLists[shapeTransfertDoubleBufferIndex];
        shapeTransfertTemporaryBuffersLinkedLists[shapeTransfertDoubleBufferIndex] = p;
    }
}

/* Noah: addToShapeTransfertTable (queue a LoadImage, or ClearImage if data is NULL). */
void addToShapeTransfertTable(void* data, s16 x, s16 y, s16 w, s16 h) {
    ShapeTransfert* p = (ShapeTransfert*)shapeTransfertTableCurrentEntry;
    if ((u8*)(p + 1) < shapeTransfertTableEnd) {
        p->m0_rect.x = x;
        p->m0_rect.y = y;
        p->m0_rect.w = w;
        p->m0_rect.h = h;
        p->m8_data = data;
        shapeTransfertTableCurrentEntry = (u8*)(p + 1);
        p->mC_pNext = shapeTransfertTable[shapeTransfertDoubleBufferIndex];
        shapeTransfertTable[shapeTransfertDoubleBufferIndex] = p;
    }
}

/* Noah: spriteBytecode2ExtendedE0_Sub0Sub0 (select the draw callback). */
void spriteBytecode2ExtendedE0_Sub0Sub0(TaskHeader* task, s32 mode) {
    setTaskDrawFunction(task, spriteBytecode2ExtendedE0_Sub0Sub0_callback[mode]);
}

/* Noah: spriteCallback_render0 (2D sprite actor). */
void spriteCallback_render0(TaskHeader* task) {
    SpriteActorCore* actor = task->m4_data;
    SVECTOR pos;
    VECTOR trans;
    s32 sxy;
    s32 flag;
    s32 otIndex;

    if (!actor->mB0_x8 || !battleSpritesDisabled) {
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
            if ((u32)(otIndex - 1) < 0xFFF) {
                submitSpriteActorToRendering(actor, &characterRenderingOT[otIndex]);
            }
        } else {
            if (actor->m3C_x1D) {
                /* Battle effect sprites can have no parent: the PS1 reads
                 * low RAM there, zero (not drawn). */
                otIndex = actor->m70_parent != NULL ? actor->m70_parent->m2E_otIndex : 0;
            }
            if ((u32)(otIndex - 1) < 0xFFF) {
                renderSpriteActor(actor, &characterRenderingOT[otIndex]);
            }
        }
    }
}

/* Noah: spriteCallback_render8 (single-pixel TILE_1 in the actor colour). */
void spriteCallback_render8(TaskHeader* task) {
    SpriteActorCore* actor = task->m4_data;
    SVECTOR pos;
    s32 flag;
    TILE_1* tile;
    DR_TPAGE* tpage;
    s32 otz;

    if ((u16)actor->m34_currentSpriteFrame == 0) {
        tile = (TILE_1*)shapeTransfertTableCurrentEntry;
        if ((u8*)(tile + 1) < shapeTransfertTableEnd) {
            pos.vx = actor->m0_position.vx >> 16;
            pos.vy = actor->m0_position.vy >> 16;
            pos.vz = actor->m0_position.vz >> 16;
            shapeTransfertTableCurrentEntry = (u8*)(tile + 1);
            SetRotMatrix(&currentRenderingMatrix);
            SetTransMatrix(&currentRenderingMatrix);
            otz = RotTransPers(&pos, (s32*)&tile->x0, &flag, &flag) >> gDepthDivider;
            actor->m2E_otIndex = otz;
            setlen(tile, 2);
            *(u32*)&tile->r0 = *(u32*)&actor->m28_r;
            AddPrim(&characterRenderingOT[otz], tile);
            tpage = (DR_TPAGE*)shapeTransfertTableCurrentEntry;
            if ((u8*)(tpage + 1) < shapeTransfertTableEnd) {
                shapeTransfertTableCurrentEntry = (u8*)(tpage + 1);
                setlen(tpage, 1);
                tpage->code[0] = 0xE1000000 | ((actor->m3C_x5_blendMode & 3) << 5);
                AddPrim(&characterRenderingOT[otz], tpage);
            }
        }
    }
}

/* Noah: spriteCallback_render9 (TILE sized by projected distance). */
void spriteCallback_render9(TaskHeader* task) {
    SpriteActorCore* actor = task->m4_data;
    SVECTOR pos;
    SVECTOR pos2;
    s32 sxy1[2]; /* 8-byte slot: retail passes sp+0x30 and sp+0x38 */
    s32 sxy2;
    s32 flag;
    TILE* tile;
    DR_TPAGE* tpage;
    s32 otz;
    s32 size;

    if ((u16)actor->m34_currentSpriteFrame == 0) {
        size = (u16)actor->m36_height; /* same variable as the tile size (both in s1); retail loads it lhu */
        tile = (TILE*)shapeTransfertTableCurrentEntry;
        if ((u8*)(tile + 1) < shapeTransfertTableEnd) {
            pos.vx = actor->m0_position.vx >> 16;
            pos.vy = actor->m0_position.vy >> 16;
            pos.vz = actor->m0_position.vz >> 16;
            shapeTransfertTableCurrentEntry = (u8*)(tile + 1);
            SetRotMatrix(&currentRenderingMatrix);
            SetTransMatrix(&currentRenderingMatrix);
            pos2 = pos;
            pos2.vx += size;
            otz = RotTransPers3(&pos, &pos2, &pos, (s32*)&tile->x0, sxy1, &sxy2, &flag, &flag) >> gDepthDivider;
            actor->m2E_otIndex = otz;
            size = (s16)sxy1[0] - tile->x0;
            if (size == 0) {
                size = 1;
            }
            size = abs(size); /* abssi2: bgez/nop/negu with an unfillable slot */
            tile->x0 -= size / 2;
            tile->y0 -= size / 2;
            *(u32*)&tile->r0 = *(u32*)&actor->m28_r;
            setlen(tile, 3);
            tile->w = tile->h = size;
            AddPrim(&characterRenderingOT[otz], tile);
            tpage = (DR_TPAGE*)shapeTransfertTableCurrentEntry;
            if ((u8*)(tpage + 1) < shapeTransfertTableEnd) {
                shapeTransfertTableCurrentEntry = (u8*)(tpage + 1);
                setlen(tpage, 1);
                tpage->code[0] = 0xE1000000 | ((actor->m3C_x5_blendMode & 3) << 5);
                AddPrim(&characterRenderingOT[otz], tpage);
            }
        }
    }
}

void spriteCallback_render1(TaskHeader* task) {
}

/* Noah: spriteCallback_render2 (3D model actor). */
void spriteCallback_render2(TaskHeader* task) {
    SpriteActorCore* actor = task->m4_data;
    MATRIX m;
    VECTOR trans;

    spriteCallback_render2_updateMatrix(actor);
    if (((ModelObject*)actor->m20_object)->m34_modelBlock != NULL) {
        trans.vx = actor->m0_position.vx >> 16;
        trans.vy = actor->m0_position.vy >> 16;
        trans.vz = actor->m0_position.vz >> 16;
        TransMatrix(&actor->m20_object->mC_matrix, &trans);
        if (!actor->m3C_x18) {
            CompMatrix(&currentRenderingMatrix, &actor->m20_object->mC_matrix, &m);
        }
        SetRotMatrix(&m);
        SetTransMatrix(&m);
        pcInterpolationObject(actor, ((ModelObject*)actor->m20_object)->m34_modelBlock);
        submitModelForRendering(((ModelObject*)actor->m20_object)->m34_modelBlock,
                      ((ModelObject*)actor->m20_object)->m2C_primBuffers[shapeTransfertDoubleBufferIndex], characterRenderingOT,
                      (actor->m40_draw.raw >> 16) & 4);
        pcInterpolationObject(0, 0);
    }
}

/* Draw callback 15: lit 3D model actor (Noah: not implemented). */
void spriteCallback_render15(TaskHeader* task) {
    SpriteActorCore* actor = task->m4_data;
    MATRIX m;
    VECTOR trans;
    MATRIX lightRot;
    MATRIX light;
    s32 ofx;
    s32 ofy;
    s32 savedDepthShift;

    spriteCallback_render2_updateMatrix(actor);
    if (((ModelObject*)actor->m20_object)->m34_modelBlock != NULL) {
        if (actor->m40_draw.bits.x1) {
            PushMatrix();
            spriteModelLightColors.m[0][0] = ((ModelObject*)actor->m20_object)->m4C_lightColor[0];
            spriteModelLightColors.m[1][0] = ((ModelObject*)actor->m20_object)->m4C_lightColor[1];
            spriteModelLightColors.m[2][0] = ((ModelObject*)actor->m20_object)->m4C_lightColor[2];
            createRotationMatrix(&((ModelObject*)actor->m20_object)->m44_lightRotation, &lightRot);
            MulMatrix0(&lightRot, &actor->m20_object->mC_matrix, &lightRot);
            MulMatrix0(&spriteModelLightDirections, &lightRot, &light);
            SetBackColor(0x20, 0x20, 0x20);
            SetColorMatrix(&spriteModelLightColors);
            SetLightMatrix(&light);
            PopMatrix();
        }
        trans.vx = actor->m0_position.vx >> 16;
        trans.vy = actor->m0_position.vy >> 16;
        trans.vz = actor->m0_position.vz >> 16;
        TransMatrix(&actor->m20_object->mC_matrix, &trans);
        if (!actor->m3C_x18) {
            CompMatrix(&currentRenderingMatrix, &actor->m20_object->mC_matrix, &m);
            SetRotMatrix(&m);
            SetTransMatrix(&m);
        } else {
            SetRotMatrix(&actor->m20_object->mC_matrix);
            SetTransMatrix(&actor->m20_object->mC_matrix);
        }
        if (actor->m3C_x1F) {
            ReadGeomOffset(&ofx, &ofy);
            SetGeomOffset(0xA0, 0x70);
        }
        if (actor->m3C_x19) {
            savedDepthShift = gDepthDivider;
            gDepthDivider = 0x10;
            renderCustomPolyBuffer(((ModelObject*)actor->m20_object)->m34_modelBlock,
                          ((ModelObject*)actor->m20_object)->m2C_primBuffers[shapeTransfertDoubleBufferIndex], characterRenderingOT, 0, 0xFEC,
                          actor->m3C_x5_blendMode);
            gDepthDivider = savedDepthShift;
        } else {
            renderCustomPolyBuffer(((ModelObject*)actor->m20_object)->m34_modelBlock,
                          ((ModelObject*)actor->m20_object)->m2C_primBuffers[shapeTransfertDoubleBufferIndex], characterRenderingOT, 0, actor->m30_otOffset,
                          actor->m3C_x5_blendMode);
        }
        if (actor->m3C_x1F) {
            SetGeomOffset(ofx, ofy);
        }
    }
}

/* 3D model actor whose translation is baked into its matrix (not in the spriteBytecode2ExtendedE0_Sub0Sub0_callback table). */
void spriteCallback_renderBillboardModel(TaskHeader* task) {
    SpriteActorCore* actor = task->m4_data;
    MATRIX unused; /* only reserves retail's 0x20-byte stack slot */
    VECTOR out;
    SVECTOR pos;
    s32 savedDepthShift;

    spriteCallback_render2_updateMatrix(actor);
    if (((ModelObject*)actor->m20_object)->m34_modelBlock != NULL) {
        pos.vx = actor->m0_position.vx >> 16;
        pos.vy = actor->m0_position.vy >> 16;
        pos.vz = actor->m0_position.vz >> 16;
        ApplyMatrix(&currentRenderingMatrix, &pos, &out);
        actor->m20_object->mC_matrix.t[0] = currentRenderingMatrix.t[0] + out.vx;
        actor->m20_object->mC_matrix.t[1] = currentRenderingMatrix.t[1] + out.vy;
        actor->m20_object->mC_matrix.t[2] = currentRenderingMatrix.t[2] + out.vz;
        SetRotMatrix(&actor->m20_object->mC_matrix);
        SetTransMatrix(&actor->m20_object->mC_matrix);
        if (actor->m3C_x19) {
            savedDepthShift = gDepthDivider;
            gDepthDivider = 0x10;
            renderCustomPolyBuffer(((ModelObject*)actor->m20_object)->m34_modelBlock,
                          ((ModelObject*)actor->m20_object)->m2C_primBuffers[shapeTransfertDoubleBufferIndex], characterRenderingOT, 0, 0xFEC,
                          actor->m3C_x5_blendMode);
            gDepthDivider = savedDepthShift;
        } else {
            renderCustomPolyBuffer(((ModelObject*)actor->m20_object)->m34_modelBlock,
                          ((ModelObject*)actor->m20_object)->m2C_primBuffers[shapeTransfertDoubleBufferIndex], characterRenderingOT, 0, actor->m30_otOffset,
                          actor->m3C_x5_blendMode);
        }
    }
}

void scalePaletteColorsClamped(s32 count, s32 scale, u16* dst, u16* src) {
    PaletteScratch* s = (PaletteScratch*)PSX_SCRATCH(0x1F800000);

    gte_lddp(scale << 7);
    while (--count != -1) {
        s->m4.vx = *src & 0x1F;
        s->m4.vy = *src & 0x3E0;
        s->m4.vz = *src & 0x7C00;
        gte_ldlvl(&s->m4);
        gte_gpf12();
        gte_stlvl(&s->m14);
        if (s->m14.vx >= 0x20) {
            s->m0 = 0x1F;
        } else {
            s->m0 = s->m14.vx & 0x1F;
        }
        if (s->m14.vy > 0x3E0) {
            s->m0 |= 0x3E0;
        } else {
            s->m0 |= s->m14.vy & 0x3E0;
        }
        if (s->m14.vz > 0x7C00) {
            s->m0 |= 0x7C00;
        } else {
            s->m0 |= s->m14.vz & 0x7C00;
        }
        s->m0 |= *src++ & 0x8000;
        *dst++ = s->m0;
    }
}

void blendPaletteColors(s32 count, u16* src, u16* target, u16* dst, s32 rOfs, s32 gOfs, s32 bOfs, s32 mode, s32 blend) {
    VECTOR v;
    s16 r, g, b;
    s16 nr, ng, nb;
    u16 c;

    if (blend > 0x20) {
        blend = 0x20;
    }
    gte_lddp(blend << 7);
    bOfs <<= 10;
    gOfs <<= 5;
    while (--count != -1) {
        c = *src;
        switch (mode) {
        case 0:
            r = c & 0x1F;
            g = c & 0x3E0;
            b = c & 0x7C00;
            break;
        case 1:
            r = (c & 0x1E) >> 1;
            g = (c & 0x3C0) >> 1;
            b = (c & 0x7800) >> 1;
            break;
        case 2:
            r = (c & 0x1C) >> 2;
            g = (c & 0x380) >> 2;
            b = (c & 0x7000) >> 2;
            break;
        case 3:
            {
                u32 y = c & 0x3E0;
                u32 z = c & 0x7C00;
                u32 x = c & 0x1F;
                r = (s32)(x + (y >> 5) + (z >> 10)) / 3;
                g = r << 5;
                b = r << 10;
            }
            break;
        }
        nr = r + rOfs;
        if (nr < 0) {
            nr = 0;
        }
        ng = g + gOfs;
        if (ng < 0) {
            ng = 0;
        }
        nb = b + bOfs;
        if (nb < 0) {
            nb = 0;
        }
        if (nr > 0x1F) {
            nr = 0x1F;
        }
        if (ng > 0x3E0) {
            ng = 0x3E0;
        }
        if (nb > 0x7C00) {
            nb = 0x7C00;
        }
        v.vx = (*target & 0x1F) - nr;
        v.vy = (*target & 0x3E0) - ng;
        v.vz = (*target & 0x7C00) - nb;
        gte_ldlvl(&v);
        gte_gpf12();
        gte_stlvl(&v);
        if (c != 0) {
            c = (u16)(nr + (v.vx & 0x1F)) | 0x8000 | (u16)(ng + (v.vy & 0x3E0)) | (u16)(nb + (v.vz & 0x7C00));
            *dst = c;
        }
        src++;
        target++;
        dst++;
    }
}
