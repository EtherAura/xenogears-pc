#!/usr/bin/env python3
"""Check that the PC port's executable carries no retail module data.

    python tools/payloadcheck.py [--build build/pc/release]
    python tools/payloadcheck.py --exe OLD/xenogears [--build DIR] [--ver us]
    python tools/payloadcheck.py --build DIR --measure   # also list src/'s own data by file

The game modules' data comes from the player's disc at startup
(port/src/system/gamedata.c). This proves it: every run of at least --min (16)
bytes of a retail module image (disc/<ver>/main.exe and *.bin, the images as
the game loads them, from tools/extract.py) that also appears in the linked
executable is found and attributed to the build object it comes from (by the
executable's symbol at that place, else by the objects that hold its bytes):

  generated   tools/pc_link.py's module data (gen/<module>.c): payload, fails
  src         game C in src/ that holds its own initialised data (decompiled
              tables and strings as C initialisers): the module link zeroes
              those bytes and the disc fills them (tools/pc_srcdata.py), so
              any left fail, unless tools/payload_allow.txt allows them
  port        the port's own code (port/src, third_party): fails unless
              tools/payload_allow.txt allows it, with a reason and a byte budget
  other       bytes no object explains: fails

A 16-byte window counts only when it carries information: at least 4 distinct
byte values and at least 4 non-zero bytes. Windows of zeros with a few small
numbers, or of a few repeated values, turn up in any executable by
coincidence (ELF headers, symbol and relocation tables, 0/1/-1 fills). Zero
bytes at a run's ends are padding: a run must keep --min bytes without them.

--exe checks another executable (an old build, a Windows .exe) with --build's
objects for attribution. A PE image needs its .map and .payload.json sidecars
(the Windows CMake link creates them); copies need those sidecars too.
--record-map EXE records the executable/map hashes after a Windows link.
PE-only allowance entries have individual budgets and require their exact
source/symbol pattern; they never borrow another symbol's budget.
Prints one summary line starting with 'payload';
exit status 1 when anything fails, 2 on a usage error.
"""

from __future__ import annotations

import argparse
import bisect
import fnmatch
import re
import subprocess
import sys
from pathlib import Path
import payload_pe

ROOT = Path(__file__).resolve().parent.parent
ALLOW = ROOT / "tools" / "payload_allow.txt"


def build_version(build: Path) -> str | None:
    cache = build / "CMakeCache.txt"
    if cache.exists():
        m = re.search(r"^XG_VERSION:\w+=(\w+)", cache.read_text(errors="replace"), re.M)
        if m:
            return m.group(1).lower()
    return None


def retail_images(ver: str) -> dict[str, bytes]:
    """module -> its image as loaded (config/<ver>/disc.yaml's names, extracted under disc/<ver>/)."""
    out = {}
    for m in re.finditer(r"\{name:\s*(\w+),([^}]*)\}", (ROOT / "config" / ver / "disc.yaml").read_text()):
        name = m.group(1)
        path = ROOT / "disc" / ver / (f"{name}.exe" if "iso:" in m.group(2) else f"{name}.bin")
        if path.exists():
            out[name] = path.read_bytes()
    return out


def informative(w: bytes) -> bool:
    return len(set(w)) >= 4 and len(w) - w.count(0) >= 4


def windows(images: dict[str, bytes], n: int) -> set[bytes]:
    s: set[bytes] = set()
    for data in images.values():
        mv = memoryview(data)
        for i in range(len(data) - n + 1):
            w = bytes(mv[i:i + n])
            if w not in s and informative(w):
                s.add(w)
    return s


def hits(data: bytes, wanted: set[bytes], n: int) -> list[int]:
    """Offsets of `data` where a wanted window starts."""
    mv = memoryview(data)
    return [i for i in range(len(data) - n + 1) if bytes(mv[i:i + n]) in wanted]


