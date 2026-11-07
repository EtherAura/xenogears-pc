/* Retail button resources are supplied locally, never embedded in fixtures.
 * Compare remapped draws against independently selected native atlas tiles,
 * and generated controller keycaps against the shared pixel specification. */
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "common.h"
#include "psyq.h"
#include "gpu/texture.h"
#include "system/prompt.h"
#include "gpu/shaders/prompt.glsl"
#include "test/env.h"
int gpuInit(SDL_Window* window);
void gpuShutdown(void);
static int checks, mapping[8], drawSprite;
#include "test/check.h"
int pcPadPromptGlyph(int slot) { return mapping[slot]; }
static void identity(void) { for (int i=0;i<8;++i) mapping[i]=i; }
static void quad(TextureRequest r,int width,int x) {
    if (drawSprite) {
        DR_TPAGE tp; SetDrawTPage(&tp,1,0,r.tpage); DrawPrim(&tp);
        SPRT s={0}; SetSprt(&s); SetShadeTex(&s,1);
        s.x0=x; s.y0=20; s.w=width; s.h=12;
        s.u0=r.u; s.v0=r.v; s.clut=r.clut; DrawPrim(&s);
        return;
    }
    POLY_FT4 p={0}; SetPolyFT4(&p); SetShadeTex(&p,1);
    p.x0=p.x2=x; p.x1=p.x3=x+width;
    p.y0=p.y1=20; p.y2=p.y3=32;
    p.u0=p.u2=r.u; p.u1=p.u3=r.u+r.w;
    p.v0=p.v1=r.v; p.v2=p.v3=r.v+r.h;
    p.tpage=r.tpage; p.clut=r.clut;
    DrawPrim(&p);
}
static void readTile(int x,int w,uint16_t* pixels) {
    RECT r={x,20,w,12}; StoreImage(&r,(uint32_t*)pixels);
}
static TextureRequest expected(TextureRequest from,int style,int glyph) {
    TextureRequest to=from;
    if (style==0) { static const int u[]={0,36,24,12}; to.u=u[glyph]; }
    else if (style==3) {
        static const int u[]={208,244,232,220},clut[]={31820,31823,31822,31821};
        to.u=u[glyph]; to.clut=clut[glyph]; to.w=glyph==1?11:12;
    } else {
        static const int u[]={80,116,104,92}; to.tpage=143; to.clut=32128;
        to.u=u[glyph]; to.v=140;
    }
    return to;
}
int main(int argc,char** argv) {
    CHECK(argc==4); CHECK(testSetEnv("XG_SETTINGS","none")==0);
    CHECK(SDL_Init(SDL_INIT_VIDEO)); CHECK(gpuInit(NULL));
    FILE* f=fopen(argv[1],"rb"); CHECK(f);
    uint16_t* image=malloc(1024*512*2); CHECK(image);
    CHECK(fread(image,2,1024*512,f)==1024*512 && fclose(f)==0);
    RECT all={0,0,1024,512}; LoadImage(&all,image);
    DRAWENV env; SetDefDrawEnv(&env,0,0,320,224); PutDrawEnv(&env);
    int menu=!strcmp(argv[2],"menu"),jp=!strcmp(argv[3],"jp");
    TextureRequest sources[16]; int slots[16],styles[16],count=0;
    if (menu) {
        const int u[]={0,36,24,12};
        for (int i=0;i<4;++i) {
            sources[count]=(TextureRequest){135,32704,0,u[i],120,12,12,0,0};
            slots[count]=i; styles[count++]=0;
        }
    } else {
        for (int i=0;i<3;++i) {
            sources[count]=(TextureRequest){143,32384,0,80+i*12,128,12,12,0,0};
            slots[count]=i==0?(jp?0:1):i==1?(jp?3:2):(jp?2:3); styles[count++]=1;
        }
        const int u[]={80,116,104,92};
        for (int i=0;i<4;++i) {
            sources[count]=(TextureRequest){143,32128,0,u[i],140,12,12,0,0};
            slots[count]=i; styles[count++]=2;
        }
        const int u2[]={208,244,232,220},clut[]={31820,31823,31822,31821};
        for (int i=0;i<4;++i) {
            sources[count]=(TextureRequest){15,clut[i],0,u2[i],152,i==1?11:12,12,0,0};
            slots[count]=i; styles[count++]=3;
        }
    }
    uint16_t actual[144], reference[144];
    for (int scale=0;scale<=4;scale+=4) {
        hiresSetUpscale(scale?scale:1); hiresSetPrecision(0); hiresSetSeamGuard(0);
        CHECK(gvkHiConfigure(scale,HIRES_COLOR_RAW)==scale);
        for (drawSprite=0;drawSprite<2;++drawSprite)
        for (int i=0;i<count;++i) for (int glyph=0;glyph<PAD_GLYPH_COUNT;++glyph) {
            if (glyph>=4 && i>0) continue;
            identity(); mapping[slots[i]]=glyph;
            RECT clear={0,0,64,64}; ClearImage(&clear,0,0,0);
            quad(sources[i],sources[i].w,10);
            readTile(10,sources[i].w,actual);
            if (glyph<4) {
                TextureRequest to=glyph==slots[i]?sources[i]:expected(sources[i],styles[i],glyph);
                identity();
                int saved=drawSprite; drawSprite=0;
                quad(to,sources[i].w,30); readTile(30,sources[i].w,reference);
                drawSprite=saved;
                if (memcmp(actual,reference,sources[i].w*12*2)) {
                    fprintf(stderr,"source %d slot %d style %d glyph %d scale %d\n",i,slots[i],styles[i],glyph,scale);
                    for (int k=0;k<sources[i].w*12;++k) if (actual[k]!=reference[k]) {
                        fprintf(stderr,"pixel %d actual %04x expected %04x\n",k,actual[k],reference[k]); break;
                    }
                }
                CHECK(!memcmp(actual,reference,sources[i].w*12*2));
            } else {
                for (int y=0;y<12;++y) for (int x=0;x<sources[i].w;++x)
                    CHECK(actual[y*sources[i].w+x]==promptTexel(glyph,x,y));
                if (scale) {
                    uint32_t pixels[48*48]; gvkHiRead(0,pixels,40,80,48,48);
                    for (int y=0;y<48;++y) for (int x=0;x<48;++x) {
                        unsigned c=promptTexel(glyph,x/4,y/4);
                        /* word15 expands five bits by bit replication. */
                        unsigned rgba=(((c&31)<<3)|((c&31)>>2))|(((((c>>5)&31)<<3)|(((c>>5)&31)>>2))<<8)|
                             (((((c>>10)&31)<<3)|(((c>>10)&31)>>2))<<16);
                        CHECK((pixels[y*48+x]&0xffffff)==rgba);
                    }
                }
            }
        }
    }
    /* The battle's fourth tilted icon is an arrow, never a face button. */
    if (!menu) {
        TextureRequest arrow={143,32384,0,116,128,12,12,0,0},to;
        for (int i=0;i<4;++i) mapping[i]=PAD_GLYPH_A;
        CHECK(!texturePromptRemap(arrow,&to));
    }
    /* Remapping leaves the native source atlas and palette bytes unchanged. */
    RECT native=menu?(RECT){448,120,24,12}:(RECT){1000,128,24,24};
    uint16_t original[24*24]; StoreImage(&native,(uint32_t*)original);
    for (int y=0;y<native.h;++y)
        CHECK(!memcmp(original+y*native.w,image+(native.y+y)*1024+native.x,native.w*2));
    free(image); gpuShutdown(); SDL_Quit();
    printf("prompt GPU (%s %s): %d checks passed\n",argv[2],argv[3],checks);
    return 0;
}
