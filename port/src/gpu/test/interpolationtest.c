#include "gpu/interpolation_core.h"
#include "pc_interpolation.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static int checks;
#include "test/check.h"

static InterpFrame a, b;
static HiVertex triangle[3], output[12];
static void frame(InterpFrame* f, int dx) {
    interpFrameClear(f); f->scene = 1; f->w = 320; f->h = 224; f->cleared = 1;
    f->wide = f->originX = f->originY = f->aspect = f->aspectW = f->aspectH = f->expanded = 0;
    f->viewX = f->viewY = 0; f->viewW = 320; f->viewH = 224;
    for (int i = 0; i < 3; i++) {
        triangle[i] = (HiVertex){0}; triangle[i].x = 40 + dx + (i == 1 ? 20 : 0);
        triangle[i].y = 40 + (i == 2 ? 20 : 0); triangle[i].w = 1;
        triangle[i].clipMax = 319u | 223u << 16;
        triangle[i].r = 248;
    }
    interpFrameDraw(f, triangle, 3, 123, 17, 42);
}
static int diagnostics(void) {
    uint8_t rgba[1003*4];
    uint8_t everyWord[65537*4];
    for(unsigned i=0;i<65536;i++) {
        for(int k=0;k<3;k++){unsigned c=i>>(5*k)&31;everyWord[i*4+k]=(uint8_t)(c<<3|c>>2);}
        everyWord[i*4+3]=i?(i&32768?128:255):0;
    }
    memset(everyWord+65536*4,0,3);everyWord[65536*4+3]=255;
    interpFrameClear(&a);uint32_t everyOffset,everyFormat;
    CHECK(interpFrameSample(&a,everyWord,65537,&everyOffset,&everyFormat));
    CHECK((everyFormat&INTERP_SAMPLE_OPAQUE_BLACK)&&a.pixelCount==34818);
    CHECK(interpSampleEqual(&a,everyOffset,everyFormat,everyWord,65537));
    /* More than 256 native colours, odd count, transparency and STP retain
     * every RGBA byte in half the storage. Non-native RGBA stays exact. */
    for(int i=0;i<1003;i++) {
        unsigned c=i%10?i+1:0;if(i%2&&c)c|=32768;
        for(int k=0;k<3;k++){unsigned v=c>>(k*5)&31;rgba[i*4+k]=(uint8_t)(v<<3|v>>2);}
        rgba[i*4+3]=c?(c&32768?128:255):0;
    }
    interpFrameClear(&a);uint32_t nativeOffset,nativeFormat;
    CHECK(interpFrameSample(&a,rgba,1003,&nativeOffset,&nativeFormat));
    CHECK((nativeFormat&INTERP_SAMPLE_NATIVE_WORD)&&a.pixelCount==502);
    CHECK(interpSampleEqual(&a,nativeOffset,nativeFormat,rgba,1003));
    rgba[3]=255; /* distinguish opaque black from PS1's transparent zero */
    interpFrameClear(&a);CHECK(interpFrameSample(&a,rgba,1003,&nativeOffset,&nativeFormat));
    CHECK((nativeFormat&INTERP_SAMPLE_OPAQUE_BLACK)&&a.pixelCount==534&&interpSampleEqual(&a,nativeOffset,nativeFormat,rgba,1003));
    rgba[0]=1; /* non-native channel values remain full RGBA */
    interpFrameClear(&a);CHECK(interpFrameSample(&a,rgba,1003,&nativeOffset,&nativeFormat));
    CHECK(!nativeFormat&&a.pixelCount==1003&&interpSampleEqual(&a,nativeOffset,nativeFormat,rgba,1003));
    frame(&a, 0); frame(&b, 10);
    CHECK(interpFramesMismatch(&a, &b) == NULL);
    CHECK(!strcmp(interpFramesMismatch(NULL, &b), "missing-endpoint"));
    b.scene++; CHECK(!strcmp(interpFramesMismatch(&a, &b), "scene-change")); b.scene--;
    b.w++; CHECK(!strcmp(interpFramesMismatch(&a, &b), "viewport-change")); b.w--;
    b.viewW++; CHECK(!strcmp(interpFramesMismatch(&a, &b), "viewport-change")); b.viewW--;
    b.viewH++; CHECK(!strcmp(interpFramesMismatch(&a, &b), "viewport-change")); b.viewH--;
    b.originY++; CHECK(!strcmp(interpFramesMismatch(&a, &b), "viewport-change")); b.originY--;
    b.wide++; CHECK(!strcmp(interpFramesMismatch(&a, &b), "viewport-change")); b.wide--;
    b.aspectW++; CHECK(!strcmp(interpFramesMismatch(&a, &b), "viewport-change")); b.aspectW--;
    b.expanded++; CHECK(!strcmp(interpFramesMismatch(&a, &b), "viewport-change")); b.expanded--;
    b.viewY = 258; CHECK(interpFramesCompatible(&a, &b)); b.viewY = 0; /* another framebuffer slot */
    b.background++; CHECK(!strcmp(interpFramesMismatch(&a, &b), "background-change")); b.background--;
    b.draws[0].material++; CHECK(!strcmp(interpDrawMismatch(&a, &b, 0), "texture-change")); b.draws[0].material--;
    b.draws[0].count = 6; CHECK(!strcmp(interpDrawMismatch(&a, &b, 0), "topology-change")); b.draws[0].count = 3;
    b.vertices[0].x = 100; CHECK(!strcmp(interpFramesMismatch(&a, &b), "motion-bound")); b.vertices[0].x = 50;
    b.vertices[0].w = 0; CHECK(!strcmp(interpFramesMismatch(&a, &b), "invalid-projection")); b.vertices[0].w = 1;
    b.vertices[0].u++; CHECK(!strcmp(interpDrawMismatch(&a, &b, 0), "draw-attributes")); b.vertices[0].u--;
    /* Depth-cued terrain: a palette step (material, the texture word's top
     * half) keeps the face moving; its texture page or UVs still hold it. */
    a.draws[0].kind = b.draws[0].kind = PC_INTERP_TERRAIN;
    b.draws[0].material++; CHECK(interpDrawMismatch(&a, &b, 0) == NULL); b.draws[0].material--;
    for (int i = 0; i < 3; i++) b.vertices[i].tex += 0x10000u;
    CHECK(interpDrawMismatch(&a, &b, 0) == NULL);
    for (int i = 0; i < 3; i++) b.vertices[i].tex -= 0x10000u;
    b.vertices[0].tex ^= 1u; CHECK(!strcmp(interpDrawMismatch(&a, &b, 0), "draw-attributes")); b.vertices[0].tex ^= 1u;
    b.vertices[0].u++; CHECK(!strcmp(interpDrawMismatch(&a, &b, 0), "draw-attributes")); b.vertices[0].u--;
    a.draws[0].kind = b.draws[0].kind = PC_INTERP_MODEL;
    frame(&a, -39); frame(&b, -41);
    CHECK(interpFramesCompatible(&a, &b));
    frame(&a, 0); frame(&b, 0);
    for (int i = 0; i < 3; i++) {
        b.vertices[i].x = 100 - a.vertices[i].x;
        b.vertices[i].y = 100 - a.vertices[i].y;
    }
    CHECK(!strcmp(interpDrawMismatch(&a, &b, 0), "winding-change"));
    CHECK(interpFrameVertices(&a, &b, 0.5, output, 6) == 3 && output[0].x == a.vertices[0].x);
    interpFrameReject(&a, "texture-feedback"); interpFrameReject(&a, "later-failure");
    CHECK(!a.valid && !strcmp(a.rejection, "texture-feedback"));
    interpFrameClear(&a); CHECK(a.valid && !a.rejection);
    frame(&a, 0); frame(&b, 0);
    a.frozen = b.frozen = 1;
    a.frozenRect = b.frozenRect = (VRect){704,256,1023,511};
    a.draws[0].kind = b.draws[0].kind = PC_INTERP_TRANSITION;
    for (int i = 0; i < 3; i++) { a.vertices[i].r = 100; b.vertices[i].r = 112; }
    CHECK(interpFramesMotionKinds(&a, &b) == (1u << PC_INTERP_TRANSITION));
    CHECK(interpFrameVertices(&a, &b, .5, output, 12) == 3 && output[0].r == 106);
    b.frozenRect.x1++; CHECK(!strcmp(interpFramesMismatch(&a,&b), "capture-size")); b.frozenRect.x1--;
    b.frozen = 0; CHECK(!strcmp(interpFramesMismatch(&a,&b), "capture-change")); b.frozen = 1;
    a.draws[0].kind = b.draws[0].kind = PC_INTERP_MODEL;
    CHECK(!interpFramesMotionKinds(&a, &b));
    CHECK(interpFrameVertices(&a, &b, .5, output, 12) == 3 && output[0].r == 100);
    return 0;
}
static int feedback(void) {
    HiVertex v[3]={{0}};int32_t rows[8+8*48]={176,48,69904,0,0,0,0,176};uint32_t steps[15*48];
    const float x[3]={9,24,9},y[3]={176,176,224},u[3]={0,16,0},vv[3]={176,176,192};
    for(int k=0;k<3;k++) {
        v[k].x=x[k];v[k].y=y[k];v[k].u=u[k];v[k].v=vv[k];v[k].sample[0]=12;
        v[k].uvMin=176u<<16;v[k].uvMax=15u|192u<<16;
    }
    for(int i=0;i<48;i++) {
        int32_t* r=rows+8+i*8;r[0]=9;r[1]=(24*48-15*i+47)/48-1;
        r[2]=32768;r[3]=176*65536+32768+21840*i;
    }
    int overlap=0;VRect box={9,176,23,223};
    CHECK(interpCaptureSteps(v,rows,box,steps,1,&overlap)==8&&overlap);
    CHECK(steps[11*15+10]==0&&steps[4*15+13]==0);
    /* At 2x, native block (19,187) also samples block (22,180).
     * The old 1x-only graph put both in step0, although the latter must
     * finish first. This is the real 32:9 wave regression. */
    CHECK(interpCaptureSteps(v,rows,box,steps,2,&overlap)>=8&&overlap);
    CHECK(steps[11*15+10]>steps[4*15+13]);
    CHECK(interpCaptureSteps(v,rows,box,steps,4,&overlap)>=8&&overlap);
    CHECK(steps[11*15+10]>steps[4*15+13]);
    HiVertex sample={.uvMax=31u|31u<<16,.sample={16u|32u<<16,0,0,0},.texWindow=1u|3u<<5|1u<<10|2u<<15};
    VRect rect;CHECK(interpCaptureRect(&sample,&rect));CHECK(rect.x0==24&&rect.x1==47&&rect.y0==48&&rect.y1==55);
    sample.sample[0]=sample.texWindow=0;sample.uvMin=254;sample.uvMax=258;
    CHECK(interpCaptureRect(&sample,&rect));CHECK(rect.x0==0&&rect.x1==255);
    frame(&a,0);frame(&b,0);
    a.draws[0].kind=b.draws[0].kind=PC_INTERP_WAVE;a.draws[0].collapse=b.draws[0].collapse=1;
    for(int k=0;k<3;k++) {a.vertices[k].u=b.vertices[k].u=k==1?16:0;a.vertices[k].v=b.vertices[k].v=k==2?16:0;}
    b.vertices[0].x=b.vertices[2].x=60;b.vertices[1].x=50;
    CHECK(!interpDrawMismatch(&a,&b,0));
    CHECK(interpFrameVertices(&a,&b,.5,output,12)==3&&output[0].x==50&&output[1].x==55);
    CHECK(interpFrameVertices(&a,&b,.75,output,12)==3&&output[0].x==55&&output[1].x==55&&output[2].x==55);
    b.vertices[0].y=b.vertices[1].y=60;b.vertices[2].y=40;
    CHECK(!strcmp(interpDrawMismatch(&a,&b,0),"winding-change"));
    return 0;
}

