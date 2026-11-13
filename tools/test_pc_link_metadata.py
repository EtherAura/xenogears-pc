#!/usr/bin/env python3
"""The PC link's facts from the extracted images (tools/pc_link.py): the facts file's schema and
both regions' identity, its round trip, its refusals, and that the copy lists made from the file
are the ones made from the images, with the images' directory closed to the file's path.
Synthetic modules and bytes only: no disc, no retail bytes."""
import builtins
import io
import json
import os
import sys
import tempfile
import unittest
from contextlib import contextmanager
from pathlib import Path
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parent))
from pc_link import (FactsError, DiscFacts, Image, MetadataFacts, boot_name, copy_list, disc_files,  # noqa: E402
                     facts_json, load_facts, parse_facts)


def fnv(data: bytes) -> int:
    h = 0xCBF29CE484222325
    for b in data:
        h = ((h ^ b) * 0x100000001B3) & 0xFFFFFFFFFFFFFFFF
    return h


@contextmanager
def closed(root: Path):
    """Every open and stat under `root` fails (and is recorded) while inside."""
    tried = []
    real_open, real_io_open, real_os_open, real_stat = builtins.open, io.open, os.open, os.stat

    def check(path):
        p = os.path.abspath(os.fspath(path)) if isinstance(path, (str, bytes, os.PathLike)) else ""
        if isinstance(p, bytes):
            p = p.decode()
        if p == str(root) or p.startswith(str(root) + os.sep):
            tried.append(p)
            raise PermissionError(f"disc access refused: {p}")

    def guarded(real):
        def f(path, *a, **k):
            check(path)
            return real(path, *a, **k)
        return f
    with mock.patch("builtins.open", guarded(real_open)), mock.patch("io.open", guarded(real_io_open)), \
            mock.patch("os.open", guarded(real_os_open)), mock.patch("os.stat", guarded(real_stat)):
        yield tried


class Module:
    """A synthetic module: data at 0x80100000 in a file 0x100 bytes on, its file, and three
    copies (the second disc-specific, the third where the file's bytes are not the data's)."""
    BASE, DELTA = 0x80100000, 0x100 - 0x80100000

    def __init__(self, disc: Path):
        self.img = Image("mod", deltas={self.DELTA})
        data = bytes(range(1, 0x41))
        for i, b in enumerate(data):
            self.img.mem[self.BASE + i] = b
        self.file = bytearray(0x100) + bytearray(data) + bytearray(0x40)
        self.file[0x100 + 0x30] ^= 0xFF  # the file's byte differs from the data assembly's
        self.path = disc / "mod.bin"
        self.path.write_bytes(bytes(self.file))
        self.info = {"path": self.path, "dir": 3, "file": 7, "lzss": False}
        self.copies = [("D_A", 0, self.BASE, 0x10, False), ("D_B", 4, self.BASE + 0x10, 0x8, True),
                       ("D_C", 0, self.BASE + 0x30, 0x10, False)]


