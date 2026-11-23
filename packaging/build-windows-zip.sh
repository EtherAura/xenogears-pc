#!/usr/bin/env bash
# The Windows .zip and optional NSIS installer of xenogears-pc, built offline
# from this checkout, the player's own disc and the pinned dependency tarballs.
# Both engines and SDL3 go through the payload audit before packaging.
#
#   bash packaging/build-windows-zip.sh --discs DIR --cache DIR --version vYYYY.MM.N --out OUT \
#       [--region us|jp] [--scratch DIR] [--installer [--makensis EXE]] [--builder ROOTFS] [--tables-out DIR]
#
# --discs   the folder holding the player's Disc 1 image, under the name
#           config/<region>/disc.yaml gives it (directly, or in a folder of its
#           own name); it is only read.
# --cache   the folder holding the tarballs tools/deps.json pins (SDL3, glslang,
#           dav1d). Checked with tools/deps.py check before anything is built;
#           each must resolve inside this folder with its pinned SHA-256, and the
#           builders get it as XG_DEPS_CACHE. Nothing is fetched: a missing or
#           corrupt tarball is a refusal (python tools/deps.py fetch --cache DIR
#           is a separate step, with network).
# --version the release tag, as packaging/windows_release.py names reads it.
# --out     where the assets and their checksums go (made if missing).
#           The requested names must be free. Existing files are preserved.
# --installer  also make setup.exe, its .sha256 and the NSIS build record.
#              Requires makensis 3.10 or later before the build starts.
# --makensis   compiler for --installer (default: makensis on PATH).
# --region  the disc's region (default us).
# --scratch the folder the private work folder is made in (default $TMPDIR or
#           /tmp); it needs room for a full build, the libraries included. The
#           work folder is removed at the end, whatever happens.
# --builder the root filesystem of the release's Windows builder
#           (packaging/mingw_debian.Dockerfile, which build-release.sh keeps in
#           its cache): the build, both payload audits of each executable, the
#           strip and the map's seal run inside it under bubblewrap, with no
#           network, the work folder at /work/w and --cache at /work/cache.
#           Without it they run on this machine with its own MinGW-w64.
# --tables-out  also write each engine's link tables (port/CMakeLists.txt
#           XG_WRITE_LINK_TABLES) into DIR/<engine>/link-tables, with the
#           SHA-256 of its audited installed bytes, the ones the .zip holds, in
#           DIR/<engine>/installed.sha256 (build-release.sh gathers them).
#
# Steps, each a refusal (exit 1, nothing new in OUT) when it fails:
# 1. the tag's names, OUT free of those assets, and installer compiler preflight;
# 2. the cache check;
# 3. a private copy of this checkout (without build/, disc/, bin/, work/,
#    .git, .venv or tools/toolchain*) with its own disc/<region>, extracted from
#    the image by tools/extract.py --check-image: the module list of disc.yaml,
#    every file, nothing else;
# 4. tools/configure.py <region>, then tools/build_windows.sh --dist for both
#    engines into a stage, with SOURCE_DATE_EPOCH=0 throughout (the linker's and
#    strip's PE time stamps), so the install's strip can be reproduced. The
#    libraries are built into the private copy from the checked cache;
# 5. the stage: exactly the files the install makes (both engines, SDL3.dll,
#    the licences of every library, glslang's and dav1d's included), and no
#    other executable;
# 6. per engine and SDL3: its Release CMake build and the
#    linked executable against the seal of its link map (<exe>.payload.json:
#    the executable's and the map's SHA-256); tools/payloadcheck.py --dist on it;
#    a private copy of that verified executable stripped by the command and
#    arguments the build's cmake_install.cmake runs, which must equal the
#    staged executable byte for byte; only then the verified map beside that
#    copy, sealed for it by payloadcheck.py --record-map, and payloadcheck.py
#    --dist on the copy: the installed bytes' audit. An audit passes only on
#    exit 0, a heading naming the copy, the region and every module image, and
#    one clean "payload ok" line;
# 7. package the audited stage in the work folder. Check that the .zip holds
#    the stage's files and its executables match the audited copies. Check the
#    asset checksums and the installer compiler record when requested;
# 8. copy the verified assets into OUT as new files and verify the copies.
#    If a copy fails, remove the new files made during that attempt.
#
# The game reads the player's disc at every start. This helper builds unsigned
# release assets for local use.
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/.." && pwd)"
PY="$(command -v python3 || true)"

