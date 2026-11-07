/* battle_moves.c on made-up data: the learnable counts, the attack cycle and
 * the enemy turns a script file queues. */
#include "battle_moves.h"
#include <stdio.h>
#include <string.h>

static int checks;
#include "test/check.h"

static unsigned char set[0x400];
static int pc;
static void op(int a, int b, int c, int d) {
    set[pc] = (unsigned char)a; set[pc + 1] = (unsigned char)b;
    set[pc + 2] = (unsigned char)c; set[pc + 3] = (unsigned char)d;
    pc += 4;
}
/* One type whose script 0 starts at 0x200 (scripts 1-3 at 0x300: an end). */
static void fresh(void) {
    memset(set, 0, sizeof(set));
    set[0] = 0x80; set[1] = 0x01;                 /* type 0's script block at 0x180 */
    set[0x30] = 0xF0; set[0x31] = 0x03;           /* the strings past the scripts */
    set[0x180] = 0x80; set[0x182] = 0x80; set[0x183] = 0x01; /* script 0 at +0x80 */
    set[0x184] = 0x80; set[0x185] = 0x01;         /* script 2 at +0x180 */
    set[0x186] = 0x80; set[0x187] = 0x01;         /* script 3 at +0x180 */
    set[0x300] = 0xFD;
    pc = 0x200;
}
static void attack(int record, int animation) {
    op(0x01, 1, record, 0);
    op(0x01, 2, animation, 0);
    op(0x01, 0, 1, 0);
}

