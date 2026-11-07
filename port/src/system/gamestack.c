/*
 * The game's own stack (gamestack.h).
 *
 * The switch saves the callee-saved registers of the side it leaves on that
 * side's stack, keeps its stack pointer, and continues on the other side's
 * stack (as port/src/game/battling/thread.c does for Battling's coroutine,
 * whose stacks are static too: they sit in battling's BSS). A parked game is
 * therefore its stack's bytes plus one stack pointer.
 */

#include "gamestack.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The main thread's stack on Linux is 8 MB, and the game's frames, the
 * vblank's present (the Vulkan driver) and the settings menu all run on this
 * one. Only the pages ever touched take memory. */
#define GAME_STACK_SIZE (8u << 20)
#define PAGE 4096u

/* stateServe (savestate.c), weak: a build or test without save states. */
void stateServe(void) __attribute__((weak));

#if defined(__x86_64__) && !defined(_WIN32)

/* Saves rbp, rbx, r12-r15, MXCSR and the x87 control word on the current
 * stack, stores its stack pointer in *save, and continues on `load`
 * (System V x86-64: the other registers are the caller's to keep). */
void portStackSwitch(void** save, void* load);
__asm__(".text\n"
        ".globl portStackSwitch\n"
        ".type portStackSwitch, @function\n"
        "portStackSwitch:\n"
        "    pushq %rbp\n"
        "    pushq %rbx\n"
        "    pushq %r12\n"
        "    pushq %r13\n"
        "    pushq %r14\n"
        "    pushq %r15\n"
        "    subq $8, %rsp\n"
        "    stmxcsr (%rsp)\n"
        "    fnstcw 4(%rsp)\n"
        "    movq %rsp, (%rdi)\n"
        "    movq %rsi, %rsp\n"
        "    ldmxcsr (%rsp)\n"
        "    fldcw 4(%rsp)\n"
        "    addq $8, %rsp\n"
        "    popq %r15\n"
        "    popq %r14\n"
        "    popq %r13\n"
        "    popq %r12\n"
        "    popq %rbx\n"
        "    popq %rbp\n"
        "    ret\n"
        ".size portStackSwitch, . - portStackSwitch\n");

/* Zeroes from `from` up to its own return address: the frames below the
 * caller are dead, and it pushes nothing. */
void portStackZeroBelow(unsigned char* from);
__asm__(".text\n"
        ".globl portStackZeroBelow\n"
        ".type portStackZeroBelow, @function\n"
        "portStackZeroBelow:\n"
        "    movq %rsp, %rcx\n"
        "    subq %rdi, %rcx\n"
        "    jbe 1f\n"
        "    xorl %eax, %eax\n"
        "    rep stosb\n"
        "1:  ret\n"
        ".size portStackZeroBelow, . - portStackZeroBelow\n");

/* What the first switch onto a stack pops: the System V switch's frame. */
static uint64_t* contextFrame(uint64_t* sp, uintptr_t low, uintptr_t high) {
    (void)low;
    (void)high;
    for (int i = 0; i < 6; i++) {
        *--sp = 0; /* rbp, rbx, r12-r15 */
    }
    *--sp = (uint64_t)0x037F << 32 | 0x1F80; /* x87 control word, MXCSR: the defaults */
    return sp;
}

static void hostBounds(int save) {
    (void)save;
}

#elif defined(__x86_64__) && defined(_WIN32)

/* Win64 keeps rbx, rbp, rdi, rsi, r12-r15 and xmm6-xmm15 across calls, and
 * the thread's block (TEB) names the stack it runs on: its top (gs:0x08)
 * and its lowest address (gs:0x10), which the system's unwinder and other
 * compilers' stack probes check frames against. The switch keeps them with
 * the registers, as a fiber switch does. Below the pushes: xmm6-15 at 0x00,
 * MXCSR 0xA0, the x87 control word 0xA4, the TEB's stack top 0xA8 and its
 * bottom 0xB0. */
