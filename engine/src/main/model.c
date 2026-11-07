/*
 * 3D models (Noah: kernel/3dModel*): offset relocation, heap trimming,
 * submission for rendering, per-primitive-type setup, texture page / CLUT
 * overrides, face normals.
 *
 * File 0x8002C3E8-0x8002E010 (main executable). The start is fixed by the
 * read-only data layout (its jump table begins a new object); the end by the
 * hand-written GTE render loops that follow. Also uploads texture bundles
 * and sets the screen clipping globals. Built with GCC 2.7.2 (PSY-Q), ASPSX
 * < 2.50.
 */
#include "common.h"
#include "psyq.h"
#include "main/memory.h"
#include "pc_convert.h"
#include "pc_interpolation.h"
#include "pc_retail_heap.h"

/* ---- models (0x8002C3E8-): Noah kernel/3dModel*.cpp ------------------------ */

typedef struct {
    s32 m0_count;
    u8* m4_positions;
    u8* m8_normals;
} ModelDynVertEntry;

typedef struct {
    s32 m0_count;
    ModelDynVertEntry m4_entries[1];
} ModelDynVerts;

/* Noah: sModelBlock */
typedef struct {
    /* 0x00 */ u16 m0_flags;
    /* 0x02 */ u16 m2_numVertices;
    /* 0x04 */ u16 m4_numPrims;
    /* 0x06 */ u16 m6_numMeshBlocks;
    /* 0x08 */ SVECTOR* m8_vertices;
    /* 0x0C */ SVECTOR* mC_normals;
    /* 0x10 */ u8* m10_meshBlocks;
    /* 0x14 */ u8* m14_displayList;
    /* 0x18 */ u8* m18_instanceData;
    /* 0x1C */ ModelDynVerts* m1C_dynamicVertices;
    /* 0x20 */ s16 m20_boundingBox[8];
    /* 0x30 */ s32 m30_instanceDataSize;
    /* 0x34 */ s32 m34_primBufferSize;
} ModelBlock; /* 0x38 */

/* Noah: sModel */
typedef struct {
    /* 0x00 */ s32 m0_numBlocks;
    /* 0x04 */ s32 m4_flags;
    /* 0x08 */ s32 m8;
    /* 0x0C */ s32 mC;
    /* 0x10 */ ModelBlock m10_blocks[1];
} Model;

/* Mesh block header in m10_meshBlocks: prim type, count, then the index lists. */
typedef struct {
    /* 0x00 */ u8 m0_primType;
    /* 0x01 */ u8 m1;
    /* 0x02 */ s16 m2_count;
} MeshBlock;

typedef s32 (*PrimFunc)(u8*, s32);

typedef s32 (*PrimInitFunc)(u8*, s16*, s16);

/* Noah: sPolyTypeRenderDefinition */
typedef struct {
    /* 0x00 */ PrimFunc m0_render[6];
    /* 0x18 */ PrimInitFunc m18_init;
    /* 0x1C */ s32 m1C_indexSize;
    /* 0x20 */ s32 m20_displayListSize;
    /* 0x24 */ s32 m24_primSize;
} PrimTypeDef; /* 0x28 */

extern u8 D_8006FAF0[];  /* end of BSS = overlay load address / heap start */
void NormalColorCol3(SVECTOR* v0, SVECTOR* v1, SVECTOR* v2, CVECTOR* v3, CVECTOR* v4, CVECTOR* v5,
                     CVECTOR* v6);
void pcWideModel(void* b, u8* prims, OTag* ot, s32 renderMode);
static u16 primD_initSub0Sub0Var0;           /* Noah: primD_initSub0Sub0Var0 (tpage) */
static u16 primD_initSub0Sub1Var0;           /* Noah: primD_initSub0Sub1Var0 (clut) */
static s32 overrideTPageValue;
static s32 primD_initSub0Sub1Var1;
extern u8* currentModelInstanceDrawPrims;
extern u8* currentModeBlock18;
extern u8* g_currentModelBlockSubBlocks;
extern SVECTOR* currentModelBlockNormals;
extern u8* g_currentModelBlockDisplayLists;
extern SVECTOR* currentModelBlockVertices;
extern OTag* currentOTEntry;
extern CVECTOR fogNearColor; /* near fog colour; cd holds the GTE code byte */
extern s32 fieldPolyCount2;
void traceNextAlloc(s32);
void shrinkAllocation(void*, s32);  /* shrink heap block */
s32 isObjectClipped(ModelBlock* block, u8 mask);
void initModel2Sub0(void);
void setupPrimTexturePage(u16* arg0);
void setupPrimClut(u16* arg0);
s32 primD_isValid(u8* arg0);
void computeFaceNormal(SVECTOR* v0, SVECTOR* v1, SVECTOR* v2, SVECTOR* out);
s32 getLargestAbsComponent(s32 x, s32 y, s32 z);

/* Per-primitive-type setup (below). */
s32 prim0_init(u8* dl, s16* idx, s32 initParam);
s32 prim1_init(u8* dl, s16* idx, s32 initParam);
s32 prim2_init(u8* dl, s16* idx, s32 initParam);
s32 prim3_init(u8* dl, s16* idx);
s32 prim4_init(u32* arg0);
s32 prim5_init(u8* dl);
s32 prim6_init(u8* dl, s16* idx);
s32 prim8_init(u8* dl, s16* idx, s32 initParam);
s32 prim9_init(u8* dl, s16* idx, s32 initParam);
s32 primA_init(u8* dl, s16* idx);
s32 primB_init(u8* dl, s16* idx);
s32 primC_init(u32* arg0);
s32 primD_init(u8* dl);
s32 prim10_init(void);

