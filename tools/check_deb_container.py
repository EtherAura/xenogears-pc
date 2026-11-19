#!/usr/bin/env python3
"""Check packaging/deb_ubuntu.Dockerfile (xenogears-pc): the Ubuntu 26.04 builder image recipe.

    python tools/check_deb_container.py --ver us

Two kinds of evidence, printed under their own headings:

1. STATIC (the recipe's text only): one stage, FROM --platform=linux/amd64
   ubuntu:26.04; one apt-get install naming every package the shipping
   Release/US/extras build needs (glslc, glslang, dav1d, SDL3, Vulkan headers,
   cmake/cpack, ninja, dpkg-shlibdeps, file, binutils, python3, compilers) and
   nothing outside the package list given for Ubuntu 26.04 main and universe;
   the base image's apt sources read (main and universe present), never
   edited, no other source or download tool; no COPY or ADD, ENV, ARG or CMD;
   a final non-root numeric USER after every RUN, created with that uid; the
   exec-form ENTRYPOINT running the mounted, unchanged deb_ubuntu.py with
   --source/--build-dir/--out-dir only (options deb_ubuntu.py really has); no
   configure, build, cpack, Depends, flag or private-folder logic of its own;
   no privilege, Docker socket or private path; the usage comments naming the
   read-only source mount, the new empty build folder, sources.cmake as a
   prerequisite, the uid the writable mounts need and that no other uid is
   claimed; the publisher's date/time/private scan. Then isolated mutations
   of the recipe (wrong base or architecture, each required package missing,
   final root, COPY/ADD, a bypassed or changed helper call, added sources,
   flags, privilege) must each be refused for the expected reason.
2. EXECUTABLE (this host's Python running the recipe's own ENTRYPOINT argv,
   its container paths mapped to scratch folders: NOT a container, NOT an
   Ubuntu build, no apt, no Depends). With the real deb_ubuntu.py and its
   helpers in a read-only fixture source tree: a tree without
   build/pc/sources.cmake is refused at the inputs step; a non-empty build
   folder is refused; with a labelled fixture sources.cmake the run stops at
   the environment step (this host is not Ubuntu 26.04); a missing --tag exits
   2; each time exit status as documented and the source, build and output
   folders unchanged. With a CLEARLY LABELLED FIXTURE standing in for
   deb_ubuntu.py: the argv it receives is exactly the documented one with
   paths and a tag full of shell metacharacters passed through inert (nothing
   they name is created), and its exit statuses 0, 1, 2 and 7 come back
   unchanged.

Scratch stays in build/pc/deb-container-<ver>/. No Docker, root or network.
"""
from __future__ import annotations

import argparse
import ast
import importlib.util
import json
import os
import re
import shutil
import stat
import sys
from pathlib import Path

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parent.parent
PACKAGING = ROOT / "tools/publish/repo-port/packaging"
RECIPE = PACKAGING / "deb_ubuntu.Dockerfile"
HELPERS = ("deb_ubuntu.py", "deb_build.py", "deb_assets.py")
SOURCE_INPUTS = ("port/CMakeLists.txt", "port/cmake/deb.cmake", "tools/pc_link.py")
BASE = "--platform=linux/amd64 ubuntu:26.04"
# What the shipping build needs: compilers and dpkg-dev, cmake/cpack, ninja, ld/objcopy, file (CPack's
# shlibdeps), Python, SDL3, Vulkan headers, glslc, glslang (with spirv-tools), dav1d.
REQUIRED = ("build-essential", "cmake", "ninja-build", "dpkg-dev", "binutils", "file", "python3", "libsdl3-dev",
            "libvulkan-dev", "glslc", "glslang-dev", "glslang-tools", "spirv-tools-dev", "libdav1d-dev")
# Names given for Ubuntu 26.04 (resolute) main and universe, amd64: nothing else may be installed.
KNOWN = set(REQUIRED) | {"pkg-config", "spirv-tools"}
ENTRYPOINT = ["python3", "-I", "-B", "/work/source/packaging/deb_ubuntu.py", "--source", "/work/source",
              "--build-dir", "/work/build", "--out-dir", "/work/out"]
MOUNTS = ("/work/source", "/work/build", "/work/out")
DOC_PHRASES = ("dst=/work/source,readonly", "read-only", "build/pc/sources.cmake", "new empty folder",
               "uid 10001", "writable by that uid", "No other uid", "--network none", "not root")

