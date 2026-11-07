#include "entity_names.h"
#include "field_locations.h"
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <set>
extern "C" {
#include "files.h"
#include "tables_int.h"
#include "gallery.h"
}
namespace extras {
namespace {
std::vector<uint8_t> read(XgDisc* d, int dir, int file, bool compressed = false) {
    int e = xgDiscFileEntry(d, dir, file), n = 0;
    if (e < 0 || xgDiscEntry(d, e).size <= 0 || xgDiscEntry(d, e).size > 8 * 1024 * 1024) return {};
    auto* p = xgDiscReadEntry(d, e, &n);
    if (p && compressed) { auto* q = xgLzssDecompress(p, n, &n); free(p); p = q; }
    std::vector<uint8_t> out;
    if (p && n > 0) out.assign(p, p + n);
    free(p); return out;
}
unsigned word(const std::vector<uint8_t>& b, size_t o) { return o + 2 <= b.size() ? tablesRd16(b.data() + o) : 0; }
unsigned dword(const std::vector<uint8_t>& b, size_t o) { return o + 4 <= b.size() ? tablesRd32(b.data() + o) : 0; }
std::string text(const std::vector<uint8_t>& b, size_t at, int region) {
    char out[160];
    return at < b.size() && tablesGameText(b.data() + at, int(b.size() - at), region, out, sizeof(out)) ? out : "";
}
const char* canonical[] = {
    "Fei", "Elly", "Citan", "Bart", "Billy", "Rico", "Emeralda", "Chu-Chu", "Maria", "Citan", "Emeralda",
    "Weltall", "Weltall-2", "Vierge", "Heimdal", "Brigandier", "Renmazuo", "Stier", "Chu-Chu", "Seibzehn",
    "Crescens", "Regurus", "Fenrir", "Andvari", "Renmazuo", "Stier", "Xenogears", "Heimdal", "Dora", "Yggdrasil", ""};
std::string join(const std::set<std::string>& names) {
    std::string out;
    for (const auto& s : names) { if (!out.empty()) out += " / "; out += s; }
    return out;
}
}
EntityNames::EntityNames(XgDisc* d) : disc(d), region(tablesRegionOf(d)), defaults(std::begin(canonical), std::end(canonical)) {
    auto state = read(d, 0x10, 3), textFile = read(d, 1, 7, true);
    size_t bank = dword(textFile, 4 + 4 * 27);
    if (state.size() >= 31 * 20 && bank > 0 && bank < textFile.size()) {
        for (int i = 0; i < 31; i++) {
            std::vector<uint8_t> decoded;
            for (int k = 0; k < 10; k++) {
                unsigned c = word(state, i * 20 + 2 * k);
                if (c == 15) break; // default names terminate with the name token
                size_t at = bank + 2 * c;
                if (at + 2 > textFile.size()) { decoded.clear(); break; }
                if (textFile[at]) decoded.push_back(textFile[at]);
                decoded.push_back(textFile[at + 1]);
            }
            decoded.push_back(0);
            std::string name = text(decoded, 0, region);
            if (!name.empty()) defaults[i] = name;
        }
    }
    auto battle = read(d, 1, 0x10, true), loader = read(d, 0x0C, 4);
    size_t table = region == 'U' ? 0x800C3508 - 0x8006FAF0 : 0x800C2C40 - 0x8006F180;
    for (int i = 0; i < 20 && table + 2 * i + 2 <= battle.size(); i++) {
        gearFiles.push_back(battle[table + 2 * i] + 1);
        gearExtras.push_back(battle[table + 2 * i + 1]);
    }
    table = region == 'U' ? 0x801E95BC - 0x801E4000 : 0x801E9664 - 0x801E4000;
    for (int i = 0; i < 11 && table + 8 * (i + 1) <= loader.size(); i++) {
        spriteFiles.push_back(dword(loader, table + 8 * i));
        specialFiles.push_back(dword(loader, table + 8 * i + 4));
    }
    auto battling = read(d, 1, 0x0D, true);
    unsigned base = region == 'U' ? 0x8006FAF0 : 0x8006F180;
    table = (region == 'U' ? 0x80091964 : 0x80090FA0) - base;
    auto ascii = [&](unsigned p) -> std::string {
        if (p < base || p - base >= battling.size()) return {};
        size_t at = p - base, end = at;
        while (end < battling.size() && end - at < 80 && battling[end] >= 32 && battling[end] < 127) ++end;
        return end < battling.size() && battling[end] == 0 ? std::string(battling.begin() + at, battling.begin() + end) : "";
    };
    fighters.resize(49); fighterCodes.resize(49);
    for (int i = 0; i < 49 && table + 12 * (i + 1) <= battling.size(); i++) {
        int id = dword(battling, table + 12 * i);
        if (id < 0 || id >= 49) continue;
        fighters[id] = ascii(dword(battling, table + 12 * i + 8));
        fighterCodes[id] = ascii(dword(battling, table + 12 * i + 4));
    }
}
std::string EntityNames::party(int id) const {
    if (id < 0 || id >= 11) return {};
    return defaults[id] + (id == 9 ? " (sword)" : id == 10 ? " (adult)" : "");
}
std::string EntityNames::gear(int id) const {
    // These debug party slots carry shortened/internal names in the save
    // template (BARTHOS and Yggdra), rather than the entities' story names.
    if (id == 17) return "Dora";
    if (id == 18) return "Yggdrasil";
    return id >= 0 && id < 20 ? defaults[11 + id] : "";
}
std::string EntityNames::battleGear(int file) const {
    for (size_t i = 0; i < gearFiles.size(); i++) {
        int offset = file - gearFiles[i];
        if (offset >= 0 && offset <= gearExtras[i] && offset % 2 == 0)
            return gear(int(i)) + (offset ? " (weapon)" : "");
    }
    return {};
}
std::string EntityNames::battleSprite(int file) const {
    std::set<std::string> names;
    for (size_t i = 0; i < spriteFiles.size(); i++) if (spriteFiles[i] == file || specialFiles[i] == file) names.insert(party(int(i)));
    return join(names);
}
std::string EntityNames::enemy(int set, int entity) {
    auto found = enemies.find(set);
    if (found == enemies.end()) {
        auto b = read(disc, 0x0D, 2 + 2 * set);
        std::vector<std::string> names;
        size_t bank = word(b, 0x30);
        if (bank >= 0x32 && bank + 4 <= b.size()) {
            int count = std::min(16u, word(b, bank));
            for (int k = 1; k <= count && bank + 6 + 2 * k <= b.size(); k++)
                names.push_back(text(b, bank + word(b, bank + 4 + 2 * k), region));
        }
        found = enemies.emplace(set, std::move(names)).first;
    }
    return entity >= 0 && entity < int(found->second.size()) ? found->second[entity] : "";
}
std::string EntityNames::battling(int id) const { return id >= 0 && id < int(fighters.size()) ? fighters[id] : ""; }
std::string EntityNames::battlingCode(int id) const { return id >= 0 && id < int(fighterCodes.size()) ? fighterCodes[id] : ""; }
// Field models use their own ID space; never treat these as party Gear IDs.
// Uncertain identities remain empty so the viewer retains their asset code.
// Review captures and scene uses are recorded in docs/PORT_EXTRAS.md. Shared mesh
// geometry is insufficient evidence: e.g. field 37 is Weltall-Id, while a
// differently textured battle model using that geometry is Sword Seraph.
std::string EntityNames::fieldGear(int id) {
    static const char* names[] = {
        "Weltall", "Vierge", "Shakhan", "Rankar Dragon", "", "Trooper", "Clawknight", "Weltall",
        "Musha Mk100", "Alpha Weltall", "", "", "Calamity", "Brigandier", "Dora",
        "Yggdrasil (wreck)", "", "Heimdal", "Renmazuo", "", "Wyrm", "Seibzehn",
        "Wyvern", "Stier", "Goliath turret", "Wandknight", "Swordknight", "Aegisknight", "", "",
        "Bladegash", "Skyghene", "Marinebasher", "Grandgrowl", "G-Elements", "Crescens", "Chu-Chu", "Weltall-Id",
        "", "", "Haishao (flight)", "Miang's Gear", "Weltall-2", "Fenrir", "Andvari",
        "Renmazuo (Omnigear)", "Stier (Omnigear)", "Eagle Gunner", "Alpha Weltall (winged)", "",
        "Hammer", "Dora (wreck)", "Reaper", "Salvager", "Opiomorph", "Deus", "Lahan Gear wreck", "Xenogears",
        "Regurus", "Vendetta", "", "Achtzehn", "", "Xenogears (awakening)",
        "Deus (final form)", "Sufal Gear", "", "Etone", "", "Deus (core)",
        "Weltall-Id (folded wings)", "Amphysvena"};
    return id >= 0 && id < int(std::size(names)) ? names[id] : "";
}
std::string EntityNames::npc(int field, int slot) {
    auto found = npcs.find(field);
    if (found == npcs.end()) {
        std::vector<std::string> names(32);
        auto a = read(disc, 4, 0xB8 + 2 * field);
        auto section = [&](int k) {
            size_t at = dword(a, 0x130 + 4 * k); int n = 0;
            uint8_t* p = at > 0 && at < a.size() ? xgLzssDecompress(a.data() + at, int(a.size() - at), &n) : nullptr;
            std::vector<uint8_t> b;
            if (p && n > 0) b.assign(p, p + n);
            free(p); return b;
        };
        auto dialogue = section(7);
        XgFieldScript fs{};
        if (!dialogue.empty() && xgFieldScriptRead(disc, field, &fs)) {
            for (int actor = 0; actor < fs.actors; actor++) {
                uint16_t roots[1][32]{};
                std::fill(std::begin(roots[0]), std::end(roots[0]), fs.routines[actor][0]);
                XgFieldScript one = fs; one.actors = 1; one.routines = roots;
                uint8_t* init = xgFieldScriptWalk(&one);
                int sprite = -1;
                for (int pc = 0; init && pc + 3 <= fs.codeSize; pc++) if (init[pc] && fs.code[pc] == 0x0B) {
                    unsigned v = tablesRd16(fs.code + pc + 1);
                    if ((v & 0x8000) && (v & 0x7fff) < 32) sprite = v & 0x7fff;
                }
                free(init);
                if (sprite < 0) continue;
                std::fill(std::begin(roots[0]), std::end(roots[0]), fs.routines[actor][2]);
                uint8_t* talk = xgFieldScriptWalk(&one);
                std::set<std::string> speakers;
                for (int pc = 0; talk && pc + 4 <= fs.codeSize; pc++) if (talk[pc] &&
                    (fs.code[pc] == 0x03 || fs.code[pc] == 0xD2 || fs.code[pc] == 0xD3)) {
                    unsigned id = tablesRd16(fs.code + pc + 1);
                    if (id > word(dialogue, 0) || 6 + 2 * id > dialogue.size()) continue;
                    size_t at = word(dialogue, 4 + 2 * id), end = at;
                    while (end < dialogue.size() && dialogue[end] != 0 && dialogue[end] != 1 && end - at < 32) end++;
                    // A speaker's line is followed by a quoted dialogue line.
                    if (end + 1 >= dialogue.size() || dialogue[end] != 1 || dialogue[end + 1] != 0x58) continue;
                    auto first = std::vector<uint8_t>(dialogue.begin() + at, dialogue.begin() + end);
                    first.push_back(0); std::string speaker = text(first, 0, region);
                    bool partyMember = false;
                    for (int i = 0; i < 11; i++) partyMember |= speaker == defaults[i];
                    if (!partyMember && !speaker.empty()) speakers.insert(speaker);
                }
                free(talk);
                if (speakers.size() == 1) names[sprite] = *speakers.begin();
            }
            xgFieldScriptFree(&fs);
        }
        found = npcs.emplace(field, std::move(names)).first;
    }
    return slot >= 0 && slot < int(found->second.size()) ? found->second[slot] : "";
}
}
