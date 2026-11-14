#!/usr/bin/env python3
"""The PC link's assembly layout (tools/pc_link.py --write-asm-layout): what the producer takes
from synthetic data assembly in both regions (separated spans, blob ends, symbolic and blob
pointer slots, data labels in order), that the file is deterministic and carries no byte, string
or path, that a link writing it generates exactly what a link without it does, that a malformed
layout is refused before anything is written, and that a failed link writes none. Synthetic
modules, symbols, assembly and images only: no PS1 build, no disc."""
import contextlib
import io
import json
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parent))
import pc_link  # noqa: E402
from pc_link import (AsmLayoutError, asm_layout_check, asm_layout_facts, asm_layout_json,  # noqa: E402
                     load_images, ps1_objects_facts, ps1_objects_json, read_ps1_objects)
from test_pc_link_ps1_metadata import BOOT, MODULES, make_tree, pc_data  # noqa: E402

SECRETS = ["SECRETTEX", "BLOBTEXT", "deadbeef", "DEADBEEF", str(0xDEADBEEF), "nonmatchings", ".s\"", "/data/"]


def line(a: int, delta: int, text: str) -> str:
    return f"    /* {a + delta:X} {a:08X} */ {text}\n"


def write_asm(root: Path, ver: str) -> None:
    """main: two data files apart (a symbolic pointer slot, a string, a number); ovl: a word
    pointing by number into an INCLUDE_RODATA blob. JP one word further on, with other offsets."""
    d = 0 if ver == "us" else 4
    md, od = (0x800 - 0x80010000, 0x2000 - 0x80100000) if ver == "us" else (0x900 - 0x80010000, 0x2100 - 0x80100000)
    asm = root / "asm" / ver
    for p in ("main/data", "ovl/data", "ovl/nonmatchings/func_synth"):
        (asm / p).mkdir(parents=True)
    (asm / "main/data/a.s").write_text(
        ".section .data\n\ndlabel D_asm_head\n" + line(0x80020000 + d, md, ".word D_asm_tail + 0x4")
        + line(0x80020004 + d, md, '.asciz "SECRETTEX"') + line(0x8002000E + d, md, ".space 0x2")
        + "\ndlabel D_asm_numeric\n" + line(0x80020010 + d, md, ".word 0xDEADBEEF"))
    (asm / "main/data/b.s").write_text(".section .bss\n\ndlabel D_asm_tail\n" + line(0x80030000 + d, md, ".space 0x8"))
    (asm / "ovl/data/c.s").write_text("dlabel D_ovl_tab\n" + line(0x80100400 + d, od, f".word 0x{0x80100804 + d:08X}"))
    (asm / "ovl/nonmatchings/func_synth/blob.s").write_text(
        ".section .rodata\n\ndlabel D_blob\n" + line(0x80100800 + d, od, '.asciz "BLOBTEXT"')
        + line(0x80100809 + d, od, ".byte 0x1") + "enddlabel D_blob\n")


def expected(ver: str) -> dict:
    d = 0 if ver == "us" else 4
    md, od = (0x800 - 0x80010000, 0x2000 - 0x80100000) if ver == "us" else (0x900 - 0x80010000, 0x2100 - 0x80100000)
    return {"main": {"deltas": [md], "spans": [[0x80020000 + d, 0x80020014 + d], [0x80030000 + d, 0x80030008 + d]],
                     "ends": [], "refs": [[0x80020000 + d, "D_asm_tail", 4]],
                     "labels": [["D_asm_head", 0x80020000 + d], ["D_asm_numeric", 0x80020010 + d],
                                ["D_asm_tail", 0x80030000 + d]]},
            "ovl": {"deltas": [od], "spans": [[0x80100400 + d, 0x80100404 + d], [0x80100800 + d, 0x8010080A + d]],
                    "ends": [0x8010080A + d], "refs": [[0x80100400 + d, "D_blob", 4]],
                    "labels": [["D_ovl_tab", 0x80100400 + d], ["D_blob", 0x80100800 + d]]}}


