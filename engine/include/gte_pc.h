#ifndef GTE_PC_H
#define GTE_PC_H

/*
 * The GTE macros of include/gte.h for the PC port (PLATFORM_PC): the same
 * register traffic, in the same order, as calls into a register-level
 * software GTE. The port implements these seven entry points; the game code
 * and the macro names stay as they are.
 *
 *   gteLwc2/gteSwc2   data register <-> memory (lwc2/swc2)
 *   gteMtc2/gteMfc2   data register <-> CPU value (mtc2/mfc2)
 *   gteCtc2/gteCfc2   control register <-> CPU value (ctc2/cfc2)
 *   gteCommand        run a command; the argument is the instruction's
 *                     25-bit command field (include/gte_macros.inc)
 */

#include "common.h"

void gteLwc2(int reg, const void* addr);
void gteSwc2(int reg, void* addr);
void gteMtc2(int reg, u32 value);
u32 gteMfc2(int reg);
void gteCtc2(int reg, u32 value);
u32 gteCfc2(int reg);
void gteCommand(u32 command);

/* The whole register file, saved and put back around GTE work the PS1 does
 * not do (the wide picture's extra geometry, include/pc_wide.h). */
typedef struct {
    double x, y, z;
    u32 word;
    int valid;
} GtePrecise;
typedef struct {
    u32 data[32];
    u32 ctrl[32];
    GtePrecise precise[3];
} GteState;
void gteSave(GteState* s);
void gteLoad(const GteState* s);

/* Shadow metadata for the display copy. Integer GTE results never change. */
void gtePrecisionEnable(int on);
int gtePrecisionEnabled(void);
/* Signed projected area of the current SXY FIFO, if all vertices are valid.
 * Unlike NCLIP, this query never changes an integer register or flag. */
int gtePrecisionNclip(double* area);
void gtePrecisionNewFrame(void);
/* Keep pending packet metadata alive through construction and submission. */
void gtePrecisionBeginDraw(void);
void gtePrecisionEndDraw(void);
void gtePrecisionRecord(void* addr, const GtePrecise* vertex);
int gtePrecisionLookup(const void* addr, u32 word, GtePrecise* out);
void gtePrecisionForget(const void* addr);
void gteCopyPrecision(void* dst, const void* src);
void gteCopySxy(void* dst, const void* src);

/* Command fields: fake_op << 20 | sf << 19 | mx << 17 | v << 15 | cv << 13 | lm << 10 | op. */
#define GTE_RTPS 0x0180001
#define GTE_RTPT 0x0280030
#define GTE_NCLIP 0x1400006
#define GTE_AVSZ3 0x158002D
#define GTE_AVSZ4 0x168002E
#define GTE_GPF(sf) (0x190003D | ((sf) << 19))
#define GTE_GPL(sf) (0x1A0003E | ((sf) << 19))
#define GTE_NCDS 0x0E80413
#define GTE_NCCS 0x108041B
#define GTE_NCS 0x0C8041E
#define GTE_DPCS 0x0780010
#define GTE_INTPL 0x0980011
#define GTE_SQR(sf) (0x0A00428 | ((sf) << 19))
#define GTE_OP(sf) (0x170000C | ((sf) << 19))
#define GTE_MVMVA(sf, mx, v, cv, lm) \
    (0x0400012 | ((sf) << 19) | ((mx) << 17) | ((v) << 15) | ((cv) << 13) | ((lm) << 10))

#define GTE_AT(r0, off) ((u8*)(r0) + (off))
#define GTE_U32(r0, off) (*(u32*)GTE_AT(r0, off))
#define GTE_U16(r0, off) (*(u16*)GTE_AT(r0, off))

/* Loads */
#define gte_ldv0(r0) (gteLwc2(0, GTE_AT(r0, 0)), gteLwc2(1, GTE_AT(r0, 4)))
#define gte_ldv1(r0) (gteLwc2(2, GTE_AT(r0, 0)), gteLwc2(3, GTE_AT(r0, 4)))
#define gte_ldv2(r0) (gteLwc2(4, GTE_AT(r0, 0)), gteLwc2(5, GTE_AT(r0, 4)))
#define gte_ldv3(r0, r1, r2) (gte_ldv0(r0), gte_ldv1(r1), gte_ldv2(r2))
#define gte_ldv01(r0, r1) (gte_ldv0(r0), gte_ldv1(r1))
#define gte_ldv3c(r0)                                                                                 \
    (gteLwc2(0, GTE_AT(r0, 0)), gteLwc2(1, GTE_AT(r0, 4)), gteLwc2(2, GTE_AT(r0, 8)),                \
     gteLwc2(3, GTE_AT(r0, 12)), gteLwc2(4, GTE_AT(r0, 16)), gteLwc2(5, GTE_AT(r0, 20)))
