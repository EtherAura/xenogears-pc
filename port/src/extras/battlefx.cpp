// Battle effects from the player's disc, through the original sprite VM,
// effect helpers and Gear trail decoder in private sandbox worlds.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>
#include "ui/l10n.h"
#include "viewer.h"
#include "replace.h"
#include "library.h"
#include "view.h"
#include "picker.h"
#include "export_provenance.h"
#include "bookmark_source.h"
#include "battlefx_assets.h"
#include "entity_names.h"
extern "C" {
#include "cd/cd.h"
#include "battlefx_decode.h"
#include "disc.h"
#include "models.h"
#include "sprites_game.h"
#include "tables.h"
#include "writers.h"
}
namespace {
using namespace extras;

// The packs a battle picture can show (the Enhanced look): the texture pack and the
// sprite library's cels, by content (none ships for battle yet: the note says so).
const unsigned battleLibraries = XG_REPLACE_PACK | XG_REPLACE_CELS;
enum { ABILITIES, COMMON, GEARS };
constexpr int LIMIT = 10000;
const char* const ids[] = {"abilities", "common", "gears"};
const char* const names[] = {N_("Ability sprites"), N_("Common sprites"), N_("Gear trails")}; /* tr() where shown */
struct Item {
    int id, file, texture;
    std::string name;
    int partyFile = 0;
    int disc = 0;
};
struct State {
    XgDisc* disc = nullptr;
    std::vector<XgDisc*> discs;
    bool flat = false;
    XgSprite* sprite = nullptr;
    XgScene* scene = nullptr;
    std::unique_ptr<View> view{new View};
    std::vector<Item> items;
    std::vector<std::pair<int, int>> modelFiles;
    Item selected{-1, -1, -1, ""};
    int source = ABILITIES, shown = ABILITIES, anim = 0, animations = 0, frame = 0;
    int fragment = -1, primitives = 0;
    bool playing = false;
    bool followBounds = true;
    double clock = 0;
    XgCamera camera{};
    bool focused = false; // the focused view (viewer.h): the effect alone across the area
};
State* S;
LibraryStates<State> slots;
uint32_t word(const std::vector<uint8_t>& v, size_t at) {
    return at + 4 <= v.size() ? v[at] | uint32_t(v[at + 1]) << 8 | uint32_t(v[at + 2]) << 16 | uint32_t(v[at + 3]) << 24
                              : 0;
}
std::vector<uint8_t> read(XgDisc* d, int dir, int file) {
    int entry = xgDiscFileEntry(d, dir, file), size = 0;
    if (entry < 0 || xgDiscEntry(d, entry).size <= 0 || xgDiscEntry(d, entry).size > 8 * 1024 * 1024)
        return {};
    uint8_t* bytes = xgDiscReadEntry(d, entry, &size);
    std::vector<uint8_t> out;
    if (bytes && size > 0)
        out.assign(bytes, bytes + size);
    free(bytes);
    return out;
}
bool bundle(const std::vector<uint8_t>& v) {
    return xgBattleFxBundle(v.data(), (int)v.size()) != 0;
}
std::vector<Item> catalog(XgDisc* d, int source) {
    std::vector<Item> out;
    if (!d)
        return out;
    EntityNames names(d);
    char name[160];
    if (source == COMMON) {
        if (bundle(read(d, 0x2C, 2)))
            out.push_back({0, 2, 1, "Common effect bank"});
    } else if (source == ABILITIES) {
        for (const auto& ability : battleAbilities(d))
            out.push_back({ability.id, ability.file, ability.file + 1, ability.name, ability.partyFile});
    } else {
        int id = 0;
        for (int f = 1; f < xgDiscDirFiles(d, 0x29); f++) {
            auto m = read(d, 0x29, f), a = read(d, 0x29, f + 1);
            unsigned n = word(m, 0);
            if (m.size() < 24 || n < 4 || n > 5 || (word(m, 4) != 4 + 4 * n && word(m, 4) != 8 + 4 * n) ||
                word(a, 0) != 2 || (word(a, 4) != 12 && word(a, 4) != 16) || word(a, 8) <= word(a, 4) ||
                word(a, 8) >= a.size())
                continue;
            bool valid = true;
            for (unsigned k = 1; k < n; k++)
                valid &= word(m, 4 * k + 4) > word(m, 4 * k) && word(m, 4 * k + 4) < m.size();
            if (!valid)
                continue;
            std::string entity = names.battleGear(f);
            snprintf(name, sizeof(name), "gear %02d / 29/%03X", id, f);
            out.push_back({id++, f, f + 1, entityLabel(entity, name)});
            f++;
        }
    }
    return out;
}
void clear() {
    xgSpriteDestroy(S->sprite);
    S->sprite = nullptr;
    xgSceneDestroy(S->scene);
    S->scene = nullptr;
    S->anim = -1;
    S->playing = false;
}
double rate() {
    return S->scene ? xgGearStepRate(2) : xgSpriteTickRate(2);
}
void fit() {
    if (S->sprite) {
        int x0, y0, x1, y1;
        if (xgSpritePreviewBounds(S->sprite, &x0, &y0, &x1, &y1)) {
            float cx = (x0 + x1) * .5f, cy = (y0 + y1) * .5f;
            float w = std::max(160.f, float(x1 - x0) + 32), h = std::max(120.f, float(y1 - y0) + 32);
            S->view->setScene(cx - w / 2, cy - h / 2, cx + w / 2, cy + h / 2);
        }
    }
    S->view->fit();
}
void refresh() {
    if (S->sprite) {
        S->view->show(xgSpriteGpu(S->sprite));
        if (S->followBounds)
            fit();
    }
    if (S->scene) {
        S->primitives = xgSceneDraw(S->scene, &S->camera);
        S->view->show(xgSceneGpu(S->scene));
    }
}
bool play(int anim) {
    if (anim < 0 || anim >= S->animations) {
        error("Animation %d is unavailable", anim);
        return false;
    }
    char why[160] = "Unsafe sprite bytecode; animation refused";
    if (S->sprite) {
        if (!xgSpritePlay(S->sprite, anim, 0, 0)) {
            error("%s", why);
            return false;
        }
        S->primitives = xgSpriteStep(S->sprite, 0);
    } else if (S->scene) {
        int f = xgSceneProgramExtraFile(S->scene, anim);
        auto extra = f > 0 ? read(S->disc, 0x2A, f) : std::vector<uint8_t>{};
        if (f > 0 && extra.empty()) {
            error("Extra animation 2A/%03X is unavailable", f);
            return false;
        }
        xgSceneSetExtraFile(S->scene, extra.data(), (int)extra.size());
        if (!xgScenePlay(S->scene, anim, why, sizeof(why))) {
            error("%s", why);
            return false;
        }
    } else
        return false;
    S->anim = anim;
    S->frame = 0;
    S->clock = 0;
    refresh();
    return true;
}
void step(int ticks) {
    if (S->anim < 0)
        return;
    ticks = std::clamp(ticks, 0, LIMIT - S->frame);
    if (!ticks)
        return;
    if (S->sprite)
        for (int i = 0; i < ticks; i++)
            S->primitives = xgSpriteStep(S->sprite, 1);
    if (S->scene)
        xgSceneTick(S->scene, ticks);
    S->frame += ticks;
    refresh();
}
void seek(int frame) {
    if (play(S->anim))
        step(std::clamp(frame, 0, LIMIT));
}
bool select(const Item& item) {
    S->disc = S->discs[item.disc];
    auto data = read(S->disc, S->source == GEARS ? 0x29 : S->source == COMMON ? 0x2C : 0x0E, item.file);
    auto texture = read(S->disc, S->source == GEARS ? 0x29 : S->source == COMMON ? 0x2C : 0x0E, item.texture);
    char why[160];
    XgSprite* sprite = nullptr;
    XgScene* scene = nullptr;
    std::vector<std::pair<int, int>> modelFiles;
    int fragment = -1, animations = 0;
    XgCamera camera = S->camera;
    if (S->source == GEARS) {
        scene = xgSceneCreate();
        if (!scene) {
            error("Out of memory");
            return false;
        }
        xgSceneSetShadow(scene, 0);
        xgSceneSetTrails(scene, 1);
        auto commonTexture = read(S->disc, 0x2C, 1);
        xgSceneAddPackedTextures(scene, commonTexture.data(), (int)commonTexture.size());
        xgSceneAddMecha(scene, data.data(), (int)data.size(), texture.data(), (int)texture.size(), XG_MECHA_BATTLE);
        if (!xgSceneBuild(scene, why, sizeof(why))) {
            xgSceneDestroy(scene);
            error("%s", why);
            return false;
        }
        XgSceneInfo info{};
        xgSceneInfo(scene, &info);
        animations = info.animations;
        float r = 0;
        for (int k = 0; k < 3; k++) {
            camera.target[k] = (info.min[k] + info.max[k]) / 2;
            r += std::pow((info.max[k] - info.min[k]) / 2, 2);
        }
        camera.distance = std::max(200.f, 2 * std::sqrt(r));
        camera.zoom = 1;
        camera.yaw = 1664;
        camera.pitch = 256;
    } else {
        BattleSpriteAsset asset;
        if (!asset.load(S->disc, S->source == COMMON ? 0x2C : 0x0E, item.file, item.partyFile)) {
            error("The ability's sprite data or matching party bank is unavailable");
            return false;
        }
        XgSpriteSetup setup = asset.setup();
        modelFiles = asset.modelFiles();
        fragment = asset.fragment;
        sprite = xgSpriteCreate(&setup, why, sizeof(why));
        if (!sprite) {
            error("%s", why);
            return false;
        }
        animations = xgSpriteAnimationCount(sprite);
    }
    int first = 0;
    while (first < animations && sprite && (xgSpriteAnimationFlags(sprite, first) & XG_SPRITE_ANIM_UNSAFE))
        first++;
    if (first >= animations) {
        xgSpriteDestroy(sprite);
        xgSceneDestroy(scene);
        error("No safe animation in this effect bank");
        return false;
    }
    clear();
    S->sprite = sprite;
    S->modelFiles = std::move(modelFiles);
    S->scene = scene;
    S->camera = camera;
    S->fragment = fragment;
    S->selected = item;
    S->shown = S->source;
    S->animations = animations;
    S->playing = false;
    S->view->setScene(S->scene ? 0 : -320, S->scene ? 0 : -240, S->scene ? 640 : 320, S->scene ? 480 : 240);
    S->view->fit();
    return play(first);
}
void source(int source) {
    S->source = source;
    S->items.clear();
    std::vector<uint64_t> hashes;
    int dir = source == GEARS ? 0x29 : source == COMMON ? 0x2C : 0x0E;
    for (int i = 0; i < (int)S->discs.size(); ++i) {
        for (Item item : catalog(S->discs[i], source)) {
            item.disc = i;
            uint64_t hash = libraryFileHash(S->discs[i], dir, item.file) ^ libraryFileHash(S->discs[i], dir, item.texture);
            bool duplicate = false, alternate = false;
            for (size_t k = 0; k < S->items.size(); ++k) if (S->items[k].id == item.id) {
                duplicate |= hashes[k] == hash; alternate = true;
            }
            if (!duplicate) {
                if (alternate) item.name += " (alternate)";
                S->items.push_back(item); hashes.push_back(hash);
            }
        }
    }
    if (!S->items.empty())
        select(S->items[0]);
}
bool image(const char* path) {
    const char* why = nullptr;
    XgDisc* d = xgDiscOpen(path, &why);
    if (!d) {
        error("%s", why ? why : "Disc image unavailable");
        return false;
    }
    clear();
    for (auto* old : S->discs) xgDiscClose(old);
    S->discs = {d};
    S->disc = d;
    S->selected.id = -1;
    S->animations = 0;
    S->frame = 0;
    S->playing = false;
    S->view->setPrims(nullptr, 0);
    source(S->source);
    return true;
}
void info(FILE* f) {
    XgSpriteState sprite{};
    XgVmState vm{};
    if (S->sprite)
        xgSpriteGetState(S->sprite, &sprite);
    if (S->scene)
        xgSceneVm(S->scene, &vm);
    int trails = S->scene ? xgSceneTrailSegments(S->scene) : 0;
    uint32_t key = sprite.key;
    if (S->scene) {
        key = 2166136261u;
        const uint8_t* p = (const uint8_t*)xgVgpuPrims(xgSceneGpu(S->scene));
        size_t bytes = (size_t)xgVgpuPrimCount(xgSceneGpu(S->scene)) * sizeof(XgPrim);
        for (size_t i = 0; i < bytes; i++)
            key = (key ^ p[i]) * 16777619u;
    }
    fprintf(f,
            "{\"source\":\"%s\",\"item\":%d,\"file\":%d,\"animation\":%d,\"frame\":%d,\"primitives\":%d,\"effects\":%d,"
            "\"native_commands\":%d,\"wobbles\":%d,\"trails\":%d,\"key\":%u,\"heap_full\":%d,\"omitted\":%d,\"missing_"
            "keyframes\":%d}\n",
            ids[S->shown], S->selected.id, S->selected.file, S->anim, S->frame, S->primitives, sprite.effects,
            sprite.nativeCommands, sprite.wobbles, trails, key, sprite.heapFull,
            S->sprite ? !!(xgSpriteAnimationFlags(S->sprite, S->anim) & XG_SPRITE_ANIM_SKIPS) : vm.suppressedOps,
            vm.missingKeyframes);
}
void exportPng() {
    std::vector<uint32_t> rgba;
    if (!S->view->thumbnail(rgba)) {
        error("No effect picture to export");
        return;
    }
    char name[96];
    snprintf(name, sizeof(name), "%s%d/battlefx/%s_%03d_%02d_%03d.png", xgDiscRegion(S->disc), xgDiscNumber(S->disc),
             ids[S->shown], S->selected.id, S->anim, S->frame);
    std::string path = exportPath(name);
    if (!xgPngWrite(path.c_str(), rgba.data(), 160, 120)) {
        error("%s", xgWriterError());
        return;
    }
    int dir = S->shown == GEARS ? 0x29 : S->shown == COMMON ? 0x2C : 0x0E;
    std::vector<ExportSource> sources{
        exportSource(S->disc, dir, S->selected.file, "effect"),
        exportSource(S->disc, dir, S->selected.texture, S->scene ? "animation" : "texture")};
    if (S->selected.partyFile)
        sources.push_back(exportSource(S->disc, 0x2D, S->selected.partyFile, "normal party sprites"));
    if (S->shown == ABILITIES) {
        sources.push_back(exportSource(S->disc, 0x2C, 2, "common sprites"));
        sources.push_back(exportSource(S->disc, 0x2C, 1, "common texture"));
        if (!S->selected.partyFile) sources.push_back(exportSource(S->disc, 0x2D, 1, "preview caster and target"));
        for (auto [dir, file] : S->modelFiles)
            sources.push_back(exportSource(S->disc, dir, file, dir == 0x2A ? "spell model clip" : file & 1 ? "spell model" : "spell model animation"));
    }
    if (S->shown == GEARS)
        sources.push_back(exportSource(S->disc, 0x2C, 1, "trail texture"));
    int extra = S->scene ? xgSceneProgramExtraFile(S->scene, S->anim) : -1;
    if (extra > 0)
        sources.push_back(exportSource(S->disc, 0x2A, extra, "extra animation"));
    recordExport(path, "battlefx", std::move(sources),
                 {{"source", ids[S->shown]},
                  {"item", std::to_string(S->selected.id)},
                  {"animation", std::to_string(S->anim)},
                  {"frame", std::to_string(S->frame)},
                  {"native_rate", std::to_string(rate())},
                  {"preview", "160x120"}});
    status("Saved effect preview: %s", path.c_str());
}
void opened() {
    S = new State;
    S->camera.w = 640;
    S->camera.h = 480;
    S->camera.lightFollows = 1;
    S->view->setKeepInArea(false);
    S->view->setFitOnChange(false);
    for (int i = 0; i < 2; ++i) {
        const char* path = cdSwapPath(i);
        if (path && *path) {
            bool duplicate = false;
            for (auto* d : S->discs) duplicate |= !strcmp(path, xgDiscPath(d));
            if (!duplicate) if (auto* d = xgDiscOpen(path, nullptr)) S->discs.push_back(d);
        }
    }
    std::sort(S->discs.begin(), S->discs.end(), [](XgDisc* a, XgDisc* b) { return xgDiscNumber(a) < xgDiscNumber(b); });
    source(ABILITIES);
}
void closeState() {
    clear();
    for (auto* d : S->discs) xgDiscClose(d);
    delete S;
    S = nullptr;
}
void closed() { slots.close(S, closeState); }

void fxOptions();
void animationCombo();
void setFocused(bool on);
void focusedView(const Area& a);

void draw(const Area& a) {
    if (S->playing && S->anim >= 0) {
        S->clock += std::min(.1f, ImGui::GetIO().DeltaTime) * rate();
        int ticks = (int)S->clock;
        S->clock -= ticks;
        if (ticks)
            step(ticks);
        XgSpriteState state{};
        if (S->sprite)
            xgSpriteGetState(S->sprite, &state);
        if (S->frame == LIMIT || state.heapFull)
            S->playing = false;
    }
    if (S->focused) {
        focusedView(a);
        return;
    }
    bool focus = false;
    float split = std::min(155 * a.unit, a.size().x * .35f);
    if (beginPane("fxlist", a.min, ImVec2(a.min.x + split, a.max.y), "Source")) {
        if (S->discs.empty()) {
            if (ImGui::Button(tr("Open image..."))) openPicker("fxdisc", "Open a disc image", "", ".cue;.bin", false);
        }
        std::string chosen;
        if (picker("fxdisc", &chosen)) image(chosen.c_str());
        if (S->flat) ImGui::TextUnformatted(tr(names[S->source]));
        else for (int i = 0; i < 3; ++i)
            if (listItem(tr(names[i]), S->source == i)) source(i);
        ImGui::Separator();
        for (const Item& it : S->items)
            if (listItem(it.name.c_str(), S->shown == S->source && S->selected.id == it.id && S->selected.disc == it.disc)) {
                select(it);
                focus = true; // chosen: across the area once this frame is drawn
            }
        if (S->items.empty())
            ImGui::TextWrapped(tr("No effect assets in this image."));
    }
    endPane();
    if (beginPane("fxdetail", ImVec2(a.min.x + split + 8 * a.unit, a.min.y), a.max, "Battle effects", false)) {
        ImGui::TextUnformatted(S->selected.name.c_str());
        if (ImGui::Button(tr("Options / export"))) ImGui::OpenPopup("fxoptions");
        fxOptions();
        const float transport = ImGui::GetFrameHeightWithSpacing() * 3 + 6 * a.unit;
        S->view->setReplacements(lookLibraries(battleLibraries));
        S->view->draw(ImVec2(ImGui::GetContentRegionAvail().x, std::max(1.f, ImGui::GetContentRegionAvail().y - transport)));
        if (S->animations > 0) {
            animationCombo();
            if (ImGui::Button(S->playing ? tr("Pause") : tr("Play"))) { S->playing = !S->playing; S->clock = 0; }
            ImGui::SameLine();
            if (ImGui::Button(tr("Step"))) { S->playing = false; step(1); }
            ImGui::SameLine();
            if (ImGui::Button(tr("Restart"))) seek(0);
            ImGui::SameLine();
            if (ImGui::Button(tr("Fit"))) fit();
            int frame = S->frame; ImGui::SetNextItemWidth(-1);
            if (ImGui::SliderInt("##tick", &frame, 0, LIMIT, tr("Tick %d"))) { S->playing = false; seek(frame); }
        }
    }
    endPane();
    if (focus) setFocused(true);
}

// The Options window (its button opens "fxoptions" in the current window).
void fxOptions() {
    if (commandPopup("fxoptions")) {
        if (ImGui::Button(tr("Bookmark"))) bookmarkSelection();
        ImGui::Checkbox(tr("Follow effect bounds"), &S->followBounds);
        lookOptions(battleLibraries, lookCovered(S->view.get(), battleLibraries));
        if (ImGui::Button(tr("Export PNG"))) exportPng();
        XgSpriteState sprite{};
        if (S->sprite) {
            xgSpriteGetState(S->sprite, &sprite);
            ImGui::Text(tr("%d effect tasks / %d native commands"), sprite.effects, sprite.nativeCommands);
            ImGui::TextWrapped(tr("Effect assets isolate the effect on a preview stage. Use Battle scene for the arena, actor, camera and audio."));
        }
        if (S->scene) {
            int yaw = S->camera.yaw;
            if (ImGui::SliderInt(tr("Orbit"), &yaw, 0, 4095)) { S->camera.yaw = yaw; refresh(); }
        }
        ImGui::EndPopup();
    }
}

// The animation's combo (the width left on its line).
void animationCombo() {
    int anim = S->anim;
    char label[64]; snprintf(label, sizeof(label), tr("Animation %d"), anim);
    ImGui::SetNextItemWidth(-1);
    if (ImGui::BeginCombo("##anim", label)) {
        for (int i = 0; i < S->animations; i++) {
            snprintf(label, sizeof(label), tr("Animation %d"), i);
            if (ImGui::Selectable(label, anim == i)) { S->playing = false; play(i); }
        }
        ImGui::EndCombo();
    }
}

// ---- the focused view: the effect alone across the area, its controls in a bar --------------------

void setFocused(bool on) {
    if (on == S->focused || (on && S->selected.id < 0)) return;
    S->focused = on;
    if (on) focusEnter();
    else focusLeave();
    fit(); // the new space
}

void focusedView(const Area& a) {
    if (S->selected.id < 0) { setFocused(false); return; }
    FocusLayout f = focusLayout(a, 1);
    if (beginPane("fxfocus", f.min, f.max, nullptr, false)) {
        S->view->setReplacements(lookLibraries(battleLibraries));
        S->view->draw(ImGui::GetContentRegionAvail());
        focusPicture(S->selected.name.c_str());
    }
    endPane();
    if (beginFocusBar(f)) setFocused(false);
    if (S->animations > 0) {
        if (focusButton("<")) { S->playing = false; play((S->anim + S->animations - 1) % S->animations); }
        focusText("%s %d/%d", tr("Animation"), S->anim + 1, S->animations);
        if (focusButton(">")) { S->playing = false; play((S->anim + 1) % S->animations); }
        if (focusButton(S->playing ? "Pause" : "Play")) { S->playing = !S->playing; S->clock = 0; }
        if (focusButton("Step")) { S->playing = false; step(1); }
        if (focusButton("Restart")) seek(0);
        focusText(tr("Tick %d"), S->frame);
    }
    switch (focusZoomButtons()) {
    case 1: fit(); break;
    case 2: S->view->setZoom(S->view->zoom() * 0.8f); break;
    case 3: S->view->setZoom(S->view->zoom() * 1.25f); break;
    case 4: S->view->setZoom(1); break; // one screen pixel of the effect per window pixel
    default: break;
    }
    focusLook(battleLibraries, lookCovered(S->view.get(), battleLibraries));
    if (focusButton("Options")) ImGui::OpenPopup("fxoptions");
    fxOptions();
    endFocusBar();
}

bool back() {
    if (!S->focused) return false;
    setFocused(false);
    return true;
}
bool command(const char* cmd, const char* arg) {
    if (!strcmp(cmd, "section")) {
        for (int i = 0; i < 3; ++i) if (!strcmp(arg, ids[i])) {
            if (slots.select(arg, S, opened)) source(i);
            S->flat = true; return true;
        }
        return false;
    }
    if (!strcmp(cmd, "fit")) { fit(); return true; }
    if (!strcmp(cmd, "focus")) {
        bool on = strcmp(arg, "0") != 0;
        setFocused(on);
        return S->focused == on;
    }
    if (!strcmp(cmd, "toggle")) { if (S->anim >= 0) S->playing = !S->playing; S->clock = 0; return true; }
    if (!strcmp(cmd, "source")) {
        for (int i = 0; i < 3; i++)
            if (!strcmp(arg, ids[i])) {
                if (S->source != i) source(i);
                return true;
            }
        error("Unknown effect source");
        return true;
    }
    if (!strcmp(cmd, "item")) {
        for (const Item& it : S->items)
            if (it.id == atoi(arg)) {
                select(it);
                return true;
            }
        error("Effect %s unavailable", arg);
        return true;
    }
    if (!strcmp(cmd, "anim")) {
        S->playing = false;
        play(atoi(arg));
        return true;
    }
    if (!strcmp(cmd, "seek")) {
        S->playing = false;
        seek(atoi(arg));
        return true;
    }
    if (!strcmp(cmd, "step")) {
        S->playing = false;
        step(*arg ? atoi(arg) : 1);
        return true;
    }
    if (!strcmp(cmd, "pause") || !strcmp(cmd, "play")) {
        S->playing = !strcmp(cmd, "play");
        S->clock = 0;
        return true;
    }
    if (!strcmp(cmd, "image")) {
        image(arg);
        return true;
    }
    if (!strcmp(cmd, "disc")) {
        for (int i = 0; i < 2; i++) {
            const char* p = cdSwapPath(i);
            const char* why = nullptr;
            XgDisc* d = p ? xgDiscOpen(p, &why) : nullptr;
            bool match = d && xgDiscNumber(d) == atoi(arg);
            xgDiscClose(d);
            if (match) {
                image(p);
                return true;
            }
        }
        error("Disc unavailable");
        return true;
    }
    if (!strcmp(cmd, "info")) {
        info(stderr);
        return true;
    }
    if (!strcmp(cmd, "dump")) {
        if (S->sprite)
            xgSpriteDebugDump(S->sprite);
        XgVgpu* gpu = S->sprite ? xgSpriteGpu(S->sprite) : S->scene ? xgSceneGpu(S->scene) : nullptr;
        if (gpu) {
            auto vram = xgVgpuVram(gpu);
            auto p = xgVgpuPrims(gpu);
            for (int i = 0; i < std::min(8, xgVgpuPrimCount(gpu)); i++) {
                int colours = 0, row = p[i].clut >> 6, col = (p[i].clut & 63) * 16;
                for (int k = 0; k < 16; k++)
                    colours += vram[row * 1024 + ((col + k) & 1023)] != 0;
                fprintf(stderr,
                        "battlefx primitive %d: %d,%d..%d,%d UV %d,%d page %04X clut %04X colours %d rgb %d,%d,%d "
                        "flags %02X\n",
                        i, p[i].x[0], p[i].y[0], p[i].x[3], p[i].y[3], p[i].u[0], p[i].v[0], p[i].tpage, p[i].clut,
                        colours, p[i].r[0], p[i].g[0], p[i].b[0], p[i].flags);
            }
        }
        return true;
    }
    if (!strcmp(cmd, "fit")) {
        fit();
        return true;
    }
    if (!strcmp(cmd, "followbounds")) {
        S->followBounds = !strcmp(arg, "on");
        if (S->followBounds)
            fit();
        return true;
    }
    if (!strcmp(cmd, "view")) {
        int yaw = S->camera.yaw, pitch = S->camera.pitch;
        float zoom = S->camera.zoom;
        if (sscanf(arg, "%d %d %f", &yaw, &pitch, &zoom) >= 2 && std::isfinite(zoom)) {
            S->camera.yaw = (yaw % 4096 + 4096) % 4096;
            S->camera.pitch = std::clamp(pitch, -1000, 1000);
            S->camera.zoom = std::clamp(zoom, .1f, 40.f);
            refresh();
        }
        return true;
    }
    if (!strcmp(cmd, "pose")) {
        FILE* f = fopen(arg, "wb");
        if (f) {
            info(f);
            fclose(f);
        } else
            error("Cannot write diagnostic");
        return true;
    }
    if (!strcmp(cmd, "export")) {
        exportPng();
        return true;
    }
    if (!strcmp(cmd, "sweep")) {
        int ticks = std::clamp(atoi(arg), 0, LIMIT);
        auto items = S->items;
        for (const Item& it : items)
            if (select(it))
                for (int a = 0; a < S->animations; a++) {
                    if (S->sprite && (xgSpriteAnimationFlags(S->sprite, a) & XG_SPRITE_ANIM_UNSAFE))
                        continue;
                    if (play(a)) {
                        step(ticks);
                        info(stderr);
                    }
                }
        return true;
    }
    return false;
}
View* picture() {
    return S ? S->view.get() : nullptr;
}
bool bookmark(Bookmark& b) {
    if (S->selected.id < 0 || S->anim < 0 || !bookmarkSource(b, S->disc, "battlefx", S->selected.name))
        return false;
    b.target.commands.push_back({"source", ids[S->shown]});
    b.target.commands.push_back({"item", std::to_string(S->selected.id)});
    b.target.commands.push_back({"anim", std::to_string(S->anim)});
    b.target.commands.push_back({"seek", std::to_string(S->frame)});
    b.target.commands.push_back({"followbounds", S->followBounds ? "on" : "off"});
    if (S->scene) {
        char view[96];
        snprintf(view, sizeof(view), "%d %d %.6f", S->camera.yaw, S->camera.pitch, S->camera.zoom);
        b.target.commands.push_back({"view", view});
    }
    b.target.commands.push_back({"pause", ""});
    return true;
}
const Viewer viewer = {"battlefx", "Battle effects", "Ability sprites, native effects and Gear trails",
                       45,         opened,           closed,
                       draw,       back,             command,
                       nullptr,    nullptr,          bookmark,
                       picture};
} // namespace
XG_EXTRAS_VIEWER(viewer);
