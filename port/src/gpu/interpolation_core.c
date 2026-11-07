#include "interpolation_core.h"
#include "pc_interpolation.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define LOOKUP_SIZE (INTERP_MAX_DRAWS * 2)
static uint32_t drawHash(uint64_t object, uint64_t face) {
    uint64_t h = object * UINT64_C(0x9e3779b97f4a7c15) ^ face;
    h ^= h >> 30; h *= UINT64_C(0xbf58476d1ce4e5b9); h ^= h >> 27;
    return (uint32_t)h & (LOOKUP_SIZE - 1);
}
static uint32_t* drawSlot(const InterpFrame* f, uint64_t object, uint64_t face) {
    if (!f->lookup) return NULL;
    uint32_t h = drawHash(object, face);
    for (int n = 0; n < LOOKUP_SIZE; n++, h = (h + 1) & (LOOKUP_SIZE - 1)) {
        uint32_t* p = &f->lookup[h];
        if (!*p) return p;
        const InterpDraw* d = &f->draws[*p - 1];
        if (d->object == object && d->face == face) return p;
    }
    return NULL;
}
static const InterpDraw* findDraw(const InterpFrame* f, const InterpDraw* d) {
    uint32_t* p = drawSlot(f, d->object, d->face);
    return p && *p ? &f->draws[*p - 1] : NULL;
}
static uint32_t* anchorSlot(const InterpFrame* f, uint64_t object) {
    if (!f->anchors) return NULL;
    uint32_t h = drawHash(object, 0);
    for (int n = 0; n < LOOKUP_SIZE; n++, h = (h + 1) & (LOOKUP_SIZE - 1)) {
        uint32_t* p = &f->anchors[h];
        if (!*p || f->draws[*p - 1].object == object) return p;
    }
    return NULL;
}
static const InterpDraw* findAnchor(const InterpFrame* f, uint64_t object) {
    uint32_t* p = anchorSlot(f, object);
    return p && *p ? &f->draws[*p - 1] : NULL;
}

static int room(void** ptr, uint32_t* cap, uint32_t need, uint32_t limit, size_t size) {
    if (need > limit) return 0;
    if (need <= *cap) return 1;
    uint32_t n = *cap ? *cap : 256;
    while (n < need) n = n > limit / 2 ? limit : n * 2;
    void* next = realloc(*ptr, (size_t)n * size);
    if (!next) return 0;
    *ptr = next;
    *cap = n;
    return 1;
}

int interpCaptureRect(const HiVertex* v, VRect* rect) {
    int bounds[4];
    for (int axis=0;axis<2;axis++) {
        int lo=axis?(int16_t)(v->uvMin>>16):(int16_t)v->uvMin;
        int hi=axis?(int16_t)(v->uvMax>>16):(int16_t)v->uvMax;
        if (hi<lo) return 0;
        if (hi-lo>=255) { lo=0; hi=255; }
        unsigned mask=(v->texWindow>>(axis*5)&31)*8,offset=(v->texWindow>>(10+axis*5)&31)*8;
        int first=256,last=-1;
        for(int q=lo;q<=hi;q++) {
            int t=(((unsigned)q&255)&~mask)|(offset&mask);
            if(t<first)first=t;
            if(t>last)last=t;
        }
        int origin=axis?(int16_t)(v->sample[0]>>16):(int16_t)v->sample[0];
        bounds[axis]=origin+first;bounds[axis+2]=origin+last;
    }
    *rect=(VRect){bounds[0],bounds[1],bounds[2],bounds[3]};return 1;
}

/* Order whole native pixel blocks, including every texel an enhanced block
 * can sample. Every edge follows the measured native raster order. Resolve
 * earlier sources first, then propagate to future overwrites: one pixel may
 * have several source dependencies at >1x. */
