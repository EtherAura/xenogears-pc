/*
 * The extras' file writers (writers.h): PNG, WAV and glTF (GLB). Our own
 * code; the PNG encoder is SDL3's where the SDL has one.
 */

#include "writers.h"

#include <SDL3/SDL.h>
#include <float.h>
#include <limits.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char lastError[256];

static int fail(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(lastError, sizeof(lastError), fmt, ap);
    va_end(ap);
    return 0;
}

const char* xgWriterError(void) {
    return lastError;
}

static int writeFile(const char* path, const void* data, size_t size) {
    FILE* f = fopen(path, "wb");
    if (f == NULL) {
        return fail("cannot write %s", path);
    }
    size_t n = fwrite(data, 1, size, f);
    if (fclose(f) != 0 || n != size) {
        return fail("cannot write %s", path);
    }
    return 1;
}

/* ---- PNG ------------------------------------------------------------------------------------ */

#if !SDL_VERSION_ATLEAST(3, 4, 0)
/* Our own minimal encoder: RGBA, filter 0, zlib with stored blocks. */
static uint32_t crcTable[256];

static uint32_t crc(uint32_t c, const uint8_t* p, size_t n) {
    if (crcTable[1] == 0) {
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t v = i;
            for (int k = 0; k < 8; k++) {
                v = v & 1 ? 0xEDB88320u ^ (v >> 1) : v >> 1;
            }
            crcTable[i] = v;
        }
    }
    c = ~c;
    for (size_t i = 0; i < n; i++) {
        c = crcTable[(c ^ p[i]) & 0xFF] ^ (c >> 8);
    }
    return ~c;
}

static uint8_t* put32(uint8_t* p, uint32_t v) {
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
    return p + 4;
}

static uint8_t* chunk(uint8_t* p, const char* type, const uint8_t* data, uint32_t n) {
    p = put32(p, n);
    memcpy(p, type, 4);
    if (n > 0) {
        memcpy(p + 4, data, n);
    }
    uint32_t c = crc(0, p, n + 4);
    return put32(p + 4 + n, c);
}

static uint8_t* ownPng(const uint32_t* rgba, int w, int h, size_t* size) {
    size_t raw = (size_t)(w * 4 + 1) * h;
    size_t blocks = (raw + 65534) / 65535;
    size_t zlen = 2 + raw + blocks * 5 + 4;
    uint8_t* z = malloc(zlen);
    uint8_t* out = malloc(8 + 25 + 12 + zlen + 12);
    if (z == NULL || out == NULL) {
        free(z);
        free(out);
        return NULL;
    }
    uint8_t* q = z;
    *q++ = 0x78;
    *q++ = 0x01;
    uint32_t a = 1, b = 0;
    size_t left = raw, pos = 0;
    uint8_t row[65535];
    while (left > 0) {
        size_t n = left < 65535 ? left : 65535;
        *q++ = left == n;
        *q++ = (uint8_t)n;
        *q++ = (uint8_t)(n >> 8);
        *q++ = (uint8_t)~n;
        *q++ = (uint8_t)(~n >> 8);
        for (size_t i = 0; i < n; i++, pos++) {
            size_t y = pos / (size_t)(w * 4 + 1), x = pos % (size_t)(w * 4 + 1);
            uint8_t v = x == 0 ? 0 : (uint8_t)(rgba[y * w + (x - 1) / 4] >> ((x - 1) % 4 * 8));
            row[i] = v;
            a = (a + v) % 65521;
            b = (b + a) % 65521;
        }
        memcpy(q, row, n);
        q += n;
        left -= n;
    }
    q = put32(q, b << 16 | a);
    static const uint8_t sig[8] = {0x89, 'P', 'N', 'G', 13, 10, 26, 10};
    uint8_t ihdr[13];
    put32(ihdr, (uint32_t)w);
    put32(ihdr + 4, (uint32_t)h);
    ihdr[8] = 8;
    ihdr[9] = 6;
    ihdr[10] = ihdr[11] = ihdr[12] = 0;
    uint8_t* p = out;
    memcpy(p, sig, 8);
    p = chunk(p + 8, "IHDR", ihdr, 13);
    p = chunk(p, "IDAT", z, (uint32_t)(q - z));
    p = chunk(p, "IEND", NULL, 0);
    free(z);
    *size = (size_t)(p - out);
    return out;
}
#endif

