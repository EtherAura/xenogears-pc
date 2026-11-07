/* Regional audio from the player's own images, independent of extras and of
 * the original patchers. No patched executables or retail assets are embedded. */
#include "regional.h"
#include "cd_internal.h"
#include "verify.h"
#include "setup.h"
#include "system/settings.h"
#include "../system/savestate.h"
#include "common.h"
#include "main/filesystem.h"
#include <SDL3/SDL.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_FILE (8*1024*1024)
#define CACHE_FILES 160

typedef struct {
    CdDisc* image;
    uint8_t table[32768];
    uint16_t dirs[61];
    char path[4096], status[1200];
    int attempted;
} Source;
static Source sources[2];
static int initialized, battle;
static struct { int index, disc, size; uint8_t* bytes; } cache[CACHE_FILES];
static int cacheCount;
typedef struct {
    int active, disc, clip, frame, baseLba, sourceClip;
    int sourceLba, sourceSectors, audio, japanese;
} RegionalMovie;
static RegionalMovie movie;

static unsigned le16(const uint8_t* p) { return p[0] | (unsigned)p[1]<<8; }
static uint32_t le32(const uint8_t* p) { return p[0] | (uint32_t)p[1]<<8 | (uint32_t)p[2]<<16 | (uint32_t)p[3]<<24; }
static void put32(uint8_t* p, unsigned v) { for (int i=0;i<4;i++) p[i]=(uint8_t)(v>>(8*i)); }
static int entryLba(const uint8_t* t, int i) { const uint8_t* p=t+7*i; return p[0] | p[1]<<8 | p[2]<<16; }
static int entrySize(const uint8_t* t, int i) { return (int32_t)le32(t+7*i+3); }
static int rawUser(CdDisc* image, int lba, uint8_t* out, int size) {
    uint8_t raw[2352];
    for (int done=0;done<size;done+=2048) {
        if (!(image ? discRead(image,lba++,raw) : cdReadRawSector(lba++,raw)) || raw[15]!=2 || (raw[18]&0x20)) return 0;
        memcpy(out+done,raw+24,(size_t)(size-done<2048 ? size-done : 2048));
    }
    return 1;
}
static int tables(CdDisc* image, uint8_t* table, uint16_t* dirs) {
    uint8_t raw[2048];
    if (!rawUser(image,24,table,32768) || !rawUser(image,40,raw,122)) return 0;
    for (int i=0;i<61;i++) dirs[i]=(uint16_t)le16(raw+2*i);
    return 1;
}
static uint8_t* fileBytes(CdDisc* image, const uint8_t* table, int index, int* size) {
    if (index<0 || index>=32768/7) return NULL;
    int n=entrySize(table,index), lba=entryLba(table,index);
    if (n<=0 || n>MAX_FILE || lba==0xffffff) return NULL;
    uint8_t* data=calloc(1,(size_t)((n+3)&~3));
    if (!data || !rawUser(image,lba,data,n)) { free(data); return NULL; }
    *size=n; return data;
}
static const char* sourceKey(int disc) { return disc==1 ? "japanese_disc1" : "japanese_disc2"; }
static void describe(void) {
    settingsDescribe("japanese_battle_voices","japanese_battle_voices: Japanese battle voices from your JP discs; applies after restart");
    settingsDescribe("japanese_cutscene_audio","japanese_cutscene_audio: Japanese XA audio with the current version's movie video");
    settingsDescribe("cutscene_subtitles","cutscene_subtitles: 0 off, 1 US dialogue, 2 Japanese translation; dialogue font");
    settingsDescribe("japanese_disc1","japanese_disc1: your Japanese Disc 1 .cue or raw .bin (read-only)");
    settingsDescribe("japanese_disc2","japanese_disc2: your Japanese Disc 2 .cue or raw .bin (read-only)");
}
void regionalInit(void) {
    if (initialized) return;
    initialized=1; describe(); battle=settingsBool("japanese_battle_voices",0);
    for (int i=0;i<2;i++) {
        const char* p=settingsGet(sourceKey(i+1));
        if (p && *p) snprintf(sources[i].path,sizeof sources[i].path,"%s",p);
#ifdef XG_ROOT
        else snprintf(sources[i].path,sizeof sources[i].path,
            XG_ROOT "/bin/Xenogears (Japan) (Disc %d)/Xenogears (Japan) (Disc %d).cue",i+1,i+1);
#endif
    }
}
static Source* source(int disc) {
    regionalInit();
    if (disc<1 || disc>2) return NULL;
    Source* s=&sources[disc-1];
    if (!s->attempted) {
        s->attempted=1;
        CdImageInfo info;
        if (!cdVerifyImage(s->path,&info,s->status,sizeof s->status)) return NULL;
        if (strcmp(info.region,"JP") || info.disc!=disc) {
            snprintf(s->status,sizeof s->status,"Choose Japanese Disc %d; selected %s Disc %d.",disc,info.region,info.disc);
            return NULL;
        }
        s->image=discOpen(s->path);
        if (!s->image || !tables(s->image,s->table,s->dirs)) {
            discClose(s->image); s->image=NULL;
            snprintf(s->status,sizeof s->status,"Cannot read Japanese Disc %d. Choose a complete image.",disc);
        } else { discWarm(s->image); snprintf(s->status,sizeof s->status,"Japanese Disc %d ready",disc); }
    }
    return s->image ? s : NULL;
}
int regionalBattleEnabled(void) { regionalInit(); return battle; }
int regionalMovieEnabled(void) { return settingsBool("japanese_cutscene_audio",0); }
void regionalSetBattle(int on) { describe(); settingsSetInt("japanese_battle_voices",!!on); settingsSave(); }
void regionalSetMovie(int on) { describe(); settingsSetInt("japanese_cutscene_audio",!!on); settingsSave(); }
const char* regionalSourcePath(int disc) { regionalInit(); return disc>=1 && disc<=2 ? sources[disc-1].path : ""; }
const char* regionalSourceStatus(int disc) {
    Source* s=source(disc);
    return s ? s->status : disc>=1 && disc<=2 ? sources[disc-1].status : "Invalid disc number";
}
int regionalSetSource(int disc, const char* path, char* why, size_t size) {
    CdImageInfo info;
    char absolute[4096];
    if (disc<1 || disc>2 || !cdSetupPath(path,absolute,sizeof absolute) || !cdVerifyImage(absolute,&info,why,size)) return 0;
    if (strcmp(info.region,"JP") || info.disc!=disc) {
        snprintf(why,size,"Choose Japanese Disc %d; selected %s Disc %d.",disc,info.region,info.disc); return 0;
    }
    /* Existing allocations/cache keep their source until the next launch. */
    settingsSet(sourceKey(disc),absolute); settingsSave();
    snprintf(why,size,"Japanese Disc %d saved. Restart the game to use it.",disc); return 1;
}

