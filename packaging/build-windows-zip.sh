#!/usr/bin/env bash
# The portable Windows .zip of xenogears-pc and its .sha256, built offline from
# this checkout, the player's own disc and a folder of the pinned dependency
# tarballs, with both engines, every installed executable through the payload
# audit.
#
#   bash packaging/build-windows-zip.sh --discs DIR --cache DIR --version vYYYY.MM.N --out OUT \
#       [--region us|jp] [--scratch DIR]
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
# --out     where xenogears-pc-<version>-windows-x64.zip and its .sha256 go
#           (made if missing). It must not hold either name: nothing there is
#           ever overwritten or deleted.
# --region  the disc's region (default us).
# --scratch the folder the private work folder is made in (default $TMPDIR or
#           /tmp); it needs room for a full build, the libraries included. The
#           work folder is removed at the end, whatever happens.
#
# Steps, each a refusal (exit 1, nothing new in OUT) when it fails:
# 1. the tag's names, and OUT free of both;
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
# 7. windows_release.py zip and sha256 in the work folder; the .zip must hold
#    the stage's files, its executables the audited bytes, and no map or seal;
#    the .sha256 the .zip's SHA-256;
# 8. both copied into OUT as new files (the .zip first), checked again there;
#    on a failure the ones copied are removed.
#
# The game still reads the player's disc at every start; nothing of the disc
# is packaged. This makes the
# .zip only: no setup.exe, no signing, no upload, no Windows run.
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/.." && pwd)"
PY="$(command -v python3 || true)"

die() { printf 'build-windows-zip: %s\n' "$*" >&2; exit 1; }
log() { printf '==> %s\n' "$*"; }

DISCS= CACHE= TAG= OUT= REGION=us SCRATCH_PARENT="${TMPDIR:-/tmp}"
while [ $# -gt 0 ]; do
    case "$1" in
        --discs|--cache|--version|--out|--region|--scratch)
            [ $# -ge 2 ] || die "$1 needs a value"
            case "$1" in
                --discs) DISCS="$2" ;; --cache) CACHE="$2" ;; --version) TAG="$2" ;;
                --out) OUT="$2" ;; --region) REGION="$2" ;; --scratch) SCRATCH_PARENT="$2" ;;
            esac
            shift 2 ;;
        -h|--help) sed -n '2,/^set -euo/{/^set -euo/d;s/^# \{0,1\}//;p}' "$0"; exit 0 ;;
        *) die "unknown argument $1" ;;
    esac
done
[ -n "$DISCS" ] && [ -n "$CACHE" ] && [ -n "$TAG" ] && [ -n "$OUT" ] || die "--discs, --cache, --version and --out are required"
case "$REGION" in us|jp) ;; *) die "--region takes us or jp" ;; esac
[ -n "$PY" ] || die "no python3"
for f in tools/deps.py tools/deps.json tools/extract.py tools/configure.py tools/build_windows.sh \
         tools/payloadcheck.py "config/$REGION/disc.yaml" packaging/windows_release.py; do
    [ -f "$ROOT/$f" ] || die "no $f beside packaging/ (run this from a xenogears-pc checkout)"
done
[ -d "$DISCS" ] || die "--discs $DISCS: not a folder"
[ -d "$CACHE" ] || die "--cache $CACHE: not a folder"
[ -d "$SCRATCH_PARENT" ] || die "--scratch $SCRATCH_PARENT: not a folder"
DISCS="$(cd "$DISCS" && pwd)"
CACHE="$(cd "$CACHE" && pwd)"
SCRATCH_PARENT="$(cd "$SCRATCH_PARENT" && pwd)"
OUT_ABS="$("$PY" -I -c 'import os, sys; print(os.path.abspath(sys.argv[1]))' "$OUT")"

