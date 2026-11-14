#!/usr/bin/env python3
"""Port test runner isolation regressions; no game, GPU or disc data needed."""
from contextlib import redirect_stdout
import io
import os
from pathlib import Path
import queue
import subprocess
import sys
import tempfile
import threading
import time
import unittest
from unittest.mock import patch

import port_tests as P


class Commands(P.Runner):
    """Exercise fixture scheduling without launching the listed game tests."""
    def run(self, name, exe, *args, **kwargs):
        if self.wanted(name):
            self.results.append((name, args, kwargs.get("env", {})))

    def skip(self, name, why):
        pass


class RunnerTests(unittest.TestCase):
    def setUp(self):
        parent = P.ROOT / "work/port-runner-tests"
        parent.mkdir(parents=True, exist_ok=True)
        self.tmp = tempfile.TemporaryDirectory(prefix="case-", dir=parent)
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        self.out = self.root / "output with spaces"
        self.build = self.root / "build"
        self.build.mkdir()
        (self.build / "CMakeCache.txt").write_text("XG_VERSION:STRING=US\nXG_EXTRAS:BOOL=ON\n")
        self.addCleanup(patch.stopall)
        patch.object(P, "OUT", self.out).start()
        patch.dict(os.environ, XG_GPU_LOCK=str(self.root / "gpu.lock")).start()

    def fixtures(self):
        # Empty stand-ins enable every disc-dependent branch, including rewind
        # and extras, without reading or copying any player's disc data.
        for reg in ("USA", "Japan"):
            for n in (1, 2):
                name = f"Xenogears ({reg}) (Disc {n})"
                path = self.root / "bin" / name / f"{name}.cue"
                path.parent.mkdir(parents=True)
                path.touch()
        patch.object(P, "ROOT", self.root).start()

    def commands(self, pattern=None, listing=False):
        runner = Commands(self.build, gpu=True, pattern=pattern, listing=listing)
        with patch.object(P.subprocess, "run", return_value=subprocess.CompletedProcess([], 0)), \
             redirect_stdout(io.StringIO()):
            runner.all()
        return runner

    def seed(self):
        self.commands()
        paths = list((self.out / "scratch").rglob("*"))
        for path in paths:
            if path.is_dir() and not any(p.is_dir() for p in path.iterdir()):
                (path / "sentinel").write_text("preserve me")
        return list((self.out / "scratch").rglob("sentinel"))

    def test_filtered_combat_preserves_every_other_fixture(self):
        self.fixtures()
        sentinels = self.seed()
        extras = {p: p.read_bytes() for p in (self.out / "scratch").rglob("extras.txt")}
        runner = self.commands("gameplay_combattest")
        self.assertEqual([name for name, _, _ in runner.results],
                         ["gameplay_combattest-check", "gameplay_combattest-write", "gameplay_combattest-read"])
        for path in sentinels:
            if "gameplay_combattest" not in path.parts:
                self.assertEqual(path.read_text(), "preserve me", str(path))
            else:
                self.assertFalse(path.exists(), str(path))
        self.assertEqual(extras, {p: p.read_bytes() for p in extras})

    def test_empty_filtered_run_creates_no_scratch(self):
        self.fixtures()
        self.assertEqual(self.commands("no-such-test").results, [])
        self.assertFalse(self.out.exists())

    def test_shared_fixture_survives_write_and_read(self):
        seen = []

        def child(argv, **kwargs):
            directory, mode = Path(argv[1]), argv[2]
            marker = directory / "written"
            if mode == "write":
                marker.write_text("saved settings")
            elif mode == "read":
                self.assertEqual(marker.read_text(), "saved settings")
            seen.append((directory, mode))
            return subprocess.CompletedProcess(argv, 0)

        (self.build / "gameplay_combattest").touch()
        runner = P.Runner(self.build, gpu=True, pattern="gameplay_combattest", listing=False)
        with patch.object(P.subprocess, "run", side_effect=child), redirect_stdout(io.StringIO()):
            runner.all()
        self.assertEqual([mode for _, mode in seen], ["check", "write", "read"])
        self.assertEqual(seen[1][0], seen[2][0])
        self.assertTrue(all(status == "ok" for _, status, _ in runner.results))
        self.assertEqual(sorted(p.name for p in (self.out / "scratch").iterdir()), ["gameplay_combattest"])

    def test_each_consumer_prepares_its_own_fixtures(self):
        self.fixtures()
        commands = self.commands().results
        for name, args, env in commands:
            with self.subTest(name=name):
                sentinels = self.seed()
                consumed = [str(arg) for arg in args] + list(env.values())
                self.commands(name)
                for sentinel in sentinels:
                    directory = str(sentinel.parent)
                    used = any(str(arg) == directory or str(arg).startswith(directory + os.sep)
                               for arg in consumed)
                    # Substring -k can select more than one command (e.g. a
                    # base test and its reload); include all selected consumers.
                    if not used:
                        for other, other_args, other_env in commands:
                            if name in other:
                                used |= any(str(arg) == directory or str(arg).startswith(directory + os.sep)
                                            for arg in (*other_args, *other_env.values()))
                    self.assertEqual(sentinel.exists(), not used, str(sentinel))

    def cli(self, out=None, *args, no_build=True):
        env = dict(os.environ, XG_PORT_TESTS_OUT=str(out or self.out),
                   XG_GATES_OUT=str(self.root / "unused-gates"))
        return subprocess.run([sys.executable, str(Path(P.__file__).resolve()), "--build", str(self.build),
                               *(["--no-build"] if no_build else []), *args],
                              env=env, capture_output=True, text=True, timeout=10)

    def test_list_has_no_output_side_effects(self):
        result = self.cli(None, "--list", "-k", "gameplay_combattest")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("gameplay_combattest-read", result.stdout)
        self.assertFalse(self.out.exists())

    def holder(self, out=None):
        # A separate process owns the real advisory lock, just as main() does
        # throughout a full run. Readiness is signalled only after acquisition.
        script = ("import sys,time; from pathlib import Path; "
                  f"sys.path.insert(0, {str(Path(P.__file__).resolve().parent)!r}); "
                  "import port_tests as p; "
                  "exec('with p.output_lock():\\n print(\"locked\", flush=True)\\n time.sleep(30)')")
        process = subprocess.Popen([sys.executable, "-c", script],
                                   env=dict(os.environ, XG_PORT_TESTS_OUT=str(out or self.out)),
                                   stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        self.addCleanup(self.stop, process)
        # Bound the wait even when importing or acquiring the lock fails
        # (a reader thread: select() on a pipe is POSIX-only).
        lines = queue.Queue()
        threading.Thread(target=lambda: lines.put(process.stdout.readline()), daemon=True).start()
        try:
            line = lines.get(timeout=5)
        except queue.Empty:
            self.fail("lock holder failed to start")
        self.assertEqual(line.strip(), "locked")
        return process

    @staticmethod
    def stop(process):
        if process.poll() is None:
            process.kill()
        process.communicate(timeout=5)

    def test_concurrent_full_and_filtered_runs_fail_before_modifying_outputs(self):
        self.out.mkdir()
        fixture = self.out / "scratch/rewind/state"
        fixture.parent.mkdir(parents=True)
        fixture.write_bytes(b"active state")
        log = self.out / "build.log"
        log.write_text("active build log")
        self.holder()
        # --no-build for the full run too: were the lock ever not held, it
        # must not run cmake against this stand-in build directory.
        for args in ((), ("-k", "gameplay_combattest")):
            result = self.cli(None, *args)
            self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
            self.assertIn("is in use", result.stderr)
            self.assertIn("XG_PORT_TESTS_OUT", result.stderr)
            self.assertEqual(fixture.read_bytes(), b"active state")
            self.assertEqual(log.read_text(), "active build log")
        listing = self.cli(None, "--list")
        self.assertEqual(listing.returncode, 0, listing.stderr)

    def test_independent_output_root_can_run_concurrently(self):
        self.holder()
        other = self.root / "another output"
        result = self.cli(other, "-k", "no-such-test")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn(str(P.display_path(other)), result.stdout)
        self.assertTrue((other / ".lock").exists())
        self.assertFalse((self.root / "unused-gates").exists())

    def test_killed_owner_releases_lock_without_stale_lock_cleanup(self):
        owner = self.holder()
        lock = self.out / ".lock"
        inode = lock.stat().st_ino
        self.stop(owner)
        result = self.cli(None, "-k", "no-such-test")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual(lock.stat().st_ino, inode)
        again = self.cli(None, "-k", "no-such-test")
        self.assertEqual(again.returncode, 0, again.stderr)

    def test_exception_releases_lock(self):
        with self.assertRaisesRegex(RuntimeError, "interrupted"):
            with P.output_lock():
                raise RuntimeError("interrupted")
        result = self.cli(None, "-k", "no-such-test")
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_gates_output_fallback(self):
        gates = self.root / "gates output"
        env = dict(os.environ, XG_GATES_OUT=str(gates))
        env.pop("XG_PORT_TESTS_OUT", None)
        result = subprocess.run([sys.executable, str(Path(P.__file__).resolve()), "--build", str(self.build),
                                 "--no-build", "-k", "no-such-test"],
                                env=env, capture_output=True, text=True, timeout=10)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertTrue((gates / "port_tests/.lock").exists())
        self.assertFalse(self.out.exists())


class Children:
    """A stand-in for subprocess.run: records which tests overlap, never by timing.

    hooks[name](argv) runs inside the child's lifetime; return codes come from
    codes (a number, or a function of the run's index: 0 for the first run).
    A rerun's overlaps replace the first run's; runs[name] keeps each run's.
    """
    def __init__(self, hooks=None, codes=None, gpu=()):
        self.hooks, self.codes, self.gpu = hooks or {}, codes or {}, set(gpu)
        self.lock = threading.Lock()
        self.active = set()
        self.started, self.finished = [], []
        self.overlaps = {}  # name -> every other test running at some point during it
        self.runs = {}      # name -> [its overlaps, per run]
        self.threads = {}
        self.gpu_peak = 0   # the most tests named in gpu running at once

    def __call__(self, argv, **kwargs):
        if argv[0] == "sh":  # the standalone CD and SPU tests' build scripts
            return subprocess.CompletedProcess(argv, 0, "", "")
        name = Path(kwargs["stdout"].name).stem
        with self.lock:
            self.overlaps[name] = set(self.active)
            self.runs.setdefault(name, []).append(self.overlaps[name])
            for other in self.active:
                self.overlaps[other].add(name)
            self.active.add(name)
            self.gpu_peak = max(self.gpu_peak, len(self.active & self.gpu))
            run = self.started.count(name)
            self.started.append(name)
            self.threads[name] = threading.current_thread()
        try:
            if name in self.hooks:
                self.hooks[name](argv)
            kwargs["stdout"].write(f"{name} says hello (run {run})\n")
            code = self.codes.get(name, 0)
            return subprocess.CompletedProcess(argv, code(run) if callable(code) else code)
        finally:
            with self.lock:
                self.active.discard(name)
                self.finished.append(name)


class SchedulerTests(unittest.TestCase):
    def setUp(self):
        parent = P.ROOT / "work/port-runner-tests"
        parent.mkdir(parents=True, exist_ok=True)
        self.tmp = tempfile.TemporaryDirectory(prefix="sched-", dir=parent)
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        self.out = self.root / "out"
        self.build = self.root / "build"
        self.build.mkdir()
        (self.build / "CMakeCache.txt").write_text("XG_VERSION:STRING=US\nXG_EXTRAS:BOOL=ON\n")
        self.addCleanup(patch.stopall)
        patch.object(P, "OUT", self.out).start()
        # Never the host's shared GPU lock: these runs use no GPU.
        patch.dict(os.environ, XG_GPU_LOCK=str(self.root / "gpu.lock")).start()

    def exe(self, name):
        path = self.build / name
        path.touch()
        return path

    def execute(self, runner, children):
        stdout = io.StringIO()
        with patch.object(P.subprocess, "run", side_effect=children), redirect_stdout(stdout):
            runner.execute()
        return [line.split()[1] for line in stdout.getvalue().splitlines()]

    def test_independent_tests_overlap_and_report_in_planned_order(self):
        # c starts only once a and b are both running (they must overlap), and
        # a ends only after c has: completion order differs from the plan's.
        both = threading.Barrier(3, timeout=10)
        c_done = threading.Event()
        hooks = {"a": lambda _: (both.wait(), c_done.wait(10)), "b": lambda _: both.wait(),
                 "c": lambda _: (both.wait(), c_done.set())}
        children = Children(hooks)
        runner = P.Runner(self.build, gpu=True, pattern=None, listing=False, jobs=3)
        for name in "abc":
            runner.run(name, self.exe(f"{name}test"))
        printed = self.execute(runner, children)
        self.assertEqual(printed, ["a", "b", "c"])
        self.assertEqual([n for n, _, _ in runner.results], ["a", "b", "c"])
        self.assertTrue(all(s == "ok" for _, s, _ in runner.results), runner.results)
        self.assertLess(children.finished.index("c"), children.finished.index("a"))
        self.assertIn("b", children.overlaps["a"])

    def test_clock_tests_run_alone_and_first(self):
        gpu = {"gputest", "presenttest", "state-rewind"}
        children = Children(gpu=gpu)
        runner = P.Runner(self.build, gpu=True, pattern=None, listing=False, jobs=4)
        names = ["gameidtest", "gputest", "jpegtest", "netplaytest", "mdectest", "hostpolltest", "presenttest"]
        for name in names:
            runner.run(name, self.exe(name))
        runner.run("state-rewind", Path(sys.executable), "-c", "", target="xenogears")
        printed = self.execute(runner, children)
        self.assertEqual(printed, names + ["state-rewind"])
        clock = ["netplaytest", "hostpolltest"]
        self.assertEqual(children.started[:len(clock)], clock)
        for name in clock:
            self.assertEqual(children.overlaps[name], set(), name)
            self.assertIs(children.threads[name], threading.main_thread())
        self.assertEqual([(e, n) for e, n, _ in runner.trace[:2]], [("alone", n) for n in clock])
        for name in gpu:  # GPU tests are no longer alone, but share at most two at a time
            self.assertFalse(runner.tests[printed.index(name)].job.alone, name)
        self.assertLessEqual(children.gpu_peak, P.GPU_SLOTS)
        for event, name, running in runner.trace:
            self.assertLessEqual(len(({name} | running) & gpu), P.GPU_SLOTS, (name, running))

    def test_gpu_tests_share_at_most_two(self):
        # gputest and presenttest must run together (a barrier only both can
        # pass); the cap is the scheduler's own record of every start, and the
        # stand-in children's peak.
        gpu = ["gputest", "presenttest", "slangtest", "texturetest", "verticaltest"]
        pair = threading.Barrier(2, timeout=10)
        children = Children({"gputest": lambda _: pair.wait(), "presenttest": lambda _: pair.wait()}, gpu=gpu)
        runner = P.Runner(self.build, gpu=True, pattern=None, listing=False, jobs=8)
        for name in gpu + ["jpegtest", "mdectest"]:
            runner.run(name, self.exe(name))
        self.assertEqual(self.execute(runner, children), gpu + ["jpegtest", "mdectest"])
        self.assertIn("presenttest", children.overlaps["gputest"])
        self.assertLessEqual(children.gpu_peak, P.GPU_SLOTS)
        starts = [(n, r) for e, n, r in runner.trace if e == "start"]
        self.assertEqual(sorted(n for n, _ in starts), sorted(gpu + ["jpegtest", "mdectest"]))
        for name, running in starts:
            self.assertLessEqual(len(({name} | running) & set(gpu)), P.GPU_SLOTS, (name, running))
        # The first pass fills both slots and starts the CPU tests beside them.
        self.assertEqual({n for n, _ in starts[:4]}, {"gputest", "presenttest", "jpegtest", "mdectest"})
        self.assertTrue(all(s == "ok" for _, s, _ in runner.results), runner.results)

    def test_clock_test_inside_a_chain_waits_for_quiescence(self):
        # battle-speed shares the game with the GPU tests around it: they keep
        # their order, and it runs with nothing else at all running.
        children = Children(gpu={"state-equivalence", "state-rewind", "gputest", "presenttest"})
        runner = P.Runner(self.build, gpu=True, pattern=None, listing=False, jobs=8)
        for name in ("state-equivalence", "battle-speed", "state-rewind"):
            runner.run(name, Path(sys.executable), f"{name}.py", target="xenogears")
        for name in ("gputest", "presenttest", "jpegtest", "high-refresh"):
            runner.run(name, self.exe(name), target="highrefreshtest" if name == "high-refresh" else None)
        printed = self.execute(runner, children)
        self.assertEqual(printed, ["state-equivalence", "battle-speed", "state-rewind",
                                   "gputest", "presenttest", "jpegtest", "high-refresh"])
        for name in ("battle-speed", "high-refresh"):
            self.assertEqual(children.overlaps[name], set(), name)
            self.assertIs(children.threads[name], threading.main_thread())
            self.assertIn(("alone", name, frozenset()), runner.trace)
        self.assertLess(children.finished.index("state-equivalence"), children.started.index("battle-speed"))
        self.assertLess(children.finished.index("battle-speed"), children.started.index("state-rewind"))
        self.assertEqual(runner.trace[0][:2], ("alone", "high-refresh"))   # a chain's head: first

    def shared_failure(self, first):
        """flaky fails beside partner (a barrier makes them overlap) and passes
        alone; next shares partner's executable, so it waits for partner.

        first is the test whose result reaches the scheduler first: the
        other's child stays running until that result is queued, so the
        arrival order is fixed, not left to thread timing. The runner's
        result queue is the stand-in below only to see that moment.
        """
        reported = {name: threading.Event() for name in ("flaky", "partner", "next")}

        class Reports(queue.SimpleQueue):
            def put(self, item, block=True, timeout=None):
                super().put(item, block, timeout)
                if isinstance(item, tuple) and item and isinstance(item[0], P.Test):
                    reported[item[0].name].set()

        def after(name):
            if not reported[name].wait(10):
                raise RuntimeError(f"{name}'s result never reached the scheduler")

        both = threading.Barrier(2, timeout=10)
        last = "partner" if first == "flaky" else "flaky"

        def flaky(_):
            if children.started.count("flaky") == 1:  # the shared run; the rerun returns at once
                both.wait()
                if last == "flaky":
                    after(first)

        def partner(_):
            both.wait()
            if last == "partner":
                after(first)

        children = Children({"flaky": flaky, "partner": partner},
                            codes={"flaky": lambda run: 1 if run == 0 else 0})
        runner = P.Runner(self.build, gpu=True, pattern=None, listing=False, jobs=4)
        runner.run("flaky", self.exe("flakytest"))
        runner.run("partner", self.exe("pairtest"), "1", target="pairtest")
        runner.run("next", self.exe("pairtest"), "2", target="pairtest")
        with patch.object(P.queue, "SimpleQueue", Reports):
            printed = self.execute(runner, children)
        self.assertEqual(printed, ["flaky", "partner", "next"])
        self.assertEqual(set(children.started[:2]), {"flaky", "partner"})
        self.assertEqual(sorted(children.started[2:]), ["flaky", "next"])
        self.assertIn("partner", children.runs["flaky"][0])
        # The rerun: nothing running when it started and nothing started
        # during it (runs[...][1] gains every test that overlaps it).
        self.assertEqual(len(children.runs["flaky"]), 2)
        self.assertEqual(children.runs["flaky"][1], set())           # alone
        rerun_end = len(children.finished) - 1 - children.finished[::-1].index("flaky")
        self.assertLess(children.finished.index("partner"), rerun_end)
        self.assertIs(children.threads["flaky"], threading.main_thread())
        self.assertIn(("retry", "flaky", frozenset()), runner.trace)
        # next starts only once partner has ended, in the children and in
        # the scheduler's own record.
        self.assertLess(children.finished.index("partner"), children.started.index("next"))
        self.assertNotIn("partner", children.overlaps["next"])
        starts = {n: r for e, n, r in runner.trace if e == "start"}
        self.assertNotIn("partner", starts["next"])
        self.assertEqual([(n, s) for n, s, _ in runner.results], [("flaky", "ok"), ("partner", "ok"), ("next", "ok")])
        detail = runner.results[0][2]
        self.assertIn("FAILED ONLY WHEN SHARED", detail)
        self.assertIn("exit 1: flaky says hello (run 0)", detail)
        self.assertIn("flaky.shared.log", detail)
        self.assertIn(P.ALONE_FILE, detail)
        self.assertEqual((self.out / "flaky.shared.log").read_text(), "flaky says hello (run 0)\n")
        self.assertEqual((self.out / "flaky.log").read_text(), "flaky says hello (run 1)\n")
        self.assertEqual((self.out / P.ALONE_FILE).read_text(), "flaky\n")
        return children, runner

    def test_known_failure_is_rerun_before_anything_else_starts(self):
        # flaky's failure is known while partner still runs: the waiting
        # rerun stops new starts, so it runs before next.
        children, runner = self.shared_failure(first="flaky")
        self.assertEqual(children.started[2:], ["flaky", "next"])
        events = [(e, n) for e, n, _ in runner.trace]
        self.assertLess(events.index(("retry", "flaky")), events.index(("start", "next")))

    def test_failure_when_shared_is_rerun_alone_and_kept_alone(self):
        # partner ends first, while flaky's failure is not yet known: next may
        # start then, beside flaky's shared run, so the rerun and next run in
        # either order; the rerun still waits until nothing else runs.
        self.shared_failure(first="partner")

        # The next run in this output directory keeps it alone from the start.
        children = Children({"partner": lambda _: None})
        runner = P.Runner(self.build, gpu=True, pattern=None, listing=False, jobs=4)
        runner.run("flaky", self.exe("flakytest"))
        runner.run("partner", self.exe("pairtest"), "1", target="pairtest")
        self.execute(runner, children)
        self.assertTrue(runner.tests[0].job.alone)
        self.assertEqual(children.started, ["flaky", "partner"])
        self.assertEqual(children.overlaps["flaky"], set())
        self.assertEqual(runner.trace[0], ("alone", "flaky", frozenset()))
        self.assertEqual((self.out / P.ALONE_FILE).read_text(), "flaky\n")

    def test_failure_alone_too_stays_a_failure(self):
        both = threading.Barrier(2, timeout=10)
        hooks = {"broken": lambda _: both.wait() if children.started.count("broken") == 1 else None,
                 "other": lambda _: both.wait()}
        children = Children(hooks, codes={"broken": 4})
        runner = P.Runner(self.build, gpu=True, pattern=None, listing=False, jobs=2)
        runner.run("broken", self.exe("brokentest"))
        runner.run("other", self.exe("othertest"))
        self.execute(runner, children)
        self.assertEqual([(n, s) for n, s, _ in runner.results], [("broken", "FAIL"), ("other", "ok")])
        self.assertEqual(children.started.count("broken"), 2)
        self.assertEqual(children.runs["broken"][1], set())
        detail = runner.results[0][2]
        self.assertTrue(detail.startswith("exit 4: broken says hello (run 1)"), detail)
        self.assertIn("shared too: exit 4: broken says hello (run 0)", detail)
        self.assertNotIn("ONLY WHEN SHARED", detail)
        self.assertFalse((self.out / P.ALONE_FILE).exists())
        self.assertTrue((self.out / "broken.shared.log").exists())

    def test_known_failure_is_not_rerun(self):
        children = Children(codes={"knowntest": 1})
        runner = P.Runner(self.build, gpu=True, pattern=None, listing=False, jobs=2)
        runner.run("knowntest", self.exe("knowntest"))
        runner.run("other", self.exe("othertest"))
        with patch.dict(P.KNOWN, {"knowntest": "xg-test"}):
            self.execute(runner, children)
        self.assertEqual(runner.results[0][1], "known")
        self.assertEqual(children.started.count("knowntest"), 1)

    def test_shared_scratch_and_executable_stay_ordered(self):
        children = Children()
        runner = P.Runner(self.build, gpu=True, pattern=None, listing=False, jobs=8)
        d = runner.scratch("pair", "pair-write", "pair-read")
        runner.run("pair-write", self.exe("writer"), d, "write")
        runner.run("other", self.exe("othertest"))
        runner.run("pair-read", self.exe("reader"), d / "settings.ini", "read")   # same scratch
        runner.run("again-1", self.exe("againtest"), "1", target="againtest")
        runner.run("again-2", self.exe("againtest"), "2", target="againtest")   # same executable
        self.execute(runner, children)
        for first, then in (("pair-write", "pair-read"), ("again-1", "again-2")):
            self.assertLess(children.finished.index(first), children.started.index(then))
            self.assertNotIn(then, children.overlaps[first])
        groups = P.chains([t for t in runner.tests])
        self.assertEqual([[t.name for t in g] for g in groups],
                         [["pair-write", "pair-read"], ["other"], ["again-1", "again-2"]])

    def test_python_scripts_without_a_target_are_independent(self):
        runner = P.Runner(self.build, gpu=True, pattern=None, listing=False, jobs=2)
        runner.run("one", Path(sys.executable), "a.py")
        runner.run("two", Path(sys.executable), "b.py")
        self.assertEqual(len(P.chains(runner.tests)), 2)

    def test_failures_counted_once_in_order(self):
        children = Children(codes={"b": 3})
        runner = P.Runner(self.build, gpu=True, pattern=None, listing=False, jobs=3)
        runner.run("a", self.exe("atest"))
        runner.skip("s", "no fixture")
        runner.run("b", self.exe("btest"))
        runner.run("m", self.build / "missingtest")
        runner.run("c", self.exe("ctest"))
        self.execute(runner, children)
        self.assertEqual([(n, s) for n, s, _ in runner.results],
                         [("a", "ok"), ("s", "skip"), ("b", "FAIL"), ("m", "FAIL"), ("c", "ok")])
        detail = dict((n, d) for n, _, d in runner.results)
        self.assertIn("exit 3: b says hello", detail["b"])
        self.assertIn("not built", detail["m"])
        # a, b and c start together: b failed shared, so it reran alone (still failing).
        self.assertEqual(sorted(children.started), ["a", "b", "b", "c"])
        self.assertEqual(children.runs["b"][1], set())

    def test_timeout_is_a_failure(self):
        def slow(argv):
            raise subprocess.TimeoutExpired(argv, 1)
        for jobs in (1, 2):
            with self.subTest(jobs=jobs):
                runner = P.Runner(self.build, gpu=True, pattern=None, listing=False, jobs=jobs)
                runner.run("slow", self.exe("slowtest"))
                runner.run("fast", self.exe("fasttest"))
                self.execute(runner, Children({"slow": slow}))
                self.assertEqual([(n, s) for n, s, _ in runner.results], [("slow", "FAIL"), ("fast", "ok")])
                self.assertIn("exit timeout", runner.results[0][2])

    def test_worker_exception_stops_new_tests_and_waits_for_running_ones(self):
        b_running = threading.Event()
        release = threading.Event()

        def boom(_):
            b_running.wait(10)
            release.set()
            raise OSError("exec format error")

        children = Children({"a": boom, "b": lambda _: (b_running.set(), release.wait(10))})
        runner = P.Runner(self.build, gpu=True, pattern=None, listing=False, jobs=2)
        runner.run("a", self.exe("atest"), target="shared")
        runner.run("a-next", self.exe("atest"), target="shared")
        runner.run("b", self.exe("btest"))
        with self.assertRaisesRegex(OSError, "exec format"):
            self.execute(runner, children)
        self.assertNotIn("a-next", children.started)
        self.assertEqual(children.active, set())          # b finished before the raise
        self.assertIn("b", children.finished)

    def test_jobs_1_runs_everything_in_order_on_the_main_thread(self):
        children = Children()
        runner = P.Runner(self.build, gpu=True, pattern=None, listing=False, jobs=1)
        names = ["gputest", "atest", "netplaytest", "btest"]
        for name in names:
            runner.run(name, self.exe(name))
        self.assertEqual(self.execute(runner, children), names)
        self.assertEqual(children.started, names)
        self.assertTrue(all(t is threading.main_thread() for t in children.threads.values()))
        self.assertTrue(all(not v for v in children.overlaps.values()))

    def test_jobs_1_never_reruns_a_failure(self):
        children = Children(codes={"atest": 2})
        runner = P.Runner(self.build, gpu=True, pattern=None, listing=False, jobs=1)
        for name in ("atest", "btest"):
            runner.run(name, self.exe(name))
        self.execute(runner, children)
        self.assertEqual([(n, s) for n, s, _ in runner.results], [("atest", "FAIL"), ("btest", "ok")])
        self.assertEqual(children.started, ["atest", "btest"])
        self.assertFalse((self.out / "atest.shared.log").exists())
        self.assertFalse((self.out / P.ALONE_FILE).exists())

    def full_plan(self, jobs):
        for reg in ("USA", "Japan"):
            for n in (1, 2):
                name = f"Xenogears ({reg}) (Disc {n})"
                path = self.root / "bin" / name / f"{name}.cue"
                path.parent.mkdir(parents=True, exist_ok=True)
                path.touch()
        spu = self.root / "build/pc/spu-test/spu_test"
        spu.parent.mkdir(parents=True, exist_ok=True)
        spu.touch()
        cd = self.root / "build/pc/cdtest/cdtest"
        cd.parent.mkdir(parents=True, exist_ok=True)
        cd.touch()
        patch.object(P, "ROOT", self.root).start()
        runner = P.Runner(self.build, gpu=True, pattern=None, listing=False, jobs=jobs)
        exes = set(P.PLAIN) | set(P.SETTINGS) | P.GPU | {
            "highrefreshtest", "pad_profiletest", "texturepacktest", "beetlepacktest", "portraitpacktest",
            "glyphtest", "celpacktest", "card_biostest", "card_historytest", "card_speeduptest",
            "gameplay_portraittest", "regional_audiotest", "gametexttest"}
        for name in exes:
            (self.build / name).touch()
        children = Children()
        stdout = io.StringIO()
        with patch.object(P.subprocess, "run", side_effect=children), redirect_stdout(stdout):
            runner.plan()
            children.gpu = {t.name for t in runner.tests if t.job and t.job.gpu}
            runner.execute()
        return runner, children

    def test_full_plan_same_report_serial_and_parallel(self):
        serial, serial_children = self.full_plan(1)
        parallel, children = self.full_plan(4)
        names = [n for n, _, _ in serial.results]
        self.assertEqual(len(names), len(set(names)))
        self.assertEqual([(n, s) for n, s, _ in parallel.results], [(n, s) for n, s, _ in serial.results])
        self.assertEqual(sorted(set(children.started)), sorted(serial_children.started))
        self.assertEqual(len(serial_children.started), len(set(serial_children.started)))
        # Only failures rerun (the extras: the stand-in's output doesn't match), once each, alone.
        failed = {n for n, s, _ in parallel.results if s == "FAIL"}
        self.assertTrue(failed)
        for name in set(children.started):
            self.assertEqual(children.started.count(name), 2 if name in failed else 1, name)
            if name in failed:
                self.assertEqual(children.runs[name][1], set(), name)
                self.assertIn(("retry", name, frozenset()), parallel.trace)
        jobs = {t.name: t.job for t in parallel.tests if t.job}
        for name, job in jobs.items():
            if job.alone:
                self.assertEqual(children.overlaps[name], set(), name)
        self.assertLessEqual(children.gpu_peak, P.GPU_SLOTS)
        for event, name, running in parallel.trace:
            if event != "retry":
                self.assertEqual(event == "start", not jobs[name].alone, name)
            if event != "start":
                self.assertEqual(running, frozenset(), name)
            self.assertLessEqual(sum(jobs[n].gpu for n in {name} | running), P.GPU_SLOTS, (name, running))
            self.assertFalse(any(jobs[n].alone for n in running), (name, running))
        # Every pair sharing a resource ran in the planned order, never overlapping.
        order = [t.name for t in parallel.tests if t.job]
        for i, a in enumerate(order):
            for b in order[i + 1:]:
                if jobs[a].resources & jobs[b].resources:
                    self.assertLess(children.finished.index(a), children.started.index(b), (a, b))
        # The wall clock: alone. The Vulkan device only: shared, two at a time.
        for name in ("high-refresh", "battle-hw-pacing", "battle-speed", "netplaytest", "battlespeedtest",
                     "hostpolltest"):
            self.assertTrue(jobs[name].alone, name)
        for name in ("transitiontest", "distortion-replay", "gputest", "orderedgputest", "presenttest",
                     "state-rewind", "state-equivalence", "text-wordwrap", "battle-hw-pacing-step",
                     "extras-bestiary", "interpolationgputest"):
            self.assertTrue(jobs[name].gpu, name)
            self.assertFalse(jobs[name].alone, name)
        for name in ("gameidtest", "jpegtest", "hard-profile-ids", "regional_audiotest-0", "glyphtest"):
            self.assertFalse(jobs[name].alone or jobs[name].gpu, name)
        self.assertEqual({n for n, j in jobs.items() if j.alone},
                         {"high-refresh", "battle-hw-pacing", "battle-speed", "netplaytest", "battlespeedtest",
                          "hostpolltest"})
        for pair in (("cdtest", "cdtest-check"), ("card_historytest", "card_historytest-restart"),
                     ("gameplay_combattest-write", "gameplay_combattest-read")):
            self.assertTrue(jobs[pair[0]].resources & jobs[pair[1]].resources, pair)


@unittest.skipIf(P.fcntl is None, "the GPU lock is flock's (not on Windows)")
class GpuLockTests(unittest.TestCase):
    """GPU tests run holding the shared GPU lock (xg-dby); CPU-only tests never take it."""
    exe, execute = SchedulerTests.exe, SchedulerTests.execute

    def setUp(self):
        SchedulerTests.setUp(self)
        self.lock = self.root / "gpu.lock"
        self.assertEqual(P.gpu_lock_path(), str(self.lock))

    def held(self):
        """Whether anything holds the lock (probed from a descriptor of our own)."""
        fd = os.open(self.lock, os.O_RDONLY | os.O_CREAT, 0o644)
        try:
            P.fcntl.flock(fd, P.fcntl.LOCK_EX | P.fcntl.LOCK_NB)
        except BlockingIOError:
            return True
        finally:
            os.close(fd)  # closing drops a probe's own lock
        return False

    def other_process(self):
        """Another process (an art capture's stand-in) holding the lock until killed."""
        script = ("import fcntl,os,sys,time; fd = os.open(sys.argv[1], os.O_RDONLY | os.O_CREAT, 0o644); "
                  "fcntl.flock(fd, fcntl.LOCK_EX); print('locked', flush=True); time.sleep(60)")
        process = subprocess.Popen([sys.executable, "-c", script, str(self.lock)],
                                   stdout=subprocess.PIPE, text=True)
        self.addCleanup(RunnerTests.stop, process)
        lines = queue.Queue()
        threading.Thread(target=lambda: lines.put(process.stdout.readline()), daemon=True).start()
        try:
            self.assertEqual(lines.get(timeout=10).strip(), "locked")
        except queue.Empty:
            self.fail("lock holder failed to start")
        return process

    def until(self, condition, what, timeout=10):
        end = time.monotonic() + timeout
        while not condition():
            if time.monotonic() > end:
                self.fail(f"timed out waiting for {what}")
            time.sleep(0.02)

    def test_default_path_and_override(self):
        with patch.dict(os.environ):
            os.environ.pop("XG_GPU_LOCK", None)
            self.assertEqual(P.gpu_lock_path(), "/tmp/xg-intel-gpu.lock")
            os.environ["XG_GPU_LOCK"] = ""
            self.assertEqual(P.gpu_lock_path(), "/tmp/xg-intel-gpu.lock")

    def test_gpu_test_waits_for_another_process_and_cpu_test_does_not(self):
        # Real child processes: each writes its marker when it starts.
        holder = self.other_process()
        marks = {n: self.root / f"{n}.started" for n in ("gpu", "cpu")}
        runner = P.Runner(self.build, gpu=True, pattern=None, listing=False, jobs=2)
        for n, target in (("gpu", "gputest"), ("cpu", "jpegtest")):
            runner.run(n, Path(sys.executable), "-c", "import sys; open(sys.argv[1], 'w').close()",
                       marks[n], target=target)
        self.assertEqual([t.job.gpu for t in runner.tests], [True, False])
        errors = []

        def execute():
            try:
                with redirect_stdout(io.StringIO()):
                    runner.execute()
            except BaseException as e:
                errors.append(e)
        thread = threading.Thread(target=execute, daemon=True)
        thread.start()
        # The CPU test runs to its end while the other process holds the lock...
        self.until(lambda: runner.tests[1].result is not None, "the CPU test")
        self.assertEqual(runner.tests[1].result[0], "ok", runner.tests[1].result)
        self.assertTrue(marks["cpu"].exists())
        # ...while the GPU test, started by the scheduler, waits in flock without starting.
        self.until(lambda: ("start", "gpu") in {(e, n) for e, n, _ in runner.trace}, "the GPU test's start")
        self.until(P.gpu_lock.mutex.locked, "the GPU test waiting for the lock")
        time.sleep(0.5)
        self.assertFalse(marks["gpu"].exists())
        self.assertIsNone(runner.tests[0].result)
        self.assertTrue(thread.is_alive())
        RunnerTests.stop(holder)  # the other process ends: its lock is released
        thread.join(20)
        self.assertFalse(thread.is_alive(), "the GPU test never ran after the release")
        self.assertEqual(errors, [])
        self.assertTrue(marks["gpu"].exists())
        self.assertEqual([r[1] for r in runner.results], ["ok", "ok"], runner.results)
        self.assertFalse(self.held())
        self.assertEqual(P.gpu_lock.users, 0)

    def probing(self, names):
        return {n: (lambda _, n=n: self.seen.append((n, self.held()))) for n in names}

    def test_every_route_holds_it_for_gpu_tests_only(self):
        self.seen = []
        names = ["gputest", "jpegtest", "distortiontest", "distortion-replay", "mdectest"]
        # --jobs 1: one after another on the main thread, nothing else running.
        runner = P.Runner(self.build, gpu=True, pattern=None, listing=False, jobs=1)
        for n in names[:3]:
            runner.run(n, self.exe(n))
        runner.run("distortion-replay", self.exe("distortiontest"), self.root, timeout=5 * P.TIMEOUT)
        runner.run("mdectest", self.exe("mdectest"))
        self.execute(runner, Children(self.probing(names)))
        self.assertEqual(self.seen, [("gputest", True), ("jpegtest", False), ("distortiontest", True),
                                     ("distortion-replay", True), ("mdectest", False)])
        self.assertFalse(self.held())
        # Parallel: a CLOCK GPU test and a CPU one alone on the main thread, and a GPU
        # test's rerun alone after failing when shared.
        self.seen = []
        runner = P.Runner(self.build, gpu=True, pattern=None, listing=False, jobs=4)
        runner.run("high-refresh", Path(sys.executable), "-c", "", target="highrefreshtest")
        runner.run("netplaytest", self.exe("netplaytest"))
        runner.run("gputest", self.exe("gputest"))
        runner.run("jpegtest", self.exe("jpegtest"))
        hooks = self.probing(["high-refresh", "netplaytest", "gputest"])
        printed = self.execute(runner, Children(hooks, codes={"gputest": lambda run: 1 if run == 0 else 0}))
        self.assertEqual(printed, ["high-refresh", "netplaytest", "gputest", "jpegtest"])
        self.assertIn(("retry", "gputest", frozenset()), runner.trace)
        self.assertEqual(self.seen, [("high-refresh", True), ("netplaytest", False),
                                     ("gputest", True), ("gputest", True)])
        self.assertFalse(self.held())
        self.assertEqual(P.gpu_lock.users, 0)

    def test_the_plans_distortion_replay_is_a_gpu_test(self):
        self.exe("distortiontest")
        runner = P.Runner(self.build, gpu=True, pattern="distortion-replay", listing=False)
        with redirect_stdout(io.StringIO()):
            runner.plan()
        self.assertEqual([(t.name, t.job.gpu) for t in runner.tests], [("distortion-replay", True)])

    def test_two_gpu_tests_share_this_runs_hold(self):
        # Both of this run's GPU slots run together under the one hold; the lock is held
        # throughout and released once both have ended.
        pair = threading.Barrier(2, timeout=10)
        seen = []
        hook = lambda _: (pair.wait(), seen.append(self.held()))
        runner = P.Runner(self.build, gpu=True, pattern=None, listing=False, jobs=4)
        for n in ("gputest", "presenttest"):
            runner.run(n, self.exe(n))
        self.execute(runner, Children({"gputest": hook, "presenttest": hook}))
        self.assertEqual(seen, [True, True])
        self.assertFalse(self.held())
        self.assertEqual(P.gpu_lock.users, 0)

    def test_released_on_errors(self):
        def boom(_):
            raise RuntimeError("child failed to launch")
        runner = P.Runner(self.build, gpu=True, pattern=None, listing=False, jobs=1)
        runner.run("gputest", self.exe("gputest"))
        with self.assertRaisesRegex(RuntimeError, "failed to launch"):
            self.execute(runner, Children({"gputest": boom}))
        self.assertFalse(self.held())
        # A timeout is a failure, and releases it too.
        def slow(argv, **kwargs):
            raise subprocess.TimeoutExpired(argv, 1)
        runner = P.Runner(self.build, gpu=True, pattern=None, listing=False, jobs=1)
        runner.run("gputest", self.exe("gputest"))
        with patch.object(P.subprocess, "run", side_effect=slow), redirect_stdout(io.StringIO()):
            runner.execute()
        self.assertEqual(runner.results[0][1], "FAIL")
        self.assertFalse(self.held())
        self.assertEqual(P.gpu_lock.users, 0)

    def test_unusable_lock_fails_the_gpu_test_without_running_it(self):
        patch.dict(os.environ, XG_GPU_LOCK=str(self.root / "no such dir" / "gpu.lock")).start()
        children = Children()
        runner = P.Runner(self.build, gpu=True, pattern=None, listing=False, jobs=1)
        runner.run("gputest", self.exe("gputest"))
        runner.run("jpegtest", self.exe("jpegtest"))
        self.execute(runner, children)
        self.assertEqual(children.started, ["jpegtest"])
        self.assertEqual(runner.results[0][1], "FAIL")
        self.assertIn("GPU lock", runner.results[0][2])
        self.assertEqual(P.gpu_lock.users, 0)

    def test_read_only_lock_file_is_usable(self):
        # Opened read-only, never created over: a lock file this account may not write
        # (another account's, under fs.protected_regular) still locks.
        self.lock.touch()
        self.lock.chmod(0o444)
        seen = []
        runner = P.Runner(self.build, gpu=True, pattern=None, listing=False, jobs=1)
        runner.run("gputest", self.exe("gputest"))
        self.execute(runner, Children({"gputest": lambda _: seen.append(self.held())}))
        self.assertEqual(seen, [True])
        self.assertEqual(runner.results[0][1], "ok")


class JobsOption(unittest.TestCase):
    def test_default_is_capped(self):
        self.assertTrue(1 <= P.default_jobs() <= P.DEFAULT_JOBS_CAP)

    def test_invalid_counts_are_refused(self):
        for text in ("0", "-1", "abc", "2.5", "", str(P.MAX_JOBS + 1)):
            with self.subTest(text=text):
                result = subprocess.run([sys.executable, str(Path(P.__file__).resolve()), "--jobs", text,
                                         "--build", "no-such-build", "--list"],
                                        capture_output=True, text=True, timeout=10)
                self.assertEqual(result.returncode, 2, result.stderr)
                self.assertIn("--jobs", result.stderr)
        self.assertEqual(P.job_count("1"), 1)
        self.assertEqual(P.job_count(str(P.MAX_JOBS)), P.MAX_JOBS)


if __name__ == "__main__":
    unittest.main()
