#!/usr/bin/env python3
"""Ownership checks for platform references to generated module data, the
copy lists that fill the generated data from the player's disc, the layout
probes, and the one-object-per-PS1-address check."""
import shutil
import tempfile
import unittest
from pathlib import Path

import subprocess

from pc_link import (SEPARATE, Image, ProbeError, alias_declarations, coresident, copy_runs, data_residues,
                     export_platform_symbols, fnv1a64, overlapping_storage, pointers_outside, probe_flags,
                     probe_sizes, relayout)


class PlatformExports(unittest.TestCase):
    def test_compiled_and_generated(self):
        exports = {"main": {"existing"}, "video_player": set()}
        export_platform_symbols(exports, {"kernelMain", "D_player", "hostFn"},
                                {"main": {"kernelMain"}}, {"video_player": {"D_player"}})
        self.assertEqual(exports, {"main": {"existing", "kernelMain"}, "video_player": {"D_player"}})

    def test_same_owner_common_and_generated(self):
        exports = {"main": set()}
        export_platform_symbols(exports, {"data"}, {"main": {"data"}}, {"main": {"data"}})
        self.assertEqual(exports["main"], {"data"})

    def test_generated_alias_and_initializer_target(self):
        exports = {"main": set(), "overlay": set()}
        export_platform_symbols(exports, {"alias", "target"}, {}, {"overlay": {"alias", "target"}})
        self.assertEqual(exports["overlay"], {"alias", "target"})

    def test_ambiguous_generated(self):
        with self.assertRaisesRegex(ValueError, "ambiguous game symbol data"):
            export_platform_symbols({"a": set(), "b": set()}, {"data"}, {},
                                    {"a": {"data"}, "b": {"data"}})

    def test_compiled_generated_collision(self):
        with self.assertRaisesRegex(ValueError, "ambiguous game symbol data"):
            export_platform_symbols({"a": set(), "b": set()}, {"data"},
                                    {"a": {"data"}}, {"b": {"data"}})


class CopyLists(unittest.TestCase):
    def image(self, start, data):
        img = Image("m")
        for i, b in enumerate(data):
            img.mem[start + i] = b
        return img

    def test_runs_merge_and_skip(self):
        img = self.image(0x100, b"abcdef")
        pairs = [(i, 0x100 + i) for i in range(4)] + [(4, None), (5, 0x104), (6, 0x105), (7, 0x200)]
        self.assertEqual(copy_runs(pairs, img), [(0, 0x100, 4), (5, 0x104, 2)])

    def test_runs_cut_at_disc_ranges(self):
        img = self.image(0x100, bytes(16))
        self.assertEqual(copy_runs([(i, 0x100 + i) for i in range(16)], img, [(0x104, 0x108)]),
                         [(0, 0x100, 4), (4, 0x104, 4), (8, 0x108, 8)])

    def test_relayout_sources(self):
        # One element {u32 n; void* p;}: PS1 8 bytes, PC 16 (the pointer moves to offset 8).
        img = self.image(0x100, bytes([1, 2, 3, 4, 0, 0, 0, 0]))
        img.refs[0x104] = ("target", 0)
        src = []
        words = relayout(img, 0x100, 8, 16, {"ps1_elem": 8, "pc_elem": 16}, src)
        self.assertEqual(words, [0x04030201, ("target", 0)])
        self.assertEqual(src[:4], [0x100, 0x101, 0x102, 0x103])
        self.assertEqual(src[4:], [None] * 12)

    def test_fnv1a64(self):
        self.assertEqual(fnv1a64([b""]), 0xCBF29CE484222325)
        self.assertEqual(fnv1a64([b"a"]), 0xAF63DC4C8601EC8C)
        self.assertEqual(fnv1a64([b"ab", b"c"]), fnv1a64([b"abc"]))


# A probe source: a scalar, a struct with a pointer (8 bytes on the PS1, 16 on the PC), an
# incomplete array, and a pointer conversion GCC accepts with -fpermissive and clang only with
# its diagnostics off (clang's default error that made the probes fail, xg-htn.32).
PROBE_SOURCE = """
typedef struct { void* p; int q; } S;
int b;
S c;
extern unsigned short a[];
void f(unsigned int* x);
void g(unsigned short* y) { f(y); }
"""