static int bundleOffsets(const uint8_t* p, int size, unsigned offsets[6]) {
    if (size<28 || le32(p)!=5) return 0;
    for (int i=0;i<6;i++) {
        offsets[i]=le32(p+4+i*4);
        if (offsets[i]>(unsigned)size || (i && offsets[i]<offsets[i-1])) return 0;
    }
    return offsets[0]>=28 && offsets[4]-offsets[3]>=4 && offsets[5]-offsets[4]>=4 &&
           !memcmp(p+offsets[3],"seds",4) && !memcmp(p+offsets[4],"wds ",4);
}
int pcRegionalFile(int index, const unsigned char* table, const unsigned short* dirs, void* copy) {
#if VERSION_JP
    return 0; /* already using native Japanese assets */
#endif
    if (cdDiscIsJapanese() || !regionalBattleEnabled() || !table || !dirs || index<0 || index>=32768/7) return 0;
    int disc=dirs[60], dir=0, file=0;
    if (disc<1 || disc>2) return 0;
    if (dirs[0x2d]!=0xffff && index>=(int)dirs[0x2d]-1 && index<(int)dirs[0x2d]+0x5c-1) {
        dir=0x2d; file=index-(dirs[dir]-1)+1;
        if (file<0x12) return 0;
    } else if (dirs[0x2c]!=0xffff && index==(int)dirs[0x2c]-1+6) { dir=0x2c; file=7; }
    else return 0;
    for (int i=0;i<cacheCount;i++) if (cache[i].index==index && cache[i].disc==disc) {
        if (copy) memcpy(copy,cache[i].bytes,(size_t)cache[i].size);
        return cache[i].size;
    }
    Source* s=source(disc);
    if (!s || cacheCount==CACHE_FILES) return 0;
    int ownSize=0, jpSize=0, jpIndex=(int)s->dirs[dir]-1+file-1;
    uint8_t* own=fileBytes(NULL,table,index,&ownSize);
    uint8_t* jp=fileBytes(s->image,s->table,jpIndex,&jpSize);
    uint8_t* merged=NULL; int length=0;
    if (!own || !jp) goto done;
    if (file<0x1f && dir==0x2d) {
        unsigned a[6], b[6];
        if (!bundleOffsets(own,ownSize,a) || !bundleOffsets(jp,jpSize,b)) goto done;
        length=(int)a[3]+(int)(b[5]-b[3]);
        merged=calloc(1,(size_t)((length>ownSize?length:ownSize)+3)&~(size_t)3);
        if (!merged) goto done;
        memcpy(merged,own,a[3]); memcpy(merged+a[3],jp+b[3],b[5]-b[3]);
        put32(merged+4+4*4,a[3]+b[4]-b[3]); put32(merged+4+5*4,(unsigned)length);
    } else {
        if (jpSize<4 || ownSize<4 || (memcmp(jp,"wds ",4) && memcmp(jp,"seds",4)) || memcmp(own,jp,4)) goto done;
        length=jpSize;
        merged=calloc(1,(size_t)((length>ownSize?length:ownSize)+3)&~(size_t)3);
        if (!merged) goto done;
        memcpy(merged,jp,(size_t)length);
    }
    length=((length>ownSize?length:ownSize)+3)&~3;
    cache[cacheCount].index=index; cache[cacheCount].disc=disc;
    cache[cacheCount].bytes=merged; cache[cacheCount++].size=length;
    if (copy) memcpy(copy,merged,(size_t)length);
    fprintf(stderr,"regional: Japanese battle audio %02X/%02X, disc %d (%d bytes)\n",dir,file,disc,length);
 done:
    free(own); free(jp); return merged ? length : 0;
}
int pcRegionalBatch(void* commands, int first, const unsigned char* table, const unsigned short* dirs) {
    LoadingBatchCommand* cmds=commands;
    int count=0, changed=0;
    while (count<1024 && cmds[count].m0_fileIndex) {
        changed |= pcRegionalFile(first+cmds[count].m0_fileIndex-1,table,dirs,NULL)>0;
        count++;
    }
    if (!changed || count==1024) return 0;
    uint8_t** data=calloc((size_t)count,sizeof(*data));
    int* sizes=calloc((size_t)count,sizeof(*sizes));
    int ok=data && sizes;
    for (int i=0;ok && i<count;i++) {
        if (!cmds[i].m4_loadPtr) continue;
        int index=first+cmds[i].m0_fileIndex-1;
        sizes[i]=pcRegionalFile(index,table,dirs,NULL);
        if (sizes[i]) {
            data[i]=malloc((size_t)sizes[i]);
            ok=data[i] && pcRegionalFile(index,table,dirs,data[i])==sizes[i];
        } else data[i]=fileBytes(NULL,table,index,&sizes[i]);
        ok=ok && data[i]!=NULL;
    }
    if (ok) for (int i=0;i<count;i++) if (data[i]) memcpy(cmds[i].m4_loadPtr,data[i],(size_t)((sizes[i]+3)&~3));
    if (data) { for (int i=0;i<count;i++) free(data[i]); free(data); }
    free(sizes); return ok ? 1 : -1;
}

