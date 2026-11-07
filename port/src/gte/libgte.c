/*
 * LIBGTE (PSY-Q 4.0) for the PC port: the library functions the game calls,
 * as C on top of the software GTE (gte.c).
 *
 * Most of LIBGTE is hand-written GTE assembly (asm/us/main/393BC.s). Each
 * function here issues the same register traffic in the same order, so it
 * leaves the GTE as the PS1 does: the game mixes these calls with its inline
 * GTE macros and relies on what they leave in RT/TR, the vertex and IR
 * registers and the FIFOs. Results are stored with the original word layout
 * too (a 32-bit store of a 16-bit field also writes the pad after it).
 * ratan2 and SetFogNearFar are the repository's matched C
 * (src/main/psyq/libgte), included as they are.
 *
 * The tables the assembly reads from the PS1 data (sine/cosine and square
 * root) are computed: they are exactly round(4096 sin), floor(512 sqrt) and
 * floor(32768 / sqrt) of their index (checked against the disc's tables by
 * work/agents/PORT_M3/gte).
 */

#include "common.h"
#include "gte_pc.h"
#include "psyq.h"
#include "system/hw.h"
#include "../system/savestate.h"

#include <stdio.h>

/* Every SDK entry point is a safe point (system/hw.h). Weak, so that the GTE
 * also links on its own (the tests in work/agents/PORT_M3/gte). */
#pragma weak hwSafePoint
static inline void safePoint(void) {
    if (hwSafePoint) {
        hwSafePoint();
    }
}

#define WORD(p, off) GTE_U32(p, off)
#define HALF(p, off) GTE_U16(p, off)
#define SHALF(p, off) (*(s16*)GTE_AT(p, off))

#define MVMVA_RT_V0 GTE_MVMVA(1, 0, 0, 3, 0)    /* RT * V0, no translation */
#define MVMVA_RT_V0_TR GTE_MVMVA(1, 0, 0, 0, 0) /* RT * V0 + TR */
#define GTE_NCT 0x0D80420                        /* include/gte_macros.inc nct */
#define GTE_NCCT 0x118043F                       /* ncct */

/* ------------------------------------------------------------------ tables -- */

static s16 sinTable[4096];
static s16 cosTable[4096];
/* SquareRoot0's table (192 entries and zero padding) followed by the inverse
 * square root table, laid out as in the PS1 data: VectorNormal of a zero
 * vector indexes 64 entries before the second one. */
#define SQRT_ENTRIES 202
static s16 sqrtTables[SQRT_ENTRIES + 198];
static int tablesReady;

/* sin(x) for |x| <= pi/4 by its Taylor series (exact to double precision there). */
static double taylorSin(double x) {
    double term = x;
    double sum = x;
    for (int n = 1; n < 12; n++) {
        term *= -x * x / ((2 * n) * (2 * n + 1));
        sum += term;
    }
    return sum;
}

static double taylorCos(double x) {
    double term = 1.0;
    double sum = 1.0;
    for (int n = 1; n < 12; n++) {
        term *= -x * x / ((2 * n - 1) * (2 * n));
        sum += term;
    }
    return sum;
}

static s16 roundToS16(double v) {
    return (s16)(v < 0 ? -(s32)(-v + 0.5) : (s32)(v + 0.5));
}

static u32 isqrt(u32 v) {
    u32 r = 0;
    for (u32 bit = 1u << 30; bit; bit >>= 2) {
        if (v >= r + bit) {
            v -= r + bit;
            r = (r >> 1) + bit;
        } else {
            r >>= 1;
        }
    }
    return r;
}

static void initTables(void) {
    const double quarterTurn = 1.57079632679489661923; /* pi / 2 */
    /* round(4096 sin(2 pi i / 4096)); each octant from the series closest to it. */
    for (int i = 0; i < 4096; i++) {
        int q = i / 1024;
        int r = i % 1024;
        double s;
        double c;
        if (r <= 512) {
            double x = r * quarterTurn / 1024;
            s = taylorSin(x);
            c = taylorCos(x);
        } else {
            double x = (1024 - r) * quarterTurn / 1024;
            s = taylorCos(x);
            c = taylorSin(x);
        }
        double sv[4] = {s, c, -s, -c}; /* sin of the angle in quadrant q */
        double cv[4] = {c, -s, -c, s};
        sinTable[i] = roundToS16(sv[q] * 4096);
        cosTable[i] = roundToS16(cv[q] * 4096);
    }
    for (int i = 0; i < 192; i++) {
        sqrtTables[i] = (s16)isqrt((u32)(i + 64) << 18);
        sqrtTables[SQRT_ENTRIES + i] = (s16)isqrt((1u << 30) / (u32)(i + 64));
    }
    tablesReady = 1;
}

static inline void needTables(void) {
    if (!tablesReady) {
        initTables();
    }
}