uint8_t* xgPngEncode(const uint32_t* rgba, int w, int h, size_t* size) {
    if (w <= 0 || h <= 0) {
        fail("empty picture");
        return NULL;
    }
#if SDL_VERSION_ATLEAST(3, 4, 0)
    /* RGBA bytes in memory are SDL's ABGR8888 on a little-endian host. */
    SDL_Surface* s = SDL_CreateSurfaceFrom(w, h, SDL_PIXELFORMAT_RGBA32, (void*)rgba, w * 4);
    SDL_IOStream* io = SDL_IOFromDynamicMem();
    uint8_t* out = NULL;
    if (s != NULL && io != NULL && SDL_SavePNG_IO(s, io, false)) {
        Sint64 n = SDL_TellIO(io);
        void* mem = SDL_GetPointerProperty(SDL_GetIOProperties(io), SDL_PROP_IOSTREAM_DYNAMIC_MEMORY_POINTER, NULL);
        out = n > 0 && mem != NULL ? malloc((size_t)n) : NULL;
        if (out != NULL) {
            memcpy(out, mem, (size_t)n);
            *size = (size_t)n;
        }
    } else {
        fail("PNG: %s", SDL_GetError());
    }
    if (io != NULL) {
        SDL_CloseIO(io);
    }
    SDL_DestroySurface(s);
    return out;
#else
    uint8_t* out = ownPng(rgba, w, h, size);
    if (out == NULL) {
        fail("out of memory");
    }
    return out;
#endif
}

int xgPngWrite(const char* path, const uint32_t* rgba, int w, int h) {
    size_t size;
    uint8_t* png = xgPngEncode(rgba, w, h, &size);
    if (png == NULL) {
        return 0;
    }
    int ok = writeFile(path, png, size);
    free(png);
    return ok;
}

/* ---- WAV ------------------------------------------------------------------------------------ */

