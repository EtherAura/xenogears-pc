#!/usr/bin/env python3
"""Find literal sizes that cover a type which grows on the PC (PC port, xg-htn.18).

    python tools/heapsizes.py                    # US: findings that need a look
    python tools/heapsizes.py --all              # every call site, OK ones too
    python tools/heapsizes.py --untyped          # also literal sizes with no typed destination
    python tools/heapsizes.py --ver jp
    python tools/heapsizes.py --modules battle battling
    python tools/heapsizes.py --files src/menu/m_801C511C.c
    python tools/heapsizes.py --markdown out.md  # the listed findings as a table
    python tools/heapsizes.py --wrappers         # the allocators found, with their size argument

The PC port compiles the game C with native 64-bit layouts. A size written as a
number is the PS1 size of whatever it covers; where that type holds a pointer
or an ordering-table tag, the PC needs more, and the block (or the clear, or
the copy) silently falls short. This lists every call of

  - a heap allocator: allocateHeap, allocateBattleHeap,
    allocateFromSoundArena[Top], and every function that passes one of its
    parameters on as an allocator's size, plus a constant or a sizeof
    (allocateTaskPair, allocateUpdateTask: size + sizeof(TaskHeader), ...),
    found in the AST and followed to a fixed point;
  - a byte function: bzero, memset, memcpy, memmove, bcopy;

with:

  - the size expression: a literal, an expression with literals (n * 0x2C),
    sizeof-based (fine), or computed (from a file header and the like);
  - the type it covers: for an allocator, the pointer type its result is
    converted to (cast, assignment, initialisation, return); when the result
    goes to a void*/byte pointer (or the allocator's own header type), the
    types that variable or field is cast to elsewhere in the file; for the
    byte functions, the destination's type (the whole array when it decays);
  - that type's size in the PS1 layout (-m32 -DXG_PS1_LAYOUT, as
    tools/pc_link.py probes it) and in the PC layout (x86-64).

Each site gets a verdict:

  grows      the PS1 bytes (literal, plus what the allocator adds) are k * the
             type's PS1 size and the PC size is bigger: write the size with
             sizeof, as the suggestion says; on the PS1 it is the same
             constant (check with ninja)
  partial    the literal is less than the PS1 size of a type that grows: a
             prefix of the struct (offsetof), or the variable reused
  mismatch   not a multiple of the PS1 size of a type that grows: header plus
             data, a disc format, several structs, or a reused variable
  disc       mallocAndDecompress (sized by the data) read as a type that differs
             on the PC: a disc format to convert at load, not a literal to fix
  untyped    a literal size with no typed destination (--untyped / --all)
  ok         sizeof-based, computed, or a type with the same size on both

The verdict is a lead, not a proof: a reused local variable or a cast picked
up elsewhere in the file can name the wrong type. Read each one.

Clang's JSON AST (-Xclang -ast-dump=json) of each file, compiled for the PC
(PLATFORM_PC), gives the calls and the types; sizes come from probes compiled
after the file (`char x[sizeof(T)]`), once per layout.
"""

from __future__ import annotations

import argparse
import concurrent.futures
import json
import os
import re
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
EXCLUDE = ("src/main/psyq/", "src/video_player/psyq/")
CLANG_FLAGS = ["-fsyntax-only", "-std=gnu89", "-nostdinc", "-fno-builtin", "-D_LANGUAGE_C", "-DPLATFORM_PC",
               "-Iinclude", "-Wno-everything", "-ferror-limit=0"]
PROBE_FLAGS = ["-std=gnu89", "-nostdinc", "-fno-builtin", "-funsigned-char", "-fwrapv", "-fno-strict-aliasing",
               "-fcommon", "-fpermissive", "-w", "-D_LANGUAGE_C", "-DPLATFORM_PC", "-Iinclude"]
LAYOUTS = {"pc": ["-m64"], "ps1": ["-m32", "-DXG_PS1_LAYOUT"]}

