/*
 * The model viewer's glTF export (models.h xgSceneExportGlb): the scene as
 * the game sets it up, written with our own GLB writer (writers.h).
 *
 * What goes in:
 *   - Nodes: one top node turning the game's axes into glTF's (the PS1's y
 *     points down and its z away from the viewer: a half turn about x) and
 *     scaling model units (XgExportOptions.unit); under it each skeleton's
 *     root (bone 0: where the model stands, its scale), then a node per bone
 *     with its translation and rotation from its parent, as the Gear
 *     overlay's matrices have them (rigid parts, no skinning). A model
 *     without a skeleton gets a node per part.
 *   - Meshes: a mesh per model block, a primitive per material. Positions
 *     are the block's vertices, the faces its mesh blocks' (quads split
 *     0-1-2, 1-3-2 as the GPU does), texture coordinates, pages and CLUTs
 *     from the primitives initModel2 set up (texel centres, / 256).
 *   - Materials: unlit (KHR_materials_unlit), double sided, the texture
 *     page decoded through its CLUT from the sandbox's VRAM as a 256 x 256
 *     PNG with nearest filtering; texel 0 clear (alpha mask).
 *     Semi-transparency is approximated (glTF only blends): a semi-
 *     transparent polygon's material blends, its STP texels at alpha 0.5
 *     (0.25 for the B + F/4 mode); additive and subtractive modes become the
 *     same 50% blend. Untextured semi-transparent polygons: vertex alpha 0.5.
 *   - Colours (COLOR_0): a model without a skeleton keeps the colours its
 *     set-up gave its primitives (the archive's own, or lit once); a
 *     skeleton's parts, which the game lights as they move, get white on
 *     textured polygons and the archive's colour on the others.
 *   - Animations: each program played from a fresh build, every live rig's
 *     translation, rotation and scale sampled at every game tick
 *     (XgExportOptions.tickRate). Translation/rotation interpolate linearly;
 *     scale and visibility hold until the next tick. Mesh children carry
 *     bone visibility without hiding enabled descendants. Spawned battle
 *     slots have distinct lifetimes and are isolated between programs.
 *   - MIMe: original vertices, a position delta target decoded by main/mime.c
 *     for each embedded target, and current weights / 4096 as mesh defaults.
 *     Requested target clips sample the native out-and-back weight transition
 *     each tick (STEP). glTF blends positions in floating point rather than
 *     rounding each native 4.12 offset to an integer.
 * All work runs in an owned copy of the loading steps and sandbox.
 */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common.h"
#include "main/model.h"
#include "models.h"
#include "psyq.h"
#include "writers.h"

/* main's model code (model.c polyRenderDefs): sizes per mesh block family. */
typedef struct {
    void* render[6];
    void* init;
    s32 indexSize, displayListSize, primSize;
} PolyDef;
extern PolyDef polyRenderDefs[17];

#define MAX_PARTS 8192
#define MAX_BONES 256
#define MAX_MATERIALS 256
#define MAX_MORPHS 512

typedef struct {
    int count, ids[MAX_MORPHS], node;
} MorphPart;

/* ---- primitives ------------------------------------------------------------------------------ */

typedef struct {
    int textured, semi, raw, quad, gouraud, n;
    uint16_t tpage, clut;
    uint8_t u[4], v[4];
    uint8_t r[4], g[4], b[4];
} Packet;

static int readPacket(const u8* prim, Packet* p) {
    const u32* w = (const u32*)(prim + PRIM_OFS(4));
    uint8_t code = (uint8_t)(w[0] >> 24);
    memset(p, 0, sizeof(*p));
    if ((code & 0xE0) != 0x20) {
        return 0;
    }
    p->gouraud = (code & 0x10) != 0;
    p->quad = (code & 0x08) != 0;
    p->textured = (code & 0x04) != 0;
    p->semi = (code & 0x02) != 0;
    p->raw = (code & 0x01) != 0;
    p->n = p->quad ? 4 : 3;
    int at = 0;
    for (int k = 0; k < p->n; k++) {
        u32 c = k == 0 || p->gouraud ? w[at++] : w[0];
        p->r[k] = (uint8_t)c;
        p->g[k] = (uint8_t)(c >> 8);
        p->b[k] = (uint8_t)(c >> 16);
        at++;
        if (p->textured) {
            u32 t = w[at++];
            p->u[k] = (uint8_t)t;
            p->v[k] = (uint8_t)(t >> 8);
            if (k == 0) {
                p->clut = (uint16_t)(t >> 16);
            } else if (k == 1) {
                p->tpage = (uint16_t)(t >> 16);
            }
        }
    }
    return 1;
}

/* Families whose set-up reads the attribute stream's page and CLUT words
 * (model.c: the textured ones go through primD_isValid). */
static int takesControlWords(int type) {
    return type < 16 && (type & 1);
}

/* ---- materials ------------------------------------------------------------------------------- */

typedef struct {
    int textured;
    uint16_t tpage, clut; /* tpage: page and depth; blend bits only when semi */
    int semi;
    int index; /* glTF material */
} Material;

