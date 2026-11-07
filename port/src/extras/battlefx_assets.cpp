#include "battlefx_assets.h"
#include <cstdio>
#include <cstdlib>
#include <set>
extern "C" {
#include "battlefx_decode.h"
#include "files.h"
#include "tables_int.h"
#include "models_vm.h"
extern uint8_t sizePerBytecodeTable[128];
void modelsBattleExtraFile(int operand, int param, int* file);
}

namespace extras {
namespace {
std::vector<uint8_t> read(XgDisc* disc, int dir, int file, bool compressed = false) {
    int entry = xgDiscFileEntry(disc, dir, file), size = 0;
    if (entry < 0 || xgDiscEntry(disc, entry).size <= 0 || xgDiscEntry(disc, entry).size > 8 * 1024 * 1024)
        return {};
    uint8_t* bytes = xgDiscReadEntry(disc, entry, &size);
    if (bytes && compressed) {
        uint8_t* unpacked = xgLzssDecompress(bytes, size, &size);
        free(bytes);
        bytes = unpacked;
    }
    std::vector<uint8_t> out;
    if (bytes && size > 0) out.assign(bytes, bytes + size);
    free(bytes);
    return out;
}
uint32_t word(const std::vector<uint8_t>& data, size_t at) {
    return at + 4 <= data.size() ? tablesRd32(data.data() + at) : 0;
}
// Member k of a relocated archive (u32 count, u32 offsets: the loader's
// reloc[k + 1]), LZSS, up to the next member. Some streams end a few bytes
// past that (the loader reads on into the next member): those last bytes,
// past every table read here, come out as zeros.
std::vector<uint8_t> member(const std::vector<uint8_t>& archive, unsigned k) {
    unsigned count = word(archive, 0);
    if (archive.size() < 8 || k >= count || 4 + 4 * size_t(count) > archive.size()) return {};
    size_t at = word(archive, 4 + 4 * size_t(k)), end = archive.size();
    if (at < 8 || at >= archive.size()) return {};
    for (unsigned j = 0; j < count; j++) {
        size_t next = word(archive, 4 + 4 * size_t(j));
        if (next > at && next < end) end = next;
    }
    int size = 0;
    uint8_t* bytes = xgLzssDecompress(archive.data() + at, int(end - at), &size);
    std::vector<uint8_t> out;
    if (bytes && size > 0) out.assign(bytes, bytes + size);
    free(bytes);
    return out;
}
std::string bankName(const std::vector<uint8_t>& text, int bankId, int id, int region) {
    if (word(text, 0) <= unsigned(bankId) || 8 + 4 * size_t(bankId) > text.size()) return {};
    size_t bank = word(text, 4 + 4 * bankId);
    if (bank + 4 > text.size() || id > tablesRd16(text.data() + bank) ||
        bank + 6 + 2 * size_t(id) > text.size()) return {};
    size_t at = bank + tablesRd16(text.data() + bank + 4 + 2 * id);
    char name[96];
    if (at >= text.size() || !tablesGameText(text.data() + at, int(text.size() - at), region, name, sizeof(name)))
        return {};
    return name;
}
// The entry holds text: not missing, not empty and not the lone new-line
// code (0x01) unused entries hold.
bool bankUsed(const std::vector<uint8_t>& text, int bankId, int id) {
    if (word(text, 0) <= unsigned(bankId) || 8 + 4 * size_t(bankId) > text.size()) return false;
    size_t bank = word(text, 4 + 4 * bankId);
    if (bank + 4 > text.size() || id > tablesRd16(text.data() + bank) || bank + 6 + 2 * size_t(id) > text.size())
        return false;
    size_t at = bank + tablesRd16(text.data() + bank + 4 + 2 * id);
    return at < text.size() && text[at] != 0 && text[at] != 1;
}
// Follow animation entries, spawned controllers and branches before reading
// a model pair. Model requests in texture/vertex data are not selectors.
std::set<int> modelIds(const std::vector<uint8_t>& b) {
    std::set<int> ids;
    std::vector<size_t> todo;
    std::vector<bool> seen(b.size());
    auto anim = [&](size_t h) {
        if (h + 4 <= b.size()) todo.push_back(h + 2 + tablesRd16(b.data() + h + 2));
    };
    size_t table = word(b, 4);
    if (table + 2 > b.size()) return ids;
    for (int k = 0; k < (tablesRd16(b.data() + table) & 63); k++)
        if (table + 4 + 2 * k <= b.size()) anim(table + tablesRd16(b.data() + table + 2 + 2 * k));
    while (!todo.empty()) {
        size_t pc = todo.back(); todo.pop_back();
        while (pc < b.size() && !seen[pc]) {
            seen[pc] = true;
            int op = b[pc], n = op < 128 ? 1 : op == 0xBE ? 3 : sizePerBytecodeTable[op - 128];
            if (!n || pc + n > b.size()) break;
            if (op == 0xEC && b[pc + 1] == 0x46) ids.insert(b[pc + 2]);
            if (op == 0xE0) anim(pc + 1 + int16_t(tablesRd16(b.data() + pc + 1)));
            if (op == 0xE1 || op == 0xD4 || op == 0xE2 || op == 0xE3 || op == 0xE4 || op == 0xF8 || op == 0xFB)
                todo.push_back(pc + int16_t(tablesRd16(b.data() + pc + 1)));
            if (op == 0xFA) todo.push_back(pc + int16_t(tablesRd16(b.data() + pc + 2)));
            if (op == 0x80 || op == 0x81 || op == 0x82 || op == 0x85 || op == 0x8E || op == 0x8F || op == 0xE1 || op == 0xD4) break;
            pc += n;
        }
    }
    return ids;
}
struct ExtraRequest { int file = -1; };
int findExtra(const uint16_t* op, int, void* data) {
    auto* request = static_cast<ExtraRequest*>(data);
    if ((op[0] & 255) == 4 && op[1] < 32 && request->file < 0)
        modelsBattleExtraFile(op[1], op[0] >> 8, &request->file);
    return (op[0] & 255) != 0;
}
void modelExtras(XgDisc* disc, BattleModelAsset& m) {
    size_t a = word(m.animation, 4), size = m.animation.size();
    if (a + 4 > size) return;
    unsigned count = word(m.animation, a);
    if (count < 2 || count > 256 || a + 4 * (count + 1) > size) return;
    for (unsigned k = 2; k <= count; k++) {
        size_t pc = a + word(m.animation, a + 4 * k), end = size;
        for (unsigned j = 2; j <= count; j++) {
            size_t next = a + word(m.animation, a + 4 * j);
            if (next > pc && next < end) end = next;
        }
        if (pc >= end || (pc & 1)) continue;
        ExtraRequest request;
        xgMechaScan(reinterpret_cast<const uint16_t*>(m.animation.data() + pc), int((end - pc) / 2), 1, findExtra, &request);
        if (request.file > 0) {
            auto data = read(disc, 0x2A, request.file);
            if (!data.empty()) m.extras.push_back({int(k - 2), request.file, std::move(data)});
        }
    }
}
} // namespace

BattleNames battleNames(XgDisc* disc, int character, int loadout, int gear) {
    BattleNames names;
    if (!disc || character < 0 || character > 10) return names;
    if (loadout < 0 || loadout > 10) loadout = character;
    int region = tablesRegionOf(disc);
    auto text = read(disc, 1, 7, true);
    for (int i = 0; i < 13; i++) names.deathblows.push_back(bankName(text, character, i, region));
    for (int i = 0; i < 16; i++) names.spells.push_back(bankName(text, 20, 16 * loadout + i, region));
    if (gear >= 0 && gear < 19) {
        for (int i = 0; i < 15; i++) names.gearMoves.push_back(bankName(text, 28 + gear, i, region));
        for (int i = 0; i < 16; i++) {
            names.gearEthers.push_back(bankName(text, 48, 16 * gear + i, region));
            names.gearEthersUsed.push_back(bankUsed(text, 48, 16 * gear + i));
        }
        for (int i = 0; i < 4; i++) {
            names.gearSpecials.push_back(bankName(text, 52, 4 * gear + i, region));
            names.gearSpecialsUsed.push_back(bankUsed(text, 52, 4 * gear + i));
        }
    }
    auto levels = member(read(disc, 0x12, 2), 0);
    xgBattleLearnable(levels.data(), int(levels.size()), loadout, &names.learn);
    return names;
}
std::vector<BattleCommand> battleCommands(XgDisc* disc, int loadout) {
    std::vector<BattleCommand> out;
    if (!disc || loadout < 0 || loadout > 10) return out;
    auto table = member(read(disc, 0x0C, 3), 4 + loadout);
    for (size_t at = 0; at + 0x28 <= table.size() && out.size() < 38; at += 0x28)
        out.push_back({int16_t(tablesRd16(table.data() + at + 2)), tablesRd16(table.data() + at + 0x0A)});
    return out;
}
std::vector<BattleAbility> battleAbilities(XgDisc* disc) {
    std::vector<BattleAbility> out;
    if (!disc) return out;
    int region = tablesRegionOf(disc);
    auto battle = read(disc, 1, 0x10, true), loader = read(disc, 0x0C, 4), text = read(disc, 1, 7, true);
    auto levels = member(read(disc, 0x12, 2), 0);
    size_t table = region == 'U' ? 0x800C3630 - 0x8006FAF0 : region == 'J' ? 0x800C2D68 - 0x8006F180 : battle.size();
    size_t mapping = region == 'U' ? 0x801E95BC - 0x801E4000 : region == 'J' ? 0x801E9664 - 0x801E4000 : loader.size();
    const char* chars[] = {"Fei", "Elly", "Citan", "Bart", "Billy", "Rico", "Emeralda", "Chu-Chu", "Maria", "Citan (sword)", "Emeralda (adult)"};
    // Which character's deathblow or Ether plays each ability: the entries a
    // loadout can learn, their animation as startBattleAttackAnimation picks
    // it (the attack type when flags bit 0, else the entry), from 0x10 an
    // ability of the character's range (processBattleAnimation:
    // abilityAnimationTable0[c] + m47 - 0x10; below abilityAnimationTable1[c]
    // with the party sprite's pieces).
    struct Owner { int character = -1, end = 0; std::string name; };
    std::vector<Owner> owners(512);
    for (int ch = 0; ch < 11 && table + 48 <= battle.size(); ch++) {
        int base = tablesRd16(battle.data() + table + 2 * ch), end = tablesRd16(battle.data() + table + 24 + 2 * ch);
        XgBattleLearnable learn;
        xgBattleLearnable(levels.data(), int(levels.size()), ch, &learn);
        auto commands = battleCommands(disc, ch);
        for (int e = 7; e < int(commands.size()); e++) {
            bool deathblow = e < 7 + learn.deathblows, ether = e >= 22 && e < 22 + learn.ethers;
            if (!deathblow && !ether) continue;
            int anim = (commands[e].flags & 1 ? commands[e].type : e) & 0xFF;
            int id = anim - 0x10 + base;
            if (anim < 0x10 || id < 0 || id >= int(owners.size()) || owners[id].character >= 0) continue;
            std::string name = deathblow ? bankName(text, ch, e - 7, region) : bankName(text, 20, 16 * ch + e - 22, region);
            if (name.empty()) {
                char fallback[32];
                snprintf(fallback, sizeof(fallback), deathblow ? "Deathblow %d" : "Ether %d", deathblow ? e - 6 : e - 21);
                name = fallback;
            }
            owners[id] = {ch, end, std::string(chars[ch]) + " - " + name};
        }
    }
    for (int file = 0x22; file <= xgDiscDirFiles(disc, 0x0E); file += 2) {
        auto data = read(disc, 0x0E, file);
        if (!xgBattleFxBundle(data.data(), int(data.size()))) continue;
        int id = (file - 0x22) / 2;
        bool pieces = !(tablesRd16(data.data() + word(data, 8)) & 0x8000);
        char label[96]; snprintf(label, sizeof(label), "Ability %03d  0E/%03X", id, file);
        BattleAbility item{id, file, 0, label, pieces};
        item.programs = tablesRd16(data.data() + word(data, 4)) & 63;
        const Owner& owner = owners[id];
        if (owner.character >= 0) {
            item.name = owner.name;
            if (pieces && id < owner.end && mapping + 8 * (owner.character + 1) <= loader.size())
                item.partyFile = int(word(loader, mapping + 8 * owner.character));
        }
        out.push_back(std::move(item));
    }
    return out;
}

bool BattleSpriteAsset::load(XgDisc* disc, int dir, int file, int partyFile) {
    ability = dir == 0x0E;
    bundle = read(disc, dir, file);
    if (!xgBattleFxBundle(bundle.data(), int(bundle.size()))) return false;
    texture = dir == 0x0E ? read(disc, dir, file + 1) : std::vector<uint8_t>{};
    common = read(disc, 0x2C, 2);
    commonTexture = read(disc, 0x2C, 1);
    fragments.clear(); fragment = -1;
    if (dir == 0x0E) {
        for (unsigned k = 3; k < word(bundle, 0); k++) {
            unsigned at = word(bundle, 4 + 4 * k),
                     end = k + 1 < word(bundle, 0) ? word(bundle, 8 + 4 * k) : unsigned(bundle.size());
            if (at < end && end <= bundle.size() && xgBattleFxBundle(bundle.data() + at, int(end - at))) {
                fragments.assign(bundle.begin() + at, bundle.begin() + end);
                fragment = int(k);
            }
        }
    }
    party = partyFile ? read(disc, 0x2D, partyFile) : std::vector<uint8_t>{};
    caster = ability && !partyFile ? read(disc, 0x2D, 1) : std::vector<uint8_t>{};
    models.clear();
    if (ability) for (int id : modelIds(bundle)) {
        BattleModelAsset m{id, read(disc, 0x28, 2 * id + 1), read(disc, 0x28, 2 * id + 2), {}, {}};
        if (m.model.size() < 24 || m.animation.size() < 16) return false;
        modelExtras(disc, m);
        models.push_back(std::move(m));
    }
    return !partyFile || xgBattleFxBundle(party.data(), int(party.size()));
}
XgSpriteSetup BattleSpriteAsset::setup() const {
    XgSpriteSetup s{};
    s.bundle = bundle.data(); s.bundleSize = int(bundle.size());
    s.battle = s.nativeEffects = 1;
    s.vramX = party.empty() ? 0x380 : 0x100;
    s.vramY = party.empty() ? (ability ? 0x100 : 0) : 0x1C0;
    s.clutY = party.empty() ? (ability ? 0x1F4 : 0x1D1) : 0x1C0;
    s.effects = common.data(); s.effectsSize = int(common.size());
    s.effectsTexture = commonTexture.data(); s.effectsTextureSize = int(commonTexture.size());
    s.textureFile = texture.data(); s.textureFileSize = int(texture.size());
    s.fragments = fragments.data(); s.fragmentsSize = int(fragments.size());
    s.caster = caster.data(); s.casterSize = int(caster.size());
    modelSetup.clear();
    for (const auto& m : models) {
        m.extraSetup.clear();
        for (const auto& e : m.extras) m.extraSetup.push_back({e.animation, e.file, e.data.data(), int(e.data.size())});
        modelSetup.push_back({m.id, m.model.data(), int(m.model.size()), m.animation.data(), int(m.animation.size()),
                              m.extraSetup.data(), int(m.extraSetup.size())});
    }
    s.models = modelSetup.data(); s.modelCount = int(modelSetup.size());
    if (!party.empty()) {
        s.companion = party.data(); s.companionSize = int(party.size());
        s.special = 1; s.subSprites = 0x20;
    }
    return s;
}
std::vector<std::pair<int, int>> BattleSpriteAsset::modelFiles() const {
    std::set<std::pair<int, int>> files;
    for (const auto& m : models) {
        files.insert({0x28, 2 * m.id + 1}); files.insert({0x28, 2 * m.id + 2});
        for (const auto& e : m.extras) files.insert({0x2A, e.file});
    }
    return {files.begin(), files.end()};
}
} // namespace extras
