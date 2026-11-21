#!/usr/bin/env python3
"""Split the extracted retail files with splat and write build.ninja.

    python tools/configure.py            # every version that has module configs
    python tools/configure.py us         # one version
    python tools/configure.py --force    # re-run splat even if configs are unchanged

For each module listed in config/<ver>/disc.yaml that has a config/<ver>/<mod>.yaml,
splat runs on the merge of:
    config/splat.common.yaml, config/<ver>/splat.yaml,
    build/<ver>/splat/<mod>.paths.yaml (generated here), config/<ver>/<mod>.yaml
The resulting linker scripts give the object list for build.ninja, whose
default target rebuilds every module image and checks its sha1 against the
retail file.
"""

from __future__ import annotations

import argparse
import bisect
import hashlib
import importlib.util
import re
import shutil
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor
from dataclasses import dataclass
from pathlib import Path

import yaml

ROOT = Path(__file__).resolve().parent.parent
CONFIG = ROOT / "config"
TOOLCHAIN = "tools/toolchain"
SPLAT_CACHE_VERSION = "1"  # bump to force every module to re-split


@dataclass
class Module:
    version: str
    name: str
    image: str      # retail file under disc/<ver>/
    sha1: str
    size: int       # retail file size (images are zero-padded up to it)
    config: Path

    @property
    def build(self) -> str:
        return f"build/{self.version}"

    @property
    def linker_script(self) -> str:
        return f"linker/{self.version}/{self.name}.ld"

    def undefined(self, kind: str) -> str:
        return f"linker/{self.version}/{self.name}.undefined_{kind}_auto.txt"


def load_modules(version: str) -> list[Module]:
    manifest = yaml.safe_load((CONFIG / version / "disc.yaml").read_text())
    modules = []
    for entry in manifest["files"]:
        config = CONFIG / version / f"{entry['name']}.yaml"
        if not config.is_file():
            continue
        image = entry["name"] + (".exe" if "iso" in entry else ".bin")
        retail = ROOT / "disc" / version / image
        size = retail.stat().st_size if retail.is_file() else 0
        modules.append(Module(version, entry["name"], image, entry["sha1"], size, config))
    return modules


def overlay_main_symbols(mod: Module) -> str:
    """main.txt for an overlay's split, minus main's names that would cut
    the overlay's own functions or .rodata: main.txt names addresses of other
    overlays loaded at the same base (e.g. the battle code main calls, and the
    <ver> renames tools/share_symbols.py adds for them), and splat starts a
    new function or data label at every name inside a function. Dropped:
    function names inside the overlay's image, names inside .rodata that C
    files own, and any name strictly inside a function the overlay's own
    symbol file declares. Other data names stay (the renames keep splat's
    automatic names from colliding with main's)."""
    main = f"config/{mod.version}/symbols/main.txt"
    segs = yaml.safe_load(mod.config.read_text())["segments"]
    seg = next((s for s in segs if isinstance(s, dict) and "vram" in s), None)
    if seg is None or not (ROOT / main).is_file():
        return main
    vram, end = seg["vram"], seg["vram"] + mod.size
    starts = []
    own = ROOT / f"config/{mod.version}/symbols/{mod.name}.txt"
    if own.is_file():
        for l in own.read_text().splitlines():
            m = SYM_RE.match(l)
            if m and "type:func" in m.group(3) and vram <= int(m.group(2), 16) < end:
                starts.append(int(m.group(2), 16))
    starts.sort()
    # functions end where the module's data starts
    text_end = end
    for sub in seg.get("subsegments", []):
        kind = sub[1] if isinstance(sub, list) and len(sub) > 1 else None
        if kind in ("data", ".data") and starts and seg["vram"] + sub[0] > starts[0]:
            text_end = seg["vram"] + sub[0]
            break

    # .rodata that C files own: splat moves each piece into the INCLUDE_ASM
    # of the function that uses it, and a foreign label inside a jump table
    # keeps part of it from moving (it would be lost).
    subs = [s for s in seg.get("subsegments", []) if isinstance(s, list) and len(s) > 1]
    c_rodata = [(seg["vram"] + a[0], seg["vram"] + b[0]) for a, b in zip(subs, subs[1:])
                if a[1] == ".rodata"]

    def cuts(name: str, addr: int, comment: str) -> bool:
        if not vram <= addr < end:
            return False
        if "type:func" in comment or name.startswith("func_"):
            return True
        if any(lo <= addr < hi for lo, hi in c_rodata):
            return True
        i = bisect.bisect_right(starts, addr) - 1
        return i >= 0 and starts[i] != addr and addr < text_end

    lines = (ROOT / main).read_text().splitlines()
    keep = [l for l in lines
            if not ((m := SYM_RE.match(l)) and cuts(m.group(1), int(m.group(2), 16), m.group(3)))]
    if len(keep) == len(lines):
        return main
    out = ROOT / mod.build / "splat" / f"{mod.name}.main_syms.txt"
    out.parent.mkdir(parents=True, exist_ok=True)
    text = "\n".join(keep) + "\n"
    if not out.is_file() or out.read_text() != text:
        out.write_text(text)
    return str(out.relative_to(ROOT))


