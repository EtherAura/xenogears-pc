/* Original 3x5 keycap lettering. Pure unsigned arithmetic is shared with C
 * so the 1x cache, native dialogue, ImGui and Vulkan use the same pixels. */
#ifdef __STDC_VERSION__
#define P_UINT uint32_t
#define P_INLINE static inline
#else
#ifdef __cplusplus
#define P_UINT uint32_t
#define P_INLINE static inline
#else
#define P_UINT uint
#define P_INLINE
#endif
#endif

P_INLINE P_UINT promptLetter(P_UINT c) {
    switch (c) {
    case 65u: return 0x5beau;
    case 66u: return 0x3aebu;
    case 67u: return 0x624eu;
    case 68u: return 0x3b6bu;
    case 69u: return 0x72cfu;
    case 70u: return 0x12cfu;
    case 71u: return 0x6b4eu;
    case 72u: return 0x5bedu;
    case 73u: return 0x7497u;
    case 75u: return 0x5aedu;
    case 76u: return 0x7249u;
    case 77u: return 0x5bfdu;
    case 78u: return 0x5ffdu;
    case 79u: return 0x2b6au;
    case 80u: return 0x12ebu;
    case 82u: return 0x5aebu;
    case 83u: return 0x388eu;
    case 84u: return 0x2497u;
    case 85u: return 0x7b6du;
    case 86u: return 0x2b6du;
    case 87u: return 0x5fedu;
    case 88u: return 0x5aadu;
    case 89u: return 0x24adu;
    case 90u: return 0x72a7u;
    case 49u: return 0x749au;
    case 50u: return 0x72a3u;
    case 51u: return 0x38a3u;
    case 52u: return 0x49edu;
    case 53u: return 0x79cfu;
    case 54u: return 0x7bceu;
    case 43u: return 0x05d0u;
    case 45u: return 0x01c0u;
    case 60u: return 0x4454u;
    case 62u: return 0x1511u;
    case 94u: return 0x002au;
    case 118u: return 0x2a00u;
    default: return 0x2083u;
    }
}

/* Must stay in system/prompt.h's enum order, beginning at PAD_GLYPH_A. */
P_INLINE P_UINT promptLabel(P_UINT id) {
    switch (id) {
    case 4u: return 65u; case 5u: return 66u; case 6u: return 88u; case 7u: return 89u;
    case 8u: return 0x314cu; case 9u: return 0x3152u;
    case 10u: return 0x324cu; case 11u: return 0x3252u;
    case 12u: return 0x424cu; case 13u: return 0x4252u;
    case 14u: return 0x544cu; case 15u: return 0x5452u;
    case 16u: return 76u; case 17u: return 82u;
    case 18u: return 0x4c5au; case 19u: return 0x525au;
    case 20u: return 0x334cu; case 21u: return 0x3352u;
    case 22u: return 0x5453u; case 23u: return 0x4553u;
    case 24u: return 0x4b42u; case 25u: return 0x4e4du;
    case 26u: return 0x504fu; case 27u: return 0x4853u;
    case 28u: return 0x5243u; case 29u: return 43u; case 30u: return 45u;
    case 31u: return 94u; case 32u: return 118u; case 33u: return 60u; case 34u: return 62u;
    case 35u: return 83u; case 36u: return 69u; case 37u: return 87u; case 38u: return 78u;
    case 39u: return 0x2d2du;
    case 41u: return 0x5756u; case 42u: return 0x4447u; case 43u: return 0x5054u;
    case 44u: return 0x3150u; case 45u: return 0x3250u; case 46u: return 0x3350u; case 47u: return 0x3450u;
    case 48u: return 0x314du; case 49u: return 0x324du; case 50u: return 0x334du;
    case 51u: return 0x344du; case 52u: return 0x354du; case 53u: return 0x364du;
    case 54u: return 0x494du; case 55u: return 0x4143u;
    default: return 63u;
    }
}

P_INLINE P_UINT promptIndex(P_UINT id, P_UINT x, P_UINT y) {
    if (x >= 12u || y >= 12u) return 0u;
    if (x == 0u || x == 11u || y == 0u || y == 11u) return 0u;
    if (x == 1u || x == 10u || y == 1u || y == 10u) return 1u;
    P_UINT label = promptLabel(id), c = label & 255u, start = 4u;
    if (label > 255u) {
        start = x < 6u ? 2u : 6u;
        if (x >= 6u) c = label >> 8u;
    }
    if (x >= start && x < start + 3u && y >= 4u && y < 9u &&
        ((promptLetter(c) >> ((y - 4u) * 3u + x - start)) & 1u) != 0u) return 1u;
    return 2u;
}

P_INLINE P_UINT promptTexel(P_UINT id, P_UINT x, P_UINT y) {
    P_UINT index = promptIndex(id,x,y);
    return index == 1u ? 0x7bdeu : index == 2u ? 0x1084u : 0u;
}
#undef P_UINT
#undef P_INLINE
