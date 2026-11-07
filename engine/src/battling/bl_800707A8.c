/* Battling overlay, provisional file 1 of 4 (0x800707A8-0x800725B0): the
 * tutorial / scripted-scene interpreter, camera moves and the ending. */

#include "common.h"
#include "psyq.h"
#include "main/dialog.h"
#include "main/sound.h"
#include "battling/battling.h"

extern u8 battlingEntryParams; /* main: byte 0 (mode) of the field->battling entry block; the field writes it as battlingEntryParams[0..5] */
extern u8 battlingResult; /* battlingEntryParams[6] */

void stopAllSounds(void);
void mergeQueuedInputs(void);
s32 getAngleCos(s32 angle); /* main: cosine (Noah: getAngleCos) */
s32 getAngleSin(s32 angle); /* main: sine (Noah: getAngleSin) */
void moveBattlingSceneToArea(s32 mode);
void settleBattlingGearOnGround(BattlingGear* gear);
s32 runBattlingScriptSpecialOp(s32 arg0);
void pushBattlingGearInput(BattlingGear* g, u8 v);
void clearBattlingGearInput(BattlingGear*);
void updateBattlingFight(BattlingGear*, BattlingGear*);
s32 spawnBattlingHitEffect(VECTOR* pos, s32 kind);
void clearBattlingSplashAndDust(void);
s32 getBattlingGroundHeight(VECTOR* pos, s32 flag);
u32 getBattlingMapCell(VECTOR* pos); /* the map cell word (flags in bits 24-25) */
void clampBattlingMoveToArena(VECTOR* a, VECTOR* b, s32 c);
void updateBattlingScriptFightCamera(VECTOR* a, VECTOR* b);
void clearBattlingVram(s32);
void setBattlingSceneMode(s32);
void exitBattling(void);
void drawBattlingFadeTile(void*, s32, s32);
void playBattlingSfx(s32);
s32 isBattlingGearInWater(BattlingGear*);
extern u16 newPadButtonForDialogs; /* main: newPadButtonForDialogs */
extern u16 newPadButtonForField; /* main: pad 0 auto-repeat */
extern u8 battlingEmptyScript[4]; /* a script that ends at once (op 0) */
extern u8* battlingTutorialScripts[];
extern s32 battlingNeighborDirections[16]; /* 8 (x, z) step directions */
extern u8 battlingEventScript[];
extern s32 battlingScriptTextId;
extern s32 battlingShownTextId;
extern s32 D_800925DC;
extern s32 battlingTutorialCursorX;
extern s32 battlingTutorialCursorY;
extern s32 battlingTutorialCursorTargetX;
extern s32 battlingTutorialCursorTargetY;
extern u8 battlingTutorialCursorShown;
extern s32 battlingScriptCameraLift;
extern u8* battlingScriptPtr;
extern s32 battlingScriptWait;
extern s16 battlingScriptGaugeHold;
extern s8 battlingTutorialChoice;
extern u8 battlingTutorialMenuActive;
extern s16 battlingEventFade;
extern s32 battlingGearDistance;
extern BattlingDisplayBuffer* battlingDisplayBuffer; /* current display buffer */
extern u8* battlingDialogTexts;
extern u8 battlingRubberBandMode;
extern BattlingGear* battlingScriptGear;
extern u8 battlingBufferIndex;
extern s32 battlingGameMode;
extern u8 battlingFightActive;
extern s32 battlingFrameCounter;
extern s32 battlingEventState;
extern s32 battlingScriptCameraMode;
extern s32 battlingGearAngle;
extern OTag* battlingCurrentOt; /* current OT */
extern u8 battlingEventStarted;
extern void* battlingMusic;
extern DialogWindow18 battlingTutorialMenuWindow;
extern DR_TPAGE battlingTutorialCursorTPages[2];
extern VECTOR battlingEventSparkPoints[3]; /* pad: countdown */
extern BattlingGear battlingGear2;
extern VECTOR battlingCameraTarget; /* camera target */
extern DialogWindow18 battlingMessageWindow;
extern VECTOR battlingCameraPos; /* camera position */
extern BattlingGear battlingGear1;
/* Splat names inside battlingGear1: its PC layout differs. */
extern BattlingSettings battlingSettings;
/* The display buffers' cursor sprites (m74_sprtTag on): names inside battlingDisplayBuffers. */
extern BattlingDisplayBuffer battlingDisplayBuffers[2];

