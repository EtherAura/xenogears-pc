/* Resolution-aware texture edges through actual class-tagged GPU packets.
 * Compare diagonal coverage with the geometric half plane, not a copy of
 * the shader algorithm. Original VRAM and nearest STP must stay exact. */
#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "common.h"
#include "psyq.h"
#include "pc_wide.h"
#include "gpu/texture.h"
#include "test/env.h"

int gpuInit(SDL_Window* window);
void gpuShutdown(void);
void SetTile(void* p);
static int checks;
#include "test/check.h"
#define SIDE 32
static int tp, clut;
static uint16_t palette[16], data[SIDE * SIDE / 4];
static RECT image = {640,256,SIDE/4,SIDE}, pal = {976,480,16,1};

static void load(int slope, int clear) {
    for (int y=0; y<SIDE; ++y) for (int x=0; x<SIDE; x+=4) {
        uint16_t word=0;
        for (int i=0; i<4; ++i) {
            int on = slope ? x+i+slope*y < SIDE*(slope+1)/2 : x+i==16 || y==16;
            word |= (on ? 1 : 0) << (i*4);
        }
        data[y*(SIDE/4)+x/4]=word;
    }
    palette[0]=clear ? 0 : 0x8000; palette[1]=clear ? 0x801f : 0x7fff;
    LoadImage(&image,data); LoadImage(&pal,palette);
}
static void draw(int kind, int u, int w, int h) {
    TILE bg; SetTile(&bg); setRGB0(&bg,0,0,255);
    bg.x0=bg.y0=20; bg.w=bg.h=SIDE; DrawPrim(&bg);
    SPRT p; SetSprt(&p); SetShadeTex(&p,1);
    p.x0=p.y0=20; p.u0=u; p.v0=0; p.w=w; p.h=h; p.clut=clut;
    pcTextureClass(&p,kind); DrawPrim(&p);
}
static uint32_t* readCopy(int n) {
    uint32_t* pixels=malloc((size_t)SIDE*SIDE*n*n*4);
    if (!pixels || !gvkHiRead(0,pixels,20*n,20*n,SIDE*n,SIDE*n)) {
        fprintf(stderr,"edge readback failed at %dx\n",n); free(pixels); exit(1);
    }
    return pixels;
}
static double error(const uint32_t* p, int n, int slope) {
    int side=SIDE*n; double sum=0;
    /* Leave patch-clamped endpoints out of the geometric half-plane test. */
    for (int y=2*n; y<side-2*n; ++y) for (int x=2*n; x<side-2*n; ++x) {
        int covered=0;
        for (int j=0;j<8;++j) for (int i=0;i<8;++i)
            covered += ((double)x+(i+0.5)/8 + slope*((double)y+(j+0.5)/8)) <
                       (SIDE*(slope+1)/2 + slope/2.0)*n;
        sum += fabs((double)(p[y*side+x]&255)/255 - covered/64.0);
    }
    return sum;
}
static int save(const char* dir, const char* name, const uint32_t* pixels, int n) {
    if (!dir) return 1;
    int side=SIDE*n; SDL_Surface* s=SDL_CreateSurface(side,side,SDL_PIXELFORMAT_RGBA32);
    if (!s) return 0;
    for (int y=0;y<side;++y) for (int x=0;x<side;++x) {
        uint32_t p=pixels[y*side+x]; uint8_t* dst=(uint8_t*)s->pixels+y*s->pitch+x*4;
        dst[0]=p; dst[1]=p>>8; dst[2]=p>>16; dst[3]=255;
    }
    char path[1024]; snprintf(path,sizeof(path),"%s/%s-%dx.png",dir,name,n);
    int ok=SDL_SavePNG(s,path); SDL_DestroySurface(s); return ok;
}
int main(int argc, char** argv) {
    testSetEnv("XG_SETTINGS","none"); CHECK(SDL_Init(SDL_INIT_VIDEO)); CHECK(gpuInit(NULL));
    DRAWENV env; SetDefDrawEnv(&env,0,0,320,224);
    tp=GetTPage(0,0,640,256); clut=GetClut(976,480); env.tpage=tp; PutDrawEnv(&env);
    hiresSetPrecision(0); hiresSetSeamGuard(0); hiresSetColor(HIRES_COLOR_RAW);
    const int scales[]={2,3,4,5,6,7,8,9,10,11,12};
    for (int s=0;s<(int)(sizeof(scales)/sizeof(scales[0]));++s) {
        int n=scales[s], count=SIDE*SIDE*n*n;
        hiresSetUpscale(n); CHECK(gvkHiConfigure(n,HIRES_COLOR_RAW)==n);
        for (int slope=1;slope<=2;++slope) {
            load(slope,0);
            textureSetFilter(PC_TEX_SPRITE,TEXTURE_FILTER_NEAREST); draw(PC_TEX_SPRITE,0,SIDE,SIDE);
            uint32_t* nearest=readCopy(n);
            uint16_t exact[SIDE*SIDE], after[SIDE*SIDE]; RECT r={20,20,SIDE,SIDE};
            StoreImage(&r,(u32*)exact);
            textureSetFilter(PC_TEX_SPRITE,TEXTURE_FILTER_EDGE); draw(PC_TEX_SPRITE,0,SIDE,SIDE);
            uint32_t* edge=readCopy(n); StoreImage(&r,(u32*)after);
            CHECK(!memcmp(exact,after,sizeof(exact)));
            int changed=0, mixed=0, maskErrors=0;
            for (int i=0;i<count;++i) {
                changed += (edge[i]&0xffffff)!=(nearest[i]&0xffffff);
                mixed += (edge[i]&255)>0 && (edge[i]&255)<255;
                maskErrors += ((edge[i]^nearest[i])&0x1000000)!=0;
            }
            double base=error(nearest,n,slope), filtered=error(edge,n,slope);
            printf("edge %dx slope %d: geometric error %.3f -> %.3f, changed %d, mixed %d, mask errors %d\n",
                   n,slope,base,filtered,changed,mixed,maskErrors);
            CHECK(changed>0 && (n==2 ? mixed==0 : mixed>0) && maskErrors==0);
            CHECK(n==2 || filtered < base*0.55); /* 2x retains its binary corner rule */
            if (slope==1 && (n==4 || n==12)) {
                CHECK(save(argc>1 ? argv[1] : NULL,"nearest",nearest,n));
                CHECK(save(argc>1 ? argv[1] : NULL,"edge",edge,n));
            }
            if (n==4 && slope==1) {
                /* All four classes use the same edge policy when selected. */
                for (int kind=0;kind<PC_TEX_CLASSES;++kind) {
                    textureSetFilter(kind,TEXTURE_FILTER_EDGE); draw(kind,0,SIDE,SIDE);
                    uint32_t* classified=readCopy(n);
                    CHECK(!memcmp(edge,classified,(size_t)count*4)); free(classified);
                }
            }
            free(nearest); free(edge);
        }
        /* Thin horizontal/vertical lines stay intact. */
        load(0,0); textureSetFilter(PC_TEX_SPRITE,0); draw(PC_TEX_SPRITE,0,SIDE,SIDE);
        uint32_t* nearest=readCopy(n);
        textureSetFilter(PC_TEX_SPRITE,2); draw(PC_TEX_SPRITE,0,SIDE,SIDE);
        uint32_t* edge=readCopy(n);
        CHECK(n==2 || !memcmp(nearest,edge,(size_t)count*4)); /* legacy Scale2x cuts the cross junction */
        free(nearest); free(edge);
        /* Transparent neighbours never darken red or tint the blue backdrop. */
        load(1,1); textureSetFilter(PC_TEX_SPRITE,0); draw(PC_TEX_SPRITE,0,SIDE,SIDE);
        nearest=readCopy(n); textureSetFilter(PC_TEX_SPRITE,2); draw(PC_TEX_SPRITE,0,SIDE,SIDE);
        edge=readCopy(n); int red=0, blue=0, bad=0, maskErrors=0;
        for (int i=0;i<count;++i) {
            uint32_t rgb=edge[i]&0xffffff;
            red += rgb==255; blue += rgb==0xff0000;
            bad += rgb!=255 && rgb!=0xff0000;
            if (rgb==255) maskErrors += ((edge[i]^nearest[i])&0x1000000)!=0;
        }
        printf("edge %dx transparency: red %d, blue %d, bad %d, mask errors %d\n",n,red,blue,bad,maskErrors);
        CHECK(red>0 && blue>0 && !bad && !maskErrors); free(nearest); free(edge);
        /* Live palette change; the cached texels must not retain red. */
        palette[1]=0x83e0; LoadImage(&pal,palette); draw(PC_TEX_SPRITE,0,SIDE,SIDE);
        edge=readCopy(n); bad=0;
        for (int i=0;i<count;++i) bad += (edge[i]&0xffffff)!=0xff00 && (edge[i]&0xffffff)!=0xff0000;
        CHECK(!bad); free(edge);
        /* A one-texel-wide patch cannot tap its blue atlas neighbours. */
        for (int i=0;i<SIDE*SIDE/4;++i) data[i]=0x2221;
        palette[1]=31; palette[2]=31<<10; LoadImage(&image,data); LoadImage(&pal,palette);
        draw(PC_TEX_SPRITE,0,1,SIDE); edge=readCopy(n); bad=0;
        for (int y=0;y<SIDE*n;++y) for (int x=0;x<n;++x) bad += (edge[y*SIDE*n+x]&0xffffff)!=255;
        CHECK(!bad); free(edge);
    }
    printf("GPU texture edges: %d checks passed\n",checks);
    gpuShutdown(); SDL_Quit(); return 0;
}
