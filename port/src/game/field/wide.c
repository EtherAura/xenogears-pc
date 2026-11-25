/*
 * Fields in the wide picture (include/pc_wide.h; PC only).
 *
 * - Models: the field draws each room model whose bounding sphere passes
 *   isObjectOcluded (src/field/f_800AA9DC.c), a test against the 4:3 screen.
 *   A model it drops is drawn into the wide picture only when the widened
 *   test (wideSphereOff, without libgte's safe points) keeps it
 *   (pcFieldWideObject), with the GTE and the model renderer's globals put
 *   back as the PS1 leaves them, so VRAM, the game and its timing do not
 *   change.
 * - NPCs: the off-screen flag 0x200 (src/field/f_80072140.c), which freezes an
 *   NPC's animation, scripts and dialogs outside x -39..359, is widened by the
 *   visible margin (the user's choice; it changes the game from the PS1's).
 *   XG_WIDE_NPC=ps1 keeps the PS1's test, so a wide run's VRAM frames can be
 *   compared with a 4:3 run's (tools/widecheck.py).
 */
#include "common.h"
#include "psyq.h"
#include "gte.h"
#include "field/field.h"
#include "pc_camera.h"
#include "pc_speedup.h"

/* port/src/system/options.c (game code has no libc) */
const char* portOption(const char* name);
int portSceneReplayActive(void);
/* port/src/system/fieldspeed.c: a field movie plays (field speed holds 1x) */
void fieldSpeedMovie(int on);

extern s32 g_frameOddOrEven;
extern u8* currentModelInstanceDrawPrims;
extern OTag* currentOTEntry;
extern u8* g_currentModelBlockSubBlocks;
extern u8* currentModeBlock18;
extern SVECTOR* currentModelBlockNormals;
extern SVECTOR* currentModelBlockVertices;
extern s32 fieldPolyCount;
extern s32 fieldPolyCount2;
extern MATRIX occlusionTestMatrix;
extern s32 OPX8E_param0;
extern s32 OPX8E_param1;

s32 submitModelForRendering(ModelBlock* b, u8* prims, OTag* ot, s32 renderMode);

/* isObjectOcluded's bounding-sphere test (src/field/f_800AA9DC.c), widened
 * by the cull margins: 0 when the sphere's square reaches the wide picture,
 * -1 when it is off it. The game's test calls libgte, and every libgte call
 * is a safe point that moves the stepped clock (XG_CLOCK=step,
 * port/src/system/hw.c) on by a microsecond; a test only the wide picture
 * makes would change when interrupts arrive, and so the game (the field's
 * random battle came out different at 16:9). This is the same GTE work in
 * the same order with the GTE macros, which have no safe point: RotTrans
 * (RT * V0 + TR), the sphere's centre as the translation, two RotTransPers
 * of its corners. It writes occlusionTestMatrix as the game's test does;
 * the caller puts that and the GTE back. */
static s32 wideSphereOff(sFieldEntitySub0* p) {
    VECTOR v;
    SVECTOR sv;
    s32 sxy;
    s32 r = p->m20_boundingRadius;
    s32 x0;
    s32 y0;
    s32 x1;
    s32 y1;

    gte_ldv0(&p->m18_boundingCenter);
    gte_rt();
    gte_stlvnl(&v);
    occlusionTestMatrix.t[0] = v.vx;
    occlusionTestMatrix.t[1] = v.vy;
    occlusionTestMatrix.t[2] = v.vz;
    gte_SetRotMatrix(&occlusionTestMatrix);
    gte_SetTransMatrix(&occlusionTestMatrix);
    sv.vx = -r;
    sv.vy = -r;
    sv.vz = 0;
    gte_ldv0(&sv);
    gte_rtps();
    gte_stsxy(&sxy);
    y0 = sxy >> 16;
    x0 = (s16)sxy;
    sv.vx = r;
    sv.vy = r;
    sv.vz = 0;
    gte_ldv0(&sv);
    gte_rtps();
    gte_stsxy(&sxy);
    y1 = sxy >> 16;
    x1 = (s16)sxy;
    if (y0 < OPX8E_param1 + 0xE0 + pcVerticalCull && -OPX8E_param1 - pcVerticalCull < y1 &&
        x0 < OPX8E_param0 + 0x140 + pcWideCull && -OPX8E_param0 - pcWideCull < x1) {
        return 0;
    }
    return -1;
}

