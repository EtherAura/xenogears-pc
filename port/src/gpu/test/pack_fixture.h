#ifndef XG_PACK_FIXTURE_H
#define XG_PACK_FIXTURE_H
#include <inttypes.h>
#include "gpu/texture.h"
/* Procedural PNGs only. A fixture directory is always outside the repo. */
static void packPath(char* path, size_t size, const char* root, const DecodedTexture* d, int index) {
    if (index) snprintf(path,size,"%s/%s/%016" PRIx64 ".indices.png",root,texturePackVersion(),d->texelKey);
    else snprintf(path,size,"%s/%s/%016" PRIx64 "-%016" PRIx64 ".png",root,texturePackVersion(),d->texelKey,d->paletteKey);
}
static int packPng(const char* root, const DecodedTexture* d, int index, int w, int h, uint8_t* pixels) {
    char path[3400]; packPath(path,sizeof(path),root,d,index);
    SDL_Surface* s=SDL_CreateSurfaceFrom(w,h,SDL_PIXELFORMAT_RGBA32,pixels,w*4);
    if (!s) return 0;
    int ok=SDL_SavePNG(s,path); SDL_DestroySurface(s); return ok;
}
static int packDir(const char* root) {
    char path[3400]; snprintf(path,sizeof(path),"%s/%s",root,texturePackVersion());
    return SDL_CreateDirectory(path);
}
#endif
