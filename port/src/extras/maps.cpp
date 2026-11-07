// The map viewer (xg-htn.5): fields and the world map, loaded and drawn by
// the game's own code in a sandbox (mapfield.h, mapworld.h).
//
// Fields: the list on the left (the game's own place names, tables.h); the
// scene on the right, by the game's camera (its 320 x 224 screen, zoom and
// pan as any picture) or a free one (orbit, pan and dolly; the game's camera
// code from another eye), with the walkmesh, trigger zones, exits and the
// actors' places drawn over it. Below: the actors, the walkmesh's materials,
// the exits found in the scripts, the start (story progress, entry point)
// and the exports.
//
// The world map: its story stages and special configurations (the list);
// a free camera over the terrain as the world map draws it (streamed in
// around the camera's target), with the exits and landmarks marked, and the
// whole area's top-down map (an overview, clicked to fly there, and a PNG).
//
// Free camera: drag to orbit, right or middle drag (or Shift+drag) to pan,
// the wheel to come closer; W A S D Q E move (Shift: faster) while the mouse
// is over the scene; the gamepad's right stick orbits, its triggers come
// closer. A double click goes back to the game's camera.
//
// Script commands (XG_EXTRAS_SCRIPT "do ..."):
//   field <n>             load field n (0-729)
//   camera game|free      the game's camera or the free one
//   orbit <yaw> <pitch> <distance>   the free camera around its target (degrees, world units)
//   target <x> <y> <z>    the free camera's target
//   layers <hex>          XG_FIELD_* bits
//   select <n>            an actor (-1 none)
//   tab <name>            view actors walkmesh exits start assets export
//   follow archive|textures   open this field's source file in Browser
//   media                 list reachable movie/picture requests and availability
//   follow media <n>      open a verified request in Gallery
//   follow section <n>|model|sprite|music  decoded section or selected actor's asset
//   progress <n>|game|initial   the story progress to start from (then reloads)
//   entry <n>             the entry point to start from (-1 the state's)
//   export png|glb|walkmesh   export what is shown
//   info                  the loaded field (or world map) on stderr
//   sweep <from> <to>     load every field in the range, one a frame (log)
//   world <stage>         the world map's configuration for that stage (fields: "fields")
//   page <-1|1>           the list's other tab, Fields or World map (L1/R1, visuals.cpp)
//   worldcam <yaw> <pitch> <distance>   its camera around its target (degrees, world units)
//   worldtarget <x> <z>   its camera's target
//   worldlayers <hex>     XG_WORLD_* bits
//   worldanimate <0|1>    pause/play the animated terrain tiles
//   overview              the whole area top-down (in the view)
//   export worldmap <px>  the whole area top-down as PNG, px pixels a chunk (default 256)
//   export worldpng       the world map camera's picture

#include <SDL3/SDL.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <memory>
#include <string>
#include <vector>

#include "imgui.h"
#include "imgui_internal.h"
#include "picker.h"
#include "ui/skin.h"
#include "view.h"
#include "ui/l10n.h"
#include "viewer.h"
#include "replace.h"
#include "bookmark_source.h"
#include "export_provenance.h"
#include "gallery.h"
#include "files.h"

extern "C" {
#include "cd/cd.h"
#include "disc.h"
#include "field_assets.h"
#include "mapfield.h"
#include "maprast.h"
#include "mapworld.h"
#include "tables.h"
#include "writers.h"
}

// tables.c (xg-htn.9) is another agent's: reached through weak references,
// so the viewer works (with numbers only) in a build without it.
extern "C" __attribute__((weak)) const char* xgFieldName(int field);
extern "C" __attribute__((weak)) int xgStoryChapterCount(void);
extern "C" __attribute__((weak)) const char* xgStoryChapterName(int chapter);
extern "C" __attribute__((weak)) int xgStoryChapterStart(int chapter);
extern "C" __attribute__((weak)) const char* xgWorldPlaceName(int place);

namespace {

using namespace extras;

// The packs a map can show (the Enhanced look): the field's textures (the texture pack),
// its NPCs' and the party's cels (the sprite library; no poses: several actors at once).
const unsigned mapLibraries = XG_REPLACE_PACK | XG_REPLACE_CELS;

enum Tab { TAB_VIEW, TAB_ACTORS, TAB_WALKMESH, TAB_EXITS, TAB_START, TAB_ASSETS, TAB_EXPORT, TAB_COUNT };
const char* const tabIds[TAB_COUNT] = {"view", "actors", "walkmesh", "exits", "start", "assets", "export"};
const char* const tabNames[TAB_COUNT] = {N_("View"), N_("Actors"), N_("Walkmesh"), N_("Exits"), N_("Start"), N_("Assets"),
                                          N_("Export")}; /* tr() where shown */

enum StartFrom { START_INITIAL, START_GAME, START_VALUE };

// The field's renderers take camera depths below 32768 (3 x 10922 field
// units, the field's scale): farther, polygons drop out.
#define MAX_DISTANCE 10000.0f

struct Orbit {
    float at[3];
    float yaw, pitch, distance; // radians, world units
    float fov;                  // degrees, 0: the game's
};

// The world map's ground is drawn to 0xF00 deep: a camera farther from its
// target sees little.
#define WORLD_MAX_DISTANCE 3600.0f
#define OVERVIEW_PX 32 // overview pixels a chunk (a 16 x 16 map: 512 x 512 in VRAM)

struct State {
    int mode = 0; // 0 fields, 1 the world map
    // The focused view (viewer.h): the field or world map across the area,
    // its details pane beside it on request.
    bool focused = false, focusDetails = false, focusWanted = false;
    XgDisc* disc = nullptr; // disc 1 (the fields are there)
    XgField* field = nullptr;
    int fieldId = -1;
    std::vector<XgFieldMedia> media;
    std::vector<std::string> mediaProblems;
    int mediaField = -1;
    int cursor = 0;
    bool freeCam = false;
    bool animate = true;
    unsigned layers = XG_FIELD_MODELS | XG_FIELD_SPRITES | XG_FIELD_BACKGROUND;
    std::unique_ptr<View> view;
    Orbit orbit = {}, gameOrbit = {};
    int selected = -1;
    uint64_t lastTick = 0;
    bool redraw = true;
    int freeW = 640, freeH = 480; // the free camera's screen: the pane's shape, at most 1024 across
    std::string why;
    int tab = TAB_VIEW, wantTab = -1;
    // the start
    int startFrom = START_INITIAL;
    int progress = 0;
    int entry = -1;
    std::vector<uint8_t> gameState; // the paused game's, taken when the extras opened
    // exports
    int pictureScale = 4;
    bool exportModels = true, exportWalkmesh = true, exportTriggers = true, exportActors = true;
    // the world map
    XgWorld* world = nullptr;
    int worldStage = 0;
    std::vector<int> stages; // story progress from which each configuration is used
    std::string worldWhy;
    Orbit worldOrbit = {}, worldHome = {};
    unsigned worldLayers = XG_WORLD_GROUND | XG_WORLD_LANDMARKS | XG_WORLD_TREES | XG_WORLD_SKY | XG_WORLD_FOG |
                           XG_WORLD_EXITS;
    bool overview = false;      // the view shows the overview
    std::vector<uint16_t> overviewVram; // 1024 x 512, the overview as 15-bit pixels
    unsigned overviewVersion = 0;
    int overviewW = 0, overviewH = 0;
    bool worldRedraw = true;
    bool worldAnimate = true;
    uint64_t worldTick = 0;
    int worldPx = 256;          // export: pixels a chunk
    int worldExit = -1;         // the exit chosen (Exits tab)
    // tests
    int sweepNext = -1, sweepEnd = -1;
};

State* S;

// The view fits its scene in whole window pixels per pixel when it can
// (pictures stay crisp); a scene here fills its pane instead.
bool fillPending = true;
void refit() {
    if (S->view != nullptr) {
        S->view->fit();
    }
    fillPending = true;
}
void fill(ImVec2 avail, float sw, float sh, bool always) {
    if ((always || fillPending) && sw > 0 && sh > 0 && avail.x > 0 && avail.y > 0) {
        S->view->setZoom(fminf(avail.x / sw, avail.y / sh));
        fillPending = false;
    }
}

// ---- names ------------------------------------------------------------------------------------

std::string fieldLabel(int id) {
    char buf[96];
    const char* name = xgFieldName != nullptr ? xgFieldName(id) : nullptr;
    if (name != nullptr && name[0] != '\0') {
        snprintf(buf, sizeof(buf), "%03d %s", id, name);
    } else {
        snprintf(buf, sizeof(buf), "%03d", id);
    }
    return buf;
}

// ---- the free camera ----------------------------------------------------------------------------

// World y is down: the eye sits above the target (negative y) at positive pitch.
void eyeOf(const Orbit& o, float eye[3]) {
    float c = cosf(o.pitch);
    eye[0] = o.at[0] - sinf(o.yaw) * c * o.distance;
    eye[1] = o.at[1] - sinf(o.pitch) * o.distance;
    eye[2] = o.at[2] - cosf(o.yaw) * c * o.distance;
}

Orbit orbitFrom(const int eye[3], const int at[3]) {
    Orbit o = {};
    float d[3] = {(float)(at[0] - eye[0]), (float)(at[1] - eye[1]), (float)(at[2] - eye[2])};
    o.distance = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
    if (o.distance < 1) {
        o.distance = 1000;
        d[2] = 1;
    }
    o.yaw = atan2f(d[0], d[2]);
    o.pitch = asinf(fmaxf(-1.0f, fminf(1.0f, d[1] / o.distance)));
    for (int k = 0; k < 3; k++) {
        o.at[k] = (float)at[k];
    }
    return o;
}

// The camera's right and up (screen) directions in the world.
void cameraAxes(const Orbit& o, float right[3], float up[3], float fwd[3]) {
    float c = cosf(o.pitch);
    fwd[0] = sinf(o.yaw) * c;
    fwd[1] = sinf(o.pitch);
    fwd[2] = cosf(o.yaw) * c;
    right[0] = cosf(o.yaw);
    right[1] = 0;
    right[2] = -sinf(o.yaw);
    // up = right x fwd, then negated: y is down
    up[0] = -(right[1] * fwd[2] - right[2] * fwd[1]);
    up[1] = -(right[2] * fwd[0] - right[0] * fwd[2]);
    up[2] = -(right[0] * fwd[1] - right[1] * fwd[0]);
}

void freeCamera(XgFieldCamera* c) {
    eyeOf(S->orbit, c->eye);
    memcpy(c->at, S->orbit.at, sizeof(c->at));
    c->fov = S->orbit.fov;
}

void moveCamera(Orbit& o, const Orbit& home, float maxDistance, bool* redraw) {
    ImGuiIO& io = ImGui::GetIO();
    bool changed = false;
    float right[3], up[3], fwd[3];
    cameraAxes(o, right, up, fwd);
    float panScale = o.distance / 600.0f;
    if (S->view->hovered()) {
        ImGui::SetItemKeyOwner(ImGuiKey_MouseWheelY);
        if (io.MouseWheel != 0) {
            o.distance = fmaxf(50.0f, fminf(o.distance * powf(0.85f, io.MouseWheel), maxDistance));
            changed = true;
        }
        if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
            o = home;
            changed = true;
        }
        float speed = (io.KeyShift ? 40.0f : 12.0f) * fmaxf(1.0f, o.distance / 1000.0f);
        float mv[3] = {0, 0, 0};
        if (ImGui::IsKeyDown(ImGuiKey_W)) {
            mv[0] += fwd[0], mv[2] += fwd[2];
        }
        if (ImGui::IsKeyDown(ImGuiKey_S)) {
            mv[0] -= fwd[0], mv[2] -= fwd[2];
        }
        if (ImGui::IsKeyDown(ImGuiKey_D)) {
            mv[0] += right[0], mv[2] += right[2];
        }
        if (ImGui::IsKeyDown(ImGuiKey_A)) {
            mv[0] -= right[0], mv[2] -= right[2];
        }
        if (ImGui::IsKeyDown(ImGuiKey_E)) {
            mv[1] -= 1; // up (y is down)
        }
        if (ImGui::IsKeyDown(ImGuiKey_Q)) {
            mv[1] += 1;
        }
        if (mv[0] != 0 || mv[1] != 0 || mv[2] != 0) {
            for (int k = 0; k < 3; k++) {
                o.at[k] += mv[k] * speed;
            }
            changed = true;
        }
    }
    bool dragging = ImGui::IsItemActive() && (io.MouseDelta.x != 0 || io.MouseDelta.y != 0);
    if (dragging) {
        bool pan = io.KeyShift || ImGui::IsMouseDown(ImGuiMouseButton_Right) || ImGui::IsMouseDown(ImGuiMouseButton_Middle);
        if (pan) {
            for (int k = 0; k < 3; k++) {
                o.at[k] += (-right[k] * io.MouseDelta.x + up[k] * io.MouseDelta.y) * panScale;
            }
        } else {
            o.yaw += io.MouseDelta.x * 0.006f;
            o.pitch = fmaxf(-1.57f, fminf(1.57f, o.pitch + io.MouseDelta.y * 0.006f));
        }
        changed = true;
    }
    // The gamepad: the right stick orbits, the triggers come closer.
    float sx = ImGui::GetKeyData(ImGuiKey_GamepadRStickRight)->AnalogValue -
               ImGui::GetKeyData(ImGuiKey_GamepadRStickLeft)->AnalogValue;
    float sy = ImGui::GetKeyData(ImGuiKey_GamepadRStickDown)->AnalogValue -
               ImGui::GetKeyData(ImGuiKey_GamepadRStickUp)->AnalogValue;
    float zt = ImGui::GetKeyData(ImGuiKey_GamepadR2)->AnalogValue - ImGui::GetKeyData(ImGuiKey_GamepadL2)->AnalogValue;
    if (fabsf(sx) > 0.2f || fabsf(sy) > 0.2f) {
        o.yaw += sx * 0.04f;
        o.pitch = fmaxf(-1.57f, fminf(1.57f, o.pitch + sy * 0.03f));
        changed = true;
    }
    if (fabsf(zt) > 0.2f) {
        o.distance = fmaxf(50.0f, fminf(o.distance * (1.0f - zt * 0.03f), maxDistance));
        changed = true;
    }
    *redraw |= changed;
}

