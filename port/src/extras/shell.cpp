// The extras' shell (extras.h, viewer.h): their loop while the game is
// paused (or not running), their ImGui context, the home screen, the viewer
// framework, status messages and XG_EXTRAS_SCRIPT.
//
// ImGui: a context of the extras' own, with Vulkan and SDL backends of its
// own, sharing the settings menu's font atlas when the menu opened them (the
// game's font and the skin's pictures live there: port/src/ui/skin.h keeps
// one set); with --extras, which never opens the menu, its own atlas, which
// skin::init fills. The menu's context is current again whenever the extras
// are not running code of their own.
//
// XG_EXTRAS_SCRIPT=file: the extras driven from a script, for headless runs.
// Each line is "<frame> <command> [argument]", <frame> counting the frames
// the extras have been open (all openings together):
//   key <SDL key name>   press and release a key (Down, Return, Escape...)
//   text <text>          type text
//   pad <button>         a gamepad button as ImGui sees it: up down left right
//                        a b x y l1 r1 start back
//   stick <x> <y>        hold the right stick (-1..1); 0 0 releases it
//   nav <label>          log controller focus and scroll position
//   mouse <x> <y>        move the mouse (window coordinates)
//   click <x> <y>        and click there
//   rclick <x> <y>       a right click there
//   wheel <n>            the mouse wheel, n steps (negative: down)
//   open <viewer id>     open a viewer (browser, ...)
//   home                 back to the home screen
//   back                 the viewer's Back, then the linked source or home
//   return               return directly to the linked source
//   do <cmd> [arg]       the open viewer's own command (viewer.h)
//   shot <name>          write the window's picture as <name>.png (into
//                        XG_DUMP_PRESENT, else XG_DUMP_FRAMES, else the
//                        working directory)
//   leave                leave the extras
// XG_EXTRAS_OPEN=<id>: open that viewer when the extras first open.

#include <SDL3/SDL.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <algorithm>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
#include <memory>

#include "imgui.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_vulkan.h"
#include "imgui_internal.h"
#include "picker.h"
#include "shell.h"
#include "ui/sfx.h"
#include "ui/l10n.h"
#include "ui/skin.h"
#include "view.h"
#include "replace.h"
extern "C" {
#include "gpu/portrait_pack.h"
#include "gpu/moviepack.h"
}
#include "viewer.h"
#include "library.h"
#include "bookmarks.h"
#include "bookmark_path.h"

extern "C" {
#include "cd/cd.h"
#include "extras.h"
#include "disc.h"
#include "gpu/gpu.h"
#include "preview.h"
#include "sandbox.h"
#include "system/hw.h"
#include "system/pad.h"
#include "system/settings.h"
#include "ui/ui.h"
void quitNow(void); // port/src/system/quit.c
}

#define IMAGE_COUNT 8

