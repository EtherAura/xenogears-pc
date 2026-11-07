/*
 * A field loaded and drawn by the field overlay's own code, in a sandbox
 * (mapfield.h).
 */

#include "mapfield.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common.h"
#include "field/field.h"
#include "game/field/mapsview.h"
#include "hostfiles.h"
#include "main/game_state.h"
#include "psyq.h"
#include "pc_wide.h"
#include "sandbox.h"
#include "system/world.h"

#define SANDBOX_HEAP (8u << 20) /* as the game's own (memory.c); larger breaks allocateHeap mode 2 */
#define GAME_W 320
#define GAME_H 224
#define MARK_SIZE 24 /* actor marker half size (world units) */

struct XgField {
    XgSandbox* sb;
    HwWorld* world; /* the interrupt side and SPU the sound driver sets up (system/world.h) */
    XgHostFiles* files;
    XgFieldInfo info;
    /* what it started from (to start it again) */
    int useState, storyProgress, entry;
    unsigned char* state;
    int exitsFound;
    /* the last drawn screen's projection (overlays and xgFieldProject) */
    float m[3][3], t[3], h, ofx, ofy;
    int screenW, screenH;
};

/* Inside: the sandbox (the modules, heap, GPU), a world of its own for the
 * sound driver's events, root counter and SPU, and for any vblank wait (a
 * frame of the world's own time), and the disc's files. */
static void enter(XgField* f) {
    xgSandboxEnter(f->sb);
    hwWorldEnter(f->world);
    hwWorldSetSpu(0); /* the driver runs, nothing is mixed: nobody hears it */
    xgHostFilesAttach(f->files);
}

static void leave(XgField* f) {
    xgHostFilesDetach(f->files);
    hwWorldLeave(f->world);
    xgSandboxLeave(f->sb);
}

static void bounds(XgFieldInfo* in, int x, int y, int z) {
    int v[3] = {x, y, z};
    for (int k = 0; k < 3; k++) {
        in->min[k] = v[k] < in->min[k] ? v[k] : in->min[k];
        in->max[k] = v[k] > in->max[k] ? v[k] : in->max[k];
    }
}

