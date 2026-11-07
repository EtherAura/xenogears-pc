#!/usr/bin/env python3
"""The interface's languages: the F1 menu's and the extras' text (port/src/ui/l10n.h).

    python tools/l10n.py extract [--out FILE.pot]   # the English strings (msgids) in the sources
    python tools/l10n.py check [PO...]              # validate catalogs (default: port/src/ui/lang/*.po)
    python tools/l10n.py update PO                  # add new msgids (empty), mark gone ones obsolete
    python tools/l10n.py missing PO                 # msgids without a translation, one per line
    python tools/l10n.py chars PO...                # the characters a catalog needs beyond ASCII
    python tools/l10n.py embed OUT.h PO...          # the build: built-in catalogs as C++ data

Catalogs are gettext PO files (UTF-8): msgid is the English text as the
sources have it, msgstr its translation; an empty or fuzzy msgstr, or one
whose printf conversions differ from the msgid's, is not used (the English
shows). The header entry (msgid "") gives "Language: <code>" and
"X-Language-Name: <name in that language>". The port reads the built-in
catalogs (port/src/ui/lang/*.po, embedded) and the player's own
(<save folder>/lang/*.po) at run time and checks the same things then.

What is extracted: string literals passed to tr() and N_() (l10n.h), and to
the helpers that translate their own text (KEYWORDS: the settings menu's
row(), hint(), textLines(), warning(), checkbox(), beginModal(), a Message's
set(failed, fmt)...). An ImGui id suffix ("##id", "###id") is not part of the
msgid: tr() keeps the source's suffix. Strings with no letter are skipped.

check reports, per catalog: entries whose msgid the sources no longer have
(stale), printf conversions that differ, translations that are empty, and
characters the menu's font cannot draw (with --glyphs: every character must be
ASCII, in the dialogue font's table (port/src/ui/japanese_codes.h) or in the
pixel fallback (port/third_party/jukebox-font/glyphs.bin)). The exit status is
the number of errors (stale entries and untranslated ones are only counted).

Standard library only (the build runs embed).
"""

from __future__ import annotations

import argparse
import re
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
LANG_DIR = ROOT / "port/src/ui/lang"
SOURCES = ["port/src/ui/*.cpp", "port/src/extras/*.cpp"]

# function -> the arguments (from 1) that are translatable text. Methods
# count by name too (Message::set). Every string literal in such an argument
# is extracted (both sides of a ?:), and an argument naming an array of
# strings in the same file (choice(id, names, ...), tr(about[i])) extracts
# that array's strings.
KEYWORDS = {
    "tr": (1,),
    "l10nText": (1,),
    "N_": (1,),
    # port/src/ui (menu.h, sections.cpp): helpers that translate their text
    "hint": (1,),
    "row": (1,),
    "textLines": (1,),
    "warning": (1,),
    "checkbox": (1,),
    "set": (2,),
    "pathText": (2,),
    "folderField": (5,),
    "pathField": (8,),
    "mouseOption": (2, 4),
    "choice": (2,),
    "listChoice": (2,),
    "headers": (1,),
    # port/src/extras (viewer.h): the shell translates these
    "status": (1,),
    "error": (1,),
    "commandItem": (1,),
    "beginPane": (4,),
    "line": (2,),  # the jukebox's coloured line (jukebox.cpp)
    "flowButton": (1,),  # the sprite viewer's flowing buttons (sprites.cpp)
    "flowCheckbox": (1,),
    "focusButton": (1,),  # the focused view's bar (viewer.h, shell.cpp)
    "focusCheckbox": (1,),
    "focusText": (1,),
    "openPicker": (2,),  # the file picker's title (picker.cpp)
    "layer": (1,),  # the map viewer's layer boxes (maps.cpp)
    "worldLayer": (1,),
}

# `TYPE name = { ... }`: the fields (from 1) that are text (viewer.h's Viewer:
# its name and blurb).
INITIALIZERS = {
    "Viewer": (2, 3),
}

FORMAT = re.compile(r"%(?:%|[-+#0]*(?:\*|\d+)?(?:\.(?:\*|\d+))?(?:hh|h|ll|l|L|z|j|t)?[diouxXeEfFgGaAcspn])")


