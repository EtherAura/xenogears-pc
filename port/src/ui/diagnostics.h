// The settings menu's diagnostics (port/src/ui/diagnostics.cpp, written
// separately): what the menu calls of it. The calls are weak (sections.cpp),
// so the menu builds and links while diagnostics.cpp does not exist: each is
// called only when it is linked in.
//
//   diagnosticsPerfRows()   Display > Performance, after the group's own rows:
//                           inside the Display page (opt::beginPage), its
//                           "Performance" group open, outside any table.
//                           Its own tables and ImGui widgets; opt::row()
//                           (options.h) opens the group's two-column table
//                           for a setting's row (a label, controls after it).
//   diagnosticsPacksPage()  the Packs section, at its end: inside the Packs
//                           page, a "Diagnostics" group open, outside any
//                           table (the same rules).
//
// Both are called in the menu's frame (the game paused), every frame the
// section's page shows, and only then: not while the Search page indexes or
// filters the rows (their call sites ask opt::plain()), nor while the group
// around them is collapsed.

#ifndef PORT_UI_DIAGNOSTICS_H
#define PORT_UI_DIAGNOSTICS_H

void diagnosticsPerfRows();
void diagnosticsPacksPage();

#endif
