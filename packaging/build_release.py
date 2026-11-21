"""Build the audited desktop release assets from a public port checkout.

Two builders do the compiling, each a root filesystem in the cache made from a
recipe beside this file that names its base image by digest and takes its
packages from one snapshot of its distribution's archive, so everyone who
builds gets the same compilers: deb_ubuntu.Dockerfile for Linux (GCC 15) and
mingw_debian.Dockerfile for Windows (MinGW-w64 GCC 16). Both run under
bubblewrap with no network.

With --discs, the player's US Disc 1 image: the Windows and Linux assets, each
executable through the payload audit before it is packaged, and the release's
link tables, xenogears-pc-<version>-link-tables.tar.gz: what a checkout links
from instead of the disc (port/CMakeLists.txt XG_LINK_FROM), as addresses,
sizes, offsets and hashes, never game bytes. The Windows build writes its
engines' tables as it builds; for Linux both engines are built once more in
the builder with the release's configuration. Each engine's executable, audited
and required to be the bytes just packaged, is recorded with its tables by its
SHA-256, beside a digest of the sources it was built from (executable.json).

With --discs and --write-tables DIR: those tables as files in DIR and nothing
else (for a checkout of one's own: DIR is its port/link-tables), for the US
release or, with --region jp and the JP Disc 1 image in --discs, the JP one.

Without --discs: the Linux and Windows executables of one region (--region,
default us; --only linux or windows for one of them), built in the same
builders from link tables: --tables FILE, a release's link-tables.tar.gz, else
the checkout's own port/link-tables. No disc is read and none is needed. It is
not the release: there is no payload audit without the images, so nothing is
packaged. The executables go into new folders of --out with their SHA-256
beside them, and each is compared with the one the tables record: with the
same sources it must be the same bytes, which passed the audit when the
tables were made (a difference is a failure). With other sources there is
nothing to compare, which --reproduce turns into a failure. The default cache
gets the builders (and for Windows the pinned library sources) when it has
none (Docker, network); a cache named with --cache is never fetched into.
"""

from __future__ import annotations

import argparse
import datetime
import errno
import gzip
import hashlib
import io
import json
import os
import shutil
import subprocess
import sys
import tarfile
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
MINGW_RECIPE = HERE / "mingw_debian.Dockerfile"
# A builder's root is complete when it holds this regular file (Debian's x86_64-w64-mingw32-gcc is a symbolic link
# through /etc/alternatives, which only resolves inside it).
UBUNTU_MARK, MINGW_MARK = "usr/bin/makensis", "usr/bin/x86_64-w64-mingw32-gcc-posix"
PYTHON_TOOLS = ("splat64[mips]", "spimdisasm", "rabbitizer", "pyyaml")
DEFAULT_CACHE = Path(os.environ.get("XDG_CACHE_HOME") or Path.home() / ".cache") / "xenogears-pc"
REGIONS = ("us", "jp")
ENGINES = {"original": "bin/xenogears", "enhanced": "bin/xenogears-enhanced"}  # the builds' XG_ENGINE: installed as
TABLES_CONFIG = "{engine}-linux-x86_64"  # port/CMakeLists.txt XG_TABLES_CONFIG, in the builder
WINDOWS = {"original": "xenogears.exe", "enhanced": "xenogears-enhanced.exe"}  # what the Windows engines install
WINDOWS_CONFIG = "{engine}-windows-x86_64"
WINDOWS_BUILD = {"original": "win-{region}-dist", "enhanced": "win-{region}-enhanced-dist"}  # tools/build_windows.sh
PLATFORMS = ("linux", "windows")
RECORD = "executable.json"  # beside a configuration's tables: the executable a disc-backed build made of them
TABLES_ASSET = "{package}-{version}-link-tables.tar.gz"  # the release's link tables: one folder, link-tables/
TABLES_FOLDER = "link-tables"
AUDIT_OK = "payload ok: "


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


def cached_tools(cache: Path, pins: list[str]) -> tuple[Path, Path, Path]:
    """The cache's Python tools, Linux builder and Windows builder, each named by what it is made from."""
    python_key = hashlib.sha256(b"\0".join(s.encode() for s in [sys.version, *pins])).hexdigest()[:16]
    ubuntu_key = hashlib.sha256(BUILDER_RECIPE.read_bytes()).hexdigest()[:16]
    mingw_key = hashlib.sha256(MINGW_RECIPE.read_bytes()).hexdigest()[:16]
    return cache / f"python-{python_key}", cache / f"ubuntu-{ubuntu_key}", cache / f"mingw-{mingw_key}"


