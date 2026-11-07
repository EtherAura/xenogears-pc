#include "battle_moves.h"

#include <string.h>

static int rd16(const uint8_t *p) { return p[0] | p[1] << 8; }

int xgBattleLearnable(const uint8_t *levelUp, int size, int loadout, XgBattleLearnable *out) {
    memset(out, 0, sizeof(*out));
    if (!levelUp || loadout < 0 || loadout > 10 || (loadout + 1) * 0x110 > size)
        return 0;
    const uint8_t *c = levelUp + loadout * 0x110;
    while (out->deathblows < 13 && c[0x100 + out->deathblows] != 0xFF)
        out->deathblows++;
    while (out->ethers < 16 && c[0xF0 + out->ethers] != 0xFF)
        out->ethers++;
    while (out->gearDeathblows < 9 && c[0xD0 + out->gearDeathblows] != 0xFF)
        out->gearDeathblows++;
    while (out->gearEthers < 16 && c[0xE0 + out->gearEthers] != 0)
        out->gearEthers++;
    return 1;
}

int xgBattleAttackCost(int entry, int *place) {
    static const int cost[7] = {1, 1, 1, 1, 2, 2, 3}, order[7] = {1, 2, 3, 4, 1, 2, 1};
    if (entry < 0 || entry > 6) {
        if (place)
            *place = 0;
        return 0;
    }
    if (place)
        *place = order[entry];
    return cost[entry];
}

/* The fields an action plays from (executeBattleCode's handlers). */
static int playedFields(int kind) {
    switch (kind) {
    case XG_ACT_ATTACK: return 1 << 2;
    case XG_ACT_MOVE: case XG_ACT_IDLE: case XG_ACT_ABILITY: return 3 << 4;
    case XG_ACT_ANIMATION: case XG_ACT_WAIT: return 1 << 4;
    case XG_ACT_NAME: return 1 << 3;
    case XG_ACT_REMOVE: case XG_ACT_CAMERA: case XG_ACT_HIDE: return 0;
    }
    return -1;
}

int xgBattleActionPlays(const uint8_t action[8], int variable) {
    int fields = playedFields(action[0]);
    return fields >= 0 && !(variable & fields);
}

typedef struct {
    XgBattleTurn *out;
    int max, n, found, script;
    uint8_t actions[2][XG_TURN_ACTIONS][8], variable[2][XG_TURN_ACTIONS];
    int count[2]; /* 0 the unguarded actions, 1 the current block's */
} Walk;

static void add(Walk *w, int list, const uint8_t ctx[8], int variable) {
    if (w->count[list] < XG_TURN_ACTIONS) {
        memcpy(w->actions[list][w->count[list]], ctx, 8);
        w->variable[list][w->count[list]++] = (uint8_t)variable;
    }
}

static void flush(Walk *w, int withBlock) {
    XgBattleTurn t;
    memset(&t, 0, sizeof(t));
    t.script = w->script;
    t.nameIndex = -1;
    int visible = 0, plays = 1;
    for (int list = 0; list <= withBlock; list++)
        for (int i = 0; i < w->count[list]; i++) {
            const uint8_t *a = w->actions[list][i];
            int v = w->variable[list][i];
            if (playedFields(a[0]) < 0)
                continue; /* a state change (stats, slots): nothing to see */
            plays &= xgBattleActionPlays(a, v);
            visible |= a[0] == XG_ACT_ATTACK || a[0] == XG_ACT_ABILITY || a[0] == XG_ACT_ANIMATION;
            if (t.nameIndex < 0 && !(v & 8) &&
                (a[0] == XG_ACT_NAME || ((a[0] == XG_ACT_ATTACK || a[0] == XG_ACT_ABILITY) && a[3])))
                t.nameIndex = a[3];
            if (t.count < XG_TURN_ACTIONS) {
                memcpy(t.actions[t.count], a, 8);
                t.variable[t.count++] = (uint8_t)v;
            }
        }
    if (!visible)
        return;
    w->found++;
    if (!plays)
        return;
    for (int i = 0; i < w->n; i++) {
        const XgBattleTurn *o = &w->out[i];
        if (o->count == t.count && !memcmp(o->actions, t.actions, sizeof(t.actions)) &&
            !memcmp(o->variable, t.variable, sizeof(t.variable)))
            return;
    }
    if (w->n < w->max)
        w->out[w->n++] = t;
}

int xgBattleTurns(const uint8_t *b, int size, int type, XgBattleTurn *out, int max, int *found) {
    if (found)
        *found = 0;
    if (!b || size < 0x32 || type < 0 || type >= 24)
        return -1;
    int types = 0;
    while (types < 24 && rd16(b + 2 * types) != 0)
        types++;
    if (type >= types)
        return -1;
    int strings = rd16(b + 0x30), end = strings < size ? strings : size;
    int block = rd16(b + 2 * type);
    Walk w;
    memset(&w, 0, sizeof(w));
    w.out = out;
    w.max = max;
    for (int script = 0; script < 4 && block + 8 <= size; script++) {
        if (script == 1)
            continue; /* loaded, never run (b_80079674.c) */
        int pc = block + rd16(b + block + 2 * script), guarded = 0, unguardedUsed = 0;
        uint8_t ctx[8] = {0};
        int variable = 0;
        w.script = script;
        w.count[0] = w.count[1] = 0;
        for (int steps = 0; pc >= 0 && pc + 4 <= end && steps < 4096; steps++, pc += 4) {
            int op = b[pc], x = b[pc + 1], y = b[pc + 2], z = b[pc + 3];
            if (op == 0xFF || op == 0xFD) {
                /* a guarded block's return; unguarded, the script's end */
                if (!guarded)
                    break;
                flush(&w, 1);
                unguardedUsed = 1;
                guarded = 0;
                w.count[1] = 0;
                continue;
            }
            if (op >= 0x81 && op <= 0xEF && op != 0x9A) {
                if (guarded && w.count[1]) {
                    flush(&w, 1); /* the block ran into the next guard */
                    unguardedUsed = 1;
                }
                if (!guarded || w.count[1])
                    w.count[1] = 0;
                guarded = 1;
                continue;
            }
            if ((op == 0x01 || op == 0x02) && x < 8) {
                ctx[x] = op == 0x01 ? (uint8_t)y : 0;
                variable = op == 0x01 ? variable & ~(1 << x) : variable | 1 << x;
                if (op == 0x01 && x == 0) {
                    add(&w, guarded, ctx, variable);
                    if (!guarded)
                        unguardedUsed = 0;
                    memset(ctx, 0, sizeof(ctx));
                    variable = 0;
                }
            } else if ((op == 0x3D || op == 0x52) && x < 7) {
                ctx[x] = op == 0x3D ? (uint8_t)y : 0;
                ctx[x + 1] = op == 0x3D ? (uint8_t)z : 0;
                variable = op == 0x3D ? variable & ~(3 << x) : variable | 3 << x;
            }
        }
        if (w.count[0] && !unguardedUsed)
            flush(&w, 0);
    }
    if (found)
        *found = w.found;
    return w.n;
}
