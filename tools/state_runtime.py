"""Run release save-state cases on the platform that shipped the game."""
from __future__ import annotations

import base64
import functools
import json
import re
import shlex
import shutil
import subprocess
import tarfile
import tempfile
import time
import uuid
from pathlib import Path, PureWindowsPath

import state_corpus as cs

ANDROID_PACKAGE = "io.github.etheraura.xgport"
ANDROID_ACTIVITY = f"{ANDROID_PACKAGE}/.XgActivity"


def wine_runner(exe: Path, config: dict):
    prefix = Path(config["prefix"]).expanduser().resolve()
    prefix.mkdir(parents=True, exist_ok=True)
    return functools.partial(cs.run_game, exe, launcher=tuple(config["command"]),
                             host_env={"WINEPREFIX": str(prefix), "WINEDEBUG": "-all",
                                       "WINEDLLOVERRIDES": "mscoree,mshtml="})


def external_runner(command: list[str], artifact: Path, exe: Path, runtime: str, region: str, engine: str):
    """Pass a case as JSON to a native runner, with paths on the gate host."""
    def play(out: Path, image: Path, quit_at: int, env: dict) -> int:
        out.mkdir(parents=True, exist_ok=True)
        request = out / "runtime.json"
        cs.write_json(request, {"runtime": runtime, "artifact": str(artifact), "executable": str(exe),
                                "region": region, "engine": engine, "output": str(out),
                                "disc": str(image), "quit_at": quit_at, "env": env})
        with (out / "runner.log").open("w") as log:
            try:
                return subprocess.run([*command, str(request)], stdout=log, stderr=subprocess.STDOUT,
                                      check=False, timeout=1800).returncode
            except (OSError, subprocess.TimeoutExpired) as error:
                log.write(f"{error}\n")
                return 124 if isinstance(error, subprocess.TimeoutExpired) else 127
    return play


