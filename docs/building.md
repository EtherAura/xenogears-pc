# Building

The port is built from source with your own Xenogears discs. The executable
carries no game data: at every start it reads the game's code and data from
your disc image, which must be the release the build was made for.

## Requirements

- Linux x86-64 with bash, curl, tar, git, make, gcc and cpp, Python 3, Ninja
  and CMake 3.25 or newer.
- A C and C++ compiler, SDL 3, the Vulkan headers and `glslc`.
- A GPU and driver with Vulkan 1.3.
- Optional: Khronos glslang (for shader presets) and dav1d (for AV1 movie
  packs). The build leaves those features out when they are missing.
- Your discs as Redump-style `.cue`/`.bin` images: US (SLUS-00664) or
  Japanese (SLPS-01160).

No root access is needed. `tools/setup.sh` downloads the PS1 toolchain into
`tools/toolchain` and the Python packages into `.venv`.

## Steps

```bash
tools/setup.sh
source .venv/bin/activate      # fish: source .venv/bin/activate.fish
```

Put the disc images under `bin/` with their Redump names, one folder per
disc:

```
bin/Xenogears (USA) (Disc 1)/Xenogears (USA) (Disc 1).cue + .bin
bin/Xenogears (USA) (Disc 2)/Xenogears (USA) (Disc 2).cue + .bin
bin/Xenogears (Japan) (Disc 1)/...
```

Only disc 1 is needed to build. Then:

```bash
python tools/extract.py us     # the game's code files, checked against retail
python tools/configure.py us   # split them and write build.ninja
ninja                          # rebuild every module and check it byte for byte
python tools/pc_sources.py     # source lists for CMake
cmake -S port -B build/pc/release -G Ninja -DCMAKE_BUILD_TYPE=Release -DXG_VERSION=US -DXG_EXTRAS=ON
cmake --build build/pc/release
build/pc/release/xenogears --setup
```

Use `jp` and `-DXG_VERSION=JP` for the Japanese release, in its own build
folder (for example `build/pc/release-jp`). Leave out the version name to
extract and configure both.

`--setup` opens the setup screen: choose the folder with your images (it
finds both discs) and, optionally, a PS1 BIOS image. Without a BIOS only the
save names on the memory card screens and some scrolling scene text stay
blank. Later starts use the saved paths; `--setup` changes them.

Settings, memory cards and save states are kept in
`~/.local/share/xenogears/` (on Windows `%APPDATA%\xenogears\`).

## Enhanced engine

`-DXG_ENGINE=enhanced` builds the Enhanced engine as `xenogears-enhanced`.
Build it into `build/pc/enhanced`, or put it beside `xenogears`, and F1 >
System > Engine can switch between the two.

## Windows

After the steps above, `tools/build_windows.sh [--jp] [--debug] [OUTDIR]`
cross-builds the Windows version on Linux with MinGW-w64. It needs
`x86_64-w64-mingw32-gcc`, CMake, Ninja, `glslc` and an SDL 3 source tarball
(set `SDL3_TARBALL`). The result, in
`build/pc/dist/xenogears-us-windows-x86_64` by default, holds `xenogears.exe`,
`xenogears-enhanced.exe` and `SDL3.dll`.
