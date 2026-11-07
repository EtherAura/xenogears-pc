/* Authored HUD sprites, authenticated by native indices. Replacement index
 * maps preserve the game's live palettes, transparency and blend flags. */
#include <SDL3/SDL.h>
#include "system/engine.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "texture.h"
#include "system/hw.h"
#include "system/settings.h"
#include "system/cputime.h"

#define CEL_SPRITES 512 /* across the 2x, 4x and 8x sets: a 56-sprite library is 168 lines */
#define CEL_BYTES (32u*1024u*1024u)
typedef struct {
    int depth,x,y,w,h,scale,checked;
    char path[3300];
    uint8_t *native,*art;
    uint64_t revision;
} CelSprite;
static CelSprite sprites[CEL_SPRITES];
static int loaded,count,ignored,badFiles,unreadable,capped;
static char root[3072],message[256],status[512];
static size_t bytes;
static uint64_t revision;
/* Diagnostics (xg-j85.40), Cel draws only: the last complete frame's
 * samples by outcome; under XG_TEXTURE_LOG each distinct patch that keeps
 * native pixels is logged once (until a reload). */
typedef struct { uint32_t vblank; unsigned matched,uncovered,changed,unavailable; } CelFrame;
static CelFrame celNow,celLast;
#define LOGGED 512
static uint64_t logged[LOGGED];
static int loggedCount,logMode=-1;

static int safeName(const char* s) {
    for(;*s;s++)if(!isalnum((unsigned char)*s) && *s!='-' && *s!='_')return 0;
    return 1;
}
void textureCelShutdown(void) {
    for(int i=0;i<count;i++){free(sprites[i].native);free(sprites[i].art);}
    memset(sprites,0,sizeof(sprites));count=loaded=0;bytes=0;ignored=badFiles=unreadable=capped=0;
    memset(&celNow,0,sizeof(celNow));memset(&celLast,0,sizeof(celLast));
    memset(logged,0,sizeof(logged));loggedCount=0;
}
/* One sprite a line: depth x y width height basename. A file saved or zipped
 * on Windows (CRLF, a UTF-8 BOM) or with CR-only lines reads the same; blank
 * lines and '#' comments are skipped; any other line that does not parse is
 * counted in the status rather than dropped silently (xg-j85.14). */
