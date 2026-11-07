// The bestiary (xg-htn.35.25): every enemy of the player's discs as the
// battle loader reads it (bestiary_data.h), one entry per monster (the same
// record, name and picture in several enemy sets are one), where it appears
// (the fields' random tables and story battles, the world map's zones), its
// numbers, weaknesses, drops, attacks and the lines its own scripts show.
//
// Everything is the game's own data; what the game does not have is said
// (no enemy descriptions; a story chapter that is an estimate). Element
// names: the battle's own messages pair the bits with Wind, Earth, Fire and
// Water (setStatusEffectMessage type 9, src/battle/b_800941A4.c; 0C/03
// member 38), and Billy's EthLightAmmo and EthDark Ammo carry 0x20 and 0x10
// (their names only: marked).
//
// Two ways to show it (settings.ini extras_bestiary): everything, or the
// player's progress (bestiary_record.h): an enemy met shows its name, picture
// and places, one defeated its numbers, weaknesses, drops and attacks; the
// others are "???" rows. Progress is recorded either way. "Catch up from a
// save" marks as met every enemy whose estimated first appearance is no
// later than a save's (or the paused game's) story progress.
//
// Script commands (XG_EXTRAS_SCRIPT "do ..."):
//   wait                   finish reading the fields and the world map now
//   tab <story|az|bosses>  the list   page <-1|1> the previous or next tab (L1/R1)
//   mode <all|progress>    what is shown
//   find <text>            the list's filter ("" none)
//   select <name>          the first entry of that name (or #n: the n-th enemy row)
//   attack <n>             open attack n of the selection in the Battle scene
//   catchup <progress>     catch up to that story progress
//   info                   counts, the selection, on stderr
//   dump <file>            every entry, its records, places and attacks (validation)

#include <SDL3/SDL.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <algorithm>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "imgui.h"
#include "imgui_internal.h"
#include "ui/l10n.h"
#include "ui/skin.h"
#include "library.h"
#include "view.h"
#include "viewer.h"
#include "replace.h"

extern "C" {
#include "battle_moves.h"
#include "battle_preview.h"
#include "bestiary_data.h"
#include "bestiary_record.h"
#include "cd/cd.h"
#include "disc.h"
#include "system/settings.h"
#include "tables.h"
#include "tables_int.h"
}

