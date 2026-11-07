#!/usr/bin/env python3
"""Find calls whose arguments depend on their order of evaluation (PC port gate).

The PS1 compilers (GCC 2.6/2.7 MIPS, every preset) evaluate call arguments
left to right; x86-64 GCC evaluates them right to left. A call that changes
something in one argument and reads it in another gets different values on
the PC:

    f(*p++, *p++)                      a cursor advanced twice
    f(ANIM_WORD_ARG(p), ANIM_WORD_ARG(p))
    f(*p++, p[0])                      advanced in one, read in another
    f(s->n, s->n = v)                  a field assigned in one, read in another
    f(rand(), rand())                  a function with side effects, twice

Neither compiler warns about a GNU statement expression such as
ANIM_WORD_ARG's, so this is a text scan. It reads the code the PC compiles:
#if/#ifdef branches on PLATFORM_PC and VERSION_US/VERSION_JP are resolved,
any other condition keeps both branches.

    python tools/argorder.py                 # US, every src/ C file
    python tools/argorder.py --ver jp
    python tools/argorder.py src/main/sprite.c ...

Exit status 1 when a call is found. A call that is safe (the callee reads the
variable only after both arguments are evaluated, say) is silenced with
`/* argorder: ok */` on the line where the call starts.
"""
from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
EXCLUDE = ("src/main/psyq/", "src/video_player/psyq/")
OK_MARK = "argorder: ok"

IDENT = r"[A-Za-z_]\w*(?:\s*(?:->|\.)\s*\w+|\[[^\]]*\])*"
MOD = re.compile(rf"(?:\+\+|--)\s*(?P<pre>{IDENT})|(?P<post>{IDENT})\s*(?:\+\+|--)"
                 rf"|ANIM_WORD_ARG\(\s*(?P<anim>{IDENT})\s*\)"
                 rf"|(?P<asg>{IDENT})\s*(?:\+|-|\*|/|%|\||&|\^|<<|>>)?=(?!=)")
# Functions with side effects whose second call in one argument list is a bug:
# the random number generators (PsyQ rand and the game's wrappers).
IMPURE = re.compile(r"\b(rand|\w*[Rr]andom\w*)\s*\(")
NOT_CALLS = {"if", "while", "for", "switch", "return", "sizeof", "ANIM_WORD_ARG", "defined"}


def resolve(text: str, ver: str) -> str:
    """Blank the lines of #if branches the PC build of `ver` doesn't compile."""
    known = {"PLATFORM_PC": 1, "VERSION_US": int(ver == "us"), "VERSION_JP": int(ver == "jp")}

    def value(cond: str):
        cond = re.sub(r"/\*.*?\*/|//.*", "", cond).strip()
        cond = re.sub(r"\bdefined\s*\(?\s*(\w+)\s*\)?",
                      lambda m: str(int(m.group(1) in known)) if m.group(1) in known else "?", cond)
        names = set(re.findall(r"[A-Za-z_]\w*", cond))
        if "?" in cond or names - set(known):
            return None
        expr = re.sub(r"[A-Za-z_]\w*", lambda m: str(known[m.group(0)]), cond)
        expr = expr.replace("&&", " and ").replace("||", " or ").replace("!", " not ")
        try:
            return bool(eval(expr, {"__builtins__": {}}))  # noqa: S307 (digits and operators only)
        except Exception:  # noqa: BLE001
            return None

    out, stack = [], []   # stack: (parent emits, this branch emits, a branch was taken, known)
    for line in text.split("\n"):
        m = re.match(r"\s*#\s*(ifdef|ifndef|if|elif|else|endif)\b(.*)", line)
        emit = all(s[1] for s in stack)
        if m:
            kind, rest = m.group(1), m.group(2)
            if kind in ("if", "ifdef", "ifndef"):
                if kind == "if":
                    v = value(rest)
                else:
                    name = rest.split()[0] if rest.split() else ""
                    v = (name in known) if name in known else None
                    if kind == "ifndef" and v is not None:
                        v = not v
                stack.append([emit, True if v is None else v, bool(v), v is not None])
            elif kind == "elif" and stack:
                s = stack[-1]
                v = value(rest)
                if not s[3] or v is None:
                    s[1], s[3] = True, False
                else:
                    s[1] = v and not s[2]
                    s[2] = s[2] or v
            elif kind == "else" and stack:
                s = stack[-1]
                s[1] = (not s[2]) if s[3] else True
            elif kind == "endif" and stack:
                stack.pop()
            out.append("")
            continue
        out.append(line if emit else "")
    return "\n".join(out)


