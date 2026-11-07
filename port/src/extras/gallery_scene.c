#include "gallery_scene.h"
#include <string.h>

/* Original presets from local replay evidence (xg-htn.34.3.1). Each reached
 * its SET_GAMEPROGRESS on US and JP with the listed party; the intro also
 * continued into fields 2 and 14. Remaining catalog starts are inferred.
 * Arrival/dinner presets additionally checked against retail US/JP scripts
 * and replay captures (xg-htn.35.81): Citan is supplied by the scene as an
 * NPC, and the dinner starts at entrance 6. These are start parameters,
 * never a captured retail game-state blob. */
static const XgScenePreset presets[] = {
    {3, {4, 0, 0, 1, {0, 2, 255}, 0, {{0, 0}}},
     "The intro runs automatically. Advance dialogue and continue prompts with Circle; this sequence takes several minutes."},
    {6, {14, 1, 3, 1, {0, 2, 255}, 0, {{0, 0}}},
     "The sequence starts automatically. Advance dialogue with the confirm button."},
    /* xg-55m. Field 14's exit leads to entrance 1, with Fei alone after
     * story 6. Field 13 actor 10 waits for trigger 1 before the conversation;
     * confirm presses alone never start it. No carried story flags needed.
     * Walk right from the doorway: Beetle US 7@5669, JP 7@4708. */
    {7, {13, 1, 6, 1, {0, 255, 255}, 0, {{0, 0}}},
     "Walk right from the doorway toward the group to begin the conversation. Advance dialogue with the confirm button."},
    {55, {80, 1, 54, 1, {0, 255, 255}, 0, {{0, 0}}},
     "Arrival at Bart's Lair starts automatically. Citan appears as a scripted NPC; advance dialogue with the confirm button."},
    {56, {74, 6, 55, 1, {0, 255, 255}, 0, {{0, 0}}},
     "The conversation starts automatically. Advance dialogue with the confirm button."},
    /* xg-55m. Field 78's workshop conversation sets variable 225 bit 12
     * and returns to field 74, where Margie's conversation adds bit 9.
     * Retail US/JP then has 0x1202 and Fei alone; field 74 exits to 79/0.
     * Bart needs that carried state. Center on the walkway, walk down into
     * trigger 0 and accept his request to talk before the story advances. */
    {57, {79, 0, 56, 1, {0, 255, 255}, 1, {{225, 4610}}},
     "Walk left onto the center walkway, then down toward Bart. Accept his request to talk and advance dialogue with the confirm button."},
    /* xg-qkf. Field 315's start (actor 0) sets the party and story by
     * entrance: 4 Fei and Elly, story 141 (the way in from field 323); 3 and
     * 8 Fei and Bart, story 145 (from fields 313, 314 and 317). Story 142 is
     * set through entrance 4 (actor 21 runs actor 17's routine 7), not the
     * scanner's entrance 3; 146 through 3 or 8 (actor 21, then field 314).
     * Story 174: field 272 leads to field 411 entrance 2 at story 171; the
     * scene (actor 9) sets 174 before it branches on Elly being in the party
     * (her line); the tower's third member is the player's choice. Each start
     * reached its story in Beetle (US and JP retail) and in Gallery runs. */
    {142, {315, 4, 141, 1, {0, 1, 255}, 0, {{0, 0}}},
     "The scene starts automatically; the field makes the party Fei and Elly. Advance dialogue with the confirm button."},
    {146, {315, 8, 144, 1, {0, 3, 255}, 0, {{0, 0}}},
     "The scene starts automatically; the field makes the party Fei and Bart. Advance dialogue with the confirm button."},
    {174, {411, 2, 171, 1, {0, 1, 255}, 0, {{0, 0}}},
     "The scene starts automatically. Elly in the party gives her lines; advance dialogue with the confirm button."},
    /* xg-55m. Arrivals whose field start (actor 0) sets the party and the
     * story for the entrance: field 315 entrance 4 (Fei, Elly; 141) and 3
     * (Fei, Bart; 145, the way in from field 313), field 312 entrance 2 (Fei,
     * Bart, Citan; 144, from field 319) and field 313 entrance 2 (Elly,
     * Citan, Bart; 148, from field 311). The party given is the one the
     * field sets (also field 412 entrance 4 below). Reached in Beetle (US
     * and JP retail) and in Gallery runs. */
    {141, {315, 4, 140, 1, {0, 1, 255}, 0, {{0, 0}}},
     "The arrival starts automatically. Advance dialogue with the confirm button."},
    {145, {315, 3, 144, 1, {0, 3, 255}, 0, {{0, 0}}},
     "The arrival starts automatically. Advance dialogue with the confirm button."},
    {144, {312, 2, 143, 1, {0, 3, 2}, 0, {{0, 0}}},
     "The arrival starts automatically. Advance dialogue with the confirm button."},
    {148, {313, 2, 147, 1, {1, 2, 3}, 0, {{0, 0}}},
     "The arrival starts automatically. Advance dialogue with the confirm button."},
    /* Field 412 entrance 4 (story 203): Fei, Elly and Citan, on their Gears. */
    {203, {412, 4, 202, 1, {0, 1, 2}, 0, {{0, 0}}},
     "The party arrives on their Gears; the field is then yours to explore."},
    /* Field 297 entrance 0 (story 139): Bart alone on the sand cruiser's deck
     * after Fei and Elly are lost; its start removes Fei and Elly and adds
     * Bart, so they come in as the party. Beetle US 139@1909, JP 139@1770. */
    {139, {297, 0, 138, 1, {0, 1, 255}, 0, {{0, 0}}},
     "The scene starts automatically; the field makes Bart the party. Advance dialogue with the confirm button."},
    /* Field 319 entrance 3 (story 140): Bart, alone (as story 139 leaves
     * him), arrives at the sea platform. Beetle US 140@1876. */
    {140, {319, 3, 139, 1, {3, 255, 255}, 0, {{0, 0}}},
     "The arrival starts automatically; the field is then yours to explore."},
    /* Field 172 entrance 0 (story 90): the Gears' cave after Elly leaves;
     * its start removes Elly from Fei, Elly and Citan. Beetle US 90@1886. */
    {90, {172, 0, 87, 1, {0, 1, 2}, 0, {{0, 0}}},
     "The scene starts automatically; the field takes Elly out of the party. The field is then yours to explore."},
    /* xg-55m. The actual approach is from field 147 at story 90: its init
     * removes Fei/Citan, adds Bart then Citan, and sets variable 42 = 2.
     * Its exit routes through field 108 entrance 7 to field 134 entrance 0.
     * Field 134 needs both party actors; without Bart its scene stalls. */
    {93, {134, 0, 90, 1, {3, 2, 255}, 1, {{42, 2}}},
     "The sequence starts automatically with Bart and Citan. Advance dialogue with the confirm button."},
    /* Fields whose start (actor 0) sets the whole party before the scene,
     * whatever party comes in: Beetle (US and JP retail) from this start
     * with Fei alone, with Fei, Elly and Citan, and with the party below
     * gave that party and the story value each time (US 145@3702,
     * 208@4844, 210@2396; JP 145@3079, 208@3837, 210@2257), and so did
     * Gallery runs. Field 325: Elly alone (it removes the three party
     * slots and adds her); field 453 entrance 3: Fei, Maria and Chu-Chu;
     * field 511: Fei, Citan and Elly. */
    {145, {325, 0, 144, 1, {1, 255, 255}, 0, {{0, 0}}},
     "The scene starts automatically; the field makes Elly the party. Advance dialogue with the confirm button."},
    {208, {453, 3, 207, 1, {0, 8, 7}, 0, {{0, 0}}},
     "The scene starts automatically; the field makes the party Fei, Maria and Chu-Chu. Advance dialogue with the confirm button."},
    {210, {511, 0, 209, 1, {0, 2, 1}, 0, {{0, 0}}},
     "The scene starts automatically; the field makes the party Fei, Citan and Elly. Advance dialogue with the confirm button."},
    /* Party and story variables from the real state before the scene: the
     * previous verified preset run on in Beetle (US RAM): story 142 (315/4)
     * Fei and Elly, variable 42 = 9; story 144 (312/2) Fei, Bart and Citan,
     * variable 198 = 0x8000; story 208 (453/3) arriving at field 452, Fei,
     * Maria and Chu-Chu, variables 360/361 = 8/7. Other differences are the
     * engine's transition slots (2-41). Beetle from these starts: US
     * 143@5078, 145@1855, 209@3333; JP 143@4143, 145@1717, 209@2972. */
    {143, {314, 2, 142, 1, {0, 1, 255}, 1, {{42, 9}}},
     "The scene starts automatically. Advance dialogue with the confirm button."},
    {145, {290, 1, 144, 1, {0, 3, 2}, 1, {{198, 32768}}},
     "The scene starts automatically. Advance dialogue with the confirm button."},
    {209, {452, 0, 208, 1, {0, 8, 7}, 2, {{360, 8}, {361, 7}}},
     "The scene starts automatically. Advance dialogue with the confirm button."},
    /* xg-55m. Fresh regional replay: field 314 entrance 4 sets Fei, Bart
     * and Elly, then removes Elly as its arrival sets story 146. Field 286
     * replaces the incoming party with Fei, Elly and Citan; field 320
     * replaces it with Elly, Citan and Bart. No carried flags are needed. */
    {146, {314, 4, 144, 1, {0, 3, 1}, 0, {{0, 0}}},
     "The arrival starts automatically; the field selects the party. Advance dialogue with the confirm button."},
    {147, {286, 0, 146, 1, {0, 1, 2}, 0, {{0, 0}}},
     "The sequence starts automatically with Fei, Elly and Citan. Advance dialogue with the confirm button."},
    {149, {320, 0, 148, 1, {1, 2, 3}, 0, {{0, 0}}},
     "The conversation starts automatically with Elly, Citan and Bart. Advance dialogue with the confirm button; this sequence takes several minutes."},
    /* Field 420's first event sets variable 266 bit 8 before going to
     * field 421. The return is entrance 1; its actor 18 completes story 175.
     * Field 529's story-213 event uses entrance 4, reached from field 518,
     * and needs Fei and Elly. Citan is supplied by the scene as an NPC. */
    {175, {420, 1, 174, 1, {0, 1, 255}, 2, {{42, 4}, {266, 256}}},
     "The return sequence starts automatically. Advance dialogue with the confirm button."},
    {213, {529, 4, 210, 1, {0, 1, 255}, 0, {{0, 0}}},
     "The sequence starts automatically with Fei and Elly. Advance dialogue with the confirm button."},
    /* xg-55m. Regional early-story replays. The forest returns Fei alone
     * after Elly leaves; field 24 adds Elly at story 32. At story 34 its
     * incoming order is Fei, Citan, Elly, as its story-33 setup selects. */
    {27, {23, 0, 24, 1, {0, 255, 255}, 0, {{0, 0}}},
     "The sequence starts automatically with Fei alone. Advance dialogue with the confirm button."},
    {32, {23, 0, 31, 1, {0, 255, 255}, 0, {{0, 0}}},
     "The sequence starts automatically with Fei alone. Advance dialogue with the confirm button."},
    {33, {24, 0, 32, 1, {0, 255, 255}, 0, {{0, 0}}},
     "The sequence starts automatically; the field adds Elly. Advance dialogue with the confirm button."},
    {36, {24, 0, 34, 1, {0, 2, 1}, 0, {{0, 0}}},
     "The sequence starts automatically with Fei, Citan and Elly. Advance dialogue with the confirm button."},
    {38, {35, 16, 37, 1, {0, 2, 255}, 0, {{0, 0}}},
     "The arrival starts automatically with Fei and Citan. Advance dialogue with the confirm button."},
    /* Measured incoming party and carried slots from the Shevat arrival
     * lead (field 412): field 303 entrance 4, story 203, variables 137/138
     * selecting Elly and Citan. Both regions run this through story 204. */
    {204, {303, 4, 203, 1, {0, 1, 2}, 2, {{137, 1}, {138, 2}}},
     "The sequence starts automatically with Fei, Elly and Citan. Advance dialogue with the confirm button."},
    /* xg-55m. These regional starts also exercise the scripted party
     * changes: field 62 removes Citan, field 72 adds Bart, and field 73
     * replaces Bart with Citan during the scene. */
    {45, {62, 0, 43, 1, {0, 2, 255}, 0, {{0, 0}}},
     "The sequence starts automatically; Citan leaves the party during the scene. Advance dialogue with the confirm button."},
    {48, {72, 0, 45, 1, {0, 255, 255}, 0, {{0, 0}}},
     "The sequence starts automatically; the field adds Bart. Advance dialogue with the confirm button."},
    {54, {73, 0, 52, 1, {0, 3, 255}, 0, {{0, 0}}},
     "The sequence starts automatically with Fei and Bart. Advance dialogue with the confirm button."},
    {61, {104, 0, 60, 1, {0, 3, 2}, 0, {{0, 0}}},
     "The sequence starts automatically with Fei, Bart and Citan. Advance dialogue with the confirm button."},
    /* The entry-2 briefing in field 80 hides the incoming party and uses
     * its own cast. An 8000-vblank pilot ended just before the US target;
     * longer fresh US/JP runs reach it without additional story flags. */
    {59, {80, 2, 58, 1, {0, 255, 255}, 0, {{0, 0}}},
     "The briefing starts automatically. Advance dialogue with the confirm button; this sequence takes several minutes."},
    {105, {218, 0, 102, 1, {0, 255, 255}, 0, {{0, 0}}},
     "The sequence starts automatically with Fei alone; Rico joins during playback. Advance dialogue with the confirm button."},
    {129, {225, 0, 123, 1, {0, 1, 5}, 0, {{0, 0}}},
     "The sequence starts automatically with Fei, Elly and Rico. Advance dialogue with the confirm button."},
    /* The captain's first conversation needs Fei and Elly. Entrance 0 is
     * a neutral replay position: walk right and talk to him. The old
     * entrance-2/Bart lead did not reach this story operation. */
    {141, {314, 0, 140, 1, {0, 1, 255}, 0, {{0, 0}}},
     "Walk right toward the captain and talk to him. Advance dialogue with the confirm button."},
    {155, {327, 2, 154, 1, {0, 1, 2}, 0, {{0, 0}}},
     "The sequence starts automatically with Fei, Elly and Citan. Advance dialogue with the confirm button."},
    /* Field 327's party-selection branch removes Fei and Citan and adds
     * Elly, the selected companion, Bart and Rico. This replay selects
     * Elly, Bart and Rico for the following field-335 conversation. */
    {158, {335, 0, 155, 1, {1, 3, 5}, 0, {{0, 0}}},
     "The sequence starts automatically with Elly, Bart and Rico. Advance dialogue with the confirm button."},
    {160, {360, 0, 158, 1, {0, 1, 2}, 0, {{0, 0}}},
     "The sequence starts automatically with Fei, Elly and Citan. Advance dialogue with the confirm button."},
    {167, {293, 0, 166, 1, {0, 1, 2}, 0, {{0, 0}}},
     "The sequence starts automatically with Fei, Elly and Citan. Advance dialogue with the confirm button."},
    /* Fresh field-420/273 playback reaches field 437 entrance 3 at story
     * 175 with Fei/Elly and these three carried values in both regions. */
    {176, {437, 3, 175, 1, {0, 1, 255}, 3, {{42, 2}, {255, 512}, {266, 256}}},
     "The sequence starts automatically with Fei and Elly. Advance dialogue with the confirm button."},
    {177, {423, 0, 176, 1, {0, 1, 2}, 0, {{0, 0}}},
     "The sequence starts automatically with Fei, Elly and Citan. Advance dialogue with the confirm button."},
    {181, {473, 0, 180, 1, {0, 1, 2}, 0, {{0, 0}}},
     "The sequence starts automatically with Fei, Elly and Citan. Advance dialogue with the confirm button."},
    /* Later saved candidates, replayed from these same starts on both
     * retail regions. The two story-234 operations belong to different
     * fields and remain separate presets. */
    {216, {538, 0, 213, 1, {0, 1, 255}, 0, {{0, 0}}},
     "The sequence starts automatically with Fei and Elly. Advance dialogue with the confirm button."},
    {228, {579, 0, 222, 1, {0, 1, 255}, 0, {{0, 0}}},
     "The sequence starts automatically with Fei and Elly. Advance dialogue with the confirm button."},
    {234, {593, 0, 231, 1, {0, 255, 255}, 0, {{0, 0}}},
     "The sequence starts automatically. Advance dialogue with the confirm button."},
    {234, {595, 0, 231, 1, {0, 255, 255}, 0, {{0, 0}}},
     "The sequence starts automatically with Fei alone. Advance dialogue with the confirm button."},
    {242, {460, 0, 241, 1, {0, 1, 255}, 0, {{0, 0}}},
     "The sequence starts automatically with Fei and Elly. Advance dialogue with the confirm button."},
    {243, {467, 0, 242, 1, {0, 1, 255}, 0, {{0, 0}}},
     "The sequence starts automatically with Fei and Elly. Advance dialogue with the confirm button."},
    /* Disc 2. Fields 709, 711 and 712 set up their Gear presentation
     * themselves. Story 301 uses field 713 entrance 0, not the scanner's
     * entrance 1. Before field 714, the preceding script selects Billy
     * and Emeralda alongside Fei. The separate field-713 story-303 return
     * still has no verified start and retains override protection. */
    {298, {709, 1, 295, 2, {0, 255, 255}, 0, {{0, 0}}},
     "The arrival starts automatically with Fei alone. Advance dialogue with the confirm button."},
    {300, {711, 0, 298, 2, {0, 255, 255}, 0, {{0, 0}}},
     "The sequence starts automatically with Fei alone. Advance dialogue with the confirm button."},
    {301, {713, 0, 300, 2, {0, 255, 255}, 0, {{0, 0}}},
     "The sequence starts automatically with Fei alone. Advance dialogue with the confirm button."},
    {302, {712, 0, 301, 2, {0, 255, 255}, 0, {{0, 0}}},
     "The sequence starts automatically with Fei alone. Advance dialogue with the confirm button."},
    {303, {714, 0, 302, 2, {0, 4, 9}, 0, {{0, 0}}},
     "The sequence starts automatically with Fei, Billy and Emeralda. Advance dialogue with the confirm button; this sequence takes several minutes."},
};

