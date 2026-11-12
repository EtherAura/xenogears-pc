# Security

## Reporting a vulnerability

**[Open a private security advisory](https://github.com/EtherAura/xenogears-pc/security/advisories/new).**
Private vulnerability reporting is enabled on this repository: the report stays
between you and the maintainer until an advisory is published.

Please do not open a regular issue for a suspected vulnerability. Issues are
public the moment they are filed.

Say what you found, how to reproduce it and what it lets someone do. A fix is
welcome and not required.

## What counts here

The port is a program you build yourself and run with your own disc images. It
ships no executable and no game data. What matters is what it does with files
that did not come from you:

- **Packs.** Texture, sprite, text, portrait and movie packs are files made by
  other people, and the port reads them. A pack that crashes the port in a way
  that corrupts memory, or makes it read or write outside its own folders, is a
  vulnerability.
- **Save states, memory cards and settings** that someone else gave you are
  the same kind of input.
- **Disc images** are yours, but a damaged or altered one must not do more than
  fail to load.
- **The build tools.** `tools/setup.sh` downloads the PlayStation toolchain
  into `tools/toolchain` and the Python packages into `.venv`, without root,
  each download pinned by checksum. A download that is not checked against its
  pin, or a tool that writes outside the tree, is a vulnerability.

Not vulnerabilities: bugs of the game itself (the game logic is the original
code, bugs included), cheats and anything else a player does to their own
game, and anything that needs a modified build to work.

## Supported versions

The newest commit on `main`. There are no releases.
