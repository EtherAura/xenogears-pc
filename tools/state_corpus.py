#!/usr/bin/env python3
"""The canonical save-state corpus of a build or a release (xg-htn.60.5; docs/CANONICAL_STATES.md 9.3, 17).

    tools/state_corpus.py BUILD [BUILD ...] OUT_DIR [--release NAME] [--case ID ...] [--jobs N] [--no-verify]
    tools/state_corpus.py list [--jp]
    tools/state_corpus.py check CORPUS
    tools/state_corpus.py release TAG [--tarball FILE] [--build FILE.tar.gz | [CONFIG=]DIR ...] [--gate] [--replace]

A corpus is the portable slots one release saves, each with what a later or earlier release needs to load it
and to tell whether it went on as it should: per region (us, jp) and engine (original, enhanced), a
"configuration", every case below, saved as a player saves (XG_STATE_SLOT_SAVE: F7, which waits for the next
safe point) in a save folder of the case's own, so the slot carries its trailer and the folder its writer's
schema (schemas/<hash>.xgschema).

BUILD is one configuration: a CMake build folder of the port (its xenogears or xenogears-enhanced and its
state_schema.json), an installed prefix or an unpacked release tarball (bin/ and share/xenogears-pc/: both
engines, one configuration each), or an executable. The region comes from the schema ("version"), the engine
from the executable's name. Each build's states must be portable: a build without portable slots (a release
before canonical states, XG_PORTABLE_SLOTS off) writes none, and its corpus is refused.

Each case: the game runs from power-on to the case's moment through the existing scene machinery
(tools/statecheck.py SCENES, tools/engine_diff.py's ad-hoc fields and selector battles, the stepped clock
and a scripted pad), the slot save is asked at a vblank and lands at the next safe point V, and the run goes
on to V + RUN (600) vblanks. Kept per case: the slot (slot1.xgs), the pad script (a reader replays it from
V on: the pad is indexed by the restored vblank count), and the writer's own continuation, its invariant log
(port/src/system/invariants.c) and sweep log (port/src/game/*/sweep.c) lines of vblanks V+1..V+RUN: the
reference every reader is held to (state_compat.py, check_load below). With verification (the default), the
writer's build loads its own slot raw and must give that same continuation, so a case that does not repeat
itself never enters a corpus.

The layout:

    OUT_DIR/manifest.json                        (format, release, canonical, configurations, cases)
    OUT_DIR/<config>/schemas/<hash>.xgschema     the writer's schema, as the game filed it
    OUT_DIR/<config>/<case>/slot1.xgs            the slot
    OUT_DIR/<config>/<case>/pad.txt              its input
    OUT_DIR/<config>/<case>/invariants.log       the reference continuation (V+1..V+RUN)
    OUT_DIR/<config>/<case>/sweep.log
    OUT_DIR/executables/...                      a release's executables that are no asset (below)

States hold game memory read from the disc: a corpus never leaves this host (work/state-corpus/<release>/).

A canonical release: one whose corpus manifest says "canonical": true. `release` writes that, and only
for a release whose executables write portable slots and, with --gate, pass state_compat.py both ways
against every earlier canonical release; releases before (no portable slots) never get a corpus, so they are
never in the set. `release TAG` takes the release's Linux tarball (--tarball, as the release run made it,
before the upload; else downloaded from GitHub into the cache, checksum-verified), unpacks it in the cache
and makes the corpus of both its engines (US: the tarball is US only), into work/state-corpus/TAG.

--build adds executables of the same release that are no asset: the JP ones (for the
gate only: the release run builds them, packaging/build_release.py --write-linux, and never publishes them).
Either that region's Linux tarball of the tag (FILE.tar.gz, FILE.sha256 beside it: both engines) or a folder
(a build folder, an install; CONFIG=DIR to take one configuration of it). The corpus keeps them, since no
later release can download them: the tarball and its .sha256 as executables/<region>/<name>, a folder's
executable and schema as executables/<config>/bin/ and share/; the manifest's executable records name them
by their path in the corpus and their SHA-256, and state_compat.py runs them from there (the backward
direction of every later release).
"""

from __future__ import annotations

import argparse
import concurrent.futures
import datetime
import hashlib
import json
import os
import re
import shutil
import struct
import subprocess
import sys
import tarfile
import tempfile
import threading
import time
import urllib.error
import urllib.request
from dataclasses import dataclass, field
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import engine_diff as ed  # noqa: E402 (the scenes' pads, the logs' comparison)
import statecheck as sc  # noqa: E402 (the state format, the scenes, the discs)

FORMAT = 1
RUN = 600  # vblanks a loaded case runs (docs/CANONICAL_STATES.md 9: invariants hold for 600 vblanks)
WAIT = 120  # a slot save waits this long for a safe point (port/src/system/savestate.c), then saves as it is
CORPORA = ROOT / "work" / "state-corpus"
KEPT = "executables"  # a corpus' folder of the release's executables that are no asset (release --build)
CACHE = Path(os.environ.get("XG_RELEASE_CACHE", "/mnt/Games/scratch/xg-release-cache"))
REPO = "EtherAura/xenogears-pc"
PACKAGE = "xenogears-pc"
TAG = re.compile(r"v(\d{4})\.(0[1-9]|1[0-2])\.(0|[1-9]\d*)")  # linux_release.py's: vYYYY.MM.N
ENGINES = {"original": "xenogears", "enhanced": "xenogears-enhanced"}
# Start options (port/src/system/options.c): a reader boots the plain way and loads the slot at its first
# park, so only the load can give it the writer's moment; everything else (Speedup, battle speed) is a
# setting a player keeps, and the reader runs with it too.
BOOT_KEYS = ("XG_FIELD", "XG_BOOT_MODE", "XG_PARTY", "XG_PRESET", "XG_GAMESTATE")
# A run's environment starts from the host's without its XG_ variables but these (a caller's XG_STATE_SCHEMA,
# XG_STATE_DIR or dump folders must not reach a case).
KEEP_ENV = ("XG_VK_DEVICE", "XG_YIELD_JOBS")
OFF_TEXT = b"slots that other versions load come in a later release"  # stateportable.c, XG_PORTABLE_SLOTS off
# What portable_off finds of it: its first 32 bytes. A compiler may copy the literal in pieces: the release builder's
# GCC (packaging/deb_ubuntu.Dockerfile) loads its first 48 bytes from .rodata in 16-byte chunks and stores the rest as
# immediates, so the whole sentence is in no XG_DIST image (every release would have looked canonical, and
# its corpus failed). The text is in that #ifdef alone: no build with portable slots holds its start. A build whose
# image holds less of it is taken for portable and its corpus fails (no portable slot): held, never passed.
OFF_MARK = OFF_TEXT[:32]
SAFE_NAMES = {1: "field", 2: "battle", 3: "world map", 4: "Battling", 5: "menu"}  # include/pc_state.h PC_SAFE_*


class CorpusError(Exception):
    pass


# ---- cases ----------------------------------------------------------------------------------------------


@dataclass(frozen=True)
class Case:
    """One moment of the game: where a run starts (disc, start options), its input, the vblank its slot save is
    asked at, and the safe point it must land on (a mode, 1..5, or an exact id such as 0x201)."""
    id: str
    kind: str  # scene, field, battle
    disc: str  # "1" or "2"
    env: tuple  # start options and settings, as (key, value) pairs
    asked: int
    safe: int
    pad: tuple  # ("scene", name) | ("field", id) | ("battle", file, scene, gear, mash_from)
    why: str
    region: str = ""  # "": both; "us" or "jp": that region's variant of the case

    def boot_env(self) -> dict:
        return dict(self.env)

    def reader_env(self) -> dict:
        return {k: v for k, v in self.env if k not in BOOT_KEYS}


def scene_case(name: str, asked: int, safe: int, why: str) -> Case:
    s = sc.SCENES[name]
    return Case(name, "scene", s["disc"], tuple(sorted(s["env"].items())), asked, safe, ("scene", name), why)


