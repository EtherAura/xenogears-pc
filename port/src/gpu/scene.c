/*
 * A drawn scene's depth, and the depth of field and the scene lighting made
 * from it (docs/PORT_HD2D.md, bd xg-rgcf.5 and xg-rgcf.5.1). The HD-2D-style
 * Display look: effects over the picture the game already draws, in the
 * hidden display copy only. The 1x VRAM, the wide target and everything the
 * game can read stay exactly as they are; with the options off none of this
 * runs and no image is made.
 *
 * The game says which ordering tables are the scene (pcSceneDepth,
 * include/pc_scene.h: the Enhanced field, right before its DrawOTag). While
 * libgpu.c walks them:
 *   - each tag inside a table sets the depth of the packets that follow it:
 *     slot * zPerSlot view depth units (the field sorts by view depth / 16).
 *     gp0.c replaces it per vertex with the GTE's own view depth where the
 *     packet still holds what the GTE projected (every scene polygon of the
 *     rooms traced), so floors and walls get a smooth depth and only
 *     screen-space pieces fall back to their slot;
 *   - the copy's draws write the depth of each pixel they replace into a
 *     depth image (shaders/hires.frag DEPTH). A fragment that blends leaves
 *     the depth alone: shadows, glows, water and mist blur as what is behind
 *     them, and scenery the free camera fades does not blur the party;
 *   - at the tag after the scene (the field's UI table) the copy's frame is
 *     lit (shaders/light.comp) and then blurred by that depth (vk.c
 *     gvkSceneEnd, shaders/dof.comp), and only then are the dialogue windows,
 *     portraits, compass and faders drawn, by the same commands as without
 *     the effects.
 *
 * The amount comes from the relative depth u: behind the focus the thin
 * lens's 1 - focus / z (0 at the focus, 1 at infinity), in front the same
 * shape mirrored, z / focus - 1 (the lens's own grows without bound there).
 * Nothing blurs inside the focus range |u| <= band; beyond it the radius
 * rises to the chosen one over 0.25 more. The radius is in PS1 pixels, so
 * the look does not depend on the internal resolution, the window or the
 * aspect.
 *
 * Figures: a sprite's pixels are marked in the depth image, and by default
 * they stay in focus wherever they stand: the scenery softens around crisp
 * characters, the look the option is named for, and sprite art keeps its
 * pixels. What is in front of a figure still spreads over it, and its
 * shadow (blended) blurs with the ground.
 *
 * The lighting (shaders/light.comp has the parts). The field headers hold
 * a light in 36 of 730 rooms and the scenery is pre-lit, so nothing of it is
 * the room's own light: the top light is overhead in every room (where the
 * blob shadow under each character says the light is), with the room's up
 * direction from the view the game projects its scenery with (pcSceneView);
 * shade is occlusion from the depth image; bounce and glow come from what
 * the picture painted bright. Lengths are in PS1 pixels at the focus depth,
 * so the look does not depend on the internal resolution either. Figures get
 * no top light and no shade and are no source of light; a distant backdrop
 * is left as painted.
 *
 * Settings (settings.ini; XG_<KEY> overrides):
 *   depth_of_field=off        off (default), subtle, medium or strong: the
 *                             largest blur radius, 1.25, 2.5 or 4 PS1 pixels
 *   depth_of_field_focus=normal  how deep the sharp range around the focus
 *                             is: narrow, normal or wide
 *   depth_of_field_sprites=sharp  sharp (default): sprites stay in focus;
 *                             depth: a sprite blurs by where it stands, as
 *                             the scenery does (settings.ini only)
 *   lighting=off              off (default), subtle, medium or strong: the
 *                             top light, the shade and the bounce light
 *   glow=off                  off (default), subtle, medium or strong
 *   lighting_shade=on, lighting_bounce=on  off: lighting without that part
 *                             (settings.ini only)
 * The Enhanced engine only: the Original has no hook and never arms a scene.
 *
 * Env: XG_DOF_DEBUG=1: the blur amounts as colours instead of the blur (red
 * behind the focus, blue in front, the picture's own brightness in focus);
 * 2: the same inverted, on white (work/hd2d/verify.py tells the UI's opaque
 * pixels from the scene's by what is equal over both);
 * XG_DOF_BAND=<u>: the focus range as a number (1e9: nothing blurs, the
 * whole path still runs: the picture must equal the one with the option
 * off); XG_DOF_LOG=1: a line per scene; XG_LIGHT_GAIN=<x>: every lighting
 * strength times x (0: the lighting's whole path runs and must change
 * nothing); XG_LIGHT_DEBUG=1: the lighting's parts as colours (red shade,
 * green top light, blue glow and bounce); XG_SCENE_TEST_FAIL=1 and
 * XG_SCENE_TEST_FAIL_LIGHT=1 (vk.c, for the checks): the device cannot do any
 * of it, or has no room for the lighting's images.
 */

