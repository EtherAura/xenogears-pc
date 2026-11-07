// The model viewer (xg-htn.3) and the animation viewer (xg-htn.4): Gears,
// battle Gears, enemies, arenas and field models from the player's disc, set
// up, animated and drawn by the game's own code in a sandbox (models.h), in
// a 3D view with orbit, zoom and pan; the texture pages they use; export to
// glTF (GLB) with the skeleton and its animations.
//
// Both viewers are this file: the model viewer lists every kind of model and
// shows one with a small animation bar; the animation viewer lists the
// animated ones and shows the animation programs, the VM's state frame by
// frame and a scrubber.
//
// The view: a left drag turns the model, a right or middle drag moves it, the
// wheel zooms (the projection, through the GTE: the picture stays sharp), a
// double click frames it again. Gamepad: the right stick turns it, L2 and R2
// zoom; L1/R1 changes models, Left/Right changes animations.
//
// Script commands (XG_EXTRAS_SCRIPT "do ..."):
//   source <gears|battle|enemies|arenas|fields|world|battling>   the list
//   item <n>              the list's n-th model (fields: field <n> <model>)
//   field <n> <model>     a field's model
//   world <c> <landmark>  a world map configuration's landmark
//   disc <n>              disc 1 or 2 of the drive
//   image <path>          select this exact disc image
//   anim <n>              animation program n, from its start
//   play / pause          the animation
//   seek <frame>          frame n of the program (replayed from its start)
//   step [n]              n game ticks on
//   loop on|off           repeat the selected animation
//   elapsed <seconds>     advance by elapsed time while paused (diagnostic)
//   view <yaw> <pitch> [zoom]   the camera (4096 = 360 degrees)
//   tab <model|textures>  the right pane's tab
//   page <n>              the texture page shown
//   textures|edges|shadow on|off, light world|view
//   morphticks <n>        exported MIMe transition duration (1-120 ticks each way)
//   export glb|glball|png the model (selected / all programs and MIMe targets), the page
//   info                  the model, the VM's state, on stderr
//   pose <path>           native bone matrices and MIMe vertices (JSON, diagnostic)
//   rigscan <ticks> <first> <models>   sample programs with a split opcode

#include <SDL3/SDL.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

#include "imgui.h"
#include "imgui_internal.h"
#include "picker.h"
#include "ui/l10n.h"
#include "ui/skin.h"
#include "view.h"
#include "viewer.h"
#include "replace.h"
#include "library.h"
#include "entity_names.h"
#include "bookmark_source.h"
#include "export_provenance.h"

extern "C" {
#include "cd/cd.h"
#include "disc.h"
#include "files.h"
#include "models.h"
#include "models_vm.h"
#include "tables.h"
#include "writers.h"
}

