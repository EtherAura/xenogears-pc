/* Real dialogue packet class: independent sets, live CLUT guard and native VRAM. */
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "common.h"
#include "psyq.h"
#include "pc_wide.h"
#include "test/env.h"
#include "pack_fixture.h"
#include "gpu/portrait_pack.h"
#include "gpu/interpolation.h"
#include "pc_interpolation.h"
#include "gte_pc.h"
int gpuInit(SDL_Window* window);
void gpuShutdown(void);
static int checks;
#include "test/check.h"
static uint32_t pixel(int x,int y) { uint32_t p=0; gvkHiRead(0,&p,x,y,1,1); return p&0x1ffffff; }
static void makeQuad(POLY_FT4* p,int x,int y,int tp,int clut,int portrait,int crop,int extent) {
    SetPolyFT4(p); SetShadeTex(p,1);
    p->x0=p->x2=x; p->x1=p->x3=x+extent; p->y0=p->y1=y; p->y2=p->y3=y+extent;
    p->u0=p->u2=crop; p->v0=p->v1=0; p->u1=p->u3=crop+extent; p->v2=p->v3=extent;
    p->tpage=tp; p->clut=clut;
    pcTextureClass(p,portrait?PC_TEX_PORTRAIT:PC_TEX_3D);
}
static void quad(POLY_FT4* p,int x,int y,int tp,int clut,int portrait,int crop,int extent) {
    makeQuad(p,x,y,tp,clut,portrait,crop,extent); DrawPrim(p);
}
static int setDir(const char* root,const char* set) {
    char path[3500]; snprintf(path,sizeof(path),"%s/%s/%s",root,set,texturePackVersion()); if(!SDL_CreateDirectory(path)) return 0;
    snprintf(path,sizeof(path),"%s/%s/portrait.ini",root,set);
    SDL_IOStream* io=SDL_IOFromFile(path,"wb"); if(!io) return 0;
    const char text[]="name=GPU fixture\n";
    int ok=SDL_WriteIO(io,text,sizeof(text)-1)==sizeof(text)-1; return SDL_CloseIO(io) && ok;
}
static int picture(const char* root,const char* set,const DecodedTexture* d,int r,int g,int b) {
    char path[3500]; snprintf(path,sizeof(path),"%s/%s/%s/%016" PRIx64 "-%016" PRIx64 ".png",root,set,texturePackVersion(),d->texelKey,d->paletteKey);
    SDL_Surface* s=SDL_CreateSurface(d->request.w*4,d->request.h*4,SDL_PIXELFORMAT_RGBA32); if(!s) return 0;
    SDL_FillSurfaceRect(s,NULL,SDL_MapSurfaceRGBA(s,r,g,b,255));
    int ok=SDL_SavePNG(s,path); SDL_DestroySurface(s); return ok;
}
static int cutout(const char* root,const DecodedTexture* d) {
    char path[3500]; snprintf(path,sizeof(path),"%s/cutout/%s/%016" PRIx64 "-%016" PRIx64 ".png",root,texturePackVersion(),d->texelKey,d->paletteKey);
    SDL_Surface* s=SDL_CreateSurface(256,256,SDL_PIXELFORMAT_RGBA32); if(!s) return 0;
    SDL_FillSurfaceRect(s,NULL,SDL_MapSurfaceRGBA(s,255,0,255,255));
    SDL_Rect edge={0,0,64,256}; SDL_FillSurfaceRect(s,&edge,0);
    /* Fine artwork exposes phase/clamp differences hidden by flat colours. */
    for(int y=80;y<180;y+=2) for(int x=80;x<180;x+=2) if((x+y)%4) {
        SDL_Rect dot={x,y,2,2}; SDL_FillSurfaceRect(s,&dot,SDL_MapSurfaceRGBA(s,0,255,255,255));
    }
    int ok=SDL_SavePNG(s,path); SDL_DestroySurface(s); return ok;
}
static void preciseSurface(void) {
    static POLY_FT4 surface; static int owner;
    SetPolyFT4(&surface); SetShadeTex(&surface,1);
    surface.x0=surface.x2=160; surface.x1=surface.x3=192;
    surface.y0=surface.y1=30; surface.y2=surface.y3=62;
    surface.u0=surface.u2=surface.v0=surface.v1=0;
    surface.u1=surface.u3=surface.v2=surface.v3=15;
    surface.tpage=GetTPage(2,0,896,0);
    void* xy[]={&surface.x0,&surface.x1,&surface.x2,&surface.x3};
    for(int k=0;k<4;k++) {
        uint32_t word; memcpy(&word,xy[k],4);
        GtePrecise v={(int16_t)word+0.25,(int16_t)(word>>16)+0.25,4096,word,1};
        gtePrecisionRecord(xy[k],&v);
    }
    pcTextureClass(&surface,PC_TEX_3D);
    pcInterpolationObject(&owner,&surface); pcInterpolationModel(&surface); pcInterpolationPrim(&surface);
    DrawPrim(&surface); pcInterpolationObject(NULL,NULL);
}
static int sameSurface(int n) {
    int width=24*n; size_t count=(size_t)width*width;
    uint32_t* live=malloc(count*4),*replay=malloc(count*4); int ok=live && replay;
    if(ok) ok=gvkHiRead(0,live,162*n,32*n,width,width) && gvkInterpolationRead(replay,162*n,32*n,width,width);
    for(size_t i=0;ok && i<count;i++) if((live[i]&0x1ffffff)!=(replay[i]&0x1ffffff)) ok=0;
    free(live); free(replay); return ok;
}
static int samePortrait(int n,int clipped) {
    int width=64*n,height=(clipped?42:64)*n; size_t count=(size_t)width*height;
    uint32_t* live=malloc(count*4),*replay=malloc(count*4); int ok=live && replay;
    if(ok) ok=gvkHiRead(0,live,10*n,10*n,width,height) && gvkInterpolationRead(replay,10*n,10*n,width,height);
    int different=0;
    for(size_t i=0;ok && i<count;i++) if((live[i]&0x1ffffff)!=(replay[i]&0x1ffffff)) {
        if(different++<12) fprintf(stderr,"face difference %zu,%zu: %08x / %08x\n",i%width,i/width,live[i],replay[i]);
    }
    if(different) { fprintf(stderr,"face differences: %d\n",different); ok=0; }
    free(live); free(replay); return ok;
}
static void portraitFrame(int tp,int clut,int mirror,int clipped,int sourceV) {
    DRAWENV env; SetDefDrawEnv(&env,0,0,320,224); PutDrawEnv(&env);
    pcInterpolationFrameBegin(8,0,0,320,224);
    RECT r={0,0,320,224}; ClearImage(&r,0,0,248);
    static POLY_F4 model; static int owner,frame;
    SetPolyF4(&model); setRGB0(&model,0,248,0);
    int x=100+(frame++%2)*10;
    model.x0=model.x2=x; model.x1=model.x3=x+16;
    model.y0=model.y1=100; model.y2=model.y3=116;
    pcInterpolationObject(&owner,&model); pcInterpolationModel(&model); pcInterpolationPrim(&model);
    DrawPrim(&model); pcInterpolationObject(NULL,NULL);
    preciseSurface();
    if(clipped) { SetDefDrawEnv(&env,0,0,320,52); PutDrawEnv(&env); }
    POLY_FT4 p; makeQuad(&p,10,10,tp,clut,1,64,64);
    p.v0=p.v1=sourceV; p.v2=p.v3=sourceV+64;
    if(mirror) {
        p.x0=p.x2=74; p.x1=p.x3=10;
        p.u0=p.u2=63; p.u1=p.u3=127;
    }
    DrawPrim(&p);
    pcInterpolationFrameEnd(); interpolationPacketsDone();
}
static int replayPixel(int n,int x,int y,uint32_t wanted) {
    uint32_t p=0; return gvkInterpolationRead(&p,x*n+n/2,y*n+n/2,1,1) && (p&0x1ffffff)==wanted;
}
static int motion(const char* root,const DecodedTexture* d,int tp,int clut,int maxScale) {
    CHECK(setDir(root,"cutout")); CHECK(cutout(root,d)); CHECK(portraitPackSelect("cutout"));
    uint16_t texels[16*16];
    for(int y=0;y<16;y++) for(int x=0;x<16;x++) texels[y*16+x]=(x&1?31:0)|((y%3?15:31)<<5)|(((x+y)%5)<<10);
    RECT rect={896,0,16,16}; LoadImage(&rect,texels);
    uint16_t indices[32*64]; for(int i=0;i<32*64;i++) indices[i]=i%32?0x0101:0x0102;
    RECT face={672,256,32,64};
    GpuDisplay display={0,0,320,224,0,1,0,0,0,0,0};
    const int rows[]={0,64,128,191}; /* includes the last, unaligned atlas row */
    for(int row=0;row<4;row++) for(int aged=0;aged<=2;aged++) for(int n=4;n<=maxScale;n+=8) for(int mirror=0;mirror<=1;mirror++) for(int original=0;original<=1;original++) {
        face.y=256+rows[row];
        LoadImage(&face,indices);
        /* Streaming unrelated textures must not change a stationary face.
         * Evict its upload from the bounded provenance history. */
        if(aged==1) for(int i=0;i<65;i++) {
            RECT unrelated={960,256,1,1}; uint16_t value=(uint16_t)(i+1);
            LoadImage(&unrelated,&value);
        }
        if(aged==2) {
            /* A later atlas upload can cover more than one face. Its
             * bounds must not become this portrait's content key either. */
            uint16_t atlas[64*64]; for(int i=0;i<64*64;i++) atlas[i]=i%32?0x0101:0x0102;
            RECT combined={640,face.y,64,64}; LoadImage(&combined,atlas);
        }
        CHECK(portraitPackSelect(original?"original":"cutout"));
        interpolationSetEnabled(0); hiresSetUpscale(n); hiresSetPrecision(1); hiresSetSeamGuard(1);
        CHECK(gvkHiConfigure(n,HIRES_COLOR_RAW)==n); interpolationSetEnabled(1);
        int stamp=100+row*8000+aged*2000+n*100+mirror*40+original*20;
        portraitFrame(tp,clut,mirror,aged,rows[row]); portraitFrame(tp,clut,mirror,aged,rows[row]); gvkPresent(&display,stamp);
        portraitFrame(tp,clut,mirror,aged,rows[row]); gvkPresent(&display,stamp+2);
        /* Replacement transparency exposes the earlier background, not the
         * opaque original portrait; snapshots use the same authored image. */
        CHECK(pixel((mirror?60:12)*n+n/2,12*n+n/2)==(original?255:0xff0000));
        CHECK(pixel(30*n+n/2,12*n+n/2)==(original?255:0xff00ff));
        int a,b; double alpha; CHECK(interpolationPicture(&display,stamp+3,&a,&b,&alpha) && alpha==0.5);
        gvkPresent(&display,stamp+3);
        CHECK(replayPixel(n,mirror?60:12,12,original?255:0xff0000)); CHECK(replayPixel(n,30,12,original?255:0xff00ff));
        int same=samePortrait(n,aged);
        if(!same) fprintf(stderr,"atlas row=%d provenance=%d scale=%d mirror=%d original=%d\n",rows[row],aged,n,mirror,original);
        CHECK(same);
        /* Stationary projected textures use exactly the live draw's sample
         * phase, even while another object moves at an intermediate time. */
        CHECK(sameSurface(n));
        uint32_t native=0; RECT one={12,12,1,1}; StoreImage(&one,&native); CHECK((native&65535)==31);
        interpolationSetEnabled(0);
    }
    return 0;
}
/* Battle faces are 24x24 uploads inside a crowded atlas. Their own class
 * must retain that exact canvas in live draws and immutable replay frames. */
