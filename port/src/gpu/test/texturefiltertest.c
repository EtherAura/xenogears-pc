/* Real packet classes, transparent sampling, CLUT revisions and frame arenas. */
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "common.h"
#include "psyq.h"
#include "pc_wide.h"
#include "gpu/texture.h"
#include "test/env.h"

int gpuInit(SDL_Window* window);
void gpuShutdown(void);
void SetTile(void* p);
static int checks;
#include "test/check.h"
static uint32_t pixel(int x, int y) {
    uint32_t p = 0;
    gvkHiRead(0,&p,x,y,1,1);
    /* Colour and PS1 mask, excluding display-only seam coverage. */
    return p & 0x1ffffff;
}
static void quad(POLY_FT4* p, int x, int y, int tp, int clut, int w) {
    SetPolyFT4(p); SetShadeTex(p,1);
    p->x0 = p->x2 = x; p->x1 = p->x3 = x+w;
    p->y0 = p->y1 = y; p->y2 = p->y3 = y+16;
    p->u0 = p->u2 = p->v0 = p->v1 = 0;
    p->u1 = p->u3 = w; p->v2 = p->v3 = 16;
    p->tpage = tp; p->clut = clut;
}
/* Legacy options frozen by the first dropdown change (xg-j85.9): every
 * category draws exactly what it drew before, also after a restart. Each
 * case writes legacy settings, changes one category and records the result;
 * "restart" reads it back in a new process. Expectations follow from the
 * legacy inputs: the pack first, then glyphs or the upscale, then the filter. */