static void le32(uint8_t* p, uint32_t v) {
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

static void le16(uint8_t* p, uint16_t v) {
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

int xgWavWrite(const char* path, const int16_t* samples, int frames, int channels, int rate) {
    uint32_t data = (uint32_t)frames * (uint32_t)channels * 2;
    uint8_t h[44];
    memcpy(h, "RIFF", 4);
    le32(h + 4, 36 + data);
    memcpy(h + 8, "WAVEfmt ", 8);
    le32(h + 16, 16);
    le16(h + 20, 1);
    le16(h + 22, (uint16_t)channels);
    le32(h + 24, (uint32_t)rate);
    le32(h + 28, (uint32_t)rate * (uint32_t)channels * 2);
    le16(h + 32, (uint16_t)(channels * 2));
    le16(h + 34, 16);
    memcpy(h + 36, "data", 4);
    le32(h + 40, data);
    FILE* f = fopen(path, "wb");
    if (f == NULL) {
        return fail("cannot write %s", path);
    }
    int ok = fwrite(h, 1, 44, f) == 44;
    for (int i = 0; ok && i < frames * channels; i++) {
        uint8_t s[2];
        le16(s, (uint16_t)samples[i]); /* little-endian whatever the host */
        ok = fwrite(s, 1, 2, f) == 2;
    }
    if (fclose(f) != 0 || !ok) {
        return fail("cannot write %s", path);
    }
    return 1;
}

/* ---- glTF ----------------------------------------------------------------------------------- */

#define GL_FLOAT 5126
#define GL_UNSIGNED_INT 5125
#define GL_ARRAY_BUFFER 34962
#define GL_ELEMENT_ARRAY_BUFFER 34963

typedef struct {
    char* p;
    size_t n, cap;
    int bad;
} Text;

static void put(Text* t, const char* fmt, ...) {
    va_list ap;
    for (;;) {
        va_start(ap, fmt);
        int k = vsnprintf(t->p != NULL ? t->p + t->n : NULL, t->p != NULL ? t->cap - t->n : 0, fmt, ap);
        va_end(ap);
        if (k < 0) {
            t->bad = 1;
            return;
        }
        if (t->p != NULL && t->n + (size_t)k < t->cap) {
            t->n += (size_t)k;
            return;
        }
        size_t cap = (t->cap + (size_t)k + 1) * 2;
        char* p = realloc(t->p, cap);
        if (p == NULL) {
            t->bad = 1;
            return;
        }
        t->p = p;
        t->cap = cap;
    }
}

/* A name as a JSON string's contents. */
static void putName(Text* t, const char* s) {
    put(t, "\"");
    for (; *s != '\0'; s++) {
        if (*s == '"' || *s == '\\') {
            put(t, "\\%c", *s);
        } else if ((unsigned char)*s < 0x20) {
            put(t, "\\u%04x", (unsigned char)*s);
        } else {
            put(t, "%c", *s);
        }
    }
    put(t, "\"");
}

typedef struct {
    size_t offset, length;
    int target;
} View;

typedef struct {
    int view, componentType, count, components;
    int minMax; /* write min and max (positions, animation times) */
    float min[4], max[4];
} Accessor;

typedef struct {
    char name[64];
    int image; /* buffer view of the PNG, -1 none */
    int blend;
} Material;

typedef struct {
    int material, position, uv, colour, indices;
    int* targets;
    int targetCount, targetCap;
} Primitive;

typedef struct {
    char name[64];
    Primitive* prims; /* one per material (xgGltfAddPrimitive) */
    int primCount, primCap;
    float* weights;
    int weightCount;
} Mesh;

typedef struct {
    char name[64];
    int mesh, parent;
    float t[3], r[4], s[3];
    int hasT, hasR, hasS;
} Node;

typedef struct {
    int node;
    char path;
    int input, output;
    int step;
} Channel;

typedef struct {
    char name[64];
    Channel* channels;
    int count, cap;
} Animation;

#define GROW(arr, count, cap)                                                   \
    ((count) < (cap) || ((cap) = (cap) ? (cap) * 2 : 8,                         \
                         (arr) = realloc((arr), (size_t)(cap) * sizeof(*(arr))), \
                         (arr) != NULL))

struct XgGltf {
    uint8_t* bin;
    size_t binSize, binCap;
    View* views;
    int viewCount, viewCap;
    Accessor* accessors;
    int accessorCount, accessorCap;
    Material* materials;
    int materialCount, materialCap;
    Mesh* meshes;
    int meshCount, meshCap;
    Node* nodes;
    int nodeCount, nodeCap;
    Animation* animations;
    int animationCount, animationCap;
};

XgGltf* xgGltfCreate(void) {
    return calloc(1, sizeof(XgGltf));
}

void xgGltfDestroy(XgGltf* g) {
    if (g == NULL) {
        return;
    }
    for (int i = 0; i < g->animationCount; i++) {
        free(g->animations[i].channels);
    }
    free(g->bin);
    free(g->views);
    free(g->accessors);
    free(g->materials);
    for (int i = 0; i < g->meshCount; i++) {
        for (int k = 0; k < g->meshes[i].primCount; k++) {
            free(g->meshes[i].prims[k].targets);
        }
        free(g->meshes[i].prims);
        free(g->meshes[i].weights);
    }
    free(g->meshes);
    free(g->nodes);
    free(g->animations);
    free(g);
}

/* Bytes into the binary chunk, 4-byte aligned, as a buffer view. */
static int addView(XgGltf* g, const void* data, size_t size, int target) {
    size_t at = (g->binSize + 3) & ~(size_t)3;
    if (at + size > g->binCap) {
        size_t cap = (at + size) * 2 + 1024;
        uint8_t* b = realloc(g->bin, cap);
        if (b == NULL) {
            return -1;
        }
        g->bin = b;
        g->binCap = cap;
    }
    memset(g->bin + g->binSize, 0, at - g->binSize);
    memcpy(g->bin + at, data, size);
    g->binSize = at + size;
    if (!GROW(g->views, g->viewCount, g->viewCap)) {
        return -1;
    }
    g->views[g->viewCount] = (View){at, size, target};
    return g->viewCount++;
}

static int addAccessor(XgGltf* g, const void* data, int count, int components, int componentType, int target,
                       int minMax) {
    if (count <= 0) {
        return -1;
    }
    int view = addView(g, data, (size_t)count * components * 4, target);
    if (view < 0 || !GROW(g->accessors, g->accessorCount, g->accessorCap)) {
        return -1;
    }
    Accessor* a = &g->accessors[g->accessorCount];
    memset(a, 0, sizeof(*a));
    a->view = view;
    a->componentType = componentType;
    a->count = count;
    a->components = components;
    a->minMax = minMax;
    if (minMax) {
        const float* f = data;
        for (int c = 0; c < components; c++) {
            a->min[c] = FLT_MAX;
            a->max[c] = -FLT_MAX;
        }
        for (int i = 0; i < count; i++) {
            for (int c = 0; c < components; c++) {
                float v = f[i * components + c];
                a->min[c] = v < a->min[c] ? v : a->min[c];
                a->max[c] = v > a->max[c] ? v : a->max[c];
            }
        }
    }
    return g->accessorCount++;
}

static void copyName(char* out, const char* name) {
    snprintf(out, 64, "%s", name != NULL ? name : "");
}

int xgGltfAddTexturedMaterial(XgGltf* g, const char* name, const uint8_t* png, size_t pngSize, int blend) {
    int view = addView(g, png, pngSize, 0);
    if (view < 0 || !GROW(g->materials, g->materialCount, g->materialCap)) {
        return -1;
    }
    Material* m = &g->materials[g->materialCount];
    copyName(m->name, name);
    m->image = view;
    m->blend = blend;
    return g->materialCount++;
}

void xgGltfSetBlend(XgGltf* g, int material, int blend) {
    if (material >= 0 && material < g->materialCount) {
        g->materials[material].blend = blend;
    }
}

int xgGltfAddColourMaterial(XgGltf* g, const char* name) {
    if (!GROW(g->materials, g->materialCount, g->materialCap)) {
        return -1;
    }
    Material* m = &g->materials[g->materialCount];
    copyName(m->name, name);
    m->image = -1;
    m->blend = 0;
    return g->materialCount++;
}

int xgGltfAddPrimitive(XgGltf* g, int mesh, int material, const float* positions, const float* uvs,
                       const float* colours, int vertices, const uint32_t* indices, int indexCount) {
    if (mesh < 0 || mesh >= g->meshCount) {
        return -1;
    }
    Mesh* m = &g->meshes[mesh];
    if (!GROW(m->prims, m->primCount, m->primCap)) {
        return -1;
    }
    Primitive* p = &m->prims[m->primCount];
    memset(p, 0, sizeof(*p));
    p->material = material;
    p->position = addAccessor(g, positions, vertices, 3, GL_FLOAT, GL_ARRAY_BUFFER, 1);
    p->uv = uvs != NULL ? addAccessor(g, uvs, vertices, 2, GL_FLOAT, GL_ARRAY_BUFFER, 0) : -1;
    p->colour = colours != NULL ? addAccessor(g, colours, vertices, 4, GL_FLOAT, GL_ARRAY_BUFFER, 0) : -1;
    p->indices = addAccessor(g, indices, indexCount, 1, GL_UNSIGNED_INT, GL_ELEMENT_ARRAY_BUFFER, 0);
    if (p->position < 0 || p->indices < 0) {
        return -1;
    }
    return m->primCount++;
}

int xgGltfAddMorphTarget(XgGltf* g, int mesh, int primitive, const float* deltas, int vertices) {
    if (g == NULL || mesh < 0 || mesh >= g->meshCount || primitive < 0 ||
        primitive >= g->meshes[mesh].primCount || deltas == NULL || vertices <= 0) return -1;
    Primitive* p = &g->meshes[mesh].prims[primitive];
    if (vertices != g->accessors[p->position].count || !GROW(p->targets, p->targetCount, p->targetCap)) return -1;
    int a = addAccessor(g, deltas, vertices, 3, GL_FLOAT, GL_ARRAY_BUFFER, 1);
    if (a < 0) return -1;
    p->targets[p->targetCount] = a;
    return p->targetCount++;
}

int xgGltfSetMorphWeights(XgGltf* g, int mesh, const float* weights, int count) {
    if (g == NULL || mesh < 0 || mesh >= g->meshCount || weights == NULL || count <= 0) return 0;
    Mesh* m = &g->meshes[mesh];
    for (int k = 0; k < m->primCount; k++) if (m->prims[k].targetCount != count) return 0;
    for (int k = 0; k < count; k++) if (!isfinite(weights[k])) return 0;
    float* copy = malloc(sizeof(float) * (size_t)count);
    if (copy == NULL) return 0;
    memcpy(copy, weights, sizeof(float) * (size_t)count);
    free(m->weights);
    m->weights = copy;
    m->weightCount = count;
    return 1;
}

int xgGltfAddEmptyMesh(XgGltf* g, const char* name) {
    if (!GROW(g->meshes, g->meshCount, g->meshCap)) {
        return -1;
    }
    Mesh* m = &g->meshes[g->meshCount];
    memset(m, 0, sizeof(*m));
    copyName(m->name, name);
    return g->meshCount++;
}

int xgGltfAddMesh(XgGltf* g, const char* name, int material, const float* positions, const float* uvs,
                  const float* colours, int vertices, const uint32_t* indices, int indexCount) {
    int mesh = xgGltfAddEmptyMesh(g, name);
    if (mesh < 0 || xgGltfAddPrimitive(g, mesh, material, positions, uvs, colours, vertices, indices, indexCount) < 0) {
        if (mesh >= 0) {
            free(g->meshes[mesh].prims);
            g->meshCount--;
        }
        return -1;
    }
    return mesh;
}

int xgGltfAddNode(XgGltf* g, const char* name, int mesh, int parent, const float* t, const float* r,
                  const float* s) {
    if (parent >= g->nodeCount || !GROW(g->nodes, g->nodeCount, g->nodeCap)) {
        return -1;
    }
    Node* n = &g->nodes[g->nodeCount];
    memset(n, 0, sizeof(*n));
    copyName(n->name, name);
    n->mesh = mesh;
    n->parent = parent;
    if (t != NULL) {
        memcpy(n->t, t, sizeof(n->t));
        n->hasT = 1;
    }
    if (r != NULL) {
        memcpy(n->r, r, sizeof(n->r));
        n->hasR = 1;
    }
    if (s != NULL) {
        memcpy(n->s, s, sizeof(n->s));
        n->hasS = 1;
    }
    return g->nodeCount++;
}

int xgGltfAddAnimation(XgGltf* g, const char* name) {
    if (!GROW(g->animations, g->animationCount, g->animationCap)) {
        return -1;
    }
    Animation* a = &g->animations[g->animationCount];
    memset(a, 0, sizeof(*a));
    copyName(a->name, name);
    return g->animationCount++;
}

int xgGltfAddChannel(XgGltf* g, int animation, int node, char path, const float* times, const float* values,
                     int count) {
    if (animation < 0 || animation >= g->animationCount || node < 0 || node >= g->nodeCount ||
        (path != 't' && path != 'r' && path != 's')) {
        return -1;
    }
    Animation* a = &g->animations[animation];
    if (!GROW(a->channels, a->count, a->cap)) {
        return -1;
    }
    int input = addAccessor(g, times, count, 1, GL_FLOAT, 0, 1);
    int output = addAccessor(g, values, count, path == 'r' ? 4 : 3, GL_FLOAT, 0, 0);
    if (input < 0 || output < 0) {
        return -1;
    }
    a->channels[a->count] = (Channel){node, path, input, output};
    return a->count++;
}

int xgGltfAddStepChannel(XgGltf* g, int animation, int node, char path, const float* times, const float* values,
                         int count) {
    int index = xgGltfAddChannel(g, animation, node, path, times, values, count);
    if (index >= 0) g->animations[animation].channels[index].step = 1;
    return index;
}

int xgGltfAddWeightsChannel(XgGltf* g, int animation, int node, const float* times, const float* values,
                            int keys, int targets) {
    if (g == NULL || animation < 0 || animation >= g->animationCount || node < 0 || node >= g->nodeCount ||
        keys <= 0 || targets <= 0 || keys > INT_MAX / targets || times == NULL || values == NULL) return -1;
    int mesh = g->nodes[node].mesh;
    if (mesh < 0 || mesh >= g->meshCount || g->meshes[mesh].weightCount != targets) return -1;
    for (int k = 0; k < keys; k++) {
        if (!isfinite(times[k]) || times[k] < 0 || (k > 0 && times[k] <= times[k - 1])) return -1;
    }
    for (int k = 0; k < keys * targets; k++) if (!isfinite(values[k])) return -1;
    Animation* a = &g->animations[animation];
    if (!GROW(a->channels, a->count, a->cap)) return -1;
    int input = addAccessor(g, times, keys, 1, GL_FLOAT, 0, 1);
    int output = addAccessor(g, values, keys * targets, 1, GL_FLOAT, 0, 0);
    if (input < 0 || output < 0) return -1;
    a->channels[a->count] = (Channel){node, 'w', input, output};
    return a->count++;
}

static void putFloats(Text* t, const float* v, int n) {
    put(t, "[");
    for (int i = 0; i < n; i++) {
        put(t, "%s%.9g", i ? "," : "", (double)v[i]);
    }
    put(t, "]");
}

static void json(XgGltf* g, Text* t) {
    static const char* const types[] = {"", "SCALAR", "VEC2", "VEC3", "VEC4"};
    put(t, "{\"asset\":{\"version\":\"2.0\",\"generator\":\"Xenogears PC port extras\"}");
    put(t, ",\"extensionsUsed\":[\"KHR_materials_unlit\"]");
    put(t, ",\"scene\":0,\"scenes\":[{\"nodes\":[");
    for (int i = 0, first = 1; i < g->nodeCount; i++) {
        if (g->nodes[i].parent < 0) {
            put(t, "%s%d", first ? "" : ",", i);
            first = 0;
        }
    }
    put(t, "]}]");
    if (g->nodeCount > 0) {
        put(t, ",\"nodes\":[");
        for (int i = 0; i < g->nodeCount; i++) {
            Node* n = &g->nodes[i];
            put(t, "%s{\"name\":", i ? "," : "");
            putName(t, n->name);
            if (n->mesh >= 0) {
                put(t, ",\"mesh\":%d", n->mesh);
            }
            int children = 0;
            for (int k = 0; k < g->nodeCount; k++) {
                if (g->nodes[k].parent == i) {
                    put(t, "%s%d", children++ ? "," : ",\"children\":[", k);
                }
            }
            if (children) {
                put(t, "]");
            }
            if (n->hasT) {
                put(t, ",\"translation\":");
                putFloats(t, n->t, 3);
            }
            if (n->hasR) {
                put(t, ",\"rotation\":");
                putFloats(t, n->r, 4);
            }
            if (n->hasS) {
                put(t, ",\"scale\":");
                putFloats(t, n->s, 3);
            }
            put(t, "}");
        }
        put(t, "]");
    }
    if (g->meshCount > 0) {
        put(t, ",\"meshes\":[");
        for (int i = 0; i < g->meshCount; i++) {
            Mesh* m = &g->meshes[i];
            put(t, "%s{\"name\":", i ? "," : "");
            putName(t, m->name);
            put(t, ",\"primitives\":[");
            for (int k = 0; k < m->primCount; k++) {
                Primitive* p = &m->prims[k];
                put(t, "%s{\"attributes\":{\"POSITION\":%d", k ? "," : "", p->position);
                if (p->uv >= 0) {
                    put(t, ",\"TEXCOORD_0\":%d", p->uv);
                }
                if (p->colour >= 0) {
                    put(t, ",\"COLOR_0\":%d", p->colour);
                }
                put(t, "},\"indices\":%d,\"mode\":4", p->indices);
                if (p->material >= 0) {
                    put(t, ",\"material\":%d", p->material);
                }
                if (p->targetCount > 0) {
                    put(t, ",\"targets\":[");
                    for (int j = 0; j < p->targetCount; j++) {
                        put(t, "%s{\"POSITION\":%d}", j ? "," : "", p->targets[j]);
                    }
                    put(t, "]");
                }
                put(t, "}");
            }
            put(t, "]");
            if (m->weightCount > 0) {
                put(t, ",\"weights\":");
                putFloats(t, m->weights, m->weightCount);
            }
            put(t, "}");
        }
        put(t, "]");
    }
    int textures = 0;
    if (g->materialCount > 0) {
        put(t, ",\"materials\":[");
        for (int i = 0; i < g->materialCount; i++) {
            Material* m = &g->materials[i];
            put(t, "%s{\"name\":", i ? "," : "");
            putName(t, m->name);
            put(t, ",\"pbrMetallicRoughness\":{");
            if (m->image >= 0) {
                put(t, "\"baseColorTexture\":{\"index\":%d},", textures++);
            }
            put(t, "\"metallicFactor\":0,\"roughnessFactor\":1}");
            if (m->image >= 0) {
                put(t, m->blend ? ",\"alphaMode\":\"BLEND\"" : ",\"alphaMode\":\"MASK\",\"alphaCutoff\":0.5");
            } else if (m->blend) {
                put(t, ",\"alphaMode\":\"BLEND\""); /* the vertex colours' alpha */
            }
            put(t, ",\"doubleSided\":true,\"extensions\":{\"KHR_materials_unlit\":{}}}");
        }
        put(t, "]");
    }
    if (textures > 0) {
        put(t, ",\"samplers\":[{\"magFilter\":9728,\"minFilter\":9728,\"wrapS\":10497,\"wrapT\":10497}]");
        put(t, ",\"textures\":[");
        for (int i = 0; i < textures; i++) {
            put(t, "%s{\"sampler\":0,\"source\":%d}", i ? "," : "", i);
        }
        put(t, "],\"images\":[");
        for (int i = 0, k = 0; i < g->materialCount; i++) {
            if (g->materials[i].image >= 0) {
                put(t, "%s{\"bufferView\":%d,\"mimeType\":\"image/png\"}", k++ ? "," : "", g->materials[i].image);
            }
        }
        put(t, "]");
    }
    if (g->animationCount > 0) {
        put(t, ",\"animations\":[");
        for (int i = 0; i < g->animationCount; i++) {
            Animation* a = &g->animations[i];
            static const char* const paths[] = {"translation", "rotation", "scale"};
            put(t, "%s{\"name\":", i ? "," : "");
            putName(t, a->name);
            put(t, ",\"channels\":[");
            for (int k = 0; k < a->count; k++) {
                Channel* c = &a->channels[k];
                put(t, "%s{\"sampler\":%d,\"target\":{\"node\":%d,\"path\":\"%s\"}}", k ? "," : "", k, c->node,
                    c->path == 'w' ? "weights" : paths[c->path == 't' ? 0 : c->path == 'r' ? 1 : 2]);
            }
            put(t, "],\"samplers\":[");
            for (int k = 0; k < a->count; k++) {
                put(t, "%s{\"input\":%d,\"output\":%d,\"interpolation\":\"%s\"}", k ? "," : "",
                    a->channels[k].input, a->channels[k].output,
                    a->channels[k].step || a->channels[k].path == 'w' ? "STEP" : "LINEAR");
            }
            put(t, "]}");
        }
        put(t, "]");
    }
    if (g->accessorCount > 0) {
        put(t, ",\"accessors\":[");
        for (int i = 0; i < g->accessorCount; i++) {
            Accessor* a = &g->accessors[i];
            put(t, "%s{\"bufferView\":%d,\"componentType\":%d,\"count\":%d,\"type\":\"%s\"", i ? "," : "", a->view,
                a->componentType, a->count, types[a->components]);
            if (a->minMax) {
                put(t, ",\"min\":");
                putFloats(t, a->min, a->components);
                put(t, ",\"max\":");
                putFloats(t, a->max, a->components);
            }
            put(t, "}");
        }
        put(t, "]");
    }
    if (g->viewCount > 0) {
        put(t, ",\"bufferViews\":[");
        for (int i = 0; i < g->viewCount; i++) {
            View* v = &g->views[i];
            put(t, "%s{\"buffer\":0,\"byteOffset\":%zu,\"byteLength\":%zu", i ? "," : "", v->offset, v->length);
            if (v->target != 0) {
                put(t, ",\"target\":%d", v->target);
            }
            put(t, "}");
        }
        put(t, "],\"buffers\":[{\"byteLength\":%zu}]", (g->binSize + 3) & ~(size_t)3);
    }
    put(t, "}");
}

int xgGltfWriteGlb(XgGltf* g, const char* path) {
    for (int i = 0; i < g->meshCount; i++) {
        Mesh* m = &g->meshes[i];
        for (int k = 0; k < m->primCount; k++) {
            if (m->prims[k].targetCount != m->weightCount) return fail("inconsistent morph target count in mesh %d", i);
        }
    }
    Text t = {0};
    json(g, &t);
    if (t.bad) {
        free(t.p);
        return fail("out of memory");
    }
    size_t jsonLen = (t.n + 3) & ~(size_t)3;
    size_t binLen = (g->binSize + 3) & ~(size_t)3;
    size_t total = 12 + 8 + jsonLen + (binLen > 0 ? 8 + binLen : 0);
    uint8_t* out = calloc(1, total);
    if (out == NULL) {
        free(t.p);
        return fail("out of memory");
    }
    le32(out, 0x46546C67u); /* "glTF" */
    le32(out + 4, 2);
    le32(out + 8, (uint32_t)total);
    le32(out + 12, (uint32_t)jsonLen);
    le32(out + 16, 0x4E4F534Au); /* "JSON" */
    memcpy(out + 20, t.p, t.n);
    memset(out + 20 + t.n, ' ', jsonLen - t.n);
    if (binLen > 0) {
        uint8_t* b = out + 20 + jsonLen;
        le32(b, (uint32_t)binLen);
        le32(b + 4, 0x004E4942u); /* "BIN\0" */
        memcpy(b + 8, g->bin, g->binSize);
    }
    free(t.p);
    int ok = writeFile(path, out, total);
    free(out);
    return ok;
}
