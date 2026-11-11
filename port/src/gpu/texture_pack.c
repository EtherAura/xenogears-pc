/* Content-keyed user PNGs. No game data is written or patched here. The
 * decoder has already excluded rendered direct-colour tiles. */
#define _GNU_SOURCE 1 /* posix_fadvise, syscall (the pack warm-up's I/O priority) */
#include <SDL3/SDL.h>
#include "system/engine.h"
#include <ctype.h>
#include <inttypes.h>
#include <unistd.h>
#ifdef __linux__
#include <fcntl.h>
#include <sys/resource.h>
#include <sys/syscall.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pngfast.h"
#include "texture.h"
#include "system/settings.h"
#include "system/cputime.h"

#define PACK_SLOTS TEXTURE_PACK_SLOTS /* images and known misses (a field meets thousands of keys) */
#define PACK_HASH 16384
#define PACK_BYTES (32u * 1024u * 1024u)
#define PNG_BYTES (16u * 1024u * 1024u)
#define PNG_PIXELS (2048u * 2048u) /* a 256x256 field page at 8x */
#define PATH_BYTES 3072
typedef struct {
    uint64_t texel, palette, touched, revision, livePalette, loaded;
    int indexed, w, h, scale, limit;
    int pending;      /* its file is being decoded on the loader thread; drawn as the original until then */
    uint64_t serial;  /* this use of the slot: a finished load for an earlier one is dropped */
    unsigned room;    /* a page: the room (roomSerial) whose preload queued it, 0 none */
    int nativeSeen;   /* a page: drawn as the original while loading (counted once) */
    size_t bytes;
    uint8_t *rgba, *indices;
} Entry;
static Entry entries[PACK_SLOTS];
/* The entries by key: chains through `chain` from `bucket` (index + 1, 0 end). */
static int32_t bucket[PACK_HASH], chain[PACK_SLOTS];
/* vk.c keeps images on the GPU and colours index maps there (texturePackSetGpu). */
static int gpuKeeps;
/* Big replacements (a field texture page at 2x or 4x) go to the GPU as the
 * patch a primitive samples, coloured with its live palette, not as the
 * whole image: the renderer copies every sample it gets into each frame,
 * and a page drawn with a dozen palettes would be recoloured a dozen times
 * a frame. Crops are kept by image, palette and patch. */
#define CROP_FROM (256u * 256u) /* replacement pixels above which patches are cropped */
#define CROP_SLOTS 1024
#define CROP_BYTES (96u * 1024u * 1024u) /* default; pack.ini crop_mb */
typedef struct {
    uint64_t loaded, palette, revision, touched;
    int u, v, w, h;
    size_t bytes;
    uint8_t* rgba;
} Crop;
static Crop crops[CROP_SLOTS];
static size_t cropBytes, cropLimit = CROP_BYTES;
static uint64_t cropClock;
static void cropsReset(void) {
    for (int i=0;i<CROP_SLOTS;++i) free(crops[i].rgba);
    memset(crops,0,sizeof(crops)); cropBytes=0;
}
static TexturePackStats stats;
static uint64_t useClock, revision, generation; /* Never reset: pending GPU snapshots may survive a reload. */
static int loaded;
/* The A/B comparison (textureReplacementsShow): every replacement lookup still runs (the same
 * loads, the same caches) and its answer is withheld, so showing them again gives exactly the
 * picture a run that never hid them has. */
static int replacementsHidden;
/* Optional local HD lettering, independent of the general texture pack. */
static char glyphDir[PATH_BYTES+40];
static char spriteDir[PATH_BYTES+40]; /* Sprites -> Cel 8x: index maps per sampled sheet rectangle (tools/field_sprite_pack.py) */
/* The sprite pack's room preload (xg-hbc.23): parts.tsv ("<part> <texel key> <w> <h> <depth>" a cel) and
 * npcs.tsv ("<disc> <field> <npc> <part>" a field NPC slot showing the part's sheet) beside its pack.ini
 * (tools/field_sprite_index.py). As a field uploads its NPC sheets (initFieldData, pcSpritePackPreload)
 * every cel of the slot's part is queued for the loader thread, so none is decoded on the game thread
 * at its first draw (2.6-8.5 ms vblanks in crowds); a cel drawn before its decode finishes is decoded
 * there and then, as before, so the picture never shows the native pose first. sprite_preload=0 (or
 * XG_SPRITE_PRELOAD=0) keeps the first-draw decodes. */
#define SPRITE_PART_NAME 48
#define SPRITE_PARTS 1024
#define SPRITE_CEL_SCALE 8 /* the packs' cel scale, for the cache budget estimate (the decoder accepts any) */
typedef struct { uint64_t texel; int part, w, h, limit; } SpriteCel;
typedef struct { int disc, field, npc, part; } SpriteNpc;
static SpriteCel* spriteCels; static int spriteCelCount;
static SpriteNpc* spriteNpcs; static int spriteNpcCount;
static char (*spritePartNames)[SPRITE_PART_NAME]; static int spritePartCount;
static char spriteRoot[PATH_BYTES]; /* the folder holding pack.ini and the version directory */
static size_t preloadPending;       /* bytes of queued preloads not installed yet (estimated at SPRITE_CEL_SCALE) */
static int spritePreloadOn=1, spriteReportArmed;
/* The room's poses (xg-hbc.26): poses.tsv ("<part> <key> <w> <h> <scale> <member>" a pose of the part,
 * member the party character the part is the field doll of, -1 none; tools/field_sprite_index.py) beside
 * pack.ini. As a field loads (pcSpritePackPreload) every pose of its NPC slots' parts and of the current
 * party's dolls (pcSpritePackParty) is queued for the pose loader threads, and once a pose of another part
 * is drawn (a scene's actor, a doll with no slot) the rest of that part's; a pose drawn before its decode
 * finished is decoded there and then, as before, so the picture never shows the native sprite first. */
typedef struct { uint64_t key; int part, w, h, scale, member; } SpritePoseRow;
static SpritePoseRow* spritePoses; static int spritePoseCount;
static int* spritePoseByKey;                        /* spritePoses' indices by key (a drawn pose's parts) */
static int partPoseFirst[SPRITE_PARTS], partPoseCount[SPRITE_PARTS]; /* spritePoses is in part order */
static unsigned partPoseRoom[SPRITE_PARTS];         /* the room (poseRoom) a part's poses were last queued for */
static int partyIds[3]={-1,-1,-1};                  /* pcSpritePackParty: the current party's character ids */
static void spriteIndexRead(void);
static void spriteIndexFree(void);
static void texturePackReport(void);
static int spriteLogOn(void); /* XG_TEXTURE_LOG, below */
/* With XG_CPU_TIMING: the game thread's pack loads by kind (sprite cels, other images, sprite poses):
 * their time, the slowest, and those over a millisecond, for the exit report. */
static struct { double ms, worst; unsigned slow; } loadKinds[3];
static void loadTimed(int kind, uint64_t ns) {
    double ms=(double)ns*1e-6;
    loadKinds[kind].ms+=ms; loadKinds[kind].slow+=ms>1.0;
    if (ms>loadKinds[kind].worst) loadKinds[kind].worst=ms;
}
/* pack.ini rect_keys=no: the pack was made from a discovery with stable
 * regions, so a region's patches need no second look-up by their own keys
 * (how packs made before stable regions name them). */
static int rectKeys = 1;
/* pack.ini cache_mb=N (32..1024): pixel storage for packs with big images.
 * Stable regions make a field texture page one image: at 4x up to 1024x1024
 * replacement pixels, 5 MiB as an index map, so a field needs more than the
 * default 32 MiB. */
static size_t packBytes = PACK_BYTES;
static size_t beetleBytes; /* with a Beetle pack: the cache's budget at least (beetle_pack_cache_mb) */
static size_t cacheBudget(void) { return packBytes>beetleBytes?packBytes:beetleBytes; }
static void loaderStop(void); /* the background loader and the room lists, below */
static void posesClear(void); /* the sprite pose cache and its loaders, below */
static void roomsRead(void);
static void roomsPreloadField(int field);
/* rooms.txt: each field's pages, asked for as the field changes (roomsPreload) */
#define ROOM_PAGES 4096
static struct { int field, w, h, limit; uint64_t texel; } roomPages[ROOM_PAGES];
static int roomCount, roomField=-2;
/* texture_pack_sync=1 (XG_TEXTURE_PACK_SYNC): every page decodes on the game thread as it is asked for
 * (a debug switch: the picture the background loads must give, frame for frame) */
static int pageSyncAlways;
static int roomPreloadAtLoad=1; /* texture_pack_room_preload=0 (XG_TEXTURE_PACK_ROOM_PRELOAD, debug): only at the room's first draw */
#define ROOM_WAIT_NS (50ull * 1000000ull) /* the most a room's first frames wait for its pages, in all */
static uint64_t roomWaitLimitNs=ROOM_WAIT_NS; /* ROOM_WAIT_NS; texture_pack_room_wait_ms (XG_TEXTURE_PACK_ROOM_WAIT_MS, 0..1000, tests) */
/* the room: roomsPreloadField's count, its first texture pack draw's vblank, its waits so far */
static unsigned roomSerial; static int roomDrawn, roomOver; static uint32_t roomDrawVblank; static uint64_t roomWaitNs;
static char root[PATH_BYTES], dir[PATH_BYTES + 32];
static int rootChosen; /* root from texture_pack (the user's own pack), not the default texture-packs library */
/* A Beetle PSX HW replacement pack (beetle_pack; texture_beetle.c names what a draw needs): its folder,
 * read once per reload into an index of the names it holds ('<upload>-<palette>.png', hex either case
 * as on the core's Windows builds, no leading zeros as the core asks for them, top level only), so a
 * miss costs no file system call. Its images are entries of kind 5 (RGBA, alpha 0 clear, 255 opaque,
 * anything between STP). */
#define BEETLE_KIND 5
#define BEETLE_CACHE_MB 512 /* the pack cache's budget at least, with a Beetle pack (beetle_pack_cache_mb) */
static char beetleDir[PATH_BYTES];
static struct { uint32_t upload, palette; char* name; }* beetleNames;
static size_t beetleCap, beetleCount;
static int beetleFirstSetting; /* beetle_pack_order=beetle: its images before ours */
static BeetlePackStats bstats;
static uint64_t beetleGeneration;

const char* texturePackVersion(void) {
#if defined(VERSION_JP)
    return "SLPS-01160";
#else
    return "SLUS-00664";
#endif
}
static uint32_t keyHash(uint64_t texel, uint64_t palette, int indexed, int w, int h) {
    uint64_t k = texel ^ (palette * 0x9E3779B97F4A7C15ull) ^ ((uint64_t)indexed << 61) ^ ((uint64_t)w << 20) ^ (uint64_t)h;
    k ^= k >> 29; k *= 0xBF58476D1CE4E5B9ull; k ^= k >> 32;
    return (uint32_t)k & (PACK_HASH - 1);
}
static void unchain(Entry* e) {
    int32_t idx = (int32_t)(e - entries) + 1;
    int32_t* at = &bucket[keyHash(e->texel, e->palette, e->indexed, e->w, e->h)];
    while (*at && *at != idx) at = &chain[*at - 1];
    if (*at == idx) *at = chain[idx - 1];
}
static void drop(Entry* e) {
    if (e->touched) { unchain(e); --stats.entries; }
    stats.bytes -= e->bytes;
    free(e->rgba); free(e->indices); memset(e,0,sizeof(*e));
}
void texturePackSetGpu(int on) { gpuKeeps = on != 0; }
static uint64_t newRevision(void) { return UINT64_C(0x8000000000000000) | ++revision; }
static char* trim(char* s) {
    while (isspace((unsigned char)*s)) ++s;
    char* end = s + strlen(s);
    while (end > s && isspace((unsigned char)end[-1])) --end;
    *end = 0; return s;
}
static void metadata(void) {
    char path[sizeof(dir)+32], data[8193];
    snprintf(path,sizeof(path),"%s/pack.ini",dir);
    SDL_IOStream* io = SDL_IOFromFile(path,"rb");
    if (!io) return; /* Optional; PNG keys are sufficient. */
    Sint64 size = SDL_GetIOSize(io);
    if (size < 0 || size > 8192 || SDL_ReadIO(io,data,(size_t)size) != (size_t)size) {
        snprintf(stats.message,sizeof(stats.message),"Ignoring unreadable or oversized pack.ini");
        SDL_CloseIO(io); return;
    }
    SDL_CloseIO(io); data[size] = 0;
    char* line = data;
    while (line && *line) {
        char* next = strchr(line,'\n'); if (next) *next++ = 0;
        char* s = trim(line), *eq = strchr(s,'=');
        if (*s != '#' && *s != ';' && eq) {
            *eq++ = 0;
            if (!strcmp(trim(s),"name")) snprintf(stats.name,sizeof(stats.name),"%s",trim(eq));
            else if (!strcmp(s,"author")) snprintf(stats.author,sizeof(stats.author),"%s",trim(eq));
            else if (!strcmp(s,"rect_keys")) rectKeys = SDL_strcasecmp(trim(eq),"no") != 0;
            else if (!strcmp(s,"cache_mb")) {
                long mb = strtol(trim(eq), NULL, 10);
                packBytes = (size_t)(mb < 32 ? 32 : mb > 1024 ? 1024 : mb) * 1024u * 1024u;
            }
            else if (!strcmp(s,"crop_mb")) {   /* 8x field pages crop to 64x the patch pixels */
                long mb = strtol(trim(eq), NULL, 10);
                cropLimit = (size_t)(mb < 96 ? 96 : mb > 1024 ? 1024 : mb) * 1024u * 1024u;
            }
        }
        line = next;
    }
}
static void beetleNamesFree(void);
void texturePackShutdown(void) {
    texturePackReport();   /* the counters go with the entries (libgpu.c's quit) */
    loaderStop();
    if (bstats.enabled && (cpuTimingFromEnv() || settingsGet("beetle_pack_log"))) {
        BeetleStats b; beetleStats(&b);
        fprintf(stderr,"beetle pack: %llu images loaded, %llu missing, %llu invalid; %llu uploads named, %d owning texels; "
                "%llu request walks (%.1f ms in all): %llu one image, %llu composites made, %llu kept (%llu evicted, %zu MB now), %llu without a palette name; %llu draws\n",
                (unsigned long long)bstats.loads,(unsigned long long)bstats.misses,(unsigned long long)bstats.invalid,
                (unsigned long long)b.uploads,b.slots,(unsigned long long)b.walks,b.walkNs*1e-6,(unsigned long long)b.singles,
                (unsigned long long)b.composites,(unsigned long long)b.compositeReuses,(unsigned long long)b.compositeEvictions,b.compositeBytes>>20,
                (unsigned long long)b.paletteMissing,(unsigned long long)b.draws);
    }
    beetleNamesFree(); beetleDir[0]=0; memset(&bstats,0,sizeof(bstats)); beetleBytes=0;
    beetleTrackEnable(0); beetleNameLogOpen(NULL);
    posesClear();
    spriteIndexFree();
    cropsReset();
    for (int i=0;i<PACK_SLOTS;++i) drop(&entries[i]);
    memset(&stats,0,sizeof(stats)); loaded=0; root[0]=dir[0]=0;
}
#define CELL_MEMO 4096
static struct { uint64_t key; uint8_t found, valid; } cellMemo[CELL_MEMO];
static struct { uint64_t key; uint8_t valid, found, a, b, c, e; } probeMemo[CELL_MEMO];
/* ---- field sprite poses (texturePoseGet) ---------------------------------------------------
 * <spriteDir>/poses/<key>.pose ("size W H", "quad a0 b0 a1 b1 a2 b2") and <key>.indices.png beside
 * it: decoded on the pose loader threads (the room preload, above: poses.tsv) or, for a pose not
 * queued or not decoded yet, on the game thread at its first draw. Kept by key, missing ones too,
 * within the sprite pack's pack.ini cache_mb (the poses' index maps): the least recently drawn of
 * the earlier rooms' make way for a room's; a pose drawn in this or the last ordering table
 * (widePoseSerial) is never dropped, as gp0.c keeps the pointer for the table's passes. */
