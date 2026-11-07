#!/usr/bin/env python3
"""beetle_race.py: is a Beetle picture the GPU draw-timing race (shared primitives relinked by the
next frame's CPU work before the PS1's GPU reaches them)?

    beetle_race.py --ram-a RAM_A --ram-b RAM_B --vram VRAM --pic BEETLE_PNG --trace PORT_TRACE
            (--field | --head 0x... --head-b 0x...) [--port-frame PNG] [--out DIR] [--crop 15,10]

RAM_A: Beetle main RAM at the end of the vsync whose work built the OT drawn next (the OT is
complete, its DrawOTag comes after the next vblank); RAM_B: at the end of the following vsync
(the CPU's next frame done, while the GPU drew OT A); VRAM: Beetle's VRAM at that following
vsync's interrupt (before the DrawOTag); BEETLE_PNG: the picture that shows OT A's buffer.
The draw environment and the transfers queued before the DrawOTag come from the port's
XG_GPU_TRACE (its O line that matches OT A best, the D line before it, the C/M/L lines between).

Renders with Beetle's GPU (beetle_gpu.py, our black-box workload):
  plain: OT A as in RAM_A                      (what the port draws)
  race:  OT A walked with the primitives shared by both OTs as in RAM_B (their tags then lead
         into OT B's chain: the GPU draws the rest of OT B instead of the rest of OT A)
and counts the pixels that differ from the Beetle picture (5-bit colour)."""
import argparse
import difflib
import json
import struct
from pathlib import Path

import beetle_gpu as lr

FIELD_CUR = 0x800C426C  # pCurrentFieldRenderingContext (US)
FIELD_UIOT7 = 0x80D4 + 7 * 4
WM_RS = 0x8009BBC8  # worldmapRenderingStructs (US), 0x78 each: DRAWENV at +0, OT pointer at +0x70
BATTLE_RS = 0x800C4A20  # battleRenderStructs (US), 0x4070 each: DRAWENV at +0, OT at +0x70
MENU_CTX = 0x800625A0  # gMenuContext (US): m1D4_currentDrawContext, its m70_ot[15]


def u32(ram, a):
    return struct.unpack_from("<I", ram, a & 0x1FFFFF)[0]


def cmds(words):
    out, i = [], 0
    while i < len(words):
        n = lr.command_length(words[i] >> 24)
        if n < 0:
            j = i + 2
            while j < len(words) and (words[j] & 0xF000F000) != 0x50005000:
                j += 2 if words[i] & 0x10000000 else 1
            n = j + 1 - i
        if n <= 0 or i + n > len(words):
            raise ValueError("truncated GP0 command")
        out.append(tuple(words[i:i + n]))
        i += n
    return out


def walk(read_ram, head, limit=300000):
    """[(tag address, words)] in GPU order; read_ram(addr) -> the RAM image to read that tag from"""
    out, a = [], head
    seen = set()
    for _ in range(limit):
        ram = read_ram(a)
        key = (a, id(ram))
        if key in seen or a & 3 or (a & 0xFFFFFF) >= 0x200000:
            raise ValueError(f"invalid or cyclic DMA tag {a:#x}")
        seen.add(key)
        tag = u32(ram, a)
        ln, nxt = tag >> 24, tag & 0xFFFFFF
        if ln:
            out.append((a, list(struct.unpack_from(f"<{ln}I", ram, (a & 0x1FFFFF) + 4))))
        if nxt == 0xFFFFFF:
            return out
        a = 0x80000000 | nxt
    raise ValueError("DMA walk exceeds tag limit")


def norm(c):
    """unused bits cleared (as BEETLE_SWEEP/battles/triage.py): raw-texture colour, the later gouraud
    vertices' colour top bytes, the 3rd/4th uv words' high halves"""
    op = c[0] >> 24
    c = tuple(c)
    if 0x20 <= op < 0x40 and op & 5 == 5:
        c = (c[0] & 0xFF000000,) + c[1:]
    if 0x20 <= op < 0x40 and op & 0x10:
        n = 4 if op & 8 else 3
        tex = bool(op & 4)
        c = list(c)
        k = 1 + 1 + (1 if tex else 0)
        for j in range(1, n):
            if k < len(c):
                c[k] &= 0xFFFFFF
            k += 2 + (1 if tex else 0)
        c = tuple(c)
    if 0x20 <= op < 0x40 and op & 4:
        n = 4 if op & 8 else 3
        g = bool(op & 0x10)
        c = list(c)
        k = 1
        for j in range(n):
            if g and j:
                k += 1
            k += 1
            if j >= 2 and k < len(c):
                c[k] &= 0xFFFF
            k += 1
        c = tuple(c)
    return c