/* Inside, after the start: what the viewer shows, copied out. */
static void collect(XgField* f, int triggerBytes, int walkBytes) {
    XgFieldInfo* in = &f->info;
    MapsFieldView v;
    mapsFieldView(&v);
    in->startProgress = gameState.m1930_fieldVarsBackup[0];
    in->startEntry = gameState.m1930_fieldVarsBackup[2];
    for (int k = 0; k < 3; k++) {
        in->min[k] = 0x7FFFFFFF;
        in->max[k] = -0x7FFFFFFF;
    }
    in->objects = v.objects;
    in->actors = v.actors;
    in->actor = calloc((size_t)(in->objects > 0 ? in->objects : 1), sizeof(XgFieldActor));
    for (int i = 0; in->actor != NULL && i < in->objects; i++) {
        MapsFieldActor m;
        mapsFieldActor(i, &m);
        XgFieldActor* a = &in->actor[i];
        a->flags = m.flags;
        a->script = m.script;
        a->hidden = m.hidden;
        a->sprite = m.sprite;
        a->model = m.model;
        a->npc = m.npc;
        memcpy(a->pos, m.pos, sizeof(a->pos));
        memcpy(a->rot, m.rot, sizeof(a->rot));
        a->direction = m.direction;
        a->walkmesh = m.walkmesh;
        a->triangle = m.triangle;
        a->character = m.character;
        if (!a->hidden) {
            bounds(in, a->pos[0], a->pos[1], a->pos[2]);
        }
    }
    in->models = v.models;
    in->layers = v.layers;
    /* the material flags run from their offset to the section's end (a
     * triangle's material is a byte: at most 256) */
    int flagsAt = v.walkMesh != NULL ? (int)((u8*)v.materialFlags - v.walkMesh) : 0;
    in->materials = flagsAt > 0 && walkBytes > flagsAt ? (walkBytes - flagsAt) / 4 : 0;
    in->materials = in->materials > 256 ? 256 : in->materials;
    for (int l = 0; l < in->layers; l++) {
        const sWalkMeshTriangle* tri = (const sWalkMeshTriangle*)v.triangles[l];
        in->tris[l] = v.tris[l];
        in->walk[l] = calloc((size_t)(in->tris[l] > 0 ? in->tris[l] : 1), sizeof(XgFieldTri));
        for (int i = 0; in->walk[l] != NULL && i < in->tris[l]; i++) {
            XgFieldTri* t = &in->walk[l][i];
            for (int c = 0; c < 3; c++) {
                const SVECTOR* p = &v.vertices[l][tri[i].m0_verticeIndex[c]];
                t->v[c][0] = p->vx;
                t->v[c][1] = p->vy;
                t->v[c][2] = p->vz;
                t->neighbour[c] = tri[i].m6_connectivity[c];
                bounds(in, p->vx, p->vy, p->vz);
            }
            t->material = tri[i].mC_indexInWalkmeshData1;
            t->thickness = tri[i].mD_thicknessDiv4;
            t->flags = t->material < in->materials ? (uint32_t)v.materialFlags[t->material] : 0;
        }
    }
    in->triggers = v.triggers != NULL ? triggerBytes / (int)sizeof(XgFieldTrigger) : 0;
    in->trigger = calloc((size_t)(in->triggers > 0 ? in->triggers : 1), sizeof(XgFieldTrigger));
    if (in->trigger != NULL && in->triggers > 0) {
        memcpy(in->trigger, v.triggers, (size_t)in->triggers * sizeof(XgFieldTrigger));
    }
    memcpy(in->eye, v.eye, sizeof(in->eye));
    memcpy(in->at, v.at, sizeof(in->at));
    in->scrz = v.scrz;
    in->mechas = v.mechas;
    in->music = v.music;
    if (in->min[0] > in->max[0]) {
        memset(in->min, 0, sizeof(in->min));
        memset(in->max, 0, sizeof(in->max));
    }
}

int xgFieldGameStateSize(void) {
    return (int)sizeof(GameState);
}

int xgFieldGameState(void* out, int size) {
    if (out == NULL || xgSandboxCurrent() != NULL || size < (int)sizeof(GameState)) {
        return 0;
    }
    memcpy(out, &gameState, sizeof(GameState));
    return 1;
}

/* Inside: the field's start in a fresh world, the game camera settled. */
static int restart(XgField* f) {
    if (!mapsFieldStart(f->info.id, f->useState ? f->state : NULL, f->storyProgress, f->entry)) {
        return 0;
    }
    mapsFieldSettleCamera(64);
    return 1;
}

static void fresh(XgField* f) {
    xgSandboxReset(f->sb, 1);
    xgHostFilesCloseAll(f->files);
    HwWorld* w = hwWorldCreate();
    if (w != NULL) {
        hwWorldDestroy(f->world);
        f->world = w;
    }
}

