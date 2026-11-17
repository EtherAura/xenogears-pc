#!/usr/bin/env python3
"""Run the Ubuntu 26.04 builder image on an already generated public port tree.

    python packaging/deb_container_run.py --source <public port tree> \\
        --build-dir <new empty folder> --out-dir <output folder> \\
        --tag v<YYYY>.<MM>.<N> --image <local image> [--runtime docker]

This is packaging/deb_ubuntu.Dockerfile's documented `docker run` command as a
small command line: it checks the three folders, then starts the container
runtime once, with an argument list (no shell), as

    <runtime> run --rm --platform linux/amd64 --pull never --network none
        --cap-drop ALL --security-opt no-new-privileges
        --mount type=bind,src=<source>,dst=/work/source,readonly
        --mount type=bind,src=<build>,dst=/work/build
        --mount type=bind,src=<out>,dst=/work/out
        <image> --tag <tag>

and nothing else: no other mount, device, capability, privilege, user,
environment variable or entry point. The image's own ENTRYPOINT runs the
mounted tree's packaging/deb_ubuntu.py, unchanged, which does every step (the
Release configure, the build, cpack, the strict Depends check, the .sha256,
the SBOM and the staging); this launcher configures, builds, packages and
parses nothing of its own.

What the caller provides:
  --source     the already generated public port tree (the one holding
               packaging/, port/ and build/pc/sources.cmake), mounted
               read-only. Nothing here generates sources.cmake or reads a
               disc. The working clone it is published from is refused when
               it shows one of a few known markers (.beads, .claude,
               CLAUDE.md, AGENTS.md, tools/publish_decomp.py, tools/publish/,
               docs/PLAN.md, disc/, bin/, work/), as is a tree holding the
               home folder or a disc image file (.bin, .cue, .iso, ...) at its
               top or in build/. That is a guard against mounting the wrong
               tree by mistake, not an audit: that the tree holds no game
               data, credentials or private documents is the caller's part.
  --build-dir  an existing, empty folder (not even hidden files; never a
               symbolic link): deb_ubuntu.py's own rule, checked here first.
  --out-dir    an existing folder; it must not hold the package's three
               names (its other contents are kept).
               The three folders must be separate: none may be, hold or sit
               inside another. Each must be a path Docker's --mount syntax
               can carry: no comma, double quote or control character
               (spaces and shell metacharacters are passed through inert).
  --tag        the release tag, by packaging/deb_assets.py's rule.
  --image      an image already present in the runtime's local store, built
               from deb_ubuntu.Dockerfile by the caller. --pull never: no image
               is pulled or built here. Whether the given image really is
               that recipe's build is not checked.
  --runtime    the container runtime's command line (default: docker). It must
               accept docker's run options above; its daemon (or socket) and
               the caller's access to it are the caller's to arrange.

Ownership: the image runs as uid 10001, gid 10001. The build and output
folders must be writable by that uid and the source readable by it; this
launcher changes no owner or permission and claims no other uid (rootless
runtimes and user namespaces map uids their own way). It deletes nothing.

Helpers: the tag rule, the package's names and the folder checks are
deb_ubuntu.py's and deb_assets.py's, loaded from this file's own folder
(never from --source, whose files run only inside the container).

Exit status: 0 when the runtime exited 0; 2 for a command line the option
parser refuses; 3 when a check refused the run (the runtime was not started);
4 when the runtime could not be started; otherwise the runtime's own exit
status, unchanged (docker gives 125 to 127 for its own failures, else the
container's; a runtime killed by a signal gives 128 plus its number). Those numbers can coincide, so the last stderr line says which
it was. After any start the output folder's three names are looked up and
reported: a failure may have left staged files there; nothing is deleted.

Limits: a 0 means the container's helper reported success; it is not a proof
that the image matches the recipe, that the build is Ubuntu 26.04's, that its
Depends are right, or that the package installs or starts anywhere. Standard
library only.
"""
from __future__ import annotations

import argparse
import importlib.util
import re
import shutil
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
PRIVATE_MARKERS = (".beads", ".claude", "CLAUDE.md", "AGENTS.md", "tools/publish_decomp.py", "tools/publish",
                   "docs/PLAN.md", "disc", "bin", "work")
DISC_SUFFIXES = (".bin", ".cue", ".iso", ".img", ".chd", ".ecm", ".mdf", ".mds", ".pbp", ".ccd", ".sub")
SOURCE_HELPERS = ("packaging/deb_ubuntu.py", "packaging/deb_build.py", "packaging/deb_assets.py")
IMAGE = re.compile(r"[A-Za-z0-9][A-Za-z0-9._/:@-]*")
UNSAFE = re.compile(r"[,\"\x00-\x1f\x7f]")
REFUSED, NOT_STARTED = 3, 4


class Refused(Exception):
    pass


