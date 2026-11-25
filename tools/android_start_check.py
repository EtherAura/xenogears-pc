#!/usr/bin/env python3
"""Start the Android app on emulators before it is handed out (xg-o1dx).

    python tools/android_start_check.py --apk APP-x86_64.apk [--twin APP-arm64-v8a.apk]
        --disc us=DISC1.bin [--disc jp=DISC1.bin] [--avd NAME=SYSTEM_IMAGE[=load|game] ...]
        [--runs us:original,jp:enhanced] [--quit-at N] [--work DIR]

A release's APK once died in its activity's onCreate on every device (xg-vei1), and nothing in the
release build had started it: the payload audit, the 16 KB alignment, the state schemas and the
signature all look at files. This installs the x86_64 APK on each Android Virtual Device (x86_64,
headless, on KVM) once the system has finished starting, and starts it:

  first start   as a player's: no options and nothing chosen yet (the app opens the system's folder
                picker). The app must still be running FIRST_WAIT seconds later, with no crash.
  runs          region:engine (by default every one the APK carries, for the regions given a disc:
                us:original, us:enhanced, jp:original, jp:enhanced), no settings file, no BIOS, the
                update check off. On a "game" AVD with the region's Disc 1 in the app's own folder and
                XG_QUIT_AT: the loader must load that engine's library for the region, and the game must
                reach kernelMain and quit by itself at that vblank. On a "load" AVD, one whose emulated
                GPU has no Vulkan 1.3 device the game can use (the Android 13 image's), without a disc:
                the loader must load the library and the game open its window.
  Each run changes the theme while the game is up (a fabricated overlay of one system colour, turned
  on and off, as a new wallpaper's dynamic colours change the resource overlays): the game must go
  on. Any run fails on a crash, and on the activity being made again in its process (a
  configuration change the manifest does not take), which ends an SDL app (xg-7xkb).

The default AVDs: xg-check-a13 (Android 13, the app's minimum: load) and xg-check-a16 (a current
Android with 16 KB pages: game).

--twin names the arm64-v8a APK (an emulator here runs x86_64 only): its classes.dex,
AndroidManifest.xml and resources.arsc must be the x86_64 APK's byte for byte, so that starting one
covers the Java side of both.

An AVD that does not exist is made from its system image (avdmanager); a system image that is not
installed is an error naming the sdkmanager command. Each emulator is started for the check and
stopped after it (its own port; nothing else on the machine is touched). The package is uninstalled
before it is installed: these AVDs are the check's own. Logs go to --work (default
build/android/start-check), one per AVD and run.

Exit 0 when every start passed, 1 when one failed, 2 when the check could not run (no SDK, image,
KVM or disc).
"""
from __future__ import annotations

import argparse
import grp
import os
import pwd
import re
import shlex
import shutil
import signal
import socket
import subprocess
import sys
import time
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SDK = Path(os.environ.get("ANDROID_HOME") or Path.home() / "Android" / "sdk")
BUILD_TOOLS = "36.1.0"
# (name, system image, mode): the Android 13 image's emulated Vulkan has no device the game can use
# (SwiftShader without Vulkan 1.3), so there the libraries are loaded and the game's window opened.
DEFAULT_AVDS = (
    ("xg-check-a13", "system-images;android-33;google_apis;x86_64", "load"),         # the app's minimum
    ("xg-check-a16", "system-images;android-36.1;google_apis_ps16k;x86_64", "game"),  # current, 16 KB pages
)
MODES = ("load", "game")
ENGINES = ("original", "enhanced")
AVD_CONFIG = {"hw.ramSize": "4096", "disk.dataPartition.size": "8G", "hw.cpu.ncore": "4"}
TWIN_ENTRIES = ("classes.dex", "AndroidManifest.xml", "resources.arsc")
THEME_OVERLAY = "com.android.shell:xgcheck"  # Emulator.make_theme_overlay
FIRST_WAIT = 15      # seconds the first start must live
BOOT_TIMEOUT = 300   # seconds for an emulator to boot
SETTLE_MIN = 30      # seconds after the boot before the app starts, at least
SETTLE_STABLE = 20   # seconds the resource overlays must stay unchanged before it starts
SETTLE_TIMEOUT = 150 # seconds at most for that
START_TIMEOUT = 30   # seconds for the app's process to appear
LOAD_TIMEOUT = 60    # seconds for a load run's lines
RUN_TIMEOUT = 420    # seconds for a game run to quit by itself