#define POSE_SLOTS 8192                       /* poses and known misses (a Lahan room ~1200, a party doll up to 1700) */
#define POSE_HASH 8192
#define POSE_BYTES (512u * 1024u * 1024u)     /* default; the sprite pack's pack.ini cache_mb */
#define POSE_THREADS 4                        /* at most; a quarter of the logical cores (a Lahan room's 1900 poses in ~25 vblanks) */
typedef struct { int status; SpritePose pose; uint8_t* indices; } PoseRead; /* status 1 loaded, 0 no .pose, -1 invalid */
typedef struct PoseJob { struct PoseJob* next; int slot; uint64_t serial, key; size_t estimate; SDL_AtomicInt cancel; PoseRead r; } PoseJob;
typedef struct {
    uint64_t key, touched, serial;
    unsigned used;     /* widePoseSerial() at its last draw */
    int state;         /* 0 empty, 1 loaded, 2 missing, 3 queued for a pose loader */
    size_t bytes;
    SpritePose pose;
    PoseJob* job;      /* state 3 */
} PoseSlot;
static PoseSlot poses[POSE_SLOTS];
static int32_t poseBucket[POSE_HASH], poseChain[POSE_SLOTS], poseFree[POSE_SLOTS]; /* chains: index + 1, 0 end */
static int poseFreeCount=-1;                  /* -1: the free list not made yet */
static size_t poseBytes, posePendingBytes, posePeak, poseLimit=POSE_BYTES;
static uint64_t poseClock, poseSerials, poseRoomMark;
static unsigned poseRoom;                     /* rooms preloaded so far (partPoseRoom) */
static SDL_Mutex* poseLock; static SDL_Condition* poseWake; static SDL_Thread* poseThreads[POSE_THREADS];
static PoseJob *poseTodo, **poseTodoTail=&poseTodo, *poseDone; static SDL_AtomicInt poseReady; static int poseQuit;
unsigned widePoseSerial(void) __attribute__((weak)); /* wide.c; standalone GPU tests have none */
static unsigned poseNow(void) { return widePoseSerial ? widePoseSerial() : 0; }
static int poseHeld(const PoseSlot* s, unsigned now) { return s->state==1 && now-s->used<2; }
static uint32_t poseHash(uint64_t key) { return (uint32_t)((key^(key>>33))*UINT64_C(0xFF51AFD7ED558CCD)>>40)&(POSE_HASH-1); }
static PoseSlot* poseFind(uint64_t key) {
    for (int32_t i=poseBucket[poseHash(key)];i;i=poseChain[i-1]) if (poses[i-1].key==key) return &poses[i-1];
    return NULL;
}
static void poseDrop(PoseSlot* s) {
    int32_t idx=(int32_t)(s-poses)+1, *at=&poseBucket[poseHash(s->key)];
    while (*at && *at!=idx) at=&poseChain[*at-1];
    if (*at==idx) *at=poseChain[idx-1];
    if (s->job) SDL_SetAtomicInt(&s->job->cancel,1);
    if (s->state==1) { ++stats.poseEvictions; poseBytes-=s->bytes; }
    free((void*)s->pose.indices); memset(s,0,sizeof(*s));
    poseFree[poseFreeCount++]=idx-1;
}
/* Pose pixels down by `need` bytes: the earlier rooms' (touched before this room's preload), least
 * recently used first, never one held (poseHeld). The freed bytes. */
static int touchedOrder(const void* x, const void* y) {
    uint64_t a=poses[*(const int32_t*)x].touched, b=poses[*(const int32_t*)y].touched;
    return a<b ? -1 : a>b;
}
static size_t poseEvict(size_t need) {
    static int32_t order[POSE_SLOTS];
    unsigned now=poseNow(); int n=0; size_t freed=0;
    for (int i=0;i<POSE_SLOTS;++i)
        if (poses[i].state==1 && poses[i].touched<poseRoomMark && !poseHeld(&poses[i],now)) order[n++]=i;
    qsort(order,(size_t)n,sizeof(order[0]),touchedOrder);
    for (int i=0;i<n && freed<need;++i) { freed+=poses[order[i]].bytes; poseDrop(&poses[order[i]]); }
    return freed;
}
static PoseSlot* poseNew(uint64_t key) {
    if (poseFreeCount<0) { poseFreeCount=0; for (int i=POSE_SLOTS-1;i>=0;--i) poseFree[poseFreeCount++]=i; }
    if (!poseFreeCount) {   /* every slot used: the least recently used one not held */
        unsigned now=poseNow(); PoseSlot* victim=NULL;
        for (int i=0;i<POSE_SLOTS;++i) if ((poses[i].state==1 || poses[i].state==2) && !poseHeld(&poses[i],now) &&
                                             (!victim || poses[i].touched<victim->touched)) victim=&poses[i];
        if (!victim) return NULL;
        poseDrop(victim);
    }
    PoseSlot* s=&poses[poseFree[--poseFreeCount]];
    memset(s,0,sizeof(*s));
    s->key=key; s->touched=++poseClock; s->used=poseNow()-2; s->serial=++poseSerials;
    uint32_t h=poseHash(key); poseChain[s-poses]=poseBucket[h]; poseBucket[h]=(int32_t)(s-poses)+1;
    return s;
}
/* One pose's files (any thread: spriteDir is fixed while the loaders run). */
static void poseRead(uint64_t key, PoseRead* r) {
    memset(r,0,sizeof(*r));
    char path[sizeof(spriteDir)+64], line[160];
    snprintf(path,sizeof(path),"%s/poses/%016" PRIx64 ".pose",spriteDir,key);
    FILE* f=fopen(path,"r");
    if (!f) return;
    SpritePose p={0}; int got=0;
    while (fgets(line,sizeof(line),f)) {
        if (sscanf(line,"size %d %d",&p.w,&p.h)==2) got|=1;
        else if (sscanf(line,"quad %f %f %f %f %f %f",&p.a[0],&p.b[0],&p.a[1],&p.b[1],&p.a[2],&p.b[2])==6) got|=2;
    }
    fclose(f);
    r->status=-1;
    if (got!=3 || p.w<1 || p.h<1 || p.w>256 || p.h>256) { fprintf(stderr,"sprite pose: invalid %s\n",path); return; }
    snprintf(path,sizeof(path),"%s/poses/%016" PRIx64 ".indices.png",spriteDir,key);
    int found=0;
    SDL_Surface* image=texturePackReadPng(path,p.w,p.h,&p.scale,&found);
    if (!image) { fprintf(stderr,"sprite pose: %s %s\n",found?"invalid":"missing",path); return; }
    size_t count=(size_t)image->w*image->h;
    uint8_t* idx=malloc(count); int ok=idx!=NULL;
    for (int y=0;ok && y<image->h;++y) for (int x=0;x<image->w;++x) {
        uint8_t* q=(uint8_t*)image->pixels+(size_t)y*image->pitch+x*4;
        if (q[0]!=q[1] || q[0]!=q[2] || q[3]!=255 || q[0]>=16) { ok=0; break; }
        idx[(size_t)y*image->w+x]=q[0];
    }
    SDL_DestroySurface(image);
    if (!ok) { free(idx); fprintf(stderr,"sprite pose: invalid indices in %s\n",path); return; }
    p.indices=idx; r->pose=p; r->indices=idx; r->status=1;
}
static void poseInstall(PoseSlot* s, PoseRead* r) {
    s->job=NULL;
    if (r->status!=1) { s->state=2; return; }
    s->pose=r->pose; s->pose.revision=newRevision(); r->indices=NULL;
    s->bytes=(size_t)s->pose.w*s->pose.h*s->pose.scale*s->pose.scale;
    poseBytes+=s->bytes; s->state=1; ++stats.poseLoads;
    if (poseBytes+posePendingBytes>poseLimit) poseEvict(poseBytes+posePendingBytes-poseLimit);
    if (poseBytes>posePeak) posePeak=poseBytes;
}
static int SDLCALL poseLoaderMain(void* unused) {
    (void)unused;
    SDL_LockMutex(poseLock);
    for (;;) {
        while (!poseTodo && !poseQuit) SDL_WaitCondition(poseWake,poseLock);
        if (poseQuit) break;
        PoseJob* j=poseTodo; poseTodo=j->next; if (!poseTodo) poseTodoTail=&poseTodo;
        SDL_UnlockMutex(poseLock);
        if (!SDL_GetAtomicInt(&j->cancel)) poseRead(j->key,&j->r);   /* drawn meanwhile: decoded on the game thread */
        SDL_LockMutex(poseLock);
        j->next=poseDone; poseDone=j; SDL_AddAtomicInt(&poseReady,1);
    }
    SDL_UnlockMutex(poseLock);
    return 0;
}
static void posesStop(void) {
    if (!poseLock) return;
    SDL_LockMutex(poseLock); poseQuit=1; SDL_BroadcastCondition(poseWake); SDL_UnlockMutex(poseLock);
    for (int i=0;i<POSE_THREADS;++i) if (poseThreads[i]) { SDL_WaitThread(poseThreads[i],NULL); poseThreads[i]=NULL; }
    poseQuit=0;
    for (PoseJob* l[2]={poseTodo,poseDone}, **p=l; p<l+2; ++p) while (*p) { PoseJob* j=*p; *p=j->next; free(j->r.indices); free(j); }
    poseTodo=poseDone=NULL; poseTodoTail=&poseTodo; SDL_SetAtomicInt(&poseReady,0); posePendingBytes=0;
}
/* The finished decodes, installed (game thread). */
static void posePoll(void) {
    if (!poseLock || !SDL_GetAtomicInt(&poseReady)) return;
    SDL_LockMutex(poseLock); PoseJob* done=poseDone; poseDone=NULL; SDL_SetAtomicInt(&poseReady,0); SDL_UnlockMutex(poseLock);
    while (done) {
        PoseJob* j=done; done=j->next;
        posePendingBytes-=j->estimate<=posePendingBytes?j->estimate:posePendingBytes;
        PoseSlot* s=&poses[j->slot];
        if (s->state==3 && s->job==j && s->serial==j->serial) { poseInstall(s,&j->r); if (s->state==1) ++stats.poseLoader; }
        free(j->r.indices); free(j);
    }
}
/* The pose loaders, started with the first poses queued; the number running. */
static int posesStart(void) {
    if (!poseLock) { poseLock=SDL_CreateMutex(); poseWake=SDL_CreateCondition(); }
    if (!poseLock || !poseWake) return 0;
    int running=0, want=SDL_GetNumLogicalCPUCores()/4;
    want=want<1?1:want>POSE_THREADS?POSE_THREADS:want;
    for (int i=0;i<want;++i) {
        if (!poseThreads[i]) poseThreads[i]=SDL_CreateThread(poseLoaderMain,"sprite-pose-loader",NULL);
        running+=poseThreads[i]!=NULL;
    }
    return running;
}
/* Queued poses go to the loaders in one hand-over (the list from head, its last next at *tail). */
static void posesQueue(PoseJob* head, PoseJob** tail) {
    if (!head) return;
    SDL_LockMutex(poseLock);
    *poseTodoTail=head; poseTodoTail=tail;
    SDL_BroadcastCondition(poseWake); SDL_UnlockMutex(poseLock);
}
/* Every pose of these parts (indices into spritePartNames) queued for the loaders, a pose of each part
 * in turn (each actor's first poses come first); the ones already here are touched (this room keeps
 * them). Earlier rooms' poses make way within the budget; what still does not fit is left out (its
 * first draw decodes it). */
