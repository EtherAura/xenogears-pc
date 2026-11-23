/*
 * The menu screens' safe points (include/pc_state.h): each screen's loop
 * keeps its locals here at the top of each frame, where a canonical state is
 * saved, and takes them back when a stackless load starts the game again
 * there (the chain: the mode's loop, its menu call, enterMenu, the main menu,
 * menu2_executeMainMenuSelection, the screen). Part of the menu module: saved
 * with its BSS.
 */
#include "common.h"
#include "pc_state.h"

/* ---- the screens' integer locals, as bytes ---------------------------------- */

/* Each screen's locals, kept at its loop's top: integers only (u8, s32), the
 * same bytes in every build. The shops' sell lists keep the most: five byte
 * arrays of their entry count (up to 200) and a dozen integers. */
#define LOCALS_BYTES 1088
static u8 screenLocals[PC_SAFE_SUB_COUNT][LOCALS_BYTES];

static void tooMany(void) {
    portTodo(__FUNCTION__, "a menu screen keeps more locals than LOCALS_BYTES");
}
/* The screen a shared loop (runEquipMenu, runEtherListMenu) runs as: its caller
 * names it, so its safe point's id says the way back to it. */
static s32 menuChild;

void pcMenuKeep(s32 sub, s32 n, ...) {
    __builtin_va_list ap;
    u8* out = screenLocals[sub];
    s32 used = 0;
    __builtin_va_start(ap, n);
    while (n-- > 0) {
        u8* p = __builtin_va_arg(ap, u8*);
        s32 size = __builtin_va_arg(ap, s32);
        s32 i;
        if (used + size > LOCALS_BYTES) {
            tooMany();
            break;
        }
        for (i = 0; i < size; i++) {
            out[used++] = p[i];
        }
    }
    __builtin_va_end(ap);
}

void pcMenuTake(s32 sub, s32 n, ...) {
    __builtin_va_list ap;
    const u8* in = screenLocals[sub];
    s32 used = 0;
    __builtin_va_start(ap, n);
    while (n-- > 0) {
        u8* p = __builtin_va_arg(ap, u8*);
        s32 size = __builtin_va_arg(ap, s32);
        s32 i;
        if (used + size > LOCALS_BYTES) {
            tooMany();
            break;
        }
        for (i = 0; i < size; i++) {
            p[i] = in[used++];
        }
    }
    __builtin_va_end(ap);
}

void pcMenuChild(s32 sub) {
    menuChild = sub;
}

s32 pcMenuChildNow(void) {
    return menuChild;
}