/* The hand-written GTE render loops (0x8002E010-0x8003014C, assembly): one
 * routine per render mode and triangle/quad family, entered through stubs
 * that load the primitive's GPU code, length and size. Names: Noah's where it
 * has them, otherwise the same primT_M pattern (a polyRenderDefs row T that
 * uses the entry, render mode M); G3 takes row 2 because Noah's prim6_* are
 * GT3 entries. */
typedef s32 PrimRenderFunc(u8* indices, s32 count);
PrimRenderFunc prim3_0, prim2_0, prim4_0, prim5_0;          /* mode 0: GT3 G3 F3 FT3 */
PrimRenderFunc primF_0, primE_0, primC_0, primD_0;          /* mode 0: GT4 G4 F4 FT4 */
PrimRenderFunc prim6_2, prim2_2, prim4_2, prim5_2;          /* mode 2: GT3 G3 F3 FT3 */
PrimRenderFunc primF_2, primE_2, primC_2, primD_2;    /* mode 2: GT4 G4 F4 FT4 */
PrimRenderFunc prim6_3, prim2_3, prim4_3, prim5_3;    /* mode 3: GT3 G3 F3 FT3 */
PrimRenderFunc primF_3, primE_3, primC_3, primD_3;    /* mode 3: GT4 G4 F4 FT4 */
PrimRenderFunc prim5_4, primD_4;                            /* mode 4 */
PrimRenderFunc prim5_5, primD_5;                /* mode 5 */
PrimRenderFunc prim0_1, FT3_FAKE, prim2_1, GT3_FAKE, prim8_1, primD_1; /* mode 1 */
PrimRenderFunc prim10_0;                               /* self-patched loop (prim10) */

/* .data (0x8004FE50-0x80050110; JP 0x8004F4F4) */
/* Noah: polyRenderDefs. Per primitive type (F3 FT3 G3 GT3 x2, F4 FT4 G4 GT4
 * x2, then type 0x10): render loop per mode, setup function, index, display
 * list and primitive sizes. Rows 0-3 and 8-9 have their own setup and mode-1
 * loop; the other modes are shared with rows 4-7 and C-D. */
PrimTypeDef polyRenderDefs[17] = {
    { { prim4_0, prim0_1, prim4_2, prim4_3, prim4_0, prim4_0 }, (PrimInitFunc)prim0_init, 8, 4, sizeof(POLY_F3) },
    { { prim5_0, FT3_FAKE, prim5_2, prim5_3, prim5_4, prim5_5 }, (PrimInitFunc)prim1_init, 8, 8, sizeof(POLY_FT3) },
    { { prim2_0, prim2_1, prim2_2, prim2_3, prim2_0, prim2_0 }, (PrimInitFunc)prim2_init, 8, 4, sizeof(POLY_G3) },
    { { prim3_0, GT3_FAKE, prim6_2, prim6_3, prim3_0, prim3_0 }, (PrimInitFunc)prim3_init, 8, 8, sizeof(POLY_GT3) },
    { { prim4_0, prim4_0, prim4_2, prim4_3, prim4_0, prim4_0 }, (PrimInitFunc)prim4_init, 8, 4, sizeof(POLY_F3) },
    { { prim5_0, prim5_0, prim5_2, prim5_3, prim5_4, prim5_5 }, (PrimInitFunc)prim5_init, 8, 8, sizeof(POLY_FT3) },
    { { prim2_0, prim2_0, prim2_2, prim2_3, prim2_0, prim2_0 }, (PrimInitFunc)prim6_init, 8, 4, sizeof(POLY_G3) },
    { { prim3_0, prim3_0, prim6_2, prim6_3, prim3_0, prim3_0 }, (PrimInitFunc)prim3_init, 8, 8, sizeof(POLY_GT3) },
    { { primC_0, prim8_1, primC_2, primC_3, primC_0, primC_0 }, (PrimInitFunc)prim8_init, 8, 4, sizeof(POLY_F4) },
    { { primD_0, primD_1, primD_2, primD_3, primD_4, primD_5 }, (PrimInitFunc)prim9_init, 8, 0xC, sizeof(POLY_FT4) },
    { { primE_0, primE_0, primE_2, primE_3, primE_0, primE_0 }, (PrimInitFunc)primA_init, 8, 4, sizeof(POLY_G4) },
    { { primF_0, primF_0, primF_2, primF_3, primF_0, primF_0 }, (PrimInitFunc)primB_init, 8, 0xC, sizeof(POLY_GT4) },
    { { primC_0, primC_0, primC_2, primC_3, primC_0, primC_0 }, (PrimInitFunc)primC_init, 8, 4, sizeof(POLY_F4) },
    { { primD_0, primD_0, primD_2, primD_3, primD_4, primD_5 }, (PrimInitFunc)primD_init, 8, 0xC, sizeof(POLY_FT4) },
    { { primE_0, primE_0, primE_2, primE_3, primE_0, primE_0 }, (PrimInitFunc)primA_init, 8, 4, sizeof(POLY_G4) },
    { { primF_0, primF_0, primF_2, primF_3, primF_0, primF_0 }, (PrimInitFunc)primB_init, 8, 0xC, sizeof(POLY_GT4) },
    { { prim10_0, prim10_0, prim10_0, prim10_0, prim10_0, prim10_0 },
      (PrimInitFunc)prim10_init, 8, 4, sizeof(POLY_FT3) },
};
s32 screenClippingX = 319;
s32 screenClippingY = 238 << 16;
s32 gDepthDivider = 2;              /* also read by the render loops, light.c, shape_transfer.c */
s32 objectClippingMask = 1;
s32 overrideTPageMode = 0;
s32 initModel2Sub0Var1 = 1;

