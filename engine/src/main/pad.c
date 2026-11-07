/*
 * Controller input (Noah: inputs): pad reading and remaps, the vsync input
 * buffer and replay ring, play-time clock, vibration, pad init, and debug
 * dumps of VRAM to TIM/PPM files on the PC host.
 *
 * File 0x8003569C-0x800366E0 (main executable). Built with GCC 2.7.2
 * (PSY-Q) and -G0 (its short strings sit in .rodata), ASPSX < 2.50.
 * vsyncCallback (vsync callback) polls the SN debugger with POLLHOST().
 */
#include "common.h"
#include "psyq.h"
#include "main/debug_font.h"
#include "pollhost.h"

typedef struct {
    /* 0x0 */ u8 m0_active;
    /* 0x1 */ u8 m1;
    /* 0x2 */ u8 m2_motorOn;
    /* 0x3 */ u8 m3;
    /* 0x4 */ s16 m4_duration;
    /* 0x6 */ u8 m6_state;
    /* 0x7 */ u8 m7_disabled;
} PadActuator;

typedef struct {
    /* 0x00 */ u8 m0_status;  /* 0: data valid, 0xFF: no controller */
    /* 0x01 */ u8 m1_type;    /* terminal type << 4 | data size / 2 */
    /* 0x02 */ u8 m2_data[0x20];
} PadRecv; /* 0x22 */

/* file-local in retail: .sbss (declared after use, so never $gp) */
static u8 g_timeVSyncCount;       /* frames */
static s32 previousDownButtons;      /* pad 0 previous buttons */
static s32 previousDownButtons2;      /* pad 1 previous buttons */
static u32 inputReplayPosition;      /* queued frames */
static s32 inputReplayPosition2;      /* write position */
static s32 inputReplayOffset;      /* read position */
static u8 lastPadType;       /* type of the last pad read */
static u8 D_8005938C;
static s32 debuggerPollEnabled;
/* .bss: input replay ring buffer (16 frames) */
static u16 inputReplayScriptButtons[16];  /* pad 0 buttons */
static u16 inputReplayScriptButtons2[16];  /* pad 1 buttons */
static u16 inputReplayDialogButtons[16];  /* pad 0 new presses */
static u16 inputReplayDialogButtons2[16];  /* pad 1 new presses */
static u16 inputReplayFieldButtons[16];  /* pad 0 auto-repeat */
static u16 inputReplayFieldButtons2[16];  /* pad 1 auto-repeat */
static PadActuator padActuators[2];
extern PadRecv padReceiveBuffers[2];   /* pad receive buffers (InitPAD) */
extern u16 newPadButtonForScripts;      /* pad 0 buttons */
extern u16 newPadButtonForScripts2;      /* pad 1 buttons */
extern u16 newPadButtonForDialogs;      /* pad 0 newly pressed */
extern u16 newPadButtonForDialogs2;      /* pad 1 newly pressed */
extern u16 newPadButtonForField;      /* pad 0 pressed with auto-repeat */
extern u16 newPadButtonForField2;      /* pad 1 pressed with auto-repeat */
extern s32 g_playTimeInVsync;      /* vsync counter */
extern u8 pad0LeftStickX;
extern u8 pad1LeftStickX;
extern u8 pad0LeftStickY;
extern u8 pad1LeftStickY;
extern u8 pad0RightStickX;
extern u8 pad1RightStickX;
extern u8 pad0RightStickY;
extern u8 pad1RightStickY;
extern u16 remappedPadPressed;
extern u16 remappedPadPressed2;
extern u16 remappedPadRepeat;
extern u16 remappedPadRepeat2;
extern u16 remappedPadButtons;
extern u16 remappedPadButtons2;
extern u8 g_playTimeSeconds;       /* seconds */
extern u8 g_playTimeMinutes;       /* minutes */
extern u8 g_playTimeHours;       /* hours */
s32 PCclose(s32 fd);                      /* PCclose */
void SendPAD(void* a, s32 b, void* c, s32 d);
void InitPAD(void* buf1, s32 len1, void* buf2, s32 len2);
void ChangeClearPAD(s32);
extern s32 runningOnDTL;