# ---- sources --------------------------------------------------------------------------------

def tokens(text: str):
    """C/C++ tokens: ('id', name), ('str', value), ('p', punct); comments and
    character literals dropped. String values are decoded (C escapes, the
    bytes as UTF-8)."""
    i, n = 0, len(text)
    while i < n:
        c = text[i]
        if c in " \t\r\n":
            i += 1
        elif text.startswith("//", i):
            j = text.find("\n", i)
            i = n if j < 0 else j
        elif text.startswith("/*", i):
            j = text.find("*/", i + 2)
            i = n if j < 0 else j + 2
        elif c == "#" and (i == 0 or text[i - 1] == "\n" or text[:i].rstrip(" \t").endswith("\n")):
            # a preprocessor line (with its continuations): #include <...> is no string
            j = i
            while True:
                k = text.find("\n", j)
                if k < 0:
                    j = n
                    break
                if text[k - 1] == "\\":
                    j = k + 1
                    continue
                j = k
                break
            line = text[i:j]
            if not re.match(r"#\s*(include|pragma|error|warning)", line):
                yield from tokens(re.sub(r"^#\s*\w+", "", line).replace("\\\n", " "))
            i = j
        elif c == '"' or (c in "uUL8R" and re.match(r'(u8|u|U|L)?R?"', text[i:i + 4])):
            m = re.match(r'(u8|u|U|L)?(R?)"', text[i:])
            raw = m.group(2) == "R"
            i += m.end()
            if raw:
                d = text.index("(", i)
                delim = ")" + text[i:d] + '"'
                j = text.index(delim, d)
                yield ("str", text[d + 1:j])
                i = j + len(delim)
                continue
            out = bytearray()
            while i < n and text[i] != '"':
                if text[i] == "\\":
                    e = text[i + 1]
                    if e == "x":
                        m2 = re.match(r"[0-9a-fA-F]+", text[i + 2:])
                        out.append(int(m2.group(0), 16) & 0xFF)
                        i += 2 + m2.end()
                        continue
                    if e in "uU":
                        k = 4 if e == "u" else 8
                        out += chr(int(text[i + 2:i + 2 + k], 16)).encode()
                        i += 2 + k
                        continue
                    if e in "01234567":
                        m2 = re.match(r"[0-7]{1,3}", text[i + 1:])
                        out.append(int(m2.group(0), 8) & 0xFF)
                        i += 1 + m2.end()
                        continue
                    out += {"n": b"\n", "t": b"\t", "r": b"\r", "0": b"\0", "a": b"\a", "b": b"\b",
                            "f": b"\f", "v": b"\v"}.get(e, e.encode())
                    i += 2
                else:
                    out += text[i].encode()
                    i += 1
            i += 1
            yield ("str", out.decode("utf-8", "replace"))
        elif c == "'":
            j = i + 1
            while j < n and text[j] != "'":
                j += 2 if text[j] == "\\" else 1
            i = j + 1
        elif c.isalpha() or c == "_":
            m = re.match(r"[A-Za-z_]\w*", text[i:])
            yield ("id", m.group(0))
            i += m.end()
        elif c.isdigit():
            m = re.match(r"[0-9][0-9A-Za-z_.']*", text[i:])
            i += m.end()
        else:
            yield ("p", c)
            i += 1


def strip_id(s: str) -> str:
    """The visible part of an ImGui label (before "##" or "###")."""
    k = s.find("##")
    return s if k < 0 else s[:k]


def translatable(s: str) -> bool:
    return any(ch.isalpha() for ch in FORMAT.sub("", s))


def chains(toks, a: int, b: int):
    """The string-literal chains (adjacent literals joined) in toks[a:b]."""
    out, cur = [], None
    for kind, value in toks[a:b]:
        if kind == "str":
            cur = value if cur is None else cur + value
        else:
            if cur is not None:
                out.append(cur)
            cur = None
    if cur is not None:
        out.append(cur)
    return out


