#ifndef GTE_H
#define GTE_H

/*
 * Inline GTE (geometry coprocessor, COP2) macros in the style of PSY-Q's
 * inline_c.h. Some use $12-$14 as scratch registers, as PSY-Q's do. Data registers: V0 $0/$1, V1 $2/$3, V2 $4/$5, RGBC $6,
 * OTZ $7, IR0 $8, IR1-3 $9-$11, SXY0-2 $12-$14, SZ0-3 $16-$19,
 * RGB0-2 $20-$22, MAC0-3 $24-$27. Commands are issued after two nops, as
 * PSY-Q's macros do; the command mnemonics come from include/gte_macros.inc
 * (pulled in by maspsx --macro-inc).
 *
 * This is the one sanctioned use of inline asm in matched code (see
 * docs/PLAN.md).
 */

#include "gte_pc.h"

#endif /* GTE_H */