class Windows:
    """Run the unmodified Windows ZIP in a Windows x64 guest over OpenSSH."""

    def __init__(self, config: dict):
        self.ssh = config["ssh"]
        self.scp = config["scp"]
        self.host = config["host"]
        self.root = PureWindowsPath(config["work"])
        self.discs = PureWindowsPath(config["discs"])
        self.vulkan_driver = config.get("vulkan_driver")
        if not self.root.is_absolute() or not self.discs.is_absolute():
            raise cs.CorpusError("Windows guest discs and work directories must be absolute")
        if self.powershell("$env:OS; $env:PROCESSOR_ARCHITECTURE").stdout.split() != ["Windows_NT", "AMD64"]:
            raise cs.CorpusError("the Windows runner is not a Windows x64 guest")
        self.powershell(f"New-Item -Force -ItemType Directory -Path {self.literal(self.root)} | Out-Null")
        self.installed: set[str] = set()

    @staticmethod
    def literal(path: PureWindowsPath) -> str:
        return "'" + str(path).replace("'", "''") + "'"

    def powershell(self, script: str, *, timeout: int = 120, check: bool = True):
        encoded = base64.b64encode(("$ErrorActionPreference='Stop'\n" + script).encode("utf-16-le")).decode("ascii")
        proc = subprocess.run([*self.ssh, self.host, "powershell.exe", "-NoProfile", "-NonInteractive",
                               "-EncodedCommand", encoded], capture_output=True, text=True, timeout=timeout)
        if check and proc.returncode:
            raise cs.CorpusError(f"Windows: {proc.stderr.strip()[-800:]}")
        return proc

    def transfer(self, source: str, dest: str):
        proc = subprocess.run([*self.scp, source, dest], capture_output=True, text=True, timeout=300)
        if proc.returncode:
            raise cs.CorpusError(f"Windows scp: {proc.stderr.strip()[-800:]}")

    def remote(self, path: PureWindowsPath) -> str:
        return f"{self.host}:{path.as_posix()}"

    def install(self, artifact: Path, digest: str) -> PureWindowsPath:
        folder = self.root / digest
        archive = folder / artifact.name
        if digest not in self.installed:
            self.powershell(f"New-Item -Force -ItemType Directory -Path {self.literal(folder)} | Out-Null")
            self.transfer(str(artifact), self.remote(archive))
            self.powershell(f"""
if ((Get-FileHash -Algorithm SHA256 -LiteralPath {self.literal(archive)}).Hash -ne '{digest}') {{
    throw 'Windows ZIP checksum mismatch'
}}
Expand-Archive -Force -LiteralPath {self.literal(archive)} -DestinationPath {self.literal(folder)}
""", timeout=300)
            self.installed.add(digest)
        return folder / artifact.stem

    def play(self, artifact: Path, digest: str, exe: Path, out: Path, image: Path, quit_at: int, env: dict) -> int:
        install = self.install(artifact, digest)
        out.mkdir(parents=True, exist_ok=True)
        states, cards = Path(env["XG_STATE_DIR"]), out / "cards"
        for path in (states, cards):
            path.mkdir(parents=True, exist_ok=True)
        job = self.root / f"case-{uuid.uuid4().hex}"
        pad, first, second = Path(env["XG_PAD_SCRIPT"]), image, Path(env["XG_DISC2"])
        options = {**env, "XG_HEADLESS": "1", "SDL_VIDEO_DRIVER": "windows", "SDL_AUDIO_DRIVER": "dummy",
                   "XG_SETTINGS": "none", "XG_CLOCK": "step", "XG_QUIT_AT": str(quit_at),
                   "XG_CARD_DIR": str(job / "cards"), "XG_STATE_DIR": str(job / "states"),
                   "XG_PAD_SCRIPT": str(job / "pad.txt"), "XG_INVARIANT_LOG": str(job / "invariants.log"),
                   "XG_SWEEP_LOG": str(job / "sweep.log"), "XG_DISC2": str(self.discs / second.name)}
        if self.vulkan_driver:
            options["VK_DRIVER_FILES"] = self.vulkan_driver
        self.powershell(f"New-Item -ItemType Directory -Path {self.literal(job)} | Out-Null")
        try:
            with tempfile.TemporaryDirectory(dir=out) as temp:
                stage = Path(temp)
                request = stage / "case.json"
                cs.write_json(request, {"exe": str(install / exe.name), "disc": str(self.discs / first.name), "env": options})
                inputs = stage / "inputs.tar"
                with tarfile.open(inputs, "w") as archive:
                    for path, name in ((states, "states"), (cards, "cards"), (pad, "pad.txt"), (request, "case.json"),
                                       (Path(__file__).with_name("state_windows_run.ps1"), "run.ps1")):
                        archive.add(path, arcname=name)
                self.transfer(str(inputs), self.remote(job / "inputs.tar"))
                result = self.powershell(f"""
Set-Location -LiteralPath {self.literal(job)}
tar.exe -xf inputs.tar
if ($LASTEXITCODE -ne 0) {{ exit $LASTEXITCODE }}
powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass -File ./run.ps1
exit $LASTEXITCODE
""", timeout=660, check=False)
                if result.stderr:
                    (out / "runner.log").write_text(result.stderr)
                outputs = stage / "outputs.tar"
                self.transfer(self.remote(job / "outputs.tar"), str(outputs))
                with tarfile.open(outputs) as archive:
                    archive.extractall(stage, filter="data")
                for path in (states, cards):
                    shutil.copytree(stage / path.name, path, dirs_exist_ok=True)
                for name in ("port.log", "invariants.log", "sweep.log"):
                    if (stage / name).is_file():
                        shutil.copyfile(stage / name, out / name)
                return result.returncode
        finally:
            self.powershell(f"Remove-Item -Recurse -Force -LiteralPath {self.literal(job)}", check=False)


