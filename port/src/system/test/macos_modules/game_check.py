"""Link-only imports and payload verification for the field/battle reset executable.

On a Linux host (tools/darwin_link.py) XG_NM names an nm that reads Mach-O
(llvm-nm), and verify --link-only checks the link without codesign or running it."""
import argparse
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parents[4] / "tools"))
import pc_link
import pc_macho
import pc_srcdata
import payloadcheck

MODULES = ("field", "battle")
# tools/pc_link.py's names inside another object's storage: .set name, host + offset
ALIAS = re.compile(r"\.set (\w+), (\w+) \+ (\d+)")


def check_aliases(module, image, executable=False):
    """Each generated alias sits at its offset from its host, inside the module's own sections.
    A lost alias would otherwise link as an import from outside, a moved one would read
    its neighbour. The module's object must name them all. The executable's symbol table
    is the final linker's to keep, so a name it left out is counted, not failed."""
    obj = pc_macho.Object(image.read_bytes(), executable)
    own = {s.index for s in obj.sections if s.segment == "__DATA" and s.name in pc_macho.section_names(module)}
    table = obj.symbols(own)
    rows = ALIAS.findall((HERE / "us-original" / (module + ".c")).read_text())
    unnamed = 0
    for alias, host, offset in rows:
        a, h = table.get(alias), table.get(host)
        if executable and (a is None or h is None):
            unnamed += 1
        elif a is None or h is None or a[0] != h[0] or a[1] - h[1] != int(offset):
            raise ValueError(f"{module}: {alias} is not {host} + {offset} in {image.name}: {a} against {h}")
    if not rows:
        raise ValueError(f"{module}: no generated aliases to check")
    print(f"PASS: {module} keeps {len(rows) - unnamed} generated aliases at their offsets in {image.name}"
          + (f" ({unnamed} more have no symbol there)" if unnamed else ""))


def symbols(obj, flags):
    output = subprocess.check_output([os.environ.get("XG_NM", "nm"), flags, obj], text=True)
    return {line.split()[-1] for line in output.splitlines() if line.strip()}


def imports(build):
    defined, undefined = set(), set()
    for module in MODULES:
        obj = build / "modules" / (module + ".o")
        exported = symbols(obj, "-gUj")
        expected = set()
        for path in (HERE / "us-original" / (module + ".exports"), build / "gen" / (module + ".srcsyms")):
            expected.update("_" + name for name in path.read_text().split() if name != pc_macho.NO_EXPORTS)
        if exported != expected:
            raise ValueError(f"{module} missing exports {expected - exported}, unexpected {exported - expected}")
        check_aliases(module, obj)
        defined |= exported
        undefined |= symbols(obj, "-guj")
        print(f"PASS: {module} exports exactly {len(exported)} game and restore symbols")
    # Other game modes and the desktop platform are outside this executable. Only their
    # addresses are needed in tables. Any attempted call must stop the test immediately.
    standins = sorted(undefined - defined - {"_" + name for name in pc_link.HOST_LIBC})
    lines = [".text", ".p2align 2"]
    for symbol in standins:
        if symbol.startswith("___"):
            continue  # compiler runtime and libSystem
        if not re.fullmatch(r"_[a-zA-Z]\w*", symbol):
            raise ValueError(f"unexpected external symbol {symbol!r}")
        # Weak and private: the reset code linked beside the modules defines a few of these itself.
        lines.extend([f".private_extern {symbol}", f".weak_definition {symbol}", f"{symbol}:", "    brk #0"])
    (build / "gen" / "imports.s").write_text("\n".join(lines) + "\n")
    print(f"{len(standins)} address-only stand-ins for imports from outside these two overlays")


def verify(build, run=True):
    objs = json.loads((build / "compile_commands.json").read_text())
    exe = build / "darwin_module_check"
    if run:
        subprocess.run(["codesign", "--verify", "--strict", exe], check=True)
    for module in MODULES:
        check_aliases(module, exe, executable=True)
    with tempfile.TemporaryDirectory() as tmpdir:
        files = []
        for module in MODULES:
            plan = json.loads((HERE / "us-original" / (module + ".json")).read_text())
            image = bytearray(plan["image"]["size"])
            paths = {pc_srcdata.identity(r["output"]): build / r["output"] for r in objs
                     if f"CMakeFiles/game_{module}.dir/" in r["output"]}
            copied = 0
            runs = []  # the stripped bytes as the module's file holds them, with no byte around them
            for rec in plan["objects"]:
                obj = pc_macho.Object(paths[rec["name"]].read_bytes())
                sections = {s.key: s for s in obj.sections}
                for s in rec["sections"]:
                    section = sections[s["name"]]
                    raw = obj.data[section.offset:section.offset + section.size]
                    end = None  # where the last copy stopped: (in the section, in the file)
                    for dest, source, size in sorted(map(tuple, s["copies"])):
                        image[source:source + size] = raw[dest:dest + size]
                        copied += size
                        # A zero between two copies that the file holds too (the end of a
                        # string) belongs to the run. Elsewhere the file's bytes are unknown.
                        if end and dest - end[0] == source - end[1] and not any(raw[end[0]:dest]):
                            runs[-1] += raw[end[0]:dest + size]
                        else:
                            runs.append(bytearray(raw[dest:dest + size]))
                        end = (dest + size, source + size)
            # Only windows inside a run: the zeros this image has around one are not the file's.
            wanted = payloadcheck.windows({str(i): bytes(run) for i, run in enumerate(runs)}, pc_srcdata.MIN)
            if not wanted or not copied or payloadcheck.hits(exe.read_bytes(), wanted, pc_srcdata.MIN):
                raise ValueError(f"{module}: source window in executable or empty source evidence")
            print(f"PASS: {len(paths)} {module} objects, {copied} restored bytes, "
                  f"none of their {len(wanted)} source windows in the linked executable")
            path = Path(tmpdir) / module
            path.write_bytes(image)
            files.append(path)
        if run:
            subprocess.run([exe, *files], check=True)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("action", choices=("imports", "verify"))
    parser.add_argument("build", type=Path)
    parser.add_argument("--link-only", action="store_true",
                        help="verify: no codesign and no run (a host that cannot run the executable)")
    args = parser.parse_args()
    if args.action == "imports":
        imports(args.build.resolve())
    else:
        verify(args.build.resolve(), run=not args.link_only)
