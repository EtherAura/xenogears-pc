/* The save states' codec (statecodec.h): round trips of memory-like data,
 * worst cases and damaged streams. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "../statecodec.h"

static int failures;

static void check(int ok, const char* what) {
    if (!ok) {
        printf("FAIL: %s\n", what);
        failures++;
    }
}

static uint32_t rng = 12345;
static uint32_t next(void) {
    rng = rng * 1103515245u + 12345u;
    return rng >> 8;
}

static void roundTrip(const char* name, const uint8_t* data, size_t n, int timing) {
    uint8_t* packed = malloc(statePackBound(n));
    uint8_t* back = malloc(n + 1);
    clock_t t0 = clock();
    size_t size = statePack(data, n, packed);
    clock_t t1 = clock();
    int ok = stateUnpack(packed, size, back, n);
    clock_t t2 = clock();
    char what[200];
    snprintf(what, sizeof what, "%s: round trip (%zu bytes)", name, n);
    check(ok && memcmp(back, data, n) == 0, what);
    snprintf(what, sizeof what, "%s: within the bound", name);
    check(size <= statePackBound(n), what);
    if (timing) {
        double pack = (double)(t1 - t0) / CLOCKS_PER_SEC, unpack = (double)(t2 - t1) / CLOCKS_PER_SEC;
        printf("%-12s %9zu -> %9zu bytes (%5.1f%%), pack %6.0f MB/s, unpack %6.0f MB/s\n", name, n, size,
               100.0 * (double)size / (double)(n ? n : 1), n / 1e6 / (pack > 0 ? pack : 1e-9),
               n / 1e6 / (unpack > 0 ? unpack : 1e-9));
    }
    /* Damaged: every truncation point near the start and end, flipped bytes. */
    if (size > 16 && n > 0) {
        for (size_t cut = 0; cut < size; cut += size / 13 + 1) {
            stateUnpack(packed, cut, back, n); /* must not crash */
        }
        for (int i = 0; i < 50; i++) {
            uint8_t* bad = malloc(size);
            memcpy(bad, packed, size);
            bad[next() % size] ^= (uint8_t)(1 + next() % 255);
            stateUnpack(bad, size, back, n); /* must not crash or overrun */
            free(bad);
        }
        snprintf(what, sizeof what, "%s: a wrong raw size is refused", name);
        check(!stateUnpack(packed, size, back, n - 1), what);
    }
    free(packed);
    free(back);
}

int main(void) {
    roundTrip("empty", (const uint8_t*)"", 0, 0);
    roundTrip("one", (const uint8_t*)"x", 1, 0);
    roundTrip("short", (const uint8_t*)"abcabcabcabcabcabcabc", 21, 0);

    size_t n = 17u << 20;
    uint8_t* mem = calloc(n, 1);
    /* Memory-like: zero pages, small structures repeated with variation,
     * random runs (packed data), long equal runs. */
    for (size_t at = 0; at < n;) {
        size_t len = 64 + next() % 8192;
        int kind = (int)(next() % 6);
        if (at + len > n) len = n - at;
        if (kind == 0) {
            for (size_t i = 0; i < len; i++) mem[at + i] = (uint8_t)next();
        } else if (kind == 1) {
            for (size_t i = 0; i < len; i++) mem[at + i] = (uint8_t)((i * 7) ^ (i >> 5));
        } else if (kind == 2) {
            memset(mem + at, 0x80, len);
        } else if (kind == 3 && at > 70000) {
            memmove(mem + at, mem + at - 1000 - next() % 60000, len);
        }
        at += len;
    }
    roundTrip("memory", mem, n, 1);
    memset(mem, 0, n);
    roundTrip("zeros", mem, n, 1);
    for (size_t i = 0; i < n; i++) mem[i] = (uint8_t)next();
    roundTrip("random", mem, 3u << 20, 1);
    for (size_t i = 0; i < 4096; i++) mem[i] = (uint8_t)(i % 3);
    roundTrip("period3", mem, 4096, 0);

    /* The hash: deterministic, sensitive to every byte and the length. */
    uint64_t h = stateHash(mem, 1000, 1);
    check(h == stateHash(mem, 1000, 1), "hash deterministic");
    mem[999] ^= 1;
    check(h != stateHash(mem, 1000, 1), "hash sees the last byte");
    check(stateHash(mem, 1000, 1) != stateHash(mem, 999, 1), "hash sees the length");
    check(stateHash(mem, 1000, 1) != stateHash(mem, 1000, 2), "hash sees the seed");
    free(mem);
    if (failures == 0) {
        printf("statecodectest: all checks passed\n");
    }
    return failures != 0;
}