/* Video-frame correspondence measured on the player's original US/JP
 * discs (docs/REGIONAL_OPTIONS.md). US Disc 2 splits JP movie 6 into clips.
 * Movie 0 is the full ending and credits in both releases. US movie 6 is
 * a music-only compilation of story shots; JP movie 6 is a much longer
 * reel carrying the ending score. These are different edits, not shorter
 * and longer credits. Preserve the US compilation soundtrack instead of
 * cutting the JP ending song to fit it (xg-htn.35.24.2). The actual credits
 * use movie 0's full JP XA from the beginning, with no music reshaping. */
static int movieSourceFrame(int disc, int clip, int frame) {
    if (disc!=2) return frame;
    if (clip==8) return frame+3600;
    if (clip==9) return frame+2000;
    if (clip==10) return frame+2300;
    if (clip==11) return frame+2900;
    return frame;
}
void regionalMovieBegin(int lba) {
    regionalMovieEnd();
    uint8_t table[32768]; uint16_t dirs[61];
    if (!tables(NULL,table,dirs)) return;
    int first=(int)dirs[0x19]-1;
    if (first<0 || first>=32768/7) return;
    int count=-entrySize(table,first);
    if (count<1 || count>32) return;
    for (int clip=0;clip<count;clip++) {
        int index=first+1+clip, begin=entryLba(table,index), sectors=entrySize(table,index)/2336;
        if (lba<begin || lba>=begin+sectors) continue;
        movie.active=1; movie.japanese=cdDiscIsJapanese(); movie.disc=dirs[60]; movie.clip=clip; movie.baseLba=begin;
        movie.sourceClip=movie.disc==2 && clip>=8 ? 6 : clip;
#if !VERSION_JP
        if (!cdDiscIsJapanese() && regionalMovieEnabled() && !(movie.disc==2 && clip==6)) {
            Source* s=source(movie.disc);
            if (s) {
                int group=(int)s->dirs[0x19]-1;
                if (group>=0 && movie.sourceClip < -entrySize(s->table,group)) {
                    int src=group+1+movie.sourceClip;
                    movie.sourceLba=entryLba(s->table,src);
                    movie.sourceSectors=entrySize(s->table,src)/2336;
                    movie.audio=1;
                }
            }
        }
#endif
        fprintf(stderr,"regional: disc %d cutscene %d (%s audio)\n",movie.disc,clip,movie.audio?"Japanese":"original");
        break;
    }
}
/* The JP XA sector of the same 8-sector group at `sourceOffset` in the source
 * clip, if it is audio coded as `raw` is; NULL past the source's end. */
