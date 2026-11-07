/* Actual field transition strip/grid renderers, with a captured striped
 * image. Replay must own that image and match the authored halfway pose. */
#include "common.h"
#include "psyq.h"
#include "field/field.h"
#include "gpu/gpu_internal.h"
#include "gpu/interpolation.h"
#include "pc_interpolation.h"
#include "gte_pc.h"
#include "test/env.h"
#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
s16 rcossin_tbl[8192];

int gpuInit(SDL_Window*); void gpuShutdown(void);
void setupScreenTransitionPoly(s32,s32); void renderScreenTransitionPoly(void);
void initTransitionFadeGrid(void); void renderTransitionFadeGrid(void); void freeTransitionFadeGrid(void);
void SetTile(void*);
void SetPolyFT4(void*);
void SetDrawMode(DR_MODE*,s32,s32,s32,RECT*);
typedef struct { DR_MODE modes[5][2]; RECT windows[5][2]; POLY_FT4 polys[5][2]; SVECTOR vertices[5][4]; } Strips;
Strips fieldTransitionStrips;
SVECTOR transitionStripRotation;
s32 transitionStripScale, transitionFadeGridRadius, g_frameOddOrEven, fieldMapNumber;
void* transitionFadeGrid;
DR_MODE gDialogDrModes[2][5];
static sFieldRenderContext context;
sFieldRenderContext* pCurrentFieldRenderingContext = &context;
void* allocateHeap(s32 n,s32 f) { (void)f; return calloc(1,(size_t)n); }
s32 freeHeap(void* p) { free(p); return 0; }
static int checks;
#include "test/check.h"
static GpuDisplay display = {0,0,320,224,0,1,0,0,0,0,0};
static int explicitPresentation;
static void frame(int buffer) {
    g_frameOddOrEven = buffer; display.y = buffer*256;
    DISPENV de; SetDefDispEnv(&de,0,buffer*256,320,224); de.screen.y=10; de.screen.h=216;
    PutDispEnv(&de);
    /* Reported-mode presents use the explicit test timestamps below. Do not
     * activate an older alternating-buffer snapshot at the GPU's unrelated
     * housekeeping timestamp before drawing its replacement. */
    SetDispMask(explicitPresentation?0:1);gpuVBlank();SetDispMask(1);
    SetDefDrawEnv(&context.m0_drawEnv,0,buffer*256,320,224); context.m0_drawEnv.dtd=0;
    PutDrawEnv(&context.m0_drawEnv);
    InitGeom(); SetGeomOffset(160,112); SetGeomScreen(512);
}
static void capture(int buffer) {
    frame(buffer);
    RECT r={0,buffer*256,320,224}; ClearImage(&r,0,0,0);
    for (int x=-512;x<832;x+=8) {
        TILE t; SetTile(&t); setRGB0(&t,8*((x+512)/8%31+1),96,144);
        t.x0=x;t.y0=-16;t.w=8;t.h=256; DrawPrim(&t);
    }
    DrawSync(0); MoveImage(&r,704,256); DrawSync(0);
    /* The actual setup's capture mask preparation: preserve native and
     * wide high-resolution colour through a matching mask upload. */
    static u16 pixels[(320+1024)*256]; s32 margin,top,bottom;
    RECT cr={704,256,320,224};
    int wide=pcWideCaptureReadFrame(&cr,pixels,sizeof(pixels)/sizeof(pixels[0]),&margin,&top,&bottom);
    static u16 native[320*224]; StoreImage(&cr,(u32*)native);
    for(int i=0;i<320*224;i++)native[i]|=0x8000;
    LoadImage(&cr,(u32*)native);
    if(wide) {
        for(int i=0;i<(320+2*margin)*(224+top+bottom);i++)pixels[i]|=0x8000;
        pcWideCaptureLoadFrame(&cr,pixels,margin,top,bottom,0);
    }
    setupScreenTransitionPoly(1,1);
    initTransitionFadeGrid();
    for(int b=0;b<2;b++)SetDrawMode(&gDialogDrModes[b][4],0,0,GetTPage(2,1,704,256),NULL);
}
static int halfFade, sourceWrite, offscreenDraw;
static unsigned queuedStamp;
static uint64_t queuedSerial[2];
static int queuedFrames;
static int checkQueued(const InterpFrame* previous,int after,int buffer) {
    CHECK(previous);
    CHECK(interpolationFrame(after)->serial==queuedSerial[buffer]);
    CHECK(previous->serial<interpolationFrame(after)->serial);
    return 0;
}
static void draw(int buffer,int grid,int value) {
    frame(buffer); ClearOTagR(context.m80D4_uiOT,8);
    /* The game prepares transition packets before updateAndRenderField's
     * frame begin. The test deliberately retains that ordering. */
    transitionStripScale=0x1100; transitionFadeGridRadius=200;
    if(grid) {
        /* Each invocation darkens every corner by six: authored colour-only
         * frames must generate an intermediate too. */
        renderTransitionFadeGrid();
    } else renderScreenTransitionPoly();
    if (halfFade || !grid) for (OTag* tag=&context.m80D4_uiOT[7];tag && (uptr)tag!=OT_LINK_MASK;tag=(OTag*)OT_NEXT(*tag)) {
        if (!grid && ((P_TAG*)tag)->len && (((P_TAG*)tag)->code & 0xfc)==0x2c) {
            POLY_FT4* p=(POLY_FT4*)tag;
            p->x0+=value;p->x1+=value;p->x2+=value;p->x3+=value;
        }
        if (halfFade && ((P_TAG*)tag)->len && (((P_TAG*)tag)->code & 0xfc)==0x3c) {
            POLY_GT4* p=(POLY_GT4*)tag;
            if(p->r0<128)setRGB0(p,p->r0+3,p->g0+3,p->b0+3);
            if(p->r1<128)setRGB1(p,p->r1+3,p->g1+3,p->b1+3);
            if(p->r2<128)setRGB2(p,p->r2+3,p->g2+3,p->b2+3);
            if(p->r3<128)setRGB3(p,p->r3+3,p->g3+3,p->b3+3);
        }
    }
    pcInterpolationFrameBegin(0,0,buffer*256,320,224);
    if (queuedStamp) {
        /* The real field loop begins the next draw before VSync shows the
         * previously completed buffer. That snapshot must survive Begin. */
        display.y=buffer*256;
        int before,after;double alpha;
        const InterpFrame* previous=interpolationPicture(&display,queuedStamp,&before,&after,&alpha);
        if (queuedFrames>=3 && checkQueued(previous,after,buffer)) exit(1);
        gvkPresent(&display,queuedStamp);
        gvkPresent(&display,queuedStamp+1);
        display.y=buffer*256;
    }
    RECT r={0,buffer*256,320,224};ClearImage(&r,0,0,0);
    DrawOTag(&context.m80D4_uiOT[7]);DrawSync(0);
    if(offscreenDraw){
        POLY_F4 q;SetPolyF4(&q);setRGB0(&q,248,0,0);
        q.x0=q.x2=704;q.x1=q.x3=720;q.y0=q.y1=256;q.y2=q.y3=272;
        DrawPrim(&q); /* Its bounds reach the source, but the clip excludes it. */
    }
    if(sourceWrite){RECT r={704,256,320,224};ClearImage(&r,248,0,0);}
    pcInterpolationFrameEnd();
    if (queuedStamp) {
        queuedSerial[buffer]=0;
        for(int i=0;i<INTERP_FRAMES;i++) {
            const InterpFrame* f=interpolationFrame(i);
            if(f->serial>queuedSerial[buffer])queuedSerial[buffer]=f->serial;
        }
        queuedFrames++;
    }
}
static SDL_Surface* shot(const char* dir,unsigned stamp) {
    gvkPresent(&display,stamp);
    char path[1200];snprintf(path,sizeof(path),"%s/present_%06u.png",dir,stamp);
    SDL_Surface* s=SDL_LoadPNG(path);if(!s)return NULL;
    SDL_Surface* rgb=SDL_ConvertSurface(s,SDL_PIXELFORMAT_RGB24);SDL_DestroySurface(s);return rgb;
}
/* Three versions of one capture within a native frame, including cache hits
 * that still see the earlier version. The second/third samples overlap the
 * first sample's UVs; changing the live image must not rewrite owned history. */