class FreeBSD:
    """Run the exact packaged ELF on a dedicated FreeBSD amd64 SSH runner."""

    def __init__(self, config: dict):
        self.command = config["ssh"]
        self.discs = Path(config["discs"])
        self.root = Path(config["work"])
        self.launcher = config.get("launcher", ["xvfb-run", "-a"])
        if not self.discs.is_absolute() or not self.root.is_absolute():
            raise cs.CorpusError("FreeBSD guest discs and work directories must be absolute")
        if self.ssh("uname", "-sm").stdout.decode().strip() != "FreeBSD amd64":
            raise cs.CorpusError("the FreeBSD runner is not a FreeBSD amd64 guest")
        if self.ssh("uname", "-r").stdout.decode().split(".", 1)[0] != "15":
            raise cs.CorpusError("the FreeBSD runner needs FreeBSD 15")
        self.ssh("mkdir", "-p", str(self.root))

    def ssh(self, *command: str, data: bytes | None = None, check: bool = True,
            timeout: int = 120) -> subprocess.CompletedProcess:
        proc = subprocess.run([*self.command, shlex.join(command)], input=data, capture_output=True, timeout=timeout)
        if check and proc.returncode:
            raise cs.CorpusError(f"FreeBSD: {command[0]}: {proc.stderr.decode(errors='replace').strip()[-600:]}")
        return proc

    def build_package(self, tag: str, source: Path, digest: str, destination: Path, config: dict):
        directory = self.root / f"package-{tag}-{digest[:16]}"
        self.ssh("mkdir", "-p", str(directory))
        archive = str(directory / "source.tar.gz")
        if self.ssh("sha256", "-q", archive, check=False).stdout.decode().strip() != digest:
            self.ssh("sh", "-c", 'cat > "$1"', "sh", archive, data=source.read_bytes(), timeout=300)
        builder = Path(__file__).with_name("state_freebsd_build.sh")
        remote_builder = str(directory / builder.name)
        self.ssh("sh", "-c", 'cat > "$1"', "sh", remote_builder, data=builder.read_bytes())
        command = shlex.join(["nice", "-n", "19", "sh", remote_builder, archive, tag[1:],
                              config["retail"], str(directory), str(config.get("build_jobs", 4))])
        log = destination / "freebsd-build.log"
        with log.open("wb") as output:
            proc = subprocess.run([*self.command, command], stdout=output, stderr=subprocess.STDOUT, timeout=10800)
        if proc.returncode:
            tail = "\n".join(log.read_text(errors="replace").splitlines()[-10:])
            raise cs.CorpusError(f"FreeBSD build failed (exit {proc.returncode})\n{tail}")
        name = f"xenogears-pc-{tag[1:]}-freebsd15-amd64.pkg"
        for file in (name, name + ".sha256"):
            (destination / file).write_bytes(self.ssh("cat", str(directory / "out" / file), timeout=300).stdout)
        package = destination / name
        if (destination / (name + ".sha256")).read_text().split() != [cs.sha256(package), name]:
            raise cs.CorpusError(f"{package}: the FreeBSD builder returned a mismatched package checksum")
        self.ssh("rm", "-rf", str(directory))

    def play(self, exe: Path, schema: Path, out: Path, image: Path, quit_at: int, env: dict) -> int:
        out.mkdir(parents=True, exist_ok=True)
        states = Path(env["XG_STATE_DIR"])
        states.mkdir(parents=True, exist_ok=True)
        cards = out / "cards"
        cards.mkdir(exist_ok=True)
        ident = cs.sha256(exe)
        binaries = self.root / ident
        self.ssh("mkdir", "-p", str(binaries))
        guest_exe = binaries / exe.name
        # The schema is adjacent to the ELF, just as it is in the package.
        for source in (exe, schema):
            dest = str(binaries / source.name)
            digest = self.ssh("sha256", "-q", dest, check=False)
            if digest.stdout.decode().strip() != cs.sha256(source):
                self.ssh("sh", "-c", 'cat > "$1"', "sh", dest, data=source.read_bytes())
        self.ssh("chmod", "700", str(guest_exe))
        job = Path(self.ssh("mktemp", "-d", str(self.root / "case.XXXXXXXX")).stdout.decode().strip())
        try:
            self.ssh("mkdir", "-p", str(job / "cards"), str(job / "states"))
            for path in (states, cards):
                inputs = subprocess.run(["tar", "-cf", "-", "-C", str(path), "."], capture_output=True, check=True)
                self.ssh("tar", "-xf", "-", "-C", str(job / path.name), data=inputs.stdout)
            self.ssh("sh", "-c", 'cat > "$1"', "sh", str(job / "pad.txt"), data=Path(env["XG_PAD_SCRIPT"]).read_bytes())
            disc2 = Path(env["XG_DISC2"])
            options = {**env, "XG_HEADLESS": "1", "XG_CLOCK": "step", "XG_SETTINGS": "none",
                       "XG_QUIT_AT": str(quit_at), "XG_CARD_DIR": str(job / "cards"), "SDL_AUDIO_DRIVER": "dummy",
                       "XG_STATE_DIR": str(job / "states"), "XG_PAD_SCRIPT": str(job / "pad.txt"),
                       "XG_INVARIANT_LOG": str(job / "invariants.log"), "XG_SWEEP_LOG": str(job / "sweep.log"),
                       "XG_DISC2": str(self.discs / disc2.parent.name / disc2.name)}
            run = self.ssh("timeout", "600", *self.launcher, "env", *[f"{k}={v}" for k, v in options.items()],
                           str(guest_exe), str(self.discs / image.parent.name / image.name), check=False, timeout=630)
            (out / "port.log").write_bytes(run.stdout + run.stderr)
            for what in ("invariants.log", "sweep.log"):
                pulled = self.ssh("cat", str(job / what), check=False)
                if not pulled.returncode:
                    (out / what).write_bytes(pulled.stdout)
            for path in (states, cards):
                with tempfile.TemporaryDirectory(dir=out) as temp:
                    response = self.ssh("tar", "-cf", "-", "-C", str(job / path.name), ".")
                    subprocess.run(["tar", "-xf", "-", "-C", temp], input=response.stdout, check=True)
                    shutil.copytree(temp, path, dirs_exist_ok=True)
            return run.returncode
        finally:
            self.ssh("rm", "-rf", str(job), check=False)


