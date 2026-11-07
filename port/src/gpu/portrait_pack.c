/* Separate, bounded artwork library; never changes the native VRAM or discs. */
#include <SDL3/SDL.h>
#include "system/engine.h"
#include <ctype.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "portrait_pack.h"
#include "texture.h"
#include "system/settings.h"
#include "system/cputime.h"
#include "pc_wide.h"

#define SETS 64
#define SLOTS 128
#define CACHE_BYTES (32u*1024u*1024u)
#define PATH_BYTES 3072
static PortraitSet sets[SETS];
static int count, loaded;
static char root[PATH_BYTES], selected[128], dir[PATH_BYTES+180];
static char battleSelected[128], battleDir[PATH_BYTES+180];
static char menuSelected[128], menuDir[PATH_BYTES+180];
static PortraitPackStats stats;
static uint64_t serial, tick, generation;
typedef struct { uint64_t texel,palette,used; int category; PortraitImage image; size_t bytes; } Entry;
static Entry entries[SLOTS];
static int validId(const char* s) {
    if (!s || !*s || strlen(s)>=sizeof(selected) || !strcmp(s,".") || !strcmp(s,"..")) return 0;
    for (;*s;++s) if (!isalnum((unsigned char)*s) && *s!='-' && *s!='_' && *s!='.') return 0;
    return 1;
}
static char* trim(char* s) {
    while (isspace((unsigned char)*s)) ++s;
    char* e=s+strlen(s); while(e>s && isspace((unsigned char)e[-1])) --e;
    *e=0; return s;
}
static int metadata(const char* path, PortraitSet* set) {
    size_t bytes=0; SDL_PathInfo info;
    if (!SDL_GetPathInfo(path,&info) || info.type!=SDL_PATHTYPE_FILE || info.size>8192) return 0;
    char* data=SDL_LoadFile(path,&bytes); if (!data) return 0;
    for (char* line=data;line && *line;) {
        char* next=strchr(line,'\n'); if(next) *next++=0;
        char* s=trim(line), *eq=strchr(s,'=');
        if (*s!='#' && *s!=';' && eq) {
            *eq++=0;
            if (!strcmp(trim(s),"name")) SDL_strlcpy(set->name,trim(eq),sizeof(set->name));
            else if (!strcmp(s,"author")) SDL_strlcpy(set->author,trim(eq),sizeof(set->author));
        }
        line=next;
    }
    SDL_free(data); return 1;
}
static SDL_EnumerationResult scan(void* userdata, const char* dirname, const char* name) {
    (void)userdata;
    if (count>=SETS || !validId(name) || !strcmp(name,"original")) return SDL_ENUM_CONTINUE;
    char path[sizeof(dir)+32];
    snprintf(path,sizeof(path),"%s%s/portrait.ini",dirname,name);
    PortraitSet set={0}; SDL_strlcpy(set.id,name,sizeof(set.id)); SDL_strlcpy(set.name,name,sizeof(set.name));
    if (metadata(path,&set)) sets[count++]=set;
    return SDL_ENUM_CONTINUE;
}
static int order(const void* a,const void* b) { return strcmp(((const PortraitSet*)a)->id,((const PortraitSet*)b)->id); }
static void clear(void) {
    for(int i=0;i<SLOTS;++i) free((void*)entries[i].image.rgba);
    memset(entries,0,sizeof(entries)); memset(&stats,0,sizeof(stats));
    stats.generation=++generation;
}
static int configureSet(const char* id,char* path,size_t pathBytes,char* message,size_t messageBytes) {
    path[0]=0;
    if (!strcmp(id,"original")) { SDL_strlcpy(message,"Original artwork",messageBytes); return 0; }
    int found=0; for(int i=1;i<count;++i) if(!strcmp(id,sets[i].id)) found=1;
    if (!found) { SDL_strlcpy(message,"Selected set unavailable; using originals",messageBytes); return 0; }
    snprintf(path,pathBytes,"%s/%s/%s",root,id,texturePackVersion());
    SDL_PathInfo info;
    if (!SDL_GetPathInfo(path,&info) || info.type!=SDL_PATHTYPE_DIRECTORY) {
        SDL_strlcpy(message,"Regional set unavailable; using originals",messageBytes); return 0;
    }
    SDL_strlcpy(message,"Ready; missing pictures use originals",messageBytes); return 1;
}
static void load(void) { if (!loaded) portraitPackReload(); }
void portraitPackReload(void) {
    if (!loaded) {
        loaded=1;
        settingsDescribe("portrait_packs","Folder containing independent named portrait sets; default beside executable/portrait-packs");
        settingsDescribe("portrait_set","Regular portrait set ID; original (default) keeps native artwork");
        settingsDescribe("battle_portrait_set","Independent in-battle and results portrait set ID; original (default) keeps native artwork");
        settingsDescribe("menu_portrait_set","Independent menu portrait set ID; original (default) keeps native artwork");
        const char* s=settingsGet("portrait_packs");
        snprintf(root,sizeof(root),"%s",s?s:"");
        if (!s) enginePackDir("portrait-packs",root,sizeof(root)); /* beside this or the other engine's executable */
        fprintf(stderr,"portrait packs: %s\n",root);
        s=settingsGet("portrait_set");
        SDL_strlcpy(selected,s && validId(s)?s:"original",sizeof(selected));
        s=settingsGet("battle_portrait_set");
        SDL_strlcpy(battleSelected,s && validId(s)?s:"original",sizeof(battleSelected));
        s=settingsGet("menu_portrait_set");SDL_strlcpy(menuSelected,s && validId(s)?s:"original",sizeof(menuSelected));
    }
    clear(); count=0; dir[0]=0;
    memset(sets,0,sizeof(sets));
    SDL_strlcpy(sets[0].id,"original",sizeof(sets[0].id));
    SDL_strlcpy(sets[0].name,"Original",sizeof(sets[0].name)); count=1;
    if (root[0]) SDL_EnumerateDirectory(root,scan,NULL);
    qsort(sets+1,(size_t)(count-1),sizeof(sets[0]),order);
    stats.enabled=configureSet(selected,dir,sizeof(dir),stats.message,sizeof(stats.message));
    stats.menuEnabled=configureSet(menuSelected,menuDir,sizeof(menuDir),stats.menuMessage,sizeof(stats.menuMessage));
    stats.battleEnabled=configureSet(battleSelected,battleDir,sizeof(battleDir),stats.battleMessage,sizeof(stats.battleMessage));
}
const char* portraitPackRoot(void) { load(); return root; }
const char* portraitPackSelected(void) { load(); return selected; }
const char* portraitBattlePackSelected(void) { load(); return battleSelected; }
const char* portraitMenuPackSelected(void) { load();return menuSelected; }
int portraitPackSizeEnabled(int size) { load();return size==48?stats.menuEnabled:size==24?stats.battleEnabled:size==64?stats.enabled:0; }
int portraitPackClassEnabled(int kind) {
    load();return kind==PC_TEX_MENU_THUMB_PORTRAIT?stats.menuEnabled:portraitPackSizeEnabled(PC_TEX_PORTRAIT_SIZE(kind));
}
int portraitBattlePackEnabled(void) { load(); return stats.battleEnabled; }
int portraitPackCount(void) { load(); return count; }
const PortraitSet* portraitPackSet(int i) { load(); return i>=0 && i<count?&sets[i]:NULL; }
int portraitPackEnabled(void) { load(); return stats.enabled; }
void portraitPackStats(PortraitPackStats* out) { load(); *out=stats; }
void portraitPackSetRoot(const char* s) {
    load(); if (!s || strlen(s)>=sizeof(root)) return;
    memmove(root,s,strlen(s)+1); settingsSet("portrait_packs",root); settingsSave(); portraitPackReload();
}
int portraitPackSelect(const char* s) {
    load(); if (!validId(s)) return 0;
    memmove(selected,s,strlen(s)+1); settingsSet("portrait_set",selected); settingsSave(); portraitPackReload(); return 1;
}
int portraitBattlePackSelect(const char* s) {
    load(); if (!validId(s)) return 0;
    memmove(battleSelected,s,strlen(s)+1); settingsSet("battle_portrait_set",battleSelected); settingsSave(); portraitPackReload(); return 1;
}
int portraitMenuPackSelect(const char* s) {
    load();if(!validId(s))return 0;memmove(menuSelected,s,strlen(s)+1);
    settingsSet("menu_portrait_set",menuSelected);settingsSave();portraitPackReload();return 1;
}
void portraitPackShutdown(void) { clear(); loaded=0; count=0; root[0]=selected[0]=dir[0]=battleSelected[0]=battleDir[0]=menuSelected[0]=menuDir[0]=0; }
static int imageForSize(uint64_t texel,uint64_t palette,int size,int thumbnail,PortraitImage* out) {
    load(); memset(out,0,sizeof(*out));
    int menu=size==48 || thumbnail,battle=size==24 && !menu,category=menu?1:battle?2:0;
    int kind=menu?(size==48?PORTRAIT_MENU:PORTRAIT_MENU_THUMB):battle?PORTRAIT_BATTLE:PORTRAIT_DIALOGUE;
    if(!(menu?stats.menuEnabled:battle?stats.battleEnabled:stats.enabled)) return 0;
    Entry* e=NULL; Entry* victim=entries;
    for(int i=0;i<SLOTS;++i) {
        Entry* p=&entries[i];
        if(p->used && p->texel==texel && p->palette==palette && p->category==category) { e=p; break; }
        if(p->used<victim->used) victim=p;
    }
    if(!e) {
        e=victim;
        stats.bytes-=e->bytes; if(e->used) --stats.entries;
        free((void*)e->image.rgba); memset(e,0,sizeof(*e));
        e->texel=texel; e->palette=palette; e->category=category; e->used=++tick; ++stats.entries;
        char path[sizeof(dir)+64];
        snprintf(path,sizeof(path),"%s/%016" PRIx64 "-%016" PRIx64 ".png",menu?menuDir:battle?battleDir:dir,texel,palette);
        int scale=0,found=0;
        CPU_TIME_BEGIN(CPU_LOAD);
        SDL_Surface* png=texturePackReadPng(path,size,size,&scale,&found);
        CPU_TIME_END(CPU_LOAD);
        if(!png) {
            if(found) ++stats.invalid; else { ++stats.misses; ++stats.kindMisses[kind]; }
            /* Which picture a set lacks (xg-j85.40), once: the miss is kept. */
            const char* log=SDL_getenv("XG_TEXTURE_LOG");
            if(log && *log && strcmp(log,"0")) fprintf(stderr,"portrait: %s %s\n",found?"invalid":"missing",path);
        }
        else {
            size_t bytes=(size_t)png->w*png->h*4;
            /* Over the budget the least recently used pictures go, never the
             * hottest face (xg-j85.21). */
            while(stats.bytes+bytes>CACHE_BYTES) {
                Entry* old=NULL;
                for(int i=0;i<SLOTS;++i) if(&entries[i]!=e && entries[i].bytes && (!old || entries[i].used<old->used)) old=&entries[i];
                if(!old) break;
                stats.bytes-=old->bytes; --stats.entries;
                free((void*)old->image.rgba); memset(old,0,sizeof(*old));
            }
            uint8_t* rgba=malloc(bytes);
            if(rgba) {
                for(int y=0;y<png->h;++y) memcpy(rgba+(size_t)y*png->w*4,(uint8_t*)png->pixels+(size_t)y*png->pitch,(size_t)png->w*4);
                for(size_t i=3;i<bytes;i+=4) rgba[i]=rgba[i]<64?0:rgba[i]<192?128:255;
                e->image=(PortraitImage){rgba,png->w,scale,UINT64_C(0x2000000000000000)|++serial};
                e->bytes=bytes; stats.bytes+=bytes; ++stats.loads;
            }
            SDL_DestroySurface(png);
        }
    }
    e->used=++tick;
    if(!e->image.rgba) return 0;
    *out=e->image; ++stats.draws; ++stats.kindDraws[kind]; return 1;
}
int portraitPackImage(uint64_t texel,uint64_t palette,PortraitImage* out) {
    return imageForSize(texel,palette,64,0,out);
}
/* Both live drawing and smooth-motion snapshots use the same independent
 * portrait resource, including its authored transparent silhouette. */
