#!/usr/bin/env python3
"""The PC link without the data assembly (tools/pc_link.py --asm-layout): the zero answers a
link from the assembly records, that a link from the three files alone (link facts, PS1 object
facts, assembly layout) generates exactly what the link from the assembly, the extracted image
and the PS1 build does, with all three gone from the tree, for every kind of object the link
places (zero storage, copied bytes, a pointer array, a relaid struct array, data only pointed at,
an object it cannot convert), and that a layout that cannot answer, contradicts itself, is
another region's, another PS1 split's or names a module the link has not is refused before
anything is written. Synthetic modules, symbols, assembly and images only: no PS1 build, no disc."""
import contextlib
import io
import json
import shutil
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parent))
import pc_link  # noqa: E402
from pc_link import AsmLayoutError, Image, asm_layout_check, layout_images, load_images  # noqa: E402
from test_pc_link_asm_metadata import line  # noqa: E402
from test_pc_link_ps1_metadata import BOOT, MODULES, make_tree, pc_data  # noqa: E402

BASE = 0x80060000
# name -> (offset from the data's start, the sizes the probes give)
OBJECTS = {"D_zero": (0x00, {"pc": 8, "ps1": 8}),
           "D_bytes": (0x08, {"pc": 8, "ps1": 8}),
           "D_ptrs": (0x10, {"pc": 24, "ps1": 12, "pc_elem": 8, "ps1_elem": 4}),
           "D_relaid": (0x20, {"pc": 32, "ps1": 16, "pc_elem": 16, "ps1_elem": 8}),
           "D_stuck": (0x38, {"pc": 16, "ps1": 8})}
POINTED = 0x30  # D_pointed: only D_ptrs points at it


def start(ver: str) -> int:
    return BASE + (0 if ver == "us" else 4)


def delta(ver: str) -> int:
    return (0x800 if ver == "us" else 0x900) - 0x80010000


def write_tree(root: Path, ver: str) -> None:
    """main's data: every kind of object the link places, then the extracted image holding the
    same bytes at the offsets the assembly gives."""
    a, d = start(ver), delta(ver)
    asm = root / "asm" / ver / "main" / "data"
    asm.mkdir(parents=True)
    asm.joinpath("a.s").write_text(
        ".section .data\n\ndlabel D_zero\n" + line(a, d, ".space 0x8")
        + "dlabel D_bytes\n" + line(a + 0x08, d, ".word 0x11223344") + line(a + 0x0C, d, ".word 0x0")
        + "dlabel D_ptrs\n" + line(a + 0x10, d, ".word D_bytes") + line(a + 0x14, d, ".word 0x0")
        + line(a + 0x18, d, ".word D_pointed + 0x4") + line(a + 0x1C, d, ".word 0x0")
        + "dlabel D_relaid\n" + line(a + 0x20, d, ".word D_zero") + line(a + 0x24, d, ".word 0x7")
        + line(a + 0x28, d, ".word 0x0") + line(a + 0x2C, d, ".word 0x9")
        + "dlabel D_pointed\n" + line(a + 0x30, d, ".word 0x55667788") + line(a + 0x34, d, ".word 0x0")
        + "dlabel D_stuck\n" + line(a + 0x38, d, ".word 0x1") + line(a + 0x3C, d, ".word 0x2"))
    img = load_images(ver)[0]["main"]
    file = bytearray(max(img.mem) + d + 0x11)
    for at, b in img.mem.items():
        file[at + d] = b
    (root / "disc" / ver).mkdir(parents=True)
    (root / "disc" / ver / "main.exe").write_bytes(bytes(file))
    (root / "disc" / ver / "ovl.bin").write_bytes(bytes(0x20))


def asked(ver: str) -> dict[str, list[list[int]]]:
    """What the link asks of these bytes, as the layout lists it."""
    a = start(ver)
    return {"zero": [[a, 8], [a + 0x14, 4], [a + 0x28, 4]],
            "nonzero": [[a + 0x08, 8], [a + 0x24, 4], [a + 0x30, 8], [a + 0x38, 4], [a + 0x38, 8]]}


