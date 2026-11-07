#!/usr/bin/env python3
"""Capture boundaries and conservative race classification; no core or discs needed.

Picture tests use NumPy/Pillow when available, as beetle.py compare does.
"""
import io
import contextlib
import importlib.util
import json
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest
from types import SimpleNamespace
from unittest import mock

from beetle import Capture, KINDS, RaceCases, cmd_compare
from beetle_race import cmds, walk


def record(arg, frame, hblank, kind="v"):
    return (KINDS[kind] | hblank << 16, arg, frame, 1, frame)


class FrameBoundary(unittest.TestCase):
    def capture(self):
        c = Capture.__new__(Capture)
        c.nominal = 263
        c.trap = {0: 0}
        c.expect = {0: 0}
        c.last_return = None
        c.blocked_until = 0
        c.last_wait = -1.0
        c.last_wait_arg = 2
        c.last_frac = 0.0
        c.frame_v = -1
        c.pending = None
        c.held = []
        c.spliced = c.unspliced = c.forced_waits = c.synthesized = c.records = 0
        c.notes_spliced = []
        c.args = SimpleNamespace(start_vsync=0)
        c.log = io.StringIO()
        c.libetc = (0, 1)
        c.etc_prev = None
        return c

    def ram(self, c, previous, current):
        c.etc_prev = previous
        c.b = SimpleNamespace(u32=lambda address: current[address])

    def test_returned_wait_cannot_keep_main_thread_blocked(self):
        c = self.capture()
        c.last_return = c.blocked_until = 2067  # the log's stale prediction
        c.last_wait = 2064 + 2 / 263
        c.resync_return(2065, (1051, 1051))
        late = c.splice(2065, [record(2, 2066, 1)], [], (0, 0))
        self.assertEqual([r[1] for r in late], [2])
        c.feed(late)
        self.assertEqual(c.last_return, 2067)
        self.assertEqual(c.log.getvalue(), "v 2 2065.999\n")

    def test_pending_wait_still_blocks_extra_calls(self):
        c = self.capture()
        c.last_wait = 2065.5
        c.blocked_until = 2067
        c.resync_return(2066, (1051, 1052))
        self.assertEqual(c.last_return, 2065)
        self.assertEqual(c.splice(2066, [record(2, 2067, 1)], [], (0, 0)), [])

    def test_return_resync_handles_counter_wrap(self):
        c = self.capture()
        c.last_wait = 95.5
        c.blocked_until = 102
        c.resync_return(100, (0xFFFFFFFE, 1))
        self.assertEqual(c.last_return, 97)
        self.assertEqual(c.blocked_until, 97)

    def test_uninitialised_ram_does_not_resync(self):
        c = self.capture()
        c.last_return = c.blocked_until = 42
        c.resync_return(100, None)
        c.resync_return(100, (0, 0))
        self.assertEqual((c.last_return, c.blocked_until), (42, 42))

    def test_ram_proves_same_looking_calls_are_different_waits(self):
        c = self.capture()
        c.last_wait = 2064.008
        self.ram(c, (1051, 1052), (1053, 1053))
        missing = c.wait_missing(2067)
        self.assertTrue(missing)
        c.resync_return(2066, c.etc_frames[0])
        late = c.splice(2066, [record(2, 2067, 253)], [record(2, 2067, 254)],
                        (0, 0), missing_wait=missing)
        self.assertEqual(len(late), 1)
        self.assertEqual(c.forced_waits, 1)
        c.feed(late)
        c.feed([record(2, 2067, 254)])
        self.assertEqual(c.last_return, 2069)
        self.assertEqual(c.log.getvalue(), "v 2 2066.999\nv 2 2067.966\n")

    def test_recorded_frame_end_wait_prevents_duplicate_next_wait(self):
        c = self.capture()
        c.last_wait = 2065.999
        self.ram(c, (1051, 1052), (1053, 1053))
        missing = c.wait_missing(2067)
        self.assertFalse(missing)
        self.assertEqual(c.splice(2066, [record(2, 2067, 253)], [record(2, 2067, 254)],
                                  (0, 0), missing_wait=missing), [])

    def test_wait_without_any_copy_record_uses_reference_evidence(self):
        c = self.capture()
        c.last_return = 2115
        late = c.splice(2116, [], [], (0, 0), missing_wait=True)
        c.feed(late)
        self.assertEqual(c.log.getvalue(), "v 2 2116.999\n")
        self.assertEqual((c.synthesized, c.spliced), (1, 1))

    def test_no_wait_is_invented_without_reference_evidence(self):
        c = self.capture()
        self.assertEqual(c.splice(2116, [], [], (0, 0)), [])
        self.assertEqual(c.synthesized, 0)

    def test_ram_wait_checks_need_a_return_and_previous_state(self):
        for previous, current in [(None, (1, 1)), ((0, 0), (1, 1)),
                                  ((5, 6), (7, 8)), ((5, 6), (0, 0))]:
            with self.subTest(previous=previous, current=current):
                c = self.capture()
                self.ram(c, previous, current)
                self.assertFalse(c.wait_missing(10))

    def test_field_trailing_call_tied_with_next_copy_is_not_spliced(self):
        c = self.capture()
        cont = [record(1, 1340, 2), record(-1, 1340, 11),
                record(1, 1340, 255), record(0, 1340, 255)]
        nxt = [record(-1, 1340, 1), record(1, 1340, 2),
               record(-1, 1340, 11), record(1, 1340, 255)]
        self.assertEqual(c.splice(1339, cont, nxt, (1, -1)), [])

    def test_battle_frame_end_pair_is_not_mistaken_for_next_run(self):
        c = self.capture()
        cont = [record(-1, 2896, 1), record(0, 2896, 2)]
        nxt = [record(-1, 2896, 6)]
        late = c.splice(2895, cont, nxt, (1, -1))
        self.assertEqual([r[1] for r in late], [-1, 0])

    def test_interrupt_event_matching_remains_independent(self):
        c = self.capture()
        self.assertEqual(c.splice(100, [record(2, 101, 2, "r")],
                                  [record(2, 101, 3, "r")], (0, 0)), [])
        late = c.splice(100, [record(3, 101, 2, "r")], [], (0, 0))
        self.assertEqual(len(late), 1)
        self.assertEqual(late[0][0] & 0xFF, KINDS["r"])


