/* f_8007DCF8.c (US 0x8007DCF8-0x8008083C): field dialog windows
 * (Noah: field/dialogWindows.cpp).
 *
 * COMPILER: gcc-2.7.2-psx, -O2 -G0, ASPSX 2.34. gcc-2.6.3/2.6.0 fail DCF8,
 * F814, FFE8, 8004C, 80760, 807B4; gcc-2.7.2-cdk fails all but a few. */
#include "common.h"
#include "pc_observe.h"
#include "pc_gameplay.h"
#include "pc_wide.h"
#include "psyq.h"
#include "field/field.h"

extern s32 fieldMapNumber;
extern DialogWindow gDialogWindows[4];      /* gDialogWindows */
extern u16 g_padButtonForField;                  /* g_padButtonForField */

extern DR_MODE gDialogDrModes[2][16];       /* gDialogDrModes */
extern RECT gDialogRects[16];             /* gDialogRects */
extern s32 dialogWindowVar1[4];               /* dialogWindowVar1 */

void SetDrawMode(DR_MODE* p, s32 dfe, s32 dtd, s32 tpage, RECT* tw);
/* windowOpenBF: declared as an array: retail loads its address into a
 * register for the read-modify-write, as GCC does for arrays. */
extern u16 windowOpenBF[];
extern MATRIX g_currentProjectionMatrix;
extern FieldBss_800AF880 fieldSceneState;
/* dialogWindowPortraitPolySetups (Noah): 8 (u, v) pairs; retail indexes it as
 * a flat s16 array (setup * 2 + column). */
extern s16 dialogWindowPortraitPolySetups[8 * 2];
extern u8 dialogWindowColor[3];                /* dialogWindowColor */
/* The dialog RECT tables as one flat s16 (x, y, w, h) table: RECTs 0-7
 * dialogWindowsRectConfigs3 (0x800ADE9C, window borders), 8-12
 * dialogWindowsRectConfigs2 (0x800ADEDC, cursor frames), 13-18
 * dialogWindowsRectConfigs (0x800ADF04, wait icon frames). Retail indexes
 * them flat (orphan pseudos and CSE-related addresses in the functions
 * below need one symbol and element-wise indexing). */
extern s16 dialogWindowVramRects[19 * 4];
void SetTile(void* p);
extern s32 dialogWindowsFrameCount;                  /* dialogWindowsFrameCount */
extern s32 dialogWindowsFrameCount2;                  /* dialogWindowsFrameCount2 */
extern u16 padButtonForDialogs;                  /* padButtonForDialogs */
extern s32 g_frameOddOrEven;                  /* g_frameOddOrEven */
void dialogWindowSetupForRendering(OTag* ot, s32 odd, s32 index);
/* port/src/game/field/wide.c: the window moved in the wide picture */
s32 pcFieldDialogDx(s32 window);
uptr pcFieldOtHead(OTag* ot);
void pcFieldOtShift(OTag* ot, uptr head, s32 dx);
/* The field code sees addDialogWindowsToOTSub0 as returning an int (no
 * 0xFF mask after the call); main/dialog.h declares it u8. */
#define ADD_DIALOG_SUB0(p) (((s32 (*)(DialogWindow18*))addDialogWindowsToOTSub0)(p))
extern s32 createDialogWindowCounter;                  /* createDialogWindowCounter */
extern s16 dialogWindowOpenAnimationNumFrames;                  /* dialogWindowOpenAnimationNumFrames */
extern FieldBss_800B2184 op9DVar1;
extern u8* rawFieldDialogBundle;                  /* rawFieldDialogBundle */
/* windowsDefaultPositions (Noah): (x, y) per window. A flat array: the
 * y reads index &windowsDefaultPositions[1] (a second symbolic base), which is what
 * leaves retail's unused stack slot in createDialogWindow. */
extern u16 windowsDefaultPositions[4 * 2];
s32 getVariable(s32 var);             /* getVariable */
/* The field code passes setupWindowSize2's coordinates as ints (no
 * truncation at the call); main/dialog.h declares them s16. */
#define SETUP_WINDOW_SIZE2 ((void (*)(DialogWindow18*, s32, s32, s32, s32, s32, s32))setupWindowSize2)
void initIndividualDialogWindow(s32 index);
s32 releaseDialogWindow(s16 index);

s32 pcFieldOffTick(void); /* f_80072140.c */
void updateMultiChoice(s32 index, OTag* ot, s32 odd) {
    if (gDialogWindows[index].m37C_cursor.m0_state == 0 && gDialogWindows[index].m408_openAnimationCounter == 0) {
        if (gDialogWindows[index].m410_priority == 0) {
            /* Enhanced (xg-hbc.19.5): the choice's Up/Down on the ticks only. A 60 Hz field
             * frame between peeks the native frame's presses (updateFieldInputs) and the
             * tick reads them again: here they moved the cursor twice a press (and the
             * frame between changed the state the tick runs from). The frame between
             * after the tick draws the moved cursor (m6_currentMultiChoiceSelection, the
             * line's shade) a vblank before the next tick does. */
            if (!pcFieldOffTick() && (g_padButtonForField & 0x4000)) {
                gDialogWindows[index].m37C_cursor.m6_currentMultiChoiceSelection++;
                if (gDialogWindows[index].m37C_cursor.m4_numMaxMultiChoiceSelection - 1 <
                    gDialogWindows[index].m37C_cursor.m6_currentMultiChoiceSelection) {
                    gDialogWindows[index].m37C_cursor.m6_currentMultiChoiceSelection = 0;
                }
            }
            if (!pcFieldOffTick() && (g_padButtonForField & 0x1000)) {
                gDialogWindows[index].m37C_cursor.m6_currentMultiChoiceSelection--;
                if (gDialogWindows[index].m37C_cursor.m6_currentMultiChoiceSelection < 0) {
                    gDialogWindows[index].m37C_cursor.m6_currentMultiChoiceSelection =
                        gDialogWindows[index].m37C_cursor.m4_numMaxMultiChoiceSelection - 1;
                }
            }
            /* the field code passes the line unmasked (int parameter) */
            ((void (*)(DialogWindow18*, s32))setSelectedLineForMultiChoice)(&gDialogWindows[index].m18_text,
                                          gDialogWindows[index].m37C_cursor.m6_currentMultiChoiceSelection +
                                              gDialogWindows[index].m37C_cursor.m2_startOfMultiChoice);
        } else {
            clearSelectedLineForMultiChoice(&gDialogWindows[index].m18_text);
        }
    }
}

