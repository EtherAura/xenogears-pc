#ifndef INCLUDE_ASM_H
#define INCLUDE_ASM_H

/*
 * INCLUDE_ASM pulls a not-yet-decompiled function's assembly into a C file,
 * in place, so the object keeps retail function order.
 *
 * GCC 2.7.2 with a non-zero -G moves functions after file-scope __asm__
 * blocks, so each include is wrapped in a dummy function that maspsx strips,
 * keeping only the lines tagged "maspsx-keep" (see maspsx's README).
 *
 * M2CTX/PERMUTER builds (m2c context, decomp-permuter) and the PC port
 * (PLATFORM_PC) see no assembly: the port supplies C for what is still
 * hand-written assembly.
 */


#define INCLUDE_ASM(FOLDER, NAME)
#define INCLUDE_RODATA(FOLDER, NAME)


#endif /* INCLUDE_ASM_H */
