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

#endif

#if defined(__x86_64__)

static _Alignas(PAGE) unsigned char gameStack[GAME_STACK_SIZE];

/* A stack whose first switch "returns" into start(), as if called with the
 * default floating-point modes. */
static void* newContext(void (*start)(void)) {
    uint64_t* sp = (uint64_t*)(gameStack + GAME_STACK_SIZE);
    uintptr_t low = (uintptr_t)gameStack, high = (uintptr_t)(gameStack + GAME_STACK_SIZE);
#ifdef _WIN32
    sp -= 4; /* start()'s home space (its caller's on Win64) */
    portGameStackBounds(&low, &high);
#endif
    *--sp = 0;                       /* start()'s return address: it never returns */
    *--sp = (uint64_t)(uintptr_t)start; /* taken by the switch's ret */
    return contextFrame(sp, low, high);
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
