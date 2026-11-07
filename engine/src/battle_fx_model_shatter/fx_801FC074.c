/* Model shatter: breaks a battle custom-poly model into flying polygons.
 * gcc-2.7.2-cdk, ASPSX 2.56. */
#include "common.h"
#include "psyq.h"
#include "main/task.h"
#include "main/memory.h"
#include "battle/battle.h"
#include "battle_fx_model_shatter/battle_fx_model_shatter.h"

/* main */
extern MATRIX currentRenderingMatrix;
extern OTag* characterRenderingOT;
TaskHeader* allocateTaskPair(s32 size, TaskHeader* owner, void (*update)(TaskHeader*), void (*draw)(TaskHeader*), void (*del)(TaskHeader*));
void registerSpriteCallback2Sub0(TaskHeader* task);
void allocateSavePointMeshDataSub0_callback(TaskHeader* task);
void clearShapeTransferEntry(void* data);
void createRotationMatrix(SVECTOR* rot, MATRIX* m);
/* battle */
s32 getCustomPolySubBufferSize(sCustomPolySubBuffer* sub);
void computeDirectionAngles(SVECTOR* a, SVECTOR* b, SVECTOR* out);
/* this overlay */
extern SVECTOR fxModelShatterOrigin;

void fxModelShatter_delete(TaskHeader* task) {
    Fx05Effect* e = (Fx05Effect*)task->m4_data;

    freeHeap(e->m70_particles);
    clearShapeTransferEntry(e->m64_prims[0]);
    registerSpriteCallback2Sub0(&e->m1C_drawTask);
    allocateSavePointMeshDataSub0_callback(&e->m0_updateTask);
    freeHeap(e);
}

void fxModelShatter_update(TaskHeader* task) {
    Fx05Effect* e = (Fx05Effect*)task->m4_data;
    s32 n = e->m58_count;
    Fx05Particle* p = e->m70_particles;
    s32 i;

    for (i = 0; i != n; i++, p++) {
        p->m10_position.vx += p->m20_velocity.vx;
        p->m10_position.vy += p->m20_velocity.vy;
        p->m10_position.vz += p->m20_velocity.vz;
        p->m0_rotation.vx += p->m8_rotationStep.vx;
        p->m0_rotation.vy += p->m8_rotationStep.vy;
        p->m0_rotation.vz += p->m8_rotationStep.vz;
        p->m20_velocity.vy += p->m30_gravity;
    }
    if (--e->m5C_timer < 0) {
        task->mC_deleteCallback(task);
    }
}

void fxModelShatter_draw(TaskHeader* task);

