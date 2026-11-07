/* Run against the player's original US/JP images. Independently checks
 * rebuilt audio bundles, bounds, movie video isolation and missing sources. */
#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cd/cd_internal.h"
#include "cd/regional.h"
#include "system/settings.h"
#include "common.h"
#include "main/filesystem.h"
#include "test/env.h"
static CdDisc* current;
static int checks;
#include "test/check.h"
int cdDiscIsJapanese(void) { return discIsJapanese(current); }
int cdReadRawSector(int lba, uint8_t out[2352]) { return discRead(current,lba,out); }
int cdSetupPath(const char* p, char* out, size_t n) { return snprintf(out,n,"%s",p)>0; }
void cdSwapBytes(void* a, void* b, size_t n) { uint8_t* x=a; uint8_t* y=b; while(n--) { uint8_t t=*x; *x++=*y; *y++=t; } }
void spuCdSubmit(const int16_t* lr, int frames, int rate) { (void)lr; (void)frames; (void)rate; } /* xa.c's output */
typedef struct { unsigned char disc, clip; unsigned short start, end; const char* text; } Caption;
#if __has_include("../subtitles.inc")
#define XG_CAPTIONS 1 /* the local transcription (regional.c) */
#include "../subtitles.inc"
#endif
/* Cue boundaries are half-open. A rounded overlap silently hides the next
 * line because the runtime selects the first active cue. */
#ifdef XG_CAPTIONS
static int captionTrackCheck(const Caption* track, size_t count) {
 for (size_t i=0;i<count;i++) {
  const Caption* c=&track[i];
  CHECK(c->disc>=1 && c->disc<=2 && c->start>=1 && c->end>c->start);
  CHECK(c->text && c->text[0]);
  for (const unsigned char* p=(const unsigned char*)c->text;*p;p++)
   CHECK(*p>=32 && *p<127); /* native dialogue font's caption repertoire */
  if (i) {
   const Caption* p=&track[i-1];
   CHECK(p->disc<c->disc || (p->disc==c->disc && p->clip<=c->clip));
   if (p->disc==c->disc && p->clip==c->clip) CHECK(p->end<=c->start);
  }
 }
 return 0;
}
#endif
/* The music-only US story-footage reel keeps every authored byte. The
 * actual credits are movie 0: its complete JP XA is consecutive from the
 * beginning; only the US file's three trailing padding sectors are silent. */
static int fullEndingCheck(CdDisc* jp, int jpBegin, int begin, int sectors, int compilation) {
 uint8_t raw[2352], original[2352], source[2352];int audio=0;
 regionalMovieEnd();regionalMovieBegin(begin);
 for (int o=0;o<sectors;o++) {
  CHECK(discRead(current,begin+o,raw));memcpy(original,raw,sizeof raw);
  regionalMovieSector(begin+o,raw);
  if (compilation || (original[18]&0x44)!=0x44) CHECK(!memcmp(raw,original,sizeof raw));
  else {
   CHECK(!memcmp(raw,original,24) && !memcmp(raw+2328,original+2328,24));
   if (o<67368) {
    CHECK(discRead(jp,jpBegin+o,source));CHECK(!memcmp(raw+24,source+24,2304));
   } else {
    static const uint8_t silence[2304]={0};CHECK(!memcmp(raw+24,silence,sizeof silence));
   }
  }
  if ((original[18]&0x44)==0x44) audio++;
 }
 CHECK(audio==(compilation?3318:8424));return 0;
}
static unsigned read16(const uint8_t* p) { return p[0]|p[1]<<8; }
static unsigned read32(const uint8_t* p) { return p[0]|(unsigned)p[1]<<8|(unsigned)p[2]<<16|(unsigned)p[3]<<24; }
static int sizeAt(const uint8_t* t, int i) { return (int32_t)read32(t+7*i+3); }
static int lbaAt(const uint8_t* t, int i) { return t[7*i]|t[7*i+1]<<8|t[7*i+2]<<16; }
static int tables(CdDisc* d, uint8_t* table, uint16_t* dirs) {
 uint8_t raw[2352];
 for (int i=0;i<16;i++) { if(!discRead(d,24+i,raw)) return 0; memcpy(table+2048*i,raw+24,2048); }
 if(!discRead(d,40,raw)) return 0;
 for(int i=0;i<61;i++) dirs[i]=read16(raw+24+2*i);
 return 1;
}
static uint8_t* file(CdDisc* d, const uint8_t* t, int i) {
 int n=sizeAt(t,i), lba=lbaAt(t,i); if(n<=0 || n>8*1024*1024) return NULL;
 uint8_t* p=calloc(1,(n+3)&~3); uint8_t raw[2352];
 for(int off=0;off<n;off+=2048) { if(!discRead(d,lba++,raw)) { free(p);return NULL; } memcpy(p+off,raw+24,n-off<2048?n-off:2048); }
 return p;
}
/* Ending scenes use the base release's sequenced effects instead of JP XA.
 * Check that assumption against the player's images, independently of the
 * routing lookup: sequences 0, 2 and 3 must supply the same complete bank. */
