# Xenogears PC port

Part of **Project Exodus**, an unofficial Xenogears preservation effort.

A native 64-bit PC port of Xenogears (PlayStation, 1998) for Linux and
Windows, built from the C of the matching decompilation,
[EtherAura/xenogears](https://github.com/EtherAura/xenogears). The game logic
is the decompiled game code itself, compiled for the PC. A platform layer of
our own stands in for the PlayStation hardware and Sony's SDK: CD, a Vulkan
GPU, GTE, SPU, MDEC, pads and memory cards. It builds for the US release
(SLUS-00664) and the Japanese release (SLPS-01160).

No game data is included, and the executable carries none: at every start it
reads the game's data from your own disc image, which must be the release it
was built for. This repository is source only.

## What it adds

Everything is optional; the defaults play the original game.

- An F1 settings menu: display, sound, controls, gameplay and memory cards.
- Higher internal resolutions, widescreen and expanded vertical framing.
- Motion display rates of 60, 120, 144 or 240 Hz with smoothed motion.
- Replacement packs for textures, sprites, the HUD, text and portraits, and
  Original, Linear or Edge filtering for each.
- Gameplay controls: difficulty, the random-encounter rate, and EXP and gold
  multipliers.
- Regional controls, audio and cutscene subtitles.
- An Enhanced engine profile (`engine/`) beside the Original one.
- Extras read from your disc: a jukebox, a gallery of movies and pictures, a
  sprite viewer, a bestiary and asset browsers.

## Building

Host requirements: Linux x86-64, bash, curl, git, make, gcc/cpp, python3,
ninja, CMake, a C/C++ compiler, SDL3 and a Vulkan 1.3 driver. No root access
is needed.

```bash
tools/setup.sh                       # toolchain -> tools/toolchain, Python deps -> .venv
source .venv/bin/activate            # fish: source .venv/bin/activate.fish
```

Put your disc images under `bin/` using the Redump names listed in
`config/<version>/disc.yaml`. The port is built after the matching build
passes:

```bash
python tools/extract.py --check-image      # every version whose disc is present
python tools/configure.py                  # split every module, write build.ninja
ninja                                      # rebuild every module, sha1-check it against retail
python tools/pc_sources.py                 # source lists for CMake
cmake -S port -B build/pc/release -G Ninja -DCMAKE_BUILD_TYPE=Release -DXG_VERSION=US -DXG_EXTRAS=ON
cmake --build build/pc/release
build/pc/release/xenogears --setup
```

Use `-DXG_VERSION=JP` in a separate build directory for the Japanese release.
`tools/build_windows.sh` cross-builds for Windows with MinGW-w64; the
executable needs `SDL3.dll` beside it.

## Checks

`tools/gates.sh` runs every gate the port keeps green, for both versions:
the matching build, `.bss` placement, 64-bit and declaration checks, the
payload check that proves the executable carries no retail data, and
`tools/port_tests.py`. Behaviour is measured against Beetle PSX and
PCSX-Redux captures (`tools/beetle.py`, `tools/redux.py`).

## Layout

| Path | Contents |
|---|---|
| `src/`, `include/`, `config/` | The decompilation, as in EtherAura/xenogears |
| `port/` | Platform layer, settings menu and extras |
| `engine/` | The Enhanced engine profile, forked from `src/` by `tools/fork_engine.py` |
| `tools/` | Setup, build, checking and validation scripts |

This repository is the decompilation plus the port: its history merges the
decompilation's. Changes to the decompiled C belong in EtherAura/xenogears.

## Clean room

The platform layer is our own code, written from documented behaviour
([psx-spx](https://psx-spx.consoledev.net/)) and our own measurements.
Emulators (Beetle PSX, PCSX-Redux) and yaz0r's
[Noah](https://github.com/yaz0r/Noah) were run and compared against as
references, never copied.

## Licence

The port's own code is free software under the GNU General Public License,
version 3 or (at your option) any later version; the text is in `LICENSE`.
The components in `port/third_party/` keep their own licences, next to them
(Dear ImGui, glslang, dav1d, and the jukebox's Noto Sans CJK subset under
the SIL Open Font License 1.1), as does SDL 3, which the port links.

Xenogears is © Square Enix. This project is not affiliated with or endorsed
by Square Enix, and the licence does not cover the game: its code and data
come from your own discs and are never part of this repository.
