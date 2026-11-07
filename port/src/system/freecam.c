/*
 * The free field camera (Enhanced engine; settings.ini free_camera, off by default): a
 * third-person camera the player turns and tilts with the right stick and zooms with L2 held
 * and the stick up/down (or J/L, I/K, U/O; R3 or Home returns to the game's view), in the
 * manner of Xenoblade.
 *
 * engine/src/field/f_80072140.c updateAllEntities hands pcFieldCamera the eye and target the
 * game chose this frame (after its interpolation and shake), just before the camera matrix is
 * built. The free camera keeps the game's target (the tracked actor) and places the eye around
 * it: the game's heading plus a turn of the player's (so L1/R1's 45-degree turns and any turn
 * the room makes itself still show), a pitch of the player's, at the game's eye distance times
 * a zoom.
 *
 * It is free while the game's own camera follows the tracked actor (interpolation mode 0 or 2),
 * also in rooms whose camera L1/R1 may not turn (playerCamera). Otherwise it blends back to the
 * game's camera (scripted shots) and, while fully the game's, follows it, so freeing it again
 * starts where the game is. The heading it ends with is written to m10E_cameraTan (every sprite's 8-way facing,
 * the compass) and m10C_camera2Tan (the pad's walking directions), so those follow the view.
 *
 * XG_FREECAM=pitch,zoom[,turn] (tests) turns it on regardless of the setting and starts it at
 * that pitch (degrees), zoom (times the game's distance) and turn (degrees from the game's
 * heading).
 *
 * The camera turns and tilts its whole range in every room (user): from some angles the
 * dark past the modelled room shows, as around the game's own interior views. Per-room limits
 * (freecam_limits.h, from tools/freecam_sweep.py; XG_FREECAM_LIMITS=1, A/B): for each room, zoom
 * and world heading, the band of pitches that shows no more open void than the game's own view;
 * the camera does not turn into a heading the room refuses, tilting stays inside the band, and a
 * band that moves (turning, zooming) draws the pitch in, eased. Near the game's own heading the
 * band widens to the game's pitch, so the room's own view always passes.
 *
 * XG_FREECAM_SWEEP=first,gap,cycles (tools/freecam_sweep.py, the per-room limits): from vblank
 * `first`, `cycles` times with `gap` vblanks between (the pad script walks the party on), the
 * camera steps through every world heading, pitch and zoom of the grid below, each held for
 * SWEEP_HOLD vblanks: characters drawn for the first half, hidden for the second, so the frames'
 * difference is what of the party shows. The room's clear colour is magenta for the whole run,
 * so open void shows as magenta. Every vblank logs "freecam sweep: ..." on stderr.
 */
#include <math.h>
#ifndef M_PI
#define M_PI 3.14159265358979323846 /* not in ISO C */
#endif
#include <stdlib.h>

#include <stdio.h>

#include "common.h"
#include "field/field.h"
#include "hw.h"
#include "pad.h"
#include "pc_camera.h"
#include "settings.h"
#include "freecam_limits.h"

extern FieldBss_800AF880 fieldSceneState;
extern FieldBss_800B2184 op9DVar1;
extern u8 renderCharsDisabled;
extern s32 fieldMapNumber;

#define TURN_SPEED (2.2)    /* radians a second at full stick */
#define TILT_SPEED (1.1)
#define ZOOM_SPEED (0.9)    /* zoom factor change a second */
#define PITCH_MIN (8.0 * M_PI / 180)
#define PITCH_MAX (75.0 * M_PI / 180)
#define ZOOM_MIN 0.3
#define ZOOM_MAX 1.4
#define BLEND_TIME 0.4      /* seconds between the game's camera and the free one */

static struct {
    int read, forced, enabled;
    double turn, pitch, zoom;      /* the free camera: turn from the game's heading, pitch, zoom of the game's distance */
    double blend;                  /* 0 the game's camera .. 1 the free one */
    double gamePitch;              /* the game's own pitch this frame */
    double startPitch, startZoom, startTurn;
    uint32_t vblank;
    int recentring;
    double clip;                   /* radians the walls lift the camera's pitch (wallClip) */
} fc;

