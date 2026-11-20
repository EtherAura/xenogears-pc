"""Build the audited desktop release assets from a public port checkout."""

from __future__ import annotations

import argparse
import datetime
import errno
import hashlib
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent
sys.path.insert(0, str(HERE))  # the shell entry point invokes Python with -I

import deb_assets
import linux_release
import release_inputs
import windows_release

BUILDER_RECIPE = HERE / "deb_ubuntu.Dockerfile"
PYTHON_TOOLS = ("splat64[mips]", "spimdisasm", "rabbitizer", "pyyaml")
DEFAULT_CACHE = Path(os.environ.get("XDG_CACHE_HOME") or Path.home() / ".cache") / "xenogears-pc"


class ReleaseError(Exception):
    pass


def run(*argv, **kwargs):
    return subprocess.run([str(part) for part in argv], stdin=subprocess.DEVNULL, check=True, **kwargs)


def python_pins() -> list[str]:
    entries = dict(line.split("==", 1) for line in (ROOT / "requirements.txt").read_text().splitlines()
                   if not line.startswith("#") and "==" in line)
    if missing := set(PYTHON_TOOLS) - entries.keys():
        raise ReleaseError(f"requirements.txt lacks release tooling pins for {', '.join(sorted(missing))}")
    return [f"{name}=={entries[name]}" for name in PYTHON_TOOLS]


def cached_tools(cache: Path, pins: list[str]) -> tuple[Path, Path]:
    python_key = hashlib.sha256(b"\0".join(s.encode() for s in [sys.version, *pins])).hexdigest()[:16]
    ubuntu_key = hashlib.sha256(BUILDER_RECIPE.read_bytes()).hexdigest()[:16]
    return cache / f"python-{python_key}", cache / f"ubuntu-{ubuntu_key}"


def cache_move(source: Path, dest: Path, marker: str) -> None:
    try:
        source.rename(dest)
    except OSError as error:
        if error.errno not in (errno.EEXIST, errno.ENOTEMPTY) or not (dest / marker).is_file():
            raise


def fetch(cache: Path, pins: list[str], python: Path, ubuntu: Path) -> None:
    """Prepare dependencies, with no disc path or release output."""
    cache.mkdir(parents=True, exist_ok=True)
    run(sys.executable, "-I", "-B", ROOT / "tools/deps.py", "fetch", "--cache", cache)
    if not (python / "bin/python3").is_file():
        with tempfile.TemporaryDirectory(prefix=".python-", dir=cache) as temp:
            env = Path(temp) / "env"
            run(sys.executable, "-I", "-m", "venv", env)
            run(env / "bin/python3", "-I", "-m", "pip", "--isolated", "install",
                "--disable-pip-version-check", "--no-cache-dir", *pins)
            cache_move(env, python, "bin/python3")
    if not (ubuntu / "usr/bin/makensis").is_file():
        with tempfile.TemporaryDirectory(prefix=".ubuntu-", dir=cache) as temp:
            temp = Path(temp)
            context = temp / "context"
            context.mkdir()
            image = f"xenogears-pc-deb:{ubuntu.name}"
            run("docker", "build", "--platform", "linux/amd64", "--file", BUILDER_RECIPE, "--tag", image, context)
            container = run("docker", "create", "--platform", "linux/amd64", "--pull", "never",
                            "--network", "none", image, capture_output=True, text=True).stdout.strip()
            archive = temp / "ubuntu.tar"
            try:
                run("docker", "export", "--output", archive, container)
            finally:
                run("docker", "rm", container)
            rootfs = temp / "rootfs"
            rootfs.mkdir()
            run("tar", "--extract", "--file", archive, "--directory", rootfs, "--no-same-owner", "--no-overwrite-dir")
            if not (rootfs / "usr/bin/makensis").is_file():
                raise ReleaseError("the Ubuntu builder does not contain makensis")
            cache_move(rootfs, ubuntu, "usr/bin/makensis")


def asset_names(tag: str) -> tuple[list[str], str]:
    windows = windows_release.package_names(windows_release.release_names(tag))
    linux = linux_release.release_names(tag)
    version = deb_assets.version_from_tag(tag)
    deb = f"{deb_assets.PACKAGE}_{version}_{deb_assets.ARCH}.deb"
    return [*windows, linux["tarball"], linux["sha256"], deb, f"{deb}.sha256", f"{deb}.cdx.json"], deb