static POLY_FT4 feedbackQuads[3];
static int feedbackOwner;
static void feedbackDraw(int buffer,int motion) {
    static uint16_t pixels[320*224];
    frame(buffer);pcInterpolationFrameBegin(0,0,buffer*256,320,224);
    RECT clear={0,buffer*256,320,224},source={704,256,320,224};ClearImage(&clear,0,0,0);
    for(int y=0;y<224;y++)for(int x=0;x<320;x++)pixels[y*320+x]=(uint16_t)(1+(x/2%30)) | (uint16_t)(1+(y/2%30))<<5 | 12<<10 | ((x+y)&1?0x8000:0);
    LoadImage(&source,(u32*)pixels);pcWideCaptureLoadFrame(&source,pixels,0,0,0,0);
    for(int part=0;part<3;part++) {
        if(part) {
            ClearImage(&source,part==1?0:248,part==1?248:0,0);
            for(int i=0;i<320*224;i++)pixels[i]=part==1?31<<5:31;
            pcWideCaptureLoadFrame(&source,pixels,0,0,0,0);
        }
        POLY_FT4* q=&feedbackQuads[part];SetPolyFT4(q);setRGB0(q,128,128,128);
        int x=24+part*48+motion,y=32+part*32,u=part*8,v=part*8;
        q->x0=q->x2=x;q->x1=q->x3=x+32;q->y0=q->y1=y;q->y2=q->y3=y+32;
        q->u0=q->u2=u;q->u1=q->u3=u+32;q->v0=q->v1=v;q->v2=q->v3=v+32;
        q->tpage=GetTPage(2,0,704,256);
        pcWidePrim(q,0,PC_WIDE_HANDLED);
        pcInterpolationTransitionPrim(&feedbackOwner,(unsigned)part,q);DrawPrim(q);
    }
    widePacketsDone();interpolationPacketsDone();DrawSync(0);pcInterpolationFrameEnd();
}
static int feedbackTests(const char* directory,unsigned stamp) {
    const char* aspects[]={"4:3","4:3","21:9","48:9"};
    for(int n=1;n<=4;n*=2)for(int framing=0;framing<4;framing++)for(int buffer=0;buffer<2;buffer++) {
        interpolationSetEnabled(0);hiresSetUpscale(n);hiresSetPrecision(0);hiresSetColor(HIRES_COLOR_DITHER_1X);
        CHECK(gvkHiConfigure(n==1?0:n,HIRES_COLOR_DITHER_1X)==(n==1?0:n));
        CHECK(presentSetAspectText(aspects[framing]));presentSetExpandedVertical(framing!=0);interpolationSetEnabled(1);
        feedbackDraw(buffer,0);feedbackDraw(buffer,0);feedbackDraw(buffer,0);
        SDL_Surface* old=shot(directory,stamp);CHECK(old);
        feedbackDraw(buffer,4);SDL_Surface* anchor=shot(directory,stamp+2);CHECK(anchor);
        CHECK(!memcmp(old->pixels,anchor->pixels,(size_t)anchor->pitch*anchor->h));SDL_DestroySurface(anchor);
        int before,after;double alpha;const InterpFrame* f=interpolationPicture(&display,stamp+3,&before,&after,&alpha);
        if(!f)fprintf(stderr,"feedback n%d framing%d buffer%d: %s\n",n,framing,buffer,interpolationStatus());
        CHECK(f&&f->frozenCount==3&&f->frozenWrites&&alpha>.49&&alpha<.51);
        uint32_t cache0[1280],cache1[1280];gp0InterpolationCache(cache0);
        static uint16_t vram0[1024*512],vram1[1024*512];gpuReadVram(vram0);
        SDL_Surface* middle=shot(directory,stamp+3);CHECK(middle);
        int width=f->viewW*n,height=f->viewH*n;
        size_t pixels=(size_t)width*height;
        uint32_t* replay=malloc(pixels*sizeof(*replay));CHECK(replay);
        CHECK(gvkInterpolationRead(replay,f->viewX*n,f->viewY*n,width,height));
        gp0InterpolationCache(cache1);gpuReadVram(vram1);
        CHECK(!memcmp(cache0,cache1,sizeof(cache0)));CHECK(!memcmp(vram0,vram1,sizeof(vram0)));
        CHECK(memcmp(old->pixels,middle->pixels,(size_t)middle->pitch*middle->h));
        RECT source={704,256,320,224};ClearImage(&source,0,0,248);
        SDL_Surface* changed=shot(directory,stamp+3);CHECK(changed);
        CHECK(!memcmp(changed->pixels,middle->pixels,(size_t)middle->pitch*middle->h));SDL_DestroySurface(changed);
        interpolationSetEnabled(0);feedbackDraw(buffer,2);SDL_Surface* expected=shot(directory,stamp+4);CHECK(expected);
        /* Compare the owned render before presentation too, including the
         * mask bit that the displayed RGB comparison cannot observe. */
        uint32_t* native=malloc(pixels*sizeof(*native));CHECK(native);
        if(n>1)CHECK(gvkHiRead(f->wide,native,f->viewX*n,f->viewY*n,width,height));
        else {
            uint16_t* words=malloc(pixels*sizeof(*words));CHECK(words);
            if(f->wide)gvkReadWide(words,f->viewX,f->viewY,width,height);
            else {
                gpuReadVram(vram1);
                for(int y=0;y<height;y++)memcpy(words+y*width,vram1+(f->viewY+y)*1024+f->viewX,(size_t)width*sizeof(*words));
            }
            for(size_t i=0;i<pixels;i++) {
                unsigned c=words[i],r=c&31,g=c>>5&31,b=c>>10&31;
                native[i]=(r<<3|r>>2)|(g<<3|g>>2)<<8|(b<<3|b>>2)<<16|(c>>15)<<24;
            }
            free(words);
        }
        unsigned rawDifferent=0;
        for(size_t i=0;i<pixels;i++)rawDifferent+=(native[i]&0x1ffffff)!=(replay[i]&0x1ffffff);
        if(rawDifferent)fprintf(stderr,"feedback raw n%d framing%d buffer%d: %u pixels differ\n",n,framing,buffer,rawDifferent);
        CHECK(!rawDifferent);free(replay);free(native);
        unsigned different=0;for(int y=0;y<middle->h;y++)for(int x=0;x<middle->w;x++)
            different+=memcmp((char*)middle->pixels+y*middle->pitch+x*3,(char*)expected->pixels+y*expected->pitch+x*3,3)!=0;
        if(different)fprintf(stderr,"feedback n%d framing%d buffer%d: %u pixels differ\n",n,framing,buffer,different);
        CHECK(!different);SDL_DestroySurface(old);SDL_DestroySurface(middle);SDL_DestroySurface(expected);stamp+=10;
    }
    return 0;
}

