#!/usr/bin/env python3
"""The link-table check (tools/check_link_tables.py), run as its CLI on synthetic regions: an
invented manifest and invented module images per region under TMPDIR, and tables made of them.
No disc, no retail bytes. Also shows the cases fail when the scan is disabled or its minimum
raised, through the checker itself (not a copy of the algorithm)."""
import contextlib
import hashlib
import io
import json
import random
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

TOOLS = Path(__file__).resolve().parent
TOOL = TOOLS / "check_link_tables.py"
sys.path.insert(0, str(TOOLS))
import check_link_tables  # noqa: E402
import payloadcheck  # noqa: E402

# An invented block with low-information and zero-padded stretches, the same in each region's main.
LOW3 = bytes([1, 2, 3] * 11)                         # 3 distinct values: never informative
SPARSE = bytes(13) + bytes([5, 6, 7])                 # 4 distinct, 3 non-zero: never informative
PAD15 = bytes(8) + bytes(range(0x41, 0x50)) + bytes(8)  # 15 informative bytes between zeros
PAD16 = bytes(8) + bytes(range(0x61, 0x71)) + bytes(8)  # 16 informative bytes between zeros


def invented(seed: int, size: int) -> bytes:
    return random.Random(seed).randbytes(size)


class Region:
    """A synthetic region: config/<ver>/disc.yaml (one ISO executable, one overlay) and the images."""

    def __init__(self, root: Path, ver: str, seed: int):
        self.ver = ver
        self.main = invented(seed, 0x400) + LOW3 + SPARSE + PAD15 + PAD16 + invented(seed + 1, 0x100)
        self.ovl = invented(seed + 2, 0x300)
        cfg, disc = root / "config" / ver, root / "disc" / ver
        cfg.mkdir(parents=True)
        disc.mkdir(parents=True)
        (disc / "main.exe").write_bytes(self.main)
        (disc / "ovl.bin").write_bytes(self.ovl)
        sha = lambda b: hashlib.sha1(b).hexdigest()  # noqa: E731
        (cfg / "disc.yaml").write_text(
            f"image: synthetic {ver}\nfiles:\n"
            f"  - {{name: main, iso: SYNTH_{ver.upper()}.00, vram: 0x80010000, sha1: {sha(self.main)}}}\n"
            f"  - {{name: ovl, dir: 0x01, file: 0x02, lzss: true, vram: 0x80080000, sha1: {sha(self.ovl)}}}\n")


def facts_like(region: Region, extra: bytes = b"") -> bytes:
    """A table shaped like pc_link's facts: sizes and hashes of the region's images (no bytes)."""
    doc = {"format": "xenogears-pc-link-facts", "version": 1, "region": region.ver,
           "boot": f"SYNTH_{region.ver.upper()}.00",
           "modules": {"main": {"size": len(region.main), "copies": hashlib.sha1(region.main).hexdigest()[:16],
                                "hash": hashlib.sha1(region.main[:64]).hexdigest()[:16], "differs": [1, 4]},
                       "ovl": {"size": len(region.ovl), "copies": "0123456789abcdef",
                               "hash": hashlib.sha1(region.ovl).hexdigest()[:16], "differs": []}}}
    return json.dumps(doc, indent=1).encode() + extra + b"\n"