/* The battling code sees addDialogWindowsToOTSub0 as returning an int (no
 * u8 truncation after the call). */
#define ADD_DIALOG_SUB0(p) (((s32 (*)(DialogWindow18*))addDialogWindowsToOTSub0)(p))

void endBattlingTutorial(void) {
    setBattlingSceneMode(3);
    dealocateDialogWindow18Memory(&battlingTutorialMenuWindow);
}

s32 getBattlingApproachStep(s32 a, s32 b, s32 c) {
    s32 d = a - b;
    if (d < 0) {
        d++;
        d -= c;
    } else {
        d--;
        d += c;
    }
    return R3000_DIV(d, c);
}

void moveBattlingCameraTargetTo(VECTOR* p, s32 c) {
    battlingCameraTarget.vx += getBattlingApproachStep(p->vx, battlingCameraTarget.vx, c);
    battlingCameraTarget.vz += getBattlingApproachStep(p->vz, battlingCameraTarget.vz, c);
    battlingCameraTarget.vy += getBattlingApproachStep(p->vy, battlingCameraTarget.vy + battlingScriptCameraLift, c);
}

void moveBattlingCameraTo(VECTOR* p, s32 c) {
    VECTOR d;

    d.vx = getBattlingApproachStep(p->vx, battlingCameraPos.vx, c);
    d.vy = getBattlingApproachStep(p->vy, battlingCameraPos.vy, c);
    d.vz = getBattlingApproachStep(p->vz, battlingCameraPos.vz, c);
    clampBattlingMoveToArena(&battlingCameraPos, &d, 0x3D00);
    battlingCameraPos.vx += d.vx;
    battlingCameraPos.vy += d.vy;
    battlingCameraPos.vz += d.vz;
}

void updateBattlingScriptCamera(s32 mode) {
    VECTOR v;
    s32 y;
    s32 t;

    switch (mode) {
    case 0:
        updateBattlingScriptFightCamera(&battlingGear1, &battlingGear2);
        break;
    case 1:
        v = battlingGear1.m0_position;
        v.vy -= 0xA0;
        battlingScriptCameraLift = 0;
        moveBattlingCameraTargetTo(&v, 4);
        break;
    case 2:
        v = battlingGear2.m0_position;
        v.vy -= 0xA0;
        battlingScriptCameraLift = 0;
        moveBattlingCameraTargetTo(&v, 4);
        break;
    case 3:
        battlingScriptCameraLift = 0xA0;
        goto near;
    case 4:
        battlingScriptCameraLift = 0x80;
    near:
        v = battlingGear1.m94C_target;
        v.vy += battlingScriptCameraLift;
        moveBattlingCameraTargetTo(&v, 0x10);
        v.vx = battlingGear1.m0_position.vx + ((getAngleCos(battlingGear1.m54_rotY + 0xA80) * 0xD0) >> 12);
        t = battlingGear1.m0_position.vy - 0x20;
        v.vy = t - battlingScriptCameraLift;
        v.vz = battlingGear1.m0_position.vz + ((getAngleSin(battlingGear1.m54_rotY + 0xA80) * 0xD0) >> 12);
        moveBattlingCameraTo(&v, 0x46);
        y = getBattlingGroundHeight(&battlingCameraPos, 0) - 0x40;
        if (y - battlingScriptCameraLift < battlingCameraPos.vy) {
            battlingCameraPos.vy = y - battlingScriptCameraLift;
        }
        break;
    case 5:
        battlingCameraTarget.vx = battlingGear2.m0_position.vx;
        battlingCameraTarget.vy = battlingGear2.m0_position.vy - 0xC0;
        battlingCameraTarget.vz = battlingGear2.m0_position.vz;
        battlingCameraPos.vx = battlingCameraTarget.vx + ((getAngleCos(battlingGear2.m54_rotY + 0x900) * 0xE0) >> 12);
        battlingCameraPos.vy = battlingGear2.m0_position.vy - 0xD0;
        battlingCameraPos.vz = battlingCameraTarget.vz + ((getAngleSin(battlingGear2.m54_rotY + 0x900) * 0xE0) >> 12);
        break;
    }
}