def check_out(out: Path, names: list[str]) -> None:
    if out.is_symlink() or (out.exists() and not out.is_dir()):
        raise ReleaseError(f"--out {out}: expected a directory")
    if collisions := [name for name in names if os.path.lexists(out / name)]:
        raise ReleaseError(f"--out {out} already holds {', '.join(collisions)}")


def host_sandbox(work: Path, cwd: Path, *inputs: Path) -> list[str]:
    # NSIS uses /tmp for its mapped compression buffer even with TMPDIR set.
    argv = ["bwrap", "--die-with-parent", "--unshare-net", "--unshare-ipc", "--ro-bind", "/", "/",
            "--dev-bind", "/dev", "/dev", "--proc", "/proc", "--tmpfs", "/tmp"]
    for folder in dict.fromkeys(inputs):
        if folder.is_relative_to("/tmp") and not folder.is_relative_to(work):
            argv.extend(("--ro-bind", str(folder), str(folder)))
    return [*argv, "--bind", str(work), str(work), "--setenv", "TMPDIR", str(work / "tmp"),
            "--chdir", str(cwd), "--"]


def build_windows(work: Path, tree: Path, cache: Path, python: Path, ubuntu: Path,
                  discs: Path, tag: str, pending: Path, makensis: str | None) -> None:
    env = dict(os.environ, PATH=os.pathsep.join([str(python / "bin"), os.environ.get("PATH", os.defpath)]))
    if makensis is None:
        env.update(NSISDIR=str(ubuntu / "usr/share/nsis"), NSISCONFDIR=str(ubuntu / "etc"))
        makensis = str(ubuntu / "usr/bin/makensis")
    run(*host_sandbox(work, tree, cache, discs, Path(makensis).parent),
        "bash", tree / "packaging/build-windows-zip.sh", "--installer",
        "--makensis", makensis, "--discs", discs, "--cache", cache, "--version", tag,
        "--out", pending, "--scratch", work, env=env)


def build_linux(work: Path, tree: Path, python: Path, ubuntu: Path, image: Path, tag: str,
                deb: str, pending: Path) -> None:
    for argv in (("tools/extract.py", "us", "--image", image, "--check-image"), ("tools/configure.py", "us")):
        run(*host_sandbox(work, tree, python, image.parent), python / "bin/python3", "-E", "-s", "-B", *argv)
    count = release_inputs.module_count(tree, "us")
    print(f"Linux release: {count} checked module images", flush=True)
    build = work / "linux"
    build.mkdir()
    run("bwrap", "--die-with-parent", "--unshare-net", "--unshare-ipc", "--clearenv",
        "--ro-bind", ubuntu, "/", "--dev", "/dev", "--proc", "/proc", "--tmpfs", "/tmp",
        "--ro-bind", tree, "/work/source", "--bind", build, "/work/build", "--bind", pending, "/work/out",
        "--setenv", "HOME", "/tmp", "--setenv", "PATH", "/usr/bin:/bin", "--setenv", "LC_ALL", "C.UTF-8",
        "--setenv", "SOURCE_DATE_EPOCH", "0", "--setenv", "PYTHONDONTWRITEBYTECODE", "1", "--chdir", "/work",
        "--", "/bin/bash", "/work/source/packaging/build-linux-release.sh", tag, deb)


def promote(pending: Path, out: Path, names: list[str]) -> None:
    if {path.name for path in pending.iterdir()} != set(names):
        raise ReleaseError("the builders did not produce the complete release bundle")
    for name in names:
        path = pending / name
        if not path.is_file() or path.is_symlink() or not path.stat().st_size:
            raise ReleaseError(f"{name}: expected a regular, nonempty release asset")
        if name.endswith(".sha256"):
            asset = name.removesuffix(".sha256")
            if asset not in names:
                raise ReleaseError(f"{name}: missing companion asset")
            with (pending / asset).open("rb") as stream:
                digest = hashlib.file_digest(stream, "sha256").hexdigest()
            if path.read_text() != f"{digest}  {asset}\n":
                raise ReleaseError(f"{name}: SHA-256 does not match {asset}")
    check_out(out, names)
    missing = []
    folder = out
    while not folder.exists():
        missing.append(folder)
        folder = folder.parent
    created = []
    written = []
    try:
        for folder in reversed(missing):
            try:
                folder.mkdir()
            except FileExistsError:
                if not folder.is_dir() or folder.is_symlink():
                    raise
            else:
                created.insert(0, folder)
        for name in names:
            windows_release.copy_new(pending / name, out / name, written)
    except (OSError, windows_release.ReleaseError) as error:
        raise ReleaseError(f"promoting the release bundle: {error}; "
                           + windows_release.undo(written, created, out, pending)) from error
    for path in written:
        print(path)