def field_case(fid: int, why: str, disc: str = "1", asked: int = 1000, region: str = "") -> Case:
    env = {"XG_FIELD": str(fid), "XG_PARTY": "0", **({"XG_PRESET": "disc2"} if disc == "2" else {})}
    return Case(f"f{fid}", "field", disc, tuple(sorted(env.items())), asked, 1, ("field", fid), why, region)


SELECTOR_AT = 500  # the debug battle selector's first press (engine_diff.pad_selector's)


def battle_case(cid: str, file: int, scene: int, gear: int, asked: int, why: str, region: str = "") -> Case:
    """A battle from the debug selector (engine_diff.selector_presses), waiting at its first command menu
    (PC_SAFE_BATTLE_TURN, 0x201) when the save is asked: no input after Start until then, then the selector
    battles' mashing (Triangle, Cross)."""
    return Case(cid, "battle", "1", (("XG_BOOT_MODE", "2"),), asked, 0x201, ("battle", file, scene, gear, asked + 30),
                why, region)


# The statecheck scenes (tools/statecheck.py SCENES) whose moment a player can save portably. Left out: movie
# (no safe point during a movie: its slot is this version's only, by design, and every other build refuses it)
# and disc2swap (disc2's state; its swap is a load path of one build, statecheck run's).
SCENE_CASES = [
    scene_case("field", 700, 1, "field 8, walking (statecheck's field)"),
    scene_case("menu", 500, 5, "the menu mode's main menu (statecheck's menu; from about 600 its pad leaves for a field)"),
    scene_case("battle", 1600, 2, "a selector battle in progress (statecheck's battle; a Gear of 51998 HP)"),
    scene_case("worldmap", 700, 3, "the world map on foot (statecheck's worldmap; from about 900 it enters a field)"),
    scene_case("opening", 1550, 1, "the title screen's field 490 after the opening movie (statecheck's opening)"),
    scene_case("battling", 1300, 4, "the Battling arena (statecheck's battling)"),
    scene_case("disc2", 700, 1, "disc 2's field 616 (statecheck's disc2)"),
    scene_case("speedup", 1100, 2, "a battle with Speedup on (statecheck's speedup; its data load runs to about 1000)"),
    scene_case("battlespeed", 1500, 2, "a battle at 4x battle speed (statecheck's battlespeed)"),
]

# About 20 fields of the sweep (every field id, work/agents/PORT_M6/fields; names from
# port/src/extras/field_locations.cpp), each started cold (XG_FIELD, the default party) and walked with
# engine_diff's field pad: towns, interiors, dungeons, ships, cutscenes, Gear fields and disc 2.
FIELD_CASES = [
    field_case(2, "town: Lahan village square (the night of the attack)"),
    field_case(29, "town: Dazil, the water pump"),
    field_case(102, "town: Bledavik southern market"),
    field_case(195, "town: Nortune A Block street"),
    field_case(389, "town: Zeboim, Kim's laboratory"),
    field_case(423, "town: Shevat city overview"),
    field_case(529, "town: Etrenank immigration desk (Solaris)"),
    field_case(23, "dungeon: Blackmoon Forest, large tree"),
    field_case(64, "dungeon: Stalactite Cavern entry"),
    field_case(240, "dungeon: Nortune sewers"),
    field_case(368, "dungeon: Zeboim ruins, first cross passage"),
    field_case(406, "dungeon: Babel Tower entry walkway"),
    field_case(162, "interior: Gebler headquarters", region="us"),
    field_case(162, "interior: Gebler headquarters (JP's script prompts at about 1100)", asked=1200, region="jp"),
    field_case(282, "ship: Yggdrasil, Maison's counter"),
    field_case(4, "cutscene: Lahan, the opening scene"),
    field_case(161, "cutscene: Ramsus' memories, Elru battlefield"),
    field_case(172, "Gear field: Aveh mountains lower cave (Weltall walks, field_mecha)"),
    field_case(184, "Gear field: Aveh Desert, a Gear deployed"),
    field_case(628, "disc 2 dungeon: Anima Relic 1", disc="2"),
    field_case(702, "disc 2: Merkava outer entry ring (from about 1000 it leaves for the world map)", disc="2", asked=700),
]

# Three battles at their first command menu (PC_SAFE_BATTLE_TURN): on foot, in Gears, a boss. The selector's
# battle files differ between the discs, so the ordinary fights have a variant per region; the boss is the one
# statecheck's battle scene starts (the same Gear in both), reached by its pad and left idle at the menu.
BOSS_AT = 1400
BATTLE_CASES = [
    battle_case("b-foot", 2, 4, 0, 1450, "on foot: four ordinary enemies (selector file 2, 14 HP each)", "us"),
    battle_case("b-gear", 3, 0, 1, 1700, "in Gears: the party in Gears against one Gear (selector file 3, 500 HP)",
                "us"),
    battle_case("b-foot", 0, 4, 0, 1450, "on foot: six ordinary enemies (selector file 0, 200 HP each)", "jp"),
    battle_case("b-gear", 1, 0, 1, 1700, "in Gears: the party in Gears against four Gears (selector file 1, 150 HP)",
                "jp"),
    Case("b-boss", "battle", "1", (("XG_BOOT_MODE", "2"),), BOSS_AT, 0x201, ("boss", BOSS_AT + 30),
         "a boss: the Gear of 52,000 HP statecheck's battle fights, at its first command menu"),
]


def all_cases(region: str | None = None) -> list[Case]:
    """Every case, or a region's (its own variant of a case where it has one)."""
    cases = SCENE_CASES + FIELD_CASES + BATTLE_CASES
    return [c for c in cases if region is None or c.region in ("", region)]


def find_cases(ids: list[str], region: str | None = None) -> list[Case]:
    """The cases of these ids (all when none) for a region; an id no region knows is an error, one only the
    other region has is left out."""
    every = {c.id for c in all_cases()}
    missing = [i for i in ids if i not in every]
    if missing:
        raise CorpusError(f"unknown case {', '.join(missing)} (state_corpus.py list)")
    return [c for c in all_cases(region) if not ids or c.id in ids]


def write_pad(case: Case, path: Path) -> None:
    kind = case.pad[0]
    if kind == "scene":
        spec = ed.scene_spec(case.pad[1], None)
        ed.write_pad(path.parent, spec)
        if path.name != "pad.txt":
            (path.parent / "pad.txt").rename(path)
    elif kind == "field":
        sc.pad_script(path, 7 + case.pad[1], start=400, buttons=["2000", "8000", "1000", "4000", "0040", "0010"])
    elif kind == "boss":
        sc.pad_battle(path)  # statecheck's selector presses up to Start (778), then its mashing from 1000
        lines = [ln for ln in path.read_text().splitlines() if int(ln.split()[0]) < 1000]
        v, k = case.pad[1], 0
        while v < 20000:
            lines += [f"{v} {ed.TRIANGLE if k % 2 == 0 else ed.CROSS:04x}", f"{v + 6} 0000"]
            v, k = v + 20, k + 1
        path.write_text("\n".join(lines) + "\n")
    elif kind == "battle":
        _, file, scene, gear, mash_from = case.pad
        lines, t = [], SELECTOR_AT
        # The file row by Cross alone: its Triangle (+10) does not register with this pad (engine_diff's
        # --battle FILE of 10 and more lands on FILE % 10).
        for b in ed.selector_presses(0, scene, gear) + [ed.CROSS] * file:
            lines += [f"{t} {b:04x}", f"{t + 6} 0000"]
            t += 12
        start = t + 30
        lines += [f"{start} {ed.START:04x}", f"{start + 8} 0000"]
        v, k = max(mash_from, start + 240), 0
        while v < 20000:
            lines += [f"{v} {ed.TRIANGLE if k % 2 == 0 else ed.CROSS:04x}", f"{v + 6} 0000"]
            v, k = v + 20, k + 1
        path.write_text("\n".join(lines) + "\n")
    else:
        raise CorpusError(f"{case.id}: unknown pad {kind}")


# ---- builds ---------------------------------------------------------------------------------------------


@dataclass
class Build:
    """One configuration's executable and the schema it reads."""
    region: str
    engine: str
    exe: Path
    schema: Path | None
    source: dict = field(default_factory=dict)

    @property
    def config(self) -> str:
        return f"{self.region}-{self.engine}"

    def image_id(self) -> str:
        return image_id(self.exe)


