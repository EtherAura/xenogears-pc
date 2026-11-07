/* Exercise the game's actual distortion grid. With zero wave amplitude it
 * must preserve the entire image, including margins and the expanded rows
 * above and below the frame, at both buffer origins. Row colours expose bad
 * scratch mapping and missing feedback barriers.
 *
 * With a wave, over vertical stripes (one colour per column, the same on
 * every line, so only each line's horizontal mapping shows): native VRAM is
 * the same at every aspect and with or without expanded rows; the display
 * copy's centre is VRAM's; the expanded rows continue the picture's edge rows
 * (line -1 as line 0, line 224 as line 223, margins included), so the bands'
 * scratch copies are read back in place and the game's rows drawn 32 lines
 * low leave nothing in the bottom band; and the other buffer's picture is
 * untouched (scratch isolation). */
#include "common.h"
#include "psyq.h"
#include "field/field.h"
#include "field/screen_effects.h"
#include "gpu/gpu_internal.h"
#include "gpu/interpolation.h"
#include "pc_interpolation.h"
#include "main/memory.h"
#include "test/env.h"
#include <SDL3/SDL.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int gpuInit(SDL_Window*);
void gpuShutdown(void);
void SetTile(void*);
void setupScreenDistortion(s32);
void updateAndRenderScreenDistortion(void);
void freeScreenDistortion(void);
FieldScreenEffects fieldScreenEffects;
s32 screenDistrotionAllocated, g_frameOddOrEven;
u16 screenDistortionStripOrigins[30];
typedef struct { DR_MODE m0_modes[16]; } Unk800B1E18;
Unk800B1E18 D_800B1E18[2];
static sFieldRenderContext context;
sFieldRenderContext* pCurrentFieldRenderingContext = &context;
void* allocateHeap(s32 bytes, s32 flags) { (void)flags; return calloc(1, (size_t)bytes); }
s32 freeHeap(void* ptr) { free(ptr); return 0; }
s32 getImmediateOrVariableUnsigned(s32 offset) { (void)offset; return 0; }
static int replayAmplitude = 5, replayPhase = 0x123;
static int replayAmpX = 6, replayStepX = 0x50, replayStepY = 0x70, replayPhaseX, replayPattern = 1, replayPasses = 1;
static int waveOn; /* 0: the zero-amplitude control */
s32 getAngleSin(s32 angle) { return waveOn ? (s32)lround(4096.0 * sin(angle * M_PI / 2048.0)) : 0; }
static int checks;
#include "test/check.h"
#define ROWS (224 + 32)
static uint16_t before[2048*ROWS], after[2048*ROWS], other[2048*ROWS], otherAfter[2048*ROWS];
static uint16_t nativeBefore[1024*512], nativeAfter[1024*512], reference[3][2][1024*224];

static void frame(int buffer) {
    int y = buffer*256;
    g_frameOddOrEven = buffer;
    DISPENV display; SetDefDispEnv(&display,0,y,320,224);
    display.screen.y = 10; display.screen.h = 216;
    PutDispEnv(&display); SetDispMask(1); gpuVBlank();
    DRAWENV draw; SetDefDrawEnv(&draw,0,y,320,224);
    draw.dtd = 0; PutDrawEnv(&draw);
}

/* Rows of colour (control) or columns of colour (wave), past every edge. */
static void picture(int stripes) {
    if (stripes == 2) {
        for (int y=-32; y<256; y+=16) for (int x=-512; x<832; x+=16) {
            TILE t; SetTile(&t);
            int col=(x+512)/16, row=(y+32)/16;
            setRGB0(&t,8*((col*3+row*7)&31),8*((col*11+row*5)&31),8*((col*7+row*13)&31));
            t.x0=x; t.y0=y; t.w=t.h=16; DrawPrim(&t);
        }
        return;
    }
    if (!stripes) {
        for (int row = -2; row < 16; row++) {
            TILE t; SetTile(&t);
            setRGB0(&t,8*(row+3),8*(16-row),8*((row+5)%16+1));
            t.x0 = 0; t.y0 = row*16; t.w = 320; t.h = 16;
            DrawPrim(&t);
        }
        return;
    }
    for (int x = -1024; x < 320+1024; x += 2) {
        TILE t; SetTile(&t);
        int c = (x+1024)/2;
        setRGB0(&t,8*(c&31),8*((c>>5)&31)+4,8*((c*7)&31)+2);
        t.x0 = x; t.y0 = -32; t.w = 2; t.h = 224+64;
        DrawPrim(&t);
    }
}

