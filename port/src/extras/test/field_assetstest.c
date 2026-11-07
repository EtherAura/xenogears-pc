#include "field_assets.h"
#include <stdio.h>
#include <string.h>

static int checks;
#include "../../test/check.h" /* no port/src on this test's include path */
static void wr32(unsigned char* p, unsigned n) {
    p[0] = n; p[1] = n >> 8; p[2] = n >> 16; p[3] = n >> 24;
}
int main(void) {
    unsigned char raw[0x230] = {0}, bad[sizeof(raw)];
    XgFieldArchive a, keep;
    for (int k = 0; k < 9; k++) {
        wr32(raw + 0x10C + 4 * k, 4);
        wr32(raw + 0x130 + 4 * k, 0x190 + 12 * k);
        wr32(raw + 0x190 + 12 * k, 4);
    }
    CHECK(xgFieldArchiveParse(raw, sizeof(raw), &a));
    for (int k = 0; k < 9; k++) {
        CHECK(a.section[k].offset == 0x190 + 12 * k && a.section[k].decoded == 4);
        CHECK(a.section[k].bytes == (k < 8 ? 12 : sizeof(raw) - 0x190 - 12 * k));
    }
    CHECK(!strcmp(xgFieldSectionName(2), "Models") && !strcmp(xgFieldSectionName(7), "Dialogue"));
    CHECK(!strcmp(xgFieldSectionName(-1), "?") && !strcmp(xgFieldSectionName(9), "?"));
    CHECK(!xgFieldArchiveParse(NULL, sizeof(raw), &a) && !xgFieldArchiveParse(raw, -1, &a));
    CHECK(!xgFieldArchiveParse(raw, 0x18F, &a) && !xgFieldArchiveParse(raw, sizeof(raw), NULL));
    keep = a;
    memcpy(bad, raw, sizeof(raw)); bad[0x18D] = 0xFF;
    CHECK(!xgFieldArchiveParse(bad, sizeof(bad), &a) && !memcmp(&keep, &a, sizeof(a)));
    memcpy(bad, raw, sizeof(raw)); wr32(bad + 0x130, 0x18C);
    CHECK(!xgFieldArchiveParse(bad, sizeof(bad), &a));
    memcpy(bad, raw, sizeof(raw)); wr32(bad + 0x130, sizeof(raw) - 3);
    CHECK(!xgFieldArchiveParse(bad, sizeof(bad), &a));
    memcpy(bad, raw, sizeof(raw)); wr32(bad + 0x10C, 0xFFFFFFFF);
    CHECK(!xgFieldArchiveParse(bad, sizeof(bad), &a));
    memcpy(bad, raw, sizeof(raw)); wr32(bad + 0x190, 21);
    CHECK(!xgFieldArchiveParse(bad, sizeof(bad), &a));
    memcpy(bad, raw, sizeof(raw)); wr32(bad + 0x190, 3);
    CHECK(!xgFieldArchiveParse(bad, sizeof(bad), &a));
    memcpy(bad, raw, sizeof(raw)); wr32(bad + 0x190, 11);
    CHECK(xgFieldArchiveParse(bad, sizeof(bad), &a) && a.section[0].decoded == 11);
    memcpy(bad, raw, sizeof(raw)); wr32(bad + 0x134, 0x190);
    CHECK(!xgFieldArchiveParse(bad, sizeof(bad), &a));
    memcpy(bad, raw, sizeof(raw)); wr32(bad + 0x130, 0x19C); wr32(bad + 0x134, 0x190);
    CHECK(xgFieldArchiveParse(bad, sizeof(bad), &a) && a.section[1].bytes == 12);
    memcpy(bad, raw, sizeof(raw)); wr32(bad + 0x10C, 0); wr32(bad + 0x130, 0);
    CHECK(xgFieldArchiveParse(bad, sizeof(bad), &a) && a.section[0].bytes == 0);
    printf("Field asset headers: %d checks passed\n", checks);
    return 0;
}
