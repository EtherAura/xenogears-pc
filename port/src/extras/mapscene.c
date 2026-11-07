/*
 * The map viewer's scene exports (mapfield.h xgFieldExportGlb): a field as
 * its overlay set it up, written with our own GLB writer (writers.h).
 *
 * What goes in:
 *   - Axes and units: the PS1's y points down and its z away from the
 *     viewer, glTF's y up: positions become (x, -y, -z) (a half turn about
 *     x, as the model viewer's exports), times `unit` (default 1/512: a room
 *     of 6000 field units is about 12 m).
 *   - Models: every model the field draws (renderObjects' entities: placed
 *     room models and script actors with a model), each its own node and
 *     mesh, its vertices placed where the field's matrix puts them (baked:
 *     the field's matrices may scale). Faces from the block's mesh blocks
 *     (quads split 0-1-2, 1-3-2 as the GPU does), texture coordinates, page,
 *     CLUT and colours from the primitives initModel2 set up and lit.
 *   - Gears: every visible field Gear's bone geometry, baked into its
 *     current pose and world placement, as static mesh nodes.
 *   - Materials: unlit, double sided; a texture page decoded through its
 *     CLUT from the field's VRAM (the sandbox's: the field's images, NPC
 *     sheets and texture file as the field uploaded them) as a 256 x 256 PNG
 *     with nearest filtering, texel 0 clear (alpha mask). Semi-transparency
 *     is approximated (glTF only blends): a semi-transparent polygon's
 *     material blends, its STP texels at alpha 0.5 (0.25 for the B + F/4
 *     mode); additive and subtractive modes become the same blend.
 *   - Colours (COLOR_0): textured polygons' colours / 128 (the GPU's
 *     modulation, white 128 = 1), untextured ones' / 255.
 *   - The walkmesh (a mesh per layer, coloured by material, half
 *     transparent), the trigger zones (red quads at their floor height), and
 *     an empty node per script actor where it stands (sprites are pictures
 *     the GPU draws facing the camera: they have no 3D form).
 */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common.h"
#include "field/field.h"
#include "game/field/mapsview.h"
#include "mapfield.h"
#include "models.h"
#include "psyq.h"
#include "writers.h"

/* main's model code (model.c polyRenderDefs): sizes per mesh block family. */
typedef struct {
    void* render[6];
    void* init;
    s32 indexSize, displayListSize, primSize;
} MapsPolyDef;
extern MapsPolyDef polyRenderDefs[17];

#define MAX_MODELS 512
#define MAX_MATERIALS 256

/* mapfield.c: inside the field's sandbox, and its VRAM */
void xgFieldEnter(XgField* f);
void xgFieldLeave(XgField* f);
const uint16_t* xgFieldVram(XgField* f);

typedef struct {
    int textured, semi, raw, quad, gouraud, n;
    uint16_t tpage, clut;
    uint8_t u[4], v[4];
    uint8_t r[4], g[4], b[4];
} Packet;

/* A primitive as initModel2 left it (GP0 packet after the tag). */
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
        at++; /* the position */
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
 * (model.c: the textured ones). */
static int takesControlWords(int type) {
    return type < 16 && (type & 1);
}

typedef struct {
    int textured, semi;
    uint16_t tpage, clut;
    int index;
} Material;

typedef struct {
    float* pos;
    float* uv;
    float* col;
    uint32_t* idx;
    int verts, cap;
} Batch;

typedef struct {
    XgGltf* g;
    const uint16_t* vram;
    float unit;
    Material mats[MAX_MATERIALS];
    int matCount;
    int failed;
} Ctx;

/* A texture page through its CLUT as 256 x 256 RGBA (texel 0 clear; STP
 * texels at alpha `stp` when semi-transparent). */