#include <math.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "common.h"
#include "psyq.h"

#include "gpu_internal.h"
#include "interpolation.h"
#include "pc_scene.h"
#include "pc_wide.h"
#include "system/engine.h"
#include "system/settings.h"

#define TABLES 2

static const char* const levelNames[] = {"off", "subtle", "medium", "strong"};
static const float levelRadius[] = {0, 1.25f, 2.5f, 4.0f};
static const char* const focusNames[] = {"narrow", "normal", "wide"};
static const float focusBand[] = {0.05f, 0.10f, 0.18f};
/* Lighting and glow by level (light.comp has the fixed shares). */
static const float lightTop[] = {0, 0.25f, 0.45f, 0.75f};
static const float lightShade[] = {0, 0.6f, 1.0f, 1.4f};
static const float lightBounce[] = {0, 0.6f, 1.0f, 1.5f};
static const float glowGain[] = {0, 0.6f, 1.0f, 1.5f};
/* The blur reaches its radius 0.25 of relative depth beyond the focus range. */
#define DOF_GAIN 4.0f

static struct {
    int loaded;
    int level, focus;
    int figures; /* sprites stay in focus */
    int debug, log;
    float bandOverride;
    int haveBand;
    int lighting, glow;       /* levels, 0 off */
    int shadePart, bouncePart;
    int lightDebug, haveLightGain;
    float lightGain;

    /* The view pcSceneView gave for the scene pcSceneDepth registers next. */
    int viewGiven, viewArmed;
    float up[3], screenZ, centerX, centerY;

    /* The scene pcSceneDepth registered, for the next walk. */
    int armed;
    const OTag* table[TABLES];
    int count[TABLES];
    int zPerSlot;
    const OTag* uiTag;
    int focusZ;
    int pictureSlot;

    int drawing;         /* the device took the scene: depth writes are on */
    const char* status;  /* why the last armed scene showed nothing, or NULL */
    const char* lightStatus; /* why it was not lit though it was drawn, or NULL */
    unsigned scenes;
} sc;

int sceneWalkOn;

/* A level setting: 0 off .. 3 strong; anything else is off, and says so. */
static int levelOf(const char* key) {
    const char* s = settingsGet(key);
    if (s == NULL || s[0] == '\0' || strcmp(s, "0") == 0) return 0;
    for (int i = 0; i < 4; i++) if (strcmp(s, levelNames[i]) == 0) return i;
    fprintf(stderr, "scene: %s '%s' unknown (off, subtle, medium, strong); using off\n", key, s);
    return 0;
}