/* The 1.3.12 sine and cosine of angle a (4096 per turn), as RotMatrix* read them:
 * a negative angle uses the table at -a with the sine negated. */
static void sinCos(s32 a, s32* s, s32* c) {
    u32 i = (a >= 0 ? (u32)a : -(u32)a) & 0xFFF;
    *s = a >= 0 ? sinTable[i] : -sinTable[i];
    *c = cosTable[i];
}

/* An entry of the square root tables; outside them the PS1 reads other data. */
static s32 sqrtEntry(s32 i) {
    if (i < 0 || i >= (s32)(sizeof sqrtTables / sizeof sqrtTables[0])) {
        static int reported;
        if (!reported) {
            reported = 1;
            fprintf(stderr, "libgte: square root table index %d out of range\n", (int)i);
        }
        return 0;
    }
    return sqrtTables[i];
}

/* ------------------------------------------------------------ geometry setup -- */

void InitGeom(void) {
    safePoint();
    /* _patch_gte (a BIOS patch) and enabling COP2 have no PC counterpart. */
    gteCtc2(29, 0x155);         /* ZSF3 */
    gteCtc2(30, 0x100);         /* ZSF4 */
    gteCtc2(26, 1000);          /* H */
    gteCtc2(27, (u32)-0x1062);  /* DQA */
    gteCtc2(28, 0x1400000);     /* DQB */
    gteCtc2(24, 0);             /* OFX */
    gteCtc2(25, 0);             /* OFY */
}

void SetGeomOffset(s32 ofx, s32 ofy) {
    safePoint();
    gteCtc2(24, (u32)ofx << 16);
    gteCtc2(25, (u32)ofy << 16);
}

void ReadGeomOffset(s32* ofx, s32* ofy) {
    safePoint();
    *ofx = (s32)gteCfc2(24) >> 16;
    *ofy = (s32)gteCfc2(25) >> 16;
}

void SetGeomScreen(s32 h) {
    safePoint();
    gteCtc2(26, (u32)h);
}

s32 ReadGeomScreen(void) {
    safePoint();
    return (s32)gteCfc2(26);
}

void SetDQA(s32 iDqa) {
    safePoint();
    gteCtc2(27, (u32)iDqa);
}

void SetDQB(s32 iDqb) {
    safePoint();
    gteCtc2(28, (u32)iDqb);
}

void SetBackColor(s32 rbk, s32 gbk, s32 bbk) {
    safePoint();
    gteCtc2(13, (u32)rbk << 4);
    gteCtc2(14, (u32)gbk << 4);
    gteCtc2(15, (u32)bbk << 4);
}

void SetFarColor(s32 r, s32 g, s32 b) {
    safePoint();
    gteCtc2(21, (u32)r << 4);
    gteCtc2(22, (u32)g << 4);
    gteCtc2(23, (u32)b << 4);
}

static void setMatrixRegs(int base, MATRIX* m) {
    for (int i = 0; i < 5; i++) {
        gteCtc2(base + i, WORD(m, 4 * i));
    }
}

void SetRotMatrix(MATRIX* m) {
    safePoint();
    setMatrixRegs(0, m);
}

void SetLightMatrix(MATRIX* m) {
    safePoint();
    setMatrixRegs(8, m);
}

void SetColorMatrix(MATRIX* m) {
    safePoint();
    setMatrixRegs(16, m);
}

void SetTransMatrix(MATRIX* m) {
    safePoint();
    gteCtc2(5, WORD(m, 0x14));
    gteCtc2(6, WORD(m, 0x18));
    gteCtc2(7, WORD(m, 0x1C));
}

/* PushMatrix/PopMatrix: RT and TR on a 20-deep stack. */
#define MATRIX_STACK_DEPTH 20
static u32 matrixStack[MATRIX_STACK_DEPTH][8];
static int matrixStackTop;

void PushMatrix(void) {
    safePoint();
    if (matrixStackTop >= MATRIX_STACK_DEPTH) {
        /* The PS1 prints this through a disabled printf and does nothing. */
        fprintf(stderr, "libgte: Can't push matrix,stack(max 20) is full!\n");
        return;
    }
    for (int i = 0; i < 8; i++) {
        matrixStack[matrixStackTop][i] = gteCfc2(i);
    }
    matrixStackTop++;
}

void PopMatrix(void) {
    safePoint();
    if (matrixStackTop <= 0) {
        fprintf(stderr, "libgte: Can't pop matrix,stack is empty!\n");
        return;
    }
    matrixStackTop--;
    for (int i = 0; i < 8; i++) {
        gteCtc2(i, matrixStack[matrixStackTop][i]);
    }
}

/* -------------------------------------------------------- matrix products -- */

/*
 * RT * m1 column by column (the core of MulMatrix0 and its relatives): each
 * column of m1 goes into V0 and through MVMVA; out[] gets the result as the
 * five matrix words, the last one IR3 as a full (sign-extended) word.
 */
