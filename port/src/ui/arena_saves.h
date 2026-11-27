// The save browser of party vs party (xg-htn.35.143): a player's party from a
// memory-card save on this machine, no game loaded first (card/saves.h reads
// the cards, card/party.h packs and checks the party).

#ifndef PORT_UI_ARENA_SAVES_H
#define PORT_UI_ARENA_SAVES_H

#include <stddef.h>

#include "card/party.h"

namespace menu {

// Draws, where it is called (the Versus battle screen's party vs party
// block), player `player`'s picker (1 or 2: each its own save and members,
// kept apart; both may pick the same save and the same members): this
// region's saves on both memory cards (each with its slot, number, place,
// disc and play time), and for the save picked a member picker of the
// members who have joined in it (1-3, in order, the first leading; the
// save's own party at first). The cards are read each time the panel
// appears, and again on its button.
// Returns true while the save and members picked make a party that passes
// the range checks, with *out that party record; false otherwise (*out is
// then left as it was). Player 0: this machine's own party for an online
// session (party vs party), a browser of its own.
bool arenaSavesPanel(int player, XgPartyRecord* out);

// The gauntlet's card picker. NULL until a save is selected.
// The returned block is valid until this picker refreshes.
const uint8_t* gauntletSavePicker();
const uint8_t* arenaItemSavePicker(int side);

// What a refused party shows (card/party.h XG_PARTY_*, translated), with the
// member's name where the reason names one (character id 0-10, else none).
void arenaPartyReason(int reason, int characterId, char* out, size_t size);

} // namespace menu

#endif
