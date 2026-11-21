"""Build the audited desktop release assets from a public port checkout.

With --discs, the player's US Disc 1 image: the Windows and Linux assets, each
executable through the payload audit before it is packaged, and the release's
link tables, xenogears-pc-<version>-link-tables.tar.gz: what a checkout links
from instead of the disc (port/CMakeLists.txt XG_LINK_FROM), as addresses,
sizes, offsets and hashes, never game bytes. For the tables both Linux engines
are built once more in the builder with the release's configuration, audited
and required to be the bytes just packaged; each one's SHA-256 goes with the
tables, beside a digest of the sources they were built from (executable.json).

With --discs and --write-tables DIR: those tables as files in DIR and nothing
else (for a checkout of one's own: DIR is its port/link-tables), for the US
release or, with --region jp and the JP Disc 1 image in --discs, the JP one.

Without --discs: the Linux executables of one region (--region, default us),
built in the same builder from link tables: --tables FILE, a release's
link-tables.tar.gz, else the checkout's own port/link-tables. No disc is read
and none is needed. It is not the release: there is no payload audit without
the images, so nothing is packaged. The executables go into a new folder of
--out with their SHA-256 beside it, and each is compared with the one the
tables record: with the same sources it must be the same bytes, which passed
the audit when the tables were made (a difference is a failure). With other
sources there is nothing to compare, which --reproduce turns into a failure.
The Windows executables are not built this way yet: their compiler is the
host's. Only the builder is needed, and the default cache gets one when it has
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
PYTHON_TOOLS = ("splat64[mips]", "spimdisasm", "rabbitizer", "pyyaml")
DEFAULT_CACHE = Path(os.environ.get("XDG_CACHE_HOME") or Path.home() / ".cache") / "xenogears-pc"
REGIONS = ("us", "jp")
ENGINES = {"original": "bin/xenogears", "enhanced": "bin/xenogears-enhanced"}  # the builds' XG_ENGINE: installed as
TABLES_CONFIG = "{engine}-linux-x86_64"  # port/CMakeLists.txt XG_TABLES_CONFIG, in the builder
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
    fetch_builder(cache, ubuntu)


def fetch_builder(cache: Path, ubuntu: Path) -> None:
    """The Ubuntu builder's root filesystem: all a build without a disc needs."""
    cache.mkdir(parents=True, exist_ok=True)
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


def write_tables(work: Path, tree: Path, python: Path, ubuntu: Path, tag: str, digest: str,
                 tarball: Path | None = None, region: str = "us") -> Path:
    """The link tables of one region's Linux executables (both engines), gathered and scanned; their folder.
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
        record = {"version": 1, "file": ENGINES[engine], "sha256": sha256(exe), "sources": digest}
        print(f"Link tables: {region} {engine} {record['sha256']} ({count} module images; {verdict})", flush=True)
        if packaged is not None and packaged.get(ENGINES[engine]) != record["sha256"]:
            raise ReleaseError(f"the table build's {ENGINES[engine]} is not the bytes packaged in {tarball.name}")
        (folder / "link-tables" / RECORD).write_text(json.dumps(record, sort_keys=True) + "\n")
        builds.append(folder)
    staged = work / TABLES_FOLDER
    run(*host_sandbox(work, tree, python), python / "bin/python3", "-E", "-s", "-B", tree / "tools/link_tables.py",
        *builds, "--dest", staged)
    return staged


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


def compare(made: dict[str, str], tables: Path, digest: str, reproduce: bool, what: str) -> list[str]:
    """Each executable made (engine: SHA-256) against the one its tables record; the lines to print, or
    ReleaseError when the same sources gave other bytes, or with reproduce when there is nothing to compare."""
    lines, failures = [], []
    for engine, got in made.items():
        try:
            record = json.loads((tables / TABLES_CONFIG.format(engine=engine) / RECORD).read_text())
            want, sources = record["sha256"], record["sources"]
        except (OSError, ValueError, KeyError, TypeError):
            want = sources = None
        if sources != digest:
            lines.append(f"{ENGINES[engine]} {got}: nothing to compare (the tables record "
                         + ("no executable)" if sources is None else "an executable of other sources)"))
            if reproduce:
                failures.append(f"--reproduce: the tables record no {ENGINES[engine]} built from these sources")
        elif want == got:
            lines.append(f"{ENGINES[engine]} {got}: the bytes of the disc-backed build these tables were made with "
                         "(payload audit passed there)")
        else:
            failures.append(f"{ENGINES[engine]} is {got}, but the tables record {want} for these same sources: "
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


def build_free(args, cache: Path, ubuntu: Path, tag: str) -> None:
    """The Linux executables of one region from the checkout's link tables: no disc is read."""
    folder = linux_release.release_names(tag)["folder"]
    names = [folder, f"{folder}.sha256"]
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
    if not (ubuntu / "usr/bin/makensis").is_file():
        raise ReleaseError(f"the build cache has no builder. Run bash build-release.sh --fetch-only --cache {cache}")
    if not shutil.which("bwrap"):
        raise ReleaseError("the build needs bubblewrap")
    print("No disc: the Linux executables from the link tables, not audited here and not packaged. "
          "The Windows executables are not built this way yet.", flush=True)

    scratch = args.scratch or next(parent for parent in out.parents if parent.is_dir())
    with tempfile.TemporaryDirectory(prefix=".xenogears-release-", dir=scratch) as temp:
        work = Path(temp)
        tree, build_dir = work / "tree", work / "linux"
        build_dir.mkdir()
        release_inputs.copy_tree(ROOT, tree, work, cache, out)
        digest = release_inputs.tree_digest(tree)
        if args.tables is not None:
            unpack_tables(args.tables.resolve(), tree)
            if not (tree / release_inputs.TABLES / args.region / "asm-layout.json").is_file():
                raise ReleaseError(f"--tables {args.tables}: it holds no tables for {args.region}")
        builder(ubuntu, tree, build_dir, "python3", "-I", "-B", "/work/source/tools/pc_sources.py", writable=True)
        builder(ubuntu, tree, build_dir, "/bin/bash", "/work/source/packaging/build-linux-executables.sh",
                args.region, tag)
        stage = build_dir / f"{args.region}-stage"
        made = {engine: sha256(exe) for engine, exe in staged_executables(stage).items()}
        verdicts = compare(made, tree / release_inputs.TABLES / args.region, digest, args.reproduce,
                           "the build is not reproduced (another builder image?)")
        sums = "".join(f"{digest_}  {folder}/{ENGINES[engine]}\n" for engine, digest_ in made.items())
        check_out(out, names)
        out.mkdir(parents=True, exist_ok=True)
        written = []
        try:
            written.append(out / folder)
            shutil.copytree(stage, out / folder)
            written.append(out / names[1])
            with (out / names[1]).open("x") as stream:
                stream.write(sums)
        except OSError as error:
            for path in written:
                shutil.rmtree(path) if path.is_dir() else path.unlink(missing_ok=True)
            raise ReleaseError(f"writing into --out {out}: {error}") from error
    for line in verdicts:
        print(line)
    for path in written:
        print(path)


