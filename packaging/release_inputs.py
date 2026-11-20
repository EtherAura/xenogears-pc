"""Private source copies and checked disc inputs for the release builders."""

import re
import shutil
from pathlib import Path, PurePosixPath


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
