#!/usr/bin/env python3
"""List the game C's possibly uninitialised reads (PC port gate).

An uninitialised local gives the PS1 whatever its register or stack slot held
(a caller's value, a leftover of an earlier call); the PC gets something else,
often host addresses that change with the link layout. Each one is either a
false positive, or reached: then the PC reproduces the PS1's value
(IMPLICIT_RETURN, a PS1-identical initialiser or a `static` under PLATFORM_PC).

This compiles the game C (src/ without the PsyQ copies, plus the PC-only
port/src/game; US and JP, the PLATFORM_PC branches taken) with
  - clang -Wuninitialized -Wsometimes-uninitialized -Wconditional-uninitialized
    (-fsyntax-only: the front end's flow analysis), and
  - gcc -O2 -Wuninitialized -Wmaybe-uninitialized (after optimisation and
    inlining: it sees through some flags clang cannot, and misses others),
and lists one entry per variable: <file> | <function> | <variable>.
Neither sees a struct or array whose address is passed on (an SVECTOR handed
to the GTE, an edge buffer a callee may fill): MemorySanitizer runs of the
test paths cover those (bd memory port-uninit-msan).

Reviewed variables go in tools/uninit_allow.txt, one per line:
    <file> | <function> | <variable> | <one-line reason>
Exit status 1 when a variable is not on the list or a list entry matches no
warning (a fixed one: take it off). gcc's findings depend on its version (and
its inlining); the list records what gcc 16 and clang 22 report.

    python tools/uninit.py              # US + JP: variables not reviewed yet (the gate)
    python tools/uninit.py --all        # every variable, with the list's reasons
    python tools/uninit.py --ver us src/battle/b_80070E2C.c ...   # some files only (no list check)
"""

from __future__ import annotations

import argparse
import collections
import concurrent.futures
import os
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import pc_sources  # noqa: E402

ALLOW = ROOT / "tools" / "uninit_allow.txt"
VERSIONS = ("us", "jp")
# The port's game options (port/CMakeLists.txt XG_GAME_OPTIONS) that change meaning.
COMMON = ["-std=gnu89", "-nostdinc", "-fno-builtin", "-funsigned-char", "-fwrapv", "-fno-strict-aliasing",
          "-D_LANGUAGE_C", "-DPLATFORM_PC", "-Iinclude"]
CLANG = ["-target", "x86_64-linux-gnu", "-fsyntax-only", "-Wno-everything", "-Wuninitialized",
         "-Wsometimes-uninitialized", "-Wconditional-uninitialized", "-fno-caret-diagnostics",
         "-fdiagnostics-show-option", "-ferror-limit=0"]
GCC = ["-O2", "-c", "-o", os.devnull, "-fcommon", "-fno-pie", "-fpermissive", "-Wuninitialized",
       "-Wmaybe-uninitialized", "-fdiagnostics-plain-output", "-fno-diagnostics-show-caret"]
WARN = re.compile(r"^(?P<file>[^:\n]+):(?P<line>\d+):(?P<col>\d+): warning: (?P<msg>.*?) "
                  r"\[-W(?P<flag>(?:maybe-|sometimes-|conditional-)?uninitialized)\]$")
# clang: variable 'x' is/may be uninitialized when used here; gcc: 'x' may be used uninitialized
# gcc: "file: In function 'name':" before a function's warnings (a macro's
# warning is placed in the macro's definition, outside any function)
IN_FUNCTION = re.compile(r"^[^:]+: In function [‘'](\w+)[’']:$")
NAME = re.compile(r"[‘'](?P<name>[^’']+)[’']")


def var_of(msg: str) -> str:
    m = NAME.search(msg)
    name = m.group("name") if m else msg
    m = re.search(r"[A-Za-z_]\w*", name)  # rgb[2] -> rgb, *p -> p
    return m.group(0) if m else name


def run(job: tuple) -> dict:
    """Both compilers over one file: its warnings, and its functions' line ranges."""
    path, ver, cc_clang, cc_gcc = job
    vflag = f"-DVERSION_{ver.upper()}"
    warns, errors = [], []
    for tool, cmd in (("clang", [cc_clang, *CLANG, *COMMON, vflag, path]),
                      ("gcc", [cc_gcc, *GCC, *COMMON, vflag, path])):
        p = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True)
        if p.returncode != 0:
            errors.append(f"{tool}: " + (p.stderr.strip().splitlines() or ["failed"])[-1])
        context = "?"
        for line in p.stderr.splitlines():
            m = IN_FUNCTION.match(line)
            if m:
                context = m.group(1)
                continue
            m = WARN.match(line)
            if m:
                f = os.path.relpath(os.path.join(ROOT, m["file"]), ROOT)
                warns.append((f, int(m["line"]), var_of(m["msg"]), tool, m["flag"], context))
    # Function ranges, from the preprocessed text (the PLATFORM_PC branches).
    p = subprocess.run([cc_clang, "-E", *COMMON, vflag, path], cwd=ROOT, capture_output=True, text=True)
    funcs = function_ranges(p.stdout)
    return {"path": path, "ver": ver, "warns": warns, "funcs": funcs, "errors": errors}


TOKEN = re.compile(r'"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|[{}();]|[^\s{}();"\']+')
MARKER = re.compile(r'^# (\d+) "([^"]*)"')