# The checks, in Python, run isolated (-I) with the checkout's own files named explicitly.
read -r -d '' PYLIB <<'PYEOF' || true
import hashlib, json, os, re, shutil, subprocess, sys, zipfile
from pathlib import Path, PurePosixPath

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
IMAGE_ENTRY = re.compile(r"\{name:\s*(\w+),([^}]*)\}")
NAME_KEY = re.compile(r"\bname\s*:")
HEADING = re.compile(r"retail module data in (.+) \((US|JP)\): ([0-9]+) module images, runs of >= ([0-9]+) bytes")
OK = re.compile(r"payload ok: generated 0 runs/0 bytes, port 0/0, other 0/0; library 0/0; "
                r"allowed [0-9]+/[0-9]+, symbol names [0-9]+/[0-9]+, src \(game C's own data\) 0/0")
STRIP = re.compile(r'execute_process\(COMMAND ("[^"]*"(?: "[^"]*")*)\)')
STAGE_FILES = {"xenogears.exe", "xenogears-enhanced.exe", "SDL3.dll", "COPYING.txt", "README.txt",
               "LICENSE-SDL3.txt", "LICENSE-imgui.txt", "LICENSE-Jukebox-font.txt", "LICENSE-glslang.txt",
               "LICENSE-dav1d.txt"}
EXECUTABLES = set(AUDIT_TARGETS.values())

def names(root, tag):
    try:
        n = release(root).release_names(tag)
    except Exception as e:
        fail(str(e))
    print(n["zip"])

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

def copy_tree(root, tree, work):
    root, tree, work = Path(root).resolve(), Path(tree), Path(work).resolve()
    skip_top = {"build", "disc", "bin", "work", ".git", ".venv", ".beads"}
    def ignore(folder, entries):
        rel = Path(folder).resolve().relative_to(root)
        out = {e for e in entries if e == "__pycache__" or (Path(folder) / e).resolve() == work}
        if rel == Path("."):
            out |= {e for e in entries if e in skip_top}
        if rel == Path("tools"):
            out |= {e for e in entries if e.startswith("toolchain")}
        return out
    shutil.copytree(root, tree, symlinks=True, ignore=ignore)

def image(root, discs, region):
    text = (Path(root) / "config" / region / "disc.yaml").read_text(encoding="utf-8")
    m = re.search(r"^image:\s*(.+?)\s*$", text, re.M)
    if not m:
        fail(f"config/{region}/disc.yaml names no image")
    base = PurePosixPath(m.group(1)).name
    found = [p for p in (Path(discs) / base, Path(discs) / PurePosixPath(base).stem / base) if p.is_file()]
    if len(found) != 1:
        fail(f"--discs {discs}: {'no' if not found else 'two copies of'} {base}")
    print(found[0])

def modules(tree, region):
    tree = Path(tree)
    text = (tree / "config" / region / "disc.yaml").read_text(encoding="utf-8")
    entries = IMAGE_ENTRY.findall(text)
    if not entries or len(NAME_KEY.findall(text)) != len(entries):
        fail(f"config/{region}/disc.yaml: module entries not of the one form the auditor reads")
    want = {f"{n}.exe" if "iso:" in rest else f"{n}.bin" for n, rest in entries}
    if len(want) != len(entries):
        fail(f"config/{region}/disc.yaml lists a module twice")
    folder = tree / "disc" / region
    have = {p.name for p in folder.iterdir()} if folder.is_dir() else set()
    if have != want or any((folder / n).is_symlink() or not (folder / n).is_file() or (folder / n).stat().st_size == 0
                           for n in want):
        fail(f"extraction incomplete or unexpected: missing {sorted(want - have)}, extra {sorted(have - want)}")
    print(len(want))

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
        if listed != [f"{top}/"] + [f"{top}/{f}" for f in files]:
            fail(f"{archive.name}: entries {listed} are not the stage's")
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
    if sidecar.read_text() != f"{sha(archive)}  {archive.name}\n":
        fail(f"{sidecar.name}: not the .zip's SHA-256")
    print(f"sha256: {sidecar.read_text().strip()}")

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

