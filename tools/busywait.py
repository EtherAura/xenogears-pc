#!/usr/bin/env python3
"""Find game loops that can wait for an interrupt without a safe point (PC port gate).

On the PS1 the game's interrupts (vsync, CD, the sound tick, DMA and drawing
completion) run while a loop spins. On the PC they are delivered only at safe
points (port/src/system/hw.h): a platform function that calls hwSafePoint, or
PORT_YIELD() in a game loop. A game loop that waits for state an interrupt
changes, and reaches no safe point in its body or in anything it calls, hangs
the PC for good.

This reads clang's AST of the game C (src/ without the PsyQ copies, plus the
PC-only port/src/game; US and JP, the PLATFORM_PC branches taken) and lists
every loop (for, while, do, backward goto) that
  - can go round without a safe point: some path through its body back to
    its condition calls nothing that is sure to reach hwSafePoint, hwYield or
    portYield. Path by path, so `while (isCDBusy())` counts although
    isCDBusy calls CdDataSync on one path: not on the one that waits. Paths
    that leave the loop (break, return, goto out) need none. A function is
    sure to reach one when all its paths do; platform functions count when
    any of their paths does (their LLVM IR, from port/src: SDK entry points
    start with hwSafePoint, but LIBGTE helpers, the C library and the
    generated PORT_TODO stubs do not). A call through a function pointer
    is not sure to. `if (<loop condition>) PORT_YIELD();` last in the body
    counts for every round (it is the waiting path);
  - reads a global or memory, in its condition, its body or what they call;
  - does not clearly end on its own. Left out: loops with an exit condition
    (the loop condition, or one guarding a break, return or goto out) that
    reads a variable the loop steps (i++, n--, p = p->next, p += 4,
    p->count++, or a local computed from one), which covers counted loops and
    table or list walks; and loops with an unconditional exit.
A loop whose exit conditions read something interrupt context writes is
marked [irq: names]; [some paths] marks a loop with a call that can reach a
safe point, but not on every round. Interrupt context: the functions the game
registers as callbacks (VSyncCallback, setVsyncUserCallback,
CdReady/Sync/DataCallback, DrawSyncCallback, the sound tick's OpenEvent,
SpuSetTransferCallback, ...) and every game function they call, plus the
buffers the platform fills on its own (InitPAD, CdRead, StSetRing, ...).

Reviewed loops go in tools/busywait_allow.txt, one per line:
    <file> | <function> | <loop> | <one-line reason>
<loop> is the key this tool prints (the loop kind and its condition; #2, #3
for repeats in one function). Exit status 1 when a loop is not on the list or
a list entry matches no loop.

    python tools/busywait.py              # US + JP: loops not reviewed yet (the gate)
    python tools/busywait.py --all        # every loop found, with the list's reasons
    python tools/busywait.py --bounded    # also bounded loops that poll interrupt state
                                          # (they end, by their counter, on the PC too)
    python tools/busywait.py --why NAME   # how a function reaches a safe point, path by path
    python tools/busywait.py --irq        # interrupt context: the callbacks, what they write
    python tools/busywait.py --ver us src/main/kernel.c ...   # some files only (no list check)

Checking the tool itself: --ignore-yield treats PORT_YIELD as no safe point,
so every loop fixed with it must show up, one per PORT_YIELD in src/ (29 on
29 US, 25 JP).
"""

from __future__ import annotations

import argparse
import collections
import concurrent.futures
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import pc_sources  # noqa: E402

ALLOW = ROOT / "tools" / "busywait_allow.txt"
VERSIONS = ("us", "jp")
SAFE_POINTS = {"hwSafePoint", "hwYield", "hwWaitVSync", "portYield"}
# Calls that count as a safe point although the platform's IR does not show
# one: the battling thread switch (top-level assembly in
# port/src/game/battling/thread.c) hands the game thread to the other side,
# and battlingMain reaches VSync before it resumes the battling thread.
SWITCHES = {"portBattlingSwitch"}

# Functions that register a callback the platform runs as an interrupt: the
# indices of their function arguments.
REGISTRARS = {
    "VSyncCallback": (0,), "VSyncCallbacks": (1,), "setVsyncUserCallback": (0,),
    "CdReadyCallback": (0,), "CdSyncCallback": (0,), "CdDataCallback": (0,),
    "DrawSyncCallback": (0,), "DMACallback": (1,), "InterruptCallback": (1,),
    "OpenEvent": (3,), "SpuSetTransferCallback": (0,), "SpuSetIRQCallback": (0,),
    "DecDCToutCallback": (0,), "DecDCTinCallback": (0,), "StSetStream": (3, 4),
}
# Functions that hand the platform a buffer it fills asynchronously (in
# interrupt context, or as DMA does): the indices of the buffer arguments.
BUFFERS = {
    "InitPAD": (0, 2), "PadInitDirect": (0, 1), "PadInitMtap": (0, 1), "PadSetAct": (1,),
    "StSetRing": (0,), "CdRead": (1,), "StoreImage": (1,), "SpuRead": (0,), "DecDCTout": (0,),
}

GAME_FLAGS = ["-target", "x86_64-linux-gnu", "-fsyntax-only", "-std=gnu89", "-nostdinc", "-fno-builtin",
              "-funsigned-char", "-D_LANGUAGE_C", "-DPLATFORM_PC", "-Iinclude", "-Wno-everything",
              "-Xclang", "-ast-dump=json"]
LOOP_KINDS = {"ForStmt": "for", "WhileStmt": "while", "DoStmt": "do"}
TRANSPARENT = {"ImplicitCastExpr", "ParenExpr", "CStyleCastExpr"}


# ----------------------------------------------------------------- game C --

def strip(n: dict) -> dict:
    while n.get("kind") in TRANSPARENT and n.get("inner"):
        n = n["inner"][0]
    return n


def fn_name(n: dict) -> str | None:
    """The function a callee/argument expression names directly, if any."""
    while True:
        n = strip(n)
        if n.get("kind") == "UnaryOperator" and n.get("opcode") in ("&", "*") and n.get("inner"):
            n = n["inner"][0]
            continue
        break
    if n.get("kind") == "DeclRefExpr" and n["referencedDecl"]["kind"] == "FunctionDecl":
        return n["referencedDecl"]["name"]
    return None


# "Every path reaches a safe point" as a formula over the calls made, kept as
# a DAG of interned nodes (a continuation is shared by every branch that
# leads to it): ids TRUE and FALSE, or an index into Formulas.nodes, whose
# entries are ("c", name), ("|", ids) or ("&", ids). Evaluated once every
# function's own formula is known (Version.compute).
TRUE, FALSE = -1, -2


