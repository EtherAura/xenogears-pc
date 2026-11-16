#!/usr/bin/env python3
"""tools/build_android.sh's 16 KB alignment wiring (xg-6gm), offline, on fixture APKs.

    python tools/test_android_alignment_flow.py --ver us
    python tools/test_android_alignment_flow.py --ver us --script OTHER.sh   # e.g. a pre-change copy

The build script is never run: it is read, its wiring is checked in place
(the checker invoked on the APK of the selected variant, after Gradle and
before the release signature check, readelf required, failure fatal; the
payload check, options and signing untouched), and only the new pieces are
taken out of it and run by bash on their own: the ABI test, check_alignment()
and the loop that calls it. They run on fixture APKs made here (AArch64
libraries linked by clang and ld.lld with an explicit -z max-page-size,
tools/test_android_alignment.py) and the host's llvm-readelf in the NDK's
place: fixtures, not a release APK, and no Android build. --ver only labels
the run: the wiring does not depend on the region.
"""
import argparse
import os
import re
import shutil
import subprocess
import sys
import tempfile
import unittest
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import test_android_alignment as base  # noqa: E402  (link_so, make_apk, synthetic_so)

SCRIPT = ROOT / "tools/build_android.sh"
TIMEOUT = 120
APK_GLOB = '"$OUT"/gradle/app/outputs/apk/"$VARIANT"/*.apk'
ABI_TEST = 'case " ${ABIS[*]} " in *" arm64-v8a "*) HAS_ARM64=1 ;; *) HAS_ARM64=0 ;; esac'
READELF_DEF = 'READELF="$NDK/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-readelf"'
FUNC_RE = re.compile(r"^check_alignment\(\) \{\n(?:.*\n)*?^\}\n", re.M)
GUARD_RE = re.compile(r'^if \[ "\$HAS_ARM64" = 1 \]; then\n(?:.*\n)*?^fi\n', re.M)
LOOP_RE = re.compile(r'^    for (\w+) in ' + re.escape(APK_GLOB) + r'; do\n        check_alignment "\$\1"\n    done\n', re.M)
# What the script already did before the wiring: still there, unchanged.
PRESERVED = [
    '--abi) ABIS+=("$2"); shift ;;',
    '--region) REGIONS+=("$2"); shift ;;',
    '--release) VARIANT=release ;;',
    '--native-only) NATIVE_ONLY=1 ;;',
    '[ ${#ABIS[@]} -gt 0 ] || ABIS=(x86_64 arm64-v8a)',
    '[ ${#REGIONS[@]} -gt 0 ] || REGIONS=(us jp)',
    'for name in XG_ANDROID_KEYSTORE XG_ANDROID_KEY_ALIAS XG_ANDROID_KEYSTORE_PASSWORD XG_ANDROID_KEY_PASSWORD; do',
    '"$PY" tools/payloadcheck.py --build "$b" --exe "$b/libxenogears-$region.so"',
    '[ "$NATIVE_ONLY" = 1 ] && exit 0',
    'task=assemble${VARIANT^}',
    '-Pxg.sdlSource="$SDL3_SOURCE" -Pxg.jniLibs="$OUT/jniLibs" -Pxg.abis="$abilist" -Pxg.buildRoot="$OUT/gradle" \\',
    'ls -l ' + APK_GLOB,
    'apk=$(ls "$OUT"/gradle/app/outputs/apk/release/*.apk | head -1)',
    'certs=$("$ANDROID_HOME/build-tools/36.1.0/apksigner" verify --verbose --print-certs "$apk")',
    "grep -E '^Verified using v[0-9]+ scheme' <<< \"$certs\" | grep true",
]


def line_of(text: str, needle: str) -> int:
    """The 0-based line of needle's first appearance, -1 when absent."""
    at = text.find(needle)
    return -1 if at < 0 else text.count("\n", 0, at)


