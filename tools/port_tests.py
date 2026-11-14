#!/usr/bin/env python3
"""Build and run every PC port unit test with the arguments it needs.

    python tools/port_tests.py [--build build/pc/release] [--no-build]
                               [--no-gpu] [-k SUBSTR] [--list] [--jobs N]

The tests are EXCLUDE_FROM_ALL targets in port/CMakeLists.txt plus the
standalone SPU and CD loader tests (port/src/spu/test/build.sh,
port/src/cd/test/build.sh). Each runs headless
(XG_SETTINGS=none, XG_HEADLESS=1, SDL offscreen video and dummy audio) with
its own scratch directory under build/port_tests/. Only selected tests reset
their scratch. One invocation owns an output directory at a time; a second
fails immediately (use XG_PORT_TESTS_OUT for a separate run). Settings tests run their
'check' mode in one directory and 'write' then 'read' in another.

Some tests need things that are never committed: the disc images under bin/
(Redump names, as in config/<ver>/disc.yaml) or fixtures cut from them under
work/ (work/regional/button-vram-*.bin, work/issue-portraits/<ver>-d<n>/). Without them the test is skipped,
not failed. GPU tests need a Vulkan 1.3 device; --no-gpu skips them.

EXTRAS are scripted runs of `xenogears --extras` (XG_EXTRAS_SCRIPT) whose
output must match a pattern: they need the disc image and a Vulkan device.

KNOWN lists failures tracked in beads: they print as 'known' and don't fail
the run.

--jobs N runs independent tests side by side (default: the CPU count, at
most 8; --jobs 1 runs every test one after another, in order). Tests that
share a scratch directory or an executable stay in their order, one at a time;
at most GPU_SLOTS (2) GPU tests share the Vulkan device; CLOCK tests (the wall
clock) and ALONE ones run with nothing else running. A test that fails while
sharing the host is rerun alone; if it then passes, its line says it failed
only when shared and later runs keep it alone (OUT/alone-only.txt; move it to
ALONE).

GPU tests run holding the Intel GPU lock (/tmp/xg-intel-gpu.lock, or
XG_GPU_LOCK; flock, as tools/gpu_lock.sh and the art chapters' run.sh take
it), shared by this run's GPU tests and released whenever none is running, so
another process's GPU jobs never run beside one of them. Not on Windows.

Prints one line per test, in the same order whatever --jobs is, and a summary
starting with 'port-tests'. Exit status: the number of failed tests.
"""

from __future__ import annotations

import argparse
from concurrent.futures import ThreadPoolExecutor
from contextlib import ExitStack, contextmanager
import os
import queue
import re
import shutil
import subprocess
import sys
import threading
import time
from pathlib import Path

try:
    import fcntl
except ImportError:  # Windows (the MinGW port runs this suite too): msvcrt's byte-range lock
    fcntl = None
    import msvcrt

ROOT = Path(__file__).resolve().parent.parent
# Logs and scratch: XG_PORT_TESTS_OUT, else XG_GATES_OUT's port_tests (parallel
# gate runs keep their own), else build/port_tests.
OUT = Path(os.environ.get("XG_PORT_TESTS_OUT") or
           (Path(os.environ["XG_GATES_OUT"]) / "port_tests" if os.environ.get("XG_GATES_OUT") else
            ROOT / "build" / "port_tests")).resolve()
TIMEOUT = 300
# --jobs: the default's cap, and the most a caller may ask for.
DEFAULT_JOBS_CAP = 8
MAX_JOBS = 64


class OutputBusy(Exception):
    pass