int interpCaptureSteps(const HiVertex* v,const int32_t* rows,VRect box,uint32_t* map,int n,int* overlap) {
    int bw=box.x1-box.x0+1,bh=box.y1-box.y0+1;
    memset(map,0,(size_t)bw*bh*sizeof(*map));*overlap=0;
    VRect sourceBounds;
    if (interpCaptureRect(v, &sourceBounds) &&
        (sourceBounds.x1 < box.x0 || sourceBounds.x0 > box.x1 ||
         sourceBounds.y1 < box.y0 || sourceBounds.y0 > box.y1)) return 0;
    float p[6]={0};
    if(n>1) {
        float x1=v[1].x-v[0].x,y1=v[1].y-v[0].y,x2=v[2].x-v[0].x,y2=v[2].y-v[0].y;
        float det=x1*y2-y1*x2;
        if(det==0)return 0;
        for(int axis=0;axis<2;axis++) {
            float a=axis?v[0].v:v[0].u,b=axis?v[1].v:v[1].u,c=axis?v[2].v:v[2].u;
            p[axis*3]=((b-a)*y2-(c-a)*y1)/det;
            p[axis*3+1]=((c-a)*x1-(b-a)*x2)/det;
            p[axis*3+2]=a-p[axis*3]*v[0].x-p[axis*3+1]*v[0].y;
        }
    }
    uint32_t last=0;
    int first=rows[0],end=first+rows[1]-1,start=rows[7]>first?rows[7]:first,down=start<=end?end-start+1:0;
    for(int i=0;i<rows[1];i++) {
        int y=i<down?start+i:(start<=end?start:end+1)-1-(i-down);
        const int32_t* r=rows+8+(y-first)*8;
        for(int x=r[0];x<=r[1];x++) {
            unsigned k=(unsigned)(x-r[0]);
            int lo[2],hi[2];
            for(int axis=0;axis<2;axis++) {
                lo[axis]=hi[axis]=(int32_t)((uint32_t)r[axis+2]+k*(uint32_t)rows[axis+2])>>16;
                if(n>1) {
                    const float* a=p+axis*3;
                    float small=(.5f-.5f*n)/n,large=-small;
                    float center=a[0]*x+a[1]*y+a[2]+.5f;
                    float radius=(fabsf(a[0])+fabsf(a[1]))*large;
                    /* Cover CPU/GPU affine rounding at exact texel edges. */
                    float error=0.000004f*(fabsf(a[0]*x)+fabsf(a[1]*y)+fabsf(a[2])+512);
                    int u0=(int)floorf(center-radius-error),u1=(int)floorf(center+radius+error);
                    int bound0=axis?(int16_t)(v->uvMin>>16):(int16_t)v->uvMin;
                    int bound1=axis?(int16_t)(v->uvMax>>16):(int16_t)v->uvMax;
                    if(u0<bound0)u0=bound0;
                    if(u1>bound1)u1=bound1;
                    if(u0<lo[axis])lo[axis]=u0;
                    if(u1>hi[axis])hi[axis]=u1;
                }
                if(hi[axis]-lo[axis]>=255){lo[axis]=0;hi[axis]=255;}
            }
            uint32_t* m=map+(y-box.y0)*bw+x-box.x0;
            for(int future=0;future<2;future++)for(int vv=lo[1];vv<=hi[1];vv++)for(int u=lo[0];u<=hi[0];u++) {
                int mx=(v->texWindow&31)*8,my=(v->texWindow>>5&31)*8;
                int ox=(v->texWindow>>10&31)*8,oy=(v->texWindow>>15&31)*8;
                int ax=((((u&255)&~mx)|(ox&mx))+(int16_t)v->sample[0])&(WIDE_W-1);
                int ay=((((vv&255)&~my)|(oy&my))+(int16_t)(v->sample[0]>>16))&(WIDE_H-1);
                if(ax<box.x0||ax>box.x1||ay<box.y0||ay>box.y1)continue;
                const int32_t* source=rows+8+(ay-first)*8;
                if(ax<source[0]||ax>source[1])continue;
                *overlap=1;
                if(ax==x&&ay==y)continue; /* the step's immutable source owns this whole block */
                int rank=ay>=start?ay-start:down+start-1-ay;
                int current=y>=start?y-start:down+start-1-y;
                int before=rank-current;
                uint32_t* t=map+(ay-box.y0)*bw+ax-box.x0;
                if(!future&&(before<0||(!before&&ax<x))) {if(*t>=*m)*m=*t+1;}
                if(future&&(before>0||(!before&&ax>x))) {if(*t<=*m)*t=*m+1;}
                if(*m>=4096||*t>=4096)return -1;
            }
            if(*m>last)last=*m;
        }
    }
    return (int)last;
}

