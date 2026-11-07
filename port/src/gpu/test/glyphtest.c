/* Packed dialog planes, source lifetime, authored indices and cache bounds. */
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pc_glyph.h"
#include "test/env.h"
#include "glyph_fixture.h"
static int checks;
#define CHECK_DETAIL SDL_GetError()
#include "test/check.h"
/* gp0.c's texture cache hook (vram.c): no GP0 here. */
void gp0VramChanging(VRect r) { (void)r; }
/* No GPU-side draw is queued by this CPU texture fixture. */
void gvkUpload(VRect r,const uint16_t* src,int pitch) { (void)r;(void)src;(void)pitch; }
void gvkReadback(VRect r, uint16_t* dst, int pitch) {
    for (int y = r.y0; y <= r.y1; y++)
        memcpy(dst+(y-r.y0)*pitch,&vramShadow[y][r.x0],(size_t)(r.x1-r.x0+1)*2);
}
void gpuLog(const char* fmt,...) { va_list a; va_start(a,fmt); vfprintf(stderr,fmt,a); va_end(a); fputc('\n',stderr); }
static void upload(int x,int y,int w,int h,const uint16_t* data) {
    for (int row=0;row<h;++row) memcpy(&vramShadow[y+row][x],data+row*w,(size_t)w*2);
    vramMarkUploaded((VRect){x,y,x+w-1,y+h-1},0);
    textureGlyphUpload(x,y,w,h,data);
}
static const uint8_t* pixel(TextureSample s,int x,int y,int subx,int suby) {
    return s.rgba+((y*s.scale+suby)*s.w*s.scale+x*s.scale+subx)*4;
}
static int textOn(void) { return 1; }
static int nearestBlocks(TextureSample s,const DecodedTexture* d) {
    for(int y=0;y<s.h;y++)for(int x=0;x<s.w;x++) {
        const uint8_t* native=d->rgba+((size_t)y*d->request.w+x)*4;
        for(int sy=0;sy<s.scale;sy++)for(int sx=0;sx<s.scale;sx++)
            if(memcmp(pixel(s,x,y,sx,sy),native,4))return 0;
    }
    return 1;
}
int main(int argc,char** argv) {
    CHECK(argc>=3); CHECK(testSetEnv("XG_SETTINGS",argv[2])==0);
    if (argc<=3) CHECK(testSetEnv("XG_TEXTURE_PACK","none")==0); /* the first pass: not the default library beside the executable */ CHECK(SDL_Init(0));
    if (argc>3) {
        CHECK(textureGlyphsEnabled()); CHECK(!strcmp(texturePackRoot(),argv[1]));
        SDL_Quit(); puts("glyph settings reloaded"); return 0;
    }
    CHECK(!textureGlyphsEnabled()); vramReset(); textureCacheReset(); textureGlyphReset();
    char hdRoot[3200];snprintf(hdRoot,sizeof(hdRoot),"%s/default-hd",argv[1]);
    CHECK(testSetEnv("XG_GLYPH_PACKS",hdRoot)==0);
    char dump[3200]; snprintf(dump,sizeof(dump),"%s/dump-é",argv[1]); textureDumpSetRoot(dump);
    uint16_t image[6*13],palette[16]={0}; uint8_t even[8*13],odd[8*13];
    glyphImage(image,6,even,odd);
    for (int i=1;i<16;++i) palette[i]=(uint16_t)i;
    palette[13]=0x801f; palette[14]=31<<5;
    pcGlyphDialog(image,6,8,0); pcGlyphDialog(image,6,8,1);
    upload(128,256,6,13,image); upload(976,480,16,1,palette);
    TextureRequest r={18,(uint16_t)(61|480<<6),0,0,0,24,13,0,0};
    const DecodedTexture* d=textureCacheGet(r); CHECK(d);
    TextureSample s; CHECK(!textureGlyphSample(d,&s));
    TextureGlyphStats stats; textureGlyphStats(&stats); CHECK(stats.ramRecords==2 && stats.pieces==2);
    textureSetGlyphs(1); CHECK(textureGlyphSample(d,&s) && s.scale==8 && s.w==24);
    CHECK(nearestBlocks(s,d));
    CHECK(pixel(s,1,0,0,0)[0]==255 && pixel(s,1,0,0,0)[3]==128);
    CHECK(pixel(s,1,1,7,7)[1]==255 && pixel(s,1,1,7,7)[3]==255);
    uint64_t previous=s.revision; CHECK(textureGlyphSample(d,&s) && s.revision==previous);
    /* Default HD artwork is independent of the general texture pack. User
     * glyphs still take priority, and disabling the library restores nearest. */
    uint64_t hdKey=textureGlyphKey(even,8,13,4);uint8_t hd[8*13*64];memset(hd,1,sizeof(hd));
    CHECK(glyphPng(hdRoot,hdKey,8,13,hd,8));texturePackReload();
    CHECK(!texturePackEnabled() && textureGlyphSample(d,&s));
    CHECK(pixel(s,1,1,0,0)[0]==255 && pixel(s,1,1,0,0)[3]==128);
    const uint8_t* hdIndices;int hdScale;
    CHECK(textureHDGlyph(hdKey,8,13,4,&hdIndices,&hdScale) && hdScale==8 && hdIndices[0]==1);
    CHECK(!textureHDGlyph(hdKey,8,13,1,&hdIndices,&hdScale));
    char priority[3200];snprintf(priority,sizeof(priority),"%s/hd-override",argv[1]);memset(hd,2,sizeof(hd));
    CHECK(glyphPng(priority,hdKey,8,13,hd,8));texturePackSetRoot(priority);
    CHECK(textureGlyphSample(d,&s) && pixel(s,1,1,0,0)[1]==255 && pixel(s,1,1,0,0)[3]==255);
    CHECK(testSetEnv("XG_GLYPH_PACKS","none")==0);texturePackSetRoot("");
    CHECK(textureGlyphSample(d,&s) && nearestBlocks(s,d));previous=s.revision;
    /* Palette changes never change source indices or their packed planes. */
    palette[13]=0x8000; palette[14]=0; upload(976,480,16,1,palette); d=textureCacheGet(r);
    CHECK(textureGlyphSample(d,&s) && s.revision!=previous);
    CHECK(nearestBlocks(s,d));
    CHECK(pixel(s,1,0,0,0)[0]==0 && pixel(s,1,0,0,0)[3]==128);
    CHECK(pixel(s,1,1,7,7)[3]==0);
    /* A cropped request keeps logical UV origin and validates the whole glyph. */
    TextureRequest crop=r; crop.u=crop.v=1; crop.w=crop.h=1;
    CHECK(textureGlyphSample(textureCacheGet(crop),&s) && s.u==1 && s.v==1 && s.w==1);
    /* A 3x authored map replaces only low two bits; high-line bits survive. */
    uint64_t key=textureGlyphKey(even,8,13,4);
    uint8_t pack[24*39]; memset(pack,2,sizeof(pack));
    CHECK(glyphPng(argv[1],key,8,13,pack,3)); texturePackSetRoot(argv[1]);
    palette[14]=31<<10; upload(976,480,16,1,palette); d=textureCacheGet(r);
    CHECK(textureGlyphSample(d,&s)); CHECK(pixel(s,1,1,0,0)[2]==255 && pixel(s,1,1,0,0)[3]==255);
    previous=s.revision; memset(pack,1,sizeof(pack)); CHECK(glyphPng(argv[1],key,8,13,pack,3));
    CHECK(textureGlyphSample(d,&s) && s.revision==previous);
    texturePackReload(); CHECK(textureGlyphSample(d,&s) && s.revision!=previous && pixel(s,1,1,0,0)[3]==128);
    /* Sharing a key with a full 4-bit atlas must not bypass the 2-bit limit. */
    memset(pack,5,sizeof(pack)); CHECK(glyphPng(argv[1],key,8,13,pack,3)); texturePackReload();
    const uint8_t* indices; int scale;
    CHECK(texturePackGlyph(key,8,13,16,&indices,&scale) && scale==3);
    CHECK(!texturePackGlyph(key,8,13,4,&indices,&scale));
    CHECK(textureGlyphSample(d,&s)); CHECK(nearestBlocks(s,d)); /* safe nearest fallback */
    texturePackSetRoot("");
    /* Clears forget one RAM plane; overwriting VRAM retires its old source. */
    pcGlyphClearRAM(image,sizeof(image),1);
    for (int i=0;i<6*13;++i) image[i]&=0xcccc;
    upload(128,256,6,13,image); d=textureCacheGet(r); CHECK(textureGlyphSample(d,&s));
    textureGlyphStats(&stats); CHECK(stats.ramRecords==1 && stats.pieces==1 && stats.invalidated>=1);
    CHECK(pixel(s,1,1,0,0)[0]==(12<<3 | 12>>2));
    pcGlyphClearRAM(image,sizeof(image),3); memset(image,0,sizeof(image)); upload(128,256,6,13,image);
    CHECK(!textureGlyphSample(textureCacheGet(r),&s));
    /* Atlas records remain valid across CLUT changes, and are retired by a
     * native texel change even outside a cropped draw's rectangle. */
    image[0]=0x1211; upload(128,256,6,13,image); pcGlyphTile(18,0,0,8,13);
    CHECK(textureGlyphSample(textureCacheGet(r),&s));
    image[12*6+1]=1; upload(128,256,6,13,image);
    CHECK(!textureGlyphSample(textureCacheGet(crop),&s));
    /* Windowed/wrapped and direct samples safely retain the original path. */
    TextureRequest window=r; window.window=1; CHECK(!textureGlyphSample(textureCacheGet(window),&s));
    DecodedTexture direct={0}; CHECK(!textureGlyphSample(&direct,&s));
    /* Wide dialog characters keep their last column and stable relocation key. */
    textureGlyphReset(); memset(image,0,sizeof(image));
    image[6*6+2]=0x1000;
    pcGlyphDialog(image,6,12,0); upload(128,256,6,13,image);
    TextureRequest wide=r; wide.w=12;
    CHECK(textureGlyphSample(textureCacheGet(wide),&s));
    CHECK(pixel(s,11,6,4,4)[0]==8); /* the last column retains palette index 1 */
    CHECK(pixel(s,11,6,0,0)[0]==8); /* complete native pixel, including corners */
    uint8_t wideIndices[12*13]={0}; wideIndices[6*12+11]=1;
    uint64_t wideKey=textureGlyphKey(wideIndices,12,13,4);
    upload(192,256,6,13,image); wide.tpage=19;
    CHECK(textureGlyphSample(textureCacheGet(wide),&s) && pixel(s,11,6,4,4)[0]==8);
    CHECK(textureCacheGet(wide)->texelKey==wideKey);
    /* Proportional glyphs use independent keys at arbitrary pixel offsets.
     * Their authored maps OR together where their native outlines overlap. */
    textureGlyphReset(); uint8_t a[39],b[39],authored[39*9];
    glyphProportionalImage(image,6,a,b);
    pcGlyphProportional(image,6,1,3,0,a); pcGlyphProportional(image,6,3,3,0,b);
    palette[13]=31; palette[14]=31<<5; palette[15]=31<<10;
    upload(976,480,16,1,palette); upload(128,256,6,13,image);
    uint64_t akey=textureGlyphKey(a,3,13,4),bkey=textureGlyphKey(b,3,13,4);
    memset(authored,1,sizeof(authored)); CHECK(glyphPng(argv[1],akey,3,13,authored,3));
    memset(authored,2,sizeof(authored)); CHECK(glyphPng(argv[1],bkey,3,13,authored,3));
    texturePackSetRoot(argv[1]); d=textureCacheGet(r); CHECK(textureGlyphSample(d,&s));
    CHECK(pixel(s,1,1,0,0)[0]==255); /* A */
    CHECK(pixel(s,4,1,0,0)[1]==255); /* B */
    CHECK(pixel(s,3,1,0,0)[2]==255); /* A | B */
    crop=r; crop.u=3; crop.v=1; crop.w=crop.h=1;
    CHECK(textureGlyphSample(textureCacheGet(crop),&s) && pixel(s,0,0,7,7)[2]==255);
    /* Relocation keeps both independent authored keys. */
    upload(192,256,6,13,image); TextureRequest moved=r; moved.tpage=19;
    CHECK(textureGlyphSample(textureCacheGet(moved),&s) && pixel(s,3,1,0,0)[2]==255);
    /* Corrupt B outside A's bounds and a cropped request. The whole plane's
     * composition retires, including A selected before B was checked. */
    image[12*6+1]|=0x20; upload(128,256,6,13,image);
    CHECK(!textureGlyphSample(textureCacheGet(r),&s));
    /* The same native pixels with a different glyph decomposition must not
     * reuse A's old authored key or an unchanged-line cache entry. */
    glyphProportionalImage(image,6,a,b); uint8_t combined[5*13];
    for (int y=0;y<13;++y) for (int x=0;x<5;++x)
        combined[y*5+x]=(uint8_t)((image[y*6+(x+1)/4]>>(((x+1)%4)*4))&3);
    pcGlyphProportional(image,6,1,5,0,combined); upload(128,256,6,13,image);
    CHECK(textureGlyphSample(textureCacheGet(r),&s) && pixel(s,1,1,0,0)[1]==255);
    /* Column clears retire just intersecting RAM records in that plane. */
    pcGlyphClearColumns(image,6,1,0,23); textureGlyphStats(&stats); CHECK(stats.ramRecords==2);
    pcGlyphClearColumns(image,6,0,1,2); textureGlyphStats(&stats); CHECK(stats.ramRecords==1);
    pcGlyphClearRAM(image,sizeof(image),1); textureGlyphStats(&stats); CHECK(stats.ramRecords==0);
    /* A full 12-column letter plus its outlines spans 14 pixels and five
     * words when it starts at pixel 7. Its final column must survive. */
    textureGlyphReset(); for (int i=0;i<6*13;++i) image[i]=0xcccc;
    uint8_t fullGlyph[14*13]={0}; fullGlyph[6*14+13]=1;
    image[6*6+5]|=1; pcGlyphProportional(image,6,7,14,0,fullGlyph);
    upload(128,256,6,13,image); texturePackSetRoot("");
    CHECK(textureGlyphSample(textureCacheGet(r),&s) && pixel(s,20,6,7,7)[0]==255);
    /* 8-bit atlas glyphs preserve the complete index, including values >15. */
    textureGlyphReset(); uint16_t atlas[4*13]={0}, palette8[256]={0};
    atlas[4*6+3]=0xc800; palette8[200]=31<<10;
    upload(256,256,4,13,atlas); upload(768,480,256,1,palette8);
    pcGlyphTile(148,0,0,8,13);
    TextureRequest eight={148,(uint16_t)(48|480<<6),0,0,0,8,13,0,0};
    CHECK(textureGlyphSample(textureCacheGet(eight),&s) && pixel(s,7,6,4,4)[2]==255);
    /* Overlays are found by content (xg-j85.22): more than eight text draws
     * keep their compositions, and so does a re-decode of identical indices
     * and CLUT (battle re-decodes its atlases every frame). A CLUT change
     * still composes anew. */
    textureGlyphReset(); uint16_t line[48*13]={0}; uint64_t revs[12],decodes[12];
    for (int k=0;k<12;++k) line[6*48+k*4+1]=(uint16_t)(0xc8|(k+1)<<8);
    upload(256,256,48,13,line);
    for (int k=0;k<12;++k) pcGlyphTile(148,k*8,0,8,13);
    textureGlyphStats(&stats); CHECK(stats.pieces==12);
    for (int pass=0;pass<3;++pass) {
        if (pass==1) upload(768,480,256,1,palette8); /* the same CLUT again */
        if (pass==2) { palette8[200]=31; upload(768,480,256,1,palette8); }
        for (int k=0;k<12;++k) {
            TextureRequest q=eight; q.u=k*8;
            const DecodedTexture* dq=textureCacheGet(q);
            CHECK(dq && textureGlyphSample(dq,&s));
            if (pass==0) { revs[k]=s.revision; decodes[k]=dq->revision; }
            else if (pass==1) CHECK(dq->revision==decodes[k] && s.revision==revs[k]); /* the texels and CLUT unchanged: not even re-decoded (texture.c paletteOnly) */
            else CHECK(s.revision!=revs[k] && pixel(s,2,6,0,0)[0]==255);
        }
    }
    textureGlyphStats(&stats); CHECK(stats.cached==24);
    /* RAM records and native source storage have independent hard bounds. */
    uint16_t* many=calloc(1040*39,2); CHECK(many);
    for (int i=0;i<1040;++i) pcGlyphDialog(many+i*39,3,8,0);
    textureGlyphStats(&stats); CHECK(stats.ramRecords==1024); free(many);
    for (int i=0;i<320;++i) pcGlyphTile((unsigned)(i%16),0,i/16,256,64);
    textureGlyphStats(&stats); CHECK(stats.sourceBytes<=4*1024*1024 && stats.pieces<=2048);
    /* Frame snapshots are at most 4 MiB, even for full 256-square requests. */
    textureGlyphReset(); pcGlyphTile(0,0,0,16,13);
    uint8_t* native=calloc(256*256,1); CHECK(native);
    DecodedTexture full={0}; full.request=(TextureRequest){0,0,0,0,0,256,256,0,0};
    full.depth=4; full.paletteSize=16; full.texels=native;
    for (int i=0;i<6;++i) { full.revision=100000+i; CHECK(textureGlyphSample(&full,&s) && s.scale==4); }
    free(native); textureGlyphStats(&stats); CHECK(stats.cacheBytes<=16*1024*1024 && stats.cached<=4);
    textureDumpShutdown(); CHECK(!textureDumpEnabled());
    /* A GPU-drawn atlas is read back when the text category uses glyphs,
     * even with the legacy switch off (texture_style_text=glyphs). */
    textureGlyphReset(); textureSetGlyphs(0); upload(256,256,4,13,atlas); vramMarkDrawn((VRect){256,256,259,268});
    pcGlyphTile(148,0,0,8,13); textureGlyphStats(&stats); CHECK(stats.pieces==0);
    textureGlyphPolicy(textOn); pcGlyphTile(148,0,0,8,13); textureGlyphStats(&stats); CHECK(stats.pieces==1);
    textureGlyphPolicy(NULL); textureSetGlyphs(1);
    texturePackSetRoot(argv[1]); textureGlyphReset(); textureCacheReset(); texturePackShutdown();
    printf("CPU glyphs: %d checks passed\n",checks); SDL_Quit(); return 0;
}
