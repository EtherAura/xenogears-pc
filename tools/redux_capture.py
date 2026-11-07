#!/usr/bin/env python3
"""Capture a PCSX-Redux reference run from power-on with one pad script (PC port).

    python tools/redux_capture.py OUT PADFILE STOP --bios FILE [--every 30] [--from 0]
                                  [--states 6000,9000] [--log 0x800CCC5C,...] [--vsync-log]

PADFILE has the port's XG_PAD_SCRIPT format: lines "<vsync> <hex buttons>"
(0x0800 Start, 0x0040 Cross, 0x1000 Up, ...), '#' comments. Here the vsyncs
count from power-on, BIOS intro included, so the same moment has a larger
number than on the PC (Fei's room: PC = Redux - 1766).

Everything is recorded inside the emulator by a Lua GPU::Vsync listener, so
the run is never paused or slowed by this script, which only polls every 3 s
(Redux's web server is flaky under fast polling):

    OUT/v<vsync>.png    the TV picture every --every vsyncs from --from
                        (--keep-raw: Redux's raw dumps, OUT/shots.txt lists them)
    OUT/s<vsync>.sav    save states at --states (tools/redux.py state load)
    OUT/ramlog.txt      --log: "<vsync> <u32> ..." for the listed RAM words,
                        every vsync from --from
    OUT/vsyncs.txt      --vsync-log: the game's VSync() calls, game mode
                        starts, CD commands, reads and their data sectors
                        and command completions from --from: the PS1 timing
                        that the port's slowdown replay plays back
                        (XG_SLOWDOWN; the format is in
                        port/src/system/slowdown.c). Needs breakpoints, so
                        Redux runs its interpreter: about 2 minutes from
                        power-on to the opening battle, slower where the game
                        busy-waits on VSync(-1)

A slowdown replay run (the opening battle, lined up frame for frame with
Redux from the battle on, at a constant offset):

    python tools/redux_capture.py ref redux.pad 17800 --from 14000 --vsync-log --every 1
    XG_SLOWDOWN=ref/vsyncs.txt XG_CLOCK=step XG_PAD_SCRIPT=pc.pad ... build/pc/<dir>/xenogears

where pc.pad has the Redux pad's buttons from the battle start on, shifted
by the offset the port prints ("log vblank N = vblank M here").

Drive every capture from power-on: after `state load` Redux ignores pad input.
16000 vsyncs take about 20 s. --port and --rundir keep parallel captures
apart. The BIOS comes from --bios or PCSX_REDUX_BIOS and is never copied.
"""
from __future__ import annotations

import argparse
import os
import re
import subprocess
import sys
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent
sys.path.insert(0, str(HERE))
import redux  # noqa: E402

# The VSync log's breakpoints: VSync (from the symbol file); bootGame's
# `jalr v0` to the game mode's entry point, with gameModes, the table of entry
# points; LIBCD's interrupt handler (`callback`) calling the CD ready callback.
# The jalr addresses come from the build's main.elf (mips-linux-gnu-objdump).
ENTRY_CALL = {"us": 0x80019C04, "jp": 0x80019C88}
# CD_cw, LIBCD's command write, comes from the symbol file.
READY_CALL = {"us": 0x80042D18, "jp": 0x80042B90}
# `callback` past its test of getintr's sync bit: a command completed (its
# status byte, CdlComplete or CdlDiskError, is at 0($s3)), callback or not.
SYNC_SEEN = {"us": 0x80042D2C, "jp": 0x80042BA4}
GAME_MODES = 0x8001808C

NAMES = [(0x0001, "L2"), (0x0002, "R2"), (0x0004, "L1"), (0x0008, "R1"), (0x0010, "TRIANGLE"),
         (0x0020, "CIRCLE"), (0x0040, "CROSS"), (0x0080, "SQUARE"), (0x0100, "SELECT"),
         (0x0800, "START"), (0x1000, "UP"), (0x2000, "RIGHT"), (0x4000, "DOWN"), (0x8000, "LEFT")]


def pad_steps(path: Path, stop: int) -> list[tuple[str, int]]:
    """XG_PAD_SCRIPT lines -> redux.py pad steps (buttons held for N vsyncs)."""
    events = []
    for line in path.read_text().splitlines():
        line = line.split("#")[0].strip()
        if line:
            v, b = line.split()
            events.append((int(v), int(b, 16)))
    steps, cur, at = [], 0, 0
    for v, b in sorted(events):
        if v > at:
            steps.append(("+".join(n for m, n in NAMES if cur & m) or "_", v - at))
            at = v
        cur = b
    steps.append(("+".join(n for m, n in NAMES if cur & m) or "_", max(1, stop - at)))
    return steps