namespace {

using namespace extras;

enum Source { SRC_GEARS, SRC_BATTLE, SRC_ENEMIES, SRC_ARENAS, SRC_FIELDS, SRC_WORLD, SRC_BATTLING, SRC_COUNT };
const char* const sourceIds[SRC_COUNT] = {"gears", "battle", "enemies", "arenas", "fields", "world", "battling"};
const char* const sourceNames[SRC_COUNT] = {N_("Gears"), N_("Battle"), N_("Enemies"), N_("Arenas"), N_("Fields"), N_("World map"),
                                            N_("Battling")};

struct Item {
    int disc = 0;
    std::string label; // the list's line
    std::string title; // the detail pane's
    int src = 0;
    int a = -1, b = -1, c = -1; // per source: see load()
    bool fieldRow = false;      // a field's line (fields: its models follow when open)
};

std::string itemKey(const Item& it) {
    char key[128];
    snprintf(key, sizeof(key), "%d/%d/%d/%d/%d/%d", it.disc, it.src, it.a, it.b, it.c, it.fieldRow);
    return key;
}



// A field's archive, read when its line opens: the models of section 2 and
// the set-up mode each gets from the first entity placing it.
struct Field {
    bool read = false;
    std::vector<uint8_t> models; // section 2, decompressed
    std::vector<int> at, size;   // its members
    std::vector<int> initMode;   // per model, -1 unplaced
};

// A world map configuration (24/2B+0B·c): its archive's landmark models.
struct World {
    bool read = false;
    std::vector<uint8_t> archive; // file +1, decompressed
    int modelsAt = 0, modelsSize = 0, blocks = 0;
};

struct State {
    bool flat = false, exactDisc = false;
    bool animations = false; // the animation viewer
    std::vector<XgDisc*> discs;
    int disc = 0;
    int source = SRC_GEARS;
    std::vector<Item> items[SRC_COUNT];
    bool listed[SRC_COUNT] = {};
    std::vector<Field> fields;
    std::vector<bool> fieldOpen;
    std::vector<World> worlds;
    std::vector<bool> worldOpen;
    ImGuiID highlight = 0;
    int characterStep = 0;
    bool optionsRequested = false, optionsOpen = false;
    // The focused view (viewer.h): the model chosen, alone across the area.
    bool focused = false, focusRequested = false;
    std::string focusKey;    // the model it opened on (Back: unchanged, the list's scroll stays)
    std::string focusParent;
    int sel = -1;      // in items[source]
    int shownSrc = -1; // what the scene holds
    Item shown;
    std::string shownName;
    XgScene* scene = nullptr;
    std::string why;
    XgSceneInfo info = {};
    std::unique_ptr<View> view;
    XgCamera cam = {};
    float radius = 1000;
    bool dirty = true;
    bool textures = true, edges = false, lightView = false, shadow = false;
    // animation
    int anim = -1;
    int length = -1;   // the program's frames until it stops; -1 unknown, 0 it loops
    std::vector<int> lengths; // each program's, as measured (-1 not yet)
    bool playing = true, loop = true;
    double clock = 0;
    int scrub = 0;
    // texture pages
    int tab = 0, wantTab = -1;
    std::vector<XgScenePage> pages;
    int page = 0;
    std::unique_ptr<View> pageView;
    // export
    bool exportAll = false;
    int exportTicks = 300;
    int scrollTo = -1;
    int morphTarget = 0, morphTicks = 30, morphRemaining = 0;
};

State* S;
LibraryStates<State> slots;

double stepRate(int src) {
    if (src == SRC_BATTLING) {
        return xgVsyncRate() / 2;
    }
    // The tables' rate when they have it (field Gears: mode 1, battle: 2).
    double r = xgGearStepRate(src == SRC_GEARS ? 1 : 2);
    return r > 1 ? r : XG_MECHA_TICK_HZ;
}

// ---- the disc ---------------------------------------------------------------------------------

XgDisc* disc() {
    return S->discs.empty() ? nullptr : S->discs[(size_t)S->disc];
}

std::string discTag() {
    XgDisc* d = disc();
    if (d == nullptr) {
        return "disc";
    }
    std::string r = xgDiscRegion(d);
    for (char& c : r) {
        c = (char)SDL_tolower(c);
    }
    return (r == "?" ? std::string("xg") : r) + std::to_string(xgDiscNumber(d));
}

std::vector<uint8_t> readFile(int dir, int file) {
    std::vector<uint8_t> out;
    XgDisc* d = disc();
    int index = d != nullptr ? xgDiscFileEntry(d, dir, file) : -1;
    if (index < 0) {
        return out;
    }
    XgEntry e = xgDiscEntry(d, index);
    if (e.size <= 0 || e.size > (24 << 20)) {
        return out;
    }
    int size = 0;
    uint8_t* data = xgDiscReadEntry(d, index, &size);
    if (data != nullptr) {
        out.assign(data, data + e.size);
        out.resize((size_t)e.size + 64, 0); // readers run a little past the end
        out.resize((size_t)e.size);
        free(data);
    }
    return out;
}

// The first sector's u32 words of a file (to tell bundles apart), none if empty.
std::vector<uint32_t> peek(int dir, int file, int* size) {
    std::vector<uint32_t> w;
    XgDisc* d = disc();
    int index = d != nullptr ? xgDiscFileEntry(d, dir, file) : -1;
    *size = 0;
    if (index < 0) {
        return w;
    }
    XgEntry e = xgDiscEntry(d, index);
    if (e.size <= 16) {
        return w;
    }
    *size = e.size;
    uint8_t* head = xgDiscReadSectors(d, e.lba, 1);
    if (head != nullptr) {
        w.resize(16);
        memcpy(w.data(), head, 64);
        free(head);
    }
    return w;
}

// A model bundle as the Gear loader takes it: [textures, model archive,
// bone links, gear info (, more)], offsets rising from right after the table.
bool mechaBundle(const std::vector<uint32_t>& w, int size) {
    if (w.size() < 6 || w[0] < 4 || w[0] > 5) {
        return false;
    }
    uint32_t n = w[0];
    if (w[1] != 4 + 4 * n && w[1] != 8 + 4 * n) {
        return false;
    }
    for (uint32_t i = 1; i < n; i++) {
        if (w[i + 1] <= w[i] || w[i + 1] >= (uint32_t)size) {
            return false;
        }
    }
    return true;
}

bool animFile(const std::vector<uint32_t>& w, int size) {
    // [animation, sound], the table perhaps with its end offset after it.
    return w.size() >= 3 && w[0] == 2 && (w[1] == 12 || w[1] == 16) && w[2] > w[1] && w[2] < (uint32_t)size;
}

// ---- the lists --------------------------------------------------------------------------------

std::vector<uint8_t> readCompressed(int dir, int file);
uint32_t le32(const std::vector<uint8_t>& v, size_t at);

bool battlingFile(const std::vector<uint8_t>& v) {
    if (v.size() < 0x28) {
        return false;
    }
    uint32_t base = le32(v, 0x1C), skel = le32(v, 0) - base, model = le32(v, 4) - base;
    uint32_t anim = le32(v, 8) - base;
    if (skel < 0x28 || skel + 4 > v.size() || model < 0x28 || model + 16 > v.size() ||
        anim < 0x28 || anim + 4 > v.size()) {
        return false;
    }
    uint32_t bones = le32(v, skel), parts = le32(v, model), clips = le32(v, anim);
    return bones > 0 && bones < 256 && skel + 4 + bones * 16 <= v.size() && parts > 0 && parts < 256 &&
           clips > 0 && clips < 256 && anim + 4 + clips * 4 <= v.size();
}

void listDiscSource(int src) {
    std::vector<Item>& out = S->items[src];
    out.clear();
    XgDisc* d = disc();
    if (d == nullptr) {
        return;
    }
    EntityNames names(d);
    char label[160];
    switch (src) {
    case SRC_BATTLING:
        // The game reads fighter id+2 in directory 31 with
        // loadBattlingFile, which decompresses a BattlingDataFile.
        for (int f = 2; f <= xgDiscDirFiles(d, 0x31); f++) {
            auto data = readCompressed(0x31, f);
            if (!battlingFile(data)) {
                continue;
            }
            Item it;
            snprintf(label, sizeof(label), "%s  (%s / fighter %02d)", names.battling(f - 2).c_str(), names.battlingCode(f - 2).c_str(), f - 2);
            it.label = label;
            snprintf(label, sizeof(label), "%s  (%s / fighter %02d / 31/%03X)", names.battling(f - 2).c_str(), names.battlingCode(f - 2).c_str(), f - 2, f);
            it.title = label;
            it.src = src;
            it.a = f;
            it.c = f - 2;
            out.push_back(it);
        }
        break;
    case SRC_GEARS:
        // 04/6BA+2n animation, 04/6BB+2n model (n = 0..71).
        for (int n = 0; n < 72; n++) {
            int sa, sm;
            auto wa = peek(4, 0x6BA + 2 * n, &sa), wm = peek(4, 0x6BB + 2 * n, &sm);
            if (!mechaBundle(wm, sm) || !animFile(wa, sa)) {
                continue;
            }
            std::string name = names.fieldGear(n);
            snprintf(label, sizeof(label), "gear %02d", n);
            Item it;
            it.label = entityLabel(name, label);
            snprintf(label, sizeof(label), "gear %02d / 04/%03X", n, 0x6BB + 2 * n);
            it.title = entityLabel(name, label);
            it.src = src;
            it.a = 0x6BB + 2 * n;
            it.b = 0x6BA + 2 * n;
            it.c = n;
            out.push_back(it);
        }
        break;
    case SRC_BATTLE: {
        // Directory 29: a model bundle, then its animation file.
        int files = xgDiscDirFiles(d, 0x29), n = 0;
        for (int f = 1; f < files; f++) {
            int sm, sa;
            auto wm = peek(0x29, f, &sm);
            if (!mechaBundle(wm, sm)) {
                continue;
            }
            auto wa = peek(0x29, f + 1, &sa);
            if (!animFile(wa, sa)) {
                continue;
            }
            std::string name = names.battleGear(f);
            snprintf(label, sizeof(label), "battle %02d", n);
            Item it;
            it.label = entityLabel(name, label);
            snprintf(label, sizeof(label), "battle %02d / 29/%03X", n, f);
            it.title = entityLabel(name, label);
            it.src = src;
            it.a = f;
            it.b = f + 1;
            it.c = n++;
            out.push_back(it);
            f++;
        }
        break;
    }
    case SRC_ENEMIES: {
        // 0D/3+2n: an enemy set's visuals, 12-byte entries from +8 (animation
        // and container offsets, 1 for a 3D model); the battle loader's view.
        int files = xgDiscDirFiles(d, 0x0D);
        for (int f = 3, set = 0; f <= files; f += 2, set++) {
            std::vector<uint8_t> v = readFile(0x0D, f);
            if (v.size() < 8 || v[0] == 0 || v[0] > 16) {
                continue;
            }
            int n = v[0];
            for (int k = 0; k < n && 8 + 12 * k + 12 <= (int)v.size(); k++) {
                uint32_t animAt, boxAt;
                memcpy(&animAt, &v[(size_t)(8 + 12 * k)], 4);
                memcpy(&boxAt, &v[(size_t)(8 + 12 * k + 4)], 4);
                if (v[(size_t)(8 + 12 * k + 8)] == 0 || boxAt < 8 || animAt < 8 || boxAt >= v.size() ||
                    animAt >= v.size()) {
                    continue; // a sprite, or a model an earlier entry gives
                }
                std::string name = names.enemy(set, k);
                snprintf(label, sizeof(label), "set %02d.%d", set, k);
                Item it;
                it.label = entityLabel(name, label);
                snprintf(label, sizeof(label), "set %02d, model %d / 0D/%03X", set, k, f);
                it.title = entityLabel(name, label);
                it.src = src;
                it.a = f;
                it.b = k;
                it.c = set;
                out.push_back(it);
            }
        }
        break;
    }
    case SRC_ARENAS:
        // 0F/6+2n: an arena's model bundle (with the bone placements).
        for (int n = 0; n < 75; n++) {
            int sm;
            auto wm = peek(0x0F, 6 + 2 * n, &sm);
            if (!mechaBundle(wm, sm) || wm[0] != 5) {
                continue;
            }
            snprintf(label, sizeof(label), "Arena %02d  %03X", n, 6 + 2 * n);
            Item it;
            it.label = label;
            snprintf(label, sizeof(label), "Arena %02d  0F/%03X", n, 6 + 2 * n);
            it.title = label;
            it.src = src;
            it.a = 6 + 2 * n;
            it.c = n;
            out.push_back(it);
        }
        break;
    case SRC_FIELDS: {
        // 04/B8+2n: field n's archive (730 fields); its models open below it.
        int fields = 730;
        S->fields.assign((size_t)fields, Field());
        S->fieldOpen.assign((size_t)fields, false);
        for (int n = 0; n < fields; n++) {
            int size;
            auto w = peek(4, 0xB8 + 2 * n, &size);
            if (size < 0x200) {
                continue; // a placeholder (disc 2)
            }
            const char* name = xgFieldName(n);
            snprintf(label, sizeof(label), "%03d %s", n, name != nullptr ? name : "");
            Item it;
            it.label = label;
            it.title = label;
            it.src = src;
            it.a = n;
            it.fieldRow = true;
            out.push_back(it);
        }
        break;
    }
    case SRC_WORLD:
        // 24/2B+0B·c: world map configuration c (17 story states of the map);
        // its landmarks open below it.
        S->worlds.assign(17, World());
        S->worldOpen.assign(17, false);
        for (int c = 0; c < 17; c++) {
            int size;
            peek(0x24, 0x2B + 0x0B * c + 1, &size);
            if (size <= 0) {
                continue;
            }
            snprintf(label, sizeof(label), "World %02d  %03X", c, 0x2B + 0x0B * c);
            Item it;
            it.label = label;
            it.title = label;
            it.src = src;
            it.a = c;
            it.fieldRow = true;
            out.push_back(it);
        }
        break;
    default:
        break;
    }
    S->listed[src] = true;
}

void listSource(int src) {
    int previous = S->disc;
    std::vector<Item> items;
    std::vector<uint64_t> hashes;
    for (int i = 0; i < (int)S->discs.size(); ++i) {
        if (S->exactDisc && i != previous) continue;
        S->disc = i; listDiscSource(src);
        for (Item it : S->items[src]) {
            it.disc = i;
            int dir = src == SRC_GEARS || src == SRC_FIELDS ? 4 : src == SRC_BATTLE ? 0x29 :
                      src == SRC_ENEMIES ? 0x0D : src == SRC_ARENAS ? 0x0F : src == SRC_WORLD ? 0x24 : 0x31;
            uint64_t hash = it.fieldRow ? 0 : libraryFileHash(disc(), dir, it.a);
            if (src == SRC_GEARS || src == SRC_BATTLE) hash ^= libraryFileHash(disc(), dir, it.b);
            bool duplicate = false, alternate = false;
            for (size_t n = 0; n < items.size(); ++n) if (items[n].a == it.a && items[n].b == it.b) {
                duplicate |= it.fieldRow || hashes[n] == hash;
                alternate = true;
            }
            if (!duplicate) {
                if (alternate) it.label += " (alternate)";
                items.push_back(it); hashes.push_back(hash);
            }
        }
    }
    S->disc = previous;
    S->items[src] = std::move(items);
    S->listed[src] = true;
}

uint32_t le32(const std::vector<uint8_t>& v, size_t at) {
    uint32_t x = 0;
    if (at + 4 <= v.size()) {
        memcpy(&x, &v[at], 4);
    }
    return x;
}

// Section k of a field archive, decompressed by the game's own decoder.
std::vector<uint8_t> fieldSection(const std::vector<uint8_t>& arc, int k) {
    std::vector<uint8_t> out;
    uint32_t at = le32(arc, 0x130 + 4 * (size_t)k), next = k < 8 ? le32(arc, 0x130 + 4 * (size_t)k + 4) : 0;
    if (at < 0x190 || at >= arc.size()) {
        return out;
    }
    if (next <= at || next > arc.size()) {
        next = (uint32_t)arc.size();
    }
    int size = 0;
    uint8_t* data = xgLzssDecompress(arc.data() + at, (int)(next - at), &size);
    if (data != nullptr) {
        out.assign(data, data + size);
        free(data);
    }
    return out;
}

void readField(int n) {
    Field& f = S->fields[(size_t)n];
    if (f.read) {
        return;
    }
    f.read = true;
    std::vector<uint8_t> arc = readFile(4, 0xB8 + 2 * n);
    if (arc.size() < 0x190) {
        return;
    }
    f.models = fieldSection(arc, 2);
    uint32_t count = le32(f.models, 0);
    if (count == 0 || count > 1024 || 4 + 4 * count > f.models.size()) {
        f.models.clear();
        return;
    }
    for (uint32_t i = 0; i < count; i++) {
        uint32_t at = le32(f.models, 4 + 4 * (size_t)i);
        uint32_t end = i + 1 < count ? le32(f.models, 8 + 4 * (size_t)i) : (uint32_t)f.models.size();
        f.at.push_back((int)at);
        f.size.push_back(end > at && end <= f.models.size() ? (int)(end - at) : 0);
        f.initMode.push_back(-1);
    }
    // The entities (header 0x18C: count; 0x190: 16-byte records, flags then
    // the model index last): the first placing a model gives its set-up mode
    // (initFieldData: initModel2 with (flags & 0xC) >> 2).
    int entities = arc[0x18C] | arc[0x18D] << 8;
    for (int e = 0; e < entities && 0x190 + 16 * (size_t)e + 16 <= arc.size(); e++) {
        const uint8_t* r = &arc[0x190 + 16 * (size_t)e];
        int flags = r[0] | r[1] << 8, model = r[14] | r[15] << 8;
        if (!(flags & 0x40) && model < (int)count && f.initMode[(size_t)model] < 0) {
            f.initMode[(size_t)model] = (flags & 0xC) >> 2;
        }
    }
}

// The fields' list with each open field's models under it.
std::vector<Item> fieldRows() {
    std::vector<Item> rows;
    char label[96];
    for (const Item& it : S->items[SRC_FIELDS]) {
        rows.push_back(it);
        if (!S->fieldOpen[(size_t)it.a]) {
            continue;
        }
        int previous = S->disc; S->disc = it.disc;
        readField(it.a); S->disc = previous;
        const Field& f = S->fields[(size_t)it.a];
        for (size_t k = 0; k < f.at.size(); k++) {
            Item m;
            snprintf(label, sizeof(label), "  model %zu%s", k, f.initMode[k] < 0 ? " (unplaced)" : "");
            m.label = label;
            const char* name = xgFieldName(it.a);
            snprintf(label, sizeof(label), "Field %03d model %zu  04/%03X%s%s", it.a, k, 0xB8 + 2 * it.a,
                     name != nullptr && name[0] ? "  " : "", name != nullptr ? name : "");
            m.title = label;
            m.disc = it.disc;
            m.src = SRC_FIELDS;
            m.a = it.a;
            m.b = (int)k;
            rows.push_back(m);
        }
    }
    return rows;
}

// A file the game reads through mallocAndDecompress: its LZSS stream, by the
// game's own decoder.
std::vector<uint8_t> readCompressed(int dir, int file) {
    std::vector<uint8_t> raw = readFile(dir, file), out;
    int size = 0;
    uint8_t* data = raw.empty() ? nullptr : xgLzssDecompress(raw.data(), (int)raw.size(), &size);
    if (data != nullptr) {
        out.assign(data, data + size);
        free(data);
    }
    return out;
}

void readWorld(int c) {
    World& w = S->worlds[(size_t)c];
    if (w.read) {
        return;
    }
    w.read = true;
    w.archive = readCompressed(0x24, 0x2B + 0x0B * c + 1);
    // The header's offsets (worldmapFile1Buffer): +8 the models (one model
    // archive, a block per landmark), +0xC the collision meshes after them.
    uint32_t at = le32(w.archive, 8), end = le32(w.archive, 0xC);
    if (at < 0x40 || at >= w.archive.size()) {
        return;
    }
    if (end <= at || end > w.archive.size()) {
        end = (uint32_t)w.archive.size();
    }
    w.modelsAt = (int)at;
    w.modelsSize = (int)(end - at);
    w.blocks = (int)le32(w.archive, at);
    if (w.blocks < 0 || w.blocks > 512) {
        w.blocks = 0;
    }
}

std::vector<Item> worldRows() {
    std::vector<Item> rows;
    char label[96];
    for (const Item& it : S->items[SRC_WORLD]) {
        rows.push_back(it);
        if (!S->worldOpen[(size_t)it.a]) {
            continue;
        }
        int previous = S->disc; S->disc = it.disc;
        readWorld(it.a); S->disc = previous;
        for (int k = 0; k < S->worlds[(size_t)it.a].blocks; k++) {
            Item m;
            snprintf(label, sizeof(label), "  landmark %d", k);
            m.label = label;
            snprintf(label, sizeof(label), "World %02d landmark %d  24/%03X", it.a, k, 0x2B + 0x0B * it.a + 1);
            m.title = label;
            m.disc = it.disc;
            m.src = SRC_WORLD;
            m.a = it.a;
            m.b = k;
            rows.push_back(m);
        }
    }
    return rows;
}

std::vector<Item> rowsFor(int src) {
    return src == SRC_FIELDS ? fieldRows() : src == SRC_WORLD ? worldRows() : S->items[src];
}

// ---- loading ----------------------------------------------------------------------------------

void frame() {
    // Framed: the bounds' centre at the screen's, all of it in view.
    float c[3], r = 0;
    for (int k = 0; k < 3; k++) {
        c[k] = (S->info.min[k] + S->info.max[k]) / 2;
        float h = (S->info.max[k] - S->info.min[k]) / 2;
        r += h * h;
    }
    r = sqrtf(r);
    S->radius = r > 16 ? r : 1000;
    memcpy(S->cam.target, c, sizeof(c));
    S->cam.distance = S->radius * 2.0f;
    S->cam.zoom = 1;
    // From the front, a little above (y points down: a positive pitch looks
    // down on it); field rooms, made to be seen from above, from higher;
    // arenas, whose walls face in, from inside.
    S->cam.yaw = 2048 - 384;
    S->cam.pitch = S->shownSrc == SRC_FIELDS ? 640 : S->shownSrc == SRC_WORLD ? 400 : 256;
    if (S->shownSrc == SRC_ARENAS) {
        S->cam.distance = S->radius * 0.7f;
        S->cam.pitch = 400;
    }
    S->dirty = true;
}

// The battle's copy of the VM: the extra animation file the program loads
// first (op 0x04, directory 0x2A), from the disc, for the scene to give the
// mecha as the op would.
void prepareExtra(XgScene* scene, int anim) {
    if (scene == nullptr || !xgSceneBattle(scene)) {
        return;
    }
    int file = xgSceneProgramExtraFile(scene, anim);
    std::vector<uint8_t> data;
    if (file > 0) {
        data = readFile(0x2A, file);
    }
    xgSceneSetExtraFile(scene, data.empty() ? nullptr : data.data(), (int)data.size());
}

void prepareExtraFor(XgScene* scene, int anim, void* user) {
    (void)user;
    prepareExtra(scene, anim);
}

// How long the program plays before it stops (0: it loops past `limit`).
int probeLength(int limit) {
    char why[128];
    if (!xgScenePlay(S->scene, S->anim, why, sizeof(why))) {
        return -1;
    }
    XgBattlingTiming timing = {};
    xgSceneBattlingTiming(S->scene, S->anim, &timing);
    if (timing.native && timing.speed == 0) return 0;
    for (int f = 1; f <= limit; f++) {
        xgSceneTick(S->scene, 1);
        XgVmState vm;
        xgSceneVm(S->scene, &vm);
        if (vm.stopped) {
            return f;
        }
    }
    return 0;
}

void playAnim(int anim) {
    if (S->info.animations <= 0) {
        return;
    }
    S->anim = std::max(0, std::min(anim, S->info.animations - 1));
    prepareExtra(S->scene, S->anim);
    if ((int)S->lengths.size() != S->info.animations) {
        S->lengths.assign((size_t)S->info.animations, -1);
    }
    if (S->lengths[(size_t)S->anim] < 0) {
        S->lengths[(size_t)S->anim] = probeLength(600);
    }
    S->length = S->lengths[(size_t)S->anim];
    char why[128];
    if (!xgScenePlay(S->scene, S->anim, why, sizeof(why))) {
        error("Animation %d: %s", S->anim, why);
        S->anim = -1;
    }
    S->scrub = 0;
    S->clock = 0;
    S->dirty = true;
}

bool load(const Item& it) {
    S->disc = it.disc;
    if (S->scene == nullptr) {
        S->scene = xgSceneCreate();
        if (S->scene == nullptr) {
            error("Out of memory");
            return false;
        }
    }
    xgSceneClear(S->scene);
    S->why.clear();
    S->anim = -1;
    S->length = -1;
    S->lengths.clear();
    S->playing = false;
    S->loop = true;
    S->pages.clear();
    S->morphTarget = S->morphRemaining = 0;
    S->page = 0;
    char name[96];
    switch (it.src) {
    case SRC_BATTLING: {
        auto data = readCompressed(0x31, it.a);
        if (!battlingFile(data)) {
            return false;
        }
        xgSceneAddBattling(S->scene, data.data(), (int)data.size());
        snprintf(name, sizeof(name), "battling_%02d", it.c);
        break;
    }
    case SRC_GEARS:
    case SRC_BATTLE: {
        int dir = it.src == SRC_GEARS ? 4 : 0x29;
        std::vector<uint8_t> model = readFile(dir, it.a), anim = readFile(dir, it.b);
        xgSceneAddMecha(S->scene, model.data(), (int)model.size(), anim.data(), (int)anim.size(),
                        it.src == SRC_GEARS ? XG_MECHA_ANIMATED : XG_MECHA_BATTLE);
        snprintf(name, sizeof(name), "%s_%02d", it.src == SRC_GEARS ? "gear" : "battle_gear", it.c);
        break;
    }
    case SRC_ENEMIES: {
        std::vector<uint8_t> v = readFile(0x0D, it.a);
        uint32_t animAt = le32(v, 8 + 12 * (size_t)it.b), boxAt = le32(v, 8 + 12 * (size_t)it.b + 4);
        if (animAt < v.size() && boxAt < v.size()) {
            xgSceneAddMecha(S->scene, &v[boxAt], (int)(v.size() - boxAt), &v[animAt], (int)(v.size() - animAt),
                            XG_MECHA_BATTLE);
        }
        snprintf(name, sizeof(name), "enemy_%02d_%d", it.c, it.b);
        break;
    }
    case SRC_ARENAS: {
        std::vector<uint8_t> model = readFile(0x0F, it.a);
        xgSceneAddMecha(S->scene, model.data(), (int)model.size(), nullptr, 0, XG_MECHA_ARENA);
        snprintf(name, sizeof(name), "arena_%02d", it.c);
        break;
    }
    case SRC_FIELDS: {
        readField(it.a);
        const Field& f = S->fields[(size_t)it.a];
        if (it.b < 0 || it.b >= (int)f.at.size() || f.size[(size_t)it.b] <= 0) {
            return false;
        }
        // The field's textures: its texture file and section 0's TIMs.
        std::vector<uint8_t> tex = readFile(4, 0xB9 + 2 * it.a);
        if (!tex.empty()) {
            xgSceneAddFieldTextures(S->scene, tex.data(), (int)tex.size());
        }
        std::vector<uint8_t> arc = readFile(4, 0xB8 + 2 * it.a);
        std::vector<uint8_t> tims = fieldSection(arc, 0);
        if (tims.size() > 8) {
            xgSceneAddTimBundle(S->scene, tims.data(), (int)tims.size());
        }
        int mode = f.initMode[(size_t)it.b] < 0 ? 0 : f.initMode[(size_t)it.b];
        xgSceneAddModel(S->scene, &f.models[(size_t)f.at[(size_t)it.b]], f.size[(size_t)it.b], -1, mode, 0);
        snprintf(name, sizeof(name), "field_%03d_model_%d", it.a, it.b);
        break;
    }
    case SRC_WORLD: {
        readWorld(it.a);
        const World& w = S->worlds[(size_t)it.a];
        if (w.blocks <= 0 || it.b < 0 || it.b >= w.blocks) {
            return false;
        }
        // The world map's textures (loadWorldmapTextureFromFile2/3): two
        // compressed bundles of TIMs, each LoadImage'd where it says; the
        // landmarks set up as worldmapInitAllModels does (lit once).
        for (int k = 2; k <= 3; k++) {
            std::vector<uint8_t> tims = readCompressed(0x24, 0x2B + 0x0B * it.a + k);
            if (tims.size() > 8) {
                xgSceneAddTimBundle(S->scene, tims.data(), (int)tims.size());
            }
        }
        xgSceneAddModel(S->scene, &w.archive[(size_t)w.modelsAt], w.modelsSize, it.b, 1, 0);
        snprintf(name, sizeof(name), "world_%02d_landmark_%d", it.a, it.b);
        break;
    }
    default:
        return false;
    }
    char why[128];
    S->shown = it;
    S->shownSrc = it.src;
    S->shownName = name;
    if (!xgSceneBuild(S->scene, why, sizeof(why))) {
        S->why = why;
        S->info = XgSceneInfo();
        error("%s: %s", it.title.c_str(), why);
        return false;
    }
    xgSceneSetShadow(S->scene, S->shadow);
    xgSceneInfo(S->scene, &S->info);
    frame();
    if (S->info.animations > 0) {
        playAnim(0);
        S->playing = true;
    }
    status("%s: %d parts, %d polygons%s (the game's own model code)", it.title.c_str(), S->info.parts,
           S->info.polygons, S->info.bones > 0 ? ", on its skeleton" : "");
    return true;
}

bool choose(int i) {
    std::vector<Item> rows = rowsFor(S->source);
    if (i < 0 || i >= (int)rows.size()) {
        return false;
    }
    S->sel = i;
    const Item& it = rows[(size_t)i];
    if (it.fieldRow) {
        std::vector<bool>& open = it.src == SRC_WORLD ? S->worldOpen : S->fieldOpen;
        open[(size_t)it.a] = !open[(size_t)it.a];
        sound(2);
        return true;
    }
    if (load(it)) {
        sound(2);
        return true;
    } else {
        sound(4);
    }
    return false;
}

void setSource(int src) {
    if (src < 0 || src >= SRC_COUNT || (S->animations && src > SRC_ENEMIES && src != SRC_BATTLING && src != SRC_FIELDS)) {
        return;
    }
    S->source = src;
    S->sel = -1;
    S->scrollTo = 0;
    if (!S->listed[src]) {
        listSource(src);
    }
}

void selectDisc(int i, bool force = false) {
    if (i < 0 || i >= (int)S->discs.size() || (i == S->disc && !force)) {
        return;
    }
    S->disc = i;
    xgSceneDestroy(S->scene);
    S->scene = nullptr;
    S->view.reset();
    S->pageView.reset();
    S->shownSrc = -1;
    S->shown = Item();
    S->shownName.clear();
    S->info = XgSceneInfo();
    S->why.clear();
    S->anim = S->length = -1;
    S->lengths.clear();
    S->playing = false;
    S->clock = 0;
    S->pages.clear();
    S->page = 0;
    S->morphTarget = S->morphRemaining = 0;
    for (int k = 0; k < SRC_COUNT; k++) {
        S->listed[k] = false;
        S->items[k].clear();
    }
    S->sel = -1;
    S->highlight = 0;
    S->focusParent.clear();
    setSource(S->source);
}

int addDisc(const char* path) {
    for (int i = 0; i < (int)S->discs.size(); i++) if (strcmp(xgDiscPath(S->discs[i]), path) == 0) return i;
    const char* why = nullptr;
    XgDisc* d = xgDiscOpen(path, &why);
    if (d == nullptr) { error("%s: %s", path, why); return -1; }
    S->discs.push_back(d);
    return (int)S->discs.size() - 1;
}

// ---- exports ----------------------------------------------------------------------------------

std::vector<ExportSource> exportSources() {
    const Item& it = S->shown; XgDisc* d = disc(); std::vector<ExportSource> refs;
    auto add = [&](int dir, int file, const char* role, int field = -1, int section = -1, std::vector<int> members = {}) {
        if (xgDiscFileEntry(d, dir, file) >= 0) refs.push_back(exportSource(d, dir, file, role, field, section, members));
    };
    switch (it.src) {
    case SRC_GEARS: case SRC_BATTLE: {
        int dir = it.src == SRC_GEARS ? 4 : 0x29; add(dir, it.a, "model"); add(dir, it.b, "animation"); break;
    }
    case SRC_ENEMIES: add(0x0d, it.a, "enemy", -1, -1, {it.b}); break;
    case SRC_ARENAS: add(0x0f, it.a, "arena"); break;
    case SRC_BATTLING: add(0x31, it.a, "fighter"); break;
    case SRC_FIELDS:
        add(4, 0xb8 + 2 * it.a, "model", it.a, 2, {it.b});
        add(4, 0xb8 + 2 * it.a, "TIMs", it.a, 0); add(4, 0xb9 + 2 * it.a, "textures", it.a); break;
    case SRC_WORLD:
        add(0x24, 0x2b + 0x0b * it.a + 1, "landmark", -1, -1, {it.b});
        add(0x24, 0x2b + 0x0b * it.a + 2, "textures"); add(0x24, 0x2b + 0x0b * it.a + 3, "textures"); break;
    }
    return refs;
}

void exportGlb(bool all) {
    if (S->scene == nullptr || !xgSceneBuilt(S->scene)) {
        error("Choose a model first");
        return;
    }
    std::vector<int> anims;
    std::vector<int> morphs;
    int morphCount = xgSceneMorphCount(S->scene);
    for (int i = 0; i < morphCount; i++) if (all || i == S->morphTarget) morphs.push_back(i);
    std::string weights;
    for (int i = 0; i < morphCount; i++) {
        if (!weights.empty()) weights += " ";
        weights += std::to_string(xgSceneMorphWeight(S->scene, i));
    }
    if (all) {
        for (int i = 0; i < S->info.animations; i++) {
            anims.push_back(i);
        }
    } else if (S->anim >= 0) {
        anims.push_back(S->anim);
    }
    auto sources = exportSources();
    std::vector<int> extraFiles;
    for (int anim : anims) {
        int file = xgSceneProgramExtraFile(S->scene, anim);
        if (file > 0 && xgDiscFileEntry(disc(), 0x2a, file) >= 0 &&
            std::find(extraFiles.begin(), extraFiles.end(), file) == extraFiles.end()) {
            extraFiles.push_back(file);
            sources.push_back(exportSource(disc(), 0x2a, file, "extra animation"));
        }
    }
    std::string path = exportPath((discTag() + "/models/" + S->shownName + ".glb").c_str());
    XgExportOptions o = {};
    o.unit = 1.0f / 1024;
    o.animations = anims.data();
    o.animationCount = (int)anims.size();
    o.morphs = morphs.data();
    o.morphCount = (int)morphs.size();
    o.morphTicks = S->morphTicks;
    o.maxTicks = S->exportTicks;
    o.tickRate = stepRate(S->shownSrc);
    o.beforePlay = prepareExtraFor;
    o.name = S->shownName.c_str();
    char why[160] = "";
    bool wasPlaying = S->playing;
    if (path.empty() || !xgSceneExportGlb(S->scene, path.c_str(), &o, why, sizeof(why))) {
        error("Cannot write %s: %s", path.c_str(), why);
    } else {
        std::string animations;
        std::string targets;
        std::string extras;
        for (int id : anims) { if (!animations.empty()) animations += " "; animations += std::to_string(id); }
        for (int id : morphs) { if (!targets.empty()) targets += " "; targets += std::to_string(id); }
        for (int id : extraFiles) { if (!extras.empty()) extras += " "; extras += std::to_string(id); }
        if (recordExport(path, "models", sources, {{"source", sourceIds[S->shownSrc]}, {"animations", animations},
            {"extra_animation_files", extras}, {"rigs", "all sampled lifetimes"},
            {"morph_targets", targets}, {"morph_weights_4_12", weights}, {"morph_ticks", std::to_string(o.morphTicks)},
            {"max_ticks", std::to_string(S->exportTicks)}, {"tick_rate", std::to_string(o.tickRate)}}))
            status("Exported %s (%d programs, %d MIMe clips)", path.c_str(), (int)anims.size(), (int)morphs.size());
    }
    S->playing = wasPlaying;
    S->dirty = true;
}

void exportPage() {
    if (S->pages.empty()) {
        error("No texture page");
        return;
    }
    const XgScenePage& p = S->pages[(size_t)S->page];
    std::vector<uint32_t> rgba(256 * 256);
    xgSceneBakePage(S->scene, p.tpage, p.clut, 0, rgba.data());
    char suffix[64];
    snprintf(suffix, sizeof(suffix), "_page%d_%d_clut%d_%d.png", (p.tpage & 15) * 64, (p.tpage >> 4 & 1) * 256,
             (p.clut & 63) * 16, p.clut >> 6 & 511);
    std::string path = exportPath((discTag() + "/models/" + S->shownName + suffix).c_str());
    if (path.empty() || !xgPngWrite(path.c_str(), rgba.data(), 256, 256)) {
        error("Cannot write %s: %s", path.c_str(), xgWriterError());
    } else {
        if (recordExport(path, "models", exportSources(), {{"tpage", std::to_string(p.tpage)}, {"clut", std::to_string(p.clut)}})) status("Exported %s", path.c_str());
    }
}

// ---- the panes --------------------------------------------------------------------------------

void hint(const char* fmt, ...) IM_FMTARGS(1);
void hint(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    ImGui::PushStyleColor(ImGuiCol_Text, skin::textHint);
    ImGui::TextWrappedV(tr(fmt), ap); // in the menu's language (ui/l10n.h)
    ImGui::PopStyleColor();
    va_end(ap);
}

bool previewItem(const Item& it) {
    if (it.fieldRow) return false;
    if (S->shownSrc == it.src && itemKey(S->shown) == itemKey(it)) return S->scene && xgSceneBuilt(S->scene);
    return load(it);
}

// Open only groups reached by browsing. Field archives are decoded lazily.
void browseCharacter(int delta) {
    if (!delta) return;
    delta = delta > 0 ? 1 : -1;
    std::vector<Item> rows = rowsFor(S->source);
    if (rows.empty()) return;
    int at = delta > 0 ? -1 : 0;
    for (int i = 0; i < (int)rows.size(); ++i)
        if (!rows[i].fieldRow && S->shownSrc == rows[i].src && itemKey(rows[i]) == itemKey(S->shown)) at = i;
    for (int tries = 0; tries < 10000; ++tries) {
        at = (at + delta + (int)rows.size()) % (int)rows.size();
        Item it = rows[at];
        if (it.fieldRow) {
            // Going up from a group's first model continues into the previous group.
            if (delta < 0 && S->shownSrc == it.src && S->shown.a == it.a) continue;
            (it.src == SRC_WORLD ? S->worldOpen : S->fieldOpen)[(size_t)it.a] = true;
            rows = rowsFor(S->source);
            for (int i = 0; i < (int)rows.size(); ++i) if (itemKey(rows[i]) == itemKey(it)) { at = i; break; }
            int end = at + 1;
            while (end < (int)rows.size() && !rows[end].fieldRow) ++end;
            if (end == at + 1) continue;
            at = delta > 0 ? at + 1 : end - 1;
            it = rows[at];
        }
        if (previewItem(it)) {
            S->sel = at;
            S->scrollTo = -1;
            S->focusParent = itemKey(it);
            S->playing = S->loop = true;
            sound(1);
            return;
        }
    }
}

void cycleAnimation(int delta) {
    int n = S->info.animations;
    if (!S->scene || n <= 0) return;
    playAnim((std::max(0, S->anim) + delta % n + n) % n);
    S->playing = S->loop = true;
    if (viewerShortcuts()) S->focusParent = itemKey(S->shown);
    sound(1);
}

void listPane(ImVec2 min, ImVec2 max) {
    if (beginPane("##models", min, max, nullptr, true)) {
        ImGui::TextColored(skin::textWarning, "%s", tr(sourceNames[S->source]));
        ImGui::Separator();
        if (S->characterStep || (S->shownSrc < 0 && S->why.empty())) {
            browseCharacter(S->characterStep ? S->characterStep : 1);
            S->characterStep = 0;
        }
        std::vector<Item> rows = rowsFor(S->source);
        int focusRow = -1;
        for (int i = 0; i < (int)rows.size(); ++i)
            if (S->scrollTo == i || S->focusParent == itemKey(rows[i])) focusRow = i;
        ImGui::BeginChild("##rows", ImVec2(0, 0), ImGuiChildFlags_NavFlattened);
        if (rows.empty()) hint(disc() == nullptr ? "No disc image." : "None available.");
        int preview = -1, toggle = -1;
        ImGuiListClipper clip;
        clip.Begin((int)rows.size());
        if (focusRow >= 0) clip.IncludeItemByIndex(focusRow);
        while (clip.Step()) for (int r = clip.DisplayStart; r < clip.DisplayEnd; ++r) {
            const Item& it = rows[r];
            std::string key = itemKey(it);
            bool shown = !it.fieldRow && S->shownSrc == it.src && itemKey(S->shown) == key;
            std::string label = it.label;
            if (it.fieldRow) label = std::string((it.src == SRC_WORLD ? S->worldOpen : S->fieldOpen)[(size_t)it.a] ? "- " : "+ ") + label;
            label += "###row";
            ImGui::PushID(key.c_str());
            bool confirmed = listItem(label.c_str(), shown);
            if (libraryHighlight(confirmed, S->highlight) && !it.fieldRow) preview = r;
            if (confirmed) { if (it.fieldRow) toggle = r; else S->focusRequested = true; }
            if (r == focusRow && !ImGui::IsPopupOpen((const char*)nullptr, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel)) {
                ImGui::SetScrollHereY(.3f);
                ImGui::SetKeyboardFocusHere(-1);
                S->highlight = ImGui::GetItemID();
                S->focusParent.clear(); S->scrollTo = -1;
            }
            ImGui::PopID();
        }
        ImGui::EndChild();
        if (preview >= 0) { S->sel = preview; previewItem(rows[preview]); }
        if (toggle >= 0) choose(toggle);
    }
    endPane();
}

// The view's own camera: drags, the wheel, the stick and the triggers.
void cameraInput() {
    if (!viewerShortcuts() && !ImGui::IsItemActive()) return;
    ImGuiIO& io = ImGui::GetIO();
    bool held = ImGui::IsItemActive(); // the view's own item (draw()'s last)
    if (held && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        S->cam.yaw = (S->cam.yaw - (int)lrintf(io.MouseDelta.x * 6) + 8192) % 4096;
        S->cam.pitch = std::max(-1000, std::min(1000, S->cam.pitch + (int)lrintf(io.MouseDelta.y * 6)));
        S->dirty |= io.MouseDelta.x != 0 || io.MouseDelta.y != 0;
    } else if (held && (ImGui::IsMouseDown(ImGuiMouseButton_Right) || ImGui::IsMouseDown(ImGuiMouseButton_Middle))) {
        // Move the target in the view's plane: a window pixel is this many
        // model units at the target's depth.
        float perPixel = S->cam.distance / (S->cam.h * S->cam.zoom) * (S->cam.h / std::max(1.0f, ImGui::GetItemRectSize().y));
        float y = S->cam.yaw * 3.14159265f / 2048, p = S->cam.pitch * 3.14159265f / 2048;
        float right[3] = {cosf(y), 0, sinf(y)};
        float down[3] = {sinf(p) * sinf(y), cosf(p), -sinf(p) * cosf(y)};
        for (int k = 0; k < 3; k++) {
            S->cam.target[k] -= (right[k] * io.MouseDelta.x + down[k] * io.MouseDelta.y) * perPixel;
        }
        S->dirty |= io.MouseDelta.x != 0 || io.MouseDelta.y != 0;
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetItemKeyOwner(ImGuiKey_MouseWheelY);
        if (io.MouseWheel != 0) {
            S->cam.zoom = std::max(0.1f, std::min(S->cam.zoom * powf(1.15f, io.MouseWheel), 40.0f));
            S->dirty = true;
        }
        if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
            frame();
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Equal) || ImGui::IsKeyPressed(ImGuiKey_KeypadAdd)) {
            S->cam.zoom = std::min(S->cam.zoom * 1.25f, 40.0f);
            S->dirty = true;
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Minus) || ImGui::IsKeyPressed(ImGuiKey_KeypadSubtract)) {
            S->cam.zoom = std::max(S->cam.zoom / 1.25f, 0.1f);
            S->dirty = true;
        }
        if (ImGui::IsKeyPressed(ImGuiKey_0)) {
            frame();
        }
    }
    float sx = ImGui::GetKeyData(ImGuiKey_GamepadRStickRight)->AnalogValue -
               ImGui::GetKeyData(ImGuiKey_GamepadRStickLeft)->AnalogValue;
    float sy = ImGui::GetKeyData(ImGuiKey_GamepadRStickDown)->AnalogValue -
               ImGui::GetKeyData(ImGuiKey_GamepadRStickUp)->AnalogValue;
    float zt = ImGui::GetKeyData(ImGuiKey_GamepadR2)->AnalogValue - ImGui::GetKeyData(ImGuiKey_GamepadL2)->AnalogValue;
    if (fabsf(sx) > 0.2f || fabsf(sy) > 0.2f) {
        S->cam.yaw = (S->cam.yaw - (int)lrintf(sx * 40) + 8192) % 4096;
        S->cam.pitch = std::max(-1000, std::min(1000, S->cam.pitch + (int)lrintf(sy * 30)));
        S->dirty = true;
    }
    if (fabsf(zt) > 0.2f) {
        S->cam.zoom = std::max(0.1f, std::min(S->cam.zoom * (1.0f + zt * 0.04f), 40.0f));
        S->dirty = true;
    }
}

