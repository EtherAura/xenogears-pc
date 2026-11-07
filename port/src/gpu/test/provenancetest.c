/* Native capture identity, upload footprints and bounded save-state data,
 * without a display copy. Device readback and tagged state IO are test
 * transports; the provenance, texture protection and state part are real. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gpu/texture.h"
#include "system/savestate.h"

static int checks, readbacks;
#include "test/check.h"
static uint16_t device[VRAM_H][VRAM_W];
void gp0VramChanging(VRect r) { (void)r; }
/* No GPU-side draw is queued by this CPU texture fixture. */
void gvkUpload(VRect r,const uint16_t* src,int pitch) { (void)r;(void)src;(void)pitch; }
void gvkReadback(VRect r, uint16_t* dst, int pitch) {
    for (int y = r.y0; y <= r.y1; ++y)
        memcpy(dst + (y-r.y0)*pitch, &device[y][r.x0], (size_t)(r.x1-r.x0+1)*2);
    ++readbacks;
}

struct XgState {
    int mode, next, used, errors; /* 0 save, 1 verify, 2 load */
    struct { char tag[32]; size_t size; void* data; } chunk[32];
};
int xgStateLoading(const XgState* s) { return s->mode == 2; }
int xgStateSaving(const XgState* s) { return s->mode == 0; }
int xgStateLate(XgState* s, int wide, int x, int y, int w, int h, uint16_t* dst, int pitch) { return 0; }
void xgStateIo(XgState* s, const char* tag, void* p, size_t n);
void xgStateIoAside(XgState* s, const char* tag, void* p, size_t n) { xgStateIo(s, tag, p, n); }
void xgStateIo(XgState* s, const char* tag, void* p, size_t n) {
    int i = s->next++;
    if (i >= 32) { s->errors++; return; }
    if (!s->mode) {
        s->used = s->next;
        snprintf(s->chunk[i].tag, sizeof(s->chunk[i].tag), "%s", tag);
        s->chunk[i].size = n;
        s->chunk[i].data = malloc(n ? n : 1);
        if (!s->chunk[i].data) { s->errors++; return; }
        memcpy(s->chunk[i].data, p, n);
    } else if (i >= s->used || strcmp(s->chunk[i].tag,tag) || s->chunk[i].size != n) {
        s->errors++;
    } else if (s->mode == 2) memcpy(p, s->chunk[i].data, n);
}
size_t xgStateLen(XgState* s, const char* tag, size_t n) {
    uint64_t value = n;
    int mode = s->mode;
    if (mode == 1) s->mode = 2; /* lengths are read in the checking pass too */
    xgStateIo(s, tag, &value, sizeof(value));
    s->mode = mode;
    return (size_t)value;
}
static int chunk(const XgState* s, const char* tag) {
    for (int i = 0; i < s->used; ++i) if (!strcmp(s->chunk[i].tag,tag)) return i;
    return -1;
}
static void freeState(XgState* s) {
    for (int i = 0; i < s->used; ++i) free(s->chunk[i].data);
    memset(s, 0, sizeof(*s));
}
static void pass(XgState* s, int mode) {
    s->mode = mode; s->next = s->errors = 0; vramStateIo(s);
}
static void reset(void) {
    textureCacheReset(); vramReset(); memset(device,0,sizeof(device)); readbacks = 0;
}
static void pixels(uint16_t* p, int n, int seed) {
    for (int i = 0; i < n; ++i) p[i] = (uint16_t)((i*73+seed) & 0x7fff);
}
static void draw(int x, int y, int w, int h, const uint16_t* p) {
    for (int j = 0; j < h; ++j) for (int i = 0; i < w; ++i)
        device[(y+j)&511][(x+i)&1023] = p[j*w+i];
    VPiece pc[4]; int n = vramWrap(x,y,w,h,pc);
    for (int i = 0; i < n; ++i) vramMarkDrawn(pc[i].r);
}
static void upload(int x, int y, int w, int h, const uint16_t* p) {
    int rendered = vramLoadRendered(x,y,w,h,p,0,0);
    for (int j = 0; j < h; ++j) for (int i = 0; i < w; ++i)
        vramShadow[(y+j)&511][(x+i)&1023] = device[(y+j)&511][(x+i)&1023] = p[j*w+i];
    VPiece pc[4]; int n = vramWrap(x,y,w,h,pc);
    for (int i = 0; i < n; ++i) vramMarkCopied(pc[i].r,rendered);
}

