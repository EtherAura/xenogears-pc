/* Replay immutable field snapshots into private GPU images. Verify endpoints,
 * a real midpoint, native/hi-res isolation, texture ownership and resets. */
#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "common.h"
#include "psyq.h"
#include "pc_interpolation.h"
#include "pc_geometry.h"
#include "pc_glyph.h"
#include "gte_pc.h"
#include "gpu/gpu.h"
#include "gpu/gpu_internal.h"
#include "gpu/interpolation.h"
#include "gpu/shaderchain.h"
#include "pack_fixture.h"
#include "glyph_fixture.h"
#include "test/env.h"
#include "system/savestate.h"

int gpuInit(SDL_Window* window);
void gpuShutdown(void);
static int checks;
#include "test/check.h"
static POLY_F4 quad;
static char directory[1024];
static uint8_t* picture;
static int pictureW = 320, pictureH = 224;
static int owner;
static unsigned base;
static int flip;
static GpuDisplay display = {0, 0, 320, 224, 0, 1, 0, 0, 0, 0, 0};

static void draw(int x) {
    if (flip) {
        display.y = 256 - display.y;
        DRAWENV env; SetDefDrawEnv(&env, 0, display.y, 320, 224); PutDrawEnv(&env);
    }
    pcInterpolationFrameBegin(8, 0, display.y, 320, 224);
    RECT r = {0, display.y, 320, 224}; ClearImage(&r, 0, 0, 0);
    SetPolyF4(&quad); setRGB0(&quad, 248, 0, 0);
    quad.x0 = quad.x2 = x; quad.x1 = quad.x3 = x + 20;
    quad.y0 = quad.y1 = 40; quad.y2 = quad.y3 = 60;
    pcInterpolationObject(&owner, &quad); pcInterpolationModel(&quad); pcInterpolationPrim(&quad);
    DrawPrim(&quad); pcInterpolationObject(NULL, NULL);
    pcInterpolationFrameEnd(); interpolationPacketsDone();
}
static int shot(unsigned vblank) {
    vblank += base;
    gvkPresent(&display, vblank);
    char path[1200]; snprintf(path, sizeof(path), "%s/present_%06u.png", directory, vblank);
    SDL_Surface* p = SDL_LoadPNG(path);
    if (!p) return picture != NULL; /* dumps retain an unchanged picture */
    SDL_Surface* rgb = SDL_ConvertSurface(p, SDL_PIXELFORMAT_RGB24); SDL_DestroySurface(p);
    if (!rgb || rgb->w != pictureW || rgb->h != pictureH) { SDL_DestroySurface(rgb); return 0; }
    uint8_t* next = realloc(picture, (size_t)pictureW * pictureH * 3);
    if (!next) { SDL_DestroySurface(rgb); return 0; }
    picture = next;
    for (int y = 0; y < pictureH; y++) memcpy(picture + y * pictureW * 3, (uint8_t*)rgb->pixels + y * rgb->pitch, pictureW * 3);
    SDL_DestroySurface(rgb); return 1;
}
static int red(int x) { return picture[(50 * 320 + x) * 3] > 240; }
static int run(void) {
    base += 1000;
    CHECK(!interpolationEnabled()); interpolationSetEnabled(1);
    CHECK(gtePrecisionEnabled() && !pcGeometryActive());
    DRAWENV env; SetDefDrawEnv(&env, 0, display.y, 320, 224); PutDrawEnv(&env);
    draw(40); draw(40); CHECK(shot(100)); CHECK(red(40) && !red(39) && !red(60));
    draw(50); CHECK(shot(102)); /* exact earlier endpoint */
    CHECK(red(40) && !red(39) && !red(60));
    static uint16_t native[320 * 224], again[320 * 224];
    gvkReadback((VRect){0, display.y, 319, display.y + 223}, native, 320);
    CHECK(native[50 * 320 + 40] == 0 && native[50 * 320 + 50] == 31);
    CHECK(shot(103)); CHECK(red(45) && !red(44) && !red(65));
    gvkReadback((VRect){0, display.y, 319, display.y + 223}, again, 320);
    CHECK(!memcmp(native, again, sizeof(native)));
    CHECK(shot(104)); CHECK(red(50) && !red(49) && !red(70));
    int before, after; double alpha;
    CHECK(interpolationPicture(&display, base + 104, &before, &after, &alpha));
    pcInterpolationHeapFree(&owner, sizeof(owner));
    CHECK(!interpolationPicture(&display, base + 105, &before, &after, &alpha));
    draw(40); CHECK(shot(106)); draw(50); CHECK(shot(108));
    CHECK(interpolationPicture(&display, base + 108, &before, &after, &alpha));
    /* Readbacks preserve owned history; edits to the framebuffer reset it. */
    RECT rect = {0, display.y, 320, 224}; StoreImage(&rect, (u32*)again);
    CHECK(interpolationPicture(&display, base + 109, &before, &after, &alpha));
    MoveImage(&rect, 512, 0);
    CHECK(interpolationPicture(&display, base + 109, &before, &after, &alpha));
    LoadImage(&rect, (u32*)again);
    CHECK(!interpolationPicture(&display, base + 109, &before, &after, &alpha));
    draw(40); CHECK(shot(110)); draw(50); CHECK(shot(112));
    pcInterpolationScene();
    CHECK(!interpolationPicture(&display, base + 113, &before, &after, &alpha));
    draw(40); CHECK(shot(114)); draw(50); CHECK(shot(116));
    gpuStateLoaded(); CHECK(!interpolationPicture(&display, base + 117, &before, &after, &alpha));
    draw(40); CHECK(shot(118)); draw(50); CHECK(shot(120));
    draw(40); CHECK(shot(130)); draw(50); CHECK(shot(133));
    CHECK(shot(134)); CHECK(red(44) && !red(42) && !red(65));
    CHECK(shot(135)); CHECK(red(48) && !red(45) && !red(68));
    draw(40); CHECK(shot(140)); draw(50); CHECK(shot(144));
    CHECK(shot(145)); CHECK(red(44) && !red(41) && !red(64));
    CHECK(shot(146)); CHECK(red(45) && !red(44) && !red(65));
    CHECK(shot(147)); CHECK(red(49) && !red(46) && !red(69));
    interpolationSetEnabled(0); CHECK(shot(150)); CHECK(red(50) && !red(49));
    return 0;
}
static POLY_FT4 textured;
static int texturePage = -1, textureClut;
static int keepTexturePage;
static RECT textureWindow;
static void texturedDraw(int x) {
    DRAWENV env; SetDefDrawEnv(&env, 0, 0, 320, 224); env.tw = textureWindow;
    if (keepTexturePage) env.tpage=texturePage<0?GetTPage(2,0,640,0):texturePage;
    PutDrawEnv(&env);
    pcInterpolationFrameBegin(8, 0, 0, 320, 224);
    RECT r = {0, 0, 320, 224}; ClearImage(&r, 0, 0, 0);
    SetPolyFT4(&textured); SetShadeTex(&textured, 1);
    textured.x0 = textured.x2 = x; textured.x1 = textured.x3 = x + 20;
    textured.y0 = textured.y1 = 40; textured.y2 = textured.y3 = 60;
    textured.u0 = textured.u2 = textured.v0 = textured.v1 = 0;
    textured.u1 = textured.u3 = textured.v2 = textured.v3 = 15;
    textured.tpage = texturePage < 0 ? GetTPage(2, 0, 640, 0) : texturePage; textured.clut = textureClut;
    pcInterpolationObject(&owner, &textured); pcInterpolationModel(&textured); pcInterpolationPrim(&textured);
    DrawPrim(&textured); pcInterpolationObject(NULL, NULL);
    pcInterpolationFrameEnd(); interpolationPacketsDone();
}
static int resources(void) {
    base += 1000; flip = 0; display.y = 0; interpolationSetEnabled(1);
    static uint16_t pixels[16 * 16]; RECT rect = {640, 0, 16, 16};
    for (int i = 0; i < 256; i++) pixels[i] = 31;
    LoadImage(&rect, (u32*)pixels);
    texturedDraw(40); CHECK(shot(100)); texturedDraw(50); CHECK(shot(102));
    for (int i = 0; i < 256; i++) pixels[i] = 31 << 10;
    LoadImage(&rect, (u32*)pixels); /* mutate the original after both captures */
    CHECK(shot(103)); CHECK(red(48)); /* owned snapshot still has red texels */
    uint32_t hiBefore, hiAfter;
    CHECK(gvkHiRead(0, &hiBefore, 110, 100, 1, 1));
    CHECK(shot(104)); CHECK(red(55));
    CHECK(gvkHiRead(0, &hiAfter, 110, 100, 1, 1) && hiBefore == hiAfter);
    texturedDraw(60); CHECK(shot(106)); CHECK(red(65)); /* changed face retains its authored old resource */
    int before, after; double alpha;
    CHECK(interpolationPicture(&display, base + 106, &before, &after, &alpha));
    texturedDraw(70); CHECK(shot(108)); texturedDraw(80);
    textureSetFilter(PC_TEX_3D, TEXTURE_FILTER_LINEAR);
    CHECK(shot(110));
    CHECK(interpolationPicture(&display, base + 110, &before, &after, &alpha));
    textureSetFilter(PC_TEX_3D, TEXTURE_FILTER_NEAREST);
    pcInterpolationScene();
    hiresSetUpscale(1); CHECK(gvkHiConfigure(0, HIRES_COLOR_DITHER_1X) == 0);
    uint16_t indices[4 * 16], palette[16];
    for (int i = 0; i < 64; i++) indices[i] = 0x1111;
    for (int i = 0; i < 16; i++) palette[i] = 31;
    RECT indexed = {704, 0, 4, 16}, pal = {960, 480, 16, 1};
    LoadImage(&indexed, (u32*)indices); LoadImage(&pal, (u32*)palette);
    texturePage = GetTPage(0, 0, 704, 0); textureClut = GetClut(960, 480);
    texturedDraw(40); texturedDraw(40); CHECK(shot(120)); texturedDraw(50); CHECK(shot(122)); CHECK(shot(123)); CHECK(red(48));
    for (int i = 0; i < 16; i++) palette[i] = 31 << 10;
    /* GP0 upload without LIBGPU LoadImage's explicit cache clear. */
    gpuLoad(pal.x, pal.y, pal.w, pal.h, palette); /* cached CLUT remains red */
    texturedDraw(60); CHECK(shot(124)); CHECK(red(65));
    CHECK(!interpolationPicture(&display, base + 124, &before, &after, &alpha));
    interpolationSetEnabled(0);
    return 0;
}
static int dirtyCacheFootprint(void) {
    base+=1000;display.y=0;flip=0;wideReset();presentSetExpandedVertical(0);
    hiresSetUpscale(1);CHECK(gvkHiConfigure(0,HIRES_COLOR_DITHER_1X)==0);
    texturePage=-1;textureClut=0;textureWindow=(RECT){0};keepTexturePage=1;
    uint16_t pixels[16*16];for(int i=0;i<256;i++)pixels[i]=31;
    RECT rect={640,0,16,16};LoadImage(&rect,(u32*)pixels);
    interpolationSetEnabled(0);texturedDraw(40);
    gpuFill(800,0,16,16,0xf80000); /* dirty the same page, outside the sampled patch */
    CHECK(!gp0InterpolationCaptureSafe());
    TextureRequest request={GetTPage(2,0,640,0),0,0,0,0,16,16,0,0};
    const DecodedTexture* d=textureCacheGet(request);CHECK(d);
    DecodedTexture checked=*d;
    uint32_t before[1280],after[1280];gp0InterpolationCache(before);
    CHECK(gp0InterpolationTextureReason(d)==NULL);
    gp0InterpolationCache(after);CHECK(!memcmp(before,after,sizeof(before)));
    interpolationSetEnabled(1);texturedDraw(40);texturedDraw(40);CHECK(shot(100));
    texturedDraw(50);CHECK(shot(102));CHECK(shot(103));CHECK(red(48));
    int a,b;double alpha;CHECK(interpolationPicture(&display,base+103,&a,&b,&alpha));
    CHECK(!gp0InterpolationCaptureSafe()); /* the unrelated dirt was not cleared */
    /* Rendered-source decoding has its own guard; directly verify that
     * this guard also rejects a stale word inside the same UV footprint. */
    gpuFill(640,0,1,1,0xf80000);
    gp0InterpolationCache(before);
    const char* reason=gp0InterpolationTextureReason(&checked);CHECK(reason&&!strcmp(reason,"dirty-texture-cache"));
    gp0InterpolationCache(after);CHECK(!memcmp(before,after,sizeof(before)));
    keepTexturePage=0;interpolationSetEnabled(0);return 0;
}
static int nativeWordSamples(void) {
    texturePage=-1;textureClut=0;textureWindow=(RECT){0};
    uint16_t texels[256*32];uint8_t rgba[256*32*4];
    for(int i=0;i<256*32;i++) {
        unsigned c=texels[i]=(i==5*256+5||i==5*256+6||i==6*256+5)?32768:i%10?(uint16_t)((i+1)|(i%2?32768:0)):0;
        for(int k=0;k<3;k++){unsigned v=c>>(5*k)&31;rgba[i*4+k]=(uint8_t)(v<<3|v>>2);}
        rgba[i*4+3]=c?(c&32768?128:255):0;
    }
    for(int n=1;n<=2;n++) {
        base+=1000;display.y=0;flip=0;wideReset();presentSetExpandedVertical(0);hiresSetUpscale(n);
        CHECK(gvkHiConfigure(n==1?0:n,HIRES_COLOR_DITHER_1X)==(n==1?0:n));
        textureSetFilter(PC_TEX_3D,n==1?TEXTURE_FILTER_NEAREST:TEXTURE_FILTER_EDGE);
        RECT rect={640,0,256,32};LoadImage(&rect,(u32*)texels);
        interpolationSetEnabled(1);texturedDraw(40);texturedDraw(40);CHECK(shot(100));
        texturedDraw(50);CHECK(shot(102));
        int a,b;double alpha;const InterpFrame* f=interpolationPicture(&display,base+103,&a,&b,&alpha);
        CHECK(f&&f->pixelCount==256*32/2&&(f->vertices[0].sample[3]&INTERP_SAMPLE_NATIVE_WORD));
        size_t count=(size_t)320*224*n*n;uint32_t* packed=malloc(count*4),*reference=malloc(count*4);
        CHECK(packed&&reference);
        if(n==1) {
            /* A coverage bit preserves opaque black introduced by filtering.
             * Compare its GPU decode against the same owned, uncompressed
             * RGBA source, with identical geometry and sampling. */
            uint8_t source[sizeof(rgba)];memcpy(source,rgba,sizeof(source));
            memset(source+(5*256+5)*4,0,3);source[(5*256+5)*4+3]=255;
            for(int which=0;which<2;which++) {
                InterpFrame* frame=(InterpFrame*)interpolationFrame(which?b:a);frame->pixelCount=0;
                uint32_t offset,format;CHECK(interpFrameSample(frame,source,256*32,&offset,&format));
                CHECK(format&INTERP_SAMPLE_OPAQUE_BLACK);
                for(uint32_t i=0;i<frame->vertexCount;i++)frame->vertices[i].sample[3]=(frame->vertices[i].sample[3]&65535)|format;
            }
            CHECK(shot(103));CHECK(gvkInterpolationRead(packed,0,0,320,224));
            static uint16_t before[VRAM_W*VRAM_H],after[VRAM_W*VRAM_H];gpuReadVram(before);
            for(int which=0;which<2;which++) {
                InterpFrame* frame=(InterpFrame*)interpolationFrame(which?b:a);frame->pixelCount=0;
                uint32_t offset;CHECK(interpFramePixels(frame,source,256*32,&offset));
                for(uint32_t i=0;i<frame->vertexCount;i++)frame->vertices[i].sample[3]&=65535;
            }
            CHECK(shot(103));CHECK(gvkInterpolationRead(reference,0,0,320,224));
            gpuReadVram(after);CHECK(!memcmp(before,after,sizeof(before)));
        } else {
            /* Compare the same moving geometry against the original
             * per-face CPU Scale2x snapshots, at every enhanced pixel. */
            CHECK(shot(103));CHECK(gvkInterpolationRead(packed,0,0,640,448));
            for(int which=0;which<2;which++) {
                InterpFrame* frame=(InterpFrame*)interpolationFrame(which?b:a);frame->pixelCount=0;
                DecodedTexture original={0};original.request=(TextureRequest){GetTPage(2,0,640,0),0,0,0,0,256,32,0,0};
                original.rgba=rgba;original.revision=UINT64_MAX-(unsigned)which;
                for(uint32_t d=0;d<frame->drawCount;d++) {
                    const InterpDraw* draw=&frame->draws[d];HiVertex* v=&frame->vertices[draw->first];
                    TextureRequest patch=original.request;patch.u=(int16_t)v->uvMin;patch.v=(int16_t)(v->uvMin>>16);
                    patch.w=(int16_t)v->uvMax-patch.u+1;patch.h=(int16_t)(v->uvMax>>16)-patch.v+1;
                    TextureSample sample;CHECK(textureSampleGet(&original,patch,TEXTURE_FILTER_EDGE,&sample));
                    uint32_t offset;CHECK(interpFramePixels(frame,sample.rgba,(size_t)sample.w*sample.h*sample.scale*sample.scale,&offset));
                    for(uint32_t i=0;i<draw->count;i++) {
                        v[i].sample[0]=offset;v[i].sample[1]=(uint32_t)(uint16_t)sample.u|(uint32_t)(uint16_t)sample.v<<16;
                        v[i].sample[2]=(uint32_t)sample.w|(uint32_t)sample.h<<16;
                        v[i].sample[3]=(TEXTURE_FILTER_EDGE+1)|(uint32_t)sample.scale<<8;
                    }
                }
            }
            CHECK(shot(103));CHECK(gvkInterpolationRead(reference,0,0,640,448));
        }
        unsigned different=0;
        for(size_t i=0;i<count;i++)if((packed[i]&0x1ffffff)!=(reference[i]&0x1ffffff)) {
            if(different++<12)fprintf(stderr,"owned native-word mismatch n%d %zu,%zu: %08x/%08x\n",n,i%(320*n),i/(320*n),packed[i],reference[i]);
        }
        CHECK(!different);free(packed);free(reference);interpolationSetEnabled(0);
    }
    textureSetFilter(PC_TEX_3D,TEXTURE_FILTER_NEAREST);hiresSetUpscale(1);
    CHECK(gvkHiConfigure(0,HIRES_COLOR_DITHER_1X)==0);return 0;
}
static int windows(void) {
    base += 1000; flip = 0; display.y = 0;
    texturePage = -1; textureClut = 0;
    textureWindow = (RECT){8, 0, 8, 16};
    interpolationSetEnabled(1);
    static uint16_t pixels[16 * 16], native[320 * 224];
    for (int y = 0; y < 16; y++) for (int x = 0; x < 16; x++) pixels[y * 16 + x] = x < 8 ? 31 << 10 : 31;
    RECT rect = {640, 0, 16, 16}; LoadImage(&rect, (u32*)pixels);
    /* A preceding fixture may have changed resolution. Prime its new
     * private target before measuring the pair's first displayed endpoint. */
    texturedDraw(40); texturedDraw(40); CHECK(shot(100)); texturedDraw(50); CHECK(shot(102));
    CHECK(shot(103)); CHECK(red(45) && !red(44) && !red(65));
    gvkReadback((VRect){0, 0, 319, 223}, native, 320);
    CHECK(native[50 * 320 + 50] == 31 && native[50 * 320 + 40] == 0);
    int before, after; double alpha;
    CHECK(interpolationPicture(&display, base + 103, &before, &after, &alpha) && alpha == 0.5);
    textureWindow = (RECT){0}; interpolationSetEnabled(0);
    return 0;
}
static int partOwners[2], partModels[3];
static POLY_F4 parts[2][2];
static unsigned partBuffer;
static void partDraw(int delta, int swap) {
    pcInterpolationFrameBegin(172, 0, 0, 320, 224);
    RECT r = {0, 0, 320, 224}; ClearImage(&r, 0, 0, 0);
    for (int i = 0; i < 2; i++) {
        POLY_F4* q = &parts[partBuffer & 1][i];
        int x = i ? 100 - delta : 40 + delta;
        SetPolyF4(q); setRGB0(q, i ? 0 : 248, i ? 248 : 0, 0);
        q->x0 = q->x2 = x; q->x1 = q->x3 = x + 20;
        q->y0 = q->y1 = 40; q->y2 = q->y3 = 60;
        pcInterpolationKey(&partOwners[i], &partModels[i + (!i && swap ? 2 : 0)], 7, q);
        DrawPrim(q);
    }
    partBuffer++; pcInterpolationFrameEnd(); interpolationPacketsDone();
}
static int identities(void) {
    base += 1000; flip = 0; display.y = 0;
    hiresSetUpscale(1); CHECK(gvkHiConfigure(0, HIRES_COLOR_DITHER_1X) == 0);
    DRAWENV env; SetDefDrawEnv(&env, 0, 0, 320, 224); PutDrawEnv(&env);
    interpolationSetEnabled(1);
    partDraw(0, 0); CHECK(shot(100)); partDraw(10, 0); CHECK(shot(102)); CHECK(shot(103));
    CHECK(red(45) && !red(44));
    CHECK(picture[(50 * 320 + 95) * 3 + 1] > 240 && picture[(50 * 320 + 94) * 3 + 1] == 0);
    int before, after; double alpha;
    CHECK(interpolationPicture(&display, base + 103, &before, &after, &alpha));
    pcInterpolationLifetime(&partOwners[0]); partDraw(20, 0); CHECK(shot(104));
    CHECK(interpolationPicture(&display, base + 104, &before, &after, &alpha));
    CHECK(shot(105)); CHECK(red(50) && !red(49));
    CHECK(picture[(50 * 320 + 85) * 3 + 1] > 240 && picture[(50 * 320 + 84) * 3 + 1] == 0);
    partDraw(25, 0); CHECK(shot(106)); CHECK(shot(107));
    CHECK(interpolationPicture(&display, base + 107, &before, &after, &alpha));
    partDraw(30, 1); CHECK(shot(108));
    CHECK(interpolationPicture(&display, base + 108, &before, &after, &alpha));
    CHECK(shot(109)); CHECK(red(65) && !red(64));
    pcInterpolationHeapFree(partOwners, sizeof(partOwners));
    CHECK(!interpolationPicture(&display, base + 109, &before, &after, &alpha));
    interpolationSetEnabled(0); return 0;
}
static void placementDraw(int root, int pose) {
    pcInterpolationFrameBegin(8, 0, 0, 320, 224);
    RECT r = {0, 0, 320, 224}; ClearImage(&r, 0, 0, 0);
    for (int i = 0; i < (pose ? 1 : 2); i++) {
        POLY_F4* q = &parts[0][i];
        int x = root + (pose ? 8 : 24 * i);
        SetPolyF4(q); setRGB0(q, pose ? 0 : 248, pose ? 248 : 0, 0);
        q->x0 = q->x2 = x; q->x1 = q->x3 = x + (pose ? 30 : 20);
        q->y0 = q->y1 = 40; q->y2 = q->y3 = 60;
        pcInterpolationPlacement(&owner, &parts, pose ? 99 : 7 + i, q, root, 60, 100);
        DrawPrim(q);
    }
    pcInterpolationFrameEnd(); interpolationPacketsDone();
}
static int placements(void) {
    base += 1000; flip = 0; display.y = 0;
    hiresSetUpscale(1); CHECK(gvkHiConfigure(0, HIRES_COLOR_DITHER_1X) == 0);
    DRAWENV env; SetDefDrawEnv(&env, 0, 0, 320, 224); PutDrawEnv(&env);
    interpolationSetEnabled(1);
    placementDraw(40, 0); CHECK(shot(100)); placementDraw(50, 1); CHECK(shot(102)); CHECK(shot(103));
    /* Both old parts move together even though neither exists in the new
     * animation layout. The new colour/shape appears on its native step. */
    CHECK(red(45) && !red(44) && red(69) && !red(89));
    CHECK(picture[(50 * 320 + 50) * 3 + 1] == 0);
    uint16_t native[320 * 224]; gvkReadback((VRect){0, 0, 319, 223}, native, 320);
    CHECK(native[50 * 320 + 58] == (31 << 5) && native[50 * 320 + 45] == 0);
    CHECK(shot(104)); CHECK(!red(58) && picture[(50 * 320 + 58) * 3 + 1] > 240);
    interpolationSetEnabled(0); return 0;
}
static int filters(void) {
    base += 1000; display.y = 0; flip = 0; texturePage = -1; textureClut = 0;
    static uint16_t pixels[16 * 16]; RECT rect = {640, 0, 16, 16};
    for (int i = 0; i < 256; i++) pixels[i] = 31;
    LoadImage(&rect, (u32*)pixels);
    for (int n = 2; n <= 4; n += 2) for (int filter = 1; filter <= TEXTURE_FILTER_EDGE; filter++) {
        hiresSetUpscale(n); CHECK(gvkHiConfigure(n, HIRES_COLOR_DITHER_1X) == n);
        textureSetFilter(PC_TEX_3D, filter); interpolationSetEnabled(1);
        texturedDraw(40); texturedDraw(40); CHECK(shot(100)); texturedDraw(50); CHECK(shot(102)); CHECK(shot(103));
        CHECK(red(50) && !red(35));
        int before, after; double alpha;
        CHECK(interpolationPicture(&display, base + 103, &before, &after, &alpha) && alpha == 0.5);
        interpolationSetEnabled(0); base += 100;
    }
    textureSetFilter(PC_TEX_3D, TEXTURE_FILTER_NEAREST);
    return 0;
}
static int replacements(void) {
    base += 1000; display.y = 0; flip = 0;
    hiresSetUpscale(2); CHECK(gvkHiConfigure(2, HIRES_COLOR_DITHER_1X) == 2);
    uint16_t indices[4 * 16], palette[16] = {0, 31, 31 << 5};
    for (int i = 0; i < 64; i++) indices[i] = 0x1111;
    RECT image = {704, 0, 4, 16}, clut = {960, 480, 16, 1};
    LoadImage(&image, (u32*)indices); LoadImage(&clut, (u32*)palette);
    texturePage = GetTPage(0, 0, 704, 0); textureClut = GetClut(960, 480);
    TextureRequest req = {texturePage, textureClut, 0, 0, 0, 16, 16, 0, 0};
    const DecodedTexture* original = textureCacheGet(req); CHECK(original);
    DecodedTexture key = *original;
    char root[1200]; snprintf(root, sizeof(root), "%s/pack", directory); CHECK(packDir(root));
    uint8_t replacement[32 * 32 * 4];
    for (int i = 0; i < 32 * 32; i++) { memset(replacement + i * 4, 2, 3); replacement[i * 4 + 3] = 255; }
    CHECK(packPng(root, &key, 1, 32, 32, replacement)); texturePackSetRoot(root);
    interpolationSetEnabled(1); texturedDraw(40); texturedDraw(40); CHECK(shot(100));
    texturedDraw(50); CHECK(shot(102));
    palette[2] = 31 << 10; LoadImage(&clut, (u32*)palette);
    CHECK(shot(103)); CHECK(picture[(50 * 320 + 50) * 3 + 1] > 240); /* owned old palette */
    uint16_t native[320 * 224]; gvkReadback((VRect){0, 0, 319, 223}, native, 320);
    CHECK(native[50 * 320 + 50] == 31);
    key = *textureCacheGet(req);
    for (int i = 0; i < 32 * 32; i++) { replacement[i * 4] = replacement[i * 4 + 1] = 0; replacement[i * 4 + 2] = 255; }
    CHECK(packPng(root, &key, 0, 32, 32, replacement)); texturePackReload();
    pcInterpolationScene(); texturedDraw(40); CHECK(shot(110)); texturedDraw(50); CHECK(shot(112)); CHECK(shot(113));
    CHECK(picture[(50 * 320 + 50) * 3 + 2] > 240);
    int before, after; double alpha;
    CHECK(interpolationPicture(&display, base + 113, &before, &after, &alpha) && alpha == 0.5);
    texturePackSetRoot("none"); interpolationSetEnabled(0); return 0;
}
static int presets(void) {
    if (!shaderChainAvailable()) return 0;
    base += 1000; display.y = 0; flip = 0;
    hiresSetUpscale(1); CHECK(gvkHiConfigure(0, HIRES_COLOR_DITHER_1X) == 0);
    shaderChainEdit(0); CHECK(shaderChainSetPath(XG_ROOT "/port/src/gpu/test/slang/twopass.slangp"));
    interpolationSetEnabled(1);
    draw(40); draw(40); CHECK(shot(100)); draw(50); CHECK(shot(102)); CHECK(shot(103));
    int before, after; double alpha;
    CHECK(interpolationPicture(&display, base + 103, &before, &after, &alpha) && alpha == 0.5);
    CHECK(picture[(50 * 320 + 50) * 3] > 0 && picture[(50 * 320 + 35) * 3] == 0);
    shaderChainSetPath(""); CHECK(shaderChainPasses() == 0); interpolationSetEnabled(0); return 0;
}
static SPRT caption;
static int captionTpage=18;
static void captionDraw(int x, int clut) {
    DRAWENV env; SetDefDrawEnv(&env, 0, 0, 320, 224); env.tpage = captionTpage; PutDrawEnv(&env);
    pcInterpolationFrameBegin(8, 0, 0, 320, 224);
    RECT r = {0, 0, 320, 224}; ClearImage(&r, 0, 0, 0);
    SetPolyF4(&quad); setRGB0(&quad, 248, 0, 0);
    quad.x0 = quad.x2 = x; quad.x1 = quad.x3 = x + 20;
    quad.y0 = quad.y1 = 40; quad.y2 = quad.y3 = 60;
    pcInterpolationObject(&owner, &quad); pcInterpolationModel(&quad); pcInterpolationPrim(&quad);
    DrawPrim(&quad); pcInterpolationObject(NULL, NULL);
    SetSprt(&caption); SetShadeTex(&caption, 1);
    caption.x0 = 100; caption.y0 = 40; caption.u0 = caption.v0 = 0;
    caption.w = 8; caption.h = 13; caption.clut = clut;
    pcTextureClass(&caption, PC_TEX_TEXT); DrawPrim(&caption);
    pcInterpolationFrameEnd(); interpolationPacketsDone();
}
static int captions(void) {
    base += 1000; display.y = 0; flip = 0;
    hiresSetUpscale(4); CHECK(gvkHiConfigure(4, HIRES_COLOR_RAW) == 4);
    uint16_t image[6 * 13], palette[16]; uint8_t even[8 * 13], odd[8 * 13];
    glyphImage(image, 6, even, odd);
    for (int i = 0; i < 16; i++) palette[i] = 31;
    palette[14] = 31 << 5;
    pcGlyphDialog(image, 6, 8, 0); pcGlyphDialog(image, 6, 8, 1);
    RECT rect = {128, 256, 6, 13}, pal = {976, 480, 16, 1};
    LoadImage(&rect, image); LoadImage(&pal, palette);
    char root[1200]; snprintf(root, sizeof(root), "%s/glyph-pack", directory);
    uint8_t authored[24 * 39]; memset(authored, 2, sizeof(authored));
    CHECK(glyphPng(root, textureGlyphKey(even, 8, 13, 4), 8, 13, authored, 3));
    texturePackSetRoot(root); textureSetGlyphs(1); textureSetGlyphSmooth(0); interpolationSetEnabled(1);
    captionDraw(40, GetClut(976, 480)); captionDraw(40, GetClut(976, 480)); CHECK(shot(100));
    captionDraw(50, GetClut(976, 480)); CHECK(shot(102));
    palette[14] = 31 << 10; LoadImage(&pal, palette);
    CHECK(shot(103)); CHECK(red(45) && !red(44));
    CHECK(picture[(50 * 320 + 107) * 3 + 1] > 240); /* owned enhanced glyph/old palette */
    uint16_t native[320 * 224]; gvkReadback((VRect){0, 0, 319, 223}, native, 320);
    CHECK(native[50 * 320 + 107] == 31); /* native packed text remains red here */
    int before, after; double alpha;
    CHECK(interpolationPicture(&display, base + 103, &before, &after, &alpha) && alpha == 0.5);
    /* Missing authored glyphs must replay as exact nearest native blocks,
     * even with an old Linear/Edge text setting. Check the full 4x image. */
    texturePackSetRoot("");palette[14]=31<<5;LoadImage(&pal,palette);
    uint32_t blocks[8*13*16];
    for(int filter=TEXTURE_FILTER_LINEAR;filter<=TEXTURE_FILTER_EDGE;filter++) {
        textureSetFilter(PC_TEX_TEXT,filter);
        CHECK(textureFilter(PC_TEX_TEXT)==TEXTURE_FILTER_NEAREST);
        unsigned vb=106+(unsigned)(filter-TEXTURE_FILTER_LINEAR)*6;
        captionDraw(40,GetClut(976,480));captionDraw(40,GetClut(976,480));CHECK(shot(vb));
        captionDraw(50,GetClut(976,480));CHECK(shot(vb+2));CHECK(shot(vb+3));
        CHECK(interpolationPicture(&display,base+vb+3,&before,&after,&alpha) && alpha==0.5);
        CHECK(gvkInterpolationRead(blocks,400,160,32,52));
        CHECK(glyphNearestPixels(blocks,4,even,odd,palette));
    }
    textureSetFilter(PC_TEX_TEXT,TEXTURE_FILTER_NEAREST);
    /* A glyph in a full font atlas must retain its actual authored detail.
     * Composing the whole 256-square upload drops an 8x glyph to 4x. */
    textureGlyphReset();uint16_t* atlas=calloc(64*256,2);CHECK(atlas);
    glyphImage(atlas,64,even,odd);pcGlyphDialog(atlas,64,8,0);pcGlyphDialog(atlas,64,8,1);
    gp0SetMaskBits(0);
    captionTpage=GetTPage(0,0,640,256);
    RECT atlasRect={640,256,64,256};LoadImage(&atlasRect,atlas);free(atlas);
    uint8_t fine[64*104];for(int y=0;y<104;y++)for(int x=0;x<64;x++)fine[y*64+x]=((x+y)&1)?1:2;
    CHECK(glyphPng(root,textureGlyphKey(even,8,13,4),8,13,fine,8));texturePackSetRoot(root);
    TextureRequest glyphRequest={(uint16_t)captionTpage,(uint16_t)GetClut(976,480),0,0,0,8,13,0,0};
    DrawSync(0);
    TextureRequest fullAtlas=textureRequestSnap(glyphRequest);
    CHECK(fullAtlas.w==256 && fullAtlas.h==256);
    hiresSetUpscale(8);CHECK(gvkHiConfigure(8,HIRES_COLOR_RAW)==8);
    captionDraw(40,GetClut(976,480));captionDraw(40,GetClut(976,480));CHECK(shot(120));
    captionDraw(50,GetClut(976,480));CHECK(shot(122));
    uint32_t direct[64*104],replayed[64*104];CHECK(gvkHiRead(0,direct,800,320,64,104));
    CHECK(shot(123));CHECK(gvkInterpolationRead(replayed,800,320,64,104));
    int detail=1;for(int i=0;i<64*104;i++)if((direct[i]&0x1ffffff)!=(fine[i]==1?0xffu:0xff00u) || (replayed[i]&0x1ffffff)!=(direct[i]&0x1ffffff))detail=0;
    CHECK(detail);
    captionTpage=18;
    interpolationSetEnabled(0); textureSetGlyphs(0); texturePackSetRoot("none"); textureGlyphReset();
    return 0;
}
static POLY_F4 expandedParts[10];
static int expandedOwners[7];
static POLY_G4 backdrop;
static POLY_G4 ditherPanel;
static void expandedDraw(int delta, int height) {
    DRAWENV env; SetDefDrawEnv(&env, 0, display.y, 320, height); env.dtd = 1; PutDrawEnv(&env);
    pcInterpolationFrameBegin(8, 0, display.y, 320, height);
    RECT r = {0, display.y, 320, height}; ClearImage(&r, 0, 0, 0);
    SetPolyG4(&backdrop);
    backdrop.x0 = backdrop.x2 = 0; backdrop.x1 = backdrop.x3 = 320;
    backdrop.y0 = backdrop.y1 = 0; backdrop.y2 = backdrop.y3 = height;
    setRGB0(&backdrop, 0, 0, 64); setRGB1(&backdrop, 0, 0, 64);
    setRGB2(&backdrop, 0, 64, 0); setRGB3(&backdrop, 0, 64, 0);
    DrawPrim(&backdrop);
    SetPolyG4(&ditherPanel);
    ditherPanel.x0 = ditherPanel.x2 = 150; ditherPanel.x1 = ditherPanel.x3 = 170;
    ditherPanel.y0 = ditherPanel.y1 = 15; ditherPanel.y2 = ditherPanel.y3 = 200;
    setRGB0(&ditherPanel, 64, 64, 64); setRGB1(&ditherPanel, 64, 64, 64);
    setRGB2(&ditherPanel, 64, 64, 64); setRGB3(&ditherPanel, 64, 64, 64);
    DrawPrim(&ditherPanel);
    const int xs[] = {40, -30, 330, 70, 70, 100, 124, 300, 180, 220};
    const int ys[] = {40, 40, 40, -8, height + 2, 70, 70, 30, 40, 40};
    for (int i = 0; i < 10; i++) {
        POLY_F4* q = &expandedParts[i];
        int x = xs[i] + (i < 7 ? delta : 0);
        SetPolyF4(q); setRGB0(q, i < 7 || i == 9 ? 248 : 0, i == 8 ? 248 : 0, i == 7 || i == 9 ? 248 : 0);
        q->x0 = q->x2 = x; q->x1 = q->x3 = x + 20;
        q->y0 = q->y1 = ys[i]; q->y2 = q->y3 = ys[i] + (i == 3 || i == 4 ? 6 : 20);
        if (i < 5) pcInterpolationKeyType(&expandedOwners[i], q, 0, q, PC_INTERP_MODEL);
        else if (i < 7) pcInterpolationPlacement(&expandedOwners[5], &expandedParts, i, q, 100 + delta, 90, 100);
        else if (i == 7) pcWideAnchorRight(q);
        else pcWidePrim(q, 0, i == 8 ? PC_WIDE_ONLY : PC_WIDE_NATIVE_ONLY);
        DrawPrim(q);
    }
    pcInterpolationFrameEnd(); interpolationPacketsDone(); widePacketsDone();
}
/* Sample a native-coordinate location through the actual presentation layout,
 * retaining square geometry, TV borders and the chosen expanded framing. */
