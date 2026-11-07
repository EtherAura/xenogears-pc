#include "field_locations.h"
#include <cstdio>
#include <cstring>

int main() {
    int checks = 0, failures = 0;
    auto check = [&](bool okay, const char* message) {
        ++checks;
        if (!okay) { ++failures; std::fprintf(stderr, "%s\n", message); }
    };
    for (int i = 0; i < 730; ++i) {
        const auto& l = extras::fieldLocation(i);
        check(l.area && *l.area && l.room && *l.room, "Missing location");
        check(std::strcmp(l.area, "Unidentified locations") != 0, "Unidentified catalogue field");
    }
    auto named = [&](int n, const char* area, const char* room) {
        const auto& l = extras::fieldLocation(n);
        check(!std::strcmp(l.area, area) && !std::strcmp(l.room, room), "Wrong location boundary");
    };
    named(13, "Lahan Village", "Fei's room");
    named(18, "Citan's House", "Bedroom");
    named(19, "Citan's House", "Kitchen");
    named(100, "Nisan", "Cathedral stairs");
    named(125, "Bledavik", "Festival approach");
    named(147, "Bledavik sewers", "Castle access ladder");
    named(160, "Aveh Castle", "Ramsus' quarters");
    named(171, "Gebler headquarters", "Large hangar");
    named(225, "Nortune", "Rico's rescue");
    named(259, "Nortune Gear dock", "Ventilation duct");
    named(293, "Yggdrasil", "Weltall berth");
    named(307, "Yggdrasil", "Navigation charts");
    named(360, "Ethos headquarters", "Computer archive");
    named(422, "Babel Tower", "Tram cabin");
    named(450, "Shevat", "Escape terrace");
    named(548, "Etrenank", "Food-storage passage");
    named(606, "Krelian's laboratory", "Side cabin");
    named(638, "Anima Relic 1", "Anima chamber");
    named(651, "Anima Relic 2", "Drained chasm");
    named(684, "Lacan's memories", "Fei's arrival");
    named(701, "Snowfield Hideout", "Swordsmith stairs");
    named(717, "Merkava", "Deus core");
    named(729, "Development", "Matu's planner room");
    check(!std::strcmp(extras::fieldLocation(102).room, extras::fieldLocation(103).room), "Market variants split");
    check(std::strcmp(extras::fieldLocation(105).room, extras::fieldLocation(106).room), "Separate inn rooms combined");
    check(std::strcmp(extras::fieldLocation(300).room, extras::fieldLocation(301).room), "Separate cabins combined");
    check(std::strcmp(extras::fieldLocation(581).room, extras::fieldLocation(582).room), "Separate pedestal rooms combined");
    check(!std::strcmp(extras::fieldLocation(-1).room, "Unidentified room"), "Negative field not guarded");
    check(!std::strcmp(extras::fieldLocation(730).room, "Unidentified room"), "Oversized field not guarded");
    std::printf("field locations: %d checks, %d failures\n", checks, failures);
    return failures != 0;
}
