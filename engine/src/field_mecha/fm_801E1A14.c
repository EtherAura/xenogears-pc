/*
 * Gear cloth / soft-body parts (sLoadedMechas_114): build, update + render,
 * free. Split out of field_mecha.c by BG6 (round 25): 0x801E1A14-0x801E34BC.
 * Boundary evidence: (1) initMechaCloth is defined with int gravity/scale/
 * numColliders (no sign extension of a2/a3/stack arg 7 at entry) while its
 * caller mechaInitNewMecha passes them sign-extended through a short prototype,
 * which one translation unit cannot express; (2) battle links the same three
 * functions as one block (freeTransitionFadeGrid, playMovieScrollingTextSection, func_800A8A88) in a
 * different place relative to the other gear-library twins.
 * GCC 2.6.3, -O2 -G0, ASPSX 2.34 with divide checks (the field_mecha preset).
 */
#include "common.h"
#include "pc_retail_heap.h"
#include "pc_interpolation.h"
#include "psyq.h"
#include "gte.h"
#include "main/memory.h"
#include "field_mecha/mecha.h"

extern s32 gDepthDivider; /* Noah: gDepthDivider (OT z shift) */

void initMechaCloth(sLoadedMechas_114* obj, u16* data, s32 gravity, s32 scale, s16 x, s16 y, s16 z, s32 numColliders,
                   s16 tpx, s16 tpy, s16 uSpan, s16 vSpan, s16 clutX, s16 clutY, u8 frontR, u8 frontG, u8 frontB,
                   u8 backR, u8 backG, u8 backB);

/* Build a cloth part from its data block: chain roots (scaled into bone
 * space), the vertex chains (weight, gravity, root position), two
 * double-buffered POLY_GT3 per quad of neighbouring chains, and the
 * sphere colliders (zeroed; the caller fills them). Note the error paths
 * clear m14 before freeing it, so they free NULL instead of the roots. */
