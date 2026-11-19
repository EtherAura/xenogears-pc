#!/usr/bin/env python3
"""test_battle_speed passes a lost Vulkan device on (xg-au4): a run of the game
that fails having reported it in its own port.log has that line, as it is, in
test_battle_speed's output, where tools/port_tests.py's device_lost finds it.

No GPU, disc or game: the build is a fake executable (a Python script named
xenogears) that run really starts through nice and timeout, under the GPU lock
(a lock file of the test's own, XG_GPU_LOCK, as tools/test_port_tests.py's
battle-speed lock tests use); it writes what the port would to its log and
exits or aborts as told. main() runs in a child process of its own (the disc
image an empty stand-in), bounded and its whole process group killed if it is
not done in time.
"""
from __future__ import annotations

import contextlib
import io
import os
import signal
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
from tempfile import TemporaryDirectory
from unittest import TestCase, main
from unittest.mock import patch

TOOLS = Path(__file__).resolve().parent
sys.path.insert(0, str(TOOLS))
import port_tests  # noqa: E402
import test_battle_speed as B  # noqa: E402

LIMIT = 120  # seconds for one wrapper process or run; the fake runs end at once
# A run that aborted, as run sees it through timeout (which dies of the same signal).
ABORTED = (-signal.SIGABRT, 128 + signal.SIGABRT)
STEP = ("off", "1x", "2x", "4x")

# The port's check (port/src/gpu/vk.c): "vulkan: CALL failed (%d) at FILE:LINE", then abort().
CALL = "vkQueueSubmit(vk.queue, 1, &submit, vk.fence)"
LINES = {"off": 3001, "1x": 3002, "2x": 3003, "4x": 3004, "baseline": 3005, "4x-real": 3011, "x": 3031}


def lost_line(run: str, code: int = -4) -> str:
    return f"vulkan: {CALL} failed ({code}) at src/gpu/vk.c:{LINES[run]}"


# The fake game. FAKE_PLAN names what each run (its directory) does, "run=mode,...";
# a run not named ends well having written what main() reads of a run.
FAKE = r'''
import os, sys
run = os.path.basename(os.getcwd())
plan = dict(p.split("=", 1) for p in os.environ.get("FAKE_PLAN", "").split(",") if p)
mode = plan.get(run, "ok")
line = "vulkan: %%s failed (%%d) at src/gpu/vk.c:%%d" %% (%r, -4, %r.get(run, 3999))
def say(s):
    sys.stderr.write(s + "\n")
    sys.stderr.flush()
print("fake game: booting " + run, flush=True)
if mode in ("ok", "ok-said-lost"):
    for name in ("XG_SWEEP_LOG", "XG_INVARIANT_LOG", "XG_AUDIO_DUMP"):
        with open(os.environ[name], "w") as f:
            f.write("B 1\n" if name == "XG_SWEEP_LOG" else "")
    print("spu 1.000: key on 0001 off", flush=True)  # one music key-on: main's music check has an end
    if mode == "ok-said-lost":  # the line, yet a run that ends well
        say(line)
    sys.exit(0)
if mode == "lost":
    say(line)
    os.abort()
if mode == "lost-exit":      # reported, then a plain failing exit, no abort
    say(line)
    sys.exit(3)
if mode == "other-code":     # another Vulkan error: not a lost device
    say(line.replace("(-4)", "(-3)"))
    os.abort()
if mode == "bare":           # -4 without the port's line
    say("gpu: error -4 failed (-4)")
    say("  " + line + " (quoted, not at a line's start)")
    os.abort()
if mode == "abort":          # a crash with no Vulkan report
    say("xenogears: src/system/main.c:99: main: Assertion `0' failed.")
    os.abort()
if mode == "silent-abort":
    os.abort()
if mode == "fail":
    say("battle: no such scene")
    sys.exit(7)
sys.exit(99)
'''

# main() in a process of its own, the disc image the empty stand-in argv[1].
DRIVER = r'''
import sys
sys.path.insert(0, %r)
import statecheck, test_battle_speed as B
statecheck.DISCS = {k: sys.argv[1] for k in statecheck.DISCS}
sys.argv = ["test_battle_speed.py", *sys.argv[2:]]
sys.exit(B.main())
'''


def fake_build(tmp: Path) -> Path:
    build = tmp / "build"
    build.mkdir()
    exe = build / "xenogears"
    exe.write_text(f"#!{sys.executable}\n" + FAKE % (CALL, LINES))
    exe.chmod(0o755)
    return build


def vulkan_lines(out: str) -> list[str]:
    return [x for x in out.splitlines() if "vulkan" in x]


