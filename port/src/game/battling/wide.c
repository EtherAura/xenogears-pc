/*
 * The gear arena (battling) in a wide picture (include/pc_wide.h): the PC-only
 * parts the game C calls under PLATFORM_PC. Nothing here changes VRAM, the
 * game's state or its random numbers; at aspect 4:3 (pcWideView == 0) every
 * framing function does nothing. The pause hooks also notify the optional
 * high-resolution CPU effect, including in the original framing.
 *
 *  - The floor: the game draws the cells inside a view cone it rasterises
 *    into per-row column spans (setBattlingGroundView, a 112.5-degree arc 24
 *    cells out, or the replay's narrow one). A wider cone, from the current
 *    projection and the cull margin, gives the cells the margins need; they
 *    are drawn wide-only from pcWideAlloc (ground.c), so the game's 1800
 *    floor triangles are never used up.
 *  - The fire (drawBattlingFire): a 112 x 48 heat field the CPU steps and
 *    loads as an 8-bit texture stretched over the 320 x 218 screen. A wider
 *    field here, stepped the same way with its own random numbers, holds the
 *    game's field in the middle and goes on into both margins, where it is
 *    drawn as wide-only tiles of the same palette and blend. The game's field
 *    cools to black at its edges (its outer columns are never heated), so the
 *    columns there are drawn over from the wide field too, unless
 *    XG_WIDE_NOSHIFT asks for the 4:3 centre to stay as it is (widecheck).
 *  - The pause screen: captureBattlingScreen moves the shown frame to
 *    (0x140, 0x100), which keeps its wide picture (the core's capture), and
 *    StoreImages it; each blurBattlingScreenCopy blurs the RAM copy and loads
 *    it back, which would leave the capture without margins. The wide picture
 *    (as shown, the HUD at the wide edges) is read once, blurred the same way
 *    here each time, and loaded as the capture's wide copy.
 *  - The demo (renderBattlingDemoScene) draws the last frame over the new one
 *    with three sprites; wide-only sprites beside them take the last frame's
 *    margins.
 *  - The fight HUD is one frame across the top: each gear's portrait and
 *    gauge column in a corner, its name plate, and the HP bars in the middle.
 *    In a wide picture the corner groups (portrait, column, gauges, round
 *    marks, the name) go to the wide edges and the name plates and the top
 *    and bottom lines are drawn on to them; the HP bars stay in the middle.
 *    The practice screen's move lines and the replay's REPLAY go with their
 *    side, and the tutorial's arrow with what it points at. The menus, the
 *    dialog windows and the centred texts stay in the middle.
 *  - XG_WIDE_NOSHIFT (tools/widecheck.py) leaves out everything drawn over the
 *    4:3 centre, so the centre stays the 4:3 picture.
 */
#include "common.h"
#include "psyq.h"
#include "gte.h"
#include "battling/battling.h"
#include "pc_effect.h"

s32 getAngleCos(s32 angle);
s32 getAngleSin(s32 angle);
void pcWideBattlingGroundCells(OTag* ot, s32 camX, s32 camZ, const u8* wideStart, const u8* wideEnd);
const char* portOption(const char* name);

extern u8 battlingArenaRowStarts[];  /* per map row: the arena's first column */
extern u8 battlingArenaRowEnds[];  /* per map row: its last column */
extern MATRIX battlingAspectMatrix; /* the camera's aspect matrix (x scale in m[0][0]) */

/* ---- the floor ------------------------------------------------------------- */

static struct {
    s32 valid;
    u8 start[0x80];
    u8 end[0x80];
} cone;

/* addBattlingGroundSpanEdge into the wide cone's spans. */
static void coneEdge(s32 x0, s32 y0, s32 x1, s32 y1) {
    s32 t;
    s32 x;
    s32 dx;
    s32 xi;

    if (y0 == y1) {
        return;
    }
    if (y1 < y0) {
        t = x1;
        x1 = x0;
        x0 = t;
        t = y1;
        y1 = y0;
        y0 = t;
    }
    x = x0 << 8;
    dx = ((x1 - x0) << 8) / (y1 - y0);
    for (; y0 < y1; y0++, x += dx) {
        if (y0 < 0) {
            continue;
        }
        if (y0 >= 0x80) {
            return;
        }
        xi = x >> 8;
        if (xi < cone.start[y0]) {
            if (xi < battlingArenaRowStarts[y0]) {
                xi = battlingArenaRowStarts[y0];
            }
            cone.start[y0] = xi;
        }
        if (cone.end[y0] < xi) {
            if (battlingArenaRowEnds[y0] + 1 < xi) {
                xi = battlingArenaRowEnds[y0] + 1;
            }
            cone.end[y0] = xi;
        }
    }
}

