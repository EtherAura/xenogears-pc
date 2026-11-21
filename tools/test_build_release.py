#!/usr/bin/env python3
"""Regression checks for the combined release command's offline and output boundaries."""

import argparse
import contextlib
import hashlib
import io
import shutil
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
             patch.object(release, "build_tables") as self.tables, contextlib.redirect_stderr(io.StringIO()):
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
                     ["--discs", "d", "--tables", "t.tar.gz"], ["--fetch-only", "--tables", "t.tar.gz"],
                     ["--fetch-only", "--write-tables", "tables"],
                     ["--discs", "d", "--write-tables", "tables", "--out", "out"],
                     ["--discs", "d", "--write-tables", "tables", "--version", "2026.11.0"],
                     ["--discs", "d", "--write-tables", "tables", "--makensis", "makensis"]):
            with self.subTest(argv=argv):
                status, fetch, builder, build, free = self.main(*argv)
                self.assertEqual(status, 2)
                for call in (fetch, builder, build, free, self.tables):
                    call.assert_not_called()

    def test_write_tables_makes_tables_and_no_asset(self):
        status, fetch, builder, build, free = self.main("--discs", "d", "--cache", "cache", "--write-tables", "tables",
                                                        "--scratch", "s")
        self.assertEqual(status, 0)
        for call in (fetch, builder, build, free):
            call.assert_not_called()
        self.assertEqual(self.tables.call_args.args[0].write_tables, Path("tables"))
        self.assertEqual(self.tables.call_args.args[0].region, "us")
        self.assertEqual(self.main("--discs", "d", "--write-tables", "tables", "--region", "jp")[0], 0)
        self.assertEqual(self.tables.call_args.args[0].region, "jp")

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

    @staticmethod
    def made(stage: Path) -> dict:
        return {engine: release.sha256(exe) for engine, exe in release.staged_executables(stage).items()}

    def test_release_packs_the_tables_its_own_build_made(self):
        """build(): after the Linux assets, the tables of a build that must be the packaged bytes, as one asset."""
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            python, ubuntu, out = root / "python", root / "ubuntu", root / "out"
            for tool in (python / "bin/python3", ubuntu / "usr/bin/makensis"):
                tool.parent.mkdir(parents=True)
                tool.touch()
            tag, seen = "v2026.11.0", {}

            def tables(work, tree, py, builder, tag, digest, tarball):
                seen.update(digest=digest, tarball=tarball.name)
                (work / "link-tables/us").mkdir(parents=True)
                (work / "link-tables/us/asm-layout.json").write_text("{}")
                return work / "link-tables"

            args = argparse.Namespace(discs=root / "discs", out=out, scratch=root, makensis=None)
            with patch.object(release.release_inputs, "disc_image", return_value=root / "d"), \
                 patch.object(release.release_inputs, "copy_tree"), \
                 patch.object(release.release_inputs, "tree_digest", return_value="d1"), \
                 patch.object(release.shutil, "which", return_value="tool"), patch.object(release, "run"), \
                 patch.object(release, "build_windows"), patch.object(release, "build_linux"), \
                 patch.object(release, "write_tables", side_effect=tables), patch.object(release, "promote") as promote:
                release.build(args, root / "cache", python, ubuntu, tag)
            self.assertEqual(seen, {"digest": "d1", "tarball": "xenogears-pc-2026.11.0-linux-x86_64.tar.gz"})
            names = promote.call_args.args[2]
            asset = "xenogears-pc-2026.11.0-link-tables.tar.gz"
            self.assertEqual(names[-2:], [asset, asset + ".sha256"])
            self.assertEqual(len(names), len(set(names)))

    def packed(self, root: Path, files: dict) -> Path:
        staged, pending = root / "staged", root / "pending"
        pending.mkdir(exist_ok=True)
        for rel, text in files.items():
            (staged / rel).parent.mkdir(parents=True, exist_ok=True)
            (staged / rel).write_text(text)
        release.pack_tables(staged, pending, "v2026.11.0")
        return pending / "xenogears-pc-2026.11.0-link-tables.tar.gz"

    def test_tables_asset_is_the_same_bytes_for_the_same_tables_and_unpacks_into_the_tree(self):
        files = {"us/asm-layout.json": "layout", "us/original-linux-x86_64/facts.json": "facts",
                 "us/original-linux-x86_64/srcdata/main.json": "plan"}
        with tempfile.TemporaryDirectory() as one, tempfile.TemporaryDirectory() as two:
            first = self.packed(Path(one), files)
            second = self.packed(Path(two), dict(reversed(files.items())))  # another order and other file times
            self.assertEqual(first.read_bytes(), second.read_bytes())
            self.assertEqual(first.with_name(first.name + ".sha256").read_text(),
                             f"{hashlib.sha256(first.read_bytes()).hexdigest()}  {first.name}\n")
            tree = Path(one) / "tree"
            (tree / "port/link-tables/us/stale-config").mkdir(parents=True)
            release.unpack_tables(first, tree)
            self.assertEqual({str(p.relative_to(tree / "port/link-tables")): p.read_text()
                              for p in (tree / "port/link-tables").rglob("*") if p.is_file()}, files)

    def test_tables_archive_with_anything_else_is_refused(self):
        import tarfile
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            cases = {"outside": ("elsewhere/file", tarfile.REGTYPE, ""), "above": ("link-tables/../x", tarfile.REGTYPE, ""),
                     "link": ("link-tables/us", tarfile.SYMTYPE, "/etc"), "hard": ("link-tables/a", tarfile.LNKTYPE, "b")}
            for label, (name, kind, target) in cases.items():
                with self.subTest(label):
                    asset = root / f"{label}.tar.gz"
                    with tarfile.open(asset, "w:gz") as archive:
                        info = tarfile.TarInfo(name)
                        info.type, info.linkname = kind, target
                        archive.addfile(info, io.BytesIO(b"") if kind == tarfile.REGTYPE else None)
                    tree = root / label
                    (tree / "port/link-tables").mkdir(parents=True)
                    (tree / "port/link-tables/keep").write_text("kept")
                    with self.assertRaisesRegex(release.ReleaseError, "not a file or folder under link-tables"):
                        release.unpack_tables(asset, tree)
                    self.assertEqual((tree / "port/link-tables/keep").read_text(), "kept")
            (root / "junk").write_text("not an archive")
            with self.assertRaisesRegex(release.ReleaseError, "not a link tables archive"):
                release.unpack_tables(root / "junk", root / "tree")

    def test_same_sources_must_give_the_recorded_executables(self):
        with tempfile.TemporaryDirectory() as temp:
            stage, tables = self.stage(Path(temp), "d1")
            lines = release.compare(self.made(stage), tables, "d1", True, "not reproduced")
            self.assertEqual(len(lines), 2)
            self.assertTrue(all("payload audit passed there" in line for line in lines))
            (stage / "bin/xenogears-enhanced").write_text("another compiler's")
            with self.assertRaisesRegex(release.ReleaseError, "bin/xenogears-enhanced is .* not reproduced"):
                release.compare(self.made(stage), tables, "d1", False, "not reproduced")

    def test_other_sources_are_not_compared(self):
        with tempfile.TemporaryDirectory() as temp:
            stage, tables = self.stage(Path(temp), "d1")
            lines = release.compare(self.made(stage), tables, "d2", False, "")
            self.assertTrue(all("an executable of other sources" in line for line in lines))
            with self.assertRaisesRegex(release.ReleaseError, "--reproduce"):
                release.compare(self.made(stage), tables, "d2", True, "")
        with tempfile.TemporaryDirectory() as temp:
            stage, tables = self.stage(Path(temp), "d1", record=False)
            lines = release.compare(self.made(stage), tables, "d1", False, "")
            self.assertTrue(all("no executable" in line for line in lines))
            (stage / "bin/xenogears").unlink()
            with self.assertRaisesRegex(release.ReleaseError, "left no bin/xenogears"):
                release.staged_executables(stage)

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
            args = argparse.Namespace(out=root / "out", scratch=root, region="us", reproduce=False, tables=None)
            with patch.object(release, "ROOT", root / "checkout"), patch.object(release, "builder") as builder:
                with self.assertRaisesRegex(release.ReleaseError, "no link tables for us .*link-tables.tar.gz"):
                    release.build_free(args, root / "cache", root / "ubuntu", "v2026.11.0")
                args.tables = root / "absent.tar.gz"
                with self.assertRaisesRegex(release.ReleaseError, "no such file"):
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

            args = argparse.Namespace(out=out, scratch=root, region="us", reproduce=False, tables=None)
            with patch.object(release, "ROOT", checkout), patch.object(release, "builder", side_effect=builder), \
                 patch.object(release.shutil, "which", return_value="bwrap"), contextlib.redirect_stdout(io.StringIO()):
                release.build_free(args, root / "cache", ubuntu, "v2026.11.0")
                with self.assertRaisesRegex(release.ReleaseError, "already holds"):
                    release.build_free(args, root / "cache", ubuntu, "v2026.11.0")
            self.assertEqual(calls, [(("/work/source/tools/pc_sources.py",), True), (("us", "v2026.11.0"), False)])
            # a release's tables in place of the checkout's: the build links from what the archive holds
            shutil.rmtree(checkout / "port/link-tables")
            asset = self.packed(root, {"us/asm-layout.json": "from the release"})
            linked = []

            def with_tables(rootfs, tree, build, *command, out=None, writable=False):
                linked.append((tree / "port/link-tables/us/asm-layout.json").read_text())
                builder(rootfs, tree, build, *command, out=out, writable=writable)

            args = argparse.Namespace(out=root / "out2", scratch=root, region="us", reproduce=False, tables=asset)
            with patch.object(release, "ROOT", checkout), patch.object(release, "builder", side_effect=with_tables), \
                 patch.object(release.shutil, "which", return_value="bwrap"), contextlib.redirect_stdout(io.StringIO()):
                release.build_free(args, root / "cache", ubuntu, "v2026.11.0")
                args = argparse.Namespace(out=root / "out3", scratch=root, region="jp", reproduce=False, tables=asset)
                with self.assertRaisesRegex(release.ReleaseError, "holds no tables for jp"):
                    release.build_free(args, root / "cache", ubuntu, "v2026.11.0")
            self.assertEqual(linked, ["from the release", "from the release"])
            self.assertFalse((checkout / "port/link-tables").exists())  # the checkout itself is not written to
            folder = "xenogears-pc-2026.11.0-linux-x86_64"
            self.assertEqual(sorted(p.name for p in out.iterdir()), [folder, folder + ".sha256"])
            self.assertEqual((out / (folder + ".sha256")).read_text(), "".join(
                f"{hashlib.sha256(engine.encode()).hexdigest()}  {folder}/{rel}\n" for engine, rel in release.ENGINES.items()))
            self.assertFalse(list(root.glob(".xenogears-release-*")))

if __name__ == "__main__":
    unittest.main()
