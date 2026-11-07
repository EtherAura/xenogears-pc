/* Replacements through real GP0 packets, uploads, captures and frame arenas. */
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
int gpuInit(SDL_Window* window);
void gpuShutdown(void);
void SetTile(void* p);
static int checks;
#include "test/check.h"
/* Colour and PS1 mask, excluding display-only seam coverage. */
static uint32_t pixel(int x,int y) { uint32_t p=0; gvkHiRead(0,&p,x,y,1,1); return p & 0x1ffffff; }
static void quad(POLY_FT4* p,int x,int y,int tp,int clut,int uv) {
    SetPolyFT4(p); SetShadeTex(p,1);
    p->x0=p->x2=x; p->x1=p->x3=x+4; p->y0=p->y1=y; p->y2=p->y3=y+4;
    p->u0=p->u2=p->v0=p->v1=0; p->u1=p->u3=uv; p->v2=p->v3=4;
    p->tpage=tp; p->clut=clut;
}
int main(int argc,char** argv) {
    CHECK(argc>=2); CHECK(testSetEnv("XG_SETTINGS","none")==0); CHECK(SDL_Init(SDL_INIT_VIDEO));
    CHECK(packDir(argv[1])); CHECK(gpuInit(NULL));
    DRAWENV env; SetDefDrawEnv(&env,0,0,320,224); PutDrawEnv(&env);
    hiresSetUpscale(4); hiresSetPrecision(0); hiresSetSeamGuard(0); hiresSetColor(HIRES_COLOR_RAW);
    CHECK(gvkHiConfigure(4,HIRES_COLOR_RAW)==4);
    RECT image={640,256,1,4}, pal={976,480,16,1};
    uint16_t data[4]={0x1111,0x1111,0x1111,0x1111}, palette[16]={0,31,0xfc00,31<<5};
    LoadImage(&image,data); LoadImage(&pal,palette);
    int tp=GetTPage(0,0,640,256), clut=GetClut(976,480);
    TextureRequest req={tp,clut,0,0,0,4,4,0,0};
    const DecodedTexture* original=textureCacheGet(req); CHECK(original);
    DecodedTexture d=*original;
    uint8_t indexed[8*8*4];
    for (int i=0;i<64;++i) { memset(indexed+i*4,i%8<4?2:3,3); indexed[i*4+3]=255; }
    CHECK(packPng(argv[1],&d,1,8,8,indexed)); texturePackSetRoot(argv[1]);
    POLY_FT4 p;
    quad(&p,20,20,tp,clut,4); DrawPrim(&p);
    palette[2]=0x83e0; palette[3]=0; LoadImage(&pal,palette);
    quad(&p,30,20,tp,clut,4); DrawPrim(&p);
    CHECK(pixel(80,80)==0x1ff0000); CHECK(pixel(88,80)==0xff00);
    CHECK(pixel(120,80)==0x100ff00); CHECK(pixel(128,80)==0);
    uint32_t exact=0; RECT one={20,20,1,1}; StoreImage(&one,&exact); CHECK((exact&65535)==31);
    /* A live CLUT swap at a different VRAM address uses the same index PNG. */
    pal.x=944; palette[2]=0x801f; LoadImage(&pal,palette); int swap=GetClut(944,480);
    quad(&p,40,20,tp,swap,4); DrawPrim(&p); CHECK(pixel(160,80)==0x10000ff);
    /* RGBA replaces only the exact texel+palette pair; reload does not
     * overwrite a snapshot already queued in this command buffer. */
    d=*textureCacheGet(req);
    uint8_t rgba[12*12*4];
    for (int i=0;i<144;++i) { rgba[i*4]=255; rgba[i*4+1]=0; rgba[i*4+2]=255; rgba[i*4+3]=255; }
    CHECK(packPng(argv[1],&d,0,12,12,rgba)); texturePackReload();
    quad(&p,50,20,tp,clut,4); DrawPrim(&p);
    for (int i=0;i<144;++i) { rgba[i*4]=0; rgba[i*4+1]=255; rgba[i*4+2]=255; }
    CHECK(packPng(argv[1],&d,0,12,12,rgba)); texturePackReload();
    quad(&p,60,20,tp,clut,4); DrawPrim(&p);
    CHECK(pixel(200,80)==0xff00ff); CHECK(pixel(240,80)==0xffff00);
    /* Author 3x detail inside one logical texel. Nearest and linear both
     * use replacement pixels, and taps stop at the primitive UV bounds. */
    for (int y=0;y<12;++y) for (int x=0;x<12;++x) {
        int at=(y*12+x)*4; rgba[at]=x==0?255:0; rgba[at+1]=x==1?255:0;
        rgba[at+2]=x>=2?255:0; rgba[at+3]=255;
    }
    CHECK(packPng(argv[1],&d,0,12,12,rgba)); texturePackReload();
    quad(&p,70,20,tp,clut,4); DrawPrim(&p);
    CHECK(pixel(280,80)==255 && pixel(281,80)==0xff00 && pixel(283,80)==0xff0000);
    textureSetFilter(0,1); quad(&p,80,20,tp,clut,4); DrawPrim(&p);
    uint32_t blend=pixel(321,80); CHECK(blend!=255 && blend!=0xff00 && blend!=0xff0000);
    /* Cropped primitive sees only the first three authored columns. */
    textureSetFilter(0,1); quad(&p,90,20,tp,clut,1); p.u1=p.u3=0; DrawPrim(&p);
    CHECK((pixel(375,80)&0xffffff)==0xff00);
    textureSetFilter(0,2); quad(&p,100,20,tp,clut,4); DrawPrim(&p);
    CHECK(pixel(400,80)==255 && pixel(401,80)==0xff00); /* preserve authored detail */
    /* A native-sized pack uses resolution-aware cuts above 2x, rather
     * than being converted to 2x before it reaches the GPU. */
    uint8_t native[4*4*4];
    for (int y=0;y<4;++y) for (int x=0;x<4;++x) {
        int at=(y*4+x)*4;
        memset(native+at,x+y<4 ? 255:0,3); native[at+3]=255;
    }
    CHECK(packPng(argv[1],&d,0,4,4,native)); texturePackReload();
    quad(&p,100,30,tp,clut,4); DrawPrim(&p);
    uint32_t cut[16*16]; CHECK(gvkHiRead(0,cut,400,120,16,16));
    int mixed=0;
    for (int i=0;i<16*16;++i) mixed += (cut[i]&255)>0 && (cut[i]&255)<255;
    CHECK(mixed>0);
    /* Missing files keep original nearest sampling. */
    textureSetFilter(0,0); data[0]=0x2222; LoadImage(&image,data);
    quad(&p,110,20,tp,clut,4); DrawPrim(&p); CHECK(pixel(440,80)==0x100ff00);
    /* Direct uploads can match; GPU-rendered captures cannot. */
    RECT directRect={640,0,16,16}; uint16_t direct[256]; for (int i=0;i<256;++i) direct[i]=31;
    LoadImage(&directRect,direct); int dtp=GetTPage(2,0,640,0);
    TextureRequest dr={dtp,0,0,0,0,16,16,0,0}; d=*textureCacheGet(dr);
    uint8_t directPng[48*48*4];
    for (int i=0;i<48*48;++i) { directPng[i*4]=0; directPng[i*4+1]=0; directPng[i*4+2]=255; directPng[i*4+3]=255; }
    CHECK(packPng(argv[1],&d,0,48,48,directPng)); texturePackReload();
    quad(&p,120,20,dtp,0,4); DrawPrim(&p); CHECK(pixel(480,80)==0xff0000);
    SetDefDrawEnv(&env,0,0,1024,512); PutDrawEnv(&env);
    TILE tile; SetTile(&tile); setRGB0(&tile,255,0,0); tile.x0=640; tile.y0=0; tile.w=tile.h=4;
    DrawPrim(&tile); CHECK(!textureCacheGet(dr));
    quad(&p,130,20,dtp,0,4); DrawPrim(&p); CHECK(pixel(520,80)==255);
    /* A same-pixel CPU round trip still preserves capture provenance. */
    LoadImage(&directRect,direct); CHECK(!textureCacheGet(dr));
    /* A state load discards the display copy's history, not the native
     * capture's identity. Otherwise its replacement can cover the screen. */
    hiresStateLoaded();
    LoadImage(&directRect,direct); CHECK(!textureCacheGet(dr));
    /* Native StoreImage identity also survives losing the display stash,
     * including with that copy disabled and a different upload origin. */
    uint16_t captured[256]; StoreImage(&directRect,(u32*)captured);
    hiresStateLoaded(); CHECK(gvkHiConfigure(0,HIRES_COLOR_RAW)==0);
    RECT elsewhere={704,0,16,16}; LoadImage(&elsewhere,captured);
    TextureRequest copied={GetTPage(2,0,704,0),0,0,0,0,16,16,0,0};
    CHECK(!textureCacheGet(copied));
    CHECK(gvkHiConfigure(4,HIRES_COLOR_RAW)==4); hiresStateLoaded();
    LoadImage(&elsewhere,captured); CHECK(!textureCacheGet(copied));
    /* Fresh content covering the tile restores uploaded provenance. */
    for (int i=0;i<256;++i) direct[i]=31<<5;
    LoadImage(&directRect,direct); CHECK(textureCacheGet(dr));
    d=*textureCacheGet(dr); CHECK(packPng(argv[1],&d,0,48,48,directPng)); texturePackReload();
    quad(&p,140,20,dtp,0,4); DrawPrim(&p); CHECK(pixel(560,80)==0xff0000);
    /* Arena rollover, then resident-cache growth, with 4 MiB snapshots.
     * Earlier draws must keep their colours while later uploads reuse or
     * replace storage and the caller mutates the same source buffer. */
    uint8_t* large=malloc(1024*1024*4); CHECK(large);
    memset(large,0,1024*1024*4);
    for (int i=0;i<1024*1024;++i) large[i*4+3]=255;
    HiVertex v[6]={0};
    for (int resident=0;resident<2;resident++) {
    for (int i=0;i<6;++i) {
        for (int j=0;j<6;++j) {
            int right=j==1||j==3||j==5,bottom=j==2||j==4||j==5;
            v[j].x=20+i*10+right*4; v[j].y=50+resident*8+bottom*4;
            v[j].u=right*4; v[j].v=bottom*4; v[j].w=1;
            v[j].mode=DRAW_TEXTURED|DRAW_RAW; v[j].clipMax=319u|223u<<16; v[j].uvMax=3u|3u<<16;
        }
        large[0]=i&1?255:0; large[1]=i&1?0:255;
        TextureSample snap={large,UINT64_C(0xf000000000000000)+i+resident*100,0,0,256,256,4,NULL,resident};
        gvkHiDrawTexture(v,6,0,&snap,0);
    }
    CHECK(pixel(80,200+resident*32)==0xff00 && pixel(120,200+resident*32)==255 && pixel(280,200+resident*32)==255);
    }
    free(large); TexturePackStats stats; texturePackStats(&stats); CHECK(stats.draws>=1);
    /* A shared loaded pack is opt-in for each appearance category. A
     * later selection must not alter replacement pixels already queued. */
    CHECK(textureSetAppearance(PC_TEX_3D,0));
    CHECK(textureSetAppearance(PC_TEX_SPRITE,5));
    quad(&p,160,20,dtp,0,4);DrawPrim(&p);
    quad(&p,170,20,dtp,0,4);pcTextureClass(&p,PC_TEX_SPRITE);DrawPrim(&p);
    CHECK(textureSetAppearance(PC_TEX_SPRITE,0));
    quad(&p,180,20,dtp,0,4);pcTextureClass(&p,PC_TEX_SPRITE);DrawPrim(&p);
    CHECK(pixel(640,80)==0xff00 && pixel(680,80)==0xff0000 && pixel(720,80)==0xff00);
    one=(RECT){170,20,1,1};StoreImage(&one,&exact);CHECK((exact&65535)==31<<5);
    textureDumpSetRoot(argv[1]); CHECK(textureDumpEnabled()); textureDumpSetRoot(""); CHECK(!textureDumpEnabled());
    printf("GPU texture packs: %d checks passed\n",checks);
    gpuShutdown(); SDL_Quit(); return 0;
}
