/* Callback order, owned requests and return-state lifetime without the UI. */
#include "navigation.h"
#include <cstdio>
#include <map>
#include <string>
#include <vector>

using namespace extras;
static int checks;
#include "../../test/check.h" /* no port/src on this test's include path */

struct Host : NavigationHost {
    std::map<std::string, int> state;
    std::vector<std::string> events;
    std::string active;
    ViewerNavigation* nav = nullptr;
    bool reentryRejected = false;
    bool exists(const std::string& id) override { return id != "missing"; }
    void open(const std::string& id) override { events.push_back("open " + id); state[id] = 0; active = id; }
    void suspend(const std::string& id) override { events.push_back("suspend " + id); active.clear(); }
    void resume(const std::string& id) override { events.push_back("resume " + id); active = id; }
    void close(const std::string& id) override { events.push_back("close " + id); state.erase(id); active.clear(); }
    bool command(const ViewerCommand& c) override {
        events.push_back(c.name + " " + c.argument);
        if (c.name == "reenter") {
            reentryRejected = !nav->follow({"other", {}}) && !nav->back();
            nav->dispatch(*this);
        }
        return c.name != "fail";
    }
    void failed(const std::string&) override { events.push_back("failed"); }
};

int main() {
    for (const char* id : {"sprites", "models", "animations", "battlefx", "battle", "maps", "gallery"}) {
        ViewerTarget old{id, {{"image", "Japan.cue"}, {"anim", "7"}}};
        auto target = visualTarget(old);
        CHECK(target.viewer == "visuals" && target.commands.size() == 3);
        CHECK(target.commands[0].name == "family" && target.commands[0].argument == (std::string(id) == "animations" ? "models" : id));
        CHECK(target.commands[1].argument == "Japan.cue" && target.commands[2].argument == "7");
        CHECK(visualTarget(target).commands.size() == 3);
        CHECK(old.viewer == id && old.commands.size() == 2);
    }
    CHECK(visualTarget({"maps", {{"field", "120"}}}).viewer == "visuals");
    for (const auto& pair : std::vector<std::pair<ViewerTarget, std::string>>{
        {{"sprites", {{"image", "disc.cue"}, {"source", "npc 1 0"}}}, "npc"},
        {{"models", {{"image", "disc.cue"}, {"field", "1 0"}}}, "fields"},
        {{"animations", {{"image", "disc.cue"}, {"source", "gears"}, {"item", "5"}}}, "field-gears"},
        {{"battlefx", {{"image", "disc.cue"}, {"source", "gears"}, {"item", "0"}}}, "trails"}}) {
        auto target = visualTarget(pair.first);
        CHECK(target.viewer == "visuals" && target.commands.front().name == "section");
        CHECK(target.commands.front().argument == pair.second && target.commands[1].name == "image");
    }
    ViewerNavigation nav;
    Host host;
    host.nav = &nav;
    CHECK(!nav.back() && !nav.follow({"browser", {}}));
    host.open("maps"); nav.root("maps"); host.events.clear();
    host.state["maps"] = 713; // camera/selection owned by the source viewer
    ViewerTarget target{"browser", {{"image", "Japan Disc 1.cue"}, {"select", "04 148"}}};
    CHECK(nav.follow(target));
    target.viewer = "missing"; target.commands[0].argument = "wrong disc";
    CHECK(host.events.empty() && host.active == "maps" && nav.current() == "maps");
    CHECK(!nav.follow({"other", {}}) && !nav.back());
    nav.dispatch(host);
    CHECK((host.events == std::vector<std::string>{"suspend maps", "open browser", "image Japan Disc 1.cue", "select 04 148"}));
    CHECK(nav.current() == "browser" && nav.source() == "maps" && nav.depth() == 1);
    CHECK(host.state["maps"] == 713 && host.active == "browser");
    CHECK(!nav.follow({"maps", {}}) && !nav.follow({"browser", {}}) && !nav.follow({"", {}}));
    host.events.clear();
    CHECK(nav.back() && host.events.empty() && host.state.count("browser") == 1);
    nav.dispatch(host);
    CHECK((host.events == std::vector<std::string>{"close browser", "resume maps"}));
    CHECK(host.state["maps"] == 713 && !host.state.count("browser") && nav.depth() == 0 && nav.source().empty());
    CHECK(!nav.back());

    host.events.clear();
    CHECK(nav.follow({"missing", {}})); nav.dispatch(host);
    CHECK(host.events == std::vector<std::string>{"failed"} && host.active == "maps" && host.state.size() == 1);
    CHECK(nav.follow({"browser", {}})); nav.dispatch(host);
    host.state["browser"] = 42; host.events.clear();
    CHECK(nav.follow({"models", {{"image", "disc"}, {"fail", "bad file"}, {"never", "run"}}}));
    nav.dispatch(host);
    CHECK((host.events == std::vector<std::string>{"suspend browser", "open models", "image disc", "fail bad file",
                                                 "close models", "resume browser", "failed"}));
    CHECK(host.state["browser"] == 42 && host.state["maps"] == 713 && nav.current() == "browser" && nav.source() == "maps");
    host.events.clear();
    CHECK(nav.follow({"models", {{"reenter", ""}}})); nav.dispatch(host);
    CHECK(host.reentryRejected && nav.depth() == 2 && !nav.pending());
    CHECK(nav.back()); nav.dispatch(host);
    CHECK(host.active == "browser" && host.state["browser"] == 42);
    CHECK(nav.follow({"models", {}})); // pending request is cancelled by Home
    host.events.clear(); nav.clear(host);
    CHECK((host.events == std::vector<std::string>{"close browser", "close maps"}));
    CHECK(host.state.empty() && nav.current().empty() && !nav.pending());
    nav.dispatch(host); CHECK(host.state.empty());
    host.events.clear(); nav.clear(host); CHECK(host.events.empty());

    host.open("root"); nav.root("root");
    ViewerTarget oversized{"target", std::vector<ViewerCommand>(33)};
    CHECK(!nav.follow(oversized));
    for (unsigned i = 0; i < ViewerNavigation::MAX_DEPTH; i++) {
        CHECK(nav.follow({"viewer" + std::to_string(i), {}})); nav.dispatch(host);
    }
    CHECK(nav.depth() == ViewerNavigation::MAX_DEPTH && !nav.follow({"overflow", {}}));
    CHECK(nav.back()); // pending Back is cancelled by closing the whole chain
    host.events.clear(); nav.clear(host);
    CHECK(host.events.size() == ViewerNavigation::MAX_DEPTH + 1 && host.events.front() == "close viewer7" &&
          host.events.back() == "close root" && host.state.empty() && !nav.pending());
    std::printf("Extras navigation: %d checks passed\n", checks);
    return 0;
}