static int expandedColourFrom(const uint8_t* pixels, int x, int y, int channel) {
    PresentConstants pc; presentConstants(&display, pictureW, pictureH, &pc, NULL, 0);
    const WideView* view = wideCurrentView();
    if (view->wide) { x += view->m; y += view->top; }
    int px = pc.dest[0] + (int)((x + 0.5) * pc.dest[2] / pc.disp[2]);
    int py = pc.dest[1] + (int)((y + 0.5) * pc.dest[3] / pc.disp[3]);
    if (px < 0 || py < 0 || px >= pictureW || py >= pictureH) return -1;
    return pixels[((size_t)py * pictureW + px) * 3 + channel];
}
static int expandedColour(int x, int y, int channel) { return expandedColourFrom(picture, x, y, channel); }
static POLY_FT4 aliases[40];
static int aliasOwner;
static void aliasDraw(int delta) {
    DRAWENV env; SetDefDrawEnv(&env, 0, 0, 320, 224); PutDrawEnv(&env);
    pcInterpolationFrameBegin(8, 0, 0, 320, 224);
    RECT r = {0, 0, 320, 224}; ClearImage(&r, 0, 0, 0);
    for (int i = 0; i < 40; i++) {
        POLY_FT4* q = &aliases[i]; SetPolyFT4(q); SetShadeTex(q, 1);
        setXYWH(q, 10 + i % 8 * 25 + delta, 40 + i / 8 * 25, 20, 20);
        q->u0 = q->u2 = q->v0 = q->v1 = 0;
        q->u1 = q->u3 = q->v2 = q->v3 = 255;
        q->tpage = GetTPage(0, 0, 640, 0); q->clut = GetClut(i * 16, 480);
        pcInterpolationKeyType(&aliasOwner, aliases, i, q, PC_INTERP_MODEL);
        DrawPrim(q);
    }
    pcInterpolationFrameEnd(); interpolationPacketsDone();
}
static int textureAliases(void) {
    base += 1000; display = (GpuDisplay){0, 0, 320, 224, 0, 1};
    wideReset(); presentSetAspectText("1:1"); presentSetExpandedVertical(0);
    hiresSetUpscale(1); CHECK(gvkHiConfigure(0, HIRES_COLOR_DITHER_1X) == 0);
    static uint16_t indices[64 * 256], palettes[40 * 16];
    for (int i = 0; i < 64 * 256; i++) indices[i] = 0x1111;
    for (int i = 0; i < 40 * 16; i++) palettes[i] = 31;
    RECT page = {640, 0, 64, 256}, cluts = {0, 480, 640, 1};
    LoadImage(&page, (u32*)indices); LoadImage(&cluts, (u32*)palettes);
    interpolationSetEnabled(1); aliasDraw(0); aliasDraw(0); CHECK(shot(100));
    aliasDraw(4); CHECK(shot(102));
    int before, after; double alpha;
    const InterpFrame* f = interpolationPicture(&display, base + 102, &before, &after, &alpha);
    CHECK(f && f->pixelCount == 1 + 256 * 256 / 4); /* one owned colour + packed indices */
    CHECK(interpolationFrame(after)->pixelCount == f->pixelCount);
    for (int i = 39 * 16; i < 40 * 16; i++) palettes[i] = 31 << 10;
    LoadImage(&cluts, (u32*)palettes); /* old shared storage remains immutable */
    CHECK(shot(103)); CHECK(red(13) && !red(11));
    CHECK(picture[(145 * 320 + 190) * 3] > 240);
    aliasDraw(8); CHECK(shot(104)); aliasDraw(12); CHECK(shot(106));
    f = interpolationPicture(&display, base + 106, &before, &after, &alpha);
    CHECK(f && f->pixelCount == 2 * (1 + 256 * 256 / 4)); /* changed palette gets separate pixels */
    CHECK(shot(107));
    CHECK(picture[(145 * 320 + 198) * 3 + 2] > 240 && picture[(145 * 320 + 198) * 3] == 0);
    interpolationSetEnabled(0); return 0;
}
/* 110 distinct 256x256 samples exceed the persistent 32-MiB encoding
 * budget while fitting one owned frame. Eviction must never change an
 * earlier replay, and a later live CLUT edit must still appear. */