class Tree(unittest.TestCase):
    ver = "us"

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name)
        make_tree(self.root, self.ver)
        self.patch = mock.patch.object(pc_link, "ROOT", self.root)
        self.patch.start()
        write_tree(self.root, self.ver)
        self.data = pc_data(self.root)
        self.facts, self.ps1, self.layout = (self.root / n for n in ("facts.json", "ps1.json", "layout.json"))

    def tearDown(self):
        self.patch.stop()
        self.tmp.cleanup()

    def link(self, out: str, *options: str):
        objs = [str(self.root / f"pc/CMakeFiles/game_{m}.dir/src/{m}.c.o") for m in MODULES]

        def nm(o):
            mod = pc_link.module_of(o[0])
            undefined = {n: [o[0]] for n in OBJECTS} if mod == "main" else {}
            return set(self.data[mod]), undefined, set(), dict(self.data[mod]), dict(undefined)
        argv = ["pc_link.py", "--ver", self.ver, "--game-root", str(self.root), *options, str(self.root / out), *objs]
        with mock.patch.object(pc_link, "GAME_FLAGS", list(pc_link.GAME_FLAGS)), \
                mock.patch.object(pc_link, "nm", nm), \
                mock.patch.object(pc_link, "probe_sizes", lambda *a: {n: dict(s) for n, (_, s) in OBJECTS.items()}), \
                mock.patch.object(pc_link, "stale_objects", lambda *a, **k: []), \
                mock.patch.object(sys, "argv", argv), contextlib.redirect_stdout(io.StringIO()), \
                contextlib.redirect_stderr(io.StringIO()):
            return pc_link.main()

    def outputs(self, out: str) -> dict[str, bytes]:
        return {p.name: p.read_bytes() for p in sorted((self.root / out).iterdir())}

    def write(self) -> dict:
        """The three files from a link that has the assembly, the image and the PS1 build."""
        self.assertEqual(self.link("write", "--write-facts", str(self.facts), "--write-ps1-objects", str(self.ps1),
                                   "--write-asm-layout", str(self.layout)), 0)
        return json.loads(self.layout.read_text())

    def strip(self) -> None:
        """A public checkout: no data assembly, no extracted image, no PS1 build."""
        for d in ("asm", "disc", "build"):
            shutil.rmtree(self.root / d)

    def free(self, out: str, layout: Path | None = None, *more: str):
        return self.link(out, "--facts", str(self.facts), "--ps1-objects", str(self.ps1),
                         "--asm-layout", str(layout or self.layout), *more)

    def changed(self, doc: dict, change) -> Path:
        d = json.loads(json.dumps(doc))
        change(d)
        path = self.root / "changed.json"
        path.write_text(json.dumps(d))
        return path


def no_assembly(*a):
    raise AssertionError("the data assembly was read on the --asm-layout path")


class Writer(Tree):
    def test_every_kind_is_placed(self):
        self.assertEqual(self.link("orig"), 0)
        report = (self.root / "orig" / "report.txt").read_text()
        self.assertIn("bytes 1, zero 1, pointers 1, relaid 1,", report)
        self.assertIn("pointed-at 1,", report)
        self.assertIn("unconverted 1,", report)
        main = (self.root / "orig" / "main.c").read_text()
        for decl in ("unsigned char D_zero[8];", "void* D_ptrs[3] = {", "unsigned long long D_relaid[4] = {",
                     "unsigned char D_pointed[8]; /* pointed at only */", "unsigned char D_stuck[16];"):
            self.assertIn(decl, main)

    def test_answers_are_the_questions_asked(self):
        doc = self.write()
        self.assertEqual(doc["version"], 2)
        self.assertEqual({k: doc["modules"]["main"][k] for k in ("zero", "nonzero")}, asked(self.ver))
        self.assertEqual(list(doc["modules"]), ["main"])  # ovl has no data assembly
        self.assertEqual(self.outputs("write"), self.link("orig") or self.outputs("orig"))

    def test_no_bytes_in_the_layout(self):
        self.write()
        text = self.layout.read_text()
        for value in ("11223344", "55667788", str(0x11223344), str(0x55667788), str(self.root)):
            self.assertNotIn(value, text.lower())


class WriterJP(Writer):
    ver = "jp"


