/* Real LIBGPU writes/copies/readbacks and draw-time dump observation. */
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <stdio.h>
#include <string.h>
#include "common.h"
#include "psyq.h"
#include "gpu/texture.h"
#include "test/env.h"

static int checks;
#include "test/check.h"
int gpuInit(SDL_Window* window);
void gpuShutdown(void);
void SetTile(void* p);
static void quad(POLY_FT4* p, int tpage, int clut) {
    SetPolyFT4(p); SetShadeTex(p, 1);
    p->x0 = p->x2 = 20; p->x1 = p->x3 = 36;
    p->y0 = p->y1 = 20; p->y2 = p->y3 = 36;
    p->u0 = p->u2 = p->v0 = p->v1 = 0;
    p->u1 = p->u3 = p->v2 = p->v3 = 16;
    p->tpage = (u16)tpage; p->clut = (u16)clut;
}
int main(int argc, char** argv) {
    testSetEnv("XG_SETTINGS", "none");
    if (argc > 1) CHECK(SDL_SetEnvironmentVariable(SDL_GetEnvironment(), "XG_TEXDUMP", argv[1], true));
    CHECK(SDL_Init(SDL_INIT_VIDEO)); CHECK(gpuInit(NULL));
    if (argc > 1) CHECK(textureDumpEnabled());
    DRAWENV env; SetDefDrawEnv(&env, 0,0,320,224); PutDrawEnv(&env);
    uint16_t packed[4 * 16], palette[16];
    for (int i = 0; i < 64; ++i) packed[i] = 0x4321;
    for (int i = 0; i < 16; ++i) palette[i] = (u16)(i | i << 5 | (i & 1 ? 0x8000 : 0));
    RECT tex = {640,256,4,16}, pal = {976,480,16,1};
    LoadImage(&tex, packed); LoadImage(&pal, palette);
    TextureRequest r = {26, (uint16_t)GetClut(976,480), 0, 0,0,16,16, 0, 0};
    const DecodedTexture* d = textureCacheGet(r); CHECK(d && d->texels[0] == 1);
    uint64_t tk = d->texelKey, pk = d->paletteKey, revision = d->revision;
    POLY_FT4 p; quad(&p, r.tpage, r.clut); DrawPrim(&p);
    CHECK(textureCacheGet(r)->revision == revision);
    /* Texture + CLUT relocation through actual CPU copy path. */
    MoveImage(&tex,704,256); MoveImage(&pal,32,496);
    TextureRequest moved = {27,(uint16_t)GetClut(32,496),0,0,0,16,16,0,0};
    d = textureCacheGet(moved); CHECK(d && d->texelKey == tk && d->paletteKey == pk);
    quad(&p,moved.tpage,moved.clut); p.x0 = p.x2 = 40; p.x1 = p.x3 = 56; DrawPrim(&p);
    /* Palette fade changes decoded colour only; texel identity stays. */
    palette[1] = 0x7c00; RECT movedPal = {32,496,16,1}; LoadImage(&movedPal,palette);
    d = textureCacheGet(moved); CHECK(d && d->texelKey == tk && d->paletteKey != pk && d->rgba[2] == 255);
    quad(&p,moved.tpage,moved.clut); DrawPrim(&p);
    /* Clipped sprites dump only their used patch (or a covering upload). */
    env.tpage = moved.tpage; PutDrawEnv(&env);
    SPRT sprite; SetSprt(&sprite); SetShadeTex(&sprite,1);
    sprite.x0 = 318; sprite.y0 = 50; sprite.u0 = sprite.v0 = 0;
    sprite.w = 8; sprite.h = 8; sprite.clut = moved.clut; DrawPrim(&sprite);
    uint32_t pixel = 0; RECT one = {20,20,1,1}; StoreImage(&one,&pixel);
    CHECK((pixel & 0xffff) == 0x7c00);
    /* Copying over an existing cache entry invalidates it, not just the
     * first lookup of a new destination. */
    packed[0] = 0x4322; LoadImage(&tex,packed); MoveImage(&tex,704,256);
    d = textureCacheGet(moved); CHECK(d && d->texels[0] == 2 && d->texelKey != tk);
    /* Fill invalidation for indexed texels. */
    RECT movedTex = {704,256,4,16}; ClearImage(&movedTex,248,0,0);
    d = textureCacheGet(moved); CHECK(d && d->texelKey != tk && d->texels[0] == 15);
    /* A draw into a palette is read back before the next decode. */
    SetDefDrawEnv(&env,0,0,1024,512); PutDrawEnv(&env);
    TILE tile; SetTile(&tile); setRGB0(&tile,0,248,0);
    tile.x0 = 33; tile.y0 = 496; tile.w = tile.h = 1; DrawPrim(&tile);
    d = textureCacheGet(moved); CHECK(d && d->palette[1] == 0x3e0);
    /* An uploaded direct texture is eligible; draw/readback/copy makes it
     * a capture, including the CPU copy path after StoreImage. */
    uint16_t direct[16*16]; for (int i = 0; i < 256; ++i) direct[i] = 0x7fff;
    RECT dr = {960,256,16,16}; LoadImage(&dr,direct);
    TextureRequest directRequest = {15|16|256,0,0,0,0,16,16,0,0};
    CHECK(textureCacheGet(directRequest));
    tile.x0 = 960; tile.y0 = 256; tile.w = tile.h = 16; DrawPrim(&tile);
    CHECK(!textureCacheGet(directRequest));
    StoreImage(&dr,(u32*)direct); CHECK(direct[0] == 0x3e0);
    CHECK(!textureCacheGet(directRequest));
    MoveImage(&dr,928,256); directRequest.u = 32; directRequest.tpage = 14|16|256;
    CHECK(!textureCacheGet(directRequest));
    /* Masked uploads cannot clear rendered provenance. */
    gp0SetMaskBits(2); RECT dst = {928,256,16,16}; LoadImage(&dst,direct);
    CHECK(!textureCacheGet(directRequest)); gp0SetMaskBits(0);
    /* The same captured pixels stay protected in an unmasked round trip
     * at exact 1x too. Genuine fresh content restores upload eligibility. */
    LoadImage(&dst,direct); CHECK(!textureCacheGet(directRequest));
    for (int i = 0; i < 256; ++i) direct[i] = 0x7fff;
    LoadImage(&dst,direct); CHECK(textureCacheGet(directRequest));
    /* CPU round trips that retain the high-resolution capture must not
     * relabel that detail as a replaceable uploaded texture. */
    CHECK(gvkHiConfigure(4,HIRES_COLOR_DITHER_1X) == 4);
    tile.x0 = 960; tile.y0 = 256; DrawPrim(&tile); StoreImage(&dr,(u32*)direct);
    LoadImage(&dr,direct); directRequest.tpage = 15|16|256; directRequest.u = 0;
    CHECK(!textureCacheGet(directRequest));
    RECT stashDst = {896,256,16,16}; LoadImage(&stashDst,direct); directRequest.tpage = 14|16|256;
    CHECK(!textureCacheGet(directRequest));
    gpuVBlank();
    TextureStats st; textureCacheStats(&st); CHECK(st.hits > 0 && st.decodes > 0 && st.renderedSkips >= 4);
    printf("GPU textures: %d checks passed\n",checks);
    gpuShutdown(); SDL_Quit();
    return 0;
}