/* A model isObjectOcluded dropped (the GTE holds what the PS1 has after it):
 * drawn into the wide picture only, if the widened test keeps it. */
void pcFieldWideObject(sFieldEntitySub0* sub, MATRIX* proj, OTag* ot) {
    GteState gte;
    MATRIX sphere;
    u8* drawPrims;
    OTag* otEntry;
    u8* subBlocks;
    u8* block18;
    SVECTOR* normals;
    SVECTOR* verts;
    s32 polys;
    s32 polys2;

    if (pcWideCull == 0 && pcVerticalCull == 0) {
        return;
    }
    gteSave(&gte);
    sphere = occlusionTestMatrix;
    drawPrims = currentModelInstanceDrawPrims;
    otEntry = currentOTEntry;
    subBlocks = g_currentModelBlockSubBlocks;
    block18 = currentModeBlock18;
    normals = currentModelBlockNormals;
    verts = currentModelBlockVertices;
    polys = fieldPolyCount;
    polys2 = fieldPolyCount2;
    /* The test projects the sphere with the model's matrix, as the PS1's
     * call did. */
    gte_SetRotMatrix(proj);
    gte_SetTransMatrix(proj);
    if (wideSphereOff(sub) == 0) {
        gte_SetRotMatrix(proj);
        gte_SetTransMatrix(proj);
        pcWideOnlyScope = 1;
        submitModelForRendering(sub->m4_pModelBlock, sub->m8_primBuffers[g_frameOddOrEven], ot, sub->m12_renderMode);
        pcWideOnlyScope = 0;
    }
    currentModelInstanceDrawPrims = drawPrims;
    currentOTEntry = otEntry;
    g_currentModelBlockSubBlocks = subBlocks;
    currentModeBlock18 = block18;
    currentModelBlockNormals = normals;
    currentModelBlockVertices = verts;
    fieldPolyCount = polys;
    fieldPolyCount2 = polys2;
    occlusionTestMatrix = sphere;
    gteLoad(&gte);
}

/* Gallery replays keep the native visibility test: this flag also controls
 * movement and script updates. Widening it advances actors that the scene
 * expects to remain paused off screen (field 35, story 37, actor 23).
 * Ordinary gameplay retains the widened NPC policy. */
s32 pcFieldNpcMargin(void) {
    static s32 checked;
    static s32 ps1;

    if (portSceneReplayActive()) {
        return 0;
    }
    if (!checked) {
        const char* e = portOption("XG_WIDE_NPC");
        checked = 1;
        ps1 = e != NULL && e[0] == 'p' && e[1] == 's' && e[2] == '1' && e[3] == '\0';
    }
    return ps1 ? 0 : pcWideView;
}

/* ---- what the field shows ------------------------------------------------- */

extern s32 fieldMapNumber;
extern s32 onlyUpdateDirector;

/* A field on the opt-out list (port/src/gpu/wide_optout.txt) stays 4:3; called
 * as each field's data is set up (initFieldData). */
void pcFieldWideScene(void) {
    pcFirstPersonEnd();
    pcWideBlock(PC_WIDE_BLOCK_OPTOUT, pcWideOptOut(PC_WIDE_FIELD, fieldMapNumber & 0xFFF) != NULL);
}

/* The field mode starts or ends: nothing it blocks applies (the effect block
 * of src/field/f_800A55B8.c lives in the field's data, reset with it). */
void pcFieldWideExit(void) {
    pcFirstPersonEnd();
    pcWideBlock(PC_WIDE_BLOCK_OPTOUT | PC_WIDE_BLOCK_MOVIE | PC_WIDE_BLOCK_EFFECT, 0);
    fieldSpeedMovie(0); /* no movie plays across the mode's start or end */
    pcTransitionRoom(0); /* nor a room change: an exit to the world map leaves the field without one */
}

/* A field movie (startFieldTransition). Its frames go to the frame buffers
 * (2D backgrounds, the dialogs-over-movie mode, 24-bit movies), except in
 * the mode where the field keeps running (onlyUpdateDirector 2) and the movie
 * goes where the script says: only when that is inside a frame buffer
 * ((0, 0) or (0, 0x100), 320 x 224) is there nothing to show wide. */
