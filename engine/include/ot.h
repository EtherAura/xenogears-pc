#ifndef OT_H
#define OT_H

/*
 * ADD_PRIM_ASM(ot, p, len): link primitive p (len words after its tag) at
 * the head of ordering-table entry ot, like psyq.h's addPrim, but as one
 * inline-asm block through $12/$13 (the scratch registers of the GTE
 * macros). The world map's cloud renderer does its OT links this way: the
 * eight instructions are never interleaved with other code and always use
 * t4/t5, which GCC's allocator would not pick, so C cannot produce them.
 * Retail: lw $12,0(ot); lui $13,len<<8; or $12,$12,$13; li $13,0xFFFFFF;
 * and $13,p,$13; sw $13,0(ot); sw $12,0(p).
 *
 * Sanctioned like include/gte.h and include/stack.h (user decision
 * together with the register variables func_80086798 needs).
 */
/* The same link in C; the PC tag layout lives behind setaddr/getaddr/setlen. */
#define ADD_PRIM_ASM(ot, p, len) (setlen(p, len), addPrim(ot, p))

#endif /* OT_H */
