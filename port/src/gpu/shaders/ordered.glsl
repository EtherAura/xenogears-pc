// Bounded per-pixel operation lists. Rasterization and foreground arithmetic
// are the existing shaders'; only destination-dependent work is deferred.
#ifndef ORDERED_SET
#define ORDERED_SET 2
#endif
const uint ORD_QUANT5 = 16u;
const uint ORD_MASK = 32u;
const uint ORD_CHECK = 64u;
const uint ORD_SURFACE = 128u;
const uint ORD_FRINGE = 256u;
const uint ORD_CLEAR = 512u;
const uint ORD_QUARTER5 = 1024u;
// Bits 16-23: smooth text's coverage of the pixel in 255ths (0: whole);
// the resolved colour is blended over the destination by it.
const uint ORD_COVER_SHIFT = 16u;
layout(set=ORDERED_SET,binding=0,std430) coherent buffer Operations {
    uint firstOrder;
    uint overflow;
    ivec2 origin;
    ivec2 grid;
    uint capacity;
    uint targetKind;
    uvec4 rect[64]; // triangle: x, y, width | height <<16, first node
    uvec4 operation[]; // next, primitive order, foreground, operation flags
} ordered;
layout(set=ORDERED_SET,binding=1,std430) coherent buffer Heads { uint head[]; };
#ifdef ORDERED_COLLECT
void orderedAppend(ivec2 pixel,uint front,uint flags) {
    ivec2 q=pixel-ordered.origin;
    if(any(lessThan(q,ivec2(0)))||any(greaterThanEqual(q,ordered.grid)))return;
    uint triangle=vOrder-ordered.firstOrder;
    if(triangle>=64u){atomicAdd(ordered.overflow,1u);return;}
    uvec4 r=ordered.rect[triangle];
    ivec2 local=pixel-ivec2(r.xy);
    // Each triangle has at most one invocation per pixel. Its conservative
    // rectangle owns a disjoint node range; only the pixel head is atomic.
    uint width=r.z&65535u,height=r.z>>16;
    uint slot=r.w+uint(local.y)*width+uint(local.x);
    if(any(lessThan(local,ivec2(0)))||uint(local.x)>=width||uint(local.y)>=height||slot>=ordered.capacity){atomicAdd(ordered.overflow,1u);return;}
    uint at=uint(q.y*ordered.grid.x+q.x);
    uint next=atomicExchange(head[at],slot);
    ordered.operation[slot]=uvec4(next,vOrder,front,flags);
}
uint orderedFlags(uint mode,bool semi,uint semiMode,bool mask) {
    return (semi?semiMode+1u:0u)|(mask?ORD_MASK:0u)|
           ((mode&128u)!=0u?ORD_CHECK:0u)|
           ((mode&0x800u)!=0u?ORD_SURFACE:0u)|
           ((mode&0x1000u)!=0u?ORD_FRINGE:0u)|
           ((mode&0x100u)!=0u?ORD_CLEAR:0u);
}
#endif
