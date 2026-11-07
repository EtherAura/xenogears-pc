#ifndef XG_PORTRAIT_PACK_H
#define XG_PORTRAIT_PACK_H
#include <stddef.h>
#include <stdint.h>

typedef struct { char id[128], name[128], author[128]; } PortraitSet;
/* Lookups by picture: dialogue (64 px), menu (48 px), the menu's 24 px
 * faces (the Load screen's) and battle (24 px). */
enum { PORTRAIT_DIALOGUE, PORTRAIT_MENU, PORTRAIT_MENU_THUMB, PORTRAIT_BATTLE, PORTRAIT_KINDS };
typedef struct {
    int enabled, battleEnabled, menuEnabled, entries;
    size_t bytes;
    uint64_t loads, misses, invalid, draws, generation;
    /* Since the last reload: each distinct missing picture once (misses are
     * remembered), each replacement draw. */
    uint64_t kindDraws[PORTRAIT_KINDS], kindMisses[PORTRAIT_KINDS];
    char message[256], battleMessage[256], menuMessage[256];
} PortraitPackStats;
typedef struct {
    const uint8_t* rgba; /* borrowed until another lookup, switch or reload */
    int size, scale;
    uint64_t revision;
} PortraitImage;
/* A library contains named set folders, each with portrait.ini and regional
 * content-key PNG folders. Original is the default and is always available. */
const char* portraitPackRoot(void);
const char* portraitPackSelected(void);
const char* portraitBattlePackSelected(void);
int portraitBattlePackSelect(const char* id);
int portraitBattlePackEnabled(void);
const char* portraitMenuPackSelected(void);
int portraitMenuPackSelect(const char* id);
int portraitPackSizeEnabled(int size);
int portraitPackClassEnabled(int kind);
int portraitPackCount(void);
const PortraitSet* portraitPackSet(int index);
void portraitPackSetRoot(const char* root);
int portraitPackSelect(const char* id);
void portraitPackReload(void);
void portraitPackShutdown(void);
int portraitPackEnabled(void);
void portraitPackStats(PortraitPackStats* out);
int portraitPackImage(uint64_t texel, uint64_t palette, PortraitImage* out);
/* Accept only the dialogue portrait format, with all lengths checked. */
int portraitPackTim(const void* tim, size_t bytes, PortraitImage* out);
int portraitSourceKeys(const void* tim, size_t bytes, uint64_t* texel, uint64_t* palette);
#endif