XgField* xgFieldLoad(XgDisc* disc, int id, const XgFieldStart* start, char* why, int whySize) {
    static const char* const modules[] = {"field", "field_mecha", NULL};
    if (id < 0 || id >= XG_FIELD_COUNT) {
        snprintf(why, (size_t)whySize, "no field %d", id);
        return NULL;
    }
    int file = xgDiscFileEntry(disc, 4, 0xB8 + 2 * id);
    XgEntry e = xgDiscEntry(disc, file);
    if (file < 0 || e.size < 0x190) {
        snprintf(why, (size_t)whySize, "field %d is not on this disc (%d bytes)", id, e.size);
        return NULL;
    }
    /* The trigger section's size, from the archive's header. */
    int triggerBytes = 0, walkBytes = 0;
    uint8_t* head = xgDiscReadSectors(disc, e.lba, 1);
    if (head != NULL) {
        memcpy(&walkBytes, head + 0x110, 4);
        memcpy(&triggerBytes, head + 0x12C, 4);
        free(head);
    }
    XgField* f = calloc(1, sizeof(XgField));
    if (f == NULL || (f->sb = xgSandboxCreate(modules, SANDBOX_HEAP)) == NULL ||
        (f->world = hwWorldCreate()) == NULL || (f->files = xgHostFilesCreate(disc)) == NULL) {
        snprintf(why, (size_t)whySize, "out of memory");
        xgFieldDestroy(f);
        return NULL;
    }
    f->info.id = id;
    f->storyProgress = start != NULL ? start->storyProgress : -1;
    f->entry = start != NULL ? start->entry : -1;
    if (start != NULL && start->useState && start->gameState != NULL) {
        f->state = malloc(sizeof(GameState));
        if (f->state == NULL) {
            snprintf(why, (size_t)whySize, "out of memory copying the game state");
            xgFieldDestroy(f);
            return NULL;
        }
        memcpy(f->state, start->gameState, sizeof(GameState));
        f->useState = 1;
    }
    enter(f);
    int ok = restart(f);
    if (ok) {
        collect(f, triggerBytes, walkBytes);
    }
    f->info.hostReads = xgHostFilesOpened(f->files, NULL);
    leave(f);
    if (!ok) {
        snprintf(why, (size_t)whySize, "field %d did not set up (the sandbox's heap?)", id);
        xgFieldDestroy(f);
        return NULL;
    }
    snprintf(f->info.note, sizeof(f->info.note), "%d files read, %d actors, %d objects", f->info.hostReads,
             f->info.actors, f->info.objects);
    return f;
}

void xgFieldDestroy(XgField* f) {
    if (f == NULL) {
        return;
    }
    xgSandboxDestroy(f->sb);
    hwWorldDestroy(f->world);
    xgHostFilesDestroy(f->files);
    free(f->info.actor);
    for (int l = 0; l < 4; l++) {
        free(f->info.walk[l]);
    }
    free(f->info.trigger);
    free(f->info.exit);
    free(f->state);
    free(f);
}

/* ---- exits ---------------------------------------------------------------------------------- */

#define PROBE_FRAMES 40 /* scripts wait (a door opens, a sound plays) before they change field */

static void addExit(XgFieldInfo* in, int how, int field, int entry, int actor, int trigger) {
    int target = how == 2 ? 0x1000 + field : field;
    for (int i = 0; i < in->exits; i++) {
        XgFieldExit* x = &in->exit[i];
        if (x->field == target && x->entry == entry) {
            if (x->trigger < 0) {
                x->trigger = trigger;
            }
            return;
        }
    }
    XgFieldExit* grown = realloc(in->exit, (size_t)(in->exits + 1) * sizeof(XgFieldExit));
    if (grown == NULL) {
        return;
    }
    in->exit = grown;
    XgFieldExit* x = &in->exit[in->exits++];
    memset(x, 0, sizeof(*x));
    x->actor = actor;
    x->routine = -1;
    x->field = target;
    x->entry = entry;
    x->trigger = trigger;
    x->op = how;
}

int xgFieldFindExits(XgField* f) {
    XgFieldInfo* in = &f->info;
    if (f->exitsFound) {
        return in->exits;
    }
    f->exitsFound = 1;
    /* Where the player is put: each trigger zone's middle (on its floor), and
     * at each visible actor (its touch). */
    int probes = in->triggers + in->actors;
    for (int p = 0; p < probes; p++) {
        int x, y, z, trigger = -1, actor = -1;
        if (p < in->triggers) {
            const XgFieldTrigger* t = &in->trigger[p];
            x = (t->c[0][0] + t->c[1][0] + t->c[2][0] + t->c[3][0]) / 4;
            z = (t->c[0][2] + t->c[1][2] + t->c[2][2] + t->c[3][2]) / 4;
            y = t->c[0][1];
            trigger = p;
        } else {
            actor = p - in->triggers;
            const XgFieldActor* a = &in->actor[actor];
            if (a->hidden || !a->script) {
                continue;
            }
            x = a->pos[0];
            y = a->pos[1];
            z = a->pos[2];
        }
        int field = -1, entry = -1;
        fresh(f);
        enter(f);
        int how = restart(f) ? mapsFieldProbe(x, y, z, PROBE_FRAMES, &field, &entry) : 0;
        leave(f);
        if (how != 0) {
            addExit(in, how, field, entry, actor, trigger);
        }
    }
    /* The world the viewer draws, as loaded. */
    fresh(f);
    enter(f);
    restart(f);
    leave(f);
    return in->exits;
}