// The 3D view: the scene drawn by the game's code at the view's size (up to
// the GTE's 1024), shown fitted.
void sceneView(ImVec2 size) {
    if (S->view == nullptr) {
        S->view.reset(new View);
        S->view->setInteractive(false);
        S->view->setWholeFit(false); // rendered at most 1024 across, scaled up to fill a larger pane
        S->view->setBackground(IM_COL32(0, 0, 0, 255));
    }
    ImVec2 avail = size.x > 0 ? size : ImGui::GetContentRegionAvail();
    float k = std::min(1.0f, 1024.0f / std::max(1.0f, std::max(avail.x, avail.y)));
    int w = std::max(16, (int)(avail.x * k)), h = std::max(16, (int)(avail.y * k));
    if (w != S->cam.w || h != S->cam.h) {
        S->cam.w = w;
        S->cam.h = h;
        S->dirty = true;
    }
    S->cam.lightFollows = S->lightView;
    if (S->dirty && S->scene != nullptr) {
        xgSceneDraw(S->scene, &S->cam);
        S->dirty = false;
    }
    if (S->scene != nullptr) {
        S->view->show(xgSceneGpu(S->scene));
    }
    S->view->setScene(0, 0, (float)w, (float)h);
    S->view->setTextures(S->textures);
    S->view->setWireframe(S->edges);
    S->view->setReplacements(lookLibraries(XG_REPLACE_PACK)); // the Enhanced look: the texture pack
    S->view->draw(avail);
    cameraInput();
}

