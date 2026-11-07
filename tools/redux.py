#!/usr/bin/env python3
"""Drive PCSX-Redux headless through its web API (validation harness).

Starts tools/toolchain/pcsx-redux/bin/pcsx-redux with -no-ui and the web
server on, loads redux_harness.lua (Lua handlers for save states, breakpoints,
vsync counting, running Lua files), and talks to it over HTTP on 127.0.0.1.
Only the standard library is used.

    python tools/redux.py start --ver us --disc 1 [--bios FILE] [--debugger] [--paused]
    python tools/redux.py start --ver us --disc 1 --bios FILE --gui --audio   # watch and play along
    python tools/redux.py status
    python tools/redux.py symbols main field         # config/<ver>/symbols/<module>.txt
    python tools/redux.py ram out.bin                 # 2 MB main RAM
    python tools/redux.py vram out.png                # 1024x512 VRAM (.bin for raw 16-bit)
    python tools/redux.py screenshot out.png          # what the TV shows
    python tools/redux.py state save title.sav        # relative paths: in the run directory
    python tools/redux.py state load title.sav
    python tools/redux.py run 600                     # run 600 vsyncs, then pause
    python tools/redux.py speed 0                     # unthrottled (1 = real time, the default)
    python tools/redux.py fps PutDispEnv --vsyncs 300 # needs `start --debugger`
    python tools/redux.py bp add 0x80044E9C --name flip [--pause]
    python tools/redux.py pad tap X --after 240       # title: open the menu (JP confirms with O)
    python tools/redux.py pad seq "UP*4 _*30 X*4"     # timed steps (BUTTONS*VSYNCS), then pause
    python tools/redux.py pad press UP+X ; python tools/redux.py pad release   # holds
    python tools/redux.py pad state                   # what the game sees on port 1
    python tools/redux.py lua -e 'return PCSX.getCPUCycles()'
    python tools/redux.py stop
    python tools/redux.py selftest                    # everything that works without a BIOS

The emulator runs in the run directory (default work/redux, gitignored and not
synced): pcsx.json settings, memory cards, redux.log, save states. A BIOS is
never searched for or copied; pass --bios or set PCSX_REDUX_BIOS. Without one
PCSX-Redux uses share/pcsx-redux/resources/openbios.bin when it was built, and
otherwise halts in a stub (the web API still answers).

Security: the build script patches PCSX-Redux to listen on 127.0.0.1 only
(upstream listens on every interface). The Lua handlers also need the random
per-session token this script passes in XG_REDUX_TOKEN.
"""

from __future__ import annotations

import argparse
import gzip
import http.client
import json
import os
import re
import secrets
import signal
import socket
import struct
import subprocess
import sys
import time
import urllib.parse
import zlib
from pathlib import Path


def _find_root() -> Path:
    here = Path(__file__).resolve().parent
    for d in (here, *here.parents):
        if (d / "config" / "splat.common.yaml").exists() and (d / "tools").is_dir():
            return d
    raise SystemExit("cannot find the repository root above " + str(here))


ROOT = _find_root()
HERE = Path(__file__).resolve().parent
DEFAULT_BIN = ROOT / "tools/toolchain/pcsx-redux/bin/pcsx-redux"
DEFAULT_RUNDIR = ROOT / "work/redux"
HARNESS_LUA = HERE / "redux_harness.lua"
DISC_NAMES = {"us": "USA", "jp": "Japan"}
SESSION = "redux.json"
MAX_REQUEST_LINE = 200   # PCSX-Redux parses each 256-byte read's URL on its own

# Headless defaults. SDL's HIDAPI joystick scan reads every USB device's sysfs
# entries through libusb and blocks forever on a wedged host controller (seen
# on this machine: /sys/bus/usb/devices/usb2); no pad input is needed here.
HEADLESS_ENV = {
    "SDL_JOYSTICK_HIDAPI": "0",
    "SDL_HIDAPI_LIBUSB": "0",
    "SDL_AUDIO_DRIVER": "dummy",
}


class ReduxError(RuntimeError):
    pass


# ------------------------------------------------------------------ symbols --

SYM_RE = re.compile(r"^\s*([A-Za-z_.$][\w.$]*)\s*=\s*(0x[0-9A-Fa-f]+|\d+)\s*;")


def read_symbols(ver: str, modules: list[str]) -> dict[str, int]:
    """name -> address from config/<ver>/symbols/<module>.txt (later modules win)."""
    out: dict[str, int] = {}
    for mod in modules:
        path = ROOT / "config" / ver / "symbols" / f"{mod}.txt"
        if not path.exists():
            raise ReduxError(f"no symbol file {path.relative_to(ROOT)}")
        for line in path.read_text().splitlines():
            m = SYM_RE.match(line)
            if m:
                out[m.group(1)] = int(m.group(2), 0)
    return out


def resolve_address(text: str, ver: str, modules: list[str]) -> int:
    try:
        return int(text, 0)
    except ValueError:
        pass
    syms = read_symbols(ver, modules)
    if text not in syms:
        raise ReduxError(f"unknown symbol {text!r} in {ver} {' '.join(modules)}")
    return syms[text]


