# Extras

![The map viewer with a free camera, original art](img/extras.png)

The extras read everything from your own disc images and use the game's own
code to set up, animate and draw it. They are in builds made with
`-DXG_EXTRAS=ON`.

Open them from F1 > Extras > Open the extras; the game stays paused and
leaving them returns to the menu. `xenogears --extras` opens them on their
own, without starting the game. They work with the keyboard, the mouse or a
gamepad.

## On the extras screen

| Extra | What it does |
|---|---|
| Assets | Both discs' files: their kinds, hex, LZSS, bundles, TIM pictures, models and sound banks, with export |
| Visuals | The visual library: sprites, models, battle scenes and effects, the map viewer and the gallery |
| Jukebox | Music, sound effects and movie music, played by the game's own sound driver |
| Bestiary | Every enemy: its picture, numbers, weaknesses, drops, attacks and where it appears |
| Tables | Names, music, sounds and rates read from the disc and the game's code |
| Scripts | Actor routines, variables, execution traces and dialogue history |

In Visuals:

- Sprites: party, NPC, Gear, enemy and battle sprites, animated by the
  game's sprite code, with every frame, facing and palette. PNG export of a
  frame, a sheet or an animation.
- Models: Gears, enemies, arenas, field and world map models, with their
  animations and texture pages. glTF (GLB) export with the skeleton and
  animations.
- Battle scene and effects: battle arenas, actors and moves, ability
  effects and Gear trails.
- Map viewer: fields and the world map as the game draws them, from the
  game's camera or a free one, with the walkmesh, triggers, exits and actors
  marked.
- Gallery: the movies, played by the game's own movie player; portraits
  and event pictures; and Scenes, which starts the game at a chosen cutscene.

The bestiary can show everything, or only what you have met and defeated
(My progress). Battles are recorded as you play, for each game separately.
Exports go to an `exports` folder beside `settings.ini`; the extras screen
can change it.

## In the settings menu

- Battle arena (F1 > Extras): Battling's two-player bouts (BONUS BATTLING,
  PLAYER1 VS PLAYER2) from any field, then back to where you were. Every Gear
  lets both players pick from all 49 Gears.
- Versus battle (F1 > Extras): player 2 commands an enemy formation from
  the disc against your party. Nothing is won or lost.
- Online (F1 > Extras): Battling or a versus battle with a player
  elsewhere. One player hosts, the other joins, and both type the same session
  word. The host's UDP port (7461 by default) must be reachable. Both need the
  same game version and disc.
- Gauntlet (F1 > Gameplay): the story's bosses one after another: all of
  them in story order, the character bosses, the Gear bosses, or a list you
  make. You pick the party for each fight from the members with you, and a
  recovery rule between fights. When the run ends, the game is put back as it
  was; nothing is won and no memory card is touched.
