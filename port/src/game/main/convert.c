/*
 * Disc data with 32-bit pointer slots, converted into native structures after
 * loading (include/pc_convert.h). Formats used by more than one module.
 */
#include "common.h"
#include "pc_retail_heap.h"
#include "main/memory.h"
#include "main/model.h"
#include "pc_convert.h"

void** pcRelocTable(u32* data) {
    return pcRelocTableInto(allocateHeap(PC_HEAP_NATIVE((s32)((data[0] + 1) * sizeof(uptr))), 1), data);
}

void** pcRelocTableInto(void** out, u32* data) {
    uptr* t = (uptr*)out;
    u32 n = data[0];
    u32 i;

    t[0] = n;
    for (i = 1; i <= n; i++) {
        t[i] = (uptr)data + data[i];
    }
    return out;
}

/* A model block as loaded: offsets from the model's start where the native
 * block has pointers. */
typedef struct {
    /* 0x00 */ u16 m0_flags;
    /* 0x02 */ u16 m2_numVertices;
    /* 0x04 */ u16 m4_numPrims;
    /* 0x06 */ u16 m6_numMeshBlocks;
    /* 0x08 */ u32 m8_vertices;
    /* 0x0C */ u32 mC_normals;
    /* 0x10 */ u32 m10_meshBlocks;
    /* 0x14 */ u32 m14_displayList;
    /* 0x18 */ u32 m18_instanceData;
    /* 0x1C */ u32 m1C_dynamicVertices;
    /* 0x20 */ s16 m20_boundingBox[8];
    /* 0x30 */ s32 m30_instanceDataSize;
    /* 0x34 */ s32 m34_primBufferSize;
} DiscModelBlock; /* 0x38 */

typedef struct {
    s32 m0_count;
    u32 m4_positions;
    u32 m8_normals;
} DiscDynVertEntry;

/* The dynamic-vertex table: a count, then count + 1 entries (the last one
 * lists the vertices to restore). */
static s32 dynVertsSize(u8* raw, u32 offset) {
    return offset == 0 ? 0 : (s32)((sizeof(ModelDynVerts) - sizeof(ModelDynVertEntry)) +
                                   (*(s32*)(raw + offset) + 1) * sizeof(ModelDynVertEntry));
}

void pcConvertModel(void* model) {
    u8* raw = model;
    s32 n = *(s32*)raw;
    DiscModelBlock* d = (DiscModelBlock*)(raw + 0x10);
    Model* m;
    u8* extra;
    s32 size;
    s32 fresh;
    s32 i;
    s32 j;

    /* A model whose relocation was undone and that was copied elsewhere (the
     * gear loader) keeps its native copy, as the PS1 block keeps its flags
     * and instance data: only the pointers move to the new bytes. */
    memcpy(&m, raw + 8, sizeof(m));
    fresh = m == NULL;
    if (fresh) {
        size = (s32)(sizeof(Model) + (n - 1) * sizeof(ModelBlock));
        for (i = 0; i < n; i++) {
            size += dynVertsSize(raw, d[i].m1C_dynamicVertices);
        }
        m = allocateHeap(PC_HEAP_NATIVE(size), 1);
        m->m0_numBlocks = n;
        m->m4_flags = *(s32*)(raw + 4);
        m->m8 = 0;
        m->mC = 0;
    }
    extra = (u8*)&m->m10_blocks[n];
    for (i = 0; i < n; i++, d++) {
        ModelBlock* b = &m->m10_blocks[i];

        b->m8_vertices = (SVECTOR*)(raw + d->m8_vertices);
        b->mC_normals = (SVECTOR*)(raw + d->mC_normals);
        b->m10_meshBlocks = raw + d->m10_meshBlocks;
        b->m14_displayList = raw + d->m14_displayList;
        if (fresh) {
            b->m0_flags = d->m0_flags;
            b->m2_numVertices = d->m2_numVertices;
            b->m4_numPrims = d->m4_numPrims;
            b->m6_numMeshBlocks = d->m6_numMeshBlocks;
            b->m18_instanceData = NULL; /* allocated by initModel2; 0 on disc */
            for (j = 0; j < 8; j++) {
                b->m20_boundingBox[j] = d->m20_boundingBox[j];
            }
            b->m30_instanceDataSize = d->m30_instanceDataSize;
            b->m34_primBufferSize = pcModelPrimBufferSize(b->m10_meshBlocks, b->m6_numMeshBlocks);
            if (pcModelRetailPrimBufferSize(b->m10_meshBlocks, b->m6_numMeshBlocks) != d->m34_primBufferSize)
                pcHeapUnsupported(); /* never infer a different original packet extent */
        }
        b->m1C_dynamicVertices = NULL;
        if (d->m1C_dynamicVertices != 0) {
            s32* src = (s32*)(raw + d->m1C_dynamicVertices);
            DiscDynVertEntry* e = (DiscDynVertEntry*)(src + 1);
            ModelDynVerts* dv = (ModelDynVerts*)extra;

            dv->m0_count = src[0];
            for (j = 0; j <= src[0]; j++) {
                dv->m4_entries[j].m0_count = e[j].m0_count;
                dv->m4_entries[j].m4_positions = raw + e[j].m4_positions;
                dv->m4_entries[j].m8_normals = raw + e[j].m8_normals;
            }
            b->m1C_dynamicVertices = dv;
            extra += dynVertsSize(raw, d->m1C_dynamicVertices);
        }
    }
    memcpy(raw + 8, &m, sizeof(m));
}