static void rtTimesColumns(MATRIX* m1, u32 out[5]) {
    gteMtc2(0, HALF(m1, 0) | (WORD(m1, 4) & 0xFFFF0000u));
    gteMtc2(1, WORD(m1, 0xC));
    gteCommand(MVMVA_RT_V0);
    u32 x0 = gteMfc2(9);
    u32 y0 = gteMfc2(10);
    u32 z0 = gteMfc2(11);
    gteMtc2(0, HALF(m1, 2) | WORD(m1, 8) << 16);
    gteMtc2(1, (u32)(s32)SHALF(m1, 0xE));
    gteCommand(MVMVA_RT_V0);
    u32 x1 = gteMfc2(9);
    u32 y1 = gteMfc2(10);
    u32 z1 = gteMfc2(11);
    gteMtc2(0, HALF(m1, 4) | (WORD(m1, 8) & 0xFFFF0000u));
    gteMtc2(1, WORD(m1, 0x10));
    gteCommand(MVMVA_RT_V0);
    out[0] = (x0 & 0xFFFF) | x1 << 16;
    out[3] = (z0 & 0xFFFF) | z1 << 16;
    out[1] = (gteMfc2(9) & 0xFFFF) | y0 << 16;
    out[2] = (y1 & 0xFFFF) | gteMfc2(10) << 16;
    out[4] = gteMfc2(11);
}

static void storeMatrixWords(MATRIX* m, const u32 w[5]) {
    for (int i = 0; i < 5; i++) {
        WORD(m, 4 * i) = w[i];
    }
}

MATRIX* MulMatrix0(MATRIX* m0, MATRIX* m1, MATRIX* m2) {
    u32 w[5];
    safePoint();
    setMatrixRegs(0, m0);
    rtTimesColumns(m1, w);
    storeMatrixWords(m2, w);
    return m2;
}

MATRIX* MulMatrix(MATRIX* m0, MATRIX* m1) {
    u32 w[5];
    safePoint();
    setMatrixRegs(0, m0);
    rtTimesColumns(m1, w);
    storeMatrixWords(m0, w);
    return m0;
}

MATRIX* MulMatrix2(MATRIX* m0, MATRIX* m1) {
    u32 w[5];
    safePoint();
    setMatrixRegs(0, m0);
    rtTimesColumns(m1, w);
    storeMatrixWords(m1, w);
    return m1;
}

/* RT = m0 * m1 (the product stays in the GTE). */
MATRIX* SetMulMatrix(MATRIX* m0, MATRIX* m1) {
    u32 w[5];
    safePoint();
    setMatrixRegs(0, m0);
    rtTimesColumns(m1, w);
    for (int i = 0; i < 5; i++) {
        gteCtc2(i, w[i]);
    }
    return m0;
}

/* m2 = m0 * m1 with translation m0.t + m0 * m1.t (m1.t taken as 16-bit, as V0). */
MATRIX* CompMatrix(MATRIX* m0, MATRIX* m1, MATRIX* m2) {
    u32 w[5];
    safePoint();
    setMatrixRegs(0, m0);
    rtTimesColumns(m1, w);
    WORD(m2, 0) = w[0];
    WORD(m2, 0xC) = w[3];
    WORD(m2, 0x10) = w[4];
    gteMtc2(0, HALF(m1, 0x14) | WORD(m1, 0x18) << 16);
    gteMtc2(1, WORD(m1, 0x1C));
    gteCommand(MVMVA_RT_V0);
    WORD(m2, 4) = w[1];
    WORD(m2, 8) = w[2];
    u32 tx = gteMfc2(25) + WORD(m0, 0x14);
    u32 ty = gteMfc2(26) + WORD(m0, 0x18);
    u32 tz = gteMfc2(27) + WORD(m0, 0x1C);
    WORD(m2, 0x14) = tx;
    WORD(m2, 0x18) = ty;
    WORD(m2, 0x1C) = tz;
    return m2;
}

MATRIX* TransposeMatrix(MATRIX* m0, MATRIX* m1) {
    safePoint();
    /* The PS1's store order, which also makes m0 == m1 work. */
    u32 a = WORD(m0, 0);
    u32 b = WORD(m0, 4);
    WORD(m1, 4) = a;
    WORD(m1, 0) = b;
    HALF(m1, 0) = (u16)a;
    u32 c = WORD(m0, 8);
    u32 d = WORD(m0, 0xC);
    WORD(m1, 0xC) = c;
    WORD(m1, 8) = d;
    HALF(m1, 0xC) = (u16)b;
    HALF(m1, 8) = (u16)c;
    u16 e = HALF(m0, 0x10);
    HALF(m1, 4) = (u16)d;
    HALF(m1, 0x10) = e;
    return m1;
}

MATRIX* TransMatrix(MATRIX* m, VECTOR* v) {
    safePoint();
    m->t[0] = v->vx;
    m->t[1] = v->vy;
    m->t[2] = v->vz;
    return m;
}