/* ---- walls: the camera kept inside the room (xg-ugy.3) ----------------------------------------
 *
 * The camera's offset from its target, laid on the floor, is followed through the tracked actor's
 * walkmesh from the actor (triangle to neighbouring triangle across the edge the line leaves by).
 * Only a camera inside something moves: above no floor of the walkmesh (in a building, past a
 * room's wall) and lower than a wall there (WALL_HEIGHT above the edge's floor, the first edge
 * with no neighbour the line leaves by). It keeps its distance and turns up, the least that
 * clears the wall, as the game's own cameras look over walls: up at once, back down eased.
 * Pulling it in instead put it in other buildings or in the party's face in Lahan's lanes. A
 * camera behind a building, above floor again, stays where it is (the scenery between fades:
 * pcSceneryFade). */
#define WALL_HEIGHT 192.0   /* world units: two characters */
#define WALL_MARGIN 24.0
#define LIFT_STEP 0.035     /* radians (2 degrees) */
#define LIFT_MAX 1.2        /* radians: the camera turns up at most this far (about 70 degrees) */

/* Whether (x, z) is above a triangle of the walkmesh. */
static int onFloor(const sWalkMeshTriangle* tris, const SVECTOR* v, int count, double x, double z) {
    for (int i = 0; i < count; i++) {
        const SVECTOR* a = &v[tris[i].m0_verticeIndex[0]];
        const SVECTOR* b = &v[tris[i].m0_verticeIndex[1]];
        const SVECTOR* c = &v[tris[i].m0_verticeIndex[2]];
        double d0 = (b->vx - a->vx) * (z - a->vz) - (b->vz - a->vz) * (x - a->vx);
        double d1 = (c->vx - b->vx) * (z - b->vz) - (c->vz - b->vz) * (x - b->vx);
        double d2 = (a->vx - c->vx) * (z - c->vz) - (a->vz - c->vz) * (x - c->vx);
        if ((d0 >= 0 && d1 >= 0 && d2 >= 0) || (d0 <= 0 && d1 <= 0 && d2 <= 0)) return 1;
    }
    return 0;
}

static double wallHit(double ax, double ay, double az, double dx, double dy, double dz) {
    sFieldEntity* a = &fieldSceneState.m290_actorArray[op9DVar1.m1BA_actorCameraTracked];
    sFieldScriptEntity* e = a->m4C_scriptEntity;
    if (e == NULL || e->m10_walkmeshId < 0 || e->m10_walkmeshId > 3) return 1;
    int mesh = e->m10_walkmeshId;
    const sWalkMeshTriangle* tris = (const sWalkMeshTriangle*)fieldSceneState.m2A4_walkMeshTriangle[mesh];
    const SVECTOR* v = (const SVECTOR*)fieldSceneState.m2B4_walkMeshVertices[mesh];
    int count = fieldSceneState.m2C4_walkMeshNumTrianglePerBlock[mesh];
    int t = e->m8_currentWalkMeshTriangle[mesh], from = -1;
    if (tris == NULL || v == NULL || t < 0 || t >= count) return 1;
    double len = sqrt(dx * dx + dz * dz);
    if (len < 1 || onFloor(tris, v, count, ax + dx, az + dz)) return 1;
    double s = 0;
    for (int step = 0; step < 512; step++) {
        const sWalkMeshTriangle* tri = &tris[t];
        double best = 2; int edge = -1; double bu = 0;
        for (int k = 0; k < 3; k++) {
            if (tri->m6_connectivity[k] == from && from >= 0) continue;
            const SVECTOR* p = &v[tri->m0_verticeIndex[k]];
            const SVECTOR* q = &v[tri->m0_verticeIndex[(k + 1) % 3]];
            double ex = q->vx - p->vx, ez = q->vz - p->vz;
            double den = dx * ez - dz * ex;
            if (fabs(den) < 1e-9) continue;
            double wx = p->vx - ax, wz = p->vz - az;
            double sk = (wx * ez - wz * ex) / den, u = (wx * dz - wz * dx) / den;
            if (u < -1e-6 || u > 1 + 1e-6 || sk < s - 1e-9 || sk >= best) continue;
            best = sk; edge = k; bu = u;
        }
        if (edge < 0 || best >= 1) return 1;            /* the camera is above this triangle: inside */
        int next = tri->m6_connectivity[edge];
        if (next < 0 || next >= count) {
            const SVECTOR* p = &v[tri->m0_verticeIndex[edge]];
            const SVECTOR* q = &v[tri->m0_verticeIndex[(edge + 1) % 3]];
            double floorY = p->vy + (q->vy - p->vy) * bu;
            double camY = ay + dy * best;
            if (floorY - camY > WALL_HEIGHT) return 1;  /* over the wall (up is -y) */
            return best - WALL_MARGIN / len;
        }
        from = t; t = next; s = best;
    }
    return 1;
}

