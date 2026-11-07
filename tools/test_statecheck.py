#!/usr/bin/env python3
"""A process that crashes after writing valid captures must fail equivalence."""
from pathlib import Path
from tempfile import TemporaryDirectory
from unittest import TestCase, main
from unittest.mock import patch
import statecheck


class ExitStatusTest(TestCase):
    def run_case(self, rewind, failed):
        def fake_run(build, out, disc, quit_at, env):
            out.mkdir(parents=True, exist_ok=True)
            (out / "audio.pcm").write_bytes(b"")
            (out / "port.log").write_text("rewind: from vblank 701 back to 579\n")
            for save in env.get("XG_STATE_SAVE", "").split(","):
                Path(save.split(":", 1)[1]).touch()
            return 134 if out.name == failed else 0

        with TemporaryDirectory() as tmp, \
                patch.object(statecheck, "run_port", side_effect=fake_run), \
                patch.object(statecheck, "read_state", return_value=({"vblank": 20, "dumpFrames": 0}, b"")), \
                patch.object(statecheck, "state_diffs", return_value=([], {})), \
                patch.object(statecheck, "compare_frames", return_value=[]):
            fn = statecheck.rewind_test if rewind else statecheck.scene_test
            return fn(Path(tmp), Path(tmp) / "case", "field", False)[0]

    def test_rewind_crash_after_capture(self):
        self.assertIn("R1 exited 134", self.run_case(True, "r1"))

    def test_replay_crash_after_capture(self):
        self.assertIn("R2 exited 134", self.run_case(True, "r2"))

    def test_save_crash_after_capture(self):
        self.assertIn("A exited 134", self.run_case(False, "a"))

    def test_load_crash_after_capture(self):
        self.assertIn("B exited 134", self.run_case(False, "b"))

    def test_speedup_scene_every_run(self):
        """The Speedup scene's load must continue a Speedup run: every run has it."""
        seen = {}

        def fake_run(build, out, disc, quit_at, env):
            seen[out.name] = env.get("XG_SPEEDUP")
            out.mkdir(parents=True, exist_ok=True)
            (out / "audio.pcm").write_bytes(b"")
            for save in env.get("XG_STATE_SAVE", "").split(","):
                Path(save.split(":", 1)[1]).touch()
            return 0

        with TemporaryDirectory() as tmp, \
                patch.object(statecheck, "run_port", side_effect=fake_run), \
                patch.object(statecheck, "read_state", return_value=({"vblank": 20, "dumpFrames": 0}, b"")), \
                patch.object(statecheck, "state_diffs", return_value=([], {})), \
                patch.object(statecheck, "compare_frames", return_value=[]):
            self.assertEqual([], statecheck.scene_test(Path(tmp), Path(tmp) / "case", "speedup", False)[0])
        self.assertEqual({"a": "1", "b": "1", "c": "1", "d": "1"}, seen)

    def test_successful_processes(self):
        self.assertEqual([], self.run_case(True, None))
        self.assertEqual([], self.run_case(False, None))


if __name__ == "__main__":
    main()
