/*
 * The world map loaded and drawn by the world map overlay's own code, in a
 * sandbox (mapworld.h).
 */

#include "mapworld.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common.h"
#include "game/worldmap/mapsview.h"
#include "hostfiles.h"
#include "maprast.h"
#include "psyq.h"
#include "sandbox.h"
#include "system/world.h"

#define SANDBOX_HEAP (8u << 20) /* as the game's own: larger breaks allocateHeap mode 2 */
#define SCREEN_W XG_WORLD_SCREEN_W
#define SCREEN_H XG_WORLD_SCREEN_H
#define GAME_H 0x100 /* initWorldmapGraphics' projection distance */
#define TOPDOWN_HEIGHT 3000 /* under the landmarks' 0xD80 and the ground's 0xF00 */

#define CENTRE_Y 0x78 /* the walking map's projection centre (worldmapGeometryOffsetY) */

struct XgWorld {
    XgSandbox* sb;
    HwWorld* world;
    XgHostFiles* files;
    XgWorldInfo info;
    int posX, posZ; /* the world map's position (world units) */
    /* the last drawn screen's projection */
    float m[3][3], t[3], h, cy;
};

static void enter(XgWorld* w) {
    xgSandboxEnter(w->sb);
    hwWorldEnter(w->world);
    hwWorldSetSpu(0);
    xgHostFilesAttach(w->files);
}

static void leave(XgWorld* w) {
    xgHostFilesDetach(w->files);
    hwWorldLeave(w->world);
    xgSandboxLeave(w->sb);
}

static void collect(XgWorld* w) {
    XgWorldInfo* in = &w->info;
    MapsWorldView v;
    mapsWorldView(&v);
    in->sizeX = v.sizeX;
    in->sizeZ = v.sizeZ;
    in->config = v.config;
    in->baseFile = v.baseFile;
    in->startX = v.startX;
    in->startZ = v.startZ;
    in->stages = mapsWorldStages(in->stage, 17);
    in->models = v.models > 0 ? v.models : 0;
    in->model = calloc((size_t)(in->models > 0 ? in->models : 1), sizeof(XgWorldModel));
    for (int i = 0; in->model != NULL && i < in->models; i++) {
        XgWorldModel* m = &in->model[i];
        mapsWorldModel(i, &m->x, &m->y, &m->z, &m->hidden);
    }
    int total = v.exits[0] + v.exits[1] + v.exits[2] + v.exits[3];
    in->exit = calloc((size_t)(total > 0 ? total : 1), sizeof(XgWorldExit));
    in->exits = 0;
    for (int t = 0; in->exit != NULL && t < 4; t++) {
        for (int i = 0; i < v.exits[t]; i++) {
            XgWorldExit* e = &in->exit[in->exits];
            if (mapsWorldExit(t, i, &e->x, &e->z, &e->w, &e->d, &e->field, &e->type)) {
                e->table = t;
                in->exits++;
            }
        }
    }
}

XgWorld* xgWorldLoad(XgDisc* disc, int progress, char* why, int whySize) {
    static const char* const modules[] = {"worldmap", NULL};
    XgWorld* w = calloc(1, sizeof(XgWorld));
    if (w == NULL || (w->sb = xgSandboxCreate(modules, SANDBOX_HEAP)) == NULL || (w->world = hwWorldCreate()) == NULL ||
        (w->files = xgHostFilesCreate(disc)) == NULL) {
        snprintf(why, (size_t)whySize, "out of memory");
        xgWorldDestroy(w);
        return NULL;
    }
    w->info.progress = progress < 0 ? 0 : progress;
    enter(w);
    int ok = mapsWorldStart(progress);
    if (ok) {
        collect(w);
        w->posX = w->info.startX;
        w->posZ = w->info.startZ;
        if (w->info.startX == 0 && w->info.startZ == 0 && w->info.exits > 0) {
            /* no start for a new game here: the first exit (a place to go) */
            w->info.startX = w->info.exit[0].x + w->info.exit[0].w / 2;
            w->info.startZ = w->info.exit[0].z + w->info.exit[0].d / 2;
        }
    }
    int reads = xgHostFilesOpened(w->files, NULL);
    leave(w);
    if (!ok) {
        snprintf(why, (size_t)whySize, "the world map did not set up");
        xgWorldDestroy(w);
        return NULL;
    }
    snprintf(w->info.note, sizeof(w->info.note), "%d x %d chunks, %d landmarks, %d exits, %d files read", w->info.sizeX,
             w->info.sizeZ, w->info.models, w->info.exits, reads);
    return w;
}