class Facts(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.disc = Path(self.tmp.name) / "disc"
        self.disc.mkdir()
        self.mod = Module(self.disc)

    def tearDown(self):
        self.tmp.cleanup()

    def from_disc(self, copies=None):
        m = self.mod
        return copy_list("mod", m.copies if copies is None else copies, m.img, m.info, DiscFacts())

    def doc(self, ver="us", boot="SYNTH_US.00", **changes):
        facts = self.from_disc()[3]
        d = json.loads(facts_json(ver, boot, {"mod": facts, "empty": {"size": 0x20, "copies": fnv(b""),
                                                                      "hash": fnv(b""), "differs": []}}))
        d.update(changes)
        return d

    def parse(self, d, ver="us", boot="SYNTH_US.00", modules=("mod", "empty")):
        return parse_facts(d if isinstance(d, str) else json.dumps(d), ver, boot, list(modules))

    def test_disc_facts(self):
        items, notes, entry, facts = self.from_disc()
        f = self.mod.file
        self.assertEqual(facts["size"], len(f))
        # The disc-specific copy is left out of the hash.
        self.assertEqual(facts["hash"], fnv(bytes(f[0x100:0x110]) + bytes(f[0x130:0x140])))
        self.assertEqual(facts["differs"], [2])
        self.assertEqual(items, ["    {(char*)D_A, 0x100, 0x10},", "    {(char*)D_B + 4, 0x110, 0x8 | 0x80000000u},",
                                 "    {(char*)D_C, 0x130, 0x10},"])
        self.assertEqual(notes, ["mod D_C+0x0: the disc's bytes differ from the data assembly's (the disc's are used)"])
        self.assertEqual((entry["count"], entry["size"], entry["hash"], entry["dir"], entry["file"]),
                         (3, len(f), facts["hash"], 3, 7))

    def test_schema(self):
        d = self.doc()
        self.assertEqual(set(d), {"format", "version", "region", "boot", "modules"})
        self.assertEqual((d["format"], d["version"], d["region"], d["boot"]),
                         ("xenogears-pc-link-facts", 1, "us", "SYNTH_US.00"))
        self.assertEqual(set(d["modules"]["mod"]), {"size", "copies", "hash", "differs"})
        # Addresses, sizes and hashes only: no byte string anywhere.
        for v in d["modules"]["mod"].values():
            self.assertIsInstance(v, (int, str, list))
        self.assertRegex(d["modules"]["mod"]["hash"], r"^[0-9a-f]{16}$")

    def test_round_trip_both_regions(self):
        facts = self.from_disc()[3]
        for ver, boot in (("us", "SYNTH_US.00"), ("jp", "SYNTH_JP.00")):
            text = facts_json(ver, boot, {"mod": facts})
            doc = parse_facts(text, ver, boot, ["mod"])
            self.assertEqual(doc["region"], ver)
            self.assertEqual(doc["modules"]["mod"], facts)
            self.assertEqual(facts_json(ver, boot, doc["modules"]), text)
            path = Path(self.tmp.name) / f"{ver}.json"
            path.write_text(text)
            self.assertEqual(load_facts(path, ver, boot, ["mod"])["modules"], doc["modules"])

    def test_region_identities(self):
        # Each region's boot file (config/<ver>/disc.yaml) names that region and not the other.
        us, jp = boot_name(disc_files("us")), boot_name(disc_files("jp"))
        self.assertIsInstance(us, str)
        self.assertIsInstance(jp, str)
        self.assertNotEqual(us, jp)

    def test_same_output_without_disc(self):
        disc_out = self.from_disc()
        doc = self.parse(self.doc())
        m = self.mod
        with closed(self.disc) as tried:
            with self.assertRaises(PermissionError):
                self.from_disc()  # the images' path is closed
            del tried[:]
            meta_out = copy_list("mod", m.copies, m.img, m.info, MetadataFacts(doc))
            empty = copy_list("empty", [], None, None, MetadataFacts(doc))
        self.assertEqual(tried, [])
        self.assertEqual(meta_out, disc_out)
        self.assertEqual((empty[0], empty[2]["size"], empty[2]["hash"]), ([], 0x20, fnv(b"")))

    def test_rejects_malformed(self):
        good = self.doc()
        bad = [
            "not json", "[]", json.dumps({k: v for k, v in good.items() if k != "boot"}),
            json.dumps({**good, "extra": 1}), json.dumps({**good, "format": "other"}),
            json.dumps({**good, "version": 2}), json.dumps({**good, "version": True}),
            json.dumps({**good, "modules": []}),
            '{"format": "xenogears-pc-link-facts", "format": "xenogears-pc-link-facts"}',
        ]
        mod = good["modules"]["mod"]
        for k, v in [("size", -1), ("size", 1 << 32), ("size", True), ("size", "0x10"), ("hash", "ABCDEF0123456789"),
                     ("hash", "12"), ("copies", 5), ("differs", [2, 1]), ("differs", [1, 1]), ("differs", [-1]),
                     ("differs", "2")]:
            bad.append(json.dumps({**good, "modules": {**good["modules"], "mod": {**mod, k: v}}}))
        bad.append(json.dumps({**good, "modules": {**good["modules"], "mod": {k: v for k, v in mod.items()
                                                                               if k != "hash"}}}))
        for text in bad:
            with self.subTest(text=text[:80]), self.assertRaises(FactsError):
                self.parse(text)

    def test_rejects_incomplete_or_other_link(self):
        good = self.doc()
        with self.assertRaisesRegex(FactsError, "missing \\['other'\\]"):
            self.parse(good, modules=("mod", "empty", "other"))
        with self.assertRaisesRegex(FactsError, "not linked \\['empty'\\]"):
            self.parse(good, modules=("mod",))
        with self.assertRaisesRegex(FactsError, "region 'us', but this link is 'jp'"):
            self.parse(good, ver="jp", boot="SYNTH_US.00")
        with self.assertRaisesRegex(FactsError, "boot file"):
            self.parse(good, boot="SYNTH_JP.00")  # labelled US, another region's boot file
        with self.assertRaisesRegex(FactsError, "boot file"):
            self.parse({**good, "boot": None}, boot=None)
        with self.assertRaisesRegex(FactsError, "No such file"):
            load_facts(Path(self.tmp.name) / "absent.json", "us", "SYNTH_US.00", ["mod"])

    def test_rejects_facts_for_another_copy_list(self):
        doc = self.parse(self.doc())
        m = self.mod
        changed = m.copies[:2] + [("D_C", 0, Module.BASE + 0x30, 0xC, False)]
        with self.assertRaisesRegex(FactsError, "copy list is not the one"):
            copy_list("mod", changed, m.img, m.info, MetadataFacts(doc))
        with self.assertRaisesRegex(FactsError, "copy list is not the one"):
            copy_list("mod", [], None, None, MetadataFacts(doc))

    def test_rejects_copy_past_the_file(self):
        d = self.doc()
        d["modules"]["mod"]["size"] = 0x13F  # one byte short of the last copy's end
        with self.assertRaises(SystemExit) as e:
            copy_list("mod", self.mod.copies, self.mod.img, self.mod.info, MetadataFacts(self.parse(d)))
        self.assertIn("run past mod.bin", str(e.exception))
        short = Module.BASE + 0x38  # and from the images: a copy past a shorter file
        self.mod.path.write_bytes(bytes(self.mod.file[:0x13F]))
        with self.assertRaises(SystemExit) as e:
            self.from_disc([("D_C", 0, short, 0x8, False)])
        self.assertIn("run past mod.bin", str(e.exception))

    def test_differs_past_the_copies(self):
        d = self.doc()
        d["modules"]["mod"]["differs"] = [2, 3]
        with self.assertRaisesRegex(FactsError, "differs names copy 3 of 3"):
            copy_list("mod", self.mod.copies, self.mod.img, self.mod.info, MetadataFacts(self.parse(d)))


if __name__ == "__main__":
    unittest.main()
