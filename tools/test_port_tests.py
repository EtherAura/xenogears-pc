#!/usr/bin/env python3
"""Port test runner isolation regressions; no game, GPU or disc data needed."""
import argparse
from concurrent.futures import ThreadPoolExecutor
from contextlib import redirect_stdout
import io
import os
from pathlib import Path
import queue
import re
import shutil
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
    def __init__(self, hooks=None, codes=None, gpu=(), texts=None):
        self.hooks, self.codes, self.gpu = hooks or {}, codes or {}, set(gpu)
        self.texts = texts or {}  # name -> function of the run's index: more output, after the hello
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
            if name in self.texts:
                kwargs["stdout"].write(self.texts[name](run))
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
        # A device-loss recovery's pause, recorded instead of slept (none of these runs loses it).
        self.settled = []
        patch.object(P, "settle", self.settled.append).start()

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
        self.assertEqual(self.settled, [])  # an ordinary failure gets no recovery rerun

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


# What the port prints before abort() when the Vulkan device is lost (port/src/gpu/vk.c).
LOST = "vulkan: vkQueueSubmit(vk.queue, 1, &si, vk.fence) failed (-4) at port/src/gpu/vk.c:2201"


class DeviceLossTests(unittest.TestCase):
    """A host GPU reset (xg-au4): a failure reporting a lost Vulkan device is marked so,
    is rerun alone after a pause at most P.DEVICE_RECOVERIES times over its whole
    outcome, and teaches alone-only.txt nothing; every other pass, failure and rerun
    is as before."""
    exe = SchedulerTests.exe

    def setUp(self):
        SchedulerTests.setUp(self)
        # Each recovery pause (mocked: no waiting): (seconds, the children running, this run's
        # GPU lock users, whether another holder could take the GPU lock file then).
        self.children = None
        patch.object(P, "settle", self.pause).start()

    def pause(self, secs):
        free, path = None, os.environ["XG_GPU_LOCK"]
        if os.path.exists(path):
            fd = os.open(path, os.O_RDONLY)
            try:
                P.fcntl.flock(fd, P.fcntl.LOCK_EX | P.fcntl.LOCK_NB)
                P.fcntl.flock(fd, P.fcntl.LOCK_UN)
                free = True
            except BlockingIOError:
                free = False
            finally:
                os.close(fd)
        active = frozenset(self.children.active) if self.children else None
        self.settled.append((secs, active, P.gpu_lock.users, free))

    def assert_paused(self, times, free=None):
        """times pauses, each of DEVICE_SETTLE with no child running and no GPU lock held."""
        self.assertEqual(self.settled, [(P.DEVICE_SETTLE, frozenset(), 0, free)] * times)

    def shared(self, codes, texts):
        """lossy and other run together (a barrier only both pass); lossy's rerun alone returns at once."""
        both = threading.Barrier(2, timeout=10)
        hooks = {"lossy": lambda _: both.wait() if children.started.count("lossy") == 1 else None,
                 "other": lambda _: both.wait()}
        children = Children(hooks, codes={"lossy": codes}, texts={"lossy": texts})
        return children

    def suite(self, jobs, children, plan=None):
        """run_tests() over lossy and other (or plan's): its exit status, its output and its runner."""
        runners = []
        self.children = children

        def planned(runner):
            runners.append(runner)
            if plan is not None:
                return plan(runner)
            runner.run("lossy", self.exe("lossytest"))
            runner.run("other", self.exe("othertest"))
        args = argparse.Namespace(no_build=True, list=False, no_gpu=False, pattern=None, jobs=jobs)
        stdout = io.StringIO()
        with patch.object(P.Runner, "plan", planned), patch.object(P.subprocess, "run", side_effect=children), \
             redirect_stdout(stdout):
            code = P.run_tests(self.build, args)
        return code, stdout.getvalue(), runners[0]

    def retries(self, runner):
        return [n for e, n, _ in runner.trace if e == "retry"]

    def recovers(self, runner):
        return [n for e, n, _ in runner.trace if e == "recover"]

    def lost(self, log):
        """A lost run's detail, its log (now) at OUT/log."""
        return f"DEVICE LOST ({LOST}): exit -6: {LOST} (log {P.display_path(self.out / log)})"

    def passed(self, detail, prefix, run):
        """detail is prefix then the passing run's own ok detail."""
        self.assertTrue(detail.startswith(prefix), f"{detail}\n!=\n{prefix}")
        self.assertRegex(detail[len(prefix):], rf"^lossy says hello \(run {run}\) \(\d+\.\ds\)$")

    def recovered(self, n, *runs):
        return (f"DEVICE LOST, passed on recovery rerun {n} of 2 alone; not kept alone (a host GPU reset is "
                f"not a sharing failure): {'; '.join(runs)}; then ")

    def test_only_the_vulkan_device_lost_line_is_recognised(self):
        for text in (LOST, "vulkan: vkCreateDevice failed (-4)", f"before\n{LOST}\nAborted (core dumped)\n"):
            with self.subTest(text=text):
                self.assertIsNotNone(P.device_lost(text))
        self.assertEqual(P.device_lost(f"a\n{LOST}\n"), LOST)
        for text in ("Aborted (core dumped)", "exit -4", "error -4", "failed (-4)", "VK_ERROR_DEVICE_LOST",
                     "vulkan: device lost", "vulkan: vkQueueSubmit failed (-3) at port/src/gpu/vk.c:1",
                     "vulkan: vkQueueSubmit failed (-42) at port/src/gpu/vk.c:1",
                     "vulkan: vkQueueSubmit failed (-4) at port/src/gpu/vk.c:1 and then",
                     "vulkan: failed (-4)", "gpu: vkQueueSubmit failed (-4)", "note: " + LOST, "  " + LOST, ""):
            with self.subTest(text=text):
                self.assertIsNone(P.device_lost(text))

    def test_serial_device_loss_stays_a_failure_and_fails_the_suite(self):
        children = Children(codes={"lossy": -6}, texts={"lossy": lambda run: LOST + "\n"})
        code, out, runner = self.suite(1, children)
        self.assertEqual(code, 1, out)
        self.assertIn("port-tests: 1 ok, 1 failed, 0 known, 0 skipped", out)
        self.assertEqual([(n, s) for n, s, _ in runner.results], [("lossy", "FAIL"), ("other", "ok")])
        # --jobs 1: no ordinary rerun, but exactly the two recovery reruns, before other starts.
        self.assertEqual(children.started, ["lossy", "lossy", "lossy", "other"])
        self.assertEqual(self.retries(runner), [])
        self.assertEqual(self.recovers(runner), ["lossy", "lossy"])
        self.assert_paused(2)
        self.assertEqual(runner.tests[0].job.recoveries, P.DEVICE_RECOVERIES)
        self.assertEqual(runner.results[0][2],
                         f"{self.lost('lossy.log')}; lost again on every recovery rerun alone (2 of 2): "
                         f"lost {self.lost('lossy.lost1.log')}; lost {self.lost('lossy.lost2.log')}")
        for log, run in (("lossy.lost1.log", 0), ("lossy.lost2.log", 1), ("lossy.log", 2)):
            self.assertEqual((self.out / log).read_text(), f"lossy says hello (run {run})\n{LOST}\n", log)
        self.assertFalse((self.out / P.ALONE_FILE).exists())

    def test_lost_when_shared_then_passing_alone_is_ok_disclosed_and_not_kept_alone(self):
        children = self.shared(lambda run: -6 if run == 0 else 0, lambda run: LOST + "\n" if run == 0 else "")
        code, out, runner = self.suite(2, children)
        self.assertEqual(code, 0, out)
        self.assertIn("port-tests: 2 ok, 0 failed, 0 known, 0 skipped", out)
        self.assertEqual([(n, s) for n, s, _ in runner.results], [("lossy", "ok"), ("other", "ok")])
        self.assertEqual(children.started.count("lossy"), 2)          # the rerun alone: recovery rerun 1
        self.assertEqual(children.runs["lossy"][1], set())
        self.assertEqual(self.retries(runner), ["lossy"])
        self.assertEqual(self.recovers(runner), ["lossy"])
        self.assert_paused(1)
        detail = runner.results[0][2]
        self.passed(detail, self.recovered(1, "shared " + self.lost("lossy.shared.log")), 1)
        self.assertNotIn("ONLY WHEN SHARED", detail)
        self.assertFalse((self.out / "lossy.lost1.log").exists())
        self.assertFalse((self.out / P.ALONE_FILE).exists())
        self.assertEqual(runner.alone_only, set())
        # The next run in this output directory still shares it.
        runner = P.Runner(self.build, gpu=True, pattern=None, listing=False, jobs=2)
        runner.run("lossy", self.exe("lossytest"))
        self.assertFalse(runner.tests[0].job.alone)

    def test_device_lost_every_run_fails_marked(self):
        children = self.shared(lambda run: -6, lambda run: LOST + "\n")
        code, out, runner = self.suite(2, children)
        self.assertEqual(code, 1, out)
        self.assertIn("port-tests: 1 ok, 1 failed, 0 known, 0 skipped", out)
        self.assertEqual([(n, s) for n, s, _ in runner.results], [("lossy", "FAIL"), ("other", "ok")])
        # The shared run, then the two recovery reruns (the rerun alone the first), each alone.
        self.assertEqual(children.started.count("lossy"), 3)
        self.assertEqual(children.runs["lossy"][1:], [set(), set()])
        self.assertEqual(self.retries(runner), ["lossy"])
        self.assertEqual(self.recovers(runner), ["lossy", "lossy"])
        self.assert_paused(2)
        self.assertEqual(runner.results[0][2],
                         f"{self.lost('lossy.log')}; lost again on every recovery rerun alone (2 of 2): "
                         f"shared {self.lost('lossy.shared.log')}; lost {self.lost('lossy.lost2.log')}")
        self.assertEqual(runner.tests[0].job.device_lost, [LOST, LOST, LOST])
        self.assertFalse((self.out / "lossy.lost1.log").exists())
        self.assertFalse((self.out / P.ALONE_FILE).exists())

    def test_lost_when_shared_then_failing_alone_stays_an_ordinary_failure(self):
        children = self.shared(lambda run: -6 if run == 0 else 4, lambda run: LOST + "\n" if run == 0 else "")
        code, out, runner = self.suite(2, children)
        self.assertEqual(code, 1, out)
        self.assertEqual(runner.results[0][1], "FAIL")
        self.assertEqual(children.started.count("lossy"), 2)          # an ordinary failure: no more reruns
        self.assertEqual(self.recovers(runner), ["lossy"])
        self.assert_paused(1)
        log = P.display_path(self.out / "lossy.log")
        self.assertEqual(runner.results[0][2],
                         f"exit 4: lossy says hello (run 1) (log {log}); after DEVICE LOST (not a sharing failure; "
                         f"recovery reruns alone: 1 of 2): shared {self.lost('lossy.shared.log')}")
        self.assertFalse((self.out / P.ALONE_FILE).exists())

    def test_generic_abort_failing_only_when_shared_is_still_kept_alone(self):
        # SIGABRT, a bare -4 and another Vulkan error are no device loss: the
        # ordinary sharing-only failure, reported and remembered as before.
        generic = "error -4\nvulkan: vkQueueSubmit failed (-3) at port/src/gpu/vk.c:1\nAborted (core dumped)\n"
        children = self.shared(lambda run: -6 if run == 0 else 0, lambda run: generic if run == 0 else "")
        code, out, runner = self.suite(2, children)
        self.assertEqual(code, 0, out)
        self.assertEqual([(n, s) for n, s, _ in runner.results], [("lossy", "ok"), ("other", "ok")])
        self.assertEqual(children.started.count("lossy"), 2)
        self.assertEqual(self.retries(runner), ["lossy"])
        detail = runner.results[0][2]
        self.assertTrue(detail.startswith("FAILED ONLY WHEN SHARED, alone from now on"), detail)
        self.assertIn("shared exit -6: Aborted (core dumped)", detail)
        self.assertNotIn("DEVICE LOST", detail)
        self.assertEqual(runner.tests[0].job.device_lost, [])
        self.assertEqual((self.out / P.ALONE_FILE).read_text(), "lossy\n")
        self.assertEqual(self.recovers(runner), [])
        self.assert_paused(0)

    def test_generic_serial_failure_report_is_unchanged(self):
        for text in ("error -4\nAborted (core dumped)\n",
                     "vulkan: vkQueueSubmit failed (-3) at port/src/gpu/vk.c:1\nAborted (core dumped)\n"):
            with self.subTest(text=text):
                self.settled.clear()
                children = Children(codes={"lossy": -6}, texts={"lossy": lambda run: text})
                code, out, runner = self.suite(1, children)
                self.assertEqual(code, 1, out)
                log = P.display_path(self.out / "lossy.log")
                self.assertEqual(runner.results[0][1:], ("FAIL", f"exit -6: Aborted (core dumped) (log {log})"))
                self.assertEqual(children.started, ["lossy", "other"])  # no recovery rerun
                self.assertEqual(self.recovers(runner), [])
                self.assert_paused(0)

    def test_device_lost_line_in_a_passing_run_changes_nothing(self):
        children = Children(texts={"lossy": lambda run: LOST + "\n"})
        code, out, runner = self.suite(1, children)
        self.assertEqual(code, 0, out)
        self.assertEqual(runner.results[0][1], "ok")
        self.assertTrue(runner.results[0][2].startswith(LOST), runner.results[0][2])
        self.assertEqual(runner.tests[0].job.device_lost, [])
        self.assert_paused(0)

    # Recovery reruns (xg-au4 points 2 and 3).

    def losing(self, losses, then=0):
        """lossy loses the device on its first `losses` runs, then exits `then`."""
        return (lambda run: -6 if run < losses else then), (lambda run: LOST + "\n" if run < losses else "")

    def test_serial_loss_then_pass_recovers_ok_marked_and_not_kept_alone(self):
        codes, texts = self.losing(1)
        children = Children(codes={"lossy": codes}, texts={"lossy": texts})
        code, out, runner = self.suite(1, children)
        self.assertEqual(code, 0, out)
        self.assertIn("port-tests: 2 ok, 0 failed, 0 known, 0 skipped", out)
        self.assertEqual(children.started, ["lossy", "lossy", "other"])
        self.assertTrue(all(t is threading.main_thread() for t in children.threads.values()))
        self.assert_paused(1)
        self.passed(runner.results[0][2], self.recovered(1, "lost " + self.lost("lossy.lost1.log")), 1)
        self.assertEqual((self.out / "lossy.lost1.log").read_text(), f"lossy says hello (run 0)\n{LOST}\n")
        self.assertFalse((self.out / P.ALONE_FILE).exists())

    def test_passing_on_the_last_allowed_recovery_rerun(self):
        for jobs in (1, 2):
            with self.subTest(jobs=jobs):
                self.settled.clear()
                codes, texts = self.losing(2)
                children = self.shared(codes, texts) if jobs == 2 else Children(codes={"lossy": codes},
                                                                                texts={"lossy": texts})
                code, out, runner = self.suite(jobs, children)
                self.assertEqual(code, 0, out)
                self.assertEqual(children.started.count("lossy"), 3)
                self.assertEqual(self.recovers(runner), ["lossy", "lossy"])
                self.assert_paused(2)
                first = ("shared " + self.lost("lossy.shared.log") if jobs == 2 else
                         "lost " + self.lost("lossy.lost1.log"))
                self.passed(runner.results[0][2], self.recovered(2, first, "lost " + self.lost("lossy.lost2.log")), 2)
                self.assertFalse((self.out / P.ALONE_FILE).exists())

    def test_unshared_worker_loss_and_alone_tests_recover_with_nothing_running(self):
        codes, texts = self.losing(1)
        cases = {  # plan, the test that loses the device, the scheduler's first events
            "worker": (lambda r: r.run("lossy", self.exe("lossytest")), "lossy", ["start", "recover"]),
            "clock": (lambda r: (r.run("netplaytest", self.exe("netplaytest")), r.run("other", self.exe("othertest"))),
                      "netplaytest", ["alone", "recover"]),
            "alone-only": (lambda r: (r.run("lossy", self.exe("lossytest")), r.run("other", self.exe("othertest"))),
                           "lossy", ["alone", "recover"]),
        }
        for case, (plan, name, events) in cases.items():
            with self.subTest(case=case):
                self.settled.clear()
                (self.out / P.ALONE_FILE).unlink(missing_ok=True)
                if case == "alone-only":
                    self.out.mkdir(parents=True, exist_ok=True)
                    (self.out / P.ALONE_FILE).write_text("lossy\n")
                children = Children(codes={name: codes}, texts={name: texts})
                code, out, runner = self.suite(2, children, plan)
                self.assertEqual(code, 0, out)
                self.assertEqual([e for e, n, _ in runner.trace if n == name], events)
                self.assertEqual(children.runs[name], [set(), set()])
                self.assertIs(children.threads[name], threading.main_thread())
                self.assert_paused(1)
                self.passed(runner.results[0][2].replace(name, "lossy"),
                            self.recovered(1, "lost " + self.lost("lossy.lost1.log")), 1)
                if case == "alone-only":  # kept as it was: neither removed nor added to
                    self.assertEqual((self.out / P.ALONE_FILE).read_text(), "lossy\n")
                else:
                    self.assertFalse((self.out / P.ALONE_FILE).exists())

    def test_recovery_is_bounded_over_the_whole_outcome(self):
        # An ordinary shared failure, then a lost device on the rerun alone and on both recovery
        # reruns: four runs in all, one ordinary rerun, two recoveries; failed, marked, not kept alone.
        children = self.shared(lambda run: 4 if run == 0 else -6, lambda run: "" if run == 0 else LOST + "\n")
        code, out, runner = self.suite(2, children)
        self.assertEqual(code, 1, out)
        self.assertEqual(children.started.count("lossy"), 4)
        self.assertEqual(self.retries(runner), ["lossy"])
        self.assertEqual(self.recovers(runner), ["lossy", "lossy"])
        self.assert_paused(2)
        shared = f"exit 4: lossy says hello (run 0) (log {P.display_path(self.out / 'lossy.shared.log')})"
        self.assertEqual(runner.results[0][2],
                         f"{self.lost('lossy.log')}; lost again on every recovery rerun alone (2 of 2): "
                         f"shared {shared}; lost {self.lost('lossy.lost1.log')}; lost {self.lost('lossy.lost2.log')}")
        self.assertFalse((self.out / P.ALONE_FILE).exists())
        # The same with a recovery passing: ok, the loss told, and not kept alone (a run lost the device).
        self.settled.clear()
        children = self.shared(lambda run: {0: 4, 1: -6}.get(run, 0), lambda run: LOST + "\n" if run == 1 else "")
        code, out, runner = self.suite(2, children)
        self.assertEqual(code, 0, out)
        self.assertEqual(children.started.count("lossy"), 3)
        self.assert_paused(1)
        self.passed(runner.results[0][2], self.recovered(1, f"shared {shared}", "lost " + self.lost("lossy.lost1.log")), 2)
        self.assertNotIn("ONLY WHEN SHARED", runner.results[0][2])
        self.assertFalse((self.out / P.ALONE_FILE).exists())

    def test_serial_loss_then_ordinary_failure_stays_that_failure(self):
        codes, texts = self.losing(1, then=4)
        children = Children(codes={"lossy": codes}, texts={"lossy": texts})
        code, out, runner = self.suite(1, children)
        self.assertEqual(code, 1, out)
        self.assertEqual(children.started, ["lossy", "lossy", "other"])
        self.assert_paused(1)
        log = P.display_path(self.out / "lossy.log")
        self.assertEqual(runner.results[0][1:], (
            "FAIL", f"exit 4: lossy says hello (run 1) (log {log}); after DEVICE LOST (not a sharing failure; "
                    f"recovery reruns alone: 1 of 2): lost {self.lost('lossy.lost1.log')}"))
        self.assertFalse((self.out / P.ALONE_FILE).exists())

    def test_a_recovery_rerun_must_match_its_expected_output(self):
        # The rerun exits 0 but misses the pattern: it fails as any run does, the loss told.
        def plan(r):
            r.run("lossy", self.exe("lossytest"), expect=r"never printed")
        codes, texts = self.losing(1)
        children = Children(codes={"lossy": codes}, texts={"lossy": texts})
        code, out, runner = self.suite(1, children, plan)
        self.assertEqual(code, 1, out)
        self.assertEqual(runner.results[0][1], "FAIL")
        self.assertTrue(runner.results[0][2].startswith("exit no match: lossy says hello (run 1)"),
                        runner.results[0][2])
        self.assertIn("; after DEVICE LOST (not a sharing failure; recovery reruns alone: 1 of 2)",
                      runner.results[0][2])

    def coordination(self, first):
        """--jobs 2: lossy (a GPU test) loses the device beside slow, then on its first
        recovery rerun, and passes the second. slow-next follows slow in its chain and
        spare is ready all along, at first without a free worker.

        Every order is fixed by events, none by timing (each wait bounded): lossy and slow
        overlap (a barrier). first="lossy": slow ends only once the scheduler has taken
        lossy's lost run and come back for the next result, so a scheduler that still
        started tests then would have started spare in the freed worker. first="slow":
        slow's result arrives while lossy still runs, the loss not known yet, so slow-next
        starts beside lossy; lossy ends once it has, and slow-next only once the scheduler
        has taken the lost run and come back: the recovery has to wait for it.
        The runner's result queue is the stand-in below only to see those moments.
        """
        reported = {name: threading.Event() for name in ("lossy", "slow")}
        took_loss, draining, next_running = [], threading.Event(), threading.Event()

        def ours(item):  # (the pool's own queue of work is a SimpleQueue too)
            return isinstance(item, tuple) and item and isinstance(item[0], P.Test)

        class Reports(queue.SimpleQueue):
            def put(self, item, block=True, timeout=None):
                super().put(item, block, timeout)
                if ours(item) and item[0].name in reported:
                    reported[item[0].name].set()

            def get(self, block=True, timeout=None):
                scheduler = threading.current_thread() is threading.main_thread()
                if scheduler and took_loss:
                    draining.set()  # back for the next result: it has decided what to start
                item = super().get(block, timeout)
                if scheduler and ours(item) and item[0].name == "lossy":
                    took_loss.append(item)
                return item

        def wait(event, what):
            if not event.wait(10):
                raise RuntimeError(f"timed out waiting for {what}")

        both = threading.Barrier(2, timeout=10)
        held = []

        def lossy(_):
            held.append(P.gpu_lock.users)
            if children.started.count("lossy") == 1:  # the shared run; the reruns return at once
                both.wait()
                if first == "slow":
                    wait(reported["slow"], "slow's result")
                    wait(next_running, "slow-next beside lossy")

        def slow(_):
            both.wait()
            if first == "lossy":
                wait(reported["lossy"], "lossy's result")
                wait(draining, "the scheduler draining")

        def slow_next(_):
            next_running.set()
            if first == "slow":
                wait(reported["lossy"], "lossy's result")
                wait(draining, "the scheduler draining")

        codes, texts = self.losing(2)
        children = Children({"lossy": lossy, "slow": slow, "slow-next": slow_next},
                            codes={"lossy": codes}, texts={"lossy": texts}, gpu={"lossy"})

        def plan(r):
            r.run("lossy", self.exe("lossytest"), target="gputest")
            r.run("slow", self.exe("slowtest"), "1", target="slowtest")
            r.run("slow-next", self.exe("slowtest"), "2", target="slowtest")  # one chain: slow, then slow-next
            r.run("spare", self.exe("sparetest"))
        self.settled.clear()
        with patch.object(P.queue, "SimpleQueue", Reports):
            code, out, runner = self.suite(2, children, plan)
        self.assertEqual(code, 0, out)
        self.assertIn("port-tests: 4 ok, 0 failed, 0 known, 0 skipped", out)
        self.assertTrue(runner.tests[0].job.gpu)
        self.assertEqual(held, [1, 1, 1])           # each lossy run held the (temporary) GPU lock...
        self.assert_paused(2, free=True)            # ...and each pause had no child and no hold of it
        self.assertEqual(P.gpu_lock.users, 0)
        self.assertEqual(children.active, set())
        # The reruns: nothing running when they started, nothing started during them, on the scheduler's thread.
        self.assertEqual(children.runs["lossy"][1:], [set(), set()])
        self.assertIs(children.threads["lossy"], threading.main_thread())
        self.assertEqual(set(children.started[:2]), {"lossy", "slow"})
        self.assertEqual(children.overlaps["spare"] - {"slow-next"}, set())
        none, beside = frozenset(), frozenset({"lossy"})
        recovery = [("retry", "lossy", none), ("recover", "lossy", none), ("recover", "lossy", none)]
        if first == "lossy":
            self.assertEqual(children.runs["lossy"][0], {"slow"})
            self.assertEqual(children.started[2:4], ["lossy", "lossy"])
            self.assertEqual(set(children.started[4:]), {"slow-next", "spare"})
            self.assertEqual(runner.trace, [("start", "lossy", none), ("start", "slow", beside), *recovery,
                                            ("start", "slow-next", none),
                                            ("start", "spare", frozenset({"slow-next"}))])
        else:
            self.assertEqual(children.runs["lossy"][0], {"slow", "slow-next"})
            self.assertEqual(children.started[2:], ["slow-next", "lossy", "lossy", "spare"])
            self.assertEqual(children.finished[:3], ["slow", "lossy", "slow-next"])
            self.assertEqual(runner.trace, [("start", "lossy", none), ("start", "slow", beside),
                                            ("start", "slow-next", beside), *recovery, ("start", "spare", none)])
        self.passed(runner.results[0][2],
                    self.recovered(2, "shared " + self.lost("lossy.shared.log"), "lost " + self.lost("lossy.lost2.log")), 2)
        self.assertFalse((self.out / P.ALONE_FILE).exists())

    @unittest.skipIf(P.fcntl is None, "the GPU lock is flock's (not on Windows)")
    def test_recovery_waits_for_every_other_child_and_nothing_starts_meanwhile(self):
        for first in ("lossy", "slow"):
            for round_ in range(20):
                with self.subTest(first=first, round=round_):
                    self.coordination(first)

    def test_timeout_and_other_failures_without_the_line_get_no_recovery(self):
        def slow(argv):
            raise subprocess.TimeoutExpired(argv, 1)
        for jobs in (1, 2):
            with self.subTest(jobs=jobs):
                children = Children({"lossy": slow})
                code, out, runner = self.suite(jobs, children)
                self.assertEqual(code, 1, out)
                self.assertIn("exit timeout", runner.results[0][2])
                self.assertNotIn("DEVICE LOST", runner.results[0][2])
                # --jobs 2: the one ordinary rerun alone of a failure when shared, as ever.
                self.assertEqual(children.started.count("lossy"), jobs)
                self.assertEqual(self.retries(runner), ["lossy"] * (jobs - 1))
                self.assertEqual((self.recovers(runner), runner.tests[0].job.recoveries), ([], 0))
                self.assert_paused(0)
                self.assertEqual(list(self.out.glob("lossy.lost*.log")), [])

    @unittest.skipIf(P.fcntl is None, "SIGABRT, RLIMIT_CORE and waitpid's ECHILD are POSIX's")
    def test_recovery_reruns_are_real_children_reaped_before_the_next(self):
        # No stand-in for subprocess.run: a real child that loses the "device" (the port's line, then
        # SIGABRT with core dumps off) until its third run, told apart by a counter file in its scratch.
        # RLIMIT_CORE 0 doesn't stop a dump when core_pattern is a pipe (systemd-coredump), so on Linux
        # the child also makes itself undumpable (PR_SET_DUMPABLE 0) and reads the flag back just
        # before aborting; if that fails it exits without the line, an ordinary failure, not a loss.
        script = ("import os, resource, sys\n"
                  "from pathlib import Path\n"
                  "runs = Path(sys.argv[1])\n"
                  "n = int(runs.read_text()) + 1 if runs.exists() else 1\n"
                  "runs.write_text(str(n))\n"
                  "print(f'pid {os.getpid()} run {n}', flush=True)\n"
                  "if n < 3:\n"
                  "    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))\n"
                  "    if sys.platform.startswith('linux'):\n"
                  "        import ctypes\n"
                  "        libc = ctypes.CDLL(None, use_errno=True)\n"
                  "        if libc.prctl(4, 0, 0, 0, 0) != 0:  # PR_SET_DUMPABLE\n"
                  "            sys.exit(f'PR_SET_DUMPABLE failed: errno {ctypes.get_errno()}')\n"
                  "        dumpable = libc.prctl(3, 0, 0, 0, 0)  # PR_GET_DUMPABLE\n"
                  "        if dumpable != 0:\n"
                  "            sys.exit(f'still dumpable: PR_GET_DUMPABLE returned {dumpable}')\n"
                  "    print(sys.argv[2], flush=True)\n"
                  "    os.abort()\n")
        counter = self.root / "runs.txt"
        runner = P.Runner(self.build, gpu=True, pattern=None, listing=False, jobs=2)
        runner.run("lossy", Path(sys.executable), "-c", script, counter, LOST, timeout=60)
        with redirect_stdout(io.StringIO()):
            runner.execute()
        self.assertEqual(runner.results[0][1], "ok", runner.results)
        self.assertTrue(runner.results[0][2].startswith(
            self.recovered(2, "lost " + self.lost("lossy.lost1.log"), "lost " + self.lost("lossy.lost2.log"))),
            runner.results[0][2])
        self.assertEqual(counter.read_text(), "3")
        self.assertEqual([e for e, _, _ in runner.trace], ["start", "recover", "recover"])
        self.assertEqual(self.settled, [(P.DEVICE_SETTLE, None, 0, None)] * 2)  # (no stand-in children; a CPU test)
        pids = []
        for log, run in (("lossy.lost1.log", 1), ("lossy.lost2.log", 2), ("lossy.log", 3)):
            words = (self.out / log).read_text().split()
            self.assertEqual(words[2:4], ["run", str(run)], log)
            self.assertEqual(LOST in (self.out / log).read_text(), run < 3, log)
            pids.append(int(words[1]))
        for pid in pids:  # each child was waited for: none is left, running or a zombie
            with self.assertRaises(ChildProcessError, msg=pid):
                os.waitpid(pid, os.WNOHANG)
        self.assertFalse((self.out / P.ALONE_FILE).exists())

    def test_logs_and_losses_belong_to_their_own_run_and_invocation(self):
        tags = {}

        def invocation(tag, losses):
            self.settled.clear()
            tags["tag"] = tag
            children = Children(codes={"lossy": lambda run: -6 if run < losses else 0},
                                texts={"lossy": lambda run: f"{tags['tag']}\n" + (LOST + "\n" if run < losses else "")})
            return self.suite(1, children)
        code, out, runner = invocation("first", 1)
        self.assertEqual((code, runner.tests[0].job.device_lost), (0, [LOST]))
        self.assertEqual((self.out / "lossy.lost1.log").read_text(), f"lossy says hello (run 0)\nfirst\n{LOST}\n")
        # A later invocation passing at once: no mark, no rerun, its own log; the earlier
        # invocation's lost run is neither cited nor left to be taken for this one's.
        (self.out / "lossy.lost2.log").write_text("stale\n")
        (self.out / "other.lost1.log").write_text("stale\n")
        code, out, runner = invocation("second", 0)
        self.assertEqual(code, 0, out)
        self.assertNotIn("lost", runner.results[0][2])
        self.assertEqual(list(self.out.glob("*.lost*.log")), [])
        self.assertEqual(runner.results[0][2][:len("second (")], "second (")
        self.assertEqual((runner.tests[0].job.device_lost, runner.tests[0].job.recoveries), ([], 0))
        self.assert_paused(0)
        self.assertEqual((self.out / "lossy.log").read_text(), "lossy says hello (run 0)\nsecond\n")
        # A third losing once: its lost run's log replaces the first invocation's.
        code, out, runner = invocation("third", 1)
        self.assertEqual(code, 0, out)
        self.assertEqual((self.out / "lossy.lost1.log").read_text(), f"lossy says hello (run 0)\nthird\n{LOST}\n")
        self.assertFalse((self.out / P.ALONE_FILE).exists())

    def test_known_failure_reporting_the_loss_is_not_rerun(self):
        children = Children(codes={"lossy": -6}, texts={"lossy": lambda run: LOST + "\n"})
        with patch.dict(P.KNOWN, {"lossytest": "xg-test"}):
            code, out, runner = self.suite(2, children)
        self.assertEqual(runner.results[0][1], "known")
        self.assertEqual(children.started.count("lossy"), 1)
        self.assert_paused(0)