def cache_move(source: Path, dest: Path, marker: str) -> None:
    try:
        source.rename(dest)
    except OSError as error:
        if error.errno not in (errno.EEXIST, errno.ENOTEMPTY) or not (dest / marker).is_file():
            raise


def fetch(cache: Path, pins: list[str], python: Path, ubuntu: Path, mingw: Path) -> None:
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
    fetch_image(cache, BUILDER_RECIPE, ubuntu, UBUNTU_MARK)
    fetch_image(cache, MINGW_RECIPE, mingw, MINGW_MARK)


def fetch_free(cache: Path, ubuntu: Path, mingw: Path, platforms: tuple[str, ...]) -> None:
    """What a build without a disc needs: the builders of its platforms, and Windows's library sources."""
    cache.mkdir(parents=True, exist_ok=True)
    if "linux" in platforms:
        fetch_image(cache, BUILDER_RECIPE, ubuntu, UBUNTU_MARK)
    if "windows" in platforms:
        run(sys.executable, "-I", "-B", ROOT / "tools/deps.py", "fetch", "--cache", cache)
        fetch_image(cache, MINGW_RECIPE, mingw, MINGW_MARK)


def fetch_image(cache: Path, recipe: Path, dest: Path, marker: str) -> None:
    """A builder's root filesystem from its recipe, built by Docker from an empty folder and exported."""
    if (dest / marker).is_file():
        return
    kind = dest.name.split("-", 1)[0]
    with tempfile.TemporaryDirectory(prefix=f".{kind}-", dir=cache) as temp:
        temp = Path(temp)
        context = temp / "context"
        context.mkdir()
        image = f"xenogears-pc-{'deb' if kind == 'ubuntu' else kind}:{dest.name}"
        run("docker", "build", "--platform", "linux/amd64", "--file", recipe, "--tag", image, context)
        container = run("docker", "create", "--platform", "linux/amd64", "--pull", "never",
                        "--network", "none", image, capture_output=True, text=True).stdout.strip()
        archive = temp / "rootfs.tar"
        try:
            run("docker", "export", "--output", archive, container)
        finally:
            run("docker", "rm", container)
        rootfs = temp / "rootfs"
        rootfs.mkdir()
        run("tar", "--extract", "--file", archive, "--directory", rootfs, "--no-same-owner", "--no-overwrite-dir")
        if not (rootfs / marker).is_file():
            raise ReleaseError(f"the builder made from {recipe.name} does not contain {marker}")
        cache_move(rootfs, dest, marker)


def asset_names(tag: str) -> tuple[list[str], str]:
    windows = windows_release.package_names(windows_release.release_names(tag))
    linux = linux_release.release_names(tag)
    version = deb_assets.version_from_tag(tag)
    deb = f"{deb_assets.PACKAGE}_{version}_{deb_assets.ARCH}.deb"
    tables = tables_asset(tag)
    return [*windows, linux["tarball"], linux["sha256"], deb, f"{deb}.sha256", f"{deb}.cdx.json",
            tables, f"{tables}.sha256"], deb


def tables_asset(tag: str) -> str:
    return TABLES_ASSET.format(package=linux_release.PACKAGE, version=linux_release.release_names(tag)["version"])


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


def build_windows(work: Path, tree: Path, cache: Path, python: Path, ubuntu: Path, mingw: Path,
                  discs: Path, tag: str, pending: Path, makensis: str | None, tables: Path) -> None:
    """The Windows assets, built in the Windows builder; each engine's link tables and installed bytes' SHA-256
    into tables/<engine>/."""
    env = dict(os.environ, PATH=os.pathsep.join([str(python / "bin"), os.environ.get("PATH", os.defpath)]))
    if makensis is None:
        env.update(NSISDIR=str(ubuntu / "usr/share/nsis"), NSISCONFDIR=str(ubuntu / "etc"))
        makensis = str(ubuntu / "usr/bin/makensis")
    run(*host_sandbox(work, tree, cache, discs, Path(makensis).parent),
        "bash", tree / "packaging/build-windows-zip.sh", "--installer",
        "--makensis", makensis, "--discs", discs, "--cache", cache, "--version", tag,
        "--out", pending, "--scratch", work, "--builder", mingw, "--tables-out", tables, env=env)