class Reader(Tree):
    def test_same_outputs_with_nothing_from_the_disc(self):
        self.assertEqual(self.link("orig"), 0)
        self.write()
        self.strip()
        again = self.root / "again.json"
        with mock.patch.object(pc_link, "load_images", no_assembly), \
                mock.patch.object(pc_link, "scan_data_labels", no_assembly), \
                mock.patch.object(pc_link, "read_ps1_objects", no_assembly):
            self.assertEqual(self.free("free"), 0)
            self.assertEqual(self.free("both", None, "--write-asm-layout", str(again)), 0)
        self.assertEqual(self.outputs("free"), self.outputs("orig"))
        self.assertEqual(self.outputs("both"), self.outputs("orig"))
        self.assertEqual(again.read_bytes(), self.layout.read_bytes())  # the same questions, the same answers
        self.assertFalse((self.root / "asm").exists())

    def test_without_the_ps1_object_facts(self):
        """Tables made where no PS1 build stood: the same files but the report, and no image opened."""
        self.write()
        self.strip()

        def no_image(*a):
            raise AssertionError("a PS1 image was looked for on the --asm-layout path")
        self.assertEqual(self.free("with"), 0)
        with mock.patch.object(pc_link, "ps1_elf_objects", no_image), mock.patch.object(pc_link, "ps1_elf", no_image):
            self.assertEqual(self.link("bare", "--facts", str(self.facts), "--asm-layout", str(self.layout)), 0)
        full, bare = self.outputs("with"), self.outputs("bare")
        self.assertIn(" 0 C objects checked", bare["report.txt"].decode())
        self.assertNotIn(" 0 C objects checked", full["report.txt"].decode())
        del full["report.txt"], bare["report.txt"]
        self.assertEqual(bare, full)

    def test_needs_the_facts(self):
        self.write()
        for options in (["--asm-layout", str(self.layout)],
                        ["--asm-layout", str(self.layout), "--ps1-objects", str(self.ps1)],
                        ["--asm-layout", str(self.layout), "--asm-labels", str(self.layout),
                         "--facts", str(self.facts), "--ps1-objects", str(self.ps1)]):
            with self.subTest(options=options[2:]), self.assertRaises(SystemExit) as cm:
                self.link("out", *options)
            self.assertEqual(cm.exception.code, 2)
            self.assertFalse((self.root / "out").exists())

    def test_answers_reach_inside_and_around(self):
        doc = self.write()
        self.assertEqual(self.link("orig"), 0)
        a = start(self.ver)

        def wider(d):  # the whole of D_stuck follows from its first word
            d["modules"]["main"]["nonzero"].remove([a + 0x38, 8])
            d["modules"]["main"]["zero"].remove([a + 0x14, 4])
            d["modules"]["main"]["zero"].remove([a + 0x28, 4])
            d["modules"]["main"]["zero"] = sorted(d["modules"]["main"]["zero"] + [[a + 0x14, 4 + 4 + 4], [a + 0x28, 4]])
        self.assertEqual(self.free("free", self.changed(doc, wider)), 0)
        self.assertEqual(self.outputs("free"), self.outputs("orig"))

    def test_refused_before_output(self):
        doc = self.write()
        a = start(self.ver)
        other = "jp" if self.ver == "us" else "us"
        main = lambda d: d["modules"]["main"]  # noqa: E731
        cases = [
            ("no answer", lambda d: main(d)["zero"].remove([a, 8]), "does not say whether"),
            ("no answer for a slot", lambda d: main(d)["zero"].remove([a + 0x28, 4]), "does not say whether"),
            ("contradiction", lambda d: main(d)["nonzero"].insert(0, [a + 4, 4]), "not zero inside"),
            ("answer off the data", lambda d: main(d)["zero"].append([a + 0x40, 4]), "zero answers"),
            ("other region", lambda d: d.update(region=other), "region"),
            ("other boot file", lambda d: d.update(boot=BOOT[other]), "region"),
            ("old version", lambda d: d.update(version=1), "format"),
            ("a module the link has not", lambda d: d["modules"].update(ghost=dict(main(d))), "which this link has not"),
            ("no modules", lambda d: d.update(modules={}), "modules"),
            ("another split's labels", lambda d: main(d)["labels"][1].__setitem__(1, a + 0x0C), "link facts"),
            ("another split's pointers", lambda d: main(d)["refs"].pop(), "does not say whether"),
        ]
        for what, change, message in cases:
            out = "out-" + what.replace(" ", "-").replace("'", "")
            with self.subTest(what=what), self.assertRaises(SystemExit) as cm:
                self.free(out, self.changed(doc, change))
            self.assertIn(message, str(cm.exception), what)
            self.assertFalse((self.root / out).exists(), what)
        for what, text in (("missing", None), ("not json", "{"), ("duplicate key", json.dumps(doc)[:-1] + ', "region": "us"}')):
            path = self.root / "bad.json"
            path.unlink(missing_ok=True)
            if text is not None:
                path.write_text(text)
            with self.subTest(what=what), self.assertRaises(SystemExit) as cm:
                self.free("bad", path)
            self.assertIn("not readable", str(cm.exception))
            self.assertFalse((self.root / "bad").exists())


