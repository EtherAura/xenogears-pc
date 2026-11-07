/* Decoding and cache lifetime without a Vulkan device. Fake readbacks only
 * replace the exact CPU image, like the real device; writes use real tracking. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gpu/texture.h"

static uint16_t device[VRAM_H][VRAM_W];
static int checks, readbacks, readbackPixels, uploads;
#include "test/check.h"
/* gp0.c's texture cache hook (vram.c): no GP0 here. */
void gp0VramChanging(VRect r) { (void)r; }
void gvkReadback(VRect r, uint16_t* dst, int pitch) {
    int w = r.x1-r.x0+1, h = r.y1-r.y0+1;
    for (int y = 0; y < h; y++) memcpy(dst+y*pitch,&device[r.y0+y][r.x0],(size_t)w*2);
    ++readbacks; readbackPixels += w*h;
}
void gvkUpload(VRect r,const uint16_t* src,int pitch) {
    for(int y=r.y0;y<=r.y1;y++)memcpy(&device[y][r.x0],src+(y-r.y0)*pitch,(size_t)(r.x1-r.x0+1)*2);
    uploads++;
    /* The real backend can request another flush while making room. */
    vramFlushCpuWrites(r);
}
static void put(int x, int y, uint16_t c) {
    vramShadow[y][x] = device[y][x] = c;
    vramMarkUploaded((VRect){x, y, x, y}, 0);
}
static void reset(void) {
    textureCacheReset(); vramReset(); memset(device, 0, sizeof(device)); readbacks = readbackPixels = uploads = 0;
}
/* An upload of r filled by a pattern, stamped at vblank `at`. */
static void upload(VRect r, uint32_t at, unsigned seed) {
    for (int y = r.y0; y <= r.y1; ++y) for (int x = r.x0; x <= r.x1; ++x)
        vramShadow[y][x] = device[y][x] = (uint16_t)((x * 7 + y * 13 + seed) & 0x7fff);
    vramStampClock(at);
    vramMarkUploaded(r, 0);
}
/* Field-like load of the 4-bit page at x 640, y 256 (tpage 26): 22-wide
 * strips 46 rows tall, crossing the page's right edge, over vblanks. */
static void streamPage(uint32_t at) {
    int n = 0;
    for (int x = 640; x < 720; x += 22)
        for (int y = 256; y < 512; y += 46) {
            if (x == 662 && y == 348) continue; /* a hole: what was there stays */
            upload((VRect){x, y, x + 21, y + 45 > 511 ? 511 : y + 45}, at + (uint32_t)(n++ % 7), 1);
        }
}
/* Sixty-four unrelated uploads elsewhere: none of the page's is among the
 * most recent any more (vramUploadCover's ring). */