static void bakePage(const uint16_t* vram, uint16_t tpage, uint16_t clut, int semi, uint8_t stp, uint32_t* rgba) {
    int px = (tpage & 15) * 64, py = (tpage >> 4 & 1) * 256, depth = tpage >> 7 & 3;
    int cx = (clut & 63) * 16, cy = clut >> 6 & 511;
    for (int y = 0; y < 256; y++) {
        for (int x = 0; x < 256; x++) {
            int vy = (py + y) & 511;
            uint16_t t;
            if (depth == 0) {
                uint16_t w = vram[vy * 1024 + ((px + (x >> 2)) & 1023)];
                t = vram[cy * 1024 + ((cx + ((w >> ((x & 3) * 4)) & 15)) & 1023)];
            } else if (depth == 1) {
                uint16_t w = vram[vy * 1024 + ((px + (x >> 1)) & 1023)];
                t = vram[cy * 1024 + ((cx + ((w >> ((x & 1) * 8)) & 255)) & 1023)];
            } else {
                t = vram[vy * 1024 + ((px + x) & 1023)];
            }
            uint32_t r = t & 31, g = t >> 5 & 31, b = t >> 10 & 31;
            uint32_t a = t == 0 ? 0 : semi && (t & 0x8000) ? stp : 255;
            rgba[y * 256 + x] = (r << 3 | r >> 2) | (g << 3 | g >> 2) << 8 | (b << 3 | b >> 2) << 16 | a << 24;
        }
    }
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
        c->failed = 2;
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
            c->failed = 1;
            return -1;
        }
        bakePage(c->vram, key.tpage, key.clut, key.semi, (key.tpage >> 5 & 3) == 3 ? 64 : 128, rgba);
        size_t pngSize = 0;
        uint8_t* png = xgPngEncode(rgba, 256, 256, &pngSize);
        free(rgba);
        key.index = png != NULL ? xgGltfAddTexturedMaterial(c->g, name, png, pngSize, key.semi) : -1;
        free(png);
    } else {
        snprintf(name, sizeof(name), "colour%s", key.semi ? "_semi" : "");
        key.index = xgGltfAddColourMaterial(c->g, name);
        if (key.semi && key.index >= 0) {
            xgGltfSetBlend(c->g, key.index, 1);
        }
    }
    if (key.index < 0) {
        c->failed = 1;
        return -1;
    }
    c->mats[c->matCount++] = key;
    return key.index;
}

static int batchVertex(Batch* b, const float* pos, float u, float v, const float* col) {
    if (b->verts == b->cap) {
        int cap = b->cap ? b->cap * 2 : 256;
        float* p = realloc(b->pos, sizeof(float) * 3 * (size_t)cap);
        if (p != NULL) {
            b->pos = p;
        }
        float* q = p != NULL ? realloc(b->uv, sizeof(float) * 2 * (size_t)cap) : NULL;
        if (q != NULL) {
            b->uv = q;
        }
        float* r = q != NULL ? realloc(b->col, sizeof(float) * 4 * (size_t)cap) : NULL;
        if (r != NULL) {
            b->col = r;
        }
        uint32_t* s = r != NULL ? realloc(b->idx, sizeof(uint32_t) * (size_t)cap) : NULL;
        if (s == NULL) {
            return 0;
        }
        b->idx = s;
        b->cap = cap;
    }
    memcpy(b->pos + 3 * b->verts, pos, sizeof(float) * 3);
    b->uv[2 * b->verts] = u;
    b->uv[2 * b->verts + 1] = v;
    memcpy(b->col + 4 * b->verts, col, sizeof(float) * 4);
    b->idx[b->verts] = (uint32_t)b->verts;
    b->verts++;
    return 1;
}

/* A field position (the GTE's MATRIX: out = m . v / 4096 + t) in glTF's axes. */
static void place(const Ctx* c, const MATRIX* m, const SVECTOR* v, float out[3]) {
    float p[3];
    for (int r = 0; r < 3; r++) {
        p[r] = (m->m[r][0] * (float)v->vx + m->m[r][1] * (float)v->vy + m->m[r][2] * (float)v->vz) / 4096.0f +
               (float)m->t[r];
    }
    out[0] = p[0] * c->unit;
    out[1] = -p[1] * c->unit;
    out[2] = -p[2] * c->unit;
}