const XgFieldInfo* xgFieldInfo(const XgField* f) {
    return &f->info;
}

XgVgpu* xgFieldGpu(XgField* f) {
    return xgSandboxGpu(f->sb);
}

/* ---- drawing ------------------------------------------------------------------------------- */

static void keepProjection(XgField* f, int w, int h, float H, float ofx, float ofy) {
    MATRIX mat;
    mapsFieldProjection(&mat);
    MATRIX* m = &mat;
    for (int r = 0; r < 3; r++) {
        for (int c = 0; c < 3; c++) {
            f->m[r][c] = m->m[r][c] / 4096.0f;
        }
        f->t[r] = (float)m->t[r];
    }
    f->h = H;
    f->ofx = ofx;
    f->ofy = ofy;
    f->screenW = w;
    f->screenH = h;
}

static void toCamera(const XgField* f, const float p[3], float c[3]) {
    for (int r = 0; r < 3; r++) {
        c[r] = f->m[r][0] * p[0] + f->m[r][1] * p[1] + f->m[r][2] * p[2] + f->t[r];
    }
}

int xgFieldProject(XgField* f, const float p[3], float* x, float* y) {
    float c[3];
    toCamera(f, p, c);
    if (c[2] < 1.0f) {
        return 0;
    }
    *x = f->ofx + f->h * c[0] / c[2];
    *y = f->ofy + f->h * c[1] / c[2];
    return 1;
}

static void addLine(XgField* f, XgVgpu* g, const float a[3], const float b[3], uint8_t r, uint8_t gr, uint8_t bl) {
    float ca[3], cb[3];
    toCamera(f, a, ca);
    toCamera(f, b, cb);
    const float zn = 16.0f;
    if (ca[2] < zn && cb[2] < zn) {
        return;
    }
    /* clip at the near plane */
    if (ca[2] < zn || cb[2] < zn) {
        float t = (zn - ca[2]) / (cb[2] - ca[2]);
        float m[3] = {ca[0] + (cb[0] - ca[0]) * t, ca[1] + (cb[1] - ca[1]) * t, zn};
        if (ca[2] < zn) {
            memcpy(ca, m, sizeof(m));
        } else {
            memcpy(cb, m, sizeof(m));
        }
    }
    XgPrim p;
    memset(&p, 0, sizeof(p));
    float xa = f->ofx + f->h * ca[0] / ca[2], ya = f->ofy + f->h * ca[1] / ca[2];
    float xb = f->ofx + f->h * cb[0] / cb[2], yb = f->ofy + f->h * cb[1] / cb[2];
    if (fabsf(xa) > 30000 || fabsf(ya) > 30000 || fabsf(xb) > 30000 || fabsf(yb) > 30000) {
        return;
    }
    p.x[0] = (int16_t)lrintf(xa);
    p.y[0] = (int16_t)lrintf(ya);
    p.x[1] = (int16_t)lrintf(xb);
    p.y[1] = (int16_t)lrintf(yb);
    p.r[0] = p.r[1] = r;
    p.g[0] = p.g[1] = gr;
    p.b[0] = p.b[1] = bl;
    p.count = 2;
    p.flags = XG_PRIM_LINE;
    p.area[0] = 0; /* the screen, as the scene's own primitives */
    p.area[1] = 0;
    p.area[2] = (int16_t)(f->screenW - 1);
    p.area[3] = (int16_t)(f->screenH - 1);
    xgVgpuAddPrim(g, &p);
}

