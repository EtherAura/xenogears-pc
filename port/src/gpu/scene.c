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
 * The shadows (docs/PORT_HD2D.md section 20): a sun 55 degrees up from the
 * left of the camera the room starts with, kept in room space from then on,
 * so it does not swing when the camera turns. The scenery's shadows are
 * marched through the depth image toward it; each character's sprite is laid
 * on the ground away from it from the point where the game lays its own
 * shadow (pcSceneFigure), as if the sprite turned to face the sun.
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
 *   shadows=off               off (default), subtle, medium or strong: cast
 *                             shadows from the room's sun (xg-rgcf.5.2)
 *   fog=off                   off (default), subtle, medium or strong: haze
 *                             past the focus, at most 35, 50 or 70% of a
 *                             pixel's colour (xg-rgcf.5.4)
 *   color_grade=off           off (default), subtle, medium or strong: the
 *                             warm and cool grade (xg-rgcf.5.4)
 * The Enhanced engine only: the Original has no hook and never arms a scene.
 *
 * Env: XG_DOF_DEBUG=1: the blur amounts as colours instead of the blur (red
 * behind the focus, blue in front, the picture's own brightness in focus);
 * 2: the same inverted, on white (work/hd2d/verify.py tells the UI's opaque
 * pixels from the scene's by what is equal over both);
 * XG_DOF_BAND=<u>: the focus range as a number (1e9: nothing blurs, the
 * whole path still runs: the picture must equal the one with the option
 * off); XG_DOF_RADIUS=<r>, XG_DOF_GAIN=<g>, XG_TILT_RADIUS=<r>: the
 * largest radius (PS1 pixels), its rise per unit of relative depth past the
 * range, and the tilt-shift's radius at the edges (when it is on), for
 * comparing looks; XG_LIGHTING_GAIN=<x>, XG_GLOW_GAIN=<x>, XG_SHAFTS_GAIN=<x>:
 * the lighting's top light, shade and bounce, the glow, the light shafts
 * times x (comparing looks); XG_DOF_LOG=1: a line per scene; XG_LIGHT_GAIN=<x>: every lighting
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
#include "outdoor_fields.h"
#include "pc_scene.h"
#include "pc_wide.h"
#include "system/engine.h"
#include "system/hw.h"
#include "system/settings.h"

#define TABLES 2

static const char* const levelNames[] = {"off", "subtle", "medium", "strong"};
/* The largest radius by level and the focus range by setting. Testing
 * at 9x: "a bit too strong still" (a step softer), then "still too blurry and
 * tall, looks like smeared vaseline": half the screen was half blurred. He
 * chose, from captures of Strong + Wide, the radius at 2/3 and the range
 * 0.18 -> 0.65 (only the far backdrop and what is right before the camera
 * soften), every level and range scaled the same way. */
static const float levelRadius[] = {0, 0.65f, 1.25f, 2.0f};
static const char* const focusNames[] = {"narrow", "normal", "wide"};
static const float focusBand[] = {0.18f, 0.36f, 0.65f};
/* Dialogue focus (xg-rgcf.5.18): while someone speaks the focus
 * moves to the speaker and the sharp range narrows to this share, easing over
 * DIALOGUE_EASE vblanks (half a second) each way. */
#define DIALOGUE_BAND 0.5f
#define DIALOGUE_EASE 30.0f
/* The tilt-shift's radius at the frame's edges per level (PS1 pixels; the
 * blur's sigma is half of it): Medium, from the prototypes
 * (xg-rgcf.5.14; its shape: tilt_shift.glsl), then, testing at 9x ("blur
 * stands out too much still, needs to be a smooth gradual transition"), a
 * third of it over a taller strip. */
static const float tiltRadius[] = {0, 1.5f, 3.0f, 5.0f};
/* Lighting and glow by level (light.comp has the fixed shares). Half what
 * was first chosen, the light shafts too (testing at 9x: "tone down the
 * lighting"; all three at half from captures,). */
static const float lightTop[] = {0, 0.125f, 0.225f, 0.375f};
static const float lightShade[] = {0, 0.3f, 0.5f, 0.7f};
static const float lightBounce[] = {0, 0.3f, 0.5f, 0.75f};
static const float glowGain[] = {0, 0.3f, 0.5f, 0.75f};
/* How dark a full shadow makes what it falls on, by level. */
static const float shadowDark[] = {0, 0.25f, 0.45f, 0.6f};
/* Fog (docs/PORT_HD2D.md section 27): the most it takes and how quickly it
 * thickens, Medium and Strong the prototype's "fog" and "fog, strong"; the
 * grade's amount, Medium the prototype's "warm/cool". */
static const float fogMost[] = {0, 0.35f, 0.5f, 0.7f};
static const float fogDensity[] = {0, 0.8f, 1.1f, 1.4f};
static const float gradeAmount[] = {0, 0.5f, 1.0f, 1.5f};
/* The vignette: the corners' darkening per level (xg-rgcf.5.13). */
static const float vignetteAmount[] = {0, 0.12f, 0.22f, 0.35f};
/* Light shafts: their strength per level (xg-rgcf.5.16; Medium was the
 * prototype that was chosen, light.comp's shares). Then half, as above, and then
 * 35% of that ("light shafts still too strong"; from captures at 60,
 * 35 and 15%,). */
static const float shaftAmount[] = {0, 0.105f, 0.175f, 0.2625f};
/* Cloud shadows (xg-rgcf.5.17): how much of the sun a cloud takes per level
 * (Medium the prototype that was chosen), a cloud's
 * size (the width of a 16:9 picture, CLOUD_PICTURE PS1 pixels, at the focus
 * distance when the room starts: "Large"), and their drift in cloud sizes a
 * second; it wraps after CLOUD_PERIOD of them, where light.comp's cover
 * repeats. */
static const float cloudDark[] = {0, 0.24f, 0.38f, 0.52f};
#define CLOUD_PICTURE 432.0f
#define CLOUD_DRIFT_ALONG 0.016
#define CLOUD_DRIFT_ACROSS 0.012
#define CLOUD_PERIOD 512.0
/* Flickering firelight (xg-rgcf.5.20): how deep per level of Lighting (or
 * of Glow, when Lighting is off), and its character: "Torch", from
 * the prototypes, about two flickers a second,
 * somewhat irregular. */
static const float flickerDepth[] = {0, 0.25f, 0.4f, 0.6f};
#define FLICKER_SPEED 1.8f
/* Footstep dust (xg-rgcf.5.23, the lingering "clouds", a step
 * paler than the ground, from in-game prototypes):
 * how strong per level of Particles; a puff's life (vblanks), its radius when
 * born and how much it covers at most; a step, the farthest a foot moves from
 * one scene to the next, and a run's speed, in characters' heights
 * (DUST_HEIGHT view units, as light.comp FIGURE_HEIGHT) and heights a vblank. */
static const float dustAmount[] = {0, 0.5f, 0.8f, 1.2f};
/* Sun glare (xg-rgcf.5.24): its strength per level (Medium the prototype that was chosen). */
static const float glareAmount[] = {0, 0.5f, 1.0f, 1.6f};
#define DUST_LIFE 60u
#define DUST_SIZE 0.5f
#define DUST_COVER 0.9f
#define DUST_HEIGHT 96.0f
#define DUST_STEP 0.32f
#define DUST_MATCH 0.6f
#define DUST_RUN 0.055f /* Fei's pace in the fields, measured: 0.063 */
#define FLICKER_IRREGULAR 0.6f
/* The focus a room starts with is not yet its own (a field's first frame
 * carries a default): a cloud's size follows the focus for this many vblanks
 * of a room (its fade-in), then stays. */
#define CLOUD_SETTLE 90u
/* The sun: this far above the ground, from the camera's left and a little
 * from beyond the scene, when the room starts. */
#define SUN_ELEVATION 55.0f
/* The blur reaches its radius 0.25 of relative depth beyond the focus range. */
#define DOF_GAIN 4.0f

static struct {
    int loaded;
    int level, focus;
    int figures; /* sprites stay in focus */
    int dialogue; /* dialogue focus on (depth_of_field_dialogue) */
    /* The speaker's depth given for the next scene, the scene's, the last
     * one seen (for the ease back), how far the focus is eased toward it
     * (0..1) and the vblank that was measured at. */
    int speakerGiven, speakerZ, speakerLast;
    float speakerBlend;
    uint32_t speakerVblank;
    int debug, log;
    float bandOverride;
    int haveBand;
    float radiusOverride, gainOverride, tiltOverride; /* XG_DOF_RADIUS, XG_DOF_GAIN, XG_TILT_RADIUS; < 0 none */
    float lightingTimes, glowTimes, shaftsTimes; /* XG_LIGHTING_GAIN, XG_GLOW_GAIN, XG_SHAFTS_GAIN (1: as set) */
    int lighting, glow, shadows, fog, grade; /* levels, 0 off */
    int particles, particleSize;             /* particles' level (0 off) and size (0 small .. 2 large) */
    int vignette;                            /* the vignette's level, 0 off */
    int tiltShift;                           /* the tilt-shift's level, 0 off */
    int shafts;                              /* light shafts' level, 0 off */
    int clouds;                              /* cloud shadows' level, 0 off */
    int firelightPart;                       /* lighting_firelight: fires flicker, as part of lighting */
    int gradeAreaPart;                       /* color_grade_area: the grade leans into the room's colour */
    int dustPart;                            /* particles_footsteps: footstep dust, as part of particles */
    int glare;                               /* sun glare's level, 0 off */
    float viewScale;                         /* view units per room unit (the field's matrix's scale) */
    int frameRows;                           /* the scene's frame's rows (its drawing area) */
    int shadePart, bouncePart;
    int characterPart; /* lighting_characters: the sprites take the lighting too */
    int lightDebug, haveLightGain;
    float lightGain;

    /* Focus where the view's centre is (PC_SCENE_FOCUS_CENTRE, first-person
     * look): the depth there, measured a few frames back and eased toward;
     * autoValid while such scenes follow one another. */
    int focusCentre, autoValid, lastFocus;
    float autoFocus;

    /* The place pcScenePlace gave for the scene pcSceneDepth registers next
     * (PC_SCENE_*), and the place of the scene being drawn. */
    int placeGiven, place;
    /* The view pcSceneView gave for the scene pcSceneDepth registers next. */
    int viewGiven, viewArmed;
    float up[3], screenZ, centerX, centerY;
    float rot[3][3];          /* room to view, unit length (the field's matrix over its scale) */
    float matrix[3][3], trans[3]; /* room to view as the GTE takes it (4096 = 1) */

    /* The shadows' sun in room space, set when the room changes. */
    int sunRoomId, sunSet;
    float sunRoom[3];
    /* The particles' box (view units), set from the focus when the room changes. */
    int particleRoomId, particleBoxSet;
    float particleBox;
    /* A cloud's size (view units), from the focus as the room starts. */
    int cloudRoomId, cloudSizeSet;
    uint32_t cloudRoomStart; /* the vblank the room started */
    float cloudSize;
    /* Ground points pcSceneFigure gave this frame (room space), and the vblank they belong to. */
    int figureCount;
    uint32_t figureVblank; /* the last point's */
    float figureRoom[SCENE_FIGURES][3];
    /* The points the scene pcSceneDepth registered took: those given since
     * the registration before (a frame's sprites are drawn before its table
     * is walked, a vblank or more apart in a 30 or 20 Hz room or a battle). */
    int sceneFigureCount;
    float sceneFigure[SCENE_FIGURES][3];

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
    /* Sprites blur by where they stand by default (xg-rgcf.5.18). */
    settingsDescribe("depth_of_field_sprites", "depth_of_field_sprites: depth (default): a sprite blurs by where it "
                                               "stands; sharp: sprites stay in focus");
    s = settingsGet("depth_of_field_sprites");
    sc.figures = s != NULL && strcmp(s, "sharp") == 0;
    settingsDescribe("depth_of_field_dialogue", "depth_of_field_dialogue: on (default) or off: while someone speaks, "
                                                "the depth of field focuses on the speaker and softens the room a little more");
    s = settingsGet("depth_of_field_dialogue");
    sc.dialogue = !(s != NULL && (strcmp(s, "off") == 0 || strcmp(s, "0") == 0));
    const char* e = getenv("XG_DOF_DEBUG");
    sc.debug = e != NULL && e[0] == '1' ? 1 : e != NULL && e[0] == '2' ? 2 : 0;
    e = getenv("XG_DOF_LOG");
    sc.log = e != NULL && e[0] == '1';
    e = getenv("XG_DOF_BAND");
    if (e != NULL && e[0] != '\0') {
        sc.bandOverride = (float)atof(e);
        sc.haveBand = 1;
    }
    e = getenv("XG_DOF_RADIUS");
    sc.radiusOverride = e != NULL && e[0] != '\0' ? (float)atof(e) : -1.0f;
    e = getenv("XG_DOF_GAIN");
    sc.gainOverride = e != NULL && e[0] != '\0' ? (float)atof(e) : -1.0f;
    e = getenv("XG_TILT_RADIUS");
    sc.tiltOverride = e != NULL && e[0] != '\0' ? (float)atof(e) : -1.0f;
    e = getenv("XG_LIGHTING_GAIN");
    sc.lightingTimes = e != NULL && e[0] != '\0' ? (float)atof(e) : 1.0f;
    e = getenv("XG_GLOW_GAIN");
    sc.glowTimes = e != NULL && e[0] != '\0' ? (float)atof(e) : 1.0f;
    e = getenv("XG_SHAFTS_GAIN");
    sc.shaftsTimes = e != NULL && e[0] != '\0' ? (float)atof(e) : 1.0f;

    settingsDescribe("lighting", "lighting: off (default), subtle, medium or strong: light from above, shade in "
                                 "corners and light from bright spots on the scenery in fields (the Enhanced engine)");
    settingsDescribe("glow", "glow: off (default), subtle, medium or strong: a glow around bright areas in fields "
                             "(the Enhanced engine)");
    settingsDescribe("lighting_shade", "lighting_shade: on (default) or off: the shade in corners, as part of lighting");
    settingsDescribe("lighting_bounce", "lighting_bounce: on (default) or off: the light from bright spots, as part of lighting");
    sc.lighting = levelOf("lighting");
    sc.glow = levelOf("glow");
    settingsDescribe("shadows", "shadows: off (default), subtle, medium or strong: shadows cast by the scenery and the "
                                "characters in fields, battles and on the world map, from a sun set per place (the "
                                "Enhanced engine)");
    sc.shadows = levelOf("shadows");
    settingsDescribe("fog", "fog: off (default), subtle, medium or strong: haze that thickens past the characters in "
                            "fields, toward the colour of the room's farthest scenery (the Enhanced engine)");
    sc.fog = levelOf("fog");
    settingsDescribe("color_grade", "color_grade: off (default), subtle, medium or strong: shadows a little cool and "
                                    "highlights a little warm in fields (the Enhanced engine)");
    sc.grade = levelOf("color_grade");
    settingsDescribe("sun_glare", "sun_glare: off (default), subtle, medium or strong: a soft warm glow and faint lens "
                                  "rings from the sun's side of the picture, outdoors by day where sky shows (the Enhanced engine)");
    sc.glare = levelOf("sun_glare");
    settingsDescribe("particles_footsteps", "particles_footsteps: on (default) or off: dust kicked up where characters "
                                            "walk and run on dusty ground, as part of particles");
    s = settingsGet("particles_footsteps");
    sc.dustPart = !(s != NULL && (strcmp(s, "off") == 0 || strcmp(s, "0") == 0));
    settingsDescribe("color_grade_area", "color_grade_area: on (default) or off: the colour grade also leans into the "
                                         "room's own colour (a sandy place warmer, a green one greener, a dark one bluer)");
    s = settingsGet("color_grade_area");
    sc.gradeAreaPart = !(s != NULL && (strcmp(s, "off") == 0 || strcmp(s, "0") == 0));
    settingsDescribe("particles", "particles: off (default), subtle, medium or strong: ambient particles in fields, "
                                  "battles and on the world map, dust where the place is lit and embers where it is dark "
                                  "(the Enhanced engine)");
    sc.particles = levelOf("particles");
    settingsDescribe("particle_size", "particle_size: small (default), medium or large: how big the ambient particles are");
    settingsDescribe("vignette", "vignette: off (default), subtle, medium or strong: the frame's edges a little darker in "
                                 "fields, battles and on the world map (the Enhanced engine)");
    sc.vignette = levelOf("vignette");
    settingsDescribe("tilt_shift", "tilt_shift: off (default), subtle, medium or strong: the picture blurred toward "
                                   "its edges, in fields and on the world map (the Enhanced engine)");
    sc.tiltShift = levelOf("tilt_shift");
    settingsDescribe("light_shafts", "light_shafts: off (default), subtle, medium or strong: light spilling in shafts "
                                     "from windows, openings and the sky, and soft sunbeams, in fields and on the "
                                     "world map (the Enhanced engine)");
    sc.shafts = levelOf("light_shafts");
    settingsDescribe("cloud_shadows", "cloud_shadows: off (default), subtle, medium or strong: the soft shadows of "
                                      "clouds drifting over the ground, on the world map and in fields outdoors (the "
                                      "Enhanced engine)");
    sc.clouds = levelOf("cloud_shadows");
    s = settingsGet("particle_size");
    sc.particleSize = s != NULL && strcmp(s, "medium") == 0 ? 1 : s != NULL && strcmp(s, "large") == 0 ? 2 : 0;
    s = settingsGet("lighting_shade");
    sc.shadePart = !(s != NULL && (strcmp(s, "off") == 0 || strcmp(s, "0") == 0));
    s = settingsGet("lighting_bounce");
    sc.bouncePart = !(s != NULL && (strcmp(s, "off") == 0 || strcmp(s, "0") == 0));
    settingsDescribe("lighting_firelight", "lighting_firelight: on (default) or off: fires, torches and lamps flicker, "
                                           "and so do the light they cast and their glow, as part of lighting and glow");
    s = settingsGet("lighting_firelight");
    sc.firelightPart = !(s != NULL && (strcmp(s, "off") == 0 || strcmp(s, "0") == 0));
    settingsDescribe("lighting_characters", "lighting_characters: on (default) or off: the characters take the lighting "
                                            "too (the sun on their sunward side, shade at their feet, glow), as part of lighting");
    s = settingsGet("lighting_characters");
    sc.characterPart = !(s != NULL && (strcmp(s, "off") == 0 || strcmp(s, "0") == 0));
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

int sceneDofDialogue(void) {
    load();
    return sc.dialogue;
}

void sceneSetDofDialogue(int on) {
    load();
    sc.dialogue = on != 0;
    settingsSet("depth_of_field_dialogue", sc.dialogue ? "on" : "off");
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

int sceneShadows(void) {
    load();
    return engineThis() == ENGINE_ENHANCED ? sc.shadows : 0;
}

void sceneSetLighting(int level) { setLevel("lighting", &sc.lighting, level); }

int sceneLightCharacters(void) {
    load();
    return sc.characterPart;
}

void sceneSetLightCharacters(int on) {
    load();
    sc.characterPart = on != 0;
    settingsSet("lighting_characters", sc.characterPart ? "on" : "off");
    settingsSave();
}
void sceneSetGlow(int level) { setLevel("glow", &sc.glow, level); }
void sceneSetShadows(int level) { setLevel("shadows", &sc.shadows, level); }

int sceneFog(void) {
    load();
    return engineThis() == ENGINE_ENHANCED ? sc.fog : 0;
}

int sceneGrade(void) {
    load();
    return engineThis() == ENGINE_ENHANCED ? sc.grade : 0;
}

void sceneSetFog(int level) { setLevel("fog", &sc.fog, level); }
void sceneSetGrade(int level) { setLevel("color_grade", &sc.grade, level); }

int sceneSunGlare(void) {
    load();
    return engineThis() == ENGINE_ENHANCED ? sc.glare : 0;
}

void sceneSetSunGlare(int level) { setLevel("sun_glare", &sc.glare, level); }

int sceneFootstepDust(void) {
    load();
    return sc.dustPart;
}

void sceneSetFootstepDust(int on) {
    load();
    sc.dustPart = on != 0;
    settingsSet("particles_footsteps", sc.dustPart ? "on" : "off");
    settingsSave();
}

int sceneGradeArea(void) {
    load();
    return sc.gradeAreaPart;
}

void sceneSetGradeArea(int on) {
    load();
    sc.gradeAreaPart = on != 0;
    settingsSet("color_grade_area", sc.gradeAreaPart ? "on" : "off");
    settingsSave();
}

int sceneParticles(void) {
    load();
    return engineThis() == ENGINE_ENHANCED ? sc.particles : 0;
}

void sceneSetParticles(int level) { setLevel("particles", &sc.particles, level); }

int sceneVignette(void) {
    load();
    return engineThis() == ENGINE_ENHANCED ? sc.vignette : 0;
}

void sceneSetVignette(int level) { setLevel("vignette", &sc.vignette, level); }

int sceneTiltShift(void) {
    load();
    return engineThis() == ENGINE_ENHANCED ? sc.tiltShift : 0;
}

void sceneSetTiltShift(int level) { setLevel("tilt_shift", &sc.tiltShift, level); }

int sceneLightShafts(void) {
    load();
    return engineThis() == ENGINE_ENHANCED ? sc.shafts : 0;
}

void sceneSetLightShafts(int level) { setLevel("light_shafts", &sc.shafts, level); }

int sceneCloudShadows(void) {
    load();
    return engineThis() == ENGINE_ENHANCED ? sc.clouds : 0;
}

void sceneSetCloudShadows(int level) { setLevel("cloud_shadows", &sc.clouds, level); }

int sceneFirelight(void) {
    load();
    return sc.firelightPart;
}

void sceneSetFirelight(int on) {
    load();
    sc.firelightPart = on != 0;
    settingsSet("lighting_firelight", sc.firelightPart ? "on" : "off");
    settingsSave();
}

int sceneParticleSize(void) {
    load();
    return sc.particleSize;
}

void sceneSetParticleSize(int size) {
    static const char* const names[] = {"small", "medium", "large"};
    load();
    sc.particleSize = size < 0 ? 0 : size > 2 ? 2 : size;
    settingsSet("particle_size", names[sc.particleSize]);
    settingsSave();
}

const char* sceneLightingStatus(void) {
    if (!sceneLighting() && !sceneGlow() && !sceneShadows() && !sceneFog() && !sceneGrade() && !sceneParticles() &&
        !sceneVignette())
        return NULL;
    return sc.status != NULL ? sc.status : sc.lightStatus;
}

int sceneWantsCopy(void) {
    return sceneDof() != 0 || sceneLighting() != 0 || sceneGlow() != 0 || sceneShadows() != 0 || sceneFog() != 0 ||
           sceneGrade() != 0 || sceneParticles() != 0 || sceneVignette() != 0 || sceneTiltShift() != 0 ||
           sceneLightShafts() != 0 || sceneCloudShadows() != 0 || sceneSunGlare() != 0;
}

static void normalize3(float v[3]) {
    float length = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    if (length > 0) for (int i = 0; i < 3; i++) v[i] /= length;
}

/* Footstep dust (xg-rgcf.5.23): the characters' feet (their ground points)
 * matched from one scene to the next by nearness, how far each has gone, and
 * the puffs dropped along the way: one a step when running, now and then at
 * a walk, fainter. A new room starts with none. */
static struct {
    int room, count;
    uint32_t vblank, rng;
    float foot[SCENE_FIGURES][3], travel[SCENE_FIGURES];
    struct { float room[3]; uint32_t born; float strength, seed; } puff[SCENE_DUST];
    int next;
    float fastest; /* the fastest foot this scene, heights a vblank (the log) */
} dust;

static float dustRandom(void) {
    dust.rng = dust.rng * 1664525u + 1013904223u;
    return (float)(dust.rng >> 8) / 16777216.0f;
}

static void dustStep(void) {
    uint32_t now = hwVSyncCount();
    if (dust.room != sc.sunRoomId) {
        memset(dust.puff, 0, sizeof(dust.puff));
        dust.count = 0;
        dust.room = sc.sunRoomId;
    }
    if (now - dust.vblank > 30u) dust.count = 0; /* after a pause the feet start again */
    float dt = dust.count > 0 && now > dust.vblank ? (float)(now - dust.vblank) : 1.0f;
    float height = DUST_HEIGHT / (sc.viewScale > 0 ? sc.viewScale : 1.0f); /* a character's height, room units */
    float step = DUST_STEP * height, match = DUST_MATCH * height;
    float travel[SCENE_FIGURES];
    dust.fastest = 0;
    for (int k = 0; k < sc.sceneFigureCount; k++) {
        const float* f = sc.sceneFigure[k];
        int best = -1;
        float bestD2 = match * match;
        for (int j = 0; j < dust.count; j++) {
            float dx = f[0] - dust.foot[j][0], dz = f[2] - dust.foot[j][2];
            float d2 = dx * dx + dz * dz;
            if (d2 < bestD2) { bestD2 = d2; best = j; }
        }
        float moved = best >= 0 ? sqrtf(bestD2) : 0, before = best >= 0 ? dust.travel[best] : 0;
        travel[k] = before + moved;
        float speed = moved / height / dt;
        if (speed > dust.fastest) dust.fastest = speed;
        if (moved < 0.01f * height || floorf(travel[k] / step) == floorf(before / step)) continue;
        float run = (speed - 0.25f * DUST_RUN) / (0.75f * DUST_RUN);
        run = run < 0 ? 0 : run > 1 ? 1 : run;
        if (run < 0.5f && dustRandom() > 0.5f) continue; /* at a walk, now and then */
        int i = dust.next;
        dust.next = (dust.next + 1) % SCENE_DUST;
        for (int c = 0; c < 3; c++) dust.puff[i].room[c] = f[c];
        dust.puff[i].room[0] += (dustRandom() - 0.5f) * 0.12f * height;
        dust.puff[i].room[2] += (dustRandom() - 0.5f) * 0.12f * height;
        dust.puff[i].born = now;
        dust.puff[i].strength = 0.3f + 0.7f * run;
        dust.puff[i].seed = dustRandom();
    }
    dust.count = sc.sceneFigureCount;
    memcpy(dust.foot, sc.sceneFigure, (size_t)dust.count * sizeof(dust.foot[0]));
    memcpy(dust.travel, travel, (size_t)dust.count * sizeof(float));
    dust.vblank = now;
}

static void params(SceneParams* p) {
    DofParams* d = &p->dof;
    d->focus = (float)sc.focusZ;
    d->band = sc.haveBand ? sc.bandOverride : focusBand[sc.focus];
    /* Dialogue focus: toward the speaker's depth, the sharp range narrowed,
     * eased (the lighting keeps the camera's focus: only the blur moves). */
    if (sc.speakerBlend > 0 && sc.speakerLast > 0 && !sc.focusCentre) {
        float k = sc.speakerBlend * sc.speakerBlend * (3.0f - 2.0f * sc.speakerBlend);
        d->focus += k * ((float)sc.speakerLast - d->focus);
        d->band *= 1.0f + k * (DIALOGUE_BAND - 1.0f);
    }
    d->gain = sc.gainOverride >= 0 ? sc.gainOverride : DOF_GAIN;
    d->radius = sceneDof() ? (sc.radiusOverride >= 0 ? sc.radiusOverride : levelRadius[sc.level]) : 0;
    d->debug = sceneDof() ? sc.debug : 0;
    d->figures = sc.figures;
    /* The tilt-shift: fields and the world map (xg-rgcf.5.14). */
    d->tilt = sc.place == PC_SCENE_FIELD || sc.place == PC_SCENE_WORLDMAP ? tiltRadius[sceneTiltShift()] : 0;
    if (d->tilt > 0 && sc.tiltOverride >= 0) d->tilt = sc.tiltOverride;
    /* The display shows its rows from the top of the frame (the game's
     * display and drawing environments start at the same row). */
    int shown = gpuDisplay()->h;
    d->tiltHidden = shown > 0 && sc.frameRows > shown ? sc.frameRows - shown : 0;

    LightParams* l = &p->light;
    memset(l, 0, sizeof(*l));
    int lighting = sceneLighting(), glow = sceneGlow(), shadows = sceneShadows(), fog = sceneFog(), grade = sceneGrade();
    int vignette = sceneVignette();
    /* Particles: fields, battles and the world map (xg-rgcf.5.11), not Battling. */
    int particles = sc.place == PC_SCENE_BATTLING ? 0 : sceneParticles();
    /* Light shafts: fields and the world map (xg-rgcf.5.16). */
    int shafts = sc.place == PC_SCENE_FIELD || sc.place == PC_SCENE_WORLDMAP ? sceneLightShafts() : 0;
    /* Cloud shadows: the world map, and the fields outdoors (xg-rgcf.5.17;
     * a field's room is its number, pcSceneView). */
    int outdoors = sc.place == PC_SCENE_WORLDMAP || (sc.place == PC_SCENE_FIELD && fieldOutdoors(sc.sunRoomId));
    int clouds = outdoors ? sceneCloudShadows() : 0;
    sc.lightStatus = NULL;
    /* Firelight: it flickers the bounce light and the glow, so it goes with
     * them, as deep as the stronger of the two is set. */
    int firelight = sceneFirelight() ? (lighting > glow ? lighting : glow) : 0;
    /* Only where it is dim: fields indoors or at night (not on the outdoor
     * list, which is by day) and battles. In daylight a warm, bright surface
     * (Lahan's roofs and sunlit walls on the world map) would pass for a
     * flame, and a fire's flicker would not show in the sun anyway. */
    if (sc.place == PC_SCENE_WORLDMAP || (sc.place == PC_SCENE_FIELD && fieldOutdoors(sc.sunRoomId))) firelight = 0;
    /* Sun glare: outdoors by day (the world map and the outdoor fields). */
    int glare = sc.place == PC_SCENE_WORLDMAP || (sc.place == PC_SCENE_FIELD && fieldOutdoors(sc.sunRoomId)) ? sceneSunGlare() : 0;
    if (!lighting && !glow && !shadows && !fog && !grade && !particles && !vignette && !shafts && !clouds && !glare) return;
    if (!sc.viewArmed || sc.screenZ <= 0) {
        sc.lightStatus = "the game gave no view for this scene";
        return;
    }
    float gain = sc.haveLightGain ? sc.lightGain : 1.0f;
    l->on = 1;
    l->all = sc.haveLightGain || sc.lightDebug;
    l->top = lightTop[lighting] * gain * sc.lightingTimes;
    l->shade = sc.shadePart ? lightShade[lighting] * gain * sc.lightingTimes : 0;
    l->bounce = sc.bouncePart ? lightBounce[lighting] * gain * sc.lightingTimes : 0;
    l->figureLight = lighting && sc.characterPart && l->top > 0;
    l->glow = glowGain[glow] * gain * sc.glowTimes;
    l->shadow = shadowDark[shadows] * gain;
    l->fogMost = fogMost[fog] * gain;
    l->fogDensity = fogDensity[fog];
    /* Battles: their flat backdrop shares the arena's farthest slot, so the
     * fog stops thickening at twice the focus distance (the backdrop takes
     * the far arena's haze, not a wall of it). The world map has its own
     * depth fog: this one is half as thick on top of it (xg-rgcf.5.3). */
    if (sc.place == PC_SCENE_BATTLE || sc.place == PC_SCENE_BATTLING) l->fogCap = 1.0f;
    if (sc.place == PC_SCENE_WORLDMAP) l->fogDensity *= 0.5f;
    l->grade = gradeAmount[grade] * gain;
    l->gradeArea = sc.gradeAreaPart ? gradeAmount[grade] * gain : 0;
    l->vignette = vignetteAmount[vignette];
    l->shafts = shaftAmount[shafts] * sc.shaftsTimes;
    l->shaftTime = (float)(hwVSyncCount() % 216000u) / 60.0f; /* seconds, over an hour that repeats */
    if (firelight) {
        l->flicker = flickerDepth[firelight];
        l->flickerTime = (float)(hwVSyncCount() % 216000u) / 60.0f;
        l->flickerSpeed = FLICKER_SPEED;
        l->flickerIrregular = FLICKER_IRREGULAR;
        memcpy(l->roomTrans, sc.trans, sizeof(l->roomTrans));
    }
    if (clouds) {
        /* A cloud's size from the focus as the room starts (CLOUD_SETTLE),
         * then kept, so a moving camera does not change it. */
        uint32_t now = hwVSyncCount();
        if (!sc.cloudSizeSet || sc.cloudRoomId != sc.sunRoomId) {
            sc.cloudRoomId = sc.sunRoomId;
            sc.cloudSizeSet = 1;
            sc.cloudRoomStart = now;
        }
        if (now - sc.cloudRoomStart < CLOUD_SETTLE)
            sc.cloudSize = CLOUD_PICTURE * (float)(sc.focusZ > 0 ? sc.focusZ : 3000) / sc.screenZ;
        double seconds = (double)now / 60.0;
        l->clouds = cloudDark[clouds] * gain;
        l->cloudSize = sc.cloudSize;
        l->cloudDrift[0] = (float)fmod(seconds * CLOUD_DRIFT_ALONG, CLOUD_PERIOD);
        l->cloudDrift[1] = (float)fmod(seconds * CLOUD_DRIFT_ACROSS, CLOUD_PERIOD);
        l->cloudFar = sc.place == PC_SCENE_FIELD; /* a field's far backdrop is a painting */
        memcpy(l->roomTrans, sc.trans, sizeof(l->roomTrans));
    }
    if (sc.sunSet) {
        /* The room's sun in this frame's view. */
        for (int i = 0; i < 3; i++)
            l->sun[i] = sc.rot[i][0] * sc.sunRoom[0] + sc.rot[i][1] * sc.sunRoom[1] + sc.rot[i][2] * sc.sunRoom[2];
        normalize3(l->sun);
    } else {
        memcpy(l->sun, sc.up, sizeof(l->sun));
    }
    /* This scene's actors' ground points, in view space. */
    for (int k = 0; k < sc.sceneFigureCount; k++) {
        const float* r = sc.sceneFigure[k];
        for (int i = 0; i < 3; i++)
            l->figure[k][i] = (sc.matrix[i][0] * r[0] + sc.matrix[i][1] * r[1] + sc.matrix[i][2] * r[2]) / 4096.0f + sc.trans[i];
    }
    l->figures = sc.sceneFigureCount;
    l->glare = glareAmount[glare];
    l->dustSize = DUST_SIZE;
    l->dustCover = DUST_COVER;
    /* Footstep dust: the live puffs, in this frame's view, at the level of Particles. */
    l->dustCount = 0;
    int dustLevel = sceneFootstepDust() ? particles : 0;
    for (int i = 0; dustLevel && i < SCENE_DUST; i++) {
        uint32_t age = hwVSyncCount() - dust.puff[i].born;
        if (dust.puff[i].strength <= 0 || age >= DUST_LIFE) continue;
        const float* r = dust.puff[i].room;
        float* q = l->dust[l->dustCount++];
        for (int c = 0; c < 3; c++)
            q[c] = (sc.matrix[c][0] * r[0] + sc.matrix[c][1] * r[1] + sc.matrix[c][2] * r[2]) / 4096.0f + sc.trans[c];
        q[3] = (float)age / (float)DUST_LIFE;
        q[4] = dust.puff[i].strength * dustAmount[dustLevel];
        q[5] = dust.puff[i].seed;
    }
    if (particles) {
        /* The box they wrap in: from the focus when the room starts, then
         * kept, so a moving camera does not stretch it. */
        if (!sc.particleBoxSet || sc.particleRoomId != sc.sunRoomId) {
            sc.particleRoomId = sc.sunRoomId;
            sc.particleBoxSet = 1;
            sc.particleBox = 2.5f * (float)(sc.focusZ > 0 ? sc.focusZ : 3000);
        }
        static const float sizes[] = {0.45f, 0.75f, 1.1f};
        l->particles = particles;
        l->particleSize = sizes[sc.particleSize];
        /* Seconds, over an hour that repeats (a float keeps them fine). */
        l->particleTime = (float)(hwVSyncCount() % 216000u) / 60.0f;
        l->particleBox = sc.particleBox;
        memcpy(l->roomRot, sc.rot, sizeof(l->roomRot));
        memcpy(l->roomTrans, sc.trans, sizeof(l->roomTrans));
        l->blurBand = d->band;
        l->blurGain = d->gain;
        l->blurRadius = d->radius;
        l->blurTilt = d->tilt;
        l->blurTiltHidden = d->tiltHidden;
    }
    memcpy(l->up, sc.up, sizeof(l->up));
    l->screenZ = sc.screenZ;
    l->centerX = sc.centerX;
    l->centerY = sc.centerY;
    l->focus = (float)sc.focusZ;
    l->debug = sc.lightDebug;
}

void sceneFocusMeasured(float z) {
    /* One read after the view went back to the game's belongs to no scene. */
    if (!(z > 0) || !sc.autoValid) return;
    /* Eased, as a camera's focus moves: a third of the way each time. */
    sc.autoFocus += (z - sc.autoFocus) / 3.0f;
}

void pcScenePlace(int place) { sc.placeGiven = place; }

void pcSceneSpeaker(int focusZ) { sc.speakerGiven = focusZ > 0 ? focusZ : 0; }

/* The dialogue's ease toward the speaker, measured once per scene: toward 1
 * while someone speaks (and dialogue focus is on), back toward 0 after, by
 * the vblanks passed (a smoothstep of it is used). */
static void speakerEase(void) {
    uint32_t now = hwVSyncCount();
    float passed = (float)(now - sc.speakerVblank);
    sc.speakerVblank = now;
    if (passed > DIALOGUE_EASE || passed < 0) passed = DIALOGUE_EASE; /* a scene after a pause starts settled */
    int on = sc.dialogue && sc.speakerZ > 0 && sc.place == PC_SCENE_FIELD;
    if (on) sc.speakerLast = sc.speakerZ;
    float step = passed / DIALOGUE_EASE;
    sc.speakerBlend = on ? fminf(sc.speakerBlend + step, 1.0f) : fmaxf(sc.speakerBlend - step, 0.0f);
    if (sc.speakerLast <= 0) sc.speakerBlend = 0;
}

/* The longest a frame takes, in vblanks (a 15 Hz room): older points are stale. */
#define FIGURE_STALE 4u

void pcSceneFigure(int x, int y, int z) {
    uint32_t vblank = hwVSyncCount();
    /* Points given long before (no scene registered since: a menu, a
     * movie) are not this frame's. */
    if (vblank - sc.figureVblank > FIGURE_STALE) sc.figureCount = 0;
    sc.figureVblank = vblank;
    if (sc.figureCount >= SCENE_FIGURES) return;
    for (int k = 0; k < sc.figureCount; k++)
        if (sc.figureRoom[k][0] == (float)x && sc.figureRoom[k][1] == (float)y && sc.figureRoom[k][2] == (float)z) return;
    sc.figureRoom[sc.figureCount][0] = (float)x;
    sc.figureRoom[sc.figureCount][1] = (float)y;
    sc.figureRoom[sc.figureCount][2] = (float)z;
    sc.figureCount++;
}

/* The room's sun from this view: SUN_ELEVATION above the ground, from the
 * camera's left and a little from beyond the scene, kept in room space. */
static void setSun(void) {
    float target[3] = {-0.7071f, 0.0f, 0.7071f}; /* left and away from the camera, in view space */
    float d = target[0] * sc.up[0] + target[1] * sc.up[1] + target[2] * sc.up[2];
    float h[3] = {target[0] - d * sc.up[0], target[1] - d * sc.up[1], target[2] - d * sc.up[2]};
    normalize3(h);
    float e = SUN_ELEVATION * 3.14159265f / 180.0f;
    float sun[3];
    for (int i = 0; i < 3; i++) sun[i] = sc.up[i] * sinf(e) + h[i] * cosf(e);
    /* View to room: the rotation's transpose. */
    for (int j = 0; j < 3; j++) sc.sunRoom[j] = sc.rot[0][j] * sun[0] + sc.rot[1][j] * sun[1] + sc.rot[2][j] * sun[2];
    normalize3(sc.sunRoom);
    sc.sunSet = 1;
}

void pcSceneView(const short rotation[3][3], const int translation[3], int screenZ, int centerX, int centerY, int room) {
    sc.viewGiven = 0;
    if (rotation == NULL || translation == NULL || screenZ <= 0) return;
    /* The room's up (its y axis points down, as the PS1's does) in view
     * space: minus the matrix's second column, made unit length (the
     * field's matrix is a rotation times 3). light.comp's top light keeps
     * the agreed response of 3 on its own. */
    float up[3] = {-(float)rotation[0][1], -(float)rotation[1][1], -(float)rotation[2][1]};
    float length = sqrtf(up[0] * up[0] + up[1] * up[1] + up[2] * up[2]);
    if (length <= 0) return;
    for (int i = 0; i < 3; i++) sc.up[i] = up[i] / length;
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) {
            sc.matrix[i][j] = (float)rotation[i][j];
            sc.rot[i][j] = (float)rotation[i][j] / length; /* the columns share the scale */
        }
        sc.trans[i] = (float)translation[i];
    }
    sc.viewScale = length / 4096.0f;
    sc.screenZ = (float)screenZ;
    sc.centerX = (float)centerX;
    sc.centerY = (float)centerY;
    sc.viewGiven = 1;
    if (!sc.sunSet || room != sc.sunRoomId) {
        sc.sunRoomId = room;
        setSun();
    }
}

void pcSceneDepth(const void* mainOt, int mainCount, const void* groundOt, int groundCount, int zPerSlot,
                  const void* uiTag, int focusZ, int pictureSlot) {
    sc.armed = 0;
    /* The actors' points given since the last registration are this scene's. */
    sc.sceneFigureCount = hwVSyncCount() - sc.figureVblank > FIGURE_STALE ? 0 : sc.figureCount;
    memcpy(sc.sceneFigure, sc.figureRoom, (size_t)sc.sceneFigureCount * sizeof(sc.sceneFigure[0]));
    sc.figureCount = 0;
    sc.place = sc.placeGiven; /* for this scene only, as the view */
    sc.placeGiven = PC_SCENE_FIELD;
    sc.speakerZ = sc.speakerGiven; /* for this scene only too */
    sc.speakerGiven = 0;
    if (sceneFootstepDust() && sceneParticles()) dustStep();
    /* The view is for this scene only: a scene registered without one is not lit. */
    sc.viewArmed = sc.viewGiven;
    sc.viewGiven = 0;
    if (!sceneWantsCopy() || gpuCapturing()) return;
    if (mainOt == NULL || mainCount <= 0 || uiTag == NULL || zPerSlot <= 0) return;
    sc.focusCentre = focusZ == PC_SCENE_FOCUS_CENTRE;
    if (sc.focusCentre) {
        /* From where the game's view last focused, eased to what the centre holds. */
        if (!sc.autoValid) sc.autoFocus = sc.lastFocus > 0 ? (float)sc.lastFocus : 3000.0f;
        sc.autoValid = 1;
        focusZ = (int)(sc.autoFocus + 0.5f);
    } else {
        sc.autoValid = 0;
        if (focusZ > 0) sc.lastFocus = focusZ;
    }
    sc.table[0] = mainOt;
    sc.count[0] = mainCount;
    sc.table[1] = groundOt;
    sc.count[1] = groundOt != NULL ? groundCount : 0;
    sc.zPerSlot = zPerSlot;
    sc.uiTag = uiTag;
    sc.focusZ = focusZ;
    speakerEase();
    sc.pictureSlot = pictureSlot;
    sc.armed = 1;
    sceneWalkOn = 1;
}

/* The walk starts: the device takes the scene, or the frame is drawn as
 * without the option. */
/* A walk DrawOTagEnv starts at its draw environment's packet (libgpu.c): the
 * scene's frame is the area that packet sets, so the scene starts at the next
 * tag, once it is drawn (Battling: its two display buffers alternate, and the
 * area before the packet is the other buffer's). */
const void* sceneWalkEnvTag;
static const void* walkEnvTag;
static int walkPending;

static void walkStart(void);

void sceneWalkBegin(void) {
    if (!sc.armed) {
        sceneWalkOn = 0;
        return;
    }
    sc.armed = 0;
    sc.drawing = 0;
    sc.status = NULL;
    walkEnvTag = sceneWalkEnvTag;
    walkPending = walkEnvTag != NULL;
    if (!walkPending) walkStart();
}

static void walkStart(void) {
    walkPending = 0;
    if (sc.focusZ <= 0) {
        /* The look-at point is behind the view, or the game gave none. */
        sc.status = "no focus point in this view";
    } else if (wideBlockReasons() & PC_WIDE_BLOCK_MOVIE) {
        sc.status = "a movie-backed scene";
    } else {
        VRect narrow, wide;
        int inset[2] = {0, 0};
        int rects = gp0SceneRects(&narrow, &wide, inset);
        sc.frameRows = rects ? narrow.y1 - narrow.y0 + 1 : 0;
        if (rects == 0) {
            sc.status = "no frame";
        } else {
            gp0Flush(); /* what was drawn before the scene has no depth */
            if (gvkSceneBegin(narrow, rects == 2 ? &wide : NULL, inset[0], inset[1])) {
                sc.drawing = 1;
            } else {
                sc.status = gvkScenePreparing() ? "being prepared (shaders compiling)"
                            : gvkHiScale() == 0 ? "no display copy yet"
                                                : "not available on this device, or without memory for it";
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
    p.measureFocus = sc.focusCentre;
    p.centreX = sc.centerX;
    p.centreY = sc.centerY;
    /* A walk that never reached the UI tag drew something else: no effect. */
    gvkSceneEnd(reached ? &p : NULL);
    interpolationSceneEnd(reached ? &p : NULL);
    sc.scenes++;
    if (sc.log) {
        gpuLog("scene: %u focus %d%s band %.3f radius %.2f%s", sc.scenes, sc.focusZ, sc.focusCentre ? " (centre)" : "",
               p.dof.band, p.dof.radius, reached ? "" : " (no UI tag: not blurred)");
        if (sc.speakerBlend > 0)
            gpuLog("scene: %u speaker %d blend %.2f focus %.0f", sc.scenes, sc.speakerLast, sc.speakerBlend, p.dof.focus);
        if (p.dof.tilt > 0) gpuLog("scene: %u tilt-shift %.1f, %d rows not shown", sc.scenes, p.dof.tilt, p.dof.tiltHidden);
        if (p.light.on)
            gpuLog("scene: %u light top %.2f shade %.2f bounce %.2f glow %.2f fog %.2f grade %.2f up %.3f %.3f %.3f screen %.0f figures %d",
                   sc.scenes, p.light.top, p.light.shade, p.light.bounce, p.light.glow, p.light.fogMost, p.light.grade,
                   p.light.up[0], p.light.up[1], p.light.up[2], p.light.screenZ, p.light.figures);
        if (p.light.shafts > 0) gpuLog("scene: %u light shafts %.2f time %.2f", sc.scenes, p.light.shafts, p.light.shaftTime);
        if (sceneFootstepDust() && sceneParticles())
            gpuLog("scene: %u footstep dust %d puffs, %d feet, fastest %.4f, first %.0f %.0f %.0f, scale %.2f", sc.scenes,
                   p.light.dustCount, dust.count, dust.fastest, dust.count ? dust.foot[0][0] : 0.0, dust.count ? dust.foot[0][1] : 0.0,
                   dust.count ? dust.foot[0][2] : 0.0, sc.viewScale);
        if (sceneFootstepDust() && p.light.dustCount > 0) {
            const float* q = p.light.dust[0];
            gpuLog("scene: %u puff 0 view %.0f %.0f %.0f age %.2f strength %.2f screen %.1f %.1f (centre %.0f %.0f, screenZ %.0f)",
                   sc.scenes, q[0], q[1], q[2], q[3], q[4], q[2] > 0 ? q[0] * sc.screenZ / q[2] + sc.centerX : -1.0,
                   q[2] > 0 ? q[1] * sc.screenZ / q[2] + sc.centerY : -1.0, sc.centerX, sc.centerY, sc.screenZ);
        }
        if (p.light.clouds > 0)
            gpuLog("scene: %u cloud shadows %.2f size %.0f drift %.3f %.3f", sc.scenes, p.light.clouds, p.light.cloudSize,
                   p.light.cloudDrift[0], p.light.cloudDrift[1]);
        if (p.light.particles > 0)
            gpuLog("scene: %u particles %d size %.2f time %.2f box %.0f blur %.3f %.2f %.2f", sc.scenes, p.light.particles,
                   p.light.particleSize, p.light.particleTime, p.light.particleBox, p.light.blurBand, p.light.blurGain,
                   p.light.blurRadius);
    }
}

void sceneWalkTag(const void* tag) {
    if (walkPending) {
        if (tag == walkEnvTag) return; /* its packet, the area, is written after this */
        walkStart();
    }
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
    if (walkPending) {
        walkPending = 0;
        sceneWalkOn = 0; /* a walk of the environment's packet alone: no scene in it */
        return;
    }
    finish(0);
    sceneWalkOn = 0;
}