static void wave(void) {
    fieldScreenEffects.m4_distortionParamDivider = 0;
    fieldScreenEffects.m8_distortionParamDiff[0] = replayAmpX << 16;    /* amplitude x */
    fieldScreenEffects.m8_distortionParamDiff[1] = replayAmplitude << 16;    /* amplitude y */
    fieldScreenEffects.m8_distortionParamDiff[2] = replayStepX << 16; /* step x */
    fieldScreenEffects.m8_distortionParamDiff[3] = replayStepY << 16; /* step y */
    fieldScreenEffects.m8_distortionParamDiff[4] = 0;
    fieldScreenEffects.m8_distortionParamDiff[5] = 0;
    /* column 19's edge pushed right (sin = 1): the PS1's stale columns at
     * x 316-319, which the wide picture's margin covers, do not occur */
    fieldScreenEffects.m38_phaseX = replayPhaseX ? replayPhaseX : 1024 - 19*0x50;
    fieldScreenEffects.m3A_phaseY = replayPhase;
}

static void readWide(uint16_t* dst, int y, int top, int rows) {
    WideDraw target;
    if (wideDrawTarget(0,y,319,y+223,&target)) {
        gvkReadWide(dst,0,y+target.shiftY-top,320+2*pcWideView,rows);
    }
}


/* Native distortion of a halfway pose is the image oracle for private replay.
 * The background and wave stay fixed; an identified rectangle moves behind
 * the actual game OT. Pixel equality proves that its captured colour is
 * recomputed from the moving prefix, rather than a frozen final screenshot. */