void xgWorldDestroy(XgWorld* w) {
    if (w == NULL) {
        return;
    }
    xgSandboxDestroy(w->sb);
    hwWorldDestroy(w->world);
    xgHostFilesDestroy(w->files);
    free(w->info.model);
    free(w->info.exit);
    free(w);
}

const XgWorldInfo* xgWorldInfo(const XgWorld* w) {
    return &w->info;
}

XgVgpu* xgWorldGpu(XgWorld* w) {
    return xgSandboxGpu(w->sb);
}

/* ---- drawing ------------------------------------------------------------------------------- */

static float wrapDelta(float d, int size) {
    float s = (float)(size * XG_WORLD_CHUNK);
    if (s <= 0) {
        return d;
    }
    while (d > s / 2) {
        d -= s;
    }
    while (d < -s / 2) {
        d += s;
    }
    return d;
}

/* A world point in the frame the world map draws in (its position's). */
static void toLocal(const XgWorld* w, const float p[3], float out[3]) {
    out[0] = wrapDelta(p[0] - (float)w->posX, w->info.sizeX);
    out[1] = p[1];
    out[2] = -wrapDelta(p[2] - (float)w->posZ, w->info.sizeZ);
}

int xgWorldProject(XgWorld* w, const float p[3], float* x, float* y) {
    float l[3], c[3];
    toLocal(w, p, l);
    for (int r = 0; r < 3; r++) {
        c[r] = w->m[r][0] * l[0] + w->m[r][1] * l[1] + w->m[r][2] * l[2] + w->t[r];
    }
    if (c[2] < 1) {
        return 0;
    }
    *x = SCREEN_W / 2 + w->h * c[0] / c[2];
    *y = w->cy + w->h * c[1] / c[2];
    return 1;
}

/* The camera's rotation rows (right, down, forward) for a view from eye to
 * at (local frame, y down; straight down: north up), as the game's look-at
 * makes them. */
static void lookAt(const float eye[3], const float at[3], MATRIX* m, float rows[3][3]) {
    float f[3] = {at[0] - eye[0], at[1] - eye[1], at[2] - eye[2]};
    float len = sqrtf(f[0] * f[0] + f[1] * f[1] + f[2] * f[2]);
    if (len < 1e-3f) {
        f[0] = 0, f[1] = 1, f[2] = 0, len = 1;
    }
    for (int k = 0; k < 3; k++) {
        f[k] /= len;
    }
    float r[3] = {f[2], 0, -f[0]};
    float rl = sqrtf(r[0] * r[0] + r[2] * r[2]);
    if (rl < 1e-4f) {
        r[0] = 1, r[2] = 0, rl = 1;
    }
    r[0] /= rl;
    r[2] /= rl;
    float d[3] = {f[1] * r[2] - f[2] * r[1], f[2] * r[0] - f[0] * r[2], f[0] * r[1] - f[1] * r[0]};
    memset(m, 0, sizeof(*m));
    for (int c = 0; c < 3; c++) {
        rows[0][c] = r[c];
        rows[1][c] = d[c];
        rows[2][c] = f[c];
    }
    for (int i = 0; i < 3; i++) {
        for (int c = 0; c < 3; c++) {
            m->m[i][c] = (s16)lrintf(rows[i][c] * 4096.0f);
        }
    }
}

