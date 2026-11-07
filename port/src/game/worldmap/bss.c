/*
 * World map BSS the port defines itself: the two render contexts, which the
 * world map's exits free through their members (tools/pc_link.py leaves
 * objects the port defines out of the generated data).
 */
#include "common.h"
#include "psyq.h"
#include "worldmap/worldmap.h"

sWorldmapRenderingStruct worldmapRenderingStructs[2] = { 0 };