namespace {
using namespace extras;

// The packs a battle picture can show (the Enhanced look): the texture pack and the
// sprite library's cels, by content (none ships for battle yet: the note says so).
const unsigned battleLibraries = XG_REPLACE_PACK | XG_REPLACE_CELS;

enum Tab { TAB_STORY, TAB_AZ, TAB_BOSSES, TAB_COUNT };
const char* const tabIds[TAB_COUNT] = {"story", "az", "bosses"};
const char* const tabLabels[TAB_COUNT] = {N_("Story"), N_("A-Z"), N_("Bosses")}; /* tr() where shown */
constexpr int FIELDS = 730, WORLDS = 17;

struct Place {
    XgBeastPlace p;
    int story = -1; // estimated story progress when it happens
};

struct Entry {
    std::vector<int> records; // State::beasts; the first one is shown
    std::string name;
    std::vector<int> places;  // State::places
    int story = -1;           // estimated first appearance
    bool boss = false;        // only in story battles
};

struct Row {
    std::string heading; // a heading row when entry < 0
    int entry = -1;
};

struct State {
    XgDisc* discs[2] = {nullptr, nullptr};
    std::vector<XgBeast> beasts;
    std::vector<Place> places;
    std::vector<Entry> entries;
    int fieldStory[2][FIELDS];
    // the scan of fields and world maps (a few per frame)
    int scanDisc = 0, scanField = 0, scanWorld = 0;
    bool scanned = false;
    // the list
    int tab = TAB_STORY;
    bool progress = false;
    char find[64] = "";
    std::vector<Row> rows;
    bool rowsDirty = true;
    int sel = -1; // entry
    bool focusList = true;
    // The focused view (viewer.h): the enemy chosen across the area, its
    // details beside the picture (or the picture alone).
    bool focused = false, pictureOnly = false;
    int focusEntry = -1; // the enemy it opened on (Back: unchanged, the list's scroll stays)
    ImGuiID highlight = 0;
    // the selection's details
    int detailFor = -1;
    int infoTop = -1;  // the entry whose details pane was last scrolled to the top
    std::vector<XgBeastAttack> attacks;
    XgBeast withOptions = {};
    bool optionsDiffer = false;
    // the picture
    std::unique_ptr<View> view{new View};
    XgBattlePreview* preview = nullptr;
    int previewFor = -1, settle = 0;
    double clock = 0;
    // the record shown: this game (-1), the shared record of games without an id (-2), or a save
    std::vector<XgBestiarySave> saves;
    int viewSave = -1;
    bool resetAsk = false;
};
State* S;

// ---- names -------------------------------------------------------------------------------------

// Bits of the weakness byte (m38) and of an attack's element byte (m22).
const char* const elementNames[6] = {N_("Water"), N_("Fire"), N_("Earth"), N_("Wind"), N_("Dark*"), N_("Light*")};

std::string elementList(int bits) {
    std::string out;
    for (int i = 0; i < 6; i++)
        if (bits & (1 << i)) out += (out.empty() ? "" : ", ") + std::string(tr(elementNames[i]));
    return out;
}

int regionOf(int disc) {
    XgDisc* d = disc >= 1 && disc <= 2 ? S->discs[disc - 1] : nullptr;
    return d ? tablesRegionOf(d) : 0;
}

std::string fieldLabel(int field) {
    const char* n = xgFieldName(field);
    char buf[160];
    if (n && *n) snprintf(buf, sizeof(buf), tr("%s (field %d)"), n, field);
    else snprintf(buf, sizeof(buf), tr("Field %d"), field);
    return buf;
}

std::string chapter(int story) {
    const char* c = story >= 0 ? xgStoryName(story) : nullptr;
    return c && *c ? c : "";
}

const XgBeast& shown(const Entry& e) { return S->beasts[e.records.front()]; }

std::string entryName(const Entry& e) {
    if (!e.name.empty()) return e.name;
    const XgBeast& b = shown(e);
    char buf[64];
    snprintf(buf, sizeof(buf), tr("Set %d, type %d"), b.set, b.type);
    return buf;
}

// XG_BESTIARY_* of an entry: any of its records.
int stateOf(const Entry& e) {
    int flags = 0;
    for (int r : e.records) {
        const XgBeast& b = S->beasts[r];
        flags |= xgBestiaryState(regionOf(b.disc), b.disc, b.set, b.type);
    }
    return flags;
}
bool met(const Entry& e) { return !S->progress || (stateOf(e) & (XG_BESTIARY_SEEN | XG_BESTIARY_CAUGHT_UP)); }
bool defeated(const Entry& e) { return !S->progress || (stateOf(e) & XG_BESTIARY_DEFEATED); }

// ---- reading -----------------------------------------------------------------------------------

void readBeasts() {
    XgBeast* all = nullptr;
    int count = 0;
    for (XgDisc* d : S->discs) {
        int n = xgBeastsRead(d, &all, count);
        if (n >= 0) count = n;
    }
    S->beasts.assign(all, all + count);
    free(all);
}

// Fields 0 (the developers' room) and 720-729 (the planners' maps) start any
// battle for testing: not where the game puts its enemies.
bool debugField(int field) { return field == 0 || field >= 720; }

bool stepScan(int budget) {
    XgBeastPlace buf[64];
    while (!S->scanned && budget-- > 0) {
        XgDisc* d = S->scanDisc < 2 ? S->discs[S->scanDisc] : nullptr;
        if (S->scanDisc >= 2) {
            S->scanned = true;
            break;
        }
        if (d == nullptr) {
            S->scanDisc++;
            continue;
        }
        if (S->scanField < FIELDS) {
            int story = -1, field = S->scanField++;
            int n = debugField(field) ? 0 : xgBeastFieldPlaces(d, field, buf, 64, &story);
            S->fieldStory[S->scanDisc][field] = story;
            for (int i = 0; i < n; i++) S->places.push_back({buf[i], -1});
        } else if (S->scanWorld < WORLDS) {
            int world = S->scanWorld++;
            int n = xgBeastWorldPlaces(d, world, buf, 64);
            for (int i = 0; i < n; i++) {
                int a = xgBeastWorldStory(world), b = xgBeastRangeStory(buf[i].range);
                S->places.push_back({buf[i], a < 0 ? -1 : std::max(a, b)});
            }
        } else {
            S->scanDisc++;
            S->scanField = S->scanWorld = 0;
        }
    }
    return S->scanned;
}

double scanProgress() {
    double per = FIELDS + WORLDS;
    return std::min(1.0, (S->scanDisc * per + S->scanField + S->scanWorld) / (2 * per));
}

void buildEntries() {
    // A field's story: the lowest progress its scripts set, else that of the
    // world map place it is named after (tables.c xgFieldPlace), else the
    // lowest of the fields up to three exits away (its scripts' change-field
    // ops, tables.c xgTablesFieldExits, both ways): a lower bound, never later.
    for (int disc = 0; disc < 2; disc++) {
        std::vector<std::vector<int>> links(FIELDS);
        for (int f = 0; f < FIELDS; f++)
            for (const int* x = xgTablesFieldExits(f); *x >= 0; x++)
                if (*x < FIELDS && !debugField(*x) && !debugField(f)) {
                    links[f].push_back(*x);
                    links[*x].push_back(f);
                }
        for (int round = 0; round < 3; round++) {
            int next[FIELDS];
            for (int f = 0; f < FIELDS; f++) {
                next[f] = S->fieldStory[disc][f];
                if (next[f] >= 0) continue;
                for (int x : links[f]) {
                    int s = S->fieldStory[disc][x];
                    if (s >= 0 && (next[f] < 0 || s < next[f])) next[f] = s;
                }
            }
            memcpy(S->fieldStory[disc], next, sizeof(next));
        }
        std::map<int, int> placeMin;
        for (int f = 0; f < FIELDS; f++) {
            int p = xgFieldPlace(f), s = S->fieldStory[disc][f];
            if (p >= 0 && s >= 0 && (!placeMin.count(p) || s < placeMin[p])) placeMin[p] = s;
        }
        for (Place& pl : S->places) {
            if (pl.p.disc != disc + 1 || pl.p.kind == XG_PLACE_WORLD) continue;
            int s = S->fieldStory[disc][pl.p.field];
            if (s < 0 && xgFieldPlace(pl.p.field) >= 0 && placeMin.count(xgFieldPlace(pl.p.field)))
                s = placeMin[xgFieldPlace(pl.p.field)];
            pl.story = s;
        }
    }
    std::map<long long, int> recordOf; // (disc, set, type) -> beast
    for (int i = 0; i < (int)S->beasts.size(); i++) {
        const XgBeast& b = S->beasts[i];
        recordOf[(long long)b.disc << 16 | b.set << 8 | b.type] = i;
    }
    std::map<uint64_t, int> byIdentity;
    std::vector<int> entryOfBeast(S->beasts.size(), -1);
    for (int pi = 0; pi < (int)S->places.size(); pi++) {
        const XgBeastPlace& p = S->places[pi].p;
        if (p.set == 0) continue; // the debug set (its names are "?????")
        for (int t : p.types) {
            if ((t & 0x7F) == 0x7F) continue;
            auto found = recordOf.find((long long)p.disc << 16 | p.set << 8 | (t & 0x7F));
            if (found == recordOf.end()) continue;
            int bi = found->second;
            const XgBeast& rec = S->beasts[bi];
            if (rec.hp == 0)
                continue; // a stand-in of a story scene (no HP, the default numbers: Alpha Weltall's prologue, 12 nameless ones)
            if (entryOfBeast[bi] < 0) {
                const XgBeast& b = S->beasts[bi];
                auto id = byIdentity.find(b.identity);
                if (id == byIdentity.end()) {
                    id = byIdentity.emplace(b.identity, (int)S->entries.size()).first;
                    S->entries.push_back({});
                    S->entries.back().name = b.name;
                }
                entryOfBeast[bi] = id->second;
                S->entries[id->second].records.push_back(bi);
            }
            Entry& e = S->entries[entryOfBeast[bi]];
            if (std::find(e.places.begin(), e.places.end(), pi) == e.places.end()) e.places.push_back(pi);
        }
    }
    // First met: the fields' estimates first. A world map table lists a
    // whole continent's areas from its first configuration on, before the
    // player can reach them: only when no field has it, and never the first
    // configuration (story below 0x18, the Lahan days).
    for (Entry& e : S->entries) {
        bool random = false;
        int world = -1;
        for (int pi : e.places) {
            const Place& pl = S->places[pi];
            random |= pl.p.kind != XG_PLACE_FIELD_SCRIPTED;
            if (pl.story < 0) continue;
            if (pl.p.kind == XG_PLACE_WORLD) {
                if (pl.p.world >= 1 && (world < 0 || pl.story < world)) world = pl.story;
            } else if (e.story < 0 || pl.story < e.story) {
                e.story = pl.story;
            }
        }
        if (e.story < 0) e.story = world;
        e.boss = !random;
    }
    S->rowsDirty = true;
    logEvent("bestiary: %zu entries from %zu records, %zu battle places", S->entries.size(), S->beasts.size(), S->places.size());
}

// ---- the list ----------------------------------------------------------------------------------

std::string lower(std::string s) {
    for (char& c : s) c = (char)SDL_tolower(c);
    return s;
}

void buildRows() {
    S->rows.clear();
    S->rowsDirty = false;
    std::vector<int> order;
    std::string find = lower(S->find);
    for (int i = 0; i < (int)S->entries.size(); i++) {
        const Entry& e = S->entries[i];
        if (S->tab == TAB_BOSSES && !e.boss) continue;
        if (!find.empty() && (!met(e) || lower(entryName(e)).find(find) == std::string::npos)) continue;
        order.push_back(i);
    }
    auto byStory = [](int a, int b) {
        const Entry &x = S->entries[a], &y = S->entries[b];
        int sx = x.story < 0 ? 1 << 30 : x.story, sy = y.story < 0 ? 1 << 30 : y.story;
        return sx != sy ? sx < sy : a < b;
    };
    if (S->tab == TAB_AZ) {
        std::stable_sort(order.begin(), order.end(), [](int a, int b) {
            bool ma = met(S->entries[a]), mb = met(S->entries[b]);
            if (ma != mb) return ma; // "???" at the end
            return lower(entryName(S->entries[a])) < lower(entryName(S->entries[b]));
        });
    } else {
        std::stable_sort(order.begin(), order.end(), byStory);
    }
    S->rows.push_back({"", -2}); // the overview
    std::string last = "\x01";
    std::map<std::string, bool> chapterMet; // progress: no chapter name before one of its enemies is met
    for (int i : order) {
        const Entry& e = S->entries[i];
        chapterMet[e.story < 0 ? "" : chapter(e.story)] |= met(e);
    }
    for (int i : order) {
        const Entry& e = S->entries[i];
        std::string head = S->tab == TAB_AZ ? "" : e.story < 0 ? tr("Not placed in the story") : chapter(e.story);
        if (S->tab != TAB_AZ && head != last) {
            bool open = !S->progress || chapterMet[e.story < 0 ? "" : chapter(e.story)];
            S->rows.push_back({!open ? "???" : head.empty() ? tr("(an unnamed chapter)") : head, -1});
            last = head;
        }
        S->rows.push_back({"", i});
    }
}

void showTab(int t) {
    if (S->tab != t) sound(1);
    S->tab = t;
    S->rowsDirty = true;
    S->focusList = true;
}

// ---- the picture -------------------------------------------------------------------------------

void dropPreview() {
    xgBattlePreviewAudio(S->preview, 0);
    xgBattlePreviewDestroy(S->preview);
    S->preview = nullptr;
    S->previewFor = -1;
}

int arenaOf(const Entry& e) {
    for (int pi : e.places) return S->places[pi].p.arena;
    return 0;
}

void makePreview(int entry) {
    dropPreview();
    S->previewFor = entry;
    const Entry& e = S->entries[entry];
    const XgBeast& b = shown(e);
    XgDisc* d = S->discs[b.disc - 1];
    if (d == nullptr || !met(e)) return;
    // The battle's own loader in a world of its own: the enemy alone in slot
    // 3 of an arena it is fought in (port/src/game/battle/preview.c).
    S->preview = xgBattlePreviewCreate(d, 0, arenaOf(e), b.set, 0, b.type);
    if (S->preview == nullptr) return;
    xgBattlePreviewPresentation(S->preview, 3, 0);
    xgBattlePreviewStep(S->preview, 60); /* past the battle's fade from black */
    S->view->show(xgBattlePreviewGpu(S->preview));
    int y = xgBattlePreviewScreenY(S->preview);
    S->view->setScene(0, y, 320, y + 224);
    S->clock = 0;
}

void stepPreview() {
    if (!S->preview) return;
    S->clock += std::min(.1f, ImGui::GetIO().DeltaTime) * xgSpriteTickRate(2);
    int ticks = (int)S->clock;
    S->clock -= ticks;
    if (ticks > 0) {
        xgBattlePreviewStep(S->preview, ticks);
        if (xgBattlePreviewFailed(S->preview)) {
            dropPreview();
            S->previewFor = S->sel; // not again: "no picture"
            return;
        }
        // The battle draws its frames into either half of VRAM (battle.cpp refresh).
        S->view->show(xgBattlePreviewGpu(S->preview));
        int y = xgBattlePreviewScreenY(S->preview);
        S->view->setScene(0, y, 320, y + 224);
    }
}

// ---- the selection -----------------------------------------------------------------------------

void select(int entry) {
    if (entry == S->sel) return;
    S->sel = entry;
    S->settle = 0;
    S->detailFor = -1;
}

void details() {
    if (S->detailFor == S->sel || S->sel < 0) return;
    S->detailFor = S->sel;
    const Entry& e = S->entries[S->sel];
    const XgBeast& b = shown(e);
    XgBeastAttack buf[48];
    int n = xgBeastAttacks(S->discs[b.disc - 1], b.set, b.type, buf, 48);
    S->attacks.assign(buf, buf + n);
    xgBeastWithOptions(&b, &S->withOptions);
    const XgBeast& w = S->withOptions;
    S->optionsDiffer = w.hp != b.hp || w.attack != b.attack || w.defence != b.defence || w.ether != b.ether ||
                       w.etherDefence != b.etherDefence || w.agility != b.agility || w.exp != b.exp || w.gold != b.gold;
}

// The first of the type's turns (battle_moves.h; the Battle scene lists
// the same) that plays this attack or ability. -1: none does.
int turnOf(const XgBeast& b, const XgBeastAttack& a) {
    XgDisc* d = S->discs[b.disc - 1];
    int size = 0, entry = d ? xgDiscFileEntry(d, 0x0D, 2 + 2 * b.set) : -1;
    uint8_t* set = entry >= 0 ? xgDiscReadEntry(d, entry, &size) : nullptr;
    std::vector<XgBattleTurn> turns(256);
    int n = xgBattleTurns(set, size, b.type, turns.data(), int(turns.size()), nullptr);
    free(set);
    for (int i = 0; i < n; i++)
        for (int k = 0; k < turns[i].count; k++) {
            const uint8_t* x = turns[i].actions[k];
            if (a.kind == 1 && x[0] == XG_ACT_ATTACK && x[2] == a.animation && (a.attack < 0 || x[1] == a.attack))
                return i;
            if (a.kind == 13 && x[0] == XG_ACT_ABILITY && (x[4] | x[5] << 8) == a.value) return i;
        }
    return -1;
}

void followBattle(const Entry& e, const XgBeastAttack* a) {
    const XgBeast& b = shown(e);
    char setup[96], move[96];
    // a Gear fights the party's Gears: Fei in his (form 1)
    snprintf(setup, sizeof(setup), "0 %d %d %d %d", arenaOf(e), b.set, b.visual < 0 ? 1 : 0, b.type);
    ViewerTarget t{"battle", {{"setup", setup}}};
    if (a != nullptr) {
        // The enemy's turn that uses it (kind 6), at the party's first
        // member (slot 0); else an attack's animation alone: a sprite
        // enemy's pose (kind 4), a model's program (kind 3).
        int turn = turnOf(b, *a);
        if (turn >= 0)
            snprintf(move, sizeof(move), "3 0 6 %d 0 1", turn);
        else if (a->kind == 1)
            snprintf(move, sizeof(move), "3 0 %d %d 0 1", b.visual < 0 ? 3 : 4, a->animation);
        else {
            error("No turn of its scripts plays this ability with fixed values");
            return;
        }
        t.commands.push_back({"move", move});
        t.commands.push_back({"toggle", ""}); // playing
    }
    if (!follow(t)) error("The Battle scene could not be opened");
}

bool playable(const XgBeastAttack& a) { return (a.kind == 1 && a.animation >= 0) || (a.kind == 13 && a.value >= 0); }

// ---- catch up ------------------------------------------------------------------------------------

int catchUp(int region, int progress) {
    int marked = 0;
    for (const Entry& e : S->entries) {
        if (e.story < 0 || e.story > progress) continue;
        for (int r : e.records) {
            const XgBeast& b = S->beasts[r];
            if (regionOf(b.disc) == region) marked += xgBestiaryCatchUp(region, b.disc, b.set, b.type);
        }
    }
    xgBestiarySave();
    S->rowsDirty = true;
    return marked;
}

// ---- drawing -------------------------------------------------------------------------------------

void hint(const char* fmt, ...) IM_FMTARGS(1);
void hint(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    ImGui::PushStyleColor(ImGuiCol_Text, skin::textHint);
    ImGui::TextWrappedV(tr(fmt), ap); // in the menu's language (ui/l10n.h)
    ImGui::PopStyleColor();
    va_end(ap);
}

// ---- the focused view: the enemy across the area, its details beside it -----------------------------

void setFocused(bool on) {
    if (on == S->focused || (on && S->sel < 0)) return;
    S->focused = on;
    if (S->view) S->view->fit();
    if (on) {
        S->focusEntry = S->sel;
        S->infoTop = -1; // its details from the top
        focusEnter();
        return;
    }
    // Back on the list: the row it opened on keeps its place; a row changed
    // meanwhile is scrolled to.
    S->focusList = S->sel != S->focusEntry;
    focusLeave();
}

// The list's previous or next enemy (headings and the overview skipped).
void stepEnemy(int delta) {
    if (S->rowsDirty) buildRows();
    std::vector<int> list;
    int at = -1;
    for (const Row& r : S->rows)
        if (r.entry >= 0) {
            if (r.entry == S->sel) at = (int)list.size();
            list.push_back(r.entry);
        }
    if (list.empty()) return;
    int n = (int)list.size();
    at = at < 0 ? (delta > 0 ? 0 : n - 1) : (at + delta + n) % n;
    select(list[(size_t)at]);
    S->infoTop = -1;
    sound(1);
}

void picturePane(ImVec2 min, ImVec2 max, bool focused);
void infoPane(ImVec2 min, ImVec2 max);

void focusedView(const Area& a) {
    if (S->sel < 0) { setFocused(false); return; }
    float u = a.unit;
    FocusLayout f = focusLayout(a, 1);
    if (S->pictureOnly) picturePane(f.min, f.max, true);
    else {
        float split = floorf(fmaxf(f.min.x + 160 * u, f.min.x + (f.max.x - f.min.x) * .58f) / u) * u;
        picturePane(f.min, ImVec2(split, f.max.y), true);
        infoPane(ImVec2(split + 12 * u, f.min.y), f.max);
    }
    if (beginFocusBar(f)) setFocused(false);
    if (focusButton("Previous")) stepEnemy(-1);
    if (focusButton("Next")) stepEnemy(1);
    if (focusButton(S->pictureOnly ? "Details" : "Picture only")) {
        S->pictureOnly = !S->pictureOnly;
        if (S->view) S->view->fit();
    }
    focusZoom(S->view.get(), 1); // 1:1: one battle-screen pixel per window pixel
    focusLook(battleLibraries, lookCovered(S->view.get(), battleLibraries));
    endFocusBar();
}

void listPane(ImVec2 min, ImVec2 max) {
    float u = unit();
    ImGui::SetCursorScreenPos(ImVec2(min.x + 4 * u, min.y));
    ImGui::PushItemFlag(ImGuiItemFlags_NoNavDefaultFocus, true);
    for (int t = 0; t < TAB_COUNT; t++) {
        if (t) ImGui::SameLine(0, 14 * u);
        ImGui::PushID(t);
        if (skin::selectable(tr(tabLabels[t]), S->tab == t, 0, ImVec2(ImGui::CalcTextSize(tr(tabLabels[t])).x + 4 * u, 0)))
            showTab(t);
        ImGui::PopID();
    }
    ImGui::PopItemFlag();
    min.y += ImGui::GetTextLineHeightWithSpacing() + 12 * u;
    if (beginPane("##bestlist", min, max, nullptr, true)) {
        ImGui::SetNextItemWidth(-1);
        ImGui::PushItemFlag(ImGuiItemFlags_NoNavDefaultFocus, true);
        if (ImGui::InputTextWithHint("##find", tr("Find (keyboard)"), S->find, sizeof(S->find))) S->rowsDirty = true;
        ImGui::PopItemFlag();
        if (S->rowsDirty) buildRows();
        int target = -1;
        for (int i = 0; i < (int)S->rows.size(); i++)
            if (S->rows[i].entry == (S->sel < 0 ? -2 : S->sel)) target = i;
        if (target < 0) target = 0;
        // Up and Down inside the details stay there (the panes' items share one navigation space: the
        // list's rows beside them would otherwise take the cursor and change the enemy).
        ImGuiContext& g = *ImGui::GetCurrentContext();
        bool fromInfo = g.NavMoveScoringItems && (g.NavMoveDir == ImGuiDir_Up || g.NavMoveDir == ImGuiDir_Down) &&
                        g.NavWindow != nullptr && strstr(g.NavWindow->Name, "##bestinfo") != nullptr;
        ImGui::PushItemFlag(ImGuiItemFlags_NoNav, fromInfo);
        for (int i = 0; i < (int)S->rows.size(); i++) {
            const Row& r = S->rows[i];
            if (r.entry == -1) {
                ImGui::SeparatorText(r.heading.c_str());
                continue;
            }
            ImGui::PushID(i);
            bool known = r.entry >= 0 && met(S->entries[r.entry]);
            std::string label = r.entry == -2 ? tr("Overview") : known ? entryName(S->entries[r.entry]) : "???";
            if (r.entry >= 0 && S->progress && known && !defeated(S->entries[r.entry])) label += tr("  (met)");
            bool chosen = listItem(label.c_str(), r.entry == (S->sel < 0 ? -2 : S->sel), r.entry >= 0 && !known);
            if (libraryHighlight(chosen, S->highlight)) select(r.entry == -2 ? -1 : r.entry);
            if (chosen && r.entry >= 0) setFocused(true);
            else if (chosen) sound(2);
            if (S->focusList && i == target) {
                ImGui::SetKeyboardFocusHere(-1);
                ImGui::SetScrollHereY(.5f);
                S->focusList = false;
            }
            ImGui::PopID();
        }
        ImGui::PopItemFlag();
        if (S->entries.empty()) hint("No enemies were found on these discs.");
    }
    endPane();
}

void numbersTable(const XgBeast& b) {
    const XgBeast& w = S->withOptions;
    struct Row { const char* name; int disc, opt; };
    Row rows[] = {{N_("HP"), b.hp, w.hp},           {N_("Attack"), b.attack, w.attack},      {N_("Defence"), b.defence, w.defence},
                  {N_("Ether"), b.ether, w.ether},  {N_("Ether defence"), b.etherDefence, w.etherDefence},
                  {N_("Agility"), b.agility, w.agility}, {N_("Hit %"), b.hit, w.hit},          {N_("Evade %"), b.evade, w.evade}};
    int columns = S->optionsDiffer ? 3 : 2;
    if (ImGui::BeginTable("##numbers", columns, ImGuiTableFlags_SizingFixedFit)) {
        ImGui::TableSetupColumn("");
        ImGui::TableSetupColumn(tr("Disc"));
        if (columns == 3) ImGui::TableSetupColumn(tr("Your options"));
        ImGui::TableHeadersRow();
        for (const Row& r : rows) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(tr(r.name));
            ImGui::TableNextColumn();
            ImGui::Text("%d", r.disc);
            if (columns == 3) {
                ImGui::TableNextColumn();
                if (r.opt != r.disc) ImGui::TextColored(skin::textWarning, "%d", r.opt);
                else ImGui::Text("%d", r.opt);
            }
        }
        ImGui::EndTable();
    }
    if (b.gear) hint("A Gear: HP, attack and defences are its Gear numbers.");
    if (!S->optionsDiffer) hint("Your gameplay options leave enemies as the disc has them.");
}