void initDialogWindows(void) {
    RECT rect;
    s32 i;
    RECT* p;


    rect.y = 0;
    rect.x = 0;
    rect.h = 0xFF;
    rect.w = 0xFF;
    for (i = 0; i < 16; i++) {
        p = &gDialogRects[i];
        p->y = 0;
        gDialogRects[i].x = 0;
        p->h = 0xFF;
        gDialogRects[i].w = 0xFF;
        p->y = 0;
        gDialogRects[i].x = 0;
        p->h = 0xFF;
        gDialogRects[i].w = 0xFF;
        SetDrawMode(&gDialogDrModes[0][i], 0, 0, GetTPage(0, 0, 0x380, 0x100), p);
        SetDrawMode(&gDialogDrModes[1][i], 0, 0, GetTPage(0, 0, 0x380, 0x100), p);
    }
    for (i = 0; i < 4; i++) {
        gDialogWindows[i].m416_fieldActorId = 0xFF;
        gDialogWindows[i].m418_actorId = 0xFF;
        gDialogWindows[i].m37C_cursor.m0_state = -1;
        gDialogWindows[i].m3C4_waitIcon.m0_state = -1;
        gDialogWindows[i].m40E_state = -1;
        gDialogWindows[i].m414_keepOpen = -1;
        gDialogWindows[i].m410_priority = 0xFFFF;
        gDialogWindows[i].m416_fieldActorId = 0xFF;
        gDialogWindows[i].m412_onTop = 0;
        initIndividualDialogWindow(i);
        dialogWindowVar1[i] = -1;
        SetDrawMode(&gDialogWindows[i].m0_drawMode[0], 0, 0, GetTPage(0, 0, 0x300, 0x100), &rect);
        SetDrawMode(&gDialogWindows[i].m0_drawMode[1], 0, 0, GetTPage(0, 0, 0x300, 0x100), &rect);
    }
}

void setupWindowSize1(s32 index, s32 x, s32 y, s32 width, s32 height) {
    gDialogWindows[index].mAC_frame.m0_x = x;
    gDialogWindows[index].mAC_frame.m2_y = y;
    gDialogWindows[index].mAC_frame.m4_width = width;
    gDialogWindows[index].mAC_frame.m6_height = height;
}

void setupFacePolyPosition(POLY_FT4* poly, s32 x, s32 y, s32 width, s32 height, s32 mode) {
    if (mode == 0) {
        x--;
        poly->x0 = x;
        poly->x1 = x + width;
        poly->x2 = x;
        poly->x3 = x + width;
    } else {
        poly->x1 = x;
        poly->x0 = x + width;
        poly->x3 = x;
        poly->x2 = x + width;
    }
    poly->y0 = y;
    poly->y1 = y;
    poly->y2 = y + height;
    poly->y3 = y + height;
}

s32 pcFieldSubIndex(void);
s32 pcFieldSubFrames(void); /* f_80072140.c */
/* ---- Enhanced (xg-hbc.19.5): the dialogue's response a vblank earlier ----
 * The dialogue's logic runs on the 60 Hz field's ticks (the script VM, the
 * confirm, the printer, stepDialogWindows' release: pcFieldOffTick in
 * addDialogWindowsToOT); a frame between draws the windows as the tick left
 * them. What the next tick will certainly do to a window, from the state as
 * it stands and from this vblank's press (peeked: the tick reads the same
 * samples, so its padButtonForDialogs is a superset), is drawn a vblank
 * early, drawing only, no state touched:
 * - a window whose opening is done and whose text has ended (flag 4 clear:
 *   the page's end taken by the confirm before) or that a script closed
 *   (keepOpen 0) is released by the next tick's stepDialogWindows before it
 *   draws: not drawn (PC_DIALOG_HIDE; the window vanishes at the press's
 *   4th vblank, not its 5th);
 * - a confirm press (padButtonForDialogs 0x20) reaching a window the next
 *   tick applies it to (its opening done, or on its last frame: the tick's
 *   stepDialogWindows runs first): the choice cursor is reset (not drawn,
 *   PC_DIALOG_NO_CURSOR); the wait icon is gone for a window of the priority
 *   loop (its state is taken after addDialogWindowsToOTSub1, PC_DIALOG_NO_ICON),
 *   still drawn for an on-top one (taken before); and a window whose next page
 *   waits behind its break (flag 0x40) is cleared for it (its lines not drawn,
 *   PC_DIALOG_CLEAR: the tick's own picture, which prints the page's first
 *   characters only the frame after).
 * Not previewed: the auto-advance (its own timing), the text's next step
 * (the text image and its VRAM upload: the printer's cadence is the
 * authored one), a window the tick's script creates (the VM's), and a
 * cursor move (the tick's picture still shows the old place: the move is
 * drawn by the frame between after it, see updateMultiChoice). A script
 * that re-creates a hidden window on the same index at the tick (rare)
 * shows the gap a vblank early; a page break followed at once by the
 * text's end shows the icon's return a vblank late. */
static u8 pcDialogPreview[4];
#define PC_DIALOG_HIDE 1
#define PC_DIALOG_NO_CURSOR 2
#define PC_DIALOG_NO_ICON 4
#define PC_DIALOG_CLEAR 8
static void pcDialogPreviewPlan(s32 between) {
    s32 i;
    s32 last = 0xFF;
    s32 press = (padButtonForDialogs & 0x20) != 0;

    for (i = 0; i < 4; i++) {
        if (gDialogWindows[i].m412_onTop != 0) {
            last = i;
        }
    }
    for (i = 0; i < 4; i++) {
        DialogWindow* d = &gDialogWindows[i];
        u8 p = 0;
        if (between && d->m40E_state == 0) {
            if ((d->m408_openAnimationCounter == 0 && !(d->m18_text.m10_flags & 4)) || d->m414_keepOpen == 0) {
                p = PC_DIALOG_HIDE;
            } else if (press && d->m408_openAnimationCounter <= 1) {
                if (d->m412_onTop != 0) {
                    p = PC_DIALOG_NO_CURSOR;
                } else if (d->m410_priority == 0 && i != last) {
                    p = PC_DIALOG_NO_CURSOR | PC_DIALOG_NO_ICON;
                }
                if (p != 0 && (d->m18_text.m10_flags & 0x40)) {
                    p |= PC_DIALOG_CLEAR;
                }
            }
        }
        pcDialogPreview[i] = p;
    }
}
/* updateAndRenderTextForDialogWindow (main/dialog.c) with the window's preview. */
static void pcDialogRenderText(s32 i, DialogWindow18* p, OTag* ot, s32 odd) {
    pcGameplaySetDialogHideLines((pcDialogPreview[i] & PC_DIALOG_CLEAR) != 0);
    updateAndRenderTextForDialogWindow(p, ot, odd);
    pcGameplaySetDialogHideLines(0);
}

