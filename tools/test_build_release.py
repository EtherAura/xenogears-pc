#!/usr/bin/env python3
"""Regression checks for the combined release command's offline and output boundaries."""

import argparse
import contextlib
import hashlib
import io
import json
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
            out, python, ubuntu, mingw = (root / name for name in ("out", "python", "ubuntu", "mingw"))
            out.mkdir()
            (out / "keep").write_text("existing")
            self.tools(python, ubuntu, mingw)

            def windows(work, tree, cache, py, builder, mingw, discs, tag, pending, compiler, tables):
                (pending / "windows.zip").write_bytes(b"private")

            args = argparse.Namespace(discs=root / "discs", out=out, scratch=root, makensis=None)
            with patch.object(release.release_inputs, "disc_image", return_value=root / "disc.bin"), \
                 patch.object(release.release_inputs, "copy_tree"), patch.object(release.shutil, "which", return_value="tool"), \
                 patch.object(release, "run"), patch.object(release, "build_windows", side_effect=windows), \
                 patch.object(release, "build_linux", side_effect=subprocess.CalledProcessError(1, "linux")):
                with self.assertRaises(subprocess.CalledProcessError):
                    release.build(args, root / "cache", python, ubuntu, mingw, "v2026.11.0")
            self.assertEqual([p.name for p in out.iterdir()], ["keep"])
            self.assertFalse(list(root.glob(".xenogears-release-*")))

    @staticmethod
    def tools(python: Path, ubuntu: Path, mingw: Path) -> None:
        for tool in (python / "bin/python3", ubuntu / release.UBUNTU_MARK, mingw / release.MINGW_MARK):
            tool.parent.mkdir(parents=True)
            tool.touch()

    def test_windows_builder_recipe_is_pinned(self):
        """mingw_debian.Dockerfile: the base by digest, every package but ca-certificates from one archive snapshot
        read with errors fatal, no file copied in, the mount point the release scripts use, a final non-root user."""
        import re
        text = release.MINGW_RECIPE.read_text()
        steps = re.sub(r"\\\n\s*", " ", "\n".join(l for l in text.splitlines() if not l.lstrip().startswith("#")))
        froms = re.findall(r"^FROM (.+)$", steps, re.M)
        self.assertEqual(len(froms), 1)
        self.assertRegex(froms[0], r"^--platform=linux/amd64 debian:\w+@sha256:[0-9a-f]{64}$")
        parts = [part.strip() for line in re.findall(r"^RUN (.+)$", steps, re.M) for part in line.split(";")]
        snapshot = re.findall(r"--snapshot=(\d{8}T\d{6}Z)", steps)
        self.assertEqual(len(set(snapshot)), 1, snapshot)
        installs = [part for part in parts if "apt-get install" in part]
        self.assertEqual(len(installs), 2)
        self.assertTrue(installs[0].endswith("--no-install-recommends ca-certificates") and "--snapshot" not in installs[0])
        self.assertIn(f"--snapshot={snapshot[0]}", installs[1])
        for package in ("gcc-mingw-w64-x86-64-posix", "g++-mingw-w64-x86-64-posix", "binutils-mingw-w64-x86-64",
                        "cmake", "ninja-build", "meson", "nasm", "glslc", "python3-yaml", "libvulkan-dev"):
            self.assertIn(package, installs[1].split())
        self.assertNotIn("gcc-mingw-w64-x86-64-win32", installs[1])  # one thread model: x86_64-w64-mingw32-gcc is posix
        update = [part for part in parts if part.startswith("apt-get update --snapshot")]
        self.assertEqual(update, [f"apt-get update --snapshot={snapshot[0]} -o APT::Update::Error-Mode=any"])
        self.assertLess(parts.index("rm -rf /var/lib/apt/lists/*"), parts.index(update[0]))
        self.assertIn("mkdir -p /work", parts)
        self.assertNotRegex(steps, r"^(COPY|ADD|ENV|ARG) ", "nothing is copied in and nothing set for the build")
        self.assertRegex(steps.rstrip().splitlines()[-2], r"^USER [1-9]\d*:[1-9]\d*$")
        self.assertNotRegex(steps, r"\b(curl|wget|pip3?|git)\b|https?://")

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
             patch.object(release, "fetch") as fetch, patch.object(release, "fetch_free") as builder, \
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
        fetch.assert_not_called()  # no Python tools: the builders, and Windows's library sources
        self.assertEqual(builder.call_args.args[-1], ("linux", "windows"))
        self.assertEqual(free.call_args.args[0].region, "us")
        status, fetch, builder, build, free = self.main("--only", "linux")
        self.assertEqual((status, builder.call_args.args[-1], free.call_args.args[0].only), (0, ("linux",), "linux"))

    def test_options_of_the_other_mode_are_refused(self):
        for argv in (["--write-tables", "tables"], ["--makensis", "makensis"], ["--discs", "d", "--region", "us"],
                     ["--discs", "d", "--reproduce"], ["--fetch-only", "--reproduce"],
                     ["--discs", "d", "--tables", "t.tar.gz"], ["--fetch-only", "--tables", "t.tar.gz"],
                     ["--fetch-only", "--write-tables", "tables"], ["--discs", "d", "--only", "linux"],
                     ["--fetch-only", "--only", "windows"], ["--only", "macos"],
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

    def test_release_packs_the_tables_its_own_builds_made(self):
        """build(): the Linux tables of a build that must be the packaged bytes and the Windows build's own tables,
        whose recorded bytes must be the .zip's, gathered as one asset."""
        import zipfile
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            python, ubuntu, mingw, out = root / "python", root / "ubuntu", root / "mingw", root / "out"
            self.tools(python, ubuntu, mingw)
            tag, seen = "v2026.11.0", {}
            top = "xenogears-pc-2026.11.0-windows-x64"

            def windows(work, tree, cache, py, builder, mingw_, discs, tag, pending, compiler, tables):
                seen["builder"] = mingw_
                with zipfile.ZipFile(pending / f"{top}.zip", "w") as packed:
                    for name in release.WINDOWS.values():
                        packed.writestr(f"{top}/{name}", self.zip_bytes.get(name, name))
                for engine, name in release.WINDOWS.items():
                    (tables / engine / "link-tables").mkdir(parents=True)
                    (tables / engine / "link-tables/config.txt").write_text(f"us {engine}-windows-x86_64\n")
                    (tables / engine / "installed.sha256").write_text(hashlib.sha256(name.encode()).hexdigest() + "\n")

            def linux(work, tree, ubuntu_, tag, digest, tarball):
                seen.update(digest=digest, tarball=tarball.name)
                return [work / "tables/us-original", work / "tables/us-enhanced"]

            def gather(work, tree, py, builds):
                seen["builds"] = [str(b.relative_to(work)) for b in builds]
                seen["records"] = {b.name: json.loads((b / "link-tables" / release.RECORD).read_text())
                                   for b in builds if (b / "link-tables" / release.RECORD).exists()}
                (work / "link-tables/us").mkdir(parents=True)
                (work / "link-tables/us/asm-layout.json").write_text("{}")
                return work / "link-tables"

            args = argparse.Namespace(discs=root / "discs", out=out, scratch=root, makensis=None)
            self.zip_bytes = {}
            context = lambda: (patch.object(release.release_inputs, "disc_image", return_value=root / "d"),  # noqa: E731
                               patch.object(release.release_inputs, "copy_tree"),
                               patch.object(release.release_inputs, "tree_digest", return_value="d1"),
                               patch.object(release.shutil, "which", return_value="tool"), patch.object(release, "run"),
                               patch.object(release, "build_windows", side_effect=windows),
                               patch.object(release, "build_linux"),
                               patch.object(release, "linux_tables", side_effect=linux),
                               patch.object(release, "gather_tables", side_effect=gather),
                               contextlib.redirect_stdout(io.StringIO()))
            with contextlib.ExitStack() as stack:
                for cm in context():
                    stack.enter_context(cm)
                promote = stack.enter_context(patch.object(release, "promote"))
                release.build(args, root / "cache", python, ubuntu, mingw, tag)
            self.assertEqual(seen["builder"], mingw)
            self.assertEqual((seen["digest"], seen["tarball"]), ("d1", "xenogears-pc-2026.11.0-linux-x86_64.tar.gz"))
            self.assertEqual([b.rsplit("/", 1)[-1] for b in seen["builds"]], ["us-original", "us-enhanced", "original",
                                                                            "enhanced"])
            self.assertEqual(seen["records"]["original"], {"version": 1, "file": "xenogears.exe", "sources": "d1",
                                                           "sha256": hashlib.sha256(b"xenogears.exe").hexdigest()})
            names = promote.call_args.args[2]
            asset = "xenogears-pc-2026.11.0-link-tables.tar.gz"
            self.assertEqual(names[-2:], [asset, asset + ".sha256"])
            self.assertEqual(len(names), len(set(names)))
            # the Windows tables must record the .zip's own bytes
            self.zip_bytes = {"xenogears-enhanced.exe": "other bytes"}
            with contextlib.ExitStack() as stack:
                for cm in context():
                    stack.enter_context(cm)
                promote = stack.enter_context(patch.object(release, "promote"))
                with self.assertRaisesRegex(release.ReleaseError, "xenogears-enhanced.exe is not the bytes packaged in "
                                                                  f"{top}.zip"):
                    release.build(args, root / "cache", python, ubuntu, mingw, tag)
                promote.assert_not_called()

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
            args = argparse.Namespace(out=root / "out", scratch=root, region="us", reproduce=False, tables=None,
                                      only=None)
            with patch.object(release, "ROOT", root / "checkout"), patch.object(release, "builder") as builder:
                with self.assertRaisesRegex(release.ReleaseError, "no link tables for us .*link-tables.tar.gz"):
                    release.build_free(args, root / "cache", root / "ubuntu", root / "mingw", "v2026.11.0")
                args.tables = root / "absent.tar.gz"
                with self.assertRaisesRegex(release.ReleaseError, "no such file"):
                    release.build_free(args, root / "cache", root / "ubuntu", root / "mingw", "v2026.11.0")
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

            args = argparse.Namespace(out=out, scratch=root, region="us", reproduce=False, tables=None, only="linux")
            with patch.object(release, "ROOT", checkout), patch.object(release, "builder", side_effect=builder), \
                 patch.object(release.shutil, "which", return_value="bwrap"), contextlib.redirect_stdout(io.StringIO()):
                release.build_free(args, root / "cache", ubuntu, root / "mingw", "v2026.11.0")
                with self.assertRaisesRegex(release.ReleaseError, "already holds"):
                    release.build_free(args, root / "cache", ubuntu, root / "mingw", "v2026.11.0")
            self.assertEqual(calls, [(("/work/source/tools/pc_sources.py",), True), (("us", "v2026.11.0"), False)])
            # a release's tables in place of the checkout's: the build links from what the archive holds
            shutil.rmtree(checkout / "port/link-tables")
            asset = self.packed(root, {"us/asm-layout.json": "from the release"})
            linked = []

            def with_tables(rootfs, tree, build, *command, out=None, writable=False):
                linked.append((tree / "port/link-tables/us/asm-layout.json").read_text())
                builder(rootfs, tree, build, *command, out=out, writable=writable)

            args = argparse.Namespace(out=root / "out2", scratch=root, region="us", reproduce=False, tables=asset,
                                      only="linux")
            with patch.object(release, "ROOT", checkout), patch.object(release, "builder", side_effect=with_tables), \
                 patch.object(release.shutil, "which", return_value="bwrap"), contextlib.redirect_stdout(io.StringIO()):
                release.build_free(args, root / "cache", ubuntu, root / "mingw", "v2026.11.0")
                args = argparse.Namespace(out=root / "out3", scratch=root, region="jp", reproduce=False, tables=asset,
                                          only="linux")
                with self.assertRaisesRegex(release.ReleaseError, "holds no tables for jp"):
                    release.build_free(args, root / "cache", ubuntu, root / "mingw", "v2026.11.0")
            self.assertEqual(linked, ["from the release", "from the release"])
            self.assertFalse((checkout / "port/link-tables").exists())  # the checkout itself is not written to
            folder, sums = "xenogears-pc-2026.11.0-linux-x86_64", "xenogears-pc-2026.11.0-us-executables.sha256"
            self.assertEqual(sorted(p.name for p in out.iterdir()), [folder, sums])
            self.assertEqual((out / sums).read_text(), "".join(
                f"{hashlib.sha256(engine.encode()).hexdigest()}  {folder}/{rel}\n" for engine, rel in release.ENGINES.items()))
            self.assertFalse(list(root.glob(".xenogears-release-*")))

    def test_without_discs_builds_windows_in_its_builder_and_compares_it(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            checkout, mingw, cache = root / "checkout", root / "mingw", root / "cache"
            cache.mkdir()
            config = checkout / "port/link-tables/us/original-windows-x86_64"
            config.mkdir(parents=True)
            (checkout / "port/link-tables/us/asm-layout.json").write_text("{}")
            (config / "facts.json").write_text("{}")
            (config / release.RECORD).write_text(json.dumps({"version": 1, "file": "xenogears.exe", "sources": "d1",
                                                             "sha256": hashlib.sha256(b"xenogears.exe").hexdigest()}))
            (mingw / release.MINGW_MARK).parent.mkdir(parents=True)
            (mingw / release.MINGW_MARK).touch()
            built = []

            def windows(work, mingw_, cache_, region, *, audit, tables):
                built.append((mingw_, region, audit, tables))
                stage = work / "windows-stage"
                stage.mkdir()
                for name in release.WINDOWS.values():
                    (stage / name).write_text(self.bytes_.get(name, name))
                return stage

            def run_once(out, reproduce=False):
                args = argparse.Namespace(out=out, scratch=root, region="us", reproduce=reproduce, tables=None,
                                          only="windows")
                with patch.object(release, "ROOT", checkout), patch.object(release, "run") as ran, \
                     patch.object(release, "windows_executables", side_effect=windows), \
                     patch.object(release, "builder") as linux, \
                     patch.object(release.release_inputs, "tree_digest", return_value="d1"), \
                     patch.object(release.shutil, "which", return_value="bwrap"), \
                     contextlib.redirect_stdout(io.StringIO()) as said:
                    release.build_free(args, cache, root / "ubuntu", mingw, "v2026.11.0")
                linux.assert_not_called()
                self.assertIn("check", ran.call_args.args)  # the library sources are checked first
                return said.getvalue()

            self.bytes_ = {}
            said = run_once(root / "out")
            self.assertEqual(built, [(mingw, "us", False, False)])  # no audit without the images; no tables written
            self.assertIn("xenogears.exe " + hashlib.sha256(b"xenogears.exe").hexdigest() + ": the bytes of the "
                          "disc-backed build", said)
            self.assertIn("xenogears-enhanced.exe", said.split("nothing to compare")[0].rsplit("\n", 1)[-1])
            folder = "xenogears-pc-2026.11.0-windows-x64"
            self.assertEqual(sorted(p.name for p in (root / "out").iterdir()),
                             sorted([folder, "xenogears-pc-2026.11.0-us-executables.sha256"]))
            with self.assertRaisesRegex(release.ReleaseError, "--reproduce: the tables record no xenogears-enhanced.exe"):
                run_once(root / "out2", reproduce=True)
            self.bytes_ = {"xenogears.exe": "other bytes"}
            with self.assertRaisesRegex(release.ReleaseError, "xenogears.exe is .* not reproduced"):
                run_once(root / "out3")
            self.assertFalse((root / "out3").exists())
            (config / "facts.json").unlink()
            with self.assertRaisesRegex(release.ReleaseError, "no Windows tables for us"):
                run_once(root / "out4")

if __name__ == "__main__":
    unittest.main()