void moveBattlingSceneToArea(s32 mode) {
    VECTOR player;
    VECTOR opponent;
    VECTOR camera;
    VECTOR center;

    player = battlingGear1.m0_position;
    opponent = battlingGear2.m0_position;
    camera = battlingCameraPos;
    center = player;
    center.vx += opponent.vx;
    center.vy += opponent.vy;
    center.vz += opponent.vz;
    center.vx /= 2;
    center.vy /= 2;
    center.vz /= 2;
    player.vx -= center.vx;
    player.vy -= center.vy;
    player.vz -= center.vz;
    opponent.vx -= center.vx;
    opponent.vy -= center.vy;
    opponent.vz -= center.vz;
    camera.vx -= center.vx;
    camera.vy -= center.vy;
    camera.vz -= center.vz;
    switch (mode) {
    case 0:
        center.vx = 0x4000;
        center.vy = 0;
        center.vz = 0x4000;
        break;
    case 1:
        center.vx = 0x6000;
        center.vy = 0;
        center.vz = 0x6000;
        break;
    case 2:
        center.vx = 0x2000;
        center.vy = 0;
        center.vz = 0x2000;
        break;
    case 3:
        center.vx = 0x4000;
        center.vy = 0;
        center.vz = 0x2400;
        break;
    }
    player.vx += center.vx;
    player.vy += center.vy;
    player.vz += center.vz;
    opponent.vx += center.vx;
    opponent.vy += center.vy;
    opponent.vz += center.vz;
    camera.vx += center.vx;
    camera.vy += center.vy;
    camera.vz += center.vz;
    player.vy = 0;
    opponent.vy = 0;
    camera.vy = -0x300;
    battlingGear1.m0_position = player;
    battlingGear2.m0_position = opponent;
    battlingCameraPos = camera;
    clearBattlingSplashAndDust();
}

void startBattlingScript(u8* script) {
    battlingScriptPtr = script;
    battlingGear1.m48_speed = 0;
    battlingGear2.m48_speed = 0;
    battlingScriptWait = 0;
    battlingGear1.mD0_status.bits.b15_scriptTurn = 0;
    battlingGear2.mD0_status.bits.b15_scriptTurn = 0;
}

s32 steerBattlingGearOutOfWater(BattlingGear* gear) {
    VECTOR v;

    v = gear->m0_position;
    v.vx -= 0x3F80;
    v.vz -= 0x3F80;
    if ((ratan2(v.vx, v.vz) & 0xFFF) > 0x200) {
        gear->m58_targetRotation = 0x800 - battlingGearAngle;
    } else {
        gear->m58_targetRotation = 0xC00 - battlingGearAngle;
    }
    gear->m48_speed = 0xFF;
    gear->mCE_inputRotation = 0;
    return 0;
}