class Formulas:
    def __init__(self):
        self.nodes: list[tuple] = []
        self.index: dict[tuple, int] = {}

    def intern(self, node: tuple) -> int:
        i = self.index.get(node)
        if i is None:
            i = self.index[node] = len(self.nodes)
            self.nodes.append(node)
        return i

    def call(self, name: str) -> int:
        return self.intern(("c", name))

    def any(self, *xs: int) -> int:
        if TRUE in xs:
            return TRUE
        xs = tuple(sorted({x for x in xs if x != FALSE}))
        return FALSE if not xs else xs[0] if len(xs) == 1 else self.intern(("|", xs))

    def all(self, *xs: int) -> int:
        if FALSE in xs:
            return FALSE
        xs = tuple(sorted({x for x in xs if x != TRUE}))
        return TRUE if not xs else xs[0] if len(xs) == 1 else self.intern(("&", xs))


def f_eval(nodes: list, root: int, atom) -> bool:
    """Evaluate a formula; atom(name) gives a call's value."""
    if root < 0:
        return root == TRUE
    order, seen, stack = [], set(), [root]
    while stack:  # the sub-DAG; children have lower ids than their parents
        i = stack.pop()
        if i in seen:
            continue
        seen.add(i)
        order.append(i)
        if nodes[i][0] != "c":
            stack.extend(x for x in nodes[i][1] if x >= 0)
    val = {TRUE: True, FALSE: False}
    for i in sorted(order):
        op, arg = nodes[i]
        if op == "c":
            val[i] = atom(arg)
        elif op == "|":
            val[i] = any(val[x] for x in arg)
        else:
            val[i] = all(val[x] for x in arg)
    return val[root]


class Acc:
    """What a piece of code does: calls, globals read and written, memory."""
    __slots__ = ("calls", "indirect", "reads", "writes", "mem", "refs", "assigned", "progress", "gprogress", "lprogress",
                 "regs", "bufs", "derive")

    def __init__(self):
        self.calls, self.reads, self.writes = set(), set(), set()
        self.refs, self.assigned, self.progress, self.gprogress = set(), set(), set(), set()
        self.lprogress = set()  # other lvalues stepped (p->count++), as text
        self.derive: dict = {}  # local -> what its assigned values are computed from (locals, globals)
        self.regs, self.bufs = set(), set()
        self.indirect = self.mem = False

    def merge(self, o: "Acc") -> None:
        for s in self.__slots__:
            v = getattr(o, s)
            if isinstance(v, set):
                getattr(self, s).update(v)
        for k, v in o.derive.items():
            self.derive.setdefault(k, set()).update(v)
        self.indirect |= o.indirect
        self.mem |= o.mem


def annotate(tu: dict) -> None:
    """Give every node its expansion position: _f file, _l line, _o and _e
    offsets. clang's JSON leaves out a location's file and line when they
    repeat the previous location written, so walk in document order."""
    state = {"file": None, "line": 0}

    def bare(loc):
        if "file" in loc:
            state["file"] = loc["file"]
        if "line" in loc:
            state["line"] = loc["line"]
        return state["file"], state["line"], loc.get("offset", -1), loc.get("tokLen", 0)

    def where(loc):
        if not loc:
            return None
        if "spellingLoc" in loc:
            bare(loc["spellingLoc"])
            return bare(loc["expansionLoc"])
        return bare(loc)

    stack = [iter([tu])]
    while stack:
        n = next(stack[-1], None)
        if n is None:
            stack.pop()
            continue
        if not isinstance(n, dict):
            continue
        children = []
        pos = {}
        for key, value in n.items():
            if key == "loc":
                w = where(value)
                if w:
                    pos["_lf"], pos["_ll"] = w[0], w[1]
            elif key == "range":
                b = where(value.get("begin"))
                e = where(value.get("end"))
                if b:
                    pos["_f"], pos["_l"], pos["_o"] = b[0], b[1], b[2]
                if e:
                    pos["_e"] = e[2] + e[3]
            elif key in ("inner", "array_filler"):
                children.extend(value)
        n.update(pos)
        if children:
            stack.append(iter(children))


