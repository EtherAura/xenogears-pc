/* Own native simulation and displayed frames separately. Native casting and
 * loading routines can call battleRender recursively; every drawn frame is
 * queued, including its VRAM changes and PCM, and consumed at native timing. */
#include "battle_preview.h"
#include "hostfiles.h"
#include "preview.h"
#include "sandbox.h"
#include "raster.h"
#include "sprites_enemy.h"
#include "battlefx_decode.h"
#include <setjmp.h>
#include "system/world.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

int battlePreviewStart(int character, int arena, int enemySet, int gear);
void battlePreviewEnemy(int id, int gear);
int battlePreviewPlay(int slot, int target, int kind, int id, int program, int full);
int battlePreviewTurn(int slot, int target, const unsigned char *actions, int count, int full);
int battlePreviewInfo(int slot, int *out);
int battlePreviewCommands(int slot, int *types, int *flags);
int battlePreviewActive(void);
int battlePreviewFrame(void);
int battlePreviewPresentation(int, int);
void battlePreviewDump(void);
void battlePreviewCapture(void (*capture)(void *, int, int), void *data);
struct Change {
    uint32_t index;
    uint16_t value;
};
struct Frame {
    struct Frame *next;
    XgPrim *prims;
    struct Change *changes;
    int16_t *pcm;
    int primitives, changed, samples, screenY;
};
struct XgBattlePreview {
    XgDisc *disc;
    XgSandbox *sb;
    HwWorld *world;
    XgHostFiles *files;
    XgVgpu *display;
    uint16_t previous[1024 * 512];
    int16_t pcm[44100 * 2];
    uint32_t pixels[320 * 224];
    jmp_buf abort;
    int samples, screenY, audible, budget, failed, raster;
    struct Frame *first, *last;
};
static void sink(void *user, const int16_t *pcm, uint32_t frames) {
    XgBattlePreview *p = user;
    int count = (int)frames;
    if (count > 44100 - p->samples)
        count = 44100 - p->samples;
    memcpy(p->pcm + p->samples * 2, pcm, (size_t)count * 4);
    p->samples += count;
}
static void capture(void *user, int finish, int screenY) {
    XgBattlePreview *p = user;
    XgVgpu *gpu = xgSandboxGpu(p->sb);
    if (!finish) {
        xgVgpuClearPrims(gpu);
        return;
    }
    if (--p->budget < 0) {
        p->failed = 1;
        longjmp(p->abort, 1);
    }
    /* Screen warps and captures read the native double-buffered picture.
     * Rasterize before collecting VRAM deltas, so later StoreImage reads
     * the same pixels that were shown, including nested casting frames. */
    if (p->raster) {
        xgVgpuRaster(gpu, 0, screenY, 320, 224, 1, 0xFF000000, p->pixels);
        uint16_t *vram = xgVgpuVram(gpu);
        for (int y = 0; y < 224; y++)
            for (int x = 0; x < 320; x++) {
                uint32_t c = p->pixels[y * 320 + x];
                vram[(y + screenY) * 1024 + x] = ((c >> 3) & 31) | ((c >> 6) & 0x3E0) | ((c >> 9) & 0x7C00);
            }
        xgVgpuVramChanged(gpu);
    }
    struct Frame *f = calloc(1, sizeof(*f));
    if (!f) {
        p->failed = 1;
        longjmp(p->abort, 1);
    }
    f->primitives = xgVgpuPrimCount(gpu);
    f->screenY = screenY;
    f->samples = p->samples;
    f->prims = malloc((size_t)f->primitives * sizeof(XgPrim));
    f->pcm = malloc((size_t)f->samples * 4);
    uint16_t *vram = xgVgpuVram(gpu);
    for (int i = 0; i < 1024 * 512; i++)
        f->changed += vram[i] != p->previous[i];
    f->changes = malloc((size_t)f->changed * sizeof(struct Change));
    if ((f->primitives && !f->prims) || (f->samples && !f->pcm) || (f->changed && !f->changes)) {
        free(f->prims);
        free(f->pcm);
        free(f->changes);
        free(f);
        p->failed = 1;
        longjmp(p->abort, 1);
    }
    memcpy(f->prims, xgVgpuPrims(gpu), (size_t)f->primitives * sizeof(XgPrim));
    memcpy(f->pcm, p->pcm, (size_t)f->samples * 4);
    p->samples = 0;
    int at = 0;
    for (int i = 0; i < 1024 * 512; i++)
        if (vram[i] != p->previous[i]) {
            f->changes[at++] = (struct Change){(uint32_t)i, vram[i]};
            p->previous[i] = vram[i];
        }
    if (p->last)
        p->last->next = f;
    else
        p->first = f;
    p->last = f;
}
static void enter(XgBattlePreview *p) {
    xgSandboxEnter(p->sb);
    hwWorldEnter(p->world);
    hwWorldSetSpu(1);
    hwWorldSetSink(sink, p);
    xgHostFilesAttach(p->files);
}
static void leave(XgBattlePreview *p) {
    xgHostFilesDetach(p->files);
    hwWorldSetSink(NULL, NULL);
    hwWorldLeave(p->world);
    xgSandboxLeave(p->sb);
}
XgBattlePreview *xgBattlePreviewCreate(XgDisc *disc, int character, int arena, int enemySet, int gear,
                                       int enemy) {
    static const char *const modules[] = {"battle",
                                          "battle_loader",
                                          "field_mecha",
                                          "battle_event",
                                          "battle_debug_selector",
                                          "battle_fx_circle_warp",
                                          "battle_fx_sprite_wobble",
                                          "battle_fx_model_shatter",
                                          "battle_fx_tiled_sprite",
                                          "battle_fx_sprite_band",
                                          "battle_fx_screen_ripple",
                                          NULL};
    if (!disc || character < 0 || character > 10 || arena < 0 || arena > 127 || enemySet < 0 ||
        enemySet > 255 || gear < 0 || gear > 2)
        return NULL;
    if (gear && character == 7)
        return NULL;
    int size = 0, entry = xgDiscFileEntry(disc, 0x0D, 3 + 2 * enemySet);
    uint8_t *bytes = xgDiscReadEntry(disc, entry, &size);
    XgEnemySprite sprite;
    int enemyKind = xgEnemySpriteGet(bytes, size, enemy, &sprite);
    free(bytes);
    entry = xgDiscFileEntry(disc, 0x0F, 6 + 2 * arena);
    bytes = xgDiscReadEntry(disc, entry, &size);
    int arenaOk = bytes && size >= 24 && bytes[0] == 5;
    free(bytes);
    if (!enemyKind || !arenaOk)
        return NULL;
    XgBattlePreview *p = calloc(1, sizeof(*p));
    if (!p || !(p->sb = xgSandboxCreate(modules, 8u << 20)) || !(p->world = hwWorldCreate()) ||
        !(p->files = xgHostFilesCreate(disc)) || !(p->display = xgVgpuCreate())) {
        xgBattlePreviewDestroy(p);
        return NULL;
    }
    p->disc = disc;
    xgSandboxZeroHeap(p->sb);
    enter(p);
    hwWorldSetSpu(0);
    battlePreviewEnemy(enemy, enemyKind < 0);
    int ok = battlePreviewStart(character, arena, enemySet, gear);
    p->samples = 0;
    battlePreviewCapture(capture, p);
    leave(p);
    if (!ok) {
        xgBattlePreviewDestroy(p);
        return NULL;
    }
    p->budget = 1200;
    xgBattlePreviewStep(p, 1);
    return p;
}
void xgBattlePreviewDestroy(XgBattlePreview *p) {
    if (!p)
        return;
    while (p->first) {
        struct Frame *f = p->first;
        p->first = f->next;
        free(f->prims);
        free(f->changes);
        free(f->pcm);
        free(f);
    }
    xgSandboxDestroy(p->sb);
    hwWorldDestroy(p->world);
    xgHostFilesDestroy(p->files);
    xgVgpuDestroy(p->display);
    free(p);
}
int xgBattlePreviewPlay(XgBattlePreview *p, int slot, int target, int kind, int id, int program, int full) {
    if (!p || slot < 0 || slot > 10 || target < 0 || target > 10 || kind < 0 || kind > 5 || id < 0 ||
        id > 255 || program < 0 || program > 63)
        return 0;
    if (kind == 2) {
        int size = 0;
        uint8_t *bytes = xgDiscReadEntry(p->disc, xgDiscFileEntry(p->disc, 0x0E, 0x22 + 2 * id), &size);
        int valid = xgBattleFxBundle(bytes, size);
        if (valid) {
            uint32_t at =
                bytes[4] | (uint32_t)bytes[5] << 8 | (uint32_t)bytes[6] << 16 | (uint32_t)bytes[7] << 24;
            valid = program < (bytes[at] & 63);
        }
        free(bytes);
        if (!valid)
            return 0;
    }
    enter(p);
    int info[8], other[8], ok = 0;
    p->budget = 1200;
    /* effects that capture the screen read the shown picture (capture) */
    p->raster = (kind == 2 || kind == 5) && full;
    if (!setjmp(p->abort) && battlePreviewInfo(slot, info) && battlePreviewInfo(target, other) &&
        ((kind == 0 && id < info[2]) || (kind == 1 && slot < 3 && !info[1] && id < 14) ||
         (kind == 2 && id < 251 && slot < 3 && !info[1]) ||
         (kind == 3 && info[1] && id < info[2] && id < 80) ||
         (kind == 4 && !info[1] && slot >= 3 && id < info[2]) || (kind == 5 && slot < 3 && id < 41)))
        ok = battlePreviewPlay(slot, target, kind, id, program, full);
    leave(p);
    return ok;
}
int xgBattlePreviewTurn(XgBattlePreview *p, int slot, int target, const unsigned char *actions, int count,
                        int full) {
    if (!p || slot < 3 || slot > 10 || target < 0 || target > 10 || !actions || count <= 0 || count > 31)
        return 0;
    enter(p);
    int info[8], other[8], ok = 0;
    p->budget = 1200;
    p->raster = full;
    if (!setjmp(p->abort) && battlePreviewInfo(slot, info) && battlePreviewInfo(target, other))
        ok = battlePreviewTurn(slot, target, actions, count, full);
    leave(p);
    return ok;
}
int xgBattlePreviewCommands(XgBattlePreview *p, int slot, int types[41], int flags[41]) {
    if (!p || slot < 0 || slot >= 3 || !types || !flags)
        return 0;
    enter(p);
    int n = battlePreviewCommands(slot, types, flags);
    leave(p);
    return n;
}
int xgBattlePreviewInfo(XgBattlePreview *p, int slot, int out[8]) {
    if (!p || slot < 0 || slot > 10 || !out)
        return 0;
    enter(p);
    int ok = battlePreviewInfo(slot, out);
    leave(p);
    return ok;
}
int xgBattlePreviewStep(XgBattlePreview *p, int ticks) {
    if (!p || p->failed || ticks < 0 || ticks > 10000)
        return 0;
    for (volatile int i = 0; i < ticks; i++) {
        if (!p->first) {
            enter(p);
            p->budget = 1200;
            if (!setjmp(p->abort))
                battlePreviewFrame();
            leave(p);
            if (p->failed)
                break;
        }
        struct Frame *f = p->first;
        if (!f)
            break;
        xgVgpuClearPrims(p->display);
        for (int k = 0; k < f->primitives; k++)
            xgVgpuAddPrim(p->display, f->prims + k);
        uint16_t *vram = xgVgpuVram(p->display);
        for (int k = 0; k < f->changed; k++)
            vram[f->changes[k].index] = f->changes[k].value;
        if (f->changed)
            xgVgpuVramChanged(p->display);
        if (p->audible && f->samples)
            xgPreviewQueue(f->pcm, f->samples);
        p->screenY = f->screenY;
        p->first = f->next;
        if (!p->first)
            p->last = NULL;
        free(f->prims);
        free(f->changes);
        free(f->pcm);
        free(f);
    }
    return xgVgpuPrimCount(p->display);
}
XgVgpu *xgBattlePreviewGpu(XgBattlePreview *p) {
    return p ? p->display : NULL;
}
int xgBattlePreviewScreenY(const XgBattlePreview *p) {
    return p ? p->screenY : 0;
}
void xgBattlePreviewAudio(XgBattlePreview *p, int audible) {
    if (p)
        p->audible = audible;
    if (!audible)
        xgPreviewStop();
}

