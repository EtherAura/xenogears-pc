/*
 * The Retry / Game over choice of the retry setting (xg-htn.35.89; the
 * checkpoint is port/src/game/main/retry.c). battleMain calls
 * pcBattleRetryOffer when its loop ends with the party wiped in a battle the
 * game would end in its game over (battleRunningVar1 0x81, and no battle
 * event menu that turns the loss into the story's own outcome); with the
 * setting off, or no checkpoint of this battle, it returns at once and the
 * native game over follows.
 *
 * The look is the game's own: a battle dialogue window (window 0, the one
 * battle event dialogues use, opening as they do), the dialogue font in its
 * text area at VRAM 0x380,0x100 (the battle event dialogue's), and the
 * chosen line tinted as the field tints a choice (setupMultiChoiceColor
 * 0xEF,0x1E,0xF0, OP_SETUP_MULTICHOICE). Up and Down move with the battle's
 * cursor sound (battleSoundEffect2 0x4C), the confirm button chooses with its
 * confirm sound (0x4D). The battle's own input reader stops once the battle
 * has ended (battleHandleInput answers 0xFF), so the pad is read directly
 * (pcPadHeldRemapped, src/main/pad.c), presses as edges of what is held.
 * The text: US "Retry" / "Game over" in the font's single-byte codes; JP
 * リトライ / ゲームオーバー through the game's own character code table
 * (decodeCharacterCodes, text bank 27: codes read from the JP disc's table).
 */
#include "common.h"
#include "psyq.h"
#include "main/dialog.h"
#include "battle/battle.h"

s32 pcRetryAvailable(void); /* port/src/game/main/retry.c */
void pcRetryArm(void);
s32 pcPadHeldRemapped(void); /* src/main/pad.c */
s32 battleRenderDebugAndMain(void);

#define RETRY_WINDOW 0
#define RETRY_X 0x64
#define RETRY_Y 0x4C
#define RETRY_W 0x78
#define RETRY_H 0x2C
#define RETRY_TEXT_UNITS 0x18 /* 96 pixels: ゲームオーバー is 84 */

static DialogWindow18 retryWindow;
static s32 retryShown;
static u8 retryText[48];

static void buildText(void) {
#if VERSION_JP
    static const u16 codes[] = {185, 151, 184, 115, 0xFFFF, 129, 193, 175, 121, 193, 159, 193};
    u8* out = retryText;
    s32 i;

    for (i = 0; i < (s32)(sizeof(codes) / sizeof(codes[0])); i++) {
        if (codes[i] == 0xFFFF) {
            *out++ = 1; /* a new line */
        } else {
            decodeCharacterCodes((u16*)&codes[i], out, 1);
            while (*out != 0) {
                out++;
            }
        }
    }
    *out = 0;
#else
    /* R e t r y, new line, G a m e space o v e r */
    static const u8 text[] = {0x31, 0x41, 0x50, 0x4E, 0x55, 0x01, 0x26, 0x3D, 0x49, 0x41, 0x10, 0x4B, 0x52, 0x41, 0x4E, 0x00};
    s32 i;

    for (i = 0; i < (s32)sizeof(text); i++) {
        retryText[i] = text[i];
    }
#endif
}

/* drawBattleMode1/2, right after the dialogue windows: the choice's text. */
/* Battle speed (speed.c): the choice is up, the battle waits for the player. */
s32 pcBattleRetryShown(void) {
    return retryShown;
}

void pcBattleRetryDraw(void) {
    if (retryShown) {
        updateAndRenderTextForDialogWindow(&retryWindow, D_800CCB04.m0_pCurrentBattleOT + 1,
                                           D_800CCB04.m30_battleOddOrEven);
    }
}

void pcBattleRetryOffer(void) {
    s32 choice = 0;
    s32 held;
    s32 previous;
    s32 pressed;

    if (!pcRetryAvailable() || battleVar1 == NULL || battleVar1->mB0_isDialogWindowInitialized[RETRY_WINDOW] ||
        battleVar1->mB8_isDialogWindowInitialized2[RETRY_WINDOW]) {
        return;
    }
    allocateAndSetupBattleDialogWindow(RETRY_WINDOW, RETRY_X, RETRY_Y, RETRY_W, RETRY_H, 1, 0);
    while (!battleVar1->mBF_isDialogWindowInitialized3[RETRY_WINDOW]) {
        battleRenderDebugAndMain();
    }
    buildText();
    setupWindowSize2(&retryWindow, 0x380, 0x100, RETRY_X + 0xC, RETRY_Y + 8, RETRY_TEXT_UNITS, 2);
    retryWindow.m10_flags |= 2; /* no background tile: the window is the background */
    retryWindow.m68_charsPerStep = 0x20; /* a whole line a step */
    resetDialogWindow18(&retryWindow);
    addDialogWindowsToOTSub2(&retryWindow, retryText);
    setupMultiChoiceColor(&retryWindow, 0xEF, 0x1E, 0xF0);
    setSelectedLineForMultiChoice(&retryWindow, choice);
    retryShown = 1;
    previous = pcPadHeldRemapped();
    for (;;) {
        battleRenderDebugAndMain();
        held = pcPadHeldRemapped();
        pressed = held & ~previous;
        previous = held;
        if (!(retryWindow.m10_flags & 8)) {
            continue; /* the text is still being printed */
        }
        if (pressed & (0x1000 | 0x4000)) {
            choice ^= 1;
            setSelectedLineForMultiChoice(&retryWindow, choice);
            battleSoundEffect2(0x4C);
        } else if (pressed & 0x20) {
            battleSoundEffect2(0x4D);
            break;
        }
    }
    retryShown = 0;
    dealocateDialogWindow18Memory(&retryWindow);
    clearBattleDialogWindow(RETRY_WINDOW);
    if (choice == 0) {
        pcRetryArm();
    }
}
