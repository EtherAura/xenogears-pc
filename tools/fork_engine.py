#!/usr/bin/env python3
"""Generate the Enhanced engine's fork of the game C (docs/ENGINE_PLAN.md).

    python tools/fork_engine.py            # write engine/src and engine/include from src/ and include/
    python tools/fork_engine.py --verify   # prove the fork preprocesses identically (both versions)
    python tools/fork_engine.py --check    # has the fork been edited since it was generated?

The Original engine compiles src/ with PLATFORM_PC defined. The Enhanced
engine (port/CMakeLists.txt, -DXG_ENGINE=enhanced) compiles engine/, a copy
of src/ and include/ in which every conditional on PLATFORM_PC is resolved
to the PC side: `#ifdef PLATFORM_PC` blocks stay, `#ifndef PLATFORM_PC`
blocks (the PS1 code) go, and a mixed condition such as
`defined(PLATFORM_PC) && !defined(XG_PS1_LAYOUT)` keeps its other terms.
Nothing else changes: VERSION_JP conditionals, PORT_TODO calls, names and
layout all stay, so at the fork point the two engines are the same program
(--verify: `cc -E -P` of every port-compiled C file from both trees is
byte-identical). From then on engine/ is edited freely, under none of the
matching rules; src/ stays the decomp.

The PsyQ SDK copies (src/main/psyq, src/video_player/psyq) are not forked:
the port's platform layer replaces them in both engines.

engine/FORK_MANIFEST.json records the generating commit and a hash of every
written file. Regenerating refuses to overwrite a fork that has been edited
since (--check reports it; --force overwrites anyway), because after the
fork diverges this tool is history, not a build step.
"""

from __future__ import annotations

import argparse
import concurrent.futures
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import pc_sources  # noqa: E402

ENGINE = ROOT / "engine"
MANIFEST = ENGINE / "FORK_MANIFEST.json"
TREES = ("src", "include")
EXCLUDE = tuple(ROOT / e for e in ("src/main/psyq", "src/video_player/psyq"))
KNOWN = {"PLATFORM_PC": True}  # the macros whose definedness the fork resolves
TEXT_SUFFIXES = {".c", ".h", ".inc", ".s", ".txt", ".md"}

DIRECTIVE = re.compile(r"^\s*#\s*(if|ifdef|ifndef|elif|else|endif)\b(.*)$", re.S)
TOKEN = re.compile(r"\s*(&&|\|\||!=|==|<=|>=|[()!]|[A-Za-z_]\w*|0[xX][0-9a-fA-F]+|\d+|\S)")


class Unknown:
    """A condition the fork cannot decide: its text, and how tightly it binds."""

    def __init__(self, text: str, level: int):
        self.text = text
        self.level = level  # 0 primary/not, 1 and, 2 or


def strip_comments(s: str) -> str:
    s = re.sub(r"/\*.*?\*/", " ", s, flags=re.S)
    s = re.sub(r"//.*", "", s)
    return s.strip()


class Parser:
    """A three-valued evaluation of a #if expression: True, False or Unknown(text)."""

    def __init__(self, text: str):
        self.toks = [m.group(1) for m in TOKEN.finditer(text)]
        self.i = 0

    def peek(self) -> str | None:
        return self.toks[self.i] if self.i < len(self.toks) else None

    def next(self) -> str:
        t = self.toks[self.i]
        self.i += 1
        return t

    def parse(self):
        v = self.p_or()
        if self.peek() is not None:
            raise ValueError("trailing tokens")
        return v

    def p_or(self):
        parts = [self.p_and()]
        while self.peek() == "||":
            self.next()
            parts.append(self.p_and())
        if any(p is True for p in parts):
            return True
        parts = [p for p in parts if p is not False]
        if not parts:
            return False
        if len(parts) == 1:
            return parts[0]
        return Unknown(" || ".join(p.text for p in parts), 2)

    def p_and(self):
        parts = [self.p_not()]
        while self.peek() == "&&":
            self.next()
            parts.append(self.p_not())
        if any(p is False for p in parts):
            return False
        parts = [p for p in parts if p is not True]
        if not parts:
            return True
        if len(parts) == 1:
            return parts[0]
        return Unknown(" && ".join(f"({p.text})" if p.level > 1 else p.text for p in parts), 1)

    def p_not(self):
        if self.peek() == "!":
            self.next()
            v = self.p_not()
            if isinstance(v, bool):
                return not v
            return Unknown("!" + (f"({v.text})" if v.level > 0 else v.text), 0)
        return self.p_primary()

    def p_primary(self):
        t = self.peek()
        if t == "(":
            self.next()
            v = self.p_or()
            if self.next() != ")":
                raise ValueError("missing )")
            if isinstance(v, bool):
                return v
            return Unknown(f"({v.text})", 0)
        if t == "defined":
            self.next()
            paren = self.peek() == "("
            if paren:
                self.next()
            name = self.next()
            if paren and self.next() != ")":
                raise ValueError("defined(")
            if name in KNOWN:
                return KNOWN[name]
            return Unknown(f"defined({name})", 0)
        # Anything else up to the next && / || / ) at this level: a number, a
        # macro, a comparison. Decided only when it is a bare known macro or a number.
        start = self.i
        depth = 0
        while self.peek() is not None:
            t = self.peek()
            if depth == 0 and t in ("&&", "||", ")"):
                break
            if t == "(":
                depth += 1
            elif t == ")":
                depth -= 1
            self.next()
        toks = self.toks[start:self.i]
        if not toks:
            raise ValueError("empty operand")
        if len(toks) == 1:
            if toks[0] in KNOWN:
                return KNOWN[toks[0]]  # a defined macro expands to 1 (PLATFORM_PC is defined as 1)
            if re.fullmatch(r"0[xX][0-9a-fA-F]+|\d+", toks[0]):
                return int(toks[0], 0) != 0
        return Unknown(" ".join(toks), 0)