class Function:
    def __init__(self, fd: dict, tu: "Unit"):
        self.name = fd["name"]
        self.tu = tu
        self.locals: set[str] = set()
        self.arrays: set[str] = set()
        self.statics: dict[str, str] = {}
        self.labels: dict[str, dict] = {}
        self.body = next(c for c in fd["inner"] if c.get("kind") == "CompoundStmt")
        for c in fd["inner"]:
            if c.get("kind") == "ParmVarDecl":
                self.locals.add(c["id"])
        self.label_scope: dict[str, dict] = {}
        for n in walk(self.body):
            for c in n.get("inner", ()):
                if isinstance(c, dict) and c.get("kind") == "LabelStmt":
                    self.label_scope[c["declId"]] = n
            k = n.get("kind")
            if k == "VarDecl":
                sc = n.get("storageClass")
                if sc == "static":
                    self.statics[n["id"]] = f"{self.name}.{n['name']}"
                elif sc != "extern":
                    self.locals.add(n["id"])
                    if "[" in n.get("type", {}).get("qualType", ""):
                        self.arrays.add(n["id"])
            elif k == "LabelStmt":
                self.labels[n["declId"]] = n
        self.acc = Acc()
        self.scan(self.body, self.acc)
        # A function body: every path to its end or a return.
        self.fn_ctx = {"brk": FALSE, "cont": FALSE, "ret": FALSE}
        self.must = self.with_labels(lambda: self.must_stmt(self.body, FALSE, self.fn_ctx))
        self.loops: list[dict] = []
        self.find_loops(self.body, [])

    # -- expressions --

    def global_name(self, ref: dict) -> str | None:
        if ref["kind"] == "ParmVarDecl" or ref["id"] in self.locals:
            return None
        return self.statics.get(ref["id"], ref["name"])

    def base_global(self, n: dict) -> str | None:
        """The global an lvalue or pointer expression is reached through."""
        while True:
            n = strip(n)
            k = n.get("kind")
            if k == "DeclRefExpr":
                ref = n["referencedDecl"]
                return self.global_name(ref) if ref["kind"] in ("VarDecl", "ParmVarDecl") else None
            if k in ("MemberExpr", "ArraySubscriptExpr", "UnaryOperator") and n.get("inner"):
                n = n["inner"][0]
            elif k == "BinaryOperator" and n.get("opcode") in ("+", "-"):
                a, b = n["inner"]
                n = a if "*" in a.get("type", {}).get("qualType", "") or "[" in a.get("type", {}).get(
                    "qualType", "") else b
            else:
                return None

    def local_root(self, n: dict) -> str | None:
        """The local variable an lvalue is part of (x, s.f, a[i] for a local array)."""
        while True:
            n = strip(n)
            k = n.get("kind")
            if k == "DeclRefExpr":
                ref = n["referencedDecl"]
                if ref["kind"] == "ParmVarDecl" or ref["id"] in self.locals:
                    return ref["id"]
                return None
            if k == "MemberExpr" and not n.get("isArrow"):
                n = n["inner"][0]
            elif k == "ArraySubscriptExpr":
                base = strip(n["inner"][0])
                if base.get("kind") == "DeclRefExpr" and base["referencedDecl"]["id"] in self.arrays:
                    n = base
                else:
                    return None
            else:
                return None

    def lvalue(self, n: dict, acc: Acc, mode: str, rhs: Acc | None = None, rhs_node: dict | None = None) -> None:
        """A write to n (mode 'w', rhs the value), or a read-modify-write ('rw': ++, +=).
        A variable written from its own value (i++, p = p->next) is stepped: progress."""
        local = self.local_root(n)
        if local is not None:
            acc.assigned.add(local)
            acc.refs.add(local)
            if mode == "rw" or (rhs and local in rhs.refs):
                acc.progress.add(local)
            elif rhs:
                acc.derive.setdefault(local, set()).update(rhs.refs | {("g", g) for g in rhs.reads})
            # indices and such inside the lvalue are reads
            self.scan_subscripts(n, acc)
            return
        s = strip(n)
        k = s.get("kind")
        if k == "DeclRefExpr" and s["referencedDecl"]["kind"] == "VarDecl":
            g = self.global_name(s["referencedDecl"])
            acc.mem = True
            acc.writes.add(g)
            if mode == "rw":
                acc.reads.add(g)
            if mode == "rw" or (rhs and g in rhs.reads):
                acc.gprogress.add(g)
            return
        if k == "MemberExpr" and not s.get("isArrow"):
            self.lvalue(s["inner"][0], acc, mode, rhs, rhs_node)
            return
        acc.mem = True
        g = self.base_global(s)
        if g:
            acc.writes.add(g)
            if mode == "rw":
                acc.reads.add(g)
        if k == "MemberExpr":
            t = self.tu.text_of(s)
            if mode == "rw" or (rhs_node is not None and re.search(
                    rf"(?<![\w.>]){re.escape(t)}(?!\w)", self.tu.text_of(rhs_node))):
                acc.lprogress.add(t)  # p->n++, p->next = p->next[-1].next
        if k == "ArraySubscriptExpr":
            base, idx = s["inner"]
            b = strip(base)
            if not (b.get("kind") == "DeclRefExpr" and "[" in b.get("type", {}).get("qualType", "")):
                self.scan(base, acc)
            self.scan(idx, acc)
        elif s.get("inner"):
            for c in s["inner"]:
                self.scan(c, acc)

    def scan_subscripts(self, n: dict, acc: Acc) -> None:
        n = strip(n)
        if n.get("kind") == "ArraySubscriptExpr":
            self.scan_subscripts(n["inner"][0], acc)
            self.scan(n["inner"][1], acc)
        elif n.get("kind") == "MemberExpr":
            self.scan_subscripts(n["inner"][0], acc)

    def scan(self, n: dict, acc: Acc) -> None:
        k = n.get("kind")
        if k is None:
            return
        inner = n.get("inner", ())
        if k == "DeclRefExpr":
            ref = n["referencedDecl"]
            if ref["kind"] in ("VarDecl", "ParmVarDecl"):
                g = self.global_name(ref)
                if g is None:
                    acc.refs.add(ref["id"])
                else:
                    acc.mem = True
                    acc.reads.add(g)
            return
        if k == "CallExpr":
            name = fn_name(inner[0])
            if name:
                acc.calls.add(name)
            else:
                acc.indirect = True
                self.scan(inner[0], acc)
            args = inner[1:]
            for i in REGISTRARS.get(name, ()):
                f = fn_name(args[i]) if i < len(args) else None
                if f:
                    acc.regs.add((name, f))
            for i in BUFFERS.get(name, ()):
                g = self.base_global(args[i]) if i < len(args) else None
                if g:
                    acc.bufs.add((name, g))
            for a in args:
                self.scan(a, acc)
            return
        if k == "BinaryOperator" and n.get("opcode") == "=":
            sub = Acc()
            self.scan(inner[1], sub)
            self.lvalue(inner[0], acc, "w", sub, inner[1])
            acc.merge(sub)
            return
        if k == "CompoundAssignOperator":
            self.lvalue(inner[0], acc, "rw")
            self.scan(inner[1], acc)
            return
        if k == "UnaryOperator":
            op = n.get("opcode")
            if op in ("++", "--"):
                self.lvalue(inner[0], acc, "rw")
                return
            if op == "*":
                acc.mem = True
        elif k == "MemberExpr" and n.get("isArrow"):
            acc.mem = True
        elif k == "ArraySubscriptExpr":
            if self.local_root(n) is None:
                acc.mem = True
        elif k == "VarDecl" and inner and n["id"] in self.locals:
            sub = Acc()
            for c in inner:
                self.scan(c, sub)
            acc.assigned.add(n["id"])
            acc.derive.setdefault(n["id"], set()).update(sub.refs | {("g", g) for g in sub.reads})
            acc.merge(sub)
            return
        elif k == "GCCAsmStmt":
            acc.mem = True
        for c in inner:
            if isinstance(c, dict):
                self.scan(c, acc)

    # -- must reach a safe point --
    #
    # must_stmt(n, k, ctx) is the formula for "every path from n on reaches a
    # safe point before it gets anywhere that matters": k is the formula of
    # what follows n, ctx the formulas of where break, continue, return and
    # goto lead (in a loop body: leaving the loop is fine, True; going round
    # again without one is not, False).

    def with_labels(self, compute):
        """compute() with gotos followed to their label's formula: a second
        pass gives backward gotos the first pass's (no safe point assumed for
        what the first pass had not seen yet)."""
        self.label_f, self.label_prev, self.label_missing = {}, {}, False
        f = compute()
        if self.label_missing:
            self.label_f, self.label_prev, self.label_missing = {}, self.label_f, False
            f = compute()
        return f

    def must_expr(self, n: dict) -> int:
        """Evaluating n reaches a safe point on every path."""
        F = self.tu.formulas
        k = n.get("kind")
        if k is None or k == "UnaryExprOrTypeTraitExpr":  # sizeof is not evaluated
            return FALSE
        inner = [c for c in n.get("inner", ()) if isinstance(c, dict)]
        if k == "CallExpr":
            name = fn_name(inner[0])
            callee = F.call(name) if name else self.must_expr(inner[0])
            return F.any(*(self.must_expr(a) for a in inner[1:]), callee)
        if k == "BinaryOperator" and n.get("opcode") in ("&&", "||"):
            return self.must_expr(inner[0])
        if k == "ConditionalOperator":
            c, t, e = inner
            return F.any(self.must_expr(c), F.all(self.must_expr(t), self.must_expr(e)))
        if k == "BinaryConditionalOperator":
            return self.must_expr(inner[0])
        if k == "StmtExpr":
            return self.must_stmt(inner[0], FALSE, self.fn_ctx)
        return F.any(*(self.must_expr(c) for c in inner))

    def must_seq(self, stmts: list, k: int, ctx: dict, tail: bool = False) -> int:
        for i in range(len(stmts) - 1, -1, -1):
            k = self.must_stmt(stmts[i], k, ctx, tail and i == len(stmts) - 1)
        return k

    def has(self, n: dict, kinds: tuple) -> bool:
        return any(x.get("kind") in kinds for x in walk(n))

    @staticmethod
    def forever(cond: dict) -> bool:
        """A loop condition that is always true: none, or a nonzero constant."""
        c = strip(cond) if cond.get("kind") else {}
        return not c or (c.get("kind") == "IntegerLiteral" and c.get("value") != "0")

    def breaks(self, body: dict) -> bool:
        """A break that leaves this loop (not one of an inner loop or switch)."""
        stack = [body]
        while stack:
            x = stack.pop()
            k = x.get("kind")
            if k == "BreakStmt":
                return True
            if k in LOOP_KINDS or k == "SwitchStmt":
                continue
            stack.extend(c for c in x.get("inner", ()) if isinstance(c, dict))
        return False

    def must_stmt(self, n: dict, k: int, ctx: dict, tail: bool = False) -> int:
        F = self.tu.formulas
        kind = n.get("kind")
        if kind is None:
            return k
        raw = n.get("inner", [])
        inner = [c for c in raw if isinstance(c, dict) and c.get("kind")]
        if kind == "CompoundStmt":
            return self.must_seq(inner, k, ctx, tail)
        if kind == "IfStmt":
            c = raw[0]
            then = self.must_stmt(raw[1], k, ctx, tail)
            other = self.must_stmt(raw[2], k, ctx, tail) if n.get("hasElse") and len(raw) > 2 else k
            if tail and ctx.get("guard") is not None and other == k and self.tu.text_of(c) == ctx["guard"]:
                # `if (<loop condition>) PORT_YIELD();` last in the body: when
                # it is false the loop ends.
                other = TRUE
            return F.any(self.must_expr(c), F.all(then, other))
        if kind in ("WhileStmt", "ForStmt"):
            if kind == "ForStmt":
                init = self.must_stmt(raw[0], FALSE, ctx) if raw[0].get("kind") else FALSE
                cond, body = raw[2], raw[4]
            else:
                init, cond, body = FALSE, raw[0], raw[-1]
            out = ctx["ret"] if self.has(body, ("ReturnStmt", "GotoStmt")) else TRUE
            if self.forever(cond) and not self.breaks(body):
                return F.any(init, out)  # what follows is never reached
            return F.any(init, self.must_expr(cond) if cond.get("kind") else FALSE, F.all(k, out))
        if kind == "DoStmt":
            body, cond = raw[0], raw[1]
            after = F.any(self.must_expr(cond), k)
            return self.must_stmt(body, after, dict(ctx, brk=k, cont=after, guard=None))
        if kind == "SwitchStmt":
            c, body = raw[0], raw[-1]
            sctx = dict(ctx, brk=k, guard=None)
            items = body.get("inner", []) if body.get("kind") == "CompoundStmt" else [body]
            items = [x for x in items if isinstance(x, dict) and x.get("kind")]
            entries, default, suffix = [], False, k
            for x in reversed(items):
                suffix = self.must_stmt(x, suffix, sctx)
                y = x
                while y.get("kind") in ("CaseStmt", "DefaultStmt", "LabelStmt"):
                    if y["kind"] != "LabelStmt":
                        entries.append(suffix)
                        default |= y["kind"] == "DefaultStmt"
                    sub = [z for z in y.get("inner", ()) if isinstance(z, dict) and z.get("kind")]
                    if not sub:
                        break
                    y = sub[-1]
            return F.any(self.must_expr(c), F.all(*entries, TRUE if default else k))
        if kind == "ReturnStmt":
            return F.any(self.must_expr(inner[0]) if inner else FALSE, ctx["ret"])
        if kind == "BreakStmt":
            return ctx["brk"]
        if kind == "ContinueStmt":
            return ctx["cont"]
        if kind == "GotoStmt":
            target = n["targetLabelDeclId"]
            label = self.labels.get(target)
            lo, hi = ctx.get("range", (0, -1))
            inside = label is not None and lo <= label.get("_o", -1) < hi
            if "range" in ctx and not inside:
                return ctx["ret"]  # out of the loop
            # On as from the label: known for a forward goto (statements are
            # read backwards); a backward one takes the previous pass's.
            if target in self.label_f:
                return self.label_f[target]
            self.label_missing = True
            return self.label_prev.get(target, FALSE)
        if kind == "LabelStmt":
            f = self.must_stmt(inner[-1], k, ctx, tail) if inner else k
            self.label_f[n["declId"]] = f
            return f
        if kind in ("CaseStmt", "DefaultStmt", "AttributedStmt"):
            return self.must_stmt(inner[-1], k, ctx, tail) if inner else k
        if kind == "DeclStmt":
            return F.any(*(self.must_expr(c) for d in inner for c in d.get("inner", ()) if isinstance(c, dict)), k)
        if kind in ("NullStmt", "GCCAsmStmt"):
            return k
        if kind == "BinaryOperator" and n.get("opcode") == "=" and ctx.get("flag"):
            lhs, rhs = strip(raw[0]), strip(raw[1])
            if (lhs.get("kind") == "DeclRefExpr" and lhs["referencedDecl"]["name"] == ctx["flag"]
                    and rhs.get("kind") == "IntegerLiteral" and rhs.get("value") == "0"):
                return TRUE  # `while (loop) { ... loop = 0; ... }`: this path ends the loop
        return F.any(self.must_expr(n), k)

    # -- loops --

    def exits(self, nodes: list, lo: int, hi: int) -> tuple[list, bool]:
        """Conditions guarding a way out of the code in nodes (a loop body):
        break (not inside an inner loop or switch), return, goto out of
        [lo, hi). Also whether some way out has no guard at all."""
        conds: list = []
        free = False

        def out(guards):
            nonlocal free
            if guards:
                conds.extend(guards)
            else:
                free = True

        def go(n, guards, in_loop, in_switch):
            k = n.get("kind")
            if k is None:
                return
            inner = n.get("inner", ())
            if k == "IfStmt":
                for c in inner[1:]:
                    go(c, guards + [inner[0]], in_loop, in_switch)
                return
            if k == "SwitchStmt":
                for c in inner[1:]:
                    go(c, guards + [inner[0]], in_loop, True)
                return
            if k in LOOP_KINDS:
                for c in inner:
                    if isinstance(c, dict):
                        go(c, guards, True, in_switch)
                return
            if k == "BreakStmt":
                if not in_loop and not in_switch:
                    out(guards)
                return
            if k == "ReturnStmt":
                out(guards)
                return
            if k == "GotoStmt":
                label = self.labels.get(n["targetLabelDeclId"])
                if label is None or not (lo <= label.get("_o", -1) < hi):
                    out(guards)
                return
            for c in inner:
                if isinstance(c, dict):
                    go(c, guards, in_loop, in_switch)

        for n in nodes:
            go(n, [], False, False)
        return conds, free

    def add_loop(self, node: dict, kind: str, cond: dict | None, body_nodes: list, step: dict | None,
                 extra_exits: list, lo: int, hi: int, label: str = "") -> None:
        # Every iteration that goes round again reaches a safe point; leaving
        # the loop (break, return, goto out) needs none.
        F = self.tu.formulas
        guard = self.tu.text_of(cond if cond else extra_exits[0]) if cond or extra_exits else None
        flag = re.fullmatch(r"(\w+)(?: != 0)?", guard or "")
        ctx = {"brk": TRUE, "cont": FALSE, "ret": TRUE, "guard": guard, "range": (lo, hi),
               "flag": flag.group(1) if flag and not flag.group(1).isdigit() else None}
        must = self.with_labels(lambda: F.any(self.must_expr(cond) if cond else FALSE,
                                              self.must_expr(step) if step else FALSE,
                                              self.must_seq(body_nodes, FALSE, ctx, tail=True)))
        cacc, bacc = Acc(), Acc()
        if cond:
            self.scan(cond, cacc)
        for b in body_nodes:
            self.scan(b, bacc)
        if step:
            self.scan(step, bacc)
        conds, free = self.exits(body_nodes, lo, hi)
        exit_conds = ([cond] if cond else []) + extra_exits + conds
        xacc = Acc()
        for c in exit_conds:
            self.scan(c, xacc)
        progress = (cacc.progress | bacc.progress) & self.locals
        gprogress = cacc.gprogress | bacc.gprogress
        # Locals computed from a stepped variable (x0 = k * 64) step too.
        derive = {}
        for a in (cacc, bacc):
            for v, src in a.derive.items():
                derive.setdefault(v, set()).update(src)
        steps = progress | {("g", g) for g in gprogress}
        # Locals that feed each other round the loop (p = next; next = p->next).
        for v in derive:
            seen, todo = set(), [x for x in derive[v] if x in derive]
            while todo:
                x = todo.pop()
                if x == v:
                    progress.add(v)
                    steps.add(v)
                    break
                if x not in seen:
                    seen.add(x)
                    todo += [y for y in derive.get(x, ()) if y in derive]
        grew = True
        while grew:
            grew = False
            for v, src in derive.items():
                if v not in progress and v in self.locals and src & steps:
                    progress.add(v)
                    steps.add(v)
                    grew = True
        # A counter in memory stepped by the loop (for (; p->i < p->n; p->i++)).
        xtext = " ".join(self.tu.text_of(c) for c in exit_conds)
        lstep = any(re.search(rf"(?<![\w.>]){re.escape(t)}(?!\w)", xtext) for t in cacc.lprogress | bacc.lprogress)
        whole = Acc()
        whole.merge(cacc)
        whole.merge(bacc)
        if kind == "goto":
            text = f"goto {label}" + (f" if ({self.tu.text_of(extra_exits[0])})" if extra_exits else "")
        else:
            text = f"{kind} ({self.tu.text_of(cond) if cond else ''})"
        self.loops.append({
            "func": self.name, "file": node.get("_f"), "line": node.get("_l"), "offset": node.get("_o"),
            "text": text, "calls": whole.calls, "indirect": whole.indirect, "mem": whole.mem, "must": must,
            "xreads": xacc.reads, "xcalls": xacc.calls, "writes": whole.writes,
            "bounded": free or bool(xacc.refs & progress) or bool(xacc.reads & gprogress) or lstep,
        })

    def find_loops(self, n: dict, guards: list) -> None:
        k = n.get("kind")
        if k is None:
            return
        inner = n.get("inner", ())
        if k in LOOP_KINDS:
            if k == "ForStmt":
                cond, step, body = inner[2], inner[3], inner[4]
            elif k == "WhileStmt":
                cond, step, body = inner[0], None, inner[-1]
            else:
                body, cond, step = inner[0], inner[1], None
            cond = cond if cond and cond.get("kind") else None
            step = step if step and step.get("kind") else None
            c = strip(cond) if cond else {}
            if c.get("kind") == "IntegerLiteral" and c.get("value") == "0":
                pass  # do { } while (0): runs once
            else:
                self.add_loop(n, LOOP_KINDS[k], cond, [body], step, [], n.get("_o", -1), n.get("_e", -1))
        if k == "GotoStmt":
            label = self.labels.get(n["targetLabelDeclId"])
            scope = self.label_scope.get(n["targetLabelDeclId"], {})
            # A loop when the goto lies after the label within the statement
            # that holds the label (not, say, from an else back into its then).
            if label is not None and label.get("_o", 1 << 60) < n.get("_o", -1) <= scope.get("_e", -1):
                lo, hi = label["_o"], n["_e"]
                own = [g for g in guards if g.get("_o", -1) >= lo]
                body = list(self.within(self.body, lo, hi))
                self.add_loop(label, "goto", None, body, None, own[-1:], lo, hi, label["name"])
        for i, c in enumerate(inner):
            if isinstance(c, dict):
                self.find_loops(c, guards + [inner[0]] if k == "IfStmt" and i > 0 else guards)

    def within(self, n: dict, lo: int, hi: int):
        """The largest nodes inside [lo, hi)."""
        o, e = n.get("_o", -1), n.get("_e", -1)
        if lo <= o and e <= hi and n is not self.body:
            yield n
            return
        if e <= lo or o >= hi:
            return
        for c in n.get("inner", ()):
            if isinstance(c, dict):
                yield from self.within(c, lo, hi)