static void wallClip(const VECTOR* at, VECTOR* eye, double dt) {
    sFieldScriptEntity* e = fieldSceneState.m290_actorArray[op9DVar1.m1BA_actorCameraTracked].m4C_scriptEntity;
    if (e == NULL) return;
    double ax = e->m20_position.vx / 65536.0, ay = at->vy / 65536.0, az = e->m20_position.vz / 65536.0;
    double dx = (double)(eye->vx - at->vx) / 65536, dy = (double)(eye->vy - at->vy) / 65536, dz = (double)(eye->vz - at->vz) / 65536;
    double horiz = sqrt(dx * dx + dz * dz), dist = sqrt(dx * dx + dy * dy + dz * dz);
    if (dist < 1 || horiz < 1) return;
    double pitch = atan2(-dy, horiz), want = 0;
    /* the least lift that clears: the camera keeps its distance and turns up over the wall */
    for (double lift = 0; pitch + lift <= LIFT_MAX; lift += LIFT_STEP) {
        double p = pitch + lift, h = dist * cos(p);
        if (wallHit(ax, ay, az, dx / horiz * h, -dist * sin(p), dz / horiz * h) >= 1) { want = lift; break; }
        want = lift + LIFT_STEP;
    }
    if (pitch + want > LIFT_MAX) want = fmax(0, LIFT_MAX - pitch);
    if (want > fc.clip) fc.clip = want;                                  /* up at once */
    else fc.clip += (want - fc.clip) * (1 - exp(-dt * 3));                /* down eased */
    if (fc.clip < 0.001) return;
    double p = pitch + fc.clip, h = dist * cos(p);
    eye->vx = at->vx + (int32_t)(dx / horiz * h * 65536);
    eye->vy = at->vy + (int32_t)(-dist * sin(p) * 65536);
    eye->vz = at->vz + (int32_t)(dz / horiz * h * 65536);
}

static int sweepRequested(void) { const char* e = getenv("XG_FREECAM_SWEEP"); return e && *e; }

int pcFreeCameraEnabled(void) {
    if (!fc.read) {
        fc.read = 1;
        settingsDescribe("free_camera", "free_camera: 1: a third-person camera in fields and on the world map (Enhanced engine), turned, tilted "
                                        "with the right stick, zoomed with L2 + stick up/down, or J/L, I/K, U/O; R3 or Home recentres");
        const char* s = getenv("XG_FREECAM");
        if (s && *s) {
            char* e;
            fc.forced = 1; fc.startZoom = 1;
            fc.startPitch = strtod(s, &e);
            if (*e == ',') fc.startZoom = strtod(e + 1, &e);
            if (*e == ',') fc.startTurn = strtod(e + 1, &e);
        }
    }
    return fc.forced || sweepRequested() || settingsBool("free_camera", 0);
}
void pcFreeCameraSet(int on) { settingsSetInt("free_camera", on ? 1 : 0); settingsSave(); }

static int playerCamera(void) {
    /* Rooms where L1/R1 may not turn the game's camera (dolly set/stop 0xFF, op 0x99 flag
     * 0x8000) are free too (user): there the pad's directions follow the game's
     * fixed view, which the player turning nothing but the free camera found at odds with the
     * picture; turned freely, they follow the view as everywhere else. XG_FREECAM_FIXED=0 keeps
     * those rooms on the game's camera (A/B). */
    static int fixedRooms = -1;
    if (fixedRooms < 0) { const char* e = getenv("XG_FREECAM_FIXED"); fixedRooms = !(e && *e == '0'); }
    int follows = fieldSceneState.mB4_cameraInterpolationMode == 0 || fieldSceneState.mB4_cameraInterpolationMode == 2;
    return follows && (fixedRooms || (fieldSceneState.m174_dollySet != 0xFF && fieldSceneState.m175_dollyStop != 0xFF &&
                                      !(fieldSceneState.m158_op99Var7 & 0x8000)));
}