void initMechaCloth(sLoadedMechas_114* obj, u16* data, s32 gravity, s32 scale, s16 x, s16 y, s16 z, s32 numColliders,
                   s16 tpx, s16 tpy, s16 uSpan, s16 vSpan, s16 clutX, s16 clutY, u8 frontR, u8 frontG, u8 frontB,
                   u8 backR, u8 backG, u8 backB) {
    SVECTOR* roots;
    sMechaClothVertex** chains;
    sMechaClothVertex* vertex;
    sMechaClothVertex* vertexBase;
    sMechaClothTriangle* tri;
    sMechaClothCollider* collider;
    u16* counts;
    u16* weights;
    u8* gravities;
    u16 numLinks;
    s32 i;
    s32 j;
    s32 k;
    s32 numSegs;
    s32 tpX;
    s32 tpY;
    s32 base;
    u16 tpage;
    u16 clut;
    u16 u;
    u16 v;
    s16 uStep;
    s16 vStep;

    obj->m4_numChains = *data++;
    obj->m6_numTriangles = *data++ * 2;
    resetMemoryAllocStats(4, 0);
    roots = allocateHeap(PC_HEAP_SIZED(obj->m4_numChains * sizeof(SVECTOR), obj->m4_numChains * 0x8), 0);
    if (roots == NULL) {
        obj->m14_vertices = NULL;
        return;
    }
    obj->m14_vertices = roots;
    for (i = 0; i < obj->m4_numChains; i++) {
        roots->vx = ((*data++ + x) * scale) / 4096;
        roots->vy = ((*data++ + y) * scale) / 4096;
        roots->vz = ((*data++ + z) * scale) / 4096;
        roots++;
    }
    numLinks = data[obj->m4_numChains];
    obj->m8_numVertices = numLinks + obj->m4_numChains;
    chains = allocateHeap(PC_HEAP_SIZED(obj->m4_numChains * sizeof(sMechaClothVertex*), obj->m4_numChains * 0x4), 0);
    if (chains == NULL) {
        obj->m14_vertices = NULL;
        freeHeap(obj->m14_vertices);
        return;
    }
    counts = data;
    obj->m1C_chains = chains;
    weights = counts + obj->m4_numChains + 1;
    gravities = (u8*)&weights[numLinks];
    vertex = allocateHeap(PC_HEAP_SIZED((numLinks + obj->m4_numChains) * sizeof(sMechaClothVertex), (numLinks + obj->m4_numChains) * 0x18), 0);
    if (vertex == NULL) {
        obj->m14_vertices = NULL;
        freeHeap(obj->m14_vertices);
        freeHeap(obj->m1C_chains);
        return;
    }
    vertexBase = vertex;
    roots = obj->m14_vertices;
    for (i = 0; i < obj->m4_numChains; i++) {
        *chains = vertex;
        for (j = 0; j < *counts; j++) {
            vertex->m0_weight = (*weights++ * scale) / 4096;
            vertex->m2_gravity = *gravities++ + gravity;
            vertex->m4_position.vx = roots->vx;
            vertex->m4_position.vy = roots->vy;
            vertex->m4_position.vz = roots->vz;
            vertex++;
        }
        vertex->m0_weight = 0;
        vertex->m2_gravity = 0;
        vertex->m4_position.vx = roots->vx;
        vertex->m4_position.vy = roots->vy;
        vertex->m4_position.vz = roots->vz;
        vertex++;
        chains++;
        counts++;
        roots++;
    }
    counts = data;
    tri = allocateHeap(PC_HEAP_SIZED(obj->m6_numTriangles * sizeof(sMechaClothTriangle), obj->m6_numTriangles * 0x58), 0);
    if (tri == NULL) {
        obj->m14_vertices = NULL;
        freeHeap(obj->m14_vertices);
        freeHeap(obj->m1C_chains);
        freeHeap(vertexBase);
        return;
    }
    obj->m20_triangles = tri;
    tpX = (tpx / 64) << 6;
    tpY = (tpy / 256) << 8;
    tpage = GetTPage(0, 1, (s16)tpX, (s16)tpY);
    clut = GetClut(clutX, clutY);
    u = (tpx - (s16)tpX) * 4;
    v = tpy - tpY;
    base = 0;
    uStep = uSpan / (obj->m4_numChains - 1);
    for (i = 0; i < obj->m4_numChains - 1; i++) {
        numSegs = counts[0] < counts[1] ? counts[0] : counts[1];
        vStep = vSpan / numSegs;
        for (j = 0; j < numSegs; j++) {
            tri->m0_vertex[0] = base + j;
            tri->m0_vertex[1] = base + j + counts[0] + 1;
            tri->m0_vertex[2] = base + j + 1;
            for (k = 0; k < 2; k++) {
                SetPolyGT3(&tri->m8_poly[k]);
                tri->m8_poly[k].tpage = tpage;
                tri->m8_poly[k].clut = clut;
                tri->m8_poly[k].u0 = u + i * uStep;
                tri->m8_poly[k].v0 = v + vStep * j;
                tri->m8_poly[k].u1 = u + (i + 1) * uStep;
                tri->m8_poly[k].v1 = v + vStep * j;
                tri->m8_poly[k].u2 = u + i * uStep;
                tri->m8_poly[k].v2 = v + vStep * (j + 1);
            }
            tri++;
            tri->m0_vertex[0] = base + j + counts[0] + 1;
            tri->m0_vertex[1] = base + j + counts[0] + 2;
            tri->m0_vertex[2] = base + j + 1;
            for (k = 0; k < 2; k++) {
                SetPolyGT3(&tri->m8_poly[k]);
                tri->m8_poly[k].tpage = tpage;
                tri->m8_poly[k].clut = clut;
                tri->m8_poly[k].u0 = u + (i + 1) * uStep;
                tri->m8_poly[k].v0 = v + vStep * j;
                tri->m8_poly[k].u1 = u + (i + 1) * uStep;
                tri->m8_poly[k].v1 = v + vStep * (j + 1);
                tri->m8_poly[k].u2 = u + i * uStep;
                tri->m8_poly[k].v2 = v + vStep * (j + 1);
            }
            tri++;
        }
        base += *counts++ + 1;
    }
    obj->mC_frontColor[0] = frontR;
    obj->mC_frontColor[1] = frontG;
    obj->mC_frontColor[2] = frontB;
    obj->mF_backColor[0] = backR;
    obj->mF_backColor[1] = backG;
    obj->mF_backColor[2] = backB;
    obj->mA_numColliders = numColliders;
    if ((s16)numColliders > 0) {
        collider = allocateHeap(PC_HEAP_SIZED((s16)numColliders * sizeof(sMechaClothCollider), (s16)numColliders * 0x10), 0);
        if (collider == NULL) {
            obj->mA_numColliders = 0;
        }
        obj->m18_colliders = collider;
        for (i = 0; i < obj->mA_numColliders; i++) {
            collider->m0_pos.vx = 0;
            collider->m0_pos.vy = 0;
            collider->m0_pos.vz = 0;
            collider->m0_pos.pad = 0;
            collider->m8_x = 0;
            collider->mA_y = 0;
            collider->mC_z = 0;
            collider->mE_radius = 0;
            collider++;
        }
    } else {
        obj->m18_colliders = NULL;
    }
}