def build_tables(args, cache: Path, python: Path, ubuntu: Path, tag: str) -> None:
    """The link tables of one region's Linux executables, from the disc, into --write-tables; no asset."""
    discs = args.discs.resolve()
    image = release_inputs.disc_image(ROOT, discs, args.region)
    dest = args.write_tables.absolute()
    if dest.is_symlink() or (dest.exists() and not dest.is_dir()):
        raise ReleaseError(f"--write-tables {dest}: expected a folder")
    for folder in (discs, cache):
        if dest.resolve().is_relative_to(folder) or folder.is_relative_to(dest.resolve()):
            raise ReleaseError(f"--write-tables {dest} overlaps {folder}")
    if not (python / "bin/python3").is_file() or not (ubuntu / "usr/bin/makensis").is_file():
        raise ReleaseError(f"the build cache is incomplete. Run bash build-release.sh --fetch-only --cache {cache}")
    if not shutil.which("bwrap"):
        raise ReleaseError("the build needs bubblewrap")
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
        install_tables(write_tables(work, tree, python, ubuntu, tag, digest, region=args.region), dest)


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
        digest = release_inputs.tree_digest(tree)
        build_windows(work, tree, cache, python, ubuntu, discs, tag, pending, makensis)
        build_linux(work, tree, python, ubuntu, image, tag, deb, pending)
        tarball = pending / linux_release.release_names(tag)["tarball"]
        pack_tables(write_tables(work, tree, python, ubuntu, tag, digest, tarball), pending, tag)
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
    parser.add_argument("--reproduce", action="store_true",
                        help="without --discs: fail unless each executable is the one the tables record")
    parser.add_argument("--version", help="release version YYYY.MM.N, with optional leading v")
    parser.add_argument("--out", type=Path, help="new or existing output folder (default ./release)")
    parser.add_argument("--scratch", type=Path, help="existing scratch parent (default output's parent)")
    parser.add_argument("--makensis", help="use this compiler instead of the cached one")
    args = parser.parse_args()
    if args.fetch_only and any((args.discs, args.version, args.out, args.scratch, args.makensis)):
        parser.error("--fetch-only accepts only --cache")
    if args.fetch_only and any((args.write_tables, args.tables, args.region, args.reproduce)):
        parser.error("--fetch-only accepts only --cache")
    if args.discs and (args.tables or args.reproduce or (args.region and not args.write_tables)):
        parser.error("--tables and --reproduce are for a build without --discs, --region for one or for --write-tables")
    if not args.discs and (args.write_tables or args.makensis):
        parser.error("--write-tables and --makensis need --discs")
    if args.write_tables and any((args.version, args.out, args.makensis)):
        parser.error("--write-tables makes the tables only: it takes --discs, --cache and --scratch")
    try:
        cache = (args.cache or DEFAULT_CACHE).expanduser().resolve()
        pins = python_pins()
        python, ubuntu = cached_tools(cache, pins)
        if args.fetch_only:
            fetch(cache, pins, python, ubuntu)
        else:
            implicit = args.cache is None and args.version is None and args.out is None
            args.out = args.out or Path("release")
            version = args.version or datetime.datetime.now(datetime.timezone.utc).strftime("%Y.%m.0")
            tag = f"v{version.removeprefix('v')}"
            if args.discs:
                if implicit:
                    fetch(cache, pins, python, ubuntu)
                if args.write_tables:
                    args.region = args.region or "us"
                    build_tables(args, cache, python, ubuntu, tag)
                else:
                    build(args, cache, python, ubuntu, tag)
            else:
                if args.cache is None:  # the default cache gets its builder when it has none; a named one never
                    fetch_builder(cache, ubuntu)
                args.region = args.region or "us"
                build_free(args, cache, ubuntu, tag)
    except (OSError, subprocess.CalledProcessError, ReleaseError, release_inputs.InputError,
            windows_release.ReleaseError, linux_release.ReleaseError, deb_assets.AssetError) as error:
        print(f"build-release: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