def verify_wiring(text: str) -> list[str]:
    """Problems with the alignment wiring in a build_android.sh text; empty when it is right."""
    problems = []
    func = FUNC_RE.search(text)
    if not func:
        return ["no check_alignment() function: the APK's 16 KB LOAD alignment is never checked"]
    body = func.group(0)
    call = '"$PY" tools/check_android_alignment.py --apk "$1" --require-readelf --readelf "$READELF"'
    if f"    if ! {call}; then\n" not in body:
        problems.append(f"check_alignment() does not run `if ! {call}; then`")
    if "        exit 1\n" not in body:
        problems.append("check_alignment() does not exit 1 when the checker fails")
    for soft in ("|| true", "|| :", "exit 0", "return 0", "set +e"):
        if soft in body:
            problems.append(f"check_alignment() contains `{soft}`: a failure would pass")
    if READELF_DEF not in text:
        problems.append(f"READELF is not the NDK's llvm-readelf ({READELF_DEF})")
    if ABI_TEST not in text:
        problems.append("HAS_ARM64 is not derived from the selected ABIs")
    loop = LOOP_RE.search(text)
    if not loop:
        problems.append(f"no `for ... in {APK_GLOB}; do check_alignment` over the selected variant's APKs")
        return problems
    guard = GUARD_RE.search(text)
    if not guard or not (guard.start() < loop.start() and loop.end() <= guard.end()):
        problems.append('the APK loop is not directly under `if [ "$HAS_ARM64" = 1 ]; then` at the top level')
    elif "else\n" in guard.group(0) and "not run" not in guard.group(0).split("else\n", 1)[1]:
        problems.append("the no-arm64 branch does not say that the check is not run")
    if text.count("check_alignment ") != 1:
        problems.append("check_alignment is called other than once, from the APK loop")
    at_func, at_loop = text.count("\n", 0, func.start()), text.count("\n", 0, loop.start())
    at_gradle = line_of(text, '    "$task"\n')
    at_ls = line_of(text, "ls -l " + APK_GLOB)
    at_native = line_of(text, '[ "$NATIVE_ONLY" = 1 ] && exit 0')
    at_sign = line_of(text, 'if [ "$VARIANT" = release ]; then\n    # The signature')
    if at_gradle < 0 or at_ls < 0 or at_sign < 0:
        problems.append("the Gradle call, its APK listing or the release signature check is missing")
    elif not (at_func < at_native < at_gradle < at_ls < at_loop < at_sign):
        problems.append(f"order is not check_alignment() < native-only exit < Gradle < ls < check < signature "
                        f"(lines {at_func}, {at_native}, {at_gradle}, {at_ls}, {at_loop}, {at_sign})")
    for keep in PRESERVED:
        if keep not in text:
            problems.append(f"no longer present: {keep}")
    return problems


def run_bash(script: str, *args: str, cwd: Path = ROOT, **env: str) -> subprocess.CompletedProcess:
    """bash -c on a snippet taken out of build_android.sh (never the whole script)."""
    full = {**os.environ, **env}
    return subprocess.run(["bash", "-c", "set -euo pipefail\n" + script, "snippet", *args], cwd=cwd, env=full,
                          capture_output=True, text=True, timeout=TIMEOUT)


class Wiring(unittest.TestCase):
    """The script as it stands, and narrow mutations of it that a check must catch."""

    text = ""

    def test_wiring(self):
        self.assertEqual(verify_wiring(self.text), [])

    def test_mutations_detected(self):
        problems = verify_wiring(self.text)
        self.assertEqual(problems, [], "missing enforcement: the mutations need a wired script")
        call = 'check_android_alignment.py --apk "$1" --require-readelf --readelf "$READELF"'
        mutations = {
            "not wired": ('        check_alignment "$built"\n', ""),
            "readelf optional": (call, 'check_android_alignment.py --apk "$1" --readelf "$READELF"'),
            "PATH readelf": (call, 'check_android_alignment.py --apk "$1" --require-readelf'),
            "failure ignored": ('        exit 1\n    fi\n}', '        true\n    fi\n}'),
            "or true": (f'{call}; then', f'{call} || true; then'),
            "hard-coded APK": ('check_alignment "$built"', 'check_alignment "$OUT/old.apk"'),
            "debug only": ('if [ "$HAS_ARM64" = 1 ]; then', 'if [ "$HAS_ARM64" = 1 ] && [ "$VARIANT" = debug ]; then'),
            "x86_64 guard": (ABI_TEST, 'HAS_ARM64=0'),
            "other readelf": (READELF_DEF, 'READELF=llvm-readelf'),
            "payload dropped": ('        "$PY" tools/payloadcheck.py', '        : tools/payloadcheck.py'),
            "signature dropped": ('| grep true', ''),
        }
        for name, (old, new) in mutations.items():
            with self.subTest(name):
                self.assertIn(old, self.text)
                self.assertNotEqual(verify_wiring(self.text.replace(old, new, 1)), [], name)
        # The check moved after the signature/digest handoff.
        guard = GUARD_RE.search(self.text).group(0)
        moved = self.text.replace(guard, "", 1).rstrip("\n") + "\n" + guard
        self.assertTrue(any("order" in p for p in verify_wiring(moved)), verify_wiring(moved))


