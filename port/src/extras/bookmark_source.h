#ifndef PORT_EXTRAS_BOOKMARK_SOURCE_H
#define PORT_EXTRAS_BOOKMARK_SOURCE_H
#include "bookmarks.h"
#include "bookmark_path.h"
extern "C" {
#include "disc.h"
}
namespace extras {
inline bool bookmarkSource(Bookmark& b, const XgDisc* d, const char* viewer, const std::string& label) {
    if (!d) return false;
    std::error_code ec;
    auto path = std::filesystem::absolute(bookmarkPath(xgDiscPath(d)), ec);
    if (ec) return false;
    auto utf8 = path.u8string();
    b = Bookmark(); b.image.assign(reinterpret_cast<const char*>(utf8.data()), utf8.size());
    b.region = xgDiscRegion(d); b.boot = xgDiscBootName(d);
    b.disc = xgDiscNumber(d); b.fingerprint = xgDiscFingerprint(d); b.label = label;
    b.target = {viewer, {{"image", b.image}}}; return true;
}
} // namespace extras
#endif