void interpFrameClear(InterpFrame* f) {
    f->vertexCount = f->drawCount = f->pixelCount = f->opCount = 0;
    if (f->lookup) memset(f->lookup, 0, LOOKUP_SIZE * sizeof(*f->lookup));
    if (f->anchors) memset(f->anchors, 0, LOOKUP_SIZE * sizeof(*f->anchors));
    f->frozen = f->frozenCount = f->frozenDirty = f->frozenWrites = 0;
    f->capture = 0;
    f->cleared = 0;
    f->viewSet = 0;
    f->valid = 1;
    f->rejection = NULL;
}

void interpFrameReject(InterpFrame* f, const char* reason) {
    f->valid = 0;
    if (!f->rejection) f->rejection = reason;
}

void interpFrameFree(InterpFrame* f) {
    free(f->ops); free(f->vertices); free(f->draws); free(f->pixels); free(f->lookup); free(f->anchors);
    memset(f, 0, sizeof(*f));
}

int interpFrameOp(InterpFrame* f, InterpOp op) {
    if (!f->valid || !room((void**)&f->ops, &f->opCapacity, f->opCount+1, INTERP_MAX_OPS, sizeof(op))) {
        interpFrameReject(f,"operation-capacity"); return 0;
    }
    op.vertex = f->vertexCount;
    f->ops[f->opCount++] = op; return 1;
}
int interpFrameWords(InterpFrame* f, const uint32_t* words, uint32_t count, uint32_t* offset) {
    if (!f->valid || !words || !count || count > INTERP_MAX_PIXELS || f->pixelCount > INTERP_MAX_PIXELS-count || !room((void**)&f->pixels, &f->pixelCapacity, f->pixelCount+count, INTERP_MAX_PIXELS, sizeof(*words))) {
        interpFrameReject(f,"texture-capacity"); return 0;
    }
    *offset = f->pixelCount;
    memcpy(f->pixels+f->pixelCount,words,(size_t)count*sizeof(*words)); f->pixelCount += count; return 1;
}

int interpFrameDraw(InterpFrame* f, const HiVertex* v, int count, uint64_t object, uint64_t face, uint64_t material) {
    if (!v || !f->valid || count <= 0 || count % 3 || count > INTERP_MAX_VERTICES ||
        !room((void**)&f->vertices, &f->vertexCapacity, f->vertexCount + (uint32_t)count,
              INTERP_MAX_VERTICES, sizeof(*v)) ||
        !room((void**)&f->draws, &f->drawCapacity, f->drawCount + 1, INTERP_MAX_DRAWS, sizeof(InterpDraw))) {
        interpFrameReject(f, "snapshot-capacity");
        return 0;
    }
    memcpy(f->vertices + f->vertexCount, v, (size_t)count * sizeof(*v));
    if (object) {
        if (!f->lookup) f->lookup = calloc(LOOKUP_SIZE, sizeof(*f->lookup));
        uint32_t* p = drawSlot(f, object, face);
        if (!p || *p) { interpFrameReject(f, p ? "duplicate-face" : "identity-capacity"); return 0; }
        *p = f->drawCount + 1;
    }
    f->draws[f->drawCount++] = (InterpDraw){.object = object, .face = face, .material = material,
                                          .first = f->vertexCount, .count = (uint32_t)count};
    f->vertexCount += (uint32_t)count;
    return 1;
}
void interpFramePlacement(InterpFrame* f, float x, float y, float z) {
    if (!f->drawCount) return;
    InterpDraw* d = &f->draws[f->drawCount - 1];
    d->placement = 1; d->anchor[0] = x; d->anchor[1] = y; d->anchor[2] = z;
    if (!f->anchors) f->anchors = calloc(LOOKUP_SIZE, sizeof(*f->anchors));
    uint32_t* slot = anchorSlot(f, d->object);
    if (!slot) { interpFrameReject(f, "identity-capacity"); return; }
    if (!*slot) *slot = f->drawCount;
}

