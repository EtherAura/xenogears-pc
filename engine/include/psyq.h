#ifndef PSYQ_H
#define PSYQ_H

/*
 * PSY-Q SDK types the game uses, declared here (the SDK headers are not
 * distributed). Layouts and field names follow the PlayStation's documented
 * libgte/libgpu structures.
 */

#include "common.h"

/* libgte */

typedef struct {
    s16 vx, vy, vz, pad;
} SVECTOR;

typedef struct {
    s32 vx, vy, vz, pad;
} VECTOR;

typedef struct {
    s16 m[3][3];
    s32 t[3];
} MATRIX;

/* libgpu */

/*
 * Ordering tables and primitive headers. A tag links a primitive to the next
 * one and holds its word count: on the PS1 a 24-bit address and an 8-bit
 * count in one word. The PC port (PLATFORM_PC) keeps the same idea in 64
 * bits: a 56-bit host pointer and the 8-bit count. OTag is the type of a tag
 * (and of an ordering-table entry), so primitive structs and OT arrays are
 * declared with it; on the PS1 it is plain u32.
 */
#if !defined(XG_PS1_LAYOUT)
typedef u64 OTag;

typedef struct {
    u64 addr : 56;
    u64 len : 8;
    u8 r0, g0, b0, code;
} P_TAG;

#define setaddr(p, _addr) (((P_TAG*)(p))->addr = (u64)(uptr)(_addr))
#define getaddr(p) ((uptr)(((P_TAG*)(p))->addr))
#else
typedef u32 OTag;

/* Primitive header view: ordering-table link (24 bits) and word count. */
typedef struct {
    u32 addr : 24;
    u32 len : 8;
    u8 r0, g0, b0, code;
} P_TAG;

#define setaddr(p, _addr) (((P_TAG*)(p))->addr = (u32)(_addr))
#define getaddr(p) (u32)(((P_TAG*)(p))->addr)
#endif

/*
 * PRIM_OFS(o): where byte o of a PS1 primitive (o >= 4: past the tag) lies in
 * the PC layout, whose tag is 8 bytes. For code that fills primitives through
 * byte offsets rather than field names.
 */
#if !defined(XG_PS1_LAYOUT)
#define PRIM_OFS(o) ((o) + 4)
#else
#define PRIM_OFS(o) (o)
#endif

/*
 * Hand-written tag arithmetic, for the few places that bypass the macros
 * above. On the PS1 each expands to exactly the expression the retail code
 * used; on the PC to the same operation on a 64-bit tag.
 *   OT_LINK(p)    the link part of a tag that points at p
 *   OT_NEXT(tag)  the address a tag links to
 *   OT_LEN(n)     a word count in the length byte
 *   OT_LINK_MASK  the link bits of a tag
 */
#if !defined(XG_PS1_LAYOUT)
#define OT_LINK_MASK 0x00FFFFFFFFFFFFFFull
#define OT_LINK(p) ((OTag)(uptr)(p) & OT_LINK_MASK)
#define OT_NEXT(tag) ((uptr)((tag) & OT_LINK_MASK))
#define OT_LEN(n) ((OTag)(n) << 56)
#else
#define OT_LINK_MASK 0xFFFFFF
#define OT_LINK(p) ((u32)(p) & 0xFFFFFF)
#define OT_NEXT(tag) (((tag) & 0xFFFFFF) + 0x80000000)
#define OT_LEN(n) ((n) << 24)
#endif

#define setlen(p, _len) (((P_TAG*)(p))->len = (u8)(_len))
#define setcode(p, _code) (((P_TAG*)(p))->code = (u8)(_code))
#define addPrim(ot, p) setaddr(p, getaddr(ot)), setaddr(ot, p)

