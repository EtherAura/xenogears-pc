#!/usr/bin/env python3
"""Coverage evidence must never promote missing/static/failed captures."""
import json
from contextlib import redirect_stdout
import io
from pathlib import Path
import struct
import tempfile
import unittest
from unittest.mock import patch
import zlib

import interpolation_coverage as C


def png(path, colour, filtering=0):
    def chunk(kind, data):
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))
    path.write_bytes(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", 1, 1, 8, 2, 0, 0, 0)) +
                     chunk(b"IDAT", zlib.compress(bytes([filtering, *colour]))) + chunk(b"IEND", b""))


class EvidenceTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name)
        self.directory = self.root / "run"
        (self.directory / "present").mkdir(parents=True)
        self.run = {"schema": 1, "disc": "us1", "field": 8, "binary_sha256": "build-a",
                    "disc_sha256": "disc-a", "dump_range": [100, 110], "exit_code": 0}
        self.catalogue = {"images": {"us1": {"sha256": "disc-a"}}, "fields": [
            {"disc": "us1", "field": 8, "availability": "present"},
            {"disc": "us1", "field": 9, "availability": "present"},
            {"disc": "us1", "field": 10, "availability": "placeholder"}]}
        self.lines = []

    def tearDown(self):
        self.tmp.cleanup()

    def event(self, event, reason, tick, alpha, field=8):
        self.lines.append(f"gpu: interpolation-coverage: field={field} event={event} reason={reason} vblank={tick:.3f} alpha={alpha:.6f}\n")

    def write(self):
        C.save(self.directory / "run.json", self.run)
        (self.directory / "port.log").write_text("".join(self.lines))

    def motion(self, changed=True):
        self.event("snapshot", "valid", -1, -1)
        self.event("endpoint", "compatible", 100, 0)
        self.event("intermediate", "compatible", 101, .5)
        png(self.directory / "present/present_000100.png", [10, 20, 30])
        if changed:
            png(self.directory / "present/present_000101.png", [30, 20, 10])
        self.write()

    def test_motion_requires_changed_pixels(self):
        self.motion()
        result = C.report(self.catalogue, [self.root])
        self.assertEqual([row["status"] for row in result["fields"]],
                         ["motion-demonstrated", "unvisited", "not-on-disc"])
        self.assertEqual(result["fields"][0]["changed_intermediates"], 1)

    def test_unchanged_dumps_are_carried_forward_without_motion_claim(self):
        self.motion(changed=False)
        self.assertEqual(C.report(self.catalogue, [self.root])["fields"][0]["status"], "intermediate-unverified")

    def test_missing_pixels_do_not_count(self):
        self.motion()
        for path in (self.directory / "present").glob("*.png"):
            path.unlink()
        self.assertEqual(C.evidence(self.run, self.directory)["8"]["changed_intermediates"], 0)

    def test_different_png_encodings_of_same_pixels_do_not_count(self):
        self.motion(changed=False)
        png(self.directory / "present/present_000101.png", [10, 20, 30], filtering=1)
        self.assertEqual(C.evidence(self.run, self.directory)["8"]["changed_intermediates"], 0)

    def test_corrupt_pixels_cannot_be_motion_proof(self):
        self.motion()
        path = self.directory / "present/present_000101.png"
        data = bytearray(path.read_bytes()); data[48] ^= 1; path.write_bytes(data)
        with self.assertRaisesRegex(ValueError, "checksum"):
            C.evidence(self.run, self.directory)

    def test_static_endpoints_are_separate(self):
        self.event("snapshot", "valid", -1, -1)
        self.event("endpoint", "compatible", 100, 0)
        self.write()
        self.assertEqual(C.report(self.catalogue, [self.root])["fields"][0]["status"], "endpoints-only")

    def test_failures_remain_failures_even_with_earlier_motion(self):
        self.motion(); self.run["exit_code"] = -11; self.write()
        self.assertEqual(C.report(self.catalogue, [self.root])["fields"][0]["status"], "failed-run")

    def test_reset_and_fallback_clear_the_endpoint_anchor(self):
        self.motion(); self.lines.insert(2, self.lines[0].replace("event=snapshot reason=valid", "event=reset reason=framebuffer-transfer"))
        self.write()
        self.assertEqual(C.evidence(self.run, self.directory)["8"]["changed_intermediates"], 0)

    def test_per_field_reasons_do_not_leak_to_other_fields(self):
        self.event("snapshot", "unidentified-geometry", -1, -1)
        self.event("native", "unidentified-geometry", 100, -1)
        self.event("native", "missing-snapshot", 101, -1, field=9)
        self.event("native", "missing-snapshot", 102, -1, field=-1)
        self.write()
        result = C.evidence(self.run, self.directory)
        self.assertEqual(result["8"]["fallbacks"]["native:unidentified-geometry"], 1)
        self.assertNotIn("native:unidentified-geometry", result["9"]["fallbacks"])
        self.assertNotIn("-1", result)

    def test_wrong_disc_or_mixed_binaries_refused(self):
        self.motion(); self.run["disc_sha256"] = "other"; self.write()
        with self.assertRaisesRegex(ValueError, "disc identity"):
            C.report(self.catalogue, [self.root])
        self.run["disc_sha256"] = "disc-a"; self.write()
        other = self.root / "other"; other.mkdir()
        C.save(other / "run.json", dict(self.run, binary_sha256="build-b"))
        (other / "port.log").write_text("".join(self.lines))
        with self.assertRaisesRegex(ValueError, "mixed us binaries"):
            C.report(self.catalogue, [self.root])

    def test_endpoint_outside_capture_window_is_not_motion_proof(self):
        self.motion(); self.run["dump_range"] = [101, 110]
        self.assertEqual(C.evidence(self.run, self.directory)["8"]["changed_intermediates"], 0)

    def test_overlapping_input_directories_do_not_duplicate_motion(self):
        self.motion()
        result = C.report(self.catalogue, [self.root, self.directory])
        self.assertEqual(len(result["runs"]), 1)
        self.assertEqual(result["fields"][0]["changed_intermediates"], 1)

    def test_probe_passes_its_input_script_and_records_options(self):
        build = self.root / "build"; build.mkdir()
        (build / "CMakeCache.txt").write_text("XG_VERSION:STRING=US\n")
        args = C.argparse.Namespace(build=build, out=self.root / "probe", disc="us1", field=8,
                                   frames=1500, start=1000, gpu="0", timeout=1, env=["XG_PARTY=0,1,2"], pad=None)
        with patch.object(C.shutil, "which", return_value="/usr/bin/bwrap"), \
             patch.object(C, "sha256", return_value="identity"), \
             patch.object(C.subprocess, "run") as run:
            run.return_value.returncode = 0
            with redirect_stdout(io.StringIO()):
                C.probe(args)
        env = run.call_args.kwargs["env"]
        self.assertEqual(env["XG_PAD_SCRIPT"], str(args.out / "pad.txt"))
        self.assertEqual(env["XG_PARTY"], "0,1,2")
        self.assertIn("1400 0004", Path(env["XG_PAD_SCRIPT"]).read_text())
        self.assertEqual(json.loads((args.out / "run.json").read_text())["fixture"]["XG_PARTY"], "0,1,2")


if __name__ == "__main__":
    unittest.main()
