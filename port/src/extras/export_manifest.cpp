#include "export_manifest.h"
#include "bookmark_path.h"
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#ifdef _WIN32
#include <windows.h>
#endif
namespace extras {
void ExportManifest::add(std::string output, std::string viewer, std::vector<ExportSource> sources, ExportSelection selection) {
    records_.push_back({std::move(output), std::move(viewer), std::move(sources), std::move(selection)});
}
static void quoted(std::ostream& out, const std::string& s) {
    const char* hex = "0123456789abcdef";
    out << '"';
    for (unsigned char c : s) {
        if (c == '"' || c == '\\') out << '\\' << c;
        else if (c < 32) out << "\\u00" << hex[c >> 4] << hex[c & 15];
        else out << c;
    }
    out << '"';
}
bool ExportManifest::write(const std::string& path, std::string& why) const {
    if (path.empty()) { why = "No export manifest path"; return false; }
    if (records_.empty()) { why = "No successful exports to describe"; return false; }
    for (const auto& r : records_) {
        if (r.output.empty() || r.viewer.empty() || r.sources.empty()) { why = "Export source is unavailable"; return false; }
        for (const auto& s : r.sources) if (s.image.empty() || s.entry < 0 || s.disc < 1 || s.disc > 2 || s.directory < 0 || s.file < 1) {
            why = "Export source is unavailable"; return false;
        }
    }
    auto dest = bookmarkPath(path), temp = dest; temp += ".tmp";
    std::ofstream out(temp, std::ios::binary | std::ios::trunc);
    if (!out) { why = "Cannot write export manifest: " + path; return false; }
    out << "{\n  \"schema\": \"xg-export-manifest-1\",\n  \"exports\": [\n";
    for (size_t i = 0; i < records_.size(); ++i) {
        const auto& r = records_[i];
        if (i) out << ",\n";
        out << "    {\"output\": "; quoted(out, r.output); out << ", \"viewer\": "; quoted(out, r.viewer);
        out << ", \"selection\": {";
        for (size_t k = 0; k < r.selection.size(); ++k) {
            if (k) out << ", ";
            quoted(out, r.selection[k].first); out << ": "; quoted(out, r.selection[k].second);
        }
        out << "}, \"sources\": [";
        for (size_t k = 0; k < r.sources.size(); ++k) {
            if (k) out << ", ";
            const auto& s = r.sources[k];
            out << "{\"image\": "; quoted(out, s.image); out << ", \"region\": "; quoted(out, s.region);
            out << ", \"boot\": "; quoted(out, s.boot); out << ", \"fingerprint\": "; quoted(out, s.fingerprint);
            out << ", \"role\": "; quoted(out, s.role);
            out << ", \"disc\": " << s.disc << ", \"directory\": " << s.directory << ", \"file\": " << s.file
                << ", \"entry\": " << s.entry << ", \"lba\": " << s.lba << ", \"bytes\": " << s.bytes
                << ", \"field\": " << s.field << ", \"section\": " << s.section << ", \"members\": [";
            for (size_t m = 0; m < s.members.size(); ++m) { if (m) out << ", "; out << s.members[m]; }
            out << "]}";
        }
        out << "]}";
    }
    out << "\n  ]\n}\n";
    out.close();
    std::error_code ec;
    if (!out) { std::filesystem::remove(temp, ec); why = "Cannot finish export manifest: " + path; return false; }
#ifdef _WIN32
    bool moved = MoveFileExW(temp.c_str(), dest.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
    std::filesystem::rename(temp, dest, ec); bool moved = !ec;
#endif
    if (!moved) { std::filesystem::remove(temp, ec); why = "Cannot replace export manifest: " + path; return false; }
    return true;
}
}
