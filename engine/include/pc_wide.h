#ifndef PC_WIDE_H
#define PC_WIDE_H

/*
 * True widescreen hooks for the game C (PC only; port/src/gpu/wide.c).
 *
 * With a wide aspect (aspect=16:9, 21:9, 32:9, w:h in settings.ini), every
 * primitive the game draws into its frame buffer is drawn a second time into
 * a wide picture: the same projection and draw area, widened by a margin of
 * pcWideView PS1 pixels on each side. VRAM, and so the 4:3 picture, never
 * changes. The game sees nothing of it except through these hooks, which
 * every caller uses under #ifdef PLATFORM_PC so the PS1 build stays the same.
 * With aspect 4:3, in a mode that is not wide (movies, opted-out fields), or
 * before a mode is marked ready, pcWideView and pcWideCull are 0 and every
 * hook gives the PS1's result.
 *
 * expanded_vertical=1 also uses the display copy at aspect 4:3 or 1:1:
 * fields, RPG battles and the world map draw extra lines above/below the native picture;
 * menus preserve the expanded backdrop captured when they open.
 * pcVerticalCull and PC_WIDE_UY extend render-only Y tests; the same
 * PC_WIDE_ONLY and GTE/state restoration rules apply. Horizontal margins
 * and pcWideCull remain zero when the aspect is not wide.
 *
 * 1. Culling. A render test that drops geometry off the 320-wide screen
 *    must keep what lands in the margins. Use the cull margin pcWideCull
 *    (the visible margin plus a guard band: the PS1's tests keep a polygon
 *    only when one vertex is inside, so tall polygons beside the picture
 *    need room). Widen only decisions about drawing; never a test whose
 *    result reaches game logic, unless that is decided for the mode.
 *      PC_WIDE_UX(x, w)    the unsigned test (u16)x < w, widened:
 *                          -C <= x < w + C
 *      PC_WIDE_X0(x0), PC_WIDE_X1(x1)   a signed bound moved out by C
 *    What only the widened test keeps must not reach VRAM or change the game:
 *      - mark it PC_WIDE_ONLY (pcWideOnly(prim)), or the 4:3 picture changes
 *        where the PS1's test drops polygons that touch the screen (a
 *        polygon spanning it with no vertex inside; battle's 319-wide test
 *        and column 319);
 *      - undo GTE work the PS1 does not do (gteSave/gteLoad, gte_pc.h) and
 *        game state the extra drawing touches (counters, "current" pointers);
 *      - never use up a game buffer the 4:3 picture also needs (the
 *        worldmap's ground triangles did): put extras in a PC pool.
 *    port/src/game/main/render.c (polygons) and model.c's pcWideModel
 *    (whole models) do this for the shared model renderer: copy them.
 *    tools/widecheck.py fails a wide run whose VRAM frames differ from the
 *    4:3 run's.
 *
 * 2. Where wide is allowed. pcWideSetMode() names what the game is showing
 *    (the boot modes; nested screens such as a menu opened from a field set
 *    their own kind and restore the old one). Only kinds marked ready go
 *    wide (port/src/gpu/wide.c: wideReadyDefault, every kind but movie;
 *    XG_WIDE_MODES=all|none|battle,field,... overrides it). pcWideBlock() keeps the picture 4:3 for
 *    a reason the mode knows (a field on the opt-out list, a movie behind a
 *    field). A 4:3 frame in a wide window gets black bars beside it, or
 *    with widefill=zoom is scaled up to the frame's width (top and bottom
 *    cut), or with widefill=blur gets blurred sides (F6 cycles them).
 *
 * 3. 2D layout and wide-only drawing. pcWidePrim() attaches a wide-picture
 *    treatment to one primitive (packet) for the next DrawOTag/DrawOTagEnv:
 *      dx      moved this many pixels in the wide picture only (HUD groups
 *              anchored to the wide edges: pcWideAnchorLeft/Right);
 *      flags   PC_WIDE_ONLY: drawn into the wide picture only, never VRAM
 *              (extra pieces for the margins);
 *              PC_WIDE_HANDLED: it samples a screen capture or a frame
 *              buffer and the mode draws a wide version, so the frame may
 *              stay wide (otherwise such a primitive makes the frame 4:3);
 *              PC_WIDE_EXTEND / PC_WIDE_NOEXTEND: stretch it across the
 *              margins, or never (by default an untextured primitive from
 *              edge to edge of the draw area is stretched: fades, clears).
 *    Register it each frame where it is linked; registrations are dropped
 *    after each DrawOTag. In the 4:3 picture nothing changes.
 *
 * 4. Screen captures. A MoveImage of a whole frame buffer (a capture to
 *    texture space, e.g. 320x224 to 0x2C0,0x100) keeps the wide picture
 *    beside it; a later MoveImage back (menus over the field) uses it, so
 *    the margins come back too. A whole-screen capture of a picture that has
 *    no wide version (a frame-sized LoadImage or MoveImage into texture
 *    space, a frame shown 4:3, a movie's last frame) gets black margins, so
 *    effects drawn from it stay wide. A primitive textured from a capture
 *    (or from a frame buffer) takes its 15-bit texels from the wide copy in
 *    the wide pass, so its centre is the same; such a primitive makes the
 *    frame 4:3 unless marked PC_WIDE_HANDLED (the mode draws the margins
 *    itself) or the capture's margins are the black ones. pcWidePrimTex()
 *    adds texDx to the texel x in the wide pass, so extra PC_WIDE_ONLY
 *    pieces can reach the capture's margins (x < 0 or >= width).
 *    pcWideCapture() makes the wide copy for an effect that captures with
 *    StoreImage instead.
 *
 * Done for the GPU without any game change: clears (ClearImage, a DRAWENV's
 * background) and untextured primitives from edge to edge of the draw area
 * (fades, flashes, sky bands) cover the margins; each frame's margins start
 * black; a picture that comes from VRAM rather than drawing (a LoadImage, a
 * MoveImage from outside the frame buffers, drawing with a draw area that is
 * no frame buffer, a display showing nothing drawn wide) goes into the
 * centre, beside it the colour of each of its rows that has one colour
 * (a cleared background), else black; 24-bit displays (movies) and
 * displays not 256-640 pixels wide stay 4:3.
 *
 * Checking: XG_DUMP_FRAMES also writes wide_<vblank>.png and wide.log (m,
 * clean, shifted, masks, why; a frame with no wide picture at all gets none=1
 * and the reason); tools/widecheck.py compares a 4:3 run with a wide one: a
 * shifted frame's centre is compared outside its masks (where 2D moved by
 * pcWidePrim's dx was and landed, and wide-only pieces over the centre), so
 * HUD anchored to the wide edges needs no XG_WIDE_NOSHIFT=1 (which keeps
 * pcWidePrim's dx out, so the whole centre is compared). XG_WIDE_LOG=1 logs
 * frame buffers, captures, 4:3 frames and what each display shows.
 *
 * Timing: nothing done only for the wide picture may reach a safe point
 * (port/src/system/hw.h): every libgte function starts with one, and under
 * XG_CLOCK=step each moves the clock on, so interrupts would land elsewhere
 * than at 4:3 and the game could change. Use the GTE macros (gte.h,
 * include/battle/pc_wide_fx.h) and the pcWide* calls, which have none.
 *
 * Reference code (battle):
 *   port/src/game/main/render.c   polygons kept for the margins (all modes)
 *   src/main/model.c, light.c     whole models (pcWideModel)
 *   src/battle_fx_tiled_sprite/fx_801FC000.c   a screen-x loop widened, its
 *                                 extras from pcWideAlloc, GTE put back
 *   src/main/background_render.c  panorama strips for the margins
 *   src/battle/b_800728B8.c       the party's boxes anchored to the edges
 */


