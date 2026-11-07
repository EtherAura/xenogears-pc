/*
 * Battling's coroutine (hand-written assembly at 0x8008BB00 in the PS1 build;
 * config/handwritten.yaml), and the task table and DrawSync callback that sit
 * in an assembly block at 0x80088BFC.
 *
 * On the PS1, resumeBattlingThread saves the caller's registers below its
 * stack pointer, remembers that stack pointer and the thread (battlingThreadResumeSp,
 * battlingCurrentThread), and loads the thread's registers from its context
 * (BattlingThread.m0_regs, by register number); yieldBattlingThread stores
 * every register into the context and returns to the last resumer. A new
 * thread starts at m0_regs[31] (its entry) with m0_regs[4] as its argument,
 * on its own stack.
 *
 * The PC cannot load the PS1 registers, so it switches C stacks: each thread
 * gets a PC stack of its own on its first resume (the PS1's 4 KB stack at a
 * fixed address is far too small for PC frames and is not used), and the
 * switch saves and restores the callee-saved registers with a few
 * instructions of assembly per host ABI. The PC context hangs off
 * m0_regs[0], the $zero slot that the PS1 code never saves or loads.
 * Everything still runs on the game thread, one side at a time.
 */
#include "common.h"
#include "psyq.h"
#include "battling/battling.h"

void battlingMainTask(void);
void pcArenaBattlingStart(void); /* include/pc_arena.h */
s32 VSync(s32 mode);

extern s32 battlingGpuTime; /* the VSync count at the last DrawSync */

/* ---- the host stack switch ----------------------------------------------- */

/* Saves the callee-saved registers on the current stack, stores its stack
 * pointer in *save, then continues on the stack `load` (one saved by an
 * earlier switch, or a new one from newStack). */
void portBattlingSwitch(void** save, void* load);

#if defined(__x86_64__) && !defined(_WIN32)
/* System V x86-64: rbx, rbp, r12-r15, MXCSR and the x87 control word. */
__asm__(".text\n"
        ".globl portBattlingSwitch\n"
        ".type portBattlingSwitch, @function\n"
        "portBattlingSwitch:\n"
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
        ".size portBattlingSwitch, . - portBattlingSwitch\n");

/* A stack whose first switch "returns" into start(), as if called. */
static void* newStack(u8* base, uptr size, void (*start)(void)) {
    u64* sp = (u64*)(((uptr)base + size) & ~(uptr)15);
    s32 i;

    *--sp = 0;                 /* start()'s return address: it never returns */
    *--sp = (u64)(uptr)start;  /* popped by the switch's ret */
    for (i = 0; i < 6; i++) {
        *--sp = 0;             /* rbp, rbx, r12-r15 */
    }
    *--sp = (u64)0x037F << 32 | 0x1F80; /* x87 control word, MXCSR: the defaults */
    return sp;
}
#elif defined(__x86_64__) && defined(_WIN32)
/* Win64: rbx, rbp, rdi, rsi, r12-r15, xmm6-xmm15, MXCSR and the x87 control
 * word, and the stack the thread's block (TEB) names (its top at gs:0x08,
 * its lowest address at gs:0x10), as the game stack's switch keeps them
 * (port/src/system/gamestack.c, same frame). Static stacks, as on Linux, so
 * that a save state holds them. */