def windows_builder(mingw: Path, work: Path, cache: Path, *command) -> None:
    """Run a command in the Windows builder: work at /work/w (its tree at /work/w/tree), the cache read-only at
    /work/cache, no network, as packaging/build-windows-zip.sh --builder does."""
    run("bwrap", "--die-with-parent", "--unshare-net", "--unshare-ipc", "--clearenv", "--ro-bind", mingw, "/",
        "--dev", "/dev", "--proc", "/proc", "--tmpfs", "/tmp", "--tmpfs", "/work", "--bind", work, "/work/w",
        "--ro-bind", cache, "/work/cache", "--setenv", "HOME", "/tmp", "--setenv", "PATH", "/usr/bin:/bin",
        "--setenv", "LC_ALL", "C.UTF-8", "--setenv", "SOURCE_DATE_EPOCH", "0", "--setenv", "XG_DEPS_CACHE", "/work/cache",
        "--setenv", "TMPDIR", "/work/w/tmp", "--setenv", "PYTHONDONTWRITEBYTECODE", "1",
        "--setenv", "GIT_CEILING_DIRECTORIES", "/work/w", "--chdir", "/work/w/tree", "--", *command)


def windows_executables(work: Path, mingw: Path, cache: Path, region: str, *, audit: bool, tables: bool) -> Path:
    """Both Windows engines of one region built in work/tree by tools/build_windows.sh --dist in the Windows builder
    (with the payload audit, or without for a tree that has no disc images; with link tables written, or not),
    installed into work/windows-stage; that folder."""
    options = [*(["--jp"] if region == "jp" else []), "--dist", *([] if audit else ["--no-audit"]),
               *(["--write-tables"] if tables else []), "--engine", "both"]
    windows_builder(mingw, work, cache, "bash", "/work/w/tree/tools/build_windows.sh", *options, "/work/w/windows-stage")
    stage = work / "windows-stage"
    for name in WINDOWS.values():
        if not (stage / name).is_file() or (stage / name).is_symlink():
            raise ReleaseError(f"the Windows builder left no {name}")
    return stage


def build_linux(work: Path, tree: Path, python: Path, ubuntu: Path, image: Path, tag: str,
                deb: str, pending: Path) -> None:
    for argv in (("tools/extract.py", "us", "--image", image, "--check-image"), ("tools/configure.py", "us")):
        run(*host_sandbox(work, tree, python, image.parent), python / "bin/python3", "-E", "-s", "-B", *argv)
    count = release_inputs.module_count(tree, "us")
    print(f"Linux release: {count} checked module images", flush=True)
    build = work / "linux"
    build.mkdir()
    builder(ubuntu, tree, build, "/bin/bash", "/work/source/packaging/build-linux-release.sh", tag, deb, out=pending)


def builder_argv(ubuntu: Path, tree: Path, build: Path, out: Path | None = None, writable: bool = False) -> list:
    """The command prefix that runs inside the cached Ubuntu root filesystem: the tree read-only unless
    writable (/work/source), the build folder (/work/build), no network."""
    binds = ["--bind" if writable else "--ro-bind", tree, "/work/source", "--bind", build, "/work/build"]
    if out is not None:
        binds += ["--bind", out, "/work/out"]
    return ["bwrap", "--die-with-parent", "--unshare-net", "--unshare-ipc", "--clearenv",
            "--ro-bind", ubuntu, "/", "--dev", "/dev", "--proc", "/proc", "--tmpfs", "/tmp", *binds,
            "--setenv", "HOME", "/tmp", "--setenv", "PATH", "/usr/bin:/bin", "--setenv", "LC_ALL", "C.UTF-8",
            "--setenv", "SOURCE_DATE_EPOCH", "0", "--setenv", "PYTHONDONTWRITEBYTECODE", "1", "--chdir", "/work",
            "--"]


def builder(ubuntu: Path, tree: Path, build: Path, *command, out: Path | None = None, writable: bool = False) -> None:
    run(*builder_argv(ubuntu, tree, build, out, writable), *command)


def sha256(path: Path) -> str:
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def staged_executables(stage: Path) -> dict[str, Path]:
    found = {engine: stage / rel for engine, rel in ENGINES.items()}
    for path in found.values():
        if path.is_symlink() or not path.is_file() or not path.stat().st_size:
            raise ReleaseError(f"the builder left no {path.relative_to(stage)}")
    return found


