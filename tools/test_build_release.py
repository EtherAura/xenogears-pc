#!/usr/bin/env python3
"""Regression checks for the combined release command's offline and output boundaries."""

import argparse
import contextlib
import hashlib
import io
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

sys.dont_write_bytecode = True
root = Path(__file__).resolve().parents[1]
packaging = root / "packaging"
if not (packaging / "build_release.py").is_file():
    packaging = root / "tools" / "publish" / "repo-port" / "packaging"
sys.path.insert(0, str(packaging))
import build_release as release


class ReleaseTests(unittest.TestCase):
    def test_integrator_version_and_offline_dispatch(self):
        argv = ["build-release.sh", "--discs", "player discs", "--cache", "cache",
                "--version", "2026.11.0", "--out", "out"]
        with patch.object(sys, "argv", argv), patch.object(release, "python_pins", return_value=[]), \
             patch.object(release, "fetch") as fetch, patch.object(release, "build") as build:
            self.assertEqual(release.main(), 0)
        fetch.assert_not_called()
        self.assertEqual(build.call_args.args[-1], "v2026.11.0")

    def test_fetch_provisions_cache_only(self):
        argv = ["build-release.sh", "--fetch-only", "--cache", "cache"]
        with patch.object(sys, "argv", argv), patch.object(release, "python_pins", return_value=[]), \
             patch.object(release, "fetch") as fetch, patch.object(release, "build") as build:
            self.assertEqual(release.main(), 0)
        fetch.assert_called_once()
        build.assert_not_called()

    def test_linux_failure_leaves_windows_outputs_private(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            out, python, ubuntu = (root / name for name in ("out", "python", "ubuntu"))
            out.mkdir()
            (out / "keep").write_text("existing")
            for tool in (python / "bin/python3", ubuntu / "usr/bin/makensis"):
                tool.parent.mkdir(parents=True)
                tool.touch()

            def windows(work, tree, cache, py, builder, discs, tag, pending, compiler):
                (pending / "windows.zip").write_bytes(b"private")

            args = argparse.Namespace(discs=root / "discs", out=out, scratch=root, makensis=None)
            with patch.object(release.release_inputs, "disc_image", return_value=root / "disc.bin"), \
                 patch.object(release.release_inputs, "copy_tree"), patch.object(release.shutil, "which", return_value="tool"), \
                 patch.object(release, "run"), patch.object(release, "build_windows", side_effect=windows), \
                 patch.object(release, "build_linux", side_effect=subprocess.CalledProcessError(1, "linux")):
                with self.assertRaises(subprocess.CalledProcessError):
                    release.build(args, root / "cache", python, ubuntu, "v2026.11.0")
            self.assertEqual([p.name for p in out.iterdir()], ["keep"])
            self.assertFalse(list(root.glob(".xenogears-release-*")))

    def test_complete_bundle_requires_every_checksum(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            pending = root / "pending"
            pending.mkdir()
            out = root / "subfolder" / "out"
            names, _ = release.asset_names("v2026.11.0")
            for name in names:
                if not name.endswith(".sha256"):
                    (pending / name).write_text(name)
            for name in names:
                if name.endswith(".sha256"):
                    asset = name.removesuffix(".sha256")
                    (pending / name).write_text(f"{hashlib.sha256(asset.encode()).hexdigest()}  {asset}\n")
            sidecar = pending / next(name for name in names if name.endswith(".deb.sha256"))
            checksum = sidecar.read_text()
            sidecar.write_text("bad checksum\n")
            with self.assertRaisesRegex(release.ReleaseError, "SHA-256 does not match"):
                release.promote(pending, out, names)
            self.assertFalse(out.parent.exists())
            sidecar.write_text(checksum)
            with contextlib.redirect_stdout(io.StringIO()):
                release.promote(pending, out, names)
            self.assertEqual({p.name for p in out.iterdir()}, set(names))


if __name__ == "__main__":
    unittest.main()