def arrays(toks) -> dict[str, list[tuple[int, list[str]]]]:
    """NAME -> [(token position, its strings)], for every `NAME[...] = { "..", ".." }`
    whose elements are all string literals (a table of names). A name used
    in several functions has several tables: the nearest one before a use is
    its (array_at)."""
    out = {}
    for k in range(len(toks) - 3):
        if toks[k][0] != "id" or toks[k + 1] != ("p", "["):
            continue
        j = k + 2
        while j < len(toks) and toks[j] != ("p", "]"):
            j += 1
        if j + 2 >= len(toks) or toks[j + 1] != ("p", "=") or toks[j + 2] != ("p", "{"):
            continue
        depth, e, flat = 0, j + 3, True
        while e < len(toks):
            if toks[e] == ("p", "{"):
                depth += 1
                flat = False
            elif toks[e] == ("p", "}"):
                if depth == 0:
                    break
                depth -= 1
            elif toks[e][0] not in ("str",) and toks[e] != ("p", ","):
                flat = False
            e += 1
        if flat:
            out.setdefault(toks[k][1], []).append((k, chains(toks, j + 3, e)))
    return out


def array_at(tables, name: str, at: int) -> list[str]:
    defs = tables.get(name, [])
    before = [d for d in defs if d[0] < at]
    return (before[-1] if before else defs[0])[1] if defs else []


def extract(files: list[Path] | None = None) -> dict[str, list[str]]:
    """msgid -> [source file, ...] in source order."""
    found: dict[str, list[str]] = {}
    if files is None:
        files = sorted({p for pat in SOURCES for p in ROOT.glob(pat)})
    for path in files:
        text = path.read_text(encoding="utf-8")
        toks = list(tokens(text))
        tables = arrays(toks)
        rel = path.relative_to(ROOT).as_posix()

        def add(msgid: str):
            msgid = strip_id(msgid)
            if translatable(msgid):
                found.setdefault(msgid, []).append(rel)

        for k, (kind, value) in enumerate(toks):
            if (kind == "id" and value in INITIALIZERS and k + 3 < len(toks) and toks[k + 1][0] == "id"
                    and toks[k + 2] == ("p", "=") and toks[k + 3] == ("p", "{")):
                field, j, depth, text = 1, k + 4, 0, None
                while j < len(toks) and not (depth == 0 and toks[j] == ("p", "}")):
                    if toks[j] in (("p", "{"), ("p", "(")):
                        depth += 1
                    elif toks[j] in (("p", "}"), ("p", ")")):
                        depth -= 1
                    elif toks[j] == ("p", ",") and depth == 0:
                        if text is not None and field in INITIALIZERS[value]:
                            add(text)
                        field, text = field + 1, None
                    elif toks[j][0] == "str" and depth == 0:
                        text = toks[j][1] if text is None else text + toks[j][1]
                    j += 1
                continue
            if kind != "id" or value not in KEYWORDS or k + 1 >= len(toks) or toks[k + 1] != ("p", "("):
                continue
            want = KEYWORDS[value]
            depth, arg, j, begin = 0, 1, k + 2, k + 2
            spans = []
            while j < len(toks):
                kind2, v2 = toks[j]
                if kind2 == "p" and v2 in "([{":
                    depth += 1
                elif kind2 == "p" and v2 in ")]}":
                    if depth == 0:
                        break
                    depth -= 1
                elif kind2 == "p" and v2 == "," and depth == 0:
                    spans.append((arg, begin, j))
                    arg += 1
                    begin = j + 1
                j += 1
            spans.append((arg, begin, j))
            for n, a, b in spans:
                if n not in want or a >= b:
                    continue
                if toks[a][0] == "id" and toks[a][1] in tables and (a + 1 == b or toks[a + 1] == ("p", "[")):
                    for m in array_at(tables, toks[a][1], a):
                        add(m)
                for m in chains(toks, a, b):
                    add(m)
    return found


# ---- PO files ----------------------------------------------------------------------------------

def po_unquote(s: str) -> str:
    s = s.strip()
    if not (s.startswith('"') and s.endswith('"')):
        raise ValueError(f"not a quoted string: {s}")
    body, out, i = s[1:-1], [], 0
    while i < len(body):
        if body[i] == "\\" and i + 1 < len(body):
            e = body[i + 1]
            out.append({"n": "\n", "t": "\t", "r": "\r", '"': '"', "\\": "\\"}.get(e, "\\" + e))
            i += 2
        else:
            out.append(body[i])
            i += 1
    return "".join(out)