def audit(ubuntu: Path, tree: Path, build: Path, engine: str, region: str, count: int) -> str:
    """tools/payloadcheck.py --dist on one installed executable, in the builder (the auditor reads the
    libraries the link took): its "payload ok" line, or ReleaseError."""
    exe = build / f"{region}-stage" / ENGINES[engine]
    before = sha256(exe)
    argv = [*builder_argv(ubuntu, tree, build), "python3", "-E", "-s", "-B", "/work/source/tools/payloadcheck.py",
            "--exe", f"/work/build/{region}-stage/{ENGINES[engine]}", "--build", f"/work/build/{region}-{engine}",
            "--ver", region, "--dist"]
    result = subprocess.run([str(part) for part in argv], stdin=subprocess.DEVNULL, capture_output=True, text=True)
    lines = result.stdout.splitlines()
    if (result.returncode != 0 or len(lines) != 2 or f"({region.upper()}): {count} module images" not in lines[0]
            or not lines[1].startswith(AUDIT_OK) or sha256(exe) != before):
        said = "".join(f"\n    | {line[:200]}" for line in lines[-20:] + result.stderr.splitlines()[-5:])
        raise ReleaseError(f"payload audit of the {region} {exe.name} FAILED (exit {result.returncode}){said}")
    return lines[1]


def record(folder: Path, file: str, digest_: str, sources: str) -> None:
    """executable.json beside a build's link tables: the executable it made of them, audited."""
    (folder / "link-tables" / RECORD).write_text(
        json.dumps({"version": 1, "file": file, "sha256": digest_, "sources": sources}, sort_keys=True) + "\n")


def linux_tables(work: Path, tree: Path, ubuntu: Path, tag: str, digest: str,
                 tarball: Path | None = None, region: str = "us") -> list[Path]:
    """Both Linux engines of one region built for their link tables, audited and recorded; their build folders.
    With tarball, the release's Linux package, the executables built for the tables must be the ones in it."""
    count = release_inputs.module_count(tree, region)
    build = work / "tables"
    build.mkdir()
    builder(ubuntu, tree, build, "/bin/bash", "/work/source/packaging/build-linux-executables.sh", region, tag,
            "--write-tables")
    packaged = packaged_executables(tarball) if tarball else None
    builds = []
    for engine, exe in staged_executables(build / f"{region}-stage").items():
        folder = build / f"{region}-{engine}"
        verdict = audit(ubuntu, tree, build, engine, region, count)
        made = sha256(exe)
        print(f"Link tables: {region} linux {engine} {made} ({count} module images; {verdict})", flush=True)
        if packaged is not None and packaged.get(ENGINES[engine]) != made:
            raise ReleaseError(f"the table build's {ENGINES[engine]} is not the bytes packaged in {tarball.name}")
        record(folder, ENGINES[engine], made, digest)
        builds.append(folder)
    return builds


def windows_tables(found: dict[str, tuple[Path, str]], region: str, digest: str,
                   packaged: dict[str, str] | None = None, archive: str = "") -> list[Path]:
    """The Windows engines' tables (engine: (build folder holding link-tables/, its installed bytes' SHA-256)),
    recorded; their build folders. With packaged (name: SHA-256 in the release's .zip), they must be those bytes."""
    builds = []
    for engine, (folder, made) in found.items():
        if packaged is not None and packaged.get(WINDOWS[engine]) != made:
            raise ReleaseError(f"the Windows build's {WINDOWS[engine]} is not the bytes packaged in {archive}")
        if not (folder / "link-tables" / "config.txt").is_file():
            raise ReleaseError(f"the Windows build of {engine} wrote no link tables")
        record(folder, WINDOWS[engine], made, digest)
        print(f"Link tables: {region} windows {engine} {made}", flush=True)
        builds.append(folder)
    return builds


def gather_tables(work: Path, tree: Path, python: Path, builds: list[Path]) -> Path:
    """The builds' tables gathered and scanned for retail bytes (tools/link_tables.py); their folder."""
    staged = work / TABLES_FOLDER
    run(*host_sandbox(work, tree, python), python / "bin/python3", "-E", "-s", "-B", tree / "tools/link_tables.py",
        *builds, "--dest", staged)
    return staged


