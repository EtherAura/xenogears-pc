#ifndef PORT_BATTLEFX_DECODE_H
#define PORT_BATTLEFX_DECODE_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Supported native sprite-effect commands. Relative payloads must stay in
 * this bytecode region; dynamic payloads and scene/camera/audio ops are omitted. */
int xgBattleFxCommandSafe(const uint8_t* data, int size, int pc);
/* Native F3 custom-polygon model: bounded packets and vertex/normal indices. */
int xgBattleFxModelSafe(const uint8_t* data, int size, int pc);
/* A complete sprite bundle's first three tables, including FX files with
 * later sound banks or additional sprite bundles. */
int xgBattleFxBundle(const uint8_t* data, int size);
#ifdef __cplusplus
}
#endif
#endif
