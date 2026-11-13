#!/usr/bin/env python3
"""The PC link's PS1 object facts (tools/pc_link.py --write-ps1-objects / --ps1-objects): the
producer reading the PS1 build's images, the file's schema, both regions' identity, its round
trip, its refusals, that the check's input made from the file is the one made from the images,
and that a link given the file never reaches the images (a guard that refuses them, checked
first on pc_link's own reader). Synthetic modules, symbols and images only: no PS1 build, no disc."""
import builtins
import contextlib
import io
import json
import os
import struct
import sys
import tempfile
import unittest
from contextlib import contextmanager
from pathlib import Path
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parent))
import pc_link  # noqa: E402
from pc_link import (Ps1ObjectsError, load_ps1_objects, parse_ps1_objects, ps1_c_objects,  # noqa: E402
                     ps1_elf, ps1_elf_objects, ps1_objects_facts, ps1_objects_json, read_ps1_objects)

BOOT = {"us": "SYNTH_US.00", "jp": "SYNTH_JP.00"}
MODULES = ["main", "ovl"]
OBJECT, FUNC, GLOBAL, LOCAL, ABS = 1, 2, 1, 0, 0xFFF1


def elf32(symbols: list[tuple[str, int, int, int, int, int]]) -> bytes:
    """A little-endian ELF32 file with one symbol table: (name, value, size, type, binding, section)."""
    strtab, symtab = bytearray(b"\0"), bytearray(16)
    for name, value, size, typ, bind, shndx in symbols:
        symtab += struct.pack("<IIIBBH", len(strtab), value, size, (bind << 4) | typ, 0, shndx)
        strtab += name.encode() + b"\0"
    strtab += bytes(-len(strtab) % 4)
    str_off, sym_off = 52, 52 + len(strtab)
    shoff = sym_off + len(symtab)
    head = bytearray(b"\x7fELF\x01\x01\x01" + bytes(9) + bytes(36))
    struct.pack_into("<I", head, 0x20, shoff)
    struct.pack_into("<HHH", head, 0x2E, 40, 3, 2)
    sections = bytes(40) + struct.pack("<10I", 0, 2, 0, 0, sym_off, len(symtab), 2, 1, 4, 16) \
        + struct.pack("<10I", 0, 3, 0, 0, str_off, len(strtab), 0, 0, 1, 0)
    return bytes(head + strtab + symtab + sections)


# Each region's synthetic PS1 images: what the PS1 build placed (JP one word further on).
def images(ver: str) -> dict[str, list]:
    d = 0 if ver == "us" else 4
    return {"main": [("D_one", 0x80020000 + d, 8, OBJECT, GLOBAL, 1), ("D_one_alias", 0x80020000 + d, 8, OBJECT, GLOBAL, 1),
                     ("synthFunctionNameXY", 0x80011000, 0x40, FUNC, GLOBAL, 1), ("D_two", 0x80020010 + d, 4, OBJECT, GLOBAL, 2),
                     ("D_local", 0x80020020, 4, OBJECT, LOCAL, 2), ("D_undef", 0, 0, 0, GLOBAL, 0),
                     ("D_zero", 0x80020030 + d, 0, OBJECT, GLOBAL, 2)],
            "ovl": [("D_ovl", 0x80100000 + d, 0, 0, GLOBAL, ABS), ("D_ovl_b", 0x80100100 + d, 0x20, OBJECT, GLOBAL, 3),
                    ("D_not_c", 0x80100200, 4, OBJECT, GLOBAL, 3)]}