void fxModelShatter_draw(TaskHeader* task) {
    Fx05Effect* e = (Fx05Effect*)task->m4_data;
    MATRIX lm;
    VECTOR t;
    MATRIX m;
    s32 sxy[4];
    s32 depth, flag;
    u8* prim;
    u8* hdr;
    Fx05Particle* p;
    s32 n;
    s32 i;
    s32 code;

    CompMatrix(&currentRenderingMatrix, &e->m38_matrix, &lm);
    p = e->m70_particles;
    prim = e->m64_prims[battleBssBlock.m8C84_oddOrEven];
    {
        sCustomPolySubBuffer* model = e->m6C_model;
        hdr = (u8*)(model->m10_offsetToPackets + (sptr)model);
    }
    n = e->m58_count;
    for (i = 0; i != n; i++, p++) {
        t.vx = (s16)(p->m10_position.vx >> 16);
        t.vy = (s16)(p->m10_position.vy >> 16);
        t.vz = (s16)(p->m10_position.vz >> 16);
        TransMatrix(&m, &t);
        createRotationMatrix(&p->m0_rotation, &m);
        CompMatrix(&lm, &m, &m);
        SetTransMatrix(&m);
        SetRotMatrix(&m);
        code = hdr[3] & 0x1C;
        if (code & 8) {
            RotTransPers4(&p->m34_vertices[0], &p->m34_vertices[1], &p->m34_vertices[2], &p->m34_vertices[3],
                          &sxy[0], &sxy[1], &sxy[2], &sxy[3], &depth, &flag);
        } else {
            RotTransPers3(&p->m34_vertices[0], &p->m34_vertices[1], &p->m34_vertices[2],
                          &sxy[0], &sxy[1], &sxy[2], &depth, &flag);
        }
        AddPrim(characterRenderingOT, prim);
        switch (code) {
        case 0x0:
            *(s32*)(prim + PRIM_OFS(0x8)) = sxy[0];
            *(s32*)(prim + PRIM_OFS(0xC)) = sxy[1];
            *(s32*)(prim + PRIM_OFS(0x10)) = sxy[2];
            break;
        case 0x4:
            *(s32*)(prim + PRIM_OFS(0x8)) = sxy[0];
            *(s32*)(prim + PRIM_OFS(0x10)) = sxy[1];
            *(s32*)(prim + PRIM_OFS(0x18)) = sxy[2];
            break;
        case 0x8:
            *(s32*)(prim + PRIM_OFS(0x8)) = sxy[0];
            *(s32*)(prim + PRIM_OFS(0xC)) = sxy[1];
            *(s32*)(prim + PRIM_OFS(0x10)) = sxy[2];
            *(s32*)(prim + PRIM_OFS(0x14)) = sxy[3];
            break;
        case 0xC:
            *(s32*)(prim + PRIM_OFS(0x8)) = sxy[0];
            *(s32*)(prim + PRIM_OFS(0x10)) = sxy[1];
            *(s32*)(prim + PRIM_OFS(0x18)) = sxy[2];
            *(s32*)(prim + PRIM_OFS(0x20)) = sxy[3];
            break;
        case 0x10:
            *(s32*)(prim + PRIM_OFS(0x8)) = sxy[0];
            *(s32*)(prim + PRIM_OFS(0x10)) = sxy[1];
            *(s32*)(prim + PRIM_OFS(0x18)) = sxy[2];
            break;
        case 0x14:
            *(s32*)(prim + PRIM_OFS(0x8)) = sxy[0];
            *(s32*)(prim + PRIM_OFS(0x14)) = sxy[1];
            *(s32*)(prim + PRIM_OFS(0x20)) = sxy[2];
            break;
        case 0x18:
            *(s32*)(prim + PRIM_OFS(0x8)) = sxy[0];
            *(s32*)(prim + PRIM_OFS(0x10)) = sxy[1];
            *(s32*)(prim + PRIM_OFS(0x18)) = sxy[2];
            *(s32*)(prim + PRIM_OFS(0x20)) = sxy[3];
            break;
        case 0x1C:
            *(s32*)(prim + PRIM_OFS(0x8)) = sxy[0];
            *(s32*)(prim + PRIM_OFS(0x14)) = sxy[1];
            *(s32*)(prim + PRIM_OFS(0x20)) = sxy[2];
            *(s32*)(prim + PRIM_OFS(0x2C)) = sxy[3];
            break;
        }
        prim += (hdr[0] + 1) * 4 + PRIM_OFS(0); /* PC tags are 4 bytes longer */
        hdr += (hdr[1] + 1) * 4;
    }
}

/* Halfword at a byte offset of a packet header. */
#define H(p, o) (*(u16*)((u8*)(p) + (o)))

