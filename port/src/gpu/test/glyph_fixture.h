#ifndef XG_GLYPH_FIXTURE_H
#define XG_GLYPH_FIXTURE_H
#include <inttypes.h>
#include "gpu/texture.h"
static void glyphImage(uint16_t* image,int stride,uint8_t* even,uint8_t* odd) {
    memset(image,0,(size_t)stride*13*2);
    for (int y=0;y<13;++y) for (int x=0;x<8;++x) {
        int e=0,o=3;
        if ((x==1 && y==0)||(x==0 && y==1)) e=1;
        if ((x==1 && y==1)||(x==2 && y==1)||(x==1 && y==2)) e=2;
        even[y*8+x]=(uint8_t)e; odd[y*8+x]=(uint8_t)o;
        image[y*stride+x/4]|=(uint16_t)((e|(o<<2))<<((x%4)*4));
    }
}
/* Every subpixel must be the exact native palette colour, with no edge
 * reconstruction or blended values. Shared by direct and replay checks. */
static int glyphNearestPixels(const uint32_t* pixels,int scale,const uint8_t* even,const uint8_t* odd,const uint16_t* palette) {
    for(int y=0;y<13*scale;y++)for(int x=0;x<8*scale;x++) {
        int i=(y/scale)*8+x/scale;uint16_t word=palette[even[i]|(odd[i]<<2)];
        uint32_t expected=word&0x8000?0x1000000:0;
        for(int k=0;k<3;k++){unsigned c=(word>>(k*5))&31;expected|=((c<<3)|(c>>2))<<(k*8);}
        if((pixels[y*8*scale+x]&0x1ffffff)!=expected)return 0;
    }
    return 1;
}
static int glyphPng(const char* root,uint64_t key,int w,int h,uint8_t* indices,int scale) {
    char path[3400]; snprintf(path,sizeof(path),"%s/%s/glyphs",root,texturePackVersion());
    if (!SDL_CreateDirectory(path)) return 0;
    snprintf(path,sizeof(path),"%s/%s/glyphs/%016" PRIx64 ".indices.png",root,texturePackVersion(),key);
    size_t count=(size_t)w*h*scale*scale;
    uint8_t* grey=malloc(count*4); if (!grey) return 0;
    for (size_t i=0;i<count;++i) { memset(grey+i*4,indices[i],3); grey[i*4+3]=255; }
    SDL_Surface* s=SDL_CreateSurfaceFrom(w*scale,h*scale,SDL_PIXELFORMAT_RGBA32,grey,w*scale*4);
    int ok=s && SDL_SavePNG(s,path); SDL_DestroySurface(s); free(grey); return ok;
}
/* Two three-pixel glyphs at subword positions 1 and 3, with overlapping
 * outlines and different independent keys. The other packed plane is 3. */
static void glyphProportionalImage(uint16_t* image,int stride,uint8_t a[39],uint8_t b[39]) {
    for (int i=0;i<stride*13;++i) image[i]=0xcccc;
    memset(a,0,39); memset(b,0,39);
    for (int y=0;y<3;++y) for (int x=0;x<3;++x) a[y*3+x]=2;
    for (int y=1;y<4;++y) for (int x=0;x<3;++x) b[y*3+x]=2;
    a[4]=1; b[7]=1;
    for (int y=0;y<13;++y) for (int x=0;x<3;++x) {
        int ax=x+1,bx=x+3;
        image[y*stride+ax/4]|=(uint16_t)(a[y*3+x]<<((ax%4)*4));
        image[y*stride+bx/4]|=(uint16_t)(b[y*3+x]<<((bx%4)*4));
    }
}
#endif