static void posesQueueParts(const int* parts, int n) {
    if (!n || !posesStart()) return;
    size_t need=0;
    for (int i=0;i<n;++i) for (int k=0;k<partPoseCount[parts[i]];++k) {
        const SpritePoseRow* r=&spritePoses[partPoseFirst[parts[i]]+k];
        PoseSlot* s=poseFind(r->key);
        if (s) s->touched=++poseClock;
        else need+=(size_t)r->w*r->h*r->scale*r->scale;
    }
    if (poseBytes+posePendingBytes+need>poseLimit) poseEvict(poseBytes+posePendingBytes+need-poseLimit);
    PoseJob *head=NULL, **tail=&head;
    for (int k=0,more=1; more; ++k) {
        more=0;
        for (int i=0;i<n;++i) {
            if (k>=partPoseCount[parts[i]]) continue;
            more=1;
            const SpritePoseRow* r=&spritePoses[partPoseFirst[parts[i]]+k];
            if (poseFind(r->key)) continue;   /* here, queued, a known miss, or in two parts */
            size_t estimate=(size_t)r->w*r->h*r->scale*r->scale;
            if (poseBytes+posePendingBytes+estimate>poseLimit) { ++stats.poseSkipped; continue; }
            PoseJob* j=calloc(1,sizeof(*j)); PoseSlot* s=j?poseNew(r->key):NULL;
            if (!s) { free(j); ++stats.poseSkipped; continue; }
            s->state=3; s->job=j;
            j->slot=(int)(s-poses); j->serial=s->serial; j->key=r->key; j->estimate=estimate;
            posePendingBytes+=estimate; ++stats.posePreloads;
            *tail=j; tail=&j->next;
        }
    }
    posesQueue(head,tail);
}
/* A pose drawn that no preload asked for: the rest of its parts' poses (a scene's actor, a doll with
 * no NPC slot), once a room. */
static void poseFirstSight(uint64_t key) {
    if (!spritePreloadOn || !spritePoseCount) return;
    int lo=0, hi=spritePoseCount, parts[8], n=0;
    while (lo<hi) { int mid=(lo+hi)/2; if (spritePoses[spritePoseByKey[mid]].key<key) lo=mid+1; else hi=mid; }
    for (int i=lo;i<spritePoseCount && n<8 && spritePoses[spritePoseByKey[i]].key==key;++i) {
        int part=spritePoses[spritePoseByKey[i]].part;
        if (partPoseRoom[part]==poseRoom+1) continue;
        partPoseRoom[part]=poseRoom+1; parts[n++]=part;
        ++stats.posePartsSeen;
    }
    posesQueueParts(parts,n);
}
static void posesClear(void) {
    posesStop();
    for (int i=0;i<POSE_SLOTS;++i) free((void*)poses[i].pose.indices);
    memset(poses,0,sizeof(poses)); memset(poseBucket,0,sizeof(poseBucket));
    poseFreeCount=-1; poseBytes=posePendingBytes=0;
}
/* The pack files into the page cache, in the background, as the packs are chosen: sprite
 * poses, glyphs, Cel UI and portraits are read on the game thread at their first draw, and a
 * file the system no longer holds in memory there was a disk read in the middle of a frame
 * (a battle's first poses after a while away: 40-100 ms stalls, the sound starved;).
 * The kernel reads ahead (posix_fadvise WILLNEED), at idle I/O priority. The field pages are
 * not warmed: they load on their own thread, and their 0.5 GB read at start competed with the
 * disc image's reads (XA music stalled). Linux only; elsewhere a no-op. */
typedef struct { int count; char paths[6][PATH_BYTES+64]; } WarmList;
#ifdef __linux__
static SDL_EnumerationResult SDLCALL warmEntry(void* user, const char* dirname, const char* name) {
    (void)user;
    char path[PATH_BYTES*2+64];
    snprintf(path,sizeof(path),"%s%s",dirname,name);
    SDL_PathInfo info;
    if (!SDL_GetPathInfo(path,&info)) return SDL_ENUM_CONTINUE;
    if (info.type==SDL_PATHTYPE_DIRECTORY) SDL_EnumerateDirectory(path,warmEntry,NULL);
    else if (info.type==SDL_PATHTYPE_FILE) {
        int fd=open(path,O_RDONLY|O_CLOEXEC);
        if (fd>=0) { posix_fadvise(fd,0,0,POSIX_FADV_WILLNEED); close(fd); }
    }
    return SDL_ENUM_CONTINUE;
}
static int SDLCALL warmMain(void* data) {
    WarmList* list=data;
    /* out of the game's way: idle I/O class (the disc image's reads come first) and the lowest
     * CPU priority */
    syscall(SYS_ioprio_set, 1 /* IOPRIO_WHO_PROCESS: this thread */, 0, (3 << 13) /* IOPRIO_CLASS_IDLE */);
    setpriority(PRIO_PROCESS, (id_t)syscall(SYS_gettid), 19);
    for (int i=0;i<list->count;++i) SDL_EnumerateDirectory(list->paths[i],warmEntry,NULL);
    free(list);
    return 0;
}
#endif
static void warmPacks(const char* const* dirs, int count) {
#ifdef __linux__
    static char last[sizeof(((WarmList*)0)->paths)];
    WarmList* list=calloc(1,sizeof(*list)); if (!list) return;
    for (int i=0;i<count && list->count<6;++i)
        if (dirs[i] && dirs[i][0]) snprintf(list->paths[list->count++],sizeof(list->paths[0]),"%s",dirs[i]);
    if (!memcmp(last,list->paths,sizeof(last))) { free(list); return; }   /* already warmed */
    memcpy(last,list->paths,sizeof(last));
    SDL_Thread* t=SDL_CreateThread(warmMain,"pack-warmup",list);
    if (t) SDL_DetachThread(t); else free(list);
#else
    (void)dirs; (void)count;
#endif
}

/* The sprite pack's pack.ini: cache_mb=N (64..4096), the pose cache's budget (the index maps of the
 * poses kept; default 512: a room's NPCs and a party of three, at 8x). */
static void spriteMetadata(void) {
    char path[sizeof(spriteRoot)+16], line[256];
    snprintf(path,sizeof(path),"%s/pack.ini",spriteRoot);
    FILE* f=fopen(path,"r"); if (!f) return;
    while (fgets(line,sizeof(line),f)) {
        char* s=trim(line), *eq=strchr(s,'=');
        if (*s=='#' || *s==';' || !eq) continue;
        *eq++=0;
        if (!strcmp(trim(s),"cache_mb")) {
            long mb=strtol(trim(eq),NULL,10);
            poseLimit=(size_t)(mb<64?64:mb>4096?4096:mb)*1024u*1024u;
        }
    }
    fclose(f);
}
static void beetleReload(void);
void texturePackReload(void) {
    memset(cellMemo,0,sizeof(cellMemo)); memset(probeMemo,0,sizeof(probeMemo)); posesClear(); /* another library root: probe again */
    ++generation;
    /* Load settings only once; a UI edit remains authoritative this run. */
    if (!loaded) {
        loaded=1;
        settingsDescribe("texture_pack","PNG pack root (contains SLUS-00664 or SLPS-01160); default texture-packs beside the executable (3D textures -> Cel 8x: the field texture library); none disables; enhanced display only");
        const char* s=settingsGet("texture_pack");
        rootChosen=s!=NULL;
        if (s && strlen(s) < sizeof(root)) snprintf(root,sizeof(root),"%s",s);
        else if (s) fprintf(stderr,"texture pack: root path too long\n");
        else { /* beside this or the other engine's executable, as the sprite and glyph libraries */
            char packRoot[PATH_BYTES]; enginePackDir("texture-packs",packRoot,sizeof(packRoot));
            SDL_PathInfo info; char version[PATH_BYTES+16];
            snprintf(version,sizeof(version),"%s/%s",packRoot,texturePackVersion());
            if (SDL_GetPathInfo(version,&info) && info.type==SDL_PATHTYPE_DIRECTORY && strlen(packRoot)<sizeof(root)) snprintf(root,sizeof(root),"%s",packRoot);
        }
    }
    loaderStop();   /* its finished loads would name entries dropped here */
    cropsReset();
    for (int i=0;i<PACK_SLOTS;++i) drop(&entries[i]);
    memset(&stats,0,sizeof(stats)); dir[0]=0; rectKeys=1; packBytes=PACK_BYTES; cropLimit=CROP_BYTES; roomCount=0; roomField=-2;
    roomSerial=0; roomDrawn=roomOver=0; roomWaitNs=0;
    {
        const char* sync=settingsGet("texture_pack_sync");   /* XG_TEXTURE_PACK_SYNC=1: a debug switch, no menu row */
        pageSyncAlways=sync && !strcmp(sync,"1");
        const char* early=settingsGet("texture_pack_room_preload");
        roomPreloadAtLoad=!(early && !strcmp(early,"0"));
        const char* wait=settingsGet("texture_pack_room_wait_ms");
        long ms=wait?strtol(wait,NULL,10):-1;
        roomWaitLimitNs=ms<0?ROOM_WAIT_NS:(uint64_t)(ms>1000?1000:ms)*1000000u;
    }
    settingsDescribe("glyph_packs","HD lettering library; default glyph-packs beside the executable; none keeps native glyphs");
    const char* glyphRoot=settingsGet("glyph_packs");
    glyphDir[0]=0;
    if(!glyphRoot){char glyphRootDir[PATH_BYTES];enginePackDir("glyph-packs",glyphRootDir,sizeof(glyphRootDir));snprintf(glyphDir,sizeof(glyphDir),"%s/%s",glyphRootDir,texturePackVersion());}
    if(glyphDir[0])fprintf(stderr,"glyph packs: %s\n",glyphDir);
    else if(strlen(glyphRoot)<PATH_BYTES && SDL_strcasecmp(glyphRoot,"none"))snprintf(glyphDir,sizeof(glyphDir),"%s/%s",glyphRoot,texturePackVersion());
    SDL_PathInfo glyphInfo;
    if(glyphDir[0] && (!SDL_GetPathInfo(glyphDir,&glyphInfo) || glyphInfo.type!=SDL_PATHTYPE_DIRECTORY))glyphDir[0]=0;
    settingsDescribe("sprite_packs","Field sprite cel library (Sprites -> Cel 8x); default sprite-packs beside the executable; none keeps native sprites");
    const char* spriteSetting=settingsGet("sprite_packs");
    spriteDir[0]=spriteRoot[0]=0; spriteIndexFree();
    if(!spriteSetting)enginePackDir("sprite-packs",spriteRoot,sizeof(spriteRoot));
    else if(strlen(spriteSetting)<PATH_BYTES && SDL_strcasecmp(spriteSetting,"none"))snprintf(spriteRoot,sizeof(spriteRoot),"%s",spriteSetting);
    if(spriteRoot[0])snprintf(spriteDir,sizeof(spriteDir),"%s/%s",spriteRoot,texturePackVersion());
    SDL_PathInfo spriteInfo;
    if(spriteDir[0] && (!SDL_GetPathInfo(spriteDir,&spriteInfo) || spriteInfo.type!=SDL_PATHTYPE_DIRECTORY))spriteDir[0]=spriteRoot[0]=0;
    settingsDescribe("sprite_preload","Sprite pack room preload: a field's NPC cels and poses and the party's poses decode in the background as it loads (1, default) or at their first draw (0)");
    const char* preload=settingsGet("sprite_preload");
    spritePreloadOn=!(preload && !strcmp(preload,"0"));
    poseLimit=POSE_BYTES;
    if(spriteDir[0]){spriteIndexRead();spriteMetadata();}
    if(spriteDir[0])fprintf(stderr,"sprite packs: %s%s\n",spriteDir,spriteNpcCount||spritePoseCount?" (room preload)":"");
    if((spriteCelCount||spritePoseCount) && !spriteReportArmed){spriteReportArmed=1;atexit(texturePackReport);}
    {
        char ui[PATH_BYTES], portraits[PATH_BYTES];
        enginePackDir("ui-packs",ui,sizeof(ui));
        enginePackDir("portrait-packs",portraits,sizeof(portraits));
        const char* warm[]={spriteDir,glyphDir,ui,portraits};
        warmPacks(warm,4);
    }
    beetleReload();
    if (!root[0] || !SDL_strcasecmp(root,"none")) {
        snprintf(stats.message,sizeof(stats.message),"Disabled"); return;
    }
    snprintf(dir,sizeof(dir),"%s/%s",root,texturePackVersion());
    SDL_PathInfo info;
    if (!SDL_GetPathInfo(dir,&info) || info.type != SDL_PATHTYPE_DIRECTORY) {
        snprintf(stats.message,sizeof(stats.message),"Version directory unavailable; using original textures");
        fprintf(stderr,"texture pack: %s: %s\n",dir,stats.message); return;
    }
#if SDL_VERSION_ATLEAST(3,4,0)
    stats.enabled=1;
    snprintf(stats.message,sizeof(stats.message),"Ready; PNGs load when their textures are drawn");
    metadata();
    roomsRead();
    fprintf(stderr,"texture pack: %s%s%s%s\n",dir,stats.name[0]?" - ":"",stats.name,roomCount?" (room preload)":"");
#else
    snprintf(stats.message,sizeof(stats.message),"PNG packs require SDL 3.4");
#endif
}
static void load(void) { if (!loaded) texturePackReload(); }
const char* texturePackRoot(void) { load(); return root; }
int texturePackEnabled(void) { load(); return stats.enabled; }
/* A pack the user chose (texture_pack): the legacy appearance applies it to every category; the
 * default texture-packs library applies only where a category picks it (3D textures -> Cel 8x). */