def packaged_windows(archive: Path, tag: str) -> dict[str, str]:
    """SHA-256 of each Windows engine in the release's .zip, by its name under the one folder."""
    import zipfile
    top = windows_release.release_names(tag)["folder"]
    with zipfile.ZipFile(archive) as packed:
        listed = set(packed.namelist())
        return {name: hashlib.sha256(packed.read(f"{top}/{name}")).hexdigest()
                for name in WINDOWS.values() if f"{top}/{name}" in listed}


def pack_tables(staged: Path, pending: Path, tag: str) -> None:
    """The gathered tables as the release's asset and its .sha256: one folder, fixed order, times and owners,
    so the same tables give the same bytes."""
    name = tables_asset(tag)
    paths = sorted(path for path in staged.rglob("*") if path.is_dir() or path.is_file())
    with (pending / name).open("wb") as stream, \
            gzip.GzipFile(filename="", mode="wb", fileobj=stream, compresslevel=9, mtime=0) as packed, \
            tarfile.open(fileobj=packed, mode="w", format=tarfile.PAX_FORMAT) as archive:
        for path in [staged, *paths]:
            info = tarfile.TarInfo((Path(TABLES_FOLDER) / path.relative_to(staged)).as_posix())
            info.uid = info.gid = info.mtime = 0
            info.uname = info.gname = ""
            if path.is_dir():
                info.type, info.mode = tarfile.DIRTYPE, 0o755
                archive.addfile(info)
            else:
                data = path.read_bytes()
                info.size, info.mode = len(data), 0o644
                archive.addfile(info, io.BytesIO(data))
    (pending / f"{name}.sha256").write_text(f"{sha256(pending / name)}  {name}\n")


def unpack_tables(asset: Path, tree: Path) -> None:
    """A release's link tables into the private copy's port/link-tables, in place of any it has. Only folders and
    regular files under link-tables/ are taken; anything else in the archive is a refusal."""
    try:
        with tarfile.open(asset) as archive:
            members = archive.getmembers()
            for member in members:
                parts = Path(member.name).parts
                if (not (member.isdir() or member.isfile()) or not parts or parts[0] != TABLES_FOLDER
                        or ".." in parts or Path(member.name).is_absolute()):
                    raise ReleaseError(f"--tables {asset}: {member.name!r} is not a file or folder under {TABLES_FOLDER}/")
            dest = tree / release_inputs.TABLES
            if dest.exists():
                shutil.rmtree(dest)
            archive.extractall(dest.parent, members=members, filter="data")
    except (tarfile.TarError, EOFError) as error:
        raise ReleaseError(f"--tables {asset}: not a link tables archive ({error})") from error


def packaged_executables(tarball: Path) -> dict[str, str]:
    """SHA-256 of each executable in the Linux tarball, by its name under the one folder."""
    found = {}
    with tarfile.open(tarball) as archive:
        for member in archive:
            rel = member.name.partition("/")[2]
            if member.isfile() and rel in ENGINES.values():
                found[rel] = hashlib.file_digest(archive.extractfile(member), "sha256").hexdigest()
    return found


def install_tables(staged: Path, dest: Path) -> None:
    """The gathered tables into dest: each region's layout and the configurations made, others left as they are."""
    for region in sorted(path for path in staged.iterdir() if path.is_dir()):
        (dest / region.name).mkdir(parents=True, exist_ok=True)
        for entry in sorted(region.iterdir()):
            target = dest / region.name / entry.name
            if entry.is_dir():
                if target.exists():
                    shutil.rmtree(target)
                shutil.copytree(entry, target)
            else:
                shutil.copyfile(entry, target)
            print(target)


def compare(made: dict[str, str], tables: Path, digest: str, reproduce: bool, what: str,
            platform: str = "linux") -> list[str]:
    """Each executable made (engine: SHA-256) against the one its tables record; the lines to print, or
    ReleaseError when the same sources gave other bytes, or with reproduce when there is nothing to compare."""
    config, names = (TABLES_CONFIG, ENGINES) if platform == "linux" else (WINDOWS_CONFIG, WINDOWS)
    lines, failures = [], []
    for engine, got in made.items():
        try:
            recorded = json.loads((tables / config.format(engine=engine) / RECORD).read_text())
            want, sources = recorded["sha256"], recorded["sources"]
        except (OSError, ValueError, KeyError, TypeError):
            want = sources = None
        if sources != digest:
            lines.append(f"{names[engine]} {got}: nothing to compare (the tables record "
                         + ("no executable)" if sources is None else "an executable of other sources)"))
            if reproduce:
                failures.append(f"--reproduce: the tables record no {names[engine]} built from these sources")
        elif want == got:
            lines.append(f"{names[engine]} {got}: the bytes of the disc-backed build these tables were made with "
                         "(payload audit passed there)")
        else:
            failures.append(f"{names[engine]} is {got}, but the tables record {want} for these same sources: "
                            f"{what}")
    if failures:
        raise ReleaseError("; ".join(failures))
    return lines


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


