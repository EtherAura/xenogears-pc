English | [日本語](building-ja.md)

# Building

The port is built from source with your own discs. The executable carries no
game data: at every start it reads the game from your disc image, which must
be the release the build was made for.

## Requirements

- Linux x86-64 with bash, curl, tar, git, make, gcc and cpp, Python 3, Ninja
  and CMake 3.25 or newer.
- A C and C++ compiler, SDL 3, the Vulkan headers and `glslc`.
- LLVM's `llvm-dwarfdump`, which writes each build's state schema: with it,
  a save state slot loads in other versions of the port too
  (`-DXG_STATE_SCHEMA=OFF` builds without it).
- A GPU and driver with Vulkan 1.3.
- Optional: Khronos glslang (shader presets) and dav1d (AV1 movie packs).
- Your discs as Redump `.cue`/`.bin` images: US (SLUS-00664) or Japanese
  (SLPS-01160).

No root access is needed. `tools/setup.sh` puts the PS1 toolchain in
`tools/toolchain` and the Python packages in `.venv`.

### FreeBSD amd64

FreeBSD 15.1 builds use Clang and the `binutils`, `cmake-core`, `ninja`,
`sdl3`, `shaderc`, `vulkan-headers`, and `vulkan-loader` packages. Follow the
matching build's FreeBSD setup for the pinned PS1 compiler and Python tools.
Give CMake a fresh build directory so it finds GNU `ld.bfd` and `objcopy`
for the relocatable game modules. The regular build targets below work
with the same checked retail inputs.

## Steps

```bash
tools/setup.sh
source .venv/bin/activate      # fish: source .venv/bin/activate.fish
```

Put the images under `bin/` with their Redump names, a folder per disc:

```
bin/Xenogears (USA) (Disc 1)/Xenogears (USA) (Disc 1).cue + .bin
bin/Xenogears (USA) (Disc 2)/Xenogears (USA) (Disc 2).cue + .bin
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

For the Japanese release use `jp` and `-DXG_VERSION=JP`, in its own build
folder (for example `build/pc/release-jp`).

## First start

![The setup screen](img/building-setup.png)

`--setup` opens the setup screen. Choose the folder with your images (it
finds both discs) and, optionally, a PS1 BIOS. Without a BIOS only the save
names on the memory card screens and some scrolling text stay blank. Later
starts use the saved paths.

Settings, memory cards and save states are kept in
`~/.local/share/xenogears/` (Windows: `%APPDATA%\xenogears\`).

## Enhanced engine

`-DXG_ENGINE=enhanced` builds the Enhanced engine as `xenogears-enhanced`.
Build it into `build/pc/enhanced`, or put it beside `xenogears`, and F1 >
System > Engine switches between the two.

## Windows

After the steps above, `tools/build_windows.sh [--jp] [--debug] [OUTDIR]`
cross-builds for Windows with MinGW-w64. It needs `x86_64-w64-mingw32-gcc`,
CMake, Ninja, `glslc` and three source tarballs: SDL 3, and for the shader
presets and the movie packs' AV1 videos, glslang and dav1d (Meson). Their
versions, URLs and SHA-256 sums are in `tools/deps.json`;
`python tools/deps.py fetch` downloads them into `tools/toolchain-src` and
keeps only files whose sum matches. The build itself downloads nothing.
`--dist` makes a build to hand out, with no path of the build machine in its
executables and no link time in their headers. The result, in
`build/pc/dist/xenogears-us-windows-x86_64` by default, holds `xenogears.exe`,
`xenogears-enhanced.exe` and `SDL3.dll`. Releases are built the same way inside
a pinned Debian build environment with its MinGW-w64 (below), not with the
host's.

## Desktop release packages

Download the amd64 `.deb` from the [releases page](https://github.com/EtherAura/xenogears-pc/releases/latest)
and run `sudo apt install ./FILE.deb`, substituting the downloaded filename.
Ubuntu 26.04 LTS is the minimum supported Ubuntu release. The same package
installed and reached the disc setup screen on Debian Forky (testing).

On Windows x64, run the `setup.exe` download to install both engines. For a
portable copy, extract the `.zip` and launch `xenogears.exe` or
`xenogears-enhanced.exe` from the extracted folder. Both downloads include
the SDL runtime and require a Vulkan 1.3 driver.

On a Linux x86-64 host, the release command needs bubblewrap, Python 3 and
Docker. Docker prepares two cached build environments once: Ubuntu's for the
Linux build and the NSIS compiler, and Debian's MinGW-w64 for the Windows
build (`packaging/deb_ubuntu.Dockerfile` and `packaging/mingw_debian.Dockerfile`,
each pinned by image digest and archive snapshot, so everyone gets the same
compilers). The builds run inside them, offline.

```bash
bash build-release.sh --fetch-only --cache release-cache
bash build-release.sh --discs bin --cache release-cache --version YYYY.MM.N --out release
```

The second command uses the US Disc 1 image and builds offline. It writes
the Windows installer and ZIP, the Linux tarball and the Ubuntu amd64
`.deb` once their payload audits pass. The tarball and Windows packages
include both engines. The `.deb` includes Original.

## Updates

Release packages can check for a new release. The first start asks whether
to check; F1 > System > Updates changes the answer, and `update_check=off`
in `settings.ini` turns the check off. A check runs at most once a day and
fetches two small files from the GitHub releases: the update list and its
signature. The request carries nothing about you or your copy, and a list
whose signature does not verify is ignored. On Linux the check uses the
system's libcurl (`libcurl.so.4`); without it there is no check.

When a new release is out, F1 > System > Updates offers it. The Windows
installer and ZIP copies, the Linux tarball and the Android app install it
themselves (Install and restart). The `.deb`, Arch and Gentoo packages only
say that it is out; update them with the package manager. Save state slots
from a release that checks for updates load in the other such releases, older
and newer; a slot marked "This version only" loads only in the release that
made it.

### APT repository

The releases are also an APT repository for the `.deb`, so `apt upgrade`
keeps the port up to date:

```bash
sudo install -d /etc/apt/keyrings
sudo curl -fsSLo /etc/apt/keyrings/xenogears-pc-archive-keyring.asc \
    https://github.com/EtherAura/xenogears-pc/releases/latest/download/xenogears-pc-archive-keyring.asc