/* .data (0x800501E8-0x80050240; JP 0x8004F88C-0x8004F8E0) */
/* Button bit per remap slot: circle, cross, triangle, square, L1, L2, R1, R2. */
u16 remapSlotButtonBits[8] = { 0x20, 0x40, 0x10, 0x80, 0x04, 0x01, 0x08, 0x02 };
u8 g_playTimeOverflow = 0;          /* play time stopped (99:59:59 reached) */
void (*vsyncUserCallback)(void) = NULL;    /* user vsync callback */
s32 D_80050200 = 1;
s32 D_80050204 = 0;
s32 inputOverflowed = 0;            /* input replay overflow flag */
/* Stick X / Y (0x80 = centre) per d-pad state (buttons >> 12). */
u8 dpadToStickX[16] = {
    0x80, 0x80, 0xFF, 0xFF, 0x80, 0x80, 0xFF, 0x80, 0x00, 0x00, 0x80, 0x80, 0x00, 0x80, 0x80, 0x80,
};
u8 dpadToStickY[16] = {
    0x80, 0x00, 0x80, 0x00, 0xFF, 0x80, 0xFF, 0x80, 0x80, 0x00, 0x80, 0x80, 0xFF, 0x80, 0x80, 0x80,
};
s32 pad0RepeatTimer = 0;                 /* pad 0 hold counter */
s32 pad1RepeatTimer = 0;                 /* pad 1 hold counter */
#if !VERSION_JP
s32 D_80050234 = 0;                 /* US only, unreferenced */
#endif
/* Remap slot -> button slot (identity by default; dialog.c reads it too). */
u8 dpadButtonMapping[8] = { 0, 1, 2, 3, 4, 5, 6, 7 };

s32 readPadButtons(s32 port) {
    PadRecv* p;

    p = &padReceiveBuffers[port];
    lastPadType = 0;
    if (p->m0_status != 0) {
        return 0;
    }
    lastPadType = p->m1_type & 0xF0;
    if (lastPadType == 0x40 || lastPadType == 0x50 || lastPadType == 0x70) {
        return (u8)~p->m2_data[1] | ((u8)~p->m2_data[0] << 8);
    }
    return 0;
}

s32 isControllerConnected(s32 port) {
    if (padReceiveBuffers[port].m0_status == 0xFF) {
        return 0;
    }
    switch (padReceiveBuffers[port].m1_type & 0xF0) {
    case 0x10:
        return 2;
    case 0x40:
        return 1;
    case 0x50:
        return 3;
    case 0x70:
        return 4;
    }
    return -1;
}

s16 remapPadButtons(s32 arg) {
    u16 buttons = arg;
    s32 out;
    s32 i;

    out = buttons & 0xFF00;
    for (i = 0; i < 8; i++) {
        if (remapSlotButtonBits[i] & buttons) {
            out |= remapSlotButtonBits[dpadButtonMapping[i]];
        }
    }
    return out;
}

/* JP only: remap the current pad state into the remappedPadPressed.. copies. */
#if VERSION_JP
void func_80035650(void) {
    remappedPadButtons = remapPadButtons((s16)newPadButtonForScripts);
    remappedPadButtons2 = remapPadButtons((s16)newPadButtonForScripts2);
    remappedPadPressed = remapPadButtons((s16)newPadButtonForDialogs);
    remappedPadPressed2 = remapPadButtons((s16)newPadButtonForDialogs2);
    remappedPadRepeat = remapPadButtons((s16)newPadButtonForField);
    remappedPadRepeat2 = remapPadButtons((s16)newPadButtonForField2);
}
#endif

s16 remapAnalogJoystickButtons(s32 buttons) {
    u16 out;

    out = buttons & ~0x9E;
    if (buttons & 8) {
        out |= 0x10;
    }
    if (buttons & 2) {
        out |= 4;
    }
    if (buttons & 0x10) {
        out |= 8;
    }
    if (buttons & 0x80) {
        out |= 2;
    }
    if (buttons & 4) {
        out |= 0x80;
    }
    return out;
}