namespace extras {

namespace {

// The extras the home screen lists; a viewer registering the same id
// replaces its placeholder ("coming").
struct Planned {
    const char* id;
    const char* name;
    const char* blurb;
    int order;
};
const Planned planned[] = {
    {"browser", N_("Asset browser"), N_("Both discs' files: kinds, hex, LZSS, offset bundles, TIM pictures; export"), 10},
    {"visuals", N_("Visual library"), N_("Sprites, models, animations and battle previews"), 20},
    {"jukebox", N_("Jukebox"), N_("Music and sound effects"), 70},
    {"tables", N_("Game tables"), N_("The game's data tables: items, enemies, shops"), 80},
};

std::vector<const Viewer*>& registry() {
    static std::vector<const Viewer*> viewers; // made on first use: registrations run at static init
    return viewers;
}

struct HomeEntry {
    const char* id;
    const char* name;
    const char* blurb;
    int order;
    const Viewer* viewer; // null: coming
};

struct Step {
    unsigned long at;
    char cmd[16];
    char arg[256];
    bool done;
};

struct Release {
    unsigned long at;
    int kind; // 0 key (scancode), 1 ImGui gamepad key, 2 mouse button
    int code;
};

struct PaneNav {
    ImGuiID pane;
    ImGuiWindow* window;
};

struct PaneFrame { ImVec2 min, max; };

struct State {
    bool haveDevice, ready, open, leaving, quitAfter, rendered, fromMenu, openedOnce, escapeFree;
    UiVulkan dev;
    Device device;
    ImGuiContext* ctx;
    ImGuiContext* menuCtx; // current when the extras opened (the menu's), restored after
    VkFormat pipelineFormat;
    int unit;
    float pixelsPerUnit;
    float menuUnit;
    float scriptStickX = 0, scriptStickY = 0;
    bool scriptStick = false;
    int homeCursor = 0;
    const Viewer* viewer; // the open one, null: home
    char status[256];
    char statusLog[256]; // the same in English, for the logs (ui/l10n.h)
    bool statusError;
    uint64_t statusAt;
    unsigned long frames;
    const char* shot;
    char shotName[256];
    std::vector<Step> steps;
    bool scriptLoaded;
    std::vector<Release> releases;
    ImGuiID lastNav;
    ImGuiID leftFrom; // Left pressed on this item: still there next frame, it had nowhere to go
    bool navReset;    // the screen changed: the cursor on its first item
    ImGuiID focusPane; // a horizontal move with no item in the adjacent pane's direction
    std::vector<PaneNav> paneNav;
    std::vector<PaneFrame> paneFrames;
    ImGuiID commandWindow = 0;
    bool intoGame;    // left into the game (leaveIntoGame)
    ViewerNavigation navigation;
    std::unique_ptr<Bookmarks> bookmarks;
    std::unique_ptr<View> bookmarkPicture;
    std::string pictureKey;
    bool bookmarkSave = false, bookmarkOpen = false;
    std::string bookmarkLabel;
    Bookmark bookmarkTarget;
    // The focused view (viewer.h): drawn this frame / the last; the cursor
    // to put on its asset (or its bar); the list's row to take it back, for
    // this many frames, with the pointer shown when it was at the opening.
    bool focusNow = false, focusLast = false, focusInit = false, focusCursor = false, barFirst = false;
    int focusReturn = 0;
    int barLines = 1, barNeeded = 1; // the bar's rows of controls: drawn, and needed last frame
    // Stock / Enhanced (viewer.h): the choice per viewer (its id, or the visual library's
    // page), whether the open one offered it this frame / the last (the shortcut, the
    // footer), and a toggle asked for by the replacements key (F12) outside the frame.
    bool scripted = false;
    std::vector<std::pair<std::string, int>> looks;
    bool lookNow = false, lookLast = false, lookToggle = false;
    unsigned lookWanted = 0; // what the open viewer's look covers (viewer.h LOOK_* / XG_REPLACE_*)
};

State st;

// The open viewer's name for its look: the visual library's page, else the viewer's id.
std::string lookKey() {
    if (st.viewer == nullptr) return "";
    if (!strcmp(st.viewer->id, "visuals") && *visualFamily()) return visualFamily();
    return st.viewer->id;
}

const char* lookName(int l) {
    return l == LOOK_COMPARE ? N_("Compare") : l == LOOK_ENHANCED ? N_("Enhanced") : N_("Stock");
}

// A toggle (the bar's button, F12, Select): Stock and Enhanced in turn (Compare goes to
// Stock); Compare is chosen only in a viewer's options (lookOptions).
void lookStep() {
    int next = look(st.lookWanted) == LOOK_STOCK ? LOOK_ENHANCED : LOOK_STOCK;
    setLook(next);
    sound(1);
    status("%s", tr(next == LOOK_STOCK ? N_("Stock: the assets as on the disc")
                                       : N_("Enhanced: with the installed replacement packs")));
    logEvent("look: %s %s", lookKey().c_str(), next == LOOK_STOCK ? "stock" : "enhanced");
}

PFN_vkVoidFunction loadVk(const char* name, void* user) {
    const UiVulkan* d = (const UiVulkan*)user;
    PFN_vkVoidFunction f = ((PFN_vkGetInstanceProcAddr)d->getInstanceProcAddr)((VkInstance)d->instance, name);
    if (f == nullptr && strstr(name, "KHR") != nullptr) {
        f = (PFN_vkVoidFunction)+[]() { fprintf(stderr, "extras: a Vulkan function they never use was called\n"); };
    }
    return f;
}

void checkVk(VkResult r) {
    if (r != VK_SUCCESS) {
        fprintf(stderr, "extras: vulkan error %d\n", (int)r);
    }
}

std::vector<HomeEntry> homeEntries() {
    std::vector<HomeEntry> list;
    for (const Planned& p : planned) {
        list.push_back({p.id, p.name, p.blurb, p.order, nullptr});
    }
    for (const Viewer* v : registry()) {
        if (visualTarget({v->id, {}}).viewer != v->id) continue;
        bool replaced = false;
        for (HomeEntry& e : list) {
            if (strcmp(e.id, v->id) == 0) {
                e = {v->id, v->name, v->blurb, v->order, v};
                replaced = true;
            }
        }
        if (!replaced) {
            list.push_back({v->id, v->name, v->blurb, v->order, v});
        }
    }
    std::stable_sort(list.begin(), list.end(), [](const HomeEntry& a, const HomeEntry& b) { return a.order < b.order; });
    return list;
}

// ---- viewers ----------------------------------------------------------------------------------

const Viewer* findViewer(const std::string& id) {
    for (const Viewer* v : registry()) if (id == v->id) return v;
    return nullptr;
}

void resetViewerNav() {
    st.status[0] = '\0';
    st.navReset = true;
    st.focusPane = 0;
    st.paneNav.clear();
    st.leftFrom = 0;
    for (ImGuiWindow* w : ImGui::GetCurrentContext()->Windows) {
        w->Scroll = ImVec2(0, 0);
        w->ScrollTarget = ImVec2(FLT_MAX, FLT_MAX);
    }
}

class ShellNavigationHost : public NavigationHost {
public:
    bool exists(const std::string& id) override { return findViewer(id) != nullptr; }
    void open(const std::string& id) override {
        st.viewer = findViewer(id);
        logEvent("viewer: %s", id.c_str());
        resetViewerNav();
        if (st.viewer->opened) st.viewer->opened();
    }
    void suspend(const std::string& id) override {
        xgPreviewStop();
        const Viewer* v = findViewer(id);
        if (v->suspended != nullptr) v->suspended();
        st.viewer = nullptr;
    }
    void resume(const std::string& id) override {
        st.viewer = findViewer(id);
        logEvent("viewer: %s (back to it)", id.c_str());
        resetViewerNav();
        if (st.viewer->resumed != nullptr) st.viewer->resumed();
    }
    void close(const std::string& id) override {
        const Viewer* v = findViewer(id);
        if (v->closed) v->closed();
        if (st.viewer == v) st.viewer = nullptr;
        xgPreviewStop();
        resetViewerNav();
    }
    bool command(const ViewerCommand& cmd) override {
        return st.viewer->command != nullptr && st.viewer->command(cmd.name.c_str(), cmd.argument.c_str());
    }
    void failed(const std::string& message) override { error("%s", message.c_str()); }
};
ShellNavigationHost navigationHost;

void closeViewer() {
    st.navigation.clear(navigationHost);
    st.homeCursor = 0;
}

bool openViewer(const char* id) {
    ViewerTarget target = visualTarget({id, {}});
    for (const Viewer* v : registry()) {
        if (v->id == target.viewer) {
            closeViewer();
            navigationHost.open(target.viewer);
            st.navigation.root(target.viewer);
            for (const auto& command : target.commands) navigationHost.command(command);
            return true;
        }
    }
    for (const Planned& p : planned) {
        if (strcmp(p.id, id) == 0) {
            status("%s: coming in a later phase", p.name);
            return false;
        }
    }
    error("No viewer '%s'", id);
    return false;
}

Bookmarks& bookmarks() {
    if (!st.bookmarks) {
        const char* path = settingsPath();
        st.bookmarks.reset(new Bookmarks(path ? std::string(path) + ".bookmarks" : ""));
        std::string why;
        if (!st.bookmarks->load(why)) error("%s", why.c_str());
    }
    return *st.bookmarks;
}

bool sourceAvailable(const Bookmark& b, const std::string& path) {
    const char* why = nullptr; XgDisc* d = xgDiscOpen(path.c_str(), &why);
    if (!d) { error("Bookmark source unavailable. Use Home > Locate image: %s (%s)", path.c_str(), why); return false; }
    bool same = b.disc == xgDiscNumber(d) && b.region == xgDiscRegion(d) && b.boot == xgDiscBootName(d) &&
                b.fingerprint == xgDiscFingerprint(d);
    xgDiscClose(d);
    if (!same) error("Bookmark source identity differs: choose the original disc image.");
    return same;
}

void queueBookmark(size_t index) {
    const auto& items = bookmarks().items();
    if (index >= items.size()) { error("No such bookmark"); return; }
    st.bookmarkTarget = items[index]; st.bookmarkOpen = true;
}

void dispatchBookmark() {
    if (!st.bookmarkOpen) return;
    st.bookmarkOpen = false;
    Bookmark b = st.bookmarkTarget;
    if (!validBookmark(b) || !findViewer(visualTarget(b.target).viewer)) { error("Invalid bookmark target"); return; }
    if (!sourceAvailable(b, b.image)) return; // Keep the complete current navigation chain on failure.
    // Root replacement closes all suspended instances before opening a same
    // viewer or the Models/Animations alias, outside any viewer callback.
    ViewerTarget target = visualTarget(b.target);
    openViewer(target.viewer.c_str());
    for (const auto& c : target.commands) {
        if (!navigationHost.command(c)) {
            closeViewer(); error("Cannot restore %s: %s %s", b.label.c_str(), c.name.c_str(), c.argument.c_str()); return;
        }
    }
    status("Opened bookmark: %s", b.label.c_str());
}

void saveBookmark() {
    if (!st.bookmarkSave) return;
    st.bookmarkSave = false;
    Bookmark b;
    if (!st.viewer || !st.viewer->bookmark || !st.viewer->bookmark(b)) { error("Select an asset to bookmark"); return; }
    if (!st.bookmarkLabel.empty()) b.label = st.bookmarkLabel;
    else for (const auto& old : bookmarks().items()) if (old.key() == b.key()) { b.label = old.label; break; }
    size_t index = 0; std::string why;
    if (!bookmarks().put(b, index, why)) { error("%s", why.c_str()); return; }
    View* view = st.viewer->picture ? st.viewer->picture() : nullptr;
    std::vector<uint32_t> pixels;
    bool captured = st.viewer->thumbnail ? st.viewer->thumbnail(pixels) : view && view->thumbnail(pixels);
    if (captured && !bookmarks().picture(index, pixels, why)) {
        error("Bookmark saved; preview unavailable: %s", why.c_str()); return;
    }
    st.pictureKey.clear(); status("Saved bookmark %zu: %s", index, b.label.c_str());
}

void bookmarkScript(const char* arg) {
    char action[16] = "", value[240] = ""; sscanf(arg, "%15s %239[^\r\n]", action, value);
    std::string why;
    if (!strcmp(action, "add")) { st.bookmarkLabel = value; st.bookmarkSave = true; }
    else if (!strcmp(action, "open")) queueBookmark((size_t)strtoul(value, nullptr, 10));
    else if (!strcmp(action, "delete")) {
        if (!bookmarks().erase((size_t)strtoul(value, nullptr, 10), why)) error("%s", why.c_str());
        st.pictureKey.clear();
    } else if (!strcmp(action, "rename") || !strcmp(action, "locate")) {
        char* end = nullptr; size_t index = (size_t)strtoul(value, &end, 10);
        while (*end == ' ') ++end;
        if (!strcmp(action, "rename")) {
            if (!bookmarks().rename(index, end, why)) error("%s", why.c_str());
        } else if (index < bookmarks().items().size() && sourceAvailable(bookmarks().items()[index], end)) {
            if (!bookmarks().relocate(index, end, why)) error("%s", why.c_str());
        }
    } else if (!strcmp(action, "list")) {
        for (size_t i = 0; i < bookmarks().items().size(); ++i) {
            const Bookmark& b = bookmarks().items()[i];
            fprintf(stderr, "extras: bookmark %zu: %s | %s | %s disc %d | %s | %s\n", i, b.label.c_str(),
                    b.target.viewer.c_str(), b.region.c_str(), b.disc, b.key().c_str(), b.image.c_str());
        }
    } else error("Unknown bookmark command: %s", action);
}

void viewerBack() {
    if (st.navigation.pending()) return;
    if (st.viewer == nullptr) { st.leaving = true; return; }
    if (st.viewer->back != nullptr && st.viewer->back()) return;
    if (!st.navigation.back()) closeViewer();
}

// ---- XG_EXTRAS_SCRIPT -------------------------------------------------------------------------

void loadScript() {
    st.scriptLoaded = true;
    const char* path = getenv("XG_EXTRAS_SCRIPT");
    if (path == nullptr || path[0] == '\0') {
        return;
    }
    st.scripted = true; // the viewers start Stock: scripted pictures stay as they were (do look)
    FILE* f = fopen(path, "r");
    if (f == nullptr) {
        fprintf(stderr, "extras: cannot open XG_EXTRAS_SCRIPT %s\n", path);
        return;
    }
    char line[512];
    while (fgets(line, sizeof(line), f) != nullptr) {
        Step s = {};
        char when[24];
        int n = sscanf(line, "%23s %15s %255[^\r\n]", when, s.cmd, s.arg);
        if (n < 2 || when[0] == '#') {
            continue;
        }
        s.at = strtoul(when, nullptr, 10);
        st.steps.push_back(s);
    }
    fclose(f);
    fprintf(stderr, "extras: %d scripted steps from %s\n", (int)st.steps.size(), path);
}

void pushKey(SDL_Scancode code, bool down) {
    SDL_Event e = {};
    e.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
    e.key.timestamp = SDL_GetTicksNS();
    e.key.windowID = presentWindow() != nullptr ? SDL_GetWindowID(presentWindow()) : 0;
    e.key.scancode = code;
    e.key.key = SDL_GetKeyFromScancode(code, SDL_KMOD_NONE, false);
    e.key.down = down;
    SDL_PushEvent(&e);
}

void pushMouse(SDL_EventType type, float x, float y, float wheel) {
    SDL_Event e = {};
    e.type = type;
    Uint32 window = presentWindow() != nullptr ? SDL_GetWindowID(presentWindow()) : 0;
    if (type == SDL_EVENT_MOUSE_MOTION) {
        e.motion.timestamp = SDL_GetTicksNS();
        e.motion.windowID = window;
        e.motion.x = x;
        e.motion.y = y;
    } else if (type == SDL_EVENT_MOUSE_WHEEL) {
        e.wheel.timestamp = SDL_GetTicksNS();
        e.wheel.windowID = window;
        e.wheel.y = wheel;
        e.wheel.mouse_x = x;
        e.wheel.mouse_y = y;
    } else {
        e.button.timestamp = SDL_GetTicksNS();
        e.button.windowID = window;
        e.button.button = SDL_BUTTON_LEFT;
        e.button.down = type == SDL_EVENT_MOUSE_BUTTON_DOWN;
        e.button.clicks = 1;
        e.button.x = x;
        e.button.y = y;
    }
    SDL_PushEvent(&e);
}

ImGuiKey padKey(const char* name) {
    static const struct {
        const char* name;
        ImGuiKey key;
    } keys[] = {{"up", ImGuiKey_GamepadDpadUp},     {"down", ImGuiKey_GamepadDpadDown},
                {"left", ImGuiKey_GamepadDpadLeft}, {"right", ImGuiKey_GamepadDpadRight},
                {"a", ImGuiKey_GamepadFaceDown},    {"b", ImGuiKey_GamepadFaceRight},
                {"x", ImGuiKey_GamepadFaceLeft},    {"y", ImGuiKey_GamepadFaceUp},
                {"l1", ImGuiKey_GamepadL1},         {"r1", ImGuiKey_GamepadR1},
                {"l2", ImGuiKey_GamepadL2},         {"r2", ImGuiKey_GamepadR2},
                {"r3", ImGuiKey_GamepadR3},
                {"start", ImGuiKey_GamepadStart},   {"back", ImGuiKey_GamepadBack}};
    for (const auto& k : keys) {
        if (SDL_strcasecmp(name, k.name) == 0) {
            return k.key;
        }
    }
    return ImGuiKey_None;
}

void runStep(Step& s) {
    s.done = true;
    float x = 0, y = 0;
    if (strcmp(s.cmd, "key") == 0) {
        SDL_Scancode code = SDL_GetScancodeFromName(s.arg);
        if (code == SDL_SCANCODE_UNKNOWN) {
            fprintf(stderr, "extras: script: unknown key '%s'\n", s.arg);
            return;
        }
        pushKey(code, true);
        st.releases.push_back({s.at + 2, 0, (int)code});
    } else if (strcmp(s.cmd, "text") == 0) {
        SDL_Event e = {};
        e.type = SDL_EVENT_TEXT_INPUT;
        e.text.timestamp = SDL_GetTicksNS();
        e.text.windowID = presentWindow() != nullptr ? SDL_GetWindowID(presentWindow()) : 0;
        e.text.text = s.arg;
        SDL_PushEvent(&e);
    } else if (strcmp(s.cmd, "pad") == 0) {
        ImGuiKey key = padKey(s.arg);
        if (key == ImGuiKey_None) {
            fprintf(stderr, "extras: script: pad '%s'?\n", s.arg);
            return;
        }
        ImGui::GetIO().AddKeyEvent(key, true);
        st.releases.push_back({s.at + 2, 1, (int)key});
    } else if (strcmp(s.cmd, "stick") == 0) {
        st.scriptStick = true;
        sscanf(s.arg, "%f %f", &st.scriptStickX, &st.scriptStickY);
    } else if (strcmp(s.cmd, "nav") == 0) {
        auto& g = *ImGui::GetCurrentContext();
        fprintf(stderr, "extras: nav %s: %08x %s scroll %.0f %.0f\n", s.arg, g.NavId,
                g.NavWindow ? g.NavWindow->Name : "none", g.NavWindow ? g.NavWindow->Scroll.x : 0,
                g.NavWindow ? g.NavWindow->Scroll.y : 0);
    } else if (strcmp(s.cmd, "mouse") == 0 || strcmp(s.cmd, "click") == 0) {
        sscanf(s.arg, "%f %f", &x, &y);
        pushMouse(SDL_EVENT_MOUSE_MOTION, x, y, 0);
        if (s.cmd[0] == 'c') {
            pushMouse(SDL_EVENT_MOUSE_BUTTON_DOWN, x, y, 0);
            st.releases.push_back({s.at + 2, 2, 0});
        }
    } else if (strcmp(s.cmd, "rclick") == 0) {
        sscanf(s.arg, "%f %f", &x, &y);
        pushMouse(SDL_EVENT_MOUSE_MOTION, x, y, 0);
        for (int down = 1; down >= 0; down--) { // (ImGui takes the release a frame after the press)
            SDL_Event e = {};
            e.type = down ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
            e.button.timestamp = SDL_GetTicksNS();
            e.button.windowID = presentWindow() != nullptr ? SDL_GetWindowID(presentWindow()) : 0;
            e.button.button = SDL_BUTTON_RIGHT;
            e.button.down = down != 0;
            e.button.clicks = 1;
            e.button.x = x;
            e.button.y = y;
            SDL_PushEvent(&e);
        }
    } else if (strcmp(s.cmd, "wheel") == 0) {
        ImVec2 m = ImGui::GetIO().MousePos;
        pushMouse(SDL_EVENT_MOUSE_WHEEL, m.x / st.pixelsPerUnit, m.y / st.pixelsPerUnit, (float)atof(s.arg));
    } else if (strcmp(s.cmd, "open") == 0) {
        openViewer(s.arg);
    } else if (strcmp(s.cmd, "home") == 0) {
        closeViewer();
    } else if (strcmp(s.cmd, "bookmark") == 0) {
        bookmarkScript(s.arg);
    } else if (strcmp(s.cmd, "back") == 0) {
        viewerBack();
    } else if (strcmp(s.cmd, "return") == 0) {
        returnToSource();
    } else if (strcmp(s.cmd, "do") == 0) {
        char cmd[64] = "", arg[192] = "";
        sscanf(s.arg, "%63s %191[^\r\n]", cmd, arg);
        if (strcmp(cmd, "look") == 0 && st.viewer != nullptr) {
            // do look stock|enhanced|compare|settings: the open viewer's look (viewer.h);
            // settings: the first view an unscripted run gets (from the player's settings)
            static const char* const names[] = {"stock", "enhanced", "compare", "settings"};
            int l = -2;
            for (int i = 0; i < 4; i++) if (strcmp(arg, names[i]) == 0) l = i == 3 ? LOOK_SETTINGS : i;
            if (l == -2) fprintf(stderr, "extras: script: look %s: stock, enhanced, compare or settings\n", arg);
            else {
                setLook(l);
                logEvent("look: %s %s", lookKey().c_str(), arg);
            }
        } else if (st.viewer == nullptr || st.viewer->command == nullptr || !st.viewer->command(cmd, arg)) {
            fprintf(stderr, "extras: script: '%s %s' not taken (%s)\n", cmd, arg,
                    st.viewer != nullptr ? st.viewer->id : "home");
        }
    } else if (strcmp(s.cmd, "shot") == 0) {
        snprintf(st.shotName, sizeof(st.shotName), "%s", s.arg);
        st.shot = st.shotName;
    } else if (strcmp(s.cmd, "leave") == 0) {
        st.leaving = true;
    } else {
        fprintf(stderr, "extras: script: unknown command '%s'\n", s.cmd);
    }
}

void runScript() {
    for (size_t i = 0; i < st.releases.size();) {
        Release r = st.releases[i];
        if (r.at > st.frames) {
            i++;
            continue;
        }
        if (r.kind == 0) {
            pushKey((SDL_Scancode)r.code, false);
        } else if (r.kind == 1) {
            ImGui::GetIO().AddKeyEvent((ImGuiKey)r.code, false);
        } else {
            ImVec2 m = ImGui::GetIO().MousePos;
            pushMouse(SDL_EVENT_MOUSE_BUTTON_UP, m.x / st.pixelsPerUnit, m.y / st.pixelsPerUnit, 0);
        }
        st.releases.erase(st.releases.begin() + (long)i);
    }
    for (Step& s : st.steps) {
        if (!s.done && s.at <= st.frames) {
            runStep(s);
        }
    }
}

// ---- the game's buttons -------------------------------------------------------------------------

// As in the settings menu (port/src/ui/menu.cpp): the PS1 buttons through
// the player's key and gamepad bindings (pad.h): the D-pad moves, the game's
// confirm button chooses and its cancel button goes back (US Cross and
// Circle, JP Circle and Cross), L1 and R1 page, Start leaves. ImGui sees them
// as its gamepad keys (the SDL backend's own gamepad reading is off: it would
// take buttons by place, not by binding). Enter, Esc and the arrows keep
// their keyboard meanings.
struct NavButton {
    const char* button; // pad.h's name
    ImGuiKey key;
};
#define BUTTON_CHOOSE padConfirmButton()
#define BUTTON_BACK padCancelButton()
const NavButton navButtons[] = {
    {"up", ImGuiKey_GamepadDpadUp},       {"down", ImGuiKey_GamepadDpadDown},       {"left", ImGuiKey_GamepadDpadLeft},
    {"right", ImGuiKey_GamepadDpadRight}, {"confirm", ImGuiKey_GamepadFaceDown}, {"cancel", ImGuiKey_GamepadFaceRight},
    {"l1", ImGuiKey_GamepadL1},           {"r1", ImGuiKey_GamepadR1},               {"l2", ImGuiKey_GamepadL2},
    {"r2", ImGuiKey_GamepadR2},           {"start", ImGuiKey_GamepadStart},
    {"square", ImGuiKey_GamepadFaceLeft}, {"triangle", ImGuiKey_GamepadFaceUp}, {"r3", ImGuiKey_GamepadR3},
};
#define NAV_BUTTONS (int)IM_COUNTOF(navButtons)

bool keyHeld[SDL_SCANCODE_COUNT];
bool navDown[NAV_BUTTONS];

int padButtonIndex(const char* name) {
    if (!strcmp(name, "confirm")) name = padConfirmButton();
    if (!strcmp(name, "cancel")) name = padCancelButton();
    for (int i = 0; i < PAD_BUTTONS; i++) {
        if (strcmp(padButtonName(i), name) == 0) {
            return i;
        }
    }
    return -1;
}

// Keys ImGui reads itself as keyboard keys: never a game button too.
bool keyboardOwn(SDL_Scancode k) {
    switch (k) {
    case SDL_SCANCODE_UP: case SDL_SCANCODE_DOWN: case SDL_SCANCODE_LEFT: case SDL_SCANCODE_RIGHT:
    case SDL_SCANCODE_RETURN: case SDL_SCANCODE_KP_ENTER: case SDL_SCANCODE_ESCAPE: case SDL_SCANCODE_TAB:
    case SDL_SCANCODE_SPACE: case SDL_SCANCODE_H: case SDL_SCANCODE_BACKSPACE: case SDL_SCANCODE_DELETE: case SDL_SCANCODE_HOME:
    case SDL_SCANCODE_END: case SDL_SCANCODE_PAGEUP: case SDL_SCANCODE_PAGEDOWN:
        return true;
    default:
        return false;
    }
}

bool buttonHeld(int b, bool typing) {
    if (b < 0) {
        return false;
    }
    for (int slot = 0; slot < PAD_KEYS && !typing; slot++) {
        SDL_Scancode k = ::padKey(b, slot);
        if (k != SDL_SCANCODE_UNKNOWN && !keyboardOwn(k) && keyHeld[k]) {
            return true;
        }
    }
    int count = 0;
    SDL_JoystickID* ids = SDL_GetGamepads(&count);
    bool held = false;
    for (int i = 0; i < count && !held; i++) {
        held = padGamepadHeld(ids[i], b);
    }
    SDL_free(ids);
    return held;
}

// Each frame: the buttons' changes to ImGui and the right stick (the active
// viewer decides whether it rotates or pans). quiet: what is held counts as known (just opened).
void feedButtons(bool quiet) {
    ImGuiIO& io = ImGui::GetIO();
    io.BackendFlags |= ImGuiBackendFlags_HasGamepad;
    bool typing = io.WantTextInput;
    for (int i = 0; i < NAV_BUTTONS; i++) {
        bool down = buttonHeld(padButtonIndex(navButtons[i].button), typing);
        if (down != navDown[i] && !quiet) {
            io.AddKeyEvent(navButtons[i].key, down);
        }
        navDown[i] = down;
    }
    float sx = 0, sy = 0;
    int count = 0;
    SDL_JoystickID* ids = SDL_GetGamepads(&count);
    for (int i = 0; i < count; i++) {
        SDL_Gamepad* pad = SDL_GetGamepadFromID(ids[i]);
        float x = pad != nullptr ? SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_RIGHTX) / 32767.0f : 0.0f;
        float y = pad != nullptr ? SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_RIGHTY) / 32767.0f : 0.0f;
        sx = fabsf(x) > fabsf(sx) ? x : sx;
        sy = fabsf(y) > fabsf(sy) ? y : sy;
    }
    SDL_free(ids);
    if (st.scriptStick) { sx = st.scriptStickX; sy = st.scriptStickY; }
    sx = fabsf(sx) < 0.25f ? 0.0f : sx;
    sy = fabsf(sy) < 0.25f ? 0.0f : sy;
    io.AddKeyAnalogEvent(ImGuiKey_GamepadRStickLeft, sx < 0, sx < 0 ? -sx : 0.0f);
    io.AddKeyAnalogEvent(ImGuiKey_GamepadRStickRight, sx > 0, sx > 0 ? sx : 0.0f);
    io.AddKeyAnalogEvent(ImGuiKey_GamepadRStickUp, sy < 0, sy < 0 ? -sy : 0.0f);
    io.AddKeyAnalogEvent(ImGuiKey_GamepadRStickDown, sy > 0, sy > 0 ? sy : 0.0f);
}