# ---------------------------------------------------------------------- png --

def write_png(path: Path, width: int, height: int, rgb_rows: list[bytes]) -> None:
    def chunk(tag: bytes, data: bytes) -> bytes:
        return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", zlib.crc32(tag + data))

    raw = b"".join(b"\x00" + row for row in rgb_rows)
    png = (b"\x89PNG\r\n\x1a\n"
           + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0))
           + chunk(b"IDAT", zlib.compress(raw, 6))
           + chunk(b"IEND", b""))
    path.write_bytes(png)


_BGR555 = None


def vram_to_png(vram: bytes, path: Path, width: int = 1024, height: int = 512) -> None:
    """Write VRAM (15-bit BGR, bit 15 = mask) as a 24-bit PNG, read as 16bpp."""
    global _BGR555
    if _BGR555 is None:
        c5 = [(v << 3) | (v >> 2) for v in range(32)]
        _BGR555 = [bytes((c5[p & 31], c5[(p >> 5) & 31], c5[(p >> 10) & 31])) for p in range(32768)]
    lut = _BGR555
    words = struct.unpack(f"<{width * height}H", vram[: width * height * 2])
    rows = [b"".join(lut[p & 0x7FFF] for p in words[y * width:(y + 1) * width]) for y in range(height)]
    write_png(path, width, height, rows)


# ------------------------------------------------------------------- client --