# callee -> {"size": size argument, "dest": destination argument (None: the result),
#            "add": what the callee adds to the size ([int | "sizeof:T"]), "header": its result's pointee}
BASE_CALLS = {
    "allocateHeap": {"size": 0, "dest": None, "add": [], "header": None},
    "allocateBattleHeap": {"size": 0, "dest": None, "add": [], "header": None},
    "allocateFromSoundArena": {"size": 0, "dest": None, "add": [], "header": None},
    "allocateFromSoundArenaTop": {"size": 0, "dest": None, "add": [], "header": None},
    # Sized by the data: listed when the result is read as a type that differs on the PC (a disc format
    # to convert at load, include/pc_convert.h), never as a literal to fix.
    "mallocAndDecompress": {"size": None, "dest": None, "add": [], "header": None},
    "bzero": {"size": 1, "dest": 0, "add": [], "header": None},
    "memset": {"size": 2, "dest": 0, "add": [], "header": None},
    "memcpy": {"size": 2, "dest": 0, "add": [], "header": None},
    "__builtin_memcpy": {"size": 2, "dest": 0, "add": [], "header": None},
    "memmove": {"size": 2, "dest": 0, "add": [], "header": None},
    "bcopy": {"size": 2, "dest": 1, "add": [], "header": None},
}
WRAPPED = {"ImplicitCastExpr", "ParenExpr", "ConstantExpr", "FullExpr", "ExprWithCleanups"}
# Pointer targets that say nothing about the layout.
BYTE_TYPES = {"void", "u8", "s8", "char", "unsigned char", "signed char", "const void", "const u8", "const char",
              "volatile void", "volatile u8"}


def sources(files: list[str] | None, modules: list[str] | None) -> list[Path]:
    if files:
        return [Path(f) for f in files]
    out = []
    for p in sorted((ROOT / "src").rglob("*.c")):
        rel = p.relative_to(ROOT)
        if str(rel).startswith(EXCLUDE):
            continue
        if modules and rel.parts[1] not in modules:
            continue
        out.append(rel)
    return out


def pointee(qt: str | None) -> str | None:
    """'Foo *' -> 'Foo', 'u8 (*)[16]' -> 'u8 [16]'; None when not a plain pointer."""
    if not qt:
        return None
    qt = qt.strip()
    m = re.fullmatch(r"(.*?)\s*\(\*\)\s*(\[.*\])", qt)
    if m:
        return f"{m.group(1)} {m.group(2)}"
    if "(" in qt:
        return None  # function pointer
    m = re.fullmatch(r"(.*\S)\s*\*\s*(?:const|volatile|restrict)?", qt)
    return m.group(1).strip() if m else None


def bare(t: str) -> str:
    return re.sub(r"\b(struct|union|const|volatile)\s+", "", t).strip()


def is_byte(t: str | None, header: str | None = None) -> bool:
    if t is None:
        return True
    b = bare(t)
    return b in BYTE_TYPES or (header is not None and b == bare(header))


def probe_ok(t: str) -> bool:
    return "(unnamed" not in t and "(anonymous" not in t and "<" not in t


def return_pointee(fn_type: str | None) -> str | None:
    """'TaskHeader *(TaskHeader *, s32)' -> 'TaskHeader'."""
    if not fn_type or "(" not in fn_type:
        return None
    return pointee(fn_type[:fn_type.index("(")])