/* ==== Probable file boundary: rodata alignment puts jtbl_80018980 (submitModelForRendering)
 * at the start of a new file's .rodata; the strings of printImageFileLoadState end the
 * previous (CD streaming) file. Model code from here on (Noah kernel/3dModel*). */

s32 fieldModelRelocation(Model* model) {

    s32 n = model->m0_numBlocks;
    s32 i = model->m4_flags;
    s32 j;
    ModelBlock* b;
    s32* d;
    ModelDynVertEntry* e;

    if (!(i & 1)) {
        model->m4_flags = i | 1;
        b = model->m10_blocks;
        pcConvertModel(model);
    }
    return n;
}

s32 undoFieldModelRelocation(Model* model) {
    s32 n = model->m0_numBlocks;
    s32 i = model->m4_flags;
    s32 j;
    ModelBlock* b;
    s32* d;
    ModelDynVertEntry* e;

    if (i & 1) {
        model->m4_flags = i & ~1;
        b = model->m10_blocks;
        /* The native copy stays (pc_convert.h): the next relocation of these
         * bytes, or of a copy of them, moves its pointers. */
    }
    return n;
}

void relocateModelBlock(ModelBlock* b) {
    s32 j;
    s32* d;
    ModelDynVertEntry* e;

    if (!(b->m0_flags & 0x20)) {
        b->m0_flags |= 0x20;
        pcConvertModelBlock(b); /* pcModelBlockOf(b) gives the native block */
    }
}

s32 initModel5(Model* model) {
    s32 ret;
    s32 flags = model->m4_flags;

    if (!(flags & 2)) {
        model->m4_flags = flags | 2;
        /* The loaded model keeps its offsets (pc_convert.h): the first
         * block's display list offset is its word at 0x24. */
        shrinkAllocation(model, *(s32*)((u8*)model + 0x24));
        ret = 0;
    } else {
        ret = 1;
    }
    return ret;
}

s32 freeModelBlockDisplayList(ModelBlock* b) {
    if (b->m0_flags & 0x40) {
        return 1;
    }
    b->m0_flags |= 0x40;
    shrinkAllocation(b, (s32)(b->m14_displayList - (u8*)b));
    b->m14_displayList = NULL;
    return 0;
}

void setFogNearColor(u8 arg0, u8 arg1, u8 arg2) {
    fogNearColor.r = arg0;
    fogNearColor.g = arg1;
    fogNearColor.b = arg2;
}

s32 submitModelForRendering(ModelBlock* b, u8* prims, OTag* ot, s32 renderMode) {
    s32 i;
    MeshBlock* mb;
    PrimTypeDef* def;
    PrimFunc fn;

    if (objectClippingMask != 0 && isObjectClipped(b, objectClippingMask) != 0) {
        /* Off the 4:3 screen: drawn into the wide picture only, if it shows
         * in its margins (port/src/game/main/render.c). */
        pcWideModel(b, prims, ot, renderMode);
        return 0;
    }
    currentModelInstanceDrawPrims = prims;
    pcInterpolationModel(prims);
    currentOTEntry = ot;
    g_currentModelBlockSubBlocks = b->m10_meshBlocks;
    currentModeBlock18 = b->m18_instanceData;
    currentModelBlockNormals = b->mC_normals;
    currentModelBlockVertices = b->m8_vertices;
    fieldPolyCount2 += b->m4_numPrims;
    i = b->m6_numMeshBlocks; while (--i != -1) {
        mb = (MeshBlock*)g_currentModelBlockSubBlocks;
        def = &polyRenderDefs[mb->m0_primType];
        switch (renderMode) {
        case 0:
            fn = def->m0_render[0];
            break;
        case 1:
            fn = def->m0_render[1];
            break;
        case 2:
            fn = def->m0_render[2];
            break;
        case 3:
            fn = def->m0_render[3];
            break;
        case 4:
            fn = def->m0_render[4];
            break;
        case 5:
            fn = def->m0_render[5];
            break;
        }
        g_currentModelBlockSubBlocks += 4;
        fn(g_currentModelBlockSubBlocks, mb->m2_count);
        g_currentModelBlockSubBlocks += mb->m2_count * def->m1C_indexSize;
    }
    return 1;
}