static void load(void) {
    if (sc.loaded) return;
    sc.loaded = 1;
    settingsDescribe("depth_of_field", "depth_of_field: off (default), subtle, medium or strong: scenery away from the "
                                       "focus softens in fields (the Enhanced engine)");
    settingsDescribe("depth_of_field_focus", "depth_of_field_focus: narrow, normal (default) or wide: how deep the "
                                             "sharp range around the focus is");
    const char* s = settingsGet("depth_of_field");
    sc.level = 0;
    for (int i = 0; s != NULL && i < 4; i++) if (strcmp(s, levelNames[i]) == 0) sc.level = i;
    if (s != NULL && strcmp(s, "light") == 0) sc.level = 1; /* the same level's other name */
    if (s != NULL && s[0] != '\0' && sc.level == 0 && strcmp(s, "off") != 0 && strcmp(s, "0") != 0)
        fprintf(stderr, "scene: depth_of_field '%s' unknown (off, subtle, medium, strong); using off\n", s);
    s = settingsGet("depth_of_field_focus");
    sc.focus = 1;
    for (int i = 0; s != NULL && i < 3; i++) if (strcmp(s, focusNames[i]) == 0) sc.focus = i;
    settingsDescribe("depth_of_field_sprites", "depth_of_field_sprites: sharp (default): sprites stay in focus; "
                                               "depth: a sprite blurs by where it stands");
    s = settingsGet("depth_of_field_sprites");
    sc.figures = !(s != NULL && strcmp(s, "depth") == 0);
    const char* e = getenv("XG_DOF_DEBUG");
    sc.debug = e != NULL && e[0] == '1' ? 1 : e != NULL && e[0] == '2' ? 2 : 0;
    e = getenv("XG_DOF_LOG");
    sc.log = e != NULL && e[0] == '1';
    e = getenv("XG_DOF_BAND");
    if (e != NULL && e[0] != '\0') {
        sc.bandOverride = (float)atof(e);
        sc.haveBand = 1;
    }

    settingsDescribe("lighting", "lighting: off (default), subtle, medium or strong: light from above, shade in "
                                 "corners and light from bright spots on the scenery in fields (the Enhanced engine)");
    settingsDescribe("glow", "glow: off (default), subtle, medium or strong: a glow around bright areas in fields "
                             "(the Enhanced engine)");
    settingsDescribe("lighting_shade", "lighting_shade: on (default) or off: the shade in corners, as part of lighting");
    settingsDescribe("lighting_bounce", "lighting_bounce: on (default) or off: the light from bright spots, as part of lighting");
    sc.lighting = levelOf("lighting");
    sc.glow = levelOf("glow");
    s = settingsGet("lighting_shade");
    sc.shadePart = !(s != NULL && (strcmp(s, "off") == 0 || strcmp(s, "0") == 0));
    s = settingsGet("lighting_bounce");
    sc.bouncePart = !(s != NULL && (strcmp(s, "off") == 0 || strcmp(s, "0") == 0));
    e = getenv("XG_LIGHT_DEBUG");
    sc.lightDebug = e != NULL && e[0] == '1';
    e = getenv("XG_LIGHT_GAIN");
    if (e != NULL && e[0] != '\0') {
        sc.lightGain = (float)atof(e);
        sc.haveLightGain = 1;
    }
}

int sceneDof(void) {
    load();
    return engineThis() == ENGINE_ENHANCED ? sc.level : 0;
}

void sceneSetDof(int level) {
    load();
    sc.level = level < 0 ? 0 : level > 3 ? 3 : level;
    settingsSet("depth_of_field", levelNames[sc.level]);
    settingsSave();
    hiresPrecisionTable(); /* the vertices' view depths, from now on */
}

int sceneDofFocus(void) {
    load();
    return sc.focus;
}

void sceneSetDofFocus(int range) {
    load();
    sc.focus = range < 0 ? 0 : range > 2 ? 2 : range;
    settingsSet("depth_of_field_focus", focusNames[sc.focus]);
    settingsSave();
}

const char* sceneDofStatus(void) {
    return sceneDof() ? sc.status : NULL;
}

int sceneLighting(void) {
    load();
    return engineThis() == ENGINE_ENHANCED ? sc.lighting : 0;
}

