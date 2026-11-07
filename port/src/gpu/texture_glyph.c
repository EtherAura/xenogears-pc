/* Text layout stays in the game's packets. These bounded records describe
 * native index images; only the enhanced display receives scaled indices. */
#include <stdlib.h>
#include <string.h>
#include "texture.h"
#include "pc_glyph.h"
#include "system/settings.h"

#define RAM_SLOTS 1024
#define PIECE_SLOTS 2048
#define SOURCE_BYTES (4u*1024u*1024u)
/* Composed overlays, found by content: battle re-decodes its HUD/text
 * atlases every frame with identical pixels, so a decode revision alone
 * matched nothing. One battle or dialogue screen draws dozens of text
 * primitives; the byte cap bounds memory (xg-j85.22). */
#define CACHE_SLOTS 256
#define CACHE_BUCKETS 512
#define CACHE_BYTES (16u*1024u*1024u)
/* Pieces indexed by the 64-halfword VRAM column blocks they overlap (16
 * across, 2 down), so a draw visits only pieces near its texture page. */
#define CELLS 32
typedef struct {
    uintptr_t addr;
    uint64_t age;
    int stride, w, odd, col, proportional;
    uint8_t indices[14*13];
} RamGlyph;
typedef struct {
    uint64_t age, key, checked, group;
    int x, y, w, h, depth, shift, mask, limit, valid, proportional;
    uint8_t* indices;
} Piece;
typedef struct {
    /* keyed: texels/palette are the decode's content keys and place its
     * page and UVs; otherwise decoded is the decode's revision. */
    uint64_t age, decoded, texels, palette, layout, pack;
    uint32_t place;
    int w, h, keyed, next; /* next: slot+1 in this bucket's chain */
    TextureSample sample;
    uint8_t* rgba;
    size_t bytes;
} Sample;
static RamGlyph ram[RAM_SLOTS];
static Piece pieces[PIECE_SLOTS];
static Sample samples[CACHE_SLOTS];
static int buckets[CACHE_BUCKETS]; /* slot+1, 0 empty */
static uint64_t cellPieces[CELLS][PIECE_SLOTS/64];
static int (*textPolicy)(void);
static TextureGlyphStats stats;
static uint64_t clock, layout, revision;
static int loaded, enabled;