void dialogWindowSetupForRendering(OTag* ot, s32 odd, s32 index) {
    RECT rect;
    s32 x;
    s32 y;
    s32 width;
    s32 height;
    s32 w2;
    s32 h2;

    if (gDialogWindows[index].m40E_state == 0) {
        x = gDialogWindows[index].mAC_frame.m0_x;
        y = gDialogWindows[index].mAC_frame.m2_y;
        w2 = (u16)gDialogWindows[index].mAC_frame.m4_width << 16;
        width = w2 >> 16;
        h2 = (u16)gDialogWindows[index].mAC_frame.m6_height << 16;
        height = h2 >> 16;
        if (gDialogWindows[index].m408_openAnimationCounter != 0) {
            s32 frames2 = dialogWindowOpenAnimationNumFrames * 2;
            s32 passed = dialogWindowOpenAnimationNumFrames - gDialogWindows[index].m408_openAnimationCounter;
            s32 xo = ((width << 16) / frames2) * passed;
            s32 yo;
            s32 sub = pcFieldSubIndex();
            s32 subs = pcFieldSubFrames();

            yo = ((height << 16) / frames2) * passed;
            if (sub != 0) {
                /* Enhanced (xg-hbc.19.3): at 60 Hz the opening steps on the ticks (its counter is
                 * stepDialogWindows'); the frames between show it on its way to the next tick's step,
                 * which only the window's release could stop */
                xo += (((width << 16) / frames2) * sub) / subs;
                yo += (((height << 16) / frames2) * sub) / subs;
            }
            x = x + width / 2 - (xo >> 16);
            y = y + height / 2 - (yo >> 16);
            height = (yo * 2) >> 16;
            width = (xo * 2) >> 16;
            if (width < 16) {
                x -= (16 - width) / 2;
                width = 16;
            }
            if (height < 16) {
                y -= (16 - height) / 2;
                height = 16;
            }
            if (sub == 0) { /* Enhanced: the flight steps once a native frame (it flew past at twice the rate) */
                gDialogWindows[index].m41C_openOffset[0] += gDialogWindows[index].m424_openOffsetStep[0];
                gDialogWindows[index].m41C_openOffset[1] += gDialogWindows[index].m424_openOffsetStep[1];
                y += gDialogWindows[index].m41C_openOffset[1] >> 16;
                x += gDialogWindows[index].m41C_openOffset[0] >> 16;
            } else {
                y += (gDialogWindows[index].m41C_openOffset[1] + gDialogWindows[index].m424_openOffsetStep[1] / subs * sub) >> 16;
                x += (gDialogWindows[index].m41C_openOffset[0] + gDialogWindows[index].m424_openOffsetStep[0] / subs * sub) >> 16;
            }
        }
        pcFieldPicAdd(PC_FIELD_PIC_DIALOGS, x); /* Enhanced oracle: the window as drawn */
        pcFieldPicAdd(PC_FIELD_PIC_DIALOGS, y);
        pcFieldPicAdd(PC_FIELD_PIC_DIALOGS, width);
        pcFieldPicAdd(PC_FIELD_PIC_DIALOGS, height);
        if (gDialogWindows[index].m3C4_waitIcon.m0_state == 0 && gDialogWindows[index].m410_priority == 0 &&
            gDialogWindows[index].m408_openAnimationCounter == 0 && !(gDialogWindows[index].m40C_flags & 0x40) &&
            gDialogWindows[index].m37C_cursor.m0_state != 0) {
            if (gDialogWindows[index].m40A_waitIconDelay == 0) {
                DialogWindow18* sub = &gDialogWindows[index].m18_text;
                s32 sx = getWindowSubStructX(sub);
                s32 sy = getWindowSubStructY(sub);

                rect.x = dialogWindowVramRects[(dialogWindowsFrameCount2 + 13) * 4 + 0];
                rect.y = dialogWindowVramRects[(dialogWindowsFrameCount2 + 13) * 4 + 1];
                rect.w = dialogWindowVramRects[(dialogWindowsFrameCount2 + 13) * 4 + 2];
                rect.h = dialogWindowVramRects[(dialogWindowsFrameCount2 + 13) * 4 + 3];
                SetDrawMode(&gDialogWindows[index].m3C4_waitIcon.m4_drawMode[odd], 0, 0, GetTPage(0, 0, 0x298, 0x1C0), &rect);
                gDialogWindows[index].m3C4_waitIcon.m1C_sprt[odd].x0 = sx;
                gDialogWindows[index].m3C4_waitIcon.m1C_sprt[odd].y0 = sy + 4;
                if (!(pcDialogPreview[index] & PC_DIALOG_NO_ICON)) { /* Enhanced: the confirm's preview */
                    addPrim(ot, &gDialogWindows[index].m3C4_waitIcon.m1C_sprt[odd]);
                    addPrim(ot, &gDialogWindows[index].m3C4_waitIcon.m4_drawMode[odd]);
                }
            } else if (!pcFieldOffTick()) { /* Enhanced: counts native frames */
                gDialogWindows[index].m40A_waitIconDelay--;
            }
        } else {
            gDialogWindows[index].m40A_waitIconDelay = 2;
        }
        {
            s32 hh;
            s32 i;

            gDialogWindows[index].mAC_frame.m140_borders[odd][0].x0 = x - 8;
            gDialogWindows[index].mAC_frame.m140_borders[odd][0].y0 = y - 7;
            gDialogWindows[index].mAC_frame.m140_borders[odd][2].x0 = x + width - 8;
            gDialogWindows[index].mAC_frame.m140_borders[odd][2].y0 = y - 7;
            gDialogWindows[index].mAC_frame.m140_borders[odd][4].x0 = x - 8;
            gDialogWindows[index].mAC_frame.m140_borders[odd][4].y0 = y + height - 9;
            gDialogWindows[index].mAC_frame.m140_borders[odd][6].x0 = x + width - 8;
            gDialogWindows[index].mAC_frame.m140_borders[odd][6].y0 = y + height - 9;
            gDialogWindows[index].mAC_frame.m140_borders[odd][3].x0 = x - 8;
            gDialogWindows[index].mAC_frame.m140_borders[odd][3].y0 = y + 9;
            gDialogWindows[index].mAC_frame.m140_borders[odd][1].x0 = x + width - 8;
            gDialogWindows[index].mAC_frame.m140_borders[odd][1].y0 = y + 9;
            hh = height - 0x12;
            if (hh < 0) {
                hh = 0;
            }
            gDialogWindows[index].mAC_frame.m140_borders[odd][3].h = hh;
            gDialogWindows[index].mAC_frame.m140_borders[odd][1].h = hh;
            gDialogWindows[index].mAC_frame.m140_borders[odd][5].x0 = x + 8;
            gDialogWindows[index].mAC_frame.m140_borders[odd][5].y0 = y - 7;
            gDialogWindows[index].mAC_frame.m140_borders[odd][5].w = width - 0x10;
            gDialogWindows[index].mAC_frame.m140_borders[odd][7].x0 = x + 8;
            gDialogWindows[index].mAC_frame.m140_borders[odd][7].y0 = y + height - 9;
            gDialogWindows[index].mAC_frame.m140_borders[odd][7].w = width - 0x10;
            if (!(gDialogWindows[index].m40C_flags & 0x40)) {
                for (i = 0; i < 8; i++) {
                    addPrim(ot, &gDialogWindows[index].mAC_frame.m140_borders[odd][i]);
                    addPrim(ot, &gDialogWindows[index].mAC_frame.m50_borderDrawModes[odd][i]);
                }
            }
        }
        {
            s32 faceW = 0x40;
            s32 faceH;
            s32 px;
            s32 py;

            if (width - 4 < 0x40) {
                faceW = width - 8;
            }
            faceH = 0x40;
            if (height - 4 < 0x40) {
                faceH = height - 8;
            }
            px = x + 4;
            py = y + 4;
            if (gDialogWindows[index].m40C_flags & 0x20) {
                px = x + width - faceW - 4;
            }
            setupFacePolyPosition(&gDialogWindows[index].m444_portraitPoly[odd], px, py, faceW, faceH,
                                  gDialogWindows[index].m40C_flags & 0x20);
            if (gDialogWindows[index].m494_hasPortrait == 1) {
                pcTextureClass(&gDialogWindows[index].m444_portraitPoly[odd], PC_TEX_PORTRAIT);
                addPrim(ot, &gDialogWindows[index].m444_portraitPoly[odd]);
                addPrim(ot, &gDialogWindows[index].m42C_portraitDrawMode[odd]);
            }
        }
        if (gDialogWindows[index].m37C_cursor.m0_state == 0 && gDialogWindows[index].m410_priority == 0 &&
            gDialogWindows[index].m408_openAnimationCounter == 0) {
            if (gDialogWindows[index].m494_hasPortrait == 1 && !(gDialogWindows[index].m40C_flags & 0x20)) {
                gDialogWindows[index].m37C_cursor.m20_sprt[odd].x0 = x + 0x5A;
            } else {
                gDialogWindows[index].m37C_cursor.m20_sprt[odd].x0 = x + 0x16;
            }
            gDialogWindows[index].m37C_cursor.m20_sprt[odd].y0 =
                (gDialogWindows[index].m37C_cursor.m6_currentMultiChoiceSelection + gDialogWindows[index].m37C_cursor.m2_startOfMultiChoice) * 14 + y + 8;
            rect.x = dialogWindowVramRects[(dialogWindowsFrameCount2 + 8) * 4 + 0];
            rect.y = dialogWindowVramRects[(dialogWindowsFrameCount2 + 8) * 4 + 1];
            rect.w = dialogWindowVramRects[(dialogWindowsFrameCount2 + 8) * 4 + 2];
            rect.h = dialogWindowVramRects[(dialogWindowsFrameCount2 + 8) * 4 + 3];
            SetDrawMode(&gDialogWindows[index].m37C_cursor.m8_drawMode[odd], 0, 0, GetTPage(0, 0, 0x288, 0x1C0), &rect);
            if (!(pcDialogPreview[index] & PC_DIALOG_NO_CURSOR)) { /* Enhanced: the confirm's preview */
                addPrim(ot, &gDialogWindows[index].m37C_cursor.m20_sprt[odd]);
                addPrim(ot, &gDialogWindows[index].m37C_cursor.m8_drawMode[odd]);
            }
        }
        gDialogWindows[index].mAC_frame.m30_backgroundTile[odd].x0 = x;
        gDialogWindows[index].mAC_frame.m30_backgroundTile[odd].y0 = y + 1;
        gDialogWindows[index].mAC_frame.m30_backgroundTile[odd].w = width;
        gDialogWindows[index].mAC_frame.m30_backgroundTile[odd].h = height - 2;
        if (!(gDialogWindows[index].m40C_flags & 0x40)) {
            addPrim(ot, &gDialogWindows[index].mAC_frame.m30_backgroundTile[odd]);
            addPrim(ot, &gDialogWindows[index].mAC_frame.m18_drawMode[odd]);
        }
        /* On the PS1 `width` lives in the stack word 2 under the field's next
         * wall-slide edge buffer (f_8008083C.c pcSlideEdgeStack). */
        pcSlideEdgeStack[2] = width;
    }
}