static POLY_F4 movingQuad;
static int movingOwner;
static GpuDisplay replayDisplay = {0,0,320,224,0,1,0,0,0,0,0};
static void replayDraw(int x, int buffer) {
    frame(buffer); replayDisplay.y = buffer*256;
    pcInterpolationFrameBegin(2,0,buffer*256,320,224);
    RECT r = {0,buffer*256,320,224}; ClearImage(&r,0,0,0);
    picture(replayPattern);
    SetPolyF4(&movingQuad); setRGB0(&movingQuad,248,0,0);
    movingQuad.x0 = movingQuad.x2 = x; movingQuad.x1 = movingQuad.x3 = x+20;
    movingQuad.y0 = movingQuad.y1 = -16; movingQuad.y2 = movingQuad.y3 = 240;
    pcInterpolationObject(&movingOwner,&movingQuad); pcInterpolationModel(&movingQuad); pcInterpolationPrim(&movingQuad);
    DrawPrim(&movingQuad); pcInterpolationObject(NULL,NULL);
    waveOn = 1; setupScreenDistortion(1); wave();
    SetDrawTPage((DR_TPAGE*)&D_800B1E18[buffer].m0_modes[0],1,0,GetTPage(2,0,0,buffer*256));
    DR_TPAGE outerBegin, outerEnd;
    if (replayPasses > 1) {
        SetDrawTPage(&outerBegin,1,0,GetTPage(2,0,0,buffer*256));
        pcInterpolationCapturePrim(&outerBegin,PC_INTERP_CAPTURE_BEGIN,0,0); DrawPrim(&outerBegin);
    }
    for (int pass=0; pass<replayPasses; pass++) {
        ClearOTagR(context.mCC_OT,2); updateAndRenderScreenDistortion();
        DrawOTag(&context.mCC_OT[1]); DrawSync(0);
    }
    if (replayPasses > 1) {
        SetDrawTPage(&outerEnd,1,0,GetTPage(2,0,0,buffer*256));
        pcInterpolationCapturePrim(&outerEnd,PC_INTERP_CAPTURE_SKIP|PC_INTERP_CAPTURE_END,0,0); DrawPrim(&outerEnd);
    }
    pcInterpolationFrameEnd(); waveOn = 0;
}
static SDL_Surface* replayShot(const char* directory, unsigned stamp) {
    gvkPresent(&replayDisplay,stamp);
    char path[1200]; snprintf(path,sizeof(path),"%s/present_%06u.png",directory,stamp);
    SDL_Surface* image = SDL_LoadPNG(path);
    if (!image) return NULL;
    SDL_Surface* rgb = SDL_ConvertSurface(image,SDL_PIXELFORMAT_RGB24); SDL_DestroySurface(image);
    return rgb;
}
static int replayTest(const char* directory) {
    static const char* aspects[] = {"4:3","16:9","21:9","32:9","48:9"};
    unsigned stamp = 100;
    for (int scenario = 0; scenario < 5; scenario++) for (int n = 1; n <= 4; n *= 2) for (int aspect = 0; aspect < 5; aspect++)
        for (int expanded = 0; expanded < 2; expanded++) for (int buffer = 0; buffer < 2; buffer++) {
        if (getenv("XG_DISTORTION_SCENARIO") && scenario != atoi(getenv("XG_DISTORTION_SCENARIO"))) continue;
        if (getenv("XG_DISTORTION_SCALE") && n != atoi(getenv("XG_DISTORTION_SCALE"))) continue;
        if (getenv("XG_DISTORTION_ASPECT") && aspect != atoi(getenv("XG_DISTORTION_ASPECT"))) continue;
        if (getenv("XG_DISTORTION_EXPANDED") && expanded != atoi(getenv("XG_DISTORTION_EXPANDED"))) continue;
        if (getenv("XG_DISTORTION_BUFFER") && buffer != atoi(getenv("XG_DISTORTION_BUFFER"))) continue;
        replayPattern = scenario ? 2 : 1;
        replayPasses = scenario == 4 ? 2 : 1;
        replayAmplitude = scenario == 1 ? 64 : scenario == 3 ? 64 : 5;
        replayPhase = scenario == 1 ? 0x900 : scenario == 3 ? 0xc00 : scenario >= 2 ? 0x400 : 0x123;
        replayStepX = scenario >= 2 ? 0 : 0x50;
        replayStepY = scenario >= 2 ? 0 : 0x70;
        replayPhaseX = scenario >= 2 ? 0x400 : 0;
        replayAmpX = 6;
        interpolationSetEnabled(0);
        hiresSetUpscale(n); hiresSetPrecision(0); hiresSetColor(HIRES_COLOR_DITHER_1X);
        CHECK(gvkHiConfigure(n == 1 ? 0 : n, HIRES_COLOR_DITHER_1X) == (n == 1 ? 0 : n));
        CHECK(presentSetAspectText(aspects[aspect])); presentSetExpandedVertical(expanded);
        interpolationSetEnabled(1);
        replayDraw(40,buffer); replayDraw(40,buffer); replayDraw(40,buffer);
        SDL_Surface* endpoint = replayShot(directory,stamp); CHECK(endpoint);
        if (scenario >= 2) replayAmpX = 10;
        if (scenario == 3) replayAmplitude = 68;
        replayDraw(50,buffer); SDL_Surface* anchor = replayShot(directory,stamp+2); CHECK(anchor);
        CHECK(!memcmp(endpoint->pixels,anchor->pixels,(size_t)anchor->pitch*anchor->h));
        SDL_DestroySurface(anchor);
        int beforeSlot, afterSlot; double alpha;
        const InterpFrame* f = interpolationPicture(&replayDisplay,stamp+3,&beforeSlot,&afterSlot,&alpha);
        if (!f) fprintf(stderr,"replay: n%d %s expanded%d buffer%d: %s\n",n,aspects[aspect],expanded,buffer,interpolationStatus());
        CHECK(f && f->capture && f->valid); CHECK(alpha > .49 && alpha < .51);
        uint32_t cacheBefore[1280], cacheAfter[1280];
        gp0InterpolationCache(cacheBefore);
        gpuReadVram(nativeBefore);
        SDL_Surface* middle = replayShot(directory,stamp+3); CHECK(middle);
        uint32_t* privatePixels=malloc((size_t)f->viewW*f->viewH*n*n*4);
        CHECK(gvkInterpolationRead(privatePixels,f->viewX*n,f->viewY*n,f->viewW*n,f->viewH*n));
        gpuReadVram(nativeAfter); CHECK(!memcmp(nativeBefore,nativeAfter,sizeof(nativeBefore)));
        gp0InterpolationCache(cacheAfter); CHECK(!memcmp(cacheBefore,cacheAfter,sizeof(cacheBefore)));
        if (scenario >= 2) CHECK(interpFramesMotionKinds(f,interpolationFrame(afterSlot)) & (1u << PC_INTERP_WAVE));
        CHECK(memcmp(endpoint->pixels,middle->pixels,(size_t)middle->pitch*middle->h));
        /* Mutating the live capture strips cannot alter owned replay. */
        RECT scratch = {960,0,64,240}; ClearImage(&scratch,0,248,0);
        SDL_Surface* mutation = replayShot(directory,stamp+3);
        if (mutation) CHECK(!memcmp(middle->pixels,mutation->pixels,(size_t)middle->pitch*middle->h));
        SDL_DestroySurface(mutation);
        if (scenario >= 2) replayAmpX = 8;
        if (scenario == 3) replayAmplitude = 66;
        interpolationSetEnabled(0); replayDraw(45,buffer);
        SDL_Surface* expected = replayShot(directory,stamp+4); CHECK(expected);
        unsigned rawDifferent=0;
        size_t pixelCount=(size_t)f->viewW*f->viewH*n*n;
        uint32_t* exact=malloc(pixelCount*4);
        if(n>1)CHECK(gvkHiRead(f->wide,exact,f->viewX*n,f->viewY*n,f->viewW*n,f->viewH*n));
        else {
            uint16_t* words=malloc(pixelCount*2);
            if(f->wide)gvkReadWide(words,f->viewX,f->viewY,f->viewW,f->viewH);
            else {gpuReadVram(nativeAfter);for(int y=0;y<f->viewH;y++)memcpy(words+y*f->viewW,nativeAfter+(f->viewY+y)*1024+f->viewX,(size_t)f->viewW*2);}
            for(size_t k=0;k<pixelCount;k++) {
                unsigned c=words[k],r=c&31,g=c>>5&31,b=c>>10&31;
                exact[k]=(r<<3|r>>2)|(g<<3|g>>2)<<8|(b<<3|b>>2)<<16|(c>>15)<<24;
            }
            free(words);
        }
        for(size_t k=0;k<pixelCount;k++)if((exact[k]&0x1ffffff)!=(privatePixels[k]&0x1ffffff)) {
            if(rawDifferent++<12)fprintf(stderr,"raw mismatch %zu,%zu private%08x native%08x\n",k%(f->viewW*n),k/(f->viewW*n),privatePixels[k],exact[k]);
        }
        if(rawDifferent)fprintf(stderr,"raw differences: scenario%d n%d aspect%s expanded%d buffer%d: %u\n",scenario,n,aspects[aspect],expanded,buffer,rawDifferent);
        if(rawDifferent && n>1) {
            replayDraw(45,buffer);
            uint32_t* repeat=malloc(pixelCount*4);CHECK(gvkHiRead(f->wide,repeat,f->viewX*n,f->viewY*n,f->viewW*n,f->viewH*n));
            unsigned changed=0,vsPrivate=0;
            for(size_t k=0;k<pixelCount;k++){changed+=(repeat[k]&0x1ffffff)!=(exact[k]&0x1ffffff);vsPrivate+=(repeat[k]&0x1ffffff)!=(privatePixels[k]&0x1ffffff);}
            fprintf(stderr,"native repeat differences %u, versus private %u\n",changed,vsPrivate);free(repeat);
        }
        free(exact);CHECK(!rawDifferent);
        free(privatePixels);
        if (memcmp(middle->pixels,expected->pixels,(size_t)middle->pitch*middle->h)) {
            int different = 0;
            for (int y = 0; y < middle->h; y++) for (int x = 0; x < middle->w; x++)
                different += memcmp((char*)middle->pixels+y*middle->pitch+x*3,(char*)expected->pixels+y*expected->pitch+x*3,3)!=0;
            fprintf(stderr,"replay differs: scenario%d n%d %s expanded%d buffer%d pixels%d\n",scenario,n,aspects[aspect],expanded,buffer,different);
        }
        CHECK(!memcmp(middle->pixels,expected->pixels,(size_t)middle->pitch*middle->h));
        SDL_DestroySurface(endpoint); SDL_DestroySurface(middle); SDL_DestroySurface(expected);
        freeScreenDistortion(); stamp += 20;
    }
    replayAmplitude=5; replayPhase=0x123; replayAmpX=6; replayStepX=0x50; replayStepY=0x70;
    replayPhaseX=0; replayPattern=1; replayPasses=1;
    interpolationSetEnabled(0);
    printf("Screen distortion replay: %d checks passed\n",checks); return 0;
}