static void face24Frame(int kind,int tp,int clut,int shift) {
    DRAWENV env; SetDefDrawEnv(&env,0,0,320,224); PutDrawEnv(&env);
    pcInterpolationFrameBegin(8,0,0,320,224);
    RECT clear={0,0,320,224}; ClearImage(&clear,0,0,0);
    static POLY_F4 model; static int owner;
    SetPolyF4(&model);setRGB0(&model,0,248,0);
    model.x0=model.x2=100+shift;model.x1=model.x3=116+shift;
    model.y0=model.y1=100;model.y2=model.y3=116;
    pcInterpolationObject(&owner,&model);pcInterpolationModel(&model);pcInterpolationPrim(&model);
    DrawPrim(&model);pcInterpolationObject(NULL,NULL);
    POLY_FT4 p;makeQuad(&p,10,10,tp,clut,0,0,24);
    p.v0=p.v1=64;p.v2=p.v3=88;pcTextureClass(&p,kind);DrawPrim(&p);
    POLY_FT4 hud;makeQuad(&hud,200,20,GetTPage(2,0,896,256),0,0,0,16);
    pcTextureClass(&hud,PC_TEX_BATTLE_HUD);DrawPrim(&hud);
    pcInterpolationFrameEnd();interpolationPacketsDone();
}
static void battleFrame(int tp,int clut,int shift) { face24Frame(PC_TEX_BATTLE_PORTRAIT,tp,clut,shift); }
static int battleFaces(const char* root,int tp,int clut) {
    RECT image={640,320,12,24};uint16_t data[12*24];
    for(int i=0;i<12*24;i++)data[i]=0x0101;LoadImage(&image,data);
    TextureRequest request={tp,clut,0,0,64,24,24,0,0};
    const DecodedTexture* source=textureCacheGet(request);CHECK(source && source->depth==8);
    DecodedTexture d=*source;
    CHECK(picture(root,"a",&d,0,255,255));CHECK(portraitPackSelect("a"));
    CHECK(!strcmp(portraitBattlePackSelected(),"original"));
    battleFrame(tp,clut,0);CHECK(pixel(80,80)==255);
    CHECK(portraitBattlePackSelect("a"));CHECK(portraitPackSelect("original"));
    CHECK(portraitBattlePackEnabled() && !portraitPackEnabled());
    uint16_t hud[16*16];for(int i=0;i<16*16;i++)hud[i]=31;
    hud[1]=hud[16]=31<<10;RECT hudRect={896,256,16,16};LoadImage(&hudRect,hud);
    textureSetHudUpscale(0,0);textureSetHudUpscale(1,4);
    textureSetFilter(PC_TEX_BATTLE_PORTRAIT,TEXTURE_FILTER_EDGE);
    textureSetFilter(PC_TEX_ICON,TEXTURE_FILTER_LINEAR);
    CHECK(textureFilter(PC_TEX_PORTRAIT)==TEXTURE_FILTER_NEAREST);
    CHECK(textureFilter(PC_TEX_BATTLE_PORTRAIT)==TEXTURE_FILTER_NEAREST);
    CHECK(textureFilter(PC_TEX_HUD)==TEXTURE_FILTER_NEAREST);
    CHECK(textureFilter(PC_TEX_ICON)==TEXTURE_FILTER_LINEAR);
    interpolationSetEnabled(1);
    GpuDisplay display={0,0,320,224,0,1,0,0,0,0,0};
    battleFrame(tp,clut,0);battleFrame(tp,clut,0);gvkPresent(&display,100);
    battleFrame(tp,clut,4);gvkPresent(&display,102);
    CHECK(pixel(80,80)==0xffff00);
    CHECK(pixel(804,84)==0xff0000);
    int a,b;double alpha;CHECK(interpolationPicture(&display,103,&a,&b,&alpha) && alpha==0.5);
    CHECK(portraitBattlePackSelect("original"));textureSetHudUpscale(1,0);gvkPresent(&display,103);
    CHECK(replayPixel(4,20,20,0xffff00));
    /* A queued HUD image owns its upscale after the setting changes. */
    uint32_t replay=0;CHECK(gvkInterpolationRead(&replay,804,84,1,1) && (replay&0x1ffffff)==0xff0000);
    uint32_t native=0;RECT one={20,20,1,1};StoreImage(&one,&native);CHECK((native&65535)==31);
    interpolationSetEnabled(0);battleFrame(tp,clut,0);CHECK(pixel(80,80)==255);
    CHECK(pixel(804,84)==255);
    textureSetFilter(PC_TEX_BATTLE_PORTRAIT,TEXTURE_FILTER_NEAREST);
    textureSetFilter(PC_TEX_ICON,TEXTURE_FILTER_NEAREST);
    return 0;
}
static void menuFrame(int tp,int clut,int shift,int projected) {
    DRAWENV env;SetDefDrawEnv(&env,0,0,320,224);PutDrawEnv(&env);
    pcInterpolationFrameBegin(9,0,0,320,224);RECT r={0,0,320,224};ClearImage(&r,0,0,0);
    static POLY_F4 model;static int owner;SetPolyF4(&model);setRGB0(&model,0,248,0);
    model.x0=model.x2=100+shift;model.x1=model.x3=116+shift;model.y0=model.y1=100;model.y2=model.y3=116;
    pcInterpolationObject(&owner,&model);pcInterpolationModel(&model);pcInterpolationPrim(&model);DrawPrim(&model);pcInterpolationObject(NULL,NULL);
    POLY_FT4 p;makeQuad(&p,10,10,tp,clut,0,0,48);p.v0=p.v1=128;p.v2=p.v3=176;
    if(projected) {
        void* xy[]={&p.x0,&p.x1,&p.x2,&p.x3};
        for(int k=0;k<4;k++) {
            uint32_t word;memcpy(&word,xy[k],sizeof(word));
            GtePrecise v={(int16_t)word+0.25,(int16_t)(word>>16)+0.25,4096,word,1};
            gtePrecisionRecord(xy[k],&v);
        }
    }
    pcTextureClass(&p,PC_TEX_MENU_PORTRAIT);DrawPrim(&p);
    pcInterpolationFrameEnd();interpolationPacketsDone();
}
static int menuFaces(const char* root,int tp,int clut) {
    RECT r={640,384,24,48};uint16_t texels[24*48];for(int i=0;i<24*48;i++)texels[i]=0x0101;LoadImage(&r,texels);
    TextureRequest req={tp,clut,0,0,128,48,48,0,0};const DecodedTexture* d=textureCacheGet(req);CHECK(d);
    CHECK(picture(root,"a",d,0,255,255));CHECK(!strcmp(portraitMenuPackSelected(),"original"));
    menuFrame(tp,clut,0,0);CHECK(pixel(80,80)==255);
    CHECK(portraitMenuPackSelect("a"));CHECK(!portraitPackEnabled() && !portraitBattlePackEnabled());
    interpolationSetEnabled(1);GpuDisplay display={0,0,320,224,0,1,0,0,0,0,0};
    menuFrame(tp,clut,0,0);menuFrame(tp,clut,0,0);gvkPresent(&display,200);menuFrame(tp,clut,4,0);gvkPresent(&display,202);
    CHECK(pixel(80,80)==0xffff00);CHECK(portraitMenuPackSelect("original"));gvkPresent(&display,203);
    CHECK(replayPixel(4,20,20,0xffff00));interpolationSetEnabled(0);menuFrame(tp,clut,0,0);CHECK(pixel(80,80)==255);
    /* Status portraits go through the GTE. Conservative triangle bounds
     * include the excluded UV endpoint (49x49); their canvas is still 48x48. */
    hiresSetPrecision(1);CHECK(portraitMenuPackSelect("a"));
    menuFrame(tp,clut,0,1);CHECK(pixel(80,80)==0xffff00);
    interpolationSetEnabled(1);
    menuFrame(tp,clut,0,1);menuFrame(tp,clut,0,1);gvkPresent(&display,210);
    menuFrame(tp,clut,4,1);gvkPresent(&display,212);
    CHECK(portraitMenuPackSelect("original"));gvkPresent(&display,213);
    CHECK(replayPixel(4,20,20,0xffff00));
    interpolationSetEnabled(0);menuFrame(tp,clut,0,1);CHECK(pixel(80,80)==255);
    hiresSetPrecision(0);
    uint32_t native=0;RECT one={20,20,1,1};StoreImage(&one,&native);CHECK((native&65535)==31);
    return 0;
}
/* A Load thumbnail shares 24px geometry with battle faces, but must follow
 * the independent menu set even when its native content key is identical. */