class Base(TestCase):
    def setUp(self):
        self.tmp = TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        self.build = fake_build(self.root)
        p = patch.dict(os.environ, XG_GPU_LOCK=str(self.root / "gpu.lock"))
        p.start()
        self.addCleanup(p.stop)

    def run_output(self, plan: str, out: Path, speed: int | None = 4, real: bool = False) -> tuple[int, str]:
        """run in this process: its exit and what it wrote to stdout."""
        with patch.dict(os.environ, FAKE_PLAN=plan):
            buf = io.StringIO()
            with contextlib.redirect_stdout(buf), ThreadPoolExecutor(max_workers=1) as pool:
                code = pool.submit(B.run, self.build, out, speed, 10, False, real=real).result(timeout=LIMIT)
        return code, buf.getvalue()

    def parallel_output(self, plan: str, names=STEP) -> tuple[list[int], str]:
        """main()'s step-clock runs side by side in this process: exits and stdout."""
        with patch.dict(os.environ, FAKE_PLAN=plan):
            buf = io.StringIO()
            with contextlib.redirect_stdout(buf), ThreadPoolExecutor(max_workers=len(names)) as pool:
                futures = [pool.submit(B.run, self.build, self.root / "runs" / n, None, 10, False) for n in names]
                codes = [f.result(timeout=LIMIT) for f in futures]
        return codes, buf.getvalue()

    def wrapper_output(self, plan: str, *more: str) -> tuple[int, str]:
        """test_battle_speed's main(): exit and output, stdout and stderr together as
        port_tests.run_job captures a test's; the process group killed at LIMIT."""
        disc = self.root / "disc.cue"
        disc.touch()
        env = {**os.environ, "FAKE_PLAN": plan}
        p = subprocess.Popen([sys.executable, "-c", DRIVER % str(TOOLS), str(disc), str(self.build),
                              str(self.root / "out"), "--until", "10", *more], env=env, stdout=subprocess.PIPE,
                             stderr=subprocess.STDOUT, text=True, start_new_session=True)
        try:
            out, _ = p.communicate(timeout=LIMIT)
        except subprocess.TimeoutExpired:
            os.killpg(p.pid, signal.SIGKILL)
            p.communicate()
            raise
        return p.returncode, out


class RunTest(Base):
    """run, the one place test_battle_speed starts the game, on either clock."""

    def test_lost_device_forwarded(self):
        for real in (False, True):
            with self.subTest(real=real):
                out = self.root / "x"
                code, said = self.run_output("x=lost", out, real=real)
                self.assertIn(code, ABORTED)  # still the failed run's own exit
                self.assertIn(lost_line("x") + "\n", (out / "port.log").read_text())  # in the run's own log
                self.assertEqual([lost_line("x")], said.splitlines())  # a whole line of its own
                self.assertEqual(lost_line("x"), port_tests.device_lost(said))

    def test_lost_then_plain_exit_forwarded(self):
        for real in (False, True):
            with self.subTest(real=real):
                code, said = self.run_output("x=lost-exit", self.root / "x", real=real)
                self.assertEqual(3, code)
                self.assertEqual(lost_line("x"), port_tests.device_lost(said))

    def test_nothing_else_forwarded(self):
        for real in (False, True):
            for mode, want in (("other-code", ABORTED), ("bare", ABORTED), ("abort", ABORTED),
                               ("silent-abort", ABORTED), ("fail", (7,))):
                with self.subTest(real=real, mode=mode):
                    code, said = self.run_output(f"x={mode}", self.root / "x", real=real)
                    self.assertIn(code, want)
                    self.assertEqual("", said)
                    self.assertIsNone(port_tests.device_lost(said))

    def test_success_forwards_nothing(self):
        for real in (False, True):
            for mode in ("ok", "ok-said-lost"):
                with self.subTest(real=real, mode=mode):
                    code, said = self.run_output(f"x={mode}", self.root / "x", real=real)
                    self.assertEqual((0, ""), (code, said))

    def test_stale_log_not_reported(self):
        """A device-lost line from an earlier run in the same directory is not this run's."""
        out = self.root / "x"
        out.mkdir()
        (out / "port.log").write_text(lost_line("x") + "\n")
        code, said = self.run_output("x=abort", out)
        self.assertIn(code, ABORTED)
        self.assertEqual("", said)
        code, said = self.run_output("x=lost", out)
        self.assertEqual([lost_line("x")], said.splitlines())
        code, said = self.run_output("x=fail", out, real=True)
        self.assertEqual((7, ""), (code, said))

    def test_lock_failure_unchanged(self):
        """A GPU lock that cannot be taken: the game never ran, nothing is forwarded."""
        with patch.dict(os.environ, XG_GPU_LOCK=str(self.root / "no such dir" / "gpu.lock")):
            out = self.root / "x"
            code, said = self.run_output("x=lost", out)
        self.assertEqual((1, ""), (code, said))
        self.assertIn("GPU lock", (out / "port.log").read_text())