int sceneGlow(void) {
    load();
    return engineThis() == ENGINE_ENHANCED ? sc.glow : 0;
}

static void setLevel(const char* key, int* held, int level) {
    load();
    *held = level < 0 ? 0 : level > 3 ? 3 : level;
    settingsSet(key, levelNames[*held]);
    settingsSave();
    hiresPrecisionTable(); /* the vertices' view depths, from now on */
}

void sceneSetLighting(int level) { setLevel("lighting", &sc.lighting, level); }
void sceneSetGlow(int level) { setLevel("glow", &sc.glow, level); }

const char* sceneLightingStatus(void) {
    if (!sceneLighting() && !sceneGlow()) return NULL;
    return sc.status != NULL ? sc.status : sc.lightStatus;
}

int sceneWantsCopy(void) { return sceneDof() != 0 || sceneLighting() != 0 || sceneGlow() != 0; }

static void params(SceneParams* p) {
    DofParams* d = &p->dof;
    d->focus = (float)sc.focusZ;
    d->band = sc.haveBand ? sc.bandOverride : focusBand[sc.focus];
    d->gain = DOF_GAIN;
    d->radius = sceneDof() ? levelRadius[sc.level] : 0;
    d->debug = sceneDof() ? sc.debug : 0;
    d->figures = sc.figures;

    LightParams* l = &p->light;
    memset(l, 0, sizeof(*l));
    int lighting = sceneLighting(), glow = sceneGlow();
    sc.lightStatus = NULL;
    if (!lighting && !glow) return;
    if (!sc.viewArmed || sc.screenZ <= 0) {
        sc.lightStatus = "the game gave no view for this scene";
        return;
    }
    float gain = sc.haveLightGain ? sc.lightGain : 1.0f;
    l->on = 1;
    l->all = sc.haveLightGain || sc.lightDebug;
    l->top = lightTop[lighting] * gain;
    l->shade = sc.shadePart ? lightShade[lighting] * gain : 0;
    l->bounce = sc.bouncePart ? lightBounce[lighting] * gain : 0;
    l->glow = glowGain[glow] * gain;
    memcpy(l->up, sc.up, sizeof(l->up));
    l->screenZ = sc.screenZ;
    l->centerX = sc.centerX;
    l->centerY = sc.centerY;
    l->focus = (float)sc.focusZ;
    l->debug = sc.lightDebug;
}

void pcSceneView(const short rotation[3][3], int screenZ, int centerX, int centerY) {
    sc.viewGiven = 0;
    if (rotation == NULL || screenZ <= 0) return;
    /* The room's up (its y axis points down, as the PS1's does) in view
     * space: minus the matrix's second column, made unit length (the
     * field's matrix is a rotation times 3). light.comp's top light keeps
     * the agreed response of 3 on its own. */
    float up[3] = {-(float)rotation[0][1], -(float)rotation[1][1], -(float)rotation[2][1]};
    float length = sqrtf(up[0] * up[0] + up[1] * up[1] + up[2] * up[2]);
    if (length <= 0) return;
    for (int i = 0; i < 3; i++) sc.up[i] = up[i] / length;
    sc.screenZ = (float)screenZ;
    sc.centerX = (float)centerX;
    sc.centerY = (float)centerY;
    sc.viewGiven = 1;
}

void pcSceneDepth(const void* mainOt, int mainCount, const void* groundOt, int groundCount, int zPerSlot,
                  const void* uiTag, int focusZ, int pictureSlot) {
    sc.armed = 0;
    /* The view is for this scene only: a scene registered without one is not lit. */
    sc.viewArmed = sc.viewGiven;
    sc.viewGiven = 0;
    if (!sceneWantsCopy() || gpuCapturing()) return;
    if (mainOt == NULL || mainCount <= 0 || uiTag == NULL || zPerSlot <= 0) return;
    sc.table[0] = mainOt;
    sc.count[0] = mainCount;
    sc.table[1] = groundOt;
    sc.count[1] = groundOt != NULL ? groundCount : 0;
    sc.zPerSlot = zPerSlot;
    sc.uiTag = uiTag;
    sc.focusZ = focusZ;
    sc.pictureSlot = pictureSlot;
    sc.armed = 1;
    sceneWalkOn = 1;
}