// Playback: game ticks at the game's rate as time passes.
bool animationEnded() {
    if (xgSceneBattling(S->scene)) {
        XgVmState vm;
        xgSceneVm(S->scene, &vm);
        return vm.stopped != 0;
    }
    return S->length > 0 && xgSceneFrame(S->scene) >= S->length;
}

void advance(double seconds) {
    if ((!S->playing || S->anim < 0) && S->morphRemaining <= 0) {
        return;
    }
    if (S->scene == nullptr) {
        return;
    }
    if (!std::isfinite(seconds) || seconds < 0) return;
    S->clock += seconds * stepRate(S->shownSrc);
    // Bound work per draw, retaining the debt when a slow host owes more.
    int ticks = (int)std::min(S->clock + 1e-9, 4.0);
    if (ticks <= 0) {
        return;
    }
    S->clock = std::max(0.0, S->clock - ticks);
    for (int i = 0; i < ticks; i++) {
        if (S->morphRemaining > 0) {
            xgSceneTick(S->scene, 1);
            S->morphRemaining--;
            continue;
        }
        if (animationEnded()) {
            if (!S->loop) {
                S->playing = false;
                S->clock = 0;
                break;
            }
            if (xgSceneBattling(S->scene)) {
                xgSceneRepeatBattling(S->scene);
            } else {
                double remainder = S->clock;
                playAnim(S->anim);
                S->clock = remainder;
            }
            S->playing = true;
        }
        xgSceneTick(S->scene, 1);
        if (!S->loop && animationEnded()) {
            S->playing = false;
            S->clock = 0;
            break;
        }
    }
    S->scrub = xgSceneFrame(S->scene);
    S->dirty = true;
}