class FailedRunKeptTest(Base):
    """run keeps a failed run's status and the end of its port.log in failed.N.log (xg-oevj),
    which the next run in the same directory (a retry's) leaves alone. The game is a mocked
    subprocess.run: it writes the given output to the log run hands it and ends as told."""

    def mocked_run(self, out: Path, ends: list[tuple[int, str]], speed: int | None = 4,
                   real: bool = False) -> list[tuple[int, str]]:
        """run once per (exit, output) in ends, all in out: each run's exit and stdout."""
        results = []
        for code, text in ends:
            def game(argv, **kw):
                kw["stdout"].write(text)
                return subprocess.CompletedProcess(argv, code)
            with patch.object(B.subprocess, "run", game):
                buf = io.StringIO()
                with contextlib.redirect_stdout(buf):
                    results.append((B.run(self.build, out, speed, 10, False, real=real), buf.getvalue()))
        return results

    def kept(self, out: Path) -> list[str]:
        return [(out / f"failed.{n}.log").read_text() for n in range(1, 10) if (out / f"failed.{n}.log").exists()]

    def test_preserves_abnormal_exit_log_after_retry_succeeds(self):
        for real in (False, True):
            with self.subTest(real=real):
                out = self.root / ("4x-real" if real else "4x")
                crash = "fake game: booting\nxenogears: src/system/main.c:99: main: Assertion `0' failed.\n"
                results = self.mocked_run(out, [(-signal.SIGABRT, crash), (0, "fake game: a run that ends well\n")],
                                          real=real)
                self.assertEqual([(-signal.SIGABRT, ""), (0, "")], results)  # exits as they were, stdout quiet
                self.assertNotIn("Assertion", (out / "port.log").read_text())  # the retry's own log
                [kept] = self.kept(out)
                self.assertIn(f"run: {out.name} ({out}): battle speed 4, {'real' if real else 'step'} clock", kept)
                self.assertIn("status: exit -6 (killed by SIGABRT)\n", kept)
                self.assertTrue(kept.endswith(crash))

    def test_preserves_abnormal_exit_log_every_failed_attempt(self):
        out = self.root / "off"
        results = self.mocked_run(out, [(128 + signal.SIGABRT, "first\n"), (124, "second"), (7, "third\n")], speed=None)
        self.assertEqual([134, 124, 7], [c for c, _ in results])
        first, second, third = self.kept(out)
        self.assertIn("battle speed off, step clock", first)
        self.assertIn("status: exit 134 (128 + SIGABRT: the game died of that signal)\n", first)
        self.assertTrue(first.endswith("all 6 bytes):\nfirst\n"))
        self.assertIn("status: exit 124 (timeout", second)
        self.assertTrue(second.endswith("second\n"))
        self.assertIn("status: exit 7\n", third)

    def test_preserves_abnormal_exit_log_bounded(self):
        out = self.root / "2x"
        text = "".join(f"spu {i}.000: key on 0001 off\n" for i in range(5000)) + "abort: the last line\n"
        [(code, said)] = self.mocked_run(out, [(-signal.SIGABRT, text)])
        self.assertEqual((-signal.SIGABRT, ""), (code, said))
        [kept] = self.kept(out)
        self.assertIn(f"the last {B.FAILED_TAIL} of its {len(text)} bytes", kept)
        self.assertTrue(kept.endswith(text[-B.FAILED_TAIL:]))
        self.assertLess(len(kept), B.FAILED_TAIL + 400)

    def test_preserves_abnormal_exit_log_lost_device_still_forwarded(self):
        out = self.root / "x"
        [(code, said)] = self.mocked_run(out, [(-signal.SIGABRT, lost_line("x") + "\n")])
        self.assertEqual(-signal.SIGABRT, code)
        self.assertEqual(lost_line("x"), port_tests.device_lost(said))
        [kept] = self.kept(out)
        self.assertIn(lost_line("x"), kept)

    def test_preserves_abnormal_exit_log_not_for_success_or_lock(self):
        out = self.root / "1x"
        self.assertEqual([(0, "")], self.mocked_run(out, [(0, "fine\n")]))
        self.assertEqual([], self.kept(out))
        with patch.dict(os.environ, XG_GPU_LOCK=str(self.root / "no such dir" / "gpu.lock")):
            self.assertEqual(1, self.mocked_run(out, [(-signal.SIGABRT, "never ran\n")])[0][0])
        self.assertEqual([], self.kept(out))  # the game never ran: port.log says why, as before

    def test_preserves_abnormal_exit_log_real_game_process(self):
        """The real launch (nice, timeout, the fake game aborting), then a rerun that ends well."""
        out = self.root / "x"
        code, _ = self.run_output("x=abort", out)
        self.assertIn(code, ABORTED)
        self.assertEqual((0, ""), self.run_output("x=ok", out))
        [kept] = self.kept(out)
        self.assertRegex(kept, r"status: exit (-6 \(killed by SIGABRT\)|134 \(128 \+ SIGABRT)")
        self.assertIn("main: Assertion `0' failed.", kept)