void weaknesses(const XgBeast& b) {
    std::string weak = elementList(b.weakness & 0x3F);
    ImGui::TextWrapped(tr("Weak to: %s%s"), weak.empty() ? tr("nothing") : weak.c_str(),
                       !weak.empty() && (b.weakness & 0x40) ? " (x1.8)" : !weak.empty() ? " (x1.5)" : "");
    int res = (b.resist[0] | b.resist[1]) >> 8 & 0xF, own = (b.resist[0] | b.resist[1]) >> 12 & 0xF;
    std::string blocks = elementList(res), elem = elementList(own);
    int low = (b.resist[0] | b.resist[1]) & 0xE;
    ImGui::TextWrapped(tr("Resists: %s%s"), blocks.empty() ? tr("nothing") : blocks.c_str(),
                       blocks.empty() ? "" : (low & 8) ? tr(" (absorbs)") : (low & 4) ? tr(" (strongly)") : "");
    if (!elem.empty()) ImGui::TextWrapped(tr("Its own element: %s"), elem.c_str());
    if (b.monsterFlags2 & 0x8000) ImGui::TextWrapped(tr("Damage heals it and healing hurts it."));
    else if (b.monsterFlags2 & 0x4000) ImGui::TextWrapped(tr("Ether attacks heal it."));
    else if (b.monsterFlags2 & 0x2000) ImGui::TextWrapped(tr("Non-elemental Ether attacks heal it."));
    // Status immunities: m7C[1] guards m7C[0]'s, m7C[3] m7C[2]'s (b_800941A4.c).
    const uint8_t* r = b.raw;
    int g1 = r[0x7E] | r[0x7F] << 8, g3 = r[0x82] | r[0x83] << 8;
    struct { int word, bit; const char* name; } st[] = {{1, 0x2000, N_("Stop")},  {1, 0x1000, N_("Slow")},      {1, 0x800, N_("Poison")},
                                                       {1, 0x400, N_("Accuracy down")}, {1, 0x200, N_("Attack down")},
                                                       {3, 0x2000, N_("Confusion")}, {3, 0x1000, N_("Sleep")}, {3, 0x800, N_("Forget")},
                                                       {3, 0x400, N_("Ether down")}};
    std::string imm;
    for (auto& s : st)
        if ((s.word == 1 ? g1 : g3) & s.bit) imm += (imm.empty() ? "" : ", ") + std::string(tr(s.name));
    if (b.gear) imm = tr("every status (a Gear)");
    if (!imm.empty()) ImGui::TextWrapped(tr("Immune to: %s"), imm.c_str());
    if (((b.weakness | own | res) & 0x30) || ((b.weakness & 0x30))) hint("* Light and Dark are named only by Billy's ammunition.");
}