// ---- fields -----------------------------------------------------------------------------------

void openDisc() {
    for (int i = 0; i < 2 && S->disc == nullptr; i++) {
        const char* p = cdSwapPath(i);
        if (p == nullptr || p[0] == '\0') {
            continue;
        }
        const char* why = nullptr;
        XgDisc* d = xgDiscOpen(p, &why);
        if (d != nullptr && xgDiscNumber(d) == 1) {
            S->disc = d;
        } else {
            xgDiscClose(d);
        }
    }
}

void loadField(int id) {
    xgFieldDestroy(S->field);
    S->field = nullptr;
    S->fieldId = id;
    S->mediaField = -1; S->media.clear(); S->mediaProblems.clear();
    S->selected = -1;
    S->redraw = true;
    if (S->disc == nullptr) {
        S->why = N_("The fields are on disc 1: no disc 1 image was found."); /* tr() where shown */
        return;
    }
    char why[160] = "";
    XgFieldStart start = {};
    start.storyProgress = S->startFrom == START_VALUE ? S->progress : -1;
    start.entry = S->entry;
    if (S->startFrom == START_GAME && !S->gameState.empty()) {
        start.useState = 1;
        start.gameState = S->gameState.data();
    }
    uint64_t t0 = SDL_GetTicksNS();
    S->field = xgFieldLoad(S->disc, id, &start, why, sizeof(why));
    S->why = why;
    if (S->field != nullptr) {
        const XgFieldInfo* in = xgFieldInfo(S->field);
        status("%s: %s (%.0f ms)", fieldLabel(id).c_str(), in->note, (SDL_GetTicksNS() - t0) / 1e6);
        S->gameOrbit = orbitFrom(in->eye, in->at);
        S->gameOrbit.fov = 0;
        S->orbit = S->gameOrbit;
    } else {
        error("Field %d: %s", id, why);
    }
    if (S->view == nullptr) {
        S->view.reset(new View);
    }
    refit();
}

void fieldList() {
    if (S->fieldId >= 0 && S->cursor != S->fieldId) {
        S->cursor = S->fieldId;
        ImGui::SetScrollY(
            fmaxf(0, S->fieldId * ImGui::GetTextLineHeightWithSpacing() - ImGui::GetWindowHeight() * 0.4f));
    }
    ImGuiListClipper clip;
    clip.Begin(XG_FIELD_COUNT);
    while (clip.Step()) {
        for (int i = clip.DisplayStart; i < clip.DisplayEnd; i++) {
            ImGui::PushID(i);
            if (listItem(fieldLabel(i).c_str(), S->fieldId == i)) {
                sound(2);
                loadField(i);
                S->focusWanted = true; // across the area once this frame is drawn
            }
            ImGui::PopID();
        }
    }
}

void worldList();

void listPane(ImVec2 min, ImVec2 max) {
    if (beginPane("##list", min, max, nullptr, false)) {
        if (skin::selectable(tr("Fields"), S->mode == 0, 0, ImVec2(ImGui::CalcTextSize(tr("Fields")).x, 0))) {
            S->mode = 0;
            sound(2);
        }
        ImGui::SameLine(0, 12 * unit());
        if (skin::selectable(tr("World map"), S->mode == 1, 0, ImVec2(ImGui::CalcTextSize(tr("World map")).x, 0))) {
            S->mode = 1;
            sound(2);
        }
        ImGui::Separator();
        ImGui::BeginChild("##entries", ImVec2(0, 0), ImGuiChildFlags_NavFlattened);
        if (S->mode == 0) {
            fieldList();
        } else {
            worldList();
        }
        ImGui::EndChild();
    }
    endPane();
}

void drawScene() {
    if (S->field == nullptr) {
        return;
    }
    uint64_t now = SDL_GetTicksNS();
    bool tick = now - S->lastTick >= 33333333ull; // the field's 30 frames a second
    if (!(tick && S->animate) && !S->redraw) {
        return;
    }
    if (tick) {
        S->lastTick = now;
    }
    bool step = tick && S->animate;
    if (S->layers & XG_FIELD_EXITS) {
        xgFieldFindExits(S->field);
    }
    if (S->freeCam) {
        XgFieldCamera c;
        freeCamera(&c);
        xgFieldDrawFree(S->field, &c, S->freeW, S->freeH, S->layers, step, S->selected);
        S->view->setScene(0, 0, (float)S->freeW, (float)S->freeH);
    } else {
        xgFieldDrawGame(S->field, S->layers, step, S->selected);
        S->view->setScene(0, 0, 320, 224);
    }
    S->view->show(xgFieldGpu(S->field));
    S->redraw = false;
}

void scenePane(ImVec2 min, ImVec2 max, bool focused = false) {
    char title[128];
    snprintf(title, sizeof(title), "%s%s", S->fieldId >= 0 ? fieldLabel(S->fieldId).c_str() : tr("No field"),
             S->field == nullptr ? "" : S->freeCam ? tr("  (free camera)") : tr("  (the game's camera)"));
    if (beginPane("##scene", min, max, title, false)) {
        if (S->field == nullptr) {
            ImGui::PushStyleColor(ImGuiCol_Text, S->fieldId < 0 ? skin::textHint : skin::textError);
            ImGui::TextWrapped("%s", S->fieldId < 0 ? tr("Choose a field.") : tr(S->why.c_str()));
            ImGui::PopStyleColor();
        } else {
            // The free camera's screen takes the pane's shape (the GTE's
            // screen reaches 1024 across).
            ImVec2 avail = ImGui::GetContentRegionAvail();
            float k = fminf(1.0f, 1024.0f / fmaxf(fmaxf(avail.x, avail.y), 1.0f));
            int w = (int)fmaxf(64, floorf(avail.x * k)), h = (int)fmaxf(64, floorf(avail.y * k));
            if (w != S->freeW || h != S->freeH) {
                S->freeW = w;
                S->freeH = h;
                S->redraw |= S->freeCam;
            }
            drawScene();
            S->view->setReplacements(lookLibraries(mapLibraries));
            S->view->setInteractive(!S->freeCam);
            S->view->setBackground(IM_COL32(0, 0, 0, 255));
            S->view->draw();
            if (focused) focusPicture(nullptr);
            fill(avail, S->freeCam ? (float)S->freeW : 320.0f, S->freeCam ? (float)S->freeH : 224.0f, S->freeCam);
            if (S->freeCam) {
                moveCamera(S->orbit, S->gameOrbit, MAX_DISTANCE, &S->redraw);
            }
        }
    }
    endPane();
}

// The next checkbox or radio button on this line if it fits, else on the
// next (panes are narrow in some windows).
void flow(const char* label) {
    label = tr(label); // the width of what follows: the translated label (ui/l10n.h)
    ImGuiStyle& st = ImGui::GetStyle();
    float w = ImGui::GetFrameHeight() + st.ItemInnerSpacing.x + ImGui::CalcTextSize(label, nullptr, true).x;
    float right = ImGui::GetItemRectMax().x + st.ItemSpacing.x + w;
    if (right <= ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x) {
        ImGui::SameLine();
    }
}

bool layer(const char* label, unsigned bit) {
    bool on = (S->layers & bit) != 0;
    if (ImGui::Checkbox(tr(label), &on)) { // tools/l10n.py extracts layer()'s text
        S->layers = on ? S->layers | bit : S->layers & ~bit;
        S->redraw = true;
        return true;
    }
    return false;
}