cmd, args = sys.argv[1], sys.argv[2:]
{"names": names, "free": free, "outside": outside, "cache": cache, "copy-tree": copy_tree, "image": image,
 "modules": modules, "linked": linked, "strip-argv": strip_argv, "audit": audit, "same-seal": same_seal,
 "map-is": map_is, "stage": stage, "zipcheck": zipcheck, "only": only, "promote": promote}[cmd](*args)
PYEOF
py() { "$PY" -I -c "$PYLIB" "$@"; }

ZIP="$(py names "$ROOT" "$TAG")"
log "Release $TAG: $ZIP and $ZIP.sha256 into $OUT_ABS"
py free "$OUT_ABS" "$ZIP" "$ZIP.sha256"
py outside "$OUT_ABS" "$DISCS" "$CACHE"

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

log "Private copy of the checkout"
py copy-tree "$ROOT" "$TREE" "$WORK"
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
log "Windows dist build of both engines (SOURCE_DATE_EPOCH=0)"
XG_WIN_BUILD="$BUILD_O" XG_WIN_BUILD_ENHANCED="$BUILD_E" \
    bash "$TREE/tools/build_windows.sh" "${regionflag[@]}" --dist --engine both "$STAGE" </dev/null \
    || die "tools/build_windows.sh --dist failed"
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
    py audit "$TREE" "$build/$name" "$build" "$REGION" "$COUNT"
    strip_cmd=()
    while IFS= read -r a; do strip_cmd+=("$a"); done < <(py strip-argv "$sealed")
    copy="$AUDIT/all/$name"
    cp "$build/$name" "$copy"
    chmod u+w "$copy"
    log "$target: the install's strip (${strip_cmd[*]}) on a private copy"
    "${strip_cmd[@]}" "$copy" </dev/null || die "$target: strip failed"
    cmp "$copy" "$STAGE/$name" || die "$target: the staged $name is not the reproducible strip of the verified linked executable"
    log "$target: staged $name = the reproducible strip ($("$PY" -I -c 'import hashlib, sys; print(hashlib.sha256(open(sys.argv[1], "rb").read()).hexdigest())' "$copy"))"
    py same-seal "$build" "$target" "$sealed"
    cp "$build/$name.map" "$copy.map"
    py map-is "$copy.map" "$sealed"
    "$PY" -E -s -B "$TREE/tools/payloadcheck.py" --record-map "$copy" </dev/null || die "$target: sealing the audit copy failed"
    chmod a-w "$copy"
    log "$target: the installed bytes"
    py audit "$TREE" "$copy" "$build" "$REGION" "$COUNT"
    cmp "$copy" "$STAGE/$name" || die "$target: the staged $name changed after its audit"
done

log "Zip and checksum"
mkdir -p "$ASSETS"
"$PY" -I "$ROOT/packaging/windows_release.py" zip --tag "$TAG" --stage "$STAGE" --out-dir "$ASSETS" </dev/null >/dev/null \
    || die "windows_release.py zip failed"
"$PY" -I "$ROOT/packaging/windows_release.py" sha256 --tag "$TAG" "$ASSETS/$ZIP" </dev/null >/dev/null \
    || die "windows_release.py sha256 failed"
py only "$ASSETS" "$ZIP" "$ZIP.sha256"
py zipcheck "$ROOT" "$TAG" "$ASSETS/$ZIP" "$ASSETS/$ZIP.sha256" "$STAGE" "$AUDIT/all"

made_out=0
if [ ! -e "$OUT_ABS" ]; then mkdir -p "$OUT_ABS"; made_out=1; fi
py free "$OUT_ABS" "$ZIP" "$ZIP.sha256"
py promote "$ASSETS" "$OUT_ABS" "$made_out" "$ZIP" "$ZIP.sha256"
log "Done (the portable .zip only: no setup.exe, no signing, no upload)"