/* Element times a 32-bit factor as the PS1 does it: low 32 bits of the product, >> 12. */
static inline u32 scaled(s32 element, s32 factor) {
    return (u32)((s32)((u32)element * (u32)factor) >> 12);
}

/* Matrix elements times factors f[0..8], stored as the five words (the last
 * one a full word). */
static void scaleElements(MATRIX* m, const s32 f[9]) {
    s16* e = &m->m[0][0];
    s32 v[9];
    for (int k = 0; k < 9; k++) {
        v[k] = e[k];
    }
    for (int w = 0; w < 4; w++) {
        WORD(m, 4 * w) = (scaled(v[2 * w], f[2 * w]) & 0xFFFF) | scaled(v[2 * w + 1], f[2 * w + 1]) << 16;
    }
    WORD(m, 0x10) = scaled(v[8], f[8]);
}

/* Columns scaled by v (m = m * diag(v)). */
MATRIX* ScaleMatrix(MATRIX* m, VECTOR* v) {
    s32 f[9] = {v->vx, v->vy, v->vz, v->vx, v->vy, v->vz, v->vx, v->vy, v->vz};
    safePoint();
    scaleElements(m, f);
    return m;
}

/* Rows scaled by v (m = diag(v) * m). */
MATRIX* ScaleMatrixL(MATRIX* m, VECTOR* v) {
    s32 f[9] = {v->vx, v->vx, v->vx, v->vy, v->vy, v->vy, v->vz, v->vz, v->vz};
    safePoint();
    scaleElements(m, f);
    return m;
}

/* ------------------------------------------------------- rotation matrices -- */

static inline s32 fx(s32 a, s32 b) {
    return (s32)((u32)a * (u32)b) >> 12;
}

/* Rotation about X, applied to rows 1 and 2 of m. */
void RotMatrixX(s32 r, MATRIX* m) {
    s32 s;
    s32 c;
    safePoint();
    needTables();
    sinCos(r, &s, &c);
    for (int j = 0; j < 3; j++) {
        s32 a = m->m[1][j];
        s32 b = m->m[2][j];
        m->m[1][j] = (s16)((s32)((u32)c * a - (u32)s * b) >> 12);
        m->m[2][j] = (s16)((s32)((u32)s * a + (u32)c * b) >> 12);
    }
}

/* Rotation about Y, applied to rows 0 and 2 of m. */
MATRIX* RotMatrixY(s32 r, MATRIX* m) {
    s32 s;
    s32 c;
    safePoint();
    needTables();
    sinCos(r, &s, &c);
    for (int j = 0; j < 3; j++) {
        s32 a = m->m[0][j];
        s32 b = m->m[2][j];
        m->m[0][j] = (s16)((s32)((u32)c * a + (u32)s * b) >> 12);
        m->m[2][j] = (s16)((s32)((u32)c * b - (u32)s * a) >> 12);
    }
    return m;
}

/* Rotation about Z, applied to rows 0 and 1 of m. */
void RotMatrixZ(s32 r, MATRIX* m) {
    s32 s;
    s32 c;
    safePoint();
    needTables();
    sinCos(r, &s, &c);
    for (int j = 0; j < 3; j++) {
        s32 a = m->m[0][j];
        s32 b = m->m[1][j];
        m->m[0][j] = (s16)((s32)((u32)c * a - (u32)s * b) >> 12);
        m->m[1][j] = (s16)((s32)((u32)s * a + (u32)c * b) >> 12);
    }
}

/* m = Ry * Rx * Rz, with the PS1's rounding order. */
MATRIX* RotMatrixYXZ(SVECTOR* r, MATRIX* m) {
    s32 sx, cx, sy, cy, sz, cz;
    safePoint();
    needTables();
    sinCos(r->vx, &sx, &cx);
    sinCos(r->vy, &sy, &cy);
    sinCos(r->vz, &sz, &cz);
    s32 sysx = fx(sy, sx);
    s32 cysx = fx(cy, sx);
    m->m[1][2] = (s16)-sx;
    m->m[0][2] = (s16)fx(sy, cx);
    m->m[2][2] = (s16)fx(cy, cx);
    m->m[1][0] = (s16)fx(sz, cx);
    m->m[1][1] = (s16)fx(cz, cx);
    m->m[0][0] = (s16)(fx(cy, cz) + fx(sysx, sz));
    m->m[0][1] = (s16)(fx(sysx, cz) - fx(cy, sz));
    m->m[2][1] = (s16)(fx(sy, sz) + fx(cysx, cz));
    m->m[2][0] = (s16)(fx(cysx, sz) - fx(sy, cz));
    return m;
}