die() { printf 'build-windows-zip: %s\n' "$*" >&2; exit 1; }
log() { printf '==> %s\n' "$*"; }

DISCS= CACHE= TAG= OUT= REGION=us SCRATCH_PARENT="${TMPDIR:-/tmp}" INSTALLER=0 MAKENSIS= BUILDER= TABLES_OUT=
while [ $# -gt 0 ]; do
    case "$1" in
        --discs|--cache|--version|--out|--region|--scratch|--makensis|--builder|--tables-out)
            [ $# -ge 2 ] || die "$1 needs a value"
            case "$1" in
                --discs) DISCS="$2" ;; --cache) CACHE="$2" ;; --version) TAG="$2" ;;
                --out) OUT="$2" ;; --region) REGION="$2" ;; --scratch) SCRATCH_PARENT="$2" ;;
                --makensis) [ -n "$2" ] || die "--makensis needs an executable"; MAKENSIS="$2" ;;
                --builder) BUILDER="$2" ;; --tables-out) TABLES_OUT="$2" ;;
            esac
            shift 2 ;;
        --installer) INSTALLER=1; shift ;;
        -h|--help) sed -n '2,/^set -euo/{/^set -euo/d;s/^# \{0,1\}//;p}' "$0"; exit 0 ;;
        *) die "unknown argument $1" ;;
    esac
done
[ -n "$DISCS" ] && [ -n "$CACHE" ] && [ -n "$TAG" ] && [ -n "$OUT" ] || die "--discs, --cache, --version and --out are required"
case "$REGION" in us|jp) ;; *) die "--region takes us or jp" ;; esac
[ -z "$MAKENSIS" ] || [ "$INSTALLER" = 1 ] || die "--makensis requires --installer"
[ -n "$PY" ] || die "no python3"
for f in tools/deps.py tools/deps.json tools/extract.py tools/configure.py tools/build_windows.sh \
         tools/payloadcheck.py "config/$REGION/disc.yaml" packaging/windows_release.py packaging/release_inputs.py; do
    [ -f "$ROOT/$f" ] || die "no $f beside packaging/ (run this from a xenogears-pc checkout)"
done
[ -d "$DISCS" ] || die "--discs $DISCS: not a folder"
[ -d "$CACHE" ] || die "--cache $CACHE: not a folder"
[ -d "$SCRATCH_PARENT" ] || die "--scratch $SCRATCH_PARENT: not a folder"
DISCS="$(cd "$DISCS" && pwd)"
CACHE="$(cd "$CACHE" && pwd)"
SCRATCH_PARENT="$(cd "$SCRATCH_PARENT" && pwd)"
if [ -n "$BUILDER" ]; then
    [ -d "$BUILDER/work" ] && [ -x "$BUILDER/usr/bin/x86_64-w64-mingw32-gcc-posix" ] \
        || die "--builder $BUILDER: not the Windows builder's root filesystem (build-release.sh --fetch-only makes it)"
    command -v bwrap >/dev/null || die "--builder needs bubblewrap"
    BUILDER="$(cd "$BUILDER" && pwd)"
fi
if [ -n "$TABLES_OUT" ]; then
    [ -d "$TABLES_OUT" ] && [ -z "$(ls -A "$TABLES_OUT")" ] || die "--tables-out $TABLES_OUT: not an empty folder"
    TABLES_OUT="$(cd "$TABLES_OUT" && pwd)"
fi
OUT_ABS="$("$PY" -I -c 'import os, sys; print(os.path.abspath(sys.argv[1]))' "$OUT")"