/* ratan2's arithmetic (the repository's matched C, port/src/gte/libgte.c). */
s32 libgteRatan2(s32 y, s32 x);

/* Half the wide picture's horizontal view angle with the cull margin (4096 to
 * a turn): the projection is H * (aspect x scale) / 4096 pixels per unit of
 * x / z, and the picture reaches 160 + pcWideCull pixels from its centre.
 * Read without ReadGeomScreen and ratan2: every libgte call is a safe point
 * that moves the stepped clock (XG_CLOCK=step) on, and the 4:3 picture does
 * not make these. */
static s32 wideHalfAngle(void) {
    s32 focal = ((s32)gteCfc2(26) * battlingAspectMatrix.m[0][0]) >> 12; /* H, as ReadGeomScreen */

    if (focal < 1) {
        focal = 1;
    }
    return libgteRatan2(160 + pcWideCull, focal);
}

/* The same marker as setBattlingGroundView(Narrow) (x, z in world units, the
 * camera's position; angle from the target to the camera), with the far arc
 * continued to the wide view: the game's near points behind the camera, the
 * game's arc points, and more at the same step on to the wide half angle and a
 * little more. Inside the game's arc the far edge is the game's (no extra cell
 * shows in the 4:3 picture there); the polygon holds the game's, and its sides
 * are no narrower than the arc's ends anywhere in front of the camera. */
void pcWideBattlingGroundCone(s32 x, s32 z, s32 angle, s32 narrow) {
    s32 nearR = narrow ? 16 : 10;
    s32 nearA = narrow ? 0x100 : 0x280;
    s32 farR = narrow ? 32 : 24;
    s32 a0 = angle + (narrow ? 0x78A : 0x580); /* the game's arc */
    s32 step = narrow ? 0x75 : 0x100;
    s32 a1 = a0 + step * (narrow ? 2 : 5);
    s32 arc[48];
    s32 half;
    s32 sx;
    s32 sz;
    s32 px;
    s32 pz;
    s32 nx;
    s32 nz;
    s32 a;
    s32 n = 0;
    s32 i;

    for (i = 0; i < 0x80; i++) {
        cone.start[i] = 0xFF;
        cone.end[i] = 0;
    }
    cone.valid = 0;
    if (pcWideCull == 0) {
        return;
    }
    half = wideHalfAngle() + 0x20;
    if (half > 0x3E0) {
        half = 0x3E0;
    }
    if (angle + 0x800 - half < a0) {
        arc[n++] = angle + 0x800 - half;
        for (a = a0 - step * ((a0 - (angle + 0x800 - half) - 1) / step); a < a0; a += step) {
            arc[n++] = a;
        }
    }
    for (a = a0; a <= a1; a += step) {
        arc[n++] = a;
    }
    if (angle + 0x800 + half > a1) {
        for (a = a1 + step; a < angle + 0x800 + half; a += step) {
            arc[n++] = a;
        }
        arc[n++] = angle + 0x800 + half;
    }
    x >>= 8;
    z >>= 8;
    sx = x + ((getAngleCos(angle + nearA) * nearR) >> 12);
    sz = z + ((getAngleSin(angle + nearA) * nearR) >> 12);
    px = sx;
    pz = sz;
    for (i = 0; i < n; i++) {
        nx = x + ((getAngleCos(arc[i]) * farR) >> 12);
        nz = z + ((getAngleSin(arc[i]) * farR) >> 12);
        coneEdge(px, pz, nx, nz);
        px = nx;
        pz = nz;
    }
    nx = x + ((getAngleCos(angle - nearA) * nearR) >> 12);
    nz = z + ((getAngleSin(angle - nearA) * nearR) >> 12);
    coneEdge(px, pz, nx, nz);
    coneEdge(sx, sz, nx, nz);
    cone.valid = 1;
}

/* After the game's floor (drawBattlingGround): the wide cone's other cells. */
void pcWideBattlingGround(OTag* ot, s32 camX, s32 camZ) {
    if (cone.valid && pcWideCull != 0) {
        pcWideBattlingGroundCells(ot, camX, camZ, cone.start, cone.end);
    }
    cone.valid = 0;
}

/* ---- the fire ------------------------------------------------------------------ */