static void drawLocal(XgWorld* w, const float eye[3], const float at[3], float H, unsigned layers) {
    MATRIX m;
    float rows[3][3];
    lookAt(eye, at, &m, rows);
    XgVgpu* g = xgSandboxGpu(w->sb);
    xgVgpuClearPrims(g);
    xgVgpuSetArea(g, 0, 0, SCREEN_W - 1, SCREEN_H - 1, 0, 0);
    mapsWorldDraw(&m, (s32)lrintf(eye[0]), (s32)lrintf(eye[1]), (s32)lrintf(eye[2]), (s32)H, layers & 0xFF);
    for (int r = 0; r < 3; r++) {
        for (int c = 0; c < 3; c++) {
            w->m[r][c] = m.m[r][c] / 4096.0f;
        }
        w->t[r] = -(w->m[r][0] * eye[0] + w->m[r][1] * eye[1] + w->m[r][2] * eye[2]);
    }
    w->h = H;
    w->cy = (layers & XG_WORLD_CENTRED) ? SCREEN_H / 2 : CENTRE_Y;
}

static void addLine(XgWorld* w, XgVgpu* g, const float a[3], const float b[3], uint8_t r, uint8_t gr, uint8_t bl) {
    float pa[2], pb[2];
    if (!xgWorldProject(w, a, &pa[0], &pa[1]) || !xgWorldProject(w, b, &pb[0], &pb[1])) {
        return;
    }
    if (fabsf(pa[0]) > 30000 || fabsf(pa[1]) > 30000 || fabsf(pb[0]) > 30000 || fabsf(pb[1]) > 30000) {
        return;
    }
    XgPrim p;
    memset(&p, 0, sizeof(p));
    p.x[0] = (int16_t)lrintf(pa[0]);
    p.y[0] = (int16_t)lrintf(pa[1]);
    p.x[1] = (int16_t)lrintf(pb[0]);
    p.y[1] = (int16_t)lrintf(pb[1]);
    p.r[0] = p.r[1] = r;
    p.g[0] = p.g[1] = gr;
    p.b[0] = p.b[1] = bl;
    p.count = 2;
    p.flags = XG_PRIM_LINE;
    p.area[0] = p.area[1] = 0; /* the screen, as the world map's own primitives */
    p.area[2] = SCREEN_W - 1;
    p.area[3] = SCREEN_H - 1;
    xgVgpuAddPrim(g, &p);
}

static void overlays(XgWorld* w, unsigned layers) {
    XgVgpu* g = xgSandboxGpu(w->sb);
    const XgWorldInfo* in = &w->info;
    if (layers & XG_WORLD_EXITS) {
        for (int i = 0; i < in->exits; i++) {
            const XgWorldExit* e = &in->exit[i];
            float x0 = (float)e->x, z0 = (float)e->z, x1 = x0 + (float)e->w, z1 = z0 + (float)e->d;
            float c[4][3] = {{x0, 0, z0}, {x1, 0, z0}, {x1, 0, z1}, {x0, 0, z1}};
            uint8_t gr = e->table == 0 ? 80 : 160;
            for (int k = 0; k < 4; k++) {
                addLine(w, g, c[k], c[(k + 1) % 4], 255, gr, 60);
            }
        }
    }
    if (layers & XG_WORLD_MARKS) {
        for (int i = 0; i < in->models; i++) {
            const XgWorldModel* m = &in->model[i];
            if (m->hidden) {
                continue;
            }
            float s = 96, x = (float)m->x, y = (float)m->y, z = (float)m->z;
            float a[3] = {x - s, y, z}, b[3] = {x + s, y, z}, c[3] = {x, y, z - s}, d[3] = {x, y, z + s};
            addLine(w, g, a, b, 255, 255, 120);
            addLine(w, g, c, d, 255, 255, 120);
        }
    }
}

