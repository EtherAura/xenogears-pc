#include "navigation.h"
#include <algorithm>
#include <utility>

namespace extras {

ViewerTarget visualTarget(const ViewerTarget& target) {
    const std::string& id = target.viewer;
    if (id != "sprites" && id != "models" && id != "animations" && id != "battlefx" && id != "battle" && id != "maps" && id != "gallery") return target;
    std::string family = id == "animations" ? "models" : id;
    std::string source;
    for (const auto& command : target.commands) {
        if (command.name == "source") source = command.argument.substr(0, command.argument.find(' '));
        if (family == "models" && (command.name == "field" || command.name == "world"))
            source = command.name == "field" ? "fields" : "world";
    }
    struct Section { const char* family; const char* source; const char* id; };
    static const Section sections[] = {
        {"sprites", "party", "party"}, {"sprites", "gear", "gear"}, {"sprites", "npc", "npc"},
        {"sprites", "battle", "battle-sprites"}, {"sprites", "special", "special"},
        {"sprites", "effects", "effects"}, {"sprites", "ability", "effects"}, {"sprites", "enemy", "enemy-sprites"},
        {"models", "gears", "field-gears"}, {"models", "battle", "battle-models"}, {"models", "enemies", "enemy-models"},
        {"models", "arenas", "arenas"}, {"models", "fields", "fields"}, {"models", "world", "world"}, {"models", "battling", "battling"},
        {"battlefx", "abilities", "abilities"}, {"battlefx", "common", "common"}, {"battlefx", "gears", "trails"},
    };
    ViewerCommand entry{"family", family};
    // Pick the destination before applying its image command: restoring an
    // NPC link must not clear the retained Party section on the way there.
    for (const auto& section : sections)
        if (family == section.family && source == section.source) entry = {"section", section.id};
    ViewerTarget result{"visuals", {entry}};
    result.commands.insert(result.commands.end(), target.commands.begin(), target.commands.end());
    return result;
}

bool ViewerNavigation::follow(const ViewerTarget& target) {
    if (pending() || current_.empty() || target.viewer.empty() || target.viewer == current_ ||
        parents_.size() >= MAX_DEPTH || target.commands.size() > 32 ||
        std::find(parents_.begin(), parents_.end(), target.viewer) != parents_.end()) return false;
    target_ = target; /* callers may destroy or change their strings after this call */
    pending_ = FOLLOW;
    return true;
}

bool ViewerNavigation::back() {
    if (pending() || parents_.empty()) return false;
    pending_ = BACK;
    return true;
}

const std::string& ViewerNavigation::source() const {
    static const std::string empty;
    return parents_.empty() ? empty : parents_.back();
}

void ViewerNavigation::root(const std::string& id) {
    current_ = id;
}

void ViewerNavigation::dispatch(NavigationHost& host) {
    if (dispatching_) return;
    Action action = pending_;
    pending_ = NONE;
    if (action == NONE) return;
    struct Dispatch {
        bool& flag;
        explicit Dispatch(bool& f) : flag(f) { flag = true; }
        ~Dispatch() { flag = false; }
    } dispatch(dispatching_);
    if (action == BACK) {
        host.close(current_);
        current_ = parents_.back();
        parents_.pop_back();
        host.resume(current_);
        return;
    }
    ViewerTarget target = std::move(target_);
    if (!host.exists(target.viewer)) {
        host.failed("Viewer unavailable: " + target.viewer);
        return;
    }
    host.suspend(current_);
    host.open(target.viewer);
    for (const ViewerCommand& command : target.commands) {
        if (!host.command(command)) {
            host.close(target.viewer);
            host.resume(current_);
            host.failed("Cannot open linked asset: " + command.name + " " + command.argument);
            return;
        }
    }
    parents_.push_back(current_);
    current_ = target.viewer;
}

void ViewerNavigation::clear(NavigationHost& host) {
    pending_ = NONE;
    target_ = {};
    if (!current_.empty()) host.close(current_);
    for (auto p = parents_.rbegin(); p != parents_.rend(); ++p) host.close(*p);
    parents_.clear();
    current_.clear();
}

} // namespace extras
