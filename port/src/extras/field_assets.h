#ifndef PORT_EXTRAS_FIELD_ASSETS_H
#define PORT_EXTRAS_FIELD_ASSETS_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

#define XG_FIELD_SECTIONS 9
typedef struct { int offset, bytes, decoded; } XgFieldSection;
typedef struct { XgFieldSection section[XG_FIELD_SECTIONS]; } XgFieldArchive;

/* The matched field loader's sizes at 0x10C and offsets at 0x130.
 * Validate the whole header before offering a section; the LZSS walker
 * and game decoder validate its contents when it is opened. No allocation. */
int xgFieldArchiveParse(const uint8_t* data, int size, XgFieldArchive* out);
const char* xgFieldSectionName(int section);

#ifdef __cplusplus
}
#endif
#endif