void initModel2(ModelBlock* b, u8* prims, s32 mode) {
    s32 initParam;
    u16 flags;
    s32 i;
    s32 j;
    MeshBlock* mb;
    PrimTypeDef* def;
    PrimInitFunc fn;

    currentModelInstanceDrawPrims = prims;
    if (!(b->m0_flags & 1) && b->m30_instanceDataSize != 0 && mode != 0) {
        traceNextAlloc(0x26);
        b->m18_instanceData = allocateHeap(PC_HEAP_BYTES(b->m30_instanceDataSize), 0);
        b->m0_flags |= 1;
    }
    g_currentModelBlockSubBlocks = b->m10_meshBlocks;
    g_currentModelBlockDisplayLists = b->m14_displayList;
    currentModelBlockNormals = b->mC_normals;
    currentModelBlockVertices = b->m8_vertices;
    currentModeBlock18 = b->m18_instanceData;
    flags = b->m0_flags;
    switch (mode) {
    case 0:
        initParam = 0;
        break;
    case 1:
        initParam = 1;
        break;
    case 2:
        if (flags & 2) {
            initParam = 1;
            if (flags & 1) {
                initParam = 4;
            }
            break;
        }
        if (!(flags & 1)) {
            initParam = 1;
            break;
        }
    case 3:
        if (flags & 1) {
            initParam = 3;
            b->m0_flags = flags | 2;
        }
        break;
    }
    fieldPolyCount2 += b->m4_numPrims;
    i = b->m6_numMeshBlocks;
    while (--i != -1) {
        mb = (MeshBlock*)g_currentModelBlockSubBlocks;
        j = mb->m2_count;
        g_currentModelBlockSubBlocks += 4;
        def = &polyRenderDefs[mb->m0_primType];
        fn = def->m18_init;
        while (--j != -1) {
            if (fn(g_currentModelBlockDisplayLists, (s16*)g_currentModelBlockSubBlocks, initParam)) {
                g_currentModelBlockSubBlocks += def->m1C_indexSize;
                currentModelInstanceDrawPrims += def->m24_primSize;
                g_currentModelBlockDisplayLists += def->m20_displayListSize;
            } else {
                g_currentModelBlockDisplayLists += 4;
                j++;
            }
        }
    }
    initModel2Sub0();
}

s32 pcModelPrimBufferSize(u8* meshBlocks, s32 numMeshBlocks) {
    MeshBlock* mb;
    PrimTypeDef* def;
    s32 size = 0;

    while (--numMeshBlocks != -1) {
        mb = (MeshBlock*)meshBlocks;
        def = &polyRenderDefs[mb->m0_primType];
        size += mb->m2_count * def->m24_primSize;
        meshBlocks += 4 + mb->m2_count * def->m1C_indexSize;
    }
    return size;
}

/* The PS1 primitive sizes per polyRenderDefs row (POLY_F3 0x14, FT3 0x20, G3
 * 0x1C, GT3 0x28, F4 0x18, FT4 0x28, G4 0x24, GT4 0x34). */
static const u8 pcPs1PrimSize[17] = { 0x14, 0x20, 0x1C, 0x28, 0x14, 0x20, 0x1C, 0x28,
                                      0x18, 0x28, 0x24, 0x34, 0x18, 0x28, 0x24, 0x34, 0x20 };

s32 pcModelRetailPrimBufferSize(u8* mesh, s32 count) {
    s32 i;
    u64 size = 0;
    if (count < 0) return PC_HEAP_UNKNOWN;
    for (i = 0; i < count; i++) {
        MeshBlock* m = (MeshBlock*)mesh;
        if (m->m0_primType >= 17 || m->m2_count < 0) return PC_HEAP_UNKNOWN;
        size += m->m2_count * pcPs1PrimSize[m->m0_primType];
        if (size > 0x3FFFFFFFu) return PC_HEAP_UNKNOWN;
        mesh += 4 + m->m2_count * polyRenderDefs[m->m0_primType].m1C_indexSize;
    }
    return (s32)size;
}

u8* pcModelPrimBufferAt(u8* meshBlocks, s32 numMeshBlocks, u8* prims, s32 ps1Offset) {
    MeshBlock* mb;
    PrimTypeDef* def;
    s32 ps1Size;
    s32 k;
    s32 o;

    if (ps1Offset < 0) {
        return NULL;
    }
    while (--numMeshBlocks != -1) {
        mb = (MeshBlock*)meshBlocks;
        def = &polyRenderDefs[mb->m0_primType];
        ps1Size = pcPs1PrimSize[mb->m0_primType];
        if (ps1Offset < mb->m2_count * ps1Size) {
            k = ps1Offset / ps1Size;
            o = ps1Offset % ps1Size;
            return prims + k * def->m24_primSize + (o >= 4 ? PRIM_OFS(o) : o);
        }
        ps1Offset -= mb->m2_count * ps1Size;
        prims += mb->m2_count * def->m24_primSize;
        meshBlocks += 4 + mb->m2_count * def->m1C_indexSize;
    }
    return NULL;
}

void initModel1(ModelBlock* b, u8** start, u8** end) {
    s32 retailSize = pcModelRetailPrimBufferSize(b->m10_meshBlocks, b->m6_numMeshBlocks);
    if (retailSize >= 0) retailSize *= 2;
    traceNextAlloc(0x25);
    *start = allocateHeap(PC_HEAP_SIZED(b->m34_primBufferSize * 2, retailSize), 0);
    *end = *start + b->m34_primBufferSize;
}

void freeModelBlock(ModelBlock* b) {
    if (b->m0_flags & 1) {
        freeHeap(b->m18_instanceData);
        b->m0_flags &= ~1;
    }
}

void setupOverrideTPage(u16 x, u16 y) {
    overrideTPageValue = GetTPage(0, 0, x, y) & 0x1F;
    overrideTPageMode = 1;
}