#define setRGB0(p, _r0, _g0, _b0) ((p)->r0 = (_r0), (p)->g0 = (_g0), (p)->b0 = (_b0))
#define setRGB1(p, _r1, _g1, _b1) ((p)->r1 = (_r1), (p)->g1 = (_g1), (p)->b1 = (_b1))
#define setRGB2(p, _r2, _g2, _b2) ((p)->r2 = (_r2), (p)->g2 = (_g2), (p)->b2 = (_b2))
#define setRGB3(p, _r3, _g3, _b3) ((p)->r3 = (_r3), (p)->g3 = (_g3), (p)->b3 = (_b3))
#define setVector(v, _x, _y, _z) ((v)->vx = (_x), (v)->vy = (_y), (v)->vz = (_z))
#define setRECT(r, _x, _y, _w, _h) ((r)->x = (_x), (r)->y = (_y), (r)->w = (_w), (r)->h = (_h))
#define setXYWH(p, _x0, _y0, _w, _h)                                          \
    ((p)->x0 = (_x0), (p)->y0 = (_y0), (p)->x1 = (_x0) + (_w), (p)->y1 = (_y0), \
     (p)->x2 = (_x0), (p)->y2 = (_y0) + (_h), (p)->x3 = (_x0) + (_w), (p)->y3 = (_y0) + (_h))
#define setUVWH(p, _u0, _v0, _w, _h)                                          \
    ((p)->u0 = (_u0), (p)->v0 = (_v0), (p)->u1 = (_u0) + (_w), (p)->v1 = (_v0), \
     (p)->u2 = (_u0), (p)->v2 = (_v0) + (_h), (p)->u3 = (_u0) + (_w), (p)->v3 = (_v0) + (_h))

#define getTPage(tp, abr, x, y) \
    ((((tp) & 0x3) << 7) | (((abr) & 0x3) << 5) | (((y) & 0x100) >> 4) | \
     (((x) & 0x3FF) >> 6) | (((y) & 0x200) << 2))

typedef struct {
    s16 x, y, w, h;
} RECT;

typedef struct {
    OTag tag;
    u32 code[15];
} DR_ENV;

typedef struct {
    /* 0x00 */ RECT clip;
    /* 0x08 */ s16 ofs[2];
    /* 0x0C */ RECT tw;
    /* 0x14 */ u16 tpage;
    /* 0x16 */ u8 dtd;
    /* 0x17 */ u8 dfe;
    /* 0x18 */ u8 isbg;
    /* 0x19 */ u8 r0, g0, b0;
    /* 0x1C */ DR_ENV dr_env;
} DRAWENV; /* 0x5C */

typedef struct {
    /* 0x00 */ RECT disp;
    /* 0x08 */ RECT screen;
    /* 0x10 */ u8 isinter;
    /* 0x11 */ u8 isrgb24;
    /* 0x12 */ u8 pad0, pad1;
} DISPENV; /* 0x14 */

typedef struct {
    OTag tag;
    u8 r0, g0, b0, code;
    s16 x0, y0;
    s16 x1, y1;
    s16 x2, y2;
} POLY_F3;

typedef struct {
    OTag tag;
    u8 r0, g0, b0, code;
    s16 x0, y0;
    u8 u0, v0;
    u16 clut;
    s16 w, h;
} SPRT;

typedef struct {
    OTag tag;
    u8 r0, g0, b0, code;
    s16 x0, y0;
    u8 u0, v0;
    u16 clut;
    s16 x1, y1;
    u8 u1, v1;
    u16 tpage;
    s16 x2, y2;
    u8 u2, v2;
    u16 pad1;
    s16 x3, y3;
    u8 u3, v3;
    u16 pad2;
} POLY_FT4;

typedef struct {
    OTag tag;
    u8 r0, g0, b0, code;
    s16 x0, y0;
    u8 u0, v0;
    u16 clut;
    s16 x1, y1;
    u8 u1, v1;
    u16 tpage;
    s16 x2, y2;
    u8 u2, v2;
    u16 pad1;
} POLY_FT3;

typedef struct {
    OTag tag;
    u8 r0, g0, b0, code;
    s16 x0, y0;
    u8 u0, v0;
    u16 clut;
    u8 r1, g1, b1, p1;
    s16 x1, y1;
    u8 u1, v1;
    u16 tpage;
    u8 r2, g2, b2, p2;
    s16 x2, y2;
    u8 u2, v2;
    u16 pad2;
    u8 r3, g3, b3, p3;
    s16 x3, y3;
    u8 u3, v3;
    u16 pad3;
} POLY_GT4; /* 0x34 */