/* XG_FREECAM_SWEEP: the grid every cycle steps through */
#define SWEEP_HEADINGS 16
static const double sweepPitch[] = {8, 15, 25, 35, 50, 65};
static const double sweepZoom[] = {0.6, 1.0, 1.4};
#define SWEEP_PITCHES ((int)(sizeof(sweepPitch) / sizeof(sweepPitch[0])))
#define SWEEP_ZOOMS ((int)(sizeof(sweepZoom) / sizeof(sweepZoom[0])))
#define SWEEP_HOLD 6
#define SWEEP_LENGTH (SWEEP_HEADINGS * SWEEP_PITCHES * SWEEP_ZOOMS * SWEEP_HOLD)
static struct { int read, on, first, gap, cycles, hid; } sw;

/* The sweep's pose this vblank: 1 and the grid cell while a cycle runs, else 0. */
static int sweepPose(uint32_t now, double* heading, double* pitch, double* zoom, int* chars) {
    if (!sw.read) {
        sw.read = 1;
        const char* e = getenv("XG_FREECAM_SWEEP");
        if (e && sscanf(e, "%d,%d,%d", &sw.first, &sw.gap, &sw.cycles) == 3 && sw.cycles > 0) sw.on = 1;
    }
    if (!sw.on || (int)now < sw.first) return 0;
    int t = (int)now - sw.first, period = SWEEP_LENGTH + sw.gap;
    if (t / period >= sw.cycles || t % period >= SWEEP_LENGTH) return 0;
    int cell = t % period / SWEEP_HOLD, phase = t % period % SWEEP_HOLD;
    *heading = (cell % SWEEP_HEADINGS) * (2 * M_PI / SWEEP_HEADINGS);
    *pitch = sweepPitch[cell / SWEEP_HEADINGS % SWEEP_PITCHES] * M_PI / 180;
    *zoom = sweepZoom[cell / (SWEEP_HEADINGS * SWEEP_PITCHES)];
    *chars = phase < SWEEP_HOLD / 2;
    return 1;
}

static double clampd(double v, double lo, double hi) { return v < lo ? lo : v > hi ? hi : v; }
static double wrap(double a) { while (a > M_PI) a -= 2 * M_PI; while (a < -M_PI) a += 2 * M_PI; return a; }

/* The room's usable pitch band (radians) for an eye heading (world, from the target) and zoom,
 * interpolated over the table's headings and zooms. A heading the room refuses counts as the
 * empty band (90, 0) degrees, so near one the band closes and the camera stops turning. A room
 * without limits, or limits off (the default: user Fei's house held the camera to a
 * 20-42 degree tilt and half a turn), gives the camera's whole range. */
static void roomBand(double heading, double zoom, double* lo, double* hi) {
    static int field = -2, row = -1, limits = -1;
    if (limits < 0) { const char* e = getenv("XG_FREECAM_LIMITS"); limits = e && *e == '1'; }
    if (!limits) { *lo = PITCH_MIN; *hi = PITCH_MAX; return; }
    if (field != (int)fieldMapNumber) {
        field = (int)fieldMapNumber; row = -1;
        for (int i = 0; i < (int)(sizeof(freecamLimits) / sizeof(freecamLimits[0])); i++)
            if (freecamLimits[i].field == field) row = i;
    }
    if (row < 0) { *lo = PITCH_MIN; *hi = PITCH_MAX; return; }
    double hp = heading / (2 * M_PI) * FREECAM_LIMIT_HEADINGS;
    hp -= floor(hp / FREECAM_LIMIT_HEADINGS) * FREECAM_LIMIT_HEADINGS;
    int h0 = (int)hp % FREECAM_LIMIT_HEADINGS, h1 = (h0 + 1) % FREECAM_LIMIT_HEADINGS;
    double fh = hp - floor(hp);
    int z0 = 0;
    while (z0 < FREECAM_LIMIT_ZOOMS - 2 && zoom > freecamLimitZoom[z0 + 1]) z0++;
    double fz = clampd((zoom - freecamLimitZoom[z0]) / (freecamLimitZoom[z0 + 1] - freecamLimitZoom[z0]), 0, 1);
    for (int e = 0; e < 2; e++) {
        double v[2][2];
        for (int a = 0; a < 2; a++)
            for (int b = 0; b < 2; b++) {
                unsigned char d = freecamLimits[row].pitch[z0 + a][b ? h1 : h0][e];
                v[a][b] = d == FREECAM_LIMIT_FORBIDDEN ? (e ? 0 : 90) : d;
            }
        double deg = (v[0][0] * (1 - fh) + v[0][1] * fh) * (1 - fz) + (v[1][0] * (1 - fh) + v[1][1] * fh) * fz;
        *(e ? hi : lo) = deg * M_PI / 180;
    }
}