class Snippets(unittest.TestCase):
    """The wiring's own lines, run by bash on fixture APKs (labelled so in their names)."""

    text = ""

    @classmethod
    def setUpClass(cls):
        missing = [n for n, v in (("clang", base.CLANG), ("ld.lld", base.LLD), ("llvm-readelf", base.READELF)) if not v]
        if missing:
            raise RuntimeError(f"{', '.join(missing)} not found: no linked fixture, so no result (not a pass)")
        func = FUNC_RE.search(cls.text)
        guard = GUARD_RE.search(cls.text)
        if not func or not guard:
            raise RuntimeError("missing enforcement: the build script has no check_alignment()/HAS_ARM64 "
                               "wiring to run on the fixtures (which were found)")
        cls.func, cls.guard = func.group(0), guard.group(0)
        cls._tmp = tempfile.TemporaryDirectory(prefix="xg-align-flow-")
        cls.tmp = Path(cls._tmp.name) / "fixture dir with spaces"
        cls.tmp.mkdir()
        good_a = base.link_so(cls.tmp / "libfix-a 16k.so", 16384, "a").read_bytes()
        good_b = base.link_so(cls.tmp / "libfix-b 16k.so", 16384, "b").read_bytes()
        bad_b = base.link_so(cls.tmp / "libfix-b 4k.so", 4096, "b").read_bytes()
        cls.aligned = base.make_apk(cls.tmp / "FIXTURE aligned.apk",
                                    {"lib/arm64-v8a/libfixa.so": good_a, "lib/arm64-v8a/libfixb.so": good_b})
        cls.unaligned = base.make_apk(cls.tmp / "FIXTURE unaligned.apk",
                                      {"lib/arm64-v8a/libfixa.so": good_a, "lib/arm64-v8a/libfixb4k.so": bad_b})
        cls.no_arm64 = base.make_apk(cls.tmp / "FIXTURE x86_64 only.apk",
                                     {"lib/x86_64/libfix.so": base.synthetic_so([0x4000], machine=62)})
        cls.garbage = cls.tmp / "FIXTURE not a zip.apk"
        cls.garbage.write_bytes(b"PK\x03\x04 truncated, not a zip")
        # The host's llvm-readelf where the NDK's would be, through a path with spaces.
        cls.readelf = cls.tmp / "ndk bin" / "llvm-readelf"
        cls.readelf.parent.mkdir()
        os.symlink(base.READELF, cls.readelf)

    @classmethod
    def tearDownClass(cls):
        cls._tmp.cleanup()

    def check(self, apk: Path, readelf: Path | None = None, cwd: Path = ROOT) -> subprocess.CompletedProcess:
        script = self.func + 'check_alignment "$1"\necho XG_FLOW_RETURNED\n'
        return run_bash(script, str(apk), cwd=cwd, PY=sys.executable, READELF=str(readelf or self.readelf))

    def refused(self, r: subprocess.CompletedProcess, apk: Path) -> str:
        self.assertEqual(r.returncode, 1, r.stdout + r.stderr)
        self.assertNotIn("XG_FLOW_RETURNED", r.stdout)
        self.assertIn(f"build_android.sh: {apk}: arm64-v8a LOAD alignment below 16 KB", r.stderr)
        return r.stdout + r.stderr

    def test_aligned_passes(self):
        r = self.check(self.aligned)
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        self.assertIn("XG_FLOW_RETURNED", r.stdout)
        self.assertIn(f"cross-checking with {self.readelf} -lW", r.stdout)
        self.assertRegex(r.stdout, r"ok    lib/arm64-v8a/libfixa.so: \d+ LOAD, align 0x4000")
        self.assertRegex(r.stdout, r"ok    lib/arm64-v8a/libfixb.so: \d+ LOAD, align 0x4000")

    def test_unaligned_refused(self):
        out = self.refused(self.check(self.unaligned), self.unaligned)
        self.assertRegex(out, r"FAIL  lib/arm64-v8a/libfixb4k.so: LOAD alignment 0x1000.* below 0x4000")
        self.assertIn("ok    lib/arm64-v8a/libfixa.so", out)

    def test_missing_readelf_refused(self):
        out = self.refused(self.check(self.aligned, readelf=self.tmp / "no such ndk" / "llvm-readelf"), self.aligned)
        self.assertIn("failed", out)

    def test_missing_checker_refused(self):
        empty = self.tmp / "root without the checker"
        empty.mkdir(exist_ok=True)
        out = self.refused(self.check(self.aligned, cwd=empty), self.aligned)
        self.assertIn("check_android_alignment.py", out)

    def test_malformed_and_absent_refused(self):
        self.assertIn("not a readable APK", self.refused(self.check(self.garbage), self.garbage))
        absent = self.tmp / "FIXTURE absent.apk"
        self.assertIn("not a readable APK", self.refused(self.check(absent), absent))

    def test_no_arm64_library_refused(self):
        self.assertIn("no lib/arm64-v8a/*.so", self.refused(self.check(self.no_arm64), self.no_arm64))

    def test_abi_selection(self):
        for abis, want in [(["x86_64"], "0"), (["arm64-v8a"], "1"), (["x86_64", "arm64-v8a"], "1"),
                           (["arm64-v8a", "x86_64"], "1"), (["arm64-v8a-x"], "0")]:
            with self.subTest(abis):
                r = run_bash('ABIS=("$@")\n' + ABI_TEST + '\necho "$HAS_ARM64"\n', *abis)
                self.assertEqual((r.returncode, r.stdout.strip()), (0, want), r.stderr)

    def loop(self, variant: str, has_arm64: str, out: Path, abis: str = "x86_64 arm64-v8a"):
        script = self.func + 'ABIS=($XG_ABIS)\n' + self.guard + 'echo XG_FLOW_RETURNED\n'
        return run_bash(script, PY=sys.executable, READELF=str(self.readelf), OUT=str(out), VARIANT=variant,
                        HAS_ARM64=has_arm64, XG_ABIS=abis)

    def test_selected_variant_apk(self):
        out = self.tmp / "out dir"
        apks = out / "gradle/app/outputs/apk"
        for variant, src in (("release", self.aligned), ("debug", self.unaligned)):
            (apks / variant).mkdir(parents=True, exist_ok=True)
            shutil.copyfile(src, apks / variant / f"FIXTURE app-{variant}.apk")
        r = self.loop("release", "1", out)
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        self.assertIn(f"{apks}/release/FIXTURE app-release.apk: ", r.stdout)
        self.assertNotIn("app-debug", r.stdout)
        r = self.loop("debug", "1", out)
        self.assertEqual(r.returncode, 1, r.stdout + r.stderr)
        self.assertNotIn("XG_FLOW_RETURNED", r.stdout)
        self.assertIn("FIXTURE app-debug.apk: arm64-v8a LOAD alignment below 16 KB", r.stderr)
        # Every APK of the variant, not the first: a bad second one still fails.
        shutil.copyfile(self.unaligned, apks / "release" / "FIXTURE app-release-zz.apk")
        r = self.loop("release", "1", out)
        self.assertEqual(r.returncode, 1, r.stdout + r.stderr)
        self.assertIn("FIXTURE app-release-zz.apk: arm64-v8a LOAD alignment", r.stderr)

    def test_no_apk_is_no_success(self):
        out = self.tmp / "out without apk"
        (out / "gradle/app/outputs/apk/release").mkdir(parents=True, exist_ok=True)
        r = self.loop("release", "1", out)
        self.assertEqual(r.returncode, 1, r.stdout + r.stderr)
        self.assertIn("not a readable APK", r.stdout)

    def test_x86_64_only_is_said(self):
        r = self.loop("debug", "0", self.tmp / "unused", abis="x86_64")
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        self.assertIn("no arm64-v8a in --abi (x86_64): the 16 KB LOAD check is not run", r.stderr)
        self.assertNotIn("cross-checking", r.stdout)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--ver", choices=("us", "jp"), default="us", help="a label: the wiring is the same for both")
    ap.add_argument("--script", type=Path, default=SCRIPT, help="the build script to read (never run)")
    args, rest = ap.parse_known_args()
    text = args.script.read_text()
    print(f"[{args.ver}] {args.script}: alignment wiring, fixture APKs only (no Android build)")
    Wiring.text = Snippets.text = text
    prog = unittest.main(argv=[sys.argv[0], *rest], exit=False)
    return 0 if prog.result.wasSuccessful() else 1


if __name__ == "__main__":
    sys.exit(main())