void initIndividualDialogWindow(s32 index) {
    RECT rect;
    s32 i;

    SetDrawMode(&gDialogWindows[index].mAC_frame.m18_drawMode[0], 0, 0, GetTPage(0, 2, 0x280, 0x1F0), NULL);
    SetDrawMode(&gDialogWindows[index].mAC_frame.m18_drawMode[1], 0, 0, GetTPage(0, 2, 0x280, 0x1F0), NULL);
    SetTile(&gDialogWindows[index].mAC_frame.m30_backgroundTile[0]);
    setRGB0(&gDialogWindows[index].mAC_frame.m30_backgroundTile[0], dialogWindowColor[0], dialogWindowColor[1], dialogWindowColor[2]);
    SetSemiTrans(&gDialogWindows[index].mAC_frame.m30_backgroundTile[0], 1);
    gDialogWindows[index].mAC_frame.m30_backgroundTile[1] = gDialogWindows[index].mAC_frame.m30_backgroundTile[0];

    rect.x = dialogWindowVramRects[13 * 4 + 0];
    rect.y = dialogWindowVramRects[13 * 4 + 1];
    rect.w = dialogWindowVramRects[13 * 4 + 2];
    rect.h = dialogWindowVramRects[13 * 4 + 3];
    SetDrawMode(&gDialogWindows[index].m3C4_waitIcon.m4_drawMode[0], 0, 0, GetTPage(0, 0, 0x298, 0x1C0), &rect);
    SetDrawMode(&gDialogWindows[index].m3C4_waitIcon.m4_drawMode[1], 0, 0, GetTPage(0, 0, 0x298, 0x1C0), &rect);
    SetSprt(&gDialogWindows[index].m3C4_waitIcon.m1C_sprt[0]);
    setRGB0(&gDialogWindows[index].m3C4_waitIcon.m1C_sprt[0], 0x80, 0x80, 0x80);
    gDialogWindows[index].m3C4_waitIcon.m1C_sprt[0].clut = GetClut(0x100, 0xF6);
    gDialogWindows[index].m3C4_waitIcon.m1C_sprt[0].u0 = 0x80;
    gDialogWindows[index].m3C4_waitIcon.m1C_sprt[0].v0 = 0xC0;
    gDialogWindows[index].m3C4_waitIcon.m1C_sprt[0].w = 0xC;
    gDialogWindows[index].m3C4_waitIcon.m1C_sprt[0].h = 8;
    gDialogWindows[index].m3C4_waitIcon.m1C_sprt[0].x0 = 0;
    gDialogWindows[index].m3C4_waitIcon.m1C_sprt[0].y0 = 0;
    gDialogWindows[index].m3C4_waitIcon.m1C_sprt[1] = gDialogWindows[index].m3C4_waitIcon.m1C_sprt[0];

    rect.x = dialogWindowVramRects[8 * 4 + 0];
    rect.y = dialogWindowVramRects[8 * 4 + 1];
    rect.w = dialogWindowVramRects[8 * 4 + 2];
    rect.h = dialogWindowVramRects[8 * 4 + 3];
    SetDrawMode(&gDialogWindows[index].m37C_cursor.m8_drawMode[0], 0, 0, GetTPage(0, 0, 0x288, 0x1C0), &rect);
    SetDrawMode(&gDialogWindows[index].m37C_cursor.m8_drawMode[1], 0, 0, GetTPage(0, 0, 0x288, 0x1C0), &rect);
    SetSprt(&gDialogWindows[index].m37C_cursor.m20_sprt[0]);
    setRGB0(&gDialogWindows[index].m37C_cursor.m20_sprt[0], 0x80, 0x80, 0x80);
    gDialogWindows[index].m37C_cursor.m20_sprt[0].clut = GetClut(0x100, 0xF6);
    gDialogWindows[index].m37C_cursor.m20_sprt[0].u0 = 0x80;
    gDialogWindows[index].m37C_cursor.m20_sprt[0].v0 = 0xC0;
    gDialogWindows[index].m37C_cursor.m20_sprt[0].w = 8;
    gDialogWindows[index].m37C_cursor.m20_sprt[0].h = 0xC;
    gDialogWindows[index].m37C_cursor.m20_sprt[0].x0 = 0;
    gDialogWindows[index].m37C_cursor.m20_sprt[0].y0 = 0;
    gDialogWindows[index].m37C_cursor.m20_sprt[1] = gDialogWindows[index].m37C_cursor.m20_sprt[0];

    gDialogWindows[index].m40A_waitIconDelay = 2;

    for (i = 0; i < 8; i++) {
        rect.x = dialogWindowVramRects[i * 4 + 0];
        rect.y = dialogWindowVramRects[i * 4 + 1];
        rect.w = dialogWindowVramRects[i * 4 + 2];
        rect.h = dialogWindowVramRects[i * 4 + 3];
        SetDrawMode(&gDialogWindows[index].mAC_frame.m50_borderDrawModes[0][i], 0, 0, GetTPage(0, 2, 0x280, 0x1F0), &rect);
        SetDrawMode(&gDialogWindows[index].mAC_frame.m50_borderDrawModes[1][i], 0, 0, GetTPage(0, 2, 0x280, 0x1F0), &rect);
        SetSprt(&gDialogWindows[index].mAC_frame.m140_borders[0][i]);
        setRGB0(&gDialogWindows[index].mAC_frame.m140_borders[0][i], 0x80, 0x80, 0x80);
        gDialogWindows[index].mAC_frame.m140_borders[0][i].clut = GetClut(0x100, 0xF4);
        SetSemiTrans(&gDialogWindows[index].mAC_frame.m140_borders[0][i], 1);
        gDialogWindows[index].mAC_frame.m140_borders[0][i].u0 = 0x80;
        gDialogWindows[index].mAC_frame.m140_borders[0][i].v0 = 0xC0;
        gDialogWindows[index].mAC_frame.m140_borders[0][i].w = dialogWindowVramRects[i * 4 + 2];
        gDialogWindows[index].mAC_frame.m140_borders[0][i].h = dialogWindowVramRects[i * 4 + 3];
        gDialogWindows[index].mAC_frame.m140_borders[0][i].x0 = 0;
        gDialogWindows[index].mAC_frame.m140_borders[0][i].y0 = 0;
        /* a copy through casts, not the plain array assignment: its addresses
         * must be built like the setRGB0 pointer so CSE shares them */
        *(SPRT*)&gDialogWindows[index].mAC_frame.m140_borders[1][i] = *(SPRT*)&gDialogWindows[index].mAC_frame.m140_borders[0][i];
    }

    rect.y = 0;
    rect.x = 0;
    rect.h = 0xFF;
    rect.w = 0xFF;
    SetDrawMode(&gDialogWindows[index].m42C_portraitDrawMode[0], 0, 0, GetTPage(1, 0, 0x2C0, 0x100), &rect);
    SetDrawMode(&gDialogWindows[index].m42C_portraitDrawMode[1], 0, 0, GetTPage(1, 0, 0x2C0, 0x100), &rect);
    SetPolyFT4(&gDialogWindows[index].m444_portraitPoly[0]);
    setRGB0(&gDialogWindows[index].m444_portraitPoly[0], 0x80, 0x80, 0x80);
    gDialogWindows[index].m444_portraitPoly[0].clut = GetClut(0, 0xE0);
    gDialogWindows[index].m444_portraitPoly[0].tpage = GetTPage(1, 0, 0x2C0, 0x100);
    gDialogWindows[index].m444_portraitPoly[1] = gDialogWindows[index].m444_portraitPoly[0];
}