/* m = Rz * Ry * Rx, with the PS1's rounding order. */
MATRIX* RotMatrixZYX(SVECTOR* r, MATRIX* m) {
    s32 sx, cx, sy, cy, sz, cz;
    safePoint();
    needTables();
    sinCos(r->vx, &sx, &cx);
    sinCos(r->vy, &sy, &cy);
    sinCos(r->vz, &sz, &cz);
    s32 sxsy = fx(sx, sy);
    s32 sycx = fx(sy, cx);
    m->m[2][0] = (s16)-sy;
    m->m[2][1] = (s16)fx(sx, cy);
    m->m[2][2] = (s16)fx(cx, cy);
    m->m[0][0] = (s16)fx(cy, cz);
    m->m[1][0] = (s16)fx(sz, cy);
    m->m[0][1] = (s16)(fx(sxsy, cz) - fx(sz, cx));
    m->m[1][1] = (s16)(fx(sxsy, sz) + fx(cx, cz));
    m->m[0][2] = (s16)(fx(sycx, cz) + fx(sx, sz));
    m->m[1][2] = (s16)(fx(sycx, sz) - fx(sx, cz));
    return m;
}

/* ------------------------------------------------------ vector transforms -- */

VECTOR* ApplyMatrix(MATRIX* m, SVECTOR* v0, VECTOR* v1) {
    safePoint();
    setMatrixRegs(0, m);
    gte_ldv0(v0);
    gteCommand(MVMVA_RT_V0);
    gteSwc2(25, &v1->vx);
    gteSwc2(26, &v1->vy);
    gteSwc2(27, &v1->vz);
    return v1;
}

SVECTOR* ApplyMatrixSV(MATRIX* m, SVECTOR* v0, SVECTOR* v1) {
    safePoint();
    setMatrixRegs(0, m);
    gte_ldv0(v0);
    gteCommand(MVMVA_RT_V0);
    v1->vx = (s16)gteMfc2(9);
    v1->vy = (s16)gteMfc2(10);
    v1->vz = (s16)gteMfc2(11);
    return v1;
}

VECTOR* ApplyRotMatrix(SVECTOR* v0, VECTOR* v1) {
    safePoint();
    gte_ldv0(v0);
    gteCommand(MVMVA_RT_V0);
    gteSwc2(9, &v1->vx);
    gteSwc2(10, &v1->vy);
    gteSwc2(11, &v1->vz);
    return v1;
}

/* RT * a 32-bit vector, as two MVMVAs: the high parts (>> 15) unshifted, the
 * low 15 bits with sf, then high * 8 + low. Each part keeps the sign. */
static void rotateLong(VECTOR* v0, VECTOR* v1) {
    u32 hi[3];
    u32 lo[3];
    u32 in[3] = {(u32)v0->vx, (u32)v0->vy, (u32)v0->vz};
    for (int i = 0; i < 3; i++) {
        if ((s32)in[i] >= 0) {
            hi[i] = (u32)((s32)in[i] >> 15);
            lo[i] = in[i] & 0x7FFF;
        } else {
            u32 n = -in[i];
            hi[i] = -(u32)((s32)n >> 15);
            lo[i] = -(n & 0x7FFF);
        }
    }
    for (int i = 0; i < 3; i++) {
        gteMtc2(9 + i, hi[i]);
    }
    gteCommand(GTE_MVMVA(0, 0, 3, 3, 0));
    u32 h[3] = {gteMfc2(25), gteMfc2(26), gteMfc2(27)};
    for (int i = 0; i < 3; i++) {
        gteMtc2(9 + i, lo[i]);
    }
    gteCommand(GTE_MVMVA(1, 0, 3, 3, 0));
    u32 out[3];
    for (int i = 0; i < 3; i++) {
        out[i] = gteMfc2(25 + i) + h[i] * 8;
    }
    v1->vx = (s32)out[0];
    v1->vy = (s32)out[1];
    v1->vz = (s32)out[2];
}

VECTOR* ApplyMatrixLV(MATRIX* m, VECTOR* v0, VECTOR* v1) {
    safePoint();
    setMatrixRegs(0, m);
    rotateLong(v0, v1);
    return v1;
}

VECTOR* ApplyRotMatrixLV(VECTOR* v0, VECTOR* v1) {
    safePoint();
    rotateLong(v0, v1);
    return v1;
}

s32 RotTrans(SVECTOR* v0, VECTOR* v1, s32* flag) {
    safePoint();
    gte_ldv0(v0);
    gteCommand(MVMVA_RT_V0_TR);
    gteSwc2(25, &v1->vx);
    gteSwc2(26, &v1->vy);
    gteSwc2(27, &v1->vz);
    u32 f = gteCfc2(31);
    *flag = (s32)f;
    return (s32)f;
}

void RotTransSV(SVECTOR* v0, SVECTOR* v1, s32* flag) {
    safePoint();
    gte_ldv0(v0);
    gteCommand(MVMVA_RT_V0_TR);
    u32 x = gteMfc2(9);
    u32 y = gteMfc2(10);
    gteSwc2(11, &v1->vz); /* a word: vz and the pad */
    v1->vx = (s16)x;
    v1->vy = (s16)y;
    *flag = (s32)gteCfc2(31);
}