int texturePackChosen(void) { load(); return stats.enabled && rootChosen; }
uint64_t texturePackGeneration(void) { load(); return generation; }
void textureReplacementsShow(int on) { replacementsHidden = !on; }
int textureReplacementsShown(void) { return !replacementsHidden; }
const char* textureSpritePackRoot(void) { load(); return spriteRoot; }
const char* textureGlyphPackDir(void) { load(); return glyphDir; }
void texturePackLoadTimes(double ms[3], double worst[3], unsigned slow[3]) {
    for (int k=0;k<3;++k) { ms[k]=loadKinds[k].ms; worst[k]=loadKinds[k].worst; slow[k]=loadKinds[k].slow; }
}
void texturePackStats(TexturePackStats* out) { load(); *out=stats; }
void texturePackSetRoot(const char* s) {
    load(); if (!s) s="";
    if (strlen(s) >= sizeof(root)) {
        snprintf(stats.message,sizeof(stats.message),"Root path too long; previous selection kept"); return;
    }
    memmove(root,s,strlen(s)+1); rootChosen=1;
    settingsSet("texture_pack",root[0]?root:"none"); settingsSave(); texturePackReload();
}
static uint32_t be32(const uint8_t* p) {
    return (uint32_t)p[0]<<24 | (uint32_t)p[1]<<16 | (uint32_t)p[2]<<8 | p[3];
}
/* Reject unreasonable sizes before the PNG decoder allocates anything. A
 * replacement has the same aspect ratio and an integer 1..8 scale. */
SDL_Surface* texturePackReadPng(const char* path, int w, int h, int* scale, int* found) {
    SDL_PathInfo info;
    if (!SDL_GetPathInfo(path,&info)) return NULL;
    *found=1;
    if (info.type != SDL_PATHTYPE_FILE || info.size > PNG_BYTES) return NULL;
    SDL_IOStream* io=SDL_IOFromFile(path,"rb"); if (!io) return NULL;
    uint8_t head[33];
    int ok=SDL_ReadIO(io,head,sizeof(head))==sizeof(head) &&
        !memcmp(head,"\x89PNG\r\n\x1a\n",8) && be32(head+8)==13 && !memcmp(head+12,"IHDR",4);
    uint32_t pw=ok?be32(head+16):0, ph=ok?be32(head+20):0;
    ok=ok && pw && ph && pw<=2048 && ph<=2048 && (uint64_t)pw*ph<=PNG_PIXELS &&
        pw%(unsigned)w==0 && ph%(unsigned)h==0 && pw/(unsigned)w==ph/(unsigned)h && pw/(unsigned)w<=8;
    if (!ok || SDL_SeekIO(io,0,SDL_IO_SEEK_SET)<0) { SDL_CloseIO(io); return NULL; }
    {
        /* an uncompressed PNG (stored deflate blocks): read without inflating (pngfast.c) */
        size_t n=0; uint8_t* file=SDL_LoadFile_IO(io,&n,false);
        int other=0, sw=0, sh=0;
        uint8_t* px=file?pngDecodeStored(file,n,&sw,&sh,2048,PNG_PIXELS,&other):NULL;
        SDL_free(file);
        if (px) {
            SDL_CloseIO(io);
            if (sw!=(int)pw || sh!=(int)ph) { free(px); return NULL; }
            SDL_Surface* s=SDL_CreateSurface(sw,sh,SDL_PIXELFORMAT_RGBA32);
            if (s) for (int y=0;y<sh;++y) memcpy((uint8_t*)s->pixels+(size_t)y*s->pitch,px+(size_t)y*sw*4,(size_t)sw*4);
            free(px);
            *scale=(int)pw/w; return s;
        }
        if (SDL_SeekIO(io,0,SDL_IO_SEEK_SET)<0) { SDL_CloseIO(io); return NULL; }
    }
#if SDL_VERSION_ATLEAST(3,4,0)
    SDL_Surface* source=SDL_LoadPNG_IO(io,true); if (!source) return NULL;
    if (source->w!=(int)pw || source->h!=(int)ph) { SDL_DestroySurface(source); return NULL; }
    SDL_Surface* out=SDL_ConvertSurface(source,SDL_PIXELFORMAT_RGBA32); SDL_DestroySurface(source);
    *scale=(int)pw/w; return out;
#else
    SDL_CloseIO(io); return NULL;
#endif
}
/* The least recently used slot (an empty one first); one whose load is still running only when every
 * other is (a room's pages queued at its field load are older than the cels queued after them). */
static Entry* oldest(Entry* keep) {
    Entry *out=NULL, *busy=NULL;
    for (int i=0;i<PACK_SLOTS;++i) {
        Entry* e=&entries[i];
        if (e==keep) continue;
        if (e->pending) { if (!busy || e->touched<busy->touched) busy=e; }
        else if (!out || e->touched<out->touched) out=e;
    }
    return out?out:busy;
}
/* A decoded replacement: RGBA, or an index map (its RGBA buffer is made later, entryRgba). */
typedef struct { uint8_t *rgba, *indices; int scale; } Decoded;
/* Decode one replacement file for a w x h texture (any thread: no shared state). An index
 * map stored uncompressed as 8-bit grey (pngfast) is read straight into indices; anything
 * else goes through texturePackReadPng. 1 when usable; *found 0 when the file is absent. */
static int decodeBeetle(const char* path, int w, int h, Decoded* out, int* found);
static int decodeImage(const char* path, int w, int h, int indexed, int limit, Decoded* out, int* found) {
    memset(out,0,sizeof(*out)); *found=0;
    if (indexed==BEETLE_KIND) return decodeBeetle(path,w,h,out,found);
    if (indexed) {
        size_t n=0; uint8_t* file=NULL; SDL_PathInfo info;
        if (SDL_GetPathInfo(path,&info) && info.type==SDL_PATHTYPE_FILE && info.size<=PNG_BYTES) file=SDL_LoadFile(path,&n);
        if (file) {
            int sw=0, sh=0, other=0; *found=1;
            uint8_t* px=pngDecodeStoredGray(file,n,&sw,&sh,2048,PNG_PIXELS,&other);
            SDL_free(file);
            if (px) {
                int ok=sw%w==0 && sh%h==0 && sw/w==sh/h && sw/w>=1 && sw/w<=8;
                size_t count=(size_t)sw*sh;
                for (size_t i=0;ok && i<count;++i) if (px[i]>=limit) ok=0;
                if (!ok) { free(px); return 0; }
                out->indices=px; out->scale=sw/w; return 1;
            }
        }
    }
    SDL_Surface* image=texturePackReadPng(path,w,h,&out->scale,found);
    if (!image) return 0;
    size_t count=(size_t)image->w*image->h;
    if (indexed) out->indices=malloc(count); else out->rgba=malloc(count*4);
    int ok=indexed?out->indices!=NULL:out->rgba!=NULL;
    for (int y=0;ok && y<image->h;++y) for (int x=0;x<image->w;++x) {
        uint8_t* p=(uint8_t*)image->pixels+(size_t)y*image->pitch+x*4;
        size_t at=(size_t)y*image->w+x;
        if (indexed) {
            if (p[0]!=p[1] || p[0]!=p[2] || p[3]!=255 || p[0]>=limit) { ok=0; break; }
            out->indices[at]=p[0];
        } else {
            memcpy(out->rgba+at*4,p,4);
            out->rgba[at*4+3]=p[3]<64?0:p[3]<192?128:255;
        }
    }
    SDL_DestroySurface(image);
    if (!ok) { free(out->rgba); free(out->indices); memset(out,0,sizeof(*out)); }
    return ok;
}
/* A Beetle pack's replacement for an upload of w x h texels (any thread). As the core takes them
 * (measured, docs/BEETLE_PACKS.md): a PNG of 8-bit channels (RGB, RGBA, grey, grey and alpha,
 * palette; 16-bit ones are refused), the upload's size times 1, 2, 4, 8 or 16 on both axes; alpha 0
 * clear, 255 opaque, anything between the STP flag. Here at most 8x and 4096 x 4096, 4M pixels:
 * bigger ones are halved until they fit (every other pixel; alpha classes stay crisp). */
#define BEETLE_PNG_BYTES (64u * 1024u * 1024u)
static int decodeBeetle(const char* path, int w, int h, Decoded* out, int* found) {
    SDL_PathInfo info;
    if (!SDL_GetPathInfo(path,&info)) return 0;
    *found=1;
    if (info.type!=SDL_PATHTYPE_FILE || info.size>BEETLE_PNG_BYTES || w<1 || h<1) return 0;
    SDL_IOStream* io=SDL_IOFromFile(path,"rb"); if (!io) return 0;
    uint8_t head[26];
    int ok=SDL_ReadIO(io,head,sizeof(head))==sizeof(head) && !memcmp(head,"\x89PNG\r\n\x1a\n",8) &&
           be32(head+8)==13 && !memcmp(head+12,"IHDR",4);
    uint32_t pw=ok?be32(head+16):0, ph=ok?be32(head+20):0;
    int bits=ok?head[24]:0;
    int scale=w>0 && pw%(unsigned)w==0 ? (int)(pw/(unsigned)w) : 0;
    ok=ok && bits<=8 && scale>=1 && scale<=16 && !(scale&(scale-1)) && ph==(uint32_t)h*(uint32_t)scale;
    if (!ok || SDL_SeekIO(io,0,SDL_IO_SEEK_SET)<0) {
        SDL_CloseIO(io);
        if (ok==0 && pw) fprintf(stderr,"beetle pack: %s: %ux%u is not the upload's %dx%d times 1, 2, 4, 8 or 16 (or not 8 bits)\n",path,pw,ph,w,h);
        return 0;
    }
#if SDL_VERSION_ATLEAST(3,4,0)
    SDL_Surface* source=SDL_LoadPNG_IO(io,true); if (!source) return 0;
    SDL_Surface* image=SDL_ConvertSurface(source,SDL_PIXELFORMAT_RGBA32); SDL_DestroySurface(source);
    if (!image) return 0;
    if (image->w!=(int)pw || image->h!=(int)ph) { SDL_DestroySurface(image); return 0; }
    int step=1;
    while (scale/step>8 || (uint64_t)w*(scale/step)>4096 || (uint64_t)h*(scale/step)>4096 ||
           (uint64_t)w*h*(scale/step)*(scale/step)>4096u*1024u) {
        if (scale/step==1) { SDL_DestroySurface(image); return 0; }
        step*=2;
    }
    int s2=scale/step, ow=w*s2, oh=h*s2;
    out->rgba=malloc((size_t)ow*oh*4);
    if (!out->rgba) { SDL_DestroySurface(image); return 0; }
    for (int y=0;y<oh;++y) {
        const uint8_t* row=(const uint8_t*)image->pixels+(size_t)y*step*image->pitch;
        uint8_t* dst=out->rgba+(size_t)y*ow*4;
        for (int x=0;x<ow;++x) {
            memcpy(dst+x*4,row+(size_t)x*step*4,4);
            uint8_t a=dst[x*4+3];
            dst[x*4+3]=a==0?0:a==255?255:128;
        }
    }
    SDL_DestroySurface(image);
    out->scale=s2;
    return 1;
#else
    SDL_CloseIO(io); return 0;
#endif
}
/* Loaded: an RGBA image, or an index map (with or without its RGBA buffer yet). */
static int entryLoaded(const Entry* e) { return e->rgba || e->indices; }
/* The least recently used other entries dropped until bytes more fit the cache. */
static void cacheRoom(Entry* keep, size_t bytes) {
    while (stats.bytes+bytes>cacheBudget()) {
        Entry* victim=NULL;
        for (int i=0;i<PACK_SLOTS;++i) if (&entries[i]!=keep && entries[i].bytes &&
            (!victim || entries[i].touched<victim->touched)) victim=&entries[i];
        if (!victim) break;
        ++stats.evictions; drop(victim);
    }
}
static void install(Entry* e, Decoded* dec) {
    size_t count=(size_t)e->w*e->h*dec->scale*dec->scale, bytes=count*(e->indexed && e->indexed!=BEETLE_KIND?1:4);
    if (e->indexed==BEETLE_KIND) { ++bstats.loads; ++beetleGeneration; }
    cacheRoom(e,bytes);
    e->rgba=dec->rgba; e->indices=dec->indices; e->scale=dec->scale;
    e->bytes=bytes; stats.bytes+=bytes; ++stats.loads;
    e->revision=e->loaded=newRevision();
}
/* An index map's RGBA buffer, made lazily at its first CPU-path recolour (the game thread; never on the GPU path). */
static int entryRgba(Entry* e) {
    if (e->rgba) return 1;
    size_t bytes=(size_t)e->w*e->h*e->scale*e->scale*4;
    cacheRoom(e,bytes);
    if (!(e->rgba=malloc(bytes))) return 0;
    e->bytes+=bytes; stats.bytes+=bytes; e->livePalette=0;   /* recoloured next (sampleEntry) */
    return 1;
}
static void invalid(const char* path, uint64_t key) {
    ++stats.invalid;
    snprintf(stats.message,sizeof(stats.message),"Invalid PNG for %016" PRIx64 "; using original or index fallback",key);
    fprintf(stderr,"texture pack: invalid PNG %s\n",path);
}

/* The loader threads: big replacements (a field texture page at 8x is 4M pixels) decode
 * there, so a room's first frames do not wait for them; each is installed by the game
 * thread at its next look-up (poll). Pages go before sprite cels (a room's pages are queued
 * at its field load, its cels with them). A page drawn while still loading is drawn as the
 * original, except in a room's first frames (roomFirstFrames): there its draw waits for the
 * loader, or decodes it there and then when no loader has started it (pageWait), within
 * ROOM_WAIT_NS a room, so a room entered does not show its native textures first (xg-hbc.27). */