# The checks, in Python, run isolated (-I) with the checkout's own files named explicitly.
read -r -d '' PYLIB <<'PYEOF' || true
import hashlib, json, os, re, subprocess, sys, zipfile
from pathlib import Path

def fail(msg):
    print(f"build-windows-zip: {msg}", file=sys.stderr)
    sys.exit(1)

def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()

def release(root):
    sys.path.insert(0, str(Path(root) / "packaging"))
    import windows_release
    return windows_release

AUDIT_TARGETS = {"original": "xenogears.exe", "enhanced": "xenogears-enhanced.exe", "sdl3": "SDL3.dll"}
HEADING = re.compile(r"retail module data in (.+) \((US|JP)\): ([0-9]+) module images, runs of >= ([0-9]+) bytes")
OK = re.compile(r"payload ok: generated 0 runs/0 bytes, port 0/0, other 0/0; library 0/0; "
                r"allowed [0-9]+/[0-9]+, symbol names [0-9]+/[0-9]+, src \(game C's own data\) 0/0")
STRIP = re.compile(r'execute_process\(COMMAND ("[^"]*"(?: "[^"]*")*)\)')
STAGE_FILES = {"xenogears.exe", "xenogears-enhanced.exe", "SDL3.dll", "COPYING.txt", "README.txt",
               "LICENSE-SDL3.txt", "LICENSE-imgui.txt", "LICENSE-Jukebox-font.txt", "LICENSE-glslang.txt",
               "LICENSE-dav1d.txt"}
EXECUTABLES = set(AUDIT_TARGETS.values())

def names(root, tag, installer):
    try:
        win = release(root)
        n = win.release_names(tag)
    except Exception as e:
        fail(str(e))
    files = win.package_names(n) if installer == "1" else [n["zip"], n["zip"] + ".sha256"]
    print(n["zip"], *files, sep="\n")

def compiler(root, makensis):
    win = release(root)
    try:
        exe = win.find_makensis(makensis)
        version = win.makensis_version(exe)
    except win.ReleaseError as e:
        fail(str(e))
    print(exe, version, sep="\n")

def free(out, *wanted):
    out = Path(out)
    if os.path.lexists(out) and (out.is_symlink() or not out.is_dir()):
        fail(f"--out {out}: not a folder")
    taken = [w for w in wanted if os.path.lexists(out / w)]
    if taken:
        fail(f"--out {out} already holds {', '.join(taken)}; nothing there is overwritten")

def outside(out, *folders):
    out = Path(out).resolve()
    for f in folders:
        f = Path(f).resolve()
        if out == f or out.is_relative_to(f) or f.is_relative_to(out):
            fail(f"--out {out} overlaps {f}")

def cache(root, cache):
    root, cache = Path(root), Path(cache).resolve()
    deps = [sys.executable, "-E", "-s", "-B", str(root / "tools" / "deps.py")]
    env = {k: v for k, v in os.environ.items() if k != "XG_DEPS_CACHE"}
    check = subprocess.run(deps + ["check", "--cache", str(cache)], stdin=subprocess.DEVNULL, env=env,
                           capture_output=True, text=True)
    sys.stdout.write(check.stdout)
    if check.returncode != 0:
        fail(f"dependency cache {cache}: deps.py check exit {check.returncode}: {(check.stdout + check.stderr).strip()[-400:]}; "
             "nothing fetched, nothing built")
    pins = json.loads((root / "tools" / "deps.json").read_text())
    if not pins:
        fail("tools/deps.json pins nothing")
    for name, pin in sorted(pins.items()):
        got = subprocess.run(deps + ["path", name, "--cache", str(cache)], stdin=subprocess.DEVNULL, env=env,
                             capture_output=True, text=True)
        path = Path(got.stdout.strip()).resolve() if got.returncode == 0 and got.stdout.strip() else None
        if path is None or path.parent != cache or path.name != pin["file"]:
            fail(f"dependency cache {cache}: {name} does not resolve there ({got.stdout.strip() or got.stderr.strip()}); "
                 "nothing fetched, nothing built")
        if sha(path) != pin["sha256"]:
            fail(f"dependency cache {cache}: {path.name} is not the pinned SHA-256; nothing fetched, nothing built")
        print(f"cache: {name} {pin['version']} {pin['sha256']} {path.name}")