class Tree(unittest.TestCase):
    ver = "us"

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name)
        make_tree(self.root, self.ver)
        write_asm(self.root, self.ver)
        self.data = pc_data(self.root)
        self.patch = mock.patch.object(pc_link, "ROOT", self.root)
        self.patch.start()

    def tearDown(self):
        self.patch.stop()
        self.tmp.cleanup()

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

    def doc(self) -> dict:
        return json.loads(asm_layout_json(self.ver, BOOT[self.ver], expected(self.ver), MODULES))

    def refused(self, doc, what: str = "assembly layout"):
        with self.assertRaises(AsmLayoutError) as cm:
            asm_layout_check(doc, self.ver, BOOT[self.ver], MODULES)
        self.assertIn(what, str(cm.exception))


class Producer(Tree):
    def test_facts_from_the_assembly(self):
        images, _ = load_images(self.ver)
        labels = {}
        for n, per in [("D_asm_head", "main"), ("D_asm_numeric", "main"), ("D_asm_tail", "main"),
                       ("D_ovl_tab", "ovl"), ("D_blob", "ovl")]:
            labels[n] = {per: dict(expected(self.ver)[per]["labels"])[n]}
        self.assertEqual(asm_layout_facts(images, labels), expected(self.ver))

    def test_well_formed_passes(self):
        asm_layout_check(self.doc(), self.ver, BOOT[self.ver], MODULES)

    def test_refuses_malformed(self):
        other = "jp" if self.ver == "us" else "us"
        for change, what in [
                (lambda d: d.pop("boot"), "want keys"), (lambda d: d.update(extra=1), "want keys"),
                (lambda d: d.update(format="other"), "format"), (lambda d: d.update(version=2), "format"),
                (lambda d: d.update(version=True), "format"), (lambda d: d.update(region=other), "region"),
                (lambda d: d.update(boot="OTHER.00"), "region"), (lambda d: d["modules"].pop("ovl"), "modules"),
                (lambda d: d["modules"].update(extra=d["modules"]["main"]), "modules"),
                (lambda d: d.update(modules=[]), "modules"),
                (lambda d: d["modules"]["main"].pop("refs"), "want keys"),
                (lambda d: d["modules"]["main"].update(bytes=[1]), "want keys"),
                (lambda d: d["modules"]["main"].update(spans={}), "not a list"),
                (lambda d: d["modules"]["main"].update(deltas=[]), "deltas"),
                (lambda d: d["modules"]["main"].update(deltas=[1, 0]), "deltas"),
                (lambda d: d["modules"]["main"].update(deltas=["0x800"]), "deltas"),
                (lambda d: d["modules"]["main"]["spans"].reverse(), "spans"),
                (lambda d: d["modules"]["main"]["spans"][0].__setitem__(1, d["modules"]["main"]["spans"][1][0]), "spans"),
                (lambda d: d["modules"]["main"]["spans"][0].__setitem__(1, d["modules"]["main"]["spans"][0][0]), "spans"),
                (lambda d: d["modules"]["main"]["spans"].append([0xFFFFFFFF, 0x100000001]), "spans"),
                (lambda d: d["modules"]["main"]["spans"][0].append(0), "spans"),
                (lambda d: d["modules"]["ovl"]["ends"].append(0x80200000), "blob ends"),
                (lambda d: d["modules"]["ovl"]["ends"].insert(0, d["modules"]["ovl"]["ends"][0]), "blob ends"),
                (lambda d: d["modules"]["main"]["refs"][0].__setitem__(0, 0x80040000), "refs"),
                (lambda d: d["modules"]["main"]["refs"][0].__setitem__(1, "D x"), "refs"),
                (lambda d: d["modules"]["main"]["refs"][0].__setitem__(2, "4"), "refs"),
                (lambda d: d["modules"]["main"]["refs"].append(list(d["modules"]["main"]["refs"][0])), "refs"),
                (lambda d: d["modules"]["main"]["labels"].append(["D_asm_head", 0x80020000]), "labels"),
                (lambda d: d["modules"]["main"]["labels"][0].__setitem__(1, -1), "labels"),
                (lambda d: d["modules"]["main"]["labels"][0].__setitem__(0, "AB\"CD"), "labels"),
                (lambda d: d["modules"]["main"]["labels"].append("D_x"), "labels")]:
            d = self.doc()
            change(d)
            with self.subTest(what=what):
                self.refused(d, what)
        bad = expected(self.ver)
        bad["main"]["spans"].insert(0, [0, 0])
        with self.assertRaises(AsmLayoutError):
            asm_layout_json(self.ver, BOOT[self.ver], bad, MODULES)


