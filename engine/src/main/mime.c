/*
 * MIMe vertex morphing (Noah: modelDynamicVertices): per model block, blend
 * vertex and normal offsets toward target weights ("MIMe Work/Vertex/
 * Normal" heap blocks).
 *
 * File 0x8003014C-0x80030750 (main executable), between hand-written render
 * code. Built with GCC 2.7.2 (PSY-Q), ASPSX < 2.50.
 */
#include "common.h"
#include "pc_retail_heap.h"
#include "psyq.h"
#include "main/memory.h"

/* ---- dynamic (morphing) vertices: Noah kernel/modelDynamicVertices.cpp ---- */

/* One vertex offset of a morph target. */
typedef struct {
    /* 0x00 */ s16 m0_offset[3];
    /* 0x06 */ s16 m6_index;
} DynVertOffset;

typedef struct {
    /* 0x00 */ s32 m0_count;
    /* 0x04 */ DynVertOffset* m4_positions;
    /* 0x08 */ DynVertOffset* m8_normals;
} ModelDynVertEntry;

/* ModelBlock.m1C: count, count entries, then { index count, s16* indices }. */
typedef struct {
    /* 0x00 */ s32 m0_count;
    /* 0x04 */ ModelDynVertEntry m4_entries[1];
} ModelDynVerts;

/* Noah: sModelBlock (same layout as src/main/model.c) */
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

/* Noah: sDynamicVerticesData */
typedef struct DynVertData {
    /* 0x00 */ s32 (*m0_getScale)(struct DynVertData*);
    /* 0x04 */ s32 m4_target;
    /* 0x08 */ s32 m8_current;
    /* 0x0C */ s32 mC_step;
    /* 0x10 */ s32 m10[4];
} DynVertData; /* 0x20 */

/* Noah: sDynamicVertices; allocated with its mC_count DynVertData behind it. */
typedef struct {
    /* 0x00 */ ModelBlock* m0_block;
    /* 0x04 */ SVECTOR* m4_vertices;  /* the block's original vertices */
    /* 0x08 */ SVECTOR* m8_normals;   /* the block's original normals */
    /* 0x0C */ s32 mC_count;
    /* 0x10 */ DynVertData* m10_data;
} DynVertices; /* 0x14 */

void traceNextAlloc(s32);

/* Noah: dynamicVerticesDefaultUpdateFunction. Steps m8 towards m4 by mC. */
s32 dynamicVerticesDefaultUpdateFunction(DynVertData* d) {
    if (d->m8_current > d->m4_target) {
        d->m8_current -= d->mC_step;
        if (d->m8_current < d->m4_target) {
            d->m8_current = d->m4_target;
        } else {
            goto end;
        }
    }
    if (d->m8_current < d->m4_target) {
        d->m8_current += d->mC_step;
        if (d->m8_current > d->m4_target) {
            d->m8_current = d->m4_target;
        }
    }
end:
    return d->m8_current;
}

/* Noah: dynamicVertices_resertVertices. Restores the listed vertices. */
void dynamicVertices_resertVertices(SVECTOR* dst, SVECTOR* src, s32 count, s16* indices) {
    s32 unused[1];
    s32 i;
    s32 idx;

    for (i = count - 1; i != -1; i--) {
        idx = indices[i];
        dst[idx].vx = src[idx].vx;
        dst[idx].vy = src[idx].vy;
        dst[idx].vz = src[idx].vz;
    }
}

/* Noah: dynamicVertices_updateVertices. Adds scaled (4.12) offsets. */
void dynamicVertices_updateVertices(SVECTOR* verts, DynVertOffset* offsets, s32 count, s32 scale) {
    s32 idx;

    if (scale != 0) {
        while (--count != -1) {
            idx = offsets->m6_index;
            verts[idx].vx += (offsets->m0_offset[0] * scale) >> 12;
            verts[idx].vy += (offsets->m0_offset[1] * scale) >> 12;
            verts[idx].vz += (offsets->m0_offset[2] * scale) >> 12;
            offsets++;
        }
    }
}