/* The walk starts: the device takes the scene, or the frame is drawn as
 * without the option. */
void sceneWalkBegin(void) {
    if (!sc.armed) {
        sceneWalkOn = 0;
        return;
    }
    sc.armed = 0;
    sc.drawing = 0;
    sc.status = NULL;
    if (sc.focusZ <= 0) {
        /* The look-at point is behind the view, or the game gave none (first-person look). */
        sc.status = "no focus point in this view";
    } else if (wideBlockReasons() & PC_WIDE_BLOCK_MOVIE) {
        sc.status = "a movie-backed scene";
    } else {
        VRect narrow, wide;
        int inset[2] = {0, 0};
        int rects = gp0SceneRects(&narrow, &wide, inset);
        if (rects == 0) {
            sc.status = "no frame";
        } else {
            gp0Flush(); /* what was drawn before the scene has no depth */
            if (gvkSceneBegin(narrow, rects == 2 ? &wide : NULL, inset[0], inset[1])) {
                sc.drawing = 1;
            } else {
                sc.status = gvkHiScale() == 0 ? "no display copy yet"
                                              : "not available on this device, with GPU ordering, or without memory for it";
            }
        }
    }
    if (!sc.drawing) {
        sceneWalkOn = 0;
        if (sc.log) gpuLog("scene: not drawn: %s", sc.status);
        return;
    }
    gp0SceneDepth(1, SCENE_Z_NONE);
}

static void finish(int reached) {
    if (!sc.drawing) return;
    sc.drawing = 0;
    sceneWalkOn = 0;
    gp0Flush(); /* the scene's last batch writes its depth before the blur reads it */
    gp0SceneDepth(0, SCENE_Z_NONE);
    SceneParams p;
    params(&p);
    /* A walk that never reached the UI tag drew something else: no effect. */
    gvkSceneEnd(reached ? &p : NULL);
    interpolationSceneEnd(reached ? &p : NULL);
    sc.scenes++;
    if (sc.log) {
        gpuLog("scene: %u focus %d band %.3f radius %.2f%s", sc.scenes, sc.focusZ, p.dof.band, p.dof.radius,
               reached ? "" : " (no UI tag: not blurred)");
        if (p.light.on)
            gpuLog("scene: %u light top %.2f shade %.2f bounce %.2f glow %.2f up %.3f %.3f %.3f screen %.0f", sc.scenes,
                   p.light.top, p.light.shade, p.light.bounce, p.light.glow, p.light.up[0], p.light.up[1], p.light.up[2],
                   p.light.screenZ);
    }
}

void sceneWalkTag(const void* tag) {
    if (!sc.drawing) return;
    const OTag* t = tag;
    if (t == sc.uiTag) {
        finish(1);
        return;
    }
    for (int k = 0; k < TABLES; k++) {
        /* A packet and the ordering tables can be separate C objects. */
        uintptr_t offset = (uintptr_t)t - (uintptr_t)sc.table[k];
        if (sc.table[k] != NULL && offset < (size_t)sc.count[k] * sizeof(OTag) && offset % sizeof(OTag) == 0) {
            int slot = (int)(offset / sizeof(OTag));
            /* The middle of the slot; slot 0 is the nearest and still a depth. */
            float z = k == 1 && slot == sc.pictureSlot ? SCENE_Z_SHARP : (float)slot * (float)sc.zPerSlot + 0.5f * (float)sc.zPerSlot;
            gp0SceneDepth(1, z);
            return;
        }
    }
}

void sceneWalkEnd(void) {
    finish(0);
    sceneWalkOn = 0;
}