def build_free(args, cache: Path, ubuntu: Path, mingw: Path, tag: str) -> None:
    """The Linux and Windows executables of one region from link tables: no disc is read."""
    platforms = (args.only,) if args.only else PLATFORMS
    folders = {"linux": linux_release.release_names(tag)["folder"], "windows": windows_release.release_names(tag)["folder"]}
    sums = f"{linux_release.PACKAGE}-{linux_release.release_names(tag)['version']}-{args.region}-executables.sha256"
    names = [folders[platform] for platform in platforms] + [sums]
    out = args.out.absolute()
    check_out(out, names)
    if out.resolve().is_relative_to(cache) or cache.is_relative_to(out.resolve()):
        raise ReleaseError(f"--out {out} overlaps {cache}")
    if ROOT.is_relative_to(out.resolve()):
        raise ReleaseError(f"--out {out} contains the source checkout")
    if args.tables is None and not (ROOT / release_inputs.TABLES / args.region / "asm-layout.json").is_file():
        raise ReleaseError(f"this checkout has no link tables for {args.region} ({release_inputs.TABLES}): give a "
                           f"release's with --tables ({tables_asset(tag)} of the release this checkout is), "
                           "or build from your disc (--discs)")
    if args.tables is not None and not args.tables.is_file():
        raise ReleaseError(f"--tables {args.tables}: no such file")
    if "linux" in platforms and not (ubuntu / UBUNTU_MARK).is_file():
        raise ReleaseError(f"the build cache has no Linux builder. Run bash build-release.sh --fetch-only --cache {cache}")
    if "windows" in platforms:
        if not (mingw / MINGW_MARK).is_file():
            raise ReleaseError(f"the build cache has no Windows builder. Run bash build-release.sh --fetch-only "
                               f"--cache {cache}")
        run(sys.executable, "-I", "-B", ROOT / "tools/deps.py", "check", "--cache", cache)
    if not shutil.which("bwrap"):
        raise ReleaseError("the build needs bubblewrap")
    print(f"No disc: the {' and '.join(platforms)} executables from the link tables, not audited here and not "
          "packaged.", flush=True)

    scratch = args.scratch or next(parent for parent in out.parents if parent.is_dir())
    with tempfile.TemporaryDirectory(prefix=".xenogears-release-", dir=scratch) as temp:
        work = Path(temp)
        tree, build_dir = work / "tree", work / "linux"
        build_dir.mkdir()
        (work / "tmp").mkdir()
        release_inputs.copy_tree(ROOT, tree, work, cache, out)
        digest = release_inputs.tree_digest(tree)
        if args.tables is not None:
            unpack_tables(args.tables.resolve(), tree)
            if not (tree / release_inputs.TABLES / args.region / "asm-layout.json").is_file():
                raise ReleaseError(f"--tables {args.tables}: it holds no tables for {args.region}")
        tables = tree / release_inputs.TABLES / args.region
        stages, verdicts, lines = {}, [], []
        if "linux" in platforms:
            builder(ubuntu, tree, build_dir, "python3", "-I", "-B", "/work/source/tools/pc_sources.py", writable=True)
            builder(ubuntu, tree, build_dir, "/bin/bash", "/work/source/packaging/build-linux-executables.sh",
                    args.region, tag)
            stages["linux"] = build_dir / f"{args.region}-stage"
            made = {engine: sha256(exe) for engine, exe in staged_executables(stages["linux"]).items()}
            verdicts += compare(made, tables, digest, args.reproduce,
                                "the build is not reproduced (another builder image?)")
            lines += [f"{got}  {folders['linux']}/{ENGINES[engine]}\n" for engine, got in made.items()]
        if "windows" in platforms:
            if not (tables / WINDOWS_CONFIG.format(engine="original") / "facts.json").is_file():
                raise ReleaseError(f"the tables hold no Windows tables for {args.region} (--only linux builds Linux alone)")
            stages["windows"] = windows_executables(work, mingw, cache, args.region, audit=False, tables=False)
            made = {engine: sha256(stages["windows"] / name) for engine, name in WINDOWS.items()}
            verdicts += compare(made, tables, digest, args.reproduce,
                                "the build is not reproduced (another builder image?)", platform="windows")
            lines += [f"{got}  {folders['windows']}/{WINDOWS[engine]}\n" for engine, got in made.items()]
        check_out(out, names)
        out.mkdir(parents=True, exist_ok=True)
        written = []
        try:
            for platform, stage in stages.items():
                written.append(out / folders[platform])
                shutil.copytree(stage, out / folders[platform])
            written.append(out / sums)
            with (out / sums).open("x") as stream:
                stream.write("".join(lines))
        except OSError as error:
            for path in written:
                shutil.rmtree(path) if path.is_dir() else path.unlink(missing_ok=True)
            raise ReleaseError(f"writing into --out {out}: {error}") from error
    for line in verdicts:
        print(line)
    for path in written:
        print(path)