enum { P_FILTER, P_SCALE, P_CEL, P_GLYPHS, P_PACK, P_MENU, P_COUNT };
static void policy(int kind, int* out) {
    out[P_FILTER] = textureFilter(kind); out[P_SCALE] = textureHudScale(kind); out[P_CEL] = textureCelScale(kind);
    out[P_GLYPHS] = textureUseGlyphs(kind);
    out[P_PACK] = !PC_TEX_IS_PORTRAIT(kind) && textureUsePack(kind) && texturePackEnabled();
    /* The menu's row (battle text and Load faces have none of their own). */
    out[P_MENU] = kind == PC_TEX_BATTLE_TEXT || kind == PC_TEX_MENU_THUMB_PORTRAIT ? -1 : textureAppearance(kind);
}
static int same(int kind, int filter, int scale, int glyphs, int pack) {
    int p[P_COUNT]; policy(kind, p);
    return p[P_FILTER] == filter && p[P_SCALE] == scale && !p[P_CEL] && p[P_GLYPHS] == glyphs && p[P_PACK] == pack;
}
static int legacy(const char* settings, int which, int restart) {
    char dir[3000], pack[3100], path[3200];
    SDL_strlcpy(dir, settings, sizeof(dir));
    char* slash = strrchr(dir, '/'); if (slash) *slash = 0; else SDL_strlcpy(dir, ".", sizeof(dir));
    snprintf(pack, sizeof(pack), "%s/pack", dir);
    snprintf(path, sizeof(path), "%s/policy.txt", dir);
    static const char* const cases[] = {
        /* Pack with filters and different field/battle upscales. */
        "texture_filter_3d=linear\ntexture_filter_sprite=edge\ntexture_filter_hud=linear\ntexture_filter_text=edge\n"
        "texture_filter_battle_hud=edge\nhud_upscale=2\nbattle_hud_upscale=4\ntext_glyphs=0\n",
        /* Pack with enhanced text, and a shared battle upscale. */
        "text_glyphs=1\ntexture_filter_text=linear\nhud_upscale=0\nbattle_hud_upscale=2\n"
        "texture_filter_hud=edge\ntexture_filter_battle_hud=linear\n",
        /* No pack: an upscale with a filter, text upscaled in the field only. */
        "texture_pack=none\nhud_upscale=4\nbattle_hud_upscale=0\ntexture_filter_hud=linear\n"
        "texture_filter_text=linear\ntexture_filter_3d=edge\n",
    };
    static const int changed[][2] = {{PC_TEX_3D, 0}, {PC_TEX_BATTLE_HUD, 0}, {PC_TEX_HUD, 1}};
    CHECK(which >= 1 && which <= 3);
    if (!restart) {
        char version[3200]; snprintf(version, sizeof(version), "%s/%s", pack, texturePackVersion());
        CHECK(SDL_CreateDirectory(version));
        FILE* f = fopen(settings, "w"); CHECK(f);
        if (which != 3) fprintf(f, "texture_pack=%s\n", pack);
        fputs(cases[which - 1], f); CHECK(fclose(f) == 0);
    }
    CHECK(testSetEnv("XG_SETTINGS", settings) == 0); CHECK(SDL_Init(0));
    int before[PC_TEX_CLASSES][P_COUNT], after[PC_TEX_CLASSES][P_COUNT];
    for (int k = 0; k < PC_TEX_CLASSES; ++k) policy(k, before[k]);
    if (restart) {
        FILE* f = fopen(path, "r"); CHECK(f);
        for (int k = 0; k < PC_TEX_CLASSES; ++k) for (int i = 0; i < P_COUNT; ++i) CHECK(fscanf(f, "%d", &after[k][i]) == 1);
        fclose(f);
        for (int k = 0; k < PC_TEX_CLASSES; ++k) CHECK(!memcmp(before[k], after[k], sizeof(after[k])));
        texturePackShutdown(); SDL_Quit();
        printf("legacy texture options %d: %d checks after a restart\n", which, checks); return 0;
    }
    CHECK(which == 3 || texturePackEnabled());
    if (which == 1) {
        CHECK(same(PC_TEX_3D, 1, 0, 0, 1) && same(PC_TEX_SPRITE, 2, 0, 0, 1) && same(PC_TEX_HUD, 1, 2, 0, 1));
        CHECK(same(PC_TEX_TEXT, 2, 2, 0, 1) && same(PC_TEX_BATTLE_TEXT, 2, 4, 0, 1));
        CHECK(same(PC_TEX_ICON, 0, 4, 0, 1) && same(PC_TEX_BATTLE_HUD, 2, 4, 0, 1));
    } else if (which == 2) {
        CHECK(same(PC_TEX_TEXT, 0, 0, 1, 1) && same(PC_TEX_BATTLE_TEXT, 0, 0, 1, 1) && same(PC_TEX_HUD, 2, 0, 0, 1));
        CHECK(same(PC_TEX_BATTLE_HUD, 1, 2, 0, 1) && same(PC_TEX_ICON, 0, 2, 0, 1) && same(PC_TEX_3D, 0, 0, 0, 1));
    } else {
        CHECK(same(PC_TEX_3D, 2, 0, 0, 0) && same(PC_TEX_HUD, 1, 4, 0, 0) && same(PC_TEX_TEXT, 1, 4, 0, 0));
        CHECK(same(PC_TEX_BATTLE_TEXT, 1, 0, 0, 0) && same(PC_TEX_ICON, 0, 0, 0, 0) && same(PC_TEX_BATTLE_HUD, 0, 0, 0, 0));
    }
    /* The first dropdown change: only that category changes. */
    int kind = changed[which - 1][0], mode = changed[which - 1][1];
    CHECK(textureSetAppearance(kind, mode));
    for (int k = 0; k < PC_TEX_CLASSES; ++k) {
        policy(k, after[k]);
        int mine = k == kind || (kind == PC_TEX_TEXT && k == PC_TEX_BATTLE_TEXT);
        if (!mine && memcmp(before[k], after[k], sizeof(after[k])))
            fprintf(stderr, "class %d: filter %d->%d scale %d->%d glyphs %d->%d pack %d->%d menu %d->%d\n", k,
                    before[k][P_FILTER], after[k][P_FILTER], before[k][P_SCALE], after[k][P_SCALE], before[k][P_GLYPHS],
                    after[k][P_GLYPHS], before[k][P_PACK], after[k][P_PACK], before[k][P_MENU], after[k][P_MENU]);
        if (!mine) CHECK(!memcmp(before[k], after[k], sizeof(after[k])));
    }
    CHECK(after[kind][P_MENU] == mode && after[kind][P_FILTER] == (mode == 1 ? 1 : mode == 2 ? 2 : 0));
    CHECK(!after[kind][P_PACK] && !after[kind][P_SCALE]);
    if (which == 3) {
        /* A new pack folder (the menu freezes first) changes no category. */
        textureFreezeAppearances(); texturePackSetRoot(pack); CHECK(texturePackEnabled());
        for (int k = 0; k < PC_TEX_CLASSES; ++k) { int p[P_COUNT]; policy(k, p); CHECK(!memcmp(p, after[k], sizeof(p))); }
    }
    FILE* f = fopen(path, "w"); CHECK(f);
    for (int k = 0; k < PC_TEX_CLASSES; ++k) for (int i = 0; i < P_COUNT; ++i) fprintf(f, "%d%c", after[k][i], i + 1 < P_COUNT ? ' ' : '\n');
    CHECK(fclose(f) == 0);
    texturePackShutdown(); SDL_Quit();
    printf("legacy texture options %d: %d checks after one change\n", which, checks); return 0;
}
int main(int argc, char** argv) {
    if (argc > 3 && !strcmp(argv[2], "legacy")) return legacy(argv[1], atoi(argv[3]), argc > 4 && !strcmp(argv[4], "restart"));
    testSetEnv("XG_SETTINGS",argc > 1 ? argv[1] : "none");
    CHECK(SDL_Init(SDL_INIT_VIDEO));
    if (argc > 2) {
        CHECK(textureFilter(0)==1 && textureFilter(1)==2 && textureFilter(2)==0 && textureFilter(3)==1);
        CHECK(textureHudUpscale(0)==2 && textureHudUpscale(1)==4);
        CHECK(textureAppearance(PC_TEX_ICON)==4 && textureHudScale(PC_TEX_ICON)==4);
        CHECK(textureAppearance(PC_TEX_MENU_PORTRAIT)==2);
        SDL_Quit(); puts("texture filter settings reloaded"); return 0;
    }
    for (int k = 0; k < PC_TEX_CLASSES; ++k) CHECK(textureFilter(k)==0);
    CHECK(!textureHudUpscale(0) && !textureHudUpscale(1));
    CHECK(gpuInit(NULL));
    DRAWENV env; SetDefDrawEnv(&env,0,0,320,224); PutDrawEnv(&env);
    hiresSetUpscale(4); hiresSetColor(HIRES_COLOR_RAW);
    hiresSetPrecision(0); hiresSetSeamGuard(0);
    CHECK(gvkHiConfigure(4,HIRES_COLOR_RAW)==4);
    RECT image = {640,256,4,16}, pal = {976,480,16,1};
    uint16_t data[64], palette[16] = {0,31,31<<10,31<<5};
    for (int i = 0; i < 64; ++i) data[i] = 0x2211;
    LoadImage(&image,data); LoadImage(&pal,palette);
    int tp = GetTPage(0,0,640,256), clut = GetClut(976,480);
    POLY_FT4 p[4];
    textureSetFilter(0,1);
    for (int k = 0; k < 4; ++k) {
        quad(&p[k],20,20+22*k,tp,clut,16);
        if (k) pcTextureClass(&p[k],k);
        DrawPrim(&p[k]);
    }
    CHECK((pixel(88,100)&0xffffff)==0x9f0060); /* 3D linear */
    for (int k = 1; k < 4; ++k) CHECK((pixel(88,100+88*k)&0xffffff)==0xff0000);
    CHECK(widePacket(&p[3])->texClass==PC_TEX_TEXT); /* independent of 4:3 aspect */
    pcTextureClassDefault(&p[3],PC_TEX_HUD); CHECK(widePacket(&p[3])->texClass==PC_TEX_TEXT);
    widePacketsDone(); CHECK(!widePacket(&p[3]));
    /* Exact VRAM remains nearest regardless of display filtering. */
    uint32_t exact = 0; RECT one = {22,25,1,1}; StoreImage(&one,&exact); CHECK((exact&65535)==31<<10);
    /* Transparent neighbours neither darken an opaque colour nor leak a
     * different atlas patch. Coverage is thresholded; STP is nearest. */
    data[0] = 0x0011; for (int i = 1; i < 64; ++i) data[i] = data[0];
    LoadImage(&image,data); quad(&p[0],50,20,tp,clut,16); DrawPrim(&p[0]);
    CHECK((pixel(207,100)&0xffffff)==255); CHECK(pixel(208,100)==0);
    palette[1] = 0x801f; LoadImage(&pal,palette); quad(&p[0],70,20,tp,clut,16); DrawPrim(&p[0]);
    CHECK(pixel(288,100)==0); CHECK(pixel(287,100)==0x10000ff);
    /* Same texels, changed live CLUT: earlier snapshots survive. */
    palette[1] = 31<<5; LoadImage(&pal,palette); quad(&p[0],90,20,tp,clut,16); DrawPrim(&p[0]);
    CHECK((pixel(364,100)&0xffffff)==0xff00); CHECK((pixel(204,100)&0xffffff)==255);
    /* A narrow UV patch excludes blue neighbours within the same upload. */
    for (int i = 0; i < 64; ++i) data[i] = 0x2221;
    palette[1] = 31; LoadImage(&pal,palette); LoadImage(&image,data);
    quad(&p[0],110,20,tp,clut,1); p[0].u1=p[0].u3=0; DrawPrim(&p[0]);
    CHECK((pixel(443,100)&0xffffff)==255);
    /* Edge variants are cropped before scaling, not after neighbour taps. */
    TextureRequest req = {tp,clut,0,0,0,16,16,0,0}, patch = req; patch.w = 1;
    const DecodedTexture* d = textureCacheGet(req); TextureSample sample;
    CHECK(textureSampleGet(d,patch,2,&sample) && sample.scale==2 && sample.w==1);
    for (int i = 0; i < 64; ++i) CHECK(sample.rgba[i*4]==255 && sample.rgba[i*4+2]==0);
    const uint8_t* cached = sample.rgba; CHECK(textureSampleGet(d,patch,2,&sample) && sample.rgba==cached);
    textureSetFilter(0,2); quad(&p[0],130,20,tp,clut,1); p[0].u1=p[0].u3=0; DrawPrim(&p[0]);
    CHECK((pixel(523,100)&0xffffff)==255);
    /* A diagonal changes in the edge variant, with STP from its centre
     * rather than the neighbour selected by the corner rule. */
    for (int i=0;i<64;++i) data[i]=0x1111;
    data[0]=0x1121; data[4]=0x1112; /* top + left blue, right + bottom red */
    palette[1]=0x801f; palette[2]=31<<10;
    LoadImage(&pal,palette); LoadImage(&image,data);
    patch=req; patch.w=patch.h=3; d=textureCacheGet(req);
    CHECK(textureSampleGet(d,patch,2,&sample));
    int corner=(2*6+2)*4;
    CHECK(sample.rgba[corner]==0 && sample.rgba[corner+2]==255 && sample.rgba[corner+3]==128);
    CHECK(sample.rgba[corner+4]==255 && sample.rgba[corner+6]==0);
    TextureSample previous=sample;
    palette[2]=31<<5; LoadImage(&pal,palette); d=textureCacheGet(req);
    CHECK(textureSampleGet(d,patch,2,&sample) && sample.revision!=previous.revision);
    CHECK(sample.rgba[corner+1]==255 && sample.rgba[corner+2]==0);
    env.tpage=tp; PutDrawEnv(&env);
    SPRT spr; SetSprt(&spr); SetShadeTex(&spr,1);
    spr.x0=150; spr.y0=20; spr.u0=spr.v0=0; spr.w=spr.h=3; spr.clut=clut;
    textureSetFilter(PC_TEX_SPRITE,2); pcTextureClass(&spr,PC_TEX_SPRITE); DrawPrim(&spr);
    CHECK(pixel(604,84)==0x100ff00); CHECK(pixel(607,84)==0x10000ff);
    /* Linear never averages the blend flag. */
    for (int i=0;i<64;++i) data[i]=0x2211;
    palette[1]=31; palette[2]=0x801f; LoadImage(&pal,palette); LoadImage(&image,data);
    textureSetFilter(PC_TEX_3D,1); quad(&p[0],170,20,tp,clut,16); DrawPrim(&p[0]);
    CHECK((pixel(687,100)&0x1000000)==0); CHECK((pixel(688,100)&0x1000000)!=0);
    /* Force decoded eviction and span-arena rollover without a readback.
     * A rendered direct-colour capture always uses the existing twin. */
    textureSetFilter(0,1);
    static uint16_t direct[256*256];
    for (int i = 0; i < 256*256; ++i) direct[i] = 31;
    RECT large = {640,0,256,256}; LoadImage(&large,direct);
    int dtp = GetTPage(2,0,640,0);
    for (int i = 0; i < 40; ++i) {
        direct[0] = i&1 ? 31 : 31<<5; LoadImage(&large,direct);
        quad(&p[0],(i%10)*28,100+(i/10)*20,dtp,0,16); DrawPrim(&p[0]);
    }
    CHECK((pixel(0,400)&0xffffff)==0xff00);
    CHECK((pixel(112,400)&0xffffff)==255);
    CHECK((pixel(0,480)&0xffffff)==0xff00);
    /* Snapshot pressure alone, with no transfers/readbacks to submit it:
     * 40 distinct full pages exceed the 8 MiB span arena in one batch. */
    static uint8_t rgba[256*256*4];
    for (int i = 0; i < 256*256; ++i) { rgba[i*4]=255; rgba[i*4+3]=255; }
    HiVertex v[6] = {0};
    for (int i = 0; i < 40; ++i) {
        float x=(float)((i%10)*28), y=(float)(100+(i/10)*20);
        for (int j=0; j<6; ++j) {
            int right=j==1||j==3||j==5, bottom=j==2||j==4||j==5;
            v[j].x=x+right*16; v[j].y=y+bottom*16;
            v[j].u=(float)(right*16); v[j].v=(float)(bottom*16); v[j].w=1;
            v[j].mode=DRAW_TEXTURED|DRAW_RAW; v[j].clipMax=319u|223u<<16;
            v[j].uvMax=15u|15u<<16;
        }
        rgba[0]=i&1 ? 255:0; rgba[1]=i&1 ? 0:255;
        TextureSample snap={rgba,1000000u+i,0,0,256,256,1,NULL,0};
        gvkHiDrawTexture(v,6,0,&snap,1);
    }
    CHECK((pixel(0,400)&0xffffff)==0xff00);
    CHECK((pixel(112,400)&0xffffff)==255);
    CHECK((pixel(0,480)&0xffffff)==0xff00);
    TextureStats stats; textureCacheStats(&stats); CHECK(stats.decodes>=40);
    TILE tile; SetTile(&tile); setRGB0(&tile,0,255,0);
    tile.x0=640; tile.y0=0; tile.w=tile.h=16;
    SetDefDrawEnv(&env,0,0,1024,512); PutDrawEnv(&env); DrawPrim(&tile);
    req = (TextureRequest){dtp,0,0,0,0,16,16,0,0}; CHECK(!textureCacheGet(req));
    quad(&p[0],20,20,dtp,0,16); DrawPrim(&p[0]); CHECK((pixel(80,80)&0xffffff)==0xff00);
    /* Wide treatment and texture class share a lifetime, not their fields. */
    presentSetAspectText("16:9"); pcWideSetMode(PC_WIDE_FIELD);
    GpuDisplay display={0}; display.w=320; display.h=224; display.enabled=1; display.clocks=8;
    wideVBlank(&display); CHECK(pcWideView>0);
    pcTextureClass(&p[3],PC_TEX_TEXT); pcWidePrimTex(&p[3],7,PC_WIDE_NOEXTEND,5);
    const WidePacket* packet=widePacket(&p[3]);
    CHECK(packet && packet->texClass==PC_TEX_TEXT && packet->dx==7 && packet->texDx==5);
    pcTextureClass(&p[3],PC_TEX_HUD);
    CHECK(packet->texClass==PC_TEX_HUD && packet->flags==PC_WIDE_NOEXTEND && packet->dx==7);
    widePacketsDone(); pcWidePrim(&p[3],3,0); packet=widePacket(&p[3]);
    CHECK(packet && !packet->classified && packet->texClass==0 && packet->dx==3);
    /* A native menu submitter defaults HUD and respects an explicit text
     * class while linking exactly the same ordering-table addresses. */
    OTag ot=OT_LINK_MASK; pcTextureHudPrim(&ot,&p[2]);
    CHECK(OT_NEXT(ot)==(uptr)&p[2] && widePacket(&p[2])->texClass==PC_TEX_HUD);
    pcTextureClass(&p[2],PC_TEX_TEXT); pcTextureHudPrim(&ot,&p[2]);
    CHECK(widePacket(&p[2])->texClass==PC_TEX_TEXT);
    widePacketsDone(); presentSetAspectText("4:3tv");
    /* Independent native-art upscales: battle's corner changes while the
     * general HUD and all native VRAM pixels retain the original. */
    textureSetFilter(PC_TEX_HUD,0);textureSetHudUpscale(0,0);textureSetHudUpscale(1,4);
    for(int i=0;i<64;i++)data[i]=0x1111;
    data[0]=0x1121;data[4]=0x1112;palette[1]=0x801f;palette[2]=31<<10;
    LoadImage(&pal,palette);LoadImage(&image,data);
    quad(&p[0],200,20,tp,clut,16);pcTextureClass(&p[0],PC_TEX_HUD);DrawPrim(&p[0]);
    quad(&p[1],220,20,tp,clut,16);pcTextureClass(&p[1],PC_TEX_BATTLE_HUD);DrawPrim(&p[1]);
    CHECK(pixel(804,84)==0x10000ff);CHECK(pixel(884,84)==0x1ff0000);
    one=(RECT){221,21,1,1};StoreImage(&one,&exact);CHECK((exact&65535)==0x801f);
    req=(TextureRequest){tp,clut,0,0,0,16,16,0,0};d=textureCacheGet(req);patch=req;
    CHECK(textureUpscaleSample(d,patch,4,&sample) && sample.scale==4);
    cached=sample.rgba;CHECK(textureUpscaleSample(d,patch,4,&sample) && sample.rgba==cached);
    CHECK(textureUpscaleSample(d,patch,2,&sample) && sample.scale==2 && sample.rgba!=cached);
    patch.h=8;CHECK(textureUpscaleSample(d,patch,4,&sample));
    CHECK(sample.rgba[(4*64+4)*4]==255 && sample.rgba[(4*64+4)*4+3]==128);
    pcWideSetMode(PC_WIDE_BATTLE);pcTextureClass(&p[0],PC_TEX_HUD);pcTextureClass(&p[1],PC_TEX_TEXT);
    CHECK(widePacket(&p[0])->texClass==PC_TEX_BATTLE_HUD && widePacket(&p[1])->texClass==PC_TEX_BATTLE_TEXT);
    CHECK(textureHudScale(PC_TEX_BATTLE_TEXT)==4 && !textureHudScale(PC_TEX_BATTLE_PORTRAIT));
    pcWideSetMode(PC_WIDE_FIELD);textureSetHudUpscale(0,2);
    CHECK(textureHudScale(PC_TEX_HUD)==2 && textureHudScale(PC_TEX_ICON)==4 && !textureHudScale(PC_TEX_3D));
    textureSetHudUpscale(0,3);CHECK(textureHudUpscale(0)==2);
    /* One category choice selects a filter, faithful upscale or pack,
     * and keeps every other category independent. */
    CHECK(textureSetAppearance(PC_TEX_HUD,4));CHECK(textureHudScale(PC_TEX_HUD)==4);
    CHECK(textureSetAppearance(PC_TEX_BATTLE_HUD,1));CHECK(textureFilter(PC_TEX_BATTLE_HUD)==1 && !textureHudScale(PC_TEX_BATTLE_HUD));
    CHECK(textureFilter(PC_TEX_HUD)==0 && textureHudScale(PC_TEX_HUD)==4);
    CHECK(textureSetAppearance(PC_TEX_3D,5) && textureUsePack(PC_TEX_3D));
    CHECK(textureSetAppearance(PC_TEX_3D,9) && textureUsePack(PC_TEX_3D) && textureAppearance(PC_TEX_3D)==9); /* 3D Cel 8x: the field texture library */
    CHECK(textureSetAppearance(PC_TEX_3D,5));
    CHECK(textureSetAppearance(PC_TEX_SPRITE,0) && !textureUsePack(PC_TEX_SPRITE));
    CHECK(textureSetAppearance(PC_TEX_TEXT,6) && textureGlyphsEnabled());
    textureSetGlyphs(0);CHECK(textureUseGlyphs(PC_TEX_TEXT) && textureUseGlyphs(PC_TEX_BATTLE_TEXT));
    CHECK(!textureUseGlyphs(PC_TEX_HUD));
    CHECK(textureSetAppearance(PC_TEX_TEXT,0) && !textureGlyphsEnabled());
    textureSetGlyphs(1);CHECK(!textureUseGlyphs(PC_TEX_TEXT));textureSetGlyphs(0);
    CHECK(!textureSetAppearance(PC_TEX_3D,4) && !textureSetAppearance(PC_TEX_HUD,6));
    CHECK(!textureSetAppearance(PC_TEX_MENU_PORTRAIT,5));
    CHECK(textureSetAppearance(PC_TEX_ICON,4));textureSetFilter(PC_TEX_MENU_PORTRAIT,2);
    textureSetHudUpscale(0,2);textureSetHudUpscale(1,4);
    /* Setter bounds, class independence and persistence across a restart. */
    textureSetFilter(0,1); textureSetFilter(1,2); textureSetFilter(2,0); textureSetFilter(3,1);
    textureSetFilter(0,-1); textureSetFilter(4,2);
    CHECK(textureFilter(0)==1 && textureFilter(1)==2 && textureFilter(2)==0 && textureFilter(3)==1);
    /* Two classes drawing the same patch in one frame (xg-j85.15): a
     * sprite's Edge variant (Scale2x turns the corner blue) and the HUD's
     * Upscaled 2x (a label 8 or fewer texels tall: exact red blocks) are
     * different pictures, so the GPU's per-frame copy must not share them. */
    hiresSetUpscale(2); CHECK(gvkHiConfigure(2,HIRES_COLOR_RAW)==2);
    CHECK(textureSetAppearance(PC_TEX_SPRITE,2) && textureSetAppearance(PC_TEX_HUD,3));
    SetDefDrawEnv(&env,0,0,320,224); env.tpage=tp; PutDrawEnv(&env);
    /* One ordering table, as the game draws: one frame's GPU copies. */
    SPRT pair[2]; OTag table; ClearOTagR(&table,1);
    for (int i=0;i<2;++i) {
        SetSprt(&pair[i]); SetShadeTex(&pair[i],1); pair[i].x0=170-20*i; pair[i].y0=40;
        pair[i].u0=pair[i].v0=0; pair[i].w=pair[i].h=3; pair[i].clut=clut;
        pcTextureClass(&pair[i],i?PC_TEX_SPRITE:PC_TEX_HUD); AddPrim(&table,&pair[i]);
    }
    DrawOTag(&table);
    CHECK(pixel(302,82)==0x1ff0000 && pixel(342,82)==0x10000ff);
    printf("GPU texture filters: %d checks passed\n",checks);
    gpuShutdown(); SDL_Quit(); return 0;
}