s32 RotTransPers(SVECTOR* v0, s32* sxy, s32* p, s32* flag) {
    safePoint();
    gte_ldv0(v0);
    gte_rtps();
    gteSwc2(14, sxy);
    gteSwc2(8, p);
    *flag = (s32)gteCfc2(31);
    return (s32)gteMfc2(19) >> 2;
}

s32 RotTransPers3(SVECTOR* v0, SVECTOR* v1, SVECTOR* v2, s32* sxy0, s32* sxy1, s32* sxy2, s32* p, s32* flag) {
    safePoint();
    gte_ldv3(v0, v1, v2);
    gte_rtpt();
    gteSwc2(12, sxy0);
    gteSwc2(13, sxy1);
    gteSwc2(14, sxy2);
    gteSwc2(8, p);
    *flag = (s32)gteCfc2(31);
    return (s32)gteMfc2(19) >> 2;
}

s32 RotTransPers4(SVECTOR* v0, SVECTOR* v1, SVECTOR* v2, SVECTOR* v3, s32* sxy0, s32* sxy1, s32* sxy2, s32* sxy3,
                  s32* p, s32* flag) {
    safePoint();
    gte_ldv3(v0, v1, v2);
    gte_rtpt();
    gteSwc2(12, sxy0);
    gteSwc2(13, sxy1);
    gteSwc2(14, sxy2);
    u32 f = gteCfc2(31);
    gte_ldv0(v3);
    gte_rtps();
    gteSwc2(14, sxy3);
    gteSwc2(8, p);
    *flag = (s32)(gteCfc2(31) | f);
    return (s32)gteMfc2(19) >> 2;
}

/* The game declares the screen XY pointers as s16* (they point at a primitive's x0, x1, ...);
 * each still receives a whole SXY word. */
s32 RotAverage4(SVECTOR* v0, SVECTOR* v1, SVECTOR* v2, SVECTOR* v3, s16* sxy0, s16* sxy1, s16* sxy2, s16* sxy3,
                s32* p, s32* flag) {
    safePoint();
    gte_ldv3(v0, v1, v2);
    gte_rtpt();
    gteSwc2(12, sxy0);
    gteSwc2(13, sxy1);
    gteSwc2(14, sxy2);
    u32 f = gteCfc2(31);
    gte_ldv0(v3);
    gte_rtps();
    gteSwc2(14, sxy3);
    u32 f2 = gteCfc2(31);
    gteSwc2(8, p);
    *flag = (s32)(f2 | f);
    gte_avsz4();
    return (s32)gteMfc2(7);
}

/* The quad's normal clip first: a back-facing (or edge-on) quad stops there. */
s32 RotAverageNclip4(SVECTOR* v0, SVECTOR* v1, SVECTOR* v2, SVECTOR* v3, s32* sxy0, s32* sxy1, s32* sxy2,
                     s32* sxy3, s32* p, s32* otz, s32* flag) {
    safePoint();
    gte_ldv3(v0, v1, v2);
    gte_rtpt();
    u32 f = gteCfc2(31);
    *flag = (s32)f;
    gte_nclip();
    s32 opz = (s32)gteMfc2(24);
    if (opz <= 0) {
        return opz;
    }
    gteSwc2(12, sxy0);
    gteSwc2(13, sxy1);
    gteSwc2(14, sxy2);
    gte_ldv0(v3);
    gte_rtps();
    gteSwc2(14, sxy3);
    u32 f2 = gteCfc2(31);
    gteSwc2(8, p);
    *flag = (s32)(f2 | f);
    gte_avsz4();
    *otz = (s32)gteMfc2(7);
    return opz;
}

s32 NormalClip(s32 sxy0, s32 sxy1, s32 sxy2) {
    safePoint();
    gteMtc2(12, (u32)sxy0);
    gteMtc2(14, (u32)sxy2);
    gteMtc2(13, (u32)sxy1);
    gte_nclip();
    return (s32)gteMfc2(24);
}

/* ------------------------------------------------------- vector arithmetic -- */

void Square0(VECTOR* v0, VECTOR* v1) {
    safePoint();
    gteLwc2(9, &v0->vx);
    gteLwc2(10, &v0->vy);
    gteLwc2(11, &v0->vz);
    gte_sqr0();
    gteSwc2(25, &v1->vx);
    gteSwc2(26, &v1->vy);
    gteSwc2(27, &v1->vz);
}

