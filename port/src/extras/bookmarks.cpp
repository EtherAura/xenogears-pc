#include "bookmarks.h"
#include "bookmark_path.h"
#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <sstream>
#ifdef _WIN32
#include <windows.h>
#endif
namespace extras {
namespace {
constexpr size_t maxFile = 1024 * 1024, maxString = 4096;
constexpr char magic[] = "XGBOOK01";
FILE* openFile(const std::string& path, bool writing) {
#ifdef _WIN32
    return _wfopen(bookmarkPath(path).c_str(), writing ? L"wb" : L"rb");
#else
    return fopen(path.c_str(), writing ? "wb" : "rb");
#endif
}
void removeFile(const std::string& path) {
    std::error_code ec; std::filesystem::remove(bookmarkPath(path), ec);
}
void word(std::vector<uint8_t>& out, uint32_t n) {
    for (int i = 0; i < 4; ++i) out.push_back((uint8_t)(n >> (i * 8)));
}
void string(std::vector<uint8_t>& out, const std::string& s) {
    word(out, (uint32_t)s.size()); out.insert(out.end(), s.begin(), s.end());
}
struct Reader {
    const std::vector<uint8_t>& bytes; size_t at = 8; bool ok = true;
    uint32_t word() {
        if (!ok || at + 4 > bytes.size()) { ok = false; return 0; }
        uint32_t n = 0; for (int i = 0; i < 4; ++i) n |= (uint32_t)bytes[at++] << (i * 8);
        return n;
    }
    std::string string() {
        size_t n = word();
        if (!ok || n > maxString || n > bytes.size() - at) { ok = false; return {}; }
        std::string s((const char*)bytes.data() + at, n); at += n; return s;
    }
};
bool text(const std::string& s, size_t max, bool empty = false) {
    return (empty || !s.empty()) && s.size() <= max &&
        std::none_of(s.begin(), s.end(), [](unsigned char c) { return c < 32 || c == 127; });
}
bool oneOf(const std::string& s, const char* list) {
    std::istringstream in(list); std::string v;
    while (in >> v) if (s == v) return true;
    return false;
}
bool numbers(const std::string& s, int count, double lo, double hi, bool integral = true, int base = 10) {
    const char* p = s.c_str();
    for (int i = 0; i < count; ++i) {
        while (*p == ' ') ++p;
        if (!*p) return false;
        char* end; errno = 0;
        double n = integral ? (double)strtoll(p, &end, base) : strtod(p, &end);
        if (p == end || errno || !std::isfinite(n) || n < lo || n > hi || (*end && *end != ' ')) return false;
        p = end;
    }
    while (*p == ' ') ++p;
    return !*p;
}
bool selector(const std::string& viewer, const ViewerCommand& c) {
    const auto& n = c.name; const auto& a = c.argument;
    if (viewer == "browser") {
        if (n == "select") return numbers(a, 2, 0, 65535, true, 16);
        if (n == "section") return numbers(a, 1, 0, 8);
        if (n == "member" || n == "clut") return numbers(a, 1, 0, 65535);
        if (n == "lzss") return a.empty();
        if (n == "tab") return oneOf(a, "info hex bundle picture model sound");
        if (n == "model") return numbers(a, 2, -4096, 4096);
    } else if (viewer == "gallery") {
        if (n == "media") { std::istringstream in(a); int field, pc; std::string extra;
            return (in >> field >> pc) && !(in >> extra) && field >= 0 && field <= 729 && pc >= 0 && pc <= 8 * 1024 * 1024;
        }
        if (n == "clip") { std::istringstream in(a); long long stream, offset, frame; std::string extra;
            return (in >> stream >> offset >> frame) && !(in >> extra) && stream >= 0 && stream <= 65535 &&
                offset >= 0 && offset <= 2147483647 && frame >= 1 && frame <= 65535;
        }
        if (n == "hold") return numbers(a, 1, 1, 65535);
        if (n == "picture") return numbers(a, 1, 0, 179);
        if (n == "clut") return numbers(a, 1, 0, 65535);
        if (n == "zero") return oneOf(a, "0 1");
    } else if (viewer == "maps") {
        if (n == "field") return numbers(a, 1, 0, 729);
        if (n == "world") return numbers(a, 1, 0, 65535);
        if (n == "progress") return a == "initial" || numbers(a, 1, 0, 65535);
        if (n == "entry" || n == "select") return numbers(a, 1, -1, 65535);
        if (n == "camera") return oneOf(a, "free game");
        if (n == "orbit" || n == "target" || n == "worldcam" || n == "worldtarget") return numbers(a, 3, -1e7, 1e7, false);
        if (n == "layers" || n == "worldlayers") return numbers(a, 1, 0, 0xffff, true, 16);
        if (n == "animate" || n == "worldanimate") return oneOf(a, "0 1");
        if (n == "overview") return a.empty();
        if (n == "tab") return oneOf(a, "view walkmesh actors assets exits start export");
    } else if (viewer == "models" || viewer == "animations") {
        if (n == "source") return oneOf(a, "gears battle enemies arenas fields world battling");
        if (n == "field" || n == "world") return numbers(a, 2, 0, 65535);
        if (n == "item") return numbers(a, 1, 0, 65535);
        if (n == "anim") return numbers(a, 1, 0, 4095);
        if (n == "seek") return numbers(a, 1, 0, 10000);
        if (n == "view") return numbers(a, 3, -1e6, 1e6, false);
        if (n == "tab") return oneOf(a, "model textures");
        if (n == "page") return numbers(a, 1, 0, 63);
        if (n == "textures" || n == "edges" || n == "shadow") return oneOf(a, "on off");
        if (n == "light") return oneOf(a, "view fixed");
        if (n == "pause") return a.empty();
    } else if (viewer == "battle") {
        if (n == "presentation") {
            std::istringstream in(a); int actor, target, full; std::string extra;
            return (in >> actor >> target >> full) && !(in >> extra) && actor >= 0 && actor < 11 &&
                target >= 0 && target < 11 && full >= 0 && full <= 1;
        }
        if (n == "setup") {
            std::istringstream in(a); int actor, arena, set, form, enemy; std::string extra;
            return (in >> actor >> arena >> set >> form >> enemy) && !(in >> extra) &&
                actor >= 0 && actor <= 10 && arena >= 0 && arena < 75 && set >= 0 && set <= 255 &&
                form >= 0 && form <= 2 && enemy >= 0 && enemy < 8;
        }
        if (n == "move") {
            std::istringstream in(a); int actor, target, kind, id, program, full; std::string extra;
            return (in >> actor >> target >> kind >> id >> program >> full) && !(in >> extra) &&
                actor >= 0 && actor < 11 && target >= 0 && target < 11 && kind >= 0 && kind <= 4 &&
                id >= 0 && id < 251 && program >= 0 && program < 64 && full >= 0 && full <= 1;
        }
        if (n == "seek") return numbers(a, 1, 0, 3600);
        if (n == "pause") return a.empty();
    } else if (viewer == "battlefx") {
        if (n == "source") return oneOf(a, "abilities common gears");
        if (n == "item" || n == "anim") return numbers(a, 1, 0, 65535);
        if (n == "seek") return numbers(a, 1, 0, 10000);
        if (n == "view") return numbers(a, 3, -4096, 4096, false);
        if (n == "followbounds") return oneOf(a, "on off");
        if (n == "pause") return a.empty();
    } else if (viewer == "sprites") {
        if (n == "source") {
            auto space = a.find(' ');
            return space != std::string::npos && oneOf(a.substr(0, space), "party gear npc battle special effects") &&
                numbers(a.substr(space + 1), 2, 0, 65535);
        }
        if (n == "tab") return oneOf(a, "anims frames");
        if (n == "anim" || n == "frame" || n == "palette") return numbers(a, 1, 0, 4095);
        if (n == "dir") return numbers(a, 1, 0, 15);
        if (n == "step") return numbers(a, 1, 0, 10000);
        if (n == "flip") return oneOf(a, "0 1");
        if (n == "loop") return oneOf(a, "0 1");
        if (n == "zoom") return numbers(a, 1, 0.01, 256, false);
        if (n == "pause") return a.empty();
    } else if (viewer == "jukebox") {
        if (n == "play") return a.size() > 1 && a[0] == '#' && numbers(a.substr(1), 1, 0, 127);
        if (n == "stop") return a.empty();
    }
    return false;
}
bool read(const std::string& path, size_t max, std::vector<uint8_t>& bytes, bool& missing) {
    FILE* f = openFile(path, false); missing = !f && errno == ENOENT;
    if (!f) return false;
    bool ok = fseek(f, 0, SEEK_END) == 0;
    long n = ok ? ftell(f) : -1;
    ok = n >= 0 && (size_t)n <= max && fseek(f, 0, SEEK_SET) == 0;
    if (ok) { bytes.resize((size_t)n); ok = fread(bytes.data(), 1, bytes.size(), f) == bytes.size(); }
    fclose(f); return ok;
}
bool write(const std::string& path, const std::vector<uint8_t>& bytes, std::string& why) {
    std::error_code ec;
    auto parent = bookmarkPath(path).parent_path();
    if (!parent.empty()) std::filesystem::create_directories(parent, ec);
    if (ec) { why = "Cannot create bookmark folder: " + ec.message(); return false; }
    std::string temp = path + ".tmp";
    FILE* f = openFile(temp, true);
    if (!f) { why = "Cannot write bookmarks: " + std::string(strerror(errno)); return false; }
    bool ok = fwrite(bytes.data(), 1, bytes.size(), f) == bytes.size();
    if (fclose(f) != 0) ok = false;
    if (ok) {
#ifdef _WIN32
        // rename() cannot replace an existing file on Windows.
        ok = MoveFileExW(bookmarkPath(temp).c_str(), bookmarkPath(path).c_str(),
                         MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
        ok = std::rename(temp.c_str(), path.c_str()) == 0;
#endif
    }
    if (!ok) { removeFile(temp); why = "Cannot replace bookmark file"; }
    return ok;
}
} // namespace
bool validBookmark(const Bookmark& b) {
    if (!text(b.label, 160) || !text(b.image, maxString) || !text(b.boot, 64) || !oneOf(b.region, "US JP EU ?") ||
        b.disc < 1 || b.disc > 2 || !b.fingerprint || !oneOf(b.target.viewer, "browser maps models animations sprites jukebox gallery battlefx battle") ||
        b.target.commands.size() < 2 || b.target.commands.size() > 32 || b.target.commands[0].name != "image" ||
        b.target.commands[0].argument != b.image) return false;
    for (size_t i = 1; i < b.target.commands.size(); ++i)
        if (!text(b.target.commands[i].argument, 256, true) || !selector(b.target.viewer, b.target.commands[i])) return false;
    return true;
}
std::string Bookmark::key() const {
    uint64_t h = UINT64_C(14695981039346656037);
    auto add = [&](const std::string& s) { for (unsigned char c : s) h = (h ^ c) * UINT64_C(1099511628211); h = (h ^ 0) * UINT64_C(1099511628211); };
    add(region); add(boot); add(std::to_string(disc)); add(std::to_string(fingerprint)); add(target.viewer);
    // Image path is a locator, not identity: cue/bin aliases of one disc deduplicate.
    for (size_t i = 1; i < target.commands.size(); ++i) { add(target.commands[i].name); add(target.commands[i].argument); }
    char s[17]; snprintf(s, sizeof(s), "%016llx", (unsigned long long)h); return s;
}
bool Bookmarks::load(std::string& why) {
    why.clear(); if (path_.empty()) return true;
    std::vector<uint8_t> bytes; bool missing;
    if (!read(path_, maxFile, bytes, missing)) {
        if (missing) { items_.clear(); return true; }
        why = "Cannot read bookmark file"; return false;
    }
    if (bytes.size() < 12 || memcmp(bytes.data(), magic, 8)) { why = "Unknown bookmark format"; return false; }
    Reader in{bytes}; uint32_t count = in.word(); std::vector<Bookmark> next;
    if (count > limit) { why = "Too many bookmarks"; return false; }
    for (uint32_t i = 0; i < count && in.ok; ++i) {
        Bookmark b; b.label = in.string(); b.image = in.string(); b.region = in.string(); b.boot = in.string();
        b.disc = (int)in.word(); b.fingerprint = in.word(); b.fingerprint |= (uint64_t)in.word() << 32;
        b.target.viewer = in.string(); uint32_t n = in.word();
        if (n > 32) { in.ok = false; break; }
        for (uint32_t k = 0; k < n && in.ok; ++k) { auto name = in.string(); auto arg = in.string(); b.target.commands.push_back({name, arg}); }
        if (!validBookmark(b) || std::any_of(next.begin(), next.end(), [&](const Bookmark& old) { return old.key() == b.key(); })) in.ok = false;
        next.push_back(std::move(b));
    }
    if (!in.ok || in.at != bytes.size()) { why = "Invalid or truncated bookmark file"; return false; }
    items_ = std::move(next); return true;
}
bool Bookmarks::save(const std::vector<Bookmark>& next, std::string& why) const {
    if (path_.empty()) { why = "Bookmarks need a settings path (XG_SETTINGS=none disables saving)"; return false; }
    std::vector<uint8_t> bytes(magic, magic + 8); word(bytes, (uint32_t)next.size());
    for (const Bookmark& b : next) {
        string(bytes, b.label); string(bytes, b.image); string(bytes, b.region); string(bytes, b.boot);
        word(bytes, b.disc); word(bytes, (uint32_t)b.fingerprint); word(bytes, (uint32_t)(b.fingerprint >> 32));
        string(bytes, b.target.viewer); word(bytes, (uint32_t)b.target.commands.size());
        for (const auto& c : b.target.commands) { string(bytes, c.name); string(bytes, c.argument); }
    }
    if (bytes.size() > maxFile) { why = "Bookmark file is full"; return false; }
    return write(path_, bytes, why);
}
bool Bookmarks::put(const Bookmark& b, size_t& index, std::string& why) {
    if (!validBookmark(b)) { why = "This asset cannot be bookmarked"; return false; }
    auto next = items_; index = next.size();
    for (size_t i = 0; i < next.size(); ++i) if (next[i].key() == b.key()) { index = i; break; }
    if (index == limit) { why = "The bookmark collection is full (128)"; return false; }
    if (index == next.size()) next.push_back(b); else next[index] = b;
    if (!save(next, why)) return false;
    items_ = std::move(next); return true;
}
bool Bookmarks::rename(size_t index, const std::string& label, std::string& why) {
    if (index >= items_.size() || !text(label, 160)) { why = "Choose a bookmark and a name (1-160 bytes)"; return false; }
    auto next = items_; next[index].label = label;
    if (!save(next, why)) return false;
    items_ = std::move(next); return true;
}
bool Bookmarks::relocate(size_t index, const std::string& image, std::string& why) {
    if (index >= items_.size() || !text(image, maxString)) { why = "Invalid source image path"; return false; }
    auto next = items_; next[index].image = image; next[index].target.commands[0].argument = image;
    if (!save(next, why)) return false;
    items_ = std::move(next); return true;
}
std::string Bookmarks::picturePath(const Bookmark& b) const { return path_ + ".cache/" + b.key() + ".rgba"; }
bool Bookmarks::erase(size_t index, std::string& why) {
    if (index >= items_.size()) { why = "No such bookmark"; return false; }
    auto next = items_; auto cache = picturePath(next[index]); next.erase(next.begin() + index);
    if (!save(next, why)) return false;
    items_ = std::move(next); removeFile(cache); return true;
}
bool Bookmarks::picture(size_t index, const std::vector<uint32_t>& pixels, std::string& why) {
    if (index >= items_.size() || pixels.size() != bookmarkWidth * bookmarkHeight) return false;
    std::vector<uint8_t> bytes{'X','G','T','H','U','M','B','1'};
    for (uint32_t pixel : pixels) word(bytes, pixel);
    return write(picturePath(items_[index]), bytes, why);
}
bool Bookmarks::picture(size_t index, std::vector<uint32_t>& pixels) const {
    pixels.clear(); if (index >= items_.size() || path_.empty()) return false;
    std::vector<uint8_t> bytes; bool missing;
    constexpr size_t size = 8 + bookmarkWidth * bookmarkHeight * 4;
    if (!read(picturePath(items_[index]), size, bytes, missing) || bytes.size() != size || memcmp(bytes.data(), "XGTHUMB1", 8)) return false;
    Reader in{bytes}; pixels.resize(bookmarkWidth * bookmarkHeight);
    for (uint32_t& p : pixels) p = in.word();
    return true;
}
} // namespace extras