class Redux:
    """HTTP client for one running PCSX-Redux (its web server + redux_harness.lua)."""

    def __init__(self, port: int, token: str | None = None, rundir: Path = DEFAULT_RUNDIR,
                 host: str = "127.0.0.1", timeout: float = 30.0):
        self.host, self.port, self.token, self.timeout = host, port, token, timeout
        self.rundir = Path(rundir).resolve()

    # -- raw HTTP
    def request(self, method: str, path: str, params: dict | None = None, body: bytes | None = None,
                timeout: float | None = None) -> tuple[int, bytes]:
        url = path
        if params:
            url += "?" + urllib.parse.urlencode(params)
        if len(method) + len(url) + 11 > MAX_REQUEST_LINE:
            raise ReduxError(f"request line too long for PCSX-Redux ({len(url)} bytes): {url[:80]}...")
        conn = http.client.HTTPConnection(self.host, self.port, timeout=timeout or self.timeout)
        try:
            headers = {"Content-Type": "application/octet-stream"} if body is not None else {}
            conn.request(method, url, body=body, headers=headers)
            resp = conn.getresponse()
            return resp.status, resp.read()
        finally:
            conn.close()

    def get(self, path: str, params: dict | None = None, ok: tuple = (200,)) -> bytes:
        status, data = self.request("GET", path, params)
        if status not in ok:
            raise ReduxError(f"GET {path} -> {status}: {data[:200]!r}")
        return data

    def post(self, path: str, params: dict | None = None, body: bytes | None = None) -> bytes:
        status, data = self.request("POST", path, params, body)
        if status != 200:
            raise ReduxError(f"POST {path} -> {status}: {data[:200]!r}")
        return data

    # -- built-in endpoints (src/core/web-server.cc)
    def flow(self) -> dict:
        return json.loads(self.get("/api/v1/execution-flow"))

    def pause(self) -> None:
        self.post("/api/v1/execution-flow", {"function": "pause"})

    def resume(self) -> None:
        self.post("/api/v1/execution-flow", {"function": "resume"})

    def reset(self, hard: bool = True) -> None:
        self.post("/api/v1/execution-flow", {"function": "reset", "type": "hard" if hard else "soft"})

    def ram(self) -> bytes:
        return self.get("/api/v1/cpu/ram/raw")

    def write_ram(self, offset: int, data: bytes) -> None:
        """Write main RAM at a physical offset (0 = 0x80000000), then flush the CPU cache."""
        self.post("/api/v1/cpu/ram/raw", {"offset": offset, "size": len(data)}, data)
        self.post("/api/v1/cpu/cache", {"function": "flush"})

    def vram(self) -> bytes:
        return self.get("/api/v1/gpu/vram/raw")

    def screenshot_png(self) -> bytes:
        return self.get("/api/v1/screen/still")

    def upload_symbols(self, symbols: dict[str, int], reset: bool = True) -> None:
        if reset:
            self.post("/api/v1/assembly/symbols", {"function": "reset"})
        # The parser wants "<hex address without 0x> <name>" per line.
        body = "".join(f"{addr:08X} {name}\n" for name, addr in symbols.items()).encode()
        self.post("/api/v1/assembly/symbols", {"function": "upload"}, body)

    def cd_info(self) -> dict:
        return json.loads(self.get("/api/v1/cd/info"))

    def cd_file(self, name: str) -> bytes:
        """A file from the disc image's ISO9660 tree (read-only; ';1' is added if needed)."""
        status, data = self.request("GET", "/api/v1/cd/files", {"filename": name})
        if status == 404 and ";" not in name:
            status, data = self.request("GET", "/api/v1/cd/files", {"filename": name + ";1"})
        if status != 200:
            raise ReduxError(f"cd file {name} -> {status}: {data[:200]!r}")
        return data

    # -- redux_harness.lua handlers
    def lua(self, handler: str, **params) -> dict:
        if not self.token:
            raise ReduxError("no session token (start the emulator with this script)")
        status, data = self.request("GET", f"/api/v1/lua/xg_{handler}", {"token": self.token, **params})
        try:
            obj = json.loads(data) if data else {}
        except ValueError:
            obj = {"raw": data.decode(errors="replace")}
        if status != 200:
            raise ReduxError(f"xg_{handler} -> {status}: {obj.get('error', obj)}")
        return obj

    def stats(self) -> dict:
        return self.lua("stats")

    def short_path(self, path: Path) -> str:
        """Path as the emulator sees it: relative to the run directory when inside it."""
        path = Path(path).resolve()
        try:
            return str(path.relative_to(self.rundir.resolve()))
        except ValueError:
            return str(path)

    def _tmp(self, suffix: str) -> Path:
        tmp = self.rundir / "tmp"
        tmp.mkdir(exist_ok=True)
        return tmp / f"xg_{secrets.token_hex(4)}{suffix}"

    def save_state(self, path: Path, compress: bool = True) -> dict:
        """Save state; gzipped by default, like the GUI's (File > Load state reads it)."""
        path = Path(path)
        path.parent.mkdir(parents=True, exist_ok=True)
        if not compress:
            return self.lua("state", op="save", path=self.short_path(path))
        raw = self._tmp(".raw")
        try:
            res = self.lua("state", op="save", path=self.short_path(raw))
            path.write_bytes(gzip.compress(raw.read_bytes(), 6))
        finally:
            raw.unlink(missing_ok=True)
        return {**res, "path": str(path), "gzipBytes": path.stat().st_size}

    def load_state(self, path: Path) -> dict:
        """Load a save state, gzipped (GUI, save_state) or raw."""
        path = Path(path)
        with open(path, "rb") as f:
            gz = f.read(2) == b"\x1f\x8b"
        if not gz:
            return self.lua("state", op="load", path=self.short_path(path))
        raw = self._tmp(".raw")
        try:
            raw.write_bytes(gzip.decompress(path.read_bytes()))
            res = self.lua("state", op="load", path=self.short_path(raw))
        finally:
            raw.unlink(missing_ok=True)
        return {**res, "path": str(path)}

    def run_lua(self, code: str) -> list:
        f = self._tmp(".lua")
        f.write_text(code)
        try:
            return self.lua("dofile", path=self.short_path(f)).get("results", [])
        finally:
            f.unlink(missing_ok=True)

    def run_file(self, path: Path) -> list:
        return self.lua("dofile", path=self.short_path(path)).get("results", [])

    def add_breakpoint(self, addr: int, name: str | None = None, pause: bool = False,
                       bptype: str = "Exec", width: int = 4) -> dict:
        params = {"op": "add", "addr": f"0x{addr:08X}", "type": bptype, "width": width}
        if name:
            params["name"] = name
        if pause:
            params["pause"] = 1
        return self.lua("bp", **params)

    def breakpoints(self, op: str = "list", name: str | None = None) -> dict:
        return self.lua("bp", op=op, **({"name": name} if name else {}))

    # -- controller (port 1 or 2; buttons: START X O TRI SQ UP ... joined with '+')
    def pad(self, op: str = "state", port: int = 1, **params) -> dict:
        return self.lua("pad", op=op, port=port, **params)

    def pad_press(self, buttons: str, port: int = 1) -> dict:
        """Hold buttons until pad_release."""
        return self.pad("press", port, buttons=buttons)

    def pad_release(self, buttons: str = "all", port: int = 1) -> dict:
        return self.pad("release", port, buttons=buttons)

    def pad_sequence(self, steps: list[tuple[str, int]], port: int = 1, pause: bool = True) -> dict:
        """Start a sequence of (buttons or '_', vsyncs) steps; it advances while the emulator
        runs. With pause, the emulator pauses when it ends (see run_sequence)."""
        f = self._tmp(".pad")
        f.write_text("".join(f"{b or '_'} {int(n)}\n" for b, n in steps))
        try:
            return self.pad("seq", port, path=self.short_path(f), **({"pause": 1} if pause else {}))
        finally:
            f.unlink(missing_ok=True)

    def run_sequence(self, steps: list[tuple[str, int]], port: int = 1, timeout: float = 600.0) -> dict:
        """Play a pad sequence from the current point and wait until it has ended (paused)."""
        self.pause()
        self.pad_sequence(steps, port, pause=True)
        self.lua("run")
        end = time.monotonic() + timeout
        while time.monotonic() < end:
            if not self.flow()["running"]:
                return self.pad("state", port)
            time.sleep(0.05)
        raise ReduxError(f"sequence still running after {timeout}s")

    def run_vsyncs(self, n: int, timeout: float = 600.0) -> dict:
        """Resume, let the harness pause after n vsyncs (or on a pause breakpoint), wait."""
        self.lua("run", vsyncs=n)
        end = time.monotonic() + timeout
        while time.monotonic() < end:
            if not self.flow()["running"]:
                return self.stats()
            time.sleep(0.05)
        raise ReduxError(f"still running after {timeout}s")

    def set_speed(self, speed: int) -> int:
        """Host pacing: 1 = real time, N = N times, 0 = as fast as possible. Returns the old value.
        Guest timing is cycle-based either way; this only changes how fast the host lets it run."""
        return self.run_lua(f"local s = PCSX.settings.spu; local old = s.Speed; s.Speed = {int(speed)}; return old")[0]

    def quit(self, code: int = 0) -> None:
        try:
            self.lua("quit", code=code)
        except (ConnectionError, http.client.HTTPException, socket.timeout):
            pass


