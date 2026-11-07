/* Cel HUD libraries without a GPU: sprites.txt as other systems save it
 * (xg-j85.14), sprites whose artwork fails or exceeds the budget (xg-j85.20)
 * and the last frame's outcome counts in the status (xg-j85.40). */
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <stdio.h>
#include <string.h>
#include "test/env.h"
#include "gpu/texture.h"
static int checks;
#define CHECK_DETAIL SDL_GetError()
#include "test/check.h"
void gp0VramChanging(VRect r) { (void)r; }
/* No GPU-side draw is queued by this CPU fixture. */
void gvkUpload(VRect r,const uint16_t* src,int pitch) { (void)r;(void)src;(void)pitch; }
void gvkReadback(VRect r,uint16_t* dst,int pitch) {
    for(int y=r.y0;y<=r.y1;++y) memcpy(dst+(y-r.y0)*pitch,&vramShadow[y][r.x0],(size_t)(r.x1-r.x0+1)*2);
}
void gpuLog(const char* fmt,...) { (void)fmt; }
/* The game's vblank count, which the Cel counters follow. */
static uint32_t vblank;
uint32_t hwVSyncCount(void) { return vblank; }

static int writeFile(const char* dir,const char* text) {
    char path[3600]; snprintf(path,sizeof(path),"%s/sprites.txt",dir);
    SDL_IOStream* io=SDL_IOFromFile(path,"wb"); if(!io) return 0;
    int ok=SDL_WriteIO(io,text,strlen(text))==strlen(text); return SDL_CloseIO(io) && ok;
}
static int grey(const char* dir,const char* name,int w,int h,int index) {
    char path[3600]; snprintf(path,sizeof(path),"%s/%s",dir,name);
    SDL_Surface* s=SDL_CreateSurface(w,h,SDL_PIXELFORMAT_RGBA32); if(!s) return 0;
    for(int y=0;y<h;++y) for(int x=0;x<w;++x) {
        uint8_t* p=(uint8_t*)s->pixels+(size_t)y*s->pitch+x*4; p[0]=p[1]=p[2]=(uint8_t)index; p[3]=255;
    }
    int ok=SDL_SavePNG(s,path); SDL_DestroySurface(s); return ok;
}
static int set(const char* root,int scale,char* dir,size_t size) {
    snprintf(dir,size,"%s/cel-%dx/%s",root,scale,texturePackVersion()); return SDL_CreateDirectory(dir);
}
/* A 4-bit decode of `w` x `h` texels of one index at page `tpage`, u v. */
static DecodedTexture texture(uint8_t* texels,int tpage,int u,int v,int w,int h,int index) {
    memset(texels,index,(size_t)w*h);
    DecodedTexture d={0}; d.request=(TextureRequest){(uint16_t)tpage,0,0,u,v,w,h,0,0};
    d.depth=4; d.paletteSize=16; d.texels=texels; return d;
}
int main(int argc,char** argv) {
    CHECK(argc==2); CHECK(testSetEnv("XG_SETTINGS","none")==0); CHECK(SDL_Init(0));
    char a[3200],b[3200],dir[3400];
    snprintf(a,sizeof(a),"%s/crlf",argv[1]); snprintf(b,sizeof(b),"%s/budget",argv[1]);
    /* CRLF with a UTF-8 BOM before the first sprite, comments and a blank
     * line (2x); CR-only lines (4x). One unsafe name and one line with an
     * extra field are counted, not loaded. */
    CHECK(set(a,2,dir,sizeof(dir)));
    CHECK(writeFile(dir,
        "\xEF\xBB\xBF" "4 2560 256 4 4 test\r\n# depth x y w h name\r\n\r\n4 2560 256 4 4 ../../invalid\r\n"
        "4 2564 256 4 4 noart   # its artwork is missing\r\n"));
    CHECK(grey(dir,"test.native.png",4,4,1) && grey(dir,"test.indices.png",8,8,2) && grey(dir,"noart.native.png",4,4,1));
    CHECK(set(a,4,dir,sizeof(dir)));
    CHECK(writeFile(dir,"4 2560 256 4 4 test\r4 2560 256 4 4 test extra\r"));
    CHECK(grey(dir,"test.native.png",4,4,1) && grey(dir,"test.indices.png",16,16,3));
    vblank=1; textureCelSetRoot(a);
    const char* status=textureCelStatus();
    CHECK(!strcmp(status,"3 cel sprite variants ready; unmatched graphics keep native pixels. 2 sprites.txt lines ignored"));
    uint8_t texels[256*256]; TextureSample s;
    DecodedTexture d=texture(texels,26,0,0,4,4,1); /* x 2560, y 256 */
    CHECK(textureCelSample(&d,d.request,2,&s) && s.indices && s.scale==2 && s.keep);
    CHECK(textureCelSample(&d,d.request,4,&s) && s.indices && s.scale==4 && s.indices[0]==3);
    d=texture(texels,26,4,0,4,4,1); CHECK(!textureCelSample(&d,d.request,2,&s)); /* noart */
    d=texture(texels,26,0,0,4,4,2); CHECK(!textureCelSample(&d,d.request,2,&s)); /* changed pixels */
    d=texture(texels,27,0,0,4,4,1); CHECK(!textureCelSample(&d,d.request,2,&s)); /* no sprite there */
    CHECK(!strstr(textureCelStatus(),"Last frame")); /* this frame is still being drawn */
    vblank=2;
    status=textureCelStatus();
    CHECK(strstr(status,". 1 sprite unreadable"));
    CHECK(strstr(status,"Last frame: 2 cel draws, 1 without cel art, 1 changed from the native sprite, 1 unavailable"));
    /* A sprite that cannot fit leaves nothing allocated or counted: forty
     * 1 MiB index maps at 4x reach the 32 MiB budget after thirty, and a
     * small sprite listed after them still loads. */
    CHECK(set(b,4,dir,sizeof(dir)));
    char list[4096]=""; for(int i=0;i<40;++i) strcat(list,"4 0 0 256 256 big\n");
    strcat(list,"4 0 0 16 16 small\n");
    CHECK(writeFile(dir,list));
    CHECK(grey(dir,"big.native.png",256,256,1) && grey(dir,"big.indices.png",1024,1024,1));
    CHECK(grey(dir,"small.native.png",16,16,3) && grey(dir,"small.indices.png",64,64,4));
    textureCelSetRoot(b);
    d=texture(texels,0,0,0,16,16,3);
    CHECK(textureCelSample(&d,d.request,4,&s) && s.w==16 && s.indices[0]==4);
    status=textureCelStatus();
    CHECK(!strncmp(status,"41 cel sprite variants ready",28));
    CHECK(strstr(status,". 10 sprites over the 32 MiB artwork limit keep native pixels"));
    CHECK(!strstr(status,"unreadable"));
    textureCelShutdown(); texturePackShutdown(); SDL_Quit();
    printf("celpacktest: %d checks\n",checks); return 0;
}