/* The visible margin M (PS1 pixels on each side of the frame buffer) and
 * the cull margin C = M + guard band, for the frame being built. */
extern int pcWideView;
extern int pcWideCull;
/* Optional vertical scene expansion; zero with original framing. */
extern int pcVerticalCull;
#define PC_WIDE_UY(y, h) ((unsigned int)((s16)(y) + pcVerticalCull) < (unsigned int)((h) + 2 * pcVerticalCull))

#define PC_WIDE_UX(x, w) ((unsigned int)(((x) + pcWideCull) & 0xFFFF) < (unsigned int)((w) + 2 * pcWideCull))
#define PC_WIDE_X0(x0) ((x0) - pcWideCull)
#define PC_WIDE_X1(x1) ((x1) + pcWideCull)

/* While nonzero, the shared model renderer marks every primitive it links
 * PC_WIDE_ONLY (a model only the widened object test keeps). */
extern int pcWideOnlyScope;

/* What the game shows (the boot mode numbers, kernel.c gameModes). */
#define PC_WIDE_KERNEL 0
#define PC_WIDE_FIELD 1
#define PC_WIDE_BATTLE 2
#define PC_WIDE_WORLDMAP 3
#define PC_WIDE_BATTLING 4
#define PC_WIDE_MENU 5
#define PC_WIDE_MOVIE 6
#define PC_WIDE_KINDS 7
/* Set the kind; returns the previous one (restore it when a nested screen
 * ends). */
int pcWideSetMode(int kind);
int pcWideGetMode(void); /* current screen kind, without changing it */