void releaseButtons() {
    for (int i = 0; i < NAV_BUTTONS; i++) {
        if (navDown[i]) {
            ImGui::GetIO().AddKeyEvent(navButtons[i].key, false);
            navDown[i] = false;
        }
    }
}

// ---- input --------------------------------------------------------------------------------------

bool gamepadHeld() {
    int count = 0;
    SDL_JoystickID* ids = SDL_GetGamepads(&count);
    bool held = false;
    for (int i = 0; i < count && !held; i++) {
        SDL_Gamepad* pad = SDL_GetGamepadFromID(ids[i]);
        for (int b = 0; pad != nullptr && b < SDL_GAMEPAD_BUTTON_COUNT && !held; b++) {
            held = SDL_GetGamepadButton(pad, (SDL_GamepadButton)b);
        }
    }
    SDL_free(ids);
    return held;
}

} // namespace
} // namespace extras
// port/src/ui/diagnostics.cpp (weak: the extras build without it): the A/B key's binding.
extern "C" int diagnosticsReplacementsKey(int scancode, int mod) __attribute__((weak));
namespace extras {
namespace {

void pump() {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        padDeviceEvent(&e);
        switch (e.type) {
        case SDL_EVENT_QUIT:
            st.leaving = true;
            st.quitAfter = st.fromMenu; // with --extras, leaving is quitting
            break;
        case SDL_EVENT_KEY_UP:
            keyHeld[e.key.scancode] = false;
            ImGui_ImplSDL3_ProcessEvent(&e);
            break;
        case SDL_EVENT_KEY_DOWN:
            keyHeld[e.key.scancode] = true;
            if (e.key.scancode == SDL_SCANCODE_F1) {
                st.leaving |= !e.key.repeat;
                break;
            }
            // The replacements' A/B key (F12, replacements_ab_key): the open viewer's Stock /
            // Enhanced (viewer.h), while it offers one; the game's own A/B is not touched.
            if (!e.key.repeat && diagnosticsReplacementsKey != nullptr &&
                diagnosticsReplacementsKey((int)e.key.scancode, (int)e.key.mod)) {
                st.lookToggle = true;
                break;
            }
            if (e.key.scancode == SDL_SCANCODE_F11 ||
                ((e.key.scancode == SDL_SCANCODE_RETURN || e.key.scancode == SDL_SCANCODE_KP_ENTER) &&
                 (e.key.mod & SDL_KMOD_ALT))) {
                if (!e.key.repeat) {
                    presentToggleFullscreen();
                }
                break;
            }
            ImGui_ImplSDL3_ProcessEvent(&e);
            break;
        case SDL_EVENT_MOUSE_MOTION: {
            SDL_Event m = e; // into window pixels, as ImGui works
            m.motion.x *= st.pixelsPerUnit;
            m.motion.y *= st.pixelsPerUnit;
            ImGui_ImplSDL3_ProcessEvent(&m);
            break;
        }
        case SDL_EVENT_GAMEPAD_ADDED:
        case SDL_EVENT_GAMEPAD_REMOVED:
            ImGui_ImplSDL3_ProcessEvent(&e);
            break;
        case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
            if (e.gbutton.button == SDL_GAMEPAD_BUTTON_GUIDE) {
                st.leaving = true;
                break;
            }
            ImGui_ImplSDL3_ProcessEvent(&e);
            break;
        default:
            if (e.type >= SDL_EVENT_WINDOW_FIRST && e.type <= SDL_EVENT_WINDOW_LAST) {
                presentWindowEvent(&e);
            }
            ImGui_ImplSDL3_ProcessEvent(&e);
            break;
        }
    }
}

// ---- look ---------------------------------------------------------------------------------------

// Whole window pixels per game pixel, as the settings menu has them (its
// text size setting, skin.h unitFor).
void updateScale() {
    int pw = 0, ph = 0, lw = 0, lh = 0;
    SDL_GetWindowSizeInPixels(presentWindow(), &pw, &ph);
    SDL_GetWindowSize(presentWindow(), &lw, &lh);
    st.pixelsPerUnit = lw > 0 ? (float)pw / (float)lw : 1.0f;
    int s = skin::unitFor(pw, ph);
    if (s != st.unit || skin::unit() != (float)s) {
        st.unit = s;
        skin::setUnit((float)s);
    }
    // Native lists show their cursor, not a coloured row or focus rectangle.
    ImGuiStyle& style = ImGui::GetStyle();
    style.Colors[ImGuiCol_Header] = ImVec4(0, 0, 0, 0);
    style.Colors[ImGuiCol_HeaderHovered] = ImVec4(0, 0, 0, 0);
    style.Colors[ImGuiCol_HeaderActive] = ImVec4(0, 0, 0, 0);
    style.Colors[ImGuiCol_Button] = ImVec4(0, 0, 0, 0);
    style.Colors[ImGuiCol_PopupBg] = ImVec4(.204f, .204f, .204f, 1);
    style.Colors[ImGuiCol_CheckboxSelectedBg] = style.Colors[ImGuiCol_FrameBgActive];
    style.Colors[ImGuiCol_Tab] = style.Colors[ImGuiCol_TabSelected] =
        style.Colors[ImGuiCol_TabDimmed] = style.Colors[ImGuiCol_TabDimmedSelected] = ImVec4(0, 0, 0, 0);
    style.Colors[ImGuiCol_TabHovered] = style.Colors[ImGuiCol_FrameBgHovered];
    style.Colors[ImGuiCol_TabSelectedOverline] = style.Colors[ImGuiCol_TabDimmedSelectedOverline] =
        style.Colors[ImGuiCol_SliderGrab];
    style.ScrollbarSize = 0; // endPane draws the native track on the frame's left edge
}

bool begin() {
    if (st.ready) {
        return true;
    }
    if (presentWindow() == nullptr || !st.haveDevice) {
        fprintf(stderr, "extras: no window or no Vulkan device\n");
        return false;
    }
    ImGuiContext* prev = ImGui::GetCurrentContext();
    // The menu's atlas (its font and the skin's pictures) when it exists.
    ImFontAtlas* shared = prev != nullptr ? ImGui::GetIO().Fonts : nullptr;
    st.ctx = ImGui::CreateContext(shared);
    ImGui::SetCurrentContext(st.ctx);
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad;
    skin::init(); // with a shared atlas the menu did it already (it then returns at once)
    ImGui_ImplSDL3_InitForVulkan(presentWindow());
    ImGui_ImplSDL3_SetGamepadMode(ImGui_ImplSDL3_GamepadMode_Manual, nullptr, 0); // feedButtons reads them
    ImGui_ImplVulkan_LoadFunctions(VK_API_VERSION_1_3, loadVk, &st.dev);
    ImGui_ImplVulkan_InitInfo info = {};
    info.ApiVersion = VK_API_VERSION_1_3;
    info.Instance = (VkInstance)st.dev.instance;
    info.PhysicalDevice = (VkPhysicalDevice)st.dev.physical;
    info.Device = (VkDevice)st.dev.device;
    info.QueueFamily = st.dev.queueFamily;
    info.Queue = (VkQueue)st.dev.queue;
    info.DescriptorPoolSize = 128; // the atlas and every view's picture
    info.MinImageCount = 2;
    info.ImageCount = IMAGE_COUNT;
    info.UseDynamicRendering = true;
    info.CheckVkResultFn = checkVk;
    ImGui_ImplVulkan_Init(&info);
    ImGui::SetCurrentContext(prev);
    st.ready = true;
    return true;
}

// ---- a frame ------------------------------------------------------------------------------------

bool popupOpen() {
    return ImGui::IsPopupOpen((const char*)nullptr, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel);
}

ImVec2 at(ImVec2 o, float x, float y) {
    return ImVec2(o.x + x * st.unit, o.y + y * st.unit);
}

void drawHome(ImVec2 min, ImVec2 max) {
    float u = (float)st.unit;
    int& cursor = st.homeCursor;
    std::vector<HomeEntry> list = homeEntries();
    auto& store = bookmarks();
    const float listW = fminf(150 * u, (max.x - min.x) * .4f);
    ImVec2 listMax(min.x + listW, max.y);
    ImVec2 paneMin(min.x + listW + 16 * u, min.y);
    // The short command arc remains unboxed, as on the native Main screen.
    // Bookmarks share its scrolling region when the user's list grows.
    float commandTop = store.items().empty()
        ? fmaxf(32 * u, (max.y - min.y - (list.size() + 2) * 16 * u) * .75f) : 32 * u;
    ImGui::SetCursorScreenPos(ImVec2(min.x, min.y + commandTop));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(2 * u, 2 * u));
    if (ImGui::BeginChild("##home", ImVec2(listMax.x - min.x, listMax.y - min.y - commandTop),
                          ImGuiChildFlags_AlwaysUseWindowPadding | ImGuiChildFlags_NavFlattened,
                          ImGuiWindowFlags_NoScrollbar)) {
        st.commandWindow = ImGui::GetCurrentWindow()->ID;
        for (int i = 0; i < (int)list.size(); i++) {
            const HomeEntry& e = list[i];
            ImGui::PushID(i);
            float x = ImGui::GetCursorPosX();
            ImGui::SetCursorPosX(x + sinf(i * .55f) * 8 * u);
            const char* command = !strcmp(e.id, "browser") ? N_("Assets") : !strcmp(e.id, "visuals") ? N_("Visuals") :
                                  !strcmp(e.id, "tables") ? N_("Tables") : !strcmp(e.id, "scripts") ? N_("Scripts") : e.name;
            if (commandItem(command, cursor == i, ImGui::GetContentRegionAvail().x)) {
                cursor = i;
                if (e.viewer != nullptr) {
                    sound(2);
                    openViewer(e.id);
                } else {
                    sound(4);
                    status("%s: coming in a later phase", tr(e.name));
                }
            }
            ImGui::SetCursorPosX(x);
            if (ImGui::IsItemFocused()) {
                cursor = i;
            }
            ImGui::PopID();
        }
        ImGui::Dummy(ImVec2(0, 6 * u));
        if (!store.items().empty()) ImGui::TextUnformatted(tr("Bookmarks"));
        for (size_t i = 0; i < store.items().size(); ++i) {
            ImGui::PushID((int)(list.size() + i));
            if (listItem(store.items()[i].label.c_str(), cursor == (int)(list.size() + i)) || ImGui::IsItemFocused())
                cursor = (int)(list.size() + i);
            ImGui::PopID();
        }
        ImGui::Dummy(ImVec2(0, 4 * u));
        if (commandItem(st.fromMenu ? N_("Back") : N_("Quit"), false, ImGui::GetContentRegionAvail().x)) {
            st.leaving = true;
        }
    }
    ImGui::EndChild();
    ImGui::PopStyleVar();
    if (st.viewer != nullptr) {
        return; // opened just now: it draws from the next frame
    }
    cursor = cursor >= 0 && cursor < (int)(list.size() + store.items().size()) ? cursor : 0;
    if (cursor >= (int)list.size()) {
        size_t index = (size_t)cursor - list.size();
        // Copy: Rename/Delete can replace the store's vector in this frame.
        Bookmark b = store.items()[index];
        static char name[161] = ""; static std::string nameKey;
        if (nameKey != b.key()) { nameKey = b.key(); snprintf(name, sizeof(name), "%s", b.label.c_str()); }
        if (beginPane("##bookmark", paneMin, max, b.label.c_str(), true)) {
            ImGui::Text(tr("%s  %s disc %d"), b.target.viewer.c_str(), b.region.c_str(), b.disc);
            ImGui::TextWrapped("%s", b.boot.c_str());
            if (st.pictureKey != b.key()) {
                st.bookmarkPicture.reset(); st.pictureKey = b.key();
                std::vector<uint32_t> pixels;
                if (store.picture(index, pixels)) {
                    st.bookmarkPicture.reset(new View);
                    st.bookmarkPicture->setPixels(pixels, bookmarkWidth, bookmarkHeight);
                }
            }
            if (st.bookmarkPicture) {
                float w = fminf(160 * u, ImGui::GetContentRegionAvail().x);
                st.bookmarkPicture->draw(ImVec2(w, w * bookmarkHeight / bookmarkWidth));
            } else ImGui::TextWrapped(tr("No cached picture. The saved asset location is ready to open."));
            if (ImGui::Button(tr("Open bookmark"))) queueBookmark(index);
            ImGui::InputText(tr("Name"), name, sizeof(name));
            std::string why;
            if (ImGui::Button(tr("Rename"))) {
                if (!store.rename(index, name, why)) error("%s", why.c_str()); else status("Bookmark renamed");
            }
            ImGui::SameLine();
            if (ImGui::Button(tr("Delete"))) {
                if (!store.erase(index, why)) error("%s", why.c_str());
                else { cursor = 0; nameKey.clear(); st.pictureKey.clear(); status("Bookmark deleted"); }
            }
            ImGui::TextWrapped(tr("Source: %s"), b.image.c_str());
            if (ImGui::Button(tr("Locate image..."))) openPicker("bookmark-image", tr("Locate the original disc image"), b.image.c_str(), ".cue;.bin", false);
            std::string path;
            if (picker("bookmark-image", &path) && sourceAvailable(b, path)) {
                if (!store.relocate(index, path, why)) error("%s", why.c_str()); else status("Bookmark source updated");
            }
        }
        endPane(); return;
    }
    const HomeEntry& e = list[cursor];
    ImVec2 aboutMax(max.x, fminf(max.y, paneMin.y + 66 * u));
    if (beginPane("##about", paneMin, aboutMax, e.name, true)) {
        const char* about = !strcmp(e.id, "browser") ? N_("Browse and export the disc's files.") :
                            !strcmp(e.id, "visuals") ? N_("Sprites, Gears and animations.") :
                            !strcmp(e.id, "jukebox") ? N_("Music and sound effects.") :
                            !strcmp(e.id, "tables") ? N_("Names and data from the game.") :
                            !strcmp(e.id, "scripts") ? N_("Dialogue and actor routines.") : e.blurb;
        ImGui::TextWrapped("%s", tr(about));
        if (e.viewer == nullptr) {
            ImGui::PushStyleColor(ImGuiCol_Text, skin::textHint);
            ImGui::TextWrapped(tr("Coming in a later phase."));
            ImGui::PopStyleColor();
        }
    }
    endPane();
    ImVec2 exportMin(paneMin.x, aboutMax.y + 12 * u);
    if (beginPane("##exports", exportMin, max, "Exports", true)) {
        ImGui::PushStyleColor(ImGuiCol_Text, skin::textHint);
        ImGui::TextWrapped("%s", exportDir());
        ImGui::PopStyleColor();
        if (ImGui::Button(tr("Change..."))) {
            openPicker("export", tr("The export folder"), exportDir(), "", true);
        }
        ImGui::SameLine();
        if (ImGui::GetContentRegionAvail().x < ImGui::CalcTextSize(tr("Default")).x + 8 * u) ImGui::NewLine();
        if (ImGui::Button(tr("Default"))) {
            setExportDir("");
            status("Exports go to %s", exportDir());
        }
        ImGui::SameLine();
        if (ImGui::GetContentRegionAvail().x < ImGui::CalcTextSize(tr("Open folder")).x + 8 * u) ImGui::NewLine();
        if (ImGui::Button(tr("Open folder")) && !openExportDir()) {
            error("Cannot open %s", exportDir());
        }
        ImGui::Dummy(ImVec2(0, 8 * u));
        ImGui::PushStyleColor(ImGuiCol_Text, skin::textHint);
        ImGui::TextWrapped(tr("Save an asset with Bookmark in its Options menu."));
        ImGui::PopStyleColor();
    }
    endPane();
    std::string folder;
    if (picker("export", &folder)) {
        setExportDir(folder.c_str());
        status("Exports go to %s", exportDir());
    }
}