class CheckLinkTables(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.dir = Path(self.tmp.name)
        self.root = self.dir / "ref"
        self.us = Region(self.root, "us", 100)
        self.jp = Region(self.root, "jp", 200)

    def tearDown(self):
        self.tmp.cleanup()

    def table(self, name: str, data: bytes) -> Path:
        p = self.dir / name
        p.write_bytes(data)
        return p

    def run_cli(self, ver, *tables, root=None):
        r = subprocess.run([sys.executable, str(TOOL), "--ver", ver, "--reference-root", str(root or self.root),
                            "--tables", *map(str, tables)], capture_output=True, text=True)
        return r.returncode, r.stdout + r.stderr

    def run_inproc(self, ver, *tables):
        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            rc = check_link_tables.main(["--ver", ver, "--reference-root", str(self.root),
                                         "--tables", *map(str, tables)])
        return rc, out.getvalue()

    def assertNoBytes(self, out: str, *pieces: bytes):
        for b in pieces:
            self.assertNotIn(b.hex(), out)
            self.assertNotIn(b.hex(" "), out)
            self.assertNotIn(b.decode("latin-1"), out)

    def runs(self, out: str, path: Path) -> list[tuple[int, int]]:
        prefix = f"{path}+0x"
        found = []
        for line in out.splitlines():
            line = line.strip()
            if line.startswith(prefix):
                off, length, _ = line[len(prefix):].split(" ", 2)
                found.append((int(off, 16), int(length)))
        return found

    def test_clean_tables_both_regions(self):
        for region in (self.us, self.jp):
            t = self.table(f"{region.ver}-facts.json", facts_like(region))
            rc, out = self.run_cli(region.ver, t)
            self.assertEqual(rc, 0, out)
            self.assertIn(f"ok         {t}", out)
            self.assertIn("link tables ok: 1 files", out)
            self.assertIn("2 module images, runs of >= 16 bytes", out)

    def test_exact_16_byte_run(self):
        piece = self.us.ovl[0x40:0x50]
        self.assertTrue(payloadcheck.informative(piece))
        base = facts_like(self.us)
        t = self.table("t16.json", base + piece)
        rc, out = self.run_cli("us", t)
        self.assertEqual(rc, 1, out)
        self.assertEqual(self.runs(out, t), [(len(base), 16)])
        self.assertIn("link tables FAIL", out)
        self.assertNoBytes(out, piece)

    def test_longer_run(self):
        piece = self.us.main[0x100:0x15D]
        t = self.table("tlong.bin", b"head" + piece + b"tail")
        rc, out = self.run_cli("us", t)
        self.assertEqual(rc, 1, out)
        self.assertEqual(self.runs(out, t), [(4, len(piece))])
        self.assertNoBytes(out, piece, piece[:16])

    def test_overlapping_and_adjacent_windows_merge(self):
        a = self.us.main[0x10:0x24]  # 20 bytes: five overlapping windows, one run
        b = self.us.ovl[0x200:0x210]  # another image's 16 bytes right after: adjacent, joins it
        c = self.us.main[0x300:0x318]
        t = self.table("tover.bin", b"x" * 7 + a + b + b"-" * 9 + c)
        rc, out = self.run_cli("us", t)
        self.assertEqual(rc, 1, out)
        self.assertEqual(self.runs(out, t), [(7, 36), (7 + 36 + 9, 24)])
        self.assertIn("2 retail runs, 60 bytes", out)

    def test_15_bytes_do_not_match(self):
        t = self.table("t15.bin", b"{" + self.us.main[0x80:0x8F] + b"}" + self.us.ovl[0x10:0x1F])
        rc, out = self.run_cli("us", t)
        self.assertEqual(rc, 0, out)

    def test_low_information_windows(self):
        t = self.table("tlow.bin", b"<" + LOW3 + b">" + SPARSE + b"|" + SPARSE + SPARSE)
        rc, out = self.run_cli("us", t)
        self.assertEqual(rc, 0, out)

    def test_zero_padding(self):
        # 15 informative bytes padded with the reference's zeros: under a window once trimmed.
        t = self.table("tpad15.bin", b"a" + PAD15 + b"b")
        rc, out = self.run_cli("us", t)
        self.assertEqual(rc, 0, out)
        # 16 padded ones: one run, its length without the zeros.
        t = self.table("tpad16.bin", b"a" + PAD16 + b"b")
        rc, out = self.run_cli("us", t)
        self.assertEqual(rc, 1, out)
        self.assertEqual(self.runs(out, t), [(1 + 8, 16)])

    def test_every_table_checked_one_unsafe(self):
        first = self.table("first.json", facts_like(self.us))
        bad = self.table("bad.json", facts_like(self.us, self.us.main[0x200:0x220]))
        last = self.table("last.json", facts_like(self.jp))
        rc, out = self.run_cli("us", first, bad, last)
        self.assertEqual(rc, 1, out)
        self.assertIn(f"ok         {first}", out)
        self.assertIn(f"FAIL       {bad}", out)
        self.assertIn(f"ok         {last}", out)
        self.assertIn("3 files, 1 with retail runs (1 runs/32 bytes)", out)
        # Unsafe first and last: both found, not only the first.
        rc, out = self.run_cli("us", bad, first, bad)
        self.assertEqual(rc, 1, out)
        self.assertIn("3 files, 2 with retail runs (2 runs/64 bytes)", out)

    def test_region_selection(self):
        jp_bytes = self.table("jp-bytes.bin", self.jp.ovl[0x20:0x60])
        us_bytes = self.table("us-bytes.bin", self.us.ovl[0x20:0x60])
        self.assertEqual(self.run_cli("jp", jp_bytes)[0], 1)
        self.assertEqual(self.run_cli("jp", us_bytes)[0], 0)
        self.assertEqual(self.run_cli("us", us_bytes)[0], 1)
        self.assertEqual(self.run_cli("us", jp_bytes)[0], 0)

    def test_wrong_or_missing_reference(self):
        t = self.table("t.json", facts_like(self.us))
        # The other region's image where this region's belongs.
        (self.root / "disc" / "us" / "ovl.bin").write_bytes(self.jp.ovl)
        rc, out = self.run_cli("us", t)
        self.assertEqual(rc, 2, out)
        self.assertIn("ovl", out)
        self.assertIn("sha1 differs", out)
        # A missing image: the set is incomplete.
        (self.root / "disc" / "us" / "ovl.bin").unlink()
        rc, out = self.run_cli("us", t)
        self.assertEqual(rc, 2, out)
        self.assertIn("incomplete", out)
        # No images at all, a manifest without sha1s, no manifest, a manifest listing nothing.
        rc, out = self.run_cli("jp", t, root=self.dir / "nowhere")
        self.assertEqual(rc, 2, out)
        cfg = self.root / "config" / "jp" / "disc.yaml"
        cfg.write_text(cfg.read_text().replace("sha1:", "crc:"))
        self.assertEqual(self.run_cli("jp", t)[0], 2)
        cfg.write_text("image: x\nfiles: []\n")
        self.assertEqual(self.run_cli("jp", t)[0], 2)
        cfg.unlink()
        self.assertEqual(self.run_cli("jp", t)[0], 2)

    def test_bad_table_inputs(self):
        good = self.table("good.json", facts_like(self.us))
        bad = self.table("bad.json", self.us.main[0x40:0x60])
        missing = self.dir / "absent.json"
        empty = self.table("empty.json", b"")
        folder = self.dir / "folder"
        folder.mkdir()
        rc, out = self.run_cli("us", missing, good, empty, folder, bad)
        self.assertEqual(rc, 2, out)
        self.assertIn(f"unreadable {missing}", out)
        self.assertIn(f"empty      {empty}", out)
        self.assertIn(f"unreadable {folder}", out)
        self.assertIn(f"ok         {good}", out)
        self.assertIn(f"FAIL       {bad}", out)  # still scanned after the bad inputs
        self.assertIn("link tables FAIL: 5 files, 1 with retail runs", out)
        # No selection, no region, another region: usage errors, never a pass.
        for args in (["--ver", "us", "--tables"], ["--ver", "us"], ["--tables", str(good)],
                     ["--ver", "eu", "--tables", str(good)]):
            r = subprocess.run([sys.executable, str(TOOL), "--reference-root", str(self.root), *args],
                               capture_output=True, text=True)
            self.assertEqual(r.returncode, 2, args)

    def test_catches_disabled_or_weakened_scan(self):
        """The cases above depend on the checker's real scan: with it disabled, or with its minimum
        raised to 17, the unsafe tables pass (so those tests would fail)."""
        t16 = self.table("t16.bin", b"=" + self.us.ovl[0x40:0x50] + b"=")
        long_ = self.table("tl.bin", self.us.main[0x100:0x140])
        self.assertEqual(self.run_inproc("us", t16)[0], 1)
        self.assertEqual(self.run_inproc("us", long_)[0], 1)
        with mock.patch.object(check_link_tables, "MIN", 17):
            self.assertEqual(self.run_inproc("us", t16)[0], 0)
        with mock.patch.object(payloadcheck, "hits", lambda data, wanted, n: []):
            self.assertEqual(self.run_inproc("us", t16, long_)[0], 0)
        with mock.patch.object(payloadcheck, "windows", lambda images, n: set()):
            self.assertEqual(self.run_inproc("us", t16, long_)[0], 0)
        with mock.patch.object(check_link_tables, "scan", lambda data, wanted: []):
            self.assertEqual(self.run_inproc("us", t16, long_)[0], 0)


if __name__ == "__main__":
    unittest.main()