def copy_tree(root, tree, work, cache):
    release_inputs.copy_tree(root, tree, work, cache)

def image(root, discs, region):
    print(release_inputs.disc_image(root, discs, region))

def modules(tree, region):
    print(release_inputs.module_count(tree, region))

def cache_value(text, key):
    m = re.search(rf"^{key}:\w+=(.*?)\s*$", text, re.M)
    return m.group(1) if m else None

def linked(build, target, region):
    build = Path(build)
    name = AUDIT_TARGETS[target]
    exe, mapping, seal = build / name, build / f"{name}.map", build / f"{name}.payload.json"
    try:
        text = (build / "CMakeCache.txt").read_text(encoding="utf-8", errors="replace")
    except OSError:
        fail(f"{target}: no CMakeCache.txt in its build")
    expected = {"CMAKE_BUILD_TYPE": "Release"}
    if target != "sdl3":
        expected.update(XG_VERSION=region.upper(), XG_ENGINE=target, XG_DIST="ON")
    for key, want in expected.items():
        if (cache_value(text, key) or "").upper() != want.upper():
            fail(f"{target}: its build has {key}={cache_value(text, key)}, not {want}")
    for p in (exe, mapping, seal):
        if p.is_symlink() or not p.is_file():
            fail(f"{target}: no {p.name} in its build")
    try:
        sealed = json.loads(seal.read_text())
    except ValueError:
        fail(f"{target}: {seal.name} is not a seal")
    if sealed != {"version": 1, "exe": sha(exe), "map": sha(mapping)}:
        fail(f"{target}: the linked {name} and its map do not match their seal {seal.name}")
    try:
        install = (build / "cmake_install.cmake").read_text(encoding="utf-8")
    except OSError:
        fail(f"{target}: no cmake_install.cmake in its build")
    directory = "bin" if target == "sdl3" else "."
    destination = '"$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/' + f'{directory}/{name}"'
    found = [m.group(1) for m in STRIP.finditer(install) if m.group(1).endswith(" " + destination)]
    if len(found) != 1:
        fail(f"{target}: cmake_install.cmake has {len(found)} strip commands for {name}, not one")
    argv = re.findall(r'"([^"]*)"', found[0][:-len(destination)])
    if not argv or "$" in "".join(argv) or not os.path.isabs(argv[0]):
        fail(f"{target}: the install's strip command is not plain")
    print(json.dumps({"exe": sha(exe), "map": sha(mapping), "strip": argv}))

def strip_argv(sealed):
    print("\n".join(json.loads(sealed)["strip"]))

def audit(tree, exe, build, region, count):
    tree, exe = Path(tree), Path(exe)
    before = sha(exe)
    argv = [sys.executable, "-E", "-s", "-B", str(tree / "tools" / "payloadcheck.py"), "--exe", str(exe), "--build", str(build),
            "--ver", region, "--dist"]
    if exe.name == AUDIT_TARGETS["sdl3"]:
        argv.append("--sdl3")
    try:
        proc = subprocess.run(argv, cwd=tree, stdin=subprocess.DEVNULL, capture_output=True, text=True,
                              errors="replace", timeout=3600)
    except (OSError, subprocess.TimeoutExpired) as e:
        fail(f"payload audit of {exe}: {e}")
    lines = proc.stdout.splitlines()
    head = HEADING.fullmatch(lines[0]) if lines else None
    if sha(exe) != before:
        why = "the executable changed during its audit"
    elif proc.returncode != 0:
        why = f"the auditor exited {proc.returncode}"
    elif head is None or head.group(1) != str(exe) or head.group(2) != region.upper():
        why = "the auditor's heading does not name this executable and region"
    elif int(head.group(3)) != int(count) or int(head.group(4)) != 16:
        why = f"the auditor read {head.group(3)} module images (runs of {head.group(4)}), not all {count} (16)"
    elif len(lines) != 2 or not OK.fullmatch(lines[1]):
        why = "the auditor's output is not its heading and one clean payload ok line"
    else:
        print(f"audit {exe} sha256 {before}: exit 0, {count} module images; {lines[1]}")
        return
    said = "".join(f"\n    | {l[:200]}" for l in (lines[-20:] + proc.stderr.splitlines()[-5:]))
    fail(f"payload audit of {exe} FAILED: {why}{said}")