/* The band at a turn from the game's heading: within 22.5 degrees of it the band widens to take
 * in the game's own pitch, so the room's own view always passes. 0 when the room refuses it (the
 * band is narrower than BAND_MIN). */
#define BAND_MIN (10.0 * M_PI / 180)
static int turnBand(double gHeading, double gPitch, double turn, double zoom, double* lo, double* hi) {
    roomBand(gHeading + turn, zoom, lo, hi);
    double relax = clampd(fabs(turn) / (M_PI / 8), 0, 1);
    double elo = *lo < gPitch ? *lo : gPitch, ehi = *hi > gPitch ? *hi : gPitch;
    *lo = clampd(elo + (*lo - elo) * relax, PITCH_MIN, PITCH_MAX);
    *hi = clampd(ehi + (*hi - ehi) * relax, PITCH_MIN, PITCH_MAX);
    return *hi - *lo >= BAND_MIN;
}

/* The scenery fade (phase 2b) runs while the free camera shows a view of its own (turned,
 * tilted or zoomed away from the game's: the game's own framing stays as it drew it); never in
 * a sweep (it would change what the sweep measures); XG_FREECAM_FADE=0 turns it off (A/B). */
int pcFreeCameraActive(void) {
    static int fade = -1;
    if (fade < 0) { const char* e = getenv("XG_FREECAM_FADE"); fade = !(e && *e == '0'); }
    return fade && !sw.on && fc.blend > 0 &&
           (fabs(fc.turn) > 0.03 || fabs(fc.pitch - fc.gamePitch) > 0.03 || fabs(fc.zoom - 1) > 0.02);
}