int main(void) {
    CHECK(!diagnostics());
    CHECK(!feedback());
    /* Odd-length packed samples, all palette sizes and the 257-colour
     * RGBA fallback preserve transparent/STP/opaque bytes exactly. */
    uint8_t rgba[1003 * 4];
    const int colorCounts[] = {1, 2, 16, 256, 257};
    for (int c = 0; c < 5; c++) {
        int colors = colorCounts[c];
        for (int i = 0; i < 1003; i++) {
            int k = i % colors;
            rgba[i * 4] = (uint8_t)k; rgba[i * 4 + 1] = (uint8_t)(k >> 8);
            rgba[i * 4 + 2] = 31; rgba[i * 4 + 3] = k % 3 == 0 ? 0 : k % 3 == 1 ? 128 : 255;
        }
        interpFrameClear(&a);
        uint32_t offset, format;
        CHECK(interpFrameSample(&a, rgba, 1003, &offset, &format) && offset == 0);
        CHECK(interpSampleEqual(&a, offset, format, rgba, 1003));
        CHECK(a.pixelCount == (colors <= 256 ? (1003 + 3) / 4 + colors : 1003));
        CHECK(!!(format & INTERP_SAMPLE_PALETTE) == (colors <= 256));
        if (colors <= 256) CHECK(((a.pixels[colors + 2] >> 16) & 255) == (unsigned)(10 % colors));
        rgba[1002 * 4] ^= 1;
        CHECK(!interpSampleEqual(&a, offset, format, rgba, 1003));
    }
    frame(&a, 0); frame(&b, 10);
    CHECK(interpFramesCompatible(&a, &b));
    CHECK(interpFramesMove(&a, &b));
    CHECK(interpFramesMotionKinds(&a, &b) == 1);
    a.draws[0].kind = 1; CHECK(interpFramesMotionKinds(&a, &b) == 2); a.draws[0].kind = 0;
    frame(&b, 0); CHECK(!interpFramesMove(&a, &b)); frame(&b, 10);
    CHECK(interpFrameVertices(&a, &b, 0, output, 6) == 3 && output[0].x == 40);
    CHECK(interpFrameVertices(&a, &b, 0.5, output, 6) == 3 && output[0].x == 45);
    CHECK(interpFrameVertices(&a, &b, 1, output, 6) == 3 && output[0].x == 50);
    CHECK(a.vertices[0].x == 40 && b.vertices[0].x == 50);
    b.vertices[0].r = 0;
    CHECK(interpFrameVertices(&a, &b, 0.5, output, 6) == 3 && output[0].r == 248);
    b.vertices[0].r = 248;
    CHECK(!interpFrameVertices(NULL, &b, 0.5, output, 6));
    CHECK(!interpFrameVertices(&a, &b, 0.5, output, 2));
    CHECK(!interpFrameVertices(&a, &b, NAN, output, 6));
    CHECK(!interpFrameVertices(&a, &b, -0.1, output, 6));
    CHECK(!interpFrameVertices(&a, &b, 1.1, output, 6));
    b.scene++; CHECK(!interpFramesCompatible(&a, &b)); b.scene--;
    b.draws[0].object++; CHECK(!interpFramesCompatible(&a, &b)); b.draws[0].object--;
    b.draws[0].face++; CHECK(!interpFramesCompatible(&a, &b)); b.draws[0].face--;
    b.draws[0].material++; CHECK(interpFramesCompatible(&a, &b) && interpDrawMismatch(&a, &b, 0)); b.draws[0].material--;
    b.vertices[0].u++; CHECK(interpFramesCompatible(&a, &b) && interpDrawMismatch(&a, &b, 0)); b.vertices[0].u--;
    b.vertices[0].clipMin++; CHECK(interpFramesCompatible(&a, &b) && interpDrawMismatch(&a, &b, 0)); b.vertices[0].clipMin--;
    b.vertices[0].x = 100; CHECK(!interpFramesCompatible(&a, &b)); b.vertices[0].x = 50;
    b.vertices[0].x = NAN; CHECK(!interpFramesCompatible(&a, &b)); b.vertices[0].x = 50;
    b.vertices[0].x = -1; CHECK(!interpFramesCompatible(&a, &b)); b.vertices[0].x = 50; /* motion bound */
    b.vertices[0].w = 0; CHECK(!interpFramesCompatible(&a, &b)); b.vertices[0].w = 1;
    b.valid = 0; CHECK(!interpFramesCompatible(&a, &b)); b.valid = 1;
    b.cleared = 0; CHECK(!interpFramesCompatible(&a, &b)); b.cleared = 1;
    b.w++; CHECK(!interpFramesCompatible(&a, &b)); b.w--;
    b.background++; CHECK(!interpFramesCompatible(&a, &b)); b.background--;
    /* An isolated projected jump holds that face. A majority jumping still
     * identifies a scene-wide discontinuity and falls back immediately. */
    frame(&a, 0); frame(&b, 10);
    for (int i = 0; i < 2; i++) {
        CHECK(interpFrameDraw(&a, a.vertices, 3, 124 + i, 17, 42));
        CHECK(interpFrameDraw(&b, b.vertices, 3, 124 + i, 17, 42));
    }
    b.vertices[0].x = 140;
    CHECK(interpFramesCompatible(&a, &b));
    CHECK(!strcmp(interpDrawMismatch(&a, &b, 0), "motion-bound"));
    CHECK(interpFrameVertices(&a, &b, 0.5, output, 12) == 9);
    CHECK(output[0].x == 40 && output[3].x == 45 && output[6].x == 45);
    b.vertices[3].x = 140;
    CHECK(!strcmp(interpFramesMismatch(&a, &b), "motion-bound"));
    b.vertices[0].x = b.vertices[3].x = 50;
    interpFramePlacement(&a, 40, 40, 100); interpFramePlacement(&b, 50, 40, 100);
    b.draws[2].anchor[2] = 0;
    CHECK(interpFramesCompatible(&a, &b));
    CHECK(!strcmp(interpDrawMismatch(&a, &b, 2), "invalid-projection"));
    CHECK(interpFrameVertices(&a, &b, 0.5, output, 12) == 9);
    CHECK(output[0].x == 45 && output[3].x == 45 && output[6].x == 40);
    /* Matching endpoint winding alone is insufficient: these triangles
     * rotate 180 degrees and collapse halfway through the interval. */
    frame(&a, 0); frame(&b, 0);
    for (int i = 0; i < 3; i++) {
        b.vertices[i].x = 100 - a.vertices[i].x;
        b.vertices[i].y = 100 - a.vertices[i].y;
    }
    CHECK(interpFramesCompatible(&a, &b) && interpDrawMismatch(&a, &b, 0));
    frame(&b, 10);
    CHECK(!interpFrameDraw(&b, triangle, 3, 123, 17, 42) && !b.valid);
    frame(&b, 10);
    /* PS1 OT sorting can reorder stable faces between native frames.
     * Match identities while retaining the earlier authored draw order. */
    CHECK(interpFrameDraw(&a, triangle, 3, 456, 23, 91));
    CHECK(interpFrameDraw(&b, triangle, 3, 456, 23, 91));
    frame(&b, 10); interpFrameClear(&b); b.scene = 1; b.w = 320; b.h = 224; b.cleared = 1;
    CHECK(interpFrameDraw(&b, triangle, 3, 456, 23, 91));
    CHECK(interpFrameDraw(&b, triangle, 3, 123, 17, 42));
    CHECK(interpFramesCompatible(&a, &b));
    frame(&a, 0); frame(&b, 10);
    /* Authored discrete layers do not move with the geometry. */
    CHECK(interpFrameDraw(&a, triangle, 3, 0, 0, 0));
    CHECK(interpFramesCompatible(&a, &b));
    CHECK(interpFrameVertices(&a, &b, 0.5, output, 6) == 6 && output[3].x == triangle[0].x);
    uint8_t pixels[8] = {1, 2, 3, 255, 4, 5, 6, 128}; uint32_t offset;
    CHECK(interpFramePixels(&a, pixels, 2, &offset) && offset == 0);
    memset(pixels, 0, sizeof(pixels));
    CHECK(a.pixels[0] == 0xff030201u && a.pixels[1] == 0x80060504u);
    CHECK(!interpFramePixels(&a, pixels, INTERP_MAX_PIXELS + 1u, &offset) && !a.valid);

    /* A changed animation layout keeps its old image/pose but follows the
     * independent actor anchor, even if this particular part disappears. */
    frame(&a, 0); interpFramePlacement(&a, 40, 60, 100);
    frame(&b, 10); interpFrameClear(&b); b.scene = 1; b.w = 320; b.h = 224; b.cleared = 1;
    CHECK(interpFrameDraw(&b, triangle, 3, 123, 99, 999)); interpFramePlacement(&b, 50, 60, 100);
    CHECK(interpFramesCompatible(&a, &b));
    CHECK(interpFrameVertices(&a, &b, 0.5, output, 6) == 3 && output[0].x == 45 && output[1].x == 65);
    CHECK(output[0].u == a.vertices[0].u && output[0].r == a.vertices[0].r);
    interpFramePlacement(&b, 50, 60, 200);
    CHECK(interpFrameVertices(&a, &b, 0.5, output, 6) == 3 && fabsf(output[1].x - (45 + 20.0f / 1.5f)) < 1e-5);
    b.draws[0].anchor[0] = 100; CHECK(!interpFramesCompatible(&a, &b)); b.draws[0].anchor[0] = 50;
    b.draws[0].anchor[2] = 0; CHECK(!interpFramesCompatible(&a, &b));

    InterpTimeline t;
    interpTimelineReset(&t);
    interpTimelineActivate(&t, 1, 0, 100);
    CHECK(t.previous == -1 && interpTimelineAlpha(&t, 100) == 1);
    interpTimelineActivate(&t, 2, 1, 102);
    CHECK(t.previous == 0 && t.current == 1);
    CHECK(interpTimelineAlpha(&t, 102) == 0 && interpTimelineAlpha(&t, 103) == 0.5);
    CHECK(interpTimelineAlpha(&t, 105) == 1 && interpTimelineAlpha(&t, 101) == 0);
    interpTimelineActivate(&t, 2, 1, 103);
    CHECK(t.activated == 102); /* repeated display is not another endpoint */
    interpTimelineActivate(&t, 3, 2, 105);
    CHECK(t.interval == 3 && fabs(interpTimelineAlpha(&t, 106) - 1.0 / 3) < 1e-12);
    interpTimelineActivate(&t, 4, 3, 109);
    CHECK(t.interval == 4 && interpTimelineAlpha(&t, 111) == 0.5);
    interpTimelineActivate(&t, 5, 0, 120);
    CHECK(t.previous == -1); /* stall/cut: no extrapolation */

    for (int hz = 60; hz <= 240; hz += 12) {
        InterpPacer p; interpPacerReset(&p, 1000, hz);
        uint64_t last = 1000;
        for (int i = 1; i <= hz * 10; i++) {
            uint64_t next = interpPacerDeadline(&p);
            CHECK(next > last);
            CHECK(next == 1000 + (uint64_t)i * UINT64_C(1000000000) / (uint64_t)hz);
            last = next; interpPacerAdvance(&p, next);
        }
        CHECK(last == UINT64_C(10000001000));
        interpPacerAdvance(&p, UINT64_C(20000001000));
        CHECK(interpPacerDeadline(&p) > UINT64_C(20000001000));
        interpPacerAdvance(&p, 500); CHECK(p.anchor == 500 && p.tick == 1);
    }
    InterpPacer off; interpPacerReset(&off, 0, 0); CHECK(interpPacerDeadline(&off) == UINT64_MAX);
    int adaptive = 0, prepare = 60;
    for (int second = 0; second < 20; second++) {
        prepare = interpPrepareRate(60,prepare,60.0,&adaptive);
        CHECK(prepare == 60 && !adaptive); /* a healthy target stays fixed */
    }
    /* A device losing idle preparation opportunities must converge to its
     * output goal, without chasing an impossible rate or changing the goal. */
    for (int second = 0; second < 20; second++) {
        double delivered = fmin(64.5,prepare*.94);
        int wasAdaptive = adaptive;
        int next = interpPrepareRate(60,prepare,delivered,&adaptive);
        CHECK(next >= 60 && next <= 120);
        CHECK(wasAdaptive ? abs(next-prepare) <= 15 : next == 120);
        prepare = next;
        if (second > 5) CHECK(fmin(64.5,prepare*.94) >= 63.5);
    }
    CHECK(adaptive);
    int steady = prepare;
    for (int second = 0; second < 20; second++) {
        prepare = interpPrepareRate(60,prepare,64.5+(second%3-1)*.25,&adaptive);
        CHECK(prepare == steady); /* no oscillation around a device ceiling */
    }
    for (int second = 0; second < 30; second++) prepare = interpPrepareRate(60,prepare,20,&adaptive);
    CHECK(prepare == 120); /* bounded even when hardware cannot meet it */
    for (int second = 0; second < 20; second++) {
        prepare = interpPrepareRate(60,prepare,prepare*.99,&adaptive);
        CHECK(prepare >= 60 && prepare <= 120);
    }
    CHECK(prepare <= 75); /* release excess work when capacity returns */
    CHECK(interpPrepareRate(60,60,0,&adaptive) == 120);
    CHECK(interpPrepareRate(60,60,1e-300,&adaptive) == 120);
    CHECK(interpPrepareRate(60,75,NAN,&adaptive) == 75);
    CHECK(interpPrepareRate(60,75,INFINITY,&adaptive) == 75);
    int unchangedRates[] = {0,120,144,240};
    for (unsigned i = 0; i < sizeof(unchangedRates)/sizeof(unchangedRates[0]); i++) {
        adaptive = 1;
        CHECK(interpPrepareRate(unchangedRates[i],75,1,&adaptive) == unchangedRates[i] && !adaptive);
    }
    interpFrameFree(&a); interpFrameFree(&b);
    printf("Interpolation snapshots and pacing: %d checks passed\n", checks);
    return 0;
}