def same_seal(build, target, want):
    build = Path(build)
    name = AUDIT_TARGETS[target]
    want = json.loads(want)
    if sha(build / name) != want["exe"] or sha(build / f"{name}.map") != want["map"]:
        fail(f"{target}: the linked executable or its map changed after its audit")

def map_is(path, want):
    if sha(path) != json.loads(want)["map"]:
        fail(f"{path}: not the sealed map")

def stage(stage):
    stage = Path(stage)
    seen = set()
    for here, dirs, files in os.walk(stage):
        if dirs:
            fail(f"stage: unexpected folder {dirs[0]}")
        for n in files:
            p = Path(here) / n
            if p.is_symlink() or not p.is_file():
                fail(f"stage: {n} is not a regular file")
            if n not in STAGE_FILES:
                fail(f"stage: unexpected file {n}")
            with p.open("rb") as f:
                if f.read(2) == b"MZ" and n not in EXECUTABLES:
                    fail(f"stage: unexpected executable {n}")
            seen.add(n)
    missing = sorted(STAGE_FILES - seen)
    if missing:
        fail(f"stage: no {', '.join(missing)} (both engines and every library are required)")
    for n in sorted(seen):
        print(f"stage: {n} {sha(stage / n)}")

def zipcheck(root, tag, archive, sidecar, stagedir, audited):
    n = release(root).release_names(tag)
    archive, sidecar, stagedir, audited = Path(archive), Path(sidecar), Path(stagedir), Path(audited)
    if archive.name != n["zip"] or sidecar.name != n["zip"] + ".sha256":
        fail("the assets do not have the tag's names")
    files = sorted(p.name for p in stagedir.iterdir())
    top = n["folder"]
    with zipfile.ZipFile(archive) as z:
        if z.testzip() is not None:
            fail(f"{archive.name}: a damaged entry")
        listed = z.namelist()
        own = release(root).RELEASE_FILE  # the .zip's own release.txt, after the stage's files
        if listed != [f"{top}/"] + [f"{top}/{f}" for f in files] + [f"{top}/{own}"]:
            fail(f"{archive.name}: entries {listed} are not the stage's and its {own}")
        if z.read(f"{top}/{own}") != release(root).release_text(n["version"], "zip"):
            fail(f"{archive.name}: {own} is not version {n['version']}, kind zip")
        print(f"zip: {top}/{own} version {n['version']}, kind zip")
        if any(e.endswith((".map", ".payload.json")) for e in listed):
            fail(f"{archive.name}: holds a link map or seal")
        for f in files:
            data = z.read(f"{top}/{f}")
            if hashlib.sha256(data).hexdigest() != sha(stagedir / f):
                fail(f"{archive.name}: {f} is not the staged bytes")
            if f in EXECUTABLES and data != (audited / f).read_bytes():
                fail(f"{archive.name}: {f} is not the audited bytes")
            print(f"zip: {top}/{f} {len(data)} bytes sha256 {hashlib.sha256(data).hexdigest()}"
                  + (" (= audited installed bytes)" if f in EXECUTABLES else ""))
    checksum(archive, sidecar)

def checksum(asset, sidecar):
    if sidecar.read_text() != f"{sha(asset)}  {asset.name}\n":
        fail(f"{sidecar.name}: not the SHA-256 of {asset.name}")
    print(f"sha256: {sidecar.read_text().strip()}")