def walk(n: dict):
    stack = [n]
    while stack:
        x = stack.pop()
        yield x
        stack.extend(c for c in reversed(x.get("inner", ())) if isinstance(c, dict))


class Unit:
    def __init__(self, path: str):
        self.path = path
        self.formulas = Formulas()
        self.texts: dict[str, str] = {}
        self.current = path

    def text_of(self, n: dict) -> str:
        f = n.get("_f") or self.current
        if f not in self.texts:
            try:
                self.texts[f] = (ROOT / f).read_text(errors="replace")
            except OSError:
                self.texts[f] = ""
        o, e = n.get("_o", -1), n.get("_e", -1)
        t = self.texts[f][o:e] if 0 <= o < e else "?"
        t = re.sub(r"/\*.*?\*/|//[^\n]*", " ", t, flags=re.S)
        return re.sub(r"\s+", " ", t).strip()


def game_unit(job: tuple) -> dict:
    """One game C file: its functions (calls, globals, loops)."""
    path, ver, module, clang = job
    sys.setrecursionlimit(100000)
    cmd = [clang, *GAME_FLAGS, f"-DVERSION_{ver.upper()}", path]
    p = subprocess.run(cmd, cwd=ROOT, capture_output=True)
    if p.returncode != 0 or not p.stdout:
        return {"path": path, "module": module, "error": p.stderr.decode(errors="replace")[-2000:]}
    tu = json.loads(p.stdout)
    annotate(tu)
    unit = Unit(path)
    funcs = {}
    loops = []
    for fd in tu.get("inner", ()):
        if fd.get("kind") != "FunctionDecl" or not any(c.get("kind") == "CompoundStmt" for c in fd.get("inner", ())):
            continue
        unit.current = fd.get("_f") or path
        f = Function(fd, unit)
        a = f.acc
        funcs[f.name] = {"calls": a.calls, "indirect": a.indirect, "reads": a.reads, "writes": a.writes, "must": f.must,
                         "mem": a.mem, "regs": a.regs, "bufs": a.bufs, "file": fd.get("_f")}
        loops.extend(f.loops)
    return {"path": path, "module": module, "funcs": funcs, "loops": loops, "nodes": unit.formulas.nodes}