void morphControls() {
    int count = xgSceneMorphCount(S->scene);
    if (count <= 0) {
        return;
    }
    ImGui::TextColored(skin::textWarning, tr("MIMe targets (%d)"), count);
    S->morphTarget = std::max(0, std::min(S->morphTarget, count - 1));
    ImGui::SetNextItemWidth(100 * unit());
    ImGui::SliderInt(tr("Target"), &S->morphTarget, 0, count - 1);
    int weight = xgSceneMorphWeight(S->scene, S->morphTarget);
    ImGui::SetNextItemWidth(100 * unit());
    if (ImGui::SliderInt(tr("Strength"), &weight, 0, 4096)) {
        xgSceneSetMorph(S->scene, S->morphTarget, weight, 0);
        S->morphRemaining = 0;
        S->dirty = true;
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip(tr("4096 is full strength. Field scripts choose these weights."));
    }
    ImGui::SetNextItemWidth(100 * unit());
    ImGui::SliderInt(tr("Transition ticks"), &S->morphTicks, 1, 120);
    if (ImGui::Button(tr("Blend in"))) {
        xgSceneSetMorph(S->scene, S->morphTarget, 4096, S->morphTicks);
        S->morphRemaining = S->morphTicks;
        S->clock = 0;
    }
    ImGui::SameLine();
    if (ImGui::Button(tr("Blend out"))) {
        xgSceneSetMorph(S->scene, S->morphTarget, 0, S->morphTicks);
        S->morphRemaining = S->morphTicks;
        S->clock = 0;
    }
    hint("GLB keeps these weights and adds a target clip. All exports a clip per target.");
}

// The transport: program, play, step, loop, the frame.
void transport() {
    float u = unit();
    if (ImGui::ArrowButton("##prev", ImGuiDir_Left)) {
        cycleAnimation(-1);
    }
    ImGui::SameLine();
    ImGui::Text("%02d/%02d", S->anim + 1, S->info.animations);
    ImGui::SameLine();
    if (ImGui::ArrowButton("##next", ImGuiDir_Right)) {
        cycleAnimation(1);
    }
    ImGui::SameLine();
    if (ImGui::Button(S->playing ? tr("Pause") : tr("Play"))) {
        S->playing = !S->playing;
    }
    ImGui::SameLine();
    if (ImGui::Button(tr("Step"))) {
        S->playing = false;
        S->clock = 0;
        xgSceneTick(S->scene, 1);
        S->dirty = true;
    }
    ImGui::SameLine();
    ImGui::Checkbox(tr("Loop"), &S->loop);
    int last = S->length > 0 ? S->length : std::max(300, xgSceneFrame(S->scene));
    S->scrub = xgSceneFrame(S->scene);
    char fmt[48];
    if (S->length > 0) {
        snprintf(fmt, sizeof(fmt), "%%d / %d", S->length);
    } else {
        snprintf(fmt, sizeof(fmt), "%%d%s", S->length == 0 ? tr(" (loops)") : "");
    }
    ImGui::SetNextItemWidth(std::max(60.0f, ImGui::GetContentRegionAvail().x - 4 * u));
    if (ImGui::SliderInt("##frame", &S->scrub, 0, last, fmt)) {
        S->playing = false;
        S->clock = 0;
        xgSceneSeek(S->scene, S->scrub);
        S->dirty = true;
    }
}

// The VM's state, read once a frame (each read swaps the sandbox in and out).
const XgVmState& vmNow() {
    static XgVmState vm;
    static unsigned long at = ~0ul;
    if (at != frameNumber() || S->dirty) {
        xgSceneVm(S->scene, &vm);
        at = frameNumber();
    }
    return vm;
}

// The VM's state this frame: the program counter and the words there, the
// wait and loop counters, the tracks running, the keyframe event frame.
void vmState(bool full) {
    const XgVmState& vm = vmNow();
    if (xgSceneBattling(S->scene)) {
        ImGui::Text(tr("Clip %d  tick %d  frame %d%s"), S->anim, xgSceneFrame(S->scene), vm.eventFrame, vm.stopped ? tr(" (end)") : "");
        if (full) {
            ImGui::Text(tr("Tracks %d  bones %d"), vm.tracks, vm.visibleBones);
            XgBattlingTiming timing = {};
            xgSceneBattlingTiming(S->scene, S->anim, &timing);
            hint(timing.native ? "Normal fight speed, stationary, %.2f ticks/s. Fight events are not played."
                               : "Raw clip, %.2f frames/s. This clip has no verified fight speed settings.",
                 stepRate(S->shownSrc));
        }
        return;
    }
    ImVec4 colour = vm.missingKeyframes ? skin::textError : skin::textHint;
    if (!full) {
        ImGui::TextColored(colour, tr("pc %s%04X op %02X wait %d tracks %d%s"), vm.pc < 0 ? "?" : "+",
                           vm.pc < 0 ? 0 : vm.pc * 2, vm.words[0] & 0xFF, vm.wait, vm.tracks,
                           vm.stopped ? " end" : "");
        return;
    }
    ImGui::Text(tr("Frame %d  pc %s%04X%s"), xgSceneFrame(S->scene), vm.pc < 0 ? "?" : "+", vm.pc < 0 ? 0 : vm.pc * 2,
                vm.stopped ? tr("  (end)") : "");
    ImGui::Text(tr("Op %02X %02X  %04X %04X %04X"), vm.words[0] & 0xFF, vm.words[0] >> 8, vm.words[1], vm.words[2],
                vm.words[3]);
    ImGui::Text(tr("Wait %d  loop %d  tracks %d"), vm.wait, vm.loops, vm.tracks);
    ImGui::Text(tr("Events %d  bones %d/%d"), vm.eventFrame, vm.visibleBones, std::max(0, S->info.bones - 1));
    XgSceneRig rigs[XG_SCENE_MAX_RIGS];
    int rigCount = xgSceneRigs(S->scene, rigs, XG_SCENE_MAX_RIGS), temporary = 0;
    for (int i = 0; i < rigCount; i++) temporary += rigs[i].slot >= 19 && rigs[i].slot < 31;
    if (rigCount > 1) ImGui::Text(tr("Rigs %d (%d temporary)"), rigCount, temporary);
    if (vm.suppressedOps) {
        ImGui::TextColored(skin::textWarning, tr("%d unsafe references omitted in this file"), vm.suppressedOps);
    }
    if (vm.missingKeyframes) {
        ImGui::TextColored(skin::textError, tr("Names keyframes its file lacks"));
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(tr("The game never runs this program with this file: keyframe 0 stands in."));
        }
    } else {
        ImGui::TextColored(skin::textHint, tr("%s VM, %.2f ticks/s"), xgSceneBattle(S->scene) ? tr("Battle") : tr("Field"),
                           stepRate(S->shownSrc));
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(xgSceneBattle(S->scene)
                                  ? tr("The battle's copy of the Gear code runs it (with no target, damage, camera or "
                                    "effect of a battle around it).")
                                  : tr("The field's Gear overlay runs it."));
        }
    }
}

void viewToggles() {
    if (ImGui::Checkbox(tr("Textures"), &S->textures)) {
        S->dirty = true;
    }
    ImGui::SameLine();
    ImGui::Checkbox(tr("Edges"), &S->edges);
    ImGui::SameLine();
    if (ImGui::Checkbox(tr("Light"), &S->lightView)) {
        S->dirty = true;
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip(tr("The light turns with the view (else it stays where the game's is)"));
    }
    if (S->info.bones > 0 && S->shownSrc != SRC_ARENAS && S->shownSrc != SRC_BATTLING) {
        ImGui::SameLine();
        if (ImGui::Checkbox(tr("Shadow"), &S->shadow)) {
            xgSceneSetShadow(S->scene, S->shadow);
            S->dirty = true;
        }
    }
}

bool shownOrHint() {
    if (S->scene != nullptr && xgSceneBuilt(S->scene)) {
        return true;
    }
    if (!S->why.empty()) {
        ImGui::TextColored(skin::textError, "%s", S->why.c_str());
    } else {
        hint(S->animations ? "Choose an animated model on the left. Its animations are the game's own programs, run by "
                             "the Gear overlay's animation VM, a game tick at a time."
                           : "Choose a model on the left: it is set up and drawn by the game's own model code.");
    }
    return false;
}

