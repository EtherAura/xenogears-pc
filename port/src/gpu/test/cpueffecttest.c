/* The actual GPU effect against a scalar colour oracle. The oracle splits
 * the captured image into N*N independent native-sized images, applies the
 * authored linear CPU kernel, then interleaves the phases again. */
#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "common.h"
#include "psyq.h"
#include "pc_effect.h"
#include "gpu/gpu_internal.h"
#include "system/settings.h"
#include "test/env.h"

int gpuInit(SDL_Window* window);
void gpuShutdown(void);
void SetPolyG3(void* p);
void hiresStateLoaded(void);
static int checks;
#include "test/check.h"
#define W 320
#define H 218
static RECT capture = {320,256,W,H};
static const VRect area = {320,256,639,473};
static uint16_t native[W*H], before[W*H];

static uint32_t average(const uint32_t* p, int i, int size, int w) {
    unsigned sum[3]={0};
    int ids[4]={i,i+1,i+w,i+w+1};
    for (int j=0;j<4;j++) for (int c=0;c<3;c++)
        if (ids[j]<size) sum[c]+=(p[ids[j]]>>(c*8+3))&31;
    uint32_t v=0;
    for (int c=0;c<3;c++) { unsigned s=sum[c]/4; v|=((s*8)+(s/4))<<(c*8); }
    return v;
}
static void oracle(uint32_t* image,int w,int n,int wide,int centre) {
    uint32_t* phase=malloc((size_t)w*H*4); if (!phase) exit(1);
    int stride=w*n;
    for (int sy=0;sy<n;sy++) for (int sx=0;sx<n;sx++) {
        for (int y=0;y<H;y++) for (int x=0;x<w;x++)
            phase[y*w+x]=image[(y*n+sy)*stride+x*n+sx];
        for (int i=0;i<w*(H-1)-(wide?0:1);i++) {
            int x=i%w,y=i/w;
            if (centre>=0 && x>=centre && x<centre+W) continue;
            image[(y*n+sy)*stride+x*n+sx]=average(phase,i,w*H,w);
        }
    }
    free(phase);
}
static uint32_t* readHi(int wide,VRect r,int n) {
    int w=r.x1-r.x0+1,h=r.y1-r.y0+1;
    uint32_t* p=malloc((size_t)w*h*n*n*4); if (!p || !gvkHiRead(wide,p,r.x0*n,r.y0*n,w*n,h*n)) exit(1); return p;
}
static void draw(int width) {
    DRAWENV e; SetDefDrawEnv(&e,0,0,width,H); PutDrawEnv(&e);
    RECT r={0,0,width,H}; ClearImage(&r,12,80,140);
    POLY_G3 p; SetPolyG3(&p);
    p.x0=13;p.y0=11;p.x1=width-9;p.y1=37;p.x2=width/3;p.y2=H-3;
    setRGB0(&p,248,8,96);setRGB1(&p,48,240,136);setRGB2(&p,104,72,248);
    DrawPrim(&p); DrawSync(0);
    RECT narrow={0,0,W,H}; MoveImage(&narrow,320,256); DrawSync(0);
    /* Processed pixels clear STP; the final pixel/row retain it exactly. */
    uint16_t masked[W*2];
    for (int i=0;i<W*2;i++) masked[i]=0xbdef;
    RECT tail={320,256+H-2,W,2}; LoadImage(&tail,masked);
}
static void nativeBlur(void) {
    memcpy(before,native,sizeof(native));
    for (int i=0;i<W*(H-1)-1;i++) {
        unsigned v=0;
        for (int c=0;c<3;c++) {
            unsigned sum=((before[i]>>(c*5))&31)+((before[i+1]>>(c*5))&31)+
                ((before[i+W]>>(c*5))&31)+((before[i+W+1]>>(c*5))&31);
            v|=(sum/4)<<(c*5);
        }
        native[i]=(uint16_t)v;
    }
    LoadImage(&capture,native);
}
int main(int argc,char** argv) {
    if (argc==3) {
        testSetEnv("XG_SETTINGS",argv[2]);
        if (!strcmp(argv[1],"write")) { hiresSetCpuEffects(1); CHECK(settingsSave()); }
        else { CHECK(!strcmp(argv[1],"read")); CHECK(hiresCpuEffects()); }
        return 0;
    }
    testSetEnv("XG_SETTINGS","none"); testSetEnv("XG_CLOCK","step");
    CHECK(SDL_Init(SDL_INIT_VIDEO)); CHECK(gpuInit(NULL)); CHECK(!hiresCpuEffects());
    hiresSetCpuEffects(1); hiresSetPrecision(0); hiresSetSeamGuard(0);
    const int scales[]={2,3,4,8,12};
    for (int s=0;s<5;s++) for (int color=0;color<4;color++) {
        int n=scales[s]; hiresSetUpscale(n); hiresSetColor(color);
        CHECK(gvkHiConfigure(n,color)==n); draw(W);
        StoreImage(&capture,(u32*)native);
        uint32_t* expected=readHi(0,area,n);
        pcHiresBattlingCapture(&capture);
        for (int pass=0;pass<6;pass++) {
            oracle(expected,W,n,0,-1); nativeBlur(); pcHiresBattlingBlur(&capture);
            uint32_t* actual=readHi(0,area,n);
            CHECK(!memcmp(expected,actual,(size_t)W*H*n*n*4)); free(actual);
            StoreImage(&capture,(u32*)before); CHECK(!memcmp(native,before,sizeof(native)));
            if (pass==0) {
                int detail=0;
                for (int y=0;y<H*n;y++) for (int x=0;x<W*n;x++) {
                    uint16_t v=native[(y/n)*W+x/n]; uint32_t replicated=0;
                    for (int c=0;c<3;c++) {
                        unsigned q=(v>>(c*5))&31; replicated|=((q*8)+(q/4))<<(c*8);
                    }
                    detail+=(expected[y*W*n+x]&0xffffff)!=replicated;
                }
                CHECK(detail>100); /* genuinely preserves subpixel detail */
            }
        }
        free(expected);
        /* Invalidated GPU sources cannot overwrite a new timeline. */
        hiresStateLoaded(); CHECK(!gvkHiEffectBlur(0,area,-1));
        draw(W); pcHiresBattlingCapture(&capture); hiresSetCpuEffects(0);
        CHECK(!gvkHiEffectBlur(0,area,-1)); hiresSetCpuEffects(1);
        printf("CPU effect %dx colour %d: six oracle passes, native VRAM exact, invalidation OK\n",n,color);
    }
    /* Wide sources survive slot relocation and retain their own recurrence.
     * A zero-shift wide centre takes the already blurred narrow source. */
    for (int shift=0;shift<3;shift++) {
        int n=4,w=shift==2 ? 896 : 560; CHECK(gvkHiConfigure(n,0)==n); draw(w);
        VRect source={700,512,700+w-1,512+H-1},dst={700,768,700+w-1,768+H-1};
        gvkCopy(0,0,0,1,source.x0,source.y0,w,H);
        uint32_t* expected=readHi(1,source,n), *narrow=readHi(0,area,n);
        CHECK(gvkHiEffectCapture(area,&source));
        for (int pass=0;pass<6;pass++) {
            oracle(narrow,W,n,0,-1); CHECK(gvkHiEffectBlur(0,area,-1));
            oracle(expected,w,n,1,-1);
            if (!shift) for (int y=0;y<H*n;y++)
                memcpy(expected+y*w*n+120*n,narrow+y*W*n,(size_t)W*n*4);
            CHECK(gvkHiEffectBlur(1,dst,shift?-1:120));
            uint32_t* actual=readHi(1,dst,n);
            CHECK(!memcmp(expected,actual,(size_t)w*H*n*n*4));free(actual);
        }
        free(expected);free(narrow);
    }
    CHECK(gvkHiConfigure(4,0)==4); draw(W); pcHiresBattlingCapture(&capture);
    presentSetExpandedVertical(1); CHECK(!gvkHiEffectBlur(0,area,-1));
    presentSetExpandedVertical(0);
    pcHiresBattlingCapture(&capture); CHECK(gvkHiConfigure(3,0)==3);
    CHECK(!gvkHiEffectBlur(0,area,-1));
    pcHiresBattlingCapture(&capture); CHECK(gvkHiConfigure(3,3)==3);
    CHECK(!gvkHiEffectBlur(0,area,-1));
    CHECK(gvkHiConfigure(1,0)==1); CHECK(!gvkHiEffectCapture(area,NULL));
    gpuShutdown(); SDL_Quit(); printf("cpueffecttest: %d checks\n",checks); return 0;
}