def paths_yaml(mod: Module) -> Path:
    """Write the per-module path options splat needs (kept out of the
    hand-edited module config so every module follows one layout)."""
    symbols = [f"config/{mod.version}/symbols/{mod.name}.txt"]
    if mod.name != "main":
        # Overlays call into the executable; share its names.
        symbols.append(overlay_main_symbols(mod))
    options = {
        "basename": mod.name,
        "target_path": f"disc/{mod.version}/{mod.image}",
        "elf_path": f"{mod.build}/{mod.name}.elf",
        "asm_path": f"asm/{mod.version}/{mod.name}",
        "src_path": f"src/{mod.name}",
        "build_path": mod.build,
        "ld_script_path": mod.linker_script,
        "undefined_funcs_auto_path": mod.undefined("funcs"),
        "undefined_syms_auto_path": mod.undefined("syms"),
        "symbol_addrs_path": symbols,
        "cache_path": f"{mod.build}/splat/{mod.name}.cache",
    }
    out = ROOT / mod.build / "splat" / f"{mod.name}.paths.yaml"
    out.parent.mkdir(parents=True, exist_ok=True)
    text = yaml.safe_dump({"options": options}, sort_keys=False)
    if not out.is_file() or out.read_text() != text:
        out.write_text(text)
    return out


def split_inputs(mod: Module, paths: Path) -> list[Path]:
    return [
        CONFIG / "splat.common.yaml",
        CONFIG / mod.version / "splat.yaml",
        paths,
        mod.config,
    ]


def split(mod: Module, force: bool) -> tuple[Module, bool, str]:
    """Run splat for one module unless its inputs are unchanged."""
    paths = paths_yaml(mod)
    configs = split_inputs(mod, paths)
    symbol_files = [ROOT / f"config/{mod.version}/symbols/{mod.name}.txt",
                    ROOT / f"config/{mod.version}/symbols/main.txt"]
    if mod.name != "main":
        symbol_files.append(ROOT / overlay_main_symbols(mod))
    retail = ROOT / "disc" / mod.version / mod.image
    if not retail.is_file():
        return mod, False, f"missing {retail.relative_to(ROOT)}; run tools/extract.py {mod.version}"

    h = hashlib.sha256(SPLAT_CACHE_VERSION.encode())
    for f in configs + symbol_files:
        h.update(f.name.encode())
        h.update(f.read_bytes() if f.is_file() else b"<missing>")
    key = h.hexdigest()
    stamp = ROOT / mod.build / "splat" / f"{mod.name}.stamp"
    if not force and stamp.is_file() and stamp.read_text() == key and (ROOT / mod.linker_script).is_file():
        return mod, True, "unchanged"

    for f in symbol_files[:1]:
        f.parent.mkdir(parents=True, exist_ok=True)
        f.touch()
    # splat never deletes output, so clear the module's old assembly first,
    # and its cache too: the cache ignores symbol-file changes and would keep
    # stale names.
    shutil.rmtree(ROOT / "asm" / mod.version / mod.name, ignore_errors=True)
    (ROOT / mod.build / "splat" / f"{mod.name}.cache").unlink(missing_ok=True)
    cmd = [sys.executable, "-m", "splat", "split", *[str(c.relative_to(ROOT)) for c in configs]]
    proc = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True)
    if proc.returncode != 0:
        return mod, False, (proc.stdout + proc.stderr)[-4000:]
    stamp.write_text(key)
    return mod, True, "split"