static POLY_FT4 pressureQuads[110];
static int pressureOwner;
static void pressureDraw(int delta) {
    DRAWENV env; SetDefDrawEnv(&env, 0, 0, 320, 224); env.dtd = 0; PutDrawEnv(&env);
    pcInterpolationFrameBegin(8, 0, 0, 320, 224);
    RECT r = {0, 0, 320, 224}; ClearImage(&r, 0, 0, 0);
    for (int i = 0; i < 110; i++) {
        POLY_FT4* q = &pressureQuads[i]; SetPolyFT4(q); SetShadeTex(q, 1);
        setXYWH(q, 10 + i % 11 * 25 + delta, 10 + i / 11 * 20, 20, 18);
        q->u0 = q->u2 = q->v0 = q->v1 = 0;
        q->u1 = q->u3 = q->v2 = q->v3 = 255;
        q->tpage = GetTPage(0, 0, 640, 0); q->clut = GetClut(i % 64 * 16, 480 + i / 64);
        pcInterpolationKeyType(&pressureOwner, pressureQuads, i, q, PC_INTERP_MODEL);
        DrawPrim(q);
    }
    pcInterpolationFrameEnd(); interpolationPacketsDone();
}
static int encodingPressure(void) {
    base += 1000; display = (GpuDisplay){0, 0, 320, 224, 0, 1};
    wideReset(); presentSetAspectText("1:1"); presentSetExpandedVertical(0);
    hiresSetUpscale(1); CHECK(gvkHiConfigure(0, HIRES_COLOR_DITHER_1X) == 0);
    static uint16_t indices[64 * 256], palettes[2048];
    for (int i = 0; i < 64 * 256; i++) indices[i] = 0x1111;
    for (int i = 0; i < 110; i++) for (int j = 0; j < 16; j++) palettes[i * 16 + j] = i ? i + 32 : 31;
    RECT page = {640, 0, 64, 256}, cluts = {0, 480, 1024, 2};
    LoadImage(&page, indices); LoadImage(&cluts, palettes);
    interpolationSetEnabled(1); pressureDraw(0); pressureDraw(0); CHECK(shot(100));
    pressureDraw(2); CHECK(shot(102));
    int before, after; double alpha;
    const InterpFrame* f = interpolationPicture(&display, base + 102, &before, &after, &alpha);
    CHECK(f && f->pixelCount == 110 * (1 + 256 * 256 / 4));
    CHECK(picture[(15 * 320 + 15) * 3] > 240);
    for (int j = 0; j < 16; j++) palettes[j] = 31 << 10;
    LoadImage(&cluts, palettes);
    CHECK(shot(103)); CHECK(picture[(15 * 320 + 15) * 3] > 240);
    pressureDraw(4); CHECK(shot(104)); pressureDraw(6); CHECK(shot(106));
    CHECK(shot(107));
    CHECK(picture[(15 * 320 + 18) * 3 + 2] > 240 && picture[(15 * 320 + 18) * 3] == 0);
    interpolationSetEnabled(0); return 0;
}
static POLY_G4 partialQuad;
static int partialOwner;
static void partialDraw(int delta, int half) {
    DRAWENV env; SetDefDrawEnv(&env, 0, 0, 320, 224); env.dtd = 0; PutDrawEnv(&env);
    pcInterpolationFrameBegin(8, 0, 0, 320, 224);
    RECT r = {0, 0, 320, 224}; ClearImage(&r, 0, 0, 0);
    SetPolyG4(&partialQuad); setXYWH(&partialQuad, 40 + delta, 40, 20, 20);
    setRGB0(&partialQuad, 248, 0, 0); setRGB1(&partialQuad, 248, 0, 0);
    setRGB2(&partialQuad, 248, 0, 0); setRGB3(&partialQuad, 248, 0, 0);
    if (half) { partialQuad.x3 = -1000; partialQuad.y3 = -500; }
    else { partialQuad.x0 = -1000; partialQuad.y0 = -500; }
    pcInterpolationKeyType(&partialOwner, &partialQuad, 0, &partialQuad, PC_INTERP_MODEL);
    DrawPrim(&partialQuad); pcInterpolationFrameEnd(); interpolationPacketsDone();
}
static int partialQuads(void) {
    static uint16_t native[320 * 224], again[320 * 224];
    for (int n = 1; n <= 2; n++) for (int half = 0; half < 2; half++) {
        base += 100; interpolationSetEnabled(1); hiresSetUpscale(n);
        CHECK(gvkHiConfigure(n == 1 ? 0 : n, HIRES_COLOR_DITHER_1X) == (n == 1 ? 0 : n));
        partialDraw(0, half); partialDraw(0, half); CHECK(shot(100));
        partialDraw(4, half); CHECK(shot(102));
        int before, after; double alpha;
        const InterpFrame* f = interpolationPicture(&display, base + 103, &before, &after, &alpha);
        CHECK(f && f->vertexCount == 3 && alpha == 0.5);
        gvkReadback((VRect){0, 0, 319, 223}, native, 320);
        CHECK(shot(103));
        int inside = half ? 45 : 57, outside = half ? 40 : 48;
        CHECK(red(inside) && !red(outside));
        gvkReadback((VRect){0, 0, 319, 223}, again, 320);
        CHECK(!memcmp(native, again, sizeof(native)));
        interpolationSetEnabled(0);
    }
    return 0;
}
static int resizeWindow(SDL_Window* window, int width, int height) {
    CHECK(SDL_SetWindowSize(window, width, height));
    /* X11 acknowledges even hidden-window resizes asynchronously. Drain
     * that acknowledgement before checking pixels from the new swapchain. */
    for (int attempt = 0; attempt < 100; attempt++) {
        int w, h;
        SDL_PumpEvents();
        CHECK(SDL_GetWindowSizeInPixels(window, &w, &h));
        if (w == width && h == height) { pictureW = w; pictureH = h; return 0; }
        SDL_Delay(10);
    }
    CHECK(0 && "window resize timed out");
    return 0;
}
/* The compass is projected, but deliberately remains a discrete HUD layer.
 * Its snapshot must retain the live enhanced draw's fractional positions
 * even though it has no object identity for motion interpolation. */