static void addTriangle(XgField* f, XgVgpu* g, const float v[3][3], uint8_t r, uint8_t gr, uint8_t bl) {
    float c[3][3];
    for (int k = 0; k < 3; k++) {
        toCamera(f, v[k], c[k]);
        if (c[k][2] < 16.0f) {
            return; /* whole triangles only (their edges are clipped lines) */
        }
    }
    XgPrim p;
    memset(&p, 0, sizeof(p));
    for (int k = 0; k < 3; k++) {
        float x = f->ofx + f->h * c[k][0] / c[k][2], y = f->ofy + f->h * c[k][1] / c[k][2];
        if (fabsf(x) > 30000 || fabsf(y) > 30000) {
            return;
        }
        p.x[k] = (int16_t)lrintf(x);
        p.y[k] = (int16_t)lrintf(y);
        p.r[k] = r;
        p.g[k] = gr;
        p.b[k] = bl;
    }
    p.count = 3;
    p.flags = XG_PRIM_SEMI;
    p.tpage = 0; /* blend 0: half the picture, half the colour */
    p.area[0] = 0; /* the screen, as the scene's own primitives */
    p.area[1] = 0;
    p.area[2] = (int16_t)(f->screenW - 1);
    p.area[3] = (int16_t)(f->screenH - 1);
    xgVgpuAddPrim(g, &p);
}

/* A colour per walkmesh material (stable, distinct). */
static void materialColour(int material, uint32_t flags, uint8_t* r, uint8_t* g, uint8_t* b) {
    static const uint8_t pal[8][3] = {{80, 200, 120}, {90, 160, 230}, {230, 200, 80}, {200, 110, 220},
                                      {240, 140, 90}, {120, 220, 220}, {180, 230, 100}, {230, 110, 140}};
    const uint8_t* c = pal[material & 7];
    *r = c[0];
    *g = c[1];
    *b = c[2];
    (void)flags;
}

static void drawOverlays(XgField* f, XgVgpu* g, unsigned layers, int selected) {
    const XgFieldInfo* in = &f->info;
    if (layers & XG_FIELD_WALKMESH) {
        for (int l = 0; l < in->layers; l++) {
            for (int i = 0; i < in->tris[l]; i++) {
                const XgFieldTri* t = &in->walk[l][i];
                float v[3][3];
                for (int k = 0; k < 3; k++) {
                    for (int a = 0; a < 3; a++) {
                        v[k][a] = t->v[k][a];
                    }
                }
                uint8_t r, gr, b;
                materialColour(t->material, t->flags, &r, &gr, &b);
                if (layers & XG_FIELD_FILLED) {
                    addTriangle(f, g, v, (uint8_t)(r / 2), (uint8_t)(gr / 2), (uint8_t)(b / 2));
                }
                for (int k = 0; k < 3; k++) {
                    /* an outer edge (no neighbour) brighter */
                    int outer = t->neighbour[k] < 0;
                    int k2 = (k + 1) % 3;
                    if (outer) {
                        addLine(f, g, v[k], v[k2], 255, 255, 255);
                    } else {
                        addLine(f, g, v[k], v[k2], r, gr, b);
                    }
                }
            }
        }
    }
    if (layers & (XG_FIELD_TRIGGERS | XG_FIELD_EXITS)) {
        for (int i = 0; i < in->triggers; i++) {
            int exit = 0;
            for (int x = 0; x < in->exits; x++) {
                exit |= in->exit[x].trigger == i;
            }
            if (!(layers & XG_FIELD_TRIGGERS) && !exit) {
                continue;
            }
            const XgFieldTrigger* t = &in->trigger[i];
            uint8_t r = exit ? 255 : 255, gr = exit ? 70 : 200, b = exit ? 70 : 40;
            for (int k = 0; k < 4; k++) {
                const int16_t* a = t->c[k];
                const int16_t* c = t->c[(k + 1) % 4];
                /* the zone's floor, at corner 0's height (the game's test) */
                float pa[3] = {a[0], t->c[0][1], a[2]}, pb[3] = {c[0], t->c[0][1], c[2]};
                addLine(f, g, pa, pb, r, gr, b);
            }
        }
    }
    if (layers & (XG_FIELD_MARKERS | XG_FIELD_EXITS)) {
        for (int i = 0; i < in->objects; i++) {
            const XgFieldActor* a = &in->actor[i];
            int exit = 0;
            for (int x = 0; x < in->exits; x++) {
                exit |= in->exit[x].actor == i && in->exit[x].trigger < 0;
            }
            int sel = i == selected;
            if (!sel && !(layers & XG_FIELD_MARKERS) && !((layers & XG_FIELD_EXITS) && exit)) {
                continue;
            }
            if (!a->script && !sel) {
                continue;
            }
            uint8_t r = 255, gr = 220, b = 255;
            if (sel) {
                gr = 255, b = 80;
            } else if (exit && (layers & XG_FIELD_EXITS)) {
                gr = 70, b = 70;
            } else if (a->hidden) {
                r = gr = b = 120;
            }
            float s = sel ? MARK_SIZE * 1.5f : MARK_SIZE;
            float x = (float)a->pos[0], y = (float)a->pos[1], z = (float)a->pos[2];
            float c[4][3] = {{x - s, y, z - s}, {x + s, y, z - s}, {x + s, y, z + s}, {x - s, y, z + s}};
            for (int k = 0; k < 4; k++) {
                addLine(f, g, c[k], c[(k + 1) % 4], r, gr, b);
            }
            /* a post up to about a character's height (y is down) */
            float top[3] = {x, y - 4 * s, z};
            float base[3] = {x, y, z};
            addLine(f, g, base, top, r, gr, b);
            /* its facing */
            if (a->script) {
                float ang = (float)a->direction * 6.2831853f / 4096.0f;
                float tip[3] = {x + sinf(ang) * s * 2, y, z - cosf(ang) * s * 2};
                addLine(f, g, base, tip, r, gr, b);
            }
        }
    }
}