static void pushRing(void) {
    for (int i = 0; i < 64; ++i) vramMarkUploaded((VRect){i, 0, i, 0}, 0);
}
int main(void) {
    reset();
    {
    /* Deferred CPU tiles are physical uploads only. A dirty device tile
     * between two CPU writes must survive, including across unequal rows. */
    for(int y=0;y<32;y++)for(int x=0;x<80;x++)device[y][x]=0x1234;
    VRect a={16,0,31,31},b={48,0,63,15},gap={32,0,47,15};
    for(int y=0;y<32;y++)for(int x=16;x<32;x++)vramShadow[y][x]=0x801f;
    for(int y=0;y<16;y++)for(int x=48;x<64;x++)vramShadow[y][x]=0x03e0;
    vramMarkDrawn(a);vramDeferCpuDraw(a);vramMarkDrawn(b);vramDeferCpuDraw(b);vramMarkDrawn(gap);
    uint64_t generation=vramTileGeneration(1,0);
    vramFlushCpuWrites(gap);
    CHECK(uploads==0 && vramIsDrawn(gap));
    vramFlushCpuWrites((VRect){17,1,18,2});
    CHECK(device[0][16]==0x801f && device[15][31]==0x801f);
    CHECK(device[16][16]==0x1234 && device[0][48]==0x1234);
    vramFlushCpuWrites((VRect){0,0,VRAM_W-1,VRAM_H-1});
    CHECK(device[31][31]==0x801f && device[15][63]==0x03e0);
    CHECK(device[0][32]==0x1234 && device[15][47]==0x1234 && device[31][48]==0x1234);
    CHECK(vramIsDrawn(gap) && vramTileGeneration(1,0)==generation && readbacks==0);
    int previous=uploads;vramFlushCpuWrites(a);CHECK(uploads==previous);
    /* All 64 columns, and reset with an outstanding CPU write. */
    memset(vramShadow,0x5a,sizeof(vramShadow));
    vramDeferCpuDraw((VRect){0,32,VRAM_W-1,47});vramFlushCpuWrites((VRect){0,32,VRAM_W-1,47});
    CHECK(device[32][0]==0x5a5a && device[47][1023]==0x5a5a);
    vramDeferCpuDraw(a);vramReset();previous=uploads;vramFlushCpuWrites(a);
    CHECK(uploads==previous && device[0][16]==0x801f);
    }
    reset();
    /* A CPU feedback draw already has the authoritative pixels. Resolving
     * it avoids readback, preserves texture provenance/versioning, and does
     * not clear unrelated GPU writes. */
    VRect cpu = {17,17,18,18}, other = {80,80,81,81};
    vramMarkDrawn(cpu);
    uint64_t cpuGeneration = vramTileGeneration(1,1);
    vramShadow[17][17] = device[17][17] = 0x801f;
    vramMarkDrawn(other); device[80][80] = 0x03e0;
    vramResolveCpuDraw(cpu);
    vramSyncRect(cpu);
    TileSet cpuTiles = {0}; tilesAdd(&cpuTiles,cpu);
    CHECK(readbacks == 0 && vramShadow[17][17] == 0x801f);
    CHECK(vramTileGeneration(1,1) == cpuGeneration && vramWasRendered(&cpuTiles));
    CHECK(vramIsDrawn(other));
    vramSyncRect(other);
    CHECK(readbacks == 1 && vramShadow[80][80] == 0x03e0);
    CHECK(readbackPixels == 256);
    reset();
    /* A small request synchronizes its whole tile, retaining dirty state
     * and the old CPU contents of distant tiles until they are requested. */
    device[17][17] = 0x801f; device[31][31] = 0x7c00; device[80][80] = 0x03e0;
    vramMarkDrawn(cpu); vramMarkDrawn(other);
    vramSyncRect(cpu);
    CHECK(readbacks == 1 && readbackPixels == 256);
    CHECK(vramShadow[17][17] == 0x801f && vramShadow[31][31] == 0x7c00);
    CHECK(vramIsDrawn(other) && vramShadow[80][80] == 0);
    vramSyncRect(other);
    CHECK(readbacks == 2 && readbackPixels == 512 && vramShadow[80][80] == 0x03e0);
    reset();
    /* Complete CPU overwrites discard obsolete device pixels. A partial
     * edge keeps neighbouring pixels dirty until their real readback. */
    vramMarkDrawn((VRect){16,16,63,63});
    device[16][16]=0x7c00; device[63][63]=0x03e0;
    vramResolveCpuWrite((VRect){17,17,62,62});
    CHECK(vramIsDrawn((VRect){16,16,31,31}));
    CHECK(!vramIsDrawn((VRect){32,32,47,47}));
    CHECK(vramIsDrawn((VRect){48,48,63,63}));
    vramShadow[40][40]=0x801f;
    vramSyncRect((VRect){40,40,40,40});
    CHECK(readbacks==0 && vramShadow[40][40]==0x801f);
    vramResolveCpuWrite((VRect){16,16,63,63});
    vramSyncRect((VRect){16,16,63,63});
    CHECK(readbacks==0 && !vramIsDrawn((VRect){16,16,63,63}));
    reset();
    /* Screen feedback samples successive pieces of a dense rendered area.
     * Synchronize it together without downloading unused texture RAM. */
    VRect screen = {32,32,95,95};
    vramMarkDrawn(screen); device[95][95] = 0x7c00;
    vramSyncRect((VRect){32,32,33,33});
    CHECK(readbacks == 1 && readbackPixels == 64*64);
    CHECK(!vramIsDrawn(screen) && vramShadow[95][95] == 0x7c00);
    vramSyncRect(screen);
    CHECK(readbacks == 1);
    reset();
    /* A distant scratch strip must not turn a dense framebuffer into one
     * download per sampled triangle. Keep untouched pages dirty, and make
     * subsequent GPU writes visible even after the larger prefetch. */
    VRect frame = {32,32,351,223}, scratch = {960,480,1023,495};
    vramMarkDrawn(frame); vramMarkDrawn(scratch);
    device[223][255] = 0x801f; device[223][351] = 0x03e0; device[480][960] = 0x7c00;
    vramSyncRect((VRect){32,32,33,33});
    CHECK(readbacks == 1 && readbackPixels == 224*192);
    CHECK(vramShadow[223][255] == 0x801f && vramShadow[223][351] == 0);
    CHECK(!vramIsDrawn((VRect){32,32,255,223}));
    CHECK(vramIsDrawn((VRect){256,32,351,223}) && vramIsDrawn(scratch));
    vramSyncRect((VRect){256,32,257,33});
    CHECK(readbacks == 2 && readbackPixels == 320*192 && vramShadow[223][351] == 0x03e0);
    CHECK(!vramIsDrawn(frame) && vramIsDrawn(scratch) && vramShadow[480][960] == 0);
    device[40][40] = 0x7fff; vramMarkDrawn((VRect){40,40,40,40});
    vramSyncRect((VRect){40,40,40,40});
    CHECK(readbacks == 3 && readbackPixels == 320*192+256 && vramShadow[40][40] == 0x7fff);
    CHECK(vramIsDrawn(scratch));
    reset();
    /* Sparse writes in one page remain separate; the last VRAM page uses
     * the same batching rule without crossing the image's right/bottom edge. */
    VRect sparse[] = {{0,0,15,15},{240,0,255,15},{0,240,15,255},{240,240,255,255}};
    for (int i = 0; i < 4; i++) vramMarkDrawn(sparse[i]);
    vramSyncRect(sparse[0]);
    CHECK(readbacks == 1 && readbackPixels == 256 && vramIsDrawn(sparse[3]));
    VRect corner = {960,480,1023,511};
    vramMarkDrawn(corner); device[511][1023] = 0xffff;
    vramSyncRect((VRect){1000,500,1000,500});
    CHECK(readbacks == 2 && readbackPixels == 256+64*32);
    CHECK(vramShadow[511][1023] == 0xffff && !vramIsDrawn(corner) && vramIsDrawn(sparse[3]));
    reset();
    /* Nonzero index zero and STP black are both visible. */
    put(128, 0, 0x3210);
    const uint16_t colors[16] = {31, 0, 0x8000, 0x83e0};
    for (int i = 0; i < 16; ++i) put(16 + i, 400, colors[i]);
    TextureRequest r = {2, (uint16_t)(1 | 400 << 6), 0, 0, 0, 4, 1, 0, 0};
    const DecodedTexture* a = textureCacheGet(r);
    CHECK(a && a->depth == 4 && a->paletteSize == 16);
    CHECK(a->rgba[0] == 255 && a->rgba[3] == 255);
    CHECK(a->rgba[7] == 0 && a->rgba[11] == 128 && a->rgba[10] == 0);
    CHECK(a->rgba[13] == 255 && a->rgba[15] == 128);
    CHECK(a->texels[0] == 0 && a->texels[1] == 1 && a->texels[3] == 3);
    uint64_t tk = a->texelKey, pk = a->paletteKey, rev = a->revision;
    CHECK(textureCacheGet(r)->revision == rev);
    put(800, 90, 12);
    CHECK(textureCacheGet(r)->revision == rev);
    put(16, 400, 0x7c00);
    a = textureCacheGet(r);
    CHECK(a->revision != rev && a->texelKey == tk && a->paletteKey != pk);
    CHECK(a->rgba[2] == 255 && a->rgba[0] == 0);
    pk = a->paletteKey;
    /* Relocating both page and CLUT has no effect on the content keys. */
    put(384, 256, 0x3210);
    for (int i = 0; i < 16; ++i) put(64 + i, 450, i == 0 ? 0x7c00 : colors[i]);
    TextureRequest relocated = {6 | 16, (uint16_t)(4 | 450 << 6), 0, 0, 0, 4, 1, 0, 0};
    const DecodedTexture* b = textureCacheGet(relocated);
    CHECK(b && b->texelKey == tk && b->paletteKey == pk);
    CHECK(memcmp(a->rgba, b->rgba, 16) == 0 && readbacks == 0);
    /* Unsampled packed nibbles cannot change a one-texel key. */
    r.w = 1;
    a = textureCacheGet(r); tk = a->texelKey;
    put(128, 0, 0xffff & ~15);
    CHECK(textureCacheGet(r)->texelKey == tk);
    /* A GPU-written CLUT is fetched once; readback is not another write. */
    rev = textureCacheGet(r)->revision;
    uint64_t gen = vramTileGeneration(1, 25);
    device[400][16] = 0x3e0; vramMarkDrawn((VRect){16,400,16,400});
    CHECK(vramTileGeneration(1,25) != gen);
    a = textureCacheGet(r);
    CHECK(a->revision != rev && a->rgba[1] == 255 && readbacks == 1);
    gen = vramTileGeneration(1,25); rev = a->revision;
    CHECK(textureCacheGet(r)->revision == rev && readbacks == 1 && vramTileGeneration(1,25) == gen);
    /* Windows remap logical UVs before fetch; negative UVs wrap. */
    put(130, 8, 0x1112);
    r.u = -256; r.v = 0; r.window = 1 | 1 << 5 | 1 << 10 | 1 << 15;
    a = textureCacheGet(r); CHECK(a && a->texels[0] == 2);
    reset();
    /* Eight-bit pages and 256-entry CLUTs may cross VRAM's right edge. */
    for (int i = 0; i < 256; ++i) put((1008 + i) & 1023, 510, (uint16_t)(i | (i & 1 ? 0x8000 : 0)));
    put(36, 511, 0xfffe);
    r = (TextureRequest){15 | 16 | 128, (uint16_t)(63 | 510 << 6), 0, 200,255,2,1, 0, 0};
    a = textureCacheGet(r);
    CHECK(a && a->depth == 8 && a->paletteSize == 256);
    CHECK(a->texels[0] == 254 && a->texels[1] == 255);
    CHECK(a->palette[254] == 254 && a->palette[255] == 0x80ff);
    CHECK(a->rgba[3] == 255 && a->rgba[7] == 128);
    reset();
    r = (TextureRequest){15 | 16 | 256, 123, 0, 80,255,2,1, 0, 0};
    put(16,511,0x8000); put(17,511,0x7fff);
    a = textureCacheGet(r);
    CHECK(a && a->depth == 15 && a->paletteKey == 0 && a->paletteSize == 0);
    CHECK(a->texels[0] == 0 && a->texels[1] == 128 && a->rgba[3] == 128);
    CHECK(a->rgba[4] == 255 && a->rgba[7] == 255);
    tk = a->texelKey; r.tpage |= 128;
    CHECK(textureCacheGet(r)->texelKey == tk); /* reserved depth has direct semantics */
    /* Render provenance survives syncing, fills, and copying a capture. */
    vramMarkDrawn((VRect){16,511,17,511});
    CHECK(!textureCacheGet(r) && readbacks == 0);
    vramSyncRect((VRect){16,511,17,511});
    CHECK(!textureCacheGet(r) && readbacks == 1);
    vramMarkUploaded((VRect){16,511,17,511},0);
    CHECK(!textureCacheGet(r)); /* partial upload does not erase the rest of a tile */
    vramMarkUploaded((VRect){16,496,31,511},1);
    CHECK(!textureCacheGet(r)); /* mask rejection can keep rendered pixels */
    vramMarkUploaded((VRect){16,496,31,511},0);
    CHECK(textureCacheGet(r));
    vramMarkCopied((VRect){16,496,31,511},1);
    CHECK(!textureCacheGet(r));
    vramMarkCopied((VRect){16,496,31,511},0);
    CHECK(textureCacheGet(r));
    reset();
    /* A full-page entry retains at most 6 bytes/texel; slot count is fixed. */
    r = (TextureRequest){256,0,0,0,0,256,256,0,0};
    CHECK(textureCacheGet(r));
    for (int i = 1; i <= TEXTURE_CACHE_SLOTS; ++i) {
        r.w = 1; r.h = 1; r.u = i;
        CHECK(textureCacheGet(r));
    }
    TextureStats stats; textureCacheStats(&stats);
    CHECK(stats.entries == TEXTURE_CACHE_SLOTS && stats.evictions == 1 && stats.bytes == TEXTURE_CACHE_SLOTS * 6);
    CHECK(!textureCacheGet((TextureRequest){0,0,0,0,0,257,1,0,0}));
    CHECK(!textureCacheGet((TextureRequest){0,0,0,32768,0,1,1,0,0}));
    CHECK(!textureCacheGet((TextureRequest){0,0,0,0,0,0,1,0,0}));
    reset();
    /* Adjacent triangle patches share the whole upload when it is intact. */
    vramMarkUploaded((VRect){640,256,647,271},0);
    r = (TextureRequest){26,0,0,1,2,12,10,0,0};
    TextureRequest snapped = textureRequestSnap(r);
    CHECK(snapped.u == 0 && snapped.v == 0 && snapped.w == 32 && snapped.h == 16);
    vramMarkUploaded((VRect){647,271,647,271},0);
    snapped = textureRequestSnap(r);
    CHECK(snapped.u == r.u && snapped.w == r.w); /* assembled upload: sampled patch */
    r.window = 1; vramMarkUploaded((VRect){640,256,647,271},0);
    CHECK(textureRequestSnap(r).w == r.w);
    /* Stable regions: a page streamed as strips (none covers a patch) snaps
     * to the whole load; any patch of it gives the same request and key. */
    reset();
    upload((VRect){640,256,703,511}, 1, 99);        /* an older load left in VRAM */
    upload((VRect){648,300,655,307}, 5, 7);
    streamPage(400);
    pushRing();
    r = (TextureRequest){26, 0, 0, 40, 60, 12, 10, 0, 0};
    snapped = textureRequestSnapStable(r);
    CHECK(snapped.u == 0 && snapped.v == 0 && snapped.w == 256 && snapped.h == 256);
    CHECK(snapped.stableFrom == 401 && snapped.stableTo == 407);
    TextureRequest elsewhere = textureRequestSnapStable((TextureRequest){26, 0, 0, 200, 230, 30, 20, 0, 0});
    CHECK(elsewhere.u == snapped.u && elsewhere.w == snapped.w && elsewhere.stableFrom == snapped.stableFrom);
    const DecodedTexture* page = textureCacheGet(snapped);
    CHECK(page && page->request.stableTo == 407);
    uint64_t pageKey = page->texelKey;
    CHECK(textureCacheGet(elsewhere)->texelKey == pageKey);
    /* A later sprite-like upload inside the page is masked (clear, out of the
     * key) and keeps its own key; the page keeps its key. */
    upload((VRect){660,280,667,295}, 900, 3);
    page = textureCacheGet(textureRequestSnapStable(r));
    CHECK(page && page->texelKey != pageKey); /* those texels are clear now */
    TextureRequest fresh = textureRequestSnapStable((TextureRequest){26, 0, 0, 84, 26, 8, 8, 0, 0});
    CHECK(textureRequestSnap(r).stableTo == 0 && textureRequestSnap(r).w == r.w); /* sprites, HUD: no regions */
    CHECK(fresh.stableTo == 0 && fresh.u == 80 && fresh.w == 32 && fresh.h == 16); /* the intact upload, as before */
    CHECK(page->texels[(size_t)(280 - 256) * 256 + (660 - 640) * 4] == 0 && page->rgba[((size_t)(280 - 256) * 256 + 80) * 4 + 3] == 0);
    /* The same load over different leftovers has the same key. */
    reset();
    upload((VRect){640,256,703,511}, 1, 1234);
    streamPage(400);
    pushRing();
    page = textureCacheGet(textureRequestSnapStable(r));
    CHECK(page && page->texelKey == pageKey);
    CHECK(page->texels[(size_t)(348 - 256 + 4) * 256 + (662 - 640 + 4) * 4] == 0); /* the hole: clear */
    /* A load too small for a page keeps the sampled patch; so does a patch
     * across two loads more than STABLE_GAP vblanks apart. */
    reset();
    upload((VRect){640,256,663,295}, 400, 1);
    upload((VRect){664,256,687,295}, 402, 1);
    CHECK(textureRequestSnapStable((TextureRequest){26, 0, 0, 90, 4, 12, 10, 0, 0}).w == 12);
    reset();
    streamPage(400);
    upload((VRect){640,256,703,319}, 700, 5);
    pushRing();
    snapped = textureRequestSnapStable((TextureRequest){26, 0, 0, 40, 200, 12, 10, 0, 0});
    CHECK(snapped.stableFrom >= 400 && snapped.stableTo <= 407 && snapped.v == 64 && snapped.h == 192);
    CHECK(textureRequestSnapStable((TextureRequest){26, 0, 0, 40, 56, 12, 16, 0, 0}).stableTo == 0); /* spans both */
    /* Draws clear stamps: drawn texels are not part of any load. */
    vramMarkDrawn((VRect){640,400,647,407});
    CHECK(textureRequestSnapStable((TextureRequest){26, 0, 0, 0, 144, 4, 4, 0, 0}).stableTo == 0);
    reset(); textureCacheStats(&stats);
    CHECK(stats.entries == 0 && stats.bytes == 0 && stats.hits == 0);
    /* Edge cache crop, STP, reuse, revision invalidation and eviction. */
    uint8_t pixels[9*4];
    for (int i=0;i<9;++i) { pixels[i*4]=255; pixels[i*4+1]=pixels[i*4+2]=0; pixels[i*4+3]=128; }
    pixels[1*4]=pixels[3*4]=0; pixels[1*4+2]=pixels[3*4+2]=255;
    pixels[1*4+3]=pixels[3*4+3]=255;
    DecodedTexture edge = {0}; edge.request=(TextureRequest){0,0,0,-1,-1,3,3,0,0};
    edge.rgba=pixels; edge.revision=9000000;
    TextureSample sample; r=edge.request;
    CHECK(textureSampleGet(&edge,r,2,&sample));
    int center=(2*6+2)*4;
    CHECK(sample.rgba[center]==0 && sample.rgba[center+2]==255 && sample.rgba[center+3]==128);
    const uint8_t* borrowed=sample.rgba;
    CHECK(textureSampleGet(&edge,r,2,&sample) && sample.rgba==borrowed);
    r.u=r.v=0; r.w=r.h=1;
    CHECK(textureSampleGet(&edge,r,2,&sample));
    for (int i=0;i<4;++i) CHECK(sample.rgba[i*4]==255 && sample.rgba[i*4+2]==0 && sample.rgba[i*4+3]==128);
    CHECK(textureSampleGet(&edge,r,1,&sample) && sample.scale==1 && sample.rgba==pixels);
    r.u=-2; CHECK(!textureSampleGet(&edge,r,2,&sample));
    CHECK(!textureSampleGet(NULL,r,2,&sample)); r=edge.request;
    for (int i=0;i<40;++i) {
        ++edge.revision; pixels[4*4]=i; CHECK(textureSampleGet(&edge,r,2,&sample));
        CHECK(sample.rgba[center+4]==i);
    }
    /* Upscale variants by content (xg-j85.7): a re-decode with the same
     * pixels, and the same pixels moved, reuse a variant; Edge and
     * Upscaled variants of one patch stay apart; a reset never reuses a
     * variant's revision; the byte budget evicts the least recently used. */
    reset();
    for (int i = 0; i < 16; ++i) put(16 + i, 400, colors[i]);
    for (int y = 0; y < 16; ++y) for (int x = 0; x < 4; ++x) {
        put(128 + x, y, (uint16_t)(0x1230 + y + x * 0x1000));
        put(384 + x, y, (uint16_t)(0x1230 + y + x * 0x1000));
    }
    TextureRequest q = {2, (uint16_t)(1 | 400 << 6), 0, 0, 0, 16, 16, 0, 0}, moved = q;
    moved.tpage = 6;
    const DecodedTexture* qd = textureCacheGet(q);
    TextureSample up, again; TextureSampleStats ss;
    CHECK(qd && textureUpscaleSample(qd, q, 2, &up) && up.scale == 2 && !up.keep);
    textureSampleStats(&ss); uint64_t made = ss.made;
    uint64_t decoded = qd->revision;
    put(128, 0, 0x1230); /* the same value again: a new decode, equal content */
    qd = textureCacheGet(q);
    CHECK(qd->revision != decoded && textureUpscaleSample(qd, q, 2, &again));
    textureSampleStats(&ss); CHECK(ss.made == made && again.revision == up.revision && again.rgba == up.rgba);
    const DecodedTexture* copy = textureCacheGet(moved);
    CHECK(copy && copy->texelKey == qd->texelKey && textureUpscaleSample(copy, moved, 2, &again));
    textureSampleStats(&ss); CHECK(ss.made == made && again.rgba == up.rgba && again.u == moved.u);
    CHECK(textureSampleGet(qd, q, TEXTURE_FILTER_EDGE, &again) && again.revision != up.revision);
    CHECK(!memcmp(again.rgba, up.rgba, (size_t)16 * 16 * 4 * 4)); /* tall: the same Scale2x pixels */
    put(129, 3, 0x0001); qd = textureCacheGet(q);
    CHECK(textureUpscaleSample(qd, q, 2, &again) && again.revision != up.revision);
    textureSampleStats(&ss); CHECK(ss.made == made + 2);
    uint64_t lastRevision = again.revision;
    textureSampleReset();
    CHECK(textureUpscaleSample(qd, q, 2, &again) && again.revision != lastRevision && again.revision != up.revision);
    /* Sixteen 4 MiB variants fill the budget; the next evicts the least
     * recently used one, not the one just drawn again. */
    textureSampleReset();
    static uint8_t art[256 * 256 * 4];
    DecodedTexture big = {0}; big.request = (TextureRequest){0, 0, 0, 0, 0, 256, 256, 0, 0}; big.rgba = art;
    for (int i = 0; i < 16; ++i) { big.revision = 7000000 + i; CHECK(textureUpscaleSample(&big, big.request, 4, &again)); }
    textureSampleStats(&ss); CHECK(ss.made == 16 && ss.evictions == 0 && ss.bytes == 64u << 20);
    big.revision = 7000000; CHECK(textureUpscaleSample(&big, big.request, 4, &again));
    big.revision = 7000016; CHECK(textureUpscaleSample(&big, big.request, 4, &again));
    textureSampleStats(&ss); CHECK(ss.made == 17 && ss.evictions == 1 && ss.bytes == 64u << 20);
    big.revision = 7000000; CHECK(textureUpscaleSample(&big, big.request, 4, &again));
    textureSampleStats(&ss); CHECK(ss.made == 17);
    big.revision = 7000001; CHECK(textureUpscaleSample(&big, big.request, 4, &again));
    textureSampleStats(&ss); CHECK(ss.made == 18 && ss.evictions == 2 && ss.entries == 16);
    textureSampleReset();
    puts("Texture cache: content keys, CLUTs, wrapping, provenance, stable regions and LRU passed");
    printf("%d checks passed\n", checks);
    return 0;
}
