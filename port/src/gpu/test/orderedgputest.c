/* Exact ordered writes versus individually serialized hardware draws. The
 * reference does not use lists, sorting or compute. Include raw mask and
 * display coverage bits in every comparison. */
#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "gpu/gpu_internal.h"
#include "gpu/ordered.h"
#include "test/env.h"
int gpuInit(SDL_Window*);void gpuShutdown(void);
static int checks,compact;
#include "test/check.h"
#define SIDE 40
#define ORIGIN 16
static const int corner[6][2]={{0,0},{1,0},{0,1},{1,0},{1,1},{0,1}};
static void rectangle(int index,int wide,int enhanced,int scene,int serialized) {
    GpuVertex v[6]={0};HiVertex hv[6]={0};
    uint32_t mode=(uint32_t)(index%4)<<3;
    if(index%3)mode|=DRAW_SEMI;
    if(index%7==1)mode|=DRAW_DITHER;
    if(index%17==5)mode|=DRAW_SET_MASK;
    if(index%5==4)mode|=DRAW_CHECK_MASK;
    if(index%11==3)mode|=DRAW_SURFACE;
    if(index%13==4)mode|=DRAW_FRINGE;
    if(index%19==2)mode|=DRAW_FILL;
    if(index%4==2)mode|=DRAW_TEXTURED|DRAW_RAW;
    if(index%4==3)mode|=DRAW_TEXTURED;
    int left=ORIGIN+(index*7)%20,top=ORIGIN+(index*11)%20;
    for(int k=0;k<6;k++) {
        float x=(float)(left+corner[k][0]*20),y=(float)(top+corner[k][1]*20);
        v[k]=(GpuVertex){.x=x,.y=y,.u=(float)(corner[k][0]*7),.v=(float)(corner[k][1]*7),
            .r=(float)(index*43%256),.g=(float)(index*19%256),.b=(float)(index*31%256),
            .mode=mode,.tex=(uint32_t)(10|2<<7|(index%4)<<5),.clipMin=ORIGIN|ORIGIN<<16,
            .clipMax=(ORIGIN+SIDE-1)|(ORIGIN+SIDE-1)<<16,.span=UINT32_MAX};
        hv[k]=(HiVertex){.x=x+.125f,.y=y+.25f,.u=v[k].u,.v=v[k].v,
            .r=v[k].r,.g=v[k].g,.b=v[k].b,.mode=mode|DRAW_PRECISE|(scene?DRAW_SCENE_RASTER:0),.tex=v[k].tex,
            .clipMin=v[k].clipMin,.clipMax=v[k].clipMax,.uvMax=7|7<<16,
            .w=1.0f+(float)corner[k][0]/2};
    }
    if(enhanced) {
        int step = serialized ? 3 : 6;
        for (int first = 0; first < 6; first += step) {
            if (wide) gvkHiDrawWide(hv + first, step); else gvkHiDraw(hv + first, step);
            if (serialized) gvkBarrier();
        }
    }
    else {if(wide)gvkDrawWide(v,6);else gvkDraw(v,6);}
}
static int run(int on,int wide,int n,int color,int raster,int serialized,uint16_t* native,uint32_t* copy) {
    gpuSetOrderingMode(on ? GPU_ORDERING_FALLBACK : GPU_ORDERING_SPLIT);
    CHECK(gvkOrderedWrites()==(on||gvkHasInterlock()));
    hiresSetUpscale(n);hiresSetColor(color);CHECK(gvkHiConfigure(n>1?n:0,color)==(n>1?n:0));
    hiresSetSceneScale(raster);
    if (raster) CHECK(hiresSceneScaleNow() == raster);
    if(wide)gvkWideTouch(ORIGIN+SIDE);
    uint16_t zero[SIDE*SIDE]={0},tex[64];
    for(int i=0;i<64;i++)tex[i]=(uint16_t)(i%5==0?0:(i*913)&0x7fff)|((i%3==0)?0x8000:0);
    gvkUpload((VRect){640,0,647,7},tex,8);
    VRect rect={ORIGIN,ORIGIN,ORIGIN+SIDE-1,ORIGIN+SIDE-1};
    if(wide)gvkUploadWide(rect,zero,SIDE);else gvkUpload(rect,zero,SIDE);
    /* Four native-sized backing pixels, arbitrary destination high bits,
     * and 173 overdraws span several ordered groups and buffer stripes. */
    if(n>1) {
        uint16_t init[SIDE*SIDE];for(int i=0;i<SIDE*SIDE;i++)init[i]=(uint16_t)(i*39)&0x7fff;
        if(wide)gvkUploadWide(rect,init,SIDE);else {gvkUpload(rect,init,SIDE);gvkHiLoad(rect,init,SIDE,HI_LOAD_REPLICATE,0,NULL,0);}
    }
    for(int i=0;i<(compact?73:173);i++) {
        rectangle(i,wide,0,0,0);
        if(n>1)rectangle(i,wide,1,raster && i%6!=1,serialized);
        if(!on)gvkBarrier();
    }
    gvkBarrier();
    if(wide)gvkReadWide(native,ORIGIN,ORIGIN,SIDE,SIDE);else gvkReadback(rect,native,SIDE);
    if(n>1)CHECK(gvkHiRead(wide,copy,ORIGIN*n,ORIGIN*n,SIDE*n,SIDE*n));
    CHECK(gvkOrderedOverflow()==0);return 0;
}
static int planner(void) {
    const uint32_t caps[]={64,103,4096,ORDERED_CAPACITY};
    for(int count=1;count<=64;count++)for(int c=0;c<4;c++) {
        OrderedBox boxes[64];
        for(int i=0;i<count;i++)boxes[i]=(OrderedBox){i%9,i%13,50+i%29,60+i%31};
        int width=(int)(caps[c]/(unsigned)count);if(width>80)width=80;
        for(int x=0;x<80;x+=width) {
            OrderedBox col={x,0,x+width<80?x+width:80,96};
            for(int y=0;y<96;) {
                int end=orderedBandEnd(boxes,count,col,y,caps[c]);CHECK(end>y&&end<=96);
                uint64_t actual=0;
                /* Independent exhaustive count of all potential triangle
                 * fragments, including overlapping boxes. */
                for(int yy=y;yy<end;yy++)for(int xx=x;xx<col.x1;xx++)for(int i=0;i<count;i++)
                    actual+=xx>=boxes[i].x0&&xx<boxes[i].x1&&yy>=boxes[i].y0&&yy<boxes[i].y1;
                CHECK(actual<=caps[c]);CHECK((uint64_t)(col.x1-x)*(end-y)<=caps[c]);y=end;
            }
        }
    }
    float pair[6][3]={{0,0,1},{20,0,1},{0,20,1},{20,0,1},{20,20,1},{0,20,1}};
    CHECK(orderedIndependent(pair,sizeof(pair[0]),3,8));
    CHECK(orderedIndependent(pair,sizeof(pair[0]),6,8));
    pair[4][0]=2;pair[4][1]=2;CHECK(!orderedIndependent(pair,sizeof(pair[0]),6,8));
    pair[4][0]=20;pair[4][1]=20;pair[3][2]=2;CHECK(!orderedIndependent(pair,sizeof(pair[0]),6,8));
    pair[3][2]=1;pair[0][0]=NAN;CHECK(!orderedIndependent(pair,sizeof(pair[0]),6,8));
    CHECK(!orderedIndependent(pair,sizeof(pair[0]),9,8));
    float p[3][2]={{-1e30f,-1e30f},{1e30f,1e30f},{0,0}};
    OrderedBox b=orderedBounds(p,sizeof(p[0]),12,24576,6144);CHECK(b.x0==0&&b.y0==0&&b.x1==24576&&b.y1==6144);
    p[0][0]=NAN;b=orderedBounds(p,sizeof(p[0]),4,100,90);CHECK(b.x0==0&&b.y0==0&&b.x1==100&&b.y1==90);
    return 0;
}
static int clipped(void) {
    hiresSetUpscale(2);CHECK(gvkHiConfigure(2,HIRES_COLOR_RAW)==2);
    uint32_t a[SIDE*SIDE*4],b[SIDE*SIDE*4];uint16_t clear[SIDE*SIDE]={0};
    VRect r={ORIGIN,ORIGIN,ORIGIN+SIDE-1,ORIGIN+SIDE-1};
    for(int on=0;on<2;on++) {
        gpuSetOrderingMode(on ? GPU_ORDERING_FALLBACK : GPU_ORDERING_SPLIT);
        gvkUpload(r,clear,SIDE);gvkHiLoad(r,clear,SIDE,HI_LOAD_REPLICATE,0,NULL,0);
        HiVertex v[9]={0};
        for(int t=0;t<3;t++)for(int k=0;k<3;k++)v[t*3+k]=(HiVertex){
            .x=(float)(ORIGIN+8+(k==1?8:0)),.y=(float)(ORIGIN+8+(k==2?8:0)),
            .r=(float)(t*70+40),.g=90,.b=144,.w=t==1&&k==2?-1:1,
            .mode=DRAW_SEMI|DRAW_PRECISE,.clipMin=ORIGIN|ORIGIN<<16,
            .clipMax=(ORIGIN+SIDE-1)|(ORIGIN+SIDE-1)<<16};
        if(on)gvkHiDraw(v,9);else for(int t=0;t<3;t++){gvkHiDraw(v+t*3,3);gvkBarrier();}
        CHECK(gvkHiRead(0,on?b:a,ORIGIN*2,ORIGIN*2,SIDE*2,SIDE*2));
    }
    CHECK(!memcmp(a,b,sizeof(a)));CHECK(!gvkOrderedOverflow());return 0;
}
static int hdrEmissivePixels(void) {
    hiresSetUpscale(2);
    CHECK(gvkHiConfigure(2, HIRES_COLOR_RAW) == 2);
    uint16_t clear[SIDE * SIDE] = {0};
    VRect rect = {ORIGIN, ORIGIN, ORIGIN + SIDE - 1, ORIGIN + SIDE - 1};
    const int at = (ORIGIN + 9) * 2;
    HiVertex draw[6] = {0};
    for (int i = 0; i < 6; i++) {
        draw[i] = (HiVertex){.x = ORIGIN + 8 + (i % 3 == 1 ? 8 : 0),
                             .y = ORIGIN + 8 + (i % 3 == 2 ? 8 : 0),
                             .r = 255, .g = 255, .b = 255, .w = 1,
                             .clipMax = 1023u | (511u << 16)};
    }
    for (int ordered = 0; ordered < 2; ordered++) {
        gpuSetOrderingMode(ordered ? GPU_ORDERING_FALLBACK : GPU_ORDERING_SPLIT);
        gvkUpload(rect, clear, SIDE);
        gvkHiLoad(rect, clear, SIDE, HI_LOAD_REPLICATE, 0, NULL, 0);
        for (int i = 0; i < 6; i++)
            draw[i].mode = DRAW_HDR_EMISSION | DRAW_SEMI | (1u << DRAW_SEMI_SHIFT);
        if (ordered) gvkHiDraw(draw, 6);
        else {
            gvkHiDraw(draw, 3);
            gvkBarrier();
            gvkHiDraw(draw + 3, 3);
        }
        uint32_t pixel = 0;
        CHECK(gvkHiRead(0, &pixel, at, at, 1, 1));
        CHECK(pixel == ((63u << 26) | 0xFFFFFFu));
        for (int i = 0; i < 6; i++) draw[i].mode = 0; /* an opaque white overlay */
        gvkHiDraw(draw, 3);
        CHECK(gvkHiRead(0, &pixel, at, at, 1, 1));
        CHECK(pixel == 0xFFFFFFu);
    }
    return 0;
}

