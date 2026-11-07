/*
 * Battle speed (xg-mrj.2, port/src/system/battlespeed.c): battleRender tells
 * the platform each frame that a battle runs, whether it waits for the
 * player, and which text it shows (each new text has its first moments at
 * 1x).
 *
 * Waiting for the player, from what the battle's own loops test
 * (setupTurnRenderLoop, b_80079674.c; handleMenuSelectEnemy, b_80080AE4.c):
 * the command ring up and not yet disabled by an attack (choosing a command:
 * the D-pad's auto-repeat and the ring's turning are per frame), the target
 * arrows up before the target is confirmed, an item, ether or combo list
 * open (mB7_openMenuType), an event dialogue box (mC9_dialog: story text,
 * some of it closing by itself), a page or the results waiting for the
 * confirm button (mCF_isWaitingForInput), the Retry / Game over choice
 * (retryprompt.c). The attack combo itself (the ring disabled, menu 100)
 * plays at speed: the game takes the next attack button whenever it comes.
 *
 * Text: the name banner at the bottom (dialogue window 4: attack, ether and
 * deathblow names), the acting enemy's name (window 5) and the name labels
 * (previousEntityTurn: the enemy's, then its attacks'); each is up for as
 * long as its turn takes, so only its start is held (battlespeed.c).
 */
#include "common.h"
#include "battle/battle.h"

void battleSpeedBattleFrame(int wait, unsigned int text); /* port/src/system/battlespeed.c */
s32 pcBattleRetryShown(void);                             /* retryprompt.c */

void pcBattleSpeedFrame(void) {
    s32 wait = 0;
    u32 text = 0;
    s32 i;

    if (battleVar1 != NULL && battleVar2 != NULL) {
        wait = (battleVar1->mCB_drawCommandRing && !battleVar2->m2E0_isBattleMenuDisabled) ||
               (battleVar1->mC6_isTargetSelectionCursorVisible && !battleVar2->m2E9_targetConfirmed) ||
               battleVar1->mB7_openMenuType != 0 || battleVar1->mC9_dialog != 0 ||
               battleVar1->mCF_isWaitingForInput != 0;
        text = (battleVar1->mB0_isDialogWindowInitialized[4] ? 1 : 0) |
               (battleVar1->mB0_isDialogWindowInitialized[5] ? 2 : 0);
        for (i = 0; i < 8; i++) {
            if (previousEntityTurn.m8_nameLabels[i].m5D_monsterNameIsDisplayed) {
                text |= 4u << i;
            }
        }
    }
    if (pcBattleRetryShown()) {
        wait = 1;
    }
    battleSpeedBattleFrame(wait, text);
}
