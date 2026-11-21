#!/usr/bin/env python3
"""Gathering builds' link tables (tools/link_tables.py): one layout per region with every build's
zero answers, each configuration's own files, the retail-byte scan before anything is written,
and what is refused (a build without tables, two builds of one configuration, layouts of
different PS1 splits, answers that contradict, a table holding retail bytes). Synthetic regions
and tables only: no disc, no retail bytes."""
import contextlib
import io
import json
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parent))
import link_tables  # noqa: E402
import pc_link  # noqa: E402
from test_check_link_tables import Region  # noqa: E402

A = 0x80020000


def layout(ver: str, zero=(), nonzero=()) -> dict:
    return {"format": pc_link.ASM_LAYOUT_FORMAT, "version": pc_link.ASM_LAYOUT_VERSION, "region": ver,
            "boot": f"SYNTH_{ver.upper()}.00",
            "modules": {"main": {"deltas": [-0x7FFF0000], "spans": [[A, A + 0x40]], "ends": [], "refs": [[A + 0x10, "D_x", 0]],
                                 "labels": [["D_a", A], ["D_x", A + 0x20]],
                                 "zero": sorted(map(list, zero)), "nonzero": sorted(map(list, nonzero))}}}


class Gather(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.dir = Path(self.tmp.name)
        self.root = self.dir / "ref"
        self.us = Region(self.root, "us", 100)
        self.jp = Region(self.root, "jp", 200)
        self.dest = self.dir / "checkout" / "port" / "link-tables"
        self.dest.mkdir(parents=True)
        self.patch = mock.patch.object(pc_link, "ROOT", self.root)
        self.patch.start()

    def tearDown(self):
        self.patch.stop()
        self.tmp.cleanup()

    def build(self, name: str, ver: str, config: str, doc: dict, facts: bytes = b'{"facts": 1}\n') -> Path:
        build = self.dir / name
        tables = build / "link-tables"
        (tables / "srcdata").mkdir(parents=True)
        (tables / "config.txt").write_text(f"{ver} {config}\n")
        (tables / "facts.json").write_bytes(facts)
        (tables / "ps1-objects.json").write_text('{"ps1": 1}\n')
        (tables / "asm-layout.json").write_text(json.dumps(doc))
        for module in ("main", "ovl"):
            (tables / "srcdata" / f"{module}.json").write_text(json.dumps(
                {"module": module, "objects": [{"name": "a.c.o", "sections": []}, {"name": "b.c.o", "sections": []}]}, indent=1))
        return build

    def files(self) -> list[str]:
        return sorted(str(p.relative_to(self.dest)) for p in self.dest.rglob("*") if p.is_file())

    def test_one_layout_per_region_and_each_builds_own_files(self):
        builds = [self.build("b1", "us", "original-linux-x86_64", layout("us", [(A, 8)], [(A + 0x20, 4)])),
                  self.build("b2", "us", "enhanced-linux-x86_64", layout("us", [(A, 8), (A + 0x30, 4)], [(A + 0x24, 4)])),
                  self.build("b3", "jp", "original-linux-x86_64", layout("jp", [(A, 4)]))]
        report = link_tables.gather(builds, self.dest, False)
        per = ["facts.json", "ps1-objects.json", "srcdata/main.json", "srcdata/ovl.json"]
        self.assertEqual(self.files(), sorted(
            ["us/asm-layout.json", "jp/asm-layout.json"]
            + [f"us/{c}/{f}" for c in ("original-linux-x86_64", "enhanced-linux-x86_64") for f in per]
            + [f"jp/original-linux-x86_64/{f}" for f in per]))
        main = json.loads((self.dest / "us" / "asm-layout.json").read_text())["modules"]["main"]
        self.assertEqual(main["zero"], [[A, 8], [A + 0x30, 4]])
        self.assertEqual(main["nonzero"], [[A + 0x20, 4], [A + 0x24, 4]])
        self.assertEqual(json.loads((self.dest / "jp" / "asm-layout.json").read_text())["modules"]["main"]["zero"], [[A, 4]])
        plan = (self.dest / "us" / "original-linux-x86_64" / "srcdata" / "main.json").read_text()
        self.assertEqual(plan.count("\n"), 2)  # one line per object
        self.assertEqual(json.loads(plan)["module"], "main")
        self.assertIn("us: asm-layout.json from 2 layouts, 4 answers", report)
        self.assertTrue(any("no run of 16 retail bytes" in line for line in report))

    def test_optional_files_go_where_a_build_has_them(self):
        """A release builder's tables: no PS1 object facts, the compiler and the executable recorded."""
        release = self.build("b1", "us", "original-linux-x86_64", layout("us", [(A, 8)]))
        (release / "link-tables" / "ps1-objects.json").unlink()
        (release / "link-tables" / "compiler.txt").write_text("GNU 15.2.0\n")
        (release / "link-tables" / "executable.json").write_text('{"sha256": "00"}\n')
        plain = self.build("b2", "us", "enhanced-linux-x86_64", layout("us", [(A, 8)]))
        report = link_tables.gather([release, plain], self.dest, False)
        self.assertEqual([f for f in self.files() if "srcdata" not in f], [
            "us/asm-layout.json", "us/enhanced-linux-x86_64/facts.json", "us/enhanced-linux-x86_64/ps1-objects.json",
            "us/original-linux-x86_64/compiler.txt", "us/original-linux-x86_64/executable.json",
            "us/original-linux-x86_64/facts.json"])
        self.assertIn("us original-linux-x86_64: facts, 2 source data plans, compiler.txt, executable.json", report)
        self.assertIn("us enhanced-linux-x86_64: facts, 2 source data plans, ps1-objects.json", report)

    def test_keep_adds_to_the_layout_in_place(self):
        link_tables.gather([self.build("b1", "us", "original-linux-x86_64", layout("us", [(A, 8)]))], self.dest, False)
        other = [self.build("b2", "us", "original-windows-x86_64", layout("us", [(A + 0x30, 4)]))]
        link_tables.gather(other, self.dest, True)
        zero = lambda: json.loads((self.dest / "us" / "asm-layout.json").read_text())["modules"]["main"]["zero"]  # noqa: E731
        self.assertEqual(zero(), [[A, 8], [A + 0x30, 4]])
        self.assertTrue((self.dest / "us" / "original-linux-x86_64" / "facts.json").is_file())  # left as it was
        link_tables.gather(other, self.dest, False)
        self.assertEqual(zero(), [[A + 0x30, 4]])

    def test_refused_and_nothing_written(self):
        good = self.build("good", "us", "original-linux-x86_64", layout("us", [(A, 8)]))
        retail = self.build("retail", "us", "enhanced-linux-x86_64", layout("us"), b'{"x": "' + self.us.main[:24] + b'"}')
        other_split = layout("us")
        other_split["modules"]["main"]["spans"] = [[A, A + 0x44]]
        untouched = self.dir / "untouched"
        untouched.mkdir()
        unlinked = self.build("unlinked", "us", "enhanced-linux-x86_64", layout("us"))
        (unlinked / "link-tables" / "facts.json").unlink()
        no_plans = self.build("noplans", "us", "enhanced-linux-x86_64", layout("us"))
        for plan in (no_plans / "link-tables" / "srcdata").iterdir():
            plan.unlink()
        cases = [
            ("retail bytes", [good, retail], link_tables.TablesError, "retail bytes in us/enhanced-linux-x86_64/facts.json"),
            ("no tables", [good, untouched], link_tables.TablesError, "XG_WRITE_LINK_TABLES"),
            ("not linked", [good, unlinked], link_tables.TablesError, "facts.json is missing"),
            ("no plans", [good, no_plans], link_tables.TablesError, "no source data plans"),
            ("one configuration twice", [good, self.build("twice", "us", "original-linux-x86_64", layout("us"))],
             link_tables.TablesError, "are both us original-linux-x86_64"),
            ("another split", [good, self.build("split", "us", "enhanced-linux-x86_64", other_split)],
             pc_link.AsmLayoutError, "spans differ between the layouts"),
            ("contradiction", [good, self.build("contra", "us", "enhanced-linux-x86_64", layout("us", (), [(A + 4, 4)]))],
             pc_link.AsmLayoutError, "not zero inside"),
            ("another region's layout", [self.build("wrong", "us", "enhanced-linux-x86_64", layout("jp"))],
             pc_link.AsmLayoutError, "region"),
        ]
        for what, builds, error, message in cases:
            with self.subTest(what=what), self.assertRaises(error) as cm:
                link_tables.gather(builds, self.dest, False)
            self.assertIn(message, str(cm.exception), what)
            self.assertEqual(self.files(), [], what)
            self.assertEqual([p.name for p in self.dest.parent.iterdir()], ["link-tables"], what)  # no scratch left

    def test_cli(self):
        build = self.build("b1", "us", "original-linux-x86_64", layout("us", [(A, 8)]))
        out, err = io.StringIO(), io.StringIO()
        with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
            self.assertEqual(link_tables.main([str(build), "--dest", str(self.dest)]), 0)
            self.assertEqual(link_tables.main([str(self.dir / "untouched"), "--dest", str(self.dest)]), 1)
        self.assertIn("no run of 16 retail bytes", out.getvalue())
        self.assertIn("no link tables", err.getvalue())


if __name__ == "__main__":
    unittest.main()
