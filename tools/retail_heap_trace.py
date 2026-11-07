#!/usr/bin/env python3
"""Observe retail allocation lifetimes with original black-box Redux breakpoints.

    python tools/retail_heap_trace.py OUT --ver us --field 2 --stop 1900 --fastboot
    python tools/retail_heap.py OUT/events.txt --allow-unlinked-frees

Redux supplies debugger observations; read-only Beetle heap snapshots from
wallslide_probe.py --heap supply the accuracy-reference cross-check. This tool
uses our existing harness, never emulator source, and modifies no disc files.
Its disposable run directory and cards are under OUT; BIOS files are neither
copied nor bundled. Independent collectors need different --port values.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import struct
import time

import redux
import redux_capture
import start_state

ROOT = Path(__file__).resolve().parents[1]
FUNCTIONS = dict(initHeap=2, resetHeap=1, allocateHeap=2, freeHeap=1,
                 shrinkAllocation=2, flagAllocation=1, unflagAllocation=1,
                 unpinAllocation=1, setHeapUser=1, resetHeapTraceState=0,
                 resetMemoryAllocStats=2, mergeFreeHeapBlocks=0)


def trace_lua(out: Path, syms: dict, stop: int) -> str:
    sites = ",".join("{'%s',%d,%d}" %
                     (name, syms[name] + (4 if name == "allocateHeap" else 0), count)
                     for name, count in FUNCTIONS.items())
    # Keep UTF-8 literal: Lua quoted strings do not accept JSON's \u escapes.
    return r'''
local file=Support.File.open(OUTPUT,'TRUNCATE')
local lines,bps,active,resetActive={}, {}, nil, false
local function put(s) lines[#lines+1]=s end
local functions={SITES}
XG.heapTraceStatus='running'
for _,site in ipairs(functions) do
 local name,address,n=site[1],site[2],site[3]
 bps[#bps+1]=PCSX.addBreakpoint(address,'Exec',4,'heap:'..name,function()
  local g=PCSX.getRegisters().GPR.n
  if active and name~='mergeFreeHeapBlocks' then error('nested traced heap call '..name) end
  local row=string.format('%s %d %08X %08X %08X',name,XG.vsyncs,
                         n>0 and tonumber(g.a0) or 0,n>1 and tonumber(g.a1) or 0,tonumber(g.ra))
  if name=='resetHeap' then
   if resetActive then error('nested heap reset') end
   resetActive=true
   put(row:gsub('resetHeap','resetHeapBegin'))
   local resetReturn
   resetReturn=PCSX.addBreakpoint(tonumber(g.ra),'Exec',4,'heap:resetreturn',function()
    put(row:gsub('resetHeap','resetHeapEnd'))
    resetActive=false;resetReturn:remove();return true
   end)
   return true
  end
  if name~='allocateHeap' and name~='freeHeap' and name~='shrinkAllocation' then
   put(row);return true
  end
  active=PCSX.addBreakpoint(tonumber(g.ra),'Exec',4,'heap:return',function()
   put(row..string.format(' %08X',tonumber(PCSX.getRegisters().GPR.n.v0)))
   active:remove();active=nil;return true
  end)
  return true
 end)
end
XG.listeners.heapTrace=PCSX.Events.createEventListener('GPU::Vsync',function()
 if XG.vsyncs>=STOP and not active and not resetActive then
  put('# complete at vsync '..XG.vsyncs)
  file:write(table.concat(lines,string.char(10))..string.char(10));file:close()
  for _,bp in ipairs(bps) do bp:remove() end
  XG.heapTraceStatus='complete';PCSX.pauseEmulator()
 end
end)
return 'ok'
'''.replace("SITES", sites).replace("STOP", str(stop)).replace(
        "OUTPUT", json.dumps(str(out / "events.txt"), ensure_ascii=False))


def run(args) -> dict:
    out = args.out.resolve()
    out.mkdir(parents=True, exist_ok=False)
    syms = redux.read_symbols(args.ver, ["main"])
    main = ROOT / "disc" / args.ver / "main.exe"
    image = main.read_bytes()
    base = struct.unpack_from("<I", image, 0x18)[0]
    instruction = struct.unpack_from("<I", image, syms["allocateHeap"] - base + 0x800)[0]
    if instruction != 0x27BDFFD8:  # addiu sp,sp,-0x28: does not consume/change a0
        raise ValueError("unexpected allocateHeap entry; load-delay sampling point needs review")
    lua = trace_lua(out, syms, args.stop)
    (out / "trace.lua").write_text(lua)
    pad = args.pad.resolve() if args.pad else out / "idle.pad"
    if not args.pad:
        pad.write_text("0 0000\n")
    bios = args.bios or Path("/mnt/Games/Arcade/Frontends/RetroArch/system") / (
        "scph5500.bin" if args.ver == "jp" else "scph5501.bin")
    metadata = dict(ver=args.ver, disc=args.disc, field=args.field, stop=args.stop,
                    fastboot=args.fastboot, start=args.start, functions=FUNCTIONS,
                    sampling="allocateHeap + 4; other entries; call returns; reset begin/end",
                    main_sha256=hashlib.sha256(image).hexdigest(),
                    field_sha256=hashlib.sha256((ROOT / "disc" / args.ver / "field.bin").read_bytes()).hexdigest(),
                    collector_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
                    lua_sha256=hashlib.sha256(lua.encode()).hexdigest(),
                    debugger_sha256=hashlib.sha256(redux.DEFAULT_BIN.read_bytes()).hexdigest(),
                    pad_sha256=hashlib.sha256(pad.read_bytes()).hexdigest())
    (out / "run.json").write_text(json.dumps(metadata, indent=2) + "\n")
    rundir = out / "redux"
    redux.start(rundir=rundir, port=args.port, iso=redux.disc_image(args.ver, args.disc),
                bios=bios, paused=True, speed=0, debugger=True, fastboot=args.fastboot)
    try:
        client, _ = redux.connect(rundir)
        env = dict(option.split("=", 1) for option in args.start)
        env["XG_FIELD"] = str(args.field)
        if args.disc == 2:
            env.setdefault("XG_PRESET", "disc2")
        client.run_lua(start_state.lua(env, args.ver) + "\nreturn 'ok'")
        client.run_lua(lua)
        client.pad_sequence(redux_capture.pad_steps(pad, args.stop + 120), pause=False)
        client.lua("run")
        until = time.monotonic() + args.timeout
        while time.monotonic() < until:
            time.sleep(2)
            if not client.flow()["running"]:
                status = client.run_lua("return XG.heapTraceStatus")
                if "complete" not in str(status):
                    raise RuntimeError(f"debugger paused before trace completed: {status}")
                break
        else:
            raise TimeoutError("retail heap collector timed out")
    finally:
        redux.stop(rundir)
    lines = (out / "events.txt").read_text().replace("\\n", "\n").splitlines()
    if not lines or not lines[-1].startswith("# complete"):
        raise RuntimeError("incomplete event log")
    metadata["events"] = sum(not line.startswith("#") for line in lines)
    metadata["events_sha256"] = hashlib.sha256((out / "events.txt").read_bytes()).hexdigest()
    (out / "summary.json").write_text(json.dumps(metadata, indent=2) + "\n")
    return metadata


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("out", type=Path)
    ap.add_argument("--ver", choices=("us", "jp"), default="us")
    ap.add_argument("--disc", choices=(1, 2), type=int, default=1)
    ap.add_argument("--field", type=int, required=True)
    ap.add_argument("--stop", type=int, default=1900)
    ap.add_argument("--fastboot", action="store_true")
    ap.add_argument("--port", type=int, default=8360)
    ap.add_argument("--timeout", type=int, default=240)
    ap.add_argument("--bios", type=Path)
    ap.add_argument("--pad", type=Path)
    ap.add_argument("--start", action="append", default=[], metavar="XG_OPTION=VALUE")
    args = ap.parse_args()
    if not 0 <= args.field < 1024 or args.stop <= 0 or args.timeout <= 0:
        ap.error("valid field id and positive stop/timeout required")
    if any(option.partition("=")[0] not in ("XG_PARTY", "XG_GAMESTATE") or "=" not in option
           for option in args.start):
        ap.error("--start accepts XG_PARTY or XG_GAMESTATE")
    print(json.dumps(run(args), indent=2))


if __name__ == "__main__":
    main()