void viewTab() {
    float u = unit();
    if (ImGui::RadioButton(tr("The game's camera"), !S->freeCam)) {
        S->freeCam = false;
        S->redraw = true;
        refit();
    }
    flow("Free camera");
    if (ImGui::RadioButton(tr("Free camera"), S->freeCam)) {
        S->freeCam = true;
        S->redraw = true;
        refit();
    }
    flow("Animate");
    if (ImGui::Checkbox(tr("Animate"), &S->animate)) {
        S->redraw = true;
    }
    layer("Models", XG_FIELD_MODELS);
    flow("Sprites");
    layer("Sprites", XG_FIELD_SPRITES);
    flow("Background, Gears");
    layer("Background, Gears", XG_FIELD_BACKGROUND);
    layer("Walkmesh", XG_FIELD_WALKMESH);
    flow("Filled");
    layer("Filled", XG_FIELD_FILLED);
    flow("Triggers");
    layer("Triggers", XG_FIELD_TRIGGERS);
    flow("Exits");
    layer("Exits", XG_FIELD_EXITS);
    flow("Actors");
    layer("Actors", XG_FIELD_MARKERS);
    lookOptions(mapLibraries, lookCovered(S->view.get(), mapLibraries));
    if (S->freeCam) {
        float fov = S->orbit.fov;
        ImGui::SetNextItemWidth(120 * u);
        if (ImGui::SliderFloat(tr("Field of view"), &fov, 0, 90, fov < 1 ? tr("the game's") : tr("%.0f degrees"))) {
            S->orbit.fov = fov < 1 ? 0 : fov;
            S->redraw = true;
        }
        ImGui::SameLine();
        if (ImGui::Button(tr("The game's view"))) {
            S->orbit = S->gameOrbit;
            S->redraw = true;
        }
        ImGui::PushStyleColor(ImGuiCol_Text, skin::textHint);
        ImGui::TextWrapped(tr("Drag: orbit. Right drag or Shift: pan. Wheel: closer. W A S D Q E: move. Right stick, "
                           "triggers."));
        ImGui::PopStyleColor();
    }
}

bool followFieldFile(bool textures) {
    if (S->mode != 0 || S->field == nullptr || S->disc == nullptr) return false;
    // The field loader reads this pair in directory 04, on this exact disc.
    int file = 0xB8 + 2 * S->fieldId + (textures ? 1 : 0);
    if (xgDiscFileEntry(S->disc, 4, file) < 0) return false;
    char select[32];
    snprintf(select, sizeof(select), "04 %X", file);
    return follow({"browser", {{"image", xgDiscPath(S->disc)}, {"select", select}}});
}

bool followSection(int k) {
    if (S->field == nullptr || S->mode != 0 || k < 0 || k >= XG_FIELD_SECTIONS) return false;
    char select[32];
    snprintf(select, sizeof(select), "04 %X", 0xB8 + 2 * S->fieldId);
    return follow({"browser", {{"image", xgDiscPath(S->disc)}, {"select", select}, {"section", std::to_string(k)}}});
}

bool followActor(bool model) {
    if (S->field == nullptr || S->mode != 0) return false;
    const XgFieldInfo* in = xgFieldInfo(S->field);
    if (S->selected < 0 || S->selected >= in->objects) return false;
    const XgFieldActor& a = in->actor[S->selected];
    std::string arg;
    if (model) {
        if (a.model < 0) return false;
        arg = std::to_string(S->fieldId) + " " + std::to_string(a.model);
        return follow({"models", {{"image", xgDiscPath(S->disc)}, {"field", arg}}});
    }
    if (a.npc >= 0) arg = "npc " + std::to_string(S->fieldId) + " " + std::to_string(a.npc);
    else if (a.character >= 0 && a.character <= 10) arg = "party " + std::to_string(a.character);
    else return false;
    return follow({"sprites", {{"image", xgDiscPath(S->disc)}, {"source", arg}, {"pause", ""}}});
}

bool followMusic() {
    if (S->field == nullptr || S->mode != 0) return false;
    int music = xgFieldInfo(S->field)->music;
    if (music < 0) return false;
    return follow({"jukebox", {{"image", xgDiscPath(S->disc)}, {"play", "#" + std::to_string(music)}}});
}

void actorLinks() {
    const XgFieldInfo* in = xgFieldInfo(S->field);
    if (S->selected < 0 || S->selected >= in->objects) return;
    const XgFieldActor& a = in->actor[S->selected];
    ImGui::Text(tr("Actor %d: model %d, NPC bundle %d, character %d"), S->selected, a.model, a.npc, a.character);
    if (a.model >= 0 && ImGui::Button(tr("Open actor's model"))) followActor(true);
    if ((a.npc >= 0 || (a.character >= 0 && a.character <= 10)) && ImGui::Button(tr("Open actor's sprite"))) followActor(false);
}

void readMedia() {
    if (S->mediaField == S->fieldId) return;
    S->mediaField = S->fieldId; S->media.clear(); S->mediaProblems.clear();
    XgFieldMedia* refs = nullptr;
    int count = xgFieldMediaRead(S->disc, S->fieldId, &refs);
    for (int k = 0; k < count; ++k) {
        const XgFieldMedia& r = refs[k];
        std::string why;
        /* English for the logs; tr() where shown */
        if (r.problem == XG_MEDIA_DYNAMIC) why = N_("Uses a game variable");
        else if (r.problem) why = N_("Incomplete or invalid request");
        else if (r.kind == XG_MEDIA_MOVIE) {
            XgMovieEntry m;
            if (!xgFieldMediaMovie(S->disc, &r, &m)) why = N_("Frame range unavailable on this image");
        } else {
            int bytes = 0, index = xgDiscFileEntry(S->disc, 4, 0x7fb + r.picture);
            uint8_t* data = index >= 0 ? xgDiscReadEntry(S->disc, index, &bytes) : nullptr;
            XgTim tim;
            if (r.picture >= 88 || !data || !xgTimParse(data, bytes, &tim)) why = N_("Picture unavailable on this image");
            free(data);
        }
        S->media.push_back(r); S->mediaProblems.push_back(why);
    }
    free(refs);
}

bool followMedia(int index) {
    if (!S->field || S->mode != 0) return false;
    readMedia();
    if (index < 0 || index >= (int)S->media.size()) return false;
    if (!S->mediaProblems[index].empty()) { error("%s", S->mediaProblems[index].c_str()); return false; }
    const auto& r = S->media[index];
    return follow({"gallery", {{"image", xgDiscPath(S->disc)}, {"media", std::to_string(r.field) + " " + std::to_string(r.pc)}}});
}

void assetsTab() {
    ImGui::Text(tr("%s disc %d  -  field %d"), xgDiscRegion(S->disc), xgDiscNumber(S->disc), S->fieldId);
    ImGui::TextWrapped("%s", xgDiscPath(S->disc));
    ImGui::Separator();
    char label[96];
    snprintf(label, sizeof(label), tr("Open field archive 04/%03X"), 0xB8 + 2 * S->fieldId);
    if (ImGui::Button(label)) followFieldFile(false);
    snprintf(label, sizeof(label), tr("Open texture uploads 04/%03X"), 0xB9 + 2 * S->fieldId);
    if (ImGui::Button(label)) followFieldFile(true);
    const XgFieldInfo* in = xgFieldInfo(S->field);
    if (in->music >= 0) {
        snprintf(label, sizeof(label), tr("Play requested music %d"), in->music);
        if (ImGui::Button(label)) followMusic();
    }
    actorLinks();
    ImGui::SeparatorText(tr("Archive sections"));
    for (int k = 0; k < XG_FIELD_SECTIONS; k++) {
        snprintf(label, sizeof(label), tr("Open %s##section%d"), xgFieldSectionName(k), k);
        if (ImGui::Button(label)) followSection(k);
    }
    ImGui::PushStyleColor(ImGuiCol_Text, skin::textHint);
    ImGui::TextWrapped(tr("Back returns to this field with its camera and selection preserved."));
    ImGui::PopStyleColor();
    readMedia();
    ImGui::SeparatorText(tr("Movies and event pictures"));
    if (S->media.empty()) ImGui::TextWrapped(tr("No reachable media requests in this field script."));
    for (size_t k = 0; k < S->media.size(); ++k) {
        const auto& r = S->media[k];
        ImGui::PushID((int)k + 100);
        if (r.kind == XG_MEDIA_MOVIE) snprintf(label, sizeof(label), tr("Open movie %d, frames %d-%d (PC %04X)"), r.stream, r.first, r.last, r.pc);
        else snprintf(label, sizeof(label), tr("Open event picture %d (PC %04X)"), r.picture, r.pc);
        ImGui::BeginDisabled(!S->mediaProblems[k].empty());
        if (ImGui::Button(label)) followMedia((int)k);
        ImGui::EndDisabled();
        if (!S->mediaProblems[k].empty()) ImGui::TextWrapped(tr("Unavailable: %s"), tr(S->mediaProblems[k].c_str()));
        ImGui::PopID();
    }

}

void actorsTab() {
    const XgFieldInfo* in = xgFieldInfo(S->field);
    actorLinks();
    ImGui::Text(tr("%d entities: %d script actors, %d placed models (%d in the archive)"), in->objects, in->actors,
                in->objects - in->actors, in->models);
    for (int i = 0; i < in->objects; i++) {
        const XgFieldActor& a = in->actor[i];
        char label[128];
        snprintf(label, sizeof(label), "%3d %-6s %6d %6d %6d%s%s", i, a.script ? (a.sprite ? tr("sprite") : tr("actor")) : tr("model"),
                 a.pos[0], a.pos[1], a.pos[2], a.hidden ? tr("  hidden") : "",
                 a.character >= 0 && a.script ? tr("  party") : "");
        ImGui::PushID(i);
        if (listItem(label, S->selected == i, a.hidden)) {
            S->selected = S->selected == i ? -1 : i;
            S->layers |= XG_FIELD_MARKERS;
            S->redraw = true;
            sound(2);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(tr("flags %04X, rotation %d %d %d, facing %d, walkmesh %d triangle %d"), a.flags, a.rot[0],
                              a.rot[1], a.rot[2], a.direction, a.walkmesh, a.triangle);
        }
        ImGui::PopID();
    }
    if (S->selected >= 0 && S->selected < in->actors && ImGui::Button(tr("Inspect script"))) {
        follow({"scripts", {{"image", xgDiscPath(S->disc)}, {"field", std::to_string(S->fieldId)}, {"actor", std::to_string(S->selected)}}});
    }
    if (S->selected >= 0 && S->freeCam && ImGui::Button(tr("Look at it"))) {
        const XgFieldActor& a = in->actor[S->selected];
        for (int k = 0; k < 3; k++) {
            S->orbit.at[k] = (float)a.pos[k];
        }
        S->redraw = true;
    }
}