def evaluate(kind: str, arg: str):
    """kind: if/ifdef/ifndef/elif; arg: the rest of the line. True, False or Unknown."""
    arg = strip_comments(arg)
    if kind in ("ifdef", "ifndef"):
        name = arg.split()[0] if arg else ""
        if name in KNOWN:
            return KNOWN[name] if kind == "ifdef" else not KNOWN[name]
        return Unknown(f"defined({name})" if kind == "ifdef" else f"!defined({name})", 0)
    try:
        return Parser(arg).parse()
    except (ValueError, IndexError):
        return Unknown(arg, 2)


def mentions_known(line: str) -> bool:
    return any(re.search(rf"\b{k}\b", line) for k in KNOWN)


class Frame:
    __slots__ = ("kind", "active", "done", "parent")

    def __init__(self, kind: str, active: bool, done: bool, parent: bool):
        self.kind = kind  # pass (directives emitted), resolved (consumed), dead (inside dropped code)
        self.active = active
        self.done = done
        self.parent = parent


def comment_state(line: str, in_comment: bool) -> tuple[bool, bool]:
    """(the line starts a directive outside a comment, in_comment after the line)."""
    stripped = line.lstrip()
    directive = not in_comment and stripped.startswith("#")
    i = 0
    quote = None
    while i < len(line):
        c = line[i]
        if in_comment:
            if line.startswith("*/", i):
                in_comment = False
                i += 2
                continue
        elif quote:
            if c == "\\":
                i += 2
                continue
            if c == quote:
                quote = None
        else:
            if line.startswith("/*", i):
                in_comment = True
                i += 2
                continue
            if line.startswith("//", i):
                break
            if c in "\"'":
                quote = c
        i += 1
    return directive, in_comment


def resolve(text: str) -> str:
    """The text with every conditional on the KNOWN macros resolved."""
    lines = text.split("\n")
    out: list[str] = []
    stack: list[Frame] = []
    in_comment = False
    i = 0

    def eff() -> bool:
        return (stack[-1].parent and stack[-1].active) if stack else True

    while i < len(lines):
        line = lines[i]
        directive, in_comment = comment_state(line, in_comment)
        m = DIRECTIVE.match(line) if directive else None
        if not m:
            if eff():
                out.append(line)
            i += 1
            continue
        # A directive, possibly continued with backslashes.
        physical = [line]
        while physical[-1].endswith("\\") and i + 1 < len(lines):
            i += 1
            physical.append(lines[i])
            _, in_comment = comment_state(lines[i], in_comment)
        i += 1
        logical = "\n".join(physical)
        kind, arg = m.group(1), m.group(2) if len(physical) == 1 else "".join(p.rstrip("\\") for p in physical)[m.end(1):]
        indent = line[:len(line) - len(line.lstrip())]

        def emit(text: str | None = None) -> None:
            out.extend(physical if text is None else [text])

        if kind in ("if", "ifdef", "ifndef"):
            parent = eff()
            if not parent:
                stack.append(Frame("dead", False, True, False))
                continue
            if not mentions_known(logical):
                stack.append(Frame("pass", True, False, True))
                emit()
                continue
            v = evaluate(kind, arg)
            if v is True:
                stack.append(Frame("resolved", True, True, True))
            elif v is False:
                stack.append(Frame("resolved", False, False, True))
            else:
                stack.append(Frame("pass", True, False, True))
                emit(f"{indent}#if {v.text}")
        elif kind == "elif":
            f = stack[-1]
            if f.kind == "dead":
                continue
            if f.done:
                f.active = False
                continue
            if not mentions_known(logical):
                if f.kind == "resolved":  # the #if was dropped: this starts the conditional
                    f.kind = "pass"
                    f.active = True
                    emit(f"{indent}#if {strip_comments(arg)}")
                else:
                    f.active = True
                    emit()
                continue
            v = evaluate(kind, arg)
            if v is True:
                f.active, f.done = True, True
                if f.kind == "pass":
                    emit(f"{indent}#else")
            elif v is False:
                f.active = False
            else:
                f.active = True
                if f.kind == "resolved":
                    f.kind = "pass"
                    emit(f"{indent}#if {v.text}")
                else:
                    emit(f"{indent}#elif {v.text}")
        elif kind == "else":
            f = stack[-1]
            if f.kind == "dead":
                continue
            if f.done:
                f.active = False
                continue
            f.active, f.done = True, True
            if f.kind == "pass":
                emit()
        else:  # endif
            f = stack.pop()
            if f.kind == "pass":
                emit()
    if stack:
        raise ValueError("unbalanced conditionals")
    return "\n".join(out)