void setupDialogWindowPortraitPoly(s32 index, s32 setup) {
    DialogWindow* w = &gDialogWindows[index];

    gDialogWindows[index].m444_portraitPoly[1].u0 = w->m444_portraitPoly[0].u0 = dialogWindowPortraitPolySetups[setup * 2 + 0];
    gDialogWindows[index].m444_portraitPoly[1].v0 = w->m444_portraitPoly[0].v0 = dialogWindowPortraitPolySetups[setup * 2 + 1];
    gDialogWindows[index].m444_portraitPoly[1].u1 = w->m444_portraitPoly[0].u1 = dialogWindowPortraitPolySetups[setup * 2 + 0] + 0x40;
    gDialogWindows[index].m444_portraitPoly[1].v1 = w->m444_portraitPoly[0].v1 = dialogWindowPortraitPolySetups[setup * 2 + 1];
    gDialogWindows[index].m444_portraitPoly[1].u2 = w->m444_portraitPoly[0].u2 = dialogWindowPortraitPolySetups[setup * 2 + 0];
    gDialogWindows[index].m444_portraitPoly[1].v2 = w->m444_portraitPoly[0].v2 = dialogWindowPortraitPolySetups[setup * 2 + 1] + 0x40;
    gDialogWindows[index].m444_portraitPoly[1].u3 = w->m444_portraitPoly[0].u3 = dialogWindowPortraitPolySetups[setup * 2 + 0] + 0x40;
    gDialogWindows[index].m444_portraitPoly[1].v3 = w->m444_portraitPoly[0].v3 = dialogWindowPortraitPolySetups[setup * 2 + 1] + 0x40;
    gDialogWindows[index].m444_portraitPoly[1].clut = w->m444_portraitPoly[0].clut = GetClut(0, setup + 0xE0);
}