def build(args, cache: Path, python: Path, ubuntu: Path, tag: str) -> None:
    names, deb = asset_names(tag)
    discs = args.discs.resolve()
    image = release_inputs.disc_image(ROOT, discs, "us")
    out = args.out.absolute()
    check_out(out, names)
    for folder in (discs, cache):
        if out.resolve().is_relative_to(folder) or folder.is_relative_to(out.resolve()):
            raise ReleaseError(f"--out {out} overlaps {folder}")
    if ROOT.is_relative_to(out.resolve()):
        raise ReleaseError(f"--out {out} contains the source checkout")
    if not (python / "bin/python3").is_file() or not (ubuntu / "usr/bin/makensis").is_file():
        raise ReleaseError(f"the build cache is incomplete. Run bash build-release.sh --fetch-only --cache {cache}")
    if any(not shutil.which(tool) for tool in ("bwrap", "x86_64-w64-mingw32-gcc")):
        raise ReleaseError("the build needs bubblewrap and the host's x86_64 MinGW toolchain")
    makensis = windows_release.find_makensis(args.makensis) if args.makensis else None
    run(sys.executable, "-I", "-B", ROOT / "tools/deps.py", "check", "--cache", cache)

    scratch = args.scratch or next(folder for folder in out.parents if folder.is_dir())
    with tempfile.TemporaryDirectory(prefix=".xenogears-release-", dir=scratch) as temp:
        work = Path(temp)
        tree, pending = work / "tree", work / "pending"
        pending.mkdir()
        (work / "tmp").mkdir()
        release_inputs.copy_tree(ROOT, tree, work, cache, discs, out)
        build_windows(work, tree, cache, python, ubuntu, discs, tag, pending, makensis)
        build_linux(work, tree, python, ubuntu, image, tag, deb, pending)
        promote(pending, out, names)


def main() -> int:
    parser = argparse.ArgumentParser(prog="build-release.sh", description=__doc__)
    parser.add_argument("--fetch-only", action="store_true", help="prepare the cache without disc input")
    parser.add_argument("--cache", type=Path, help=f"dependency cache (default {DEFAULT_CACHE})")
    parser.add_argument("--discs", type=Path, help="folder holding the US Disc 1 image")
    parser.add_argument("--version", help="release version YYYY.MM.N, with optional leading v")
    parser.add_argument("--out", type=Path, help="new or existing output folder (default ./release)")
    parser.add_argument("--scratch", type=Path, help="existing scratch parent (default output's parent)")
    parser.add_argument("--makensis", help="use this compiler instead of the cached one")
    args = parser.parse_args()
    if args.fetch_only and any((args.discs, args.version, args.out, args.scratch, args.makensis)):
        parser.error("--fetch-only accepts only --cache")
    if not args.fetch_only and not args.discs:
        parser.error("--discs is required for the build")
    try:
        cache = (args.cache or DEFAULT_CACHE).expanduser().resolve()
        pins = python_pins()
        python, ubuntu = cached_tools(cache, pins)
        if args.fetch_only:
            fetch(cache, pins, python, ubuntu)
        else:
            implicit = args.cache is None and args.version is None and args.out is None
            if implicit:
                fetch(cache, pins, python, ubuntu)
            args.out = args.out or Path("release")
            version = args.version or datetime.datetime.now(datetime.timezone.utc).strftime("%Y.%m.0")
            build(args, cache, python, ubuntu, f"v{version.removeprefix('v')}")
    except (OSError, subprocess.CalledProcessError, ReleaseError, release_inputs.InputError,
            windows_release.ReleaseError, linux_release.ReleaseError, deb_assets.AssetError) as error:
        print(f"build-release: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
