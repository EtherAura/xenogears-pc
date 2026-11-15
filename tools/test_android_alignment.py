#!/usr/bin/env python3
"""check_android_alignment.py regressions on fixture APKs, not a release APK.

    python tools/test_android_alignment.py                       # the tests
    python tools/test_android_alignment.py --write-fixtures DIR  # DIR/aligned.apk, DIR/unaligned.apk

Two kinds of fixture library, both made here and never shipped:
- linked: a tiny no-dependency AArch64 shared object linked by clang and
  ld.lld with an explicit -z max-page-size (16384, 65536 or 4096), when both
  are installed (the tests that need them say so when skipped);
- synthetic: ELF64 headers written by struct, for shapes a linker will not
  give (one 4 KB LOAD among 16 KB ones, truncated or foreign headers).
Every APK is a plain zip under a temporary folder (TMPDIR).
"""
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
CHECK = ROOT / "tools/check_android_alignment.py"
CLANG = shutil.which("clang")
LLD = shutil.which("ld.lld")
READELF = shutil.which("llvm-readelf")
TIMEOUT = 120

SOURCES = {
    "a": "int xg_fixture_a(int x) { return x * 3 + 1; }\nint xg_fixture_data_a[64] = {1};\n",
    "b": "static int counter;\nint xg_fixture_b(void) { return ++counter; }\n",
}


def link_so(out: Path, page: int, source: str = "a") -> Path:
    """A shared object linked for AArch64 with -z max-page-size=page."""
    src = out.with_suffix(".c")
    src.write_text(SOURCES[source])
    subprocess.run([CLANG, "--target=aarch64-linux-gnu", "-fPIC", "-shared", "-nostdlib", "-O1",
                    f"--ld-path={LLD}", f"-Wl,-z,max-page-size={page}", "-o", str(out), str(src)],
                   check=True, capture_output=True, text=True, timeout=TIMEOUT)
    return out


def synthetic_so(aligns: list[int], machine: int = 183, e_type: int = 3) -> bytes:
    """An ELF64 header and one PT_LOAD per alignment (the first covers the headers, the rest are BSS-like)."""
    phoff, phnum = 64, len(aligns)
    size = phoff + 56 * phnum
    ehdr = struct.pack("<16sHHIQQQIHHHHHH", b"\x7fELF\x02\x01\x01" + bytes(9), e_type, machine, 1,
                       0, phoff, 0, 0, 64, 56, phnum, 64, 0, 0)
    phdrs = b""
    for i, align in enumerate(aligns):
        vaddr = i * 0x100000
        filesz = size if i == 0 else 0
        phdrs += struct.pack("<IIQQQQQQ", 1, 4 if i == 0 else 6, 0, vaddr, vaddr, filesz, max(filesz, 0x100), align)
    return ehdr + phdrs


def make_apk(path: Path, members: dict[str, bytes], stored: bool = False) -> Path:
    with zipfile.ZipFile(path, "w", zipfile.ZIP_STORED if stored else zipfile.ZIP_DEFLATED) as z:
        z.writestr("AndroidManifest.xml", b"fixture manifest, not an Android one")
        for name, data in members.items():
            z.writestr(name, data)
    return path


def run_check(apk: Path, *extra: str) -> subprocess.CompletedProcess:
    return subprocess.run([sys.executable, str(CHECK), "--apk", str(apk), *extra],
                          capture_output=True, text=True, timeout=TIMEOUT)