class SetupError(Exception):
    """The check cannot run here (exit 2), as opposed to a start that failed (exit 1)."""


def say(text: str) -> None:
    print(f"android_start_check: {text}", flush=True)


def tool(*parts: str) -> Path:
    path = SDK.joinpath(*parts)
    if not path.exists():
        raise SetupError(f"no {path} (the Android SDK: ANDROID_HOME, default ~/Android/sdk)")
    return path


def run(cmd: list[str], timeout: float = 120, check: bool = False, **kw) -> subprocess.CompletedProcess:
    proc = subprocess.run(cmd, capture_output=True, text=True, errors="replace", timeout=timeout, **kw)
    if check and proc.returncode != 0:
        raise SetupError(f"{shlex.join(cmd)}: exit {proc.returncode}: {(proc.stdout + proc.stderr).strip()[-300:]}")
    return proc


# ---- what the logs must and must not say (pure: tools/test_android_start_check.py) ----------------

LINE = re.compile(r"^\S+\s+\S+\s+(\d+)\s+(\d+)\s+([VDIWEF])\s+(.*?)\s*: (.*)$")  # logcat -v threadtime


def parse(log: str) -> list[tuple[int, str, str]]:
    """(pid, tag, message) of each logcat -v threadtime line."""
    out = []
    for line in log.splitlines():
        m = LINE.match(line)
        if m:
            out.append((int(m.group(1)), m.group(4), m.group(5)))
    return out


def app_pids(log: str, package: str) -> set[int]:
    """The app's processes as the system started them ("Start proc PID:PACKAGE/...")."""
    pids = set()
    for _, _, msg in parse(log):
        m = re.search(r"Start proc (\d+):" + re.escape(package) + r"(?:[/:]|\s|$)", msg)
        if m:
            pids.add(int(m.group(1)))
    return pids


def crashes(log: str, package: str, pids: set[int]) -> list[str]:
    """Lines that say the app crashed: a Java exception that ended one of its processes, a fatal
    signal in one, a native crash dump of one, or an ANR. Its processes are the package's own and
    its ":name" ones (RestartActivity's), never another package that only starts with its name."""
    name = re.escape(package) + r"(?::[\w.]+)?"
    named = re.compile(rf"Process: {name}, PID|>>> {name} <<<|ANR in {name}(?:\s|\(|$)")
    found = []
    for pid, tag, msg in parse(log):
        if named.search(msg) or ("FATAL EXCEPTION" in msg or "Fatal signal" in msg) and pid in pids:
            found.append(f"{tag}: {msg}")
    return found


def relaunched(log: str, pids: set[int]) -> list[str]:
    """SDL's word that its activity was made again in a process whose game had ended: Android
    destroyed the activity for a configuration change it does not take (resource overlays, for one,
    which no app can take), and SDL ends the game when its activity is destroyed."""
    return [f"{tag}: {msg} (the activity was made again after a configuration change; SDL ended the game)"
            for pid, tag, msg in parse(log) if tag == "SDL" and msg == "activity finished" and pid in pids]


def assets_seq(dumpsys: str) -> int | None:
    """The global configuration's assets sequence (" as.N" in dumpsys window's mGlobalConfiguration
    line): it grows each time the system's resource overlays change."""
    m = re.search(r" as\.(\d+)", dumpsys)
    return int(m.group(1)) if m else None