static const uint8_t* sourceSector(int sourceOffset, const uint8_t* raw, uint8_t jp[2352]) {
    Source* s=source(movie.disc);
    sourceOffset=sourceOffset/8*8+7;
    if (s && sourceOffset>=0 && sourceOffset<movie.sourceSectors && discRead(s->image,movie.sourceLba+sourceOffset,jp) &&
        jp[15]==2 && (jp[18]&0x44)==0x44 && jp[19]==raw[19]) return jp;
    return NULL;
}
void regionalMovieSector(int lba, unsigned char raw[2352]) {
    if (!movie.active || !movie.audio || raw[15]!=2 || (raw[18]&0x44)!=0x44) return;
    int offset=lba-movie.baseLba;
    if (offset<0) return;
    int frame=offset/10+1, sourceFrame=movieSourceFrame(movie.disc,movie.clip,frame);
    uint8_t jp[2352];
    if (sourceSector(offset+(sourceFrame-frame)*10,raw,jp)) {
        /* Keep the base image's identity/channel/filter and video untouched. */
        memcpy(raw+24,jp+24,2304);
    } else {
        /* The shorter regional stream's trailing padding is silence. Never
         * introduce an English voice halfway through a Japanese cutscene. */
        memset(raw+24,0,2304);
    }
}
/* The scenes of US Disc 2's movies 8-11 as the JP game plays them
 * (xg-htn.35.24.3, work/agents/BD/AUDIO/NOTES.md). The US scripts play the
 * US streams 8-11 with OP_SET_2D_BACKGROUND, XA on (English voices and
 * effects, no music); JP's scripts play the same pictures, parts of its movie
 * 6, with OPX_A0, which turns the XA audio off, and a sound sequence of
 * effects (1C/115+n, the same banks and event lists on both discs). Measured
 * in the scene of movie 10 (field 20, entrance 2, story 305): JP's audio there
 * (Beetle PSX, the JP disc) is not its movie 6 XA (correlation 0.06 at most)
 * but sequence 2's effects. So with Japanese cutscene audio these four
 * movies play without XA, with the JP scene's sequence: (field, stream) ->
 * sequence, 0xFF for none (field 300: JP plays that part silent). Movie 8 in
 * field 24 is disc 1's stream 8 (story 31) and stays as it is. */