class ReaderJP(Reader):
    ver = "jp"


class Answers(unittest.TestCase):
    """Image.nonzero and Image.filled on their own."""

    def image(self, answers=None) -> Image:
        img = Image("m", answers=answers)
        for a in list(range(0x100, 0x110)) + list(range(0x120, 0x128)):
            img.mem[a] = 0
        if answers is None:
            img.mem[0x104] = 9
            img.mem[0x121] = 1
        return img

    def test_filled_cuts_to_the_data(self):
        img = self.image()
        self.assertEqual(img.spans(), [[0x100, 0x110], [0x120, 0x128]])
        self.assertEqual(img.filled(0x100, 0x10), (0x100, 0x10))
        self.assertEqual(img.filled(0xF0, 0x40), (0x100, 0x28))
        self.assertEqual(img.filled(0x10C, 0x18), (0x10C, 0x18))  # the gap between the two is zero
        self.assertEqual(img.filled(0x108, 0x10), (0x108, 8))
        self.assertEqual(img.filled(0x118, 0x0C), (0x120, 4))
        for addr, size in ((0xF0, 0x10), (0x110, 0x10), (0x128, 8), (0x100, 0)):
            self.assertIsNone(img.filled(addr, size))

    def test_bytes_answer_and_are_recorded(self):
        img = self.image()
        self.assertTrue(img.nonzero(0x100, 8))
        self.assertFalse(img.nonzero(0x108, 0x10))  # past the data: zero there
        self.assertFalse(img.nonzero(0x110, 0x10))  # no data at all: not a question
        self.assertTrue(img.nonzero(0x118, 0x10))
        self.assertEqual(img.asked, {(0x100, 8): True, (0x108, 8): False, (0x120, 8): True})

    def test_a_layout_answers_what_it_holds(self):
        img = self.image({(0x100, 4): False, (0x108, 8): False, (0x104, 1): True})
        self.assertFalse(img.nonzero(0x100, 4))
        self.assertFalse(img.nonzero(0x10A, 2))      # inside a zero range
        self.assertFalse(img.nonzero(0x10C, 0x10))   # cut to the data, inside a zero range
        self.assertTrue(img.nonzero(0x100, 0x10))    # around a range that is not zero
        self.assertFalse(img.nonzero(0x130, 4))      # no data
        for addr, size in ((0x105, 2), (0x120, 4), (0x105, 3)):
            with self.subTest(addr=addr), self.assertRaises(AsmLayoutError) as cm:
                img.nonzero(addr, size)
            self.assertIn("does not say whether", str(cm.exception))
        with self.assertRaises(AsmLayoutError):
            img.get(0x100, 4)  # a layout has no bytes

    def test_layout_images(self):
        doc = {"format": pc_link.ASM_LAYOUT_FORMAT, "version": pc_link.ASM_LAYOUT_VERSION, "region": "us", "boot": "B",
               "modules": {"m": {"deltas": [-0x10], "spans": [[0x100, 0x110], [0x120, 0x128]], "ends": [0x128],
                                 "refs": [[0x108, "D_x", 4]], "labels": [["D_a", 0x100]],
                                 "zero": [[0x100, 4]], "nonzero": [[0x104, 4]]}}}
        asm_layout_check(doc, "us", "B", ["m"])
        img = layout_images(doc)["m"]
        self.assertEqual(sorted(img.mem), list(range(0x100, 0x110)) + list(range(0x120, 0x128)))
        self.assertEqual((img.deltas, img.refs, img.ends), ({-0x10}, {0x108: ("D_x", 4)}, {0x128}))
        self.assertEqual(img.answers, {(0x100, 4): False, (0x104, 4): True})
        self.assertEqual(pc_link.layout_labels(doc), {"D_a": {"m": 0x100}})


if __name__ == "__main__":
    unittest.main()