int xgFieldDrawGame(XgField* f, unsigned layers, int animate, int selected) {
    XgVgpu* g = xgSandboxGpu(f->sb);
    enter(f);
    xgVgpuClearPrims(g);
    xgVgpuSetArea(g, 0, 0, GAME_W - 1, GAME_H - 1, 0, 0);
    mapsFieldGameCamera();
    int H = f->info.scrz > 0 ? f->info.scrz : 0x200;
    keepProjection(f, GAME_W, GAME_H, (float)H, GAME_W / 2, GAME_H / 2);
    mapsFieldDraw(layers | (animate ? XG_FIELD_ANIMATE : 0), GAME_W, GAME_H, H, 2, -1);
    drawOverlays(f, g, layers, selected);
    leave(f);
    return xgVgpuPrimCount(g);
}

int xgFieldDrawFree(XgField* f, const XgFieldCamera* cam, int w, int h, unsigned layers, int animate, int selected) {
    XgVgpu* g = xgSandboxGpu(f->sb);
    w = w < 64 ? 64 : w > 1024 ? 1024 : w;
    h = h < 64 ? 64 : h > 1024 ? 1024 : h;
    /* the projection distance for the field of view on this screen (the
     * game's: its own on a 320 x 224 screen, scaled) */
    float H = cam->fov > 1.0f ? (float)h / 2.0f / tanf(cam->fov * 3.14159265f / 360.0f)
                              : (float)(f->info.scrz > 0 ? f->info.scrz : 0x200) * (float)h / GAME_H;
    H = H < 16 ? 16 : H > 4000 ? 4000 : H;
    /* The camera's rotation as lookAtDivided makes them (rows: right, down,
     * forward); its projection the field side makes (mapsFieldFreeCamera). */
    float fwd[3] = {cam->at[0] - cam->eye[0], cam->at[1] - cam->eye[1], cam->at[2] - cam->eye[2]};
    float len = sqrtf(fwd[0] * fwd[0] + fwd[1] * fwd[1] + fwd[2] * fwd[2]);
    if (len < 1e-3f) {
        fwd[0] = 0, fwd[1] = 0, fwd[2] = 1, len = 1;
    }
    for (int k = 0; k < 3; k++) {
        fwd[k] /= len;
    }
    /* right = up x forward with up = +y (the game's m20_cameraUp), then
     * down = forward x right; straight down: north up */
    float right[3] = {fwd[2], 0, -fwd[0]};
    float rl = sqrtf(right[0] * right[0] + right[2] * right[2]);
    if (rl < 1e-4f) {
        right[0] = 1, right[2] = 0, rl = 1;
    }
    right[0] /= rl;
    right[2] /= rl;
    float down[3] = {fwd[1] * right[2] - fwd[2] * right[1], fwd[2] * right[0] - fwd[0] * right[2],
                     fwd[0] * right[1] - fwd[1] * right[0]};
    MATRIX m;
    memset(&m, 0, sizeof(m));
    const float* rows[3] = {right, down, fwd};
    for (int r = 0; r < 3; r++) {
        for (int c = 0; c < 3; c++) {
            m.m[r][c] = (s16)lrintf(rows[r][c] * 4096.0f);
        }
    }
    VECTOR eye = {(s32)lrintf(fminf(fmaxf(cam->eye[0], -32000), 32000) * 65536.0f),
                  (s32)lrintf(fminf(fmaxf(cam->eye[1], -32000), 32000) * 65536.0f),
                  (s32)lrintf(fminf(fmaxf(cam->eye[2], -32000), 32000) * 65536.0f), 0};
    VECTOR at = {(s32)lrintf(cam->at[0] * 65536.0f), (s32)lrintf(cam->at[1] * 65536.0f),
                 (s32)lrintf(cam->at[2] * 65536.0f), 0};
    s32 tan = (s32)lrintf(atan2f(fwd[2], fwd[0]) * 4096.0f / 6.2831853f) - 0x400;
    enter(f);
    xgVgpuClearPrims(g);
    xgVgpuSetArea(g, 0, 0, w - 1, h - 1, 0, 0);
    mapsFieldFreeCamera(&m, (s32)lrintf(cam->eye[0]), (s32)lrintf(cam->eye[1]), (s32)lrintf(cam->eye[2]), &eye, &at,
                        tan);
    keepProjection(f, w, h, (float)(s32)H, (float)(w / 2), (float)(h / 2));
    /* depth shift 4: any depth the GTE gives stays inside the OT; all of it
     * drawn, the ground OT behind everything (the game's far clip, its
     * linkOTIndex, assumes its own camera) */
    mapsFieldDraw(layers | (animate ? XG_FIELD_ANIMATE : 0), w, h, (s32)H, 4, 0xFFF);
    drawOverlays(f, g, layers, selected);
    mapsFieldGameCamera();
    leave(f);
    return xgVgpuPrimCount(g);
}