static int fieldMovieRoute(int field, int stream, int music) {
#if VERSION_JP
    (void)field; (void)stream; (void)music;
    return -1; /* the JP scripts already play them so */
#else
    static const struct { short field, stream, sequence, music; } scenes[] = {
        {622, 8, 0, 31}, {300, 9, 0xFF, -1}, {20, 10, 2, -1},
        {664, 11, 3, 34}, {665, 11, 3, 34},
    };
    uint8_t table[32768]; uint16_t dirs[61];
    if (cdDiscIsJapanese() || !regionalMovieEnabled()) return -1;
    for (size_t i=0;i<sizeof scenes/sizeof scenes[0];i++) {
        if (scenes[i].field!=field || scenes[i].stream!=stream) continue;
        /* US Disc 2 in the drive, and Japanese audio in effect (its disc set). */
        if (!tables(NULL,table,dirs) || dirs[60]!=2 || !source(2)) return -1;
        return music ? scenes[i].music : scenes[i].sequence;
    }
    return -1;
#endif
}
int pcRegionalFieldMovieSound(int field, int stream) {
    int sequence=fieldMovieRoute(field,stream,0);
    if (sequence>=0)
        fprintf(stderr,"regional: field %d movie %d as the JP scene plays it: no XA, sound sequence %d\n",
                field,stream,sequence);
    return sequence;
}
/* The JP scripts select these silent scores before their movie op. They
 * load the movie's WDS samples and reverb; copying only the SEDS events
 * plays them with the preceding field's samples (audible as beeps).
 * Field 20 already selects score 33 in both regions; field 300 keeps its
 * background music and has no movie effects. */
int pcRegionalFieldMovieMusic(int field, int stream) {
    return fieldMovieRoute(field,stream,1);
}
void regionalMovieEnd(void) { memset(&movie,0,sizeof movie); }
void regionalMovieFrame(int frame) { if (movie.active) movie.frame=frame; }
int regionalSubtitlesEnabled(void) { int n=settingsInt("cutscene_subtitles",0); return n>=0 && n<=2 ? n : 0; }
void regionalSetSubtitles(int mode) { describe(); settingsSetInt("cutscene_subtitles",mode>=0 && mode<=2 ? mode : 0); settingsSave(); }

typedef struct { unsigned char disc, clip; unsigned short start, end; const char* text; } Caption;
#if __has_include("subtitles.inc")
/* The captions are a transcription of the game's movie dialogue, so they
 * stay out of the public repository; a build without them has none. */
#include "subtitles.inc"
static const Caption* captionTrack(int japanese, size_t* count) {
    *count=japanese ? sizeof captionsJapanese/sizeof captionsJapanese[0] : sizeof captionsUS/sizeof captionsUS[0];
    return japanese ? captionsJapanese : captionsUS;
}
#else
static const Caption* captionTrack(int japanese, size_t* count) { (void)japanese; *count=0; return NULL; }
#endif
const char* regionalSubtitle(void) {
    int mode=regionalSubtitlesEnabled();
    if (!movie.active || !mode) return NULL;
    size_t count;
    const Caption* track=captionTrack(mode==2,&count);
    int clip=movie.clip, frame=movie.frame;
    /* JP movie 6 is the long story-footage reel: its opening exchange
     * uses clip 0's captions, later story speech is the scene split into
     * US clip 8. The actual credits (movie 0) have a different video
     * timeline despite sharing XA; never put later story captions there. */
    if (movie.japanese && movie.disc==2 && clip==6) {
        if (frame>=3601) { clip=8; frame-=3600; }
        else clip=0;
    }
    for (size_t i=0;i<count;i++)
        if (track[i].disc==movie.disc && track[i].clip==clip &&
            frame>=track[i].start && frame<track[i].end) return track[i].text;
    return NULL;
}

size_t regionalPartSize(void) { return sizeof movie; }
void regionalPartInit(void* blob) { memset(blob,0,sizeof movie); }
void regionalPartSwap(void* blob) { CD_SWAP(movie,*(RegionalMovie*)blob); }

/* ---- save states (system/savestate.h) ---- */

/* The voice bundles' files are this process's. */
void regionalStateIo(XgState* s) {
    xgStateIo(s, "movie", &movie, sizeof movie);
}