void portStackSwitch(void** save, void* load);
__asm__(".text\n"
        ".globl portStackSwitch\n"
        "portStackSwitch:\n"
        "    pushq %rbp\n"
        "    pushq %rbx\n"
        "    pushq %rdi\n"
        "    pushq %rsi\n"
        "    pushq %r12\n"
        "    pushq %r13\n"
        "    pushq %r14\n"
        "    pushq %r15\n"
        "    subq $0xB8, %rsp\n"
        "    movups %xmm6, 0x00(%rsp)\n"
        "    movups %xmm7, 0x10(%rsp)\n"
        "    movups %xmm8, 0x20(%rsp)\n"
        "    movups %xmm9, 0x30(%rsp)\n"
        "    movups %xmm10, 0x40(%rsp)\n"
        "    movups %xmm11, 0x50(%rsp)\n"
        "    movups %xmm12, 0x60(%rsp)\n"
        "    movups %xmm13, 0x70(%rsp)\n"
        "    movups %xmm14, 0x80(%rsp)\n"
        "    movups %xmm15, 0x90(%rsp)\n"
        "    stmxcsr 0xA0(%rsp)\n"
        "    fnstcw 0xA4(%rsp)\n"
        "    movq %gs:0x08, %rax\n"
        "    movq %rax, 0xA8(%rsp)\n"
        "    movq %gs:0x10, %rax\n"
        "    movq %rax, 0xB0(%rsp)\n"
        "    movq %rsp, (%rcx)\n"
        "    movq %rdx, %rsp\n"
        "    movq 0xA8(%rsp), %rax\n"
        "    movq %rax, %gs:0x08\n"
        "    movq 0xB0(%rsp), %rax\n"
        "    movq %rax, %gs:0x10\n"
        "    movups 0x00(%rsp), %xmm6\n"
        "    movups 0x10(%rsp), %xmm7\n"
        "    movups 0x20(%rsp), %xmm8\n"
        "    movups 0x30(%rsp), %xmm9\n"
        "    movups 0x40(%rsp), %xmm10\n"
        "    movups 0x50(%rsp), %xmm11\n"
        "    movups 0x60(%rsp), %xmm12\n"
        "    movups 0x70(%rsp), %xmm13\n"
        "    movups 0x80(%rsp), %xmm14\n"
        "    movups 0x90(%rsp), %xmm15\n"
        "    ldmxcsr 0xA0(%rsp)\n"
        "    fldcw 0xA4(%rsp)\n"
        "    addq $0xB8, %rsp\n"
        "    popq %r15\n"
        "    popq %r14\n"
        "    popq %r13\n"
        "    popq %r12\n"
        "    popq %rsi\n"
        "    popq %rdi\n"
        "    popq %rbx\n"
        "    popq %rbp\n"
        "    ret\n");

/* As on System V, but rdi is the callee's to keep here (r8 holds it). */
void portStackZeroBelow(unsigned char* from);
__asm__(".text\n"
        ".globl portStackZeroBelow\n"
        "portStackZeroBelow:\n"
        "    movq %rdi, %r8\n"
        "    movq %rcx, %rdi\n"
        "    movq %rsp, %rcx\n"
        "    subq %rdi, %rcx\n"
        "    jbe 1f\n"
        "    xorl %eax, %eax\n"
        "    rep stosb\n"
        "1:  movq %r8, %rdi\n"
        "    ret\n");

/* What the first switch onto a stack pops: the Win64 switch's frame, the
 * TEB naming low..high meanwhile. */
static uint64_t* contextFrame(uint64_t* sp, uintptr_t low, uintptr_t high) {
    for (int i = 0; i < 8; i++) {
        *--sp = 0; /* rbp, rbx, rdi, rsi, r12-r15 */
    }
    sp -= 0xB8 / 8;
    memset(sp, 0, 0xB8);
    uint32_t mxcsr = 0x1F80;
    uint16_t fcw = 0x037F;
    uint64_t top = high, bottom = low;
    memcpy((unsigned char*)sp + 0xA0, &mxcsr, 4);
    memcpy((unsigned char*)sp + 0xA4, &fcw, 2);
    memcpy((unsigned char*)sp + 0xA8, &top, 8);
    memcpy((unsigned char*)sp + 0xB0, &bottom, 8);
    return sp;
}

/* The host's stack as the TEB names it, for a longjmp off the game's
 * stacks (gameStackAbandon). */