def po_quote(s: str, width: int = 76) -> str:
    esc = s.replace("\\", "\\\\").replace('"', '\\"').replace("\t", "\\t").replace("\n", "\\n\n")
    lines = esc.split("\n")
    if lines and lines[-1] == "":
        lines.pop()
    if len(lines) == 1 and len(lines[0]) + 9 <= width:
        return f'"{lines[0]}"'
    # long: wrap at spaces, as msgcat does
    out = ['""']
    for line in lines:
        while len(line) > width:
            cut = line.rfind(" ", 0, width)
            if cut <= 0:
                break
            out.append(f'"{line[:cut + 1]}"')
            line = line[cut + 1:]
        out.append(f'"{line}"')
    return "\n".join(out)


class Entry:
    def __init__(self):
        self.comments: list[str] = []
        self.msgctxt: str | None = None
        self.msgid = ""
        self.msgstr = ""
        self.fuzzy = False
        self.obsolete = False
        self.line = 0


def read_po(path: Path) -> list[Entry]:
    entries: list[Entry] = []
    cur = Entry()
    field = None
    started = False

    def flush():
        nonlocal cur, started, field
        if started:
            entries.append(cur)
        cur, started, field = Entry(), False, None

    for no, raw in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        line = raw.rstrip("\n")
        obsolete = line.startswith("#~")
        if obsolete:
            line = line[2:].lstrip()
        if not line.strip():
            flush()
            continue
        if line.startswith("#") and not obsolete:
            if started and field is not None:
                flush()
            if line.startswith("#,") and "fuzzy" in line:
                cur.fuzzy = True
            cur.comments.append(raw)
            continue
        m = re.match(r"(msgctxt|msgid|msgstr)\s+(\".*\")\s*$", line)
        if m:
            if m.group(1) in ("msgctxt", "msgid") and field == "msgstr":
                flush()
            if not started:
                cur.line = no
            started = True
            cur.obsolete = cur.obsolete or obsolete
            field = m.group(1)
            setattr(cur, field, po_unquote(m.group(2)))
        elif line.startswith('"') and field:
            setattr(cur, field, getattr(cur, field) + po_unquote(line))
        else:
            raise ValueError(f"{path}:{no}: cannot read: {raw}")
    flush()
    return entries


def header_fields(entries: list[Entry]) -> dict[str, str]:
    for e in entries:
        if e.msgid == "" and not e.obsolete:
            out = {}
            for line in e.msgstr.split("\n"):
                if ":" in line:
                    k, v = line.split(":", 1)
                    out[k.strip()] = v.strip()
            return out
    return {}


def write_po(path: Path, entries: list[Entry]) -> None:
    out = []
    for e in entries:
        block = list(e.comments)
        pre = "#~ " if e.obsolete else ""
        def field(name, value):
            q = po_quote(value).split("\n")
            block.append(f"{pre}{name} {q[0]}")
            block.extend(pre + x for x in q[1:])
        if e.msgctxt is not None:
            field("msgctxt", e.msgctxt)
        field("msgid", e.msgid)
        field("msgstr", e.msgstr)
        out.append("\n".join(block))
    path.write_text("\n\n".join(out) + "\n", encoding="utf-8")


def conversions(s: str) -> list[str]:
    """The printf conversions in order, %% left out, as what they read: a '*'
    for each width or precision argument, the length and the type. Flags,
    widths and precisions may differ ("%.0s" prints nothing of its string)."""
    out = []
    for m in FORMAT.findall(s):
        if m == "%%":
            continue
        out.append("*" * m.count("*") + re.sub(r"^%[-+#0]*(?:\*|\d+)?(?:\.(?:\*|\d+))?", "", m))
    return out


def usable(e: Entry) -> str | None:
    """Why an entry's msgstr is not used, or None when it is."""
    if e.obsolete:
        return "obsolete"
    if e.fuzzy:
        return "fuzzy"
    if e.msgstr == "":
        return "untranslated"
    if conversions(e.msgid) != conversions(e.msgstr):
        return f"printf conversions differ: {conversions(e.msgid)} vs {conversions(e.msgstr)}"
    if "##" in e.msgstr:
        return "an ImGui id (##) in the translation"
    return None