# ------------------------------------------------------------------ session --

def port_in_use(port: int) -> bool:
    with socket.socket() as s:
        s.settimeout(0.5)
        return s.connect_ex(("127.0.0.1", port)) == 0


def pid_alive(pid: int) -> bool:
    try:
        os.kill(pid, 0)
    except ProcessLookupError:
        return False
    except PermissionError:
        return True
    try:  # a zombie child of this very process is not alive
        return Path(f"/proc/{pid}/stat").read_text().split(") ")[-1][0] != "Z"
    except OSError:
        return True


def disc_image(ver: str, disc: int) -> Path:
    name = f"Xenogears ({DISC_NAMES[ver]}) (Disc {disc})"
    return ROOT / "bin" / name / f"{name}.cue"


def start(rundir: Path = DEFAULT_RUNDIR, port: int = 8095, iso: Path | None = None, bios: Path | None = None,
          debugger: bool = False, paused: bool = False, fastboot: bool = False, exe: Path | None = None,
          lua_files: tuple = (), redux_bin: Path = DEFAULT_BIN, audio: bool = False, wait: float = 30.0,
          speed: int | None = None, gui: bool = False, extra_args: tuple = ()) -> tuple[Redux, subprocess.Popen]:
    # The emulator runs inside the run directory: every path it gets is absolute.
    rundir, redux_bin = Path(rundir).resolve(), Path(redux_bin).resolve()
    if not redux_bin.exists():
        raise ReduxError(f"{redux_bin} missing; build it with tools/build_redux.sh")
    rundir.mkdir(parents=True, exist_ok=True)
    sess = rundir / SESSION
    if sess.exists():
        old = json.loads(sess.read_text())
        if pid_alive(old["pid"]):
            raise ReduxError(f"PCSX-Redux already running (pid {old['pid']}, port {old['port']}); stop it first")
    if port_in_use(port):
        raise ReduxError(f"port {port} is in use")
    token = secrets.token_hex(16)
    env = dict(os.environ)
    if not audio:
        env.update(HEADLESS_ENV)
    else:
        env.update({k: v for k, v in HEADLESS_ENV.items() if k != "SDL_AUDIO_DRIVER"})
    env["XG_REDUX_TOKEN"] = token
    # Boolean flags first, then options with values: PCSX-Redux's parser hands
    # a flag the next argument when that argument does not start with '-'.
    cmd = [str(redux_bin)] + ([] if gui else ["-no-ui"]) + ["-webserver"]
    if debugger:
        cmd += ["-debugger", "-interpreter"]
    if fastboot:
        cmd += ["-fastboot"]
    if not paused:
        cmd += ["-run"]
    cmd += ["-portable", str(rundir), "-webserver-port", str(port),
            "-memcard1", str(rundir / "memcard1.mcd"), "-memcard2", str(rundir / "memcard2.mcd"),
            "-dofile", str(HARNESS_LUA)]
    for f in lua_files:
        cmd += ["-dofile", str(Path(f).resolve())]
    if iso:
        cmd += ["-iso", str(Path(iso).resolve())]
    bios = bios or (Path(os.environ["PCSX_REDUX_BIOS"]) if os.environ.get("PCSX_REDUX_BIOS") else None)
    if bios:
        cmd += ["-bios", str(Path(bios).resolve())]
    if exe:
        cmd += ["-exe", str(Path(exe).resolve())]
    cmd += list(extra_args)
    log = open(rundir / "redux.log", "wb")
    proc = subprocess.Popen(cmd, cwd=rundir, env=env, stdin=subprocess.DEVNULL, stdout=log,
                            stderr=subprocess.STDOUT, start_new_session=True)
    log.close()
    sess.write_text(json.dumps({"pid": proc.pid, "port": port, "token": token, "cmd": cmd,
                                "started": time.strftime("%Y-%m-%d %H:%M:%S")}, indent=1))
    sess.chmod(0o600)
    client = Redux(port, token, rundir)
    end = time.monotonic() + wait
    while True:
        if proc.poll() is not None:
            tail = (rundir / "redux.log").read_text(errors="replace")[-2000:]
            raise ReduxError(f"PCSX-Redux exited with {proc.returncode}:\n{tail}")
        try:
            client.lua("ping")
            if speed is not None:
                client.set_speed(speed)
            return client, proc
        except (OSError, http.client.HTTPException, ReduxError):
            if time.monotonic() > end:
                proc.terminate()
                raise ReduxError(f"web server did not answer within {wait}s; see {rundir / 'redux.log'}")
            time.sleep(0.1)