void rewards(const XgBeast& b) {
    const XgBeast& w = S->withOptions;
    if (w.exp != b.exp || w.gold != b.gold)
        ImGui::TextWrapped(tr("EXP %d, gold %d (your options: EXP %d, gold %d)"), b.exp, b.gold, w.exp, w.gold);
    else
        ImGui::TextWrapped(tr("EXP %d, gold %d"), b.exp, b.gold);
    // rollItemDrops (src/battle_result/br_801DE5C4.c): the first at its
    // chance, else the second at its own; a thief's accessory makes the first
    // certain.
    const XgBeastDrop& d1 = b.drops[0];
    const XgBeastDrop& d2 = b.drops[1];
    auto name = [](const XgBeastDrop& d) { return d.name[0] ? d.name : tr("an item the font cannot spell"); };
    if (d1.chance > 0) {
        if (d1.chance >= 100) ImGui::TextWrapped(tr("Drops %s (always)"), name(d1));
        else ImGui::TextWrapped(tr("Drops %s (%d%%)"), name(d1), d1.chance);
    }
    if (d2.chance > 0 && d1.chance < 100) {
        if (d1.chance > 0) ImGui::TextWrapped(tr("otherwise %s (%d%%)"), name(d2), std::min(d2.chance, 100));
        else ImGui::TextWrapped(tr("Drops %s (%d%%)"), name(d2), std::min(d2.chance, 100));
    }
    if (d1.chance <= 0 && d2.chance <= 0) ImGui::TextWrapped(tr("Drops nothing."));
    else if (d1.chance > 0 && d1.chance < 100) hint("A thief's accessory in the party makes the first drop certain.");
    hint("Its own script can change these during a battle.");
}