def function_ranges(text: str) -> dict[str, list[tuple[int, int, str]]]:
    """{file: [(first line, last line, function)]} of every function body."""
    out = collections.defaultdict(list)
    depth, head, segs, cur = 0, [], [], None
    fname, line = "", 0
    decl = re.compile(r"([A-Za-z_]\w*)\s*\((?:[^()]|\([^()]*\))*\)$")
    knr = re.compile(r"([A-Za-z_]\w*)\s*\(\s*[A-Za-z_][\w\s,]*\)\s+\S")  # name(a, b) T a
    for raw in text.splitlines():
        m = MARKER.match(raw)
        if m:
            line, fname = int(m.group(1)), os.path.relpath(os.path.join(ROOT, m.group(2)), ROOT)
            continue
        for tok in TOKEN.findall(raw):
            if tok == "{":
                if depth == 0:
                    # A definition: name ( params ) {, or K&R: name ( a, b ) T a; T b; {
                    m = None
                    if head:
                        m = decl.search(" ".join(head))
                    else:
                        for s in reversed(segs):
                            if "(" in s:
                                m = decl.search(s) or knr.search(s)
                                break
                    cur = [fname, line, m.group(1)] if m else None
                depth += 1
            elif tok == "}":
                depth -= 1
                if depth == 0:
                    if cur and cur[0] == fname:
                        out[fname].append((cur[1], line, cur[2]))
                    cur, head, segs = None, [], []
            elif depth == 0:
                if tok == ";":
                    segs.append(" ".join(head))
                    head = []
                else:
                    head.append(tok)
        line += 1
    return out


def func_at(funcs: dict, f: str, line: int, context: str) -> str:
    for lo, hi, name in funcs.get(f, ()):
        if lo <= line <= hi:
            return name
    return context


def load_allow() -> dict:
    allow = {}
    if ALLOW.exists():
        for n, line in enumerate(ALLOW.read_text().splitlines(), 1):
            if not line.strip() or line.lstrip().startswith("#"):
                continue
            parts = [p.strip() for p in line.split(" | ")]
            if len(parts) != 4 or not parts[-1]:
                print(f"{ALLOW.relative_to(ROOT)}:{n}: expected '<file> | <function> | <variable> | <reason>'")
                allow[("?", str(n), "")] = ""
                continue
            allow[tuple(parts[:3])] = parts[3]
    return allow


def jobs_for(ver: str, files: list[str] | None, cc_clang: str, cc_gcc: str) -> list:
    srcs = []
    for module, paths in pc_sources.modules(ver).items():
        srcs += [str(p.relative_to(ROOT)) for p in paths]
        srcs += sorted(str(p.relative_to(ROOT)) for p in (ROOT / "port/src/game" / module).glob("*.c"))
    if files:
        wanted = {str(Path(f).resolve().relative_to(ROOT)) for f in files}
        srcs = [s for s in srcs if s in wanted]
    return [(s, ver, cc_clang, cc_gcc) for s in srcs]


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--ver", choices=VERSIONS, action="append", help="a version (default: both)")
    ap.add_argument("--all", action="store_true", help="also list reviewed variables")
    ap.add_argument("--clang", default=os.environ.get("CLANG", "clang"))
    ap.add_argument("--gcc", default=os.environ.get("GCC", "gcc"))
    ap.add_argument("files", nargs="*")
    args = ap.parse_args()
    vers = args.ver or list(VERSIONS)

    jobs = [j for ver in vers for j in jobs_for(ver, args.files, args.clang, args.gcc)]
    with concurrent.futures.ThreadPoolExecutor(max_workers=os.cpu_count()) as pool:
        units = list(pool.map(run, jobs))

    errors = [f"{u['path']} ({u['ver'].upper()}): {e}" for u in units for e in u["errors"]]
    found: dict[tuple, dict] = {}
    for u in units:
        for f, line, var, tool, flag, context in u["warns"]:
            key = (f, func_at(u["funcs"], f, line, context), var)
            it = found.setdefault(key, {"lines": set(), "tools": set(), "vers": set()})
            it["lines"].add(line)
            it["tools"].add(tool)
            it["vers"].add(u["ver"])
    for e in errors:
        print(f"error: {e}")

    allow = load_allow()
    new = [k for k in found if k not in allow]
    stale = [k for k in allow if k not in found] if not args.files and set(vers) == set(VERSIONS) else []
    for k, it in sorted(found.items(), key=lambda kv: (kv[0][0], min(kv[1]["lines"]))):
        if k in allow and not args.all:
            continue
        vs = "" if len(it["vers"]) == len(vers) else f" ({'/'.join(x.upper() for x in sorted(it['vers']))} only)"
        lines = ",".join(str(x) for x in sorted(it["lines"]))
        print(f"{k[0]}:{lines}: {k[1]} | {k[2]} [{'+'.join(sorted(it['tools']))}]{vs}")
        if k in allow:
            print(f"    reviewed: {allow[k]}")
    for k in stale:
        print(f"{ALLOW.relative_to(ROOT)}: no such warning any more: {' | '.join(k)}")
    both = sum(1 for it in found.values() if len(it["tools"]) == 2)
    print(f"uninit {'+'.join(x.upper() for x in vers)}: {len({j[0] for j in jobs})} files, {len(found)} variables "
          f"({both} by clang and gcc), {len(found) - len(new)} reviewed, {len(new)} not reviewed, "
          f"{len(stale)} stale list entries" + (f", {len(errors)} errors" if errors else ""))
    return 1 if new or stale or errors else 0


if __name__ == "__main__":
    sys.exit(main())
