// The sprite viewer (xg-htn.2): the game's sprite actors from the player's
// disc, assembled, animated and drawn by the game's own sprite code
// (sprites_game.h: sprite.c, sprite_actor.c, sprite_bytecode.c and the
// battle overlay's VM, in a sandbox), in the game's look.
//
// Sources (docs/ASSETS.md "Sprites"):
//   party (field)    04/05+c, LZSS: the party's field sprites (the kernel's
//                    startLoadingPartyCharacterFiles: file character + 5)
//   gears (field)    04/15+g: the Gears' field sprites (file gear + 0x15)
//   field NPCs       field n's section 3 (one bundle per NPC), its VRAM made
//                    as the field makes it: section 0's TIMs, section 4's
//                    sheets at the field's VRAM mapping, the texture file
//                    04/B9+2n
//   battle party     2D: bundles of 3 (the battle loader's sprites per
//                    character, battlePartyFileMapping)
//   battle specials  2D: bundles of 5, the party's special animations
//   battle effects   2C/02 and ability/spell files 0E/022+2n, with textures
//   battle enemies   0D/03+2*set, sprite entries and their shared sheets
//   party (world)    24/02+c: the world map's own party bundles
//   field scenes     04/77A+n: field scripts' special 2D animations
//                    (OP_LOAD_SPECIAL_2D_ANIMATION n), played in a party slot
//
// Each source shows its animations (played with the game's bytecode at its
// own rate: the field draws a frame every two vblanks and ticks the VM twice
// (its waits count vblanks), the battle ticks once a vblank), every facing
// an animation has, each palette, and every frame held on its own. Exports
// (PNG, our writer): the frame shown, a sheet of every frame, an animation's
// frames as a PNG sequence (one picture per drawn frame, until the animation
// ends or repeats).
//
// Script commands (XG_EXTRAS_SCRIPT "do ..."):
//   disc <n>                   the disc (1, 2) the sources come from
//   image <path>               select this exact source image
//   source <kind> <n> [i]      kind party|gear|npc|battle|special|effects|enemy|world|field2d;
//                              n: the character, Gear, field, 2D file or scene animation; i: the NPC
//                              enemy: n is the set, i its entity slot
//                              ability: n is the 0E ability id
//   tab anims|frames
//   anim <n>   dir <d>   palette <p>   frame <f>   flip <0|1>
//   play   pause   step [n]   speed <x>   loop <0|1>
//   zoom <k>                   k window pixels a sprite pixel (0: fit)
//   export frame|sheet|seq
//   info                       the source, the actor's state, its parts and
//                              primitives on stderr
//   vram <file>                the sandbox's VRAM, raw (validation)
//   catalogue <file>           NPC group/disc/field/member rows (validation)
//   sheets <file>              NPC disc/field/member/sheet kind/blank rows: what
//                              the field gives each NPC for a sheet (validation)
//   notes <file>               every NPC source opened: its picture tags (validation)
//   locations <file>           named room/group/source rows (validation)
//   seed                       the game's rand() seed on stderr (validation)
//   trace <n>                  n drawn frames, the frame each shows (timing)
//   sweep <kind|all> [from to steps]  every animation and facing of every
//                              source of a kind (npc: fields from..to), each for
//                              `steps` drawn frames (a robustness run)

#include <SDL3/SDL.h>
#include <math.h>
#include <stdarg.h>
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
#include "picker.h"
#include "ui/l10n.h"
#include "ui/skin.h"
#include "view.h"
#include "viewer.h"
#include "library.h"
#include "export_provenance.h"
#include "bookmark_source.h"
#include "battlefx_assets.h"
#include "sprites_catalogue.h"
#include "entity_names.h"
#include "field_locations.h"
#include "replace.h"

extern "C" {
#include "cd/cd.h"
#include "disc.h"
#include "files.h"
#include "sprites_game.h"
#include "sprites_enemy.h"
#include "tables.h"
#include "writers.h"
}

