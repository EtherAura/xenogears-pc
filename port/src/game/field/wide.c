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

/* port/src/system/options.c (game code has no libc) */
const char* portOption(const char* name);
int portSceneReplayActive(void);

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
    pcWideBlock(PC_WIDE_BLOCK_OPTOUT, pcWideOptOut(PC_WIDE_FIELD, fieldMapNumber & 0xFFF) != NULL);
}

/* The field mode starts or ends: nothing it blocks applies (the effect block
 * of src/field/f_800A55B8.c lives in the field's data, reset with it). */
void pcFieldWideExit(void) {
    pcWideBlock(PC_WIDE_BLOCK_OPTOUT | PC_WIDE_BLOCK_MOVIE | PC_WIDE_BLOCK_EFFECT, 0);
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
        pcWideBlock(PC_WIDE_BLOCK_OPTOUT, 0);
    } else {
        pcFieldWideScene();
    }
}