def sources() -> list[Path]:
    files = []
    for tree in TREES:
        for p in sorted((ROOT / tree).rglob("*")):
            if p.is_file() and not any(p.is_relative_to(e) for e in EXCLUDE) and "__pycache__" not in p.parts:
                files.append(p)
    return files


def sha1(data: bytes) -> str:
    return hashlib.sha1(data).hexdigest()


def generate(force: bool) -> int:
    if MANIFEST.exists() and not force:
        changed = edited()
        if changed:
            print(f"engine/ has been edited since it was generated ({len(changed)} files, e.g. {changed[0]}); "
                  f"the fork has diverged. --force overwrites it.", file=sys.stderr)
            return 1
    for tree in TREES:
        if (ENGINE / tree).exists():
            shutil.rmtree(ENGINE / tree)
    manifest: dict[str, str] = {}
    resolved = 0
    for src in sources():
        rel = src.relative_to(ROOT)
        dst = ENGINE / rel
        dst.parent.mkdir(parents=True, exist_ok=True)
        data = src.read_bytes()
        if src.suffix in TEXT_SUFFIXES:
            text = data.decode("utf-8")
            new = resolve(text)
            if new != text:
                resolved += 1
            data = new.encode("utf-8")
        dst.write_bytes(data)
        manifest[rel.as_posix()] = sha1(data)
    head = subprocess.run(["git", "-C", str(ROOT), "rev-parse", "HEAD"], capture_output=True, text=True).stdout.strip()
    MANIFEST.write_text(json.dumps({"generated_from": head, "resolves": sorted(KNOWN), "files": manifest},
                                   indent=1, sort_keys=True) + "\n")
    print(f"engine/: {len(manifest)} files written, {resolved} with conditionals resolved, from {head[:12]}")
    return 0


def edited() -> list[str]:
    """Files of the fork whose contents differ from the manifest (edited, added or removed)."""
    m = json.loads(MANIFEST.read_text())["files"]
    changed = [rel for rel, h in m.items() if not (ENGINE / rel).is_file() or sha1((ENGINE / rel).read_bytes()) != h]
    present = {p.relative_to(ENGINE).as_posix() for tree in TREES for p in (ENGINE / tree).rglob("*") if p.is_file()}
    changed += sorted(present - set(m))
    return changed


def preprocess(cc: str, src: Path, include: Path, ver: str) -> str:
    flags = ["-E", "-P", "-std=gnu89", "-nostdinc", "-fno-builtin", "-funsigned-char", "-fwrapv",
             "-fno-strict-aliasing", "-fcommon", "-D_LANGUAGE_C", "-DPLATFORM_PC", f"-DVERSION_{ver.upper()}",
             f"-I{include}", str(src)]
    p = subprocess.run([cc, *flags], cwd=ROOT, capture_output=True, text=True)
    if p.returncode != 0:
        raise RuntimeError(f"{src}: {p.stderr.strip()[-600:]}")
    return p.stdout


def verify(cc: str) -> int:
    jobs = []
    for ver in pc_sources.VERSIONS:
        for files in pc_sources.modules(ver).values():
            jobs += [(ver, f) for f in files]

    def one(job):
        ver, f = job
        rel = f.relative_to(ROOT)
        a = preprocess(cc, f, ROOT / "include", ver)
        b = preprocess(cc, ENGINE / rel, ENGINE / "include", ver)
        return ver, rel, a == b.replace(str(ENGINE), str(ROOT))

    bad = []
    with concurrent.futures.ThreadPoolExecutor(os.cpu_count() or 4) as pool:
        for ver, rel, same in pool.map(one, jobs):
            if not same:
                bad.append(f"{ver} {rel}")
    print(f"verify: {len(jobs)} preprocessed files compared, {len(bad)} differ")
    for b in bad:
        print("  " + b)
    return 1 if bad else 0


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--verify", action="store_true", help="compare cc -E of src/ and engine/ (no writing)")
    ap.add_argument("--check", action="store_true", help="list fork files edited since generation")
    ap.add_argument("--force", action="store_true", help="regenerate over an edited fork")
    ap.add_argument("--cc", default=os.environ.get("CC", "gcc"))
    a = ap.parse_args()
    if a.check:
        changed = edited()
        print("\n".join(changed) if changed else "engine/ is as generated")
        return 1 if changed else 0
    if a.verify:
        return verify(a.cc)
    return generate(a.force)


if __name__ == "__main__":
    sys.exit(main())
