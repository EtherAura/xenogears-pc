// Verified OST metadata for xg-htn.35.6. Titles use the publisher's current
// English/Japanese listings, SQEX-10043-44 (checked):
// https://www.jp.square-enix.com/music/en/lineup/item/SQEX-10043-44.html
// https://www.jp.square-enix.com/music/lineup/item/SQEX-10043-44.html
//
// Score identity was verified by exact SMDS byte matches against the numbered
// album tracks of Vincent "Yaz0r" Hamm's original PSF rip (CaitSith2 credits):
// https://caitsith2.com/psf/index.htm
// https://tzone.org/~llin/psf/packs2/Xenogears_psf.rar
// Archive MD5: 823319b2670d9e71ba7e2f609bb24b89.
// Only metadata/fingerprints are included here. No PSF or game data is bundled.
// Unnumbered ambience/variants are deliberately unmapped; neither a shorthand
// name nor a music slot establishes OST identity. Hash and length come from
// the full SMDS file (the jukebox's existing FNV-1a, including its offset basis).

#include "jukebox_titles.h"

namespace extras {
namespace {
const JukeboxTitle titles[] = {
    {0xbdc4f25f7960399full, 7688, 1, 4, "Village Pride", "おらが村は世界一"}, // #00 lahan
    {0xa30ab478a48cb8b8ull, 5376, 1, 21, "Windy Song", "やさしい風がうたう"}, // #04 minato
    {0xb4b829e8fb50544cull, 4368, 1, 5, "Wind from the Valley", "風のうまれる谷"}, // #07 shitan
    {0x159c30f3242236f6ull, 1652, 1, 6, "A Distant Promise", "遠い約束"}, // #08 orgel
    {0xb1146ffe382f80ceull, 1708, 1, 22, "We Wounded Follow the Light", "傷もてるわれら　光のなかを進まん"}, // #10 church
    {0x9c39e0abca600323ull, 6496, 1, 7, "Steel Giants", "鋼の巨人"}, // #11 battle2
    {0xc3e5ca959e3f1a4eull, 464, 1, 10, "Doze (Short Version)", "まどろみ(ショートバージョン)"}, // #12 sleeps
    {0x24704a131a8a0eebull, 956, 2, 12, "Doze (Long Version)", "まどろみ(ロングバージョン)"}, // #13 sleepl
    {0x517843508c06d465ull, 1044, 1, 20, "Dark Slumber", "暗き眠りに……"}, // #14 over
    {0x0b67bd555e78f22aull, 4684, 2, 6, "With the Sky, the Clouds, and You", "大空と雲ときみと"}, // #15 chuchu
    {0xef5bc39c86117891ull, 7376, 2, 3, "Blazing Knights", "紅蓮の騎士"}, // #20 battle3
    {0x53b7a0ad627086ccull, 12440, 1, 15, "Dreams of the Brave", "つわものどもが夢のあと"}, // #22 ajito
    {0xbd38e4cd13c8b0f6ull, 4196, 2, 4, "October Mermaid", "神無月の人魚"}, // #25 emerada
    {0x89b4084b5c3e2f85ull, 7132, 1, 19, "Deadly Dance", "死の舞踏"}, // #29 battle1
    {0x3e9e523212248b97ull, 4496, 1, 9, "Where Dreams Hatch", "夢の卵の孵るところ"}, // #30 ellie
    {0x34994c0b20266f7bull, 2172, 2, 8, "Earthly Tears, Mortal Thoughts", "星の涙、人の想い"}, // #35 sad
    {0x0baf209379dc4682ull, 6720, 1, 17, "Ancient Dance of Aveh", "アヴェ　いにしえの舞"}, // #38 ave
    {0x71dfed6a4c116411ull, 2264, 1, 23, "Lost... -Screeching Shards-", "lost…　きしんだ　かけら"}, // #39 ellie2
    {0xf120431e989db949ull, 5172, 1, 12, "Adoration", "憧憬"}, // #40 world
    {0xb14ba8900c273336ull, 2740, 1, 3, "Bond of Sea and Flame", "海と炎の絆"}, // #41 balto
    {0x66b71ab459d6500eull, 5160, 1, 11, "Desert City Dazil", "熱砂の街ダジル"}, // #42 dajil
    {0x3decdd42ef39ed9dull, 3192, 2, 7, "Gather Up the Night Stars", "夜空一杯の星を集めて"}, // #43 maria1
    {0xcabaec2abb65de81ull, 10780, 2, 9, "Soaring", "飛翔"}, // #44 maria2
    {0x4192ee88e1167d7eull, 5304, 1, 24, "The Thames -Men of the Sea-", "タムズ　海の男の心意気"}, // #45 heshu
    {0x7d9a3a46ac64b2fbull, 3556, 1, 16, "Intangible Treasure", "盗めない宝石"}, // #46 kaisou
    {0x9e67aa1f2e23361cull, 2600, 2, 14, "Prayers -The Joy of Hope-", "祈り、人の望みの喜びよ"}, // #47 porgan
    {0xe6d8ea779b2dbfbaull, 5732, 2, 15, "Foreboding", "予感"}, // #56 melkaba
    {0x0b4088619c6fe13eull, 2972, 2, 1, "Cage of Remorse and Relief", "悔恨と安らぎの檻にて"}, // #57 solachu
    {0x832e8a7d9ce38cd9ull, 4744, 1, 14, "Fuse", "導火線"}, // #58 pinch
    {0x61e0784da1818795ull, 2268, 1, 18, "Infiltration", "侵入"}, // #60 shinnyu
    {0x5225051e84b4eecaull, 2352, 2, 2, "Icy Chin", "氷の顎"}, // #61 inbou
    {0x29e8bca98eca3dd7ull, 2296, 2, 13, "Torn", "引き裂かれしもの"}, // #62 ido
    {0xb1d136449b7be23eull, 9404, 1, 25, "Blue Traveler", "蒼き旅人"}, // #63 takeoff
    {0xec455bc7c26e83cbull, 6192, 2, 10, "Wings", "翼"}, // #64 hikou
    {0xd1585d15d9e3bb89ull, 5044, 2, 17, "Fangs Bared at God", "神に牙むくもの"}, // #65 last
    {0xc9516c0e420bbf44ull, 6264, 1, 13, "Grahf -Ruler of Darkness-", "グラーフ　闇の覇者"}, // #66 glaerf
    {0x3088c065616af5a1ull, 4552, 1, 8, "The Blackmoon Forest", "黒月の森"}, // #67 jyukai2
    {0x6f77232a5c1b6282ull, 5320, 2, 16, "Awakening", "覚醒"}, // #68 lastbat
    {0x0108597fab42501full, 10776, 2, 5, "Shevat -The Wind Calls-", "風が呼ぶ、蒼穹のシェバト"}, // #69 shebat
    {0x186b46a89dd122cfull, 7804, 2, 11, "Solaris -Supernal Paradise-", "天上の楽園ソラリス"}, // #72 solaris
};
}

const JukeboxTitle* jukeboxTitle(uint64_t hash, int bytes) {
    for (const auto& title : titles) {
        if (title.hash == hash && title.bytes == bytes) return &title;
    }
    return nullptr;
}
} // namespace extras