static POLY_F4 projectedHud;
static void projectedHudDraw(int x) {
    DRAWENV env; SetDefDrawEnv(&env, 0, 0, 320, 224); PutDrawEnv(&env);
    pcInterpolationFrameBegin(8, 0, 0, 320, 224);
    RECT r = {0, 0, 320, 224}; ClearImage(&r, 0, 0, 0);
    SetPolyF4(&quad); setRGB0(&quad, 248, 0, 0);
    quad.x0 = quad.x2 = x; quad.x1 = quad.x3 = x + 20;
    quad.y0 = quad.y1 = 40; quad.y2 = quad.y3 = 60;
    pcInterpolationObject(&owner, &quad); pcInterpolationModel(&quad); pcInterpolationPrim(&quad);
    DrawPrim(&quad); pcInterpolationObject(NULL, NULL);
    SetPolyF4(&projectedHud); setRGB0(&projectedHud, 200, 248, 200);
    projectedHud.x0 = projectedHud.x2 = 100; projectedHud.x1 = projectedHud.x3 = 120;
    projectedHud.y0 = projectedHud.y1 = 80; projectedHud.y2 = projectedHud.y3 = 100;
    s16* xy[] = {&projectedHud.x0, &projectedHud.x1, &projectedHud.x2, &projectedHud.x3};
    for (int i = 0; i < 4; i++) {
        GtePrecise p = {.valid = 1, .x = xy[i][0] + 0.375, .y = xy[i][1] + 0.375, .z = 4096 + i * 512};
        memcpy(&p.word, xy[i], sizeof(p.word)); gtePrecisionRecord(xy[i], &p);
    }
    pcTextureClass(&projectedHud, PC_TEX_HUD);
    DrawPrim(&projectedHud);
    pcInterpolationFrameEnd(); interpolationPacketsDone();
}
static int projectedHudReplay(SDL_Window* window) {
    base += 1000;
    int savedPrecision = hiresPrecision();
    CHECK(!resizeWindow(window, 1280, 896));
    CHECK(presentSetAspectText("1:1")); presentSetExpandedVertical(0);
    wideReset(); pcWideSetMode(PC_WIDE_FIELD);
    display = (GpuDisplay){0, 0, 320, 224, 0, 1, 0, 0, 0, 0, 0};
    hiresSetUpscale(4); hiresSetPrecision(1); hiresSetColor(HIRES_COLOR_DITHER_1X);
    CHECK(gvkHiConfigure(4, HIRES_COLOR_DITHER_1X) == 4);
    interpolationSetEnabled(0); projectedHudDraw(40); CHECK(shot(90));
    size_t bytes = (size_t)pictureW * pictureH * 3;
    uint8_t* endpoint = malloc(bytes); CHECK(endpoint); memcpy(endpoint, picture, bytes);
    interpolationSetEnabled(1); projectedHudDraw(40); projectedHudDraw(40); CHECK(shot(100));
    projectedHudDraw(50);
    int before, after; double alpha;
    const InterpFrame* f = interpolationPicture(&display, base + 102, &before, &after, &alpha);
    CHECK(f && f->drawCount == 2 && !f->draws[1].object && f->draws[1].count == 6);
    const int corners[] = {0, 1, 2, 1, 2, 3};
    for (int i = 0; i < 6; i++) {
        const HiVertex* v = &f->vertices[f->draws[1].first + i];
        int corner = corners[i];
        CHECK(v->x == (corner & 1 ? 120 : 100) + 0.375f);
        CHECK(v->y == (corner & 2 ? 100 : 80) + 0.375f);
        CHECK(v->w == (4096 + corner * 512) / 4096.0f);
    }
    /* Force native canvas growth while both immutable endpoints are live.
     * Its pressure path must preserve the cold snapshots, their anchors
     * and the following intermediate samples. */
    const char* pressure = getenv("XG_TEST_HI_WIDE_GROW_FAIL");
    char* savedPressure = pressure ? SDL_strdup(pressure) : NULL;
    testSetEnv("XG_TEST_HI_WIDE_GROW_FAIL","1");
    gvkWideTouch(2047);
    testSetEnv("XG_TEST_HI_WIDE_GROW_FAIL",savedPressure);
    SDL_free(savedPressure);
    CHECK(shot(102));
    CHECK(!memcmp(endpoint,picture,bytes));
    for (unsigned vb = 102; vb <= 104; vb++) {
        CHECK(shot(vb));
        for (int y = 76 * 4; y < 104 * 4; y++)
            CHECK(!memcmp(picture + (y * pictureW + 96 * 4) * 3,
                          endpoint + (y * pictureW + 96 * 4) * 3, 28 * 4 * 3));
    }
    free(endpoint); interpolationSetEnabled(0); hiresSetPrecision(savedPrecision);
    return 0;
}
static int expandedModes(SDL_Window* window) {
    CHECK(!resizeWindow(window, 960, 720));
    static const char* aspects[] = {"4:3tv", "16:9", "21:9", "32:9"};
    static uint16_t native[VRAM_W * VRAM_H], again[VRAM_W * VRAM_H];
    static uint16_t wideBefore[WIDE_FRAME_W * 256], wideAfter[WIDE_FRAME_W * 256];
    uint8_t* endpoint = malloc((size_t)pictureW * pictureH * 3); CHECK(endpoint);
    for (int aspect = 0; aspect < 4; aspect++) for (int vertical = 0; vertical < 2; vertical++)
        for (int n = 1; n <= 4; n *= 2) for (int lines = 0; lines < 2; lines++) {
            base += 100; int height = lines ? 224 : 216;
            wideReset(); pcWideSetMode(PC_WIDE_FIELD);
            CHECK(presentSetAspectText(aspects[aspect])); presentSetExpandedVertical(vertical);
            display = (GpuDisplay){0, 0, 320, height, 0, 1, 0, 10, 8, 1, 240};
            wideVBlank(&display); hiresSetUpscale(n);
            int color = lines ? HIRES_COLOR_DITHER_HI : HIRES_COLOR_DITHER_1X;
            hiresSetColor(color);
            CHECK(gvkHiConfigure(n == 1 ? 0 : n, color) == (n == 1 ? 0 : n));
            interpolationSetEnabled(1);
            expandedDraw(0, height); expandedDraw(0, height); expandedDraw(0, height);
            wideVBlank(&display); CHECK(shot(100));
            memcpy(endpoint, picture, (size_t)pictureW * pictureH * 3);
            display.y = lines ? 256 : 216; expandedDraw(10, height);
            wideVBlank(&display); CHECK(shot(102));
            CHECK(!memcmp(endpoint, picture, (size_t)pictureW * pictureH * 3));
            gvkReadback((VRect){0, 0, VRAM_W - 1, VRAM_H - 1}, native, VRAM_W);
            WideView view = *wideCurrentView();
            if (view.wide) gvkReadWide(wideBefore, view.x, view.y, view.w, view.h);
            uint32_t hiBefore = 0, hiAfter = 0;
            int hx = (view.wide ? view.x + view.m + 67 : 67) * n + n / 2;
            int hy = (view.wide ? view.y + view.top + 50 : display.y + 50) * n + n / 2;
            if (n > 1) CHECK(gvkHiRead(view.wide, &hiBefore, hx, hy, 1, 1));
            CHECK(shot(103));
            int before, after; double alpha;
            const InterpFrame* f = interpolationPicture(&display, base + 103, &before, &after, &alpha);
            CHECK(f && alpha == 0.5 && f->viewW == 320 + 2 * view.m);
            CHECK(expandedColour(47, 50, 0) > 240 && expandedColour(42, 50, 0) == 0 && expandedColour(67, 50, 0) == 0);
            CHECK(expandedColour(108, 80, 0) > 240 && expandedColour(102, 80, 0) == 0);
            CHECK(expandedColour(132, 80, 0) > 240);
            for (int y = 100; y < 104; y++) for (int c = 0; c < 3; c++)
                CHECK(expandedColour(160, y, c) == expandedColourFrom(endpoint, 160, y, c));
            CHECK(expandedColour(302 + view.m, 35, 2) > 240); /* right-anchored HUD */
            if (aspect || vertical) {
                CHECK(expandedColour(190, 50, 1) > 240); /* wide-only centre piece */
                CHECK(expandedColour(225, 50, 0) == 0); /* native-only piece absent */
                CHECK((native[(display.y + 50) * VRAM_W + 190] >> 5 & 31) < 31);
                CHECK(f->wide && f->originX == view.m && f->originY == view.top);
            }
            if (aspect) {
                CHECK(expandedColour(-23, 50, 0) > 240 && expandedColour(337, 50, 0) > 240);
                CHECK(expandedColour(-view.m + 8, 100, 1) > 0 && expandedColour(320 + view.m - 8, 100, 2) > 0);
            }
            if (vertical) {
                CHECK(f->viewH == 240);
                CHECK(expandedColour(77, -5, 0) > 240 && expandedColour(77, height + 5, 0) > 240);
                CHECK(expandedColour(50, -9, 2) > 20); /* fade extends above the native frame */
            }
            gvkReadback((VRect){0, 0, VRAM_W - 1, VRAM_H - 1}, again, VRAM_W);
            CHECK(!memcmp(native, again, sizeof(native)));
            if (n > 1) CHECK(gvkHiRead(view.wide, &hiAfter, hx, hy, 1, 1) && hiBefore == hiAfter);
            if (view.wide) {
                gvkReadWide(wideAfter, view.x, view.y, view.w, view.h);
                CHECK(!memcmp(wideBefore, wideAfter, (size_t)view.w * view.h * 2));
            }
            if (aspect == 1 && vertical && n == 2 && lines) {
                CHECK(!resizeWindow(window, 640, 480));
                CHECK(shot(103)); CHECK(expandedColour(47, 50, 0) > 240 && expandedColour(42, 50, 0) == 0);
            }
            CHECK(shot(104)); CHECK(expandedColour(52, 50, 0) > 240 && expandedColour(47, 50, 0) == 0);
            CHECK(!resizeWindow(window, 960, 720));
            if (lines) CHECK(presentSetAspectText(aspect ? "4:3tv" : "16:9"));
            else presentSetExpandedVertical(!vertical);
            CHECK(!interpolationPicture(&display, base + 105, &before, &after, &alpha));
            interpolationSetEnabled(0);
        }
    free(endpoint); return 0;
}
/* Allocation failure after each partial resource group must be recoverable.
 * Native and enhanced pixels remain intact, and retrying is bounded. */