void createFxModelShatter(sCustomPolySubBuffer* model, u8* prims, MATRIX* m, s32 gravity, s32 speed, s32 speedRand, s32 rotRand, s32 duration) {
    SVECTOR c;
    SVECTOR rot;
    VECTOR t;
    MATRIX rm;
    Fx05Effect* e;
    s32 count;
    Fx05Particle* p;
    s32 size;
    u8* buf;
    u8* hdr;
    sptr verts;
    SVECTOR* v0;
    SVECTOR* v1;
    SVECTOR* v2;
    SVECTOR* v3;
    s32 i;
    s32 code;
    s32 r;
    sCustomPolySubBuffer* sub;

    e = (Fx05Effect*)allocateTaskPair(sizeof(Fx05Effect), NULL, fxModelShatter_update, fxModelShatter_draw, fxModelShatter_delete);
    e->m6C_model = model;
    count = model->m14_count;
    e->m58_count = count;
    p = allocateHeap(count * sizeof(Fx05Particle), 0);
    e->m70_particles = p;
    e->m38_matrix = *m;
    e->m5C_timer = duration;
    size = getCustomPolySubBufferSize(model);
    sub = model;
    if (prims == NULL) {
        buf = allocateHeap(size * 2, 0);
        copyCustomPolySubBuffer(sub, buf, 0, 1);
        memcpy(buf + size, buf, size);
    } else {
        buf = prims;
    }
    e->m64_prims[0] = buf;
    e->m64_prims[1] = buf + size;
    hdr = (u8*)(sub->m10_offsetToPackets + (sptr)sub);
    verts = sub->m0_offsetToVertices + (sptr)sub;
    for (i = 0; i != count; i++) {
        code = hdr[3] & 0x1C;
#if VERSION_JP
        if (!(hdr[2] & 1)) {
            code |= 0x100;
        }
#else
        code |= ((hdr[2] ^ 1) & 1) << 8;
#endif
        switch (code) {
        case 0x0:
            v0 = (SVECTOR*)(H(hdr, 0x8) * 8 + verts);
            v1 = (SVECTOR*)(H(hdr, 0xA) * 8 + verts);
            v2 = (SVECTOR*)(H(hdr, 0xC) * 8 + verts);
            break;
        case 0x10:
            v0 = (SVECTOR*)(H(hdr, 0x10) * 8 + verts);
            v1 = (SVECTOR*)(H(hdr, 0x12) * 8 + verts);
            v2 = (SVECTOR*)(H(hdr, 0x14) * 8 + verts);
            break;
        case 0x18:
            v0 = (SVECTOR*)(H(hdr, 0x14) * 8 + verts);
            v1 = (SVECTOR*)(H(hdr, 0x16) * 8 + verts);
            v2 = (SVECTOR*)(H(hdr, 0x18) * 8 + verts);
            v3 = (SVECTOR*)(H(hdr, 0x1A) * 8 + verts);
            break;
        case 0x8:
            v0 = (SVECTOR*)(H(hdr, 0x8) * 8 + verts);
            v1 = (SVECTOR*)(H(hdr, 0xA) * 8 + verts);
            v2 = (SVECTOR*)(H(hdr, 0xC) * 8 + verts);
            v3 = (SVECTOR*)(H(hdr, 0xE) * 8 + verts);
            break;
        case 0x4:
            v0 = (SVECTOR*)(H(hdr, 0x14) * 8 + verts);
            v1 = (SVECTOR*)(H(hdr, 0x16) * 8 + verts);
            v2 = (SVECTOR*)(H(hdr, 0x18) * 8 + verts);
            break;
        case 0x14:
            v0 = (SVECTOR*)(H(hdr, 0x1C) * 8 + verts);
            v1 = (SVECTOR*)(H(hdr, 0x1E) * 8 + verts);
            v2 = (SVECTOR*)(H(hdr, 0x20) * 8 + verts);
            break;
        case 0xC:
            v0 = (SVECTOR*)(H(hdr, 0x18) * 8 + verts);
            v1 = (SVECTOR*)(H(hdr, 0x1A) * 8 + verts);
            v2 = (SVECTOR*)(H(hdr, 0x1C) * 8 + verts);
            v3 = (SVECTOR*)(H(hdr, 0x1E) * 8 + verts);
            break;
        case 0x1C:
            v0 = (SVECTOR*)(H(hdr, 0x24) * 8 + verts);
            v1 = (SVECTOR*)(H(hdr, 0x26) * 8 + verts);
            v2 = (SVECTOR*)(H(hdr, 0x28) * 8 + verts);
            v3 = (SVECTOR*)(H(hdr, 0x2A) * 8 + verts);
            break;
        case 0x100:
            v0 = (SVECTOR*)(H(hdr, 0xA) * 8 + verts);
            v1 = (SVECTOR*)(H(hdr, 0xC) * 8 + verts);
            v2 = (SVECTOR*)(H(hdr, 0xE) * 8 + verts);
            break;
        case 0x110:
            v0 = (SVECTOR*)(H(hdr, 0x10) * 8 + verts);
            v1 = (SVECTOR*)(H(hdr, 0x16) * 8 + verts);
            v2 = (SVECTOR*)(H(hdr, 0x1A) * 8 + verts);
            break;
        case 0x108:
            v0 = (SVECTOR*)(H(hdr, 0xA) * 8 + verts);
            v1 = (SVECTOR*)(H(hdr, 0xC) * 8 + verts);
            v2 = (SVECTOR*)(H(hdr, 0xE) * 8 + verts);
            v3 = (SVECTOR*)(H(hdr, 0x10) * 8 + verts);
            break;
        case 0x104:
            v0 = (SVECTOR*)(H(hdr, 0x12) * 8 + verts);
            v1 = (SVECTOR*)(H(hdr, 0x14) * 8 + verts);
            v2 = (SVECTOR*)(H(hdr, 0x16) * 8 + verts);
            break;
        case 0x114:
            v0 = (SVECTOR*)(H(hdr, 0x12) * 8 + verts);
            v1 = (SVECTOR*)(H(hdr, 0x16) * 8 + verts);
            v2 = (SVECTOR*)(H(hdr, 0x1A) * 8 + verts);
            break;
        case 0x10C:
            v0 = (SVECTOR*)(H(hdr, 0x16) * 8 + verts);
            v1 = (SVECTOR*)(H(hdr, 0x18) * 8 + verts);
            v2 = (SVECTOR*)(H(hdr, 0x1A) * 8 + verts);
            v3 = (SVECTOR*)(H(hdr, 0x1C) * 8 + verts);
            break;
        case 0x118:
            v0 = (SVECTOR*)(H(hdr, 0x16) * 8 + verts);
            v1 = (SVECTOR*)(H(hdr, 0x1A) * 8 + verts);
            v2 = (SVECTOR*)(H(hdr, 0x1E) * 8 + verts);
            v3 = (SVECTOR*)(H(hdr, 0x22) * 8 + verts);
            break;
        case 0x11C:
            v0 = (SVECTOR*)(H(hdr, 0x16) * 8 + verts);
            v1 = (SVECTOR*)(H(hdr, 0x1A) * 8 + verts);
            v2 = (SVECTOR*)(H(hdr, 0x1E) * 8 + verts);
            v3 = (SVECTOR*)(H(hdr, 0x22) * 8 + verts);
            break;
        }
        if (code & 8) {
            c = *v0;
            c.vx += v1->vx;
            c.vy += v1->vy;
            c.vz += v1->vz;
            c.vx += v2->vx;
            c.vy += v2->vy;
            c.vz += v2->vz;
            c.vx += v3->vx;
            c.vy += v3->vy;
            c.vz += v3->vz;
            c.vx /= 4;
            c.vy /= 4;
            c.vz /= 4;
        } else {
            c = *v0;
            c.vx += v1->vx;
            c.vy += v1->vy;
            c.vz += v1->vz;
            c.vx += v2->vx;
            c.vy += v2->vy;
            c.vz += v2->vz;
            c.vx /= 3;
            c.vy /= 3;
            c.vz /= 3;
            v3 = v2;
        }
        p->m34_vertices[0].vx = v0->vx - c.vx;
        p->m34_vertices[0].vy = v0->vy - c.vy;
        p->m34_vertices[0].vz = v0->vz - c.vz;
        p->m34_vertices[1].vx = v1->vx - c.vx;
        p->m34_vertices[1].vy = v1->vy - c.vy;
        p->m34_vertices[1].vz = v1->vz - c.vz;
        p->m34_vertices[2].vx = v2->vx - c.vx;
        p->m34_vertices[2].vy = v2->vy - c.vy;
        p->m34_vertices[2].vz = v2->vz - c.vz;
        p->m34_vertices[3].vx = v3->vx - c.vx;
        p->m34_vertices[3].vy = v3->vy - c.vy;
        p->m34_vertices[3].vz = v3->vz - c.vz;
        p->m10_position.vx = c.vx << 16;
        p->m10_position.vy = c.vy << 16;
        p->m10_position.vz = c.vz << 16;
        r = (rand() & 0xFF) * speedRand / 256;
        t.vx = speed + r;
        t.vy = 0;
        t.vz = 0;
        computeDirectionAngles(&c, &fxModelShatterOrigin, &rot);
        createRotationMatrix(&rot, &rm);
        ApplyMatrixLV(&rm, &t, &t);
        p->m20_velocity.vx = t.vx;
        p->m20_velocity.vy = t.vy;
        p->m20_velocity.vz = t.vz;
        p->m0_rotation.vx = 0;
        p->m0_rotation.vy = 0;
        p->m0_rotation.vz = 0;
        r = (rand() & 0xFF) * rotRand / 256;
        r -= r / 2;
        p->m8_rotationStep.vx = r;
        r = (rand() & 0xFF) * rotRand / 256;
        r -= r / 2;
        p->m8_rotationStep.vy = r;
        r = (rand() & 0xFF) * rotRand / 256;
        r -= r / 2;
        p->m8_rotationStep.vz = r;
        p->m30_gravity = gravity;
        p++;
        hdr += (hdr[1] + 1) * 4;
    }
}
