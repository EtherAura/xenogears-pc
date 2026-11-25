English | [日本語](extras-ja.md)

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

Battling's two-player bouts and versus battles, from anywhere in the game, the
title screen too. When a match ends both players choose **Rematch** or
**Leave**, and Leave goes back to where you were. F1 > Extras.

In a **Versus battle** player 2 fights your party with the second controller:
an enemy formation, commanded through the party's command ring (each attack
under the game's name for it, or what it does); a **Mirror match**, your party
against a copy of itself; or **Party vs party**, where each player picks a
memory-card save and up to three of its members, with their levels,
equipment, abilities and items as saved. Edited saves are accepted.

In a mirror match or party vs party each player sees their own side's HP and
EP: on one screen the status bar shows the side whose turn it is.
**Opponent's HP and EP** (off by default) shows the other side's in the enemy
information window (Select). **Battle music** plays any of the jukebox's
songs, and **Battle stage** sets where a mirror match or party vs party is
fought, from 17 places in the game.

Online plays Battling, versus battles against an enemy formation, and party
vs party with someone elsewhere: one hosts, the other joins with the same
session word (UDP port 7461 by default). In party vs party each player brings a
party from a save on their own machine; the other player's party is not
verified, only kept within the game's ranges, and the host's battle music,
battle stage and Opponent's HP and EP hold for both. The mirror match is on one
machine for now.

![Battling from the battle arena](img/extras-battling.webp)

Weltall against Heimdal in Battling. **Every Gear** offers all 49 Gears, not
only those the story has reached.
