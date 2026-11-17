#!/usr/bin/env python3
"""The gallery's movie captions on the player's discs (tools/port_tests.py gallery-subtitles).

    gallery_subtitles.py <xenogears> <scratch> <US 1.cue> <US 2.cue> <JP 1.cue> <JP 2.cue>

Runs the game's extras (XG_EXTRAS_SCRIPT) on each release's discs: the gallery
scans them, its player plays and holds movies, and `do caption` reports what
the movie pane draws, the frame shown and the movie the player's world goes by
(cd/regional.c, told by the game's video player as in the game). Each report
is checked against the disc image, read here:

- identity: the game finds movie n as entry dirs[0x19] + n of the file table
  (regionalMovieBegin); that entry's first sector is the gallery's stream n
  and the player's clip n, on the disc the table says, of the region run;
- frame: the frame shown is the player's caption frame, and the STR header
  of a video sector of that clip carries that number (where is reported);
- captions: both tracks and the one drawn are the caption table's at that
  region, disc, movie and frame (cue ranges half open; JP Disc 2's movie 6
  reel goes by movie 0, and by movie 8 from frame 3601). The table is read
  from port/src/cd/subtitles.inc when the build has it; nothing of it is kept
  here. Without it every caption must be none, and the summary says so.

It also runs the viewer through Off and both tracks on a held frame, pause,
resume, seek, stop, late frames and restart, and checks settings.ini's
subtitle and audio keys are as they were.
"""
import codecs
import os
import re
import struct
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[4]
SECTOR = 2352
checks = 0
failures: list[str] = []


def check(ok: bool, what: str) -> bool:
    global checks
    checks += 1
    if not ok and len(failures) < 40:
        failures.append(what)
    return ok


class Image:
    """A disc image's raw sectors, its file table and directory table."""

    def __init__(self, cue: Path):
        self.f = open(cue.with_suffix(".bin"), "rb")
        self.table = b"".join(self.sector(24 + i)[24:24 + 2048] for i in range(16))
        self.dirs = struct.unpack("<61H", self.sector(40)[24:24 + 122])
        self.number = self.dirs[60]

    def sector(self, lba: int) -> bytes:
        self.f.seek(lba * SECTOR)
        return self.f.read(SECTOR)

    def entry(self, i: int) -> tuple[int, int]:
        e = self.table[7 * i:7 * i + 7]
        return e[0] | e[1] << 8 | e[2] << 16, struct.unpack("<i", e[3:7])[0]

    def clip(self, n: int) -> tuple[int, int] | None:
        """Movie n as the game finds it: (first sector, sectors), None past the directory's last."""
        first = self.dirs[0x19] - 1
        if n < 0 or n >= -self.entry(first)[1]:
            return None
        lba, size = self.entry(first + 1 + n)
        return lba, size // 2336

    def frame_sector(self, lba: int, sectors: int, frame: int) -> int | None:
        """The sector of the clip whose STR header starts `frame`, searched outward from 10 a frame."""
        guess = (frame - 1) * 10
        for d in range(600):
            for o in {guess - d, guess + d}:
                if not 0 <= o < sectors:
                    continue
                raw = self.sector(lba + o)
                if raw[15] != 2 or raw[18] & 4 or raw[24:28] != b"\x60\x01\x01\x80":
                    continue
                chunk, _, number = struct.unpack("<HHI", raw[28:36])
                if chunk == 0 and number == frame:
                    return o
        return None


def caption_tables() -> dict[int, list[tuple[int, int, int, int, str]]] | None:
    """Track 1 (US dialogue) and 2 (Japanese translation) of the build's caption tables, None
    without them (the other tables, sounds and lyrics, are off in this test's settings)."""
    path = ROOT / "port/src/cd/subtitles.inc"
    if not path.exists():
        return None
    text = path.read_text()
    cue = re.compile(r'\{\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*,\s*"((?:[^"\\]|\\.)*)"\s*\}')
    def table(name: str) -> str:
        start = text.index(f"static const Caption {name}[]")
        return text[start:text.index("};", start)]
    tracks = {}
    for track, part in ((1, table("captionsUS")), (2, table("captionsJapanese"))):
        tracks[track] = [(int(d), int(c), int(s), int(e), codecs.decode(t, "unicode_escape"))
                         for d, c, s, e, t in cue.findall(part)]
        if len(tracks[track]) != len(re.findall(r"^\s*\{\s*\d", part, re.M)) or not tracks[track]:
            sys.exit(f"gallery subtitles: cannot read every cue of {path}")
    return tracks