u8 getDpadStickX(s32 buttons) {
    return dpadToStickX[(buttons >> 12) & 0xF];
}

u8 getDpadStickY(s32 buttons) {
    return dpadToStickY[(buttons >> 12) & 0xF];
}

void getInputDuringVsync(void) {
    u16 x;
    newPadButtonForScripts = readPadButtons(0);
#if !VERSION_JP
    newPadButtonForScripts = remapPadButtons((s16)newPadButtonForScripts);
#endif
    if (lastPadType != 0) {
        if (lastPadType == 0x50) {
            newPadButtonForScripts = remapAnalogJoystickButtons((s16)newPadButtonForScripts);
            goto analog0;
        }
        if (lastPadType == 0x70) {
        analog0:
            pad0RightStickX = padReceiveBuffers[0].m2_data[2];
            pad0RightStickY = padReceiveBuffers[0].m2_data[3];
            pad0LeftStickX = padReceiveBuffers[0].m2_data[4];
            pad0LeftStickY = padReceiveBuffers[0].m2_data[5];
        } else {
            pad0RightStickY = 0;
            pad0RightStickX = 0;
            pad0LeftStickX = dpadToStickX[newPadButtonForScripts >> 12];
            pad0LeftStickY = dpadToStickY[newPadButtonForScripts >> 12];
        }
    } else {
        pad0RightStickY = 0;
        pad0RightStickX = 0;
        pad0LeftStickY = 0;
        pad0LeftStickX = 0;
    }
    x = newPadButtonForScripts ^ previousDownButtons;
    newPadButtonForDialogs = x & newPadButtonForScripts;
    previousDownButtons = newPadButtonForScripts;
    if (newPadButtonForDialogs != 0) {
        pad0RepeatTimer = 0;
    }
    newPadButtonForField = newPadButtonForScripts;
    if (pad0RepeatTimer < 0x20) {
        pad0RepeatTimer++;
        newPadButtonForField = newPadButtonForDialogs;
    } else if (g_playTimeInVsync & 3) {
        newPadButtonForField = newPadButtonForDialogs;
    }

    newPadButtonForScripts2 = readPadButtons(1);
#if !VERSION_JP
    newPadButtonForScripts2 = remapPadButtons((s16)newPadButtonForScripts2);
#endif
    if (lastPadType != 0) {
        if (lastPadType == 0x50) {
            newPadButtonForScripts2 = remapAnalogJoystickButtons((s16)newPadButtonForScripts2);
            goto analog1;
        }
        if (lastPadType == 0x70) {
        analog1:
            pad1RightStickX = padReceiveBuffers[1].m2_data[2];
            pad1RightStickY = padReceiveBuffers[1].m2_data[3];
            pad1LeftStickX = padReceiveBuffers[1].m2_data[4];
            pad1LeftStickY = padReceiveBuffers[1].m2_data[5];
        } else {
            pad1RightStickY = 0;
            pad1RightStickX = 0;
            pad1LeftStickX = dpadToStickX[newPadButtonForScripts2 >> 12];
            pad1LeftStickY = dpadToStickY[newPadButtonForScripts2 >> 12];
        }
    } else {
        pad1RightStickY = 0;
        pad1RightStickX = 0;
        pad1LeftStickY = 0;
        pad1LeftStickX = 0;
    }
    x = newPadButtonForScripts2 ^ previousDownButtons2;
    newPadButtonForDialogs2 = x & newPadButtonForScripts2;
    previousDownButtons2 = newPadButtonForScripts2;
    if (newPadButtonForDialogs2 != 0) {
        pad1RepeatTimer = 0;
    }
    newPadButtonForField2 = newPadButtonForScripts2;
    if (pad1RepeatTimer < 0x20) {
        pad1RepeatTimer++;
        newPadButtonForField2 = newPadButtonForDialogs2;
    } else if (g_playTimeInVsync & 3) {
        newPadButtonForField2 = newPadButtonForDialogs2;
    }
#if VERSION_JP
    func_80035650();
#endif
}