def missing(log: str, want: list[tuple[str, str]]) -> list[str]:
    text = "\n".join(msg for _, _, msg in parse(log))
    return [why for line, why in want if line not in text]


def load_wants(library: str) -> list[tuple[str, str]]:
    """What a load run must log: the loader's line, and port/src/gpu/present.c's for the window."""
    return [(f"loader: {library} at ", f"the loader did not load {library}"),
            ("present: window ", "the game did not open its window")]


def game_wants(library: str) -> list[tuple[str, str]]:
    """What a game run must log: the loader's line, port/src/main.c's, and quit_at.c's own line for
    the end at XG_QUIT_AT's vblank. Its "xenogears: quit" is not enough: SDL's quit when Android
    destroyed the activity prints that too (xg-7xkb)."""
    return [(f"loader: {library} at ", f"the loader did not load {library}"),
            ("xenogears: entering kernelMain", "the game did not reach kernelMain"),
            ("quit at: vblank ", "the game did not play to its vblank and quit there (XG_QUIT_AT)")]


def game_missing(log: str, library: str) -> list[str]:
    return missing(log, game_wants(library))


def twin_differences(apk: Path, twin: Path) -> list[str]:
    """TWIN_ENTRIES that are not byte for byte the same in both APKs."""
    diffs = []
    with zipfile.ZipFile(apk) as a, zipfile.ZipFile(twin) as b:
        for name in TWIN_ENTRIES:
            try:
                same = a.read(name) == b.read(name)
            except KeyError:
                same = False
            if not same:
                diffs.append(name)
    return diffs


# ---- AVDs and emulators ---------------------------------------------------------------------

def avd_home() -> Path:
    if os.environ.get("ANDROID_AVD_HOME"):
        return Path(os.environ["ANDROID_AVD_HOME"])
    user = os.environ.get("ANDROID_USER_HOME") or os.environ.get("ANDROID_EMULATOR_HOME")
    return Path(user) / "avd" if user else Path.home() / ".android" / "avd"


def ensure_avd(name: str, image: str) -> None:
    """The AVD, made from its system image when it does not exist yet."""
    ini = avd_home() / f"{name}.ini"
    if ini.is_file():
        return
    if not SDK.joinpath(*image.split(";")).is_dir():
        raise SetupError(f"AVD {name}: system image {image} is not installed "
                         f"(sdkmanager --install '{image}')")
    say(f"AVD {name}: made from {image}")
    run([str(tool("cmdline-tools", "latest", "bin", "avdmanager")), "create", "avd", "-n", name, "-k", image,
         "-d", "pixel_7"], input="no\n", timeout=300, check=True)
    folder = None
    for line in ini.read_text(encoding="utf-8").splitlines():
        if line.startswith("path="):
            folder = Path(line[5:])
    if folder is None or not (folder / "config.ini").is_file():
        raise SetupError(f"AVD {name}: avdmanager made no config.ini")
    lines = [line for line in (folder / "config.ini").read_text(encoding="utf-8").splitlines()
             if line.split("=", 1)[0] not in AVD_CONFIG]
    lines += [f"{key}={value}" for key, value in AVD_CONFIG.items()]
    (folder / "config.ini").write_text("\n".join(lines) + "\n", encoding="utf-8")


def with_kvm(cmd: list[str]) -> list[str]:
    """The emulator needs /dev/kvm: as it is when this process may open it, else through sg kvm when the
    user is in the kvm group without this session having it (a group added after login)."""
    if os.access("/dev/kvm", os.R_OK | os.W_OK):
        return cmd
    user = pwd.getpwuid(os.getuid()).pw_name
    try:
        members = grp.getgrnam("kvm").gr_mem
    except KeyError:
        members = []
    if user in members and shutil.which("sg"):
        return ["sg", "kvm", "-c", shlex.join(cmd)]
    raise SetupError("no access to /dev/kvm (the emulator needs KVM: the kvm group)")