def installercheck(root, tag, folder, version):
    win = release(root)
    setup = Path(folder) / win.release_names(tag)["setup"]
    if setup.is_symlink() or not setup.is_file():
        fail(f"{setup.name}: missing installer file")
    with setup.open("rb") as f:
        if f.read(2) != b"MZ":
            fail(f"{setup.name}: missing Windows executable header")
    record = setup.with_name(setup.name + ".build.json")
    expected = {"asset": setup.name, "makensis_version": version,
                "makensis_minimum": ".".join(map(str, win.MAKENSIS_MIN))}
    try:
        info = json.loads(record.read_text())
    except (OSError, ValueError) as e:
        fail(f"{record.name}: {e}")
    if info != expected:
        fail(f"{record.name}: expected the preflight makensis version {version} for {setup.name}")
    print(f"installer: {setup.name} compiled with makensis {version}")
    checksum(setup, setup.with_name(setup.name + ".sha256"))

def only(folder, *wanted):
    if sorted(os.listdir(folder)) != sorted(wanted):
        fail(f"{folder} holds {sorted(os.listdir(folder))}, not just {sorted(wanted)}")

def promote(src, out, made_out, *assets):
    src, out = Path(src), Path(out)
    written = []
    try:
        for a in assets:
            data = (src / a).read_bytes()
            fd = os.open(out / a, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o644)
            written.append(out / a)
            with os.fdopen(fd, "wb") as f:
                f.write(data)
                f.flush()
                os.fsync(f.fileno())
            if sha(out / a) != hashlib.sha256(data).hexdigest():
                raise OSError(f"{a} reads back differently")
    except OSError as e:
        for w in written:
            w.unlink(missing_ok=True)
        if made_out == "1":
            try:
                out.rmdir()
            except OSError:
                pass
        fail(f"copying into --out {out}: {e}; removed {', '.join(w.name for w in written) or 'nothing'}")
    for a in assets:
        print(f"out: {out / a} sha256 {sha(out / a)}")

sys.path.insert(0, sys.argv[1])
import release_inputs

cmd, args = sys.argv[2], sys.argv[3:]
try:
    {"names": names, "compiler": compiler, "free": free, "outside": outside, "cache": cache,
     "copy-tree": copy_tree, "image": image,
     "modules": modules, "linked": linked, "strip-argv": strip_argv, "audit": audit, "same-seal": same_seal,
     "map-is": map_is, "stage": stage, "zipcheck": zipcheck, "installercheck": installercheck,
     "only": only, "promote": promote}[cmd](*args)
except release_inputs.InputError as e:
    fail(str(e))
PYEOF
py() { "$PY" -I -B -c "$PYLIB" "$HERE" "$@"; }

requested="$(py names "$ROOT" "$TAG" "$INSTALLER")"
mapfile -t outputs <<< "$requested"
ZIP="${outputs[0]}"
outputs=("${outputs[@]:1}")
log "Release $TAG into $OUT_ABS"
py free "$OUT_ABS" "${outputs[@]}"
py outside "$OUT_ABS" "$DISCS" "$CACHE"
if [ "$INSTALLER" = 1 ]; then
    preflight="$(py compiler "$ROOT" "$MAKENSIS")"
    mapfile -t nsis <<< "$preflight"
    MAKENSIS="${nsis[0]}"
    log "Installer compiler: ${nsis[1]} ($MAKENSIS)"
fi

log "Dependency cache $CACHE (no network)"
py cache "$ROOT" "$CACHE"

WORK="$(mktemp -d "$SCRATCH_PARENT/xenogears-winzip.XXXXXX")"
cleanup() { chmod -R u+w "$WORK" 2>/dev/null || true; rm -rf "$WORK"; }
trap cleanup EXIT
trap 'exit 1' INT TERM HUP
TREE="$WORK/tree"
STAGE="$WORK/stage"
AUDIT="$WORK/audit"
ASSETS="$WORK/assets"
# The private copy has no .git of its own: git there must not find a folder above it.
export GIT_CEILING_DIRECTORIES="$WORK"
mkdir "$WORK/tmp"

