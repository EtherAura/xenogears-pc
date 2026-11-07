#include "extras/gallery_battle.h"
#include <stdio.h>
#include <string.h>

static int checks, calls, at, values[4];
#include "test/check.h"
static void found(void* context, int pc, const int args[4]) {
    calls++; at = pc; memcpy(values, args, sizeof(values));
    if (context != &calls) calls = -100;
}
static void word(unsigned char* p, unsigned v) { p[0] = v; p[1] = v >> 8; }
static unsigned char data[256];
static unsigned char* fresh(void) {
    memset(data, 0, sizeof(data)); data[0x40] = 1;
    memset(data + 0x44, 255, 16); word(data + 0x44, 0);
    calls = 0; return data + 0x54;
}
static void movie(unsigned char* p) {
    p[0] = 0x27; word(p + 1, 0x8001); word(p + 3, 0x8005);
    word(p + 5, 0x8001); word(p + 7, 0x8260);
}
int main(void) {
    unsigned char* code = fresh(); movie(code);
    CHECK(xgBattleMovies(data, 0x54 + 10, found, &calls) == 1);
    CHECK(calls == 1 && at == 0 && values[0] == 1 && values[1] == 5 && values[2] == 1 && values[3] == 608);
    CHECK(xgBattleMovies(data, 0x54 + 9, NULL, NULL) == 1);
    CHECK(xgBattleMovies(data, 0x54 + 8, NULL, NULL) == 0); /* truncated op */
    word(code + 3, 5); calls = 0;
    CHECK(xgBattleMovies(data, 0x54 + 10, found, &calls) == 0 && !calls); /* variable */
    code = fresh(); movie(code + 4); code[0] = 1; word(code + 1, 14); /* skip movie */
    CHECK(xgBattleMovies(data, 0x54 + 15, NULL, NULL) == 0);
    word(code + 1, 4);
    CHECK(xgBattleMovies(data, 0x54 + 15, found, &calls) == 1 && at == 4);
    word(code + 1, 0);
    CHECK(xgBattleMovies(data, 0x54 + 15, NULL, NULL) == 0); /* cyclic jump */
    code = fresh(); code[0] = 2; word(code + 6, 18); movie(code + 8); movie(code + 18);
    CHECK(xgBattleMovies(data, 0x54 + 28, found, &calls) == 2 && calls == 2);
    word(data + 0x46, 18); calls = 0; /* duplicate root and branch target */
    CHECK(xgBattleMovies(data, 0x54 + 28, found, &calls) == 2 && calls == 2);
    code = fresh(); code[0] = 0x26; code[1] = 0x27; movie(code + 9);
    CHECK(xgBattleMovies(data, 0x54 + 19, found, &calls) == 1 && at == 9); /* operand bytes */
    code[0] = 0xff;
    CHECK(xgBattleMovies(data, 0x54 + 19, NULL, NULL) == 0); /* unknown flow */
    CHECK(xgBattleMovies(NULL, 0, NULL, NULL) == -1);
    CHECK(xgBattleMovies(data, 0x40, NULL, NULL) == -1);
    data[0x40] = 17;
    CHECK(xgBattleMovies(data, sizeof(data), NULL, NULL) == -1);
    data[0x40] = 1;
    CHECK(xgBattleMovies(data, 0x54, NULL, NULL) == -1);
    printf("Gallery battle events: %d checks passed\n", checks);
    return 0;
}