void pcFieldCamera(VECTOR* eye, VECTOR* at) {
    if (!pcFreeCameraEnabled()) { fc.blend = 0; return; }
    uint32_t now = hwVSyncCount();
    double dt = fc.vblank && now - fc.vblank < 30 ? (now - fc.vblank) / 60.0 : 1 / 60.0;
    fc.vblank = now;

    /* the game's own view of this frame */
    double gx = (double)eye->vx - at->vx, gy = (double)eye->vy - at->vy, gz = (double)eye->vz - at->vz;
    double gdist = sqrt(gx * gx + gy * gy + gz * gz);
    if (gdist <= 0) return;
    double gHeading = atan2(gz, gx), gPitch = atan2(-gy, sqrt(gx * gx + gz * gz));
    fc.gamePitch = gPitch;

    double sh, sp, sz; int chars;
    int posed = sweepPose(now, &sh, &sp, &sz, &chars);
    if (sw.on) {
        op9DVar1.m18_backgroundClearColor[0] = 255; op9DVar1.m18_backgroundClearColor[1] = 0;
        op9DVar1.m18_backgroundClearColor[2] = 255;
        if (posed || sw.hid) { renderCharsDisabled = posed && !chars; sw.hid = renderCharsDisabled; }
        fprintf(stderr, "freecam sweep: vb %u field %d posed %d heading %.1f pitch %.0f zoom %.2f chars %d free %d "
                        "at %d %d %d game %.1f %.1f dist %.0f\n", now, (int)fieldMapNumber, posed, posed ? sh * 180 / M_PI : 0,
                posed ? sp * 180 / M_PI : 0, posed ? sz : 0, posed ? chars : 1, playerCamera(), (int)(at->vx >> 16),
                (int)(at->vy >> 16), (int)(at->vz >> 16), gHeading * 180 / M_PI, gPitch * 180 / M_PI, gdist / 65536);
        if (posed) {
            double dist = gdist * sz;
            eye->vx = at->vx + (int32_t)(dist * cos(sp) * cos(sh));
            eye->vy = at->vy + (int32_t)(-dist * sin(sp));
            eye->vz = at->vz + (int32_t)(dist * cos(sp) * sin(sh));
            fieldSceneState.m10E_cameraTan = (s16)(lround((sh + M_PI) * 2048 / M_PI) - 0x400);
            fieldSceneState.m10C_camera2Tan = fieldSceneState.m10E_cameraTan;
            return;
        }
    }

    float turn, tilt, zoom; int recenter;
    padCameraInput(&turn, &tilt, &zoom, &recenter);
    int free = playerCamera();
    if (fc.blend <= 0) {   /* the game's camera: the free one waits where it is */
        fc.turn = fc.forced ? fc.startTurn * M_PI / 180 : 0;
        fc.pitch = fc.forced && fc.startPitch ? fc.startPitch * M_PI / 180 : gPitch;
        fc.zoom = fc.forced ? fc.startZoom : 1;
    }
    if (free) {
        if (recenter) fc.recentring = 1;
        if (turn || tilt || zoom) fc.recentring = 0;
        if (fc.recentring) {   /* back to the game's view, eased */
            double k = 1 - exp(-dt * 8);
            fc.turn -= fc.turn * k; fc.pitch += (gPitch - fc.pitch) * k; fc.zoom += (1 - fc.zoom) * k;
            if (fabs(fc.turn) < 0.002 && fabs(gPitch - fc.pitch) < 0.002 && fabs(1 - fc.zoom) < 0.002) fc.recentring = 0;
        }
        double was = fc.turn, lo, hi;
        int wasOpen = turnBand(gHeading, gPitch, was, fc.zoom, &lo, &hi);
        double wasWidth = hi - lo;
        fc.turn = wrap(fc.turn + turn * TURN_SPEED * dt);
        if (!turnBand(gHeading, gPitch, fc.turn, fc.zoom, &lo, &hi)) {
            if (!wasOpen && hi - lo > wasWidth) ;                               /* turning back out of a refused heading */
            else if (wasOpen || turn) fc.turn = was;                            /* the room refuses that heading */
            else fc.turn -= fc.turn * (1 - exp(-dt * 8));                       /* L1/R1 turned the game into one */
        }
        fc.zoom = clampd(fc.zoom * exp(zoom * ZOOM_SPEED * dt), ZOOM_MIN, ZOOM_MAX);
        turnBand(gHeading, gPitch, fc.turn, fc.zoom, &lo, &hi);
        /* tilting stays inside the band; a band that moved (a turn, a zoom) draws the pitch in, eased */
        double tilted = fc.pitch + tilt * TILT_SPEED * dt;
        if ((tilt > 0 && tilted <= hi) || (tilt < 0 && tilted >= lo)) fc.pitch = tilted;
        else if (tilt > 0 && fc.pitch < hi) fc.pitch = hi;
        else if (tilt < 0 && fc.pitch > lo) fc.pitch = lo;
        double k = 1 - exp(-dt * 10);
        if (fc.pitch < lo) fc.pitch += (lo - fc.pitch) * k;
        if (fc.pitch > hi) fc.pitch += (hi - fc.pitch) * k;
        fc.pitch = clampd(fc.pitch, PITCH_MIN, PITCH_MAX);
    }
    fc.blend = clampd(fc.blend + (free ? dt : -dt) / BLEND_TIME, 0, 1);
    if (fc.blend <= 0) return;

    double dist = gdist * fc.zoom;
    double heading = gHeading + fc.turn;
    double fx = dist * cos(fc.pitch) * cos(heading), fz = dist * cos(fc.pitch) * sin(heading), fy = -dist * sin(fc.pitch);
    double b = fc.blend * fc.blend * (3 - 2 * fc.blend);   /* smoothstep */
    eye->vx = at->vx + (int32_t)(gx + (fx - gx) * b);
    eye->vy = at->vy + (int32_t)(gy + (fy - gy) * b);
    eye->vz = at->vz + (int32_t)(gz + (fz - gz) * b);
    static int walls = -1;
    if (walls < 0) { const char* w = getenv("XG_FREECAM_WALLS"); walls = !(w && *w == '0'); }
    if (walls) wallClip(at, eye, dt);
    /* the view's heading, as the game derives it from eye and target (ratan2 - 0x400). The
     * sprites' facing and the compass read m10E; walking (OP_UPDATE_CHARACTER) reads m10C, the
     * heading of the camera's target pair, which the game recomputes after movement each frame,
     * so both are written here and the pad's directions follow the view on screen */
    double h = atan2((double)at->vz - eye->vz, (double)at->vx - eye->vx);
    fieldSceneState.m10E_cameraTan = (s16)(lround(h * 2048 / M_PI) - 0x400);
    fieldSceneState.m10C_camera2Tan = fieldSceneState.m10E_cameraTan;
}