void walkmeshTab() {
    const XgFieldInfo* in = xgFieldInfo(S->field);
    int total = 0;
    for (int l = 0; l < in->layers; l++) {
        total += in->tris[l];
    }
    ImGui::Text(tr("%d layers, %d triangles, %d trigger zones"), in->layers, total, in->triggers);
    for (int l = 0; l < in->layers; l++) {
        ImGui::Text(tr("Layer %d: %d triangles"), l, in->tris[l]);
    }
    // The materials the triangles use, with their flags.
    std::vector<int> used(256, 0);
    for (int l = 0; l < in->layers; l++) {
        for (int i = 0; i < in->tris[l]; i++) {
            used[in->walk[l][i].material]++;
        }
    }
    ImGui::SeparatorText(tr("Materials"));
    for (int m = 0; m < 256; m++) {
        if (used[(size_t)m] == 0) {
            continue;
        }
        uint32_t flags = 0;
        for (int l = 0; l < in->layers && flags == 0; l++) {
            for (int i = 0; i < in->tris[l]; i++) {
                if (in->walk[l][i].material == m) {
                    flags = in->walk[l][i].flags;
                    break;
                }
            }
        }
        ImGui::Text(tr("%3d: %4d triangles, flags %08X"), m, used[(size_t)m], flags);
    }
}

std::string exitLabel(const XgFieldExit& x) {
    char label[160];
    if (x.field < XG_FIELD_COUNT) {
        snprintf(label, sizeof(label), tr("%s, entry %d"), fieldLabel(x.field).c_str(), x.entry);
    } else {
        int place = x.field - 0x1000;
        const char* name = xgWorldPlaceName != nullptr ? xgWorldPlaceName(place) : nullptr;
        snprintf(label, sizeof(label), tr("the world map: %s (place %d, start %d)"),
                 name != nullptr && name[0] != '\0' ? name : "?", place, x.entry);
    }
    return label;
}

void exitsTab() {
    const XgFieldInfo* in = xgFieldInfo(S->field);
    if (xgFieldFindExits(S->field) > 0) {
        S->redraw = true;
    }
    ImGui::PushStyleColor(ImGuiCol_Text, skin::textHint);
    ImGui::TextWrapped(tr("Found by the field's own scripts: the player put in each trigger zone and at each actor."));
    ImGui::PopStyleColor();
    if (in->exits == 0) {
        ImGui::PushStyleColor(ImGuiCol_Text, skin::textHint);
        ImGui::TextWrapped(tr("No exits found in this field's scripts."));
        ImGui::PopStyleColor();
        return;
    }
    for (int i = 0; i < in->exits; i++) {
        const XgFieldExit& x = in->exit[i];
        char label[200];
        snprintf(label, sizeof(label), "%s  (%s %d)", exitLabel(x).c_str(), x.trigger >= 0 ? tr("trigger") : tr("actor"),
                 x.trigger >= 0 ? x.trigger : x.actor);
        ImGui::PushID(i);
        if (listItem(label, false, x.field >= XG_FIELD_COUNT) && x.field < XG_FIELD_COUNT) {
            sound(2);
            if (S->startFrom != START_GAME) {
                S->entry = x.entry;
            }
            loadField(x.field);
        }
        ImGui::PopID();
    }
}

void startTab() {
    float u = unit();
    bool reload = false;
    ImGui::TextWrapped(tr("Script routine 0 places the actors from the game's state: the story progress (field "
                       "variable 0) and the entry point (variable 2)."));
    reload |= ImGui::RadioButton(tr("The start of a new game"), &S->startFrom, START_INITIAL);
    if (!S->gameState.empty()) {
        reload |= ImGui::RadioButton(tr("The paused game's state"), &S->startFrom, START_GAME);
    }
    reload |= ImGui::RadioButton(tr("Story progress"), &S->startFrom, START_VALUE);
    if (S->startFrom == START_VALUE) {
        ImGui::SameLine();
        ImGui::SetNextItemWidth(80 * u);
        if (ImGui::InputInt("##progress", &S->progress, 1, 16)) {
            S->progress = S->progress < 0 ? 0 : S->progress > 0xFFFF ? 0xFFFF : S->progress;
        }
        if (ImGui::IsItemDeactivatedAfterEdit()) {
            reload = true;
        }
        int chapters = xgStoryChapterCount != nullptr ? xgStoryChapterCount() : 0;
        if (chapters > 0) {
            ImGui::SetNextItemWidth(220 * u);
            std::string preview = tr("Chapters");
            if (ImGui::BeginCombo("##chapter", preview.c_str())) {
                for (int c = 0; c < chapters; c++) {
                    char label[96];
                    snprintf(label, sizeof(label), "%5d  %s", xgStoryChapterStart(c), xgStoryChapterName(c));
                    if (ImGui::Selectable(label, false)) {
                        S->progress = xgStoryChapterStart(c);
                        reload = true;
                    }
                }
                ImGui::EndCombo();
            }
        }
    }
    ImGui::SetNextItemWidth(80 * u);
    ImGui::InputInt(tr("Entry point (-1: the state's)"), &S->entry, 1, 1);
    S->entry = S->entry < -1 ? -1 : S->entry;
    if (ImGui::IsItemDeactivatedAfterEdit()) {
        reload = true;
    }
    if (reload && S->fieldId >= 0) {
        loadField(S->fieldId);
    }
}

// ---- exports ----------------------------------------------------------------------------------

std::string exportName(const char* what, const char* ext) {
    char name[128];
    snprintf(name, sizeof(name), "maps/field%03d_%s.%s", S->fieldId, what, ext);
    return exportPath(name);
}

// The picture the view shows (or the top-down map), drawn again at a larger
// virtual screen, then on the CPU at `scale` (maprast.h) to PNG.
std::vector<ExportSource> exportSources() {
    bool world = S->mode != 0;
    int count = world ? xgWorldSources(S->world, nullptr, 0) : xgFieldSources(S->field, nullptr, 0);
    std::vector<int> entries((size_t)count);
    if (world) xgWorldSources(S->world, entries.data(), count); else xgFieldSources(S->field, entries.data(), count);
    std::vector<ExportSource> refs;
    for (int entry : entries) {
        int dir, file;
        if (xgDiscEntryDir(S->disc, entry, &dir, &file)) refs.push_back(exportSource(S->disc, dir, file, "sandbox_read", world ? -1 : S->fieldId));
    }
    return refs;
}
ExportSelection exportSelection() {
    return {{"field", S->mode == 0 ? std::to_string(S->fieldId) : ""}, {"world_stage", S->mode ? std::to_string(S->worldStage) : ""},
        {"layers", std::to_string(S->mode ? S->worldLayers : S->layers)}, {"scale", std::to_string(S->pictureScale)}};
}

bool exportPicture(bool topDown) {
    if (S->field == nullptr) {
        return false;
    }
    int w, h, sw, sh;
    if (S->freeCam || topDown) {
        XgFieldCamera c;
        Orbit keep = S->orbit;
        if (topDown) {
            const XgFieldInfo* in = xgFieldInfo(S->field);
            float cx = (in->min[0] + in->max[0]) * 0.5f, cz = (in->min[2] + in->max[2]) * 0.5f;
            float span = fmaxf((float)(in->max[0] - in->min[0]), (float)(in->max[2] - in->min[2]));
            S->orbit.at[0] = cx;
            S->orbit.at[1] = 0;
            S->orbit.at[2] = cz;
            S->orbit.yaw = 0;
            S->orbit.pitch = 1.5707963f; /* straight down, north up */
            /* as far as the field's renderers go (they take depths below
             * 32768: 3 x 10922 field units), with a narrow field of view:
             * nearly flat */
            S->orbit.distance = MAX_DISTANCE;
            S->orbit.fov = 2.0f * atanf(span * 0.55f / S->orbit.distance) * 57.29578f;
        }
        freeCamera(&c);
        S->orbit = keep;
        if (topDown) {
            sw = sh = 1024;
        } else {
            /* the pane's shape, at the GTE screen's largest */
            float k = 1024.0f / (float)(S->freeW > S->freeH ? S->freeW : S->freeH);
            sw = (int)(S->freeW * k);
            sh = (int)(S->freeH * k);
        }
        xgFieldDrawFree(S->field, &c, sw, sh, S->layers, 0, S->selected);
    } else {
        sw = 320, sh = 224;
        xgFieldDrawGame(S->field, S->layers, 0, S->selected);
    }
    int scale = S->pictureScale < 1 ? 1 : S->pictureScale > 8 ? 8 : S->pictureScale;
    if (sw * scale > 8192) {
        scale = 8192 / sw;
    }
    w = sw * scale;
    h = sh * scale;
    std::vector<uint32_t> rgba((size_t)w * h);
    XgVgpu* g = xgFieldGpu(S->field);
    xgRasterize(xgVgpuPrims(g), xgVgpuPrimCount(g), xgVgpuVram(g), 0, 0, (float)sw, (float)sh, w, h, 0xFF000000u, 1,
                rgba.data());
    S->redraw = true;
    std::string path = exportName(topDown ? "map" : S->freeCam ? "free" : "game", "png");
    if (path.empty() || !xgPngWrite(path.c_str(), rgba.data(), w, h)) {
        error("Export failed: %s", path.empty() ? "no export folder" : xgWriterError());
        return false;
    }
    if (!recordExport(path, "maps", exportSources(), exportSelection())) return false;
    status("Exported %s (%d x %d)", path.c_str(), w, h);
    return true;
}

bool exportScene() {
    if (S->field == nullptr) {
        return false;
    }
    unsigned what = (S->exportModels ? XG_FIELD_MODELS : 0) | (S->exportWalkmesh ? XG_FIELD_WALKMESH : 0) |
                    (S->exportTriggers ? XG_FIELD_TRIGGERS : 0) | (S->exportActors ? XG_FIELD_MARKERS : 0);
    std::string path = exportName("scene", "glb");
    char why[128] = "";
    if (path.empty() || !xgFieldExportGlb(S->field, path.c_str(), what, 0, why, sizeof(why))) {
        error("Export failed: %s", path.empty() ? "no export folder" : why);
        return false;
    }
    if (!recordExport(path, "maps", exportSources(), exportSelection())) return false;
    status("Exported %s", path.c_str());
    return true;
}

void exportTab() {
    if (ImGui::Button(tr("Bookmark"))) bookmarkSelection();
    float u = unit();
    ImGui::TextWrapped(tr("Exports go to %s"), exportDir());
    ImGui::SeparatorText(tr("Pictures (PNG)"));
    ImGui::SetNextItemWidth(100 * u);
    ImGui::SliderInt(tr("Scale"), &S->pictureScale, 1, 8, "%d x");
    if (ImGui::Button(S->freeCam ? tr("The free camera's picture") : tr("The game camera's picture"))) {
        exportPicture(false);
    }
    ImGui::SameLine();
    if (ImGui::Button(tr("Top-down map"))) {
        exportPicture(true);
    }
    ImGui::PushStyleColor(ImGuiCol_Text, skin::textHint);
    ImGui::TextWrapped(tr("The layers shown (View) are drawn: models, sprites, walkmesh, triggers, exits, actors."));
    ImGui::PopStyleColor();
    ImGui::SeparatorText(tr("Scene (glTF binary)"));
    ImGui::Checkbox(tr("Models"), &S->exportModels);
    flow("Walkmesh");
    ImGui::Checkbox(tr("Walkmesh"), &S->exportWalkmesh);
    flow("Trigger zones");
    ImGui::Checkbox(tr("Trigger zones"), &S->exportTriggers);
    flow("Actors");
    ImGui::Checkbox(tr("Actors"), &S->exportActors);
    if (ImGui::Button(tr("Export the scene"))) {
        exportScene();
    }
    ImGui::PushStyleColor(ImGuiCol_Text, skin::textHint);
    ImGui::TextWrapped(tr("Models and Gears in their current poses and places, textured from the field (nearest filtering; "
                       "semi-transparency approximated); y up, 512 field units to the metre."));
    ImGui::PopStyleColor();
    if (ImGui::Button(tr("Open the folder")) && !openExportDir()) {
        error("Cannot open %s", exportDir());
    }
}