extern u16 battlingFirePalette[]; /* the fire's palette (loaded to 0, 0x1FD) */

#define FIRE_COLS 0x70   /* the game's field: 112 columns, rows 0..47 */
#define FIRE_ROWS 0x31   /* and a row 48 that stays 0 */
#define FIRE_EXTRA 104   /* columns beside it: 32:9's margin is 93 */
#define FIRE_W (FIRE_COLS + 2 * FIRE_EXTRA)
/* The game's columns its cold edges darken (measured: below about 80% of the
 * middle's heat), more towards the top where the cold has spread further. */
#define FIRE_SEAM_L(row) (3 + (0x2F - (row)) / 4)
#define FIRE_SEAM_R(row) (FIRE_SEAM_L(row) + 1)
/* The quad maps u 0..0x6F over x 0..320 and v 0..0x2A over y 0..218: pixel
 * (x, y) shows texel (floor(x * 111 / 320 + 1/2), floor(y * 42 / 218 + 1/2)),
 * so columns 0..111 and rows 0..42 show (measured on the GPU). */
#define FIRE_SHOWN_COLS 0x70
#define FIRE_SHOWN_ROWS 0x2B

extern s16* battlingFireHeat; /* the game's field */

static struct {
    s32 live;
    u32 seed;
    s16 cur[FIRE_ROWS][FIRE_W];
    s16 next[FIRE_ROWS][FIRE_W];
} fire;

/* Whether wide-only drawing may cover the 4:3 picture (XG_WIDE_NOSHIFT keeps
 * the centre as it is, for tools/widecheck.py). */
static s32 mayCoverCentre(void) {
    static s32 known;
    static s32 may;

    if (!known) {
        const char* s = portOption("XG_WIDE_NOSHIFT");
        may = s == NULL || (s[0] == '0' && s[1] == '\0');
        known = 1;
    }
    return may;
}

static u32 fireRand(void) {
    fire.seed = fire.seed * 1103515245 + 12345;
    return fire.seed >> 16;
}

/* initBattlingFire: everything cold. */
void pcWideBattlingFireReset(void) {
    s32 r;
    s32 c;

    for (r = 0; r < FIRE_ROWS; r++) {
        for (c = 0; c < FIRE_W; c++) {
            fire.cur[r][c] = 0;
            fire.next[r][c] = 0;
        }
    }
    fire.live = 1;
}

/* After updateBattlingFire stepped the game's field (game: its rows 0..47 as
 * they are now): the same step over the wide field, then the game's columns
 * (all but the cold ones at its edges) put in. Run at every aspect, so the
 * margins are alight when a wide aspect is chosen. */
void pcWideBattlingFireUpdate(const s16* game) {
    s32 heat = 0;
    s32 x;
    s32 y;
    s32 v;

    if (!fire.live || game == NULL) {
        return;
    }
    for (x = 0; x < FIRE_W; x++) {
        switch (fireRand() & 3) {
        case 0:
            heat = 0x180;
            break;
        case 1:
            heat = 0;
            break;
        }
        fire.next[0x2F][x] = heat;
    }
    for (y = 0x2F; y >= 2; y--) {
        for (x = 1; x < FIRE_W - 1; x++) {
            v = (fire.cur[y - 1][x] + fire.cur[y][x + 1] + fire.cur[y][x - 1] + fire.cur[y + 1][x + 1] +
                 fire.cur[y + 1][x - 1]) / 5;
            if (v >= 4) {
                v -= 3;
            }
            fire.next[y - 1][x] = v;
        }
    }
    for (y = 0; y < 0x30; y++) {
        for (x = FIRE_SEAM_L(y); x < FIRE_COLS - FIRE_SEAM_R(y); x++) {
            fire.next[y][FIRE_EXTRA + x] = game[y * FIRE_COLS + x];
        }
    }
    for (y = 0; y < 0x30; y++) {
        for (x = 0; x < FIRE_W; x++) {
            fire.cur[y][x] = fire.next[y][x];
        }
    }
}

/* ceil(a / b) for b > 0. */
static s32 ceilDiv(s32 a, s32 b) {
    return a >= 0 ? (a + b - 1) / b : -(-a / b);
}

/* Texel column k's (or row k's) first pixel: the pixel centres (2x + 1) / 2
 * map to texel k from (2k - 1) / 2 on, in steps of 111 / 320 (42 / 218). */
static s32 fireEdge(s32 k, s32 pixels, s32 texels) {
    return ceilDiv((2 * k - 1) * pixels, 2 * texels);
}

