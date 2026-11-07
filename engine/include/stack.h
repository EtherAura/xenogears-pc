#ifndef STACK_H
#define STACK_H

/*
 * Inline-assembly macros for two register tricks the retail code performs
 * inline, which C cannot express:
 *
 * - STACK_SWITCH(top) / STACK_RESTORE(): run on a scratch stack. The old
 *   $sp is saved at *top and $sp becomes top - 4; STACK_RESTORE() pops it
 *   back. Used around big uploads (e.g. a 0x2000-byte heap block as stack).
 *   Retail: move $t0,top; sw $sp,0($t0); addiu $t0,$t0,-4; move $sp,$t0
 *   ... addiu $sp,$sp,4; lw $sp,0($sp).
 *
 * - GET_RA(dst): store the caller's return address into *dst, through $t7
 *   (the heap manager records who allocated each block).
 *   Retail: move $t7,dst; sw $ra,0($t7).
 *
 * Together with include/gte.h these are the only sanctioned inline asm in
 * matched code (user decision; see docs/PLAN.md).
 */

/*
 * The PC runs these calls on its own stack (the scratch stacks only kept
 * the PS1's small stack from overflowing), and records the return address
 * through the compiler. GET_RA's destination is a u32: the heap and the error
 * reporter keep it for debugging only, so a truncated host address is fine.
 */
#define STACK_SWITCH(top) ((void)(top))
#define STACK_RESTORE() ((void)0)
#define GET_RA(dst) (*(dst) = (u32)(uptr)__builtin_return_address(0))

#endif /* STACK_H */