def env_of(dline):
    d = [int(x) for x in dline.split()[1:]]
    cx, cy, cw, ch, ox, oy = d[0:6]
    tpage, dtd, dfe, isbg = d[10], d[11], d[12], d[13]
    r, g, b = d[14:17]
    e = [0xE1000000 | tpage | (dtd << 9) | (dfe << 10), 0xE2000000, 0xE3000000 | cx | (cy << 10),
         0xE4000000 | (cx + cw - 1) | ((cy + ch - 1) << 10), 0xE5000000 | (ox & 0x7FF) | ((oy & 0x7FF) << 11),
         0xE6000000]
    fill = [0x02000000 | r | (g << 8) | (b << 16), cx | (cy << 16), cw | (ch << 16)] if isbg else []
    return e, fill, (cx, cy, cw, ch)


def transfers_of(lines):
    out = []
    for l in lines:
        w = l.split()
        if not w:
            continue
        if w[0] == "L":
            x, y, ww, hh = map(int, w[1:5])
            px = []
            for d in (int(t, 16) for t in w[5:]):
                px += [d & 0xFFFF, d >> 16]
            out.append(("L", x, y, ww, hh, px[:ww * hh]))
        elif w[0] == "C":
            x, y, ww, hh, r, g, b = map(int, w[1:8])
            out.append(("G", [0x02000000 | r | (g << 8) | (b << 16), x | (y << 16), ww | (hh << 16)]))
        elif w[0] == "M":
            x, y, ww, hh, dx, dy = map(int, w[1:7])
            out.append(("G", [0x80000000, x | (y << 16), dx | (dy << 16), ww | (hh << 16)]))
    return out


_BT = None


def render(base, env_words, transfers, cmdlist):
    """cmdlist: GP0 commands (word tuples), sent in chunks (the runner's operation list is ~900 KB)"""
    global _BT
    if _BT is None:
        _BT = lr.Beetle()
    for y0 in range(0, 512, 128):
        o = lr.Ops()
        o.load(0, y0, 1024, 128, [int(v) for v in base[y0:y0 + 128].ravel()])
        _BT.run(o)
    ops = lr.Ops()
    for t in transfers:
        if t[0] == "L":
            ops.load(*t[1:])
        else:
            ops.gp0(t[1])
    ops.gp0(env_words)
    _BT.run(ops)
    # Command by command, as the GP0 DMA would hand them.
    chunk = []
    for c in cmdlist:
        c = list(c)
        chunk.extend(c)
        if len(chunk) > 120000:
            o = lr.Ops()
            o.gp0(chunk)
            _BT.run(o)
            chunk = []
    o = lr.Ops()
    if chunk:
        o.gp0(chunk)
    o.read(0, 0, 1024, 512)
    return np.frombuffer(_BT.run(o)[0], dtype="<u2").reshape(512, 1024)


def rgb(a):
    return np.stack([a & 31, (a >> 5) & 31, (a >> 10) & 31], -1)