int main(void) {
    uint16_t p[256], edit[256], other[256];
    pixels(p,256,17); pixels(other,256,888);
    reset();
    CHECK(!vramLoadRendered(640,0,16,16,p,0,0) && readbacks == 0);
    upload(640,0,16,16,p);
    VRect cover, r = {640,0,655,15};
    TextureRequest tex = {10 | (2<<7),0,0,0,0,16,16,0,0};
    CHECK(vramUploadCover(r,&cover) && !memcmp(&r,&cover,sizeof(r)));
    CHECK(textureCacheGet(tex));
    draw(640,0,16,16,p);
    CHECK(!textureCacheGet(tex));
    CHECK(vramLoadRendered(640,0,16,16,p,0,0) && readbacks == 1);
    uint64_t gen = vramTileGeneration(40,0);
    memcpy(edit,p,sizeof(p));
    for (int i = 0; i < 25; ++i) edit[i] ^= 1;
    CHECK(vramLoadRendered(640,0,16,16,edit,1,0)); /* ceil(90%): 231 pixels */
    edit[25] ^= 1;
    CHECK(!vramLoadRendered(640,0,16,16,edit,0,0));
    CHECK(vramTileGeneration(40,0) == gen && readbacks == 1);
    for (int i = 0; i < 256; ++i) device[i/16][640+i%16] = p[i] | 0x8000;
    vramMarkDrawn(r);
    CHECK(vramLoadRendered(640,0,16,16,other,0,1)); /* all writes rejected */
    CHECK(!vramLoadRendered(640,0,16,16,other,0,0));
    upload(640,0,16,16,p);
    CHECK(!textureCacheGet(tex));
    upload(640,0,16,16,other);
    CHECK(textureCacheGet(tex) && vramUploadCover(r,&cover));

    /* A texture read is not a rendered capture. Wrapped rendered reads are
     * one rectangle, so the same output can be loaded at another origin. */
    reset(); upload(640,0,16,16,p); vramStoreCapture(640,0,16,16,p);
    CHECK(!vramLoadRendered(704,32,16,16,p,0,0));
    draw(1016,504,16,16,p);
    vramStoreCapture(1016,504,16,16,p);
    CHECK(vramLoadRendered(704,32,16,16,p,0,0));
    upload(704,32,16,16,p);
    TileSet target = {0}; tilesAdd(&target,(VRect){704,32,719,47});
    CHECK(vramWasRendered(&target));
    CHECK(!vramUploadCover((VRect){704,32,719,47},&cover));
    CHECK(vramLoadRendered(1016,504,16,16,p,0,0));
    memcpy(edit,p,sizeof(p)); for (int i = 0; i < 25; ++i) edit[i] ^= 1;
    CHECK(vramLoadRendered(768,64,16,16,edit,0,0));
    edit[25] ^= 1; CHECK(!vramLoadRendered(768,64,16,16,edit,0,0));
    for (int i = 0; i < 256; ++i) vramShadow[64+i/16][768+i%16] = other[i] | 0x8000;
    CHECK(vramLoadRendered(768,64,16,16,p,1,0));
    CHECK(!vramLoadRendered(768,64,16,16,p,0,1)); /* rejected writes differ from the capture */
    CHECK(!vramLoadRendered(768,64,16,15,p,0,0));
    CHECK(!vramLoadRendered(0,0,1025,1,p,0,0));
    CHECK(!vramLoadRendered(0,0,1,513,p,0,0));
    CHECK(!vramLoadRendered(0,0,0,1,p,0,0));
    CHECK(!vramLoadRendered(0,0,16,16,NULL,0,0));

    /* Restore the real native state part into a deliberately unrelated
     * history. Saving itself must not mutate native upload tracking. */
    XgState s = {0}, again = {0};
    pass(&s,0); CHECK(!s.errors);
    int cap = chunk(&s,"capture0Pixels"), empty = chunk(&s,"capture1Pixels");
    CHECK(cap >= 0 && empty >= 0 && s.chunk[cap].size == sizeof(p) && s.chunk[empty].size == 0);
    CHECK(!vramUploadCover((VRect){704,32,719,47},&cover));
    pass(&again,0); CHECK(!again.errors && again.used == s.used);
    for (int i = 0; i < s.used; ++i)
        CHECK(s.chunk[i].size == again.chunk[i].size && !memcmp(s.chunk[i].data,again.chunk[i].data,s.chunk[i].size));
    freeState(&again);
    reset(); upload(640,0,16,16,other);
    pass(&s,1); CHECK(!s.errors); /* checking must leave the current run alone */
    CHECK(!vramLoadRendered(768,64,16,16,p,0,0));
    pass(&s,2); CHECK(!s.errors);
    CHECK(vramLoadRendered(768,64,16,16,p,0,0));
    CHECK(vramLoadRendered(704,32,16,16,p,0,0));
    CHECK(!vramUploadCover((VRect){704,32,719,47},&cover));
    int len = chunk(&s,"capture0Size");
    uint64_t saved; memcpy(&saved,s.chunk[len].data,sizeof(saved));
    uint64_t invalid = UINT64_MAX; memcpy(s.chunk[len].data,&invalid,sizeof(invalid));
    pass(&s,1); CHECK(s.errors == 1); /* bounded before copying, including verification */
    memcpy(s.chunk[len].data,&saved,sizeof(saved));
    freeState(&s);

    /* Two recent distinct captures, refreshing a rectangle and evicting
     * the older one. Large transfers stay bounded by native VRAM. */
    reset(); draw(0,0,16,16,p); vramStoreCapture(0,0,16,16,p);
    draw(16,0,16,16,other); vramStoreCapture(16,0,16,16,other);
    vramStoreCapture(0,0,16,16,p);
    pixels(edit,256,2222); draw(32,0,16,16,edit); vramStoreCapture(32,0,16,16,edit);
    CHECK(vramLoadRendered(640,0,16,16,p,0,0));
    CHECK(vramLoadRendered(640,0,16,16,edit,0,0));
    CHECK(!vramLoadRendered(640,0,16,16,other,0,0));
    uint16_t* full = malloc(VRAM_W*VRAM_H*2); CHECK(full);
    pixels(full,VRAM_W*VRAM_H,42); draw(1,1,VRAM_W,VRAM_H,full);
    vramStoreCapture(1,1,VRAM_W,VRAM_H,full);
    CHECK(vramLoadRendered(0,0,VRAM_W,VRAM_H,full,0,0));
    vramStoreCapture(0,0,VRAM_W+1,1,p); vramStoreCapture(0,0,1,VRAM_H+1,p);
    pass(&s,0); CHECK(!s.errors);
    CHECK(s.chunk[chunk(&s,"capture0Pixels")].size == VRAM_W*VRAM_H*2 ||
          s.chunk[chunk(&s,"capture1Pixels")].size == VRAM_W*VRAM_H*2);
    freeState(&s); free(full); textureCacheReset();
    printf("Native capture provenance: %d checks passed\n",checks);
    return 0;
}
