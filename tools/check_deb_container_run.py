#!/usr/bin/env python3
"""Check packaging/deb_container_run.py (xenogears-pc): the builder image's launcher.

    python tools/check_deb_container_run.py --ver us

The launcher is run as a command (python -I -B, its real path) against a
CLEARLY LABELLED FIXTURE RUNTIME: an executable that only records the argv it
was given and exits with a chosen status. It is NOT docker, no image exists,
no container runs, nothing is built or packaged; no Ubuntu, Depends,
installation or start is shown. The fixture source trees hold placeholders
(their packaging/*.py are traps that record it if anything runs them on this
host); no .deb or other package file is made.

1. STATIC: the launcher imports only the standard library and passes the
   publisher's date/time/private scan.
2. EXECUTABLE, the real launcher (paths and tag full of shell metacharacters,
   which must stay inert): with exit statuses 0, 1, 2, 7 and 125 from the
   fixture, the exact documented argv (image, tag, the three bind mounts,
   source read-only, linux/amd64, --pull never, --network none, no
   capabilities, no new privileges, nothing else) and the status come back
   unchanged, the failure says what --out-dir holds; a missing runtime exits
   4; each refusal (missing or invalid source, the working clone's markers,
   a disc image file, the home folder, missing build or output folder, a
   non-empty or symlinked build folder, overlapping folders, a comma, double
   quote or newline in a path, an invalid tag or image) exits 3 before the
   runtime is started; command line errors exit 2. Each time the source,
   build and existing output folders (bytes and mtimes) are unchanged and no
   trap ran.
3. MUTATIONS: isolated scratch copies of the launcher with one safeguard
   removed each (network, read-only source, no pull, privilege, capabilities,
   the delimiter, marker, overlap, empty-build, tag and image checks, shell
   use, the exit statuses, helpers loaded from --source, an extra mount)
   must each fail part 2. The real launcher is never changed.

The launcher does not depend on the game version; --ver only names the
scratch folder, build/pc/deb-container-run-<ver>/. No Docker, root or network.
"""
from __future__ import annotations

import argparse
import ast
import importlib.util
import json
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parent.parent
PACKAGING = ROOT / "tools/publish/repo-port/packaging"
LAUNCHER = PACKAGING / "deb_container_run.py"
HELPERS = ("deb_ubuntu.py", "deb_build.py", "deb_assets.py")
IMAGE = "xenogears-pc-deb:fixture"
ODD = "x; touch PWNED $(touch PWNED2) `touch PWNED3` 'q' & | *"

spec = importlib.util.spec_from_file_location("check_deb_build", ROOT / "tools/check_deb_build.py")
cdb = importlib.util.module_from_spec(spec)
spec.loader.exec_module(cdb)
check, snapshot, TAG = cdb.check, cdb.snapshot, cdb.TAG

FIXTURE_RUNTIME = '''#!{python} -I
# FIXTURE RUNTIME (tools/check_deb_container_run.py): stands in for docker. NOT a container runtime:
# it records its argv and exits as told; no image, no container, no build, no package.
import json, os, sys
with open(os.environ["XG_FIXTURE_LOG"], "w") as log:
    json.dump(sys.argv, log)
print("FIXTURE runtime: argv recorded (no container, no image, no build)")
sys.exit(int(os.environ["XG_FIXTURE_EXIT"]))
'''

TRAP = '''# FIXTURE trap (tools/check_deb_container_run.py): runs only inside the container, never on the host.
open({trap!r}, "w").write("a source-tree helper was executed on the host\\n")
raise SystemExit("FIXTURE trap executed on the host")
'''


def fresh(folder: Path) -> Path:
    if folder.is_symlink() or folder.is_file():
        folder.unlink()
    elif folder.exists():
        shutil.rmtree(folder)
    folder.mkdir(parents=True)
    return folder


def tree(folder: Path):
    """Every entry's bytes and mtime, and the folder's own mtime (None when absent)."""
    if not folder.exists():
        return None
    return snapshot(folder), folder.stat().st_mtime_ns


def make_source(case: Path, name: str, trap: Path, omit: tuple[str, ...] = (), extra: tuple[str, ...] = ()) -> Path:
    src = case / name
    files = {f"packaging/{h}": TRAP.format(trap=str(trap)) for h in HELPERS}
    files |= {rel: "# FIXTURE placeholder (tools/check_deb_container_run.py): not the real file\n"
              for rel in ("port/CMakeLists.txt", "port/cmake/deb.cmake", "tools/pc_link.py")}
    files["build/pc/sources.cmake"] = "# FIXTURE (tools/check_deb_container_run.py): not a module list\n"
    for rel, text in files.items():
        if rel not in omit:
            (src / rel).parent.mkdir(parents=True, exist_ok=True)
            (src / rel).write_text(text)
    for rel in extra:  # a marker: a folder, or a file when it has a suffix
        path = src / rel
        if path.suffix:
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text("FIXTURE marker (tools/check_deb_container_run.py): no content\n")
        else:
            path.mkdir(parents=True, exist_ok=True)
    src.mkdir(parents=True, exist_ok=True)
    return src


