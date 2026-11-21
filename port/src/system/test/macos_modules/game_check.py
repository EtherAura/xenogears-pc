"""Link-only imports and payload verification for the field/battle reset executable."""
import argparse
import json
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


def symbols(obj, flags):
    output = subprocess.check_output(["nm", flags, obj], text=True)
    return {line.split()[-1] for line in output.splitlines() if line.strip()}


def imports(build):
    defined, undefined = set(), set()
    for module in MODULES:
        obj = build / "modules" / (module + ".o")
        exported = symbols(obj, "-gUj")
        expected = set()
        for path in (HERE / "us-original" / (module + ".exports"), build / "gen" / (module + ".srcsyms")):
            expected.update("_" + name for name in path.read_text().split() if name != "xg_no_exports")
        if exported != expected:
            raise ValueError(f"{module} missing exports {expected - exported}, unexpected {exported - expected}")
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
        lines.extend([f".globl {symbol}", f"{symbol}:", "    brk #0"])
    (build / "gen" / "imports.s").write_text("\n".join(lines) + "\n")
    print(f"{len(standins)} address-only stand-ins for imports from outside these two overlays")


def verify(build):
    objs = json.loads((build / "compile_commands.json").read_text())
    exe = build / "darwin_module_check"
    subprocess.run(["codesign", "--verify", "--strict", exe], check=True)
    with tempfile.TemporaryDirectory() as tmpdir:
        files = []
        for module in MODULES:
            plan = json.loads((HERE / "us-original" / (module + ".json")).read_text())
            image = bytearray(plan["image"]["size"])
            paths = {pc_srcdata.identity(r["output"]): build / r["output"] for r in objs
                     if f"CMakeFiles/game_{module}.dir/" in r["output"]}
            copied = 0
            for rec in plan["objects"]:
                obj = pc_macho.Object(paths[rec["name"]].read_bytes())
                sections = {s.key: s for s in obj.sections}
                for s in rec["sections"]:
                    section = sections[s["name"]]
                    raw = obj.data[section.offset:section.offset + section.size]
                    for dest, source, size in s["copies"]:
                        image[source:source + size] = raw[dest:dest + size]
                        copied += size
            wanted = payloadcheck.windows({module: bytes(image)}, pc_srcdata.MIN)
            if not wanted or not copied or payloadcheck.hits(exe.read_bytes(), wanted, pc_srcdata.MIN):
                raise ValueError(f"{module}: source window in executable or empty source evidence")
            print(f"PASS: {len(paths)} {module} objects, {copied} restored bytes, linked source-window scan")
            path = Path(tmpdir) / module
            path.write_bytes(image)
            files.append(path)
        subprocess.run([exe, *files], check=True)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("action", choices=("imports", "verify"))
    parser.add_argument("build", type=Path)
    args = parser.parse_args()
    (imports if args.action == "imports" else verify)(args.build.resolve())
