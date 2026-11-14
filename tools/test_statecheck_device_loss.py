#!/usr/bin/env python3
"""statecheck passes a lost Vulkan device on (xg-au4): a run of the game that
fails having reported it in its own port.log has that line, as it is, in
statecheck's output, where tools/port_tests.py's device_lost finds it.

No GPU, disc or game: the build is a fake executable (a Python script named
xenogears) that run_port really starts through nice, ionice and timeout; it
writes what the port would to its log and exits or aborts as told. Every
statecheck process is bounded and its whole process group killed if it is not
done in time.
"""
from __future__ import annotations

import contextlib
import io
import os
import signal
import subprocess
import sys
from pathlib import Path
from tempfile import TemporaryDirectory
from unittest import TestCase, main

sys.path.insert(0, str(Path(__file__).resolve().parent))
import port_tests  # noqa: E402
import statecheck  # noqa: E402

STATECHECK = Path(__file__).resolve().parent / "statecheck.py"
LIMIT = 120  # seconds for one statecheck process; the fake runs end at once
# A run that aborted, as run_port sees it through timeout (which dies of the same signal).
ABORTED = (-signal.SIGABRT, 128 + signal.SIGABRT)

# The port's check (port/src/gpu/vk.c): "vulkan: CALL failed (%d) at FILE:LINE", then abort().
CALL = "vkWaitForFences(vk.device, 1, &vk.fence, VK_TRUE, UINT64_MAX)"
LINES = {"a": 3001, "b": 3002, "c": 3003, "d": 3004, "r1": 3011, "r2": 3012, "make": 3021, "x": 3031}


def lost_line(run: str, code: int = -4) -> str:
    return f"vulkan: {CALL} failed ({code}) at src/gpu/vk.c:{LINES[run]}"


# The fake game. FAKE_PLAN names what each run (its directory) does, "run=mode,...";
# a run not named exits 0 having written the states it was asked to save.
FAKE = r'''
import os, sys
run = os.path.basename(os.getcwd())
plan = dict(p.split("=", 1) for p in os.environ.get("FAKE_PLAN", "").split(",") if p)
mode = plan.get(run, "ok")
lines = %r
lines = {run: lines.get(run, 3999)}
call = %r
def say(s):
    sys.stderr.write(s + "\n")
    sys.stderr.flush()
print("fake game: booting " + run, flush=True)
if mode == "ok":
    for save in os.environ.get("XG_STATE_SAVE", "").split(","):
        if save:
            open(save.split(":", 1)[1], "wb").close()
    open("audio.pcm", "wb").close()
    sys.exit(0)
if mode == "ok-said-lost":   # the line, yet a run that ends well
    say("vulkan: %%s failed (-4) at src/gpu/vk.c:%%d" %% (call, lines[run]))
    sys.exit(0)
if mode == "lost":
    say("vulkan: %%s failed (-4) at src/gpu/vk.c:%%d" %% (call, lines[run]))
    os.abort()
if mode == "lost-exit":      # reported, then a plain failing exit, no abort
    say("vulkan: %%s failed (-4) at src/gpu/vk.c:%%d" %% (call, lines[run]))
    sys.exit(3)
if mode == "other-code":     # another Vulkan error: not a lost device
    say("vulkan: %%s failed (-3) at src/gpu/vk.c:%%d" %% (call, lines[run]))
    os.abort()
if mode == "bare":           # -4 without the port's line
    say("gpu: error -4 failed (-4)")
    say("  vulkan: %%s failed (-4) at src/gpu/vk.c:%%d (quoted, not at a line's start)" %% (call, lines[run]))
    os.abort()
if mode == "abort":          # a crash with no Vulkan report
    say("xenogears: src/system/main.c:99: main: Assertion `0' failed.")
    os.abort()
if mode == "silent-abort":
    os.abort()
if mode == "fail":
    say("state: Cannot load x.xgs: no such file")
    sys.exit(7)
sys.exit(99)
'''


def fake_build(tmp: Path) -> Path:
    build = tmp / "build"
    build.mkdir()
    exe = build / "xenogears"
    exe.write_text(f"#!{sys.executable}\n" + FAKE % (LINES, CALL))
    exe.chmod(0o755)
    return build


def statecheck_output(*args: str, plan: str) -> tuple[int, str]:
    """statecheck.py's exit and its output, stdout and stderr together as
    port_tests.run_job captures a test's; the process group killed at LIMIT."""
    env = {**os.environ, "FAKE_PLAN": plan}
    p = subprocess.Popen([sys.executable, str(STATECHECK), *args], env=env, stdout=subprocess.PIPE,
                         stderr=subprocess.STDOUT, text=True, start_new_session=True)
    try:
        out, _ = p.communicate(timeout=LIMIT)
    except subprocess.TimeoutExpired:
        os.killpg(p.pid, signal.SIGKILL)
        p.communicate()
        raise
    return p.returncode, out


def run_port_output(build: Path, out: Path, plan: str) -> tuple[int, str]:
    """run_port in this process: its exit and what it wrote to stdout."""
    old = os.environ.get("FAKE_PLAN")
    os.environ["FAKE_PLAN"] = plan
    try:
        buf = io.StringIO()
        with contextlib.redirect_stdout(buf):
            code = statecheck.run_port(build, out, "us1", 100, {})
        return code, buf.getvalue()
    finally:
        if old is None:
            os.environ.pop("FAKE_PLAN", None)
        else:
            os.environ["FAKE_PLAN"] = old