def strip(text: str) -> str:
    """Comments and string contents become spaces; line breaks stay."""
    text = re.sub(r"/\*.*?\*/", lambda m: re.sub(r"[^\n]", " ", m.group(0)), text, flags=re.S)
    text = re.sub(r"//[^\n]*", "", text)
    text = re.sub(r"\"(\\.|[^\"\\\n])*\"", '""', text)
    text = re.sub(r"'(\\.|[^'\\\n])*'", "0", text)
    return text


def split_args(s: str) -> list[str]:
    depth, cur, out = 0, "", []
    for ch in s:
        if ch in "([{":
            depth += 1
        elif ch in ")]}":
            depth -= 1
        if ch == "," and depth == 0:
            out.append(cur)
            cur = ""
        else:
            cur += ch
    out.append(cur)
    return out


def calls(text: str):
    for m in re.finditer(r"\b([A-Za-z_]\w*)\s*\(", text):
        if m.group(1) in NOT_CALLS:
            continue
        i = j = m.end()
        depth = 1
        while j < len(text) and depth:
            if text[j] == "(":
                depth += 1
            elif text[j] == ")":
                depth -= 1
            j += 1
        yield m.group(1), m.start(), text[i:j - 1]


def norm(lvalue: str) -> str:
    return re.sub(r"\s+", "", lvalue)


def uses(arg: str, target: str) -> bool:
    """Does `arg` read `target` (a variable, or a field/element lvalue)?"""
    if re.fullmatch(r"\w+", target):
        return re.search(rf"\b{target}\b", arg) is not None
    return target in norm(arg)


def check(name: str, args: str) -> str | None:
    parts = split_args(args)
    if len(parts) < 2:
        return None
    for k, a in enumerate(parts):
        for m in MOD.finditer(a):
            lv = m.group("pre") or m.group("post") or m.group("anim") or m.group("asg")
            target = norm(lv)
            if any(uses(b, target) for i, b in enumerate(parts) if i != k):
                return target
    impure = [i for i, a in enumerate(parts) if IMPURE.search(a)]
    if len(impure) >= 2:
        return IMPURE.search(parts[impure[0]]).group(1) + "()"
    return None


def scan(path: Path, ver: str) -> list[str]:
    raw = path.read_text(errors="replace")
    lines = raw.split("\n")
    text = strip(resolve(raw, ver))
    found, seen = [], set()
    for name, pos, args in calls(text):
        if pos in seen:
            continue
        what = check(name, args)
        if what is None:
            continue
        seen.add(pos)
        line = text.count("\n", 0, pos) + 1
        if OK_MARK in lines[line - 1]:
            continue
        rel = path.resolve().relative_to(ROOT) if path.resolve().is_relative_to(ROOT) else path
        found.append(f"{rel}:{line}: {name}(...) [{what}]")
    return found


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--ver", default="us", choices=["us", "jp"])
    ap.add_argument("files", nargs="*", type=Path)
    args = ap.parse_args()
    files = args.files or sorted(p for p in (ROOT / "src").rglob("*.c")
                                 if not str(p.relative_to(ROOT)).startswith(EXCLUDE))
    found = [f for p in files for f in scan(p, args.ver)]
    for f in found:
        print(f)
    print(f"argorder {args.ver.upper()}: {len(files)} files, {len(found)} order-dependent calls")
    return 1 if found else 0


if __name__ == "__main__":
    sys.exit(main())