s32 releaseDialogWindow(s16 index) {
    DialogWindow18* p;

    if (gDialogWindows[index].m40E_state == 0) {
        p = &gDialogWindows[index].m18_text;
        resetDialogWindow18(p);
        addDialogWindowsToOTSub1(p);
        dealocateDialogWindow18Memory(p);
        gDialogWindows[index].m37C_cursor.m0_state = -1;
        gDialogWindows[index].m40E_state = -1;
        gDialogWindows[index].m414_keepOpen = -1;
        gDialogWindows[index].m410_priority = 0xFFFF;
        dialogWindowVar1[index] = -1;
        windowOpenBF[0] &= (1 << index) ^ 0xFF;
        gDialogWindows[index].m416_fieldActorId = 0xFF;
        gDialogWindows[index].m412_onTop = 0;
        return 0;
    }
    return -1;
}

void projectActorToScreen(s32 actor, s32* outX, s32* outY, s32 offsetY) {
    SVECTOR v;
    MATRIX m;
    s32 sxy;
    s32 depth;
    s32 flag;

    CompMatrix(&g_currentProjectionMatrix, &fieldSceneState.m290_actorArray[actor].mC_matrix, &m);
    SetRotMatrix(&m);
    SetTransMatrix(&m);
    v.vx = 0;
    v.vy = offsetY;
    v.vz = 0;
    RotTransPers(&v, &sxy, &depth, &flag);
    *outY = ((s16*)&sxy)[1];
    *outX = ((s16*)&sxy)[0];
}