SYM_RE = re.compile(r"^\s*(\w+)\s*=\s*(0x[0-9A-Fa-f]+)\s*;(.*)$")


def write_data_symbols(mod: Module) -> str:
    """Linker assignments for data symbols (entries not marked type:func /
    jtbl / label) from the symbol files, so C can reference globals by their
    base address even when no assembly labels them."""
    files = [ROOT / f"config/{mod.version}/symbols/{mod.name}.txt"]
    if mod.name != "main":
        files.append(ROOT / f"config/{mod.version}/symbols/main.txt")
    lines = []
    for f in files:
        if not f.is_file():
            continue
        for line in f.read_text().splitlines():
            m = SYM_RE.match(line)
            # Everything not marked as code is data.
            if m and not re.search(r"type:(func|jtbl|jtbl_label|label)\b", m.group(3)):
                lines.append(f"{m.group(1)} = {m.group(2)};")
    out = ROOT / mod.build / f"{mod.name}.syms.ld"
    text = "\n".join(lines) + "\n"
    if not out.is_file() or out.read_text() != text:
        out.write_text(text)
    return str(out.relative_to(ROOT))


OBJ_RE = re.compile(r"(build/[\w./-]+?\.o)\(")


def fix_bss_address(mod: Module) -> None:
    """splat emits NOLOAD (bss) output sections without an address, so ld
    aligns them to their inputs' 16-byte alignment. Pin each segment's bss
    to the vram of its first bss subsegment (asm bss or a C file's .bss/.sbss)."""
    segments = yaml.safe_load(mod.config.read_text())["segments"]
    script = ROOT / mod.linker_script
    text = script.read_text()
    for seg in segments:
        if not isinstance(seg, dict):
            continue
        bss = [s for s in seg.get("subsegments", [])
               if isinstance(s, dict) and s.get("type") in ("bss", ".bss", ".sbss")]
        if bss:
            name = seg["name"]
            text = re.sub(rf"^(\s*\.{name}_bss) \(NOLOAD\)", rf"\1 {bss[0]['vram']:#X} (NOLOAD)", text, flags=re.M)
    script.write_text(text.replace(" 0X", " 0x"))


def load_presets() -> tuple[dict, list[dict]]:
    cfg = yaml.safe_load((CONFIG / "build.yaml").read_text())
    return cfg["default"], cfg.get("presets") or []


def preset_for(src: str, default: dict, presets: list[dict], version: str | None = None) -> dict:
    """Settings for a C file: the first preset with a matching path prefix
    (relative to src/), and a matching `version` if it names one, overrides
    the default."""
    rel = src.removeprefix("src/")
    for preset in presets:
        if preset.get("version") not in (None, version):
            continue
        if any(rel.startswith(prefix) for prefix in preset["paths"]):
            return {**default, **{k: v for k, v in preset.items() if k not in ("paths", "version")}}
    return default


def objects(mod: Module) -> list[str]:
    seen: dict[str, None] = {}
    for obj in OBJ_RE.findall((ROOT / mod.linker_script).read_text()):
        seen.setdefault(obj)
    return list(seen)


def source_of(mod: Module, obj: str) -> str:
    prefix = mod.build + "/"
    assert obj.startswith(prefix) and obj.endswith(".o"), obj
    return obj[len(prefix):-2]