# ------------------------------------------------------------- platform C --

DEFINE = re.compile(r'^define\s+([^@]*)@("[^"]*"|[\w.$-]+)\(')
CALLEE = re.compile(r'([@%]("[^"]*"|[\w.$-]+))\(')


def platform_unit(job: tuple) -> dict:
    """One platform file's call graph, from its LLVM IR (-O0: nothing inlined)."""
    path, ver, clang, cflags, stubdir = job
    cmd = [clang, "-S", "-emit-llvm", "-O0", "-o", "-", "-std=c11", "-w", "-fno-strict-aliasing",
           "-Iport/src", "-Iinclude", f"-I{stubdir}", "-DPLATFORM_PC", f"-DVERSION_{ver.upper()}",
           f'-DXG_ROOT="{ROOT}"', *cflags, path]
    for _ in range(256):  # one stand-in per missing generated header (vk.c includes dozens)
        p = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True)
        m = re.search(r"fatal error: '([^']+)' file not found", p.stderr)
        if p.returncode == 0 or not m or "/" in m.group(1):
            break
        # A generated header (the GPU's compiled shaders): an empty stand-in.
        Path(stubdir, m.group(1)).write_text("{0}\n")
    if p.returncode != 0:
        return {"path": path, "error": p.stderr[-2000:]}
    funcs: dict[str, dict] = {}
    cur = None
    for line in p.stdout.splitlines():
        if cur is None:
            m = DEFINE.match(line)
            if m:
                name = m.group(2).strip('"')
                internal = re.search(r"\b(internal|private)\b", m.group(1)) is not None
                cur = funcs.setdefault(name, {"internal": internal, "calls": set(), "indirect": False})
            continue
        if line.startswith("}"):
            cur = None
            continue
        if re.search(r"\b(call|invoke)\b", line):
            m = CALLEE.search(line[re.search(r"\b(call|invoke)\b", line).end():])
            if m:
                if m.group(1)[0] == "@":
                    name = m.group(2).strip('"')
                    if not name.startswith("llvm."):
                        cur["calls"].add(name)
                else:
                    cur["indirect"] = True
    return {"path": path, "funcs": funcs}


