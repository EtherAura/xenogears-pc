/* Real text sprites, separate packed lines, CLUTs, snapshots and class policy. */
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <stdio.h>
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include "common.h"
#include "psyq.h"
#include "pc_wide.h"
#include "pc_glyph.h"
#include "test/env.h"
#include "glyph_fixture.h"
int gpuInit(SDL_Window* window);
void gpuShutdown(void);
static int checks;
#include "test/check.h"
/* Colour and PS1 mask, excluding display-only seam coverage. */
static uint32_t pixel(int x,int y) { uint32_t p=0; gvkHiRead(0,&p,x,y,1,1); return p & 0x1ffffff; }
static void text(SPRT* p,int x,int y,int clut,int kind) {
    SetSprt(p); SetShadeTex(p,1); p->x0=x; p->y0=y; p->u0=p->v0=0;
    p->w=8; p->h=13; p->clut=clut; pcTextureClass(p,kind); DrawPrim(p);
}
int main(int argc,char** argv) {
    CHECK(argc>=2); CHECK(testSetEnv("XG_SETTINGS","none")==0); CHECK(SDL_Init(SDL_INIT_VIDEO)); CHECK(gpuInit(NULL));
    DRAWENV env; SetDefDrawEnv(&env,0,0,320,224); env.tpage=18; PutDrawEnv(&env);
    hiresSetUpscale(4); hiresSetPrecision(0); hiresSetSeamGuard(0); hiresSetColor(HIRES_COLOR_RAW);
    CHECK(gvkHiConfigure(4,HIRES_COLOR_RAW)==4);
    uint16_t image[6*13],palette[16]={0}; uint8_t even[8*13],odd[8*13];
    glyphImage(image,6,even,odd); for (int i=1;i<16;++i) palette[i]=i;
    palette[13]=0x801f; palette[14]=31<<5;
    pcGlyphDialog(image,6,8,0); pcGlyphDialog(image,6,8,1);
    RECT rect={128,256,6,13},pal={976,480,16,1}; LoadImage(&rect,image); LoadImage(&pal,palette);
    int clut=GetClut(976,480); SPRT p;
    /* Starting discovery after upload still sees unchanged, already-built text
     * with Enhanced text off, including on the exact 1x renderer. */
    uint64_t key=textureGlyphKey(even,8,13,4);
    char dump[3200],file[3300]; snprintf(dump,sizeof(dump),"%s/discovery-é",argv[1]);
    textureDumpSetRoot(dump); CHECK(gvkHiConfigure(0,HIRES_COLOR_RAW)==0);
    text(&p,10,20,clut,PC_TEX_TEXT);
    snprintf(file,sizeof(file),"%s/%s/glyphs/%016" PRIx64 ".indices.png",dump,texturePackVersion(),key);
    SDL_PathInfo info; CHECK(SDL_GetPathInfo(file,&info) && info.type==SDL_PATHTYPE_FILE);
    textureDumpSetRoot(""); CHECK(gvkHiConfigure(4,HIRES_COLOR_RAW)==4);
    text(&p,20,20,clut,PC_TEX_TEXT); CHECK(pixel(84,84)==0xff00); /* default */
    textureSetGlyphs(1); textureSetGlyphSmooth(0); /* the hard edges; smooth ones below */
    text(&p,40,20,clut,PC_TEX_TEXT);
    text(&p,60,20,clut,PC_TEX_HUD);
    CHECK(pixel(164,80)==0x10000ff && pixel(164,84)==0xff00 && pixel(167,87)==0xff00);
    CHECK(pixel(244,84)==0xff00); /* metadata does not change other classes */
    uint32_t exact=0; RECT one={41,21,1,1}; StoreImage(&one,&exact); CHECK((exact&65535)==31<<5);
    /* Legacy filter settings cannot smooth either field or battle glyphs.
     * Check complete native pixel blocks at 4x and the user's 12x scale. */
    uint32_t blocks[8*13*12*12];
    for(int scale=4;scale<=12;scale+=8) {
        CHECK(gvkHiConfigure(scale,HIRES_COLOR_RAW)==scale);
        for(int filter=TEXTURE_FILTER_LINEAR;filter<=TEXTURE_FILTER_EDGE;filter++) {
            textureSetFilter(PC_TEX_TEXT,filter);
            CHECK(textureFilter(PC_TEX_TEXT)==TEXTURE_FILTER_NEAREST && textureFilter(PC_TEX_BATTLE_TEXT)==TEXTURE_FILTER_NEAREST);
            for(int kind=0;kind<2;kind++) {
                text(&p,40,20,clut,kind?PC_TEX_BATTLE_TEXT:PC_TEX_TEXT);
                CHECK(gvkHiRead(0,blocks,40*scale,20*scale,8*scale,13*scale));
                CHECK(glyphNearestPixels(blocks,scale,even,odd,palette));
            }
        }
    }
    textureSetFilter(PC_TEX_TEXT,TEXTURE_FILTER_NEAREST);
    CHECK(gvkHiConfigure(4,HIRES_COLOR_RAW)==4);
    /* Smooth edges (text_smooth): the edge pixels blend face, outline and
     * background, so the glyph is no longer palette colours in whole native
     * blocks; drawn again with it off, it is. Both on cleared VRAM. */
    textureSetGlyphSmooth(1); text(&p,140,60,clut,PC_TEX_TEXT);
    CHECK(gvkHiRead(0,blocks,140*4,60*4,8*4,13*4)); CHECK(!glyphNearestPixels(blocks,4,even,odd,palette));
    textureSetGlyphSmooth(0); text(&p,160,60,clut,PC_TEX_TEXT);
    CHECK(gvkHiRead(0,blocks,160*4,60*4,8*4,13*4)); CHECK(glyphNearestPixels(blocks,4,even,odd,palette));
    /* Both lines can use different live CLUTs without changing packed data. */
    text(&p,80,20,clut,PC_TEX_TEXT);
    palette[13]=0x83e0; palette[14]=0; LoadImage(&pal,palette);
    text(&p,100,20,clut,PC_TEX_TEXT);
    CHECK(pixel(324,80)==0x10000ff && pixel(327,87)==0xff00);
    CHECK(pixel(404,80)==0x100ff00 && pixel(407,87)==0);
    pal.x=944; for (int i=0;i<16;++i) palette[i]=((i>>2)==3)?31<<10:0;
    LoadImage(&pal,palette); text(&p,120,20,GetClut(944,480),PC_TEX_TEXT);
    CHECK(pixel(484,84)==0xff0000 && pixel(487,87)==0xff0000);
    /* Stable glyph key, authored 3x indices, and queued reload snapshots. */
    pal.x=976; memset(palette,0,sizeof(palette)); palette[13]=0x801f; palette[14]=31<<5;
    LoadImage(&pal,palette);
    uint8_t authored[24*39]; memset(authored,2,sizeof(authored));
    CHECK(glyphPng(argv[1],key,8,13,authored,3)); texturePackSetRoot(argv[1]);
    text(&p,140,20,clut,PC_TEX_TEXT);
    memset(authored,1,sizeof(authored)); CHECK(glyphPng(argv[1],key,8,13,authored,3)); texturePackReload();
    text(&p,160,20,clut,PC_TEX_TEXT);
    CHECK(pixel(564,84)==0xff00 && pixel(644,84)==0x10000ff);
    /* Authored glyphs also retain discrete nearest samples. */
    textureSetFilter(PC_TEX_TEXT,TEXTURE_FILTER_LINEAR); text(&p,180,20,clut,PC_TEX_TEXT);
    CHECK(pixel(724,84)==0x10000ff);
    textureSetFilter(PC_TEX_TEXT,TEXTURE_FILTER_NEAREST);
    texturePackSetRoot("");
    /* Retire overwritten even-plane glyphs while the other line remains. */
    for (int i=0;i<6*13;++i) image[i]&=0xcccc;
    pcGlyphClearRAM(image,sizeof(image),1); LoadImage(&rect,image);
    text(&p,200,20,clut,PC_TEX_TEXT); CHECK(pixel(804,84)==0);
    TextureGlyphStats stats; textureGlyphStats(&stats); CHECK(stats.invalidated>=1);
    /* Proportional glyphs start within a word and overlap their outlines.
     * Authored per-glyph indices combine without erasing the other line. */
    textureGlyphReset(); uint8_t a[39],b[39],maps[39*9];
    glyphProportionalImage(image,6,a,b);
    pcGlyphProportional(image,6,1,3,0,a); pcGlyphProportional(image,6,3,3,0,b);
    palette[13]=31; palette[14]=31<<5; palette[15]=31<<10;
    LoadImage(&rect,image); LoadImage(&pal,palette);
    memset(maps,1,sizeof(maps)); CHECK(glyphPng(argv[1],textureGlyphKey(a,3,13,4),3,13,maps,3));
    memset(maps,2,sizeof(maps)); CHECK(glyphPng(argv[1],textureGlyphKey(b,3,13,4),3,13,maps,3));
    texturePackSetRoot(argv[1]); text(&p,20,60,clut,PC_TEX_TEXT);
    CHECK(pixel(84,244)==0xff && pixel(96,244)==0xff00 && pixel(92,244)==0xff0000);
    text(&p,40,60,clut,PC_TEX_HUD); CHECK(pixel(172,244)==0xff00);
    one.x=23; one.y=61; StoreImage(&one,&exact); CHECK((exact&65535)==31<<5);
    /* Recolour the same indexed composition using the current live CLUT. */
    palette[15]=0x83e0; LoadImage(&pal,palette); text(&p,60,60,clut,PC_TEX_TEXT);
    CHECK(pixel(252,244)==0x100ff00);
    texturePackSetRoot("");
    /* The combined appearance is authoritative over legacy glyph flags,
     * including after importing settings with a conflicting old value. */
    textureGlyphReset();glyphImage(image,6,even,odd);pcGlyphDialog(image,6,8,0);
    palette[13]=0x801f;palette[14]=31<<5;LoadImage(&rect,image);LoadImage(&pal,palette);
    CHECK(textureSetAppearance(PC_TEX_TEXT,6));textureSetGlyphs(0);
    text(&p,80,60,clut,PC_TEX_TEXT);CHECK(pixel(324,240)==0x10000ff);
    CHECK(textureSetAppearance(PC_TEX_TEXT,0));textureSetGlyphs(1);
    text(&p,100,60,clut,PC_TEX_TEXT);CHECK(pixel(404,244)==0xff00);
    CHECK(pixel(324,240)==0x10000ff); /* queued enhanced pixels survive */
    /* Real HD artwork retains every authored texel at matching resolution:
     * an alternating 8x map must never acquire blended edge colours. */
    char hdRoot[3200];snprintf(hdRoot,sizeof(hdRoot),"%s/default-hd",argv[1]);
    uint8_t fine[64*104];for(int y=0;y<104;y++)for(int x=0;x<64;x++)fine[y*64+x]=((x+y)&1)?1:2;
    CHECK(glyphPng(hdRoot,textureGlyphKey(even,8,13,4),8,13,fine,8));
    CHECK(testSetEnv("XG_GLYPH_PACKS",hdRoot)==0);texturePackReload();
    CHECK(textureSetAppearance(PC_TEX_TEXT,6));textureSetFilter(PC_TEX_TEXT,TEXTURE_FILTER_LINEAR);
    CHECK(gvkHiConfigure(8,HIRES_COLOR_RAW)==8);text(&p,80,80,clut,PC_TEX_TEXT);
    uint32_t finePixels[64*104];CHECK(gvkHiRead(0,finePixels,640,640,64,104));
    int sharp=1;for(int i=0;i<64*104;i++)if((finePixels[i]&0x1ffffff)!=(fine[i]==1?0x10000ffu:0xff00u))sharp=0;
    CHECK(sharp);
    /* An atlas quad has two native triangle bounds. Its authored 8x canvas
     * must sample exactly like the same unrotated sprite, including the
     * upper-right corner and the shared diagonal (the 28/28 regression). */
    textureGlyphReset(); palette[1]=0x801f;palette[2]=31<<5;LoadImage(&pal,palette);
    uint16_t atlas[3*14]={0}; uint8_t nativeAtlas[10*14],celAtlas[80*112];
    for(int shape=0;shape<2;shape++) {
        int width=shape?10:9, height=shape?14:11;
        memset(atlas,0,sizeof(atlas));
        for(int y=0;y<height;y++)for(int x=0;x<width;x++) {
            nativeAtlas[y*width+x]=(uint8_t)(1+(x+y)%2);
            atlas[y*3+x/4]|=(uint16_t)nativeAtlas[y*width+x]<<((x%4)*4);
        }
        RECT tile={128,256,3,(short)height}; LoadImage(&tile,atlas);
        pcGlyphTile(18,0,0,width,height);
        for(int y=0;y<height*8;y++)for(int x=0;x<width*8;x++) {
            float dx=(x+.5f-width*4)/(width*4-1),dy=(y+.5f-height*4)/(height*4-1);
            float radius=dx*dx+dy*dy;
            celAtlas[y*width*8+x]=(uint8_t)(radius>1?0:radius>.6f||radius<.25f?1:2);
        }
        CHECK(glyphPng(hdRoot,textureGlyphKey(nativeAtlas,width,height,4),width,height,celAtlas,8));
        texturePackReload();
        CHECK(textureGlyphCanvas((TextureRequest){18,(uint16_t)clut,0,0,0,width,height,0,0}));
        for(int scale=4;scale<=12;scale+=4) {
            CHECK(gvkHiConfigure(scale,HIRES_COLOR_RAW)==scale);
            TILE background;memset(&background,0,sizeof(background));setlen(&background,3);background.code=0x60;background.r0=background.g0=background.b0=0;
            background.x0=20;background.y0=100;background.w=50;background.h=height;DrawPrim(&background);
            SetSprt(&p);SetShadeTex(&p,1);p.x0=20;p.y0=100;p.u0=p.v0=0;
            p.w=width;p.h=height;p.clut=clut;pcTextureClass(&p,PC_TEX_BATTLE_TEXT);DrawPrim(&p);
            POLY_FT4 q;SetPolyFT4(&q);SetShadeTex(&q,1);
            q.x0=q.x2=50;q.x1=q.x3=50+width;q.y0=q.y1=100;q.y2=q.y3=100+height;
            q.u0=q.u2=0;q.u1=q.u3=width;q.v0=q.v1=0;q.v2=q.v3=height;
            q.tpage=18;q.clut=clut;pcTextureClass(&q,PC_TEX_BATTLE_TEXT);DrawPrim(&q);
            uint32_t spritePixels[120*168],quadPixels[120*168];
            CHECK(gvkHiRead(0,spritePixels,20*scale,100*scale,width*scale,height*scale));
            CHECK(gvkHiRead(0,quadPixels,50*scale,100*scale,width*scale,height*scale));
            int same=1, differences=0, stride=width*scale;
            for(int i=0;i<width*height*scale*scale;i++) {
                uint32_t wanted=spritePixels[i]&0x1ffffff, actual=quadPixels[i]&0x1ffffff;
                if(wanted==actual)continue;
                differences++;
                /* Float interpolation can choose either adjoining texel at
                 * an exact sampling tie. Permit one display sample at an
                 * edge, never a distant texel repeated into the contour. */
                int neighbour=0;
                for(int dy=-1;dy<=1;dy++)for(int dx=-1;dx<=1;dx++) {
                    int x=i%stride+dx,y=i/stride+dy;
                    if(x<0||x>=stride||y<0||y>=height*scale) { if(actual==0)neighbour=1; }
                    else if((spritePixels[y*stride+x]&0x1ffffff)==actual)neighbour=1;
                }
                if(!neighbour) { if(same)printf("first wrong at %d,%d expected %x got %x\n",i%stride,i/stride,wanted,actual);same=0; }
            }
            printf("atlas quad %dx%d at %dx: %d boundary ties\n",width,height,scale,differences);
            CHECK(same);
        }
    }
    textureGlyphReset();glyphImage(image,6,even,odd);pcGlyphDialog(image,6,8,0);LoadImage(&rect,image);
    CHECK(testSetEnv("XG_GLYPH_PACKS","none")==0);texturePackReload();
    /* The 1x game-readable path stays exact even when enhanced text is on. */
    CHECK(gvkHiConfigure(0,HIRES_COLOR_RAW)==0);
    text(&p,220,20,clut,PC_TEX_TEXT); one.x=221; one.y=21;
    StoreImage(&one,&exact); CHECK((exact&65535)==31<<5);
    printf("GPU glyphs: %d checks passed\n",checks); gpuShutdown(); SDL_Quit(); return 0;
}