void setupOverrideTPageFull(u16 arg0) {
    overrideTPageValue = arg0;
    overrideTPageMode = 2;
}

void setupOverrideClut(u16 x, u16 y) {
    primD_initSub0Sub1Var1 = GetClut(x, y) & 0xFFF0;
    initModel2Sub0Var1 = 0;
}

void initModel2Sub0(void) {
    overrideTPageMode = 0;
    initModel2Sub0Var1 = 1;
}

void setupPrimTexturePage(u16* arg0) {
    primD_initSub0Sub0Var0 = *arg0;
    if (overrideTPageMode == 1) {
        primD_initSub0Sub0Var0 &= 0xFFE0;
        primD_initSub0Sub0Var0 |= overrideTPageValue;
    } else if (overrideTPageMode == 2) {
        primD_initSub0Sub0Var0 = overrideTPageValue;
    }
}

void setupPrimClut(u16* arg0) {
    primD_initSub0Sub1Var0 = *arg0;
    if (initModel2Sub0Var1 == 0) {
        primD_initSub0Sub1Var0 &= 0xF;
        primD_initSub0Sub1Var0 |= primD_initSub0Sub1Var1;
    }
}

s32 primD_isValid(u8* arg0) {
    u8 code = arg0[3];

    if ((code & 0xF0) == 0xC0) {
        switch (code) {
        case 0xC4:
            setupPrimTexturePage((u16*)arg0);
            return 0;
        case 0xC8:
            setupPrimClut((u16*)arg0);
            return 0;
        }
    }
    return 1;
}

s32 prim0_init(u8* dl, s16* idx, s32 initParam) {
    POLY_FT3* p = (POLY_FT3*)currentModelInstanceDrawPrims;
    SVECTOR normal;

    setlen(p, 4);
    if (initParam & 1) {
        if (initParam & 2) {
            *(u32*)currentModeBlock18 = *(u32*)dl;
            computeFaceNormal(&currentModelBlockVertices[idx[0]], &currentModelBlockVertices[idx[1]], &currentModelBlockVertices[idx[2]],
                          (SVECTOR*)(currentModeBlock18 += 4));
            NormalColorCol((SVECTOR*)currentModeBlock18, (CVECTOR*)dl, (CVECTOR*)&p->r0);
            currentModeBlock18 += 8;
        } else {
            computeFaceNormal(&currentModelBlockVertices[idx[0]], &currentModelBlockVertices[idx[1]], &currentModelBlockVertices[idx[2]], &normal);
            NormalColorCol(&normal, (CVECTOR*)dl, (CVECTOR*)&p->r0);
        }
        p->code = dl[3];
    } else if (initParam & 4) {
        currentModeBlock18 += 4;
        NormalColorCol((SVECTOR*)currentModeBlock18, (CVECTOR*)dl, (CVECTOR*)&p->r0);
        currentModeBlock18 += 8;
        p->code = dl[3];
    } else {
        *(u32*)&p->r0 = *(u32*)dl;
    }
    return 1;
}

s32 prim4_init(u32* arg0) {
    u8* p = currentModelInstanceDrawPrims;
    setlen(p, 4);
    *(u32*)(p + PRIM_OFS(4)) = *arg0;
    return 1;
}

s32 prim8_init(u8* dl, s16* idx, s32 initParam) {
    POLY_FT3* p = (POLY_FT3*)currentModelInstanceDrawPrims;
    SVECTOR normal;

    setlen(p, 4);
    if (initParam & 1) {
        if (initParam & 2) {
            *(u32*)currentModeBlock18 = *(u32*)dl;
            computeFaceNormal(&currentModelBlockVertices[idx[0]], &currentModelBlockVertices[idx[1]], &currentModelBlockVertices[idx[2]],
                          (SVECTOR*)(currentModeBlock18 += 4));
            NormalColorCol((SVECTOR*)currentModeBlock18, (CVECTOR*)dl, (CVECTOR*)&p->r0);
            currentModeBlock18 += 8;
        } else {
            computeFaceNormal(&currentModelBlockVertices[idx[0]], &currentModelBlockVertices[idx[1]], &currentModelBlockVertices[idx[2]], &normal);
            NormalColorCol(&normal, (CVECTOR*)dl, (CVECTOR*)&p->r0);
        }
        p->code = dl[3];
    } else if (initParam & 4) {
        currentModeBlock18 += 4;
        NormalColorCol((SVECTOR*)currentModeBlock18, (CVECTOR*)dl, (CVECTOR*)&p->r0);
        currentModeBlock18 += 8;
        p->code = dl[3];
    } else {
        *(u32*)&p->r0 = *(u32*)dl;
    }
    return 1;
}

s32 primC_init(u32* arg0) {
    u8* p = currentModelInstanceDrawPrims;
    setlen(p, 5);
    *(u32*)(p + PRIM_OFS(4)) = *arg0;
    return 1;
}

s32 primD_init(u8* dl) {
    u8* p;

    if (primD_isValid(dl) == 0) {
        return 0;
    }
    p = currentModelInstanceDrawPrims;
    setlen(p, 9);
    *(u32*)(p + PRIM_OFS(4)) = *(u32*)dl;
    *(u32*)(p + PRIM_OFS(0xC)) = *(u16*)(dl + 4) | (primD_initSub0Sub1Var0 << 16);
    *(u32*)(p + PRIM_OFS(0x14)) = *(u16*)(dl + 6) | (primD_initSub0Sub0Var0 << 16);
    *(u16*)(p + PRIM_OFS(0x1C)) = *(u16*)(dl + 8);
    *(u16*)(p + PRIM_OFS(0x24)) = *(u16*)(dl + 0xA);
    return 1;
}