# ---- glyphs ------------------------------------------------------------------------------------

def known_glyphs() -> set[int]:
    """Characters the menu can draw with either release's disc and no other
    image: ASCII, what both dialogue fonts hold (japanese_codes.h: a JP and
    a US position), and the pixel fallback (glyphs.bin)."""
    have = set(range(0x20, 0x7F))
    codes = (ROOT / "port/src/ui/japanese_codes.h").read_text(encoding="utf-8")
    have |= {int(u, 16) for u, jp, us in re.findall(r"\{0x([0-9a-fA-F]+), (-?\d+), (-?\d+)\}", codes)
             if int(jp) >= 0 and int(us) >= 0}
    data = (ROOT / "port/third_party/jukebox-font/glyphs.bin").read_bytes()
    have |= {struct.unpack_from("<I", data, at)[0] for at in range(0, len(data), 26)}
    # the US font's signs (assets.cpp fontCode) and the arrows it draws itself
    have |= {0x00D7, 0x2026, 0x2606, 0x25CB, 0x25A1, 0x25B3, 0x2190, 0x2191, 0x2192, 0x2193}
    return have


# ---- commands ----------------------------------------------------------------------------------

def cmd_extract(args) -> int:
    found = extract()
    entries = []
    head = Entry()
    head.msgstr = "Content-Type: text/plain; charset=UTF-8\nLanguage: \nX-Language-Name: \n"
    entries.append(head)
    for msgid, where in found.items():
        e = Entry()
        e.comments = ["#: " + " ".join(sorted(set(where)))]
        if conversions(msgid):
            e.comments.append("#, c-format")
        e.msgid = msgid
        entries.append(e)
    if args.out:
        write_po(Path(args.out), entries)
        print(f"l10n: {len(found)} msgids -> {args.out}")
    else:
        for msgid in found:
            print(msgid.replace("\n", "\\n"))
    return 0


def cmd_check(args) -> int:
    paths = [Path(p) for p in args.po] or sorted(LANG_DIR.glob("*.po"))
    found = extract()
    have = known_glyphs() if args.glyphs else set()
    errors = 0
    for path in paths:
        entries = read_po(path)
        head = header_fields(entries)
        stale = untranslated = used = 0
        seen = set()
        missing_glyphs: dict[str, list[str]] = {}
        if not head.get("Language"):
            print(f"{path}: no Language in the header")
            errors += 1
        for e in entries:
            if e.msgid == "" or e.obsolete:
                continue
            if e.msgid in seen:
                print(f"{path}:{e.line}: duplicate msgid")
                errors += 1
            seen.add(e.msgid)
            why = usable(e)
            if why == "untranslated" or why == "fuzzy":
                untranslated += 1
            elif why is not None:
                print(f"{path}:{e.line}: {why}")
                errors += 1
            else:
                used += 1
            if e.msgid not in found and not any(c.startswith("#. runtime") for c in e.comments):
                stale += 1
                if args.verbose:
                    print(f"{path}:{e.line}: stale (not in the sources): {e.msgid[:60]!r}")
            if args.glyphs and why is None:
                for ch in e.msgstr:
                    if ord(ch) >= 0x20 and ord(ch) not in have and ch != "\n":
                        missing_glyphs.setdefault(ch, []).append(str(e.line))
        for ch, lines in sorted(missing_glyphs.items()):
            print(f"{path}: no glyph for {ch} (U+{ord(ch):04X}), lines {', '.join(lines[:5])}")
            errors += 1
        new = sum(1 for m in found if m not in seen)
        print(f"l10n {path.name}: {used} translated, {untranslated} untranslated, {new} new in the sources, "
              f"{stale} stale, {errors} errors")
    return errors


