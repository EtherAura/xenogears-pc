/*
 * LIBGPU's primitive helpers on the PC: packet initialisers, ordering-table
 * links, texture page and CLUT words, and the TIM reader. The same logic as
 * the PS1 library (src/main/psyq/libgpu), on the PC's packet header: a 64-bit
 * tag (include/psyq.h OTag) followed by the GP0 words.
 *
 * These only touch the game's memory, never the GPU, so they are not safe
 * points (hw.h): the game calls them thousands of times a frame, and under
 * XG_CLOCK=step every safe point costs virtual time.
 */

#include "common.h"
#include "psyq.h"

#include "gpu_internal.h"
#include "../system/savestate.h"
#include "pc_wide.h"

/* The GP0 words of a packet, after its tag. */
#define WORDS(p) ((u32*)((OTag*)(p) + 1))

/* Packet types the game declares in its own headers (same layouts). */
typedef struct {
    OTag tag;
    u32 code[2];
} DrPair; /* DR_AREA, DR_OFFSET, DR_TWIN, DR_PRIO */

typedef struct {
    OTag tag;
    u32 code[5];
} DrMove;

/* ---- ordering tables ------------------------------------------------------------- */

void AddPrim(void* ot, void* p) {
    addPrim(ot, p);
}

void pcTextureHudPrim(void* ot, void* p) {
    pcTextureClassDefault(p, PC_TEX_HUD);
    addPrim(ot, p);
}

void pcTextureTextPrim(void* ot, void* p) {
    pcTextureClass(p, PC_TEX_TEXT);
    addPrim(ot, p);
}

void AddPrims(void* ot, void* p0, void* p1) {
    setaddr(p1, getaddr(ot));
    setaddr(ot, p0);
}

void CatPrim(void* p0, void* p1) {
    setaddr(p0, p1);
}

void TermPrim(void* p) {
    *(OTag*)p |= OT_LINK_MASK;
}

void* NextPrim(void* p) {
    return (void*)OT_NEXT(*(OTag*)p);
}

s32 IsEndPrim(void* p) {
    return (*(OTag*)p & OT_LINK_MASK) == OT_LINK_MASK;
}

/* p1 must follow p0 in memory. Its header becomes NOP words inside the merged
 * packet: one on the PS1, two on the PC (the 64-bit tag). The 16-word limit is
 * the PS1's count, so the same merges succeed. */
s32 MargePrim(void* p0, void* p1) {
    int len = ((P_TAG*)p0)->len + ((P_TAG*)p1)->len + 1;
    if (len > 16) {
        return -1;
    }
    setlen(p0, len + (int)(sizeof(OTag) / 4) - 1);
    *(OTag*)p1 = 0;
    return 0;
}

/* ---- primitive attributes ------------------------------------------------------------ */

void SetSemiTrans(void* p, s32 abe) {
    if (abe) {
        ((P_TAG*)p)->code |= 2;
    } else {
        ((P_TAG*)p)->code &= ~2;
    }
}

void SetShadeTex(void* p, s32 tge) {
    if (tge) {
        ((P_TAG*)p)->code |= 1;
    } else {
        ((P_TAG*)p)->code &= ~1;
    }
}

u16 GetTPage(s32 tp, s32 abr, s32 x, s32 y) {
    return getTPage(tp, abr, x, y);
}

u16 GetClut(s32 x, s32 y) {
    return (u16)((y << 6) | ((x >> 4) & 0x3F));
}

/* ---- packet initialisers ---------------------------------------------------------------- */

#define SETTER(name, len, code)   \
    void name(void* p) {          \
        setlen(p, len);           \
        setcode(p, code);         \
    }

SETTER(SetPolyF3, 4, 0x20)
SETTER(SetPolyFT3, 7, 0x24)
SETTER(SetPolyG3, 6, 0x30)
SETTER(SetPolyGT3, 9, 0x34)
SETTER(SetPolyF4, 5, 0x28)
SETTER(SetPolyFT4, 9, 0x2C)
SETTER(SetPolyG4, 8, 0x38)
SETTER(SetPolyGT4, 12, 0x3C)
SETTER(SetSprt8, 3, 0x74)
SETTER(SetSprt16, 3, 0x7C)
SETTER(SetSprt, 4, 0x64)
SETTER(SetTile1, 2, 0x68)
SETTER(SetTile8, 2, 0x70)
SETTER(SetTile16, 2, 0x78)
SETTER(SetTile, 3, 0x60)
SETTER(SetLineF2, 3, 0x40)
SETTER(SetLineG2, 4, 0x50)

/* Polylines end with a terminator word after their last vertex. */
void SetLineF3(void* p) {
    setlen(p, 5);
    setcode(p, 0x48);
    WORDS(p)[4] = 0x55555555;
}

void SetLineG3(void* p) {
    setlen(p, 7);
    setcode(p, 0x58);
    WORDS(p)[6] = 0x55555555;
}

void SetLineF4(void* p) {
    setlen(p, 6);
    setcode(p, 0x4C);
    WORDS(p)[5] = 0x55555555;
}

void SetLineG4(void* p) {
    setlen(p, 9);
    setcode(p, 0x5C);
    WORDS(p)[8] = 0x55555555;
}

/* ---- environment packets ----------------------------------------------------------------- */