typedef struct {
    float* pos;
    float* uv;
    float* col;
    uint32_t* idx;
    int* source; /* original vertex for each duplicated triangle corner */
    int verts, cap;
} Batch;

typedef struct {
    XgScene* scene;
    XgGltf* g;
    Material mats[MAX_MATERIALS];
    int matCount;
    char why[128];
    void** meshBlocks;
    int* meshes;
    int meshCount, meshCap;
} Ctx;

static float semiAlpha(uint16_t tpage) {
    return (tpage >> 5 & 3) == 3 ? 0.25f : 0.5f;
}

static int material(Ctx* c, const Packet* p) {
    Material key;
    memset(&key, 0, sizeof(key));
    key.textured = p->textured;
    key.semi = p->semi;
    if (p->textured) {
        key.tpage = (uint16_t)(p->tpage & (p->semi ? 0x1FF : 0x19F));
        key.clut = (p->tpage >> 7 & 3) == 2 ? 0 : p->clut;
    }
    for (int i = 0; i < c->matCount; i++) {
        Material* m = &c->mats[i];
        if (m->textured == key.textured && m->semi == key.semi && m->tpage == key.tpage && m->clut == key.clut) {
            return m->index;
        }
    }
    if (c->matCount >= MAX_MATERIALS) {
        snprintf(c->why, sizeof(c->why), "too many materials");
        return -1;
    }
    char name[64];
    if (key.textured) {
        int depth = key.tpage >> 7 & 3;
        snprintf(name, sizeof(name), "page%d_%d_%dbit_clut%d_%d%s", (key.tpage & 15) * 64, (key.tpage >> 4 & 1) * 256,
                 depth == 0 ? 4 : depth == 1 ? 8 : 15, (key.clut & 63) * 16, key.clut >> 6 & 511,
                 key.semi ? "_semi" : "");
        uint32_t* rgba = malloc(256 * 256 * 4);
        if (rgba == NULL) {
            snprintf(c->why, sizeof(c->why), "out of memory");
            return -1;
        }
        /* Semi-transparent: its STP texels (the bake's alpha 128) blend. */
        xgSceneBakePage(c->scene, key.tpage, key.clut, key.semi, rgba);
        if (key.semi) {
            uint32_t a = (uint32_t)lrintf(semiAlpha(key.tpage) * 255);
            for (int i = 0; i < 256 * 256; i++) {
                if (rgba[i] >> 24 == 128) {
                    rgba[i] = (rgba[i] & 0xFFFFFF) | a << 24;
                }
            }
        }
        size_t pngSize = 0;
        uint8_t* png = xgPngEncode(rgba, 256, 256, &pngSize);
        free(rgba);
        int index = png != NULL ? xgGltfAddTexturedMaterial(c->g, name, png, pngSize, key.semi) : -1;
        free(png);
        key.index = index;
    } else {
        snprintf(name, sizeof(name), "colour%s", key.semi ? "_semi" : "");
        key.index = xgGltfAddColourMaterial(c->g, name);
        if (key.semi) {
            xgGltfSetBlend(c->g, key.index, 1);
        }
    }
    c->mats[c->matCount++] = key;
    return key.index;
}

static int batchVertex(Batch* b, const float* pos, int source, float u, float v, const float* col) {
    if (b->verts == b->cap) {
        int cap = b->cap ? b->cap * 2 : 256;
        float* p = realloc(b->pos, sizeof(float) * 3 * (size_t)cap);
        float* q = p != NULL ? realloc(b->uv, sizeof(float) * 2 * (size_t)cap) : NULL;
        float* r = q != NULL ? realloc(b->col, sizeof(float) * 4 * (size_t)cap) : NULL;
        uint32_t* s = r != NULL ? realloc(b->idx, sizeof(uint32_t) * (size_t)cap) : NULL;
        int* vtx = s != NULL ? realloc(b->source, sizeof(int) * (size_t)cap) : NULL;
        if (p != NULL) {
            b->pos = p;
        }
        if (q != NULL) {
            b->uv = q;
        }
        if (r != NULL) {
            b->col = r;
        }
        if (s != NULL) {
            b->idx = s;
        }
        if (vtx != NULL) b->source = vtx;
        if (vtx == NULL) {
            return 0;
        }
        b->cap = cap;
    }
    memcpy(b->pos + 3 * b->verts, pos, sizeof(float) * 3);
    b->uv[2 * b->verts] = u;
    b->uv[2 * b->verts + 1] = v;
    memcpy(b->col + 4 * b->verts, col, sizeof(float) * 4);
    b->idx[b->verts] = (uint32_t)b->verts;
    b->source[b->verts] = source;
    b->verts++;
    return 1;
}

/* A model block as one mesh: a primitive per material. Inside the sandbox
 * (the block's pointers are its heap's). */