def connect(rundir: Path = DEFAULT_RUNDIR) -> tuple[Redux, dict]:
    rundir = Path(rundir).resolve()
    sess = rundir / SESSION
    if not sess.exists():
        raise ReduxError(f"no session in {rundir}; run `redux.py start` first")
    info = json.loads(sess.read_text())
    if not pid_alive(info["pid"]):
        raise ReduxError(f"PCSX-Redux (pid {info['pid']}) is not running; see {rundir / 'redux.log'}")
    return Redux(info["port"], info["token"], rundir), info


def stop(rundir: Path = DEFAULT_RUNDIR, timeout: float = 10.0) -> int | None:
    client, info = connect(rundir)
    pid = info["pid"]
    client.quit(0)
    end = time.monotonic() + timeout
    while pid_alive(pid) and time.monotonic() < end:
        time.sleep(0.1)
    if pid_alive(pid):
        os.kill(pid, signal.SIGTERM)
        time.sleep(2)
        if pid_alive(pid):
            os.kill(pid, signal.SIGKILL)
    (rundir / SESSION).unlink(missing_ok=True)
    return pid


# ---------------------------------------------------------------- frame rate --

def measure_fps(client: Redux, addr: int, vsyncs: int, refresh: int = 60, name: str = "fps",
                keep: bool = False) -> dict:
    """Count calls of a once-per-frame function over `vsyncs` vsyncs."""
    if not client.flow().get("debugger"):
        raise ReduxError("breakpoints need the debugger: start with --debugger")
    client.add_breakpoint(addr, name)
    before = client.stats()
    after = client.run_vsyncs(vsyncs)
    if not keep:
        client.breakpoints("remove", name)
    hits = after["bps"][name]["hits"] - before["bps"][name]["hits"]
    dv = after["vsyncs"] - before["vsyncs"]
    return {
        "addr": f"0x{addr:08X}", "calls": hits, "vsyncs": dv,
        "fps": hits * refresh / dv if dv else None,
        "guestFPS": after["guestFPS"],
        "cycles": after["cycles"] - before["cycles"],
    }


# ----------------------------------------------------------------- selftest --

def parse_pad_steps(text: str) -> list[tuple[str, int]]:
    """'_*600 START*4 UP+X*2 O' -> [('_', 600), ('START', 4), ('UP+X', 2), ('O', 1)]."""
    steps = []
    for tok in text.replace(",", " ").split():
        buttons, _, n = tok.partition("*")
        if not n:
            n = "1"
        if not n.isdigit() or int(n) < 1:
            raise ReduxError(f"bad step {tok!r}: write BUTTONS*VSYNCS, e.g. START*4 or _*60")
        steps.append((buttons, int(n)))
    if not steps:
        raise ReduxError("empty pad sequence")
    return steps


def _pad_roundtrip(client: Redux) -> str:
    st = client.pad_press("START+X")
    if sorted(st["down"]) != ["CROSS", "START"]:
        raise ReduxError(f"after press: {st}")
    st = client.pad_release("START")
    if st["down"] != ["CROSS"]:
        raise ReduxError(f"after release START: {st}")
    st = client.pad_release()
    if st["down"]:
        raise ReduxError(f"after release all: {st}")
    _expect_error(lambda: client.pad_press("TURBO"), "unknown button")
    return "press/release/state"


def _pad_sequence(client: Redux) -> str:
    start = client.stats()["vsyncs"]
    st = client.run_sequence([("UP", 3), ("_", 2), ("O", 4)])
    took = st["vsyncs"] - start
    if st["down"] or st.get("seq") is not None or took != 9:
        raise ReduxError(f"sequence: {st}, took {took} vsyncs")
    return f"3 steps in {took} vsyncs, paused at the end"


