#ifndef PORT_EXTRAS_EXPORT_MANIFEST_H
#define PORT_EXTRAS_EXPORT_MANIFEST_H
#include <string>
#include <vector>
#include <utility>
#include "disc.h"
namespace extras {
struct ExportSource {
    std::string image, region, boot, fingerprint, role;
    int disc = 0, directory = -1, file = -1, entry = -1, lba = 0, bytes = 0;
    int field = -1, section = -1;
    std::vector<int> members;
};
ExportSource exportSource(XgDisc*, int directory, int file, const char* role = "asset", int field = -1, int section = -1, std::vector<int> members = {});
using ExportSelection = std::vector<std::pair<std::string, std::string>>;
struct ExportRecord {
    std::string output, viewer;
    std::vector<ExportSource> sources;
    ExportSelection selection;
};
// One manifest per output or batch. Records own the actual source locators;
// callers add only successfully written outputs and commit once per batch.
class ExportManifest {
public:
    void add(std::string output, std::string viewer, std::vector<ExportSource> sources, ExportSelection selection = {});
    bool empty() const { return records_.empty(); }
    bool write(const std::string& path, std::string& why) const;
private:
    std::vector<ExportRecord> records_;
};
}
#endif
