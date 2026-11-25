#!/usr/bin/env python3
"""Unit tests of tools/android_start_check.py's judgement (no emulator): what in a logcat says the app
crashed, which processes are the app's, what a game run must have logged, and the twin APK check."""
from __future__ import annotations

import sys
import tempfile
import unittest
import zipfile
from pathlib import Path

sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parent))
import android_start_check as check

PKG = "io.github.etheraura.xgport"


def line(pid: int, tag: str, msg: str, level: str = "I", tid: int | None = None) -> str:
    return f"11-25 16:13:28.529 {pid:5d} {tid or pid:5d} {level} {tag}: {msg}"


START = line(517, "ActivityManager", f"Start proc 3564:{PKG}/u0a174 for next-top-activity {{{PKG}/{PKG}.XgActivity}}")
ONCREATE_CRASH = "\n".join([
    START,
    line(3564, "AndroidRuntime", "FATAL EXCEPTION: SDLActivity", "E"),
    line(3564, "AndroidRuntime", f"Process: {PKG}, PID: 3564", "E"),
    line(3564, "AndroidRuntime", "java.lang.SecurityException: Need to declare "
                                 "android.permission.REQUEST_INSTALL_PACKAGES to call this api", "E"),
])


class Judgement(unittest.TestCase):
    def test_oncreate_crash(self):
        self.assertEqual(check.app_pids(ONCREATE_CRASH, PKG), {3564})
        found = check.crashes(ONCREATE_CRASH, PKG, {3564})
        self.assertEqual(len(found), 2)
        self.assertIn("FATAL EXCEPTION", found[0])

    def test_named_without_pid(self):
        # The process died before it was seen and its Start proc line is gone: the name still counts.
        log = line(3564, "AndroidRuntime", f"Process: {PKG}, PID: 3564", "E")
        self.assertEqual(len(check.crashes(log, PKG, set())), 1)

    def test_other_package_with_the_same_prefix(self):
        log = "\n".join([
            line(517, "ActivityManager", f"Start proc 4000:{PKG}.dev/u0a175 for activity"),
            line(4000, "AndroidRuntime", "FATAL EXCEPTION: main", "E"),
            line(4000, "AndroidRuntime", f"Process: {PKG}.dev, PID: 4000", "E"),
            line(4100, "DEBUG", f">>> {PKG}.dev <<<", "F"),
            line(517, "ActivityManager", f"ANR in {PKG}.dev", "E"),
        ])
        self.assertEqual(check.app_pids(log, PKG), set())
        self.assertEqual(check.crashes(log, PKG, set()), [])

    def test_restart_process(self):
        log = "\n".join([
            line(517, "ActivityManager", f"Start proc 4200:{PKG}:restart/u0a174 for activity"),
            line(4200, "AndroidRuntime", f"Process: {PKG}:restart, PID: 4200", "E"),
        ])
        self.assertEqual(check.app_pids(log, PKG), {4200})
        self.assertEqual(len(check.crashes(log, PKG, {4200})), 1)

    def test_fatal_signal_only_in_ours(self):
        theirs = line(999, "libc", "Fatal signal 11 (SIGSEGV), code 1 in tid 999 (RenderThread)", "F")
        ours = line(3564, "libc", "Fatal signal 6 (SIGABRT), code -1 in tid 3600 (xenogears)", "F", 3600)
        self.assertEqual(check.crashes(theirs, PKG, {3564}), [])
        self.assertEqual(len(check.crashes(ours, PKG, {3564})), 1)
        dump = line(3700, "DEBUG", f">>> {PKG} <<<", "F")
        self.assertEqual(len(check.crashes(dump, PKG, set())), 1)

    def test_anr(self):
        log = line(517, "ActivityManager", f"ANR in {PKG} ({PKG}/.XgActivity)", "E")
        self.assertEqual(len(check.crashes(log, PKG, set())), 1)

    def test_game_run_lines(self):
        lib = check.library("jp", "enhanced")
        self.assertEqual(lib, "libxenogears-enhanced-jp.so")
        self.assertEqual(check.library("us", "original"), "libxenogears-us.so")
        ok = "\n".join([
            line(3564, "xenogears", f"loader: {lib} at 0x5801000000"),
            line(3564, "xenogears", "xenogears: entering kernelMain"),
            line(3564, "xenogears", "quit at: vblank 300 (XG_QUIT_AT)"),
            line(3564, "xenogears", "xenogears: quit"),
        ])
        self.assertEqual(check.game_missing(ok, lib), [])
        self.assertEqual(len(check.game_missing(ok, "libxenogears-us.so")), 1)
        # Measured with the SDL template's configChanges on Android 16: a theme change destroyed the
        # activity, SDL's quit ended the game right after kernelMain, with the same "xenogears: quit".
        destroyed = "\n".join(l for l in ok.splitlines() if "quit at:" not in l)
        self.assertEqual(check.game_missing(destroyed, lib),
                         ["the game did not play to its vblank and quit there (XG_QUIT_AT)"])
        # Lines the parser cannot read (logcat's own headers) count for nothing.
        self.assertEqual(len(check.game_missing("--------- beginning of main\nxenogears: quit", lib)), 3)

    def test_relaunch(self):
        # Measured on the Android 13 image: a resource overlay change right after boot made the
        # activity again, and SDL ended the game ("activity finished" in the same process).
        log = "\n".join([
            START,
            line(512, "ActivityTaskManager", "Config changes=80000000 {1.0 310mcc260mnc [en_US]}"),
            line(3564, "SDL", "onDestroy()", "V"),
            line(3564, "SDL", "onCreate()", "V"),
            line(3564, "SDL", "activity finished", "V"),
        ])
        found = check.relaunched(log, check.app_pids(log, PKG))
        self.assertEqual(len(found), 1)
        self.assertIn("configuration change", found[0])
        self.assertEqual(check.relaunched(log, {9999}), [])
        self.assertEqual(check.crashes(log, PKG, {3564}), [])

    def test_load_run_lines(self):
        lib = check.library("us", "enhanced")
        ok = "\n".join([
            line(3564, "xenogears", f"loader: {lib} at 0x200001000000"),
            line(3564, "xenogears", "present: window 960x720 (2264x943 pixels, android), fullscreen"),
            line(3564, "xenogears", "vulkan: no usable device (needs Vulkan 1.3, R16_UINT storage images)"),
        ])
        self.assertEqual(check.missing(ok, check.load_wants(lib)), [])
        self.assertEqual(check.missing(ok.splitlines()[0], check.load_wants(lib)),
                         ["the game did not open its window"])

    def test_assets_sequence(self):
        line_ = ("  mGlobalConfiguration={1.0 310mcc260mnc [en_US] ldltr sw411dp w914dp h411dp 420dpi nrml "
                 "long port finger -keyb/v/h -nav/h winConfig={ mBounds=Rect(0, 0 - 1080, 2400)} as.3 s.22}")
        self.assertEqual(check.assets_seq(line_), 3)
        self.assertIsNone(check.assets_seq("mGlobalConfiguration={1.0 ?mcc?mnc}"))

    def test_avd_argument(self):
        image = "system-images;android-33;google_apis;x86_64"
        self.assertEqual(check.parse_avd(f"a13={image}=load"), ("a13", image, "load"))
        self.assertEqual(check.parse_avd(f"a16={image}"), ("a16", image, "game"))
        self.assertEqual(check.parse_avd("old"), ("old", "", "game"))
        with self.assertRaises(check.SetupError):
            check.parse_avd(f"a13={image}=fast")

    def test_twin(self):
        with tempfile.TemporaryDirectory() as tmp:
            def apk(name, dex, extra=b""):
                path = Path(tmp) / name
                with zipfile.ZipFile(path, "w") as z:
                    z.writestr("classes.dex", dex)
                    z.writestr("AndroidManifest.xml", b"manifest")
                    z.writestr("resources.arsc", b"arsc")
                    z.writestr("lib/x86_64/libxenogears-us.so", extra)
                return path
            a, b = apk("a.apk", b"dex", b"x86"), apk("b.apk", b"dex", b"arm")
            self.assertEqual(check.twin_differences(a, b), [])
            self.assertEqual(check.carried(a), {"libxenogears-us.so"})
            c = apk("c.apk", b"other dex")
            self.assertEqual(check.twin_differences(a, c), ["classes.dex"])


if __name__ == "__main__":
    unittest.main()