s32 primA_init(u8* dl, s16* idx) {
    u8* p = currentModelInstanceDrawPrims;

    setlen(p, 8);
    NormalColorCol3(&currentModelBlockNormals[idx[0]], &currentModelBlockNormals[idx[1]], &currentModelBlockNormals[idx[2]], (CVECTOR*)dl,
                    (CVECTOR*)(p + PRIM_OFS(4)), (CVECTOR*)(p + PRIM_OFS(0xC)), (CVECTOR*)(p + PRIM_OFS(0x14)));
    NormalColorCol(&currentModelBlockNormals[idx[3]], (CVECTOR*)dl, (CVECTOR*)(p + PRIM_OFS(0x1C)));
    p[PRIM_OFS(7)] = dl[3];
    return 1;
}

s32 primB_init(u8* dl, s16* idx) {
    u8* p;

    if (primD_isValid(dl) == 0) {
        return 0;
    }
    p = currentModelInstanceDrawPrims;
    setlen(p, 0xC);
    NormalColor3(&currentModelBlockNormals[idx[0]], &currentModelBlockNormals[idx[1]], &currentModelBlockNormals[idx[2]], (CVECTOR*)(p + PRIM_OFS(4)),
                 (CVECTOR*)(p + PRIM_OFS(0x10)), (CVECTOR*)(p + PRIM_OFS(0x1C)));
    NormalColor(&currentModelBlockNormals[idx[3]], (CVECTOR*)(p + PRIM_OFS(0x28)));
    *(u32*)(p + PRIM_OFS(0xC)) = *(u16*)(dl + 4) | (primD_initSub0Sub1Var0 << 16);
    *(u32*)(p + PRIM_OFS(0x18)) = *(u16*)(dl + 6) | (primD_initSub0Sub0Var0 << 16);
    *(u16*)(p + PRIM_OFS(0x24)) = *(u16*)(dl + 8);
    *(u16*)(p + PRIM_OFS(0x30)) = *(u16*)(dl + 0xA);
    p[PRIM_OFS(7)] = dl[3];
    return 1;
}

s32 primE_init(u8* dl, s16* idx) {
    u8* p = currentModelInstanceDrawPrims;

    setlen(p, 8);
    *(u32*)(p + PRIM_OFS(4)) = *(u32*)dl;
    NormalColorCol3(&currentModelBlockNormals[idx[0]], &currentModelBlockNormals[idx[1]], &currentModelBlockNormals[idx[2]], (CVECTOR*)dl,
                    (CVECTOR*)(p + PRIM_OFS(4)), (CVECTOR*)(p + PRIM_OFS(0xC)), (CVECTOR*)(p + PRIM_OFS(0x14)));
    NormalColorCol(&currentModelBlockNormals[idx[3]], (CVECTOR*)dl, (CVECTOR*)(p + PRIM_OFS(0x1C)));
    p[PRIM_OFS(7)] = dl[3];
    return 1;
}

s32 primF_init(u8* dl, s16* idx) {
    u8* p;

    if (primD_isValid(dl) == 0) {
        return 0;
    }
    p = currentModelInstanceDrawPrims;
    setlen(p, 0xC);
    NormalColor3(&currentModelBlockNormals[idx[0]], &currentModelBlockNormals[idx[1]], &currentModelBlockNormals[idx[2]], (CVECTOR*)(p + PRIM_OFS(4)),
                 (CVECTOR*)(p + PRIM_OFS(0x10)), (CVECTOR*)(p + PRIM_OFS(0x1C)));
    NormalColor(&currentModelBlockNormals[idx[3]], (CVECTOR*)(p + PRIM_OFS(0x28)));
    *(u32*)(p + PRIM_OFS(0xC)) = *(u16*)(dl + 4) | (primD_initSub0Sub1Var0 << 16);
    *(u32*)(p + PRIM_OFS(0x18)) = *(u16*)(dl + 6) | (primD_initSub0Sub0Var0 << 16);
    *(u16*)(p + PRIM_OFS(0x24)) = *(u16*)(dl + 8);
    *(u16*)(p + PRIM_OFS(0x30)) = *(u16*)(dl + 0xA);
    p[PRIM_OFS(7)] = dl[3];
    return 1;
}