def pc_data(root: Path) -> dict[str, dict[str, tuple[str, str, int]]]:
    """Each module's C data as nm gives it from the PC objects (D_one_alias: the same place as D_one)."""
    o = {m: str(root / f"pc/CMakeFiles/game_{m}.dir/src/{m}.c.o") for m in MODULES}
    return {"main": {"D_one": (o["main"], "D", 0), "D_one_alias": (o["main"], "D", 0), "D_two": (o["main"], "B", 0x10),
                     "D_zero": (o["main"], "B", 0x20), "D_port_only": (o["main"], "B", 0x40)},
            "ovl": {"D_ovl": (o["ovl"], "D", 0), "D_ovl_b": (o["ovl"], "D", 0x10)}}


def make_tree(root: Path, ver: str) -> None:
    (root / "config" / ver / "symbols").mkdir(parents=True)
    (root / "config" / ver / "disc.yaml").write_text(
        f"files:\n  - {{name: main, iso: {BOOT[ver]}, vram: 0x80010000}}\n"
        "  - {name: ovl, dir: 1, file: 2, vram: 0x80100000}\n")
    (root / "config" / "handwritten.yaml").write_text("")
    (root / "include").mkdir()
    (root / "include" / "psyq.h").write_text("")
    (root / "build" / ver).mkdir(parents=True)
    for m, syms in images(ver).items():
        (root / "build" / ver / f"{m}.elf").write_bytes(elf32(syms))


@contextmanager
def ps1_elfs_closed(root: Path, ver: str):
    """Every open, stat, listing or command naming a PS1 build image under root/build/<ver> (or
    listing that directory) fails and is recorded while inside."""
    tried = []
    d = os.path.abspath(root / "build" / ver)

    def check(path):
        if isinstance(path, (str, bytes, os.PathLike)):
            p = os.path.abspath(os.fsdecode(os.fspath(path)))
            if p == d or (p.startswith(d + os.sep) and p.endswith(".elf")):
                tried.append(p)
                raise PermissionError(f"PS1 build image refused: {p}")

    def guarded(real):
        def f(path=".", *a, **k):
            check(path)
            return real(path, *a, **k)
        return f

    def guarded_cmd(real):
        def f(cmd, *a, **k):
            for c in [cmd] if isinstance(cmd, (str, bytes, os.PathLike)) else cmd:
                check(c)
            return real(cmd, *a, **k)
        return f
    names = ["builtins.open", "io.open", "os.open", "os.stat", "os.lstat", "os.listdir", "os.scandir"]
    reals = [builtins.open, io.open, os.open, os.stat, os.lstat, os.listdir, os.scandir]
    with contextlib.ExitStack() as stack:
        for n, r in zip(names, reals):
            stack.enter_context(mock.patch(n, guarded(r)))
        stack.enter_context(mock.patch("subprocess.run", guarded_cmd(pc_link.subprocess.run)))
        stack.enter_context(mock.patch("subprocess.Popen", guarded_cmd(pc_link.subprocess.Popen)))
        yield tried


class Tree(unittest.TestCase):
    ver = "us"

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name)
        make_tree(self.root, self.ver)
        self.data = pc_data(self.root)
        self.patch = mock.patch.object(pc_link, "ROOT", self.root)
        self.patch.start()

    def tearDown(self):
        self.patch.stop()
        self.tmp.cleanup()

    def facts_text(self, ver=None):
        return ps1_objects_json(ver or self.ver, BOOT[ver or self.ver],
                                ps1_objects_facts(self.data, read_ps1_objects(self.ver, MODULES)))

    def doc(self, **changes):
        d = json.loads(self.facts_text())
        d.update(changes)
        return d

    def refused(self, doc_or_text, what):
        text = doc_or_text if isinstance(doc_or_text, str) else json.dumps(doc_or_text)
        with self.assertRaises(Ps1ObjectsError) as cm:
            parse_ps1_objects(text, self.ver, BOOT[self.ver], MODULES)
        self.assertIn(what, str(cm.exception))