#define ASYNC_FROM (1024 * 1024) /* file bytes above which a replacement loads in the background */
#define LOADER_THREADS 2         /* a room's dozen 4 MB pages in half the time; the decodes are I/O and copies */
#define ROOM_FIRST_VBLANKS 60    /* a room's first frames: this many vblanks from its first page draw */
typedef struct Job {
    struct Job* next; int slot, page, state; /* state 0 queued, 1 decoding, 2 done (jobLock) */
    uint64_t serial; uint64_t key; int w, h, indexed, limit, ok, found; size_t estimate; Decoded dec; char path[PATH_BYTES+32+80];
} Job;
static SDL_Mutex* jobLock; static SDL_Condition *jobWake, *jobFinished; static SDL_Thread* loaders[LOADER_THREADS];
static Job *pagesTodo, *celsTodo, *jobsDone; static SDL_AtomicInt jobsReady; static int loaderQuit;
static uint64_t serials;
static int lookupDraw; /* texturePackSample's look-ups: a page drawn now (pageDrawnLoading) */
uint32_t hwVSyncCount(void) __attribute__((weak)); /* port/src/system/hw.c; standalone tests have none */
static int loaderMain(void* unused) {
    (void)unused;
    SDL_LockMutex(jobLock);
    for (;;) {
        while (!pagesTodo && !celsTodo && !loaderQuit) SDL_WaitCondition(jobWake,jobLock);
        if (loaderQuit) break;
        Job** from=pagesTodo?&pagesTodo:&celsTodo;
        Job* j=*from; *from=j->next; j->state=1;
        SDL_UnlockMutex(jobLock);
        j->ok=decodeImage(j->path,j->w,j->h,j->indexed,j->limit,&j->dec,&j->found);
        SDL_LockMutex(jobLock);
        j->state=2; j->next=jobsDone; jobsDone=j; SDL_AddAtomicInt(&jobsReady,1);
        SDL_BroadcastCondition(jobFinished);
    }
    SDL_UnlockMutex(jobLock);
    return 0;
}
/* estimate: the bytes a preloaded cel will take, counted against the cache budget until the job is back;
 * page: a big replacement (the pages' queue, ahead of the cels') */
static int enqueue(Entry* e, const char* path, size_t estimate, int page) {
    if (!jobLock) { jobLock=SDL_CreateMutex(); jobWake=SDL_CreateCondition(); jobFinished=SDL_CreateCondition(); }
    if (!jobLock || !jobWake || !jobFinished) return 0;
    int running=0;
    for (int i=0;i<LOADER_THREADS;++i) {
        if (!loaders[i]) loaders[i]=SDL_CreateThread(loaderMain,"texture-pack-loader",NULL);
        running+=loaders[i]!=NULL;
    }
    if (!running) return 0;
    Job* j=calloc(1,sizeof(*j)); if (!j) return 0;
    j->slot=(int)(e-entries); j->serial=e->serial; j->key=e->texel; j->w=e->w; j->h=e->h; j->indexed=e->indexed; j->limit=e->limit;
    j->estimate=estimate; j->page=page; preloadPending+=estimate;
    snprintf(j->path,sizeof(j->path),"%s",path);
    SDL_LockMutex(jobLock);
    Job** tail=page?&pagesTodo:&celsTodo; while (*tail) tail=&(*tail)->next; *tail=j;   /* in order: a room's pages as asked */
    SDL_SignalCondition(jobWake); SDL_UnlockMutex(jobLock);
    e->pending=1; return 1;
}
static void jobInstall(Entry* e, Job* j, int gameThread) {
    e->pending=0;
    if (j->ok) {
        install(e,&j->dec); j->dec.rgba=j->dec.indices=NULL;
        if (j->page) { ++stats.pageLoads; if (gameThread) ++stats.pageSync; else ++stats.pageLoader; }
        if (gameThread) ++stats.gameThreadLoads;
    }
    else if (j->found) { invalid(j->path,j->key); if (j->indexed==BEETLE_KIND) { ++bstats.invalid; ++beetleGeneration; } }
    else ++stats.misses;
}
static void poll(void) {
    if (!SDL_GetAtomicInt(&jobsReady)) return;
    SDL_LockMutex(jobLock); Job* done=jobsDone; jobsDone=NULL; SDL_SetAtomicInt(&jobsReady,0); SDL_UnlockMutex(jobLock);
    while (done) {
        Job* j=done; done=j->next;
        Entry* e=&entries[j->slot];
        preloadPending-=j->estimate<=preloadPending?j->estimate:preloadPending;
        if (e->pending && e->serial==j->serial) jobInstall(e,j,0);
        free(j->dec.rgba); free(j->dec.indices); free(j);
    }
}
static void loaderStop(void) {
    if (!jobLock) return;
    SDL_LockMutex(jobLock); loaderQuit=1; SDL_BroadcastCondition(jobWake); SDL_UnlockMutex(jobLock);
    for (int i=0;i<LOADER_THREADS;++i) if (loaders[i]) { SDL_WaitThread(loaders[i],NULL); loaders[i]=NULL; }
    loaderQuit=0;
    for (Job* l[3]={pagesTodo,celsTodo,jobsDone}, **p=l; p<l+3; ++p) while (*p) { Job* j=*p; *p=j->next; free(j->dec.rgba); free(j->dec.indices); free(j); }
    pagesTodo=celsTodo=jobsDone=NULL; SDL_SetAtomicInt(&jobsReady,0); preloadPending=0;
}
/* A room's first frames (pages drawn while loading are waited for): ROOM_FIRST_VBLANKS from its first
 * texture pack draw (roomDrawnNow), while its waits stay within ROOM_WAIT_NS. */
static void roomDrawnNow(void) {
    if (roomSerial && !roomDrawn) { roomDrawn=1; roomDrawVblank=hwVSyncCount?hwVSyncCount():0; }
}
static int roomFirstFrames(void) {
    if (!hwVSyncCount || !roomSerial || !roomDrawn || roomWaitNs>=roomWaitLimitNs) return 0;   /* no frame clock: no first frames */
    return hwVSyncCount()-roomDrawVblank<ROOM_FIRST_VBLANKS;
}
static void roomWaited(uint64_t ns) {
    roomWaitNs+=ns; stats.pageWaitUs+=ns/1000; ++stats.pageWaits;
    if (roomWaitNs/1000>stats.pageWaitWorstUs) stats.pageWaitWorstUs=roomWaitNs/1000;
    if (roomWaitNs>=roomWaitLimitNs && !roomOver) { roomOver=1; ++stats.roomsOverBudget; }
    if (cpuTimingOn) loadTimed(1,ns);
}
/* A page drawn while its load runs, in a room's first frames: a job no loader has started is taken
 * from the queue and decoded here; one decoding is waited for (until the room's bound). */
static void pageWait(Entry* e) {
    Job* found=NULL;
    uint64_t t0=SDL_GetTicksNS(), deadline=t0+(roomWaitLimitNs-roomWaitNs);
    CPU_TIME_BEGIN(CPU_LOAD);
    SDL_LockMutex(jobLock);
    for (Job** at=&pagesTodo; *at; at=&(*at)->next)
        if ((*at)->slot==(int)(e-entries) && (*at)->serial==e->serial) { found=*at; *at=found->next; break; }
    if (found) {
        SDL_UnlockMutex(jobLock);
        found->ok=decodeImage(found->path,found->w,found->h,found->indexed,found->limit,&found->dec,&found->found);
        preloadPending-=found->estimate<=preloadPending?found->estimate:preloadPending;
        jobInstall(e,found,1);
        free(found->dec.rgba); free(found->dec.indices); free(found);
    } else {
        /* decoding on a loader (or done, not installed yet): until it is back, or the room's bound */
        for (;;) {
            int back=0;
            for (Job* j=jobsDone; j; j=j->next) back|=j->slot==(int)(e-entries) && j->serial==e->serial;
            uint64_t now=SDL_GetTicksNS();
            if (back || now>=deadline) break;
            SDL_WaitConditionTimeout(jobFinished,jobLock,(Sint32)((deadline-now)/1000000u+1));
        }
        SDL_UnlockMutex(jobLock);
        poll();
    }
    CPU_TIME_END(CPU_LOAD);
    uint64_t ns=SDL_GetTicksNS()-t0;
    roomWaited(ns);
    if (spriteLogOn()) fprintf(stderr,"texture pack: page %016" PRIx64 " drawn while loading at vblank %u: %s, %.2f ms (room %.2f ms)\n",e->texel,
                               hwVSyncCount?hwVSyncCount():0,found?"decoded here":e->pending?"waited, still loading":"waited for the loader",ns*1e-6,roomWaitNs*1e-6);
}
static void pageDrawnLoading(Entry* e) {
    if (roomFirstFrames()) { pageWait(e); if (!e->pending) return; }
    if (!e->nativeSeen) {
        e->nativeSeen=1; ++stats.pageNative;
        if (spriteLogOn()) fprintf(stderr,"texture pack: page %016" PRIx64 " drawn as the original while loading at vblank %u\n",e->texel,hwVSyncCount?hwVSyncCount():0);
    }
}

static Entry* entryFind(uint64_t texel, uint64_t palette, int indexed, int w, int h, int limit) {
    uint32_t hash=keyHash(texel,palette,indexed,w,h);
    for (int32_t i=bucket[hash];i;i=chain[i-1]) {
        Entry* e=&entries[i-1];
        if (e->touched && e->texel==texel && e->palette==palette && e->indexed==indexed &&
            e->w==w && e->h==h && (!indexed || e->limit==limit)) return e;
    }
    return NULL;
}
static Entry* entryNew(uint64_t texel, uint64_t palette, int indexed, int w, int h, int limit) {
    uint32_t hash=keyHash(texel,palette,indexed,w,h);
    Entry* e=oldest(NULL);
    if (e->touched) { ++stats.evictions; drop(e); }
    e->texel=texel; e->palette=palette; e->indexed=indexed;
    e->w=w; e->h=h; e->touched=++useClock; e->serial=++serials; ++stats.entries;
    chain[e-entries]=bucket[hash]; bucket[hash]=(int32_t)(e-entries)+1;
    e->limit=limit;
    return e;
}
/* The pack file of a key, 1 when it is there (*info its stat). */
static const char* beetleName(uint32_t upload, uint32_t palette);
static int packFile(char* path, size_t size, uint64_t texel, uint64_t palette, int indexed, SDL_PathInfo* info) {
    if (indexed==BEETLE_KIND) {
        const char* name=beetleName((uint32_t)texel,(uint32_t)palette);
        if (!name) return 0;
        snprintf(path,size,"%s/%s",beetleDir,name);
        return SDL_GetPathInfo(path,info) && info->type==SDL_PATHTYPE_FILE;
    }
    const char* base=indexed==4?spriteDir:indexed==3?glyphDir:dir; const char* sub=indexed==2||indexed==3?"glyphs/":"";
    if (indexed) snprintf(path,size,"%s/%s%016" PRIx64 ".indices.png",base,sub,texel);
    else snprintf(path,size,"%s/%016" PRIx64 "-%016" PRIx64 ".png",dir,texel,palette);
    int there=SDL_GetPathInfo(path,info) && info->type==SDL_PATHTYPE_FILE;
    if (indexed && !there) {   /* the short index filename is accepted too; dumped names take priority */
        snprintf(path,size,"%s/%s%016" PRIx64 ".png",base,sub,texel);
        there=SDL_GetPathInfo(path,info) && info->type==SDL_PATHTYPE_FILE;
    }
    return there;
}
static Entry* lookupKey(uint64_t texel, uint64_t palette, int indexed, int w, int h, int limit) {
    poll();
    Entry* e=entryFind(texel,palette,indexed,w,h,limit);
    if (e) {
        e->touched=++useClock;
        if (e->pending && (indexed<=1 || indexed==BEETLE_KIND) && lookupDraw) pageDrawnLoading(e);
        if (!(e->pending && indexed==4)) return e;
        /* a preloaded cel drawn before its decode finished: decoded here, now, as before the
         * preload (the picture never shows the native pose first); the loader's result is dropped */
        ++stats.celLate; e->pending=0; e->serial=++serials;
    } else e=entryNew(texel,palette,indexed,w,h,limit);
    char path[sizeof(dir)+80]; SDL_PathInfo info;
    if (!packFile(path,sizeof(path),texel,palette,indexed,&info)) { /* Negative cache; explicit reload retries. */
        ++stats.misses; if (indexed==BEETLE_KIND) ++bstats.misses;
        return e;
    }
    int page=(indexed<=1 || indexed==BEETLE_KIND) && info.size>ASYNC_FROM;
    /* a page drawn first in a room's first frames is decoded here (its draw would show the original) */
    int here=page && (pageSyncAlways || (lookupDraw && roomFirstFrames()));
    if (page && !here && enqueue(e,path,0,1)) { if (lookupDraw) pageDrawnLoading(e); return e; }
    Decoded dec; int found=0;
    uint64_t t0=cpuTimingOn||here?SDL_GetTicksNS():0;
    CPU_TIME_BEGIN(CPU_LOAD);
    int decoded=decodeImage(path,w,h,indexed,limit,&dec,&found);
    CPU_TIME_END(CPU_LOAD);
    uint64_t ns=t0?SDL_GetTicksNS()-t0:0;
    if (here && !pageSyncAlways) roomWaited(ns);   /* counted as a room wait (and its load time) */
    else if (cpuTimingOn) loadTimed(indexed==4?0:1,ns);
    if (!decoded) {
        if (found) invalid(path,texel); else ++stats.misses;
        if (indexed==BEETLE_KIND) { if (found) ++bstats.invalid; else ++bstats.misses; }
        return e;
    }
    install(e,&dec); ++stats.gameThreadLoads; if (indexed==4) ++stats.celSync;
    if (page) { ++stats.pageLoads; ++stats.pageSync; }
    return e;
}
static Entry* lookup(const DecodedTexture* d, int indexed) {
    return lookupKey(d->texelKey,indexed?0:d->paletteKey,indexed,d->request.w,d->request.h,d->paletteSize);
}