int textureGlyphsEnabled(void) {
    if (!loaded) {
        loaded=1;
        settingsDescribe("text_glyphs","Higher-resolution indexed glyph overlays (off by default); live game palette, enhanced display only");
        enabled=settingsBool("text_glyphs",0);
    }
    return enabled;
}
void textureSetGlyphs(int on) {
    textureGlyphsEnabled(); enabled=!!on;
    settingsSetInt("text_glyphs",enabled); settingsSave();
}
static int smoothLoaded, smooth;
int textureGlyphSmooth(void) {
    if (!smoothLoaded) {
        smoothLoaded=1;
        settingsDescribe("text_smooth","Enhanced text with smooth edges (on by default): the glyph overlay's edges blend into the picture; 0 keeps them hard");
        smooth=settingsBool("text_smooth",1);
    }
    return smooth;
}
void textureSetGlyphSmooth(int on) {
    textureGlyphSmooth(); smooth=!!on;
    settingsSetInt("text_smooth",smooth); settingsSave();
}
void textureGlyphPolicy(int (*textUsesGlyphs)(void)) { textPolicy=textUsesGlyphs; }
void textureGlyphStats(TextureGlyphStats* out) { *out=stats; }
/* The cells a VRAM rectangle (x0..x1 in halfwords, y0..y1) overlaps. */
static uint32_t cellsOf(int x0,int y0,int x1,int y1) {
    x0=x0<0?0:x0/64; x1=x1/64>15?15:x1/64; y0=y0<0?0:y0/256; y1=y1/256>1?1:y1/256;
    uint32_t cells=0;
    for (int y=y0;y<=y1;++y) for (int x=x0;x<=x1;++x) cells|=1u<<(y*16+x);
    return cells;
}
static void markPiece(const Piece* p,int on) {
    int ppw=16/p->depth; size_t i=(size_t)(p-pieces);
    uint32_t cells=cellsOf(p->x/ppw,p->y,(p->x+p->w-1)/ppw,p->y+p->h-1);
    for (int c=0;c<CELLS;++c) if (cells&(1u<<c)) {
        if (on) cellPieces[c][i/64]|=UINT64_C(1)<<(i%64);
        else cellPieces[c][i/64]&=~(UINT64_C(1)<<(i%64));
    }
}
static void dropPiece(Piece* p) {
    if (p->indices) { markPiece(p,0); stats.sourceBytes-=(size_t)p->w*p->h; --stats.pieces; free(p->indices); }
    memset(p,0,sizeof(*p)); ++layout;
}
static uint32_t sampleHash(const Sample* s) {
    uint64_t h=s->keyed?s->texels^s->palette*UINT64_C(0x9E3779B97F4A7C15)^((uint64_t)s->place<<17)^
               ((uint64_t)(unsigned)s->w<<40)^((uint64_t)(unsigned)s->h<<52):s->decoded;
    h^=s->layout*UINT64_C(0xC2B2AE3D27D4EB4F)^s->pack*UINT64_C(0x165667B19E3779F9);
    h^=h>>29; h*=UINT64_C(0xBF58476D1CE4E5B9); h^=h>>32;
    return (uint32_t)(h%CACHE_BUCKETS);
}
static int sameSample(const Sample* a,const Sample* b) {
    if (a->keyed!=b->keyed || a->layout!=b->layout || a->pack!=b->pack) return 0;
    return a->keyed?a->texels==b->texels && a->palette==b->palette && a->place==b->place && a->w==b->w && a->h==b->h:
                    a->decoded==b->decoded;
}
static void dropSample(Sample* s) {
    if (s->rgba) {
        stats.cacheBytes-=s->bytes; --stats.cached; free(s->rgba);
        int at=(int)(s-samples)+1;
        for (int* link=&buckets[sampleHash(s)];*link;link=&samples[*link-1].next)
            if (*link==at) { *link=s->next; break; }
    }
    memset(s,0,sizeof(*s));
}
void textureGlyphReset(void) {
    for (int i=0;i<PIECE_SLOTS;++i) dropPiece(&pieces[i]);
    for (int i=0;i<CACHE_SLOTS;++i) dropSample(&samples[i]);
    memset(ram,0,sizeof(ram)); memset(&stats,0,sizeof(stats));
    memset(cellPieces,0,sizeof(cellPieces)); memset(buckets,0,sizeof(buckets));
    /* Revisions never repeat while a queued immutable GPU copy can exist. */
}
static uint64_t byte(uint64_t h,unsigned b) { return (h^(uint8_t)b)*UINT64_C(1099511628211); }
uint64_t textureGlyphKey(const uint8_t* p,int w,int h,int depth) {
    uint64_t key=UINT64_C(14695981039346656037);
    const char* domain="xgtex1";
    while (*domain) key=byte(key,*domain++);
    key=byte(key,depth); key=byte(key,w); key=byte(key,w>>8); key=byte(key,h); key=byte(key,h>>8);
    for (int i=0;i<w*h;++i) key=byte(key,p[i]);
    return key;
}
void pcGlyphClearRAM(const void* image,int bytes,unsigned planes) {
    if (bytes<=0) return;
    uintptr_t lo=(uintptr_t)image, hi=lo+(unsigned)bytes;
    if (hi<lo) return;
    for (int i=0;i<RAM_SLOTS;++i) {
        RamGlyph* r=&ram[i];
        uintptr_t end=r->addr+(unsigned)(12*r->stride+(r->col+r->w+3)/4)*2;
        if (r->age && (planes&(1u<<r->odd)) && r->addr<hi && end>lo) {
            memset(r,0,sizeof(*r)); --stats.ramRecords;
        }
    }
}
void pcGlyphDialog(const void* image,int stride,int width,int odd) {
    if (!image || stride<3 || stride>256 || (width!=8 && width!=12)) return;
    odd&=1;
    RamGlyph* slot=&ram[0];
    for (int i=0;i<RAM_SLOTS;++i) {
        if (ram[i].age && ram[i].addr==(uintptr_t)image && !ram[i].col && ram[i].odd==odd) { slot=&ram[i]; break; }
        if (ram[i].age<slot->age) slot=&ram[i];
    }
    if (!slot->age) ++stats.ramRecords;
    slot->addr=(uintptr_t)image; slot->stride=stride; slot->w=width; slot->odd=odd; slot->age=++clock;
    slot->col=slot->proportional=0;
    const uint16_t* data=image;
    for (int y=0;y<13;++y) for (int x=0;x<width;++x)
        slot->indices[y*width+x]=(uint8_t)((data[y*stride+x/4]>>((x%4)*4+odd*2))&3);
    textureDumpGlyph(textureGlyphKey(slot->indices,width,13,4),slot->indices,width,13,4);
}
void pcGlyphProportional(const void* image,int stride,int x,int width,int odd,const unsigned char* indices) {
    if (!image || !indices || stride<1 || stride>256 || x<0 || width<1 || width>14 || x+width>stride*4) return;
    uintptr_t addr=(uintptr_t)image+(unsigned)(x/4)*2;
    odd&=1;
    RamGlyph* slot=&ram[0];
    for (int i=0;i<RAM_SLOTS;++i) {
        if (ram[i].age && ram[i].addr==addr && ram[i].col==(x&3) && ram[i].odd==odd) { slot=&ram[i]; break; }
        if (ram[i].age<slot->age) slot=&ram[i];
    }
    if (!slot->age) ++stats.ramRecords;
    slot->addr=addr; slot->stride=stride; slot->w=width; slot->odd=odd; slot->col=x&3;
    slot->proportional=1; slot->age=++clock;
    memcpy(slot->indices,indices,(size_t)width*13);
    textureDumpGlyph(textureGlyphKey(indices,width,13,4),indices,width,13,4);
}
void pcGlyphClearColumns(const void* image,int stride,int odd,int first,int last) {
    uintptr_t base=(uintptr_t)image;
    for (int i=0;i<RAM_SLOTS;++i) {
        RamGlyph* r=&ram[i];
        if (!r->age || r->stride!=stride || r->odd!=(odd&1) || r->addr<base || r->addr>=base+(unsigned)stride*2) continue;
        int x=(int)((r->addr-base)/2)*4+r->col;
        if (x<=last && x+r->w>first) { memset(r,0,sizeof(*r)); --stats.ramRecords; }
    }
}
static Piece* oldestPiece(void) {
    Piece* p=NULL;
    for (int i=0;i<PIECE_SLOTS;++i)
        if (pieces[i].indices && (!p || pieces[i].age<p->age)) p=&pieces[i];
    return p;
}
static void addPiece(int x,int y,int w,int h,int depth,int shift,int mask,int limit,uint64_t group,const uint8_t* indices) {
    size_t bytes=(size_t)w*h;
    Piece* p=&pieces[0];
    for (int i=0;i<PIECE_SLOTS;++i) {
        Piece* a=&pieces[i];
        if (a->indices && a->x==x && a->y==y && a->depth==depth && a->mask==mask) {
            if (a->w==w && a->h==h && a->group==group && !memcmp(a->indices,indices,bytes)) {
                a->age=++clock; return;
            }
            p=a; break;
        }
        if (a->age<p->age) p=a;
    }
    uint8_t* copy=malloc(bytes); if (!copy) return;
    memcpy(copy,indices,bytes); dropPiece(p);
    while (stats.sourceBytes+bytes>SOURCE_BYTES) dropPiece(oldestPiece());
    p->indices=copy; p->x=x; p->y=y; p->w=w; p->h=h; p->depth=depth;
    p->shift=shift; p->mask=mask; p->limit=limit; p->age=++clock;
    p->proportional=group!=0; p->group=group;
    p->key=textureGlyphKey(indices,w,h,depth);
    markPiece(p,1);
    stats.sourceBytes+=bytes; ++stats.pieces;
    textureDumpGlyph(p->key,indices,w,h,limit);
}
void textureGlyphUpload(int x,int y,int w,int h,const uint16_t* data) {
    if (!data || w<1 || h<13 || w>256) return;
    uintptr_t base=(uintptr_t)data, end=base+(size_t)w*h*2;
    /* Repeated uploads of an unchanged line keep their pieces and caches.
     * Include independent glyph metadata: the same native pixels can have
     * different authored replacements when their decomposition changes. */
    uint64_t group=UINT64_C(14695981039346656037);
    int proportional=0;
    for (int i=0;i<RAM_SLOTS;++i) {
        RamGlyph* r=&ram[i];
        if (!r->age || !r->proportional || r->stride!=w || r->addr<base || r->addr>=end) continue;
        proportional=1;
        uint64_t key=textureGlyphKey(r->indices,r->w,13,4);
        uint64_t where=((r->addr-base)/2<<4)|(unsigned)(r->col*2+r->odd);
        for (int b=0;b<8;++b) { group=byte(group,(unsigned)(key>>(8*b))); group=byte(group,(unsigned)(where>>(8*b))); }
    }
    if (proportional) {
        uint64_t dataKey=textureGlyphKey((const uint8_t*)data,w*2,h,0);
        uint64_t location=(unsigned)x|((uint64_t)(unsigned)y<<32);
        for (int b=0;b<8;++b) { group=byte(group,(unsigned)(dataKey>>(8*b))); group=byte(group,(unsigned)(location>>(8*b))); }
        if (!group) group=1;
    } else group=0;
    /* A line upload replaces its old composition, including glyphs no longer
     * in RAM. Keep unrelated atlas and native records on their existing path. */
    for (int i=0;i<PIECE_SLOTS;++i) {
        Piece* p=&pieces[i];
        if (p->indices && p->proportional && p->group!=group && p->x<(x+w)*4 && p->x+p->w>x*4 &&
            p->y<y+h && p->y+p->h>y) dropPiece(p);
    }
    for (int i=0;i<RAM_SLOTS;++i) {
        RamGlyph* r=&ram[i];
        if (!r->age || r->stride!=w || r->addr<base || r->addr>=end || (r->addr-base)%2) continue;
        size_t word=(r->addr-base)/2;
        int row=(int)(word/w), col=(int)(word%w);
        int words=(r->col+r->w+3)/4;
        if (row+13>h || col+words>w || y+row+13>VRAM_H || x+col+words>VRAM_W) continue;
        int valid=1;
        for (int yy=0;valid && yy<13;++yy) for (int xx=0;xx<r->w;++xx)
            {
                unsigned native=(data[word+(size_t)yy*w+(xx+r->col)/4]>>(((xx+r->col)%4)*4+r->odd*2))&3;
                unsigned own=r->indices[yy*r->w+xx];
                if (r->proportional ? (native&own)!=own : native!=own) { valid=0; break; }
            }
        if (valid) addPiece((x+col)*4+r->col,y+row,r->w,13,4,r->odd*2,3<<(r->odd*2),4,r->proportional?group:0,r->indices);
    }
}
static int geometry(unsigned tp,int u,int v,int w,int h,int* depth,int* x,int* y) {
    int bits=(tp>>7)&3;
    u&=255; v&=255;
    if (bits>1 || w<1 || w>256 || h<1 || h>64 || u+w>256 || v+h>256) return 0;
    *depth=bits?8:4;
    int ppw=16 / *depth;
    *x=(int)(tp&15)*64*ppw+u; *y=(int)((tp>>4)&1)*256+v;
    return *x+w<=VRAM_W*ppw && *y+h<=VRAM_H;
}
void pcGlyphTile(unsigned tp,int u,int v,int w,int h) {
    int depth,x,y;
    if (!geometry(tp,u,v,w,h,&depth,&x,&y)) return;
    int ppw=16/depth;
    VRect rect={x/ppw,y,(x+w-1)/ppw,y+h-1};
    /* Uploaded font atlases are already in the CPU shadow. Default metadata
     * collection never introduces a GPU readback. */
    if (vramIsDrawn(rect)) {
        /* The text category's live choice (texture_filter.c), else the
         * legacy switch before that policy has loaded. */
        int used=textPolicy?textPolicy():textureGlyphsEnabled();
        if (!used && !textureDumpEnabled()) return;
        vramSyncRect(rect);
    }
    uint8_t indices[256*64]; int mask=(1<<depth)-1;
    for (int yy=0;yy<h;++yy) for (int xx=0;xx<w;++xx)
        indices[yy*w+xx]=(uint8_t)((vramShadow[y+yy][(x+xx)/ppw]>>(((x+xx)%ppw)*depth))&mask);
    addPiece(x,y,w,h,depth,0,mask,1<<depth,0,indices);
}
/* Independent proportional glyphs share outline pixels. Validate their OR
 * composition against native VRAM, rather than mistaking a neighbour's
 * outline for an overwrite. Each authored key still describes one glyph. */