static int endingBanksCheck(CdDisc* jp, const uint8_t* t, const uint16_t* dirs,
                           const uint8_t* j, const uint16_t* jd) {
 const int sequences[]={0,2,3};
 for (unsigned k=0;k<sizeof sequences/sizeof sequences[0];k++) {
  int f=0x115+sequences[k], a=dirs[0x1c]-1+f-1, b=jd[0x1c]-1+f-1;
  int n=sizeAt(t,a);
  CHECK(n>0 && n==sizeAt(j,b));
  uint8_t* us=file(current,t,a); uint8_t* other=file(jp,j,b);
  CHECK(us && other);
  CHECK(!memcmp(us,other,n));
  free(us);free(other);
 }
 /* Silent scores 31/33/34 load the movie WDS samples. The SEDS banks
  * alone are insufficient: their sample numbers otherwise select sounds
  * from the previous field, producing the reported high-pitched beeps. */
 const int cues[]={31,33,34};
 for (unsigned k=0;k<sizeof cues/sizeof cues[0];k++) {
  for (int score=0;score<2;score++) {
   int f=0x13+2*cues[k]+score, a=dirs[0x1c]-1+f-1, b=jd[0x1c]-1+f-1;
   int n=sizeAt(t,a);
   CHECK(n>0 && n==sizeAt(j,b));
   uint8_t* us=file(current,t,a); uint8_t* other=file(jp,j,b);
   CHECK(us && other && !memcmp(us,other,n));
   if (score) {
    CHECK(n>=0x24 && !memcmp(us,"smds",4));
    for (int track=0;track<us[0x14];track++) {
     CHECK(0x24+2*track<=n);
     unsigned at=read16(us+0x22+2*track);
     CHECK(at<(unsigned)n && us[at]==0x90); /* no music notes */
    }
   }
   free(us);free(other);
  }
 }
 return 0;
}
int main(int argc, char** argv) {
 if(argc!=5 && argc!=6) return 2; /* US1 US2 JP1 JP2 [audio bits] */
 int mode=argc==6?atoi(argv[5]):3, missing=mode==4, battleOn=missing?1:mode&1, movieOn=missing?1:(mode>>1)&1;
 testSetEnv("XG_SETTINGS","none"); testSetEnv("XG_JAPANESE_BATTLE_VOICES",battleOn?"1":"0");
 testSetEnv("XG_JAPANESE_CUTSCENE_AUDIO",movieOn?"1":"0"); testSetEnv("XG_CUTSCENE_SUBTITLES","1");
 testSetEnv("XG_JAPANESE_DISC1",missing?"/missing/japanese-disc1.cue":argv[3]); testSetEnv("XG_JAPANESE_DISC2",missing?"/missing/japanese-disc2.cue":argv[4]);
 regionalInit();
#ifdef XG_CAPTIONS
 CHECK(captionTrackCheck(captionsUS,sizeof captionsUS/sizeof captionsUS[0])==0);
 CHECK(captionTrackCheck(captionsJapanese,sizeof captionsJapanese/sizeof captionsJapanese[0])==0);
#endif
 CHECK(regionalBattleEnabled()==battleOn && regionalMovieEnabled()==movieOn);
 regionalSetMovie(!movieOn); CHECK(regionalMovieEnabled()!=movieOn && regionalBattleEnabled()==battleOn); regionalSetMovie(movieOn);
 regionalSetBattle(!battleOn); CHECK(regionalBattleEnabled()==battleOn); /* allocation snapshot */
 for(int disc=1;disc<=2;disc++) {
  current=discOpen(argv[disc]); CdDisc* jp=discOpen(argv[disc+2]); CHECK(current && jp);
  uint8_t t[32768],j[32768]; uint16_t dirs[61],jd[61];
  CHECK(tables(current,t,dirs) && tables(jp,j,jd));
  CHECK(endingBanksCheck(jp,t,dirs,j,jd)==0);
  if (missing) {
   CHECK(pcRegionalFile(dirs[0x2d]-1+0x11,t,dirs,NULL)==0);
   CHECK(strstr(regionalSourceStatus(disc),"ready")==NULL);
   CHECK(pcRegionalFieldMovieSound(20,10)==-1); /* no Japanese disc: as it is */
   CHECK(pcRegionalFieldMovieMusic(622,8)==-1 && pcRegionalFieldMovieMusic(665,11)==-1);
   int begin=lbaAt(t,dirs[0x19]+2); uint8_t raw[2352], old[2352];
   regionalMovieBegin(begin);CHECK(discRead(current,begin+7,raw));memcpy(old,raw,2352);
   regionalMovieSector(begin+7,raw);CHECK(!memcmp(raw,old,2352));regionalMovieEnd();
   discClose(current);discClose(jp);continue;
  }
  for(int f=0x12;f<=0x5c;f++) {
   int i=dirs[0x2d]-1+f-1, ji=jd[0x2d]-1+f-1, n=pcRegionalFile(i,t,dirs,NULL);
   if (!battleOn) { CHECK(n==0);continue; }
   CHECK(n>=sizeAt(t,i));
   uint8_t* p=malloc(n+32); memset(p,0xcd,n+32);
   CHECK(pcRegionalFile(i,t,dirs,p)==n);
   for(int k=0;k<32;k++) CHECK(p[n+k]==0xcd);
   uint8_t* a=file(current,t,i);uint8_t* b=file(jp,j,ji); CHECK(a && b);
   if(f<0x1f) {
    unsigned prefix=read32(a+16), jb=read32(b+16), end=read32(b+24);
    CHECK(!memcmp(p+28,a+28,prefix-28));
    CHECK(!memcmp(p+prefix,b+jb,end-jb));
    CHECK(read32(p+20)==prefix+read32(b+20)-jb && read32(p+24)==prefix+end-jb);
   } else CHECK(!memcmp(p,b,sizeAt(j,ji)));
   free(a);free(b);free(p);
  }
  int i=dirs[0x2c]-1+6, n=pcRegionalFile(i,t,dirs,NULL); CHECK(battleOn ? n>=sizeAt(t,i) : n==0);
  CHECK(!pcRegionalFile(dirs[4]-1,t,dirs,NULL)); /* field untouched */
  for(int clip=0;clip<-sizeAt(t,dirs[0x19]-1);clip++) {
   int i=dirs[0x19]+clip, begin=lbaAt(t,i), sectors=sizeAt(t,i)/2336;
   regionalMovieBegin(begin);
   uint8_t raw[2352], old[2352];
   CHECK(discRead(current,begin,raw));memcpy(old,raw,2352);
   regionalMovieSector(begin,raw);CHECK(!memcmp(raw,old,2352)); /* video exact */
   CHECK(discRead(current,begin+7,raw));memcpy(old,raw,2352);
   regionalMovieSector(begin+7,raw);CHECK(!memcmp(raw,old,24) && !memcmp(raw+2328,old+2328,24));
   if (!movieOn) CHECK(!memcmp(raw,old,2352));
   /* Ordinary clips: the matching XA sector from the same JP movie. */
   if(movieOn && !(disc==2 && clip==6) && clip<(disc==1?17:6)) {
    int ji=jd[0x19]+clip;CHECK(discRead(jp,lbaAt(j,ji)+7,old));CHECK(!memcmp(raw+24,old+24,2304));
   }
   if (clip==0) { /* field movies played as the JP scenes (startFieldTransition) */
    int on=movieOn && disc==2;
    CHECK(pcRegionalFieldMovieSound(20,10)==(on?2:-1) && pcRegionalFieldMovieSound(622,8)==(on?0:-1));
    CHECK(pcRegionalFieldMovieSound(300,9)==(on?0xFF:-1) && pcRegionalFieldMovieSound(664,11)==(on?3:-1));
    CHECK(pcRegionalFieldMovieSound(665,11)==(on?3:-1));
    CHECK(pcRegionalFieldMovieSound(24,8)==-1 && pcRegionalFieldMovieSound(20,6)==-1 && pcRegionalFieldMovieSound(618,6)==-1);
    CHECK(pcRegionalFieldMovieMusic(622,8)==(on?31:-1));
    CHECK(pcRegionalFieldMovieMusic(664,11)==(on?34:-1) && pcRegionalFieldMovieMusic(665,11)==(on?34:-1));
    CHECK(pcRegionalFieldMovieMusic(300,9)==-1 && pcRegionalFieldMovieMusic(20,10)==-1);
    CHECK(pcRegionalFieldMovieMusic(24,8)==-1 && pcRegionalFieldMovieMusic(618,6)==-1);
   }
   if (movieOn && disc==2 && (clip==0 || clip==6)) {
    regionalMovieEnd(); regionalMovieBegin(begin);
    CHECK(fullEndingCheck(jp,lbaAt(j,jd[0x19]+clip),begin,sectors,clip==6)==0);
   }
   if (movieOn && disc==2 && clip>=8) {
    const int shift[]={3600,2000,2300,2900};
    int sourceOffset=(shift[clip-8]*10+7)/8*8+7;
    CHECK(discRead(jp,lbaAt(j,jd[0x19]+6)+sourceOffset,old));
    CHECK(!memcmp(raw+24,old+24,2304));
   }
#ifdef XG_CAPTIONS
#include "subtitles_test.inc"
#endif
   CHECK(discRead(current,begin+sectors-1,raw));regionalMovieSector(begin+sectors-1,raw);
   regionalMovieEnd(); CHECK(regionalSubtitle()==NULL);
  }
  if (battleOn) {
   int base=dirs[0x2d]-1, voice=base+0x11, native=base+0x10;
   int vn=pcRegionalFile(voice,t,dirs,NULL), nn=sizeAt(t,native);
   uint8_t* v=malloc(vn+16); uint8_t* a=malloc(nn+16); CHECK(v && a && nn>0);
   memset(v,0xcd,vn+16);memset(a,0xcd,nn+16);
   LoadingBatchCommand batch[]={{.m0_fileIndex=0x12,.m4_loadPtr=v},{.m0_fileIndex=0x11,.m4_loadPtr=a},{0}};
   CHECK(pcRegionalBatch(batch,base,t,dirs)==1);
   uint8_t* expected=file(current,t,native); CHECK(expected && !memcmp(a,expected,nn));free(expected);
   expected=malloc(vn); CHECK(expected && pcRegionalFile(voice,t,dirs,expected)==vn && !memcmp(v,expected,vn));free(expected);
   for(int k=0;k<16;k++) CHECK(v[vn+k]==0xcd && a[nn+k]==0xcd);
   uint8_t bad[32768];memcpy(bad,t,sizeof bad);memset(bad+7*native,0xff,3);
   memset(v,0xab,vn);memset(a,0xab,nn);
   CHECK(pcRegionalBatch(batch,base,bad,dirs)==-1);
   for(int k=0;k<vn;k++) CHECK(v[k]==0xab);
   for(int k=0;k<nn;k++) CHECK(a[k]==0xab);
   free(v);free(a);
  }
  char why[1200]; CHECK(!regionalSetSource(disc,argv[disc],why,sizeof why));
  CHECK(strstr(why,"Japanese Disc"));
  CdDisc* us=current; current=jp;
  CHECK(cdDiscIsJapanese() && !pcRegionalFile(jd[0x2d]-1+0x11,j,jd,NULL));
#ifdef XG_CAPTIONS
#include "subtitles_reels_test.inc"
#endif
  int begin=lbaAt(j,jd[0x19]+2); uint8_t raw[2352], old[2352];
  regionalMovieBegin(begin);CHECK(discRead(jp,begin+7,raw));memcpy(old,raw,2352);
  regionalMovieSector(begin+7,raw);CHECK(!memcmp(raw,old,2352));regionalMovieEnd();
  current=us;
  discClose(current);discClose(jp);
 }
 printf("regional audio: %d checks passed\n",checks);return 0;
}