_ids: dict = {}


def image_id(exe: Path) -> str:
    """An executable's image identity, the build id its states carry (statefile.c stateImageId at bias 0;
    stripping keeps it), once per file."""
    st = exe.stat()
    key = (str(exe), st.st_mtime_ns, st.st_size)
    if key not in _ids:
        import state_schema  # (the identity, as the schema tool computes it from the file)
        _ids[key] = state_schema.image_identity(exe)["id"]
    return _ids[key]


def schema_version(path: Path) -> str | None:
    """A schema's "version" (the region: us or jp) from its first bytes, in either form (the packed one's
    first block alone is unpacked)."""
    data = path.read_bytes()[:1 << 21]
    if data[:8] == sc.SCHEMA_MAGIC:
        raw, packed = struct.unpack_from("<II", data, 16)
        block = data[24:24 + packed]
        data = block if packed == raw else sc.unpack_block(block, raw)
    m = re.search(rb'"version"\s*:\s*"(\w+)"', data[:4096])
    return m.group(1).decode() if m else None


def resolve_builds(path: Path, region: str | None = None, region_default: str | None = None) -> list[Build]:
    """The configurations a path holds: a build folder (one), an installed prefix or unpacked tarball (one per
    engine in bin/), or an executable. The region: given, else the schema's, else the build's cache, else
    region_default."""
    path = path.absolute()
    found: list[tuple[Path, Path | None]] = []
    if path.is_file():
        exe = path
        cand = [exe.parent / "state_schema.json", exe.parent / f"{exe.name}.xgschema",
                exe.parent.parent / "share" / PACKAGE / f"{exe.name}.xgschema"]
        found.append((exe, next((c for c in cand if c.is_file()), None)))
    elif (path / "bin").is_dir() and not (path / "CMakeCache.txt").exists():
        for name in ENGINES.values():
            exe = path / "bin" / name
            if exe.is_file():
                schema = path / "share" / PACKAGE / f"{name}.xgschema"
                found.append((exe, schema if schema.is_file() else None))
    else:
        for name in ENGINES.values():
            exe = path / name
            if exe.is_file():
                schema = next((c for c in (path / "state_schema.json", path / f"{name}.xgschema") if c.is_file()), None)
                found.append((exe, schema))
                break  # a build folder has one engine (XG_ENGINE)
    if not found:
        raise CorpusError(f"{path}: no xenogears or xenogears-enhanced executable (a build folder, an installed "
                          "prefix or an unpacked release)")
    builds = []
    for exe, schema in found:
        engine = "enhanced" if exe.name.startswith(ENGINES["enhanced"]) else "original"
        reg = region or (schema_version(schema) if schema else None) or cache_region(path) or region_default
        if reg not in ("us", "jp"):
            raise CorpusError(f"{exe}: its region is not known (no schema: give --region)")
        builds.append(Build(reg, engine, exe, schema, {"kind": "build", "path": str(path)}))
    return builds


def cache_region(path: Path) -> str | None:
    cache = path / "CMakeCache.txt"
    if cache.is_file():
        m = re.search(r"^XG_VERSION:\w+=(\w+)$", cache.read_text(errors="replace"), re.M)
        if m:
            return m.group(1).lower()
    return None


def portable_off(exe: Path) -> bool:
    """A build whose portable slots are switched off (port/CMakeLists.txt XG_PORTABLE_SLOTS=OFF, a release's
    until P5) carries stateportable.c's reason for it (OFF_MARK: its start)."""
    with open(exe, "rb") as f:
        data = f.read()
    return OFF_MARK in data


# ---- running the game -----------------------------------------------------------------------------------

_said = threading.Lock()


def disc_path(region: str, disc: str, discs: Path | None = None) -> Path:
    rel = Path(sc.DISCS[region + disc])
    return (discs / rel.relative_to("bin")) if discs else ROOT / rel


def run_game(exe: Path, out: Path, image: Path, quit_at: int, env: dict, timeout: int = 300) -> int:
    """statecheck.run_port's run, for one executable named outright (a release's bin/ holds both engines):
    headless, the stepped clock, no settings, cards of its own, no frame or sound dumps unless env asks."""
    out.mkdir(parents=True, exist_ok=True)
    (out / "cards").mkdir(exist_ok=True)
    e = {k: v for k, v in os.environ.items() if not k.startswith("XG_") or k in KEEP_ENV}
    e.update({
        "XG_HEADLESS": "1", "SDL_VIDEO_DRIVER": "offscreen", "SDL_AUDIO_DRIVER": "dummy", "XG_SETTINGS": "none",
        "XG_CLOCK": "step", "XG_QUIT_AT": str(quit_at), "XG_CARD_DIR": str(out / "cards"),
        "XG_VK_DEVICE": e.get("XG_VK_DEVICE", "1"), "TMPDIR": str(out),
    })
    e.update(env)
    with open(out / "port.log", "w") as log:
        try:
            p = subprocess.run(["nice", "-n", "19", "ionice", "-c3", "timeout", "-s", "INT", "-k", "30", str(timeout),
                                str(exe), str(image)], env=e, stdout=log, stderr=subprocess.STDOUT, cwd=out,
                               stdin=subprocess.DEVNULL)
        except OSError as error:
            log.write(f"state_corpus: cannot run {exe}: {error}\n")
            return 127
    if p.returncode != 0:
        lost = sc.DEVICE_LOST.search((out / "port.log").read_text(errors="replace"))
        if lost is not None:
            with _said:
                sys.stdout.write(lost.group(0) + "\n")
                sys.stdout.flush()
    return p.returncode


def log_rows(path: Path, lo: int, hi: int) -> list[str]:
    """A log's lines of vblanks lo+1..hi (every line's second token is its vblank: invariants.c, sweeplog.c)."""
    rows = []
    if not path.exists():
        return rows
    for line in path.read_text(errors="replace").splitlines():
        parts = line.split(None, 2)
        if len(parts) >= 2 and parts[1].isdigit() and lo < int(parts[1]) <= hi:
            rows.append(line)
    return rows


def first_difference(ref: list[str], got: list[str], tolerate: int = 0) -> str | None:
    for i, (x, y) in enumerate(zip(ref, got)):
        if not ed.lines_equal(x, y, tolerate):
            cut = lambda s: s[:160] + ("..." if len(s) > 160 else "")  # noqa: E731
            return f"line {i + 1} of {len(ref)}/{len(got)}: writer {cut(x)!r}, reader {cut(y)!r}"
    if len(ref) != len(got):
        return f"{len(ref)} lines in the writer's, {len(got)} in the reader's (the first {min(len(ref), len(got))} equal)"
    return None


def state_lines(log: str) -> list[str]:
    return [line for line in log.splitlines() if line.startswith("state:")]


@dataclass
class Loaded:
    """What a reader made of a corpus case (check_load)."""
    ok: bool
    problems: list[str]
    notes: list[str]
    how: str = ""  # "raw" or "converted"
    seconds: float = 0.0
    lines: tuple = (0, 0)