class Unit:
    """One file's AST with parents, enclosing functions, parameters and source lines."""

    def __init__(self, path: Path, ast: dict):
        self.path = str(path)
        self.text = (ROOT / path).read_bytes()
        self.parent: dict[int, dict] = {}
        self.func: dict[int, dict] = {}     # node -> enclosing FunctionDecl (with a body)
        self.params: dict[int, dict[str, int]] = {}  # FunctionDecl -> {ParmVarDecl id: index}
        self.file_of: dict[int, str] = {}   # node -> file of its begin location
        self.nodes: list[dict] = []
        self.uses: dict[tuple[str, str], list[dict]] = {}  # ('decl'|'member', id) -> DeclRefExpr/MemberExpr
        self._file = None
        self._walk(ast, None, None)

    def _track(self, loc: dict):
        """Replay clang's location elision: 'file' appears only when it changes."""
        if "spellingLoc" in loc or "expansionLoc" in loc:
            for k in ("spellingLoc", "expansionLoc"):
                if k in loc:
                    self._track(loc[k])
            return
        if "offset" not in loc:
            return
        if "file" in loc:
            self._file = loc["file"]
        loc["_file"] = self._file

    def _walk(self, n: dict, parent: dict | None, func: dict | None):
        if "loc" in n:
            self._track(n["loc"])
        rng = n.get("range")
        if rng:
            for k in ("begin", "end"):
                if k in rng:
                    self._track(rng[k])
        key = id(n)
        if parent is not None:
            self.parent[key] = parent
        inner = [c for c in n.get("inner", []) if isinstance(c, dict) and c]
        if n.get("kind") == "FunctionDecl" and any(c.get("kind") == "CompoundStmt" for c in inner):
            func = n
            self.params[key] = {c["id"]: i for i, c in enumerate(c for c in inner if c.get("kind") == "ParmVarDecl")}
        if func is not None:
            self.func[key] = func
        begin = (rng or {}).get("begin", {})
        begin = begin.get("expansionLoc", begin)
        self.file_of[key] = begin.get("_file")
        self.nodes.append(n)
        if n.get("kind") == "DeclRefExpr" and "referencedDecl" in n:
            self.uses.setdefault(("decl", n["referencedDecl"]["id"]), []).append(n)
        elif n.get("kind") == "MemberExpr" and "referencedMemberDecl" in n:
            self.uses.setdefault(("member", n["referencedMemberDecl"]), []).append(n)
        for c in inner:
            self._walk(c, n, func)

    def line_of(self, n: dict) -> int:
        b = n["range"]["begin"]
        b = b.get("expansionLoc", b)
        return self.text.count(b"\n", 0, b["offset"]) + 1

    def source_line(self, line: int) -> str:
        lines = self.text.split(b"\n")
        return lines[line - 1].decode("latin-1").strip() if 0 < line <= len(lines) else ""


def strip(n: dict) -> dict:
    while n.get("kind") in WRAPPED and n.get("inner"):
        n = n["inner"][0]
    return n


def callee_name(call: dict) -> str | None:
    c = strip(call["inner"][0])
    if c.get("kind") == "DeclRefExpr":
        return c.get("referencedDecl", {}).get("name")
    return None


def qtype(n: dict) -> str | None:
    t = n.get("type")
    return t.get("qualType") if t else None


def eval_const(n: dict) -> int | None:
    """Value of an integer constant expression made of literals and + - * << | ."""
    n = strip(n)
    k = n.get("kind")
    if k == "IntegerLiteral":
        return int(n["value"])
    if k == "CStyleCastExpr" and n.get("inner"):
        return eval_const(n["inner"][0])
    if k == "BinaryOperator" and len(n.get("inner", [])) == 2:
        a, b = (eval_const(x) for x in n["inner"])
        if a is None or b is None:
            return None
        return {"+": a + b, "-": a - b, "*": a * b, "<<": a << b, "|": a | b}.get(n.get("opcode"))
    return None


def size_expr(n: dict) -> tuple[str, list[int]]:
    """('literal'|'expr'|'sizeof'|'computed', literal values)."""
    lits: list[int] = []
    has_sizeof = False
    has_other = False

    def walk(x: dict):
        nonlocal has_sizeof, has_other
        k = x.get("kind")
        if k == "IntegerLiteral":
            lits.append(int(x["value"]))
        elif k == "UnaryExprOrTypeTraitExpr":
            has_sizeof = True
        elif k in ("DeclRefExpr", "MemberExpr", "CallExpr", "ArraySubscriptExpr"):
            if k != "DeclRefExpr" or x.get("referencedDecl", {}).get("kind") != "EnumConstantDecl":
                has_other = True
                return
        for c in x.get("inner", []):
            if isinstance(c, dict):
                walk(c)

    walk(n)
    if has_sizeof:
        return "sizeof", lits
    if not lits:
        return "computed", lits
    if not has_other:
        v = eval_const(n)
        return "literal", [v] if v is not None else lits
    return "expr", lits