static int allocationPressure(SDL_Window* window) {
    static uint16_t before[VRAM_W*VRAM_H], after[VRAM_W*VRAM_H];
    uint32_t hiBefore, hiAfter;
    hiresSetUpscale(2); CHECK(gvkHiConfigure(2, HIRES_COLOR_DITHER_1X)==2);
    gpuReadVram(before); CHECK(gvkHiRead(0,&hiBefore,8,8,1,1));
    for (int stage=1;stage<=3;stage++) {
        char value[8]; snprintf(value,sizeof(value),"%d",stage);
        testSetEnv("XG_TEST_INTERPOLATION_ALLOC_FAIL",value);
        interpolationReset();
        InterpFrame f={0}; f.viewW=301+stage; f.viewH=223; f.valid=1;
        gvkInterpolationCapture(0,&f);
        for(int i=0;i<60;i++) CHECK(!gvkInterpolationReady());
        CHECK(gvkInterpolationReady());
        testSetEnv("XG_TEST_INTERPOLATION_ALLOC_FAIL","0");
        gvkInterpolationCapture(0,&f);
        gpuReadVram(after); CHECK(!memcmp(before,after,sizeof(before)));
        CHECK(gvkHiRead(0,&hiAfter,8,8,1,1)&&hiBefore==hiAfter);
    }
    interpolationReset(); display.y=0; flip=0;
    wideReset(); presentSetAspectText("1:1"); presentSetExpandedVertical(0);
    CHECK(!resizeWindow(window,320,224));
    interpolationSetEnabled(1); draw(40); interpolationSetEnabled(0);
    return run();
}

