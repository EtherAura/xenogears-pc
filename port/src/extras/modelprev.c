/*
 * A model archive shown by the game's own model code, in a sandbox
 * (modelprev.h).
 */

#include "modelprev.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common.h"
#include "main/model.h"
#include "pc_convert.h"
#include "psyq.h"
#include "sandbox.h"

/* src/main/model.c */
s32 fieldModelRelocation(Model* model);
void initModel1(ModelBlock* b, u8** start, u8** end);
void initModel2(ModelBlock* b, u8* prims, s32 mode);
s32 submitModelForRendering(ModelBlock* b, u8* prims, OTag* ot, s32 renderMode);
s32 uploadTextureToVram(s32* data, s16 mode, s16 x, s16 y, s16 mode2, s16 clutX, s16 clutY);
MATRIX* RotMatrixYXZ(SVECTOR* r, MATRIX* m);
extern s32 objectClippingMask;
extern s32 screenClippingX;
extern s32 screenClippingY;

/* OT entries: AVSZ's 16-bit depth >> gDepthDivider (2). */
#define PREVIEW_OT 0x4000
#define MAX_PARTS 256

struct XgModelPreview {
    XgSandbox* sb;
    u8* raw;
    ModelBlock* blocks;
    int parts, prims, radius;
    u8* primBuffers[MAX_PARTS];
    OTag* ot;
};

XgModelPreview* xgModelPreviewCreate(const uint8_t* model, int size, const uint8_t* const* textures,
                                     const int* textureSizes, int textureCount, char* why, int whySize) {
    XgModelPreview* m = calloc(1, sizeof(XgModelPreview));
    if (m == NULL || (m->sb = xgSandboxCreate(NULL, 32u << 20)) == NULL) {
        snprintf(why, (size_t)whySize, "out of memory");
        free(m);
        return NULL;
    }
    int parts = size >= 16 ? *(const s32*)model : 0;
    if (parts <= 0 || parts > MAX_PARTS) {
        snprintf(why, (size_t)whySize, "not a model archive");
        xgModelPreviewDestroy(m);
        return NULL;
    }
    xgSandboxEnter(m->sb);
    for (int i = 0; i < textureCount; i++) {
        void* t = xgSandboxCopy(textures[i], (size_t)textureSizes[i]);
        if (t != NULL) {
            uploadTextureToVram(t, 0, 0, 0, 0, 0, 0); /* each record at its own place */
        }
    }
    /* A light for the primitives' set-up (initModel2 mode 1 lights each face
     * once with NormalColor, as a field's static models are): one white
     * light from the upper left front, and some ambient. */
    MATRIX light = {{{-2365, -2365, -2365}, {0, 0, 0}, {0, 0, 0}}, {0, 0, 0}};
    MATRIX colour = {{{3072, 0, 0}, {3072, 0, 0}, {3072, 0, 0}}, {0, 0, 0}};
    SetLightMatrix(&light);
    SetColorMatrix(&colour);
    SetBackColor(56, 56, 64);
    m->raw = xgSandboxCopy(model, (size_t)size);
    if (m->raw != NULL) {
        fieldModelRelocation((Model*)m->raw);
        m->blocks = pcModelBlocks(m->raw);
        m->parts = parts;
        m->ot = xgSandboxAlloc(PREVIEW_OT * sizeof(OTag));
    }
    for (int i = 0; m->blocks != NULL && m->ot != NULL && i < parts; i++) {
        ModelBlock* b = &m->blocks[i];
        u8* second;
        initModel1(b, &m->primBuffers[i], &second);
        if (m->primBuffers[i] == NULL) {
            break;
        }
        initModel2(b, m->primBuffers[i], 1);
        m->prims += b->m4_numPrims;
        for (int k = 0; k < 6; k++) {
            int v = b->m20_boundingBox[k] < 0 ? -b->m20_boundingBox[k] : b->m20_boundingBox[k];
            m->radius = v > m->radius ? v : m->radius;
        }
    }
    int ok = m->ot != NULL && m->parts > 0 && m->primBuffers[m->parts - 1] != NULL;
    xgSandboxLeave(m->sb);
    if (!ok) {
        snprintf(why, (size_t)whySize, "the sandbox's heap is full");
        xgModelPreviewDestroy(m);
        return NULL;
    }
    if (m->radius == 0) {
        m->radius = 1000;
    }
    return m;
}

void xgModelPreviewDestroy(XgModelPreview* m) {
    if (m != NULL) {
        xgSandboxDestroy(m->sb);
        free(m);
    }
}

int xgModelPreviewParts(const XgModelPreview* m) {
    return m->parts;
}

int xgModelPreviewPrims(const XgModelPreview* m) {
    return m->prims;
}

int xgModelPreviewRadius(const XgModelPreview* m) {
    return m->radius;
}

XgVgpu* xgModelPreviewGpu(XgModelPreview* m) {
    return xgSandboxGpu(m->sb);
}

int xgModelPreviewDraw(XgModelPreview* m, int yaw, int pitch, int distance, int w, int h) {
    XgVgpu* gpu = xgSandboxGpu(m->sb);
    xgSandboxEnter(m->sb);
    xgVgpuClearPrims(gpu);
    xgVgpuSetArea(gpu, 0, 0, w - 1, h - 1, 0, 0);
    /* The game's screen for the render loops' tests, whole models (no object
     * culling), a camera looking at the model's origin. */
    objectClippingMask = 0;
    screenClippingX = w;
    screenClippingY = (h - 1) << 16;
    SetGeomOffset(w / 2, h / 2);
    SetGeomScreen(h);
    SVECTOR rot = {(s16)pitch, (s16)yaw, 0, 0};
    MATRIX mat;
    RotMatrixYXZ(&rot, &mat);
    mat.t[0] = 0;
    mat.t[1] = 0;
    mat.t[2] = distance;
    SetRotMatrix(&mat);
    SetTransMatrix(&mat);
    ClearOTagR(m->ot, PREVIEW_OT);
    for (int i = 0; i < m->parts; i++) {
        submitModelForRendering(&m->blocks[i], m->primBuffers[i], m->ot, 0);
    }
    DrawOTag(&m->ot[PREVIEW_OT - 1]);
    xgSandboxLeave(m->sb);
    return xgVgpuPrimCount(gpu);
}
