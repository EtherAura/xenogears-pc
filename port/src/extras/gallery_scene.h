#ifndef PORT_EXTRAS_GALLERY_SCENE_H
#define PORT_EXTRAS_GALLERY_SCENE_H
#include "gallery.h"
#include "system/start.h"

typedef struct {
    int setsTo;
    PortStartRequest start;
    const char* instructions;
} XgScenePreset;

#ifdef __cplusplus
extern "C" {
#endif
/* Only presets whose indexed story transition was reached in actual US and
 * JP runs. Catalog order and region-specific bytecode PCs are not identities. */
const XgScenePreset* xgScenePreset(const XgScene* scene, const char* region);
void xgSceneStart(const XgScene* scene, const char* region, PortStartRequest* out);
int xgScenePresetMatches(const XgScenePreset* preset, const PortStartRequest* request);
/* Known failing cold starts stay unavailable until a working state is proven. */
const char* xgSceneStartProblem(const PortStartRequest* request);
/* Unverified/custom starts require an explicit override. Known failing
 * entrances remain blocked even with it. Shared by UI and scripted starts. */
const char* xgSceneStartProtection(const XgScenePreset* preset, const PortStartRequest* request, int override);
#ifdef __cplusplus
}
#endif
#endif