#define gte_ldrgb(r0) gteLwc2(6, GTE_AT(r0, 0))
#define gte_ldlvl(r0) (gteLwc2(9, GTE_AT(r0, 0)), gteLwc2(10, GTE_AT(r0, 4)), gteLwc2(11, GTE_AT(r0, 8)))
#define gte_lddp(r0) gteMtc2(8, (u32)(r0))
#define gte_ldIR0(r0) gte_lddp(r0)
#define gte_ldsxy3(r0, r1, r2) (gteMtc2(12, (u32)(r0)), gteMtc2(14, (u32)(r2)), gteMtc2(13, (u32)(r1)))

/* Commands */
#define gte_rtps() gteCommand(GTE_RTPS)
#define gte_rtpt() gteCommand(GTE_RTPT)
#define gte_nclip() gteCommand(GTE_NCLIP)
#define gte_avsz3() gteCommand(GTE_AVSZ3)
#define gte_avsz4() gteCommand(GTE_AVSZ4)
#define gte_gpf0() gteCommand(GTE_GPF(0))
#define gte_gpf12() gteCommand(GTE_GPF(1))
#define gte_gpl0() gteCommand(GTE_GPL(0))
#define gte_gpl12() gteCommand(GTE_GPL(1))
#define gte_ncds() gteCommand(GTE_NCDS)
#define gte_nccs() gteCommand(GTE_NCCS)
#define gte_ncs() gteCommand(GTE_NCS)
#define gte_dpcs() gteCommand(GTE_DPCS)
#define gte_intpl() gteCommand(GTE_INTPL)
#define gte_sqr0() gteCommand(GTE_SQR(0))
#define gte_sqr12() gteCommand(GTE_SQR(1))
#define gte_rtv0() gteCommand(GTE_MVMVA(1, 0, 0, 3, 0))
#define gte_rtir() gteCommand(GTE_MVMVA(1, 0, 3, 3, 0))
#define gte_rt() gteCommand(GTE_MVMVA(1, 0, 0, 0, 0))
#define gte_op12() gteCommand(GTE_OP(1))
#define gte_op0() gteCommand(GTE_OP(0))

/* Stores */
#define gte_stsxy(r0) gteSwc2(14, GTE_AT(r0, 0))
#define gte_stsxy3(r0, r1, r2) (gteSwc2(12, GTE_AT(r0, 0)), gteSwc2(13, GTE_AT(r1, 0)), gteSwc2(14, GTE_AT(r2, 0)))
#define gte_stsxy01(r0, r1) (gteSwc2(12, GTE_AT(r0, 0)), gteSwc2(13, GTE_AT(r1, 0)))
/* A POLY_FT4's x0/x1/x2 by name: the PC primitive header is wider than the PS1's. */
#define gte_stsxy3_ft4(r0) \
    (gteSwc2(12, &((POLY_FT4*)(r0))->x0), gteSwc2(13, &((POLY_FT4*)(r0))->x1), gteSwc2(14, &((POLY_FT4*)(r0))->x2))
#define gte_stotz(r0) gteSwc2(7, GTE_AT(r0, 0))
#define gte_stopz(r0) gteSwc2(24, GTE_AT(r0, 0))
#define gte_stsz(r0) gteSwc2(19, GTE_AT(r0, 0))
#define gte_stsz1(r0) gteSwc2(17, GTE_AT(r0, 0))
#define gte_stsz2(r0) gteSwc2(18, GTE_AT(r0, 0))
#define gte_strgb(r0) gteSwc2(22, GTE_AT(r0, 0))
#define gte_stlvl(r0) (gteSwc2(9, GTE_AT(r0, 0)), gteSwc2(10, GTE_AT(r0, 4)), gteSwc2(11, GTE_AT(r0, 8)))
#define gte_stsxy0(r0) gteSwc2(12, GTE_AT(r0, 0))
#define gte_stsxy1(r0) gteSwc2(13, GTE_AT(r0, 0))
#define gte_stsxy2(r0) gteSwc2(14, GTE_AT(r0, 0))
#define gte_stsz3(r0, r1, r2) (gteSwc2(17, GTE_AT(r0, 0)), gteSwc2(18, GTE_AT(r1, 0)), gteSwc2(19, GTE_AT(r2, 0)))
#define gte_ldsz4(r0, r1, r2, r3) \
    (gteMtc2(16, (u32)(r0)), gteMtc2(17, (u32)(r1)), gteMtc2(18, (u32)(r2)), gteMtc2(19, (u32)(r3)))
#define gte_stflg(r0) (*(u32*)(r0) = gteCfc2(31))
#define gte_stlvnl(r0) (gteSwc2(25, GTE_AT(r0, 0)), gteSwc2(26, GTE_AT(r0, 4)), gteSwc2(27, GTE_AT(r0, 8)))
#define gte_stdp(r0) gteSwc2(8, GTE_AT(r0, 0))
#define gte_stszotz(r0) (*(s32*)(r0) = (s32)gteMfc2(19) >> 2)
#define gte_stsz4c(r0) \
    (gteSwc2(16, GTE_AT(r0, 0)), gteSwc2(17, GTE_AT(r0, 4)), gteSwc2(18, GTE_AT(r0, 8)), gteSwc2(19, GTE_AT(r0, 12)))