/* The sprite pack's room preload index (above): parts.tsv and npcs.tsv beside pack.ini. */
static int spritePartIndex(const char* name, int add) {
    for (int i=0;i<spritePartCount;++i) if (!strcmp(spritePartNames[i],name)) return i;
    if (!add || spritePartCount>=SPRITE_PARTS) return -1;
    if (!spritePartNames && !(spritePartNames=calloc(SPRITE_PARTS,sizeof(*spritePartNames)))) return -1;
    snprintf(spritePartNames[spritePartCount],SPRITE_PART_NAME,"%s",name);
    return spritePartCount++;
}
static void spriteIndexFree(void) {
    free(spriteCels); free(spriteNpcs); free(spritePartNames); free(spritePoses); free(spritePoseByKey);
    spriteCels=NULL; spriteNpcs=NULL; spritePartNames=NULL; spritePoses=NULL; spritePoseByKey=NULL;
    spriteCelCount=spriteNpcCount=spritePartCount=spritePoseCount=0;
    memset(partPoseFirst,0,sizeof(partPoseFirst)); memset(partPoseCount,0,sizeof(partPoseCount)); memset(partPoseRoom,0,sizeof(partPoseRoom));
}
static int poseKeyOrder(const void* x, const void* y) {
    uint64_t a=spritePoses[*(const int*)x].key, b=spritePoses[*(const int*)y].key;
    return a<b ? -1 : a>b;
}
/* poses.tsv (above): the rows in part order (as written), each part's run, and an index by key. */
static int spritePosesRead(void) {
    char path[sizeof(spriteRoot)+16], line[256], name[SPRITE_PART_NAME];
    snprintf(path,sizeof(path),"%s/poses.tsv",spriteRoot);
    FILE* f=fopen(path,"r"); if (!f) return 0;
    int cap=0, bad=0;
    SpritePoseRow* rows=NULL; int n=0;
    while (fgets(line,sizeof(line),f)) {
        unsigned long long key; int w,h,scale,member;
        if (line[0]=='#' || line[0]=='\n') continue;
        if (sscanf(line,"%47s %llx %d %d %d %d",name,&key,&w,&h,&scale,&member)!=6 || !key || w<1 || h<1 || w>256 || h>256 || scale<1 || scale>8) { ++bad; continue; }
        int part=spritePartIndex(name,1); if (part<0) { ++bad; continue; }
        if (n==cap) {
            int more=cap?cap*2:4096; SpritePoseRow* grown=realloc(rows,(size_t)more*sizeof(*grown));
            if (!grown) break;
            rows=grown; cap=more;
        }
        rows[n++]=(SpritePoseRow){(uint64_t)key,part,w,h,scale,member};
    }
    fclose(f);
    /* counting sort by part, keeping the file's order within a part */
    SpritePoseRow* sorted=n?malloc((size_t)n*sizeof(*sorted)):NULL;
    int* byKey=n?malloc((size_t)n*sizeof(*byKey)):NULL;
    if (n && (!sorted || !byKey)) { free(rows); free(sorted); free(byKey); return bad; }
    for (int i=0;i<n;++i) ++partPoseCount[rows[i].part];
    for (int p=0,at=0;p<SPRITE_PARTS;++p) { partPoseFirst[p]=at; at+=partPoseCount[p]; }
    {
        int fill[SPRITE_PARTS]; memcpy(fill,partPoseFirst,sizeof(fill));
        for (int i=0;i<n;++i) sorted[fill[rows[i].part]++]=rows[i];
    }
    free(rows);
    spritePoses=sorted; spritePoseCount=n;
    for (int i=0;i<n;++i) byKey[i]=i;
    if (n) qsort(byKey,(size_t)n,sizeof(*byKey),poseKeyOrder);
    spritePoseByKey=byKey;
    return bad;
}
static void spriteIndexRead(void) {
    spriteIndexFree();
    char path[sizeof(spriteRoot)+16], line[256], name[SPRITE_PART_NAME];
    int cap=0, bad=0;
    snprintf(path,sizeof(path),"%s/parts.tsv",spriteRoot);
    FILE* f=fopen(path,"r");   /* absent: no cels listed, the poses and slots still may be */
    while (f && fgets(line,sizeof(line),f)) {
        unsigned long long key; int w,h,depth;
        if (line[0]=='#' || line[0]=='\n') continue;
        if (sscanf(line,"%47s %llx %d %d %d",name,&key,&w,&h,&depth)!=5 || !key || w<1 || h<1 || w>256 || h>256 || (depth!=4 && depth!=8)) { ++bad; continue; }
        int part=spritePartIndex(name,1); if (part<0) { ++bad; continue; }
        if (spriteCelCount==cap) {
            int more=cap?cap*2:4096; SpriteCel* grown=realloc(spriteCels,(size_t)more*sizeof(*grown));
            if (!grown) break;
            spriteCels=grown; cap=more;
        }
        spriteCels[spriteCelCount++]=(SpriteCel){(uint64_t)key,part,w,h,depth==4?16:256};
    }
    if (f) fclose(f);
    bad+=spritePosesRead();
    snprintf(path,sizeof(path),"%s/npcs.tsv",spriteRoot);
    f=fopen(path,"r"); cap=0;
    if (f) {
        while (fgets(line,sizeof(line),f)) {
            int disc,field,npc;
            if (line[0]=='#' || line[0]=='\n') continue;
            if (sscanf(line,"%d %d %d %47s",&disc,&field,&npc,name)!=4 || field<0 || npc<0) { ++bad; continue; }
            int part=spritePartIndex(name,0); if (part<0) { ++bad; continue; }   /* a part with no cels */
            if (spriteNpcCount==cap) {
                int more=cap?cap*2:512; SpriteNpc* grown=realloc(spriteNpcs,(size_t)more*sizeof(*grown));
                if (!grown) break;
                spriteNpcs=grown; cap=more;
            }
            spriteNpcs[spriteNpcCount++]=(SpriteNpc){disc,field,npc,part};
        }
        fclose(f);
    }
    if (bad) fprintf(stderr,"sprite packs: %d lines of parts.tsv/npcs.tsv/poses.tsv ignored\n",bad);
    if (!spriteNpcCount && !spritePoseCount) spriteIndexFree();   /* nothing a field could ask for */
}
/* The cel's entry as its first draw would make it (lookup(d,4): the texel key, palette 0, the cell's
 * rectangle, the sheet's colours), queued for the loader thread. */
#define CEL_SLOTS_FREE 2048 /* slots the preloads leave for the room's draws (its other keys, its misses) */
static void preloadCel(const SpriteCel* c) {
    if (entryFind(c->texel,0,4,c->w,c->h,c->limit)) return;   /* loaded, decoding, or a known miss */
    if (stats.entries>=PACK_SLOTS-CEL_SLOTS_FREE) { ++stats.celSkipped; return; }   /* would make the room's own entries make way */
    size_t estimate=(size_t)c->w*c->h*SPRITE_CEL_SCALE*SPRITE_CEL_SCALE*5;
    if (stats.bytes+preloadPending+estimate>cacheBudget()) { ++stats.celSkipped; return; }
    char path[sizeof(dir)+80]; SDL_PathInfo info;
    if (!packFile(path,sizeof(path),c->texel,0,4,&info)) return;   /* not in the pack after all: the draw keeps its own miss */
    Entry* e=entryNew(c->texel,0,4,c->w,c->h,c->limit);
    if (enqueue(e,path,estimate,0)) ++stats.celPreloads;
}
int cdBootDiscNumber(void) __attribute__((weak)); /* port/src/cd/boot.c (1, 2); standalone tests have none */
void pcSpritePackParty(int a, int b, int c) {
    int ids[3]={a,b,c};
    for (int i=0;i<3;++i) partyIds[i]=ids[i]>=0 && ids[i]<0xFF ? ids[i] : -1;
}
int textureUsePack(int kind) __attribute__((weak)); /* texture_filter.c; standalone tests have none */
void pcSpritePackPreload(int field, int npc) {
    load();
    /* the field load: the room's texture pages first (3D textures from the pack: PC_TEX_3D, 0) */
    if (npc<0 && roomPreloadAtLoad && texturePackEnabled() && (!textureUsePack || textureUsePack(0))) roomsPreloadField(field);
    if (!spriteDir[0]) return;
    posePoll();
    if (npc<0) { ++poseRoom; poseRoomMark=poseClock+1; }   /* a new room: the earlier rooms' poses may make way */
    if (!spritePreloadOn || (!spriteNpcCount && !spritePoseCount)) return;
    int disc=cdBootDiscNumber?cdBootDiscNumber():0, parts[64], n=0, party=0;
    uint64_t queued=stats.celPreloads, posesQueued=stats.posePreloads, posesLeft=stats.poseSkipped;
    /* the party's field dolls (every slot of the field: the party walks in with it) */
    for (int m=0; npc<0 && m<3; ++m) for (int p=0; partyIds[m]>=0 && p<spritePartCount; ++p) {
        if (!partPoseCount[p] || spritePoses[partPoseFirst[p]].member!=partyIds[m]) continue;
        int seen=0;
        for (int k=0;k<n;++k) seen|=parts[k]==p;
        if (seen || n==64) continue;
        parts[n++]=p; ++party;
    }
    for (int i=0;i<spriteNpcCount;++i) {
        const SpriteNpc* r=&spriteNpcs[i];
        if (r->field!=field || (npc>=0 && r->npc!=npc) || (disc && r->disc && r->disc!=disc)) continue;
        int seen=0;
        for (int k=0;k<n;++k) seen|=parts[k]==r->part;
        if (seen || n==64) continue;
        parts[n++]=r->part;
        for (int c=0;c<spriteCelCount;++c) if (spriteCels[c].part==r->part) preloadCel(&spriteCels[c]);
    }
    /* the party's dolls' cels after the NPCs' (parts.tsv: no NPC slot; xg-hbc.27): in Enhanced their poses
     * draw them nearly always, in Original every frame */
    for (int k=0;k<party;++k) for (int c=0;c<spriteCelCount;++c) if (spriteCels[c].part==parts[k]) preloadCel(&spriteCels[c]);
    for (int k=0;k<n;++k) partPoseRoom[parts[k]]=poseRoom+1;
    posesQueueParts(parts,n);
    if (spriteLogOn()) fprintf(stderr,"sprite preload: field %d npc %d (disc %d): %d parts (%d party dolls: %d %d %d), %llu cels queued, %llu poses queued, %llu left out (pose cache %zu of %zu MB)\n",
                               field,npc,disc,n,party,partyIds[0],partyIds[1],partyIds[2],(unsigned long long)(stats.celPreloads-queued),
                               (unsigned long long)(stats.posePreloads-posesQueued),(unsigned long long)(stats.poseSkipped-posesLeft),
                               (poseBytes+posePendingBytes)>>20,poseLimit>>20);
}
int texturePackPreloadPending(void) {
    poll(); posePoll();
    int n=0;
    for (int i=0;i<PACK_SLOTS;++i) n+=entries[i].touched && entries[i].pending;   /* cels and pages */
    for (int i=0;i<POSE_SLOTS;++i) n+=poses[i].state==3;
    return n;
}
void texturePackPoseCache(size_t* bytes, size_t* peak, size_t* limit) { *bytes=poseBytes; *peak=posePeak; *limit=poseLimit; }
/* At exit, with XG_CPU_TIMING (cputime.c's summary): where the pack's loads ran. */
static void texturePackReport(void) {
    static int reported;
    if (!cpuTimingFromEnv() || reported) return;
    reported=1;
    fprintf(stderr,"texture pack: %llu loads, %llu on the game thread (%llu sprite cels), %llu sprite poses; room preload: %llu cels queued, "
            "%llu drawn before their decode finished (decoded there), %llu left out (cache budget)\n",
            (unsigned long long)stats.loads,(unsigned long long)stats.gameThreadLoads,(unsigned long long)stats.celSync,(unsigned long long)stats.poseLoads,
            (unsigned long long)stats.celPreloads,(unsigned long long)stats.celLate,(unsigned long long)stats.celSkipped);
    fprintf(stderr,"texture pack: sprite poses: %llu loaded, %llu on the pose loaders, %llu on the game thread (%llu drawn before their decode finished); "
            "%llu queued (%llu parts at their first drawn pose), %llu left out (cache budget), %llu evicted; cache %zu MB, peak %zu of %zu MB\n",
            (unsigned long long)stats.poseLoads,(unsigned long long)stats.poseLoader,(unsigned long long)stats.poseSync,(unsigned long long)stats.poseLate,
            (unsigned long long)stats.posePreloads,(unsigned long long)stats.posePartsSeen,(unsigned long long)stats.poseSkipped,(unsigned long long)stats.poseEvictions,
            poseBytes>>20,posePeak>>20,poseLimit>>20);
    fprintf(stderr,"texture pack: field pages: %llu loaded, %llu on the loader threads, %llu on the game thread; %llu queued by room preloads; "
            "%llu drawn while loading in a room's first frames and waited for (%.1f ms in all, worst room %.1f ms, %llu rooms at the %llu ms bound), "
            "%llu drawn as the original while loading; cache %d entries, %llu evicted\n",
            (unsigned long long)stats.pageLoads,(unsigned long long)stats.pageLoader,(unsigned long long)stats.pageSync,(unsigned long long)stats.pagePreloads,
            (unsigned long long)stats.pageWaits,stats.pageWaitUs*1e-3,stats.pageWaitWorstUs*1e-3,(unsigned long long)stats.roomsOverBudget,
            (unsigned long long)(roomWaitLimitNs/1000000u),(unsigned long long)stats.pageNative,stats.entries,(unsigned long long)stats.evictions);
    static const char* const kinds[3]={"sprite cels","other images","sprite poses"};
    for (int k=0;k<3;++k)
        fprintf(stderr,"texture pack: game thread %s: %.1f ms in all, worst %.2f ms, %u over 1 ms\n",kinds[k],loadKinds[k].ms,loadKinds[k].worst,loadKinds[k].slow);
}