class ProducerJP(Producer):
    ver = "jp"


class Cli(Tree):
    """pc_link.main on the synthetic tree (nm and the size probes stand in for the PC objects)."""

    def test_writer_output_is_the_default_output(self):
        layout, again = self.root / "layout.json", self.root / "again.json"
        self.assertEqual(self.link("orig"), 0)
        self.assertEqual(self.link("write", "--write-asm-layout", str(layout)), 0)
        self.assertEqual(self.link("again", "--write-asm-layout", str(again)), 0)
        self.assertEqual(self.outputs("write"), self.outputs("orig"))
        self.assertEqual(layout.read_bytes(), again.read_bytes())
        doc = json.loads(layout.read_text())
        self.assertEqual(doc, {"format": "xenogears-pc-link-asm-layout", "version": 1, "region": self.ver,
                               "boot": BOOT[self.ver], "modules": expected(self.ver)})
        self.assertNotIn("layout.json", self.outputs("write"))

    def test_no_bytes_text_or_paths(self):
        layout = self.root / "layout.json"
        self.assertEqual(self.link("write", "--write-asm-layout", str(layout)), 0)
        text = layout.read_text()
        for s in SECRETS + [str(self.root), self.tmp.name]:
            self.assertNotIn(s, text)

    def test_default_writes_no_layout(self):
        self.assertEqual(self.link("orig"), 0)
        self.assertEqual(sorted(p.name for p in self.root.iterdir()),
                         sorted(["asm", "build", "config", "include", "orig"]))

    def test_malformed_producer_output_refused_and_not_written(self):
        layout = self.root / "layout.json"
        real = pc_link.asm_layout_facts

        def broken(images, labels):
            out = real(images, labels)
            out["main"]["spans"].reverse()
            return out
        with mock.patch.object(pc_link, "asm_layout_facts", broken), self.assertRaises(SystemExit) as cm:
            self.link("write", "--write-asm-layout", str(layout))
        self.assertIn("assembly layout", str(cm.exception))
        self.assertFalse(layout.exists())
        with mock.patch.object(pc_link, "asm_layout_facts", lambda i, l: {"main": real(i, l)["main"]}), \
                self.assertRaises(SystemExit) as cm:
            self.link("write", "--write-asm-layout", str(layout))
        self.assertIn("modules", str(cm.exception))
        self.assertFalse(layout.exists())

    def test_failed_link_writes_no_layout(self):
        # PS1 object facts placing D_two inside D_one: the link stops at its one-object check.
        d = json.loads(ps1_objects_json(self.ver, BOOT[self.ver],
                                        ps1_objects_facts(self.data, read_ps1_objects(self.ver, MODULES))))
        d["modules"]["main"]["objects"] = [["D_one", 0x80020000, 8], ["D_two", 0x80020004, 4]]
        facts, layout = self.root / "ps1.json", self.root / "layout.json"
        facts.write_text(json.dumps(d))
        with self.assertRaises(SystemExit) as cm:
            self.link("write", "--ps1-objects", str(facts), "--write-asm-layout", str(layout))
        self.assertIn("two PC objects for one PS1 address", str(cm.exception))
        self.assertFalse(layout.exists())


class CliJP(Cli):
    ver = "jp"


if __name__ == "__main__":
    unittest.main()