typedef struct {
    OTag tag;
    u8 r0, g0, b0, code;
    s16 x0, y0;
    s16 w, h;
} TILE;

typedef struct {
    OTag tag;
    u8 r0, g0, b0, code;
    s16 x0, y0;
    u8 r1, g1, b1, pad1;
    s16 x1, y1;
    u8 r2, g2, b2, pad2;
    s16 x2, y2;
} POLY_G3; /* 0x1C */

typedef struct {
    OTag tag;
    u8 r0, g0, b0, code;
    s16 x0, y0;
    s16 x1, y1;
    s16 x2, y2;
    s16 x3, y3;
} POLY_F4; /* 0x18 */

typedef struct {
    OTag tag;
    u8 r0, g0, b0, code;
    s16 x0, y0;
    u8 r1, g1, b1, pad1;
    s16 x1, y1;
    u8 r2, g2, b2, pad2;
    s16 x2, y2;
    u8 r3, g3, b3, pad3;
    s16 x3, y3;
} POLY_G4; /* 0x24 */

typedef struct {
    OTag tag;
    u8 r0, g0, b0, code;
    s16 x0, y0;
    u8 u0, v0;
    u16 clut;
    u8 r1, g1, b1, p1;
    s16 x1, y1;
    u8 u1, v1;
    u16 tpage;
    u8 r2, g2, b2, p2;
    s16 x2, y2;
    u8 u2, v2;
    u16 pad2;
} POLY_GT3; /* 0x28 */

typedef struct {
    OTag tag;
    u8 r0, g0, b0, code;
    s16 x0, y0;
    s16 x1, y1;
} LINE_F2; /* 0x10 */

typedef struct {
    OTag tag;
    u8 r0, g0, b0, code;
    s16 x0, y0;
    s16 x1, y1;
    s16 x2, y2;
    u32 pad;
} LINE_F3; /* 0x18 */

typedef struct {
    OTag tag;
    u8 r0, g0, b0, code;
    s16 x0, y0;
    s16 x1, y1;
    s16 x2, y2;
    s16 x3, y3;
    u32 pad;
} LINE_F4; /* 0x1C */

typedef struct {
    OTag tag;
    u8 r0, g0, b0, code;
    s16 x0, y0;
    u8 r1, g1, b1, p1;
    s16 x1, y1;
} LINE_G2; /* 0x14 */

/* 8x8 and 16x16 sprites: no size fields */
typedef struct {
    OTag tag;
    u8 r0, g0, b0, code;
    s16 x0, y0;
    u8 u0, v0;
    u16 clut;
} SPRT_8;

typedef struct {
    OTag tag;
    u8 r0, g0, b0, code;
    s16 x0, y0;
    u8 u0, v0;
    u16 clut;
} SPRT_16;

typedef struct {
    OTag tag;
    u8 r0, g0, b0, code;
    s16 x0, y0;
} TILE_1;

typedef struct {
    OTag tag;
    u32 code[1];
} DR_TPAGE;

typedef struct {
    OTag tag;
    u32 code[2];
} DR_MODE;

typedef struct {
    OTag tag;
    u32 code[5];
} DR_MOVE; /* VRAM to VRAM move */

typedef struct {
    OTag tag;
    u32 code[2];
} DR_AREA; /* drawing area */

typedef struct {
    OTag tag;
    u32 code[2];
} DR_OFFSET; /* drawing offset */

/* CD position, callback and TIM header types (LIBCD, LIBGPU). */
typedef struct {
    u8 minute;
    u8 second;
    u8 sector;
    u8 track;
} CdlLOC;

typedef void (*CdlCB)(u8, u8*);

typedef struct {
    u8 r, g, b, cd;
} CVECTOR;

typedef struct {
    /* 0x00 */ u32 mode;
    /* 0x04 */ RECT* crect;
    /* 0x08 */ u32* caddr;
    /* 0x0C */ RECT* prect;
    /* 0x10 */ u32* paddr;
} TIM_IMAGE;

