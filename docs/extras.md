# Extras

The extras read your disc images and use the game's own code to load,
animate and draw what they show. Open them from F1 > Extras > Open the extras
(the game stays paused), or start `xenogears --extras`. Builds made with
`-DXG_EXTRAS=ON` have them.

![The extras](img/extras-home.png)

## Assets

![A portrait as a TIM picture](img/extras-browser.png)

Both discs' files, with their kinds, bundles, LZSS, TIM pictures, models and
sound banks, and export.

![The same file in hex](img/extras-browser-hex.png)

## Visuals

![The visual library](img/extras-visuals.png)

![Fei's field sprite](img/extras-sprites.png)

Sprites: every frame, facing and palette, animated by the game's sprite code.
PNG export of a frame, a sheet or an animation.

![A field NPC](img/extras-sprites-npc.png)

![Fei's battle sprite](img/extras-sprites-battle.png)

Field NPCs by room, and battle sprites.

![Weltall](img/extras-models.webp)

Models: Gears, enemies, arenas, field and world map models, with glTF export.

![A battle arena](img/extras-models-arena.webp)

![A Gear's animation programs](img/extras-animations.png)

Animations: Gear programs, Battling clips and field MIMe targets.

![Battle effects](img/extras-battlefx.webp)

Battle effects: ability sprites, effects and Gear trails.

![A field with its walkmesh, triggers and exits](img/extras-maps-walkmesh.png)

Map viewer: fields with the walkmesh, triggers, exits and actors marked,
from the game's camera or a free one.

![A field from a free camera](img/extras.png)

![The world map](img/extras-maps-world.png)

The world map from a free camera, and a top-down overview with PNG export.

![A movie](img/extras-gallery-movie.png)

Gallery: the movies, played by the game's own movie player.

![Portraits](img/extras-gallery-pictures.png)

Portraits and event pictures.

![Scenes](img/extras-gallery-scenes.png)

Scenes starts the game at a chosen cutscene.

## Jukebox

![The jukebox](img/extras-jukebox.png)

Music, sound effects and movie music, played by the game's own sound driver,
with WAV export.

## Bestiary

![An enemy in the bestiary](img/extras-bestiary.png)

Every enemy: its numbers, weaknesses, drops, attacks and where it appears.
Everything, or only what you have met (My progress).

![An enemy's attack in the battle scene](img/extras-bestiary-battle.webp)

An attack played in the battle scene.

## Tables and scripts

![Playback rates](img/extras-tables.png)

Tables: names, music, sounds and rates read from the disc and the game's
code, with TSV export.

![A room's actor routines](img/extras-scripts.png)

Scripts: a room's actor routines, variables, a trace and the dialogue history.

## Battle arena

![F1 > Extras](img/extras-f1.png)

Battling's two-player bouts from any field, and versus battles where player 2
commands an enemy formation. Online plays either with someone elsewhere: one
hosts, the other joins with the same session word (UDP port 7461 by default).
F1 > Extras.