bool assetBrowser();

void drawFooter(ImVec2 min, ImVec2 max) {
    float u = (float)st.unit;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float x = min.x + 4 * u, y = min.y + 2 * u;
    struct Clip { ImDrawList* d; Clip(ImDrawList* d, ImVec2 a, ImVec2 b): d(d) { d->PushClipRect(a,b,true); } ~Clip() { d->PopClipRect(); } } clip(dl,min,max);
    uint64_t now = SDL_GetTicksNS();
    if (st.status[0] != '\0' && now - st.statusAt < 5000000000ull) {
        ImGui::PushStyleColor(ImGuiCol_Text, st.statusError ? skin::textError : skin::textWarning);
        ImGui::RenderTextEllipsis(dl, ImVec2(x, y), ImVec2(max.x - 4 * u, max.y), max.x - 4 * u,
                                  st.status, nullptr, nullptr);
        ImGui::PopStyleColor();
        return;
    }
    // The game's pad pictures keep the footer readable at its 320-pixel size.
    auto action = [&](const char* button, const char* label) {
        const char* glyph = skin::native() ? skin::actionGlyph(button) : padActionLabel(button);
        if (!glyph) glyph=button;
        else if (!skin::native()) glyph = tr(glyph); // pad.c's label in the menu's language (a runtime entry)
        dl->AddText(ImVec2(x,y),ImGui::GetColorU32(ImGuiCol_Text),glyph);
        x += ImGui::CalcTextSize(glyph).x + 4*u;
        label = tr(label);
        dl->AddText(ImVec2(x, y), ImGui::GetColorU32(ImGuiCol_Text), label);
        x += ImGui::CalcTextSize(label).x + 12 * u;
    };
    // A focused view (viewer.h): Back returns to its list; in a list,
    // confirm opens the asset there.
    if (!focusShown()) action(padConfirmButton(), assetBrowser() ? N_("View") : N_("Choose"));
    action(padCancelButton(), focusShown() ? N_("List") : N_("Back"));
    if (st.viewer && !strcmp(st.viewer->id, "visuals") && *visualFamily()) {
        const char* family = visualFamily();
        if (!strcmp(family, "maps")) {
            dl->AddText(ImVec2(x, y), ImGui::GetColorU32(ImGuiCol_Text), tr("RS camera"));
            x += 84 * u;
        } else action("square", N_("Play"));
        // The pages with tabs above their list (visuals.cpp's L1/R1).
        if (!strcmp(family, "gallery") || !strcmp(family, "maps") || !strcmp(family, "battlefx")) {
            const char* l1 = skin::native() ? skin::actionGlyph("l1") : padActionLabel("l1");
            dl->AddText(ImVec2(x, y), ImGui::GetColorU32(ImGuiCol_Text), l1 != nullptr ? l1 : "l1");
            x += ImGui::CalcTextSize(l1 != nullptr ? l1 : "l1").x + 2 * u;
            action("r1", N_("Tab"));
        }
    }
    if (lookOffered()) action("select", look(st.lookWanted) == LOOK_STOCK ? N_("Enhanced") : N_("Stock"));
    const char* help = tr("H help");
    float helpX = max.x - ImGui::CalcTextSize(help).x - 4 * u;
    if (helpX >= x) dl->AddText(ImVec2(helpX, y), ImGui::GetColorU32(ImGuiCol_Text), help);

}