s32 prim9_init(u8* dl, s16* idx, s32 initParam) {
    u8* p;
    SVECTOR normal;

    if (!primD_isValid(dl)) {
        return 0;
    }
    p = currentModelInstanceDrawPrims;
    setlen(p, 9);
    *(u32*)(p + PRIM_OFS(0xC)) = *(u16*)(dl + 4) | (primD_initSub0Sub1Var0 << 16);
    *(u32*)(p + PRIM_OFS(0x14)) = *(u16*)(dl + 6) | (primD_initSub0Sub0Var0 << 16);
    *(u16*)(p + PRIM_OFS(0x1C)) = *(u16*)(dl + 8);
    *(u16*)(p + PRIM_OFS(0x24)) = *(u16*)(dl + 0xA);
    if (initParam & 1) {
        if (initParam & 2) {
            computeFaceNormal(&currentModelBlockVertices[idx[0]], &currentModelBlockVertices[idx[1]], &currentModelBlockVertices[idx[2]],
                          (SVECTOR*)currentModeBlock18);
            NormalColor((SVECTOR*)currentModeBlock18, (CVECTOR*)(p + PRIM_OFS(4)));
        } else {
            computeFaceNormal(&currentModelBlockVertices[idx[0]], &currentModelBlockVertices[idx[1]], &currentModelBlockVertices[idx[2]], &normal);
            NormalColor(&normal, (CVECTOR*)(p + PRIM_OFS(4)));
        }
    } else if (initParam & 4) {
        NormalColor((SVECTOR*)currentModeBlock18, (CVECTOR*)(p + PRIM_OFS(4)));
    }
    currentModeBlock18 += 8;
    p[PRIM_OFS(7)] = dl[3];
    return 1;
}

s32 prim2_init(u8* dl, s16* idx, s32 initParam) {
    u8* p = currentModelInstanceDrawPrims;

    setlen(p, 6);
    if (initParam & 2) {
        *(u32*)currentModeBlock18 = *(u32*)dl;
        currentModeBlock18 += 4;
    }
    NormalColorCol3(&currentModelBlockNormals[idx[0]], &currentModelBlockNormals[idx[1]], &currentModelBlockNormals[idx[2]], (CVECTOR*)dl,
                    (CVECTOR*)(p + PRIM_OFS(4)), (CVECTOR*)(p + PRIM_OFS(0xC)), (CVECTOR*)(p + PRIM_OFS(0x14)));
    p[PRIM_OFS(7)] = dl[3];
    return 1;
}

s32 prim6_init(u8* dl, s16* idx) {
    u8* p = currentModelInstanceDrawPrims;

    setlen(p, 6);
    NormalColorCol3(&currentModelBlockNormals[idx[0]], &currentModelBlockNormals[idx[1]], &currentModelBlockNormals[idx[2]], (CVECTOR*)dl,
                    (CVECTOR*)(p + PRIM_OFS(4)), (CVECTOR*)(p + PRIM_OFS(0xC)), (CVECTOR*)(p + PRIM_OFS(0x14)));
    p[PRIM_OFS(7)] = dl[3];
    return 1;
}

s32 prim1_init(u8* dl, s16* idx, s32 initParam) {
    u8* p;
    SVECTOR normal;

    if (!primD_isValid(dl)) {
        return 0;
    }
    p = currentModelInstanceDrawPrims;
    setlen(p, 7);
    *(u32*)(p + PRIM_OFS(0xC)) = *(u16*)(dl + 4) | (primD_initSub0Sub1Var0 << 16);
    *(u32*)(p + PRIM_OFS(0x14)) = *(u16*)(dl + 6) | (primD_initSub0Sub0Var0 << 16);
    *(u16*)(p + PRIM_OFS(0x1C)) = *(u16*)dl;
    if (initParam & 1) {
        if (initParam & 2) {
            computeFaceNormal(&currentModelBlockVertices[idx[0]], &currentModelBlockVertices[idx[1]], &currentModelBlockVertices[idx[2]],
                          (SVECTOR*)currentModeBlock18);
            NormalColor((SVECTOR*)currentModeBlock18, (CVECTOR*)(p + PRIM_OFS(4)));
        } else {
            computeFaceNormal(&currentModelBlockVertices[idx[0]], &currentModelBlockVertices[idx[1]], &currentModelBlockVertices[idx[2]], &normal);
            NormalColor(&normal, (CVECTOR*)(p + PRIM_OFS(4)));
        }
    } else if (initParam & 4) {
        NormalColor((SVECTOR*)currentModeBlock18, (CVECTOR*)(p + PRIM_OFS(4)));
    }
    currentModeBlock18 += 8;
    p[PRIM_OFS(7)] = dl[3];
    return 1;
}

s32 prim5_init(u8* dl) {
    u8* p;

    if (primD_isValid(dl) == 0) {
        return 0;
    }
    p = currentModelInstanceDrawPrims;
    setlen(p, 7);
    *(u32*)(p + PRIM_OFS(0xC)) = *(u16*)(dl + 4) | (primD_initSub0Sub1Var0 << 16);
    *(u32*)(p + PRIM_OFS(0x14)) = *(u16*)(dl + 6) | (primD_initSub0Sub0Var0 << 16);
    *(u16*)(p + PRIM_OFS(0x1C)) = *(u16*)dl;
    p[PRIM_OFS(7)] = dl[3];
    return 1;
}

s32 prim3_init(u8* dl, s16* idx) {
    u8* p;

    if (primD_isValid(dl) == 0) {
        return 0;
    }
    p = currentModelInstanceDrawPrims;
    setlen(p, 9);
    NormalColor3(&currentModelBlockNormals[idx[0]], &currentModelBlockNormals[idx[1]], &currentModelBlockNormals[idx[2]], (CVECTOR*)(p + PRIM_OFS(4)),
                 (CVECTOR*)(p + PRIM_OFS(0x10)), (CVECTOR*)(p + PRIM_OFS(0x1C)));
    *(u32*)(p + PRIM_OFS(0xC)) = *(u16*)(dl + 4) | (primD_initSub0Sub1Var0 << 16);
    *(u32*)(p + PRIM_OFS(0x18)) = *(u16*)(dl + 6) | (primD_initSub0Sub0Var0 << 16);
    *(u16*)(p + PRIM_OFS(0x24)) = *(u16*)dl;
    p[PRIM_OFS(7)] = dl[3];
    return 1;
}

