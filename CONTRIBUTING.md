# Contributing

Thank you for considering it. Bug reports, fixes, reports from hardware the
port has not been run on, and corrections to the pages are all welcome.

## Reporting a bug

Use the **Bug report** form. Say which game release and which engine, what you
run on (system, GPU and driver), and what you changed from the default
settings. With the defaults the port should play like the PlayStation version,
so "it differs from the original" is a bug by definition.

Please attach nothing from the game: no disc images, no extracted files, no
save states. A screenshot of what looks wrong is fine.

## Before you change code

- **You need your own discs.** Nothing from the game is in this repository,
  and nothing from the game may be added to it.
- **The port's code is original.** Emulators and other projects are references
  for facts only: run them and compare what comes out, but do not translate
  their code, tables or constants into `port/`. Write from documented hardware
  behaviour and your own measurements, and say truthfully in a comment where a
  fact comes from.
- **The game logic is the decompilation.** `src/` and `include/` are shared
  with [EtherAura/xenogears](https://github.com/EtherAura/xenogears) and must
  keep compiling to byte-identical PlayStation code. PC-only code goes in
  `#ifdef PLATFORM_PC` blocks there, or in `port/`.
- **Everything beyond the original is optional.** A new feature is off by
  default, and the default settings keep playing like the PlayStation version.
- **Build first.** [Building](docs/building.md) is the whole procedure.

## What a change has to pass

1. The decompilation still matches: `ninja` and `python tools/check_bss.py`.
2. The port builds: `cmake --build build/pc/release`.
3. `python tools/port_tests.py` reports no failure. A test whose disc image
   you do not have is skipped, not failed, and `--no-gpu` skips the tests that
   need a Vulkan 1.3 device.

CI here cannot build the port, because no runner has a disc. It checks that
the tools compile and the configuration parses, and runs the tool tests that
need no game data. The build and the port's own tests are yours to run before
you open the pull request, and they are run again before anything is
published.

## How a pull request is handled

This repository is published from a development tree, one commit at a time. A
pull request is read and discussed here. When it is accepted, the change is
applied to the development tree and appears on `main` with the next publish,
with you credited in the commit message. It is not merged with the merge
button, so do not be surprised when it is closed with a link to that commit.

## Questions

[Discussions](https://github.com/EtherAura/xenogears-pc/discussions) is the
place for questions, setups and things you made with the pack tools.

## Licence

GPL-3.0 or later. The libraries in `port/third_party/` keep their own licences.
By contributing you agree that your contribution is licensed under the same
terms as the part of the tree it goes into.
