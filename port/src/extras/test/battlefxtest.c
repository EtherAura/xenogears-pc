#include "battlefx_decode.h"
#include <stdio.h>
#include <string.h>
#include <limits.h>
static int checks;
#include "../../test/check.h" /* no port/src on this test's include path */
static void u32(uint8_t* p, unsigned n) {
    p[0] = n;
    p[1] = n >> 8;
    p[2] = n >> 16;
    p[3] = n >> 24;
}
int main(void) {
    uint8_t data[128] = {0};
    u32(data, 3);
    u32(data + 4, 16);
    u32(data + 8, 32);
    u32(data + 12, 64);
    data[16] = 1;
    data[32] = 1;
    data[64] = 1;
    CHECK(xgBattleFxBundle(data, 100));
    CHECK(!xgBattleFxBundle(data, 99)); /* incomplete palette */
    data[64] = 0;
    CHECK(xgBattleFxBundle(data, 68)); /* pre-baked bank's external CLUT */
    data[64] = 1;
    u32(data + 12, 0); data[32] = 0; data[33] = 0x80;
    CHECK(xgBattleFxBundle(data, 68)); /* palette-free root controller */
    data[32] = 1; CHECK(!xgBattleFxBundle(data, 68)); /* root frames require their table */
    data[32] = 0; data[33] = 0; CHECK(!xgBattleFxBundle(data, 68)); /* only pre-baked roots */
    data[32] = 1;
    CHECK(!xgBattleFxBundle(NULL, 100));
    u32(data + 12, 0xFFFFFFFEu);
    CHECK(!xgBattleFxBundle(data, 100)); /* overflow */
    u32(data + 12, 64);
    data[16] = 63;
    CHECK(!xgBattleFxBundle(data, 100)); /* truncated animation table */
    data[16] = 1;
    data[32] = 511 & 255;
    data[33] = 1;
    CHECK(!xgBattleFxBundle(data, 100));
    memset(data, 0, sizeof(data));
    data[0] = 0xC3;
    data[1] = 5;
    CHECK(xgBattleFxCommandSafe(data, 2, 0)); /* line renderer, no operands */
    CHECK(!xgBattleFxCommandSafe(data, 1, 0));
    data[1] = 0x0D;
    CHECK(!xgBattleFxCommandSafe(data, 2, 0)); /* rotation needs a byte */
    data[0] = 0xEC;
    CHECK(xgBattleFxCommandSafe(data, 3, 0));
    data[1] = 0x46;
    CHECK(xgBattleFxCommandSafe(data, 3, 0)); /* owned animated model selector */
    CHECK(!xgBattleFxCommandSafe(data, 2, 0));
    data[1] = 0x47;
    CHECK(xgBattleFxCommandSafe(data, 3, 0)); /* owned model clip selector */
    data[0] = 0xC3; data[1] = 0x50;
    CHECK(xgBattleFxCommandSafe(data, 2, 0)); /* native model event synchronisation */
    data[1] = 0x51;
    CHECK(xgBattleFxCommandSafe(data, 2, 0)); /* release owned model */
    data[0] = 0xEC; data[1] = 0x0D;
    data[0] = 0xE8;
    CHECK(xgBattleFxCommandSafe(data, 3, 0)); /* bounded at runtime after resolving the parameter */
    data[1] = 5;
    CHECK(xgBattleFxCommandSafe(data, 3, 0));
    data[0] = 0xF9;
    data[1] = 0x19;
    data[2] = 6; /* payload at 8 */
    CHECK(xgBattleFxCommandSafe(data, 14, 0));
    CHECK(!xgBattleFxCommandSafe(data, 13, 0));
    data[2] = 0xFC;
    data[3] = 0xFF;
    CHECK(!xgBattleFxCommandSafe(data, 14, 0)); /* before region */
    data[2] = 0xFE;
    CHECK(xgBattleFxCommandSafe(data, 14, 0)); /* payload at zero */
    data[1] = 0x18;
    CHECK(xgBattleFxCommandSafe(data, 14, 0)); /* owned screen capture */
    data[1] = 0x3B;
    CHECK(!xgBattleFxCommandSafe(data, 14, 0)); /* retail wobble's invalid parent path */
    data[1] = 0x24;
    CHECK(xgBattleFxCommandSafe(data, 14, 0)); /* owned tiled helper overlay */
    data[1] = 0x27; data[2] = data[3] = 0xFF;
    CHECK(xgBattleFxCommandSafe(data, 4, 0)); /* CLUT deltas are literal bytes */
    data[0] = 0xEC; CHECK(!xgBattleFxCommandSafe(data, 3, 0)); /* needs both deltas */
    data[0] = 0xF9;
    CHECK(!xgBattleFxCommandSafe(data, 14, -1));
    CHECK(!xgBattleFxCommandSafe(data, 14, 14));
    CHECK(!xgBattleFxCommandSafe(data, INT_MIN, 0));
    memset(data, 0, sizeof(data));
    data[0] = 0xF3; data[1] = 3; /* model starts at 4 */
    u32(data + 12, 1); /* one sub-buffer, starting at 16 */
    u32(data + 16, 44); u32(data + 20, 3); /* three vertices at 60 */
    u32(data + 24, 68); u32(data + 32, 28); u32(data + 36, 1);
    data[44] = 4; data[45] = 3; data[46] = 1; data[47] = 0x20;
    data[54] = 1; data[56] = 2;
    CHECK(xgBattleFxModelSafe(data, 84, 0));
    CHECK(!xgBattleFxModelSafe(data, 83, 0)); /* missing vertex */
    data[56] = 3; CHECK(!xgBattleFxModelSafe(data, 84, 0)); /* invalid index */
    data[56] = 2; data[45] = 0; CHECK(!xgBattleFxModelSafe(data, 84, 0)); /* truncated packet */
    data[45] = 3; data[44] = 12; CHECK(!xgBattleFxModelSafe(data, 84, 0)); /* packet/primitive mismatch */
    data[44] = 4; u32(data + 4 + 4, 1); CHECK(!xgBattleFxModelSafe(data, 84, 0)); /* host pointers unsupported */
    data[1] = data[2] = data[3] = 0; CHECK(xgBattleFxModelSafe(data, 4, 0)); /* release */
    CHECK(!xgBattleFxModelSafe(NULL, 4, 0));
    CHECK(!xgBattleFxModelSafe(data, 4, -1));
    CHECK(!xgBattleFxModelSafe(data, INT_MIN, 0));
    printf("Battle FX bounds: %d checks passed\n", checks);
    return 0;
}