static int msaaAcrossGroups(void) {
    gpuSetOrderingMode(GPU_ORDERING_FALLBACK);
    hiresSetUpscale(4);
    CHECK(gvkHiConfigure(4, HIRES_COLOR_RAW) == 4);
    uint16_t clear[SIDE * SIDE] = {0};
    HiVertex faces[9] = {0};
    const int xy[6][2] = {{32, 32}, {40, 32}, {32, 40}, {40, 32}, {40, 40}, {32, 40}};
    for (int i = 0; i < 6; ++i) {
        faces[i] = (HiVertex){.x = xy[i][0], .y = xy[i][1], .w = 1, .clipMin = 16u | (16u << 16),
            .clipMax = 55u | (55u << 16), .mode = DRAW_PRECISE | DRAW_SCENE_RASTER | DRAW_SURFACE};
        if (i < 3) {
            faces[i].r = 240;
            faces[i].mode |= DRAW_SET_MASK;
        } else {
            faces[i].g = 120;
            faces[i].mode |= DRAW_SEMI | DRAW_CHECK_MASK | (1u << DRAW_SEMI_SHIFT);
            faces[i + 3] = faces[i];
            faces[i + 3].x = faces[i - 3].x;
            faces[i + 3].y = faces[i - 3].y;
        }
    }
    for (int samples = 2; samples <= 4; samples *= 2) {
        if (!hiresSceneMsaaSupported(samples)) continue;
        hiresSetSceneMsaa(samples);
        CHECK(hiresSceneMsaaNow() == samples);
        for (int raster = 3; raster <= 4; ++raster) for (int wide = 0; wide <= 1; ++wide) {
            hiresSetSceneScale(raster);
            /* The narrow full-width clip places the diagonal on a scratch
             * band boundary as well as separating its two draws by a list. */
            int y = samples == 4 ? 60 : 92;
            VRect area = {16, 16 + y, 55, 55 + y};
            uint32_t pixels[2][48 * 48];
            for (int padded = 0; padded < 2; ++padded) {
                HiVertex run[(ORDERED_TRIANGLES + 2) * 3];
                memcpy(run, faces, sizeof(HiVertex) * 3);
                int last = padded ? ORDERED_TRIANGLES * 3 : 3;
                for (int i = 3; i < last; ++i) {
                    run[i] = faces[i % 3];
                    run[i].x -= 15;
                    run[i].y -= 15;
                }
                memcpy(run + last, faces + 3, sizeof(HiVertex) * 6);
                for (int i = 0; i < last + 6; ++i) {
                    run[i].y += y;
                    run[i].clipMin = 0;
                    run[i].clipMax = 1023u | (511u << 16);
                }
                if (wide) {
                    gvkUploadWide(area, clear, SIDE);
                    gvkHiDrawWide(run, last + 6);
                } else {
                    gvkHiLoad(area, clear, SIDE, HI_LOAD_REPLICATE, 0, NULL, 0);
                    gvkHiDraw(run, last + 6);
                }
                CHECK(gvkHiRead(wide, pixels[padded], 30 * 4, (30 + y) * 4, 48, 48));
            }
            CHECK(!memcmp(pixels[0], pixels[1], sizeof(pixels[0])));
            uint32_t edge = pixels[0][24 * 48 + 24];
            CHECK((edge & 0xFFFFFFu) == 0x003C78u);
            CHECK(!gvkOrderedOverflow());
        }
    }
    hiresSetSceneMsaa(0);
    hiresSetSceneScale(0);
    return 0;
}