def alloc_targets(u: Unit, call: dict, header: str | None) -> list[str]:
    """Pointer types the allocator's result is converted to, nearest first."""
    out: list[str] = []
    n = call
    while True:
        p = u.parent.get(id(n))
        if p is None:
            break
        k = p.get("kind")
        if k in ("ImplicitCastExpr", "CStyleCastExpr"):
            t = qtype(p)
            if t and pointee(t) is not None:
                out.append(t)
                if k == "CStyleCastExpr" and not is_byte(pointee(t), header):
                    break
            elif k == "CStyleCastExpr":
                out.append(t or "?")
                break
            n = p
            continue
        if k == "ParenExpr":
            n = p
            continue
        if k == "BinaryOperator" and p.get("opcode") == "=":
            lhs = p["inner"][0]
            if lhs is not n and qtype(lhs):
                out.append(qtype(lhs))
                out.extend(retyped(u, lhs, header))
            break
        if k == "VarDecl":
            if qtype(p):
                out.append(qtype(p))
                out.extend(retyped(u, p, header, decl=True))
            break
        break
    return out


def retyped(u: Unit, n: dict, header: str | None, decl: bool = False) -> list[str]:
    """The pointer types a void*/byte-pointer variable or field is cast to elsewhere in the file."""
    if not is_byte(pointee(qtype(n)), header):
        return []
    n = strip(n)
    if decl:
        key = ("decl", n["id"])
    elif n.get("kind") == "DeclRefExpr":
        key = ("decl", n["referencedDecl"]["id"])
    elif n.get("kind") == "MemberExpr":
        key = ("member", n.get("referencedMemberDecl"))
    else:
        return []
    found = []
    for use in u.uses.get(key, []):
        x = use
        while True:
            p = u.parent.get(id(x))
            if p is None:
                break
            k = p.get("kind")
            if k in ("ImplicitCastExpr", "ParenExpr"):
                t = qtype(p)
                if k == "ImplicitCastExpr" and p.get("castKind") == "BitCast" and not is_byte(pointee(t), header):
                    found.append(t)
                    break
                x = p
                continue
            if k == "CStyleCastExpr":
                t = qtype(p)
                if pointee(t) is not None and not is_byte(pointee(t), header):
                    found.append(t)
                break
            if k == "BinaryOperator" and p.get("opcode") == "=" and p["inner"][1] is x:
                t = qtype(p["inner"][0])
                if pointee(t) is not None and not is_byte(pointee(t), header):
                    found.append(t)
                break
            break
    return list(dict.fromkeys(found))


def dest_types(u: Unit, arg: dict) -> list[str]:
    """Types a byte function's destination covers: the pointer's target, and the whole array when one decays."""
    out = []
    x = arg
    while True:
        k = x.get("kind")
        if k in ("ImplicitCastExpr", "CStyleCastExpr", "ParenExpr") and x.get("inner"):
            inner = x["inner"][0]
            if k == "ImplicitCastExpr" and x.get("castKind") == "ArrayToPointerDecay":
                if qtype(inner):
                    out.append(f"@array {qtype(inner)}")
                out.append(qtype(x))
                break
            if k in ("ParenExpr", "ImplicitCastExpr") or is_byte(pointee(qtype(x))):
                x = inner
                continue
            out.append(qtype(x))
            break
        t = qtype(x)
        if t:
            out.append(t)
            if is_byte(pointee(t)):
                out.extend(retyped(u, x, None))
        break
    return [t for t in out if t]


def wrapper_of(u: Unit, call: dict, arg: dict, info: dict) -> tuple[str, dict] | None:
    """If the size argument is a parameter of the enclosing function (plus a constant or a sizeof),
    that function is an allocator too."""
    fn = u.func.get(id(call))
    if fn is None or fn.get("name") in BASE_CALLS:
        return None
    params = u.params.get(id(fn), {})
    e = strip(arg)
    add = list(info["add"])
    if e.get("kind") == "BinaryOperator" and e.get("opcode") == "+":
        a, b = (strip(x) for x in e["inner"])
        if not (a.get("kind") == "DeclRefExpr" and a.get("referencedDecl", {}).get("id") in params):
            a, b = b, a
        if b.get("kind") == "IntegerLiteral":
            add.append(int(b["value"]))
        elif b.get("kind") == "UnaryExprOrTypeTraitExpr" and b.get("name") == "sizeof" and b.get("argType"):
            add.append("sizeof:" + b["argType"]["qualType"])
        else:
            return None
        e = a
    if e.get("kind") != "DeclRefExpr" or e.get("referencedDecl", {}).get("id") not in params:
        return None
    return fn["name"], {"size": params[e["referencedDecl"]["id"]], "dest": None, "add": add,
                        "header": return_pointee(qtype(fn)), "file": u.path}