echo 'deb [signed-by=/etc/apt/keyrings/xenogears-pc-archive-keyring.asc] https://github.com/EtherAura/xenogears-pc/releases/latest/download/ ./' \
    | sudo tee /etc/apt/sources.list.d/xenogears-pc.list
sudo apt update && sudo apt install xenogears-pc
```

`gpg --show-keys /etc/apt/keyrings/xenogears-pc-archive-keyring.asc` shows
the key; its fingerprint is `98D8 FE73 4486 2E22 A6CC 4BD1 F8DB C452 62B8 B7A6`.

## Building without a disc

A release's Linux and Windows executables can be rebuilt on a machine that
has no disc. Each release carries its link tables,
`xenogears-pc-VERSION-link-tables.tar.gz`. Check out the release's tag,
download that file, and run:

```bash
bash build-release.sh --tables xenogears-pc-VERSION-link-tables.tar.gz --out release
```

`--only linux` or `--only windows` builds one of them. It needs bubblewrap,
and Docker once to prepare the cached build environments and fetch the
Windows libraries' pinned sources (with network access); the build itself
runs offline. The tables hold addresses, sizes, offsets and hashes of the
game's data and never its bytes. The executables are the same either way:
they read the game from your own disc at every start.

The tables are made where the release is built, from the disc, with the
compilers of the release's build environments. With them is the SHA-256 of
each executable that build made and audited. With the sources of that
release, the command fails unless it made the same bytes; `--reproduce` also
fails when the sources are not the tables'. Nothing is packaged, because the
packages' payload audit needs the disc images. The repository's "Build
without a disc" workflow runs this for each release and compares the result
with the release's Linux tarball and Windows `.zip`.

A plain CMake build links from tables too when the checkout has no `asm/`
folder and the tables are extracted as `port/link-tables`
(`-DXG_LINK_FROM=tables` asks for them). They fit the build environments'
compilers: with another one the build stops at a stale source data plan
wherever that compiler lays an object's data out differently.

With the disc, `bash build-release.sh --discs bin --write-tables port/link-tables`
writes tables for your own checkout: it builds and audits the Linux and
Windows engines and packages nothing.