static int thumbnailFaces(const char* root,int tp,int clut) {
    TextureRequest req={tp,clut,0,0,64,24,24,0,0};const DecodedTexture* d=textureCacheGet(req);CHECK(d);
    CHECK(picture(root,"b",d,0,0,255));CHECK(portraitBattlePackSelect("a"));
    CHECK(portraitMenuPackSelect("original"));
    face24Frame(PC_TEX_MENU_THUMB_PORTRAIT,tp,clut,0);CHECK(pixel(80,80)==255);
    CHECK(portraitMenuPackSelect("b"));CHECK(portraitPackClassEnabled(PC_TEX_MENU_THUMB_PORTRAIT));
    textureSetFilter(PC_TEX_MENU_PORTRAIT,TEXTURE_FILTER_LINEAR);
    CHECK(textureFilter(PC_TEX_MENU_THUMB_PORTRAIT)==TEXTURE_FILTER_NEAREST);
    face24Frame(PC_TEX_MENU_THUMB_PORTRAIT,tp,clut,0);CHECK(pixel(80,80)==0xff0000);
    battleFrame(tp,clut,0);CHECK(pixel(80,80)==0xffff00);
    face24Frame(PC_TEX_MENU_THUMB_PORTRAIT,tp,clut,0);CHECK(pixel(80,80)==0xff0000);
    interpolationSetEnabled(1);GpuDisplay display={0,0,320,224,0,1,0,0,0,0,0};
    face24Frame(PC_TEX_MENU_THUMB_PORTRAIT,tp,clut,0);face24Frame(PC_TEX_MENU_THUMB_PORTRAIT,tp,clut,0);gvkPresent(&display,300);
    face24Frame(PC_TEX_MENU_THUMB_PORTRAIT,tp,clut,4);gvkPresent(&display,302);
    CHECK(portraitMenuPackSelect("original"));gvkPresent(&display,303);CHECK(replayPixel(4,20,20,0xff0000));
    interpolationSetEnabled(0);face24Frame(PC_TEX_MENU_THUMB_PORTRAIT,tp,clut,0);CHECK(pixel(80,80)==255);
    uint32_t native=0;RECT one={20,20,1,1};StoreImage(&one,&native);CHECK((native&65535)==31);
    CHECK(portraitBattlePackSelect("original"));textureSetFilter(PC_TEX_MENU_PORTRAIT,TEXTURE_FILTER_NEAREST);
    return 0;
}
int main(int argc,char** argv) {
    SDL_setenv_unsafe("XG_SPEEDUP", "1", 1);
    CHECK(argc>=2); CHECK(testSetEnv("XG_SETTINGS","none")==0);
    CHECK(testSetEnv("XG_TEXTURE_PACK","none")==0); /* not the default library beside the executable */
    CHECK(testSetEnv("XG_DUMP_PRESENT",argv[1])==0); CHECK(testSetEnv("XG_DUMP_RANGE","0-40000")==0);
    CHECK(testSetEnv("XG_ASPECT","1:1")==0); CHECK(testSetEnv("XG_SCALE","fit")==0);
    CHECK(SDL_Init(SDL_INIT_VIDEO));
    SDL_Window* window=SDL_CreateWindow("portraitgputest",320,224,SDL_WINDOW_HIDDEN); CHECK(window && gpuInit(window)); gpuVBlank();
    DRAWENV env; SetDefDrawEnv(&env,0,0,320,224); PutDrawEnv(&env);
    hiresSetUpscale(4); hiresSetPrecision(0); hiresSetSeamGuard(0); hiresSetColor(HIRES_COLOR_RAW); CHECK(gvkHiConfigure(4,HIRES_COLOR_RAW)==4);
    RECT image={640,256,32,64}, pal={768,480,256,1}; uint16_t data[2048],palette[256]={0,31,31<<5};
    for(int i=0;i<2048;++i) data[i]=i%32?0x0101:0x0102;
    LoadImage(&image,data); LoadImage(&pal,palette);
    int tp=GetTPage(1,0,640,256),clut=GetClut(768,480);
    TextureRequest req={tp,clut,0,0,0,64,64,0,0}; const DecodedTexture* original=textureCacheGet(req); CHECK(original);
    DecodedTexture d=*original; CHECK(d.depth==8);
    CHECK(setDir(argv[1],"a")); CHECK(setDir(argv[1],"b"));
    CHECK(picture(argv[1],"a",&d,255,0,255)); CHECK(picture(argv[1],"b",&d,0,255,0));
    portraitPackSetRoot(argv[1]); CHECK(portraitPackSelect("a")); CHECK(!texturePackEnabled());
    POLY_FT4 p;
    quad(&p,10,10,tp,clut,1,0,64); CHECK(pixel(48,48)==0xff00ff);
    quad(&p,50,10,tp,clut,0,0,32); CHECK(pixel(208,48)==255);
    CHECK(portraitPackSelect("b")); quad(&p,90,10,tp,clut,1,16,32); CHECK(pixel(368,48)==0xff00);
    /* Earlier snapshot remains immutable through a switch/reload. */
    CHECK(pixel(48,48)==0xff00ff);
    uint32_t exact=0; RECT one={12,12,1,1}; StoreImage(&one,&exact); CHECK((exact&65535)==31);
    char general[3400]; snprintf(general,sizeof(general),"%s/general",argv[1]); CHECK(packDir(general));
    uint8_t rgba[128*128*4]; for(int i=0;i<128*128;++i) { rgba[i*4]=0; rgba[i*4+1]=255; rgba[i*4+2]=255; rgba[i*4+3]=255; }
    CHECK(packPng(general,&d,0,128,128,rgba)); texturePackSetRoot(general);
    CHECK(portraitPackSelect("original")); quad(&p,10,50,tp,clut,1,0,32); CHECK(pixel(48,208)==255);
    quad(&p,50,50,tp,clut,0,0,32); CHECK(pixel(208,208)==0xffff00);
    CHECK(portraitPackSelect("a")); quad(&p,90,50,tp,clut,1,0,32); CHECK(pixel(368,208)==0xff00ff);
    palette[1]=31<<10; LoadImage(&pal,palette); quad(&p,130,50,tp,clut,1,0,32); CHECK(pixel(528,208)==0xff0000);
    palette[1]=31; LoadImage(&pal,palette);
    if(battleFaces(argv[1],tp,clut)) return 1;
    if(menuFaces(argv[1],tp,clut)) return 1;
    if(thumbnailFaces(argv[1],tp,clut)) return 1;
    if(motion(argv[1],&d,tp,clut,argc>2?atoi(argv[2]):4)) return 1;
    portraitPackStats(&(PortraitPackStats){0});
    gpuShutdown(); SDL_DestroyWindow(window); SDL_Quit(); printf("portraitgputest: %d checks\n",checks); return 0;
}