int main(int argc, char** argv) {
    SDL_setenv_unsafe("XG_SPEEDUP", "1", 1);

    testSetEnv("XG_SETTINGS","none");
    if (argc == 2) {
        testSetEnv("XG_DUMP_PRESENT",argv[1]); testSetEnv("XG_DUMP_RANGE","0-30000");
        testSetEnv("XG_PRESENT_FILTER","nearest"); testSetEnv("XG_SCALE","fit");
    }
    CHECK(SDL_Init(SDL_INIT_VIDEO));
    SDL_Window* window = argc == 2 ? SDL_CreateWindow("distortion replay",960,720,SDL_WINDOW_HIDDEN) : NULL;
    CHECK(gpuInit(window));
    pcWideSetMode(PC_WIDE_FIELD);
    for (int i = 0; i < 15; i++) {
        screenDistortionStripOrigins[i*2] = (i%5)*64;
        screenDistortionStripOrigins[i*2+1] = 176+(i/5)*16;
    }
    if (argc == 2) {
        int result = replayTest(argv[1]); gpuShutdown(); SDL_DestroyWindow(window); SDL_Quit(); return result;
    }
    /* 48:9: a margin over 352, where the bands' scratch copy is cut in two */
    static const char* aspects[] = {"4:3","16:9","21:9","32:9","48:9"};
    for (int scale = 0; scale <= 4; scale = scale ? scale*2 : 2) {
        hiresSetUpscale(scale ? scale : 1);
        hiresSetPrecision(0);
        gvkHiConfigure(scale,HIRES_COLOR_DITHER_1X);
        for (int aspect = 0; aspect < 5; aspect++) for (int expanded = 0; expanded < 2; expanded++) {
            CHECK(presentSetAspectText(aspects[aspect]));
            presentSetExpandedVertical(expanded);
            for (int stripes = 0; stripes < 2; stripes++) for (int buffer = 0; buffer < 2; buffer++) {
                int y = buffer*256, top, bottom;
                /* the other buffer's picture first: the scratch must leave it */
                frame(1-buffer); picture(1); DrawSync(0);
                frame(buffer); picture(stripes);
                int wide = wideActive();
                pcWideFrameLines(0,y,&top,&bottom);
                CHECK(expanded || (top == 0 && bottom == 0));
                CHECK(!expanded || (top == 10 && bottom == 6));
                int width = 320+2*pcWideView, rows = top+224+bottom;
                readWide(before,y,top,rows);
                readWide(other,256-y,top,rows);
                gpuReadVram(nativeBefore);
                waveOn = stripes;
                setupScreenDistortion(1);
                if (stripes) wave();
                ClearOTagR(context.mCC_OT,2);
                updateAndRenderScreenDistortion();
                DrawOTag(&context.mCC_OT[1]); DrawSync(0);
                waveOn = 0;
                readWide(after,y,top,rows);
                readWide(otherAfter,256-y,top,rows);
                gpuReadVram(nativeAfter);
                if (wide) {
                    /* isolation: the other buffer's copy (bands and margins too) */
                    CHECK(memcmp(other,otherAfter,(size_t)width*rows*2)==0);
                }
                if (!stripes) {
                    for (int row = 0; wide && row < rows; row++) {
                        if (memcmp(before+row*width,after+row*width,(size_t)width*2)) {
                            fprintf(stderr,"scale %d aspect %s expanded %d buffer %d row %d\n",scale,aspects[aspect],expanded,buffer,row-top);
                            for (int x = 0, n = 0; x < width && n < 12; x++) if (before[row*width+x] != after[row*width+x]) {
                                fprintf(stderr,"  x %d: %04x -> %04x\n",x-pcWideView,before[row*width+x],after[row*width+x]); n++;
                            }
                        }
                        CHECK(memcmp(before+row*width,after+row*width,(size_t)width*2)==0);
                    }
                    for (int row = 0; row < 224; row++)
                        CHECK(memcmp(nativeBefore+(y+row)*1024,nativeAfter+(y+row)*1024,640)==0);
                } else {
                    /* native VRAM: the same with any aspect and framing */
                    if (aspect == 0 && !expanded) {
                        memcpy(reference[scale/2][buffer],nativeAfter+y*1024,sizeof(reference[0][0]));
                    }
                    CHECK(memcmp(reference[scale/2][buffer],nativeAfter+y*1024,sizeof(reference[0][0]))==0);
                    CHECK(memcmp(nativeBefore+(256-y)*1024,nativeAfter+(256-y)*1024,224*1024*2)==0);
                    for (int row = 0; wide && row < 224; row++) {
                        /* the centre: VRAM's */
                        CHECK(memcmp(after+(top+row)*width+pcWideView,nativeAfter+(y+row)*1024,640)==0);
                    }
                    if (wide && top) {
                        /* line -1 continues line 0, line 224 line 223, edge to edge */
                        if (memcmp(after+(top-1)*width,after+top*width,(size_t)width*2) ||
                            memcmp(after+(top+224)*width,after+(top+223)*width,(size_t)width*2)) {
                            fprintf(stderr,"scale %d aspect %s buffer %d: band edge\n",scale,aspects[aspect],buffer);
                        }
                        CHECK(memcmp(after+(top-1)*width,after+top*width,(size_t)width*2)==0);
                        CHECK(memcmp(after+(top+224)*width,after+(top+223)*width,(size_t)width*2)==0);
                        /* and the whole band follows its edge row */
                        for (int row = 0; row < top; row++)
                            CHECK(memcmp(after+row*width,after+top*width,(size_t)width*2)==0);
                        for (int row = top+224; row < rows; row++)
                            CHECK(memcmp(after+row*width,after+(top+223)*width,(size_t)width*2)==0);
                    }
                }
                if (scale && !stripes && wide) for (int row = 0; row < rows; row += 8) {
                    int columns[] = {0,pcWideView ? pcWideView-1 : 1,pcWideView+319,width-1};
                    for (int i = 0; i < 4; i++) {
                        uint32_t got, want = 0;
                        uint16_t c = before[row*width+columns[i]];
                        for (int k = 0; k < 3; k++) {
                            unsigned v = c>>(5*k)&31;
                            want |= (v<<3|v>>2)<<(8*k);
                        }
                        WideDraw target;
                        CHECK(wideDrawTarget(0,y,319,y+223,&target));
                        CHECK(gvkHiRead(1,&got,columns[i]*scale+1,(y+target.shiftY-top+row)*scale+1,1,1));
                        CHECK((got&0xffffff)==want);
                    }
                }
                freeScreenDistortion();
            }
        }
    }
    gpuShutdown(); SDL_Quit();
    printf("Screen distortion: %d checks passed\n",checks);
    return 0;
}