/* ---- the world map (xg-ugy.5) -------------------------------------------------------------
 *
 * The same controls on the world map: the right stick turns (the world camera's own target
 * heading, which L1/R1 step and the game eases toward: walking follows it as it follows L1/R1),
 * tilts (pitch) and, with L2, zooms (distance); recentre puts tilt and zoom back. Tilt and zoom
 * only change what is drawn: the camera task's eye and target and the ground's view matrix are
 * built from copies (pcWorldCameraPitch, pcWorldCameraDistance), the game's own angles and
 * distance stay as they are. */
#define WORLD_TILT_MIN (-0.30)   /* radians from the game's pitch: toward the horizon */
#define WORLD_TILT_MAX (0.45)    /* steeper */
#define WORLD_ZOOM_MIN 0.5
#define WORLD_ZOOM_MAX 1.8
static struct { double tilt, zoom, turn; uint32_t vblank; int recentring; } wc = {0, 1, 0, 0, 0};

void pcWorldCameraFrame(void) {
    float turn, tilt, zoom; int recenter;
    uint32_t now = hwVSyncCount();
    double dt = wc.vblank && now - wc.vblank < 30 ? (now - wc.vblank) / 60.0 : 1 / 30.0;
    wc.vblank = now;
    wc.turn = 0;
    static int start = 1;
    if (start) {   /* XG_WORLDCAM=tilt,zoom (tests): degrees from the game's pitch, distance factor */
        const char* e = getenv("XG_WORLDCAM");
        double t = 0, z = 1;
        if (e && sscanf(e, "%lf,%lf", &t, &z) >= 1) { wc.tilt = t * M_PI / 180; wc.zoom = z; }
        start = 0;
    }
    if (!pcFreeCameraEnabled()) { wc.tilt = 0; wc.zoom = 1; return; }
    padCameraInput(&turn, &tilt, &zoom, &recenter);
    if (recenter) wc.recentring = 1;
    if (tilt || zoom) wc.recentring = 0;
    if (wc.recentring) {
        double k = 1 - exp(-dt * 8);
        wc.tilt -= wc.tilt * k; wc.zoom += (1 - wc.zoom) * k;
        if (fabs(wc.tilt) < 0.002 && fabs(1 - wc.zoom) < 0.002) { wc.tilt = 0; wc.zoom = 1; wc.recentring = 0; }
    }
    wc.turn = turn * TURN_SPEED * dt * 4096 / (2 * M_PI);
    wc.tilt = fmax(WORLD_TILT_MIN, fmin(WORLD_TILT_MAX, wc.tilt + tilt * TILT_SPEED * dt));
    wc.zoom = fmax(WORLD_ZOOM_MIN, fmin(WORLD_ZOOM_MAX, wc.zoom * exp(zoom * ZOOM_SPEED * dt)));
}

int pcWorldCameraTurn(void) { return (int)lround(wc.turn); }

int pcWorldCameraActive(void) { return fabs(wc.tilt) > 1e-4 || fabs(wc.zoom - 1) > 1e-4; }

/* the world map's pitch grows toward the horizon: tilting up (steeper, as in fields) lowers it */
s16 pcWorldCameraPitch(s16 game) { return (s16)(game - lround(wc.tilt * 4096 / (2 * M_PI))); }

s32 pcWorldCameraDistance(s32 game) { return (s32)(game * wc.zoom); }
