"""Private source copies and checked disc inputs for the release builders."""

import hashlib
import os
import re
import shutil
from pathlib import Path, PurePosixPath

TABLES = "port/link-tables"  # what a checkout without a disc links from (port/CMakeLists.txt XG_LINK_FROM)


class InputError(Exception):
    pass


def copy_tree(root, tree, *excluded):
    root = Path(root).resolve()
    excluded = {Path(path).resolve() for path in excluded}
    scratch = {"build", "disc", "bin", "work", ".git", ".venv", ".beads"}

    def ignore(folder, entries):
        folder = Path(folder)
        rel = folder.relative_to(root)
        out = {e for e in entries if e == "__pycache__" or (folder / e).resolve() in excluded}
        if rel == Path("."):
            out |= scratch.intersection(entries)
        if rel == Path("tools"):
            out.update(e for e in entries if e.startswith("toolchain"))
        return out

    shutil.copytree(root, tree, symlinks=True, ignore=ignore)


def disc_image(root, discs, region):
    text = (Path(root) / "config" / region / "disc.yaml").read_text(encoding="utf-8")
    match = re.search(r"^image:\s*(.+?)\s*$", text, re.M)
    if not match:
        raise InputError(f"config/{region}/disc.yaml names no image")
    base = PurePosixPath(match.group(1)).name
    found = [p for p in (Path(discs) / base, Path(discs) / PurePosixPath(base).stem / base) if p.is_file()]
    if len(found) != 1:
        raise InputError(f"--discs {discs}: {'no' if not found else 'two copies of'} {base}")
    return found[0]


def module_count(tree, region):
    tree = Path(tree)
    text = (tree / "config" / region / "disc.yaml").read_text(encoding="utf-8")
    entries = re.findall(r"\{name:\s*(\w+),([^}]*)\}", text)
    if not entries or len(re.findall(r"\bname\s*:", text)) != len(entries):
        raise InputError(f"config/{region}/disc.yaml: module entries not of the one form the auditor reads")
    want = {f"{n}.exe" if "iso:" in fields else f"{n}.bin" for n, fields in entries}
    if len(want) != len(entries):
        raise InputError(f"config/{region}/disc.yaml lists a module twice")
    folder = tree / "disc" / region
    have = {p.name for p in folder.iterdir()} if folder.is_dir() else set()
    if have != want or any((folder / n).is_symlink() or not (folder / n).is_file()
                           or (folder / n).stat().st_size == 0 for n in want):
        raise InputError(f"extraction incomplete or unexpected: missing {sorted(want - have)}, extra {sorted(have - want)}")
    return len(want)


def tree_digest(tree):
    """One SHA-256 naming a private copy's sources: every file's name and bytes, the link tables left out.

    A disc-backed build records it beside the tables it writes, with its executables' sums; a build from
    those tables whose copy has the same digest must give the same executables (build_release.py)."""
    tree = Path(tree)
    entries = []
    for folder, subdirs, names in os.walk(tree):
        for name in names + [d for d in subdirs if (Path(folder) / d).is_symlink()]:
            path = Path(folder) / name
            rel = path.relative_to(tree).as_posix()
            if rel.startswith(TABLES + "/"):
                continue
            if path.is_symlink():
                data = b"link " + os.readlink(path).encode()
            elif path.is_file():
                data = path.read_bytes()
            else:
                raise InputError(f"{rel}: not a regular file")
            entries.append(rel.encode() + b"\0" + hashlib.sha256(data).digest())
    digest = hashlib.sha256()
    for entry in sorted(entries):
        digest.update(len(entry).to_bytes(4, "big") + entry)
    return digest.hexdigest()