class Producer(Tree):
    def test_reads_global_data_symbols_only(self):
        got = read_ps1_objects("us", MODULES)
        self.assertEqual(got["main"], [("D_one", 0x80020000, 8), ("D_one_alias", 0x80020000, 8),
                                       ("D_two", 0x80020010, 4), ("D_zero", 0x80020030, 1)])
        self.assertEqual(got["ovl"], [("D_ovl", 0x80100000, 1), ("D_ovl_b", 0x80100100, 0x20),
                                      ("D_not_c", 0x80100200, 4)])

    def test_facts_keep_only_the_checked_names(self):
        facts = ps1_objects_facts(self.data, read_ps1_objects("us", MODULES))
        self.assertEqual(facts["main"]["names"], ["D_one", "D_port_only", "D_two", "D_zero"])
        self.assertEqual([o[0] for o in facts["main"]["objects"]], ["D_one", "D_two", "D_zero"])
        self.assertEqual([o[0] for o in facts["ovl"]["objects"]], ["D_ovl", "D_ovl_b"])
        self.assertNotIn("synthFunctionNameXY", self.facts_text())

    def test_unreadable_image_stops(self):
        (self.root / "build/us/ovl.elf").unlink()
        with self.assertRaises(Ps1ObjectsError) as cm:
            read_ps1_objects("us", MODULES)
        self.assertIn("ovl.elf not read", str(cm.exception))
        (self.root / "build/us/ovl.elf").write_bytes(b"not an image")
        with self.assertRaises(Ps1ObjectsError):
            read_ps1_objects("us", MODULES)


class Schema(Tree):
    def test_round_trip_deterministic(self):
        text = self.facts_text()
        parsed = parse_ps1_objects(text, "us", BOOT["us"], MODULES)
        self.assertEqual(ps1_objects_json("us", BOOT["us"], parsed), text)
        backwards = dict(reversed(list(parsed.items())))
        self.assertEqual(ps1_objects_json("us", BOOT["us"], backwards), text)
        self.assertEqual(self.facts_text(), text)

    def test_loads_from_file_and_refuses_unreadable(self):
        p = self.root / "ps1.json"
        p.write_text(self.facts_text())
        self.assertEqual(load_ps1_objects(p, "us", BOOT["us"], MODULES),
                         parse_ps1_objects(self.facts_text(), "us", BOOT["us"], MODULES))
        with self.assertRaises(Ps1ObjectsError):
            load_ps1_objects(self.root / "missing.json", "us", BOOT["us"], MODULES)
        with self.assertRaises(Ps1ObjectsError):
            load_ps1_objects(self.root, "us", BOOT["us"], MODULES)  # a directory

    def test_refuses_malformed_and_foreign(self):
        self.refused("{", "not JSON")
        self.refused("[]", "want keys")
        self.refused(self.facts_text().replace('"region"', '"region": "us", "region"', 1), "given twice")
        self.refused(self.facts_text().replace('"names"', '"objects": [], "names"', 1), "given twice")
        self.refused(self.doc(extra=1), "want keys")
        d = self.doc()
        del d["boot"]
        self.refused(d, "want keys")
        self.refused(self.doc(format="xenogears-pc-link-facts"), "format")
        self.refused(self.doc(version="1"), "version")
        self.refused(self.doc(version=True), "version")
        self.refused(self.doc(version=2), "version")
        self.refused(self.doc(region="jp"), "region")
        self.refused(self.doc(region=["us"]), "region")
        self.refused(self.doc(boot=BOOT["jp"]), "boot file")
        self.refused(self.doc(boot=None), "boot file")
        self.refused(self.doc(modules=[]), "not an object")

    def test_refuses_wrong_modules(self):
        d = self.doc()
        del d["modules"]["ovl"]
        self.refused(d, "missing ['ovl']")
        d = self.doc()
        d["modules"]["extra"] = {"names": [], "objects": []}
        self.refused(d, "not linked ['extra']")

    def test_refuses_bad_entries(self):
        def with_main(**entry):
            d = self.doc()
            d["modules"]["main"].update(entry)
            return d
        d = self.doc()
        d["modules"]["main"]["more"] = []
        self.refused(d, "want keys")
        self.refused(self.doc(modules={"main": [], "ovl": {"names": [], "objects": []}}), "want keys")
        self.refused(with_main(names=["D_two", "D_one"]), "ascending")
        self.refused(with_main(names=["D_one", "D_one"]), "ascending")
        self.refused(with_main(names=["D_one", 7]), "ascending")
        self.refused(with_main(names=["D one"]), "ascending")
        self.refused(with_main(names="D_one"), "ascending")
        self.refused(with_main(objects={}), "not a list")
        self.refused(with_main(objects=[["D_one", 0x80020000]]), "is not [name, address, extent]")
        self.refused(with_main(objects=[["D_other", 0x80020000, 4]]), "not one of its names")
        self.refused(with_main(objects=[["D_one", 0x80020000, 4], ["D_one", 0x80020000, 4]]), "given twice")
        for a, n in ((True, 4), (0x80020000, True), (-1, 4), (0x100000000, 1), (0x80020000, 0),
                     (0xFFFFFFFF, 2), (0x80020000, 4.0), ("0x80020000", 4), (None, 4)):
            self.refused(with_main(objects=[["D_one", a, n]]), "range of 32-bit addresses")
        self.assertTrue(parse_ps1_objects(json.dumps(with_main(objects=[["D_one", 0xFFFFFFFF, 1]])), "us",
                                          BOOT["us"], MODULES))


