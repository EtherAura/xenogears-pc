/*
 * The GTE (geometry transformation engine, coprocessor 2) in software, at the
 * register level: the seven entry points of include/gte_pc.h, which the game's
 * GTE macros and the LIBGTE functions (libgte.c) use in the order the PS1 code
 * issues lwc2/swc2/mtc2/mfc2/ctc2/cfc2 and commands.
 *
 * Written from psx-spx, chapter "Geometry Transformation Engine (GTE)"; the
 * comments cite its section names. Where psx-spx is silent or ambiguous the
 * behaviour was settled by black-box measurement: register results a PS1
 * emulator recorded for random register files (every command with its sf/lm
 * variants, all MVMVA forms, every register write) and for the game's own
 * LIBGTE calls, compared register by register with this code. Those points
 * are marked "Measured:".
 *
 * Kept from the earlier file: the register indices and FLAG bits (psx-spx),
 * the power-on register state (measured), gteSave/gteLoad, the UNR division
 * (psx-spx "GTE Division Inaccuracy"), gteCfc2/gteLwc2/gteSwc2 and the
 * geometry precision hooks (port feature).
 */

#include "gte_pc.h"
#include "../system/savestate.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* Data registers (cop2r0-31). */
enum {
    D_VXY0 = 0,
    D_VZ0 = 1,
    D_RGBC = 6,
    D_OTZ = 7,
    D_IR0 = 8,
    D_IR1 = 9,
    D_SXY0 = 12,
    D_SXY1 = 13,
    D_SXY2 = 14,
    D_SXYP = 15,
    D_SZ0 = 16,
    D_RGB0 = 20,
    D_RGB1 = 21,
    D_RGB2 = 22,
    D_MAC0 = 24,
    D_MAC1 = 25,
    D_IRGB = 28,
    D_ORGB = 29,
    D_LZCS = 30,
    D_LZCR = 31,
};

/* Control registers (cop2r32-63). Matrices are 5 words: RT at 0, LLM at 8, LCM at 16;
 * translation vectors 3 words: TR at 5, BK at 13, FC at 21. */
enum {
    C_RT = 0,
    C_TR = 5,
    C_LLM = 8,
    C_BK = 13,
    C_LCM = 16,
    C_FC = 21,
    C_OFX = 24,
    C_OFY = 25,
    C_H = 26,
    C_DQA = 27,
    C_DQB = 28,
    C_ZSF3 = 29,
    C_ZSF4 = 30,
    C_FLAG = 31,
};

/* FLAG bits. */
#define F_MAC_POS(i) (1u << (30 - (i))) /* MAC1-3 above 43 bits */
#define F_MAC_NEG(i) (1u << (27 - (i))) /* MAC1-3 below -2^43 */
#define F_IR(i) (1u << (24 - (i)))      /* IR1-3 saturated */
#define F_COLOR(i) (1u << (21 - (i)))   /* colour FIFO R/G/B saturated */
#define F_SZ 0x40000u                   /* SZ3/OTZ saturated */
#define F_DIV 0x20000u                  /* RTPS/RTPT division overflow */
#define F_MAC0_POS 0x10000u
#define F_MAC0_NEG 0x8000u
#define F_SX 0x4000u
#define F_SY 0x2000u
#define F_IR0 0x1000u
#define F_ERROR_BITS 0x7F87E000u /* bits 30-23 and 18-13 set bit 31 */
#define F_ERROR 0x80000000u

/* Power-on state: what the BIOS leaves in the GTE when it starts the game's
 * executable, after its boot intro drew the 3D logo (PCSX-Redux, SCPH-5501,
 * at the executable's entry point). The game never clears the rotation,
 * light, colour or back-colour registers: the first lit models it sets up
 * before it loads lighting of its own use them (a field started directly, or
 * the first field after Continue: the sand cruiser of field 182 is lit by the
 * back colour 0x640, 100 per channel, not black). */
static u32 dataRegs[32] = {
    0x00000B50, 0x00000B50, 0xFDABFF8D, 0x0000FE51, 0xFDAB000A, 0x0000FDD4, 0x200808FF, 0x00000509,
    0x000000A4, 0x0000104A, 0x00000082, 0x00000082, 0x0041012D, 0x0063012D, 0x00690146, 0x00000000,
    0x00001441, 0x00001441, 0x00001486, 0x00001419, 0x200808FF, 0x200808FF, 0x200808FF, 0x00000000,
    0xFFFFFCAE, 0x0000104A, 0x00000082, 0x00000082, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
};
static u32 ctrlRegs[32] = {
    0x00000FFB, 0xFFB7FF44, 0xF9CA0EBC, 0x063700AD, 0x00000EB7, 0x00000000, 0xFFFFFEAC, 0x00001700,
    0x00000000, 0x00000FA0, 0x0000F060, 0x0000F060, 0x00000000, 0x00000640, 0x00000640, 0x00000640,
    0x0BB80FA0, 0x0FA00FA0, 0x0FA00BB8, 0x0BB80FA0, 0x00000FA0, 0x00000000, 0x00000000, 0x00000000,
    0x01400000, 0x00F00000, 0x00000400, 0xFFFFF9E9, 0x01400000, 0x00000155, 0x00000100, 0x00600000,
};
static u32 flag; /* FLAG while a command runs */
static GtePrecise precise[3];