def free_port(adb: Path) -> int:
    """An even console port from 5600 on that no emulator uses and nothing listens on (the port and the
    next, adb's)."""
    listed = run([str(adb), "devices"], timeout=30).stdout
    for port in range(5600, 5682, 2):
        if f"emulator-{port}" in listed:
            continue
        try:
            for p in (port, port + 1):
                with socket.socket() as s:
                    s.bind(("127.0.0.1", p))
        except OSError:
            continue
        return port
    raise SetupError("no free emulator port in 5600-5681")


class Emulator:
    """One headless emulator of an AVD, started for the check and always stopped after it."""

    def __init__(self, name: str, work: Path):
        self.name, self.work = name, work
        self.adb = tool("platform-tools", "adb")
        self.port = free_port(self.adb)
        self.serial = f"emulator-{self.port}"
        self.proc: subprocess.Popen | None = None

    def __enter__(self) -> "Emulator":
        cmd = [str(tool("emulator", "emulator")), "-avd", self.name, "-port", str(self.port), "-no-window",
               "-no-audio", "-no-snapshot", "-no-boot-anim", "-no-metrics", "-gpu", "swiftshader_indirect"]
        log = open(self.work / f"{self.name}-emulator.log", "wb")
        self.proc = subprocess.Popen(with_kvm(cmd), stdout=log, stderr=subprocess.STDOUT, stdin=subprocess.DEVNULL,
                                     start_new_session=True)
        log.close()
        try:
            self.wait_boot()
            self.adb_run("root", timeout=60)
            self.adb_run("wait-for-device", timeout=60)
            self.wait_boot()
            self.wait_settled()
        except BaseException:
            self.stop()
            raise
        return self

    def __exit__(self, *exc) -> None:
        self.stop()

    def wait_boot(self) -> None:
        start = time.time()
        while time.time() - start < BOOT_TIMEOUT:
            if self.proc is not None and self.proc.poll() is not None:
                raise SetupError(f"the emulator of {self.name} ended (exit {self.proc.returncode}): "
                                 f"{self.work / (self.name + '-emulator.log')}")
            if self.shell("getprop sys.boot_completed", timeout=15).stdout.strip() == "1":
                return
            time.sleep(3)
        raise SetupError(f"the emulator of {self.name} did not boot in {BOOT_TIMEOUT} s")

    def wait_settled(self) -> None:
        """Until the system's resource overlays have stopped changing after the boot: the global
        configuration's assets sequence (dumpsys window, " as.N") the same for SETTLE_STABLE seconds,
        and SETTLE_MIN seconds at least. While they change, Android makes every activity again; both
        images change them a few seconds after BOOT_COMPLETED (Config changes=80000000), and on the
        Android 13 one the app's activity was made again 3 s after its start, so SDL ended the game."""
        start = since = time.time()
        last = None
        while time.time() - start < SETTLE_TIMEOUT:
            seq = self.assets()
            if seq != last:
                last, since = seq, time.time()
            elif seq is not None and time.time() - start >= SETTLE_MIN and time.time() - since >= SETTLE_STABLE:
                return
            time.sleep(2)
        say(f"{self.name}: the resource overlays still changed {SETTLE_TIMEOUT} s after the boot; going on "
            f"(a relaunch fails a run)")

    def assets(self) -> int | None:
        return assets_seq(self.shell("dumpsys window | grep -m1 mGlobalConfiguration", timeout=30).stdout)

    def make_theme_overlay(self) -> None:
        """A fabricated overlay of one system colour (THEME_OVERLAY, disabled; needs adb root): turned on
        and off, it changes the resource overlays as a dynamic-colour theme does (a new wallpaper's
        colours), which made Android recreate the activity and SDL end the game (xg-7xkb)."""
        self.shell(shlex.join(["cmd", "overlay", "fabricate", "--target", "android", "--name",
                               THEME_OVERLAY.split(":")[1], "android:color/system_accent1_100", "0x1c",
                               "0xff336699"]))
        self.shell(f"cmd overlay disable {THEME_OVERLAY}")
        if THEME_OVERLAY not in self.shell("cmd overlay list android").stdout:
            raise SetupError(f"{self.name}: could not make the theme overlay (cmd overlay fabricate)")

    def theme_change(self) -> list[str]:
        """The theme overlay turned on and off while the game runs: two changes of the resource
        overlays. Problems if the system's assets sequence did not move (the change did not happen)."""
        before = self.assets()
        self.shell(f"cmd overlay enable {THEME_OVERLAY}")
        time.sleep(2)
        self.shell(f"cmd overlay disable {THEME_OVERLAY}")
        time.sleep(2)
        after = self.assets()
        if before is None or after is None or after <= before:
            return [f"the theme change did not happen (assets sequence {before} to {after})"]
        return []

    def stop(self) -> None:
        if self.proc is None:
            return
        run([str(self.adb), "-s", self.serial, "emu", "kill"], timeout=30)
        try:
            self.proc.wait(timeout=60)
        except subprocess.TimeoutExpired:
            for sig in (signal.SIGTERM, signal.SIGKILL):
                try:
                    os.killpg(self.proc.pid, sig)
                    self.proc.wait(timeout=20)
                    break
                except (ProcessLookupError, subprocess.TimeoutExpired):
                    continue
        self.proc = None

    def adb_run(self, *args: str, timeout: float = 120) -> subprocess.CompletedProcess:
        try:
            return run([str(self.adb), "-s", self.serial, *args], timeout=timeout)
        except subprocess.TimeoutExpired:
            return subprocess.CompletedProcess(args, 124, "", "timed out")

    def shell(self, command: str, timeout: float = 60) -> subprocess.CompletedProcess:
        return self.adb_run("shell", command, timeout=timeout)

    def pid(self, package: str) -> int | None:
        out = self.shell(f"pidof {shlex.quote(package)}", timeout=15).stdout.split()
        return int(out[0]) if out and out[0].isdigit() else None

    def log(self) -> str:
        return self.adb_run("logcat", "-d", "-b", "main,system,crash", "-v", "threadtime", timeout=60).stdout