/* SDK functions (official signatures; int == s32). */

s32 ResetGraph(s32 mode);
s32 DrawSync(s32 mode);
s32 VSync(s32 mode);
void DrawOTag(OTag* p);
DISPENV* PutDispEnv(DISPENV* env);
DRAWENV* PutDrawEnv(DRAWENV* env);
DISPENV* SetDefDispEnv(DISPENV* env, s32 x, s32 y, s32 w, s32 h);
DRAWENV* SetDefDrawEnv(DRAWENV* env, s32 x, s32 y, s32 w, s32 h);
void InitGeom(void);
void SetGeomOffset(s32 ofx, s32 ofy);
s32 rand(void);

void LoadImage(RECT* rect, void* data);
/* Officially u_char r, g, b; retail callers pass ints unmasked. */
void ClearImage(RECT* rect, s32 r, s32 g, s32 b);
void AddPrim(void* ot, void* p);

void PushMatrix(void);
void PopMatrix(void);
void SetRotMatrix(MATRIX* m);
void SetTransMatrix(MATRIX* m);
void SetColorMatrix(MATRIX* m);
void SetLightMatrix(MATRIX* m);
void SetBackColor(s32 rbk, s32 gbk, s32 bbk);
void ReadGeomOffset(s32* ofx, s32* ofy);
MATRIX* TransMatrix(MATRIX* m, VECTOR* v);
MATRIX* CompMatrix(MATRIX* m0, MATRIX* m1, MATRIX* m2);
MATRIX* MulMatrix0(MATRIX* m0, MATRIX* m1, MATRIX* m2);
MATRIX* ScaleMatrixL(MATRIX* m, VECTOR* v);
VECTOR* ApplyMatrix(MATRIX* m, SVECTOR* v0, VECTOR* v1);
s32 RotTransPers(SVECTOR* v0, s32* sxy, s32* p, s32* flag);
s32 RotAverage4(SVECTOR* v0, SVECTOR* v1, SVECTOR* v2, SVECTOR* v3,
                s16* sxy0, s16* sxy1, s16* sxy2, s16* sxy3, s32* p, s32* flag);

/* More SDK functions, by library (moved from local declarations; the
 * signatures follow our SDK C where it defines the function). */
/* LIBAPI */
s32 OpenEvent(u32 desc, s32 spec, s32 mode, void* func);
void SwEnterCriticalSection(void);
void SwExitCriticalSection(void);
void StartPAD(void);
void StopPAD(void);
/* The PC uses the host C library: its own signatures, so platform code can
 * include these headers next to the system's. */