void gteSave(GteState* s) {
    memcpy(s->data, dataRegs, sizeof(dataRegs));
    memcpy(s->ctrl, ctrlRegs, sizeof(ctrlRegs));
    memcpy(s->precise, precise, sizeof(precise));
}

void gteLoad(const GteState* s) {
    memcpy(dataRegs, s->data, sizeof(dataRegs));
    memcpy(ctrlRegs, s->ctrl, sizeof(ctrlRegs));
    memcpy(precise, s->precise, sizeof(precise));
}

static void precisionDataWrite(int reg);

/* ----------------------------------------------------------- register words -- */

/* The signed 16-bit halves of a register word: bits 0-15 and bits 16-31. */
static s32 halfLo(u32 word) {
    return (s16)(u16)word;
}

static s32 halfHi(u32 word) {
    return (s16)(u16)(word >> 16);
}

/* x held to lo..hi; when it had to be, the FLAG bits in raise are set (the
 * FLAG of the running command; none for raise = 0). */
static s64 clampFlag(s64 x, s64 lo, s64 hi, u32 raise) {
    if (x < lo || x > hi) {
        flag |= raise;
        return x < lo ? lo : hi;
    }
    return x;
}

/* cop2r29 ORGB: IR1, IR2, IR3 divided by 80h, each held to 00h..1Fh, as
 * bits 0-4, 5-9 and 10-14; no FLAG bit (psx-spx "cop2r29 - ORGB"). Reading
 * cop2r28 IRGB returns the same: "ORGB is simply a read-only mirror of
 * IRGB", and changes to IR1-3 show in both. */
static u32 orgbOf(void) {
    u32 packed = 0;
    for (int i = 0; i < 3; i++) {
        packed |= (u32)clampFlag(halfLo(dataRegs[D_IR1 + i]) >> 7, 0, 0x1F, 0) << (5 * i);
    }
    return packed;
}

/* cop2r31 LZCR: the number of leading bits equal to LZCS's sign bit, 1..32
 * (psx-spx "cop2r30 - LZCS"). Worked out when LZCS is written and stored:
 * LZCR reads what was stored then (or loaded, or set at power-on, where it
 * is 0 with LZCS 0). */
static u32 lzcrOf(u32 lzcs) {
    u32 x = (lzcs & 0x80000000u) ? ~lzcs : lzcs;
    u32 run = 0;
    while (run < 32 && !(x & (0x80000000u >> run))) {
        run++;
    }
    return run;
}

/* Data register writes (psx-spx "GTE Data Register Summary" and the sections
 * of each register). The 16-bit registers keep only their 16 bits, sign- or
 * zero-extended as a read returns them; no saturation and no FLAG ("GTE
 * Saturation": writing 32-bit values to 16-bit registers). */
void gteMtc2(int reg, u32 value) {
    reg &= 31;
    switch (reg) {
        case D_VZ0:
        case D_VZ0 + 2:
        case D_VZ0 + 4:
        case D_IR0:
        case D_IR1:
        case D_IR1 + 1:
        case D_IR1 + 2:
            dataRegs[reg] = (u32)halfLo(value);
            break;
        case D_OTZ:
        case D_SZ0:
        case D_SZ0 + 1:
        case D_SZ0 + 2:
        case D_SZ0 + 3:
            dataRegs[reg] = value & 0xFFFF;
            break;
        case D_SXYP: /* "Screen XYZ Coordinate FIFOs": move-on-write */
            dataRegs[D_SXY0] = dataRegs[D_SXY1];
            dataRegs[D_SXY1] = dataRegs[D_SXY2];
            dataRegs[D_SXY2] = value;
            break;
        case D_IRGB: /* "cop2r28 - IRGB": 5:5:5 to IR1-3, times 80h */
            for (int i = 0; i < 3; i++) {
                dataRegs[D_IR1 + i] = ((value >> (5 * i)) & 0x1F) << 7;
            }
            break;
        case D_LZCS:
            dataRegs[D_LZCS] = value;
            dataRegs[D_LZCR] = lzcrOf(value);
            break;
        case D_ORGB:
        case D_LZCR: /* read-only */
            break;
        default:
            dataRegs[reg] = value;
            break;
    }
    precisionDataWrite(reg);
}