def picture(path, crop, h=216, w=320):
    a = np.asarray(Image.open(path).convert("RGB")).astype(int) >> 3
    return a[crop[1]:crop[1] + h, crop[0]:crop[0] + w]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--ram-a", required=True)
    ap.add_argument("--ram-b", required=True)
    ap.add_argument("--vram", required=True)
    ap.add_argument("--pic", required=True)
    ap.add_argument("--trace", help="the port's XG_GPU_TRACE (draw env and transfers); else --env-at")
    ap.add_argument("--env-at", type=lambda s: int(s, 0), help="DRAWENV address in RAM_A (no transfers)")
    ap.add_argument("--battle", action="store_true", help="battleRenderStructs: the context whose plain walk "
                    "matches the port picture best is A")
    ap.add_argument("--field", action="store_true")
    ap.add_argument("--menu", action="store_true")
    ap.add_argument("--worldmap", action="store_true", help="worldmapRenderingStructs: as --battle")
    ap.add_argument("--dump", help="write the plain and race command lists (hex) here")
    ap.add_argument("--head", type=lambda s: int(s, 0))
    ap.add_argument("--head-b", type=lambda s: int(s, 0))
    ap.add_argument("--port-frame")
    ap.add_argument("--crop", default="15,10")
    ap.add_argument("--h", type=int, default=216)
    ap.add_argument("--out")
    ap.add_argument("--trace-from", type=int, default=0, help="only O lines from this line number")
    a = ap.parse_args()
    global np, Image
    import numpy as np
    from PIL import Image
    ramA, ramB = Path(a.ram_a).read_bytes(), Path(a.ram_b).read_bytes()
    if len(ramA) != 0x200000 or len(ramB) != 0x200000:
        raise ValueError("RAM inputs must be exactly 2 MiB")
    if a.field:
        headA = u32(ramA, FIELD_CUR) + FIELD_UIOT7
        headB = u32(ramB, FIELD_CUR) + FIELD_UIOT7
        if a.env_at is None:
            a.env_at = u32(ramA, FIELD_CUR)
    elif a.menu:
        ctxA = u32(ramA, u32(ramA, MENU_CTX) + 0x1D4)
        ctxB = u32(ramB, u32(ramB, MENU_CTX) + 0x1D4)
        headA, headB = ctxA + 0x70 + 15 * 4, ctxB + 0x70 + 15 * 4
        if a.env_at is None:
            a.env_at = ctxA
    elif a.battle or a.worldmap:
        if a.battle:
            cands = [(BATTLE_RS + k * 0x4070, BATTLE_RS + (1 - k) * 0x4070) for k in (0, 1)]
            hd = lambda ram, c: c + 0x70 + 0xFFF * 4  # noqa: E731
        else:
            cands = [(WM_RS + k * 0x78, WM_RS + (1 - k) * 0x78) for k in (0, 1)]
            hd = lambda ram, c: u32(ram, c + 0x70) + 0x3FF * 4  # noqa: E731
        res, errors = [], []
        for ca, cb in cands:
            try:
                res.append(case(a, ramA, ramB, hd(ramA, ca), hd(ramB, cb), ca))
            except (ValueError, struct.error) as exc:
                errors.append(str(exc))
        if not res:
            raise ValueError("no valid rendering context: " + "; ".join(errors))
        res.sort(key=lambda r: r.get("plain_vs_port_px", r.get("plain_vs_beetle_px", float("inf"))))
        res[0]["context_errors"] = errors
        print(json.dumps(res[0]))
        return
    else:
        headA, headB = a.head, a.head_b
    print(json.dumps(case(a, ramA, ramB, headA, headB, a.env_at)))


