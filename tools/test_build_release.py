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


    def main(self, *argv):
        with patch.object(sys, "argv", ["build-release.sh", *argv]), patch.object(release, "python_pins", return_value=[]), \
             patch.object(release, "fetch") as fetch, patch.object(release, "fetch_builder") as builder, \
             patch.object(release, "build") as build, patch.object(release, "build_free") as free, \
             contextlib.redirect_stderr(io.StringIO()):
            try:
                status = release.main()
            except SystemExit as stop:
                status = stop.code
        return status, fetch, builder, build, free

    def test_without_discs_builds_from_the_tables(self):
        status, fetch, builder, build, free = self.main("--cache", "cache", "--version", "2026.11.0", "--out", "out",
                                                        "--region", "jp", "--reproduce")
        self.assertEqual(status, 0)
        build.assert_not_called()
        fetch.assert_not_called()
        builder.assert_not_called()  # an explicit cache is never fetched into
        args = free.call_args.args[0]
        self.assertEqual((args.region, args.reproduce, free.call_args.args[-1]), ("jp", True, "v2026.11.0"))
        status, fetch, builder, build, free = self.main()
        self.assertEqual(status, 0)
        fetch.assert_not_called()  # no Python tools, no library tarballs: the builder only
        builder.assert_called_once()
        self.assertEqual(free.call_args.args[0].region, "us")

    def test_options_of_the_other_mode_are_refused(self):
        for argv in (["--write-tables", "tables"], ["--makensis", "makensis"], ["--discs", "d", "--region", "us"],
                     ["--discs", "d", "--reproduce"], ["--fetch-only", "--reproduce"],
                     ["--fetch-only", "--write-tables", "tables"]):
            with self.subTest(argv=argv):
                status, fetch, builder, build, free = self.main(*argv)
                self.assertEqual(status, 2)
                for call in (fetch, builder, build, free):
                    call.assert_not_called()

    def test_tree_digest_names_the_sources_not_the_tables(self):
        with tempfile.TemporaryDirectory() as temp:
            tree = Path(temp)
            (tree / "port/link-tables/us").mkdir(parents=True)
            (tree / "port/CMakeLists.txt").write_text("project")
            (tree / "a").write_text("b")
            first = release.release_inputs.tree_digest(tree)
            (tree / "port/link-tables/us/asm-layout.json").write_text("{}")
            self.assertEqual(release.release_inputs.tree_digest(tree), first)
            (tree / "port/link-tables.txt").write_text("")  # only the folder is left out
            second = release.release_inputs.tree_digest(tree)
            self.assertNotEqual(second, first)
            (tree / "port/link-tables.txt").unlink()
            (tree / "a").write_text("c")
            self.assertNotIn(release.release_inputs.tree_digest(tree), (first, second))
            (tree / "a").unlink()
            (tree / "ab").write_text("")  # a name and its bytes do not run together
            self.assertNotIn(release.release_inputs.tree_digest(tree), (first, second))

    def stage(self, root: Path, digest: str, record=True):
        stage, tables = root / "stage", root / "tables"
        for engine, rel in release.ENGINES.items():
            (stage / rel).parent.mkdir(parents=True, exist_ok=True)
            (stage / rel).write_text(engine)
            folder = tables / release.TABLES_CONFIG.format(engine=engine)
            folder.mkdir(parents=True)
            if record:
                (folder / release.RECORD).write_text(release.json.dumps(
                    {"version": 1, "file": rel, "sha256": hashlib.sha256(engine.encode()).hexdigest(), "sources": digest}))
        return stage, tables

    def test_same_sources_must_give_the_recorded_executables(self):
        with tempfile.TemporaryDirectory() as temp:
            stage, tables = self.stage(Path(temp), "d1")
            lines = release.compare(stage, tables, "d1", True)
            self.assertEqual(len(lines), 2)
            self.assertTrue(all("payload audit passed there" in line for line in lines))
            (stage / "bin/xenogears-enhanced").write_text("another compiler's")
            with self.assertRaisesRegex(release.ReleaseError, "bin/xenogears-enhanced is .* not reproduced"):
                release.compare(stage, tables, "d1", False)

    def test_other_sources_are_not_compared(self):
        with tempfile.TemporaryDirectory() as temp:
            stage, tables = self.stage(Path(temp), "d1")
            lines = release.compare(stage, tables, "d2", False)
            self.assertTrue(all("an executable of other sources" in line for line in lines))
            with self.assertRaisesRegex(release.ReleaseError, "--reproduce"):
                release.compare(stage, tables, "d2", True)
        with tempfile.TemporaryDirectory() as temp:
            stage, tables = self.stage(Path(temp), "d1", record=False)
            self.assertTrue(all("no executable" in line for line in release.compare(stage, tables, "d1", False)))
            (stage / "bin/xenogears").unlink()
            with self.assertRaisesRegex(release.ReleaseError, "left no bin/xenogears"):
                release.compare(stage, tables, "d1", False)

    def test_tables_replace_their_configurations_only(self):
        with tempfile.TemporaryDirectory() as temp:
            staged, dest = Path(temp) / "staged", Path(temp) / "checkout/port/link-tables"
            for path, text in (("us/asm-layout.json", "new"), ("us/original-linux-x86_64/facts.json", "new"),
                               ("us/original-linux-x86_64/srcdata/main.json", "new")):
                (staged / path).parent.mkdir(parents=True, exist_ok=True)
                (staged / path).write_text(text)
            for path in ("us/asm-layout.json", "us/original-linux-x86_64/srcdata/gone.json",
                         "us/original-windows-x86_64/facts.json", "jp/asm-layout.json"):
                (dest / path).parent.mkdir(parents=True, exist_ok=True)
                (dest / path).write_text("old")
            with contextlib.redirect_stdout(io.StringIO()):
                release.install_tables(staged, dest)
            self.assertEqual({str(p.relative_to(dest)): p.read_text() for p in dest.rglob("*") if p.is_file()}, {
                "us/asm-layout.json": "new", "us/original-linux-x86_64/facts.json": "new",
                "us/original-linux-x86_64/srcdata/main.json": "new", "us/original-windows-x86_64/facts.json": "old",
                "jp/asm-layout.json": "old"})

    def test_without_discs_refuses_a_checkout_without_tables(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            args = argparse.Namespace(out=root / "out", scratch=root, region="us", reproduce=False)
            with patch.object(release, "ROOT", root / "checkout"), patch.object(release, "builder") as builder, \
                 self.assertRaisesRegex(release.ReleaseError, "no link tables for us"):
                release.build_free(args, root / "cache", root / "ubuntu", "v2026.11.0")
            builder.assert_not_called()
            self.assertFalse((root / "out").exists())

    def test_without_discs_writes_the_folder_and_its_sums(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            checkout, ubuntu, out = root / "checkout", root / "ubuntu", root / "out"
            (checkout / "port/link-tables/us").mkdir(parents=True)
            (checkout / "port/link-tables/us/asm-layout.json").write_text("{}")
            (ubuntu / "usr/bin").mkdir(parents=True)
            (ubuntu / "usr/bin/makensis").touch()
            calls = []

            def builder(rootfs, tree, build, *command, out=None, writable=False):
                calls.append((command[-2:] if "bash" in command[0] else command[-1:], writable))
                if "bash" in command[0]:
                    for engine, rel in release.ENGINES.items():
                        (build / "us-stage" / rel).parent.mkdir(parents=True, exist_ok=True)
                        (build / "us-stage" / rel).write_text(engine)

            args = argparse.Namespace(out=out, scratch=root, region="us", reproduce=False)
            with patch.object(release, "ROOT", checkout), patch.object(release, "builder", side_effect=builder), \
                 patch.object(release.shutil, "which", return_value="bwrap"), contextlib.redirect_stdout(io.StringIO()):
                release.build_free(args, root / "cache", ubuntu, "v2026.11.0")
                with self.assertRaisesRegex(release.ReleaseError, "already holds"):
                    release.build_free(args, root / "cache", ubuntu, "v2026.11.0")
            self.assertEqual(calls, [(("/work/source/tools/pc_sources.py",), True), (("us", "v2026.11.0"), False)])
            folder = "xenogears-pc-2026.11.0-linux-x86_64"
            self.assertEqual(sorted(p.name for p in out.iterdir()), [folder, folder + ".sha256"])
            self.assertEqual((out / (folder + ".sha256")).read_text(), "".join(
                f"{hashlib.sha256(engine.encode()).hexdigest()}  {folder}/{rel}\n" for engine, rel in release.ENGINES.items()))
            self.assertFalse(list(root.glob(".xenogears-release-*")))

if __name__ == "__main__":
    unittest.main()