static void flushBatches(Ctx* c, int mesh, Batch* batches, const int* batchMat, int count) {
    for (int b = 0; b < count; b++) {
        if (mesh >= 0 && batches[b].verts > 0) {
            int textured = 0;
            for (int i = 0; i < c->matCount; i++) {
                if (c->mats[i].index == batchMat[b]) {
                    textured = c->mats[i].textured;
                }
            }
            if (xgGltfAddPrimitive(c->g, mesh, batchMat[b], batches[b].pos, textured ? batches[b].uv : NULL,
                                   batches[b].col, batches[b].verts, batches[b].idx, batches[b].verts) < 0) {
                c->failed = 1;
            }
        }
        free(batches[b].pos);
        free(batches[b].uv);
        free(batches[b].col);
        free(batches[b].idx);
    }
}

/* A model as one mesh, a primitive per material, its vertices placed. */
static int modelMesh(Ctx* c, const MapsFieldModel* fm, const XgScenePart* gear, const char* name) {
    const ModelBlock* blk = gear != NULL ? gear->block : fm->block;
    Batch* batches = calloc(MAX_MATERIALS, sizeof(Batch));
    int batchMat[MAX_MATERIALS];
    int batchCount = 0;
    if (batches == NULL) {
        c->failed = 1;
        return -1;
    }
    const u8* mb = blk->m10_meshBlocks;
    const u8* prim = gear != NULL ? gear->prims : fm->prims;
    const u8* dl = blk->m14_displayList;
    for (int g = 0; g < blk->m6_numMeshBlocks && mb != NULL && prim != NULL; g++) {
        int type = mb[0], polys = *(const u16*)(mb + 2);
        if (type > 16) {
            break;
        }
        const u16* idx = (const u16*)(mb + 4);
        for (int k = 0; k < polys; k++, prim += polyRenderDefs[type].primSize, idx += 4) {
            /* the attribute stream as initModel2 walks it */
            while (takesControlWords(type) && dl != NULL && (dl[3] == 0xC4 || dl[3] == 0xC8)) {
                dl += 4;
            }
            if (dl != NULL) {
                dl += polyRenderDefs[type].displayListSize;
            }
            Packet p;
            if (!readPacket(prim, &p)) {
                continue;
            }
            int mat = material(c, &p);
            if (mat < 0) {
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
                    int vert = idx[vi] < blk->m2_numVertices ? idx[vi] : 0;
                    float pos[3];
                    const SVECTOR* v = &blk->m8_vertices[vert];
                    if (gear != NULL) {
                        for (int r = 0; r < 3; r++) {
                            float p = gear->m[r][0] * v->vx + gear->m[r][1] * v->vy + gear->m[r][2] * v->vz + gear->t[r];
                            pos[r] = p * c->unit * (r == 0 ? 1.0f : -1.0f);
                        }
                    } else {
                        place(c, &fm->m, v, pos);
                    }
                    int gi = p.gouraud ? vi : 0;
                    float scale = p.textured ? 1.0f / 128 : 1.0f / 255;
                    float col[4] = {fminf(p.r[gi] * scale, 1.0f), fminf(p.g[gi] * scale, 1.0f),
                                    fminf(p.b[gi] * scale, 1.0f), !p.textured && p.semi ? 0.5f : 1.0f};
                    if (p.textured && p.raw) {
                        col[0] = col[1] = col[2] = 1;
                    }
                    if (!batchVertex(&batches[b], pos, (p.u[vi] + 0.5f) / 256.0f, (p.v[vi] + 0.5f) / 256.0f, col)) {
                        c->failed = 1;
                    }
                }
            }
        }
        mb += 4 + polys * polyRenderDefs[type].indexSize;
    }
    int mesh = batchCount > 0 ? xgGltfAddEmptyMesh(c->g, name) : -1;
    if (batchCount > 0 && mesh < 0) {
        c->failed = 1;
    }
    flushBatches(c, mesh, batches, batchMat, batchCount);
    free(batches);
    return mesh;
}