class Consumer(Tree):
    def test_same_check_input_both_ways(self):
        meta = parse_ps1_objects(self.facts_text(), self.ver, BOOT[self.ver], MODULES)
        from_images = ps1_c_objects(self.ver, self.data)
        self.assertEqual(ps1_c_objects(self.ver, self.data, meta), from_images)
        self.assertEqual(len(from_images), 5)

    def test_other_links_names_refused(self):
        meta = parse_ps1_objects(self.facts_text(), self.ver, BOOT[self.ver], MODULES)
        data = pc_data(self.root)
        data["ovl"]["D_new"] = (data["ovl"]["D_ovl"][0], "D", 0x40)
        with self.assertRaises(Ps1ObjectsError) as cm:
            ps1_c_objects(self.ver, data, meta)
        self.assertIn("ovl: the C data names are not this link's", str(cm.exception))
        del self.data["main"]["D_port_only"]
        with self.assertRaises(Ps1ObjectsError):
            ps1_c_objects(self.ver, self.data, meta)

    def test_guard_refuses_images_and_metadata_route_tries_none(self):
        meta = parse_ps1_objects(self.facts_text(), self.ver, BOOT[self.ver], MODULES)
        expected = ps1_c_objects(self.ver, self.data)
        with ps1_elfs_closed(self.root, self.ver) as tried:
            with self.assertRaises(PermissionError):  # positive control: pc_link's own reader
                ps1_elf_objects(ps1_elf(self.ver, "main"))
            self.assertEqual(len(tried), 1)
            with contextlib.redirect_stderr(io.StringIO()) as err:
                self.assertEqual(ps1_c_objects(self.ver, self.data), [])  # the images' route is closed
            self.assertIn("not read", err.getvalue())
            self.assertEqual(len(tried), 3)
            del tried[:]
            self.assertEqual(ps1_c_objects(self.ver, self.data, meta), expected)
            self.assertEqual(tried, [])


class ConsumerJP(Consumer):
    ver = "jp"