void pcFieldMovieBlock(s32 on) {
    s32 i;
    s32 x;
    s32 y;
    s32 w = fieldMovieParams.m10_modeAndSize[1];
    s32 h = fieldMovieParams.m10_modeAndSize[2];

    /* Field speed holds 1x for every movie, the ones below that leave the
     * picture wide too: told before `on` becomes the wide picture's. */
    if (on) pcFirstPersonEnd();
    fieldSpeedMovie(on);
    if (on && onlyUpdateDirector == 2) {
        on = 0;
        for (i = 0; i < 2; i++) {
            x = i == 0 ? fieldMovieParams.m2_buffer0X : fieldMovieParams.m6_buffer1X;
            y = i == 0 ? fieldMovieParams.m4_buffer0Y : fieldMovieParams.m8_buffer1Y;
            if (x < 0x140 && x + w > 0 && ((y < 0xE0 && y + h > 0) || (y < 0x1E0 && y + h > 0x100))) {
                on = 1;
            }
        }
    }
    pcWideBlock(PC_WIDE_BLOCK_MOVIE, on);
}

/* ---- past the room (Enhanced, xg-3hyf.1) ------------------------------------ */

extern FieldBss_800B2184 op9DVar1;

/* Rooms where the clear colour shows only past the room's geometry, never as
 * part of the scene: there it gives way to black in the wide picture. Opt-in:
 * in most rooms with a clear colour other than black it IS part of the scene
 * the 4:3 picture shows (Solaris's sky, clouds, the radar screen under a
 * translucent layer), and the margins rightly continue it. The survey
 * of every room whose script sets one (work/xg-3hyf-1/NOTES.md):
 *   405  Babel Tower's outer courtyard: pale lavender beside and below it. */
static const u16 fieldPastBlack[] = {405};

/* Whether the clear colour runs on past the room in the wide picture (a
 * colour other than black, in a room not on fieldPastBlack): then the room's
 * effects are not kept to the scene either (f_800A9274.c), as they show over
 * that colour in the 4:3 picture too. */
s32 pcFieldWideKeepsClear(void) {
    const u8* c = op9DVar1.m18_backgroundClearColor;
    u32 i;

    if ((c[0] | c[1] | c[2]) == 0) {
        return 0;
    }
    for (i = 0; i < sizeof(fieldPastBlack) / sizeof(fieldPastBlack[0]); i++) {
        if (fieldPastBlack[i] == (fieldMapNumber & 0xFFF)) {
            return 0;
        }
    }
    return 1;
}

/* The field clears each frame to the room's clear colour (OP 0xE7), and the
 * wide picture's clear reaches the margins and extra rows (port/src/gpu/
 * wide.c: a full-screen clear covers them). Where the room's geometry ends
 * inside the wide picture that flat colour stood beside it (Babel Tower's
 * courtyard, field 405: pale lavender). The user's choice:
 * past the room is black. So in the rooms on fieldPastBlack black is laid
 * there first, drawn only into the
 * wide picture (PC_WIDE_ONLY, never VRAM: the 4:3 picture keeps the clear
 * colour) and before the room (the head of the chain DrawOTag walks), as
 * primitives so that motion smoothing's replay lays it too; no part of the
 * scene (PC_WIDE_NOT_SCENE: effects are not kept over it). With expanded
 * framing the frame buffer's rows below the 216 the TV shows (screen.h; the
 * 4:3 picture never has them) are shown too: black goes under them as well.
 * ot: the chain's head, linked after the scene tables. */
