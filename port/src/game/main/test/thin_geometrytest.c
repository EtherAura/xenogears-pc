/* Actual non-model drawing paths. Narrow faces must leave native packet
 * payloads, GTE registers, counters, cursors and scheduler yields unchanged. */
#include <stdio.h>
#include <string.h>
#include "common.h"
#include "psyq.h"
#include "gte_pc.h"
#include "pc_geometry.h"
#include "battle/battle.h"
#include "../../../test/check.h"

static int checks, enabled, allocationFailure, allocated, yields, blends, negativeCase;
static _Alignas(16) u8 extra[65536];
static u8 packets[16384], before[16384], modes[512];
static OTag ot[4096];
static SVECTOR vertices[8];
SVECTOR* currentModelBlockVertices;
u8* currentModelInstanceDrawPrims;
u8* currentModeBlock18;
OTag* currentOTEntry;
s32 fieldPolyCount, fieldPolyCount2, screenClippingX, screenClippingY, gDepthDivider;
s32 renderWorldmapGroundVar1;
s16 worldmapBillboardPolyCount;
VECTOR worldmapRadarPosition3;
s32 worldmapSizeX, worldmapSizeY;
u8* worldmapChunks[256];
void* worldmapFile1Buffer_14;
void pcInterpolationKeyType(const void* o, const void* r, unsigned part, void* p, int kind) { (void)o; (void)r; (void)part; (void)p; (void)kind; }
u8 battlingBufferIndex;
POLY_FT3* battlingGroundPolys[2];
u32* battlingHeightMap;
int pcWideView, pcWideCull, pcVerticalCull, pcWideOnlyScope;
u8* shapeTransfertTableCurrentEntry;
u8* shapeTransfertTableEnd;
DR_TPAGE customPolyTPage;

int pcGeometryActive(void) { return gtePrecisionEnabled(); }
int pcGeometrySlivers(void) { return enabled; }
void pcGeometryPrim(void* p, int t) { (void)p; (void)t; }
void pcFadePrim(void* p) { (void)p; }
void pcGeometryBlend(void* p, int abr) { (void)p; if (abr < 0 || abr > 3) blends += 1000; else blends++; }
void* pcGeometryAlloc(const void* p, int bytes) {
    if (allocationFailure || bytes <= 0 || allocated + bytes > (int)sizeof(extra)) return NULL;
    void* out = extra + allocated; allocated += (bytes + 15) & ~15;
    memcpy(out, p, bytes); return out;
}
void* pcWideAlloc(int bytes) { return pcGeometryAlloc(packets, bytes); }
void pcWidePrim(void* p, int dx, unsigned flags) { (void)p; (void)dx; (void)flags; }
void copyBattlingWords(void* dst, void* src, s32 n) { memcpy(dst, src, n); }
void hwSafePoint(void) { yields++; }
void SetShadeTex(void* p, s32 on) { P_TAG* t=p; t->code=(t->code & ~1) | (on!=0); }
void SetSemiTrans(void* p, s32 on) { P_TAG* t=p; t->code=(t->code & ~2) | ((on!=0)<<1); }
s32 addClamp255(s32 a, s32 b) { s32 c=a+b; return c<0?0:c>255?255:c; }
void SetDrawTPage(DR_TPAGE* p, s32 dfe, s32 dtd, s32 page) {
    (void)dfe; (void)dtd; p->code[0] = 0xe1000000 | page; setlen(p, 1);
}
void AddPrim(void* entry, void* p) { OTag* ot = entry; *(OTag*)p = (*ot & OT_LINK_MASK) | (*(OTag*)p & ~OT_LINK_MASK); *ot = OT_LINK(p); }
int portScratchpadInit(void);
void renderLitFlatQuads(void*, s32);
void renderBattlingShadowTris(u8*, s32);
void renderBattlingShadowQuads(u8*, s32);
s32 renderBattlingGround(OTag*, s32, s32);
void pcWideBattlingGroundCells(OTag*,s32,s32,const u8*,const u8*);
void drawWorldmapGroundCellSub0(u32*, void*, void*);
void renderCustomPolyBuffer(sCustomPolySubBuffer*, u8*, OTag*, s32, s32, s32);
void copyCustomPolySubBuffer(sCustomPolySubBuffer*, u8*, s32, u8);