int interpFramePixels(InterpFrame* f, const uint8_t* rgba, size_t count, uint32_t* offset) {
    if (!f->valid || !rgba || !count || count > INTERP_MAX_PIXELS ||
        !room((void**)&f->pixels, &f->pixelCapacity, f->pixelCount + (uint32_t)count,
              INTERP_MAX_PIXELS, sizeof(uint32_t))) {
        interpFrameReject(f, "texture-capacity");
        return 0;
    }
    *offset = f->pixelCount;
    /* The shader storage is words; input pixels have explicit RGBA bytes. */
    for (size_t i = 0; i < count; i++) {
        const uint8_t* p = rgba + 4 * i;
        f->pixels[f->pixelCount++] = (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
    }
    return 1;
}

static uint32_t rgbaWord(const uint8_t* p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
static unsigned colorSlot(uint32_t color) { return (color * 0x9e3779b1u >> 23) & 511; }
static uint32_t nativeRgba(unsigned word) {
    if (!word) return 0;
    unsigned r=word&31,g=word>>5&31,b=word>>10&31;
    return (r<<3|r>>2)|(g<<3|g>>2)<<8|(b<<3|b>>2)<<16|(word&32768?128u:255u)<<24;
}
static int rgbaNative(const uint8_t* p) {
    if (rgbaWord(p)==0xff000000u) return 0; /* coverage is stored separately */
    unsigned word=(p[0]>>3)|((p[1]>>3)<<5)|((p[2]>>3)<<10)|(p[3]==128?32768:0);
    return nativeRgba(word)==rgbaWord(p)?(int)word:-1;
}
static int nativeSample(InterpFrame* f,const uint8_t* rgba,size_t count,uint32_t* offset,uint32_t* format) {
    size_t colors=(count+1)/2;int opaqueBlack=0;
    for(size_t i=0;i<count;i++) {
        if(rgbaNative(rgba+i*4)<0)return interpFramePixels(f,rgba,count,offset);
        opaqueBlack|=rgbaWord(rgba+i*4)==0xff000000u;
    }
    size_t words=colors+(opaqueBlack?(count+31)/32:0);
    if (words>=count) return interpFramePixels(f,rgba,count,offset);
    if (!room((void**)&f->pixels,&f->pixelCapacity,f->pixelCount+(uint32_t)words,INTERP_MAX_PIXELS,sizeof(uint32_t))) {
        interpFrameReject(f,"texture-capacity");return 0;
    }
    *offset=f->pixelCount;
    memset(f->pixels+f->pixelCount,0,words*sizeof(uint32_t));
    for(size_t i=0;i<count;i++) {
        f->pixels[f->pixelCount+i/2]|=(uint32_t)rgbaNative(rgba+i*4)<<(i%2*16);
        if(opaqueBlack&&rgbaWord(rgba+i*4)==0xff000000u)f->pixels[f->pixelCount+colors+i/32]|=1u<<(i%32);
    }
    f->pixelCount+=(uint32_t)words;
    *format=(1u<<17)|INTERP_SAMPLE_NATIVE_WORD|(opaqueBlack?INTERP_SAMPLE_OPAQUE_BLACK:0);return 1;
}
int interpFrameSample(InterpFrame* f, const uint8_t* rgba, size_t count, uint32_t* offset, uint32_t* format) {
    *format = 0;
    if (!rgba || !count || count > INTERP_MAX_PIXELS || !f->valid)
        return interpFramePixels(f, rgba, count, offset);
    uint32_t palette[256], slots[512] = {0};
    unsigned colors = 0;
    for (size_t i = 0; i < count; i++) {
        uint32_t color = rgbaWord(rgba + i * 4);
        unsigned slot = colorSlot(color);
        while (slots[slot] && palette[slots[slot] - 1] != color) slot = (slot + 1) & 511;
        if (slots[slot]) continue;
        if (colors == 256) return nativeSample(f, rgba, count, offset,format);
        palette[colors] = color; slots[slot] = ++colors;
    }
    size_t indices = (count + 3) / 4, words = colors + indices;
    if (words >= count) return interpFramePixels(f, rgba, count, offset);
    if (!room((void**)&f->pixels, &f->pixelCapacity, f->pixelCount + (uint32_t)words,
              INTERP_MAX_PIXELS, sizeof(uint32_t))) {
        interpFrameReject(f, "texture-capacity"); return 0;
    }
    *offset = f->pixelCount;
    memcpy(f->pixels + f->pixelCount, palette, colors * sizeof(uint32_t));
    uint32_t* packed = f->pixels + f->pixelCount + colors;
    memset(packed, 0, indices * sizeof(uint32_t));
    for (size_t i = 0; i < count; i++) {
        uint32_t color = rgbaWord(rgba + i * 4);
        unsigned slot = colorSlot(color);
        while (palette[slots[slot] - 1] != color) slot = (slot + 1) & 511;
        packed[i / 4] |= (slots[slot] - 1) << (i % 4 * 8);
    }
    f->pixelCount += (uint32_t)words;
    *format = (1u << 17) | INTERP_SAMPLE_PALETTE | (colors - 1) << 24;
    return 1;
}
int interpSampleEqual(const InterpFrame* f, uint32_t offset, uint32_t format, const uint8_t* rgba, size_t count) {
    unsigned colors = (format >> 24) + 1;
    for (size_t i = 0; i < count; i++) {
        uint32_t color;
        if (format & INTERP_SAMPLE_NATIVE_WORD) {
            color=nativeRgba(f->pixels[offset+i/2]>>(i%2*16)&65535);
            if (!color&&(format&INTERP_SAMPLE_OPAQUE_BLACK)&&(f->pixels[offset+(count+1)/2+i/32]>>(i%32)&1))color=0xff000000u;
        } else if (format & INTERP_SAMPLE_PALETTE) {
            uint32_t word = f->pixels[offset + colors + i / 4];
            color = f->pixels[offset + ((word >> (i % 4 * 8)) & 255)];
        } else color = f->pixels[offset + i];
        if (color != rgbaWord(rgba + i * 4)) return 0;
    }
    return 1;
}

/* terrain: depth-cued, its palette (the word's top half) steps as the camera moves. */
static const char* continuous(const HiVertex* a, const HiVertex* b, int w, int h, int transition, int terrain) {
    if (!isfinite(a->x) || !isfinite(a->y) || !isfinite(a->w) ||
        !isfinite(b->x) || !isfinite(b->y) || !isfinite(b->w) || a->w <= 0 || b->w <= 0) return "invalid-projection";
    if (fabsf(a->x - b->x) > 32 || fabsf(a->y - b->y) > 32) return "motion-bound";
    (void)w; (void)h; /* Replay clips each fragment to the owned viewport. */
    return a->u == b->u && a->v == b->v &&
           a->mode == b->mode && (terrain ? (a->tex & 0xFFFFu) == (b->tex & 0xFFFFu) : a->tex == b->tex) && a->texWindow == b->texWindow &&
           a->clipMin == b->clipMin && a->clipMax == b->clipMax && (transition || (a->uvMin == b->uvMin && a->uvMax == b->uvMax)) ? NULL : "draw-attributes";
}

static int winding(const HiVertex* a, const HiVertex* b, int collapse) {
    double x1 = a[1].x - a[0].x, y1 = a[1].y - a[0].y;
    double x2 = a[2].x - a[0].x, y2 = a[2].y - a[0].y;
    double dx1 = b[1].x - b[0].x - x1, dy1 = b[1].y - b[0].y - y1;
    double dx2 = b[2].x - b[0].x - x2, dy2 = b[2].y - b[0].y - y2;
    double p = x1 * y2 - y1 * x2;
    double q = dx1 * y2 + x1 * dy2 - dy1 * x2 - y1 * dx2;
    double r = dx1 * dy2 - dy1 * dx2;
    double sign = p > 0 ? 1 : -1;
    if (fabs(p) < 1e-6 && (!collapse || fabs(p+q+r)<1e-6)) {
        for (int i = 0; i < 3; i++) if (a[i].x != b[i].x || a[i].y != b[i].y) return 0;
        return 1; /* an unchanged empty triangle remains empty */
    }
    double end=p+q+r;
    if (collapse && fabs(p)<1e-6 && fabs(end)>=1e-6) sign=end>0?1:-1;
    if (sign*p < (collapse?-1e-6:1e-6) || sign*end < (collapse?-1e-6:1e-6)) return 0;
    if (r * sign > 0) {
        double t = -q / (2 * r);
        if (t > 0 && t < 1 && sign * (p + q * t + r * t * t) < 1e-6) return 0;
    }
    return 1;
}

static const char* placementMismatch(const InterpDraw* a, const InterpDraw* b) {
    for (int k = 0; k < 3; k++)
        if (!isfinite(a->anchor[k]) || !isfinite(b->anchor[k])) return "invalid-projection";
    if (a->anchor[2] <= 0 || b->anchor[2] <= 0) return "invalid-projection";
    if (fabsf(a->anchor[0] - b->anchor[0]) > 32 || fabsf(a->anchor[1] - b->anchor[1]) > 32) return "motion-bound";
    return NULL;
}

const char* interpFramesMismatch(const InterpFrame* a, const InterpFrame* b) {
    if (!a || !b) return "missing-endpoint";
    if (a->capture != b->capture || a->frozen != b->frozen || a->frozenCount != b->frozenCount || a->frozenWrites != b->frozenWrites) return "capture-change";
    uint32_t ai=0, bi=0;
    for (;;) {
        while (ai<a->opCount && a->ops[ai].kind!=INTERP_OP_COPY) ai++;
        while (bi<b->opCount && b->ops[bi].kind!=INTERP_OP_COPY) bi++;
        if (ai==a->opCount || bi==b->opCount) {
            if (ai!=a->opCount || bi!=b->opCount) return "capture-order-change";
            break;
        }
        const InterpOp *x=&a->ops[ai++], *y=&b->ops[bi++];
        if (x->w!=y->w || x->h!=y->h || x->sx!=y->sx || x->dx!=y->dx || x->data!=y->data ||
            x->sy-((x->data&1)?a->viewY:0)!=y->sy-((y->data&1)?b->viewY:0) ||
            x->dy-((x->data&2)?a->viewY:0)!=y->dy-((y->data&2)?b->viewY:0)) return "capture-order-change";
    }
    for (int i=0;i<a->frozenCount;i++) if (a->frozenRects[i].x1-a->frozenRects[i].x0 != b->frozenRects[i].x1-b->frozenRects[i].x0 ||
        a->frozenRects[i].y1-a->frozenRects[i].y0 != b->frozenRects[i].y1-b->frozenRects[i].y0) return "capture-size";
    if (a->frozen && (a->frozenRect.x1-a->frozenRect.x0 != b->frozenRect.x1-b->frozenRect.x0 ||
                      a->frozenRect.y1-a->frozenRect.y0 != b->frozenRect.y1-b->frozenRect.y0)) return "capture-size";
    if (!a->valid || !b->valid) return "invalid-snapshot";
    if (!a->cleared || !b->cleared) return "missing-clear";
    if (!a->scene || a->scene != b->scene) return "scene-change";
    if (a->w != b->w || a->h != b->h) return "viewport-change";
    if (a->wide != b->wide || a->viewW != b->viewW || a->viewH != b->viewH ||
        a->originX != b->originX || a->originY != b->originY || a->aspect != b->aspect ||
        a->aspectW != b->aspectW || a->aspectH != b->aspectH || a->expanded != b->expanded) return "viewport-change";
    if (a->background != b->background) return "background-change";
    if (!a->drawCount || !b->drawCount) return "empty-snapshot";
    uint32_t geometry = 0, discontinuities = 0;
    const char* discontinuity = NULL;
    for (uint32_t i = 0; i < a->drawCount; i++) {
        const InterpDraw* x = &a->draws[i];
        if (!x->object) continue;
        const InterpDraw* y = findDraw(b, x);
        if (x->placement) {
            y = findAnchor(b, x->object);
            if (y && y->placement) {
                const char* reason = placementMismatch(x, y);
                if (reason) { discontinuities++; if (!discontinuity) discontinuity = reason; }
                geometry++;
            }
            continue;
        }
        if (!y) continue; /* visibility changes are local authored steps */
        geometry++;
        if (x->count != y->count) continue;
        for (uint32_t k = 0; k < x->count; k++) {
            const char* reason = continuous(&a->vertices[x->first + k], &b->vertices[y->first + k], a->w, a->h, (x->kind == PC_INTERP_TRANSITION || x->kind == PC_INTERP_WAVE), x->kind == PC_INTERP_TERRAIN);
            if (reason && (!strcmp(reason, "invalid-projection") || !strcmp(reason, "motion-bound"))) {
                discontinuities++; if (!discontinuity) discontinuity = reason; break;
            }
        }
    }
    if (!geometry) return "no-identified-geometry";
    /* One fast limb must not stop the rest of a scene. A cut or widespread
     * projection jump still uses the native picture immediately. */
    if (discontinuities * 2 >= geometry) return discontinuity;
    return NULL;
}
static const InterpDraw* partner(const InterpFrame* b, const InterpDraw* x) {
    if (!x->placement) return findDraw(b, x);
    const InterpDraw* y = findAnchor(b, x->object);
    return y && y->placement ? y : NULL;
}
const char* interpDrawMismatch(const InterpFrame* a, const InterpFrame* b, uint32_t index) {
    if (!a || !b || index >= a->drawCount) return "missing-face";
    const InterpDraw* x = &a->draws[index];
    if (!x->object) return NULL;
    const InterpDraw* y = partner(b, x);
    if (!y) return "missing-face";
    if (x->placement) return placementMismatch(x, y); /* old image/pose follows its safe actor anchor */
    /* Depth-cued terrain steps its palette as the camera moves: the face
     * still moves (with the earlier picture's palette), else it would tear
     * from its neighbours. */
    if (x->material != y->material && x->kind != PC_INTERP_TERRAIN) return "texture-change";
    if (x->count != y->count) return "topology-change";
    if (x->collapse != y->collapse) return "wave-edge-change";
    for (uint32_t k = 0; k < x->count; k++) {
        const char* reason = continuous(&a->vertices[x->first + k], &b->vertices[y->first + k], a->w, a->h, (x->kind == PC_INTERP_TRANSITION || x->kind == PC_INTERP_WAVE), x->kind == PC_INTERP_TERRAIN);
        if (reason) return reason;
    }
    if(x->collapse) for(uint32_t k=0;k<x->count;k+=3) {
        const HiVertex *p=a->vertices+x->first+k,*q=b->vertices+y->first+k;
        int top=0,bottom=0;
        for(int i=1;i<3;i++){if(p[i].v<p[top].v)top=i;if(p[i].v>p[bottom].v)bottom=i;}
        if((p[bottom].y-p[top].y)*(q[bottom].y-q[top].y)<0)return "winding-change";
    }
    if (!x->collapse) for (uint32_t k = 0; k < x->count; k += 3)
        if (!winding(&a->vertices[x->first + k], &b->vertices[y->first + k], x->kind == PC_INTERP_WAVE)) return "winding-change";
    return NULL;
}

int interpFramesCompatible(const InterpFrame* a, const InterpFrame* b) {
    return interpFramesMismatch(a, b) == NULL;
}

uint32_t interpFramesMotionKinds(const InterpFrame* a, const InterpFrame* b) {
    if (!interpFramesCompatible(a, b)) return 0;
    uint32_t kinds = 0;
    for (uint32_t i = 0; i < a->drawCount; i++) {
        const InterpDraw* x = &a->draws[i];
        if (!x->object) continue;
        const InterpDraw* y = partner(b, x);
        if (interpDrawMismatch(a, b, i)) continue;
        if (x->placement) {
            if (memcmp(x->anchor, y->anchor, sizeof(x->anchor)) && (unsigned)x->kind < 32) kinds |= 1u << x->kind;
            continue;
        }
        for (uint32_t k = 0; k < x->count; k++) {
            const HiVertex *p = &a->vertices[x->first + k], *q = &b->vertices[y->first + k];
            if (p->x != q->x || p->y != q->y || p->w != q->w ||
                (x->kind == PC_INTERP_TRANSITION && (p->r != q->r || p->g != q->g || p->b != q->b))) {
                if ((unsigned)x->kind < 32) kinds |= 1u << x->kind;
                break;
            }
        }
    }
    return kinds;
}
int interpFramesMove(const InterpFrame* a, const InterpFrame* b) { return interpFramesMotionKinds(a, b) != 0; }

int interpFrameVertices(const InterpFrame* a, const InterpFrame* b, double alpha, HiVertex* out, size_t cap) {
    if (!a || !out || cap < a->vertexCount || !isfinite(alpha) || alpha < 0 || alpha > 1 || !interpFramesCompatible(a, b)) return 0;
    memcpy(out, a->vertices, (size_t)a->vertexCount * sizeof(*out));
    for (uint32_t i = 0; i < a->drawCount; i++) {
        const InterpDraw* x = &a->draws[i];
        if (!x->object) continue;
        const InterpDraw* y = partner(b, x);
        if (interpDrawMismatch(a, b, i)) continue;
        if (x->placement) {
            float dx = (float)(alpha * (y->anchor[0] - x->anchor[0]));
            float dy = (float)(alpha * (y->anchor[1] - x->anchor[1]));
            float z = (float)(x->anchor[2] + alpha * (y->anchor[2] - x->anchor[2]));
            float scale = x->anchor[2] / z;
            for (uint32_t k = 0; k < x->count; k++) {
                HiVertex* v = &out[x->first + k];
                v->x = x->anchor[0] + dx + (v->x - x->anchor[0]) * scale;
                v->y = x->anchor[1] + dy + (v->y - x->anchor[1]) * scale;
            }
            continue;
        }
        for (uint32_t k = 0; k < x->count; k++) {
            HiVertex* v = &out[x->first + k];
            const HiVertex* next = &b->vertices[y->first + k];
            v->x += (float)(alpha * (next->x - v->x));
            v->y += (float)(alpha * (next->y - v->y));
            v->w += (float)(alpha * (next->w - v->w));
            if (x->kind == PC_INTERP_TRANSITION) {
                v->r += (float)(alpha * (next->r - v->r));
                v->g += (float)(alpha * (next->g - v->g));
                v->b += (float)(alpha * (next->b - v->b));
            }
        }
        if (x->collapse) for (uint32_t k=0;k<x->count;k+=3) {
            HiVertex* t=out+x->first+k;
            int left=0,right=0;
            for(int q=1;q<3;q++) { if(t[q].u<t[left].u)left=q; if(t[q].u>t[right].u)right=q; }
            if(t[right].x<t[left].x) for(int q=0;q<3;q++) if(t[q].u==t[right].u)t[q].x=t[left].x;
        }
    }
    return (int)a->vertexCount;
}

void interpTimelineReset(InterpTimeline* t) {
    *t = (InterpTimeline){0, 0, 0, -1, -1};
}
void interpTimelineActivate(InterpTimeline* t, uint64_t serial, int frame, double vblank) {
    if (serial == t->serial) return;
    double interval = vblank - t->activated;
    t->previous = t->current;
    if (!t->serial || interval < 1 || interval > 4.01 || !isfinite(interval)) t->previous = -1;
    t->interval = interval;
    t->activated = vblank;
    t->serial = serial;
    t->current = frame;
}
double interpTimelineAlpha(const InterpTimeline* t, double vblank) {
    if (t->previous < 0 || t->interval <= 0 || !isfinite(vblank)) return 1;
    double a = (vblank - t->activated) / t->interval;
    return a < 0 ? 0 : a > 1 ? 1 : a;
}

void interpPacerReset(InterpPacer* p, uint64_t now, int hz) {
    *p = (InterpPacer){now, 1, hz >= 30 && hz <= 240 ? hz : 0};
}
uint64_t interpPacerDeadline(const InterpPacer* p) {
    return p->hz ? p->anchor + (p->tick / (uint64_t)p->hz) * UINT64_C(1000000000) +
        (p->tick % (uint64_t)p->hz) * UINT64_C(1000000000) / (uint64_t)p->hz : UINT64_MAX;
}
void interpPacerAdvance(InterpPacer* p, uint64_t now) {
    if (!p->hz) return;
    if (now < p->anchor) { interpPacerReset(p, now, p->hz); return; }
    uint64_t elapsed = now - p->anchor;
    p->tick = (elapsed / UINT64_C(1000000000)) * (uint64_t)p->hz +
              (elapsed % UINT64_C(1000000000)) * (uint64_t)p->hz / UINT64_C(1000000000) + 1;
    /* Integer nanosecond deadlines round down; ensure it is in the future. */
    if (interpPacerDeadline(p) <= now) p->tick++;
}

int interpPrepareRate(int requested, int current, double delivered, int* adaptive) {
    /* Higher selections already offer multiple idle opportunities per
     * native interval. Keep their established scheduling unchanged. */
    if (requested != 60) { *adaptive = 0; return requested; }
    if (current < 60) current = 60;
    if (current > 120) current = 120;
    if (!isfinite(delivered) || delivered < 0) return current;
    if (delivered < requested*.98) {
        /* Two opportunities per native interval avoid repeatedly losing
         * the only slot to native drawing. Do not ramp through rates that
         * alias those same busy windows. */
        *adaptive = 1;
        return 120;
    }
    if (!*adaptive || (delivered >= 63.5 && delivered <= 75.0)) return current;
    /* A small delivery margin covers presentation boundaries and repeated
     * authored pictures. Reduce work only with clear spare capacity, so
     * measurement noise at the device ceiling cannot oscillate the clock.
     * Change gradually and keep the tested 120 Hz bound. */
    double wanted = delivered > 0 ? ceil(current*64.0/delivered) : current+15;
    int next = wanted > current+15 ? current+15 : wanted < current-15 ? current-15 : (int)wanted;
    if (next < 60) next = 60;
    if (next > 120) next = 120;
    return next;
}
