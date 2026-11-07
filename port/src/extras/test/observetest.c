/* Synthetic glyph planes and VM events: no disc or renderer. */
#include "observe.h"
#include "pc_observe.h"
#include "sandbox.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static int inside, checks;
XgSandbox* xgSandboxCurrent(void) { return inside ? (XgSandbox*)1 : NULL; }
uint32_t hwVSyncCount(void) { return 1234; }
uint16_t fontPalettes[32] = {0, 0x7fff, 0x42};
#include "test/check.h"
int main(void) {
    XgTrace t;
    XgHistory h;
    uint16_t pixels[13*16];
    int window;
    memset(pixels,0,sizeof(pixels));
    xgTraceEnable(0); pcObserveOp(120,1,2,3,4,0xFE,0x77); CHECK(!xgTraceCount());
    xgTraceEnable(1); inside=1; pcObserveOp(1,2,3,4,5,6,-1); inside=0; CHECK(!xgTraceCount());
    for(int i=0;i<XG_TRACE_CAPACITY+20;i++) pcObserveOp(120,1,2,3,i,0xFE,0x77);
    CHECK(xgTraceCount()==XG_TRACE_CAPACITY); CHECK(xgTraceGet(0,&t) && t.pc==20 && t.vblank==1234 && t.ext==0x77);
    CHECK(xgTraceGet(XG_TRACE_CAPACITY-1,&t) && t.pc==XG_TRACE_CAPACITY+19);
    CHECK(!xgTraceGet(-1,&t) && !xgTraceGet(XG_TRACE_CAPACITY,&t) && !xgTraceGet(0,NULL));
    xgTraceClear(); CHECK(!xgTraceCount()); pcObserveOp(1,2,3,4,7,8,-1); CHECK(xgTraceGet(0,&t) && t.pc==7);
    xgHistoryClear(); xgHistoryEnable(1);
    pcObserveLine(&window,pixels,16,0,2,0,0); CHECK(!xgHistoryCount());
    inside=1; pcObserveWindow(&window,1,2,3); inside=0;
    pcObserveLine(&window,pixels,16,0,2,0,0); CHECK(!xgHistoryCount());
    pcObserveWindow(&window,612,7,10); pcObserveText(&window);
    pixels[0]=0x0021; pixels[2]=0x1111; pcObserveLine(&window,pixels,16,0,2,0,0);
    CHECK(xgHistoryGet(0,&h) && h.field==612 && h.actor==7 && h.dialog==10 && h.lines==1);
    CHECK(h.pixels[0]==1 && h.pixels[1]==2 && h.body==0x7fff && h.outline==0x42);
    CHECK(h.width==8 && h.pixels[8]==0); /* pixels beyond the visible sprite are stale */
    pixels[0]=0x0084; pcObserveLine(&window,pixels,16,0,2,1,1);
    CHECK(xgHistoryGet(0,&h) && h.lines==2 && h.pixels[512*14]==1 && h.pixels[512*14+1]==2);
    CHECK(h.pixels[0]==1 && h.pixels[1]==2); /* previous even line survives */
    pcObserveLine(&window,pixels,16,2,2,1,1); CHECK(xgHistoryGet(0,&h) && h.lines==2);
    pcObserveLine(&window,pixels,16,0,2,1,1); CHECK(xgHistoryGet(0,&h) && h.lines==3); /* pagination/reused physical line */
    inside=1; pcObserveText(&window); pcObserveLine(&window,pixels,16,0,2,0,0); inside=0; CHECK(xgHistoryCount()==1);
    xgHistoryEnable(0); pcObserveText(&window); pcObserveLine(&window,pixels,16,0,2,0,0); CHECK(xgHistoryCount()==1);
    xgHistoryEnable(1);
    for(int i=0;i<XG_HISTORY_CAPACITY+2;i++) { pcObserveWindow(&window,120,2,i); pcObserveText(&window); pcObserveLine(&window,pixels,16,0,2,0,0); }
    CHECK(xgHistoryCount()==XG_HISTORY_CAPACITY); CHECK(xgHistoryGet(0,&h) && h.dialog==2);
    CHECK(xgHistoryGet(XG_HISTORY_CAPACITY-1,&h) && h.dialog==XG_HISTORY_CAPACITY+1);
    for(int i=0;i<XG_HISTORY_LINES+2;i++) pcObserveLine(&window,pixels,16,0,2,i&1,0);
    CHECK(xgHistoryGet(XG_HISTORY_CAPACITY-1,&h) && h.truncated && h.lines==XG_HISTORY_LINES);
    CHECK(!xgHistoryGet(-1,&h) && !xgHistoryGet(64,&h) && !xgHistoryGet(0,NULL));
    xgHistoryClear(); CHECK(!xgHistoryCount());
    pcObserveLine(&window,pixels,16,0,2,0,0); CHECK(xgHistoryGet(0,&h) && h.lines==1);
    pcObserveLine(&window,pixels,0,0,2,0,0); pcObserveLine(&window,pixels,129,0,2,0,0); pcObserveLine(&window,pixels,16,-1,2,0,0);
    xgHistoryClear();
    printf("Observation: %d checks passed\n",checks);
    return 0;
}