void detailPane(ImVec2 min, ImVec2 max) {
    if (beginPane("##detail", min, max, nullptr, false)) {
        if (S->field == nullptr) {
            ImGui::PushStyleColor(ImGuiCol_Text, skin::textHint);
            ImGui::TextWrapped(tr("The fields are read from disc 1 and set up by the field overlay's own code; the "
                               "scene is drawn by its own renderers."));
            ImGui::PopStyleColor();
        } else if (ImGui::BeginTabBar("##tabs")) {
            for (int t = 0; t < TAB_COUNT; t++) {
                ImGuiTabItemFlags f = S->wantTab == t ? ImGuiTabItemFlags_SetSelected : 0;
                if (ImGui::BeginTabItem(tr(tabNames[t]), nullptr, f)) {
                    S->tab = t;
                    ImGui::BeginChild("##tab", ImVec2(0, 0), ImGuiChildFlags_NavFlattened);
                    switch (t) {
                    case TAB_VIEW:
                        viewTab();
                        break;
                    case TAB_ACTORS:
                        actorsTab();
                        break;
                    case TAB_WALKMESH:
                        walkmeshTab();
                        break;
                    case TAB_EXITS:
                        exitsTab();
                        break;
                    case TAB_START:
                        startTab();
                        break;
                    case TAB_ASSETS:
                        assetsTab();
                        break;
                    case TAB_EXPORT:
                        exportTab();
                        break;
                    default:
                        break;
                    }
                    ImGui::EndChild();
                    ImGui::EndTabItem();
                }
            }
            S->wantTab = -1;
            ImGui::EndTabBar();
        }
    }
    endPane();
}

// ---- the world map ---------------------------------------------------------------------------

std::string stageLabel(int k) {
    char buf[128];
    int from = k < (int)S->stages.size() ? S->stages[(size_t)k] : 0;
    if (from < 0) {
        snprintf(buf, sizeof(buf), tr("Special map %d"), k);
        return buf;
    }
    const char* chapter = nullptr;
    int chapters = xgStoryChapterCount != nullptr ? xgStoryChapterCount() : 0;
    for (int c = 0; c < chapters; c++) {
        if (xgStoryChapterStart(c) <= from) {
            chapter = xgStoryChapterName(c);
        }
    }
    snprintf(buf, sizeof(buf), tr("Stage %d (from %d)%s%s"), k, from, chapter != nullptr ? "  " : "",
             chapter != nullptr ? chapter : "");
    return buf;
}

std::string exitTarget(int field) {
    int f = field & 0x3FF; /* the field number; the high bits are the game's flags */
    return fieldLabel(f);
}

void loadWorld(int stage) {
    xgWorldDestroy(S->world);
    S->world = nullptr;
    S->worldStage = stage;
    S->worldExit = -1;
    S->overview = false;
    S->overviewW = 0;
    S->worldRedraw = true;
    if (S->disc == nullptr) {
        S->worldWhy = N_("The world map is read from disc 1: no disc 1 image was found."); /* tr() where shown */
        return;
    }
    int progress = stage >= 0 && stage < (int)S->stages.size() ? S->stages[(size_t)stage] : 0;
    char why[160] = "";
    uint64_t t0 = SDL_GetTicksNS();
    S->world = xgWorldLoad(S->disc, progress, why, sizeof(why));
    S->worldWhy = why;
    if (S->world == nullptr) {
        error("World map: %s", why);
        return;
    }
    const XgWorldInfo* in = xgWorldInfo(S->world);
    if (S->stages.empty()) {
        S->stages.assign(in->stage, in->stage + in->stages);
    }
    status("World map, stage %d: %s (%.0f ms)", stage, in->note, (SDL_GetTicksNS() - t0) / 1e6);
    Orbit o = {};
    o.at[0] = (float)in->startX;
    o.at[1] = 0;
    o.at[2] = (float)in->startZ;
    o.yaw = 0;
    o.pitch = 0.6f;
    o.distance = 2400;
    o.fov = 0;
    S->worldOrbit = S->worldHome = o;
    if (S->view == nullptr) {
        S->view.reset(new View);
    }
    refit();
}

void worldList() {
    if (S->stages.empty() && S->world == nullptr && S->disc != nullptr) {
        loadWorld(0); /* the first configuration also tells the stages */
    }
    for (int k = 0; k < (int)S->stages.size(); k++) {
        ImGui::PushID(k);
        if (listItem(stageLabel(k).c_str(), S->world != nullptr && S->worldStage == k)) {
            sound(2);
            loadWorld(k);
            S->focusWanted = true; // across the area once this frame is drawn
        }
        ImGui::PopID();
    }
}

// The overview: the whole area top-down, as 15-bit pixels shown by the view.
void makeOverview() {
    if (S->world == nullptr) {
        return;
    }
    int w = 0, h = 0;
    uint64_t t0 = SDL_GetTicksNS();
    uint32_t* rgba = xgWorldTopDown(S->world, OVERVIEW_PX, S->worldLayers & (XG_WORLD_GROUND | XG_WORLD_LANDMARKS | XG_WORLD_TREES), &w, &h);
    if (rgba == nullptr || w > XG_VRAM_W || h > XG_VRAM_H) {
        free(rgba);
        error("World map: no overview (%d x %d)", w, h);
        return;
    }
    S->overviewVram.assign((size_t)XG_VRAM_W * XG_VRAM_H, 0);
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            uint32_t v = rgba[(size_t)y * w + x];
            uint16_t c = (uint16_t)((v >> 3 & 31) | (v >> 11 & 31) << 5 | (v >> 19 & 31) << 10);
            S->overviewVram[(size_t)y * XG_VRAM_W + x] = c != 0 ? c : 0x8000; /* black stays drawn */
        }
    }
    free(rgba);
    S->overviewW = w;
    S->overviewH = h;
    S->overviewVersion++;
    S->overview = true;
    S->worldRedraw = true;
    status("World map overview: %d x %d (%.0f ms)", w, h, (SDL_GetTicksNS() - t0) / 1e6);
}

void addOverviewLine(std::vector<XgPrim>& prims, float x0, float y0, float x1, float y1, uint8_t r, uint8_t g,
                     uint8_t b) {
    XgPrim p = {};
    p.x[0] = (int16_t)lrintf(x0);
    p.y[0] = (int16_t)lrintf(y0);
    p.x[1] = (int16_t)lrintf(x1);
    p.y[1] = (int16_t)lrintf(y1);
    p.r[0] = p.r[1] = r;
    p.g[0] = p.g[1] = g;
    p.b[0] = p.b[1] = b;
    p.count = 2;
    p.flags = XG_PRIM_LINE;
    p.area[0] = p.area[1] = -1024;
    p.area[2] = p.area[3] = 2047;
    prims.push_back(p);
}

void showOverview() {
    const XgWorldInfo* in = xgWorldInfo(S->world);
    std::vector<XgPrim> prims;
    XgPrim p = {};
    p.count = 4;
    p.flags = XG_PRIM_TEXTURED | XG_PRIM_RAW | XG_PRIM_FREE_UV;
    p.tpage = 2 << 7; /* 15-bit, page (0, 0) */
    for (int k = 0; k < 4; k++) {
        p.x[k] = (int16_t)(k & 1 ? S->overviewW : 0);
        p.y[k] = (int16_t)(k & 2 ? S->overviewH : 0);
        p.u[k] = (int16_t)(k & 1 ? S->overviewW : 0);
        p.v[k] = (int16_t)(k & 2 ? S->overviewH : 0);
        p.r[k] = p.g[k] = p.b[k] = 128;
    }
    p.area[2] = (int16_t)(S->overviewW - 1);
    p.area[3] = (int16_t)(S->overviewH - 1);
    prims.push_back(p);
    float k = (float)OVERVIEW_PX / XG_WORLD_CHUNK;
    if (S->worldLayers & XG_WORLD_EXITS) {
        for (int i = 0; i < in->exits; i++) {
            const XgWorldExit& e = in->exit[i];
            float x0 = e.x * k, y0 = e.z * k, x1 = (e.x + e.w) * k, y1 = (e.z + e.d) * k;
            uint8_t g = e.table == 0 ? 80 : 170;
            addOverviewLine(prims, x0, y0, x1, y0, 255, g, 60);
            addOverviewLine(prims, x1, y0, x1, y1, 255, g, 60);
            addOverviewLine(prims, x1, y1, x0, y1, 255, g, 60);
            addOverviewLine(prims, x0, y1, x0, y0, 255, g, 60);
        }
    }
    if (S->worldLayers & XG_WORLD_MARKS) {
        for (int i = 0; i < in->models; i++) {
            if (!in->model[i].hidden) {
                float x = in->model[i].x * k, y = in->model[i].z * k;
                addOverviewLine(prims, x - 3, y, x + 3, y, 255, 255, 120);
                addOverviewLine(prims, x, y - 3, x, y + 3, 255, 255, 120);
            }
        }
    }
    /* the camera's target */
    float cx = S->worldOrbit.at[0] * k, cy = S->worldOrbit.at[2] * k;
    addOverviewLine(prims, cx - 6, cy, cx + 6, cy, 80, 255, 255);
    addOverviewLine(prims, cx, cy - 6, cx, cy + 6, 80, 255, 255);
    S->view->setPrims(prims.data(), (int)prims.size());
    S->view->setVram(S->overviewVram.data(), S->overviewVersion);
    S->view->setScene(0, 0, (float)S->overviewW, (float)S->overviewH);
}

void drawWorld() {
    if (S->world == nullptr) {
        return;
    }
    uint64_t now = SDL_GetTicksNS();
    bool tick = !S->overview && S->worldAnimate && now - S->worldTick >= 33333333ull; // 30 frames a second
    if (!tick && !S->worldRedraw && !S->overview) {
        return;
    }
    if (tick) {
        S->worldTick = now;
    }
    if (S->overview) {
        showOverview();
    } else {
        float eye[3];
        eyeOf(S->worldOrbit, eye);
        xgWorldDraw(S->world, eye, S->worldOrbit.at, S->worldOrbit.fov,
                    S->worldLayers | (tick ? XG_WORLD_ANIMATE : 0));
        S->view->show(xgWorldGpu(S->world));
        S->view->setScene(0, 0, XG_WORLD_SCREEN_W, XG_WORLD_SCREEN_H);
    }
    S->worldRedraw = false;
}