spec = importlib.util.spec_from_file_location("check_deb_build", ROOT / "tools/check_deb_build.py")
cdb = importlib.util.module_from_spec(spec)
spec.loader.exec_module(cdb)
check, run, snapshot, TAG = cdb.check, cdb.run, cdb.snapshot, cdb.TAG


def instructions(text: str) -> list[tuple[str, str]]:
    """(KEYWORD, arguments) per instruction: comments dropped, backslash continuations joined."""
    out, pending = [], ""
    for line in text.splitlines():
        if not pending and (not line.strip() or line.lstrip().startswith("#")):
            continue
        if line.rstrip().endswith("\\"):
            pending += line.rstrip()[:-1] + " "
            continue
        full = (pending + line).strip()
        pending = ""
        keyword, _, rest = full.partition(" ")
        out.append((keyword.upper(), rest.strip()))
    if pending:
        out.append(("UNFINISHED", pending))
    return out


def helper_options() -> set[str]:
    tree = ast.parse((PACKAGING / "deb_ubuntu.py").read_text())
    return {n.args[0].value for n in ast.walk(tree) if isinstance(n, ast.Call)
            and getattr(n.func, "attr", "") == "add_argument"}


def problems(text: str) -> dict[str, str]:
    """The recipe's faults by kind (empty: none)."""
    found: dict[str, str] = {}
    steps = instructions(text)
    kinds = [k for k, _ in steps]
    froms = [rest for k, rest in steps if k == "FROM"]
    if froms != [BASE]:
        found["base"] = f"FROM is {froms}, not exactly [{BASE!r}]"
    runs = [rest for k, rest in steps if k == "RUN"]
    installs = [r for r in runs if "apt-get install" in r]
    packages: list[str] = []
    for command in installs:
        for part in command.split(";"):
            if "apt-get install" in part:
                packages += [w for w in part.split("apt-get install", 1)[1].split() if not w.startswith("-")]
    missing = [p for p in REQUIRED if p not in packages]
    if len(installs) != 1 or missing:
        found["tool"] = f"one apt-get install with every required package ({len(installs)} found; missing {missing})"
    if extra := sorted(set(packages) - KNOWN):
        found["package"] = f"packages outside the given Ubuntu 26.04 main/universe list: {extra}"
    for word in ("main", "universe"):
        if not any(f"]{word}([" in r and "grep -Eq" in r and "/etc/apt/sources.list.d/ubuntu.sources" in r for r in runs):
            found["components"] = f"the base image's sources are not checked for {word}"
    sourcing = re.compile(r"add-apt-repository|ppa:|apt-key|sources\.list(?!\.d/ubuntu\.sources)|\bsed\b|\btee\b|"
                          r">|\bcurl\b|\bwget\b|\bpip3?\b|\bgit\b|https?://|\bgpg\b")
    if any(sourcing.search(r) for r in runs):
        found["sources"] = "a RUN edits apt sources, redirects, or downloads outside apt"
    if {"COPY", "ADD"} & set(kinds):
        found["copy"] = "COPY or ADD: nothing may be copied into the image"
    if {"ENV", "ARG", "CMD", "ONBUILD", "SHELL", "VOLUME"} & set(kinds):
        found["env"] = "ENV, ARG, CMD, ONBUILD, SHELL or VOLUME: none is part of the recipe"
    users = [i for i, k in enumerate(kinds) if k == "USER"]
    last_run = max((i for i, k in enumerate(kinds) if k == "RUN"), default=-1)
    user = steps[users[-1]][1] if users else ""
    uid = user.split(":")[0]
    if (not users or users[-1] < last_run or not re.fullmatch(r"[1-9]\d*:[1-9]\d*", user)
            or not any(f"--uid {uid} " in r and "useradd" in r for r in runs)):
        found["user"] = f"the final USER must be a non-root numeric uid:gid after every RUN, created by useradd ({user!r})"
    entries = [rest for k, rest in steps if k == "ENTRYPOINT"]
    try:
        entry = json.loads(entries[0]) if len(entries) == 1 else None
    except ValueError:
        entry = None
    options = {a for a in (entry or []) if a.startswith("--")}
    if entry != ENTRYPOINT or not options <= helper_options():
        found["entrypoint"] = f"ENTRYPOINT must be exactly {ENTRYPOINT} in exec form ({entries})"
    logic = re.compile(r"cmake\s+-S|\bcpack\b|\bninja\s|deb_build\.py|dpkg-shlibdeps\s|-march|-mtune|native|"
                       r"\b(CC|CXX|C\w*FLAGS|LDFLAGS)=|Depends|CPACK_|-DXG_|XG_|PRIVATE_DIRS|shlibdeps-private", re.I)
    if any(logic.search(rest) for k, rest in steps if k != "FROM"):
        found["logic"] = "configure, build, cpack, Depends, flag or private-folder logic outside deb_ubuntu.py"
    private = re.compile(r"--privileged|--cap-add|docker\.sock|--device|/h" r"ome/|/root/|\.ssh|\bdisc/|\bbin/|"
                         r"\bwork/(?!source|build|out)", re.I)
    if flagged := [line for line in text.splitlines() if private.search(line)]:
        found["private"] = f"privilege, Docker socket or private path: {flagged}"
    comments = " ".join(line.lstrip("# ").strip() for line in text.splitlines() if line.lstrip().startswith("#"))
    comments = re.sub(r"\s+", " ", comments.replace("\\", " "))
    if missing_doc := [p for p in DOC_PHRASES if p not in comments]:
        found["doc"] = f"usage comments lack {missing_doc}"
    return found


