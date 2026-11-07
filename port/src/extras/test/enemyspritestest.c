#include "sprites_enemy.h"
#include <stdio.h>
#include <string.h>

static int checks, failures;
#include "../../test/check.h" /* no port/src on this test's include path */
static void put32(uint8_t* p, unsigned n) {
    for (int i = 0; i < 4; i++) p[i] = (uint8_t)(n >> (8 * i));
}
static void sheet(uint8_t* p, int columns) {
    put32(p, (unsigned)columns);
    for (int i = 0; i < columns; i++) {
        int at = 4 + 4 * columns + 6 * i;
        put32(p + 4 + 4 * i, (unsigned)at);
        p[at] = p[at + 2] = 1; p[at + 4] = (uint8_t)(0x11 + i);
    }
}
int main(void) {
    /* A model before the first upload, two sprites sharing its sheet with
     * different palettes, and a second upload that crosses the VRAM wrap. */
    uint8_t data[216] = {4, 3};
    put32(data + 4, 160);
    int controls[] = {56, 72, 104, 128}, sheets[] = {200, 160, 0, 176};
    for (int i = 0; i < 4; i++) {
        put32(data + 8 + 12 * i, (unsigned)controls[i]);
        put32(data + 12 + 12 * i, (unsigned)sheets[i]);
    }
    data[16] = 1; data[43] = 2;
    sheet(data + 160, 1); sheet(data + 176, 2);
    XgEnemySprite a, b;
    EXPECT(xgEnemySpriteCount(data, sizeof(data)) == 4);
    EXPECT(xgEnemySpriteGet(data, sizeof(data), 0, &a) == -1);
    EXPECT(xgEnemySpriteGet(data, sizeof(data), 1, &a) == 1);
    EXPECT(a.bundleOffset == 72 && a.bundleSize == 88);
    EXPECT(a.sheetOffset == 160 && a.sheetSize == 16 && a.vramX == 576);
    EXPECT(xgEnemySpriteGet(data, sizeof(data), 2, &b) == 1);
    EXPECT(b.sheetOffset == a.sheetOffset && b.vramX == a.vramX && b.palette == 2);
    EXPECT(xgEnemySpriteGet(data, sizeof(data), 3, &b) == 1 && b.vramX == 640 && b.sheetSize == 24);
    EXPECT(!xgEnemySpriteGet(data, sizeof(data), 4, &b));
    EXPECT(!xgEnemySpriteGet(data, sizeof(data), -1, &b));
    EXPECT(!xgEnemySpriteGet(data, sizeof(data), 1, NULL));
    EXPECT(!xgEnemySpriteCount(NULL, 0));
    EXPECT(!xgEnemySpriteCount(data, 55));
    put32(data + 36, 1); /* shared upload 1 does not exist yet */
    EXPECT(!xgEnemySpriteGet(data, sizeof(data), 2, &b));
    put32(data + 36, 0);
    put32(data + 168, 0xFFFFFFFF); /* malicious dimensions */
    EXPECT(!xgEnemySpriteGet(data, sizeof(data), 1, &b));
    put32(data + 168, 0x00010001);
    put32(data + 164, 0xFFFFFFFF); /* image offset */
    EXPECT(!xgEnemySpriteGet(data, sizeof(data), 1, &b));
    put32(data + 164, 8);
    put32(data + 160, 0xFFFFFFFF); /* column count */
    EXPECT(!xgEnemySpriteGet(data, sizeof(data), 1, &b));
    put32(data + 160, 1);
    put32(data + 24, 0xFFFFFFFF); /* sheet offset */
    EXPECT(!xgEnemySpriteGet(data, sizeof(data), 1, &b));
    put32(data + 24, 160);
    for (int size = 0; size < 174; size++) EXPECT(!xgEnemySpriteGet(data, size, 1, &b));
    EXPECT(xgEnemySpriteGet(data, sizeof(data), 1, &b) == 1);
    printf("enemy sprites: %d checks, %d failures\n", checks, failures);
    return failures != 0;
}