s32 runBattlingScript(void) {
    BattlingGear* gear;
    s32 ret;

    gear = battlingScriptGear;
    if (gear == NULL) {
        /* Before a script has picked a gear the PS1 writes through NULL,
         * into low RAM; the PC uses a scratch gear. */
        static BattlingGear pcNoGear;
        gear = &pcNoGear;
    }
    gear->m48_speed = 0;
    if (battlingScriptGaugeHold != 0) {
        if (battlingScriptGaugeHold < gear->mB6_gauge) {
            battlingScriptGaugeHold = gear->mB6_gauge;
        }
        gear->mB6_gauge = battlingScriptGaugeHold;
    }
    while (1) {
        switch (*battlingScriptPtr) {
        case 0:
            IMPLICIT_RETURN(0);
        case 1:
            if (battlingScriptWait != 0) {
                if (--battlingScriptWait != 0) {
                    IMPLICIT_RETURN(0);
                }
                battlingScriptPtr += 2;
                break;
            }
            battlingScriptWait = battlingScriptPtr[1];
            IMPLICIT_RETURN(0);
        case 2:
            battlingScriptGear = &battlingGear1;
            gear = battlingScriptGear;
            battlingScriptPtr++;
            break;
        case 3:
            battlingScriptGear = &battlingGear2;
            gear = battlingScriptGear;
            battlingScriptPtr++;
            break;
        case 4:
            clearBattlingGearInput(gear);
            battlingScriptPtr++;
            break;
        case 5:
            pushBattlingGearInput(gear, 1);
            battlingScriptPtr++;
            break;
        case 6:
            pushBattlingGearInput(gear, 2);
            battlingScriptPtr++;
            break;
        case 8:
            pushBattlingGearInput(gear, 3);
            battlingScriptPtr++;
            break;
        case 7:
            pushBattlingGearInput(gear, 4);
            battlingScriptPtr++;
            break;
        case 10:
            pushBattlingGearInput(gear, 5);
            battlingScriptPtr++;
            break;
        case 9:
            pushBattlingGearInput(gear, 5);
            battlingScriptPtr++;
            break;
        case 11:
            if (battlingGearDistance < 0x100) {
                battlingScriptPtr++;
                break;
            }
            gear->m48_speed = 0xF0;
            gear->m58_targetRotation = 0;
            gear->mCE_inputRotation = 0;
            IMPLICIT_RETURN(0);
        case 12:
            if (battlingGearDistance > 0x400) {
                battlingScriptPtr++;
                break;
            }
            gear->m48_speed = 0xF0;
            gear->m58_targetRotation = 0x800;
            gear->mCE_inputRotation = 0;
            IMPLICIT_RETURN(0);
        case 13:
            if (isBattlingGearInWater(gear) != 0) {
                steerBattlingGearOutOfWater(gear);
                IMPLICIT_RETURN(0);
            }
            if (battlingGearDistance > 0x800) {
                battlingScriptPtr++;
                break;
            }
            gear->m48_speed = 0xF0;
            gear->m58_targetRotation = 0x800;
            gear->mCE_inputRotation = 0;
            IMPLICIT_RETURN(0);
        case 15:
            if (battlingScriptWait != 0) {
                if (--battlingScriptWait == 0) {
                    battlingScriptPtr += 2;
                    break;
                }
                gear->m48_speed = 0xF0;
                gear->m58_targetRotation = 0x400;
                gear->mCE_inputRotation = 0;
                IMPLICIT_RETURN(0);
            }
            battlingScriptWait = battlingScriptPtr[1];
            IMPLICIT_RETURN(0);
        case 14:
            if (battlingScriptWait != 0) {
                if (--battlingScriptWait == 0) {
                    battlingScriptPtr += 2;
                    break;
                }
                gear->m48_speed = 0xF0;
                gear->m58_targetRotation = -0x400;
                gear->mCE_inputRotation = 0;
                IMPLICIT_RETURN(0);
            }
            battlingScriptWait = battlingScriptPtr[1];
            IMPLICIT_RETURN(0);
        case 16:
        case 17:
            break;
        case 20:
            if (battlingScriptWait != 0) {
                if (--battlingScriptWait == 0) {
                    battlingScriptPtr += 2;
                    break;
                }
                gear->m48_speed = 0xF0;
                gear->m58_targetRotation = -0x400;
                gear->mCE_inputRotation = 0;
                gear->mD0_status.bits.b15_scriptTurn = 1;
                IMPLICIT_RETURN(0);
            }
            battlingScriptWait = battlingScriptPtr[1];
            IMPLICIT_RETURN(0);
        case 18:
            battlingScriptTextId = *++battlingScriptPtr;
            battlingScriptPtr++;
            break;
        case 19:
            addDialogWindowsToOTSub1(&battlingMessageWindow);
            battlingScriptPtr++;
            break;
        case 27:
            battlingTutorialCursorTargetX = 0xA0;
            battlingTutorialCursorX = 0xA0;
            battlingTutorialCursorShown = 0;
            battlingTutorialCursorTargetY = 0x6D;
            battlingTutorialCursorY = 0x6D;
            battlingScriptPtr++;
            break;
        case 21:
            battlingTutorialCursorTargetX = battlingScriptPtr[1] * 2;
            battlingTutorialCursorTargetY = battlingScriptPtr[2];
            battlingTutorialCursorShown = 1;
            battlingScriptPtr += 3;
            break;
        case 22:
            runBattlingScriptSpecialOp(battlingScriptPtr[1]);
            battlingScriptPtr += 2;
            break;
        case 23:
            battlingScriptCameraMode = battlingScriptPtr[1];
            battlingScriptPtr += 2;
            break;
        case 24:
            battlingEventState = battlingScriptPtr[1];
            battlingScriptPtr += 2;
            break;
        case 25:
            if (ADD_DIALOG_SUB0(&battlingMessageWindow) == 0 || !(newPadButtonForDialogs & 0x20)) {
                IMPLICIT_RETURN(0);
            }
            if (ADD_DIALOG_SUB0(&battlingMessageWindow) == 1) {
                battlingScriptPtr++;
            }
            addDialogWindowsToOTSub1(&battlingMessageWindow);
            break;
        case 26:
            if (ADD_DIALOG_SUB0(&battlingMessageWindow) != 0 && (newPadButtonForDialogs & 0x20)) {
                ret = ADD_DIALOG_SUB0(&battlingMessageWindow);
                if (ret < 4) {
                    if (ret >= 2) {
                        addDialogWindowsToOTSub1(&battlingMessageWindow);
                        battlingScriptPtr++;
                    }
                }
            }
            IMPLICIT_RETURN(0);
        case 28:
            if (battlingScriptPtr[1] != 0) {
                gear->mD8_opponent->mB4_hp = gear->mD8_opponent->mBC_maxHp;
            } else {
                gear->mB4_hp = gear->mBC_maxHp;
            }
            battlingScriptPtr += 2;
            break;
        case 29:
            if (battlingScriptPtr[1] != 0) {
                gear->mD8_opponent->mB4_hp = 1;
            } else {
                gear->mB4_hp = 1;
            }
            battlingScriptPtr += 2;
            break;
        case 30:
            endBattlingTutorial();
            battlingScriptPtr++;
            break;
        case 31:
            gear->mB6_gauge = battlingScriptPtr[1] << 4;
            battlingScriptPtr += 2;
            break;
        case 32:
            battlingScriptGaugeHold = battlingScriptPtr[1] << 4;
            battlingScriptPtr += 2;
            break;
        case 33:
            moveBattlingSceneToArea(battlingScriptPtr[1]);
            battlingScriptPtr += 2;
            break;
        case 34:
            if (battlingScriptPtr[1] != 0) {
                gear->mD0_status.bits.b1 = 1;
            } else {
                gear->mD0_status.bits.b1 = 0;
                gear->mD0_status.bits.b3_combo = 0;
            }
            battlingScriptPtr += 2;
            break;
        default:
            IMPLICIT_RETURN(0);
        }
    }
}

