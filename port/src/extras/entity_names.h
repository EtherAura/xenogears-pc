#ifndef PORT_EXTRAS_ENTITY_NAMES_H
#define PORT_EXTRAS_ENTITY_NAMES_H
#include <map>
#include <string>
#include <vector>
extern "C" {
#include "disc.h"
}
namespace extras {
inline std::string entityLabel(const std::string& name, const std::string& code) {
    return name.empty() ? code : name + "  (" + code + ")";
}
// Owned metadata from this image. Asset selectors and export paths keep their
// numeric/internal identities; these names are only their visible labels.
class EntityNames {
public:
    explicit EntityNames(XgDisc* disc);
    std::string party(int id) const;
    std::string gear(int id) const;
    std::string battleGear(int file) const;
    std::string battleSprite(int file) const;
    std::string enemy(int set, int entity);
    std::string battling(int id) const;
    std::string battlingCode(int id) const;
    std::string fieldGear(int id);
    std::string npc(int field, int slot);
private:
    XgDisc* disc;
    int region;
    std::vector<std::string> defaults, fighters, fighterCodes;
    std::vector<int> gearFiles, gearExtras, spriteFiles, specialFiles;
    std::map<int, std::vector<std::string>> enemies, npcs;
};
}
#endif