// The program as the VM reads it, op by op, the one at the VM's pc marked.
void listing() {
    if (xgSceneBattling(S->scene)) {
        hint("Battling clips contain bone channels and delta tracks. They have no Gear bytecode program.");
        return;
    }
    static uint16_t words[0x4000];
    static int n = 0;
    static const void* forScene = nullptr;
    static int forAnim = -1;
    static unsigned long forFrame = ~0ul;
    if (forScene != S->scene || forAnim != S->anim || forFrame != frameNumber()) {
        n = xgSceneProgram(S->scene, S->anim, words, 0x4000); // ops 0x30 and 0x31 keep counters in it
        forScene = S->scene;
        forAnim = S->anim;
        forFrame = frameNumber();
    }
    const XgVmState& vm = vmNow();
    struct Line {
        int at;
        char text[96];
        bool stops;
    };
    std::vector<Line> lines;
    int at = 0;
    while (at < n && lines.size() < 512) {
        Line l;
        l.at = at;
        int len = xgMechaDisasm(words + at, n - at, xgSceneBattle(S->scene), l.text, sizeof(l.text));
        l.stops = len <= 0;
        lines.push_back(l);
        if (len <= 0) {
            break;
        }
        at += len;
    }
    ImGui::BeginChild("##listing", ImVec2(0, 0), ImGuiChildFlags_NavFlattened);
    for (const Line& l : lines) {
        bool here = l.at == vm.pc;
        ImVec4 c = here ? skin::textWarning : l.stops ? skin::textError : ImGui::GetStyleColorVec4(ImGuiCol_Text);
        ImGui::TextColored(c, "%s%04X %s", here ? ">" : " ", l.at * 2, l.text);
        if (here && S->playing) {
            ImGui::SetScrollHereY(0.4f);
        }
    }
    ImGui::EndChild();
}

void texturesTab() {
    if (S->scene == nullptr || !xgSceneBuilt(S->scene)) {
        hint("Choose a model first.");
        return;
    }
    if (S->pages.empty()) {
        S->pages.resize(64);
        S->pages.resize((size_t)xgScenePages(S->scene, S->pages.data(), 64));
        S->page = 0;
    }
    if (S->pages.empty()) {
        hint("No textured polygons.");
        return;
    }
    float u = unit();
    float listH = std::min(6.0f, (float)S->pages.size()) * ImGui::GetTextLineHeightWithSpacing() + 4 * u;
    ImGui::BeginChild("##pages", ImVec2(0, listH), ImGuiChildFlags_NavFlattened);
    for (int i = 0; i < (int)S->pages.size(); i++) {
        const XgScenePage& p = S->pages[(size_t)i];
        int depth = p.tpage >> 7 & 3;
        char label[160];
        snprintf(label, sizeof(label), tr("Page (%d, %d) %d-bit, CLUT (%d, %d): %d polygons%s"), (p.tpage & 15) * 64,
                 (p.tpage >> 4 & 1) * 256, depth == 0 ? 4 : depth == 1 ? 8 : 15, (p.clut & 63) * 16,
                 p.clut >> 6 & 511, p.polygons, p.semi ? tr(", semi-transparent") : "");
        ImGui::PushID(i);
        if (listItem(label, S->page == i)) {
            S->page = i;
            sound(1);
        }
        ImGui::PopID();
    }
    ImGui::EndChild();
    if (ImGui::Button(tr("Export PNG"))) {
        exportPage();
    }
    ImGui::SameLine();
    hint("The page through its CLUT, from the sandbox's VRAM; the frame marks the texels the polygons use.");
    if (S->pageView == nullptr) {
        S->pageView.reset(new View);
        S->pageView->setBackground(IM_COL32(40, 40, 52, 255));
    }
    const XgScenePage& p = S->pages[(size_t)S->page];
    XgPrim prims[5] = {};
    XgPrim& q = prims[0];
    q.count = 4;
    q.flags = XG_PRIM_TEXTURED | XG_PRIM_RAW | XG_PRIM_FREE_UV;
    q.tpage = (uint16_t)(p.tpage & 0x19F);
    q.clut = p.clut;
    for (int k = 0; k < 4; k++) {
        q.x[k] = q.u[k] = (int16_t)(k & 1 ? 256 : 0);
        q.y[k] = q.v[k] = (int16_t)(k & 2 ? 256 : 0);
        q.r[k] = q.g[k] = q.b[k] = 128;
    }
    q.area[2] = q.area[3] = 255;
    // The used texels' frame: four lines.
    int fx[5] = {p.u0, p.u1 + 1, p.u1 + 1, p.u0, p.u0}, fy[5] = {p.v0, p.v0, p.v1 + 1, p.v1 + 1, p.v0};
    for (int l = 0; l < 4; l++) {
        XgPrim& e = prims[1 + l];
        e.count = 2;
        e.flags = XG_PRIM_LINE;
        e.x[0] = (int16_t)fx[l];
        e.y[0] = (int16_t)fy[l];
        e.x[1] = (int16_t)fx[l + 1];
        e.y[1] = (int16_t)fy[l + 1];
        e.r[0] = e.r[1] = 255;
        e.g[0] = e.g[1] = 200;
        e.area[2] = e.area[3] = 256;
    }
    XgVgpu* gpu = xgSceneGpu(S->scene);
    S->pageView->setPrims(prims, 5);
    S->pageView->setVram(xgVgpuVram(gpu), xgVgpuVramVersion(gpu));
    S->pageView->setScene(0, 0, 256, 256);
    S->pageView->draw();
}

void exportRow() {
    if (ImGui::Button(tr("Export glTF"))) {
        exportGlb(S->exportAll);
    }
    ImGui::SameLine();
    if (S->info.animations > 0 || xgSceneMorphCount(S->scene) > 0) {
        ImGui::Checkbox(tr("All"), &S->exportAll);
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(tr("Every program and MIMe target (else the current program and selected target).\n"
                              "MIMe clips go from zero to full weight and back, using Transition ticks."));
        }
        ImGui::SameLine();
    }
    if (ImGui::Button(tr("Folder"))) {
        openExportDir();
    }
}

void modelOptions();

void detailPane(ImVec2 min, ImVec2 max) {
    ImVec2 pictureMax(max.x, max.y - 56 * unit());
    if (beginPane("##model", min, pictureMax, nullptr, false)) {
        if (shownOrHint()) {
            sceneView(ImGui::GetContentRegionAvail());
            advance(ImGui::GetIO().DeltaTime);
        }
    }
    endPane();
    if (beginPane("##model-details", ImVec2(min.x, pictureMax.y + 12 * unit()), max, nullptr, true)) {
        ImGui::TextWrapped("%s", S->shown.title.c_str());
        if (S->info.animations > 0) ImGui::Text(tr("Animation %02d/%02d"), S->anim + 1, S->info.animations);
        else ImGui::Text(tr("%d parts  %d polygons"), S->info.parts, S->info.polygons);
    }
    endPane();
    modelOptions();
}

// The Options window: everything about the model shown.
void modelOptions() {
    bool options = libraryOptions("modeloptions", S->optionsRequested, 360, 340);
    if (!options && S->optionsOpen && S->shownSrc >= 0) {
        S->focusParent = itemKey(S->shown);
        ImGui::SetNavCursorVisibleAfterMove();
    }
    S->optionsOpen = options;
    if (options) {
        if (ImGui::Button(tr("Close"))) ImGui::CloseCurrentPopup();
        if (shownOrHint()) {
            if (ImGui::Button(tr("Bookmark"))) bookmarkSelection();
            ImGui::TextWrapped("%s", S->shown.title.c_str());
            ImGui::Text(tr("%s disc %d"), xgDiscRegion(disc()), xgDiscNumber(disc()));
            ImGui::TextWrapped(tr("%d parts, %d polygons, %d bones, %d animations"), S->info.parts, S->info.polygons, std::max(0, S->info.bones - 1), S->info.animations);
            if (S->info.animations > 0) transport();
            morphControls();
            viewToggles();
            lookOptions(XG_REPLACE_PACK, lookCovered(S->view.get(), XG_REPLACE_PACK), false, N_("No texture pack installed"));
            exportRow();
            if (S->info.animations > 0 && ImGui::CollapsingHeader(tr("Animation details"))) vmState(true);
            if (S->info.animations > 0 && ImGui::CollapsingHeader(tr("Bytecode"))) {
                ImGui::BeginChild("##code", ImVec2(0, 100 * unit())); listing(); ImGui::EndChild();
            }
            if (S->wantTab >= 0) ImGui::SetNextItemOpen(S->wantTab == 1);
            if (ImGui::CollapsingHeader(tr("Textures"))) {
                ImGui::BeginChild("##textures", ImVec2(0, 240 * unit())); texturesTab(); ImGui::EndChild();
            }
            S->wantTab = -1;
        }
        ImGui::EndPopup();
    }
}

// ---- the viewers ------------------------------------------------------------------------------

void openWith(bool animations) {
    S = new State;
    S->animations = animations;
    for (int i = 0; i < 2; i++) {
        const char* p = cdSwapPath(i);
        if (p != nullptr && p[0] != '\0') {
            const char* why = nullptr;
            XgDisc* d = xgDiscOpen(p, &why);
            if (d != nullptr) {
                S->discs.push_back(d);
            }
        }
    }
    std::sort(S->discs.begin(), S->discs.end(), [](XgDisc* a, XgDisc* b) { return xgDiscNumber(a) < xgDiscNumber(b); });
    if (S->discs.empty()) {
        error("No Xenogears disc image found");
    }
    S->cam.w = 640;
    S->cam.h = 480;
    S->cam.zoom = 1;
    setSource(SRC_GEARS);
}

void openedModels() {
    openWith(false);
}

void openedAnimations() {
    openWith(false); // compatibility alias: models always expose their animations
}

void closeState() {
    xgSceneDestroy(S->scene);
    for (XgDisc* d : S->discs) {
        xgDiscClose(d);
    }
    delete S; // its views go with it
    S = nullptr;
}

void closed() { slots.close(S, closeState); }

// ---- the focused view: the model alone across the area, its controls in a bar ----------------------

void setFocused(bool on) {
    if (on == S->focused || (on && !(S->scene && xgSceneBuilt(S->scene)))) return;
    S->focused = on;
    if (on) {
        S->focusKey = itemKey(S->shown);
        frame(); // fitted to the new space
        focusEnter();
        return;
    }
    if (S->shownSrc >= 0 && itemKey(S->shown) != S->focusKey) S->focusParent = itemKey(S->shown);
    else S->focusParent.clear();
    focusLeave();
}

void focusedView(const Area& a) {
    if (S->characterStep) { // Up/Down (L1/R1): the list's next model, as its pane steps it
        browseCharacter(S->characterStep);
        S->characterStep = 0;
    }
    if (!(S->scene && xgSceneBuilt(S->scene))) { setFocused(false); return; }
    FocusLayout f = focusLayout(a, 1);
    if (beginPane("##model-focus", f.min, f.max, nullptr, false)) {
        sceneView(ImGui::GetContentRegionAvail());
        focusPicture(S->shown.title.c_str());
        advance(ImGui::GetIO().DeltaTime);
    }
    endPane();
    if (beginFocusBar(f)) setFocused(false);
    if (S->info.animations > 0) {
        if (focusButton("<")) cycleAnimation(-1);
        focusText("%s %02d/%02d", tr("Animation"), S->anim + 1, S->info.animations);
        if (focusButton(">")) cycleAnimation(1);
        if (focusButton(S->playing ? "Pause" : "Play")) S->playing = !S->playing;
        if (focusButton("Step")) {
            S->playing = false;
            S->clock = 0;
            xgSceneTick(S->scene, 1);
            S->dirty = true;
        }
    } else focusText(tr("%d parts  %d polygons"), S->info.parts, S->info.polygons);
    switch (focusZoomButtons(false)) { // a camera: no picture pixels for a 1:1
    case 1: frame(); break;
    case 2: S->cam.zoom = std::max(S->cam.zoom / 1.25f, 0.1f); S->dirty = true; break;
    case 3: S->cam.zoom = std::min(S->cam.zoom * 1.25f, 40.0f); S->dirty = true; break;
    default: break;
    }
    focusLook(XG_REPLACE_PACK, lookCovered(S->view.get(), XG_REPLACE_PACK), N_("No texture pack installed"));
    if (focusButton("Options")) S->optionsRequested = true;
    endFocusBar();
    modelOptions();
}