/* A texel's colour, as the quad's colour c modulates it (0x80: as it is). */
static u32 fireTexel(s32 heat, s32 c) {
    u32 t15 = battlingFirePalette[heat & 0xFF];
    s32 r = t15 & 0x1F;
    s32 g = (t15 >> 5) & 0x1F;
    s32 b = (t15 >> 10) & 0x1F;

    if (c != 0x80) {
        r = (r * c) >> 7;
        g = (g * c) >> 7;
        b = (b * c) >> 7;
        r = r > 0x1F ? 0x1F : r;
        g = g > 0x1F ? 0x1F : g;
        b = b > 0x1F ? 0x1F : b;
    }
    return r | g << 8 | b << 16;
}

/* A tile of colour rgb (fireTexel's form) over texel (k, r), linked at ot;
 * 0 when the pool is full. */
static s32 fireTile(OTag* ot, s32 k, s32 r, u32 rgb) {
    s32 x0 = fireEdge(k, 320, 111);
    s32 x1 = fireEdge(k + 1, 320, 111);
    s32 y0 = fireEdge(r, 0xDA, 42);
    s32 y1 = fireEdge(r + 1, 0xDA, 42);
    TILE* t;

    if (k == -1) {
        x1 = 0; /* texel 0's pixels left of the quad */
    }
    if (k == FIRE_SHOWN_COLS) {
        x0 = 320; /* texel 111's right of it */
    }
    if (k >= 0 && k < FIRE_SHOWN_COLS) {
        x0 = x0 < 0 ? 0 : x0;
        x1 = x1 > 320 ? 320 : x1;
    }
    x0 = x0 < -pcWideView ? -pcWideView : x0;
    x1 = x1 > 320 + pcWideView ? 320 + pcWideView : x1;
    y0 = y0 < 0 ? 0 : y0;
    y1 = y1 > 0xDA ? 0xDA : y1;
    if (rgb == 0 || x0 >= x1) {
        return 1; /* adds or takes nothing */
    }
    t = pcWideAlloc(sizeof(TILE));
    if (t == NULL) {
        return 0;
    }
    setlen(t, 3);
    t->r0 = (rgb & 0x1F) << 3;
    t->g0 = ((rgb >> 8) & 0x1F) << 3;
    t->b0 = ((rgb >> 16) & 0x1F) << 3;
    t->code = 0x62; /* TILE, semi-transparent: the blend of the draw mode */
    t->x0 = x0;
    t->y0 = y0;
    t->w = x1 - x0;
    t->h = y1 - y0;
    AddPrim(ot, t);
    pcWideOnly(t);
    return 1;
}

/* Whether texel (k, r) is drawn from the wide field: the margins, and with
 * seam the game's cold columns. */
static s32 fireWide(s32 k, s32 r, s32 seam) {
    return k < 0 || k >= FIRE_SHOWN_COLS || (seam && (k < FIRE_SEAM_L(r) || k >= FIRE_COLS - FIRE_SEAM_R(r)));
}

/* drawBattlingFire, before it links its quad (colour c, texture page tpage)
 * at ot: the wide field's texels as wide-only tiles, linked so they are drawn
 * after the quad. The margins' are added in the quad's draw mode (additive).
 * Over the game's cold columns its texels are first taken away again
 * (subtractive tiles between two draw-mode packets, the second putting back
 * the mode the quad left), so those show the wide field's colour too. */
void pcWideBattlingFire(OTag* ot, s32 c, s32 tpage) {
    static DR_TPAGE modes[2][2];
    static s32 buf;
    s32 reach;
    s32 seam;
    s32 k;
    s32 r;

    if (pcWideView == 0 || !fire.live || battlingFireHeat == NULL) {
        return;
    }
    reach = ceilDiv(pcWideView * 111, 320) + 1;
    if (reach > FIRE_EXTRA - 1) {
        reach = FIRE_EXTRA - 1;
    }
    seam = mayCoverCentre();
    for (k = -reach; k < FIRE_SHOWN_COLS + reach; k++) {
        for (r = 0; r < FIRE_SHOWN_ROWS; r++) {
            if (fireWide(k, r, seam) && !fireTile(ot, k, r, fireTexel(fire.cur[r][FIRE_EXTRA + k], c))) {
                return;
            }
        }
    }
    if (!seam) {
        return;
    }
    buf ^= 1;
    SetDrawTPage(&modes[buf][1], 0, 1, tpage); /* as the quad left it */
    AddPrim(ot, &modes[buf][1]);
    for (k = 0; k < FIRE_SHOWN_COLS; k++) {
        for (r = 0; r < FIRE_SHOWN_ROWS; r++) {
            if (fireWide(k, r, seam) && !fireTile(ot, k, r, fireTexel(battlingFireHeat[r * FIRE_COLS + k], c))) {
                break;
            }
        }
    }
    SetDrawTPage(&modes[buf][0], 0, 1, (tpage & ~0x60) | 0x40); /* subtractive */
    AddPrim(ot, &modes[buf][0]);
}

