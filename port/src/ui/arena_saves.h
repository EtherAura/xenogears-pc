// The save browser of party vs party (xg-htn.35.143): a player's party from a
// memory-card save on this machine, no game loaded first (card/saves.h reads
// the cards, card/party.h packs and checks the party).

#ifndef PORT_UI_ARENA_SAVES_H
#define PORT_UI_ARENA_SAVES_H

#include "card/party.h"

namespace menu {

// Draws, where it is called (the Versus battle screen's party vs party
// block): this region's saves on both memory cards (each with its slot,
// number, place, disc and play time), and for the save picked a member
// picker of the members who have joined in it (1-3, in order, the first
// leading; the save's own party at first), then the note that the other
// player's party comes from their own save and is not verified. The cards
// are read each time the panel appears, and again on its button.
// Returns true while the save and members picked make a party that passes
// the range checks, with *out that party record; false otherwise (*out is
// then left as it was).
bool arenaSavesPanel(XgPartyRecord* out);

} // namespace menu

#endif
