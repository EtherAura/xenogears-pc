"""The EXTRAS runs' device-loss diagnostics end to end (xg-au4), with a fake game.

tools/test_port_tests.py's DeviceLossTests replace the plan and subprocess.run.
Here Runner.plan() builds the real EXTRAS jobs (game_executable, --extras, the
first disc, XG_EXTRAS_SCRIPT in the test's own scratch, the expected pattern)
and run_job() launches them and reads their logs, against a temporary build
whose `xenogears` is a CPU-only Python script. ROOT (so bin/ and the disc
image: an empty placeholder), OUT and XG_GPU_LOCK all point into a temporary
directory; nothing in the repository's bin/, disc/ or build/, and no shared
GPU lock, is touched. The fake loses the "device" by printing the port's line
(port/src/gpu/vk.c's VK_CHECK) and aborting, core dumps off.
"""

from __future__ import annotations

from contextlib import redirect_stdout
import io
import json
import os
from pathlib import Path
import re
import stat
import sys
import tempfile
import unittest
from unittest.mock import patch

import port_tests as P

GALLERY = "extras-visuals-gallery"
GAMEPAD = "extras-gallery-gamepad"
PARTNERS = ("gallery_battletest", "gallery_mediatest", "gallery_scenetest")
LOST = "vulkan: vkQueueSubmit(gpu.queue, 1, &submit, frame->fence) failed (-4) at port/src/gpu/vk.c:1871"
# Output satisfying each EXTRAS entry's pattern (checked against P.EXTRAS in setUp).
OK_TEXT = {
    GALLERY: "extras: Gallery: 12 movies, 3 places the game plays them, 40 scenes",
    GAMEPAD: "nav open: list ##extras/##gmovies\nnav left: list ##extras/##gmovies\n"
             "nav r1: list ##extras/##gpics\nnav l1l1: list ##extras/##gscenes\nnav back: list ##extras/##sections",
}
WAIT = 20      # the fake children's own bound on a handshake (seconds)
TIMEOUT = 60   # each planned job's, instead of the plan's TIMEOUT

# The fake game (and the fake CPU tests beside it). Which test it is comes from
# XG_EXTRAS_SCRIPT's directory (each EXTRAS run's own scratch) or its own name;
# what it does on its Nth run, from FAKE_XG_STATE/config.json. Every run is
# recorded in FAKE_XG_STATE, outside the runner's OUT.
FAKE = r'''
import fcntl, json, os, resource, sys, time
from pathlib import Path

state = Path(os.environ["FAKE_XG_STATE"])
me = Path(sys.argv[0]).name
script = os.environ.get("XG_EXTRAS_SCRIPT")
name = Path(script).parent.name if me == "xenogears" else me
runs = json.loads((state / "config.json").read_text())[name]
calls = state / f"{name}.calls"
with calls.open("a") as f:
    lock = os.environ.get("XG_GPU_LOCK")
    held = None
    if me == "xenogears" and lock:
        fd = os.open(lock, os.O_RDONLY)
        try:
            fcntl.flock(fd, fcntl.LOCK_EX | fcntl.LOCK_NB)
            held = False
        except BlockingIOError:
            held = True
        finally:
            os.close(fd)
    f.write(json.dumps({"argv": sys.argv[1:], "script": script,
                        "text": Path(script).read_text() if script else None,
                        "clock": os.environ.get("XG_CLOCK"), "headless": os.environ.get("XG_HEADLESS"),
                        "lock": lock, "held": held, "cwd": os.getcwd()}) + "\n")
n = len(calls.read_text().splitlines())
action = runs[min(n, len(runs)) - 1]
ok, lost = os.environ["FAKE_XG_OK"], os.environ["FAKE_XG_LOST"]


def wait_for(pattern):
    """Bounded: the other side of the handshake started."""
    end = time.monotonic() + int(os.environ["FAKE_XG_WAIT"])
    while not any(state.glob(pattern)):
        if time.monotonic() > end:
            print(f"fake: nothing matching {pattern} started", flush=True)
            sys.exit(3)
        time.sleep(0.02)


def abort(*lines):
    sys.stdout.flush()
    for x in lines:
        print(x, file=sys.stderr)
    sys.stderr.flush()
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
    os.abort()


if action.startswith("shared-"):  # run only once a partner is running
    (state / f"{name}.started").touch()
    wait_for("gallery_*.started")
    action = action[len("shared-"):]
print(f"extras: {name} run {n}", flush=True)
if action == "ok":
    print(json.loads(ok)[name] if me == "xenogears" else f"{name} ok")
elif action == "partner":
    (state / f"{name}.started").touch()
    wait_for("extras-*.started")
    print(f"{name} ok")
elif action == "lost":
    abort(lost, "xenogears: aborting")
elif action == "lost-matching":  # the expected output, then the device lost
    print(json.loads(ok)[name], flush=True)
    abort(lost, "xenogears: aborting")
elif action == "abort":
    abort("xenogears: gpu.c:120: gpuFlush: Assertion `frame' failed.")
elif action == "bare-4":
    abort("vkQueueSubmit failed (-4)", "error -4", "VK_ERROR_DEVICE_LOST")
elif action == "other-vulkan":
    abort(lost.replace("(-4)", "(-3)"), "xenogears: aborting")
elif action == "fail":
    print("extras: section gallery did not open")
    sys.exit(1)
elif action == "matching-fail":  # the expected output, then a nonzero exit
    print(json.loads(ok)[name])
    sys.exit(1)
elif action == "nomatch":
    print("extras: Gallery: 0 movies, 0 places the game plays them, 0 scenes")
else:
    sys.exit(f"fake: unknown action {action}")
'''