def check_load(reader: Build, corpus: Path, rec: dict, work: Path, discs: Path | None = None,
               tolerate: int = 0, logs: tuple = ("invariants", "sweep"), keep: bool = False) -> Loaded:
    """Load one corpus case in a reader and hold it to the writer's continuation. The definition of "the
    invariants hold for 600 vblanks" (docs/CANONICAL_STATES.md 9.3) used by the corpus' own verification
    and by the state-compat gate:

      no refusal  the reader's log says "state: Loaded slot 1" and no "Cannot load slot"; a reader of
                  another build than the writer's must have converted it (", converted from <writer>":
                  "version X" when both builds carry a release version and they differ, else "another build
                  of the port"; stateportable.c statePortableWriter), the writer's own build loads it raw;
      no crash    the run exits 0 at XG_QUIT_AT = V + 600 (V: the vblank the slot was saved at; the count a
                  load restores), within the time limit;
      invariants  the reader's invariant log (invariants.c: per vblank the screen kind, a hash of gameState,
                  of the field variables and the leader's position, the world map's entries; every memory
                  card frame written) and its sweep log (the field's and the battle's per-frame state: the
                  random seed, every actor's position, script position and animation, every battle
                  entity's HP, fuel and status, the turn, AP, the camera) for vblanks V+1..V+600 equal the
                  writer's own continuation recorded in the corpus, line for line (engine_diff.py's
                  comparison; --tolerate N lets positions differ by N units, as engine_diff's does).

    Frames and sound are not compared: releases may draw and mix differently on purpose (the Enhanced
    engine above all); the game's state may not change."""
    case_dir = corpus / rec["dir"]
    v, end = rec["vblank"], rec["vblank"] + rec["run"]
    shutil.rmtree(work, ignore_errors=True)
    states = work / "states"
    (states / "schemas").mkdir(parents=True)
    shutil.copyfile(case_dir / "slot1.xgs", states / "slot1.xgs")
    schema = corpus / rec["config"] / "schemas" / f"{rec['schema']}.xgschema"
    if schema.is_file():
        shutil.copyfile(schema, states / "schemas" / schema.name)
    region, disc = rec["region"], rec["disc"]
    env = {**rec["reader_env"], "XG_PAD_SCRIPT": str(case_dir / "pad.txt"), "XG_INVARIANT_LOG": "invariants.log",
           "XG_SWEEP_LOG": "sweep.log", "XG_STATE_DIR": str(states), "XG_STATE_SLOT_LOAD": "1",
           "XG_DISC2": str(disc_path(region, "2" if disc == "1" else "1", discs))}
    t0 = time.monotonic()
    # Quit one vblank later: XG_QUIT_AT ends the run right after that vblank's handlers, before its park
    # writes the vblank's log lines.
    code = run_game(reader.exe, work, disc_path(region, disc, discs), end + 1, env)
    seconds = time.monotonic() - t0
    log = (work / "port.log").read_text(errors="replace") if (work / "port.log").exists() else ""
    problems, notes = [], []
    refused = re.search(r"state: Cannot load slot 1: [^\n]*", log)
    loaded = re.search(r"state: Loaded slot 1[^\n]*", log)
    how = ""
    if refused:
        problems.append("refused: " + refused.group(0)[len("state: "):])
    elif not loaded:
        problems.append("no load (" + ("; ".join(state_lines(log)[-3:]) or "the log says nothing of slot 1") + ")")
    else:
        how = "converted" if ", converted from " in loaded.group(0) else "raw"
        same = reader.image_id() == rec["build"]
        if same and how != "raw":
            problems.append("the writer's own build converted its slot instead of loading it raw")
        elif not same and how != "converted":
            problems.append("another build loaded the slot without converting it")
        warn = re.search(r"in \d+ ms: (\d+) encode and (\d+) decode warnings", log)
        if warn and int(warn.group(2)):
            notes.append(f"{warn.group(2)} decode warnings")
        carried = re.search(r"(\d+) values? not carried", log)
        if carried:
            notes.append(f"{carried.group(1)} values not carried")
    if code != 0:
        problems.append(f"exit {code}" + (" (timed out)" if code in (124, 137) else ""))
    counts = []
    for what in logs:
        ref = (case_dir / f"{what}.log").read_text().splitlines() if (case_dir / f"{what}.log").exists() else []
        got = log_rows(work / f"{what}.log", v, end)
        counts.append(len(ref))
        if loaded and not refused:
            d = first_difference(ref, got, tolerate)
            if d:
                problems.append(f"{what}: {d}")
    if not problems and not keep:
        shutil.rmtree(work, ignore_errors=True)
    return Loaded(not problems, problems, notes, how, seconds, tuple(counts))


# ---- making a case --------------------------------------------------------------------------------------


def sha256(path: Path) -> str:
    with open(path, "rb") as f:
        return hashlib.file_digest(f, "sha256").hexdigest()


def make_case(build: Build, case: Case, work: Path, discs: Path | None, verify: bool) -> dict:
    """Run the writer to the case's moment and on to V + RUN; the record and its files in work/out, or
    CorpusError with why."""
    shutil.rmtree(work, ignore_errors=True)
    out = work / "out"
    out.mkdir(parents=True)
    write_pad(case, out / "pad.txt")
    states = work / "w" / "states"
    other = "2" if case.disc == "1" else "1"
    env = {**case.boot_env(), "XG_PAD_SCRIPT": str(out / "pad.txt"), "XG_INVARIANT_LOG": "invariants.log",
           "XG_SWEEP_LOG": "sweep.log", "XG_STATE_DIR": str(states), "XG_STATE_SLOT_SAVE": f"{case.asked}:1",
           "XG_DISC2": str(disc_path(build.region, other, discs))}
    t0 = time.monotonic()
    quit_at = case.asked + 2 * WAIT + RUN + 2  # (a safe point can come a little after the wait's deadline)
    code = run_game(build.exe, work / "w", disc_path(build.region, case.disc, discs), quit_at, env)
    seconds = time.monotonic() - t0
    log = (work / "w" / "port.log").read_text(errors="replace")
    where = f"{build.config}/{case.id}"
    if code != 0:
        raise CorpusError(f"{where}: the writer exited {code} ({'; '.join(state_lines(log)[-2:]) or 'no state line'})")
    saved = re.search(r"state: saving \S*slot1\.xgs at vblank (\d+)", log)
    slot = states / "slot1.xgs"
    if not saved or not slot.is_file() or "State saved (slot 1)" not in log:
        raise CorpusError(f"{where}: no slot saved (asked at {case.asked})")
    v = int(saved.group(1))
    if v + RUN >= quit_at:
        raise CorpusError(f"{where}: saved at {v}, too late for {RUN} vblanks of continuation (asked at {case.asked})")
    trailer = sc.read_trailer(slot)
    if trailer is None:
        why = re.search(r"state: [^\n]*(this version only|not canonical)[^\n]*", log)
        raise CorpusError(f"{where}: the slot saved at {v} is not portable"
                          + (f" ({why.group(0)[7:]})" if why else " (no safe point came, or the build writes none)"))
    schema = states / "schemas" / f"{trailer['schemaHash']}.xgschema"
    if not schema.is_file():
        raise CorpusError(f"{where}: the writer's schema {trailer['schemaHash']} is not filed in the save folder")
    safe = trailer["safePoint"]
    want = case.safe
    if (want > 0xFF and safe != want) or (want <= 0xFF and safe >> 8 != want):
        raise CorpusError(f"{where}: saved at vblank {v} at safe point {safe:#x} ({SAFE_NAMES.get(safe >> 8, '?')}), "
                          f"not the case's {want:#x}" if want > 0xFF else
                          f"{where}: saved at vblank {v} at safe point {safe:#x}, not a {SAFE_NAMES[want]} one")
    # The memory cards are host files, not state: a card frame the writer wrote before its save would be
    # missing from a reader's fresh cards.
    early = [r for r in log_rows(work / "w" / "invariants.log", 0, v) if r.startswith("c ")]
    if early:
        raise CorpusError(f"{where}: the game wrote {len(early)} memory card frames before the save (cards are "
                          "not in a state)")
    for what in ("invariants", "sweep"):
        rows = log_rows(work / "w" / f"{what}.log", v, v + RUN)
        (out / f"{what}.log").write_text("".join(r + "\n" for r in rows))
    inv = (out / "invariants.log").read_text().splitlines()
    if not inv:
        raise CorpusError(f"{where}: no invariant line after the save")
    shutil.copyfile(slot, out / "slot1.xgs")
    header, body = sc.read_state(slot)
    rec = {
        "id": where, "config": build.config, "case": case.id, "kind": case.kind, "why": case.why,
        "region": build.region, "engine": build.engine, "disc": case.disc,
        "boot_env": case.boot_env(), "reader_env": case.reader_env(), "pad_kind": list(case.pad),
        "asked": case.asked, "vblank": v, "run": RUN, "safe_point": safe, "safe_point_hex": f"{safe:#x}",
        "mode": header["mode"], "place": header["place"], "label": header["label"], "build": header["build"],
        "schema": trailer["schemaHash"], "strings": len(trailer["strings"]),
        "strings_bytes": sum(len(s) for s in trailer["strings"].values()),
        "dir": f"{build.config}/{case.id}", "slot_bytes": slot.stat().st_size, "body_bytes": len(body),
        "slot_sha256": sha256(slot),
        "invariants": {"lines": len(inv), "sha256": sha256(out / "invariants.log")},
        "sweep": {"lines": len((out / "sweep.log").read_text().splitlines()), "sha256": sha256(out / "sweep.log")},
        "seconds": round(seconds, 1),
    }
    (work / "schema").mkdir()
    shutil.copyfile(schema, work / "schema" / schema.name)
    if verify:
        # The writer's build loads its own slot raw and must give its own continuation.
        tmp = work / "corpus"
        (tmp / build.config / "schemas").mkdir(parents=True)
        shutil.copytree(out, tmp / rec["dir"])
        shutil.copyfile(schema, tmp / build.config / "schemas" / schema.name)
        got = check_load(build, tmp, rec, work / "v", discs)
        if not got.ok:
            raise CorpusError(f"{where}: its own build does not repeat it: " + "; ".join(got.problems))
        rec["verified"] = {"how": got.how, "seconds": round(got.seconds, 1)}
        shutil.rmtree(tmp, ignore_errors=True)
    shutil.rmtree(work / "w", ignore_errors=True)
    return rec