class DrawRace(unittest.TestCase):
    def proof(self):
        return dict(shared=1, race_model="race@0x80001000", plain_vs_port_px=0,
                    race_vs_beetle_px=0, plain_vs_beetle_px=4, port_vs_beetle_px=4)

    def test_requires_exact_reconstructions_of_both_pictures(self):
        proof = self.proof()
        self.assertTrue(RaceCases.accepted(proof, 4))
        for key, value in [("shared", 0), ("race_model", "plain"), ("plain_vs_port_px", 1),
                           ("race_vs_beetle_px", 1), ("plain_vs_beetle_px", 5),
                           ("port_vs_beetle_px", 5), ("error", "no VRAM"),
                           ("plain_vs_port_px", False), ("shared", "1")]:
            with self.subTest(key=key, value=value):
                self.assertFalse(RaceCases.accepted(dict(proof, **{key: value}), 4))
        self.assertFalse(RaceCases.accepted({}, 4))
        self.assertFalse(RaceCases.accepted(proof, 0))

    def manifest(self, directory, region="US", **entry):
        path = directory / "races.json"
        frame = dict(mode="worldmap", ram_a="ram-a.bin", ram_b="ram-b.bin", vram="vram.bin", **entry)
        for name in ("ram-a.bin", "ram-b.bin", "vram.bin", "ref.png", "pc.png"):
            (directory / name).write_bytes(name.encode())
        path.write_text(json.dumps(dict(schema=1, region=region, frames={"10": frame})))
        return RaceCases(path, 1)

    def check(self, cases):
        return cases.check(10, cases.path.parent / "ref.png", cases.path.parent / "pc.png",
                           (15, 10), (216, 320, 3), False)

    def test_replay_receives_exact_current_images_and_records_input_hashes(self):
        with tempfile.TemporaryDirectory() as directory:
            cases = self.manifest(Path(directory))
            result = subprocess.CompletedProcess([], 0, json.dumps(self.proof()), "")
            with mock.patch("beetle.subprocess.run", return_value=result) as run:
                proof = self.check(cases)
            self.assertTrue(RaceCases.accepted(proof, 4))
            self.assertEqual(len(proof["inputs"]), 5)
            self.assertEqual(len(proof["inputs"]["ram_a"]["sha256"]), 64)
            command = run.call_args.args[0]
            self.assertIn(str(Path(directory) / "pc.png"), command)
            self.assertIn(str(Path(directory) / "ref.png"), command)
            self.assertIn("--worldmap", command)
            self.assertEqual(run.call_args.kwargs["timeout"], 1)
            self.assertIsNone(cases.check(11, None, None, None, None, False))

    def test_failed_missing_and_timed_out_evidence_stays_unaccepted(self):
        with tempfile.TemporaryDirectory() as directory:
            cases = self.manifest(Path(directory))
            results = [subprocess.CompletedProcess([], 1, "", "bad OT"),
                       subprocess.CompletedProcess([], 0, "not JSON", ""),
                       subprocess.CompletedProcess([], 0, "[]", "")]
            for result in results:
                with self.subTest(result=result), mock.patch("beetle.subprocess.run", return_value=result):
                    self.assertIn("error", self.check(cases))
            with mock.patch("beetle.subprocess.run", side_effect=subprocess.TimeoutExpired([], 1)):
                self.assertIn("error", self.check(cases))
            (Path(directory) / "ram-a.bin").unlink()
            result = subprocess.CompletedProcess([], 0, json.dumps(self.proof()), "")
            with mock.patch("beetle.subprocess.run", return_value=result):
                self.assertFalse(RaceCases.accepted(self.check(cases), 4))

    def test_us_context_addresses_cannot_be_used_for_jp(self):
        with tempfile.TemporaryDirectory() as directory:
            cases = self.manifest(Path(directory), region="JP")
            with mock.patch("beetle.subprocess.run") as run:
                self.assertIn("US only", self.check(cases)["error"])
                run.assert_not_called()

    def test_dma_walk_rejects_cycles_bad_addresses_and_truncated_commands(self):
        ram = bytearray(64)
        struct.pack_into("<II", ram, 0, 0x01000008, 0xE1000000)
        struct.pack_into("<I", ram, 8, 0xFFFFFF)
        self.assertEqual(walk(lambda a: ram, 0), [(0, [0xE1000000])])
        struct.pack_into("<I", ram, 8, 0)
        for head in (0, 1, 0x80200000):
            with self.subTest(head=head), self.assertRaises(ValueError):
                walk(lambda a: ram, head)
        with self.assertRaises(ValueError):
            cmds([0x200000FF])  # incomplete triangle
        with self.assertRaises(ValueError):
            cmds([0x480000FF, 0, 1])  # unterminated polyline