void draw(const Area& a) {
    if (S->focused) {
        focusedView(a);
        return;
    }
    float u = a.unit;
    float split = floorf(fminf(150 * u, (a.max.x - a.min.x) * 0.34f) / u) * u;
    listPane(a.min, ImVec2(a.min.x + split, a.max.y));
    detailPane(ImVec2(a.min.x + split + 16 * u, a.min.y), a.max);
    // Confirm on a row: that model in the focused view, once it is built.
    if (S->focusRequested) {
        S->focusRequested = false;
        setFocused(true);
    }
}

bool back() {
    if (!S->focused) return false;
    setFocused(false);
    return true;
}

bool onOff(const char* arg) {
    return strcmp(arg, "off") != 0 && strcmp(arg, "0") != 0;
}

bool command(const char* cmd, const char* arg) {
    if (!strcmp(cmd, "section")) {
        for (int src = 0; src < SRC_COUNT; ++src) if (!strcmp(arg, sourceIds[src])) {
            if (slots.select(arg, S, openedModels)) setSource(src);
            S->flat = true; return true;
        }
        return false;
    }
    if (!strcmp(cmd, "character")) { S->characterStep = atoi(arg); return true; }
    if (!strcmp(cmd, "cycle")) { cycleAnimation(atoi(arg)); return true; }
    if (!strcmp(cmd, "options")) { S->optionsRequested = true; return true; }
    if (!strcmp(cmd, "focus")) { setFocused(onOff(arg)); return S->focused == onOff(arg); }
    if (!strcmp(cmd, "toggle")) { if (S->scene) S->playing = !S->playing; return true; }
    if (!strcmp(cmd, "fit")) { if (S->scene) frame(); return true; }
    if (strcmp(cmd, "image") == 0) {
        int i = addDisc(arg);
        if (i < 0) return false;
        S->exactDisc = true;
        selectDisc(i, true);
        return true;
    }
    bool built = S->scene != nullptr && xgSceneBuilt(S->scene);
    if (!built && (strcmp(cmd, "seek") == 0 || strcmp(cmd, "step") == 0 || strcmp(cmd, "anim") == 0)) {
        fprintf(stderr, "extras: models: %s: no model shown\n", cmd);
        return true;
    }
    if (strcmp(cmd, "source") == 0) {
        for (int s = 0; s < SRC_COUNT; s++) {
            if (strcmp(arg, sourceIds[s]) == 0) {
                if (S->source != s) setSource(s);
                return true;
            }
        }
        return false;
    }
    if (strcmp(cmd, "disc") == 0) {
        int n = atoi(arg);
        for (int i = 0; i < (int)S->discs.size(); i++) {
            if (xgDiscNumber(S->discs[(size_t)i]) == n) {
                S->exactDisc = true;
                selectDisc(i, true);
                return true;
            }
        }
        return false;
    }
    if (strcmp(cmd, "item") == 0) {
        int n = atoi(arg);
        S->scrollTo = n;
        return choose(n);
    }
    if (strcmp(cmd, "field") == 0) {
        int n = 0, k = 0;
        if (sscanf(arg, "%d %d", &n, &k) != 2) {
            return false;
        }
        setSource(SRC_FIELDS);
        if (n < 0 || n >= (int)S->fieldOpen.size()) {
            return false;
        }
        S->fieldOpen[(size_t)n] = true;
        std::vector<Item> rows = fieldRows();
        for (int r = 0; r < (int)rows.size(); r++) {
            if (!rows[(size_t)r].fieldRow && rows[(size_t)r].a == n && rows[(size_t)r].b == k) {
                S->scrollTo = r;
                return choose(r);
            }
        }
        return false;
    }
    if (strcmp(cmd, "world") == 0) {
        int c = 0, k = 0;
        if (sscanf(arg, "%d %d", &c, &k) != 2) {
            return false;
        }
        setSource(SRC_WORLD);
        if (c < 0 || c >= (int)S->worldOpen.size()) {
            return false;
        }
        S->worldOpen[(size_t)c] = true;
        std::vector<Item> rows = worldRows();
        for (int r = 0; r < (int)rows.size(); r++) {
            if (!rows[(size_t)r].fieldRow && rows[(size_t)r].a == c && rows[(size_t)r].b == k) {
                S->scrollTo = r;
                return choose(r);
            }
        }
        return false;
    }
    if (strcmp(cmd, "anim") == 0) {
        playAnim(atoi(arg));
        S->playing = false;
        return true;
    }
    if (strcmp(cmd, "play") == 0 || strcmp(cmd, "pause") == 0) {
        S->playing = cmd[1] == 'l';
        return true;
    }
    if (strcmp(cmd, "seek") == 0) {
        S->playing = false;
        S->clock = 0;
        xgSceneSeek(S->scene, atoi(arg));
        S->dirty = true;
        return true;
    }
    if (strcmp(cmd, "morph") == 0) {
        int target = 0, weight = 0, ticks = 0;
        if (sscanf(arg, "%d %d %d", &target, &weight, &ticks) < 2 || S->scene == nullptr) {
            return false;
        }
        xgSceneSetMorph(S->scene, target, weight, ticks);
        S->morphTarget = target;
        S->morphRemaining = 0; /* scripts advance with `step` */
        S->dirty = true;
        return true;
    }
    if (strcmp(cmd, "morphticks") == 0) {
        S->morphTicks = std::max(1, std::min(120, atoi(arg)));
        return true;
    }
    if (strcmp(cmd, "step") == 0) {
        S->playing = false;
        S->clock = 0;
        xgSceneTick(S->scene, arg[0] != '\0' ? atoi(arg) : 1);
        S->dirty = true;
        return true;
    }
    if (strcmp(cmd, "loop") == 0) {
        S->loop = onOff(arg);
        return true;
    }
    if (strcmp(cmd, "elapsed") == 0) {
        // Feed the real playback clock without depending on the test host's
        // render speed. Subsequent UI draws stay paused; fractional time and
        // pending ticks carry between calls exactly as in normal playback.
        S->playing = true;
        advance(atof(arg));
        S->playing = false;
        return true;
    }
    if (strcmp(cmd, "view") == 0) {
        float zoom = S->cam.zoom;
        sscanf(arg, "%d %d %f", &S->cam.yaw, &S->cam.pitch, &zoom);
        S->cam.zoom = zoom;
        S->dirty = true;
        return true;
    }
    if (strcmp(cmd, "tab") == 0) {
        S->tab = S->wantTab = strcmp(arg, "textures") == 0 ? 1 : 0;
        if (S->tab == 1) S->optionsRequested = true;
        return true;
    }
    if (strcmp(cmd, "page") == 0) {
        S->page = std::max(0, std::min(atoi(arg), (int)S->pages.size() - 1));
        return true;
    }
    if (strcmp(cmd, "textures") == 0 || strcmp(cmd, "edges") == 0 || strcmp(cmd, "shadow") == 0) {
        bool on = onOff(arg);
        (cmd[0] == 't' ? S->textures : cmd[0] == 'e' ? S->edges : S->shadow) = on;
        if (S->scene != nullptr) {
            xgSceneSetShadow(S->scene, S->shadow);
        }
        S->dirty = true;
        return true;
    }
    if (strcmp(cmd, "light") == 0) {
        S->lightView = strcmp(arg, "view") == 0;
        S->dirty = true;
        return true;
    }
    if (strcmp(cmd, "export") == 0) {
        if (strcmp(arg, "glb") == 0 || strcmp(arg, "glball") == 0) {
            exportGlb(strcmp(arg, "glball") == 0);
        } else if (strcmp(arg, "png") == 0) {
            if (S->pages.empty() && S->scene != nullptr) {
                S->pages.resize(64);
                S->pages.resize((size_t)xgScenePages(S->scene, S->pages.data(), 64));
            }
            exportPage();
        } else {
            return false;
        }
        return true;
    }
    if (strcmp(cmd, "vmscan") == 0) {
        // Every model of the list: each program decoded op by op as one copy
        // of the VM reads it ("battle": the battle's); where the decoding
        // stops, and how many programs use each op.
        int src = S->source, total = 0, clean = 0, battleOps = 0, stops = 0;
        int battle = strcmp(arg, "battle") == 0;
        std::vector<int> with(256, 0);
        std::vector<Item> rows = rowsFor(src);
        static uint16_t words[0x4000];
        for (const Item& it : rows) {
            if (it.fieldRow || !load(it)) {
                continue;
            }
            for (int p = 0; p < S->info.animations; p++) {
                int n = xgSceneProgram(S->scene, p, words, 0x4000);
                int at = 0, sawBattle = 0;
                std::vector<bool> seen(256, false);
                while (at < n) {
                    int op = words[at] & 0xFF;
                    int len = xgMechaOpLength(words + at, n - at, battle);
                    if (len <= 0) {
                        break;
                    }
                    sawBattle |= xgMechaOpBattleOnly(op);
                    if (!seen[(size_t)op]) {
                        seen[(size_t)op] = true;
                        with[(size_t)op]++;
                    }
                    at += len;
                }
                total++;
                clean += at >= n;
                stops += at < n;
                battleOps += sawBattle;
                if (at < n) {
                    fprintf(stderr, "extras: vmscan: %s program %d: stops at +%04X (op %02X) of %d words\n",
                            it.title.c_str(), p, at * 2, words[at] & 0xFF, n);
                }
            }
        }
        fprintf(stderr, "extras: vmscan: %s: %d programs, %d decoded to their end, %d stop, %d with battle-only ops\n",
                sourceIds[src], total, clean, stops, battleOps);
        std::string h;
        for (int op = 0; op < 256; op++) {
            if (with[(size_t)op] > 0) {
                char b[24];
                snprintf(b, sizeof(b), " %02X:%d", op, with[(size_t)op]);
                h += b;
            }
        }
        fprintf(stderr, "extras: vmscan: programs using each op:%s\n", h.c_str());
        return true;
    }
    if (strcmp(cmd, "rigscan") == 0) {
        int ticks = 120, first = 0, limit = 1, row = 0, candidates = 0;
        sscanf(arg, "%d %d %d", &ticks, &first, &limit);
        ticks = std::max(1, std::min(ticks, 3000));
        for (const Item& it : rowsFor(S->source)) {
            if (it.fieldRow || row++ < first) continue;
            if (limit-- <= 0) break;
            if (!load(it)) continue;
            for (int a = 0; a < S->info.animations; a++) {
                uint16_t words[0x4000];
                int n = xgSceneProgram(S->scene, a, words, 0x4000), at = 0;
                bool split = false;
                while (at < n) {
                    int length = xgMechaOpLength(words + at, n - at, xgSceneBattle(S->scene));
                    if (length <= 0) break;
                    split |= (words[at] & 0xff) == 0x15;
                    at += length;
                }
                if (!split) continue;
                candidates++;
                char why[160];
                prepareExtra(S->scene, a);
                if (!xgScenePlay(S->scene, a, why, sizeof(why))) continue;
                int peak = 0, firstSpawn = -1;
                for (int f = 0; f <= ticks; f++) {
                    XgSceneRig rigs[XG_SCENE_MAX_RIGS];
                    if (f > 0) xgSceneTick(S->scene, 1);
                    int count = xgSceneRigs(S->scene, rigs, XG_SCENE_MAX_RIGS);
                    peak = std::max(peak, count);
                    for (int k = 0; k < count; k++) {
                        if (rigs[k].slot >= 19 && rigs[k].slot < 31 && firstSpawn < 0) firstSpawn = f;
                    }
                }
                fprintf(stderr, "extras: rigscan: %s program %d: peak %d rigs, first spawn %d, extra 2A/%03X\n",
                        it.title.c_str(), a, peak, firstSpawn, xgSceneProgramExtraFile(S->scene, a));
            }
        }
        fprintf(stderr, "extras: rigscan: %d programs with direct splits sampled\n", candidates);
        S->playing = false;
        S->dirty = true;
        return true;
    }
    if (strcmp(cmd, "stress") == 0) {
        // Every model of the list, every program played `arg` ticks (the
        // VM's robustness in the sandbox): how many stop, loop, miss keyframes.
        int ticks = 120, first = 0, limit = 0x7FFFFFFF;
        sscanf(arg, "%d %d %d", &ticks, &first, &limit);
        int programs = 0, stopped = 0, missing = 0, models = 0, row = 0;
        for (const Item& it : rowsFor(S->source)) {
            if (row++ < first) {
                continue;
            }
            if (models >= limit) {
                break;
            }
            if (it.fieldRow || !load(it)) {
                continue;
            }
            models++;
            for (int p = 0; p < S->info.animations; p++) {
                char why[128];
                fprintf(stderr, "extras: stress: %s program %d\n", it.title.c_str(), p);
                prepareExtra(S->scene, p);
                if (!xgScenePlay(S->scene, p, why, sizeof(why))) {
                    continue;
                }
                xgSceneTick(S->scene, ticks);
                XgVmState vm;
                xgSceneVm(S->scene, &vm);
                programs++;
                stopped += vm.stopped;
                missing += vm.missingKeyframes;
            }
        }
        fprintf(stderr, "extras: stress: %s: %d models, %d programs played %d ticks: %d at their end, %d naming "
                        "keyframes their files lack\n",
                sourceIds[S->source], models, programs, ticks, stopped, missing);
        return true;
    }
    if (strcmp(cmd, "disasm") == 0) {
        // A program as the running copy of the VM reads it, on stderr.
        static uint16_t words[0x4000];
        int anim = atoi(arg), battle = S->scene != nullptr && xgSceneBattle(S->scene);
        int n = S->scene != nullptr ? xgSceneProgram(S->scene, anim, words, 0x4000) : 0;
        fprintf(stderr, "extras: disasm: program %d, %d words, %s VM; extra file %d\n", anim, n,
                battle ? "battle" : "field", S->scene != nullptr ? xgSceneProgramExtraFile(S->scene, anim) : -1);
        for (int at = 0; at < n;) {
            char text[128];
            int len = xgMechaDisasm(words + at, n - at, battle, text, sizeof(text));
            fprintf(stderr, "extras: disasm:   %04X  %s\n", at * 2, text);
            if (len <= 0) {
                break;
            }
            at += len;
        }
        return true;
    }
    if (strcmp(cmd, "vram") == 0) {
        // The scene's VRAM (1024 x 512 halfwords, as XG_DUMP_VRAM's .bin) for tests.
        FILE* f = S->scene != nullptr ? fopen(arg, "wb") : nullptr;
        if (f != nullptr) {
            fwrite(xgVgpuVram(xgSceneGpu(S->scene)), 2, XG_VRAM_W * XG_VRAM_H, f);
            fclose(f);
        }
        fprintf(stderr, "extras: models: VRAM %s %s\n", f != nullptr ? "written to" : "not written:", arg);
        return true;
    }
    if (strcmp(cmd, "info") == 0) {
        fprintf(stderr, "extras: models: playback playing %d loop %d\n", S->playing, S->loop);
        fprintf(stderr, "extras: models: source %s disc %d: %s; field %d section %d member %d\n",
                xgDiscRegion(disc()), xgDiscNumber(disc()), xgDiscPath(disc()),
                S->shownSrc == SRC_FIELDS ? S->shown.a : -1, S->shownSrc == SRC_FIELDS ? 2 : -1,
                S->shownSrc == SRC_FIELDS ? S->shown.b : -1);
        if (S->scene == nullptr || !xgSceneBuilt(S->scene)) {
            fprintf(stderr, "extras: models: nothing shown%s%s\n", S->why.empty() ? "" : ": ", S->why.c_str());
            return true;
        }
        fprintf(stderr, "extras: models: camera yaw %d pitch %d zoom %.3f\n", S->cam.yaw, S->cam.pitch, S->cam.zoom);
        XgVmState vm;
        xgSceneVm(S->scene, &vm);
        int drawn = xgSceneDraw(S->scene, &S->cam);
        fprintf(stderr, "extras: models: %d primitives drawn (screen %dx%d, distance %.0f, target %.0f %.0f %.0f)\n",
                drawn, S->cam.w, S->cam.h, (double)S->cam.distance, (double)S->cam.target[0],
                (double)S->cam.target[1], (double)S->cam.target[2]);
        fprintf(stderr,
                "extras: models: %s: %d parts, %d polygons, %d bones, %d animations; bounds (%.0f %.0f %.0f)-(%.0f "
                "%.0f %.0f); animation %d frame %d length %d; vm pc %d op %04X wait %d loops %d tracks %d stopped %d "
                "event %d bones shown %d missing keyframes %d\n",
                S->shown.title.c_str(), S->info.parts, S->info.polygons, S->info.bones, S->info.animations,
                (double)S->info.min[0], (double)S->info.min[1], (double)S->info.min[2], (double)S->info.max[0],
                (double)S->info.max[1], (double)S->info.max[2], S->anim, xgSceneFrame(S->scene), S->length, vm.pc,
                vm.words[0], vm.wait, vm.loops, vm.tracks, vm.stopped, vm.eventFrame, vm.visibleBones,
                vm.missingKeyframes);
        if (xgSceneBattling(S->scene)) {
            XgBattlingTiming timing = {};
            xgSceneBattlingTiming(S->scene, S->anim, &timing);
            fprintf(stderr, "extras: models: timing %s frames %d speed %d/16 blend %d; clock %.9f ticks/s %.9f\n",
                    timing.native ? "native" : "raw", timing.frames, timing.speed, timing.blend,
                    S->clock, stepRate(S->shownSrc));
        }
        int morphs = xgSceneMorphCount(S->scene);
        if (morphs > 0) {
            fprintf(stderr, "extras: models: %d MIMe targets, weights", morphs);
            for (int i = 0; i < morphs; i++) {
                fprintf(stderr, " %d", xgSceneMorphWeight(S->scene, i));
            }
            fprintf(stderr, "\n");
        }
        return true;
    }
    if (strcmp(cmd, "pose") == 0) {
        XgSceneBone bones[256];
        int count = S->scene != nullptr ? xgSceneBones(S->scene, bones, 256) : 0;
        FILE* f = fopen(arg, "w");
        if (f == nullptr) {
            return true;
        }
        fprintf(f, "{\"frame\":%d,\"bones\":[", S->scene != nullptr ? xgSceneFrame(S->scene) : 0);
        for (int i = 0; i < count; i++) {
            XgSceneBone& b = bones[i];
            fprintf(f, "%s{\"parent\":%d,\"enabled\":%d,\"m\":[", i ? "," : "", b.parent, b.enabled);
            for (int r = 0; r < 3; r++) {
                fprintf(f, "%s[%.9g,%.9g,%.9g]", r ? "," : "", b.m[r][0], b.m[r][1], b.m[r][2]);
            }
            fprintf(f, "],\"t\":[%.9g,%.9g,%.9g]}", b.t[0], b.t[1], b.t[2]);
        }
        fprintf(f, "],\"morph_parts\":[");
        if (S->scene != nullptr && xgSceneBuilt(S->scene)) {
            XgScenePart parts[512];
            int n = xgSceneParts(S->scene, parts, 512), first = 1;
            xgSceneEnter(S->scene);
            for (int p = 0; p < n; p++) {
                int targets[512];
                int m = xgScenePartMorphs(S->scene, parts[p].block, targets, 512);
                if (m <= 0) continue;
                int v = xgScenePartVertices(parts[p].block, nullptr, 0);
                std::vector<float> xyz((size_t)v * 3);
                xgScenePartVertices(parts[p].block, xyz.data(), v);
                fprintf(f, "%s{\"part\":%d,\"targets\":[", first ? "" : ",", p);
                first = 0;
                for (int k = 0; k < m; k++) fprintf(f, "%s%d", k ? "," : "", targets[k]);
                fprintf(f, "],\"weights\":[");
                for (int k = 0; k < m; k++) fprintf(f, "%s%d", k ? "," : "", xgSceneMorphWeight(S->scene, targets[k]));
                fprintf(f, "],\"vertices\":[");
                for (int k = 0; k < v; k++) fprintf(f, "%s[%.9g,%.9g,%.9g]", k ? "," : "", xyz[3*k], xyz[3*k+1], xyz[3*k+2]);
                fprintf(f, "]}");
            }
            xgSceneLeave(S->scene);
        }
        fprintf(f, "],\"rigs\":[");
        if (S->scene != nullptr && xgSceneBuilt(S->scene)) {
            XgSceneRig rigs[XG_SCENE_MAX_RIGS];
            int n = xgSceneRigs(S->scene, rigs, XG_SCENE_MAX_RIGS);
            for (int p = 0; p < n; p++) {
                XgSceneBone rigBones[256];
                int v = xgSceneRigBones(S->scene, &rigs[p], rigBones, 256);
                fprintf(f, "%s{\"slot\":%d,\"generation\":%u,\"source\":%d,\"bones\":[", p ? "," : "",
                        rigs[p].slot, rigs[p].generation, rigs[p].source);
                for (int k = 0; k < v; k++) {
                    const XgSceneBone& b = rigBones[k];
                    fprintf(f, "%s{\"parent\":%d,\"geometry\":%d,\"enabled\":%d,\"m\":[", k ? "," : "", b.parent, b.geometry, b.enabled);
                    for (int r = 0; r < 3; r++) fprintf(f, "%s[%.9g,%.9g,%.9g]", r ? "," : "", b.m[r][0], b.m[r][1], b.m[r][2]);
                    fprintf(f, "],\"t\":[%.9g,%.9g,%.9g]}", b.t[0], b.t[1], b.t[2]);
                }
                fprintf(f, "]}");
            }
        }
        fprintf(f, "]}\n");
        fclose(f);
        return true;
    }
    return false;
}