def sdk_code_range(ver: str) -> tuple[int, int]:
    """main's PsyQ SDK code (config/<ver>/main.yaml): from its first psyq/ object to the data
    after it. Its own VSync calls (DrawSync's and LIBCD's timeouts poll VSync(-1)) are not the
    game's: the port's libraries make none."""
    text = (ROOT / "config" / ver / "main.yaml").read_text()
    start = int(re.search(r"^\s+start:\s*(0x[0-9A-Fa-f]+)", text, re.M).group(1), 16)
    vram = int(re.search(r"^\s+vram:\s*(0x[0-9A-Fa-f]+)", text, re.M).group(1), 16)
    lo = min(int(m, 16) for m in re.findall(r"^\s+- \[(0x[0-9A-Fa-f]+), (?:c|asm), psyq/", text, re.M))
    hi = min(int(m, 16) for m in re.findall(r"^\s+- \[(0x[0-9A-Fa-f]+), \.?data", text, re.M) if int(m, 16) > lo)
    return vram + lo - start, vram + hi - start


def lua_vsync_log(out: Path, args) -> str:
    """Breakpoint handlers that write OUT/vsyncs.txt (XG.vsl: arm, flush, close)."""
    syms = redux.read_symbols(args.ver, ["main"])
    vsync, cd_cw, boot = syms["VSync"], syms["CD_cw"], syms["VSyncCallback"]
    sdk_lo, sdk_hi = sdk_code_range(args.ver)
    return f"""
local VSL = {{ file = Support.File.open('{out}/vsyncs.txt', 'TRUNCATE'), buf = {{}}, arg = nil, bps = {{}} }}
VSL.file:write('# xenogears VSync log 1 (PCSX-Redux, {args.ver}, tools/redux_capture.py; vblanks from power-on)\\n')
local function vrun()
    if VSL.arg ~= nil then
        if VSL.n == 1 then
            VSL.buf[#VSL.buf + 1] = string.format('v %d %.3f', VSL.arg, VSL.first)
        else
            VSL.buf[#VSL.buf + 1] = string.format('v %d %.3f %d %d', VSL.arg, VSL.first, VSL.last, VSL.n)
        end
        VSL.arg = nil
    end
end
function VSL.flush()
    if #VSL.buf > 0 then
        VSL.file:write(table.concat(VSL.buf, '\\n') .. '\\n')
        VSL.buf = {{}}
    end
end
function VSL.arm()
    VSL.bps[1] = PCSX.addBreakpoint({vsync}, 'Exec', 4, 'xg:vsynclog', function()
        local g = PCSX.getRegisters().GPR.n
        local ra = tonumber(g.ra)
        if ra >= {sdk_lo} and ra < {sdk_hi} then return true end
        local a = tonumber(g.a0)
        if a >= 2147483648 then a = a - 4294967296 end
        local v = XG.vsyncs
        -- VSync(-1) polls and VSync(1) reads merge into runs; each waiting
        -- call (VSync(0), VSync(n > 1)) gets its own line
        if VSL.arg == a and (a < 0 or a == 1) then
            VSL.last = v
            VSL.n = VSL.n + 1
        else
            vrun()
            -- the first call's time with the fraction of the frame: where the
            -- PS1's vblanks fell inside a computation
            VSL.arg, VSL.first, VSL.last, VSL.n = a, VSL.now(), v, 1
        end
        return true
    end)
    VSL.bps[2] = PCSX.addBreakpoint({ENTRY_CALL[args.ver]}, 'Exec', 4, 'xg:modelog', function()
        local target = tonumber(PCSX.getRegisters().GPR.n.v0)
        local m = PCSX.getMemPtr()
        local mode = -1
        for i = 0, 6 do
            if tonumber(ffi.cast('uint32_t*', m + {GAME_MODES % 0x200000} + 16 * i)[0]) == target then mode = i end
        end
        vrun()
        VSL.buf[#VSL.buf + 1] = string.format('m %d %d', mode, XG.vsyncs)
        return true
    end)
    VSL.bps[3] = PCSX.addBreakpoint({READY_CALL[args.ver]}, 'Exec', 4, 'xg:cdlog', function()
        local status = tonumber(PCSX.getRegisters().GPR.n.a0)
        -- Not a break in the current run of VSync calls: the port's runs do not see sectors.
        VSL.buf[#VSL.buf + 1] = string.format('r %d %.3f', status, VSL.now())
        return true
    end)
    VSL.bps[4] = PCSX.addBreakpoint({cd_cw}, 'Exec', 4, 'xg:cdcom', function()
        local g = PCSX.getRegisters().GPR.n
        local com = bit.band(tonumber(g.a0), 0xFF)
        local t = VSL.now()
        VSL.buf[#VSL.buf + 1] = string.format('k %d %.3f', com, t)
        if com == 2 then
            local p = bit.band(tonumber(g.a1), 0x1FFFFFFF)
            if p ~= 0 then
                local m = p >= 0x1F800000 and PCSX.getScratchPtr() + bit.band(p, 0x3FF)
                          or PCSX.getMemPtr() + bit.band(p, 0x1FFFFF)
                local function bcd(b) return bit.rshift(b, 4) * 10 + bit.band(b, 15) end
                VSL.setloc = (bcd(m[0]) * 60 + bcd(m[1])) * 75 + bcd(m[2]) - 150
            end
        elseif com == 6 or com == 0x1B then
            VSL.buf[#VSL.buf + 1] = string.format('c %d %d %.3f', com, VSL.setloc or -1, t)
            VSL.setloc = nil
        end
        return true
    end)
    VSL.bps[5] = PCSX.addBreakpoint({SYNC_SEEN[args.ver]}, 'Exec', 4, 'xg:cdsync', function()
        local s3 = bit.band(tonumber(PCSX.getRegisters().GPR.n.s3), 0x1FFFFF)
        VSL.buf[#VSL.buf + 1] = string.format('s %d %.3f', PCSX.getMemPtr()[s3], VSL.now())
        return true
    end)
end
-- Vblanks so far with the fraction of the frame (from the CPU cycles since the last).
function VSL.now()
    local frac = 0
    if VSL.period and VSL.period > 0 then
        frac = (tonumber(PCSX.getCPUCycles()) - VSL.vcyc) / VSL.period
        if frac > 0.999 then frac = 0.999 end
    end
    return XG.vsyncs + frac
end
-- The vblank's CPU cycle count, for the CD records' fraction of a frame.
function VSL.vblank()
    local c = tonumber(PCSX.getCPUCycles())
    if VSL.vcyc then VSL.period = c - VSL.vcyc end
    VSL.vcyc = c
end
-- kernelMain installing its vsync callback, whatever --from says: the replay
-- lines the boot up with it (the first one only; a soft reset installs it again).
VSL.bootbp = PCSX.addBreakpoint({boot}, 'Exec', 4, 'xg:boot', function()
    if not VSL.boot and tonumber(PCSX.getRegisters().GPR.n.a0) ~= 0 then
        VSL.boot = VSL.now()
        VSL.buf[#VSL.buf + 1] = string.format('b %.3f', VSL.boot)
    end
    return true
end)
function VSL.close()
    vrun()
    VSL.flush()
    VSL.file:close()
    for _, b in ipairs(VSL.bps) do b:remove() end
    VSL.bootbp:remove()
end
XG.vsl = VSL
return 'ok'
"""


