#include "field_locations.h"
#include <array>
#include <initializer_list>

namespace extras {
namespace {
std::array<FieldLocation, 730> locations() {
    std::array<FieldLocation, 730> out{};
    for (auto& l : out) l = {"Unidentified locations", "Unidentified room"};
    auto set = [&](int first, int last, const char* area, const char* room) {
        for (int n = first; n <= last; ++n) out[n] = {area, room};
    };
    auto rooms = [&](int first, const char* area, std::initializer_list<const char*> names) {
        for (const char* room : names) out[first++] = {area, room};
    };
    // Descriptions of the player's own maps, not a bundled community naming
    // catalogue. Native captures and naming evidence: docs/PORT_EXTRAS.md.
    rooms(0, "Development", {"Party and sound tests"});
    rooms(1, "Lahan Village", {"Village square", "Village square", "Village outskirts", "Opening scene",
        "Training house", "General store", "Rock-paper-scissors house", "Rock-paper-scissors cellar",
        "Tavern", "Tavern bedroom", "Alice's house", "Alice's room", "Fei's room", "Fei's painting"});
    rooms(15, "Mountain Path", {"Trail", "Gear flyover"});
    rooms(17, "Citan's House", {"Garden", "Bedroom", "Kitchen", "Upper floor", "Workshop"});
    rooms(22, "Blackmoon Forest", {"Forest entrance", "Large tree", "Rankar clearing", "Campsite", "Campsite"});
    rooms(27, "Elly's memories", {"Gear flight", "Crash site"});
    rooms(29, "Dazil", {"Water pump", "Soldiers' lodging", "House with dining table", "House with long bench",
        "Two-bed house", "House with counter", "Waterworks yard", "Vehicle hire", "Excavation office",
        "Guard post", "Dig-site overlook", "Tavern"});
    set(41, 41, "Lahan Village", "Weltall cockpit");
    set(42, 42, "Development", "Untextured Gear room");
    rooms(43, "Dazil", {"Tavern bedroom", "Gear equipment", "Ethos repair bay"});
    rooms(46, "Duneman Isle", {"Above the sandfall", "Below the sandfall", "Sand dunes"});
    rooms(49, "Aveh Desert", {"Shevat flyover", "Motorcycle trail"});
    set(51, 52, "Duneman Isle", "Skeleton remains");
    rooms(53, "Aveh Desert", {"Dune approach", "Night camp", "Grahf encounter"});
    rooms(56, "Aveh transport", {"Prison cabin", "Cargo hold", "Machinery", "Crane deck", "Prison cabin", "Upper deck"});
    set(62, 62, "Aveh Desert", "Brigandier cockpit");
    set(63, 63, "Aveh transport", "Upper deck");
    rooms(64, "Stalactite Cavern", {"Entry cavern", "Mining tunnel", "Balthasar's ledge", "Narrow passage",
        "Sloping passage", "Mine workings", "Lift machinery", "Balthasar's workshop", "Cave approach", "Cave mouth"});
    rooms(74, "Bart's Lair", {"Living-area passage", "Mess", "Sleeping quarters", "Bart's quarters",
        "Briefing room", "Ship dock", "Ship dock", "Excavated wall", "Hangar catwalk", "Hangar floor", "Gear lift"});
    rooms(85, "Nisan", {"Shop counter", "House with balcony", "Guesthouse", "Assembly hall", "Planning map",
        "Small bedroom", "Painter's house", "Stone house", "Town gate", "Town overview", "Cathedral aisle",
        "Hidden ship dock", "Cathedral bookshelves", "Sophia's portrait", "Cathedral stairs", "Cathedral stairs"});
    rooms(101, "Bledavik", {"City overview", "Southern market", "Southern market", "Inn reception", "Sisters' guestroom",
        "Inn upper guestroom", "Inn lower guestroom", "Inn south guestroom", "Schemer's house", "Schemer's upper floor",
        "Soldier's home", "Soldier's upper floor", "Armorer's home", "Armor counter", "Conscript's home",
        "Entertainment house", "Entertainment upper floor", "Bar", "Medicine counter", "Weapon counter",
        "Gear service bay", "Gear office", "Bar", "Festival approach", "Festival approach", "Festival games",
        "Flag game", "Balloon game", "Fortune-telling stage"});
    set(130, 130, "Bledavik sewers", "Inn access ladder");
    rooms(131, "Aveh Castle", {"Tournament registration", "Courtyard", "Waterway gate", "Waterway gate", "Main stair hall",
        "South wing, west bedroom", "South wing, east bedroom", "East wing, fireplace room", "East wing, bedroom",
        "East wing, weapons room", "Vanderkaum's office", "East wing, minister's office", "West wing, kitchen",
        "West wing, refectory", "West wing, bookshelves", "West wing, minister's office"});
    set(147, 147, "Bledavik sewers", "Castle access ladder");
    rooms(148, "Aveh Castle", {"Joe's tournament tent", "Dan's tournament tent", "Royal audience hall", "Royal banquet room",
        "Tower passage", "Royal bedchamber", "Margie's quarters", "Shakhan's quarters", "Ground-floor landing",
        "Second-floor landing", "Third-floor landing", "Fourth-floor landing", "Ramsus' quarters"});
    set(161, 161, "Ramsus' memories", "Elru battlefield");
    rooms(162, "Gebler headquarters", {"Meeting table", "Airship access passage", "Entry passage", "Elly's cabin",
        "Lower passage", "Lift cabin", "Gear loading bay", "Flight deck", "Loading-bay side passage", "Large hangar"});
    rooms(172, "Aveh mountains", {"Lower cave", "Upper cave", "Elly's ambush", "Mountain trail"});
    set(176, 176, "Elly's memories", "Drive experiment");
    rooms(177, "Aveh mountains", {"After the Gear battle", "Summit overlook"});
    set(179, 179, "Gebler flagship", "Bridge");
    set(180, 181, "Aveh Desert", "Flagship pursuit");
    set(182, 182, "Aveh Desert", "Flagship encounter");
    set(183, 183, "Yggdrasil", "Desert battle");
    rooms(184, "Aveh Desert", {"Dora deployment", "Flagship wreck", "Gear clash"});
    set(187, 187, "Gebler flagship", "Upper deck");
    set(188, 188, "Aveh Desert", "Id's assault");
    set(189, 189, "Yggdrasil", "Desert battle");
    set(190, 190, "Aveh Desert", "Id's assault");
    set(191, 191, "Yggdrasil", "Desert battle");
    set(192, 192, "Aveh Desert", "Gebler retreat");
    set(193, 193, "Gebler flagship", "Bridge");
    set(194, 194, "Nortune", "City overview");
    rooms(195, "Nortune", {"A Block street", "D Block street", "Freight train", "City overview", "Baptism yard",
        "Doctor's clinic", "Clinic washroom", "Vehicle house", "Vehicle house, rear room", "Vehicle house, upper floor",
        "Wildcat counter", "Weapon dealer", "Medicine dealer", "Small house", "Arena lobby", "Ethos service room",
        "Battlers' dining room", "Battlers' equipment room", "Battlers' ward", "Battlers' washroom",
        "Children's home", "Children's upper floor", "Latina's counter", "Latina's upper floor", "Rico's bedroom",
        "D Block house", "Arena registration", "Arena lobby", "Arena Gear bay", "Rico's rescue", "Rico's rescue"});
    rooms(226, "Nortune palace", {"Kitchen passage", "Kitchen", "Storage bay", "Rico's childhood home", "Front carpeted hall",
        "Guard hall", "Goliath observation room", "Stairs", "Story scenes", "Operations room", "Queen's sitting room", "Sigmund's office"});
    set(238, 238, "Development", "Dark test room");
    rooms(239, "Nortune sewers", {"Entry channel", "Locked maintenance door", "Lower access ladder", "Pipe machinery"});
    set(243, 243, "Fei's memories", "Childhood scenes");
    set(244, 244, "Nortune", "Purge skyline");
    rooms(245, "Nortune", {"Weltall cockpit", "Vierge cockpit", "Evacuation street", "Grahf's intervention", "Burning city"});
    rooms(250, "Nortune Gear dock", {"Weltall hangar", "Entry duct", "Crate storeroom", "Loading shaft", "Production floor",
        "Key office", "Hangar staircase", "Factory passage", "Office passage", "Ventilation duct"});
    rooms(260, "Goliath Factory", {"Lower ambush bay", "Upper ambush bay", "Factory approach", "Conveyor controls",
        "Ambush-bay passage", "Reversed conveyor", "Central conveyor", "Final conveyor", "Fis-6 bay", "Boarding deck"});
    rooms(270, "Goliath", {"Cockpit", "Flight"});
    set(272, 272, "Gebler flagship", "Bridge");
    set(273, 274, "Solaris", "Gazel chamber");
    set(275, 275, "Yggdrasil", "Bridge");
    rooms(276, "Aveh Desert", {"Citan's escape", "Dora wreck", "Desert battle"});
    set(279, 280, "Yggdrasil", "Surface approach");
    set(281, 281, "Aveh transport", "Upper deck");
    rooms(282, "Yggdrasil", {"Maison's counter", "Bridge", "Desert briefing", "Upper deck", "Thames boarding", "Bridge",
        "Main stairwell", "Crew bunks", "Bridge", "Gear hangar", "Gear counter", "Weltall berth"});
    set(294, 294, "Shevat battleship", "Bridge");
    rooms(295, "Yggdrasil", {"Engine chamber", "Gear hangar", "Outer walkway", "Rico's cabin", "Medical cabin", "Elly's cabin",
        "Margie's cabin", "Upper lift", "Main stairwell", "Lower lift", "Navigation charts", "Navigation charts", "Navigation charts"});
    rooms(308, "Open sea", {"Floating wreckage", "Floating wreckage"});
    rooms(310, "Thames", {"Salvage crane", "Sea battle", "Gangplank", "Medical room", "Captain's office", "Drinking room",
        "Working deck", "Spiral cargo lift", "Store entrance", "Ship overview", "Working deck", "Card table"});
    set(322, 322, "Gebler destroyer", "Story scenes");
    set(323, 323, "Yggdrasil", "Weltall cockpit");
    rooms(324, "Gebler destroyer", {"Cabin passage", "Control room"});
    rooms(326, "Orphanage", {"Front garden", "Common room", "Primera's bedroom", "Billy's bedroom", "Washroom", "Children's bedroom", "Common room"});
    rooms(333, "Ethos transport", {"Ship exterior", "Wels containment bay", "Gear battle", "Long passage", "Cold storage", "Cargo bay"});
    rooms(339, "Ethos headquarters", {"Front hall", "Etone dormitory", "Bishop's quarters"});
    set(342, 342, "Solaris", "Gazel chamber");
    rooms(343, "Ethos headquarters", {"Medical-wing stairs", "Bishop's landing", "Bishop's stairs", "Lower landing",
        "Upper landing", "Dormitory passage", "Reading room", "Dining tables", "Cooking area", "Medical beds",
        "Medical passage", "Confession booth", "Archive passage", "Holding cell", "Inclined lift, upper", "Inclined lift, lower",
        "Computer archive", "Computer archive"});
    rooms(361, "Ethos excavation", {"Inclined lift, upper", "Inclined lift, lower", "Transfer lobby"});
    set(364, 364, "Ethos excavation", "Story scenes");
    set(365, 365, "Ethos excavation", "Vertical lift");
    set(366, 366, "Solaris", "Krelian's observation room");
    rooms(367, "Zeboim ruins", {"First red airlock", "First cross passage", "First console room", "First staircase, upper",
        "Second red airlock", "Second cross passage", "Second console room", "Save console", "Second staircase, upper",
        "Reactor airlock", "Reactor entry passage", "Nanoreactor chamber", "Emeralda's airlock"});
    set(380, 380, "Yggdrasil", "Medical cabin");
    set(381, 381, "Fei's memories", "Zeboim scenes");
    rooms(382, "Zeboim ruins", {"Lift lobby", "City overlook", "Reactor exit passage", "Reactor pipework", "Vertical lift", "Second staircase, lower"});
    set(388, 388, "Ethos excavation", "Sea battle");
    rooms(389, "Zeboim", {"Kim's laboratory", "Kim's dining room", "Grocery counter", "Grocery upper floor", "Joe's counter",
        "Newsroom approach", "Newsroom", "City plaza", "Subway far platform", "Subway entry platform", "Subway tracks",
        "Christmas plaza", "Christmas restaurant"});
    rooms(402, "Lighthouse", {"Outer doorway", "Underground landing", "Lift shaft"});
    rooms(405, "Babel Tower", {"Outer courtyard", "Entry walkway", "Communications room", "Communications controls",
        "Railing platform", "Tram platform", "Outer shell", "Duct entry", "First long duct", "Second long duct",
        "Stepped duct", "Round duct chamber", "Mirror controls", "Moving platforms", "Moving platforms", "Summit platform",
        "High exterior", "Tram cabin"});
    rooms(423, "Shevat", {"City overview", "Outer tower bridge", "Circular lobby", "City lift", "Lower lobby", "Hidden sage's room",
        "Brothers' bar", "Chu-Chu gathering room", "Fountain courtyard", "Mirror house", "Mirror house, upper floor",
        "Memorial terrace", "City kitchen", "Gear counter", "Dock lift lobby", "Dock lift shaft", "Hangar passage",
        "Yggdrasil dock", "Seibzehn bay", "Achtzehn battle", "Seibzehn launch", "Escape passage, upper",
        "Escape lift, upper", "Escape spiral stairs", "Escape passage, lower", "Escape lift, lower", "Outer escape lift",
        "Escape terrace"});
    rooms(451, "Shevat palace", {"Palace lift", "Royal approach stairs", "Zephyr's audience hall", "Zephyr's audience hall",
        "Zephyr's chambers", "Tower balcony", "Prison approach", "Laboratory approach", "Science chamber"});
    rooms(460, "Shevat palace", {"Tactical consoles", "Records room", "Soldiers' quarters", "Elder's quarters",
        "Engineers' quarters", "Historians' quarters", "Guest lounge", "Prison cages", "Cold-sleep chamber",
        "Containment chamber", "Palace kitchen", "Food-storage tanks", "Escape shaft", "Zephyr's audience hall"});
    set(474, 474, "Nisan", "Town gate");
    rooms(475, "Nisan mausoleum", {"Cave entry", "Rock staircase", "Sealed cave door", "Metal bridge", "Rising lift"});
    rooms(480, "Fort Jasper", {"First lift passage", "Angel seal", "Control walkway", "Surface dome", "Lower lift",
        "Second lift passage", "Bent access passage", "Second seal", "Andvari hangar"});
    set(489, 489, "Development", "Movie test");
    set(490, 490, "Title screen", "Logo scene");
    rooms(491, "Fort Jasper", {"Andvari launch bay", "Circular upper catwalk"});
    set(493, 493, "Nisan", "Assembly hall");
    rooms(494, "Gate Generator 1", {"Treasure cave", "Shakhan's chamber"});
    rooms(496, "Sargasso Point", {"First underwater passage", "Second underwater passage", "First fork approach",
        "Deep underwater passage", "Current junction", "First treasure ledge", "Generator approach", "Drained passage",
        "Current-control ledge"});
    set(505, 505, "Gate Generator 3", "Crescens' chamber");
    set(506, 506, "Solaris", "City exterior");
    set(507, 507, "Krelian's laboratory", "Private laboratory");
    set(508, 508, "Soylent System", "First meeting room");
    rooms(509, "Etrenank", {"Immigration exterior", "Service passage"});
    set(511, 511, "Solaris", "City exterior");
    rooms(512, "Etrenank", {"Immigration waiting room", "First checkpoint", "Ceremony plaza"});
    set(515, 515, "Solaris", "Cain's audience");
    rooms(516, "Etrenank", {"Van Houten exterior", "Van Houten bridge", "Ceremony plaza"});
    rooms(519, "Solaris third class", {"Residential causeway", "Save-point quarters", "Mutated resident's quarters",
        "Vacant quarters", "Frightened resident's quarters", "Elly's shelter", "Waste-pipe access", "Security maze"});
    rooms(527, "Etrenank", {"Second checkpoint", "Immigration desk", "Ceremony plaza", "Ticket desk", "Gear enthusiast's flat",
        "Ticket-giver's flat", "Joe's flat", "Save-point flat", "Apartment foyer", "Apartment lift", "Outer walkway",
        "Van Houten stairs", "Van Houten sitting room", "Van Houten dining room", "Erich's study", "Elly's bedroom", "Van Houten exterior",
        "Waste chute", "Waste shredder", "Waste access ladder", "Food crates", "Food-storage passage"});
    rooms(549, "Soylent System", {"Code junction", "Scanner machinery", "Outer storage", "Entry junction", "Lower inclined lift", "Upper inclined lift"});
    rooms(555, "Krelian's laboratory", {"Front console room", "Entry corridor", "Sloped junction", "Short branch corridor",
        "Wels-cage approach", "Locked sloping corridor", "Upper entry corridor", "Surgery approach", "Save-room approach",
        "Scanner approach", "Musical-lock approach", "Citan's access corridor", "Surgical machinery", "Wels holding cylinders",
        "Specimen platform", "First Wels chamber", "Second Wels chamber", "Omnigear observation", "First computer gallery",
        "Save room", "Second meeting room", "First Anima bench", "Second Anima bench", "Cylindrical passage", "Monitor chamber",
        "Hologram platform", "First pedestal chamber", "Second pedestal chamber", "Third pedestal chamber", "First curved ramp",
        "Second curved ramp", "Third curved ramp", "Merkava projection", "First Animus bench", "Angel samples", "Scanner rings",
        "Monitor chamber", "Private-lab upper catwalk", "Gate chamber", "Private-lab lower catwalk", "Private laboratory",
        "Rendezvous corridor", "Rendezvous east branch", "Rendezvous west branch", "Blind branch", "Cross junction",
        "Landmark approach", "Second landmark room", "Second Animus bench", "Second computer gallery", "Third meeting room", "Side cabin"});
    rooms(607, "Solaris", {"Hammer's confrontation", "Escape causeway", "City assault", "Weltall encounter", "Yggdrasil deck"});
    rooms(612, "Taura's House", {"Weltall upgrade", "Gear pursuit", "Garden", "Cottage", "Nanoreactor", "Dream scenes"});
    set(618, 618, "Nortune", "Fort Hurricane battle");
    set(619, 619, "Mahanon", "Razael's Tree");
    rooms(620, "Ignas Soylent System", {"Treatment room", "Treatment balcony"});
    set(622, 622, "Ignas Soylent System", "Sufal Mass encounter");
    rooms(623, "Ignas Soylent System", {"Lift passage", "Treatment hall"});
    set(625, 625, "Ignas Soylent System", "Sufal Mass encounter");
    set(626, 626, "Ignas Soylent System", "Blue platform");
    set(627, 627, "Anima Relic 1", "Elements encounter");
    rooms(628, "Anima Relic 1", {"Front machine hall", "Fuse cabinet", "Door-B controls", "Lift console", "Main access corridor",
        "Branch corridor", "Round lift lobby", "Four-letter code chamber", "Sealed-door walkway", "Deep lift shaft", "Anima chamber"});
    rooms(639, "Anima Relic 2", {"Sealed-door walkway", "Anima chamber", "Entry chasm", "Bridged entry chasm", "Three floor openings",
        "Flooded chasm", "Pit-room approach", "Pitfall chamber", "Spike walkway", "Flood controls", "Return tunnel",
        "Hammer's ambush", "Drained chasm"});
    rooms(652, "Mahanon", {"Eldridge exterior", "Eldridge interior", "Deus chamber", "Razael's Tree", "Grahf's confrontation"});
    set(657, 657, "Golgoda", "Crucifixion site");
    set(658, 658, "Lacan's memories", "Narration scenes");
    rooms(659, "Point of Sephirot", {"Contact chamber", "Crystal cavern", "Crystal cavern"});
    rooms(662, "Fei's memories", {"Childhood room", "Experiment chamber", "Karen's farewell", "Coward's room"});
    set(666, 666, "Point of Sephirot", "Xenogears awakening");
    rooms(667, "Fei's memories", {"Past lives", "Wave Existence"});
    set(669, 669, "Point of Sephirot", "Cavern exit");
    rooms(670, "Lacan's memories", {"Sophia's portrait", "Cathedral organ", "Cathedral approach", "Miang's laboratory",
        "Roni's voyage", "Portrait commission", "Portrait commission", "Battlefield cockpit", "Battlefield", "Sophia's sickroom",
        "Shevat containment", "Sephirot approach", "Sophia's sacrifice", "Ship bridge", "Fei's arrival"});
    rooms(685, "Snowfield Hideout", {"Abandoned Chu-Chu room", "Planning consoles", "Resting alcove", "Mirror junction",
        "Prison approach cave", "Prison landing", "Planning-room approach", "Snow-covered passage", "Swordsmith approach cave",
        "Jukebox alcove", "Central pool", "Cold-sleep chamber", "Midori and Dan's room", "Prison cages", "Mirror room",
        "Zephyr's snow tower", "Swordsmith stairs"});
    rooms(702, "Merkava", {"Outer entry ring", "Inner entry hall", "First laser ledge", "Second laser ledge", "Platform mechanism",
        "North puzzle tunnel", "West puzzle tunnel", "Angel passage", "Ramsus' arena", "Ramsus' aftermath", "Krelian's chamber",
        "Opiomorph platform", "Elly's platform", "Repair-bot approach", "Repair-bot alcove", "Deus core"});
    rooms(718, "Ending", {"Deus pursuit", "Krelian and Urobolus"});
    rooms(720, "Development", {"Kato's planner room", "Eto's planner room", "Kazuomi's planner room", "Suzuki's planner room",
        "Tane's planner room", "Koumoto's planner room", "Kigoshi's planner room", "Uchimichi's planner room",
        "Saito's planner room", "Matu's planner room"});
    return out;
}
const auto names = locations();
const FieldLocation invalid = {"Unidentified locations", "Unidentified room"};
}
const FieldLocation& fieldLocation(int field) {
    return field >= 0 && field < (int)names.size() ? names[field] : invalid;
}
}
