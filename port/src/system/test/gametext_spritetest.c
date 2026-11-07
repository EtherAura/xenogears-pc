/* Original synthetic sprite tables: aliases, piece identity, bounded
 * placement and atomic rejection. No disc, SDL or GPU is needed. */
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "system/gametext.h"
static int checks, failures;
static char why[160];
#define CHECK_DETAIL why
#include "test/check.h"
static void w16(uint8_t* p, int v) { p[0] = (uint8_t)v; p[1] = (uint8_t)((uint16_t)v >> 8); }
static int r16(const uint8_t* p) { return p[0] | p[1] << 8; }
static uint8_t* table(int count, int polys, int* size) {
    int header = 4 + 2 * count;
    *size = header + 4 + 28 * polys;
    uint8_t* p = calloc((size_t)*size, 1);
    if (!p) exit(2);
    p[0] = (uint8_t)count; p[1] = (uint8_t)(count >> 8);
    for (int i = 0; i < count; i++) w16(p + 4 + 2 * i, header);
    w16(p + header, polys); w16(p + header + 2, 0x7788);
    for (int k = 0; k < polys; k++) {
        uint8_t* q = p + header + 4 + 28 * k;
        memset(q, 0x5a, 28);
        w16(q, 20 + k); w16(q + 2, 32);
        w16(q + 4, 8); w16(q + 6, 13);
        w16(q + 8, 4 + k); w16(q + 10, 5 + k);
    }
    return p;
}
static char* layout(const uint8_t* p, int size, const char* rows) {
    size_t bytes = strlen(rows) + 80;
    char* text = malloc(bytes);
    if (!text) exit(2);
    snprintf(text, bytes, "xg-sprites-1 %016" PRIx64 "\n%s", gtSpriteFingerprint(p, size), rows);
    return text;
}
static uint8_t* apply(const uint8_t* p, int size, const char* rows, int* outSize) {
    char* text = layout(p, size, rows);
    uint8_t* out = gtSpriteLayoutApply(p, size, text, strlen(text), outSize, why, sizeof(why));
    free(text); return out;
}
static void rejects(const uint8_t* p, int size, const char* rows) {
    int outSize = 123;
    uint8_t* out = apply(p, size, rows, &outSize);
    EXPECT(!out && !outSize && why[0]); free(out);
}
int main(void) {
    int size, outSize, length;
    uint8_t* p = table(3, 1, &size);
    uint64_t original = gtSpriteFingerprint(p, size);
    char* text = gtSpriteLayoutTemplate(p, size, &length);
    CHECK(text && length > 0 && strlen(text) == (size_t)length);
    CHECK(!gtSpriteLayoutApply(p, size, text, (size_t)length, &outSize, why, sizeof(why)) && !outSize && !why[0]);
    free(text);
    uint8_t* out = apply(p, size, "0 0 24 -12 # moved word\n", &outSize);
    CHECK(out && !why[0] && outSize == size + 32);
    CHECK(r16(out + 4) == size && r16(out + 6) == 10 && r16(out + 8) == 10);
    CHECK(!memcmp(out + 10, p + 10, 32)); /* aliased sprites 1/2 retain the original */
    CHECK((int16_t)r16(out + size + 12) == 24 && (int16_t)r16(out + size + 14) == -12);
    for (int i = 0; i < 32; i++) if (i < 12 || i >= 16) CHECK(out[size + i] == p[10 + i]);
    CHECK(gtSpriteFingerprint(p, size) == original);
    free(out);
    uint8_t* odd = malloc((size_t)size + 1); CHECK(odd);
    memcpy(odd, p, (size_t)size); odd[size] = 0x77;
    out = apply(odd, size + 1, "0 0 24 -12\n", &outSize);
    CHECK(out && outSize == size + 34 && r16(out + 4) == size + 2 && out[size] == 0x77 && out[size + 1] == 0);
    free(out); free(odd);
    out = apply(p, size, "0 0 24 30\n1 0 -8 -12\n", &outSize);
    CHECK(out && outSize == size + 64 && r16(out + 4) != r16(out + 6));
    CHECK((int16_t)r16(out + r16(out + 4) + 12) == 24 && (int16_t)r16(out + r16(out + 6) + 12) == -8);
    CHECK(r16(out + 8) == 10 && !memcmp(out + 10, p + 10, 32)); free(out);
    out = apply(p, size, "2 0 1016 1011", &outSize);
    CHECK(out && !why[0]); free(out);
    out = apply(p, size, "2 0 -1024 -1024\n", &outSize);
    CHECK(out && !why[0]); free(out);
    rejects(p, size, "3 0 0 0\n"); rejects(p, size, "-1 0 0 0\n");
    rejects(p, size, "0 1 0 0\n"); rejects(p, size, "0 -1 0 0\n");
    rejects(p, size, "0 0 1017 0\n"); rejects(p, size, "0 0 0 1012\n");
    rejects(p, size, "0 0 -1025 0\n"); rejects(p, size, "0 0 0 -1025\n");
    rejects(p, size, "0 0 214748364899999 0\n"); rejects(p, size, "0 0 0x10 0\n");
    rejects(p, size, "0 0 0\n"); rejects(p, size, "0 0 0 0 extra\n");
    rejects(p, size, "0 0 1 2\n0 0 1 2\n");
    rejects(p, size, "0 0 1 2\n2 1 1 2\n"); /* late failure discards the earlier edit */
    text = layout(p, size, "0 0 10 20\n"); text[13] = text[13] == '0' ? '1' : '0';
    CHECK(!gtSpriteLayoutApply(p, size, text, strlen(text), &outSize, why, sizeof(why)) && why[0]); free(text);
    const char* bad[] = {"xg-sprites-2 0000000000000000\n", "xg-sprites-1 0\n", "# no header\n", ""};
    for (unsigned i = 0; i < sizeof(bad) / sizeof(bad[0]); i++)
        CHECK(!gtSpriteLayoutApply(p, size, bad[i], strlen(bad[i]), &outSize, why, sizeof(why)) && why[0]);
    text = layout(p, size, "\t0\t0 12 13\r\n");
    size_t n = strlen(text); memmove(text + 3, text, n + 1); memcpy(text, "\xEF\xBB\xBF", 3);
    out = gtSpriteLayoutApply(p, size, text, n + 3, &outSize, why, sizeof(why)); CHECK(out && !why[0]); free(out);
    text[20] = 0;
    CHECK(!gtSpriteLayoutApply(p, size, text, n + 3, &outSize, why, sizeof(why)) && why[0]); free(text);
    CHECK(!gtSpriteLayoutApply(p, size, "x", GT_SPRITE_LAYOUT_MAX + 1, &outSize, why, sizeof(why)) && why[0]);
    w16(p + 10 + 8, 257); rejects(p, size, "0 0 1 2\n"); w16(p + 10 + 8, 8);
    w16(p + 10 + 10, 0); rejects(p, size, "0 0 1 2\n"); w16(p + 10 + 10, 13);
    w16(p + 4, 1); rejects(p, size, "0 0 1 2\n"); w16(p + 4, 10);
    w16(p + 10, 65); rejects(p, size, "0 0 1 2\n"); w16(p + 10, -1);
    rejects(p, size, "0 0 1 2\n"); w16(p + 10, 1);
    rejects(p, size - 1, "0 0 1 2\n"); free(p);

    p = table(1, 2, &size);
    out = apply(p, size, "0 0 12 13\n0 1 22 23\n", &outSize);
    CHECK(out && outSize == size + 60); /* one clone for two pieces */
    CHECK(r16(out + size + 12) == 12 && r16(out + size + 40) == 22); free(out); free(p);
    p = table(1, 0, &size); rejects(p, size, "0 0 1 2\n"); free(p);
    p = table(4097, 1, &size); rejects(p, size, "0 0 1 2\n"); free(p);

    p = table(1025, 1, &size);
    text = malloc(65536); CHECK(text);
    int used = snprintf(text, 65536, "xg-sprites-1 %016" PRIx64 "\n", gtSpriteFingerprint(p, size));
    for (int i = 0; i < 1024; i++) used += snprintf(text + used, 65536 - (size_t)used, "%d 0 10 20\n", i);
    out = gtSpriteLayoutApply(p, size, text, (size_t)used, &outSize, why, sizeof(why));
    CHECK(out && outSize == size + 1024 * 32); free(out);
    used += snprintf(text + used, 65536 - (size_t)used, "1024 0 10 20\n");
    CHECK(!gtSpriteLayoutApply(p, size, text, (size_t)used, &outSize, why, sizeof(why)) && why[0]);
    free(text); free(p);
    p = table(50, 64, &size);
    text = malloc(4096); CHECK(text);
    used = snprintf(text, 4096, "xg-sprites-1 %016" PRIx64 "\n", gtSpriteFingerprint(p, size));
    for (int i = 0; i < 50; i++) used += snprintf(text + used, 4096 - (size_t)used, "%d 0 10 20\n", i);
    CHECK(!gtSpriteLayoutApply(p, size, text, (size_t)used, &outSize, why, sizeof(why)) && why[0]);
    free(text); free(p);
    p = table(4096, 64, &size);
    text = gtSpriteLayoutTemplate(p, size, &length);
    CHECK(text && length <= (int)GT_SPRITE_LAYOUT_MAX && strstr(text, "omitted"));
    CHECK(!gtSpriteLayoutApply(p, size, text, (size_t)length, &outSize, why, sizeof(why)) && !why[0]);
    free(text); free(p);
    printf("sprite placement: %d checks, %d failed\n", checks, failures);
    return failures != 0;
}