/* The cross product of v0 and v1 through OP, RT's diagonal restored afterwards. */
static void outerProduct(VECTOR* v0, VECTOR* v1, VECTOR* v2, u32 command) {
    u32 d0 = gteCfc2(0);
    u32 d1 = gteCfc2(2);
    u32 d2 = gteCfc2(4);
    gteCtc2(0, (u32)v0->vx);
    gteCtc2(2, (u32)v0->vy);
    gteCtc2(4, (u32)v0->vz);
    gteLwc2(11, &v1->vz);
    gteLwc2(9, &v1->vx);
    gteLwc2(10, &v1->vy);
    gteCommand(command);
    gteSwc2(25, &v2->vx);
    gteSwc2(26, &v2->vy);
    gteSwc2(27, &v2->vz);
    gteCtc2(0, d0);
    gteCtc2(2, d1);
    gteCtc2(4, d2);
}

void OuterProduct0(VECTOR* v0, VECTOR* v1, VECTOR* v2) {
    safePoint();
    outerProduct(v0, v1, v2, GTE_OP(0));
}

void OuterProduct12(VECTOR* v0, VECTOR* v1, VECTOR* v2) {
    safePoint();
    outerProduct(v0, v1, v2, GTE_OP(1));
}

s32 SquareRoot0(s32 a) {
    safePoint();
    needTables();
    gteMtc2(30, (u32)a);
    u32 lz = gteMfc2(31);
    if (lz == 32) {
        return 0;
    }
    s32 even = (s32)(lz & ~1u);
    s32 shift = (31 - even) >> 1;
    /* The top bits of a, normalised to 64..255. */
    s32 top = even >= 24 ? (s32)((u32)a << (even - 24)) : a >> (24 - even);
    return (s32)((u32)sqrtEntry(top - 64) << shift >> 12);
}

/*
 * VectorNormalInner: x/y/z (as IR1-3, so 16-bit) scaled to length 4096 with
 * the inverse square root table and GPF. Returns the sum of squares.
 */
static s32 normalize(u32* x, u32* y, u32* z) {
    gteMtc2(9, *x);
    gteMtc2(10, *y);
    gteMtc2(11, *z);
    gte_sqr0();
    u32 sum = gteMfc2(25) + gteMfc2(26) + gteMfc2(27);
    gteMtc2(30, sum);
    s32 even = (s32)(gteMfc2(31) & ~1u);
    s32 shift = (31 - even) >> 1;
    s32 top = even >= 24 ? (s32)(sum << ((even - 24) & 31)) : (s32)sum >> ((24 - even) & 31);
    gteMtc2(8, (u32)sqrtEntry(SQRT_ENTRIES + top - 64));
    gteMtc2(9, *x);
    gteMtc2(10, *y);
    gteMtc2(11, *z);
    gte_gpf0();
    /* srav takes the shift amount mod 32 (a zero vector shifts by 31). */
    *x = (u32)((s32)gteMfc2(25) >> (shift & 31));
    *y = (u32)((s32)gteMfc2(26) >> (shift & 31));
    *z = (u32)((s32)gteMfc2(27) >> (shift & 31));
    return (s32)sum;
}

s32 VectorNormal(VECTOR* v0, VECTOR* v1) {
    safePoint();
    needTables();
    u32 x = (u32)v0->vx;
    u32 y = (u32)v0->vy;
    u32 z = (u32)v0->vz;
    s32 sum = normalize(&x, &y, &z);
    v1->vx = (s32)x;
    v1->vy = (s32)y;
    v1->vz = (s32)z;
    return sum;
}

void VectorNormalS(VECTOR* v0, SVECTOR* v1) {
    safePoint();
    needTables();
    u32 x = (u32)v0->vx;
    u32 y = (u32)v0->vy;
    u32 z = (u32)v0->vz;
    normalize(&x, &y, &z);
    v1->vx = (s16)x;
    v1->vy = (s16)y;
    v1->vz = (s16)z;
}

void VectorNormalSS(SVECTOR* v0, SVECTOR* v1) {
    safePoint();
    needTables();
    u32 x = (u32)(s32)v0->vx;
    u32 y = (u32)(s32)v0->vy;
    u32 z = (u32)(s32)v0->vz;
    normalize(&x, &y, &z);
    v1->vx = (s16)x;
    v1->vy = (s16)y;
    v1->vz = (s16)z;
}

/* n = m's rows 0 and 1 and their cross products, normalised: row 0 of n is
 * (row1 x (row0 x row1)), row 1 is row 1, row 2 is row0 x row1. */