void pcFieldWideCover(OTag* ot, const DRAWENV* draw, const DRAWENV* clear, const DISPENV* disp) {
    s32 top;
    s32 bottom;
    s32 m = pcWideView;
    s32 w = clear->clip.w;
    s32 h = clear->clip.h;
    s32 ox = clear->clip.x - draw->ofs[0]; /* the buffer's corner in drawing coordinates */
    s32 oy = clear->clip.y - draw->ofs[1];
    s32 shown = disp->screen.h > 0 && disp->screen.h < h ? disp->screen.h : h;
    s32 i;
    s32 n = 0;
    s16 r[4][4];
    const u8* c = op9DVar1.m18_backgroundClearColor;

    if ((c[0] | c[1] | c[2]) == 0 || pcFieldWideKeepsClear()) {
        return; /* black already, or the colour belongs to the scene */
    }
    pcWideFrameLines(clear->clip.x, clear->clip.y, &top, &bottom);
    if (m > 0) {
        r[n][0] = -m, r[n][1] = -top, r[n][2] = m, r[n][3] = top + h + bottom, n++; /* left */
        r[n][0] = w, r[n][1] = -top, r[n][2] = m, r[n][3] = top + h + bottom, n++;  /* right */
    }
    if (top > 0) {
        r[n][0] = 0, r[n][1] = -top, r[n][2] = w, r[n][3] = top, n++;
    }
    if (top > 0 || bottom > 0) {
        if (h - shown + bottom > 0) {
            r[n][0] = 0, r[n][1] = shown, r[n][2] = w, r[n][3] = h - shown + bottom, n++;
        }
    }
    for (i = 0; i < n; i++) {
        TILE* t = pcWideAlloc(sizeof(TILE));
        if (t == NULL) {
            return;
        }
        setlen(t, 3);
        setcode(t, 0x60);
        t->r0 = t->g0 = t->b0 = 0;
        t->x0 = ox + r[i][0];
        t->y0 = oy + r[i][1];
        t->w = r[i][2];
        t->h = r[i][3];
        addPrim(ot, t);
        pcWidePrim(t, 0, PC_WIDE_ONLY | PC_WIDE_NOEXTEND | PC_WIDE_NOT_SCENE);
    }
}

/* ---- dialog windows -------------------------------------------------------- */

/* A window anchored to its speaker is clamped inside the 4:3 screen (x 12 to
 * 0x124 - width * 4). In the wide picture it is clamped inside the wide one
 * instead, so a speaker in the margins gets the window beside them: the
 * whole window is moved there (pcWidePrim's dx), sliding with its opening
 * animation, while VRAM keeps the PS1's window. */
extern DialogWindow gDialogWindows[4];
extern s16 dialogWindowOpenAnimationNumFrames;

static struct {
    s16 ps1X;  /* where the game put it */
    s16 wantX; /* where it wanted it before the clamps */
    s16 width; /* its width in the clamp's units */
} dialogWide[4];

/* showDialogWindowForActor, after createDialogWindow: wantX == ps1X for a
 * window the script placed (or centred), which never moves. */
void pcFieldDialogWide(s32 window, s32 ps1X, s32 wantX, s32 width) {
    if (window < 0 || window >= 4) {
        return;
    }
    dialogWide[window].ps1X = ps1X;
    dialogWide[window].wantX = wantX;
    dialogWide[window].width = width;
}

/* The window's move in the wide picture this frame. */
s32 pcFieldDialogDx(s32 window) {
    s32 m = pcWideView;
    s32 x;
    s32 dx;
    s32 frames = dialogWindowOpenAnimationNumFrames;
    s32 left;

    if (m == 0 || window < 0 || window >= 4) {
        return 0;
    }
    x = dialogWide[window].wantX;
    if (x < 0xC - m) {
        x = 0xC - m;
    }
    if (x + 0x10 + dialogWide[window].width * 4 >= 0x135 + m) {
        x = 0x124 - dialogWide[window].width * 4 + m;
    }
    dx = x - dialogWide[window].ps1X;
    left = gDialogWindows[window].m408_openAnimationCounter;
    if (left > 0 && frames > 0) {
        /* opening: it comes from the speaker in both pictures */
        dx = dx * (frames - left) / frames;
    }
    return dx;
}

/* The first packet linked to ot[0] now. */
uptr pcFieldOtHead(OTag* ot) {
    return OT_NEXT(*ot);
}

/* Move in the wide picture every packet linked to ot[0] since `head`. */
void pcFieldOtShift(OTag* ot, uptr head, s32 dx) {
    uptr p = OT_NEXT(*ot);
    s32 guard = 0;

    if (dx == 0 || pcWideView == 0) {
        return;
    }
    while (p != head && p != 0 && guard++ < 4096) {
        pcWidePrim((void*)p, dx, 0);
        p = OT_NEXT(*(OTag*)p);
    }
}

/* The field menu (openFieldMenu): it has its own wide layout over the field's
 * picture, so a field's opt-out does not hold while it shows. */
void pcFieldWideMenu(s32 open) {
    if (open) {
        pcFirstPersonEnd();
        pcWideBlock(PC_WIDE_BLOCK_OPTOUT, 0);
    } else {
        pcFieldWideScene();
    }
}