def expected(runtime: Path, src: Path, build: Path, out: Path, tag: str = TAG) -> list[str]:
    """The documented argv, written out here independently of the launcher."""
    return [str(runtime), "run", "--rm", "--platform", "linux/amd64", "--pull", "never", "--network", "none",
            "--cap-drop", "ALL", "--security-opt", "no-new-privileges",
            "--mount", f"type=bind,src={src.resolve()},dst=/work/source,readonly",
            "--mount", f"type=bind,src={build.resolve()},dst=/work/build",
            "--mount", f"type=bind,src={out.resolve()},dst=/work/out",
            IMAGE, "--tag", tag]


class Suite:
    def __init__(self, launcher: Path, work: Path, runtime: Path, quiet: bool):
        self.launcher, self.work, self.runtime, self.quiet = launcher, work, runtime, quiet
        self.trap = work / "TRAP-host-executed"
        self.results: list[tuple[bool, str]] = []

    def note(self, ok: bool, what: str) -> None:
        self.results.append((ok, what))
        if not self.quiet:
            check(ok, what)

    def case(self, name: str, **source):
        case = fresh(self.work / re.sub(r"\W+", "-", name).strip("-"))
        src = make_source(case, f"source {ODD}", self.trap, **source)
        build = fresh(case / f"build {ODD}")
        out = fresh(case / f"out {ODD}")
        (out / f"KEEP {ODD}").write_text("existing output content (fixture): must stay unchanged\n")
        return case, src, build, out

    def launch(self, case: Path, args: list, watch: list[Path], status: int = 0, home: Path | None = None):
        log = case / "fixture-argv.json"
        log.unlink(missing_ok=True)
        env = {"PATH": os.environ.get("PATH", "/usr/bin:/bin"), "HOME": str(home or case / "home"),
               "XG_FIXTURE_LOG": str(log), "XG_FIXTURE_EXIT": str(status)}
        before = [tree(p) for p in watch]
        argv = [str(a) for a in (sys.executable, "-I", "-B", self.launcher, *args)]
        if not self.quiet:
            print("$", " ".join(argv), flush=True)
        result = subprocess.run(argv, cwd=case, env=env, capture_output=True, text=True)
        got = json.loads(log.read_text()) if log.is_file() else None
        return result, got, [tree(p) for p in watch] == before

    def args(self, src, build, out, tag=TAG, image=IMAGE, runtime=None) -> list:
        return ["--source", src, "--build-dir", build, "--out-dir", out, f"--tag={tag}", f"--image={image}",
                "--runtime", runtime or self.runtime]  # --x=value: a value starting with - stays a value

    def accepted(self) -> None:
        for status in (0, 1, 2, 7, 125):
            case, src, build, out = self.case(f"status {status}")
            result, got, same = self.launch(case, self.args(src, build, out), [src, build, out], status)
            want = expected(self.runtime, src, build, out)
            self.note(got == want, f"exit {status}: the FIXTURE runtime got exactly the documented argv, "
                                   f"metacharacters inert ({'as documented' if got == want else got})")
            self.note(result.returncode == status, f"exit {status}: the launcher exits {status} ({result.returncode})")
            if status == 0:
                self.note("the runtime exited 0" in result.stdout and "no proof" in result.stdout
                          and "FAILED" not in result.stderr, "exit 0: reported as the helper's success, no proof claimed")
            else:
                self.note(f"FAILED: the runtime exited {status}" in result.stderr
                          and "holds none of the package's three names" in result.stderr
                          and "nothing staged" not in result.stderr,
                          f"exit {status}: reported as the runtime's failure, --out-dir looked up "
                          f"({result.stderr.strip().splitlines()[-2:]})")
            self.note(same, f"exit {status}: source, build and existing output unchanged (bytes, mtimes)")
        case, src, build, out = self.case("missing runtime")
        result, got, same = self.launch(case, self.args(src, build, out, runtime=case / "no such runtime"),
                                        [src, build, out])
        self.note(result.returncode == 4 and "could not start the runtime" in result.stderr and got is None and same,
                  f"a missing runtime: exit 4, could not start, folders unchanged ({result.returncode})")

    def refused(self, what: str, says: str, *, source: dict | None = None, layout=None, tag=TAG, image=IMAGE,
                home=None) -> None:
        case, src, build, out = self.case(what, **(source or {}))
        if layout:
            src, build, out = layout(case, src, build, out)
        watch = [p for p in (src, build, out, case / f"source {ODD}", case / f"build {ODD}") if p.exists()]
        result, got, same = self.launch(case, self.args(src, build, out, tag, image), watch,
                                        home=home(src) if home else None)
        self.note(result.returncode == 3 and "REFUSED" in result.stderr and says in result.stderr and got is None,
                  f"refused, {what}: exit 3 ({result.returncode}), says {says!r}, runtime not started "
                  f"({result.stderr.strip().splitlines()[:1]})")
        self.note(same, f"refused, {what}: every folder unchanged (bytes, mtimes)")

    def refusals(self) -> None:
        def nested(case, src, build, out):
            inner = src / "build" / "inner"
            inner.mkdir(parents=True)
            return src, inner, out

        def symlinked(case, src, build, out):
            (case / "link").symlink_to(build, target_is_directory=True)
            return src, case / "link", out

        def renamed(part: str, char: str):
            def layout(case, src, build, out):
                place = {"source": src, "build": build, "out": out}[part]
                moved = place.with_name(place.name + char + "y")
                place.rename(moved)
                return tuple(moved if p == place else p for p in (src, build, out))
            return layout

        self.refused("no source folder", "no such folder", layout=lambda c, s, b, o: (c / "absent", b, o))
        self.refused("no build/pc/sources.cmake", "build/pc/sources.cmake", source={"omit": ("build/pc/sources.cmake",)})
        self.refused("no packaging/deb_ubuntu.py", "not a generated public port tree",
                     source={"omit": ("packaging/deb_ubuntu.py",)})
        for marker in (".beads", "CLAUDE.md", "tools/publish_decomp.py", "docs/PLAN.md", "disc", "bin", "work"):
            self.refused(f"marker {marker}", "working clone", source={"extra": (marker,)})
        self.refused("a disc image file", "disc image files", source={"extra": ("build/game.cue",)})
        self.refused("the home folder inside", "home folder", home=lambda src: src / "h")
        self.refused("no build folder", "no such folder", layout=lambda c, s, b, o: (s, c / "absent", o))
        self.refused("no output folder", "no such folder", layout=lambda c, s, b, o: (s, b, c / "absent"))
        self.refused("a non-empty build folder", "is not empty",
                     layout=lambda c, s, b, o: ((b / ".leftover").write_text("fixture\n"), (s, b, o))[1])
        self.refused("a symlinked build folder", "symbolic link", layout=symlinked)
        self.refused("build inside the source", "overlap", layout=nested)
        self.refused("output inside the build", "overlap",
                     layout=lambda c, s, b, o: ((b / "o").mkdir(), (s, b, b / "o"))[1])
        self.refused("build and output the same", "overlap", layout=lambda c, s, b, o: (s, b, b))
        self.refused("a comma in the build path", "cannot carry", layout=renamed("build", ","))
        self.refused("a double quote in the output path", "cannot carry", layout=renamed("out", '"'))
        self.refused("a newline in the source path", "cannot carry", layout=renamed("source", "\n"))
        self.refused("an invalid tag", "not vYYYY.MM.N", tag="v2026.13.0")
        self.refused("a tag with metacharacters", "not vYYYY.MM.N", tag=f"{TAG} {ODD}")
        self.refused("an option as image", "not an image reference", image="--privileged")
        self.refused("an empty image", "not an image reference", image="")

    def usage(self) -> None:
        case, src, build, out = self.case("usage")
        full = self.args(src, build, out)
        for what, args in (("no arguments", []), ("no --image", [a for a in full if not str(a).startswith("--image=")]),
                           ("an unknown option", full + ["--privileged"]),
                           ("an abbreviated option", ["--sour", src] + full[2:])):
            result, got, same = self.launch(case, args, [src, build, out])
            self.note(result.returncode == 2 and got is None and same and "usage" in result.stderr,
                      f"command line error, {what}: exit 2, runtime not started ({result.returncode})")

    def all(self) -> list[tuple[bool, str]]:
        fresh(self.work)
        self.accepted()
        self.refusals()
        self.usage()
        self.note(not list(self.work.rglob("PWNED*")), "nothing the metacharacters name was created")
        self.note(not self.trap.exists(), "no source-tree helper ran on the host (no trap fired)")
        return self.results