static int proportionalMatches(const Piece* p) {
    uint8_t expected[14*13]={0};
    for (int i=0;i<PIECE_SLOTS;++i) {
        const Piece* a=&pieces[i];
        if (!a->indices || a->group!=p->group || a->mask!=p->mask || a->x>=p->x+p->w ||
            a->x+a->w<=p->x || a->y>=p->y+p->h || a->y+a->h<=p->y) continue;
        int x0=a->x>p->x?a->x:p->x, x1=a->x+a->w<p->x+p->w?a->x+a->w:p->x+p->w;
        int y0=a->y>p->y?a->y:p->y, y1=a->y+a->h<p->y+p->h?a->y+a->h:p->y+p->h;
        for (int y=y0;y<y1;++y) for (int x=x0;x<x1;++x)
            expected[(y-p->y)*p->w+x-p->x]|=a->indices[(y-a->y)*a->w+x-a->x];
    }
    for (int y=0;y<p->h;++y) for (int x=0;x<p->w;++x) {
        unsigned native=vramShadow[p->y+y][(p->x+x)/4]>>(((p->x+x)%4)*4+p->shift);
        if ((native&3)!=expected[y*p->w+x]) return 0;
    }
    return 1;
}
static int matches(Piece* p) {
    int ppw=16/p->depth;
    VRect r={p->x/ppw,p->y,(p->x+p->w-1)/ppw,p->y+p->h-1};
    uint64_t signature=UINT64_C(14695981039346656037);
    for (int y=r.y0/16;y<=r.y1/16;++y) for (int x=r.x0/16;x<=r.x1/16;++x)
        signature=(signature^vramTileGeneration(x,y))*UINT64_C(1099511628211);
    signature^=layout;
    if (p->valid && p->checked==signature) return 1;
    vramSyncRect(r);
    if (p->proportional) {
        if (!proportionalMatches(p)) {
            uint64_t group=p->group; int mask=p->mask;
            for (int i=0;i<PIECE_SLOTS;++i) if (pieces[i].indices && pieces[i].group==group && pieces[i].mask==mask) {
                ++stats.invalidated; dropPiece(&pieces[i]);
            }
            return 0;
        }
        p->valid=1; p->checked=signature; return 1;
    }
    for (int y=0;y<p->h;++y) for (int x=0;x<p->w;++x) {
        unsigned native=vramShadow[p->y+y][(p->x+x)/ppw]>>(((p->x+x)%ppw)*p->depth);
        if (((native&p->mask)>>p->shift)!=p->indices[y*p->w+x]) {
            ++stats.invalidated; dropPiece(p); return 0;
        }
    }
    p->valid=1; p->checked=signature; return 1;
}
static int selectPieces(TextureRequest r,int depth,int* x,int* y,int* selected) {
    /* Wrapped/windowed samples safely retain their original texture path. */
    if (r.window || (depth!=4 && depth!=8) || r.u<0 || r.v<0 || r.u>255 || r.v>255 ||
        r.w<1 || r.h<1 || r.w>256 || r.h>256) return 0;
    int ppw=16/depth;
    *x=(r.tpage&15)*64*ppw+r.u; *y=((r.tpage>>4)&1)*256+r.v;
    if (r.u+r.w>256 || r.v+r.h>256 || *x+r.w>VRAM_W*ppw || *y+r.h>VRAM_H) return 0;
    /* Ascending piece order, as a scan of every slot: overlapping pieces
     * of one plane compose in that order. A word is read before its pieces
     * are checked; one dropped meanwhile has no indices. */
    uint32_t cells=cellsOf(*x/ppw,*y,(*x+r.w-1)/ppw,*y+r.h-1);
    int count=0;
    for (int word=0;word<PIECE_SLOTS/64;++word) {
        uint64_t bits=0;
        for (int c=0;c<CELLS;++c) if (cells&(1u<<c)) bits|=cellPieces[c][word];
        for (;bits;bits&=bits-1) {
            int i=word*64+__builtin_ctzll(bits);
            Piece* p=&pieces[i];
            if (p->indices && p->depth==depth && p->x<*x+r.w && p->x+p->w>*x &&
                p->y<*y+r.h && p->y+p->h>*y && matches(p)) selected[count++]=i;
        }
    }
    /* A later mismatch can retire a composition already selected above. */
    int kept=0;
    for (int i=0;i<count;++i) if (pieces[selected[i]].indices) selected[kept++]=selected[i];
    return kept;
}
void textureGlyphDiscover(TextureRequest r) {
    if (!textureDumpEnabled()) return;
    int bits=(r.tpage>>7)&3, x,y,selected[PIECE_SLOTS];
    int count=selectPieces(r,bits==0?4:bits==1?8:15,&x,&y,selected);
    for (int i=0;i<count;++i) {
        Piece* p=&pieces[selected[i]];
        textureDumpGlyph(p->key,p->indices,p->w,p->h,p->limit);
    }
}
int textureGlyphCanvas(TextureRequest r) {
    int bits=(r.tpage>>7)&3, x,y,selected[PIECE_SLOTS];
    int count=selectPieces(r,bits==0?4:bits==1?8:15,&x,&y,selected);
    for (int i=0;i<count;++i) {
        const Piece* p=&pieces[selected[i]];
        if (p->proportional || p->x!=x || p->y!=y || p->w!=r.w || p->h!=r.h) continue;
        const uint8_t* indices=NULL; int scale=0;
        if (texturePackGlyph(p->key,p->w,p->h,p->limit,&indices,&scale) ||
            textureHDGlyph(p->key,p->w,p->h,p->limit,&indices,&scale)) return scale>1;
    }
    return 0;
}
int textureGlyphSample(const DecodedTexture* d,TextureSample* out) {
    return textureGlyphsEnabled()?textureGlyphCompose(d,out):0;
}
int textureGlyphCompose(const DecodedTexture* d,TextureSample* out) {
    if (!d || !d->paletteSize) return 0;
    TextureRequest r=d->request;
    int x,y,selected[PIECE_SLOTS];
    int count=selectPieces(r,d->depth,&x,&y,selected);
    if (!count) return 0;
    /* The overlay is a function of the decoded indices and palette, the
     * pieces over its page position (layout) and the authored maps (pack). */
    Sample want={0};
    want.keyed=d->texelKey && !r.stableTo;
    want.decoded=d->revision; want.texels=d->texelKey; want.palette=d->paletteKey;
    want.place=(uint32_t)r.tpage|(uint32_t)(r.u&255)<<16|(uint32_t)(r.v&255)<<24;
    want.w=r.w; want.h=r.h; want.layout=layout;
    want.pack=texturePackGeneration()*2+!textureReplacementsShown(); /* the A/B comparison's overlays apart */
    uint32_t bucket=sampleHash(&want);
    for (int at=buckets[bucket];at;at=samples[at-1].next) {
        Sample* s=&samples[at-1];
        if (s->rgba && sameSample(s,&want)) { s->age=++clock; *out=s->sample; ++stats.overlays; return 1; }
    }
    Sample* slot=&samples[0];
    for (int i=0;i<CACHE_SLOTS && slot->rgba;++i) if (!samples[i].rgba || samples[i].age<slot->age) slot=&samples[i];
    int scale=8;
    while ((size_t)r.w*r.h*scale*scale>1024*1024) scale/=2;
    int width=r.w*scale,height=r.h*scale;
    size_t pixels=(size_t)width*height, bytes=pixels*4;
    uint8_t* indices=malloc(pixels), *rgba=malloc(bytes);
    if (!indices || !rgba) { free(indices); free(rgba); return 0; }
    for (int yy=0;yy<height;++yy) for (int xx=0;xx<width;++xx)
        indices[(size_t)yy*width+xx]=d->texels[(yy/scale)*r.w+xx/scale];
    /* Clear just the selected proportional planes before OR-compositing their
     * enhanced glyphs. The other packed line and palette remain native. */
    for (int i=0;i<count;++i) {
        Piece* p=&pieces[selected[i]];
        if (!p->proportional) continue;
        int x0=p->x>x?p->x:x, x1=p->x+p->w<x+r.w?p->x+p->w:x+r.w;
        int y0=p->y>y?p->y:y, y1=p->y+p->h<y+r.h?p->y+p->h:y+r.h;
        for (int yy=y0*scale;yy<y1*scale;++yy) for (int xx=x0*scale;xx<x1*scale;++xx)
            indices[(size_t)(yy-y*scale)*width+xx-x*scale]&=(uint8_t)~p->mask;
    }
    /* Native fallback and authored maps both use nearest index selection.
     * Edge reconstruction alters strokes and makes small letters muddy. */
    for (int i=0;i<count;++i) {
        Piece* p=&pieces[selected[i]];
        const uint8_t* replacement=NULL; int authored=0;
        if (texturePackGlyph(p->key,p->w,p->h,p->limit,&replacement,&authored)) ++stats.packGlyphs;
        else if (textureHDGlyph(p->key,p->w,p->h,p->limit,&replacement,&authored)) ++stats.packGlyphs;
        int x0=p->x>x?p->x:x, x1=p->x+p->w<x+r.w?p->x+p->w:x+r.w;
        int y0=p->y>y?p->y:y, y1=p->y+p->h<y+r.h?p->y+p->h:y+r.h;
        for (int yy=y0*scale;yy<y1*scale;++yy) for (int xx=x0*scale;xx<x1*scale;++xx) {
            int gx=xx-p->x*scale,gy=yy-p->y*scale;
            uint8_t index=replacement?replacement[(gy*authored/scale)*(p->w*authored)+gx*authored/scale]:
                p->indices[(gy/scale)*p->w+gx/scale];
            size_t at=(size_t)(yy-y*scale)*width+xx-x*scale;
            if (p->proportional) indices[at]|=(uint8_t)((index<<p->shift)&p->mask);
            else indices[at]=(uint8_t)((indices[at]&~p->mask)|((index<<p->shift)&p->mask));
        }
    }
    for (size_t i=0;i<pixels;++i) {
        uint16_t word=d->palette[indices[i]];
        for (int k=0;k<3;++k) { int c=(word>>(k*5))&31; rgba[i*4+k]=(uint8_t)((c<<3)|(c>>2)); }
        rgba[i*4+3]=word==0?0:word&0x8000?128:255;
    }
    free(indices); dropSample(slot);
    while (stats.cacheBytes+bytes>CACHE_BYTES) {
        Sample* victim=NULL;
        for (int i=0;i<CACHE_SLOTS;++i) if (samples[i].rgba && (!victim || samples[i].age<victim->age)) victim=&samples[i];
        dropSample(victim);
    }
    *slot=want;
    slot->rgba=rgba; slot->bytes=bytes; slot->age=++clock;
    slot->next=buckets[bucket]; buckets[bucket]=(int)(slot-samples)+1;
    slot->sample=(TextureSample){rgba,UINT64_C(0x4000000000000000)|++revision,r.u,r.v,r.w,r.h,scale,NULL,0};
    stats.cacheBytes+=bytes; ++stats.cached; ++stats.overlays; *out=slot->sample; return 1;
}