def analyse(path: Path, ver: str, clang: str, calls: dict) -> tuple[list[dict], dict]:
    sys.setrecursionlimit(100000)
    text = (ROOT / path).read_text(errors="replace")
    if not re.search(r"\b(" + "|".join(map(re.escape, calls)) + r")\s*\(", text):
        return [], {}
    cmd = [clang, "-target", "x86_64-linux-gnu", *CLANG_FLAGS, f"-DVERSION_{ver.upper()}", "-Xclang",
           "-ast-dump=json", str(path)]
    p = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True)
    if not p.stdout:
        return [{"file": str(path), "error": p.stderr[-500:]}], {}
    u = Unit(path, json.loads(p.stdout))
    out, wrappers = [], {}
    for n in u.nodes:
        if n.get("kind") != "CallExpr" or not n.get("inner"):
            continue
        name = callee_name(n)
        info = calls.get(name)
        if info is None or u.file_of.get(id(n)) != str(path):
            continue  # not a sizing call, or one in a header (an inline function or a macro body)
        args = n["inner"][1:]
        if info["size"] is None:
            arg, (kind, lits) = None, ("disc", [])
        elif len(args) <= info["size"]:
            continue
        else:
            arg = args[info["size"]]
            kind, lits = size_expr(arg)
        if info["dest"] is None:
            types = alloc_targets(u, n, info["header"])
            w = wrapper_of(u, n, arg, info) if arg is not None else None
            if w:
                old = wrappers.get(w[0])
                if old is not None and (old["size"], old["add"]) != (w[1]["size"], w[1]["add"]):
                    w[1]["add"] = ["?"]  # several paths add different amounts: judge the literal alone
                wrappers[w[0]] = w[1]
        else:
            types = dest_types(u, args[info["dest"]]) if len(args) > info["dest"] else []
        line = u.line_of(n)
        fn = u.func.get(id(n))
        out.append({"file": str(path), "line": line, "func": fn.get("name") if fn else "?", "callee": name,
                    "alloc": info["dest"] is None, "add": info["add"], "header": info["header"], "kind": kind,
                    "lits": lits, "types": types, "src": u.source_line(line)})
    return out, wrappers


def probe(path: str, types: list[str], ver: str, cc: str) -> dict[str, dict[str, int]]:
    """type -> {'pc': n, 'ps1': n} from sizeof probes compiled after the file."""
    result: dict[str, dict[str, int]] = {t: {} for t in types}
    for layout, flags in LAYOUTS.items():
        names = [t for t in types if probe_ok(t)]
        with tempfile.TemporaryDirectory() as tmp:
            while names:
                lines = [f'#include "{ROOT / path}"']
                for i, t in enumerate(names):
                    lines.append(f"char xg_probe_{i}[sizeof({t})];")
                c, o = Path(tmp) / "probe.c", Path(tmp) / "probe.o"
                c.write_text("\n".join(lines) + "\n")
                r = subprocess.run([cc, "-c", *PROBE_FLAGS, *flags, f"-DVERSION_{ver.upper()}", str(c), "-o", str(o)],
                                   cwd=ROOT, capture_output=True, text=True)
                if r.returncode != 0:
                    bad = {int(m.group(1)) - 2 for m in re.finditer(r"probe\.c:(\d+):\d+: error", r.stderr)}
                    if not bad:
                        break
                    names = [t for i, t in enumerate(names) if i not in bad]
                    continue
                nm = subprocess.run(["nm", "-S", str(o)], capture_output=True, text=True).stdout
                for m in re.finditer(r"^\S+ (\S+) [BbCcDd] xg_probe_(\d+)$", nm, re.M):
                    result[names[int(m.group(2))]][layout] = int(m.group(1), 16)
                break
    return result


