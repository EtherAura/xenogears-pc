/* Authenticated authored UI, independent choices, palettes and queued images. */
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "common.h"
#include "psyq.h"
#include "pc_wide.h"
#include "test/env.h"
#include "gpu/texture.h"
#include "gpu/interpolation.h"
#include "pc_interpolation.h"
int gpuInit(SDL_Window* window);
void gpuShutdown(void);
static int checks;
#include "test/check.h"
static int png(const char* path,int w,int h,int index) {
    SDL_Surface* s=SDL_CreateSurface(w,h,SDL_PIXELFORMAT_RGBA32);if(!s)return 0;
    for(int y=0;y<h;y++)for(int x=0;x<w;x++) {
        uint8_t* p=(uint8_t*)s->pixels+(size_t)y*s->pitch+x*4;
        p[0]=p[1]=p[2]=index!=1 && ((x+y)&1)?1:index;p[3]=255;
    }
    int ok=SDL_SavePNG(s,path);SDL_DestroySurface(s);return ok;
}
static int fixtures(const char* root,int scale) {
    char dir[3400],path[3500];snprintf(dir,sizeof(dir),"%s/cel-%dx/%s",root,scale,texturePackVersion());
    if(!SDL_CreateDirectory(dir))return 0;
    snprintf(path,sizeof(path),"%s/sprites.txt",dir);SDL_IOStream* f=SDL_IOFromFile(path,"wb");if(!f)return 0;
    const char text[]="4 2560 256 4 4 test\n4 2560 256 4 4 ../../invalid\n";
    int ok=SDL_WriteIO(f,text,sizeof(text)-1)==sizeof(text)-1;ok=SDL_CloseIO(f)&&ok;
    snprintf(path,sizeof(path),"%s/test.native.png",dir);ok=png(path,4,4,1)&&ok;
    snprintf(path,sizeof(path),"%s/test.indices.png",dir);return png(path,4*scale,4*scale,scale==2?2:3)&&ok;
}
static uint32_t pixel(int x,int y){uint32_t p=0;gvkHiRead(0,&p,x,y,1,1);return p&0x1ffffff;}
static uint32_t colour(uint16_t word) {
    uint32_t out=word&0x8000?0x1000000:0;
    for(int k=0;k<3;k++){unsigned c=(word>>(k*5))&31;out|=((c<<3)|(c>>2))<<(k*8);}
    return out;
}
static void sprite(int x,int y,int clut,int kind,int h) {
    SPRT p;SetSprt(&p);SetShadeTex(&p,1);p.x0=x;p.y0=y;p.u0=p.v0=0;p.w=4;p.h=h;p.clut=clut;
    pcTextureClass(&p,kind);DrawPrim(&p);
}
static void quad(int x,int tp,int clut,int kind) {
    POLY_FT4 p;SetPolyFT4(&p);SetShadeTex(&p,1);
    p.x0=p.x2=x;p.x1=p.x3=x+4;p.y0=p.y1=20;p.y2=p.y3=24;
    p.u0=p.u2=p.v0=p.v1=0;p.u1=p.u3=p.v2=p.v3=3;p.tpage=tp;p.clut=clut;
    pcTextureClass(&p,kind);DrawPrim(&p);
}
/* A filled history icon uses a guarded Cel index map, not the Enhanced
 * text map. Both halves of the quad must sample the same complete canvas. */