def try_lock(handle) -> bool:
    """Lock the open lock file without waiting; False if another run holds it."""
    if fcntl is not None:
        try:
            fcntl.flock(handle, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError:
            return False
        return True
    # Its first byte (beyond the end of the empty file is allowed); Windows,
    # like flock, drops the lock when the owning process exits.
    handle.seek(0)
    try:
        msvcrt.locking(handle.fileno(), msvcrt.LK_NBLCK, 1)
    except OSError:
        return False
    return True


def unlock(handle) -> None:
    if fcntl is not None:
        fcntl.flock(handle, fcntl.LOCK_UN)
    else:
        handle.seek(0)
        msvcrt.locking(handle.fileno(), msvcrt.LK_UNLCK, 1)


@contextmanager
def output_lock():
    """Own logs and fixtures through build and execution; crashes release it.

    Keep the lock file: unlinking it would let a new invocation lock another
    inode while an existing invocation still owns this one.
    """
    OUT.mkdir(parents=True, exist_ok=True)
    with (OUT / ".lock").open("a+") as handle:
        if not try_lock(handle):
            raise OutputBusy(f"{display_path(OUT)} is in use by another port-tests run; "
                             "set XG_PORT_TESTS_OUT to a different directory")
        try:
            yield
        finally:
            unlock(handle)


def display_path(path: Path) -> Path:
    return path.relative_to(ROOT) if path.is_relative_to(ROOT) else path


# The Intel GPU lock every headless game run on that GPU shares (tools/gpu_lock.sh, the
# art chapters' run.sh): XG_GPU_LOCK, else this. Two jobs on the device at once from
# different processes can reset it (xg-dby).
GPU_LOCK = "/tmp/xg-intel-gpu.lock"


def gpu_lock_path() -> str:
    return os.environ.get("XG_GPU_LOCK") or GPU_LOCK


class GpuLock:
    """This run's hold of the GPU lock: taken while at least one GPU test runs.

    The GPU tests this runner shares the device among (GPU_SLOTS) share one
    flock; another process using the GPU (an art capture) runs before the
    first or after the last of them, never beside one. CPU-only tests never
    take it. Opened read-only, like tools/gpu_lock.sh: the file may belong to
    another account, and with fs.protected_regular an O_CREAT open of it in
    /tmp is refused. The descriptor is not inherited (O_CLOEXEC), so a test's
    own children never hold it. No lock on Windows (no flock).
    """

    def __init__(self):
        self.mutex = threading.Lock()  # held while waiting for the flock: other GPU tests wait behind it
        self.users = 0
        self.fd: int | None = None

    @staticmethod
    def take(path: str) -> int:
        try:
            fd = os.open(path, os.O_RDONLY | os.O_CLOEXEC)
        except FileNotFoundError:  # the first user makes it (tools/gpu_lock.sh's mode)
            try:
                os.close(os.open(path, os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_CLOEXEC, 0o644))
            except FileExistsError:
                pass
            fd = os.open(path, os.O_RDONLY | os.O_CLOEXEC)
        try:
            fcntl.flock(fd, fcntl.LOCK_EX)
        except BaseException:
            os.close(fd)
            raise
        return fd

    @contextmanager
    def held(self):
        with self.mutex:
            if self.users == 0:
                self.fd = self.take(gpu_lock_path())
            self.users += 1
        try:
            yield
        finally:
            with self.mutex:
                self.users -= 1
                if self.users == 0:
                    fd, self.fd = self.fd, None
                    try:
                        fcntl.flock(fd, fcntl.LOCK_UN)
                    finally:
                        os.close(fd)


gpu_lock = GpuLock()

# GPU tests whose own children take the GPU lock (test_battle_speed.py runs the game
# under tools/gpu_lock.sh on the Intel GPU): holding it around them would deadlock.
SELF_LOCKED = {"battle-speed"}

# test -> beads issue; a failure prints as 'known' and doesn't count.
# (gputest's intermittent 2x resident texture/CLUT failures, xg-htn.35.83,
# were a wall-clock vblank switching the upscaled copy off mid-test; fixed in
# gputest's useScale.)
KNOWN: dict[str, str] = {}

# Tests that open a Vulkan device (xenogears: the extras' scripted checks).
GPU = {
    "celgputest",
    "orderedgputest", "cpueffecttest", "highrefreshtest", "autoresolutiontest", "interpolationgputest", "distortion-replay", "transitiontest",
    "distortiontest", "geometrytest", "glyphgputest", "gputest",
    "moviepacktest", "presentfiltertest", "presenttest", "promptgputest", "slangtest", "texturefiltertest",
    "textureedgetest", "texturepackgputest", "portraitgputest", "texturetest", "verticaltest", "xenogears",
}

# Tests whose result depends on the host's real clock (keyed by test name, or
# like GPU): netplaytest (loopback peers and wall-clock pauses), hostpolltest
# (a timer 4 ms ahead of the next vblank, XG_CLOCK unset), battlespeedtest
# (the hardware clock's rate against SDL's), high-refresh (its real and poll
# clocks: host samples against real deadlines, SDL_Delay gaps), battle-hw-pacing
# (highrefreshtest's real clock: vblanks due over a 25 ms delay), battle-speed
# (4x on the real clock: vblank spacing, the sound device never dry). Other
# load on the host could fail them.
CLOCK = {"netplaytest", "hostpolltest", "battlespeedtest", "high-refresh", "battle-hw-pacing", "battle-speed"}

# Tests that failed only while sharing the host (the report says so, and the
# run lists them in OUT/ALONE_FILE so that later runs there keep them alone):
# move them here, by test name.
ALONE: set[str] = set()
ALONE_FILE = "alone-only.txt"

# With --jobs > 1: CLOCK and ALONE tests run with nothing else running; at
# most GPU_SLOTS GPU tests share the Vulkan device at a time.
GPU_SLOTS = 2

# Scripted runs of the extras (xenogears --extras with XG_EXTRAS_SCRIPT): name
# -> (script, a regular expression its output must match).
EXTRAS = {
    # Home > Visuals > Gallery (xg-htn.35.91): the gallery opened from the
    # visual library lists the movies and scenes its disc scan finds.
    "extras-visuals-gallery": (
        "2 open visuals\n4 do section gallery\n6 do wait\n8 leave\n",
        r"extras: Gallery: [1-9]\d* movies, [1-9]\d* places the game plays them, [1-9]\d* scenes"),
    # The gallery with a gamepad (xg-htn.35.93): the cursor starts in the
    # movie list, Left there stays in the gallery, L1/R1 change its tabs
    # (wrapping) and Back returns to the library's index.
    "extras-gallery-gamepad": (
        "2 open visuals\n4 do section gallery\n6 do wait\n10 nav open\n12 pad left\n16 nav left\n"
        "18 pad r1\n24 nav r1\n26 pad l1\n30 pad l1\n36 nav l1l1\n38 pad b\n44 nav back\n46 leave\n",
        r"(?s)nav open: \w+ ##extras/##gmovies.*nav left: \w+ ##extras/##gmovies.*"
        r"nav r1: \w+ ##extras/##gpics.*nav l1l1: \w+ ##extras/##gscenes.*nav back: \w+ ##extras/##sections"),
    # The bestiary (xg-htn.35.25): the enemies, their battles and a row's details read from the disc.
    "extras-bestiary": (
        "2 open bestiary\n4 do wait\n6 do info\n8 do select #3\n10 do info\n12 leave\n",
        r"(?s)bestiary: [1-9]\d* entries .* from [1-9]\d* records, [1-9]\d* places.*bestiary: selected .*: [1-9]\d* records, [1-9]\d* places"),
    # The battle scene's moves (xg-htn.35.29/30): Fei's commands, a Weltall
    # deathblow against a Gear and an enemy's scripted turn each play their
    # native sequence to its end.
    "extras-battle-moves": (
        "2 open battle\n4 do setup 0 0 0\n6 do catalog\n8 do move 0 3 5 7 0 1\n10 do finish 3000\n"
        "12 do setup 0 0 2 1 0\n14 do catalog\n16 do move 0 3 5 3 0 1\n18 do finish 3000\n"
        "20 do move 3 0 6 0 0 1\n22 do finish 3000\n24 leave\n",
        r"(?s)battle catalog: actor 0 form 0 slot 0 enemy 0/0: 7 attacks, 13 deathblows, 0 infinity, 8 ethers.*"
        r"battle finish: actor 0 form 0 enemy 0/0 move 5 7 0: ended.*"
        r"battle catalog: actor 0 form 1 slot 0 enemy 2/0: 6 attacks, 9 deathblows, 3 infinity, 6 ethers.*"
        r"battle finish: actor 0 form 1 enemy 2/0 move 5 3 0: ended.*"
        r"battle finish: actor 0 form 1 enemy 2/0 move 6 0 0: ended"),
}

# Tests that take no arguments.
PLAIN = [
    "orderedgputest", "gameidtest",
    "autoresolutiontest",
    "netplaytest", "interpolationtest",
    "cd_drivetest", "disc_setuptest", "disc_verifytest", "distortiontest", "extras_battlefxtest",
    "extras_battlemovestest", "extras_bookmarktest", "extras_enemyspritestest",
    "extras_field_assetstest", "extras_fieldlocationstest",
    "extras_field_scriptstest", "extras_manifesttest",
    "extras_navigationtest", "extras_observetest", "extras_ratestest",
    "extras_spritecataloguetest", "extras_writertest", "gallery_battletest",
    "gallery_mediatest", "gallery_scenetest", "geometrytest",
    "gte_precisiontest", "jpegtest", "mdectest", "model_geometrytest", "thin_geometrytest", "slidestacktest", "retailheaptest", "rumbletest", "texturecachetest", "provenancetest",
    "gametext_spritetest",
    "cpueffecttest", "textureedgetest", "texturefiltertest", "texturetest", "ui_l10ntest", "ui_motiontest", "verticaltest",
    "state_codectest", "rewindpacktest", "seamreusetest", "spansteptest", "battlepacingtest", "spu_outputtest",
    "hostpolltest", "battlespeedtest", "backuptest",
]

# Settings tests: DIR check, then DIR write and DIR read in a fresh DIR.
SETTINGS = [
    "gameplay_bugfixtest", "gameplay_combattest", "gameplay_deathblowtest",
    "gameplay_dialoguetest", "gameplay_encountertest", "gameplay_field60slowtest", "gameplay_gauntlettest",
    "gameplay_retrytest",
    "gameplay_rewardtest",
]

def disc(region: str, n: int) -> Path:
    name = f"Xenogears ({region}) (Disc {n})"
    return ROOT / "bin" / name / f"{name}.cue"


def build_extras(build: Path) -> bool:
    cache = build / "CMakeCache.txt"
    text = cache.read_text(errors="replace") if cache.exists() else ""
    return re.search(r"^XG_EXTRAS:\w+=(OFF|0|FALSE|NO)$", text, re.M | re.I) is None


def build_version(build: Path) -> str:
    cache = build / "CMakeCache.txt"
    if cache.exists():
        m = re.search(r"^XG_VERSION:\w+=(\w+)", cache.read_text(errors="replace"), re.M)
        if m:
            return m.group(1).upper()
    return "US"


class Job:
    """One test subprocess: what to run and what it shares with other tests."""

    def __init__(self, name: str, argv: list[str], env: dict, timeout: int, expect: str | None,
                 key: str, resources: set, alone: bool = False):
        self.name, self.argv, self.env, self.timeout, self.expect = name, argv, env, timeout, expect
        self.key = key              # GPU, CLOCK and KNOWN's key: the target, else the executable
        self.gpu = key in GPU       # one of GPU_SLOTS on the device
        self.alone = alone or bool({name, key} & (CLOCK | ALONE))  # nothing else running
        self.resources = resources  # tests sharing one stay in order on one worker


class Test:
    """A line of the report: a skip or a failure known while planning, or a Job's result."""

    def __init__(self, name: str, job: Job | None = None, result: tuple[str, str] | None = None):
        self.name, self.job, self.result = name, job, result


def read_alone() -> set[str]:
    """The tests earlier runs in OUT found failing only when shared."""
    path = OUT / ALONE_FILE
    return {x.strip() for x in path.read_text().splitlines() if x.strip()} if path.exists() else set()


def run_job(job: Job) -> tuple[str, str]:
    """Run one test to its end (killed at its timeout); its status and detail.

    Touches only the job's own log; the worker threads call it. A GPU test
    runs holding the GPU lock (its timeout and time start once it has it).
    """
    log = OUT / f"{job.name}.log"
    log.parent.mkdir(parents=True, exist_ok=True)
    with ExitStack() as held:
        if job.gpu and job.name not in SELF_LOCKED and fcntl is not None:
            try:
                held.enter_context(gpu_lock.held())
            except OSError as e:  # the lock file cannot be opened or locked: the test did not run
                return "FAIL", f"GPU lock {gpu_lock_path()}: {e}"
        start = time.monotonic()
        try:
            with log.open("w") as f:
                p = subprocess.run(job.argv, env=job.env,
                                   stdout=f, stderr=subprocess.STDOUT, timeout=job.timeout,
                                   cwd=OUT)
            code = p.returncode
        except subprocess.TimeoutExpired:
            code = "timeout"
        secs = time.monotonic() - start
    text = log.read_text(errors="replace")
    lines = [x for x in text.splitlines() if x.strip()]
    last = lines[-1] if lines else "(no output)"
    if code == 0 and job.expect is not None:
        m = re.search(job.expect, text)
        if m is None:
            code = "no match"
        else:
            last = m.group(0)
    if code == 0:
        return "ok", f"{last[:90]} ({secs:.1f}s)"
    if job.key in KNOWN:
        return "known", f"{KNOWN[job.key]}: {last[:60]}"
    return "FAIL", f"exit {code}: {last[:90]} (log {display_path(log)})"


def chains(tests: list[Test]) -> list[list[Test]]:
    """Group the tests that share a resource (transitively), each group in its order."""
    parent = list(range(len(tests)))

    def find(i: int) -> int:
        while parent[i] != i:
            parent[i] = parent[parent[i]]
            i = parent[i]
        return i

    owner: dict = {}
    for i, t in enumerate(tests):
        for r in sorted(t.job.resources):
            if r in owner:
                parent[find(i)] = find(owner[r])
            else:
                owner[r] = i
    groups: dict[int, list[Test]] = {}
    for i, t in enumerate(tests):
        groups.setdefault(find(i), []).append(t)
    return sorted(groups.values(), key=lambda g: tests.index(g[0]))


class Runner:
    def __init__(self, build: Path, gpu: bool, pattern: str | None, listing: bool, jobs: int = 1):
        self.build = build
        self.gpu = gpu
        self.pattern = pattern
        self.listing = listing
        self.jobs = jobs
        self.results: list[tuple[str, str, str]] = []  # name, status, detail
        self.tests: list[Test] = []    # every report line, in the order planned
        self.printed = 0               # tests[:printed] are in results and printed
        self.scratches: list[Path] = []
        self.alone_only = read_alone()  # ALONE_FILE's tests, run alone like ALONE's
        # The scheduler's decisions, in order (main thread): ("start" | "alone" | "retry",
        # test, the tests running then).
        self.trace: list[tuple[str, str, frozenset]] = []
        self.env = dict(os.environ, XG_SETTINGS="none", XG_HEADLESS="1",
                        SDL_VIDEO_DRIVER="offscreen", SDL_AUDIO_DRIVER="dummy")

    def scratch(self, name: str, *tests: str) -> Path:
        """Reset a fixture only if one of its consumers is selected.

        Some fixtures have multiple consumers (write then read); prepare them
        once per group, preserving their contents between subprocesses.
        Planning resets them all before any test runs; the tests naming one
        run in order on one worker.
        """
        d = OUT / "scratch" / name
        if not self.listing and any(self.wanted(t) for t in (tests or (name,))):
            shutil.rmtree(d, ignore_errors=True)
            d.mkdir(parents=True)
        self.scratches.append(d)
        return d

    def wanted(self, name: str) -> bool:
        return self.pattern is None or self.pattern in name

    def record(self, name: str, status: str, detail: str) -> None:
        self.tests.append(Test(name, result=(status, detail)))
        self.flush()

    def flush(self) -> None:
        """Print the finished tests at the head of the plan (on the main thread only)."""
        while self.printed < len(self.tests) and self.tests[self.printed].result is not None:
            t = self.tests[self.printed]
            status, detail = t.result
            self.results.append((t.name, status, detail))
            print(f"  {status:5} {t.name:34} {detail}", flush=True)
            self.printed += 1

    def skip(self, name: str, why: str) -> None:
        if self.wanted(name):
            self.record(name, "skip", why)

    def run(self, name: str, exe: Path, *args, env: dict | None = None,
            target: str | None = None, expect: str | None = None, timeout: int = TIMEOUT) -> None:
        """Plan a test; execute() runs it."""
        if not self.wanted(name):
            return
        if self.listing:
            print(f"  {name:34} {exe.name} {' '.join(map(str, args))}")
            return
        key = target or exe.name
        if key in GPU and not self.gpu:
            self.record(name, "skip", "--no-gpu")
            return
        if not exe.exists():
            self.record(name, "FAIL", f"not built: {exe}")
            return
        argv = [str(exe), *map(str, args)]
        env = env or self.env
        # Runs of one executable stay in order (a test may keep files beside
        # its scratch, in OUT); the interpreter's scripts are told apart by name.
        resources = {("exe", name if target is None and exe == Path(sys.executable) else key)}
        values = [*argv[1:], *env.values()]
        for d in map(str, self.scratches):
            if any(v == d or v.startswith(d + os.sep) for v in values):
                resources.add(("scratch", d))
        self.tests.append(Test(name, Job(name, argv, env, timeout, expect, key, resources,
                                         alone=name in self.alone_only)))

    def all(self) -> None:
        self.plan()
        self.execute()

    def execute(self) -> None:
        """Run the planned tests and report them in their planned order.

        --jobs 1: one after another on this thread. Otherwise run_parallel().
        """
        pending = [t for t in self.tests if t.result is None]
        if self.jobs == 1:
            for t in pending:
                t.result = run_job(t.job)
                self.flush()
        elif pending:
            self.run_parallel(chains(pending))
        lost = [t.name for t in self.tests if t.result is None]
        if lost or self.printed != len(self.tests):
            raise RuntimeError(f"port-tests: no result for {' '.join(lost) or '?'}")

    def run_parallel(self, groups: list[list[Test]]) -> None:
        """Run the resource chains side by side, each in its order.

        This thread owns every decision and all the state: which tests run
        (running, and so the GPU slots), which shared the host, the retries,
        the results and the report. Workers only run one test's subprocess
        (run_job) and hand back its result or exception; a test leaves running
        (its slot free) when that arrives, an exception included.

        CLOCK and ALONE tests, and the reruns, run on this thread only when
        nothing is running, and nothing starts until they end: then no other
        test subprocess exists. While one waits, no new test starts. A test
        that fails having shared the host (another test running at some point
        during it) is rerun alone: still failing, it fails; passing, the report
        says it failed only when shared and ALONE_FILE keeps it alone from then
        on. A worker's exception stops new tests from starting and is raised
        here once every running test (each ended by its timeout at worst) has
        finished.
        """
        done: queue.SimpleQueue = queue.SimpleQueue()  # (test, result, exception), from the workers
        heads = [0] * len(groups)          # groups[i][heads[i]] is the group's next test
        running: dict[Test, int] = {}      # test -> its group
        shared: set[Test] = set()          # ran while another test ran
        retries: list[tuple[Test, int, tuple[str, str]]] = []  # failed shared, to rerun alone
        error: BaseException | None = None

        def work(t: Test) -> None:
            try:
                done.put((t, run_job(t.job), None))
            except BaseException as e:
                done.put((t, None, e))

        def finished(i: int, t: Test, result: tuple[str, str]) -> None:
            t.result = result
            heads[i] += 1
            self.flush()

        def left(i: int) -> int:  # the group's remaining time limits: the longest first
            return sum(t.job.timeout for t in groups[i][heads[i]:])

        with ThreadPoolExecutor(max_workers=self.jobs, thread_name_prefix="port-tests") as pool:
            while True:
                waiting = {i for _, i, _ in retries}
                idle = [i for i, g in enumerate(groups)
                        if heads[i] < len(g) and i not in waiting and i not in running.values()]
                alone = [i for i in idle if groups[i][heads[i]].job.alone]
                if error is None and not running and retries:
                    t, i, first = retries.pop(0)
                    self.trace.append(("retry", t.name, frozenset()))
                    finished(i, t, self.rerun_alone(t, first))
                    continue
                if error is None and not running and alone:
                    t = groups[alone[0]][heads[alone[0]]]
                    self.trace.append(("alone", t.name, frozenset()))
                    finished(alone[0], t, run_job(t.job))
                    continue
                if error is None and not retries and not alone:
                    gpu = sum(t.job.gpu for t in running)
                    for i in sorted(idle, key=lambda i: (not groups[i][heads[i]].job.gpu, -left(i), i)):
                        t = groups[i][heads[i]]
                        if len(running) >= self.jobs:
                            break
                        if t.job.gpu and gpu >= GPU_SLOTS:
                            continue
                        self.trace.append(("start", t.name, frozenset(r.name for r in running)))
                        if running:
                            shared.add(t)
                            shared.update(running)
                        running[t] = i
                        gpu += t.job.gpu
                        pool.submit(work, t)
                if not running:
                    if error is not None:
                        raise error
                    if not retries and all(h == len(g) for h, g in zip(heads, groups)):
                        return
                    continue
                try:
                    t, result, e = done.get(timeout=1)
                except queue.Empty:
                    continue
                i = running.pop(t)
                if e is not None or error is not None:
                    error = error or e
                elif result[0] == "FAIL" and t in shared:
                    retries.append((t, i, result))
                else:
                    finished(i, t, result)

    def rerun_alone(self, t: Test, first: tuple[str, str]) -> tuple[str, str]:
        """Rerun a test that failed while shared, nothing else running; its result.

        The shared run's log is kept beside the new one as NAME.shared.log.
        """
        log, kept = OUT / f"{t.name}.log", OUT / f"{t.name}.shared.log"
        if log.exists():
            os.replace(log, kept)
        when_shared = first[1].replace(str(display_path(log)), str(display_path(kept)))
        status, detail = run_job(t.job)
        if status != "ok":
            return status, f"{detail}; shared too: {when_shared}"
        if t.name not in self.alone_only:
            self.alone_only.add(t.name)
            with (OUT / ALONE_FILE).open("a") as f:
                f.write(t.name + "\n")
        return status, (f"FAILED ONLY WHEN SHARED, alone from now on (add it to ALONE; "
                        f"{display_path(OUT / ALONE_FILE)}): shared {when_shared}; alone {detail}")

    def plan(self) -> None:
        b = self.build

        for t in PLAIN:
            self.run(t, b / t)

        self.run("rewindpack-sync", b / "rewindpacktest", "sync", target="rewindpacktest")

        self.run("transitiontest", b / "transitiontest", self.scratch("transition-replay", "transitiontest"))
        self.run("transitiontest-reported", b / "transitiontest", self.scratch("transition-reported", "transitiontest-reported"), "--reported")
        # Five actual-wave families, each with the original 60 display cases.
        self.run("distortion-replay", b / "distortiontest", self.scratch("distortion-replay"), timeout=5*TIMEOUT)
        self.run("interpolationgputest", b / "interpolationgputest", self.scratch("interpolation-gpu", "interpolationgputest"))
        self.run("high-refresh", Path(sys.executable), ROOT / "tools/test_high_refresh.py", b,
                 self.scratch("high-refresh"), target="highrefreshtest")
        for clock in ("pacing", "pacing-step"):
            name = "battle-hw-" + clock
            self.run(name, b / "highrefreshtest", "0", self.scratch(name), clock, target="highrefreshtest")

        d = self.scratch("cpu-effect-settings", "cpueffect-write", "cpueffect-reload") / "settings.ini"
        self.run("cpueffect-write", b / "cpueffecttest", "write", d, target="cpueffecttest")
        self.run("cpueffect-reload", b / "cpueffecttest", "read", d, target="cpueffecttest")

        d = self.scratch("autoresolution-settings", "autoresolution-write", "autoresolution-reload") / "settings.ini"
        self.run("autoresolution-write", b / "autoresolutiontest", "write", d, target="autoresolutiontest")
        self.run("autoresolution-reload", b / "autoresolutiontest", "read", d, target="autoresolutiontest")

        for t in SETTINGS:
            self.run(f"{t}-check", b / t, self.scratch(f"{t}/check", f"{t}-check"), "check", target=t)
            d = self.scratch(f"{t}/persist", f"{t}-write", f"{t}-read")
            self.run(f"{t}-write", b / t, d, "write", target=t)
            self.run(f"{t}-read", b / t, d, "read", target=t)

        d = self.scratch("pad", *(f"pad_profiletest-{mode}" for mode in ("write", "read", "script", "script2")))
        for mode in ("write", "read", "script", "script2"):
            self.run(f"pad_profiletest-{mode}", b / "pad_profiletest", d, mode, target="pad_profiletest")
        d = self.scratch("pad_ini", "pad_profiletest-ini")
        self.run("pad_profiletest-ini", b / "pad_profiletest", d, "ini", target="pad_profiletest")

        for t in ("texturepacktest", "glyphtest", "portraitpacktest", "celgputest"):
            d = self.scratch(t, t, f"{t}-reload")
            self.run(t, b / t, d, f"{d}/settings.ini")
            self.run(f"{t}-reload", b / t, d, f"{d}/settings.ini", "read", target=t)

        # Beetle PSX HW packs (xg-mrj.1): upload names and ownership, palettes, the folder's rules,
        # one-image and composite samples, precedence against the texture pack, the A/B; no GPU.
        d = self.scratch("beetlepacktest")
        self.run("beetlepacktest", b / "beetlepacktest", d, d / "settings.ini")

        # Legacy texture options kept exactly by the menu's first appearance
        # change, then after a restart (xg-j85.9); no GPU work.
        for case in ("1", "2", "3"):
            name = f"texturefilter-legacy{case}"
            d = self.scratch(name, name, f"{name}-restart")
            self.run(name, b / "texturefiltertest", d / "settings.ini", "legacy", case, target="texturefiltertest")
            self.run(f"{name}-restart", b / "texturefiltertest", d / "settings.ini", "legacy", case, "restart",
                     target="texturefiltertest")
        # Cel HUD libraries without a GPU: sprites.txt from other systems, the
        # artwork budget and the status's outcome counts (xg-j85.14/.20/.40).
        self.run("celpacktest", b / "celpacktest", self.scratch("celpacktest"))
        # The art tools' own tests, where the art tools are present (the public
        # port repository leaves the HD art pipeline out).
        if (ROOT / "tools" / "ui_cel_pack.py").exists():
            # The cel HUD packager's native reads at VRAM's edges (xg-j85.25).
            self.run("ui-cel-pack", Path(sys.executable), ROOT / "tools" / "test_ui_cel_pack.py", target="test_ui_cel_pack")
            self.run("battle-ui-metadata", Path(sys.executable), "-m", "unittest", "discover",
                     "-s", ROOT / "tools", "-p", "test_*_metadata.py")
            # Portrait registration (Perfect Works art to the sprite's framing) and the
            # faithfulness gate for redrawn portraits (synthetic sprites, no disc).
            self.run("portrait-tools", Path(sys.executable), ROOT / "tools" / "test_portrait_tools.py")

        for t in ("glyphgputest", "texturepackgputest", "portraitgputest", "gputest", "card_biostest", "slangtest", "moviepacktest",
                  "presentfiltertest"):
            self.run(t, b / t, self.scratch(t))

        d = self.scratch("card_history", "card_historytest", "card_historytest-restart") / "cards"
        self.run("card_historytest", b / "card_historytest", d)
        self.run("card_historytest-restart", b / "card_historytest", d, "restart",
                 target="card_historytest")

        d = self.scratch("card_speedup", "card_speeduptest", "card_speeduptest-read", "card_speeduptest-off")
        self.run("card_speeduptest", b / "card_speeduptest", d, "write")
        self.run("card_speeduptest-read", b / "card_speeduptest", d, "read", target="card_speeduptest")
        self.run("card_speeduptest-off", b / "card_speeduptest", d, "off", target="card_speeduptest")

        # The game's text from other sources (xg-htn.35.22); with disc 1 images, a pack's round trip
        # (the JP one after the US one: every JP glyph has its character).
        us1, jp1 = disc("USA", 1), disc("Japan", 1)
        self.run("gametexttest", b / "gametexttest", self.scratch("gametext", "gametexttest"),
                 *([us1] if us1.exists() else []), *([jp1] if us1.exists() and jp1.exists() else []))

        # Hard's curated exceptions (system/gameplay.c hardPercent, xg-htn.35.7.2) key the same
        # enemies in the JP set files as in the US ones, on each disc both releases have here.
        pairs = [p for n in (1, 2) for p in (disc("USA", n), disc("Japan", n))
                 if disc("USA", n).exists() and disc("Japan", n).exists()]
        if pairs or self.listing:
            self.run("hard-profile-ids", Path(sys.executable), ROOT / "tools" / "test_hard_profile_ids.py",
                     *(p.with_suffix(".bin") for p in pairs))
        else:
            self.skip("hard-profile-ids", "needs a US and a JP disc image of one disc in bin/")

        env = dict(self.env, XG_DUMP_PRESENT=str(self.scratch("present", "presenttest")), XG_ASPECT="4:3", XG_SCALE="fit")
        self.run("presenttest", b / "presenttest", env=env)

        for reg in ("USA", "Japan"):
            for kind in ("menu", "battle"):
                name = f"promptgputest-{reg}-{kind}"
                vram = ROOT / f"work/regional/button-vram-{reg}-{kind}.bin"
                if vram.exists() or self.listing:
                    self.run(name, b / "promptgputest", vram, kind, "jp" if reg == "Japan" else "us",
                             target="promptgputest")
                else:
                    self.skip(name, f"no fixture {vram.relative_to(ROOT)}")

        for cut in ("us-d1", "us-d2", "jp-d1", "jp-d2"):
            portraits = ROOT / "work/issue-portraits" / cut
            name = f"gameplay_portraittest-{cut}"
            if (portraits / "portrait-mapping.bin").exists() or self.listing:
                self.run(name, b / "gameplay_portraittest", portraits, target="gameplay_portraittest")
            else:
                self.skip(name, f"no fixture {portraits.relative_to(ROOT)}/portrait-mapping.bin")

        discs = [disc(r, n) for r in ("USA", "Japan") for n in (1, 2)]
        for mode in range(5):
            name = f"regional_audiotest-{mode}"
            if all(x.exists() for x in discs) or self.listing:
                self.run(name, b / "regional_audiotest", *discs, mode, target="regional_audiotest")
            else:
                self.skip(name, "needs all four US and JP disc images in bin/")
        if all(x.exists() for x in discs) or self.listing:
            self.run("mdectest-retail", b / "mdectest", *discs, target="mdectest")
        else:
            self.skip("mdectest-retail", "needs all four US and JP disc images in bin/")

        # Save states (xg-htn.34.10, tools/statecheck.py): saving changes nothing, and a load goes on exactly
        # (the frames, the sound and the whole state after it), in a field and a movie.
        jp = build_version(b) == "JP"
        if disc("Japan" if jp else "USA", 1).exists() or self.listing:
            self.run("text-wordwrap", Path(sys.executable), ROOT / "tools" / "test_gametext_layout.py", b,
                     self.scratch("text-wordwrap"), *(["--jp"] if jp else []), target="xenogears")
            self.run("state-equivalence", Path(sys.executable), ROOT / "tools" / "statecheck.py", "run", b,
                     self.scratch("states", "state-equivalence"), "--scene", "field", "--scene", "movie", *(["--jp"] if jp else []),
                     target="xenogears")
        else:
            self.skip("text-wordwrap", "needs the disc 1 image in bin/")
            self.skip("state-equivalence", "needs the disc 1 image in bin/")
        # Battle speed (xg-mrj.2, tools/test_battle_speed.py): a scripted battle at 1x, 2x and 4x reaches
        # the original's states in the same order, the music at its tempo; 4x on the real clock never
        # runs the sound device dry.
        if disc("Japan" if jp else "USA", 1).exists() or self.listing:
            self.run("battle-speed", Path(sys.executable), ROOT / "tools" / "test_battle_speed.py", b,
                     self.scratch("battle-speed"), *(["--jp"] if jp else []), target="xenogears", timeout=3 * TIMEOUT)
        else:
            self.skip("battle-speed", "needs the disc 1 image in bin/")
        # Rewind (xg-htn.34.10): on and unused it changes nothing; going back and playing on reaches the same
        # frames, sound and state as never going back (a field, and Battling with its coroutine).
        if disc("Japan" if jp else "USA", 1).exists() or self.listing:
            self.run("state-rewind", Path(sys.executable), ROOT / "tools" / "statecheck.py", "rewind", b,
                     self.scratch("rewind", "state-rewind"), "--scene", "field", "--scene", "battling", *(["--jp"] if jp else []),
                     target="xenogears")
        else:
            self.skip("state-rewind", "needs the disc 1 image in bin/")

        first = disc("Japan" if build_version(b) == "JP" else "USA", 1)
        for name, (script, expect) in EXTRAS.items():
            if not self.wanted(name):
                continue
            if not build_extras(b):
                self.skip(name, "a build without the extras (XG_EXTRAS=OFF)")
                continue
            if not (first.exists() or self.listing):
                self.skip(name, f"no disc image {first.relative_to(ROOT)}")
                continue
            path = self.scratch(name) / "extras.txt"
            if not self.listing:
                path.write_text(script)
            # No real gamepad (a player's, connected now) reaches the script.
            env = dict(self.env, XG_EXTRAS_SCRIPT=str(path), XG_CLOCK="step", SDL_JOYSTICK_HIDAPI="0",
                       SDL_GAMECONTROLLER_IGNORE_DEVICES_EXCEPT="0x0000/0x0000")
            self.run(name, game_executable(b), "--extras", first, env=env, target="xenogears", expect=expect)

        # The CD layer under the game's own loader (standalone, like spu_test):
        # the boot's loads from US Disc 1, then cdcheck.py compares them with
        # the image read by tools/xenofs.py.
        us1 = disc("USA", 1)
        if not (us1.exists() or self.listing):
            self.skip("cdtest", f"no disc image {us1.relative_to(ROOT)}")
        else:
            built = True
            if self.wanted("cdtest") and not self.listing:
                p = subprocess.run(["sh", str(ROOT / "port/src/cd/test/build.sh")],
                                   capture_output=True, text=True)
                if p.returncode != 0:
                    self.record("cdtest", "FAIL", "build: " + (p.stderr.strip().splitlines() or ["?"])[-1])
                    built = False
            if built:
                d = self.scratch("cdtest", "cdtest", "cdtest-check")
                self.run("cdtest", ROOT / "build/pc/cdtest/cdtest", us1, d)
                self.run("cdtest-check", Path(sys.executable), ROOT / "port/src/cd/test/cdcheck.py",
                         us1.with_suffix(".bin"), d, target="cdcheck")

        spu = ROOT / "build/pc/spu-test/spu_test"
        if self.wanted("spu_test") and not self.listing:
            p = subprocess.run(["sh", str(ROOT / "port/src/spu/test/build.sh")],
                               capture_output=True, text=True)
            if p.returncode != 0:
                self.record("spu_test", "FAIL", "build: " + (p.stderr.strip().splitlines() or ["?"])[-1])
                return
        self.run("spu_test", spu)


def game_executable(build: Path) -> Path:
    """The game in a build: the Original engine's xenogears or the Enhanced engine's
    xenogears-enhanced (port/CMakeLists.txt XG_ENGINE; the CMake target is xenogears in both)."""
    for name in ("xenogears", "xenogears-enhanced"):
        if (build / name).exists() or (build / f"{name}.exe").exists():
            return build / name
    return build / "xenogears"


def targets(build: Path) -> list[str]:
    """The CMake targets this runner uses (every test and the GPU fixtures)."""
    names = set(PLAIN) | set(SETTINGS) | {"highrefreshtest", "interpolationgputest", "transitiontest",
        "pad_profiletest", "texturepacktest", "beetlepacktest", "portraitpacktest", "glyphtest", "glyphgputest", "celgputest", "celpacktest",
        "texturepackgputest", "portraitgputest", "gputest", "card_biostest", "card_historytest", "card_speeduptest",
        "presenttest", "promptgputest", "gameplay_portraittest",
        "regional_audiotest", "slangtest", "moviepacktest", "gametexttest", "presentfiltertest",
    }
    help_ = subprocess.run(["cmake", "--build", str(build), "--target", "help"],
                           capture_output=True, text=True).stdout
    known = set(re.findall(r"^(\w+):", help_, re.M))
    missing = sorted(names - known)
    if missing:
        print("port-tests: targets missing from the build (stale runner?): " + " ".join(missing))
    # The game itself, for the EXTRAS runs (a default target, so 'help' leaves it out).
    return sorted(names & known) + ["xenogears"]


def default_jobs() -> int:
    """The CPUs this process may use, at most DEFAULT_JOBS_CAP."""
    if hasattr(os, "process_cpu_count"):
        cpus = os.process_cpu_count()
    elif hasattr(os, "sched_getaffinity"):
        cpus = len(os.sched_getaffinity(0))
    else:
        cpus = os.cpu_count()
    return max(1, min(DEFAULT_JOBS_CAP, cpus or 1))


def job_count(text: str) -> int:
    try:
        n = int(text)
    except ValueError:
        n = 0
    if not 1 <= n <= MAX_JOBS:
        raise argparse.ArgumentTypeError(f"--jobs takes a whole number from 1 to {MAX_JOBS}, not {text!r}")
    return n


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--jobs", "-j", type=job_count, default=default_jobs(),
                    help=f"tests run side by side (default: the CPU count, at most {DEFAULT_JOBS_CAP}; "
                         "1 runs them one after another)")
    ap.add_argument("--build", default="build/pc/release", help="configured CMake build directory")
    ap.add_argument("--no-build", action="store_true", help="don't build the test targets first")
    ap.add_argument("--no-gpu", action="store_true", help="skip tests that need a Vulkan device")
    ap.add_argument("-k", dest="pattern", help="only tests whose name contains this")
    ap.add_argument("--list", action="store_true", help="print the test commands and exit")
    a = ap.parse_args()

    build = (ROOT / a.build).resolve() if not Path(a.build).is_absolute() else Path(a.build)
    if not (build / "CMakeCache.txt").exists():
        print(f"port-tests: {build} is not a configured build (see docs/PORT_SETUP.md)")
        return 1
    if a.list:
        return run_tests(build, a)
    try:
        with output_lock():
            return run_tests(build, a)
    except OutputBusy as e:
        print(f"port-tests: {e}", file=sys.stderr)
        return 1


def run_tests(build: Path, a: argparse.Namespace) -> int:
    if not a.no_build and not a.list:
        t = targets(build)
        p = subprocess.run(["cmake", "--build", str(build), "--target", *t],
                           stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        (OUT / "build.log").write_text(p.stdout)
        if p.returncode != 0:
            print(f"port-tests: building the tests failed (see {display_path(OUT / 'build.log')})")
            return 1

    r = Runner(build, gpu=not a.no_gpu, pattern=a.pattern, listing=a.list, jobs=a.jobs)
    print(f"port tests ({display_path(build)}, "
          f"{build_version(build)}; logs in {display_path(OUT)}/):")
    r.all()
    if a.list:
        return 0
    count = {s: sum(1 for _, st, _ in r.results if st == s) for s in ("ok", "FAIL", "known", "skip")}
    print(f"port-tests: {count['ok']} ok, {count['FAIL']} failed, {count['known']} known, "
          f"{count['skip']} skipped")
    return count["FAIL"]


if __name__ == "__main__":
    sys.exit(main())