def lua_listener(out: Path, args) -> str:
    states = ",".join(str(int(s)) for s in args.states.split(",") if s)
    return f"""
local out, every, stop, start = '{out}', {args.every}, {args.stop}, {args.start}
local vsl = XG.vsl
local logaddrs = {{}}
for a in ('{args.log}'):gmatch('[^,]+') do logaddrs[#logaddrs + 1] = tonumber(a) % 0x200000 end
local ramlog = #logaddrs > 0 and Support.File.open(out .. '/ramlog.txt', 'TRUNCATE') or nil
local states = {{}}
for s in ('{states}'):gmatch('%d+') do states[tonumber(s)] = true end
local log = Support.File.open(out .. '/shots.txt', 'TRUNCATE')
if XG.listeners.cap then XG.listeners.cap:remove() end
XG.capErr = nil
local function tick()
    local v = XG.vsyncs
    if vsl then
        vsl.vblank()
        if v == start or (v > start and not vsl.armed) then
            vsl.arm()
            vsl.armed = true
        end
        if v % 60 == 0 then vsl.flush() end
    end
    if ramlog and v >= start then
        local m = PCSX.getMemPtr()
        local s = tostring(v)
        for _, a in ipairs(logaddrs) do s = s .. ' ' .. tostring(ffi.cast('uint32_t*', m + a)[0]) end
        ramlog:write(s .. '\\n')
    end
    if every > 0 and v >= start and v % every == 0 then
        local s = PCSX.GPU.takeScreenShot()
        local f = Support.File.open(string.format('%s/v%05d.raw', out, v), 'TRUNCATE')
        f:writeMoveSlice(s.data)
        f:close()
        log:write(string.format('%d %d %d %d\\n', v, s.width, s.height, tonumber(s.bpp)))
    end
    if states[v] then
        local slice = PCSX.createSaveState()
        local f = Support.File.open(string.format('%s/s%05d.sav', out, v), 'TRUNCATE')
        f:writeMoveSlice(slice)
        f:close()
    end
    if v >= stop then
        log:close()
        if ramlog then ramlog:close() end
        if vsl then vsl.close(); XG.vsl = nil; vsl = nil end
        PCSX.pauseEmulator()
    end
end
XG.listeners.cap = PCSX.Events.createEventListener('GPU::Vsync', function()
    local ok, err = pcall(tick)
    if not ok and not XG.capErr then XG.capErr = tostring(err) .. ' at ' .. XG.vsyncs end
end)
return 'ok'
"""