ImGuiWindow* outerPane(ImGuiWindow* window) {
    if (window == nullptr || window == window->RootWindow) {
        return nullptr;
    }
    while (window->ParentWindow != window->RootWindow) {
        window = window->ParentWindow;
    }
    return window;
}

bool assetBrowser() {
    if (!st.viewer || strcmp(st.viewer->id, "visuals")) return false;
    const char* family = visualFamily();
    return !strcmp(family, "sprites") || !strcmp(family, "models");
}

// A page of the visual library is open (not its index).
bool librarySection() {
    return st.viewer && !strcmp(st.viewer->id, "visuals") && *visualFamily();
}

void moveBetweenPanes() {
    if (assetBrowser() && !popupOpen()) return;
    ImGuiContext& g = *ImGui::GetCurrentContext();
    // Gamepad navigation may clamp a long list row to the visible area and
    // choose that same row again. That still leaves the horizontal move stuck.
    bool stayed = g.NavMoveScoringItems && st.lastNav != 0 &&
                  g.NavMoveResultLocal.ID == st.lastNav && g.NavMoveResultOther.ID == 0;
    if ((!ImGui::NavMoveRequestButNoResultYet() && !stayed) ||
        (g.NavMoveDir != ImGuiDir_Left && g.NavMoveDir != ImGuiDir_Right) ||
        ImGui::IsAnyItemActive() || g.IO.WantTextInput || popupOpen()) {
        return;
    }
    ImGuiWindow* from = outerPane(g.NavWindow);
    if (from == nullptr) {
        return;
    }
    ImGuiWindow* next = nullptr;
    float nearest = FLT_MAX;
    for (ImGuiWindow* pane : from->ParentWindow->DC.ChildWindows) {
        if (pane == from || !pane->Active || pane->SkipItems ||
            !(pane->DC.NavLayersActiveMaskNext & (1 << ImGuiNavLayer_Main))) {
            continue;
        }
        float dx = pane->Rect().GetCenter().x - from->Rect().GetCenter().x;
        if ((g.NavMoveDir == ImGuiDir_Right ? dx <= 0 : dx >= 0) ||
            pane->Rect().Max.y <= from->Rect().Min.y || pane->Rect().Min.y >= from->Rect().Max.y) {
            continue;
        }
        if (fabsf(dx) < nearest) {
            next = pane;
            nearest = fabsf(dx);
        }
    }
    if (next != nullptr) {
        // Lists and controls can be far apart vertically. ImGui then has
        // no horizontal candidate, although the neighbouring pane is usable.
        st.focusPane = next->ID;
        st.leftFrom = 0;
        ImGui::NavMoveRequestCancel();
    }
}

