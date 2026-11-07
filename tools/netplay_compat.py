#!/usr/bin/env python3
"""The netplay compatibility id (port/src/system/netplay.c): which builds can play together.

    python tools/netplay_compat.py OUT.h VERSION [ENGINE]   # the build: gen/netplay_compat.h (written when it changes)
    python tools/netplay_compat.py --list VERSION [ENGINE]  # the files it covers
ENGINE is original (default: the game C in src/ and include/) or enhanced (the fork in engine/,
docs/ENGINE_PLAN.md): the two engines never play together.

Two players' builds must run the same game, not be the same file: a Linux and a Windows build of
the same sources (another compiler, other host code around it) play together; builds whose game
differs must not. The id is a hash of the sources that decide the game, as text with LF line
ends: the game C (src/, include/), the symbols and splits that lay out its data (config/), the
platform layer that runs it (port/src/: the virtual PS1, the game-side port code, netplay's
protocol), the port's CMakeLists and the tools that generate code into the build (pc_link.py,
pc_sources.py), with the region. Left out: what cannot change the game: the menu (port/src/ui),
the extras (port/src/extras: their game-changing starts are refused in a session), tests.
"""
import hashlib
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
TREES = ["src", "include", "config", "port/src"]
FILES = ["port/CMakeLists.txt", "tools/pc_link.py", "tools/pc_sources.py"]
LEFT_OUT = ["port/src/ui", "port/src/extras"]


def covered(engine: str = "original") -> list[Path]:
    out = [ROOT / f for f in FILES if (ROOT / f).is_file()]
    trees = ["engine/src", "engine/include"] + TREES[2:] if engine == "enhanced" else TREES
    for tree in trees:
        for p in (ROOT / tree).rglob("*"):
            rel = p.relative_to(ROOT).as_posix()
            parts = rel.split("/")
            if (not p.is_file() or "test" in parts or any(x.startswith(".") for x in parts) or rel.endswith("~")
                    or any(rel == d or rel.startswith(d + "/") for d in LEFT_OUT)):
                continue
            out.append(p)
    return sorted(out, key=lambda p: p.relative_to(ROOT).as_posix())


def compat(version: str, engine: str = "original") -> tuple[int, int]:
    h = hashlib.sha256(b"xg-netplay-compat\0" + version.upper().encode() + b"\0" + engine.encode() + b"\0")
    files = covered(engine)
    for p in files:
        h.update(p.relative_to(ROOT).as_posix().encode() + b"\0")
        h.update(p.read_bytes().replace(b"\r\n", b"\n"))
        h.update(b"\0")
    return int.from_bytes(h.digest()[:8], "little"), len(files)


def main() -> int:
    engine = sys.argv[3] if len(sys.argv) == 4 else "original"
    if len(sys.argv) in (3, 4) and sys.argv[1] == "--list":
        for p in covered(engine):
            print(p.relative_to(ROOT).as_posix())
        return 0
    if len(sys.argv) not in (3, 4):
        print(__doc__.split("\n\n")[1], file=sys.stderr)
        return 2
    out, version = Path(sys.argv[1]), sys.argv[2]
    value, count = compat(version, engine)
    text = ("/* tools/netplay_compat.py: the netplay compatibility id, a hash of the game's sources */\n"
            f"#define XG_NETPLAY_COMPAT 0x{value:016x}ull\n"
            f"#define XG_NETPLAY_COMPAT_FILES {count}\n")
    if not out.exists() or out.read_text() != text:
        out.parent.mkdir(parents=True, exist_ok=True)
        out.write_text(text)
    return 0


if __name__ == "__main__":
    sys.exit(main())