_LUT = None


def raws_to_png(out: Path) -> int:
    """OUT/shots.txt + v*.raw (bpp 0 = 15-bit, 1 = 24-bit) -> v*.png."""
    global _LUT
    if _LUT is None:
        c5 = [(c << 3) | (c >> 2) for c in range(32)]
        _LUT = [bytes((c5[p & 31], c5[(p >> 5) & 31], c5[(p >> 10) & 31])) for p in range(32768)]
    n = 0
    for line in (out / "shots.txt").read_text().splitlines():
        v, w, h, bpp = map(int, line.split())
        raw = out / f"v{v:05d}.raw"
        if not raw.exists() or w == 0:
            continue
        data = raw.read_bytes()
        if bpp == 0:
            words = memoryview(data[: w * h * 2]).cast("H")
            rows = [b"".join(_LUT[p & 0x7FFF] for p in words[y * w:(y + 1) * w]) for y in range(h)]
        else:
            rows = [data[y * w * 3:(y + 1) * w * 3] for y in range(h)]
        redux.write_png(out / f"v{v:05d}.png", w, h, rows)
        raw.unlink()
        n += 1
    return n


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("out", type=Path)
    ap.add_argument("pad", type=Path)
    ap.add_argument("stop", type=int, help="last vsync (from power-on)")
    ap.add_argument("--every", type=int, default=30, help="screenshot period in vsyncs (0: none)")
    ap.add_argument("--from", dest="start", type=int, default=0, help="first vsync with screenshots and logs")
    ap.add_argument("--states", default="", help="comma-separated vsyncs that get a save state")
    ap.add_argument("--log", default="", help="comma-separated RAM addresses (u32) logged every vsync")
    ap.add_argument("--vsync-log", action="store_true", help="log VSync() calls and mode starts (slowdown replay)")
    ap.add_argument("--ver", default="us", choices=["us", "jp"])
    ap.add_argument("--disc", type=int, default=1)
    ap.add_argument("--bios", type=Path, default=os.environ.get("PCSX_REDUX_BIOS"))
    ap.add_argument("--port", type=int, default=8199, help="web server port")
    ap.add_argument("--rundir", type=Path, help="Redux run directory (default work/redux_capture/<port>)")
    ap.add_argument("--keep-raw", action="store_true", help="leave Redux's raw screenshots unconverted")
    args = ap.parse_args()
    if args.bios is None:
        sys.exit("redux_capture: pass --bios or set PCSX_REDUX_BIOS (a BIOS is never searched for)")
    out = args.out.resolve()
    out.mkdir(parents=True, exist_ok=True)
    rundir = (args.rundir or ROOT / "work/redux_capture" / str(args.port)).resolve()
    steps = pad_steps(args.pad, args.stop)

    redux.start(rundir=rundir, port=args.port, iso=redux.disc_image(args.ver, args.disc), bios=args.bios,
                paused=True, speed=0, debugger=args.vsync_log)
    client, _ = redux.connect(rundir)
    try:
        print(client.run_lua(lua_vsync_log(out, args) if args.vsync_log else "XG.vsl = nil return 'ok'"))
        print(client.run_lua(lua_listener(out, args)))
        client.pad_sequence(steps, pause=False)
        client.lua("run")
        t0 = time.monotonic()
        while True:
            time.sleep(3)
            try:
                if not client.flow()["running"]:
                    print("paused at", client.run_lua("return XG.vsyncs, XG.capErr"))
                    break
            except Exception as e:  # noqa: BLE001
                print("poll:", e)
                time.sleep(3)
                try:
                    client.flow()
                except Exception:  # noqa: BLE001
                    sys.exit("Redux died")
        print(f"done in {time.monotonic() - t0:.0f}s")
    finally:
        redux.stop(rundir)
    if not args.keep_raw and args.every > 0:
        print(f"{raws_to_png(out)} screenshots")
    return 0


if __name__ == "__main__":
    sys.exit(main())