/* Data register reads. SXYP reads as SXY2; IRGB and ORGB are views of IR1-3,
 * worked out when read (their own words in dataRegs are not used). */
u32 gteMfc2(int reg) {
    reg &= 31;
    switch (reg) {
        case D_SXYP:
            return dataRegs[D_SXY2];
        case D_IRGB:
        case D_ORGB:
            return orgbOf();
        default:
            return dataRegs[reg];
    }
}

/* Control register writes (psx-spx "GTE Control Register Summary"). RT33,
 * L33, LB3, DQA, ZSF3 and ZSF4 are 16-bit and read back sign-extended
 * ("Matrix Registers"); so is H, although RTPS/RTPT use it unsigned ("Screen
 * Offset and Distance"). FLAG keeps bits 30-12 and works out bit 31 ("GTE
 * Saturation"). */
void gteCtc2(int reg, u32 value) {
    reg &= 31;
    switch (reg) {
        case C_RT + 4:
        case C_LLM + 4:
        case C_LCM + 4:
        case C_H:
        case C_DQA:
        case C_ZSF3:
        case C_ZSF4:
            value = (u32)halfLo(value);
            break;
        case C_FLAG:
            value &= 0x7FFFF000u;
            if (value & F_ERROR_BITS) {
                value |= F_ERROR;
            }
            break;
        default:
            break;
    }
    ctrlRegs[reg] = value;
}

u32 gteCfc2(int reg) {
    return ctrlRegs[reg & 31];
}

void gteLwc2(int reg, const void* addr) {
    u32 value;
    memcpy(&value, addr, 4);
    gteMtc2(reg, value);
}

void gteSwc2(int reg, void* addr) {
    u32 value = gteMfc2(reg);
    memcpy(addr, &value, 4);
    reg &= 31;
    if (reg >= D_SXY0 && reg <= D_SXYP) {
        gtePrecisionRecord(addr, &precise[reg == D_SXYP ? 2 : reg - D_SXY0]);
    } else {
        gtePrecisionForget(addr);
    }
}

/* --------------------------------------------------------------- division -- */

/* UNR reciprocal table (psx-spx): max(0, (0x40000 / (i + 0x100) + 1) / 2 - 0x101). */
static u8 unrTable[0x101];
static int unrReady;

static void initUnr(void) {
    for (int i = 0; i <= 0x100; i++) {
        int v = (0x40000 / (i + 0x100) + 1) / 2 - 0x101;
        unrTable[i] = (u8)(v < 0 ? 0 : v);
    }
    unrReady = 1;
}

/* H / SZ3 as RTPS/RTPT compute it: 1.16 fixed point, at most 0x1FFFF. */
static u32 divideUnr(u32 h, u32 sz3) {
    if (h >= sz3 * 2) {
        flag |= F_DIV;
        return 0x1FFFF;
    }
    if (!unrReady) {
        initUnr();
    }
    int z = 0;
    while (!(sz3 & (0x8000u >> z))) { /* sz3 > h / 2 >= 0, so it is not zero */
        z++;
    }
    u32 n = h << z;
    u32 d = sz3 << z;
    u32 u = unrTable[(d - 0x7FC0) >> 7] + 0x101;
    d = (0x2000080 - d * u) >> 8;
    d = (0x0000080 + d * u) >> 8;
    u64 q = ((u64)n * d + 0x8000) >> 16;
    return q > 0x1FFFF ? 0x1FFFF : (u32)q;
}

/* ------------------------------------------------------ geometry precision -- */

/* Port feature (not GTE behaviour): the fractional screen position of each
 * projected vertex, for the enhanced display (precision.c, include/gte_pc.h).
 * precise[0..2] follows the SXY0-2 FIFO. These hooks never change a register
 * or FLAG: turning precision on must leave every register bit-identical. */

/* Data register write hook, for every write to a data register (mtc2, lwc2):
 * a write to SXYP moves the metadata with the FIFO, a direct write to SXY0-2
 * invalidates that entry. */
static void precisionDataWrite(int reg) {
    reg &= 31;
    if (reg == D_SXYP) {
        precise[0] = precise[1];
        precise[1] = precise[2];
        precise[2].valid = 0;
    } else if (reg >= D_SXY0 && reg <= D_SXY2) {
        precise[reg - D_SXY0].valid = 0;
    }
}

/* RTPS/RTPT hook, once per vertex, right after the vertex's screen XY word has
 * been pushed into SXY2. It moves the metadata with the FIFO and, when the
 * vertex can be trusted, records its fractional position in precise[2]:
 *   sxy2      the word just pushed into SXY2
 *   row[3]    the vertex's three sums (TR * 1000h + RT row * V) at the full
 *             width of the MAC1-3 accumulators, before the sf shift
 *   sx, sy    the full-width screen sums (n * IR1 + OFX, n * IR2 + OFY, n the
 *             division result), before the division by 10000h
 *   shift     12 when the command's sf bit is set, else 0
 *   flags     the FLAG bits this command has raised so far (any MAC1-3
 *             overflow rejects the vertex)
 *   ir1, ir2  IR1 and IR2 as stored for this vertex (sign-extended)
 *   h         H as the division uses it (unsigned 16 bits)
 *   sz3       SZ3 as stored for this vertex
 *   ofx, ofy  OFX and OFY */