static int blockMesh(Ctx* c, const XgScenePart* part, const char* name, const MorphPart* morph) {
    ModelBlock* blk = part->block;
    float* base = morph->count > 0 ? malloc(sizeof(float) * 3 * blk->m2_numVertices) : NULL;
    if (morph->count > 0 && (base == NULL || !xgScenePartMorphPositions(c->scene, blk, -1, base, blk->m2_numVertices))) {
        free(base);
        snprintf(c->why, sizeof(c->why), "cannot copy MIMe base vertices");
        return -1;
    }
    Batch batches[MAX_MATERIALS];
    int batchMat[MAX_MATERIALS];
    int batchCount = 0;
    memset(batches, 0, sizeof(batches));
    u8* mb = blk->m10_meshBlocks;
    u8* prim = part->prims;
    u8* dl = blk->m14_displayList;
    for (int g = 0; g < blk->m6_numMeshBlocks; g++) {
        int type = mb[0], polys = *(u16*)(mb + 2);
        if (type > 16) {
            break;
        }
        u16* idx = (u16*)(mb + 4);
        for (int k = 0; k < polys; k++, prim += polyRenderDefs[type].primSize, idx += 4) {
            /* The attribute stream as initModel2 walks it. */
            while (takesControlWords(type) && dl != NULL && (dl[3] == 0xC4 || dl[3] == 0xC8)) {
                dl += 4;
            }
            u8* attr = dl;
            if (dl != NULL) {
                dl += polyRenderDefs[type].displayListSize;
            }
            Packet p;
            if (!readPacket(prim, &p)) {
                continue;
            }
            int mat = material(c, &p);
            if (mat < 0) {
                snprintf(c->why, sizeof(c->why), "cannot create material: %s", xgWriterError());
                continue;
            }
            int b;
            for (b = 0; b < batchCount && batchMat[b] != mat; b++) {
            }
            if (b == batchCount) {
                if (batchCount == MAX_MATERIALS) {
                    continue;
                }
                batchMat[batchCount++] = mat;
            }
            static const int tri[2][3] = {{0, 1, 2}, {1, 3, 2}};
            for (int t = 0; t < (p.quad ? 2 : 1); t++) {
                for (int corner = 0; corner < 3; corner++) {
                    int vi = tri[t][corner];
                    int vert = idx[vi];
                    if (vert >= blk->m2_numVertices) {
                        vert = 0;
                    }
                    SVECTOR* sv = &blk->m8_vertices[vert];
                    float pos[3] = {(float)sv->vx, (float)sv->vy, (float)sv->vz};
                    if (base != NULL) memcpy(pos, base + 3 * vert, sizeof(pos));
                    float col[4];
                    int gi = p.gouraud ? vi : 0;
                    if (!part->mecha) {
                        float scale = p.textured ? 1.0f / 128 : 1.0f / 255;
                        col[0] = fminf(p.r[gi] * scale, 1.0f);
                        col[1] = fminf(p.g[gi] * scale, 1.0f);
                        col[2] = fminf(p.b[gi] * scale, 1.0f);
                        if (p.textured && p.raw) {
                            col[0] = col[1] = col[2] = 1;
                        }
                    } else if (p.textured || attr == NULL) {
                        col[0] = col[1] = col[2] = 1;
                    } else {
                        col[0] = attr[0] / 255.0f;
                        col[1] = attr[1] / 255.0f;
                        col[2] = attr[2] / 255.0f;
                    }
                    col[3] = !p.textured && p.semi ? 0.5f : 1.0f;
                    if (!batchVertex(&batches[b], pos, vert, (p.u[vi] + 0.5f) / 256.0f, (p.v[vi] + 0.5f) / 256.0f, col)) {
                        snprintf(c->why, sizeof(c->why), "out of memory");
                    }
                }
            }
        }
        mb += 4 + polys * polyRenderDefs[type].indexSize;
    }
    int mesh = batchCount > 0 ? xgGltfAddEmptyMesh(c->g, name) : -1;
    if (batchCount > 0 && mesh < 0) {
        snprintf(c->why, sizeof(c->why), "%s", xgWriterError());
    }
    for (int b = 0; b < batchCount; b++) {
        if (mesh >= 0) {
            int textured = 0;
            for (int i = 0; i < c->matCount; i++) {
                if (c->mats[i].index == batchMat[b]) {
                    textured = c->mats[i].textured;
                }
            }
            Batch* batch = &batches[b];
            int primitive = xgGltfAddPrimitive(c->g, mesh, batchMat[b], batch->pos, textured ? batch->uv : NULL,
                                   batch->col, batch->verts, batch->idx, batch->verts);
            if (primitive < 0) {
                snprintf(c->why, sizeof(c->why), "%s", xgWriterError());
            }
            if (primitive >= 0 && morph->count > 0) {
                float* full = malloc(sizeof(float) * 3 * blk->m2_numVertices);
                float* delta = malloc(sizeof(float) * 3 * (size_t)batch->verts);
                for (int target = 0; target < morph->count; target++) {
                    if (full == NULL || delta == NULL ||
                        !xgScenePartMorphPositions(c->scene, blk, target, full, blk->m2_numVertices)) {
                        snprintf(c->why, sizeof(c->why), "cannot copy MIMe target vertices");
                        break;
                    }
                    for (int i = 0; i < batch->verts; i++) {
                        for (int k = 0; k < 3; k++) delta[3 * i + k] = full[3 * batch->source[i] + k] - batch->pos[3 * i + k];
                    }
                    if (xgGltfAddMorphTarget(c->g, mesh, primitive, delta, batch->verts) < 0) {
                        snprintf(c->why, sizeof(c->why), "cannot write MIMe target");
                        break;
                    }
                }
                free(full);
                free(delta);
            }
        }
        free(batches[b].pos);
        free(batches[b].uv);
        free(batches[b].col);
        free(batches[b].idx);
        free(batches[b].source);
    }
    if (mesh >= 0 && morph->count > 0) {
        float weights[MAX_MORPHS];
        for (int i = 0; i < morph->count; i++) weights[i] = xgSceneMorphWeight(c->scene, morph->ids[i]) / 4096.0f;
        if (!xgGltfSetMorphWeights(c->g, mesh, weights, morph->count)) {
            snprintf(c->why, sizeof(c->why), "cannot set MIMe weights");
        }
    }
    free(base);
    return mesh;
}