/* A captured move larger than the replay scratch image (a wave reaching
 * below every copy) grows only that image, whose contents never outlive one
 * replay: the captured endpoints and the current pair survive. */
static int scratchGrowth(void) {
    base += 1000;
    hiresSetUpscale(2); CHECK(gvkHiConfigure(0, HIRES_COLOR_DITHER_1X) == 0);
    CHECK(gvkHiConfigure(2, HIRES_COLOR_DITHER_1X) == 2);
    interpolationSetEnabled(1);
    DRAWENV env; SetDefDrawEnv(&env, 0, display.y, 320, 224); PutDrawEnv(&env);
    draw(40); draw(40); CHECK(shot(100)); draw(50); CHECK(shot(102));
    int before, after; double alpha;
    const InterpFrame* pair = interpolationPicture(&display, base + 102, &before, &after, &alpha);
    CHECK(pair);
    /* The latest endpoint again, as captured, plus a whole-VRAM move: at 2x
     * far beyond the 640x512 scratch these frames needed so far. */
    InterpOp move = {.kind = INTERP_OP_COPY, .w = VRAM_W, .h = VRAM_H};
    InterpFrame f = *pair; f.ops = &move; f.opCount = 1;
    gvkInterpolationCapture(after, &f);
    CHECK(shot(103)); CHECK(red(45) && !red(44) && !red(65));
    CHECK(interpolationPicture(&display, base + 103, &before, &after, &alpha) && alpha == .5);
    interpolationSetEnabled(0);
    return 0;
}