class ParallelTest(Base):
    """main()'s four step-clock runs side by side: each run's line once, whole, its own."""

    def test_every_run_lost(self):
        for _ in range(5):
            codes, said = self.parallel_output(",".join(f"{n}=lost" for n in STEP))
            for c in codes:
                self.assertIn(c, ABORTED)
            self.assertEqual(sorted(lost_line(n) for n in STEP), sorted(said.splitlines()))
            for line in said.splitlines():
                self.assertEqual(line, port_tests.device_lost(line + "\n"))

    def test_only_the_lost_runs_line(self):
        codes, said = self.parallel_output("2x=lost,off=abort,4x=other-code")
        self.assertIn(codes[2], ABORTED)
        self.assertEqual([lost_line("2x")], said.splitlines())
        self.assertEqual(lost_line("2x"), port_tests.device_lost(said))

    def test_none_lost(self):
        codes, said = self.parallel_output("off=abort,1x=bare,2x=fail,4x=ok-said-lost")
        self.assertEqual(7, codes[2])
        self.assertEqual(0, codes[3])
        self.assertEqual("", said)


class WrapperTest(Base):
    """test_battle_speed.py's main(), run as port_tests runs it: exit and output."""

    def test_step_run_lost(self):
        code, out = self.wrapper_output("4x=lost,off=abort")
        self.assertEqual(1, code)
        self.assertEqual([lost_line("4x")], vulkan_lines(out))
        self.assertEqual(lost_line("4x"), port_tests.device_lost(out))
        self.assertRegex(out, r"(?m)^  FAIL 4x: the run ends at vblank 10 \(exit -?\d+\)$")
        self.assertRegex(out, r"(?m)^  FAIL off: the run ends at vblank 10 \(exit -?\d+\)$")
        self.assertEqual("battle-speed: 2 failed", out.splitlines()[-1])

    def test_baseline_run_lost(self):
        code, out = self.wrapper_output("baseline=lost", "--baseline", str(self.build), "--no-real")
        self.assertEqual(1, code)
        self.assertEqual(lost_line("baseline"), port_tests.device_lost(out))
        self.assertEqual("battle-speed: 1 failed", out.splitlines()[-1])

    def test_real_run_lost(self):
        code, out = self.wrapper_output("4x-real=lost")
        self.assertEqual(1, code)
        self.assertEqual([lost_line("4x-real")], vulkan_lines(out))
        self.assertEqual(lost_line("4x-real"), port_tests.device_lost(out))
        self.assertRegex(out, r"(?m)^  FAIL 4x real clock: the run ends \(exit -?\d+\)$")
        self.assertRegex(out, r"(?m)^battle-speed: \d+ failed, \d+ ok$")

    def test_ordinary_failures_unchanged(self):
        """Without the port's line the output is what it always was: no Vulkan line in it."""
        for plan in ("off=abort", "1x=other-code", "2x=bare,4x=silent-abort", "4x=fail"):
            with self.subTest(plan=plan):
                code, out = self.wrapper_output(plan)
                self.assertEqual(1, code)
                lines = out.splitlines()
                self.assertEqual(4 + 1, len(lines))
                self.assertTrue(all(x.startswith(("  ok   ", "  FAIL ")) for x in lines[:4]))
                self.assertRegex(lines[-1], r"^battle-speed: \d failed$")
                self.assertIsNone(port_tests.device_lost(out))
        for plan in ("4x-real=abort", "4x-real=other-code", "4x-real=ok-said-lost"):
            with self.subTest(plan=plan):
                code, out = self.wrapper_output(plan)
                self.assertEqual(1, code)  # the fake runs fail main's battle checks
                self.assertEqual([], [x for x in out.splitlines() if x.startswith("vulkan:")])
                self.assertIsNone(port_tests.device_lost(out))

    def test_same_out_rerun_not_stale(self):
        """A second run of the wrapper in the same OUT whose runs fail otherwise reports no lost device."""
        code, out = self.wrapper_output("2x=lost")
        self.assertEqual(lost_line("2x"), port_tests.device_lost(out))
        code, out = self.wrapper_output("2x=abort")
        self.assertEqual(1, code)
        self.assertIsNone(port_tests.device_lost(out))


if __name__ == "__main__":
    main()