void xgBattlePreviewDump(XgBattlePreview *p) {
    if (!p)
        return;
    enter(p);
    battlePreviewDump();
    leave(p);
    uint32_t key = 2166136261u;
    const uint8_t *bytes = (const uint8_t *)xgVgpuPrims(p->display);
    for (size_t i = 0; i < (size_t)xgVgpuPrimCount(p->display) * sizeof(XgPrim); i++)
        key = (key ^ bytes[i]) * 16777619u;
    bytes = (const uint8_t *)xgVgpuVram(p->display);
    for (size_t i = 0; i < 1024 * 512 * 2; i++)
        key = (key ^ bytes[i]) * 16777619u;
    fprintf(stderr, "battle picture: key %u, failed %d\n", key, p->failed);
}

int xgBattlePreviewFailed(const XgBattlePreview *p) {
    return p && p->failed;
}

int xgBattlePreviewActive(XgBattlePreview *p) {
    if (!p || p->failed)
        return 0;
    enter(p);
    int active = battlePreviewActive();
    leave(p);
    return active;
}

int xgBattlePreviewPresentation(XgBattlePreview *p, int slot, int full) {
    if (!p || slot < 0 || slot > 10)
        return 0;
    enter(p);
    int ok = battlePreviewPresentation(slot, full);
    leave(p);
    if (ok)
        xgBattlePreviewStep(p, 1);
    return ok;
}