bool bookmark(Bookmark& b) {
    if (!S->scene || S->shownSrc < 0 || !bookmarkSource(b, disc(), S->animations ? "animations" : "models", S->shownName)) return false;
    b.target.commands.push_back({"source", sourceIds[S->shownSrc]});
    if (S->shownSrc == SRC_FIELDS || S->shownSrc == SRC_WORLD) {
        b.target.commands.push_back({S->shownSrc == SRC_FIELDS ? "field" : "world",
                                    std::to_string(S->shown.a) + " " + std::to_string(S->shown.b)});
    } else {
        auto rows = rowsFor(S->shownSrc); int index = -1;
        for (size_t i = 0; i < rows.size(); ++i)
            if (rows[i].a == S->shown.a && rows[i].b == S->shown.b && rows[i].c == S->shown.c) { index = (int)i; break; }
        if (index < 0) return false;
        b.target.commands.push_back({"item", std::to_string(index)});
    }
    if (S->anim >= 0) {
        b.target.commands.push_back({"anim", std::to_string(S->anim)});
        b.target.commands.push_back({"seek", std::to_string(std::min(10000, xgSceneFrame(S->scene)))});
    }
    char view[96]; snprintf(view, sizeof(view), "%d %d %.6f", S->cam.yaw, S->cam.pitch, S->cam.zoom);
    b.target.commands.push_back({"view", view});
    b.target.commands.push_back({"textures", S->textures ? "on" : "off"});
    b.target.commands.push_back({"edges", S->edges ? "on" : "off"});
    b.target.commands.push_back({"shadow", S->shadow ? "on" : "off"});
    b.target.commands.push_back({"light", S->lightView ? "view" : "fixed"});
    b.target.commands.push_back({"tab", S->tab == 1 ? "textures" : "model"});
    b.target.commands.push_back({"page", std::to_string(S->page)});
    b.target.commands.push_back({"pause", ""}); return true;
}
View* picture() { return S->tab == 1 ? S->pageView.get() : S->view.get(); }

const Viewer models = {"models", "Model viewer",
                       "Gears, enemies, arenas and field models, by the game's own code; glTF export", 30,
                       openedModels, closed, draw, back, command, nullptr, nullptr, bookmark, picture};
const Viewer animations = {"animations", "Animation viewer",
                           "Gear programs, Battling clips and field MIMe targets through the game's decoders", 40,
                           openedAnimations, closed, draw, back, command, nullptr, nullptr, bookmark, picture};

} // namespace

XG_EXTRAS_VIEWER(models);
XG_EXTRAS_VIEWER(animations);