static int historyCanvas(const char* root,int depth) {
    enum { W=11,H=12,GW=13,GH=14 };
    uint16_t native[8*GH];int ppw=16/depth,nw=16/ppw;
    for(int i=0;i<nw*GH;i++)native[i]=depth==4?0x1111:0x0101;
    RECT tile={704,320,(short)nw,GH};LoadImage(&tile,native);
    int tp=GetTPage(depth==4?0:1,0,704,256),clut=GetClut(976,480);
    for(int artScale=2;artScale<=4;artScale+=2) {
        char dir[3400],path[3500];snprintf(dir,sizeof(dir),"%s/cel-%dx/%s",root,artScale,texturePackVersion());
        snprintf(path,sizeof(path),"%s/sprites.txt",dir);SDL_IOStream* f=SDL_IOFromFile(path,"ab");CHECK(f);
        char row[100];int length=snprintf(row,sizeof(row),"%d %d 320 13 14 history%d\n",depth,704*ppw+3,depth);CHECK(SDL_WriteIO(f,row,(size_t)length)==(size_t)length);CHECK(SDL_CloseIO(f));
        snprintf(path,sizeof(path),"%s/history%d.native.png",dir,depth);CHECK(png(path,GW,GH,1));
        SDL_Surface* image=SDL_CreateSurface(GW*artScale,GH*artScale,SDL_PIXELFORMAT_RGBA32);CHECK(image);
        for(int y=0;y<image->h;y++)for(int x=0;x<image->w;x++) {
            int index=1;
            if(x>=artScale && x<(W+1)*artScale && y>=artScale && y<(H+1)*artScale) {
                float xx=(x+.5f-artScale)/(W*artScale),yy=(y+.5f-artScale)/(H*artScale);
                float radius=(xx-.5f)*(xx-.5f)*4+(yy-.5f)*(yy-.5f)*4;
                index=radius>.95f?0:radius>.55f||xx>yy?2:3;
            }
            uint8_t* p=(uint8_t*)image->pixels+(size_t)y*image->pitch+x*4;
            p[0]=p[1]=p[2]=(uint8_t)index;p[3]=255;
        }
        snprintf(path,sizeof(path),"%s/history%d.indices.png",dir,depth);CHECK(SDL_SavePNG(image,path));SDL_DestroySurface(image);
    }
    textureCelReload();
    for(int artScale=2;artScale<=4;artScale+=2)for(int scale=4;scale<=12;scale+=4)for(int flip=0;flip<4;flip++) {
        CHECK(textureSetAppearance(PC_TEX_ICON,artScale==2?7:8));CHECK(gvkHiConfigure(scale,HIRES_COLOR_RAW)==scale);
        RECT area={20,76,60,H};ClearImage(&area,0,0,0);
        DRAWENV env;SetDefDrawEnv(&env,0,0,320,224);env.tpage=tp;PutDrawEnv(&env);
        SPRT a;SetSprt(&a);SetShadeTex(&a,1);a.x0=20;a.y0=76;a.u0=4;a.v0=65;a.w=W;a.h=H;a.clut=clut;pcTextureClass(&a,PC_TEX_ICON);DrawPrim(&a);
        POLY_FT4 q;SetPolyFT4(&q);SetShadeTex(&q,1);
        q.x0=q.x2=50+(flip&1?W:0);q.x1=q.x3=50+(flip&1?0:W);
        q.y0=q.y1=76+(flip&2?H:0);q.y2=q.y3=76+(flip&2?0:H);
        q.u0=q.u2=4-(flip&1?1:0);q.u1=q.u3=q.u0+W;
        q.v0=q.v1=65-(flip&2?1:0);q.v2=q.v3=q.v0+H;q.tpage=tp;q.clut=clut;
        pcTextureClass(&q,PC_TEX_ICON);DrawPrim(&q);
        uint32_t expected[W*H*144],actual[W*H*144];
        CHECK(gvkHiRead(0,expected,20*scale,76*scale,W*scale,H*scale));CHECK(gvkHiRead(0,actual,50*scale,76*scale,W*scale,H*scale));
        int same=1,n=W*scale;
        for(int y=0;y<H*scale;y++)for(int x=0;x<n;x++) {
            int xx=flip&1?n-1-x:x,yy=flip&2?H*scale-1-y:y;
            uint32_t got=actual[y*n+x]&0x1ffffff;if(got==(expected[yy*n+xx]&0x1ffffff))continue;
            int near=0;
            for(int dy=-1;dy<=1;dy++)for(int dx=-1;dx<=1;dx++) {
                int nx=xx+dx,ny=yy+dy;
                if(nx>=0 && nx<n && ny>=0 && ny<H*scale && got==(expected[ny*n+nx]&0x1ffffff))near=1;
            }
            if(!near)same=0;
        }
        if(!same)printf("history canvas differs: art=%dx display=%dx flip=%d\n",artScale,scale,flip);
        CHECK(same);
    }
    return 0;
}
/* Unmatched Cel graphics are the Original appearance, pixel for pixel: the
 * native texels, live CLUT and STP, texture window and 8-bit wrap, through
 * every raster path (sprites, magnified, minified, rotated, mirrored,
 * Gouraud and blended faces), at several scales and colour modes. None of
 * these textures has Cel artwork. */