void drawShell() {
    ImGuiIO& io = ImGui::GetIO();
    float u = (float)st.unit;
    bool pad = (io.BackendFlags & ImGuiBackendFlags_HasGamepad) != 0;

    // Back: Esc or the cancel button when nothing in the extras takes it;
    // Start opens asset options, or leaves other viewers.
    if (st.escapeFree && !pickerOpen() &&
        (ImGui::IsKeyPressed(ImGuiKey_Escape, false) || (pad && ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false)))) {
        sound(3);
        viewerBack();
    }
    // The right button goes back too, as in the settings menu: a click, not a
    // drag, that nothing took (a view pans and turns with it: it is active).
    if (st.escapeFree && !pickerOpen() && ImGui::IsMouseReleased(ImGuiMouseButton_Right) &&
        io.MouseDragMaxDistanceSqr[ImGuiMouseButton_Right] < io.MouseDragThreshold * io.MouseDragThreshold) {
        sound(3);
        viewerBack();
    }
    if (pad && !pickerOpen() && !popupOpen() && ImGui::IsKeyPressed(ImGuiKey_GamepadStart, false)) {
        if (assetBrowser()) st.viewer->command("options", "");
        else st.leaving = true;
    }
    // Stock / Enhanced: Select, or the replacements key (F12, extrasLookHotkey), while the
    // open viewer offers it (its bar or options showed the choice).
    if (st.lookToggle || (pad && lookOffered() && viewerShortcuts() && ImGui::IsKeyPressed(ImGuiKey_GamepadBack, false))) {
        if (lookOffered()) lookStep();
        st.lookToggle = false;
    }
    // Left with nowhere to go (the cursor did not move): the viewer's back,
    // as Left at a row's left end goes back in the settings menu. Not in a
    // visual library page: its list is at the left, and Left there took a
    // gamepad out of the gallery to the library's index (xg-htn.35.93);
    // Back and the header's Library leave such a page.
    ImGuiContext& g = *ImGui::GetCurrentContext();
    if (st.leftFrom != 0) {
        if (!librarySection() && g.NavId == st.leftFrom && st.viewer != nullptr && st.viewer->back != nullptr && !popupOpen() &&
            !ImGui::IsAnyItemActive() && st.viewer->back()) {
            sound(3);
        }
        st.leftFrom = 0;
    }
    if (!librarySection() && g.NavId != 0 && g.NavCursorVisible && !popupOpen() && !ImGui::IsAnyItemActive() && !io.WantTextInput &&
        (ImGui::IsKeyPressed(ImGuiKey_LeftArrow) || (pad && ImGui::IsKeyPressed(ImGuiKey_GamepadDpadLeft)))) {
        st.leftFrom = g.NavId;
    }

    // The game's picture behind, darkened (black with --extras).
    ImGui::GetBackgroundDrawList()->AddRectFilled(ImVec2(0, 0), io.DisplaySize, IM_COL32(0, 0, 8, 200));

    const int edge = 12, bar = 17, gap = 12;
    int cw = std::min((int)(io.DisplaySize.x / u), skin::textSize() == skin::TEXT_GAME ? 400 : 560);
    int ch = std::min((int)(io.DisplaySize.y / u), skin::textSize() == skin::TEXT_GAME ? 284 : 360);
    ImVec2 o(floorf((io.DisplaySize.x - cw * u) / 2), floorf((io.DisplaySize.y - ch * u) / 2));
    ImVec2 headMin = at(o, edge + 8, edge), headMax = at(o, cw - edge, edge + bar);
    ImVec2 footMin = at(o, edge, ch - edge - bar), footMax = at(o, cw - edge, ch - edge);
    ImVec2 areaMin = at(o, edge, edge + bar + gap), areaMax = at(o, cw - edge, ch - edge - bar - gap);

    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(io.DisplaySize);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::Begin("##extras", nullptr,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar |
                     ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoBackground |
                     ImGuiWindowFlags_NoBringToFrontOnFocus);
    ImGui::PopStyleVar();
    if (st.navReset) {
        st.navReset = false;
        ImGui::FocusWindow(ImGui::GetCurrentWindow());
        ImGui::NavInitWindow(ImGui::GetCurrentWindow(), true);
    }
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->PushClipRectFullScreen();
    char title[160];
    bool visual = st.viewer && !strcmp(st.viewer->id, "visuals");
    snprintf(title, sizeof(title), "%s", tr(visual ? visualTitle() : st.viewer ? st.viewer->name : N_("Extras")));
    dl->PopClipRect();
    float buttonX = headMax.x - 4 * u;
    // The header's buttons are never where the cursor starts on a new screen
    // (resetViewerNav): that is the viewer's first item, else these.
    ImGui::PushItemFlag(ImGuiItemFlags_NoNavDefaultFocus, true);
    if (visual && *visualFamily()) {
        if (assetBrowser()) {
            float width = ImGui::CalcTextSize(tr("Options")).x + 8 * u;
            buttonX -= width;
            ImGui::SetCursorScreenPos(ImVec2(buttonX, headMin.y + u));
            if (ImGui::Button(tr("Options"), ImVec2(width, 15 * u))) st.viewer->command("options", "");
        }
        float width = ImGui::CalcTextSize(tr("Library")).x + 8 * u;
        buttonX -= width + 8 * u;
        ImGui::SetCursorScreenPos(ImVec2(buttonX, headMin.y + u));
        if (ImGui::Button(tr("Library"), ImVec2(width, 15 * u))) { sound(3); st.viewer->command("index", ""); }
    }
    if (findViewer(st.navigation.source())) {
        std::string label = tr("Return");
        float width = ImGui::CalcTextSize(label.c_str()).x + 12 * u;
        buttonX -= width;
        ImGui::SetCursorScreenPos(ImVec2(buttonX, headMin.y + u));
        if (ImGui::Button(label.c_str(), ImVec2(width, 15 * u))) { sound(3); returnToSource(); }
    }
    ImGui::PopItemFlag();
    // The shop's heading is a small label window, independent of its list.
    ImVec2 titleMax(fminf(buttonX - 8 * u, headMin.x + ImGui::CalcTextSize(title).x + 12 * u), headMax.y);
    dl->PushClipRectFullScreen();
    skin::box(dl, headMin, titleMax);
    dl->PopClipRect();
    if (st.viewer) dl->PushClipRect(headMin, ImVec2(buttonX - 4 * u, headMax.y), false);
    ImGui::RenderTextEllipsis(dl, ImVec2(headMin.x + 6 * u, headMin.y + 2 * u),
                              ImVec2(titleMax.x - 4 * u, headMax.y), titleMax.x - 4 * u, title, nullptr, nullptr);
    if (st.viewer) dl->PopClipRect();
    drawFooter(footMin, footMax);

    if (st.viewer != nullptr) {
        Area area = {areaMin, areaMax, u};
        ImGui::PushID(st.viewer->id); // suspended viewers keep distinct tabs and scroll positions
        st.viewer->draw(area);
        ImGui::PopID();
    } else {
        drawHome(areaMin, areaMax);
    }
    moveBetweenPanes();
    static bool helpRequested = false;
    if (viewerShortcuts() && ImGui::IsKeyPressed(ImGuiKey_H, false)) helpRequested = true;
    if (libraryOptions("extras-help", helpRequested, 336, 216)) {
        ImGui::TextUnformatted(tr("Extras controls"));
        ImGui::Spacing();
        ImGui::TextWrapped(tr("Enter / confirm: open the asset full size. Esc / cancel: back to the list, then out. F1: return to the game menu."));
        ImGui::TextWrapped(tr("Up/Down or L1/R1: character. Left/Right: animation. Square or Space: play/pause. Triangle: one frame. Start: Options."));
        ImGui::TextWrapped(tr("Gallery, Map viewer, effects: L1/R1 change the tabs above the list."));
        ImGui::TextWrapped(tr("Select or F12: Stock / Enhanced, the assets as on the disc or with the installed replacement packs (in the viewers that offer it)."));
        ImGui::TextWrapped(tr("Right stick: camera. L2/R2: zoom (arena in Battle scene). R3: fit the picture."));
        ImGui::TextWrapped(tr("Mouse: choose a row, drag to move the picture, wheel to zoom. Drag the red track to scroll a list."));
        if (ImGui::Button(tr("Close"))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    ImGui::End();
}

// The game's menu sounds for what the cursor did this frame.
void sounds() {
    ImGuiContext& g = *ImGui::GetCurrentContext();
    if (g.NavId != st.lastNav && g.NavId != 0 && st.lastNav != 0 && g.NavCursorVisible) {
        sound(1);
    }
    st.lastNav = g.NavId;
}

void frame() {
    extrasJukeboxTick();
    updateScale();
    viewsBeginFrame();
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    int pw = 0, ph = 0;
    SDL_GetWindowSizeInPixels(presentWindow(), &pw, &ph);
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2((float)pw, (float)ph);
    io.DisplayFramebufferScale = ImVec2(1.0f, 1.0f);
    feedButtons(st.frames == 0 || !st.open);
    ImGui::NewFrame();
    skin::beginFrame();
    st.focusLast = st.focusNow;
    st.focusNow = false;
    st.lookLast = st.lookNow;
    st.lookNow = false;
    drawShell();
    sounds();
    if (st.focusReturn > 0) st.focusReturn--;
    // The pointer at the cursor, unless its window is gone (a screen left).
    ImGuiWindow* nav = ImGui::GetCurrentContext()->NavWindow;
    skin::endFrame(nav != nullptr && !nav->Active ? nav->ID : st.commandWindow);
    ImGui::Render();
    viewsEndFrame();
    saveBookmark();
    st.escapeFree = !popupOpen() && !ImGui::IsAnyItemActive() && !io.WantTextInput;
    st.rendered = true;
}

void run(bool fromMenu) {
    if (st.open || !begin()) {
        return;
    }
    st.menuCtx = ImGui::GetCurrentContext();
    st.menuUnit = skin::unit();
    ImGui::SetCurrentContext(st.ctx);
    st.fromMenu = fromMenu;
    st.open = true;
    st.leaving = st.quitAfter = false;
    st.escapeFree = false;
    st.lastNav = 0;
    st.homeCursor = 0;
    resetViewerNav();
    if (!fromMenu) {
        hwPause(1); // no game: no clock either (the sandbox's safe points stay still)
    }
    if (!st.scriptLoaded) {
        loadScript();
    }
    sfx::opened(); // the menu's sounds (made from the disc the first time)
    fprintf(stderr, "extras: open (%s)\n", fromMenu ? "from the menu, the game paused" : "--extras");
    {
        int pw = 0, ph = 0;
        SDL_GetWindowSizeInPixels(presentWindow(), &pw, &ph);
        logEvent("open (%s); drive %s; other disc %s; window %dx%d pixels", fromMenu ? "F1 menu" : "--extras",
                 cdSwapPath(0) != nullptr ? cdSwapPath(0) : "-", cdSwapPath(1) != nullptr ? cdSwapPath(1) : "-", pw, ph);
    }
    if (!st.openedOnce) {
        st.openedOnce = true;
        const char* first = getenv("XG_EXTRAS_OPEN");
        if (first != nullptr && first[0] != '\0') {
            openViewer(first);
        }
    }
    ImGui::SetNavCursorVisible(true);
    {
        int n = 0;
        const bool* keys = SDL_GetKeyboardState(&n);
        for (int i = 0; i < n && i < SDL_SCANCODE_COUNT; i++) {
            keyHeld[i] = keys[i];
        }
    }
    feedButtons(true); // what opened the extras is no press in them
    uint64_t next = SDL_GetTicksNS();
    bool viaPad = false;
    while (!st.leaving || (viaPad = gamepadHeld())) {
        pump();
        if (!st.leaving) {
            st.navigation.dispatch(navigationHost);
            dispatchBookmark();
            runScript();
        }
        frame();
        gpuPresentMenu(st.shot);
        st.shot = nullptr;
        st.rendered = false;
        st.frames++;
        // The stall watchdog and quit requests see the menu's context current.
        ImGui::SetCurrentContext(st.menuCtx);
        hwPausedFrame();
        ImGui::SetCurrentContext(st.ctx);
        uint64_t now = SDL_GetTicksNS();
        next += (uint64_t)(hwVSyncPeriod() * 1e9);
        if (next > now) {
            SDL_DelayNS(next - now);
        } else {
            next = now;
        }
        if (viaPad && st.frames > 600) {
            break;
        }
    }
    closeViewer();
    if (!fromMenu || st.intoGame) extrasJukeboxEnd();
    st.bookmarkSave = st.bookmarkOpen = false;
    releaseButtons();
    st.scriptStick = false;
    SDL_StopTextInput(presentWindow());
    SDL_CaptureMouse(false);
    SDL_SetCursor(SDL_GetDefaultCursor());
    settingsSave();
    ImGui::SetCurrentContext(st.menuCtx);
    if (st.menuCtx != nullptr) {
        skin::setUnit(st.menuUnit); // the menu's style and the skin's unit as it had them
    }
    if (!fromMenu) {
        hwPause(0);
    }
    st.open = false;
    fprintf(stderr, "extras: closed\n");
    logEvent("closed%s", st.intoGame ? " (into the game)" : "");
    if (st.quitAfter) {
        quitNow();
    }
}

} // namespace

// ---- viewer.h ------------------------------------------------------------------------------------

ViewerRegistration::ViewerRegistration(const Viewer& v) {
    registry().push_back(&v);
}

float unit() {
    return st.unit > 0 ? (float)st.unit : 1.0f;
}

void resetViewerPosition() { resetViewerNav(); }

static void message(bool isError, const char* fmt, va_list ap) {
    // On screen in the menu's language (ui/l10n.h); in the logs in English,
    // whatever the language (scripts and reports read them).
    va_list again;
    va_copy(again, ap);
    vsnprintf(st.statusLog, sizeof(st.statusLog), fmt, again);
    va_end(again);
    vsnprintf(st.status, sizeof(st.status), tr(fmt), ap);
    st.statusError = isError;
    st.statusAt = SDL_GetTicksNS();
    fprintf(stderr, "extras: %s\n", st.statusLog);
}

void status(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    message(false, fmt, ap);
    va_end(ap);
}

void error(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    message(true, fmt, ap);
    va_end(ap);
    sound(4);
    logEvent("error: %s", st.statusLog);
}