# ---- the corpus -----------------------------------------------------------------------------------------


def release_order(name: str, created: str = "") -> list:
    """Sort key of a release: a calendar tag by its numbers, anything else (a local stand-in) after them by
    the time its corpus was made."""
    m = TAG.fullmatch(name)
    return [0, int(m.group(1)), int(m.group(2)), int(m.group(3)), ""] if m else [1, 0, 0, 0, created]


def make_corpus(builds: list[Build], out: Path, release: str, case_ids: list[str] | None = None, jobs: int = 4,
                discs: Path | None = None, verify: bool = True, canonical: bool = False, source: dict | None = None,
                keep_going: bool = False, replace: bool = False, quiet: bool = False) -> dict:
    """Every case (of case_ids; all when empty) of every build, each build's region's variant, into a new
    corpus folder out (made beside it, then moved in place)."""
    out = out.absolute()
    if out.exists() and not replace:
        raise CorpusError(f"{out} exists (--replace makes it again)")
    configs = [b.config for b in builds]
    if len(set(configs)) != len(configs):
        raise CorpusError(f"two builds of one configuration: {', '.join(configs)}")
    for b in builds:
        if b.schema is None:
            raise CorpusError(f"{b.exe}: no state schema beside it: its slots would not be portable")
        if portable_off(b.exe):
            raise CorpusError(f"{b.exe}: portable slots are off in this build (XG_PORTABLE_SLOTS): it writes no "
                              "canonical state")
    out.parent.mkdir(parents=True, exist_ok=True)
    stage = Path(tempfile.mkdtemp(prefix=f".{out.name}.partial-", dir=out.parent))
    base = Path(os.environ.get("XG_CORPUS_WORK", "/mnt/Games/scratch"))
    work = Path(tempfile.mkdtemp(prefix="state-corpus-", dir=base if base.is_dir() else out.parent))
    t0 = time.monotonic()
    records, failed = [], []
    lock = threading.Lock()

    def one(b: Build, c: Case):
        rec = make_case(b, c, work / b.config / c.id, discs, verify)
        with lock:
            shutil.copytree(work / b.config / c.id / "out", stage / rec["dir"])
            (stage / b.config / "schemas").mkdir(parents=True, exist_ok=True)
            for s in (work / b.config / c.id / "schema").iterdir():
                if not (stage / b.config / "schemas" / s.name).exists():
                    shutil.copyfile(s, stage / b.config / "schemas" / s.name)
        shutil.rmtree(work / b.config / c.id, ignore_errors=True)
        return rec

    try:
        with concurrent.futures.ThreadPoolExecutor(max(1, jobs)) as pool:
            futures = {pool.submit(one, b, c): (b, c) for b in builds for c in find_cases(case_ids or [], b.region)}
            for f in concurrent.futures.as_completed(futures):
                b, c = futures[f]
                try:
                    rec = f.result()
                except CorpusError as error:
                    failed.append({"id": f"{b.config}/{c.id}", "why": str(error)})
                    if not quiet:
                        print(f"FAIL {error}", flush=True)
                    continue
                records.append(rec)
                if not quiet:
                    print(f"ok   {rec['id']}: {rec['label'] or '-'}, saved at {rec['vblank']} (asked {rec['asked']}) at "
                          f"{rec['safe_point_hex']}, {rec['slot_bytes']} bytes, {rec['invariants']['lines']}+"
                          f"{rec['sweep']['lines']} lines" + (f", verified {rec['verified']['how']}" if verify else "")
                          + f" ({rec['seconds']} s)", flush=True)
        empty = [b.config for b in builds if not any(r["config"] == b.config for r in records)]
        if empty:
            raise CorpusError(f"no state of {', '.join(empty)} made it into the corpus: "
                              + "; ".join(x["why"] for x in failed[:2]))
        if failed and not keep_going:
            raise CorpusError(f"{len(failed)} of {len(failed) + len(records)} cases failed: "
                              + "; ".join(x["why"] for x in failed[:3]) + (" ..." if len(failed) > 3 else ""))
        order = list(dict.fromkeys(c.id for c in all_cases()))
        records.sort(key=lambda r: (configs.index(r["config"]), order.index(r["case"])))
        manifest = {
            "format": FORMAT, "kind": "xenogears-pc state corpus", "release": release,
            "canonical": canonical, "created": datetime.datetime.now(datetime.timezone.utc).isoformat(timespec="seconds"),
            "run": RUN, "source": source or {},
            "tool": {"git": git_rev(), "cases": sorted(case_ids or [])},
            "configs": {},
            "cases": records, "failed": failed,
        }
        manifest["order"] = release_order(release, manifest["created"])
        for b in builds:
            recs = [r for r in records if r["config"] == b.config]
            manifest["configs"][b.config] = {
                "region": b.region, "engine": b.engine, "build": recs[0]["build"] if recs else b.image_id(),
                "schemas": sorted({r["schema"] for r in recs}), "executable": b.source.get("executable")
                or {"kind": "local", "path": str(b.exe)}, "cases": len(recs),
            }
        manifest["bytes"] = sum(p.stat().st_size for p in stage.rglob("*") if p.is_file())
        manifest["seconds"] = round(time.monotonic() - t0, 1)
        write_json(stage / "manifest.json", manifest)
        if out.exists():
            shutil.rmtree(out)
        stage.rename(out)
        return manifest
    finally:
        shutil.rmtree(work, ignore_errors=True)
        if stage.exists():
            shutil.rmtree(stage, ignore_errors=True)


def git_rev() -> str:
    p = subprocess.run(["git", "-C", str(ROOT), "rev-parse", "--short", "HEAD"], capture_output=True, text=True)
    return p.stdout.strip()


def write_json(path: Path, data: dict) -> None:
    tmp = path.with_name(path.name + ".tmp")
    tmp.write_text(json.dumps(data, indent=1, sort_keys=True) + "\n")
    tmp.replace(path)


def read_manifest(corpus: Path) -> dict:
    """A corpus' manifest, checked for what the readers rely on (CorpusError otherwise)."""
    path = corpus / "manifest.json"
    try:
        m = json.loads(path.read_text())
    except (OSError, ValueError) as error:
        raise CorpusError(f"{path}: {error}") from error
    if not isinstance(m, dict) or m.get("kind") != "xenogears-pc state corpus":
        raise CorpusError(f"{path}: not a state corpus manifest")
    if m.get("format") != FORMAT:
        raise CorpusError(f"{path}: format {m.get('format')} (this tool reads {FORMAT})")
    for key, kind in (("release", str), ("canonical", bool), ("configs", dict), ("cases", list), ("order", list)):
        if not isinstance(m.get(key), kind):
            raise CorpusError(f"{path}: no {key}")
    need = ("id", "config", "dir", "region", "disc", "vblank", "run", "schema", "build", "reader_env")
    for r in m["cases"]:
        if not isinstance(r, dict) or any(k not in r for k in need):
            raise CorpusError(f"{path}: a case without {', '.join(k for k in need if k not in (r or {}))}")
        if r["config"] not in m["configs"]:
            raise CorpusError(f"{path}: case {r['id']} of an unlisted configuration {r['config']}")
        if ".." in Path(r["dir"]).parts or Path(r["dir"]).is_absolute():
            raise CorpusError(f"{path}: case {r['id']} outside the corpus")
    for config, info in m["configs"].items():
        for rel, _ in kept_files(info.get("executable") if isinstance(info, dict) else None):
            if not rel or ".." in Path(rel).parts or Path(rel).is_absolute() or Path(rel).parts[0] != KEPT:
                raise CorpusError(f"{path}: {config}'s executable outside the corpus' {KEPT}/")
    return m