def publisher_scan(text: str) -> list[str]:
    spec = importlib.util.spec_from_file_location("publish_decomp", ROOT / "tools/publish_decomp.py")
    publish = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(publish)
    patterns = (publish.LEFTOVER_DATE, publish.TIME_OF_DAY, publish.FORBIDDEN, re.compile(r"/h" r"ome/|herm" r"es", re.I))
    return [line for line in text.splitlines() if any(p.search(line) for p in patterns)]


def drop_package(text: str, name: str) -> str:
    return "\n".join(re.sub(rf"(?<!\S){re.escape(name)}(?=[\s;]|$)", "", line) if line.startswith("        ") else line
                     for line in text.splitlines())


def mutations(text: str) -> list[tuple[str, str, str]]:
    user_line = "USER 10001:10001\n"
    entry_line = next(line for line in text.splitlines() if line.startswith("ENTRYPOINT"))
    found = [
        ("wrong base release", text.replace("ubuntu:26.04", "ubuntu:24.04"), "base"),
        ("wrong base distribution", text.replace("ubuntu:26.04", "debian:forky"), "base"),
        ("wrong architecture", text.replace("linux/amd64 ubuntu", "linux/arm64 ubuntu"), "base"),
        ("no platform pin", text.replace("FROM --platform=linux/amd64 ", "FROM "), "base"),
        ("a second stage", text + "FROM ubuntu:26.04\n", "base"),
        ("final user root", text + "USER root\n", "user"),
        ("final user uid 0", text.replace(user_line, "USER 0:0\n"), "user"),
        ("no USER", text.replace(user_line, ""), "user"),
        ("USER before the last RUN", text.replace(user_line, "") .replace("WORKDIR", user_line + "RUN true\nWORKDIR"), "user"),
        ("source COPY", text.replace(user_line, "COPY . /work/source\n" + user_line), "copy"),
        ("source ADD", text.replace(user_line, "ADD port /work/source/port\n" + user_line), "copy"),
        ("helper bypassed for deb_build.py", text.replace(entry_line, entry_line.replace(
            "deb_ubuntu.py", "deb_build.py")), "entrypoint"),
        ("helper bypassed for cmake", text.replace(entry_line, 'ENTRYPOINT ["cmake", "-S", "/work/source/port"]'),
         "entrypoint"),
        ("shell-form entry", text.replace(entry_line, "ENTRYPOINT " + " ".join(ENTRYPOINT)), "entrypoint"),
        ("private shlibdeps folder", text.replace(entry_line, entry_line.replace(
            '"/work/out"', '"/work/out", "--shlibdeps-private-dir", "/usr/lib"')), "entrypoint"),
        ("helper without --source", text.replace('"--source", "/work/source", ', ""), "entrypoint"),
        ("extra CMD", text + 'CMD ["--tag", "v0"]\n', "env"),
        ("native flags", text.replace(user_line, "ENV CFLAGS=-march=native\n" + user_line), "env"),
        ("a configure of its own", text.replace(user_line, "RUN cmake -S /work/source/port -B /tmp/b\n" + user_line),
         "logic"),
        ("hard-coded Depends", text.replace(user_line, "RUN echo 'Depends: libsdl3-0' > /tmp/d\n" + user_line), "logic"),
        ("third-party source", text.replace("apt-get update;", "add-apt-repository ppa:x/y; apt-get update;"), "sources"),
        ("suite replaced", text.replace("apt-get update;", "sed -i s/resolute/devel/ /etc/apt/sources.list.d/"
                                        "ubuntu.sources; apt-get update;"), "sources"),
        ("download outside apt", text.replace("apt-get update;", "curl -fsSL example.invalid; apt-get update;"),
         "sources"),
        ("universe not checked", "\n".join(l for l in text.splitlines() if "]universe(" not in l), "components"),
        ("a package outside the list", text.replace("libdav1d-dev;", "libdav1d-dev curl;"), "package"),
        ("privileged run documented", text.replace("--network none", "--privileged --network none"), "private"),
        ("Docker socket mounted", text.replace("dst=/work/out", "dst=/work/out --mount type=bind,src=/var/run/"
                                               "docker.sock,dst=/var/run/docker.sock"), "private"),
        ("read-only mount not documented", text.replace(",readonly", ""), "doc"),
        ("uid requirement not documented", text.replace("No other uid", "Any uid"), "doc"),
    ]
    found += [(f"missing {name}", drop_package(text, name), "tool") for name in REQUIRED]
    return found


