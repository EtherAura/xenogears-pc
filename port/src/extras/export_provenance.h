#ifndef PORT_EXTRAS_EXPORT_PROVENANCE_H
#define PORT_EXTRAS_EXPORT_PROVENANCE_H
#include "export_manifest.h"
#include "viewer.h"
namespace extras {
inline bool writeManifest(const ExportManifest& m, const std::string& path) {
    std::string why;
    if (m.write(path, why)) return true;
    error("Export saved; manifest unavailable: %s", why.c_str()); return false;
}
inline bool recordExport(const std::string& output, const char* viewer, std::vector<ExportSource> sources, ExportSelection selection = {}, ExportManifest* batch = nullptr) {
    if (batch) { batch->add(output, viewer, std::move(sources), std::move(selection)); return true; }
    ExportManifest m; m.add(output, viewer, std::move(sources), std::move(selection));
    return writeManifest(m, output + ".manifest.json");
}
}
#endif