/* Cloth / soft-body part update + render (sLoadedMechas_114; Noah has no
 * code for it). Note: tri only advances when the triangle is not clipped
 * (retail behaviour). */
void updateAndRenderMechaCloth(sLoadedMechas_114* arg0, SVECTOR* arg1, MATRIX* arg2, OTag* ot, s32 oddOrEven, s32 scale,
                   s16 floorY) {
    VECTOR v;
    SVECTOR n;
    VECTOR a;
    VECTOR b;
    VECTOR c;
    SVECTOR unused;
    CVECTOR col;
    s32 flag;
    s32 p;
    s32 otz;
    s32 i;
    s32 len;
    s32 r;
    sMechaClothVertex* e;
    sMechaClothCollider* coll;
    sMechaClothTriangle* tri;

    if (arg0->m14_vertices == NULL) {
        return;
    }
    col.cd = arg0->m20_triangles->m8_poly[0].code;
    for (i = 0; i < arg0->m4_numChains; i++) {
        for (e = arg0->m1C_chains[i]; e->m0_weight != 0; e++) {
            v.vx = e[1].m4_position.vx - e->m4_position.vx + arg1->vx;
            v.vy = e[1].m4_position.vy - e->m4_position.vy + arg1->vy + e->m2_gravity;
            v.vz = e[1].m4_position.vz - e->m4_position.vz + arg1->vz;
            len = SquareRoot0(v.vx * v.vx + v.vy * v.vy + v.vz * v.vz);
            if (len != 0) {
                v.vx = (v.vx << 8) / len;
                v.vy = (v.vy << 8) / len;
                v.vz = (v.vz << 8) / len;
            } else {
                v.vx = 0;
                v.vy = 0;
                v.vz = 0;
            }
            e[1].m4_position.vx = e->m4_position.vx + e->m0_weight * v.vx * scale / 0x100000;
            e[1].m4_position.vy = e->m4_position.vy + e->m0_weight * v.vy * scale / 0x100000;
            e[1].m4_position.vz = e->m4_position.vz + e->m0_weight * v.vz * scale / 0x100000;
            if (e[1].m4_position.vy > floorY) {
                e[1].m4_position.vy = floorY;
            }
            coll = arg0->m18_colliders;
            for (flag = 0; flag < arg0->mA_numColliders; flag++, coll++) {
                v.vx = e[1].m4_position.vx - coll->m8_x;
                v.vy = e[1].m4_position.vy - coll->mA_y;
                v.vz = e[1].m4_position.vz - coll->mC_z;
                len = SquareRoot0(v.vx * v.vx + v.vy * v.vy + v.vz * v.vz);
                r = (s16)(coll->mE_radius * scale / 0x1000);
                if (len < r) {
                    if (len != 0) {
                        v.vx = coll->m8_x + r * v.vx / len;
                        v.vy = coll->mA_y + r * v.vy / len;
                        v.vz = coll->mC_z + r * v.vz / len;
                    } else {
                        v.vx = coll->m8_x;
                        v.vy = coll->mA_y;
                        v.vz = coll->mC_z;
                    }
                    v.vx = v.vx - e->m4_position.vx;
                    v.vy = v.vy - e->m4_position.vy;
                    v.vz = v.vz - e->m4_position.vz;
                    len = SquareRoot0(v.vx * v.vx + v.vy * v.vy + v.vz * v.vz);
                    if (len != 0) {
                        v.vx = (v.vx << 8) / len;
                        v.vy = (v.vy << 8) / len;
                        v.vz = (v.vz << 8) / len;
                    } else {
                        v.vx = 0;
                        v.vy = 0;
                        v.vz = 0;
                    }
                    e[1].m4_position.vx = e->m4_position.vx + e->m0_weight * v.vx * scale / 0x100000;
                    e[1].m4_position.vy = e->m4_position.vy + e->m0_weight * v.vy * scale / 0x100000;
                    e[1].m4_position.vz = e->m4_position.vz + e->m0_weight * v.vz * scale / 0x100000;
                }
            }
        }
    }

    e = arg0->m1C_chains[0];
    for (i = 0; i < arg0->m8_numVertices; e++, i++) {
        e->m4_position.pad = 0;
        e->mC_normal[0] = 0;
        e->mC_normal[1] = 0;
        e->mC_normal[2] = 0;
    }

    tri = arg0->m20_triangles;
    e = arg0->m1C_chains[0];
    for (i = 0; i < arg0->m6_numTriangles; i++, tri++) {
        a.vx = e[tri->m0_vertex[0]].m4_position.vx - e[tri->m0_vertex[1]].m4_position.vx;
        a.vy = e[tri->m0_vertex[0]].m4_position.vy - e[tri->m0_vertex[1]].m4_position.vy;
        a.vz = e[tri->m0_vertex[0]].m4_position.vz - e[tri->m0_vertex[1]].m4_position.vz;
        b.vx = e[tri->m0_vertex[0]].m4_position.vx - e[tri->m0_vertex[2]].m4_position.vx;
        b.vy = e[tri->m0_vertex[0]].m4_position.vy - e[tri->m0_vertex[2]].m4_position.vy;
        b.vz = e[tri->m0_vertex[0]].m4_position.vz - e[tri->m0_vertex[2]].m4_position.vz;
        gte_OuterProduct0(&a, &b, &c);
        c.vx /= 8;
        c.vy /= 8;
        c.vz /= 8;
        VectorNormal(&c, &a);
        e[tri->m0_vertex[0]].mC_normal[0] += a.vx;
        e[tri->m0_vertex[0]].mC_normal[1] += a.vy;
        e[tri->m0_vertex[0]].mC_normal[2] += a.vz;
        e[tri->m0_vertex[0]].m4_position.pad++;
        e[tri->m0_vertex[1]].mC_normal[0] += a.vx;
        e[tri->m0_vertex[1]].mC_normal[1] += a.vy;
        e[tri->m0_vertex[1]].mC_normal[2] += a.vz;
        e[tri->m0_vertex[1]].m4_position.pad++;
        e[tri->m0_vertex[2]].mC_normal[0] += a.vx;
        e[tri->m0_vertex[2]].mC_normal[1] += a.vy;
        e[tri->m0_vertex[2]].mC_normal[2] += a.vz;
        e[tri->m0_vertex[2]].m4_position.pad++;
    }

    e = arg0->m1C_chains[0];
    for (i = 0; i < arg0->m8_numVertices; e++, i++) {
        e->mC_normal[0] /= e->m4_position.pad;
        e->mC_normal[1] /= e->m4_position.pad;
        e->mC_normal[2] /= e->m4_position.pad;
    }

    SetRotMatrix(arg2);
    SetTransMatrix(arg2);
    tri = arg0->m20_triangles;
    e = arg0->m1C_chains[0];
    for (i = 0; i < arg0->m6_numTriangles; i++) {
        gte_ldv3(&e[tri->m0_vertex[0]].m4_position, &e[tri->m0_vertex[1]].m4_position,
                 &e[tri->m0_vertex[2]].m4_position);
        gte_rtpt();
        flag = 0;
        gte_stflg(&flag);
        if (!(flag & 0x40000)) {
            gte_nclip();
            gte_stopz(&p);
            gte_stsxy3(&tri->m8_poly[oddOrEven].x0, &tri->m8_poly[oddOrEven].x1, &tri->m8_poly[oddOrEven].x2);
            gte_avsz3();
            gte_stotz(&otz);
            otz >>= gDepthDivider;
            if (p < 0) {
                col.r = arg0->mC_frontColor[0];
                col.g = arg0->mC_frontColor[1];
                col.b = arg0->mC_frontColor[2];
                n.vx = e[tri->m0_vertex[0]].mC_normal[0];
                n.vy = e[tri->m0_vertex[0]].mC_normal[1];
                n.vz = e[tri->m0_vertex[0]].mC_normal[2];
            } else {
                col.r = arg0->mF_backColor[0];
                col.g = arg0->mF_backColor[1];
                col.b = arg0->mF_backColor[2];
                n.vx = -e[tri->m0_vertex[0]].mC_normal[0];
                n.vy = -e[tri->m0_vertex[0]].mC_normal[1];
                n.vz = -e[tri->m0_vertex[0]].mC_normal[2];
            }
            gte_ldv0(&n);
            gte_ldrgb(&col);
            gte_nccs();
            gte_strgb(&tri->m8_poly[oddOrEven].r0);
            if (p < 0) {
                n.vx = e[tri->m0_vertex[1]].mC_normal[0];
                n.vy = e[tri->m0_vertex[1]].mC_normal[1];
                n.vz = e[tri->m0_vertex[1]].mC_normal[2];
            } else {
                n.vx = -e[tri->m0_vertex[1]].mC_normal[0];
                n.vy = -e[tri->m0_vertex[1]].mC_normal[1];
                n.vz = -e[tri->m0_vertex[1]].mC_normal[2];
            }
            gte_ldv0(&n);
            gte_nccs();
            gte_strgb(&tri->m8_poly[oddOrEven].r1);
            if (p < 0) {
                n.vx = e[tri->m0_vertex[2]].mC_normal[0];
                n.vy = e[tri->m0_vertex[2]].mC_normal[1];
                n.vz = e[tri->m0_vertex[2]].mC_normal[2];
            } else {
                n.vx = -e[tri->m0_vertex[2]].mC_normal[0];
                n.vy = -e[tri->m0_vertex[2]].mC_normal[1];
                n.vz = -e[tri->m0_vertex[2]].mC_normal[2];
            }
            gte_ldv0(&n);
            gte_nccs();
            gte_strgb(&tri->m8_poly[oddOrEven].r2);
            pcInterpolationObjectType(arg0, arg0->m20_triangles, PC_INTERP_MECHA);
            pcInterpolationModel(&arg0->m20_triangles->m8_poly[oddOrEven]);
            pcInterpolationPrim(&tri->m8_poly[oddOrEven]);
            pcInterpolationObject(0, 0);
            addPrim(&ot[otz], &tri->m8_poly[oddOrEven]);
            tri++;
        }
    }
}

/* Noah: sLoadedMechas_114 cleanup */
void freeMechaCloth(sLoadedMechas_114* arg0) {
    if (arg0->m14_vertices != NULL) {
        freeHeap(arg0->m14_vertices);
        freeHeap(*arg0->m1C_chains);
        freeHeap(arg0->m1C_chains);
        freeHeap(arg0->m20_triangles);
        if (arg0->m18_colliders != NULL) {
            freeHeap(arg0->m18_colliders);
        }
        arg0->m14_vertices = NULL;
    }
}