static void precisionProjected(u32 sxy2, const s64 row[3], s64 sx, s64 sy, int shift, u32 flags, s32 ir1, s32 ir2,
                               u32 h, u32 sz3, s32 ofx, s32 ofy) {
    s64 depth = row[2] >> 12;
    precise[0] = precise[1];
    precise[1] = precise[2];
    GtePrecise* p = &precise[2];
    p->word = sxy2;
    p->valid = 0;
    if (gtePrecisionEnabled() && shift == 12 && depth > 0 && depth <= 65535 &&
        !(flags & (F_MAC_POS(0) | F_MAC_POS(1) | F_MAC_POS(2) | F_MAC_NEG(0) | F_MAC_NEG(1) | F_MAC_NEG(2))) &&
        h < 2 * sz3 && (sx >> 16) >= -1024 && (sx >> 16) <= 1023 && (sy >> 16) >= -1024 && (sy >> 16) <= 1023 &&
        (row[0] >> 12) == ir1 && (row[1] >> 12) == ir2) {
        p->z = (double)row[2] / 4096.0;
        double ratio = (double)h / p->z;
        p->x = (double)ofx / 65536.0 + (double)row[0] / 4096.0 * ratio;
        p->y = (double)ofy / 65536.0 + (double)row[1] / 4096.0 * ratio;
        double dx = p->x - (s16)(u16)p->word, dy = p->y - (s16)(u16)(p->word >> 16);
        p->valid = dx >= -4 && dx <= 4 && dy >= -4 && dy <= 4;
    }
}

int gtePrecisionNclip(double* area) {
    if (!gtePrecisionEnabled()) return 0;
    for (int i = 0; i < 3; i++) {
        if (!precise[i].valid || precise[i].word != dataRegs[D_SXY0 + i]) return 0;
    }
    *area = (precise[1].x - precise[0].x) * (precise[2].y - precise[0].y) -
            (precise[1].y - precise[0].y) * (precise[2].x - precise[0].x);
    return 1;
}

/* ------------------------------------------------------ the arithmetic unit -- */

/* The fields of the running command (psx-spx "GTE Command Encoding"). */
static int cmdShift; /* sf * 12: the SAR on MAC1-3 results */
static s32 cmdIrMin; /* lm: IR1-3 lower limit, 0 (lm=1) or -8000h (lm=0) */

/* The sums behind MAC1-3 are 44 bits wide; FLAG 30-28/27-25 tell a sum that
 * left that range ("GTE Saturation": "larger than 43 bits"). lane 0-2 is
 * MAC1-3. Measured: the range is checked after every addition (translation
 * first, then the products in column order), and the sum wraps to 44 bits,
 * so one sum can raise both of its bits. */
#define MAC44_MAX ((s64)0x7FFFFFFFFFF)
#define MAC44_MIN (-(s64)0x80000000000)

static s64 mac44Add(int lane, s64 acc, s64 term) {
    s64 sum = acc + term;
    if (sum > MAC44_MAX) {
        flag |= F_MAC_POS(lane);
    } else if (sum < MAC44_MIN) {
        flag |= F_MAC_NEG(lane);
    }
    return (s64)((u64)sum << 20) >> 20;
}

/* IR1-3 (lane 0-2) from a MAC value, held to lowest..7FFFh with FLAG 24-22. */
static s32 irClampLane(int lane, s32 mac, s32 lowest) {
    return (s32)clampFlag(mac, lowest, 0x7FFF, F_IR(lane));
}

/* MAC1-3 = sums SAR (sf*12), and [IR1,IR2,IR3] = [MAC1,MAC2,MAC3] within the
 * lm range: the last line of most command formulas. Measured: IR1-3 come
 * from MAC1-3 as stored, the shifted sum cut to 32 bits, not from the 44-bit
 * sum (a sum past 32 bits can store a positive IR from a negative sum). */
static void macIrCommit(const s64 sums[3]) {
    for (int lane = 0; lane < 3; lane++) {
        s32 mac = (s32)(sums[lane] >> cmdShift);
        dataRegs[D_MAC1 + lane] = (u32)mac;
        dataRegs[D_IR1 + lane] = (u32)irClampLane(lane, mac, cmdIrMin);
    }
}

