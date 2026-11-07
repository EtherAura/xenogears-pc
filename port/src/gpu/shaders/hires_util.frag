#version 450

// The hi-res copy's transfers (port/src/gpu/vk.c, hires.c): one pass over a
// rectangle of the copy (the scissor), each hi-res pixel p from its 1x pixel
// p / N or from another part of the copy.
//
//   REPLICATE  a 1x picture (VRAM, the wide target, or the scratch image
//              holding an upload) as N x N blocks
//   LOAD       an upload whose 1x colours (bits 0-14) equal a reference's
//              (VRAM's own pixel there, or a stashed StoreImage) keeps that
//              reference's hi-res pixels and takes only the mask bit from
//              the upload; other pixels are replicated (CPU round trips that
//              only set bit 15 stay hi-res)
//   MASK       the mask bit set on every pixel (a capture made semi-opaque)
//   BLIT       MoveImage with the mask bits: from another part of the copy
//              (or its scratch), checking and setting the mask per pixel

const int OP_REPLICATE = 0;
const int OP_LOAD = 1;
const int OP_MASK = 2;
const int OP_BLIT = 3;
const int OP_BLUR = 4;

const uint MASK = 0x1000000u;

layout(set = 0, binding = 0, r16ui) uniform readonly uimage2D vram;
layout(set = 0, binding = 2, r16ui) uniform readonly uimage2D wideImg;
layout(set = 0, binding = 3, r32ui) uniform uimage2D hiVram;
layout(set = 0, binding = 4, r32ui) uniform uimage2D hiWide;
layout(set = 0, binding = 5, r16ui) uniform readonly uimage2D scratch;
layout(set = 0, binding = 6, r32ui) uniform readonly uimage2D hiScratch;
layout(set = 0, binding = 7, r32ui) uniform readonly uimage2D hiStash;

layout(set = 1, binding = 0, r32ui) uniform readonly uimage2D hiEffect;

layout(push_constant) uniform Util {
    int n;
    int op;
    int target;  // 0: the hi-res VRAM, 1: the hi-res wide target
    int srcKind; // REPLICATE: 0 VRAM, 1 wide target, 2 scratch; BLIT: 0 hi-res VRAM, 1 hi-res scratch
    ivec2 dst0;  // the rectangle's 1x origin in the target
    ivec2 src0;  // REPLICATE/LOAD: the source's 1x origin; BLIT: hi-res origin
    ivec2 ref0;  // LOAD: the reference's origin in the scratch image (refKind 1)
    int refKind; // LOAD: 0 VRAM's pixel at the destination, 1 the scratch image
    int hiKind;  // LOAD: 0 the copy's own pixel, 1 the stash
    ivec2 hi0;   // LOAD from the stash: its hi-res origin
    int flags;   // BLIT: 1 set the mask bit, 2 check it
} pc;

uint word15(uint c) {
    ivec3 c5 = ivec3(int(c & 31u), int((c >> 5) & 31u), int((c >> 10) & 31u));
    ivec3 c8 = (c5 << 3) | (c5 >> 2);
    return uint(c8.r) | uint(c8.g) << 8 | uint(c8.b) << 16 | ((c & 0x8000u) != 0u ? MASK : 0u);
}

uint load1x(int kind, ivec2 a) {
    if (kind == 0) {
        return imageLoad(vram, ivec2(a.x & 1023, a.y & 511)).r;
    } else if (kind == 1) {
        return imageLoad(wideImg, ivec2(a.x & 2047, a.y & 2047)).r;
    }
    return imageLoad(scratch, ivec2(a.x & 1023, a.y & 511)).r;
}

uint loadTarget(ivec2 p) {
    return pc.target == 0 ? imageLoad(hiVram, p).r : imageLoad(hiWide, p).r;
}

void store(ivec2 p, uint v) {
    if (pc.target == 0) {
        imageStore(hiVram, p, uvec4(v));
    } else {
        imageStore(hiWide, p, uvec4(v));
    }
}

// Same native-space 2x2 kernel for each subpixel phase. CPU pixels are
// traversed linearly: the right neighbour of a row's last pixel wraps to
// the next row. The narrow routine leaves its final candidate untouched;
// the existing wide routine also blurs that pixel (its padding is zero).
uint effectPixel(ivec2 p) {
    int w=pc.ref0.x*pc.n, h=pc.ref0.y*pc.n;
    if (p.x>=w) { p.x-=w; p.y+=pc.n; }
    if (p.y>=h) return 0u;
    return imageLoad(hiEffect,p+pc.src0).r;
}
uvec3 rgb5(uint p) { return uvec3(p,p>>8,p>>16) / 8u & 31u; }

void main() {
    ivec2 p = ivec2(gl_FragCoord.xy);
    ivec2 q = p / pc.n;
    if (pc.op == OP_REPLICATE) {
        store(p, word15(load1x(pc.srcKind, q - pc.dst0 + pc.src0)));
    } else if (pc.op == OP_LOAD) {
        uint now = load1x(2, q - pc.dst0 + pc.src0);
        uint ref = pc.refKind == 0 ? load1x(0, q) : load1x(2, q - pc.dst0 + pc.ref0);
        uint v;
        if ((now & 0x7FFFu) == (ref & 0x7FFFu)) {
            uint hi = pc.hiKind == 0 ? loadTarget(p) : imageLoad(hiStash, p - pc.dst0 * pc.n + pc.hi0).r;
            v = (hi & ~MASK) | ((now & 0x8000u) != 0u ? MASK : 0u);
        } else {
            v = word15(now);
        }
        store(p, v);
    } else if (pc.op == OP_BLUR) {
        ivec2 a=p-pc.dst0*pc.n;
        int pixel=(a.y/pc.n)*pc.ref0.x+a.x/pc.n;
        int end=pc.ref0.x*(pc.ref0.y-1)-(pc.target==0 ? 1 : 0);
        uint v=effectPixel(a);
        if (pc.target==1 && pc.flags!=0 && a.x>=pc.hi0.x && a.x<pc.hi0.x+320*pc.n) {
            // XG_WIDE_NOSHIFT retains the independently blurred 4:3 centre.
            v=imageLoad(hiEffect,ivec2(a.x-pc.hi0.x,a.y)).r;
        } else if (pixel<end) {
            uvec3 c=(rgb5(v)+rgb5(effectPixel(a+ivec2(pc.n,0)))+
                     rgb5(effectPixel(a+ivec2(0,pc.n)))+rgb5(effectPixel(a+ivec2(pc.n))))/4u;
            c=(c<<3)|(c>>2);
            v=c.r|(c.g<<8)|(c.b<<16); // ORGB clears the mask
        }
        store(p,v);
    } else if (pc.op == OP_MASK) {
        store(p, loadTarget(p) | MASK);
    } else {
        ivec2 s = p - pc.dst0 * pc.n + pc.src0;
        uint v = pc.srcKind == 0 ? imageLoad(hiVram, s).r : imageLoad(hiScratch, s).r;
        uint d = imageLoad(hiVram, p).r;
        if ((pc.flags & 2) == 0 || (d & MASK) == 0u) {
            imageStore(hiVram, p, uvec4(v | ((pc.flags & 1) != 0 ? MASK : 0u)));
        }
    }
}