class Probes(unittest.TestCase):
    def setUp(self):
        self.tmp = Path(tempfile.mkdtemp())

    def tearDown(self):
        shutil.rmtree(self.tmp)

    def compilers(self):
        found = [cc for cc in ("gcc", "clang") if shutil.which(cc)]
        if not found:
            self.skipTest("no gcc or clang")
        return found

    def obj(self, text: str) -> str:
        src = self.tmp / "game.c"
        src.write_text(text)
        return f"{self.tmp}/CMakeFiles/game_m.dir{src}.o"  # as source_of() reads CMake's object paths

    def test_family_flags(self):
        for cc in self.compilers():
            self.assertIn("-Wno-everything" if cc == "clang" else "-fpermissive", probe_flags(cc))
        with self.assertRaises(ProbeError):
            probe_flags(str(self.tmp / "no-such-compiler"))

    def test_sizes_both_layouts(self):
        obj = self.obj(PROBE_SOURCE)
        for cc in self.compilers():
            sizes = probe_sizes({"b": [obj], "c": [obj], "a": [obj]}, "us", cc)
            self.assertEqual(sizes["b"], {"pc": 4, "ps1": 4}, cc)
            self.assertEqual(sizes["c"], {"pc": 16, "ps1": 8}, cc)
            self.assertEqual(sizes["a"], {"pc_elem": 2, "ps1_elem": 2}, cc)

    def test_source_that_does_not_compile_stops(self):
        obj = self.obj("int b = ;\n")
        for cc in self.compilers():
            with self.assertRaisesRegex(ProbeError, "does not compile"):
                probe_sizes({"b": [obj]}, "us", cc)

    def test_name_without_any_size_stops(self):
        obj = self.obj("struct Unknown; extern struct Unknown u;\n")
        for cc in self.compilers():
            with self.assertRaisesRegex(ProbeError, "without sizes"):
                probe_sizes({"u": [obj]}, "us", cc)


    def test_alias_declarations(self):
        obj = self.obj('#include "common.h"\n'
                       "extern unsigned char plain[0x30];\n"
                       "extern unsigned char marked[0x30] PORT_ALIAS;\n"
                       "extern unsigned char marked[0x30];\n"  # inherits the first one's attribute
                       "extern unsigned char unsized[];\n"
                       "extern unsigned char two[0x30], other[0x30] PORT_ALIAS;\n")
        names = ["plain", "marked", "unsized", "two"]
        for cc in self.compilers():
            out = alias_declarations({n: [obj] for n in names}, "us", cc)
            self.assertEqual([o.split(":")[0] for o in out], ["plain", "two"], cc)
            self.assertIn("declare it on its own", out[1])
            self.assertIn("game.c:2:", out[0])

    def test_data_residues(self):
        src = self.tmp / "bss.c"
        src.write_text("struct { char pad[4]; unsigned char a[0x30]; } __attribute__((aligned(16))) block;\n"
                       '__asm__(".globl inner\\n.set inner, block + 4");\n')
        cc = self.compilers()[0]
        o = self.tmp / "bss.o"
        subprocess.run([cc, "-c", "-fno-pie", str(src), "-o", str(o)], check=True)
        nm_out = subprocess.run(["nm", str(o)], capture_output=True, text=True).stdout
        where = {p[2]: (str(o), p[1], int(p[0], 16)) for p in (l.split() for l in nm_out.splitlines()) if len(p) == 3}
        self.assertEqual(data_residues(where), {"block": 0, "inner": 4})


EXTENTS = {"main": (0x80010000, 0x80060000), "battle": (0x80070000, 0x800C0000),
           "field": (0x80070000, 0x800B0000), "battle_loader": (0x801E4000, 0x801E9000),
           "battle_result": (0x801DE000, 0x801E4500), "menu": (0x801C5000, 0x801EA000)}


class PointerBounds(unittest.TestCase):
    def test_inside_and_past_the_end(self):
        out = {"battling": [
            '__attribute__((aligned(16), section(".data"))) unsigned char names[876]; /* pointed at only */',
            "    (void*)((char*)names + 868),",
            "    (void*)((char*)names + 1060),",
        ], "main": ["extern unsigned char names[];", "    (void*)((char*)other + 4000),"]}
        self.assertEqual(pointers_outside(out), ["battling: names + 1060 (the object is 876 bytes)"])


class OneObjectPerAddress(unittest.TestCase):
    def test_coresident(self):
        self.assertTrue(coresident("main", "battle", EXTENTS))
        self.assertTrue(coresident("battle_loader", "battle", EXTENTS))
        self.assertFalse(coresident("battle", "field", EXTENTS))  # the same base
        self.assertFalse(coresident("battle_loader", "battle_result", EXTENTS))  # overlapping images
        self.assertFalse(coresident("battle_loader", "field", EXTENTS))  # field sits where battle does
        self.assertFalse(coresident("menu", "battle_loader", EXTENTS))

    def test_overlaps(self):
        objs = [("main", "flag", 0x800A0000, 1, ""), ("battle", "flag", 0x800A0000, 1, ""),
                ("field", "other", 0x800A0000, 4, ""),  # never loaded with battle: fine
                ("battle", "table", 0x80080000, 0x10, ""), ("battle", "inner", 0x80080008, 4, ""),
                ("battle", "kept", 0x80090000, 8, ""), ("battle", "apart", 0x80090004, 8, "a reason")]
        out = overlapping_storage(objs, EXTENTS)
        self.assertEqual(out, ["battle table 0x80080000+0x10 and battle inner 0x80080008+0x4",
                               "battle flag 0x800A0000+0x1 and main flag 0x800A0000+0x1",
                               "field other 0x800A0000+0x4 and main flag 0x800A0000+0x1"])
        used = set()
        allowed = {frozenset({("battle", "table"), ("battle", "inner")}): "why"}
        self.assertEqual(len(overlapping_storage(objs, EXTENTS, allowed, used)), 2)
        self.assertEqual(used, set(allowed))

    def test_separate_pairs_have_reasons(self):
        for pair, why in SEPARATE.items():
            self.assertEqual(len(pair), 2)
            self.assertTrue(why)


if __name__ == "__main__":
    unittest.main()