def check_static() -> str:
    print("== STATIC: the recipe's text (no image is built) ==")
    text = RECIPE.read_text()
    found = problems(text)
    check(not found, f"{RECIPE.name} meets the recipe rules ({found or 'clean'})")
    flagged = publisher_scan(text)
    check(not flagged, f"{RECIPE.name} passes the publisher's date/time/private scan ({flagged or 'clean'})")
    check(all((PACKAGING / h).is_file() for h in HELPERS), f"the helpers it delegates to are beside it ({HELPERS})")
    for label, mutated, kind in mutations(text):
        check(mutated != text and kind in problems(mutated), f"mutation refused: {label} ({kind})")
    return text


def make_writable(folder: Path) -> None:
    for path in [folder, *folder.rglob("*")]:
        if path.is_dir() and not path.is_symlink():
            path.chmod(path.stat().st_mode | stat.S_IWUSR)


def fresh(folder: Path) -> Path:
    if folder.exists():
        make_writable(folder)
        shutil.rmtree(folder)
    folder.mkdir(parents=True)
    return folder


def read_only(folder: Path) -> None:
    for path in sorted(folder.rglob("*"), reverse=True) + [folder]:
        path.chmod(path.stat().st_mode & ~(stat.S_IWUSR | stat.S_IWGRP | stat.S_IWOTH))


def mapped(entry: list[str], places: dict[str, Path], extra: list[str]) -> list[str]:
    """The recipe's ENTRYPOINT with its container paths replaced by scratch folders, then the caller's arguments."""
    argv = [sys.executable if entry[0] == "python3" else entry[0]]
    for arg in entry[1:]:
        for mount, place in places.items():
            if arg == mount or arg.startswith(mount + "/"):
                arg = str(place) + arg[len(mount):]
        argv.append(arg)
    return argv + extra


def entry_of(text: str) -> list[str]:
    return json.loads(next(rest for k, rest in instructions(text) if k == "ENTRYPOINT"))


def real_source(work: Path, name: str, sources: bool) -> Path:
    src = fresh(work / name)
    (src / "packaging").mkdir()
    for helper in HELPERS:
        shutil.copy2(PACKAGING / helper, src / "packaging" / helper)
    for rel in SOURCE_INPUTS:
        (src / rel).parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(ROOT / rel, src / rel)
    if sources:
        (src / "build/pc").mkdir(parents=True)
        (src / "build/pc/sources.cmake").write_text("# FIXTURE (tools/check_deb_container.py): not a module list\n")
    read_only(src)
    return src


def host_is_target() -> bool:
    for path in (Path("/etc/os-release"), Path("/usr/lib/os-release")):
        if path.is_file():
            text = path.read_text(errors="replace")
            return bool(re.search(r"^ID=\"?ubuntu\"?$", text, re.M) and re.search(r"^VERSION_ID=\"?26\.04\"?$", text, re.M))
    return False