int main(void) {
    /* learnable counts */
    static unsigned char levels[11 * 0x110];
    memset(levels, 0, sizeof(levels));
    unsigned char *fei = levels;
    memset(fei + 0x100, 0, 16);
    fei[0x100 + 2] = 0xFF;                          /* two deathblows */
    fei[0xF0] = 1; fei[0xF1] = 0xFF;                 /* one Ether */
    memset(fei + 0xD0, 1, 9);                       /* nine Gear deathblows */
    fei[0xE0] = 1; fei[0xE1] = 2; fei[0xE2] = 0;     /* two Gear Ethers */
    XgBattleLearnable learn;
    CHECK(xgBattleLearnable(levels, sizeof(levels), 0, &learn));
    CHECK(learn.deathblows == 2 && learn.ethers == 1 && learn.gearDeathblows == 9 && learn.gearEthers == 2);
    unsigned char *maria = levels + 8 * 0x110;
    maria[0x100] = 0xFF; maria[0xF0] = 0xFF;
    maria[0xD0] = 5; maria[0xD1] = 0xFE; maria[0xD2] = 0xFF; /* levels; 0xFE stays (events) */
    CHECK(xgBattleLearnable(levels, sizeof(levels), 8, &learn));
    CHECK(learn.deathblows == 0 && learn.ethers == 0 && learn.gearDeathblows == 2 && learn.gearEthers == 0);
    CHECK(!xgBattleLearnable(levels, sizeof(levels) - 1, 10, &learn) && learn.deathblows == 0);
    CHECK(!xgBattleLearnable(levels, sizeof(levels), 11, &learn));

    /* the plain attacks */
    int place;
    CHECK(xgBattleAttackCost(0, &place) == 1 && place == 1);
    CHECK(xgBattleAttackCost(3, &place) == 1 && place == 4);
    CHECK(xgBattleAttackCost(5, &place) == 2 && place == 2);
    CHECK(xgBattleAttackCost(6, &place) == 3 && place == 1);
    CHECK(xgBattleAttackCost(7, &place) == 0 && place == 0);

    /* an unguarded turn: approach, a named attack, the end */
    XgBattleTurn turns[8];
    int found;
    fresh();
    op(0x52, 6, 7, 0);                 /* the target from a variable */
    op(0x01, 0, 2, 0);                 /* moveAndAttack */
    op(0x01, 3, 10, 0);
    op(0x01, 0, 12, 0);                /* show name 10 */
    attack(3, 0x1E);
    op(0xFD, 0xFF, 0xFF, 0xFF);
    CHECK(xgBattleTurns(set, sizeof(set), 0, turns, 8, &found) == 1 && found == 1);
    CHECK(turns[0].script == 0 && turns[0].count == 3 && turns[0].nameIndex == 10);
    CHECK(turns[0].actions[0][0] == XG_ACT_MOVE && turns[0].variable[0] == 0xC0);
    CHECK(turns[0].actions[2][0] == XG_ACT_ATTACK && turns[0].actions[2][1] == 3 && turns[0].actions[2][2] == 0x1E);

    /* guarded blocks: each with the unguarded actions before it; an
     * ability then an attack; a block that only waits shows nothing; a
     * variable animation does not play; a repeated turn counts once */
    fresh();
    op(0x01, 0, 15, 0);                /* camera, unguarded */
    op(0x90, 7, 0, 0); op(0x81, 0, 0, 0);
    op(0x3D, 4, 0xF2, 0x02); op(0x01, 0, 13, 0); /* ability 0xF2 | 1 << 9 */
    attack(1, 0x20);
    op(0xFF, 0, 0, 0);
    op(0x81, 1, 0, 0);
    op(0x01, 4, 30, 0); op(0x01, 0, 14, 0);      /* only a wait */
    op(0xFF, 0, 0, 0);
    op(0x81, 2, 0, 0);
    op(0x02, 2, 5, 0); op(0x01, 0, 1, 0);        /* a variable animation */
    op(0xFF, 0, 0, 0);
    op(0x81, 3, 0, 0);
    op(0x3D, 4, 0xF2, 0x02); op(0x01, 0, 13, 0);
    attack(1, 0x20);
    op(0x01, 1, 1, 0); op(0x01, 0, 8, 0);        /* a stat change: left out */
    op(0xFF, 0, 0, 0);
    op(0xFD, 0, 0, 0);
    CHECK(xgBattleTurns(set, sizeof(set), 0, turns, 8, &found) == 1 && found == 3);
    CHECK(turns[0].count == 3 && turns[0].actions[0][0] == XG_ACT_CAMERA && turns[0].actions[1][0] == XG_ACT_ABILITY);
    CHECK((turns[0].actions[1][4] | turns[0].actions[1][5] << 8) == (0xF2 | 1 << 9));
    CHECK(turns[0].nameIndex == -1);
    CHECK(xgBattleTurns(set, sizeof(set), 0, turns, 0, &found) == 0 && found == 3);

    /* a block running into the next guard ends there */
    fresh();
    op(0x81, 0, 0, 0);
    attack(2, 0x21);
    op(0x81, 1, 0, 0); op(0x82, 1, 0, 0);       /* two guards: one block */
    attack(2, 0x22);
    attack(2, 0x23);
    op(0xFF, 0, 0, 0);
    op(0xFD, 0, 0, 0);
    CHECK(xgBattleTurns(set, sizeof(set), 0, turns, 8, &found) == 2 && found == 2);
    CHECK(turns[0].count == 1 && turns[0].actions[0][2] == 0x21);
    CHECK(turns[1].count == 2 && turns[1].actions[1][2] == 0x23);

    /* the other scripts: when attacked (2) and after the party's turn (3) */
    fresh();
    op(0xFD, 0, 0, 0);
    pc = 0x300;
    attack(0, 0x24);
    op(0xFD, 0, 0, 0);
    CHECK(xgBattleTurns(set, sizeof(set), 0, turns, 8, &found) == 1 && found == 2 && turns[0].script == 2);

    /* not a set file, no such type, a file cut before its script or in it */
    CHECK(xgBattleTurns(NULL, 0, 0, turns, 8, NULL) == -1);
    CHECK(xgBattleTurns(set, sizeof(set), 1, turns, 8, NULL) == -1);
    fresh();
    attack(0, 0x25);
    CHECK(xgBattleTurns(set, 0x1F0, 0, turns, 8, &found) == 0 && found == 0);
    CHECK(xgBattleTurns(set, 0x20C, 0, turns, 8, &found) == 1 && turns[0].actions[0][2] == 0x25);
    CHECK(xgBattleTurns(set, sizeof(set), 0, turns, 8, &found) == 1);

    unsigned char action[8] = {XG_ACT_ATTACK, 0, 0x30};
    CHECK(xgBattleActionPlays(action, 0) && xgBattleActionPlays(action, 1 << 1) && !xgBattleActionPlays(action, 1 << 2));
    action[0] = 9;
    CHECK(!xgBattleActionPlays(action, 0));
    printf("Battle moves: %d checks passed\n", checks);
    return 0;
}