/* pcWideBlock reasons: a bit set keeps the picture 4:3. */
#define PC_WIDE_BLOCK_OPTOUT 0x1 /* a field on the opt-out list */
#define PC_WIDE_BLOCK_MOVIE 0x2  /* a movie or movie-backed scene */
#define PC_WIDE_BLOCK_EFFECT 0x4 /* an effect without a wide version */
void pcWideBlock(unsigned int reason, int on);
/* The opt-out list: scenes judged not to look right wide (a field whose room
 * ends at the 4:3 edges). Built in from port/src/gpu/wide_optout.txt;
 * XG_WIDE_OPTOUT=<file> reads another one, XG_WIDE_OPTOUT=none none. Returns
 * the reason scene `id` of `kind` (a field's number) is on it, or NULL; the
 * mode blocks with PC_WIDE_BLOCK_OPTOUT while it shows such a scene. */
const char* pcWideOptOut(int kind, int id);

#define PC_WIDE_ONLY 0x1
#define PC_WIDE_HANDLED 0x2
#define PC_WIDE_EXTEND 0x4
#define PC_WIDE_NOEXTEND 0x8
/* The native packet has a separate PC_WIDE_ONLY replacement in the display
 * copy (e.g. the zoomed title). It still draws normally into native VRAM. */
#define PC_WIDE_NATIVE_ONLY 0x20
/* With PC_WIDE_ONLY: the primitive may also draw into the 32 rows below the
 * frame buffer in the wide picture (rows no frame uses: 224-255 and 480-511 in
 * fields; none where buffers touch or with expanded vertical framing), and
 * beyond its sides (the wide target is 2048 pixels wide), and takes texels
 * from there as from the frame buffer. Never shown: scratch for an effect that
 * needs a copy of part of the frame while it redraws the frame in place (the
 * field's screen distortion). Place it beside the picture with pcWidePrim's
 * dx, which XG_WIDE_NOSHIFT leaves alone for these: the GPU wraps vertex x to
 * 11 bits (-1024..1023) before the wide shift. */
#define PC_WIDE_BELOW 0x10
void pcWidePrim(void* prim, int dx, unsigned int flags);
void pcWidePrimTex(void* prim, int dx, unsigned int flags, int texDx);
void pcWidePrimTex2D(void* prim, int dx, unsigned int flags, int texDx, int texDy);
/* Texture policy for this packet, independently of its wide treatment.
 * Untagged packets are 3D. Register at submission each frame; the same
 * per-DrawOTag lifetime as pcWidePrim applies, even at aspect 4:3. */
#define PC_TEX_3D 0
#define PC_TEX_SPRITE 1
#define PC_TEX_HUD 2
#define PC_TEX_TEXT 3
#define PC_TEX_PORTRAIT 4
#define PC_TEX_BATTLE_PORTRAIT 5
#define PC_TEX_ICON 6
#define PC_TEX_BATTLE_HUD 7
#define PC_TEX_BATTLE_TEXT 8
#define PC_TEX_MENU_PORTRAIT 9
#define PC_TEX_MENU_THUMB_PORTRAIT 10
#define PC_TEX_CLASSES 11
#define PC_TEX_IS_TEXT(kind) ((kind) == PC_TEX_TEXT || (kind) == PC_TEX_BATTLE_TEXT)
#define PC_TEX_IS_PORTRAIT(kind) ((kind) == PC_TEX_PORTRAIT || (kind) == PC_TEX_BATTLE_PORTRAIT || (kind) == PC_TEX_MENU_PORTRAIT || (kind) == PC_TEX_MENU_THUMB_PORTRAIT)
#define PC_TEX_PORTRAIT_SIZE(kind) (((kind) == PC_TEX_BATTLE_PORTRAIT || (kind) == PC_TEX_MENU_THUMB_PORTRAIT) ? 24 : (kind) == PC_TEX_MENU_PORTRAIT ? 48 : 64)
void pcTextureClass(void* prim, int kind);
/* Draw this packet half transparent, every texel (blend mode 0, whatever its STP bit): the
 * free field camera's scenery between it and the party. The same per-DrawOTag lifetime. */
void pcFadePrim(void* prim);
void pcTextureClassDefault(void* prim, int kind);
/* HUD/menu submitter: tag an unclassified packet HUD, then AddPrim. */
void pcTextureHudPrim(void* ot, void* prim);
void pcTextureTextPrim(void* ot, void* prim);
#define pcWideOnly(prim) pcWidePrim((prim), 0, PC_WIDE_ONLY)
#define pcWideAnchorLeft(prim) pcWidePrim((prim), -pcWideView, 0)
#define pcWideAnchorRight(prim) pcWidePrim((prim), pcWideView, 0)
/* Whether pcWidePrim's dx moves anything (0 under XG_WIDE_NOSHIFT): pieces
 * that only fill gaps the moves open need not be drawn without them. */
int pcWideShifts(void);

