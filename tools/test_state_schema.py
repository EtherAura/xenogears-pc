#!/usr/bin/env python3
"""tools/state_schema.py on a small program built here with debug information (gates.sh: state-schema-tests).

A module "testmod" with one variable in xgdata_testmod and one in xgbss_testmod, a struct with
offset-tagged fields (m0_, m4_, m8 without a name), a PC-only field, a pointer and a function pointer, and a
splat label whose name is its retail address; a constant in the module's data (from a file of its own, as a
position-independent build's relocated constants land there). Checks the schema's variables, their retail
addresses and the report's counts, the image's identity (the same once the debug information is stripped);
the schema to hand out (--ship: packed, its exe the image's name) and its check (--verify: against the image,
stripped or not, and refused against another image); an AArch64 object's location in a block of merged globals;
with MinGW, the same program as a Windows (PE/COFF) executable."""
import gzip, json, re, shutil, subprocess, sys, tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SOURCE = r"""
typedef struct { int m0_a; short m4_b; short m8; void* pcExtra; void (*m10_fn)(void); } Thing;
static void tick(void) {}
__attribute__((section("xgdata_testmod"), used)) Thing D_80123456 = {1, 2, 3, 0, tick};
__attribute__((section("xgbss_testmod"), used)) Thing pcOnlyThing;
int main(void) { return D_80123456.m0_a + pcOnlyThing.m4_b; }
"""
KONST = r"""
__attribute__((section("xgdata_testmod"), used)) const int D_80123500[2] = {7, 8};
"""
# A clang build (xg-htn.60.8): the generated data as bytes (gen.c, as tools/pc_link.py writes it), the game C's
# declarations of it in a header (THING from the compile command), and a type the C reaches only through one.
CLANG_HEADER = r"""
typedef struct { int m0_a; void* m8_p; } Holder;
typedef struct { short m0_x; short m2_y; } OnlyDeclared; /* nothing clang describes uses it */
#ifdef THING
extern Holder* D_80123600;
#endif
extern Holder D_80123700[2];
extern Holder* D_80123800[];
extern struct Missing* D_80123900;
extern char D_80123A00[4];
extern OnlyDeclared D_80123B00;
#ifdef ANON
extern struct { void* m0_p; } D_80123C00; /* no name to find its type by */
#endif
"""
CLANG_GAME = r"""
#include "thing.h"
static int use(Holder* h) { return h->m0_a; }
int main(void) {
    return use(D_80123600) + D_80123700[1].m0_a + (D_80123800[2] != 0) + (D_80123900 != 0) + D_80123A00[3]
        + D_80123B00.m2_y
#ifdef ANON
        + (D_80123C00.m0_p != 0)
#endif
        ;
}
"""
CLANG_GEN = r"""
#define D(name, n) __attribute__((section("xgdata_testmod"), used, aligned(16))) unsigned char name[n];
D(D_80123600, 8) D(D_80123700, 32) D(D_80123800, 24) D(D_80123900, 8) D(D_80123A00, 4) D(D_80123B00, 4)
D(D_80123C00, 8)
"""


