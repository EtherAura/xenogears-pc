#version 450
// One private invocation walks an authored 15-bit capture triangle in native
// raster/cache order. It never binds the emulated VRAM or texture cache for
// writing. Clean, non-overlapping triangles are subsequently drawn at full
// resolution by hires.frag; cached/feedback results follow the native
// renderer's replicated 15-bit resolve.
layout(set=0,binding=1,std430) readonly buffer Words { uint words[]; };
layout(set=0,binding=3,r32ui) uniform coherent uimage2D target;
layout(set=0,binding=4,r32ui) uniform coherent uimage2D samples;
layout(set=0,binding=6,r32ui) uniform coherent uimage2D cache;
layout(set=0,binding=7,r32ui) uniform coherent uimage2D texels;
layout(push_constant) uniform Capture {
    int n, action, first, seed;
    ivec2 source;
    uint mode, window;
    int feedback, frozen;
    ivec2 readOffset;
} pc;
const int dither[16] = int[16](-4,0,-3,1,2,-2,3,-1,-3,1,-4,0,3,-1,2,-2);
int walkFirst;
ivec2 walkSource;
uint walkMode, walkWindow;
uint nativeWord(uint w) {
    return (w>>3&31u) | (w>>11&31u)<<5 | (w>>19&31u)<<10 | (w>>24&1u)<<15;
}
uint rgbWord(uint w) {
    uvec3 c = uvec3(w&31u,w>>5&31u,w>>10&31u);
    c = c<<3 | c>>2;
    return c.r | c.g<<8 | c.b<<16 | (w>>15&1u)<<24;
}
uint at(int i) { return words[walkFirst+i]; }
int rowAt(int i) {
    int first = int(at(0)), last = first+int(at(1))-1, start = max(first,int(at(7)));
    int down = max(0,last-start+1);
    return i < down ? start+i : min(start,last+1)-1-(i-down);
}
ivec2 texel(int r, int k) {
    ivec2 uv = ivec2(int(at(r+2)+uint(k)*at(2))>>16,int(at(r+3)+uint(k)*at(3))>>16)&255;
    ivec2 mask = ivec2(walkWindow&31u,walkWindow>>5&31u)*8;
    ivec2 offset = ivec2(walkWindow>>10&31u,walkWindow>>15&31u)*8;
    return ((uv&~mask)|(offset&mask))+walkSource;
}
uint current(ivec2 a) {
    return nativeWord(pc.frozen != 0 ? imageLoad(samples,(a+pc.readOffset)*pc.n).r : imageLoad(target,a*pc.n).r);
}
uint sampleCache(ivec2 a, inout bool stale) {
    a &= ivec2(1023,511);
    int slot = (a.y&31)*8+(a.x>>2&7);
    uint tag = uint(a.y*256+(a.x>>2));
    uint c;
    if (imageLoad(cache,ivec2(0,slot)).r != tag) {
        imageStore(cache,ivec2(0,slot),uvec4(tag));
        c = 0u;
        for (int k = 0; k < 4; k++) {
            uint v = current(ivec2((a.x&~3)+k,a.y));
            imageStore(cache,ivec2(k+1,slot),uvec4(v));
            if (k == (a.x&3)) c = v;
        }
        // The cache walk is one invocation: its own stores precede its reads.
        // vk.c supplies image barriers between this walk and other draws.
    } else c = imageLoad(cache,ivec2((a.x&3)+1,slot)).r;
    stale = stale || c != current(a);
    return c;
}
void shade(int x, int y, int r, uint t) {
    if (t == 0u) return;
    uint dst = nativeWord(imageLoad(target,ivec2(x,y)*pc.n).r);
    if ((walkMode&128u) != 0u && (dst&32768u) != 0u) return;
    ivec3 color = ivec3(int(at(r+4)+uint(x-int(at(r)))*at(4))>>16,
                      int(at(r+5)+uint(x-int(at(r)))*at(5))>>16,
                      int(at(r+6)+uint(x-int(at(r)))*at(6))>>16);
    ivec3 f = ivec3(t&31u,t>>5&31u,t>>10&31u);
    f = (f*((walkMode&2u) != 0u ? ivec3(128) : color))>>4;
    if ((walkMode&32u) != 0u) f += dither[(y&3)*4+(x&3)];
    f = clamp(f>>3,0,31);
    if ((walkMode&4u) != 0u && (t&32768u) != 0u) {
        ivec3 b = ivec3(dst&31u,dst>>5&31u,dst>>10&31u);
        uint mode = walkMode>>3&3u;
        f = mode == 0u ? (b+f)>>1 : mode == 1u ? b+f : mode == 2u ? b-f : b+(f>>2);
        f = clamp(f,0,31);
    }
    uint w = uint(f.r) | uint(f.g)<<5 | uint(f.b)<<10 | (t&32768u) | ((walkMode&64u) != 0u ? 32768u : 0u);
    for (int sy = 0; sy < pc.n; sy++) for (int sx = 0; sx < pc.n; sx++)
        imageStore(target,ivec2(x,y)*pc.n+ivec2(sx,sy),uvec4(rgbWord(w)));
}
void main() {
    if (pc.action == 2) { imageStore(cache,ivec2(0,256),uvec4(0u)); return; }
    if (pc.action == 0) {
        for (int i = 0; i < 256; i++) for (int k = 0; k < 5; k++)
            imageStore(cache,ivec2(k,i),uvec4(pc.seed >= 0 ? words[pc.seed+i*5+k] : k == 0 ? 0xffffffffu : 0u));
        imageStore(cache,ivec2(0,256),uvec4(0u)); return;
    }
    if (pc.action == 3) {
        // At exact 1x every triangle resolves to native pixels. Keep their
        // complete cache/raster order in one invocation, avoiding a device
        // barrier per triangle. Copies and resets remain separate commands.
        for (int i=0;i<pc.seed;i++) {
            int entry=pc.first+i*5;
            walkFirst=int(words[entry]);
            if (walkFirst<0) continue;
            walkMode=words[entry+1];walkWindow=words[entry+2];
            uint source=words[entry+3];walkSource=ivec2(int(source<<16)>>16,int(source)>>16);
            bool feedback=words[entry+4]!=0u;
            bool stale=false;
            for (int row=0;row<int(at(1));row++) {
                int y=rowAt(row), r=8+(y-int(at(0)))*8;
                for (int x=int(at(r));x<=int(at(r+1));x++) {
                    uint t=sampleCache(texel(r,x-int(at(r))),stale);
                    if (feedback) shade(x,y,r,t);
                    else imageStore(texels,ivec2(x,y),uvec4(t));
                }
            }
            if (!feedback) for (int row=0;row<int(at(1));row++) {
                int y=rowAt(row), r=8+(y-int(at(0)))*8;
                for (int x=int(at(r));x<=int(at(r+1));x++) shade(x,y,r,imageLoad(texels,ivec2(x,y)).r);
            }
        }
        return;
    }
    walkFirst=pc.first;walkSource=pc.source;walkMode=pc.mode;walkWindow=pc.window;
    bool stale = false;
    for (int i = 0; i < int(at(1)); i++) {
        int y = rowAt(i), r = 8+(y-int(at(0)))*8;
        for (int x = int(at(r)); x <= int(at(r+1)); x++) {
            uint t = sampleCache(texel(r,x-int(at(r))),stale);
            imageStore(texels,ivec2(x,y),uvec4(t));
            if (pc.feedback != 0) shade(x,y,r,t);
        }
    }
    memoryBarrierImage();
    if (stale && pc.feedback == 0) for (int i = 0; i < int(at(1)); i++) {
        int y = rowAt(i), r = 8+(y-int(at(0)))*8;
        for (int x = int(at(r)); x <= int(at(r+1)); x++) shade(x,y,r,imageLoad(texels,ivec2(x,y)).r);
    }
    if (pc.n>1 && (stale || pc.feedback != 0)) {
        int left=0x7fffffff, right=-1;
        for (int i=0;i<int(at(1));i++) {
            int r=8+i*8;
            if(int(at(r))<=int(at(r+1))) { left=min(left,int(at(r))); right=max(right,int(at(r+1))); }
        }
        for(int y=int(at(0));y<int(at(0)+at(1));y++)for(int x=left;x<=right;x++) {
            uint w=rgbWord(nativeWord(imageLoad(target,ivec2(x,y)*pc.n).r));
            for(int sy=0;sy<pc.n;sy++)for(int sx=0;sx<pc.n;sx++)imageStore(target,ivec2(x,y)*pc.n+ivec2(sx,sy),uvec4(w));
        }
    }
    imageStore(cache,ivec2(0,256),uvec4(stale || pc.feedback != 0 ? 1u : 0u));
}