def kept_files(executable: dict | None) -> list[tuple[str, str | None]]:
    """The files a configuration's executable record keeps in the corpus (release --build), as (path in the corpus,
    SHA-256): a kind "tarball" record with "kept" (the tarball), a kind "kept" one (the executable and its schema)."""
    if not isinstance(executable, dict):
        return []
    if executable.get("kind") == "tarball" and "kept" in executable:
        return [(str(executable["kept"]), executable.get("sha256"))]
    if executable.get("kind") == "kept":
        schema = executable.get("schema") if isinstance(executable.get("schema"), dict) else None
        return [(str(executable.get("path") or ""), executable.get("sha256"))] + (
            [(str(schema.get("path") or ""), schema.get("sha256"))] if schema else [])
    return []


def check_corpus(corpus: Path) -> list[str]:
    """What is wrong with a corpus on disk: its manifest, every file it names, their hashes."""
    try:
        m = read_manifest(corpus)
    except CorpusError as error:
        return [str(error)]
    problems = []
    for r in m["cases"]:
        d = corpus / r["dir"]
        for name in ("slot1.xgs", "pad.txt", "invariants.log", "sweep.log"):
            if not (d / name).is_file():
                problems.append(f"{r['id']}: no {name}")
        if (d / "slot1.xgs").is_file() and r.get("slot_sha256") and sha256(d / "slot1.xgs") != r["slot_sha256"]:
            problems.append(f"{r['id']}: slot1.xgs changed")
        for what in ("invariants", "sweep"):
            want = (r.get(what) or {}).get("sha256")
            if want and (d / f"{what}.log").is_file() and sha256(d / f"{what}.log") != want:
                problems.append(f"{r['id']}: {what}.log changed")
        if not (corpus / r["config"] / "schemas" / f"{r['schema']}.xgschema").is_file():
            problems.append(f"{r['id']}: its writer's schema {r['schema']} is missing")
    for config, info in m["configs"].items():
        for rel, digest in kept_files(info.get("executable")):
            if not (corpus / rel).is_file():
                problems.append(f"{config}: no {rel} (kept in the corpus: the release has no such asset)")
            elif not digest or sha256(corpus / rel) != digest:
                problems.append(f"{config}: {rel} changed")
    return problems


def corpora(root: Path) -> list[tuple[Path, dict]]:
    """Every canonical corpus under root, earliest release first. A folder without a manifest, or whose
    manifest does not say canonical, is not a canonical release's (a release before canonical states, a
    rejected or unfinished one, a stand-in)."""
    found = []
    if not root.is_dir():
        return found
    for d in sorted(root.iterdir()):
        if d.name.startswith(".") or not (d / "manifest.json").is_file():
            continue
        m = read_manifest(d)
        if m["canonical"]:
            found.append((d, m))
    found.sort(key=lambda x: x[1]["order"])
    return found


# ---- release tarballs: the cache -------------------------------------------------------------------------


def tarball_name(tag: str) -> str:
    if not TAG.fullmatch(tag):
        raise CorpusError(f"release tag {tag!r} is not vYYYY.MM.N")
    return f"{PACKAGE}-{tag[1:]}-linux-x86_64.tar.gz"


class ReleaseCache:
    """Each release's Linux tarball, downloaded once (or copied from a local stand-in of the release assets,
    assets_from/<tag>/<asset>) into root/<tag>/, checked against the release's published SHA-256 (its .sha256
    asset, GitHub's digest of the asset when it gives one, and the corpus manifest's record), and unpacked
    beside it. A tarball whose sums disagree is never used; one with no published sum is used with a note."""

    def __init__(self, root: Path = CACHE, repo: str = REPO, assets_from: Path | None = None, offline: bool = False):
        self.root, self.repo, self.assets_from, self.offline = root, repo, assets_from, offline
        self.notes: list[str] = []
        self.lock = threading.Lock()

    def _fetch(self, tag: str, name: str, dest: Path) -> bool:
        """One asset into dest; False when the release has no such asset."""
        if self.assets_from is not None:
            src = self.assets_from / tag / name
            if not src.is_file():
                return False
            shutil.copyfile(src, dest)
            return True
        if self.offline:
            raise CorpusError(f"{tag}: {name} is not in the cache and the cache is offline")
        url = f"https://github.com/{self.repo}/releases/download/{tag}/{name}"
        req = urllib.request.Request(url, headers={"User-Agent": "xenogears-state-compat"})
        try:
            with urllib.request.urlopen(req, timeout=120) as r, open(dest, "wb") as f:
                shutil.copyfileobj(r, f)
        except urllib.error.HTTPError as error:
            if error.code == 404:
                return False
            raise CorpusError(f"{tag}: downloading {name}: {error}") from error
        except (urllib.error.URLError, OSError) as error:
            raise CorpusError(f"{tag}: downloading {name}: {error}") from error
        return True

    def _api_digest(self, tag: str, name: str) -> str | None:
        """GitHub's own SHA-256 of a release asset (the API's "digest"), when it gives one."""
        if self.assets_from is not None:
            d = self.assets_from / tag / "digests.json"
            if d.is_file():
                return json.loads(d.read_text()).get(name, "").removeprefix("sha256:") or None
            return None
        if self.offline:
            return None
        url = f"https://api.github.com/repos/{self.repo}/releases/tags/{tag}"
        req = urllib.request.Request(url, headers={"User-Agent": "xenogears-state-compat",
                                                   "Accept": "application/vnd.github+json"})
        try:
            with urllib.request.urlopen(req, timeout=60) as r:
                data = json.load(r)
        except (urllib.error.URLError, OSError, ValueError):
            return None
        for a in data.get("assets") or []:
            if a.get("name") == name and str(a.get("digest") or "").startswith("sha256:"):
                return a["digest"][7:]
        return None

    def tarball(self, tag: str, expect: str | None = None, local: Path | None = None) -> Path:
        """The verified tarball of a release (local: a file to take it from, with FILE.sha256 beside it)."""
        name = tarball_name(tag)
        folder = self.root / tag
        path = folder / name
        with self.lock:
            record = folder / "verified.json"
            if path.is_file() and record.is_file():
                done = json.loads(record.read_text())
                if done.get("sha256") == sha256(path) and (expect is None or done["sha256"] == expect):
                    return path
            folder.mkdir(parents=True, exist_ok=True)
            tmp = Path(tempfile.mkdtemp(prefix=".fetch-", dir=folder))
            try:
                if local is not None:
                    shutil.copyfile(local, tmp / name)
                    side = local.with_name(local.name + ".sha256")
                    has_sum = side.is_file()
                    if has_sum:
                        shutil.copyfile(side, tmp / f"{name}.sha256")
                    api = None
                else:
                    if not self._fetch(tag, name, tmp / name):
                        raise CorpusError(f"{tag}: the release has no {name}")
                    has_sum = self._fetch(tag, f"{name}.sha256", tmp / f"{name}.sha256")
                    api = self._api_digest(tag, name)
                got = sha256(tmp / name)
                checks = []
                if has_sum:
                    text = (tmp / f"{name}.sha256").read_text()
                    if text != f"{got}  {name}\n":
                        raise CorpusError(f"{tag}: {name} does not match its published {name}.sha256")
                    checks.append("sha256 asset")
                if api is not None:
                    if api != got:
                        raise CorpusError(f"{tag}: {name} is not the asset GitHub has (digest {api[:12]}...)")
                    checks.append("GitHub digest")
                if expect is not None:
                    if expect != got:
                        raise CorpusError(f"{tag}: {name} is {got[:12]}..., the corpus was made from {expect[:12]}...")
                    checks.append("corpus manifest")
                if not checks:
                    self.notes.append(f"{tag}: {name} has no published SHA-256: used unverified")
                for f in tmp.iterdir():
                    f.replace(folder / f.name)
                write_json(record, {"sha256": got, "checked": checks, "asset": name})
                return path
            finally:
                shutil.rmtree(tmp, ignore_errors=True)

    def unpacked(self, tag: str, expect: str | None = None, local: Path | None = None) -> Path:
        """The release's folder, unpacked from its verified tarball (again when the tarball changed)."""
        path = self.tarball(tag, expect, local)
        digest = json.loads((self.root / tag / "verified.json").read_text())["sha256"]
        return self.unpack(tag, path, digest)

    def unpack(self, tag: str, path: Path, digest: str) -> Path:
        """A verified tarball of the release (its digest given) unpacked once into the cache, by its digest: the
        published one, or one the release kept in its corpus (release --build: another region's, of the same
        name)."""
        top = self.root / tag / f"unpacked-{digest[:16]}"
        folder = top / tarball_name(tag).removesuffix(".tar.gz")
        with self.lock:
            if (top / ".complete").is_file() and folder.is_dir():
                return folder
            shutil.rmtree(top, ignore_errors=True)
            (self.root / tag).mkdir(parents=True, exist_ok=True)
            tmp = Path(tempfile.mkdtemp(prefix=".unpack-", dir=self.root / tag))
            try:
                safe_extract(path, tmp, folder.name)
                (tmp / ".complete").write_text(digest + "\n")
                tmp.rename(top)
            finally:
                shutil.rmtree(tmp, ignore_errors=True)
        return folder

    def executable(self, release: str, executable: dict, corpus: Path | None = None) -> Path:
        """A corpus configuration's executable from its manifest record (corpus: the corpus' folder, where
        release --build kept the executables that are no asset)."""
        kind = executable.get("kind")
        kept = kept_files(executable)
        if kept and corpus is None:
            raise CorpusError(f"{release}: its executable is kept in its corpus, and no corpus was named")
        for rel, digest in kept:
            if not (corpus / rel).is_file():
                raise CorpusError(f"{release}: no {corpus / rel} (kept in its corpus: no asset has it)")
            if not digest or sha256(corpus / rel) != digest:
                raise CorpusError(f"{release}: {corpus / rel} changed since its corpus was made")
        if kind == "tarball" and kept:
            folder = self.unpack(executable["tag"], corpus / kept[0][0], kept[0][1])
            exe = folder / executable["path"]
        elif kind == "tarball":
            folder = self.unpacked(executable["tag"], executable.get("sha256"))
            exe = folder / executable["path"]
        elif kind == "kept":
            exe = corpus / kept[0][0]
        elif kind == "local":
            exe = Path(executable["path"])
        else:
            raise CorpusError(f"{release}: an executable of kind {kind!r}")
        if not exe.is_file():
            raise CorpusError(f"{release}: no {exe}")
        return exe