# ---- the check ------------------------------------------------------------------------------

def launcher_of(apk: Path) -> tuple[str, str]:
    """The package and its launcher activity's class, by name: a test build's package has a suffix
    (tools/build_android.sh --id-suffix) that its classes do not, so "PACKAGE/.XgActivity" names
    no class there."""
    out = run([str(tool("build-tools", BUILD_TOOLS, "aapt2")), "dump", "badging", str(apk)], check=True).stdout
    package = re.search(r"^package: name='([^']+)'", out, re.M)
    activity = re.search(r"^launchable-activity: name='([^']+)'", out, re.M)
    if not package or not activity:
        raise SetupError(f"{apk.name}: aapt2 names no package or no launcher activity")
    return package.group(1), activity.group(1)


def carried(apk: Path) -> set[str]:
    """The game libraries the APK carries (libxenogears[-enhanced]-<region>.so)."""
    with zipfile.ZipFile(apk) as z:
        return {Path(n).name for n in z.namelist() if re.fullmatch(r"lib/[^/]+/libxenogears-.*\.so", n)}


def library(region: str, engine: str) -> str:
    return f"libxenogears-{'enhanced-' if engine == 'enhanced' else ''}{region}.so"


class Check:
    def __init__(self, apk: Path, discs: dict[str, Path], runs: list[tuple[str, str]], quit_at: int, work: Path):
        self.apk, self.discs, self.runs, self.quit_at, self.work = apk, discs, runs, quit_at, work
        self.package, self.activity = launcher_of(apk)
        self.files = f"/sdcard/Android/data/{self.package}/files"
        self.failed: list[str] = []

    def result(self, avd: str, what: str, problems: list[str], log: str, note: str = "") -> None:
        name = f"{avd}-{what.replace(':', '-').replace(' ', '-')}.log"
        (self.work / name).write_text(log, encoding="utf-8")
        if problems:
            self.failed.append(f"{avd} {what}")
            say(f"FAIL {avd} {what}: {'; '.join(problems[:4])} (log {self.work / name})")
        else:
            say(f"pass {avd} {what}{': ' + note if note else ''}")

    def start(self, emu: Emulator, extras: list[str]) -> int | None:
        emu.shell(f"am force-stop {self.package}")
        emu.adb_run("logcat", "-b", "all", "-c")
        emu.shell(shlex.join(["am", "start", "-n", f"{self.package}/{self.activity}", *extras]))
        start = time.time()
        while time.time() - start < START_TIMEOUT:
            pid = emu.pid(self.package)
            if pid is not None:
                return pid
            time.sleep(0.5)
        return None

    def judge(self, log: str, seen: set[int]) -> tuple[list[str], set[int]]:
        """Crashes and relaunches of the app's processes (those the system started, and those seen)."""
        pids = app_pids(log, self.package) | {p for p in seen if p}
        return crashes(log, self.package, pids) + relaunched(log, pids), pids

    def gone(self, pid: int | None, pids: set[int]) -> list[str]:
        if pid is not None:
            return []
        return ["the app's process ended at once" if pids else "the app's process never appeared"]

    def env(self, engine: str, *more: str) -> str:
        return ";".join(["XG_SETTINGS=none", "XG_BIOS=none", "XG_UPDATE_CHECK=off", f"XG_ENGINE={engine}", *more])

    def first_start(self, emu: Emulator, avd: str) -> None:
        pid = self.start(emu, [])
        time.sleep(FIRST_WAIT)
        alive = emu.pid(self.package)
        top = emu.shell("dumpsys activity activities | grep -m1 topResumedActivity").stdout.strip()
        log = emu.log()
        problems, pids = self.judge(log, {pid, alive})
        problems = self.gone(pid, pids) + problems
        if pid is not None and alive is None:
            problems.append(f"the app is not running {FIRST_WAIT} s after its start")
        self.result(avd, "first start", problems, log, f"running after {FIRST_WAIT} s ({top.split()[-2] if top else '?'})")
        emu.shell(f"am force-stop {self.package}")

    def until_window(self, emu: Emulator, pid: int | None, seen: set, timeout: float) -> str:
        """The log once the game's window is open (present.c's line), or the app crashed or ended,
        or the time is up."""
        start, log = time.time(), ""
        while pid is not None and time.time() - start < timeout:
            time.sleep(1)
            seen.add(emu.pid(self.package))
            log = emu.log()
            if "present: window " in log or self.judge(log, seen)[0] or emu.pid(self.package) is None:
                break
        return log

    def load_run(self, emu: Emulator, avd: str, region: str, engine: str) -> None:
        """The region's engine library loaded and the game's window open, without a disc (the game
        then waits on its setup screen, or on its message that there is no Vulkan device), and the
        game still there after a theme change."""
        lib = library(region, engine)
        pid = self.start(emu, ["--es", "discs-dir", "/nonexistent", "--es", "region", region,
                               "--es", "env", self.env(engine)])
        seen: set = {pid}
        log = self.until_window(emu, pid, seen, LOAD_TIMEOUT)
        theme = []
        if "present: window " in log and emu.pid(self.package) is not None:
            theme = emu.theme_change()
            time.sleep(3)  # a crash or a relaunch after it
        alive = emu.pid(self.package)
        seen.add(alive)
        log = emu.log()
        problems, pids = self.judge(log, seen)
        problems = self.gone(pid, pids) + problems + missing(log, load_wants(lib)) + theme
        if pid is not None and alive is None:
            problems.append("the app ended")
        self.result(avd, f"{region}:{engine}", problems, log, f"{lib} loaded, the window open, a theme change kept")
        emu.shell(f"am force-stop {self.package}")

    def push_discs(self, emu: Emulator, avd: str) -> bool:
        """Each region's Disc 1 into the app's own folder (files/discs/<region>), owned by the app."""
        if emu.shell(f"test -d {self.files} && echo yes").stdout.strip() != "yes":
            # The first start's game thread makes it (SDL's external storage path); if it did not
            # come up behind the picker, a start without discs makes it.
            self.start(emu, ["--es", "discs-dir", "/nonexistent", "--es", "env", self.env("original")])
            time.sleep(10)
            emu.shell(f"am force-stop {self.package}")
        owner = emu.shell(f"stat -c %u:%g {self.files}").stdout.strip()
        if not re.fullmatch(r"\d+:\d+", owner):
            self.result(avd, "discs", [f"the app made no {self.files}"], "")
            return False
        for region, disc in self.discs.items():
            folder = f"{self.files}/discs/{region}"
            emu.shell(f"mkdir -p {folder}")
            push = emu.adb_run("push", str(disc), f"{folder}/{disc.name}", timeout=900)
            if push.returncode != 0:
                self.result(avd, "discs", [f"adb push {disc.name}: {(push.stdout + push.stderr).strip()[-200:]}"], "")
                return False
        emu.shell(f"chown -R {owner} {self.files}/discs")
        return True

    def game_run(self, emu: Emulator, avd: str, region: str, engine: str) -> None:
        pid = self.start(emu, ["--es", "discs-dir", f"{self.files}/discs/{region}", "--es", "region", region,
                               "--es", "env", self.env(engine, f"XG_QUIT_AT={self.quit_at}")])
        start, seen = time.time(), {pid}
        # A theme change while the game plays (xg-7xkb): it must go on and still quit by itself.
        theme = ["the game's window never opened, so no theme change"]
        if "present: window " in self.until_window(emu, pid, seen, LOAD_TIMEOUT) and emu.pid(self.package):
            theme = emu.theme_change()
        while pid is not None and time.time() - start < RUN_TIMEOUT:
            time.sleep(2)
            now = emu.pid(self.package)
            if now is None:
                break
            seen.add(now)
        running = emu.pid(self.package) is not None
        log = emu.log()
        problems, pids = self.judge(log, seen)
        problems = self.gone(pid, pids) + problems + game_missing(log, library(region, engine)) + theme
        if running:
            problems.append(f"still running after {RUN_TIMEOUT} s")
        self.result(avd, f"{region}:{engine}", problems, log, f"a theme change kept, quit at vblank "
                    f"{self.quit_at} after {time.time() - start:.0f} s")
        emu.shell(f"am force-stop {self.package}")

    def on(self, avd: str, mode: str) -> None:
        say(f"{avd}: booting")
        with Emulator(avd, self.work) as emu:
            release = emu.shell("getprop ro.build.version.release").stdout.strip()
            sdk = emu.shell("getprop ro.build.version.sdk").stdout.strip()
            say(f"{avd}: Android {release} (API {sdk}), {emu.serial}, {mode} runs")
            emu.adb_run("uninstall", self.package, timeout=120)
            inst = emu.adb_run("install", "--no-incremental", str(self.apk), timeout=600)
            if "Success" not in inst.stdout:
                self.result(avd, "install", [(inst.stdout + inst.stderr).strip()[-300:]], "")
                return
            emu.make_theme_overlay()
            self.first_start(emu, avd)
            if mode == "load":
                for region, engine in self.runs:
                    self.load_run(emu, avd, region, engine)
            elif self.runs and self.push_discs(emu, avd):
                for region, engine in self.runs:
                    self.game_run(emu, avd, region, engine)
            emu.adb_run("uninstall", self.package, timeout=120)