/* A room's pages, from the pack's rooms.txt ("<field> <texel key> <w> <h> <colours>" a line,
 * tools/field_texture_redraw.py): asked for as the field changes, so they load behind its
 * fade-in rather than as each is first drawn. */
int gpuTextureField(void);
static void roomsRead(void) {
    roomCount=0; roomField=-2;
    char path[sizeof(dir)+16]; snprintf(path,sizeof(path),"%s/rooms.txt",dir);
    FILE* f=fopen(path,"r"); if (!f) return;
    char line[160];
    while (roomCount<ROOM_PAGES && fgets(line,sizeof(line),f)) {
        int field,w,h,limit; unsigned long long key;
        if (sscanf(line,"%d %llx %d %d %d",&field,&key,&w,&h,&limit)==5 && w>0 && h>0 && w<=256 && h<=256 && limit>0 && limit<=256)
            roomPages[roomCount++]=(__typeof__(roomPages[0])){field,w,h,limit,(uint64_t)key};
    }
    fclose(f);
}
/* include/pc_wide.h (the screen kind: 3 the world map); weak, standalone GPU tests have none */
int pcWideGetMode(void) __attribute__((weak));
/* A room: its pages (rooms.txt) queued for the loader threads, decoded by its first frame as a rule; a
 * page still loading when its first frames draw it is waited for (pageDrawnLoading). From the field load
 * (pcSpritePackPreload(field, -1)) or, failing that (the world map), the room's first page draw. */
static void roomsPreloadField(int field) {
    if (field==roomField) return;
    roomField=field; ++roomSerial; roomDrawn=roomOver=0; roomWaitNs=0;
    uint64_t queued=stats.pagePreloads;
    for (int i=0;i<roomCount;++i) if (roomPages[i].field==field) {
        Entry* e=lookupKey(roomPages[i].texel,0,1,roomPages[i].w,roomPages[i].h,roomPages[i].limit);
        if (e->pending && e->room!=roomSerial) { e->room=roomSerial; ++stats.pagePreloads; }
    }
    if (spriteLogOn()) fprintf(stderr,"texture pack: room %u: field %d at vblank %u: %llu pages queued\n",roomSerial,field,
                               hwVSyncCount?hwVSyncCount():0,(unsigned long long)(stats.pagePreloads-queued));
}
static void roomsPreload(void) {
    /* On the world map the field id is the script's world map command argument (0x400 plus an
     * entry spot, which varies by exit): its pages are keyed 1024 (the world map) in rooms.txt. */
    roomsPreloadField(pcWideGetMode && pcWideGetMode()==3 ? 0x400 : gpuTextureField());
}
/* ---- the Beetle PSX HW pack (beetle_pack) ---- */
static uint32_t beetleSlot(uint32_t upload, uint32_t palette) {
    uint64_t k=((uint64_t)upload<<32|palette)*0x9E3779B97F4A7C15ull;
    return (uint32_t)(k>>32)&(uint32_t)(beetleCap-1);
}
static void beetleAdd(uint32_t upload, uint32_t palette, const char* name) {
    if ((beetleCount+1)*2>beetleCap) {
        size_t cap=beetleCap?beetleCap*2:1024;
        __typeof__(beetleNames) t=calloc(cap,sizeof(*t)); if (!t) return;
        __typeof__(beetleNames) old=beetleNames; size_t oldCap=beetleCap;
        beetleNames=t; beetleCap=cap; beetleCount=0;
        for (size_t i=0;i<oldCap;++i) if (old[i].name) {
            uint32_t j=beetleSlot(old[i].upload,old[i].palette);
            while (beetleNames[j].name) j=(j+1)&(uint32_t)(beetleCap-1);
            beetleNames[j]=old[i]; ++beetleCount;
        }
        free(old);
    }
    uint32_t j=beetleSlot(upload,palette);
    while (beetleNames[j].name) {
        if (beetleNames[j].upload==upload && beetleNames[j].palette==palette) return; /* the first spelling met */
        j=(j+1)&(uint32_t)(beetleCap-1);
    }
    beetleNames[j].upload=upload; beetleNames[j].palette=palette; beetleNames[j].name=SDL_strdup(name);
    if (beetleNames[j].name) ++beetleCount;
}
static const char* beetleName(uint32_t upload, uint32_t palette) {
    if (!beetleCount) return NULL;
    for (uint32_t j=beetleSlot(upload,palette); beetleNames[j].name; j=(j+1)&(uint32_t)(beetleCap-1))
        if (beetleNames[j].upload==upload && beetleNames[j].palette==palette) return beetleNames[j].name;
    return NULL;
}
static void beetleNamesFree(void) {
    for (size_t i=0;i<beetleCap;++i) SDL_free(beetleNames[i].name);
    free(beetleNames); beetleNames=NULL; beetleCap=beetleCount=0;
}
/* A file name the core asks for: hex digits, no leading zero (0 is "0"), either case. */
static int beetleHex(const char* s, size_t n, uint32_t* out) {
    if (n<1 || n>8 || (n>1 && s[0]=='0')) return 0;
    uint32_t v=0;
    for (size_t i=0;i<n;++i) {
        int c=(unsigned char)s[i], d=c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:c>='A'&&c<='F'?c-'A'+10:-1;
        if (d<0) return 0;
        v=v<<4|(uint32_t)d;
    }
    *out=v; return 1;
}
static SDL_EnumerationResult SDLCALL beetleScan(void* user, const char* dirname, const char* name) {
    (void)user;
    size_t n=strlen(name);
    const char* dash=strchr(name,'-');
    uint32_t upload, palette;
    if (n>4 && !SDL_strcasecmp(name+n-4,".png") && dash &&
        beetleHex(name,(size_t)(dash-name),&upload) && beetleHex(dash+1,(size_t)(name+n-4-dash-1),&palette)) {
        char path[PATH_BYTES+300]; SDL_PathInfo info;
        snprintf(path,sizeof(path),"%s%s",dirname,name);
        if (SDL_GetPathInfo(path,&info) && info.type==SDL_PATHTYPE_FILE) beetleAdd(upload,palette,name);
    }
    return SDL_ENUM_CONTINUE;
}
static void beetleReload(void) {
    static int described;
    if (!described) {
        described=1;
        settingsDescribe("beetle_pack","Beetle PSX HW replacement folder (<game>-texture-replacements: <upload>-<palette>.png files) drawn where a category uses the texture pack; none or empty: off");
        settingsDescribe("beetle_pack_order","ours (default: our texture pack's images win where both have one) or beetle");
        settingsDescribe("beetle_pack_cache_mb","With a Beetle pack: the pack images' memory at least (64..4096, default 512)");
        settingsDescribe("beetle_pack_log","Debug: append each Beetle pack name the draws meet (as the core's dump names it) to this file");
    }
    beetleNamesFree();
    memset(&bstats,0,sizeof(bstats)); ++beetleGeneration;
    beetleBytes=0;
    const char* s=settingsGet("beetle_pack");
    beetleDir[0]=0;
    if (s && *s && SDL_strcasecmp(s,"none") && strlen(s)<sizeof(beetleDir)) snprintf(beetleDir,sizeof(beetleDir),"%s",s);
    const char* order=settingsGet("beetle_pack_order");
    beetleFirstSetting=order && !SDL_strcasecmp(order,"beetle");
    bstats.first=beetleFirstSetting;
    beetleNameLogOpen(settingsGet("beetle_pack_log"));
    if (!beetleDir[0]) {
        snprintf(bstats.message,sizeof(bstats.message),"Off");
        beetleTrackEnable(0); textureBeetleReset(); return;
    }
    size_t len=strlen(beetleDir);
    while (len>1 && (beetleDir[len-1]=='/' || beetleDir[len-1]=='\\')) beetleDir[--len]=0;
    SDL_PathInfo info;
    if (!SDL_GetPathInfo(beetleDir,&info) || info.type!=SDL_PATHTYPE_DIRECTORY) {
        snprintf(bstats.message,sizeof(bstats.message),"Folder unavailable; using original textures");
        fprintf(stderr,"beetle pack: %s: %s\n",beetleDir,bstats.message);
        beetleDir[0]=0; beetleTrackEnable(0); textureBeetleReset(); return;
    }
    SDL_EnumerateDirectory(beetleDir,beetleScan,NULL);
    bstats.files=(int)beetleCount;
    long mb=settingsInt("beetle_pack_cache_mb",BEETLE_CACHE_MB);
    beetleBytes=(size_t)(mb<64?64:mb>4096?4096:mb)*1024u*1024u;
#if SDL_VERSION_ATLEAST(3,4,0)
    bstats.enabled=beetleCount>0;
    snprintf(bstats.message,sizeof(bstats.message),beetleCount?"Ready; %d replacements, drawn as their uploads are seen":"No <upload>-<palette>.png files in the folder",bstats.files);
#else
    snprintf(bstats.message,sizeof(bstats.message),"PNG packs require SDL 3.4");
#endif
    beetleTrackEnable(bstats.enabled);
    textureBeetleReset();
    fprintf(stderr,"beetle pack: %s (%d replacements)\n",beetleDir,bstats.files);
}
int texturePackBeetleEnabled(void) { load(); return bstats.enabled; }
int texturePackActive(void) { load(); return stats.enabled || bstats.enabled; }
/* gpuInit: a chosen Beetle pack is read now, so its upload tracking sees the boot's first uploads. */
void texturePackBeetleInit(void) {
    const char* s=settingsGet("beetle_pack");
    if (s && *s && SDL_strcasecmp(s,"none")) load();
}
uint64_t texturePackBeetleGeneration(void) { poll(); return beetleGeneration; }
int texturePackGpuKeeps(void) { return gpuKeeps; }
const char* texturePackBeetleRoot(void) { load(); return beetleDir; }
void texturePackBeetleSetRoot(const char* s) {
    load(); if (!s) s="";
    if (strlen(s)>=sizeof(beetleDir)) { snprintf(bstats.message,sizeof(bstats.message),"Folder path too long; previous selection kept"); return; }
    settingsSet("beetle_pack",s[0]?s:"none"); settingsSave(); texturePackReload();
}
int texturePackBeetleFirst(void) { load(); return beetleFirstSetting; }
void texturePackBeetleSetFirst(int first) {
    load(); beetleFirstSetting=first!=0; bstats.first=beetleFirstSetting;
    settingsSet("beetle_pack_order",first?"beetle":"ours"); settingsSave();
}
void texturePackBeetleStats(BeetlePackStats* out) { load(); *out=bstats; }
int texturePackBeetleImage(uint32_t upload, uint32_t palette, int w, int h, TexturePackImage* out) {
    memset(out,0,sizeof(*out));
    if (!bstats.enabled || w<1 || h<1 || !beetleName(upload,palette)) return 0;
    Entry* e=lookupKey(upload,palette,BEETLE_KIND,w,h,0);
    if (e->pending) return -1;
    if (!entryLoaded(e)) return 0;
    out->rgba=e->rgba; out->w=e->w; out->h=e->h; out->scale=e->scale; out->revision=e->loaded;
    return 1;
}
static int beetleSample(const DecodedTexture* d, TextureRequest patch, TextureSample* out) {
    if (!bstats.enabled) return 0;
    beetleNameLogPatch(patch,hwVSyncCount?hwVSyncCount():0);
    return textureBeetleSample(d,patch,out);
}

int texturePackGlyph(uint64_t key, int w, int h, int limit, const uint8_t** indices, int* scale) {
    if (!texturePackEnabled() || w<1 || h<1 || w>256 || h>64 || limit<1 || limit>256) return 0;
    DecodedTexture d={0}; d.texelKey=key; d.request.w=w; d.request.h=h; d.paletteSize=limit;
    Entry* e=lookup(&d,2);
    if (!e->indices || replacementsHidden) return 0;
    *indices=e->indices; *scale=e->scale; return 1;
}
int textureHDGlyph(uint64_t key,int w,int h,int limit,const uint8_t** indices,int* scale) {
    load();
    if(!glyphDir[0] || w<1 || h<1 || w>256 || h>64 || limit<1 || limit>256)return 0;
    DecodedTexture d={0};d.texelKey=key;d.request.w=w;d.request.h=h;d.paletteSize=limit;
    Entry* e=lookup(&d,3);
    if(!e->indices || replacementsHidden)return 0;
    *indices=e->indices;*scale=e->scale;return 1;
}
static int sampleEntry(const DecodedTexture* d, Entry* e, TextureRequest patch, int filter, TextureSample* out);
static int packSample(const DecodedTexture* d, TextureRequest patch, int filter, TextureSample* out);
int texturePackSample(const DecodedTexture* d, TextureRequest patch, int filter, TextureSample* out) {
    if (d && texturePackEnabled()) roomsPreload();
    if (!d || !(texturePackEnabled() || bstats.enabled) || patch.w<1 || patch.h<1 || patch.u<d->request.u || patch.v<d->request.v ||
        patch.u+patch.w>d->request.u+d->request.w || patch.v+patch.h>d->request.v+d->request.h) return 0;
    roomDrawnNow();
    lookupDraw=1;   /* a page still loading: waited for in a room's first frames (pageDrawnLoading) */
    int got=packSample(d,patch,filter,out);
    lookupDraw=0;
    return got;
}
static int packSample(const DecodedTexture* d, TextureRequest patch, int filter, TextureSample* out) {
    /* A Beetle pack's image first (beetle_pack_order=beetle) or after ours (the default) */
    if (beetleFirstSetting && beetleSample(d,patch,out)) return !replacementsHidden;
    if (!stats.enabled) return !beetleFirstSetting && beetleSample(d,patch,out) && !replacementsHidden;
    Entry* e=lookup(d,0); /* Exact RGBA pair wins; indexed file ignores CLUT key. */
    if (!entryLoaded(e) && d->paletteSize) e=lookup(d,1);
    if (!entryLoaded(e) && d->request.stableTo && rectKeys) {
        /* Before stable regions this patch was a texture of its own: packs
         * made then name it by its own content. */
        TextureRequest plain=patch; plain.stableFrom=plain.stableTo=0;
        const DecodedTexture* p=textureCacheGet(plain);
        if (p) {
            Entry* f=lookup(p,0);
            if (!entryLoaded(f) && p->paletteSize) f=lookup(p,1);
            if (entryLoaded(f)) { d=p; e=f; }
        }
    }
    if (!entryLoaded(e)) return !beetleFirstSetting && beetleSample(d,patch,out) && !replacementsHidden;
    if (replacementsHidden) return 0;
    return sampleEntry(d,e,patch,filter,out);
}
/* Sprites -> Cel 8x: the field sprite cel library, index maps keyed by the
 * sampled sheet rectangle, independent of the user's texture pack root. */