/* MAC0 = x, with FLAG 16/15 when x needs more than 32 bits. */
static s64 mac0Commit(s64 x) {
    if (x > INT32_MAX) {
        flag |= F_MAC0_POS;
    } else if (x < INT32_MIN) {
        flag |= F_MAC0_NEG;
    }
    dataRegs[D_MAC0] = (u32)x;
    return x;
}

/* A 3x3 matrix of the control registers, row by row ("Matrix Registers"). */
static void matrixElems(int base, s32 m[9]) {
    for (int k = 0; k < 9; k++) {
        u32 word = ctrlRegs[base + k / 2];
        m[k] = (k & 1) ? halfHi(word) : halfLo(word);
    }
}

/* V0, V1, V2 or [IR1,IR2,IR3] (which = 3) ("16bit Vectors"). */
static void vectorElems(int which, s32 v[3]) {
    if (which == 3) {
        for (int i = 0; i < 3; i++) {
            v[i] = halfLo(dataRegs[D_IR1 + i]);
        }
    } else {
        u32 xy = dataRegs[D_VXY0 + 2 * which];
        v[0] = halfLo(xy);
        v[1] = halfHi(xy);
        v[2] = halfLo(dataRegs[D_VZ0 + 2 * which]);
    }
}

/* TR, BK or FC times 1000h, the base of a matrix product. */
static void offsetScaled(int base, s64 t[3]) {
    for (int i = 0; i < 3; i++) {
        t[i] = (s64)(s32)ctrlRegs[base + i] * 0x1000;
    }
}

/* out = t + M * v, each lane accumulated term by term. */
static void affineSum(const s32 m[9], const s32 v[3], const s64 t[3], s64 out[3]) {
    for (int lane = 0; lane < 3; lane++) {
        s64 acc = t[lane];
        for (int c = 0; c < 3; c++) {
            acc = mac44Add(lane, acc, (s64)m[3 * lane + c] * v[c]);
        }
        out[lane] = acc;
    }
}

/* ------------------------------------------------------- the colour stages -- */

/* The R, G, B bytes of a colour word. */
static void rgbBytes(u32 rgb, s32 c[3]) {
    for (int i = 0; i < 3; i++) {
        c[i] = (rgb >> (8 * i)) & 0xFF;
    }
}

/* "Color FIFO = [MAC1/16,MAC2/16,MAC3/16,CODE]": each byte held to 00h..FFh
 * with FLAG 21-19, CODE from RGBC; the older entries move down (psx-spx GTE
 * Color Calculation Commands, "Notes"). Measured: /16 rounds down (SAR 4). */
static void rgbFifoPush(void) {
    u32 rgb = dataRegs[D_RGBC] & 0xFF000000u;
    for (int i = 0; i < 3; i++) {
        rgb |= (u32)clampFlag((s32)dataRegs[D_MAC1 + i] >> 4, 0, 0xFF, F_COLOR(i)) << (8 * i);
    }
    dataRegs[D_RGB0] = dataRegs[D_RGB1];
    dataRegs[D_RGB1] = dataRegs[D_RGB2];
    dataRegs[D_RGB2] = rgb;
}

/* "[IR1,IR2,IR3] = [MAC1,MAC2,MAC3] = (LLM*V) SAR (sf*12)" */
static void llmTimesV(int vector) {
    s32 m[9], v[3];
    s64 zero[3] = {0, 0, 0}, sums[3];
    matrixElems(C_LLM, m);
    vectorElems(vector, v);
    affineSum(m, v, zero, sums);
    macIrCommit(sums);
}

/* "[IR1,IR2,IR3] = [MAC1,MAC2,MAC3] = (BK*1000h + LCM*IR) SAR (sf*12)" */
static void bkPlusLcmTimesIr(void) {
    s32 m[9], v[3];
    s64 bk[3], sums[3];
    matrixElems(C_LCM, m);
    vectorElems(3, v);
    offsetScaled(C_BK, bk);
    affineSum(m, v, bk, sums);
    macIrCommit(sums);
}

/* "[MAC1,MAC2,MAC3] = [R*IR1,G*IR2,B*IR3] SHL 4", unshifted. */
static void rgbTimesIr(u32 rgb, s64 out[3]) {
    s32 c[3];
    rgbBytes(rgb, c);
    for (int i = 0; i < 3; i++) {
        out[i] = (s64)c[i] * halfLo(dataRegs[D_IR1 + i]) * 16;
    }
}

/* The end of every colour command: optionally "MAC+(FC-MAC)*IR0" (psx-spx
 * "Details on MAC+(FC-MAC)*IR0", whose first IR1-3 is held as if lm=0), then
 * "[MAC1,MAC2,MAC3] = [MAC1,MAC2,MAC3] SAR (sf*12)", IR1-3 and the colour
 * FIFO. sums are the unshifted MAC values going in. Measured: like every
 * IR1-3, the first one comes from the shifted difference cut to 32 bits. */