def check_static() -> None:
    print("== STATIC: the launcher's text ==")
    text = LAUNCHER.read_text()
    imports = set()
    for node in ast.walk(ast.parse(text)):
        if isinstance(node, ast.Import):
            imports |= {a.name.split(".")[0] for a in node.names}
        elif isinstance(node, ast.ImportFrom):
            imports.add((node.module or "").split(".")[0])
    outside = sorted(imports - set(sys.stdlib_module_names) - {"__future__"})
    check(not outside, f"{LAUNCHER.name} imports only the standard library ({outside or sorted(imports)})")
    spec = importlib.util.spec_from_file_location("publish_decomp", ROOT / "tools/publish_decomp.py")
    publish = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(publish)
    patterns = (publish.LEFTOVER_DATE, publish.TIME_OF_DAY, publish.FORBIDDEN, re.compile(r"/h" r"ome/|herm" r"es", re.I))
    flagged = [line for line in text.splitlines() if any(p.search(line) for p in patterns)]
    check(not flagged, f"{LAUNCHER.name} passes the publisher's date/time/private scan ({flagged or 'clean'})")
    check(all((PACKAGING / h).is_file() for h in HELPERS), f"the helpers it loads are beside it ({HELPERS})")


MUTATIONS = (
    ("network allowed", '"--network", "none",', ""),
    ("source writable", ",dst=/work/source,readonly\"", ",dst=/work/source\""),
    ("implicit pull", '"--pull", "never", ', ""),
    ("privileged", '"--rm", ', '"--rm", "--privileged", '),
    ("capabilities kept", '"--cap-drop", "ALL", ', ""),
    ("an extra mount", '"--mount", f"type=bind,src={out},dst=/work/out",',
     '"--mount", f"type=bind,src={out},dst=/work/out", "--mount", "type=bind,src=/var/run/docker.sock,dst=/d",'),
    ("no delimiter check", "if UNSAFE.search(", "if False and UNSAFE.search("),
    ("no marker check", "found = [n for n in PRIVATE_MARKERS", "found = [n for n in ()"),
    ("no overlap check", "if within(a, b) or within(b, a):", "if False:"),
    ("no empty-build check", "        ubuntu.check_paths(source, args.build_dir, args.out_dir, names)", "        pass"),
    ("no tag check", "version = assets.version_from_tag(args.tag)", "version = args.tag[1:]"),
    ("no image check", "if not IMAGE.fullmatch(args.image):", "if False:"),
    ("helpers from --source", "ubuntu, assets = helpers(HERE)", 'ubuntu, assets = helpers(source / "packaging")'),
    ("a shell", "result = subprocess.run([runtime, *argv[1:]])",
     'result = subprocess.run(" ".join([runtime, *argv[1:]]), shell=True)'),
    ("child status lost", "        return status", "        return 1"),
    ("refusal reported as success", "REFUSED, NOT_STARTED = 3, 4", "REFUSED, NOT_STARTED = 0, 4"),
    ("missing runtime as success", "REFUSED, NOT_STARTED = 3, 4", "REFUSED, NOT_STARTED = 3, 0"),
)