s32 createDialogWindow(s16 x, s16 y, s32 dialogIndex, s32 windowIndex, s32 width, s32 height,
                       s32 fieldActorId, s32 actorId, s32 mode, u32 flags, u32 flags2) {
    u32 flagUpper;
    s32 i;
    s32 entry;
    s32 xOffset;
    s32 posX; /* two scalars, not an array: a fixed-address scalar does not */
    s32 posY; /* alias the gDialogWindows[] stores, so posY loads early */

    y -= 8;
    if (((u32)fieldSceneState.m290_actorArray[fieldActorId].m4C_scriptEntity->m84_dialogWindowFlags >> 16) == 0) {
        flagUpper = (u16)fieldSceneState.m290_actorArray[fieldActorId].m4C_scriptEntity->m84_dialogWindowFlags;
    } else {
        flagUpper = (u16)((u32)fieldSceneState.m290_actorArray[fieldActorId].m4C_scriptEntity->m84_dialogWindowFlags >> 16);
    }
    flagUpper |= flags;
    for (i = 0; i < 4; i++) {
        entry = createDialogWindowCounter & 3;
        createDialogWindowCounter++;
        if (dialogWindowVar1[entry] == -1) {
            dialogWindowVar1[entry] = 0;
            break;
        }
    }
    entry = windowIndex;
    gDialogWindows[windowIndex].m18_text.m70_vars[0] = getVariable(0x16);
    gDialogWindows[windowIndex].m18_text.m70_vars[1] = getVariable(0x18);
    gDialogWindows[windowIndex].m18_text.m70_vars[2] = getVariable(0x1A);
    gDialogWindows[windowIndex].m18_text.m70_vars[3] = getVariable(0x1C);
    gDialogWindows[windowIndex].m18_text.m80_stringRef = gDialogWindows[windowIndex].m18_text.m70_vars[3];
    switch (mode) {
    case 2:
        posX = 0xA0;
        posY = y + 0x20;
        break;
    case 3:
        posX = x + 8;
        posX += width * 2;
        posY = y + 8;
        posY += height * 7;
        break;
    default:
        projectActorToScreen(actorId, &posX, &posY, -0x40);
        break;
    }
    if ((u8)fieldSceneState.m290_actorArray[fieldActorId].m4C_scriptEntity->m80_dialogPortrait != 0xFF && !(flagUpper & 2)) {
        if ((flagUpper & 0x402) == 0) {
            setupDialogWindowPortraitPoly(windowIndex, (fieldSceneState.m290_actorArray[fieldActorId].m4C_scriptEntity->m12C_x2 << 1) | 1);
        } else {
            setupDialogWindowPortraitPoly(windowIndex, fieldSceneState.m290_actorArray[fieldActorId].m4C_scriptEntity->m12C_x2 << 1);
        }
        gDialogWindows[windowIndex].m495_portrait = fieldSceneState.m290_actorArray[fieldActorId].m4C_scriptEntity->m80_dialogPortrait;
        gDialogWindows[windowIndex].m494_hasPortrait = 1;
    } else {
        gDialogWindows[windowIndex].m495_portrait = 0x80;
        gDialogWindows[windowIndex].m494_hasPortrait = 0;
    }
    gDialogWindows[windowIndex].m37C_cursor.m0_state = -1;
    setupWindowSize1(windowIndex, x, y, width * 4 + 0x10, height * 14 + 0x10);
    xOffset = 0;
    if ((u8)fieldSceneState.m290_actorArray[fieldActorId].m4C_scriptEntity->m80_dialogPortrait != 0xFF) {
        xOffset = -((flagUpper & 0x402) == 0) & 0x44;
    }
    SETUP_WINDOW_SIZE2(&gDialogWindows[windowIndex].m18_text, windowsDefaultPositions[entry * 2], windowsDefaultPositions[entry * 2 + 1],
                       x + 8 + xOffset, y + 8, width, height);
    if (flagUpper & 0x400) {
        gDialogWindows[windowIndex].m40C_flags |= 0x20;
    }
    if (dialogWindowOpenAnimationNumFrames == 8) {
        gDialogWindows[windowIndex].m18_text.m68_charsPerStep = 1;
    } else {
        gDialogWindows[windowIndex].m18_text.m68_charsPerStep = 2;
    }
    if (pcObserveWindow) pcObserveWindow(&gDialogWindows[windowIndex].m18_text, fieldMapNumber, fieldActorId, dialogIndex);
    gDialogWindows[windowIndex].mA8_dialogPointer = getDialogParamPointer(rawFieldDialogBundle, dialogIndex);
    gDialogWindows[windowIndex].m40E_state = 0;
    gDialogWindows[windowIndex].m18_text.m10_flags |= 2;
    /* through the aggregate: the load then may alias the stores above */
    gDialogWindows[windowIndex].m408_openAnimationCounter = op9DVar1.m52_dialogWindowOpenAnimationNumFrames;
    gDialogWindows[windowIndex].m416_fieldActorId = fieldActorId;
    gDialogWindows[windowIndex].m418_actorId = actorId;
    if ((flags2 & 0x800) == 0) {
        gDialogWindows[windowIndex].m412_onTop = 0;
    } else {
        gDialogWindows[windowIndex].m412_onTop = 1;
    }
    width = width * 2 + 8;
    height = height * 7 + 8;
    gDialogWindows[windowIndex].m41C_openOffset[0] = ((posX - width) - x) << 16;
    gDialogWindows[windowIndex].m41C_openOffset[1] = ((posY - height) - y) << 16;
    if ((flagUpper & 0x100) == 0) {
        gDialogWindows[windowIndex].m424_openOffsetStep[0] = -R3000_DIV(gDialogWindows[windowIndex].m41C_openOffset[0], op9DVar1.m52_dialogWindowOpenAnimationNumFrames);
        gDialogWindows[windowIndex].m424_openOffsetStep[1] = -R3000_DIV(gDialogWindows[windowIndex].m41C_openOffset[1], op9DVar1.m52_dialogWindowOpenAnimationNumFrames);
    } else {
        gDialogWindows[windowIndex].m408_openAnimationCounter = 1;
        gDialogWindows[windowIndex].m424_openOffsetStep[0] = -gDialogWindows[windowIndex].m41C_openOffset[0];
        gDialogWindows[windowIndex].m424_openOffsetStep[1] = -gDialogWindows[windowIndex].m41C_openOffset[1];
    }
    if ((fieldSceneState.m290_actorArray[actorId].m4C_scriptEntity->m4_flags & 0x200) && !(flagUpper & 1)) {
        gDialogWindows[windowIndex].m414_keepOpen = 0;
        return -1;
    }
    return 0;
}

void releaseAllDialogWindows(void) {
    s32 i;

    for (i = 0; i < 4; i++) {
        if (gDialogWindows[i].m40E_state == 0) {
            releaseDialogWindow(i);
        }
    }
}

extern s32 g_playTimeInVsync; /* main: play time in vsyncs */
/* The auto-advance setting (off by default): 1 when it presses confirm for
 * the dialogue this frame, as a real press would (padButtonForDialogs 0x20,
 * the same windows). It waits while any window offers a choice (cursor state
 * 0), so it never answers one; it times a page while a window the press
 * reaches is stopped with its wait icon's condition (stopped, no choice). */
static s32 pcDialogAutoConfirm(void) {
    s32 i;
    s32 waiting = 0;

    for (i = 0; i < 4; i++) {
        if (gDialogWindows[i].m40E_state != 0) {
            continue;
        }
        if (gDialogWindows[i].m37C_cursor.m0_state == 0) {
            return pcGameplayDialogueAuto(0, 0, g_playTimeInVsync);
        }
        if (gDialogWindows[i].m408_openAnimationCounter == 0 && ADD_DIALOG_SUB0(&gDialogWindows[i].m18_text) != 0 &&
            (gDialogWindows[i].m412_onTop != 0 || gDialogWindows[i].m410_priority == 0)) {
            waiting = 1;
        }
    }
    return pcGameplayDialogueAuto(0, waiting, g_playTimeInVsync);
}