def clang_checks(base: Path, check) -> None:
    """The clang path: GCC reads the declarations, the image's own types are used, the objects are cached, and
    without GCC (or a game C file GCC cannot compile, the compile commands, or a declared type the image lacks)
    the schema says its states cannot convert, and why."""
    import state_schema as ss
    import statecheck as sc
    d = base / "clang"
    (d / "inc").mkdir(parents=True)
    (d / "CMakeCache.txt").write_text("XG_VERSION:STRING=US\n")
    (d / "inc" / "thing.h").write_text(CLANG_HEADER)
    (d / "game.c").write_text(CLANG_GAME)
    (d / "gen.c").write_text(CLANG_GEN)
    (d / "bad.c").write_text("#ifndef __clang__\n#error a file only clang compiles\n#endif\n")
    def link(*defines, unused_types=True):
        """game.c compiled as the Android build compiles the game C (port/cmake/game-modules.cmake), gen.c, the
        image; and the compile commands, with flags GCC must leave out. DWARF 4, as the NDK writes it (5's
        DW_OP_addrx locations are not read); each source by its full name, as CMake gives it (its unit is then
        this repository's)."""
        flags = ["-DTHING=1", *defines, f"-I{d / 'inc'}", "-std=gnu89", "-Wno-everything",
                 *(["-fno-eliminate-unused-debug-types"] if unused_types else []), "-O2"]
        subprocess.run(["clang", "-gdwarf-4", *flags, "-c", str(d / "game.c"), "-o", "game.o"], cwd=d, check=True)
        subprocess.run(["clang", "-gdwarf-4", "-c", str(d / "gen.c"), "-o", "gen.o"], cwd=d, check=True)
        subprocess.run(["clang", "-gdwarf-4", "-o", "xenogears", "game.o", "gen.o"], cwd=d, check=True)
        game = " ".join(["clang", "--target=x86_64-linux-gnu", "--sysroot=/no/such/sysroot", *flags, "-g", "-c",
                         "game.c", "-o", "CMakeFiles/game_testmod.dir/game.c.o"])
        entries = [{"directory": str(d), "command": game, "file": "game.c",
                    "output": "CMakeFiles/game_testmod.dir/game.c.o"},
                   {"directory": str(d), "arguments": ["clang", "-g", "-c", "gen.c"], "file": "gen.c",
                    "output": "CMakeFiles/gen_testmod.dir/gen.c.o"}]  # (not a game target's: not read)
        (d / "compile_commands.json").write_text(json.dumps(entries))
        return entries
    entries = link()

    def run(*extra):
        r = subprocess.run([sys.executable, str(ROOT / "tools/state_schema.py"), str(d), "--source", str(d), *extra],
                           capture_output=True, text=True)
        return r, (json.load(gzip.open(d / "state_schema.json.gz", "rt")) if r.returncode == 0 else {})

    out, schema = run("--ship", str(d / "xenogears.xgschema"))
    check(out.returncode == 0, f"clang: the tool runs ({out.stderr[-400:]})")
    check(schema.get("portable") is True and "portable_why" not in schema,
          f"clang: portable, with GCC's declarations ({schema.get('portable_why')})")
    T = schema["types"]
    v = {x["name"]: x for x in schema["variables"]}

    def t(name):
        return T.get(str(v[name]["tid"]), {})
    holder = T.get(str(t("D_80123600").get("to")), {})
    check(t("D_80123600").get("k") == "ptr" and holder.get("k") == "struct" and holder.get("size") == 16 and
          [(f["name"], f["off"]) for f in holder["fields"]] == [("m0_a", 0), ("m8_p", 8)],
          f"clang: a pointer to the header's struct, by the compile command's define ({t('D_80123600')}, {holder})")
    dies = ss.read_dies(d / "xenogears", d.resolve())
    key = t("D_80123600").get("to")
    check(int(key, 16) in dies and dies[int(key, 16)]["tag"] == "DW_TAG_structure_type",
          "clang: the struct is the image's own (an entry of its DWARF), not GCC's")
    a = t("D_80123700")
    check(a.get("k") == "arr" and a.get("n") == 2 and a.get("of") == key,
          f"clang: an array of the struct ({a})")
    a = t("D_80123800")
    check(a.get("k") == "arr" and a.get("n") == 3 and T.get(str(a.get("of")), {}).get("k") == "ptr",
          f"clang: an array declared without a size, of pointers: its count from the definition ({a})")
    check(t("D_80123900").get("k") == "ptr" and t("D_80123900").get("to") is None,
          f"clang: a pointer to a type the image lacks stays a pointer, to void ({t('D_80123900')})")
    b = t("D_80123A00")
    check(b.get("k") == "arr" and T.get(str(b.get("of")), {}).get("k") == "int", f"clang: bytes stay bytes ({b})")
    check([(f["name"], f["off"]) for f in t("D_80123B00").get("fields", [])] == [("m0_x", 0), ("m2_y", 2)],
          f"clang: a type reached only through a declaration (-fno-eliminate-unused-debug-types) ({t('D_80123B00')})")
    check(schema.get("retyped") == 5, f"clang: five retyped ({schema.get('retyped')})")
    check("1 game C files (1 compiled, 0 cached)" in out.stdout, "clang: the game C compiled once (not gen.c)")
    shipped = json.loads(sc.schema_text((d / "xenogears.xgschema").read_bytes()))
    check(shipped.get("portable") is True, "clang: the shipped schema says portable too")
    verify = [sys.executable, str(ROOT / "tools/state_schema.py"), "--verify", str(d / "xenogears.xgschema"),
              "--image", str(d / "xenogears"), "--portable"]
    check(subprocess.run(verify, capture_output=True).returncode == 0, "clang: --verify --portable passes")
    out, again = run()
    check("(0 compiled, 1 cached)" in out.stdout and again == schema, "clang: a relink reads the cached declarations")
    (d / "inc" / "thing.h").write_text(CLANG_HEADER + "\n")
    out, again = run()
    check("(1 compiled, 0 cached)" in out.stdout and again == schema, "clang: a header's change compiles it again")
    # without GCC, or a game C file it cannot compile, or the compile commands: portable false, and why
    out, bad = run("--gcc", str(d / "no-such-gcc"), "--ship", str(d / "xenogears.xgschema"))
    check(bad.get("portable") is False and "no GCC" in bad.get("portable_why", "") and "warning" in out.stderr,
          f"clang: no GCC: not portable, and why ({bad.get('portable_why')})")
    check(b"cannot convert" in subprocess.run(verify, capture_output=True).stderr,
          "clang: --verify --portable refuses a schema that says its states cannot convert")
    (d / "compile_commands.json").write_text(json.dumps(entries + [
        {"directory": str(d), "arguments": ["clang", "-c", "bad.c"], "file": "bad.c",
         "output": "CMakeFiles/game_testmod.dir/bad.c.o"}]))
    out, bad = run()
    check(bad.get("portable") is False and "bad.c" in bad.get("portable_why", "") and "only clang" in out.stderr,
          f"clang: a game C file GCC cannot compile is named ({bad.get('portable_why')})")
    link(unused_types=False)  # (clang leaves OnlyDeclared out: D_80123B00 would stay bytes)
    out, bad = run()
    check(bad.get("portable") is False and "D_80123B00: no typedef OnlyDeclared" in bad.get("portable_why", ""),
          f"clang: a declared type clang left out: not portable, and which ({bad.get('portable_why')})")
    link("-DANON=1")
    out, bad = run()
    check(bad.get("portable") is False and "D_80123C00: an anonymous struct" in bad.get("portable_why", ""),
          f"clang: a variable declared by an anonymous struct: not portable ({bad.get('portable_why')})")
    (d / "compile_commands.json").unlink()
    out, bad = run()
    check(bad.get("portable") is False and "compile_commands.json" in bad.get("portable_why", ""),
          f"clang: no compile commands: not portable ({bad.get('portable_why')})")