class RunPortTest(TestCase):
    """run_port, the one place statecheck starts the game."""

    def setUp(self):
        self.tmp = TemporaryDirectory()
        self.root = Path(self.tmp.name)
        self.build = fake_build(self.root)

    def tearDown(self):
        self.tmp.cleanup()

    def test_lost_device_forwarded(self):
        code, said = run_port_output(self.build, self.root / "x", "x=lost")
        self.assertIn(code, ABORTED)  # still the failed run's own exit
        log = (self.root / "x" / "port.log").read_text()
        self.assertIn(lost_line("x") + "\n", log)  # the port's line, in the run's own log
        self.assertIn(lost_line("x"), said.splitlines())  # a whole line of its own
        self.assertEqual(lost_line("x"), port_tests.device_lost(said))

    def test_lost_then_plain_exit_forwarded(self):
        code, said = run_port_output(self.build, self.root / "x", "x=lost-exit")
        self.assertEqual(3, code)
        self.assertEqual(lost_line("x"), port_tests.device_lost(said))

    def test_nothing_else_forwarded(self):
        for mode, want in (("other-code", ABORTED), ("bare", ABORTED), ("abort", ABORTED),
                           ("silent-abort", ABORTED), ("fail", (7,))):
            with self.subTest(mode=mode):
                code, said = run_port_output(self.build, self.root / mode, f"{mode}={mode}")
                self.assertIn(code, want)
                self.assertEqual("", said)
                self.assertIsNone(port_tests.device_lost(said))

    def test_success_forwards_nothing(self):
        for mode in ("ok", "ok-said-lost"):
            with self.subTest(mode=mode):
                code, said = run_port_output(self.build, self.root / mode, f"{mode}={mode}")
                self.assertEqual(0, code)
                self.assertEqual("", said)

    def test_stale_log_not_reported(self):
        """A device-lost line from an earlier run in the same directory is not this run's."""
        out = self.root / "x"
        out.mkdir()
        (out / "port.log").write_text(lost_line("x") + "\n")
        code, said = run_port_output(self.build, out, "x=abort")
        self.assertIn(code, ABORTED)
        self.assertEqual("", said)
        code, said = run_port_output(self.build, out, "x=lost")
        self.assertEqual([lost_line("x")], said.splitlines())
        code, said = run_port_output(self.build, out, "x=fail")
        self.assertEqual((7, ""), (code, said))


class WrapperTest(TestCase):
    """statecheck.py's commands, run as port_tests runs them: exit and output."""

    def setUp(self):
        self.tmp = TemporaryDirectory()
        self.root = Path(self.tmp.name)
        self.build = fake_build(self.root)

    def tearDown(self):
        self.tmp.cleanup()

    def run_cmd(self, cmd: str, plan: str, *more: str) -> tuple[int, str]:
        return statecheck_output(cmd, str(self.build), str(self.root / "out"), *more, plan=plan)

    def test_scene_runs_a_c_d(self):
        """A loses the device beside C crashing and D ending well: A's line alone."""
        code, out = self.run_cmd("run", "a=lost,c=abort", "--scene", "field")
        self.assertEqual(1, code)
        self.assertIn("field      FAIL", out)
        self.assertIn("did not write their states", out)
        self.assertEqual([lost_line("a")], [x for x in out.splitlines() if x.startswith("vulkan:")])
        self.assertEqual(lost_line("a"), port_tests.device_lost(out))
        self.assertEqual("statecheck: 1 scene, 0 ok, 1 failed", out.splitlines()[-1])

    def test_scene_run_b(self):
        code, out = self.run_cmd("run", "b=lost", "--scene", "field")
        self.assertEqual(1, code)
        self.assertIn("run B (exit ", out)
        self.assertEqual(lost_line("b"), port_tests.device_lost(out))
        self.assertEqual("statecheck: 1 scene, 0 ok, 1 failed", out.splitlines()[-1])

    def test_rewind(self):
        code, out = self.run_cmd("rewind", "r2=lost", "--scene", "field")
        self.assertEqual(1, code)
        self.assertIn("wrote no state", out)
        self.assertEqual(lost_line("r2"), port_tests.device_lost(out))
        self.assertEqual("rewindcheck: 1 scene, 0 ok, 1 failed", out.splitlines()[-1])

    def test_refusals_first_run(self):
        code, out = self.run_cmd("refusals", "make=lost")
        self.assertEqual(1, code)
        self.assertEqual(lost_line("make"), port_tests.device_lost(out))
        self.assertEqual("refusals: no state to start from", out.splitlines()[-1])

    def test_ordinary_failures_unchanged(self):
        """Without the port's line the output is what it always was: no Vulkan line in it."""
        for plan in ("a=abort", "a=other-code", "a=bare,c=silent-abort", "a=fail"):
            with self.subTest(plan=plan):
                code, out = self.run_cmd("run", plan, "--scene", "field")
                self.assertEqual(1, code)
                self.assertEqual(["field      FAIL", "           runs A, C, D (exit ", "statecheck: 1 scene, 0 ok, 1 failed"],
                                 [x[:len("           runs A, C, D (exit ")] if "runs A" in x else x
                                  for x in out.splitlines()])
                self.assertIsNone(port_tests.device_lost(out))
        code, out = self.run_cmd("refusals", "make=other-code")
        self.assertEqual((1, ["refusals: no state to start from"]), (code, out.splitlines()))

    def test_same_out_rerun_not_stale(self):
        """A second statecheck in the same OUT whose runs fail otherwise reports no lost device."""
        code, out = self.run_cmd("run", "a=lost", "--scene", "field")
        self.assertEqual(lost_line("a"), port_tests.device_lost(out))
        code, out = self.run_cmd("run", "a=abort", "--scene", "field")
        self.assertEqual(1, code)
        self.assertIsNone(port_tests.device_lost(out))


if __name__ == "__main__":
    main()