class ExtrasDeviceLossTests(unittest.TestCase):
    def setUp(self):
        tmp = tempfile.TemporaryDirectory()
        self.addCleanup(tmp.cleanup)
        self.root = Path(tmp.name).resolve()
        self.out, self.state, self.build = self.root / "out", self.root / "state", self.root / "build"
        for d in (self.out, self.state, self.build):
            d.mkdir()
        (self.build / "CMakeCache.txt").write_text("XG_VERSION:STRING=US\nXG_EXTRAS:BOOL=ON\n")
        for name in ("xenogears", *PARTNERS):
            exe = self.build / name
            exe.write_text(f"#!{sys.executable}\n{FAKE}")
            exe.chmod(exe.stat().st_mode | stat.S_IXUSR)
        self.addCleanup(patch.stopall)
        patch.object(P, "ROOT", self.root).start()
        patch.object(P, "OUT", self.out).start()
        self.lock = self.root / "gpu.lock"
        patch.dict(os.environ, XG_GPU_LOCK=str(self.lock), FAKE_XG_STATE=str(self.state),
                   FAKE_XG_OK=json.dumps(OK_TEXT), FAKE_XG_LOST=LOST, FAKE_XG_WAIT=str(WAIT)).start()
        self.disc = P.disc("USA", 1)
        self.assertTrue(self.disc.is_relative_to(self.root))
        self.disc.parent.mkdir(parents=True)
        self.disc.touch()  # an empty placeholder: the fake never reads it
        for name, text in OK_TEXT.items():
            self.assertIsNotNone(re.search(P.EXTRAS[name][1], text), name)

    def suite(self, pattern: str, jobs: int, runs: dict[str, list[str]]):
        """Plan and run the real suite over `pattern`; its runner (results, trace)."""
        (self.state / "config.json").write_text(json.dumps(runs))
        for x in self.state.glob("*.started"):
            x.unlink()
        r = P.Runner(self.build, gpu=True, pattern=pattern, listing=False, jobs=jobs)
        with redirect_stdout(io.StringIO()):
            r.plan()
            for t in r.tests:
                if t.job is not None:
                    t.job.timeout = TIMEOUT
            r.execute()
        return r

    def result(self, r, name: str) -> tuple[str, str]:
        return {n: (s, d) for n, s, d in r.results}[name]

    def calls(self, name: str) -> list[dict]:
        path = self.state / f"{name}.calls"
        return [json.loads(x) for x in path.read_text().splitlines()] if path.exists() else []

    def log(self, name: str, suffix: str = ".log") -> str:
        return (self.out / f"{name}{suffix}").read_text()

    def shown(self, name: str, suffix: str = ".log") -> str:
        return str(P.display_path(self.out / f"{name}{suffix}"))

    def assert_launched(self, name: str, runs: int):
        """Each run got the plan's --extras command line, its own script and the test's GPU lock."""
        calls = self.calls(name)
        self.assertEqual(len(calls), runs)
        script = self.out / "scratch" / name / "extras.txt"
        for c in calls:
            self.assertEqual(c["argv"], ["--extras", str(self.disc)])
            self.assertEqual(c["script"], str(script))
            self.assertEqual(c["text"], P.EXTRAS[name][0])
            self.assertEqual((c["clock"], c["headless"]), ("step", "1"))
            self.assertEqual(c["lock"], str(self.lock))
            self.assertTrue(c["held"], "the runner held the (temporary) GPU lock around the game")
            self.assertEqual(c["cwd"], str(self.out))

    def assert_not_kept_alone(self):
        self.assertFalse((self.out / P.ALONE_FILE).exists())

    # One test, one run: --jobs 1 and 2 report alike.

    def test_plan_launches_the_extras_job(self):
        r = self.suite(GALLERY, 1, {GALLERY: ["ok"]})
        self.assertEqual([t.name for t in r.tests], [GALLERY])
        job = r.tests[0].job
        self.assertEqual(job.argv, [str(self.build / "xenogears"), "--extras", str(self.disc)])
        self.assertEqual(job.expect, P.EXTRAS[GALLERY][1])
        self.assertTrue(job.gpu)
        self.assert_launched(GALLERY, 1)

    def test_success_is_ok_with_the_matched_output(self):
        for jobs in (1, 2):
            with self.subTest(jobs=jobs):
                (self.state / f"{GALLERY}.calls").unlink(missing_ok=True)
                r = self.suite(GALLERY, jobs, {GALLERY: ["ok"]})
                status, detail = self.result(r, GALLERY)
                self.assertEqual(status, "ok", detail)
                self.assertTrue(detail.startswith(OK_TEXT[GALLERY] + " ("), detail)
                self.assertNotIn(P.DEVICE_LOST_MARK, detail)
                self.assertEqual(r.tests[0].job.device_lost, [])
                self.assert_launched(GALLERY, 1)

    def test_device_loss_fails_marked_with_its_log(self):
        for jobs in (1, 2):
            with self.subTest(jobs=jobs):
                (self.state / f"{GALLERY}.calls").unlink(missing_ok=True)
                r = self.suite(GALLERY, jobs, {GALLERY: ["lost"]})
                status, detail = self.result(r, GALLERY)
                self.assertEqual(status, "FAIL")
                self.assertEqual(detail, f"{P.DEVICE_LOST_MARK} ({LOST[:90]}): exit -6: xenogears: aborting "
                                         f"(log {self.shown(GALLERY)})")
                self.assertEqual(r.tests[0].job.device_lost, [LOST])
                text = self.log(GALLERY)
                self.assertIn(f"{LOST}\nxenogears: aborting\n", text)
                self.assertIn(f"extras: {GALLERY} run 1", text)
                self.assertEqual([e for e, _, _ in r.trace], [] if jobs == 1 else ["start"])  # no rerun alone
                self.assert_launched(GALLERY, 1)
                self.assert_not_kept_alone()

    def test_matching_output_does_not_excuse_a_failed_exit(self):
        r = self.suite(GALLERY, 1, {GALLERY: ["lost-matching"]})
        status, detail = self.result(r, GALLERY)
        self.assertEqual(status, "FAIL")
        self.assertTrue(detail.startswith(f"{P.DEVICE_LOST_MARK} ({LOST[:90]}): exit -6: "), detail)
        self.assertIn(OK_TEXT[GALLERY], self.log(GALLERY))
        (self.state / f"{GALLERY}.calls").unlink()
        r = self.suite(GALLERY, 1, {GALLERY: ["matching-fail"]})
        self.assertEqual(self.result(r, GALLERY),
                         ("FAIL", f"exit 1: {OK_TEXT[GALLERY]} (log {self.shown(GALLERY)})"))

    def test_a_later_passing_run_in_the_same_output_shows_no_stale_loss(self):
        r = self.suite(GALLERY, 1, {GALLERY: ["lost"]})
        self.assertEqual(self.result(r, GALLERY)[0], "FAIL")
        self.assertIn(LOST, self.log(GALLERY))
        r = self.suite(GALLERY, 1, {GALLERY: ["lost", "ok"]})  # the second run of the fake passes
        status, detail = self.result(r, GALLERY)
        self.assertEqual(status, "ok", detail)
        self.assertNotIn(P.DEVICE_LOST_MARK, detail)
        self.assertEqual(r.tests[0].job.device_lost, [])
        self.assertNotIn("vulkan:", self.log(GALLERY))
        self.assertIn(f"extras: {GALLERY} run 2", self.log(GALLERY))
        self.assert_launched(GALLERY, 2)
        self.assert_not_kept_alone()

    def test_other_failures_are_not_device_loss(self):
        cases = {
            "abort": "xenogears: gpu.c:120: gpuFlush: Assertion `frame' failed.",
            "bare-4": "VK_ERROR_DEVICE_LOST",
            "other-vulkan": "xenogears: aborting",
        }
        for action, last in cases.items():
            with self.subTest(action=action):
                (self.state / f"{GALLERY}.calls").unlink(missing_ok=True)
                r = self.suite(GALLERY, 1, {GALLERY: [action]})
                self.assertEqual(self.result(r, GALLERY), ("FAIL", f"exit -6: {last} (log {self.shown(GALLERY)})"))
                self.assertEqual(r.tests[0].job.device_lost, [])
        r = self.suite(GALLERY, 1, {GALLERY: ["fail"]})
        self.assertEqual(self.result(r, GALLERY),
                         ("FAIL", f"exit 1: extras: section gallery did not open (log {self.shown(GALLERY)})"))
        self.assert_not_kept_alone()

    def test_missing_expected_output_still_fails(self):
        r = self.suite(GALLERY, 1, {GALLERY: ["nomatch"]})
        self.assertEqual(self.result(r, GALLERY),
                         ("FAIL", "exit no match: extras: Gallery: 0 movies, 0 places the game plays them, 0 scenes "
                                  f"(log {self.shown(GALLERY)})"))

    # Shared with CPU tests (--jobs 4, pattern 'gallery'): the extras' first run
    # starts with nothing running, the three CPU tests beside it; the fake only
    # goes on once one of them has started, and they once it has.

    def shared(self, runs: list[str]):
        config = {GALLERY: runs, GAMEPAD: ["ok"], **{p: ["partner"] for p in PARTNERS}}
        r = self.suite("gallery", 4, config)
        self.assertEqual([t.name for t in r.tests], [*PARTNERS, GALLERY, GAMEPAD])
        self.assertEqual(r.trace[0], ("start", GALLERY, frozenset()))
        self.assertEqual({(e, n) for e, n, _ in r.trace[1:4]}, {("start", p) for p in PARTNERS})
        self.assertIn(("retry", GALLERY, frozenset()), r.trace)
        for p in PARTNERS:
            self.assertEqual(self.result(r, p)[0], "ok", self.result(r, p))
        self.assertEqual(self.result(r, GAMEPAD)[0], "ok", self.result(r, GAMEPAD))
        self.assert_launched(GALLERY, 2)
        self.assert_launched(GAMEPAD, 1)
        self.assertNotEqual(self.calls(GALLERY)[0]["script"], self.calls(GAMEPAD)[0]["script"])
        self.assertIn(LOST if "lost" in runs[0] else "did not open", self.log(GALLERY, ".shared.log"))
        return r

    def test_lost_when_shared_then_passing_alone_is_ok_disclosed_and_not_kept_alone(self):
        r = self.shared(["shared-lost", "ok"])
        status, detail = self.result(r, GALLERY)
        self.assertEqual(status, "ok", detail)
        self.assertTrue(detail.startswith(f"{P.DEVICE_LOST_MARK} WHEN SHARED, passed alone; not kept alone"), detail)
        self.assertIn(f"shared {P.DEVICE_LOST_MARK} ({LOST[:90]}): exit -6", detail)
        self.assertIn(f"(log {self.shown(GALLERY, '.shared.log')})", detail)
        self.assertIn(f"alone {OK_TEXT[GALLERY]} (", detail)
        self.assertNotIn("vulkan:", self.log(GALLERY))
        self.assert_not_kept_alone()
        self.assertNotIn(GALLERY, r.alone_only)

    def test_lost_on_every_run_fails_marked(self):
        r = self.shared(["shared-lost", "lost"])
        status, detail = self.result(r, GALLERY)
        self.assertEqual(status, "FAIL")
        self.assertTrue(detail.startswith(f"{P.DEVICE_LOST_MARK} ({LOST[:90]}): exit -6: "), detail)
        self.assertIn(f"; shared too: {P.DEVICE_LOST_MARK} ({LOST[:90]})", detail)
        self.assertIn(LOST, self.log(GALLERY))
        self.assertEqual(r.tests[3].job.device_lost, [LOST, LOST])
        self.assert_not_kept_alone()

    def test_ordinary_failure_only_when_shared_is_still_kept_alone(self):
        r = self.shared(["shared-fail", "ok"])
        status, detail = self.result(r, GALLERY)
        self.assertEqual(status, "ok", detail)
        self.assertTrue(detail.startswith("FAILED ONLY WHEN SHARED, alone from now on"), detail)
        self.assertNotIn(P.DEVICE_LOST_MARK, detail)
        self.assertEqual((self.out / P.ALONE_FILE).read_text(), GALLERY + "\n")


if __name__ == "__main__":
    unittest.main()