s32 prim10_init(void) {
    POLY_FT3* p = (POLY_FT3*)currentModelInstanceDrawPrims;

    SetPolyFT3(p);
    SetShadeTex(p, 1);
    p->tpage = (GetTPage(1, 0, 0x280, 0) & 0xFFE0) | overrideTPageValue;
    p->clut = (GetClut(0, 0x1E0) & 0xF) | primD_initSub0Sub1Var1;
    return 1;
}

void computeFaceNormal(SVECTOR* v0, SVECTOR* v1, SVECTOR* v2, SVECTOR* out) {
    VECTOR a;
    VECTOR b;
    VECTOR c;
    s32 m;
    s32 len;

    a.vx = v1->vx - v0->vx;
    a.vy = v1->vy - v0->vy;
    a.vz = v1->vz - v0->vz;
    b.vx = v2->vx - v0->vx;
    b.vy = v2->vy - v0->vy;
    b.vz = v2->vz - v0->vz;
    OuterProduct0(&b, &a, &c);
    m = getLargestAbsComponent(c.vx, c.vy, c.vz);
    if (m < 0) {
        m = -m;
    }
    len = SquareRoot0(m);
    c.vx = R3000_DIV(c.vx, len);
    c.vy = R3000_DIV(c.vy, len);
    c.vz = R3000_DIV(c.vz, len);
    VectorNormalS(&c, out);
}

s32 getLargestAbsComponent(s32 x, s32 y, s32 z) {
    s32 ax = x;
    s32 ay = y;
    s32 az = z;

    if (ax < 0) ax = -ax;
    if (ay < 0) ay = -ay;
    if (az < 0) az = -az;
    if (ax >= ay && ax >= az) return x;
    if (ay >= ax && ay >= az) return y;
    if (az >= ax && az >= ay) return z;
    FALLOFF_RETURN(1); /* unreachable: one of the three is always the largest */
}

void loadMenuImageBundle(u32* arg0) {
    TIM_IMAGE tim;
    s32 n = *arg0;

    while (--n != -1) {
        OpenTIM(arg0 + (arg0[n + 1] >> 2));
        ReadTIM(&tim);
        if (tim.caddr != NULL) {
            DrawSync(0);
            LoadImage(tim.crect, tim.caddr);
        }
        DrawSync(0);
        LoadImage(tim.prect, tim.paddr);
    }
}

/*
 * Noah: uploadTextureToVram (field/fieldGraphicObject.cpp). Walks a texture
 * bundle ({count, offsets[count]} then count entries of {tag, x0, y0, x1, y1,
 * w, h, pixels}) and LoadImage()s each one. Tag 0x1100 is a texture page,
 * 0x1101 a CLUT; mode/mode2 pick the destination: 1 = (x, y) + (x1, y1),
 * 2 = (x, y) + (x0 + x1, y0 + y1), anything else = (x0 + x1, y0 + y1).
 * Returns 1 on an unknown tag, else 0.
 *
 * Matching notes: the "return 1" must sit before the 0x1101 switch (so the
 * default case falls into the loop tail and cross-jumps with case 1), and the
 * pointer advance needs the do/while(0) (a loop note acts as a scheduling
 * barrier; without it sched1 hoists i++ between mult and mflo).
 */
s32 uploadTextureToVram(s32* data, s16 mode, s16 x, s16 y, s16 mode2, s16 clutX, s16 clutY) {
    RECT rect;
    s32 count = *data;
    u16* p = (u16*)((u8*)data + (count * 4 + 4));
    s32 i;
    s32 tag;

    for (i = 0; i < count; i++) {
        tag = *(s32*)p;
        p += 2;
        if (tag == 0x1100) {
            switch (mode) {
            case 1:
                rect.x = x + p[2];
                rect.y = y + p[3];
                break;
            case 2:
                rect.x = x + p[0] + p[2];
                rect.y = y + p[1] + p[3];
                break;
            default:
                rect.x = p[0] + p[2];
                rect.y = p[1] + p[3];
                break;
            }
        } else {
            if (tag != 0x1101) {
                return 1;
            }
            switch (mode2) {
            case 1:
                rect.x = clutX + p[2];
                rect.y = clutY + p[3];
                break;
            case 2:
                rect.x = clutX + p[0] + p[2];
                rect.y = clutY + p[1] + p[3];
                break;
            default:
                rect.x = p[0] + p[2];
                rect.y = p[1] + p[3];
                break;
            }
        }
        p += 4;
        rect.w = *p++;
        rect.h = *p++;
        LoadImage(&rect, p);
        do {
            p += rect.w * rect.h;
        } while (0);
    }
    return 0;
}

/* End of the main executable's BSS (overlays load here; kernel.c passes it
 * to the heap initialiser initHeap as the heap start). */
u8* getBssEnd(void) {
    return D_8006FAF0;
}

/* Noah: initFieldDrawEnvsSub0. Sets the screen clip bounds the hand-written
 * GTE render loops (prim3_0..) test projected vertices against. */
void initFieldDrawEnvsSub0(s32 w, s32 h) {
    screenClippingY = (h - 1) << 16;
    screenClippingX = w;
}