def platform_files() -> list[str]:
    """The platform layer's C, as port/CMakeLists.txt globs it (src/*.c, src/<dir>/*.c)."""
    cmake = (ROOT / "port" / "CMakeLists.txt").read_text()
    dirs = {m.group(1) or "" for m in re.finditer(r'\$\{CMAKE_CURRENT_SOURCE_DIR\}/src/(?:(\w+)/)?\*\.c"', cmake)}
    out = []
    for d in sorted(dirs - {"game"}):
        out += sorted(str(p.relative_to(ROOT)) for p in (ROOT / "port" / "src" / d).glob("*.c"))
    return out


def platform_reach(ver: str, clang: str, pool) -> tuple[dict, dict, list]:
    """Exported platform functions -> whether they reach a safe point, and the call graph."""
    try:
        cflags = subprocess.run(["pkg-config", "--cflags", "sdl3"], capture_output=True, text=True,
                                check=True).stdout.split()
    except (OSError, subprocess.CalledProcessError):
        cflags = []
    stubdir = tempfile.mkdtemp(prefix="busywait-")
    try:
        units = list(pool.map(platform_unit, [(f, ver, clang, cflags, stubdir) for f in platform_files()]))
    finally:
        shutil.rmtree(stubdir, ignore_errors=True)
    errors = [f"{u['path']}: {u['error']}" for u in units if "error" in u]
    graph: dict[str, set] = {}
    exported = {}
    for u in units:
        if "error" in u:
            continue
        local = {n for n, f in u["funcs"].items() if f["internal"]}
        for name, f in u["funcs"].items():
            key = f"{u['path']}::{name}" if f["internal"] else name
            calls = {f"{u['path']}::{c}" if c in local else c for c in f["calls"]}
            graph.setdefault(key, set()).update(calls)
            if not f["internal"]:
                exported[name] = key
    reach = {k: k in SAFE_POINTS for k in graph}
    for s in SAFE_POINTS:
        reach[s] = True
    changed = True
    while changed:
        changed = False
        for k, calls in graph.items():
            if not reach[k] and any(reach.get(c, False) for c in calls):
                reach[k] = changed = True
    return {name: reach[key] for name, key in exported.items()}, graph, errors


# --------------------------------------------------------------- analysis --