# Where the build, the audits and the strip run: the builder (its own root, the work folder at
# /work/w, the cache at /work/cache, no network) or this machine. inpath is a path as they see it.
if [ -n "$BUILDER" ]; then
    inpath() {
        case "$1" in
            "$WORK"|"$WORK"/*) printf '%s' "/work/w${1#"$WORK"}" ;;
            "$CACHE"|"$CACHE"/*) printf '%s' "/work/cache${1#"$CACHE"}" ;;
            *) echo "build-windows-zip: $1 is outside the builder's folders" >&2; return 1 ;;
        esac
    }
    inbuilder() {
        bwrap --die-with-parent --unshare-net --unshare-ipc --clearenv --ro-bind "$BUILDER" / --dev /dev --proc /proc \
            --tmpfs /tmp --tmpfs /work --bind "$WORK" /work/w --ro-bind "$CACHE" /work/cache \
            --setenv HOME /tmp --setenv PATH /usr/bin:/bin --setenv LC_ALL C.UTF-8 --setenv SOURCE_DATE_EPOCH 0 \
            --setenv XG_DEPS_CACHE /work/cache --setenv TMPDIR /work/w/tmp --setenv PYTHONDONTWRITEBYTECODE 1 \
            --setenv GIT_CEILING_DIRECTORIES /work/w --chdir /work/w/tree -- "$@" </dev/null
    }
    BPY=python3
else
    inpath() { printf '%s' "$1"; }
    inbuilder() { "$@" </dev/null; }
    BPY="$PY"
fi
# The checks that read executables (the payload audit) run where the binutils that built them are.
bpy() { inbuilder "$BPY" -I -B -c "$PYLIB" "$(inpath "$TREE/packaging")" "$@"; }

log "Private copy of the checkout"
py copy-tree "$ROOT" "$TREE" "$WORK" "$CACHE"
IMAGE="$(py image "$TREE" "$DISCS" "$REGION")"
log "Extracting the player's disc ($REGION) into the private copy"
"$PY" -E -s -B "$TREE/tools/extract.py" "$REGION" --image "$IMAGE" --check-image </dev/null || die "extraction failed"
COUNT="$(py modules "$TREE" "$REGION")"
log "$COUNT module images extracted"
(cd "$TREE" && "$PY" -E -s -B tools/configure.py "$REGION" </dev/null) || die "tools/configure.py $REGION failed"

export SOURCE_DATE_EPOCH=0
export XG_DEPS_CACHE="$CACHE"
unset TARBALL SDL3_TARBALL || true
BUILD_O="$TREE/build/pc/win-$REGION-dist"
BUILD_E="$TREE/build/pc/win-$REGION-enhanced-dist"
regionflag=()
[ "$REGION" = jp ] && regionflag=(--jp)
tables=()
[ -n "$TABLES_OUT" ] && tables=(--write-tables)
log "Windows dist build of both engines (SOURCE_DATE_EPOCH=0)${BUILDER:+, in the builder}"
inbuilder env XG_WIN_BUILD="$(inpath "$BUILD_O")" XG_WIN_BUILD_ENHANCED="$(inpath "$BUILD_E")" \
    XG_DEPS_CACHE="$(inpath "$CACHE")" SOURCE_DATE_EPOCH=0 \
    bash "$(inpath "$TREE/tools/build_windows.sh")" "${regionflag[@]}" --dist "${tables[@]}" --engine both \
    "$(inpath "$STAGE")" || die "tools/build_windows.sh --dist failed"
py stage "$STAGE"

mkdir -p "$AUDIT/all"
for target in original enhanced sdl3; do
    case "$target" in
        original) build="$BUILD_O"; name=xenogears.exe ;;
        enhanced) build="$BUILD_E"; name=xenogears-enhanced.exe ;;
        sdl3) build="$TREE/tools/toolchain-src/sdl3-mingw/build"; name=SDL3.dll ;;
    esac
    log "$target: the linked $name against its sealed map"
    sealed="$(py linked "$build" "$target" "$REGION")"
    bpy audit "$(inpath "$TREE")" "$(inpath "$build/$name")" "$(inpath "$build")" "$REGION" "$COUNT"
    strip_cmd=()
    while IFS= read -r a; do strip_cmd+=("$a"); done < <(py strip-argv "$sealed")
    copy="$AUDIT/all/$name"
    cp "$build/$name" "$copy"
    chmod u+w "$copy"
    log "$target: the install's strip (${strip_cmd[*]}) on a private copy"
    inbuilder "${strip_cmd[@]}" "$(inpath "$copy")" || die "$target: strip failed"
    cmp "$copy" "$STAGE/$name" || die "$target: the staged $name is not the reproducible strip of the verified linked executable"
    log "$target: staged $name = the reproducible strip ($("$PY" -I -c 'import hashlib, sys; print(hashlib.sha256(open(sys.argv[1], "rb").read()).hexdigest())' "$copy"))"
    py same-seal "$build" "$target" "$sealed"
    cp "$build/$name.map" "$copy.map"
    py map-is "$copy.map" "$sealed"
    inbuilder "$BPY" -E -s -B "$(inpath "$TREE/tools/payloadcheck.py")" --record-map "$(inpath "$copy")" \
        || die "$target: sealing the audit copy failed"
    chmod a-w "$copy"
    log "$target: the installed bytes"
    bpy audit "$(inpath "$TREE")" "$(inpath "$copy")" "$(inpath "$build")" "$REGION" "$COUNT"
    cmp "$copy" "$STAGE/$name" || die "$target: the staged $name changed after its audit"
done

if [ -n "$TABLES_OUT" ]; then
    for target in original enhanced; do
        case "$target" in
            original) build="$BUILD_O"; name=xenogears.exe ;;
            enhanced) build="$BUILD_E"; name=xenogears-enhanced.exe ;;
        esac
        [ -f "$build/link-tables/config.txt" ] || die "$target: its build wrote no link tables"
        mkdir "$TABLES_OUT/$target"
        cp -R "$build/link-tables" "$TABLES_OUT/$target/link-tables"
        "$PY" -I -c 'import hashlib, sys; print(hashlib.sha256(open(sys.argv[1], "rb").read()).hexdigest())' \
            "$AUDIT/all/$name" > "$TABLES_OUT/$target/installed.sha256"
        log "$target: link tables for $(cat "$build/link-tables/config.txt")"
    done
fi

mkdir -p "$ASSETS"
if [ "$INSTALLER" = 1 ]; then
    log "Installer, ZIP and checksums from the audited stage"
    chmod a-w "$STAGE" "$STAGE/"*
    "$PY" -I "$ROOT/packaging/windows_release.py" package --tag "$TAG" --stage "$STAGE" --out-dir "$ASSETS" \
        --makensis "$MAKENSIS" </dev/null >/dev/null || die "windows_release.py package failed"
else
    log "Zip and checksum"
    "$PY" -I "$ROOT/packaging/windows_release.py" zip --tag "$TAG" --stage "$STAGE" --out-dir "$ASSETS" </dev/null >/dev/null \
        || die "windows_release.py zip failed"
    "$PY" -I "$ROOT/packaging/windows_release.py" sha256 --tag "$TAG" "$ASSETS/$ZIP" </dev/null >/dev/null \
        || die "windows_release.py sha256 failed"
fi
py only "$ASSETS" "${outputs[@]}"
py zipcheck "$ROOT" "$TAG" "$ASSETS/$ZIP" "$ASSETS/$ZIP.sha256" "$STAGE" "$AUDIT/all"
if [ "$INSTALLER" = 1 ]; then
    py installercheck "$ROOT" "$TAG" "$ASSETS" "${nsis[1]}"
fi

made_out=0
if [ ! -e "$OUT_ABS" ]; then mkdir -p "$OUT_ABS"; made_out=1; fi
py free "$OUT_ABS" "${outputs[@]}"
py promote "$ASSETS" "$OUT_ABS" "$made_out" "${outputs[@]}"
log "Done"