def runs(offsets: list[int], n: int) -> list[tuple[int, int]]:
    """Window starts merged into [start, end) byte runs (overlapping or adjacent windows join)."""
    out: list[tuple[int, int]] = []
    for o in offsets:
        if out and o <= out[-1][1]:
            out[-1] = (out[-1][0], max(out[-1][1], o + n))
        else:
            out.append((o, o + n))
    return out


def objects(build: Path) -> list[tuple[str, str, Path, str]]:
    """(category, source, object, CMake target) for every object linked into the executable.

    CMake keeps an object at <target>.dir/<source path>.o: absolute for game C
    outside port/ (src/...), relative to port/ otherwise (src/..., third_party/...),
    and gen/<module>.c for the generated files."""
    out = []
    root = str(ROOT).lstrip("/") + "/"
    for d in sorted((build / "CMakeFiles").glob("*.dir")):
        target = d.name[:-4]
        if not (target.startswith(("gen_", "game_")) or target in ("platform", "ui")):
            continue  # tests and tools: not in the executable
        for o in sorted(list(d.rglob("*.o")) + list(d.rglob("*.obj"))):
            src = re.sub(r"\.(o|obj)$", "", str(o.relative_to(d)))
            if target.startswith("gen_") or target == "game_generated":
                cat = "generated"
            elif src.startswith(root):
                src = src[len(root):]
                cat = "src" if src.startswith("src/") else "port"
            else:
                src = src if src.startswith("gen/") else "port/" + src
                cat = "port"
            out.append((cat, src, o, target))
    return out


def tool(name: str, exe: Path) -> str:
    """binutils for the executable's format (a MinGW one for a PE file when there is one)."""
    if exe.read_bytes()[:2] == b"MZ":
        for prefix in ("x86_64-w64-mingw32-",):
            if subprocess.run(["which", prefix + name], capture_output=True).returncode == 0:
                return prefix + name
    return name


def sections(exe: Path) -> list[tuple[int, int, int, str, bool]]:
    """(file offset, size, address, name, loaded) of the executable's sections with contents in the file."""
    out = []
    text = subprocess.run([tool("objdump", exe), "-h", str(exe)], capture_output=True, text=True).stdout
    for m in re.finditer(r"^\s*\d+\s+(\S+)\s+([0-9a-f]+)\s+([0-9a-f]+)\s+[0-9a-f]+\s+([0-9a-f]+)[^\n]*\n\s*([^\n]*)",
                         text, re.M):
        flags = m.group(5)
        if "CONTENTS" in flags:
            out.append((int(m.group(4), 16), int(m.group(2), 16), int(m.group(3), 16), m.group(1), "ALLOC" in flags))
    return out


def symbols(path: Path, exe: Path) -> list[tuple[int, int, str]]:
    """(address, size, name) of the sized symbols of an object or executable."""
    out = []
    p = subprocess.run([tool("nm", exe), "-S", "--defined-only", str(path)], capture_output=True, text=True)
    for line in p.stdout.splitlines():
        parts = line.split()
        if len(parts) == 4 and int(parts[1], 16) > 0:
            out.append((int(parts[0], 16), int(parts[1], 16), parts[3]))
    return sorted(out)


def load_allow(pe: bool = False) -> list[tuple[str, int, str, bool]]:
    """(pattern, budget, reason, strict PE-only rule) entries of payload_allow.txt."""
    out = []
    if ALLOW.exists():
        for line in ALLOW.read_text().splitlines():
            body, _, why = line.partition("#")
            parts = body.split()
            strict = len(parts) == 3 and parts[0] == "pe"
            if strict:
                parts = parts[1:]
            if len(parts) == 2 and why.strip() and (not strict or pe):
                out.append((parts[0], int(parts[1], 0), why.strip(), strict))
    return out


