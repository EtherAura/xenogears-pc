#ifndef COMMON_H
#define COMMON_H

typedef signed char s8;
typedef unsigned char u8;
typedef signed short s16;
typedef unsigned short u16;
typedef signed int s32;
typedef unsigned int u32;
typedef signed long long s64;
typedef unsigned long long u64;

/*
 * Integers that hold a pointer (PC port, docs/PORT_PLAN.md). On the PS1 a
 * pointer is 32 bits, so these are exactly u32/s32 and change no code; on a
 * 64-bit host they are pointer-sized. Use them where the game keeps an
 * address in an integer (offset arithmetic, relocation, packed fields).
 */
#ifdef PLATFORM_PC
typedef __UINTPTR_TYPE__ uptr;
typedef __INTPTR_TYPE__ sptr;
#else
typedef u32 uptr;
typedef s32 sptr;
#endif

/*
 * REG_PIN("$n"): pin a register variable to a MIPS register, the documented
 * last resort for matching (docs/PLAN.md). The PC compiler has no such
 * registers, so there it pins nothing.
 */
#ifdef PLATFORM_PC
#define REG_PIN(reg)
#else
#define REG_PIN(reg) asm(reg)
#endif

/*
 * PORT_ALIAS, at the end of an array's declaration: on the PC the name sits
 * inside another object (an alias tools/pc_link.py or a port bss.c block
 * makes at its PS1 offset), so it may not have the 16-byte alignment the
 * x86-64 ABI promises a global array of 16 bytes or more; compilers that rely
 * on the promise (clang's aligned SSE stores) must not. pc_link stops the PC
 * build when such a declaration lacks it. Nothing on the PS1.
 */
#ifdef PLATFORM_PC
#define PORT_ALIAS __attribute__((aligned(1)))
#else
#define PORT_ALIAS
#endif

/*
 * PORT_TODO("why"): code the PC port must do differently, not just with
 * other types (disc data relocated in place, fixed PS1 addresses, hardware
 * access). The PS1 code stays as is under #ifndef PLATFORM_PC and the PC
 * branch calls PORT_TODO, which the port reports at run time until a PC
 * version exists. `grep -rn PORT_TODO src` is the list of those places.
 */
#ifdef PLATFORM_PC
void portTodo(const char* func, const char* why);
#define PORT_TODO(why) portTodo(__FUNCTION__, why)
#endif

/*
 * PORT_YIELD(): in a loop that waits for an interrupt callback to change
 * something (a CD read finishing, a sound transfer). The PC runs the PS1's
 * interrupts as callbacks at safe points on the game thread, so such a loop
 * must offer one; on the PS1 it is nothing.
 */
/*
 * portResetModule("<overlay>"), PC only: where the PS1 reads an overlay's
 * image from disc to its link address. Every overlay is linked into the PC
 * executable, so there is nothing to load; this puts the overlay's data and
 * BSS back as a fresh load leaves them (port/src/system/memory.c).
 */
#ifdef PLATFORM_PC
void portResetModule(const char* name);
/* A module's data and BSS copied aside and put back (the copy is freed). */
void* portSaveModule(const char* name);
void portRestoreModule(const char* name, void* copy);
/* XG_OVERLAY_CD, a validation option (port/src/system/overlay_cd.c): the PC
 * also reads the overlay file there, as the PS1 does, into a scratch buffer
 * (slot 0: an overlay, 1: a second file in the same batch, 2: decompressed)
 * that it then discards, so the CD takes the PS1's time. */
s32 portOverlayCd(void);
void* portOverlayScratch(s32 slot, s32 size);
#endif

#ifdef PLATFORM_PC
void portYield(void);
#define PORT_YIELD() portYield()
#else
#define PORT_YIELD()
#endif

/*
 * Functions that return int but give no value on some path (K&R style): on
 * the PS1 the caller gets whatever is in $v0 there; on the PC the return
 * register would hold anything. v is the value $v0 holds, read from the
 * retail assembly; where no caller reads the result and $v0 holds nothing
 * reproducible (an address, a callee's leftover), v is 0. The PS1 code stays
 * identical; the PC returns v.
 *   IMPLICIT_RETURN(v)  in place of a `return;` (the PS1 code is `return`);
 *   FALLOFF_RETURN(v)   as the last statement of a function that falls off
 *                       its end (nothing on the PS1: a `return;` there can
 *                       change the block order).
 * `grep -rnE '(IMPLICIT|FALLOFF)_RETURN' src` lists them.
 */
#ifdef PLATFORM_PC
#define IMPLICIT_RETURN(v) return v
#define FALLOFF_RETURN(v) return v
#else
#define IMPLICIT_RETURN(v) return
#define FALLOFF_RETURN(v)
#endif

/*
 * Divisions the retail code makes without the assembler's divide check
 * (`bnez divisor; break 7`: field, battling, most of battle and main, the
 * menus) where the divisor can be 0. The R3000 then gives a result instead of
 * trapping: quotient -1 for a dividend >= 0 (all ones for divu), 1 for a
 * negative one; remainder the dividend. x86 traps. The PS1 code is the plain
 * operator; the PC gives the R3000's result. Divisions with the check would
 * stop the PS1 too, so they stay plain. `grep -rn R3000_ src` lists them.
 */
#ifdef PLATFORM_PC
#define R3000_DIV(a, b) \
    __extension__({ \
        __typeof__((a) / (b)) r3000a_ = (a), r3000b_ = (b); \
        r3000b_ != 0 ? r3000a_ / r3000b_ : r3000a_ < 0 ? 1 : -1; \
    })
#define R3000_MOD(a, b) \
    __extension__({ \
        __typeof__((a) % (b)) r3000a_ = (a), r3000b_ = (b); \
        r3000b_ != 0 ? r3000a_ % r3000b_ : r3000a_; \
    })
#else
#define R3000_DIV(a, b) ((a) / (b))
#define R3000_MOD(a, b) ((a) % (b))
#endif

/*
 * PSX_SCRATCH(addr): an address in the PS1's 1 KB scratchpad (0x1F800000 to
 * 0x1F8003FF), as the game C names it: (MATRIX*)PSX_SCRATCH(0x1F800000). On
 * the PS1 the address itself; on the PC the same offset in the platform's
 * scratchpad block (pcScratchpad, port/src/system/memory.c), wherever the
 * host put it: the PS1 address is not free in every process (an Android
 * app's Java heap holds it). tools/scratchpadcheck.py (gate scratchpad) fails
 * on a raw scratchpad address in C.
 */
#define PSX_SCRATCH_BASE 0x1F800000
#ifdef PLATFORM_PC
extern unsigned char pcScratchpad[];
#define PSX_SCRATCH(addr) ((uptr)pcScratchpad + ((addr) - PSX_SCRATCH_BASE))
#else
#define PSX_SCRATCH(addr) (addr)
#endif

#ifndef NULL
#define NULL ((void*)0)
#endif

/*
 * offsetof(T, m): the byte offset of member m in T, for sizes that cover only
 * part of a struct (a prefix cleared or allocated). On the PS1 it is the
 * constant the retail code wrote; on the PC the offset in the host layout.
 */
#ifndef offsetof
#ifdef PLATFORM_PC
#define offsetof(T, m) __builtin_offsetof(T, m)
#else
#define offsetof(T, m) ((u32) & ((T*)0)->m)
#endif
#endif

/* True widescreen hooks (PC only): include/pc_wide.h. */
#ifdef PLATFORM_PC
#include "pc_wide.h"
#endif

#include "include_asm.h"

#endif /* COMMON_H */