/* Triangles of one colour material each (the walkmesh, trigger zones). */
static int flatMesh(Ctx* c, const char* name, const char* material, const float* pos, const float* col, int verts) {
    int mat = xgGltfAddColourMaterial(c->g, material);
    if (mat < 0 || verts == 0) {
        c->failed |= mat < 0;
        return -1;
    }
    xgGltfSetBlend(c->g, mat, 1);
    uint32_t* idx = malloc(sizeof(uint32_t) * (size_t)verts);
    if (idx == NULL) {
        c->failed = 1;
        return -1;
    }
    for (int i = 0; i < verts; i++) {
        idx[i] = (uint32_t)i;
    }
    int mesh = xgGltfAddEmptyMesh(c->g, name);
    if (mesh < 0) {
        c->failed = 1;
    }
    if (mesh >= 0 && xgGltfAddPrimitive(c->g, mesh, mat, pos, NULL, col, verts, idx, verts) < 0) {
        c->failed = 1;
    }
    free(idx);
    return mesh;
}

static void toGltf(const Ctx* c, float x, float y, float z, float* out) {
    out[0] = x * c->unit;
    out[1] = -y * c->unit;
    out[2] = -z * c->unit;
}

int xgFieldExportGlb(XgField* f, const char* path, unsigned what, float unit, char* why, int whySize) {
    const XgFieldInfo* in = xgFieldInfo(f);
    Ctx c;
    memset(&c, 0, sizeof(c));
    c.unit = unit > 0 ? unit : 1.0f / 512.0f;
    c.g = xgGltfCreate();
    MapsFieldModel* models = malloc(sizeof(MapsFieldModel) * MAX_MODELS);
    if (c.g == NULL || models == NULL) {
        snprintf(why, (size_t)whySize, "out of memory");
        xgGltfDestroy(c.g);
        free(models);
        return 0;
    }
    char name[64];
    snprintf(name, sizeof(name), "field %03d", in->id);
    int root = xgGltfAddNode(c.g, name, -1, -1, NULL, NULL, NULL);
    c.failed |= root < 0;
    /* The models: inside the sandbox (its heap holds them; its VRAM the
     * textures). */
    xgFieldEnter(f);
    c.vram = xgFieldVram(f);
    int n = (what & XG_FIELD_MODELS) ? mapsFieldModels(models, MAX_MODELS) : 0;
    for (int i = 0; i < n; i++) {
        if (models[i].hidden) {
            continue;
        }
        snprintf(name, sizeof(name), "%s %d", models[i].entity < in->actors ? "actor" : "model", models[i].entity);
        int mesh = modelMesh(&c, &models[i], NULL, name);
        if (mesh >= 0) {
            c.failed |= xgGltfAddNode(c.g, name, mesh, root, NULL, NULL, NULL) < 0;
        }
    }
    if ((what & XG_FIELD_MODELS) && in->mechas > 0) {
        XgScenePart* parts = malloc(sizeof(XgScenePart) * MAX_MODELS);
        if (parts == NULL) {
            c.failed = 1;
        } else {
            /* The field renderer has posed the gears. Their native blocks
             * and world placements come from the model viewer's collector. */
            int count = xgModelsFieldMechaParts(parts, MAX_MODELS);
            for (int i = 0; i < count; i++) {
                if (!parts[i].enabled || parts[i].block == NULL || parts[i].prims == NULL) {
                    continue;
                }
                snprintf(name, sizeof(name), "gear part %d", i);
                int mesh = modelMesh(&c, NULL, &parts[i], name);
                if (mesh >= 0) {
                    c.failed |= xgGltfAddNode(c.g, name, mesh, root, NULL, NULL, NULL) < 0;
                }
            }
            free(parts);
        }
    }
    xgFieldLeave(f);
    free(models);
    /* The walkmesh: a mesh per layer. */
    for (int l = 0; (what & XG_FIELD_WALKMESH) && l < in->layers; l++) {
        int verts = in->tris[l] * 3;
        float* pos = malloc(sizeof(float) * 3 * (size_t)(verts > 0 ? verts : 1));
        float* col = malloc(sizeof(float) * 4 * (size_t)(verts > 0 ? verts : 1));
        if (pos == NULL || col == NULL) {
            c.failed = 1;
            free(pos);
            free(col);
            continue;
        }
        for (int i = 0; i < in->tris[l]; i++) {
            const XgFieldTri* t = &in->walk[l][i];
            for (int k = 0; k < 3; k++) {
                /* wound as glTF faces (counter-clockwise from above) */
                int kk = k == 0 ? 0 : 3 - k;
                toGltf(&c, t->v[kk][0], t->v[kk][1], t->v[kk][2], &pos[(i * 3 + k) * 3]);
                static const float pal[8][3] = {{0.3f, 0.8f, 0.5f}, {0.35f, 0.6f, 0.9f}, {0.9f, 0.8f, 0.3f},
                                                {0.8f, 0.45f, 0.85f}, {0.95f, 0.55f, 0.35f}, {0.5f, 0.85f, 0.85f},
                                                {0.7f, 0.9f, 0.4f}, {0.9f, 0.45f, 0.55f}};
                float* cc = &col[(i * 3 + k) * 4];
                cc[0] = pal[t->material & 7][0];
                cc[1] = pal[t->material & 7][1];
                cc[2] = pal[t->material & 7][2];
                cc[3] = 0.5f;
            }
        }
        snprintf(name, sizeof(name), "walkmesh %d", l);
        int mesh = flatMesh(&c, name, "walkmesh", pos, col, verts);
        if (mesh >= 0) {
            c.failed |= xgGltfAddNode(c.g, name, mesh, root, NULL, NULL, NULL) < 0;
        }
        free(pos);
        free(col);
    }
    /* The trigger zones: two triangles each at the floor height the game
     * tests. */
    if ((what & XG_FIELD_TRIGGERS) && in->triggers > 0) {
        int verts = in->triggers * 6;
        float* pos = malloc(sizeof(float) * 3 * (size_t)verts);
        float* col = malloc(sizeof(float) * 4 * (size_t)verts);
        if (pos != NULL && col != NULL) {
            static const int order[6] = {0, 2, 1, 0, 3, 2};
            for (int i = 0; i < in->triggers; i++) {
                const XgFieldTrigger* t = &in->trigger[i];
                for (int k = 0; k < 6; k++) {
                    const int16_t* p = t->c[order[k]];
                    toGltf(&c, p[0], t->c[0][1], p[2], &pos[(i * 6 + k) * 3]);
                    float* cc = &col[(i * 6 + k) * 4];
                    cc[0] = 1.0f, cc[1] = 0.25f, cc[2] = 0.2f, cc[3] = 0.5f;
                }
            }
            int mesh = flatMesh(&c, "trigger zones", "trigger", pos, col, verts);
            if (mesh >= 0) {
                c.failed |= xgGltfAddNode(c.g, "trigger zones", mesh, root, NULL, NULL, NULL) < 0;
            }
        } else {
            c.failed = 1;
        }
        free(pos);
        free(col);
    }
    /* The script actors where they stand. */
    for (int i = 0; (what & XG_FIELD_MARKERS) && i < in->actors; i++) {
        const XgFieldActor* a = &in->actor[i];
        float t[3];
        toGltf(&c, (float)a->pos[0], (float)a->pos[1], (float)a->pos[2], t);
        snprintf(name, sizeof(name), "actor %d%s", i, a->hidden ? " (hidden)" : "");
        c.failed |= xgGltfAddNode(c.g, name, -1, root, t, NULL, NULL) < 0;
    }
    int ok = !c.failed && xgGltfWriteGlb(c.g, path);
    if (!ok) {
        snprintf(why, (size_t)whySize, "%s", (c.failed & 2) ? "too many scene materials" :
                 c.failed ? "out of memory" : xgWriterError());
    }
    xgGltfDestroy(c.g);
    return ok;
}
