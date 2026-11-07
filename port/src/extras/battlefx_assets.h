#ifndef PORT_EXTRAS_BATTLEFX_ASSETS_H
#define PORT_EXTRAS_BATTLEFX_ASSETS_H

#include <string>
#include <vector>
#include "disc.h"
#include "sprites_game.h"
#include "battle_moves.h"

namespace extras {
struct BattleAbility {
    int id, file;
    int partyFile = 0;
    std::string name;
    bool pieces = false;
    int programs = 1;
};
std::vector<BattleAbility> battleAbilities(XgDisc* disc);
// The names the battle shows for a party member's and a Gear's commands
// (src/main/dialog.c text banks of 01/07): deathblow d of character c is
// bank c entry d (getDeathblowName), Ether n of command loadout l bank 20
// entry 16l + n (drawCircleMenuChi_updateSub0Sub1), Gear g's attack entries
// 0..14 bank 28 + g (getGearDeathblowName), its Ethers bank 48 entry 16g + n,
// its fuel specials bank 52 entry 4g + n. Empty: not a name (an unused
// entry, or text the extras cannot spell: kana and kanji).
struct BattleNames {
    std::vector<std::string> deathblows, spells, gearMoves, gearEthers, gearSpecials;
    std::vector<bool> gearEthersUsed, gearSpecialsUsed; // the entry holds a name (unused: an empty line)
    XgBattleLearnable learn{};          // of the loadout (battle_moves.h); all 0 without one
};
BattleNames battleNames(XgDisc* disc, int character, int loadout = -1, int gear = -1);
// A command loadout's 38 entries as the disc has them (0C/03 member 4 + l,
// LZSS): attack type and attack flags of each.
struct BattleCommand { int type, flags; };
std::vector<BattleCommand> battleCommands(XgDisc* disc, int loadout);

struct BattleModelAsset {
    int id;
    std::vector<uint8_t> model, animation;
    struct Extra { int animation, file; std::vector<uint8_t> data; };
    std::vector<Extra> extras;
    mutable std::vector<XgSpriteModelExtra> extraSetup;
};

// Own the bytes until xgSpriteCreate has copied/consumed the setup.
struct BattleSpriteAsset {
    std::vector<uint8_t> bundle, texture, common, commonTexture, fragments, party, caster;
    std::vector<BattleModelAsset> models;
    mutable std::vector<XgSpriteModel> modelSetup;
    int fragment = -1;
    bool ability = false;
    bool load(XgDisc* disc, int dir, int file, int partyFile = 0);
    XgSpriteSetup setup() const;
    std::vector<std::pair<int, int>> modelFiles() const;
};
} // namespace extras
#endif