def lookup(tables, track: int, japanese: bool, disc: int, clip: int, frame: int) -> str | None:
    if tables is None or track not in (1, 2):
        return None
    if japanese and disc == 2 and clip == 6:
        clip, frame = (8, frame - 3600) if frame >= 3601 else (0, frame)
    for d, c, start, end, text in tables[track]:
        if d == disc and c == clip and start <= frame < end:
            return text
    return None


def run(exe: Path, cue: Path, script: Path, settings: Path) -> str:
    env = dict(os.environ, XG_SETTINGS=str(settings), XG_EXTRAS_SCRIPT=str(script), XG_CLOCK="step",
               SDL_JOYSTICK_HIDAPI="0", SDL_GAMECONTROLLER_IGNORE_DEVICES_EXCEPT="0x0000/0x0000")
    for key, value in (("XG_HEADLESS", "1"), ("SDL_VIDEO_DRIVER", "offscreen"), ("SDL_AUDIO_DRIVER", "dummy")):
        env.setdefault(key, value)
    p = subprocess.run([str(exe), "--extras", str(cue)], env=env, capture_output=True, text=True, errors="replace")
    out = p.stdout + p.stderr
    if p.returncode != 0:
        print(out[-3000:])
        sys.exit(f"gallery subtitles: {exe.name} --extras {cue.name}: exit {p.returncode}")
    return out


def guarded_keys(settings: Path) -> dict[str, str]:
    pairs = (line.split("=", 1) for line in settings.read_text().splitlines() if "=" in line and not line.startswith("#"))
    return {k: v for k, v in pairs if re.search(r"subtitle|cutscene|japanese|regional|voice", k)}


CAPTION = re.compile(
    r"gallery: caption movie (-?\d+) disc (\d+) stream (-?\d+) lba (-?\d+) frame (\d+) playing (\d) paused (\d) track (\d) "
    r"player (\d) japanese (\d) disc (\d+) clip (\d+) lba (\d+) frame (\d+) us (none|\".*\") jp (none|\".*\") drawn (none|\".*\")$",
    re.M)


