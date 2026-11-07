# Xenogears PC port

Part of -Project Exodus-, an unofficial Xenogears preservation effort.

Xenogears running natively on Linux and Windows. It's built from the
[matching decompilation](https://github.com/EtherAura/xenogears), so the game
logic is the original code, compiled for the PC. Bring your own US or Japanese
discs: nothing from the game is included, and it reads its data from your disc
image every time it starts.

<table>
<tr>
<td width="50%"><a href="docs/display.md"><img src="docs/img/display.png" alt="Lahan in 16:9 at six times the PlayStation resolution"></a><br>
<b><a href="docs/display.md">Display</a></b>: widescreen, higher resolutions, smoother motion</td>
<td width="50%"><a href="docs/packs.md"><img src="docs/img/packs.png" alt="The packs page of the settings menu"></a><br>
<b><a href="docs/packs.md">Packs</a></b>: replacement textures, sprites, text and portraits, Beetle PSX packs included</td>
</tr>
<tr>
<td width="50%"><a href="docs/gameplay.md"><img src="docs/img/gameplay.png" alt="A forest battle at 2x speed"></a><br>
<b><a href="docs/gameplay.md">Gameplay</a></b>: battle speed, difficulty, encounters, save states</td>
<td width="50%"><a href="docs/extras.md"><img src="docs/img/extras.png" alt="The map viewer with a free camera"></a><br>
<b><a href="docs/extras.md">Extras</a></b>: jukebox, bestiary, gallery, model and map viewers</td>
</tr>
</table>

All of it is optional. With the default settings it plays like the PlayStation
version.

To build it, see [Building](docs/building.md).

## Licence

GPL-3.0 or later, in `LICENSE`. The libraries in `port/third_party/` keep their
own licences. Xenogears is © Square Enix; this project isn't affiliated with
them, and no game data is included.