static void readSet(int scale) {
    char path[3300];snprintf(path,sizeof(path),"%s/cel-%dx/%s/sprites.txt",root,scale,texturePackVersion());
    SDL_IOStream* f=SDL_IOFromFile(path,"rb");if(!f)return;
    Sint64 n=SDL_GetIOSize(f);if(n<0 || n>32768){SDL_CloseIO(f);++badFiles;return;}
    char data[32769];int ok=SDL_ReadIO(f,data,(size_t)n)==(size_t)n;SDL_CloseIO(f);if(!ok){++badFiles;return;}data[n]=0;
    char* text=n>=3 && !memcmp(data,"\xEF\xBB\xBF",3)?data+3:data;
    for(char* line=text;line && *line;) {
        char* next=strpbrk(line,"\r\n");
        if(next){int pair=next[0]=='\r' && next[1]=='\n';*next=0;next+=pair?2:1;}
        char* comment=strchr(line,'#');if(comment)*comment=0;
        while(isspace((unsigned char)*line))line++;
        if(!*line){line=next;continue;}
        CelSprite a={0};char name[81],tail;
        if(count<CEL_SPRITES && sscanf(line,"%d %d %d %d %d %80s %c",&a.depth,&a.x,&a.y,&a.w,&a.h,name,&tail)==6 &&
           (a.depth==4 || a.depth==8) && a.x>=0 && a.x<=4096 && a.y>=0 && a.y<=512 && a.w>0 && a.w<=256 && a.h>0 && a.h<=256 &&
           a.x+a.w<=1024*(16/a.depth) && a.y+a.h<=512 && safeName(name)) {
            a.scale=scale;snprintf(a.path,sizeof(a.path),"%s/cel-%dx/%s/%s",root,scale,texturePackVersion(),name);
            sprites[count++]=a;
        } else ++ignored;
        line=next;
    }
}
void textureCelReload(void) {
    int wasLoaded=loaded;char selected[sizeof(root)];SDL_strlcpy(selected,root,sizeof(selected));
    textureCelShutdown();loaded=1;
    if(wasLoaded)SDL_strlcpy(root,selected,sizeof(root));
    else {
        settingsDescribe("ui_packs","Authored cel HUD/icon library; default ui-packs beside the executable");
        const char* s=settingsGet("ui_packs");
        if(s)SDL_strlcpy(root,s,sizeof(root));
        else enginePackDir("ui-packs",root,sizeof(root)); /* beside this or the other engine's executable */
        fprintf(stderr,"ui packs: %s\n",root);
    }
    readSet(2);readSet(4);readSet(8);
    /* Unmatched graphics deliberately stay native nearest (xg-htn.35.123). */
    if(count)snprintf(message,sizeof(message),"%d cel sprite variants ready; unmatched graphics keep native pixels",count);
    else SDL_strlcpy(message,"Cel artwork unavailable; graphics keep native pixels",sizeof(message));
    if(ignored)snprintf(message+strlen(message),sizeof(message)-strlen(message),". %d sprites.txt line%s ignored",
                        ignored,ignored==1?"":"s");
    if(badFiles)snprintf(message+strlen(message),sizeof(message)-strlen(message),". %d sprites.txt unreadable or over 32 KiB",badFiles);
}
static void load(void){if(!loaded)textureCelReload();}
const char* textureCelRoot(void){load();return root;}
const char* textureCelStatus(void) {
    load();
    SDL_strlcpy(status,message,sizeof(status));
    char part[160];
    if(unreadable){snprintf(part,sizeof(part),". %d sprite%s unreadable",unreadable,unreadable==1?"":"s");SDL_strlcat(status,part,sizeof(status));}
    if(capped) {
        snprintf(part,sizeof(part),". %d sprite%s over the %u MiB artwork limit keep native pixels",capped,capped==1?"":"s",CEL_BYTES>>20);
        SDL_strlcat(status,part,sizeof(status));
    }
    /* The frame drawn before this one is complete (the menu pauses on it). */
    const CelFrame* f=celNow.vblank!=hwVSyncCount()?&celNow:&celLast;
    if(f->matched || f->uncovered || f->changed || f->unavailable) {
        snprintf(part,sizeof(part),". Last frame: %u cel draws, %u without cel art, %u changed from the native sprite, %u unavailable",
                 f->matched,f->uncovered,f->changed,f->unavailable);
        SDL_strlcat(status,part,sizeof(status));
    }
    return status;
}
void textureCelSetRoot(const char* s) {
    load();if(!s || strlen(s)>=sizeof(root))return;
    memmove(root,s,strlen(s)+1);settingsSet("ui_packs",root);settingsSave();textureCelReload();
}
static uint8_t* indices(const char* path,int w,int h,int scale,int limit) {
    int got,found;CPU_TIME_BEGIN(CPU_LOAD);SDL_Surface* png=texturePackReadPng(path,w,h,&got,&found);CPU_TIME_END(CPU_LOAD);
    if(!png)return NULL;
    if(got!=scale){SDL_DestroySurface(png);return NULL;}
    if(bytes+(size_t)png->w*png->h>CEL_BYTES){SDL_DestroySurface(png);++capped;return NULL;}
    uint8_t* out=malloc((size_t)png->w*png->h);int ok=out!=NULL;
    for(int y=0;ok && y<png->h;y++)for(int x=0;x<png->w;x++) {
        uint8_t* p=(uint8_t*)png->pixels+(size_t)y*png->pitch+x*4;
        if(p[0]>=limit || p[0]!=p[1] || p[0]!=p[2] || p[3]!=255){ok=0;break;}
        out[(size_t)y*png->w+x]=p[0];
    }
    SDL_DestroySurface(png);if(!ok){free(out);return NULL;}return out;
}
static int readSprite(CelSprite* s) {
    if(s->checked)return s->art!=NULL;
    s->checked=1;
    int before=capped;
    char path[3340];snprintf(path,sizeof(path),"%s.native.png",s->path);
    s->native=indices(path,s->w,s->h,1,1<<s->depth);
    if(s->native) {
        bytes+=(size_t)s->w*s->h;
        snprintf(path,sizeof(path),"%s.indices.png",s->path);s->art=indices(path,s->w,s->h,s->scale,1<<s->depth);
    }
    if(!s->art) {
        /* Nothing of a sprite without artwork stays allocated or counted. */
        if(s->native){free(s->native);s->native=NULL;bytes-=(size_t)s->w*s->h;}
        if(capped==before)++unreadable;
        return 0;
    }
    bytes+=(size_t)s->w*s->h*s->scale*s->scale;s->revision=UINT64_C(0x3000000000000000)|++revision;return 1;
}
static void kept(TextureRequest p,int x,int y,const CelSprite* s,const char* why) {
    if(logMode<0){const char* e=SDL_getenv("XG_TEXTURE_LOG");logMode=e && *e && strcmp(e,"0");}
    if(!logMode)return;
    uint64_t k=((uint64_t)p.tpage<<48 | (uint64_t)p.clut<<32 | (uint64_t)(uint16_t)p.u<<16 | (uint16_t)p.v)*UINT64_C(0x9E3779B97F4A7C15);
    k^=((uint64_t)p.w<<9 | (uint64_t)p.h | (uint64_t)(s?s-sprites+1:0)<<18)*UINT64_C(0xBF58476D1CE4E5B9);
    k|=1; /* 0 marks a free slot */
    unsigned at=(unsigned)(k>>40)%LOGGED;
    for(int n=0;n<LOGGED && logged[at];n++,at=(at+1)%LOGGED)if(logged[at]==k)return;
    if(loggedCount>=LOGGED*3/4)return;
    logged[at]=k;++loggedCount;
    fprintf(stderr,"cel: native pixels kept: tpage %04x clut %04x uv %d,%d %dx%d (sprites.txt x %d y %d): %s%s\n",
            p.tpage,p.clut,p.u,p.v,p.w,p.h,x,y,why,s?s->path+strlen(root):"");
}
/* The frame's counts move on at the first Cel sample of a new vblank. */
static CelFrame* frame(void) {
    uint32_t v=hwVSyncCount();
    if(v!=celNow.vblank){
        if(celNow.matched || celNow.uncovered || celNow.changed || celNow.unavailable)celLast=celNow;
        memset(&celNow,0,sizeof(celNow));celNow.vblank=v;
    }
    return &celNow;
}
int textureCelSample(const DecodedTexture* d,TextureRequest p,int scale,TextureSample* out) {
    load();if(!d || !d->texels || (d->depth!=4 && d->depth!=8) || p.window || p.w<1 || p.h<1 || p.w>256 || p.h>256 ||
        p.u<d->request.u || p.v<d->request.v || p.u+p.w>d->request.u+d->request.w ||
        p.v+p.h>d->request.v+d->request.h){++frame()->uncovered;return 0;}
    int ppw=16/d->depth,tx=(p.tpage&15)*64*ppw,ty=((p.tpage>>4)&1)*256;
    int x=tx+p.u,y=ty+p.v;
    const CelSprite* differs=NULL;int missing=0;
    for(int i=0;i<count;i++) {
        CelSprite* s=&sprites[i];
        if(s->scale!=scale || s->depth!=d->depth || x<s->x || y<s->y || x+p.w>s->x+s->w || y+p.h>s->y+s->h)continue;
        if(!readSprite(s)){missing=1;continue;}
        int same=1;
        for(int yy=0;same && yy<p.h;yy++)if(memcmp(s->native+(size_t)(y-s->y+yy)*s->w+x-s->x,
            d->texels+(size_t)(p.v-d->request.v+yy)*d->request.w+p.u-d->request.u,(size_t)p.w))same=0;
        if(!same){differs=s;continue;}
        if(!textureReplacementsShown())return 0; /* the A/B comparison: found, not shown */
        ++frame()->matched;
        *out=(TextureSample){NULL,s->revision,s->x-tx,s->y-ty,s->w,s->h,scale,s->art,1};return 1;
    }
    CelFrame* f=frame();
    if(differs){++f->changed;kept(p,x,y,differs,"native indices differ from ");}
    else if(missing){++f->unavailable;kept(p,x,y,NULL,"its sprite's PNGs are unreadable or over the limit");}
    else{++f->uncovered;kept(p,x,y,NULL,"no sprite covers it");}
    return 0;
}

/* Font quads exclude the high UV endpoint. Only a whole indexed sprite or
 * its exact one-texel guarded interior can anchor both triangle samples.
 * Native indices still authenticate the live source before any art is used. */
int textureCelCanvas(TextureRequest r,int scale) {
    if(scale<2 || r.window || r.w<1 || r.h<1)return 0;
    load();
    int depth=((r.tpage>>7)&3)==0?4:((r.tpage>>7)&3)==1?8:0;
    if(!depth)return 0;
    int x=(r.tpage&15)*64*(16/depth)+r.u,y=((r.tpage>>4)&1)*256+r.v;
    for(int i=0;i<count;i++) {
        const CelSprite* s=&sprites[i];
        if(s->scale!=scale || s->depth!=depth)continue;
        int full=x==s->x && y==s->y && r.w==s->w && r.h==s->h;
        int guarded=x==s->x+1 && y==s->y+1 && r.w==s->w-2 && r.h==s->h-2;
        if(full || guarded) {
            TextureSample sample;
            return textureCelSample(textureCacheGet(textureRequestSnap(r)),r,scale,&sample);
        }
    }
    return 0;
}