void worldScenePane(ImVec2 min, ImVec2 max, bool focused = false) {
    char title[96];
    snprintf(title, sizeof(title), "%s%s", tr("World map"), S->world == nullptr ? "" : S->overview ? tr("  (overview)") : tr("  (free camera)"));
    if (beginPane("##worldscene", min, max, title, false)) {
        if (S->world == nullptr) {
            ImGui::PushStyleColor(ImGuiCol_Text, S->worldWhy.empty() ? skin::textHint : skin::textError);
            ImGui::TextWrapped("%s", S->worldWhy.empty() ? tr("Choose a stage.") : tr(S->worldWhy.c_str()));
            ImGui::PopStyleColor();
        } else {
            ImVec2 avail = ImGui::GetContentRegionAvail();
            drawWorld();
            S->view->setReplacements(lookLibraries(mapLibraries));
            S->view->setInteractive(S->overview);
            S->view->setBackground(IM_COL32(0, 0, 0, 255));
            S->view->draw();
            if (focused) focusPicture(nullptr);
            if (S->overview) {
                fill(avail, (float)S->overviewW, (float)S->overviewH, false);
            } else {
                fill(avail, XG_WORLD_SCREEN_W, XG_WORLD_SCREEN_H, true);
            }
            if (S->overview) {
                // A click on the overview: the camera goes there.
                if (S->view->hovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
                    ImVec2 at = S->view->toScene(ImGui::GetIO().MousePos);
                    float k = (float)XG_WORLD_CHUNK / OVERVIEW_PX;
                    S->worldOrbit.at[0] = at.x * k;
                    S->worldOrbit.at[2] = at.y * k;
                    S->overview = false;
                    S->worldRedraw = true;
                    refit();
                    sound(2);
                }
            } else {
                moveCamera(S->worldOrbit, S->worldHome, WORLD_MAX_DISTANCE, &S->worldRedraw);
            }
        }
    }
    endPane();
}

bool worldLayer(const char* label, unsigned bit) {
    bool on = (S->worldLayers & bit) != 0;
    if (ImGui::Checkbox(tr(label), &on)) {
        S->worldLayers = on ? S->worldLayers | bit : S->worldLayers & ~bit;
        S->worldRedraw = true;
        return true;
    }
    return false;
}

bool exportWorldMap(int px) {
    if (S->world == nullptr) {
        return false;
    }
    int w = 0, h = 0;
    uint64_t t0 = SDL_GetTicksNS();
    uint32_t* rgba = xgWorldTopDown(S->world, px, S->worldLayers & (XG_WORLD_GROUND | XG_WORLD_LANDMARKS | XG_WORLD_TREES), &w, &h);
    if (rgba == nullptr) {
        error("Export failed: the map is too large");
        return false;
    }
    char name[96];
    snprintf(name, sizeof(name), "maps/world_stage%d_map.png", S->worldStage);
    std::string path = exportPath(name);
    bool ok = !path.empty() && xgPngWrite(path.c_str(), rgba, w, h);
    free(rgba);
    S->worldRedraw = true;
    if (!ok) {
        error("Export failed: %s", path.empty() ? "no export folder" : xgWriterError());
        return false;
    }
    if (!recordExport(path, "maps", exportSources(), exportSelection())) return false;
    status("Exported %s (%d x %d, %.1f s)", path.c_str(), w, h, (SDL_GetTicksNS() - t0) / 1e9);
    return true;
}

bool exportWorldPicture() {
    if (S->world == nullptr) {
        return false;
    }
    float eye[3];
    eyeOf(S->worldOrbit, eye);
    xgWorldDraw(S->world, eye, S->worldOrbit.at, S->worldOrbit.fov, S->worldLayers);
    int scale = S->pictureScale < 1 ? 1 : S->pictureScale > 8 ? 8 : S->pictureScale;
    int w = XG_WORLD_SCREEN_W * scale, h = XG_WORLD_SCREEN_H * scale;
    std::vector<uint32_t> rgba((size_t)w * h);
    XgVgpu* g = xgWorldGpu(S->world);
    xgRasterize(xgVgpuPrims(g), xgVgpuPrimCount(g), xgVgpuVram(g), 0, 0, XG_WORLD_SCREEN_W, XG_WORLD_SCREEN_H, w, h,
                0xFF000000u, 1, rgba.data());
    S->worldRedraw = true;
    char name[96];
    snprintf(name, sizeof(name), "maps/world_stage%d_view.png", S->worldStage);
    std::string path = exportPath(name);
    if (path.empty() || !xgPngWrite(path.c_str(), rgba.data(), w, h)) {
        error("Export failed: %s", path.empty() ? "no export folder" : xgWriterError());
        return false;
    }
    if (!recordExport(path, "maps", exportSources(), exportSelection())) return false;
    status("Exported %s (%d x %d)", path.c_str(), w, h);
    return true;
}