def cmd_update(args) -> int:
    path = Path(args.po)
    entries = read_po(path)
    found = extract()
    by_id = {e.msgid: e for e in entries if e.msgid != ""}
    out = [e for e in entries if e.msgid == ""]
    for msgid, where in found.items():
        e = by_id.pop(msgid, None)
        if e is None:
            e = Entry()
            e.msgid = msgid
        e.obsolete = False
        e.comments = [c for c in e.comments if not c.startswith("#:")]
        e.comments.insert(0, "#: " + " ".join(sorted(set(where))))
        if conversions(msgid) and not any(c.startswith("#,") and "c-format" in c for c in e.comments):
            e.comments.append("#, c-format")
        out.append(e)
    for e in by_id.values():
        if e.msgstr:
            e.obsolete = True
            e.comments = [c for c in e.comments if not c.startswith("#:")]
            out.append(e)
    write_po(path, out)
    print(f"l10n: {path}: {len(found)} msgids, {sum(1 for e in by_id.values() if e.msgstr)} obsolete")
    return 0


def cmd_missing(args) -> int:
    entries = read_po(Path(args.po))
    done = {e.msgid for e in entries if usable(e) is None}
    for msgid in extract():
        if msgid not in done:
            print(msgid.replace("\n", "\\n"))
    return 0


def cmd_chars(args) -> int:
    chars = set()
    for p in args.po:
        for e in read_po(Path(p)):
            if usable(e) is None:
                chars |= {c for c in e.msgstr if ord(c) > 0x7E}
    print("".join(sorted(chars)))
    return 0


def c_string(s: str) -> str:
    out = []
    for b in s.encode("utf-8"):
        ch = chr(b)
        if ch == "\\":
            out.append("\\\\")
        elif ch == '"':
            out.append('\\"')
        elif ch == "\n":
            out.append("\\n")
        elif 0x20 <= b < 0x7F and ch != "?":
            out.append(ch)
        else:
            out.append(f"\\{b:03o}")
    return '"' + "".join(out) + '"'


def cmd_embed(args) -> int:
    lines = ["// Generated by tools/l10n.py embed from port/src/ui/lang/*.po; do not edit.",
             "// The built-in interface languages (port/src/ui/l10n.cpp).", ""]
    catalogs = []
    for p in args.po:
        path = Path(p)
        entries = read_po(path)
        head = header_fields(entries)
        code = head.get("Language", path.stem)
        name = head.get("X-Language-Name", code)
        pairs = [(e.msgid, e.msgstr) for e in entries if e.msgid and usable(e) is None]
        ident = "l10n_" + re.sub(r"\W", "_", code)
        lines.append(f"static const L10nPair {ident}[] = {{")
        for k, v in pairs:
            lines.append(f"    {{{c_string(k)},\n     {c_string(v)}}},")
        lines.append("};")
        catalogs.append((code, name, ident, len(pairs)))
    lines.append("static const L10nBuiltin l10nBuiltins[] = {")
    for code, name, ident, n in catalogs:
        lines.append(f"    {{{c_string(code)}, {c_string(name)}, {ident}, {n}}},")
    lines.append("    {nullptr, nullptr, nullptr, 0},")
    lines.append("};")
    text = "\n".join(lines) + "\n"
    out = Path(args.out)
    out.parent.mkdir(parents=True, exist_ok=True)
    if not out.exists() or out.read_text(encoding="utf-8") != text:
        out.write_text(text, encoding="utf-8")
    return 0


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    p = sub.add_parser("extract")
    p.add_argument("--out")
    p.set_defaults(fn=cmd_extract)
    p = sub.add_parser("check")
    p.add_argument("po", nargs="*")
    p.add_argument("--glyphs", action="store_true", help="every character must have a glyph")
    p.add_argument("-v", "--verbose", action="store_true")
    p.set_defaults(fn=cmd_check)
    p = sub.add_parser("update")
    p.add_argument("po")
    p.set_defaults(fn=cmd_update)
    p = sub.add_parser("missing")
    p.add_argument("po")
    p.set_defaults(fn=cmd_missing)
    p = sub.add_parser("chars")
    p.add_argument("po", nargs="+")
    p.set_defaults(fn=cmd_chars)
    p = sub.add_parser("embed")
    p.add_argument("out")
    p.add_argument("po", nargs="*")
    p.set_defaults(fn=cmd_embed)
    args = ap.parse_args()
    return args.fn(args)


if __name__ == "__main__":
    sys.exit(main())