void drawBattlingTutorialCursor(void* ot) {
    *(s32*)&battlingDisplayBuffer->m7C_x = battlingTutorialCursorX | (battlingTutorialCursorY << 16);
    AddPrim(ot, &battlingDisplayBuffer->m74_sprtTag);
    pcWideBattlingCursor(&battlingDisplayBuffer->m74_sprtTag, battlingTutorialCursorX, battlingTutorialCursorY); /* follows the HUD (wide.c) */
    AddPrim(ot, &battlingTutorialCursorTPages[battlingBufferIndex]);
}

void initBattlingTutorialCursor(u32** data) {
    TIM_IMAGE tim;
    RECT rect;
    s16* clut;

    OpenTIM(data[24]);
    ReadTIM(&tim);
    clut = (s16*)tim.caddr;
    clut[2] = 0x8000;
    clut[0] = 0;
    clut[3] = -1;
    LoadImage(tim.crect, tim.caddr);
    LoadImage(tim.prect, tim.paddr);
    SetDrawTPage(&battlingTutorialCursorTPages[0], 0, 0, GetTPage(0, 1, tim.prect->x, tim.prect->y));
    battlingTutorialCursorTPages[1] = battlingTutorialCursorTPages[0];
    (*(SPRT_16*)&battlingDisplayBuffers[0].m74_sprtTag).u0 = (tim.prect->x & 0x3F) * 4;
    (*(SPRT_16*)&battlingDisplayBuffers[0].m74_sprtTag).v0 = tim.prect->y;
    (*(SPRT_16*)&battlingDisplayBuffers[0].m74_sprtTag).clut = GetClut(tim.crect->x, tim.crect->y);
    (*(SPRT_16*)&battlingDisplayBuffers[1].m74_sprtTag) = (*(SPRT_16*)&battlingDisplayBuffers[0].m74_sprtTag);
}