int portraitPackSample(const DecodedTexture* d,TextureRequest patch,TextureSample* out) {
    return portraitPackClassSample(d,patch,PC_TEX_PORTRAIT,out);
}
int portraitPackClassSample(const DecodedTexture* d,TextureRequest patch,int kind,TextureSample* out) {
    PortraitImage image;
    if(!d || d->depth!=8 || (d->request.w!=64 && d->request.w!=48 && d->request.w!=24) || d->request.h!=d->request.w ||
        patch.u<d->request.u || patch.v<d->request.v ||
        patch.u+patch.w>d->request.u+d->request.w || patch.v+patch.h>d->request.v+d->request.h ||
        !imageForSize(d->texelKey,d->paletteKey,d->request.w,kind==PC_TEX_MENU_THUMB_PORTRAIT,&image)) return 0;
    if(!textureReplacementsShown()) return 0; /* the A/B comparison: looked up, not shown */
    *out=(TextureSample){image.rgba,image.revision,d->request.u,d->request.v,d->request.w,d->request.h,image.scale,NULL,1};
    return 1;
}
static uint32_t le32(const uint8_t* p) { return p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24; }
static unsigned le16(const uint8_t* p) { return p[0]|(unsigned)p[1]<<8; }
static uint64_t hash(uint64_t h,const void* data,size_t n) {
    const uint8_t* p=data; while(n--) h=(h^*p++)*UINT64_C(1099511628211); return h;
}
int portraitSourceKeys(const void* data,size_t n,uint64_t* texel,uint64_t* palette) {
    if(!data || n<532) return 0;
    const uint8_t* p=data;
    if(le32(p)!=0x10 || le32(p+4)!=9 || le32(p+8)!=524 || le16(p+16)!=256 || le16(p+18)!=1) return 0;
    size_t at=532;
    if(n-at<12 || le32(p+at)!=4108 || le16(p+at+8)!=32 || le16(p+at+10)!=64 || n-at<4108) return 0;
    const uint8_t shape[]={8,64,0,64,0}, countBytes[]={0,1};
    uint64_t seed=UINT64_C(14695981039346656037);
    *texel=hash(hash(hash(seed,"xgtex1",6),shape,sizeof(shape)),p+at+12,4096);
    *palette=hash(hash(hash(seed,"xgpal1",6),countBytes,2),p+20,512);
    return 1;
}
int portraitPackTim(const void* tim,size_t bytes,PortraitImage* out) {
    uint64_t texel,palette; memset(out,0,sizeof(*out));
    return portraitSourceKeys(tim,bytes,&texel,&palette) && portraitPackImage(texel,palette,out);
}
