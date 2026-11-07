// The settings menu's sounds (sfx.cpp): the game's own menu sounds, from the
// player's disc, played beside the paused game.

#ifndef PORT_UI_SFX_H
#define PORT_UI_SFX_H

namespace sfx {

// The game's menu sounds (playMenuSoundEffect's numbers in the field's sound
// effect bank): the cursor moves, an entry is chosen, the menu goes back, a
// choice is refused (updateMenuInput, mainMenuExecute); a sub-menu comes in
// (processLoadSaveMenuSub1, as its box starts turning in) and goes
// (processLoadSaveMenuSub1Alt2, as it starts turning away).
enum Sound { MOVE = 1, CHOOSE = 2, BACK = 3, BUZZ = 4, OPEN = 0x5B, CLOSE = 0x5C };

void opened();       // the menu opened
void play(Sound s);  // one sound (again from its start if it is playing)
unsigned plays();    // how many play() calls so far

} // namespace sfx

#endif
