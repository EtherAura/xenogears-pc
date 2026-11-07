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


if __name__ == "__main__":
    unittest.main()