void initBattlingTutorialWindow(void) {
    battlingShownTextId = -1;
    battlingScriptTextId = 0;
    setupWindowSize2(&battlingMessageWindow, 0x140, 0x30, 0x1C, 0x9A, 0x40, 4);
}

void startBattlingTutorialLesson(s32 arg0) {
    battlingTutorialMenuActive = arg0 == 0;
    if (arg0 == 0) {
        playBattlingSfx(0x37);
        battlingMessageWindow.mC_height = 2;
        battlingMessageWindow.m6_y = 0xB4;
    } else {
        battlingMessageWindow.mC_height = 4;
        battlingMessageWindow.m6_y = 0x9A;
    }
    startBattlingScript(battlingTutorialScripts[arg0]);
    battlingTutorialCursorTargetX = 0xA0;
    battlingTutorialCursorX = 0xA0;
    battlingTutorialCursorTargetY = 0x6D;
    battlingTutorialCursorY = 0x6D;
    battlingScriptGaugeHold = 0;
    battlingGear1.mB4_hp = battlingGear1.mBC_maxHp;
    battlingGear2.mB4_hp = battlingGear2.mBC_maxHp;
}

void initBattlingTutorial(void) {
    battlingSettings.m5_p1Com = 0;
    battlingSettings.m6_p2Com = 0;
    setBattlingSceneMode(7);
    battlingGameMode = 5;
    battlingRubberBandMode = 0;
#if VERSION_JP
    setupWindowSize2(&battlingTutorialMenuWindow, 0x140, 0x70, 0xCA, 0x2A, 0x12, 8);
#else
    setupWindowSize2(&battlingTutorialMenuWindow, 0x140, 0x70, 0xA2, 0x2A, 0x1C, 8);
#endif
    addDialogWindowsToOTSub2(&battlingTutorialMenuWindow, getDialogParamPointer((u8*)battlingDialogTexts, 0x42));
    battlingTutorialMenuWindow.m68_charsPerStep = 0x1E;
    D_800925DC = 0;
    startBattlingScript(battlingEmptyScript);
    battlingTutorialChoice = 0;
    battlingScriptCameraMode = 0;
    battlingEventState = 0;
    startBattlingTutorialLesson(9);
}