// extras.log, in settings.ini's folder (the pref path: ~/.local/share/xenogears
// on Linux; none with XG_SETTINGS=none): one dated line per event, never per
// frame. The extras opening and leaving (with the drive's images and the
// window), each viewer and library section opened, error messages, and what
// viewers report (the gallery: the tab shown, its rows, the disc scan). Over
// 64 KB at a run's first line, it becomes extras.log.1 (the older one goes).
void logEvent(const char* fmt, ...) {
    static int state; // 0 first line of the run, 1 writing, -1 no log
    static std::filesystem::path path;
    static std::string last;
    static uint64_t lastAt;
    if (state < 0) {
        return;
    }
    char line[768];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);
    uint64_t at = SDL_GetTicks();
    if (last == line && at - lastAt < 2000) {
        return; // the same line again at once: something repeating each frame
    }
    last = line;
    lastAt = at;
    if (state == 0) {
        const char* settings = settingsPath();
        if (settings == nullptr) {
            state = -1;
            return;
        }
        path = bookmarkPath(settings).parent_path() / "extras.log";
        std::error_code ec;
        uintmax_t size = std::filesystem::file_size(path, ec);
        if (!ec && size > 64 * 1024) {
            std::filesystem::path old = path;
            old += ".1";
            std::filesystem::remove(old, ec);
            std::filesystem::rename(path, old, ec);
        }
        state = 1;
    }
    std::time_t now = std::time(nullptr);
    char when[32] = "";
    if (const std::tm* tm = std::localtime(&now)) {
        std::strftime(when, sizeof(when), "%Y-%m-%d %H:%M:%S", tm);
    }
    std::ofstream f(path, std::ios::app);
    if (f) {
        f << when << ' ' << line << '\n';
    }
}

bool beginPane(const char* id, ImVec2 min, ImVec2 max, const char* title, bool scroll) {
    float u = unit();
    st.paneFrames.push_back({min, max});
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->PushClipRectFullScreen();
    skin::box(dl, min, max);
    dl->PopClipRect();
    ImVec2 inner(min.x + 1 * u, min.y + 2 * u);
    if (title != nullptr) {
        dl->AddText(ImVec2(min.x + 6 * u, min.y + 3 * u), ImGui::GetColorU32(ImGuiCol_Text), tr(title));
        inner.y += 18 * u;
    }
    ImGui::SetCursorScreenPos(inner);
    ImVec2 size(fmaxf(max.x - inner.x - 3 * u, 8), fmaxf(max.y - inner.y - 2 * u, 8));
    // Room on the left for the list pointer (skin::endFrame puts it 10 game
    // pixels left of the item), as the menu's panes have.
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(11 * u, 2 * u));
    bool open = ImGui::BeginChild(id, size, ImGuiChildFlags_AlwaysUseWindowPadding | ImGuiChildFlags_NavFlattened,
                                  ImGuiWindowFlags_NoScrollbar | (scroll ? 0 : ImGuiWindowFlags_NoScrollWithMouse));
    ImGui::PopStyleVar();
    ImGuiWindow* pane = ImGui::GetCurrentWindow();
    if (open && pane->ID == st.focusPane) {
        ImGuiWindow* focus = pane;
        for (const PaneNav& saved : st.paneNav) {
            if (saved.pane == pane->ID && saved.window->WasActive) {
                focus = saved.window;
                break;
            }
        }
        ImGui::FocusWindow(focus);
        ImGui::NavInitWindow(focus, false);
        ImGui::SetNavCursorVisible(true);
        st.focusPane = 0;
    }
    return open;
}

void endPane() {
    ImGuiWindow* pane = ImGui::GetCurrentWindow();
    ImGuiWindow* nav = ImGui::GetCurrentContext()->NavWindow;
    if (outerPane(nav) == pane) {
        auto saved = std::find_if(st.paneNav.begin(), st.paneNav.end(),
                                  [pane](const PaneNav& p) { return p.pane == pane->ID; });
        if (saved == st.paneNav.end()) {
            st.paneNav.push_back({pane->ID, nav});
        } else {
            saved->window = nav;
        }
    }
    // A list can live in a nested child (NPCs, songs, tables). Its native
    // track belongs to the surrounding frame, rather than its text region.
    ImGuiWindow* scrolling = pane->ScrollMax.y > 0 ? pane : nullptr;
    for (ImGuiWindow* child : ImGui::GetCurrentContext()->Windows) {
        if (child->Active && child->ScrollMax.y > 0 && ImGui::IsWindowChildOf(child, pane, false, false))
            if (!scrolling || child->ScrollMax.y > scrolling->ScrollMax.y) scrolling = child;
    }
    PaneFrame frame = st.paneFrames.back();
    st.paneFrames.pop_back();
    ImGui::EndChild();
    if (scrolling) {
        float u = unit(), height = frame.max.y - frame.min.y, inner = height - 16 * u;
        float total = scrolling->ContentSize.y + 2 * scrolling->WindowPadding.y;
        float thumb = floorf(fmaxf(8 * u, inner * fminf(1.f, scrolling->Size.y / fmaxf(1.f, total))) / u) * u;
        float top = floorf((inner - thumb) * scrolling->Scroll.y / scrolling->ScrollMax.y / u) * u;
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->PushClipRectFullScreen();
        skin::scrollBar(dl, frame.min, height, top, thumb);
        dl->PopClipRect();
        ImVec2 cursor = ImGui::GetCursorScreenPos();
        ImGui::SetCursorScreenPos(frame.min);
        ImGui::PushID(pane->ID);
        ImGui::PushItemFlag(ImGuiItemFlags_NoNav, true);
        ImGui::InvisibleButton("##scroll", ImVec2(8 * u, height), ImGuiButtonFlags_FlattenChildren);
        ImGui::PopItemFlag();
        if (ImGui::IsItemActive()) {
            float at = (ImGui::GetIO().MousePos.y - frame.min.y - 8 * u - thumb / 2) / fmaxf(1.f, inner - thumb);
            ImGui::SetScrollY(scrolling, std::clamp(at, 0.f, 1.f) * scrolling->ScrollMax.y);
        }
        ImGui::PopID();
        ImGui::SetCursorScreenPos(cursor);
        ImGui::Dummy(ImVec2(0, 0));
    }
}

bool fromGame() {
    return st.fromMenu;
}

bool follow(const ViewerTarget& target) {
    ViewerTarget resolved = visualTarget(target);
    if (st.viewer && !strcmp(st.viewer->id, "visuals") && resolved.viewer == "visuals")
        return visualFollow(resolved);
    return st.viewer != nullptr && st.navigation.current() == st.viewer->id && st.navigation.follow(resolved);
}

const Viewer* registeredViewer(const char* id) { return findViewer(id); }
bool viewerShortcuts() {
    return !pickerOpen() && !popupOpen() && !ImGui::IsAnyItemActive() && !ImGui::GetIO().WantTextInput;
}

bool returnToSource() {
    if (st.viewer && !strcmp(st.viewer->id, "visuals") && visualReturn()) return true;
    return st.navigation.back();
}

void leaveIntoGame() {
    st.intoGame = true;
    st.leaving = true;
}

bool listItem(const char* label, bool selected, bool hint, bool drawLabel) {
    if (hint) {
        ImGui::PushStyleColor(ImGuiCol_Text, skin::textHint);
    }
    ImVec2 at = ImGui::GetCursorScreenPos();
    float width = ImGui::GetContentRegionAvail().x;
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0, 0, 0, 0));
    bool chosen = ImGui::Selectable(label, selected);
    ImGui::PopStyleColor();
    if (selected && focusReturning()) focusRestoreHere();
    if (drawLabel) ImGui::RenderTextEllipsis(ImGui::GetWindowDrawList(), at,
                              ImVec2(at.x + width, at.y + ImGui::GetTextLineHeight()), at.x + width,
                              label, ImGui::FindRenderedTextEnd(label), nullptr);
    if (hint) {
        ImGui::PopStyleColor();
    }
    return chosen;
}

bool commandItem(const char* label, bool selected, float width) {
    float u = unit();
    ImVec2 at = ImGui::GetCursorScreenPos();
    ImGui::PushID(label);
    bool chosen = ImGui::Selectable("##command", false, 0, ImVec2(width, 13 * u));
    ImGui::PopID();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->PushClipRectFullScreen();
    if (ImGui::IsItemFocused() && ImGui::GetCurrentContext()->NavCursorVisible)
        skin::sprite(dl, skin::SPRITE_ARROW, ImVec2(at.x + 9 * u, at.y + 5 * u));
    dl->PopClipRect();
    skin::spriteAt(dl, selected ? skin::SPRITE_BALL_RED : skin::SPRITE_BALL, at);
    dl->AddText(ImVec2(at.x + 20 * u, at.y + 2 * u), ImGui::GetColorU32(ImGuiCol_Text), tr(label));
    return chosen;
}

bool commandPopup(const char* id) {
    if (!ImGui::BeginPopup(id)) return false;
    skin::frameWindow();
    return true;
}

void bookmarkSelection() {
    st.bookmarkLabel.clear();
    st.bookmarkSave = true;
}

void sound(int s) {
    sfx::play((sfx::Sound)s);
}

float lineHeight() {
    return ImGui::GetTextLineHeight();
}

unsigned long frameNumber() {
    return st.frames;
}

// ---- the focused view (viewer.h) -------------------------------------------------------------------

FocusLayout focusLayout(const Area& a, int lines) {
    float u = a.unit;
    lines = std::max(std::max(1, lines), st.barNeeded);
    float bar = lines * ImGui::GetFrameHeight() + (lines - 1) * ImGui::GetStyle().ItemSpacing.y + 8 * u;
    FocusLayout f;
    f.barMax = a.max;
    f.barMin = ImVec2(a.min.x, floorf(a.max.y - bar));
    f.min = a.min;
    f.max = ImVec2(a.max.x, f.barMin.y - 8 * u);
    st.focusNow = true;
    return f;
}

void focusEnter(bool placeCursor) {
    st.focusInit = placeCursor;
    st.focusReturn = 0;
    st.barNeeded = 1;
    st.focusCursor = ImGui::GetCurrentContext()->NavCursorVisible;
    sound(2);
}

void focusLeave() {
    st.focusInit = false;
    st.focusReturn = 2; // the frame Back came in, and the list's next one
}

bool focusShown() {
    return st.focusNow || st.focusLast;
}

bool focusReturning() {
    return st.focusReturn > 0;
}

// The cursor on the item just submitted, without scrolling its window.
static void cursorHere(bool visible) {
    ImGuiContext& g = *ImGui::GetCurrentContext();
    ImGuiID id = g.LastItemData.ID;
    if (id == 0) return;
    ImGui::SetFocusID(id, g.CurrentWindow);
    ImGui::SetNavCursorVisible(visible);
}

void focusRestoreHere() {
    if (st.focusReturn <= 0 || popupOpen()) return;
    cursorHere(st.focusCursor);
    st.focusReturn = 0;
}

void focusPicture(const char* caption) {
    ImGuiContext& g = *ImGui::GetCurrentContext();
    if (st.focusInit) {
        st.focusInit = false;
        cursorHere(false);
    }
    // No list pointer over the picture: it shows again on the bar's controls.
    if (g.NavId != 0 && g.NavId == g.LastItemData.ID) g.NavCursorVisible = false;
    if (caption == nullptr || !*caption) return;
    ImVec2 min = ImGui::GetItemRectMin(), max = ImGui::GetItemRectMax();
    float pad = 4.0f * unit();
    ImVec2 at(min.x + 3 * pad, min.y + 2 * pad);
    ImVec2 size = ImGui::CalcTextSize(caption, nullptr, false, max.x - at.x - 3 * pad);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->PushClipRect(min, max, true);
    dl->AddRectFilled(ImVec2(at.x - pad, at.y - pad), ImVec2(at.x + size.x + pad, at.y + size.y + pad),
                      IM_COL32(24, 26, 40, 230));
    dl->AddText(ImGui::GetFont(), ImGui::GetFontSize(), at, ImGui::GetColorU32(ImGuiCol_Text), caption, nullptr,
                max.x - at.x - 3 * pad);
    dl->PopClipRect();
}