static void moveTo(XgWorld* w, float x, float z) {
    int nx = (int)lrintf(x), nz = (int)lrintf(z);
    mapsWorldGoTo(nx, nz);
    int sx = w->info.sizeX * XG_WORLD_CHUNK, sz = w->info.sizeZ * XG_WORLD_CHUNK;
    w->posX = sx > 0 ? ((nx % sx) + sx) % sx : nx;
    w->posZ = sz > 0 ? ((nz % sz) + sz) % sz : nz;
}

int xgWorldDraw(XgWorld* w, const float eye[3], const float at[3], float fov, unsigned layers) {
    float H = fov > 1 ? (float)SCREEN_H / 2.0f / tanf(fov * 3.14159265f / 360.0f) : (float)GAME_H;
    H = H < 16 ? 16 : H > 4000 ? 4000 : H;
    enter(w);
    /* the terrain streamed around the target once it is a chunk away */
    float dx = wrapDelta(at[0] - (float)w->posX, w->info.sizeX), dz = wrapDelta(at[2] - (float)w->posZ, w->info.sizeZ);
    if (fabsf(dx) > XG_WORLD_CHUNK || fabsf(dz) > XG_WORLD_CHUNK) {
        moveTo(w, at[0], at[2]);
    }
    float le[3], la[3];
    toLocal(w, eye, le);
    toLocal(w, at, la);
    drawLocal(w, le, la, H, layers);
    overlays(w, layers);
    leave(w);
    return xgVgpuPrimCount(xgSandboxGpu(w->sb));
}

uint32_t* xgWorldTopDown(XgWorld* w, int px, unsigned layers, int* outW, int* outH) {
    const XgWorldInfo* in = &w->info;
    int W = in->sizeX * px, Hh = in->sizeZ * px;
    if (W <= 0 || Hh <= 0 || px <= 0 || (size_t)W * (size_t)Hh > (size_t)16384 * 16384) {
        return NULL;
    }
    uint32_t* out = malloc((size_t)W * (size_t)Hh * 4);
    uint32_t* tile = malloc((size_t)px * (size_t)px * 4);
    if (out == NULL || tile == NULL) {
        free(out);
        free(tile);
        return NULL;
    }
    /* a chunk across the screen's height, seen straight down from
     * TOPDOWN_HEIGHT: H / height = 216 / 0x800 */
    float H = (float)TOPDOWN_HEIGHT * (float)SCREEN_H / (float)XG_WORLD_CHUNK;
    float half = (float)SCREEN_H / 2.0f;
    int keepX = w->posX, keepZ = w->posZ;
    enter(w);
    mapsWorldFlatten(1);
    for (int j = 0; j < in->sizeZ; j++) {
        for (int i = 0; i < in->sizeX; i++) {
            float cx = (float)(i * XG_WORLD_CHUNK + XG_WORLD_CHUNK / 2);
            float cz = (float)(j * XG_WORLD_CHUNK + XG_WORLD_CHUNK / 2);
            moveTo(w, cx, cz);
            float eye[3] = {0, -(float)TOPDOWN_HEIGHT, 0}, at[3] = {0, 0, 0};
            drawLocal(w, eye, at, H, (layers & 0x7F) | XG_WORLD_ALLTILES | XG_WORLD_CENTRED);
            XgVgpu* g = xgSandboxGpu(w->sb);
            xgRasterize(xgVgpuPrims(g), xgVgpuPrimCount(g), xgVgpuVram(g), SCREEN_W / 2 - half, 0,
                        SCREEN_W / 2 + half, (float)SCREEN_H, px, px, 0xFF000000u, 1, tile);
            for (int y = 0; y < px; y++) {
                memcpy(&out[(size_t)(j * px + y) * (size_t)W + (size_t)i * px], &tile[(size_t)y * px], (size_t)px * 4);
            }
        }
    }
    mapsWorldFlatten(0);
    moveTo(w, (float)keepX, (float)keepZ);
    leave(w);
    free(tile);
    *outW = W;
    *outH = Hh;
    return out;
}

int xgWorldSources(const XgWorld* w, int* entries, int capacity) {
    return w ? xgHostFilesEntries(w->files, entries, capacity) : 0;
}