@unittest.skipUnless(importlib.util.find_spec("numpy") and importlib.util.find_spec("PIL"),
                     "picture comparisons need NumPy and Pillow")
class CompareRace(unittest.TestCase):
    def compare(self, directory, proof=None):
        from PIL import Image
        ref, pc = directory / "ref", directory / "pc"
        ref.mkdir()
        pc.mkdir()
        # A partial copy of the next frame is compatible with the pixel-mixing
        # heuristic, but could equally be a renderer bug.
        a = Image.new("RGB", (8, 8)); a.putpixel((0, 0), (248, 0, 0))
        a.save(ref / "v00010.png")
        a.save(ref / "vram00010.png")  # capture VRAM previews are not pictures
        Image.new("RGB", (8, 8)).save(pc / "frame_000010.png")
        Image.new("RGB", (8, 8), (248, 0, 0)).save(pc / "frame_000011.png")
        result = directory / "result.json"
        args = SimpleNamespace(ref=ref, pc=pc, offset=0, start=None, end=None, crop="0,0",
                               offset_search=0, search=0, pictures=None, worst=3, json=result,
                               race_cases=None, race_timeout=1)
        if proof is not None:
            manifest = directory / "cases.json"
            manifest.write_text('{"schema":1,"region":"US","frames":{}}')
            args.race_cases = manifest
        with contextlib.redirect_stdout(io.StringIO()), mock.patch.object(RaceCases, "check", return_value=proof):
            self.assertEqual(cmd_compare(args), 0)
        return json.loads(result.read_text())

    def test_pixel_mixing_alone_remains_unexplained(self):
        with tempfile.TemporaryDirectory() as directory:
            report = self.compare(Path(directory))
        self.assertEqual(len(report["rows"]), 1)
        self.assertEqual(report["rows"][0]["mixed"], [1])
        self.assertEqual(report["summary"]["content_different"], 1)
        self.assertEqual(report["summary"]["accepted_races"], 0)
        self.assertEqual(report["summary"]["first_unexplained"], 10)

    def test_exact_race_is_separate_from_remaining_content_differences(self):
        proof = dict(shared=1, race_model="race@0x80001000", plain_vs_port_px=0,
                     race_vs_beetle_px=0, plain_vs_beetle_px=1, port_vs_beetle_px=1)
        with tempfile.TemporaryDirectory() as directory:
            report = self.compare(Path(directory), proof)
        self.assertEqual(report["summary"]["accepted_races"], 1)
        self.assertEqual(report["summary"]["content_different"], 0)
        self.assertIsNone(report["summary"]["first_unexplained"])
        self.assertTrue(report["rows"][0]["accepted_race"])
        self.assertEqual(report["rows"][0]["diff"], 1)

    def test_failed_proof_retains_pixel_count_and_divergence(self):
        with tempfile.TemporaryDirectory() as directory:
            report = self.compare(Path(directory), {"error": "missing capture"})
        self.assertEqual(report["summary"]["content_different"], 1)
        self.assertEqual(report["summary"]["accepted_races"], 0)
        self.assertEqual(report["rows"][0]["diff"], 1)


if __name__ == "__main__":
    unittest.main()