void attacksList(const Entry& e) {
    float u = unit();
    bool lines = false;
    for (int i = 0; i < (int)S->attacks.size(); i++) {
        const XgBeastAttack& a = S->attacks[i];
        if (a.kind == 12) {
            lines = true;
            continue;
        }
        std::string label = a.name[0] ? a.name : a.kind == 13 ? tr("Ability effect") : tr("Attack");
        std::string extra;
        if (a.kind == 1) {
            std::string el = elementList(a.elements & 0x3F);
            extra = (a.flags & 0x100) ? tr("Ether") : tr("physical");
            if (a.power) extra += tr(", power ") + std::to_string(a.power);
            if (!el.empty()) extra += ", " + el;
        } else if (a.kind == 13 && a.value >= 0) {
            extra = tr("effect ") + std::to_string(a.value & 0x1FF);
        }
        if (a.script == 2) extra += tr(", when hit");
        if (a.script == 3) extra += tr(", after the party's turn");
        ImGui::PushID(i);
        std::string row = label + "  (" + extra + ")";
        if (playable(a)) {
            if (listItem(row.c_str(), false)) {
                sound(2);
                followBattle(e, &a);
            }
        } else {
            listItem(row.c_str(), false, true);
        }
        ImGui::PopID();
    }
    if (S->attacks.empty()) hint("Its scripts use no fixed attack (they choose from variables, or it does not act).");
    else hint("Choose an attack or ability to play the enemy's turn that uses it in the Battle scene.");
    ImGui::Dummy(ImVec2(0, 2 * u));
    ImGui::SeparatorText(tr("In the game's words"));
    hint("The game has no description of its enemies.");
    if (lines) {
        ImGui::TextUnformatted(tr("Lines its own battle scripts show:"));
        for (const XgBeastAttack& a : S->attacks)
            if (a.kind == 12) ImGui::TextWrapped("%s", a.name);
    }
}