#define gte_stsxy3_r(r0, r1, r2) (gteSwc2(12, &(r0)), gteSwc2(13, &(r1)), gteSwc2(14, &(r2)))
#define gte_stsxy2_r(r0) gteSwc2(14, &(r0))
#define gte_copy_sxy(dst, src) gteCopyPrecision((dst), (src))

/* Control-register matrix loads (5 words each). */
#define GTE_CTC5(r0, base)                                                                       \
    (gteCtc2((base) + 0, GTE_U32(r0, 0)), gteCtc2((base) + 1, GTE_U32(r0, 4)),                  \
     gteCtc2((base) + 2, GTE_U32(r0, 8)), gteCtc2((base) + 3, GTE_U32(r0, 12)),                 \
     gteCtc2((base) + 4, GTE_U32(r0, 16)))
#define gte_SetRotMatrix(r0) GTE_CTC5(r0, 0)
#define gte_SetLightMatrix(r0) GTE_CTC5(r0, 8)
#define gte_SetColorMatrix(r0) GTE_CTC5(r0, 16)
#define gte_SetBackColor(r0, r1, r2) \
    (gteCtc2(13, (u32)(r0) << 4), gteCtc2(14, (u32)(r1) << 4), gteCtc2(15, (u32)(r2) << 4))
#define gte_SetTransMatrix(r0) (gteCtc2(5, GTE_U32(r0, 20)), gteCtc2(6, GTE_U32(r0, 24)), gteCtc2(7, GTE_U32(r0, 28)))

/* Matrix column / SVECTOR <-> IR1-3. */
#define gte_ldclmv(r0) (gteMtc2(9, GTE_U16(r0, 0)), gteMtc2(10, GTE_U16(r0, 6)), gteMtc2(11, GTE_U16(r0, 12)))
#define gte_stclmv(r0)                                                                            \
    (GTE_U16(r0, 0) = (u16)gteMfc2(9), GTE_U16(r0, 6) = (u16)gteMfc2(10), GTE_U16(r0, 12) = (u16)gteMfc2(11))
#define gte_stsv(r0) \
    (GTE_U16(r0, 0) = (u16)gteMfc2(9), GTE_U16(r0, 2) = (u16)gteMfc2(10), GTE_U16(r0, 4) = (u16)gteMfc2(11))
#define gte_ldsv(r0) (gteMtc2(9, GTE_U16(r0, 0)), gteMtc2(10, GTE_U16(r0, 2)), gteMtc2(11, GTE_U16(r0, 4)))
#define gte_ldlv0(r0) (gteMtc2(0, GTE_U16(r0, 0) | ((u32)GTE_U16(r0, 4) << 16)), gteLwc2(1, GTE_AT(r0, 8)))

/* Outer product operands. */
#define gte_ldopv1(r0) (gteCtc2(0, GTE_U32(r0, 0)), gteCtc2(2, GTE_U32(r0, 4)), gteCtc2(4, GTE_U32(r0, 8)))
#define gte_ldopv2(r0) (gteLwc2(11, GTE_AT(r0, 8)), gteLwc2(9, GTE_AT(r0, 0)), gteLwc2(10, GTE_AT(r0, 4)))

/* Composites (inline_c.h), as in include/gte.h. */
#define gte_MulMatrix0(r1, r2, r3)       \
    {                                    \
        gte_SetRotMatrix(r1);            \
        gte_ldclmv(r2);                  \
        gte_rtir();                      \
        gte_stclmv(r3);                  \
        gte_ldclmv((char*)(r2) + 2);     \
        gte_rtir();                      \
        gte_stclmv((char*)(r3) + 2);     \
        gte_ldclmv((char*)(r2) + 4);     \
        gte_rtir();                      \
        gte_stclmv((char*)(r3) + 4);     \
    }
#define gte_CompMatrix(r1, r2, r3)       \
    {                                    \
        gte_MulMatrix0(r1, r2, r3);      \
        gte_SetTransMatrix(r1);          \
        gte_ldlv0((char*)(r2) + 20);     \
        gte_rt();                        \
        gte_stlvnl((char*)(r3) + 20);    \
    }
#define gte_OuterProduct0(r1, r2, r3)    \
    {                                    \
        gte_ldopv1(r1);                  \
        gte_ldopv2(r2);                  \
        gte_op0();                       \
        gte_stlvnl(r3);                  \
    }
#define gte_OuterProduct12(r1, r2, r3)   \
    {                                    \
        gte_ldopv1(r1);                  \
        gte_ldopv2(r2);                  \
        gte_op12();                      \
        gte_stlvnl(r3);                  \
    }
#define gte_RotTransPers(r1, r2, r3, r4, r5) \
    {                                        \
        gte_ldv0(r1);                        \
        gte_rtps();                          \
        gte_stsxy(r2);                       \
        gte_stdp(r3);                        \
        gte_stflg(r4);                       \
        gte_stszotz(r5);                     \
    }

#endif /* GTE_PC_H */