def write_ninja(versions: list[str], modules: list[Module]) -> None:
    lines = [
        "# Generated by tools/configure.py; do not edit.",
        "ninja_required_version = 1.10",
        f"toolchain = {TOOLCHAIN}",
        "cross = $toolchain/bin/mips-linux-gnu-",
        f"python = {sys.executable}",
        "asflags = -EL -march=r3000 -mtune=r3000 -no-pad-sections -Iinclude",
        "",
        "rule as",
        "  command = ${cross}as $asflags -G0 -o $out $in",
        "  description = AS $in",
        "",
        "rule ld",
        "  command = rm -f $ok && ${cross}ld -EL -nostdlib --no-check-sections -Map $map"
        " -T $script -T $undef_syms -T $undef_funcs -T $data_syms -o $out",
        "  description = LD $out",
        "",
        "rule image",
        "  command = ${cross}objcopy -O binary $in $out && $python tools/pad_image.py $out $size",
        "  description = IMAGE $out",
        "",
        "rule check",
        "  command = rm -f $out && echo '$sha1  $in' | "
        + ("gsha1sum" if sys.platform.startswith("freebsd") else "sha1sum")
        + " --quiet --check - && touch $out",
        "  description = CHECK $in",
        "",
        "rule cpp",
        "  command = cpp -nostdinc -undef -P -D_LANGUAGE_C -DVERSION_$ver -Iinclude"
        " -MMD -MF $out.d -MT $out $in -o $out",
        "  description = CPP $in",
        "  depfile = $out.d",
        "  deps = gcc",
        "",
        "rule cc1",
        "  command = $toolchain/$cc/cc1 $cflags -o $out $in",
        "  description = CC1 $in",
        "",
        "rule maspsx",
        "  command = $python $toolchain/maspsx/maspsx.py --run-assembler --gnu-as-path ${cross}as"
        " $maspsx $asflags $gflag -o $out < $in",
        "  description = MASPSX $in",
        "",
        "rule configure",
        f"  command = $python tools/configure.py {' '.join(versions)}",
        "  description = CONFIGURE",
        "  generator = 1",
        "",
    ]

    default, presets = load_presets()
    checks: list[str] = []
    per_version: dict[str, list[str]] = {}
    regen_deps = {"tools/configure.py", "config/splat.common.yaml", "config/build.yaml"}
    for mod in modules:
        regen_deps.update(str(p.relative_to(ROOT)) for p in (
            CONFIG / mod.version / "disc.yaml", CONFIG / mod.version / "splat.yaml", mod.config))
        for sym in (f"config/{mod.version}/symbols/{mod.name}.txt",):
            regen_deps.add(sym)

        objs = objects(mod)
        fix_bss_address(mod)
        stamp = f"{mod.build}/splat/{mod.name}.stamp"
        for obj in objs:
            src = source_of(mod, obj)
            if src.endswith(".c"):
                # C file: its INCLUDE_ASM lines pull in splat output, so it
                # also depends on the module's split stamp.
                p = preset_for(src, default, presets, mod.version)
                # The assembler's -G follows cc1's unless the preset sets asG: ASPSX
                # can put small data in .sdata/.sbss while cc1 was run with -G0.
                gflag = next((f for f in p["cflags"].split() if f.startswith("-G")), "-G0")
                if "asG" in p:
                    gflag = f"-G{p['asG']}"
                base = obj.removesuffix(".o")
                lines += [
                    f"build {base}.i: cpp {src} | {stamp}",
                    f"  ver = {mod.version.upper()}",
                    f"build {base}.s: cc1 {base}.i",
                    f"  cc = {p['cc']}",
                    f"  cflags = {p['cflags']}",
                    f"build {obj}: maspsx {base}.s | include/macro.inc include/labels.inc",
                    f"  maspsx = {p['maspsx']}",
                    f"  gflag = {gflag}",
                    # INCLUDE_ASM paths are version-neutral (main/nonmatchings/...)
                    f"  asflags = $asflags -Iasm/{mod.version}",
                ]
            else:
                lines.append(f"build {obj}: as {src} | include/macro.inc include/labels.inc")
        elf = f"{mod.build}/{mod.name}.elf"
        img = f"{mod.build}/{mod.image}"
        ok = f"{mod.build}/{mod.name}.ok"
        scripts = [mod.linker_script, mod.undefined("syms"), mod.undefined("funcs"), write_data_symbols(mod)]
        lines += [
            f"build {elf} | {mod.build}/{mod.name}.map: ld {' '.join(objs)} | {' '.join(scripts)}",
            f"  map = {mod.build}/{mod.name}.map",
            f"  script = {scripts[0]}",
            f"  undef_syms = {scripts[1]}",
            f"  undef_funcs = {scripts[2]}",
            f"  data_syms = {scripts[3]}",
            f"  ok = {ok}",
            f"build {img}: image {elf} | tools/pad_image.py",
            f"  size = {mod.size}",
            f"build {ok}: check {img}",
            f"  sha1 = {mod.sha1}",
            f"build {mod.version}/{mod.name}: phony {ok}",
            "",
        ]
        checks.append(ok)
        per_version.setdefault(mod.version, []).append(ok)

    for version, oks in per_version.items():
        lines.append(f"build {version}: phony {' '.join(oks)}")
    lines += [
        f"build build.ninja: configure | {' '.join(sorted(regen_deps))}",
        f"build check: phony {' '.join(checks)}",
        "default check",
        "",
    ]
    (ROOT / "build.ninja").write_text("\n".join(lines))