def build_tables(args, cache: Path, python: Path, ubuntu: Path, mingw: Path, tag: str) -> None:
    """The link tables of one region's Linux and Windows executables, from the disc, into --write-tables; no
    asset."""
    discs = args.discs.resolve()
    image = release_inputs.disc_image(ROOT, discs, args.region)
    dest = args.write_tables.absolute()
    if dest.is_symlink() or (dest.exists() and not dest.is_dir()):
        raise ReleaseError(f"--write-tables {dest}: expected a folder")
    for folder in (discs, cache):
        if dest.resolve().is_relative_to(folder) or folder.is_relative_to(dest.resolve()):
            raise ReleaseError(f"--write-tables {dest} overlaps {folder}")
    if not all(path.is_file() for path in (python / "bin/python3", ubuntu / UBUNTU_MARK, mingw / MINGW_MARK)):
        raise ReleaseError(f"the build cache is incomplete. Run bash build-release.sh --fetch-only --cache {cache}")
    if not shutil.which("bwrap"):
        raise ReleaseError("the build needs bubblewrap")
    run(sys.executable, "-I", "-B", ROOT / "tools/deps.py", "check", "--cache", cache)
    scratch = args.scratch or next(folder for folder in dest.parents if folder.is_dir())
    with tempfile.TemporaryDirectory(prefix=".xenogears-release-", dir=scratch) as temp:
        work = Path(temp)
        tree = work / "tree"
        (work / "tmp").mkdir()
        release_inputs.copy_tree(ROOT, tree, work, cache, discs)
        digest = release_inputs.tree_digest(tree)
        for argv in (("tools/extract.py", args.region, "--image", image, "--check-image"),
                     ("tools/configure.py", args.region)):
            run(*host_sandbox(work, tree, python, image.parent), python / "bin/python3", "-E", "-s", "-B", *argv)
        builds = linux_tables(work, tree, ubuntu, tag, digest, region=args.region)
        stage = windows_executables(work, mingw, cache, args.region, audit=True, tables=True)
        found = {engine: (tree / "build/pc" / WINDOWS_BUILD[engine].format(region=args.region), sha256(stage / name))
                 for engine, name in WINDOWS.items()}
        builds += windows_tables(found, args.region, digest)
        install_tables(gather_tables(work, tree, python, builds), dest)


def build(args, cache: Path, python: Path, ubuntu: Path, mingw: Path, tag: str) -> None:
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
    if not all(path.is_file() for path in (python / "bin/python3", ubuntu / UBUNTU_MARK, mingw / MINGW_MARK)):
        raise ReleaseError(f"the build cache is incomplete. Run bash build-release.sh --fetch-only --cache {cache}")
    if not shutil.which("bwrap"):
        raise ReleaseError("the build needs bubblewrap")
    makensis = windows_release.find_makensis(args.makensis) if args.makensis else None
    run(sys.executable, "-I", "-B", ROOT / "tools/deps.py", "check", "--cache", cache)

    scratch = args.scratch or next(folder for folder in out.parents if folder.is_dir())
    with tempfile.TemporaryDirectory(prefix=".xenogears-release-", dir=scratch) as temp:
        work = Path(temp)
        tree, pending = work / "tree", work / "pending"
        pending.mkdir()
        (work / "tmp").mkdir()
        release_inputs.copy_tree(ROOT, tree, work, cache, discs, out)
        digest = release_inputs.tree_digest(tree)
        windows = work / "windows-tables"
        windows.mkdir()
        build_windows(work, tree, cache, python, ubuntu, mingw, discs, tag, pending, makensis, windows)
        build_linux(work, tree, python, ubuntu, image, tag, deb, pending)
        builds = linux_tables(work, tree, ubuntu, tag, digest, pending / linux_release.release_names(tag)["tarball"])
        archive = windows_release.release_names(tag)["zip"]
        found = {engine: (windows / engine, (windows / engine / "installed.sha256").read_text().strip())
                 for engine in WINDOWS}
        builds += windows_tables(found, "us", digest, packaged_windows(pending / archive, tag), archive)
        pack_tables(gather_tables(work, tree, python, builds), pending, tag)
        promote(pending, out, names)