static uint64_t hostTop, hostBottom;
static int hostKnown;

static void hostBounds(int save) {
    if (save) {
        __asm__ volatile("movq %%gs:0x08, %0" : "=r"(hostTop));
        __asm__ volatile("movq %%gs:0x10, %0" : "=r"(hostBottom));
        hostKnown = 1;
    } else if (hostKnown) {
        __asm__ volatile("movq %0, %%gs:0x08" : : "r"(hostTop) : "memory");
        __asm__ volatile("movq %0, %%gs:0x10" : : "r"(hostBottom) : "memory");
    }
}

/* The executable's image: its base and SizeOfImage from its PE header. The
 * game's stacks (this one, Battling's) are static arrays in it, and the TEB
 * names all of it while the game runs, so a jump from one of them to
 * another (bootGame's longjmp from a Battling stack) leaves the TEB right. */
extern const unsigned char __ImageBase[];

void portGameStackBounds(uintptr_t* low, uintptr_t* high) {
    uint32_t lfanew, size;
    memcpy(&lfanew, __ImageBase + 0x3C, 4);
    memcpy(&size, __ImageBase + lfanew + 0x50, 4); /* the NT signature, the file header, then 56 bytes in */
    *low = (uintptr_t)__ImageBase;
    *high = (uintptr_t)__ImageBase + size;
}

#elif defined(__aarch64__)

/* AAPCS64 (Linux, Android): saves x19-x28, the frame pointer x29, the link
 * register x30, d8-d15 and FPCR (the rounding and flush modes) in a 176-byte
 * frame on the current stack (16-byte aligned), stores its stack pointer in
 * *save and continues on `load`; x18, the platform register, is left alone.
 * The switch returns through x30, so a new stack starts in portStackEntry,
 * which calls the start function saved in x19 and traps should it return
 * (hint #34 is BTI's landing pad: a NOP where branch protection is off). As
 * port/src/game/battling/thread.c's switch. */
void portStackSwitch(void** save, void* load);
void portStackEntry(void);
__asm__(".text\n"
        ".globl portStackSwitch\n"
        ".type portStackSwitch, %function\n"
        ".p2align 2\n"
        "portStackSwitch:\n"
        "    hint #34\n"
        "    sub sp, sp, #176\n"
        "    stp x19, x20, [sp, #0]\n"
        "    stp x21, x22, [sp, #16]\n"
        "    stp x23, x24, [sp, #32]\n"
        "    stp x25, x26, [sp, #48]\n"
        "    stp x27, x28, [sp, #64]\n"
        "    stp x29, x30, [sp, #80]\n"
        "    stp d8, d9, [sp, #96]\n"
        "    stp d10, d11, [sp, #112]\n"
        "    stp d12, d13, [sp, #128]\n"
        "    stp d14, d15, [sp, #144]\n"
        "    mrs x9, fpcr\n"
        "    str x9, [sp, #160]\n"
        "    mov x9, sp\n"
        "    str x9, [x0]\n"
        "    mov sp, x1\n"
        "    ldr x9, [sp, #160]\n"
        "    msr fpcr, x9\n"
        "    ldp d14, d15, [sp, #144]\n"
        "    ldp d12, d13, [sp, #128]\n"
        "    ldp d10, d11, [sp, #112]\n"
        "    ldp d8, d9, [sp, #96]\n"
        "    ldp x29, x30, [sp, #80]\n"
        "    ldp x27, x28, [sp, #64]\n"
        "    ldp x25, x26, [sp, #48]\n"
        "    ldp x23, x24, [sp, #32]\n"
        "    ldp x21, x22, [sp, #16]\n"
        "    ldp x19, x20, [sp, #0]\n"
        "    add sp, sp, #176\n"
        "    ret\n"
        ".size portStackSwitch, . - portStackSwitch\n"
        ".globl portStackEntry\n"
        ".type portStackEntry, %function\n"
        ".p2align 2\n"
        "portStackEntry:\n"
        "    hint #34\n"
        "    blr x19\n"
        "    brk #0\n"
        ".size portStackEntry, . - portStackEntry\n");