void* pcModelBlocks(void* model) {
    Model* m;

    memcpy(&m, (u8*)model + 8, sizeof(m));
    return m->m10_blocks;
}

void pcFreeModel(void* model) {
    Model* m;

    memcpy(&m, (u8*)model + 8, sizeof(m));
    if (m != NULL) {
        freeHeap(m);
        m = NULL;
        memcpy((u8*)model + 8, &m, sizeof(m));
    }
}

s32 pcModelBlockPrimBufferSize(void* block) {
    return ((ModelBlock*)block)->m34_primBufferSize;
}

/* A lone model block (relocateModelBlock): the same block format, with its
 * offsets from the block itself. The native block is built once, in the
 * heap; the loaded block's m8/mC words (the vertex and normal offsets, which
 * nothing reads once it is relocated) then hold its address. */
void pcConvertModelBlock(void* raw) {
    u8* base = raw;
    DiscModelBlock* d = raw;
    ModelBlock* b;
    s32 j;

    b = allocateHeap(PC_HEAP_NATIVE((s32)sizeof(ModelBlock) + dynVertsSize(base, d->m1C_dynamicVertices)), 1);
    b->m0_flags = d->m0_flags;
    b->m2_numVertices = d->m2_numVertices;
    b->m4_numPrims = d->m4_numPrims;
    b->m6_numMeshBlocks = d->m6_numMeshBlocks;
    b->m8_vertices = (SVECTOR*)(base + d->m8_vertices);
    b->mC_normals = (SVECTOR*)(base + d->mC_normals);
    b->m10_meshBlocks = base + d->m10_meshBlocks;
    b->m14_displayList = base + d->m14_displayList;
    b->m18_instanceData = NULL; /* allocated by initModel2; 0 on disc */
    for (j = 0; j < 8; j++) {
        b->m20_boundingBox[j] = d->m20_boundingBox[j];
    }
    b->m30_instanceDataSize = d->m30_instanceDataSize;
    b->m34_primBufferSize = pcModelPrimBufferSize(b->m10_meshBlocks, b->m6_numMeshBlocks);
    if (pcModelRetailPrimBufferSize(b->m10_meshBlocks, b->m6_numMeshBlocks) != d->m34_primBufferSize)
        pcHeapUnsupported();
    b->m1C_dynamicVertices = NULL;
    if (d->m1C_dynamicVertices != 0) {
        s32* src = (s32*)(base + d->m1C_dynamicVertices);
        DiscDynVertEntry* e = (DiscDynVertEntry*)(src + 1);
        ModelDynVerts* dv = (ModelDynVerts*)(b + 1);

        dv->m0_count = src[0];
        for (j = 0; j <= src[0]; j++) {
            dv->m4_entries[j].m0_count = e[j].m0_count;
            dv->m4_entries[j].m4_positions = base + e[j].m4_positions;
            dv->m4_entries[j].m8_normals = base + e[j].m8_normals;
        }
        b->m1C_dynamicVertices = dv;
    }
    memcpy(base + 8, &b, sizeof(b));
}

void* pcModelBlockOf(void* raw) {
    ModelBlock* b;

    memcpy(&b, (u8*)raw + 8, sizeof(b));
    return b;
}
