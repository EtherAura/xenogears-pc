#ifndef XG_SPRITES_ENEMY_H
#define XG_SPRITES_ENEMY_H
#include <stdint.h>

/* 0D/3+2*set, as loadBattleSpriteVram reads it. Offsets remain relative
 * to the file; shared sheets refer to an earlier upload, not an entity. */
typedef struct {
    int bundleOffset, bundleSize;
    int sheetOffset, sheetSize;
    int vramX, palette;
} XgEnemySprite;

int xgEnemySpriteCount(const uint8_t* data, int size);
/* 1: sprite, 0: invalid/missing, -1: a 3D model (Models viewer). */
int xgEnemySpriteGet(const uint8_t* data, int size, int slot, XgEnemySprite* out);
#endif