def selftest(args) -> int:
    """Exercise every endpoint that answers without a BIOS; returns 0 on success."""
    rundir = args.rundir / "selftest"
    iso = disc_image(args.ver, 1)
    iso = iso if iso.exists() else None
    failures = 0

    def check(what, fn):
        nonlocal failures
        try:
            res = fn()
            print(f"  ok    {what}" + (f": {res}" if res not in (None, "") else ""))
        except Exception as e:  # noqa: BLE001 - report every failure and go on
            failures += 1
            print(f"  FAIL  {what}: {e}")

    print(f"starting {args.redux_bin} (port {args.port}, run dir {rundir})")
    client, proc = start(rundir, args.port, iso=iso, bios=args.bios, debugger=True, paused=True,
                         redux_bin=args.redux_bin)
    try:
        check("execution-flow", lambda: client.flow())
        check("listens on loopback only", lambda: _check_loopback(args.port))
        check("harness rejects a bad token",
              lambda: _expect_error(lambda: Redux(args.port, "bad", rundir).lua("ping"), "403"))
        check("ping", lambda: client.lua("ping"))
        syms = read_symbols(args.ver, ["main"])
        check(f"upload {len(syms)} main symbols", lambda: client.upload_symbols(syms))
        check("RAM dump", lambda: f"{len(client.ram())} bytes")
        check("RAM write + read back", lambda: _ram_roundtrip(client))
        vram = client.vram()
        check("VRAM dump", lambda: f"{len(vram)} bytes")
        check("VRAM -> PNG", lambda: _png(vram, rundir / "vram.png"))
        check("screenshot", lambda: _screenshot(client, rundir / "screen.png"))
        check("save state", lambda: client.save_state(rundir / "states/selftest.sav"))
        check("load state", lambda: client.load_state(rundir / "states/selftest.sav"))
        check("Lua via file", lambda: client.run_lua("return 1 + 2, 'two', PCSX ~= nil"))
        check("Lua error is reported", lambda: _expect_error(lambda: client.run_lua("error('boom')"), "boom"))
        check("breakpoint add/list/clear", lambda: _bp_roundtrip(client, syms["PutDispEnv"]))
        check("synthetic loop: run 30 vsyncs, count calls (fps path)", lambda: _synthetic_fps(client))
        check("pad: hold, release, state", lambda: _pad_roundtrip(client))
        check("pad: timed sequence", lambda: _pad_sequence(client))
        check("stats", lambda: client.stats())
        if iso:
            check("CD info", lambda: client.cd_info())
            check("CD file SYSTEM.CNF", lambda: client.cd_file("SYSTEM.CNF").decode(errors="replace").split()[:3])
        check("pause/resume", lambda: (client.resume(), client.pause(), client.flow()["running"])[-1])
    finally:
        client.quit(0)
        try:
            rc = proc.wait(10)
        except subprocess.TimeoutExpired:
            proc.kill()
            rc = proc.wait()
        (rundir / SESSION).unlink(missing_ok=True)
    print(f"  {'ok  ' if rc == 0 else 'FAIL'}  quit (exit code {rc})")
    failures += rc != 0
    print("selftest:", "PASS" if not failures else f"{failures} failure(s)")
    return 1 if failures else 0


def _check_loopback(port: int) -> str:
    out = subprocess.run(["ss", "-ltnH", f"sport = :{port}"], capture_output=True, text=True).stdout.split()
    addrs = [w for w in out if w.endswith(f":{port}")]
    if not addrs or any(not a.startswith(("127.0.0.1:", "[::1]:")) for a in addrs):
        raise ReduxError(f"listening on {addrs}")
    return ", ".join(addrs)


def _expect_error(fn, needle: str) -> str:
    try:
        fn()
    except ReduxError as e:
        if needle in str(e):
            return f"rejected ({needle})"
        raise
    raise ReduxError("no error")


def _ram_roundtrip(client: Redux) -> str:
    data = bytes(range(16))
    client.write_ram(0x1FF000, data)
    back = client.ram()[0x1FF000:0x1FF010]
    if back != data:
        raise ReduxError(f"read back {back.hex()}")
    return "16 bytes at 0x801FF000"


def _png(vram: bytes, path: Path) -> str:
    vram_to_png(vram, path)
    return str(path)


def _screenshot(client: Redux, path: Path) -> str:
    data = client.screenshot_png()
    if not data.startswith(b"\x89PNG"):
        raise ReduxError("not a PNG")
    path.write_bytes(data)
    w, h = struct.unpack(">II", data[16:24])
    return f"{w}x{h} (0x0 until the GPU shows something)"


SYNTH_ADDR = 0x80010000
SYNTH_FUNC = SYNTH_ADDR + 0x10
# loop: jal func; nop; j loop; nop; func: jr ra; nop  (stands in for a game's frame loop)
SYNTH_CODE = struct.pack("<6I", 0x0C000000 | ((SYNTH_FUNC >> 2) & 0x3FFFFFF), 0,
                         0x08000000 | ((SYNTH_ADDR >> 2) & 0x3FFFFFF), 0, 0x03E00008, 0)


def _synthetic_fps(client: Redux) -> str:
    """Without a BIOS: put a tiny call loop in RAM, point the PC at it, measure it."""
    client.write_ram(SYNTH_ADDR & 0x1FFFFF, SYNTH_CODE)
    client.run_lua(f"PCSX.getRegisters().pc = 0x{SYNTH_ADDR:08X}")
    res = measure_fps(client, SYNTH_FUNC, 30, name="synthetic")
    pc = int(client.stats()["pc"], 16)
    if res["vsyncs"] != 30 or res["calls"] <= 0 or client.flow()["running"]:
        raise ReduxError(f"unexpected: {res}")
    if not SYNTH_ADDR <= pc < SYNTH_ADDR + len(SYNTH_CODE):
        raise ReduxError(f"pc 0x{pc:08X} left the loop")
    return f"{res['calls']} calls in {res['vsyncs']} vsyncs, {res['cycles']} cycles, paused again"


def _bp_roundtrip(client: Redux, addr: int) -> str:
    client.add_breakpoint(addr, "selftest")
    names = list(client.breakpoints()["bps"])
    client.breakpoints("clear")
    if names != ["selftest"] or client.breakpoints()["bps"]:
        raise ReduxError(f"breakpoints: {names}")
    return f"0x{addr:08X}"


# ---------------------------------------------------------------------- cli --