static void setup(int on, int fail, int precision) {
    GteState s = {0}; gteLoad(&s); gtePrecisionEnable(precision);
    gteCtc2(0, 4096); gteCtc2(2, 4096); gteCtc2(4, 4096);
    gteCtc2(24, 160 << 16); gteCtc2(25, 100 << 16); gteCtc2(26, 256);
    gteCtc2(29, 1365); gteCtc2(30, 1024);
    vertices[0] = (SVECTOR){0,0,1000,0}; vertices[1] = (SVECTOR){1,0,1000,0};
    vertices[2] = (SVECTOR){0,1,1000,0}; vertices[3] = (SVECTOR){1,1,1000,0};
    if (negativeCase) {
        vertices[1] = (SVECTOR){12,7,1000,0}; vertices[2] = (SVECTOR){5,3,1000,0};
        vertices[3] = (SVECTOR){17,10,1000,0};
    }
    vertices[4] = (SVECTOR){-100,-100,1000,0}; vertices[5] = (SVECTOR){100,-100,1000,0};
    vertices[6] = (SVECTOR){-100,100,1000,0}; vertices[7] = (SVECTOR){100,100,1000,0};
    memset(packets, 0x55, sizeof(packets)); memset(modes, 0, sizeof(modes));
    memset(extra, 0, sizeof(extra)); memset((void*)0x1F800000, 0, 4096);
    for (int i=0;i<4096;i++) ot[i] = OT_LINK_MASK;
    currentModelBlockVertices=vertices; currentModelInstanceDrawPrims=packets;
    currentModeBlock18=modes; currentOTEntry=ot; fieldPolyCount=fieldPolyCount2=0;
    renderWorldmapGroundVar1=0; screenClippingX=320; screenClippingY=216<<16;
    gDepthDivider=4; enabled=on; allocationFailure=fail; allocated=yields=blends=0;
    pcWideCull=pcVerticalCull=0; shapeTransfertTableCurrentEntry=modes;
    shapeTransfertTableEnd=modes+sizeof(modes);
}
static int sameGte(const GteState* a, const GteState* b) {
    return !memcmp(a->data,b->data,sizeof(a->data)) && !memcmp(a->ctrl,b->ctrl,sizeof(a->ctrl)) &&
           !memcmp(a->precise,b->precise,sizeof(a->precise));
}
static void basic(int loop) {
    u16 idx[] = {0,1,2,3, 4,5,6,7, 0,1,2,3};
    if (loop==0) renderLitFlatQuads(idx,2);
    else if (loop==1) renderBattlingShadowTris((u8*)idx,2);
    else renderBattlingShadowQuads((u8*)idx,2);
}
static void ground(int arena) {
    static u32 heights[128*128], cells[81];
    u8* scratch=(u8*)0x1F800000;
    if (arena) {
        memset(heights,0,sizeof(heights)); for (int i=128;i<256;i++) heights[i]=0xffff;
        battlingHeightMap=heights; battlingGroundPolys[0]=(POLY_FT3*)packets;
        scratch[0]=1; scratch[128]=0;
        gteCtc2(7,1000); gteCtc2(25,(100<<16)+32768);
        renderBattlingGround(ot,0,0);
        if (arena==2) {
            u8 starts[127]={0},ends[127]={2};
            pcWideCull=64;
            pcWideBattlingGroundCells(ot,0,0,starts,ends);
        }
    } else {
        SVECTOR* grid=(SVECTOR*)scratch;
        for(int row=0;row<9;row++) for(int col=0;col<9;col++) grid[row*9+col]=(SVECTOR){col,row,1000,0};
        drawWorldmapGroundCellSub0(cells,ot,packets);
    }
}
int main(void) {
    CHECK(portScratchpadInit());
    for(int loop=0;loop<6;loop++) for(int face=0;face<(loop<3?2:1);face++) for(int control=0;control<4;control++) {
        negativeCase=face;
        GteState off,on;
        setup(0,0,1); if(control==3) gteCtc2(24,4000u<<16); if(loop<3) basic(loop); else ground(loop>=4?loop-3:0); gteSave(&off);
        int count=fieldPolyCount, count2=fieldPolyCount2, wc=renderWorldmapGroundVar1;
        u8* cursor=currentModelInstanceDrawPrims; u8* normal=currentModeBlock18;
        memcpy(before,packets,sizeof(packets));
        setup(1,control==1,control!=2); if(control==3) gteCtc2(24,4000u<<16); if(loop<3) basic(loop); else ground(loop>=4?loop-3:0); gteSave(&on);
        CHECK(fieldPolyCount==count && fieldPolyCount2==count2 && renderWorldmapGroundVar1==wc);
        CHECK(currentModelInstanceDrawPrims==cursor && currentModeBlock18==normal);
        CHECK(!memcmp(off.data,on.data,sizeof(off.data)) && !memcmp(off.ctrl,on.ctrl,sizeof(off.ctrl)));
        if(control!=2) CHECK(sameGte(&off,&on));
        CHECK(control ? allocated==0 : allocated>0);
        /* Headers can link through private packets; native payloads cannot change. */
        int stride=loop==1 ? sizeof(POLY_F3) : loop<3 ? sizeof(POLY_F4) : sizeof(POLY_FT3);
        for(int i=0;i<128;i++) CHECK(!memcmp(before+i*stride+sizeof(OTag),packets+i*stride+sizeof(OTag),stride-sizeof(OTag)));
    }
    /* SDK custom-buffer quads return early on zero NCLIP. All eight packet
     * layouts, both shaded/unshaded and four blend modes, use the same rescue.
     * Copying the native template happens after its native shading. */
    for(int face=0;face<2;face++) for(int kind=0;kind<8;kind++) for(int lit=0;lit<2;lit++) for(int blend=0;blend<5;blend++) {
        negativeCase=face;
        struct { sCustomPolySubBuffer h; SVECTOR v[4]; SVECTOR n[4]; u8 stream[64]; } model={0};
        static const int lengths[]={4,7,5,9,6,9,8,12};
        int type=kind*4, bytes=(lengths[kind]+1)*4+PRIM_OFS(0);
        model.h.m0_offsetToVertices=(u8*)model.v-(u8*)&model;
        model.h.m8_offsetToNormals=(u8*)model.n-(u8*)&model;
        model.h.m10_offsetToPackets=(u8*)model.stream-(u8*)&model; model.h.m14_count=1;
        model.stream[0]=lengths[kind]; model.stream[1]=15; model.stream[2]=lit ? 0 : 1; model.stream[3]=type;
        /* Retail stream vertex offsets depend on packet kind and lighting. */
        static const int offsets[2][8][4]={
            {{8,10,12,0},{20,22,24,0},{8,10,12,14},{24,26,28,30},{16,18,20,0},{28,30,32,0},{20,22,24,26},{36,38,40,42}},
            {{10,12,14,0},{18,20,22,0},{10,12,14,16},{22,24,26,28},{16,22,26,0},{18,22,26,0},{22,26,30,34},{22,26,30,34}}};
        for(int i=0;i<((type&8)?4:3);i++) *(u16*)(model.stream+offsets[lit][kind][i])=i;
        GteState off,on;
        setup(0,0,1); memcpy(model.v,vertices,sizeof(model.v));
        memset(packets,0,bytes); setlen(packets,lengths[kind]); ((P_TAG*)packets)->code=0x20|type|(blend?2:0);
        renderCustomPolyBuffer(&model.h,packets,ot,0,0,blend); gteSave(&off);
        int count=yields; u8* cursor=shapeTransfertTableCurrentEntry;
        memcpy(before,packets,bytes);
        setup(1,0,1); memset(packets,0,bytes); setlen(packets,lengths[kind]); ((P_TAG*)packets)->code=0x20|type|(blend?2:0);
        renderCustomPolyBuffer(&model.h,packets,ot,0,0,blend); gteSave(&on);
        if (!allocated) fprintf(stderr,"custom kind=%d lit=%d blend=%d\n",kind,lit,blend);
        CHECK(allocated>0); CHECK(sameGte(&off,&on)); CHECK(yields==count);
        CHECK(shapeTransfertTableCurrentEntry==cursor && !memcmp(before,packets,bytes));
        CHECK(blends==((blend && !(type&4))?1:0));
    }
    printf("Thin geometry: %d checks passed across terrain, shadows, lit quads and 16 custom layouts\n",checks);
    return 0;
}