static void rgbFinish(s64 sums[3], int towardFar) {
    if (towardFar) {
        s32 ir0 = halfLo(dataRegs[D_IR0]);
        for (int lane = 0; lane < 3; lane++) {
            s64 far = (s64)(s32)ctrlRegs[C_FC + lane] * 0x1000;
            s32 gap = (s32)(mac44Add(lane, far, -sums[lane]) >> cmdShift);
            s32 step = irClampLane(lane, gap, -0x8000);
            sums[lane] = mac44Add(lane, sums[lane], (s64)step * ir0);
        }
    }
    macIrCommit(sums);
    rgbFifoPush();
}

/* ---------------------------------------------------------------- commands -- */

/* RTPS (V0) and RTPT (V0, V1, V2): psx-spx "GTE Coordinate Calculation
 * Commands". */
static void runRtp(int vertices) {
    s32 rt[9];
    s64 tr[3];
    matrixElems(C_RT, rt);
    offsetScaled(C_TR, tr);
    u32 h = ctrlRegs[C_H] & 0xFFFF;
    s32 ofx = (s32)ctrlRegs[C_OFX], ofy = (s32)ctrlRegs[C_OFY];
    s32 dqa = halfLo(ctrlRegs[C_DQA]);
    s32 dqb = (s32)ctrlRegs[C_DQB];
    for (int k = 0; k < vertices; k++) {
        s32 v[3];
        s64 sums[3];
        vectorElems(k, v);
        affineSum(rt, v, tr, sums);
        /* IR1 and IR2 as usual. IR3 is stored per lm, but its FLAG bit tests
         * "MAC3 SAR 12" against -8000h..7FFFh whatever sf and lm say
         * ("GTE Saturation"; "RTPS/RTPT", Note). Measured: that test, and
         * SZ3, take the 44-bit sum SAR 12, not MAC3 cut to 32 bits. */
        for (int lane = 0; lane < 3; lane++) {
            s32 mac = (s32)(sums[lane] >> cmdShift);
            dataRegs[D_MAC1 + lane] = (u32)mac;
            dataRegs[D_IR1 + lane] = (u32)clampFlag(mac, cmdIrMin, 0x7FFF, lane < 2 ? F_IR(lane) : 0);
        }
        clampFlag(sums[2] >> 12, -0x8000, 0x7FFF, F_IR(2));
        /* "SZ3 = MAC3 SAR ((1-sf)*12)", 0..FFFFh, into the Z FIFO. */
        u32 sz = (u32)clampFlag(sums[2] >> 12, 0, 0xFFFF, F_SZ);
        for (int i = 0; i < 3; i++) {
            dataRegs[D_SZ0 + i] = dataRegs[D_SZ0 + i + 1];
        }
        dataRegs[D_SZ0 + 3] = sz;
        /* The screen position: n = H/SZ3 by the UNR division, then
         * MAC0 = n*IR1 + OFX, SX2 = MAC0/10000h (and Y likewise). Measured:
         * the division by 10000h is a SAR of the full-width sum, before MAC0
         * is cut to 32 bits; the same holds for IR0 and OTZ below. */
        u32 n = divideUnr(h, sz);
        s32 ir1 = halfLo(dataRegs[D_IR1]), ir2 = halfLo(dataRegs[D_IR1 + 1]);
        s64 sx = mac0Commit((s64)n * ir1 + ofx);
        s64 sy = mac0Commit((s64)n * ir2 + ofy);
        s32 x = (s32)clampFlag(sx >> 16, -0x400, 0x3FF, F_SX);
        s32 y = (s32)clampFlag(sy >> 16, -0x400, 0x3FF, F_SY);
        dataRegs[D_SXY0] = dataRegs[D_SXY1];
        dataRegs[D_SXY1] = dataRegs[D_SXY2];
        dataRegs[D_SXY2] = ((u32)y << 16) | ((u32)x & 0xFFFF);
        precisionProjected(dataRegs[D_SXY2], sums, sx, sy, cmdShift, flag, ir1, ir2, h, sz, ofx, ofy);
        /* Depth cueing: MAC0 = n*DQA + DQB, IR0 = MAC0/1000h, 0..1000h.
         * Measured: RTPT does this for V2 only (no FLAG bit comes from the
         * depth cue of V0 or V1). */
        if (k == vertices - 1) {
            s64 dq = mac0Commit((s64)n * dqa + dqb);
            dataRegs[D_IR0] = (u32)clampFlag(dq >> 12, 0, 0x1000, F_IR0);
        }
    }
}