/* Zeroes from `from` up to the stack pointer: the frames below the caller are
 * dead (a leaf: its return address is in x30, nothing on the stack). Bytes up
 * to 16-byte alignment, then 16 at a time (sp is 16-byte aligned). */
void portStackZeroBelow(unsigned char* from);
__asm__(".text\n"
        ".globl portStackZeroBelow\n"
        ".type portStackZeroBelow, %function\n"
        ".p2align 2\n"
        "portStackZeroBelow:\n"
        "    hint #34\n"
        "    mov x1, sp\n"
        "1:  cmp x0, x1\n"
        "    b.hs 3f\n"
        "    tst x0, #15\n"
        "    b.eq 2f\n"
        "    strb wzr, [x0], #1\n"
        "    b 1b\n"
        "2:  stp xzr, xzr, [x0], #16\n"
        "    cmp x0, x1\n"
        "    b.lo 2b\n"
        "3:  ret\n"
        ".size portStackZeroBelow, . - portStackZeroBelow\n");

/* bootGame's unwind (src/main/kernel.c, engine/src/main/kernel.c) where the
 * compiler has no __builtin_setjmp (clang on aarch64): portBootSetjmp keeps
 * the callee-saved registers, the frame pointer, the return address and the
 * stack pointer in a 21-word context and returns 0; portBootLongjmp puts them
 * back and returns 1 from that portBootSetjmp. Like the builtin pair, only
 * for a frame that is still live, and the floating-point modes stay as they
 * are. */
int portBootSetjmp(void** context);
void portBootLongjmp(void** context);
__asm__(".text\n"
        ".globl portBootSetjmp\n"
        ".type portBootSetjmp, %function\n"
        ".p2align 2\n"
        "portBootSetjmp:\n"
        "    hint #34\n"
        "    stp x19, x20, [x0, #0]\n"
        "    stp x21, x22, [x0, #16]\n"
        "    stp x23, x24, [x0, #32]\n"
        "    stp x25, x26, [x0, #48]\n"
        "    stp x27, x28, [x0, #64]\n"
        "    stp x29, x30, [x0, #80]\n"
        "    stp d8, d9, [x0, #96]\n"
        "    stp d10, d11, [x0, #112]\n"
        "    stp d12, d13, [x0, #128]\n"
        "    stp d14, d15, [x0, #144]\n"
        "    mov x9, sp\n"
        "    str x9, [x0, #160]\n"
        "    mov w0, #0\n"
        "    ret\n"
        ".size portBootSetjmp, . - portBootSetjmp\n"
        ".globl portBootLongjmp\n"
        ".type portBootLongjmp, %function\n"
        ".p2align 2\n"
        "portBootLongjmp:\n"
        "    hint #34\n"
        "    ldp x19, x20, [x0, #0]\n"
        "    ldp x21, x22, [x0, #16]\n"
        "    ldp x23, x24, [x0, #32]\n"
        "    ldp x25, x26, [x0, #48]\n"
        "    ldp x27, x28, [x0, #64]\n"
        "    ldp x29, x30, [x0, #80]\n"
        "    ldp d8, d9, [x0, #96]\n"
        "    ldp d10, d11, [x0, #112]\n"
        "    ldp d12, d13, [x0, #128]\n"
        "    ldp d14, d15, [x0, #144]\n"
        "    ldr x9, [x0, #160]\n"
        "    mov sp, x9\n"
        "    mov w0, #1\n"
        "    ret\n"
        ".size portBootLongjmp, . - portBootLongjmp\n");

/* What the first switch onto a stack pops: the AAPCS64 switch's frame, with
 * x19 = start, x30 = portStackEntry, x29 = 0 (the end of the frame chain)
 * and FPCR 0 (round to nearest, no flush to zero: the default). */
static uint64_t* contextFrame(uint64_t* sp, void (*start)(void)) {
    sp -= 176 / 8;
    memset(sp, 0, 176);
    sp[0] = (uint64_t)(uintptr_t)start;          /* x19 */
    sp[11] = (uint64_t)(uintptr_t)portStackEntry; /* x30 */
    return sp;
}

static void hostBounds(int save) {
    (void)save;
}