def case(a, ramA, ramB, headA, headB, env_at):
    otA = walk(lambda ad: ramA, headA)
    otB = walk(lambda ad: ramB, headB)
    setB = {ad for ad, _ in otB}
    shared = [ad for ad, _ in otA if ad in setB]
    # race: the GPU walks OT A (RAM_A) until it reaches a shared primitive S that the next
    # frame's CPU work has already relinked (and rewritten): from S on it reads RAM_B, i.e.
    # S as rewritten and then OT B's chain after S. Every shared S is a candidate (the GPU may
    # pass the first ones before the CPU gets to them); the best one is reported.
    jumps = []
    for s_ad in shared:
        state = {"b": False}

        def rd(ad, s_ad=s_ad, state=state):
            if ad == s_ad:
                state["b"] = True
            return ramB if state["b"] else ramA
        jumps.append((s_ad, walk(rd, headA)))
    otR = jumps[0][1] if jumps else otA
    rep = {"headA": hex(headA), "headB": hex(headB), "tagsA": len(otA), "tagsB": len(otB), "shared": len(shared),
           "shared_addrs": [hex(x) for x in shared[:12]]}
    cA = cmds([w for _, ws in otA for w in ws])
    cR = cmds([w for _, ws in otR for w in ws])
    if a.dump:
        with open(a.dump + f".{headA:x}", "w") as f:
            for name, ot in (("plain", otA), ("race", otR)):
                for ad, ws in ot:
                    f.write(f"{name} {ad:08x} " + " ".join(f"{w:08x}" for w in ws) + "\n")
    rep["jumps"] = [(hex(sa), cmds([w for _, ws in ot for w in ws])) for sa, ot in jumps]
    if a.trace is None:
        return finish(a, rep, cA, cR, *env_ram(ramA, env_at), [])
    # the port's O line closest to OT A
    T = open(a.trace).read().splitlines()
    best = None
    na = [norm(c) for c in cA if c[0] >> 24]
    from collections import Counter
    ca = Counter(na)
    for i, l in enumerate(T):
        if i < a.trace_from or not l.startswith("O "):
            continue
        pw = [norm(c) for c in cmds([int(t, 16) for t in l.split()[1:]]) if c[0] >> 24]
        if abs(len(pw) - len(na)) > max(20, len(na) // 2):
            continue
        score = sum((ca & Counter(pw)).values()) - abs(len(pw) - len(na))
        if best is None or score > best[0]:
            best = (score, i, pw)
    if best is not None:
        _, i, pw = best
        sm = difflib.SequenceMatcher(None, na, pw, autojunk=False)
        best = (0, i, sum(m.size for m in sm.get_matching_blocks()), len(pw))
    if best is None:
        return dict(rep, error="no port O line")
    _, oi, eq, npw = best
    di = max(i for i in range(oi) if T[i].startswith("D "))
    rep.update({"port_O_line": oi + 1, "port_prims": npw, "beetle_prims": len(na), "equal_prims": eq})
    env, fill, (cx, cy, cw, ch) = env_of(T[di])
    trans = transfers_of(T[di + 1:oi])
    return finish(a, rep, cA, cR, env, fill, (cx, cy, cw, ch), trans)


def env_ram(ram, at):
    """the draw environment from a libgpu DRAWENV in RAM (clip, ofs, tw, tpage, dtd, dfe, isbg, rgb)"""
    cx, cy, cw, ch, ox, oy = struct.unpack_from("<hhhhhh", ram, at & 0x1FFFFF)
    tpage, dtd, dfe, isbg, r, g, b = struct.unpack_from("<HBBBBBB", ram, (at & 0x1FFFFF) + 20)
    line = "D " + " ".join(str(x) for x in (cx, cy, cw, ch, ox, oy, 0, 0, 0, 0, tpage, dtd, dfe, isbg, r, g, b))
    env, fill, rect = env_of(line)
    return env, fill, rect


def finish(a, rep, cA, cR, env, fill, rect, trans):
    cx, cy, cw, ch = rect
    if fill:
        trans = [("G", fill)] + trans
    base = np.frombuffer(open(a.vram, "rb").read(), dtype="<u2").reshape(512, 1024)
    crop = tuple(int(x) for x in a.crop.split(","))
    bp = picture(a.pic, crop, a.h, cw)
    out = {}
    cands = [("plain", cA)] + [(f"race@{sa}", cl) for sa, cl in rep.pop("jumps")]
    best = None
    for name, cl in cands:
        v = render(base, env, trans, cl)
        img = rgb(v[cy:cy + a.h, cx:cx + cw])
        px = int((img != bp).any(-1).sum())
        rep.setdefault("models_vs_beetle_px", {})[name] = px
        if name == "plain":
            out["plain"] = img
            rep["plain_vs_beetle_px"] = px
        elif best is None or px < best[0]:
            best = (px, name, img)
    if best is None:
        best = (rep["plain_vs_beetle_px"], "plain", out["plain"])
    out["race"] = best[2]
    rep["race_vs_beetle_px"] = best[0]
    rep["race_model"] = best[1]
    if a.port_frame:
        pf = np.asarray(Image.open(a.port_frame).convert("RGB")).astype(int)[:a.h, :cw] >> 3
        rep["plain_vs_port_px"] = int((out["plain"] != pf).any(-1).sum())
        rep["race_vs_port_px"] = int((out["race"] != pf).any(-1).sum())
        rep["port_vs_beetle_px"] = int((pf != bp).any(-1).sum())
    if a.out:
        Path(a.out).mkdir(parents=True, exist_ok=True)
        for name, img in out.items():
            Image.fromarray((img << 3).astype(np.uint8)).save(Path(a.out) / f"{name}.png")
    return rep


if __name__ == "__main__":
    try:
        main()
    finally:
        if _BT is not None:
            _BT.close()
