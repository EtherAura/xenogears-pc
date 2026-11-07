#ifndef MAIN_MODEL_H
#define MAIN_MODEL_H

/* 3D model file types (Noah: kernel/3dModel*), for every module (field and
 * field_mecha name them sModelBlock/sModel too). Same layout as the local
 * types of src/main/model.c, light.c and mime.c. */

#include "common.h"
#include "psyq.h"

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
typedef struct ModelBlock {
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
typedef struct Model {
    /* 0x00 */ s32 m0_numBlocks;
    /* 0x04 */ s32 m4_flags;
    /* 0x08 */ s32 m8;
    /* 0x0C */ s32 mC;
    /* 0x10 */ ModelBlock m10_blocks[1];
} Model;

/* src/main/model.c. setupOverrideTPage/setupOverrideClut (u16 x, u16 y) stay
 * declared by their callers: the PS1 callers pass s16 (their code depends on
 * it), main's own as s32/u16. */
s32 fieldModelRelocation(Model* model);
void initModel1(ModelBlock* b, u8** start, u8** end);
void initModel2(ModelBlock* b, u8* prims, s32 mode);
void freeModelBlock(ModelBlock* b);

#endif /* MAIN_MODEL_H */