int main(int argc,char** argv) {
    compact=argc>1&&strcmp(argv[1],"--compact")==0;
    CHECK(planner()==0);
    testSetEnv("XG_SETTINGS","none");testSetEnv("XG_VK_NO_INTERLOCK","1");
#ifndef __ANDROID__ /* from a shell on the device there is no activity for SDL to start with */
    CHECK(SDL_Init(SDL_INIT_VIDEO));
#endif
    CHECK(gpuInit(NULL));gpuVBlank();CHECK(!gvkHasInterlock());
    CHECK(gpuOrderingMode() == GPU_ORDERING_AUTO);
    GpuOrderingStatus automatic = gpuOrderingStatus();
    CHECK(automatic == GPU_ORDERING_TILE || automatic == GPU_ORDERING_TILE_AND_COMPUTE || automatic == GPU_ORDERING_COMPUTE);
    uint16_t a[SIDE*SIDE],b[SIDE*SIDE];
    const int scales[]={1,2,4,12};
    /* The backend grows the wide canvas to the touched area. A 40x40
     * comparison does not require a full WIDE_H*n image, even at 12x. */
    for(int wide=0;wide<2;wide++)for(int si=0;si<(compact?3:4);si++)for(int color=0;color<4;color++) {
        int n=scales[si];
        size_t size=(size_t)SIDE*SIDE*n*n*4;
        uint32_t* ha=malloc(size),*hb=malloc(size);CHECK(ha&&hb);
        CHECK(run(0,wide,n,color,0,0,a,ha)==0);CHECK(run(1,wide,n,color,0,0,b,hb)==0);
        CHECK(memcmp(a,b,sizeof(a))==0);
        if(n>1) {
            if(memcmp(ha,hb,size))for(size_t i=0;i<size/4;i++)if(ha[i]!=hb[i]) {fprintf(stderr,"ordered mismatch wide%d n%d colour%d at %zu: %08x/%08x\n",wide,n,color,i,ha[i],hb[i]);break;}
            CHECK(memcmp(ha,hb,size)==0);
        }
        free(ha);free(hb);
    }
    /* Compare bounded coarse lists against a triangle at a time at a 3:4
     * ratio. Alternate in full-resolution draws with masks and dither. */
    for (int wide = 0; wide < 2; ++wide) for (int color = 0; color < 4; ++color) {
        uint32_t* reference = malloc(SIDE * SIDE * 16 * sizeof(*reference));
        uint32_t* resolved = malloc(SIDE * SIDE * 16 * sizeof(*resolved));
        CHECK(reference && resolved);
        CHECK(run(1,wide,4,color,3,1,a,reference)==0);
        CHECK(run(1,wide,4,color,3,0,b,resolved)==0);
        CHECK(memcmp(a,b,sizeof(a))==0);
        CHECK(memcmp(reference,resolved,SIDE*SIDE*16*sizeof(*reference))==0);
        free(reference);
        free(resolved);
    }
    hiresSetSceneScale(0);
    CHECK(clipped()==0);
    CHECK(hdrEmissivePixels() == 0);
    CHECK(msaaAcrossGroups() == 0);
    gpuSetOrderingMode(GPU_ORDERING_SPLIT);
    CHECK(!gvkOrderedWrites());
    CHECK(gpuOrderingStatus() == GPU_ORDERING_SPLIT_ACTIVE);
    gpuSetOrderingMode(GPU_ORDERING_AUTO);
    CHECK(gvkOrderedWrites());
    CHECK(gpuOrderingStatus() == automatic);
    CHECK(!gvkOrderedOverflow());
    gpuShutdown();SDL_Quit();printf("orderedgputest: %d checks passed\n",checks);return 0;
}