void saveInputs(void) {
    s32 idx;

    if (inputReplayPosition < 0x10) {
        idx = inputReplayPosition2;
        inputReplayPosition++;
        inputReplayScriptButtons[idx & 0xF] = newPadButtonForScripts;
        inputReplayScriptButtons2[idx & 0xF] = newPadButtonForScripts2;
        inputReplayDialogButtons[idx & 0xF] = newPadButtonForDialogs;
        inputReplayDialogButtons2[idx & 0xF] = newPadButtonForDialogs2;
        inputReplayFieldButtons[idx & 0xF] = newPadButtonForField;
        inputReplayFieldButtons2[idx & 0xF] = newPadButtonForField2;
        inputReplayPosition2 = idx + 1;
    } else {
        inputOverflowed = 1;
    }
}

s32 loadInputFromVSyncBuffer(void) {
    s32 idx;
    s32 count;

    if (inputReplayPosition == 0) {
        return 0;
    }
    idx = inputReplayOffset & 0xF;
    newPadButtonForScripts = inputReplayScriptButtons[idx];
    newPadButtonForScripts2 = inputReplayScriptButtons2[idx];
    newPadButtonForDialogs = inputReplayDialogButtons[idx];
    newPadButtonForDialogs2 = inputReplayDialogButtons2[idx];
    newPadButtonForField = inputReplayFieldButtons[idx];
    newPadButtonForField2 = inputReplayFieldButtons2[idx];
#if VERSION_JP
    func_80035650();
#endif
    inputReplayOffset++;
    count = inputReplayPosition;
    inputReplayPosition--;
    return count;
}

s32 getInputReplayCount(void) {
    return inputReplayPosition;
}

/* Enhanced (xg-hbc.19.3): the 60 Hz field's frames between read the vblanks'
 * samples without taking them (field/f_80072140.c updateFieldInputs): the
 * native frame's tick takes them all, as the Original's frame does. */
static s32 pcPadKeptPosition, pcPadKeptPosition2, pcPadKeptOffset;
void pcPadVSyncBufferKeep(void) {
    pcPadKeptPosition = inputReplayPosition;
    pcPadKeptPosition2 = inputReplayPosition2;
    pcPadKeptOffset = inputReplayOffset;
}
void pcPadVSyncBufferPutBack(void) {
    inputReplayPosition = pcPadKeptPosition;
    inputReplayPosition2 = pcPadKeptPosition2;
    inputReplayOffset = pcPadKeptOffset;
}

void resetInputs(void) {
    inputReplayPosition = 0;
    inputReplayPosition2 = 0;
    inputReplayOffset = 0;
    inputOverflowed = 0;
    D_80050200 = 1;
    remappedPadRepeat2 = 0;
    remappedPadRepeat = 0;
    remappedPadPressed2 = 0;
    remappedPadPressed = 0;
    remappedPadButtons2 = 0;
    remappedPadButtons = 0;
    newPadButtonForField2 = 0;
    newPadButtonForField = 0;
    newPadButtonForDialogs2 = 0;
    newPadButtonForDialogs = 0;
    newPadButtonForScripts2 = 0;
    newPadButtonForScripts = 0;
}

void incrementPlayTime(void) {
    if (g_playTimeOverflow == 0) {
        if (++g_timeVSyncCount == 60) {
            g_timeVSyncCount = 0;
            g_playTimeSeconds++;
        }
        if (g_playTimeSeconds == 60) {
            g_playTimeSeconds = 0;
            g_playTimeMinutes++;
        }
        if (g_playTimeMinutes == 60) {
            g_playTimeMinutes = 0;
            g_playTimeHours++;
        }
        if (g_playTimeHours == 100) {
            g_playTimeOverflow = 1;
        }
    }
}