# test_battle_speed.py's line for a game run that died of SIGABRT (xg-oevj), as it writes it.
ABORT_MARK = "child abort (diagnostic, not a result): "
ABORT = ("4x: battle speed 4, step clock, until vblank 3000, US: exit -6 (killed by SIGABRT), as nice and "
         "timeout returned it; 4x/failed.1.log: fake game: booting | xenogears: src/system/main.c:99: main: "
         "Assertion `0' failed.")


class ChildAbortTests(unittest.TestCase):
    """A test's report of a child that aborted reaches the summary line, the one line of the
    suite tools/gates.sh keeps, even when a rerun passes; it changes no status, rerun or result,
    and its text passes for no marker. The children are Children (mocked subprocess.run)."""
    D = DeviceLossTests
    exe, setUp, pause, assert_paused, shared, suite = D.exe, D.setUp, D.pause, D.assert_paused, D.shared, D.suite
    retries, recovers, lost, passed, recovered = D.retries, D.recovers, D.lost, D.passed, D.recovered

    def summary(self, out):
        """The line tools/gates.sh keeps of the suite's output (its first starting 'port-tests')."""
        return next(x for x in out.splitlines() if x.startswith("port-tests"))

    def test_abort_reported_when_the_rerun_alone_passes(self):
        children = self.shared(lambda run: 1 if run == 0 else 0,
                               lambda run: ABORT_MARK + ABORT + "\nbattle-speed: 1 failed\n" if run == 0 else "")
        code, out, runner = self.suite(2, children)
        self.assertEqual(code, 0, out)  # the pass decision is the rerun's, as before
        self.assertEqual([(n, s) for n, s, _ in runner.results], [("lossy", "ok"), ("other", "ok")])
        self.assertIn("FAILED ONLY WHEN SHARED", runner.results[0][2])
        self.assertEqual(self.retries(runner), ["lossy"])
        self.assertEqual(self.recovers(runner), [])
        self.assertEqual(self.summary(out), "port-tests: 2 ok, 0 failed, 0 known, 0 skipped; 1 child abort(s), "
                                            f"diagnostics, not results: [1] lossy: {ABORT}")
        self.assertEqual(1, out.count("Assertion"))  # in the summary only: no test line carries it

    def test_abort_reported_when_a_recovery_rerun_passes(self):
        children = self.shared(lambda run: 1 if run == 0 else 0,
                               lambda run: f"{ABORT_MARK}{ABORT}\n{LOST}\n" if run == 0 else "")
        code, out, runner = self.suite(2, children)
        self.assertEqual(code, 0, out)
        self.passed(runner.results[0][2], self.recovered(1, "shared " + self.lost("lossy.shared.log")
                                                         .replace("exit -6", "exit 1")), 1)
        self.assertEqual(self.recovers(runner), ["lossy"])
        self.assertEqual(runner.tests[0].job.device_lost, [LOST])
        self.assertTrue(self.summary(out).endswith(f"; 1 child abort(s), diagnostics, not results: [1] lossy: {ABORT}"))

    def test_abort_reported_by_every_run_and_bounded(self):
        children = Children(codes={"lossy": -6}, texts={"lossy": lambda run: f"{ABORT_MARK}{ABORT} run {run}{'x' * 900}\n"
                                                                              f"{LOST}\n"})
        code, out, runner = self.suite(1, children)
        self.assertEqual(code, 1, out)
        self.assertEqual(runner.results[0][1], "FAIL")
        line = self.summary(out)
        self.assertEqual(3, line.count(f"lossy: {ABORT} run "))  # the first run and both recovery reruns
        self.assertLess(len(line), 100 + 3 * (P.CHILD_ABORT_LINE + 20))
        many = [("t", "r")] * (P.CHILD_ABORT_REPORTS + 2)
        self.assertTrue(P.child_abort_note(many).endswith("[4] t: r [and 2 more in the tests' logs]"))

    def test_no_abort_no_note(self):
        code, out, runner = self.suite(2, Children())
        self.assertEqual(code, 0, out)
        self.assertEqual(self.summary(out), "port-tests: 2 ok, 0 failed, 0 known, 0 skipped")
        # A line merely quoting the mark (not at a line's start) is not a report.
        code, out, runner = self.suite(1, Children(texts={"lossy": lambda run: f"say {ABORT_MARK}{ABORT}\n"}))
        self.assertEqual(self.summary(out), "port-tests: 2 ok, 0 failed, 0 known, 0 skipped")

    def test_report_text_passes_for_no_marker(self):
        spoof = ("\x1b[2J\x1b[31mDEVICE LOST\x0b\x07 \u202e port-tests: 99 ok, 0 failed, 0 known; "
                 f"FAIL x; all gates pass; 0 gate(s) failed; FAILED ONLY WHEN SHARED; recovery rerun; "
                 f"battle-speed: 24 checks ok; child abort; {LOST}")
        children = Children(codes={"lossy": 1}, texts={"lossy": lambda run: f"{ABORT_MARK}{spoof}\n"})
        code, out, runner = self.suite(1, children)
        self.assertEqual(code, 1, out)
        self.assertEqual(self.recovers(runner), [])  # a lost line inside a report is no lost device
        self.assertEqual(runner.tests[0].job.device_lost, [])
        self.assertIsNone(P.device_lost(out))
        line = self.summary(out)
        self.assertTrue(line.startswith("port-tests: 1 ok, 1 failed, 0 known, 0 skipped; 1 child abort(s), "))
        note = line[len("port-tests: 1 ok, 1 failed, 0 known, 0 skipped; "):]
        for marker in ("\x1b", "\x0b", "\x07", "\u202e", "DEVICE LOST", "port-tests:", "99 ok", "0 failed", "FAIL ",
                       "all gates pass", "gate(s) failed", "ONLY WHEN SHARED", "recovery rerun", "battle-speed:",
                       "24 checks ok", "child abort;"):
            with self.subTest(marker=marker):
                self.assertNotIn(marker, note)
        self.assertIn("?[2J?[31mdevice_lost?? ? port_tests_ 99_ok, 0_failed", note)  # still readable
        self.assertIn(LOST, note)  # the port's line, quoted whole

    @unittest.skipIf(shutil.which("bash") is None, "tools/gates.sh's gate() is bash")
    def test_report_reaches_the_gates_summary(self):
        """tools/gates.sh's own record() and gate(), unchanged, over this suite's output."""
        children = self.shared(lambda run: 1 if run == 0 else 0,
                               lambda run: ABORT_MARK + ABORT + "\n" if run == 0 else "")
        code, out, runner = self.suite(2, children)
        saved = self.root / "suite.txt"
        saved.write_text(out)
        gates = (P.ROOT / "tools" / "gates.sh").read_text()
        funcs = [m.group(0) for m in re.finditer(r"(?ms)^(record|gate)\(\) \{$.*?^\}$", gates)]
        self.assertEqual(2, len(funcs))
        script = "\n".join(["failed=0", "summary=()", *funcs, f'gate port-tests cat "{saved}"',
                            "printf '  %s\\n' \"${summary[@]}\""])
        p = subprocess.run(["bash", "-c", script], env={**os.environ, "OUT": str(self.root)},
                           stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        self.assertEqual(p.returncode, 0, p.stdout)
        self.assertEqual(p.stdout, f"  ok    port-tests     port-tests: 2 ok, 0 failed, 0 known, 0 skipped; "
                                   f"1 child abort(s), diagnostics, not results: [1] lossy: {ABORT}\n")


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


# A stand-in for the game (no GPU, no disc): it notes, in its working directory
# (the run's scratch), its clock, whether the GPU lock was held while it ran
# (probed from a descriptor of its own) and whether it inherited one of the lock.
# FAKE_WAIT_FOR=N: wait (10 s at most) until N runs have started beside it.
FAKE_GAME = """\
import fcntl, os, sys, time
from pathlib import Path
lock = os.environ["XG_GPU_LOCK"]
held = False
if os.path.exists(lock):  # nothing made it: nothing holds it
    fd = os.open(lock, os.O_RDONLY)
    try:
        fcntl.flock(fd, fcntl.LOCK_EX | fcntl.LOCK_NB)
    except BlockingIOError:
        held = True
    os.close(fd)
inherited = any(os.path.realpath(f"/proc/self/fd/{n}") == os.path.realpath(lock) for n in os.listdir("/proc/self/fd"))
Path("child.txt").write_text(f"{os.environ.get('XG_CLOCK', 'real')} held={held} inherited={inherited}\\n")
n = int(os.environ.get("FAKE_WAIT_FOR", "0"))
end = time.monotonic() + 10
while n and len(list(Path("..").glob("*/child.txt"))) < n:
    if time.monotonic() > end:
        sys.exit(9)
    time.sleep(0.02)
sys.exit(int(os.environ.get("FAKE_EXIT", "0")))
"""

# test_battle_speed.py's runs with the stand-in game, as a process of their own
# (port_tests' battle-speed job): the step-clock run, then the real-clock one.
DRIVER = """\
import sys
from pathlib import Path
sys.path.insert(0, sys.argv[1])
import test_battle_speed as B
fake = sys.argv[4]
B.statecheck.executable = lambda build: ([sys.executable, fake], Path(fake))
out = Path(sys.argv[3])
codes = [B.run(Path(sys.argv[2]), out / n, 4, 10, False, real=r) for n, r in (("step", False), ("real", True))]
print("driver", codes, flush=True)
sys.exit(max(codes))
"""


@unittest.skipIf(P.fcntl is None or not Path("/proc/self/fd").is_dir(), "the GPU lock is flock's (not on Windows)")
class BattleSpeedLockTests(unittest.TestCase):
    """test_battle_speed.py runs every game, step-clock and real-clock, holding the GPU
    lock (xg-dby): its own hold, port_tests holding none around it (SELF_LOCKED)."""
    held, other_process, until = GpuLockTests.held, GpuLockTests.other_process, GpuLockTests.until

    def setUp(self):
        SchedulerTests.setUp(self)
        self.lock = self.root / "gpu.lock"
        self.fake = self.root / "fake_game.py"
        self.fake.write_text(FAKE_GAME)
        import test_battle_speed as B
        self.B = B
        patch.object(B.statecheck, "executable", lambda build: ([sys.executable, str(self.fake)], self.fake)).start()
        self.assertIs(B.port_tests, P)

    def launch(self, out, real):
        """One battle-speed run in a thread of this process: its result box and thread."""
        box = []
        thread = threading.Thread(target=lambda: box.append(self.B.run(self.build, out, 4, 10, False, real=real)),
                                  daemon=True)
        thread.start()
        self.addCleanup(thread.join, 30)
        return box, thread

    def test_each_launch_waits_for_another_process(self):
        for real, clock in ((False, "step"), (True, "real")):
            with self.subTest(clock=clock):
                out = self.root / clock
                holder = self.other_process()
                try:
                    box, thread = self.launch(out, real)
                    self.until(P.gpu_lock.mutex.locked, f"the {clock} run waiting for the lock")
                    time.sleep(0.5)
                    self.assertFalse((out / "child.txt").exists(), "the game ran while another process held the lock")
                    self.assertTrue(thread.is_alive())
                finally:
                    RunnerTests.stop(holder)
                thread.join(20)
                self.assertFalse(thread.is_alive(), "the game never ran after the release")
                self.assertEqual(box, [0])
                self.assertEqual((out / "child.txt").read_text(), f"{clock} held=True inherited=False\n")
                self.assertFalse(self.held())
                self.assertEqual(P.gpu_lock.users, 0)

    def test_side_by_side_step_runs_share_one_hold(self):
        # main()'s pool: the step-clock runs overlap under the one flock (no nested hold
        # of this process's own lock), each holding it throughout.
        patch.dict(os.environ, FAKE_WAIT_FOR="4").start()
        names = ["off", "1x", "2x", "4x"]
        with ThreadPoolExecutor(max_workers=4) as pool:
            futures = [pool.submit(self.B.run, self.build, self.root / "runs" / n, None, 10, False) for n in names]
            codes = [f.result(timeout=30) for f in futures]
        self.assertEqual(codes, [0, 0, 0, 0])
        for n in names:
            self.assertEqual((self.root / "runs" / n / "child.txt").read_text(), "step held=True inherited=False\n")
        self.assertFalse(self.held())
        self.assertEqual(P.gpu_lock.users, 0)

    def test_released_on_failure_and_errors(self):
        # The game fails: its exit status, and the lock released.
        patch.dict(os.environ, FAKE_EXIT="3").start()
        for real in (False, True):
            self.assertEqual(self.B.run(self.build, self.root / f"fail-{real}", 4, 10, False, real=real), 3)
            self.assertFalse(self.held())
        # The launch raises: released too.
        with patch.object(self.B.subprocess, "run", side_effect=RuntimeError("failed to launch")):
            with self.assertRaisesRegex(RuntimeError, "failed to launch"):
                self.B.run(self.build, self.root / "raises", 4, 10, False)
        self.assertFalse(self.held())
        self.assertEqual(P.gpu_lock.users, 0)
        # A lock that cannot be opened: the run fails without the game starting.
        patch.dict(os.environ, XG_GPU_LOCK=str(self.root / "no such dir" / "gpu.lock")).start()
        out = self.root / "unusable"
        self.assertEqual(self.B.run(self.build, out, 4, 10, False), 1)
        self.assertFalse((out / "child.txt").exists())
        self.assertIn("GPU lock", (out / "port.log").read_text())
        self.assertEqual(P.gpu_lock.users, 0)

    def job(self, name, timeout):
        driver = self.root / "driver.py"
        driver.write_text(DRIVER)
        argv = [sys.executable, str(driver), str(P.ROOT / "tools"), str(self.build), str(self.root / name), str(self.fake)]
        return P.Job(name, argv, dict(os.environ), timeout, None, "xenogears", set())

    def test_port_tests_job_waits_without_nesting_and_cpu_jobs_go_on(self):
        # port_tests' battle-speed job (a GPU test) holds no lock around the script: the
        # script's own runs wait for the other process, then both run; a CPU-only job
        # meanwhile runs to its end without taking the lock.
        job = self.job("battle-speed", 60)
        self.assertTrue(job.gpu)
        self.assertIn(job.name, P.SELF_LOCKED)
        holder = self.other_process()
        box = []
        thread = threading.Thread(target=lambda: box.append(P.run_job(job)), daemon=True)
        thread.start()
        self.addCleanup(thread.join, 70)
        mark = self.root / "cpu.started"
        cpu = P.Job("jpegtest", [sys.executable, "-c", "import sys; open(sys.argv[1], 'w').close()", str(mark)],
                    dict(os.environ), 10, None, "jpegtest", set())
        self.assertFalse(cpu.gpu)
        self.assertEqual(P.run_job(cpu)[0], "ok")
        self.assertTrue(mark.exists())
        self.assertFalse(P.gpu_lock.mutex.locked())
        self.assertEqual(P.gpu_lock.users, 0)
        self.until(lambda: (self.root / "battle-speed" / "step" / "pad.txt").exists(), "the script's first run")
        time.sleep(0.5)
        self.assertFalse((self.root / "battle-speed" / "step" / "child.txt").exists(),
                         "the game ran while another process held the lock")
        self.assertTrue(thread.is_alive())
        RunnerTests.stop(holder)
        thread.join(60)
        self.assertFalse(thread.is_alive(), "the battle-speed job hung")
        self.assertEqual(box[0][0], "ok", box)
        self.assertIn("driver [0, 0]", box[0][1])
        for clock in ("step", "real"):
            self.assertEqual((self.root / "battle-speed" / clock / "child.txt").read_text(),
                             f"{clock} held=True inherited=False\n")
        self.assertFalse(self.held())

    def test_the_exemption_is_what_keeps_it_from_deadlocking(self):
        # Were port_tests to hold the lock around the script, its own first run would
        # wait for it forever: here, to the job's timeout.
        job = self.job("battle-speed-held", 3)
        self.assertNotIn(job.name, P.SELF_LOCKED)
        self.assertEqual(P.run_job(job)[1].split(":")[0], "exit timeout")
        self.assertFalse((self.root / "battle-speed-held" / "step" / "child.txt").exists())
        self.assertFalse(self.held())
        self.assertEqual(P.gpu_lock.users, 0)


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