def check_mutations(work: Path, runtime: Path) -> None:
    print("== MUTATIONS: scratch copies of the launcher, one safeguard removed each (the real one untouched) ==")
    text = LAUNCHER.read_text()
    for i, (label, old, new) in enumerate(MUTATIONS):
        folder = fresh(work / f"mutant-{i:02d}" / "packaging")
        for helper in HELPERS:
            shutil.copy2(PACKAGING / helper, folder / helper)
        mutated = text.replace(old, new)
        (folder / LAUNCHER.name).write_text(mutated)
        failed = [what for ok, what in Suite(folder / LAUNCHER.name, folder.parent / "runs", runtime, True).all() if not ok]
        check(mutated != text and bool(failed), f"mutation caught: {label} ({len(failed)} failing, first: "
                                                f"{failed[0][:160] if failed else 'NONE'})")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--ver", choices=("us", "jp"), default="us")
    args = ap.parse_args()
    work = fresh(ROOT / f"build/pc/deb-container-run-{args.ver}")
    runtime = work / "fixture-runtime"
    runtime.write_text(FIXTURE_RUNTIME.format(python=sys.executable))
    runtime.chmod(0o755)
    caches = {p for p in PACKAGING.rglob("__pycache__")}
    before = snapshot(PACKAGING)
    check_static()
    print("== EXECUTABLE: the real launcher against a FIXTURE runtime (not docker, no image, no container) ==")
    Suite(LAUNCHER, work / "real", runtime, False).all()
    check(snapshot(PACKAGING) == before and {p for p in PACKAGING.rglob("__pycache__")} == caches,
          "the real launcher and its helpers are unchanged and left no bytecode")
    check_mutations(work, runtime)
    if cdb.failures:
        print(f"check_deb_container_run: {len(cdb.failures)} check(s) failed")
        return 1
    print("check_deb_container_run: all checks pass (the launcher's refusals and argv against a FIXTURE runtime "
          "only: no image, container, Ubuntu build, Depends, install or start checked)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