// Returns whether the cursor is on its last row (the pane then scrolls to its end: the text under
// that row has no item of its own to scroll to).
bool placesList(const Entry& e) {
    // One row per field (its random table's configs with this enemy add up) or world map area; the
    // same numbers on both discs make one row.
    struct Row { int pi, weight; bool random, scripted, event, maybe; };
    struct Group { Row disc[2]; bool on[2]; };
    std::vector<Group> groups;
    std::map<std::string, size_t> groupOf;
    for (int pi : e.places) {
        const XgBeastPlace& p = S->places[pi].p;
        char key[64];
        if (p.kind == XG_PLACE_WORLD)
            snprintf(key, sizeof(key), "w %d %d %d", p.world, p.zone, p.range);
        else
            snprintf(key, sizeof(key), "f %d", p.field);
        auto found = groupOf.find(key);
        if (found == groupOf.end()) {
            found = groupOf.emplace(key, groups.size()).first;
            groups.push_back({});
        }
        Group& gr = groups[found->second];
        int d = p.disc == 2;
        Row& r = gr.disc[d];
        if (!gr.on[d]) r = {pi, 0, false, false, false, false};
        gr.on[d] = true;
        if (p.kind == XG_PLACE_FIELD_SCRIPTED) {
            r.scripted = true;
            r.event |= (p.flags & 0x20) != 0;
        } else {
            r.random = true;
            r.weight += p.weight;
            r.maybe |= p.randomMaybe != 0;
        }
    }
    std::vector<Row> rows;
    for (const Group& gr : groups) {
        const Row &a = gr.disc[0], &b = gr.disc[1];
        bool same = gr.on[0] && gr.on[1] && a.weight == b.weight && a.random == b.random && a.scripted == b.scripted &&
                    a.event == b.event && a.maybe == b.maybe && S->places[a.pi].p.total == S->places[b.pi].p.total;
        if (gr.on[0]) rows.push_back(a);
        if (gr.on[1] && !same) rows.push_back(b);
    }
    int lastItem = -1;
    for (int i = 0; i < (int)rows.size(); i++)
        if (S->places[rows[i].pi].p.kind != XG_PLACE_WORLD) lastItem = i;
    bool lastFocused = false;
    for (const Row& r : rows) {
        const XgBeastPlace& p = S->places[r.pi].p;
        const char* disc2 = S->discs[1] && p.disc == 2 ? tr(", disc 2") : "";
        ImGui::PushID(r.pi);
        if (p.kind == XG_PLACE_WORLD) {
            ImGui::TextWrapped(tr("World map %d, area %d"), p.world, p.zone);
            ImGui::Indent();
            hint("Random battles from story %d on: %d in %d%s", xgBeastRangeStory(p.range), r.weight, p.total, disc2);
            ImGui::Unindent();
        } else {
            if (listItem(fieldLabel(p.field).c_str(), false)) {
                sound(2);
                char field[16];
                snprintf(field, sizeof(field), "%d", p.field);
                if (!follow({"maps", {{"field", field}}})) error("The Map viewer could not be opened");
            }
            lastFocused = ImGui::IsItemFocused() && &r == &rows[lastItem];
            std::string what;
            char buf[128];
            if (r.random) {
                snprintf(buf, sizeof(buf), tr("random battles: %d in %d%s"), r.weight, p.total,
                         r.maybe ? tr(", when its scripts enable them") : "");
                what += buf;
            }
            if (r.scripted) {
                if (!what.empty()) what += "; ";
                what += r.event ? tr("a story battle (with its battle event)") : tr("a story battle");
            }
            ImGui::Indent();
            hint("%s%s", what.c_str(), disc2);
            ImGui::Unindent();
        }
        ImGui::PopID();
    }
    if (e.story >= 0) hint("First met around \"%s\" (story %d), estimated from the story values the scripts of its places set.",
                           chapter(e.story).c_str(), e.story);
    return lastFocused;
}

void overview(ImVec2 min, ImVec2 max) {
    if (beginPane("##bestover", min, max, "Bestiary", true)) {
        int metCount = 0, beaten = 0;
        for (const Entry& e : S->entries) {
            int f = stateOf(e);
            metCount += (f & (XG_BESTIARY_SEEN | XG_BESTIARY_CAUGHT_UP)) != 0;
            beaten += (f & XG_BESTIARY_DEFEATED) != 0;
        }
        ImGui::TextWrapped(tr("%zu enemies on these discs. You have met %d and defeated %d."), S->entries.size(), metCount, beaten);
        ImGui::Dummy(ImVec2(0, 4 * unit()));
        ImGui::SeparatorText(tr("Show"));
        bool all = !S->progress;
        if (ImGui::RadioButton(tr("Everything"), all) && S->progress) {
            S->progress = false;
            settingsSet("extras_bestiary", "all");
            S->rowsDirty = true;
            S->detailFor = S->previewFor = -1;
        }
        if (ImGui::RadioButton(tr("My progress"), !all) && !S->progress) {
            S->progress = true;
            settingsSet("extras_bestiary", "progress");
            S->rowsDirty = true;
            S->detailFor = S->previewFor = -1;
        }
        hint("My progress: an enemy you met shows its name, picture and places; one you defeated its numbers, weaknesses, "
             "drops and attacks. Battles are recorded in either mode.");
        ImGui::SeparatorText(tr("Whose progress"));
        hint("Each game keeps its own record, by the id its saves carry from their first save on. Games saved before "
             "ids existed share one record.");
        auto choose = [&](int which, unsigned id) {
            sound(2);
            if (which == -1) xgBestiaryViewGame();
            else xgBestiaryView(id);
            S->viewSave = which;
            S->rowsDirty = true;
            S->detailFor = S->previewFor = -1;
            S->resetAsk = false;
        };
        if (fromGame() && listItem(tr("This game"), S->viewSave == -1)) choose(-1, 0);
        if (listItem(tr("Games saved before ids (the shared record)"), S->viewSave == -2)) choose(-2, 0);
        if (ImGui::Button(tr("Read the memory cards"))) {
            XgBestiarySave buf[30];
            int n = xgBestiarySaves(buf, 30);
            S->saves.assign(buf, buf + n);
            if (S->viewSave >= n) S->viewSave = -2;
            if (n == 0) status("No Xenogears saves on the memory cards");
        }
        for (int i = 0; i < (int)S->saves.size(); i++) {
            const XgBestiarySave& sv = S->saves[i];
            char label[200];
            snprintf(label, sizeof(label), sv.id ? tr("Slot %d: %s (%s, story %d)") : tr("Slot %d: %s (%s, story %d) - shared record"),
                     sv.slot + 1, sv.title, chapter(sv.progress).c_str(), sv.progress);
            ImGui::PushID(i);
            if (listItem(label, S->viewSave == i)) choose(i, sv.id);
            ImGui::PopID();
        }
        ImGui::SeparatorText(tr("Catch up"));
        hint("Marks as met, in the record shown, every enemy whose estimated first appearance is no later than the story "
             "reached.");
        if (S->viewSave == -1 && fromGame() && ImGui::Button(tr("To the game in progress"))) {
            int progress = xgBestiaryGameProgress();
            int n = catchUp(regionOf(1) ? regionOf(1) : regionOf(2), progress);
            status("Caught up to story %d: %d enemies marked as met", progress, n);
        }
        if (S->viewSave >= 0 && S->viewSave < (int)S->saves.size() && ImGui::Button(tr("To this save's story"))) {
            const XgBestiarySave& sv = S->saves[S->viewSave];
            int n = catchUp(sv.region, sv.progress);
            status("Caught up to story %d: %d enemies marked as met", sv.progress, n);
        }
        ImGui::SeparatorText(tr("Carry into a new game"));
        hint("The next new game starts with a copy of the record shown (its own progress then goes on from there), "
             "once that game saves for the first time. Loading a save instead leaves the choice for a later new game.");
        unsigned pending = xgBestiaryCarryPending();
        bool freshGame = S->viewSave == -1 && xgBestiaryGameFresh() && xgBestiaryGameId() == 0;
        if (!freshGame && ImGui::Button(tr("Carry this record"))) {
            unsigned id = S->viewSave >= 0 && S->viewSave < (int)S->saves.size() ? S->saves[S->viewSave].id : S->viewSave == -1 ? xgBestiaryGameId() : 0;
            xgBestiaryCarry(id);
            status("The next new game starts with this bestiary");
        }
        if (pending != XG_BESTIARY_CARRY_NONE) {
            if (pending == 0) ImGui::TextWrapped(tr("Pending: the shared record carries into the next new game."));
            else ImGui::TextWrapped(tr("Pending: record %08x carries into the next new game."), pending);
            if (ImGui::Button(tr("Cancel the carry-over"))) {
                xgBestiaryCarry(XG_BESTIARY_CARRY_NONE);
                status("No record carries into the next new game");
            }
        }
        ImGui::SeparatorText(tr("Start over"));
        if (!S->resetAsk) {
            if (ImGui::Button(tr("Forget this record"))) S->resetAsk = true;
        } else {
            ImGui::TextColored(skin::textWarning, tr("Forget every enemy met and defeated in the record shown?"));
            if (ImGui::Button(tr("Forget"))) {
                xgBestiaryReset();
                S->resetAsk = false;
                S->rowsDirty = true;
                status("Bestiary progress forgotten");
            }
            ImGui::SameLine();
            if (ImGui::Button(tr("Keep"))) S->resetAsk = false;
        }
        hint("Records are kept beside settings.ini (bestiary-<id>.txt per game, bestiary.txt shared), never on a memory card.");
    }
    endPane();
}