#endif

#if defined(__x86_64__) || defined(__aarch64__)

static _Alignas(PAGE) unsigned char gameStack[GAME_STACK_SIZE];

/* A stack whose first switch "returns" into start(), as if called with the
 * default floating-point modes. */
static void* newContext(void (*start)(void)) {
    uint64_t* sp = (uint64_t*)(gameStack + GAME_STACK_SIZE);
#if defined(__aarch64__)
    return contextFrame(sp, start);
#else
    uintptr_t low = (uintptr_t)gameStack, high = (uintptr_t)(gameStack + GAME_STACK_SIZE);
#ifdef _WIN32
    sp -= 4; /* start()'s home space (its caller's on Win64) */
    portGameStackBounds(&low, &high);
#endif
    *--sp = 0;                       /* start()'s return address: it never returns */
    *--sp = (uint64_t)(uintptr_t)start; /* taken by the switch's ret */
    return contextFrame(sp, low, high);
#endif
}

static void* hostSp;  /* the host side, while the game runs */
static void* gameSp;  /* the game, while parked */
static void (*gameEntry)(void);
static int running;   /* entry() has not returned */
static int onStack;   /* the game is on its stack (not parked: on it now) */
static unsigned char* lowWater = gameStack + GAME_STACK_SIZE;

static void gameStart(void) {
    gameEntry();
    running = 0;
    onStack = 0;
    portStackSwitch(&gameSp, hostSp); /* never comes back */
    abort();
}

void gameStackRun(void (*entry)(void)) {
    hostBounds(1);
    gameEntry = entry;
    gameSp = newContext(gameStart);
    running = 1;
    while (running) {
        onStack = 1;
        portStackSwitch(&hostSp, gameSp);
        onStack = 0;
        if (running && stateServe != NULL) {
            stateServe();
        }
    }
}

void gameStackAbandon(void) {
    onStack = 0;
    hostBounds(0);
}

void gameStackPark(void) {
    if (!onStack) {
        return;
    }
    portStackSwitch(&gameSp, hostSp);
}

int gameStackActive(void) {
    return onStack;
}

/* From the deepest page written so far (the pages below are untouched). */
void gameStackScrub(void) {
    if (onStack) {
        portStackZeroBelow(gameStackLowWater());
    }
}

void* gameStackParkedSp(void) {
    return gameSp;
}

void gameStackSetParkedSp(void* sp) {
    gameSp = sp;
}

unsigned char* gameStackBase(void) {
    return gameStack;
}

size_t gameStackSize(void) {
    return GAME_STACK_SIZE;
}

/* The pages below the deepest frame so far were never written: zero. A
 * whole scan from the bottom (never-touched pages read as the system's one
 * zero page, so it is quick), since a frame may leave a page it reserved
 * untouched. */
unsigned char* gameStackLowWater(void) {
    for (unsigned char* p = gameStack; p < lowWater; p += PAGE) {
        const uint64_t* w = (const uint64_t*)p;
        uint64_t any = 0;
        for (unsigned i = 0; i < PAGE / 8; i++) {
            any |= w[i];
        }
        if (any != 0) {
            lowWater = p;
            break;
        }
    }
    return lowWater;
}

/* A load wrote the state's stack: its deepest page is the state's. */
void gameStackSetLowWater(unsigned char* low) {
    if (low >= gameStack && low <= gameStack + GAME_STACK_SIZE) {
        lowWater = low;
    }
}

#else

/* No game stack on this target: the game runs on the main thread, and save
 * states are off. */
void gameStackRun(void (*entry)(void)) {
    entry();
}

void gameStackAbandon(void) {
}

void gameStackPark(void) {
}

int gameStackActive(void) {
    return 0;
}

void gameStackScrub(void) {
}

void* gameStackParkedSp(void) {
    return NULL;
}

void gameStackSetParkedSp(void* sp) {
    (void)sp;
}

unsigned char* gameStackBase(void) {
    return NULL;
}

size_t gameStackSize(void) {
    return 0;
}

unsigned char* gameStackLowWater(void) {
    return NULL;
}

void gameStackSetLowWater(unsigned char* low) {
    (void)low;
}

#endif