/* ---- transforms ------------------------------------------------------------------------------ */

/* Rotation and scale of rows m (the GTE's: out = m . v): scale per column,
 * the rotation as a unit quaternion (x, y, z, w). */
static void decompose(const float m[3][3], float q[4], float s[3]) {
    float r[3][3];
    for (int j = 0; j < 3; j++) {
        s[j] = sqrtf(m[0][j] * m[0][j] + m[1][j] * m[1][j] + m[2][j] * m[2][j]);
        for (int i = 0; i < 3; i++) {
            r[i][j] = s[j] > 1e-6f ? m[i][j] / s[j] : (i == j ? 1.0f : 0.0f);
        }
    }
    float det = r[0][0] * (r[1][1] * r[2][2] - r[1][2] * r[2][1]) - r[0][1] * (r[1][0] * r[2][2] - r[1][2] * r[2][0]) +
                r[0][2] * (r[1][0] * r[2][1] - r[1][1] * r[2][0]);
    if (det < 0) {
        s[0] = -s[0];
        for (int i = 0; i < 3; i++) {
            r[i][0] = -r[i][0];
        }
    }
    float tr = r[0][0] + r[1][1] + r[2][2];
    if (tr > 0) {
        float k = sqrtf(tr + 1.0f) * 2;
        q[3] = 0.25f * k;
        q[0] = (r[2][1] - r[1][2]) / k;
        q[1] = (r[0][2] - r[2][0]) / k;
        q[2] = (r[1][0] - r[0][1]) / k;
    } else if (r[0][0] > r[1][1] && r[0][0] > r[2][2]) {
        float k = sqrtf(1.0f + r[0][0] - r[1][1] - r[2][2]) * 2;
        q[3] = (r[2][1] - r[1][2]) / k;
        q[0] = 0.25f * k;
        q[1] = (r[0][1] + r[1][0]) / k;
        q[2] = (r[0][2] + r[2][0]) / k;
    } else if (r[1][1] > r[2][2]) {
        float k = sqrtf(1.0f + r[1][1] - r[0][0] - r[2][2]) * 2;
        q[3] = (r[0][2] - r[2][0]) / k;
        q[0] = (r[0][1] + r[1][0]) / k;
        q[1] = 0.25f * k;
        q[2] = (r[1][2] + r[2][1]) / k;
    } else {
        float k = sqrtf(1.0f + r[2][2] - r[0][0] - r[1][1]) * 2;
        q[3] = (r[1][0] - r[0][1]) / k;
        q[0] = (r[0][2] + r[2][0]) / k;
        q[1] = (r[1][2] + r[2][1]) / k;
        q[2] = 0.25f * k;
    }
    float len = sqrtf(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);
    for (int i = 0; i < 4; i++) {
        q[i] = len > 0 ? q[i] / len : (i == 3 ? 1.0f : 0.0f);
    }
}

typedef struct {
    float t[3], r[4], s[3];
} Trs;

static Trs boneTrs(const XgSceneBone* b) {
    Trs x;
    decompose(b->m, x.r, x.s);
    memcpy(x.t, b->t, sizeof(x.t));
    if (!b->enabled) {
        x.s[0] = x.s[1] = x.s[2] = 0;
    }
    return x;
}

/* ---- the export ------------------------------------------------------------------------------ */

static int cachedMesh(Ctx* c, const XgScenePart* part, const MorphPart* morph) {
    for (int i = 0; i < c->meshCount; i++) if (c->meshBlocks[i] == part->block) return c->meshes[i];
    if (c->meshCount == c->meshCap) {
        int cap = c->meshCap ? c->meshCap * 2 : 64;
        void** blocks = realloc(c->meshBlocks, sizeof(void*) * (size_t)cap);
        if (blocks == NULL) { snprintf(c->why, sizeof(c->why), "out of memory"); return -1; }
        c->meshBlocks = blocks;
        int* meshes = realloc(c->meshes, sizeof(int) * (size_t)cap);
        if (meshes == NULL) { snprintf(c->why, sizeof(c->why), "out of memory"); return -1; }
        c->meshes = meshes;
        c->meshCap = cap;
    }
    char name[32];
    snprintf(name, sizeof(name), "mesh%d", c->meshCount);
    int mesh = blockMesh(c, part, name, morph);
    c->meshBlocks[c->meshCount] = part->block;
    c->meshes[c->meshCount++] = mesh;
    return mesh;
}

