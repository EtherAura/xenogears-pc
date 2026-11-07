#include "export_manifest.h"
#include "bookmark_path.h"
#include <filesystem>
#include <fstream>
#include <chrono>
#include <cstdio>
#include <cstdlib>
using namespace extras;
static int checks;
static void check(bool yes, const char* name) { ++checks; if (!yes) { fprintf(stderr, "FAIL: %s\n", name); exit(1); } }
static std::string read(const std::filesystem::path& p) { std::ifstream in(p, std::ios::binary); return {std::istreambuf_iterator<char>(in), {}}; }
int main(int argc, char** argv) {
    auto root = std::filesystem::temp_directory_path() / ("xg-manifesttest-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(root / bookmarkPath("caf\xc3\xa9"));
    auto path = root / bookmarkPath("caf\xc3\xa9") / "manifest.json"; auto u = path.u8string();
    std::string utf8(reinterpret_cast<const char*>(u.data()), u.size()), why;
    ExportSource src; src.image = "/quoted\"/back\\slash/caf\xc3\xa9.cue"; src.region = "JP"; src.boot = "SLPS_011.60"; src.fingerprint = "1234";
    src.disc = 2; src.directory = 4; src.file = 0x1a8; src.entry = 123; src.lba = 800; src.bytes = 2048; src.field = 120; src.section = 2; src.members = {3, 8};
    ExportManifest m; m.add("file\n\t.png", "browser", {src}, {{"clut", "7"}, {"control", std::string(1, '\1')}});
    check(m.write(utf8, why), "Unicode destination"); auto first = read(path);
    check(first.find("\\\"") != std::string::npos && first.find("\\\\") != std::string::npos && first.find("\\u0001") != std::string::npos, "JSON escapes");
    check(first.find("\"members\": [3, 8]") != std::string::npos && first.find("\"field\": 120") != std::string::npos, "owned nested provenance");
    src.region = "US"; src.disc = 1;
    m.add("second.wav", "jukebox", {src}, {{"loops", "2"}});
    check(m.write(utf8, why), "atomic replacement"); auto second = read(path);
    check(second != first && second.find("second.wav") != std::string::npos && second.find("\"region\": \"JP\"") != std::string::npos, "mixed-source batch retains earlier identity");
    ExportManifest empty; check(!empty.write(utf8, why) && read(path) == second, "empty batch preserves old file");
    ExportManifest bad; src.entry = -1; bad.add("bad.png", "gallery", {src});
    check(!bad.write(utf8, why) && read(path) == second, "unknown file refused, previous manifest retained");
    check(!m.write("", why), "empty output path refused");
    auto block = root / "blocker"; std::ofstream(block) << "file";
    check(!m.write((block / "manifest").string(), why) && read(path) == second, "write failure retains metadata");
    auto dir = root / "destination-directory"; std::filesystem::create_directories(dir);
    check(!m.write(dir.string(), why) && !std::filesystem::exists(dir.string() + ".tmp"), "rename failure cleans temporary");
    if (argc > 1) { std::ofstream out(bookmarkPath(argv[1]), std::ios::binary); out << second; }
    std::filesystem::remove_all(root);
    printf("export manifests: %d checks passed\n", checks);
}