def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--rundir", type=Path, default=DEFAULT_RUNDIR, help="run directory (default work/redux)")
    ap.add_argument("--ver", choices=sorted(DISC_NAMES), default="us")
    ap.add_argument("--redux-bin", type=Path, default=Path(os.environ.get("PCSX_REDUX", DEFAULT_BIN)))
    sub = ap.add_subparsers(dest="cmd", required=True)

    p = sub.add_parser("start", help="start PCSX-Redux headless")
    p.add_argument("--disc", type=int, default=1)
    p.add_argument("--iso", type=Path, help="disc image (default bin/Xenogears (<ver>) (Disc N)/*.cue)")
    p.add_argument("--no-iso", action="store_true", help="start without a disc")
    p.add_argument("--bios", type=Path, help="BIOS image (or set PCSX_REDUX_BIOS)")
    p.add_argument("--port", type=int, default=8095)
    p.add_argument("--debugger", action="store_true", help="interpreter + debugger (breakpoints, fps)")
    p.add_argument("--paused", action="store_true", help="do not start emulation")
    p.add_argument("--fastboot", action="store_true", help="skip the BIOS logo")
    p.add_argument("--exe", type=Path, help="PS-X EXE to boot instead of the disc's")
    p.add_argument("--lua", type=Path, action="append", default=[], help="extra Lua file for -dofile")
    p.add_argument("--audio", action="store_true", help="play sound (default: SDL dummy driver)")
    p.add_argument("--gui", action="store_true",
                   help="show the emulator window; the API still works and the keyboard drives port 1")
    p.add_argument("--speed", type=int, help="1 = real time (default), N = N times, 0 = unbounded")
    p.add_argument("--symbols", nargs="*", metavar="MODULE", help="upload these symbol files after start")

    sub.add_parser("stop", help="quit PCSX-Redux")
    sub.add_parser("status", help="execution state and harness statistics")
    sub.add_parser("pause")
    p = sub.add_parser("speed", help="host pacing: 1 = real time, N = N times, 0 = unbounded")
    p.add_argument("multiplier", type=int)
    sub.add_parser("resume")
    p = sub.add_parser("reset")
    p.add_argument("--soft", action="store_true")

    p = sub.add_parser("symbols", help="upload config/<ver>/symbols/<module>.txt")
    p.add_argument("modules", nargs="*", default=["main"])
    p.add_argument("--keep", action="store_true", help="add to the current symbols instead of replacing them")

    for name, what in (("ram", "main RAM (2 MB)"), ("vram", "VRAM (.png or raw .bin)"),
                       ("screenshot", "displayed image (PNG)")):
        p = sub.add_parser(name, help=f"dump {what}")
        p.add_argument("out", type=Path)

    p = sub.add_parser("state", help="save or load a save state (gzipped like the GUI's)")
    p.add_argument("op", choices=["save", "load"])
    p.add_argument("path", type=Path, help="relative paths are inside the run directory")

    p = sub.add_parser("run", help="resume for N vsyncs, then pause")
    p.add_argument("vsyncs", type=int)
    p.add_argument("--timeout", type=float, default=600)

    p = sub.add_parser("bp", help="breakpoints (need --debugger)")
    p.add_argument("op", choices=["add", "remove", "clear", "list"])
    p.add_argument("where", nargs="?", help="address or symbol (add)")
    p.add_argument("--name")
    p.add_argument("--pause", action="store_true", help="pause the emulator on a hit")
    p.add_argument("--type", default="Exec", choices=["Exec", "Read", "Write"])
    p.add_argument("--width", type=int, default=4)
    p.add_argument("--modules", nargs="*", default=["main"], help="symbol files used to resolve names")

    p = sub.add_parser("fps", help="game frame rate: calls of a per-frame function per vsync")
    p.add_argument("function", nargs="?", default="PutDispEnv")
    p.add_argument("--vsyncs", type=int, default=300)
    p.add_argument("--refresh", type=int, default=60, help="vsyncs per second (60 NTSC, 50 PAL)")
    p.add_argument("--modules", nargs="*", default=["main"])

    p = sub.add_parser("pad", help="controller input (port 1 by default)")
    p.add_argument("op", choices=["tap", "seq", "press", "release", "stop", "state"],
                   help="tap: hold buttons for --vsyncs and run until done; seq: play timed steps "
                        "(BUTTONS*VSYNCS, _ = none) and run until done; press/release: hold/let go "
                        "(release with no buttons = all); stop: cancel a sequence")
    p.add_argument("what", nargs="?", help="buttons (START X O TRI SQ UP DOWN LEFT RIGHT L1 R1 L2 R2 "
                                          "SELECT, joined with +), or the steps for seq")
    p.add_argument("--port", type=int, default=1, choices=[1, 2])
    p.add_argument("--vsyncs", type=int, default=4, help="tap: how long to hold (default 4)")
    p.add_argument("--after", type=int, default=0, help="tap: vsyncs to keep running after letting go")
    p.add_argument("--no-wait", action="store_true", help="seq: start it and return (it advances while running)")
    p.add_argument("--timeout", type=float, default=600)

    p = sub.add_parser("lua", help="run Lua inside the emulator")
    g = p.add_mutually_exclusive_group(required=True)
    g.add_argument("file", nargs="?", type=Path)
    g.add_argument("-e", "--exec", dest="code")

    p = sub.add_parser("cd", help="disc image: info, or extract a file")
    p.add_argument("name", nargs="?")
    p.add_argument("out", nargs="?", type=Path)

    p = sub.add_parser("selftest", help="start without a BIOS and exercise the API")
    p.add_argument("--port", type=int, default=8097)
    p.add_argument("--bios", type=Path)

    args = ap.parse_args(argv)
    try:
        return run_command(args)
    except ReduxError as e:
        print(f"redux: {e}", file=sys.stderr)
        return 1