/* ---- the pause screen's blurred capture ----------------------------------------- */

#define PAUSE_W (320 + 2 * 288) /* 32:9's margin is 268 */
#define PAUSE_H 0xDA

static struct {
    s32 m; /* the wide copy's margin, 0: none */
    u16 buf[PAUSE_W * PAUSE_H + 1]; /* final row-wrap neighbour */
} pause;

static RECT* pauseRect(void) {
    static RECT r;

    r.x = 0x140;
    r.y = 0x100;
    r.w = 0x140;
    r.h = PAUSE_H;
    return &r;
}

/* captureBattlingScreen, after its MoveImage: keep the wide copy. */
void pcWideBattlingCapture(void) {
    pause.m = pcWideView != 0 ? pcWideCaptureRead(pauseRect(), pause.buf, PAUSE_W * PAUSE_H) : 0;
    if (pause.m != 0) pause.buf[(320 + 2 * pause.m) * PAUSE_H] = 0;
    pcHiresBattlingCapture(pauseRect());
}

/* blurBattlingScreenCopy, after its LoadImage of the blurred centre (game,
 * 320 x 218): the same blur over the whole wide picture (blurBattlingImage,
 * math.c: each pixel the average of itself, its right neighbour and the two
 * below, per 5-bit channel, all rows but the last), loaded as the capture's
 * wide copy. The wide picture was taken as shown, with the HUD at the wide
 * edges, so it is blurred whole; with XG_WIDE_NOSHIFT (the HUD where the 4:3
 * picture has it) its centre is the game's blurred picture exactly. */
void pcWideBattlingBlur(const u16* game) {
    s32 m = pause.m;
    s32 w = 320 + 2 * m;
    s32 i;
    s32 x;
    s32 y;
    u32 a;
    u32 b;
    u32 c;
    u32 d;
    u16* p = pause.buf;

    if (m == 0 || m != pcWideView || game == NULL) {
        if (game != NULL) pcHiresBattlingBlur(pauseRect());
        return;
    }
    for (i = 0; i < w * (PAUSE_H - 1); i++) {
        a = p[i];
        b = p[i + 1];
        c = p[i + w];
        d = p[i + w + 1];
        p[i] = ((((a & 0x1F) + (b & 0x1F) + (c & 0x1F) + (d & 0x1F)) >> 2) |
                (((((a >> 5) & 0x1F) + ((b >> 5) & 0x1F) + ((c >> 5) & 0x1F) + ((d >> 5) & 0x1F)) >> 2) << 5) |
                (((((a >> 10) & 0x1F) + ((b >> 10) & 0x1F) + ((c >> 10) & 0x1F) + ((d >> 10) & 0x1F)) >> 2) << 10));
    }
    if (!mayCoverCentre()) {
        for (y = 0; y < PAUSE_H; y++) {
            for (x = 0; x < 320; x++) {
                p[y * w + m + x] = game[y * 320 + x];
            }
        }
    }
    pcWideCaptureLoad(pauseRect(), p, m, mayCoverCentre());
    pcHiresBattlingBlur(pauseRect());
}

/* ---- the demo's last-frame sprites --------------------------------------------- */

extern SPRT battlingDemoBlurSprites[6]; /* per buffer: x 0..128, 128..256, 256..320 */
extern u8 battlingBufferIndex;      /* the buffer being drawn */

/* renderBattlingDemoScene, before it links the sprites at ot: their margins.
 * The sprites copy the other buffer (the last frame) 1:1; the game's are
 * marked as drawing their own wide version, and wide-only copies of the last
 * one (drawn after it, in its texture page, at 0x100) reach the last frame's
 * margins through texDx. */
