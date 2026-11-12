#!/usr/bin/env python3
"""Move matched functions from a candidate C file into a project C file.

Every function defined in the candidate replaces the matching
INCLUDE_ASM(..., name) line in the target. The candidate's other top-level
items (typedefs, structs, externs, prototypes, with their comments) that the
target lacks are inserted after the target's #include lines. Items that
declare a name the target already declares differently are reported, not
copied, so conflicts get resolved by hand.

    python tools/splice.py src/main/foo.c candidate.c [--only func_X ...]
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path


# An old-style (K&R) definition header: "type name(a, b, c)" with a bare
# identifier list, then parameter declarations ending in ';' before the body.
KR_HEADER = re.compile(r"\s*(?:static\s+)?[\w\s*]+?\b\w+\s*\(\s*(?!void\b)\w+(?:\s*,\s*\w+)*\s*\)\s*\n[\w\s*\[\],;]*$")


def top_level_items(text: str) -> list[str]:
    """Split C source into top-level items: each is a declaration ending in
    ';' at brace depth 0, a function definition (through its closing brace),
    or a preprocessor line. Leading comments stay with their item."""
    items, start, i, depth, n = [], 0, 0, 0, len(text)
    while i < n:
        c = text[i]
        if text.startswith("/*", i):
            i = text.index("*/", i + 2) + 2
            continue
        if text.startswith("//", i):
            i = text.find("\n", i) % (n + 1) or n
            continue
        if c in "\"'":
            j = i + 1
            while text[j] != c:
                j += 2 if text[j] == "\\" else 1
            i = j + 1
            continue
        if c == "#" and depth == 0 and strip_comments(text[start:i]).strip() == "":
            j = text.find("\n", i)
            while j > 0 and text[j - 1] == "\\":   # backslash-continued line
                j = text.find("\n", j + 1)
            j = n if j < 0 else j
            items.append(text[start:j].strip())
            start = i = j
            continue
        if c == "{":
            depth += 1
        elif c == "}":
            depth -= 1
            if depth == 0:
                # A struct/union/initializer closes with its declarator and ';'
                # on the same line ("} Name;", "};"); a function body doesn't.
                line_rest = text[i + 1:].split("\n", 1)[0]
                if not re.match(r"\s*[\w\s*,\[\]]*;", line_rest):
                    items.append(text[start:i + 1].strip())
                    start = i + 1
        elif c == ";" and depth == 0 and KR_HEADER.match(strip_comments(text[start:i])):
            pass  # a K&R parameter declaration: the item runs on to the body
        elif c == ";" and depth == 0:
            # A comment on the same line after the ';' belongs to this item.
            m = re.match(r"[ \t]*(/\*.*?\*/|//[^\n]*)[ \t]*(?=\n|$)", text[i + 1:])
            if m:
                i += len(m.group(0))
            items.append(text[start:i + 1].strip())
            start = i + 1
        i += 1
    if text[start:].strip():
        items.append(text[start:].strip())
    return [it for it in items if it]


def strip_comments(s: str) -> str:
    return re.sub(r"/\*.*?\*/|//[^\n]*", "", s, flags=re.S)


def function_name(item: str) -> str | None:
    code = strip_comments(item)
    # K&R parameter declarations sit between ')' and '{'. Each one runs up to
    # its ';' (which its class lacks), and the whitespace after the last one
    # is matched once, so no run of whitespace can be split two ways: the
    # same strings as "\s*(?:[...]+;\s*)*" without its exponential backtracking.
    m = re.match(r"\s*(?:static\s+)?[\w\s\*]+?\b(\w+)\s*\([^;{]*\)(?:[\w\s\*\[\],]+;)*\s*\{", code, re.S)
    if m and not code.lstrip().startswith(("typedef", "struct", "union", "extern")):
        return m.group(1)
    return None


def declared_name(item: str) -> str | None:
    """Name a declaration introduces (typedef name, variable, prototype)."""
    code = re.sub(r"\s+", " ", strip_comments(item)).strip()
    if code.startswith("#"):
        m = re.match(r"#\s*define\s+(\w+)", code)
        return m.group(1) if m else None
    m = re.search(r"\}\s*(\w+)\s*;$", code)          # typedef struct {...} Name;
    if m:
        return m.group(1)
    m = re.search(r"\b(\w+)\s*\([^()]*\)\s*;$", code)  # prototype
    if m:
        return m.group(1)
    m = re.search(r"\(\s*\*\s*(\w+)\s*(\[[^\]]*\])*\s*\)", code)  # function pointer (array) variable
    if m:
        return m.group(1)
    m = re.search(r"\b(\w+)\s*(\[[^\]]*\])*\s*(=[^;]*)?;$", code)
    return m.group(1) if m else None


def normalized(item: str) -> str:
    return re.sub(r"\s+", " ", strip_comments(item)).strip()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("target", type=Path)
    parser.add_argument("candidate", type=Path)
    parser.add_argument("--only", nargs="*", default=[])
    args = parser.parse_args()

    target = args.target.read_text()
    items = top_level_items(args.candidate.read_text())

    moved = []
    decls = []
    for item in items:
        name = function_name(item)
        if name:
            if args.only and name not in args.only:
                continue
            pattern = re.compile(rf'^INCLUDE_ASM\("[^"]+", {re.escape(name)}\);$', re.M)
            if pattern.search(target):
                target = pattern.sub(lambda _: item, target)
                moved.append(name)
            else:
                print(f"skip {name}: no INCLUDE_ASM for it in {args.target}", file=sys.stderr)
        elif not strip_comments(item).strip().startswith("#include"):
            decls.append(item)

    existing = {}
    for item in top_level_items(target):
        if not function_name(item):
            existing.setdefault(declared_name(item), set()).add(normalized(item))
    new_decls, conflicts = [], []
    for item in decls:
        name, norm = declared_name(item), normalized(item)
        if name in existing:
            if norm not in existing[name]:
                conflicts.append(item)
            continue
        existing.setdefault(name, set()).add(norm)
        new_decls.append(item)

    if new_decls and moved:
        includes = list(re.finditer(r"^#include .*$", target, re.M))
        at = includes[-1].end() if includes else 0
        target = target[:at] + "\n\n" + "\n\n".join(new_decls) + "\n" + target[at:]
    args.target.write_text(target)
    print(f"moved {len(moved)} function(s): {', '.join(moved)}")
    if new_decls and moved:
        print(f"added {len(new_decls)} declaration(s) after the includes")
    for item in conflicts:
        print(f"CONFLICT (not copied): {normalized(item)[:160]}", file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