void updateBattlingTutorial(void) {
    DialogWindow18* window;

    if (battlingTutorialCursorShown != 0 && (battlingFrameCounter & 4)) {
        drawBattlingTutorialCursor(battlingCurrentOt);
    }
    mergeQueuedInputs();
    window = &battlingMessageWindow;
    if (battlingTutorialMenuActive != 0) {
        if (newPadButtonForField & 0x1000) {
            playBattlingSfx(0x1E);
            battlingTutorialChoice--;
        }
        if (newPadButtonForField & 0x4000) {
            playBattlingSfx(0x1E);
            battlingTutorialChoice++;
        }
        if (battlingTutorialChoice >= 8) {
            battlingTutorialChoice = 0;
        }
        if (battlingTutorialChoice < 0) {
            battlingTutorialChoice = 7;
        }
        setupMultiChoiceColor(&battlingTutorialMenuWindow, (battlingFrameCounter * 7) & 0x3F, 0xC0, 0x10);
        /* retail passes the line sign-extended (no u8 prototype in scope) */
        ((void (*)(DialogWindow18*, s32))setSelectedLineForMultiChoice)(&battlingTutorialMenuWindow, battlingTutorialChoice);
        if (newPadButtonForDialogs & 0x20) {
            playBattlingSfx(0x21);
            startBattlingTutorialLesson(battlingTutorialChoice + 1);
            dealocateDialogWindow18MemorySub(window);
        }
    }
    if (*battlingScriptPtr == 0) {
        startBattlingTutorialLesson(0);
    }
    runBattlingScript();
    battlingTutorialCursorX += getBattlingApproachStep(battlingTutorialCursorTargetX, battlingTutorialCursorX, 4);
    battlingTutorialCursorY += getBattlingApproachStep(battlingTutorialCursorTargetY, battlingTutorialCursorY, 4);
    if (battlingTutorialMenuActive != 0) {
        updateAndRenderTextForDialogWindow(&battlingTutorialMenuWindow, battlingCurrentOt, battlingBufferIndex);
    }
    if (battlingScriptTextId != battlingShownTextId) {
        window->m68_charsPerStep = 3;
        dealocateDialogWindow18MemorySub(window);
        addDialogWindowsToOTSub2(window, getDialogParamPointer((u8*)battlingDialogTexts, battlingScriptTextId));
        battlingShownTextId = battlingScriptTextId;
    }
    updateBattlingFight(&battlingGear1, &battlingGear2);
    updateBattlingScriptCamera(battlingScriptCameraMode);
}

void settleBattlingGearOnGround(BattlingGear* gear) {
    VECTOR* pos;
    s32 tries;
    s32 best;
    s32 bestDir;
    s32 i;
    s32 h;
    VECTOR v;

    pos = &gear->m0_position;
    tries = 0;
    do {
        best = pos->vy = getBattlingGroundHeight(pos, 1);
        for (i = 0; i < 8; i++) {
            v = *pos;
            v.vx += battlingNeighborDirections[i * 2] * 0xC0;
            v.vz += battlingNeighborDirections[i * 2 + 1] * 0xC0;
            h = getBattlingGroundHeight(&v, 1);
            if (h < best - 0x40) {
                bestDir = i;
                best = h;
            }
        }
        if (best >= pos->vy - 0x40) {
            pos->vy = getBattlingGroundHeight(pos, 1);
            break;
        }
        pos->vx -= battlingNeighborDirections[bestDir * 2] * 0xC0;
        pos->vz -= battlingNeighborDirections[bestDir * 2 + 1] * 0xC0;
        tries++;
    } while (tries < 20);
    gear->mB0_groundY = getBattlingGroundHeight(gear, 1);
    gear->mD0_status.bits.b29_ground = getBattlingMapCell(gear) >> 24;
}

s32 runBattlingScriptSpecialOp(s32 arg0) {
    switch (arg0) {
    case 0:
        if (battlingMusic != NULL) /* none when started from the kernel menu: the PS1 writes low RAM */
        startMusicInstanceSub0(battlingMusic);
        stopAllSounds();
        break;
    case 1:
        settleBattlingGearOnGround(&battlingGear1);
        if (R3000_DIV(battlingGear1.mB4_hp << 8, battlingGear1.mBC_maxHp) > R3000_DIV(battlingGear2.mB4_hp << 8, battlingGear2.mBC_maxHp)) {
            battlingScriptTextId = 0x43;
        } else {
            battlingScriptTextId = 0x44;
        }
        break;
    case 2:
        settleBattlingGearOnGround(&battlingGear2);
        break;
    case 3:
        battlingCameraPos.vy = -battlingGearDistance;
        if (battlingCameraPos.vy < -0x600) {
            battlingCameraPos.vy = -0x600;
        }
        break;
    }
    FALLOFF_RETURN(0); /* result unused */
}

void resetBattlingEvent(void) {
    battlingEventStarted = 0;
}