typedef struct { Trs pose; float visible; } RigSample;
typedef struct {
    XgSceneRig id;
    int nodes[MAX_BONES], geometry[MAX_BONES];
    RigSample rest[MAX_BONES];
} ExportRig;
typedef struct { ExportRig* rig; RigSample* samples; int owned; } ClipRig;

static int sameRig(const XgSceneRig* a, const XgSceneRig* b) {
    return a->slot == b->slot && a->generation == b->generation;
}

/* Mesh visibility is independent of the bone transform: a disabled parent
 * may still have enabled children, especially in a detached subtree. */
static RigSample rigSample(const XgSceneBone* b, int index) {
    XgSceneBone visible = *b;
    visible.enabled = 1;
    RigSample sample = {boneTrs(&visible), b->enabled != 0};
    if (index == 0 && !b->enabled) memset(sample.pose.s, 0, sizeof(sample.pose.s));
    return sample;
}

static ExportRig* addRig(Ctx* c, const XgSceneRig* id, int parent, int program, int primary,
                         XgScenePart* parts, int partCount) {
    if (id->bones <= 0 || id->bones > MAX_BONES) {
        snprintf(c->why, sizeof(c->why), "rig %d has too many bones", id->slot);
        return NULL;
    }
    XgSceneBone bones[MAX_BONES];
    if (xgSceneRigBones(c->scene, id, bones, MAX_BONES) != id->bones) {
        snprintf(c->why, sizeof(c->why), "rig %d changed during export", id->slot);
        return NULL;
    }
    ExportRig* rig = calloc(1, sizeof(*rig));
    if (rig == NULL) { snprintf(c->why, sizeof(c->why), "out of memory"); return NULL; }
    rig->id = *id;
    for (int i = 0; i < id->bones; i++) {
        rig->rest[i] = rigSample(&bones[i], i);
        if (program >= 0) {
            rig->rest[i].visible = 0;
            if (i == 0) memset(rig->rest[i].pose.s, 0, sizeof(rig->rest[i].pose.s));
        }
        int up = i == 0 ? parent : rig->nodes[bones[i].parent >= 0 && bones[i].parent < i ? bones[i].parent : 0];
        char name[64];
        if (primary) snprintf(name, sizeof(name), i == 0 ? "root" : "bone%d", i);
        else if (program < 0) snprintf(name, sizeof(name), "rig%d_g%u_bone%d", id->slot, id->generation, i);
        else snprintf(name, sizeof(name), "anim%d_rig%d_g%u_bone%d", program, id->slot, id->generation, i);
        Trs* x = &rig->rest[i].pose;
        rig->nodes[i] = xgGltfAddNode(c->g, name, -1, up, x->t, x->r, x->s);
        rig->geometry[i] = -1;
        if (rig->nodes[i] < 0) goto bad;
    }
    xgSceneEnter(c->scene);
    for (int p = 0; p < partCount; p++) {
        const XgScenePart* part = &parts[p];
        if (part->rig != id->slot || part->generation != id->generation || part->bone < 0 || part->bone >= id->bones) continue;
        MorphPart none = {0};
        int mesh = cachedMesh(c, part, &none), bone = part->bone;
        if (mesh < 0) continue;
        char name[64];
        snprintf(name, sizeof(name), "rig%d_g%u_geometry%d", id->slot, id->generation, bone);
        float visible[3] = {rig->rest[bone].visible, rig->rest[bone].visible, rig->rest[bone].visible};
        rig->geometry[bone] = xgGltfAddNode(c->g, name, mesh, rig->nodes[bone], NULL, NULL, visible);
        if (rig->geometry[bone] < 0) snprintf(c->why, sizeof(c->why), "cannot add rig geometry");
    }
    xgSceneLeave(c->scene);
    if (c->why[0]) goto bad;
    return rig;
bad:
    if (!c->why[0]) snprintf(c->why, sizeof(c->why), "cannot add rig nodes");
    free(rig);
    return NULL;
}