def normalize_include_paths() -> None:
    """splat writes new C files with INCLUDE_ASM("asm/<ver>/...", f); the C is
    shared between versions, so strip the version directory (the assembler
    gets -Iasm/<ver> instead)."""
    pattern = re.compile(r'(INCLUDE_(?:ASM|RODATA)\(")asm/[a-z]+/')
    for c in (ROOT / "src").rglob("*.c"):
        text = c.read_text()
        new = pattern.sub(r"\1", text)
        if new != text:
            c.write_text(new)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("versions", nargs="*")
    parser.add_argument("--force", action="store_true", help="re-run splat for every module")
    parser.add_argument("-j", "--jobs", type=int, default=8, help="parallel splat runs")
    args = parser.parse_args()
    # split() clears a module's assembly before running splat: check first
    # that this interpreter has splat (the venv's), or every module is wiped.
    if importlib.util.find_spec("splat") is None:
        print(f"{sys.executable} has no splat; activate .venv first", file=sys.stderr)
        return 1

    versions = args.versions or sorted(
        p.name for p in CONFIG.iterdir() if p.is_dir() and (p / "disc.yaml").is_file() and load_modules(p.name))
    modules = [m for v in versions for m in load_modules(v)]
    if not modules:
        print("no module configs found", file=sys.stderr)
        return 1

    failed = False
    # US splits first: splat writes a new C file (full of INCLUDE_ASM) from
    # whichever version splits it first, and the shared C uses US names.
    first = [m for m in modules if m.version == "us"]
    phases = [first, [m for m in modules if m not in first]]
    with ThreadPoolExecutor(max_workers=args.jobs) as pool:
        for phase in phases:
            for mod, ok, msg in pool.map(lambda m: split(m, args.force), phase):
                if ok:
                    if msg != "unchanged":
                        print(f"[{mod.version}] {mod.name}: {msg}")
                else:
                    failed = True
                    print(f"[{mod.version}] {mod.name}: splat FAILED\n{msg}", file=sys.stderr)
    if failed:
        return 1
    normalize_include_paths()

    write_ninja(versions, modules)
    print(f"wrote build.ninja ({len(modules)} modules: {', '.join(versions)})")
    if (ROOT / "port").is_dir():  # the decomp-only tree has no port
        import pc_sources  # the PC port's CMake source lists (port/CMakeLists.txt)
        pc_sources.write()
    return 0


if __name__ == "__main__":
    sys.exit(main())