class Version:
    def __init__(self, ver: str, units: list, plat: dict, pgraph: dict):
        self.ver = ver
        self.plat = plat
        self.pgraph = pgraph
        self.defs: dict[tuple, dict] = {}       # (module, name) -> facts
        self.by_name = collections.defaultdict(list)
        self.loops = []
        self.nodes = [u["nodes"] for u in units]
        for ui, u in enumerate(units):
            for name, f in u["funcs"].items():
                key = (u["module"], name)
                d = self.defs.setdefault(key, {"calls": set(), "indirect": False, "reads": set(), "writes": set(),
                                               "mem": False, "regs": set(), "bufs": set(), "file": f["file"],
                                               "must": []})
                d["must"].append((ui, f["must"]))
                for s in ("calls", "reads", "writes", "regs", "bufs"):
                    d[s] |= f[s]
                d["indirect"] |= f["indirect"]
                d["mem"] |= f["mem"]
                if key not in self.by_name[name]:
                    self.by_name[name].append(key)
            for lp in u["loops"]:
                self.loops.append((u["module"], dict(lp, unit=ui)))
        self._resolved: dict[tuple, tuple] = {}
        self.edges = {k: [self.resolve(k[0], c) for c in d["calls"]] for k, d in self.defs.items()}
        self.compute()

    def resolve(self, module: str, name: str) -> tuple:
        r = self._resolved.get((module, name))
        if r is None:
            r = self._resolved[(module, name)] = self._resolve(module, name)
        return r

    def _resolve(self, module: str, name: str) -> tuple:
        if (module, name) in self.defs:
            return (module, name)
        if ("main", name) in self.defs:
            return ("main", name)
        if self.by_name.get(name):
            return self.by_name[name][0]
        if name in self.plat:
            return ("@platform", name)
        if name in SWITCHES:
            return ("@switch", name)
        return ("@stub", name)

    def compute(self) -> None:
        # Some path reaches a safe point (a call through a pointer can).
        reach = {}
        for k, d in self.defs.items():
            reach[k] = d["indirect"]

        def ext_reach(k):
            return self.plat.get(k[1], False) if k[0] == "@platform" else k[0] == "@switch"
        changed = True
        while changed:
            changed = False
            for k, callees in self.edges.items():
                if not reach[k] and any(reach[c] if c in reach else ext_reach(c) for c in callees):
                    reach[k] = changed = True
        self.reach = reach
        self.ext_reach = ext_reach
        # Every path through the function reaches a safe point: a least fixed
        # point, re-evaluating a function's callers when it becomes true.
        self.must = {k: False for k in self.defs}
        callers = collections.defaultdict(set)
        for k, callees in self.edges.items():
            for c in callees:
                callers[c].add(k)
        todo = list(self.defs)
        queued = set(todo)
        while todo:
            k = todo.pop()
            queued.discard(k)
            if self.must[k]:
                continue
            atom = self.atom(k[0])
            if all(f_eval(self.nodes[ui], root, atom) for ui, root in self.defs[k]["must"]):
                self.must[k] = True
                for c in callers[k]:
                    if c not in queued and not self.must[c]:
                        queued.add(c)
                        todo.append(c)
        mem = {k: d["mem"] or any(c[0].startswith("@") for c in self.edges[k]) for k, d in self.defs.items()}
        reads = {k: set(d["reads"]) for k, d in self.defs.items()}
        writes = {k: set(d["writes"]) for k, d in self.defs.items()}
        changed = True
        while changed:
            changed = False
            for k, callees in self.edges.items():
                for c in callees:
                    if c in self.defs:
                        if mem[c] and not mem[k]:
                            mem[k] = changed = True
                        for t in (reads, writes):
                            if not t[c] <= t[k]:
                                t[k] |= t[c]
                                changed = True
        self.mem, self.treads, self.twrites = mem, reads, writes
        # Interrupt context.
        roots = set()
        bufs = set()
        for (module, _), d in self.defs.items():
            for _, f in d["regs"]:
                roots.add(self.resolve(module, f))
            bufs |= {g for _, g in d["bufs"]}
        irq, todo = set(), [r for r in roots if r in self.defs]
        while todo:
            k = todo.pop()
            if k in irq:
                continue
            irq.add(k)
            todo += [c for c in self.edges[k] if c in self.defs]
        self.irq_funcs = irq
        self.irq_roots = roots
        self.irq_writes = set(bufs)
        for k in irq:
            self.irq_writes |= self.defs[k]["writes"]

    def atom(self, module: str):
        def call_must(name: str) -> bool:
            c = self.resolve(module, name)
            return self.must[c] if c in self.must else self.ext_reach(c)
        return call_must

    def reaches(self, k: tuple) -> bool:
        return self.reach[k] if k in self.reach else self.ext_reach(k)

    def why(self, name: str) -> list[str]:
        """A call path from name to a safe point, or why there is none."""
        out = []
        for start in self.by_name.get(name, []) or [("@platform", name)]:
            path, seen, k = [start], {start}, start
            while k in self.defs:
                nxt = next((c for c in sorted(self.edges[k]) if self.reaches(c) and c not in seen), None)
                if nxt is None:
                    if self.defs[k]["indirect"]:
                        path.append(("", "(a call through a function pointer)"))
                    break
                path.append(nxt)
                seen.add(nxt)
                k = nxt
            if k[0] == "@platform":
                pk, pseen = k[1], {k[1]}
                while pk not in SAFE_POINTS:
                    nxt = next((c for c in sorted(self.pgraph.get(pk, ())) if c not in pseen and
                                self.plat_reaches(c)), None)
                    if nxt is None:
                        break
                    path.append(("@platform", nxt))
                    pseen.add(nxt)
                    pk = nxt
            ok = self.reaches(start)
            out.append(("can reach a safe point: " if ok else "no safe point: ") +
                       " -> ".join(f"{m}:{n}" if m else n for m, n in path))
            if start in self.defs:
                must = self.must[start]
                atom = self.atom(start[0])
                for ui, root in self.defs[start]["must"]:
                    out.append(f"every path reaches one: {'yes' if must else 'no'}; paths (| any, & all): " +
                               self.show_formula(self.nodes[ui], root, atom))
        return out

    def show_formula(self, nodes: list, root: int, atom, limit: int = 2000) -> str:
        parts: list[str] = []
        size = 0

        def go(x):
            nonlocal size
            if size > limit:
                return
            if x < 0:
                t = "true" if x == TRUE else "false"
            elif nodes[x][0] == "c":
                t = f"{nodes[x][1]}={'y' if atom(nodes[x][1]) else 'n'}"
            else:
                parts.append("(")
                for j, y in enumerate(nodes[x][1]):
                    if j:
                        parts.append(f" {nodes[x][0]} ")
                    go(y)
                parts.append(")")
                return
            parts.append(t)
            size += len(t)

        go(root)
        t = "".join(parts)
        return t if size <= limit else t + " ..."

    def plat_reaches(self, key: str) -> bool:
        if key in SAFE_POINTS:
            return True
        seen, todo = set(), [key]
        while todo:
            k = todo.pop()
            if k in SAFE_POINTS:
                return True
            if k in seen:
                continue
            seen.add(k)
            todo += list(self.pgraph.get(k, ()))
        return False

    def findings(self) -> tuple[list, list]:
        busy, bounded = [], []
        seen = set()
        for module, lp in self.loops:
            ident = (lp["file"], lp["offset"])
            if ident in seen:
                continue
            seen.add(ident)
            callees = [self.resolve(module, c) for c in lp["calls"]]
            if f_eval(self.nodes[lp["unit"]], lp["must"], self.atom(module)):
                continue
            some = lp["indirect"] or any(self.reaches(c) for c in callees)
            mem = lp["mem"] or any(self.mem.get(c, c[0].startswith("@")) for c in callees)
            if not mem:
                continue
            xreads = set(lp["xreads"])
            for c in lp["xcalls"]:
                xreads |= self.treads.get(self.resolve(module, c), set())
            irq = sorted(xreads & self.irq_writes)
            writes = set(lp["writes"])
            for c in callees:
                writes |= self.twrites.get(c, set())
            item = dict(lp, module=module, irq=irq, self=sorted(xreads & writes), some=some)
            (bounded if lp["bounded"] else busy).append(item)
        return busy, bounded


# ------------------------------------------------------------------ main --

def keyed(items: list) -> list:
    """Give each loop its list key: kind + condition, #n for repeats in a function."""
    count = collections.Counter()
    for it in sorted(items, key=lambda i: (i["file"], i["offset"])):
        base = (it["file"], it["func"], it["text"])
        count[base] += 1
        it["key"] = it["text"] + (f" #{count[base]}" if count[base] > 1 else "")
    return items