static int rigChannels(Ctx* c, int animation, const ClipRig* clip, const float* times, int frames) {
    ExportRig* rig = clip->rig;
    float* values = malloc(sizeof(float) * 4 * (size_t)frames);
    if (values == NULL) return -1;
    int channels = 0;
    for (int i = 0; i < rig->id.bones; i++) {
        int moves[4] = {0};
        for (int f = 0; f < frames; f++) {
            RigSample* sample = &clip->samples[(size_t)f * rig->id.bones + i];
            Trs* x = &sample->pose;
            Trs* y = &rig->rest[i].pose;
            for (int k = 0; k < 3; k++) {
                moves[0] |= fabsf(x->t[k] - y->t[k]) > 1e-4f;
                moves[2] |= fabsf(x->s[k] - y->s[k]) > 1e-6f;
            }
            float dot = x->r[0]*y->r[0] + x->r[1]*y->r[1] + x->r[2]*y->r[2] + x->r[3]*y->r[3];
            moves[1] |= fabsf(dot) < 1 - 1e-6f;
            moves[3] |= sample->visible != rig->rest[i].visible;
        }
        for (int path = 0; path < 4; path++) {
            if (!moves[path] || (path == 3 && rig->geometry[i] < 0)) continue;
            int comps = path == 1 ? 4 : 3;
            float previous[4] = {0,0,0,1};
            for (int f = 0; f < frames; f++) {
                RigSample* sample = &clip->samples[(size_t)f * rig->id.bones + i];
                const float* v = path == 0 ? sample->pose.t : path == 1 ? sample->pose.r : sample->pose.s;
                float flip = 1;
                if (path == 1 && f > 0 && previous[0]*v[0] + previous[1]*v[1] + previous[2]*v[2] + previous[3]*v[3] < 0) flip = -1;
                for (int k = 0; k < comps; k++) values[f * comps + k] = path == 3 ? sample->visible : v[k] * flip;
                if (path == 1) memcpy(previous, values + f * comps, sizeof(previous));
            }
            int node = path == 3 ? rig->geometry[i] : rig->nodes[i];
            int result = path >= 2 ? xgGltfAddStepChannel(c->g, animation, node, 's', times, values, frames)
                                   : xgGltfAddChannel(c->g, animation, node, "tr"[path], times, values, frames);
            if (result < 0) { free(values); return -1; }
            channels++;
        }
    }
    free(values);
    return channels;
}

#define MAX_CLIP_RIGS 256
#define MAX_CLIP_BONES 4096