class Case(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory(prefix="xg-align-test-")
        self.tmp = Path(self._tmp.name)

    def tearDown(self):
        self._tmp.cleanup()

    def apk(self, members: dict[str, bytes], name: str = "fixture.apk", stored: bool = False) -> Path:
        return make_apk(self.tmp / name, members, stored)

    def passes(self, apk: Path, *extra: str) -> str:
        r = run_check(apk, *extra)
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        return r.stdout

    def fails(self, apk: Path, *extra: str) -> str:
        r = run_check(apk, *extra)
        self.assertNotEqual(r.returncode, 0, r.stdout + r.stderr)
        self.assertIn("FAIL", r.stdout)
        return r.stdout


class SyntheticTests(Case):
    """Synthetic ELF headers: measurements of these fixtures, not of any linked library."""

    def test_every_library_listed_one_bad_fails(self):
        out = self.fails(self.apk({
            "lib/arm64-v8a/libSDL3.so": synthetic_so([0x4000, 0x4000]),
            "lib/arm64-v8a/libgood.so": synthetic_so([0x10000]),
            "lib/arm64-v8a/libbad.so": synthetic_so([0x1000, 0x1000]),
        }))
        self.assertIn("ok    lib/arm64-v8a/libSDL3.so: 2 LOAD, align 0x4000, 0x4000", out)
        self.assertIn("ok    lib/arm64-v8a/libgood.so: 1 LOAD, align 0x10000", out)
        self.assertIn("FAIL  lib/arm64-v8a/libbad.so: LOAD alignment 0x1000, 0x1000 below 0x4000", out)
        self.assertIn("3 lib/arm64-v8a/*.so", out)
        self.assertIn("1 failure(s)", out)

    def test_one_bad_load_among_several(self):
        out = self.fails(self.apk({"lib/arm64-v8a/libmixed.so": synthetic_so([0x4000, 0x1000, 0x4000])}))
        self.assertIn("libmixed.so: LOAD alignment 0x1000 below 0x4000 (LOADs: 0x4000, 0x1000, 0x4000)", out)

    def test_16k_and_larger_pass(self):
        out = self.passes(self.apk({
            "lib/arm64-v8a/lib16k.so": synthetic_so([0x4000, 0x4000]),
            "lib/arm64-v8a/lib64k.so": synthetic_so([0x10000, 0x10000]),
        }, stored=True))
        self.assertIn("lib16k.so: 2 LOAD, align 0x4000, 0x4000", out)
        self.assertIn("lib64k.so: 2 LOAD, align 0x10000, 0x10000", out)

    def test_just_below_16k_fails(self):
        self.fails(self.apk({"lib/arm64-v8a/lib8k.so": synthetic_so([0x2000])}))

    def test_other_abis_not_counted(self):
        out = self.passes(self.apk({
            "lib/arm64-v8a/libok.so": synthetic_so([0x4000]),
            "lib/x86_64/libother.so": synthetic_so([0x1000], machine=62),
        }))
        self.assertNotIn("libother", out)

    def test_empty_apk_fails(self):
        out = self.fails(self.apk({}))
        self.assertIn("no lib/arm64-v8a/*.so", out)

    def test_no_arm64_libraries_fails(self):
        out = self.fails(self.apk({"lib/x86_64/libxenogears-us.so": synthetic_so([0x4000], machine=62),
                                   "lib/arm64-v8a/README.txt": b"not a library"}))
        self.assertIn("no lib/arm64-v8a/*.so", out)

    def test_not_a_zip_fails(self):
        path = self.tmp / "garbage.apk"
        path.write_bytes(b"PK\x03\x04 truncated, not a zip")
        self.assertIn("not a readable APK", self.fails(path))

    def test_missing_apk_fails(self):
        self.assertIn("not a readable APK", self.fails(self.tmp / "absent.apk"))

    def test_malformed_members_fail(self):
        good = synthetic_so([0x4000])
        bad_pow2 = bytearray(synthetic_so([0x4000]))
        struct.pack_into("<Q", bad_pow2, 64 + 48, 0x5000)
        out = self.fails(self.apk({
            "lib/arm64-v8a/libgood.so": good,
            "lib/arm64-v8a/libempty.so": b"",
            "lib/arm64-v8a/libtext.so": b"just text, no ELF header at all, long enough to be read as one" * 2,
            "lib/arm64-v8a/libtrunc.so": good[:80],
            "lib/arm64-v8a/libx86.so": synthetic_so([0x4000], machine=62),
            "lib/arm64-v8a/libexec.so": synthetic_so([0x4000], e_type=2),
            "lib/arm64-v8a/libnoload.so": synthetic_so([]),
            "lib/arm64-v8a/libpow2.so": bytes(bad_pow2),
        }))
        self.assertIn("ok    lib/arm64-v8a/libgood.so", out)
        for name, why in [("libempty", "shorter than an ELF64 header"), ("libtext", "not an ELF file"),
                          ("libtrunc", "run past"), ("libx86", "not AArch64"), ("libexec", "not a shared object"),
                          ("libnoload", "no program headers"), ("libpow2", "not a power of two")]:
            self.assertIn(f"FAIL  lib/arm64-v8a/{name}.so: ", out)
            self.assertIn(why, out)
        self.assertIn("7 failure(s)", out)

    def test_library_without_load_fails(self):
        data = bytearray(synthetic_so([0x4000]))
        struct.pack_into("<I", data, 64, 6)  # PT_PHDR instead of PT_LOAD
        self.assertIn("no LOAD segment", self.fails(self.apk({"lib/arm64-v8a/libnone.so": bytes(data)})))

    def test_duplicate_member_fails(self):
        path = self.tmp / "dup.apk"
        with zipfile.ZipFile(path, "w") as z:
            z.writestr("lib/arm64-v8a/libdup.so", synthetic_so([0x4000]))
            with self.assertWarns(UserWarning):
                z.writestr("lib/arm64-v8a/libdup.so", synthetic_so([0x1000]))
        self.assertIn("appears more than once", self.fails(path))

    def test_failing_readelf_fails(self):
        apk = self.apk({"lib/arm64-v8a/libok.so": synthetic_so([0x4000])})
        self.assertIn("failed", self.fails(apk, "--readelf", str(self.tmp / "no-such-readelf")))
        self.assertIn("exited", self.fails(apk, "--readelf", "false"))

    def test_apk_not_rewritten(self):
        apk = self.apk({"lib/arm64-v8a/libok.so": synthetic_so([0x4000])})
        before = apk.read_bytes()
        self.passes(apk)
        self.assertEqual(apk.read_bytes(), before)


@unittest.skipUnless(READELF, "llvm-readelf not installed")
class ReadelfTests(Case):
    def test_cross_check_used(self):
        out = self.passes(self.apk({"lib/arm64-v8a/libok.so": synthetic_so([0x4000, 0x10000])}), "--require-readelf")
        self.assertIn("cross-checking with", out)


@unittest.skipUnless(CLANG and LLD, "clang and ld.lld are needed to link AArch64 fixtures")
class LinkedTests(Case):
    """Libraries linked here with an explicit max-page-size: fixtures, not production libraries."""

    def test_linked_16k_and_64k_pass(self):
        apk = self.apk({
            "lib/arm64-v8a/libfix16k.so": link_so(self.tmp / "a16.so", 16384).read_bytes(),
            "lib/arm64-v8a/libfix64k.so": link_so(self.tmp / "b64.so", 65536, "b").read_bytes(),
        })
        out = self.passes(apk, *(["--require-readelf"] if READELF else []))
        self.assertRegex(out, r"ok    lib/arm64-v8a/libfix16k.so: \d+ LOAD, align 0x4000")
        self.assertRegex(out, r"ok    lib/arm64-v8a/libfix64k.so: \d+ LOAD, align 0x10000")

    def test_linked_4k_fails_beside_good(self):
        apk = self.apk({
            "lib/arm64-v8a/libfix16k.so": link_so(self.tmp / "a16.so", 16384).read_bytes(),
            "lib/arm64-v8a/libfix4k.so": link_so(self.tmp / "b4.so", 4096, "b").read_bytes(),
        })
        out = self.fails(apk)
        self.assertIn("ok    lib/arm64-v8a/libfix16k.so", out)
        self.assertRegex(out, r"FAIL  lib/arm64-v8a/libfix4k.so: LOAD alignment 0x1000.* below 0x4000")
        self.assertIn("1 failure(s)", out)


def write_fixtures(folder: Path) -> int:
    """DIR/aligned.apk (two libraries linked for 16 KB) and DIR/unaligned.apk (one of them for 4 KB)."""
    if not (CLANG and LLD):
        print("clang and ld.lld are needed to link the fixture libraries")
        return 1
    folder.mkdir(parents=True, exist_ok=True)
    good_a = link_so(folder / "libfix-a-16k.so", 16384, "a").read_bytes()
    good_b = link_so(folder / "libfix-b-16k.so", 16384, "b").read_bytes()
    bad_b = link_so(folder / "libfix-b-4k.so", 4096, "b").read_bytes()
    make_apk(folder / "aligned.apk", {"lib/arm64-v8a/libfixa.so": good_a, "lib/arm64-v8a/libfixb.so": good_b})
    make_apk(folder / "unaligned.apk", {"lib/arm64-v8a/libfixa.so": good_a, "lib/arm64-v8a/libfixb.so": bad_b})
    print(f"wrote {folder / 'aligned.apk'} and {folder / 'unaligned.apk'} (fixture APKs, not release APKs)")
    return 0


if __name__ == "__main__":
    if len(sys.argv) == 3 and sys.argv[1] == "--write-fixtures":
        sys.exit(write_fixtures(Path(sys.argv[2])))
    unittest.main()
