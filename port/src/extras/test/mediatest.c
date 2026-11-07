#include "extras/gallery.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static int checks;
static void check(int yes, const char* name) { ++checks; if (!yes) { fprintf(stderr, "FAIL: %s\n", name); exit(1); } }
static void word(uint8_t* c, int at, int v) { c[at] = (uint8_t)v; c[at + 1] = (uint8_t)(v >> 8); }
int main(void) {
    uint8_t code[64] = {0}; uint16_t roots[1][32] = {{0}};
    XgFieldScript fs = {code, sizeof(code), 1, roots}; XgFieldMedia* refs = NULL;
    /* Operand bytes resemble media ops, but a jump reaches only PC 16. */
    code[0] = 1; word(code, 1, 16); code[3] = 0xfe; code[4] = 0xa0;
    code[16] = 0xfe; code[17] = 0xa0;
    word(code, 18, 6); word(code, 20, 123); word(code, 22, 50); word(code, 24, 100); word(code, 26, 255); code[28] = 0xf8;
    int n = xgFieldMediaDecode(&fs, 120, &refs);
    check(n == 1 && refs[0].pc == 16 && refs[0].field == 120, "reachable source location only");
    check(refs[0].problem == 0 && refs[0].first == 50 && refs[0].last == 100 && refs[0].mode == 255, "A0 literal flags and sequence"); free(refs);
    code[28] = 0x78; n = xgFieldMediaDecode(&fs, 120, &refs);
    check(n == 1 && refs[0].problem == XG_MEDIA_DYNAMIC, "dynamic movie retained unavailable"); free(refs);
    memset(code, 0, sizeof(code)); code[0] = 0xfe; code[1] = 0x60;
    word(code, 2, 0x8005); word(code, 4, 0x8000); word(code, 6, 0x8014); word(code, 8, 0x8002);
    n = xgFieldMediaDecode(&fs, 1, &refs); check(n == 1 && refs[0].first == 1 && refs[0].last == 20 && !refs[0].problem, "60 immediate range"); free(refs);
    word(code, 6, 20); n = xgFieldMediaDecode(&fs, 1, &refs); check(n == 1 && refs[0].problem == XG_MEDIA_DYNAMIC, "60 variable frame count"); free(refs);
    memset(code, 0, sizeof(code)); code[0] = 0xfe; code[1] = 0x77; code[2] = 0;
    code[3] = 0xfe; code[4] = 0x77; code[5] = 1; word(code, 6, 42); code[14] = 0x80;
    fs.codeSize = 15; n = xgFieldMediaDecode(&fs, 50, &refs);
    check(n == 1 && refs[0].kind == XG_MEDIA_PICTURE && refs[0].picture == 42 && !refs[0].problem, "complete slide load and upload at buffer end"); free(refs);
    code[4] = 0x76; n = xgFieldMediaDecode(&fs, 50, &refs); check(n == 1 && refs[0].problem == XG_MEDIA_INVALID, "lookahead is not an upload"); free(refs); code[4] = 0x77;
    code[14] = 0; n = xgFieldMediaDecode(&fs, 50, &refs); check(n == 1 && refs[0].problem == XG_MEDIA_DYNAMIC, "dynamic slide number"); free(refs); code[14] = 0x80;
    for (int size = 1; size < 15; ++size) {
        fs.codeSize = size; n = xgFieldMediaDecode(&fs, 50, &refs);
        check(n == 0 || (n == 1 && refs[0].problem == XG_MEDIA_INVALID), "truncated operand cannot resolve"); free(refs);
    }
    fs.codeSize = sizeof(code); memset(code, 0, sizeof(code)); code[0] = 1; word(code, 1, 0);
    n = xgFieldMediaDecode(&fs, 0, &refs); check(n == 0, "cyclic jumps terminate"); free(refs);
    /* Fixed seed bounds exercise all opcode widths and shared routine roots. */
    unsigned seed = 17;
    for (int trial = 0; trial < 2000; ++trial) {
        for (int i = 0; i < 64; ++i) { seed = seed * 1664525u + 1013904223u; code[i] = (uint8_t)(seed >> 24); }
        fs.codeSize = trial % 64 + 1;
        for (int k = 0; k < 32; ++k) roots[0][k] = (uint16_t)(k % fs.codeSize);
        n = xgFieldMediaDecode(&fs, 0, &refs); check(n >= 0, "malformed bounds exercise"); free(refs);
    }
    printf("field media: %d checks passed\n", checks);
    return 0;
}