def run_command(args) -> int:
    if args.cmd == "selftest":
        return selftest(args)
    if args.cmd == "start":
        iso = None if args.no_iso else (args.iso or disc_image(args.ver, args.disc))
        if iso and not iso.exists():
            raise ReduxError(f"no disc image {iso}")
        client, proc = start(args.rundir, args.port, iso=iso, bios=args.bios, debugger=args.debugger,
                             paused=args.paused, fastboot=args.fastboot, exe=args.exe, lua_files=args.lua,
                             redux_bin=args.redux_bin, audio=args.audio, speed=args.speed,
                             gui=args.gui)
        if args.symbols is not None:
            client.upload_symbols(read_symbols(args.ver, args.symbols or ["main"]))
        print(f"PCSX-Redux pid {proc.pid} on 127.0.0.1:{args.port}; log {args.rundir / 'redux.log'}")
        print(json.dumps(client.flow()))
        return 0
    if args.cmd == "stop":
        print(f"stopped pid {stop(args.rundir)}")
        return 0

    client, info = connect(args.rundir)
    if args.cmd == "status":
        print(json.dumps({"pid": info["pid"], "port": info["port"], **client.flow(), **client.stats()}, indent=1))
    elif args.cmd == "speed":
        print(f"speed {client.set_speed(args.multiplier)} -> {args.multiplier}")
    elif args.cmd == "pause":
        client.pause()
    elif args.cmd == "resume":
        client.resume()
    elif args.cmd == "reset":
        client.reset(hard=not args.soft)
    elif args.cmd == "symbols":
        syms = read_symbols(args.ver, args.modules)
        client.upload_symbols(syms, reset=not args.keep)
        print(f"uploaded {len(syms)} symbols ({args.ver}: {' '.join(args.modules)})")
    elif args.cmd == "ram":
        args.out.write_bytes(client.ram())
    elif args.cmd == "vram":
        data = client.vram()
        if args.out.suffix.lower() == ".png":
            vram_to_png(data, args.out)
        else:
            args.out.write_bytes(data)
    elif args.cmd == "screenshot":
        args.out.write_bytes(client.screenshot_png())
    elif args.cmd == "state":
        path = args.path if args.path.is_absolute() else args.rundir / args.path
        path.parent.mkdir(parents=True, exist_ok=True)
        res = client.save_state(path) if args.op == "save" else client.load_state(path)
        print(json.dumps(res))
    elif args.cmd == "run":
        print(json.dumps(client.run_vsyncs(args.vsyncs, args.timeout)))
    elif args.cmd == "bp":
        if args.op == "add":
            if not args.where:
                raise ReduxError("bp add needs an address or symbol")
            addr = resolve_address(args.where, args.ver, args.modules)
            print(json.dumps(client.add_breakpoint(addr, args.name or args.where, args.pause, args.type,
                                                   args.width)))
        else:
            print(json.dumps(client.breakpoints(args.op, args.name or args.where), indent=1))
    elif args.cmd == "fps":
        addr = resolve_address(args.function, args.ver, args.modules)
        print(json.dumps(measure_fps(client, addr, args.vsyncs, args.refresh), indent=1))
    elif args.cmd == "pad":
        if args.op in ("press", "tap") and not args.what:
            raise ReduxError(f"pad {args.op} needs buttons, e.g. START or UP+X")
        if args.op == "state":
            res = client.pad("state", args.port)
        elif args.op == "press":
            res = client.pad_press(args.what, args.port)
        elif args.op == "release":
            res = client.pad_release(args.what or "all", args.port)
        elif args.op == "stop":
            res = client.pad("stop", args.port)
        else:
            steps = ([(args.what, args.vsyncs)] + ([("_", args.after)] if args.after else [])
                     if args.op == "tap" else parse_pad_steps(args.what or ""))
            if args.op == "seq" and args.no_wait:
                res = client.pad_sequence(steps, args.port, pause=False)
            else:
                res = client.run_sequence(steps, args.port, args.timeout)
        print(json.dumps(res))
    elif args.cmd == "lua":
        res = client.run_file(args.file) if args.file else client.run_lua(args.code)
        print(json.dumps(res))
    elif args.cmd == "cd":
        if not args.name:
            print(json.dumps(client.cd_info(), indent=1))
        else:
            data = client.cd_file(args.name)
            if args.out:
                args.out.write_bytes(data)
            else:
                print(f"{args.name}: {len(data)} bytes")
    return 0


if __name__ == "__main__":
    sys.exit(main())