def audit() -> int:
    if len(sys.argv) == 3 and sys.argv[1] == "--record-map":
        payload_pe.record(Path(sys.argv[2]))
        return 0
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--build", default="build/pc/release", help="configured CMake build directory")
    ap.add_argument("--exe", help="the executable to check (default: the build's)")
    ap.add_argument("--ver", choices=["us", "jp"], help="the release (default: the build's)")
    ap.add_argument("--min", type=int, default=16, help="shortest run (bytes)")
    ap.add_argument("--measure", action="store_true", help="list every source with runs, src/ included")
    ap.add_argument("-v", "--verbose", action="store_true", help="list every run")
    a = ap.parse_args()

    build = Path(a.build) if Path(a.build).is_absolute() else ROOT / a.build
    ver = a.ver or build_version(build)
    if ver is None:
        print(f"payload: no version: {build} is not a configured build and --ver is missing")
        return 2
    exe = Path(a.exe) if a.exe else next((p for p in (build / "xenogears", build / "xenogears.exe") if p.exists()), None)
    if exe is not None and not a.exe:
        # A shared-library build (XG_SHARED): the executable is a launcher, the game is the library.
        lib = exe.parent / "libxenogears.so"
        needed = subprocess.run(["objdump", "-p", str(exe)], capture_output=True, text=True).stdout if lib.exists() else ""
        if re.search(r"^\s*NEEDED\s+libxenogears\.so\s*$", needed, re.M):
            exe = lib
    if exe is None or not exe.exists():
        print(f"payload: no executable in {build}")
        return 2
    images = retail_images(ver)
    if not images:
        print(f"payload: no extracted module images under disc/{ver} (run tools/extract.py)")
        return 2
    n = a.min
    wanted = windows(images, n)
    data = exe.read_bytes()
    pe = data[:2] == b"MZ"
    try:
        mapping = payload_pe.verified_map(exe) if pe else None
    except ValueError as e:
        print(f"payload: {e}")
        return 2
    found = []
    for start, end in runs(hits(data, wanted, n), n):
        # Zero bytes at either end are padding; what is left must still be a whole window.
        while start < end and data[start] == 0:
            start += 1
        while end > start and data[end - 1] == 0:
            end -= 1
        if end - start >= n:
            found.append((start, end))

    # The build's objects: their symbols (to name what the executable's symbol at a run is) and
    # the retail windows each holds (for bytes outside any symbol: merged strings, constants).
    objs = objects(build) if (build / "CMakeFiles").is_dir() else []
    defined: dict[tuple[str, int], list[tuple[str, str]]] = {}
    holder: dict[bytes, list[tuple[str, str]]] = {}
    # A game module's objects bring into the executable what its linked object (modules/<m>.o)
    # still holds: the module link zeroes their retail bytes (tools/pc_srcdata.py), so only the
    # windows that object holds count for them.
    linked: dict[str, set[bytes] | None] = {}
    for cat, src, o, target in objs:
        for _, size, name in symbols(o, exe):
            defined.setdefault((name, size), []).append((cat, src))
        keep = None
        if target.startswith("game_"):
            module = target[5:]
            if module not in linked:
                mo = build / "modules" / f"{module}.o"
                if mo.exists():
                    blob = mo.read_bytes()
                    linked[module] = {blob[i:i + n] for i in hits(blob, wanted, n)}
                else:
                    linked[module] = None
            keep = linked[module]
        blob = o.read_bytes()
        for i in hits(blob, wanted, n):
            w = blob[i:i + n]
            if keep is None or w in keep:
                holder.setdefault(w, []).append((cat, src))
    secs = sections(exe)
    origins = payload_pe.Origins(build, mapping, objs) if mapping is not None else None
    syms = symbols(exe, exe)
    starts = [s[0] for s in syms]
    longest = max((s[1] for s in syms), default=0)

    def symbol_over(start: int, end: int) -> tuple[str, int] | None:
        """The executable's symbol that covers most of [start, end) (file offsets)."""
        for foff, size, vma, _, _ in secs:
            if foff <= start < foff + size:
                lo, hi = vma + start - foff, vma + min(end, foff + size) - foff
                best, best_n = None, 0
                i = bisect.bisect_right(starts, lo) - 1
                while i >= 0 and syms[i][0] > lo - longest:  # symbols that may reach into the run
                    i -= 1
                for addr, sz, name in syms[max(i, 0):bisect.bisect_left(starts, hi)]:
                    n_over = min(hi, addr + sz) - max(lo, addr)
                    if n_over > best_n:
                        best, best_n = (name, sz), n_over
                return best if best_n * 2 >= hi - lo else None
        return None

    rank = {"src": 0, "port": 1, "generated": 2}  # a run several objects hold goes to the first

    def attribute(start: int, end: int) -> dict[tuple[str, str, str], int]:
        """Bytes of an executable run by (category, source, symbol or '-'): the whole run to the
        object that defines the symbol covering most of it; else each byte to the best object
        holding a window over it ('other' when none does)."""
        sec = next((x for x in secs if x[0] <= start < x[0] + x[1]), None)
        if sec is None:
            # Outside every section with contents: the symbol and string tables, which objdump -h
            # leaves out (identifiers of our own source, the SDK's function names).
            return {("names", "symbol and string tables", "-"): end - start}
        if not sec[4]:
            # Not loaded: the symbol table's names (identifiers of our own source; the SDK's
            # function names are debug strings in the retail executable too), notes, comments.
            return {("names", sec[3], "-"): end - start}
        sym = symbol_over(start, end)
        if sym is not None and (sym[0], sym[1]) in defined:
            cat, src = min(defined[(sym[0], sym[1])], key=lambda cs: (rank[cs[0]], cs[1]))
            return {(cat, src, sym[0]): end - start}
        # Bytes outside any symbol (merged strings and constants): each to the object that holds
        # the most of this run's windows among those holding a window over it.
        votes: dict[tuple[str, str], int] = {}
        held = []
        for i in range(end - start - n + 1):
            hs = set(holder.get(data[start + i:start + i + n], ()))
            held.append(hs)
            for h in hs:
                votes[h] = votes.get(h, 0) + 1
        key = lambda cs: (-votes[cs], rank[cs[0]], cs[1])
        best: list[tuple[str, str] | None] = [None] * (end - start)
        for i, hs in enumerate(held):
            if hs:
                h = min(hs, key=key)
                for k in range(i, i + n):
                    if best[k] is None or key(h) < key(best[k]):
                        best[k] = h
        # A stretch shorter than a window that no object holds joins its neighbour: every window
        # over it is mostly a held one (strings that two objects' pools put side by side).
        i = 0
        while i < len(best):
            if best[i] is None:
                j = i
                while j < len(best) and best[j] is None:
                    j += 1
                fill = best[i - 1] if i > 0 else best[j] if j < len(best) else None
                if j - i < n and fill is not None:
                    best[i:j] = [fill] * (j - i)
                i = j
            else:
                i += 1
        out: dict[tuple[str, str, str], int] = {}
        for h in best:
            k = (h[0], h[1], "-") if h else ("other", "no object holds these bytes", "-")
            out[k] = out.get(k, 0) + 1
        return out

    allow = load_allow(pe)
    per: dict[tuple[str, str, str], list[tuple[int, int, int]]] = {}  # (start, end, bytes of the run here)
    for start, end in found:
        sec = next((x for x in secs if x[0] <= start < x[0] + x[1]), None)
        pieces = origins.pieces(sec[2] + start - sec[0], data[start:end]) if origins and sec and sec[4] else \
            [(0, end - start, None)]
        for lo, hi, key in pieces:
            attrs = {key: hi - lo} if key else attribute(start + lo, start + hi)
            for k, nbytes in attrs.items():
                per.setdefault(k, []).append((start + lo, start + hi, nbytes))

    def source_part(pat: str) -> str:
        return pat.split(":", 1)[0]

    def entry_for(src: str, sym: str) -> tuple[str, int, str, bool] | None:
        """The first entry for source:symbol, or, where the symbol is not known (merged strings,
        a PE file's local data), the first entry for the source."""
        for pat, budget, why, strict in allow:
            if fnmatch.fnmatchcase(f"{src}:{sym}", pat) or fnmatch.fnmatchcase(src, pat) or (
                    not strict and sym == "-" and fnmatch.fnmatchcase(src, source_part(pat))):
                return pat, budget, why, strict
        return None

    def budget_for(src: str) -> int:
        """A source's budget: every entry for it (its symbols' and its own)."""
        return sum(b for pat, b, _, strict in allow if not strict and fnmatch.fnmatchcase(src, source_part(pat)))

    used: dict[str, int] = {}  # by source
    strict_used: dict[str, tuple[int, int]] = {}
    totals = {k: [0, 0] for k in ("generated", "src", "allowed", "port", "library", "other", "names")}
    report = []
    for (cat, src, sym), rs in sorted(per.items()):
        nbytes = sum(b for _, _, b in rs)
        kind = cat
        note = ""
        if cat in ("port", "src", "library"):
            e = entry_for(src, sym)
            if e is not None:
                if e[3]:
                    previous = strict_used.get(e[0], (e[1], 0))[1]
                    strict_used[e[0]] = e[1], previous + nbytes
                else:
                    used[src] = used.get(src, 0) + nbytes
                kind, note = "allowed", e[2]
        totals[kind][0] += len(rs)
        totals[kind][1] += nbytes
        report.append((kind, src, sym, len(rs), nbytes, rs, note))
    over = [(src, budget_for(src), got) for src, got in sorted(used.items()) if got > budget_for(src)]
    over += [(pat, budget, got) for pat, (budget, got) in sorted(strict_used.items()) if got > budget]

    print(f"retail module data in {exe} ({ver.upper()}): {len(images)} module images, runs of >= {n} bytes")
    bad_kinds = ("generated", "src", "port", "library", "other")
    for kind, src, sym, count, nbytes, rs, note in report:
        if kind in bad_kinds or a.measure or a.verbose:
            print(f"  {kind:9} {nbytes:7} bytes {count:5} runs  {src}" + (f" ({sym})" if sym != "-" else "")
                  + (f"  [{note}]" if note and a.verbose else ""))
            if a.verbose or kind in bad_kinds:
                for s, e, _ in rs[:3 if not a.verbose else len(rs)]:
                    print(f"            exe+0x{s:X} {e - s} bytes: {data[s:min(e, s + 24)].hex()}")
    for src, budget, got in over:
        print(f"  over budget: {src}: {got} bytes, tools/payload_allow.txt allows {budget}")
    if a.measure:
        by_file: dict[str, int] = {}
        for kind, src, sym, count, nbytes, rs, note in report:
            if kind == "src":
                by_file[src] = by_file.get(src, 0) + nbytes
        print(f"src/ initialised data the executable holds that matches retail: "
              f"{sum(by_file.values())} bytes in {len(by_file)} files")
    bad = sum(totals[k][0] for k in bad_kinds) + len(over)
    print(f"payload {'FAIL' if bad else 'ok'}: generated {totals['generated'][0]} runs/{totals['generated'][1]} bytes, "
          f"port {totals['port'][0]}/{totals['port'][1]}, other {totals['other'][0]}/{totals['other'][1]}; "
          f"library {totals['library'][0]}/{totals['library'][1]}; "
          f"allowed {totals['allowed'][0]}/{totals['allowed'][1]}, symbol names {totals['names'][0]}/{totals['names'][1]}, "
          f"src (game C's own data) {totals['src'][0]}/{totals['src'][1]}")
    return 1 if bad else 0


def main() -> int:
    try:
        return audit()
    except (OSError, ValueError) as e:
        print(f"payload: attribution error: {e}")
        return 2


if __name__ == "__main__":
    sys.exit(main())