int textureSpriteCelEnabled(void) { load(); return spriteDir[0]!=0; }

static int cellKnown(uint64_t key);
/* The extras' viewers (texture.h texturePackFind): the libraries' images by content key. */
int texturePackFind(int library, uint64_t texel, uint64_t palette, int w, int h, int limit, TexturePackImage* out) {
    memset(out,0,sizeof(*out));
    if (w<1 || h<1 || w>256 || h>256) return 0;
    Entry* e=NULL;
    if (library==TEXTURE_FIND_PACK) {
        if (!texturePackEnabled()) return 0;
        e=lookupKey(texel,palette,0,w,h,limit);
        if (!entryLoaded(e) && !e->pending && limit) e=lookupKey(texel,0,1,w,h,limit);
    } else if (library==TEXTURE_FIND_CEL) {
        if (!textureSpriteCelEnabled() || !limit || !cellKnown(texel)) return 0;
        e=lookupKey(texel,0,4,w,h,limit);
    } else return 0;
    if (e->pending) return -1;
    if (!entryLoaded(e)) return 0;
    out->rgba=e->indexed?NULL:e->rgba; out->indices=e->indexed?e->indices:NULL;
    out->w=e->w; out->h=e->h; out->scale=e->scale; out->revision=e->loaded;
    return 1;
}
int textureSpriteCelKnown(uint64_t key) { return textureSpriteCelEnabled() && cellKnown(key); }

/* The pose with this key (the pose cache, above): loaded, or decoded here, now, when it is not (yet). */
const SpritePose* texturePoseGet(uint64_t key) {
    load();
    if (!spriteDir[0] || !key) return NULL;
    posePoll();
    unsigned now=poseNow();
    PoseSlot* s=poseFind(key); int fresh=0;
    if (s) {
        s->touched=++poseClock;
        if (s->state==1) { s->used=now; return &s->pose; }
        if (s->state==2) return NULL;
        /* queued, not decoded yet: decoded here (the picture never shows the native sprite first); the
         * loader's result is dropped, or it skips the job when it has not started it */
        SDL_SetAtomicInt(&s->job->cancel,1); s->job=NULL; s->serial=++poseSerials; ++stats.poseLate;
    } else {
        if (!(s=poseNew(key))) return NULL;   /* every slot held by the last two ordering tables */
        fresh=1;
    }
    PoseRead r;
    uint64_t t0=cpuTimingOn?SDL_GetTicksNS():0;
    CPU_TIME_BEGIN(CPU_LOAD);
    poseRead(key,&r);
    CPU_TIME_END(CPU_LOAD);
    if (cpuTimingOn && r.status) loadTimed(2,SDL_GetTicksNS()-t0);
    poseInstall(s,&r); free(r.indices);
    if (s->state!=1) return NULL;
    ++stats.poseSync; s->used=now;
    if (spriteLogOn()) fprintf(stderr,"sprite pose: %016" PRIx64 " decoded on the game thread (%s)\n",key,fresh?"not queued":"queued, not decoded yet");
    if (fresh) poseFirstSight(key);
    return &s->pose;
}

/* The library is keyed by the cell a part was packed from (the primitive's
 * whole rectangle). A draw's decode rectangle is the union of the texels
 * its native walk sampled (gp0.c flatSpanTexels), which stops a texel short
 * of the cell on any side as the cell's fractional position varies, so a
 * part was found one vblank and not the next. On a miss the cells up to a
 * texel larger on each side are probed by their texel key, nearest first,
 * without touching the texture cache (a stat per candidate, remembered by
 * key), and the one the library holds is decoded. */
/* XG_TEXTURE_LOG, read once: every sprite part's draw asked the environment. */
static int spriteLogOn(void) {
    static int on=-1;
    if (on<0) on=getenv("XG_TEXTURE_LOG")!=NULL;
    return on;
}
static int cellKnown(uint64_t key) {
    unsigned slot=(unsigned)((key^(key>>29))*UINT64_C(0x9E3779B97F4A7C15)>>40)&(CELL_MEMO-1);
    if (!cellMemo[slot].valid || cellMemo[slot].key!=key) {
        char path[sizeof(spriteDir)+40];
        snprintf(path,sizeof(path),"%s/%016" PRIx64 ".indices.png",spriteDir,key);
        cellMemo[slot].key=key; cellMemo[slot].valid=1; cellMemo[slot].found=access(path,R_OK)==0;
    }
    return cellMemo[slot].found;
}
/* probeMemo (above): the answer per decode rectangle, by its texel key (the
 * same texels give the same neighbours): the cell's offsets, or none. */
static const DecodedTexture* spriteCell(const DecodedTexture* d, int a, int b, int c, int e) {
    TextureRequest r=d->request;
    r.u-=a; r.v-=b; r.w+=a+c; r.h+=b+e; r.stableFrom=r.stableTo=0;
    if (r.u<0 || r.v<0 || r.u+r.w>256 || r.v+r.h>256) return NULL;
    uint64_t key=textureTexelKey(r);
    if (!key || !cellKnown(key)) return NULL;
    const DecodedTexture* cell=textureCacheGet(r);
    return cell && cell->texelKey==key ? cell : NULL;
}
static const DecodedTexture* spriteCellAround(const DecodedTexture* d) {
    uint64_t k=d->texelKey;
    unsigned slot=(unsigned)((k^(k>>31))*UINT64_C(0xBF58476D1CE4E5B9)>>40)&(CELL_MEMO-1);
    if (probeMemo[slot].valid && probeMemo[slot].key==k) {
        if (!probeMemo[slot].found) return NULL;
        const DecodedTexture* cell=spriteCell(d,probeMemo[slot].a,probeMemo[slot].b,probeMemo[slot].c,probeMemo[slot].e);
        if (cell) return cell;
    }
    probeMemo[slot].key=k; probeMemo[slot].valid=1; probeMemo[slot].found=0;
    /* The native walk drops exactly the cell's last column and row (the
     * vertex UVs end a texel short of the cell), so a texel more to the
     * right and below is the cell nearly always, and comes first: a cell
     * one row shorter at the same origin is another pose's head whose
     * first rows are the same texels, and drawing it lost the chin. */
    static const uint8_t order[][4]={{0,0,1,1},{0,0,1,0},{0,0,0,1},{1,0,0,0},{0,1,0,0},{1,0,1,0},{0,1,0,1},{1,1,0,0},{1,0,0,1},{0,1,1,0},
                                     {1,1,1,0},{1,1,0,1},{1,0,1,1},{0,1,1,1},{1,1,1,1}};
    for (unsigned i=0; i<sizeof(order)/sizeof(order[0]); i++) {
        int a=order[i][0], b=order[i][1], c=order[i][2], e=order[i][3];
        const DecodedTexture* cell=spriteCell(d,a,b,c,e);
        if (!cell) continue;
        probeMemo[slot].found=1; probeMemo[slot].a=(uint8_t)a; probeMemo[slot].b=(uint8_t)b; probeMemo[slot].c=(uint8_t)c; probeMemo[slot].e=(uint8_t)e;
        if (spriteLogOn()) fprintf(stderr,"sprite cel: cell for %d,%d %dx%d found %d,%d,%d,%d larger (left,top,right,bottom)\n",d->request.u,d->request.v,d->request.w,d->request.h,a,b,c,e);
        return cell;
    }
    return NULL;
}
int textureSpriteCelSample(const DecodedTexture* d, TextureRequest patch, int filter, TextureSample* out) {
    if (!d || !d->paletteSize || !textureSpriteCelEnabled() || patch.w<1 || patch.h<1 || patch.u<d->request.u || patch.v<d->request.v ||
        patch.u+patch.w>d->request.u+d->request.w || patch.v+patch.h>d->request.v+d->request.h) return 0;
    Entry* e=lookup(d,4);
    if (!entryLoaded(e)) {
        const DecodedTexture* cell=spriteCellAround(d);
        if (cell) { d=cell; e=lookup(d,4); }
    }
    if (spriteLogOn()) fprintf(stderr,"sprite cel: %s tpage %04x clut %04x uv %d,%d %dx%d in %d,%d %dx%d key %016llx\n",entryLoaded(e)?"hit":"MISS",d->request.tpage,d->request.clut,patch.u,patch.v,patch.w,patch.h,d->request.u,d->request.v,d->request.w,d->request.h,(unsigned long long)d->texelKey);
    if (!entryLoaded(e) || replacementsHidden) return 0;
    return sampleEntry(d,e,patch,filter,out);
}
static int sampleEntry(const DecodedTexture* d, Entry* e, TextureRequest patch, int filter, TextureSample* out) {
    if (gpuKeeps && !(filter==TEXTURE_FILTER_EDGE && e->scale==1)) {
        /* the whole image, kept on the GPU once uploaded; an index map is
         * coloured there with the live CLUT (no crops, no recolouring) */
        ++stats.draws;
        if (e->indexed)
            *out=(TextureSample){NULL,e->loaded,d->request.u,d->request.v,e->w,e->h,e->scale,e->indices,1};
        else
            *out=(TextureSample){e->rgba,e->loaded,d->request.u,d->request.v,e->w,e->h,e->scale,NULL,1};
        return 1;
    }
    size_t full=(size_t)e->w*e->h*e->scale*e->scale;
    if (full>CROP_FROM && (size_t)patch.w*patch.h*2<=(size_t)e->w*e->h &&
        !(filter==TEXTURE_FILTER_EDGE && e->scale==1)) {
        uint64_t palette=e->indexed?d->paletteKey:0;
        Crop* c=NULL; Crop* victim=&crops[0];
        for (int i=0;i<CROP_SLOTS;++i) {
            Crop* k=&crops[i];
            if (k->rgba && k->loaded==e->loaded && k->palette==palette &&
                k->u==patch.u && k->v==patch.v && k->w==patch.w && k->h==patch.h) { c=k; break; }
            if (!k->rgba ? victim->rgba!=NULL : (victim->rgba && k->touched<victim->touched)) victim=k;
        }
        if (!c) {
            int s=e->scale, cw=patch.w*s, ch=patch.h*s;
            size_t bytes=(size_t)cw*ch*4;
            c=victim;
            if (c->rgba) { cropBytes-=c->bytes; free(c->rgba); c->rgba=NULL; }
            while (cropBytes+bytes>cropLimit) {
                Crop* old=NULL;
                for (int i=0;i<CROP_SLOTS;++i) if (crops[i].rgba && (!old || crops[i].touched<old->touched)) old=&crops[i];
                if (!old) break;
                cropBytes-=old->bytes; free(old->rgba); old->rgba=NULL;
            }
            c->rgba=malloc(bytes);
            if (!c->rgba) return 0;
            int x0=(patch.u-d->request.u)*s, y0=(patch.v-d->request.v)*s, stride=e->w*s;
            for (int y=0;y<ch;++y) {
                size_t at=(size_t)(y0+y)*stride+x0;
                uint8_t* dst=c->rgba+(size_t)y*cw*4;
                if (!e->indexed) { memcpy(dst,e->rgba+at*4,(size_t)cw*4); continue; }
                for (int x=0;x<cw;++x,dst+=4) {
                    uint16_t word=d->palette[e->indices[at+x]];
                    for (int k=0;k<3;++k) { int v=(word>>(k*5))&31; dst[k]=(uint8_t)((v<<3)|(v>>2)); }
                    dst[3]=word==0?0:word&0x8000?128:255;
                }
            }
            c->loaded=e->loaded; c->palette=palette; c->u=patch.u; c->v=patch.v; c->w=patch.w; c->h=patch.h;
            c->bytes=bytes; cropBytes+=bytes; c->revision=newRevision();
        }
        c->touched=++cropClock;
        ++stats.draws;
        *out=(TextureSample){c->rgba,c->revision,c->u,c->v,c->w,c->h,e->scale,NULL,0};
        return 1;
    }
    if (e->indexed && !entryRgba(e)) return 0;
    if (e->indexed && (e->livePalette!=d->paletteKey || e->revision==0 || !e->livePalette)) {
        size_t count=(size_t)e->w*e->h*e->scale*e->scale;
        for (size_t i=0;i<count;++i) {
            uint16_t word=d->palette[e->indices[i]];
            uint8_t* p=e->rgba+i*4;
            for (int k=0;k<3;++k) { int c=(word>>(k*5))&31; p[k]=(uint8_t)((c<<3)|(c>>2)); }
            p[3]=word==0?0:word&0x8000?128:255;
        }
        e->livePalette=d->paletteKey; e->revision=newRevision(); ++stats.recolours;
    }
    if (filter==TEXTURE_FILTER_EDGE && e->scale==1) {
        DecodedTexture replacement=*d; replacement.rgba=e->rgba; replacement.revision=e->revision;
        int ok=textureSampleGet(&replacement,patch,filter,out);
        if (ok) ++stats.draws;
        return ok;
    }
    ++stats.draws;
    *out=(TextureSample){e->rgba,e->revision,d->request.u,d->request.v,e->w,e->h,e->scale,NULL,0};
    return 1;
}
