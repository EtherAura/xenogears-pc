// User metadata only. Disc pixels live in a separate, disposable cache.
#ifndef PORT_EXTRAS_BOOKMARKS_H
#define PORT_EXTRAS_BOOKMARKS_H
#include <cstdint>
#include <string>
#include <vector>
#include "navigation.h"
namespace extras {
struct Bookmark {
    std::string label, image, region, boot;
    int disc = 0;
    uint64_t fingerprint = 0;
    ViewerTarget target;
    std::string key() const;
};
bool validBookmark(const Bookmark& b);
class Bookmarks {
public:
    static constexpr size_t limit = 128;
    explicit Bookmarks(std::string path = {}) : path_(std::move(path)) {}
    const std::vector<Bookmark>& items() const { return items_; }
    const std::string& path() const { return path_; }
    bool load(std::string& why); // Missing file is an empty collection; bad data preserves the collection.
    bool put(const Bookmark& b, size_t& index, std::string& why);
    bool rename(size_t index, const std::string& label, std::string& why);
    bool erase(size_t index, std::string& why);
    bool relocate(size_t index, const std::string& image, std::string& why);
    bool picture(size_t index, const std::vector<uint32_t>& pixels, std::string& why);
    bool picture(size_t index, std::vector<uint32_t>& pixels) const;
private:
    bool save(const std::vector<Bookmark>& next, std::string& why) const;
    std::string picturePath(const Bookmark& b) const;
    std::string path_;
    std::vector<Bookmark> items_;
};
constexpr int bookmarkWidth = 160, bookmarkHeight = 120;
} // namespace extras
#endif