/* Dump a VRAM rectangle to a 16-bit TIM file on the PC host. */
void dumpVramToTimFile(RECT* rect, char* name) {
    struct {
        s32 id;
        s32 flag;
        s32 bnum;
        s16 x;
        s16 y;
        s16 w;
        s16 h;
    } tim;
    s32 fd;

    PORT_TODO("VRAM dump through the dev kit's fixed RAM buffers");
    return;
    StoreImage(rect, (void*)0x80700000);
    tim.id = 0x10;
    tim.flag = 2;
    tim.bnum = rect->w * rect->h * 2 + 12;
    tim.x = rect->x;
    tim.y = rect->y;
    tim.w = rect->w;
    tim.h = rect->h;
    fd = PCcreat(name, 0);
    PCwrite(fd, &tim, 0x14);
    PCwrite(fd, (void*)0x80700000, rect->w * rect->h * 2);
    PCclose(fd);
}

/* Dump a VRAM rectangle to a PPM (P6) file on the PC host. */
s32 dumpVramToPpmFile(RECT* rect, char* name) {
    char header[0x100];
    u8* src;
    u8* dst;
    s32 count;
    s32 i;
    s32 fd;

    PORT_TODO("VRAM dump through the dev kit's fixed RAM buffers");
    return -1;
    StoreImage(rect, (void*)0x80600000);
    DrawSync(0);
    sprintf(header, "P6\r%d %d\r255\r", rect->w, rect->h);
    count = rect->w * rect->h;
    src = (u8*)0x80600000;
    dst = (u8*)0x80700000;
    i = count;
    while (i--) {
        *dst++ = (*src & 0x1F) << 3;
        *dst++ = (*(u16*)src >> 2) & 0xF8;
        *dst++ = (*(u16*)src >> 7) & 0xF8;
        src += 2;
    }
    fd = PCcreat(name, 0);
    if (fd == -1) {
        return -1;
    }
    PCwrite(fd, header, (u32)strlen(header));
    PCwrite(fd, (void*)0x80700000, count * 3);
    PCclose(fd);
    return 0;
}

void initPadVibration(void) {
    padActuators[0].m0_active = 0;
    padActuators[0].m4_duration = 0;
    padActuators[0].m6_state = 0;
    padActuators[0].m7_disabled = 0;
    padActuators[1] = padActuators[0];
    SendPAD(&padActuators[0], 4, &padActuators[1], 4);
}

void updatePadActuator(PadActuator* act) {
    if (act->m7_disabled == 0) {
        if (act->m4_duration != 0) {
            act->m0_active = 1;
            act->m1 = 0x40;
            act->m2_motorOn = 1;
            act->m3 = 0;
            act->m6_state = 1;
            act->m4_duration--;
        } else if (act->m6_state == 1) {
            act->m0_active = 1;
            act->m1 = 0x40;
            act->m2_motorOn = 0;
            act->m3 = 0;
            act->m6_state = 2;
        } else if (act->m6_state == 2) {
            act->m0_active = 0;
            act->m6_state = 0;
        }
    }
}

void updatePadVibration(void) {
    updatePadActuator(&padActuators[0]);
    updatePadActuator(&padActuators[1]);
}

void startPadVibration(s32 port, s16 duration) {
    padActuators[port].m4_duration = duration;
}

void setPadVibrationDisabled(s32 port, u8 disabled) {
    padActuators[port].m7_disabled = disabled;
}

void initPadSystem(void) {
    s32 i;
    u8* p;

    InitPAD(&padReceiveBuffers[0], 0x22, &padReceiveBuffers[1], 0x22);
    StartPAD();
    ChangeClearPAD(0);
    resetInputs();
    initPadVibration();
    D_80050204 = 0;
    D_8005938C = 1;
    debuggerPollEnabled = 0;
    i = 7;
    p = &dpadButtonMapping[7];
    do {
        *p = i;
        p--;
    } while (--i >= 0);
#if !VERSION_JP
    dpadButtonMapping[0] = 1;
    dpadButtonMapping[2] = 3;
    dpadButtonMapping[1] = 0;
    dpadButtonMapping[3] = 2;
#endif
}

void func_8003633C(u8 x) {
    D_8005938C = x;
}