class Cli(Tree):
    """pc_link.main on the synthetic tree (nm and the size probes stand in for the PC objects)."""

    def link(self, out: str, *options: str):
        objs = [str(self.root / f"pc/CMakeFiles/game_{m}.dir/src/{m}.c.o") for m in MODULES]

        def nm(o):
            mod = pc_link.module_of(o[0])
            return set(self.data[mod]), {}, set(), dict(self.data[mod]), {}
        argv = ["pc_link.py", "--ver", self.ver, "--game-root", str(self.root), *options, str(self.root / out), *objs]
        with mock.patch.object(pc_link, "GAME_FLAGS", list(pc_link.GAME_FLAGS)), \
                mock.patch.object(pc_link, "nm", nm), mock.patch.object(pc_link, "probe_sizes", lambda *a: {}), \
                mock.patch.object(sys, "argv", argv), contextlib.redirect_stdout(io.StringIO()), \
                contextlib.redirect_stderr(io.StringIO()):
            return pc_link.main()

    def outputs(self, out: str) -> dict[str, bytes]:
        return {p.name: p.read_bytes() for p in sorted((self.root / out).iterdir())}

    def test_metadata_link_is_the_images_link(self):
        self.assertEqual(self.link("orig"), 0)
        facts = self.root / "ps1.json"
        self.assertEqual(self.link("write", "--write-ps1-objects", str(facts)), 0)
        self.assertEqual(facts.read_text(), self.facts_text())
        with ps1_elfs_closed(self.root, self.ver) as tried:
            with self.assertRaises(PermissionError):
                ps1_elf_objects(ps1_elf(self.ver, "ovl"))
            self.assertEqual(len(tried), 1)
            del tried[:]
            self.assertEqual(self.link("meta", "--ps1-objects", str(facts)), 0)
            self.assertEqual(tried, [])
        orig = self.outputs("orig")
        self.assertIn("5 C objects checked", orig["report.txt"].decode())
        self.assertEqual(self.outputs("meta"), orig)
        self.assertEqual(self.outputs("write"), orig)

    def test_main_takes_the_file_not_the_images(self):
        # The file places D_two inside D_one; the images do not: only a link that uses the file stops.
        d = self.doc()
        d["modules"]["main"]["objects"] = [["D_one", 0x80020000, 8], ["D_two", 0x80020004, 4]]
        facts = self.root / "ps1.json"
        facts.write_text(json.dumps(d))
        self.assertEqual(self.link("orig"), 0)
        with ps1_elfs_closed(self.root, self.ver) as tried, self.assertRaises(SystemExit) as cm:
            self.link("meta", "--ps1-objects", str(facts))
        self.assertIn("two PC objects for one PS1 address", str(cm.exception))
        self.assertEqual(tried, [])

    def test_main_refuses_foreign_file_before_linking(self):
        other = "jp" if self.ver == "us" else "us"
        facts = self.root / "ps1.json"
        facts.write_text(self.facts_text(other))
        with ps1_elfs_closed(self.root, self.ver) as tried, self.assertRaises(SystemExit) as cm:
            self.link("meta", "--ps1-objects", str(facts))
        self.assertIn(f"region '{other}'", str(cm.exception))
        self.assertEqual(tried, [])
        self.assertFalse((self.root / "meta").exists())
        with self.assertRaises(SystemExit) as cm:
            self.link("meta", "--ps1-objects", str(self.root / "missing.json"))
        self.assertIn("PS1 object facts", str(cm.exception))

    def test_write_and_read_together_refused(self):
        with self.assertRaises(SystemExit) as cm:
            self.link("x", "--ps1-objects", "a.json", "--write-ps1-objects", "b.json")
        self.assertEqual(cm.exception.code, 2)

    def test_write_needs_every_image(self):
        (self.root / f"build/{self.ver}/ovl.elf").unlink()
        self.assertEqual(self.link("orig"), 0)  # the images' route skips it, as before
        with self.assertRaises(SystemExit) as cm:
            self.link("write", "--write-ps1-objects", str(self.root / "ps1.json"))
        self.assertIn("ovl.elf not read", str(cm.exception))
        self.assertFalse((self.root / "ps1.json").exists())


class CliJP(Cli):
    ver = "jp"


if __name__ == "__main__":
    unittest.main()