static int clampInt(int v, int lo, int hi) {
    return v < lo ? lo : v > hi ? hi : v;
}

void SetDrawTPage(DR_TPAGE* p, s32 dfe, s32 dtd, s32 tpage) {
    setlen(p, 1);
    p->code[0] = 0xE1000000u | (dtd ? 0x200u : 0) | (dfe ? 0x400u : 0) | ((u32)tpage & 0x9FFu);
}

static u32 texWindowWord(RECT* tw) {
    if (tw == NULL) {
        return 0;
    }
    u32 mx = (u32)(-tw->w & 0xFF) >> 3;
    u32 my = (u32)(-tw->h & 0xFF) >> 3;
    u32 ox = (u32)(tw->x & 0xFF) >> 3;
    u32 oy = (u32)(tw->y & 0xFF) >> 3;
    return 0xE2000000u | oy << 15 | ox << 10 | my << 5 | mx;
}

static u32 areaWord(u32 code, int x, int y) {
    x = clampInt(x, 0, VRAM_W - 1);
    y = clampInt(y, 0, VRAM_H - 1);
    return code | ((u32)y & 0x3FF) << 10 | ((u32)x & 0x3FF);
}

void SetDrawMode(DR_MODE* p, s32 dfe, s32 dtd, s32 tpage, RECT* tw) {
    setlen(p, 2);
    p->code[0] = 0xE1000000u | (dtd ? 0x200u : 0) | (dfe ? 0x400u : 0) | ((u32)tpage & 0x9FFu);
    p->code[1] = texWindowWord(tw);
}

void SetTexWindow(void* p, RECT* tw) {
    DrPair* d = p;
    setlen(d, 2);
    d->code[0] = texWindowWord(tw);
    d->code[1] = 0;
}

void SetDrawArea(void* p, RECT* r) {
    DrPair* d = p;
    setlen(d, 2);
    d->code[0] = areaWord(0xE3000000u, r->x, r->y);
    d->code[1] = areaWord(0xE4000000u, r->x + r->w - 1, r->y + r->h - 1);
}

void SetDrawOffset(void* p, u16* ofs) {
    DrPair* d = p;
    setlen(d, 2);
    d->code[0] = 0xE5000000u | ((u32)ofs[1] & 0x7FF) << 11 | ((u32)ofs[0] & 0x7FF);
    d->code[1] = 0;
}

void SetPriority(void* p, s32 pbc, s32 pbw) {
    DrPair* d = p;
    setlen(d, 2);
    d->code[0] = 0xE6000000u | (pbc ? 2u : 0) | (pbw ? 1u : 0);
    d->code[1] = 0;
}

void SetDrawMove(void* p, RECT* rect, s32 x, s32 y) {
    DrMove* d = p;
    setlen(d, rect->w == 0 || rect->h == 0 ? 0 : 5);
    d->code[0] = 0x01000000u;
    d->code[1] = 0x80000000u;
    d->code[2] = (u32)(u16)rect->y << 16 | (u16)rect->x;
    d->code[3] = (u32)(u16)y << 16 | (u16)x;
    d->code[4] = (u32)(u16)rect->h << 16 | (u16)rect->w;
}

/* ---- textures ------------------------------------------------------------------------------ */

u16 LoadTPage(u32* pix, s32 tp, s32 abr, s32 x, s32 y, s32 w, s32 h) {
    RECT rect;
    rect.x = (s16)x;
    rect.y = (s16)y;
    rect.h = (s16)h;
    rect.w = (s16)(tp == 0 ? w / 4 : tp == 1 ? w / 2 : w);
    LoadImage(&rect, pix);
    return GetTPage(tp, abr, x, y);
}

u16 LoadClut(u32* clut, s32 x, s32 y) {
    RECT rect = {(s16)x, (s16)y, 256, 1};
    LoadImage(&rect, clut);
    return GetClut(x, y);
}

u16 LoadClut2(u32* clut, s32 x, s32 y) {
    RECT rect = {(s16)x, (s16)y, 16, 1};
    LoadImage(&rect, clut);
    return GetClut(x, y);
}

/* TIM: id 10h, flags (bit 3: CLUT present), then the CLUT and pixel blocks,
 * each a byte length, a RECT and the data. */
static u32* timCursor;

s32 OpenTIM(u32* addr) {
    timCursor = addr;
    return 0;
}

TIM_IMAGE* ReadTIM(TIM_IMAGE* tim) {
    u32* p = timCursor;
    if (p == NULL || *p++ != 0x10) {
        return NULL;
    }
    tim->mode = *p++;
    u32 clutWords = 0;
    if (tim->mode & 8) {
        clutWords = *p >> 2;
        tim->crect = (RECT*)(p + 1);
        tim->caddr = p + 3;
        p += clutWords;
    } else {
        tim->crect = NULL;
        tim->caddr = NULL;
    }
    u32 pixelWords = *p >> 2;
    tim->prect = (RECT*)(p + 1);
    tim->paddr = p + 3;
    timCursor += 2 + clutWords + pixelWords;
    return tim;
}

/* Save states (system/savestate.h): where the TIM reader stands (game
 * memory). */
void primStateIo(XgState* s) {
    XG_STATE_VAR(s, timCursor);
}