def size_types(site: dict) -> list[tuple[str, str]]:
    """(label, type to probe): the target of each pointer type, and arrays as wholes."""
    out = []
    for t in site["types"]:
        if t.startswith("@array "):
            out.append((t[7:], t[7:]))
        else:
            pt = pointee(t)
            if pt is not None and not is_byte(pt, site.get("header")):
                out.append((pt, pt))
    return list(dict.fromkeys(out))


def added(site: dict, sizes: dict[str, dict[str, int]], layout: str) -> int | None:
    total = 0
    for a in site["add"]:
        if a == "?":
            return None
        if isinstance(a, int):
            total += a
        else:
            v = sizes.get(a[7:], {}).get(layout)
            if v is None:
                return None
            total += v
    return total


def judge(site: dict, sizes: dict[str, dict[str, int]]) -> None:
    site.update(verdict="ok", type=None, ps1=None, pc=None, k=None, lit=None)
    cands = [(lab, sizes.get(t, {})) for lab, t in size_types(site)]
    cands = [(lab, s) for lab, s in cands if s.get("ps1") and s.get("pc")]
    if site["kind"] in ("sizeof", "computed", "disc"):
        if cands:
            site["type"], site["ps1"], site["pc"] = cands[0][0], cands[0][1]["ps1"], cands[0][1]["pc"]
            if site["kind"] == "disc" and any(s["pc"] != s["ps1"] for _, s in cands):
                site["verdict"] = "disc"
        return
    add_ps1, add_pc = added(site, sizes, "ps1"), added(site, sizes, "pc")
    if site["kind"] != "literal" or add_ps1 is None:
        add_ps1 = add_pc = 0
    lits = site["lits"]
    # The literal that stands for the size: the whole constant, else a literal of the expression
    # (n * 0x2C, 0x10 + n * 4 ...) that is a multiple of a candidate's PS1 size, the largest first.
    for lab, s in cands:
        for lit in sorted(set(lits), reverse=True):
            eff = lit + add_ps1
            if lit > 0 and eff % s["ps1"] == 0:
                k = eff // s["ps1"]
                site.update(type=lab, ps1=s["ps1"], pc=s["pc"], k=k, lit=lit)
                site["verdict"] = "grows" if k * s["pc"] > lit + add_pc else "ok"
                return
    site["lit"] = max(lits)
    if cands:
        lab, s = cands[0]
        site.update(type=lab, ps1=s["ps1"], pc=s["pc"])
        if s["pc"] == s["ps1"]:
            site["verdict"] = "ok"
        elif site["lit"] + add_ps1 < s["ps1"]:
            site["verdict"] = "partial"
        else:
            site["verdict"] = "mismatch"
        return
    site["verdict"] = "untyped"