/* Same for normals, renormalising each one. */
void dynamicVertices_updateNormals(SVECTOR* verts, DynVertOffset* offsets, s32 count, s32 scale) {
    s32 idx;

    if (scale != 0) {
        while (--count != -1) {
            idx = offsets->m6_index;
            verts[idx].vx += (offsets->m0_offset[0] * scale) >> 12;
            verts[idx].vy += (offsets->m0_offset[1] * scale) >> 12;
            verts[idx].vz += (offsets->m0_offset[2] * scale) >> 12;
            offsets++;
            VectorNormalSS(&verts[idx], &verts[idx]);
        }
    }
}

/* Noah: initModelDynamicVertices. Takes over the block's vertex/normal
 * buffers and gives the block fresh copies to morph. */
DynVertices* initModelDynamicVertices(ModelBlock* block, s32 flags) {
    ModelDynVerts* src;
    DynVertices* dyn;
    DynVertData* data;
    s32 i;

    src = block->m1C_dynamicVertices;
    if (src == NULL) {
        return NULL;
    }
    traceNextAlloc(0x2B);
    dyn = allocateHeap(PC_HEAP_SIZED(src->m0_count * sizeof(DynVertData) + sizeof(DynVertices), src->m0_count * 0x20 + 0x14), flags);
    dyn->m0_block = block;
    dyn->m4_vertices = block->m8_vertices;
    dyn->m8_normals = block->mC_normals;
    data = (DynVertData*)(dyn + 1);
    dyn->m10_data = data;
    dyn->mC_count = src->m0_count;
    traceNextAlloc(0x2C);
    block->m8_vertices = allocateHeap(PC_HEAP_SIZED(block->m2_numVertices * sizeof(SVECTOR), block->m2_numVertices * 0x8), flags);
    i = block->m2_numVertices;
    while (--i != -1) {
        block->m8_vertices[i].vx = dyn->m4_vertices[i].vx;
        block->m8_vertices[i].vy = dyn->m4_vertices[i].vy;
        block->m8_vertices[i].vz = dyn->m4_vertices[i].vz;
    }
    if (block->m0_flags & 0x10) {
        traceNextAlloc(0x2D);
        block->mC_normals = allocateHeap(PC_HEAP_SIZED(block->m2_numVertices * sizeof(SVECTOR), block->m2_numVertices * 0x8), flags);
        i = block->m2_numVertices;
        while (--i != -1) {
            block->mC_normals[i].vx = dyn->m8_normals[i].vx;
            block->mC_normals[i].vy = dyn->m8_normals[i].vy;
            block->mC_normals[i].vz = dyn->m8_normals[i].vz;
        }
    }
    for (i = 0; i < dyn->mC_count; i++) {
        data->m0_getScale = dynamicVerticesDefaultUpdateFunction;
        data->mC_step = data->m8_current = data->m4_target = 0;
        data++;
    }
    return dyn;
}

/* Noah: updateDynamicVertices. Restores the morphed vertices, then applies
 * every morph target at the scale its update function returns. */
void updateDynamicVertices(DynVertices* dyn) {
    ModelBlock* block;
    ModelDynVertEntry* e;
    DynVertData* data;
    s32 count;
    s32 i;
    s32 scale;

    if (dyn != NULL) {
        block = dyn->m0_block;
        count = dyn->mC_count;
        e = block->m1C_dynamicVertices->m4_entries;
        data = dyn->m10_data;
        dynamicVertices_resertVertices(block->m8_vertices, dyn->m4_vertices, e[count].m0_count, (s16*)e[count].m4_positions);
        for (i = 0; i < count; i++) {
            scale = data->m0_getScale(data);
            dynamicVertices_updateVertices(block->m8_vertices, e[i].m4_positions, e[i].m0_count, scale);
            if (block->m0_flags & 0x10) {
                dynamicVertices_updateNormals(block->mC_normals, e[i].m8_normals, e[i].m0_count, scale);
            }
            data++;
        }
    }
}

/* Frees the morph copies and gives the block back its original buffers. */
void freeModelDynamicVertices(DynVertices* dyn) {
    ModelBlock* block;

    if (dyn != NULL) {
        block = dyn->m0_block;
        freeHeap(block->m8_vertices);
        if (block->m0_flags & 0x10) {
            freeHeap(block->mC_normals);
        }
        block->m8_vertices = dyn->m4_vertices;
        block->mC_normals = dyn->m8_normals;
        freeHeap(dyn);
    }
}
