#!/usr/bin/env python3
"""tools/deps.py on fixtures: no network, no real tarball."""
from contextlib import redirect_stderr, redirect_stdout
import hashlib
import io
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

import deps as D

GOOD = b"a tarball's bytes " * 4096
PIN = {"version": "1.2.3", "file": "thing-1.2.3.tar.gz", "url": "https://example.invalid/thing-1.2.3.tar.gz",
       "sha256": hashlib.sha256(GOOD).hexdigest()}


def giving(data, calls=None):
    def get(url, out):
        if calls is not None:
            calls.append(url)
        Path(out).write_bytes(data)
    return get


class Fixture(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix="deps-")
        self.base = Path(self.tmp.name)
        self.cache = self.base / "cache"

    def tearDown(self): self.tmp.cleanup()

    def manifest(self, **change):
        path = self.base / "deps.json"
        path.write_text(json.dumps({"thing": {**PIN, **change}}))
        return path


class FetchTests(Fixture):
    def test_right_sum_is_kept_under_its_name(self):
        self.assertEqual(D.fetch_one("thing", PIN, self.cache, giving(GOOD)), "fetched")
        self.assertEqual((self.cache / PIN["file"]).read_bytes(), GOOD)
        self.assertEqual([p.name for p in self.cache.iterdir()], [PIN["file"]])

    def test_wrong_sum_is_refused_and_nothing_is_left(self):
        with self.assertRaisesRegex(D.DepsError, "Not kept"):
            D.fetch_one("thing", PIN, self.cache, giving(GOOD + b"!"))
        self.assertEqual(list(self.cache.iterdir()), [])

    def test_cut_off_download_is_refused(self):
        with self.assertRaisesRegex(D.DepsError, "Not kept"):
            D.fetch_one("thing", PIN, self.cache, giving(GOOD[:1000]))
        self.assertEqual(list(self.cache.iterdir()), [])

    def test_a_failed_download_leaves_nothing(self):
        def broken(url, out):
            Path(out).write_bytes(GOOD[:10])
            raise OSError("connection reset")
        with self.assertRaisesRegex(D.DepsError, "connection reset"):
            D.fetch_one("thing", PIN, self.cache, broken)
        self.assertEqual(list(self.cache.iterdir()), [])

    def test_a_checked_file_is_not_downloaded_again(self):
        calls = []
        D.fetch_one("thing", PIN, self.cache, giving(GOOD, calls))
        self.assertEqual(D.fetch_one("thing", PIN, self.cache, giving(b"other", calls)), "kept")
        self.assertEqual(len(calls), 1)

    def test_a_file_with_another_sum_is_never_replaced(self):
        self.cache.mkdir()
        (self.cache / PIN["file"]).write_bytes(b"someone's file")
        calls = []
        with self.assertRaisesRegex(D.DepsError, "Nothing was replaced"):
            D.fetch_one("thing", PIN, self.cache, giving(GOOD, calls))
        self.assertEqual(calls, [])
        self.assertEqual((self.cache / PIN["file"]).read_bytes(), b"someone's file")


class ManifestTests(Fixture):
    def test_names_outside_the_cache_are_refused(self):
        for name in ("../thing.tar.gz", "sub/thing.tar.gz", "/tmp/thing.tar.gz", "..", ".hidden", ""):
            with self.subTest(name=name), self.assertRaises(D.DepsError):
                D.load(self.manifest(file=name))

    def test_only_https(self):
        for url in ("http://example.invalid/x.tar.gz", "file:///etc/passwd", "ftp://example.invalid/x"):
            with self.subTest(url=url), self.assertRaisesRegex(D.DepsError, "https"):
                D.load(self.manifest(url=url))

    def test_sum_must_be_a_sha256(self):
        for value in ("abc", PIN["sha256"].upper(), PIN["sha256"][:-1] + "g"):
            with self.subTest(value=value), self.assertRaisesRegex(D.DepsError, "sha256"):
                D.load(self.manifest(sha256=value))

    def test_the_real_manifest_loads(self):
        pins = D.load()
        self.assertEqual(sorted(pins), ["dav1d", "glslang", "sdl3"])
        for pin in pins.values():
            self.assertIn(pin["version"], pin["file"])


class FindTests(Fixture):
    def test_found_in_the_cache_then_in_the_other_folder(self):
        other = self.base / "distfiles"
        other.mkdir()
        (other / PIN["file"]).write_bytes(GOOD)
        self.assertEqual(D.find("thing", PIN, self.cache, (other,)), other / PIN["file"])
        self.cache.mkdir()
        (self.cache / PIN["file"]).write_bytes(GOOD)
        self.assertEqual(D.find("thing", PIN, self.cache, (other,)), self.cache / PIN["file"])

    def test_a_wrong_file_is_passed_over_and_named(self):
        other = self.base / "distfiles"
        other.mkdir()
        (other / PIN["file"]).write_bytes(b"not it")
        with self.assertRaisesRegex(D.DepsError, r"another SHA-256.*deps\.py fetch thing"):
            D.find("thing", PIN, self.cache, (other,))

    def test_commands(self):
        manifest = self.manifest()
        self.cache.mkdir()
        (self.cache / PIN["file"]).write_bytes(GOOD)
        for argv, expect in ((["want", "thing"], f"{PIN['file']} {PIN['sha256']}\n"),
                             (["version", "thing"], "1.2.3\n"),
                             (["path", "thing", "--cache", str(self.cache)], f"{self.cache / PIN['file']}\n")):
            out = io.StringIO()
            with patch.object(D, "MANIFEST", manifest), redirect_stdout(out):
                self.assertEqual(D.main(argv), 0)
            self.assertEqual(out.getvalue(), expect)
        err = io.StringIO()
        with patch.object(D, "MANIFEST", manifest), redirect_stderr(err), redirect_stdout(io.StringIO()):
            self.assertEqual(D.main(["path", "nothing"]), 1)
            self.assertEqual(D.main(["check", "--cache", str(self.base / "empty")]), 1)
        self.assertIn("not pinned: nothing", err.getvalue())


if __name__ == "__main__":
    unittest.main()