MATRIX* MatrixNormal(MATRIX* m, MATRIX* n) {
    safePoint();
    needTables();
    u32 r0[3] = {(u32)(s32)m->m[0][0], (u32)(s32)m->m[0][1], (u32)(s32)m->m[0][2]};
    u32 r1[3] = {(u32)(s32)m->m[1][0], (u32)(s32)m->m[1][1], (u32)(s32)m->m[1][2]};
    u32 d0 = gteCfc2(0);
    u32 d1 = gteCfc2(2);
    u32 d2 = gteCfc2(4);
    gteCtc2(0, r0[0]);
    gteCtc2(2, r0[1]);
    gteCtc2(4, r0[2]);
    gteMtc2(11, r1[2]);
    gteMtc2(9, r1[0]);
    gteMtc2(10, r1[1]);
    gte_op12();
    u32 c[3] = {gteMfc2(25), gteMfc2(26), gteMfc2(27)};
    gteCtc2(0, r1[0]);
    gteCtc2(2, r1[1]);
    gteCtc2(4, r1[2]);
    gte_op12();
    /* Row 1 waits in V0/V1 while the GTE normalises the first row. */
    gteMtc2(0, r1[0]);
    gteMtc2(1, r1[1]);
    gteMtc2(2, r1[2]);
    u32 a[3] = {gteMfc2(25), gteMfc2(26), gteMfc2(27)};
    gteCtc2(0, d0);
    gteCtc2(2, d1);
    gteCtc2(4, d2);
    normalize(&a[0], &a[1], &a[2]);
    n->m[0][0] = (s16)a[0];
    n->m[0][1] = (s16)a[1];
    n->m[0][2] = (s16)a[2];
    u32 b[3] = {gteMfc2(0), gteMfc2(1), gteMfc2(2)};
    normalize(&b[0], &b[1], &b[2]);
    n->m[1][0] = (s16)b[0];
    n->m[1][1] = (s16)b[1];
    n->m[1][2] = (s16)b[2];
    normalize(&c[0], &c[1], &c[2]);
    n->m[2][0] = (s16)c[0];
    n->m[2][1] = (s16)c[1];
    n->m[2][2] = (s16)c[2];
    return n;
}

/* v2 = (v0 * p0 + v1 * p1) >> 12 per component, through GPF/GPL (16-bit). */
void LoadAverageShort12(SVECTOR* v0, SVECTOR* v1, s32 p0, s32 p1, SVECTOR* v2) {
    safePoint();
    u32 w = WORD(v0, 0);
    gteMtc2(8, (u32)p0);
    gteMtc2(9, w & 0xFFFF);
    gteMtc2(10, (u32)((s32)w >> 16));
    gteMtc2(11, WORD(v0, 4) & 0xFFFF);
    gte_gpf12();
    w = WORD(v1, 0);
    gteMtc2(8, (u32)p1);
    gteMtc2(9, w & 0xFFFF);
    gteMtc2(10, (u32)((s32)w >> 16));
    gteMtc2(11, WORD(v1, 4) & 0xFFFF);
    gte_gpl12();
    u32 x = gteMfc2(9);
    u32 y = gteMfc2(10);
    WORD(v2, 0) = (x & 0xFFFF) | y << 16;
    WORD(v2, 4) = gteMfc2(11);
}

/* ------------------------------------------------------------------ lighting -- */

void NormalColor(SVECTOR* v0, CVECTOR* v1) {
    safePoint();
    gte_ldv0(v0);
    gte_ncs();
    gteSwc2(22, v1);
}

void NormalColor3(SVECTOR* v0, SVECTOR* v1, SVECTOR* v2, CVECTOR* v3, CVECTOR* v4, CVECTOR* v5) {
    safePoint();
    gte_ldv3(v0, v1, v2);
    gteCommand(GTE_NCT);
    gteSwc2(20, v3);
    gteSwc2(21, v4);
    gteSwc2(22, v5);
}

void NormalColorCol(SVECTOR* v0, CVECTOR* v1, CVECTOR* v2) {
    safePoint();
    gte_ldv0(v0);
    gte_ldrgb(v1);
    gte_nccs();
    gteSwc2(22, v2);
}

void NormalColorCol3(SVECTOR* v0, SVECTOR* v1, SVECTOR* v2, CVECTOR* v3, CVECTOR* v4, CVECTOR* v5, CVECTOR* v6) {
    safePoint();
    gte_ldv3(v0, v1, v2);
    gte_ldrgb(v3);
    gteCommand(GTE_NCCT);
    gteSwc2(20, v4);
    gteSwc2(21, v5);
    gteSwc2(22, v6);
}

/* ------------------------------------------ the library's C (src/main/psyq) -- */

/* Included under other names, so that the entry points start with a safe point
 * (and the arctangent table is not a D_ symbol of the game's). */
#define ratan2 libgteRatan2
#define SetFogNearFar libgteSetFogNearFar
#define ratanTable libgteAtanTable
#include "../../../src/main/psyq/libgte/fog_02.c"
#include "../../../src/main/psyq/libgte/ratan.c"
#undef ratan2
#undef SetFogNearFar
#undef ratanTable

s32 ratan2(s32 y, s32 x) {
    safePoint();
    return libgteRatan2(y, x);
}

void SetFogNearFar(s32 a, s32 b, s32 h) {
    safePoint();
    libgteSetFogNearFar(a, b, h);
}

/* ---- save states (system/savestate.h) ---- */

void libgteStateIo(XgState* s) {
    XG_STATE_VAR(s, matrixStack);
    XG_STATE_VAR(s, matrixStackTop);
}