int main(int argc,char**argv) {
    SDL_setenv_unsafe("XG_SPEEDUP", "1", 1);
    for(int i=0;i<4096;i++){rcossin_tbl[2*i]=(s16)lround(4096*sin(i*6.283185307179586/4096));rcossin_tbl[2*i+1]=(s16)lround(4096*cos(i*6.283185307179586/4096));}
    CHECK(argc==2 || (argc==3 && !strcmp(argv[2],"--reported")));
    int reported=argc==3;
    explicitPresentation=reported;
    testSetEnv("XG_SETTINGS","none");testSetEnv("XG_DUMP_PRESENT",argv[1]);
    CHECK(SDL_Init(SDL_INIT_VIDEO));CHECK(gpuInit(NULL));gpuVBlank();
    static const char* aspects[]={"4:3","16:9","21:9","32:9","48:9"};unsigned stamp=100;
    int reportedScale=12,maxDim;uint64_t heapBytes;
    gvkHiLimits(&maxDim,&heapBytes);
    if(reportedScale>maxDim/WIDE_H)reportedScale=maxDim/WIDE_H;
    CHECK(reportedScale>=1);
    if(reported)printf("transitiontest: reported profile at %dx (device limit %d)\n",reportedScale,maxDim);
    if(getenv("XG_TRANSITION_FEEDBACK_ONLY")){CHECK(!feedbackTests(argv[1],5000));gpuShutdown();SDL_Quit();printf("transition feedback: %d checks pass\n",checks);return 0;}
    for(int n=reported?reportedScale:1;n<=(reported?reportedScale:4);n*=2)for(int a=0;a<5;a++)for(int e=0;e<2;e++)for(int b=0;b<2;b++)for(int grid=0;grid<2;grid++) {
        if(reported && (a!=1 || !e))continue;
        interpolationSetEnabled(0);hiresSetUpscale(n);hiresSetPrecision(reported);hiresSetColor(HIRES_COLOR_DITHER_1X);
        CHECK(gvkHiConfigure(n==1?0:n,HIRES_COLOR_DITHER_1X)==(n==1?0:n));CHECK(presentSetAspectText(aspects[a]));presentSetExpandedVertical(e);
        capture(b);interpolationSetEnabled(1);
        draw(b,grid,0);draw(reported?b^1:b,grid,0);draw(b,grid,0);
        SDL_Surface* old=shot(argv[1],stamp);CHECK(old);
        draw(reported?b^1:b,grid,4);SDL_Surface* anchor=shot(argv[1],stamp+2);CHECK(anchor);SDL_DestroySurface(anchor);
        int before,after;double alpha;
        const InterpFrame* f=interpolationPicture(&display,stamp+3,&before,&after,&alpha);
        if(!f)fprintf(stderr,"transition n%d %s expanded%d buffer%d grid%d: %s\n",n,aspects[a],e,b,grid,interpolationStatus());
        CHECK(f&&f->frozen&&f->valid);CHECK(alpha>.49&&alpha<.51);
        if (!interpFramesMotionKinds(f,interpolationFrame(after))) for(unsigned i=0;i<f->drawCount;i++) {
            const InterpFrame* z=interpolationFrame(after);const HiVertex* v=&f->vertices[f->draws[i].first];
            const char* why=interpDrawMismatch(f,z,i);
            fprintf(stderr,"draw%u face%llu: %s xy%.1f %.1f uv%08x..%08x\n",i,(unsigned long long)f->draws[i].face,why?why:"same",v->x,v->y,v->uvMin,v->uvMax);
            for(unsigned j=0;j<z->drawCount;j++)if(z->draws[j].face==f->draws[i].face){const HiVertex* w=&z->vertices[z->draws[j].first];fprintf(stderr," after xy%.1f %.1f uv%08x..%08x mode%x/%x tex%x/%x clip%x/%x\n",w->x,w->y,w->uvMin,w->uvMax,v->mode,w->mode,v->tex,w->tex,v->clipMax,w->clipMax);}
        }
        SDL_Surface* middle=shot(argv[1],stamp+3);CHECK(middle);
        CHECK(memcmp(old->pixels,middle->pixels,(size_t)middle->pitch*middle->h));
        static uint16_t v0[1024*512],v1[1024*512];gpuReadVram(v0);
        RECT mutate={704,256,320,224};ClearImage(&mutate,248,0,0);
        SDL_Surface* changed=shot(argv[1],stamp+3);CHECK(changed);
        CHECK(!memcmp(middle->pixels,changed->pixels,(size_t)middle->pitch*middle->h));SDL_DestroySurface(changed);
        gpuReadVram(v1);
        /* Only the deliberate source mutation changes native VRAM. */
        for(int y=0;y<512;y++)CHECK(!memcmp(v0+y*1024,v1+y*1024,(y<256||y>=480?1024:704)*sizeof(uint16_t)));
        pcInterpolationScene();CHECK(!interpolationPicture(&display,stamp+3,&before,&after,&alpha));
        interpolationSetEnabled(0);freeTransitionFadeGrid();capture(b);
        if(grid){draw(b,grid,2);draw(b,grid,2);draw(b,grid,2);halfFade=1;}
        draw(b,grid,2);halfFade=0;
        SDL_Surface* expected=shot(argv[1],stamp+4);CHECK(expected);
        unsigned diff=0;
        for(int y=0;y<middle->h;y++)for(int x=0;x<middle->w;x++)
            diff+=memcmp((char*)middle->pixels+y*middle->pitch+x*3,(char*)expected->pixels+y*expected->pitch+x*3,3)!=0;
        if(diff)fprintf(stderr,"halfway n%d %s expanded%d buffer%d grid%d: %u/%d pixels differ\n",n,aspects[a],e,b,grid,diff,middle->w*middle->h);
        CHECK(!diff);
        SDL_DestroySurface(expected);SDL_DestroySurface(old);SDL_DestroySurface(middle);freeTransitionFadeGrid();stamp+=10;
    }
    if(reported) {
        interpolationSetEnabled(0);capture(0);interpolationSetEnabled(1);
        queuedSerial[0]=queuedSerial[1]=0;queuedFrames=0;
        for(int i=0;i<12;i++) { queuedStamp=200+i*2;draw(i&1,0,i*2); }
        queuedStamp=0;
        int before,after;double alpha;
        display.y=256;
        CHECK(interpolationPicture(&display,224,&before,&after,&alpha));
        display.y=0;
        CHECK(!interpolationPicture(&display,225,&before,&after,&alpha));
        CHECK(!strcmp(interpolationStatus(),"Native: waiting for a newer field frame"));
        freeTransitionFadeGrid();
    }
    CHECK(!feedbackTests(argv[1],5000));
    interpolationSetEnabled(0);hiresSetUpscale(1);hiresSetPrecision(0);
    gvkHiConfigure(0,HIRES_COLOR_DITHER_1X);presentSetAspectText("4:3");presentSetExpandedVertical(0);
    capture(0);interpolationSetEnabled(1);offscreenDraw=1;draw(0,0,0);draw(0,0,0);draw(0,0,0);offscreenDraw=0;
    int valid=0;
    for(int i=0;i<INTERP_FRAMES;i++){const InterpFrame* f=interpolationFrame(i);if(f->serial && f->valid && f->frozen)valid=1;}
    CHECK(valid);freeTransitionFadeGrid();
    gpuShutdown();SDL_Quit();printf("transitiontest: %d checks pass\n",checks);return 0;
}