def suggestion(site: dict) -> str:
    if site["verdict"] == "grows":
        t = site["type"]
        sz = f"(s32)sizeof({t})" if site["k"] == 1 else f"{site['k']} * (s32)sizeof({t})"
        for a in site["add"]:
            if a != "?":
                sz += f" - {a}" if isinstance(a, int) else f" - (s32)sizeof({a[7:]})"
        return sz if site["kind"] == "literal" else f"0x{site['lit']:X} -> {sz}"
    if site["verdict"] == "partial":
        return "a prefix (offsetof), or a reused variable"
    if site["verdict"] == "mismatch":
        return "read: header + data, several structs, a disc format, or a reused variable"
    if site["verdict"] == "disc":
        return "disc data read as a type that differs on the PC: convert at load"
    return ""


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--ver", default="us", choices=["us", "jp"])
    ap.add_argument("--files", nargs="*")
    ap.add_argument("--modules", nargs="*")
    ap.add_argument("--all", action="store_true", help="every call site")
    ap.add_argument("--untyped", action="store_true", help="also list literal sizes with no typed destination")
    ap.add_argument("--wrappers", action="store_true", help="list the allocators found")
    ap.add_argument("--markdown", type=Path, help="write the listed findings as a Markdown table")
    ap.add_argument("--json", type=Path, help="write every site as JSON")
    ap.add_argument("--clang", default=os.environ.get("CLANG", "clang"))
    ap.add_argument("--cc", default=os.environ.get("CC", "gcc"))
    args = ap.parse_args()

    # Wrappers are found over every file (a module may call another's allocator), sites only in the chosen ones.
    every = sources(None, None)
    chosen = {str(f) for f in sources(args.files, args.modules)}
    calls = dict(BASE_CALLS)
    with concurrent.futures.ProcessPoolExecutor(max_workers=min(16, os.cpu_count() or 4)) as pool:
        for _ in range(6):
            results = list(pool.map(analyse, every, [args.ver] * len(every), [args.clang] * len(every),
                                    [calls] * len(every)))
            found = {}
            for _, w in results:
                for name, info in w.items():
                    if name not in calls:
                        found[name] = info
            if not found:
                break
            calls.update(found)
    if args.wrappers:
        for name, info in sorted(calls.items()):
            if name not in BASE_CALLS:
                print(f"{name}: size argument {info['size']}, adds {info['add'] or 0}, result {info['header']} "
                      f"({info['file']})")
    sites = [s for r, _ in results for s in r if s["file"] in chosen]
    errors = [s for s in sites if "error" in s]
    sites = [s for s in sites if "error" not in s]

    by_file: dict[str, list[str]] = {}
    for s in sites:
        ts = by_file.setdefault(s["file"], [])
        ts += [t for _, t in size_types(s)] + [a[7:] for a in s["add"] if isinstance(a, str) and a != "?"]
    with concurrent.futures.ThreadPoolExecutor(max_workers=os.cpu_count()) as pool:
        futs = {f: pool.submit(probe, f, list(dict.fromkeys(ts)), args.ver, args.cc) for f, ts in by_file.items() if ts}
        sizes = {f: fut.result() for f, fut in futs.items()}
    for s in sites:
        judge(s, sizes.get(s["file"], {}))

    show = {"grows", "partial", "mismatch", "disc"} | ({"untyped"} if args.untyped else set())
    listed = [s for s in sites if args.all or s["verdict"] in show]
    for s in listed:
        size = f"0x{s['lit']:X}" if s.get("lit") is not None else s["kind"]
        tsz = f"{s['type']} ps1 0x{s['ps1']:X} pc 0x{s['pc']:X}" if s["type"] else ",".join(s["types"]) or "-"
        sug = suggestion(s)
        print(f"{s['file']}:{s['line']}: [{s['verdict']}] {s['func']}: {s['callee']}({size}) {tsz}"
              + (f"  => {sug}" if sug else ""))
    for e in errors:
        print(f"{e['file']}: clang failed: {e['error']}", file=sys.stderr)

    counts: dict[str, int] = {}
    for s in sites:
        counts[s["verdict"]] = counts.get(s["verdict"], 0) + 1
    alloc = [s for s in sites if s["alloc"]]
    lit_alloc = [s for s in alloc if s["kind"] in ("literal", "expr")]
    alloc = [s for s in alloc if s["kind"] != "disc"]
    print(f"\nheapsizes {args.ver.upper()}: {len(chosen)} files, {len(sites)} call sites "
          f"({len(alloc)} allocations through {len([c for c, i in calls.items() if i['dest'] is None and i['size'] is not None])}"
          f" allocators, {len(lit_alloc)} with literal sizes); "
          + ", ".join(f"{k} {v}" for k, v in sorted(counts.items())))

    if args.markdown:
        rows = ["| file:line | function | call | literal | type | PS1 | PC | verdict | suggested size |",
                "|---|---|---|---|---|---|---|---|---|"]
        for s in listed:
            size = f"0x{s['lit']:X}" if s.get("lit") is not None else s["kind"]
            rows.append(f"| {s['file']}:{s['line']} | {s['func']} | {s['callee']} | {size} | "
                        f"{s['type'] or ', '.join(s['types']) or '-'} | "
                        f"{hex(s['ps1']) if s['ps1'] else '-'} | {hex(s['pc']) if s['pc'] else '-'} | "
                        f"{s['verdict']} | {suggestion(s)} |")
        args.markdown.write_text("\n".join(rows) + "\n")
    if args.json:
        args.json.write_text(json.dumps(sites, indent=1))
    return 0


if __name__ == "__main__":
    sys.exit(main())
