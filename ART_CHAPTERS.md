# Art chapters: every US field in one story chapter

The map the HD-art chapters are cut from (issue xg-pse.1). Every US field 0-729 appears exactly once in a
**Fields** column below: in a prepared chapter, a chapter still to prepare, the unresolved list, or the
no-chapter list. A **Revisits** column names fields a chapter also textures whose primary row is elsewhere; it
does not count toward coverage.

Sources (all read-only):
- Prepared rows: `python tools/art_status.py` (counts and state), each plan's `plan.json` page `rooms`
  (fields), and its `specs.txt`/`win-specs.txt` (story points and party) where the chapter has one.
- Story order and story points: the Gallery scene table (`gallery: scene N: field F sets S from P` lines in
  the dazil chapter's `scenes/run.log`). "sN" below is a scene number from that table; "@P" is the story point
  it starts from.
- Party: only the table's `verified scene` lines. A scene's party holds at that scene; it is not proven for the
  whole row. Party ids 0-3 are Fei, Elly, Citan and Bart as the prepared specs use them; higher ids are left as
  numbers; 255 is an empty slot.
- Exits and story-point ops per field, and which disc holds each field: the scans saved by round
  20261113T045241Z (field exits 0-729, per-disc file sizes). These do not read the world map's own field
  entries, so a field with no ordinary exit into it may still be reached from the world map or a script.

Party ids: 0 Fei, 1 Elly, 2 Citan, 3 Bart; 4, 5, 7, 8, 9 not identified here.

## Prepared texture plans

In story order. State and counts as `tools/art_status.py` reported them (installed pack
`lahan-ch1-op-fg-w-dz-dg-cv-cg-lr-bl-ac-ge-ni-mo-fl-8x-v17`).

| Chapter | Fields | Revisits | Story points | Party | Canvases | State |
|---|---|---|---|---|---|---|
| opening | - | 2, 14 | not in a spec | not in a spec | 10/10 | installed |
| lahan | 1-14 | - | 0-8 (scenes s1-s4; no spec) | 0,2,255 at s1-s2; 0 at s3 (scene-verified) | 275/275 | installed |
| lahan-battle | - | 2 | not in a spec | not in a spec | 106/106 | installed |
| ch1b | 15-28 | 1, 3 | 15-37 provisional (scenes s9-s18; no spec) | 0 at s12-s16; 0,2,1 at s18 (scene-verified) | 245/245 | installed |
| forest-gear | - | 16, 22, 27 | not in a spec | not in a spec | 66/66 | installed |
| dazil | 29-40, 43-45 | 49, 50, 53-63 | 37-43 (spec) | 0,2,255 (spec) | 325/325 | installed |
| aveh-desert | 49, 50, 53-63 | - | 39-45 provisional (scenes s23-s25; no spec) | 0,2,255 at s25 (scene-verified) | 247/247 | installed |
| desert-gear | - | 54, 55, 62 | not in a spec | not in a spec | 41/41 | installed |
| cave | 64-73, 281 | - | 45-54 (spec) | 0; 0,3 (spec) | 307/307 | installed |
| cave-gear | - | 64, 70 | not in a spec | not in a spec | 59/59 | installed |
| lair | 74-83, 293 | - | 54-60 (spec) | 0 (spec) | 283/283 | installed |
| bledavik | 101-130, 147-149 | - | 60-67 (spec) | 0,3,2 (spec) | 507/507 | installed |
| aveh-castle | 131-146, 150-159 | 147-149 | 70-72 (spec) | 0,3 (spec) | 584/584 | installed |
| gebler | 160-171 | - | 72-75 (spec) | 0,3; then 0 (spec) | 273/273 | installed |
| nisan | 85-100 | - | 75-87 (spec) | 0,1; then 0,1,2 (spec) | 364/364 | installed |
| mountains | 172-175 | - | 87-90 (spec) | 0,1,2 (spec) | 74/74 | installed |
| flagship | 176-193 | 134 | 90-96 (spec) | 0,1,2; 3,2 (spec) | 358/358 | installed |
| world | - | world map (not a field) | not in a spec | not in a spec | 30/30 | installed |

Revisit rule: a field textured by several plans is primary in the plan whose spec lists it, else in the plan
that covers its whole area (147-149 Bledavik's spec; 134 Aveh castle; 1, 3 Lahan; 54, 55, 62 Aveh desert,
whose rooms the Dazil spec also names).

## Doll sets

| Doll set | Pages | State |
|---|---|---|
| battle-set | 323/323 | installed |
| aveh-castle-npcs | 90/90 | installed |
| bledavik-npcs | 120/120 | installed |
| cave-npcs | 118/118 | installed |
| dazil-npcs | 139/139 | installed |
| flagship-npcs | 82/82 | installed |
| forest-npcs | 95/95 | installed |
| gebler-npcs | 42/42 | installed |
| lahan-npcs | 142/142 | installed |
| lair-npcs | 162/162 | installed |
| mountains-npcs | 13/13 | installed |
| nisan-npcs | 63/63 | installed |
| party-bart | 67/67 | installed |
| party-citan | 0/21 | prepared |
| party-elly | 0/20 | prepared |

## Chapters to prepare

Story order, disc 1 then disc 2. Rows are cut by field-id blocks that the exits connect and by the scenes
inside them. Every row is **provisional**: its story points are the span of the scenes (or story-point ops)
found in its fields, not a capture-ready spec, and a row may hold fields reached from elsewhere (hubs, ships)
that are revisited across several stretches. No row has a chapter name yet; none is invented here.

| # | Disc | Fields | Story points | Party | Status | Evidence |
|---|---|---|---|---|---|---|
| 1 | 1 | 84 | 59-87 (story ops only) | unknown | provisional | exits from lair 79 and 83; not in the lair plan |
| 2 | 1 | 194-223 | 96-135 | 0 at s55 @102 (scene-verified); rest unknown | provisional | scenes s53-s62, s66 |
| 3 | 1 | 224-249 | 111-132 | 0,1,5 at s63 @123 (scene-verified); rest unknown | provisional | scenes s58, s63-s65 |
| 4 | 1 | 250-271 | 135-138 | unknown | provisional | scene s67 (270); story op 135 in 262 |
| 5 | 1 | 288-292, 294-311 | 135-204 | 0,1 at s70 @138; 0,3,2 at s78 @144; 0,1,2 at s119 @203 (scene-verified) | provisional | scenes s68-s71, s78, s99, s119; a hub on both discs |
| 6 | 1 | 312-332 | 139-205 (mostly 139-165) | varies: 3 at s72; 0,1 at s73-s76; 0,3,2 at s77; 1 at s80; 0,3,1 at s81; 1,2,3 at s84-s85; 0,1,2 at s91 (scene-verified) | provisional | scenes s72-s91, s95, s120 |
| 7 | 1 | 272-280, 282-287 | 146-295 (hub; story ops 43-283) | 0,1,2 at s83 @146 (scene-verified); rest unknown | provisional | scenes s83, s144, s150; hubs on both discs |
| 8 | 1 | 333-360 | 149-160 | 1,3,5 at s92 @155; 0,1,2 at s94 @158 (scene-verified) | provisional | scenes s86-s87, s92, s94 |
| 9 | 1 | 361-388 | 165-168 | unknown | provisional | scenes s96, s98; story ops 165-168 |
| 10 | 1 | 405-422 | 169-203 | 0,1 at s101-s102; 0,1,2 at s118 @202 (scene-verified) | provisional | scenes s100-s102, s118 |
| 11 | 1 | 423-450 | 175-186 | 0,1 at s103 @175; 0,1,2 at s104 @176 (scene-verified) | provisional | scenes s103-s105, s107, s110-s111 |
| 12 | 1 | 451-473 | 178-243 | 0,1,2 at s108 @180; 0,8,7 at s122-s123 @207; 0,1 at s137-s138 @241 (scene-verified) | provisional | scenes s106, s108-s109, s112, s122-s123, s137-s138 |
| 13 | 1 | 474-506 | 187-207 | unknown | provisional | scenes s113, s115, s121 |
| 14 | 1 | 507-543 | 209-222 | 0,2,1 at s124 @209; 0,1 at s125-s126 (scene-verified) | provisional | scenes s124-s129; 515 is a hub (s147) |
| 15 | 1 | 544-578 | unknown (no scene or story op in these fields; entered from 525 and 543, whose scenes end at 222) | unknown | provisional | exits only |
| 16 | 1 | 579-590 | 222-241 | 0,1 at s130 @222 (scene-verified) | provisional | scenes s130, s136 |
| 17 | 1 | 591-611 | 228-240 | 0 at s132-s133 @231 (scene-verified) | provisional | scenes s131-s135 |
| 18 | 2 | 612-626 | 243-310 | unknown | provisional | scenes s139-s142, s145, s158; 617-619 are hubs |
| 19 | 2 | 627-657 | 263-290 | unknown | provisional | scenes s143, s146, s149 |
| 20 | 2 | 658-684 | 285-330 | unknown | provisional | scenes s148, s160-s161 |
| 21 | 2 | 685-719 | 295-350 | 0 at s151-s154 @295-301; 0,4,9 at s156 @302 (scene-verified) | provisional | scenes s151-s156, s159, s162-s163 |

Disc: rows 1-17 hold fields that are on disc 1 only, plus hubs on both discs; rows 18-21 hold fields on both
discs and begin at story 243, after the last disc-1-only scene (s138 @242).

## Unresolved fields

Chapter or no chapter cannot be established from the evidence above. They are not proven unused.

| Fields | Why unresolved |
|---|---|
| 41, 42 | on both discs; no exit, scene or story op in or out in the scan; ids sit inside Dazil's block but no Dazil spec or plan names them |
| 46-48, 51, 52 | on both discs; reached only from each other and debug room 725; no scene or story op, so no story placement (the earlier draft's "Duneman Isle" row is not confirmed here) |
| 389-404 | on both discs; reached only from each other and debug room 724; no scene or story op; a world-map entry or scripted transition would place them, and none was read |

## Fields in no chapter

| Fields | Reason |
|---|---|
| 0, 720-729 | debug rooms: field 0 leads only to 720-729, and each of 720-729 leads to dozens of fields across both discs and sets story points from every part of the game (0-350), a warp-room pattern no story room has |