def main() -> int:
    if not shutil.which("cc") or not (shutil.which("llvm-dwarfdump") or list(Path.home().glob("Android/sdk/ndk/*"))):
        print("state-schema tests: skipped (no C compiler or llvm-dwarfdump)")
        return 0
    base = ROOT / "build"
    base.mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(dir=base, prefix="state_schema_test.") as t:
        d = Path(t)
        (d / "CMakeCache.txt").write_text("XG_VERSION:STRING=US\n")
        src = d / "thing.c"
        src.write_text(SOURCE)
        (d / "konst.c").write_text(KONST)
        subprocess.run(["cc", "-g", "-O0", "-o", str(d / "xenogears"), str(src), str(d / "konst.c")], check=True)
        out = subprocess.run([sys.executable, str(ROOT / "tools/state_schema.py"), str(d), "--source", str(d)],
                             capture_output=True, text=True)
        checks = 0

        def check(cond, what):
            nonlocal checks
            checks += 1
            if not cond:
                print(f"state-schema tests: FAIL {what}\n{out.stdout}{out.stderr}")
                sys.exit(1)

        check(out.returncode == 0, "the tool runs")
        schema = json.load(gzip.open(d / "state_schema.json.gz", "rt"))
        byname = {v["name"]: v for v in schema["variables"]}
        check(set(byname) == {"D_80123456", "pcOnlyThing", "D_80123500"}, f"the module's variables ({sorted(byname)})")
        check(byname["D_80123500"].get("const") is True and "const" not in byname["D_80123456"], "a constant, marked")
        check(byname["D_80123456"]["module"] == "testmod" and byname["D_80123456"]["section"] == "data", "data section")
        check(byname["pcOnlyThing"]["section"] == "bss", "bss section")
        check(byname["D_80123456"]["retail"] == 0x80123456, "a splat label's retail address")
        check(byname["pcOnlyThing"]["retail"] is None, "a PC-only variable has none")
        check(byname["D_80123456"]["size"] == 24, f"the size from DWARF ({byname['D_80123456']['size']})")
        report = out.stdout
        check(re.search(r"game module variables: 3 .*2 with a retail address, 1 without", report) is not None, "the count")
        # pcExtra has no offset tag; m0_a, m4_b and m8 do (m8 without a name yet)
        check(re.search(r"without an offset tag, game types \(need an offset tag\): 1 in 1 types", report) is not None,
              "untagged fields")
        check(re.search(r"^state-schema: 2 of 3 game variables", report, re.M) is not None, "the summary line")
        check("Thing: 1 (pcExtra)" in report, "the untagged field is named")
        check(re.search(r"pointer fields: game 1 data, 1 function", report) is not None, "pointer fields")
        check(any(f["name"] == "tick" for f in schema["functions"]), "functions listed")
        # what a build converting this one's states needs without its executable (statecanon.py)
        sym = {r[0]: r for r in schema.get("symbols", [])}
        check("tick" in sym and sym["tick"][3] in "tT" and sym["tick"][1] == next(
            f["addr"] for f in schema["functions"] if f["name"] == "tick"), "symbols: a function's name, address and kind")
        check(len(schema.get("ro", [])) > 0 and all(lo < hi for lo, hi, _ in schema["ro"]), "read-only ranges")
        # the image it describes, by an identity stripping does not change (the game computes it from itself)
        sys.path.insert(0, str(ROOT / "tools"))
        import state_schema as ss
        stripped = d / "stripped"
        subprocess.run(["objcopy", "--strip-debug", str(d / "xenogears"), str(stripped)], check=True)
        image = schema.get("image", {})
        check(image.get("format") == "elf" and len(image.get("id", "")) == 32 and
              ss.image_identity(stripped)["id"] == image["id"], f"the image's identity, stripped too ({image})")
        # the schema to hand out, and its check against the image it ships with
        import statecheck as sc
        out = subprocess.run([sys.executable, str(ROOT / "tools/state_schema.py"), str(d), "--source", str(d),
                              "--ship", str(d / "xenogears.xgschema"), "--quiet"], capture_output=True, text=True)
        packed = (d / "xenogears.xgschema").read_bytes() if (d / "xenogears.xgschema").exists() else b""
        shipped = json.loads(sc.schema_text(packed)) if packed[:8] == b"XGSCHEMA" else {}
        check(out.returncode == 0 and "describes" not in out.stdout and "game module variables" not in out.stdout,
              "--ship --quiet writes it and leaves the report out")
        check(shipped == {**json.load(gzip.open(d / "state_schema.json.gz", "rt")), "exe": "xenogears"},
              "the shipped schema is the build's with its exe the image's name, packed")
        (d / "strip").mkdir()
        (d / "other").mkdir()
        subprocess.run(["objcopy", "--strip-debug", str(d / "xenogears"), str(d / "strip" / "xenogears")], check=True)

        def verify(image, *extra):
            return subprocess.run([sys.executable, str(ROOT / "tools/state_schema.py"), "--verify",
                                   str(d / "xenogears.xgschema"), "--image", str(image), *extra],
                                  capture_output=True, text=True)
        check(verify(d / "xenogears").returncode == 0, "--verify: the schema describes its image")
        check(verify(d / "xenogears", "--stripped").returncode == 1, "--verify --stripped: refused, debug information")
        check(verify(d / "strip" / "xenogears", "--stripped").returncode == 0, "--verify --stripped: the stripped image")
        subprocess.run(["cc", "-g", "-O1", "-o", str(d / "other" / "xenogears"), str(src), str(d / "konst.c")],
                       check=True)  # (other code: another image)
        check("another image" in verify(d / "other" / "xenogears").stderr, "--verify: another image is refused")
        check(ss.parse_value("DW_OP_addr 0xc06558, DW_OP_plus_uconst 0x18") == ("addr", 0xc06558 + 0x18)
              and ss.parse_value("DW_OP_addr 0x1000") == ("addr", 0x1000),
              "a location in a block of merged globals (AArch64's clang) is its address")
        check(schema.get("portable") is True and not (d / "state_schema_gcc").exists(),
              "a GCC image: portable, its own declarations (no GCC step)")
        if shutil.which("clang") and shutil.which("gcc"):
            clang_checks(d, check)
        else:
            print("state-schema tests: the clang path skipped (no clang or gcc)")
        if shutil.which("x86_64-w64-mingw32-gcc"):  # the Windows executable (MinGW, long section names with -g)
            w = d / "win"
            w.mkdir()
            (w / "CMakeCache.txt").write_text("XG_VERSION:STRING=US\n")
            subprocess.run(["x86_64-w64-mingw32-gcc", "-g", "-O0", "-o", str(w / "xenogears.exe"), str(src),
                            str(d / "konst.c")], check=True)
            out = subprocess.run([sys.executable, str(ROOT / "tools/state_schema.py"), str(w), "--source", str(d)],
                                 capture_output=True, text=True)
            check(out.returncode == 0, "the tool reads a PE executable")
            pe = json.load(gzip.open(w / "state_schema.json.gz", "rt"))
            wv = {v["name"]: v for v in pe["variables"]}
            check(set(wv) == set(byname) and wv["D_80123456"]["size"] == 24 and wv["D_80123500"].get("const"),
                  f"PE: the module's variables from DWARF ({sorted(wv)})")
            sec = pe["sections"].get("testmod.data")
            check(sec is not None and sec[0] <= wv["D_80123456"]["addr"] < sec[1], f"PE: the module's section ({sec})")
            tick = next((f for f in pe["functions"] if f["name"] == "tick"), None)
            sym = {r[0]: r for r in pe["symbols"]}
            check(tick is not None and sym.get("tick", [0, -1])[1] == tick["addr"], "PE: symbols (nm)")
            check(sym.get("D_80123500", [0, 0, 0])[2] == 8, "PE: a variable's symbol sized from DWARF (COFF has none)")
            check(pe["ro"] and all(lo < hi for lo, hi, _ in pe["ro"]), "PE: read-only ranges")
            subprocess.run(["x86_64-w64-mingw32-strip", "--strip-debug", "-o", str(stripped), str(w / "xenogears.exe")],
                           check=True)
            check(pe["image"]["format"] == "pe" and ss.image_identity(stripped)["id"] == pe["image"]["id"],
                  f"PE: the image's identity, stripped too ({pe['image']})")
            # shipped: the read-only ranges where the stripped image has them (its section table shorter, its raw
            # data earlier), so statecanon.py reads a string from the shipped image as from the build's
            out = subprocess.run([sys.executable, str(ROOT / "tools/state_schema.py"), str(w), "--source", str(d),
                                  "--ship", str(w / "xenogears.xgschema"), "--quiet"], capture_output=True, text=True)
            (w / "strip").mkdir()
            image = w / "strip" / "xenogears.exe"
            subprocess.run(["x86_64-w64-mingw32-strip", "--strip-all", "-o", str(image), str(w / "xenogears.exe")],
                           check=True)
            ship = json.loads(sc.schema_text((w / "xenogears.xgschema").read_bytes())) if out.returncode == 0 else {}
            built, shipped_image = (w / "xenogears.exe").read_bytes(), image.read_bytes()
            check(ship.get("ro") == ss.read_only(image) != pe["ro"] and all(
                built[fo:fo + hi - lo] == shipped_image[sfo:sfo + hi - lo]
                for (lo, hi, fo), (_, _, sfo) in zip(pe["ro"], ship["ro"])),
                f"PE: shipped with the stripped image's file offsets ({out.stderr[-300:]})")

            def verify_pe(packed):
                return subprocess.run([sys.executable, str(ROOT / "tools/state_schema.py"), "--verify", str(packed),
                                       "--image", str(image), "--stripped"], capture_output=True, text=True)
            check(verify_pe(w / "xenogears.xgschema").returncode == 0, "PE: --verify --stripped: the shipped image")
            (w / "old.xgschema").write_bytes(sc.schema_file(json.dumps({**ship, "ro": pe["ro"]}).encode()))
            check("file offsets" in verify_pe(w / "old.xgschema").stderr,
                  "PE: --verify: the build's file offsets are refused for the stripped image")
    print(f"state-schema tests: {checks} checks passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