/* Vsync callback: input, replay and play time, then the debugger poll. */
void vsyncCallback(void) {
    u8 unused[0x28]; /* retail's frame has 0x28 unused bytes */

    g_playTimeInVsync++;
    getInputDuringVsync();
    saveInputs();
    incrementPlayTime();
    updatePadVibration();
    if (vsyncUserCallback != NULL) {
        vsyncUserCallback();
    }
    if (runningOnDTL != -1 && debuggerPollEnabled != 0) {
        POLLHOST();
    }
}

void setDebuggerPollEnabled(s32 x) {
    debuggerPollEnabled = x;
}

void setVsyncUserCallback(void (*callback)(void)) {
    vsyncUserCallback = callback;
}

void func_80036400(s32 x) {
    D_80050200 = x;
}

s32 getInputOverflowed(void) {
    return inputOverflowed;
}

void mergeQueuedInputs(void) {
    u16 b0;
    u16 b1;
    u16 p0;
    u16 p1;
    u16 r0;
    u16 r1;

    r1 = 0;
    r0 = 0;
    p1 = 0;
    p0 = 0;
    b1 = 0;
    b0 = 0;
    if (getInputOverflowed()) {
        resetInputs();
    } else {
        while (loadInputFromVSyncBuffer()) {
            b0 |= newPadButtonForScripts;
            b1 |= newPadButtonForScripts2;
            p0 |= newPadButtonForDialogs;
            p1 |= newPadButtonForDialogs2;
            r0 |= newPadButtonForField;
            r1 |= newPadButtonForField2;
        }
    }
    newPadButtonForScripts = b0;
    newPadButtonForScripts2 = b1;
    newPadButtonForDialogs = p0;
    newPadButtonForDialogs2 = p1;
    newPadButtonForField = r0;
    newPadButtonForField2 = r1;
}

void printPadRawData(u8* buf) {
    s32 i;
    s32 len;

    len = (buf[1] & 0xF) * 2 + 2;
    for (i = 0; i < len; i++) {
        logToScreen("%02x ", *(buf + i));
    }
    logToScreen("\n");
    if (buf[0] == 0 && (buf[1] & 0xF0) == 0x40) {
        logToScreen("%04x\n", (u8)~buf[3] | ((u8)~buf[2] << 8));
    }
}

void printPadDebugInfo(void) {
    printPadRawData((u8*)&padReceiveBuffers[0]);
    printPadRawData((u8*)&padReceiveBuffers[1]);
    logToScreen("vect0 %02x %02x\n", pad0LeftStickX, pad0LeftStickY);
    logToScreen("vect1 %02x %02x\n", pad1LeftStickX, pad1LeftStickY);
    logToScreen("PADD %04x %04x\n", newPadButtonForScripts, newPadButtonForScripts2);
    while (loadInputFromVSyncBuffer()) {
        logToScreen("%04x %04x %04x %04x\n", readPadButtons(0), newPadButtonForScripts, newPadButtonForDialogs, newPadButtonForField);
    }
}

s32 pcPadHeldRemappedPort(s32 port);
/* Pad 0's buttons held now, remapped as getInputDuringVsync remaps them
 * (0x20: confirm), for the dialogue settings' hold to show text
 * (src/main/dialog.c). Reads the receive buffer only: readPadButtons would
 * also set lastPadType, and the field clears newPadButtonForScripts once it
 * has taken the frame's input (resetInputs). */
s32 pcPadHeldRemapped(void) {
    return pcPadHeldRemappedPort(0);
}

/* The same for either pad (1: the Battle arena's player 2,
 * port/src/game/battle/versus.c). */
s32 pcPadHeldRemappedPort(s32 port) {
    PadRecv* p = &padReceiveBuffers[port];
    s32 type = p->m1_type & 0xF0;
    s32 buttons;

    if (p->m0_status != 0 || (type != 0x40 && type != 0x50 && type != 0x70)) {
        return 0;
    }
    buttons = (u8)~p->m2_data[1] | ((u8)~p->m2_data[0] << 8);
#if !VERSION_JP
    buttons = (u16)remapPadButtons((s16)buttons);
#endif
    if (type == 0x50) {
        buttons = (u16)remapAnalogJoystickButtons((s16)buttons);
    }
    return buttons;
}