void picturePane(ImVec2 min, ImVec2 max, bool focused);
void infoPane(ImVec2 min, ImVec2 max);

void detailPane(ImVec2 min, ImVec2 max) {
    float u = unit();
    if (S->sel < 0) {
        overview(min, max);
        return;
    }
    float pictureH = floorf((max.y - min.y) * .42f / u) * u;
    ImVec2 picMax(max.x, min.y + pictureH);
    picturePane(min, picMax, false);
    infoPane(ImVec2(min.x, picMax.y + 8 * u), max);
}

// The enemy's picture (its battle, set up by the game's code) in a pane.
void picturePane(ImVec2 min, ImVec2 max, bool focused) {
    const Entry& e = S->entries[S->sel];
    bool known = met(e);
    if (beginPane(focused ? "##bestfocus" : "##bestpic", min, max, nullptr, false)) {
        if (known && S->preview) {
            S->view->setReplacements(lookLibraries(battleLibraries));
            S->view->draw(ImGui::GetContentRegionAvail());
            if (focused) focusPicture(S->pictureOnly ? entryName(e).c_str() : nullptr);
        } else if (known) hint(S->previewFor == S->sel ? "No picture: its battle could not be set up here." : "");
        else hint("Not met yet.");
    }
    endPane();
}

// What the bestiary knows of the enemy, in a pane that scrolls.
void infoPane(ImVec2 min, ImVec2 max) {
    float u = unit();
    const Entry& e = S->entries[S->sel];
    bool known = met(e), beaten = defeated(e);
    const XgBeast& b = shown(e);
    char title[96];
    snprintf(title, sizeof(title), "%s", known ? entryName(e).c_str() : "???");
    if (S->infoTop != S->sel) { // a new enemy's details start at the top
        ImGui::SetNextWindowScroll(ImVec2(0, 0));
        S->infoTop = S->sel;
    }
    if (beginPane("##bestinfo", min, max, title, true)) {
        if (!known) {
            hint("You have not met this enemy yet.");
            endPane();
            return;
        }
        details();
        ImGui::TextWrapped("%s%s", b.visual < 0 ? tr("A 3D model") : tr("A sprite"), b.gear ? tr(", a Gear") : "");
        if (e.name.empty())
            hint(regionOf(b.disc) == 'J' ? "Its name is not a text of the JP font (a code that is no letter)."
                                         : "Its name has no spelling in the game font's narrow letters.");
        if (ImGui::Button(tr("Battle scene"))) followBattle(e, nullptr);
        if (b.visual > 0) {
            ImGui::SameLine();
            if (ImGui::Button(tr("Sprite viewer"))) {
                char src[48];
                snprintf(src, sizeof(src), "enemy %d %d", b.set, b.type);
                if (!follow({"sprites", {{"source", src}}})) error("The Sprite viewer could not be opened");
            }
        }
        if (!beaten) {
            ImGui::Dummy(ImVec2(0, 4 * u));
            hint("Defeat it to see its numbers, weaknesses, drops and attacks.");
            ImGui::SeparatorText(tr("Where"));
            bool atEnd = placesList(e);
            ImGui::SeparatorText(tr("In the game's words"));
            hint("The game has no description of its enemies.");
            if (atEnd && ImGui::GetCurrentContext()->NavCursorVisible) ImGui::SetScrollHereY(1.0f);
            endPane();
            return;
        }
        ImGui::SeparatorText(tr("Numbers"));
        numbersTable(b);
        ImGui::SeparatorText(tr("Weaknesses"));
        weaknesses(b);
        ImGui::SeparatorText(tr("Rewards"));
        rewards(b);
        ImGui::SeparatorText(tr("Attacks"));
        attacksList(e);
        ImGui::SeparatorText(tr("Where"));
        bool atEnd = placesList(e);
        char src[96];
        snprintf(src, sizeof(src), tr("Disc %d, enemy set %d (0D/%03X), type %d"), b.disc, b.set, 2 + 2 * b.set, b.type);
        hint("%s%s", src, e.records.size() > 1 ? tr("; the same enemy is in other sets too") : "");
        if (atEnd && ImGui::GetCurrentContext()->NavCursorVisible) ImGui::SetScrollHereY(1.0f);
    }
    endPane();
}

// ---- the viewer --------------------------------------------------------------------------------

void opened() {
    S = new State;
    S->view->setInteractive(false);
    S->view->setWholeFit(false); // a battle screen: fills the focused view
    for (int i = 0; i < 2; i++) {
        const char* p = cdSwapPath(i);
        XgDisc* d = p != nullptr && *p ? xgDiscOpen(p, nullptr) : nullptr;
        int n = d != nullptr ? xgDiscNumber(d) : 0;
        if (n >= 1 && n <= 2 && S->discs[n - 1] == nullptr) S->discs[n - 1] = d;
        else xgDiscClose(d);
    }
    for (auto& row : S->fieldStory) for (int& v : row) v = -1;
    settingsDescribe("extras_bestiary", "extras_bestiary: what the bestiary shows, all (default) or progress (enemies met and defeated)");
    const char* mode = settingsGet("extras_bestiary");
    S->progress = mode != nullptr && !strcmp(mode, "progress");
    // Over the paused game its record is shown; on their own, the shared one.
    if (fromGame()) {
        xgBestiaryViewGame();
        S->viewSave = -1;
    } else {
        xgBestiaryView(0);
        S->viewSave = -2;
    }
    readBeasts();
    if (S->discs[0] == nullptr && S->discs[1] == nullptr) error("No Xenogears disc image found");
    logEvent("bestiary: opened; %zu records", S->beasts.size());
}

void closed() {
    xgBestiaryViewGame();
    dropPreview();
    for (XgDisc* d : S->discs) xgDiscClose(d);
    delete S;
    S = nullptr;
}

