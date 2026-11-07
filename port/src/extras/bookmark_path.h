#ifndef PORT_EXTRAS_BOOKMARK_PATH_H
#define PORT_EXTRAS_BOOKMARK_PATH_H
#include <filesystem>
#include <string>
namespace extras {
inline std::filesystem::path bookmarkPath(const std::string& utf8) {
#ifdef __cpp_char8_t
    return std::filesystem::path(reinterpret_cast<const char8_t*>(utf8.c_str()));
#else
    return std::filesystem::u8path(utf8);
#endif
}
}
#endif
