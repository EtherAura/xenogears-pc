/* Independent library discovery, bounded lifetime and persistence. */
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include "test/env.h"
#include "pack_fixture.h"
#include "gpu/portrait_pack.h"
#include "pc_wide.h"
static int checks;
#define CHECK_DETAIL SDL_GetError()
#include "test/check.h"
void gp0VramChanging(VRect r) { (void)r; }
/* No GPU-side draw is queued by this CPU texture fixture. */
void gvkUpload(VRect r,const uint16_t* src,int pitch) { (void)r;(void)src;(void)pitch; }
void gvkReadback(VRect r,uint16_t* dst,int pitch) {
    for(int y=r.y0;y<=r.y1;++y) memcpy(dst+(y-r.y0)*pitch,&vramShadow[y][r.x0],(size_t)(r.x1-r.x0+1)*2);
}
void gpuLog(const char* fmt,...) { (void)fmt; }
static int writeFile(const char* path,const void* data,size_t bytes) {
    SDL_IOStream* io=SDL_IOFromFile(path,"wb"); if(!io) return 0;
    int ok=SDL_WriteIO(io,data,bytes)==bytes; return SDL_CloseIO(io) && ok;
}
static void put16(uint8_t* p,unsigned n) { p[0]=n; p[1]=n>>8; }
static void put32(uint8_t* p,unsigned n) { put16(p,n); put16(p+2,n>>16); }
static int image(const char* root,const char* set,uint64_t texel,uint64_t palette,int size,int color) {
    char path[3600]; snprintf(path,sizeof(path),"%s/%s/%s/%016" PRIx64 "-%016" PRIx64 ".png",root,set,texturePackVersion(),texel,palette);
    SDL_Surface* s=SDL_CreateSurface(size,size,SDL_PIXELFORMAT_RGBA32); if(!s) return 0;
    for(int y=0;y<size;++y) for(int x=0;x<size;++x) {
        uint8_t* p=(uint8_t*)s->pixels+(size_t)y*s->pitch+x*4;
        p[0]=color; p[1]=32; p[2]=64; p[3]=x<3?x*100:255;
    }
    int ok=SDL_SavePNG(s,path); SDL_DestroySurface(s); return ok;
}
static int setDir(const char* root,const char* set) {
    char path[3500]; snprintf(path,sizeof(path),"%s/%s/%s",root,set,texturePackVersion()); if(!SDL_CreateDirectory(path)) return 0;
    snprintf(path,sizeof(path),"%s/%s/portrait.ini",root,set);
    const char meta[]="name=Test artwork\nauthor=Procedural tests\n"; return writeFile(path,meta,sizeof(meta)-1);
}
static int catalog(int argc,char** argv) {
    CHECK(argc==6); CHECK(testSetEnv("XG_SETTINGS","none")==0); CHECK(SDL_Init(0));
    portraitPackSetRoot(argv[3]); CHECK(portraitPackSelect(argv[4])); CHECK(portraitPackEnabled());
    int count=0;
    for(int file=0x46;file<=0xa0;++file) {
        char path[4096]; snprintf(path,sizeof(path),"%s/original-%02x.tim",argv[2],file);
        FILE* f=fopen(path,"rb"); if(!f) continue;
        uint8_t tim[4640]; CHECK(fread(tim,1,sizeof(tim),f)==sizeof(tim)); CHECK(fgetc(f)==EOF); fclose(f);
        PortraitImage image; CHECK(portraitPackTim(tim,sizeof(tim),&image));
        CHECK((image.scale==2 || image.scale==4) && image.size==image.scale*64);
        for(size_t i=3;i<(size_t)image.size*image.size*4;i+=4) CHECK(image.rgba[i]==0 || image.rgba[i]==128 || image.rgba[i]==255);
        ++count;
    }
    CHECK(count==atoi(argv[5])); PortraitPackStats stats; portraitPackStats(&stats);
    CHECK(!stats.misses && !stats.invalid && stats.bytes<=32u*1024u*1024u);
    portraitPackShutdown(); texturePackShutdown(); SDL_Quit();
    printf("portrait catalog: %d source variants loaded, %d checks\n",count,checks); return 0;
}
int main(int argc,char** argv) {
    if(argc>1 && !strcmp(argv[1],"--catalog")) return catalog(argc,argv);
    CHECK(argc>=3); CHECK(testSetEnv("XG_SETTINGS",argv[2])==0); CHECK(SDL_Init(0));
    if(argc>3) {
        CHECK(!strcmp(portraitPackRoot(),argv[1])); CHECK(!strcmp(portraitPackSelected(),"amber"));
        CHECK(portraitPackEnabled()); CHECK(!texturePackEnabled());
        CHECK(!strcmp(portraitBattlePackSelected(),"zebra") && portraitBattlePackEnabled());
        CHECK(!strcmp(portraitMenuPackSelected(),"amber"));
        SDL_Quit(); puts("portrait settings reloaded"); return 0;
    }
    CHECK(!strcmp(portraitPackSelected(),"original")); CHECK(!portraitPackEnabled());
    CHECK(!strcmp(portraitBattlePackSelected(),"original") && !portraitBattlePackEnabled());
    CHECK(!strcmp(portraitMenuPackSelected(),"original") && !portraitPackSizeEnabled(48));
    CHECK(setDir(argv[1],"zebra")); CHECK(setDir(argv[1],"amber"));
    portraitPackSetRoot(argv[1]); CHECK(portraitPackCount()==3);
    CHECK(!strcmp(portraitPackSet(0)->id,"original")); CHECK(!strcmp(portraitPackSet(1)->id,"amber"));
    CHECK(!strcmp(portraitPackSet(2)->id,"zebra")); CHECK(!portraitPackSet(-1) && !portraitPackSet(3));
    CHECK(!strcmp(portraitPackSet(1)->author,"Procedural tests"));
    CHECK(!portraitPackSelect("../amber")); CHECK(!strcmp(portraitPackSelected(),"original"));
    CHECK(!portraitPackSelect("/amber")); CHECK(!portraitPackSelect(""));
    CHECK(portraitPackSelect("amber")); CHECK(portraitPackEnabled()); CHECK(!texturePackEnabled());
    uint8_t tim[4640]={0}; put32(tim,16); put32(tim+4,9); put32(tim+8,524);
    put16(tim+16,256); put16(tim+18,1); put16(tim+22,31);
    put32(tim+532,4108); put16(tim+540,32); put16(tim+542,64); memset(tim+544,1,4096);
    uint64_t texel,palette; CHECK(portraitSourceKeys(tim,sizeof(tim),&texel,&palette));
    for(size_t n=0;n<sizeof(tim);++n) CHECK(!portraitSourceKeys(tim,n,&texel,&palette));
    tim[4]=8; CHECK(!portraitSourceKeys(tim,sizeof(tim),&texel,&palette)); tim[4]=9;
    CHECK(portraitSourceKeys(tim,sizeof(tim),&texel,&palette));
    PortraitImage a,b; CHECK(!portraitPackTim(tim,sizeof(tim),&a));
    PortraitPackStats stats; portraitPackStats(&stats); CHECK(stats.misses==1 && stats.kindMisses[PORTRAIT_DIALOGUE]==1);
    CHECK(image(argv[1],"amber",texel,palette,128,240)); CHECK(!portraitPackTim(tim,sizeof(tim),&a));
    portraitPackReload(); CHECK(portraitPackTim(tim,sizeof(tim),&a)); CHECK(a.size==128 && a.scale==2);
    CHECK(a.rgba[0]==240 && a.rgba[3]==0 && a.rgba[7]==128 && a.rgba[11]==255);
    CHECK(portraitPackTim(tim,sizeof(tim),&b)); CHECK(a.revision==b.revision && a.rgba==b.rgba);
    uint64_t rev=a.revision; CHECK(image(argv[1],"zebra",texel,palette,256,40));
    CHECK(portraitPackSelect("zebra")); CHECK(portraitPackTim(tim,sizeof(tim),&a)); CHECK(a.size==256 && a.rgba[0]==40 && a.revision!=rev);
    CHECK(portraitPackSelect("original")); CHECK(!portraitPackTim(tim,sizeof(tim),&a));
    CHECK(portraitPackSelect("missing")); CHECK(!portraitPackEnabled());
    CHECK(portraitPackSelect("amber")); CHECK(portraitPackTim(tim,sizeof(tim),&a)); rev=a.revision;
    CHECK(image(argv[1],"amber",texel,palette,129,60)); portraitPackReload(); CHECK(!portraitPackTim(tim,sizeof(tim),&a));
    portraitPackStats(&stats); CHECK(stats.invalid==1);
    CHECK(image(argv[1],"amber",texel,palette,128,200)); portraitPackReload(); CHECK(portraitPackTim(tim,sizeof(tim),&a)); CHECK(a.revision!=rev);
    ++tim[22]; CHECK(!portraitPackTim(tim,sizeof(tim),&a)); --tim[22];
    for(int i=0;i<36;++i) { CHECK(image(argv[1],"amber",texel+i+1,palette,512,i)); CHECK(portraitPackImage(texel+i+1,palette,&a)); }
    portraitPackStats(&stats); CHECK(stats.bytes<=32u*1024u*1024u);
    /* Over the budget the least recently used pictures go, never the face
     * drawn every frame (xg-j85.21). */
    portraitPackReload(); PortraitImage hot; CHECK(portraitPackImage(texel+1,palette,&hot));
    for(int i=2;i<=36;++i) {
        CHECK(portraitPackImage(texel+i,palette,&a));
        CHECK(portraitPackImage(texel+1,palette,&b) && b.revision==hot.revision && b.rgba==hot.rgba);
    }
    portraitPackStats(&stats);
    CHECK(stats.bytes<=32u*1024u*1024u && stats.loads==36 && stats.kindDraws[PORTRAIT_DIALOGUE]==71 && !stats.kindMisses[PORTRAIT_DIALOGUE]);
    CHECK(!portraitBattlePackSelect("../amber"));
    CHECK(portraitBattlePackSelect("zebra"));
    CHECK(!portraitMenuPackSelect("../amber"));CHECK(portraitMenuPackSelect("amber"));
    /* The menu's 24 px faces (the Load screen's) count apart (xg-j85.40). */
    DecodedTexture face={0}; face.depth=8; face.request=(TextureRequest){0,0,0,0,0,24,24,0,0};
    face.texelKey=texel+100; face.paletteKey=palette; TextureSample sample;
    CHECK(!portraitPackClassSample(&face,face.request,PC_TEX_MENU_THUMB_PORTRAIT,&sample));
    portraitPackStats(&stats); CHECK(stats.kindMisses[PORTRAIT_MENU_THUMB]==1 && !stats.kindMisses[PORTRAIT_MENU]);
    CHECK(image(argv[1],"amber",texel+100,palette,96,50)); portraitPackReload();
    CHECK(portraitPackClassSample(&face,face.request,PC_TEX_MENU_THUMB_PORTRAIT,&sample) && sample.scale==4);
    portraitPackStats(&stats); CHECK(stats.kindDraws[PORTRAIT_MENU_THUMB]==1 && !stats.kindMisses[PORTRAIT_MENU_THUMB]);
    CHECK(!strcmp(portraitPackSelected(),"amber") && portraitPackEnabled());
    portraitPackShutdown(); CHECK(!strcmp(portraitPackSelected(),"amber")); CHECK(portraitPackEnabled());
    portraitPackShutdown(); texturePackShutdown(); SDL_Quit(); printf("portraitpacktest: %d checks\n",checks); return 0;
}