void draw(const Area& a) {
    float u = a.unit;
    if (!S->scanned) {
        stepScan(12);
        if (S->scanned) buildEntries();
    }
    float split = floorf(fminf(220 * u, a.size().x * .40f) / u) * u;
    ImVec2 listMax(a.min.x + split, a.max.y), paneMin(a.min.x + split + 16 * u, a.min.y);
    if (!S->scanned) {
        if (beginPane("##bestscan", a.min, a.max, "Bestiary", false)) {
            ImGui::TextWrapped(tr("Reading the enemies' battles from the discs: the fields' random tables and story battles, "
                               "the world map's areas."));
            ImGui::ProgressBar((float)scanProgress(), ImVec2(-1, 0));
        }
        endPane();
        return;
    }
    if (viewerShortcuts()) {
        int delta = ImGui::IsKeyPressed(ImGuiKey_GamepadR1) - ImGui::IsKeyPressed(ImGuiKey_GamepadL1);
        if (delta) showTab((S->tab + delta + TAB_COUNT) % TAB_COUNT);
    }
    // The picture once the cursor has rested on an entry.
    if (S->sel >= 0 && S->previewFor != S->sel && ++S->settle > 6) makePreview(S->sel);
    stepPreview();
    if (S->focused) {
        focusedView(a);
        return;
    }
    listPane(a.min, listMax);
    detailPane(paneMin, a.max);
}

bool back() {
    if (S->focused) {
        setFocused(false);
        return true;
    }
    if (S->resetAsk) {
        S->resetAsk = false;
        return true;
    }
    return false;
}

void resumed() {
    S->focusList = true;
    S->previewFor = -1;
}

void suspended() { xgBattlePreviewAudio(S->preview, 0); }

int findEntry(const char* name) {
    for (int i = 0; i < (int)S->entries.size(); i++)
        if (lower(entryName(S->entries[i])) == lower(name)) return i;
    return -1;
}

bool command(const char* cmd, const char* arg) {
    if (!strcmp(cmd, "wait")) {
        while (!S->scanned) stepScan(1000);
        if (S->entries.empty()) buildEntries();
        return true;
    }
    if (!strcmp(cmd, "page")) {
        int delta = atoi(arg);
        if (delta != 1 && delta != -1) return false;
        showTab((S->tab + delta + TAB_COUNT) % TAB_COUNT);
        return true;
    }
    if (!strcmp(cmd, "tab")) {
        for (int t = 0; t < TAB_COUNT; t++)
            if (!strcmp(arg, tabIds[t])) {
                showTab(t);
                return true;
            }
        return false;
    }
    if (!strcmp(cmd, "mode")) {
        if (strcmp(arg, "all") && strcmp(arg, "progress")) return false;
        S->progress = !strcmp(arg, "progress");
        S->rowsDirty = true;
        S->detailFor = S->previewFor = -1;
        return true;
    }
    if (!strcmp(cmd, "focus")) {
        bool on = strcmp(arg, "0") != 0;
        setFocused(on);
        return S->focused == on;
    }
    if (!strcmp(cmd, "find")) {
        snprintf(S->find, sizeof(S->find), "%s", arg);
        S->rowsDirty = true;
        return true;
    }
    if (!strcmp(cmd, "select")) {
        if (S->rowsDirty) buildRows();
        int e = -1;
        if (arg[0] == '#') {
            int n = atoi(arg + 1); // the n-th enemy row (0 the first), headings not counted
            for (const Row& r : S->rows)
                if (r.entry >= 0 && n-- == 0) {
                    e = r.entry;
                    break;
                }
        } else {
            e = findEntry(arg);
        }
        if (e == -1) return false;
        select(e == -2 ? -1 : e);
        S->focusList = true;
        return true;
    }
    if (!strcmp(cmd, "attack")) {
        if (S->sel < 0) return false;
        details();
        int n = atoi(arg), k = 0;
        for (const XgBeastAttack& a : S->attacks)
            if (a.kind != 12 && k++ == n) {
                if (!playable(a)) return false;
                followBattle(S->entries[S->sel], &a);
                return true;
            }
        return false;
    }
    if (!strcmp(cmd, "catchup")) {
        int region = regionOf(1) ? regionOf(1) : regionOf(2);
        fprintf(stderr, "bestiary: caught up %d\n", catchUp(region, atoi(arg)));
        return true;
    }
    if (!strcmp(cmd, "info")) {
        int named = 0, bosses = 0, placed = 0, metCount = 0;
        for (const Entry& e : S->entries) {
            named += !e.name.empty();
            bosses += e.boss;
            placed += e.story >= 0;
            metCount += (stateOf(e) & (XG_BESTIARY_SEEN | XG_BESTIARY_CAUGHT_UP)) != 0;
        }
        fprintf(stderr, "bestiary: %zu entries (%d named, %d bosses, %d placed in the story, %d met) from %zu records, %zu places; mode %s\n",
                S->entries.size(), named, bosses, placed, metCount, S->beasts.size(), S->places.size(), S->progress ? "progress" : "all");
        if (S->sel >= 0) {
            const Entry& e = S->entries[S->sel];
            details();
            fprintf(stderr, "bestiary: selected %s: %zu records, %zu places, story %d, %zu actions, state %d\n", entryName(e).c_str(),
                    e.records.size(), e.places.size(), e.story, S->attacks.size(), stateOf(e));
        }
        return true;
    }
    if (!strcmp(cmd, "dump")) {
        if (!S->scanned) command("wait", "");
        FILE* f = fopen(arg, "w");
        if (!f) return false;
        for (const Entry& e : S->entries) {
            const XgBeast& b = shown(e);
            fprintf(f, "%s|story %d|boss %d|gear %d hp %d atk %d def %d agi %d eth %d edef %d hit %d eva %d|exp %d gold %d|"
                       "drop %d%% %s, %d%% %s|weak %s%s|res %04x %04x|vis %d|",
                    entryName(e).c_str(), e.story, e.boss, b.gear, b.hp, b.attack, b.defence, b.agility, b.ether, b.etherDefence,
                    b.hit, b.evade, b.exp, b.gold, b.drops[0].chance, b.drops[0].name, b.drops[1].chance, b.drops[1].name,
                    elementList(b.weakness & 0x3F).c_str(), (b.weakness & 0x40) ? " x1.8" : "", b.resist[0], b.resist[1], b.visual);
            for (int r : e.records) fprintf(f, " d%d/s%d/t%d", S->beasts[r].disc, S->beasts[r].set, S->beasts[r].type);
            for (int pi : e.places) {
                const XgBeastPlace& p = S->places[pi].p;
                if (p.kind == XG_PLACE_WORLD) fprintf(f, "\n    world %d zone %d range %d (%d/%d)", p.world, p.zone, p.range, p.weight, p.total);
                else fprintf(f, "\n    field %d %s config %d (%d/%d)%s story %d", p.field, p.kind == XG_PLACE_FIELD_RANDOM ? "random" : "scripted",
                             p.config, p.weight, p.total, p.randomMaybe ? " maybe" : "", S->places[pi].story);
            }
            XgBeastAttack attacks[48];
            int n = xgBeastAttacks(S->discs[b.disc - 1], b.set, b.type, attacks, 48);
            for (int i = 0; i < n; i++)
                fprintf(f, "\n    [%d] k%d atk %d anim %d '%s' val %d pow %d el %s fl %04x", attacks[i].script, attacks[i].kind,
                        attacks[i].attack, attacks[i].animation, attacks[i].name, attacks[i].value, attacks[i].power,
                        elementList(attacks[i].elements & 0x3F).c_str(), attacks[i].flags);
            fputc('\n', f);
        }
        fclose(f);
        return true;
    }
    return false;
}

const Viewer viewer = {"bestiary", "Bestiary",
                       "Every enemy: its picture, numbers, weaknesses, drops, where it appears and its attacks", 75,
                       opened, closed, draw, back, command, suspended, resumed};
} // namespace

XG_EXTRAS_VIEWER(viewer);