def safe_extract(tarball: Path, dest: Path, top: str) -> None:
    """A release tarball: one folder (top), folders and regular files only, no name outside it."""
    try:
        with tarfile.open(tarball) as archive:
            members = archive.getmembers()
            for m in members:
                parts = Path(m.name).parts
                if (not (m.isdir() or m.isfile()) or not parts or parts[0] != top or ".." in parts
                        or Path(m.name).is_absolute()):
                    raise CorpusError(f"{tarball.name}: {m.name!r} is not a file or folder under {top}/")
            archive.extractall(dest, members=members, filter="data")
    except (tarfile.TarError, EOFError) as error:
        raise CorpusError(f"{tarball.name}: not a release tarball ({error})") from error


# ---- commands -------------------------------------------------------------------------------------------


def default_jobs() -> int:
    return max(1, int(os.environ.get("XG_YIELD_JOBS") or max(2, (os.cpu_count() or 4) // 4)))


def cmd_make(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(prog="state_corpus.py", description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("paths", nargs="+", type=Path, help="BUILD [BUILD ...] OUT_DIR")
    ap.add_argument("--release", help="the corpus' release name (default: OUT_DIR's name)")
    ap.add_argument("--canonical", action="store_true",
                    help="mark it a canonical release's (`release` does this for a release that passed the gate; "
                         "for a local stand-in of one)")
    ap.add_argument("--case", action="append", default=[], help="only this case (state_corpus.py list)")
    ap.add_argument("--jobs", type=int, default=default_jobs())
    ap.add_argument("--no-verify", action="store_true", help="do not load each slot back in its own build")
    ap.add_argument("--keep-going", action="store_true", help="file the cases that worked, list the others")
    ap.add_argument("--replace", action="store_true")
    ap.add_argument("--discs", type=Path, help="the folder holding the disc images (default bin/)")
    ap.add_argument("--region", choices=("us", "jp"), help="for a build without a schema to tell it")
    args = ap.parse_args(argv)
    if len(args.paths) < 2:
        ap.error("give BUILD [BUILD ...] OUT_DIR")
    out = args.paths[-1]
    try:
        builds = [b for p in args.paths[:-1] for b in resolve_builds(p, args.region)]
        find_cases(args.case)
        m = make_corpus(builds, out, args.release or out.name, args.case, args.jobs, args.discs, not args.no_verify,
                        args.canonical, {"kind": "builds", "paths": [str(p.absolute()) for p in args.paths[:-1]]},
                        args.keep_going, args.replace)
    except CorpusError as error:
        print(f"state_corpus: {error}", file=sys.stderr)
        return 1
    print(f"state_corpus: {len(m['cases'])} cases of {', '.join(m['configs'])} in {out} "
          f"({m['bytes'] / 1e6:.1f} MB, {m['seconds']} s)" + (f"; {len(m['failed'])} failed" if m["failed"] else ""))
    return 0


def cmd_list(argv: list[str]) -> int:
    for c in all_cases():
        safe = f"{c.safe:#x}" if c.safe > 0xFF else SAFE_NAMES[c.safe]
        region = f"[{c.region}] " if c.region else ""
        env = " ".join(f"{k}={v}" for k, v in c.env)
        print(f"{c.id:12} disc {c.disc}  save at {c.asked:5}  {safe:10}  {env:40}  {region}{c.why}")
    return 0


def cmd_check(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(prog="state_corpus.py check")
    ap.add_argument("corpus", type=Path)
    args = ap.parse_args(argv)
    problems = check_corpus(args.corpus)
    for p in problems:
        print(p)
    if not problems:
        m = read_manifest(args.corpus)
        print(f"state_corpus: {args.corpus}: {len(m['cases'])} cases of {', '.join(m['configs'])}, release "
              f"{m['release']}{' (canonical)' if m['canonical'] else ''}: intact")
    return 1 if problems else 0


def other_build(tag: str, spec: str, cache: ReleaseCache) -> tuple[list[Build], list[tuple[str, Path | bytes]]]:
    """One --build of `release`: executables of the release that are no asset, and what its corpus keeps of them.

    FILE.tar.gz: the tag's Linux tarball of another region (packaging/build_release.py --write-linux), its
    FILE.sha256 beside it and matching; unpacked in the cache by its digest; both engines; kept as
    executables/<region>/FILE and FILE.sha256. [CONFIG=]DIR: a build folder, an install or an unpacked tarball
    (CONFIG: only that configuration of it); each executable and its schema kept as executables/<config>/bin/ and
    share/xenogears-pc/, as an install lays them out. Every one must write portable slots."""
    config, eq, path = spec.partition("=")
    if not eq:
        config, path = "", spec
    given = Path(path)
    files: list[tuple[str, Path | bytes]] = []
    if given.name.endswith(".tar.gz"):
        if config:
            raise CorpusError(f"--build {spec}: a tarball holds both engines of its region: give it alone")
        if given.name != tarball_name(tag):
            raise CorpusError(f"--build {spec}: not {tag}'s Linux tarball ({tarball_name(tag)})")
        side = given.with_name(given.name + ".sha256")
        if not given.is_file() or not side.is_file():
            raise CorpusError(f"--build {spec}: no such tarball, or no {side.name} beside it")
        digest = sha256(given)
        if side.read_text() != f"{digest}  {given.name}\n":
            raise CorpusError(f"--build {spec}: it does not match its {side.name}")
        folder = cache.unpack(tag, given, digest)
        more = resolve_builds(folder)
        regions = sorted({b.region for b in more})
        if len(regions) != 1:
            raise CorpusError(f"--build {spec}: engines of {' and '.join(regions)} in one tarball")
        kept = f"{KEPT}/{regions[0]}/{given.name}"
        for b in more:
            b.source = {"executable": {"kind": "tarball", "tag": tag, "sha256": digest, "kept": kept,
                                       "path": str(b.exe.relative_to(folder))}}
        files += [(kept, given), (kept + ".sha256", side)]
    else:
        more = resolve_builds(given)
        if config:
            more = [b for b in more if b.config == config]
            if not more:
                raise CorpusError(f"--build {spec}: that folder holds {', '.join(b.config for b in resolve_builds(given))}")
        for b in more:
            if b.schema is None:
                raise CorpusError(f"--build {spec}: {b.exe} has no state schema beside it")
            data = b.schema.read_bytes()
            packed = sc.schema_file(data) if b.schema.name.endswith(".json") else data
            rel = f"{KEPT}/{b.config}"
            exe, schema = f"{rel}/bin/{b.exe.name}", f"{rel}/share/{PACKAGE}/{b.exe.name}.xgschema"
            b.source = {"executable": {"kind": "kept", "path": exe, "sha256": sha256(b.exe),
                                       "schema": {"path": schema, "sha256": hashlib.sha256(packed).hexdigest()}}}
            files += [(exe, b.exe), (schema, packed)]
    for b in more:
        if portable_off(b.exe) or b.schema is None:
            raise CorpusError(f"--build {spec}: {b.exe.name} ({b.config}) writes no portable slots")
    return more, files


def keep_in_corpus(corpus: Path, keep: list[tuple[str, Path | bytes]], manifest: dict) -> None:
    """The --build files into the corpus, checked against the manifest's records once there."""
    for rel, source in keep:
        dest = corpus / rel
        dest.parent.mkdir(parents=True, exist_ok=True)
        if isinstance(source, bytes):
            dest.write_bytes(source)
        else:
            shutil.copy2(source, dest)
    for config, info in manifest["configs"].items():
        for rel, digest in kept_files(info.get("executable")):
            if not (corpus / rel).is_file() or sha256(corpus / rel) != digest:
                raise CorpusError(f"{config}: {rel} is not what was given")
    manifest["bytes"] = sum(p.stat().st_size for p in corpus.rglob("*") if p.is_file())


def cmd_release(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(prog="state_corpus.py release",
                                 description="A release's corpus into CORPORA/TAG, from its Linux tarball; "
                                             "nothing for a release without portable slots.")
    ap.add_argument("tag")
    ap.add_argument("--tarball", type=Path, help="the release's Linux tarball (FILE.sha256 beside it), as the release "
                    "run made it; default: the published one, downloaded into the cache")
    ap.add_argument("--build", action="append", default=[], metavar="FILE.tar.gz|[CONFIG=]DIR",
                    help="more executables of the same release that are no asset, kept in its corpus: the tag's "
                         "Linux tarball of another region (FILE.sha256 beside it; the release run's JP one), or a "
                         "folder (CONFIG=DIR: one configuration of it)")
    ap.add_argument("--corpora", type=Path, default=CORPORA)
    ap.add_argument("--cache", type=Path, default=CACHE)
    ap.add_argument("--assets-from", type=Path, help="a local stand-in of the GitHub release assets: DIR/<tag>/<asset>")
    ap.add_argument("--gate", action="store_true", help="file it only if state_compat.py passes both ways against "
                    "every earlier canonical release (exit 1 otherwise; the corpus goes to CORPORA/.rejected)")
    ap.add_argument("--case", action="append", default=[])
    ap.add_argument("--jobs", type=int, default=default_jobs())
    ap.add_argument("--discs", type=Path)
    ap.add_argument("--replace", action="store_true")
    ap.add_argument("--strict", action="store_true", help="fail when a case does not repeat in its own build "
                    "(default: left out of the corpus and listed in its manifest's failed)")
    args = ap.parse_args(argv)
    try:
        cache = ReleaseCache(args.cache, assets_from=args.assets_from)
        folder = cache.unpacked(args.tag, local=args.tarball)
        digest = json.loads((args.cache / args.tag / "verified.json").read_text())["sha256"]
        builds = resolve_builds(folder, region_default="us")  # (a release's Linux tarball is US only)
        for b in builds:
            b.source = {"executable": {"kind": "tarball", "tag": args.tag, "sha256": digest,
                                       "path": str(b.exe.relative_to(folder))}}
        off = [b for b in builds if portable_off(b.exe) or b.schema is None]
        if off:
            print(f"state_corpus: {args.tag} is not a canonical release: {', '.join(b.exe.name for b in off)} "
                  f"write{'s' if len(off) == 1 else ''} no portable slots (XG_PORTABLE_SLOTS off, or no state schema): "
                  "no corpus written")
            return 0
        keep: list[tuple[str, Path | bytes]] = []  # what the corpus keeps of --build: (path in it, file or bytes)
        for spec in args.build:
            more, files = other_build(args.tag, spec, cache)
            twice = sorted({b.config for b in more} & {b.config for b in builds})
            if twice:
                raise CorpusError(f"--build {spec}: two builds of one configuration: {', '.join(twice)}")
            builds += more
            keep += files
        dest = args.corpora / args.tag
        if dest.exists() and not args.replace:
            raise CorpusError(f"{dest} exists (--replace makes it again)")
        staged = args.corpora / f".{args.tag}.made"
        shutil.rmtree(staged, ignore_errors=True)
        find_cases(args.case)
        m = make_corpus(builds, staged, args.tag, args.case, args.jobs, args.discs, True, False,
                        {"kind": "release", "tag": args.tag, "tarball": tarball_name(args.tag), "sha256": digest},
                        keep_going=not args.strict)
        keep_in_corpus(staged, keep, m)
        gate = None
        if args.gate:
            import state_compat
            gate = state_compat.run(args.corpora, new_corpus=staged, new_builds={b.config: b for b in builds},
                                    new_release=args.tag, cache=cache, jobs=args.jobs, discs=args.discs,
                                    work=args.cache / args.tag / "gate")
            m["gate"] = gate.summary()
            print(gate.line())
            if not gate.passed:
                m["canonical"] = False
                write_json(staged / "manifest.json", m)
                rejected = args.corpora / ".rejected" / f"{args.tag}-{int(time.time())}"
                rejected.parent.mkdir(parents=True, exist_ok=True)
                staged.rename(rejected)
                print(f"state_corpus: {args.tag} FAILS state-compat: corpus kept in {rejected}, not filed")
                return 1
        m["canonical"] = True
        write_json(staged / "manifest.json", m)
        if dest.exists():
            shutil.rmtree(dest)
        staged.rename(dest)
    except CorpusError as error:
        print(f"state_corpus: {error}", file=sys.stderr)
        return 1
    for n in cache.notes:
        print(f"state_corpus: note: {n}")
    for x in m["failed"]:
        print(f"state_corpus: left out: {x['why']}")
    print(f"state_corpus: {args.tag}: canonical; {len(m['cases'])} cases of {', '.join(m['configs'])} in {dest} "
          f"({m['bytes'] / 1e6:.1f} MB, {m['seconds']} s)" + (f"; {len(m['failed'])} left out" if m["failed"] else "")
          + (f"; gate: {m['gate']['line']}" if gate else ""))
    return 0


def main(argv: list[str] | None = None) -> int:
    argv = sys.argv[1:] if argv is None else argv
    commands = {"list": cmd_list, "check": cmd_check, "release": cmd_release}
    if argv and argv[0] in commands:
        return commands[argv[0]](argv[1:])
    return cmd_make(argv)


if __name__ == "__main__":
    sys.exit(main())