def load_allow() -> dict:
    allow = {}
    if ALLOW.exists():
        for n, line in enumerate(ALLOW.read_text().splitlines(), 1):
            if not line.strip() or line.lstrip().startswith("#"):
                continue
            parts = [p.strip() for p in line.split(" | ")]
            if len(parts) < 4 or not parts[-1]:
                print(f"{ALLOW.relative_to(ROOT)}:{n}: expected '<file> | <function> | <loop> | <reason>'")
                allow[("?", str(n), "")] = ""
                continue
            # the loop's condition may hold " | " itself; the reason may not
            allow[(parts[0], parts[1], " | ".join(parts[2:-1]))] = parts[-1]
    return allow


def jobs_for(ver: str, files: list[str] | None, clang: str) -> list:
    jobs = []
    for module, paths in pc_sources.modules(ver).items():
        srcs = [str(p.relative_to(ROOT)) for p in paths]
        srcs += sorted(str(p.relative_to(ROOT)) for p in (ROOT / "port/src/game" / module).glob("*.c"))
        jobs += [(s, ver, module, clang) for s in srcs]
    if files:
        wanted = {str(Path(f).resolve().relative_to(ROOT)) for f in files}
        jobs = [j for j in jobs if j[0] in wanted]
    return jobs


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--ver", choices=VERSIONS, action="append", help="a version (default: both)")
    ap.add_argument("--all", action="store_true", help="also list reviewed loops")
    ap.add_argument("--bounded", action="store_true", help="also list bounded loops that poll interrupt state")
    ap.add_argument("--why", metavar="NAME", action="append", help="how NAME reaches a safe point")
    ap.add_argument("--irq", action="store_true", help="list the interrupt-context functions and what they write")
    ap.add_argument("--clang", default=os.environ.get("CLANG", "clang"))
    # Checks of the tool itself: PORT_YIELD as no safe point (the loops fixed
    # with it must show up); the functions called that nothing defines.
    ap.add_argument("--ignore-yield", action="store_true", help=argparse.SUPPRESS)
    ap.add_argument("--stubs", action="store_true", help=argparse.SUPPRESS)
    ap.add_argument("files", nargs="*")
    args = ap.parse_args()
    vers = args.ver or list(VERSIONS)

    results: dict[str, Version] = {}
    errors = []
    with concurrent.futures.ProcessPoolExecutor(max_workers=os.cpu_count()) as pool:
        for ver in vers:
            plat, pgraph, perr = platform_reach(ver, args.clang, pool)
            errors += perr
            if args.ignore_yield:
                plat["portYield"] = False
            units = list(pool.map(game_unit, jobs_for(ver, args.files, args.clang)))
            errors += [f"{u['path']}: {u['error']}" for u in units if "error" in u]
            results[ver] = Version(ver, [u for u in units if "error" not in u], plat, pgraph)
    for e in errors:
        print(f"error: {e}")

    if args.why:
        for ver, v in results.items():
            for name in args.why:
                for line in v.why(name):
                    print(f"{ver.upper()} {name}: {line}")
        return 0
    if args.stubs:
        for ver, v in results.items():
            ext = collections.Counter()
            for k, callees in v.edges.items():
                for c in callees:
                    if c[0] == "@stub":
                        ext[c[1]] += 1
            print(f"{ver.upper()} called but defined nowhere (pc_link stubs): " + ", ".join(sorted(ext)))
            np = sorted(n for n, r in v.plat.items() if not r)
            print(f"{ver.upper()} platform functions without a safe point: {len(np)}")
        return 0
    if args.irq:
        for ver, v in results.items():
            print(f"{ver.upper()} interrupt roots: " + ", ".join(sorted(f"{m}:{n}" for m, n in v.irq_roots)))
            print(f"{ver.upper()} interrupt context: {len(v.irq_funcs)} functions")
            for k in sorted(v.irq_funcs):
                print(f"  {k[0]}:{k[1]}  writes " + ", ".join(sorted(v.defs[k]["writes"])))
            print(f"{ver.upper()} interrupt-written: " + ", ".join(sorted(v.irq_writes)))
        return 0

    # Merge the versions: one entry per loop, with the versions it is found in.
    merged: dict[tuple, dict] = {}
    bounded: dict[tuple, dict] = {}
    for ver, v in results.items():
        busy, bnd = v.findings()
        for target, items in ((merged, busy), (bounded, bnd)):
            for it in keyed(items):
                k = (it["file"], it["func"], it["key"])
                if k in target:
                    target[k]["vers"].append(ver)
                    target[k]["irq"] = sorted(set(target[k]["irq"]) | set(it["irq"]))
                else:
                    target[k] = dict(it, vers=[ver])

    allow = load_allow()
    new = [m for k, m in merged.items() if k not in allow]
    stale = [k for k in allow if k not in merged] if not args.files and set(vers) == set(VERSIONS) else []

    def show(it, reason=None):
        tag = f" [irq: {', '.join(it['irq'])}]" if it["irq"] else ""
        tag += f" [loop writes: {', '.join(it['self'])}]" if it["self"] else ""
        tag += " [some paths]" if it["some"] else ""
        vs = "" if len(it["vers"]) == len(vers) else f" ({'/'.join(x.upper() for x in it['vers'])} only)"
        print(f"{it['file']}:{it['line']}: {it['func']} | {it['key']}{tag}{vs}")
        if reason:
            print(f"    reviewed: {reason}")

    for k, it in sorted(merged.items(), key=lambda kv: (kv[1]["file"], kv[1]["line"])):
        if k not in allow:
            show(it)
        elif args.all:
            show(it, allow[k])
    if args.bounded:
        print("bounded loops that poll interrupt state (they end by their own count):")
        for k, it in sorted(bounded.items(), key=lambda kv: (kv[1]["file"], kv[1]["line"])):
            if it["irq"]:
                show(it)
    for k in stale:
        print(f"{ALLOW.relative_to(ROOT)}: no such loop any more: {' | '.join(k)}")
    nloops = " + ".join(f"{len({(lp['file'], lp['offset']) for _, lp in v.loops})} {ver.upper()}"
                        for ver, v in results.items())
    irq = sum(1 for m in merged.values() if m["irq"])
    none = sum(1 for m in merged.values() if not m["some"])
    print(f"busywait {'+'.join(x.upper() for x in vers)}: {nloops} loops, {len(merged)} can go round without a safe "
          f"point ({none} reach none at all; {irq} on interrupt state), {len(merged) - len(new)} reviewed, "
          f"{len(new)} not reviewed, "
          f"{len(stale)} stale list entries" + (f", {len(errors)} errors" if errors else ""))
    return 1 if new or stale or errors else 0


if __name__ == "__main__":
    sys.exit(main())