int main(int argc, char** argv) {
    SDL_setenv_unsafe("XG_SPEEDUP", "1", 1);
    if (argc != 2) return 1;
    snprintf(directory, sizeof(directory), "%s", argv[1]);
    testSetEnv("XG_SETTINGS", "none"); testSetEnv("XG_INTERPOLATION", "0"); testSetEnv("XG_DUMP_PRESENT", directory);
    testSetEnv("XG_DUMP_RANGE", "0-40000"); testSetEnv("XG_ASPECT", "1:1");
    testSetEnv("XG_SCALE", "fit"); testSetEnv("XG_PRESENT_FILTER", "nearest");
    CHECK(SDL_Init(SDL_INIT_VIDEO));
    SDL_Window* w = SDL_CreateWindow("interpolationgputest", 320, 224, SDL_WINDOW_HIDDEN);
    CHECK(w && gpuInit(w)); gpuVBlank();
    int failed = run();
    if (!failed) { display.y = 256; failed = run(); }
    if (!failed) {
        hiresSetUpscale(2); hiresSetPrecision(0); hiresSetColor(HIRES_COLOR_DITHER_1X);
        CHECK(gvkHiConfigure(2, HIRES_COLOR_DITHER_1X) == 2);
        display.y = 0; failed = run();
    }
    if (!failed) { flip = 1; failed = run(); }
    if (!failed) failed = resources();
    if (!failed) failed = dirtyCacheFootprint();
    if (!failed) failed = nativeWordSamples();
    if (!failed) failed = windows();
    if (!failed) failed = identities();
    if (!failed) failed = placements();
    if (!failed) failed = filters();
    if (!failed) failed = replacements();
    if (!failed) failed = presets();
    if (!failed) failed = captions();
    if (!failed) failed = textureAliases();
    if (!failed) failed = encodingPressure();
    if (!failed) failed = partialQuads();
    if (!failed) failed = expandedModes(w);
    if (!failed) failed = projectedHudReplay(w);
    if (!failed) failed = allocationPressure(w);
    if (!failed) failed = scratchGrowth();
    free(picture); gpuShutdown(); SDL_DestroyWindow(w); SDL_Quit();
    if (!failed) printf("Interpolation GPU: %d checks passed\n", checks);
    return failed;
}