def region(exe: Path, scratch: Path, name: str, japanese: bool, cues: list[Path], tables) -> str:
    images = {}
    for cue in cues:
        image = Image(cue)
        images[image.number] = image
    check(sorted(images) == [1, 2], f"{name}: the images are discs 1 and 2")
    settings = scratch / f"settings-{name}.ini"
    settings.write_text("cutscene_subtitles=2\n")
    before = guarded_keys(settings)

    # The gallery's own list: the movies its scan found and where the game plays them.
    script = scratch / f"list-{name}.txt"
    script.write_text("2 open visuals\n4 do section gallery\n6 do wait\n8 do list\n10 leave\n")
    listing = run(exe, cues[0], script, settings)
    movies = {}
    for m in re.finditer(r"gallery: movie (\d+): disc (\d+) stream (\d+) lba (\d+) offset (\d+) sectors (\d+) frames (\d+)", listing):
        i, disc, stream, lba, offset, sectors, frames = map(int, m.groups())
        movies[i] = dict(disc=disc, stream=stream, lba=lba, offset=offset, sectors=sectors, frames=frames)
    uses = [tuple(map(int, m.groups())) for m in re.finditer(
        r"gallery: use \d+: disc (\d+) movie (\d+) stream \d+ offset (\d+) frames (\d+)-(\d+) field (-?\d+) op \w+ mode \d+ event -?\d+ pc (\d+)",
        listing)]
    by_clip = {(m["disc"], m["stream"]): i for i, m in movies.items() if m["offset"] == 0}
    check(len(movies) > 0 and {m["disc"] for m in movies.values()} == {1, 2}, f"{name}: the scan lists movies of both discs")
    split = sorted((m["disc"], m["stream"], m["offset"]) for m in movies.values() if m["offset"])

    # The frames to hold: around each track's first cue of every captioned movie, and the reels.
    def cue_frames(disc: int, clip: int, shift: int = 0) -> set[int]:
        frames = set()
        for track in (1, 2):
            first = next((c for c in (tables or {}).get(track, []) if c[0] == disc and c[1] == clip), None)
            if first:
                frames |= {first[2] - 1 + shift, first[2] + shift, first[3] - 1 + shift, first[3] + shift}
        return frames

    wanted: dict[int, set[int]] = {}
    for (disc, stream), i in by_clip.items():
        frames = cue_frames(disc, stream)
        if disc == 2 and stream == 6:  # the reels: JP's goes by movies 0 and 8, the US one is its own
            frames |= cue_frames(2, 0) | cue_frames(2, 8, 3600) | {3600, 3601}
        if disc == 2 and stream == 0:  # the credits share the reel's sound, not its pictures
            frames |= cue_frames(2, 8, 3600)
        frames = {f for f in frames if 1 <= f <= movies[i]["frames"]}
        if frames:
            wanted[i] = frames
    if tables is None:  # no cues to go by: a start inside each disc's first movie all the same
        wanted = {by_clip[(d, s)]: {20} for d, s in ((1, 2), (2, 4)) if (d, s) in by_clip}
    check(len(wanted) > 0, f"{name}: movies to hold")

    # The viewer's states on one movie: the first held movie of disc 1.
    life = min(i for i in wanted if movies[i]["disc"] == 1)
    both = [f for f in sorted(wanted[life]) if f >= 2 and all(
        lookup(tables, k, japanese, 1, movies[life]["stream"], f) for k in (1, 2))]
    held = both[0] if both else min(f for f in wanted[life] if f >= 2)  # a frame both tracks caption, if any
    lines, expect, t = ["2 open visuals", "4 do section gallery", "6 do wait"], [], 8

    def step(command: str, label: str | None = None, wait: int = 0, **facts) -> None:
        nonlocal t
        t += wait
        lines.append(f"{t} do {command}")
        if label is not None:
            lines.append(f"{t} do caption")
            expect.append(dict(facts, label=label))

    step("tab movies", "before", playing=0, known=0, track=2)
    step(f"movie {life}")
    step("tab movies", "playing", wait=60, movie=life, after=0, playing=1, paused=0, known=1, track=2)
    step(f"hold {held}", "held", movie=life, frame=held, paused=1, known=1, track=2)
    step("subtitles 1", "us", movie=life, frame=held, paused=1, known=1, track=1)
    step("subtitles 0", "off", movie=life, frame=held, paused=1, known=1, track=0)
    step("subtitles 2", "jp", movie=life, frame=held, paused=1, known=1, track=2)
    step("tab movies", "paused-later", wait=12, movie=life, frame=held, paused=1, known=1, track=2)
    step("pause")
    step("pause", "resumed", wait=16, movie=life, after=held, paused=1, known=1, track=2)
    step("tab movies", "repaused", wait=12, movie=life, same=1, paused=1, known=1, track=2)
    step(f"seek {held + 30}", "seek", movie=life, frame=0, playing=1, known=0, track=2)
    step("tab movies", "after-seek", wait=40, movie=life, after=held + 29, playing=1, known=1, track=2)
    step("stop", "stopped", playing=0, known=0, track=2)
    step("tab movies", "late", wait=20, playing=0, known=0, same=1, track=2)
    step(f"movie {life}", "restart", movie=life, frame=0, playing=1, known=0, track=2)
    step("tab movies", "restarted", wait=40, movie=life, after=0, playing=1, known=1, track=2)
    step("stop", "stopped-again", playing=0, known=0, track=2)
    step("subtitles 1")
    for i in sorted(wanted):
        step(f"movie {i}")
        for f in sorted(wanted[i]):
            step(f"hold {f}", f"movie {i} frame {f}", movie=i, frame=f, paused=1, known=1, track=1)
    # A place the game starts a movie inside its stream (a field script's sector offset).
    inside = next((u for u in uses if u[2] > 0 and u[5] >= 0 and u[0] == 1), None)
    if inside is not None:
        step(f"media {inside[5]} {inside[6]}")
        step("tab movies", f"field {inside[5]} pc {inside[6]}", wait=max(40, min(200, 4 * (inside[4] - inside[3]))),
             movie=inside[1], after=inside[3] - 1,
             upto=inside[4], known=1, track=1)
    step("stop")
    lines.append(f"{t + 4} leave")
    script = scratch / f"script-{name}.txt"
    script.write_text("\n".join(lines) + "\n")
    out = run(exe, cues[0], script, settings)
    (scratch / f"log-{name}.txt").write_text(out)

    refused = re.findall(r"extras: script: .*", out)
    check(not refused, f"{name}: every script command taken: {refused[:3]}")
    reports = [m.groups() for m in CAPTION.finditer(out)]
    if not check(len(reports) == len(expect), f"{name}: {len(reports)} caption reports for {len(expect)} asked"):
        return f"{name}: failed"

    def text(s: str) -> str | None:
        return None if s == "none" else s[1:-1]

    known = shown = inside_stream = 0
    worst = 0
    previous = None
    for e, r in zip(expect, reports):
        label = f"{name} {e['label']}"
        movie, disc, stream, lba, frame, playing, paused, track, ident, jp, pdisc, clip, plba, pframe = map(int, r[:14])
        us, jpn, drawn = text(r[14]), text(r[15]), text(r[16])
        check(track == e["track"], f"{label}: track {track}")
        check(ident == e["known"], f"{label}: the player's movie known {ident}")
        for key, got in (("movie", movie), ("frame", frame), ("playing", playing), ("paused", paused)):
            if key in e:
                check(got == e[key], f"{label}: {key} {got}, not {e[key]}")
        if "after" in e:
            check(frame > e["after"] and frame <= e.get("upto", frame), f"{label}: frame {frame}")
        if e.get("same"):
            check(r == previous, f"{label}: as the report before")
        previous = r
        if not ident:
            check(us is None and jpn is None and drawn is None, f"{label}: no caption without a movie")
            continue
        known += 1
        m = movies.get(movie)
        if not check(m is not None and (m["disc"], m["stream"], m["lba"]) == (disc, stream, lba), f"{label}: the list's movie"):
            continue
        image = images[disc]
        found = image.clip(stream)
        check(found is not None and found[0] == lba == plba, f"{label}: movie {stream}'s table entry {found}, stream {lba}, player {plba}")
        check((pdisc, clip, bool(jp)) == (disc, stream, japanese), f"{label}: player disc {pdisc} clip {clip} japanese {jp}")
        check(pframe == frame and frame >= 1, f"{label}: frame shown {frame}, caption frame {pframe}")
        at = image.frame_sector(found[0], found[1], frame) if found else None
        if check(at is not None, f"{label}: frame {frame}'s STR header in the clip"):
            inside_stream += at > 0
            worst = max(worst, abs(at - (frame - 1) * 10))
        want = {k: lookup(tables, k, japanese, disc, stream, frame) for k in (0, 1, 2)}
        check(us == want[1], f"{label}: US dialogue caption")
        check(jpn == want[2], f"{label}: Japanese translation caption")
        check(drawn == want[track], f"{label}: the caption drawn (track {track})")
        shown += drawn is not None
    check(guarded_keys(settings) == before, f"{name}: settings.ini's subtitle and audio keys unchanged")
    reel = by_clip.get((2, 6))
    return (f"{name}: {len(movies)} movies ({'none' if not split else split} starting inside a stream), "
            f"{len(wanted)} held over {sum(map(len, wanted.values()))} frames"
            f"{', reel disc 2 movie 6 at ' + str(sorted(wanted[reel])) if reel in wanted else ''}; "
            f"{known} reports with the player's movie, {inside_stream} of them started inside the stream "
            f"(frame n within {worst} sectors of 10(n-1)), {shown} with a caption drawn; "
            f"{'field use ' + str(inside) if inside else 'no field use inside a stream on disc 1'}")


def main() -> None:
    if len(sys.argv) != 7:
        sys.exit(__doc__)
    exe, scratch = Path(sys.argv[1]), Path(sys.argv[2])
    cues = [Path(a) for a in sys.argv[3:7]]
    scratch.mkdir(parents=True, exist_ok=True)
    tables = caption_tables()
    summary = [region(exe, scratch, "US", False, cues[0:2], tables), region(exe, scratch, "JP", True, cues[2:4], tables)]
    for line in summary:
        print(line)
    for f in failures:
        print("FAIL:", f)
    table = "the build's caption table" if tables else "no caption table in this build: every caption none"
    print(f"gallery subtitles: {checks} checks, {len(failures)} failed, real discs US 1+2 and JP 1+2 ({table})")
    sys.exit(1 if failures else 0)


if __name__ == "__main__":
    main()
