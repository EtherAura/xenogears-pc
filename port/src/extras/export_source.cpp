#include "export_manifest.h"
#include "bookmark_path.h"
#include <filesystem>
#include <iomanip>
#include <sstream>
namespace extras {
ExportSource exportSource(XgDisc* d, int directory, int file, const char* role, int field, int section, std::vector<int> members) {
    ExportSource s;
    if (!d) return s;
    std::error_code ec; auto path = std::filesystem::absolute(bookmarkPath(xgDiscPath(d)), ec);
    auto utf8 = path.u8string();
    s.image = ec ? xgDiscPath(d) : std::string(reinterpret_cast<const char*>(utf8.data()), utf8.size()); s.region = xgDiscRegion(d); s.boot = xgDiscBootName(d); s.disc = xgDiscNumber(d);
    std::ostringstream hash; hash << std::hex << std::setw(16) << std::setfill('0') << xgDiscFingerprint(d); s.fingerprint = hash.str();
    s.directory = directory; s.file = file; s.entry = xgDiscFileEntry(d, directory, file);
    auto e = xgDiscEntry(d, s.entry); s.lba = e.lba; s.bytes = e.size;
    s.role = role; s.field = field; s.section = section; s.members = std::move(members);
    return s;
}
}
