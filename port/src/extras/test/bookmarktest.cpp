#include "bookmarks.h"
#include "bookmark_path.h"
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <cstdlib>
#include <chrono>
using namespace extras;
static int checks;
static void check(bool ok, const char* name) { ++checks; if (!ok) { fprintf(stderr, "FAIL: %s\n", name); exit(1); } }
static Bookmark asset(int field = 120) {
    Bookmark b; b.label = "Bledavik"; b.image = "/user/disc.cue"; b.region = "US"; b.boot = "SLUS_006.64";
    b.disc = 1; b.fingerprint = UINT64_C(0xabcdef9876543210);
    b.target = {"maps", {{"image", b.image}, {"field", std::to_string(field)}, {"camera", "free"}, {"orbit", "35 -15 1500"}}};
    return b;
}
int main() {
    auto root = std::filesystem::temp_directory_path() / ("xg-bookmarktest-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    auto file = root / "settings.ini.bookmarks";
    Bookmarks store(file.string()); std::string why; size_t index = 999;
    check(store.load(why) && store.items().empty(), "missing file");
    Bookmark b = asset(); check(store.put(b, index, why) && index == 0, "first save creates parent");
    Bookmarks again(file.string()); check(again.load(why) && again.items().size() == 1, "restart persistence");
    check(again.items()[0].fingerprint == b.fingerprint && again.items()[0].target.commands[3].argument == "35 -15 1500", "owned metadata");
    auto alias = b; alias.image = "/user/disc.bin"; alias.target.commands[0].argument = alias.image;
    check(alias.key() == b.key(), "cue/bin locator independent key");
    alias.label = "Renamed"; check(store.put(alias, index, why) && index == 0 && store.items().size() == 1, "deduplicate location");
    auto jp = b; jp.region = "JP"; jp.boot = "SLPS_011.60";
    check(jp.key() != b.key() && store.put(jp, index, why) && index == 1, "regional source identity");
    auto other = b; other.fingerprint++; check(other.key() != b.key(), "table fingerprint identity");
    std::vector<uint32_t> pixels(bookmarkWidth * bookmarkHeight, 0xff123456);
    check(store.picture(0, pixels, why), "write bounded cache");
    std::vector<uint32_t> loaded; const Bookmarks& reader = store;
    check(reader.picture(0, loaded) && loaded == pixels, "RGBA channels round trip");
    check(store.rename(0, "A label with UTF-8: \xc3\xa9", why), "rename");
    check(store.relocate(0, "/new location/disc.cue", why), "relocate");
    check(reader.picture(0, loaded) && loaded == pixels, "relocate retains preview");
    check(!store.rename(0, "", why) && !store.rename(0, std::string(161, 'x'), why), "label bounds");
    for (const auto& cmd : std::vector<ViewerCommand>{{"export", "png"}, {"follow", "archive"}, {"field", "-1"},
            {"field", "730"}, {"worldcam", "nan 0 1"}, {"orbit", "1 2 inf"}, {"target", "1 2 3 extra"}}) {
        auto bad = b; bad.target.commands.push_back(cmd); check(!validBookmark(bad), "reject unsafe command / range");
    }
    auto bad = b; bad.target.commands[0].argument = "other.cue"; check(!validBookmark(bad), "source mismatch");
    bad = b; bad.label = "bad\nlabel"; check(!validBookmark(bad), "reject controls");
    bad = b; bad.target.viewer = "unknown"; check(!validBookmark(bad), "unsupported selector family");
    auto fx = b; fx.target.viewer = "battlefx";
    fx.target.commands = {{"image", b.image}, {"source", "abilities"}, {"item", "10"}, {"anim", "0"}, {"seek", "60"}, {"followbounds", "on"}, {"pause", ""}};
    check(validBookmark(fx), "battle effects bookmark");
    fx.target.commands.back() = {"seek", "1200"}; check(validBookmark(fx), "long model spell bookmark");
    fx.target.commands.back() = {"seek", "10001"}; check(!validBookmark(fx), "battle effects timeline bound");
    fx.target.commands.back() = {"export", "png"}; check(!validBookmark(fx), "battle effects export is not a selector");
    auto battle = b; battle.target = {"battle", {{"image", b.image}, {"setup", "0 0 0 0 0"},
        {"move", "0 3 2 110 2 1"}, {"seek", "1200"}, {"pause", ""}}};
    check(validBookmark(battle), "native battle actor, arena and subprogram bookmark");
    battle.target.commands.back() = {"presentation", "3 0 1"};
    check(validBookmark(battle), "native scene-only bookmark retains presentation and acting enemy");
    for (const auto& c : std::vector<ViewerCommand>{{"setup", "11 0 0 0 0"}, {"setup", "0 75 0 0 0"},
            {"setup", "0 0 0 3 0"}, {"setup", "0 0 0 0 8"}, {"move", "11 3 2 110 0 1"},
            {"move", "0 3 5 110 0 1"}, {"move", "0 3 2 251 0 1"}, {"move", "0 3 2 110 64 1"},
            {"move", "0 3 2 110 0 2"}, {"presentation", "11 0 1"}, {"presentation", "3 11 1"},
            {"presentation", "3 0 2"}, {"presentation", "3 0 1 extra"}, {"seek", "3601"}, {"export", "png"}}) {
        battle.target.commands.back() = c; check(!validBookmark(battle), "reject invalid native battle selector");
    }
    auto sprite = b;
    sprite.target = {"sprites", {{"image", b.image}, {"source", "effects 6 14"}, {"anim", "0"},
                                 {"pause", ""}, {"loop", "0"}, {"step", "90"}}};
    Bookmarks spriteStore((root / "sprites").string());
    check(spriteStore.put(sprite, index, why), "save non-looping ability at a later effect tick");
    Bookmarks spriteReload((root / "sprites").string());
    check(spriteReload.load(why) && spriteReload.items()[0].target.commands[4].argument == "0" &&
          spriteReload.items()[0].target.commands[5].argument == "90", "restore ability loop setting before replay ticks");
    sprite.target.commands[4].argument = "1";
    check(validBookmark(sprite), "looping ability bookmark");
    sprite.target.commands[4].argument = "2";
    check(!validBookmark(sprite), "reject invalid loop setting");
    bad = b; bad.target.commands.resize(33, {"field", "0"}); check(!validBookmark(bad), "command count bound");
    auto gallery = b; gallery.target = {"gallery", {{"image", b.image}, {"media", "120 42"}, {"hold", "10"}}};
    check(validBookmark(gallery), "source movie bookmark");
    gallery.target.commands = {{"image", b.image}, {"picture", "179"}, {"clut", "1"}, {"zero", "0"}};
    check(validBookmark(gallery), "picture settings bookmark");
    for (const auto& c : std::vector<ViewerCommand>{{"media", "730 1"}, {"media", "1 -1"}, {"media", "1 2 extra"},
            {"picture", "180"}, {"hold", "0"}, {"clip", "1 -1 3"}, {"clip", "1 0 0"}, {"start", ""}, {"wav", ""}, {"pause", ""}}) {
        gallery.target.commands = {{"image", b.image}, c}; check(!validBookmark(gallery), "reject unsafe gallery selector");
    }
    std::ifstream input(file, std::ios::binary); std::vector<char> bytes((std::istreambuf_iterator<char>(input)), {}); input.close();
    auto malformed = [&](std::vector<char> data) {
        std::ofstream out(file, std::ios::binary | std::ios::trunc); out.write(data.data(), data.size()); out.close();
        check(!store.load(why) && store.items().size() == 2, "bad metadata preserves collection");
    };
    for (size_t length : {size_t(0), size_t(7), size_t(12), bytes.size() - 1}) malformed({bytes.begin(), bytes.begin() + length});
    auto data = bytes; data[7] = '2'; malformed(data);
    data = bytes; data.push_back('x'); malformed(data);
    data = bytes; data[8] = (char)255; malformed(data);
    data = bytes; data[12] = (char)255; data[13] = (char)255; malformed(data);
    malformed(std::vector<char>(1024 * 1024 + 1, 0));
    // Saving after a bad load is explicit; replacement is atomic and round-trips.
    check(store.rename(1, "Japan", why) && again.load(why) && again.items()[1].label == "Japan", "replace existing file");
    check(store.erase(0, why) && !reader.picture(1, loaded), "delete shifts indices safely");
    check(!store.erase(99, why) && !store.relocate(99, "x", why), "invalid indices");
    Bookmarks disabled; check(!disabled.put(b, index, why) && disabled.items().empty(), "settings none");
    auto blocker = root / "blocker"; std::ofstream(blocker) << "file";
    Bookmarks failure((blocker / "bookmarks").string());
    check(!failure.put(b, index, why) && failure.items().empty(), "write failure does not mutate memory");
    auto unicode = root / bookmarkPath("caf\xc3\xa9") / "bookmarks";
    auto native = unicode.u8string(); std::string utf8(reinterpret_cast<const char*>(native.data()), native.size());
    Bookmarks international(utf8);
    check(international.put(b, index, why) && international.rename(0, "updated", why), "UTF-8 file replacement");
    Bookmarks reload(utf8);
    check(reload.load(why) && reload.items()[0].label == "updated", "UTF-8 path read");
    check(international.picture(0, pixels, why) && international.erase(0, why), "UTF-8 preview write/delete");
    Bookmarks full((root / "full").string());
    for (size_t i = 0; i < Bookmarks::limit; ++i) check(full.put(asset((int)i), index, why), "bounded collection add");
    check(!full.put(asset(128), index, why) && full.items().size() == 128, "collection limit");
    check(full.rename(0, "still editable", why) && full.erase(0, why) && full.put(asset(128), index, why), "full collection recovery");
    std::filesystem::remove_all(root);
    printf("bookmarks: %d checks passed\n", checks);
}