__asm__(".text\n"
        ".globl portBattlingSwitch\n"
        "portBattlingSwitch:\n"
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

/* The executable's image, which the TEB names while the game runs (all its
 * stacks are static arrays in it: gamestack.c). */
void portGameStackBounds(uptr* low, uptr* high);

/* A stack whose first switch "returns" into start(), as if called. */
static void* newStack(u8* base, uptr size, void (*start)(void)) {
    u64* sp = (u64*)(((uptr)base + size) & ~(uptr)15);
    uptr low, high;
    s32 i;

    portGameStackBounds(&low, &high);
    sp -= 4;                   /* start()'s home space (its caller's on Win64) */
    *--sp = 0;                 /* start()'s return address: it never returns */
    *--sp = (u64)(uptr)start;  /* popped by the switch's ret */
    for (i = 0; i < 8; i++) {
        *--sp = 0;             /* rbp, rbx, rdi, rsi, r12-r15 */
    }
    for (i = 0; i < 0xB8 / 8; i++) {
        *--sp = 0;             /* xmm6-xmm15 ... */
    }
    sp[0xA0 / 8] = (u64)0x037F << 32 | 0x1F80; /* x87 control word, MXCSR: the defaults */
    sp[0xA8 / 8] = high;       /* the TEB's stack: the image */
    sp[0xB0 / 8] = low;
    return sp;
}
#elif defined(XG_PS1_LAYOUT)
/* tools/pc_link.py's PS1-layout probe compiles this file for 32-bit x86 only
 * to size the data it names; no switch is built or run there. */
static void* newStack(u8* base, uptr size, void (*start)(void)) {
    return NULL;
}
#else
#error "port/src/game/battling/thread.c: no stack switch for this target yet"
#endif

/* ---- the coroutine --------------------------------------------------------- */

#define STACK_SIZE (1024 * 1024)
#define STACK_COUNT 2

typedef struct {
    void* sp;                  /* saved while the thread is suspended */
    u8 stack[STACK_SIZE];
} PcThread;

/* Battling runs one thread; a new one may start while the previous battle's
 * thread is abandoned (left suspended when the mode ended), so stacks are
 * handed out in turn. */
static PcThread pcThreads[STACK_COUNT];
static s32 nextPcThread;

static void* resumerSp;                /* battlingThreadResumeSp: the last resumer */
static BattlingThread* currentThread;  /* battlingCurrentThread */

/* A thread's first resume enters here. On the PS1 the thread starts with $ra
 * set to its entry, so an entry that returns starts over. */
static void battlingThreadStart(void) {
    BattlingThread* t = currentThread;
    void (*entry)(s32) = (void (*)(s32))t->m0_regs[31];

    for (;;) {
        entry((s32)t->m0_regs[4]);
    }
}

void resumeBattlingThread(BattlingThread* t) {
    PcThread* pc = (PcThread*)t->m0_regs[0];

    currentThread = t;
    if (pc == NULL) {
        pc = &pcThreads[nextPcThread];
        nextPcThread = (nextPcThread + 1) % STACK_COUNT;
        pc->sp = newStack(pc->stack, STACK_SIZE, battlingThreadStart);
        t->m0_regs[0] = (uptr)pc;
    }
    portBattlingSwitch(&resumerSp, pc->sp);
}

void yieldBattlingThread(void) {
    PcThread* pc = (PcThread*)currentThread->m0_regs[0];

    portBattlingSwitch(&pc->sp, resumerSp);
}

/* Save and restore the resumer and the current thread (around a nested
 * resume). Two pointer-sized slots on the PC, two words on the PS1. */
void func_8008BB00(uptr* saved) {
    saved[0] = (uptr)resumerSp;
    saved[1] = (uptr)currentThread;
}

void func_8008BB1C(uptr* saved) {
    resumerSp = (void*)saved[0];
    currentThread = (BattlingThread*)saved[1];
}

/* ---- 0x80088BFC: a one-entry task table, then a DrawSync callback --------- */

/* The PC's entry first tells a Battle arena session that it is in Battling
 * (port/src/game/main/arena.c; nothing happens without one). */
static void pcBattlingMainTask(void) {
    pcArenaBattlingStart();
    battlingMainTask();
}

void* battlingMainTaskTable[1] = { (void*)pcBattlingMainTask };

/* The PS1 passes &battlingMainTaskTable[1] (this code) to DrawSyncCallback. */
void func_80088C00(void) {
    battlingGpuTime = VSync(1);
}
