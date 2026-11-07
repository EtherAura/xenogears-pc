/*
 * The save states' compression (statecodec.h). Greedy LZ77: each position's
 * first four bytes are looked up in a hash table of the positions seen; a
 * hit within 64 KB is extended as far as it goes. Long stretches without a
 * hit are crossed in growing steps, so data that does not pack costs little.
 */

#include "statecodec.h"

#include <string.h>

#define MIN_MATCH 4
#define MAX_DISTANCE 65535
#define HASH_BITS 14
#define TAIL 5       /* the last bytes of a block are always literals */
#define MIN_PACK 16  /* shorter blocks are stored */

static uint32_t read32(const uint8_t* p) {
    uint32_t v;
    memcpy(&v, p, 4);
    return v;
}

static uint64_t read64(const uint8_t* p) {
    uint64_t v;
    memcpy(&v, p, 8);
    return v;
}

static void put32(uint8_t* p, uint32_t v) {
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

static uint32_t get32(const uint8_t* p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static uint32_t hash4(uint32_t v) {
    return (v * 2654435761u) >> (32 - HASH_BITS);
}

size_t statePackBound(size_t n) {
    size_t blocks = (n + STATE_PACK_BLOCK - 1) / STATE_PACK_BLOCK;
    return n + 8 * (blocks ? blocks : 1);
}

/* An extra length (a count past 15) in bytes; 0 when it does not fit. */
static int putLength(uint8_t* out, size_t* op, size_t cap, size_t n) {
    while (n >= 255) {
        if (*op >= cap) {
            return 0;
        }
        out[(*op)++] = 255;
        n -= 255;
    }
    if (*op >= cap) {
        return 0;
    }
    out[(*op)++] = (uint8_t)n;
    return 1;
}

/* One sequence: the literals in[from..to), then (if len) a match. */
static int putSequence(uint8_t* out, size_t* op, size_t cap, const uint8_t* lit, size_t nlit, size_t distance,
                       size_t len) {
    if (*op >= cap) {
        return 0;
    }
    size_t token = (*op)++;
    size_t m = len ? len - MIN_MATCH : 0;
    out[token] = (uint8_t)((nlit < 15 ? nlit : 15) << 4 | (m < 15 ? m : 15));
    if (nlit >= 15 && !putLength(out, op, cap, nlit - 15)) {
        return 0;
    }
    if (*op + nlit > cap) {
        return 0;
    }
    memcpy(out + *op, lit, nlit);
    *op += nlit;
    if (len == 0) {
        return 1;
    }
    if (*op + 2 > cap) {
        return 0;
    }
    out[(*op)++] = (uint8_t)distance;
    out[(*op)++] = (uint8_t)(distance >> 8);
    return m < 15 || putLength(out, op, cap, m - 15);
}

/* Packs a block into at most cap bytes; 0 when it does not get under cap. */
static size_t packBlock(const uint8_t* in, size_t n, uint8_t* out, size_t cap) {
    uint32_t local[1u << HASH_BITS]; /* position + 1, 0 empty (64 KB: each call its own, threads pack too) */
    size_t ip = 0, anchor = 0, op = 0;
    unsigned misses = 0;

    memset(local, 0, sizeof(local));
    if (n < MIN_PACK) {
        return 0;
    }
    size_t limit = n - TAIL - MIN_MATCH;
    while (ip < limit) {
        uint32_t v = read32(in + ip);
        uint32_t h = hash4(v);
        size_t cand = local[h];
        local[h] = (uint32_t)ip + 1;
        if (cand != 0 && ip - (cand - 1) <= MAX_DISTANCE && read32(in + cand - 1) == v) {
            size_t ref = cand - 1;
            while (ip > anchor && ref > 0 && in[ip - 1] == in[ref - 1]) {
                ip--;
                ref--;
            }
            size_t len = MIN_MATCH;
            size_t most = n - TAIL - ip;
            while (len + 8 <= most && read64(in + ip + len) == read64(in + ref + len)) {
                len += 8;
            }
            while (len < most && in[ip + len] == in[ref + len]) {
                len++;
            }
            if (!putSequence(out, &op, cap, in + anchor, ip - anchor, ip - ref, len)) {
                return 0;
            }
            ip += len;
            anchor = ip;
            if (ip >= 2 && ip - 2 < limit) {
                local[hash4(read32(in + ip - 2))] = (uint32_t)(ip - 2) + 1;
            }
            misses = 0;
        } else {
            ip += 1 + (misses++ >> 6);
        }
    }
    if (!putSequence(out, &op, cap, in + anchor, n - anchor, 0, 0)) {
        return 0;
    }
    return op;
}

size_t statePack(const uint8_t* in, size_t n, uint8_t* out) {
    size_t op = 0;
    for (size_t at = 0; at < n || (n == 0 && at == 0); at += STATE_PACK_BLOCK) {
        size_t raw = n - at < STATE_PACK_BLOCK ? n - at : STATE_PACK_BLOCK;
        size_t packed = raw ? packBlock(in + at, raw, out + op + 8, raw - 1) : 0;
        if (packed == 0) {
            memcpy(out + op + 8, in + at, raw);
            packed = raw;
        }
        put32(out + op, (uint32_t)raw);
        put32(out + op + 4, (uint32_t)packed);
        op += 8 + packed;
        if (n == 0) {
            break;
        }
    }
    return op;
}

/* An extra length; -1 past the end of the input. */
static long getLength(const uint8_t* in, size_t size, size_t* ip) {
    long n = 0;
    uint8_t b;
    do {
        if (*ip >= size) {
            return -1;
        }
        b = in[(*ip)++];
        n += b;
    } while (b == 255 && n < (1L << 30));
    return n;
}

static int unpackBlock(const uint8_t* in, size_t size, uint8_t* out, size_t raw) {
    size_t ip = 0, op = 0;
    while (ip < size) {
        uint8_t token = in[ip++];
        size_t nlit = token >> 4;
        if (nlit == 15) {
            long more = getLength(in, size, &ip);
            if (more < 0) {
                return 0;
            }
            nlit += (size_t)more;
        }
        if (nlit > size - ip || nlit > raw - op) {
            return 0;
        }
        memcpy(out + op, in + ip, nlit);
        ip += nlit;
        op += nlit;
        if (ip == size) {
            break; /* the last sequence: literals only */
        }
        if (size - ip < 2) {
            return 0;
        }
        size_t distance = (size_t)in[ip] | (size_t)in[ip + 1] << 8;
        ip += 2;
        size_t len = (size_t)(token & 15) + MIN_MATCH;
        if ((token & 15) == 15) {
            long more = getLength(in, size, &ip);
            if (more < 0) {
                return 0;
            }
            len += (size_t)more;
        }
        if (distance == 0 || distance > op || len > raw - op) {
            return 0;
        }
        uint8_t* d = out + op;
        const uint8_t* s = d - distance;
        if (distance == 1) {
            memset(d, *s, len);
        } else if (distance >= 8) {
            size_t i = 0;
            for (; i + 8 <= len; i += 8) {
                memcpy(d + i, s + i, 8);
            }
            for (; i < len; i++) {
                d[i] = s[i];
            }
        } else {
            for (size_t i = 0; i < len; i++) {
                d[i] = s[i];
            }
        }
        op += len;
    }
    return op == raw;
}

int stateUnpack(const uint8_t* in, size_t size, uint8_t* out, size_t rawSize) {
    size_t ip = 0, op = 0;
    while (ip < size) {
        if (size - ip < 8) {
            return 0;
        }
        size_t raw = get32(in + ip);
        size_t packed = get32(in + ip + 4);
        ip += 8;
        if (raw > STATE_PACK_BLOCK || packed > size - ip || raw > rawSize - op || packed > raw) {
            return 0;
        }
        if (packed == raw) {
            memcpy(out + op, in + ip, raw);
        } else if (!unpackBlock(in + ip, packed, out + op, raw)) {
            return 0;
        }
        ip += packed;
        op += raw;
    }
    return op == rawSize;
}

/* ---- the hash ---- */

static uint64_t rotl(uint64_t v, int r) {
    return v << r | v >> (64 - r);
}

#define P1 0x9E3779B185EBCA87ull
#define P2 0xC2B2AE3D27D4EB4Full
#define P3 0x165667B19E3779F9ull

static uint64_t lane(uint64_t acc, uint64_t w) {
    return rotl(acc + w * P2, 31) * P1;
}

uint64_t stateHash(const void* data, size_t n, uint64_t seed) {
    const uint8_t* p = data;
    uint64_t a = seed + P1 + P2, b = seed + P2, c = seed, d = seed - P1;
    size_t i = 0;
    for (; i + 32 <= n; i += 32) {
        a = lane(a, read64(p + i));
        b = lane(b, read64(p + i + 8));
        c = lane(c, read64(p + i + 16));
        d = lane(d, read64(p + i + 24));
    }
    uint64_t h = rotl(a, 1) + rotl(b, 7) + rotl(c, 12) + rotl(d, 18) + (uint64_t)n;
    for (; i + 8 <= n; i += 8) {
        h = rotl(h ^ lane(0, read64(p + i)), 27) * P1 + P3;
    }
    for (; i < n; i++) {
        h = rotl(h ^ (p[i] * P3), 11) * P1;
    }
    h ^= h >> 33;
    h *= P2;
    h ^= h >> 29;
    h *= P3;
    h ^= h >> 32;
    return h;
}