namespace {

using namespace extras;

// The packs a sprite can show (the Enhanced look): the sprite library's cels and poses
// (Sprites -> Cel 8x decides its first view).
const unsigned spriteLibraries = XG_REPLACE_CELS | XG_REPLACE_POSES;

enum Kind { K_PARTY, K_GEAR, K_NPC, K_BATTLE, K_SPECIAL, K_EFFECTS, K_ENEMY, K_WORLD, K_FIELD2D, K_COUNT };
const char* const kindNames[K_COUNT] = {N_("Party (field)"),  N_("Gears (field)"),   N_("Field NPCs"),
                                        N_("Battle party"),   N_("Battle specials"), N_("Spells / effects"), N_("Battle enemies"),
                                        N_("Party (world map)"), N_("Field scene animations")};
const char* const kindIds[K_COUNT] = {"party", "gear", "npc", "battle", "special", "effects", "enemy", "world", "field2d"};

#define FIELD_COUNT 730
#define MAX_READ (8 << 20)

// One sprite bundle on the disc.
struct Entry {
    int disc = 0;
    int kind = 0;
    int dir = 0, file = 0;   // the bundle's file (an NPC: the field archive)
    int field = -1, npc = -1;
    int enemy = -1, palette = 0;
    int partyFile = 0;       // normal sprite bank for an ability deathblow
    int npcGroup = -1;
    std::vector<int> locations; // canonical NPC row: indices into npcSources
    std::string label;       // its row in the list
    std::string title;       // its name over the picture
    std::string npcName;     // speaker identified in this field's own talk script
    std::string same;        // "same as ..." when the file repeats another's bytes
    int size = 0;
    // An NPC's sheet as the field gives it (sprites_catalogue.h, xg-htn.35.68):
    // NpcSheet::Kind, -1 not known; blank: its frames sample only colour 0.
    int sheet = -1;
    bool blank = false;
};

bool sameNpcRow(const Entry& a, const Entry& b) {
    if (a.kind != K_NPC || b.kind != K_NPC || a.npcGroup < 0 || a.npcGroup != b.npcGroup) return false;
    const auto& x = fieldLocation(a.field);
    const auto& y = fieldLocation(b.field);
    return !strcmp(x.area, y.area) && !strcmp(x.room, y.room);
}

struct Category {
    bool expanded = false;
    bool listed = false;
    std::vector<Entry> entries;
};

struct Disc {
    XgDisc* disc = nullptr;
    std::string path;
    std::shared_ptr<EntityNames> names;
};

struct NpcRow {
    std::string heading;
    int source = -1;
    int number = 0;
};


std::string entryKey(const Entry& e) {
    if (e.kind == K_NPC && e.npcGroup >= 0) {
        const auto& l = fieldLocation(e.field);
        return "npc/" + std::string(l.area) + "/" + l.room + "/" + std::to_string(e.npcGroup);
    }
    char key[128];
    snprintf(key, sizeof(key), "%d/%d/%d/%d/%d/%d/%d/%d/%d", e.disc, e.kind, e.dir, e.file, e.field, e.npc, e.enemy,
             e.palette, e.partyFile);
    return key;
}

enum Tab { TAB_ANIMS, TAB_FRAMES };

struct State {
    std::vector<Disc> discs;
    int disc = 0;
    Category cats[K_COUNT];
    std::vector<Entry> npcSources; // every original source, including grouped copies
    std::vector<NpcRow> npcCatalogueRows;
    std::vector<NpcRow> npcRows;
    std::vector<int> npcBrowse; // row indices, skipping section headings
    int npcRooms = 0;
    float npcWidth = 0, npcFontSize = 0;
    int section = -1;
    bool exactDisc = false;
    float turn = 0;
    ImGuiID highlight = 0;
    int characterStep = 0;
    bool optionsRequested = false, optionsOpen = false;
    // The focused view (viewer.h): the source chosen, alone across the area.
    bool focused = false, focusRequested = false;
    std::string focusKey;    // the source it opened on (Back: unchanged, the list's scroll stays)
    std::string focusParent;
    std::string fieldWhy;    // why an exact field/NPC source is unavailable
    // the source shown
    bool haveSource = false;
    Entry entry;
    std::string name;        // for exports: "us1/sprites/party_005"
    XgSprite* sprite = nullptr;
    std::vector<std::pair<int, int>> modelFiles;
    std::string why;
    std::string npcNote;     // what the field gives this NPC for a sheet, when it is not its own art
    std::string npcTag;      // the same in a few words, over the picture
    int bx0 = 0, by0 = 0, bx1 = 0, by1 = 0; // every frame's bounds (screen units)
    bool haveBounds = false;
    bool haveAnimBounds = false; // effect controllers can have no root frames
    int ax0 = 0, ay0 = 0, ax1 = 0, ay1 = 0; // the animation's first seconds' bounds, with every frame's
    int sceneFor = -1;                      // the animation, facing and palette they are for
    float sceneShown[4] = {};               // the extent the picture fits
    // what it shows
    int tab = TAB_ANIMS;
    int anim = 0, dir = 0, palette = 0, frame = 1;
    bool flip = false;
    bool playing = true, loop = true;
    float speed = 1.0f;
    double clock = 0;        // drawn frames owed
    double lastTime = -1;
    bool started = false;    // the selection is in the world
    int steps = 0;
    XgSpriteState state = {};
    // the picture
    std::unique_ptr<View> view;
    std::unique_ptr<View> stockView; // Compare: the stock picture beside the enhanced one
    bool edges = false, transparent = true;
    float wantZoom = -1;     // game pixels a sprite pixel (0: fit, -2: the largest whole one that fits)
    bool scrollTo = false;   // the sources: the chosen one into view
    bool navReset = false;   // the left pane changed: its first item gets the cursor
};

State* S;
LibraryStates<State> slots;

// ---- the disc --------------------------------------------------------------------------------------

XgDisc* disc() {
    return S->discs.empty() ? nullptr : S->discs[(size_t)S->disc].disc;
}

EntityNames& entityNames() {
    auto& d = S->discs[(size_t)S->disc];
    if (!d.names) d.names = std::make_shared<EntityNames>(d.disc);
    return *d.names;
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

// A file's bytes (whole sectors read; `size` the file's own), empty if none.
std::vector<uint8_t> readFile(int dir, int file, int* size = nullptr) {
    std::vector<uint8_t> out;
    XgDisc* d = disc();
    int index = d != nullptr ? xgDiscFileEntry(d, dir, file) : -1;
    if (index < 0) {
        return out;
    }
    XgEntry e = xgDiscEntry(d, index);
    if (e.size <= 0 || e.size > MAX_READ) {
        return out;
    }
    int n = 0;
    uint8_t* data = xgDiscReadEntry(d, index, &n);
    if (data != nullptr) {
        out.assign(data, data + ((n + 2047) & ~2047));
        out.resize((size_t)((n + 2047) & ~2047) + 64, 0);
        free(data);
    }
    if (size != nullptr) {
        *size = out.empty() ? 0 : n;
    }
    return out;
}

std::vector<uint8_t> lzss(const uint8_t* data, int size) {
    std::vector<uint8_t> out;
    int n = 0;
    uint8_t* d = xgLzssDecompress(data, size, &n);
    if (d != nullptr) {
        out.assign(d, d + n);
        free(d);
    }
    return out;
}

uint32_t rd32(const uint8_t* p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

// An offset bundle's member i (to the next member or the end), as [at, size).
bool member(const std::vector<uint8_t>& b, int size, int i, int* at, int* msize) {
    XgBundle bundle;
    if (!xgBundleParse(b.data(), size, &bundle) || i < 0 || i >= bundle.count) {
        return false;
    }
    *at = xgBundleMember(b.data(), size, &bundle, i, msize);
    return *at >= 0 && *msize > 0;
}

// The look of a sprite bundle: count 3-5, animations, frames and palettes
// inside it, a container count and a frame table that read sensibly.
bool spriteBundle(const uint8_t* b, int size) {
    if (size < 32) {
        return false;
    }
    uint32_t n = rd32(b), a = rd32(b + 4), f = rd32(b + 8), c = rd32(b + 12);
    if (n < 3 || n > 6 || a < 16 || f <= a || c <= f || c + 4 > (uint32_t)size) {
        return false;
    }
    int anims = (b[a] | b[a + 1] << 8) & 0x3F;
    int frames = (b[f] | b[f + 1] << 8) & 0x1FF;
    return anims > 0 && frames > 0;
}

// ---- the sources ------------------------------------------------------------------------------------

uint32_t hashBytes(const std::vector<uint8_t>& b, int size) {
    uint32_t h = 2166136261u;
    for (int i = 0; i < size && i < (int)b.size(); i++) {
        h = (h ^ b[(size_t)i]) * 16777619u;
    }
    return h;
}

void listDiscCategory(int kind) {
    Category& c = S->cats[kind];
    c.entries.clear();
    c.listed = true;
    XgDisc* d = disc();
    if (d == nullptr) {
        return;
    }
    std::vector<std::pair<uint32_t, std::string>> seen;
    auto add = [&](Entry e, const std::vector<uint8_t>& bytes, int size) {
        uint32_t h = hashBytes(bytes, size) ^ (uint32_t)size;
        for (auto& s : seen) {
            if (s.first == h) {
                e.same = s.second;
            }
        }
        if (e.same.empty()) {
            seen.push_back({h, e.label});
        }
        e.size = size;
        c.entries.push_back(e);
    };
    if (kind == K_PARTY || kind == K_GEAR) {
        // The kernel loads character c's sprites from file c + 5 and Gear
        // g's from g + 0x15 of directory 4 (startLoadingPartyCharacterFiles,
        // startLoadingPartyGearFiles), LZSS (processLoadedPartyFiles).
        int first = kind == K_PARTY ? 5 : 0x15, last = kind == K_PARTY ? 0x14 : 0x15 + 0x1F;
        for (int f = first; f <= last && f <= xgDiscDirFiles(d, 4); f++) {
            int size = 0;
            std::vector<uint8_t> raw = readFile(4, f, &size);
            if (raw.empty()) {
                if (kind == K_GEAR) {
                    break;
                }
                continue;
            }
            std::vector<uint8_t> b = lzss(raw.data(), size);
            if (!spriteBundle(b.data(), (int)b.size())) {
                continue;
            }
            Entry e;
            e.kind = kind;
            e.dir = 4;
            e.file = f;
            char label[128];
            std::string name = kind == K_PARTY ? entityNames().party(f - first) : entityNames().gear(f - first);
            snprintf(label, sizeof(label), "%s %d / 04/%03X", kind == K_PARTY ? "char" : "gear", f - first, f);
            e.label = e.title = entityLabel(name, label);
            add(e, b, (int)b.size());
        }
    } else if (kind == K_WORLD) {
        // The world map loads character c's sprites from file c + 2 of directory 0x24
        // (worldmapMode0_init), its own smaller bundles beside the field ones.
        for (int f = 2; f <= 2 + 0x0F && f <= xgDiscDirFiles(d, 0x24); f++) {
            int size = 0;
            std::vector<uint8_t> raw = readFile(0x24, f, &size);
            if (raw.empty()) continue;
            std::vector<uint8_t> b = spriteBundle(raw.data(), size) ? std::vector<uint8_t>(raw.begin(), raw.begin() + size) : lzss(raw.data(), size);
            if (!spriteBundle(b.data(), (int)b.size())) continue;
            Entry e;
            e.kind = kind;
            e.dir = 0x24;
            e.file = f;
            char label[128];
            snprintf(label, sizeof(label), "world map char %d / 24/%03X", f - 2, f);
            e.label = e.title = entityLabel(entityNames().party(f - 2), label);
            add(e, b, (int)b.size());
        }
    } else if (kind == K_FIELD2D) {
        // A field script's special 2D animation n (OP_LOAD_SPECIAL_2D_ANIMATION) is file
        // n + 0x77A of directory 4: a bundle of its own (bytecode, frames, sheet) that the actor
        // plays instead of its own (Fei painting at his easel, field 14: n = 60).
        for (int f = 0x77A; f <= xgDiscDirFiles(d, 4); f++) {
            int size = 0;
            std::vector<uint8_t> raw = readFile(4, f, &size);
            if (raw.empty() || size <= 0) break;
            std::vector<uint8_t> b = spriteBundle(raw.data(), size) ? std::vector<uint8_t>(raw.begin(), raw.begin() + size) : lzss(raw.data(), size);
            if (!spriteBundle(b.data(), (int)b.size())) continue;
            Entry e;
            e.kind = kind;
            e.dir = 4;
            e.file = f;
            char label[128];
            snprintf(label, sizeof(label), "scene animation %d / 04/%03X", f - 0x77A, f);
            e.label = e.title = label;
            add(e, b, (int)b.size());
        }
    } else if (kind == K_BATTLE || kind == K_SPECIAL) {
        // Directory 0x2D: the party's battle sprites (bundles of 3: what the
        // battle loader loads for each character) and their special
        // animations (bundles of 5: with their own sound effects and samples).
        int files = xgDiscDirFiles(d, 0x2D);
        auto abilities = kind == K_SPECIAL ? battleAbilities(d) : std::vector<BattleAbility>{};
        auto addAbility = [&](const BattleAbility& ability) {
            Entry e;
            e.kind = kind; e.dir = 0x0E; e.file = ability.file; e.partyFile = ability.partyFile;
            e.label = e.title = ability.name;
            c.entries.push_back(e);
        };
        for (int f = 1; f <= files && f < 0x80; f++) {
            int size = 0;
            std::vector<uint8_t> b = readFile(0x2D, f, &size);
            if (!spriteBundle(b.data(), size) || (int)rd32(b.data()) != (kind == K_BATTLE ? 3 : 5)) {
                continue;
            }
            Entry e;
            e.kind = kind;
            e.dir = 0x2D;
            e.file = f;
            char label[128];
            std::string name = entityNames().battleSprite(f);
            snprintf(label, sizeof(label), "2D/%03X", f);
            e.label = entityLabel(name, label);
            snprintf(label, sizeof(label), "%s / 2D/%03X", kind == K_BATTLE ? "battle sprites" : "battle specials", f);
            e.title = entityLabel(name, label);
            if (kind == K_SPECIAL && f == 0x12) {
                e.label = "Fei specials";
                e.title = "Fei - battle specials  2D/012";
            }
            add(e, b, size);
            if (kind == K_SPECIAL && f == 0x12)
                for (const auto& ability : abilities)
                    if (ability.partyFile) addAbility(ability);
        }
        if (kind == K_SPECIAL) {
            for (const auto& ability : abilities)
                if (ability.pieces && !ability.partyFile) addAbility(ability);
        }
    } else if (kind == K_ENEMY) {
        for (int f = 3; f <= xgDiscDirFiles(d, 0x0D); f += 2) {
            int size = 0;
            std::vector<uint8_t> raw = readFile(0x0D, f, &size);
            for (int i = 0; i < xgEnemySpriteCount(raw.data(), size); i++) {
                XgEnemySprite sprite;
                if (xgEnemySpriteGet(raw.data(), size, i, &sprite) != 1 ||
                    !spriteBundle(raw.data() + sprite.bundleOffset, sprite.bundleSize)) continue;
                Entry e;
                e.kind = kind; e.dir = 0x0D; e.file = f; e.enemy = i; e.palette = sprite.palette;
                char label[192];
                std::string name = entityNames().enemy((f - 3) / 2, i);
                snprintf(label, sizeof(label), "set %02d.%d / palette %d / 0D/%03X", (f - 3) / 2, i, e.palette, f);
                e.label = entityLabel(name, label);
                e.title = "Enemy " + e.label;
                e.size = sprite.bundleSize;
                // Shared control bytes can have different sheets or palettes.
                // Every native entity slot remains selectable.
                c.entries.push_back(e);
            }
        }
    } else if (kind == K_EFFECTS) {
        int size = 0;
        std::vector<uint8_t> b = readFile(0x2C, 2, &size);
        if (spriteBundle(b.data(), size)) {
            Entry e;
            e.kind = kind;
            e.dir = 0x2C;
            e.file = 2;
            e.label = "Effect sprites  2C/002";
            e.title = "Battle effect sprites (2C/002)";
            add(e, b, size);
        }
        for (const auto& ability : battleAbilities(d)) {
            Entry e;
            e.kind = kind; e.dir = 0x0E; e.file = ability.file; e.partyFile = ability.partyFile;
            e.label = e.title = ability.name;
            c.entries.push_back(e);
        }
    }
}

void listCategory(int kind) {
    int previous = S->disc;
    std::vector<Entry> entries;
    std::vector<uint64_t> hashes;
    for (int i = 0; i < (int)S->discs.size(); ++i) {
        if (S->exactDisc && i != previous) continue;
        S->disc = i;
        listDiscCategory(kind);
        for (Entry e : S->cats[kind].entries) {
            e.disc = i;
            uint64_t hash = libraryFileHash(disc(), e.dir, e.file);
            if (e.kind == K_ENEMY || e.dir == 0x0E) hash ^= libraryFileHash(disc(), e.dir, e.file + 1);
            bool duplicate = false, alternate = false;
            for (size_t n = 0; n < entries.size(); ++n) {
                const auto& old = entries[n];
                if (old.dir == e.dir && old.file == e.file && old.enemy == e.enemy && old.partyFile == e.partyFile) {
                    duplicate |= hashes[n] == hash;
                    alternate = true;
                }
            }
            if (!duplicate) {
                if (alternate) e.label += " (alternate)";
                entries.push_back(e); hashes.push_back(hash);
            }
        }
    }
    S->disc = previous;
    S->cats[kind].entries = std::move(entries);
}

// Field n's archive and its sections (docs/ASSETS.md "Field archive"): the
// sections' sizes at +0x10C, their offsets at +0x130, each its own LZSS.
struct FieldData {
    std::vector<uint8_t> archive;
    int archiveSize = 0;
    std::vector<uint8_t> section[9];
    XgSpriteSlot slots[32] = {};
};

bool readField(int n, FieldData* f, std::string* why) {
    f->archive = readFile(4, 0xB8 + 2 * n, &f->archiveSize);
    if (f->archiveSize < 0x190) {
        *why = f->archiveSize == 0 ? "no such field on this disc" : "a placeholder on this disc (disc 1 has it)";
        return false;
    }
    for (int i = 0; i < 32; i++) {
        const uint8_t* r = f->archive.data() + 8 * i;
        f->slots[i].x = r[0] | r[1] << 8;
        f->slots[i].y = r[2] | r[3] << 8;
        f->slots[i].fromTextureFile = (r[6] | r[7] << 8) != 0;
    }
    return true;
}

bool fieldSection(FieldData* f, int k) {
    if (!f->section[k].empty()) {
        return true;
    }
    uint32_t at = rd32(f->archive.data() + 0x130 + 4 * k);
    if (at == 0 || at >= (uint32_t)f->archiveSize) {
        return false;
    }
    // The stream runs to the next section (the checked decoder wants its
    // extent).
    uint32_t end = (uint32_t)f->archiveSize;
    for (int j = 0; j < 9; j++) {
        uint32_t o = rd32(f->archive.data() + 0x130 + 4 * j);
        if (o > at && o < end) {
            end = o;
        }
    }
    f->section[k] = lzss(f->archive.data() + at, (int)(end - at));
    return !f->section[k].empty();
}

std::vector<Entry> listField(int n, std::vector<std::vector<uint8_t>>* identities = nullptr) {
    struct RestoreDisc { int& slot; int previous; ~RestoreDisc() { slot = previous; } } restore{S->disc, S->disc};
    std::vector<Entry> entries;
    S->fieldWhy.clear();
    if (!S->exactDisc) {
        for (int i = 0; i < (int)S->discs.size(); ++i) {
            int at = xgDiscFileEntry(S->discs[i].disc, 4, 0xB8 + 2 * n);
            if (at >= 0 && xgDiscEntry(S->discs[i].disc, at).size >= 0x190) { S->disc = i; break; }
        }
    }
    FieldData f;
    if (!readField(n, &f, &S->fieldWhy)) {
        return entries;
    }
    if (!fieldSection(&f, 3)) {
        S->fieldWhy = "its section 3 does not decompress";
        return entries;
    }
    const std::vector<uint8_t>& s3 = f.section[3];
    int count = s3.size() >= 4 ? (int)rd32(s3.data()) : 0;
    std::vector<uint16_t> vram;
    if (identities && count > 0) {
        fieldSection(&f, 0);
        fieldSection(&f, 4);
        int texSize = 0;
        auto tex = readFile(4, 0xB9 + 2 * n, &texSize);
        XgSpriteSetup st = {};
        st.tims = f.section[0].empty() ? nullptr : f.section[0].data();
        st.timsSize = (int)f.section[0].size();
        st.sheets = f.section[4].empty() ? nullptr : f.section[4].data();
        st.sheetsSize = (int)f.section[4].size();
        st.slots = f.slots;
        st.slotCount = 32;
        st.textureFile = texSize > 0 ? tex.data() : nullptr;
        st.textureFileSize = texSize;
        vram.resize(XG_VRAM_W * XG_VRAM_H);
        if (!xgSpriteFieldVram(&st, vram.data())) vram.clear();
    }
    for (int i = 0; i < count && i < 32; i++) {
        int at, size;
        if (!member(s3, (int)s3.size(), i, &at, &size) || !spriteBundle(s3.data() + at, size)) {
            continue;
        }
        Entry e;
        e.disc = S->disc;
        e.kind = K_NPC;
        e.dir = 4;
        e.file = 0xB8 + 2 * n;
        e.field = n;
        e.npc = i;
        const auto& location = fieldLocation(n);
        char label[32];
        snprintf(label, sizeof(label), "NPC %02d", i);
        std::string name = entityNames().npc(n, i);
        e.npcName = name;
        e.label = name.empty() ? label : name + " (" + label + ")";
        e.title = e.label + " - " + location.area + " / " + location.room + " (field " + std::to_string(n) + ")";
        if (identities) identities->push_back(npcIdentity(s3.data() + at, size,
            vram.empty() ? nullptr : vram.data(), f.slots[i].x, f.slots[i].y));
        if (identities) {
            e.sheet = npcSheet(f.section[4].empty() ? nullptr : f.section[4].data(), (int)f.section[4].size(), i,
                               f.slots[i].x, f.slots[i].y, f.slots[i].fromTextureFile).kind;
            e.blank = !vram.empty() && npcBlank(s3.data() + at, size, vram.data(), f.slots[i].x, f.slots[i].y);
        }
        e.size = size;
        entries.push_back(e);
    }
    if (entries.empty()) {
        S->fieldWhy = "no sprites";
    }
    return entries;
}

void listFields() {
    Category& c = S->cats[K_NPC];
    c.entries.clear();
    S->npcSources.clear();
    S->npcCatalogueRows.clear();
    S->npcRows.clear();
    S->npcBrowse.clear();
    S->npcRooms = 0;
    S->npcWidth = 0;
    c.listed = true;
    auto compare = [](const std::vector<uint8_t>& a, const std::vector<uint8_t>& b) {
        if (a.size() != b.size()) return a.size() < b.size();
        return !a.empty() && memcmp(a.data(), b.data(), a.size()) < 0;
    };
    std::map<std::vector<uint8_t>, int, decltype(compare)> groups(compare);
    for (int field = 0; field < FIELD_COUNT; ++field) {
        std::vector<std::vector<uint8_t>> identities;
        auto entries = listField(field, &identities);
        for (size_t i = 0; i < entries.size(); ++i) {
            Entry e = entries[i];
            int group = (int)c.entries.size();
            if (!identities[i].empty()) {
                auto inserted = groups.emplace(std::move(identities[i]), group);
                group = inserted.first->second;
            }
            e.npcGroup = group;
            if (group == (int)c.entries.size()) c.entries.push_back(e);
            c.entries[group].locations.push_back((int)S->npcSources.size());
            S->npcSources.push_back(std::move(e));
        }
    }
    // A shared asset belongs to every room that uses it. Story versions of
    // the same room contribute one row per sprite, without losing aliases.
    using Place = std::pair<std::string, std::string>;
    std::map<Place, std::vector<std::vector<int>>> rooms;
    std::map<Place, std::map<int, int>> seen;
    for (int i = 0; i < (int)S->npcSources.size(); ++i) {
        const Entry& e = S->npcSources[i];
        const auto& l = fieldLocation(e.field);
        Place place{l.area, l.room};
        auto position = seen[place].emplace(e.npcGroup, (int)rooms[place].size());
        if (position.second) rooms[place].push_back({});
        rooms[place][position.first->second].push_back(i);
    }
    for (const auto& room : rooms) {
        S->npcRows.push_back({room.first.first});
        S->npcRows.push_back({"  " + room.first.second});
        int number = 0;
        for (const auto& aliases : room.second) {
            // Prefer the story version whose own actor names the speaker.
            // A reused sprite with conflicting speakers retains its first
            // source; never spread an identity into another room.
            int source = aliases.front(), named = source;
            std::set<std::string> names;
            for (int alias : aliases) if (!S->npcSources[alias].npcName.empty()) {
                names.insert(S->npcSources[alias].npcName);
                named = alias;
            }
            if (names.size() == 1) source = named;
            S->npcBrowse.push_back((int)S->npcRows.size());
            S->npcRows.push_back({{}, source, ++number});
        }
    }
    S->npcRooms = (int)rooms.size();
    S->npcCatalogueRows = S->npcRows;
    S->fieldWhy.clear();
}

// ---- the source shown ------------------------------------------------------------------------------

void closeSource() {
    xgSpriteDestroy(S->sprite);
    S->sprite = nullptr;
    S->haveSource = false;
    S->why.clear();
}

void restart();
double stepRate();

// What the library says about an NPC whose sheet is not its own art
// (xg-htn.35.68): the retail record as it is, never other artwork.
std::string npcSheetNote(const Entry& e, const FieldData& fd, const XgSpriteSetup& st, std::string* tag) {
    const XgSpriteSlot& slot = fd.slots[e.npc];
    NpcSheet sheet = npcSheet(fd.section[4].empty() ? nullptr : fd.section[4].data(), (int)fd.section[4].size(),
                              e.npc, slot.x, slot.y, slot.fromTextureFile);
    std::string note;
    auto add = [&note, tag](const std::string& line, const char* shortly) {
        note += (note.empty() ? "" : " ") + line;
        *tag += (tag->empty() ? "" : "\n") + std::string(shortly);
    };
    if (sheet.kind == NpcSheet::PLACEHOLDER)
        add(tr("Retail placeholder: the field's sheet for this NPC is the \"No Image Printing\" card, not artwork."),
            tr("Placeholder sheet"));
    else if (sheet.kind == NpcSheet::NONE)
        add(tr("The field gives this NPC no sheet of its own: it shows what its VRAM slot holds."), tr("No own sheet"));
    std::vector<uint16_t> vram((size_t)XG_VRAM_W * XG_VRAM_H);
    if (!xgSpriteFieldVram(&st, vram.data())) return note;
    if ((sheet.kind == NpcSheet::OWN || sheet.kind == NpcSheet::PLACEHOLDER) && st.textureFile != nullptr) {
        // The field's VRAM without its texture file (loadFieldGraphics comes
        // after initFieldData's sheets).
        XgSpriteSetup sheetsOnly = st;
        sheetsOnly.textureFile = nullptr;
        sheetsOnly.textureFileSize = 0;
        std::vector<uint16_t> before((size_t)XG_VRAM_W * XG_VRAM_H);
        if (xgSpriteFieldVram(&sheetsOnly, before.data()) && npcSheetReplaced(sheet, before.data(), vram.data()))
            add(tr("The field's texture file then loads over this sheet's page: the picture is the page as the field "
                   "leaves it, which the frames read."), tr("Page loaded over by the field"));
    }
    return note;
}

// Every frame held on its own, with each palette, rasterized by the views'
// rules (xgSpriteRaster): true when none has a drawn pixel. Leaves the actor
// for restart() to set up again.
bool drawsNothing(XgSprite* sp) {
    int x0, y0, x1, y1;
    if (!xgSpriteFrameBounds(sp, &x0, &y0, &x1, &y1)) return true;
    int u = std::max(1, xgSpriteUnits(sp));
    int ax = (int)floor((double)x0 / u) * u - u, ay = (int)floor((double)y0 / u) * u - u;
    int w = (int)ceil((double)(x1 - ax) / u) + 2, h = (int)ceil((double)(y1 - ay) / u) + 2;
    if (w <= 0 || h <= 0 || (long long)w * h > 1 << 20) return false;
    std::vector<uint32_t> rgba((size_t)w * h);
    int palettes = std::max(1, std::min(xgSpritePaletteCount(sp), 16));
    for (int p = 0; p < palettes; ++p)
        for (int f = 1; f <= xgSpriteFrameCount(sp); ++f) {
            if (!xgSpriteShowFrame(sp, f, 0, p)) continue;
            xgSpriteStep(sp, 0);
            xgSpriteRaster(sp, ax, ay, w, h, 0, rgba.data());
            for (uint32_t px : rgba)
                if (px >> 24) return false;
        }
    return true;
}

// Every safe animation, started as the viewer starts one (the middle facing,
// palette 0), for its first 60 drawn frames: true when no step draws a
// visible pixel (frames held on their own may: drawsNothing is the stricter).
bool animationsDrawNothing(XgSprite* sp) {
    std::vector<uint32_t> rgba;
    int u = std::max(1, xgSpriteUnits(sp)), any = 0;
    for (int a = 0; a < xgSpriteAnimationCount(sp); ++a) {
        if (xgSpriteAnimationFlags(sp, a) & XG_SPRITE_ANIM_UNSAFE) continue;
        if (!xgSpritePlay(sp, a, xgSpriteAnimationDirections(sp, a) / 2, 0)) continue;
        ++any;
        for (int k = 0; k < 60; ++k) {
            xgSpriteStep(sp, 1);
            int x0, y0, x1, y1;
            if (!xgSpritePrimBounds(sp, &x0, &y0, &x1, &y1)) continue;
            int ax = (int)floor((double)x0 / u) * u - u, ay = (int)floor((double)y0 / u) * u - u;
            int w = (int)ceil((double)(x1 - ax) / u) + 2, h = (int)ceil((double)(y1 - ay) / u) + 2;
            if (w <= 0 || h <= 0 || (long long)w * h > 1 << 20) return false;
            rgba.assign((size_t)w * h, 0);
            xgSpriteRaster(sp, ax, ay, w, h, 0, rgba.data());
            for (uint32_t px : rgba)
                if (px >> 24) return false;
        }
    }
    return any > 0;
}

bool openSource(const Entry& e) {
    S->disc = e.disc;
    closeSource();
    S->haveSource = true;
    S->entry = e;
    XgSpriteSetup st = {};
    BattleSpriteAsset abilityAsset;
    std::vector<uint8_t> bundle, companion, effects, effectsTexture, tex, enemySheet;
    FieldData fd;
    char name[96];
    switch (e.kind) {
    case K_PARTY:
    case K_GEAR:
    case K_WORLD:
    case K_FIELD2D: {
        int size = 0;
        std::vector<uint8_t> raw = readFile(e.dir, e.file, &size);
        bundle = (e.kind == K_WORLD || e.kind == K_FIELD2D) && spriteBundle(raw.data(), size) ? std::vector<uint8_t>(raw.begin(), raw.begin() + size) : lzss(raw.data(), size);
        // resetFieldScriptEntityGraphicEntity, a party slot (clutBank 1, slot 0)
        st.vramX = 0x280;
        st.vramY = 0x100;
        st.clutX = 0x100;
        st.clutY = 0xE0;
        st.subSprites = 0x20;
        snprintf(name, sizeof(name), "%s_%03x", kindIds[e.kind], e.file);
        break;
    }
    case K_NPC: {
        std::string why;
        if (!readField(e.field, &fd, &why) || !fieldSection(&fd, 3)) {
            S->why = "field " + std::to_string(e.field) + ": " + why;
            return false;
        }
        int at, size;
        if (!member(fd.section[3], (int)fd.section[3].size(), e.npc, &at, &size)) {
            S->why = "no such NPC";
            return false;
        }
        bundle.assign(fd.section[3].begin() + at, fd.section[3].begin() + at + size);
        fieldSection(&fd, 0);
        fieldSection(&fd, 4);
        int texSize = 0;
        tex = readFile(4, e.file + 1, &texSize);
        // OP_INIT_ENTITY_NPC: VRAM slot i of the field's mapping, CLUT row 0x1E0 + i
        st.vramX = fd.slots[e.npc].x;
        st.vramY = fd.slots[e.npc].y;
        st.clutX = 0x100;
        st.clutY = 0x1E0 + e.npc;
        st.tims = fd.section[0].empty() ? nullptr : fd.section[0].data();
        st.timsSize = (int)fd.section[0].size();
        st.sheets = fd.section[4].empty() ? nullptr : fd.section[4].data();
        st.sheetsSize = (int)fd.section[4].size();
        st.slots = fd.slots;
        st.slotCount = 32;
        st.textureFile = texSize > 0 ? tex.data() : nullptr;
        st.textureFileSize = texSize;
        snprintf(name, sizeof(name), "field%03d_npc%02d", e.field, e.npc);
        break;
    }
    case K_ENEMY: {
        int size = 0;
        std::vector<uint8_t> raw = readFile(e.dir, e.file, &size);
        XgEnemySprite sprite;
        if (xgEnemySpriteGet(raw.data(), size, e.enemy, &sprite) != 1) {
            S->why = "Enemy sprite data is unavailable";
            return false;
        }
        bundle.assign(raw.begin() + sprite.bundleOffset, raw.begin() + sprite.bundleOffset + sprite.bundleSize);
        enemySheet.assign(raw.begin() + sprite.sheetOffset, raw.begin() + sprite.sheetOffset + sprite.sheetSize);
        st.enemySheet = enemySheet.data(); st.enemySheetSize = (int)enemySheet.size();
        st.enemyPalette = sprite.palette;
        st.battle = 1; st.vramX = sprite.vramX; st.vramY = 0x100;
        st.clutX = 0; st.clutY = 0x1C3;
        snprintf(name, sizeof(name), "enemy_%02d_%d", (e.file - 3) / 2, e.enemy);
        break;
    }
    case K_BATTLE:
    case K_SPECIAL:
    case K_EFFECTS: {
        if (e.dir == 0x0E) {
            if (!abilityAsset.load(disc(), e.dir, e.file, e.partyFile)) {
                S->why = "The ability's sprite data or matching party bank is unavailable";
                return false;
            }
            st = abilityAsset.setup();
            snprintf(name, sizeof(name), "ability_%03d", (e.file - 0x22) / 2);
            break;
        }
        int size = 0;
        bundle = readFile(e.dir, e.file, &size);
        bundle.resize((size_t)size);
        st.battle = 1;
        // Retail party files 2D/001..00B pair with 2D/012..01C.
        // Their programs switch frame banks without changing bytecode.
        int paired = e.kind == K_BATTLE && e.file >= 1 && e.file <= 0xB ? e.file + 0x11 :
                     e.kind == K_SPECIAL && e.file >= 0x12 && e.file <= 0x1C ? e.file - 0x11 : 0;
        // These retail files are byte-identical Elly aliases on both discs.
        if (e.kind == K_BATTLE && e.file >= 0xC && e.file <= 0x11) {
            int aliasSize = 0;
            std::vector<uint8_t> alias = readFile(e.dir, 2, &aliasSize);
            alias.resize((size_t)aliasSize);
            if (bundle == alias) paired = 0x13;
        }
        if (e.kind == K_SPECIAL && e.file >= 0x1D && e.file <= 0x1E) {
            int aliasSize = 0;
            std::vector<uint8_t> alias = readFile(e.dir, 0x13, &aliasSize);
            alias.resize((size_t)aliasSize);
            if (bundle == alias) paired = 2;
        }
        if (paired) {
            int pairedSize = 0;
            companion = readFile(e.dir, paired, &pairedSize);
            if (!spriteBundle(companion.data(), pairedSize)) {
                S->why = "The matching battle sprite bundle is unavailable";
                return false;
            }
            companion.resize((size_t)pairedSize);
            st.companion = companion.data(); st.companionSize = pairedSize;
            st.special = e.kind == K_SPECIAL;
            st.subSprites = 0x20; // createBattlePlayerSpriteActors' shared part buffer
        }
        if (e.kind == K_EFFECTS) {
            st.vramX = 0x380; // mainBattleSpriteCallback_phase3
            st.vramY = 0;
            st.clutX = 0;
            st.clutY = 0x1D1;
        } else {
            st.vramX = 0x100; // createBattlePlayerSpriteActors: the first party slot
            st.vramY = 0x1C0;
            st.clutX = 0;
            st.clutY = 0x1C0;
        }
        snprintf(name, sizeof(name), "%s_%03x", kindIds[e.kind], e.file);
        break;
    }
    default:
        return false;
    }
    // Battle effects occupy pages also used by field NPC sheets. Load them
    // only for battle actors, as the game does when entering battle.
    if (st.battle) {
        int fxSize = 0, texSize = 0;
        effects = readFile(0x2C, 2, &fxSize);
        effectsTexture = readFile(0x2C, 1, &texSize);
        if (fxSize > 0 && spriteBundle(effects.data(), fxSize)) {
            st.effects = effects.data();
            st.effectsSize = fxSize;
            st.effectsTexture = texSize > 0 ? effectsTexture.data() : nullptr;
            st.effectsTextureSize = texSize;
        }
    }
    if (e.dir != 0x0E) {
        st.bundle = bundle.data();
        st.bundleSize = (int)bundle.size();
    }
    char why[160] = "";
    S->sprite = xgSpriteCreate(&st, why, sizeof(why));
    if (S->sprite == nullptr) {
        S->why = why;
        return false;
    }
    S->modelFiles = abilityAsset.modelFiles();
    S->name = discTag() + "/sprites/" + name;
    S->npcNote.clear();
    S->npcTag.clear();
    if (e.kind == K_NPC) {
        S->npcNote = npcSheetNote(e, fd, st, &S->npcTag);
        if (drawsNothing(S->sprite)) {
            S->npcNote += std::string(S->npcNote.empty() ? "" : " ") +
                          tr("No frame of this record draws a visible pixel (with any of its palettes): there is nothing to show.");
            S->npcTag += std::string(S->npcTag.empty() ? "" : "\n") + tr("Draws nothing");
        } else if (animationsDrawNothing(S->sprite)) {
            S->npcNote += std::string(S->npcNote.empty() ? "" : " ") +
                          tr("Its animations draw nothing as the field starts them; Frames shows its frames one at a time.");
            S->npcTag += std::string(S->npcTag.empty() ? "" : "\n") + tr("Animations draw nothing");
        }
    }
    S->haveBounds = xgSpriteFrameBounds(S->sprite, &S->bx0, &S->by0, &S->bx1, &S->by1);
    S->anim = 0;
    S->dir = 0;
    S->palette = e.palette < xgSpritePaletteCount(S->sprite) ? e.palette : 0;
    S->frame = 1;
    S->flip = false;
    S->sceneFor = -1;
    // the first animation the viewer runs
    for (int a = 0; a < xgSpriteAnimationCount(S->sprite); a++) {
        if (!(xgSpriteAnimationFlags(S->sprite, a) & XG_SPRITE_ANIM_UNSAFE)) {
            S->anim = a;
            break;
        }
    }
    S->dir = xgSpriteAnimationDirections(S->sprite, S->anim) / 2;
    if (S->view == nullptr) {
        S->view.reset(new View);
    }
    S->view->setKeepInArea(false);
    S->wantZoom = -2;
    S->playing = S->loop = true;
    restart();
    status("%s: %d animations, %d frames, %d palettes (the game's sprite code in a sandbox)", e.title.c_str(),
           xgSpriteAnimationCount(S->sprite), xgSpriteFrameCount(S->sprite), xgSpritePaletteCount(S->sprite));
    return true;
}

// The selection into a fresh world, and its first frame drawn.
void restart() {
    S->started = false;
    S->steps = 0;
    S->clock = 0;
    S->state = XgSpriteState();
    if (S->sprite == nullptr) {
        return;
    }
    if (S->tab == TAB_FRAMES) {
        S->started = xgSpriteShowFrame(S->sprite, S->frame, S->flip, S->palette) != 0;
        S->sceneFor = -1;
    } else {
        int key = (S->anim * 8 + S->dir) * 64 + S->palette;
        if (S->entry.dir == 0x0E && !S->entry.partyFile) {
            S->sceneFor = key;
            S->ax0 = S->ay0 = 0; S->ax1 = 320; S->ay1 = 224;
            S->haveAnimBounds = true;
        } else if (S->sceneFor != key) {
            // What the animation draws in its first seconds (effects fly
            // off): the picture shows it all. A run of its own, then the
            // animation again from its start.
            S->sceneFor = key;
            S->ax0 = S->bx0, S->ay0 = S->by0, S->ax1 = S->bx1, S->ay1 = S->by1;
            S->haveAnimBounds = S->haveBounds;
            if (xgSpritePlay(S->sprite, S->anim, S->dir, S->palette)) {
                for (int k = 0; k < (int)(stepRate() * 4); k++) {
                    xgSpriteStep(S->sprite, 1);
                    int a, b, c, d;
                    if (xgSpritePreviewBounds(S->sprite, &a, &b, &c, &d)) {
                        if (!S->haveAnimBounds) {
                            S->ax0 = a; S->ay0 = b; S->ax1 = c; S->ay1 = d;
                            S->haveAnimBounds = true;
                        } else {
                            S->ax0 = std::min(S->ax0, a), S->ay0 = std::min(S->ay0, b);
                            S->ax1 = std::max(S->ax1, c), S->ay1 = std::max(S->ay1, d);
                        }
                    }
                }
            }
        }
        S->started = xgSpritePlay(S->sprite, S->anim, S->dir, S->palette) != 0;
    }
    if (S->started) {
        xgSpriteStep(S->sprite, 1);
        S->steps = 1;
        xgSpriteGetState(S->sprite, &S->state);
    } else {
        xgVgpuClearPrims(xgSpriteGpu(S->sprite));
    }
}

// Drawn frames a second: the vblank rate over the vblanks a frame lasts (the
// field: 2; the battle: 1, a VM tick each).
double stepRate() {
    if (S->sprite == nullptr) {
        return 30;
    }
    double tick = xgSpriteTickRate(xgSpriteBattle(S->sprite) ? 2 : 1);
    if (!(tick > 0)) {
        tick = 60000.0 / 1001.0;
    }
    return tick / xgSpriteTicksPerStep(S->sprite);
}

void stepOnce() {
    if (!S->started) {
        return;
    }
    if (S->state.ended && (S->state.effects == 0 || S->state.heapFull) && S->loop && S->tab == TAB_ANIMS) {
        restart();
        return;
    }
    xgSpriteStep(S->sprite, 1);
    S->steps++;
    xgSpriteGetState(S->sprite, &S->state);
}

// ---- exports -----------------------------------------------------------------------------------------

// The picture rectangle (screen units) for exports: every frame's bounds, a
// margin of 2 sprite pixels.
void exportRect(int* x0, int* y0, int* w, int* h) {
    int u = xgSpriteUnits(S->sprite);
    int ax = S->bx0, ay = S->by0, bx = S->bx1, by = S->by1;
    if (S->entry.dir == 0x0E && !S->entry.partyFile && S->tab == TAB_ANIMS) {
        ax = ay = 0; bx = 320; by = 224;
    }
    else if (!S->haveBounds) {
        ax = ay = -32 * u;
        bx = by = 32 * u;
    }
    *x0 = (int)floor((double)ax / u) * u - 2 * u;
    *y0 = (int)floor((double)ay / u) * u - 2 * u;
    *w = (int)ceil((double)(bx - *x0) / u) + 2;
    *h = (int)ceil((double)(by - *y0) / u) + 2;
}

uint32_t exportBg() {
    return S->transparent ? 0 : 0xFF281A18u; // the view's background, opaque
}

std::vector<ExportSource> exportSources() {
    const auto& e = S->entry;
    auto refs = std::vector<ExportSource>{exportSource(disc(), e.dir, e.file, "sprite", e.field, e.kind == K_NPC ? 3 : -1,
        e.npc >= 0 ? std::vector<int>{e.npc} : std::vector<int>{})};
    if (e.kind == K_NPC) {
        refs.push_back(exportSource(disc(), 4, e.file + 1, "textures", e.field));
        refs.push_back(exportSource(disc(), 4, e.file, "TIMs", e.field, 0));
        refs.push_back(exportSource(disc(), 4, e.file, "sheets", e.field, 4));
    }
    if (e.kind == K_ENEMY) refs[0] = exportSource(disc(), e.dir, e.file, "enemy sprite and sheet", -1, -1, {e.enemy});
    if (e.dir == 0x0E) {
        refs.push_back(exportSource(disc(), e.dir, e.file + 1, "ability textures"));
        refs.push_back(exportSource(disc(), 0x2C, 1, "common textures"));
        refs.push_back(exportSource(disc(), 0x2C, 2, "common sprites"));
        if (e.partyFile) refs.push_back(exportSource(disc(), 0x2D, e.partyFile, "normal party sprites"));
        else refs.push_back(exportSource(disc(), 0x2D, 1, "preview caster and target"));
        for (auto [dir, file] : S->modelFiles)
            refs.push_back(exportSource(disc(), dir, file, dir == 0x2A ? "spell model clip" : file & 1 ? "spell model" : "spell model animation"));
    }
    return refs;
}
ExportSelection exportSelection() {
    XgSpriteState decoded = {}; xgSpriteGetState(S->sprite, &decoded);
    return {{"kind", kindIds[S->entry.kind]}, {"animation", std::to_string(S->anim)}, {"direction", std::to_string(S->dir)},
        {"palette", std::to_string(S->palette)}, {"ui_frame", std::to_string(S->frame)}, {"decoded_frame", std::to_string(decoded.frame)}, {"flip", S->flip ? "1" : "0"},
        {"tick_rate", std::to_string(stepRate())}};
}

bool writePng(const std::string& rel, const std::vector<uint32_t>& rgba, int w, int h, ExportManifest* batch = nullptr, const char* step = "", ExportSelection details = {}) {
    std::string path = exportPath(rel.c_str());
    if (path.empty() || !xgPngWrite(path.c_str(), rgba.data(), w, h)) {
        error("Cannot write %s: %s", path.c_str(), xgWriterError());
        return false;
    }
    auto selection = exportSelection(); selection.push_back({"sequence_step", step});
    selection.insert(selection.end(), details.begin(), details.end());
    return recordExport(path, "sprites", exportSources(), selection, batch);
}

std::string selectionName() {
    char s[64];
    if (S->tab == TAB_FRAMES) {
        snprintf(s, sizeof(s), "_frame%03d%s_p%d", S->frame, S->flip ? "f" : "", S->palette);
    } else {
        snprintf(s, sizeof(s), "_a%02d_d%d_p%d", S->anim, S->dir, S->palette);
    }
    return S->name + s;
}

void exportFrame() {
    int x0, y0, w, h;
    exportRect(&x0, &y0, &w, &h);
    std::vector<uint32_t> rgba((size_t)w * h);
    xgSpriteRaster(S->sprite, x0, y0, w, h, exportBg(), rgba.data());
    char s[32] = "";
    if (S->tab == TAB_ANIMS) {
        snprintf(s, sizeof(s), "_%04d", S->steps - 1);
    }
    std::string rel = selectionName() + s + ".png";
    if (writePng(rel, rgba, w, h)) {
        status("Exported %s (%d x %d)", exportPath(rel.c_str()).c_str(), w, h);
    }
}

// Every frame held on its own, in a grid of equal cells (each frame at the
// same place in its cell: the sprite's anchor).
void exportSheet() {
    int x0, y0, w, h;
    exportRect(&x0, &y0, &w, &h);
    int n = xgSpriteFrameCount(S->sprite);
    if (n <= 0) {
        error("No frames");
        return;
    }
    int cols = (int)ceil(sqrt((double)n * h / w));
    cols = std::max(1, std::min(cols, n));
    int rows = (n + cols - 1) / cols;
    std::vector<uint32_t> sheet((size_t)cols * w * rows * h, exportBg());
    std::vector<uint32_t> cell((size_t)w * h);
    for (int f = 1; f <= n; f++) {
        xgSpriteShowFrame(S->sprite, f, S->flip, S->palette);
        xgSpriteStep(S->sprite, 1);
        xgSpriteRaster(S->sprite, x0, y0, w, h, exportBg(), cell.data());
        int cx = (f - 1) % cols * w, cy = (f - 1) / cols * h;
        for (int r = 0; r < h; r++) {
            memcpy(&sheet[((size_t)(cy + r) * cols * w) + cx], &cell[(size_t)r * w], (size_t)w * 4);
        }
    }
    char s[64];
    snprintf(s, sizeof(s), "_sheet%s_p%d.png", S->flip ? "f" : "", S->palette);
    std::string rel = S->name + s;
    if (writePng(rel, sheet, cols * w, rows * h, nullptr, "", {{"mode", "sheet"}, {"frame_count", std::to_string(n)},
        {"columns", std::to_string(cols)}, {"rows", std::to_string(rows)}, {"cell_width", std::to_string(w)}, {"cell_height", std::to_string(h)}})) {
        status("Exported %s: %d frames in %d x %d cells of %d x %d", exportPath(rel.c_str()).c_str(), n, cols, rows, w,
               h);
    }
    restart();
}

// The animation from its start, one picture per drawn frame, until it ends
// (and its effect sprites are gone) or comes back to a picture and state it
// had (the VM's place, wait, frame, stack), at most a minute. Two runs: the first finds the extent of what
// it draws (effects too), the second draws it (the same world: the same
// pictures).
void exportSequence() {
    if (S->tab != TAB_ANIMS || !S->started) {
        error("Choose an animation that plays");
        return;
    }
    const int maxSteps = (int)(stepRate() * 60);
    std::vector<uint32_t> keys;
    int count = 0;
    int ax, ay, bx, by;
    int x0, y0, w, h;
    exportRect(&x0, &y0, &w, &h);
    int u = xgSpriteUnits(S->sprite);
    ax = x0, ay = y0, bx = x0 + w * u, by = y0 + h * u;
    xgSpritePlay(S->sprite, S->anim, S->dir, S->palette);
    for (count = 0; count < maxSteps; count++) {
        xgSpriteStep(S->sprite, 1);
        XgSpriteState st;
        xgSpriteGetState(S->sprite, &st);
        int a, b, c, d;
        if (xgSpritePrimBounds(S->sprite, &a, &b, &c, &d)) {
            ax = std::min(ax, a), ay = std::min(ay, b), bx = std::max(bx, c), by = std::max(by, d);
        }
        if (st.ended && st.effects == 0) {
            count++; // the end, its effects gone too
            break;
        }
        if (std::find(keys.begin(), keys.end(), st.key) != keys.end()) {
            break; // this picture repeats an earlier state's: one round done
        }
        keys.push_back(st.key);
    }
    x0 = (int)floor((double)ax / u) * u;
    y0 = (int)floor((double)ay / u) * u;
    w = (int)ceil((double)(bx - x0) / u);
    h = (int)ceil((double)(by - y0) / u);
    std::vector<uint32_t> rgba((size_t)w * h);
    xgSpritePlay(S->sprite, S->anim, S->dir, S->palette);
    std::string dir = selectionName();
    ExportManifest manifest;
    bool ok = true;
    for (int i = 0; i < count && ok; i++) {
        xgSpriteStep(S->sprite, 1);
        xgSpriteRaster(S->sprite, x0, y0, w, h, exportBg(), rgba.data());
        char s[32];
        snprintf(s, sizeof(s), "/%04d.png", i);
        ok = writePng(dir + s, rgba, w, h, &manifest, std::to_string(i).c_str());
    }
    if (ok) ok = writeManifest(manifest, exportPath((dir + "/manifest.json").c_str()));
    if (ok) {
        status("Exported %d pictures (%.2f frames/s, %d x %d) to %s", count, stepRate(), w, h,
               exportPath((dir + "/").c_str()).c_str());
    }
    restart();
}

// ---- the panes ---------------------------------------------------------------------------------------

void hint(const char* fmt, ...) IM_FMTARGS(1);
void hint(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    ImGui::PushStyleColor(ImGuiCol_Text, skin::textHint);
    ImGui::TextWrappedV(tr(fmt), ap); // in the menu's language (ui/l10n.h)
    ImGui::PopStyleColor();
    va_end(ap);
}

void selectDisc(int i) {
    if (i < 0 || i >= (int)S->discs.size()) {
        return;
    }
    S->disc = i;
    for (Category& c : S->cats) {
        c = Category();
    }
    S->npcSources.clear();
    S->npcCatalogueRows.clear();
    S->npcRows.clear();
    S->npcBrowse.clear();
    S->npcRooms = 0;
    S->npcWidth = 0;
    S->highlight = 0;
    S->focusParent.clear();
    closeSource();
}

int addDisc(const char* path) {
    for (int i = 0; i < (int)S->discs.size(); i++) if (S->discs[i].path == path) return i;
    const char* why = nullptr;
    XgDisc* d = xgDiscOpen(path, &why);
    if (d == nullptr) { error("%s: %s", path, why); return -1; }
    S->discs.push_back({d, path, {}});
    return (int)S->discs.size() - 1;
}

// A row of controls that wraps: the next item goes on this line if it fits.
void flow(float width) {
    float right = ImGui::GetItemRectMax().x + ImGui::GetStyle().ItemSpacing.x + width;
    if (right <= ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x) {
        ImGui::SameLine();
    }
}

float buttonWidth(const char* label) {
    return ImGui::CalcTextSize(label, nullptr, true).x + ImGui::GetStyle().FramePadding.x * 2;
}

// The flowing buttons translate their labels (ui/l10n.h; tools/l10n.py extracts them).
bool flowButton(const char* label, bool first = false) {
    label = tr(label);
    if (!first) {
        flow(buttonWidth(label));
    }
    return ImGui::Button(label);
}

bool flowCheckbox(const char* label, bool* v) {
    label = tr(label);
    flow(buttonWidth(label) + ImGui::GetFrameHeight() + ImGui::GetStyle().ItemInnerSpacing.x);
    return ImGui::Checkbox(label, v);
}



const char* const dir8[8] = {N_("up"), N_("up-right"), N_("right"), N_("down-right"), N_("down"), N_("down-left"),
                             N_("left"), N_("up-left")};
const char* const dir4[4] = {N_("up"), N_("right"), N_("down"), N_("left")};

// A facing's name: where the sprite looks on the screen (table d of the
// animation, or a mirrored one).
const char* dirName(int dirs, int d) {
    static char s[48];
    const char* n = dirs == 8 ? tr(dir8[d & 7]) : dirs == 4 ? tr(dir4[d & 3]) : "";
    snprintf(s, sizeof(s), "%s%s", n, xgSpriteDirectionMirrored(dirs, d) ? tr(" (mirrored)") : "");
    return s;
}

// ---- the asset list: arrows browse without entering a second pane ------------------------------------

void setTab(int t) {
    if (S->tab != t) { S->tab = t; restart(); }
}

bool previewEntry(const Entry& e) {
    if (S->haveSource && entryKey(S->entry) == entryKey(e)) return S->sprite != nullptr;
    if (openSource(e)) return true;
    error("%s: %s", e.label.c_str(), S->why.c_str());
    return false;
}

void cycleAnimation(int delta) {
    if (!S->sprite) return;
    int n = S->tab == TAB_ANIMS ? xgSpriteAnimationCount(S->sprite) : xgSpriteFrameCount(S->sprite);
    if (n <= 0) return;
    if (S->tab == TAB_ANIMS) {
        S->anim = (S->anim + delta % n + n) % n;
        S->dir = std::min(S->dir, std::max(0, xgSpriteAnimationDirections(S->sprite, S->anim) - 1));
    } else S->frame = (S->frame - 1 + delta % n + n) % n + 1;
    S->playing = S->loop = true;
    restart();
    if (S->haveSource && viewerShortcuts()) S->focusParent = entryKey(S->entry);
    sound(1);
}

void wrapNpcHeadings() {
    float width = std::max(1.f, ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ScrollbarSize);
    float font = ImGui::GetFontSize();
    if (width == S->npcWidth && font == S->npcFontSize) return;
    S->npcWidth = width; S->npcFontSize = font;
    S->npcRows.clear(); S->npcBrowse.clear();
    for (const auto& row : S->npcCatalogueRows) {
        if (row.source >= 0) {
            S->npcBrowse.push_back((int)S->npcRows.size());
            S->npcRows.push_back(row);
            continue;
        }
        std::string line;
        bool indented = row.heading.compare(0, 2, "  ") == 0;
        size_t start = indented ? 2 : 0;
        while (start < row.heading.size()) {
            size_t end = row.heading.find(' ', start);
            if (end == std::string::npos) end = row.heading.size();
            std::string word = row.heading.substr(start, end - start);
            std::string next = line.empty() ? (indented ? "  " : "") + word : line + " " + word;
            if (!line.empty() && ImGui::CalcTextSize(next.c_str()).x > width) {
                S->npcRows.push_back({line});
                line = (indented ? "  " : "") + word;
            } else line = std::move(next);
            start = end + 1;
        }
        if (!line.empty()) S->npcRows.push_back({line});
    }
}

void sourcesList() {
    if (S->discs.empty()) { hint("No Xenogears disc image was found."); return; }
    for (int k = 0; k < K_COUNT; k++) {
        if (S->section >= 0 && S->section != k) continue;
        Category& c = S->cats[k];
        ImGui::PushID(k);
        if (S->section >= 0) c.expanded = true;
        else if (listItem(tr(kindNames[k]), false)) c.expanded = !c.expanded;
        if (!c.expanded) { ImGui::PopID(); continue; }
        if (!c.listed) {
            if (k == K_NPC) listFields();
            else listCategory(k);
        }
        if (k == K_NPC) {
            // Keep the current room visible even after its scrollable heading
            // has moved above the viewport.
            if (S->haveSource && S->entry.kind == K_NPC) {
                const auto& l = fieldLocation(S->entry.field);
                ImGui::TextWrapped("%s", l.area);
                ImGui::TextWrapped("%s", l.room);
            } else ImGui::TextUnformatted(tr("Field NPCs"));
            ImGui::Separator();
            wrapNpcHeadings();
            int selected = -1;
            std::string current = S->haveSource ? entryKey(S->entry) : "";
            for (int i = 0; i < (int)S->npcBrowse.size(); ++i) {
                const auto& row = S->npcRows[S->npcBrowse[i]];
                if (current == entryKey(S->npcSources[row.source]) ||
                    (S->haveSource && sameNpcRow(S->entry, S->npcSources[row.source]))) selected = i;
            }
            if (!S->npcBrowse.empty() && (S->characterStep || !S->haveSource)) {
                int n = (int)S->npcBrowse.size(), delta = S->characterStep ? S->characterStep : 1;
                int at = selected < 0 ? (delta > 0 ? -1 : 0) : selected;
                at = (at + delta % n + n) % n;
                Entry e = S->npcSources[S->npcRows[S->npcBrowse[at]].source];
                previewEntry(e);
                S->focusParent = entryKey(e);
                selected = at;
                S->characterStep = 0;
            }
            int focusRow = -1;
            for (int row : S->npcBrowse) {
                const Entry& e = S->npcSources[S->npcRows[row].source];
                if (S->focusParent == entryKey(e) || ((S->navReset || S->scrollTo || S->focusParent == current) &&
                    (current == entryKey(e) || (S->haveSource && sameNpcRow(S->entry, e))))) focusRow = row;
            }
            const bool restoringFocus = focusRow >= 0;
            ImGui::BeginChild("##entries", ImVec2(0, 0), ImGuiChildFlags_NavFlattened);
            int preview = -1;
            ImGuiListClipper clip;
            clip.Begin((int)S->npcRows.size(), ImGui::GetTextLineHeightWithSpacing());
            if (focusRow >= 0) clip.IncludeItemsByIndex(std::max(0, focusRow - 8), focusRow + 1);
            // Navigation must see past both non-interactive heading lines
            // when the next room begins just outside the clipped viewport.
            if (selected >= 0) {
                int row = S->npcBrowse[selected];
                clip.IncludeItemsByIndex(std::max(0, row - 8), std::min((int)S->npcRows.size(), row + 9));
            }
            while (clip.Step()) for (int i = clip.DisplayStart; i < clip.DisplayEnd; ++i) {
                const auto& row = S->npcRows[i];
                if (row.source < 0) {
                    ImGui::TextColored(row.heading.compare(0, 2, "  ") == 0 ? skin::textHint : skin::textWarning,
                        "%s", row.heading.c_str());
                    continue;
                }
                const Entry& e = S->npcSources[row.source];
                std::string key = entryKey(e);
                ImGui::PushID(key.c_str());
                // Records without their own art say so, dimmed (xg-htn.35.68).
                const char* odd = e.sheet == NpcSheet::PLACEHOLDER ? tr("  - placeholder") : e.blank ? tr("  - blank") : "";
                std::string label = "  " + e.label + odd + "###row";
                bool confirmed = listItem(label.c_str(), S->haveSource && (key == entryKey(S->entry) || sameNpcRow(S->entry, e)),
                                          *odd != '\0');
                if (libraryHighlight(confirmed, S->highlight) && (!restoringFocus || i == focusRow)) preview = row.source;
                if (confirmed) S->focusRequested = true;
                if (i == focusRow && !ImGui::IsPopupOpen((const char*)nullptr, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel)) {
                    ImGui::SetKeyboardFocusHere(-1);
                    ImGui::SetScrollHereY(.3f);
                    S->focusParent.clear(); S->navReset = S->scrollTo = false;
                    S->highlight = ImGui::GetItemID();
                }
                ImGui::PopID();
            }
            if (S->npcRows.empty()) hint("None available.");
            ImGui::EndChild();
            if (preview >= 0) { Entry e = S->npcSources[preview]; previewEntry(e); }
            ImGui::PopID();
            continue;
        }
        int selected = -1;
        for (int i = 0; i < (int)c.entries.size(); ++i)
            if (S->haveSource && entryKey(c.entries[i]) == entryKey(S->entry)) selected = i;
        if (!c.entries.empty() && (S->characterStep || !S->haveSource)) {
            int n = (int)c.entries.size(), delta = S->characterStep ? S->characterStep : 1;
            int at = selected < 0 ? (delta > 0 ? -1 : 0) : selected;
            at = (at + delta % n + n) % n;
            // Copy the entry: loading can change the active disc, not the catalogue.
            Entry e = c.entries[at];
            previewEntry(e);
            S->focusParent = entryKey(e);
            selected = at;
            S->characterStep = 0;
        }
        int focusRow = -1;
        for (int i = 0; i < (int)c.entries.size(); ++i)
            if (S->focusParent == entryKey(c.entries[i]) || ((S->navReset || S->scrollTo) && i == selected)) focusRow = i;
        const bool restoringFocus = focusRow >= 0;
        ImGui::BeginChild("##entries", ImVec2(0, 0), ImGuiChildFlags_NavFlattened);
        int preview = -1;
        ImGuiListClipper clip;
        clip.Begin((int)c.entries.size());
        if (focusRow >= 0) clip.IncludeItemByIndex(focusRow);
        while (clip.Step()) for (int i = clip.DisplayStart; i < clip.DisplayEnd; ++i) {
            const Entry& e = c.entries[i];
            ImGui::PushID(entryKey(e).c_str());
            std::string label = e.label + (e.same.empty() ? "" : " = " + e.same) + "###row";
            bool confirmed = listItem(label.c_str(), selected == i, !e.same.empty());
            // A source jump restores its row this frame. The previous ImGui
            // cursor can still be visible during submission; it must not
            // replace that exact source before focus has reached the new row.
            if (libraryHighlight(confirmed, S->highlight) && (!restoringFocus || i == focusRow)) preview = i;
            if (confirmed) S->focusRequested = true;
            if (i == focusRow && !ImGui::IsPopupOpen((const char*)nullptr, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel)) {
                ImGui::SetKeyboardFocusHere(-1);
                ImGui::SetScrollHereY(.3f);
                S->focusParent.clear(); S->navReset = S->scrollTo = false;
                S->highlight = ImGui::GetItemID();
            }
            ImGui::PopID();
        }
        if (c.entries.empty()) hint("None available.");
        ImGui::EndChild();
        if (preview >= 0) { Entry e = c.entries[preview]; previewEntry(e); }
        ImGui::PopID();
    }
}

void drawPicture();

void leftPane(ImVec2 min, ImVec2 max) {
    if (beginPane("##sources", min, max, nullptr, true)) sourcesList();
    endPane();
}

// ---- the right pane: the picture and its controls ------------------------------------------------------

void controls() {
    XgSprite* sp = S->sprite;
    const bool anim = S->tab == TAB_ANIMS;
    if (anim) {
        if (flowButton(S->playing ? "Pause" : "Play", true)) S->playing = !S->playing;
        if (flowButton("Step")) { S->playing = false; stepOnce(); }
        if (flowButton("Restart")) restart();
        flowCheckbox("Loop", &S->loop);
    } else {
        if (flowButton("Previous", true)) {
            S->frame = S->frame > 1 ? S->frame - 1 : xgSpriteFrameCount(sp); restart();
        }
        if (flowButton("Next")) {
            S->frame = S->frame < xgSpriteFrameCount(sp) ? S->frame + 1 : 1; restart();
        }
    }
    if (flowButton("Fit")) S->wantZoom = 0;
    {
        ImGui::Text(tr("%s disc %d, %02X/%03X"), xgDiscRegion(disc()), xgDiscNumber(disc()), S->entry.dir, S->entry.file);
        ImGui::TextWrapped(tr("%d animations / %d frames / %.3f ticks/s"), xgSpriteAnimationCount(sp), xgSpriteFrameCount(sp), stepRate());
        if (anim) {
            ImGui::SetNextItemWidth(100 * unit());
            ImGui::SliderFloat(tr("Speed"), &S->speed, .125f, 4.f, "x%.3g", ImGuiSliderFlags_Logarithmic);
            int dirs = xgSpriteAnimationDirections(sp, S->anim);
            if (dirs > 1 && ImGui::SliderInt(tr("Facing"), &S->dir, 0, dirs - 1, dirName(dirs, S->dir))) restart();
            int alias = xgSpriteAnimationAlias(sp, S->anim);
            if (alias >= 0) ImGui::Text(tr("Shares retail animation %d"), alias);
            int flags = xgSpriteAnimationFlags(sp, S->anim);
            if (flags & XG_SPRITE_ANIM_UNSAFE) ImGui::TextUnformatted(tr("Unsafe bytecode; use Frames to inspect poses."));
            if (flags & XG_SPRITE_ANIM_SKIPS) ImGui::TextUnformatted(tr("Battle audio, hit logic and scene commands omitted."));
            ImGui::Text(tr("Frame %d / step %d / %d effects%s"), S->state.frame, S->state.step, S->state.effects,
                        S->state.heapFull ? tr(" / heap full") : "");
        } else if (ImGui::Checkbox(tr("Mirror"), &S->flip)) restart();
        int palettes = xgSpritePaletteCount(sp);
        if (palettes > 1 && ImGui::SliderInt(tr("Palette"), &S->palette, 0, palettes - 1)) restart();
        ImGui::Checkbox(tr("Clear background"), &S->transparent);
        ImGui::SameLine(); ImGui::Checkbox(tr("Edges"), &S->edges);
        for (int z = 1; z <= 4; z *= 2) {
            if (z > 1) ImGui::SameLine();
            char label[16]; snprintf(label, sizeof(label), "x%d", z);
            if (ImGui::Button(label)) S->wantZoom = (float)z;
        }
        if (ImGui::Button(tr("PNG frame"))) exportFrame();
        ImGui::SameLine(); if (ImGui::Button(tr("PNG sheet"))) exportSheet();
        if (anim && ImGui::Button(tr("PNG sequence"))) exportSequence();
    }
}

void spriteOptions() {
    bool options = libraryOptions("spriteoptions", S->optionsRequested, 340, 300);
    if (!options && S->optionsOpen && S->haveSource) {
        S->focusParent = entryKey(S->entry);
        ImGui::SetNavCursorVisibleAfterMove();
    }
    S->optionsOpen = options;
    if (options) {
        if (ImGui::Button(tr("Close"))) ImGui::CloseCurrentPopup();
        if (S->sprite) {
            if (ImGui::Button(tr("Bookmark"))) bookmarkSelection();
            ImGui::TextWrapped("%s", S->entry.title.c_str());
            if (S->entry.kind == K_NPC && S->entry.npcGroup >= 0) {
                const auto& locations = S->cats[K_NPC].entries[S->entry.npcGroup].locations;
                ImGui::Text(tr("Source: field %03d, NPC %02d"), S->entry.field, S->entry.npc);
                if (!S->npcNote.empty()) hint("%s", S->npcNote.c_str());
                ImGui::Text(locations.size() == 1 ? tr("%zu field location") : tr("%zu field locations"), locations.size());
                ImGui::PushStyleColor(ImGuiCol_PopupBg, IM_COL32(24, 26, 40, 255));
                if (ImGui::BeginCombo(tr("Location"), S->entry.label.c_str())) {
                    for (int source : locations) {
                        Entry e = S->npcSources[source];
                        ImGui::PushID(source);
                        bool selected = S->entry.disc == e.disc && S->entry.field == e.field && S->entry.npc == e.npc;
                        char sourceLabel[64];
                        snprintf(sourceLabel, sizeof(sourceLabel), " (field %03d, NPC %02d)", e.field, e.npc);
                        std::string label = e.label + sourceLabel;
                        if (ImGui::Selectable(label.c_str(), selected)) openSource(e);
                        if (selected) ImGui::SetItemDefaultFocus();
                        ImGui::PopID();
                    }
                    ImGui::EndCombo();
                }
                ImGui::PopStyleColor();
            }
            if (ImGui::RadioButton(tr("Animations"), S->tab == TAB_ANIMS)) setTab(TAB_ANIMS);
            ImGui::SameLine();
            if (ImGui::RadioButton(tr("Frames"), S->tab == TAB_FRAMES)) setTab(TAB_FRAMES);
            if (ImGui::ArrowButton("##prev", ImGuiDir_Left)) cycleAnimation(-1);
            ImGui::SameLine();
            ImGui::Text("%s %02d/%02d", S->tab == TAB_ANIMS ? tr("Animation") : tr("Frame"),
                        S->tab == TAB_ANIMS ? S->anim + 1 : S->frame,
                        S->tab == TAB_ANIMS ? xgSpriteAnimationCount(S->sprite) : xgSpriteFrameCount(S->sprite));
            ImGui::SameLine();
            if (ImGui::ArrowButton("##next", ImGuiDir_Right)) cycleAnimation(1);
            controls();
            lookOptions(spriteLibraries, lookCovered(S->view.get(), spriteLibraries), true, N_("No sprite pack installed"));
        }
        ImGui::EndPopup();
    }
}

void rightPane(ImVec2 min, ImVec2 max) {
    if (!S->haveSource || S->sprite == nullptr) {
        if (beginPane("##right", min, max, "Sprites", true)) {
            if (S->haveSource) {
                ImGui::TextColored(skin::textError, "%s", S->why.c_str());
            }
            hint("Up/Down or L1/R1 changes the character; Left/Right changes the animation. Confirm or Start opens Options. Animations use the game's sprite code and bytecode, "
                 "at the game's rate; each frame can be held on its own, and all can be exported as PNG.");
            hint("Field sprites are made as the field makes them: an NPC's sheet and palette where the field puts "
                 "them in VRAM, the party's pieces sent to VRAM each frame.");
        }
        endPane();
        spriteOptions();
        return;
    }
    XgSprite* sp = S->sprite;
    ImVec2 pictureMax(max.x, max.y - 56 * unit());
    if (beginPane("##right", min, pictureMax, nullptr, false)) drawPicture();
    endPane();
    if (beginPane("##sprite-details", ImVec2(min.x, pictureMax.y + 12 * unit()), max, nullptr, true)) {
        ImGui::TextWrapped("%s", S->entry.title.c_str());
        ImGui::Text("%s %02d/%02d", S->tab == TAB_ANIMS ? tr("Animation") : tr("Frame"),
                    S->tab == TAB_ANIMS ? S->anim + 1 : S->frame,
                    S->tab == TAB_ANIMS ? xgSpriteAnimationCount(sp) : xgSpriteFrameCount(sp));
        if (!S->npcNote.empty()) hint("%s", S->npcNote.c_str()); // the tag's full words (scroll)
    }
    endPane();
    spriteOptions();
}

// The picture in the current pane: the sprite's view across its space, fitted
// (in whole game pixels) when the extent changes. Enhanced: the installed packs'
// cels (and the actor's pose in the Enhanced engine); Compare: stock | enhanced.
void drawPicture() {
    XgSprite* sp = S->sprite;
    {
        float u = unit();
        ImVec2 avail = ImGui::GetContentRegionAvail();
        ImVec2 viewSize = avail;
        View* v = S->view.get();
        v->show(xgSpriteGpu(sp));
        XgSpriteState state;
        xgSpriteGetState(sp, &state);
        // one actor's parts alone (no effects): the pose look-up has them all
        v->setReplacements(lookLibraries(spriteLibraries), state.effects == 0 ? state.subSprites : 0);
        bool compare = look(spriteLibraries) == LOOK_COMPARE && lookInstalled(spriteLibraries);
        if (compare) viewSize.x = floorf((avail.x - 4 * u) / 2);
        v->setWireframe(S->edges);
        v->setBackground(IM_COL32(0, 0, 0, 255));
        int units = xgSpriteUnits(sp);
        bool anim = S->tab == TAB_ANIMS && S->sceneFor >= 0;
        if (S->haveBounds || (anim && S->haveAnimBounds)) {
            float m = 8.0f * units;
            float x0 = anim ? S->ax0 : S->bx0, y0 = anim ? S->ay0 : S->by0;
            float x1 = anim ? S->ax1 : S->bx1, y1 = anim ? S->ay1 : S->by1;
            if (x0 != S->sceneShown[0] || y0 != S->sceneShown[1] || x1 != S->sceneShown[2] || y1 != S->sceneShown[3]) {
                S->sceneShown[0] = x0, S->sceneShown[1] = y0, S->sceneShown[2] = x1, S->sceneShown[3] = y1;
                if (S->wantZoom == -1) {
                    S->wantZoom = -2; // a new extent: fit it again, in whole game pixels
                }
            }
            v->setScene(x0 - m, y0 - m, x1 + m, y1 + m);
        }
        bool snap = S->wantZoom == -3;
        if (S->wantZoom == -2) {
            v->fit(); // then (next frame) down to a whole multiple of the game's pixel
            S->wantZoom = -3;
        } else if (S->wantZoom >= 0) {
            if (S->wantZoom == 0) {
                v->fit();
            } else {
                v->setZoom(S->wantZoom * u / units);
            }
            S->wantZoom = -1;
        }
        if (compare) {
            if (!S->stockView) S->stockView.reset(new View);
            View* c = S->stockView.get();
            c->show(xgSpriteGpu(sp));
            c->setWireframe(S->edges);
            c->setBackground(IM_COL32(0, 0, 0, 255));
            c->setInteractive(false);
            c->setFitOnChange(false);
            c->setKeepInArea(false);
            if (S->haveBounds || (anim && S->haveAnimBounds)) {
                float m = 8.0f * units;
                c->setScene(S->sceneShown[0] - m, S->sceneShown[1] - m, S->sceneShown[2] + m, S->sceneShown[3] + m);
            }
            c->fit();
            c->draw(viewSize); // fitted, then at the enhanced view's zoom below
            c->setZoom(v->zoom());
            lookLabel(N_("Stock"));
            ImGui::SameLine(0, 4 * u);
        }
        v->draw(viewSize);
        if (compare) lookLabel(N_("Enhanced"));
        if (S->entry.kind == K_NPC) libraryTag(S->npcTag.c_str());
        if (snap) {
            float k = floorf(v->zoom() * units / u);
            if (k >= 1) {
                v->setZoom(k * u / units);
            }
            S->wantZoom = -1;
        }
    }
}

// ---- the focused view: the source alone across the area, its controls in a bar ---------------------

void setFocused(bool on) {
    if (on == S->focused || (on && S->sprite == nullptr)) return;
    S->focused = on;
    S->wantZoom = -2; // the new space: fitted again, in whole game pixels
    if (on) {
        S->focusKey = entryKey(S->entry);
        focusEnter();
        return;
    }
    // Back on the list: the row it opened on keeps its place (listItem puts
    // the cursor back); a source changed meanwhile is scrolled to.
    if (S->haveSource && entryKey(S->entry) != S->focusKey) S->focusParent = entryKey(S->entry);
    else S->focusParent.clear();
    focusLeave();
}

// Up/Down (L1/R1) while focused: the list's next source, as sourcesList steps
// it (the list itself is not drawn).
void stepFocused() {
    int delta = S->characterStep;
    if (delta == 0) return;
    S->characterStep = 0;
    for (int k = 0; k < K_COUNT; k++) {
        if (S->section >= 0 ? S->section != k : !S->cats[k].expanded) continue;
        std::vector<const Entry*> list;
        if (k == K_NPC) {
            for (const NpcRow& row : S->npcCatalogueRows) if (row.source >= 0) list.push_back(&S->npcSources[row.source]);
        } else {
            for (const Entry& e : S->cats[k].entries) list.push_back(&e);
        }
        if (list.empty()) continue;
        int n = (int)list.size(), at = -1;
        std::string current = S->haveSource ? entryKey(S->entry) : "";
        for (int i = 0; i < n; ++i)
            if (current == entryKey(*list[i]) || (k == K_NPC && S->haveSource && sameNpcRow(S->entry, *list[i]))) at = i;
        if (at < 0) at = delta > 0 ? -1 : 0;
        Entry e = *list[(size_t)((at + delta % n + n) % n)];
        previewEntry(e);
        S->focusParent = entryKey(e);
        sound(1);
        return;
    }
}

void focusedView(const Area& a) {
    stepFocused();
    XgSprite* sp = S->sprite;
    if (sp == nullptr) { setFocused(false); return; }
    const bool anim = S->tab == TAB_ANIMS;
    float u = a.unit;
    FocusLayout f = focusLayout(a, a.size().x / u >= 500 ? 1 : 2);
    if (beginPane("##sprite-focus", f.min, f.max, nullptr, false)) {
        drawPicture();
        char caption[320];
        snprintf(caption, sizeof(caption), "%s\n%s %02d/%02d", S->entry.title.c_str(),
                 anim ? tr("Animation") : tr("Frame"), anim ? S->anim + 1 : S->frame,
                 anim ? xgSpriteAnimationCount(sp) : xgSpriteFrameCount(sp));
        focusPicture(caption);
    }
    endPane();
    if (beginFocusBar(f)) setFocused(false);
    if (focusButton("<")) cycleAnimation(-1);
    focusText("%s %02d/%02d", anim ? tr("Animation") : tr("Frame"), anim ? S->anim + 1 : S->frame,
              anim ? xgSpriteAnimationCount(sp) : xgSpriteFrameCount(sp));
    if (focusButton(">")) cycleAnimation(1);
    if (anim) {
        if (focusButton(S->playing ? "Pause" : "Play")) S->playing = !S->playing;
        if (focusButton("Step")) { S->playing = false; stepOnce(); }
        int dirs = xgSpriteAnimationDirections(sp, S->anim);
        if (dirs > 1 && focusButton("Turn")) { S->dir = (S->dir + 1) % dirs; restart(); }
    }
    int palettes = xgSpritePaletteCount(sp);
    if (palettes > 1 && focusButton("Palette")) { S->palette = (S->palette + 1) % palettes; restart(); }
    focusLook(spriteLibraries, lookCovered(S->view.get(), spriteLibraries), N_("No sprite pack installed"));
    switch (focusZoomButtons()) {
    case 1: S->wantZoom = -2; break;
    case 2: if (S->view) S->view->setZoom(S->view->zoom() * 0.8f); break;
    case 3: if (S->view) S->view->setZoom(S->view->zoom() * 1.25f); break;
    case 4: S->wantZoom = 1 / u; break; // one sprite pixel per window pixel
    default: break;
    }
    if (focusButton("Options")) S->optionsRequested = true;
    endFocusBar();
    spriteOptions();
}

// ---- the viewer ----------------------------------------------------------------------------------------

double now() {
    if (getenv("XG_EXTRAS_SCRIPT") != nullptr) {
        return frameNumber() / 60.0; // scripted runs: time from the frame count
    }
    return SDL_GetTicksNS() / 1e9;
}

void tick() {
    double t = now();
    double dt = S->lastTime < 0 ? 0 : t - S->lastTime;
    S->lastTime = t;
    if (S->sprite == nullptr || !S->started || !S->playing || S->tab != TAB_ANIMS) {
        S->clock = 0;
        return;
    }
    S->clock += std::min(dt, 0.25) * stepRate() * S->speed;
    while (S->clock >= 1) {
        S->clock -= 1;
        stepOnce();
    }
}

void opened() {
    S = new State;
    for (int i = 0; i < 2; i++) {
        const char* p = cdSwapPath(i);
        if (p == nullptr || p[0] == '\0') {
            continue;
        }
        bool dup = false;
        for (const Disc& d : S->discs) {
            dup = dup || d.path == p;
        }
        const char* why = nullptr;
        XgDisc* x = dup ? nullptr : xgDiscOpen(p, &why);
        if (x != nullptr) {
            Disc d;
            d.disc = x;
            d.path = p;
            S->discs.push_back(d);
        }
    }
    std::sort(S->discs.begin(), S->discs.end(),
              [](const Disc& a, const Disc& b) { return xgDiscNumber(a.disc) < xgDiscNumber(b.disc); });
    if (S->discs.empty()) {
        error("No Xenogears disc image found");
    }
}

void closeState() {
    closeSource();
    for (Disc& d : S->discs) {
        xgDiscClose(d.disc);
    }
    delete S; // its view goes with it
    S = nullptr;
}

void closed() { slots.close(S, closeState); }

void draw(const Area& a) {
    if (viewerShortcuts() && S->sprite && S->started && S->tab == TAB_ANIMS) {
        float x = ImGui::GetKeyData(ImGuiKey_GamepadRStickRight)->AnalogValue - ImGui::GetKeyData(ImGuiKey_GamepadRStickLeft)->AnalogValue;
        S->turn = fabsf(x) > .2f ? S->turn + x * std::min(.1f, ImGui::GetIO().DeltaTime) * 6 : 0;
        int delta = (int)S->turn;
        if (delta) {
            S->turn -= delta;
            int dirs = xgSpriteAnimationDirections(S->sprite, S->anim);
            if (dirs > 1) {
                S->dir = (S->dir + delta + dirs) % dirs;
                xgSpriteFacing(S->sprite, S->anim, S->dir);
            }
        }
    }
    tick();
    if (S->focused) {
        focusedView(a);
        return;
    }
    float u = a.unit;
    float split = floorf(fminf(160 * u, (a.max.x - a.min.x) * 0.32f) / u) * u;
    leftPane(a.min, ImVec2(a.min.x + split, a.max.y));
    rightPane(ImVec2(a.min.x + split + 16 * u, a.min.y), a.max);
    // Confirm on a row: that source in the focused view, once it is loaded.
    if (S->focusRequested) {
        S->focusRequested = false;
        if (S->haveSource && S->sprite) setFocused(true);
    }
}

bool back() {
    if (!S->focused) return false;
    setFocused(false);
    return true;
}

// Every animation of a source, each facing, for `steps` drawn frames (the
// sweep command: a robustness and sanity run over a whole disc).
void sweepSource(const Entry& e, int steps, int* anims, int* skipped) {
    if (!openSource(e)) {
        fprintf(stderr, "extras: sprites: sweep: %s: cannot open: %s\n", e.label.c_str(), S->why.c_str());
        return;
    }
    XgSprite* sp = S->sprite;
    for (int a = 0; a < xgSpriteAnimationCount(sp); a++) {
        if (xgSpriteAnimationFlags(sp, a) & XG_SPRITE_ANIM_UNSAFE) {
            (*skipped)++;
            continue;
        }
        int dirs = xgSpriteAnimationDirections(sp, a);
        for (int d = 0; d < dirs; d++) {
            fprintf(stderr, "extras: sprites: sweep: %s field %d npc %d anim %d dir %d\n", e.label.c_str(), e.field,
                    e.npc, a, d);
            xgSpritePlay(sp, a, d, 0);
            int frames = 0, maxEffects = 0, ended = -1, last = -1;
            for (int k = 0; k < steps; k++) {
                xgSpriteStep(sp, 1);
                XgSpriteState st;
                xgSpriteGetState(sp, &st);
                frames += st.frame != last;
                last = st.frame;
                if (st.heapFull) {
                    fprintf(stderr, "extras: sprites: sweep:   failed: sandbox heap exhausted at tick %d\n", k);
                    (*skipped)++;
                    break;
                }
                maxEffects = std::max(maxEffects, st.effects);
                if (st.ended && ended < 0) {
                    ended = k;
                }
            }
            fprintf(stderr, "extras: sprites: sweep:   ok: %d frame changes, ended at %d, %d effects at most\n", frames,
                    ended, maxEffects);
            (*anims)++;
        }
    }
}

void sweep(const char* arg) {
    char kind[16] = "all";
    int from = 0, to = FIELD_COUNT - 1, steps = 90;
    sscanf(arg, "%15s %d %d %d", kind, &from, &to, &steps);
    int anims = 0, skipped = 0, sources = 0;
    for (int k = 0; k < K_COUNT; k++) {
        if (strcmp(kind, "all") != 0 && strcmp(kind, kindIds[k]) != 0) {
            continue;
        }
        if (k == K_NPC) {
            for (int f = from; f <= to; f++) {
                std::vector<Entry> list = listField(f);
                for (const Entry& e : list) {
                    sweepSource(e, steps, &anims, &skipped);
                    sources++;
                }
            }
            continue;
        }
        listCategory(k);
        std::vector<Entry> list = S->cats[k].entries;
        for (const Entry& e : list) {
            sweepSource(e, steps, &anims, &skipped);
            sources++;
        }
    }
    fprintf(stderr, "extras: sprites: sweep done: %d sources, %d animation facings run, %d animations not run\n",
            sources, anims, skipped);
    closeSource();
}

bool findAndChoose(int kind, int n, int npc) {
    // The stable ability id selects its 0E file; both category aliases use
    // this path so bookmarks and scripts cannot confuse it with a 2D file.
    if ((kind == K_SPECIAL || kind == K_EFFECTS) && npc == 0x0E) {
        Category& category = S->cats[kind];
        if (!category.listed) listCategory(kind);
        for (const Entry& e : category.entries)
            if (e.dir == 0x0E && (e.file - 0x22) / 2 == n) {
                category.expanded = true;
                return openSource(e);
            }
        return false;
    }
    if (kind == K_NPC) {
        Category& c = S->cats[K_NPC];
        if (!c.listed) listFields();
        for (const Entry& e : S->npcSources) {
            if (e.field == n && e.npc == npc) {
                c.expanded = true;
                return openSource(e);
            }
        }
        S->fieldWhy = "no such field/NPC on the configured disc images";
        return false;
    }
    Category& c = S->cats[kind];
    if (!c.listed) {
        listCategory(kind);
    }
    c.expanded = true;
    for (const Entry& e : c.entries) {
        // party/gear: the character or Gear number; battle kinds: the file; effects: any
        bool match = (kind == K_EFFECTS && e.dir == 0x2C && (n == 0 || e.file == n)) || (kind == K_PARTY && e.file == n + 5) || (kind == K_WORLD && e.file == n + 2) || (kind == K_FIELD2D && e.file == n + 0x77A) || (kind == K_GEAR && e.file == n + 0x15) ||
                     ((kind == K_BATTLE || kind == K_SPECIAL) && e.dir == 0x2D && e.file == n) ||
                     (kind == K_ENEMY && (e.file - 3) / 2 == n && e.enemy == npc);
        if (match) {
            return openSource(e);
        }
    }
    return false;
}

bool command(const char* cmd, const char* arg) {
    if (!strcmp(cmd, "locations")) {
        if (!S->cats[K_NPC].listed) listFields();
        FILE* f = fopen(arg, "wb");
        if (!f) return false;
        for (const NpcRow& row : S->npcRows) if (row.source >= 0) {
            const Entry& e = S->npcSources[row.source];
            const auto& l = fieldLocation(e.field);
            fprintf(f, "%s\t%s\t%d\t%d\t%d\t%d\n", l.area, l.room, e.npcGroup,
                xgDiscNumber(S->discs[e.disc].disc), e.field, e.npc);
        }
        fclose(f);
        return true;
    }
    if (!strcmp(cmd, "sheets")) {
        if (!S->cats[K_NPC].listed) listFields();
        FILE* f = fopen(arg, "wb");
        if (!f) return false;
        static const char* const kinds[] = {"own", "texture", "none", "placeholder"};
        for (const Entry& e : S->npcSources)
            fprintf(f, "%d\t%d\t%d\t%s\t%d\n", xgDiscNumber(S->discs[e.disc].disc), e.field, e.npc,
                    e.sheet >= 0 && e.sheet < 4 ? kinds[e.sheet] : "?", e.blank);
        fclose(f);
        return true;
    }
    if (!strcmp(cmd, "notes")) { // every NPC source opened: its tags (the library's words), validation
        if (!S->cats[K_NPC].listed) listFields();
        FILE* f = fopen(arg, "wb");
        if (!f) return false;
        for (const Entry& e : S->npcSources) {
            Entry copy = e;
            bool ok = openSource(copy);
            std::string tag = S->npcTag;
            for (char& c : tag) if (c == '\n') c = '|';
            fprintf(f, "%d\t%d\t%d\t%s\n", xgDiscNumber(S->discs[e.disc].disc), e.field, e.npc, ok ? tag.c_str() : "(no sprite)");
        }
        fclose(f);
        return true;
    }
    if (!strcmp(cmd, "catalogue")) {
        if (!S->cats[K_NPC].listed) listFields();
        FILE* f = fopen(arg, "wb");
        if (!f) return false;
        for (const Entry& e : S->npcSources)
            fprintf(f, "%d\t%d\t%d\t%d\n", e.npcGroup, xgDiscNumber(S->discs[e.disc].disc), e.field, e.npc);
        fclose(f);
        return true;
    }
    if (!strcmp(cmd, "section")) {
        for (int k = 0; k < K_COUNT; ++k) if (!strcmp(arg, kindIds[k])) {
            if (slots.select(arg, S, opened)) S->navReset = true;
            S->section = k; return true;
        }
        return false;
    }
    if (!strcmp(cmd, "character")) { S->characterStep = atoi(arg); return true; }
    if (!strcmp(cmd, "cycle")) { cycleAnimation(atoi(arg)); return true; }
    if (!strcmp(cmd, "options")) { S->optionsRequested = true; return true; }
    if (!strcmp(cmd, "focus")) { setFocused(strcmp(arg, "0") != 0); return S->focused == (strcmp(arg, "0") != 0); }
    if (!strcmp(cmd, "toggle")) { if (S->sprite) S->playing = !S->playing; S->clock = 0; return true; }
    if (!strcmp(cmd, "fit")) { S->wantZoom = 0; return true; }
    if (strcmp(cmd, "image") == 0) {
        int i = addDisc(arg);
        if (i < 0) return false;
        selectDisc(i);
        S->exactDisc = true;
        return true;
    }
    if (strcmp(cmd, "disc") == 0) {
        int n = atoi(arg);
        for (int i = 0; i < (int)S->discs.size(); i++) {
            if (xgDiscNumber(S->discs[(size_t)i].disc) == n) {
                S->exactDisc = true;
                selectDisc(i);
                return true;
            }
        }
        return false;
    }
    if (strcmp(cmd, "seed") == 0) {
        fprintf(stderr, "extras: sprites: the game's rand seed: %08x\n", xgSpriteGameRandSeed());
        return true;
    }
    if (strcmp(cmd, "sweep") == 0) {
        sweep(arg);
        return true;
    }
    if (strcmp(cmd, "source") == 0) {
        S->focusParent.clear();
        S->navReset = S->scrollTo = true;
        char kind[16] = "";
        int n = 0, i = 0;
        if (sscanf(arg, "%15s %d %d", kind, &n, &i) < 1) {
            return false;
        }
        if (!strcmp(kind, "ability")) {
            if (!findAndChoose(K_EFFECTS, n, 0x0E)) error("Ability %d is unavailable", n);
            return true;
        }
        for (int k = 0; k < K_COUNT; k++) {
            if (strcmp(kind, kindIds[k]) == 0) {
                bool ok = findAndChoose(k, n, i);
                if (!ok) {
                    fprintf(stderr, "extras: sprites: no %s %d %d: %s%s\n", kind, n, i, S->why.c_str(),
                            k == K_NPC ? S->fieldWhy.c_str() : "");
                }
                return ok;
            }
        }
        return false;
    }
    if (S->sprite == nullptr) {
        return false;
    }
    if (strcmp(cmd, "tab") == 0) {
        setTab(strcmp(arg, "frames") == 0 ? TAB_FRAMES : TAB_ANIMS);
        return true;
    }
    if (strcmp(cmd, "anim") == 0) {
        S->anim = atoi(arg);
        restart();
        return true;
    }
    if (strcmp(cmd, "dir") == 0) {
        S->dir = atoi(arg);
        restart();
        return true;
    }
    if (strcmp(cmd, "palette") == 0) {
        S->palette = atoi(arg);
        restart();
        return true;
    }
    if (strcmp(cmd, "frame") == 0) {
        S->frame = atoi(arg);
        restart();
        return true;
    }
    if (strcmp(cmd, "flip") == 0) {
        S->flip = atoi(arg) != 0;
        restart();
        return true;
    }
    if (strcmp(cmd, "play") == 0) {
        S->playing = true;
        return true;
    }
    if (strcmp(cmd, "pause") == 0) {
        S->playing = false;
        return true;
    }
    if (strcmp(cmd, "step") == 0) {
        int n = arg[0] != '\0' ? atoi(arg) : 1;
        S->playing = false;
        for (int k = 0; k < n; k++) {
            stepOnce();
        }
        return true;
    }
    if (strcmp(cmd, "trace") == 0) {
        // n drawn frames, the frame each shows (validation of the timing)
        S->playing = false;
        int n = atoi(arg);
        fprintf(stderr, "extras: sprites: trace:");
        for (int k = 0; k < n; k++) {
            fprintf(stderr, " %d", S->state.frame);
            stepOnce();
        }
        fprintf(stderr, "\n");
        return true;
    }
    if (strcmp(cmd, "speed") == 0) {
        S->speed = (float)atof(arg);
        return true;
    }
    if (strcmp(cmd, "loop") == 0) {
        S->loop = atoi(arg) != 0;
        return true;
    }
    if (strcmp(cmd, "zoom") == 0) {
        S->wantZoom = (float)atof(arg);
        return true;
    }
    if (strcmp(cmd, "export") == 0) {
        if (strcmp(arg, "frame") == 0) {
            exportFrame();
        } else if (strcmp(arg, "sheet") == 0) {
            exportSheet();
        } else if (strcmp(arg, "seq") == 0) {
            exportSequence();
        } else {
            return false;
        }
        return true;
    }
    if (strcmp(cmd, "vram") == 0) {
        // the sandbox's VRAM, raw 1024 x 512 halfwords (validation)
        FILE* f = fopen(arg, "wb");
        if (f != nullptr) {
            fwrite(xgVgpuVram(xgSpriteGpu(S->sprite)), 2, XG_VRAM_W * XG_VRAM_H, f);
            fclose(f);
        }
        return f != nullptr;
    }
    if (strcmp(cmd, "info") == 0) {
        if (S->entry.kind == K_NPC) {
            const auto& entries = S->npcSources;
            int fields = 0, last = -1;
            for (const Entry& e : entries) if (e.field != last) { ++fields; last = e.field; }
            fprintf(stderr, "extras: sprites: NPC catalogue: %zu unique NPCs, %zu sources across %d fields\n",
                    S->cats[K_NPC].entries.size(), entries.size(), fields);
            if (S->entry.npcGroup >= 0) fprintf(stderr, "extras: sprites: NPC group %d: %zu locations\n",
                    S->entry.npcGroup, S->cats[K_NPC].entries[S->entry.npcGroup].locations.size());
            const auto& l = fieldLocation(S->entry.field);
            fprintf(stderr, "extras: sprites: NPC location: %s - %s; %d rooms, %zu displayed sprites\n",
                l.area, l.room, S->npcRooms, S->npcBrowse.size());
            fprintf(stderr, "extras: sprites: NPC sheet: field %03d NPC %02d: [%s] %s\n", S->entry.field, S->entry.npc,
                    S->npcTag.c_str(), S->npcNote.c_str());
        }
        fprintf(stderr, "extras: sprites: playback playing %d loop %d\n", S->playing, S->loop);
        XgSprite* sp = S->sprite;
        fprintf(stderr, "extras: sprites: preview bounds %d %d %d %d\n", S->ax0, S->ay0, S->ax1, S->ay1);
        fprintf(stderr, "extras: sprites: source %s disc %d: %s; file %02X/%03X field %d section %d member %d\n",
                xgDiscRegion(disc()), xgDiscNumber(disc()), xgDiscPath(disc()), S->entry.dir, S->entry.file,
                S->entry.field, S->entry.npc >= 0 ? 3 : -1, S->entry.npc);
        fprintf(stderr, "extras: sprites: %s (%s): %d anims, %d frames, %d palettes x %d colours, %s, bounds %d %d %d %d\n",
                S->entry.label.c_str(), S->name.c_str(), xgSpriteAnimationCount(sp), xgSpriteFrameCount(sp),
                xgSpritePaletteCount(sp), xgSpriteColours(sp), xgSpritePrebaked(sp) ? "sheet" : "pieces", S->bx0,
                S->by0, S->bx1, S->by1);
        for (int k = 0; k < xgSpriteAnimationCount(sp); k++) {
            int alias = xgSpriteAnimationAlias(sp, k);
            if (alias >= 0) fprintf(stderr, "extras: sprites:   anim %d shares Anim %d's retail header\n", k, alias);
            fprintf(stderr, "extras: sprites:   anim %d: %d dirs, flags %d, left out %d %d %d %d\n", k,
                    xgSpriteAnimationDirections(sp, k), xgSpriteAnimationFlags(sp, k), xgSpriteAnimationSkipped(sp, k, 0),
                    xgSpriteAnimationSkipped(sp, k, 1), xgSpriteAnimationSkipped(sp, k, 2),
                    xgSpriteAnimationSkipped(sp, k, 3));
        }
        fprintf(stderr, "extras: sprites: %s %d dir %d palette %d: frame %d step %d ended %d effects %d parts %d key %08x, "
                        "%d prims, %d steps\n",
                S->tab == TAB_ANIMS ? "anim" : "frame", S->tab == TAB_ANIMS ? S->anim : S->frame, S->dir, S->palette,
                S->state.frame, S->state.step, S->state.ended, S->state.effects, S->state.subSprites, S->state.key,
                xgVgpuPrimCount(xgSpriteGpu(sp)), S->steps);
        xgSpriteDebugDump(sp);
        const XgPrim* pr = xgVgpuPrims(xgSpriteGpu(sp));
        for (int k = 0; k < xgVgpuPrimCount(xgSpriteGpu(sp)); k++) { /* every prim: the extractor keys the parts on them (a cap of 12 lost the Aveh soldier's two) */
            fprintf(stderr, "extras: sprites:   prim %d: (%d,%d) (%d,%d) (%d,%d) (%d,%d) uv (%d,%d)-(%d,%d) tpage %04x clut %04x "
                            "rgb %d %d %d flags %x\n",
                    k, pr[k].x[0], pr[k].y[0], pr[k].x[1], pr[k].y[1], pr[k].x[2], pr[k].y[2], pr[k].x[3], pr[k].y[3],
                    pr[k].u[0], pr[k].v[0], pr[k].u[3], pr[k].v[3], pr[k].tpage, pr[k].clut, pr[k].r[0], pr[k].g[0],
                    pr[k].b[0], pr[k].flags);
        }
        return true;
    }
    return false;
}

bool bookmark(Bookmark& b) {
    if (!S->sprite || !bookmarkSource(b, disc(), "sprites", S->entry.title)) return false;
    const Entry& e = S->entry;
    int n = e.kind == K_PARTY ? e.file - 5 : e.kind == K_WORLD ? e.file - 2 : e.kind == K_FIELD2D ? e.file - 0x77A : e.kind == K_GEAR ? e.file - 0x15 : e.kind == K_NPC ? e.field : e.kind == K_ENEMY ? (e.file - 3) / 2 : e.file;
    int slot = e.dir == 0x0E ? 0x0E : e.kind == K_ENEMY ? e.enemy : std::max(0, e.npc);
    if (e.dir == 0x0E) n = (e.file - 0x22) / 2;
    b.target.commands.push_back({"source", std::string(kindIds[e.kind]) + " " + std::to_string(n) + " " + std::to_string(slot)});
    b.target.commands.push_back({"tab", S->tab == TAB_FRAMES ? "frames" : "anims"});
    b.target.commands.push_back({"anim", std::to_string(S->anim)});
    b.target.commands.push_back({"dir", std::to_string(S->dir)});
    b.target.commands.push_back({"palette", std::to_string(S->palette)});
    b.target.commands.push_back({"frame", std::to_string(S->frame)});
    b.target.commands.push_back({"flip", S->flip ? "1" : "0"});
    if (S->view) {
        float units = (float)xgSpriteUnits(S->sprite);
        b.target.commands.push_back({"zoom", std::to_string(S->view->zoom() * units / unit())});
    }
    b.target.commands.push_back({"pause", ""});
    b.target.commands.push_back({"loop", S->loop ? "1" : "0"});
    // restart() already draws tick 1; restore only the ticks after it.
    if (S->tab == TAB_ANIMS) b.target.commands.push_back({"step", std::to_string(std::min(10000, std::max(0, S->steps - 1)))});
    return true;
}
View* picture() { return S->view.get(); }

const Viewer viewer = {"sprites", "Sprite viewer",
                       "Field and battle sprites: animations by the game's own code, frames, palettes; PNG export", 20,
                       opened, closed, draw, back, command, nullptr, nullptr, bookmark, picture};

} // namespace

XG_EXTRAS_VIEWER(viewer);