void worldDetailPane(ImVec2 min, ImVec2 max) {
    float u = unit();
    if (beginPane("##worlddetail", min, max, nullptr, false)) {
        if (S->world == nullptr) {
            ImGui::PushStyleColor(ImGuiCol_Text, skin::textHint);
            ImGui::TextWrapped(tr("The world map is set up by its overlay's own code; the terrain is streamed in and "
                               "drawn by its own renderers."));
            ImGui::PopStyleColor();
        } else if (ImGui::BeginTabBar("##worldtabs")) {
            const XgWorldInfo* in = xgWorldInfo(S->world);
            if (ImGui::BeginTabItem(tr("View"))) {
                ImGui::BeginChild("##tab", ImVec2(0, 0), ImGuiChildFlags_NavFlattened);
                if (ImGui::RadioButton(tr("Free camera"), !S->overview)) {
                    S->overview = false;
                    S->worldRedraw = true;
                    refit();
                }
                flow("Overview");
                if (ImGui::RadioButton(tr("Overview"), S->overview)) {
                    if (S->overviewW == 0) {
                        makeOverview();
                    }
                    S->overview = S->overviewW > 0;
                    S->worldRedraw = true;
                    refit();
                }
                worldLayer("Ground", XG_WORLD_GROUND);
                flow("Landmarks");
                worldLayer("Landmarks", XG_WORLD_LANDMARKS);
                flow("Trees");
                worldLayer("Trees", XG_WORLD_TREES);
                flow("Sky");
                worldLayer("Sky", XG_WORLD_SKY);
                if (ImGui::Checkbox(tr("Animate"), &S->worldAnimate)) {
                    S->worldRedraw = true;
                }
                flow("Depth fog");
                worldLayer("Depth fog", XG_WORLD_FOG);
                flow("Exits");
                worldLayer("Exits", XG_WORLD_EXITS);
                flow("Landmark marks");
                worldLayer("Landmark marks", XG_WORLD_MARKS);
                ImGui::Text(tr("%s; the camera at %.0f, %.0f"), in->note, S->worldOrbit.at[0], S->worldOrbit.at[2]);
                ImGui::PushStyleColor(ImGuiCol_Text, skin::textHint);
                ImGui::TextWrapped(S->overview ? tr("Right click: the free camera there. Wheel and drag: zoom and pan.")
                                               : tr("Drag: orbit. Right drag or Shift: pan. Wheel: closer. W A S D Q E: "
                                                 "move. The game draws the ground to about 3800 units."));
                ImGui::PopStyleColor();
                ImGui::EndChild();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem(tr("Exits"))) {
                ImGui::BeginChild("##tab", ImVec2(0, 0), ImGuiChildFlags_NavFlattened);
                ImGui::Text(tr("%d exit areas"), in->exits);
                if (S->worldExit >= 0 && S->worldExit < in->exits) {
                    const XgWorldExit& e = in->exit[S->worldExit];
                    char open[160];
                    snprintf(open, sizeof(open), tr("Open %s"), exitTarget(e.field).c_str());
                    if (ImGui::Button(open)) {
                        sound(2);
                        S->mode = 0;
                        loadField(e.field & 0x3FF);
                    }
                }
                for (int i = 0; i < in->exits; i++) {
                    const XgWorldExit& e = in->exit[i];
                    char label[200];
                    snprintf(label, sizeof(label), tr("%s  (table %d, at %d %d)"), exitTarget(e.field).c_str(), e.table,
                             e.x + e.w / 2, e.z + e.d / 2);
                    ImGui::PushID(i);
                    if (listItem(label, S->worldExit == i)) {
                        sound(2);
                        S->worldExit = i;
                        S->worldOrbit.at[0] = (float)(e.x + e.w / 2);
                        S->worldOrbit.at[2] = (float)(e.z + e.d / 2);
                        S->overview = false;
                        S->worldRedraw = true;
                    }
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip(tr("field value %04X, type %d"), e.field, e.type);
                    }
                    ImGui::PopID();
                }
                ImGui::EndChild();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem(tr("Landmarks"))) {
                ImGui::BeginChild("##tab", ImVec2(0, 0), ImGuiChildFlags_NavFlattened);
                for (int i = 0; i < in->models; i++) {
                    const XgWorldModel& m = in->model[i];
                    char label[96];
                    snprintf(label, sizeof(label), tr("%3d at %6d %6d %6d%s"), i, m.x, m.y, m.z, m.hidden ? tr("  hidden") : "");
                    ImGui::PushID(i);
                    if (listItem(label, false, m.hidden)) {
                        sound(2);
                        S->worldOrbit.at[0] = (float)m.x;
                        S->worldOrbit.at[2] = (float)m.z;
                        S->overview = false;
                        S->worldRedraw = true;
                    }
                    ImGui::PopID();
                }
                ImGui::EndChild();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem(tr("Export"))) {
                ImGui::BeginChild("##tab", ImVec2(0, 0), ImGuiChildFlags_NavFlattened);
                if (ImGui::Button(tr("Bookmark"))) bookmarkSelection();
                ImGui::TextWrapped(tr("Exports go to %s"), exportDir());
                ImGui::SetNextItemWidth(100 * u);
                ImGui::SliderInt(tr("Pixels a chunk"), &S->worldPx, 32, 512);
                if (ImGui::Button(tr("The whole area top-down (PNG)"))) {
                    exportWorldMap(S->worldPx);
                }
                ImGui::SetNextItemWidth(100 * u);
                ImGui::SliderInt(tr("Scale"), &S->pictureScale, 1, 8, "%d x");
                if (ImGui::Button(tr("The camera's picture (PNG)"))) {
                    exportWorldPicture();
                }
                ImGui::PushStyleColor(ImGuiCol_Text, skin::textHint);
                ImGui::TextWrapped(tr("The top-down map is drawn chunk by chunk, each seen straight down with the "
                                   "terrain's heights flattened (a flat ground seen from above is an exact map)."));
                ImGui::PopStyleColor();
                ImGui::EndChild();
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
    }
    endPane();
}

// ---- the viewer --------------------------------------------------------------------------------

void opened() {
    S = new State;
    openDisc();
    if (S->disc == nullptr) {
        error("No disc 1 image found: the fields are on disc 1");
    }
    if (fromGame()) {
        S->gameState.resize(xgFieldGameStateSize());
        if (!xgFieldGameState(S->gameState.data(), (int)S->gameState.size())) {
            S->gameState.clear();
        }
    }
}

void closed() {
    xgWorldDestroy(S->world);
    xgFieldDestroy(S->field);
    xgDiscClose(S->disc);
    delete S; // its view goes with it
    S = nullptr;
}

void resumed() {
    S->lastTick = SDL_GetTicksNS();
    S->worldTick = S->lastTick;
    S->wantTab = S->tab;
}

void sweepStep() {
    if (S->sweepNext < 0 || S->sweepNext > S->sweepEnd) {
        return;
    }
    int id = S->sweepNext++;
    loadField(id);
    if (S->field != nullptr) {
        xgFieldFindExits(S->field);
        const XgFieldInfo* in = xgFieldInfo(S->field);
        fprintf(stderr, "extras: maps: sweep %d ok: %s; %d triggers, %d tris, %d exits, gears %d\n", id, in->note,
                in->triggers, in->tris[0] + in->tris[1] + in->tris[2] + in->tris[3], in->exits, in->mechas);
        drawScene();
    } else {
        fprintf(stderr, "extras: maps: sweep %d failed: %s\n", id, S->why.c_str());
    }
}

// ---- the focused view: the field or world map across the area, its controls in a bar ---------------

bool shownScene() { return S->mode == 0 ? S->field != nullptr : S->world != nullptr; }

void setFocused(bool on) {
    if (on == S->focused || (on && !shownScene())) return;
    S->focused = on;
    S->redraw = S->worldRedraw = true;
    refit(); // the new space
    if (on) focusEnter();
    else focusLeave();
}

void focusedView(const Area& a) {
    if (!shownScene()) { setFocused(false); return; }
    float u = a.unit;
    FocusLayout f = focusLayout(a, 1);
    ImVec2 sceneMax = f.max;
    if (S->focusDetails) {
        float detailW = floorf(fminf(230 * u, (f.max.x - f.min.x) * 0.4f));
        sceneMax.x = f.max.x - detailW - 8 * u;
        ImVec2 detailMin(f.max.x - detailW, f.min.y);
        if (S->mode == 0) detailPane(detailMin, f.max);
        else worldDetailPane(detailMin, f.max);
    }
    if (S->mode == 0) scenePane(f.min, sceneMax, true);
    else worldScenePane(f.min, sceneMax, true);
    if (beginFocusBar(f)) setFocused(false);
    if (S->mode == 0) {
        if (focusButton(S->freeCam ? "The game's camera" : "Free camera")) {
            S->freeCam = !S->freeCam;
            S->redraw = true;
            refit();
        }
        if (focusCheckbox("Animate", &S->animate)) S->redraw = true;
    } else {
        if (focusButton(S->overview ? "Free camera" : "Overview")) {
            if (!S->overview && S->overviewW == 0) makeOverview();
            S->overview = !S->overview && S->overviewW > 0;
            S->worldRedraw = true;
            refit();
        }
        if (!S->overview && focusCheckbox("Animate", &S->worldAnimate)) S->worldRedraw = true;
    }
    // The zoom is the picture's own only where the view moves it (the game's
    // camera, the overview); a free camera has its own distance (wheel, triggers).
    bool zoomable = S->mode == 0 ? !S->freeCam : S->overview;
    if (zoomable) {
        switch (focusZoomButtons()) {
        case 1: refit(); break;
        case 2: S->view->setZoom(S->view->zoom() * 0.8f); break;
        case 3: S->view->setZoom(S->view->zoom() * 1.25f); break;
        case 4: S->view->setZoom(1); break; // one screen pixel per window pixel
        default: break;
        }
    }
    focusLook(mapLibraries, lookCovered(S->view.get(), mapLibraries));
    if (focusButton(S->focusDetails ? "Hide details" : "Details")) {
        S->focusDetails = !S->focusDetails;
        S->redraw = S->worldRedraw = true;
        refit();
    }
    endFocusBar();
}

void draw(const Area& a) {
    float u = a.unit;
    sweepStep();
    if (S->focused) {
        focusedView(a);
        return;
    }
    float split = floorf(fminf(190 * u, (a.max.x - a.min.x) * 0.32f) / u) * u;
    listPane(a.min, ImVec2(a.min.x + split, a.max.y));
    float x0 = a.min.x + split + 16 * u;
    // the details beside the scene when there is room (wide windows), else under it
    bool beside = (a.max.x - x0) / u > 560;
    ImVec2 sceneMax, detailMin;
    if (beside) {
        float detailW = floorf(fminf(230 * u, (a.max.x - x0) * 0.4f));
        sceneMax = ImVec2(a.max.x - detailW - 8 * u, a.max.y);
        detailMin = ImVec2(a.max.x - detailW, a.min.y);
    } else {
        float detailH = floorf(fmaxf(90 * u, (a.max.y - a.min.y) * 0.34f));
        sceneMax = ImVec2(a.max.x, a.max.y - detailH - 8 * u);
        detailMin = ImVec2(x0, a.max.y - detailH);
    }
    if (S->mode == 0) {
        scenePane(ImVec2(x0, a.min.y), sceneMax);
        detailPane(detailMin, a.max);
    } else {
        worldScenePane(ImVec2(x0, a.min.y), sceneMax);
        worldDetailPane(detailMin, a.max);
    }
    if (S->focusWanted) {
        S->focusWanted = false;
        setFocused(true);
    }
}

bool back() {
    if (S->selected >= 0) {
        S->selected = -1;
        S->redraw = true;
        return true;
    }
    if (S->focused) {
        setFocused(false);
        return true;
    }
    return false;
}

bool command(const char* cmd, const char* arg) {
    if (!strcmp(cmd, "fit")) { refit(); return true; }
    if (!strcmp(cmd, "focus")) {
        bool on = strcmp(arg, "0") != 0;
        setFocused(on);
        return S->focused == on;
    }

    if (strcmp(cmd, "image") == 0) {
        const char* why = nullptr;
        XgDisc* d = xgDiscOpen(arg, &why);
        if (!d) { error("%s: %s", arg, why); return false; }
        if (xgDiscNumber(d) != 1) { xgDiscClose(d); error("Maps need disc 1"); return false; }
        S->view.reset(); xgFieldDestroy(S->field); S->field = nullptr;
        xgWorldDestroy(S->world); S->world = nullptr;
        xgDiscClose(S->disc); S->disc = d;
        S->fieldId = -1; S->mediaField = -1; S->media.clear(); S->mediaProblems.clear(); S->selected = -1; S->stages.clear(); S->overviewVram.clear();
        S->overview = false; S->mode = 0; S->redraw = S->worldRedraw = true;
        return true;
    }
    if (strcmp(cmd, "media") == 0) {
        readMedia();
        for (size_t k = 0; k < S->media.size(); ++k) {
            const auto& r = S->media[k];
            fprintf(stderr, "maps: media %zu: field %d pc %d op %02X kind %d stream %d offset %d frames %d-%d picture %d unavailable %s\n",
                k, r.field, r.pc, r.op, r.kind, r.stream, r.offset, r.first, r.last, r.picture, S->mediaProblems[k].c_str());
        }
        return true;
    }
    if (strcmp(cmd, "follow") == 0) {
        if (strncmp(arg, "media ", 6) == 0) return followMedia(atoi(arg + 6));
        if (strcmp(arg, "archive") == 0) return followFieldFile(false);
        if (strcmp(arg, "textures") == 0) return followFieldFile(true);
        if (strncmp(arg, "section ", 8) == 0) return followSection(atoi(arg + 8));
        if (strcmp(arg, "model") == 0) return followActor(true);
        if (strcmp(arg, "sprite") == 0) return followActor(false);
        if (strcmp(arg, "music") == 0) return followMusic();
        return false;
    }
    if (strcmp(cmd, "field") == 0) {
        if (atoi(arg) < 0 || atoi(arg) >= XG_FIELD_COUNT) return false;
        loadField(atoi(arg));
        return S->field != nullptr;
    }
    if (strcmp(cmd, "camera") == 0) {
        S->freeCam = strcmp(arg, "free") == 0;
        S->redraw = true;
        if (S->view != nullptr) {
            refit();
        }
        return true;
    }
    if (strcmp(cmd, "orbit") == 0) {
        float yaw, pitch, dist;
        if (sscanf(arg, "%f %f %f", &yaw, &pitch, &dist) != 3) {
            return false;
        }
        S->orbit.yaw = yaw * 3.14159265f / 180.0f;
        S->orbit.pitch = pitch * 3.14159265f / 180.0f;
        S->orbit.distance = dist;
        S->redraw = true;
        return true;
    }
    if (strcmp(cmd, "target") == 0) {
        return sscanf(arg, "%f %f %f", &S->orbit.at[0], &S->orbit.at[1], &S->orbit.at[2]) == 3 && (S->redraw = true);
    }
    if (strcmp(cmd, "layers") == 0) {
        S->layers = (unsigned)strtoul(arg, nullptr, 16);
        S->redraw = true;
        return true;
    }
    if (strcmp(cmd, "animate") == 0) {
        S->animate = atoi(arg) != 0;
        return true;
    }
    if (strcmp(cmd, "select") == 0) {
        S->selected = atoi(arg);
        S->redraw = true;
        return true;
    }
    if (strcmp(cmd, "tab") == 0) {
        for (int t = 0; t < TAB_COUNT; t++) {
            if (strcmp(arg, tabIds[t]) == 0) {
                S->wantTab = t;
                return true;
            }
        }
        return false;
    }
    if (strcmp(cmd, "progress") == 0) {
        if (strcmp(arg, "game") == 0) {
            S->startFrom = START_GAME;
        } else if (strcmp(arg, "initial") == 0) {
            S->startFrom = START_INITIAL;
        } else {
            S->startFrom = START_VALUE;
            S->progress = atoi(arg);
        }
        if (S->fieldId >= 0) {
            loadField(S->fieldId);
        }
        return true;
    }
    if (strcmp(cmd, "entry") == 0) {
        S->entry = atoi(arg);
        if (S->fieldId >= 0) {
            loadField(S->fieldId);
        }
        return true;
    }
    if (strcmp(cmd, "fields") == 0) {
        S->mode = 0;
        return true;
    }
    if (strcmp(cmd, "page") == 0) {
        if (strcmp(arg, "1") && strcmp(arg, "-1")) return false;
        S->mode = !S->mode; // two tabs: either way is the other one
        sound(1);
        return true;
    }
    if (strcmp(cmd, "world") == 0) {
        S->mode = 1;
        if (S->stages.empty()) {
            loadWorld(0);
        }
        loadWorld(atoi(arg));
        return S->world != nullptr;
    }
    if (strcmp(cmd, "worldcam") == 0) {
        float yaw, pitch, dist;
        if (sscanf(arg, "%f %f %f", &yaw, &pitch, &dist) != 3) {
            return false;
        }
        S->worldOrbit.yaw = yaw * 3.14159265f / 180.0f;
        S->worldOrbit.pitch = pitch * 3.14159265f / 180.0f;
        S->worldOrbit.distance = dist;
        S->overview = false;
        S->worldRedraw = true;
        return true;
    }
    if (strcmp(cmd, "worldtarget") == 0) {
        float y = 0;
        if (sscanf(arg, "%f %f %f", &S->worldOrbit.at[0], &S->worldOrbit.at[2], &y) < 2) {
            return false;
        }
        S->worldOrbit.at[1] = y;
        S->overview = false;
        S->worldRedraw = true;
        return true;
    }
    if (strcmp(cmd, "worldlayers") == 0) {
        S->worldLayers = (unsigned)strtoul(arg, nullptr, 16);
        S->worldRedraw = true;
        return true;
    }
    if (strcmp(cmd, "worldanimate") == 0) {
        S->worldAnimate = atoi(arg) != 0;
        return true;
    }
    if (strcmp(cmd, "vram") == 0) {
        XgVgpu* g = S->mode == 1 && S->world != nullptr ? xgWorldGpu(S->world)
                    : S->field != nullptr                ? xgFieldGpu(S->field)
                                                         : nullptr;
        if (g == nullptr) {
            return false;
        }
        std::vector<uint32_t> rgba((size_t)XG_VRAM_W * XG_VRAM_H);
        const uint16_t* v = xgVgpuVram(g);
        for (size_t i = 0; i < rgba.size(); i++) {
            uint32_t c = v[i], r = c & 31, gg = c >> 5 & 31, b = c >> 10 & 31;
            rgba[i] = (r << 3 | r >> 2) | (gg << 3 | gg >> 2) << 8 | (b << 3 | b >> 2) << 16 | 0xFF000000u;
        }
        std::string path = exportPath(arg != nullptr && arg[0] ? arg : "maps/vram.png");
        xgPngWrite(path.c_str(), rgba.data(), XG_VRAM_W, XG_VRAM_H);
        fprintf(stderr, "extras: maps: VRAM to %s, %d primitives\n", path.c_str(), xgVgpuPrimCount(g));
        for (int i = 0; i < xgVgpuPrimCount(g) && i < 6; i++) {
            const XgPrim& q = xgVgpuPrims(g)[i];
            fprintf(stderr, "extras: maps:   prim %d: n %d flags %02X xy %d,%d %d,%d %d,%d uv %d,%d %d,%d %d,%d tpage %04X clut %04X rgb %d %d %d area %d %d %d %d\n", i, q.count,
                    q.flags, q.x[0], q.y[0], q.x[1], q.y[1], q.x[2], q.y[2], q.u[0], q.v[0], q.u[1], q.v[1], q.u[2], q.v[2],
                    q.tpage, q.clut, q.r[0], q.g[0], q.b[0], q.area[0], q.area[1], q.area[2], q.area[3]);
        }
        return true;
    }
    if (strcmp(cmd, "overview") == 0) {
        makeOverview();
        if (S->view != nullptr) {
            refit();
        }
        return true;
    }
    if (strcmp(cmd, "export") == 0) {
        if (strncmp(arg, "worldmap", 8) == 0) {
            int px = atoi(arg + 8);
            return exportWorldMap(px > 0 ? px : 256), true;
        }
        if (strcmp(arg, "worldpng") == 0) {
            return exportWorldPicture(), true;
        }
        if (strcmp(arg, "png") == 0) {
            return exportPicture(false), true;
        }
        if (strcmp(arg, "map") == 0) {
            return exportPicture(true), true;
        }
        if (strcmp(arg, "glb") == 0) {
            return exportScene(), true;
        }
        return false;
    }
    if (strcmp(cmd, "scale") == 0) {
        S->pictureScale = atoi(arg);
        return true;
    }
    if (strcmp(cmd, "sweep") == 0) {
        if (sscanf(arg, "%d %d", &S->sweepNext, &S->sweepEnd) != 2) {
            return false;
        }
        return true;
    }
    if (strcmp(cmd, "info") == 0) {
        if (S->mode == 1 && S->world != nullptr) {
            const XgWorldInfo* in = xgWorldInfo(S->world);
            fprintf(stderr, "extras: maps: world stage %d (progress %d): %s; config %d, base file %d, start %d %d\n",
                    S->worldStage, in->progress, in->note, in->config, in->baseFile, in->startX, in->startZ);
            for (int i = 0; i < in->exits; i++) {
                const XgWorldExit& e = in->exit[i];
                fprintf(stderr, "extras: maps:   exit %d: table %d at %d %d size %d %d to %04X (%s) type %d\n", i,
                        e.table, e.x, e.z, e.w, e.d, e.field, exitTarget(e.field).c_str(), e.type);
            }
            return true;
        }
        if (S->field != nullptr) {
            const XgFieldInfo* in = xgFieldInfo(S->field);
            fprintf(stderr, "extras: maps: source %s disc %d: %s; file 04/%03X; camera %s orbit %.6f %.6f %.6f "
                            "target %.6f %.6f %.6f layers %X selected %d tab %s start %d progress %d entry %d\n",
                    xgDiscRegion(S->disc), xgDiscNumber(S->disc), xgDiscPath(S->disc), 0xB8 + 2 * S->fieldId,
                    S->freeCam ? "free" : "game", S->orbit.yaw, S->orbit.pitch, S->orbit.distance,
                    S->orbit.at[0], S->orbit.at[1], S->orbit.at[2], S->layers, S->selected, tabIds[S->tab],
                    S->startFrom, S->progress, S->entry);
            fprintf(stderr, "extras: maps: requested music %d\n", in->music);
            fprintf(stderr,
                    "extras: maps: field %d (%s): %s; walkmesh %d layers (%d %d %d %d), %d materials, %d triggers, "
                    "%d exits, eye %d %d %d at %d %d %d scrz %d, gears %d, bounds %d %d %d - %d %d %d\n",
                    in->id, fieldLabel(in->id).c_str(), in->note, in->layers, in->tris[0], in->tris[1], in->tris[2],
                    in->tris[3], in->materials, in->triggers, in->exits, in->eye[0], in->eye[1], in->eye[2], in->at[0],
                    in->at[1], in->at[2], in->scrz, in->mechas, in->min[0], in->min[1], in->min[2], in->max[0],
                    in->max[1], in->max[2]);
            for (int i = 0; i < in->objects; i++) {
                const XgFieldActor& a = in->actor[i];
                fprintf(stderr, "extras: maps:   %2d %s flags %04X pos %d %d %d rot %d %d %d dir %d walk %d/%d%s\n", i,
                        a.script ? "actor" : "model", a.flags, a.pos[0], a.pos[1], a.pos[2], a.rot[0], a.rot[1],
                        a.rot[2], a.direction, a.walkmesh, a.triangle, a.hidden ? " hidden" : "");
                fprintf(stderr, "extras: maps:   asset actor %d model %d npc %d character %d\n",
                        i, a.model, a.npc, a.character);
            }
            for (int i = 0; i < in->exits; i++) {
                const XgFieldExit& x = in->exit[i];
                fprintf(stderr, "extras: maps:   exit to %s (actor %d trigger %d)\n", exitLabel(x).c_str(), x.actor,
                        x.trigger);
            }
            XgVgpu* g = xgFieldGpu(S->field);
            int count = xgVgpuPrimCount(g);
            const XgPrim* prim = xgVgpuPrims(g);
            fprintf(stderr, "extras: maps: %d captured primitives\n", count);
            for (int i = 0; i < count && i < 6; i++) {
                const XgPrim& p = prim[i];
                fprintf(stderr, "extras: maps:   prim %d: flags %02X xy %d,%d %d,%d %d,%d window %08X "
                                "tpage %04X clut %04X rgb %d %d %d area %d %d %d %d\n",
                        i, p.flags, p.x[0], p.y[0], p.x[1], p.y[1], p.x[2], p.y[2], p.window, p.tpage, p.clut,
                        p.r[0], p.g[0], p.b[0], p.area[0], p.area[1], p.area[2], p.area[3]);
            }
        }
        return true;
    }
    return false;
}

bool bookmark(Bookmark& b) {
    if ((S->mode == 0 ? S->field == nullptr : S->world == nullptr) ||
        !bookmarkSource(b, S->disc, "maps", S->mode == 0 ? fieldLabel(S->fieldId) : "World map stage " + std::to_string(S->worldStage))) return false;
    char a[128];
    auto orbit = [&](const char* name, const Orbit& o) {
        snprintf(a, sizeof(a), "%.6f %.6f %.6f", o.yaw * 180 / 3.14159265f, o.pitch * 180 / 3.14159265f, o.distance);
        b.target.commands.push_back({name, a});
    };
    if (S->mode == 0) {
        // Bookmark a reproducible location, not a copy of the paused game's RAM.
        b.target.commands.push_back({"progress", std::to_string(xgFieldInfo(S->field)->startProgress)});
        b.target.commands.push_back({"entry", std::to_string(xgFieldInfo(S->field)->startEntry)});
        b.target.commands.push_back({"field", std::to_string(S->fieldId)});
        b.target.commands.push_back({"camera", S->freeCam ? "free" : "game"}); orbit("orbit", S->orbit);
        snprintf(a, sizeof(a), "%.6f %.6f %.6f", S->orbit.at[0], S->orbit.at[1], S->orbit.at[2]);
        b.target.commands.push_back({"target", a});
        snprintf(a, sizeof(a), "%X", S->layers); b.target.commands.push_back({"layers", a});
        b.target.commands.push_back({"animate", "0"});
        b.target.commands.push_back({"select", std::to_string(S->selected)});
        b.target.commands.push_back({"tab", tabIds[S->tab]});
    } else {
        b.target.commands.push_back({"world", std::to_string(S->worldStage)}); orbit("worldcam", S->worldOrbit);
        snprintf(a, sizeof(a), "%.6f %.6f %.6f", S->worldOrbit.at[0], S->worldOrbit.at[2], S->worldOrbit.at[1]);
        b.target.commands.push_back({"worldtarget", a});
        snprintf(a, sizeof(a), "%X", S->worldLayers); b.target.commands.push_back({"worldlayers", a});
        b.target.commands.push_back({"worldanimate", "0"});
        if (S->overview) b.target.commands.push_back({"overview", ""});
    }
    return true;
}
View* picture() { return S->view.get(); }

const Viewer viewer = {"maps", "Map viewer",
                       "Fields and the world map, set up and drawn by the game's own code: walkmesh, triggers, exits, "
                       "actors, free camera",
                       50, opened, closed, draw, back, command, nullptr, resumed, bookmark, picture};

} // namespace

XG_EXTRAS_VIEWER(viewer);