/* NCLIP: the signed area of the SXY0-2 triangle, times two, into MAC0. */
static void runNclip(void) {
    s32 x[3], y[3];
    for (int i = 0; i < 3; i++) {
        x[i] = halfLo(dataRegs[D_SXY0 + i]);
        y[i] = halfHi(dataRegs[D_SXY0 + i]);
    }
    mac0Commit((s64)x[0] * y[1] + (s64)x[1] * y[2] + (s64)x[2] * y[0] - (s64)x[0] * y[2] - (s64)x[1] * y[0] -
           (s64)x[2] * y[1]);
}

/* AVSZ3 (SZ1-3 times ZSF3) and AVSZ4 (SZ0-3 times ZSF4): MAC0, then
 * OTZ = MAC0/1000h within 0..FFFFh ("Average Z Registers"). */
static void runAvsz(int count) {
    s64 total = 0;
    for (int i = 4 - count; i < 4; i++) {
        total += dataRegs[D_SZ0 + i] & 0xFFFF;
    }
    s32 scale = halfLo(ctrlRegs[count == 3 ? C_ZSF3 : C_ZSF4]);
    s64 mac = mac0Commit(total * scale);
    dataRegs[D_OTZ] = (u32)clampFlag(mac >> 12, 0, 0xFFFF, F_SZ);
}

/* MVMVA: matrix mx times vector v plus translation cv (psx-spx "MVMVA"). */
static void runMvmva(u32 command) {
    int mx = (command >> 17) & 3, vec = (command >> 15) & 3, cv = (command >> 13) & 3;
    s32 m[9], v[3];
    s64 t[3] = {0, 0, 0}, sums[3];
    if (mx == 3) {
        /* The reserved matrix: -R*10h, +R*10h, IR0, RT13 x3, RT22 x3. */
        s32 r = (s32)(dataRegs[D_RGBC] & 0xFF) * 16;
        s32 rt13 = halfLo(ctrlRegs[C_RT + 1]), rt22 = halfLo(ctrlRegs[C_RT + 2]);
        const s32 reserved[9] = {-r, r, halfLo(dataRegs[D_IR0]), rt13, rt13, rt13, rt22, rt22, rt22};
        memcpy(m, reserved, sizeof m);
    } else {
        matrixElems(C_RT + 8 * mx, m);
    }
    vectorElems(vec, v);
    if (cv != 3) {
        offsetScaled(C_TR + 8 * cv, t);
    }
    if (cv == 2) {
        /* FC as translation: only the last two products reach MAC1-3, but
         * "some bits in the FLAG register" come from the full sum. Measured:
         * the first part, FC*1000h + Mx11*Vx1 (each row), raises its MAC
         * FLAG bits and, shifted by sf and cut to 32 bits, an IR FLAG bit
         * tested against -8000h..7FFFh whatever lm says. */
        for (int lane = 0; lane < 3; lane++) {
            s64 first = mac44Add(lane, t[lane], (s64)m[3 * lane] * v[0]);
            irClampLane(lane, (s32)(first >> cmdShift), -0x8000);
            s64 acc = mac44Add(lane, 0, (s64)m[3 * lane + 1] * v[1]);
            sums[lane] = mac44Add(lane, acc, (s64)m[3 * lane + 2] * v[2]);
        }
    } else {
        affineSum(m, v, t, sums);
    }
    macIrCommit(sums);
}

/* SQR: [IR1,IR2,IR3] squared. */
static void runSqr(void) {
    s64 sums[3];
    for (int lane = 0; lane < 3; lane++) {
        s32 ir = halfLo(dataRegs[D_IR1 + lane]);
        sums[lane] = (s64)ir * ir;
    }
    macIrCommit(sums);
}

/* OP: [IR1,IR2,IR3] cross [RT11,RT22,RT33]. */
static void runOp(void) {
    s32 d[3], ir[3];
    for (int i = 0; i < 3; i++) {
        d[i] = halfLo(ctrlRegs[C_RT + 2 * i]);
        ir[i] = halfLo(dataRegs[D_IR1 + i]);
    }
    s64 sums[3];
    for (int lane = 0; lane < 3; lane++) {
        int a = (lane + 1) % 3, b = (lane + 2) % 3; /* MAC1 = IR3*D2 - IR2*D3, ... */
        sums[lane] = mac44Add(lane, (s64)ir[b] * d[a], -(s64)ir[a] * d[b]);
    }
    macIrCommit(sums);
}

/* How the normal colour commands end, after the two light stages. */
enum {
    NCX_LIT,          /* NCS/NCT: the light colour itself */
    NCX_TINT,     /* NCCS/NCCT: times RGBC */
    NCX_TINT_CUE, /* NCDS/NCDT: times RGBC, then toward FC */
};

/* NCS, NCT, NCCS, NCCT, NCDS, NCDT: psx-spx "GTE Color Calculation
 * Commands", with V0 (and V1, V2 for the triple forms). */