const XgScenePreset* xgScenePreset(const XgScene* scene, const char* region) {
    if (scene == NULL || region == NULL || (strcmp(region, "US") && strcmp(region, "JP"))) return NULL;
    for (unsigned i = 0; i < sizeof(presets) / sizeof(presets[0]); i++) {
        const XgScenePreset* p = &presets[i];
        if (scene->field == p->start.field && scene->setsTo == p->setsTo && scene->disc == p->start.disc) return p;
    }
    return NULL;
}

void xgSceneStart(const XgScene* scene, const char* region, PortStartRequest* out) {
    const XgScenePreset* p = xgScenePreset(scene, region);
    if (p != NULL) { *out = p->start; return; }
    memset(out, 0, sizeof(*out));
    out->field = scene->field; out->entry = scene->entry;
    out->progress = scene->progress; out->disc = scene->disc;
    out->party[0] = 0; out->party[1] = 2; out->party[2] = 255;
}

int xgScenePresetMatches(const XgScenePreset* p, const PortStartRequest* r) {
    if (p == NULL || r == NULL || r->field != p->start.field || r->entry != p->start.entry ||
        r->progress != p->start.progress || r->disc != p->start.disc ||
        r->variableCount != p->start.variableCount) return 0;
    for (int i = 0; i < 3; i++) if (r->party[i] != p->start.party[i]) return 0;
    for (int i = 0; i < r->variableCount; i++) {
        if (r->variables[i].index != p->start.variables[i].index ||
            r->variables[i].value != p->start.variables[i].value) return 0;
    }
    return 1;
}

const char* xgSceneStartProblem(const PortStartRequest* r) {
    /* Entrances no start may use, whatever its state. None now: the cold
     * starts that crashed (field 315 entrances 3 and 8, field 411 entrance 2;
     * xg-n1s, xg-qkf) were PC defects, not missing state (a sub-sprite buffer
     * overrun and a store through a freed gear's slot, both fixed in the game
     * C), and retail runs the same starts to their story values. */
    (void)r;
    return NULL;
}

const char* xgSceneStartProtection(const XgScenePreset* p, const PortStartRequest* r, int override) {
    const char* problem = xgSceneStartProblem(r);
    if (problem != NULL) return problem;
    if (r == NULL) return "No scene start selected.";
    if (!override && !xgScenePresetMatches(p, r))
        return p != NULL ? "Restore the verified preset or enable Override unverified start to customize it."
                         : "This scene has no verified preset. Enable Override unverified start to try it.";
    return NULL;
}