static int rigAnimations(Ctx* c, const XgExportOptions* o, int top, ExportRig** base, int baseCount,
                          XgScenePart* parts) {
    int maxTicks = o->maxTicks > 0 ? o->maxTicks : 300;
    if (maxTicks > 3000) { snprintf(c->why, sizeof(c->why), "export tick limit is 3000"); return 0; }
    double rate = o->tickRate > 1 ? o->tickRate : XG_MECHA_TICK_HZ;
    int* groups = malloc(sizeof(int) * (size_t)o->animationCount);
    int* animations = malloc(sizeof(int) * (size_t)o->animationCount);
    ClipRig* clips = calloc(MAX_CLIP_RIGS, sizeof(*clips));
    float* times = malloc(sizeof(float) * (size_t)(maxTicks + 1));
    int ok = groups != NULL && animations != NULL && clips != NULL && times != NULL;
    if (!ok) snprintf(c->why, sizeof(c->why), "out of memory");
    for (int a = 0; ok && a < o->animationCount; a++) {
        int program = o->animations[a], count = 0, boneTotal = 0, frames = 0;
        groups[a] = -1;
        if (o->beforePlay != NULL) o->beforePlay(c->scene, program, o->user);
        if (!xgScenePlay(c->scene, program, c->why, sizeof(c->why))) { ok = 0; break; }
        char name[64];
        snprintf(name, sizeof(name), "animation%d", program);
        animations[a] = xgGltfAddAnimation(c->g, name);
        if (animations[a] < 0) { ok = 0; break; }
        for (int i = 0; i < baseCount; i++) clips[count++] = (ClipRig){base[i], NULL, 0};
        for (int f = 0; ok && f <= maxTicks; f++) {
            if (f > 0) xgSceneTick(c->scene, 1);
            XgSceneRig ids[XG_SCENE_MAX_RIGS];
            int n = xgSceneRigs(c->scene, ids, XG_SCENE_MAX_RIGS), spawned = 0;
            if (n < 0) { ok = 0; break; }
            /* Mark missing lifetimes absent; still copy their final transforms. */
            for (int k = 0; k < count; k++) {
                ClipRig* clip = &clips[k];
                int bones = clip->rig->id.bones;
                if (clip->samples == NULL) {
                    if (boneTotal + bones > MAX_CLIP_BONES) {
                        snprintf(c->why, sizeof(c->why), "export exceeds %d bones per clip", MAX_CLIP_BONES);
                        ok = 0;
                        break;
                    }
                    clip->samples = malloc(sizeof(RigSample) * (size_t)bones * (maxTicks + 1));
                    boneTotal += bones;
                    if (clip->samples == NULL) {
                        snprintf(c->why, sizeof(c->why), "out of memory");
                        ok = 0;
                        break;
                    }
                }
                RigSample* current = clip->samples + (size_t)f * bones;
                memcpy(current, f ? current - bones : clip->rig->rest, sizeof(RigSample) * (size_t)bones);
                memset(current[0].pose.s, 0, sizeof(current[0].pose.s));
                for (int i = 0; i < bones; i++) current[i].visible = 0;
            }
            for (int k = 0; ok && k < n; k++) {
                spawned |= ids[k].slot >= 19 && ids[k].slot < 31;
                int at;
                for (at = 0; at < count && !sameRig(&clips[at].rig->id, &ids[k]); at++) {}
                if (at == count) {
                    if (count == MAX_CLIP_RIGS || boneTotal + ids[k].bones > MAX_CLIP_BONES) {
                        snprintf(c->why, sizeof(c->why), "export exceeds %d rig lifetimes or %d bones per clip",
                                 MAX_CLIP_RIGS, MAX_CLIP_BONES);
                        ok = 0;
                        break;
                    }
                    if (groups[a] < 0) {
                        float zero[3] = {0,0,0};
                        snprintf(name, sizeof(name), "animation%d_spawned", program);
                        groups[a] = xgGltfAddNode(c->g, name, -1, top, NULL, NULL, zero);
                        if (groups[a] < 0) { ok = 0; break; }
                        /* Future groups get an explicit zero in earlier clips. */
                        for (int earlier = 0; earlier < a; earlier++) {
                            float time = 0;
                            if (xgGltfAddStepChannel(c->g, animations[earlier], groups[a], 's', &time, zero, 1) < 0) ok = 0;
                        }
                    }
                    int partCount = xgSceneParts(c->scene, parts, MAX_PARTS);
                    ExportRig* rig = addRig(c, &ids[k], groups[a], program, 0, parts, partCount);
                    if (rig == NULL) { ok = 0; break; }
                    RigSample* samples = malloc(sizeof(RigSample) * (size_t)ids[k].bones * (maxTicks + 1));
                    clips[count++] = (ClipRig){rig, samples, 1};
                    boneTotal += ids[k].bones;
                    if (samples == NULL) {
                        snprintf(c->why, sizeof(c->why), "out of memory");
                        ok = 0;
                        break;
                    }
                    for (int before = 0; before <= f; before++) memcpy(samples + (size_t)before * ids[k].bones, rig->rest, sizeof(RigSample) * (size_t)ids[k].bones);
                }
                XgSceneBone bones[MAX_BONES];
                if (xgSceneRigBones(c->scene, &ids[k], bones, MAX_BONES) != clips[at].rig->id.bones) { ok = 0; break; }
                for (int i = 0; i < ids[k].bones; i++) clips[at].samples[(size_t)f * ids[k].bones + i] = rigSample(&bones[i], i);
            }
            times[f] = (float)(f / rate);
            frames = f + 1;
            XgVmState vm;
            xgSceneVm(c->scene, &vm);
            if (f > 0 && vm.stopped && !spawned) break;
        }
        int channels = 0;
        for (int k = 0; ok && k < count; k++) {
            int result = rigChannels(c, animations[a], &clips[k], times, frames);
            if (result < 0) ok = 0;
            else channels += result;
        }
        float time = 0, zero[3] = {0,0,0}, one[3] = {1,1,1};
        for (int k = 0; ok && k <= a; k++) {
            if (groups[k] >= 0 && xgGltfAddStepChannel(c->g, animations[a], groups[k], 's', &time, k == a ? one : zero, 1) < 0) ok = 0;
        }
        if (ok && channels == 0 && baseCount > 0 &&
            xgGltfAddChannel(c->g, animations[a], base[0]->nodes[0], 'r', &time, clips[0].samples[0].pose.r, 1) < 0) ok = 0;
        for (int k = 0; k < count; k++) {
            free(clips[k].samples);
            if (clips[k].owned) free(clips[k].rig);
            memset(&clips[k], 0, sizeof(clips[k]));
        }
    }
    free(groups); free(animations); free(clips); free(times);
    if (!ok && !c->why[0]) snprintf(c->why, sizeof(c->why), "cannot export rig animation");
    return ok;
}

static int morphAnimations(Ctx* c, const XgExportOptions* o, const MorphPart* parts, int partCount,
                            char* why, int whySize) {
    int count = xgSceneMorphCount(c->scene);
    int ticks = o->morphTicks > 0 ? o->morphTicks : 30;
    double rate = o->tickRate > 1 ? o->tickRate : XG_MECHA_TICK_HZ;
    if (ticks > 120 || count <= 0 || o->morphs == NULL) {
        snprintf(why, (size_t)whySize, "invalid MIMe export options");
        return 0;
    }
    int frames = 2 * ticks + 1;
    float* weights = malloc(sizeof(float) * (size_t)count * frames);
    float* values = malloc(sizeof(float) * (size_t)count * frames);
    float times[241];
    int ok = weights != NULL && values != NULL;
    if (!ok) snprintf(why, (size_t)whySize, "out of memory");
    for (int a = 0; ok && a < o->morphCount; a++) {
        int target = o->morphs[a];
        if (target < 0 || target >= count) {
            snprintf(why, (size_t)whySize, "invalid MIMe target %d", target);
            ok = 0;
            break;
        }
        for (int k = 0; k < count; k++) xgSceneSetMorph(c->scene, k, 0, 0);
        xgSceneSetMorph(c->scene, target, 4096, ticks);
        for (int f = 0; f < frames; f++) {
            if (f == ticks + 1) xgSceneSetMorph(c->scene, target, 0, ticks);
            if (f > 0) xgSceneTick(c->scene, 1);
            times[f] = (float)(f / rate);
            for (int k = 0; k < count; k++) weights[f * count + k] = xgSceneMorphWeight(c->scene, k) / 4096.0f;
        }
        char name[48];
        snprintf(name, sizeof(name), "MIMe target %d", target);
        int animation = xgGltfAddAnimation(c->g, name), channels = 0;
        for (int p = 0; animation >= 0 && p < partCount; p++) {
            const MorphPart* part = &parts[p];
            if (part->count == 0 || part->node < 0) continue;
            for (int f = 0; f < frames; f++) {
                for (int k = 0; k < part->count; k++) values[f * part->count + k] = weights[f * count + part->ids[k]];
            }
            if (xgGltfAddWeightsChannel(c->g, animation, part->node, times, values, frames, part->count) < 0) {
                ok = 0;
                break;
            }
            channels++;
        }
        if (animation < 0 || channels == 0 || !ok) {
            snprintf(why, (size_t)whySize, "cannot write MIMe animation");
            ok = 0;
        }
    }
    free(weights);
    free(values);
    return ok;
}