/* The wide copy a MoveImage of rect (a RECT*) to (x, y) would make, without
 * the move (effects that capture the screen with StoreImage). Returns the
 * copy's margin on each side. A whole screen that was not drawn wide (the
 * screen before was 4:3) gets black margins; part of one gets none (0). */
int pcWideCapture(const void* rect, int x, int y);
/* The wide copy of a whole-screen capture already in texture space at rect
 * (a RECT*), made while the picture was not wide (a movie's last frame): what
 * VRAM has there, with black margins. Returns the margin. */
int pcWideCaptureVram(const void* rect);
/* Carry the selected Zoom framing into an effect captured from a narrow
 * screen (for example, the last movie frame before the title). */
void pcWideCaptureZoom(int x, int y);
/* The same for a capture whose 4:3 part the CPU loads with the mask bit set
 * on each pixel (StoreImage, |= 0x8000, LoadImage to (x, y)); call it after
 * that LoadImage. The whole copy gets the mask bit (it makes black texels
 * opaque, and every texel of a semi-transparent primitive blend). */
int pcWideCaptureMask(const void* rect, int x, int y);
/* While on, a frame buffer's margins are kept when a new frame starts in it
 * instead of being cleared: for effects that draw over the last frame without
 * a clear, as VRAM keeps the 4:3 part. Turn it off when the effect ends. */
void pcWideKeepMargins(int on);
/* The margin on each side of the wide copy of the capture that holds VRAM
 * (x, y), made earlier by a MoveImage (pcWidePrimTex pieces may sample that
 * far beside it); 0 when there is none. */
int pcWideCaptureMargin(int x, int y);
/* Exact rectangle owns a captured scene, including its rendered margins or
 * extra rows; excludes uploaded artwork with synthetic black margins. */
int pcWideCapturedScene(int x, int y, int w, int h);
/* Extra rows belonging to a captured scene, including at horizontal margin 0. */
int pcWideCaptureLines(int x, int y, int* top, int* bottom);
/* The extra scene rows above (top) and below (bottom) the frame buffer that
 * holds VRAM (x, y) in the display copy this frame (expanded vertical
 * framing); 0 and 0 without. Effects that redraw the frame from itself (the
 * field's screen distortion) draw those rows too. */
void pcWideFrameLines(int x, int y, int* top, int* bottom);
/* While on, a picture without this frame's margins moved into a frame buffer
 * (a capture made from a 4:3 frame, or one with no wide copy) does not make
 * the frame 4:3: it fills the centre and the margins stay black. For screens
 * drawn over a captured backdrop whose own layout is wide (the menus over a
 * field shown 4:3). Turn it off after the move. */
void pcWideNarrowBackdrop(int on);
/* For effects that process a capture on the CPU (StoreImage, change it,
 * LoadImage back), and so lose its margins: pcWideCaptureRead copies the wide
 * copy of the capture at rect (the RECT a MoveImage made it at) into buf,
 * (w + 2m) x h 15-bit pixels, row by row, at most maxPixels; it returns m, 0
 * when there is no wide copy there. pcWideCaptureLoad makes buf (laid out the
 * same, margin m) the wide copy at rect: call it after the LoadImage of the
 * centre, with the same processing done to the whole wide picture; shifted
 * says its centre is not what VRAM has (2D the mode moved, processed with the
 * rest), so frames that show it are not compared with the 4:3 picture. */
int pcWideCaptureRead(const void* rect, void* buf, int maxPixels);
void pcWideCaptureLoad(const void* rect, const void* buf, int m, int shifted);
/* As above, with top/bottom rows included in the packed CPU buffer. */
int pcWideCaptureReadFrame(const void* rect, void* buf, int maxPixels, int* m, int* top, int* bottom);
void pcWideCaptureLoadFrame(const void* rect, const void* buf, int m, int top, int bottom, int shifted);

/* Memory for primitives only the wide picture has (extra copies, strips,
 * pieces), so no game buffer runs out: valid until two DrawOTags after the
 * next one (enough for building an OT and drawing it). NULL when the pool
 * is full or the frame is not wide. */
void* pcWideAlloc(int bytes);
/* Enhanced: a picture drawn while the game's next DrawOTag is pending (a frame
 * between two vblanks, libetc.c pcVSyncBetween): pcWideStash(1) puts the
 * game's registrations aside, pcWideStash(0) after the picture's DrawOTag
 * puts them back. */
void pcWideStash(int on);
/* Enhanced: a frame buffer's wide picture kept before a picture between is
 * drawn into it and put back after the wait (the ripple, fx_801FC000.c):
 * the margins and the shifted 2D the 4:3 StoreImage/LoadImage pair misses.
 * Keep returns 0 with no wide picture (nothing to put back). */
int pcWideFrameKeep(const void* rect);
void pcWideFramePutBack(void);


#endif /* PC_WIDE_H */
