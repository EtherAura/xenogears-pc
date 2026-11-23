#!/usr/bin/env python3
"""tools/state_schema.py on a small program built here with debug information (gates.sh: state-schema-tests).

A module "testmod" with one variable in xgdata_testmod and one in xgbss_testmod, a struct with
offset-tagged fields (m0_, m4_, m8 without a name), a PC-only field, a pointer and a function pointer, and a
splat label whose name is its retail address. Checks the schema's variables, their retail addresses and the
report's counts."""
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
        subprocess.run(["cc", "-g", "-O0", "-o", str(d / "xenogears"), str(src)], check=True)
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
        check(set(byname) == {"D_80123456", "pcOnlyThing"}, f"the module's variables ({sorted(byname)})")
        check(byname["D_80123456"]["module"] == "testmod" and byname["D_80123456"]["section"] == "data", "data section")
        check(byname["pcOnlyThing"]["section"] == "bss", "bss section")
        check(byname["D_80123456"]["retail"] == 0x80123456, "a splat label's retail address")
        check(byname["pcOnlyThing"]["retail"] is None, "a PC-only variable has none")
        check(byname["D_80123456"]["size"] == 24, f"the size from DWARF ({byname['D_80123456']['size']})")
        report = out.stdout
        check(re.search(r"game module variables: 2 .*1 with a retail address, 1 without", report) is not None, "the count")
        # pcExtra has no offset tag; m0_a, m4_b and m8 do (m8 without a name yet)
        check(re.search(r"without an offset tag, game types \(need an offset tag\): 1 in 1 types", report) is not None,
              "untagged fields")
        check(re.search(r"^state-schema: 1 of 2 game variables", report, re.M) is not None, "the summary line")
        check("Thing: 1 (pcExtra)" in report, "the untagged field is named")
        check(re.search(r"pointer fields: game 1 data, 1 function", report) is not None, "pointer fields")
        check(any(f["name"] == "tick" for f in schema["functions"]), "functions listed")
    print(f"state-schema tests: {checks} checks passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