enum { BX=0, BY=112, BW=176, BH=96 };
static int tp4,tp8,tp15,clut4,clut8;
static void batteryTextures(void) {
    static uint16_t t4[64*16],t8[32*32],t15[16*16],c4[16],c8[256];
    for(int y=0;y<16;y++)for(int x=0;x<256;x++) {
        unsigned index=(x*7+y*13+(x*y)%5)&15;
        t4[y*64+x/4]=(uint16_t)((t4[y*64+x/4]&~(15u<<(x%4*4)))|index<<(x%4*4));
    }
    for(int i=0;i<64*32;i++)((uint8_t*)t8)[i]=(uint8_t)(i*29+i/64*3);
    for(int i=0;i<256;i++)t15[i]=(uint16_t)(i%9==0?0:((i*1234+i/16*4321)&0x7fff)|(i%4==0?0x8000:0));
    const uint16_t pal4[16]={0,0x8000,31,31|0x8000,31<<5,(31<<5)|0x8000,31<<10,0x7fff,0xffff,0x4210,0x8421,0x1234,0x9876,0x0421,0x7c1f,0x83e0};
    memcpy(c4,pal4,sizeof(c4));
    for(int i=0;i<256;i++)c8[i]=(uint16_t)(i?((i*37)&0x7fff)|(i%3==0?0x8000:0):0);
    RECT r4={704,256,64,16},r8={768,256,32,32},r15={832,256,16,16},p4={976,481,16,1},p8={768,483,256,1};
    LoadImage(&r4,t4);LoadImage(&r8,t8);LoadImage(&r15,t15);LoadImage(&p4,c4);LoadImage(&p8,c8);
    tp4=GetTPage(0,0,704,256);tp8=GetTPage(1,0,768,256);tp15=GetTPage(2,0,832,256);
    clut4=GetClut(976,481);clut8=GetClut(768,483);
}
static void batterySprite(int kind,int tpage,int x,int y,int u,int v,int w,int h,int clut,int shade,int semi) {
    DRAWENV env;SetDefDrawEnv(&env,0,0,320,224);env.tpage=tpage;PutDrawEnv(&env);
    SPRT s;SetSprt(&s);SetShadeTex(&s,shade);SetSemiTrans(&s,semi);setRGB0(&s,0x60,0x90,0xc0);
    s.x0=x;s.y0=y;s.u0=u;s.v0=v;s.w=w;s.h=h;s.clut=clut;pcTextureClass(&s,kind);DrawPrim(&s);
}
static void batteryQuad(int kind,int tpage,int clut,const int xy[8],const int uv[8],int semi,int gouraud) {
    POLY_GT4 g;POLY_FT4 f;
    if(gouraud) {
        SetPolyGT4(&g);setRGB0(&g,0x20,0x80,0xf0);setRGB1(&g,0xf0,0x40,0x80);setRGB2(&g,0x80,0xf0,0x10);setRGB3(&g,0x50,0x50,0x50);
        g.x0=xy[0];g.y0=xy[1];g.x1=xy[2];g.y1=xy[3];g.x2=xy[4];g.y2=xy[5];g.x3=xy[6];g.y3=xy[7];
        g.u0=uv[0];g.v0=uv[1];g.u1=uv[2];g.v1=uv[3];g.u2=uv[4];g.v2=uv[5];g.u3=uv[6];g.v3=uv[7];
        g.tpage=tpage;g.clut=clut;SetSemiTrans(&g,semi);pcTextureClass(&g,kind);DrawPrim(&g);return;
    }
    SetPolyFT4(&f);SetShadeTex(&f,1);
    f.x0=xy[0];f.y0=xy[1];f.x1=xy[2];f.y1=xy[3];f.x2=xy[4];f.y2=xy[5];f.x3=xy[6];f.y3=xy[7];
    f.u0=uv[0];f.v0=uv[1];f.u1=uv[2];f.v1=uv[3];f.u2=uv[4];f.v2=uv[5];f.u3=uv[6];f.v3=uv[7];
    f.tpage=tpage;f.clut=clut;SetSemiTrans(&f,semi);pcTextureClass(&f,kind);DrawPrim(&f);
}
static void battery(int kind,int clear) {
    /* A snapshot must not hold a partial clear: replay draws on its own. */
    RECT area={BX,BY,BW,BH};if(clear)ClearImage(&area,40,80,120);
    batterySprite(kind,tp4,4,114,3,1,12,12,clut4,1,0);
    batterySprite(kind,tp4,18,114,3,1,12,12,clut4,0,0);
    batterySprite(kind,tp4,32,114,250,2,12,8,clut4,1,0); /* u wraps 255 -> 0 */
    batterySprite(kind,tp8,46,114,5,3,16,12,clut8,1,0);
    batterySprite(kind,tp15,82,114,2,1,12,12,0,1,0);
    batterySprite(kind,GetTPage(0,1,704,256),96,114,7,4,14,10,clut4,1,1);
    { /* texture window: an 8x8 tile at u 8, v 8 */
        DRAWENV env;SetDefDrawEnv(&env,0,0,320,224);env.tpage=tp8;env.tw=(RECT){8,8,8,8};PutDrawEnv(&env);
        SPRT s;SetSprt(&s);SetShadeTex(&s,1);s.x0=64;s.y0=114;s.u0=0;s.v0=0;s.w=16;s.h=16;s.clut=clut8;
        pcTextureClass(&s,kind);DrawPrim(&s);
        SetDefDrawEnv(&env,0,0,320,224);PutDrawEnv(&env);
    }
    const int magnify[8]={4,132,21,132,4,145,21,145},magnifyUv[8]={0,0,5,0,0,5,5,5};
    const int minify[8]={24,132,31,132,24,141,31,141},minifyUv[8]={0,0,15,0,0,15,15,15};
    const int rotated[8]={40,140,56,132,48,156,64,148},rotatedUv[8]={0,0,15,0,0,15,15,15};
    const int shaded[8]={70,132,90,132,70,150,90,150},shadedUv[8]={2,1,30,1,2,20,30,20};
    const int mirrored[8]={114,132,96,132,114,147,96,147},mirroredUv[8]={0,0,15,0,0,15,15,15};
    const int deep[8]={120,132,150,132,120,162,150,162},deepUv[8]={0,0,7,0,0,7,7,7};
    batteryQuad(kind,tp4,clut4,magnify,magnifyUv,0,0);
    batteryQuad(kind,tp4,clut4,minify,minifyUv,0,0);
    batteryQuad(kind,tp8,clut8,rotated,rotatedUv,0,0);
    batteryQuad(kind,tp8,clut8,shaded,shadedUv,0,1);
    batteryQuad(kind,tp4,clut4,mirrored,mirroredUv,0,0);
    batteryQuad(kind,tp15,0,deep,deepUv,0,0);
    for(int abr=0;abr<4;abr++) {
        const int xy[8]={4+abr*22,160,24+abr*22,160,4+abr*22,180,24+abr*22,180},uv[8]={0,0,31,0,0,19,31,19};
        batteryQuad(kind,GetTPage(1,abr,768,256),clut8,xy,uv,1,abr&1);
    }
}
static int batteryRead(uint32_t* out,int n,int replay) {
    return replay?gvkInterpolationRead(out,BX*n,BY*n,BW*n,BH*n):gvkHiRead(0,out,BX*n,BY*n,BW*n,BH*n);
}
static int differences(const uint32_t* a,const uint32_t* b,size_t count) {
    int different=0;for(size_t i=0;i<count;i++)different+=a[i]!=b[i];return different;
}
static void replayFrame(int kind,int shift) {
    DRAWENV env;SetDefDrawEnv(&env,0,0,320,224);PutDrawEnv(&env);
    pcInterpolationFrameBegin(8,0,0,320,224);
    RECT r={0,0,320,224};ClearImage(&r,0,0,0);
    static POLY_F4 model;static int owner;SetPolyF4(&model);setRGB0(&model,0,248,0);
    model.x0=model.x2=200+shift;model.x1=model.x3=216+shift;model.y0=model.y1=150;model.y2=model.y3=166;
    pcInterpolationObject(&owner,&model);pcInterpolationModel(&model);pcInterpolationPrim(&model);
    DrawPrim(&model);pcInterpolationObject(NULL,NULL);
    battery(kind,0);
    pcInterpolationFrameEnd();interpolationPacketsDone();
}
/* The intermediate picture between two snapshots of an unchanged battery. */
static int replayBattery(int kind,uint32_t* out,int n) {
    static uint32_t stamp=60000;GpuDisplay display={0,0,320,224,0,1,0,0,0,0,0};
    interpolationSetEnabled(1);
    replayFrame(kind,0);replayFrame(kind,0);gvkPresent(&display,stamp);
    replayFrame(kind,4);gvkPresent(&display,stamp+2);
    int a,b;double alpha;int ok=interpolationPicture(&display,stamp+3,&a,&b,&alpha) && alpha==0.5;
    gvkPresent(&display,stamp+3);ok=ok && batteryRead(out,n,1);
    stamp+=100;interpolationSetEnabled(0);return ok;
}
static int unmatchedCel(void) {
    batteryTextures();
    const int kinds[]={PC_TEX_HUD,PC_TEX_BATTLE_HUD,PC_TEX_ICON};
    const int colours[]={HIRES_COLOR_RAW,HIRES_COLOR_TRUE,HIRES_COLOR_DITHER_HI};
    size_t most=(size_t)BW*BH*16;uint32_t* original=malloc(most*4),*cel=malloc(most*4);CHECK(original && cel);
    for(int n=2;n<=4;n++)for(int c=0;c<3;c++) {
        hiresSetUpscale(n);hiresSetColor(colours[c]);CHECK(gvkHiConfigure(n,colours[c])==n);
        size_t count=(size_t)BW*BH*n*n;
        for(int k=0;k<3;k++) {
            CHECK(textureSetAppearance(kinds[k],0));battery(kinds[k],1);CHECK(batteryRead(original,n,0));
            for(int mode=7;mode<=8;mode++) {
                CHECK(textureSetAppearance(kinds[k],mode));battery(kinds[k],1);CHECK(batteryRead(cel,n,0));
                int different=differences(original,cel,count);
                if(different)fprintf(stderr,"Cel unmatched direct: scale %d colour %d kind %d mode %d: %d pixels differ\n",
                                     n,colours[c],kinds[k],mode,different);
                CHECK(!different);
            }
        }
    }
    /* Replayed (interpolated) pictures own their samples; unmatched Cel
     * graphics replay exactly as Original ones. */
    hiresSetUpscale(4);hiresSetColor(HIRES_COLOR_RAW);CHECK(gvkHiConfigure(4,HIRES_COLOR_RAW)==4);
    size_t count=(size_t)BW*BH*16;
    for(int k=0;k<3;k++) {
        CHECK(textureSetAppearance(kinds[k],0));CHECK(replayBattery(kinds[k],original,4));
        for(int mode=7;mode<=8;mode++) {
            CHECK(textureSetAppearance(kinds[k],mode));CHECK(replayBattery(kinds[k],cel,4));
            int different=differences(original,cel,count);
            if(different)fprintf(stderr,"Cel unmatched replay: kind %d mode %d: %d pixels differ\n",kinds[k],mode,different);
            CHECK(!different);
        }
    }
    free(original);free(cel);
    return 0;
}
int main(int argc,char** argv) {
    SDL_setenv_unsafe("XG_SPEEDUP","1",1); /* replayed pictures */
    CHECK(argc>=3);CHECK(testSetEnv("XG_SETTINGS",argv[2])==0);
    CHECK(testSetEnv("XG_DUMP_PRESENT",argv[1])==0);CHECK(testSetEnv("XG_DUMP_RANGE","60000-61000")==0);
    CHECK(testSetEnv("XG_ASPECT","1:1")==0);CHECK(testSetEnv("XG_SCALE","fit")==0);CHECK(SDL_Init(SDL_INIT_VIDEO));
    if(argc>3) {
        CHECK(textureCelScale(PC_TEX_HUD)==2 && textureCelScale(PC_TEX_BATTLE_HUD)==4 && textureCelScale(PC_TEX_ICON)==4);
        CHECK(!textureCelScale(PC_TEX_TEXT) && !strcmp(textureCelRoot(),argv[1]));
        SDL_Quit();puts("cel settings reloaded");return 0;
    }
    CHECK(!textureCelScale(PC_TEX_HUD) && !textureCelScale(PC_TEX_BATTLE_HUD) && !textureCelScale(PC_TEX_ICON));
    CHECK(fixtures(argv[1],2) && fixtures(argv[1],4));
    SDL_Window* window=SDL_CreateWindow("celgputest",320,224,SDL_WINDOW_HIDDEN);CHECK(window && gpuInit(window));
    interpolationSetEnabled(0);
    DRAWENV env;SetDefDrawEnv(&env,0,0,320,224);PutDrawEnv(&env);
    hiresSetUpscale(4);hiresSetPrecision(0);hiresSetSeamGuard(0);hiresSetColor(HIRES_COLOR_RAW);
    CHECK(gvkHiConfigure(4,HIRES_COLOR_RAW)==4);textureCelSetRoot(argv[1]);
    RECT image={640,256,1,4},pal={976,480,16,1};uint16_t data[4]={0x1111,0x1111,0x1111,0x1111},colors[16]={0,31,0xfc00,31<<5};
    LoadImage(&image,data);LoadImage(&pal,colors);int tp=GetTPage(0,0,640,256),clut=GetClut(976,480);
    CHECK(textureSetAppearance(PC_TEX_HUD,7));CHECK(textureSetAppearance(PC_TEX_BATTLE_HUD,8));
    quad(20,tp,clut,PC_TEX_HUD);quad(30,tp,clut,PC_TEX_BATTLE_HUD);quad(40,tp,clut,PC_TEX_ICON);
    textureCelReload(); /* submitted pixels must retain the previous artwork */
    CHECK(pixel(80,80)==0x1ff0000 && pixel(120,80)==0xff00 && pixel(160,80)==255);
    CHECK(textureSetAppearance(PC_TEX_ICON,7));quad(50,tp,clut,PC_TEX_ICON);CHECK(pixel(200,80)==0x1ff0000);
    colors[2]=0x83e0;LoadImage(&pal,colors);quad(60,tp,clut,PC_TEX_HUD);
    CHECK(pixel(240,80)==0x100ff00 && pixel(80,80)==0x1ff0000);
    uint32_t native=0;RECT one={20,20,1,1};StoreImage(&one,&native);CHECK((native&65535)==31);
    /* Authored cel texels are discrete even at their highest-frequency
     * boundary. Unmatched tall HUD graphics also retain exact native blocks. */
    env.tpage=tp;PutDrawEnv(&env);uint32_t pixels[4*12*16];
    for(int scale=2;scale<=4;scale+=2) {
        CHECK(textureSetAppearance(PC_TEX_HUD,scale==2?7:8));
        CHECK(textureFilter(PC_TEX_HUD)==TEXTURE_FILTER_NEAREST);
        sprite(80,40,clut,PC_TEX_HUD,4);CHECK(gvkHiRead(0,pixels,320,160,16,16));
        int exact=1;
        for(int y=0;y<16;y++)for(int x=0;x<16;x++) {
            int i=((x*scale/4+y*scale/4)&1)?1:scale==2?2:3;
            if((pixels[y*16+x]&0x1ffffff)!=colour(colors[i]))exact=0;
        }
        CHECK(exact);
    }
    uint16_t tall[12];for(int i=0;i<12;i++)tall[i]=0x1111;tall[0]=0x1121;tall[1]=0x1112;
    RECT tallRect=image;tallRect.h=12;LoadImage(&tallRect,tall);
    const int kinds[]={PC_TEX_HUD,PC_TEX_BATTLE_HUD,PC_TEX_ICON};
    for(int k=0;k<3;k++)for(int mode=7;mode<=8;mode++) {
        CHECK(textureSetAppearance(kinds[k],mode));sprite(100,40,clut,kinds[k],12);
        CHECK(gvkHiRead(0,pixels,400,160,16,48));int exact=1;
        for(int y=0;y<48;y++)for(int x=0;x<16;x++) {
            int index=(tall[y/4]>>((x/4)*4))&15;
            if((pixels[y*16+x]&0x1ffffff)!=colour(colors[index]))exact=0;
        }
        CHECK(exact);
    }
    CHECK(textureSetAppearance(PC_TEX_HUD,7));
    /* The same address with changed source pixels cannot inherit its art. */
    data[0]=0x1121;LoadImage(&image,data);TextureRequest request={tp,clut,0,0,0,4,4,0,0};TextureSample sample;
    CHECK(!textureCelSample(textureCacheGet(request),request,2,&sample));
    quad(70,tp,clut,PC_TEX_HUD);CHECK(pixel(280,80)==255);
    CHECK(!textureSetAppearance(PC_TEX_TEXT,7) && !textureSetAppearance(PC_TEX_3D,8));
    if(historyCanvas(argv[1],4) || historyCanvas(argv[1],8))return 1;
    if(unmatchedCel())return 1;
    /* The reload pass reads these choices back. */
    CHECK(textureSetAppearance(PC_TEX_HUD,7) && textureSetAppearance(PC_TEX_BATTLE_HUD,8));
    CHECK(textureSetAppearance(PC_TEX_ICON,8));
    printf("Cel HUD: %d checks passed\n",checks);gpuShutdown();SDL_DestroyWindow(window);SDL_Quit();return 0;
}