int sprintf(char* buf, const char* fmt, ...);
void bzero(void* p, __SIZE_TYPE__ n);
void* (memchr)(const void* s, int c, __SIZE_TYPE__ n);
void* memcpy(void* dst, const void* src, __SIZE_TYPE__ n);
void* memmove(void* dst, const void* src, __SIZE_TYPE__ n);
__SIZE_TYPE__ strlen(const char* s);
int abs(int x);
/* LIBCARD */
void InitCARD(s32 pad_enable);
void StartCARD(void);
/* LIBCD */
s32 CdInit(void);
void CdFlush(void);
s32 CdSetDebug(s32 level);
CdlCB CdSyncCallback(CdlCB func);
CdlCB CdReadyCallback(CdlCB func);
s32 CdControl(u8 com, u8* param, u8* result);
s32 CdControlF(u8 com, u8* param);
s32 CdControlB(u8 com, u8* param, u8* result);
s32 CdGetSector(void* madr, s32 size);
void* CdDataCallback(void* func);
s32 CdDataSync(s32 mode);
CdlLOC* CdIntToPos(s32 i, CdlLOC* p);
s32 CdPosToInt(CdlLOC* p);
/* LIBETC */
void ResetCallback(void);
void* VSyncCallback(void (*func)(void));
s32 SetVideoMode(s32 mode);
/* LIBGPU */
u16 GetTPage(s32 tp, s32 abr, s32 x, s32 y);
s32 MargePrim(void* p0, void* p1);
u16 GetClut(s32 x, s32 y);
void TermPrim(void* p);
void SetSemiTrans(void* p, s32 abe);
void SetShadeTex(void* p, s32 tge);
void SetPolyF3(void* p);
void SetPolyFT3(void* p);
void SetPolyF4(void* p);
void SetPolyFT4(void* p);
void SetPolyG4(void* p);
void SetPolyGT4(void* p);
void SetSprt(void* p);
void SetDrawTPage(DR_TPAGE* p, s32 dfe, s32 dtd, s32 tpage);
s32 SetGraphDebug(s32 level);
void* DrawSyncCallback(void (*func)(void));
void SetDispMask(s32 mask);
s32 StoreImage(RECT* rect, u32* p);
s32 MoveImage(RECT* rect, s32 x, s32 y);
OTag* ClearOTagR(OTag* ot, s32 n);
void DrawPrim(void* p);
DRAWENV* GetDrawEnv(DRAWENV* env);
s32 OpenTIM(u32* addr);
TIM_IMAGE* ReadTIM(TIM_IMAGE* timimg);
/* LIBGTE */
s32 ReadGeomScreen(void);
MATRIX* MulMatrix(MATRIX* m0, MATRIX* m1);
MATRIX* TransposeMatrix(MATRIX* m0, MATRIX* m1);
MATRIX* RotMatrixYXZ(SVECTOR* r, MATRIX* m);
void OuterProduct12(VECTOR* v0, VECTOR* v1, VECTOR* v2);
s32 NormalClip(s32 sxy0, s32 sxy1, s32 sxy2);
s32 RotTransPers4(SVECTOR* v0, SVECTOR* v1, SVECTOR* v2, SVECTOR* v3, s32* sxy0, s32* sxy1, s32* sxy2,
                  s32* sxy3, s32* p, s32* flag);
void SetGeomScreen(s32 h);
s32 SquareRoot0(s32 a);
void Square0(VECTOR* v0, VECTOR* v1);
s32 VectorNormal(VECTOR* v0, VECTOR* v1); /* PSY-Q: long; the return type changes callers' allocation */
void VectorNormalS(VECTOR* v0, SVECTOR* v1);
void VectorNormalSS(SVECTOR* v0, SVECTOR* v1);
VECTOR* ApplyMatrixLV(MATRIX* m, VECTOR* v0, VECTOR* v1);
MATRIX* SetMulMatrix(MATRIX* m0, MATRIX* m1);
SVECTOR* ApplyMatrixSV(MATRIX* m, SVECTOR* v0, SVECTOR* v1);
MATRIX* ScaleMatrix(MATRIX* m, VECTOR* v);
void NormalColor(SVECTOR* v0, CVECTOR* v1);
void NormalColor3(SVECTOR* v0, SVECTOR* v1, SVECTOR* v2, CVECTOR* v3, CVECTOR* v4, CVECTOR* v5);
void NormalColorCol(SVECTOR* v0, CVECTOR* v1, CVECTOR* v2);
void OuterProduct0(VECTOR* v0, VECTOR* v1, VECTOR* v2);
void RotTransSV(SVECTOR* v0, SVECTOR* v1, s32* flag);
s32 RotTransPers3(SVECTOR* v0, SVECTOR* v1, SVECTOR* v2, s32* sxy0, s32* sxy1, s32* sxy2, s32* p, s32* flag);
void RotMatrixZ(s32 r, MATRIX* m);
s32 ratan2(s32 y, s32 x);
/* LIBSN */
s32 PCopen(char* name, s32 flags, s32 perms);
s32 PClseek(s32 fd, s32 offset, s32 mode);
s32 PCcreat(char* name, s32 perms);
s32 PCread(s32 fd, char* buf, u32 len);
s32 PCwrite(s32 fd, char* buf, u32 len);
/* LIBSPU */
void SpuInit(void);
void SpuQuit(void);
/* SpuSetNoiseClock: psyq/libspu.h */

#endif /* PSYQ_H */
