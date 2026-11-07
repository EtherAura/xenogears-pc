/*
 * Battling data that tools/pc_link.py cannot lay out from the retail bytes
 * (gen/unconverted.txt): pointer fields that are NULL in every entry, or
 * primitive tags (8 bytes on the PC) that are still zero. Same contents as
 * the PS1 data, in the PC layout.
 */
#include "common.h"
#include "psyq.h"
#include "main/filesystem.h"
#include "battling/battling.h"

/* The menus' item tables (pc_link lays them out: their text and callback
 * pointers show in the data), at their PS1 extents. */
extern BattlingMenuItem battlingGear1MenuItems[2];
extern BattlingMenuItem battlingGear2MenuItems[2];
extern BattlingMenuItem battlingMainMenuItems[5];
extern BattlingMenuItem battlingBonusMenuItems[7];
extern BattlingMenuItem battlingPauseMenuItems[2];
extern BattlingMenuItem battlingOptionsMenuItems[5];
extern BattlingMenuItem battlingGiveUpMenuItems[2];
extern BattlingMenuItem battlingPracticeMenuItems[4];

void drawBattlingOptionsValues(BattlingMenu* menu);
void drawBattlingBonusValues(BattlingMenu* menu);
void drawBattlingPracticeValues(BattlingMenu* menu);

/* The eight menus (0x800915AC). Everything after the parent menu starts
 * zero, the background tiles included (initBattlingMenus fills them). */
BattlingMenu battlingMenus[8] = {
    { 0, { 0 }, battlingPauseMenuItems, 2, 0, NULL },
    { 32, { 0 }, battlingOptionsMenuItems, 5, 0, drawBattlingOptionsValues },
    { 0, { 0 }, battlingGiveUpMenuItems, 2, 0, NULL },
    { 0, { 0 }, battlingMainMenuItems, 5, 3, NULL },
    { 0, { 0 }, battlingBonusMenuItems, 7, 3, drawBattlingBonusValues },
    { 0, { 0 }, battlingGear1MenuItems, 2, 3, NULL },
    { 0, { 0 }, battlingGear2MenuItems, 2, 3, NULL },
    { 1, { 0 }, battlingPracticeMenuItems, 4, 7, drawBattlingPracticeValues },
};

/* The files battlingMainTask loads first (0x800917C0): music, the sound
 * effects, the gear tables, the floor and the 2D graphics. */
LoadingBatchCommand battlingStartupLoads[6] = {
    { 1, 0, NULL }, { 2, 0, NULL }, { 3, 0, NULL }, { 4, 0, NULL }, { 5, 0, NULL }, { 0, 0, NULL },
};