void pcWideBattlingDemo(OTag* ot) {
    s32 i = battlingBufferIndex;
    s32 side;
    s32 x;
    s32 end;
    SPRT* s;

    if (pcWideView == 0) {
        return;
    }
    pcWidePrim(&battlingDemoBlurSprites[i], 0, PC_WIDE_HANDLED);
    pcWidePrim(&battlingDemoBlurSprites[2 + i], 0, PC_WIDE_HANDLED);
    pcWidePrim(&battlingDemoBlurSprites[4 + i], 0, PC_WIDE_HANDLED);
    for (side = 0; side < 2; side++) {
        x = side ? 320 : -pcWideView;
        end = side ? 320 + pcWideView : 0;
        for (; x < end; x += 0x100) {
            s = pcWideAlloc(sizeof(SPRT));
            if (s == NULL) {
                return;
            }
            *s = battlingDemoBlurSprites[4 + i];
            s->x0 = x;
            s->w = end - x < 0x100 ? end - x : 0x100;
            s->u0 = 0;
            AddPrim(ot, s);
            pcWidePrimTex(s, 0, PC_WIDE_ONLY, x - 0x100);
        }
    }
}

/* ---- the fight HUD ---------------------------------------------------------------- */

extern BattlingPrim28 battlingHudSprites[8][2]; /* even: P1's, odd: P2's (panel, portrait, name, gauge) */
extern BattlingHudPrims battlingHudPrims[2];
extern POLY_FT4* battlingGlyphPolys[2]; /* the text glyphs, per buffer */

/* drawBattlingHud, before it links the name plates at ot: the corner groups
 * to the wide edges, and wide-only pieces that carry the name plates (black,
 * 30..99 x 9..20 and 222..290 x 19..30) and the frame's lines (y 7 and 31)
 * out to them. */
void pcWideBattlingHud(OTag* ot) {
    BattlingHudPrims* h = &battlingHudPrims[battlingBufferIndex];
    s32 m = pcWideView;
    s32 i;
    POLY_F4* q;
    LINE_F2* l;

    if (m == 0) {
        return;
    }
    for (i = 0; i < 8; i++) {
        pcWidePrim(&battlingHudSprites[i][battlingBufferIndex], (i & 1) ? m : -m, 0);
    }
    for (i = 0; i < 3; i++) {
        pcWidePrim(&h->m238_tris[i], -m, 0);
        pcWidePrim(&h->m274_trisMirror[i], m, 0);
    }
    pcWidePrim(&h->m2B0_quads[2], -m, 0); /* the ether warnings */
    pcWidePrim(&h->m2B0_quads[3], m, 0);
    if (!mayCoverCentre()) {
        return; /* the 4:3 picture's own plates and lines only */
    }
    for (i = 0; i < 2; i++) {
        q = pcWideAlloc(sizeof(POLY_F4));
        l = pcWideAlloc(sizeof(LINE_F2));
        if (q == NULL || l == NULL) {
            return;
        }
        *q = h->m2B0_quads[i]; /* black, opaque */
        q->x0 = q->x2 = i ? 0x122 : 0x1E - m;
        q->x1 = q->x3 = i ? 0x122 + m : 0x1E;
        AddPrim(ot, q);
        pcWideOnly(q);
        setlen(l, 3);
        *(u32*)&l->r0 = 0x40000000; /* a black line, as the frame's */
        l->x0 = i ? 0x121 : 0x1D - m;
        l->x1 = i ? 0x121 + m : 0x1D;
        l->y0 = l->y1 = i ? 31 : 7;
        AddPrim(ot, l);
        pcWideOnly(l);
    }
}

/* After drawBattlingGearMoveInfo (practice) printed glyphs first..count-1 for
 * the gear on side: they go with that side's corner. */
void pcWideBattlingGlyphs(s32 first, s32 count, s32 side) {
    s32 i;

    for (i = first; pcWideView != 0 && i < count; i++) {
        pcWidePrim(&battlingGlyphPolys[battlingBufferIndex][i], side ? pcWideView : -pcWideView, 0);
    }
}

/* drawBattlingTutorialCursor: the arrow (its tip at x, y) points at the HUD in
 * the Screen Layout lesson; on a corner group or a name it moves with it. */
void pcWideBattlingCursor(void* prim, s32 x, s32 y) {
    s32 dx = 0;

    if (x < 0x1E || (x < 0x64 && y < 0x15)) {
        dx = -pcWideView; /* P1's corner or name */
    } else if (x >= 0x122 || (x >= 0xDE && y >= 0x13 && y < 0x1F)) {
        dx = pcWideView;
    }
    if (dx != 0) {
        pcWidePrim(prim, dx, 0);
    }
}
