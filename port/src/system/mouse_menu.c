/*
 * The mouse pointing in the game's menu (mouse.c, settings.ini mouse_point):
 * a click on a main menu command or on an item list entry moves the game's
 * own cursor there with D-pad presses and confirms, through the pad as a
 * player would; the plan is checked against the game's cursor before its
 * confirm (mouseMenuCheck), and dropped if the game went elsewhere.
 *
 * Where things are, read from the game as it runs:
 *   - the main menu (field and world map menu, menuToEnter 0, its commands
 *     not dimmed, no transition): commands at the game's own
 *     cursorPosXTable/cursorPosYTable (menu overlay 0x801E9A00/0x801E9A2C),
 *     the selected one gMenuContext->m336_menuSelectedEntry; Up is the next
 *     entry, Down the previous (mainMenuExecute), seven entries, wrapping;
 *     in a wide picture the commands are moved left by the margin
 *     (port/src/game/menu/wide.c: pcWideView);
 *   - the item list (openItemMenu: 16 entries, two columns 0x88 apart, rows
 *     16 apart from y 0xE; names at x 0x28 + 0x88 c): its cursor from the
 *     list pointer's place (updateMenuPointerGlyph mode 0: 0x1C + 0x88 c,
 *     0x11 + 16 r); Down and Up a row, Right and Left an entry. Only on the
 *     page shown (no paging), and not while the list is dimmed (choosing whom
 *     to use an item on). The game's confirm there marks the entry; a
 *     second on it uses the item, on another entry swaps the two (openItemMenu):
 *     a click on an item, then a click on it again, uses it, as with the pad.
 * A click beside the entries there, or while the main menu opens or closes,
 * does nothing; in the menu's other screens a click is the confirm button.
 */

#include "common.h"
#include "menu/menu.h"
#include "pc_wide.h"
#include "mouse.h"

#define UP 0x1000
#define RIGHT 0x2000
#define DOWN 0x4000
#define LEFT 0x8000

/* The main menu waits for a command. */
static int mainMenuReady(void) {
    MenuContext* c = gMenuContext;
    return pcWideGetMode() == PC_WIDE_MENU && menuToEnter == 0 && c != NULL && c->m33C_drawFlags != NULL &&
           c->m350_mainMenu != NULL && c->m33C_drawFlags->m9_drawMainMenu && c->m33C_drawFlags->m4_drawCursor &&
           !c->m350_mainMenu->m1192_dimmed && c->m329_animationState == 0;
}

/* The item list waits for an entry: the list pointer (slot 0) not dimmed. */
static int itemListReady(void) {
    MenuContext* c = gMenuContext;
    if (pcWideGetMode() != PC_WIDE_MENU || menuToEnter != 0 || c == NULL || c->m33C_drawFlags == NULL ||
        c->m350_mainMenu == NULL || !c->m350_mainMenu->m1192_dimmed || !c->m33C_drawFlags->m48_draw42C ||
        !c->m33C_drawFlags->m50_draw444[0] || c->m444_pointerGlyphs[0] == NULL || c->m329_animationState != 0) {
        return 0;
    }
    MenuContext_444* p = c->m444_pointerGlyphs[0];
    return p->m0_polys[p->m75_oddOrEven].r0 != 0x21; /* setMenuPolyShadeMode 1: dimmed */
}

/* The main menu is shown but not taking a command yet (opening, closing). */
static int mainMenuBusy(void) {
    MenuContext* c = gMenuContext;
    return pcWideGetMode() == PC_WIDE_MENU && menuToEnter == 0 && c != NULL && c->m33C_drawFlags != NULL &&
           c->m350_mainMenu != NULL && c->m33C_drawFlags->m9_drawMainMenu && !c->m350_mainMenu->m1192_dimmed;
}

static int itemCursor(void) {
    MenuContext_444* p = gMenuContext->m444_pointerGlyphs[0];
    int x = p->m50_vertex[0].vx + 0xA0, y = p->m50_vertex[0].vy + 0x70;
    int col = (x - 0x1C + 0x44) / 0x88, row = (y - 0x11 + 8) / 16;
    if (col < 0 || col > 1 || row < 0 || row > 7) return -1;
    return row * 2 + col;
}

static void add(MousePlan* plan, unsigned short bits, int n) {
    while (n-- > 0 && plan->count < MOUSE_PLAN_MAX - 1) {
        plan->bits[plan->count++] = bits;
    }
}

int mouseMenuPoint(float x, float y, MousePlan* plan) {
    plan->count = 0;
    if (mainMenuReady()) {
        float mx = x + (float)pcWideView; /* the commands sit at the wide picture's left edge */
        int best = -1;
        float bestD = 1e9f;
        for (int k = 0; k < 7; k++) {
            float cx = (float)cursorPosXTable[k], cy = (float)cursorPosYTable[k];
            /* the ball from x - 8, the name to about x + 64; a line from y - 12 */
            if (mx >= cx - 9 && mx <= cx + 66 && y >= cy - 13 && y <= cy + 10) {
                float d = (y - (cy - 1)) * (y - (cy - 1));
                if (d < bestD) {
                    bestD = d;
                    best = k;
                }
            }
        }
        if (best < 0) {
            return -1; /* beside the commands: nothing */
        }
        int cur = gMenuContext->m336_menuSelectedEntry;
        int up = (best - cur + 7) % 7, down = (cur - best + 7) % 7;
        add(plan, up <= down ? UP : DOWN, up <= down ? up : down);
        plan->confirm = 1;
        plan->check = MOUSE_CHECK_MENU_ENTRY;
        plan->checkArg = best;
        return 1;
    }
    if (itemListReady()) {
        int cur = itemCursor();
        if (cur < 0 || x < 0x18 || x >= 0x18 + 2 * 0x88 || y < 12 || y >= 12 + 8 * 16) {
            return -1;
        }
        int col = ((int)x - 0x18) / 0x88, row = ((int)y - 12) / 16;
        int dr = row - cur / 2, dc = col - cur % 2;
        add(plan, dr > 0 ? DOWN : UP, dr > 0 ? dr : -dr);
        add(plan, dc > 0 ? RIGHT : LEFT, dc > 0 ? dc : -dc);
        plan->confirm = 1;
        plan->check = MOUSE_CHECK_ITEM;
        plan->checkArg = row * 2 + col;
        return 1;
    }
    return mainMenuBusy() ? -1 : 0;
}

int mouseMenuCheck(int kind, int arg) {
    if (kind == MOUSE_CHECK_MENU_ENTRY) {
        return mainMenuReady() && gMenuContext->m336_menuSelectedEntry == arg;
    }
    if (kind == MOUSE_CHECK_ITEM) {
        return itemListReady() && itemCursor() == arg;
    }
    return 0;
}