def helpers(folder: Path):
    """deb_ubuntu.py from `folder` (by path, no bytecode), which loads deb_assets.py beside it."""
    sys.dont_write_bytecode = True
    spec = importlib.util.spec_from_file_location("deb_ubuntu", folder / "deb_ubuntu.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module, module.load_assets()


def within(path: Path, folder: Path) -> bool:
    return path == folder or folder in path.parents


def check_source(source: Path) -> None:
    if not source.is_dir():
        raise Refused(f"--source {source}: no such folder")
    lacks = [n for n in SOURCE_HELPERS if not (source / n).is_file()]
    if lacks:
        raise Refused(f"--source {source} lacks {', '.join(lacks)}: not a generated public port tree")
    found = [n for n in PRIVATE_MARKERS if (source / n).exists() or (source / n).is_symlink()]
    if found:
        raise Refused(f"--source {source} holds {', '.join(found)}: that is the working clone (or a tree with "
                      "game data), not the generated public port tree")
    if within(Path.home().resolve(), source):
        raise Refused(f"--source {source} is or holds the home folder")
    images = [p.name for folder in (source, source / "build") if folder.is_dir() for p in folder.iterdir()
              if p.suffix.lower() in DISC_SUFFIXES]
    if images:
        raise Refused(f"--source {source} holds {', '.join(sorted(images))}: disc image files are never mounted")


def check_folders(source_arg: Path, build_arg: Path, out_arg: Path) -> tuple[Path, Path, Path]:
    for label, path in (("--source", source_arg), ("--build-dir", build_arg), ("--out-dir", out_arg)):
        if UNSAFE.search(str(path.absolute())):
            raise Refused(f"{label} {str(path)!r} holds a comma, double quote or control character, "
                          "which Docker's --mount syntax cannot carry")
    for label, path in (("--build-dir", build_arg), ("--out-dir", out_arg)):
        if not path.is_dir():
            raise Refused(f"{label} {path}: no such folder (create it first; this launcher creates nothing)")
    source, build, out = source_arg.resolve(), build_arg.resolve(), out_arg.resolve()
    for label, path in (("--source", source), ("--build-dir", build), ("--out-dir", out)):
        if UNSAFE.search(str(path)):
            raise Refused(f"{label} resolves to {str(path)!r}, which Docker's --mount syntax cannot carry")
    named = (("--source", source), ("--build-dir", build), ("--out-dir", out))
    for i, (label_a, a) in enumerate(named):
        for label_b, b in named[i + 1:]:
            if within(a, b) or within(b, a):
                raise Refused(f"{label_a} {a} and {label_b} {b} overlap (the same folder or one inside the other)")
    return source, build, out


def command(runtime: str, source: Path, build: Path, out: Path, image: str, tag: str) -> list[str]:
    return [runtime, "run", "--rm", "--platform", "linux/amd64", "--pull", "never", "--network", "none",
            "--cap-drop", "ALL", "--security-opt", "no-new-privileges",
            "--mount", f"type=bind,src={source},dst=/work/source,readonly",
            "--mount", f"type=bind,src={build},dst=/work/build",
            "--mount", f"type=bind,src={out},dst=/work/out",
            image, "--tag", tag]


def prepare(args) -> tuple[list[str], Path, list[str]]:
    """Every check, then the argv; raises Refused before anything is started."""
    if not IMAGE.fullmatch(args.image):
        raise Refused(f"--image {args.image!r} is not an image reference (letters, digits and ._/:@- only, "
                      "not starting with -)")
    source, build, out = check_folders(args.source, args.build_dir, args.out_dir)
    check_source(source)
    ubuntu, assets = helpers(HERE)
    try:
        version = assets.version_from_tag(args.tag)
    except assets.AssetError as error:
        raise Refused(str(error)) from error
    deb = f"{assets.PACKAGE}_{version}_{assets.ARCH}.deb"
    names = [deb, f"{deb}.sha256", f"{deb}.cdx.json"]
    try:
        ubuntu.check_paths(source, args.build_dir, args.out_dir, names)
    except ubuntu.StepError as error:
        raise Refused(str(error)) from error
    return command(args.runtime, source, build, out, args.image, args.tag), out, names


def held(out: Path, names: list[str]) -> str:
    """What --out-dir holds of the three names now, looked up, never assumed."""
    try:
        present = [n for n in names if (out / n).exists() or (out / n).is_symlink()]
    except OSError as error:
        return f"--out-dir {out} could not be read ({error}): it may hold staged files"
    if not present:
        return f"--out-dir {out} holds none of the package's three names"
    return f"--out-dir {out} HOLDS {', '.join(present)} (left in place, nothing is deleted)"


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0], allow_abbrev=False)
    ap.add_argument("--source", type=Path, required=True, help="the generated public port tree (mounted read-only)")
    ap.add_argument("--build-dir", type=Path, required=True, help="an existing, empty folder for the build")
    ap.add_argument("--out-dir", type=Path, required=True, help="an existing folder for the package set")
    ap.add_argument("--tag", required=True, help="the release tag, vYYYY.MM.N")
    ap.add_argument("--image", required=True, help="a builder image already in the runtime's local store")
    ap.add_argument("--runtime", default="docker", help="the container runtime command (default: docker)")
    args = ap.parse_args()
    try:
        argv, out, names = prepare(args)
    except Refused as error:
        print(f"deb_container_run: REFUSED: {error}", file=sys.stderr)
        print("deb_container_run: the runtime was not started; nothing was created or changed", file=sys.stderr)
        return REFUSED
    runtime = shutil.which(argv[0])
    if runtime is None:
        print(f"deb_container_run: could not start the runtime: {argv[0]!r} is not an executable on PATH",
              file=sys.stderr)
        return NOT_STARTED
    print("$", " ".join(argv), flush=True)
    try:
        result = subprocess.run([runtime, *argv[1:]])
    except OSError as error:
        print(f"deb_container_run: could not start the runtime {runtime!r} ({error})", file=sys.stderr)
        return NOT_STARTED
    if result.returncode != 0:
        status = result.returncode if result.returncode > 0 else 128 - result.returncode  # a signal: 128 + its number
        print(f"deb_container_run: {held(out, names)}; this is not a release package set", file=sys.stderr)
        print(f"deb_container_run: FAILED: the runtime exited {status}", file=sys.stderr)
        return status
    print(f"deb_container_run: {held(out, names)}")
    print("deb_container_run: the runtime exited 0 (the image's helper reported success; that is no proof the "
          "image is the recipe's, nor of Depends, installation or start)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
