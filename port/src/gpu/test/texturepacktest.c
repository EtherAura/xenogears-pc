/* File validation, live CLUTs and bounded cache lifetime without Vulkan. */
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include "test/env.h"
#include "pack_fixture.h"
#include "system/settings.h"
static int checks;
#define CHECK_DETAIL SDL_GetError()
#include "test/check.h"
/* gp0.c's texture cache hook (vram.c): no GP0 here. */
void gp0VramChanging(VRect r) { (void)r; }
/* No GPU-side draw is queued by this CPU texture fixture. */
void gvkUpload(VRect r,const uint16_t* src,int pitch) { (void)r;(void)src;(void)pitch; }
void gvkReadback(VRect r, uint16_t* dst, int pitch) {
    for (int y = r.y0; y <= r.y1; y++)
        memcpy(dst+(y-r.y0)*pitch,&vramShadow[y][r.x0],(size_t)(r.x1-r.x0+1)*2);
}
void gpuLog(const char* fmt, ...) {
    va_list args; va_start(args,fmt); vfprintf(stderr,fmt,args); va_end(args); fputc('\n',stderr);
}
static int rawFile(const char* path, const void* p, size_t n) {
    SDL_IOStream* io=SDL_IOFromFile(path,"wb"); if (!io) return 0;
    int ok=SDL_WriteIO(io,p,n)==n; return SDL_CloseIO(io) && ok;
}
/* wide.c's ordering tables drawn (the pose cache keeps the last two tables' poses): none here, the test's own count */
static unsigned testSerial;
unsigned widePoseSerial(void) { return testSerial; }
/* hw.c's vblank count (a room's first frames: the page waits); the test's own */
static uint32_t testVblank;
uint32_t hwVSyncCount(void) { return testVblank; }
/* options.c's field id (the room whose pages texturePackSample asks for); the test's own */
static int testField=-1;
int gpuTextureField(void) { return testField; }
/* A sprite pack pose: <root>/<version>/poses/<key>.pose and its index map (every index `value`). */
static int poseFiles(const char* root, uint64_t key, int w, int h, int scale, int value) {
    char path[3400], text[160];
    snprintf(path,sizeof(path),"%s/%s/poses/%016" PRIx64 ".pose",root,texturePackVersion(),key);
    int n=snprintf(text,sizeof(text),"size %d %d\nscale %d\nquad -1 -2 %d -2 -1 %d\n",w,h,scale,w-1,h-2);
    if (!rawFile(path,text,(size_t)n)) return 0;
    size_t count=(size_t)w*h*scale*scale; uint8_t* px=malloc(count*4); if (!px) return 0;
    for (size_t i=0;i<count;++i) { memset(px+i*4,value,3); px[i*4+3]=255; }
    SDL_Surface* sf=SDL_CreateSurfaceFrom(w*scale,h*scale,SDL_PIXELFORMAT_RGBA32,px,w*scale*4);
    snprintf(path,sizeof(path),"%s/%s/poses/%016" PRIx64 ".indices.png",root,texturePackVersion(),key);
    int ok=sf && SDL_SavePNG(sf,path); SDL_DestroySurface(sf); free(px); return ok;
}
static void waitPreloads(void) { for (int i=0;i<4000 && texturePackPreloadPending();++i) SDL_Delay(5); }
int main(int argc, char** argv) {
    CHECK(argc>=3); CHECK(testSetEnv("XG_SETTINGS",argv[2])==0);
    if (argc<=3) CHECK(testSetEnv("XG_TEXTURE_PACK","none")==0); /* the first pass: not the default library beside the executable */
    CHECK(SDL_Init(0));
    /* no room page waits but where the room pages' checks ask for them: big replacements load in the
     * background, the original meanwhile, as these checks expect */
    settingsSet("texture_pack_room_wait_ms","0");
    if (argc>3) {
        CHECK(!strcmp(texturePackRoot(),argv[1]));
        CHECK(!strcmp(textureDumpRoot(),argv[1]));
        CHECK(texturePackEnabled()); SDL_Quit(); puts("texture pack settings reloaded"); return 0;
    }
    CHECK(!texturePackEnabled()); CHECK(packDir(argv[1]));
    texturePackSetRoot(argv[1]); CHECK(texturePackEnabled());
    char path[3400]; snprintf(path,sizeof(path),"%s/%s/pack.ini",argv[1],texturePackVersion());
    const char meta[]="# user pack\nname = Procedural fixture\nauthor = GPU tests\n";
    CHECK(rawFile(path,meta,sizeof(meta)-1)); texturePackReload();
    TexturePackStats stats; texturePackStats(&stats);
    CHECK(!strcmp(stats.name,"Procedural fixture") && !strcmp(stats.author,"GPU tests"));
    DecodedTexture d={0}; d.request=(TextureRequest){0,0,0,7,8,4,1,0,0};
    d.texelKey=123; d.paletteKey=456; d.paletteSize=16;
    d.palette[0]=31; d.palette[1]=0; d.palette[2]=0x8000; d.palette[3]=0x83e0;
    TextureSample s;
    CHECK(!texturePackSample(&d,d.request,0,&s)); texturePackStats(&stats); CHECK(stats.misses==2);
    CHECK(!texturePackSample(&d,d.request,0,&s)); texturePackStats(&stats); CHECK(stats.misses==2);
    uint8_t pixels[64];
    for (int i=0;i<16;++i) { memset(pixels+i*4,i%8/2,3); pixels[i*4+3]=255; }
    CHECK(packPng(argv[1],&d,1,8,2,pixels));
    CHECK(!texturePackSample(&d,d.request,0,&s)); /* negative lookup waits for reload */
    texturePackReload(); CHECK(texturePackSample(&d,d.request,0,&s)); CHECK(s.scale==2 && s.u==7 && s.v==8);
    CHECK(s.rgba[0]==255 && s.rgba[3]==255 && s.rgba[11]==0 && s.rgba[19]==128);
    CHECK(s.rgba[18]==0 && s.rgba[25]==255 && s.rgba[27]==128);
    uint64_t before=s.revision; const uint8_t* cached=s.rgba;
    CHECK(texturePackSample(&d,d.request,0,&s) && s.revision==before && s.rgba==cached);
    d.palette[0]=31<<10; d.palette[3]=0; ++d.paletteKey;
    CHECK(texturePackSample(&d,d.request,1,&s)); CHECK(s.revision!=before && s.rgba[2]==255 && s.rgba[27]==0);
    texturePackStats(&stats); CHECK(stats.loads==1 && stats.recolours==2);
    /* RGBA exact-palette file wins; alpha is PS1 clear/STP/opaque. */
    for (int i=0;i<16;++i) { pixels[i*4]=255; pixels[i*4+1]=pixels[i*4+2]=0; pixels[i*4+3]=(uint8_t)(i*16); }
    CHECK(packPng(argv[1],&d,0,8,2,pixels)); texturePackReload();
    CHECK(texturePackSample(&d,d.request,0,&s));
    CHECK(s.rgba[15]==0 && s.rgba[19]==128 && s.rgba[47]==128 && s.rgba[51]==255);
    before=s.revision; pixels[0]=0; pixels[1]=255;
    CHECK(packPng(argv[1],&d,0,8,2,pixels));
    CHECK(texturePackSample(&d,d.request,0,&s) && s.revision==before && s.rgba[0]==255);
    texturePackReload(); CHECK(texturePackSample(&d,d.request,0,&s) && s.revision!=before && s.rgba[1]==255);
    TextureRequest outside=d.request; --outside.u; CHECK(!texturePackSample(&d,outside,0,&s));
    /* Partial/scaled/aspect-changing PNGs cannot be used. */
    packPath(path,sizeof(path),argv[1],&d,0); CHECK(SDL_RemovePath(path));
    packPath(path,sizeof(path),argv[1],&d,1); CHECK(SDL_RemovePath(path));
    uint8_t small[9*2*4]; memset(small,255,sizeof(small));
    CHECK(packPng(argv[1],&d,0,7,2,small)); texturePackReload(); CHECK(!texturePackSample(&d,d.request,0,&s));
    CHECK(packPng(argv[1],&d,0,9,2,small)); texturePackReload(); CHECK(!texturePackSample(&d,d.request,0,&s));
    packPath(path,sizeof(path),argv[1],&d,0);
    uint8_t huge[33]={137,80,78,71,13,10,26,10,0,0,0,13,73,72,68,82,0,1,0,0,0,1,0,0};
    CHECK(rawFile(path,huge,sizeof(huge))); texturePackReload(); CHECK(!texturePackSample(&d,d.request,0,&s));
    huge[17]=0; huge[19]=8; huge[21]=0; huge[23]=2;
    CHECK(rawFile(path,huge,sizeof(huge))); texturePackReload(); CHECK(!texturePackSample(&d,d.request,0,&s));
    /* Valid dimensions, invalid PNG stream, and excessive file size. */
    SDL_IOStream* io=SDL_IOFromFile(path,"wb"); CHECK(io);
    CHECK(SDL_SeekIO(io,16*1024*1024,SDL_IO_SEEK_SET)>=0 && SDL_WriteIO(io,huge,1)==1 && SDL_CloseIO(io));
    texturePackReload(); CHECK(!texturePackSample(&d,d.request,0,&s));
    CHECK(SDL_RemovePath(path));
    for (int invalid=0;invalid<3;++invalid) {
        for (int i=0;i<16;++i) { memset(pixels+i*4,1,3); pixels[i*4+3]=255; }
        if (invalid==0) pixels[0]=pixels[1]=pixels[2]=16;
        if (invalid==1) pixels[1]=2;
        if (invalid==2) pixels[3]=128;
        CHECK(packPng(argv[1],&d,1,8,2,pixels)); texturePackReload(); CHECK(!texturePackSample(&d,d.request,0,&s));
        texturePackStats(&stats); CHECK(stats.invalid==1);
    }
    /* Alternate short index filename and a 1x edge variant. */
    memset(pixels,1,sizeof(pixels)); for (int i=0;i<16;++i) pixels[i*4+3]=255;
    CHECK(packPng(argv[1],&d,1,4,1,pixels));
    char shortPath[3400]; snprintf(shortPath,sizeof(shortPath),"%s/%s/%016" PRIx64 ".png",argv[1],texturePackVersion(),d.texelKey);
    packPath(path,sizeof(path),argv[1],&d,1); CHECK(SDL_RenamePath(path,shortPath));
    texturePackReload(); CHECK(texturePackSample(&d,d.request,2,&s) && s.scale==2);
    /* Stable regions: with no image for the region, a patch still finds the
     * image a pack made before stable regions has under the patch's own
     * key; pack.ini rect_keys=no skips that second look-up. */
    {
        DecodedTexture region={0};
        region.request=(TextureRequest){0,0,0,0,0,64,64,10,20}; region.texelKey=777; region.paletteSize=16;
        TextureRequest patch={0,0,0,4,4,8,8,0,0};
        const DecodedTexture* own=textureCacheGet(patch); CHECK(own);
        DecodedTexture ownKey=*own;
        uint8_t grey[16*16*4];
        for (int i=0;i<16*16;++i) { memset(grey+i*4,1,3); grey[i*4+3]=255; }
        CHECK(packPng(argv[1],&ownKey,1,16,16,grey)); texturePackReload();
        CHECK(texturePackSample(&region,patch,0,&s) && s.scale==2 && s.u==4 && s.v==4 && s.w==8 && s.h==8);
        const char noRect[]="name = Procedural fixture\nrect_keys = no\n";
        char ini[3400]; snprintf(ini,sizeof(ini),"%s/%s/pack.ini",argv[1],texturePackVersion());
        CHECK(rawFile(ini,noRect,sizeof(noRect)-1)); texturePackReload();
        CHECK(!texturePackSample(&region,patch,0,&s));
        region.request.stableFrom=region.request.stableTo=0; /* an ordinary texture: no second look-up */
        CHECK(rawFile(ini,meta,sizeof(meta)-1)); texturePackReload();
        CHECK(!texturePackSample(&region,patch,0,&s));
    }
    /* A big replacement goes out as the sampled patch in its live palette;
     * the crop is kept until the palette or the patch changes. */
    {
        DecodedTexture page={0};
        page.request=(TextureRequest){0,0,0,0,0,256,256,1,2}; page.texelKey=4242; page.paletteSize=16;
        page.paletteKey=1; page.palette[3]=31;
        uint8_t* idx=malloc(512*512*4); CHECK(idx);
        for (int i=0;i<512*512;++i) { memset(idx+i*4,(i%512)/128,3); idx[i*4+3]=255; }
        CHECK(packPng(argv[1],&page,1,512,512,idx)); free(idx); texturePackReload();
        TextureRequest patch={0,0,0,200,10,8,4,0,0};
        CHECK(texturePackSample(&page,patch,0,&s) && s.u==200 && s.v==10 && s.w==8 && s.h==4 && s.scale==2);
        CHECK(s.rgba[0]==255 && s.rgba[3]==255); /* index 3 (x 400..) in palette word 31: red */
        uint64_t crop=s.revision;
        CHECK(texturePackSample(&page,patch,0,&s) && s.revision==crop);
        page.palette[3]=31<<10; page.paletteKey=2;
        CHECK(texturePackSample(&page,patch,0,&s) && s.revision!=crop && s.rgba[2]==255 && s.rgba[0]==0);
        CHECK(texturePackSample(&page,page.request,0,&s) && s.w==256 && s.u==0); /* the whole image: as before */
    }
    /* A room's pages (xg-hbc.27): rooms.txt names a field's big pages; the field load (pcSpritePackPreload
     * (field, -1)) queues them for the loader threads; a page still loading when the room's first frames
     * draw it is waited for (or decoded there when no loader has started it), within the room's bound, so the
     * draw never shows the original; a page no room lists, first drawn in those frames, is decoded there;
     * later in the room a page first drawn shows the original while it loads (counted once). Bound 0: the
     * original while loading, as before; texture_pack_sync=1: every page decoded as it is asked for. */
    {
        DecodedTexture pg[4];
        uint8_t* noise=malloc(2048u*2048u*4); CHECK(noise); uint32_t seed=7;
        for (int k=0;k<4;++k) {
            memset(&pg[k],0,sizeof(pg[k])); pg[k].request=(TextureRequest){0,0,0,0,0,256,256,0,0};
            pg[k].texelKey=UINT64_C(0x7a01)+(uint64_t)k; pg[k].paletteKey=1; pg[k].paletteSize=16; pg[k].palette[1]=31;
            for (size_t i=0;i<2048u*2048u;++i) { seed=seed*1664525u+1013904223u; memset(noise+i*4,(int)(seed>>28),3); noise[i*4+3]=255; }
            CHECK(packPng(argv[1],&pg[k],1,2048,2048,noise));   /* 8x, about 2 MB: above ASYNC_FROM */
        }
        free(noise);
        char rooms[3400], ini[3400];
        snprintf(rooms,sizeof(rooms),"%s/%s/rooms.txt",argv[1],texturePackVersion());
        const char list[]="7 0000000000007a01 256 256 16\n7 0000000000007a02 256 256 16\n9 0000000000007a04 256 256 16\nbad\n";
        CHECK(rawFile(rooms,list,sizeof(list)-1));
        snprintf(ini,sizeof(ini),"%s/%s/pack.ini",argv[1],texturePackVersion());
        const char big[]="name = Room fixture\ncache_mb = 256\n";
        CHECK(rawFile(ini,big,sizeof(big)-1));
        settingsSet("texture_pack_room_wait_ms","1000"); settingsSet("sprite_packs","none"); texturePackReload();
        testVblank=1000; testField=7;
        pcSpritePackPreload(7,-1); texturePackStats(&stats); CHECK(stats.pagePreloads==2 && stats.pageLoads==0);
        CHECK(texturePackSample(&pg[0],pg[0].request,0,&s) && s.scale==8);   /* the room's first frame */
        testVblank+=2;
        CHECK(texturePackSample(&pg[1],pg[1].request,0,&s) && s.scale==8);
        texturePackStats(&stats); CHECK(stats.pageLoads==2 && stats.pageWaits>=1 && stats.pageNative==0 && stats.roomsOverBudget==0);
        uint64_t sync=stats.pageSync;
        CHECK(texturePackSample(&pg[2],pg[2].request,0,&s));   /* no room lists it: decoded there */
        texturePackStats(&stats); CHECK(stats.pageSync==sync+1 && stats.pageNative==0 && stats.pageWaitUs>0);
        testVblank+=60;   /* later in the room */
        CHECK(!texturePackSample(&pg[3],pg[3].request,0,&s)); CHECK(!texturePackSample(&pg[3],pg[3].request,0,&s));
        texturePackStats(&stats); CHECK(stats.pageNative==1 && stats.pageSync==sync+1);
        for (int i=0;i<4000 && !texturePackSample(&pg[3],pg[3].request,0,&s);++i) SDL_Delay(5);
        texturePackStats(&stats); CHECK(stats.pageLoads==4 && stats.pageLoader>=1 && stats.pageNative==1);
        /* bound 0: the original while loading, as before */
        settingsSet("texture_pack_room_wait_ms","0"); texturePackReload(); testField=9;
        pcSpritePackPreload(9,-1); texturePackStats(&stats); CHECK(stats.pagePreloads==1);
        CHECK(!texturePackSample(&pg[3],pg[3].request,0,&s));
        texturePackStats(&stats); CHECK(stats.pageWaits==0 && stats.pageNative==1);
        waitPreloads(); CHECK(texturePackSample(&pg[3],pg[3].request,0,&s));
        /* texture_pack_sync=1: the room's pages decoded at the field load */
        settingsSet("texture_pack_sync","1"); texturePackReload(); testField=7;
        pcSpritePackPreload(7,-1); texturePackStats(&stats); CHECK(stats.pageSync==2 && stats.pagePreloads==0 && !texturePackPreloadPending());
        CHECK(texturePackSample(&pg[0],pg[0].request,0,&s)); texturePackStats(&stats); CHECK(stats.pageWaits==0 && stats.pageNative==0);
        settingsSet("texture_pack_sync","0");
        for (int k=0;k<4;++k) { packPath(path,sizeof(path),argv[1],&pg[k],1); CHECK(SDL_RemovePath(path)); }
        CHECK(SDL_RemovePath(rooms)); CHECK(rawFile(ini,meta,sizeof(meta)-1)); testField=-1; texturePackReload();
    }
    /* The sprite pack's room preload (xg-hbc.23): parts.tsv and npcs.tsv beside
     * its pack.ini name a field NPC slot's cels; pcSpritePackPreload queues them
     * for the loader thread and the draw finds them loaded (no decode on the game
     * thread); a cel drawn before its decode finished is decoded there and then. */
    {
        char sprites[3400], tsv[3400]; snprintf(sprites,sizeof(sprites),"%s/sprites",argv[1]);
        CHECK(packDir(sprites));
        DecodedTexture cel={0}; cel.request=(TextureRequest){0,0,0,16,8,4,6,0,0};
        cel.texelKey=UINT64_C(0xabcdef0123456789); cel.paletteSize=16; cel.palette[1]=31;
        uint8_t grey[8*12*4];
        for (int i=0;i<8*12;++i) { memset(grey+i*4,1,3); grey[i*4+3]=255; }
        CHECK(packPng(sprites,&cel,1,8,12,grey));
        DecodedTexture other=cel; other.texelKey=UINT64_C(0x1111); CHECK(packPng(sprites,&other,1,8,12,grey));
        const char parts[]="# part\tkey\tw\th\tdepth\n\nvillager-g2\tabcdef0123456789\t4\t6\t4\nbad line\nvillager-g3\t0000000000001111\t4\t6\t4\n";
        snprintf(tsv,sizeof(tsv),"%s/parts.tsv",sprites); CHECK(rawFile(tsv,parts,sizeof(parts)-1));
        const char npcs[]="# disc\tfield\tnpc\tpart\n1\t7\t2\tvillager-g2\n1\t7\t3\tvillager-g3\n2\t7\t4\tnobody\n";
        snprintf(tsv,sizeof(tsv),"%s/npcs.tsv",sprites); CHECK(rawFile(tsv,npcs,sizeof(npcs)-1));
        settingsSet("sprite_packs",sprites); texturePackReload(); CHECK(textureSpriteCelEnabled());
        pcSpritePackPreload(7,2); texturePackStats(&stats); CHECK(stats.celPreloads==1 && stats.celSkipped==0);
        pcSpritePackPreload(7,2); pcSpritePackPreload(7,9); pcSpritePackPreload(7,4);   /* queued already; no such slot; a part with no cels */
        texturePackStats(&stats); CHECK(stats.celPreloads==1);
        for (int i=0;i<2000 && texturePackPreloadPending();++i) SDL_Delay(5);
        CHECK(!texturePackPreloadPending());
        texturePackStats(&stats); CHECK(stats.loads==1 && stats.gameThreadLoads==0 && stats.celSync==0);
        CHECK(textureSpriteCelSample(&cel,cel.request,0,&s) && s.scale==2 && s.w==4 && s.h==6 && s.rgba[0]==255);
        texturePackStats(&stats); CHECK(stats.loads==1 && stats.gameThreadLoads==0 && stats.celLate==0);
        /* the other slot's cel was not queued: its first draw decodes it on the game thread, as before */
        CHECK(textureSpriteCelSample(&other,other.request,0,&s));
        texturePackStats(&stats); CHECK(stats.loads==2 && stats.gameThreadLoads==1 && stats.celSync==1);
        /* drawn before its decode finished: decoded there and then, the loader's result dropped
         * (pages go first and the two loaders take jobs in order: two big pages queued first keep the cel waiting) */
        DecodedTexture big={0}; big.request=(TextureRequest){0,0,0,0,0,256,256,0,0}; big.texelKey=9999; big.paletteKey=1;
        DecodedTexture big2=big; big2.texelKey=9998;
        {
            uint8_t* noise=malloc(1536*1536*4); CHECK(noise); uint32_t seed=1;
            for (size_t i=0;i<1536u*1536u*4;++i) { seed=seed*1664525u+1013904223u; noise[i]=(i&3)==3?255:(uint8_t)(seed>>24); }
            CHECK(packPng(argv[1],&big,0,1536,1536,noise)); CHECK(packPng(argv[1],&big2,0,1536,1536,noise)); free(noise);
        }
        texturePackReload();
        CHECK(!texturePackSample(&big,big.request,0,&s));   /* above ASYNC_FROM: on a loader thread, the original meanwhile */
        CHECK(!texturePackSample(&big2,big2.request,0,&s));
        pcSpritePackPreload(7,3);
        CHECK(textureSpriteCelSample(&other,other.request,0,&s));
        texturePackStats(&stats); CHECK(stats.celPreloads==1 && stats.celLate==1 && stats.celSync==1 && stats.loads==1);
        for (int i=0;i<4000 && texturePackPreloadPending();++i) SDL_Delay(5);
        for (int i=0;i<4000 && !texturePackSample(&big,big.request,0,&s);++i) SDL_Delay(5);
        CHECK(texturePackSample(&big2,big2.request,0,&s));
        texturePackStats(&stats); CHECK(stats.loads==3 && stats.gameThreadLoads==1 && stats.entries==3 && stats.pageLoader==2 && stats.pageNative==2);
        packPath(path,sizeof(path),argv[1],&big,0); CHECK(SDL_RemovePath(path));
        packPath(path,sizeof(path),argv[1],&big2,0); CHECK(SDL_RemovePath(path));
        /* sprite_preload=0 keeps the first-draw decodes */
        settingsSet("sprite_preload","0"); texturePackReload(); pcSpritePackPreload(7,2);
        texturePackStats(&stats); CHECK(stats.celPreloads==0);
        /* npc -1: every slot of the field (sheets that arrive with the texture file, as in Lahan) */
        settingsSet("sprite_preload","1"); texturePackReload(); pcSpritePackPreload(7,-1); pcSpritePackPreload(8,-1);
        texturePackStats(&stats); CHECK(stats.celPreloads==2);
        for (int i=0;i<2000 && texturePackPreloadPending();++i) SDL_Delay(5);
        texturePackStats(&stats); CHECK(stats.loads==2 && stats.gameThreadLoads==0);
        /* the party's dolls (xg-hbc.27): no NPC slot; their parts.tsv cels (the part a poses.tsv member) are
         * queued with the party at the field load (pcSpritePackParty), and drawn with no game-thread decode */
        DecodedTexture doll=cel; doll.texelKey=UINT64_C(0x2222); CHECK(packPng(sprites,&doll,1,8,12,grey));
        const char dollParts[]="villager-g2\tabcdef0123456789\t4\t6\t4\nfei-doll\t0000000000002222\t4\t6\t4\n";
        snprintf(tsv,sizeof(tsv),"%s/parts.tsv",sprites); CHECK(rawFile(tsv,dollParts,sizeof(dollParts)-1));
        const char dollPoses[]="fei-doll\t00000000000000f9\t4\t6\t2\t0\n";
        snprintf(tsv,sizeof(tsv),"%s/poses.tsv",sprites); CHECK(rawFile(tsv,dollPoses,sizeof(dollPoses)-1));
        texturePackReload(); pcSpritePackParty(0xFF,0xFF,0xFF); pcSpritePackPreload(8,-1);
        texturePackStats(&stats); CHECK(stats.celPreloads==0);   /* no party: field 8 has no slots */
        pcSpritePackParty(0,0xFF,0xFF); pcSpritePackPreload(9,-1);
        texturePackStats(&stats); CHECK(stats.celPreloads==1);
        waitPreloads();
        CHECK(textureSpriteCelSample(&doll,doll.request,0,&s) && s.scale==2);
        texturePackStats(&stats); CHECK(stats.celSync==0 && stats.celLate==0 && stats.gameThreadLoads==0);
        snprintf(tsv,sizeof(tsv),"%s/poses.tsv",sprites); CHECK(SDL_RemovePath(tsv));
        settingsSet("sprite_packs","none"); texturePackReload();
    }
    /* The room's poses (xg-hbc.26): poses.tsv beside the sprite pack's pack.ini names each part's poses
     * (member: the party doll of a character); pcSpritePackPreload queues a field's NPC parts' poses and
     * the party's (pcSpritePackParty) for the pose loaders, and texturePoseGet finds them decoded (none on
     * the game thread); a pose drawn before its decode finished is decoded there and then; a drawn pose of
     * a part no preload asked for queues the rest of that part; the pack.ini cache_mb budget makes the
     * earlier rooms' poses make way, never one drawn in the last two ordering tables. */
    {
        char sprites[3400], file[3400]; snprintf(sprites,sizeof(sprites),"%s/pose-sprites",argv[1]);
        CHECK(packDir(sprites)); snprintf(file,sizeof(file),"%s/%s/poses",sprites,texturePackVersion()); CHECK(SDL_CreateDirectory(file));
        const uint64_t villager[2]={0xa1,0xa2}, fei[2]={0xf1,0xf2}, scene[3]={0x51,0x52,0x53};
        for (int i=0;i<2;++i) { CHECK(poseFiles(sprites,villager[i],4,6,2,3)); CHECK(poseFiles(sprites,fei[i],5,7,2,4)); }
        for (int i=0;i<3;++i) CHECK(poseFiles(sprites,scene[i],3,3,2,5));
        /* the budget's rooms: 12 poses of 4 MiB each in parts big-a (field 8) and big-b (field 9); a slow
         * part (field 10): eight of them, then a small one the loaders reach last */
        for (int i=0;i<24;++i) CHECK(poseFiles(sprites,UINT64_C(0xb000)+(uint64_t)i,256,256,8,1+i%15));
        CHECK(poseFiles(sprites,0xc1,4,4,2,6));
        char tsv[65536]; int at=snprintf(tsv,sizeof(tsv),"# part\tkey\tw\th\tscale\tmember\n");
        for (int i=0;i<2;++i) at+=snprintf(tsv+at,sizeof(tsv)-(size_t)at,"villager-g2\t%016" PRIx64 "\t4\t6\t2\t-1\nfei-doll\t%016" PRIx64 "\t5\t7\t2\t0\n",villager[i],fei[i]);
        for (int i=0;i<3;++i) at+=snprintf(tsv+at,sizeof(tsv)-(size_t)at,"scene-f2d\t%016" PRIx64 "\t3\t3\t2\t-1\n",scene[i]);
        for (int i=0;i<24;++i) at+=snprintf(tsv+at,sizeof(tsv)-(size_t)at,"big-%c\t%016" PRIx64 "\t256\t256\t8\t-1\n",i<12?'a':'b',UINT64_C(0xb000)+(uint64_t)i);
        for (int i=0;i<8;++i) at+=snprintf(tsv+at,sizeof(tsv)-(size_t)at,"slow\t%016" PRIx64 "\t256\t256\t8\t-1\n",UINT64_C(0xb000)+(uint64_t)(16+i));
        at+=snprintf(tsv+at,sizeof(tsv)-(size_t)at,"slow\t00000000000000c1\t4\t4\t2\t-1\nbad line\n");
        snprintf(file,sizeof(file),"%s/poses.tsv",sprites); CHECK(rawFile(file,tsv,(size_t)at));
        const char npcs[]="# disc\tfield\tnpc\tpart\n1\t7\t2\tvillager-g2\n1\t8\t0\tbig-a\n1\t9\t0\tbig-b\n1\t10\t0\tslow\n";
        snprintf(file,sizeof(file),"%s/npcs.tsv",sprites); CHECK(rawFile(file,npcs,sizeof(npcs)-1));
        const char ini[]="name = Pose fixture\ncache_mb = 64\n";
        snprintf(file,sizeof(file),"%s/pack.ini",sprites); CHECK(rawFile(file,ini,sizeof(ini)-1));
        settingsSet("sprite_packs",sprites); settingsSet("sprite_preload","1"); texturePackReload(); CHECK(textureSpriteCelEnabled());
        size_t bytes=0, peak=0, limit=0; texturePackPoseCache(&bytes,&peak,&limit); CHECK(limit==64u*1024u*1024u && bytes==0);
        /* the room: the villager's poses and Fei's (the party: character 0) */
        pcSpritePackParty(0,0xFF,0xFF); pcSpritePackPreload(7,-1);
        texturePackStats(&stats); CHECK(stats.posePreloads==4 && stats.poseSkipped==0);
        waitPreloads(); CHECK(!texturePackPreloadPending());
        texturePackStats(&stats); CHECK(stats.poseLoads==4 && stats.poseLoader==4 && stats.poseSync==0);
        const SpritePose* pose=texturePoseGet(villager[1]);
        CHECK(pose && pose->w==4 && pose->h==6 && pose->scale==2 && pose->indices[0]==3 && pose->indices[8*12-1]==3 && pose->revision);
        CHECK(pose->a[0]==-1.0f && pose->b[2]==4.0f);
        pose=texturePoseGet(fei[0]); CHECK(pose && pose->w==5 && pose->indices[0]==4);
        texturePackStats(&stats); CHECK(stats.poseSync==0 && stats.poseLate==0);
        texturePackPoseCache(&bytes,&peak,&limit); CHECK(bytes==2*(4*6+5*7)*4 && peak==bytes);
        /* not in the library: remembered as missing */
        CHECK(!texturePoseGet(0x1234) && !texturePoseGet(0x1234)); texturePackStats(&stats); CHECK(stats.poseLoads==4);
        /* a scene's actor (no NPC slot): its first pose decodes on the game thread, the rest of its part is queued */
        CHECK(texturePoseGet(scene[1])); texturePackStats(&stats); CHECK(stats.poseSync==1 && stats.posePartsSeen==1 && stats.posePreloads==6);
        waitPreloads(); CHECK(texturePoseGet(scene[0]) && texturePoseGet(scene[2]));
        texturePackStats(&stats); CHECK(stats.poseSync==1 && stats.poseLoader==6);
        /* the budget: room 8 fills 48 of 64 MiB; room 9 needs as much again, so room 8's poses make way,
         * least recently drawn first, except the one the last ordering table drew */
        testSerial+=2;
        pcSpritePackPreload(8,-1); waitPreloads();
        texturePackStats(&stats); CHECK(stats.posePreloads==6+12 && stats.poseLoader==6+12 && stats.poseSkipped==0 && stats.poseEvictions==0);
        testSerial+=2;
        CHECK(texturePoseGet(UINT64_C(0xb000)));   /* drawn in this table: held */
        pcSpritePackPreload(9,-1); waitPreloads();
        /* 32 MiB and the small poses over: room 7's villager and the scene's poses first (the least recently
         * drawn; Fei's stay, the party is in every room), then nine of room 8's */
        texturePackStats(&stats); CHECK(stats.posePreloads==18+12 && stats.poseSkipped==0 && stats.poseEvictions==5+9);
        texturePackPoseCache(&bytes,&peak,&limit); CHECK(bytes<=limit && peak<=limit);
        uint64_t sync=stats.poseSync;
        CHECK(texturePoseGet(UINT64_C(0xb000)) && texturePoseGet(UINT64_C(0xb000)+12) && texturePoseGet(UINT64_C(0xb000)+23) && texturePoseGet(fei[1]));
        texturePackStats(&stats); CHECK(stats.poseSync==sync);   /* kept, and room 9's loaded */
        CHECK(texturePoseGet(UINT64_C(0xb001)));                   /* made way: decoded again at its draw */
        texturePackStats(&stats); CHECK(stats.poseSync==sync+1);
        /* drawn before its decode finished: decoded there and then (the loaders, at most four, are busy with eight big poses) */
        texturePackReload(); testSerial+=2;
        pcSpritePackParty(0xFF,0xFF,0xFF); pcSpritePackPreload(10,-1);
        pose=texturePoseGet(0xc1);
        texturePackStats(&stats); CHECK(pose && pose->indices[0]==6 && stats.posePreloads==9 && stats.poseLate==1 && stats.poseSync==1);
        waitPreloads(); CHECK(texturePoseGet(0xc1)==pose);
        texturePackStats(&stats); CHECK(stats.poseLoader==8 && stats.poseLoads==9);
        /* sprite_preload=0: nothing queued, each pose decodes at its first draw */
        settingsSet("sprite_preload","0"); texturePackReload(); pcSpritePackParty(0,0xFF,0xFF); pcSpritePackPreload(7,-1);
        texturePackStats(&stats); CHECK(stats.posePreloads==0);
        CHECK(texturePoseGet(villager[0]) && texturePoseGet(scene[0]));
        texturePackStats(&stats); CHECK(stats.poseSync==2 && stats.posePreloads==0 && stats.posePartsSeen==0);
        settingsSet("sprite_preload","1"); settingsSet("sprite_packs","none"); texturePackReload();
    }
    /* Eight 4 MiB PNGs fill the budget; more evict actual pixel storage. */
    uint8_t* large=malloc(1024*1024*4); CHECK(large); memset(large,255,1024*1024*4);
    d.request=(TextureRequest){0,0,0,0,0,256,256,0,0}; d.paletteSize=0; d.paletteKey=0;
    texturePackReload();
    for (int i=0;i<10;++i) {
        d.texelKey=1000+i; CHECK(packPng(argv[1],&d,0,1024,1024,large));
        CHECK(texturePackSample(&d,d.request,0,&s) && s.scale==4);
    }
    free(large); texturePackStats(&stats); CHECK(stats.bytes<=32*1024*1024 && stats.evictions>=2);
    /* Negative entries are bounded too. */
    for (int i=0;i<TEXTURE_PACK_SLOTS+22;++i) { d.texelKey=2000+i; CHECK(!texturePackSample(&d,d.request,0,&s)); }
    texturePackStats(&stats); CHECK(stats.entries==TEXTURE_PACK_SLOTS);
    textureDumpSetRoot(argv[1]); CHECK(textureDumpEnabled());
    textureDumpShutdown(); texturePackShutdown(); textureSampleReset();
    printf("CPU texture packs: %d checks passed\n",checks); SDL_Quit(); return 0;
}