bool beginFocusBar(const FocusLayout& f) {
    st.focusNow = true;
    beginPane("##focusbar", f.barMin, f.barMax, nullptr, false);
    bool back = ImGui::Button(tr("Back"));
    if (st.focusInit) { // no picture took the cursor: Back has it
        st.focusInit = false;
        cursorHere(st.focusCursor);
    }
    st.barFirst = false;
    st.barLines = 1;
    if (back) sound(3);
    return back;
}

void endFocusBar() {
    st.barNeeded = st.barLines; // the next frame's layout makes room for them
    endPane();
}

// The next bar item on this line when `width` still fits, else on the next.
static void barFlow(float width) {
    if (st.barFirst) {
        st.barFirst = false;
        return;
    }
    float right = ImGui::GetItemRectMax().x + ImGui::GetStyle().ItemSpacing.x + width;
    if (right <= ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x) ImGui::SameLine();
    else st.barLines++;
}

void focusSpace(float width) {
    barFlow(width);
}

bool focusButton(const char* label, bool enabled) {
    label = tr(label);
    barFlow(ImGui::CalcTextSize(label, nullptr, true).x + ImGui::GetStyle().FramePadding.x * 2);
    ImGui::BeginDisabled(!enabled);
    bool pressed = ImGui::Button(label);
    ImGui::EndDisabled();
    return pressed;
}

bool focusCheckbox(const char* label, bool* v) {
    label = tr(label);
    barFlow(ImGui::CalcTextSize(label, nullptr, true).x + ImGui::GetFrameHeight() + ImGui::GetStyle().ItemInnerSpacing.x);
    return ImGui::Checkbox(label, v);
}

void focusText(const char* fmt, ...) {
    char text[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(text, sizeof(text), tr(fmt), ap);
    va_end(ap);
    barFlow(ImGui::CalcTextSize(text).x);
    // On the buttons' baseline.
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(text);
}

int focusZoomButtons(bool oneToOne) {
    int chosen = 0;
    if (focusButton(N_("Fit"))) chosen = 1;
    if (focusButton(N_("Zoom -"))) chosen = 2;
    if (focusButton(N_("Zoom +"))) chosen = 3;
    if (oneToOne && focusButton("1:1")) chosen = 4;
    return chosen;
}

// ---- Stock / Enhanced ----------------------------------------------------------------------------

// What the player's settings show in the game now, of what a viewer covers.
static unsigned lookChosen(unsigned wanted) {
    unsigned on = xgReplaceChosen();
    if ((wanted & LOOK_PORTRAITS) && portraitPackEnabled() && strcmp(portraitPackSelected(), "original") != 0)
        on |= LOOK_PORTRAITS;
    if ((wanted & LOOK_MOVIES) && moviePackEnabled()) on |= LOOK_MOVIES;
    return on & wanted;
}

int look(unsigned wanted) {
    std::string key = lookKey();
    int chosen = st.scripted ? LOOK_STOCK : LOOK_SETTINGS;
    for (auto& l : st.looks)
        if (l.first == key) chosen = l.second;
    if (chosen != LOOK_SETTINGS) return chosen;
    // the first view: as the game draws it with the player's settings (scripts: Stock)
    return lookChosen(wanted) ? LOOK_ENHANCED : LOOK_STOCK;
}

void setLook(int l) {
    std::string key = lookKey();
    if (key.empty()) return;
    for (auto& e : st.looks)
        if (e.first == key) {
            e.second = l;
            return;
        }
    st.looks.push_back({key, l});
}

unsigned lookLibraries(unsigned wanted) {
    if (look(wanted) == LOOK_STOCK) return 0u;
    // the packs the player's settings draw; all installed ones when the settings draw none
    unsigned chosen = lookChosen(wanted) & XG_LOOK_LIBRARIES;
    return chosen ? chosen : wanted & xgReplaceInstalled();
}

bool lookInstalled(unsigned wanted) {
    return (wanted & xgReplaceInstalled()) != 0;
}

int lookCovered(const View* v, unsigned wanted) {
    if (!lookInstalled(wanted)) return LOOK_NO_PACK;
    if (v == nullptr || look(wanted) == LOOK_STOCK) return LOOK_UNKNOWN;
    if (v->replaced() > 0) return LOOK_COVERED;
    return v->replacing() > 0 ? LOOK_UNKNOWN : LOOK_NOT_HERE;
}

static const char* lookNote(unsigned wanted, int covered, const char* none) {
    if (covered == LOOK_NO_PACK) return none != nullptr ? none : N_("No replacement pack installed");
    if (covered == LOOK_NOT_HERE && look(wanted) != LOOK_STOCK) return N_("(no installed pack covers this)");
    return nullptr;
}

void focusLook(unsigned wanted, int covered, const char* none) {
    if (covered != LOOK_NO_PACK) {
        st.lookNow = true;
        st.lookWanted = wanted;
        if (focusButton(lookName(look(wanted)))) lookStep();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tr("Stock or Enhanced (the installed packs): F12, Select"));
    }
    const char* note = lookNote(wanted, covered, none);
    if (note != nullptr) focusText("%s", tr(note));
}

void lookOptions(unsigned wanted, int covered, bool compare, const char* none) {
    if (covered != LOOK_NO_PACK) {
        st.lookNow = true;
        st.lookWanted = wanted;
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(tr("Look"));
        ImGui::SameLine();
        int l = look(wanted);
        if (l == LOOK_COMPARE && !compare) l = LOOK_ENHANCED;
        const char* names[3] = {tr("Stock"), tr("Enhanced"), tr("Compare")};
        ImGui::SetNextItemWidth(ImGui::CalcTextSize(names[1]).x + ImGui::GetFrameHeight() * 2);
        if (ImGui::Combo("##look", &l, names, compare ? 3 : 2)) {
            setLook(l);
            logEvent("look: %s %s", lookKey().c_str(), l == LOOK_STOCK ? "stock" : l == LOOK_ENHANCED ? "enhanced" : "compare");
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tr("Stock or Enhanced (the installed packs): F12, Select"));
    }
    const char* note = lookNote(wanted, covered, none);
    if (note != nullptr) {
        ImGui::PushStyleColor(ImGuiCol_Text, skin::textHint);
        ImGui::TextWrapped("%s", tr(note));
        ImGui::PopStyleColor();
    }
}

bool lookOffered() {
    return st.lookNow || st.lookLast;
}

void lookLabel(const char* label) {
    label = tr(label);
    ImVec2 min = ImGui::GetItemRectMin(), max = ImGui::GetItemRectMax();
    float pad = 4.0f * unit();
    ImVec2 size = ImGui::CalcTextSize(label);
    ImVec2 at(min.x + 3 * pad, max.y - size.y - 3 * pad);
    if (at.y < min.y) return;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->PushClipRect(min, max, true);
    dl->AddRectFilled(ImVec2(at.x - pad, at.y - pad), ImVec2(at.x + size.x + pad, at.y + size.y + pad), IM_COL32(24, 26, 40, 230));
    dl->AddText(at, ImGui::GetColorU32(ImGuiCol_Text), label);
    dl->PopClipRect();
}

bool focusZoom(View* v, float actual) {
    int z = focusZoomButtons(actual > 0);
    if (v == nullptr || z == 0) return false;
    if (z == 1) v->fit();
    else if (z == 2) v->setZoom(v->zoom() * 0.8f);
    else if (z == 3) v->setZoom(v->zoom() * 1.25f);
    else v->setZoom(actual);
    return true;
}

// ---- shell.h ----------------------------------------------------------------------------------------

const Device* device() {
    return st.haveDevice ? &st.device : nullptr;
}

PFN_vkVoidFunction vkLoad(const char* name) {
    return st.haveDevice ? loadVk(name, &st.dev) : nullptr;
}

ImGuiContext* context() {
    return st.ctx;
}

} // namespace extras


using namespace extras;

// ---- extras.h ---------------------------------------------------------------------------------------

extern "C" void extrasInit(void) {
    xgSandboxInit();
}

extern "C" int extrasMain(void) {
    run(false);
    int game = st.intoGame;
    st.intoGame = false;
    return game ? 1 : 0;
}

extern "C" int extrasLeftIntoGame(void) {
    int game = st.intoGame;
    st.intoGame = false;
    return game;
}

extern "C" void extrasOpenFromMenu(void) {
    run(true);
}

extern "C" int extrasIsOpen(void) {
    return st.open;
}

extern "C" void extrasVulkanReady(const UiVulkan* dev) {
    st.dev = *dev;
    st.device.instance = (VkInstance)dev->instance;
    st.device.physical = (VkPhysicalDevice)dev->physical;
    st.device.device = (VkDevice)dev->device;
    st.device.queueFamily = dev->queueFamily;
    st.device.queue = (VkQueue)dev->queue;
    st.haveDevice = true;
}

extern "C" void extrasRecord(void* cmd, int w, int h, int format) {
    if (!st.open || !st.rendered || !st.ready) {
        return;
    }
    ImGuiContext* prev = ImGui::GetCurrentContext();
    ImGui::SetCurrentContext(st.ctx);
    ImDrawData* dd = ImGui::GetDrawData();
    if (dd != nullptr && dd->DisplaySize.x > 0 && dd->DisplaySize.y > 0) {
        if ((VkFormat)format != st.pipelineFormat) {
            if (st.pipelineFormat != VK_FORMAT_UNDEFINED) {
                ((PFN_vkDeviceWaitIdle)loadVk("vkDeviceWaitIdle", &st.dev))((VkDevice)st.dev.device);
            }
            VkFormat f = (VkFormat)format;
            ImGui_ImplVulkan_PipelineInfo pi = {};
            pi.PipelineRenderingCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
            pi.PipelineRenderingCreateInfo.colorAttachmentCount = 1;
            pi.PipelineRenderingCreateInfo.pColorAttachmentFormats = &f;
            ImGui_ImplVulkan_CreateMainPipeline(&pi);
            st.pipelineFormat = f;
        }
        dd->FramebufferScale = ImVec2(w / dd->DisplaySize.x, h / dd->DisplaySize.y);
        ImGui_ImplVulkan_RenderDrawData(dd, (VkCommandBuffer)cmd);
    }
    ImGui::SetCurrentContext(prev);
}

extern "C" void extrasVulkanShutdown(void) {
    extrasJukeboxEnd();
    ImGuiContext* prev = ImGui::GetCurrentContext();
    if (st.ready) {
        ImGui::SetCurrentContext(st.ctx);
        st.bookmarkPicture.reset(); st.pictureKey.clear();
        viewsShutdown();
        ImGui_ImplVulkan_Shutdown();
        st.ready = false;
    }
    st.haveDevice = false;
    // Extras may never have opened: keep the menu's context for its shutdown.
    if (prev == st.ctx) {
        prev = st.menuCtx != st.ctx ? st.menuCtx : nullptr;
    }
    ImGui::SetCurrentContext(prev);
}
