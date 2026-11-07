/* Battle-event VM facts from our matched be_801E5160.c: the header has
 * 8 u16 routine entries per actor at 0x44; op lengths are handler advances,
 * including their successful advances after waits. No byte-pattern search. */
#include "gallery_battle.h"
#include <stdlib.h>

static unsigned word(const uint8_t* p) { return p[0] | (unsigned)p[1] << 8; }
static unsigned dword(const uint8_t* p) { return word(p) | word(p + 2) << 16; }

static const uint8_t lengths[0x4c] = {
    /* 00 */ 1,3,8,3,3,3,6,3,3,6,6,6,6,3,3,6,
    /* 10 */ 6,6,5,5,3,5,6,6,4,5,11,2,1,1,3,3,
    /* 20 */ 1,1,1,7,5,1,9,9,6,9,5,3,3,3,3,5,
    /* 30 */ 3,9,1,1,1,5,3,1,7,1,5,3,3,3,9,9,
    /* 40 */ 3,7,1,1,1,9,7,1,5,3,1,3
};

static void queue(int pc, int size, uint8_t* seen, int* todo, int* count) {
    if (pc >= 0 && pc < size && !seen[pc]) {
        seen[pc] = 1;
        todo[(*count)++] = pc;
    }
}

int xgBattleMovies(const uint8_t* script, int size, XgBattleMovieFn found, void* context) {
    if (script == NULL || size < 0x44) return -1;
    unsigned actors = dword(script + 0x40);
    if (actors == 0 || actors > 16 || 0x44 + actors * 16 >= (unsigned)size) return -1;
    int start = (int)(0x44 + actors * 16);
    int bytes = size - start;
    /* Script PCs and branch targets are u16 in the VM. */
    if (bytes > 65536) bytes = 65536;
    const uint8_t* code = script + start;
    uint8_t* seen = calloc((size_t)bytes, 1);
    int* todo = malloc((size_t)bytes * sizeof(*todo));
    if (seen == NULL || todo == NULL) { free(seen); free(todo); return -1; }
    int count = 0, movies = 0;
    for (unsigned a = 0; a < actors; a++) for (int slot = 0; slot < 8; slot++) {
        queue((int)word(script + 0x44 + a * 16 + slot * 2), bytes, seen, todo, &count);
    }
    while (count) {
        int pc = todo[--count];
        while (pc < bytes && seen[pc] != 2) {
            seen[pc] = 2;
            int op = code[pc];
            int n = (unsigned)op < sizeof(lengths) ? lengths[op] : 0;
            if (!n || n > bytes - pc) break;
            if (op == 0x27) {
                int args[4], literal = 1;
                for (int i = 0; i < 4; i++) {
                    unsigned arg = word(code + pc + 1 + 2 * i);
                    if (!(arg & 0x8000)) literal = 0;
                    args[i] = (int)(arg & 0x7fff);
                }
                if (literal) {
                    movies++;
                    if (found != NULL) found(context, pc, args);
                }
            }
            if (op == 1 || op == 2) {
                queue((int)word(code + pc + (op == 1 ? 1 : 6)), bytes, seen, todo, &count);
            }
            if (op == 0 || op == 1) break;
            pc += n;
        }
    }
    free(seen); free(todo);
    return movies;
}