class Android:
    """A dedicated debuggable Android test system capable of running the release ABI."""

    def __init__(self, config: dict, abi: str):
        self.abi = abi
        self.command = [str(Path(config.get("adb", "~/Android/sdk/platform-tools/adb")).expanduser()),
                        "-s", config["serial"]]
        self.timeout = int(config.get("timeout", 900))
        self.installed: Path | None = None
        self.discs: set[Path] = set()
        self.root = f"/data/user/0/{ANDROID_PACKAGE}/files/xg-state-gate"
        if self.shell("getprop ro.debuggable").strip() != "1":
            raise cs.CorpusError(f"{self.command[-1]}: the Android runner needs a dedicated debuggable device")
        abis = self.shell("getprop ro.product.cpu.abilist").strip().split(",")
        if self.abi not in abis:
            raise cs.CorpusError(f"{self.command[-1]}: Android supports {', '.join(abis)}, requires {self.abi}")
        if int(self.shell("getprop ro.build.version.sdk").strip()) < 33:
            raise cs.CorpusError(f"{self.command[-1]}: the Android runner needs Android 13 or later")
        self.adb("root")
        self.adb("wait-for-device")
        if self.shell("id -u").strip() != "0" or self.shell("getprop sys.boot_completed").strip() != "1":
            raise cs.CorpusError(f"{self.command[-1]}: Android must be booted and allow adb root")

    def adb(self, *args: str, check: bool = True, timeout: int = 120) -> str:
        try:
            p = subprocess.run([*self.command, *args], stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                               text=True, errors="replace", timeout=timeout)
        except (OSError, subprocess.TimeoutExpired) as error:
            raise cs.CorpusError(f"{self.command[-1]}: adb {args[0]}: {error}") from error
        if check and p.returncode:
            raise cs.CorpusError(f"{self.command[-1]}: adb {args[0]}: {p.stdout.strip()[-600:]}")
        return p.stdout

    def shell(self, *args: str, check: bool = True) -> str:
        return self.adb("shell", shlex.join(args) if len(args) > 1 else args[0], check=check)

    def install(self, apk: Path):
        if self.installed == apk:
            return
        self.shell("am", "force-stop", ANDROID_PACKAGE)
        self.adb("install", "--no-incremental", "--abi", self.abi, "-r", "-d", str(apk), timeout=300)
        details = self.shell("dumpsys", "package", ANDROID_PACKAGE)
        abi = re.search(r"(?m)^\s*primaryCpuAbi=(\S+)", details)
        uid = re.search(r"(?m)^\s*(?:userId|appId)=(\d+)", details)
        if abi is None or abi[1] != self.abi or uid is None:
            raise cs.CorpusError(f"{apk}: installed package did not report the {self.abi} ABI and app UID")
        self.uid = uid[1]
        self.installed = apk

    def disc(self, cue: Path) -> str:
        guest = f"{self.root}/discs/{cue.parent.name}"
        if cue not in self.discs:
            self.shell("mkdir", "-p", guest)
            tracks = re.findall(r'(?mi)^\s*FILE\s+"([^"]+)"', cue.read_text())
            if not tracks or any(Path(track).name != track for track in tracks):
                raise cs.CorpusError(f"{cue}: expected quoted disc tracks beside the cue")
            for name in [cue.name, *dict.fromkeys(tracks)]:
                self.adb("push", str(cue.parent / name), f"{guest}/{name}", timeout=300)
            self.prepare_guest_files()
            self.discs.add(cue)
        return guest + "/" + cue.name

    def prepare_guest_files(self):
        self.shell("chown", "-R", f"{self.uid}:{self.uid}", self.root)
        self.shell("restorecon", "-RF", self.root)

    def play(self, apk: Path, region: str, engine: str, out: Path, image: Path, quit_at: int, env: dict) -> int:
        self.install(apk)
        out.mkdir(parents=True, exist_ok=True)
        local_states = Path(env["XG_STATE_DIR"])
        local_states.mkdir(parents=True, exist_ok=True)
        local_cards = out / "cards"
        local_cards.mkdir(exist_ok=True)
        # A single runner owns the device. Each case gets its own paths and the
        # Android loader reads the same launch environment as an ordinary app.
        with tempfile.TemporaryDirectory(prefix="xg-android-", dir=out) as temporary:
            name = Path(temporary).name
            raw = f"{self.root}/{name}"
            self.shell("mkdir", "-p", f"{raw}/cards", f"{raw}/states")
            try:
                self.adb("push", str(local_states) + "/.", f"{raw}/states")
                self.adb("push", str(local_cards) + "/.", f"{raw}/cards")
                self.adb("push", env["XG_PAD_SCRIPT"], f"{raw}/pad.txt")
                self.prepare_guest_files()
                options = {**env, "XG_ENGINE": engine, "XG_HEADLESS": "1", "XG_CLOCK": "step",
                           "XG_SETTINGS": "none", "XG_QUIT_AT": str(quit_at), "XG_BIOS": "none",
                           "XG_STATE_DIR": f"{raw}/states", "XG_CARD_DIR": f"{raw}/cards",
                           "XG_PAD_SCRIPT": f"{raw}/pad.txt", "XG_INVARIANT_LOG": f"{raw}/invariants.log",
                           "XG_SWEEP_LOG": f"{raw}/sweep.log", "XG_DISC": self.disc(image),
                           "XG_DISC2": self.disc(Path(env["XG_DISC2"]))}
                self.shell("am", "force-stop", ANDROID_PACKAGE)
                launched = self.shell("am", "start", "-W", "-n", ANDROID_ACTIVITY,
                                      "--es", "region", region, "--es", "discs-dir", f"{self.root}/discs",
                                      "--es", "env", ";".join(f"{k}={v}" for k, v in options.items()))
                if "Status: ok" not in launched:
                    raise cs.CorpusError(f"{apk}: Android did not launch: {launched.strip()}")
                pid, log = self.finish()
                (out / "port.log").write_text(log)
                for what in ("invariants.log", "sweep.log"):
                    self.adb("pull", f"{raw}/{what}", str(out / what), check=False)
                self.adb("pull", f"{raw}/states/.", str(local_states))
                self.adb("pull", f"{raw}/cards/.", str(local_cards))
                quit_at_target = re.search(rf"(?m)^quit at: vblank {quit_at}\b", log) is not None
                match = re.search(r"(?m)^loader: the game returned (\d+)", log)
                if match:
                    return int(match[1]) if quit_at_target else 1
                if not pid:
                    return 124
                # Packaged apps exit from the game library after draining
                # logcat. The loader never sees that return.
                for _ in range(5):
                    exits = self.shell("dumpsys", "activity", "exit-info", ANDROID_PACKAGE)
                    record = next((s for s in exits.split("ApplicationExitInfo")
                                   if re.search(rf"\bpid={pid}\b", s)), "")
                    if record:
                        (out / "android-exit.log").write_text(record)
                        clean_exit = re.search(r"reason=1 \(EXIT_SELF\).*\bstatus=0\b", record) is not None
                        return 0 if quit_at_target and clean_exit else 1
                    time.sleep(1)
                return 124
            finally:
                self.shell("am", "force-stop", ANDROID_PACKAGE, check=False)
                self.shell("rm", "-rf", raw, check=False)

    def finish(self) -> tuple[str, str]:
        deadline = time.monotonic() + self.timeout
        pid = ""
        log = ""
        while time.monotonic() < deadline:
            active = self.shell("pidof", ANDROID_PACKAGE, check=False).strip()
            if active.isdecimal() and not pid:
                pid = active
            if pid:
                log = self.adb("logcat", "-d", "--pid", pid, "-v", "raw", "-s", "xenogears:V")
                if "loader: the game returned " in log:
                    return pid, log
                if active != pid:
                    # The library can exit before the final buffered log
                    # messages are visible to the logcat reader.
                    time.sleep(0.5)
                    log = self.adb("logcat", "-d", "--pid", pid, "-v", "raw", "-s", "xenogears:V")
                    return pid, log
            time.sleep(2)
        return pid, log