void addDialogWindowsToOT(OTag* ot, s32 odd) {
    s32 processed[4];
    s32 counter;
    s32 j;
    s32 last;
    s32 i;
    DialogWindow18* p;
    uptr head;
    /* Enhanced (xg-hbc.11): at 60 Hz the dialogue's confirm (timed or pressed)
     * is handled on the tick frames only, after the script VM has run, with the
     * press from the frame between carried in: the native order was VM first,
     * then the close, in one frame. Closing on the frame between let the next
     * tick's VM see the dialogue closed and the same press still in its mask,
     * which started a sign's talk again (the user's report). The page's
     * re-queue (below) and the queue pop that starts printing it
     * (updateAndRenderTextForDialogWindow, main/dialog.c) are tick-only too:
     * natively the script VM runs between the confirm's frame and the next
     * draw and closes the window or sets the next page; drawn on the frame
     * between, the finished page was popped and printed again (the user's
     * "it just reopens the sign dialog"). */
    s32 pcConfirm;
    if (pcFieldOffTick()) {
        pcConfirm = 0;
    } else {
        pcConfirm = pcDialogAutoConfirm(); /* timed, then a real press too */
        pcConfirm |= (padButtonForDialogs & 0x20) != 0;
    }

    pcDialogPreviewPlan(pcFieldOffTick()); /* Enhanced (xg-hbc.19.5): what the next tick will show, a frame between draws */
    if (!pcFieldOffTick()) { /* Enhanced: the cursor and wait icon animate at the native rate (xg-hbc.19.3) */
        dialogWindowsFrameCount++;
        if ((dialogWindowsFrameCount & 3) == 0) {
            dialogWindowsFrameCount2++;
        }
    }
    if (dialogWindowsFrameCount2 >= 5) {
        dialogWindowsFrameCount2 = 0;
    }
    last = 0xFF;
    for (i = 0; i < 4; i++) {
        if (gDialogWindows[i].m412_onTop != 0) {
            last = i;
        }
    }
    counter = 0;
    for (i = 0; i < 4; i++) {
        processed[i] = 0xFFFF;
    }
    for (i = 0; i < 4; i++) {
        if (gDialogWindows[i].m40E_state == 0 && gDialogWindows[i].m412_onTop != 0 &&
            !(pcDialogPreview[i] & PC_DIALOG_HIDE)) {
            head = pcFieldOtHead(ot); /* the window's packets: moved together */
            p = &gDialogWindows[i].m18_text;
            gDialogWindows[i].m3C4_waitIcon.m0_state = -1;
            if (gDialogWindows[i].m408_openAnimationCounter == 0) {
                if (ADD_DIALOG_SUB0(p) != 0 && gDialogWindows[i].m37C_cursor.m0_state != 0) {
                    gDialogWindows[i].m3C4_waitIcon.m0_state = 0;
                }
                if (pcConfirm) {
                    gDialogWindows[i].m37C_cursor.m0_state = -1;
                    fieldSceneState.m290_actorArray[gDialogWindows[i].m416_fieldActorId].m4C_scriptEntity->m81_selectedMultichoiceLine =
                        gDialogWindows[i].m37C_cursor.m6_currentMultiChoiceSelection + gDialogWindows[i].m37C_cursor.m2_startOfMultiChoice;
                    addDialogWindowsToOTSub1(p);
                }
                if (p->m82_queueCount == 0 && !pcFieldOffTick()) { /* Enhanced: see pcConfirm above */
                    addDialogWindowsToOTSub2(p, gDialogWindows[i].mA8_dialogPointer);
                }
                pcDialogRenderText(i, p, ot, odd);
            }
            addPrim(ot, &gDialogWindows[i].m0_drawMode[odd]);
            dialogWindowSetupForRendering(ot, odd, i);
            updateMultiChoice(i, ot, odd);
            pcFieldOtShift(ot, head, pcFieldDialogDx(i));
        }
    }
    for (j = 0; j < 4; j++) {
        for (i = 0; i < 4; i++) {
            if (gDialogWindows[i].m410_priority == j) {
                processed[i] = counter++;
                if (gDialogWindows[i].m40E_state == 0 && i != last && !(pcDialogPreview[i] & PC_DIALOG_HIDE)) {
                    head = pcFieldOtHead(ot);
                    p = &gDialogWindows[i].m18_text;
                    gDialogWindows[i].m3C4_waitIcon.m0_state = -1;
                    if (gDialogWindows[i].m408_openAnimationCounter == 0) {
                        if (pcConfirm && gDialogWindows[i].m410_priority == 0) {
                            gDialogWindows[i].m37C_cursor.m0_state = -1;
                            fieldSceneState.m290_actorArray[gDialogWindows[i].m416_fieldActorId].m4C_scriptEntity->m81_selectedMultichoiceLine =
                                gDialogWindows[i].m37C_cursor.m6_currentMultiChoiceSelection + gDialogWindows[i].m37C_cursor.m2_startOfMultiChoice;
                            addDialogWindowsToOTSub1(p);
                        }
                        if (p->m82_queueCount == 0 && !pcFieldOffTick()) { /* Enhanced: see pcConfirm above */
                            addDialogWindowsToOTSub2(p, gDialogWindows[i].mA8_dialogPointer);
                        }
                        pcDialogRenderText(i, p, ot, odd);
                        if (ADD_DIALOG_SUB0(p) != 0 && gDialogWindows[i].m37C_cursor.m0_state != 0) {
                            gDialogWindows[i].m3C4_waitIcon.m0_state = 0;
                        }
                    }
                    addPrim(ot, &gDialogWindows[i].m0_drawMode[odd]);
                    dialogWindowSetupForRendering(ot, odd, i);
                    updateMultiChoice(i, ot, odd);
                    pcFieldOtShift(ot, head, pcFieldDialogDx(i));
                }
                if (gDialogWindows[i].m410_priority == 0xFFFF) {
                    processed[i] = 0xFFFF;
                }
            }
        }
    }
    for (j = 0; j < 4; j++) {
        gDialogWindows[j].m410_priority = processed[j];
    }
    addPrim(ot, &gDialogDrModes[g_frameOddOrEven][0]);
}

void stepDialogWindows(void) {
    s32 i;

    for (i = 0; i < 4; i++) {
        if (gDialogWindows[i].m40E_state == 0) {
            if (gDialogWindows[i].m408_openAnimationCounter == 0 && !(gDialogWindows[i].m18_text.m10_flags & 4)) {
                releaseDialogWindow(i);
            }
            if (gDialogWindows[i].m414_keepOpen == 0) {
                releaseDialogWindow(i);
            }
            if (gDialogWindows[i].m408_openAnimationCounter != 0) {
                gDialogWindows[i].m408_openAnimationCounter--;
            }
        }
    }
}

s32 findTopPriorityDialogWindow(void) {
    s32 i;

    for (i = 0; i < 4; i++) {
        if (gDialogWindows[i].m410_priority == 0) {
            return i;
        }
    }
    return 0xFFFF;
}

s32 findFreeDialogWindow(void) {
    s32 i;

    for (i = 0; i < 4; i++) {
        if (gDialogWindows[i].m410_priority == 0xFFFF) {
            return 0;
        }
    }
    return -1;
}

s32 getWindowWithLowestPriority(void) {
    s32 best = 0;
    s32 index = 0xFFFF;
    s32 i;

    for (i = 0; i < 4; i++) {
        s32 cur = gDialogWindows[i].m410_priority;
        if (cur != 0xFFFF && cur >= best) {
            best = cur;
            index = i;
        }
    }
    return index;
}

s32 allocateDialogWindow(void) {
    s32 i;

    for (i = 0; i < 4; i++) {
        if (gDialogWindows[i].m410_priority != 0xFFFF) {
            gDialogWindows[i].m410_priority++;
        }
    }
    for (i = 0; i < 4; i++) {
        if (gDialogWindows[i].m410_priority == 0xFFFF) {
            gDialogWindows[i].m410_priority = 0;
            return i;
        }
    }
    return 0xFFFF;
}