void xgFieldEnter(XgField* f) {
    enter(f);
}

void xgFieldLeave(XgField* f) {
    leave(f);
}

const uint16_t* xgFieldVram(XgField* f) {
    return xgVgpuVram(xgSandboxGpu(f->sb));
}

int xgFieldSources(const XgField* f, int* entries, int capacity) {
    return f ? xgHostFilesEntries(f->files, entries, capacity) : 0;
}

int xgFieldVM(XgField* f, int actor, XgFieldVM* out, int vars[1024]) {
    if (!f || !out || !vars) return 0;
    MapsFieldVM snapshot;
    enter(f);
    int ok = mapsFieldVM(actor, &snapshot, vars);
    leave(f);
    if (ok) memcpy(out, &snapshot, sizeof(snapshot));
    return ok;
}

int xgFieldPausedVM(int actor, int* field, int* actors, XgFieldVM* out, int vars[1024]) {
    extern s32 fieldBootedOnce;
    extern s32 fieldMapNumber;
    /* bootMode is a pending transition request; the kernel clears it to 0
     * before entering the chosen mode. The display kind tracks the active one. */
    if (xgSandboxCurrent() || pcWideGetMode() != PC_WIDE_FIELD || !fieldBootedOnce || !rawFieldScriptData || !out || !vars) return 0;
    MapsFieldVM snapshot;
    int ok = mapsFieldVM(actor, &snapshot, vars);
    if (ok) {
        memcpy(out, &snapshot, sizeof(snapshot));
        if (field) *field = fieldMapNumber;
        if (actors) *actors = rawFieldScriptData->m80_numActors;
    }
    return ok;
}