static void runNcx(int vertices, int finish) {
    for (int k = 0; k < vertices; k++) {
        llmTimesV(k);
        bkPlusLcmTimesIr();
        if (finish == NCX_LIT) {
            rgbFifoPush();
        } else {
            s64 sums[3];
            rgbTimesIr(dataRegs[D_RGBC], sums);
            rgbFinish(sums, finish == NCX_TINT_CUE);
        }
    }
}

/* CC (towardFar 0) and CDP (1). */
static void runCcCdp(int towardFar) {
    s64 sums[3];
    bkPlusLcmTimesIr();
    rgbTimesIr(dataRegs[D_RGBC], sums);
    rgbFinish(sums, towardFar);
}

/* DPCS and DPCT: [R,G,B] SHL 16 toward FC; DPCT takes R,G,B from RGB0 each
 * time, CODE from RGBC. */
static void runDpcsDpct(int times) {
    for (int k = 0; k < times; k++) {
        s32 c[3];
        s64 sums[3];
        rgbBytes(dataRegs[times == 1 ? D_RGBC : D_RGB0], c);
        for (int i = 0; i < 3; i++) {
            sums[i] = (s64)c[i] << 16;
        }
        rgbFinish(sums, 1);
    }
}

/* DCPL: [R*IR1,G*IR2,B*IR3] SHL 4 toward FC. */
static void runDcpl(void) {
    s64 sums[3];
    rgbTimesIr(dataRegs[D_RGBC], sums);
    rgbFinish(sums, 1);
}

/* INTPL: [IR1,IR2,IR3] SHL 12 toward FC. */
static void runIntpl(void) {
    s64 sums[3];
    for (int i = 0; i < 3; i++) {
        sums[i] = (s64)halfLo(dataRegs[D_IR1 + i]) * 0x1000;
    }
    rgbFinish(sums, 1);
}

/* GPF (withBase 0): [IR1,IR2,IR3] * IR0; GPL (1): the same plus
 * [MAC1,MAC2,MAC3] SHL (sf*12). */
static void runGpfGpl(int withBase) {
    s32 ir0 = halfLo(dataRegs[D_IR0]);
    s64 sums[3];
    for (int lane = 0; lane < 3; lane++) {
        s64 base = withBase ? (s64)(s32)dataRegs[D_MAC1 + lane] * ((s64)1 << cmdShift) : 0;
        sums[lane] = mac44Add(lane, base, (s64)halfLo(dataRegs[D_IR1 + lane]) * ir0);
    }
    rgbFinish(sums, 0);
}

void gteCommand(u32 command) {
    flag = 0; /* "all bits are automatically reset at the begin of a new GTE command" */
    cmdShift = (command >> 19) & 1 ? 12 : 0;
    cmdIrMin = (command >> 10) & 1 ? 0 : -0x8000;
    switch (command & 0x3F) { /* the real command number ("GTE Command Summary") */
        case 0x01: runRtp(1); break;
        case 0x06: runNclip(); break;
        case 0x0C: runOp(); break;
        case 0x10: runDpcsDpct(1); break;
        case 0x11: runIntpl(); break;
        case 0x12: runMvmva(command); break;
        case 0x13: runNcx(1, NCX_TINT_CUE); break;
        case 0x14: runCcCdp(1); break;
        case 0x16: runNcx(3, NCX_TINT_CUE); break;
        case 0x1B: runNcx(1, NCX_TINT); break;
        case 0x1C: runCcCdp(0); break;
        case 0x1E: runNcx(1, NCX_LIT); break;
        case 0x20: runNcx(3, NCX_LIT); break;
        case 0x28: runSqr(); break;
        case 0x29: runDcpl(); break;
        case 0x2A: runDpcsDpct(3); break;
        case 0x2D: runAvsz(3); break;
        case 0x2E: runAvsz(4); break;
        case 0x30: runRtp(3); break;
        case 0x3D: runGpfGpl(0); break;
        case 0x3E: runGpfGpl(1); break;
        case 0x3F: runNcx(3, NCX_TINT); break;
        default: {
            /* psx-spx lists no behaviour for the other numbers. Measured:
             * FLAG is reset as for every command (80F41000h before command
             * 000FA026h, 0 after) and no other register changes. */
            static int reported;
            if (!reported) {
                reported = 1;
                fprintf(stderr, "gte: unknown command %07X: only FLAG is reset\n", (unsigned)command);
            }
            break;
        }
    }
    if (flag & F_ERROR_BITS) {
        flag |= F_ERROR;
    }
    ctrlRegs[C_FLAG] = flag;
}

/* ---- save states (system/savestate.h) ---- */

/* The registers (flag, cmdShift and cmdIrMin only live inside a command). */
void gteStateIo(XgState* s) {
    XG_STATE_VAR(s, dataRegs);
    XG_STATE_VAR(s, ctrlRegs);
    XG_STATE_VAR(s, precise);
}
