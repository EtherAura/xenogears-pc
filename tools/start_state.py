#!/usr/bin/env python3
"""The PC's development start options, for PCSX-Redux (port/src/system/options.c).

The PC reads XG_BOOT_MODE, XG_FIELD, XG_PRESET, XG_PARTY and XG_GAMESTATE
(options.c documents them) and pokes gameState just before the first game
mode starts. This module turns the same settings into Lua for a Redux run, so
both sides start in the same state:

    * kernelMain's setGameMode(6) becomes setGameMode(n) once the executable
      is in RAM (XG_BOOT_MODE, or 1 for XG_FIELD alone), as on the PC;
    * the gameState pokes happen at the first vsync while bootGame loads
      that mode (bootMode != 0), before the mode reads them;
    * the debug font pointer is cleared there too, as the PC's bootGame
      drops the old mode's font block.

    python tools/start_state.py XG_FIELD=4 XG_PRESET=disc2      # print the Lua
    import start_state; start_state.lua(env)                     # from a capture script

The Lua runs inside a capture chunk (tools/redux_capture.py-style: `ffi`, `XG`,
`PCSX` in scope) and installs a GPU::Vsync listener XG.listeners.startState;
XG.startState.applied holds the vsync of the pokes. lua_pokes(env) gives only
the pokes, for use right after loading a save state taken in that window.
"""

from __future__ import annotations

import os
import sys

# Per version: gameState, bootMode, debugFontRam, and kernelMain's
# 'ori a0,zero,6' (the setGameMode(6) delay slot).
ADDR = {
    "us": {"gameState": 0x8006D634, "bootMode": 0x80018088, "debugFontRam": 0x80059394,
           "setMode6": 0x80019930},
    "jp": {"gameState": 0x8006CCC4, "bootMode": 0x80018088, "debugFontRam": 0x80058A30,
           "setMode6": 0x800199B4},
}
GAMESTATE_SIZE = 0x2358
PRESETS = {
    # field variable 82, the current disc (0 or 1): what a disc-2 save holds
    "disc2": [(0x1930 + 82 * 2, 2, 1)],
}
OPTIONS = ("XG_BOOT_MODE", "XG_FIELD", "XG_PRESET", "XG_PARTY", "XG_GAMESTATE")


def _num(text: str) -> int:
    return int(text.strip(), 0)


def boot_mode(env: dict) -> int:
    """The mode the PC starts after the boot logo (portBootMode), -1 = the movie."""
    s = env.get("XG_BOOT_MODE", "")
    if not s:
        return 1 if env.get("XG_FIELD") else -1
    try:
        mode = int(s)
    except ValueError:
        return -1
    return mode if 0 <= mode <= 6 else -1


def pokes(env: dict) -> tuple[list[tuple[int, int, int]], list[int] | None, list[tuple[int, int, int]]]:
    """In the PC's order: the presets' pokes [(offset, size, value)], the party
    (or None; its member bits are ORed into m1D30), then XG_GAMESTATE and
    XG_FIELD's pokes."""
    out: list[tuple[int, int, int]] = []
    for name in filter(None, env.get("XG_PRESET", "").split(",")):
        if name not in PRESETS:
            raise SystemExit(f"unknown XG_PRESET {name!r}")
        out += PRESETS[name]
    party = None
    if env.get("XG_PARTY"):
        ids = [_num(x) if x.strip() else 0xFF for x in env["XG_PARTY"].split(",")[:3]]
        party = (ids + [0xFF, 0xFF, 0xFF])[:3]
    poke_list = []
    for item in filter(None, env.get("XG_GAMESTATE", "").split(",")):
        where, value = item.split("=")
        off, _, size = where.partition(":")
        poke_list.append((_num(off), _num(size) if size else 2, _num(value)))
    for off, size, _ in poke_list:
        if size not in (1, 2, 4) or off + size > GAMESTATE_SIZE:
            raise SystemExit(f"bad XG_GAMESTATE item at 0x{off:X}:{size}")
    if env.get("XG_FIELD"):
        poke_list.append((0x231A, 2, _num(env["XG_FIELD"]) & 0xFFFF))
    return out, party, poke_list


def lua_pokes(env: dict, ver: str = "us") -> str:
    """Lua that writes the gameState pokes now (m = PCSX.getMemPtr())."""
    base = ADDR[ver]["gameState"] & 0x1FFFFF
    pre, party, rest = pokes(env)
    lines = [f"local gs = PCSX.getMemPtr() + 0x{base:X}"]

    def poke(off: int, size: int, value: int) -> None:
        ctype = {1: "uint8_t", 2: "uint16_t", 4: "uint32_t"}[size]
        lines.append(f"ffi.cast('{ctype}*', gs + 0x{off:X})[0] = 0x{value & ((1 << 8 * size) - 1):X}")

    for p in pre:
        poke(*p)
    if party is not None:
        for i, cid in enumerate(party):
            poke(0x1D34 + i, 1, cid & 0xFF)
        bits = 0
        for cid in party:
            if 0 <= cid < 16:
                bits |= 1 << cid
        lines.append(f"do local b = ffi.cast('uint16_t*', gs + 0x1D30); b[0] = bit.bor(b[0], 0x{bits:X}) end")
    for p in rest:
        poke(*p)
    return "\n".join(lines) + "\n"


def lua(env: dict, ver: str = "us") -> str:
    """Lua for a run from power-on: the boot-mode patch, then the pokes."""
    a = ADDR[ver]
    mode = boot_mode(env)
    body = lua_pokes(env, ver).replace("\n", "\n        ")
    return f"""XG.startState = {{ patched = nil, applied = nil }}
if XG.listeners.startState then XG.listeners.startState:remove() end
XG.listeners.startState = PCSX.Events.createEventListener('GPU::Vsync', function()
    local st = XG.startState
    local m = PCSX.getMemPtr()
    local mode = {mode}
    if mode < 0 then return end
    if not st.patched then
        local w = ffi.cast('uint32_t*', m + 0x{a['setMode6'] & 0x1FFFFF:X})
        if w[0] == 0x34040006 then
            w[0] = 0x34040000 + mode
            st.patched = XG.vsyncs
        end
        return
    end
    if not st.applied and ffi.cast('int32_t*', m + 0x{a['bootMode'] & 0x1FFFFF:X})[0] ~= 0 then
        ffi.cast('uint32_t*', m + 0x{a['debugFontRam'] & 0x1FFFFF:X})[0] = 0
        {body}st.applied = XG.vsyncs
    end
end)
"""


def main() -> None:
    env = {}
    ver = "us"
    for arg in sys.argv[1:]:
        if arg in ("--jp", "--us"):
            ver = arg[2:]
            continue
        k, _, v = arg.partition("=")
        env[k] = v
    if not env:
        env = {k: os.environ[k] for k in OPTIONS if k in os.environ}
    print(lua(env, ver))


if __name__ == "__main__":
    main()