int xgSceneExportGlb(XgScene* s, const char* path, const XgExportOptions* o, char* why, int whySize) {
    s = xgSceneCopy(s);
    if (s == NULL) { snprintf(why, (size_t)whySize, "cannot copy export scene"); return 0; }
    Ctx c = {0};
    c.scene = s;
    c.g = xgGltfCreate();
    XgScenePart* parts = malloc(sizeof(XgScenePart) * MAX_PARTS);
    MorphPart* morphs = NULL;
    ExportRig* base[XG_SCENE_MAX_RIGS] = {0};
    XgSceneRig ids[XG_SCENE_MAX_RIGS];
    int rigCount = 0, partCount = 0;
    int ok = c.g != NULL && parts != NULL;
    if (!ok) snprintf(why, (size_t)whySize, "out of memory");
    if (ok) ok = xgSceneBuild(s, why, whySize);
    if (ok) {
        rigCount = xgSceneRigs(s, ids, XG_SCENE_MAX_RIGS);
        partCount = xgSceneParts(s, parts, MAX_PARTS);
        morphs = calloc((size_t)(partCount ? partCount : 1), sizeof(*morphs));
        if (rigCount < 0 || morphs == NULL) { snprintf(why, (size_t)whySize, "cannot snapshot scene"); ok = 0; }
    }
    float unit = o->unit > 0 ? o->unit : 1.0f / 1024;
    float half[4] = {1,0,0,0}, scale[3] = {unit,unit,unit};
    int top = ok ? xgGltfAddNode(c.g, o->name != NULL ? o->name : "xenogears", -1, -1, NULL, half, scale) : -1;
    if (ok && top < 0) { snprintf(why, (size_t)whySize, "cannot add export root"); ok = 0; }
    for (int k = 0; ok && k < rigCount; k++) {
        base[k] = addRig(&c, &ids[k], top, -1, k == 0, parts, partCount);
        if (base[k] == NULL) ok = 0;
    }
    if (ok) {
        xgSceneEnter(s);
        for (int k = 0; k < partCount; k++) {
            if (parts[k].rig >= 0) continue;
            MorphPart* morph = &morphs[k];
            morph->count = xgScenePartMorphs(s, parts[k].block, morph->ids, MAX_MORPHS);
            morph->node = -1;
            if (morph->count < 0) { snprintf(c.why, sizeof(c.why), "too many MIMe targets"); ok = 0; break; }
            int mesh = cachedMesh(&c, &parts[k], morph);
            if (mesh < 0) continue;
            char name[32];
            float rotation[4], scale[3];
            snprintf(name, sizeof(name), "part%d", k);
            decompose(parts[k].m, rotation, scale);
            if (!parts[k].enabled) memset(scale, 0, sizeof(scale));
            morph->node = xgGltfAddNode(c.g, name, mesh, top, parts[k].t, rotation, scale);
            if (morph->node < 0) { snprintf(c.why, sizeof(c.why), "cannot add static part"); ok = 0; break; }
        }
        xgSceneLeave(s);
        if (c.why[0]) ok = 0;
    }
    if (ok && rigCount > 0 && o->animationCount > 0) ok = rigAnimations(&c, o, top, base, rigCount, parts);
    if (ok && o->morphCount > 0) ok = morphAnimations(&c, o, morphs, partCount, why, whySize);
    if (ok && !xgGltfWriteGlb(c.g, path)) { snprintf(why, (size_t)whySize, "%s", xgWriterError()); ok = 0; }
    if (c.why[0]) { snprintf(why, (size_t)whySize, "%s", c.why); ok = 0; }
    for (int k = 0; k < rigCount; k++) free(base[k]);
    free(c.meshBlocks); free(c.meshes); free(parts); free(morphs);
    xgGltfDestroy(c.g);
    xgSceneDestroy(s);
    return ok;
}