void startBattlingEvent(void) {
    if (battlingEventStarted == 0) {
        startBattlingScript(battlingEventScript);
        battlingEventStarted = 1;
        battlingFightActive = 0;
        battlingSettings.m5_p1Com = 0;
        battlingSettings.m6_p2Com = 0;
        battlingEventState = 0;
        battlingScriptTextId = 0;
        battlingShownTextId = 0;
        battlingEventSparkPoints[0].pad = 1;
        battlingEventSparkPoints[1].pad = 1;
        battlingEventSparkPoints[2].pad = 1;
        battlingGear2.mE8_stun = 0;
        battlingGear1.mE8_stun = 0;
    }
}

void updateBattlingEvent(void) {
    BattlingGear* gear;
    DialogWindow18* window;
    s32 i;
    BattlingNode* part;
    VECTOR pos;

    if (battlingEventStarted == 0) {
        return;
    }
    gear = &battlingGear1;
    window = &battlingMessageWindow;
    switch (battlingEventState) {
    case 11:
        playBattlingSfx(0x2A);
        playBattlingSfx(0x2B);
        playBattlingSfx(0x2C);
        battlingEventState = 1;
    case 1:
        for (i = 0; i < 3; i++) {
            if (--battlingEventSparkPoints[i].pad == -1) {
                part = NODE_SKELETON(battlingGear1.m5C_model)->m4_bones[rand() % NODE_SKELETON(battlingGear1.m5C_model)->m0_boneCount];
                battlingEventSparkPoints[i].vx = part->m4C_localMatrix.t[0] + battlingGear1.m0_position.vx;
                battlingEventSparkPoints[i].vy = part->m4C_localMatrix.t[1] + battlingGear1.m0_position.vy;
                battlingEventSparkPoints[i].vz = part->m4C_localMatrix.t[2] + battlingGear1.m0_position.vz;
                battlingEventSparkPoints[i].pad = rand() % 16 + 8;
            }
            if (battlingEventSparkPoints[i].pad & 1) {
                spawnBattlingHitEffect(&battlingEventSparkPoints[i], 9);
            }
        }
        break;
    case 2:
        gear->mB4_hp = 1;
        battlingEventFade = 0xFF;
        battlingEventState++;
    case 3:
        drawBattlingFadeTile(battlingCurrentOt, battlingEventFade, 0);
        if (battlingEventFade < 0) {
            battlingEventFade = 0;
        }
        battlingEventFade -= 8;
    burn:
        gear->m4F_animSpeed = 0x10;
        gear->m4C_animation = 0xA;
        gear->m52_moveAnimSpeed = 0;
        gear->mB6_gauge = 0x1000;
        gear->mD0_status.bits.b10_burning = 1;
        if (rand() & 1) {
            i = NODE_SKELETON(battlingGear1.m5C_model)->m0_boneCount;
            part = NODE_SKELETON(battlingGear1.m5C_model)->m4_bones[rand() % i];
            pos.vx = part->m4C_localMatrix.t[0] + battlingGear1.m0_position.vx;
            pos.vy = part->m4C_localMatrix.t[1] + battlingGear1.m0_position.vy;
            pos.vz = part->m4C_localMatrix.t[2] + battlingGear1.m0_position.vz;
            spawnBattlingHitEffect(&pos, 0xB);
            spawnBattlingHitEffect(&pos, 8);
        }
        break;
    case 4:
        battlingEventFade += 3;
        if (battlingEventFade >= 0x100) {
            battlingEventFade = 0xFF;
            battlingResult = 0x7F;
            clearBattlingVram(0);
            exitBattling();
        }
        drawBattlingFadeTile(battlingCurrentOt, battlingEventFade, 1);
        goto burn;
    }
    runBattlingScript();
    if (battlingScriptTextId != battlingShownTextId) {
        window->m68_charsPerStep = 1;
        dealocateDialogWindow18MemorySubSub(window);
        addDialogWindowsToOTSub2(window, getDialogParamPointer((u8*)battlingDialogTexts, battlingScriptTextId));
        window->mC_height = 2;
        window->m6_y = 0xB4;
        battlingShownTextId = battlingScriptTextId;
    }
    updateBattlingScriptCamera(battlingScriptCameraMode);
    battlingGear1.m48_speed = 0;
}

void func_800725A8(void) {
}