def parse_avd(text: str) -> tuple[str, str, str]:
    """NAME, NAME=SYSTEM_IMAGE or NAME=SYSTEM_IMAGE=MODE (load or game; default game)."""
    name, image, mode = (text.split("=", 2) + ["", ""])[:3]
    mode = mode or "game"
    if not name or mode not in MODES:
        raise SetupError(f"--avd {text}: NAME[=SYSTEM_IMAGE[=load|game]]")
    return name, image, mode


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--apk", required=True, type=Path, help="the x86_64 APK")
    ap.add_argument("--twin", type=Path, help="the arm64-v8a APK: its Java and manifest must be the x86_64 APK's")
    ap.add_argument("--disc", action="append", default=[], metavar="REGION=IMAGE",
                    help="a region's Disc 1 image (.bin), us or jp")
    ap.add_argument("--avd", action="append", default=[], metavar="NAME=SYSTEM_IMAGE[=load|game]",
                    help="an AVD, the system image it is made from when missing, and its runs (default: "
                         + ", ".join(f"{n}={i}={m}" for n, i, m in DEFAULT_AVDS) + ")")
    ap.add_argument("--runs", help="region:engine pairs, comma separated (default: every engine the APK carries "
                                   "for every region given a disc; 'none' for the first start only)")
    ap.add_argument("--quit-at", type=int, default=300, help="the vblank each game run quits at (XG_QUIT_AT)")
    ap.add_argument("--work", type=Path, default=ROOT / "build" / "android" / "start-check")
    a = ap.parse_args()
    try:
        if not a.apk.is_file():
            raise SetupError(f"no APK {a.apk}")
        if a.twin is not None:
            if not a.twin.is_file():
                raise SetupError(f"no APK {a.twin}")
            diffs = twin_differences(a.apk, a.twin)
            if diffs:
                say(f"FAIL {a.twin.name}: {', '.join(diffs)} not the same as in {a.apk.name}: the start of "
                    f"the x86_64 APK does not cover it")
                return 1
            say(f"pass {a.twin.name}: {', '.join(TWIN_ENTRIES)} the same as {a.apk.name}'s")
        discs: dict[str, Path] = {}
        for item in a.disc:
            region, _, image = item.partition("=")
            if region not in ("us", "jp") or not image:
                raise SetupError(f"--disc {item}: REGION=IMAGE, us or jp")
            if not Path(image).is_file():
                raise SetupError(f"--disc {item}: no such file")
            discs[region] = Path(image).resolve()
        libs = carried(a.apk)
        if a.runs is None:
            runs = [(r, e) for r in discs for e in ENGINES if library(r, e) in libs]
        elif a.runs == "none":
            runs = []
        else:
            runs = []
            for pair in a.runs.split(","):
                region, _, engine = pair.strip().partition(":")
                if region not in discs or engine not in ENGINES:
                    raise SetupError(f"--runs {pair}: REGION:ENGINE with a --disc for the region, engine "
                                     f"original or enhanced")
                if library(region, engine) not in libs:
                    raise SetupError(f"--runs {pair}: {a.apk.name} has no {library(region, engine)}")
                runs.append((region, engine))
        discs = {r: d for r, d in discs.items() if any(r == region for region, _ in runs)}
        avds = [parse_avd(x) for x in a.avd] or list(DEFAULT_AVDS)
        a.work.mkdir(parents=True, exist_ok=True)
        check = Check(a.apk.resolve(), discs, runs, a.quit_at, a.work.resolve())
        say(f"{a.apk.name} ({check.package}): first start, then "
            f"{', '.join(f'{r}:{e}' for r, e in runs) or 'no runs'}, on "
            f"{', '.join(f'{n} ({m})' for n, _, m in avds)}")
        for name, image, _ in avds:
            if image:
                ensure_avd(name, image)
            elif not (avd_home() / f"{name}.ini").is_file():
                raise SetupError(f"no AVD {name} (give it as NAME=SYSTEM_IMAGE to make it)")
        for name, _, mode in avds:
            check.on(name, mode)
    except SetupError as exc:
        say(f"cannot run: {exc}")
        return 2
    if check.failed:
        say(f"{len(check.failed)} failed: {', '.join(check.failed)}")
        return 1
    say("every start passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