def main() -> int:
    parser = argparse.ArgumentParser(prog="build-release.sh", description=__doc__)
    parser.add_argument("--fetch-only", action="store_true", help="prepare the cache without disc input")
    parser.add_argument("--cache", type=Path, help=f"dependency cache (default {DEFAULT_CACHE})")
    parser.add_argument("--discs", type=Path, help="folder holding the US Disc 1 image; without it, the Linux "
                        "executables are built from the checkout's link tables and nothing is packaged")
    parser.add_argument("--write-tables", type=Path, metavar="DIR",
                        help="with --discs: write the Linux executables' link tables as files into DIR (a checkout's "
                        "port/link-tables) and build no asset")
    parser.add_argument("--tables", type=Path, metavar="FILE",
                        help="without --discs: a release's link-tables.tar.gz to link from, in place of the "
                        "checkout's port/link-tables")
    parser.add_argument("--region", choices=REGIONS,
                        help="without --discs, or with --write-tables: the region to build (default us)")
    parser.add_argument("--only", choices=PLATFORMS, help="without --discs: build this platform's executables alone")
    parser.add_argument("--reproduce", action="store_true",
                        help="without --discs: fail unless each executable is the one the tables record")
    parser.add_argument("--version", help="release version YYYY.MM.N, with optional leading v")
    parser.add_argument("--out", type=Path, help="new or existing output folder (default ./release)")
    parser.add_argument("--scratch", type=Path, help="existing scratch parent (default output's parent)")
    parser.add_argument("--makensis", help="use this compiler instead of the cached one")
    args = parser.parse_args()
    if args.fetch_only and any((args.discs, args.version, args.out, args.scratch, args.makensis)):
        parser.error("--fetch-only accepts only --cache")
    if args.fetch_only and any((args.write_tables, args.tables, args.region, args.reproduce, args.only)):
        parser.error("--fetch-only accepts only --cache")
    if args.discs and (args.tables or args.reproduce or args.only or (args.region and not args.write_tables)):
        parser.error("--tables, --only and --reproduce are for a build without --discs, --region for one or for "
                     "--write-tables")
    if not args.discs and (args.write_tables or args.makensis):
        parser.error("--write-tables and --makensis need --discs")
    if args.write_tables and any((args.version, args.out, args.makensis)):
        parser.error("--write-tables makes the tables only: it takes --discs, --cache and --scratch")
    try:
        cache = (args.cache or DEFAULT_CACHE).expanduser().resolve()
        pins = python_pins()
        python, ubuntu, mingw = cached_tools(cache, pins)
        if args.fetch_only:
            fetch(cache, pins, python, ubuntu, mingw)
        else:
            implicit = args.cache is None and args.version is None and args.out is None
            args.out = args.out or Path("release")
            version = args.version or datetime.datetime.now(datetime.timezone.utc).strftime("%Y.%m.0")
            tag = f"v{version.removeprefix('v')}"
            if args.discs:
                if implicit:
                    fetch(cache, pins, python, ubuntu, mingw)
                if args.write_tables:
                    args.region = args.region or "us"
                    build_tables(args, cache, python, ubuntu, mingw, tag)
                else:
                    build(args, cache, python, ubuntu, mingw, tag)
            else:
                if args.cache is None:  # the default cache gets what it lacks; a named one is never fetched into
                    fetch_free(cache, ubuntu, mingw, (args.only,) if args.only else PLATFORMS)
                args.region = args.region or "us"
                build_free(args, cache, ubuntu, mingw, tag)
    except (OSError, subprocess.CalledProcessError, ReleaseError, release_inputs.InputError,
            windows_release.ReleaseError, linux_release.ReleaseError, deb_assets.AssetError) as error:
        print(f"build-release: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