def refused_run(work: Path, entry: list[str], what: str, src: Path, extra: list[str], status: int,
                says: tuple[str, ...], build_entries: dict[str, str] | None = None) -> None:
    build = fresh(work / "runs" / re.sub(r"\W+", "-", what) / "build")
    for name, content in (build_entries or {}).items():
        (build / name).write_text(content)
    out = fresh(build.parent / "out")
    (out / "KEEP").write_text("existing output content: must stay\n")
    before = [snapshot(src), snapshot(build), snapshot(out)]
    env = {"PATH": os.environ.get("PATH", "/usr/bin:/bin"), "HOME": str(work)}
    result = run(mapped(entry, {"/work/source": src, "/work/build": build, "/work/out": out}, extra), cwd=work, env=env)
    check(result.returncode == status and all(s in result.stderr for s in says) and "staged the package" not in result.stdout,
          f"real deb_ubuntu.py, {what}: exit {result.returncode} (want {status}), says {says} "
          f"({result.stderr.strip().splitlines()[-2:]})")
    check([snapshot(src), snapshot(build), snapshot(out)] == before and not list(src.rglob("__pycache__")),
          f"real deb_ubuntu.py, {what}: source, build and output folders unchanged, no bytecode")


FIXTURE_HELPER = '''# FIXTURE (tools/check_deb_container.py): stands in for packaging/deb_ubuntu.py.
# Not the real helper: it records its arguments and exits as told; it builds and packages nothing.
import json, os, sys
with open(os.environ["XG_FIXTURE_LOG"], "w") as log:
    json.dump(sys.argv[1:], log)
print("fixture helper: arguments recorded (no build, no package)")
sys.exit(int(os.environ["XG_FIXTURE_EXIT"]))
'''


def check_executable(work: Path, text: str) -> None:
    print("== EXECUTABLE: the recipe's ENTRYPOINT argv on this host's Python (not a container, not Ubuntu) ==")
    entry = entry_of(text)
    missing = real_source(work, "source-no-sources-cmake", sources=False)
    refused_run(work, entry, "no build/pc/sources.cmake", missing, ["--tag", TAG], 1,
                ("FAILED at step inputs", "build/pc/sources.cmake", "never generates it"))
    ready = real_source(work, "source-ready", sources=True)
    refused_run(work, entry, "a non-empty build folder", ready, ["--tag", TAG], 1,
                ("FAILED at step inputs", "is not empty"), {".hidden": "an earlier build's leftover (fixture)\n"})
    if host_is_target():
        print("note  this host is Ubuntu 26.04: the environment refusal below is not exercised")
    else:
        refused_run(work, entry, "this host is not the builder", ready, ["--tag", TAG], 1,
                    ("FAILED at step environment", "not an Ubuntu 26.04 builder"))
    refused_run(work, entry, "no --tag", ready, [], 2, ("--tag",))

    odd = "x; touch PWNED $(touch PWNED2) `touch PWNED3` 'q\" *"
    src = fresh(work / f"fixture source {odd}")
    (src / "packaging").mkdir()
    (src / "packaging/deb_ubuntu.py").write_text(FIXTURE_HELPER)
    read_only(src)
    build, out = fresh(work / f"fixture build {odd}"), fresh(work / f"fixture out {odd}")
    tag = f"v0 {odd}"
    for status in (0, 1, 2, 7):
        log = work / "fixture-argv.json"
        log.unlink(missing_ok=True)
        env = {"PATH": os.environ.get("PATH", "/usr/bin:/bin"), "XG_FIXTURE_LOG": str(log), "XG_FIXTURE_EXIT": str(status)}
        result = run(mapped(entry, {"/work/source": src, "/work/build": build, "/work/out": out}, ["--tag", tag]),
                     cwd=work, env=env)
        got = json.loads(log.read_text()) if log.is_file() else None
        want = ["--source", str(src), "--build-dir", str(build), "--out-dir", str(out), "--tag", tag]
        check(got == want, f"FIXTURE helper, exit {status}: received exactly the documented arguments, "
                           f"metacharacters inert ({'as documented' if got == want else got})")
        check(result.returncode == status, f"FIXTURE helper's exit status {status} comes back ({result.returncode})")
    check(not [p for p in work.rglob("PWNED*")], "nothing the metacharacters name was created")
    check(not any(build.iterdir()) and not any(out.iterdir()), "the fixture runs left build and output folders empty")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--ver", choices=("us", "jp"), default="us")
    args = ap.parse_args()
    work = fresh(ROOT / f"build/pc/deb-container-{args.ver}")
    text = check_static()
    check_executable(work, text)
    make_writable(work)
    if cdb.failures:
        print(f"check_deb_container: {len(cdb.failures)} check(s) failed")
        return 1
    print("check_deb_container: all checks pass (the recipe's text and its argv on this host only: no image built, "
          "no container run, no Ubuntu build, Depends, install or start checked)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
